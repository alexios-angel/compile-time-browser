#pragma once

#include "Analysis.h"
#include "ctcompile/CTNative/Analysis/HostPrefix.h"

namespace ctcompile::ctnative::host_detail {

struct prefixValue {
    enum class Kind {
        unknown,
        primitive,
        object,
        realm,
        resource,
        closure,
        absent
    } kind = Kind::unknown;
    mlir::Attribute literal;
    ctjs::CreateClosureOp made;
    unsigned object = 0;

    static prefixValue constant(mlir::Attribute value) { return {Kind::primitive, value, {}, 0}; }
};

struct prefixAnalysis {
    mlir::ModuleOp module;
    const HostContract & contract;
    unsigned remaining;
    bool followPublication;
    unsigned operationCount = 0;
    bool exhausted = false;
    bool reflective = false;
    std::string refusal, boundary;
    std::vector<HostPrefixBranch> branches;
    std::vector<HostPrefixCall> calls;
    std::vector<HostPrefixFactory> factories;
    std::vector<HostPrefixPublication> publications;
    llvm::DenseMap<unsigned, unsigned> factoryTables;
    llvm::DenseSet<mlir::Operation *> factoryClosures;
    llvm::DenseMap<unsigned, ctjs::FuncOp> functions;
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::CallDirectOp>> callers;
    llvm::DenseMap<mlir::Operation *, unsigned> creations;
    llvm::DenseSet<mlir::Operation *> visited;
    llvm::DenseSet<mlir::Operation *> pendingNewTarget;
    llvm::StringMap<prefixValue> globals;
    llvm::StringMap<llvm::SmallVector<ctjs::StoreGlobalOp>> initializers;
    mlir::DominanceInfo dominance;
    llvm::DenseMap<mlir::Value, prefixValue> observedValues;
    std::vector<llvm::StringMap<prefixValue>> objects;
    using environment = llvm::DenseMap<mlir::Value, prefixValue>;

    struct completion {
        enum class Kind {
            stopped,
            returned,
            yielded
        } kind = Kind::stopped;
        llvm::SmallVector<prefixValue> values;
    };

    prefixAnalysis(mlir::ModuleOp module, const HostContract & contract, unsigned maxSteps,
                   bool followPublication);
    bool step();
    bool spend(unsigned count);
    prefixValue stop(mlir::Operation * operation, llvm::StringRef reason);
    ctjs::FuncOp target(prefixValue value);
    bool uniqueContext(ctjs::FuncOp function, ctjs::CallDirectOp call,
                       ctjs::CreateClosureOp producer);
    bool initializedGlobal(ctjs::LoadGlobalOp load);
    bool identitySafeFunction(ctjs::FuncOp function, llvm::DenseSet<mlir::Operation *> & stack);
    bool identitySafeRegion(mlir::Region & region, llvm::DenseSet<mlir::Operation *> & stack);
    prefixValue factory(ctjs::CallOp call, ctjs::FuncOp function);
    void publication(ctjs::SetPropertyOp write, prefixValue owner, prefixValue value);
    completion function(ctjs::FuncOp function, llvm::ArrayRef<prefixValue> arguments,
                        unsigned depth);
    completion region(mlir::Region & region, environment & values, unsigned depth);
    prefixValue operation(mlir::Operation * operation, environment & values, unsigned depth);
};

std::optional<bool> prefixTruth(prefixValue value);
prefixValue prefixUnary(ctjs::UnaryKind kind, prefixValue operand, mlir::MLIRContext * context);
prefixValue prefixCompare(ctjs::CompareKind kind, prefixValue left, prefixValue right,
                          mlir::MLIRContext * context);

} // namespace ctcompile::ctnative::host_detail
