// CTJS calls operations; included by CTJSOps.td.
#ifndef CTCOMPILE_CTJS_OPS_CALLS_TD
#define CTCOMPILE_CTJS_OPS_CALLS_TD

// Calls
//===----------------------------------------------------------------------===//

def CTJS_CallOp
    : CTJS_RuntimeOp<"call", "ct_aot_call", CTJS_GenericEffects # [CTJS_MaySuspend]> {
  let summary = "Invoke a callee through the mixed-mode dispatcher";
  let description = [{
    THROUGH THE DISPATCHER, never around it. The callee may be interpreted, a
    compiled body, or a native built-in, and which of those it is is decided
    at run time by the one place that reads function_proto::aot_entry.
  }];
  // Escape effects (may_reenter 1: call, call_method, call_computed,
  // call_receiver): every operand is SINK(passed). context::invoke
  // (vm/call/invoke.cpp) copies each argument into the callee's register window
  // and the receiver into call_frame::receiver, or into current_this_ for a
  // native; all of these are GC roots through each_root. A native callee gets
  // its own std::vector copy of the arguments (execute_call in
  // vm/run_loop/call.cpp) and may retain any of them (Map.set, Promise.then,
  // addEventListener). There are no callee summaries yet, so `a.push(i)` is not
  // proved confined.
  let arguments = (ins Arg<CTJS_ValueType, "", [CTJS_SinkPassed]>:$callee,
                       Arg<CTJS_ValueType, "", [CTJS_SinkPassed]>:$receiver,
                       Arg<Variadic<CTJS_ValueType>, "", [CTJS_SinkPassed]>:$args);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$callee `(` $receiver (`,` $args^)? `)` attr-dict";
}

def CTJS_ConstructOp
    : CTJS_RuntimeOp<"construct", "ct_aot_construct", CTJS_GenericEffects # [CTJS_MaySuspend]> {
  let summary = "`new callee(...)`";
  // Escape effects (may_reenter 1: construct): every operand is SINK(passed).
  // new_target is a GC root and the arguments go through context::invoke as
  // for ctjs.call. The new instance is allocated by make_instance
  // (vm/call/construct.cpp) under this operation's pc; that site is
  // deliberately left unclaimed by escape analysis rather than claimed as a
  // boxed allocation.
  let arguments = (ins Arg<CTJS_ValueType, "", [CTJS_SinkPassed]>:$callee,
                       Arg<CTJS_ValueType, "", [CTJS_SinkPassed]>:$new_target,
                       Arg<Variadic<CTJS_ValueType>, "", [CTJS_SinkPassed]>:$args);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$callee `(` $new_target (`,` $args^)? `)` attr-dict";
}

def CTJS_CallDirectOp
    : CTJS_Op<"call_direct", [CallOpInterface, DeclareOpInterfaceMethods<SymbolUserOpInterface>]
                             # CTJS_GenericEffects # [CTJS_MaySuspend]> {
  let summary = "Invoke a ctjs.func the closed world has named - Phase 62½-A";
  let description = [{
    WHAT --ctjs-resolve-globals REWRITES A ctjs.call INTO when it can prove
    who is called. The importer's IR names nobody: `add(x, x)` is a
    ctjs.load_global and a ctjs.call through the dispatcher, and nothing in the
    module says which ctjs.func that is. This operation says it, as a symbol,
    and MLIR's CallOpInterface is what makes the statement usable - the
    dataflow framework, the call graph and the inliner all read it.

    THE OPERANDS ARE THE CALLEE'S ENTRY BLOCK, IN ORDER. ctjs.func's entry
    block takes the three implicit arguments first - receiver, new.target,
    callee - and then the declared parameters (BytecodeImport.cpp,
    `implicit_arguments`), so getArgOperands() returns EVERY operand as it
    stands: `(receiver, new_target, callee_value, args...)` lines up with
    `(%arg0, %arg1, %arg2, %arg3...)` with no reordering. The verifier holds
    the two counts equal, and the resolver pads a short call with
    `ctjs.constant #ctjs.undefined` the way op::call fills missing parameters
    (run_loop.cpp, VM_CASE(call): `registers_[new_base + i] = undefined` for
    i in [argc, param_count)).

    $callee_value IS KEPT, and it is not decoration. The boxed tier lowers
    this exactly as ctjs.call - through ct_aot_call with the callee VALUE -
    because that tier does not need the symbol; only the native tier calls
    the symbol. Dropping the value would leave the boxed tier nothing to
    dispatch on.

    $new_target IS UNDEFINED FOR A PLAIN CALL, and the resolver materialises
    that constant rather than leaving the operand implicit: op::call pushes
    its frame with `entered.new_target = pending_new_target_` and that root is
    undefined unless a pass_new_target or a construct set it (run_loop.cpp,
    VM_CASE(call), just before `frames_.push_back(entered)`; vm/call/invoke.cpp,
    context::invoke, the same two lines). A ctjs.call preceded by
    ctjs.pass_new_target is therefore NOT resolved.

    NOT A CTJS_RuntimeOp, THOUGH ctjs.call IS ONE, and the reason is the ABI
    shape trait: $callee is a proof, not an immediate of ct_aot_call, and the
    trait would count it against that helper's one immediate (argc) - passing
    by coincidence and failing the day a second attribute is added. The plain
    form keeps the trait honest; the boxed lowering names ct_aot_call itself.
  }];
  // Escape effects are the same as ctjs.call: every operand is SINK(passed).
  // Knowing the callee does not change where its arguments go; proving less
  // from the callee's body would need callee summaries, which do not exist
  // yet.
  let arguments = (ins FlatSymbolRefAttr:$callee,
                       Arg<CTJS_ValueType, "", [CTJS_SinkPassed]>:$receiver,
                       Arg<CTJS_ValueType, "", [CTJS_SinkPassed]>:$new_target,
                       Arg<CTJS_ValueType, "", [CTJS_SinkPassed]>:$callee_value,
                       Arg<Variadic<CTJS_ValueType>, "", [CTJS_SinkPassed]>:$args,
                       OptionalAttr<DictArrayAttr>:$arg_attrs,
                       OptionalAttr<DictArrayAttr>:$res_attrs);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = [{
    $callee `(` $receiver `,` $new_target `,` $callee_value (`,` $args^)? `)` attr-dict
  }];
  // The four CallOpInterface hooks, inline as func.call spells them. Every
  // operand is an argument, in the callee's block-argument order.
  let extraClassDeclaration = [{
    /// The ctjs.func the symbol names, or null when it names nothing.
    ::ctcompile::ctjs::FuncOp getTarget();
    ::mlir::Operation::operand_range getArgOperands() { return getOperation()->getOperands(); }
    ::mlir::MutableOperandRange getArgOperandsMutable() {
      return ::mlir::MutableOperandRange(getOperation());
    }
    ::mlir::CallInterfaceCallable getCallableForCallee() {
      return ::mlir::SymbolRefAttr(getCalleeAttr());
    }
    void setCalleeFromCallable(::mlir::CallInterfaceCallable callee) {
      setCalleeAttr(::mlir::cast<::mlir::FlatSymbolRefAttr>(
          ::mlir::cast<::mlir::SymbolRefAttr>(callee)));
    }
  }];
}

//===----------------------------------------------------------------------===//

#endif
