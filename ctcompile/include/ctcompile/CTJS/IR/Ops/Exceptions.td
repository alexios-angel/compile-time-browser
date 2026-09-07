// Structured, explicit exception completions for native recovery.
#ifndef CTCOMPILE_CTJS_OPS_EXCEPTIONS_TD
#define CTCOMPILE_CTJS_OPS_EXCEPTIONS_TD

def CTJS_TryOp : CTJS_Op<"try", [
    DeclareOpInterfaceMethods<RegionBranchOpInterface,
      ["getSuccessorInputs", "getRegionInvocationBounds"]>]> {
  let summary = "Execute a protected region and dispatch its exceptional completion";
  let description = [{
    The body ends in try_exit, which selects either the normal result or the
    catch region. Catch arguments receive the thrown value followed by explicit
    state from that throw site. The catch ends in try_yield. Neither the entry
    state nor an unselected completion's operands are evaluated as catch state.
    This operation is recovered transactionally for native admission; it is not
    a claim that arbitrary JavaScript operations cannot throw.
  }];
  let results = (outs CTJS_ValueType:$result);
  let regions = (region SizedRegion<1>:$body, SizedRegion<1>:$catchBody);
  let assemblyFormat = "$body `catch` $catchBody attr-dict `:` type($result)";
  let hasVerifier = 1;
}

def CTJS_TryExitOp : CTJS_Op<"try_exit", [Terminator,
    DeclareOpInterfaceMethods<RegionBranchTerminatorOpInterface,
      ["getMutableSuccessorOperands", "getSuccessorRegions"]>]> {
  let summary = "Select a normal result or a thrown value and catch state";
  let arguments = (ins I1:$isThrow, CTJS_ValueType:$normalResult,
                       Variadic<CTJS_ValueType>:$caughtValues);
  let assemblyFormat = "$isThrow `normal` $normalResult `caught` `(` $caughtValues `)` attr-dict";
  let hasVerifier = 1;
}

def CTJS_TryYieldOp : CTJS_Op<"try_yield", [Terminator,
    DeclareOpInterfaceMethods<RegionBranchTerminatorOpInterface,
      ["getMutableSuccessorOperands"]>]> {
  let summary = "Complete a catch with its normal result";
  let arguments = (ins CTJS_ValueType:$value);
  let assemblyFormat = "$value attr-dict";
  let hasVerifier = 1;
}

def CTJS_InvokeOp : CTJS_Op<"invoke", [
    DeclareOpInterfaceMethods<RegionBranchOpInterface,
      ["getSuccessorInputs", "getRegionInvocationBounds"]>]> {
  let summary = "Dispatch one direct call to normal and exceptional continuations";
  let description = [{
    The body contains exactly one call_direct followed by invoke_exit. On
    normal return, only the normal continuation receives the call result. On
    JavaScript throw, only the unwind continuation executes, receiving the
    thrown value followed by the pre-call state carried by invoke_exit. Its
    implicit payload is not the call's unavailable result. Foreign failures
    do not enter the JavaScript continuation.

    State values must be defined outside the invocation body, before the call.
    Assignments of the call result belong in the normal continuation; the call
    result has no other use. Both continuations end in invoke_yield, supplying
    the operation's results. A continuation can itself complete abruptly, but
    is not protected by this invocation.

    This is an explicit completion representation, not a nonthrowing effect
    claim or permission to compile a source call natively. Native admission
    must separately prove the callee, homogeneous owning payload and complete
    native call component. No source recovery or target lowering is implied.
  }];
  let results = (outs Variadic<CTJS_ValueType>:$results);
  let regions = (region SizedRegion<1>:$body, SizedRegion<1>:$normalBody,
                        SizedRegion<1>:$unwindBody);
  let assemblyFormat = [{
    $body `normal` $normalBody `unwind` $unwindBody attr-dict (`:` type($results)^)?
  }];
  let hasVerifier = 1;
}

def CTJS_InvokeExitOp : CTJS_Op<"invoke_exit", [Terminator,
    DeclareOpInterfaceMethods<RegionBranchTerminatorOpInterface,
      ["getMutableSuccessorOperands"]>]> {
  let summary = "Dispatch a call completion without exposing its result on throw";
  let description = [{
    This is the completion boundary of the immediately preceding call_direct,
    including its exceptional exit before a normal result exists. normalResult
    is selected only on normal return. The unwind successor receives state
    defined outside the invocation body plus an implicit owning thrown value.
    No computation or publication may intervene between call and dispatch.
  }];
  let arguments = (ins CTJS_ValueType:$normalResult,
                       Variadic<CTJS_ValueType>:$state);
  let assemblyFormat = "$normalResult `state` `(` $state `)` attr-dict";
  let hasVerifier = 1;
}

def CTJS_InvokeYieldOp : CTJS_Op<"invoke_yield", [Terminator,
    DeclareOpInterfaceMethods<RegionBranchTerminatorOpInterface,
      ["getMutableSuccessorOperands"]>]> {
  let summary = "Complete a normal or exceptional invocation continuation";
  let description = [{
    Supply every result of the enclosing invoke after the selected continuation
    completes normally. This does not catch a throw from that continuation.
  }];
  let arguments = (ins Variadic<CTJS_ValueType>:$values);
  let assemblyFormat = "`(` $values `)` attr-dict";
  let hasVerifier = 1;
}

#endif
