#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

namespace ctcompile::ctnative::binding_time_detail {
struct flow;

// A path is conditional on the actual argument being a fresh local object and
// every traversed field being its own data property. It is never a Map schema
// family or a claim that two allocation sites denote the same runtime object.
struct effectPath {
    unsigned argument = 0;
    llvm::SmallVector<mlir::StringAttr> fields;
    bool operator==(const effectPath &) const = default;
};
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
struct effectSummary {
    bool known = false;
    llvm::SmallVector<effectAccess> accesses;
    bool operator==(const effectSummary &) const = default;
};

class effectQueries {
public:
    // NativeMap facts have just been rederived by BindingTimeAnalysis. No input
    // effect summary or user annotation is read as authority.
    explicit effectQueries(mlir::ModuleOp module);
    bool invalidateCall(ctjs::CallDirectOp call, flow & state) const;

private:
    mlir::ModuleOp module;
    bool closedEnvironment = false;
    llvm::DenseMap<mlir::Operation *, effectSummary> summaries;
    effectSummary summarize(ctjs::FuncOp function, unsigned & steps) const;
    // The known summary of `call`'s matched callee when the call passes its
    // whole entry-argument window, or null.
    const effectSummary * knownSummary(ctjs::CallDirectOp call) const;
};
// Visits functions callees first, revisiting a function's callers whenever
// `visit` reports a changed summary, until nothing is pending or the former
// N+1 whole-module sweeps' N*(N+1) visits are spent. Whether nothing was left.
bool solveCallerFixpoint(mlir::ModuleOp module, llvm::function_ref<bool(ctjs::FuncOp)> visit);
bool closedEffectEnvironment(mlir::ModuleOp module);
bool safeEffectKey(llvm::StringRef name);
bool effectCalleeMatches(ctjs::CallDirectOp call, mlir::ModuleOp module);
} // namespace ctcompile::ctnative::binding_time_detail
