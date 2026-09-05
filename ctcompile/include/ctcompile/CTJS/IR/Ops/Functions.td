// CTJS functions operations; included by CTJSOps.td.
#ifndef CTCOMPILE_CTJS_OPS_FUNCTIONS_TD
#define CTCOMPILE_CTJS_OPS_FUNCTIONS_TD

// Control flow and completion
//
// NO CTJS BRANCH OPERATIONS. "Use the standard dialects wherever they suffice.
// cf.br, cf.cond_br, and cf.switch cover all bytecode branching."
//===----------------------------------------------------------------------===//

def CTJS_FuncOp : CTJS_Op<"func", [
    IsolatedFromAbove, FunctionOpInterface, Symbol,
    DeclareOpInterfaceMethods<CallableOpInterface>]> {
  let summary = "A JavaScript function: one region, an unstructured CFG";
  let description = [{
    A DISTINCT OPERATION RATHER THAN func.func, because a JavaScript function
    carries closure, receiver, new.target, constructor state, and
    program/function identity that do not belong on a func.func.

    It implements FunctionOpInterface and CallableOpInterface so that MLIR's
    inliner, symbol tables and pass infrastructure work unmodified - which is
    the whole reason for using an interface rather than inventing a parallel
    mechanism.
  }];
  let arguments = (ins SymbolNameAttr:$sym_name,
                       TypeAttrOf<FunctionType>:$function_type,
                       I32Attr:$upvalue_count,
                       OptionalAttr<DictArrayAttr>:$arg_attrs,
                       OptionalAttr<DictArrayAttr>:$res_attrs);
  let regions = (region AnyRegion:$body);
  let hasCustomAssemblyFormat = 1;
  let hasVerifier = 1;
  // NO extraClassDeclaration. MLIR 22 generates getArgumentTypes,
  // getResultTypes and getCallableRegion for a FunctionOpInterface operation
  // itself; declaring them here as the older idiom does is a redefinition.
}

def CTJS_ReturnOp : CTJS_Op<"return", [Terminator, ReturnLike, HasParent<"FuncOp">]> {
  let summary = "Return a value from a JavaScript function";
  // PHASE 55: SINK(returned) - run_loop.cpp:755 writes the caller's register,
  // 754 hands it to a C++ caller (call.cpp:136). Its own reason because 55B
  // reads it: a site that escapes ONLY by return is the std::unique_ptr
  // candidate. NOTE this is a ReturnLike terminator with no result: the
  // analysis applies sinks in a post-fixpoint walk precisely because the
  // sparse framework never calls visitOperation for it.
  let arguments = (ins Arg<CTJS_ValueType, "", [CTJS_SinkReturned]>:$value);
  let assemblyFormat = "$value attr-dict";
}

def CTJS_ThrowOp
    : CTJS_Op<"throw", [Terminator, CTJS_MayThrow, CTJS_MayReenterJS, CTJS_Safepoint]> {
  let summary = "Throw a JavaScript value";
  let description = [{
    RE-ENTERING, which is the most surprising column on its runtime row and is
    real: an uncaught throw formats a diagnostic, and formatting reaches
    to_primitive_string, which calls the thrown object's own toString. Page
    JavaScript runs in the middle of a fatal error.
  }];
  // PHASE 55 (may_reenter 1: throw_value): SINK(thrown) - thrown_ is a root
  // (objects.cpp:705; run_loop.cpp:1196), the value lands in the catching
  // frame's register (vm.hpp:1488), and describe_thrown runs getters with it
  // as `this` (call.cpp:526-543).
  let arguments = (ins Arg<CTJS_ValueType, "", [CTJS_SinkThrown]>:$value);
  let assemblyFormat = "$value attr-dict";
}

def CTJS_ResumeThrowOp : CTJS_Op<"resume_throw", [Terminator]> {
  let summary = "Re-throw the value a handler landed with";
  let description = [{
    What the end of a `finally` block does on the thrown completion: the value
    was carried across the block and is thrown again. Distinct from ctjs.throw
    because it names no operand - the value is the pending completion, not
    something the block computed.
  }];
  let assemblyFormat = "attr-dict";
}

def CTJS_PushHandlerOp : CTJS_Op<"push_handler", [DeclareOpInterfaceMethods<BranchOpInterface>,
                                                  AttrSizedOperandSegments, Terminator]> {
  let summary = "Install an exception handler naming a landing block";
  let description = [{
    A TERMINATOR WITH TWO SUCCESSORS, which is how an unstructured CFG says
    "from here on, a throw goes there": control falls through to $body, and
    the handler block is a successor the unwinder may transfer to. Modelling
    it as an ordinary operation would leave the handler block unreachable in
    the CFG and every pass would treat it as dead.
  }];
  let arguments = (ins Variadic<CTJS_ValueType>:$bodyOperands,
                       Variadic<CTJS_ValueType>:$handlerOperands);
  let successors = (successor AnySuccessor:$body, AnySuccessor:$handler);
  let assemblyFormat = [{
    $body (`(` $bodyOperands^ `:` type($bodyOperands) `)`)?
    `catch` $handler (`(` $handlerOperands^ `:` type($handlerOperands) `)`)? attr-dict
  }];
  let hasVerifier = 1;
}

def CTJS_PopHandlerOp : CTJS_Op<"pop_handler", []> {
  let summary = "Remove the innermost exception handler";
  let description = [{
    Balance is a COMPILER invariant, not a runtime check: the runtime's
    ct_aot_handler_pop takes the globally innermost handler without consulting
    the frame, so a mis-balanced emission silently takes a CALLER's catch. The
    verifier here is what makes that a build error instead.
  }];
  let assemblyFormat = "attr-dict";
  let hasVerifier = 1;
}

//===----------------------------------------------------------------------===//

#endif
