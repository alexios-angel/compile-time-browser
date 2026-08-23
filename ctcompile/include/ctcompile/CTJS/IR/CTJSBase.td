//===- CTJSBase.td - the CTJS dialect, its base classes -------------------===//
//
// TAKEN VERBATIM FROM THE MLIR DECLARATIVE DEFINITION POLICY, which quotes this
// file in full. Where a line here looks arbitrary it is not: the policy states
// what each one buys, and this file is the one place the project has agreed not
// to improvise.
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

  // BACK TO 1 IN PHASE 8, as the Phase 7 deviation said it would be. It was 0
  // only because -gen-attrdef-defs emits an empty file when there are no
  // AttrDefs, so the hooks the dialect declared had no definitions. There are
  // six constant attributes now.
  let useDefaultAttributePrinterParser = 1;

  // Inherent attributes stored as properties: cheaper and uniquified less.
  let usePropertiesForAttributes = 1;

  // Required for the folder to rebuild constants.
  let hasConstantMaterializer = 1;

  // REGISTRATION SPLIT ACROSS THE FILES THAT DEFINE THINGS, which is upstream
  // MLIR's own layout. addTypes<> and addAttributes<> need the STORAGE classes
  // complete, and those are emitted into CTJSTypes.cpp and CTJSAttrs.cpp by
  // GET_TYPEDEF_CLASSES / GET_ATTRDEF_CLASSES - so a dialect that registers
  // them from its own translation unit is registering incomplete types.
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

// THE FOUR SEMANTIC TRAITS. Their C++ templates are in CTJSTraits.h, in
// ::ctcompile::ctjs rather than ::mlir::OpTrait, which is what
// CTJS_NativeOpTrait below arranges.
def CTJS_MayThrow : CTJS_NativeOpTrait<"CTJSMayThrow">;
def CTJS_MayReenterJS : CTJS_NativeOpTrait<"CTJSMayReenterJS">;
def CTJS_Safepoint : CTJS_NativeOpTrait<"CTJSSafepoint">;
def CTJS_MaySuspend : CTJS_NativeOpTrait<"CTJSMaySuspend">;

// AND THE SHAPE CHECK, which is not a semantic trait but travels the same way.
// CTJS_RuntimeOp attaches it below, so every operation naming a helper has its
// declaration compared against that helper's row. See CTJSABIShape.cpp.
def CTJS_ABIShaped : CTJS_NativeOpTrait<"CTJSABIShaped">;

// The common case for a generic JavaScript operation.
//
// EFFECTS ARE OPT-IN TO PURITY, NEVER OPT-OUT, which is the policy's heading
// and its sharpest rule: "almost every JavaScript operation can re-enter
// arbitrary JavaScript through a getter, a valueOf, a toString, or a proxy
// trap. The default assumption must therefore be unknown effects." An
// operation wrongly marked Pure "will be hoisted out of loops, CSE'd across a
// side effect, or deleted as dead. The failure mode is a miscompile in a real
// application, not a test failure."
defvar CTJS_GenericEffects = [CTJS_MayThrow, CTJS_MayReenterJS, CTJS_Safepoint];

// AN OPERATION THAT LOWERS TO ONE SHARED RUNTIME HELPER, which the policy calls
// "the highest-leverage declarative decision in the project": roughly fifty
// CTJS operations lower to "call a helper, check the status, branch to the
// handler on thrown, otherwise continue". Written as fifty conversion patterns
// that is fifty near-identical files and fifty chances to get the operand order
// wrong; written as one interface plus one pattern it is one file.
//
// `helper` NAMES AN ENUMERATOR of ctbrowser::aot::helper_id, which is generated
// from aot_helpers.def - so a wrong name is a C++ compile error rather than a
// lowering that calls the wrong thing. That is also what ties this dialect to
// the ABI: an operation cannot claim a helper the runtime does not declare.
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
