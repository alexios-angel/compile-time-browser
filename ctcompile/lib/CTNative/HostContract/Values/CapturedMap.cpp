//===- CapturedMap.cpp - Captured-Map method families -----------*- C++ -*-===//
//
// Implements analyzer::capturedMap, which proves that the methods sharing one
// captured `new Map()` keep its contents to the types the native lowering
// relies on.
//
//===----------------------------------------------------------------------===//

#include "Arguments.hpp"

#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/ScopeExit.h"

#include <algorithm>
#include <utility>

namespace ctcompile::ctnative::host_detail {

/// Proves that `call` invokes a method of a captured-Map family: closures
/// created once in a single-invocation factory that all capture one plain
/// `new Map()` (as an upvalue or, once prepared, as a leading parameter) and
/// are each stored into one property of the same object literal. On success,
/// returns the allocation, the family's closures and the per-method parameter
/// and content facts the native lowering may rely on. Returns nullopt when any
/// check fails or the analyzer's step budget runs out.
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
    // A prepared member takes its captured Map as a leading formal, after the
    // three implicit entry arguments.
    const unsigned formals = prepared ? 4u : 3u;
    auto & body = function.getBody().front();
    if (prepared) {
        if (function.getUpvalueCount() != 0 || body.getNumArguments() < 4 || !direct ||
            direct.getArgs().size() != body.getNumArguments() - ctjs::implicit_arguments) {
            return {};
        }
        // The call's first explicit argument (operand 3, after receiver,
        // new.target and callee) is the prepared Map parameter.
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

    // After normalization the factory may still contain the original, now
    // dead, cell. Recover it from the allocation's uses and check it as
    // strictly as the source binding. The ctnative.* attributes never permit
    // extra writes.
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
    if (!result.allocation || !plainMapConstruction(result.allocation) ||
        result.allocation->getBlock() != closure->getBlock() ||
        !result.allocation->isBeforeInBlock(closure)) {
        return {};
    }
    result.intrinsic = result.allocation.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
    // `Map` must still be the initial intrinsic: no script stores to it.
    if (!globals["Map"].empty() || !before(result.intrinsic, result.allocation)) { return {}; }
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
            if (!ctjs::isUndefinedConstant(result.cell.getInitial()) ||
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

    // One method alone cannot prove what the Map contains: every closure that
    // captures the same Map must be checked too. Each must be stored exactly
    // once, under a constant key, into the same object literal (the family's
    // "table") in this factory. Walk in source order so separate calls derive
    // the same family.
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
        if (!member || member == entry || member == factory ||
            !familyFunctions.insert(member).second || !llvm::hasSingleElement(member.getBody()) ||
            member.getUpvalueCount() != (prepared ? 0u : 1u) ||
            member.getBody().front().getNumArguments() < formals ||
            (indices && (indices.size() != 1 || indices[0] >= 0)) ||
            !ctjs::isUndefinedConstant(made.getEnclosingThis()) ||
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
    // Find every call site of every family method before inspecting arguments
    // or bodies, so no recursive propertyCall analysis can use this family's
    // own unproved types as evidence.
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
    // If any method can store an object (made locally or passed by a caller)
    // into a Map, reads of unknown entries are no longer guaranteed to be
    // primitive. Scan the whole family before proving any call's result. This
    // can only withdraw the guarantee; the parameter and body proofs still
    // check every use.
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
                payload.getArgNumber() < formals) {
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
    // Facts about the child Maps stored in the outer Map (childMapContents,
    // key kinds, entry types) do not depend on any call's result. Only a scan
    // of every `set` in every family method can establish them; neither family
    // membership nor a constructor call alone is enough. The body proof below
    // still checks every operation and use, including aliases.
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
        // The outer Map: the prepared parameter (argument 3), or the callee
        // (argument 2) whose upvalue 0 holds it.
        const auto outer = memberBody.getArgument(prepared ? 3 : 2);
        // A fact must not depend on the result of a call it is used to prove,
        // so argument types are derived here with no family results available.
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
                argument.getArgNumber() < formals) {
                return false;
            }
            const auto position = argument.getArgNumber() - formals;
            return position < independent.alternatives.size() &&
                   independent.alternatives[position].tag() ==
                       mlir::TypeID::get<ctjs::StringAttr>();
        };
        // Traces a value back to the Map it came from: the captured outer Map,
        // a fresh `new Map()`, `map.set(...)` (which returns its receiver), or
        // `outer.get(k)` (a child Map). The depth limit of 32 bounds `set`
        // chains.
        const auto mapOrigin = [&](auto && self, mlir::Value value,
                                   unsigned depth = 0) -> mlir::Value {
            if (!value || depth > 32 || !step()) { return {}; }
            if (prepared && value == outer) { return outer; }
            if (auto load = value.getDefiningOp<ctjs::LoadUpvalueOp>();
                !prepared && load && load.getIndex() == 0 && load.getClosure() == outer) {
                return outer;
            }
            if (auto made = value.getDefiningOp<ctjs::ConstructOp>()) {
                if (plainMapConstruction(made) && globals["Map"].empty() &&
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
            // A child Map found by `outer.get(k)` only counts as the receiver
            // of a child write, never as proof that a value stored into the
            // outer Map is a fresh `new Map()`. The scan of every write and the
            // body proof together rule out the outer Map being stored in
            // itself.
            if (action == "get" && invoke.getArgs().size() == 1 && receiver == outer) {
                return value;
            }
            return {};
        };
        llvm::SmallVector<std::pair<ctjs::CallOp, mlir::Value>> publications, childWrites;
        // Scan every set/delete/clear on a Map in this method.
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
                           argument.getArgNumber() >= formals) {
                    const auto position = argument.getArgNumber() - formals;
                    if (position < independent.alternatives.size()) {
                        alternatives = independent.alternatives[position].categories();
                    }
                    leaf |= llvm::is_contained(independent.objectKeys, argument);
                }
                childLeafProved &= leaf || alternatives.known;
                // The type of child values needs neither initialization nor a
                // fixed key: deleting entries or publishing an empty child Map
                // cannot add another type.
                if (!alternatives.tag()) {
                    childScalarProved = false;
                } else if (!childScalar.known) {
                    childScalar = alternatives.categories();
                } else if (!(childScalar == alternatives.categories())) {
                    childScalarProved = false;
                }
                // ponytail: one literal String key and one scalar type; generalize
                // only with a per-key mutation proof when a real family needs more
                // keys.
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
    // Prove call results one call at a time, before the whole-method check
    // below. Two calls to one method may depend on each other's results
    // without a cycle, which a per-method worklist would wrongly treat as the
    // method waiting on itself. Each call must pass the full body proof on its
    // own, starting from unknown Map contents. Only a call proved this way
    // contributes its result type, so a cycle can never prove itself.
    llvm::DenseMap<mlir::Value, PrimitiveAlternatives> completedResults;
    llvm::DenseSet<mlir::Operation *> completed;
    // A scratch copy of the family facts, so reads and calls recorded while
    // proving one call never reach `result`.
    const auto provisional = [&] {
        HostCapturedMap scratch;
        scratch.childMapContents = result.childMapContents;
        scratch.childEntries = result.childEntries;
        scratch.childScalarContents = result.childScalarContents;
        scratch.childLeafContents = result.childLeafContents;
        scratch.outerStringKeys = result.outerStringKeys;
        scratch.childStringKeys = result.childStringKeys;
        scratch.scalarCallbacks = result.scalarCallbacks;
        return scratch;
    };
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
                auto scratch = provisional();
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
    // Proving individual calls does not prove the methods themselves. Recheck
    // every body with the argument types of all its calls, including later
    // calls and zero-argument methods nobody calls. Only this final pass
    // records into `result`. No result is evaluated or substituted, and every
    // getter and mutation stays in run-time order.
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
    // Whether a caller write to `made` initializes `read`'s field before the
    // read. Costs one step per write; nullopt when the budget runs out.
    const auto initializedRead = [&](ctjs::CreateObjectOp made,
                                     ctjs::GetPropertyOp read) -> std::optional<bool> {
        for (ctjs::SetPropertyOp write : result.leafWrites) {
            if (!step()) { return std::nullopt; }
            if (write->getParentOp() == entry && object(write.getObject()) == made &&
                ctjs::constantKey(write.getKey()) == ctjs::constantKey(read.getKey()) &&
                dominance.properlyDominates(write.getOperation(), read)) {
                return true;
            }
        }
        return false;
    };
    // Whether `read` reads a field of a family call's result inside an arm
    // guarded by `result === obj`, where `obj` is an entry object whose field
    // the caller initialized before the read. On that arm the result is `obj`,
    // so the field is present. The returned value's type alone proves neither
    // identity nor presence.
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
            // Strip up to 16 truthiness conversions and `!` negations.
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
            auto made = namedObject(other);
            if (!made && other.getDefiningOp<ctjs::LoadGlobalOp>()) { return false; }
            if (!made || made->getParentOp() != entry) { continue; }
            if (auto initialized = initializedRead(made, read); !initialized || *initialized) {
                return initialized.value_or(false);
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
            // A result already proved scalar needs no refinement from
            // entry-function order.
            scalarStore |= familyInvocations.contains(store.getValue().getDefiningOp()) &&
                           !completedResults.lookup(store.getValue()).tag();
            return mlir::WalkResult::advance();
        });
        if (stores.wasInterrupted()) { return {}; }
    }
    if (!unguarded.empty() || scalarStore) {
        // The proofs above do not depend on these optional facts about
        // entry-function order. Do not reuse the per-method results
        // (completedResults) for mutable state: they group calls by method,
        // not by execution order, so this replays the entry block in source
        // order instead.
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
        // Any partition of the element inputs into equal groups is possible.
        // Replay the entry once per partition, keep a returned object only if
        // every replay returns the same one, and join the scalar types. No
        // single partition's branch choice narrows the shared method body.
        // ponytail: the replay count is the Bell number of the input count,
        // paid from the shared work budget; replace the enumeration with
        // symbolic joins if real input counts outgrow it.
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
                for (auto parameter : member.getBody().front().getArguments().drop_front(formals)) {
                    if (!step()) { return {}; }
                    auto actual = explicitArgument(&operation, parameter.getArgNumber());
                    if (auto object = namedObject(actual)) { actual = object.getResult(); }
                    invocation.arguments[parameter] = actual;
                }
                auto scratch = provisional();
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
            // Advance to the next partition as a restricted-growth string, with
            // no recursion and without keeping earlier partitions. The first
            // element's group stays zero.
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
                    const auto initialized = initializedRead(leaf.object, read);
                    if (!initialized) { return {}; }
                    if (*initialized) { result.leafReads.push_back(read); }
                }
            }
        }
    }
    if (exhausted || !capturedMapOuterKeys(prepared, familyCalls, result)) { return {}; }
    // Publish call result types only now that the whole family is proved.
    // The per-call worklist above used its own map, so provisional or cyclic
    // dependencies could not use these facts to prove themselves.
    for (const auto & [value, alternatives] : completedResults) {
        if (!step()) { return {}; }
        capturedResults[value] = alternatives.categories();
    }
    return result;
}

} // namespace ctcompile::ctnative::host_detail
