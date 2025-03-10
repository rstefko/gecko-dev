/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsUserInfo.h"
#include "nsCRT.h"

#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/utsname.h>

#include "nsString.h"
#include "nsReadableUtils.h"
#include "nsNativeCharsetUtils.h"

/* Some UNIXy platforms don't have pw_gecos. In this case we use pw_name */
#if defined(NO_PW_GECOS)
#define PW_GECOS pw_name
#else
#define PW_GECOS pw_gecos
#endif

nsUserInfo::nsUserInfo()
{
}

nsUserInfo::~nsUserInfo()
{
}

NS_IMPL_ISUPPORTS(nsUserInfo,nsIUserInfo)

NS_IMETHODIMP
nsUserInfo::GetFullname(nsAString& aFullname)
{
    struct passwd *pw = nullptr;

    pw = getpwuid (geteuid());

    if (!pw || !pw->PW_GECOS) return NS_ERROR_FAILURE;

#ifdef DEBUG_sspitzer
    printf("fullname = %s\n", pw->PW_GECOS);
#endif

    nsAutoCString fullname(pw->PW_GECOS);

    // now try to parse the GECOS information, which will be in the form
    // Full Name, <other stuff> - eliminate the ", <other stuff>
    // also, sometimes GECOS uses "&" to mean "the user name" so do
    // the appropriate substitution

    // truncate at first comma (field delimiter)
    int32_t index;
    if ((index = fullname.Find(",")) != kNotFound)
        fullname.Truncate(index);

    // replace ampersand with username
    if (pw->pw_name) {
        nsAutoCString username(pw->pw_name);
        if (!username.IsEmpty() && nsCRT::IsLower(username.CharAt(0)))
            username.SetCharAt(nsCRT::ToUpper(username.CharAt(0)), 0);

        fullname.ReplaceSubstring("&", username.get());
    }

    NS_CopyNativeToUnicode(fullname, aFullname);

    return NS_OK;
}

NS_IMETHODIMP
nsUserInfo::GetUsername(nsACString& aUsername)
{
    struct passwd *pw = nullptr;

    // is this portable?  those are POSIX compliant calls, but I need to check
    pw = getpwuid(geteuid());

    if (!pw || !pw->pw_name) return NS_ERROR_FAILURE;

#ifdef DEBUG_sspitzer
    printf("username = %s\n", pw->pw_name);
#endif

    aUsername.Assign(pw->pw_name);

    return NS_OK;
}

NS_IMETHODIMP
nsUserInfo::GetDomain(nsACString& aDomain)
{
    nsresult rv = NS_ERROR_FAILURE;

    struct utsname buf;
    char *domainname = nullptr;

    if (uname(&buf) < 0) {
        return rv;
    }

#if defined(__linux__)
    domainname = buf.domainname;
#endif

    if (domainname && domainname[0]) {
        aDomain.Assign(domainname);
        rv = NS_OK;
    }
    else {
        // try to get the hostname from the nodename
        // on machines that use DHCP, domainname may not be set
        // but the nodename might.
        if (buf.nodename[0]) {
            // if the nodename is foo.bar.org, use bar.org as the domain
            char *pos = strchr(buf.nodename,'.');
            if (pos) {
                aDomain.Assign(pos + 1);
                rv = NS_OK;
            }
        }
    }

    return rv;
}

NS_IMETHODIMP
nsUserInfo::GetEmailAddress(nsACString& aEmailAddress)
{
    // use username + "@" + domain for the email address
    nsresult rv;

    nsCString username;
    nsCString domain;

    rv = GetUsername(username);
    if (NS_FAILED(rv)) return rv;

    rv = GetDomain(domain);
    if (NS_FAILED(rv)) return rv;

    if (username.IsEmpty() || domain.IsEmpty()) {
        return NS_ERROR_FAILURE;
    }

    aEmailAddress = username + NS_LITERAL_CSTRING("@") + domain;
    return NS_OK;
}

