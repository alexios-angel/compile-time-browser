//===- CTJSInterfaces.td - operation interfaces ---------------------------===//
//
// TWO INTERFACES. The first replaces about fifty lowering patterns - see the
// CTJS_RuntimeOp class in CTJSBase.td for what it buys. The second is Phase
// 55's per-operand escape table: a SideEffect vocabulary whose rows are the
// Arg<..., [CTJS_Sink...]> decorators in CTJSOps.td, beside the operands they
// describe, where part 23 §1 says a table must live.
//
// ONLY SideEffectInterfaceBase.td IS INCLUDED FOR IT, not SideEffectInterfaces
// .td: this file is run through -gen-op-interface-decls on its own, which
// generates EVERY OpInterface def it can see, and the full file defines
// MemoryEffectOpInterface and ConditionallySpeculatable - two interfaces MLIR
// already generates, and a duplicate definition of each.
//
//===----------------------------------------------------------------------===//
#ifndef CTCOMPILE_CTJS_INTERFACES
#define CTCOMPILE_CTJS_INTERFACES

include "mlir/IR/OpBase.td"
include "mlir/Interfaces/SideEffectInterfaceBase.td"

def CTJS_RuntimeCallOpInterface : OpInterface<"RuntimeCallOpInterface"> {
  let cppNamespace = "::ctcompile::ctjs";
  let description = [{
    Implemented by every CTJS operation whose semantics are defined by a
    single shared runtime helper. A generic conversion pattern lowers all
    such operations uniformly, so individual operations need no lowering
    code of their own.
  }];
  let methods = [
    InterfaceMethod<
      /*desc=*/"The shared runtime helper this operation lowers to.",
      /*retTy=*/"::ctbrowser::aot::helper_id",
      /*methodName=*/"getHelperID">,
  ];
}

//===----------------------------------------------------------------------===//
// Phase 55: the escape table's vocabulary
//===----------------------------------------------------------------------===//

// THE INTERFACE. Its C++ base effect is CTJSEscapeEffects.h's
// EscapeEffects::Effect, and everything else - getEffects, hasEffect<>,
// getEffectsOnValue - is what EffectOpInterfaceBase generates for
// MemoryEffectOpInterface too. Nothing in MLIR reads it; only
// EscapeAnalysis.cpp does.
def CTJS_EscapeEffectOpInterface
    : EffectOpInterfaceBase<"EscapeEffectOpInterface",
                            "::ctcompile::ctjs::EscapeEffects::Effect"> {
  let cppNamespace = "::ctcompile::ctjs";
  let description = [{
    Per-operand escape roles for Phase 55. A SINK operand hands the object to
    something that outlives the frame or can run user code with it; a CARRY
    operand's object is (possibly) the result. An operand with neither is
    NEITHER, and the .td row cites the VM lines that prove it - NEITHER is
    the only label that can be unsound. A BOXED-SITE result is an allocating
    operation the MVP does not track, claimed `escapes(reason)`.

    THE DEFAULT RULE, load-bearing: an operation WITHOUT this interface (and
    that is not a BranchOpInterface terminator, whose successor operands the
    dataflow framework carries) sinks every !ctjs.value operand with reason
    `unknown_op`, and its results alias an external object. A new operation
    cannot weaken the analysis by omission. An operation with no !ctjs.value
    operand needs no annotation at all: the default rule sinks nothing and its
    result is external either way.

    On the native target an escaping site is not "boxed": it needs an OWNER,
    which Phase 55B picks from the reason, or it is outside the native subset
    and diagnosed. The reason is therefore load-bearing, and the analysis
    keeps the FIRST reason it finds for a site.
  }];
}

// WHERE AN ESCAPING OBJECT GOES, as an MLIR Resource - the one per-decorator
// slot that names a C++ class. Each getName() is the spelling of the matching
// CTNative_EscapeReason case (CTNativeEnums.td); the analysis symbolizes it
// back. The classes are CTJSEscapeEffects.h's.
class CTJS_EscapeRoute<string name>
    : Resource<"::ctcompile::ctjs::EscapeEffects::" # name>;
def CTJS_Returned        : CTJS_EscapeRoute<"Returned">;
def CTJS_Thrown          : CTJS_EscapeRoute<"Thrown">;
def CTJS_Stored          : CTJS_EscapeRoute<"Stored">;
def CTJS_StoredGlobal    : CTJS_EscapeRoute<"StoredGlobal">;
def CTJS_Captured        : CTJS_EscapeRoute<"Captured">;
def CTJS_Passed          : CTJS_EscapeRoute<"Passed">;
def CTJS_Converted       : CTJS_EscapeRoute<"Converted">;
def CTJS_ProtoMutated    : CTJS_EscapeRoute<"ProtoMutated">;
def CTJS_AccessorDefined : CTJS_EscapeRoute<"AccessorDefined">;
// The boxed-site reasons, named by a result decorator only.
def CTJS_Arguments       : CTJS_EscapeRoute<"Arguments">;
def CTJS_RuntimeArray    : CTJS_EscapeRoute<"RuntimeArray">;
def CTJS_Phase59         : CTJS_EscapeRoute<"Phase59">;
def CTJS_NotTracked      : CTJS_EscapeRoute<"NotTracked">;

// THE EFFECTS. Stage 0 and a partial effect, always: the stage/range columns
// describe memory ordering and mean nothing for an escape.
class CTJS_EscapeEffect<string effect, Resource where>
    : SideEffect<CTJS_EscapeEffectOpInterface,
                 "::ctcompile::ctjs::EscapeEffects::" # effect, where, 0, PartialEffect>;

// SINK(reason): put on the operand, e.g.
//   Arg<CTJS_ValueType, "", [CTJS_SinkStored]>:$value
// Variadics decorate the same way and enumerate one instance per element.
class CTJS_Sink<Resource where> : CTJS_EscapeEffect<"Sink", where>;
def CTJS_SinkReturned        : CTJS_Sink<CTJS_Returned>;
def CTJS_SinkThrown          : CTJS_Sink<CTJS_Thrown>;
def CTJS_SinkStored          : CTJS_Sink<CTJS_Stored>;
def CTJS_SinkStoredGlobal    : CTJS_Sink<CTJS_StoredGlobal>;
def CTJS_SinkCaptured        : CTJS_Sink<CTJS_Captured>;
def CTJS_SinkPassed          : CTJS_Sink<CTJS_Passed>;
def CTJS_SinkConverted       : CTJS_Sink<CTJS_Converted>;
def CTJS_SinkProtoMutated    : CTJS_Sink<CTJS_ProtoMutated>;
def CTJS_SinkAccessorDefined : CTJS_Sink<CTJS_AccessorDefined>;

// CARRY: the result may be the operand's object. The analysis also adds an
// external object to a carried result, because the one carrier today
// (ctjs.iterable) substitutes a fresh array on every non-array arm.
def CTJS_Carry : CTJS_EscapeEffect<"Carry", DefaultResource>;

// BOXED-SITE(reason): put on a RESULT, e.g.
//   Res<CTJS_ValueType, "", [CTJS_BoxedPhase59]>:$result
class CTJS_BoxedSite<Resource why> : CTJS_EscapeEffect<"BoxedSite", why>;
def CTJS_BoxedArguments    : CTJS_BoxedSite<CTJS_Arguments>;
def CTJS_BoxedRuntimeArray : CTJS_BoxedSite<CTJS_RuntimeArray>;
def CTJS_BoxedPhase59      : CTJS_BoxedSite<CTJS_Phase59>;
def CTJS_BoxedNotTracked   : CTJS_BoxedSite<CTJS_NotTracked>;

// THE TRAIT FORM, for an operation whose every operand is NEITHER and proved
// so: it declares the interface with an empty effect list, which is how
// NoMemoryEffect is spelled too. An operation carrying it sinks nothing - so
// it is the one annotation that can be WRONG, and every use in CTJSOps.td
// carries the VM lines that prove no store and no user code passes through.
class CTJS_EscapeEffects<list<SideEffect> effects = []>
    : SideEffectsTraitBase<CTJS_EscapeEffectOpInterface, effects>;
def CTJS_NoEscapeEffects : CTJS_EscapeEffects<[]>;

#endif // CTCOMPILE_CTJS_INTERFACES
