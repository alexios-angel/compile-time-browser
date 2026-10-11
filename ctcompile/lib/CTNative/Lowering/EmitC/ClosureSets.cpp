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

/// The std::variant carrier of `v` when it is one of a proved closure set:
/// a member's closure, or a value (a merge, a field read, a struct field's
/// write) whose type is a set of members. Null otherwise; a borrowed
/// parameter that callers fill with different callbacks has a `closure_set`
/// type too, and stays the borrowed function_ref.
mlir::Type lowering::closureSetCarrierOf(mlir::Value v) {
    // Gathered on the first query, from the shape census or the first value
    // lowered, before any member's closure has been replaced. Every value of
    // a set gets the variant of the whole set, which each member's closure
    // names: a merge of two members has a type naming only those two, and a
    // field holds every member any of its writes stores.
    if (!closureSetGroups) {
        closureSetGroups.emplace();
        module.walk([&](ctjs::CreateClosureOp c) {
            auto members = c->getAttrOfType<mlir::ArrayAttr>(kNativeClosureSet);
            if (!members) { return; }
            llvm::SmallVector<llvm::StringRef> names;
            for (auto symbol : members.getAsRange<mlir::FlatSymbolRefAttr>()) {
                names.push_back(symbol.getValue());
            }
            (*closureSetGroups)[borrowedTarget(c)] = closureSetSpelling(names);
        });
    }
    std::string spelling;
    if (auto made = v.getDefiningOp<ctjs::CreateClosureOp>();
        made && made->hasAttr(kNativeClosureSet)) {
        spelling = closureSetGroups->lookup(borrowedTarget(made));
    } else if (auto set = llvm::dyn_cast_or_null<ClosureSetType>(typeOf(v))) {
        for (mlir::StringAttr target : set.getTargets()) {
            const auto group = closureSetGroups->find(target.getValue());
            if (group == closureSetGroups->end() ||
                (!spelling.empty() && spelling != group->second)) {
                return {};
            }
            spelling = group->second;
        }
    }
    if (spelling.empty()) { return {}; }
    return ec::OpaqueType::get(context, spelling);
}

bool lowering::retypeClosureSet(mlir::Value v) {
    const mlir::Type set = closureSetCarrierOf(v);
    if (set) { v.setType(set); }
    return static_cast<bool>(set);
}

/// Declares each closure-set member's function ahead of the shape classes:
/// a class field of a set's variant names them, and the prototypes that
/// follow the classes come too late for it. A second prototype of the same
/// function is valid C++.
void lowering::declareClosureSetMembers(mlir::OpBuilder & b, llvm::ArrayRef<ec::FuncOp> lowered) {
    if (!closureSetGroups || closureSetGroups->empty()) { return; }
    llvm::StringSet<> members;
    for (const auto & target : *closureSetGroups) {
        const auto named = names.find(target.getKey());
        members.insert(named == names.end() ? cIdentifier(target.getKey()) : named->second);
    }
    for (ec::FuncOp f : lowered) {
        if (!members.contains(f.getSymName())) { continue; }
        ec::DeclareFuncOp::create(b, f.getLoc(),
                                  mlir::FlatSymbolRefAttr::get(context, f.getSymName()));
    }
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
