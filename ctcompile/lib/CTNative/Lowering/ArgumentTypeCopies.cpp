//===- ArgumentTypeCopies.cpp - One copy of a function per argument type --===//
//
// A function called directly with values of different types, such as
// Bootstrap's toType called with a Number at one site and a String at
// another, has a parameter whose type is their union. A union of a String with
// another kind has only a String-union carrier, which tests its tag at every
// use, or none. When every call site passes a value of
// one exact type, the function is copied once per exact type instead: each
// call goes to the copy for its argument's type, so each copy's parameter has
// one type and the ordinary per-type helpers apply. The calls of the first
// type stay on the original. Each copy is recorded in ctnative.copies, so a
// count of source functions can be rebuilt after lowering.
//
// A call that passes a union itself is a real merge: no copy can give it one
// type, so its function is not copied.
//
//===----------------------------------------------------------------------===//

#include "ClosureLifting/ClassInheritance.h"
#include "LoweringSupport.h"
#include "ctcompile/CTNative/IR/CTNativeAttrNames.h"

#include "llvm/ADT/MapVector.h"

#include <string>

namespace ctcompile::ctnative::lowering_detail {

namespace {

/// Whether a value of `type` needs a String-union carrier, or has no carrier:
/// the parameter types a copy per argument type replaces.
bool needsUnion(mlir::Type type) {
    const carrier held = carrierOf(type);
    return held == carrier::none || held == carrier::numberString ||
           held == carrier::nullableNumberString || held == carrier::booleanString ||
           held == carrier::nullableBooleanString;
}

} // namespace

bool copyPerArgumentType(mlir::ModuleOp module,
                         llvm::function_ref<bool(ctjs::FuncOp)> keepsOriginal) {
    // Only a private function whose every use is a direct call of it: no
    // closure or other reference can pass it values the calls do not show.
    llvm::MapVector<mlir::Operation *, llvm::SmallVector<ctjs::CallDirectOp>> callers;
    module.walk([&](ctjs::CallDirectOp call) {
        if (ctjs::FuncOp target = call.getTarget()) {
            callers[target.getOperation()].push_back(call);
        }
    });
    // A closure the lift marked lifted is never called: its calls became the
    // direct calls above. Any other closure of a function can be called with
    // values no call shows.
    const auto lifted = [](ctjs::CreateClosureOp made) { return made->hasAttr(kNativeLifted); };
    llvm::DenseSet<unsigned> closed;
    module.walk([&](ctjs::CreateClosureOp made) {
        if (!lifted(made)) { closed.insert(made.getFunction()); }
    });
    llvm::SmallVector<ctjs::FuncOp> candidates;
    for (auto & [op, calls] : callers) {
        auto fn = llvm::cast<ctjs::FuncOp>(op);
        const std::optional<unsigned> index = ctjs::functionIndex(fn);
        if (calls.size() < 2 || !fn.isPrivate() || fn.getBody().empty() || !index ||
            closed.contains(*index) || keepsOriginal(fn)) {
            continue;
        }
        auto uses = mlir::SymbolTable::getSymbolUses(fn, module.getOperation());
        if (!uses || llvm::any_of(*uses, [](const mlir::SymbolTable::SymbolUse & use) {
                return !llvm::isa<ctjs::CallDirectOp>(use.getUser());
            })) {
            continue;
        }
        // A closure made inside would need a copy of its own.
        if (fn->walk([&](ctjs::CreateClosureOp made) {
                  return lifted(made) ? mlir::WalkResult::advance() : mlir::WalkResult::interrupt();
              }).wasInterrupted()) {
            continue;
        }
        candidates.push_back(fn);
    }
    if (candidates.empty()) { return false; }

    // The types every call site passes, from a solve of the module as it is.
    mlir::DataFlowSolver solver;
    solver.load<mlir::dataflow::DeadCodeAnalysis>();
    solver.load<mlir::dataflow::SparseConstantPropagation>();
    solver.load<TypeInference>(nullptr, nullptr);
    if (mlir::failed(solver.initializeAndRun(module))) { return false; }
    const auto typeOf = [&](mlir::Value value) -> mlir::Type {
        const TypeLattice * lattice = solver.lookupState<TypeLattice>(value);
        return lattice ? lattice->getValue().getType() : mlir::Type{};
    };

    unsigned nextIndex = 0;
    module.walk([&](ctjs::FuncOp fn) {
        if (const std::optional<unsigned> index = ctjs::functionIndex(fn)) {
            nextIndex = std::max(nextIndex, *index + 1);
        }
    });
    auto earlier = module->getAttrOfType<mlir::DictionaryAttr>(kNativeCopies);
    llvm::SmallVector<mlir::NamedAttribute> copies;
    if (earlier) { copies.append(earlier.begin(), earlier.end()); }
    bool changed = false;
    for (ctjs::FuncOp fn : candidates) {
        mlir::Block & entry = fn.getBody().front();
        // The parameters whose type is a union.
        llvm::SmallVector<unsigned> mixed;
        for (unsigned i = ctjs::implicit_arguments; i < entry.getNumArguments(); ++i) {
            if (!entry.getArgument(i).use_empty() && needsUnion(typeOf(entry.getArgument(i)))) {
                mixed.push_back(i);
            }
        }
        if (mixed.empty()) { continue; }
        // The calls grouped by the carrier of each of those arguments. A call
        // that passes a union, or a value with no type, is a real merge.
        llvm::SmallVector<std::pair<std::string, llvm::SmallVector<ctjs::CallDirectOp>>> byType;
        bool merged = false;
        for (ctjs::CallDirectOp call : callers[fn.getOperation()]) {
            std::string key;
            for (unsigned i : mixed) {
                const mlir::Type passed = typeOf(call->getOperand(i));
                merged |= !passed || needsUnion(passed);
                key.push_back(static_cast<char>(carrierOf(passed)));
            }
            auto group = llvm::find_if(byType, [&](const auto & row) { return row.first == key; });
            if (group == byType.end()) {
                byType.push_back({key, {}});
                group = std::prev(byType.end());
            }
            group->second.push_back(call);
        }
        if (merged || byType.size() < 2) { continue; }
        const llvm::StringRef name = fn.getSymName();
        // A copy of a copy counts toward the function the first was copied from.
        mlir::StringAttr source = fn.getSymNameAttr();
        if (earlier) {
            if (auto from = earlier.getAs<mlir::StringAttr>(name)) { source = from; }
        }
        for (auto & [key, calls] : llvm::drop_begin(byType)) {
            auto copy = llvm::cast<ctjs::FuncOp>(fn->clone());
            copy.setSymName(
                (name.substr(0, name.rfind('$')) + "$" + llvm::Twine(nextIndex++)).str());
            module.getBody()->getOperations().insertAfter(fn->getIterator(), copy);
            for (ctjs::CallDirectOp call : calls) {
                call.setCalleeAttr(mlir::FlatSymbolRefAttr::get(copy.getSymNameAttr()));
            }
            copies.emplace_back(copy.getSymNameAttr(), source);
        }
        changed = true;
    }
    if (changed) {
        module->setAttr(kNativeCopies, mlir::DictionaryAttr::get(module.getContext(), copies));
    }
    return changed;
}

} // namespace ctcompile::ctnative::lowering_detail
