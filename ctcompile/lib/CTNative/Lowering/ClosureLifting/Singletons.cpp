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
//     closure with no captures whose function reads no new.target, uses its
//     own closure only to create nested closures, and uses `this` only as a
//     read is used (`this.find(...)` in another method of the table), which
//     needs the literal to be the only holder of that method.
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

/// Whether `target`, held in `closure`, can be called with `undefined` for
/// its implicit arguments once its calls through `literal` are direct: it
/// reads no new.target, uses its own closure only to create nested closures,
/// and uses its receiver only when `closure` is held by `literal` alone, so
/// that every call of it has the literal as its receiver. Condition 3 checks
/// what the receiver is used for.
bool readsOnlyItsTable(ctjs::FuncOp target, ctjs::CreateClosureOp closure, mlir::Value literal) {
    if (target.getBody().empty()) { return false; }
    mlir::Block & entry = target.getBody().front();
    if (!entry.getArgument(ctjs::arg_new_target).use_empty()) { return false; }
    for (mlir::OpOperand & use : entry.getArgument(ctjs::arg_callee).getUses()) {
        if (!llvm::isa<ctjs::CreateClosureOp>(use.getOwner()) || use.getOperandNumber() != 0) {
            return false;
        }
    }
    return entry.getArgument(ctjs::arg_receiver).use_empty() ||
           llvm::all_of(closure->getUsers(), [&](mlir::Operation * user) {
               auto field = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
               return llvm::isa<ctjs::RootOp>(user) || (field && field.getObject() == literal);
           });
}

} // namespace

/// What can start each function: each call of a closure of it, each call
/// through a static variable it is stored in when that variable is only
/// ever called, the closure's creation when it is used any other way
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
        // that hold a capture-free closure. The called function reads no
        // new.target, uses its own closure only to create nested closures (it
        // has no captures for them to find through it), and uses its receiver
        // only as a read is used: as the receiver of calls through the
        // literal's fields (`this.find(...)`), or as the lexical `this` of an
        // arrow that never reads it. Such a receiver is the literal
        // only when every call of the method goes through it, so the method's
        // closure must be held by the literal alone; its receiver is then
        // checked like a read, and its calls become direct too.
        struct methodCall {
            ctjs::CallOp call;
            ctjs::GetPropertyOp load;
            ctjs::CreateClosureOp closure;
            ctjs::FuncOp target;
        };
        llvm::SmallVector<methodCall> calls;
        llvm::SmallPtrSet<mlir::Operation *, 8> receiving;
        // Each value naming the literal: the reads, then the receivers of
        // methods that use theirs, with the key of that method.
        llvm::SmallVector<std::pair<mlir::Value, llvm::StringRef>> tables;
        for (ctjs::CellGetOp read : reads) { tables.push_back({read.getResult(), {}}); }
        llvm::DenseSet<mlir::Operation *> checked;
        for (std::size_t next = 0; next < tables.size() && usable; ++next) {
            auto [table, method] = tables[next];
            for (mlir::OpOperand & use : table.getUses()) {
                mlir::Operation * user = use.getOwner();
                if (llvm::isa<ctjs::RootOp>(user)) { continue; }
                if (llvm::isa<ctjs::CallOp>(user) && use.getOperandNumber() == 1) {
                    // The receiver of a call; checked below to be one of the
                    // method calls found through a load.
                    receiving.insert(user);
                    continue;
                }
                // Every arrow a method creates captures the method's receiver
                // as its lexical `this`; one that never reads it does not
                // read the literal.
                if (auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(user);
                    made && !method.empty() && use.getOperandNumber() == 1) {
                    ctjs::FuncOp inner = targetOf(made);
                    if (inner && !inner.getBody().empty() &&
                        inner.getBody().front().getArgument(ctjs::arg_receiver).use_empty()) {
                        continue;
                    }
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
                    (" in `" +
                     table.getParentRegion()->getParentOfType<ctjs::FuncOp>().getSymName() + "`")
                        .str();
                // A method's receiver used other than to call the literal's
                // methods is read some other way.
                if (!method.empty() && (!load || load.getObject() != table || !target)) {
                    refuse("the method `" + method +
                           "` reads its receiver, new.target or its own closure");
                } else if (!load || load.getObject() != table) {
                    refuse("a read reaches `" + user->getName().getStringRef() + "`" + where);
                } else if (!target) {
                    refuse("a read loads `" + key + "`, which is not a function field" + where);
                } else if (!closure.getUpvalues().empty()) {
                    refuse("the method `" + key + "` still captures a binding");
                } else if (!readsOnlyItsTable(target, closure, stored)) {
                    refuse("the method `" + key +
                           "` reads its receiver, new.target or its own closure");
                }
                if (owner->hasAttr(kNativeSingletonReason)) {
                    usable = false;
                    break;
                }
                if (checked.insert(target.getOperation()).second) {
                    const mlir::Value self =
                        target.getBody().front().getArgument(ctjs::arg_receiver);
                    if (!self.use_empty()) { tables.push_back({self, key}); }
                }
                for (mlir::OpOperand & loaded : load.getResult().getUses()) {
                    auto call = llvm::dyn_cast<ctjs::CallOp>(loaded.getOwner());
                    if (llvm::isa<ctjs::RootOp>(loaded.getOwner())) { continue; }
                    if (!call || loaded.getOperandNumber() != 0 || call.getReceiver() != table) {
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

        // The rewrite. A target's receiver is only ever the receiver of calls
        // rewritten here, so each call passes `undefined` for it, and for the
        // callee, which only creates nested closures that capture nothing
        // through it; then nothing reads the binding any more, and the
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
