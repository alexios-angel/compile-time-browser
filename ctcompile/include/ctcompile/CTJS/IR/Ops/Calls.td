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
  // PHASE 55 (may_reenter 1: call, call_method, call_computed,
  // call_receiver). ALL SINK(passed): invoke copies every argument into the
  // callee's register window (call.cpp:82-92; a root, objects.cpp:668) and
  // the receiver into call_frame::receiver (call.cpp:127-129; root
  // objects.cpp:683) or current_this_ for a native (call.cpp:58-60; root
  // objects.cpp:672); a native gets a std::vector copy (run_loop.cpp:648-650)
  // and may retain anything - Map.set, Promise.then, addEventListener. The
  // MVP has no callee summaries, so `a.push(i)` is NOT proved confined.
  let arguments = (ins Arg<CTJS_ValueType, "", [CTJS_SinkPassed]>:$callee,
                       Arg<CTJS_ValueType, "", [CTJS_SinkPassed]>:$receiver,
                       Arg<Variadic<CTJS_ValueType>, "", [CTJS_SinkPassed]>:$args);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$callee `(` $receiver (`,` $args^)? `)` attr-dict";
}

def CTJS_ConstructOp
    : CTJS_RuntimeOp<"construct", "ct_aot_construct", CTJS_GenericEffects # [CTJS_MaySuspend]> {
  let summary = "`new callee(...)`";
  // PHASE 55 (may_reenter 1: construct). ALL SINK(passed): new_target is a
  // root (objects.cpp:686), the arguments go through invoke. The instance is
  // made at run_loop.cpp:1099 / call.cpp:507-517 under this operation's pc -
  // an UNCLAIMED site by design, not a boxed-site claim.
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
    VM_CASE(call), just before `frames_.push_back(entered)`; call.cpp,
    context::call, the same two lines). A ctjs.call preceded by
    ctjs.pass_new_target is therefore NOT resolved.

    NOT A CTJS_RuntimeOp, THOUGH ctjs.call IS ONE, and the reason is the ABI
    shape trait: $callee is a proof, not an immediate of ct_aot_call, and the
    trait would count it against that helper's one immediate (argc) - passing
    by coincidence and failing the day a second attribute is added. The plain
    form keeps the trait honest; the boxed lowering names ct_aot_call itself.
  }];
  // PHASE 55: the same row as ctjs.call - ALL SINK(passed), for ctjs.call's
  // reasons (call.cpp:82-92 copies every argument into the callee window, the
  // receiver is call_frame::receiver, a root). Knowing the callee does not
  // change where its arguments go; a callee summary is a later phase's.
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
  // THE FOUR CallOpInterface HOOKS, inline as func.call spells them. Every
  // operand is an argument, in the callee's block-argument order.
  let extraClassDeclaration = [{
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
