//===- CTJSBase.td - the CTJS dialect, its base classes -------------------===//
//
// The CTJS dialect definition and the base classes every CTJS op, type and
// attribute derives from. The comments below say why each setting is chosen;
// change one only with that reason in mind.
//
//===----------------------------------------------------------------------===//
#ifndef CTCOMPILE_CTJS_BASE
#define CTCOMPILE_CTJS_BASE

include "mlir/IR/OpBase.td"
include "mlir/IR/AttrTypeBase.td"
include "mlir/Interfaces/SideEffectInterfaces.td"
include "mlir/Interfaces/InferTypeOpInterface.td"
include "mlir/Interfaces/CallInterfaces.td"
include "mlir/Interfaces/FunctionInterfaces.td"
include "ctcompile/CTJS/IR/CTJSInterfaces.td"

def CTJS_Dialect : Dialect {
  let name = "ctjs";
  let cppNamespace = "::ctcompile::ctjs";
  let summary = "ECMAScript semantics as understood by the ctbrowser runtime";
  let description = [{
    CTJS is the optimizing representation for JavaScript compiled ahead of
    time by ctcompile. Its operations mirror the semantics of the ctbrowser
    bytecode and the shared runtime helpers, not the semantics of any other
    JavaScript engine. When a CTJS operation and the ctbrowser VM disagree,
    the VM is correct by definition.
  }];

  // Free parsing and printing for every TypeDef and AttrDef in the dialect.
  let useDefaultTypePrinterParser = 1;

  // Generated printing and parsing for the dialect's AttrDefs. This must be 0
  // if the dialect ever has no AttrDefs, because -gen-attrdef-defs then emits
  // an empty file and the declared hooks would have no definitions.
  let useDefaultAttributePrinterParser = 1;

  // Inherent attributes are stored as properties (MLIR's default, so nothing
  // is set here).

  // Required for the folder to rebuild constants.
  let hasConstantMaterializer = 1;

  // Registration is split across the files that define things, as in upstream
  // MLIR. addTypes<> and addAttributes<> need the storage classes complete, and
  // GET_TYPEDEF_CLASSES / GET_ATTRDEF_CLASSES emit those into CTJSTypes.cpp and
  // CTJSAttrs.cpp, so registering them from the dialect's own translation unit
  // would register incomplete types.
  let extraClassDeclaration = [{
    void registerTypes();
    void registerAttributes();
  }];

  let dependentDialects = [
    "::mlir::cf::ControlFlowDialect",
    "::mlir::func::FuncDialect",
    "::mlir::arith::ArithDialect",
  ];
}

class CTJS_Op<string mnemonic, list<Trait> traits = []>
    : Op<CTJS_Dialect, mnemonic, traits>;

class CTJS_Type<string name, string typeMnemonic, list<Trait> traits = []>
    : TypeDef<CTJS_Dialect, name, traits> {
  let mnemonic = typeMnemonic;
}

class CTJS_Attr<string name, string attrMnemonic, list<Trait> traits = []>
    : AttrDef<CTJS_Dialect, name, traits> {
  let mnemonic = attrMnemonic;
}

// Custom traits live in ::ctcompile::ctjs, not ::mlir::OpTrait.
class CTJS_NativeOpTrait<string name> : NativeOpTrait<name> {
  let cppNamespace = "::ctcompile::ctjs";
}

// The four semantic traits. Their C++ templates are in CTJSTraits.h, in
// ::ctcompile::ctjs rather than ::mlir::OpTrait, which CTJS_NativeOpTrait
// (above) arranges.
def CTJS_MayThrow : CTJS_NativeOpTrait<"CTJSMayThrow">;
def CTJS_MayReenterJS : CTJS_NativeOpTrait<"CTJSMayReenterJS">;
def CTJS_Safepoint : CTJS_NativeOpTrait<"CTJSSafepoint">;
def CTJS_MaySuspend : CTJS_NativeOpTrait<"CTJSMaySuspend">;

// The ABI shape check. Not a semantic trait, but attached the same way:
// CTJS_RuntimeOp adds it, so every operation naming a helper has its
// declaration compared against that helper's row. See CTJSABIShape.cpp.
def CTJS_ABIShaped : CTJS_NativeOpTrait<"CTJSABIShaped">;

// The common case for a generic JavaScript operation.
//
// Effects are opt-in to purity, never opt-out. Almost every JavaScript
// operation can re-enter arbitrary JavaScript through a getter, a valueOf, a
// toString or a proxy trap, so the default is unknown effects. An operation
// wrongly marked Pure would be hoisted out of loops, CSE'd across a side effect
// or deleted as dead: a miscompile in a real application, not a test failure.
defvar CTJS_GenericEffects = [CTJS_MayThrow, CTJS_MayReenterJS, CTJS_Safepoint];

// An operation that lowers to one shared runtime helper. Roughly fifty CTJS
// operations lower to "call a helper, check the status, branch to the handler
// on thrown, otherwise continue"; one interface plus one conversion pattern
// covers them all, instead of fifty near-identical patterns that could each get
// the operand order wrong.
//
// `helper` names an enumerator of ctbrowser::aot::helper_id, which is generated
// from aot_helpers.def, so a wrong name is a C++ compile error rather than a
// lowering that calls the wrong thing. This also ties the dialect to the ABI:
// an operation cannot claim a helper the runtime does not declare.
class CTJS_RuntimeOp<string mnemonic, string helper, list<Trait> traits = []>
    : CTJS_Op<mnemonic, traits # [
        DeclareOpInterfaceMethods<CTJS_RuntimeCallOpInterface>,
        CTJS_ABIShaped]> {
  let extraClassDefinition = [{
    ::ctbrowser::aot::helper_id $cppClass::getHelperID() {
      return ::ctbrowser::aot::helper_id::}] # helper # [{;
    }
  }];
}

#endif // CTCOMPILE_CTJS_BASE
