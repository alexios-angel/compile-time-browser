#include "ValueFlow.h"
#include "ctcompile/CTNative/Analysis/NativeMap.h"
#include "mlir/Dialect/SCF/IR/SCF.h"

namespace ctcompile::ctnative::object_detail {

bool mapPayloadUse(mlir::OpOperand & use) {
    auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
    return call && nativeMapAction(call) == "set" && use.getOperandNumber() == 3;
}

void connectValues(closedValueFlow & flow, mlir::ModuleOp module) {
    llvm::DenseMap<mlir::Value, bool> inert;
    const auto skip = [&](mlir::Value value) {
        const auto [at, fresh] = inert.try_emplace(value, false);
        if (fresh) { at->second = inertPoison(value); }
        return at->second;
    };
    const auto join = [&](mlir::Value left, mlir::Value right) {
        // One poison can stand for unrelated, unobserved register slots.
        // It has no schema and must not unify the live incoming families.
        if (!skip(left) && !skip(right)) { flow.join(left, right); }
    };
    llvm::DenseMap<int64_t, mlir::Value> payloads;
    module.walk([&](ctjs::CallOp call) {
        const auto group = nativeMapGroup(call.getReceiver());
        if (group < 0) { return; }
        mlir::Value value;
        if (nativeMapAction(call) == "set" && call.getArgs().size() == 2) {
            value = call.getArgs()[1];
        } else if (nativeMapAction(call) == "get") {
            value = call.getResult();
        }
        if (!value) { return; }
        const auto [at, fresh] = payloads.try_emplace(group, value);
        if (!fresh) { flow.join(at->second, value); }
    });
    module.walk([&](mlir::scf::IfOp branch) {
        for (mlir::Region & region : branch->getRegions()) {
            if (region.empty()) { continue; }
            auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(region.front().getTerminator());
            if (!yield) { continue; }
            for (auto [result, value] : llvm::zip(branch.getResults(), yield.getResults())) {
                join(result, value);
            }
        }
    });
    module.walk([&](mlir::scf::WhileOp loop) {
        for (auto [initial, argument] : llvm::zip(loop.getInits(), loop.getBeforeArguments())) {
            join(initial, argument);
        }
        auto condition = loop.getConditionOp();
        for (auto [value, argument, result] :
             llvm::zip(condition.getArgs(), loop.getAfterArguments(), loop.getResults())) {
            join(value, argument);
            join(value, result);
        }
        for (auto [value, argument] :
             llvm::zip(loop.getYieldOp().getResults(), loop.getBeforeArguments())) {
            join(value, argument);
        }
    });
    module.walk([&](mlir::scf::ForOp loop) {
        auto yield = llvm::cast<mlir::scf::YieldOp>(loop.getBody()->getTerminator());
        for (auto [initial, argument, result, value] :
             llvm::zip(loop.getInitArgs(), loop.getRegionIterArgs(), loop.getResults(),
                       yield.getResults())) {
            join(initial, argument);
            join(argument, result);
            join(argument, value);
        }
    });
}

bool primitiveProducer(mlir::Value value) {
    if (auto constant = value.getDefiningOp<ctjs::ConstantOp>()) {
        return llvm::isa<ctjs::NumberAttr, ctjs::BooleanAttr, ctjs::NullAttr, ctjs::UndefinedAttr>(
            constant.getValue());
    }
    if (llvm::isa_and_nonnull<ctjs::TruthyOp, ctjs::CompareOp, ctjs::BinaryOp, ctjs::BinaryStaticOp,
                              ctjs::UnaryOp>(value.getDefiningOp())) {
        // These produce primitives if they return. Their own operands/effects
        // still pass normal native admission; this is not a purity claim.
        return true;
    }
    const auto action = nativeMapAction(value.getDefiningOp());
    return action == "has" || action == "delete" || action == "size";
}

bool valueObservation(mlir::Operation * op) {
    if (llvm::isa<ctjs::CompareOp, ctjs::TruthyOp, ctjs::RootOp, ctjs::BinaryOp,
                  ctjs::BinaryStaticOp, mlir::scf::YieldOp, mlir::scf::ConditionOp,
                  mlir::scf::WhileOp, mlir::scf::ForOp>(op)) {
        return true;
    }
    if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(op)) {
        // Admission separately rejects object-to-primitive conversions.
        return unary.getKind() == ctjs::UnaryKind::Not ||
               unary.getKind() == ctjs::UnaryKind::TypeOf ||
               unary.getKind() == ctjs::UnaryKind::Plus || unary.getKind() == ctjs::UnaryKind::Neg;
    }
    return false;
}

} // namespace ctcompile::ctnative::object_detail
