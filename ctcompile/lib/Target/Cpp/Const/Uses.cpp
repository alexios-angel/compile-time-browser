//===- ctcompile/lib/Target/Cpp/Const/Uses.cpp - Read-only operand uses ---===//
//
// Implements readsBinding: which operand uses only read a binding, so the
// bound value can stay `const`. The list is conservative; an unknown use is
// treated as a write.
//
//===----------------------------------------------------------------------===//

#include "Bindings.h"

#include "ctcompile/CTNative/IR/CTNativeOps.h"
#include "ctcompile/Support/OpaqueType.h"

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
        // binding. Only ordinary native value or pointer conversions are reads.
        return supportsConstBinding(cast.getResult().getType());
    }
    if (llvm::isa<ec::AssignOp>(op)) { return operand.getOperandNumber() == 1; }
    // Member calls: only a short list of known const member functions on
    // native value types is a read.
    if (auto call = llvm::dyn_cast<ec::MemberCallOpaqueOp>(op)) {
        const auto receiver = llvm::dyn_cast<ec::OpaqueType>(call.getReceiver().getType());
        if (!receiver || call.getArgs() || call.getTemplateArgs() || call.getNumResults() != 1) {
            return false;
        }
        const auto result = call.getResult(0).getType();
        if (receiver.getValue() == "ctnative::js_num" &&
            call.getCallee() == "unsigned_shift_right" && call.getArgOperands().size() == 1) {
            return isOpaque(result, "ctnative::js_num") &&
                   call.getArgOperands()[0].getType() == result;
        }
        if (receiver.getValue() == "ctnative::nullable_scalar" && call.getCallee() == "to_string" &&
            call.getArgOperands().empty()) {
            return isOpaque(result, "ctnative::js_string");
        }
        if ((receiver.getValue() == "ctnative::js_string" ||
             receiver.getValue() == "ctnative::nullable_string") &&
            call.getCallee() == "to_number" && call.getArgOperands().empty()) {
            return isOpaque(result, "ctnative::js_num");
        }
        if (call.getCallee() == "value" && call.getArgOperands().empty()) {
            if (receiver.getValue() == "ctnative::js_num") { return result.isF64(); }
            return receiver.getValue() == "ctnative::js_string" && isOpaque(result, "std::string");
        }
        // js_string's JavaScript members are const: they read the String and
        // return a new value.
        if (receiver.getValue() == "ctnative::js_string") {
            const llvm::StringRef name = call.getCallee();
            const auto arguments = call.getArgOperands();
            if (name == "length" && arguments.empty()) {
                return isOpaque(result, "ctnative::js_num");
            }
            if ((name == "toLowerCase" || name == "toUpperCase" || name == "trim" ||
                 name == "toString") &&
                arguments.empty()) {
                return isOpaque(result, "ctnative::js_string");
            }
            if (name == "split" && arguments.size() == 1) {
                return isOpaque(result, "std::vector<ctnative::js_string>");
            }
        }
        if (receiver.getValue() != "ctnative::js_string" || call.getCallee() != "startsWith" ||
            call.getArgOperands().size() != 1 || !result.isInteger(1)) {
            return false;
        }
        return isOpaque(call.getArgOperands()[0].getType(), "ctnative::js_string");
    }
    if (auto call = llvm::dyn_cast<ec::CallOpaqueOp>(op)) {
        // `ctnative.const_operands` lists the operands the callee takes by value
        // or const reference. It describes the C++ operand ABI, never purity or
        // heap effects. Unspecified opaque calls may bind mutable references or
        // select different overloads when the actual argument gains const.
        auto safe = call->getAttrOfType<mlir::DenseI32ArrayAttr>(ctnative::kNativeConstOperands);
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
    // Views are read-only at their creation. Writes and address escapes through
    // a member or subscript propagate backwards to the owning binding.
    return llvm::isa<ec::AddOp, ec::SubOp, ec::MulOp, ec::DivOp, ec::RemOp, ec::CmpOp,
                     ec::LogicalAndOp, ec::LogicalOrOp, ec::LogicalNotOp, ec::UnaryMinusOp,
                     ec::UnaryPlusOp, ec::BitwiseAndOp, ec::BitwiseOrOp, ec::BitwiseXorOp,
                     ec::BitwiseNotOp, ec::BitwiseLeftShiftOp, ec::BitwiseRightShiftOp,
                     ec::ConditionalOp, ec::CastOp, ec::LoadOp, ec::MemberOp, ec::MemberOfPtrOp,
                     ec::GetFieldOp, ec::SubscriptOp, ec::DereferenceOp, ec::ReturnOp, ec::YieldOp,
                     ec::ExpressionOp, ec::IfOp, ec::ForOp, ec::SwitchOp, mlir::cf::BranchOp,
                     mlir::cf::CondBranchOp, mlir::func::ReturnOp, ctnative::CppThrowOp>(op);
}

} // namespace ctcompile::cpp
