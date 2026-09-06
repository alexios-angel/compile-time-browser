#include "Bindings.h"

#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/EmitC/IR/EmitC.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/STLExtras.h"

namespace ctcompile::cpp {
namespace ec = mlir::emitc;

bool readsBinding(mlir::OpOperand & operand) {
    mlir::Operation * op = operand.getOwner();
    if (auto cast = llvm::dyn_cast<ec::CastOp>(op)) {
        // An opaque target can hide a reference cast that exposes the input
        // binding. Only ordinary native value/pointer conversions are reads.
        return supportsConstBinding(cast.getResult().getType());
    }
    if (llvm::isa<ec::AssignOp>(op)) { return operand.getOperandNumber() == 1; }
    if (auto call = llvm::dyn_cast<ec::CallOpaqueOp>(op)) {
        // This describes the C++ operand ABI, never purity or heap effects.
        // Native helper construction supplies it for by-value/const-reference
        // operands. Unspecified opaque calls may bind mutable references or
        // select different overloads when the actual argument gains const.
        auto safe = call->getAttrOfType<mlir::DenseI32ArrayAttr>("ctnative.const_operands");
        return safe && llvm::is_contained(safe.asArrayRef(), operand.getOperandNumber());
    }
    if (auto call = llvm::dyn_cast<ec::CallOp>(op)) {
        auto callee =
            mlir::SymbolTable::lookupNearestSymbolFrom<ec::FuncOp>(call, call.getCalleeAttr());
        if (!callee || callee.isExternal()) { return false; }
        const auto types = callee.getArgumentTypes();
        const unsigned index = operand.getOperandNumber();
        return index < types.size() && supportsConstBinding(types[index]);
    }
    // Views are read-only at their creation. Writes/address escapes through
    // a member or subscript propagate backwards to the owning binding.
    return llvm::isa<ec::AddOp, ec::SubOp, ec::MulOp, ec::DivOp, ec::RemOp, ec::CmpOp,
                     ec::LogicalAndOp, ec::LogicalOrOp, ec::LogicalNotOp, ec::UnaryMinusOp,
                     ec::UnaryPlusOp, ec::BitwiseAndOp, ec::BitwiseOrOp, ec::BitwiseXorOp,
                     ec::BitwiseNotOp, ec::BitwiseLeftShiftOp, ec::BitwiseRightShiftOp,
                     ec::ConditionalOp, ec::CastOp, ec::LoadOp, ec::MemberOp, ec::MemberOfPtrOp,
                     ec::GetFieldOp, ec::SubscriptOp, ec::DereferenceOp, ec::ReturnOp, ec::YieldOp,
                     ec::ExpressionOp, ec::IfOp, ec::ForOp, ec::SwitchOp, mlir::cf::BranchOp,
                     mlir::cf::CondBranchOp, mlir::func::ReturnOp>(op);
}

} // namespace ctcompile::cpp
