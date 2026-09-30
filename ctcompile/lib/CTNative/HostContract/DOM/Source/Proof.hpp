#pragma once

#include "../../Analysis.h"
#include "../Snapshot/Bounds.h"
#include "ctbrowser/core/algorithms.hpp"
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Analysis/ImmutableCaptures.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringMap.h"

#include <optional>

namespace ctcompile::ctnative::dom_source_detail {

struct DOMSource {
    explicit DOMSource(unsigned maxSteps) : remaining(maxSteps) {}

    unsigned remaining;
    unsigned operationCount = 0;
    std::string reason;
    llvm::DenseMap<unsigned, ctjs::FuncOp> functions;
    llvm::DenseMap<unsigned, unsigned> creations;
    llvm::DenseSet<mlir::Operation *> active, expanded, retainedCallbacks;
    struct Capture {
        ctjs::CreateCellOp cell;
        ctjs::CellSetOp write;
        int32_t enclosingIndex = -1;
        mlir::Value value() { return write ? write.getValue() : cell.getInitial(); }
    };
    llvm::DenseMap<mlir::Value, Capture> cells;
    llvm::DenseMap<mlir::Operation *, unsigned> callDepth;
    llvm::DenseMap<mlir::Value, llvm::SmallVector<ctjs::StringAttr>> stringInputs;
    llvm::DenseSet<mlir::Value> inactiveFillers;

    bool refuse(llvm::StringRef message);
    // refuse(), then the empty result or a stopped walk.
    std::nullopt_t fail(llvm::StringRef message) {
        refuse(message);
        return std::nullopt;
    }
    mlir::WalkResult halt(llvm::StringRef message) {
        refuse(message);
        return mlir::WalkResult::interrupt();
    }
    bool step(uint64_t cost = 1);
    // One all-or-nothing charge; one that does not fit drains the budget, as
    // `cost` separate steps would.
    bool drain(uint64_t cost);
    bool chargeCaptureQuery();
    static bool undefined(mlir::Value value) { return ctjs::isUndefinedConstant(value); }

    bool precedesInStructuredBody(mlir::Operation * definition, mlir::Operation * use);

    ctjs::FuncOp closureTarget(ctjs::CreateClosureOp closure) {
        return closure && closure.getFunction() >= 0
                   ? functions.lookup(static_cast<unsigned>(closure.getFunction()))
                   : ctjs::FuncOp{};
    }
    // Whether the census saw exactly one creation of this closure's function.
    bool uniqueCreation(ctjs::CreateClosureOp closure) {
        return closure.getFunction() >= 0 &&
               creations.lookup(static_cast<unsigned>(closure.getFunction())) == 1;
    }
    // Erase every root of `value`; refuse any other remaining use.
    bool eraseRootedUses(mlir::Value value, llvm::StringRef reason);

    // A capture-free filter predicate or uppercase replacement, invoked only as
    // the callback of its own Array filter or String replace call.
    bool confinedCallback(ctjs::CreateClosureOp closure, ctjs::FuncOp target);

    bool bindConstantArguments(ctjs::CreateClosureOp closure, ctjs::FuncOp target);

    bool foldConstantReplacements(ctjs::FuncOp function);

    bool captureTarget(ctjs::CreateClosureOp closure, ctjs::FuncOp target);

    bool captureStorage(mlir::OpOperand & use);

    bool resolveCell(ctjs::CreateCellOp cell);

    bool forwardFields(ctjs::FuncOp function);

    bool dataObject(ctjs::CreateObjectOp object);

    bool resolveMethods(ctjs::CreateObjectOp object, llvm::DenseSet<mlir::Operation *> & methods);

    bool flattenEntryFactory(ctjs::FuncOp wrapper, ctjs::FuncOp target);

    bool initializeEntry(ctjs::FuncOp wrapper, ctjs::FuncOp target);

    bool normalizeCompletion(ctjs::FuncOp function);
    // Erase the unused inert arithmetic under `root`, users before operands.
    template <typename Root> bool eraseUnusedArithmetic(Root & root) {
        llvm::SmallVector<mlir::Operation *> arithmetic;
        const auto scanned = root.walk([&](mlir::Operation * operation) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            if (llvm::isa<mlir::arith::ConstantOp, mlir::arith::IndexCastUIOp>(operation)) {
                arithmetic.push_back(operation);
            }
            return mlir::WalkResult::advance();
        });
        if (scanned.wasInterrupted()) { return false; }
        for (mlir::Operation * operation : llvm::reverse(arithmetic)) {
            if (!step()) { return false; }
            if (operation->use_empty()) { operation->erase(); }
        }
        return true;
    }
    bool repairInactiveCompletion(ctjs::FuncOp function);

    bool checkBody(ctjs::FuncOp function, bool entry, bool directReceiver = false,
                   bool beforeReplacement = false);

    bool proveUnusedBody(ctjs::FuncOp function, unsigned depth = 0);

    bool inlineCall(ctjs::FuncOp function, ctjs::FuncOp target, mlir::Operation * call,
                    mlir::ValueRange arguments, mlir::Value receiver, mlir::Value callee,
                    llvm::MutableArrayRef<Capture> captures, unsigned depth);

    bool expand(ctjs::FuncOp function, unsigned depth, bool entry = false,
                bool directReceiver = false);
};

} // namespace ctcompile::ctnative::dom_source_detail
