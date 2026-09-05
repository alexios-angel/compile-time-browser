// CTJS frames operations; included by CTJSOps.td.
#ifndef CTCOMPILE_CTJS_OPS_FRAMES_TD
#define CTCOMPILE_CTJS_OPS_FRAMES_TD

// Frames and rooting
//===----------------------------------------------------------------------===//

def CTJS_FrameEnterOp : CTJS_RuntimeOp<"frame_enter", "ct_aot_enter", [CTJS_MayThrow]> {
  let summary = "Establish the AOT shadow frame";
  let description = [{
    May throw, and only that: the depth guard raises when the frame stack is
    exhausted, and the row says the caller returns FAILED without leaving. It
    does not re-enter and is not a safepoint - nothing inside it allocates.

    ITS FAILURE HAS A THIRD SHAPE and no backend may treat it as either of the
    other two. ct_aot_enter returns NULL on the depth raise - not a status to
    test, not a poll to defer - which is why RuntimeHelpers.hpp carves it out
    of both tiers by name. A status compare here would be comparing a pointer
    against an enumerator, and the next helper call would dereference null.

    $reg_count IS THE FRAME'S REGISTER WINDOW, and it is carried here because
    nothing else in the IR knows it. The importer reads proto.frame_size to
    seed its register file and then dropped the number; ct_aot_enter needs it,
    and a backend that guessed param_count instead would size the window to
    the parameters and leave every local without a slot.

    THE OTHER FOUR ARGUMENTS ARE NOT OPERANDS AND WILL NOT BECOME ANY. `ctx`
    and `site` are parameters of ct_aot_entry_fn, `receiver` is another, and
    `storage` is CT_AOT_FRAME_BYTES of caller-allocated space that exists only
    in the emitted body. All four exist at the entry and nowhere else in the
    IR, so this operation is lowered by the pattern that builds the entry
    rather than by a generic one.
  }];
  let arguments = (ins I32Attr:$reg_count);
  let results = (outs CTJS_ContextType:$context);
  let assemblyFormat = "$reg_count attr-dict";
}

// PHASE 55: no tracked operand (the context is not a value); NEITHER.
def CTJS_FrameExitOp : CTJS_RuntimeOp<"frame_exit", "ct_aot_leave", [CTJS_NoEscapeEffects]> {
  let summary = "Release the AOT shadow frame";
  let description = [{
    Infallible, and it must NOT run on the unwound path - the unwinder has
    already destroyed the frame, and leaving again would pop somebody else's.
    That is a lowering obligation rather than something ODS can express.
  }];
  let arguments = (ins CTJS_ContextType:$context);
  let assemblyFormat = "$context attr-dict";
}

// PHASE 55: NEITHER - it parks the value into THIS frame's own register
// window, which dies with the frame.
def CTJS_RootOp : CTJS_Op<"root", [CTJS_NoEscapeEffects]> {
  let summary = "Keep a value reachable across a safepoint";
  let description = [{
    CONSERVATIVE, and Phase 29 refines it. The collector is precise and walks
    only the roots it knows; a value living in a native frame is reachable
    from none of them, so anything live across an operation carrying
    CTJS_Safepoint has to be parked where the collector can see it.

    NOT a CTJS_RuntimeOp: the runtime's ct_aot_slots hands back the frame's
    register window and the STORE is generated code, so there is no single
    helper this lowers to.
  }];
  let arguments = (ins CTJS_ContextType:$context, CTJS_ValueType:$value);
  let assemblyFormat = "$value `in` $context attr-dict";
}

#endif
