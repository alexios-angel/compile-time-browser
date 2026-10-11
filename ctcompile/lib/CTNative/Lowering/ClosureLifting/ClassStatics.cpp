//===- ClosureLifting/ClassStatics.cpp - Static members of classes --------===//
//
// A class's static methods and static getters are functions stored on the
// constructor (Classes.cpp shows the importer's spelling). When the class
// itself is never used as a value, every read of a static member names one
// function, so each read becomes a direct call and the definitions lower to
// nothing:
//
//   K.s(a, b)               ctjs.call_direct @s(undefined, ..., a, b)
//   K.g                     ctjs.call_direct @g(undefined, ...)
//   this.s(a) inside a      the same: a static member's `this` is K, because
//     static member of K    nothing but these calls can reach it
//   this.constructor.g      the same, inside a method or the constructor of
//     inside an instance    K: an instance of K reaches K through its
//     method of K           prototype's `constructor`
//
// The values that name the class are the constructor's closure, the binding
// it is stored in (and the static variables it became, RunOnce.cpp), the
// captures of that binding, a static member's `this`, and `this.constructor`
// in a method. The rewrite needs, for every one of them:
//  1. every use is a static-member read this file rewrites, the receiver of a
//     call through one, class setup, a `new` (not of `this` or
//     `this.constructor`, which the rewrite leaves without a value), a write
//     of the binding or a capture of it; anything else could reach a static
//     member this file does not see;
//  2. each static member's closure is used only by its definition and its
//     `__home` edge; a static member that a read outside the static members
//     reaches reads neither new.target nor its own closure (its `this` is
//     checked as a name of the class, by rule 1); and none is named `__home`,
//     which the engine reads as the home object;
//  3. every read runs after the class is defined (runsAfter, as for the
//     singleton tables): before that, the static member does not exist yet;
//  4. no class extends this one and it extends none, so `this` in a static
//     member and `this.constructor` in a method name exactly this class;
//  5. no object writes a `constructor` property of its own.
// A class that fails records why in `staticRefusals`, and classCensus()
// refuses it with that reason.
//
//===----------------------------------------------------------------------===//

#include "ClosureLifter.h"

#include "llvm/ADT/SetVector.h"

namespace ctcompile::ctnative::lowering_detail {
namespace {

/// The number of JavaScript parameters `target`'s entry block takes.
size_t parametersOf(ctjs::FuncOp target) {
    return target.getBody().front().getNumArguments() - ctjs::implicit_arguments;
}

/// Erases `op` and then each constant or global load that only it used.
void eraseWithFeeders(mlir::Operation * op) {
    for (mlir::Operation * user : llvm::make_early_inc_range(op->getUsers())) {
        if (llvm::isa<ctjs::RootOp>(user)) { user->erase(); }
    }
    llvm::SetVector<mlir::Operation *> feeders;
    for (mlir::Value operand : op->getOperands()) {
        if (mlir::Operation * made = operand.getDefiningOp();
            llvm::isa_and_nonnull<ctjs::ConstantOp, ctjs::LoadGlobalOp>(made)) {
            feeders.insert(made);
        }
    }
    op->erase();
    for (mlir::Operation * made : feeders) {
        if (made->use_empty()) { made->erase(); }
    }
}

/// The function that `op` sits in, by name, for a refusal.
std::string where(mlir::Operation * op) {
    auto fn = op->getParentOfType<ctjs::FuncOp>();
    return fn ? (" in `" + fn.getSymName() + "`").str() : std::string{};
}

} // namespace

bool closureLifter::bindClassStatics(liftReport & out) {
    staticRefusals.clear();
    classCensus();
    if (jsClasses.empty()) { return false; }

    // Rule 5, for the whole module.
    bool ownConstructor = false;
    module.walk([&](ctjs::SetPropertyOp set) {
        if (ctjs::constantKey(set.getKey()) == "constructor" && !isHomeEdge(set) &&
            !set.getObject().getDefiningOp<ctjs::CreateObjectOp>()) {
            ownConstructor = true;
        }
    });
    llvm::DenseSet<mlir::Operation *> parents;
    for (auto & [key, cls] : jsClasses) {
        if (cls.parent) { parents.insert(cls.parent.getOperation()); }
    }
    indexStarters();

    bool changed = false;
    for (auto & [key, cls] : jsClasses) {
        if (cls.statics.empty() && cls.staticGetters.empty()) { continue; }
        const auto refuse = [&](const llvm::Twine & why) {
            staticRefusals.try_emplace(key, why.str());
        };
        if (cls.parent || parents.contains(key)) {
            refuse(cls.parent ? "it extends another class, so `this` in a static member is "
                                "not one class; inherited statics are the next slice"
                              : "another class extends it, so `this` in a static member is "
                                "not one class; inherited statics are the next slice");
            continue;
        }
        if (ownConstructor) {
            refuse("an object writes its own `constructor`, so `this.constructor` may not "
                   "name the class");
            continue;
        }
        ctjs::CallOp defined;
        for (mlir::Operation * op : cls.setup) {
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(op)) { defined = call; }
        }
        if (!defined) { continue; }

        // Each static member by name, with its closure and function.
        struct member {
            ctjs::CreateClosureOp closure;
            ctjs::FuncOp target;
            bool getter;
        };
        llvm::StringMap<member> statics;
        for (const auto & entry : cls.statics) {
            statics[entry.getKey()] = {entry.getValue(), targetOf(entry.getValue()), false};
        }
        for (const auto & entry : cls.staticGetters) {
            statics[entry.getKey()] = {entry.getValue(), targetOf(entry.getValue()), true};
        }
        // Rule 2.
        for (auto & entry : statics) {
            member m = entry.getValue();
            // The VM reads its own home-object edge for `K.__home`, whatever
            // the class defines under that name.
            if (entry.getKey() == "__home") {
                refuse("its static member `__home` is the engine's home-object key");
                break;
            }
            if (!m.target || m.target.getBody().empty()) {
                refuse("its static member `" + entry.getKey() + "` has no body");
                break;
            }
            for (mlir::Operation * user : m.closure->getUsers()) {
                if (!llvm::isa<ctjs::RootOp>(user) && !cls.definitions.contains(user) &&
                    !isHomeEdge(user)) {
                    refuse("its static member `" + entry.getKey() + "` is also used by `" +
                           user->getName().getStringRef() + "`" + where(user));
                    break;
                }
            }
        }
        if (staticRefusals.contains(key)) { continue; }

        // Rule 1: collect every value that names the class, and check each
        // use of each one.
        llvm::SetVector<mlir::Value> names;
        // The names that are a static member's `this` or `this.constructor`.
        llvm::DenseSet<mlir::Value> selves;
        llvm::SetVector<mlir::Operation *> boxes;
        llvm::DenseSet<std::pair<mlir::Operation *, unsigned>> slots;
        // The same slots in the order found: a slot before the slots of the
        // closures nested in its target that are filled from it.
        llvm::SmallVector<std::pair<ctjs::CreateClosureOp, unsigned>> slotOrder;
        names.insert(cls.constructor.getResult());
        for (auto & entry : statics) {
            names.insert(entry.getValue().target.getBody().front().getArgument(0));
            selves.insert(entry.getValue().target.getBody().front().getArgument(0));
        }
        // `this.constructor` in a method or the constructor.
        llvm::SmallVector<ctjs::FuncOp> instanceFunctions{targetOf(cls.constructor)};
        for (const auto & entry : cls.methods) {
            instanceFunctions.push_back(targetOf(entry.getValue()));
        }
        for (ctjs::FuncOp fn : instanceFunctions) {
            if (!fn || fn.getBody().empty()) { continue; }
            for (mlir::OpOperand & use : fn.getBody().front().getArgument(0).getUses()) {
                auto load = llvm::dyn_cast<ctjs::GetPropertyOp>(use.getOwner());
                if (load && use.getOperandNumber() == 0 &&
                    ctjs::constantKey(load.getKey()) == "constructor") {
                    names.insert(load.getResult());
                    selves.insert(load.getResult());
                }
            }
        }
        struct staticCall {
            ctjs::CallOp call;
            ctjs::GetPropertyOp load;
            ctjs::FuncOp target;
        };
        llvm::SmallVector<staticCall> calls;
        llvm::SmallVector<std::pair<ctjs::GetPropertyOp, ctjs::FuncOp>> getterReads;
        llvm::SmallPtrSet<mlir::Operation *, 8> receiving;
        // Each `new this` or `new this.constructor`.
        llvm::SmallVector<mlir::Operation *> selfConstructs;
        const std::string className = classNameOf(targetOf(cls.constructor));
        // A capture slot of a closure that holds the class: its reads in the
        // target, and the slots of closures there that are filled from it.
        const auto followSlot = [&](ctjs::CreateClosureOp c, unsigned slot) {
            if (!slots.insert({c.getOperation(), slot}).second) { return; }
            slotOrder.push_back({c, slot});
            ctjs::FuncOp target = targetOf(c);
            if (!target || target.getBody().empty() || closuresOf.lookup(target) != 1) {
                refuse("a closure that captures `" + className +
                       "` names a function made more than once" + where(c));
                return;
            }
            const mlir::Value self = target.getBody().front().getArgument(2);
            target.getBody().walk([&](mlir::Operation * op) {
                if (auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(op);
                    read && read.getClosure() == self && read.getIndex() == slot) {
                    names.insert(read.getResult());
                }
            });
        };
        const auto nestedSlots =
            [&](ctjs::CreateClosureOp c, unsigned slot,
                llvm::SmallVectorImpl<std::pair<ctjs::CreateClosureOp, unsigned>> & into) {
                ctjs::FuncOp target = targetOf(c);
                if (!target || target.getBody().empty()) { return; }
                target.getBody().walk([&](ctjs::CreateClosureOp nested) {
                    for (unsigned j = 0; j < nested.getUpvalues().size(); ++j) {
                        if (enclosingIndex(nested, j) == static_cast<std::int32_t>(slot)) {
                            into.push_back({nested, j});
                        }
                    }
                });
            };
        const auto captured = [&](ctjs::CreateClosureOp c, unsigned slot) {
            llvm::SmallVector<std::pair<ctjs::CreateClosureOp, unsigned>> pending{{c, slot}};
            while (!pending.empty()) {
                auto [made, at] = pending.pop_back_val();
                if (slots.contains({made.getOperation(), at})) { continue; }
                followSlot(made, at);
                nestedSlots(made, at, pending);
            }
        };
        // Every box of a binding that holds the class: the box itself and the
        // static variables of the same symbol.
        const auto followBox = [&](ctjs::CreateCellOp cell) {
            llvm::SmallVector<ctjs::CreateCellOp> group{cell};
            if (auto symbol = cell->getAttr(kNativeStaticCell)) {
                group.clear();
                module.walk([&](ctjs::CreateCellOp other) {
                    if (other->getAttr(kNativeStaticCell) == symbol) { group.push_back(other); }
                });
            }
            for (ctjs::CreateCellOp box : group) {
                if (!boxes.insert(box.getOperation())) { continue; }
                if (!box->hasAttr(kNativeStaticReference) &&
                    !ctjs::constantAttr<ctjs::UndefinedAttr>(box.getInitial())) {
                    refuse("the binding of `" + className + "` starts with another value");
                }
                for (mlir::OpOperand & use : box.getResult().getUses()) {
                    switch (classify(use)) {
                    case cellUse::read:
                        names.insert(llvm::cast<ctjs::CellGetOp>(use.getOwner()).getResult());
                        break;
                    case cellUse::write:
                        if (llvm::cast<ctjs::CellSetOp>(use.getOwner()).getValue() !=
                            cls.constructor.getResult()) {
                            refuse("the binding of `" + className +
                                   "` is also written with another value" + where(use.getOwner()));
                        }
                        break;
                    case cellUse::capture:
                        captured(llvm::cast<ctjs::CreateClosureOp>(use.getOwner()),
                                 use.getOperandNumber() - kFirstCapture);
                        break;
                    default:
                        if (!llvm::isa<ctjs::RootOp>(use.getOwner())) {
                            refuse("the binding of `" + className + "` is used by `" +
                                   use.getOwner()->getName().getStringRef().str() + "`" +
                                   where(use.getOwner()));
                        }
                        break;
                    }
                }
            }
        };
        // A use inside a static member is refused only if that member can
        // run (see `reached` below); until then it waits here.
        llvm::SmallPtrSet<mlir::Operation *, 8> staticTargets, reached;
        for (auto & entry : statics) {
            staticTargets.insert(entry.getValue().target.getOperation());
        }
        const auto holderOf = [](mlir::Operation * op) {
            return op->getParentOfType<ctjs::FuncOp>().getOperation();
        };
        llvm::SmallVector<std::pair<mlir::Operation *, std::string>> deferred;
        const auto refuseAt = [&](mlir::Operation * at, const llvm::Twine & why) {
            if (staticTargets.contains(holderOf(at))) {
                deferred.push_back({holderOf(at), why.str()});
                return false;
            }
            refuse(why);
            return true;
        };
        for (size_t i = 0; i < names.size() && !staticRefusals.contains(key); ++i) {
            const mlir::Value name = names[i];
            for (mlir::OpOperand & use : name.getUses()) {
                mlir::Operation * user = use.getOwner();
                if (llvm::isa<ctjs::RootOp>(user) || cls.setup.contains(user) ||
                    cls.definitions.contains(user) || isHomeEdge(user)) {
                    continue;
                }
                if (llvm::isa<ctjs::ConstructOp>(user)) {
                    if (selves.contains(name)) { selfConstructs.push_back(user); }
                    continue;
                }
                if (auto set = llvm::dyn_cast<ctjs::CellSetOp>(user);
                    set && use.getOperandNumber() == 1) {
                    if (auto cell = set.getCell().getDefiningOp<ctjs::CreateCellOp>()) {
                        followBox(cell);
                        continue;
                    }
                }
                if (auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(user);
                    made && use.getOperandNumber() >= kFirstCapture) {
                    captured(made, use.getOperandNumber() - kFirstCapture);
                    continue;
                }
                if (llvm::isa<ctjs::CallOp>(user) && use.getOperandNumber() == 1) {
                    receiving.insert(user);
                    continue;
                }
                auto load = llvm::dyn_cast<ctjs::GetPropertyOp>(user);
                const llvm::StringRef field = load && use.getOperandNumber() == 0
                                                  ? ctjs::constantKey(load.getKey())
                                                  : llvm::StringRef{};
                const auto found = statics.find(field);
                if (field.empty() || found == statics.end()) {
                    if (refuseAt(user, load && !field.empty()
                                           ? "it reads `" + className + "." + field.str() +
                                                 "`, which is not a static member" + where(user)
                                           : "`" + className + "` is used as a value by `" +
                                                 user->getName().getStringRef().str() + "`" +
                                                 where(user))) {
                        break;
                    }
                    continue;
                }
                member m = found->getValue();
                if (m.getter) {
                    getterReads.push_back({load, m.target});
                    continue;
                }
                for (mlir::OpOperand & loaded : load.getResult().getUses()) {
                    auto call = llvm::dyn_cast<ctjs::CallOp>(loaded.getOwner());
                    if (llvm::isa<ctjs::RootOp>(loaded.getOwner())) { continue; }
                    if (!call || loaded.getOperandNumber() != 0) {
                        if (refuseAt(loaded.getOwner(), "its static method `" + field +
                                                            "` is read as a value" +
                                                            where(loaded.getOwner()))) {
                            break;
                        }
                        continue;
                    }
                    if (call.getArgs().size() > parametersOf(m.target)) {
                        if (refuseAt(call, "a call passes its static method `" + field +
                                               "` more arguments than it takes" + where(call))) {
                            break;
                        }
                        continue;
                    }
                    calls.push_back({call, load, m.target});
                }
            }
        }
        if (staticRefusals.contains(key)) { continue; }
        // A call whose receiver names the class must be a static call whose
        // receiver also names it.
        for (mlir::Operation * call : receiving) {
            const bool known =
                llvm::any_of(calls, [&](staticCall s) { return s.call.getOperation() == call; });
            if (!known && refuseAt(call, "`" + className +
                                             "` is the receiver of a call through something else" +
                                             where(call))) {
                break;
            }
        }
        for (staticCall s : calls) {
            if (!names.contains(s.call.getReceiver()) &&
                refuseAt(s.call,
                         "its static method is called with another receiver" + where(s.call))) {
                break;
            }
        }
        // After the rewrite a static member's `this` is undefined and an
        // instance has no `constructor` field, so neither may be constructed
        // where it can run: in an instance method, or in a static member that
        // a read outside the static members reaches.
        for (bool grew = true; grew;) {
            grew = false;
            const auto reach = [&](mlir::Operation * read, ctjs::FuncOp target) {
                mlir::Operation * from = holderOf(read);
                if ((!staticTargets.contains(from) || reached.contains(from)) &&
                    reached.insert(target.getOperation()).second) {
                    grew = true;
                }
            };
            for (staticCall & s : calls) { reach(s.call, s.target); }
            for (auto & [load, target] : getterReads) { reach(load, target); }
        }
        for (auto & [holder, why] : deferred) {
            if (reached.contains(holder)) {
                refuse(why);
                break;
            }
        }
        // A direct call passes no closure and no new.target, so a static
        // member that can run must read neither: its captures would be gone.
        // One nothing reads stays uncalled, whatever its body reads.
        for (auto & entry : statics) {
            ctjs::FuncOp target = entry.getValue().target;
            mlir::Block & body = target.getBody().front();
            if (reached.contains(target.getOperation()) &&
                (!body.getArgument(1).use_empty() || !body.getArgument(2).use_empty())) {
                refuse("its static member `" + entry.getKey() +
                       "` reads new.target or its own closure");
                break;
            }
        }
        for (mlir::Operation * made : selfConstructs) {
            if (!staticTargets.contains(holderOf(made)) || reached.contains(holderOf(made))) {
                refuse("it constructs `new this` or `new this.constructor`" + where(made));
                break;
            }
        }
        if (staticRefusals.contains(key)) { continue; }

        // Rule 3. A member of the class starts only through the class or an
        // instance of it, which exist once the class is defined.
        // ponytail: trusts `new K` to come after the definition; a `new` of a
        // binding read before it would need the same climb as a static read.
        llvm::DenseMap<mlir::Operation *, llvm::SmallVector<mlir::Operation *>> startedBy;
        for (ctjs::FuncOp fn : instanceFunctions) {
            if (fn) { startedBy[fn.getOperation()]; }
        }
        for (auto & entry : statics) { startedBy[entry.getValue().target.getOperation()]; }
        for (const auto & entry : cls.getters) {
            if (ctjs::FuncOp fn = targetOf(entry.getValue())) { startedBy[fn.getOperation()]; }
        }
        const auto early = [&](mlir::Operation * read) {
            if (runsAfter(read, defined, startedBy)) { return false; }
            refuse("a read of a static member" + where(read) +
                   " can run before the class is defined");
            return true;
        };
        if (llvm::any_of(calls, [&](staticCall s) { return early(s.call); }) ||
            llvm::any_of(getterReads, [&](auto & r) { return early(r.first); })) {
            continue;
        }

        // The rewrite.
        for (staticCall & s : calls) {
            mlir::OpBuilder at(s.call);
            const mlir::Value undefined = undefinedAt(at, s.call.getLoc());
            auto direct = directCall(at, s.call.getLoc(), s.target, undefined, undefined, undefined,
                                     {}, s.call.getArgs(), parametersOf(s.target));
            s.call.getResult().replaceAllUsesWith(direct.getResult());
            s.call.erase();
            ++out.calls;
        }
        for (auto & [load, target] : getterReads) {
            mlir::OpBuilder at(load);
            const mlir::Value undefined = undefinedAt(at, load.getLoc());
            auto direct = directCall(at, load.getLoc(), target, undefined, undefined, undefined, {},
                                     mlir::ValueRange{}, parametersOf(target));
            load.getResult().replaceAllUsesWith(direct.getResult());
            eraseWithFeeders(load);
            ++out.calls;
        }
        llvm::SetVector<mlir::Operation *> loads;
        for (staticCall & s : calls) { loads.insert(s.load); }
        for (mlir::Operation * load : loads) {
            if (load->use_empty() || llvm::all_of(load->getUsers(), [](mlir::Operation * u) {
                    return llvm::isa<ctjs::RootOp>(u);
                })) {
                eraseWithFeeders(load);
            }
        }
        // Names nothing reads any more: `this.constructor`, reads of the
        // binding and of its captures. Later ones can feed earlier ones.
        for (bool erased = true; erased;) {
            erased = false;
            for (mlir::Value name : names) {
                mlir::Operation * made = name.getDefiningOp();
                if (!llvm::isa_and_nonnull<ctjs::GetPropertyOp, ctjs::CellGetOp,
                                           ctjs::LoadUpvalueOp>(made) ||
                    !llvm::all_of(made->getUsers(),
                                  [](mlir::Operation * u) { return llvm::isa<ctjs::RootOp>(u); })) {
                    continue;
                }
                names.remove(name);
                eraseWithFeeders(made);
                erased = true;
                break;
            }
        }
        // Capture slots that held the class and that nothing reads any more,
        // nested closures first, so that no slot is removed while a closure
        // inside its target is still filled from it.
        llvm::MapVector<mlir::Operation *, llvm::SmallVector<unsigned>> unread;
        for (auto & [made, slot] : llvm::reverse(slotOrder)) {
            unread[made.getOperation()].push_back(slot);
        }
        for (auto & [op, candidates] : unread) {
            auto made = llvm::cast<ctjs::CreateClosureOp>(op);
            ctjs::FuncOp target = targetOf(made);
            llvm::SmallVector<unsigned> gone;
            for (const unsigned slot : candidates) {
                bool read = false;
                target.getBody().walk([&](mlir::Operation * o) {
                    if (llvm::isa<ctjs::LoadUpvalueOp, ctjs::StoreUpvalueOp>(o)) {
                        read |= o->getAttrOfType<mlir::IntegerAttr>("index").getInt() ==
                                static_cast<std::int64_t>(slot);
                    } else if (auto nested = llvm::dyn_cast<ctjs::CreateClosureOp>(o)) {
                        for (unsigned j = 0; j < nested.getUpvalues().size(); ++j) {
                            read |= enclosingIndex(nested, j) == static_cast<std::int32_t>(slot);
                        }
                    }
                });
                if (!read) { gone.push_back(slot); }
            }
            if (!gone.empty()) { removeCaptureSlots(made, gone); }
        }
        // A local binding of the class that no closure captures any more
        // holds the constructor at every read a write dominates: forward it,
        // so that `new K` names the constructor's closure as it does for an
        // uncaptured binding.
        for (mlir::Operation * op : boxes) {
            auto box = llvm::cast<ctjs::CreateCellOp>(op);
            if (box->getAttr(kNativeStaticCell)) { continue; }
            llvm::SmallVector<ctjs::CellSetOp> writes;
            llvm::SmallVector<ctjs::CellGetOp> reads;
            bool other = false;
            for (mlir::OpOperand & use : box.getResult().getUses()) {
                switch (classify(use)) {
                case cellUse::read:
                    reads.push_back(llvm::cast<ctjs::CellGetOp>(use.getOwner()));
                    break;
                case cellUse::write:
                    writes.push_back(llvm::cast<ctjs::CellSetOp>(use.getOwner()));
                    break;
                default: other |= !llvm::isa<ctjs::RootOp>(use.getOwner()); break;
                }
            }
            const auto written = [&](ctjs::CellGetOp read) {
                return llvm::any_of(writes, [&](ctjs::CellSetOp write) {
                    return reachesInFrame(write.getOperation(), read.getOperation());
                });
            };
            if (other || writes.empty() || !llvm::all_of(reads, written)) { continue; }
            for (ctjs::CellGetOp read : reads) {
                read.getResult().replaceAllUsesWith(cls.constructor.getResult());
                eraseWithFeeders(read);
            }
            for (ctjs::CellSetOp write : writes) { write.erase(); }
            eraseWithFeeders(box);
        }
        // The definitions, their `__home` edges and the closures.
        for (auto & entry : statics) {
            member m = entry.getValue();
            for (mlir::Operation * user : llvm::make_early_inc_range(m.closure->getUsers())) {
                cls.setup.erase(user);
                cls.definitions.erase(user);
                eraseWithFeeders(user);
            }
            eraseWithFeeders(m.closure);
            if (--closuresOf[m.target.getOperation()] == 0) {
                // Every caller is now a visible direct call; as in lift(), the
                // function must be private for type inference to see them all.
                mlir::SymbolTable::setSymbolVisibility(m.target,
                                                       mlir::SymbolTable::Visibility::Private);
            }
        }
        // A static member nothing reads stays as a function nobody calls.
        // If it is refused, the unreachable-refusal pass in LowerToEmitC.cpp
        // removes it and records a row in ctnative.unreachable_refusals.
        cls.statics.clear();
        cls.staticGetters.clear();
        changed = true;
        indexStarters();
    }
    return changed;
}

} // namespace ctcompile::ctnative::lowering_detail
