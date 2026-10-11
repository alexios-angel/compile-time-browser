//===- CTNativeOps.td - The ctnative dialect's TableGen entry point -------===//
//
// The single TableGen entry point for the dialect: it includes the types and
// enums and defines the native operations. The C++ exception operations below
// keep structured try/catch boundaries after a function has been accepted
// for native compilation and lowered to EmitC value types.
//
//===----------------------------------------------------------------------===//
#ifndef CTCOMPILE_CTNATIVE_OPS
#define CTCOMPILE_CTNATIVE_OPS

include "ctcompile/CTNative/IR/CTNativeBase.td"
include "ctcompile/CTNative/IR/CTNativeTypes.td"
include "ctcompile/CTNative/IR/CTNativeEnums.td"
include "mlir/Interfaces/SideEffectInterfaces.td"

def CTNative_CppTryOp : CTNative_Op<"cpp_try", [SingleBlock, RecursiveMemoryEffects]> {
  let summary = "Catch a homogeneous owning primitive JavaScript exception in native C++";
  let description = [{
    Executes body and catches only ctnative::js_exception<T>. The catch region's
    sole ctnative::js_num, ctnative::js_boolean_t, ctnative::js_string or
    ctnative::js_null_t argument copies the thrown primitive. Raw f64, i1 and owning
    std::string remain supported for EmitC clients.
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
  let summary = "Throw an owning number, boolean, string or null JavaScript exception";
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

def CTNative_RefineOp : CTNative_Op<"refine", [Pure, AllTypesMatch<["value", "result"]>]> {
  let summary = "A value a dominating test proved to be of one kind";
  let description = [{
    The same JavaScript value as `value`, inside the region of a branch whose
    condition proved what `kind` names: a `typeof` result (`"string"`,
    `"number"`) for `typeof value == "<kind>"` and its `!=` else-region, or
    `"truthy"` for a branch on the value itself. Type inference narrows the
    result by the kind (TypeInference::refinedType); the native lowering
    spells it as the operand or as a ctnative::as_<kind> conversion. It is
    inserted by insertRefinements (Lowering/Refinements.cpp) and has no
    effect, so a refused function's refinements are dropped again.
  }];
  let arguments = (ins AnyType:$value, StrAttr:$kind);
  let results = (outs AnyType:$result);
  let assemblyFormat = "$value $kind attr-dict `:` type($value)";
}

#endif // CTCOMPILE_CTNATIVE_OPS
