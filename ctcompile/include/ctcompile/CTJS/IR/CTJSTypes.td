//===- CTJSTypes.td - the five CTJS types ---------------------------------===//
//
// FIVE, AND THE SMALLNESS IS THE DESIGN. The plan: "Most JavaScript values
// remain generic. This is a deliberate, permanent design choice, not a
// temporary simplification." A large CTJS type lattice would contradict both
// Principle 3 - AOT is a backend of the runtime - and Extra Phase 23, which
// says analysis facts must not become unconditional MLIR types.
//
// None of them carries a parameter, so none needs an assemblyFormat: the
// dialect sets useDefaultTypePrinterParser and they print as `!ctjs.value` and
// its four siblings.
//
//===----------------------------------------------------------------------===//
// THE POLICY'S PHASE 7 ACTION ITEM, ANSWERED. It says: "Verify TypeDef
// usability as a direct type constraint against the pinned MLIR version. If the
// pinned version does not accept a TypeDef record where a Type constraint is
// expected, add explicit Type<CPred<...>> aliases in CTJSTypes.td and use those
// in operand lists. Do not scatter AnyType as a workaround - that silently
// disables verification."
//
// VERIFIED AGAINST MLIR 22.1.8, the pinned version, by generating an operation
// that uses CTJS_ValueType directly in `arguments` and reading what came out:
//
//     if (!((::llvm::isa<::ctcompile::ctjs::ValueType>(type)))) {
//       return op->emitOpError(valueKind) << " must be A generic, boxed
//           ECMAScript value, but got " << type;
//
// A real isa<> check with the summary in the diagnostic. So Phase 8 uses the
// TypeDefs directly and needs no Type<CPred<...>> aliases, and the AnyType
// workaround the policy warns about is not needed either.
#ifndef CTCOMPILE_CTJS_TYPES
#define CTCOMPILE_CTJS_TYPES

include "ctcompile/CTJS/IR/CTJSBase.td"

def CTJS_ValueType : CTJS_Type<"Value", "value"> {
  let summary = "A generic, boxed ECMAScript value";
  let description = [{
    Corresponds exactly to the runtime `script::value` representation.
    Carries no static type information. Almost every CTJS operand and
    result is of this type; that is intentional and is not a deficiency
    to be optimized away by introducing static JavaScript types into the
    type system. Refinements are expressed as guarded operations and
    analysis facts, never as unconditional MLIR types.
  }];
}

// THE SUMMARIES ON THE OTHER FOUR ARE INFERRED, and are marked as such because
// the plan gives text for `value` only. Each is read from what the runtime
// calls the thing and from where the plan uses the name elsewhere; none of
// them changes what the type IS, which is an opaque handle with no parameters.
def CTJS_ContextType : CTJS_Type<"Context", "context"> {
  let summary = "The script::context a compiled body runs against (inferred)";
}

def CTJS_ProgramType : CTJS_Type<"Program", "program"> {
  let summary = "A compiled script::program - the importer's input in Phase 9 (inferred)";
}

def CTJS_ClosureType : CTJS_Type<"Closure", "closure"> {
  let summary = "A function together with the upvalues it captured (inferred)";
}

def CTJS_CoroutineType : CTJS_Type<"Coroutine", "coroutine"> {
  let summary = "A suspended generator or async frame - Phase 14's territory (inferred)";
}

#endif // CTCOMPILE_CTJS_TYPES
