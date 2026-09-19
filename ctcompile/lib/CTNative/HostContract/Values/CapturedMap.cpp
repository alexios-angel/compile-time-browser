#include "Arguments.hpp"

#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/ScopeExit.h"

#include <algorithm>
#include <utility>

namespace ctcompile::ctnative::host_detail {

std::optional<HostCapturedMap> analyzer::capturedMap(ctjs::CreateClosureOp closure,
                                                     ctjs::FuncOp function, mlir::Operation * call,
                                                     ctjs::GetPropertyOp read) {
    if (!step() || closure.getUpvalues().size() != 1 ||
        !llvm::is_contained(contract.initialIntrinsics, "Map")) {
        return {};
    }
    if (auto indices = closure.getEnclosingIndicesAttr();
        indices && (indices.size() != 1 || indices[0] >= 0)) {
        return {};
    }
    auto factory = closure->getParentOfType<ctjs::FuncOp>();
    if (!factory || !llvm::hasSingleElement(factory.getBody()) || !singleInvocation(factory)) {
        return {};
    }
    HostCapturedMap result;
    auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(call);
    auto capture = closure.getUpvalues().front();
    result.cell = capture.getDefiningOp<ctjs::CreateCellOp>();
    const bool prepared = !result.cell;
    auto & body = function.getBody().front();
    if (prepared) {
        if (function.getUpvalueCount() != 0 || body.getNumArguments() < 4 || !direct ||
            direct.getArgs().size() != body.getNumArguments() - ctjs::implicit_arguments) {
            return {};
        }
        result.argument = direct.getArgs().front().getDefiningOp<ctjs::LoadUpvalueOp>();
        if (!result.argument || result.argument.getIndex() != 0 ||
            result.argument.getClosure() != read.getResult() || !before(read, result.argument) ||
            !before(result.argument, call)) {
            return {};
        }
        for (mlir::OpOperand & use : result.argument.getResult().getUses()) {
            if (!step() || (use.getOwner() != call && !llvm::isa<ctjs::RootOp>(use.getOwner())) ||
                (use.getOwner() == call && use.getOperandNumber() != 3)) {
                return {};
            }
        }
    } else if (function.getUpvalueCount() != 1 || body.getNumArguments() < 3) {
        return {};
    }

    // A normalized capture may leave the original dead cell bookkeeping in
    // its factory. Recover that cell from the allocation uses and check it as
    // strictly as the source binding; markers never authorize extra writes.
    mlir::Value resource = capture;
    llvm::DenseSet<mlir::Operation *> captures;
    const auto collectCapture = [&](mlir::OpOperand & use) {
        auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(use.getOwner());
        if (!made || use.getOperandNumber() != 2 || made.getUpvalues().size() != 1 ||
            made->getBlock() != closure->getBlock()) {
            return false;
        }
        captures.insert(made);
        return true;
    };
    if (result.cell) {
        for (mlir::OpOperand & use : result.cell.getResult().getUses()) {
            if (!step()) { return {}; }
            if (auto write = llvm::dyn_cast<ctjs::CellSetOp>(use.getOwner())) {
                if (use.getOperandNumber() != 0 || result.initialization) { return {}; }
                result.initialization = write;
            }
        }
        resource =
            result.initialization ? result.initialization.getValue() : result.cell.getInitial();
    }
    result.allocation = resource.getDefiningOp<ctjs::ConstructOp>();
    if (!result.allocation || !result.allocation.getArgs().empty() ||
        result.allocation.getNewTarget() != result.allocation.getCallee() ||
        result.allocation->getBlock() != closure->getBlock() ||
        !result.allocation->isBeforeInBlock(closure)) {
        return {};
    }
    result.intrinsic = result.allocation.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
    if (!result.intrinsic || result.intrinsic.getName() != "Map" || !globals["Map"].empty() ||
        !before(result.intrinsic, result.allocation)) {
        return {};
    }
    if (prepared) {
        for (mlir::OpOperand & use : resource.getUses()) {
            if (!step()) { return {}; }
            ctjs::CreateCellOp cell;
            if (auto write = llvm::dyn_cast<ctjs::CellSetOp>(use.getOwner())) {
                cell = write.getCell().getDefiningOp<ctjs::CreateCellOp>();
                if (!cell || result.initialization) { return {}; }
                result.initialization = write;
            } else {
                cell = llvm::dyn_cast<ctjs::CreateCellOp>(use.getOwner());
            }
            if (cell) {
                if (result.cell && result.cell != cell) { return {}; }
                result.cell = cell;
            }
        }
    }
    if (result.cell) {
        if (result.cell->getBlock() != closure->getBlock() ||
            !result.cell->isBeforeInBlock(closure)) {
            return {};
        }
        if (result.initialization) {
            auto initial = result.cell.getInitial().getDefiningOp<ctjs::ConstantOp>();
            if (!initial || !llvm::isa<ctjs::UndefinedAttr>(initial.getValue()) ||
                result.initialization.getValue() != resource ||
                result.initialization->getBlock() != closure->getBlock() ||
                !result.cell->isBeforeInBlock(result.initialization) ||
                !result.allocation->isBeforeInBlock(result.initialization) ||
                !result.initialization->isBeforeInBlock(closure)) {
                return {};
            }
        } else if (result.cell.getInitial() != resource) {
            return {};
        }
        for (mlir::OpOperand & use : result.cell.getResult().getUses()) {
            if (!step()) { return {}; }
            if (llvm::isa<ctjs::RootOp>(use.getOwner()) ||
                (use.getOwner() == result.initialization.getOperation() &&
                 use.getOperandNumber() == 0) ||
                (!prepared && collectCapture(use))) {
                continue;
            }
            return {};
        }
    }
    for (mlir::OpOperand & use : resource.getUses()) {
        if (!step()) { return {}; }
        if (llvm::isa<ctjs::RootOp>(use.getOwner()) ||
            (use.getOwner() == result.cell.getOperation() && use.getOperandNumber() == 0) ||
            (use.getOwner() == result.initialization.getOperation() &&
             use.getOperandNumber() == 1) ||
            (prepared && collectCapture(use))) {
            continue;
        }
        return {};
    }

    // A selected getter cannot close the Map contents by itself. Every
    // closure that can reach the same immutable slot must have a checked body
    // and exactly one fixed-field publication in this factory's same table.
    // Walk source order so separate current calls derive identical families.
    ctjs::CreateObjectOp table;
    llvm::DenseSet<mlir::Operation *> familyFunctions;
    llvm::DenseSet<llvm::StringRef> fields;
    llvm::SmallVector<ctjs::SetPropertyOp> publications;
    for (mlir::Operation & operation : factory.getBody().front()) {
        if (!step()) { return {}; }
        if (!captures.contains(&operation)) { continue; }
        auto made = llvm::cast<ctjs::CreateClosureOp>(operation);
        auto member = callable(made.getResult());
        const auto indices = made.getEnclosingIndicesAttr();
        auto enclosingThis = made.getEnclosingThis().getDefiningOp<ctjs::ConstantOp>();
        if (!member || member == entry || member == factory ||
            !familyFunctions.insert(member).second || !llvm::hasSingleElement(member.getBody()) ||
            member.getUpvalueCount() != (prepared ? 0u : 1u) ||
            member.getBody().front().getNumArguments() < (prepared ? 4u : 3u) ||
            (indices && (indices.size() != 1 || indices[0] >= 0)) || !enclosingThis ||
            !llvm::isa<ctjs::UndefinedAttr>(enclosingThis.getValue()) ||
            !result.allocation->isBeforeInBlock(made) ||
            (result.cell && !result.cell->isBeforeInBlock(made)) ||
            (result.initialization && !result.initialization->isBeforeInBlock(made))) {
            return {};
        }
        ctjs::SetPropertyOp publication;
        for (mlir::OpOperand & use : made.getResult().getUses()) {
            if (!step()) { return {}; }
            if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
            auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
            auto owner = write ? write.getObject().getDefiningOp<ctjs::CreateObjectOp>()
                               : ctjs::CreateObjectOp{};
            if (!write || publication || use.getOperandNumber() != 2 || !owner ||
                owner->getBlock() != made->getBlock() || write->getBlock() != made->getBlock() ||
                (table && table != owner) ||
                !ctjs::ordinaryKey(ctjs::constantKey(write.getKey())) ||
                !fields.insert(ctjs::constantKey(write.getKey())).second ||
                !owner->isBeforeInBlock(write) || !made->isBeforeInBlock(write)) {
                return {};
            }
            table = owner;
            publication = write;
        }
        if (!publication) { return {}; }
        result.closures.push_back(made);
        publications.push_back(publication);
    }
    if (!captures.contains(closure) || result.closures.size() != captures.size()) { return {}; }
    llvm::DenseMap<mlir::Operation *, unsigned> creations;
    module.walk([&](ctjs::CreateClosureOp made) {
        if (!step()) { return; }
        auto member = callable(made.getResult());
        if (member && familyFunctions.contains(member)) { ++creations[member]; }
    });
    for (mlir::Operation * member : familyFunctions) {
        if (!step() || creations.lookup(member) != 1) { return {}; }
    }
    // Discover all call identities before inspecting arguments or bodies.
    // No propertyCall recursion may supply this family's own type authority.
    llvm::SmallVector<llvm::SmallVector<mlir::Operation *>> familyCalls(publications.size());
    for (ctjs::SetPropertyOp publication : publications) {
        auto member = callable(publication.getValue());
        if (!capturedMapCalls(member, publication, prepared,
                              familyCalls[result.parameters.size()])) {
            return {};
        }
        result.parameters.push_back({member, {}});
    }
    if (!scalarCallbacks(result.parameters, result)) { return {}; }
    // Once any sibling can store a local or caller-owned object, unknown Map
    // reads cannot inherit the primitive-only contents guarantee. Inspect the
    // complete family before proving even one invocation result. This only
    // removes authority; the parameter/body proofs still check every use.
    bool primitiveContents = true;
    for (unsigned index = 0; index < result.parameters.size(); ++index) {
        auto member = result.parameters[index].function;
        const auto census = member.getBody().walk([&](mlir::Operation * operation) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            if (llvm::isa<ctjs::CreateObjectOp, ctjs::ConstructOp>(operation)) {
                primitiveContents = false;
            }
            auto store = llvm::dyn_cast<ctjs::CallOp>(operation);
            auto read = store ? store.getCallee().getDefiningOp<ctjs::GetPropertyOp>()
                              : ctjs::GetPropertyOp{};
            if (!read || ctjs::constantKey(read.getKey()) != "set" || store.getArgs().size() != 2) {
                return mlir::WalkResult::advance();
            }
            auto payload = llvm::dyn_cast<mlir::BlockArgument>(store.getArgs()[1]);
            if (!payload || payload.getOwner() != &member.getBody().front() ||
                payload.getArgNumber() < (prepared ? 4u : 3u)) {
                return mlir::WalkResult::advance();
            }
            for (mlir::Operation * invocation : familyCalls[index]) {
                if (!step()) { return mlir::WalkResult::interrupt(); }
                const auto actual = explicitArgument(invocation, payload.getArgNumber());
                if (actual.getDefiningOp<ctjs::CreateObjectOp>() || elementInput(actual)) {
                    primitiveContents = false;
                }
                if (auto load = actual.getDefiningOp<ctjs::LoadGlobalOp>();
                    load && objectGlobalRead(load)) {
                    primitiveContents = false;
                }
            }
            return mlir::WalkResult::advance();
        });
        if (census.wasInterrupted() || exhausted) { return {}; }
    }
    // Positive child-kind authority is independent of invocation results. Only
    // a complete census of outer writes can establish it; membership and the
    // mere presence of a constructor cannot. The body proof below still checks
    // every operation/use, including aliases and all structural continuations.
    llvm::DenseSet<mlir::Operation *> familyInvocations;
    for (const auto & invocations : familyCalls) {
        for (mlir::Operation * invocation : invocations) {
            if (!step()) { return {}; }
            familyInvocations.insert(invocation);
        }
    }
    result.childMapContents = true;
    result.outerStringKeys = true;
    result.childStringKeys = true;
    HostChildMapEntry childEntry;
    PrimitiveAlternatives childScalar;
    const llvm::DenseMap<mlir::Value, PrimitiveAlternatives> noResults;
    bool childEntryProved = !primitiveContents, childPublished = false;
    bool childScalarProved = !primitiveContents, childLeafProved = !primitiveContents;
    for (auto [index, parameters] : llvm::enumerate(result.parameters)) {
        auto member = parameters.function;
        auto & memberBody = member.getBody().front();
        const auto outer = memberBody.getArgument(prepared ? 3 : 2);
        // An invariant may not use the result of the invocation it authorizes.
        // All actual categories must close with no family results available.
        HostMethodParameters independent{member, {}};
        if ((childEntryProved || childScalarProved || childLeafProved || result.outerStringKeys ||
             result.childStringKeys) &&
            !capturedMapParameters(member, prepared, familyCalls[index], familyInvocations,
                                   noResults, independent)) {
            childEntryProved = false;
            childScalarProved = false;
            childLeafProved = false;
            result.outerStringKeys = false;
            result.childStringKeys = false;
        }
        const auto stringKey = [&](mlir::Value value) {
            if (!step()) { return false; }
            if (auto constant = value.getDefiningOp<ctjs::ConstantOp>()) {
                return llvm::isa<ctjs::StringAttr>(constant.getValue());
            }
            auto argument = llvm::dyn_cast<mlir::BlockArgument>(value);
            if (!argument || argument.getOwner() != &memberBody ||
                argument.getArgNumber() < (prepared ? 4u : 3u)) {
                return false;
            }
            const auto position = argument.getArgNumber() - (prepared ? 4u : 3u);
            return position < independent.alternatives.size() &&
                   independent.alternatives[position].tag() ==
                       mlir::TypeID::get<ctjs::StringAttr>();
        };
        const auto mapOrigin = [&](auto && self, mlir::Value value,
                                   unsigned depth = 0) -> mlir::Value {
            if (!value || depth > 32 || !step()) { return {}; }
            if (prepared && value == outer) { return outer; }
            if (auto load = value.getDefiningOp<ctjs::LoadUpvalueOp>();
                !prepared && load && load.getIndex() == 0 && load.getClosure() == outer) {
                return outer;
            }
            if (auto made = value.getDefiningOp<ctjs::ConstructOp>()) {
                auto intrinsic = made.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
                if (intrinsic && intrinsic.getName() == "Map" && globals["Map"].empty() &&
                    made.getNewTarget() == made.getCallee() && made.getArgs().empty() &&
                    dominance.dominates(made.getCallee(), made)) {
                    return value;
                }
                return {};
            }
            auto invoke = value.getDefiningOp<ctjs::CallOp>();
            auto read = invoke ? invoke.getCallee().getDefiningOp<ctjs::GetPropertyOp>()
                               : ctjs::GetPropertyOp{};
            if (!read || read.getObject() != invoke.getReceiver()) { return {}; }
            const auto receiver = self(self, invoke.getReceiver(), depth + 1);
            const auto action = ctjs::constantKey(read.getKey());
            if (action == "set" && invoke.getArgs().size() == 2) { return receiver; }
            // This role may be used only as a child receiver, never as evidence
            // that an outer payload is a fresh constructor. The completed write
            // census and body-use proof together exclude a root stored in itself.
            if (action == "get" && invoke.getArgs().size() == 1 && receiver == outer) {
                return value;
            }
            return {};
        };
        llvm::SmallVector<std::pair<ctjs::CallOp, mlir::Value>> publications, childWrites;
        const auto census = member.getBody().walk([&](ctjs::CallOp invoke) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            auto read = invoke.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            if (!read) { return mlir::WalkResult::advance(); }
            const auto action = ctjs::constantKey(read.getKey());
            if (action != "set" && action != "delete" && action != "clear") {
                return mlir::WalkResult::advance();
            }
            const auto receiver = mapOrigin(mapOrigin, invoke.getReceiver());
            if (action != "set") {
                if (receiver != outer) { childEntryProved = false; }
                return mlir::WalkResult::advance();
            }
            if (read.getObject() != invoke.getReceiver() || invoke.getArgs().size() != 2 ||
                !receiver) {
                result.childMapContents = false;
                result.outerStringKeys = false;
                result.childStringKeys = false;
            } else if (receiver == outer) {
                result.outerStringKeys &= stringKey(invoke.getArgs()[0]);
                const auto payload = mapOrigin(mapOrigin, invoke.getArgs()[1]);
                if (!payload || !payload.getDefiningOp<ctjs::ConstructOp>()) {
                    result.childMapContents = false;
                }
                publications.emplace_back(invoke, payload);
            } else {
                result.childStringKeys &= stringKey(invoke.getArgs()[0]);
                childWrites.emplace_back(invoke, receiver);
                auto key = invoke.getArgs()[0].getDefiningOp<ctjs::ConstantOp>();
                PrimitiveAlternatives alternatives;
                bool leaf =
                    static_cast<bool>(invoke.getArgs()[1].getDefiningOp<ctjs::CreateObjectOp>());
                if (auto value = invoke.getArgs()[1].getDefiningOp<ctjs::ConstantOp>()) {
                    alternatives = PrimitiveAlternatives::forTag(value.getValue().getTypeID());
                } else if (auto argument = llvm::dyn_cast<mlir::BlockArgument>(invoke.getArgs()[1]);
                           argument && argument.getOwner() == &memberBody &&
                           argument.getArgNumber() >= (prepared ? 4u : 3u)) {
                    const auto position = argument.getArgNumber() - (prepared ? 4u : 3u);
                    if (position < independent.alternatives.size()) {
                        alternatives = independent.alternatives[position].categories();
                    }
                    leaf |= llvm::is_contained(independent.objectKeys, argument);
                }
                childLeafProved &= leaf || alternatives.known;
                // Category closure does not require initialization or a fixed key.
                // Deletion and empty publication cannot introduce another category.
                if (!alternatives.tag()) {
                    childScalarProved = false;
                } else if (!childScalar.known) {
                    childScalar = alternatives.categories();
                } else if (!(childScalar == alternatives.categories())) {
                    childScalarProved = false;
                }
                // ponytail: one literal String key and one scalar category; generalize only
                // with a per-key mutation proof when a real family needs more keys.
                if (!key || !llvm::isa<ctjs::StringAttr>(key.getValue()) || !alternatives.tag()) {
                    childEntryProved = false;
                } else if (!childEntry.key) {
                    childEntry = {key.getResult(), alternatives.categories()};
                } else if (childEntry.key.getDefiningOp<ctjs::ConstantOp>().getValue() !=
                               key.getValue() ||
                           !(childEntry.alternatives == alternatives.categories())) {
                    childEntryProved = false;
                }
            }
            return mlir::WalkResult::advance();
        });
        if (census.wasInterrupted() || exhausted) { return {}; }
        for (auto [publication, child] : publications) {
            if (!step()) { return {}; }
            childPublished = true;
            bool seeded = false;
            for (auto [write, receiver] : childWrites) {
                if (!step()) { return {}; }
                auto made = child ? child.getDefiningOp<ctjs::ConstructOp>() : ctjs::ConstructOp{};
                if (made && receiver == child && made->getBlock() == write->getBlock() &&
                    write->getBlock() == publication->getBlock() && made->isBeforeInBlock(write) &&
                    write->isBeforeInBlock(publication)) {
                    seeded = true;
                }
            }
            if (!seeded) { childEntryProved = false; }
        }
    }
    if (result.childMapContents && childEntryProved && childPublished && childEntry.key) {
        result.childEntries.push_back(childEntry);
    }
    if (result.childMapContents && childScalarProved && childPublished && childScalar.known) {
        result.childScalarContents = childScalar;
    }
    result.childLeafContents = result.childMapContents && childLeafProved && childPublished;
    result.childStringKeys &= result.childMapContents;
    // Establish invocation results before joining the complete method census.
    // Two calls to one method may have an acyclic result dependency even when
    // a method-level worklist would wait for its own unpublished result. Each
    // edge must independently pass the entire body/effect proof, starting with
    // unknown Map contents and generalized input categories. Only a completed
    // invocation supplies result evidence; cycles cannot authorize themselves.
    llvm::DenseMap<mlir::Value, PrimitiveAlternatives> completedResults;
    llvm::DenseSet<mlir::Operation *> completed;
    while (completed.size() != familyInvocations.size()) {
        bool progress = false;
        for (unsigned index = 0; index < result.parameters.size(); ++index) {
            if (!step()) { return {}; }
            auto member = result.parameters[index].function;
            for (mlir::Operation * invocation : familyCalls[index]) {
                if (!step()) { return {}; }
                if (completed.contains(invocation)) { continue; }
                HostMethodParameters parameters{member, {}};
                if (!capturedMapParameters(member, prepared, {invocation}, familyInvocations,
                                           completedResults, parameters)) {
                    continue;
                }
                // Provisional reads/calls never escape into the family plan.
                HostCapturedMap scratch;
                scratch.childMapContents = result.childMapContents;
                scratch.childEntries = result.childEntries;
                scratch.childScalarContents = result.childScalarContents;
                scratch.childLeafContents = result.childLeafContents;
                scratch.outerStringKeys = result.outerStringKeys;
                scratch.childStringKeys = result.childStringKeys;
                scratch.scalarCallbacks = result.scalarCallbacks;
                PrimitiveAlternatives alternatives;
                if (!capturedMapBody(member, prepared, primitiveContents, parameters, scratch,
                                     alternatives)) {
                    return {};
                }
                if (alternatives.known && (alternatives.truthy | alternatives.falsy)) {
                    completedResults.try_emplace(invocation->getResult(0), alternatives);
                }
                completed.insert(invocation);
                progress = true;
            }
        }
        if (!progress || exhausted) { return {}; }
    }
    // Invocation evidence does not authorize a published method or its sibling
    // family. Recheck every body with ALL actual categories, including later
    // calls and uncalled zero-argument siblings. Only this final census records
    // the owning plan. No result is evaluated or substituted, and every getter
    // and mutation remains in runtime order.
    for (unsigned index = 0; index < result.parameters.size(); ++index) {
        if (!step()) { return {}; }
        auto & parameters = result.parameters[index];
        PrimitiveAlternatives alternatives;
        if (!capturedMapParameters(parameters.function, prepared, familyCalls[index],
                                   familyInvocations, completedResults, parameters, &result) ||
            !capturedMapBody(parameters.function, prepared, primitiveContents, parameters, result,
                             alternatives)) {
            return {};
        }
    }
    // A strict identity guard can give one returned SSA value the caller's
    // initialized own fields on that arm. The payload schema and return carrier
    // alone supply neither identity nor presence; every family effect and caller
    // leaf write above must have closed before this read is published.
    const auto guardedLeafRead = [&](ctjs::GetPropertyOp read) {
        const auto receiver = read.getObject();
        if (!familyInvocations.contains(receiver.getDefiningOp()) ||
            !dominance.dominates(receiver, read) || !dominance.dominates(read.getKey(), read) ||
            !ctjs::ordinaryKey(ctjs::constantKey(read.getKey()))) {
            return false;
        }
        for (auto * child = read.getOperation(); child->getParentOp() != entry;
             child = child->getParentOp()) {
            if (!step()) { return false; }
            auto branch = llvm::dyn_cast<mlir::scf::IfOp>(child->getParentOp());
            if (!branch) { return false; }
            bool positive = child->getParentRegion() == &branch.getThenRegion();
            mlir::Value condition = branch.getCondition();
            for (unsigned depth = 0; depth < 16; ++depth) {
                if (!step() || !dominance.dominates(condition, branch)) { return false; }
                if (auto truthy = condition.getDefiningOp<ctjs::TruthyOp>()) {
                    condition = truthy.getValue();
                } else if (auto unary = condition.getDefiningOp<ctjs::UnaryOp>();
                           unary && unary.getKind() == ctjs::UnaryKind::Not) {
                    positive = !positive;
                    condition = unary.getOperand();
                } else {
                    break;
                }
            }
            auto compare = condition.getDefiningOp<ctjs::CompareOp>();
            if (!positive || !compare || compare.getKind() != ctjs::CompareKind::StrictEq) {
                continue;
            }
            mlir::Value other;
            if (compare.getLhs() == receiver) { other = compare.getRhs(); }
            if (compare.getRhs() == receiver) { other = compare.getLhs(); }
            if (!other || !dominance.dominates(receiver, compare) ||
                !dominance.dominates(other, compare)) {
                continue;
            }
            auto made = other.getDefiningOp<ctjs::CreateObjectOp>();
            if (auto load = other.getDefiningOp<ctjs::LoadGlobalOp>()) {
                const auto origin = objectGlobalRead(load);
                if (!origin) { return false; }
                made = origin->object;
            }
            if (!made || made->getParentOp() != entry) { continue; }
            for (ctjs::SetPropertyOp write : result.leafWrites) {
                if (!step()) { return false; }
                if (write->getParentOp() == entry && object(write.getObject()) == made &&
                    ctjs::constantKey(write.getKey()) == ctjs::constantKey(read.getKey()) &&
                    dominance.properlyDominates(write.getOperation(), read)) {
                    return true;
                }
            }
        }
        return false;
    };
    if (!result.leafWrites.empty()) {
        const auto checked = entry.getBody().walk([&](ctjs::GetPropertyOp read) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            if (read->getParentOp() != entry && guardedLeafRead(read)) {
                result.leafReads.push_back(read);
            }
            return mlir::WalkResult::advance();
        });
        if (checked.wasInterrupted()) { return {}; }
    }
    llvm::SmallVector<ctjs::GetPropertyOp> unguarded;
    const auto fieldCensus = entry.getBody().walk([&](ctjs::GetPropertyOp read) {
        if (!step()) { return mlir::WalkResult::interrupt(); }
        if (familyInvocations.contains(read.getObject().getDefiningOp()) &&
            !llvm::is_contained(result.leafReads, read)) {
            unguarded.push_back(read);
        }
        return mlir::WalkResult::advance();
    });
    if (fieldCensus.wasInterrupted()) { return {}; }
    bool scalarStore = false;
    if (unguarded.empty()) {
        const auto stores = entry.getBody().walk([&](ctjs::StoreGlobalOp store) {
            if (!step()) { return mlir::WalkResult::interrupt(); }
            // An already scalar generalized result needs no entry refinement.
            scalarStore |= familyInvocations.contains(store.getValue().getDefiningOp()) &&
                           !completedResults.lookup(store.getValue()).tag();
            return mlir::WalkResult::advance();
        });
        if (stores.wasInterrupted()) { return {}; }
    }
    if (!unguarded.empty() || scalarStore) {
        // Complete reusable effects above remain independent of these optional
        // entry-order facts. Never carry mutable state through the category DAG:
        // its iteration order groups calls by method, not by execution order.
        llvm::SmallVector<mlir::Value> inputs;
        llvm::SmallVector<unsigned> partition;
        for (mlir::BlockArgument argument : entry.getBody().front().getArguments()) {
            if (!step()) { return {}; }
            if (elementInput(argument)) {
                inputs.push_back(argument);
                partition.push_back(0);
            }
        }
        std::vector<HostReturnedLeaf> leaves;
        std::vector<HostReturnedScalar> scalars;
        bool complete = true;
        bool firstPartition = true;
        // Every equality partition of the stable inputs denotes possible callers.
        // Intersect exact identities and join scalar alternatives across all of
        // them. No partition's branch choice narrows the reusable method body.
        // ponytail: Bell-number replay uses the shared work budget; symbolic
        // joins can replace enumeration if measured input counts outgrow it.
        while (true) {
            if (!step()) { return {}; }
            CapturedMapInvocation invocation;
            invocation.root = result.allocation.getResult();
            auto & initial = invocation.states[{invocation.root, nullptr}];
            initial.completeKeys = true;
            initial.currentSize = 0;
            for (auto [index, input] : llvm::enumerate(inputs)) {
                if (!step()) { return {}; }
                invocation.elementClasses[input] = partition[index];
            }
            std::size_t visited = 0;
            for (mlir::Operation & operation : entry.getBody().front()) {
                if (!step()) { return {}; }
                if (!familyInvocations.contains(&operation)) { continue; }
                ctjs::FuncOp member;
                for (unsigned index = 0; index < familyCalls.size(); ++index) {
                    if (!step()) { return {}; }
                    if (llvm::is_contained(familyCalls[index], &operation)) {
                        member = result.parameters[index].function;
                        break;
                    }
                }
                HostMethodParameters parameters{member, {}};
                if (!member ||
                    !capturedMapParameters(member, prepared, {&operation}, familyInvocations,
                                           completedResults, parameters)) {
                    complete = false;
                    break;
                }
                invocation.call = &operation;
                invocation.arguments.clear();
                for (auto parameter :
                     member.getBody().front().getArguments().drop_front(prepared ? 4u : 3u)) {
                    if (!step()) { return {}; }
                    auto actual = explicitArgument(&operation, parameter.getArgNumber());
                    if (auto load = actual.getDefiningOp<ctjs::LoadGlobalOp>()) {
                        if (auto object = objectGlobalRead(load)) {
                            actual = object->object.getResult();
                        }
                    }
                    invocation.arguments[parameter] = actual;
                }
                HostCapturedMap scratch;
                scratch.childMapContents = result.childMapContents;
                scratch.childEntries = result.childEntries;
                scratch.childScalarContents = result.childScalarContents;
                scratch.childLeafContents = result.childLeafContents;
                scratch.outerStringKeys = result.outerStringKeys;
                scratch.childStringKeys = result.childStringKeys;
                scratch.scalarCallbacks = result.scalarCallbacks;
                PrimitiveAlternatives alternatives;
                if (!capturedMapBody(member, prepared, primitiveContents, parameters, scratch,
                                     alternatives, &invocation)) {
                    complete = false;
                    break;
                }
                auto leaf = invocation.returnedLeaf
                                ? invocation.returnedLeaf.getDefiningOp<ctjs::CreateObjectOp>()
                                : ctjs::CreateObjectOp{};
                if (leaf && (leaf->getParentOp() != entry ||
                             !dominance.properlyDominates(leaf.getOperation(), &operation))) {
                    leaf = {};
                }
                if (!step()) { return {}; }
                if (firstPartition) {
                    leaves.push_back({&operation, leaf});
                    scalars.push_back({&operation, alternatives});
                } else {
                    if (visited >= leaves.size() || leaves[visited].call != &operation) {
                        complete = false;
                        break;
                    }
                    if (leaves[visited].object != leaf) { leaves[visited].object = {}; }
                    scalars[visited].alternatives =
                        scalars[visited].alternatives.joined(alternatives);
                }
                ++visited;
            }
            complete &= visited == familyInvocations.size();
            if (!complete) { break; }
            firstPartition = false;
            // Restricted-growth strings enumerate canonical partitions without
            // recursion or retaining every case. The first class stays zero.
            bool next = false;
            for (std::size_t end = partition.size(); end > 1; --end) {
                if (!step()) { return {}; }
                const std::size_t index = end - 1;
                unsigned limit = 1;
                for (std::size_t before = 0; before < index; ++before) {
                    if (!step()) { return {}; }
                    limit = std::max(limit, partition[before] + 1);
                }
                if (partition[index] == limit) { continue; }
                ++partition[index];
                for (std::size_t after = end; after < partition.size(); ++after) {
                    if (!step()) { return {}; }
                    partition[after] = 0;
                }
                next = true;
                break;
            }
            if (!next) { break; }
        }
        if (complete) {
            for (const auto & leaf : leaves) {
                if (!step()) { return {}; }
                if (leaf.object) { result.returnedLeaves.push_back(leaf); }
            }
            for (const auto & scalar : scalars) {
                if (!step()) { return {}; }
                const auto alternatives = scalar.alternatives;
                if (alternatives.known && (alternatives.truthy | alternatives.falsy)) {
                    result.returnedScalars.push_back(scalar);
                }
            }
            for (ctjs::GetPropertyOp read : unguarded) {
                if (!step()) { return {}; }
                if (!ctjs::ordinaryKey(ctjs::constantKey(read.getKey())) ||
                    !dominance.dominates(read.getObject(), read) ||
                    !dominance.dominates(read.getKey(), read)) {
                    continue;
                }
                for (const auto & leaf : result.returnedLeaves) {
                    if (!step()) { return {}; }
                    if (leaf.call != read.getObject().getDefiningOp()) { continue; }
                    for (ctjs::SetPropertyOp write : result.leafWrites) {
                        if (!step()) { return {}; }
                        if (write->getParentOp() == entry &&
                            object(write.getObject()) == leaf.object &&
                            ctjs::constantKey(write.getKey()) == ctjs::constantKey(read.getKey()) &&
                            dominance.properlyDominates(write.getOperation(), read)) {
                            result.leafReads.push_back(read);
                            break;
                        }
                    }
                }
            }
        }
    }
    if (exhausted || !capturedMapOuterKeys(prepared, familyCalls, result)) { return {}; }
    // Only the completed whole-family proof supplies entry expression facts.
    // The invocation worklist above uses its own map, so provisional or cyclic
    // dependencies cannot borrow these published facts to prove themselves.
    for (const auto & [value, alternatives] : completedResults) {
        if (!step()) { return {}; }
        capturedResults[value] = alternatives.categories();
    }
    return result;
}

} // namespace ctcompile::ctnative::host_detail
