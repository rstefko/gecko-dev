/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef vm_MutexIDs_h
#define vm_MutexIDs_h

#include "threading/Mutex.h"

// Central definition point for mutex ordering.
//
// Mutexes can only be acquired in increasing order. This prevents the
// possibility of deadlock.

#define FOR_EACH_MUTEX(_)             \
  _(TestMutex,                   100) \
  _(ShellContextWatchdog,        100) \
  _(ShellWorkerThreads,          100) \
  _(ShellObjectMailbox,          100) \
                                      \
  _(AtomsTable,                  200) \
                                      \
  _(WasmInitBuiltinThunks,       250) \
  _(WasmLazyStubsTier1,          250) \
  _(WasmLazyStubsTier2,          251) \
                                      \
  _(GlobalHelperThreadState,     300) \
                                      \
  _(GCLock,                      400) \
                                      \
  _(SharedImmutableStringsCache, 500) \
  _(FutexThread,                 500) \
  _(GeckoProfilerStrings,        500) \
  _(ProtectedRegionTree,         500) \
  _(ShellOffThreadState,         500) \
  _(ShellStreamCacheEntryState,  500) \
  _(SimulatorCacheLock,          500) \
  _(Arm64SimulatorLock,          500) \
  _(IonSpewer,                   500) \
  _(PerfSpewer,                  500) \
  _(CacheIRSpewer,               500) \
  _(TraceLoggerThreadState,      500) \
  _(DateTimeInfoMutex,           500) \
  _(ProcessExecutableRegion,     500) \
  _(OffThreadPromiseState,       500) \
  _(BufferStreamState,           500) \
  _(SharedArrayGrow,             500) \
  _(RuntimeScriptData,           500) \
  _(WasmFuncTypeIdSet,           500) \
  _(WasmCodeProfilingLabels,     500) \
  _(WasmCompileTaskState,        500) \
  _(WasmCodeBytesEnd,            500) \
  _(WasmStreamEnd,               500) \
  _(WasmStreamStatus,            500) \
  _(WasmRuntimeInstances,        500) \
  _(WasmSignalInstallState,      500) \
                                      \
  _(IcuTimeZoneStateMutex,       600) \
  _(ThreadId,                    600) \
  _(WasmCodeSegmentMap,          600) \
  _(WasmDeferredValidation,      600) \
  _(TraceLoggerGraphState,       600) \
  _(VTuneLock,                   600)

namespace js {
namespace mutexid {

#define DEFINE_MUTEX_ID(name, order)  \
static const MutexId name { #name, order };
FOR_EACH_MUTEX(DEFINE_MUTEX_ID)
#undef DEFINE_MUTEX_ID

} // namespace mutexid
} // namespace js

#endif // vm_MutexIDs_h
