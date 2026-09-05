#include "Emitter.h"

namespace ctcompile::ctnative::lowering_detail {

mlir::Value lowering::absentConstant(mlir::OpBuilder & b, mlir::Location where, bool isNull) {
    needsNullable = true;
    return ec::ConstantOp::create(
        b, where, carrierType(context, carrier::nullable),
        ec::OpaqueAttr::get(context, isNull ? "ctnative::nullable_scalar::null()"
                                            : "ctnative::nullable_scalar{}"));
}

mlir::Value lowering::number(mlir::OpBuilder & b, mlir::Location where, mlir::Value value) {
    return convertScalar(b, where, value, mlir::Float64Type::get(context));
}

mlir::Value lowering::convertScalar(mlir::OpBuilder & b, mlir::Location where, mlir::Value value,
                                    mlir::Type target) {
    if (auto lvalue = llvm::dyn_cast<ec::LValueType>(value.getType())) {
        value = ec::LoadOp::create(b, where, lvalue.getValueType(), value);
    }
    if (value.getType() == target) { return value; }
    llvm::StringRef helper;
    if (isObjectValueCarrier(target)) {
        needsObjectValue = true;
        helper = "ctnative::to_object_value";
    } else if (isObjectValueCarrier(value.getType()) && llvm::isa<mlir::IntegerType>(target)) {
        needsObjectValue = true;
        helper = "ctnative::object_truthy";
    } else if (isNullableCarrier(target)) {
        needsNullable = true;
        helper = "ctnative::to_nullable";
    } else if (isNullableCarrier(value.getType())) {
        needsNullable = true;
        helper = llvm::isa<mlir::IntegerType>(target) ? "ctnative::scalar_truthy"
                                                      : "ctnative::to_number";
    } else if (llvm::isa<mlir::Float64Type>(target) &&
               llvm::isa<mlir::IntegerType>(value.getType())) {
        helper = "static_cast<double>";
    } else {
        llvm::report_fatal_error("native scalar boundary has incompatible proved carriers");
    }
    return ec::CallOpaqueOp::create(b, where, mlir::TypeRange{target}, b.getStringAttr(helper),
                                    mlir::ValueRange{value})
        .getResult(0);
}

mlir::Type lowering::joinedReturnType(ctjs::FuncOp fn) const {
    mlir::Type result = BottomType::get(context);
    fn.getBody().walk([&](ctjs::ReturnOp ret) { result = meet(result, typeOf(ret.getValue())); });
    return result;
}

void lowering::censusScalars(llvm::ArrayRef<ctjs::FuncOp> accepted) {
    const auto scalarType = [&](mlir::Type type) -> mlir::Type {
        const auto c = carrierOf(type);
        if (!isScalarCarrier(c) && !isObjectCarrier(c) && c != carrier::string) { return {}; }
        needsObjectValue |= c == carrier::objectValue;
        needsNullable |= c == carrier::nullable;
        return carrierType(context, c);
    };
    for (ctjs::FuncOp fn : accepted) {
        resultTypes[fn.getSymName()] = scalarType(joinedReturnType(fn));
        auto & params = parameterTypes[fn.getSymName()];
        for (mlir::BlockArgument arg : fn.getBody().front().getArguments()) {
            params.push_back(scalarType(typeOf(arg)));
        }
    }
}

// SCF preserves JavaScript values through typed region edges. Materialize the
// scalar widening at each edge, including initial loop operands and backedges.
// Calls use the callee's joined parameter type, not the actual argument type.
void lowering::convertBoundaries(ctjs::FuncOp fn) {
    llvm::SmallVector<mlir::Operation *> ops;
    fn.getBody().walk([&](mlir::Operation * op) { ops.push_back(op); });
    for (mlir::Operation * op : ops) {
        mlir::OpBuilder at(op);
        const auto convert = [&](unsigned index, mlir::Type target) {
            mlir::Value value = op->getOperand(index);
            if (!target || value.getType() == target ||
                (value.getDefiningOp() &&
                 value.getDefiningOp()->getName().getStringRef() == "ub.poison")) {
                return;
            }
            op->setOperand(index, convertScalar(at, op->getLoc(), value, target));
        };
        if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(op)) {
            const auto & params = parameterTypes[call.getCallee()];
            for (unsigned i = 3; i < params.size() && i < op->getNumOperands(); ++i) {
                if (!llvm::isa<ec::PointerType, ec::LValueType>(op->getOperand(i).getType())) {
                    convert(i, params[i]);
                }
            }
        } else if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(op)) {
            for (unsigned i = 0; i < op->getNumOperands(); ++i) {
                convert(i, loop.getBefore().front().getArgument(i).getType());
            }
        } else if (auto loop = llvm::dyn_cast<mlir::scf::ForOp>(op)) {
            for (unsigned i = 3; i < op->getNumOperands(); ++i) {
                convert(i, loop->getResult(i - 3).getType());
            }
        } else if (llvm::isa<mlir::scf::YieldOp>(op)) {
            mlir::Operation * parent = op->getParentOp();
            for (unsigned i = 0; i < op->getNumOperands(); ++i) {
                auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(parent);
                convert(i, loop ? loop.getBefore().front().getArgument(i).getType()
                                : parent->getResult(i).getType());
            }
        } else if (llvm::isa<mlir::scf::ConditionOp>(op)) {
            for (unsigned i = 1; i < op->getNumOperands(); ++i) {
                convert(i, op->getParentOp()->getResult(i - 1).getType());
            }
        }
    }
}

} // namespace ctcompile::ctnative::lowering_detail
