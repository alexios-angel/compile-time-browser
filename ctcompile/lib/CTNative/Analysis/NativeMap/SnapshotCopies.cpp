#include "SnapshotCopies.h"

namespace ctcompile::ctnative::map_detail {
namespace {
std::string proveImmediateConsumption(ctjs::CallOp iterator, const snapshotCopies & copies,
                                      llvm::function_ref<bool()> spend) {
    ctjs::CallOp consumed;
    for (mlir::OpOperand & use : iterator.getResult().getUses()) {
        if (!spend()) { return "snapshot proof work budget exhausted"; }
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
        if (!spend()) { return "snapshot proof work budget exhausted"; }
        if (!llvm::isa<ctjs::ConstantOp, ctjs::RootOp>(op) && !copies.builtins.contains(op)) {
            return "native Map iterator cannot cross effects before Array.from";
        }
    }
    return {};
}
} // namespace

std::string collectSnapshotCopies(mlir::Operation * scope,
                                  const llvm::DenseSet<mlir::Operation *> & mapCalls,
                                  snapshotCopies & out, llvm::function_ref<bool()> spend) {
    std::string reason;
    const auto step = [&] {
        if (!spend()) { reason = "snapshot proof work budget exhausted"; }
        return reason.empty();
    };
    scope->walk<mlir::WalkOrder::PreOrder>([&](mlir::Operation * operation) {
        if (!step()) { return mlir::WalkResult::interrupt(); }
        auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation);
        if (!load || load.getName() != "Array") { return mlir::WalkResult::advance(); }
        out.builtins.insert(load);
        for (mlir::OpOperand & use : load.getResult().getUses()) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            // The receiver use is checked again together with its callee.
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                call && use.getOperandNumber() == 1) {
                auto method = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                if (method && method.getObject() == load.getResult() &&
                    ctjs::constantKey(method.getKey()) == "from") {
                    continue;
                }
            }
            auto method = llvm::dyn_cast<ctjs::GetPropertyOp>(use.getOwner());
            if (!method || use.getOperandNumber() != 0 ||
                ctjs::constantKey(method.getKey()) != "from") {
                reason = "standard Array.from identity escapes, is inspected or is mutated";
                return mlir::WalkResult::interrupt();
            }
            out.builtins.insert(method);
            for (mlir::OpOperand & methodUse : method.getResult().getUses()) {
                if (!step()) { return mlir::WalkResult::interrupt(); }
                auto call = llvm::dyn_cast<ctjs::CallOp>(methodUse.getOwner());
                if (!call || methodUse.getOperandNumber() != 0 ||
                    call.getReceiver() != load.getResult()) {
                    reason = "standard Array.from requires its exact Array receiver";
                    return mlir::WalkResult::interrupt();
                }
                if (call.getArgs().size() != 1) {
                    reason = "native Array.from snapshot copy requires exactly one argument";
                    return mlir::WalkResult::interrupt();
                }
                auto snapshot = call.getArgs().front().getDefiningOp<ctjs::CallOp>();
                auto read = snapshot ? snapshot.getCallee().getDefiningOp<ctjs::GetPropertyOp>()
                                     : ctjs::GetPropertyOp{};
                if (!snapshot || !mapCalls.contains(snapshot) || !read ||
                    (ctjs::constantKey(read.getKey()) != "keys" &&
                     ctjs::constantKey(read.getKey()) != "values")) {
                    reason = "native Array.from requires a proved Map keys or values snapshot";
                    return mlir::WalkResult::interrupt();
                }
                out.calls.insert(call);
            }
        }
        return mlir::WalkResult::advance();
    });
    if (!reason.empty()) { return reason; }
    if (!out.builtins.empty()) {
        scope->walk<mlir::WalkOrder::PreOrder>([&](mlir::Operation * operation) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(operation);
                store && store.getName() == "Array") {
                reason = "standard Array binding is assigned in this program";
                return mlir::WalkResult::interrupt();
            }
            return mlir::WalkResult::advance();
        });
    }
    if (!reason.empty()) { return reason; }
    for (mlir::Operation * op : mapCalls) {
        if (!step()) { return reason; }
        auto call = llvm::cast<ctjs::CallOp>(op);
        auto method = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
        if (method && (ctjs::constantKey(method.getKey()) == "keys" ||
                       ctjs::constantKey(method.getKey()) == "values")) {
            reason = proveImmediateConsumption(call, out, spend);
            if (!reason.empty()) { return reason; }
        }
    }
    return reason;
}

} // namespace ctcompile::ctnative::map_detail
