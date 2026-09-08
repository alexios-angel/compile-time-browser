#pragma once
// Private to Lowering/Exceptions/Recovery/. Exceptions/Recovery.h declares the
// one entry point the lowering calls; this declares the transaction behind it
// so that its members can live in more than one file - they were 1,251 lines
// in one until 2026-09-08. It was `struct recovery` in an anonymous namespace
// then and has external linkage in lowering_detail now, which is the only
// thing about it that changed: the data members, the constructor, the nested
// types and the two one-line members are verbatim, and each remaining member
// is declared here next to the file that defines it. The includes are
// Recovery.cpp's, so every file here sees exactly what that one saw.

#include "../Recovery.h"

#include "../../../../CTJS/Lowering/Globals/RegisterFlow.h"
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
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

// Recovery runs before any handler CFG simplification. The importer carries
// the complete register vector on ordinary/check edges, while explicit throw
// has no successor. A throw-only block therefore supplies its current vector
// through its block arguments; the installation edge supplies no throw state.
//
// Work on a disposable function clone. Prove one entry handler, its dedicated
// landing, balanced continuation edges and acyclic tails. Clone normal and catch
// tails separately, including any shared continuation. Checks become normal
// edges only under the caller's subsequent native nonthrowing admission. Every
// body exit becomes the SAME completion terminator (flag, normal result,
// payload/state): LLVM can then structure ordinary branches without merging a
// throw with a normal completion. The native consumer must still emit a C++
// throw for the exceptional completion. Catch exits use try_yield. Only after
// both regions structure successfully is the recovered body adopted.
struct recovery {
    ctjs::FuncOp function;
    mlir::ModuleOp module;
    mlir::IRMapping copies;
    unsigned remaining;
    std::string refusal;
    ctjs::PushHandlerOp push;
    ctjs::CatchLandOp landing;
    ctjs::FrameEnterOp frame;
    unsigned width = 0;
    unsigned throws = 0;
    ExceptionRecoveryMode mode;
    llvm::DenseMap<mlir::Operation *, ctjs::CallDirectOp> invocations;
    llvm::DenseMap<mlir::StringAttr, ctjs::FuncOp> bindings;
    llvm::DenseSet<mlir::Operation *> boundTargets;
    llvm::DenseSet<mlir::Operation *> boundCalls;
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::CallDirectOp>> boundCallers;

    recovery(ctjs::FuncOp function, unsigned maxSteps, ExceptionRecoveryMode mode)
        : function(function), module(function->getParentOfType<mlir::ModuleOp>()),
          remaining(maxSteps), mode(mode) {}

    struct tail {
        llvm::SmallVector<mlir::Block *> blocks;
        llvm::DenseMap<mlir::Block *, bool> active;
        llvm::DenseMap<mlir::Block *, llvm::SmallVector<mlir::Block *>> edges;
    };

    bool reject(llvm::StringRef why) {
        if (refusal.empty()) { refusal = why.str(); }
        return false;
    }

    bool spend(uint64_t amount = 1) {
        if (amount > remaining) {
            return reject("native exception recovery work budget exhausted");
        }
        remaining -= static_cast<unsigned>(amount);
        return true;
    }

    // Defined in Inspect.cpp.
    bool inspect();
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
    bool collect(tail & result, mlir::Block * start, bool initiallyActive, bool isCatch);
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
