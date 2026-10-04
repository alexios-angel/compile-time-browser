#pragma once

#include "../../Lowering/Exceptions/Recovery.h"
#include "../../PartialEvaluation/Heap.h"
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

// An explicit DOM entry input that the contract declares an element.
inline bool declaredElementInput(const HostContract & contract, ctjs::FuncOp scope,
                                 mlir::BlockArgument input) {
    return (contract.provider == HostContract::Provider::ctbrowserDOMDataSession ||
            contract.provider == HostContract::Provider::ctbrowserDOM ||
            contract.provider == HostContract::Provider::ctbrowserDOMSession) &&
           input && scope.getSymName() == contract.entry &&
           input.getOwner() == &scope.getBody().front() &&
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

// An undefined constant at `at`.
inline ctjs::ConstantOp undefinedAt(mlir::OpBuilder & at, mlir::Location where) {
    return ctjs::ConstantOp::create(at, where, ctjs::UndefinedAttr::get(at.getContext()));
}

// The region an index switch selects for `key`: its last equal case, else the
// default. One `step` per case; null when `step` refuses.
inline mlir::Region * switchArm(mlir::scf::IndexSwitchOp branch, int64_t key,
                                llvm::function_ref<bool()> step) {
    auto * region = &branch.getDefaultRegion();
    for (auto [i, value] : llvm::enumerate(branch.getCases())) {
        if (!step()) { return nullptr; }
        if (value == key) { region = &branch.getCaseRegions()[i]; }
    }
    return region;
}

// Whether `load`'s slot is one of `closure`'s captures.
inline bool capturesSlot(ctjs::LoadUpvalueOp load, ctjs::CreateClosureOp closure) {
    return load.getIndex() >= 0 &&
           static_cast<size_t>(load.getIndex()) < closure.getUpvalues().size();
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
    // Shadowed bodies get private slots only until their typed probes pass.
    llvm::DenseSet<mlir::Operation *> proofOnlyMethods;
    bool needsDOMMethodProof = false;
    llvm::DenseSet<mlir::Operation *> helpers;
    llvm::DenseSet<mlir::Operation *> helperCalls;
    // Helpers and holder slots a decided arm's dropped or erased call named,
    // and those no running body calls: no census, and rewrite erases them.
    llvm::SetVector<mlir::Operation *> decidedHelpers;
    llvm::DenseSet<mlir::Operation *> uncalledHelpers;
    // Constructors of bases only leaves' super copies build: never run.
    llvm::DenseSet<mlir::Operation *> leafBuilt;
    llvm::DenseSet<mlir::Operation *> domEntryHelpers;
    // DOM entries: each declared element parameter and its index, the element
    // actual evaluateHelper knows by a partial_eval reference to that index.
    llvm::DenseMap<mlir::Value, unsigned> elementActuals;
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
    mlir::WalkResult refuseWalk(llvm::StringRef message) {
        refuse(message);
        return mlir::WalkResult::interrupt();
    }
    // Walk `root`'s operations of `visit`'s argument type, one step before each
    // visit. False when the budget or `visit` interrupts.
    template <mlir::WalkOrder Order = mlir::WalkOrder::PostOrder, class Visit>
    bool walkSteps(mlir::Operation * root, Visit && visit) {
        using Op = mlir::detail::first_argument<std::remove_reference_t<Visit>>;
        return !root->walk<Order>([&](Op op) -> mlir::WalkResult {
                        if (!step()) { return mlir::WalkResult::interrupt(); }
                        return visit(op);
                    })
                    .wasInterrupted();
    }
    // Point every cell whose recorded initial value is `from` at `to`.
    void retargetCells(mlir::Value from, mlir::Value to) {
        for (mlir::Value & initial : llvm::make_second_range(cells)) {
            if (initial == from) { initial = to; }
        }
    }
    // Whether only proved cell transport uses `cell`, one step per use;
    // nullopt when the budget stops.
    std::optional<bool> transportOnlyCell(mlir::Value cell) {
        for (mlir::Operation * user : cell.getUsers()) {
            if (!step()) { return std::nullopt; }
            if (!cellOperations.contains(user)) { return false; }
        }
        return true;
    }
    // Record a plain Map construction and its constructor load.
    void adoptMap(ctjs::ConstructOp made) {
        maps.insert(made.getResult());
        mapOperations.insert(made);
        mapOperations.insert(made.getCallee().getDefiningOp());
    }
    // Erase a rooted Map operation and its record.
    void eraseMapOperation(mlir::Operation * op) {
        mapOperations.erase(op);
        eraseRooted(op);
    }
    // Each class value's unique initialization call; null when initialized twice.
    bool completedClasses(llvm::DenseMap<mlir::Value, ctjs::CallOp> & completed);
    // The closed function a recorded helper call invokes, or null. `throughCells`
    // resolves the callee through cell transport first.
    ctjs::FuncOp helperCallTarget(mlir::Operation * op, bool throughCells = false);
    static bool undefined(mlir::Value value) { return ctjs::isUndefinedConstant(value); }
    ctjs::FuncOp target(ctjs::CreateClosureOp closure);
    bool selectParameterDefaults(ctjs::FuncOp entry);
    bool scalarizeConditionalCells(ctjs::FuncOp entry);
    bool proveCells(ctjs::FuncOp entry);
    bool proveMaps();
    mlir::Value sourceValue(mlir::Value value);
    llvm::SmallVector<mlir::OpOperand *> sourceUses(mlir::Value value);
    bool copyPrimitiveCapture(ctjs::LoadUpvalueOp load, ctjs::ConstantOp literal);
    bool fixedCaptureSlot(ctjs::CreateClosureOp closure, unsigned slot, unsigned depth = 0);
    bool fixedCallbackCell(ctjs::CreateCellOp cell);
    ctjs::ConstantOp callbackPrimitive(ctjs::CreateClosureOp closure, unsigned slot,
                                       unsigned depth = 0);
    bool fixedCallbackCaptures(ctjs::CreateClosureOp callback, ctjs::FuncOp function);
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
    // One candidate outer Map of normalizeNestedMaps.
    bool normalizeNestedMap(ctjs::FuncOp scope, const HostContract & contract,
                            ctjs::ConstructOp map);
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
    // The primitive or element actual an exact captured helper returns for
    // primitive and element actuals.
    std::optional<partial_eval::value> evaluateHelper(ctjs::FuncOp fn,
                                                      llvm::ArrayRef<partial_eval::value> actuals,
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
                              llvm::SmallVectorImpl<ctjs::SetPropertyOp> & definitions,
                              bool perLeaf, bool proofOnly = false);
    bool cloneReceiverGetters(mlir::Value leaf,
                              const llvm::StringMap<ctjs::DefineAccessorOp> & environment,
                              const llvm::StringSet<> & shadowed);
    bool staticGetters(const llvm::StringMap<ctjs::DefineAccessorOp> & definitions,
                       llvm::ArrayRef<ctjs::GetPropertyOp> reads, ctjs::CallOp helper);

    bool proveHeritage(const HostContract & contract);
    // Exact local `C.k(args)` whose static body builds through `this`: that
    // body at the call site, with C for `this` - StaticFactories.cpp.
    bool expandStaticFactories();
    // Static methods no read reaches never run; remove them before any proof.
    bool dropUnreadStatics();
    // A dropped source call `holder.key(...)`: its slot may have lost its last call.
    void decideSlot(mlir::Value holder, ctjs::GetPropertyOp read);
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
    // prove's phases: record exact helper calls, decide which helpers never
    // run, then the complete source census of every remaining operation.
    bool recordHelperCalls(ctjs::FuncOp entry, bool domEntry);
    bool decideUncalledHelpers(const HostContract & contract, bool domEntry);
    bool censusSource(const HostContract & contract, bool domEntry, ctjs::FuncOp entry,
                      ctjs::FuncOp declaration);
    bool proveDOMDataFamily(const HostContract & contract);
    bool forwardDOMDataFields(const HostContract & contract);
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
