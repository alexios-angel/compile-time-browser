//===- Iteration.cpp - DOM iteration and default folding --------*- C++ -*-===//
//
// Two rewrites on a private, fingerprint-checked clone of a DOM entry:
// normalizeDOMElementGuards folds default-parameter guards on the entry's
// element parameters (which are never null or undefined), and
// normalizeDOMIteration turns for-of over a querySelectorAll snapshot into
// indexed access. Both are declared and documented in HostContract.h; the
// caller, or normalizeDOMIteration itself, re-runs DOMEntryAnalysis afterwards.
//
//===----------------------------------------------------------------------===//

#include "../Snapshot/Bounds.h"
#include "ctcompile/CTNative/Analysis/HostContract.h"
#include "ctcompile/CTNative/Analysis/WorkBudget.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/Matchers.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Support/Error.h"

namespace ctcompile::ctnative {

llvm::Error normalizeDOMElementGuards(mlir::ModuleOp candidate, const HostContract & contract,
                                      unsigned maxSteps) {
    const auto error = [](llvm::StringRef text) { return llvm::createStringError(text); };
    WorkBudget budget{maxSteps};
    const auto spend = [&] { return budget.spend(); };
    // Charge the walk and the fingerprint before inspecting the contract.
    if (!budget.spendWalk(candidate, [](mlir::Operation * operation) {
            return 2 * (uint64_t(1) + operation->getNumOperands());
        })) {
        return error("DOM element guard work budget exhausted");
    }
    if (hostContractFingerprint(candidate) != contract.moduleSha256 ||
        candidate->hasAttr("ctjs.skipped")) {
        return error("DOM element guard fingerprint mismatch or incomplete source");
    }
    auto entry = candidate.lookupSymbol<ctjs::FuncOp>(contract.entry);
    if ((contract.provider != HostContract::Provider::ctbrowserDOM &&
         contract.provider != HostContract::Provider::ctbrowserDOMSession) ||
        !entry || !entry.getBody().hasOneBlock() ||
        entry.getBody().front().getNumArguments() < ctjs::implicit_arguments ||
        contract.elementParameters.empty() ||
        contract.elementParameters.size() !=
            entry.getBody().front().getNumArguments() - ctjs::implicit_arguments) {
        return error("DOM element guard requires a complete element entry declaration");
    }
    llvm::DenseMap<mlir::Value, bool> truth;
    for (auto [position, index] : llvm::enumerate(contract.elementParameters)) {
        if (!spend()) { return error("DOM element guard work budget exhausted"); }
        if (position != index) {
            return error("DOM element guard requires ordered explicit element parameters");
        }
        truth[entry.getBody().front().getArgument(index + ctjs::implicit_arguments)] = true;
    }
    // Only the parameters' own SSA values start out known truthy. Nullable
    // `closest` results, cells, joins and loop-carried values learn nothing.
    llvm::SmallVector<std::pair<mlir::Operation *, bool>> booleans;
    llvm::SmallVector<std::pair<ctjs::TruthyOp, bool>> queries;
    llvm::SmallVector<std::pair<mlir::scf::IfOp, bool>> branches;
    const auto scan = entry.walk<mlir::WalkOrder::PostOrder>([&](mlir::Operation * operation) {
        if (!spend()) { return mlir::WalkResult::interrupt(); }
        for (mlir::Value result : operation->getResults()) {
            if (!budget.spend(result.getNumUses())) { return mlir::WalkResult::interrupt(); }
        }
        if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation);
            compare && compare.getKind() == ctjs::CompareKind::StrictEq) {
            const auto elementAndUndefined = [&](mlir::Value element, mlir::Value other) {
                auto argument = llvm::dyn_cast<mlir::BlockArgument>(element);
                return argument && argument.getOwner() == &entry.getBody().front() &&
                       argument.getArgNumber() >= ctjs::implicit_arguments &&
                       ctjs::isUndefinedConstant(other);
            };
            if (elementAndUndefined(compare.getLhs(), compare.getRhs()) ||
                elementAndUndefined(compare.getRhs(), compare.getLhs())) {
                truth[compare.getResult()] = false;
                booleans.emplace_back(operation, false);
            } else {
                const auto undefined = [](mlir::Value value) {
                    auto global = value.getDefiningOp<ctjs::LoadGlobalOp>();
                    // The DOM provider fixes the initial `undefined` global.
                    // DOMEntryAnalysis still rejects writes to it and reentry.
                    return ctjs::isUndefinedConstant(value) ||
                           (global && global.getName() == "undefined");
                };
                // Omitted and explicit undefined select their original default arm.
                if (undefined(compare.getLhs()) && undefined(compare.getRhs())) {
                    truth[compare.getResult()] = true;
                    booleans.emplace_back(operation, true);
                }
            }
        }
        if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation);
            unary && unary.getKind() == ctjs::UnaryKind::Not) {
            if (auto found = truth.find(unary.getOperand()); found != truth.end()) {
                const bool value = !found->second;
                truth[unary.getResult()] = value;
                booleans.emplace_back(operation, value);
            }
        }
        if (auto query = llvm::dyn_cast<ctjs::TruthyOp>(operation)) {
            if (auto found = truth.find(query.getValue()); found != truth.end()) {
                const bool value = found->second;
                truth[query.getResult()] = value;
                queries.emplace_back(query, value);
            }
        }
        if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
            if (auto found = truth.find(branch.getCondition()); found != truth.end()) {
                branches.emplace_back(branch, found->second);
            }
        }
        return mlir::WalkResult::advance();
    });
    if (scan.wasInterrupted()) { return error("DOM element guard work budget exhausted"); }
    for (auto [branch, selected] : branches) {
        auto & arm = selected ? branch.getThenRegion() : branch.getElseRegion();
        if (!arm.hasOneBlock() || arm.front().getNumArguments() || arm.front().empty()) {
            return error("DOM element guard lacks an exact selected arm");
        }
        auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(arm.front().back());
        if (!yield || yield.getOperandTypes() != branch.getResultTypes()) {
            return error("DOM element guard lost its value correspondence");
        }
    }
    // Every replacement is decided before anything changes. The caller owns
    // this private clone and must still run DOMEntryAnalysis on the result.
    for (auto [operation, value] : booleans) {
        mlir::OpBuilder at(operation);
        auto constant = ctjs::ConstantOp::create(
            at, operation->getLoc(), ctjs::BooleanAttr::get(candidate.getContext(), value));
        operation->getResult(0).replaceAllUsesWith(constant.getResult());
        operation->erase();
    }
    for (auto [query, value] : queries) {
        mlir::OpBuilder at(query);
        auto constant = mlir::arith::ConstantIntOp::create(at, query.getLoc(), value, 1);
        query.getResult().replaceAllUsesWith(constant.getResult());
        query.erase();
    }
    for (auto [branch, selected] : branches) { inlineSelectedArm(branch, selected); }
    return llvm::Error::success();
}

llvm::Error normalizeDOMIteration(mlir::ModuleOp candidate, const HostContract & contract,
                                  unsigned maxSteps) {
    // Only the private preparation clone is changed. DOMEntryAnalysis, run at
    // the end, must still reject writes, reentry, escaped snapshots and unknown
    // calls.
    WorkBudget budget{maxSteps};
    const auto spend = [&] { return budget.spend(); };
    const auto error = [](llvm::StringRef text) { return llvm::createStringError(text); };
    llvm::SmallVector<ctjs::CallOp> opens;
    bool hasLoop = false;
    const auto census = candidate.walk([&](mlir::Operation * operation) {
        if (!budget.spend(uint64_t(1) + operation->getNumOperands())) {
            return mlir::WalkResult::interrupt();
        }
        if (llvm::isa<mlir::scf::WhileOp>(operation)) {
            auto owner = operation->getParentOfType<ctjs::FuncOp>();
            hasLoop |= owner && owner.getSymName() == contract.entry;
        }
        auto call = llvm::dyn_cast<ctjs::CallOp>(operation);
        if (call && ctjs::calleeGlobalName(call) == "__ctbrowser_for_of_open") {
            opens.push_back(call);
        }
        return mlir::WalkResult::advance();
    });
    if (census.wasInterrupted()) { return error("DOM iteration work budget exhausted"); }
    if (opens.empty()) { return llvm::Error::success(); }
    if (hostContractFingerprint(candidate) != contract.moduleSha256) {
        return error("DOM iteration fingerprint mismatch");
    }
    for (llvm::StringRef name :
         {"Array", "__ctbrowser_for_of_open", "__ctbrowser_iter_next", "__ctbrowser_iter_close"}) {
        if (!llvm::is_contained(contract.initialIntrinsics, name)) {
            return error("DOM iteration requires original Array iterator and helper identities");
        }
    }
    const auto undefined = ctjs::isUndefinedConstant;
    auto entry = candidate.lookupSymbol<ctjs::FuncOp>(contract.entry);
    if (!entry || !hasLoop || !entry.getBody().hasOneBlock()) {
        return error("DOM iteration requires a structured source entry");
    }
    struct Input {
        ctjs::CallOp producer;
        llvm::SmallVector<ctjs::GetPropertyOp> lengths;
    };
    llvm::SmallVector<Input> inputs;
    llvm::DenseSet<mlir::Operation *> recorded;
    llvm::DenseSet<mlir::Operation *> members;
    const auto normalize = [&](ctjs::CallOp open) -> llvm::Error {
        if (!undefined(open.getReceiver()) || open.getArgs().size() != 1) {
            return error("DOM iterator open requires one snapshot and an undefined receiver");
        }

        if (open->getParentOfType<ctjs::FuncOp>() != entry) {
            return error("DOM iterator must belong to the complete entry");
        }
        const mlir::Value originalSnapshot = open.getArgs().front();
        auto producer = originalSnapshot.getDefiningOp<ctjs::CallOp>();
        if (!producer && !originalSnapshot.getDefiningOp<ctjs::CallSpreadOp>()) {
            return error("DOM iterator input is not a proved owning snapshot");
        }
        // Only the importer's materialization arm (the `iterable` under
        // `if (iter === undefined)`) belongs to this open. A separate spread of
        // the same source must keep its own copy semantics.
        llvm::DenseSet<mlir::Operation *> materializations;
        const auto find = entry.walk([&](mlir::Operation * operation) {
            if (!spend()) { return mlir::WalkResult::interrupt(); }
            auto iterable = llvm::dyn_cast<ctjs::IterableOp>(operation);
            if (!iterable || iterable.getSource() != originalSnapshot) {
                return mlir::WalkResult::advance();
            }
            auto branch = llvm::dyn_cast<mlir::scf::IfOp>(iterable->getParentOp());
            if (branch && iterable->getParentRegion() == &branch.getThenRegion() &&
                strictUndefinedTest(truthyComparison(branch.getCondition()), open.getResult())) {
                materializations.insert(operation);
            }
            return mlir::WalkResult::advance();
        });
        if (find.wasInterrupted()) { return error("DOM iteration work budget exhausted"); }
        if (materializations.empty()) {
            return error("DOM iterator requires its exact source materialization arm");
        }
        recorded.insert(originalSnapshot.getDefiningOp());
        auto & input = inputs.emplace_back(producer);
        mlir::OpBuilder at(open);
        auto * iterationBody = open->getBlock();
        at.setInsertionPoint(open);
        if (!budget.spend(open.getResult().getNumUses())) {
            return error("DOM iteration work budget exhausted");
        }
        auto empty = ctjs::ConstantOp::create(at, open.getLoc(),
                                              ctjs::UndefinedAttr::get(candidate.getContext()));
        open.getResult().replaceAllUsesWith(empty.getResult());
        open.erase();

        // Fold only branches on constants. The discarded arms are unreachable
        // because the original helpers choose eager, indexed iteration for
        // these snapshots; no user callback or browser code runs in the
        // iterator-protocol arm.
        bool changed = true;
        while (changed) {
            changed = false;
            llvm::SmallVector<mlir::Operation *> operations;
            const auto scan =
                iterationBody->walk<mlir::WalkOrder::PreOrder>([&](mlir::Operation * op) {
                    if (!spend()) { return mlir::WalkResult::interrupt(); }
                    operations.push_back(op);
                    return mlir::WalkResult::advance();
                });
            if (scan.wasInterrupted()) { return error("DOM iteration work budget exhausted"); }
            // Restart after each mutation so no saved pointer can name an erased arm.
            for (mlir::Operation * operation : operations) {
                if (!spend()) { return error("DOM iteration work budget exhausted"); }
                for (mlir::Value result : operation->getResults()) {
                    if (!budget.spend(result.getNumUses())) {
                        return error("DOM iteration work budget exhausted");
                    }
                }
                at.setInsertionPoint(operation);
                if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation)) {
                    const bool bothUndefined =
                        undefined(compare.getLhs()) && undefined(compare.getRhs());
                    const bool definedMember =
                        (members.contains(compare.getLhs().getDefiningOp()) &&
                         undefined(compare.getRhs())) ||
                        (undefined(compare.getLhs()) &&
                         members.contains(compare.getRhs().getDefiningOp()));
                    if (compare.getKind() == ctjs::CompareKind::StrictEq &&
                        (bothUndefined || definedMember)) {
                        // A bounds-checked snapshot element is never undefined,
                        // so it cannot select a helper's undefined default.
                        // Each member is re-checked after DOMEntryAnalysis.
                        auto value = ctjs::ConstantOp::create(
                            at, compare.getLoc(),
                            ctjs::BooleanAttr::get(candidate.getContext(), bothUndefined));
                        compare.getResult().replaceAllUsesWith(value.getResult());
                        compare.erase();
                        changed = true;
                        break;
                    }
                }
                if (auto truth = llvm::dyn_cast<ctjs::TruthyOp>(operation)) {
                    if (auto boolean = ctjs::constantAttr<ctjs::BooleanAttr>(truth.getValue())) {
                        auto value = mlir::arith::ConstantIntOp::create(at, truth.getLoc(),
                                                                        boolean.getValue(), 1);
                        truth.getResult().replaceAllUsesWith(value.getResult());
                        truth.erase();
                        changed = true;
                        break;
                    }
                }
                if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
                    mlir::IntegerAttr boolean;
                    if (mlir::matchPattern(branch.getCondition(), mlir::m_Constant(&boolean)) &&
                        boolean.getType().isInteger(1)) {
                        auto & selected =
                            boolean.getInt() ? branch.getThenRegion() : branch.getElseRegion();
                        if (!selected.hasOneBlock() || selected.front().getNumArguments()) {
                            return error("DOM iterator branch lacks an exact selected arm");
                        }
                        auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(selected.front().back());
                        if (!yield || yield.getOperandTypes() != branch.getResultTypes()) {
                            return error("DOM iterator branch lost its value correspondence");
                        }
                        // ponytail: a discarded arm holding a recorded input is
                        // refused; prune such arms before recording if needed.
                        for (mlir::Region & region : branch->getRegions()) {
                            if (&region == &selected) { continue; }
                            const auto discarded = region.walk([&](mlir::Operation * dead) {
                                if (!spend() || recorded.contains(dead)) {
                                    return mlir::WalkResult::interrupt();
                                }
                                return mlir::WalkResult::advance();
                            });
                            if (discarded.wasInterrupted()) {
                                return error(budget.remaining
                                                 ? "DOM iteration would discard a recorded input"
                                                 : "DOM iteration work budget exhausted");
                            }
                        }
                        inlineSelectedArm(branch, &selected == &branch.getThenRegion());
                        changed = true;
                        break;
                    }
                }
                if (auto iterable = llvm::dyn_cast<ctjs::IterableOp>(operation);
                    iterable && materializations.contains(operation)) {
                    for (mlir::Operation * user : iterable->getUsers()) {
                        if (!spend()) { return error("DOM iteration work budget exhausted"); }
                        if (llvm::isa<ctjs::RootOp>(user)) { continue; }
                        auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(user);
                        if (!read || read.getObject() != iterable.getResult()) {
                            return error("DOM iteration materialization escapes its observations");
                        }
                        if (ctjs::constantKey(read.getKey()) == "length") {
                            input.lengths.push_back(read);
                            recorded.insert(read);
                        } else if (!hasOwnSnapshotBound(read, iterable.getResult(), spend)) {
                            return error(budget.remaining
                                             ? "DOM iterated index requires its own length guard"
                                             : "DOM iteration work budget exhausted");
                        } else {
                            members.insert(read);
                            recorded.insert(read);
                        }
                    }
                    iterable.getResult().replaceAllUsesWith(originalSnapshot);
                    materializations.erase(operation);
                    iterable.erase();
                    changed = true;
                    break;
                }
                if (auto call = llvm::dyn_cast<ctjs::CallOp>(operation)) {
                    if (ctjs::calleeGlobalName(call) == "__ctbrowser_iter_close" &&
                        undefined(call.getReceiver()) && call.getArgs().size() == 2 &&
                        call.getArgs()[0] == empty.getResult()) {
                        auto abrupt = ctjs::constantAttr<ctjs::BooleanAttr>(call.getArgs()[1]);
                        if (!abrupt || abrupt.getValue()) {
                            return error("DOM iterator close requires normal completion");
                        }
                        call.getResult().replaceAllUsesWith(empty.getResult());
                        call.erase();
                        changed = true;
                        break;
                    }
                }
                if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation);
                    load && load.getResult().use_empty() &&
                    (load.getName() == "__ctbrowser_for_of_open" ||
                     load.getName() == "__ctbrowser_iter_close")) {
                    load.erase();
                    changed = true;
                    break;
                }
            }
        }
        if (!materializations.empty()) {
            return error("DOM iteration lost its source materialization");
        }
        return llvm::Error::success();
    };
    // Rewrite each open, then check every input and the whole live entry.
    // Nested queries stay in their original bodies; no loop state is added.
    while (!opens.empty()) {
        if (auto failure = normalize(opens.front())) { return failure; }
        // Constant branch folding can erase another open in an unreachable arm,
        // so collect the live opens again instead of keeping stale pointers.
        opens.clear();
        const auto scan = entry.walk([&](mlir::Operation * operation) {
            if (!spend()) { return mlir::WalkResult::interrupt(); }
            auto call = llvm::dyn_cast<ctjs::CallOp>(operation);
            if (call && ctjs::calleeGlobalName(call) == "__ctbrowser_for_of_open") {
                opens.push_back(call);
            }
            return mlir::WalkResult::advance();
        });
        if (scan.wasInterrupted()) { return error("DOM iteration work budget exhausted"); }
    }
    auto transformed = contract;
    const auto fingerprint = [&] {
        if (!budget.spendWalk(candidate, [](mlir::Operation * operation) {
                return uint64_t(1) + operation->getNumOperands();
            })) {
            return false;
        }
        transformed.moduleSha256 = hostContractFingerprint(candidate);
        return true;
    };
    if (!fingerprint()) { return error("DOM iteration work budget exhausted"); }
    mlir::IRMapping mapping;
    unsigned snapshotWork = 0;
    if (auto failure = normalizeDOMSnapshotLengths(candidate, transformed,
                                                   static_cast<unsigned>(budget.remaining),
                                                   &mapping, &snapshotWork)) {
        return failure;
    }
    budget.remaining -= snapshotWork;
    if (!fingerprint()) { return error("DOM iteration work budget exhausted"); }
    const DOMEntryAnalysis proof(candidate, transformed, static_cast<unsigned>(budget.remaining));
    if (!proof.proved()) { return error(("DOM iterator entry: " + proof.reason()).str()); }
    budget.remaining -= proof.steps();
    llvm::DenseSet<mlir::Operation *> live;
    const auto censusLive = candidate.walk([&](mlir::Operation * operation) {
        if (!spend()) { return mlir::WalkResult::interrupt(); }
        live.insert(operation);
        return mlir::WalkResult::advance();
    });
    if (censusLive.wasInterrupted()) { return error("DOM iteration work budget exhausted"); }
    for (mlir::Operation * member : members) {
        if (!spend()) { return error("DOM iteration work budget exhausted"); }
        auto * mapped = mapping.lookupOrDefault(member);
        if (!live.contains(mapped)) { return error("DOM iteration lost its indexed member"); }
        auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(mapped);
        if (!read || (!proof.isElementVectorIndex(read) && !proof.isStringVectorIndex(read))) {
            return error("DOM iteration member lacks a proved snapshot index");
        }
    }
    for (Input & input : inputs) {
        if (!spend()) { return error("DOM iteration work budget exhausted"); }
        // Spread inputs have no producer: normalizeDOMSnapshotLengths erased
        // them after proving the copy exact. Never dereference those erased
        // IRMapping targets.
        if (!input.producer) { continue; }
        auto * mapped = mapping.lookupOrDefault(input.producer.getOperation());
        if (!live.contains(mapped)) { return error("DOM iterator lost its input producer"); }
        auto producer = llvm::dyn_cast<ctjs::CallOp>(mapped);
        const auto * edge = producer ? proof.call(producer) : nullptr;
        if (!edge || (!edge->returnsStringVector() && !edge->returnsElementVector())) {
            return error("DOM iterator input is not a proved owning snapshot");
        }
        if (!edge->returnsElementVector()) { continue; }
        if (!llvm::is_contained(contract.initialIntrinsics, "Element")) {
            return error("DOM element iteration requires original Element wrapper identities");
        }
        for (ctjs::GetPropertyOp original : input.lengths) {
            if (!spend()) { return error("DOM iteration work budget exhausted"); }
            auto * mappedLength = mapping.lookupOrDefault(original.getOperation());
            if (!live.contains(mappedLength)) { return error("DOM iteration lost its length"); }
            auto length = llvm::dyn_cast<ctjs::GetPropertyOp>(mappedLength);
            if (!length || !proof.isElementVectorLength(length)) {
                return error("DOM iteration length lacks complete element snapshot evidence");
            }
            // Only materialization length is capped. Other NodeList aliases
            // still observe the whole collection, including after DOM writes.
            mlir::OpBuilder at(length);
            length.getResult().replaceAllUsesWith(cappedLength(
                at, length.getLoc(), length.getType(), length.getObject(), length.getKey()));
            length.erase();
        }
    }
    if (!fingerprint()) { return error("DOM iteration work budget exhausted"); }
    const DOMEntryAnalysis capped(candidate, transformed, static_cast<unsigned>(budget.remaining));
    if (!capped.proved()) { return error(("DOM iterator capped entry: " + capped.reason()).str()); }
    return llvm::Error::success();
}

} // namespace ctcompile::ctnative
