//===- Diagnostics.cpp - Diagnostic operations in provider methods --------===//
//
// Implements providerDiagnosticState: global reads, own-field reads,
// `Array.from(map.keys())` key snapshots, string concatenation and scalar
// callbacks inside a provider method.
//
//===----------------------------------------------------------------------===//

#include "Diagnostics.h"

#include "Callbacks.h"

#include <ctbrowser/core/algorithms.hpp>

#include <cmath>
#include <limits>

namespace ctcompile::ctnative::host_detail {

bool providerDiagnosticState::initialize() {
    // The copy keeps object and closure identities for ordinary lookups.
    // Only providerCallback may change the primitive values in it.
    for (const auto & entry : prefix.globals) {
        if (!prefix.step() || !prefix.spendText(entry.first())) { return false; }
        globals[entry.first()] = entry.second;
    }
    return true;
}

bool providerDiagnosticState::record(mlir::Operation * operation, unsigned id,
                                     llvm::StringRef member, prefixValue result) {
    const auto * map = state.get(id);
    if (!map || !prefix.step()) { return false; }
    proof.operations.push_back({operation,
                                {map->provenance.allocation, map->provenance.invocation, id},
                                member.str(),
                                result.literal,
                                result.kind == prefixValue::Kind::resource ? result.object : 0,
                                result.kind == prefixValue::Kind::object ? result.object + 1 : 0});
    return true;
}

prefixValue providerDiagnosticState::global(ctjs::LoadGlobalOp load) {
    if (!prefix.spendText(load.getName())) { return {}; }
    if (load.getName() == "Array" &&
        llvm::is_contained(prefix.contract.initialIntrinsics, "Array")) {
        // initialBindingProblem has already checked every live use of the
        // Array intrinsic and rejected aliases, replacement, descriptor
        // changes and calls to a detached `Array.from`.
        return prefixValue::of(prefixValue::Kind::arrayConstructor, 0);
    }
    const auto found = globals.find(load.getName());
    if (found == globals.end() || found->second.kind == prefixValue::Kind::absent) { return {}; }
    return found->second;
}

prefixValue providerDiagnosticState::property(ctjs::GetPropertyOp read,
                                              prefixAnalysis::environment & values) {
    const auto owner = values.lookup(read.getObject());
    const auto key = values.lookup(read.getKey());
    if (key.kind != prefixValue::Kind::primitive) { return {}; }
    auto text = llvm::dyn_cast_if_present<ctjs::StringAttr>(key.literal);
    if (text && !prefix.spendText(text.getValue())) { return {}; }
    const auto & heap = objects ? *objects : prefix.objects;
    if (owner.kind == prefixValue::Kind::object && owner.object < heap.size() && text &&
        ctjs::ordinaryKey(text.getValue())) {
        // The source prefix created these objects. A missing field could be
        // looked up on a prototype, so it cannot be summarized.
        const auto & fields = heap[owner.object].fields;
        const auto found = fields.find(text.getValue());
        return found == fields.end() ? prefixValue{} : found->second;
    }
    if (owner.kind == prefixValue::Kind::arrayConstructor && text && text.getValue() == "from") {
        return prefixValue::of(prefixValue::Kind::arrayFrom, 0);
    }
    if (owner.kind == prefixValue::Kind::resource && state.get(owner.object) && text &&
        text.getValue() == "keys") {
        return {prefixValue::Kind::resourceMethod, text, {}, owner.object};
    }
    if (owner.kind != prefixValue::Kind::keySnapshot || owner.object >= snapshots.size()) {
        return {};
    }
    const auto index = llvm::dyn_cast_if_present<ctjs::NumberAttr>(key.literal);
    if (!index) { return {}; }
    const double number = index.getDouble();
    const auto & copied = snapshots[owner.object];
    if (!std::isfinite(number) || number < 0 || std::floor(number) != number ||
        number >= static_cast<double>(copied.keys.size()) || !prefix.step()) {
        return {};
    }
    const auto entry = copied.keys[static_cast<size_t>(number)];
    if (entry.kind != providerValue::Kind::primitive ||
        !llvm::isa_and_nonnull<ctjs::StringAttr>(entry.literal)) {
        return {};
    }
    const auto result = prefixValue::constant(entry.literal);
    return record(read, copied.map, "snapshot[index]", result) ? result : prefixValue{};
}

ctjs::CallOp providerDiagnosticState::consumer(ctjs::CallOp keys) {
    ctjs::CallOp result;
    for (mlir::OpOperand & use : keys.getResult().getUses()) {
        if (!prefix.step()) { return {}; }
        if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
        auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
        if (!call || result || call->getBlock() != keys->getBlock() || call.getArgs().size() != 1 ||
            use.getOperandNumber() != 2) {
            return {};
        }
        auto read = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
        auto load =
            read ? read.getObject().getDefiningOp<ctjs::LoadGlobalOp>() : ctjs::LoadGlobalOp{};
        if (!read || !load || load.getName() != "Array" ||
            ctjs::constantKey(read.getKey()) != "from" || call.getReceiver() != load.getResult() ||
            !llvm::is_contained(prefix.contract.initialIntrinsics, "Array")) {
            return {};
        }
        result = call;
    }
    return result;
}

prefixValue providerDiagnosticState::call(ctjs::CallOp invoked,
                                          prefixAnalysis::environment & values) {
    const auto callee = values.lookup(invoked.getCallee());
    const auto receiver = values.lookup(invoked.getReceiver());
    if (prefix.pendingNewTarget.contains(invoked->getParentOfType<ctjs::FuncOp>())) { return {}; }
    if (callee.kind == prefixValue::Kind::resourceMethod &&
        receiver.kind == prefixValue::Kind::resource && callee.object == receiver.object &&
        state.get(receiver.object) && invoked.getArgs().empty()) {
        auto member = llvm::dyn_cast_if_present<ctjs::StringAttr>(callee.literal);
        auto consumes = member && member.getValue() == "keys" ? consumer(invoked) : ctjs::CallOp{};
        if (!consumes || !prefix.step() ||
            iterators.size() >= std::numeric_limits<unsigned>::max()) {
            return {};
        }
        const auto id = static_cast<unsigned>(iterators.size());
        iterators.push_back({receiver.object, mutation, consumes, false});
        const auto result = prefixValue::of(prefixValue::Kind::mapKeysIterator, id);
        return record(invoked, receiver.object, "keys", {}) ? result : prefixValue{};
    }
    if (callee.kind == prefixValue::Kind::arrayFrom &&
        receiver.kind == prefixValue::Kind::arrayConstructor && invoked.getArgs().size() == 1) {
        const auto input = values.lookup(invoked.getArgs().front());
        if (input.kind != prefixValue::Kind::mapKeysIterator || input.object >= iterators.size()) {
            return {};
        }
        auto & iterator = iterators[input.object];
        if (iterator.consumer != invoked || iterator.consumed || iterator.mutation != mutation ||
            snapshots.size() >= std::numeric_limits<unsigned>::max() || !prefix.step()) {
            return {};
        }
        const auto charge = [&](unsigned count) { return prefix.spend(count); };
        auto keys = state.keys(iterator.map, charge);
        if (!keys) { return {}; }
        snapshot copied{iterator.map, std::move(*keys)};
        const auto id = static_cast<unsigned>(snapshots.size());
        snapshots.push_back(std::move(copied));
        iterator.consumed = true;
        const auto result = prefixValue::of(prefixValue::Kind::keySnapshot, id);
        return record(invoked, iterator.map, "Array.from", {}) ? result : prefixValue{};
    }
    if (callee.kind != prefixValue::Kind::closure || !prefix.followProviderCallbacks) { return {}; }
    llvm::SmallVector<prefixValue> arguments;
    for (mlir::Value argument : invoked.getArgs()) {
        if (!prefix.step()) { return {}; }
        arguments.push_back(values.lookup(argument));
    }
    const auto result = providerCallback(prefix, invoked, callee, arguments, globals, proof);
    // Any callback that ran counts as a mutation, even one proved to touch
    // only scalars: a keys() iterator is admitted only when nothing runs
    // between its creation and its Array.from consumption.
    if (result.kind != prefixValue::Kind::unknown) { mutated(); }
    return result;
}

prefixValue providerDiagnosticState::operation(mlir::Operation * operation,
                                               prefixAnalysis::environment & values) {
    if (!prefix.followProviderDiagnostics) { return {}; }
    if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation)) { return global(load); }
    if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
        return property(read, values);
    }
    if (auto invoked = llvm::dyn_cast<ctjs::CallOp>(operation)) { return call(invoked, values); }
    auto binary = llvm::dyn_cast<ctjs::BinaryOp>(operation);
    if (!binary || (binary.getKind() != ctjs::BinaryKind::Add &&
                    binary.getKind() != ctjs::BinaryKind::Concat)) {
        return {};
    }
    const auto left = values.lookup(binary.getLhs()), right = values.lookup(binary.getRhs());
    if (left.kind != prefixValue::Kind::primitive || right.kind != prefixValue::Kind::primitive) {
        return {};
    }
    const auto a = llvm::dyn_cast_if_present<ctjs::StringAttr>(left.literal);
    const auto b = llvm::dyn_cast_if_present<ctjs::StringAttr>(right.literal);
    if (!a || !b || !prefix.spendText(a.getValue()) || !prefix.spendText(b.getValue())) {
        return {};
    }
    std::string joined = (a.getValue() + b.getValue()).str();
    ctbrowser::join_surrogates(joined);
    return prefixValue::constant(ctjs::StringAttr::get(prefix.module.getContext(), joined));
}

} // namespace ctcompile::ctnative::host_detail
