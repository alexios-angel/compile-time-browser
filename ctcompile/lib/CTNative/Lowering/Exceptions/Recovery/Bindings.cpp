// recovery - the closed source-declaration proof: every global read in the
// protected region resolves to a uniquely bound, nonthrowing private body.
//
// One of six files carved out of a 1,251-line Exceptions/Recovery.cpp on
// 2026-09-08. All are member functions of `recovery`, the disposable-clone
// transaction declared in Attempt.h beside this; Exceptions/Recovery.h, the
// header the rest of the lowering sees, did not change.

#include "Attempt.h"

namespace ctcompile::ctnative::lowering_detail {

bool recovery::hasOrigin(mlir::Value value, mlir::Value origin) {
    unsigned used = 0;
    bool found = ctjs::globals_detail::registerFlowHasOrigin(value, origin, remaining, &used);
    if (!spend(used)) { return false; }
    if (!found && remaining == 0) { return spend(); }
    return found;
}

std::optional<llvm::SmallVector<mlir::OpOperand *>> recovery::uses(mlir::Value value) {
    unsigned used = 0;
    auto found = ctjs::globals_detail::registerFlowUses(value, remaining, &used);
    if (!spend(used)) { return {}; }
    if (!found && remaining == 0) { (void)spend(); }
    return found;
}

bool recovery::bindingFailure(llvm::StringRef why) {
    return reject(("native invocation recovery cannot prove nonthrowing source callee "
                   "bindings: " +
                   why)
                      .str());
}

bool recovery::proveBindings() {
    // This is a closed source-declaration proof, not permission from the
    // resolved symbol or a host name. Inventory every current body before
    // accepting any load. A missing body, property operation, host call or
    // coercion that could reenter leaves the original checks intact.
    if (!module) { return bindingFailure("the source module is unavailable"); }
    if (auto skipped = module->getAttr("ctjs.skipped")) {
        auto rows = llvm::dyn_cast<mlir::ArrayAttr>(skipped);
        if (!rows || !rows.empty()) { return bindingFailure("source bodies are missing"); }
    }
    llvm::DenseMap<unsigned, ctjs::FuncOp> functions;
    llvm::DenseSet<mlir::StringAttr> symbols;
    llvm::DenseMap<mlir::StringAttr, llvm::SmallVector<ctjs::StoreGlobalOp>> stores;
    llvm::SmallVector<ctjs::LoadGlobalOp> loads;
    llvm::SmallVector<ctjs::CreateClosureOp> closures;
    llvm::SmallVector<ctjs::CallDirectOp> calls;
    llvm::SmallVector<mlir::Operation *> operations;
    for (mlir::Operation & operation : module.getBody()->getOperations()) {
        if (!spend()) { return false; }
        auto body = llvm::dyn_cast<ctjs::FuncOp>(operation);
        auto index = body ? functionIndex(body) : std::optional<unsigned>{};
        if (!body || !index || body.getBody().empty() || body.getUpvalueCount() != 0 ||
            body.getBody().front().getNumArguments() < 3 || body->hasAttr("ctjs.not_lowered") ||
            !functions.try_emplace(*index, body).second ||
            !symbols.insert(body.getSymNameAttr()).second) {
            return bindingFailure("source function identity or body is incomplete");
        }
        auto walked = body.getBody().walk([&](mlir::Operation * op) {
            if (!spend(uint64_t(1) + op->getNumOperands())) {
                return mlir::WalkResult::interrupt();
            }
            operations.push_back(op);
            if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(op)) {
                stores[store.getNameAttr()].push_back(store);
            }
            if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op)) { loads.push_back(load); }
            if (auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(op)) {
                closures.push_back(closure);
            }
            if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(op)) { calls.push_back(call); }
            return mlir::WalkResult::advance();
        });
        if (walked.wasInterrupted()) { return false; }
    }
    auto entry = functions.lookup(0);
    if (!entry) { return bindingFailure("there is no unique source entry"); }
    llvm::DenseSet<mlir::Operation *> declarations;
    for (auto closure : closures) {
        if (!spend()) { return false; }
        auto target = closure.getFunction() < 0
                          ? ctjs::FuncOp{}
                          : functions.lookup(static_cast<unsigned>(closure.getFunction()));
        if (!target || target == entry || !closure.getUpvalues().empty() ||
            closure->getBlock() != &entry.getBody().front() ||
            closure.getEnclosingClosure() != entry.getBody().front().getArgument(2) ||
            mlir::SymbolTable::getSymbolVisibility(target) !=
                mlir::SymbolTable::Visibility::Private) {
            return bindingFailure("a closure lacks its exact source declaration");
        }
        auto currentUses = uses(closure.getResult());
        if (!currentUses) { return false; }
        ctjs::StoreGlobalOp declaration;
        for (mlir::OpOperand * use : *currentUses) {
            if (!spend()) { return false; }
            if (llvm::isa<ctjs::RootOp>(use->getOwner())) { continue; }
            auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(use->getOwner());
            if (!store || declaration || store.getValue() != closure.getResult() ||
                stores.find(store.getNameAttr())->second.size() != 1 ||
                store->getBlock() != &entry.getBody().front()) {
                return bindingFailure("a function binding is mutated or its closure escapes");
            }
            declaration = store;
        }
        if (!declaration) { return bindingFailure("a closure is not uniquely bound"); }
        for (mlir::Operation & before : entry.getBody().front()) {
            if (!spend()) { return false; }
            if (&before == declaration.getOperation()) { break; }
            if (!llvm::isa<ctjs::FrameEnterOp, ctjs::ConstantOp, ctjs::CreateClosureOp,
                           ctjs::StoreGlobalOp, ctjs::RootOp>(before)) {
                return bindingFailure("a call may precede declaration initialization");
            }
        }
        bindings.try_emplace(declaration.getNameAttr(), target);
        boundTargets.insert(target);
        declarations.insert(declaration);
    }
    llvm::DenseSet<mlir::Operation *> knownCalls;
    for (auto load : loads) {
        if (!spend()) { return false; }
        auto target = bindings.lookup(load.getNameAttr());
        if (!target) { return bindingFailure("a global read has a host or unknown alternative"); }
        auto currentUses = uses(load.getResult());
        if (!currentUses) { return false; }
        for (mlir::OpOperand * use : *currentUses) {
            if (!spend()) { return false; }
            if (llvm::isa<ctjs::RootOp>(use->getOwner())) { continue; }
            auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use->getOwner());
            if (!call || use->getOperandNumber() != 2 || call.getCallee() != target.getSymName() ||
                call.getNumOperands() != target.getBody().front().getNumArguments() ||
                !hasOrigin(call.getCalleeValue(), load.getResult())) {
                return bindingFailure("a callee has an unknown use, identity or predecessor");
            }
            knownCalls.insert(call);
        }
    }
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::FuncOp>> callees;
    llvm::DenseMap<mlir::Operation *, unsigned> incoming;
    for (auto call : calls) {
        if (!spend()) { return false; }
        if (!knownCalls.contains(call)) {
            return bindingFailure("a direct-call symbol lacks live callee identity");
        }
        auto target = module.lookupSymbol<ctjs::FuncOp>(call.getCallee());
        auto current = llvm::cast<ctjs::CallDirectOp>(copies.lookupOrDefault(call.getOperation()));
        boundCalls.insert(current);
        boundCallers[target].push_back(current);
        callees[call->getParentOfType<ctjs::FuncOp>()].push_back(target);
        ++incoming[target];
    }
    // Completion operands may cross more than one call. Establish the
    // entire live source call graph before following formals backwards or
    // payloads forwards: a recursive family cannot borrow a primitive
    // seed from one of its nonrecursive actuals. Include unused bodies and
    // repeated call edges; neither a prior summary nor reachability trims
    // this proof's obligations.
    llvm::SmallVector<ctjs::FuncOp> ready;
    for (auto [index, body] : functions) {
        (void)index;
        if (!spend()) { return false; }
        if (incoming.lookup(body) == 0) { ready.push_back(body); }
    }
    unsigned visited = 0;
    while (!ready.empty()) {
        if (!spend()) { return false; }
        auto body = ready.pop_back_val();
        ++visited;
        for (auto target : callees[body]) {
            if (!spend()) { return false; }
            if (--incoming[target] == 0) { ready.push_back(target); }
        }
    }
    if (visited != functions.size()) {
        return bindingFailure("the source call component is recursive");
    }
    for (auto [index, body] : functions) {
        if (!spend()) { return false; }
        auto currentUses = uses(body.getBody().front().getArgument(2));
        if (!currentUses) { return false; }
        for (mlir::OpOperand * use : *currentUses) {
            if (!spend()) { return false; }
            if (llvm::isa<ctjs::RootOp>(use->getOwner()) ||
                (index == 0 && llvm::isa<ctjs::CreateClosureOp>(use->getOwner()) &&
                 use->getOperandNumber() == 0)) {
                continue;
            }
            return bindingFailure("an implicit callee value is observed or escapes");
        }
    }
    // Declarations publish own source bindings before any call. Other
    // named stores are allowed only in the entry, and the complete name
    // census above already excludes writes to a callable binding.
    for (mlir::Operation * original : operations) {
        if (!spend()) { return false; }
        auto * op = copies.lookupOrDefault(original);
        if (auto thrown = llvm::dyn_cast<ctjs::ThrowOp>(op)) {
            // Throw diagnostics may coerce the payload. Prove every
            // actual input through the whole call family too; an uncalled body with
            // an unknown payload cannot borrow a represented invoke's
            // primitive payload fact.
            if (primitive(thrown.getValue())) { continue; }
            if (!refusal.empty()) { return false; }
            return bindingFailure("a throw payload has an unknown actual or uncalled formal");
        }
        if (llvm::isa<ctjs::CreateClosureOp, ctjs::CallDirectOp, ctjs::FrameEnterOp,
                      ctjs::PushHandlerOp>(op)) {
            continue;
        }
        if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(original)) {
            if (declarations.contains(store) || store->getParentOfType<ctjs::FuncOp>() == entry) {
                continue;
            }
            return bindingFailure("a non-entry body writes global state");
        }
        if (!nonthrowing(op)) {
            return bindingFailure(("an operation may alter bindings through reentry: `" +
                                   op->getName().getStringRef() + "`")
                                      .str());
        }
    }
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
