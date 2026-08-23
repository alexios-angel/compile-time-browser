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

  // DEVIATION FROM THE POLICY'S VERBATIM TEXT, which sets this to 1, and the
  // policy requires that a deviation say which declarative mechanism was
  // insufficient and why. This one:
  //
  // `useDefaultAttributePrinterParser = 1` makes the generated dialect DECLARE
  // CTJSDialect::parseAttribute and printAttribute. Their definitions come from
  // -gen-attrdef-defs - and with zero AttrDefs that backend emits an empty
  // file, so the dialect declares two methods nothing defines and the library
  // does not link. Types are unaffected because there are five of them.
  //
  // Nothing is hand-written to paper over it: writing those two by hand is on
  // the policy's "what must never be hand-written" list. This goes back to 1 in
  // Phase 8, in the same change as the first AttrDef, and CTJSAttrs.cpp is
  // already there to expand the definitions when it does.
  let useDefaultAttributePrinterParser = 0;

  // Inherent attributes stored as properties: cheaper and uniquified less.
  let usePropertiesForAttributes = 1;

  // Required for the folder to rebuild constants.
  let hasConstantMaterializer = 1;

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

#endif // CTCOMPILE_CTJS_BASE
