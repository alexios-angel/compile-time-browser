//===- CTNativeBase.td - the ctnative dialect and its base classes --------===//
//
// THE SECOND DIALECT, AND IT EXISTS BECAUSE THE FIRST ONE DELIBERATELY HAS NO
// TYPES. CTJSTypes.td opens with "FIVE, AND THE SMALLNESS IS THE DESIGN", and
// that is right for CTJS: "Refinements are expressed as guarded operations and
// analysis facts, never as unconditional MLIR types."
//
// ctnative is where a refinement becomes a type, and the reason the two cannot
// be one dialect is the word UNCONDITIONAL. A `!ctjs.value` is true of every
// JavaScript value there has ever been. A `!ctnative.num<i32>` is true only
// where a proof says so, and part 24 §1.2 states the consequence:
//
//   "A function compiles to native C++ only when the compiler can PROVE it is
//    in the static subset. Everything else falls back to the value-boxed
//    backend, which already compiles 574/574 of Bootstrap."
//
// So every type below is a CLAIM, every claim has a proof obligation attached
// to it in a later phase, and the top of the lattice - `!ctnative.boxed` - is
// the honest answer that hands the function back to CTJS.
//
//===----------------------------------------------------------------------===//
#ifndef CTCOMPILE_CTNATIVE_BASE
#define CTCOMPILE_CTNATIVE_BASE

include "mlir/IR/OpBase.td"
include "mlir/IR/AttrTypeBase.td"
include "ctcompile/CTNative/IR/CTNativeInterfaces.td"

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

  // Free parsing and printing for every TypeDef below. Four of them carry
  // parameters, so unlike CTJS this is doing real work: `!ctnative.num<f64>`
  // and `!ctnative.variant<...>` both round-trip without a hand-written parser.
  let useDefaultTypePrinterParser = 1;

  // NO ATTRIBUTES AND NO CONSTANT MATERIALIZER, and both absences are
  // deliberate rather than unfinished. This dialect has no operations yet -
  // Phase 53 is TYPES ONLY, and Phases 55-60 bring the operations that consume
  // them - so there is nothing for a materializer to rebuild and no inherent
  // attribute for anything to carry. CTJSBase.td sets hasConstantMaterializer
  // and then answers the obligation with a truthful nullptr; the honest
  // equivalent here is not to make the claim at all.

  // Inherent attributes stored as properties, for the same reason CTJS does it:
  // cheaper, and uniquified less. Costs nothing while there are no operations
  // and does not have to be revisited when there are.
  let usePropertiesForAttributes = 1;

  // Split for the same reason CTJS splits it: addTypes<> needs the STORAGE
  // classes complete, and those are emitted into CTNativeTypes.cpp by
  // GET_TYPEDEF_CLASSES.
  let extraClassDeclaration = [{
    void registerTypes();
  }];

  // NO dependentDialects. A dependent dialect is a promise that loading this
  // one loads that one too, and it exists so a PASS can build operations of a
  // dialect the context has not seen. This dialect builds nothing; the passes
  // that will - Phase 54's inference and Phase 55's lowering - declare their
  // own in their Passes.td, which is where MLIR wants that declaration and
  // where a reader looking for "what does this pass create" will find it.
}

class CTNative_Type<string name, string typeMnemonic, list<Trait> traits = []>
    : TypeDef<CTNative_Dialect, name, traits> {
  let mnemonic = typeMnemonic;
}

class CTNative_Op<string mnemonic, list<Trait> traits = []>
    : Op<CTNative_Dialect, mnemonic, traits>;

#endif // CTCOMPILE_CTNATIVE_BASE
