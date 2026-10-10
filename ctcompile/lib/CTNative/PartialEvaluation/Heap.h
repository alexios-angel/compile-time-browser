//===- Heap.h - Partial evaluator values and heap ---------------*- C++ -*-===//
//
// The abstract heap and evaluator behind --ctnative-partial-evaluate. The
// evaluator runs a function on known arguments over a bounded heap of
// objects, Maps, cells and closures, and returns a snapshot that residualize()
// writes back as IR.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"

#include <optional>
#include <string>
#include <vector>

namespace ctcompile::ctnative::partial_eval {

/// An abstract value: unknown, a primitive constant, a reference to a heap
/// node, the Map constructor, or a Map method bound to its receiver node.
struct value {
    enum class kind {
        unknown,
        constant,
        reference,
        mapConstructor,
        method
    } tag = kind::unknown;
    mlir::Attribute constant;
    unsigned node = 0;
    std::string method;
    static value primitive(mlir::Attribute attr) {
        return {kind::constant, attr, 0, {}};
    }
    static value reference(unsigned id) {
        return {kind::reference, {}, id, {}};
    }
    /// A primitive or heap reference, as opposed to unknown or a callee marker.
    bool known() const { return tag == kind::constant || tag == kind::reference; }
};

/// One heap allocation. Objects and Maps use `entries` (in insertion order);
/// a cell uses `contents`, `requiresWrite` (the source assigns it after
/// creation) and `assigned`; a closure uses `function` and `captures`.
struct node {
    enum class kind {
        object,
        map,
        cell,
        closure
    } tag;
    mlir::Location location;
    std::vector<std::pair<value, value>> entries;
    value contents;
    llvm::SmallVector<value> captures;
    unsigned function = 0;
    bool requiresWrite = false;
    bool assigned = false;
    node(kind tag, mlir::Location location) : tag(tag), location(location) {}
};

/// The evaluated heap and result of one run.
struct snapshot {
    std::vector<node> heap;
    value result;
    unsigned steps = 0;
    /// In a prefix snapshot, the first operation that was not evaluated; it
    /// and everything after it are kept. Null for a whole-function snapshot.
    /// Each binding names a value defined by the removed prefix and used by
    /// the kept code, including primitive copies of data that is mutated later.
    mlir::Operation * boundary = nullptr;
    llvm::SmallVector<std::pair<mlir::Value, value>> bindings;
};

/// Strict equality of two known values, or SameValueZero when `mapKey` is set.
bool sameValue(value left, value right, bool mapKey = false);
std::optional<bool> truthy(value input);
value numeric(mlir::MLIRContext * context, double input);
value boolean(mlir::MLIRContext * context, bool input);
/// The primitive operators below fold through the script runtime; each
/// returns an unknown value when it cannot fold exactly.
value binary(ctjs::BinaryKind kind, value left, value right, mlir::MLIRContext * context,
             bool staticConversions = false);
value compare(ctjs::CompareKind kind, value left, value right, mlir::MLIRContext * context);
value unary(ctjs::UnaryKind kind, value input, mlir::MLIRContext * context);
/// An IntegerAttr or ctjs Boolean literal as a constant of `type`, or null.
mlir::IntegerAttr integerConstant(mlir::IntegerType type, mlir::Attribute literal);

/// Interprets ctjs functions within step, heap-node and call-depth budgets. A
/// failed run returns nullopt and reason() says why.
class evaluator {
public:
    evaluator(mlir::ModuleOp module, unsigned steps, unsigned nodes, unsigned depth)
        : module(module), maxSteps(steps), maxNodes(nodes), maxDepth(depth) {}
    /// Evaluates the whole of `function`.
    std::optional<snapshot> run(ctjs::FuncOp function, llvm::ArrayRef<value> args);
    /// Evaluates only the leading operations of the entry block for which
    /// `isStatic` holds (see Prefix.cpp).
    std::optional<snapshot> runPrefix(ctjs::FuncOp function, llvm::ArrayRef<value> args,
                                      llvm::function_ref<bool(mlir::Operation *)> isStatic);
    const std::string & reason() const { return problem; }

private:
    struct completion {
        enum class kind {
            failed,
            returned,
            yielded,
            condition
        } tag = kind::failed;
        llvm::SmallVector<value> values;
        bool condition = false;
    };
    mlir::ModuleOp module;
    unsigned maxSteps, maxNodes, maxDepth;
    snapshot state;
    std::string problem;
    using environment = llvm::DenseMap<mlir::Value, value>;
    value fail(llvm::StringRef reason);
    bool isNode(value input, node::kind kind) const {
        return input.tag == value::kind::reference && state.heap[input.node].tag == kind;
    }
    /// fail() for a completion or a whole attempt: the result is discarded.
    completion halt(llvm::StringRef reason) {
        fail(reason);
        return {};
    }
    std::nullopt_t refuse(llvm::StringRef reason) {
        fail(reason);
        return std::nullopt;
    }
    value allocate(node::kind kind, mlir::Location location);
    /// Fails unless each of the first `count` implicit entry arguments is used
    /// only as a factory closure parameter.
    bool implicitArgumentsUnobserved(mlir::Block & entry, size_t count);
    value call(ctjs::FuncOp function, llvm::ArrayRef<value> args, unsigned depth);
    value directCall(ctjs::CallDirectOp invoked, environment & env, unsigned depth);
    completion region(mlir::Region & region, llvm::ArrayRef<value> args, environment & env,
                      unsigned depth);
    value operation(mlir::Operation * op, environment & env, unsigned depth);
    value closureOperation(mlir::Operation * op, environment & env);
};

/// The heap nodes reachable from the snapshot's result (or prefix bindings),
/// or nullopt for a cycle or an unsupported value. No IR changes until this
/// check has succeeded.
std::optional<std::vector<unsigned>> reachable(const snapshot & state);
/// Replaces `function`'s body (or evaluated prefix) with IR that rebuilds the
/// `live` nodes of `state`.
void residualize(ctjs::FuncOp function, const snapshot & state, llvm::ArrayRef<unsigned> live);
bool retainedPrefixScaffolding(mlir::Operation * op);
bool factoryParameterUse(mlir::OpOperand & use, unsigned parameter);

} // namespace ctcompile::ctnative::partial_eval
