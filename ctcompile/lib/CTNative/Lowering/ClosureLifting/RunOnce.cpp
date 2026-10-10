//===- ClosureLifting/RunOnce.cpp - Static cells of run-once frames -------===//
//
// A function whose body is proved to run at most once per program run has at
// most one activation, so each binding it boxes exists at most once, and a
// namespace-scope variable can hold that binding for the whole program. This
// file proves which functions run once and turns their boxes into such
// variables ("static cells"). The box itself becomes the variable, and every
// closure that captured it reads and writes the variable by name, through a
// reference cell in its own body, instead of through a capture slot. A closure
// whose captures were all static cells then has none, and lifts or escapes as
// an ordinary function.
//
// It is a fallback. closureLifter::run() calls it before the lift, and it
// keeps only the bindings that a closure the lift would refuse captures; a
// trial lift of a copy of the module says which closures those are. A program
// the lift already lowers therefore does not change.
//
// The temporal dead zone needs nothing here: the bytecode compiler spells it as
// a hidden `@tdz:<name>` boolean binding and an explicit check before each
// read, so the flag is one more boxed binding and the checks stay ordinary
// code.
//
//===----------------------------------------------------------------------===//

#include "ClosureLifter.h"
#include "ctcompile/Support/CppLiterals.hpp"

namespace ctcompile::ctnative::lowering_detail {
namespace {

/// Operations each of whose regions runs at most once every time the
/// operation itself runs.
bool runsRegionsOnce(mlir::Operation * op) {
    return llvm::isa<mlir::scf::IfOp, mlir::scf::IndexSwitchOp, mlir::scf::ExecuteRegionOp>(op);
}

/// Why `op` may run more than once in one activation of `holder`, the
/// function that contains it, as a predicate for a sentence whose subject
/// names `op`; nullopt when it runs at most once.
std::optional<std::string> whyRepeats(mlir::Operation * op, ctjs::FuncOp holder) {
    if (!holder.getBody().hasOneBlock()) {
        return "sits in `" + holder.getSymName().str() +
               "`, whose unstructured control flow can run a block more than once";
    }
    for (mlir::Operation * at = op->getParentOp(); at && at != holder.getOperation();
         at = at->getParentOp()) {
        if (!runsRegionsOnce(at)) {
            return ("sits inside `" + at->getName().getStringRef() +
                    "`, which can run its body more than once")
                .str();
        }
    }
    return std::nullopt;
}

/// True when `a` and `b`, two operations of one function, are in different
/// arms of one scf.if or scf.index_switch, so that at most one of them runs.
bool exclusive(mlir::Operation * a, mlir::Operation * b) {
    llvm::DenseMap<mlir::Operation *, mlir::Region *> through;
    for (mlir::Region * r = a->getParentRegion(); r != nullptr;) {
        mlir::Operation * owner = r->getParentOp();
        through[owner] = r;
        if (llvm::isa<ctjs::FuncOp>(owner)) { break; }
        r = owner->getParentRegion();
    }
    for (mlir::Region * r = b->getParentRegion(); r != nullptr;) {
        mlir::Operation * owner = r->getParentOp();
        if (const auto found = through.find(owner); found != through.end()) {
            return found->second != r &&
                   llvm::isa<mlir::scf::IfOp, mlir::scf::IndexSwitchOp>(owner);
        }
        if (llvm::isa<ctjs::FuncOp>(owner)) { break; }
        r = owner->getParentRegion();
    }
    return false;
}

/// The binding name the importer put in a box's location (a `binding:<name>`
/// NameLoc part), or empty.
std::string bindingName(mlir::Location where) {
    std::string name;
    where->walk([&](mlir::Location part) {
        if (auto named = llvm::dyn_cast<mlir::NameLoc>(part)) {
            llvm::StringRef text = named.getName().getValue();
            if (text.consume_front("binding:")) {
                name = text.str();
                return mlir::WalkResult::interrupt();
            }
        }
        return mlir::WalkResult::advance();
    });
    return name;
}

/// A C++ identifier for the static variable that holds `binding`. A TDZ flag
/// `@tdz:x` becomes `tdz_x`; any other character a C++ name cannot hold
/// becomes `_`.
std::string staticSpelling(llvm::StringRef binding) {
    std::string out = "static_";
    if (binding.consume_front("@tdz:")) { out += "tdz_"; }
    if (binding.empty()) { return out + "binding"; }
    for (const char c : binding) { out += cpp::is_identifier_char(c) ? c : '_'; }
    return out;
}

} // namespace

/// Collects in `sites` every call that can run `target` through `value`, a
/// closure value naming it, and returns why `value` can run it some other way.
/// The value may be passed on as an argument of a direct call, and the callee's
/// parameter is then followed the same way.
std::optional<std::string> closureLifter::callsThrough(
    mlir::Value value, ctjs::FuncOp target, llvm::SmallVectorImpl<mlir::Operation *> & sites,
    llvm::DenseSet<mlir::Value> & seen) {
    if (!seen.insert(value).second) { return std::nullopt; }
    for (mlir::OpOperand & use : value.getUses()) {
        mlir::Operation * user = use.getOwner();
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(user)) {
            if (use.getOperandNumber() == 0) {
                sites.push_back(user);
                continue;
            }
            // An argument of a call whose callee is a closure made here
            // (`(function (f) { ... })(value)`): follow the parameter.
            auto callee = call.getCallee().getDefiningOp<ctjs::CreateClosureOp>();
            ctjs::FuncOp called = callee ? targetOf(callee) : ctjs::FuncOp{};
            const unsigned argument = use.getOperandNumber() - 2;
            if (use.getOperandNumber() >= 2 && called && !called.getBody().empty() &&
                ctjs::implicit_arguments + argument < called.getBody().front().getNumArguments()) {
                const mlir::Value parameter =
                    called.getBody().front().getArgument(ctjs::implicit_arguments + argument);
                if (auto why = callsThrough(parameter, target, sites, seen)) { return why; }
                continue;
            }
        }
        if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(user)) {
            // The callee value of a direct call of `target` itself: the call
            // is counted by symbol, and the value is only carried beside it.
            if (use.getOperandNumber() == ctjs::arg_callee &&
                direct.getCallee() == target.getSymName()) {
                continue;
            }
            ctjs::FuncOp callee = direct.getTarget();
            if (use.getOperandNumber() >= ctjs::implicit_arguments && callee &&
                !callee.getBody().empty()) {
                const mlir::Value parameter =
                    callee.getBody().front().getArgument(use.getOperandNumber());
                if (auto why = callsThrough(parameter, target, sites, seen)) { return why; }
                continue;
            }
        }
        return ("its closure value reaches `" + user->getName().getStringRef() +
                "`, which can call it any number of times")
            .str();
    }
    return std::nullopt;
}

/// Why `fn`'s body may run more than once per program run, given that the
/// functions in `once` are proved to run at most once. Nullopt proves it.
///
/// The bytecode entry (function 0) runs once. Any other function runs at most
/// once when every way to run it is a call in a function already proved, that
/// call cannot repeat within that function's one activation, and no two of the
/// calls can both run. The ways to run it are its direct calls and the calls
/// through its closure values; any other use of a closure value refuses. The
/// proof is a well-founded induction from the entry, which is what excludes
/// recursion: a call of `fn` made from `fn`, or from anything only `fn`'s run
/// proves, sits in a function that is not in `once` yet.
std::optional<std::string> closureLifter::whyNotRunOnce(
    ctjs::FuncOp fn, const llvm::DenseSet<mlir::Operation *> & once,
    const llvm::DenseMap<mlir::Operation *, llvm::SmallVector<mlir::Operation *>> & symbolUsers,
    const llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::CreateClosureOp>> & creators) {
    if (functionIndex(fn) == 0u) { return std::nullopt; }
    if (fn.getBody().empty()) { return std::string{"it has no body"}; }
    bool suspends = false;
    fn.getBody().walk([&](mlir::Operation * o) {
        suspends |= llvm::isa<ctjs::SuspendOp, ctjs::ResumePointOp, ctjs::WrapPromiseOp>(o);
    });
    if (suspends) {
        return std::string{"it is a generator or an async function, whose body is resumed"};
    }
    // Its own closure (%arg2) may only find captures or build nested closures;
    // any other use is a function value of itself, through which it can call
    // itself.
    for (mlir::OpOperand & use : fn.getBody().front().getArgument(ctjs::arg_callee).getUses()) {
        mlir::Operation * user = use.getOwner();
        if (llvm::isa<ctjs::LoadUpvalueOp, ctjs::StoreUpvalueOp>(user) ||
            (llvm::isa<ctjs::CreateClosureOp>(user) && use.getOperandNumber() == 0)) {
            continue;
        }
        return ("it reads its own closure as a value (`" + user->getName().getStringRef() +
                "`), so it can call itself")
            .str();
    }
    llvm::SmallVector<mlir::Operation *> sites;
    for (mlir::Operation * user : symbolUsers.lookup(fn.getOperation())) {
        auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(user);
        if (!direct || direct.getCallee() != fn.getSymName()) {
            return ("its symbol is used by `" + user->getName().getStringRef() +
                    "`, which is not a direct call")
                .str();
        }
        sites.push_back(user);
    }
    llvm::DenseSet<mlir::Value> seen;
    for (ctjs::CreateClosureOp made : creators.lookup(fn.getOperation())) {
        if (auto why = callsThrough(made.getResult(), fn, sites, seen)) { return why; }
    }
    for (mlir::Operation * site : sites) {
        auto holder = site->getParentOfType<ctjs::FuncOp>();
        if (!holder || !once.contains(holder.getOperation())) {
            return "it is called from `" +
                   (holder ? holder.getSymName().str() : std::string{"outside any function"}) +
                   "`, which is not proved to run once";
        }
        if (auto why = whyRepeats(site, holder)) { return "a call of it " + *why; }
    }
    for (size_t i = 0; i < sites.size(); ++i) {
        for (size_t j = i + 1; j < sites.size(); ++j) {
            if (sites[i]->getParentOfType<ctjs::FuncOp>() !=
                    sites[j]->getParentOfType<ctjs::FuncOp>() ||
                !exclusive(sites[i], sites[j])) {
                return std::string{"two of its calls can both run"};
            }
        }
    }
    return std::nullopt;
}

/// Lifts a copy of `module` as it stands and reports, by walk-order id, the
/// closures that lift leaves unlifted (kTrialClosure) and the boxes it binds
/// as local functions (kTrialCell).
closureLifter::trialOutcome closureLifter::trialLift() {
    mlir::Builder builder(context);
    int64_t next = 0;
    module.walk([&](ctjs::CreateClosureOp made) {
        made->setAttr(kTrialClosure, builder.getI64IntegerAttr(next++));
    });
    next = 0;
    module.walk([&](ctjs::CreateCellOp cell) {
        cell->setAttr(kTrialCell, builder.getI64IntegerAttr(next++));
    });
    mlir::OwningOpRef<mlir::ModuleOp> copy(module.clone());
    module.walk([&](ctjs::CreateClosureOp made) { made->removeAttr(kTrialClosure); });
    module.walk([&](ctjs::CreateCellOp cell) { cell->removeAttr(kTrialCell); });
    closureLifter trial{*copy};
    trial.staticsDone = true;
    (void)trial.run();
    trialOutcome outcome;
    copy->walk([&](ctjs::CreateClosureOp made) {
        auto id = made->getAttrOfType<mlir::IntegerAttr>(kTrialClosure);
        if (id && !made->hasAttr(kNativeLifted) && !isDeclarationClosure(made)) {
            outcome.refused.insert(id.getInt());
        }
    });
    outcome.bound = std::move(trial.boundTrialCells);
    return outcome;
}

/// Turns the captured bindings of run-once functions that a closure the lift
/// would refuse reaches into static cells (see the file header). Returns
/// whether it changed the module. It runs before the lift, so that lift sees
/// those closures without the captures; which closures it would refuse is
/// what a trial lift of a copy of the module answers.
bool closureLifter::makeRunOnceCellsStatic(liftReport & out) {
    llvm::SmallVector<ctjs::CreateCellOp> boxes;
    module.walk([&](ctjs::CreateCellOp cell) {
        if (cell->hasAttr(kNativeStaticCell)) { return; }
        if (llvm::any_of(cell.getResult().getUses(),
                         [](mlir::OpOperand & use) { return classify(use) == cellUse::capture; })) {
            boxes.push_back(cell);
        }
    });
    if (boxes.empty()) { return false; }

    // Who names each function: its creating closures, and its symbol users.
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::CreateClosureOp>> creators;
    module.walk([&](ctjs::CreateClosureOp made) {
        if (ctjs::FuncOp target = targetOf(made)) {
            creators[target.getOperation()].push_back(made);
        }
    });
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<mlir::Operation *>> symbolUsers;
    if (auto uses = mlir::SymbolTable::getSymbolUses(&module.getBodyRegion())) {
        for (const mlir::SymbolTable::SymbolUse & use : *uses) {
            auto * named = mlir::SymbolTable::lookupNearestSymbolFrom(module.getOperation(),
                                                                      use.getSymbolRef());
            if (named) { symbolUsers[named].push_back(use.getUser()); }
        }
    }

    // The run-once functions, as a fixpoint from the entry.
    llvm::DenseSet<mlir::Operation *> once;
    llvm::DenseMap<mlir::Operation *, std::string> notOnce;
    for (bool grew = true; grew;) {
        grew = false;
        for (ctjs::FuncOp fn : module.getOps<ctjs::FuncOp>()) {
            if (once.contains(fn.getOperation())) { continue; }
            if (auto why = whyNotRunOnce(fn, once, symbolUsers, creators)) {
                notOnce[fn.getOperation()] = *why;
                continue;
            }
            once.insert(fn.getOperation());
            notOnce.erase(fn.getOperation());
            grew = true;
        }
    }

    // Every (closure, slot) through which a static cell would be reached:
    // the direct captures, then each nested closure that fills a slot of its
    // own from one of those (its `enclosing_indices`).
    struct plan {
        ctjs::CreateCellOp cell;
        llvm::SmallVector<std::pair<ctjs::CreateClosureOp, unsigned>> slots;
    };
    llvm::SmallVector<plan> plans;
    for (ctjs::CreateCellOp cell : boxes) {
        plan p{cell, {}};
        bool usable = true;
        for (mlir::OpOperand & use : cell.getResult().getUses()) {
            const cellUse kind = classify(use);
            if (kind == cellUse::read || kind == cellUse::write) { continue; }
            if (kind != cellUse::capture) {
                usable = false;
                break;
            }
            p.slots.emplace_back(llvm::cast<ctjs::CreateClosureOp>(use.getOwner()),
                                 use.getOperandNumber() - kFirstCapture);
        }
        for (size_t k = 0; usable && k < p.slots.size(); ++k) {
            ctjs::CreateClosureOp made = p.slots[k].first;
            const unsigned slot = p.slots[k].second;
            ctjs::FuncOp target = targetOf(made);
            // One creator per target, as removeCaptureSlots requires, and
            // none already lifted (its captures are call arguments now).
            if (!target || creators.lookup(target.getOperation()).size() != 1 ||
                made->hasAttr(kNativeLifted)) {
                usable = false;
                break;
            }
            target.getBody().walk([&](ctjs::CreateClosureOp nested) {
                const mlir::DenseI32ArrayAttr indices = nested.getEnclosingIndicesAttr();
                if (!indices) { return; }
                for (auto [at, from] : llvm::enumerate(indices.asArrayRef())) {
                    if (from == static_cast<std::int32_t>(slot)) {
                        p.slots.emplace_back(nested, static_cast<unsigned>(at));
                    }
                }
            });
        }
        if (usable) { plans.push_back(std::move(p)); }
    }
    if (plans.empty()) { return false; }
    // Keep the bindings some closure over them would otherwise be refused
    // for; a binding whose closures all lift stays as the lift leaves it. A
    // binding the lift turns into direct calls (bindLocalFunctions: `function
    // f() {}` called by name) also stays: a static variable would be read by
    // name, so every call through it would stay an indirect call.
    const trialOutcome trial = trialLift();
    llvm::DenseMap<mlir::Operation *, int64_t> idOf;
    {
        int64_t next = 0;
        module.walk([&](ctjs::CreateClosureOp made) { idOf[made.getOperation()] = next++; });
        next = 0;
        module.walk([&](ctjs::CreateCellOp cell) { idOf[cell.getOperation()] = next++; });
    }
    llvm::erase_if(plans, [&](const plan & p) {
        ctjs::CreateCellOp cell = p.cell;
        if (trial.bound.contains(idOf.lookup(cell.getOperation()))) { return true; }
        const bool needed = llvm::any_of(p.slots, [&](auto entry) {
            return trial.refused.contains(idOf.lookup(entry.first.getOperation()));
        });
        if (!needed) { return true; }
        auto owner = cell->getParentOfType<ctjs::FuncOp>();
        if (!owner) { return true; }
        if (!once.contains(owner.getOperation())) {
            owner->setAttr(kNativeNotRunOnce,
                           mlir::StringAttr::get(context, notOnce.lookup(owner.getOperation())));
            return true;
        }
        if (auto why = whyRepeats(cell, owner)) {
            owner->setAttr(kNativeNotRunOnce,
                           mlir::StringAttr::get(context, "a binding it boxes " + *why));
            return true;
        }
        return false;
    });
    if (plans.empty()) { return false; }

    // Rewrite: name the variable, give each reader a reference cell, and
    // remove the slots, deepest closure first (see bindLocalFunctions for why
    // the order matters to removeCaptureSlots).
    llvm::StringSet<> taken;
    const auto valueType = ctjs::ValueType::get(context);
    llvm::MapVector<mlir::Operation *, slotRemoval> removals;
    for (plan & p : plans) {
        std::string name = staticSpelling(bindingName(p.cell.getLoc()));
        for (unsigned n = 2;
             taken.contains(name) || mlir::SymbolTable::lookupSymbolIn(module.getOperation(), name);
             ++n) {
            name = staticSpelling(bindingName(p.cell.getLoc())) + "_" + std::to_string(n);
        }
        taken.insert(name);
        const auto symbol = mlir::StringAttr::get(context, name);
        p.cell->setAttr(kNativeStaticCell, symbol);
        p.cell->setAttr(kNativeCarried, mlir::UnitAttr::get(context));
        p.cell->removeAttr(kNativeCellReason);
        llvm::DenseMap<mlir::Operation *, ctjs::CreateCellOp> references;
        for (const auto & [madeAt, slotAt] : p.slots) {
            ctjs::CreateClosureOp made = madeAt;
            const unsigned slot = slotAt;
            ctjs::FuncOp target = targetOf(made);
            ctjs::CreateCellOp & reference = references[target.getOperation()];
            if (!reference) {
                // After the frame is entered, where the box would have been.
                mlir::Block & body = target.getBody().front();
                mlir::OpBuilder at = mlir::OpBuilder::atBlockBegin(&body);
                for (auto frame : body.getOps<ctjs::FrameEnterOp>()) {
                    at.setInsertionPointAfter(frame);
                    break;
                }
                auto undefined = ctjs::ConstantOp::create(at, p.cell.getLoc(), valueType,
                                                          ctjs::UndefinedAttr::get(context));
                reference = ctjs::CreateCellOp::create(at, p.cell.getLoc(), valueType, undefined);
                reference->setAttr(kNativeStaticCell, symbol);
                reference->setAttr(kNativeStaticReference, mlir::UnitAttr::get(context));
                reference->setAttr(kNativeCarried, mlir::UnitAttr::get(context));
            }
            llvm::SmallVector<mlir::Operation *> accesses;
            target.getBody().walk([&](mlir::Operation * o) {
                if (auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(o);
                    read && read.getIndex() == slot) {
                    accesses.push_back(o);
                } else if (auto write = llvm::dyn_cast<ctjs::StoreUpvalueOp>(o);
                           write && write.getIndex() == slot) {
                    accesses.push_back(o);
                }
            });
            for (mlir::Operation * o : accesses) {
                mlir::OpBuilder at(o);
                if (auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(o)) {
                    auto get =
                        ctjs::CellGetOp::create(at, o->getLoc(), valueType, reference.getResult());
                    read.getResult().replaceAllUsesWith(get.getResult());
                } else {
                    ctjs::CellSetOp::create(at, o->getLoc(), reference.getResult(),
                                            llvm::cast<ctjs::StoreUpvalueOp>(o).getValue());
                }
                o->erase();
            }
            removals[made.getOperation()].slots.push_back(slot);
        }
        ++out.statics;
    }
    llvm::DenseMap<mlir::Operation *, unsigned> depth;
    const auto depthOf = [&](mlir::Operation * made) {
        unsigned d = 0;
        for (auto in = made->getParentOfType<ctjs::FuncOp>(); in && d <= creators.size(); ++d) {
            const auto found = creators.find(in.getOperation());
            if (found == creators.end() || found->second.size() != 1) { break; }
            in = found->second.front()->getParentOfType<ctjs::FuncOp>();
        }
        return d;
    };
    llvm::SmallVector<std::pair<mlir::Operation *, slotRemoval *>> ordered;
    for (auto & entry : removals) {
        depth[entry.first] = depthOf(entry.first);
        ordered.emplace_back(entry.first, &entry.second);
    }
    llvm::stable_sort(ordered, [&](const auto & left, const auto & right) {
        return depth.lookup(left.first) > depth.lookup(right.first);
    });
    for (auto & [made, removal] : ordered) {
        removeCaptureSlots(llvm::cast<ctjs::CreateClosureOp>(made), removal->slots);
    }
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
