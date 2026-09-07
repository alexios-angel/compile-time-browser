#include "ProviderPaths.h"

#include "ProviderDiagnostics.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "llvm/ADT/bit.h"

namespace ctcompile::ctnative::host_detail {
namespace {

struct mapPathReader {
    prefixAnalysis & prefix;
    const HostPrefixFactory & factory;
    prefixValue closure;
    ctjs::FuncOp function;
    providerState state;
    HostPrefixProviderSummary proof;
    providerDiagnosticState diagnostics{prefix, state, proof};
    using environment = prefixAnalysis::environment;

    providerValue known(prefixValue value) const {
        if (value.kind == prefixValue::Kind::primitive) {
            auto literal = value.literal;
            if (auto integer = llvm::dyn_cast_or_null<mlir::IntegerAttr>(literal)) {
                if (!integer.getValue().isZero() && !integer.getValue().isOne()) { return {}; }
                literal =
                    ctjs::BooleanAttr::get(prefix.module.getContext(), integer.getValue().isOne());
            }
            return providerValue::constant(literal);
        }
        if (value.kind == prefixValue::Kind::object && value.object < prefix.objects.size()) {
            return providerValue::object(value.object);
        }
        if (value.kind == prefixValue::Kind::resource) { return providerValue::map(value.object); }
        return {};
    }

    prefixValue carried(providerValue value) const {
        if (value.kind == providerValue::Kind::primitive && value.literal) {
            return prefixValue::constant(value.literal);
        }
        if (value.kind == providerValue::Kind::map && state.get(value.id)) {
            return {prefixValue::Kind::resource, {}, {}, value.id};
        }
        return {};
    }

    bool record(mlir::Operation * operation, unsigned id, llvm::StringRef member,
                prefixValue result) {
        const auto * map = state.get(id);
        if (!map || !prefix.step()) { return false; }
        proof.operations.push_back(
            {operation,
             {map->provenance.allocation, map->provenance.invocation, id},
             member.str(),
             result.literal,
             result.kind == prefixValue::Kind::resource ? result.object : 0});
        return true;
    }

    prefixValue operation(mlir::Operation * operation, environment & values) {
        auto * context = prefix.module.getContext();
        const auto charge = [&](unsigned count) { return prefix.spend(count); };
        if (auto constant = llvm::dyn_cast<ctjs::ConstantOp>(operation)) {
            return prefixValue::constant(constant.getValue());
        }
        if (auto constant = llvm::dyn_cast<mlir::arith::ConstantOp>(operation)) {
            return prefixValue::constant(constant.getValue());
        }
        if (auto load = llvm::dyn_cast<ctjs::LoadUpvalueOp>(operation)) {
            if (load.getClosure() != function.getBody().front().getArgument(2) ||
                load.getIndex() < 0) {
                return {};
            }
            for (auto capture : factory.captures) {
                if (!prefix.step()) { return {}; }
                if (capture.closure != closure.made ||
                    capture.index != static_cast<unsigned>(load.getIndex())) {
                    continue;
                }
                for (auto [index, resource] : llvm::enumerate(factory.resources)) {
                    if (!prefix.step()) { return {}; }
                    if (resource == capture.resource) {
                        return {prefixValue::Kind::resource,
                                {},
                                {},
                                prefix.providerRoots[closure.invocation - 1][index]};
                    }
                }
            }
            return {};
        }
        if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation)) {
            if (load.getName() == "Map" &&
                llvm::is_contained(prefix.contract.initialIntrinsics, "Map")) {
                return {prefixValue::Kind::mapConstructor, {}, {}, 0};
            }
            return diagnostics.operation(operation, values);
        }
        if (auto construct = llvm::dyn_cast<ctjs::ConstructOp>(operation)) {
            auto load = construct.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
            if (!load || load.getName() != "Map" ||
                values.lookup(construct.getCallee()).kind != prefixValue::Kind::mapConstructor ||
                construct.getNewTarget() != construct.getCallee() || !construct.getArgs().empty()) {
                return {};
            }
            unsigned id = 0;
            if (!state.create({construct, proof.operation, closure.invocation}, id, charge)) {
                return {};
            }
            proof.allocations.push_back({construct, proof.operation, id});
            diagnostics.mutated();
            return {prefixValue::Kind::resource, {}, {}, id};
        }
        if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
            const auto owner = values.lookup(read.getObject());
            const auto key = keyOf(read.getKey());
            if (owner.kind != prefixValue::Kind::resource || !state.get(owner.object)) {
                return diagnostics.operation(operation, values);
            }
            if (key == "size") {
                unsigned size = 0;
                if (!state.size(owner.object, size, charge)) { return {}; }
                auto result = prefixValue::constant(ctjs::NumberAttr::get(
                    context, llvm::bit_cast<uint64_t>(static_cast<double>(size))));
                return record(read, owner.object, key, result) ? result : prefixValue{};
            }
            if (key == "has" || key == "get" || key == "set" || key == "delete") {
                return {prefixValue::Kind::resourceMethod,
                        ctjs::StringAttr::get(context, key),
                        {},
                        owner.object};
            }
            return diagnostics.operation(operation, values);
        }
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(operation)) {
            const auto method = values.lookup(call.getCallee());
            const auto receiver = values.lookup(call.getReceiver());
            if (method.kind != prefixValue::Kind::resourceMethod ||
                receiver.kind != prefixValue::Kind::resource || method.object != receiver.object ||
                !state.get(receiver.object) || prefix.pendingNewTarget.contains(function)) {
                return diagnostics.operation(operation, values);
            }
            const auto member = llvm::cast<ctjs::StringAttr>(method.literal).getValue();
            if (member != "has" && member != "get" && member != "set" && member != "delete") {
                return diagnostics.operation(operation, values);
            }
            if (call.getArgs().size() != (member == "set" ? 2u : 1u)) { return {}; }
            const auto key = known(values.lookup(call.getArgs()[0]));
            prefixValue result;
            if (member == "set") {
                if (!state.set(receiver.object, key, known(values.lookup(call.getArgs()[1])),
                               charge)) {
                    return {};
                }
                result = receiver;
                diagnostics.mutated();
            } else if (member == "delete") {
                bool removed = false;
                if (!state.erase(receiver.object, key, removed, charge)) { return {}; }
                result = prefixValue::constant(ctjs::BooleanAttr::get(context, removed));
                diagnostics.mutated();
            } else {
                bool found = false;
                providerValue value;
                if (!state.lookup(receiver.object, key, found, value, charge)) { return {}; }
                result = member == "has"
                             ? prefixValue::constant(ctjs::BooleanAttr::get(context, found))
                         : found ? carried(value)
                                 : prefixValue::constant(ctjs::UndefinedAttr::get(context));
            }
            return record(call, receiver.object, member, result) ? result : prefixValue{};
        }
        if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation)) {
            const auto operand = values.lookup(unary.getOperand());
            if (operand.kind == prefixValue::Kind::resourceMethod) { return {}; }
            return prefixUnary(unary.getKind(), operand, context);
        }
        if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation)) {
            const auto left = values.lookup(compare.getLhs()),
                       right = values.lookup(compare.getRhs());
            if (left.kind != prefixValue::Kind::primitive ||
                right.kind != prefixValue::Kind::primitive) {
                return {};
            }
            return prefixCompare(compare.getKind(), left, right, context);
        }
        if (auto truth = llvm::dyn_cast<ctjs::TruthyOp>(operation)) {
            auto bit = prefixTruth(values.lookup(truth.getValue()));
            return bit ? prefixValue::constant(ctjs::BooleanAttr::get(context, *bit))
                       : prefixValue{};
        }
        if (llvm::isa<mlir::arith::TruncIOp, mlir::arith::ExtUIOp>(operation)) {
            auto bit = prefixTruth(values.lookup(operation->getOperand(0)));
            return bit ? prefixValue::constant(ctjs::BooleanAttr::get(context, *bit))
                       : prefixValue{};
        }
        return diagnostics.operation(operation, values);
    }

    bool retained() {
        llvm::SmallVector<unsigned> pending;
        llvm::DenseSet<unsigned> seen;
        for (const auto & roots : prefix.providerRoots) {
            for (unsigned root : roots) {
                if (!prefix.step()) { return false; }
                pending.push_back(root);
            }
        }
        while (!pending.empty()) {
            if (!prefix.step()) { return false; }
            unsigned id = pending.pop_back_val();
            if (!seen.insert(id).second) { continue; }
            const auto * map = state.get(id);
            if (!map) { return false; }
            for (const auto & entry : map->entries) {
                if (!prefix.step()) { return false; }
                if (entry.value.kind == providerValue::Kind::map) {
                    pending.push_back(entry.value.id);
                }
            }
        }
        for (auto allocation : proof.allocations) {
            if (!prefix.step() || !seen.contains(allocation.mapId)) { return false; }
        }
        return true;
    }
};

} // namespace

prefixValue prefixAnalysis::providerMutation(ctjs::CallOp call, ctjs::FuncOp function,
                                             prefixValue closure, environment & arguments) {
    if (!followProviderMutations || closure.invocation == 0 ||
        closure.invocation > factories.size() || closure.invocation > providerRoots.size() ||
        call->getParentOfType<ctjs::FuncOp>().getSymName() != contract.entry ||
        !llvm::hasSingleElement(function.getBody()) ||
        !llvm::is_contained(contract.initialIntrinsics, "Map")) {
        return {};
    }
    const auto & owner = factories[closure.invocation - 1];
    bool captured = false;
    for (const auto & capture : owner.captures) {
        if (!step()) { return {}; }
        captured |= capture.closure == closure.made;
    }
    if (!captured) { return {}; }
    mapPathReader reader{*this,    owner, closure,
                         function, {},    {call, function, owner.operation, {}, {}, {}, {}}};
    const auto charge = [&](unsigned count) { return spend(count); };
    if (!providers.cloneTo(reader.state, charge)) { return {}; }
    if (followProviderDiagnostics && !reader.diagnostics.initialize()) { return {}; }
    environment values;
    for (auto [argument, value] :
         llvm::zip(function.getBody().front().getArguments().drop_front(3), call.getArgs())) {
        if (!step()) { return {}; }
        values[argument] = arguments.lookup(value);
    }
    mlir::Operation * stopped = nullptr;
    auto returned = providerRegion(reader, function.getBody(), values, &stopped);
    if (returned.kind != completion::Kind::returned || returned.values.size() != 1 ||
        returned.values.front().kind != prefixValue::Kind::primitive ||
        reader.proof.operations.empty() || !reader.retained()) {
        if (stopped) {
            providerBoundary =
                ("unsupported provider path at `" + stopped->getName().getStringRef() + "`").str();
            if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(stopped)) {
                providerBoundary += " (" + load.getName().str() + ")";
            }
        }
        return {};
    }
    reader.proof.result = returned.values.front().literal;
    providers = std::move(reader.state);
    if (followProviderDiagnostics) { globals = std::move(reader.diagnostics.globals); }
    providerCalls.push_back(std::move(reader.proof));
    discoveryOnly.insert(call->getParentOfType<ctjs::FuncOp>());
    return returned.values.front();
}

} // namespace ctcompile::ctnative::host_detail
