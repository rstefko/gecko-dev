/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "LaunchUnelevated.h"

#include "mozilla/Assertions.h"
#include "mozilla/CmdLineAndEnvUtils.h"
#include "mozilla/mscom/COMApartmentRegion.h"
#include "mozilla/RefPtr.h"
#include "nsWindowsHelpers.h"

#include "LauncherResult.h"

// For _bstr_t and _variant_t
#include <comdef.h>
#include <comutil.h>

#include <windows.h>
#include <exdisp.h>
#include <objbase.h>
#include <servprov.h>
#include <shlobj.h>
#include <shobjidl.h>

static mozilla::LauncherResult<TOKEN_ELEVATION_TYPE>
GetElevationType(const nsAutoHandle& aToken)
{
  DWORD retLen;
  TOKEN_ELEVATION_TYPE elevationType;
  if (!::GetTokenInformation(aToken.get(), TokenElevationType, &elevationType,
                             sizeof(elevationType), &retLen)) {
    return LAUNCHER_ERROR_FROM_LAST();
  }

  return elevationType;
}

static mozilla::LauncherResult<bool>
IsHighIntegrity(const nsAutoHandle& aToken)
{
  DWORD reqdLen;
  if (!::GetTokenInformation(aToken.get(), TokenIntegrityLevel, nullptr, 0,
                             &reqdLen)) {
    DWORD err = ::GetLastError();
    if (err != ERROR_INSUFFICIENT_BUFFER) {
      return LAUNCHER_ERROR_FROM_WIN32(err);
    }
  }

  auto buf = mozilla::MakeUnique<char[]>(reqdLen);

  if (!::GetTokenInformation(aToken.get(), TokenIntegrityLevel, buf.get(),
                             reqdLen, &reqdLen)) {
    return LAUNCHER_ERROR_FROM_LAST();
  }

  auto tokenLabel = reinterpret_cast<PTOKEN_MANDATORY_LABEL>(buf.get());

  DWORD subAuthCount = *::GetSidSubAuthorityCount(tokenLabel->Label.Sid);
  DWORD integrityLevel = *::GetSidSubAuthority(tokenLabel->Label.Sid,
                                               subAuthCount - 1);
  return integrityLevel > SECURITY_MANDATORY_MEDIUM_RID;
}

static mozilla::LauncherResult<HANDLE>
GetMediumIntegrityToken(const nsAutoHandle& aProcessToken)
{
  HANDLE rawResult;
  if (!::DuplicateTokenEx(aProcessToken.get(), 0, nullptr,
                          SecurityImpersonation, TokenPrimary, &rawResult)) {

    return LAUNCHER_ERROR_FROM_LAST();
  }

  nsAutoHandle result(rawResult);

  BYTE mediumIlSid[SECURITY_MAX_SID_SIZE];
  DWORD mediumIlSidSize = sizeof(mediumIlSid);
  if (!::CreateWellKnownSid(WinMediumLabelSid, nullptr, mediumIlSid,
                            &mediumIlSidSize)) {
    return LAUNCHER_ERROR_FROM_LAST();
  }

  TOKEN_MANDATORY_LABEL integrityLevel = {};
  integrityLevel.Label.Attributes = SE_GROUP_INTEGRITY;
  integrityLevel.Label.Sid = reinterpret_cast<PSID>(mediumIlSid);

  if (!::SetTokenInformation(rawResult, TokenIntegrityLevel, &integrityLevel,
                             sizeof(integrityLevel))) {
    return LAUNCHER_ERROR_FROM_LAST();
  }

  return result.disown();
}

namespace mozilla {

// If we're running at an elevated integrity level, re-run ourselves at the
// user's normal integrity level. We do this by locating the active explorer
// shell, and then asking it to do a ShellExecute on our behalf. We do it this
// way to ensure that the child process runs as the original user in the active
// session; an elevated process could be running with different credentials than
// those of the session.
// See https://blogs.msdn.microsoft.com/oldnewthing/20131118-00/?p=2643

LauncherVoidResult
LaunchUnelevated(int aArgc, wchar_t* aArgv[])
{
  // We require a single-threaded apartment to talk to Explorer.
  mscom::STARegion sta;
  if (!sta.IsValid()) {
    return LAUNCHER_ERROR_FROM_HRESULT(sta.GetHResult());
  }

  // NB: Explorer is a local server, not an inproc server
  RefPtr<IShellWindows> shellWindows;
  HRESULT hr = ::CoCreateInstance(CLSID_ShellWindows, nullptr,
                                  CLSCTX_LOCAL_SERVER, IID_IShellWindows,
                                  getter_AddRefs(shellWindows));
  if (FAILED(hr)) {
    return LAUNCHER_ERROR_FROM_HRESULT(hr);
  }

  // 1. Find the shell view for the desktop.
  _variant_t loc(CSIDL_DESKTOP);
  _variant_t empty;
  long hwnd;
  RefPtr<IDispatch> dispDesktop;
  hr = shellWindows->FindWindowSW(&loc, &empty, SWC_DESKTOP, &hwnd,
                                  SWFO_NEEDDISPATCH, getter_AddRefs(dispDesktop));
  if (FAILED(hr)) {
    return LAUNCHER_ERROR_FROM_HRESULT(hr);
  }

  RefPtr<IServiceProvider> servProv;
  hr = dispDesktop->QueryInterface(IID_IServiceProvider, getter_AddRefs(servProv));
  if (FAILED(hr)) {
    return LAUNCHER_ERROR_FROM_HRESULT(hr);
  }

  RefPtr<IShellBrowser> browser;
  hr = servProv->QueryService(SID_STopLevelBrowser, IID_IShellBrowser,
                              getter_AddRefs(browser));
  if (FAILED(hr)) {
    return LAUNCHER_ERROR_FROM_HRESULT(hr);
  }

  RefPtr<IShellView> activeShellView;
  hr = browser->QueryActiveShellView(getter_AddRefs(activeShellView));
  if (FAILED(hr)) {
    return LAUNCHER_ERROR_FROM_HRESULT(hr);
  }

  // 2. Get the automation object for the desktop.
  RefPtr<IDispatch> dispView;
  hr = activeShellView->GetItemObject(SVGIO_BACKGROUND, IID_IDispatch,
                                      getter_AddRefs(dispView));
  if (FAILED(hr)) {
    return LAUNCHER_ERROR_FROM_HRESULT(hr);
  }

  RefPtr<IShellFolderViewDual> folderView;
  hr = dispView->QueryInterface(IID_IShellFolderViewDual,
                                getter_AddRefs(folderView));
  if (FAILED(hr)) {
    return LAUNCHER_ERROR_FROM_HRESULT(hr);
  }

  // 3. Get the interface to IShellDispatch2
  RefPtr<IDispatch> dispShell;
  hr = folderView->get_Application(getter_AddRefs(dispShell));
  if (FAILED(hr)) {
    return LAUNCHER_ERROR_FROM_HRESULT(hr);
  }

  RefPtr<IShellDispatch2> shellDisp;
  hr = dispShell->QueryInterface(IID_IShellDispatch2, getter_AddRefs(shellDisp));
  if (FAILED(hr)) {
    return LAUNCHER_ERROR_FROM_HRESULT(hr);
  }

  // 4. Now call IShellDispatch2::ShellExecute to ask Explorer to re-launch us.

  // Omit argv[0] because ShellExecute doesn't need it in params
  UniquePtr<wchar_t[]> cmdLine(MakeCommandLine(aArgc - 1, aArgv + 1));
  if (!cmdLine) {
    return LAUNCHER_ERROR_GENERIC();
  }

  _bstr_t exe(aArgv[0]);
  _variant_t args(cmdLine.get());
  _variant_t operation(L"open");
  _variant_t directory;
  _variant_t showCmd(SW_SHOWNORMAL);
  hr = shellDisp->ShellExecute(exe, args, operation, directory, showCmd);
  if (FAILED(hr)) {
    return LAUNCHER_ERROR_FROM_HRESULT(hr);
  }

  return Ok();
}

LauncherResult<ElevationState>
GetElevationState(mozilla::LauncherFlags aFlags, nsAutoHandle& aOutMediumIlToken)
{
  aOutMediumIlToken.reset();

  const DWORD tokenFlags = TOKEN_QUERY | TOKEN_DUPLICATE |
                           TOKEN_ADJUST_DEFAULT | TOKEN_ASSIGN_PRIMARY;
  HANDLE rawToken;
  if (!::OpenProcessToken(::GetCurrentProcess(), tokenFlags, &rawToken)) {
    return LAUNCHER_ERROR_FROM_LAST();
  }

  nsAutoHandle token(rawToken);

  LauncherResult<TOKEN_ELEVATION_TYPE> elevationType = GetElevationType(token);
  if (elevationType.isErr()) {
    return LAUNCHER_ERROR_FROM_RESULT(elevationType);
  }

  switch (elevationType.unwrap()) {
    case TokenElevationTypeLimited:
      return ElevationState::eNormalUser;
    case TokenElevationTypeFull:
      // If we want to start a non-elevated browser process and wait on it,
      // we're going to need a medium IL token.
      if ((aFlags & (mozilla::LauncherFlags::eWaitForBrowser |
                     mozilla::LauncherFlags::eNoDeelevate)) ==
          mozilla::LauncherFlags::eWaitForBrowser) {
        LauncherResult<HANDLE> tokenResult =
          GetMediumIntegrityToken(token);
        if (tokenResult.isOk()) {
          aOutMediumIlToken.own(tokenResult.unwrap());
        } else {
          return LAUNCHER_ERROR_FROM_RESULT(tokenResult);
        }
      }

      return ElevationState::eElevated;
    case TokenElevationTypeDefault:
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Was a new value added to the enumeration?");
      return LAUNCHER_ERROR_GENERIC();
  }

  // In this case, UAC is disabled. We do not yet know whether or not we are
  // running at high integrity. If we are at high integrity, we can't relaunch
  // ourselves in a non-elevated state via Explorer, as we would just end up in
  // an infinite loop of launcher processes re-launching themselves.

  LauncherResult<bool> isHighIntegrity = IsHighIntegrity(token);
  if (isHighIntegrity.isErr()) {
    return LAUNCHER_ERROR_FROM_RESULT(isHighIntegrity);
  }

  if (!isHighIntegrity.unwrap()) {
    return ElevationState::eNormalUser;
  }

  if (!(aFlags & mozilla::LauncherFlags::eNoDeelevate)) {
    LauncherResult<HANDLE> tokenResult =
      GetMediumIntegrityToken(token);
    if (tokenResult.isOk()) {
      aOutMediumIlToken.own(tokenResult.unwrap());
    } else {
      return LAUNCHER_ERROR_FROM_RESULT(tokenResult);
    }
  }

  return ElevationState::eHighIntegrityNoUAC;
}

} // namespace mozilla

