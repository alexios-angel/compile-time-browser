#include "Analysis.h"
#include "ctcompile/CTNative/Analysis/ImmutableCaptures.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/STLExtras.h"

namespace ctcompile::ctnative {
BindingTimeAnalysis::Impl::fact BindingTimeAnalysis::Impl::scalar(mlir::Operation * op,
                                                                  flow & state) {
    using kind = fact::kind;
    if (auto constant = llvm::dyn_cast<ctjs::ConstantOp>(op)) {
        if (llvm::isa<ctjs::NumberAttr, ctjs::BooleanAttr, ctjs::NullAttr, ctjs::UndefinedAttr,
                      ctjs::StringAttr>(constant.getValue())) {
            return {kind::primitive, BindingTime::Static, constant.getValue(), {}};
        }
    }
    if (auto constant = llvm::dyn_cast<mlir::arith::ConstantOp>(op)) {
        if (llvm::isa<mlir::IntegerAttr>(constant.getValue())) {
            return {kind::primitive, BindingTime::Static, constant.getValue(), {}};
        }
    }
    if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(op);
        unary && unary.getKind() == ctjs::UnaryKind::Void) {
        return {kind::primitive,
                BindingTime::Static,
                ctjs::UndefinedAttr::get(module.getContext()),
                {}};
    }
    bool known = true, primitives = true;
    for (mlir::Value value : op->getOperands()) {
        const auto input = state.values.lookup(value);
        known &= input.time == BindingTime::Static;
        primitives &= input.domain == kind::primitive;
    }
    if (!known) { return {}; }
    bool supported = false;
    if (llvm::isa<ctjs::BinaryOp, ctjs::BinaryStaticOp, mlir::arith::TruncIOp>(op)) {
        supported = primitives;
    } else if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(op)) {
        supported = primitives || unary.getKind() == ctjs::UnaryKind::Not ||
                    unary.getKind() == ctjs::UnaryKind::TypeOf;
    } else if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(op)) {
        supported = primitives || compare.getKind() == ctjs::CompareKind::StrictEq;
    } else if (llvm::isa<ctjs::TruthyOp>(op)) {
        supported = true;
    }
    return supported ? fact{kind::primitive, BindingTime::Static, {}, {}} : fact{};
}

bool BindingTimeAnalysis::Impl::operation(mlir::Operation * op, flow & state, bool control) {
    using kind = fact::kind;
    fact result;
    bool eligible = false;
    std::string reason;
    if (op->getName().getStringRef() == "ub.poison") {
        result.time = BindingTime::Unknown;
        eligible = true; // inert unknown slot; consumers still require a known value
    } else if (llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp>(op)) {
        eligible = true;
        result = {kind::bookkeeping, BindingTime::Static, {}, {}};
    } else if (llvm::isa<ctjs::ReturnOp, mlir::scf::YieldOp, mlir::scf::ConditionOp>(op)) {
        eligible = llvm::all_of(op->getOperands(), [&](mlir::Value value) {
            return state.values.lookup(value).time == BindingTime::Static;
        });
    } else if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(op)) {
        auto target =
            mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(call, call.getCalleeAttr());
        eligible = target && complete.lookup(target) &&
                   binding_time_detail::effectCalleeMatches(call, module) &&
                   !constructsClosures(target) && target.getUpvalueCount() == 0 &&
                   mlir::SymbolTable::getSymbolVisibility(target) ==
                       mlir::SymbolTable::Visibility::Private &&
                   llvm::all_of(call.getArgs(), [&](mlir::Value arg) {
                       return state.values.lookup(arg).time == BindingTime::Static;
                   });
        if (eligible) {
            result = returns.lookup(target);
        } else if (effects->invalidateCall(call, state)) {
            reason = "runtime call with proved argument-local heap effects";
        } else {
            binding_time_detail::invalidate(state);
        }
    } else if (llvm::isa<ctjs::CreateObjectOp, ctjs::ConstructOp, ctjs::LoadGlobalOp,
                         ctjs::GetPropertyOp, ctjs::SetPropertyOp, ctjs::CallOp, ctjs::CreateCellOp,
                         ctjs::CellGetOp, ctjs::CellSetOp, ctjs::CreateClosureOp>(op)) {
        result = memory(op, state, control, eligible);
        if (!eligible) { binding_time_detail::invalidate(state); }
    } else {
        result = scalar(op, state);
        eligible = result.time == BindingTime::Static;
        // A dynamic numeric expression does not mutate heap state, but generic
        // object coercion and unknown operations may invoke arbitrary code.
        bool scalarOnly = llvm::all_of(op->getOperands(), [&](mlir::Value value) {
            return state.values.lookup(value).domain == kind::primitive;
        });
        const bool operatorOp = llvm::isa<ctjs::BinaryOp, ctjs::BinaryStaticOp, ctjs::UnaryOp,
                                          ctjs::CompareOp, ctjs::TruthyOp, mlir::arith::ConstantOp,
                                          mlir::arith::TruncIOp, ctjs::ConstantOp>(op);
        if (!operatorOp || (!eligible && !scalarOnly)) { binding_time_detail::invalidate(state); }
    }
    if (!control) {
        reason = "runtime control decides whether this operation executes";
    } else if (!eligible && reason.empty()) {
        reason = "runtime operands, heap contents or effects";
    } else if (eligible) {
        reason = "static operands and local effects";
    }
    decisions[op] = {control && eligible, std::move(reason)};
    for (mlir::Value output : op->getResults()) {
        state.values[output] = result;
        facts[output] = result;
    }
    return control && eligible;
}
} // namespace ctcompile::ctnative
