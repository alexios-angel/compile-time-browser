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
    bool operator==(const effectPath & other) const {
        return argument == other.argument && fields == other.fields;
    }
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
    bool operator==(const effectAccess & other) const {
        return action == other.action && object == other.object && key == other.key;
    }
};
struct effectSummary {
    bool known = false;
    llvm::SmallVector<effectAccess> accesses;
    bool operator==(const effectSummary & other) const {
        return known == other.known && accesses == other.accesses;
    }
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
};
bool closedEffectEnvironment(mlir::ModuleOp module);
bool safeEffectKey(llvm::StringRef name);
bool effectCalleeMatches(ctjs::CallDirectOp call, mlir::ModuleOp module);
} // namespace ctcompile::ctnative::binding_time_detail
