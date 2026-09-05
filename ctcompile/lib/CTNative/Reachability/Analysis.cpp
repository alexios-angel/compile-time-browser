#include "Analysis.h"

#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringSwitch.h"

namespace ctcompile::ctnative::reachability {
namespace {

class graph {
public:
    graph(mlir::ModuleOp module, unsigned limit) : module(module), limit(limit) {}

    result run() {
        for (ctjs::FuncOp function : module.getOps<ctjs::FuncOp>()) {
            functions.push_back(function);
        }
        if (inventory() && symbolEdges() && closureEdges()) {
            while (!pending.empty() && output.reason.empty()) {
                auto function = pending.pop_back_val();
                for (ctjs::FuncOp target : edges[function]) {
                    if (!spend()) { break; }
                    mark(target);
                }
            }
        }
        for (ctjs::FuncOp function : functions) {
            if (!output.reason.empty() || live.contains(function)) {
                ++output.retained;
            } else {
                output.dead.push_back(function);
            }
        }
        return std::move(output);
    }

private:
    bool refuse(llvm::StringRef reason) {
        if (output.reason.empty()) { output.reason = reason.str(); }
        return false;
    }

    bool spend() {
        if (output.steps >= limit) { return refuse("reachability scan budget exhausted"); }
        ++output.steps;
        return true;
    }

    void mark(ctjs::FuncOp function) {
        if (live.insert(function).second) { pending.push_back(function); }
    }

    void edge(mlir::Operation * source, ctjs::FuncOp target) {
        auto owner = llvm::dyn_cast<ctjs::FuncOp>(source);
        if (!owner) { owner = source->getParentOfType<ctjs::FuncOp>(); }
        if (owner) {
            edges[owner].push_back(target);
        } else {
            // Module attributes and operations outside function bodies remain
            // executable/exported roots, irrespective of their dialect.
            mark(target);
        }
    }

    bool inventory() {
        module.walk([&](mlir::Operation * op) {
            if (!output.reason.empty()) { return mlir::WalkResult::interrupt(); }
            if (!spend()) { return mlir::WalkResult::interrupt(); }
            if (op != module.getOperation() && op->hasTrait<mlir::OpTrait::SymbolTable>()) {
                refuse("nested symbol tables require a separate reachability proof");
                return mlir::WalkResult::interrupt();
            }
            // Opaque code and other dialects can encode function addresses in
            // strings or custom properties. This pass is deliberately before
            // native/boxed lowering, where CTJS's reference forms are explicit.
            const bool supported = op->getName().isRegistered() &&
                                   llvm::StringSwitch<bool>(op->getName().getDialectNamespace())
                                       .Cases({"builtin", "ctjs", "arith", "scf", "cf", "ub"}, true)
                                       .Default(false);
            if (!supported) {
                refuse("operation has no CTJS reachability contract");
                return mlir::WalkResult::interrupt();
            }
            if (auto function = llvm::dyn_cast<ctjs::FuncOp>(op)) {
                if (function->getParentOp() != module.getOperation()) {
                    refuse("nested functions require a separate reachability proof");
                    return mlir::WalkResult::interrupt();
                }
                const auto index = functionIndex(function);
                if (index) { indexed[*index].push_back(function); }
                if (function.getBody().empty() || index == 0 ||
                    mlir::SymbolTable::getSymbolVisibility(function) !=
                        mlir::SymbolTable::Visibility::Private) {
                    mark(function);
                }
            }
            if (auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(op)) {
                closures.push_back(closure);
            }
            return mlir::WalkResult::advance();
        });
        return output.reason.empty();
    }

    bool symbolEdges() {
        // A symbol table's own attributes and its body have separate scopes in
        // MLIR. Both can contain roots, including nested dictionary/array attrs.
        for (const auto & uses : {mlir::SymbolTable::getSymbolUses(module.getOperation()),
                                  mlir::SymbolTable::getSymbolUses(&module.getBodyRegion())}) {
            if (!uses) { return refuse("symbol uses could not be enumerated"); }
            for (const auto & use : *uses) {
                if (!spend()) { return false; }
                auto target = mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
                    use.getUser(), use.getSymbolRef());
                if (!target) { return refuse("symbol reference has no visible CTJS target"); }
                edge(use.getUser(), target);
            }
        }
        return true;
    }

    bool closureEdges() {
        for (ctjs::CreateClosureOp closure : closures) {
            if (!spend()) { return false; }
            const auto found = indexed.find(static_cast<unsigned>(closure.getFunction()));
            if (closure.getFunction() < 0 || found == indexed.end()) {
                return refuse("numeric closure reference has no visible CTJS target");
            }
            // Never choose one body from an ambiguous numeric identity. The
            // symbol namespace can distinguish them, but the closure cannot.
            for (ctjs::FuncOp target : found->second) {
                if (!spend()) { return false; }
                edge(closure, target);
            }
        }
        return true;
    }

    mlir::ModuleOp module;
    unsigned limit;
    result output;
    llvm::SmallVector<ctjs::FuncOp> functions;
    llvm::SmallVector<ctjs::CreateClosureOp> closures;
    llvm::DenseMap<unsigned, llvm::SmallVector<ctjs::FuncOp>> indexed;
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::FuncOp>> edges;
    llvm::DenseSet<mlir::Operation *> live;
    llvm::SmallVector<ctjs::FuncOp> pending;
};

} // namespace

result analyze(mlir::ModuleOp module, unsigned maxSteps) {
    return graph(module, maxSteps).run();
}

} // namespace ctcompile::ctnative::reachability
