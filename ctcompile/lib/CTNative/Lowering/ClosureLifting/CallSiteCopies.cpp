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
//  - it is a direct call, or a method call `o.m(...)` that the method census
//    resolves to one function that does not read its own closure, as
//    Bootstrap's `this._queueCallback(cb, ...)` reaches `_queueCallback`.
//    Every object the receiver may name must be known: a closed object the
//    caller made, or the caller's own receiver when the caller is a method
//    stored only into the fields the census proved. Such a call becomes a
//    direct call of its copy with an undefined callee value. A copy that
//    reads `this` keeps the call's receiver, carried as a lifted method's is
//    (kNativeReceiver), so it must use `this` as a method may (whyThisLeaks);
//    the census then treats that operand as a method call's receiver
//    (carriesReceiverToCopy). The census runs on a clone of the module
//    (methodSitesForCopies), because the main one comes after this rewrite;
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
// parameters no caller proves. A method every call of which now calls a copy
// (copiedAwayMethods) lifts with no call and its field lowers to nothing
// (whyNotLiftableMethod), so its original goes the same way.
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

/// The calls a closure passed to `first` at parameter `at` is handed on
/// through, each with the position it passes it at, when the last function
/// uses it other than only calling it and the copies pay (see the file
/// comment); nullopt otherwise.
std::optional<llvm::SmallVector<std::pair<ctjs::CallDirectOp, unsigned>>> closureLifter::copyChain(
    ctjs::FuncOp first, unsigned at) {
    llvm::SmallVector<std::pair<ctjs::CallDirectOp, unsigned>> hops;
    ctjs::FuncOp callee = first;
    unsigned position = at;
    while (hops.size() < kMaxChain) {
        if (!copyable(callee) || position >= callee.getBody().front().getNumArguments()) {
            return std::nullopt;
        }
        mlir::BlockArgument parameter = callee.getBody().front().getArgument(position);
        auto [next, nextPosition] = handedOn(callee, parameter);
        if (next) {
            hops.push_back({next, nextPosition});
            callee = next.getTarget();
            position = nextPosition;
            continue;
        }
        // The copies pay only if the closure they make can be lifted in the
        // last one. A cell that is written or captured would hold it, which no
        // closure rule lowers, and a call it is passed to or a branch that
        // merges it would refuse it there, so the copies would only move the
        // refusal, at the cost of every function they copy. A cell that only
        // holds the parameter is forwarded to its reads instead.
        const bool handed = !hops.empty() && !parameter.use_empty() &&
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
        if (!handed) { return std::nullopt; }
        return hops;
    }
    return std::nullopt;
}

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

/// Whether every object the receiver of `call` may name is known, so that
/// resolveMethod's answer holds for every run of the call: a closed object
/// the method census rooted (`behind` names only itself), or the caller's
/// own receiver when the caller is a method whose closure goes only into the
/// method fields the census proved (the first half of whyNotLiftableMethod),
/// so no call the census did not see can pass another. Asked after
/// methodCensus.
/// Whether `use` is the receiver of a direct call copyCallSitesForClosures
/// made of a copy that keeps its method call's receiver, and that copy uses
/// `this` only as a method's receiver may: the census then treats the
/// operand as the receiver of a resolved method call.
bool closureLifter::carriesReceiverToCopy(mlir::OpOperand & use) {
    auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
    return call && use.getOperandNumber() == 0 && receiverCopyCalls.contains(call.getOperation()) &&
           call.getTarget() && !whyThisLeaks(call.getTarget());
}

bool closureLifter::knowsEveryReceiver(ctjs::CallOp call) {
    const mlir::Value receiver = call.getReceiver();
    const auto named = behind.lookup(receiver);
    if (named.empty()) { return false; }
    auto argument = llvm::dyn_cast<mlir::BlockArgument>(receiver);
    if (!argument) { return named.size() == 1 && named.front() == receiver; }
    auto caller = call->getParentOfType<ctjs::FuncOp>();
    if (argument.getArgNumber() != ctjs::arg_receiver || !caller ||
        argument.getOwner() != &caller.getBody().front()) {
        return false;
    }
    ctjs::CreateClosureOp closure = uniqueClosureByTarget.lookup(caller.getOperation());
    return closure && llvm::all_of(closure.getResult().getUses(), [&](mlir::OpOperand & use) {
               if (use.getOwner()->hasAttr(kNativeClassSetup)) { return true; }
               auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
               if (!set || use.getOperandNumber() != 2) { return false; }
               const auto fields = methodsOf.find(set.getObject());
               return fields != methodsOf.end() &&
                      fields->second.lookup(ctjs::constantKey(set.getKey())).closure == closure;
           });
}

/// The function each `o.m(...)` call that passes a closure made for it
/// reaches, when that is one function reading neither its receiver nor its
/// own closure, so the call is a direct call of it whatever receiver and
/// callee value it is given; it must take every argument the call passes.
/// The method census decides it on a clone, because it marks the module
/// and reads it before the rewrites that still come before the main census;
/// calls correspond by walk order.
llvm::DenseMap<mlir::Operation *, ctjs::FuncOp> closureLifter::methodSitesForCopies() {
    const auto passesAClosure = [](ctjs::CallOp call) {
        return call.getCallee().getDefiningOp<ctjs::GetPropertyOp>() &&
               llvm::any_of(call.getArgs(), [&](mlir::Value argument) {
                   auto made = argument.getDefiningOp<ctjs::CreateClosureOp>();
                   return made && made.getResult().hasOneUse() &&
                          made->getBlock() == call->getBlock();
               });
    };
    // Whether `target` can stand for the call: it reads no callee value,
    // takes every argument, and some closure the call passes would be copied
    // down a chain from it.
    const auto copiesFrom = [&](ctjs::CallOp call, ctjs::FuncOp target) {
        if (!target || target.getBody().empty() || needsCallee(target)) { return false; }
        mlir::Block & entry = target.getBody().front();
        if (call.getArgs().size() > entry.getNumArguments() - ctjs::implicit_arguments) {
            return false;
        }
        return llvm::any_of(llvm::enumerate(call.getArgs()), [&](auto argument) {
            auto made = argument.value().template getDefiningOp<ctjs::CreateClosureOp>();
            return made && made.getResult().hasOneUse() && made->getBlock() == call->getBlock() &&
                   copyChain(target,
                             ctjs::implicit_arguments + static_cast<unsigned>(argument.index()))
                       .has_value();
        });
    };
    llvm::SmallVector<ctjs::CallOp> calls;
    module.walk([&](ctjs::CallOp call) { calls.push_back(call); });
    llvm::DenseMap<mlir::Operation *, ctjs::FuncOp> sites;
    if (llvm::none_of(calls, passesAClosure)) { return sites; }
    // The census costs about as much as the lift's own, so it runs only when
    // some function a method field holds under the call's key could stand
    // for the call.
    llvm::StringMap<llvm::SmallVector<ctjs::FuncOp, 1>> fieldsByKey;
    module.walk([&](ctjs::SetPropertyOp set) {
        auto made = set.getValue().getDefiningOp<ctjs::CreateClosureOp>();
        const llvm::StringRef key = ctjs::constantKey(set.getKey());
        if (made && !key.empty()) { fieldsByKey[key].push_back(targetOf(made)); }
    });
    if (llvm::none_of(calls, [&](ctjs::CallOp call) {
            if (!passesAClosure(call)) { return false; }
            auto load = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            return llvm::any_of(fieldsByKey.lookup(ctjs::constantKey(load.getKey())),
                                [&](ctjs::FuncOp target) { return copiesFrom(call, target); });
        })) {
        return sites;
    }
    mlir::OwningOpRef<mlir::ModuleOp> clone(module.clone());
    closureLifter census{*clone};
    census.census();
    census.constructorCensus();
    census.argumentCensus();
    census.methodCensus();
    llvm::SmallVector<ctjs::CallOp> twins;
    clone->walk([&](ctjs::CallOp call) { twins.push_back(call); });
    if (twins.size() != calls.size()) { return sites; }
    for (auto [call, twin] : llvm::zip(calls, twins)) {
        if (!passesAClosure(call) || !census.knowsEveryReceiver(twin)) { continue; }
        std::optional<methodCall> resolved = census.resolveMethod(twin);
        auto target = resolved
                          ? module.lookupSymbol<ctjs::FuncOp>(resolved->target.getSymNameAttr())
                          : ctjs::FuncOp{};
        // A target that reads `this` keeps the call's receiver, which it must
        // use only as a method's receiver may (whyThisLeaks).
        if (copiesFrom(call, target) &&
            (target.getBody().front().getArgument(ctjs::arg_receiver).use_empty() ||
             !census.whyThisLeaks(resolved->target))) {
            sites[call.getOperation()] = target;
        }
    }
    return sites;
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

    // Each closure made in a call's block and passed to it, and nothing else:
    // a direct call, or a method call the census resolves (a ctjs.call's
    // arguments start at operand 2). The call and the argument's position are
    // read when the closure's turn comes, since copying for an earlier
    // argument of the same call drops that argument, and turns a method call
    // into a direct one.
    const llvm::DenseMap<mlir::Operation *, ctjs::FuncOp> methods = methodSitesForCopies();
    llvm::SmallVector<ctjs::CreateClosureOp> sites;
    module.walk([&](mlir::Operation * call) {
        const bool direct = llvm::isa<ctjs::CallDirectOp>(call);
        if (!direct && !methods.contains(call)) { return; }
        for (mlir::OpOperand & argument : call->getOpOperands()) {
            if (argument.getOperandNumber() < (direct ? ctjs::implicit_arguments : 2)) { continue; }
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
        mlir::Operation * call = argument.getOwner();
        // A resolved method call reaches `first` as a direct call would, its
        // argument i being the parameter implicit_arguments + i.
        auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(call);
        const ctjs::FuncOp first = direct ? direct.getTarget() : methods.lookup(call);
        const unsigned at = direct ? argument.getOperandNumber()
                                   : argument.getOperandNumber() - 2 + ctjs::implicit_arguments;
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
        const auto chain = copyChain(first, at);
        if (!chain) { continue; }
        const auto & hops = *chain;
        // Every link must be able to pass its copy a callee value. Only the
        // first, in the caller that is not copied, may clone a closure; inside
        // a copy, a closure the original made names a copy of its own. A
        // resolved method reads no callee value (methodSitesForCopies).
        const auto passesCallee = [&](ctjs::CallDirectOp link, bool firstLink) {
            auto own = link.getCalleeValue().getDefiningOp<ctjs::CreateClosureOp>();
            const std::optional<ctjs::CreateClosureOp> value =
                calleeFor(link, own ? targetOf(own) : ctjs::FuncOp{});
            return value && (firstLink || !*value);
        };
        if ((direct && !passesCallee(direct, true)) ||
            !llvm::all_of(hops, [&](const auto & hop) { return passesCallee(hop.first, false); })) {
            continue;
        }
        // The chain as the copy loop walks it: the first link, then the hops.
        llvm::SmallVector<unsigned> positions{at};
        for (const auto & hop : hops) { positions.push_back(hop.second); }

        // The captures' values at the call, passed down the copies.
        mlir::OpBuilder before(call);
        llvm::SmallVector<mlir::Value> values;
        for (mlir::Value capture : made.getUpvalues()) {
            values.push_back(
                ctjs::CellGetOp::create(before, call->getLoc(), valueType, capture).getResult());
        }
        ctjs::CallDirectOp link = direct;
        llvm::SmallVector<mlir::Value> passed = values;
        mlir::BlockArgument last;
        // Each call of the chain, now naming a copy, with the copy it names.
        llvm::SmallVector<std::pair<ctjs::CallDirectOp, ctjs::FuncOp>> retargeted;
        for (auto [index, position] : llvm::enumerate(positions)) {
            ctjs::FuncOp original = link ? link.getTarget() : first;
            originals.insert(original);
            ctjs::FuncOp copy = cloneDeep(original);
            if (!link) {
                // The method call becomes a direct call of the copy. The copy
                // reads no callee value, so that is undefined, and the load of
                // the method goes with the call. A copy that reads `this` keeps
                // the call's receiver, carried as a lifted method's is.
                auto method = llvm::cast<ctjs::CallOp>(call);
                mlir::OpBuilder here(method);
                const mlir::Value undefined = undefinedAt(here, method.getLoc());
                const bool keepsReceiver =
                    !copy.getBody().front().getArgument(ctjs::arg_receiver).use_empty();
                link = directCall(
                    here, method.getLoc(), copy, keepsReceiver ? method.getReceiver() : undefined,
                    undefined, undefined, {}, method.getArgs(),
                    copy.getBody().front().getNumArguments() - ctjs::implicit_arguments);
                if (keepsReceiver) {
                    link->setAttr(kNativeReceiver, mlir::UnitAttr::get(context));
                    copy->setAttr(kNativeReceiver, mlir::UnitAttr::get(context));
                    receiverCopyCalls.insert(link.getOperation());
                }
                method.getResult().replaceAllUsesWith(link.getResult());
                mlir::Operation * load = method.getCallee().getDefiningOp();
                method.erase();
                if (load && load->use_empty()) { load->erase(); }
                copiedAwayMethods.insert(original.getOperation());
            }
            auto own = link.getCalleeValue().getDefiningOp<ctjs::CreateClosureOp>();
            const std::optional<ctjs::CreateClosureOp> callee =
                calleeFor(link, own ? targetOf(own) : ctjs::FuncOp{});
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
            if (index + 1 < positions.size()) { link = handedOn(copy, last).first; }
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
        // The parameter the closure came in by is dead in every copy now:
        // drop it, and its argument at each call, from the last copy back to
        // the call that made the closure.
        for (size_t i = positions.size(); i-- > 0;) {
            auto [linkCall, copied] = retargeted[i];
            const unsigned position = positions[i];
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
    for (bool grew = true; grew;) {
        grew = false;
        for (mlir::Operation * op : originals) {
            auto fn = llvm::cast<ctjs::FuncOp>(op);
            if (dead.contains(op)) { continue; }
            const auto uses = mlir::SymbolTable::getSymbolUses(fn, &module.getBodyRegion());
            const bool called =
                !uses || llvm::any_of(*uses, [&](const mlir::SymbolTable::SymbolUse & use) {
                    auto owner = use.getUser()->getParentOfType<ctjs::FuncOp>();
                    return !owner || !dead.contains(owner.getOperation());
                });
            if (called || llvm::any_of(madeBy.lookup(op), [&](ctjs::CreateClosureOp c) {
                    return !c.getResult().use_empty() || supplies(c);
                })) {
                continue;
            }
            dead.insert(op);
            grew = true;
        }
    }
    for (mlir::Operation * op : dead) {
        for (ctjs::CreateClosureOp c : madeBy.lookup(op)) {
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
    // A dead original keeps the importer's public visibility otherwise, and
    // MLIR's DeadCodeAnalysis gives a public function unknown callers: its
    // parameters read !ctnative.boxed, and every call it makes passes that on,
    // so a live function it shares with the copies is refused for a caller
    // that never runs. Private, it has no callers and is dead to the type
    // solve too, as lift() makes its targets private for the same reason; the
    // sweep after admission removes it.
    for (mlir::Operation * op : dead) {
        mlir::SymbolTable::setSymbolVisibility(llvm::cast<ctjs::FuncOp>(op),
                                               mlir::SymbolTable::Visibility::Private);
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
