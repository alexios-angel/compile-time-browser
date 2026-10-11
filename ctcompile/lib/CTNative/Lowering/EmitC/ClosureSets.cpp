//===- EmitC/ClosureSets.cpp - Closure-set lowering ------------*- C++ -*-===//
//
// Spells a value of the lattice's `closure_set` type, and each closure that
// makes one of its members, as one std::variant of ctnative::bound members
// (Runtime/Callable.hpp), one alternative per target in the set's sorted
// order:
//
//   std::variant<ctnative::bound<fn_2, ctnative::js_num>, ctnative::bound<fn_3>>
//
// A member closure becomes `variant(ctnative::bind<fn_2>(captures...))`. A
// call through the set is the borrowed call's `ctnative::invoke_callable`,
// whose std::variant overload dispatches with std::visit
// (Environments.cpp). ClosureLifting/ClosureSets.cpp says which closures and
// calls these are.
//
//===----------------------------------------------------------------------===//

#include "Emitter.h"

#include "ctcompile/CTNative/Analysis/NativeClosure.h"

namespace ctcompile::ctnative::lowering_detail {

/// `ctnative::bound<f, captures...>` for the member whose lifted function is
/// `target`, read from the closure that makes it.
std::string lowering::closureSetMemberSpelling(llvm::StringRef target) {
    if (const auto found = closureSetMembers.find(target); found != closureSetMembers.end()) {
        return found->second;
    }
    ctjs::CreateClosureOp made;
    module.walk([&](ctjs::CreateClosureOp c) {
        if (c->hasAttr(kNativeClosureSet) && borrowedTarget(c) == target) { made = c; }
    });
    if (!made) {
        llvm::report_fatal_error(llvm::Twine("ctnative lowering: the closure set member `") +
                                 target + "` has no closure that makes it");
    }
    const auto named = names.find(target);
    std::string spelling =
        "ctnative::bound<" + (named == names.end() ? cIdentifier(target) : named->second);
    for (mlir::Value captured : made.getUpvalues()) {
        spelling += ", " + callableTypeSpelling(typeOf(captured));
    }
    spelling += ">";
    closureSetMembers[target] = spelling;
    return spelling;
}

/// The std::variant spelling of a set of `targets`, in the given order.
std::string lowering::closureSetSpelling(llvm::ArrayRef<llvm::StringRef> targets) {
    std::string spelling = "std::variant<";
    for (auto [i, target] : llvm::enumerate(targets)) {
        if (i != 0) { spelling += ", "; }
        spelling += closureSetMemberSpelling(target);
    }
    return spelling + ">";
}

bool lowering::retypeClosureSet(mlir::Value v) {
    // Gathered on the first value of the first function lowered, before any
    // member's closure has been replaced.
    if (!closureSetTargets) {
        closureSetTargets.emplace();
        module.walk([&](ctjs::CreateClosureOp c) {
            if (auto members = c->getAttrOfType<mlir::ArrayAttr>(kNativeClosureSet)) {
                for (auto symbol : members.getAsRange<mlir::FlatSymbolRefAttr>()) {
                    closureSetTargets->insert(symbol.getValue());
                }
                // Spell the member while its closure still exists.
                (void)closureSetMemberSpelling(borrowedTarget(c));
            }
        });
    }
    llvm::SmallVector<llvm::StringRef> targets;
    if (auto made = v.getDefiningOp<ctjs::CreateClosureOp>();
        made && made->hasAttr(kNativeClosureSet)) {
        for (auto symbol : made->getAttrOfType<mlir::ArrayAttr>(kNativeClosureSet)
                               .getAsRange<mlir::FlatSymbolRefAttr>()) {
            targets.push_back(symbol.getValue());
        }
    } else if (auto set = llvm::dyn_cast_or_null<ClosureSetType>(typeOf(v))) {
        // Only a set of closure-set members is this variant. A borrowed
        // parameter that callers fill with different callbacks has a
        // `closure_set` type too, and stays the borrowed function_ref.
        for (mlir::StringAttr target : set.getTargets()) {
            if (!closureSetTargets->contains(target.getValue())) { return false; }
            targets.push_back(target.getValue());
        }
    } else {
        return false;
    }
    v.setType(ec::OpaqueType::get(context, closureSetSpelling(targets)));
    return true;
}

bool lowering::replaceClosureSetMember(mlir::Operation * op) {
    auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(op);
    if (!made || !op->hasAttr(kNativeClosureSet)) { return false; }
    mlir::OpBuilder at(op);
    const auto any = ec::OpaqueType::get(context, "auto");
    const auto target = borrowedTarget(op);
    const auto named = names.find(target);
    const std::string binder =
        "ctnative::bind<" + (named == names.end() ? cIdentifier(target) : named->second) + ">";
    const mlir::Value bound = callValue(at, op->getLoc(), any, binder, made.getUpvalues());
    const mlir::Type set = made.getResult().getType();
    made.getResult().replaceAllUsesWith(
        callValue(at, op->getLoc(), set, opaqueSpelling(set), mlir::ValueRange{bound}));
    eraseIfUnused(op);
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
