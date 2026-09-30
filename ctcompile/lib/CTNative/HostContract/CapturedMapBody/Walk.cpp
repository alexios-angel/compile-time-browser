#include "Walk.hpp"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "llvm/ADT/StringMap.h"

#include <algorithm>
#include <cmath>

namespace ctcompile::ctnative::host_detail::captured_map_detail {

// SSA scalar facts and the complete use census are immutable across paths.
// Mutable contents are copied for each arm and intersected only after both
// arms finish. An observed startup condition never selects a future path.
bool Walk::walk(mlir::Block & block, unsigned depth) {
    if (depth > 32 || !owner.step()) { return false; }
    for (mlir::Operation & operation : block) {
        if (!owner.step()) { return false; }
        // An exited entry frame can only flow through the scalar return
        // joins. No later operation may use it or perform another effect.
        if (frameExited && !llvm::isa<ctjs::ReturnOp, mlir::scf::YieldOp>(operation)) {
            return false;
        }
        if (callbackReads.contains(&operation) || callbackCalls.contains(&operation)) {
            for (mlir::Value operand : operation.getOperands()) {
                if (!owner.step() || !owner.dominance.dominates(operand, &operation)) {
                    return false;
                }
            }
            if (callbackCalls.contains(&operation)) {
                auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(operation);
                auto args =
                    direct ? direct.getArgs() : llvm::cast<ctjs::CallOp>(operation).getArgs();
                if (args.size() != 1 || alternatives.lookup(args.front()).tag() !=
                                            mlir::TypeID::get<ctjs::StringAttr>()) {
                    return false;
                }
                // The separate complete scalar effect proof excludes Map
                // access/reentry, so this call preserves current Map facts.
                scalar<ctjs::UndefinedAttr>(operation.getResult(0));
            }
            continue;
        }
        if (auto constant = llvm::dyn_cast<ctjs::ConstantOp>(operation)) {
            if (!ctjs::isPrimitiveAttr(constant.getValue())) { return false; }
            scalar(constant.getResult(), PrimitiveAlternatives::literal(constant.getValue()));
        } else if (auto constant = llvm::dyn_cast<mlir::arith::ConstantOp>(operation)) {
            // CFG-to-SCF may leave an unused integer dispatch constant. Only
            // exact i1 constants can serve as conditions in this body proof.
            if (!llvm::isa<mlir::IntegerAttr>(constant.getValue())) { return false; }
            if (constant.getType().isInteger(1)) { flags.insert(constant.getResult()); }
        } else if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation)) {
            if (load.getName() == "Array") {
                if (!prepareSnapshots() || !copies->builtins.contains(load)) { return false; }
                result.snapshotOperations.push_back(load);
                continue;
            }
            if (load.getName() != "Map" || !owner.globals["Map"].empty()) { return false; }
            constructors.insert(load);
        } else if (auto made = llvm::dyn_cast<ctjs::ConstructOp>(operation)) {
            if (!plainMapConstruction(made) ||
                !constructors.contains(made.getCallee().getDefiningOp()) ||
                !owner.dominance.dominates(made.getCallee(), made)) {
                return false;
            }
            const CapturedMapOrigin origin{made.getResult(),
                                           invocation ? invocation->call : nullptr};
            maps.try_emplace(made.getResult(), origin);
            auto & state = mapStates[origin];
            state.completeKeys = true;
            state.currentSize = 0;
            allocations.insert(made);
            result.childMaps.push_back(made);
        } else if (auto made = llvm::dyn_cast<ctjs::CreateObjectOp>(operation)) {
            if (invocation) { return false; }
            objects.try_emplace(made.getResult(), made.getResult());
            result.leafObjects.push_back(made);
        } else if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(operation)) {
            const auto payload = alternatives.lookup(write.getValue());
            constexpr unsigned scalars =
                PrimitiveAlternatives::Number | PrimitiveAlternatives::Boolean |
                PrimitiveAlternatives::String | PrimitiveAlternatives::Null |
                PrimitiveAlternatives::Undefined;
            const unsigned mask = payload.truthy | payload.falsy;
            if (!objects.contains(write.getObject()) ||
                !ctjs::ordinaryKey(ctjs::constantKey(write.getKey())) ||
                !primitives.contains(write.getValue()) || !payload.known || !mask ||
                (mask & ~scalars)) {
                return false;
            }
            objectWrites.insert(write);
            result.leafWrites.push_back(write);
            const auto origin = objects.lookup(write.getObject());
            const auto key = ctjs::constantKey(write.getKey());
            bool found = false;
            for (auto & field : fields) {
                if (!owner.step()) { return false; }
                if (field.object == origin && field.key == key) {
                    field.payload = payload;
                    found = true;
                    break;
                }
            }
            if (!found) { fields.push_back({origin, key, payload}); }
        } else if (auto concat = llvm::dyn_cast<ctjs::BinaryOp>(operation)) {
            const auto string = [&](mlir::Value value) {
                return primitives.contains(value) &&
                       alternatives.lookup(value).tag() == mlir::TypeID::get<ctjs::StringAttr>();
            };
            const bool left = string(concat.getLhs()), right = string(concat.getRhs());
            if (concat.getKind() != ctjs::BinaryKind::Concat ||
                !((left && (right || stringSnapshotElements.contains(concat.getRhs()))) ||
                  (right && stringSnapshotElements.contains(concat.getLhs())))) {
                return false;
            }
            concatenations.insert(concat);
            result.snapshotOperations.push_back(concat);
            scalar<ctjs::StringAttr>(concat.getResult());
        } else if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation)) {
            if (compare.getKind() != ctjs::CompareKind::StrictEq ||
                (!primitives.contains(compare.getLhs()) && !objects.contains(compare.getLhs()) &&
                 !leafValues.contains(compare.getLhs()) &&
                 !snapshotElements.contains(compare.getLhs())) ||
                (!primitives.contains(compare.getRhs()) && !objects.contains(compare.getRhs()) &&
                 !leafValues.contains(compare.getRhs()) &&
                 !snapshotElements.contains(compare.getRhs()))) {
                return false;
            }
            identities.insert(compare);
            scalar<ctjs::BooleanAttr>(compare.getResult());
        } else if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation)) {
            if (unary.getKind() != ctjs::UnaryKind::Not ||
                (!primitives.contains(unary.getOperand()) &&
                 !leafValues.contains(unary.getOperand()))) {
                return false;
            }
            scalar<ctjs::BooleanAttr>(unary.getResult());
        } else if (auto truthy = llvm::dyn_cast<ctjs::TruthyOp>(operation)) {
            if (!primitives.contains(truthy.getValue()) &&
                !leafValues.contains(truthy.getValue())) {
                return false;
            }
            flags.insert(truthy.getResult());
        } else if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
            if (!walkIf(branch, depth)) { return false; }
        } else if (auto load = llvm::dyn_cast<ctjs::LoadUpvalueOp>(operation)) {
            if (prepared || load.getIndex() != 0 || load.getClosure() != body.getArgument(2)) {
                return false;
            }
            result.upvalues.push_back(load);
            upvalues.insert(load);
            maps.try_emplace(load.getResult(), capturedOrigin);
        } else if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
            const auto key = ctjs::constantKey(read.getKey());
            if (copies && copies->builtins.contains(read)) {
                result.snapshotOperations.push_back(read);
                continue;
            }
            if (copies && copies->calls.contains(read.getObject().getDefiningOp())) {
                if (key == "length") {
                    scalar<ctjs::NumberAttr>(read.getResult());
                } else {
                    auto number = ctjs::constantAttr<ctjs::NumberAttr>(read.getKey());
                    if (!number || !std::isfinite(number.getDouble()) || number.getDouble() < 0 ||
                        number.getDouble() >= 4294967295.0 ||
                        std::floor(number.getDouble()) != number.getDouble()) {
                        return false;
                    }
                    snapshotElements.insert(read.getResult());
                    auto copy = read.getObject().getDefiningOp<ctjs::CallOp>();
                    auto iterator = copy.getArgs().front().getDefiningOp<ctjs::CallOp>();
                    const auto origin = maps.lookup(iterator.getReceiver());
                    if (origin.first && (origin == capturedOrigin ? result.outerStringKeys
                                                                  : result.childStringKeys)) {
                        stringSnapshotElements.insert(read.getResult());
                    }
                }
                snapshotReads.insert(read);
                result.snapshotOperations.push_back(read);
                continue;
            }
            if (auto origin = objects.lookup(read.getObject())) {
                if (!ctjs::ordinaryKey(key)) { return false; }
                PrimitiveAlternatives payload;
                for (const auto & field : fields) {
                    if (!owner.step()) { return false; }
                    if (field.object == origin && field.key == key) {
                        payload = field.payload;
                        break;
                    }
                }
                // Only a definitely initialized own field avoids prototype
                // lookup. Saved scalar values keep their read-time facts;
                // later writes through any alias update the allocation.
                if (!payload.known) { return false; }
                scalar(read.getResult(), payload);
                objectReads.insert(read);
                result.leafReads.push_back(read);
                continue;
            }
            if (!maps.contains(read.getObject()) ||
                (key != "size" && key != "keys" && !mapMethodArity(key))) {
                return false;
            }
            if (key == "keys" && !prepareSnapshots()) { return false; }
            result.reads.push_back(read);
            reads.insert(read);
            if (key == "size") {
                scalar<ctjs::NumberAttr>(read.getResult());
                // Capture this position's cardinality, including an equal
                // structural join, independently of all later mutations.
                const auto fact = sizeFacts(mapStates[maps.lookup(read.getObject())]);
                if (!fact) { return false; }
                sizeBounds[read.getResult()] = fact->lower;
                if (fact->exact) { exactSizes[read.getResult()] = *fact->exact; }
            }
        } else if (auto invoke = llvm::dyn_cast<ctjs::CallOp>(operation)) {
            if (!mapCall(invoke)) { return false; }
        } else if (auto ret = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
            if (depth != 0 || returned || &operation != &block.back() || (frame && !frameExited) ||
                (!primitives.contains(ret.getValue()) && !leafValues.contains(ret.getValue()))) {
                return false;
            }
            returned = ret;
        } else if (auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(operation)) {
            if (depth == 0 || &operation != &block.back()) { return false; }
            for (mlir::Value value : yield.getOperands()) {
                if (!owner.step() || (!primitives.contains(value) && !leafValues.contains(value))) {
                    return false;
                }
            }
        } else if (auto enter = llvm::dyn_cast<ctjs::FrameEnterOp>(operation)) {
            const auto count = enter->getAttrOfType<mlir::IntegerAttr>("reg_count");
            if (depth != 0 || frame || enter->getNumOperands() != 0 ||
                enter->getNumResults() != 1 ||
                !llvm::isa<ctjs::ContextType>(enter->getResult(0).getType()) || !count ||
                !count.getType().isInteger(32) || count.getInt() < 0) {
                return false;
            }
            frame = enter;
        } else if (auto exit = llvm::dyn_cast<ctjs::FrameExitOp>(operation)) {
            if (!frame || exit->getNumOperands() != 1 || exit->getNumResults() != 0 ||
                exit.getContext() != frame.getContext()) {
                return false;
            }
            frameExited = true;
            frameUses.insert(exit);
        } else if (auto root = llvm::dyn_cast<ctjs::RootOp>(operation)) {
            if (!frame || root->getNumOperands() != 2 || root->getNumResults() != 0 ||
                !llvm::isa<ctjs::ValueType>(root->getOperand(1).getType()) ||
                root.getContext() != frame.getContext()) {
                return false;
            }
            frameUses.insert(root);
        } else {
            return false;
        }
    }
    return true;
}

// A two-armed scalar branch: path-local Map, field and scalar facts per arm,
// joined after both finish.
bool Walk::walkIf(mlir::scf::IfOp branch, unsigned depth) {
    const bool hasElse = !branch.getElseRegion().empty();
    if (!flags.contains(branch.getCondition()) || !branch.getThenRegion().hasOneBlock() ||
        branch.getThenRegion().front().getNumArguments() != 0 ||
        (hasElse && (!branch.getElseRegion().hasOneBlock() ||
                     branch.getElseRegion().front().getNumArguments() != 0)) ||
        (!hasElse && branch.getNumResults() != 0)) {
        return false;
    }
    auto thenYield =
        llvm::dyn_cast<mlir::scf::YieldOp>(branch.getThenRegion().front().getTerminator());
    auto elseYield =
        hasElse ? llvm::dyn_cast<mlir::scf::YieldOp>(branch.getElseRegion().front().getTerminator())
                : mlir::scf::YieldOp{};
    if (!thenYield || thenYield.getNumOperands() != branch.getNumResults() ||
        (hasElse && (!elseYield || elseYield.getNumOperands() != branch.getNumResults()))) {
        return false;
    }
    if (invocation) {
        // The generic pass has already checked both arms and every
        // use. This optional transfer records only a proved actual
        // path, never a reusable method's effects or result ABI.
        // ponytail: unknown branches discard invocation facts; add
        // path intersections only when a source witness needs them.
        const auto selected = knownTruth(branch.getCondition());
        if (!selected) { return false; }
        auto & region = *selected ? branch.getThenRegion() : branch.getElseRegion();
        if (!region.empty() && !walk(region.front(), depth + 1)) { return false; }
        auto yield = *selected ? thenYield : elseYield;
        for (unsigned index = 0; index < branch.getNumResults(); ++index) {
            if (!owner.step()) { return false; }
            auto value = branch.getResult(index);
            auto source = yield.getOperand(index);
            if (leafValues.contains(source)) { leafValues.insert(value); }
            if (primitives.contains(source)) { primitives.insert(value); }
            if (auto leaf = exactLeaves.lookup(source)) { exactLeaves[value] = leaf; }
            auto fact = alternatives.lookup(source);
            if (const auto truth = knownTruth(source)) { fact = fact.filtered(*truth); }
            alternatives[value] = fact;
        }
        return true;
    }
    if (!chargeStates(mapStates) || !owner.spend(alternatives.size() + fields.size())) {
        return false;
    }
    const auto incoming = mapStates;
    const auto incomingFields = fields;
    const auto incomingAlternatives = alternatives;
    const bool incomingExit = frameExited;
    if (!learn(branch.getCondition(), true)) { return false; }
    if (!walk(branch.getThenRegion().front(), depth + 1)) { return false; }
    const bool thenExit = frameExited;
    auto thenStates = std::move(mapStates);
    auto thenFields = std::move(fields);
    auto thenAlternatives = std::move(alternatives);
    if (!chargeStates(incoming) ||
        !owner.spend(incomingAlternatives.size() + incomingFields.size())) {
        return false;
    }
    // Resolve then-arm cardinality while its scalar facts are live.
    alternatives = thenAlternatives;
    for (auto & item : thenStates) {
        const auto fact = sizeFacts(item.second);
        if (!fact) { return false; }
        item.second.currentSize = fact->exact;
    }
    mapStates = incoming;
    fields = incomingFields;
    alternatives = incomingAlternatives;
    frameExited = incomingExit;
    if (!learn(branch.getCondition(), false)) { return false; }
    if (hasElse && !walk(branch.getElseRegion().front(), depth + 1)) { return false; }
    if (frameExited != thenExit) { return false; }
    for (const auto & item : thenStates) {
        if (!owner.step()) { return false; }
        mapStates.try_emplace(item.first);
    }
    for (auto & item : mapStates) {
        if (!owner.step()) { return false; }
        auto & state = item.second;
        const auto & then = thenStates[item.first];
        const auto elseSize = sizeFacts(state);
        if (!elseSize) { return false; }
        state.currentSize = then.currentSize == elseSize->exact ? then.currentSize : std::nullopt;
        llvm::SmallVector<entry_fact> joined;
        for (auto left : then.entries) {
            if (!owner.step()) { return false; }
            bool matched = false;
            for (const auto & right : state.entries) {
                if (!owner.step()) { return false; }
                if (keyRelation(left.key, right.key) != PrimitiveMapKeyRelation::Same) { continue; }
                left.payload = left.payload.joined(right.payload);
                left.present &= right.present;
                left.absent &= right.absent;
                if (left.object != right.object) { left.object = {}; }
                if (left.map != right.map) { left.map = {}; }
                joined.push_back(left);
                matched = true;
                break;
            }
            if (!matched) {
                const auto missing =
                    absent(left.key, state.entries, state.completeKeys, state.possibleKeys, false);
                if (!missing) { return false; }
                if (*missing) {
                    left.present = false;
                    joined.push_back(left);
                }
            }
        }
        // A key first mentioned on the other arm may still be absent
        // on both paths when this arm cleared the entire Map.
        for (auto right : state.entries) {
            if (!owner.step()) { return false; }
            bool matched = false;
            for (const auto & left : then.entries) {
                if (!owner.step()) { return false; }
                if (keyRelation(left.key, right.key) == PrimitiveMapKeyRelation::Same) {
                    matched = true;
                    break;
                }
            }
            if (matched) { continue; }
            const auto missing =
                absent(right.key, then.entries, then.completeKeys, then.possibleKeys, false);
            if (!missing) { return false; }
            if (*missing) {
                right.present = false;
                joined.push_back(right);
            }
        }
        state.entries = std::move(joined);
        state.completeKeys &= then.completeKeys;
        if (state.completeKeys) {
            for (mlir::Value key : then.possibleKeys) {
                if (!owner.step()) { return false; }
                state.possibleKeys.push_back(key);
            }
        } else {
            state.possibleKeys.clear();
        }
        for (auto * observed : llvm::make_early_inc_range(state.observations)) {
            if (!owner.step()) { return false; }
            if (!then.observations.contains(observed)) { state.observations.erase(observed); }
        }
    }
    llvm::SmallVector<field_fact> joinedFields;
    for (auto left : thenFields) {
        if (!owner.step()) { return false; }
        for (const auto & right : fields) {
            if (!owner.step()) { return false; }
            if (left.object != right.object || left.key != right.key) { continue; }
            left.payload = left.payload.joined(right.payload);
            joinedFields.push_back(left);
            break;
        }
    }
    fields = std::move(joinedFields);
    for (unsigned index = 0; index < branch.getNumResults(); ++index) {
        if (!owner.step()) { return false; }
        const auto left = thenYield.getOperand(index), right = elseYield.getOperand(index);
        if ((!primitives.contains(left) && !leafValues.contains(left)) ||
            (!primitives.contains(right) && !leafValues.contains(right))) {
            return false;
        }
        auto value = branch.getResult(index);
        if (leafValues.contains(left) || leafValues.contains(right)) {
            leafValues.insert(value);
            continue;
        }
        primitives.insert(value);
        const auto joinedValue = thenAlternatives.lookup(left).joined(alternatives.lookup(right));
        thenAlternatives[value] = joinedValue;
        alternatives[value] = joinedValue;
        const unsigned bound = std::min(sizeBounds.lookup(left), sizeBounds.lookup(right));
        if (bound) { sizeBounds[value] = bound; }
        const auto exact = primitiveMapSize(left, keyEvidence(left));
        if (exact && exact == primitiveMapSize(right, keyEvidence(right))) {
            exactSizes[value] = *exact;
        }
    }
    // Restore enclosing SSA facts after path-local refinements;
    // selected results retain the union of their actual yields.
    for (auto & value : llvm::make_early_inc_range(alternatives)) {
        if (!owner.step()) { return false; }
        value.second = value.second.joined(thenAlternatives.lookup(value.first));
    }
    return true;
}

// A checked method call on a walked Map alias, or a snapshot copy call.
bool Walk::mapCall(ctjs::CallOp invoke) {
    if (copies && copies->calls.contains(invoke)) {
        auto iterator = invoke.getArgs().front().getDefiningOp<ctjs::CallOp>();
        if (!iterator || !calls.contains(iterator)) { return false; }
        result.snapshotOperations.push_back(invoke);
        return true;
    }
    auto read = invoke.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
    if (!read || !reads.contains(read) || read.getObject() != invoke.getReceiver()) {
        return false;
    }
    const auto key = ctjs::constantKey(read.getKey());
    const auto arity = key == "keys" ? std::optional(0u) : mapMethodArity(key);
    if (invoke.getArgs().size() != arity) { return false; }
    const auto origin = maps.lookup(invoke.getReceiver());
    if (!origin.first) { return false; }
    if ((key == "set" || key == "delete" || key == "clear") && !invalidateAliases(origin)) {
        return false;
    }
    auto & state = mapStates[origin];
    for (auto [index, argument] : llvm::enumerate(invoke.getArgs())) {
        if (!owner.step()) { return false; }
        if (primitives.contains(argument)) { continue; }
        if (llvm::is_contained(parameters.objectKeys, argument) &&
            (index == 0 || (key == "set" && index == 1))) {
            continue;
        }
        // ponytail: one nested level; deeper retention needs a checked
        // acyclic ownership graph before child Maps can retain Maps.
        if (key == "set" && index == 1 && origin == capturedOrigin && maps.contains(argument) &&
            maps.lookup(argument) != capturedOrigin) {
            mapStores.insert(invoke);
            continue;
        }
        if (key != "set" || index != 1 || !objects.contains(argument)) { return false; }
        objectStores.insert(invoke);
    }
    result.calls.push_back(invoke);
    calls.insert(invoke);
    if (key == "keys") { return true; }
    if (key == "set") {
        maps.try_emplace(invoke.getResult(), origin);
        if (!mutate(state, invoke.getArgs()[0], false, alternatives.lookup(invoke.getArgs()[1]),
                    invocation ? exactLeaves.lookup(invoke.getArgs()[1])
                               : objects.lookup(invoke.getArgs()[1]),
                    maps.lookup(invoke.getArgs()[1]))) {
            return false;
        }
    } else if (key == "clear") {
        // Saved SSA reads retain their old payload or object; only
        // the mutable Map state and live has observations change.
        if (!owner.spend(state.entries.size() + state.observations.size() +
                         state.possibleKeys.size())) {
            return false;
        }
        for (auto & entry : state.entries) {
            entry.present = false;
            entry.absent = true;
        }
        state.observations.clear();
        state.possibleKeys.clear();
        state.completeKeys = true;
        state.currentSize = 0;
        scalar<ctjs::UndefinedAttr>(invoke.getResult());
    } else {
        if (key == "has" || key == "delete") {
            scalar<ctjs::BooleanAttr>(invoke.getResult());
            if (invocation) {
                const auto missing = absent(invoke.getArgs()[0], state.entries, state.completeKeys,
                                            state.possibleKeys);
                if (!missing) { return false; }
                bool present = false;
                for (const auto & entry : state.entries) {
                    if (!owner.step()) { return false; }
                    present |= entry.present && keyRelation(entry.key, invoke.getArgs()[0]) ==
                                                    PrimitiveMapKeyRelation::Same;
                }
                if (*missing || present) {
                    alternatives[invoke.getResult()] =
                        alternatives.lookup(invoke.getResult()).filtered(present);
                }
            }
            if (key == "delete" && !mutate(state, invoke.getArgs()[0], true)) { return false; }
            if (key == "has") { state.observations.insert(invoke); }
        } else if (key == "get") {
            if (primitiveContents && origin == capturedOrigin) {
                primitives.insert(invoke.getResult());
            }
            const auto missing =
                absent(invoke.getArgs()[0], state.entries, state.completeKeys, state.possibleKeys);
            if (!missing) { return false; }
            if (*missing) {
                scalar<ctjs::UndefinedAttr>(invoke.getResult());
                return true;
            }
            for (auto & entry : state.entries) {
                if (!owner.step()) { return false; }
                if (pathRelation(entry.key, invoke.getArgs()[0]) == PrimitiveMapKeyRelation::Same) {
                    if (entry.present && entry.payload.known) {
                        scalar(invoke.getResult(), entry.payload);
                    } else if (entry.present && entry.map.first) {
                        maps.try_emplace(invoke.getResult(), entry.map);
                        result.returnedChildMaps.push_back(invoke.getResult());
                    } else if (entry.present && entry.object) {
                        if (invocation) {
                            exactLeaves[invoke.getResult()] = entry.object;
                            leafValues.insert(invoke.getResult());
                        } else {
                            objects.try_emplace(invoke.getResult(), entry.object);
                        }
                    } else if (entry.present && origin == capturedOrigin &&
                               result.childMapContents) {
                        // The actual returned owner is an SSA origin,
                        // not a constructor from an earlier call. Only
                        // independently proved family invariants can
                        // describe its unknown prior contents.
                        entry.map = {invoke.getResult(), invocation ? invocation->call : nullptr};
                        maps.try_emplace(invoke.getResult(), entry.map);
                        result.returnedChildMaps.push_back(invoke.getResult());
                    }
                    break;
                }
            }
            if (origin != capturedOrigin && result.childScalarContents.tag()) {
                // A local exact write/absence takes precedence. The family
                // category alone never establishes that this key exists.
                scalar(invoke.getResult(),
                       result.childScalarContents.joined(PrimitiveAlternatives::forTag(
                           mlir::TypeID::get<ctjs::UndefinedAttr>())));
            }
            if (origin != capturedOrigin && result.childLeafContents &&
                !primitives.contains(invoke.getResult()) && !objects.contains(invoke.getResult())) {
                leafValues.insert(invoke.getResult());
            }
            const auto childOrigin = maps.lookup(invoke.getResult());
            if (childOrigin.first == invoke.getResult()) {
                auto & child = mapStates[childOrigin];
                for (const auto & entry : result.childEntries) {
                    if (!owner.step()) { return false; }
                    child.entries.push_back({entry.key, entry.alternatives, true, {}});
                }
            }
        }
    }
    return true;
}

} // namespace ctcompile::ctnative::host_detail::captured_map_detail
