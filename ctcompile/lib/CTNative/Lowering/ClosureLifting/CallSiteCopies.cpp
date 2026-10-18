//===- ClosureLifting/CallSiteCopies.cpp - Copies per call site -----------===//
//
// An event registry takes a handler and hands it on before storing it:
//
//   on(element, type, handler) { addHandler(element, type, handler, false); }
//
// Each `on(...)` call passes its own closure, so `handler` is no one closure,
// and the closure, passed as an argument, has no call site to move its
// captures to. copyCallSitesForClosures gives each such call its own copy of
// the chain it goes through (here `on` and `addHandler`), passing the
// closure's captures down in place of the closure, and makes the closure at
// the top of the last copy, where it is used. Each copy then holds one known
// closure, and every other rule judges it there as it would any closure made
// in that function.
//
// A call is copied only when:
//  - the argument is a closure made for that call alone, in the call's own
//    block, with no capture relayed from the enclosing closure and no `this`
//    it reads from where it is made (an arrow's is a capture by now,
//    ArrowThis.cpp);
//  - each capture is a cell this frame made, not a static variable, written
//    only in the block that makes it, before the call, and by no closure, so
//    its value at the call is the value every later read sees (a write in a
//    loop around the call would run again after it); the copy passes that
//    value and boxes it in a new cell of its own;
//  - the parameter is handed on, as the argument of one direct call in the
//    entry block, down to a function that uses it other than only calling it
//    (a callee that only calls it borrows the closure, BorrowedCallbacks.cpp,
//    and one reached directly from the call stores it into a closure set,
//    ClosureSets.cpp). Each copy in the chain then runs exactly once per call,
//    so the last makes exactly one closure, as the call did;
//  - the closure can be lifted where the last copy makes it: that function
//    neither boxes the parameter in a cell that is written or captured (no
//    closure rule lowers a closure held in one) nor passes it to another call
//    or merges it at a branch result (the generic rule refuses it there), so
//    the copies do not merely move a refusal. A cell that only holds the
//    parameter, which the importer makes when a nested function reuses the
//    parameter's name, is forwarded to its reads;
//  - the chain is at most kMaxChain functions long, and each call in it passes
//    a callee value its copy can take: any value, when the function reads no
//    new.target and no capture through its own closure, or else the
//    function's own closure made earlier in the caller of the first link,
//    which is then cloned beside itself for the copy, as ClassStatics.cpp does
//    for class helpers.
// Each copy is a new private function reached only from the call it was made
// for. Every copy is recorded in ctnative.copies with the function it stands
// for, as the class rewrites record theirs (ClassInheritance.h), so admission
// counts by source function. An original left with no caller, made only by
// closures nothing uses, can no longer run: those closures are erased, so the
// sweep after admission removes it (LowerToEmitC.cpp), with a row
// tools/check counts as removed, rather than leaving it refused for
// parameters no caller proves.
//
//===----------------------------------------------------------------------===//

#include "ClassInheritance.h"
#include "ClosureLifter.h"

#include <functional>

namespace ctcompile::ctnative::lowering_detail {

namespace {

/// The most functions one call's chain of copies may hold. Three covers an
/// event registry's `on` -> `addHandler` -> its argument normaliser; a longer
/// chain copies more than it settles and is left alone.
constexpr unsigned kMaxChain = 3;

/// The ctjs.call_direct in `fn`'s entry block that `parameter` is handed to
/// as an argument, when that is its only use, with the argument's position;
/// null otherwise.
std::pair<ctjs::CallDirectOp, unsigned> handedOn(ctjs::FuncOp fn, mlir::BlockArgument parameter) {
    if (!parameter.hasOneUse()) { return {}; }
    mlir::OpOperand & use = *parameter.use_begin();
    auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
    if (!call || use.getOperandNumber() < ctjs::implicit_arguments ||
        call->getBlock() != &fn.getBody().front()) {
        return {};
    }
    return {call, use.getOperandNumber()};
}

/// Whether `box` only ever holds the value it was made with: no static
/// variable, and nothing writes it, captures it or reads it but a
/// ctjs.cell_get, so every read is that value.
bool holdsOnlyItsValue(ctjs::CreateCellOp box) {
    return !box->hasAttr(kNativeStaticCell) &&
           llvm::all_of(box.getResult().getUses(), [](mlir::OpOperand & use) {
               return llvm::isa<ctjs::CellGetOp>(use.getOwner());
           });
}

/// Whether a closure in place of `parameter` would be passed to another call
/// or merged with other values at a branch result, directly or through a
/// cell that only holds it. The generic rule refuses it there ("it is passed
/// as an argument", "it reaches scf.yield"), so copies that make it there
/// gain nothing.
bool passedOnOrMerged(mlir::Value parameter) {
    llvm::SmallVector<mlir::Value> values{parameter};
    for (mlir::Operation * user : parameter.getUsers()) {
        if (auto box = llvm::dyn_cast<ctjs::CreateCellOp>(user); box && holdsOnlyItsValue(box)) {
            for (mlir::Operation * read : box->getUsers()) { values.push_back(read->getResult(0)); }
        }
    }
    return llvm::any_of(values, [](mlir::Value value) {
        return llvm::any_of(value.getUses(), [](mlir::OpOperand & use) {
            mlir::Operation * user = use.getOwner();
            if (llvm::isa<ctjs::CallOp>(user)) { return use.getOperandNumber() != 0; }
            return llvm::isa<ctjs::CallDirectOp, ctjs::ConstructOp, mlir::scf::YieldOp>(user);
        });
    });
}

/// Whether `function` has a body to copy that cannot see the parameters a
/// copy appends: it makes no `arguments` object and gathers no rest array.
bool copyable(ctjs::FuncOp function) {
    if (!function || function.getBody().empty()) { return false; }
    return !function
                .walk([](mlir::Operation * op) {
                    return llvm::isa<ctjs::MakeArgumentsOp, ctjs::GatherRestOp>(op)
                               ? mlir::WalkResult::interrupt()
                               : mlir::WalkResult::advance();
                })
                .wasInterrupted();
}

/// Whether `fn` reads new.target, or its own closure for anything but making
/// closures that relay none of its captures (ClassStatics.cpp's rule).
bool needsCallee(ctjs::FuncOp fn) {
    mlir::Block & entry = fn.getBody().front();
    if (!entry.getArgument(ctjs::arg_new_target).use_empty()) { return true; }
    return llvm::any_of(entry.getArgument(ctjs::arg_callee).getUses(), [](mlir::OpOperand & use) {
        auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(use.getOwner());
        if (!made || use.getOperandNumber() != 0) { return true; }
        for (unsigned i = 0; i < made.getUpvalues().size(); ++i) {
            if (closureLifter::enclosingIndex(made, i) >= 0) { return true; }
        }
        return false;
    });
}

/// Whether `fn` reads no new.target and uses its own closure only to read its
/// captures or to make closures (ClassStatics.cpp's readsOnlyCaptures).
bool readsOnlyCaptures(ctjs::FuncOp fn) {
    mlir::Block & entry = fn.getBody().front();
    return entry.getArgument(ctjs::arg_new_target).use_empty() &&
           llvm::all_of(entry.getArgument(ctjs::arg_callee).getUses(), [](mlir::OpOperand & use) {
               return llvm::isa<ctjs::CreateClosureOp, ctjs::LoadUpvalueOp>(use.getOwner()) &&
                      use.getOperandNumber() == 0;
           });
}

/// Whether `made` runs before `op` in the same function.
bool madeBefore(mlir::Operation * made, mlir::Operation * op) {
    mlir::Operation * level = made->getBlock()->findAncestorOpInBlock(*op);
    return level && made->isBeforeInBlock(level);
}

/// How a copy of `call`'s target gets its callee value: nullopt when it
/// cannot, null when it reads none (undefined is passed), or the target's own
/// closure, made earlier in the caller, to clone for the copy. `ownTarget` is
/// the function that closure names. An undefined callee does not do for a
/// target that reads its captures: that is a call Bindings.cpp made, whose
/// captures the lift adds later.
std::optional<ctjs::CreateClosureOp> calleeFor(ctjs::CallDirectOp call, ctjs::FuncOp ownTarget) {
    ctjs::FuncOp target = call.getTarget();
    if (!needsCallee(target)) { return ctjs::CreateClosureOp{}; }
    auto own = call.getCalleeValue().getDefiningOp<ctjs::CreateClosureOp>();
    if (own && ownTarget == target && readsOnlyCaptures(target) && madeBefore(own, call)) {
        return own;
    }
    return std::nullopt;
}

} // namespace

/// Whether the cell `capture` holds, at `call`, the value every read of it
/// sees afterwards (see the file comment), and is not a static variable.
bool closureLifter::settledAt(mlir::Value capture, mlir::Operation * call) {
    auto cell = capture.getDefiningOp<ctjs::CreateCellOp>();
    if (!cell || cell->hasAttr(kNativeStaticCell) ||
        cell->getParentOfType<ctjs::FuncOp>() != call->getParentOfType<ctjs::FuncOp>()) {
        return false;
    }
    // The call, or the operation holding it, in the block that makes the cell.
    mlir::Operation * level = cell->getBlock()->findAncestorOpInBlock(*call);
    if (!level) { return false; }
    for (mlir::OpOperand & use : cell.getResult().getUses()) {
        mlir::Operation * user = use.getOwner();
        if (llvm::isa<ctjs::CellGetOp>(user)) { continue; }
        if (auto write = llvm::dyn_cast<ctjs::CellSetOp>(user)) {
            // Once per cell, before the call: a write in a nested block (a
            // loop body, a branch) could run after the call.
            if (write.getCell() != cell.getResult() || write->getBlock() != cell->getBlock() ||
                !write->isBeforeInBlock(level)) {
                return false;
            }
            continue;
        }
        auto other = llvm::dyn_cast<ctjs::CreateClosureOp>(user);
        if (!other || writesUpvalues(targetOf(other), 0)) { return false; }
    }
    return true;
}

/// Whether `fn`, or a function it makes closures of, may write an upvalue
/// (ctjs.store_upvalue); true when that is not known within a few levels.
/// The census's mutatesUpvalue set is not built yet where this runs.
bool closureLifter::writesUpvalues(ctjs::FuncOp fn, unsigned depth) {
    if (!fn || depth > 8) { return true; }
    bool writes = false;
    fn.walk([&](mlir::Operation * op) {
        if (llvm::isa<ctjs::StoreUpvalueOp>(op)) { writes = true; }
        if (auto nested = llvm::dyn_cast<ctjs::CreateClosureOp>(op)) {
            writes |= writesUpvalues(targetOf(nested), depth + 1);
        }
    });
    return writes;
}

/// Gives each call that passes a closure down a chain of functions its own
/// copy of that chain, and makes the closure in the last copy (see the file
/// comment). Returns whether anything was copied.
bool closureLifter::copyCallSitesForClosures() {
    unsigned nextIndex = 0;
    module.walk([&](ctjs::FuncOp fn) {
        if (const std::optional<unsigned> index = ctjs::functionIndex(fn)) {
            nextIndex = std::max(nextIndex, *index + 1);
        }
    });
    // The source each copy stands for, so a copy of a copy names the source
    // too.
    llvm::StringMap<mlir::StringAttr> sourceOf;
    if (auto earlier = module->getAttrOfType<mlir::DictionaryAttr>(kNativeCopies)) {
        for (const mlir::NamedAttribute & entry : earlier) {
            if (auto source = llvm::dyn_cast<mlir::StringAttr>(entry.getValue())) {
                sourceOf[entry.getName().getValue()] = source;
            }
        }
    }
    llvm::SmallVector<std::pair<mlir::StringAttr, mlir::StringAttr>> copies;
    const auto valueType = ctjs::ValueType::get(context);
    // A private copy of `fn` with fresh indices for it and every function it
    // makes closures of, as ClassStatics.cpp's helper copies are made. Each
    // such function is copied once, and the copy's direct calls of it name
    // its copy, so a call and the closure value it passes as its callee still
    // agree.
    std::function<ctjs::FuncOp(ctjs::FuncOp)> cloneDeep = [&](ctjs::FuncOp fn) {
        auto copy = llvm::cast<ctjs::FuncOp>(fn->clone());
        const unsigned index = nextIndex++;
        const llvm::StringRef name = fn.getSymName();
        copy.setSymName((name.substr(0, name.rfind('$')) + "$" + llvm::Twine(index)).str());
        mlir::SymbolTable::setSymbolVisibility(copy, mlir::SymbolTable::Visibility::Private);
        module.getBody()->getOperations().insertAfter(fn->getIterator(), copy);
        byIndex[index] = copy;
        if (passesNewTarget.contains(fn.getOperation())) {
            passesNewTarget.insert(copy.getOperation());
        }
        const mlir::StringAttr known = sourceOf.lookup(fn.getSymName());
        const mlir::StringAttr source = known ? known : fn.getSymNameAttr();
        sourceOf[copy.getSymName()] = source;
        copies.emplace_back(copy.getSymNameAttr(), source);
        llvm::DenseMap<mlir::Operation *, ctjs::FuncOp> nested;
        copy.walk([&](ctjs::CreateClosureOp c) {
            if (ctjs::FuncOp target = targetOf(c)) {
                auto [found, fresh] = nested.try_emplace(target.getOperation());
                if (fresh) { found->second = cloneDeep(target); }
                c.setFunction(*ctjs::functionIndex(found->second));
            }
        });
        copy.walk([&](ctjs::CallDirectOp call) {
            ctjs::FuncOp target = call.getTarget();
            if (ctjs::FuncOp named =
                    target ? nested.lookup(target.getOperation()) : ctjs::FuncOp{}) {
                call.setCalleeAttr(mlir::FlatSymbolRefAttr::get(named.getSymNameAttr()));
            }
        });
        return copy;
    };
    // Appends `extra` parameters to `fn` and returns them.
    const auto appendParameters = [&](ctjs::FuncOp fn, size_t extra) {
        mlir::Block & entry = fn.getBody().front();
        llvm::SmallVector<mlir::Value> added;
        for (size_t i = 0; i < extra; ++i) {
            added.push_back(entry.addArgument(valueType, fn.getLoc()));
        }
        llvm::SmallVector<mlir::Type> inputs(entry.getNumArguments(), valueType);
        fn.setFunctionTypeAttr(
            mlir::TypeAttr::get(mlir::FunctionType::get(context, inputs, {valueType})));
        return added;
    };

    // Each closure made in a direct call's block and passed to it, and nothing
    // else. The call and the argument's position are read when the closure's
    // turn comes, since copying for an earlier argument of the same call
    // drops that argument.
    llvm::SmallVector<ctjs::CreateClosureOp> sites;
    module.walk([&](ctjs::CallDirectOp call) {
        for (mlir::OpOperand & argument : call->getOpOperands()) {
            if (argument.getOperandNumber() < ctjs::implicit_arguments) { continue; }
            auto made = argument.get().getDefiningOp<ctjs::CreateClosureOp>();
            if (made && made.getResult().hasOneUse() && made->getBlock() == call->getBlock()) {
                sites.push_back(made);
            }
        }
    });
    // The functions a chain was copied from, for the dead-original sweep below.
    llvm::SetVector<mlir::Operation *> originals;
    bool changed = false;
    for (ctjs::CreateClosureOp made : sites) {
        mlir::OpOperand & argument = *made.getResult().use_begin();
        auto call = llvm::cast<ctjs::CallDirectOp>(argument.getOwner());
        const unsigned at = argument.getOperandNumber();
        ctjs::FuncOp closureTarget = targetOf(made);
        // An arrow's `this` is a capture by now (ArrowThis.cpp); one left as
        // an operand is never read, since its target ignores its receiver.
        if (!copyable(closureTarget) ||
            (!ctjs::isUndefinedConstant(made.getEnclosingThis()) &&
             !closureTarget.getBody().front().getArgument(ctjs::arg_receiver).use_empty())) {
            continue;
        }
        if (auto indices = made.getEnclosingIndicesAttr();
            indices && llvm::any_of(indices.asArrayRef(), [](int32_t k) { return k >= 0; })) {
            continue;
        }
        if (!llvm::all_of(made.getUpvalues(),
                          [&](mlir::Value capture) { return settledAt(capture, call); })) {
            continue;
        }
        // The chain: each function hands the parameter on to the next, and
        // the last uses it other than only calling it.
        llvm::SmallVector<std::pair<ctjs::CallDirectOp, unsigned>> chain{{call, at}};
        bool handed = false;
        while (chain.size() <= kMaxChain) {
            auto [link, position] = chain.back();
            ctjs::FuncOp callee = link.getTarget();
            if (!copyable(callee) || position >= callee.getBody().front().getNumArguments()) {
                break;
            }
            mlir::BlockArgument parameter = callee.getBody().front().getArgument(position);
            auto [next, nextPosition] = handedOn(callee, parameter);
            if (next) {
                chain.push_back({next, nextPosition});
                continue;
            }
            // The copies pay only if the closure they make can be lifted in
            // the last one. A cell that is written or captured would hold it,
            // which no closure rule lowers, and a call it is passed to or a
            // branch that merges it would refuse it there, so the copies
            // would only move the refusal, at the cost of every function they
            // copy. A cell that only holds the parameter is forwarded to its
            // reads instead.
            handed = chain.size() > 1 && !parameter.use_empty() &&
                     !llvm::all_of(parameter.getUses(),
                                   [](mlir::OpOperand & use) {
                                       return llvm::isa<ctjs::CallOp>(use.getOwner()) &&
                                              use.getOperandNumber() == 0;
                                   }) &&
                     llvm::none_of(parameter.getUsers(),
                                   [](mlir::Operation * user) {
                                       auto box = llvm::dyn_cast<ctjs::CreateCellOp>(user);
                                       return box && !holdsOnlyItsValue(box);
                                   }) &&
                     !passedOnOrMerged(parameter);
            // Or every closure that calls it makes it there (ExecuteCalls.cpp).
            handed = handed || (chain.size() > 1 && relaysCallback(parameter));
            break;
        }
        if (!handed || chain.size() > kMaxChain) { continue; }
        // Every link must be able to pass its copy a callee value. Only the
        // first, in the caller that is not copied, may clone a closure; inside
        // a copy, a closure the original made names a copy of its own.
        if (!llvm::all_of(llvm::enumerate(chain), [&](auto step) {
                ctjs::CallDirectOp link = step.value().first;
                auto own = link.getCalleeValue().template getDefiningOp<ctjs::CreateClosureOp>();
                const std::optional<ctjs::CreateClosureOp> callee =
                    calleeFor(link, own ? targetOf(own) : ctjs::FuncOp{});
                return callee && (step.index() == 0 || !*callee);
            })) {
            continue;
        }

        // The captures' values at the call, passed down the copies.
        mlir::OpBuilder before(call);
        llvm::SmallVector<mlir::Value> values;
        for (mlir::Value capture : made.getUpvalues()) {
            values.push_back(
                ctjs::CellGetOp::create(before, call.getLoc(), valueType, capture).getResult());
        }
        ctjs::CallDirectOp link = call;
        llvm::SmallVector<mlir::Value> passed = values;
        mlir::BlockArgument last;
        // Each call of the chain, now naming a copy, with the copy it names.
        llvm::SmallVector<std::pair<ctjs::CallDirectOp, ctjs::FuncOp>> retargeted;
        for (auto [index, step] : llvm::enumerate(chain)) {
            const unsigned position = step.second;
            ctjs::FuncOp original = link.getTarget();
            originals.insert(original);
            auto own = link.getCalleeValue().getDefiningOp<ctjs::CreateClosureOp>();
            const std::optional<ctjs::CreateClosureOp> callee =
                calleeFor(link, own ? targetOf(own) : ctjs::FuncOp{});
            ctjs::FuncOp copy = cloneDeep(original);
            link.setCalleeAttr(mlir::FlatSymbolRefAttr::get(copy.getSymNameAttr()));
            // The call is no longer the original's, so the lift must not add
            // the original's captures to it (Bindings.cpp records the calls it
            // makes of a sibling).
            if (auto bound = boundCaptureCalls.find(original.getOperation());
                bound != boundCaptureCalls.end()) {
                llvm::erase_if(bound->second,
                               [&](const boundCaptureCall & site) { return site.call == link; });
            }
            if (callee && *callee) {
                // The copy reads its captures through its own closure: a clone
                // of the original's, made beside it with the same captures.
                mlir::OpBuilder beside((*callee)->getBlock(), std::next((*callee)->getIterator()));
                auto clone = llvm::cast<ctjs::CreateClosureOp>(beside.clone(**callee));
                clone.setFunction(*ctjs::functionIndex(copy));
                link.getCalleeValueMutable().assign(clone.getResult());
            } else if (!ctjs::isUndefinedConstant(link.getCalleeValue())) {
                // The callee value is never read. The global the resolver
                // named this call's function by is a declaration it proved,
                // so its now unused read goes too.
                mlir::Operation * read = link.getCalleeValue().getDefiningOp();
                mlir::OpBuilder at(link);
                link.getCalleeValueMutable().assign(undefinedAt(at, link.getLoc()));
                if (llvm::isa_and_nonnull<ctjs::LoadGlobalOp>(read) && read->use_empty()) {
                    read->erase();
                }
            }
            link.getArgsMutable().append(passed);
            retargeted.push_back({link, copy});
            passed = appendParameters(copy, values.size());
            last = copy.getBody().front().getArgument(position);
            if (index + 1 < chain.size()) { link = handedOn(copy, last).first; }
        }
        // The closure, made in the last copy from cells of its own.
        auto copy = llvm::cast<ctjs::FuncOp>(last.getOwner()->getParentOp());
        mlir::Block & entry = copy.getBody().front();
        mlir::OpBuilder in(&entry, entry.begin());
        for (auto frame : entry.getOps<ctjs::FrameEnterOp>()) {
            in.setInsertionPointAfter(frame);
            break;
        }
        llvm::SmallVector<mlir::Value> cells;
        for (auto [capture, value] : llvm::zip(made.getUpvalues(), passed)) {
            auto cell = ctjs::CreateCellOp::create(in, copy.getLoc(), valueType, value);
            cell->setAttrs(capture.getDefiningOp()->getAttrDictionary());
            cells.push_back(cell.getResult());
        }
        // It relays nothing from the copy's own closure and reads no `this`
        // from it, so neither operand names the copy's implicit arguments,
        // which a function only ever called directly leaves unused.
        mlir::Value undefined = undefinedAt(in, copy.getLoc());
        auto inside = llvm::cast<ctjs::CreateClosureOp>(in.clone(*made));
        inside.getEnclosingClosureMutable().assign(undefined);
        inside.getEnclosingThisMutable().assign(undefined);
        inside.getUpvaluesMutable().assign(cells);
        inside.removeEnclosingIndicesAttr();
        last.replaceAllUsesWith(inside.getResult());
        // A cell that only holds the parameter is the parameter: its reads
        // take the closure itself.
        for (mlir::Operation * user : llvm::make_early_inc_range(inside->getUsers())) {
            auto box = llvm::dyn_cast<ctjs::CreateCellOp>(user);
            if (!box || !holdsOnlyItsValue(box)) { continue; }
            for (mlir::Operation * read : llvm::make_early_inc_range(box->getUsers())) {
                read->getResult(0).replaceAllUsesWith(inside.getResult());
                read->erase();
            }
            box->erase();
        }
        // A closure capturing a box of it, or execute's call of it, calls it
        // directly (ExecuteCalls.cpp).
        for (mlir::Operation * helper : relayCallback(inside, cells, cloneDeep)) {
            originals.insert(helper);
        }
        // The parameter the closure came in by is dead in every copy now:
        // drop it, and its argument at each call, from the last copy back to
        // the call that made the closure.
        for (size_t i = chain.size(); i-- > 0;) {
            auto [linkCall, copied] = retargeted[i];
            const unsigned position = chain[i].second;
            mlir::Block & body = copied.getBody().front();
            body.eraseArgument(position);
            llvm::SmallVector<mlir::Type> inputs(body.getNumArguments(), valueType);
            copied.setFunctionTypeAttr(
                mlir::TypeAttr::get(mlir::FunctionType::get(context, inputs, {valueType})));
            linkCall.getArgsMutable().erase(position - ctjs::implicit_arguments);
        }
        made->erase();
        changed = true;
    }
    if (!changed) { return false; }

    // The originals the copies left with no caller (see the file comment). One
    // whose only calls sit in such an original is dead too.
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::CreateClosureOp>> madeBy;
    module.walk([&](ctjs::CreateClosureOp c) {
        if (ctjs::FuncOp target = targetOf(c)) { madeBy[target].push_back(c); }
    });
    // A closure that a sibling's direct call takes captures from
    // (Bindings.cpp) stays while that sibling has captures.
    const auto supplies = [&](ctjs::CreateClosureOp c) {
        return llvm::any_of(boundCaptureCalls, [&](const auto & entry) {
            return llvm::cast<ctjs::FuncOp>(entry.first).getUpvalueCount() != 0 &&
                   llvm::any_of(entry.second,
                                [&](const boundCaptureCall & site) { return site.holder == c; });
        });
    };
    llvm::SetVector<mlir::Operation *> dead;
    // A closure made inside a dead function is never made: the function it
    // names is dead too once nothing else makes or calls it, so its calls
    // keep nothing alive (ExecuteCalls.cpp's relay leaves an execute helper
    // only those calls). Such a closure is left in its dead function.
    const auto inDead = [&](mlir::Operation * op) {
        auto owner = op->getParentOfType<ctjs::FuncOp>();
        return owner && dead.contains(owner.getOperation());
    };
    llvm::SetVector<mlir::Operation *> candidates(originals.begin(), originals.end());
    for (bool grew = true; grew;) {
        grew = false;
        for (size_t i = 0; i < candidates.size(); ++i) {
            mlir::Operation * op = candidates[i];
            auto fn = llvm::cast<ctjs::FuncOp>(op);
            if (dead.contains(op)) { continue; }
            const auto uses = mlir::SymbolTable::getSymbolUses(fn, &module.getBodyRegion());
            const bool called =
                !uses || llvm::any_of(*uses, [&](const mlir::SymbolTable::SymbolUse & use) {
                    auto owner = use.getUser()->getParentOfType<ctjs::FuncOp>();
                    return !owner || !dead.contains(owner.getOperation());
                });
            if (called || llvm::any_of(madeBy.lookup(op), [&](ctjs::CreateClosureOp c) {
                    return !inDead(c) && (!c.getResult().use_empty() || supplies(c));
                })) {
                continue;
            }
            dead.insert(op);
            grew = true;
            fn.walk([&](ctjs::CreateClosureOp c) {
                if (ctjs::FuncOp target = targetOf(c)) { candidates.insert(target); }
            });
        }
    }
    for (mlir::Operation * op : dead) {
        for (ctjs::CreateClosureOp c : madeBy.lookup(op)) {
            if (inDead(c)) { continue; }
            bindingClosures.erase(c.getOperation());
            for (auto & entry : boundCaptureCalls) {
                llvm::erase_if(entry.second,
                               [&](const boundCaptureCall & site) { return site.holder == c; });
            }
            c->erase();
        }
        boundCaptureCalls.erase(op);
        siblingBindingTargets.erase(op);
    }

    llvm::SmallVector<mlir::NamedAttribute> all;
    if (auto earlier = module->getAttrOfType<mlir::DictionaryAttr>(kNativeCopies)) {
        all.append(earlier.begin(), earlier.end());
    }
    for (auto & [copy, source] : copies) { all.emplace_back(copy, source); }
    module->setAttr(kNativeCopies, mlir::DictionaryAttr::get(context, all));
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
