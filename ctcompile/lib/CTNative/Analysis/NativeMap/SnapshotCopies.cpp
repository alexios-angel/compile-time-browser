#include "SnapshotCopies.h"

namespace ctcompile::ctnative::map_detail {
namespace {
llvm::StringRef keyOf(mlir::Value key) {
    auto constant = key.getDefiningOp<ctjs::ConstantOp>();
    if (!constant) { return {}; }
    auto string = llvm::dyn_cast<ctjs::StringAttr>(constant.getValue());
    return string ? string.getValue() : llvm::StringRef{};
}

std::string proveImmediateConsumption(ctjs::CallOp iterator, const snapshotCopies & copies) {
    ctjs::CallOp consumed;
    for (mlir::OpOperand & use : iterator.getResult().getUses()) {
        if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
        auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
        if (!call || !copies.calls.contains(call) || use.getOperandNumber() != 2 || consumed) {
            return "native Map iterator requires one immediate Array.from consumption";
        }
        consumed = call;
    }
    if (!consumed || iterator->getBlock() != consumed->getBlock() ||
        !iterator->isBeforeInBlock(consumed)) {
        return "native Map iterator requires one immediate Array.from consumption";
    }
    // Map iterators are stateful and observe mutations before consumption.
    // An eager native vector is equivalent only when its single consumer
    // cannot observe that timing or consume the same iterator a second time.
    for (auto * op = iterator->getNextNode(); op != consumed.getOperation();
         op = op->getNextNode()) {
        if (!llvm::isa<ctjs::ConstantOp, ctjs::RootOp>(op) && !copies.builtins.contains(op)) {
            return "native Map iterator cannot cross effects before Array.from";
        }
    }
    return {};
}
} // namespace

std::string collectSnapshotCopies(mlir::ModuleOp module,
                                  const llvm::DenseSet<mlir::Operation *> & mapCalls,
                                  snapshotCopies & out) {
    std::string reason;
    module.walk([&](ctjs::LoadGlobalOp load) {
        if (load.getName() != "Array" || !reason.empty()) { return; }
        out.builtins.insert(load);
        for (mlir::OpOperand & use : load.getResult().getUses()) {
            // The receiver use is checked again together with its callee.
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                call && use.getOperandNumber() == 1) {
                auto method = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                if (method && method.getObject() == load.getResult() &&
                    keyOf(method.getKey()) == "from") {
                    continue;
                }
            }
            auto method = llvm::dyn_cast<ctjs::GetPropertyOp>(use.getOwner());
            if (!method || use.getOperandNumber() != 0 || keyOf(method.getKey()) != "from") {
                reason = "standard Array.from identity escapes, is inspected or is mutated";
                return;
            }
            out.builtins.insert(method);
            for (mlir::OpOperand & methodUse : method.getResult().getUses()) {
                auto call = llvm::dyn_cast<ctjs::CallOp>(methodUse.getOwner());
                if (!call || methodUse.getOperandNumber() != 0 ||
                    call.getReceiver() != load.getResult()) {
                    reason = "standard Array.from requires its exact Array receiver";
                    return;
                }
                if (call.getArgs().size() != 1) {
                    reason = "native Array.from snapshot copy requires exactly one argument";
                    return;
                }
                auto snapshot = call.getArgs().front().getDefiningOp<ctjs::CallOp>();
                auto read = snapshot ? snapshot.getCallee().getDefiningOp<ctjs::GetPropertyOp>()
                                     : ctjs::GetPropertyOp{};
                if (!snapshot || !mapCalls.contains(snapshot) || !read ||
                    (keyOf(read.getKey()) != "keys" && keyOf(read.getKey()) != "values")) {
                    reason = "native Array.from requires a proved Map keys or values snapshot";
                    return;
                }
                out.calls.insert(call);
            }
        }
    });
    if (!reason.empty()) { return reason; }
    if (!out.builtins.empty()) {
        module.walk([&](ctjs::StoreGlobalOp store) {
            if (store.getName() == "Array") {
                reason = "standard Array binding is assigned in this program";
            }
        });
    }
    if (!reason.empty()) { return reason; }
    for (mlir::Operation * op : mapCalls) {
        auto call = llvm::cast<ctjs::CallOp>(op);
        auto method = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
        if (method && (keyOf(method.getKey()) == "keys" || keyOf(method.getKey()) == "values")) {
            reason = proveImmediateConsumption(call, out);
            if (!reason.empty()) { return reason; }
        }
    }
    return reason;
}

} // namespace ctcompile::ctnative::map_detail
