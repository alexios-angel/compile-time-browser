#include "../Lowering/Exceptions/Recovery.h"
#include "Analysis.h"
#include "Preparation.h"
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Transforms/Passes.h"
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

namespace ctcompile::ctnative {
#define GEN_PASS_DEF_CTNATIVESPECIALIZECLASSINITIALIZATION
#include "ctcompile/CTNative/Transforms/Passes.h.inc"
namespace {

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
    llvm::MapVector<mlir::Value, mlir::Value> cells;
    llvm::DenseSet<mlir::Operation *> cellOperations;
    llvm::SmallVector<ctjs::CellGetOp> cellReads;
    llvm::SmallVector<ctjs::LoadUpvalueOp> captureReads;
    llvm::DenseMap<mlir::Value, mlir::Value> holderCaptures;
    llvm::DenseMap<mlir::Operation *, ctjs::FuncOp> callableCaptures;
    llvm::SetVector<mlir::Operation *> capturedHelpers;
    llvm::SmallVector<ctjs::CreateClosureOp> capturedClosures;
    llvm::SetVector<mlir::Operation *> dispatchMethods;
    llvm::SmallVector<std::pair<ctjs::FuncOp, mlir::OwningOpRef<ctjs::FuncOp>>> normalizedMethods;

    classInitialization(mlir::ModuleOp module, unsigned steps) : module(module), remaining(steps) {}

    bool step() {
        if (!remaining) { return refuse("class initialization work budget exhausted"); }
        --remaining;
        return true;
    }
    bool refuse(llvm::StringRef message) {
        if (reason.empty()) { reason = message.str(); }
        return false;
    }
    static bool undefined(mlir::Value value) {
        auto constant = value.getDefiningOp<ctjs::ConstantOp>();
        return constant && llvm::isa<ctjs::UndefinedAttr>(constant.getValue());
    }
    ctjs::FuncOp target(ctjs::CreateClosureOp closure) {
        if (!closure || closure.getFunction() < 0) { return {}; }
        // Numeric function indices belong to the enclosing source program.
        auto scope = closure->getParentOfType<ctjs::FuncOp>();
        if (!scope || scope.getBody().empty() || scope.getBody().front().getNumArguments() < 3 ||
            closure.getEnclosingClosure() !=
                scope.getBody().front().getArgument(ctjs::arg_callee)) {
            return {};
        }
        return functions.lookup(static_cast<unsigned>(closure.getFunction()));
    }
    bool proveCells(ctjs::FuncOp entry) {
        // The importer boxes entry locals when one is captured. Only fixed,
        // ordered bindings are transport; their original producers stay live.
        for (ctjs::CreateCellOp cell : entry.getBody().front().getOps<ctjs::CreateCellOp>()) {
            if (!step()) { return false; }
            if (cells.count(cell.getResult())) { continue; }
            ctjs::CellSetOp first;
            const auto readPosition = [&](mlir::Operation * op) {
                if (llvm::isa<ctjs::CellGetOp>(op)) {
                    while (op->getBlock() != cell->getBlock() &&
                           llvm::isa_and_nonnull<mlir::scf::IfOp>(op->getParentOp())) {
                        if (!step()) { break; }
                        op = op->getParentOp();
                    }
                }
                return op;
            };
            for (mlir::OpOperand & use : cell.getResult().getUses()) {
                if (!step()) { return false; }
                auto * op = use.getOwner();
                auto * position = readPosition(op);
                if (position->getBlock() != cell->getBlock() || !cell->isBeforeInBlock(position)) {
                    return refuse("class local cell has a nonlocal or unordered use");
                }
                if (auto write = llvm::dyn_cast<ctjs::CellSetOp>(op)) {
                    if (use.getOperandNumber() != 0 ||
                        (first && first.getValue() != write.getValue())) {
                        return refuse("class local cell has changing writes");
                    }
                    if (!first || write->isBeforeInBlock(first)) { first = write; }
                }
            }
            for (mlir::OpOperand & use : cell.getResult().getUses()) {
                if (!step()) { return false; }
                auto * op = use.getOwner();
                if (llvm::isa<ctjs::RootOp, ctjs::CellSetOp>(op)) {
                    cellOperations.insert(op);
                    continue;
                }
                // A fixed cell may be read in a later short-circuit arm. All
                // writes/captures still belong to its original ordered block.
                if (first && !first->isBeforeInBlock(readPosition(op))) {
                    return refuse("class local cell is observed before initialization");
                }
                if (auto read = llvm::dyn_cast<ctjs::CellGetOp>(op);
                    read && use.getOperandNumber() == 0) {
                    cellReads.push_back(read);
                    cellOperations.insert(op);
                    continue;
                }
                auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(op);
                if (!first || !closure || use.getOperandNumber() < 2) {
                    return refuse("class local cell escapes its fixed reads and captures");
                }
                // The exact class method and every captured read are checked
                // below. No general immutable-capture rule is widened here.
            }
            cells[cell.getResult()] = first ? first.getValue() : cell.getInitial();
            cellOperations.insert(cell);
        }
        for (auto & [cell, value] : cells) {
            (void)cell;
            value = sourceValue(value);
        }
        return reason.empty();
    }
    mlir::Value sourceValue(mlir::Value value) {
        if (auto holder = holderCaptures.lookup(value)) { return holder; }
        while (auto read = value.getDefiningOp<ctjs::CellGetOp>()) {
            if (!step()) { return value; }
            auto found = cells.find(read.getCell());
            if (found == cells.end()) { return value; }
            value = found->second;
        }
        return value;
    }
    llvm::SmallVector<mlir::OpOperand *> sourceUses(mlir::Value value) {
        llvm::SmallVector<mlir::OpOperand *> result;
        llvm::SmallVector<mlir::Value> pending{value};
        llvm::DenseSet<mlir::Value> seen;
        while (!pending.empty()) {
            auto current = pending.pop_back_val();
            if (!seen.insert(current).second) { continue; }
            for (auto [read, object] : holderCaptures) {
                if (!step()) { return {}; }
                if (object == current) { pending.push_back(read); }
            }
            for (mlir::OpOperand & use : current.getUses()) {
                if (!step()) { return {}; }
                auto * op = use.getOwner();
                if (cellOperations.contains(op) &&
                    llvm::isa<ctjs::CreateCellOp, ctjs::CellSetOp>(op)) {
                    auto write = llvm::dyn_cast<ctjs::CellSetOp>(op);
                    mlir::Value cell = write ? mlir::Value(write.getCell()) : op->getResult(0);
                    for (mlir::OpOperand & cellUse : cell.getUses()) {
                        if (!step()) { return {}; }
                        if (auto read = llvm::dyn_cast<ctjs::CellGetOp>(cellUse.getOwner());
                            read && cells.lookup(cell) == sourceValue(current)) {
                            pending.push_back(read.getResult());
                        }
                    }
                    continue;
                }
                result.push_back(&use);
            }
        }
        return result;
    }
    bool helperCallback(mlir::OpOperand & use) {
        auto callback = llvm::dyn_cast<ctjs::CreateClosureOp>(use.getOwner());
        auto function = target(callback);
        if (!callback || use.getOperandNumber() != 0 || !function ||
            !callback.getUpvalues().empty() || function.getUpvalueCount() != 0 ||
            llvm::any_of(
                function.getBody().front().getArguments().take_front(ctjs::implicit_arguments),
                [](mlir::BlockArgument argument) { return !argument.use_empty(); })) {
            return refuse("captured helper observes its implicit callee");
        }
        for (mlir::OpOperand & callbackUse : callback.getResult().getUses()) {
            if (!step()) { return false; }
            if (llvm::isa<ctjs::RootOp>(callbackUse.getOwner())) { continue; }
            auto call = llvm::dyn_cast<ctjs::CallOp>(callbackUse.getOwner());
            auto read = call ? call.getCallee().getDefiningOp<ctjs::GetPropertyOp>()
                             : ctjs::GetPropertyOp{};
            const bool replacement = call && read && callbackUse.getOperandNumber() == 3 &&
                                     call.getArgs().size() == 2 &&
                                     ctjs::constantKey(read.getKey()) == "replace";
            const bool filter =
                call && read && callbackUse.getOperandNumber() == 2 && call.getArgs().size() == 1 &&
                ctjs::constantKey(read.getKey()) == "filter" &&
                function.getBody().front().getNumArguments() == ctjs::implicit_arguments + 1 &&
                call->getBlock() == callback->getBlock() && callback->isBeforeInBlock(call);
            if ((!replacement && !filter) || read.getObject() != call.getReceiver()) {
                return refuse("captured helper callback escapes its intrinsic call");
            }
        }
        // Retain the original callback for the shared no-match or typed
        // Array filter proof, including its complete body and uses.
        helpers.insert(function);
        domEntryHelpers.insert(function);
        return true;
    }
    bool helperCallbacks(ctjs::FuncOp helper) {
        for (mlir::OpOperand & use :
             helper.getBody().front().getArgument(ctjs::arg_callee).getUses()) {
            if (!step() || !helperCallback(use)) { return false; }
        }
        return true;
    }
    bool methodCaptures(ctjs::CreateClosureOp method, ctjs::CreateClosureOp constructor,
                        llvm::SmallVectorImpl<ctjs::GetPropertyOp> & reads, bool domEntry) {
        auto fn = target(method);
        const auto constructorValue = constructor ? constructor.getResult() : mlir::Value{};
        if (fn.getUpvalueCount() != static_cast<int64_t>(method.getUpvalues().size()) ||
            (method.getEnclosingIndicesAttr() &&
             llvm::any_of(method.getEnclosingIndicesAttr().asArrayRef(),
                          [](int32_t index) { return index >= 0; }))) {
            return refuse("class method lacks exact local capture slots");
        }
        for (mlir::Value capture : method.getUpvalues()) {
            if (!step() || !cells.count(capture)) {
                return refuse("class method capture lacks a fixed local cell");
            }
            auto value = sourceValue(cells.lookup(capture));
            if (value == constructorValue) { continue; }
            if (auto object = value.getDefiningOp<ctjs::CreateObjectOp>();
                domEntry && constructor && object) {
                if (object->getBlock() != method->getBlock() || !object->isBeforeInBlock(method)) {
                    return refuse("captured holder lacks ordered local initialization");
                }
                // The shared holder census checks every slot and alias below.
                // All original slots must already exist at closure creation.
                for (mlir::Operation * use : object->getUsers()) {
                    if (!step()) { return false; }
                    if (llvm::isa<ctjs::SetPropertyOp>(use) &&
                        (use->getBlock() != method->getBlock() || !use->isBeforeInBlock(method))) {
                        return refuse("captured holder slot changes after capture");
                    }
                }
                needsDOMMethodProof = true;
                continue;
            }
            auto closure = value.getDefiningOp<ctjs::CreateClosureOp>();
            auto helper = target(closure);
            if (!domEntry || !helper || !closure.getUpvalues().empty() ||
                helper.getUpvalueCount() != 0 || !undefined(closure.getEnclosingThis()) ||
                closure->getBlock() != method->getBlock() || !closure->isBeforeInBlock(method)) {
                return refuse(
                    "class method capture is not its constructor or an inert sibling helper");
            }
            auto & body = helper.getBody().front();
            if (!helperCallbacks(helper)) { return false; }
            if (!unusedReceiver(helper) || !body.getArgument(ctjs::arg_new_target).use_empty()) {
                return refuse("captured helper observes its implicit receiver or new.target");
            }
            helpers.insert(helper);
            domEntryHelpers.insert(helper);
            capturedHelpers.insert(closure);
            needsDOMMethodProof = true;
        }
        for (mlir::OpOperand & use : fn.getBody().front().getArgument(ctjs::arg_callee).getUses()) {
            if (!step()) { return false; }
            if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
            if (domEntry && llvm::isa<ctjs::CreateClosureOp>(use.getOwner())) {
                // Captures and callback enclosures share this original callee.
                // Prove each use separately; retain the callback's body and
                // identity for the complete typed DOM proof after rewriting.
                if (!helperCallback(use)) { return false; }
                needsDOMMethodProof = true;
                continue;
            }
            auto load = llvm::dyn_cast<ctjs::LoadUpvalueOp>(use.getOwner());
            if (!load || use.getOperandNumber() != 0 || load.getIndex() < 0 ||
                static_cast<size_t>(load.getIndex()) >= method.getUpvalues().size()) {
                return refuse("class method observes or changes its captured identity");
            }
            auto value = sourceValue(cells.lookup(method.getUpvalues()[load.getIndex()]));
            if (value.getDefiningOp<ctjs::CreateObjectOp>()) {
                holderCaptures[load.getResult()] = value;
                captureReads.push_back(load);
                continue; // Every original use goes through the shared holder census.
            }
            auto helper = value != constructorValue
                              ? target(value.getDefiningOp<ctjs::CreateClosureOp>())
                              : ctjs::FuncOp{};
            for (mlir::OpOperand & selected : load.getResult().getUses()) {
                if (!step()) { return false; }
                if (llvm::isa<ctjs::RootOp>(selected.getOwner())) { continue; }
                if (helper) {
                    auto call = llvm::dyn_cast<ctjs::CallOp>(selected.getOwner());
                    if (!call || selected.getOperandNumber() != 0 ||
                        !undefined(call.getReceiver()) ||
                        call.getArgs().size() + ctjs::implicit_arguments >
                            helper.getBody().front().getNumArguments()) {
                        return refuse("captured helper escapes its ordinary local call");
                    }
                    for (auto i = call.getArgs().size() + ctjs::implicit_arguments;
                         i < helper.getBody().front().getNumArguments(); ++i) {
                        if (!step()) { return false; }
                    }
                    helperCalls.insert(call);
                    continue;
                }
                auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(selected.getOwner());
                if (!read || selected.getOperandNumber() != 0 ||
                    !ctjs::ordinaryKey(read.getKey())) {
                    return refuse("captured class identity escapes its local getter read");
                }
                reads.push_back(read);
            }
            captureReads.push_back(load);
            if (helper) { callableCaptures[load] = helper; }
        }
        if (!method.getUpvalues().empty()) { capturedClosures.push_back(method); }
        return true;
    }
    ctjs::CreateClosureOp sourceClosure(mlir::Value value, bool domEntry = false) {
        if (auto closure = value.getDefiningOp<ctjs::CreateClosureOp>()) { return closure; }
        if (auto read = value.getDefiningOp<ctjs::GetPropertyOp>()) {
            if (auto closure = holderReads.lookup(value)) { return closure; }
            const bool localAliases = domEntry || holderCaptures.count(read.getObject());
            auto object = (localAliases ? sourceValue(read.getObject()) : read.getObject())
                              .getDefiningOp<ctjs::CreateObjectOp>();
            auto load = read.getObject().getDefiningOp<ctjs::LoadGlobalOp>();
            auto publication = load ? globals.lookup(load.getName()) : ctjs::StoreGlobalOp{};
            if (!object && !publication) { return {}; }
            const bool domLocal = object && localAliases;
            auto proof = domLocal ? analyzeLocalCallableObject(
                                        object, [&] { return step(); },
                                        [&](mlir::Value value) { return sourceUses(value); })
                         : object
                             ? analyzeLocalCallableObject(object, [&] { return step(); })
                             : analyzeGlobalCallableObject(publication, [&] { return step(); });
            if (!proof) {
                llvm::consumeError(proof.takeError());
                return {};
            }
            for (ctjs::SetPropertyOp store : proof->stores) {
                if (!step() || !ctjs::ordinaryKey(store.getKey())) { return {}; }
                auto closure = store.getValue().getDefiningOp<ctjs::CreateClosureOp>();
                auto fn = target(closure);
                if (!fn) { return {}; }
                auto & block = fn.getBody().front();
                if (domLocal) {
                    if (closure.getUpvalues().empty()) {
                        if (fn.getUpvalueCount() != 0 || !helperCallbacks(fn)) { return {}; }
                    } else {
                        // Reuse the exact sibling-function proof. A holder has
                        // no constructor identity or authority for object captures.
                        llvm::SmallVector<ctjs::GetPropertyOp> unusedReads;
                        if (!methodCaptures(closure, {}, unusedReads, true)) { return {}; }
                    }
                    if (!unusedReceiver(fn) ||
                        !block.getArgument(ctjs::arg_new_target).use_empty()) {
                        return {};
                    }
                } else if (!closure.getUpvalues().empty() ||
                           !block.getArgument(ctjs::arg_receiver).use_empty() ||
                           !block.getArgument(ctjs::arg_new_target).use_empty() ||
                           !block.getArgument(ctjs::arg_callee).use_empty()) {
                    return {};
                }
                for (mlir::OpOperand & use : closure.getResult().getUses()) {
                    if (!step()) { return {}; }
                    if (!llvm::isa<ctjs::RootOp>(use.getOwner()) &&
                        !(use.getOwner() == store && use.getOperandNumber() == 2)) {
                        return {};
                    }
                }
            }
            ctjs::CreateClosureOp selected;
            for (auto [loaded, closure] : proof->reads) {
                if (auto alias = loaded.getObject().getDefiningOp<ctjs::CellGetOp>()) {
                    for (ctjs::SetPropertyOp store : proof->stores) {
                        if (!step() || store->getBlock() != alias->getBlock() ||
                            !store->isBeforeInBlock(alias)) {
                            return {};
                        }
                    }
                }
                for (mlir::OpOperand & use : loaded.getResult().getUses()) {
                    if (!step()) { return {}; }
                    if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
                    auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                    if (!call || use.getOperandNumber() != 0 ||
                        call.getReceiver() != loaded.getObject()) {
                        return {};
                    }
                    if (publication || domLocal) {
                        const auto parameters =
                            target(closure).getBody().front().getNumArguments() -
                            ctjs::implicit_arguments;
                        if (call.getArgs().size() > parameters) { return {}; }
                        for (auto i = call.getArgs().size(); i < parameters; ++i) {
                            if (!step()) { return {}; }
                        }
                    }
                }
                if (loaded == read) { selected = closure; }
            }
            // Identity and receiver proof only. Local DOM slots remain intact
            // for the invocation/type census; other holders keep the source
            // effect census below, including every unused slot.
            for (ctjs::SetPropertyOp store : proof->stores) {
                auto fn = target(store.getValue().getDefiningOp<ctjs::CreateClosureOp>());
                helpers.insert(fn);
                if (domLocal) {
                    // Keep every original local slot for DOM helper expansion.
                    // That proof refuses a slot without a source invocation;
                    // only actual calls can supply its argument authority.
                    domEntryHelpers.insert(fn);
                    needsDOMMethodProof = true;
                }
            }
            if (publication || domLocal) {
                for (auto [loaded, closure] : proof->reads) {
                    holderReads[loaded.getResult()] = closure;
                }
            }
            if (publication) {
                for (ctjs::LoadGlobalOp loaded : proof->loads) { globalHolderLoads.insert(loaded); }
                globalHolders.try_emplace(publication, std::move(*proof));
            } else if (domLocal) {
                localDOMHolders.try_emplace(object, std::move(*proof));
            }
            return selected;
        }
        auto load = value.getDefiningOp<ctjs::LoadGlobalOp>();
        auto store = load ? globals.lookup(load.getName()) : ctjs::StoreGlobalOp{};
        if (!store) { return {}; }
        auto closure = store.getValue().getDefiningOp<ctjs::CreateClosureOp>();
        if (store->getBlock() == load->getBlock() && store->isBeforeInBlock(load)) {
            return closure;
        }
        auto fn = target(closure);
        if (!fn) { return {}; }
        auto [cached, fresh] = closedGlobals.try_emplace(load.getName(), false);
        if (fresh) {
            // Charge the existing declaration proof's operation/use scans once
            // per binding. Its hoisting and closed-callee rules also cover reads
            // in other functions without trusting resolver annotations.
            auto walked = module.walk([&](mlir::Operation * op) {
                const uint64_t cost = uint64_t(2) + op->getNumOperands();
                if (cost > remaining) {
                    refuse("class initialization work budget exhausted");
                    return mlir::WalkResult::interrupt();
                }
                remaining -= static_cast<unsigned>(cost);
                return mlir::WalkResult::advance();
            });
            cached->second = !walked.wasInterrupted() && closedDeclaration(store, fn, module);
        }
        return cached->second ? closure : ctjs::CreateClosureOp{};
    }
    bool unusedReceiver(ctjs::FuncOp fn) {
        for (mlir::OpOperand & use :
             fn.getBody().front().getArgument(ctjs::arg_receiver).getUses()) {
            if (!step()) { return false; }
            auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(use.getOwner());
            // Saving lexical this is inert when the exact helper never reads it.
            if (!closure || use.getOperandNumber() != 1 || !helpers.contains(target(closure))) {
                return false;
            }
        }
        return true;
    }
    bool fieldsOnly(mlir::Value object, const llvm::StringSet<> & methodKeys,
                    llvm::SmallVectorImpl<ctjs::GetPropertyOp> & staticReads,
                    bool methodsAvailable = false) {
        for (mlir::OpOperand * sourceUse : sourceUses(object)) {
            auto & use = *sourceUse;
            if (!step()) { return false; }
            auto * op = use.getOwner();
            if (llvm::isa<ctjs::RootOp>(op)) { continue; }
            if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
                read && use.getOperandNumber() == 0 &&
                ctjs::constantKey(read.getKey()) == "constructor") {
                // The exact fresh prototype owns this backedge. Its identity
                // may only select a proved local getter; no write or escape.
                for (mlir::OpOperand & selected : read.getResult().getUses()) {
                    if (!step()) { return false; }
                    if (llvm::isa<ctjs::RootOp>(selected.getOwner())) { continue; }
                    auto getter = llvm::dyn_cast<ctjs::GetPropertyOp>(selected.getOwner());
                    if (!getter || selected.getOperandNumber() != 0 ||
                        !ctjs::ordinaryKey(getter.getKey())) {
                        return refuse("class constructor identity escapes its local getter read");
                    }
                    staticReads.push_back(getter);
                }
                constructorReads.push_back(read);
                continue;
            }
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(op); call && methodsAvailable) {
                auto read = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                if (use.getOperandNumber() == 1 && read &&
                    sourceValue(read.getObject()) == sourceValue(object) &&
                    read.getResult().hasOneUse() &&
                    methodKeys.contains(ctjs::constantKey(read.getKey()))) {
                    methodCalls.insert(call);
                    continue;
                }
            }
            mlir::Value key;
            if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) { key = read.getKey(); }
            if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(op)) { key = write.getKey(); }
            if (use.getOperandNumber() != 0 || !key || !ctjs::ordinaryKey(key)) {
                return refuse("class receiver escapes or observes a prototype/descriptor");
            }
            if (methodKeys.contains(ctjs::constantKey(key))) {
                auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
                auto call = read && read.getResult().hasOneUse()
                                ? llvm::dyn_cast<ctjs::CallOp>(*read.getResult().getUsers().begin())
                                : ctjs::CallOp{};
                if (!methodsAvailable || !call || call.getCallee() != read.getResult() ||
                    sourceValue(call.getReceiver()) != sourceValue(object)) {
                    return refuse("class method is observed or shadowed");
                }
            }
        }
        return reason.empty();
    }

    bool staticGetters(const llvm::StringMap<ctjs::DefineAccessorOp> & definitions,
                       llvm::ArrayRef<ctjs::GetPropertyOp> reads, ctjs::CallOp helper) {
        llvm::MapVector<mlir::Operation *, llvm::SmallVector<mlir::Operation *>> dependencies;
        const auto firstRead = getterReads.size();
        const auto resolve = [&](ctjs::GetPropertyOp read) -> ctjs::FuncOp {
            auto definition = definitions.lookup(ctjs::constantKey(read.getKey()));
            if (!ctjs::ordinaryKey(read.getKey()) || !definition) { return {}; }
            return target(definition.getGetter().getDefiningOp<ctjs::CreateClosureOp>());
        };
        for (const auto & item : definitions) {
            // Closure metadata and the compiler's home field precede accessors
            // in the interpreter. Keep their source reads until that boundary
            // has one semantics across the interpreter and native output.
            const auto key = item.first();
            if (key == "name" || key == "length" || key == "__home" || key == "caller" ||
                key == "arguments") {
                return refuse("static getter shadows closure metadata");
            }
            auto definition = item.second;
            auto closure = definition.getGetter().getDefiningOp<ctjs::CreateClosureOp>();
            auto fn = target(closure);
            if (!step() || !fn || !ctjs::ordinaryKey(item.first()) ||
                !undefined(definition.getSetter()) || closure->getBlock() != helper->getBlock() ||
                !closure->isBeforeInBlock(definition) || !undefined(closure.getEnclosingThis()) ||
                !closure.getUpvalues().empty()) {
                return refuse(
                    "static getter needs a unique local capture-free getter without a setter");
            }
            ctjs::SetPropertyOp getterHome;
            for (mlir::OpOperand & use : closure.getResult().getUses()) {
                if (!step()) { return false; }
                if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
                if (use.getOwner() == definition && use.getOperandNumber() == 1) { continue; }
                auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
                if (getterHome || !write || use.getOperandNumber() != 0 ||
                    ctjs::constantKey(write.getKey()) != "__home" ||
                    write.getValue() != helper.getArgs().front() ||
                    write->getBlock() != helper->getBlock() || !closure->isBeforeInBlock(write) ||
                    !write->isBeforeInBlock(helper)) {
                    return refuse("static getter callable identity escapes its definition");
                }
                getterHome = write;
            }
            // The body census below forbids observing this metadata. Keep the
            // exact source assignment in the setup proof before erasing it.
            if (getterHome) { setup.insert(getterHome); }
            auto & entry = fn.getBody().front();
            if (!llvm::hasSingleElement(fn.getBody()) || entry.getNumArguments() != 3 ||
                !entry.getArgument(ctjs::arg_callee).use_empty() ||
                !entry.getArgument(ctjs::arg_new_target).use_empty()) {
                return refuse("static getter observes its callable identity or parameters");
            }
            auto & required = dependencies[fn];
            if (auto thrown = llvm::dyn_cast<ctjs::ThrowOp>(entry.getTerminator())) {
                if (!thrown.getValue().getDefiningOp<ctjs::ConstantOp>() &&
                    !errorOperations.contains(thrown.getOperation())) {
                    return refuse("static getter throw needs a literal or declared Error payload");
                }
                throwingGetters.insert(fn);
            }
            for (mlir::Operation & op : entry) {
                if (!step()) { return false; }
                if (errorOperations.contains(&op) ||
                    (llvm::isa<ctjs::ThrowOp>(op) && throwingGetters.contains(fn))) {
                    continue;
                }
                if (!llvm::isa<ctjs::ConstantOp, ctjs::CreateObjectOp, ctjs::BinaryOp,
                               ctjs::UnaryOp, ctjs::CompareOp, ctjs::TruthyOp, ctjs::FromBoolOp,
                               ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::ReturnOp>(op)) {
                    auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
                    if (!read || read.getObject() != entry.getArgument(ctjs::arg_receiver)) {
                        return refuse("static getter body is not a closed expression");
                    }
                }
            }
            for (mlir::OpOperand & use : entry.getArgument(ctjs::arg_receiver).getUses()) {
                if (!step()) { return false; }
                auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(use.getOwner());
                auto callee = read ? resolve(read) : ctjs::FuncOp{};
                if (use.getOperandNumber() != 0 || !callee) {
                    return refuse("static getter receiver lacks an exact local getter dependency");
                }
                required.push_back(callee);
                getterReads.emplace_back(read, callee);
            }
            getters.insert(fn);
            getterClosures.push_back(closure);
            setup.insert(definition);
        }
        // ponytail: bounded fixpoint over local getters; inherited/dynamic
        // receivers need a separate provenance proof before widening.
        llvm::DenseSet<mlir::Operation *> completed;
        llvm::DenseMap<mlir::Operation *, unsigned> expansion;
        while (completed.size() != dependencies.size()) {
            const auto before = completed.size();
            for (const auto & [fn, required] : dependencies) {
                if (!step()) { return false; }
                if (completed.contains(fn)) { continue; }
                bool ready = true;
                for (mlir::Operation * callee : required) {
                    if (!step()) { return false; }
                    ready &= completed.contains(callee);
                }
                if (!ready) { continue; }
                // A dependency call uses this function's closure argument.
                // Keep its callers too, so no such argument is copied into a
                // different function when expanding a getter chain.
                if (llvm::any_of(required, [&](mlir::Operation * callee) {
                        return throwingGetters.contains(callee);
                    })) {
                    throwingGetters.insert(fn);
                }
                unsigned cost = 0;
                for (mlir::Operation & op : llvm::cast<ctjs::FuncOp>(fn).getBody().front()) {
                    if (!step()) { return false; }
                    if (llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::ReturnOp>(op)) {
                        continue;
                    }
                    auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
                    const unsigned added = read ? expansion.lookup(resolve(read)) : 1;
                    if (cost > remaining || added > remaining - cost) {
                        return refuse("class initialization work budget exhausted");
                    }
                    cost += added;
                }
                if (throwingGetters.contains(fn)) {
                    cost = 3; // Undefined, a fresh capture-free closure, and its direct call.
                    if (cost > remaining) {
                        return refuse("class initialization work budget exhausted");
                    }
                }
                expansion[fn] = cost;
                completed.insert(fn);
                getterOrder.push_back(llvm::cast<ctjs::FuncOp>(fn));
            }
            if (completed.size() == before) { return refuse("static getter dependency cycle"); }
        }
        for (ctjs::GetPropertyOp read : reads) {
            if (!step()) { return false; }
            auto callee = resolve(read);
            if (!callee) { return refuse("constructor read lacks an exact local static getter"); }
            getterReads.emplace_back(read, callee);
        }
        // Charge every clone before any mutation, including repeated reads and
        // the transitive expansion copied from already-expanded getter bodies.
        for (const auto & item : llvm::drop_begin(getterReads, firstRead)) {
            const unsigned cost = expansion.lookup(item.second);
            if (cost > remaining) { return refuse("class initialization work budget exhausted"); }
            remaining -= cost;
        }
        return true;
    }

    // ponytail: immutable local base methods/getters; inheritance needs a complete
    // receiver/home proof before widening.
    bool examine(ctjs::CallOp call, bool domEntry) {
        if (!step() || call.getArgs().size() != 1 || !undefined(call.getReceiver()) ||
            !call.getResult().use_empty()) {
            return refuse("class helper needs one local constructor and an unused result");
        }
        auto closure = call.getArgs().front().getDefiningOp<ctjs::CreateClosureOp>();
        auto function = target(closure);
        if (!function || !undefined(closure.getEnclosingThis()) || !closure.getUpvalues().empty() ||
            closure->getBlock() != call->getBlock() || !closure->isBeforeInBlock(call)) {
            return refuse("class constructor lacks a local ordinary closure identity");
        }
        ctjs::SetPropertyOp attachment, home, backedge;
        llvm::SmallVector<ctjs::ConstructOp> instances;
        llvm::StringMap<ctjs::DefineAccessorOp> staticDefinitions;
        llvm::SmallVector<ctjs::GetPropertyOp> staticReads;
        llvm::SmallVector<ctjs::SetPropertyOp> getterHomes;
        for (mlir::OpOperand * sourceUse : sourceUses(closure.getResult())) {
            auto & use = *sourceUse;
            if (!step()) { return false; }
            auto * op = use.getOwner();
            if (op == call && use.getOperandNumber() == 2) { continue; }
            if (llvm::isa<ctjs::RootOp>(op)) { continue; }
            if (auto definition = llvm::dyn_cast<ctjs::DefineAccessorOp>(op)) {
                if (use.getOperandNumber() != 0 || definition->getBlock() != call->getBlock() ||
                    !definition->isBeforeInBlock(call) ||
                    !staticDefinitions.try_emplace(definition.getName(), definition).second) {
                    return refuse("static accessor setup is repeated or outside initialization");
                }
                continue;
            }
            if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
                if (use.getOperandNumber() != 0 || read->getBlock() != call->getBlock() ||
                    !call->isBeforeInBlock(read)) {
                    return refuse("static getter read lacks completed local initialization");
                }
                staticReads.push_back(read);
                continue;
            }
            if (auto made = llvm::dyn_cast<ctjs::ConstructOp>(op)) {
                if (use.getOperandNumber() >= 2 ||
                    sourceValue(made.getCallee()) != closure.getResult() ||
                    sourceValue(made.getNewTarget()) != closure.getResult() ||
                    made->getBlock() != call->getBlock() || !call->isBeforeInBlock(made)) {
                    return refuse("class construction escapes or lacks exact local new.target");
                }
                if (use.getOperandNumber() == 0) { instances.push_back(made); }
                continue;
            }
            auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(op);
            if (!write || write->getBlock() != call->getBlock() || !write->isBeforeInBlock(call)) {
                return refuse("class constructor has an observable use outside its setup");
            }
            const auto key = ctjs::constantKey(write.getKey());
            if (use.getOperandNumber() == 0 && key == "prototype" && !attachment) {
                attachment = write;
            } else if (use.getOperandNumber() == 0 && key == "__home" && !home) {
                home = write;
            } else if (use.getOperandNumber() == 2 && key == "constructor" && !backedge) {
                backedge = write;
            } else if (use.getOperandNumber() == 2 && key == "__home") {
                getterHomes.push_back(write); // Rechecked against exact getters below.
            } else {
                return refuse("class methods, static fields or repeated setup remain unsupported");
            }
        }
        auto prototype = attachment ? attachment.getValue().getDefiningOp<ctjs::CreateObjectOp>()
                                    : ctjs::CreateObjectOp{};
        if (!prototype || !home || !backedge || instances.empty() ||
            prototype->getBlock() != call->getBlock() || home.getValue() != prototype.getResult() ||
            backedge.getObject() != prototype.getResult()) {
            return refuse("class setup needs its exact fresh prototype, constructor and home");
        }
        const auto firstConstructorRead = constructorReads.size();
        llvm::StringSet<> methodKeys;
        llvm::SmallVector<ctjs::SetPropertyOp> definitions;
        for (mlir::OpOperand & use : prototype.getResult().getUses()) {
            if (!step()) { return false; }
            auto * op = use.getOwner();
            if ((op == attachment || op == home) && use.getOperandNumber() == 2) { continue; }
            if (op == backedge && use.getOperandNumber() == 0) { continue; }
            if (llvm::isa<ctjs::RootOp>(op)) { continue; }
            auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(op);
            if (!write || write->getBlock() != call->getBlock() || !write->isBeforeInBlock(call)) {
                return refuse("class prototype is observed or mutated outside initialization");
            }
            if (use.getOperandNumber() == 2 && ctjs::constantKey(write.getKey()) == "__home") {
                continue; // Rechecked against the exact method closure below.
            }
            if (use.getOperandNumber() != 0 || !ctjs::ordinaryKey(write.getKey()) ||
                !methodKeys.insert(ctjs::constantKey(write.getKey())).second) {
                return refuse("class prototype needs unique ordinary method definitions");
            }
            definitions.push_back(write);
        }
        llvm::DenseSet<mlir::Operation *> methodHomes;
        for (ctjs::SetPropertyOp definition : definitions) {
            auto method = definition.getValue().getDefiningOp<ctjs::CreateClosureOp>();
            auto fn = target(method);
            if (!step() || !fn || method->getBlock() != call->getBlock() ||
                !method->isBeforeInBlock(definition) || !undefined(method.getEnclosingThis())) {
                return refuse("class method needs a local ordinary closure");
            }
            if (!methodCaptures(method, closure, staticReads, domEntry)) { return false; }
            ctjs::SetPropertyOp methodHome;
            for (mlir::OpOperand & use : method.getResult().getUses()) {
                if (!step()) { return false; }
                auto * op = use.getOwner();
                if (op == definition && use.getOperandNumber() == 2) { continue; }
                if (llvm::isa<ctjs::RootOp>(op)) { continue; }
                auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(op);
                if (methodHome || !write || use.getOperandNumber() != 0 ||
                    ctjs::constantKey(write.getKey()) != "__home" ||
                    write.getValue() != prototype.getResult() ||
                    write->getBlock() != call->getBlock() || !write->isBeforeInBlock(call)) {
                    return refuse("class method identity or lexical home escapes initialization");
                }
                methodHome = write;
            }
            auto & block = fn.getBody().front();
            if (!methodHome || !block.getArgument(ctjs::arg_new_target).use_empty() ||
                !fieldsOnly(block.getArgument(ctjs::arg_receiver), methodKeys, staticReads, true)) {
                return refuse("class method observes its identity, home or an unproved receiver");
            }
            methods.insert(fn);
            methodHomes.insert(methodHome);
            setup.insert(methodHome);
        }
        for (mlir::OpOperand & use : prototype.getResult().getUses()) {
            if (!step()) { return false; }
            if (use.getOperandNumber() == 2 && use.getOwner() != attachment &&
                use.getOwner() != home && !methodHomes.contains(use.getOwner())) {
                return refuse("class prototype reaches an unrelated lexical home");
            }
        }
        for (ctjs::ConstructOp made : instances) {
            if (!fieldsOnly(made.getResult(), methodKeys, staticReads, true)) { return false; }
            for (ctjs::SetPropertyOp definition : definitions) {
                if (!step()) { return false; }
                methodProbes.emplace_back(made, definition);
            }
        }
        if (!definitions.empty() || constructorReads.size() != firstConstructorRead) {
            for (ctjs::ReturnOp returned : function.getBody().front().getOps<ctjs::ReturnOp>()) {
                if (!step() || !returned.getValue().getDefiningOp<ctjs::ConstantOp>()) {
                    return refuse(
                        "class receiver getter or method needs a primitive constructor return");
                }
            }
        }
        auto & entry = function.getBody().front();
        if (!entry.getArgument(ctjs::arg_new_target).use_empty() ||
            !entry.getArgument(ctjs::arg_callee).use_empty() ||
            !fieldsOnly(entry.getArgument(ctjs::arg_receiver), methodKeys, staticReads, true)) {
            return refuse(
                "class constructor observes new.target, lexical home or receiver identity");
        }
        if (!staticGetters(staticDefinitions, staticReads, call)) { return false; }
        for (ctjs::SetPropertyOp write : getterHomes) {
            if (!step()) { return false; }
            if (!setup.contains(write)) {
                return refuse("class constructor reaches an unrelated getter home");
            }
        }
        constructors.insert(function);
        // Keep method definitions on the prototype until constructor lowering.
        // They must already be available when the constructor body runs.
        if (definitions.empty()) {
            setup.insert(attachment);
        } else {
            retainedSetup.insert(attachment);
        }
        setup.insert(home);
        setup.insert(backedge);
        return true;
    }

    bool prove(const HostContract & contract, bool domEntry = false) {
        auto entry = module.lookupSymbol<ctjs::FuncOp>(contract.entry);
        if (!entry || !functionIndex(entry)) {
            return refuse("class initialization requires a source entry");
        }
        for (mlir::Operation & op : module.getBody()->getOperations()) {
            if (!step()) { return false; }
            auto fn = llvm::dyn_cast<ctjs::FuncOp>(op);
            auto index = fn ? functionIndex(fn) : std::nullopt;
            if (!fn || !index || fn.getBody().empty() ||
                fn.getBody().front().getNumArguments() < 3 ||
                !functions.try_emplace(*index, fn).second) {
                return refuse("class initialization requires complete source functions");
            }
        }
        ctjs::FuncOp declaration;
        if (functionIndex(entry) != 0) {
            declaration = functions.lookup(0);
            if (!declaration ||
                !host_detail::isInertEntryDeclaration(declaration, entry, [&] { return step(); })) {
                return refuse("class initialization requires an inert entry declaration");
            }
            // No host value may reenter source code before the fixed class
            // helper. DOM preparation defers these uses to its final typed proof.
            for (mlir::BlockArgument argument :
                 entry.getBody().front().getArguments().drop_front(ctjs::implicit_arguments)) {
                if (!step() || (!domEntry && !argument.use_empty())) {
                    return refuse("class initialization requires unused entry parameters");
                }
            }
            for (mlir::OpOperand & use :
                 entry.getBody().front().getArgument(ctjs::arg_callee).getUses()) {
                if (!step()) { return false; }
                if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
                if (!llvm::isa<ctjs::CreateClosureOp>(use.getOwner()) ||
                    use.getOperandNumber() != 0) {
                    return refuse("class initialization entry observes its callee identity");
                }
            }
        }
        llvm::DenseSet<int64_t> createdFunctions;
        auto walked = module.walk([&](mlir::Operation * op) -> mlir::WalkResult {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            if (op->hasAttr("ctjs.skipped")) {
                refuse("class initialization cannot omit source functions");
                return mlir::WalkResult::interrupt();
            }
            if (auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(op);
                closure && !createdFunctions.insert(closure.getFunction()).second) {
                refuse("class initialization requires unique source closure creation sites");
                return mlir::WalkResult::interrupt();
            }
            if (op->getNumRegions() && op != module.getOperation() &&
                !(llvm::isa<ctjs::FuncOp>(op) && op->getParentOp() == module.getOperation()) &&
                !llvm::isa<mlir::scf::IfOp, mlir::scf::ForOp, mlir::scf::WhileOp,
                           mlir::scf::IndexSwitchOp>(op)) {
                refuse("class initialization contains an unchecked source region");
                return mlir::WalkResult::interrupt();
            }
            if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(op)) {
                if (domEntry && llvm::is_contained(contract.initialIntrinsics, store.getName())) {
                    refuse("declared DOM intrinsic binding is replaced by source");
                    return mlir::WalkResult::interrupt();
                }
                auto [at, fresh] = globals.try_emplace(store.getName(), store);
                if (!fresh) { at->second = {}; }
            }
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(op)) {
                auto load = call.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
                if (load && load.getName() == host_detail::classDefinedIntrinsic) {
                    calls.push_back(call);
                }
            }
            return mlir::WalkResult::advance();
        });
        if (walked.wasInterrupted()) { return false; }
        if (calls.empty()) { return refuse("source has no class initialization calls"); }
        if (llvm::is_contained(contract.initialIntrinsics, "Error")) {
            walked = module.walk([&](ctjs::ConstructOp made) -> mlir::WalkResult {
                if (!step()) { return mlir::WalkResult::interrupt(); }
                auto load = made.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
                if (!load || load.getName() != "Error" || made.getNewTarget() != load.getResult() ||
                    made.getArgs().size() != 1) {
                    return mlir::WalkResult::advance();
                }
                auto message = made.getArgs().front().getDefiningOp<ctjs::ConstantOp>();
                if (!message || !llvm::isa<ctjs::StringAttr>(message.getValue())) {
                    return mlir::WalkResult::advance();
                }
                for (mlir::OpOperand & use : made.getResult().getUses()) {
                    if (!step()) { return mlir::WalkResult::interrupt(); }
                    if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
                    auto thrown = llvm::dyn_cast<ctjs::ThrowOp>(use.getOwner());
                    if (!thrown) {
                        refuse("declared Error payload escapes its throw");
                        return mlir::WalkResult::interrupt();
                    }
                    errorOperations.insert(thrown);
                }
                errorOperations.insert(load);
                errorOperations.insert(made);
                return mlir::WalkResult::advance();
            });
            if (walked.wasInterrupted()) { return false; }
        }
        for (ctjs::CallOp call : calls) {
            if (!proveCells(call->getParentOfType<ctjs::FuncOp>())) { return false; }
            if (!examine(call, domEntry)) { return false; }
        }
        const auto recordHelper = [&](mlir::Operation * op) {
            auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(op);
            auto method = llvm::dyn_cast<ctjs::CallOp>(op);
            if (!direct && !method) { return mlir::WalkResult::advance(); }
            if (!step()) { return mlir::WalkResult::interrupt(); }
            auto callee = direct ? direct.getCalleeValue() : method.getCallee();
            auto closure =
                sourceClosure(callee, domEntry && op->getParentOfType<ctjs::FuncOp>() == entry);
            auto fn = target(closure);
            if (!fn || constructors.contains(fn) || methods.contains(fn) || getters.contains(fn)) {
                return mlir::WalkResult::advance();
            }
            auto read = callee.getDefiningOp<ctjs::GetPropertyOp>();
            const bool localCall = domEntry && method && callee == closure.getResult() &&
                                   undefined(method.getReceiver());
            if (direct ? direct.getTarget() != fn || !undefined(direct.getReceiver()) ||
                             !undefined(direct.getNewTarget())
                       : !localCall && (!read || method.getReceiver() != read.getObject())) {
                return mlir::WalkResult::advance();
            }
            auto & block = fn.getBody().front();
            if (!unusedReceiver(fn) || !block.getArgument(ctjs::arg_new_target).use_empty()) {
                return mlir::WalkResult::advance();
            }
            helpers.insert(fn);
            helperCalls.insert(op);
            if (domEntry && op->getParentOfType<ctjs::FuncOp>() == entry &&
                closure->getParentOfType<ctjs::FuncOp>() == entry &&
                callee == closure.getResult()) {
                // This original call survives class rewriting and receives the
                // complete typed DOM proof. Uncalled holder slots may disappear
                // during rewriting, so they retain the strict source census.
                domEntryHelpers.insert(fn);
            }
            return mlir::WalkResult::advance();
        };
        // Prove holder targets before checking their enclosing direct callers,
        // whose receiver may only be saved by one of these unused-this arrows.
        walked = module.walk([&](ctjs::CallOp call) { return recordHelper(call); });
        if (walked.wasInterrupted() || !reason.empty()) { return false; }
        walked = module.walk([&](ctjs::CallDirectOp call) { return recordHelper(call); });
        if (walked.wasInterrupted() || !reason.empty()) { return false; }
        for (auto [read, object] : holderCaptures) {
            (void)read;
            if (!step() || !localDOMHolders.contains(object.getDefiningOp())) {
                return refuse("captured holder lacks a complete local callable proof");
            }
        }
        for (auto & [publication, holder] : globalHolders) {
            (void)publication;
            for (ctjs::SetPropertyOp store : holder.stores) {
                if (!step()) { return false; }
                domEntryHelpers.erase(
                    target(store.getValue().getDefiningOp<ctjs::CreateClosureOp>()));
            }
        }
        // No ambient object, unknown callee, accessor, dynamic key or reflective
        // instruction can replace the fixed helper between entry and any call.
        // Reject the whole module, including suffixes and uncalled bodies.
        walked = module.walk([&](mlir::Operation * op) -> mlir::WalkResult {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            // This entire declaration was checked above. Retain it for the
            // entry provider, which separately decides whether it can be erased.
            if (declaration &&
                (op == declaration || op->getParentOfType<ctjs::FuncOp>() == declaration)) {
                return mlir::WalkResult::advance();
            }
            if (setup.contains(op) || retainedSetup.contains(op) || methodCalls.contains(op) ||
                helperCalls.contains(op) || llvm::is_contained(calls, op) ||
                llvm::is_contained(constructorReads, op) || cellOperations.contains(op) ||
                llvm::is_contained(captureReads, op)) {
                return mlir::WalkResult::advance();
            }
            if (errorOperations.contains(op) &&
                throwingGetters.contains(op->getParentOfType<ctjs::FuncOp>())) {
                return mlir::WalkResult::advance();
            }
            bool accepted =
                llvm::isa<mlir::ModuleOp, ctjs::ConstantOp, ctjs::CreateObjectOp,
                          ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp, ctjs::ReturnOp,
                          ctjs::StoreGlobalOp, ctjs::BinaryOp, ctjs::UnaryOp, ctjs::CompareOp,
                          ctjs::TruthyOp, ctjs::FromBoolOp>(op);
            // Proved ordinary methods and exact local helpers may contain control flow.
            // The recursive census still checks every arm/body, including ones
            // never called. Setup, constructors and getter cloning stay linear.
            // The lift represents break/continue/return edges with integer
            // flags and switches. These exact transport ops cannot reenter or
            // change the class helper; switch arms still get the full census.
            // Static ++/-- conversions also cannot reenter; native admission
            // separately requires numeric operands before emitting arithmetic.
            if (llvm::isa<mlir::scf::IfOp, mlir::scf::ForOp, mlir::scf::WhileOp,
                          mlir::scf::IndexSwitchOp, mlir::scf::YieldOp, mlir::scf::ConditionOp,
                          mlir::arith::ConstantOp, mlir::arith::IndexCastUIOp,
                          mlir::arith::TruncIOp, mlir::ub::PoisonOp, ctjs::BinaryStaticOp,
                          mlir::cf::BranchOp, mlir::cf::CondBranchOp, mlir::cf::SwitchOp>(op)) {
                auto fn = op->getParentOfType<ctjs::FuncOp>();
                // Entry short-circuit results keep their source branches for
                // the final typed DOM proof; class setup is still checked above.
                accepted =
                    methods.contains(fn) || helpers.contains(fn) ||
                    (domEntry && fn == entry && llvm::isa<mlir::scf::IfOp, mlir::scf::YieldOp>(op));
                if (accepted && llvm::isa<mlir::scf::IndexSwitchOp>(op)) {
                    dispatchMethods.insert(op->getParentOfType<ctjs::FuncOp>());
                }
            }
            if (auto thrown = llvm::dyn_cast<ctjs::ThrowOp>(op)) {
                // An uncaught object throw can reenter through formatting.
                // Preserve literal primitive throws; downstream lowering still
                // has to prove their completion and payload representation.
                accepted = (methods.contains(op->getParentOfType<ctjs::FuncOp>()) ||
                            throwingGetters.contains(op->getParentOfType<ctjs::FuncOp>())) &&
                           thrown.getValue().getDefiningOp<ctjs::ConstantOp>();
            }
            if (llvm::isa<ctjs::PushHandlerOp, ctjs::PopHandlerOp, ctjs::CheckOp, ctjs::CatchLandOp,
                          ctjs::CreateCellOp, ctjs::CellGetOp, ctjs::CellSetOp>(op)) {
                // Keep the original exception CFG for the existing DOM URI/JSON
                // normalizer and local cells for the DOM capture proof. Neither
                // status edges nor local state disappear in this census.
                accepted = domEntryHelpers.contains(op->getParentOfType<ctjs::FuncOp>());
                needsDOMMethodProof |= accepted;
            }
            if (auto fn = llvm::dyn_cast<ctjs::FuncOp>(op)) {
                auto & block = fn.getBody().front();
                const bool receiverUnused = unusedReceiver(fn);
                if (!reason.empty()) { return mlir::WalkResult::interrupt(); }
                accepted =
                    methods.contains(fn) || helpers.contains(fn) ||
                    (llvm::hasSingleElement(fn.getBody()) &&
                     (constructors.contains(fn) || getters.contains(fn) ||
                      (((declaration && fn == entry) || block.getNumArguments() == 3) &&
                       receiverUnused && block.getArgument(ctjs::arg_new_target).use_empty())));
                accepted &= fn.getUpvalueCount() == 0 || methods.contains(fn) ||
                            llvm::any_of(capturedClosures, [&](ctjs::CreateClosureOp closure) {
                                return target(closure) == fn;
                            });
            }
            if (auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(op)) {
                auto fn = target(closure);
                accepted = fn && fn != entry &&
                           (closure.getUpvalues().empty() ||
                            llvm::is_contained(capturedClosures, closure)) &&
                           (undefined(closure.getEnclosingThis()) || helpers.contains(fn));
            }
            if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op)) {
                accepted = load.getName() == host_detail::classDefinedIntrinsic ||
                           globalHolderLoads.contains(load) ||
                           static_cast<bool>(sourceClosure(load.getResult()));
                auto fn = op->getParentOfType<ctjs::FuncOp>();
                if (domEntry &&
                    (fn == entry || methods.contains(fn) || domEntryHelpers.contains(fn)) &&
                    load.getName() != "Error" &&
                    llvm::is_contained(contract.initialIntrinsics, load.getName())) {
                    // Only the complete typed DOM proof can authorize these
                    // identities and their uses, including unused method bodies.
                    accepted = true;
                    needsDOMMethodProof |= methods.contains(fn) || domEntryHelpers.contains(fn);
                }
            }
            if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(op)) {
                accepted = helperCalls.contains(call);
            }
            if (auto made = llvm::dyn_cast<ctjs::ConstructOp>(op)) {
                accepted = constructors.contains(
                    target(sourceValue(made.getCallee()).getDefiningOp<ctjs::CreateClosureOp>()));
            }
            mlir::Value key;
            if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) { key = read.getKey(); }
            if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(op)) { key = write.getKey(); }
            if (key) {
                auto constant = key.getDefiningOp<ctjs::ConstantOp>();
                // Literal Number keys cannot name a prototype hook or invoke
                // user coercion. Native field/index representation is separate.
                accepted = ctjs::ordinaryKey(key) ||
                           (helpers.contains(op->getParentOfType<ctjs::FuncOp>()) && constant &&
                            llvm::isa<ctjs::NumberAttr>(constant.getValue()));
                auto fn = op->getParentOfType<ctjs::FuncOp>();
                if (domEntry &&
                    (fn == entry || methods.contains(fn) || domEntryHelpers.contains(fn)) &&
                    llvm::isa<ctjs::GetPropertyOp>(op) && ctjs::constantKey(key) == "toString") {
                    // The DOM proof requires the actual Number receiver; this
                    // is not permission to invoke an arbitrary coercion hook.
                    accepted = true;
                    needsDOMMethodProof |= methods.contains(fn) || domEntryHelpers.contains(fn);
                }
            }
            // Method calls defer only to complete private DOM probes below,
            // including unused/transitive callers. Exact entry-called helpers
            // survive into the final proof; other helpers keep the strict census.
            if (domEntry && llvm::isa<ctjs::CallOp>(op)) {
                auto fn = op->getParentOfType<ctjs::FuncOp>();
                if (fn == entry || methods.contains(fn) || domEntryHelpers.contains(fn)) {
                    accepted = true;
                    needsDOMMethodProof |= methods.contains(fn) || domEntryHelpers.contains(fn);
                }
            }
            if (accepted) { return mlir::WalkResult::advance(); }
            auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op);
            refuse("class initialization source contains an unknown call, binding or reflective "
                   "effect" +
                   (load ? " (global \"" + load.getName().str() + "\")"
                         : " (op " + op->getName().getStringRef().str() + ")"));
            return mlir::WalkResult::interrupt();
        });
        if (walked.wasInterrupted()) { return false; }
        for (auto & [publication, holder] : globalHolders) {
            (void)holder;
            const auto name = llvm::cast<ctjs::StoreGlobalOp>(publication).getName();
            for (const auto & root : contract.roots) {
                if (!step()) { return false; }
                if (root.binding == name) {
                    return refuse("global callable holder is requested by the host");
                }
            }
            for (const auto & observation : contract.observations) {
                if (!step()) { return false; }
                if (observation == name) {
                    return refuse("global callable holder is requested by the host");
                }
            }
        }
        // Closure references and calls are covered above. Symbol attributes
        // must not retain a getter definition after all its reads are expanded.
        for (const auto & uses : {mlir::SymbolTable::getSymbolUses(module.getOperation()),
                                  mlir::SymbolTable::getSymbolUses(&module.getBodyRegion())}) {
            if (!uses) { return refuse("static getter symbol uses could not be enumerated"); }
            for (const auto & use : *uses) {
                if (!step()) { return false; }
                auto target = mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
                    use.getUser(), use.getSymbolRef());
                if (!target) {
                    return refuse("class initialization has an unresolved symbol reference");
                }
                if (getters.contains(target)) {
                    return refuse("static getter has a remaining symbol reference");
                }
            }
        }
        // Reject before normalization can replace a method's body and
        // invalidate a nested construction or definition recorded above.
        if (domEntry && needsDOMMethodProof) {
            for (auto [made, definition] : methodProbes) {
                if (!step()) { return false; }
                auto fn = target(definition.getValue().getDefiningOp<ctjs::CreateClosureOp>());
                if (!fn || made->getParentOfType<ctjs::FuncOp>() != entry) {
                    return refuse("DOM class methods require entry-local instances");
                }
            }
        }
        return true;
    }

    bool proveDOMMethods(const HostContract & contract) {
        if (!needsDOMMethodProof) { return true; }
        llvm::SetVector<mlir::Operation *> unused;
        for (auto [made, definition] : methodProbes) {
            (void)made;
            if (!step()) { return false; }
            bool readKey = false;
            const auto key = ctjs::constantKey(definition.getKey());
            const auto scanned = module.walk([&](ctjs::GetPropertyOp read) {
                if (!step()) { return mlir::WalkResult::interrupt(); }
                const auto selected = ctjs::constantKey(read.getKey());
                readKey |= selected.empty() || selected == key;
                return mlir::WalkResult::advance();
            });
            if (scanned.wasInterrupted()) { return false; }
            if (!readKey) { unused.insert(definition); }
        }
        const auto omitUnused = [&](mlir::ModuleOp candidate, mlir::IRMapping * mapping,
                                    ctjs::SetPropertyOp keep) {
            for (mlir::Operation * operation : unused) {
                if (!step()) { return false; }
                if (operation == keep) { continue; }
                auto definition = llvm::cast<ctjs::SetPropertyOp>(
                    mapping ? mapping->lookup(operation) : operation);
                auto closure = definition.getValue().getDefiningOp<ctjs::CreateClosureOp>();
                auto original = target(llvm::cast<ctjs::SetPropertyOp>(operation)
                                           .getValue()
                                           .getDefiningOp<ctjs::CreateClosureOp>());
                auto fn = candidate.lookupSymbol<ctjs::FuncOp>(original.getSymName());
                if (!mlir::SymbolTable::symbolKnownUseEmpty(fn, candidate.getOperation()) ||
                    !mlir::SymbolTable::symbolKnownUseEmpty(fn, &candidate.getBodyRegion())) {
                    return refuse("unused DOM method retains a symbol reference");
                }
                definition.erase();
                for (mlir::Operation * root : llvm::make_early_inc_range(closure->getUsers())) {
                    if (!step() || !llvm::isa<ctjs::RootOp>(root)) {
                        return refuse("unused DOM method retains a callable use");
                    }
                    root->erase();
                }
                closure.erase();
                fn.erase();
            }
            return true;
        };
        // Reachability only establishes which original bodies the shared proof
        // must cover. It grants no authority to their parameters: actual calls
        // and field state stay in source order in the private DOM proof below.
        llvm::DenseMap<mlir::Operation *, llvm::SetVector<mlir::Operation *>> originalCalls;
        const auto calledFromEntry = [&](ctjs::ConstructOp made, ctjs::FuncOp method) {
            auto [found, fresh] = originalCalls.try_emplace(made);
            auto & reached = found->second;
            if (fresh) {
                llvm::StringMap<ctjs::FuncOp> definitions;
                for (auto [instance, definition] : methodProbes) {
                    if (!step()) { return false; }
                    if (instance == made) {
                        definitions[ctjs::constantKey(definition.getKey())] =
                            target(definition.getValue().getDefiningOp<ctjs::CreateClosureOp>());
                    }
                }
                const auto record = [&](ctjs::CallOp call, mlir::Value receiver) {
                    auto read = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                    if (!read || call.getReceiver() != receiver || read.getObject() != receiver) {
                        return;
                    }
                    if (auto fn = definitions.lookup(ctjs::constantKey(read.getKey()))) {
                        reached.insert(fn);
                    }
                };
                for (mlir::Operation * user : made.getResult().getUsers()) {
                    if (!step()) { return false; }
                    auto call = llvm::dyn_cast<ctjs::CallOp>(user);
                    if (call && call->getBlock() == made->getBlock() &&
                        made->isBeforeInBlock(call)) {
                        record(call, made.getResult());
                    }
                }
                // ponytail: only exact same-this edges; aliases need their own
                // receiver proof. A visited set bounds recursive source graphs.
                for (size_t i = 0; i < reached.size(); ++i) {
                    auto fn = llvm::cast<ctjs::FuncOp>(reached[i]);
                    auto receiver = fn.getBody().front().getArgument(ctjs::arg_receiver);
                    auto walked = fn.walk([&](mlir::Operation * op) {
                        if (!step()) { return mlir::WalkResult::interrupt(); }
                        if (auto call = llvm::dyn_cast<ctjs::CallOp>(op)) {
                            record(call, receiver);
                        }
                        return mlir::WalkResult::advance();
                    });
                    if (walked.wasInterrupted()) { return false; }
                }
            }
            return reached.contains(method);
        };
        bool provedOriginalCalls = false;
        // Probe every method, including callers that overwrite a field before
        // dispatching to a DOM method; direct DOM calls alone are not a census.
        for (auto [made, definition] : methodProbes) {
            if (!step()) { return false; }
            auto fn = target(definition.getValue().getDefiningOp<ctjs::CreateClosureOp>());
            const bool hasParameters =
                fn.getBody().front().getNumArguments() != ctjs::implicit_arguments;
            if (hasParameters) {
                if (!calledFromEntry(made, fn)) {
                    return refuse("DOM class method parameters require original entry-call "
                                  "reachability for each instance");
                }
                if (provedOriginalCalls) { continue; }
            }
            // Reserve the clone's operation/operand walk before allocating it.
            auto counted = module.walk([&](mlir::Operation * op) {
                const uint64_t cost = uint64_t(1) + op->getNumOperands();
                if (cost > remaining) {
                    refuse("class initialization work budget exhausted");
                    return mlir::WalkResult::interrupt();
                }
                remaining -= static_cast<unsigned>(cost);
                return mlir::WalkResult::advance();
            });
            if (counted.wasInterrupted()) { return false; }
            mlir::IRMapping mapping;
            mlir::OwningOpRef<mlir::ModuleOp> probe(
                llvm::cast<mlir::ModuleOp>(module->clone(mapping)));
            // Other uncalled methods have their own independent probes. Omit
            // them only from this proof copy so the existing lift sees exactly
            // the method under test and all original reachable method calls.
            if (!omitUnused(*probe, &mapping, definition)) { return false; }
            if (!hasParameters) {
                auto instance = llvm::cast<ctjs::ConstructOp>(mapping.lookup(made.getOperation()));
                mlir::OpBuilder at(instance);
                at.setInsertionPointAfter(instance);
                auto read = ctjs::GetPropertyOp::create(at, instance.getLoc(), instance.getType(),
                                                        instance.getResult(),
                                                        mapping.lookup(definition.getKey()));
                ctjs::CallOp::create(at, instance.getLoc(), instance.getType(), read.getResult(),
                                     instance.getResult(), mlir::ValueRange{});
            }
            if (auto error = liftDOMClasses(*probe, remaining)) {
                return refuse(llvm::toString(std::move(error)));
            }
            HostContract checked = contract;
            checked.moduleSha256 = hostContractFingerprint(*probe);
            // No class intrinsic remains, so this reuses normal DOM preparation
            // without recursing into class normalization. These calls exist only
            // in discarded proof copies; the emitted candidate is checked again.
            if (auto error = prepareDOMEntry(*probe, checked, remaining)) {
                return refuse("DOM class method body: " + llvm::toString(std::move(error)));
            }
            provedOriginalCalls |= hasParameters;
        }
        // Only now have all unused bodies passed the same typed source proof.
        // The original no-read census makes removing their definitions inert.
        return omitUnused(module, nullptr, {});
    }

    bool normalizeMethods() {
        for (mlir::Operation * operation : dispatchMethods) {
            auto function = llvm::cast<ctjs::FuncOp>(operation);
            // Charge the private copy before allocating it. All source effect
            // and receiver checks have finished; only structural transport is
            // normalized, with the existing bounded exception machinery.
            auto walked = function.walk([&](mlir::Operation * op) {
                const uint64_t cost = uint64_t(1) + op->getNumOperands() + op->getNumResults();
                if (cost > remaining) {
                    refuse("class initialization work budget exhausted");
                    return mlir::WalkResult::interrupt();
                }
                remaining -= static_cast<unsigned>(cost);
                return mlir::WalkResult::advance();
            });
            if (walked.wasInterrupted()) { return false; }
            mlir::IRMapping mapping;
            mlir::OwningOpRef<ctjs::FuncOp> copy(
                llvm::cast<ctjs::FuncOp>(function->clone(mapping)));
            if (auto failed = lowering_detail::normalizeStructuredExits(*copy, remaining)) {
                auto message = llvm::toString(std::move(failed));
                return refuse(message == "native exception recovery work budget exhausted"
                                  ? "class initialization work budget exhausted"
                                  : message);
            }
            // Exit normalization moves branch bodies without replacing their
            // property reads. Expansion must follow the private copy too.
            for (auto & [read, target] : getterReads) {
                if (!step()) { return false; }
                if (auto * mapped = mapping.lookupOrNull(read.getOperation())) {
                    read = llvm::cast<ctjs::GetPropertyOp>(mapped);
                }
            }
            for (ctjs::GetPropertyOp & read : constructorReads) {
                if (!step()) { return false; }
                if (auto * mapped = mapping.lookupOrNull(read.getOperation())) {
                    read = llvm::cast<ctjs::GetPropertyOp>(mapped);
                }
            }
            for (ctjs::LoadUpvalueOp & read : captureReads) {
                if (!step()) { return false; }
                if (auto * mapped = mapping.lookupOrNull(read.getOperation())) {
                    if (auto helper = callableCaptures.lookup(read)) {
                        callableCaptures.erase(read);
                        callableCaptures[mapped] = helper;
                    }
                    if (auto object = holderCaptures.lookup(read.getResult())) {
                        holderCaptures.erase(read.getResult());
                        holderCaptures[mapped->getResult(0)] = object;
                    }
                    read = llvm::cast<ctjs::LoadUpvalueOp>(mapped);
                }
            }
            for (auto & [publication, holder] : globalHolders) {
                (void)publication;
                for (auto & [read, closure] : holder.reads) {
                    (void)closure;
                    if (!step()) { return false; }
                    if (auto * mapped = mapping.lookupOrNull(read.getOperation())) {
                        read = llvm::cast<ctjs::GetPropertyOp>(mapped);
                    }
                }
                for (ctjs::LoadGlobalOp & load : holder.loads) {
                    if (!step()) { return false; }
                    if (auto * mapped = mapping.lookupOrNull(load.getOperation())) {
                        load = llvm::cast<ctjs::LoadGlobalOp>(mapped);
                    }
                }
            }
            for (auto & [object, holder] : localDOMHolders) {
                (void)object;
                for (auto & [read, closure] : holder.reads) {
                    (void)closure;
                    if (!step()) { return false; }
                    if (auto * mapped = mapping.lookupOrNull(read.getOperation())) {
                        read = llvm::cast<ctjs::GetPropertyOp>(mapped);
                    }
                }
            }
            normalizedMethods.emplace_back(function, std::move(copy));
        }
        return true;
    }

    static void eraseRooted(mlir::Operation * operation) {
        for (mlir::Operation * root : llvm::make_early_inc_range(operation->getUsers())) {
            if (!llvm::isa<ctjs::RootOp>(root)) {
                llvm::report_fatal_error("proved callable holder retains an observable use");
            }
            root->erase();
        }
        operation->erase();
    }
    void expandHolders() {
        // Every alias and implicit-argument use was checked before mutation.
        // Local DOM slots remain as functions until the complete invocation/type
        // proof; no unused body may disappear merely because its holder does.
        llvm::SmallVector<ctjs::FuncOp> targets;
        const auto expand = [&](mlir::Operation * owner, CallableObject & holder) {
            auto publication = llvm::dyn_cast<ctjs::StoreGlobalOp>(owner);
            auto * object = publication ? publication.getValue().getDefiningOp() : owner;
            for (auto [read, closure] : holder.reads) {
                auto function = target(closure);
                for (mlir::Operation * user : llvm::make_early_inc_range(read->getUsers())) {
                    auto call = llvm::dyn_cast<ctjs::CallOp>(user);
                    if (!call) { continue; } // Inert roots are removed below.
                    mlir::OpBuilder at(call);
                    auto undefined =
                        ctjs::ConstantOp::create(at, call.getLoc(), call.getType(),
                                                 ctjs::UndefinedAttr::get(module.getContext()));
                    llvm::SmallVector<mlir::Value> arguments(call.getArgs());
                    arguments.resize(function.getBody().front().getNumArguments() -
                                         ctjs::implicit_arguments,
                                     undefined);
                    auto direct = ctjs::CallDirectOp::create(
                        at, call.getLoc(), call.getType(),
                        mlir::FlatSymbolRefAttr::get(function.getSymNameAttr()), undefined,
                        undefined, undefined, arguments, nullptr, nullptr);
                    call.getResult().replaceAllUsesWith(direct.getResult());
                    call.erase();
                }
                eraseRooted(read);
            }
            for (ctjs::SetPropertyOp store : holder.stores) {
                auto closure = store.getValue().getDefiningOp<ctjs::CreateClosureOp>();
                auto function = target(closure);
                targets.push_back(function);
                mlir::SymbolTable::setSymbolVisibility(function,
                                                       mlir::SymbolTable::Visibility::Private);
                store.erase();
                eraseRooted(closure);
            }
            for (ctjs::LoadGlobalOp load : holder.loads) { eraseRooted(load); }
            if (publication) {
                publication.erase();
                eraseRooted(object);
            }
        };
        for (auto & [publication, holder] : globalHolders) { expand(publication, holder); }
        for (auto & [object, holder] : localDOMHolders) { expand(object, holder); }
        // Only strict source-census slots may disappear here. DOM slots still
        // need original calls, including complete argument and effect proof.
        for (ctjs::FuncOp function : targets) {
            if (!domEntryHelpers.contains(function) &&
                mlir::SymbolTable::symbolKnownUseEmpty(function, module.getOperation()) &&
                mlir::SymbolTable::symbolKnownUseEmpty(function, &module.getBodyRegion())) {
                function.erase();
            }
        }
    }

    void rewrite() {
        // All current-IR checks precede the first mutation. Nothing inferred
        // from report attributes authorizes removal, even on a repeated run.
        module.walk([](mlir::Operation * op) { removeAttrsWithPrefix(op, "ctnative."); });
        for (auto & [function, copy] : normalizedMethods) {
            (*copy).walk([](mlir::Operation * op) { removeAttrsWithPrefix(op, "ctnative."); });
            function.getBody().takeBody(copy->getBody());
        }
        // Holder expansion erases its slot closures. Clear their proved capture
        // metadata first; the retained bodies still own all recorded reads.
        for (ctjs::CreateClosureOp closure : capturedClosures) {
            target(closure).setUpvalueCount(0);
            closure.getUpvaluesMutable().clear();
            closure.removeEnclosingIndicesAttr();
        }
        expandHolders();
        llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::GetPropertyOp>> reads;
        for (auto [read, target] : getterReads) { reads[target].push_back(read); }
        for (ctjs::FuncOp target : getterOrder) {
            for (ctjs::GetPropertyOp read : reads[target]) {
                mlir::OpBuilder at(read);
                if (throwingGetters.contains(target)) {
                    // Keep the throw in its original function. A direct call
                    // preserves abrupt completion without cloning a terminator
                    // into the middle of the reader's block. Native completion
                    // and Error representation remain separate admission proofs.
                    auto undefined =
                        ctjs::ConstantOp::create(at, read.getLoc(), read.getType(),
                                                 ctjs::UndefinedAttr::get(module.getContext()));
                    auto scope = read->getParentOfType<ctjs::FuncOp>();
                    auto closure = ctjs::CreateClosureOp::create(
                        at, read.getLoc(), read.getType(),
                        scope.getBody().front().getArgument(ctjs::arg_callee), undefined,
                        at.getI32IntegerAttr(static_cast<int32_t>(*functionIndex(target))),
                        mlir::ValueRange{}, mlir::DenseI32ArrayAttr{});
                    auto call = ctjs::CallDirectOp::create(
                        at, read.getLoc(), read.getType(),
                        mlir::FlatSymbolRefAttr::get(target.getSymNameAttr()), undefined, undefined,
                        closure, mlir::ValueRange{}, nullptr, nullptr);
                    read.getResult().replaceAllUsesWith(call.getResult());
                    read.erase();
                    continue;
                }
                mlir::IRMapping mapping;
                // Dependencies have already been expanded. This closed body
                // has no remaining implicit-argument or external-value uses.
                // Clone at each original read, preserving evaluation order and
                // fresh object identity, including through getter dependencies.
                for (mlir::Operation & op : target.getBody().front()) {
                    if (llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp>(op)) { continue; }
                    if (auto returned = llvm::dyn_cast<ctjs::ReturnOp>(op)) {
                        read.getResult().replaceAllUsesWith(mapping.lookup(returned.getValue()));
                    } else {
                        at.clone(op, mapping);
                    }
                }
                read.erase();
            }
        }
        for (ctjs::GetPropertyOp read : constructorReads) {
            for (mlir::Operation * root : llvm::make_early_inc_range(read->getUsers())) {
                root->erase(); // Only inert roots remain after getter expansion.
            }
            read.erase();
        }
        for (ctjs::LoadUpvalueOp read : captureReads) {
            if (auto helper = callableCaptures.lookup(read)) {
                for (mlir::Operation * user : llvm::make_early_inc_range(read->getUsers())) {
                    auto call = llvm::dyn_cast<ctjs::CallOp>(user);
                    if (!call) { continue; } // Only inert roots remain below.
                    mlir::OpBuilder at(call);
                    auto absent =
                        ctjs::ConstantOp::create(at, call.getLoc(), call.getType(),
                                                 ctjs::UndefinedAttr::get(module.getContext()));
                    llvm::SmallVector<mlir::Value> arguments(call.getArgs());
                    arguments.resize(helper.getBody().front().getNumArguments() -
                                         ctjs::implicit_arguments,
                                     absent);
                    auto direct = ctjs::CallDirectOp::create(
                        at, call.getLoc(), call.getType(),
                        mlir::FlatSymbolRefAttr::get(helper.getSymNameAttr()), absent, absent,
                        absent, arguments, nullptr, nullptr);
                    call.getResult().replaceAllUsesWith(direct.getResult());
                    call.erase();
                }
            }
            for (mlir::Operation * root : llvm::make_early_inc_range(read->getUsers())) {
                root->erase(); // Only roots remain after captured getter expansion.
            }
            read.erase();
        }
        for (ctjs::CellGetOp read : cellReads) {
            read.getResult().replaceAllUsesWith(cells.lookup(read.getCell()));
            read.erase();
        }
        for (auto [value, initial] : cells) {
            (void)initial;
            for (mlir::Operation * use : llvm::make_early_inc_range(value.getUsers())) {
                use->erase(); // Proved identical stores and inert cell roots.
            }
            value.getDefiningOp()->erase();
        }
        // Captured holders remain alive until all proved cell/capture transport
        // is gone. Slot calls and closures were removed by expandHolders.
        for (auto & [object, holder] : localDOMHolders) {
            (void)holder;
            eraseRooted(object);
        }
        // The original capture proof checked every implicit argument before any
        // mutation. Only an unobserved sibling closure can disappear here; a
        // remaining entry call still goes through the ordinary closure lift.
        for (mlir::Operation * operation : capturedHelpers) {
            auto closure = llvm::cast<ctjs::CreateClosureOp>(operation);
            mlir::SymbolTable::setSymbolVisibility(target(closure),
                                                   mlir::SymbolTable::Visibility::Private);
            if (llvm::any_of(closure->getUsers(), [](mlir::Operation * user) {
                    return !llvm::isa<ctjs::RootOp>(user);
                })) {
                continue;
            }
            for (mlir::Operation * root : llvm::make_early_inc_range(closure->getUsers())) {
                root->erase();
            }
            closure.erase();
        }
        for (ctjs::CallOp call : calls) { call.erase(); }
        for (mlir::Operation * op : setup) { op->erase(); }
        for (ctjs::CreateClosureOp closure : getterClosures) {
            for (mlir::Operation * root : llvm::make_early_inc_range(closure->getUsers())) {
                root->erase(); // Only inert roots remain after descriptor removal.
            }
            closure.erase();
        }
        // Original getter closures are gone; every new numeric closure has a
        // matching direct symbol call. Remove callers before their dependencies
        // so unused throwing chains disappear in one pass.
        // ponytail: one symbol scan per getter; index uses if large classes need it.
        for (ctjs::FuncOp getter : llvm::reverse(getterOrder)) {
            if (!throwingGetters.contains(getter) ||
                mlir::SymbolTable::symbolKnownUseEmpty(getter, &module.getBodyRegion())) {
                getter.erase();
            }
        }
        module.walk([&](ctjs::LoadGlobalOp load) {
            if (load.getName() == host_detail::classDefinedIntrinsic &&
                load.getResult().use_empty()) {
                load.erase();
            }
        });
    }
};

struct CTNativeSpecializeClassInitializationPass
    : impl::CTNativeSpecializeClassInitializationBase<CTNativeSpecializeClassInitializationPass> {
    using CTNativeSpecializeClassInitializationBase::CTNativeSpecializeClassInitializationBase;

    void runOnOperation() override {
        auto module = getOperation();
        auto buffer = llvm::MemoryBuffer::getFile(manifest);
        if (!buffer) {
            module.emitError("class initialization requires a readable host manifest");
            return signalPassFailure();
        }
        auto contract = parseHostContract((*buffer)->getBuffer());
        if (!contract) {
            module.emitError() << llvm::toString(contract.takeError());
            return signalPassFailure();
        }
        if (hostContractFingerprint(module) != contract->moduleSha256) {
            module.emitError("class initialization host fingerprint mismatch");
            return signalPassFailure();
        }
        if (contract->provider != HostContract::Provider::closedSource ||
            !llvm::is_contained(contract->initialIntrinsics, host_detail::classDefinedIntrinsic) ||
            llvm::any_of(contract->initialIntrinsics,
                         [](const auto & name) {
                             return name != host_detail::classDefinedIntrinsic && name != "Error";
                         }) ||
            contract->realmGlobalThis || contract->classicScriptRealm ||
            !contract->absentBindings.empty() || !contract->undefinedBindings.empty()) {
            module.emitError("class initialization requires the standard class helper and optional "
                             "Error identity");
            return signalPassFailure();
        }
        classInitialization proof{module, maxSteps};
        if (!proof.prove(*contract)) {
            module.emitError() << proof.reason;
            return signalPassFailure();
        }
        if (auto problem = host_detail::initialBindingProblem(module, *contract);
            !problem.empty()) {
            module.emitError() << problem;
            return signalPassFailure();
        }
        if (!proof.normalizeMethods()) {
            module.emitError() << proof.reason;
            return signalPassFailure();
        }
        proof.rewrite();
    }
};

} // namespace

llvm::Error normalizeDOMClasses(mlir::ModuleOp module, HostContract & contract, unsigned maxSteps) {
    const auto refuse = [](const llvm::Twine & reason) {
        return llvm::createStringError(llvm::inconvertibleErrorCode(), reason);
    };
    if ((contract.provider != HostContract::Provider::ctbrowserDOM &&
         contract.provider != HostContract::Provider::ctbrowserDOMSession) ||
        llvm::count(contract.initialIntrinsics, host_detail::classDefinedIntrinsic) != 1 ||
        llvm::count(contract.initialIntrinsics, "Error") > 1 || contract.realmGlobalThis ||
        contract.classicScriptRealm || !contract.absentBindings.empty() ||
        !contract.undefinedBindings.empty() || !contract.realmOwnDataProperties.empty()) {
        return refuse("DOM class initialization requires the standard class helper and optional "
                      "Error identity");
    }
    classInitialization proof{module, maxSteps};
    if (!proof.prove(contract, true)) { return refuse(proof.reason); }
    // Reuse the binding proof before consuming its declaration. This projected
    // contract proves helper and optional Error identity; DOM declarations and
    // parameters are proved later. Referenced throwing getters retain their
    // original bodies and must pass the final typed DOM proof after rewriting.
    HostContract binding = contract;
    binding.provider = HostContract::Provider::closedSource;
    binding.elementParameters.clear();
    llvm::erase_if(binding.initialIntrinsics, [](const auto & name) {
        return name != host_detail::classDefinedIntrinsic && name != "Error";
    });
    if (auto problem = host_detail::initialBindingProblem(module, binding); !problem.empty()) {
        return refuse(problem);
    }
    if (!proof.normalizeMethods()) { return refuse(proof.reason); }
    proof.rewrite();
    llvm::erase_if(contract.initialIntrinsics, [](const auto & name) {
        return name == host_detail::classDefinedIntrinsic || name == "Error";
    });
    if (!proof.proveDOMMethods(contract)) { return refuse(proof.reason); }
    return llvm::Error::success();
}

} // namespace ctcompile::ctnative
