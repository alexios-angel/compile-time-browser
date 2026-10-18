//===- Expressions.cpp - EmitC constants and expressions ------ -*- C++ -*-===//
//
// Constants, global lvalues, truthiness, array push and the `**` operator for
// the JavaScript-to-EmitC lowering.
//
//===----------------------------------------------------------------------===//
#include "../Admission/Admission.h"
#include "Emitter.h"
#include "ctcompile/Support/CppLiterals.hpp"

namespace ctcompile::ctnative::lowering_detail {

mlir::Type lowering::typeOf(mlir::Value v) const {
    const TypeLattice * lattice = solver.lookupState<TypeLattice>(v);
    return lattice == nullptr ? mlir::Type{} : lattice->getValue().getType();
}

mlir::Value lowering::numberConstant(mlir::OpBuilder & b, mlir::Location where, double d) {
    auto raw = ec::ConstantOp::create(b, where, b.getF64Type(), b.getF64FloatAttr(d));
    return convertScalar(b, where, raw, carrierType(context, carrier::number));
}

mlir::Value lowering::boolConstant(mlir::OpBuilder & b, mlir::Location where, bool v) {
    return ec::ConstantOp::create(
        b, where, carrierType(context, carrier::boolean),
        ec::OpaqueAttr::get(context,
                            v ? "ctnative::js_boolean_t{true}" : "ctnative::js_boolean_t{false}"));
}

mlir::Value lowering::stringConstant(mlir::OpBuilder & builder, mlir::Location where,
                                     llvm::StringRef value) {
    const std::string initializer =
        "ctnative::js_string(" +
        cpp::c_string_literal(std::string_view(value.data(), value.size())) + ", " +
        std::to_string(value.size()) + ")";
    return ec::ConstantOp::create(builder, where, carrierType(context, carrier::string),
                                  ec::OpaqueAttr::get(context, initializer));
}

mlir::Type lowering::globalStorageType(llvm::StringRef name) const {
    // Every store to the global anywhere in the source decides its single C++
    // storage type. The type is always nullable so the global can start out as
    // undefined, which a read before the first store must see.
    const carrier stored = carrierOf(globalTypes.lookup(name));
    if (stored == carrier::booleanString || stored == carrier::nullableBooleanString) {
        return carrierType(context, carrier::nullableBooleanString);
    }
    if (stored == carrier::numberString || stored == carrier::nullableNumberString) {
        return carrierType(context, carrier::nullableNumberString);
    }
    return carrierType(context,
                       isStringCarrier(stored) ? carrier::nullableString : carrier::nullable);
}

mlir::Value lowering::lvalueOfGlobal(mlir::OpBuilder & b, mlir::Location where,
                                     llvm::StringRef name) {
    if (auto owned = ownedGlobals.find(name); owned != ownedGlobals.end()) {
        return ec::GetGlobalOp::create(
            b, where, ec::LValueType::get(ownedGlobalStoragePlans[owned->second].type),
            mlir::FlatSymbolRefAttr::get(context, ("g_" + name).str()));
    }
    globals.insert(name);
    const auto storage = globalStorageType(name);
    return ec::GetGlobalOp::create(b, where, ec::LValueType::get(storage),
                                   mlir::FlatSymbolRefAttr::get(context, ("g_" + name).str()));
}

// A definite number is truthy exactly when it is nonzero and not NaN.
mlir::Value lowering::truthyNumber(mlir::OpBuilder & b, mlir::Location where, mlir::Value x) {
    return ec::CastOp::create(b, where, b.getI1Type(), x);
}

mlir::Value lowering::truthy(mlir::OpBuilder & builder, mlir::Location where, mlir::Value value) {
    if (isBooleanCarrier(value.getType())) {
        return convertScalar(builder, where, value, builder.getI1Type());
    }
    if (isIdentityCarrier(value.getType()) ||
        value.getType() == carrierType(context, carrier::symbol)) {
        return ec::ConstantOp::create(builder, where, mlir::IntegerType::get(context, 1),
                                      builder.getBoolAttr(true));
    }
    // An optional, union or object carrier's explicit `bool` is ToBoolean of
    // the kind it holds.
    if (isNullableCarrier(value.getType()) || isObjectValueCarrier(value.getType()) ||
        isNullableStringCarrier(value.getType()) || isNumberStringCarrier(value.getType()) ||
        isNullableNumberStringCarrier(value.getType()) || isBooleanStringCarrier(value.getType()) ||
        isNullableBooleanStringCarrier(value.getType())) {
        return ec::CastOp::create(builder, where, mlir::IntegerType::get(context, 1), value);
    }
    if (llvm::isa<mlir::IntegerType>(value.getType())) { return value; }
    if (value.getType() == carrierType(context, carrier::string)) {
        // Keep this a pure comparison: an opaque .empty() call retains
        // its unused result after dead-store pruning and breaks -Werror.
        return ec::CmpOp::create(builder, where, mlir::IntegerType::get(context, 1),
                                 ec::CmpPredicate::ne, value, stringConstant(builder, where, ""));
    }
    return truthyNumber(builder, where, value);
}

// The call has no result on purpose. The C++ emitter declares a variable for
// a call result that nothing reads, which fails -Werror=unused-variable, and
// the generated code must compile warning-free. The `ctnative.statement`
// attribute exists for calls that cannot avoid a result, and
// --ctnative-prune-dead-stores deliberately will not erase a call to clean up
// after one. A push has nothing to return, so it returns nothing.
void lowering::push(mlir::OpBuilder & b, mlir::Location where, mlir::Value into,
                    mlir::Value element) {
    callWithConstValueOperands(b, where, mlir::TypeRange{}, b.getStringAttr("ctnative::vec_push"),
                               mlir::ValueRange{into, element});
}

mlir::Value lowering::libmCall(mlir::OpBuilder & b, mlir::Location where, llvm::StringRef fn,
                               mlir::ValueRange args) {
    llvm::SmallVector<mlir::Value> raw;
    for (mlir::Value arg : args) { raw.push_back(convertScalar(b, where, arg, b.getF64Type())); }
    auto result = callValue(b, where, b.getF64Type(), fn, raw);
    return convertScalar(b, where, result, carrierType(context, carrier::number));
}

// JavaScript's `**`, which is not C++'s std::pow.
//
// Number::exponentiate answers NaN when the base has magnitude one and the
// exponent is NaN or infinite; C++ answers 1 for pow(1, NaN) and
// pow(1, INFINITY). Everywhere else the two agree, including pow(NaN, 0)
// == 1. So the whole difference is one guard, and emitting it is better
// than refusing the operator: `2 ** 31` keeps working and `1 ** undefined`
// stops being wrong. Numeric conversion of undefined produces NaN, making
// this difference reachable without an explicit NaN literal.
// StdLibMap.def records the same divergence for `Math.pow`.
mlir::Value lowering::exponentiate(mlir::OpBuilder & b, mlir::Location where, mlir::Value base,
                                   mlir::Value exponent) {
    const auto i1 = mlir::IntegerType::get(context, 1);
    mlir::Value magnitude = libmCall(b, where, "std::fabs", {base});
    mlir::Value isOne = ec::CmpOp::create(b, where, i1, ec::CmpPredicate::eq, magnitude,
                                          numberConstant(b, where, 1.0));
    mlir::Value finite =
        callValue(b, where, i1, "std::isfinite", convertScalar(b, where, exponent, b.getF64Type()));
    mlir::Value notFinite = ec::LogicalNotOp::create(b, where, i1, finite);
    mlir::Value diverges = ec::LogicalAndOp::create(b, where, i1, isOne, notFinite);
    return ec::ConditionalOp::create(
        b, where, carrierType(context, carrier::number), diverges,
        numberConstant(b, where, std::numeric_limits<double>::quiet_NaN()),
        libmCall(b, where, "std::pow", {base, exponent}));
}

} // namespace ctcompile::ctnative::lowering_detail
