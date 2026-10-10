//===- CTJSTypes.td - the five CTJS types ---------------------------------===//
//
// Five opaque types, kept few on purpose: almost every value is a generic
// boxed !ctjs.value, because AOT code is a backend of the same runtime and
// analysis facts are attached as guarded operations or attributes, never
// baked into unconditional MLIR types.
//
// None of them carries a parameter, so none needs an assemblyFormat: the
// dialect sets useDefaultTypePrinterParser and they print as `!ctjs.value` and
// its four siblings.
//
//===----------------------------------------------------------------------===//
// A TypeDef works directly as an operand constraint: the generated verifier is
// a real isa<> check that prints the type's summary, for example
//
//     if (!((::llvm::isa<::ctcompile::ctjs::ValueType>(type)))) {
//       return op->emitOpError(valueKind) << " must be A generic, boxed
//           ECMAScript value, but got " << type;
//
// So operands use these TypeDefs directly, with no Type<CPred<...>> aliases,
// and never AnyType, which would silently disable verification.
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

// The summaries of the other four are marked (inferred): they are taken from
// what the runtime calls each thing. None of them changes what the type is, an
// opaque handle with no parameters.
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
