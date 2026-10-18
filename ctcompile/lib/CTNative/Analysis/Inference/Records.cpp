//===- Records.cpp - Object literals returned by value --------------------===//
//
// A data record is an object literal a function builds and returns, whose
// callers only read and write its fields through constant keys, call its lifted
// methods, or lend it to a parameter that does the same. A class instance
// qualifies: the constructor lift made it a literal, and its constructor and
// methods take it as their receiver. Such an object is never shared beyond
// those frames: the callee drops its only reference when it returns, and the
// caller holds the only one afterwards, so copying it into the caller (a C++
// struct returned by value) observes nothing JavaScript could tell apart from
// passing the reference. A parameter it is lent to receives a pointer to the
// caller's copy, which is a reference with the same meaning.
//
// markRecordReturns proves both once per module. It writes kRecordReturn on a
// returning function and on each of its calls, and adds a record parameter's
// index to the existing object-parameter list (ctnative.object_args) on the
// function and its calls, so the pointer passing, the alias groups and the
// admission checks already written for object parameters apply unchanged.
//
//===----------------------------------------------------------------------===//
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Analysis/NativeClosure.h"
#include "ctcompile/CTNative/Analysis/NativeObjectIdentity.h"
#include "ctcompile/CTNative/Analysis/TypeInference.h"

#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/IR/CTNativeAttrNames.h"

#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"

namespace ctcompile::ctnative {
namespace {
/// Whether `use` reads or writes a field of the object it names through a
/// constant key other than `__proto__`.
bool isFieldAccess(mlir::OpOperand & use) {
    const mlir::Value key = ctjs::propertyKey(use.getOwner());
    const llvm::StringRef name = key ? ctjs::constantKey(key) : llvm::StringRef{};
    return key && use.getOperandNumber() == 0 && !name.empty() && name != "__proto__";
}

/// Whether `use` is the receiver of a lifted constructor or method call. The
/// closure lift proved that callee reaches its receiver only through constant
/// keys or as the receiver of another such call, so it keeps no reference.
bool isReceiver(mlir::OpOperand & use) {
    return llvm::isa<ctjs::CallDirectOp>(use.getOwner()) && use.getOperandNumber() == 0 &&
           use.getOwner()->hasAttr(kNativeReceiver);
}

/// Whether `value` is an object literal made in `fn`, used only through
/// constant keys, as a lifted call's receiver, and returned: not a method
/// table and not an object with run-time identity, each of which has its own
/// carrier. A class instance (kNativeClassName) qualifies: the lifted `new`
/// made it a literal whose constructor and methods take it as receiver.
bool isPlainLiteral(mlir::Value value, ctjs::FuncOp fn) {
    auto literal = value.getDefiningOp<ctjs::CreateObjectOp>();
    if (!literal || literal->getParentOfType<ctjs::FuncOp>() != fn ||
        !methodTableName(literal).empty() || literal->hasAttr(kNativeObjectIdentity)) {
        return false;
    }
    return llvm::all_of(literal.getResult().getUses(), [](mlir::OpOperand & use) {
        return isFieldAccess(use) || isReceiver(use) || llvm::isa<ctjs::ReturnOp>(use.getOwner());
    });
}

/// The direct calls of `fn` when it is private, has a body, its closure does
/// not escape and every symbol use is a direct call; none otherwise.
std::optional<llvm::SmallVector<ctjs::CallDirectOp>> knownCallers(ctjs::FuncOp fn,
                                                                  mlir::ModuleOp module) {
    if (fn.getBody().empty() || !fn.isPrivate() || !closedCallableProblem(fn, module).empty()) {
        return std::nullopt;
    }
    auto callers = directCallers(fn, module);
    if (!callers || callers->empty()) { return std::nullopt; }
    return callers;
}

using Slot = std::pair<mlir::Operation *, unsigned>; ///< A function and an entry index.
} // namespace

void TypeInference::markRecordReturns(mlir::Operation * top) {
    auto module = llvm::dyn_cast<mlir::ModuleOp>(top);
    if (!module) { return; }
    // Candidates, before their uses are checked against each other.
    llvm::MapVector<mlir::Operation *, llvm::SmallVector<ctjs::CallDirectOp>> returning;
    llvm::MapVector<Slot, llvm::SmallVector<ctjs::CallDirectOp>> slots;
    module.walk([&](ctjs::FuncOp fn) {
        auto callers = knownCallers(fn, module);
        if (!callers) { return; }
        bool records = true;
        bool any = false;
        fn.walk([&](ctjs::ReturnOp ret) {
            any = true;
            records &= isPlainLiteral(ret.getValue(), fn);
        });
        if (records && any) { returning[fn] = *callers; }
        mlir::Block & entry = fn.getBody().front();
        for (unsigned i = 3; i < entry.getNumArguments(); ++i) {
            if (listsSlot(fn, kNativeObjectArgs, i) || listsSlot(fn, kNativeCellArgs, i) ||
                entry.getArgument(i).use_empty()) {
                continue;
            }
            slots[{fn.getOperation(), i}] = *callers;
        }
    });
    // A record value is a returning function's call result or a record
    // parameter. Each of its uses must be a field access, the receiver of a
    // lifted call, or a lend to a record parameter; anything else could keep
    // or compare the reference.
    const auto lendsToSlot = [&](mlir::OpOperand & use) {
        auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
        return call && use.getOperandNumber() >= 3 &&
               slots.contains({call.getTarget().getOperation(), use.getOperandNumber()});
    };
    const auto onlyRecordUses = [&](mlir::Value value) {
        return llvm::all_of(value.getUses(), [&](mlir::OpOperand & use) {
            return isFieldAccess(use) || isReceiver(use) || lendsToSlot(use);
        });
    };
    const auto isRecordValue = [&](mlir::Value value) {
        if (auto call = value.getDefiningOp<ctjs::CallDirectOp>()) {
            return returning.contains(call.getTarget().getOperation());
        }
        auto argument = llvm::dyn_cast<mlir::BlockArgument>(value);
        return argument && argument.getOwner()->isEntryBlock() &&
               slots.contains({argument.getOwner()->getParentOp(), argument.getArgNumber()});
    };
    // The greatest fixpoint: drop a candidate that fails, until none does.
    for (bool changed = true; changed;) {
        changed = false;
        for (auto it = returning.begin(); it != returning.end();) {
            if (llvm::all_of(it->second, [&](ctjs::CallDirectOp call) {
                    return onlyRecordUses(call.getResult());
                })) {
                ++it;
                continue;
            }
            it = returning.erase(it);
            changed = true;
        }
        for (auto it = slots.begin(); it != slots.end();) {
            const auto [fn, index] = it->first;
            const mlir::Value parameter =
                llvm::cast<ctjs::FuncOp>(fn).getBody().front().getArgument(index);
            if (onlyRecordUses(parameter) &&
                llvm::all_of(it->second, [&, index = index](ctjs::CallDirectOp call) {
                    return isRecordValue(call->getOperand(index));
                })) {
                ++it;
                continue;
            }
            it = slots.erase(it);
            changed = true;
        }
    }
    const auto marker = mlir::UnitAttr::get(module.getContext());
    for (const auto & [fn, callers] : returning) {
        fn->setAttr(kRecordReturn, marker);
        for (ctjs::CallDirectOp call : callers) { call->setAttr(kRecordReturn, marker); }
    }
    const auto lend = [&](mlir::Operation * op, unsigned index) {
        llvm::SmallVector<int32_t> listed(listedSlots(op, kNativeObjectArgs));
        listed.push_back(static_cast<int32_t>(index));
        llvm::sort(listed);
        op->setAttr(kNativeObjectArgs, mlir::DenseI32ArrayAttr::get(op->getContext(), listed));
    };
    for (const auto & [slot, callers] : slots) {
        lend(slot.first, slot.second);
        for (ctjs::CallDirectOp call : callers) { lend(call, slot.second); }
    }
}

bool TypeInference::isRecordCall(mlir::Value value) {
    auto call = value.getDefiningOp<ctjs::CallDirectOp>();
    return call && call->hasAttr(kRecordReturn);
}

bool TypeInference::returnsRecord(mlir::Operation * ret) {
    auto fn = llvm::isa<ctjs::ReturnOp>(ret) ? ret->getParentOfType<ctjs::FuncOp>() : nullptr;
    return fn && fn->hasAttr(kRecordReturn);
}

} // namespace ctcompile::ctnative
