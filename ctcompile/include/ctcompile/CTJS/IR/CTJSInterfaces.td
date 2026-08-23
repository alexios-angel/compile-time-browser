//===- CTJSInterfaces.td - operation interfaces ---------------------------===//
//
// ONE INTERFACE, AND IT REPLACES ABOUT FIFTY LOWERING PATTERNS. See the
// CTJS_RuntimeOp class in CTJSBase.td for what it buys.
//
//===----------------------------------------------------------------------===//
#ifndef CTCOMPILE_CTJS_INTERFACES
#define CTCOMPILE_CTJS_INTERFACES

include "mlir/IR/OpBase.td"

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

#endif // CTCOMPILE_CTJS_INTERFACES
