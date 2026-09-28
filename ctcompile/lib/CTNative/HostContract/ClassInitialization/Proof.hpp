#pragma once

#include "../../Lowering/Exceptions/Recovery.h"
#include "../Analysis.h"
#include "../Preparation.h"
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Analysis/WorkBudget.h"
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

inline bool closureMetadataKey(llvm::StringRef key) {
    static const llvm::StringSet<> names{"name", "length", "__home", "caller", "arguments"};
    return key.size() <= 9 && names.contains(key);
}

// The precharge for cloning an operation: itself, its operands and results.
inline uint64_t cloneCost(mlir::Operation * op) {
    return uint64_t(1) + op->getNumOperands() + op->getNumResults();
}

// The closed-source contract that proves only the class helper and Error bindings.
inline HostContract classBindingContract(HostContract binding) {
    binding.provider = HostContract::Provider::closedSource;
    binding.elementParameters.clear();
    binding.currentDocumentParameter.reset();
    llvm::erase_if(binding.initialIntrinsics, [](const auto & name) {
        return !host_detail::classIntrinsicArity(name) && name != "Error";
    });
    return binding;
}

// An explicit DOM Data entry input that the contract declares an element.
inline bool declaredElementInput(const HostContract & contract, ctjs::FuncOp scope,
                                 mlir::BlockArgument input) {
    return contract.provider == HostContract::Provider::ctbrowserDOMDataSession && input &&
           scope.getSymName() == contract.entry && input.getOwner() == &scope.getBody().front() &&
           input.getArgNumber() >= ctjs::implicit_arguments &&
           llvm::is_contained(contract.elementParameters,
                              input.getArgNumber() - ctjs::implicit_arguments);
}

// A nonnegative enclosing index captures a slot of an outer activation.
inline bool hasOuterCapture(ctjs::CreateClosureOp closure) {
    auto indices = closure.getEnclosingIndicesAttr();
    return indices && llvm::any_of(indices.asArrayRef(), [](int32_t index) { return index >= 0; });
}

// Replace a proved closure's capture slots; they are all local cells now.
inline void setCaptures(ctjs::CreateClosureOp closure, ctjs::FuncOp function,
                        mlir::ValueRange captures) {
    closure.getUpvaluesMutable().assign(captures);
    closure.removeEnclosingIndicesAttr();
    function.setUpvalueCount(static_cast<uint32_t>(captures.size()));
}

inline bool rootedOnly(mlir::Operation * operation) {
    return llvm::all_of(operation->getUsers(), llvm::IsaPred<ctjs::RootOp>);
}

// The precharge for closedDeclaration's operation and use scans.
inline uint64_t declarationCost(mlir::Operation * op) {
    return uint64_t(2) + op->getNumOperands();
}

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
    llvm::MapVector<mlir::Operation *, CallableObject> localHolders;
    llvm::SmallVector<ctjs::CallOp> calls;
    // Publication scans may stop early; charge work in source order, not pointer order.
    llvm::MapVector<mlir::Value, ctjs::CallOp> heritage;
    llvm::DenseSet<mlir::Value> baseClasses;
    llvm::DenseMap<mlir::Value, llvm::SmallVector<ctjs::SetPropertyOp>> inheritedMethods;
    llvm::DenseMap<mlir::Value, llvm::StringMap<ctjs::DefineAccessorOp>> inheritedGetters;
    // Per derived class: copies of inherited getters whose receiver reads reach
    // a key that class shadows, proved and expanded in its own environment.
    llvm::DenseMap<mlir::Value, llvm::StringMap<ctjs::FuncOp>> receiverGetters;
    llvm::DenseMap<mlir::Value, llvm::SmallVector<llvm::StringRef>> snapshotFields;
    llvm::DenseSet<mlir::Operation *> snapshotMethods;
    // Bases constructed only through their leaves' super calls leave their
    // snapshots (calls and methods) to the first leaf that folds them.
    llvm::DenseSet<mlir::Value> deferredSnapshots;
    llvm::DenseSet<mlir::Operation *> pendingSnapshots, pendingSnapshotMethods;
    llvm::SmallVector<std::pair<ctjs::CallOp, ctjs::SetPropertyOp>> inheritedSlots;
    llvm::DenseSet<mlir::Operation *> setup;
    llvm::DenseSet<mlir::Operation *> retainedSetup;
    llvm::DenseSet<mlir::Operation *> constructors;
    llvm::DenseSet<mlir::Operation *> normalizedSuper;
    llvm::MapVector<mlir::Operation *, bool> instanceOfResults;
    llvm::DenseSet<mlir::Operation *> methods;
    llvm::DenseSet<mlir::Operation *> methodCalls;
    llvm::SmallVector<std::pair<ctjs::ConstructOp, ctjs::SetPropertyOp>> methodProbes;
    bool needsDOMMethodProof = false;
    llvm::DenseSet<mlir::Operation *> helpers;
    llvm::DenseSet<mlir::Operation *> helperCalls;
    llvm::DenseSet<mlir::Operation *> domEntryHelpers;
    llvm::DenseSet<mlir::Operation *> domDataOperations;
    llvm::DenseMap<mlir::Value, mlir::Value> retainedRecordOrigins;
    llvm::DenseSet<mlir::Operation *> getters;
    llvm::DenseSet<mlir::Operation *> throwingGetters;
    llvm::DenseSet<mlir::Operation *> errorOperations;
    llvm::SmallVector<ctjs::GetPropertyOp> constructorReads;
    llvm::SmallVector<std::pair<ctjs::GetPropertyOp, ctjs::FuncOp>> getterReads;
    llvm::SmallVector<ctjs::FuncOp> getterOrder;
    llvm::DenseMap<mlir::Operation *, unsigned> getterExpansion;
    llvm::SmallVector<ctjs::CreateClosureOp> getterClosures;
    llvm::SmallVector<std::pair<ctjs::GetPropertyOp, ctjs::FuncOp>> staticMethodReads;
    llvm::SmallVector<ctjs::CreateClosureOp> staticMethodClosures;
    llvm::MapVector<mlir::Value, mlir::Value> cells;
    llvm::DenseSet<mlir::Operation *> cellOperations;
    llvm::DenseSet<mlir::Operation *> earlyCaptures;
    llvm::SmallVector<ctjs::CellGetOp> cellReads;
    llvm::SmallVector<ctjs::LoadUpvalueOp> captureReads;
    llvm::DenseSet<mlir::Value> maps;
    llvm::DenseSet<mlir::Value> mapCells;
    llvm::DenseMap<mlir::Value, mlir::Value> mapCaptures;
    llvm::DenseSet<mlir::Operation *> mapClosures;
    llvm::DenseSet<mlir::Operation *> mapOperations;
    llvm::DenseMap<mlir::Operation *, ctjs::CallOp> deferredPublications;
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
    // `count` steps at once; one that does not fit spends the rest, as `count` steps would.
    bool spend(uint64_t count);
    // Precharges every operation `root` walks, keeping what was spent before a misfit.
    bool spendWalk(mlir::Operation * root, llvm::function_ref<uint64_t(mlir::Operation *)> cost);
    bool refuse(llvm::StringRef message);
    static bool undefined(mlir::Value value) { return ctjs::isUndefinedConstant(value); }
    ctjs::FuncOp target(ctjs::CreateClosureOp closure);
    bool proveCells(ctjs::FuncOp entry);
    bool proveMaps();
    mlir::Value sourceValue(mlir::Value value);
    llvm::SmallVector<mlir::OpOperand *> sourceUses(mlir::Value value);
    bool helperCallback(mlir::OpOperand & use);
    bool helperCallbacks(ctjs::FuncOp helper);
    bool methodCaptures(ctjs::CreateClosureOp method, ctjs::CreateClosureOp constructor,
                        llvm::SmallVectorImpl<ctjs::GetPropertyOp> & reads, bool domEntry,
                        unsigned depth = 0);
    ctjs::CreateClosureOp sourceClosure(mlir::Value value, bool entryAliases = false,
                                        bool domEntry = false);
    bool unusedReceiver(ctjs::FuncOp fn);
    bool borrowedHelperReads(mlir::OpOperand & use, const llvm::StringSet<> & methodKeys,
                             llvm::SmallVectorImpl<ctjs::GetPropertyOp> & reads,
                             unsigned depth = 0);
    bool clearOwnFieldLoop(ctjs::CallOp snapshot, llvm::ArrayRef<llvm::StringRef> fields,
                           const HostContract & contract);
    bool foldEmptyEntries(const HostContract & contract);
    bool normalizePublicationHelper(ctjs::CreateClosureOp constructor,
                                    const HostContract & contract);
    bool normalizePublicationKey(ctjs::CreateClosureOp constructor, const HostContract & contract);
    bool publicationSuffix(ctjs::CallOp publication, llvm::ArrayRef<ctjs::ConstructOp> instances,
                           bool allowFields);
    bool sinkCapturedPublication(ctjs::CallOp setup, const HostContract & contract,
                                 llvm::SmallVectorImpl<unsigned> & requiredHelpers);
    bool normalizeCapturedMapHelpers(ctjs::FuncOp scope);
    bool normalizeNestedMaps(ctjs::FuncOp scope, const HostContract & contract);
    bool sinkConstructorPublication(ctjs::CreateClosureOp constructor,
                                    llvm::ArrayRef<ctjs::ConstructOp> instances,
                                    const HostContract & contract);
    bool ownFieldSnapshots(ctjs::CreateClosureOp constructor,
                           llvm::ArrayRef<ctjs::ConstructOp> instances,
                           llvm::ArrayRef<ctjs::SetPropertyOp> definitions,
                           const llvm::StringSet<> & methodKeys,
                           const llvm::StringMap<ctjs::DefineAccessorOp> & staticDefinitions,
                           const llvm::StringMap<ctjs::SetPropertyOp> & staticMethods,
                           const HostContract & contract);
    // `unconstructed`: the original body of a base only leaves construct.
    bool fieldsOnly(mlir::Value object, const llvm::StringSet<> & methodKeys,
                    llvm::SmallVectorImpl<ctjs::GetPropertyOp> & staticReads,
                    bool methodsAvailable = false, bool unconstructed = false);
    bool retainedMapAliases(mlir::OpOperand & use, llvm::SmallVectorImpl<mlir::Value> & reads);
    bool retainedMapReads(mlir::OpOperand & use, const llvm::StringSet<> & methodKeys,
                          llvm::SmallVectorImpl<ctjs::GetPropertyOp> & staticReads);

    bool foldDecidedArms(ctjs::CreateClosureOp constructor,
                         llvm::ArrayRef<ctjs::ConstructOp> instances);
    // The primitive an exact captured helper returns for primitive actuals.
    std::optional<mlir::Attribute> evaluateHelper(ctjs::FuncOp fn,
                                                  llvm::ArrayRef<mlir::Attribute> actuals,
                                                  unsigned depth);
    bool freshFunctionIndex(unsigned & next);
    // A copy of `fn` appended to the module as source function `index`.
    ctjs::FuncOp cloneFunction(ctjs::FuncOp fn, unsigned index);
    // The ancestor of `op` in `block` through Parents regions, one step each;
    // where the chain leaves them or the budget stops, the operation reached.
    template <class... Parents>
    mlir::Operation * positionIn(mlir::Operation * op, mlir::Block * block) {
        while (op->getBlock() != block && llvm::isa_and_nonnull<Parents...>(op->getParentOp())) {
            if (!step()) { break; }
            op = op->getParentOp();
        }
        return op;
    }
    bool cloneReceiverMethods(ctjs::CreateClosureOp leaf, ctjs::CreateObjectOp prototype,
                              ctjs::CallOp helper, const llvm::StringSet<> & shadowed,
                              llvm::SmallVectorImpl<ctjs::SetPropertyOp> & selected,
                              llvm::SmallVectorImpl<ctjs::SetPropertyOp> & definitions);
    bool cloneReceiverGetters(mlir::Value leaf,
                              const llvm::StringMap<ctjs::DefineAccessorOp> & environment,
                              const llvm::StringSet<> & shadowed);
    bool staticGetters(const llvm::StringMap<ctjs::DefineAccessorOp> & definitions,
                       llvm::ArrayRef<ctjs::GetPropertyOp> reads, ctjs::CallOp helper);

    bool proveHeritage(const HostContract & contract);
    bool proveInstanceOf(const HostContract & contract);

    bool heritageUse(mlir::OpOperand & use, mlir::Value constructor);

    // ponytail: explicit super statements over local bases. Fields, replacement
    // returns and default rest/apply need their own proof.
    bool normalizeSuper(ctjs::CreateClosureOp constructor, ctjs::CreateClosureOp baseClosure,
                        const HostContract & contract);

    bool normalizeSuperMethods(ctjs::FuncOp function,
                               llvm::ArrayRef<ctjs::SetPropertyOp> baseDefinitions);
    // Drop the helper-call, Map and capture records of a replaced body.
    void forgetRecords(mlir::Region & region);

    bool examine(ctjs::CallOp call, const HostContract & contract, bool domEntry);

    bool prove(const HostContract & contract, bool domEntry = false);
    bool proveDOMDataFamily(const HostContract & contract);
    bool proveDOMDataScalars(host_detail::analyzer & analysis);

    bool proveDOMMethods(const HostContract & contract);

    bool normalizeMethods();
    bool transportHelperMaps();

    static void eraseRooted(mlir::Operation * operation);
    static void eraseIfRooted(mlir::Operation * operation) {
        if (rootedOnly(operation)) { eraseRooted(operation); }
    }
    // Erase a fixed cell with its recorded reads and remaining transport users.
    void retireCell(mlir::Value cell);
    // Erase a helper closure whose calls were all expanded, and its function.
    void retireHelper(ctjs::CreateClosureOp closure, ctjs::FuncOp function);
    llvm::SmallVector<ctjs::FuncOp> expandHolders();

    void rewrite();
};

} // namespace ctcompile::ctnative::class_detail
