#include "Proof.hpp"

namespace ctcompile::ctnative::dom_source_detail {

bool DOMSource::forwardFields(ctjs::FuncOp function) {
    llvm::SmallVector<ctjs::CreateObjectOp> objects;
    const auto census = function.walk([&](mlir::Operation * operation) {
        if (!step()) { return mlir::WalkResult::interrupt(); }
        if (auto object = llvm::dyn_cast<ctjs::CreateObjectOp>(operation)) {
            objects.push_back(object);
        }
        return mlir::WalkResult::advance();
    });
    if (census.wasInterrupted()) { return false; }
    for (ctjs::CreateObjectOp object : objects) {
        auto & block = *object->getBlock();
        llvm::DenseSet<mlir::Operation *> uses;
        bool confined = true, read = false;
        for (mlir::OpOperand & use : object.getResult().getUses()) {
            if (!step()) { return false; }
            auto * operation = use.getOwner();
            auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(operation);
            auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(operation);
            const bool ordered =
                get ? precedesInStructuredBody(object, get)
                    : operation->getBlock() == &block && object->isBeforeInBlock(operation);
            if (!ordered ||
                (!llvm::isa<ctjs::RootOp>(operation) &&
                 (use.getOperandNumber() != 0 ||
                  !(get ? ctjs::ordinaryKey(get.getKey())
                        : set && ctjs::ordinaryKey(set.getKey()) &&
                              !set.getValue().getDefiningOp<ctjs::CreateClosureOp>())))) {
                confined = false;
                break;
            }
            read |= static_cast<bool>(get);
            uses.insert(operation);
        }
        if (!confined || !read) { continue; }
        // The isolated DOM provider fixes the initial prototype. Only
        // initialized own fields are read; the complete use census rules
        // out aliases, identity observations, accessors and prototype edits.
        // Keep every value producer for the subsequent complete DOM proof.
        // Writes stay in the source block; nested reads see the fields at
        // their enclosing branch/loop, which cannot mutate this object.
        // ponytail: one charged function scan per local object; index stores
        // if large straight-line entries exhaust the existing work budget.
        llvm::StringMap<mlir::Value> fields;
        const auto walked = function.walk([&](mlir::Operation * operation) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            if (!uses.contains(operation)) { return mlir::WalkResult::advance(); }
            if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(operation)) {
                fields[ctjs::constantKey(set.getKey())] = set.getValue();
            } else if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
                auto value = fields.lookup(ctjs::constantKey(get.getKey()));
                if (!value) {
                    refuse("DOM local field read lacks a preceding write");
                    return mlir::WalkResult::interrupt();
                }
                get.getResult().replaceAllUsesWith(value);
            }
            operation->erase();
            return mlir::WalkResult::advance();
        });
        if (walked.wasInterrupted()) { return false; }
        object.erase();
    }
    return true;
}

bool DOMSource::dataObject(ctjs::CreateObjectOp object) {
    // Preserve fresh data allocations for the complete DOM proof. Unlike
    // callable holders, their constructors and uses must not be erased here.
    for (mlir::OpOperand & operand : object.getResult().getUses()) {
        if (!step()) { return false; }
        auto * use = operand.getOwner();
        if (llvm::isa<ctjs::RootOp, ctjs::ReturnOp, mlir::scf::YieldOp, ctjs::CopyPropsOp>(use)) {
            continue;
        }
        if (auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(use);
            store && operand.getOperandNumber() == 0 &&
            !store.getValue().getDefiningOp<ctjs::CreateClosureOp>() &&
            precedesInStructuredBody(object, store)) {
            // Keep assignment order intact. Key/value semantics still need
            // complete DOM proof; callable slots retain their separate proof.
            continue;
        }
        if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(use);
            unary && unary.getKind() == ctjs::UnaryKind::TypeOf) {
            continue;
        }
        return false;
    }
    return true;
}

bool DOMSource::resolveMethods(ctjs::CreateObjectOp object,
                               llvm::DenseSet<mlir::Operation *> & methods) {
    auto proof = analyzeLocalCallableObject(object, [&] { return step(); });
    if (!proof) { return refuse(llvm::toString(proof.takeError())); }
    methods.insert(proof->calls.begin(), proof->calls.end());
    for (auto [read, closure] : proof->reads) {
        read.getResult().replaceAllUsesWith(closure.getResult());
        read.erase();
    }
    for (ctjs::SetPropertyOp store : proof->stores) {
        if (!step()) { return false; }
        store.erase();
    }
    return true;
}

bool DOMSource::flattenEntryFactory(ctjs::FuncOp wrapper, ctjs::FuncOp target) {
    ctjs::StoreGlobalOp publication;
    for (auto store : wrapper.getBody().front().getOps<ctjs::StoreGlobalOp>()) {
        if (!step()) { return false; }
        if (store.getName() != target.getSymName().rsplit('$').first) { continue; }
        if (publication) { return refuse("DOM entry factory has repeated publication"); }
        publication = store;
    }
    auto * call = publication ? publication.getValue().getDefiningOp() : nullptr;
    auto selection = llvm::dyn_cast_or_null<ctjs::GetPropertyOp>(call);
    if (selection) { call = selection.getObject().getDefiningOp(); }
    auto indirect = llvm::dyn_cast_or_null<ctjs::CallOp>(call);
    auto direct = llvm::dyn_cast_or_null<ctjs::CallDirectOp>(call);
    if (!indirect && !direct) { return true; }
    auto callee = indirect ? indirect.getCallee() : direct.getCalleeValue();
    auto receiver = indirect ? indirect.getReceiver() : direct.getReceiver();
    auto arguments = indirect ? indirect.getArgs() : direct.getArgs();
    auto closure = callee.getDefiningOp<ctjs::CreateClosureOp>();
    auto factory = closure && closure.getFunction() >= 0
                       ? functions.lookup(static_cast<unsigned>(closure.getFunction()))
                       : ctjs::FuncOp{};
    if (!factory || factory == target || factory == wrapper || !closure.getUpvalues().empty() ||
        factory.getUpvalueCount() != 0 ||
        creations.lookup(static_cast<unsigned>(closure.getFunction())) != 1 ||
        !undefined(receiver) ||
        (direct &&
         (direct.getCallee() != factory.getSymName() || !undefined(direct.getNewTarget()))) ||
        arguments.size() + ctjs::implicit_arguments !=
            factory.getBody().front().getNumArguments()) {
        return refuse("DOM entry factory requires one exact uncaptured local call");
    }
    if (!checkBody(wrapper, false) || !checkBody(factory, false)) { return false; }
    if (closure->getBlock() != &wrapper.getBody().front() ||
        call->getBlock() != closure->getBlock() || !closure->isBeforeInBlock(call)) {
        return refuse("DOM entry factory lacks local source order");
    }
    llvm::SmallVector<ctjs::RootOp> roots;
    for (mlir::OpOperand & use : closure.getResult().getUses()) {
        if (!step()) { return false; }
        if (auto root = llvm::dyn_cast<ctjs::RootOp>(use.getOwner())) {
            roots.push_back(root);
        } else if (use.getOwner() != call || use.getOperandNumber() != (indirect ? 0U : 2U)) {
            return refuse("DOM entry factory identity escapes or is called repeatedly");
        }
    }
    for (mlir::Operation * use : call->getResult(0).getUsers()) {
        if (!step()) { return false; }
        if (use != (selection ? selection.getOperation() : publication.getOperation()) &&
            !llvm::isa<ctjs::RootOp>(use)) {
            return refuse("DOM entry factory result escapes its unique publication");
        }
    }
    if (selection) {
        for (mlir::Operation * use : selection.getResult().getUsers()) {
            if (!step()) { return false; }
            if (use != publication && !llvm::isa<ctjs::RootOp>(use)) {
                return refuse("DOM entry factory selection escapes its unique publication");
            }
        }
    }
    auto & body = factory.getBody().front();
    auto result = llvm::cast<ctjs::ReturnOp>(body.back());
    auto entry = result.getValue().getDefiningOp<ctjs::CreateClosureOp>();
    auto table = result.getValue().getDefiningOp<ctjs::CreateObjectOp>();
    if (selection ? !table
                  : !entry || entry.getFunction() < 0 ||
                        static_cast<unsigned>(entry.getFunction()) != *functionIndex(target)) {
        return refuse("DOM entry factory must return its exact source entry");
    }
    // The single invocation moves its local cells with the returned closure.
    // Only unobserved creator operands change; full capture/source proof runs
    // on the flattened candidate before any native code can be published.
    mlir::IRMapping mapping;
    mapping.map(body.getArgument(ctjs::arg_receiver), receiver);
    mapping.map(body.getArgument(ctjs::arg_new_target), receiver);
    mapping.map(body.getArgument(ctjs::arg_callee),
                wrapper.getBody().front().getArgument(ctjs::arg_callee));
    for (auto [formal, actual] :
         llvm::zip(body.getArguments().drop_front(ctjs::implicit_arguments), arguments)) {
        if (!step()) { return false; }
        mapping.map(formal, actual);
    }
    mlir::OpBuilder at(call);
    for (mlir::Operation & operation : body) {
        if (!step()) { return false; }
        if (llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp, ctjs::ReturnOp>(
                operation)) {
            continue;
        }
        at.clone(operation, mapping);
        ++operationCount;
    }
    call->getResult(0).replaceAllUsesWith(mapping.lookup(result.getValue()));
    call->erase();
    if (selection) {
        auto object = mapping.lookup(table.getResult()).getDefiningOp<ctjs::CreateObjectOp>();
        llvm::DenseSet<mlir::Operation *> methods;
        if (!resolveMethods(object, methods)) { return false; }
        // Selection removes only checked own slots. Every source callable,
        // including unselected slots, still needs its invocation proof.
        while (!object.getResult().use_empty()) {
            if (!step()) { return false; }
            auto root = llvm::dyn_cast<ctjs::RootOp>(*object.getResult().getUsers().begin());
            if (!root) { return refuse("DOM entry factory table has an unexpanded use"); }
            root.erase();
        }
        object.erase();
    }
    for (ctjs::RootOp root : roots) { root.erase(); }
    closure.erase();
    functions.erase(*functionIndex(factory));
    factory.erase();
    return true;
}

bool DOMSource::initializeEntry(ctjs::FuncOp wrapper, ctjs::FuncOp target) {
    if (!flattenEntryFactory(wrapper, target)) { return false; }
    auto & block = wrapper.getBody().front();
    const auto index = functionIndex(target);
    if (!index || *index == 0 || creations.lookup(*index) != 1 ||
        block.getNumArguments() != ctjs::implicit_arguments) {
        return refuse("DOM entry initialization lacks a unique source declaration");
    }
    for (mlir::BlockArgument argument : block.getArguments()) {
        for (mlir::OpOperand & use : argument.getUses()) {
            if (!step()) { return false; }
            if (!llvm::isa<ctjs::RootOp, ctjs::CreateClosureOp>(use.getOwner())) {
                return refuse("DOM entry initialization observes an implicit argument");
            }
        }
    }
    ctjs::StoreGlobalOp publication;
    ctjs::CreateClosureOp closure;
    ctjs::ReturnOp result;
    for (mlir::Operation & operation : block) {
        if (!step()) { return false; }
        if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(operation)) {
            auto made = store.getValue().getDefiningOp<ctjs::CreateClosureOp>();
            if (publication || !made || made->getBlock() != &block ||
                !made->isBeforeInBlock(store) || made.getFunction() < 0 ||
                static_cast<unsigned>(made.getFunction()) != *index ||
                store.getName() == "undefined" ||
                store.getName() != target.getSymName().rsplit('$').first) {
                return refuse("DOM entry initialization has an unknown or repeated export");
            }
            publication = store;
            closure = made;
        } else if (auto returned = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
            if (result || !undefined(returned.getValue())) {
                return refuse("DOM entry initialization has an observable return");
            }
            result = returned;
        } else if (!llvm::isa<ctjs::ConstantOp, ctjs::CreateCellOp, ctjs::CellGetOp,
                              ctjs::CellSetOp, ctjs::CreateClosureOp, ctjs::CreateObjectOp,
                              ctjs::GetPropertyOp, ctjs::SetPropertyOp, ctjs::FrameEnterOp,
                              ctjs::FrameExitOp, ctjs::RootOp>(operation)) {
            return refuse("DOM entry initialization contains an observable source operation");
        }
    }
    if (!publication || !result) {
        return refuse("DOM entry initialization lacks a complete source export");
    }
    // Host calls follow the whole initialization, including assignments after
    // publication. Replay is safe only if expansion eliminates every private
    // cell/holder/callable and the complete DOM proof accepts the result.
    for (mlir::BlockArgument argument :
         target.getBody().front().getArguments().drop_front(ctjs::implicit_arguments)) {
        if (!step()) { return false; }
        block.addArgument(argument.getType(), argument.getLoc());
    }
    wrapper.setFunctionTypeAttr(target.getFunctionTypeAttr());
    wrapper->removeAttr("arg_attrs");
    mlir::OpBuilder at(result);
    auto call = ctjs::CallOp::create(at, result.getLoc(), result.getValue().getType(),
                                     closure.getResult(), result.getValue(),
                                     block.getArguments().drop_front(ctjs::implicit_arguments));
    ++operationCount;
    result.getValueMutable().assign(call.getResult());
    publication.erase();
    return true;
}

} // namespace ctcompile::ctnative::dom_source_detail
