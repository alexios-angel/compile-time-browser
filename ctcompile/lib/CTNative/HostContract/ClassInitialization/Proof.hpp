//===- ClassInitialization/Proof.hpp - Class setup proof state --*- C++ -*-===//
//
// Shared state and small helpers for the class-initialization proof. The proof
// shows that a module's class setup (constructors, methods, getters, helper
// calls, captured cells and Maps) has one fixed outcome, and the rewrite then
// replaces that setup with direct code. The member functions are defined across
// the .cpp files in this directory.
//
// Terms used throughout:
//   - leaf: a subclass that has no subclasses of its own.
//   - fold: inline a base constructor's body into a subclass constructor.
//   - census: the check that every remaining operation is a known effect.
//   - publication: a constructor storing `this` in a captured Map.
//   - transport: passing a value through captured cells or upvalues.
//
//===----------------------------------------------------------------------===//

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

/// Own-property keys a JavaScript function carries besides its prototype. The
/// size check is a fast reject: "arguments" (9 characters) is the longest.
inline bool closureMetadataKey(llvm::StringRef key) {
    static const llvm::StringSet<> names{"name", "length", "__home", "caller", "arguments"};
    return key.size() <= 9 && names.contains(key);
}

/// Work-budget cost charged up front for cloning `op`: one unit for the
/// operation plus one per operand and result.
inline uint64_t cloneCost(mlir::Operation * op) {
    return uint64_t(1) + op->getNumOperands() + op->getNumResults();
}

/// Copies `binding` as a closed-source contract that declares only the class
/// helper intrinsics and Error.
inline HostContract classBindingContract(HostContract binding) {
    binding.provider = HostContract::Provider::closedSource;
    binding.elementParameters.clear();
    binding.currentDocumentParameter.reset();
    llvm::erase_if(binding.initialIntrinsics, [](const auto & name) {
        return !host_detail::classIntrinsicArity(name) && name != "Error";
    });
    return binding;
}

/// Whether `input` is an explicit parameter of the DOM entry function `scope`
/// that the contract declares to be an element.
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

/// A recorded value, including unknown, or a literal when no value was recorded.
inline partial_eval::value boundOrLiteral(
    mlir::Value input, const llvm::DenseMap<mlir::Value, partial_eval::value> & values) {
    using partial_eval::value;
    if (auto found = values.find(input); found != values.end()) { return found->second; }
    if (auto constant = input.getDefiningOp<ctjs::ConstantOp>()) {
        return value::primitive(constant.getValue());
    }
    if (auto constant = input.getDefiningOp<mlir::arith::ConstantOp>()) {
        return value::primitive(constant.getValue());
    }
    return value{};
}

/// Whether `closure` captures a slot of an enclosing activation (any
/// nonnegative enclosing index).
inline bool hasOuterCapture(ctjs::CreateClosureOp closure) {
    auto indices = closure.getEnclosingIndicesAttr();
    return indices && llvm::any_of(indices.asArrayRef(), [](int32_t index) { return index >= 0; });
}

/// Replaces a proved closure's capture slots with `captures`, which are all
/// local cells after the proof.
inline void setCaptures(ctjs::CreateClosureOp closure, ctjs::FuncOp function,
                        mlir::ValueRange captures) {
    closure.getUpvaluesMutable().assign(captures);
    closure.removeEnclosingIndicesAttr();
    function.setUpvalueCount(static_cast<uint32_t>(captures.size()));
}

/// Creates an undefined constant at the builder's insertion point.
inline ctjs::ConstantOp undefinedAt(mlir::OpBuilder & at, mlir::Location where) {
    return ctjs::ConstantOp::create(at, where, ctjs::UndefinedAttr::get(at.getContext()));
}

/// The region an index switch selects for `key`: its last equal case, else the
/// default. Calls `step` once per case and returns null when `step` refuses.
inline mlir::Region * switchArm(mlir::scf::IndexSwitchOp branch, int64_t key,
                                llvm::function_ref<bool()> step) {
    auto * region = &branch.getDefaultRegion();
    for (auto [i, value] : llvm::enumerate(branch.getCases())) {
        if (!step()) { return nullptr; }
        if (value == key) { region = &branch.getCaseRegions()[i]; }
    }
    return region;
}

/// Whether `load`'s slot is one of `closure`'s captures.
inline bool capturesSlot(ctjs::LoadUpvalueOp load, ctjs::CreateClosureOp closure) {
    return load.getIndex() >= 0 &&
           static_cast<size_t>(load.getIndex()) < closure.getUpvalues().size();
}

/// Whether every user of `operation` is a ctjs.root (it is otherwise unused).
inline bool rootedOnly(mlir::Operation * operation) {
    return llvm::all_of(operation->getUsers(), llvm::IsaPred<ctjs::RootOp>);
}

/// Work-budget cost charged up front for closedDeclaration's scan of `op`
/// and its uses.
inline uint64_t declarationCost(mlir::Operation * op) {
    return uint64_t(2) + op->getNumOperands();
}

/// State for proving that a module's class setup (constructors, methods,
/// getters, helper calls, captured cells and Maps) can be evaluated at compile
/// time and rewritten away. `remaining` is the work budget: every step() spends
/// one unit, and running out refuses the proof with `reason` set. prove() runs
/// the proof; rewrite() applies it.
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
    /// Class value to the call that sets up its `extends` link. A MapVector so
    /// that scans, which may stop early when the budget runs out, charge work
    /// in source order rather than pointer order.
    llvm::MapVector<mlir::Value, ctjs::CallOp> heritage;
    llvm::DenseSet<mlir::Value> baseClasses;
    llvm::DenseMap<mlir::Value, llvm::SmallVector<ctjs::SetPropertyOp>> inheritedMethods;
    llvm::DenseMap<mlir::Value, llvm::StringMap<ctjs::DefineAccessorOp>> inheritedGetters;
    /// Per derived class: copies of inherited getters whose receiver reads reach
    /// a key that class shadows, proved and expanded in its own environment.
    llvm::DenseMap<mlir::Value, llvm::StringMap<ctjs::FuncOp>> receiverGetters;
    llvm::DenseMap<mlir::Value, llvm::SmallVector<llvm::StringRef>> snapshotFields;
    llvm::DenseSet<mlir::Operation *> snapshotMethods;
    /// Bases constructed only through their subclasses' super calls. Their
    /// snapshots (calls and methods) are taken by the first subclass that
    /// inlines the base constructor.
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
    /// Shadowed method bodies that keep private slots only until their typed
    /// probes pass.
    llvm::DenseSet<mlir::Operation *> proofOnlyMethods;
    bool needsDOMMethodProof = false;
    llvm::DenseSet<mlir::Operation *> helpers;
    llvm::DenseSet<mlir::Operation *> helperCalls;
    /// Helpers and holder slots named by a call in a branch arm that was
    /// decided away, and helpers no running body calls. The census skips them
    /// and rewrite() erases them.
    llvm::SetVector<mlir::Operation *> decidedHelpers;
    llvm::DenseSet<mlir::Operation *> uncalledHelpers;
    /// Constructors of bases that are only built through copies inlined into
    /// subclass constructors; the originals never run.
    llvm::DenseSet<mlir::Operation *> leafBuilt;
    llvm::DenseSet<mlir::Operation *> domEntryHelpers;
    /// DOM entries: each declared element parameter and its index; evaluateHelper
    /// refers to that element by a partial_eval reference to the index.
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
    /// Cells that stay real shared storage rather than fixed values. The
    /// ordinary closure lifter and the DOM Boolean proof handle the bodies
    /// that keep them.
    llvm::DenseSet<mlir::Value> booleanCells;
    llvm::DenseSet<mlir::Operation *> booleanLeaves;
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
    /// Spends `count` steps at once. If they do not fit, spends what is left and
    /// fails, as `count` separate step() calls would.
    bool spend(uint64_t count);
    /// Charges `cost` for every operation under `root` before any work is done.
    /// On failure, what was spent before the budget ran out stays spent.
    bool spendWalk(mlir::Operation * root, llvm::function_ref<uint64_t(mlir::Operation *)> cost);
    bool refuse(llvm::StringRef message);
    mlir::WalkResult refuseWalk(llvm::StringRef message) {
        refuse(message);
        return mlir::WalkResult::interrupt();
    }
    /// Walks `root`'s operations of `visit`'s argument type, one step before
    /// each visit. False when the budget or `visit` interrupts.
    template <mlir::WalkOrder Order = mlir::WalkOrder::PostOrder, class Visit>
    bool walkSteps(mlir::Operation * root, Visit && visit) {
        using Op = mlir::detail::first_argument<std::remove_reference_t<Visit>>;
        return !root->walk<Order>([&](Op op) -> mlir::WalkResult {
                        if (!step()) { return mlir::WalkResult::interrupt(); }
                        return visit(op);
                    })
                    .wasInterrupted();
    }
    /// Points every cell whose recorded initial value is `from` at `to`.
    void retargetCells(mlir::Value from, mlir::Value to) {
        for (mlir::Value & initial : llvm::make_second_range(cells)) {
            if (initial == from) { initial = to; }
        }
    }
    /// Whether every use of `cell` is a proved cell operation, one step per
    /// use; nullopt when the budget runs out.
    std::optional<bool> transportOnlyCell(mlir::Value cell) {
        for (mlir::Operation * user : cell.getUsers()) {
            if (!step()) { return std::nullopt; }
            if (!cellOperations.contains(user)) { return false; }
        }
        return true;
    }
    /// Records a plain Map construction and its constructor load.
    void adoptMap(ctjs::ConstructOp made) {
        maps.insert(made.getResult());
        mapOperations.insert(made);
        mapOperations.insert(made.getCallee().getDefiningOp());
    }
    /// Erases a rooted Map operation and its record.
    void eraseMapOperation(mlir::Operation * op) {
        mapOperations.erase(op);
        eraseRooted(op);
    }
    /// Fills `completed` with each class value's unique initialization call;
    /// false when a class is initialized twice.
    bool completedClasses(llvm::DenseMap<mlir::Value, ctjs::CallOp> & completed);
    /// The closed function a recorded helper call invokes, or null.
    /// `throughCells` first follows the callee through captured cells.
    ctjs::FuncOp helperCallTarget(mlir::Operation * op, bool throughCells = false);
    static bool undefined(mlir::Value value) { return ctjs::isUndefinedConstant(value); }
    ctjs::FuncOp target(ctjs::CreateClosureOp closure);
    bool selectParameterDefaults(ctjs::FuncOp entry);
    bool retainBooleanCaptures(ctjs::FuncOp owner);
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
    /// Handles one candidate outer Map for normalizeNestedMaps().
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
    /// `unconstructed` marks the original body of a base that only subclasses
    /// construct.
    bool fieldsOnly(mlir::Value object, const llvm::StringSet<> & methodKeys,
                    llvm::SmallVectorImpl<ctjs::GetPropertyOp> & staticReads,
                    bool methodsAvailable = false, bool unconstructed = false);
    bool retainedMapAliases(mlir::OpOperand & use, llvm::SmallVectorImpl<mlir::Value> & reads);
    bool retainedMapReads(mlir::OpOperand & use, const llvm::StringSet<> & methodKeys,
                          llvm::SmallVectorImpl<ctjs::GetPropertyOp> & staticReads);

    bool foldDecidedArms(ctjs::CreateClosureOp constructor,
                         llvm::ArrayRef<ctjs::ConstructOp> instances);
    /// Evaluates a captured helper at compile time for primitive and element
    /// arguments; returns the primitive or element it returns, or nothing.
    std::optional<partial_eval::value> evaluateHelper(ctjs::FuncOp fn,
                                                      llvm::ArrayRef<partial_eval::value> actuals,
                                                      unsigned depth);
    bool freshFunctionIndex(unsigned & next);
    /// Appends a copy of `fn` to the module as source function `index`.
    ctjs::FuncOp cloneFunction(ctjs::FuncOp fn, unsigned index);
    /// The ancestor of `op` that lies in `block`, climbing only through parents
    /// of the `Parents` types, one step each. Returns the operation reached when
    /// the chain leaves those types or the budget runs out.
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
    /// Inlines each local `C.k(args)` whose static body builds through `this`,
    /// with C for `this`. Defined in StaticFactories.cpp.
    bool expandStaticFactories();
    /// Removes static methods that no read reaches (they never run) before any
    /// proof.
    bool dropUnreadStatics();
    /// Called after a source call `holder.key(...)` is dropped: that slot may
    /// have lost its last call.
    void decideSlot(mlir::Value holder, ctjs::GetPropertyOp read);
    bool proveInstanceOf(const HostContract & contract);

    bool heritageUse(mlir::OpOperand & use, mlir::Value constructor);

    /// Inlines an explicit super(...) call to a local base constructor.
    // ponytail: only explicit super statements over local bases. Bases with
    // fields, replacement returns or default rest/apply need their own proof.
    bool normalizeSuper(ctjs::CreateClosureOp constructor, ctjs::CreateClosureOp baseClosure,
                        const HostContract & contract);

    bool normalizeSuperMethods(ctjs::FuncOp function,
                               llvm::ArrayRef<ctjs::SetPropertyOp> baseDefinitions);
    /// Drops the helper-call, Map and capture records of a replaced body.
    void forgetRecords(mlir::Region & region);

    bool examine(ctjs::CallOp call, const HostContract & contract, bool domEntry);

    /// Runs the whole proof; false with `reason` set when it refuses.
    bool prove(const HostContract & contract, bool domEntry = false);
    /// prove() runs these in order: record the exact helper calls, decide which
    /// helpers never run, then check every remaining operation (the census).
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
    /// Erases a fixed cell with its recorded reads and remaining transport users.
    void retireCell(mlir::Value cell);
    /// Erases a helper closure whose calls were all expanded, and its function.
    void retireHelper(ctjs::CreateClosureOp closure, ctjs::FuncOp function);
    llvm::SmallVector<ctjs::FuncOp> expandHolders();

    /// Applies a successful proof to the IR. Defined in Rewrite.cpp.
    void rewrite();
};

} // namespace ctcompile::ctnative::class_detail
