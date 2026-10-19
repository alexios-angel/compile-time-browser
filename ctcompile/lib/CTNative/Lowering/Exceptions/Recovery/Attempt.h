//===- Attempt.h - Exception recovery transaction ---------------*- C++ -*-===//
//
// Private to Lowering/Exceptions/Recovery/. Exceptions/Recovery.h declares the
// entry points the lowering calls; this header declares `recovery`, the
// transaction behind them, so its members can be defined across the files of
// this directory. Each member is declared next to a note naming the file that
// defines it.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../Recovery.h"

#include "../../../../CTJS/Lowering/Globals/RegisterFlow.h"
#include "ctcompile/CTNative/Analysis/TypeInference.h"
#include "ctcompile/CTNative/IR/CTNativeDialect.h"

#include "mlir/Conversion/ControlFlowToSCF/ControlFlowToSCF.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/OwningOpRef.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Transforms/CFGToSCF.h"
#include "mlir/Transforms/RegionUtils.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"

#include <cstdint>
#include <utility>

namespace ctcompile::ctnative::lowering_detail {

/// One attempt to rebuild a function's exception handler as a ctjs.try.
///
/// Recovery runs before any handler CFG simplification. The importer carries
/// the complete register vector on ordinary and check edges, while an explicit
/// throw has no successor, so a throw-only block supplies its current vector
/// through its block arguments; the edge that installs the handler supplies no
/// throw state.
///
/// The attempt works on a disposable clone of the function. It proves there is
/// one handler, a dedicated landing block, balanced continuation edges and
/// acyclic tails, then clones the normal and catch tails separately, including
/// any continuation they share. `ctjs.check` edges become ordinary edges only
/// if the caller's later native admission proves the call cannot throw. Every
/// body exit becomes the same completion terminator (flag, normal result,
/// payload/state), so MLIR's CFG-to-SCF transform can structure the branches
/// without merging a throw with a normal completion. The native consumer must
/// still emit a C++ throw for the exceptional completion. Catch exits use
/// try_yield. The recovered body replaces the original only after both regions
/// structure successfully.
struct recovery : workBudget {
    ctjs::FuncOp function;
    mlir::ModuleOp module;
    mlir::IRMapping copies;
    ctjs::PushHandlerOp push;
    ctjs::CatchLandOp landing;
    ctjs::FrameEnterOp frame;
    unsigned width = 0;
    unsigned throws = 0;
    ExceptionRecoveryMode mode;
    llvm::DenseMap<mlir::Operation *, mlir::Operation *> invocations;
    llvm::DenseMap<mlir::StringAttr, ctjs::FuncOp> bindings;
    llvm::DenseSet<mlir::Operation *> boundTargets;
    llvm::DenseSet<mlir::Operation *> boundCalls;
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::CallDirectOp>> boundCallers;
    llvm::DenseSet<mlir::Block *> prefix;

    recovery(ctjs::FuncOp function, unsigned maxSteps, ExceptionRecoveryMode mode)
        : workBudget{maxSteps, "native exception recovery work budget exhausted"},
          function(function), module(function->getParentOfType<mlir::ModuleOp>()), mode(mode) {}

    struct tail {
        llvm::SmallVector<mlir::Block *> blocks;
        llvm::DenseMap<mlir::Block *, bool> active;
        llvm::DenseMap<mlir::Block *, llvm::SmallVector<mlir::Block *>> edges;
    };

    // Defined in Inspect.cpp.
    bool inspect();
    bool partition(const tail & normal, const tail & caught);
    bool inspectInvocation(ctjs::CheckOp check);

    // Defined in Bindings.cpp.
    bool hasOrigin(mlir::Value value, mlir::Value origin);
    std::optional<llvm::SmallVector<mlir::OpOperand *>> uses(mlir::Value value);
    bool bindingFailure(llvm::StringRef why);
    bool proveBindings();

    // Defined in Effects.cpp.
    using primitiveInput = std::pair<mlir::Value, unsigned>;

    struct completionContext {
        ctjs::CallDirectOp call;
        unsigned parent = 0;
        unsigned depth = 0;
    };

    struct primitiveContexts {
        llvm::SmallVector<completionContext> frames{completionContext{}};
        llvm::DenseMap<std::pair<mlir::Operation *, unsigned>, unsigned> indices;
    };

    std::optional<unsigned> contextFor(ctjs::CallDirectOp call, unsigned parent,
                                       primitiveContexts & contexts);
    bool completionInputs(ctjs::CallDirectOp call, bool thrown, unsigned parent,
                          primitiveContexts & contexts,
                          llvm::SmallVectorImpl<primitiveInput> & inputs);
    bool primitive(mlir::Value value);
    bool nonthrowing(mlir::Operation * operation);
    bool proveEffects(const tail & normal, const tail & caught);

    // Defined in Tails.cpp.
    bool collect(tail & result, mlir::Block * start, bool isCatch);
    bool acyclic(tail & plan);
    /// Kahn's topological drain of `ready`, charging each node and edge.
    /// Returns the number of nodes visited, or nullopt once the budget is
    /// spent.
    template <class Ready, class Incoming, class Edges>
    std::optional<unsigned> drain(Ready & ready, Incoming & incoming, Edges & edges) {
        unsigned visited = 0;
        while (!ready.empty()) {
            if (!spend()) { return std::nullopt; }
            auto node = ready.pop_back_val();
            ++visited;
            for (auto successor : edges[node]) {
                if (!spend()) { return std::nullopt; }
                if (--incoming[successor] == 0) { ready.push_back(successor); }
            }
        }
        return visited;
    }
    bool cloneTail(const tail & plan, mlir::Region & destination, bool isCatch);

    // Defined in Structure.cpp.
    bool structure(mlir::Region & region);
    bool normalizeIndexSwitches(mlir::Operation * guarded);
    static bool isFullPoison(mlir::Value value);
    bool trimUnusedIfResults(mlir::Operation * guarded);

    // Defined in Transaction.cpp.
    bool run();
};

} // namespace ctcompile::ctnative::lowering_detail
