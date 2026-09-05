#pragma once
#include "Effects.h"
#include "ctcompile/CTNative/Analysis/BindingTime.h"
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Analysis/NativeMap.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include <string>

namespace ctcompile::ctnative {
namespace binding_time_detail {
struct fact {
    enum class kind {
        primitive,
        object,
        map,
        cell,
        closure,
        constructor,
        method,
        bookkeeping,
        unknown
    } domain = kind::unknown;
    BindingTime time = BindingTime::Dynamic;
    mlir::Attribute literal;
    llvm::SmallVector<mlir::Operation *> nodes;
};
struct heap {
    bool dynamic = false;
    llvm::StringMap<fact> fields;
    fact contents{fact::kind::primitive, BindingTime::Static, {}, {}};
    // Conservatively retained graph edges not represented by ordinary fields:
    // Map keys and closure captures. Deleted keys may remain in this superset.
    llvm::SmallVector<fact> retained;
};
struct flow {
    llvm::DenseMap<mlir::Value, fact> values;
    llvm::DenseMap<mlir::Operation *, heap> heaps;
};
struct decision {
    bool eligible = false;
    std::string reason;
};
fact join(fact left, const fact & right, bool staticControl);
void invalidate(flow & state);
} // namespace binding_time_detail

struct BindingTimeAnalysis::Impl {
    using fact = binding_time_detail::fact;
    using flow = binding_time_detail::flow;
    using decision = binding_time_detail::decision;
    mlir::ModuleOp module;
    llvm::DenseMap<mlir::Value, fact> facts;
    llvm::DenseMap<mlir::Operation *, decision> decisions;
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<mlir::Attribute>> arguments;
    llvm::DenseMap<mlir::Operation *, fact> returns;
    llvm::DenseMap<mlir::Operation *, bool> complete;
    std::unique_ptr<binding_time_detail::effectQueries> effects;
    Impl(mlir::ModuleOp module, llvm::function_ref<void(mlir::ModuleOp)> prepareHeapFacts);
    void seedArguments();
    void solveSummaries();
    void analyze(ctjs::FuncOp function);
    bool region(mlir::Region & body, flow & state, bool staticControl,
                llvm::ArrayRef<fact> inputs = {});
    bool operation(mlir::Operation * op, flow & state, bool staticControl);
    fact scalar(mlir::Operation * op, flow & state);
    fact memory(mlir::Operation * op, flow & state, bool staticControl, bool & eligible);
};
} // namespace ctcompile::ctnative
