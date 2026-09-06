// EmitC/Expressions.cpp - native lowering implementation.
#include "../Admission/Admission.h"
#include "Emitter.h"
#include "ctcompile/Support/CppLiterals.hpp"

namespace ctcompile::ctnative::lowering_detail {

mlir::Type lowering::typeOf(mlir::Value v) const {
    const TypeLattice * lattice = solver.lookupState<TypeLattice>(v);
    return lattice == nullptr ? mlir::Type{} : lattice->getValue().getType();
}

mlir::Value lowering::f64Constant(mlir::OpBuilder & b, mlir::Location where, double d) {
    return ec::ConstantOp::create(b, where, mlir::Float64Type::get(context), b.getF64FloatAttr(d));
}

mlir::Value lowering::boolConstant(mlir::OpBuilder & b, mlir::Location where, bool v) {
    return ec::ConstantOp::create(b, where, mlir::IntegerType::get(context, 1),
                                  b.getIntegerAttr(mlir::IntegerType::get(context, 1), v ? 1 : 0));
}

mlir::Value lowering::stringConstant(mlir::OpBuilder & builder, mlir::Location where,
                                     llvm::StringRef value) {
    const std::string initializer =
        "std::string(" + cpp::c_string_literal(std::string_view(value.data(), value.size())) +
        ", " + std::to_string(value.size()) + ")";
    return ec::ConstantOp::create(builder, where, carrierType(context, carrier::string),
                                  ec::OpaqueAttr::get(context, initializer));
}

mlir::Value lowering::lvalueOfGlobal(mlir::OpBuilder & b, mlir::Location where,
                                     llvm::StringRef name) {
    globals.insert(name);
    needsNullable = true;
    return ec::GetGlobalOp::create(b, where,
                                   ec::LValueType::get(carrierType(context, carrier::nullable)),
                                   mlir::FlatSymbolRefAttr::get(context, ("g_" + name).str()));
}

// A definite number is truthy exactly when it is nonzero and not NaN.
mlir::Value lowering::truthyNumber(mlir::OpBuilder & b, mlir::Location where, mlir::Value x) {
    const auto i1 = mlir::IntegerType::get(context, 1);
    mlir::Value nonzero =
        ec::CmpOp::create(b, where, i1, ec::CmpPredicate::ne, x, f64Constant(b, where, 0.0));
    mlir::Value notNaN = ec::CmpOp::create(b, where, i1, ec::CmpPredicate::eq, x, x);
    return ec::LogicalAndOp::create(b, where, i1, nonzero, notNaN);
}

mlir::Value lowering::truthy(mlir::OpBuilder & builder, mlir::Location where, mlir::Value value) {
    if (isIdentityCarrier(value.getType())) {
        return ec::ConstantOp::create(builder, where, mlir::IntegerType::get(context, 1),
                                      builder.getBoolAttr(true));
    }
    if (isNullableCarrier(value.getType()) || isObjectValueCarrier(value.getType()) ||
        isNullableStringCarrier(value.getType())) {
        return convertScalar(builder, where, value, mlir::IntegerType::get(context, 1));
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

// ZERO RESULTS, AND THAT IS THE WHOLE POINT. A call with one result that
// nothing reads is declared as a variable by the emitter, which is
// -Werror=unused-variable on a file this tier promises compiles clean; the
// fork's `ctnative.statement` attribute exists for the calls that cannot
// avoid it, and --ctnative-prune-dead-stores deliberately will not erase a
// call to tidy up after one. A push has nothing to return, so it returns
// nothing.
void lowering::push(mlir::OpBuilder & b, mlir::Location where, mlir::Value into,
                    mlir::Value element) {
    ec::CallOpaqueOp::create(b, where, mlir::TypeRange{}, b.getStringAttr("ctnative::vec_push"),
                             mlir::ValueRange{into, element});
}

mlir::Value lowering::libmCall(mlir::OpBuilder & b, mlir::Location where, llvm::StringRef fn,
                               mlir::ValueRange args) {
    return ec::CallOpaqueOp::create(b, where, mlir::TypeRange{mlir::Float64Type::get(context)},
                                    b.getStringAttr(fn), args)
        .getResult(0);
}

// JAVASCRIPT'S `**`, WHICH IS NOT C++'s std::pow.
//
// Number::exponentiate answers NaN when the base has magnitude one and the
// exponent is NaN or infinite; C++ answers 1 for pow(1, NaN) and
// pow(1, INFINITY). Everywhere else the two agree, including pow(NaN, 0)
// == 1. So the whole difference is one guard, and emitting it is better
// than refusing the operator: `2 ** 31` keeps working and `1 ** undefined`
// stops being wrong. Numeric conversion of undefined produces NaN, making
// this difference reachable without an explicit NaN literal.
//
// StdLibMap.td already classifies the library spelling `Math.pow` as
// Divergent with this exact witness; this is the operator path catching up.
mlir::Value lowering::exponentiate(mlir::OpBuilder & b, mlir::Location where, mlir::Value base,
                                   mlir::Value exponent) {
    const auto i1 = mlir::IntegerType::get(context, 1);
    mlir::Value magnitude = libmCall(b, where, "std::fabs", {base});
    mlir::Value isOne = ec::CmpOp::create(b, where, i1, ec::CmpPredicate::eq, magnitude,
                                          f64Constant(b, where, 1.0));
    mlir::Value finite =
        ec::CallOpaqueOp::create(b, where, mlir::TypeRange{i1}, b.getStringAttr("std::isfinite"),
                                 mlir::ValueRange{exponent})
            .getResult(0);
    mlir::Value notFinite = ec::LogicalNotOp::create(b, where, i1, finite);
    mlir::Value diverges = ec::LogicalAndOp::create(b, where, i1, isOne, notFinite);
    return ec::ConditionalOp::create(
        b, where, mlir::Float64Type::get(context), diverges,
        f64Constant(b, where, std::numeric_limits<double>::quiet_NaN()),
        libmCall(b, where, "std::pow", {base, exponent}));
}

} // namespace ctcompile::ctnative::lowering_detail
