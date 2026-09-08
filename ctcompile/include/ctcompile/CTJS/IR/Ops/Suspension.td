// CTJS suspension operations; included by CTJSOps.td.
#ifndef CTCOMPILE_CTJS_OPS_SUSPENSION_TD
#define CTCOMPILE_CTJS_OPS_SUSPENSION_TD

// Async and suspension - Phase 14
//
// ONE OF THE THREE IS NOT A SUSPENSION POINT, and that is why it lowers while
// the other two do not. ctjs.wrap_promise is what an async function's `return`
// does; the frame is never lifted, nothing is saved, and an async function
// containing no `await` carries this opcode and no await_value at all.
//===----------------------------------------------------------------------===//

def CTJS_WrapPromiseOp
    : CTJS_RuntimeOp<"wrap_promise", "ct_aot_wrap_promise", [CTJS_MayThrow, CTJS_Safepoint]> {
  let summary = "What an async function's `return` hands back";
  let description = [{
    NOT CTJS_GenericEffects. The runtime row is `may_reenter 0`, verified
    against the INSTALLED promise factory rather than the VM stub -
    detail::make_promise is a table, a prototype, four property writes and an
    array, with no call() anywhere - so CTJS_MayReenterJS would be a lie the
    ABI shape trait does not check. ctjs.create_cell is the pattern.

    THREE FACTS A FOLD MUST NOT LOSE, all of them the runtime's:

    * The already-a-promise test is `is_object()` EXACTLY, which is
      heap_kind::object and nothing else - so an array, a function or a proxy
      returned from an async function is ALWAYS re-wrapped.
    * With no promise factory installed make_promise is the IDENTITY, so this
      is not "the result is always an object".
    * The shape test is an own `__value`; is_pending_promise's is `__settled`.
      Two adjacent runtime handlers use two DIFFERENT tests for "is a promise"
      on purpose, and unifying them changes what
      `return somePendingPromise` does inside an async function.

    RAISE TIER: the helper answers the value rather than a status, so there is
    no exception edge - the allocation ceiling raises and still returns a
    well-formed object, and a caller polls ct_aot_failed at a back edge. It
    allocates, so the result is parked like every other value.
  }];
  // PHASE 55: SINK(stored) - stored as the promise's `__value`
  // (builtins/internal.hpp:262, 342) or returned as-is.
  let arguments = (ins Arg<CTJS_ValueType, "", [CTJS_SinkStored]>:$value);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$value attr-dict";
}

def CTJS_SuspendOp
    : CTJS_Op<"suspend", CTJS_GenericEffects # [CTJS_MaySuspend]> {
  let summary = "await, yield, or yield*";
  let description = [{
    Defined now and lowered in Phase 14. Defining it early "keeps the importer
    from needing a second structural pass later" - a function containing one
    is structurally different from one that does not, and discovering that
    after the importer exists would mean rewriting it.
  }];
  // PHASE 55: $value SINK(stored), AND A WHOLE-FUNCTION REFUSAL (R4,
  // `suspended`) in EscapeAnalysis.cpp: the entire register window is copied
  // into a coroutine_object (run_loop.cpp:866-867 await, 922-923 yield;
  // vm.hpp:1160-1192) and traced from the heap (trace_object's coroutine arm,
  // vm/objects/gc.cpp), so every
  // site of the function outlives its frame. The importer already refuses
  // every suspension point (BytecodeImport.cpp); the analysis's line is for
  // hand-written IR.
  let arguments = (ins CTJS_SuspendKind:$kind, Arg<CTJS_ValueType, "", [CTJS_SinkStored]>:$value);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$kind $value attr-dict";
}

def CTJS_ResumePointOp : CTJS_Op<"resume_point", []> {
  let summary = "A numbered point a suspended function can resume at";
  let arguments = (ins I32Attr:$index);
  let assemblyFormat = "$index attr-dict";
  let hasVerifier = 1;
}

//===----------------------------------------------------------------------===//

#endif
