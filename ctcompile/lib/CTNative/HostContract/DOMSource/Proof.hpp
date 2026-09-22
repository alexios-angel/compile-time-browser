#pragma once

#include "../Analysis.h"
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
    bool step();
    bool chargeCaptureQuery();
    static bool undefined(mlir::Value value);

    bool precedesInStructuredBody(mlir::Operation * definition, mlir::Operation * use);

    bool confinedFilterCallback(ctjs::CreateClosureOp closure, ctjs::FuncOp target);

    bool confinedReplacementCallback(ctjs::CreateClosureOp closure, ctjs::FuncOp target);

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
