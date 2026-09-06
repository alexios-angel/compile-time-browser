#include "SourceNames.h"

#include "mlir/Dialect/EmitC/IR/EmitC.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"

namespace ctcompile::cpp {

void SourceNames::prepare(mlir::Operation * function,
                          llvm::function_ref<bool(mlir::Value)> materialized,
                          llvm::ArrayRef<std::string> parameters) {
    names.clear();
    unavailable.clear();
    nextTemporary = 0;
    const auto module = function->getParentOfType<mlir::ModuleOp>();
    // A callable's fixed capture names also need collision-free locals when
    // a hand-written EmitC module has not requested general source naming.
    active = !parameters.empty() ||
             (module && module->hasAttrOfType<mlir::UnitAttr>("ctnative.readable_names"));
    if (!active) { return; }
    mlir::Operation * root = function;
    while (root->getParentOp()) { root = root->getParentOp(); }
    if (root != reservedRoot) {
        moduleIdentifiers.clear();
        reserveCppIdentifiers(root, moduleIdentifiers);
        reservedRoot = root;
    }
    for (const auto & name : moduleIdentifiers) { unavailable.insert(name.getKey()); }
    if (!parameters.empty()) {
        const auto arguments = function->getRegion(0).front().getArguments();
        assert(parameters.size() == arguments.size());
        for (auto [argument, name] : llvm::zip(arguments, parameters)) {
            names[argument] = name;
            unavailable.insert(name);
        }
    }
    const auto hints = inferSourceNames(function);

    struct family {
        std::string base;
        llvm::SmallVector<mlir::Value> values;
    };
    llvm::SmallVector<family> families;
    llvm::StringMap<unsigned> indices;
    llvm::StringSet<> bases;
    const auto collect = [&](mlir::Value value) {
        if (names.contains(value)) { return; }
        auto found = hints.find(value);
        if (found == hints.end() || found->second.empty() || !materialized(value)) { return; }
        std::string base = localIdentifier(found->second);
        auto [position, inserted] = indices.try_emplace(base, families.size());
        if (inserted) {
            bases.insert(base);
            families.push_back({std::move(base), {}});
        }
        families[position->second].values.push_back(value);
    };
    function->walk<mlir::WalkOrder::PreOrder>([&](mlir::Operation * op) {
        for (mlir::Value result : op->getResults()) { collect(result); }
        for (mlir::Region & region : op->getRegions()) {
            for (mlir::Block & block : region) {
                for (mlir::BlockArgument argument : block.getArguments()) { collect(argument); }
            }
        }
    });
    llvm::SmallVector<mlir::Value> returned;
    function->walk([&](mlir::emitc::ReturnOp ret) {
        if (ret.getOperand()) { returned.push_back(ret.getOperand()); }
    });
    for (family & group : families) {
        // The first binding and its returned snapshot get the first numbers;
        // branch storage and expression intermediates follow in source order.
        llvm::SmallVector<mlir::Value> ordered{group.values.front()};
        llvm::DenseSet<mlir::Value> members(group.values.begin(), group.values.end());
        llvm::DenseSet<mlir::Value> orderedValues{group.values.front()};
        for (mlir::Value value : returned) {
            if (members.contains(value) && orderedValues.insert(value).second) {
                ordered.push_back(value);
            }
        }
        for (mlir::Value value : group.values) {
            if (orderedValues.insert(value).second) { ordered.push_back(value); }
        }
        unsigned version = 0;
        for (mlir::Value value : ordered) {
            std::string candidate = group.base;
            if (ordered.size() > 1 || unavailable.contains(candidate)) {
                do {
                    candidate = group.base + (group.base.back() == '_' ? "" : "_") +
                                std::to_string(++version);
                } while (unavailable.contains(candidate) || bases.contains(candidate));
            }
            unavailable.insert(candidate);
            names[value] = std::move(candidate);
        }
    }
}

llvm::StringRef SourceNames::get(mlir::Value value, llvm::StringRef fallback) {
    auto found = names.find(value);
    if (found != names.end()) { return found->second; }
    std::string candidate;
    do {
        candidate = fallback.str() + std::to_string(++nextTemporary);
    } while (unavailable.contains(candidate));
    unavailable.insert(candidate);
    return names.try_emplace(value, std::move(candidate)).first->second;
}

} // namespace ctcompile::cpp
