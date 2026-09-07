//===- CTNativeOps.td - the ctnative dialect's TableGen entry point -------===//
//
// Typed native operations and the type lattice share the dialect's existing
// TableGen entry point. The C++ exception operations preserve structured
// unwind boundaries after CTJS admission and lowering to EmitC value types.
//
//===----------------------------------------------------------------------===//
#ifndef CTCOMPILE_CTNATIVE_OPS
#define CTCOMPILE_CTNATIVE_OPS

include "ctcompile/CTNative/IR/CTNativeBase.td"
include "ctcompile/CTNative/IR/CTNativeTypes.td"
include "ctcompile/CTNative/IR/CTNativeEnums.td"
include "ctcompile/CTNative/IR/CTNativeInterfaces.td"
include "mlir/Interfaces/SideEffectInterfaces.td"

def CTNative_CppTryOp : CTNative_Op<"cpp_try", [SingleBlock, RecursiveMemoryEffects]> {
  let summary = "Catch a homogeneous owning primitive JavaScript exception in native C++";
  let description = [{
    Executes body and catches only ctnative::js_exception<T>. The catch region's
    sole f64, i1 or owning std::string argument copies the thrown primitive.
    The payload type is homogeneous: no numeric/boolean coercion or borrowed
    string carrier is permitted. Each protected native throw must match it.
    Foreign C++ exceptions are not caught. Both regions are single blocks;
    both must terminate with cpp_try_end, with no yielded values.
    Mutable state crossing the boundary is explicit in the enclosing EmitC IR.
  }];
  let regions = (region AnyRegion:$body, AnyRegion:$catchBody);
  let assemblyFormat = "$body `catch` $catchBody attr-dict";
  let hasVerifier = 1;
}

def CTNative_CppThrowOp : CTNative_Op<"cpp_throw"> {
  let summary = "Throw an owning number, boolean or string JavaScript exception";
  let description = [{
    Emits a side-effecting C++ throw statement. It immediately precedes the
    enclosing region's terminator, including emitc.yield in an EmitC if arm.
    That terminator remains structurally present but is not reached at runtime.
  }];
  let arguments = (ins AnyType:$value);
  let assemblyFormat = "$value attr-dict `:` type($value)";
  let hasVerifier = 1;
}

def CTNative_CppTryEndOp : CTNative_Op<"cpp_try_end", [Terminator, HasParent<"CppTryOp">]> {
  let summary = "Complete a native try or catch region normally";
  let assemblyFormat = "attr-dict";
}

#endif // CTCOMPILE_CTNATIVE_OPS
