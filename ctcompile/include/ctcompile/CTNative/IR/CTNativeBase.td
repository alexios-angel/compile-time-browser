//===- CTNativeBase.td - The ctnative dialect and its base classes --------===//
//
// ctnative is a second dialect because the first, CTJS, deliberately has
// almost no types: in CTJS, refinements are guarded operations and analysis
// facts, never unconditional MLIR types (see CTJSTypes.td).
//
// ctnative is where a refinement becomes a type, and the two cannot be one
// dialect because CTJS types are unconditional. A `!ctjs.value` is true of
// every JavaScript value. A `!ctnative.num<i32>` is true only where a proof
// says so. A function compiles to native C++ only when the compiler can prove
// it is in this static subset; everything else falls back to the value-boxed
// CTJS backend. So every type below is a claim with a proof obligation, and
// the top of the lattice, `!ctnative.boxed`, is the honest answer that hands
// the function back to CTJS.
//
//===----------------------------------------------------------------------===//
#ifndef CTCOMPILE_CTNATIVE_BASE
#define CTCOMPILE_CTNATIVE_BASE

include "mlir/IR/OpBase.td"
include "mlir/IR/AttrTypeBase.td"

def CTNative_Dialect : Dialect {
  let name = "ctnative";
  let cppNamespace = "::ctcompile::ctnative";
  let summary = "Statically typed JavaScript, for the idiomatic C++ backend";
  let description = [{
    CTNative sits between CTJS and the C++ emitter and carries the STATIC
    types CTJS deliberately does not. Where CTJS has one boxed
    `!ctjs.value`, CTNative has a lattice - bottom, the numeric chain, the
    strings, the containers, and `!ctnative.boxed` at the top.

    Reaching the top is not a failure mode to be optimized away. It is the
    fallback the whole design rests on: a value that reaches `boxed` is a
    value the native backend refuses, and the refusal is recorded so the
    subset can grow one proof at a time.

    WHEN CTNATIVE AND THE ctbrowser VM DISAGREE, THE VM IS CORRECT. This is
    CTJS's rule and it binds harder here, because the whole point of a static
    type is that it lets the backend emit something the interpreter would not
    have. Every divergence that survives is DECLARED, in
    ctcompile/docs/native-divergences.md, with a test.
  }];

  // Generated parsing and printing for every TypeDef. Many of them take
  // parameters (`!ctnative.num<f64>`, `!ctnative.variant<...>`,
  // `!ctnative.map<K, V>`, ...), so unlike CTJS this replaces real hand-written
  // parsers.
  let useDefaultTypePrinterParser = 1;

  // The typed exception operations introduce neither attributes nor a
  // constant materializer. Their payload values use ordinary EmitC types.

  // Split for the same reason CTJS splits it: addTypes<> needs the storage
  // classes complete, and those are emitted into CTNativeTypes.cpp by
  // GET_TYPEDEF_CLASSES.
  let extraClassDeclaration = [{
    void registerTypes();
  }];

  // Passes declare the dialects whose operations they build in Passes.td.
  // The native type and exception definitions require no dependent dialect.
}

// Base class of every ctnative type; `typeMnemonic` is its spelling after
// `!ctnative.`.
class CTNative_Type<string name, string typeMnemonic, list<Trait> traits = []>
    : TypeDef<CTNative_Dialect, name, traits> {
  let mnemonic = typeMnemonic;
}

// Base class of every ctnative operation.
class CTNative_Op<string mnemonic, list<Trait> traits = []>
    : Op<CTNative_Dialect, mnemonic, traits>;

#endif // CTCOMPILE_CTNATIVE_BASE
