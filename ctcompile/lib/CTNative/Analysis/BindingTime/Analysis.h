//===- Analysis.h - Binding-time analysis internals -------------*- C++ -*-===//
//
// Private types of BindingTimeAnalysis (ctcompile/CTNative/Analysis/BindingTime.h):
// the per-value facts, the abstract heap, and the Impl that runs the analysis.
// The Impl's member functions are split across the .cpp files in this
// directory.
//
//===----------------------------------------------------------------------===//
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
/// What the binding-time analysis knows about one SSA value: its domain,
/// whether it is known at compile time (Static) or only at run time (Dynamic),
/// its literal when known, and the heap nodes it may refer to.
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
    /// A compile-time-known primitive, optionally with its literal value.
    static fact staticPrimitive(mlir::Attribute literal = {}) {
        return {kind::primitive, BindingTime::Static, literal, {}};
    }
    bool operator==(const fact &) const = default;
};
/// The abstract state of one heap node (an object, Map, cell or closure),
/// keyed by its creating operation.
struct heap {
    /// Set once the node may have been changed in a way not tracked here.
    bool dynamic = false;
    /// Known named fields.
    llvm::StringMap<fact> fields;
    /// The joined fact of every payload stored into a Map.
    fact contents = fact::staticPrimitive();
    /// Conservatively retained graph edges not represented by ordinary fields:
    /// Map keys and closure captures. Deleted keys may remain in this superset.
    llvm::SmallVector<fact> retained;
};
/// The analysis state at one program point: value facts and the abstract heap.
struct flow {
    llvm::DenseMap<mlir::Value, fact> values;
    llvm::DenseMap<mlir::Operation *, heap> heaps;
};
/// Whether an operation or function may be evaluated at compile time, and why.
struct decision {
    bool eligible = false;
    std::string reason;
};
/// Joins two facts. The result is static only when both inputs are and
/// \p staticControl is true (the join point is decided statically).
fact join(fact left, const fact & right, bool staticControl);
/// The static-control join of \p part over \p nodes: the default fact for none,
/// nullopt as soon as \p part has no fact for a node.
std::optional<fact> joinAcross(llvm::ArrayRef<mlir::Operation *> nodes,
                               llvm::function_ref<const fact *(mlir::Operation *)> part);
/// Whether \p value is a static reference whose every heap node is still
/// tracked and not dynamic.
bool clean(const fact & value, const flow & state);
/// Marks every tracked heap node in \p state dynamic.
void invalidate(flow & state);
} // namespace binding_time_detail

/// The analysis itself. Results are kept per value (`facts`), per operation
/// and function (`decisions`), and per function (`arguments` known at every
/// call site, `returns`, and whether the body is `complete`ly static).
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
