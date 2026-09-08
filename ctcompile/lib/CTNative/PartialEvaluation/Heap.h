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
    static value primitive(mlir::Attribute attr) { return {kind::constant, attr, 0, {}}; }
    static value reference(unsigned id) { return {kind::reference, {}, id, {}}; }
};

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

struct snapshot {
    std::vector<node> heap;
    value result;
    unsigned steps = 0;
    // A prefix snapshot retains this operation and everything after it. Each
    // binding names a value defined by the removed prefix and used by that
    // retained code, including primitive snapshots of subsequently mutable data.
    mlir::Operation * boundary = nullptr;
    llvm::SmallVector<std::pair<mlir::Value, value>> bindings;
};

bool sameValue(value left, value right, bool mapKey = false);
std::optional<bool> truthy(value input);
value numeric(mlir::MLIRContext * context, double input);
value boolean(mlir::MLIRContext * context, bool input);
value binary(ctjs::BinaryKind kind, value left, value right, mlir::MLIRContext * context,
             bool staticConversions = false);
value compare(ctjs::CompareKind kind, value left, value right, mlir::MLIRContext * context);
value unary(ctjs::UnaryKind kind, value input, mlir::MLIRContext * context);

class evaluator {
public:
    evaluator(mlir::ModuleOp module, unsigned steps, unsigned nodes, unsigned depth)
        : module(module), maxSteps(steps), maxNodes(nodes), maxDepth(depth) {}
    std::optional<snapshot> run(ctjs::FuncOp function, llvm::ArrayRef<value> args);
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
    value allocate(node::kind kind, mlir::Location location);
    value call(ctjs::FuncOp function, llvm::ArrayRef<value> args, unsigned depth);
    value directCall(ctjs::CallDirectOp invoked, environment & env, unsigned depth);
    completion region(mlir::Region & region, llvm::ArrayRef<value> args, environment & env,
                      unsigned depth);
    value operation(mlir::Operation * op, environment & env, unsigned depth);
    value closureOperation(mlir::Operation * op, environment & env);
};

// Returns nullopt for a cycle or an unsupported root. No IR changes occur
// until this reachable graph check has succeeded.
std::optional<std::vector<unsigned>> reachable(const snapshot & state);
void residualize(ctjs::FuncOp function, const snapshot & state, llvm::ArrayRef<unsigned> live);
bool retainedPrefixScaffolding(mlir::Operation * op);
bool factoryParameterUse(mlir::OpOperand & use, unsigned parameter);

} // namespace ctcompile::ctnative::partial_eval
