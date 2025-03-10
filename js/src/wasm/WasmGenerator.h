/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 *
 * Copyright 2015 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef wasm_generator_h
#define wasm_generator_h

#include "mozilla/MemoryReporting.h"

#include "jit/MacroAssembler.h"
#include "wasm/WasmCompile.h"
#include "wasm/WasmModule.h"
#include "wasm/WasmValidate.h"

namespace js {
namespace wasm {

struct CompileTask;
typedef Vector<CompileTask*, 0, SystemAllocPolicy> CompileTaskPtrVector;

// FuncCompileInput contains the input for compiling a single function.

struct FuncCompileInput
{
    const uint8_t* begin;
    const uint8_t* end;
    uint32_t       index;
    uint32_t       lineOrBytecode;
    Uint32Vector   callSiteLineNums;

    FuncCompileInput(uint32_t index,
                     uint32_t lineOrBytecode,
                     const uint8_t* begin,
                     const uint8_t* end,
                     Uint32Vector&& callSiteLineNums)
      : begin(begin),
        end(end),
        index(index),
        lineOrBytecode(lineOrBytecode),
        callSiteLineNums(std::move(callSiteLineNums))
    {}
};

typedef Vector<FuncCompileInput, 8, SystemAllocPolicy> FuncCompileInputVector;

// CompiledCode contains the resulting code and metadata for a set of compiled
// input functions or stubs.

struct CompiledCode
{
    Bytes                bytes;
    CodeRangeVector      codeRanges;
    CallSiteVector       callSites;
    CallSiteTargetVector callSiteTargets;
    TrapSiteVectorArray  trapSites;
    CallFarJumpVector    callFarJumps;
    SymbolicAccessVector symbolicAccesses;
    jit::CodeLabelVector codeLabels;

    MOZ_MUST_USE bool swap(jit::MacroAssembler& masm);

    void clear() {
        bytes.clear();
        codeRanges.clear();
        callSites.clear();
        callSiteTargets.clear();
        trapSites.clear();
        callFarJumps.clear();
        symbolicAccesses.clear();
        codeLabels.clear();
        MOZ_ASSERT(empty());
    }

    bool empty() {
        return bytes.empty() &&
               codeRanges.empty() &&
               callSites.empty() &&
               callSiteTargets.empty() &&
               trapSites.empty() &&
               callFarJumps.empty() &&
               symbolicAccesses.empty() &&
               codeLabels.empty();
    }

    size_t sizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) const;
};

// The CompileTaskState of a ModuleGenerator contains the mutable state shared
// between helper threads executing CompileTasks. Each CompileTask started on a
// helper thread eventually either ends up in the 'finished' list or increments
// 'numFailed'.

struct CompileTaskState
{
    CompileTaskPtrVector finished;
    uint32_t             numFailed;
    UniqueChars          errorMessage;

    CompileTaskState() : numFailed(0) {}
    ~CompileTaskState() { MOZ_ASSERT(finished.empty()); MOZ_ASSERT(!numFailed); }
};

typedef ExclusiveWaitableData<CompileTaskState> ExclusiveCompileTaskState;

// A CompileTask holds a batch of input functions that are to be compiled on a
// helper thread as well as, eventually, the results of compilation.

struct CompileTask
{
    const ModuleEnvironment&          env;
    ExclusiveCompileTaskState&        state;
    ExclusiveDeferredValidationState& dvs;
    LifoAlloc                         lifo;
    FuncCompileInputVector            inputs;
    CompiledCode                      output;

    CompileTask(const ModuleEnvironment& env, ExclusiveCompileTaskState& state,
                ExclusiveDeferredValidationState& dvs, size_t defaultChunkSize)
      : env(env),
        state(state),
        dvs(dvs),
        lifo(defaultChunkSize)
    {}

    size_t sizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) const;
};

// A ModuleGenerator encapsulates the creation of a wasm module. During the
// lifetime of a ModuleGenerator, a sequence of FunctionGenerators are created
// and destroyed to compile the individual function bodies. After generating all
// functions, ModuleGenerator::finish() must be called to complete the
// compilation and extract the resulting wasm module.

class MOZ_STACK_CLASS ModuleGenerator
{
    typedef Vector<CompileTask, 0, SystemAllocPolicy> CompileTaskVector;
    typedef Vector<jit::CodeOffset, 0, SystemAllocPolicy> CodeOffsetVector;

    // Constant parameters
    SharedCompileArgs const         compileArgs_;
    UniqueChars* const              error_;
    const Atomic<bool>* const       cancelled_;
    ModuleEnvironment* const        env_;

    // Data that is moved into the result of finish()
    UniqueLinkData                  linkData_;
    UniqueMetadataTier              metadataTier_;
    MutableMetadata                 metadata_;

    // Data scoped to the ModuleGenerator's lifetime
    ExclusiveCompileTaskState       taskState_;
    LifoAlloc                       lifo_;
    jit::JitContext                 jcx_;
    jit::TempAllocator              masmAlloc_;
    jit::WasmMacroAssembler         masm_;
    Uint32Vector                    funcToCodeRange_;
    uint32_t                        debugTrapCodeOffset_;
    CallFarJumpVector               callFarJumps_;
    CallSiteTargetVector            callSiteTargets_;
    uint32_t                        lastPatchedCallSite_;
    uint32_t                        startOfUnpatchedCallsites_;
    CodeOffsetVector                debugTrapFarJumps_;

    // Data accumulated for deferred validation.  Is shared and mutable.
    ExclusiveDeferredValidationState deferredValidationState_;

    // Parallel compilation
    bool                            parallel_;
    uint32_t                        outstanding_;
    CompileTaskVector               tasks_;
    CompileTaskPtrVector            freeTasks_;
    CompileTask*                    currentTask_;
    uint32_t                        batchedBytecode_;

    // Assertions
    DebugOnly<bool>                 finishedFuncDefs_;

    bool allocateGlobalBytes(uint32_t bytes, uint32_t align, uint32_t* globalDataOff);

    bool funcIsCompiled(uint32_t funcIndex) const;
    const CodeRange& funcCodeRange(uint32_t funcIndex) const;
    bool linkCallSites();
    void noteCodeRange(uint32_t codeRangeIndex, const CodeRange& codeRange);
    bool linkCompiledCode(const CompiledCode& code);
    bool finishTask(CompileTask* task);
    bool launchBatchCompile();
    bool finishOutstandingTask();
    bool finishCodegen();
    bool finishMetadataTier();
    UniqueCodeTier finishCodeTier();
    SharedMetadata finishMetadata(const Bytes& bytecode);

    bool isAsmJS() const { return env_->isAsmJS(); }
    Tier tier() const { return env_->tier(); }
    CompileMode mode() const { return env_->mode(); }
    bool debugEnabled() const { return env_->debugEnabled(); }

  public:
    ModuleGenerator(const CompileArgs& args, ModuleEnvironment* env,
                    const Atomic<bool>* cancelled, UniqueChars* error);
    ~ModuleGenerator();
    MOZ_MUST_USE bool init(Metadata* maybeAsmJSMetadata = nullptr);

    // Before finishFuncDefs() is called, compileFuncDef() must be called once
    // for each funcIndex in the range [0, env->numFuncDefs()).

    MOZ_MUST_USE bool compileFuncDef(uint32_t funcIndex, uint32_t lineOrBytecode,
                                     const uint8_t* begin, const uint8_t* end,
                                     Uint32Vector&& callSiteLineNums = Uint32Vector());

    // Must be called after the last compileFuncDef() and before finishModule()
    // or finishTier2().

    MOZ_MUST_USE bool finishFuncDefs();

    // If env->mode is Once or Tier1, finishModule() must be called to generate
    // a new Module. Otherwise, if env->mode is Tier2, finishTier2() must be
    // called to augment the given Module with tier 2 code.

    SharedModule finishModule(const ShareableBytes& bytecode,
                              JS::OptimizedEncodingListener* maybeTier2Listener = nullptr,
                              UniqueLinkData* maybeLinkData = nullptr);
    MOZ_MUST_USE bool finishTier2(const Module& module);

    ExclusiveDeferredValidationState& deferredValidationState() {
        return deferredValidationState_;
    }
};

} // namespace wasm
} // namespace js

#endif // wasm_generator_h
