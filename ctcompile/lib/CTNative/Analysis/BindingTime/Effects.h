//===- Effects.h - Function effect summaries --------------------*- C++ -*-===//
//
// Per-function summaries of which argument-reachable object fields and Maps a
// function reads or writes, used by the binding-time analysis to invalidate
// only what a direct call can change.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

namespace ctcompile::ctnative::binding_time_detail {
struct flow;

/// A path from a function argument through named fields. A path is valid only
/// when the actual argument is a fresh local object and every traversed field
/// is its own data property. It is never a Map schema family or a claim that
/// two allocation sites denote the same runtime object.
struct effectPath {
    unsigned argument = 0;
    llvm::SmallVector<mlir::StringAttr> fields;
    bool operator==(const effectPath &) const = default;
};
/// One read or write a function performs on an object field or a Map reached
/// by `object`; `key` is the field name for object accesses.
struct effectAccess {
    enum class kind {
        objectRead,
        objectWrite,
        mapRead,
        mapWrite
    } action;
    effectPath object;
    mlir::StringAttr key;
    bool writes() const { return action == kind::objectWrite || action == kind::mapWrite; }
    bool operator==(const effectAccess &) const = default;
};
/// Every access a function performs. `known` is false when the function could
/// not be summarized, in which case `accesses` means nothing.
struct effectSummary {
    bool known = false;
    llvm::SmallVector<effectAccess> accesses;
    bool operator==(const effectSummary &) const = default;
};

/// Summaries for every function in a module, and the call-precise
/// invalidation built on them (EffectQuery.cpp).
class effectQueries {
public:
    /// Expects NativeMap facts just rederived by BindingTimeAnalysis. No input
    /// effect summary or user annotation is read as authority.
    explicit effectQueries(mlir::ModuleOp module);
    /// Marks dynamic only the heap nodes \p call can write. Returns false,
    /// changing nothing, when that cannot be proved.
    bool invalidateCall(ctjs::CallDirectOp call, flow & state) const;

private:
    mlir::ModuleOp module;
    bool closedEnvironment = false;
    llvm::DenseMap<mlir::Operation *, effectSummary> summaries;
    effectSummary summarize(ctjs::FuncOp function, unsigned & steps) const;
    /// The known summary of \p call's matched callee when the call passes its
    /// whole entry-argument window, or null.
    const effectSummary * knownSummary(ctjs::CallDirectOp call) const;
};
/// Visits functions callees first, revisiting a function's callers whenever
/// \p visit reports a changed summary, until nothing is pending or N*(N+1)
/// visits are spent (N = number of functions). Returns true when the worklist
/// emptied.
bool solveCallerFixpoint(mlir::ModuleOp module, llvm::function_ref<bool(ctjs::FuncOp)> visit);
/// Whether every operation in \p module is on an allow-list that cannot reach
/// accessors, reflection or host code, so summaries can be trusted.
bool closedEffectEnvironment(mlir::ModuleOp module);
/// False for property names that reach prototype or accessor machinery.
bool safeEffectKey(llvm::StringRef name);
/// Whether \p call's callee value provably is its symbolic target.
bool effectCalleeMatches(ctjs::CallDirectOp call, mlir::ModuleOp module);
} // namespace ctcompile::ctnative::binding_time_detail
