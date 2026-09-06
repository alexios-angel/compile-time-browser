#include "Analysis.h"

#include "mlir/Dialect/SCF/IR/SCF.h"
#include "llvm/ADT/StringSwitch.h"

namespace ctcompile::ctnative::host_detail {

llvm::StringRef keyOf(mlir::Value value) {
    auto constant = value.getDefiningOp<ctjs::ConstantOp>();
    auto key =
        constant ? llvm::dyn_cast<ctjs::StringAttr>(constant.getValue()) : ctjs::StringAttr{};
    return key ? key.getValue() : llvm::StringRef{};
}

bool ordinaryKey(llvm::StringRef key) {
    return !key.empty() &&
           !llvm::StringSwitch<bool>(key)
                .Cases({"__proto__", "prototype", "constructor"}, true)
                .Cases({"toString", "valueOf", "toLocaleString"}, true)
                .Cases({"hasOwnProperty", "isPrototypeOf", "propertyIsEnumerable"}, true)
                .Cases({"__defineGetter__", "__defineSetter__"}, true)
                .Cases({"__lookupGetter__", "__lookupSetter__"}, true)
                .Default(false);
}

bool analyzer::active(mlir::Operation * operation) {
    for (auto * child = operation; child->getParentOp() != nullptr; child = child->getParentOp()) {
        if (!step()) { return true; }
        auto branch = llvm::dyn_cast<mlir::scf::IfOp>(child->getParentOp());
        if (!branch) { continue; }
        const auto selected = truth(branch.getCondition());
        if (selected && child->getParentRegion() != &branch->getRegion(*selected ? 0u : 1u)) {
            return false;
        }
    }
    return true;
}

mlir::Operation * analyzer::anchor(mlir::Operation * operation) {
    while (!llvm::isa<ctjs::FuncOp>(operation->getParentOp())) {
        if (!step()) { return nullptr; }
        auto branch = llvm::dyn_cast_or_null<mlir::scf::IfOp>(operation->getParentOp());
        if (!branch) { return nullptr; }
        auto selected = truth(branch.getCondition());
        if (!selected || operation->getParentRegion() != &branch->getRegion(*selected ? 0u : 1u)) {
            return nullptr;
        }
        operation = branch;
    }
    auto function = llvm::cast<ctjs::FuncOp>(operation->getParentOp());
    return operation->getBlock() == &function.getBody().front() ? operation : nullptr;
}

bool analyzer::before(mlir::Operation * first, mlir::Operation * second) {
    llvm::DenseSet<mlir::Operation *> visited;
    for (;;) {
        if (!first || !second || !step() || !visited.insert(first).second) { return false; }
        auto from = first->getParentOfType<ctjs::FuncOp>();
        auto to = second->getParentOfType<ctjs::FuncOp>();
        if (!from || !to) { return false; }
        if (from == to) {
            if (dominance.properlyDominates(first, second)) { return true; }
            auto * firstAnchor = anchor(first);
            auto * secondAnchor = anchor(second);
            return firstAnchor && secondAnchor && firstAnchor != secondAnchor &&
                   dominance.properlyDominates(firstAnchor, secondAnchor);
        }
        // This first slice has one statically visible invocation path per
        // helper. Multiple calls/recursion require context-indexed slot state,
        // rather than equating allocations made by different invocations.
        if (from != entry && callers[from].size() == 1 && anchor(first)) {
            auto call = callers[from].front();
            if (!exactCall(call)) { return false; }
            first = call;
            continue;
        }
        if (to != entry && callers[to].size() == 1) {
            auto call = callers[to].front();
            if (!exactCall(call)) { return false; }
            second = call;
            visited.erase(first);
            continue;
        }
        return false;
    }
}

HostSlotReport analyzer::slot(const HostRootRequest & request, llvm::StringRef key) {
    HostSlotReport result;
    result.binding = request.binding;
    result.property = key.str();
    const auto reject = [&](llvm::StringRef why) {
        if (result.reason.empty()) { result.reason = why.str(); }
    };
    const auto & stores = globals[request.binding];
    if (stores.size() != 1) {
        reject("root binding requires exactly one source initialization");
        return result;
    }
    auto initialization = stores.front();
    result.owner = initialization.getValue().getDefiningOp<ctjs::CreateObjectOp>();
    if (!result.owner || initialization->getParentOfType<ctjs::FuncOp>() != entry ||
        result.owner->getParentOfType<ctjs::FuncOp>() != entry || !anchor(initialization)) {
        reject("root must be a fresh object initialized unconditionally by the script entry");
        return result;
    }
    if (!ordinaryKey(key)) { reject("publication requires an ordinary constant own-data key"); }

    llvm::SmallVector<mlir::Value> aliases;
    module.walk([&](mlir::Operation * operation) {
        if (!active(operation)) { return; }
        for (mlir::Value value : operation->getResults()) {
            if (object(value) == result.owner) { aliases.push_back(value); }
        }
        for (mlir::Region & region : operation->getRegions()) {
            for (mlir::Block & block : region) {
                for (mlir::BlockArgument argument : block.getArguments()) {
                    if (object(argument) == result.owner) { aliases.push_back(argument); }
                }
            }
        }
    });
    llvm::DenseSet<mlir::Operation *> seenReads, seenWrites;
    for (mlir::Value value : aliases) {
        if (auto load = value.getDefiningOp<ctjs::LoadGlobalOp>()) {
            const auto & definitions = globals[load.getName()];
            if (definitions.size() != 1 || !before(definitions.front(), load)) {
                reject("root or alias load is not dominated by its binding initialization");
            }
        }
        for (mlir::OpOperand & use : value.getUses()) {
            auto * operation = use.getOwner();
            if (!active(operation)) { continue; }
            if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation);
                read && use.getOperandNumber() == 0) {
                if (!ordinaryKey(keyOf(read.getKey()))) {
                    reject("dynamic or prototype root read");
                }
                if (keyOf(read.getKey()) == key && seenReads.insert(read).second) {
                    result.reads.push_back(read);
                }
                continue;
            }
            if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(operation);
                write && use.getOperandNumber() == 0) {
                if (!ordinaryKey(keyOf(write.getKey()))) {
                    reject("dynamic or prototype root write");
                }
                if (keyOf(write.getKey()) == key && seenWrites.insert(write).second) {
                    result.writes.push_back(write);
                }
                continue;
            }
            if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(operation);
                store && globals[store.getName()].size() == 1) {
                continue;
            }
            if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(operation);
                call && use.getOperandNumber() >= 3 && exactCall(call)) {
                continue;
            }
            if (llvm::isa<ctjs::RootOp, ctjs::TruthyOp>(operation)) { continue; }
            if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation);
                unary && (unary.getKind() == ctjs::UnaryKind::TypeOf ||
                          unary.getKind() == ctjs::UnaryKind::Not ||
                          unary.getKind() == ctjs::UnaryKind::Void)) {
                continue;
            }
            if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation);
                compare && compare.getKind() == ctjs::CompareKind::StrictEq) {
                continue;
            }
            if (auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(operation)) {
                auto branch = llvm::dyn_cast<mlir::scf::IfOp>(yield->getParentOp());
                if (branch && object(branch.getResult(use.getOperandNumber())) == result.owner) {
                    continue;
                }
            }
            if (auto returned = llvm::dyn_cast<ctjs::ReturnOp>(operation);
                returned && returned->getParentOfType<ctjs::FuncOp>() != entry) {
                continue;
            }
            reject(("root escapes or has an unsupported use through `" +
                    operation->getName().getStringRef() + "`")
                       .str());
        }
    }
    if (result.writes.empty()) { reject("publication slot has no visible source write"); }
    if (result.reads.empty()) { reject("publication slot has no visible source read"); }
    for (ctjs::GetPropertyOp read : result.reads) {
        ctjs::SetPropertyOp latest;
        for (ctjs::SetPropertyOp write : result.writes) {
            if (before(write, read)) {
                if (!latest || before(latest, write)) {
                    latest = write;
                } else if (!before(write, latest)) {
                    reject("publication writes have ambiguous order");
                }
            } else if (!before(read, write)) {
                reject("publication write/read order requires unsupported control or call context");
            }
        }
        if (latest) {
            result.edges.push_back({latest, read});
        } else {
            reject("publication read has no dominating own-data initialization");
        }
    }
    return result;
}

} // namespace ctcompile::ctnative::host_detail
