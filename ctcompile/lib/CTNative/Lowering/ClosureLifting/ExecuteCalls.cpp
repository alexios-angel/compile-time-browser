//===- ClosureLifting/ExecuteCalls.cpp - Calls through execute --*- C++ -*-===//
//
// Bootstrap calls its callbacks through one helper (bootstrap.bundle.js `_`):
//
//   const execute = (possibleCallback, args = [], defaultValue = possibleCallback) =>
//       typeof possibleCallback === 'function' ? possibleCallback.call(...args) : defaultValue;
//
// Given a function and nothing else, `execute(f)` calls f with an undefined
// receiver and no arguments: `args` defaults to an empty array, so the spread
// passes nothing, and Function.prototype.call with no arguments calls f with
// `this` undefined. callExecutedFunctions rewrites each such call into
// `f()`, before any binding or closure rule reads it, so a callback a frame
// hands to execute is a callback it calls, which the local-binding and
// host-callback rules already lower.
//
// The helper is recognised by what its body does, not by its name
// (isExecuteHelper). The argument is a function when it is a closure, or a
// read of a binding written once with a closure before any read of it, whose
// every read only calls it or hands it to execute (heldClosure), so no own
// `call` property can shadow Function.prototype.call. The rewrite also needs
// the realm's own Array iteration, for the spread of the empty array, and the
// realm's own Function.prototype.call: realmBuiltinsUnproved finds nothing,
// and the program never names `Function`.
//
// A callback can also arrive as a parameter, as in Bootstrap's components:
//
//   _queueCallback(callback, element, isAnimated) {
//       executeAfterTransition(callback, element, isAnimated);
//   }
//
// executeAfterTransition hands it to execute when nothing is animated, and
// otherwise boxes it for the transitionend listener, which hands it to
// execute later. A per-call-site copy (CallSiteCopies.cpp) makes such a
// callback in the copy of executeAfterTransition, and the relay below
// (relaysCallback, relayCallback) lets it make it where it is called
// instead: execute's call of it becomes a plain call, and a closure that
// captures its box and only calls what the box holds takes the callback's
// captures and makes the callback itself where it read the box. The box is
// never written and the callback writes none of its captures (the copy's
// settledAt), so each callback made there calls the same code on the same
// values, and since nothing but a call sees it, nothing can tell the
// callbacks apart.
//
//===----------------------------------------------------------------------===//

#include "../ForOf.h"
#include "ClosureLifter.h"

#include "llvm/ADT/SetVector.h"

#include <functional>

namespace ctcompile::ctnative::lowering_detail {

namespace {

/// Whether `condition` is the truthiness of `typeof value` compared with
/// "function" (`==` or `===`, either operand order).
bool testsFunction(mlir::Value condition, mlir::Value value) {
    auto truthy = condition.getDefiningOp<ctjs::TruthyOp>();
    auto compare = truthy ? truthy.getValue().getDefiningOp<ctjs::CompareOp>() : nullptr;
    if (!compare || (compare.getKind() != ctjs::CompareKind::Eq &&
                     compare.getKind() != ctjs::CompareKind::StrictEq)) {
        return false;
    }
    const auto typeOf = [&](mlir::Value v) {
        auto unary = v.getDefiningOp<ctjs::UnaryOp>();
        return unary && unary.getKind() == ctjs::UnaryKind::TypeOf && unary.getOperand() == value;
    };
    const auto named = [](mlir::Value v) { return ctjs::constantKey(v) == "function"; };
    return (typeOf(compare.getLhs()) && named(compare.getRhs())) ||
           (named(compare.getLhs()) && typeOf(compare.getRhs()));
}

/// Whether `value` is `parameter`, or a fresh empty array when `parameter` is
/// undefined: the scf.if a default parameter `= []` imports as.
bool defaultsToEmptyArray(mlir::Value value, mlir::Value parameter) {
    auto branch = value.getDefiningOp<mlir::scf::IfOp>();
    if (!branch || branch->getNumResults() != 1 || branch.getElseRegion().empty()) { return false; }
    auto truthy = branch.getCondition().getDefiningOp<ctjs::TruthyOp>();
    auto compare = truthy ? truthy.getValue().getDefiningOp<ctjs::CompareOp>() : nullptr;
    if (!compare || compare.getKind() != ctjs::CompareKind::StrictEq ||
        !((compare.getLhs() == parameter && ctjs::isUndefinedConstant(compare.getRhs())) ||
          (compare.getRhs() == parameter && ctjs::isUndefinedConstant(compare.getLhs())))) {
        return false;
    }
    auto empty = branch.thenYield().getOperand(0).getDefiningOp<ctjs::CreateArrayOp>();
    return empty && empty.getElements().empty() && branch.elseYield().getOperand(0) == parameter;
}

/// Whether `fn` is the execute helper: its one return gives a conditional on
/// `typeof p0` being "function" whose true arm is `p0.call(...a)`, where `a`
/// is a fresh array that only receives the elements of `p1` (or of a fresh
/// empty array when `p1` is undefined), and nothing else in it has an effect.
/// Called with p1 undefined, it calls p0 with an undefined receiver and no
/// arguments; the false arm is not taken for a function.
bool isExecuteHelper(ctjs::FuncOp fn) {
    mlir::Block * entry = ctjs::implicitEntry(fn);
    if (!entry || ctjs::parameterCount(*entry) < 2 || ctjs::readsRawArguments(fn)) { return false; }
    const mlir::Value callback = entry->getArgument(ctjs::implicit_arguments);
    const mlir::Value args = entry->getArgument(ctjs::implicit_arguments + 1);
    ctjs::ReturnOp exit;
    unsigned returns = 0;
    fn.walk([&](ctjs::ReturnOp ret) {
        exit = ret;
        ++returns;
    });
    if (returns != 1 || exit->getBlock() != entry) { return false; }
    auto branch = exit.getValue().getDefiningOp<mlir::scf::IfOp>();
    if (!branch || branch->getNumResults() != 1 || branch.getElseRegion().empty() ||
        !testsFunction(branch.getCondition(), callback)) {
        return false;
    }
    auto spread = branch.thenYield().getOperand(0).getDefiningOp<ctjs::CallSpreadOp>();
    auto method = spread ? spread.getCallee().getDefiningOp<ctjs::GetPropertyOp>() : nullptr;
    if (!method || spread.getReceiver() != callback || method.getObject() != callback ||
        ctjs::constantKey(method.getKey()) != "call") {
        return false;
    }
    auto array = spread.getArgs().getDefiningOp<ctjs::CreateArrayOp>();
    if (!array || !array.getElements().empty()) { return false; }
    // Each element the array receives is read from the iterable of `p1` or
    // its empty default, and so is each other read in the body.
    llvm::DenseSet<mlir::Operation *> iterables;
    for (mlir::OpOperand & use : array.getResult().getUses()) {
        if (use.getOwner() == spread) { continue; }
        auto append = llvm::dyn_cast<ctjs::AppendOp>(use.getOwner());
        auto element = append && append.getArray() == array.getResult()
                           ? append.getElement().getDefiningOp<ctjs::GetPropertyOp>()
                           : nullptr;
        auto iterable = element ? element.getObject().getDefiningOp<ctjs::IterableOp>() : nullptr;
        if (!iterable || !defaultsToEmptyArray(iterable.getSource(), args)) { return false; }
        iterables.insert(iterable);
    }
    return !fn.walk([&](mlir::Operation * op) {
                  if (op == fn.getOperation() || op == spread.getOperation() ||
                      op == method.getOperation()) {
                      return mlir::WalkResult::advance();
                  }
                  if (auto append = llvm::dyn_cast<ctjs::AppendOp>(op)) {
                      return append.getArray() == array.getResult() ? mlir::WalkResult::advance()
                                                                    : mlir::WalkResult::interrupt();
                  }
                  if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(op)) {
                      return unary.getKind() == ctjs::UnaryKind::TypeOf ||
                                     unary.getKind() == ctjs::UnaryKind::Not
                                 ? mlir::WalkResult::advance()
                                 : mlir::WalkResult::interrupt();
                  }
                  if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
                      auto from = read.getObject().getDefiningOp<ctjs::IterableOp>();
                      return from && iterables.contains(from) ? mlir::WalkResult::advance()
                                                              : mlir::WalkResult::interrupt();
                  }
                  return llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::ConstantOp,
                                   ctjs::CompareOp, ctjs::TruthyOp, ctjs::CreateArrayOp,
                                   ctjs::IterableOp, ctjs::BinaryStaticOp, ctjs::ReturnOp,
                                   mlir::scf::IfOp, mlir::scf::YieldOp, mlir::scf::WhileOp,
                                   mlir::scf::ConditionOp>(op)
                             ? mlir::WalkResult::advance()
                             : mlir::WalkResult::interrupt();
              }).wasInterrupted();
}

} // namespace

/// Rewrites each `execute(f)` (see the file comment) whose other arguments
/// are absent or undefined and whose `f` is a function into `f()`.
void closureLifter::callExecutedFunctions() {
    if (!executeCallsRewritable()) { return; }
    // The one closure that makes each function, for reads of its captures.
    llvm::DenseMap<mlir::Operation *, ctjs::CreateClosureOp> makers;
    module.walk([&](ctjs::CreateClosureOp c) {
        if (ctjs::FuncOp target = targetOf(c)) {
            auto [found, fresh] = makers.try_emplace(target.getOperation(), c);
            if (!fresh) { found->second = {}; }
        }
    });
    llvm::DenseMap<mlir::Operation *, bool> helpers;
    const auto isHelper = [&](ctjs::FuncOp fn) {
        auto [found, fresh] = helpers.try_emplace(fn.getOperation(), false);
        if (fresh) { found->second = isExecuteHelper(fn); }
        return found->second;
    };
    // The closure every read of `value` yields, or null. `depth` bounds the
    // walk through bindings, captures and execute calls.
    std::function<ctjs::CreateClosureOp(mlir::Value, unsigned)> heldClosure;
    // Whether `call` hands its first argument to execute.
    const auto executes = [&](mlir::Operation * call, unsigned depth) {
        if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(call)) {
            return direct.getTarget() && isHelper(direct.getTarget());
        }
        auto made = heldClosure(llvm::cast<ctjs::CallOp>(call).getCallee(), depth + 1);
        ctjs::FuncOp target = made ? targetOf(made) : ctjs::FuncOp{};
        return target && isHelper(target);
    };
    // Whether every use of `value`, a read of a binding, calls it or hands it
    // to execute as the first argument.
    const auto onlyCalled = [&](mlir::Value value, unsigned depth) {
        return llvm::all_of(value.getUses(), [&](mlir::OpOperand & use) {
            mlir::Operation * user = use.getOwner();
            if (llvm::isa<ctjs::CallOp>(user) && use.getOperandNumber() == 0) { return true; }
            const unsigned first = llvm::isa<ctjs::CallOp>(user)         ? 2
                                   : llvm::isa<ctjs::CallDirectOp>(user) ? ctjs::implicit_arguments
                                                                         : 0;
            return first != 0 && use.getOperandNumber() == first && executes(user, depth);
        });
    };
    // Whether slot `slot` of `fn`, at any depth of capture relays, is read
    // only to be called and never written. Memoized, and false while it is
    // being decided, so a cycle of relays answers no.
    llvm::DenseMap<std::pair<mlir::Operation *, unsigned>, bool> slotVerdicts;
    std::function<bool(ctjs::FuncOp, unsigned, unsigned)> slotOnlyCalled = [&](ctjs::FuncOp fn,
                                                                               unsigned slot,
                                                                               unsigned depth) {
        mlir::Block * entry = fn ? ctjs::implicitEntry(fn) : nullptr;
        if (!entry || depth > 8) { return false; }
        const auto key = std::make_pair(fn.getOperation(), slot);
        if (const auto found = slotVerdicts.find(key); found != slotVerdicts.end()) {
            return found->second;
        }
        slotVerdicts[key] = false;
        const mlir::Value own = entry->getArgument(ctjs::arg_callee);
        const bool verdict =
            !fn.walk([&](mlir::Operation * op) {
                   bool fits = true;
                   if (auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(op)) {
                       fits = read.getClosure() != own ||
                              static_cast<unsigned>(read.getIndex()) != slot ||
                              onlyCalled(read.getResult(), depth);
                   } else if (auto write = llvm::dyn_cast<ctjs::StoreUpvalueOp>(op)) {
                       fits = write.getClosure() == own &&
                              static_cast<unsigned>(write.getIndex()) != slot;
                   } else if (auto nested = llvm::dyn_cast<ctjs::CreateClosureOp>(op)) {
                       for (unsigned i = 0; fits && i < nested.getUpvalues().size(); ++i) {
                           fits = enclosingIndex(nested, i) != static_cast<std::int32_t>(slot) ||
                                  slotOnlyCalled(targetOf(nested), i, depth + 1);
                       }
                   }
                   return fits ? mlir::WalkResult::advance() : mlir::WalkResult::interrupt();
               }).wasInterrupted();
        slotVerdicts[key] = verdict;
        return verdict;
    };
    // The closure `cell` holds at every read: written once with a closure of
    // its frame, before every read and every call of a closure capturing it
    // (writeReachesEveryRead), and only ever called or handed to execute.
    // Memoized, and null while it is being decided.
    llvm::DenseMap<mlir::Operation *, ctjs::CreateClosureOp> cellVerdicts;
    std::function<ctjs::CreateClosureOp(ctjs::CreateCellOp, unsigned)> bindingClosure;
    const auto decideBinding = [&](ctjs::CreateCellOp cell, unsigned depth) {
        ctjs::CellSetOp write;
        for (mlir::OpOperand & use : cell.getResult().getUses()) {
            switch (classify(use)) {
            case cellUse::write:
                if (write) { return ctjs::CreateClosureOp{}; }
                write = llvm::cast<ctjs::CellSetOp>(use.getOwner());
                break;
            case cellUse::read:
                if (!onlyCalled(use.getOwner()->getResult(0), depth)) {
                    return ctjs::CreateClosureOp{};
                }
                break;
            case cellUse::capture: {
                auto holder = llvm::cast<ctjs::CreateClosureOp>(use.getOwner());
                const unsigned slot = use.getOperandNumber() - kFirstCapture;
                if (enclosingIndex(holder, slot) >= 0 ||
                    !slotOnlyCalled(targetOf(holder), slot, depth)) {
                    return ctjs::CreateClosureOp{};
                }
                break;
            }
            default: return ctjs::CreateClosureOp{};
            }
        }
        auto made = write ? write.getValue().getDefiningOp<ctjs::CreateClosureOp>()
                          : ctjs::CreateClosureOp{};
        if (!made || !made.getResult().hasOneUse() ||
            made->getParentOfType<ctjs::FuncOp>() != cell->getParentOfType<ctjs::FuncOp>() ||
            !writeReachesEveryRead(cell, write)) {
            return ctjs::CreateClosureOp{};
        }
        return made;
    };
    bindingClosure = [&](ctjs::CreateCellOp cell, unsigned depth) {
        if (const auto found = cellVerdicts.find(cell.getOperation());
            found != cellVerdicts.end()) {
            return found->second;
        }
        cellVerdicts[cell.getOperation()] = ctjs::CreateClosureOp{};
        const ctjs::CreateClosureOp made = decideBinding(cell, depth);
        cellVerdicts[cell.getOperation()] = made;
        return made;
    };
    // The cell in slot `slot` of the closure that makes `fn`, following
    // relays out through the closures that make each enclosing function.
    std::function<ctjs::CreateCellOp(ctjs::FuncOp, unsigned, unsigned)> slotCell =
        [&](ctjs::FuncOp fn, unsigned slot, unsigned depth) {
            ctjs::CreateClosureOp maker = makers.lookup(fn.getOperation());
            if (!maker || depth > 8 || slot >= maker.getUpvalues().size()) {
                return ctjs::CreateCellOp{};
            }
            if (const std::int32_t relayed = enclosingIndex(maker, slot); relayed >= 0) {
                auto outer = maker->getParentOfType<ctjs::FuncOp>();
                return outer ? slotCell(outer, static_cast<unsigned>(relayed), depth + 1)
                             : ctjs::CreateCellOp{};
            }
            return maker.getUpvalues()[slot].getDefiningOp<ctjs::CreateCellOp>();
        };
    heldClosure = [&](mlir::Value value, unsigned depth) {
        if (depth > 8) { return ctjs::CreateClosureOp{}; }
        if (auto made = value.getDefiningOp<ctjs::CreateClosureOp>()) { return made; }
        ctjs::CreateCellOp cell;
        if (auto read = value.getDefiningOp<ctjs::CellGetOp>()) {
            cell = read.getCell().getDefiningOp<ctjs::CreateCellOp>();
        } else if (auto read = value.getDefiningOp<ctjs::LoadUpvalueOp>()) {
            auto fn = read->getParentOfType<ctjs::FuncOp>();
            mlir::Block * entry = fn ? ctjs::implicitEntry(fn) : nullptr;
            if (entry && read.getClosure() == entry->getArgument(ctjs::arg_callee)) {
                cell = slotCell(fn, static_cast<unsigned>(read.getIndex()), depth);
            }
        }
        return cell ? bindingClosure(cell, depth + 1) : ctjs::CreateClosureOp{};
    };

    llvm::SmallVector<mlir::Operation *> calls;
    module.walk([&](mlir::Operation * op) {
        if (llvm::isa<ctjs::CallOp, ctjs::CallDirectOp>(op)) { calls.push_back(op); }
    });
    const auto valueType = ctjs::ValueType::get(context);
    // The reads of a helper's binding that fed a rewritten call, and the
    // bindings they read.
    llvm::SmallVector<mlir::Operation *> helperReads;
    llvm::SetVector<mlir::Operation *> helperCells;
    for (mlir::Operation * op : calls) {
        if (!executes(op, 0)) { continue; }
        const mlir::OperandRange args = llvm::isa<ctjs::CallOp>(op)
                                            ? llvm::cast<ctjs::CallOp>(op).getArgs()
                                            : llvm::cast<ctjs::CallDirectOp>(op).getArgs();
        if (args.empty() || !llvm::all_of(args.drop_front(), ctjs::isUndefinedConstant)) {
            continue;
        }
        const mlir::Value callback = args.front();
        ctjs::CreateClosureOp made = heldClosure(callback, 0);
        if (!made || !targetOf(made)) { continue; }
        const mlir::Value helper = llvm::isa<ctjs::CallOp>(op)
                                       ? llvm::cast<ctjs::CallOp>(op).getCallee()
                                       : llvm::cast<ctjs::CallDirectOp>(op).getCalleeValue();
        if (auto read = helper.getDefiningOp<ctjs::CellGetOp>()) {
            helperReads.push_back(read);
            if (auto cell = read.getCell().getDefiningOp<ctjs::CreateCellOp>()) {
                helperCells.insert(cell);
            }
        } else if (auto read = helper.getDefiningOp<ctjs::LoadUpvalueOp>()) {
            helperReads.push_back(read);
            if (auto fn = read->getParentOfType<ctjs::FuncOp>()) {
                if (auto cell = slotCell(fn, static_cast<unsigned>(read.getIndex()), 0)) {
                    helperCells.insert(cell);
                }
            }
        }
        mlir::OpBuilder at(op);
        auto call = ctjs::CallOp::create(at, op->getLoc(), valueType, callback,
                                         undefinedAt(at, op->getLoc()), mlir::ValueRange{});
        op->getResult(0).replaceAllUsesWith(call.getResult());
        op->erase();
    }
    // A function that called the helper only for those calls keeps no capture
    // of it, and a binding of the helper nothing reads any more goes with its
    // closure: the lift would otherwise judge a function value nobody calls.
    for (mlir::Operation * read : helperReads) {
        if (read->use_empty()) { read->erase(); }
    }
    const auto unread = [&](ctjs::FuncOp fn, unsigned slot) {
        mlir::Block * entry = fn ? ctjs::implicitEntry(fn) : nullptr;
        if (!entry) { return false; }
        const mlir::Value own = entry->getArgument(ctjs::arg_callee);
        return !fn.walk([&](mlir::Operation * op) {
                      bool reads = false;
                      if (auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(op)) {
                          reads = read.getClosure() != own ||
                                  static_cast<unsigned>(read.getIndex()) == slot;
                      } else if (auto write = llvm::dyn_cast<ctjs::StoreUpvalueOp>(op)) {
                          reads = write.getClosure() != own ||
                                  static_cast<unsigned>(write.getIndex()) == slot;
                      } else if (auto nested = llvm::dyn_cast<ctjs::CreateClosureOp>(op)) {
                          for (unsigned i = 0; i < nested.getUpvalues().size(); ++i) {
                              reads |= enclosingIndex(nested, i) == static_cast<std::int32_t>(slot);
                          }
                      }
                      return reads ? mlir::WalkResult::interrupt() : mlir::WalkResult::advance();
                  }).wasInterrupted();
    };
    // Innermost closures first, so a slot a nested closure relayed is unread
    // by the time its maker is asked; repeated until nothing changes.
    llvm::DenseMap<mlir::Operation *, unsigned> depths;
    std::function<unsigned(mlir::Operation *, unsigned)> depthOf = [&](mlir::Operation * fn,
                                                                       unsigned guard) {
        if (const auto found = depths.find(fn); found != depths.end()) { return found->second; }
        ctjs::CreateClosureOp maker = makers.lookup(fn);
        auto outer = maker ? maker->getParentOfType<ctjs::FuncOp>() : ctjs::FuncOp{};
        const unsigned depth = outer && guard < 64 ? depthOf(outer, guard + 1) + 1 : 0;
        depths[fn] = depth;
        return depth;
    };
    for (bool changed = !helperCells.empty(); changed;) {
        changed = false;
        llvm::SmallVector<std::pair<ctjs::CreateClosureOp, llvm::SmallVector<unsigned>>> drops;
        module.walk([&](ctjs::CreateClosureOp c) {
            ctjs::FuncOp target = targetOf(c);
            if (!target) { return; }
            llvm::SmallVector<unsigned> slots;
            for (unsigned i = 0; i < c.getUpvalues().size(); ++i) {
                ctjs::CreateCellOp cell =
                    enclosingIndex(c, i) < 0
                        ? c.getUpvalues()[i].getDefiningOp<ctjs::CreateCellOp>()
                        : slotCell(target, i, 0);
                if (cell && helperCells.contains(cell) && unread(target, i)) { slots.push_back(i); }
            }
            if (!slots.empty()) { drops.emplace_back(c, std::move(slots)); }
        });
        llvm::stable_sort(drops, [&](const auto & a, const auto & b) {
            return depthOf(targetOf(a.first), 0) > depthOf(targetOf(b.first), 0);
        });
        for (auto & [c, slots] : drops) {
            removeCaptureSlots(c, slots);
            changed = true;
        }
    }
    for (mlir::Operation * held : helperCells) {
        auto cell = llvm::cast<ctjs::CreateCellOp>(held);
        if (!llvm::all_of(cell.getResult().getUses(), [](mlir::OpOperand & use) {
                auto write = llvm::dyn_cast<ctjs::CellSetOp>(use.getOwner());
                auto made = write && use.getOperandNumber() == 0
                                ? write.getValue().getDefiningOp<ctjs::CreateClosureOp>()
                                : ctjs::CreateClosureOp{};
                return made && made.getResult().hasOneUse();
            })) {
            continue;
        }
        for (mlir::Operation * user : llvm::make_early_inc_range(cell.getResult().getUsers())) {
            auto write = llvm::cast<ctjs::CellSetOp>(user);
            mlir::Operation * made = write.getValue().getDefiningOp();
            write.erase();
            made->erase();
        }
        cell.erase();
    }
}

/// Whether execute(f) may become f() in this module (see the file comment).
bool closureLifter::executeCallsRewritable() {
    if (!executeRewritable) {
        bool namesFunction = false;
        module.walk([&](ctjs::LoadGlobalOp load) { namesFunction |= load.getName() == "Function"; });
        executeRewritable = !namesFunction && !realmBuiltinsUnproved(module);
    }
    return *executeRewritable;
}

namespace {

/// Whether `use` hands its value alone to execute, in a module where that
/// call may become a plain call.
bool isExecuteCall(closureLifter & lifter, mlir::OpOperand & use) {
    auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
    return call && use.getOperandNumber() == ctjs::implicit_arguments && call.getTarget() &&
           llvm::all_of(call.getArgs().drop_front(), ctjs::isUndefinedConstant) &&
           isExecuteHelper(call.getTarget()) && lifter.executeCallsRewritable();
}

/// Whether every use of `value` calls it or hands it alone to execute.
bool onlyCalledOrExecuted(closureLifter & lifter, mlir::Value value) {
    return llvm::all_of(value.getUses(), [&](mlir::OpOperand & use) {
        return (llvm::isa<ctjs::CallOp>(use.getOwner()) && use.getOperandNumber() == 0) ||
               isExecuteCall(lifter, use);
    });
}

/// Closures that capture a box, each with the slot that holds it.
using RelayHolders = llvm::SmallVector<std::pair<ctjs::CreateClosureOp, unsigned>>;

/// The closures that capture `box`, each with its slot, when what the box
/// holds can be made where they call it: the box is no static variable,
/// nothing writes it, and each read of it only calls what it holds or hands
/// that to execute; each capturing closure is the one closure of its
/// function, which reads the slot only that way, never writes it and relays
/// it to no closure it makes. Nullopt otherwise.
std::optional<RelayHolders> relayHolders(closureLifter & lifter, ctjs::CreateCellOp box) {
    if (box->hasAttr(kNativeStaticCell)) { return std::nullopt; }
    RelayHolders holders;
    for (mlir::OpOperand & use : box.getResult().getUses()) {
        const cellUse kind = classify(use);
        if (kind == cellUse::read && onlyCalledOrExecuted(lifter, use.getOwner()->getResult(0))) {
            continue;
        }
        if (kind != cellUse::capture) { return std::nullopt; }
        auto holder = llvm::cast<ctjs::CreateClosureOp>(use.getOwner());
        const unsigned slot = use.getOperandNumber() - kFirstCapture;
        ctjs::FuncOp target = lifter.targetOf(holder);
        mlir::Block * entry = target ? ctjs::implicitEntry(target) : nullptr;
        if (!entry || closureLifter::enclosingIndex(holder, slot) >= 0) { return std::nullopt; }
        unsigned makers = 0;
        lifter.module.walk(
            [&](ctjs::CreateClosureOp c) { makers += lifter.targetOf(c) == target ? 1 : 0; });
        const mlir::Value own = entry->getArgument(ctjs::arg_callee);
        const bool unfit =
            target
                .walk([&](mlir::Operation * op) {
                    bool fits = true;
                    if (auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(op)) {
                        fits = read.getClosure() != own ||
                               static_cast<unsigned>(read.getIndex()) != slot ||
                               onlyCalledOrExecuted(lifter, read.getResult());
                    } else if (auto write = llvm::dyn_cast<ctjs::StoreUpvalueOp>(op)) {
                        fits = write.getClosure() == own &&
                               static_cast<unsigned>(write.getIndex()) != slot;
                    } else if (auto nested = llvm::dyn_cast<ctjs::CreateClosureOp>(op)) {
                        for (unsigned i = 0; fits && i < nested.getUpvalues().size(); ++i) {
                            fits = closureLifter::enclosingIndex(nested, i) !=
                                   static_cast<std::int32_t>(slot);
                        }
                    }
                    return fits ? mlir::WalkResult::advance() : mlir::WalkResult::interrupt();
                })
                .wasInterrupted();
        if (makers != 1 || unfit) { return std::nullopt; }
        holders.emplace_back(holder, slot);
    }
    return holders;
}

/// Makes each call that hands `callback` alone to execute a plain call of it,
/// and adds the helper each such call named to `helpers`.
void callExecuted(closureLifter & lifter, mlir::Value callback,
                  llvm::SetVector<mlir::Operation *> & helpers) {
    const auto valueType = ctjs::ValueType::get(callback.getContext());
    for (mlir::OpOperand & use : llvm::make_early_inc_range(callback.getUses())) {
        if (!isExecuteCall(lifter, use)) { continue; }
        auto executed = llvm::cast<ctjs::CallDirectOp>(use.getOwner());
        helpers.insert(executed.getTarget());
        mlir::OpBuilder at(executed);
        auto call = ctjs::CallOp::create(at, executed.getLoc(), valueType, callback,
                                         lifter.undefinedAt(at, executed.getLoc()),
                                         mlir::ValueRange{});
        executed.getResult().replaceAllUsesWith(call.getResult());
        mlir::Operation * helper = executed.getCalleeValue().getDefiningOp();
        executed.erase();
        if (llvm::isa_and_nonnull<ctjs::CellGetOp, ctjs::LoadUpvalueOp>(helper) &&
            helper->use_empty()) {
            helper->erase();
        }
    }
}

} // namespace

/// Whether a per-call-site copy whose last link takes the callback as
/// `parameter` may make the callback where it is called (see the file
/// comment): every use of the parameter calls it, hands it alone to execute,
/// or boxes it for closures relayHolders accepts, and at least one does not
/// just call it.
bool closureLifter::relaysCallback(mlir::BlockArgument parameter) {
    bool relayed = false;
    for (mlir::OpOperand & use : parameter.getUses()) {
        if (llvm::isa<ctjs::CallOp>(use.getOwner()) && use.getOperandNumber() == 0) { continue; }
        auto box = llvm::dyn_cast<ctjs::CreateCellOp>(use.getOwner());
        if (!isExecuteCall(*this, use) && !(box && relayHolders(*this, box))) { return false; }
        relayed = true;
    }
    return relayed;
}

/// The relay, in the copy's last link: `inside`, made from `cells`, now
/// stands where the parameter did. Each closure capturing a box of it takes
/// `cells` as new last slots and makes the callback from them where it read
/// the box, whose slot it then drops; each call handing the callback alone to
/// execute calls it. Each function has one closure that makes it, so every
/// callback made in a capturing closure names a copy of the callback's
/// function from `cloneDeep` (recorded in ctnative.copies), except that the
/// first takes the function itself when `inside` is left with no use.
/// Returns the execute helpers whose calls it replaced, which the copy's sweep
/// of uncalled originals then also asks about.
llvm::SetVector<mlir::Operation *>
closureLifter::relayCallback(ctjs::CreateClosureOp inside, llvm::ArrayRef<mlir::Value> cells,
                             llvm::function_ref<ctjs::FuncOp(ctjs::FuncOp)> cloneDeep) {
    llvm::SetVector<mlir::Operation *> helpers;
    llvm::SmallVector<std::pair<ctjs::CreateCellOp, RelayHolders>> boxes;
    // Whether `inside` keeps a use here, so that no callback made in a
    // holder may take its function.
    bool kept = false;
    for (mlir::OpOperand & use : inside.getResult().getUses()) {
        auto box = llvm::dyn_cast<ctjs::CreateCellOp>(use.getOwner());
        std::optional<RelayHolders> holders = box ? relayHolders(*this, box) : std::nullopt;
        if (!holders) {
            kept = true;
            continue;
        }
        kept |= llvm::any_of(box->getUsers(),
                             [](mlir::Operation * user) { return llvm::isa<ctjs::CellGetOp>(user); });
        boxes.emplace_back(box, std::move(*holders));
    }
    const ctjs::FuncOp callback = targetOf(inside);
    for (auto & [box, holders] : boxes) {
        for (auto [holder, slot] : holders) {
            ctjs::FuncOp target = targetOf(holder);
            const mlir::Value own = target.getBody().front().getArgument(ctjs::arg_callee);
            llvm::SmallVector<std::int32_t> relayed;
            for (mlir::Value cell : cells) {
                relayed.push_back(static_cast<std::int32_t>(holder.getUpvalues().size()));
                addCaptureSlot(holder, cell);
            }
            llvm::SmallVector<ctjs::LoadUpvalueOp> reads;
            target.walk([&](ctjs::LoadUpvalueOp read) {
                if (read.getClosure() == own && static_cast<unsigned>(read.getIndex()) == slot) {
                    reads.push_back(read);
                }
            });
            for (ctjs::LoadUpvalueOp read : reads) {
                mlir::OpBuilder at(read);
                const mlir::Value undefined = undefinedAt(at, read.getLoc());
                auto made = llvm::cast<ctjs::CreateClosureOp>(at.clone(*inside));
                if (kept) { made.setFunction(*ctjs::functionIndex(cloneDeep(callback))); }
                kept = true;
                made.getEnclosingClosureMutable().assign(own);
                made.getEnclosingThisMutable().assign(undefined);
                made.getUpvaluesMutable().assign(
                    llvm::SmallVector<mlir::Value>(cells.size(), undefined));
                if (!relayed.empty()) {
                    made->setAttr("enclosing_indices",
                                  mlir::Builder(context).getDenseI32ArrayAttr(relayed));
                }
                read.getResult().replaceAllUsesWith(made.getResult());
                read.erase();
                callExecuted(*this, made.getResult(), helpers);
            }
            removeCaptureSlots(holder, {slot});
        }
        for (mlir::Operation * read : llvm::make_early_inc_range(box->getUsers())) {
            read->getResult(0).replaceAllUsesWith(inside.getResult());
            read->erase();
        }
        box.erase();
    }
    callExecuted(*this, inside.getResult(), helpers);
    if (inside.getResult().use_empty()) { inside.erase(); }
    return helpers;
}

} // namespace ctcompile::ctnative::lowering_detail
