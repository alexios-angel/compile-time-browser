#pragma once

#include "../../Lowering/Exceptions/Recovery.h"
#include "../Analysis.h"
#include "../Preparation.h"
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/MemoryBuffer.h"

namespace ctcompile::ctnative::class_detail {

struct classInitialization {
    mlir::ModuleOp module;
    unsigned remaining;
    std::string reason;
    llvm::DenseMap<unsigned, ctjs::FuncOp> functions;
    llvm::StringMap<ctjs::StoreGlobalOp> globals;
    llvm::StringMap<bool> closedGlobals;
    llvm::DenseMap<mlir::Value, ctjs::CreateClosureOp> holderReads;
    llvm::DenseSet<mlir::Operation *> globalHolderLoads;
    llvm::MapVector<mlir::Operation *, CallableObject> globalHolders;
    llvm::MapVector<mlir::Operation *, CallableObject> localDOMHolders;
    llvm::SmallVector<ctjs::CallOp> calls;
    llvm::DenseMap<mlir::Value, ctjs::CallOp> heritage;
    llvm::DenseSet<mlir::Value> baseClasses;
    llvm::DenseMap<mlir::Value, llvm::SmallVector<ctjs::SetPropertyOp>> inheritedMethods;
    llvm::DenseMap<mlir::Value, llvm::SmallVector<llvm::StringRef>> snapshotFields;
    llvm::SmallVector<std::pair<ctjs::CallOp, ctjs::SetPropertyOp>> inheritedSlots;
    llvm::DenseSet<mlir::Operation *> setup;
    llvm::DenseSet<mlir::Operation *> retainedSetup;
    llvm::DenseSet<mlir::Operation *> constructors;
    llvm::DenseSet<mlir::Operation *> methods;
    llvm::DenseSet<mlir::Operation *> methodCalls;
    llvm::SmallVector<std::pair<ctjs::ConstructOp, ctjs::SetPropertyOp>> methodProbes;
    bool needsDOMMethodProof = false;
    llvm::DenseSet<mlir::Operation *> helpers;
    llvm::DenseSet<mlir::Operation *> helperCalls;
    llvm::DenseSet<mlir::Operation *> domEntryHelpers;
    llvm::DenseSet<mlir::Operation *> getters;
    llvm::DenseSet<mlir::Operation *> throwingGetters;
    llvm::DenseSet<mlir::Operation *> errorOperations;
    llvm::SmallVector<ctjs::GetPropertyOp> constructorReads;
    llvm::SmallVector<std::pair<ctjs::GetPropertyOp, ctjs::FuncOp>> getterReads;
    llvm::SmallVector<ctjs::FuncOp> getterOrder;
    llvm::SmallVector<ctjs::CreateClosureOp> getterClosures;
    llvm::SmallVector<std::pair<ctjs::GetPropertyOp, ctjs::FuncOp>> staticMethodReads;
    llvm::SmallVector<ctjs::CreateClosureOp> staticMethodClosures;
    llvm::MapVector<mlir::Value, mlir::Value> cells;
    llvm::DenseSet<mlir::Operation *> cellOperations;
    llvm::SmallVector<ctjs::CellGetOp> cellReads;
    llvm::SmallVector<ctjs::LoadUpvalueOp> captureReads;
    llvm::DenseMap<mlir::Value, mlir::Value> holderCaptures;
    llvm::DenseMap<mlir::Operation *, ctjs::FuncOp> callableCaptures;
    llvm::SetVector<mlir::Operation *> capturedHelpers;
    llvm::SmallVector<ctjs::CreateClosureOp> capturedClosures;
    llvm::SmallVector<ctjs::CreateClosureOp> inertReceiverClosures;
    llvm::SmallVector<mlir::Operation *> inertCalleeCalls;
    llvm::SetVector<mlir::Operation *> dispatchMethods;
    llvm::SmallVector<std::pair<ctjs::FuncOp, mlir::OwningOpRef<ctjs::FuncOp>>> normalizedMethods;

    classInitialization(mlir::ModuleOp module, unsigned steps) : module(module), remaining(steps) {}

    bool step();
    bool refuse(llvm::StringRef message);
    static bool undefined(mlir::Value value);
    ctjs::FuncOp target(ctjs::CreateClosureOp closure);
    bool proveCells(ctjs::FuncOp entry);
    mlir::Value sourceValue(mlir::Value value);
    llvm::SmallVector<mlir::OpOperand *> sourceUses(mlir::Value value);
    bool helperCallback(mlir::OpOperand & use);
    bool helperCallbacks(ctjs::FuncOp helper);
    bool methodCaptures(ctjs::CreateClosureOp method, ctjs::CreateClosureOp constructor,
                        llvm::SmallVectorImpl<ctjs::GetPropertyOp> & reads, bool domEntry,
                        unsigned depth = 0);
    ctjs::CreateClosureOp sourceClosure(mlir::Value value, bool domEntry = false);
    bool unusedReceiver(ctjs::FuncOp fn);
    bool ownFieldSnapshots(ctjs::CreateClosureOp constructor,
                           llvm::ArrayRef<ctjs::ConstructOp> instances,
                           llvm::ArrayRef<ctjs::SetPropertyOp> definitions,
                           const llvm::StringSet<> & methodKeys, const HostContract & contract);
    bool fieldsOnly(mlir::Value object, const llvm::StringSet<> & methodKeys,
                    llvm::SmallVectorImpl<ctjs::GetPropertyOp> & staticReads,
                    bool methodsAvailable = false);

    bool staticGetters(const llvm::StringMap<ctjs::DefineAccessorOp> & definitions,
                       llvm::ArrayRef<ctjs::GetPropertyOp> reads, ctjs::CallOp helper);

    bool proveHeritage(const HostContract & contract);

    bool heritageUse(mlir::OpOperand & use, mlir::Value constructor);

    // ponytail: explicit super statements over local bases. Fields, replacement
    // returns and default rest/apply need their own proof.
    bool normalizeSuper(ctjs::FuncOp function, ctjs::FuncOp base, const HostContract & contract,
                        const llvm::StringSet<> & methodKeys);

    bool normalizeSuperMethods(ctjs::FuncOp function,
                               llvm::ArrayRef<ctjs::SetPropertyOp> baseDefinitions);

    bool examine(ctjs::CallOp call, const HostContract & contract, bool domEntry);

    bool prove(const HostContract & contract, bool domEntry = false);

    bool proveDOMMethods(const HostContract & contract);

    bool normalizeMethods();

    static void eraseRooted(mlir::Operation * operation);
    void expandHolders();

    void rewrite();
};

} // namespace ctcompile::ctnative::class_detail
