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

#endif
