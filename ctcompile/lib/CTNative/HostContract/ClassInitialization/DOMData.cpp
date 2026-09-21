#include "Proof.hpp"

#include "llvm/ADT/ScopeExit.h"

namespace ctcompile::ctnative::class_detail {

bool classInitialization::proveDOMDataFamily(const HostContract & contract) {
    auto entry = module.lookupSymbol<ctjs::FuncOp>(contract.entry);
    llvm::SmallVector<std::pair<mlir::BlockArgument, mlir::OpOperand *>> uses;
    for (mlir::BlockArgument input :
         entry.getBody().front().getArguments().drop_front(ctjs::implicit_arguments)) {
        for (mlir::OpOperand * use : sourceUses(input)) {
            if (!step()) { return false; }
            if (!llvm::isa<ctjs::RootOp>(use->getOwner())) { uses.emplace_back(input, use); }
        }
    }
    if (!reason.empty() || uses.empty()) { return reason.empty(); }
    // These facts authorize source effects only, never native storage. The
    // provider and owned-global analyses still reprove the rewritten module.
    host_detail::analyzer analysis(module, contract, remaining);
    const llvm::scope_exit recordWork([&] { remaining = analysis.remaining; });
    const auto reject = [&] {
        return refuse(analysis.exhausted
                          ? "class initialization work budget exhausted"
                          : "class DOM input has an observer outside its proved Map keys");
    };
    if (contract.roots.size() != 1 || contract.roots.front().properties.size() != 1) {
        return reject();
    }
    auto slot = analysis.slot(contract.roots.front(), contract.roots.front().properties[0]);
    if (!slot.reason.empty() || slot.writes.size() != 1) { return reject(); }
    llvm::DenseMap<mlir::Operation *, HostCallableEdge> edges;
    ctjs::ConstructOp allocation;
    for (auto [input, use] : uses) {
        if (!analysis.step()) { return reject(); }
        auto found = edges.find(use->getOwner());
        if (found == edges.end()) {
            auto edge = analysis.propertyCall(use->getOwner());
            if (!edge || !edge->capturedMap) { return reject(); }
            found = edges.try_emplace(use->getOwner(), std::move(*edge)).first;
        }
        auto & edge = found->second;
        auto & capture = *edge.capturedMap;
        const unsigned offset =
            (llvm::isa<ctjs::CallDirectOp>(edge.call) ? 3u : 2u) + (capture.argument ? 1u : 0u);
        if (use->getOperandNumber() < offset ||
            use->getOperandNumber() - offset >= edge.arguments.size()) {
            return reject();
        }
        const auto & argument = edge.arguments[use->getOperandNumber() - offset];
        if (argument.element != input || argument.actual != use->get() ||
            !llvm::is_contained(capture.outerKeyParameters, argument.parameter) ||
            !llvm::is_contained(capture.outerKeyInputs, input) ||
            (allocation && allocation != capture.allocation)) {
            return reject();
        }
        allocation = capture.allocation;
        auto table = edge.read.getObject().getDefiningOp<ctjs::GetPropertyOp>();
        if (!table || !llvm::any_of(slot.edges, [&](HostSlotEdge published) {
                return analysis.step() && published.read == table;
            })) {
            return reject();
        }
    }
    for (ctjs::GetPropertyOp table : slot.reads) {
        for (mlir::OpOperand & use : table.getResult().getUses()) {
            if (!analysis.step()) { return reject(); }
            if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
            if (!llvm::any_of(edges, [&](const auto & item) {
                    const auto & edge = item.second;
                    return analysis.step() &&
                           ((use.getOwner() == edge.read && use.getOperandNumber() == 0) ||
                            (use.getOwner() == edge.call &&
                             use.getOperandNumber() ==
                                 (llvm::isa<ctjs::CallDirectOp>(edge.call) ? 0u : 1u)));
                })) {
                return reject();
            }
        }
    }
    for (auto & [call, edge] : edges) {
        auto & capture = *edge.capturedMap;
        for (mlir::Operation * op :
             {call, edge.read.getOperation(), capture.intrinsic.getOperation(),
              capture.allocation.getOperation(), capture.cell.getOperation(),
              capture.initialization.getOperation(), capture.argument.getOperation()}) {
            if (!analysis.step()) { return reject(); }
            if (op) { domDataOperations.insert(op); }
        }
        for (ctjs::CreateClosureOp closure : capture.closures) {
            if (!analysis.step()) { return reject(); }
            domDataOperations.insert(closure);
            // capturedMap publishes only after every complete reusable body,
            // invocation and outer-key observer has passed its own census.
            auto member = analysis.callable(closure.getResult());
            if (!member) { return reject(); }
            member.walk([&](mlir::Operation * op) {
                if (analysis.step()) { domDataOperations.insert(op); }
            });
        }
    }
    auto factory = allocation->getParentOfType<ctjs::FuncOp>();
    auto * factoryCall = slot.writes.front().getValue().getDefiningOp();
    auto wrapper = slot.writes.front()->getParentOfType<ctjs::FuncOp>();
    if (!factoryCall || wrapper == entry || wrapper == factory ||
        analysis.target(factoryCall) != factory || !analysis.exactCall(factoryCall) ||
        analysis.callers[wrapper].size() != 1) {
        return reject();
    }
    auto wrapperCall = llvm::dyn_cast<ctjs::CallDirectOp>(analysis.callers[wrapper].front());
    if (!wrapperCall || wrapperCall->getParentOfType<ctjs::FuncOp>() != entry ||
        !undefined(wrapperCall.getReceiver()) || !undefined(wrapperCall.getNewTarget()) ||
        !analysis.exactCall(wrapperCall)) {
        return reject();
    }
    for (unsigned index : {ctjs::arg_receiver, ctjs::arg_new_target}) {
        for (mlir::Operation * user : wrapper.getBody().front().getArgument(index).getUsers()) {
            if (!analysis.step() || !llvm::isa<ctjs::RootOp>(user)) { return reject(); }
        }
    }
    // Only the wrapper's signature and exact calls gain authority. Its body
    // and the factory body retain the complete class source-effect census.
    domDataOperations.insert(wrapper);
    domDataOperations.insert(wrapperCall);
    domDataOperations.insert(factoryCall);
    module.walk([&](ctjs::LoadGlobalOp load) {
        const auto & stores = analysis.globals[load.getName()];
        if (analysis.step() && stores.size() == 1 && analysis.before(stores.front(), load) &&
            analysis.object(load.getResult()) == slot.owner) {
            domDataOperations.insert(load);
        }
    });
    if (analysis.exhausted) { return refuse("class initialization work budget exhausted"); }
    return true;
}

} // namespace ctcompile::ctnative::class_detail
