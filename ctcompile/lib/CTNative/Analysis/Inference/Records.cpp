//===- Records.cpp - Object literals returned by value --------------------===//
//
// A data record is an object literal a function builds and returns, whose
// caller only reads and writes its fields through constant keys. Such an
// object is never shared: the callee drops its only reference when it returns,
// and the caller holds the only one afterwards, so copying it into the caller
// (a C++ struct returned by value) observes nothing JavaScript could tell
// apart from passing the reference.
//
// markRecordReturns proves that once per module and writes kRecordReturn on
// the function and on each of its calls. hasClosedShape and groupReceivers
// then treat the call's result as the same closed object as the literals the
// callee returns.
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

namespace ctcompile::ctnative {
namespace {
/// Whether every use of `object` reads or writes one of its fields through a
/// constant key other than `__proto__`, or, when `returned` is set, returns it.
/// Anything else (a call, a store into another object, `===`, a dynamic key)
/// could observe that the record is a copy.
bool onlyFieldAccesses(mlir::Value object, bool returned) {
    for (mlir::OpOperand & use : object.getUses()) {
        mlir::Operation * user = use.getOwner();
        if (returned && llvm::isa<ctjs::ReturnOp>(user)) { continue; }
        const mlir::Value key = ctjs::propertyKey(user);
        const llvm::StringRef name = key ? ctjs::constantKey(key) : llvm::StringRef{};
        if (!key || use.getOperandNumber() != 0 || name.empty() || name == "__proto__") {
            return false;
        }
    }
    return true;
}

/// Whether `value` is a plain object literal made in `fn`: not a method table,
/// not a class instance and not an object with run-time identity, each of
/// which has its own carrier.
bool isPlainLiteral(mlir::Value value, ctjs::FuncOp fn) {
    auto literal = value.getDefiningOp<ctjs::CreateObjectOp>();
    return literal && literal->getParentOfType<ctjs::FuncOp>() == fn &&
           methodTableName(literal).empty() && !literal->hasAttr(kNativeObjectIdentity) &&
           !literal->hasAttr(kNativeClassName) &&
           onlyFieldAccesses(literal.getResult(), /*returned=*/true);
}
} // namespace

void TypeInference::markRecordReturns(mlir::Operation * top) {
    auto module = llvm::dyn_cast<mlir::ModuleOp>(top);
    if (!module) { return; }
    module.walk([&](ctjs::FuncOp fn) {
        if (fn.getBody().empty() || !fn.isPrivate()) { return; }
        bool records = true;
        bool any = false;
        fn.walk([&](ctjs::ReturnOp ret) {
            any = true;
            records &= isPlainLiteral(ret.getValue(), fn);
        });
        if (!records || !any || !closedCallableProblem(fn, module).empty()) { return; }
        // Every call is known, and every caller only touches fields: a call
        // whose result escapes would share what the callee copied.
        const auto callers = directCallers(fn, module);
        if (!callers || callers->empty() || !llvm::all_of(*callers, [](ctjs::CallDirectOp call) {
                return onlyFieldAccesses(call.getResult(), /*returned=*/false);
            })) {
            return;
        }
        const auto marker = mlir::UnitAttr::get(fn.getContext());
        fn->setAttr(kRecordReturn, marker);
        for (ctjs::CallDirectOp call : *callers) { call->setAttr(kRecordReturn, marker); }
    });
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
