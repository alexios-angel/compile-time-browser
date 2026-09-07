#include "ProviderDiagnostics.h"

#include "ProviderCallbacks.h"

#include <cmath>
#include <limits>

namespace ctcompile::ctnative::host_detail {
namespace {

bool chargeText(prefixAnalysis & prefix, llvm::StringRef text) {
    return text.size() <= std::numeric_limits<unsigned>::max() &&
           prefix.spend(static_cast<unsigned>(text.size()));
}

} // namespace

bool providerDiagnosticState::initialize() {
    // A copy includes object and closure identities for ordinary source
    // lookups. Only the separate callback proof may modify primitive slots.
    for (const auto & entry : prefix.globals) {
        if (!prefix.step() || !chargeText(prefix, entry.first())) { return false; }
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
                                0});
    return true;
}

prefixValue providerDiagnosticState::global(ctjs::LoadGlobalOp load) {
    if (!chargeText(prefix, load.getName())) { return {}; }
    if (load.getName() == "Array" &&
        llvm::is_contained(prefix.contract.initialIntrinsics, "Array")) {
        // initialBindingProblem checks all live intrinsic uses and forbids
        // aliases, replacement, descriptor changes and detached from calls.
        return {prefixValue::Kind::arrayConstructor, {}, {}, 0};
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
    if (text && !chargeText(prefix, text.getValue())) { return {}; }
    const auto & heap = objects ? *objects : prefix.objects;
    if (owner.kind == prefixValue::Kind::object && owner.object < heap.size() && text &&
        ordinaryKey(text.getValue())) {
        // These identities were initialized by the source prefix. A missing
        // slot could consult a prototype and therefore cannot be summarized.
        const auto & fields = heap[owner.object].fields;
        const auto found = fields.find(text.getValue());
        return found == fields.end() ? prefixValue{} : found->second;
    }
    if (owner.kind == prefixValue::Kind::arrayConstructor && text && text.getValue() == "from") {
        return {prefixValue::Kind::arrayFrom, {}, {}, 0};
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
        if (!read || !load || load.getName() != "Array" || keyOf(read.getKey()) != "from" ||
            call.getReceiver() != load.getResult() ||
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
        const prefixValue result{prefixValue::Kind::mapKeysIterator, {}, {}, id};
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
        snapshot copied{iterator.map, {}};
        const auto charge = [&](unsigned count) { return prefix.spend(count); };
        if (!state.keys(iterator.map, copied.keys, charge)) { return {}; }
        const auto id = static_cast<unsigned>(snapshots.size());
        snapshots.push_back(std::move(copied));
        iterator.consumed = true;
        const prefixValue result{prefixValue::Kind::keySnapshot, {}, {}, id};
        return record(invoked, iterator.map, "Array.from", {}) ? result : prefixValue{};
    }
    if (callee.kind != prefixValue::Kind::closure || !prefix.followProviderCallbacks) { return {}; }
    llvm::SmallVector<prefixValue> arguments;
    for (mlir::Value argument : invoked.getArgs()) {
        if (!prefix.step()) { return {}; }
        arguments.push_back(values.lookup(argument));
    }
    const auto result =
        providerCallback(prefix, invoked, callee, receiver, arguments, globals, proof);
    // Even a proved scalar-only callback cannot intervene between this first
    // implementation's private iterator creation and immediate consumption.
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
    if (!a || !b || !chargeText(prefix, a.getValue()) || !chargeText(prefix, b.getValue())) {
        return {};
    }
    return prefixValue::constant(
        ctjs::StringAttr::get(prefix.module.getContext(), (a.getValue() + b.getValue()).str()));
}

} // namespace ctcompile::ctnative::host_detail
