//===- ClosureLifting/Singletons.cpp - Methods of singleton objects -------===//
//
// A bundle's module factory runs once and often builds a table of functions
// as an object literal held in a module-level binding:
//
//     const P = { on(t, e, i, n) { D(t, e, i, n, false); }, off(...) {...} };
//
// Other functions then call `P.on(...)`. Once the factory's bindings are
// static variables (RunOnce.cpp), such a binding is one variable written once,
// and when nothing but method calls ever reads it, every `P.on(...)` reaches
// the one function the literal stored under `on`. This file turns those calls
// into direct calls, so the method's callers can prove its parameters.
//
// The binding qualifies when:
//  1. It is a static variable whose only write stores an object literal made
//     in the run-once function that owns it.
//  2. The literal's only uses are that write and its own constant-key field
//     definitions, each key defined once: no later write can change a field.
//  3. Every read of the binding is used only as the receiver of a call through
//     a constant-key field of it, and each field called that way holds a
//     closure with no captures whose function uses neither `this`, new.target
//     nor its own closure.
//  4. Every read runs after the write: its function is started only after
//     the write, in the owner's body - called through a closure or by symbol,
//     or reachable by anyone a closure of it was handed to. A read that could
//     run first would find `undefined`, and a direct call would hide the error.
//
//===----------------------------------------------------------------------===//

#include "ClosureLifter.h"

namespace ctcompile::ctnative::lowering_detail {
namespace {

/// The number of JavaScript parameters `target`'s entry block takes.
size_t parametersOf(ctjs::FuncOp target) {
    return target.getBody().front().getNumArguments() - ctjs::implicit_arguments;
}

/// The operation in `block` that contains `op`, or null.
mlir::Operation * enclosingIn(mlir::Operation * op, mlir::Block * block) {
    while (op && op->getBlock() != block) { op = op->getParentOp(); }
    return op;
}

} // namespace

/// What can start each function: each call of a closure of it, each call
/// through a static variable it is stored in when that variable is only
/// ever called, each call through a global written once with it when the
/// global is only ever called, the closure's creation when it is used any
/// other way
/// (whoever receives it may call it from then on), and each call of the
/// function by symbol. Fills `starters` and `closuresOf`; a rewrite that
/// erases calls rebuilds them.
void closureLifter::indexStarters() {
    starters.clear();
    closuresOf.clear();
    const auto onlyCallee = [](mlir::OpOperand & use) {
        auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
        auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
        return (call && use.getOperandNumber() == 0 && call.getReceiver() != use.get() &&
                !llvm::is_contained(call.getArgs(), use.get())) ||
               (direct && use.get() == direct.getCalleeValue() &&
                direct.getReceiver() != use.get() &&
                !llvm::is_contained(direct.getArgs(), use.get()));
    };
    llvm::DenseMap<mlir::Attribute, std::optional<llvm::SmallVector<mlir::Operation *>>> through;
    module.walk([&](ctjs::CellGetOp read) {
        auto cell = read.getCell().getDefiningOp<ctjs::CreateCellOp>();
        auto symbol = cell ? cell->getAttr(kNativeStaticCell) : mlir::Attribute{};
        if (!symbol) { return; }
        auto & calls =
            through.try_emplace(symbol, llvm::SmallVector<mlir::Operation *>{}).first->second;
        for (mlir::OpOperand & use : read.getResult().getUses()) {
            if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
            if (calls && onlyCallee(use)) {
                calls->push_back(use.getOwner());
            } else {
                calls.reset();
            }
        }
    });
    // The same for a global a function declaration is stored in: when the
    // global is written once and every read of it is only called, those
    // calls are what can start the function.
    llvm::StringMap<std::optional<llvm::SmallVector<mlir::Operation *>>> globalCalls;
    llvm::StringMap<unsigned> globalWrites;
    module.walk([&](mlir::Operation * op) {
        if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(op)) {
            ++globalWrites[store.getName()];
            return;
        }
        auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op);
        if (!load) { return; }
        auto & calls =
            globalCalls.try_emplace(load.getName(), llvm::SmallVector<mlir::Operation *>{})
                .first->second;
        for (mlir::OpOperand & use : load.getResult().getUses()) {
            if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
            if (calls && onlyCallee(use)) {
                calls->push_back(use.getOwner());
            } else {
                calls.reset();
            }
        }
    });
    module.walk([&](ctjs::CreateClosureOp made) {
        ctjs::FuncOp target = targetOf(made);
        if (!target) { return; }
        ++closuresOf[target.getOperation()];
        auto & into = starters[target.getOperation()];
        for (mlir::OpOperand & use : made.getResult().getUses()) {
            mlir::Operation * user = use.getOwner();
            if (llvm::isa<ctjs::RootOp>(user)) { continue; }
            if (onlyCallee(use)) {
                into.push_back(user);
                continue;
            }
            if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(user);
                store && globalWrites.lookup(store.getName()) == 1) {
                // ponytail: an embedder that calls the global by name does so
                // after the script's top level returns, so it is not listed;
                // a top level that throws first would need it listed.
                const auto calls = globalCalls.find(store.getName());
                if (calls != globalCalls.end() && calls->second) {
                    llvm::append_range(into, *calls->second);
                    continue;
                }
            }
            mlir::Operation * box = llvm::isa<ctjs::CreateCellOp>(user) ? user : nullptr;
            if (auto set = llvm::dyn_cast<ctjs::CellSetOp>(user);
                set && use.getOperandNumber() == 1) {
                box = set.getCell().getDefiningOp();
            }
            const auto calls = box && box->getAttr(kNativeStaticCell)
                                   ? through.find(box->getAttr(kNativeStaticCell))
                                   : through.end();
            if (calls != through.end() && calls->second) {
                llvm::append_range(into, *calls->second);
            } else {
                into.push_back(made.getOperation());
            }
        }
    });
    if (auto uses = mlir::SymbolTable::getSymbolUses(&module.getBodyRegion())) {
        for (const mlir::SymbolTable::SymbolUse & use : *uses) {
            if (auto * named = mlir::SymbolTable::lookupNearestSymbolFrom(module.getOperation(),
                                                                          use.getSymbolRef())) {
                starters[named].push_back(use.getUser());
            }
        }
    }
}

/// Whether `op` can run only after `write`, which sits in the body of the
/// function that owns it. Climbs from `op` to that body through what starts
/// each function on the way (`starters`); a function `startedBy` lists is
/// started only by the operations it lists there, which are climbed instead.
bool closureLifter::runsAfter(
    mlir::Operation * op, mlir::Operation * write,
    const llvm::DenseMap<mlir::Operation *, llvm::SmallVector<mlir::Operation *>> & startedBy) {
    auto function = write->getParentOfType<ctjs::FuncOp>();
    mlir::Block * body = write->getBlock();
    llvm::SmallVector<mlir::Operation *> pending{op};
    llvm::DenseSet<mlir::Operation *> seen;
    while (!pending.empty()) {
        mlir::Operation * at = pending.pop_back_val();
        if (!seen.insert(at).second) { continue; }
        if (seen.size() > 256) { return false; }
        auto holder = at->getParentOfType<ctjs::FuncOp>();
        if (!holder) { return false; }
        if (holder == function) {
            mlir::Operation * level = enclosingIn(at, body);
            if (!level || level == write || !write->isBeforeInBlock(level)) { return false; }
            continue;
        }
        if (const auto field = startedBy.find(holder.getOperation()); field != startedBy.end()) {
            pending.append(field->second.begin(), field->second.end());
            continue;
        }
        // Nothing known starts it: the script entry, or a function the host
        // may call.
        const auto found = starters.find(holder.getOperation());
        if (found == starters.end()) { return false; }
        pending.append(found->second.begin(), found->second.end());
    }
    return true;
}

bool closureLifter::bindSingletonMethods(liftReport & out) {
    llvm::MapVector<mlir::StringAttr, llvm::SmallVector<ctjs::CreateCellOp>> bySymbol;
    module.walk([&](ctjs::CreateCellOp cell) {
        if (auto symbol = cell->getAttrOfType<mlir::StringAttr>(kNativeStaticCell)) {
            bySymbol[symbol].push_back(cell);
        }
    });
    if (bySymbol.empty()) { return false; }

    indexStarters();

    bool changed = false;
    for (auto & [symbol, cells] : bySymbol) {
        // Condition 1: one owning box, one write of a literal made beside it.
        ctjs::CreateCellOp owner;
        for (ctjs::CreateCellOp cell : cells) {
            if (cell->hasAttr(kNativeStaticReference)) { continue; }
            if (owner) {
                owner = {};
                break;
            }
            owner = cell;
        }
        if (!owner) { continue; }
        auto function = owner->getParentOfType<ctjs::FuncOp>();
        mlir::Operation * write = nullptr;
        mlir::Value stored;
        bool usable = true;
        llvm::SmallVector<ctjs::CellGetOp> reads;
        for (ctjs::CreateCellOp cell : cells) {
            for (mlir::OpOperand & use : cell.getResult().getUses()) {
                switch (classify(use)) {
                case cellUse::read:
                    reads.push_back(llvm::cast<ctjs::CellGetOp>(use.getOwner()));
                    break;
                case cellUse::write:
                    usable = usable && !write;
                    write = use.getOwner();
                    stored = llvm::cast<ctjs::CellSetOp>(write).getValue();
                    break;
                default: usable = usable && llvm::isa<ctjs::RootOp>(use.getOwner()); break;
                }
            }
        }
        if (!usable || !write || write->getParentOfType<ctjs::FuncOp>() != function ||
            !ctjs::constantAttr<ctjs::UndefinedAttr>(owner.getInitial())) {
            continue;
        }
        auto object = stored.getDefiningOp<ctjs::CreateObjectOp>();
        if (!object || object->getParentOfType<ctjs::FuncOp>() != function) { continue; }
        // From here on the binding holds a literal; say why it is not a
        // method table when it is not.
        const auto refuse = [&](const llvm::Twine & why) {
            owner->setAttr(kNativeSingletonReason, mlir::StringAttr::get(context, why));
        };

        // Condition 2: the literal's fields are defined once each and never
        // written again.
        llvm::StringMap<mlir::Value> fields;
        for (mlir::OpOperand & use : object.getResult().getUses()) {
            if (use.getOwner() == write || llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
            auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
            const llvm::StringRef key = set ? ctjs::constantKey(set.getKey()) : llvm::StringRef{};
            if (!set || use.get() != set.getObject() || !set->hasAttr(ctjs::defineOwnMarker) ||
                key.empty() || !fields.try_emplace(key, set.getValue()).second) {
                refuse("the literal is used by `" + use.getOwner()->getName().getStringRef() +
                       "` other than to define a constant-key field once");
                usable = false;
                break;
            }
        }
        if (!usable) { continue; }

        // Condition 3: every read is the receiver of calls through fields
        // that hold a capture-free closure of a function needing no receiver.
        struct methodCall {
            ctjs::CallOp call;
            ctjs::GetPropertyOp load;
            ctjs::CreateClosureOp closure;
            ctjs::FuncOp target;
        };
        llvm::SmallVector<methodCall> calls;
        llvm::SmallPtrSet<mlir::Operation *, 8> receiving;
        for (ctjs::CellGetOp read : reads) {
            for (mlir::OpOperand & use : read.getResult().getUses()) {
                mlir::Operation * user = use.getOwner();
                if (llvm::isa<ctjs::RootOp>(user)) { continue; }
                if (llvm::isa<ctjs::CallOp>(user) && use.getOperandNumber() == 1) {
                    // The receiver of a call; checked below to be one of the
                    // method calls found through a load.
                    receiving.insert(user);
                    continue;
                }
                auto load = llvm::dyn_cast<ctjs::GetPropertyOp>(user);
                const llvm::StringRef key =
                    load ? ctjs::constantKey(load.getKey()) : llvm::StringRef{};
                const auto field = load ? fields.find(key) : fields.end();
                auto closure = field != fields.end()
                                   ? field->second.getDefiningOp<ctjs::CreateClosureOp>()
                                   : ctjs::CreateClosureOp{};
                ctjs::FuncOp target = closure ? targetOf(closure) : ctjs::FuncOp{};
                const auto where =
                    (" in `" + read->getParentOfType<ctjs::FuncOp>().getSymName() + "`").str();
                if (!load || load.getObject() != read.getResult()) {
                    refuse("a read reaches `" + user->getName().getStringRef() + "`" + where);
                } else if (!target) {
                    refuse("a read loads `" + key + "`, which is not a function field" + where);
                } else if (!closure.getUpvalues().empty()) {
                    refuse("the method `" + key + "` still captures a binding");
                } else if (target.getBody().empty() ||
                           llvm::any_of(target.getBody().front().getArguments().take_front(
                                            ctjs::implicit_arguments),
                                        [](mlir::BlockArgument argument) {
                                            return !argument.use_empty();
                                        })) {
                    refuse("the method `" + key +
                           "` reads its receiver, new.target or its own closure");
                }
                if (owner->hasAttr(kNativeSingletonReason)) {
                    usable = false;
                    break;
                }
                for (mlir::OpOperand & loaded : load.getResult().getUses()) {
                    auto call = llvm::dyn_cast<ctjs::CallOp>(loaded.getOwner());
                    if (llvm::isa<ctjs::RootOp>(loaded.getOwner())) { continue; }
                    if (!call || loaded.getOperandNumber() != 0 ||
                        call.getReceiver() != read.getResult()) {
                        refuse("the method `" + key + "` is read as a value" + where);
                        usable = false;
                        break;
                    }
                    if (call.getArgs().size() > parametersOf(target)) {
                        refuse("a call passes `" + key + "` more arguments than it takes" + where);
                        usable = false;
                        break;
                    }
                    calls.push_back({call, load, closure, target});
                }
                if (!usable) { break; }
            }
            if (!usable) { break; }
        }
        if (!usable) { continue; }
        if (!llvm::all_of(receiving, [&](mlir::Operation * call) {
                return llvm::any_of(calls, [&](const methodCall & m) { return m.call == call; });
            })) {
            refuse("a read is the receiver of a call through something else");
            continue;
        }
        if (calls.empty()) { continue; }

        // Condition 4: each read runs after the write. Climb from the read to
        // the owner's body through the operations that start each function
        // on the way: its closures and its calls by symbol.
        // A method of this literal that nothing else holds starts only at
        // the calls through it, which are themselves checked here.
        llvm::DenseMap<mlir::Operation *, llvm::SmallVector<mlir::Operation *>> fieldCalls;
        for (methodCall & m : calls) {
            if (llvm::all_of(m.closure->getUsers(), [&](mlir::Operation * user) {
                    return llvm::isa<ctjs::RootOp>(user) ||
                           (llvm::isa<ctjs::SetPropertyOp>(user) &&
                            llvm::cast<ctjs::SetPropertyOp>(user).getObject() == stored);
                })) {
                fieldCalls[m.target.getOperation()].push_back(m.call);
            }
        }
        if (const auto early = llvm::find_if(
                reads, [&](ctjs::CellGetOp read) { return !runsAfter(read, write, fieldCalls); });
            early != reads.end()) {
            refuse("a read in `" +
                   early->getOperation()->getParentOfType<ctjs::FuncOp>().getSymName() +
                   "` can run before the literal is written");
            continue;
        }

        // The rewrite. No target reads its receiver, so each call passes
        // `undefined` for it; then nothing reads the binding any more, and the
        // literal, its write and the closures only it held are erased.
        for (methodCall & m : calls) {
            mlir::OpBuilder at(m.call);
            const mlir::Value undefined = undefinedAt(at, m.call.getLoc());
            auto direct = directCall(at, m.call.getLoc(), m.target, undefined, undefined, undefined,
                                     {}, m.call.getArgs(), parametersOf(m.target));
            m.call.getResult().replaceAllUsesWith(direct.getResult());
            m.call.erase();
            ++out.calls;
            ++out.bound;
        }
        const auto eraseWithRoots = [](mlir::Operation * op) {
            for (mlir::Operation * user : llvm::make_early_inc_range(op->getUsers())) {
                if (llvm::isa<ctjs::RootOp>(user)) { user->erase(); }
            }
            if (!op->use_empty()) { return false; }
            op->erase();
            return true;
        };
        llvm::SetVector<mlir::Operation *> loads;
        for (methodCall & m : calls) { loads.insert(m.load); }
        for (mlir::Operation * load : loads) { eraseWithRoots(load); }
        for (ctjs::CellGetOp read : reads) { eraseWithRoots(read); }
        write->erase();
        llvm::SmallVector<mlir::Value> values;
        for (mlir::Operation * user : llvm::make_early_inc_range(object->getUsers())) {
            if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
                values.push_back(set.getValue());
                set.erase();
            }
        }
        eraseWithRoots(object);
        for (mlir::Value value : values) {
            auto closure = value.getDefiningOp<ctjs::CreateClosureOp>();
            ctjs::FuncOp target = closure ? targetOf(closure) : ctjs::FuncOp{};
            if (target && eraseWithRoots(closure) && --closuresOf[target.getOperation()] == 0) {
                // Every caller is now a visible direct call. As in lift(),
                // the function must be private, or type inference gives its
                // parameters unknown callers.
                mlir::SymbolTable::setSymbolVisibility(target,
                                                       mlir::SymbolTable::Visibility::Private);
            }
        }
        changed = true;
        indexStarters();
    }
    return changed;
}

} // namespace ctcompile::ctnative::lowering_detail
