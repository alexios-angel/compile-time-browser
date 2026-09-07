#include "ProviderPaths.h"

#include "mlir/Dialect/Arith/IR/Arith.h"

namespace ctcompile::ctnative::host_detail {
namespace {

// A separate path interpreter deliberately has no rewrite/heap-publication
// hooks. It may follow one actual invocation, never specialize its reusable
// body. A failed path commits no facts, including any reads before a write.
struct emptyMapReader {
    prefixAnalysis & prefix;
    const HostPrefixFactory & factory;
    prefixValue closure;
    ctjs::FuncOp function;
    HostPrefixReadSummary proof;
    using environment = prefixAnalysis::environment;
    using completion = prefixAnalysis::completion;

    prefixValue operation(mlir::Operation * operation, environment & values) {
        auto * context = prefix.module.getContext();
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
                const auto resource = llvm::find(factory.resources, capture.resource);
                if (resource == factory.resources.end()) { return {}; }
                return {prefixValue::Kind::resource,
                        {},
                        {},
                        static_cast<unsigned>(resource - factory.resources.begin())};
            }
            return {};
        }
        if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
            const auto owner = values.lookup(read.getObject());
            const auto key = keyOf(read.getKey());
            if (owner.kind != prefixValue::Kind::resource ||
                owner.object >= factory.resources.size()) {
                return {};
            }
            // closed-source-v1 supplies unmodified intrinsic prototypes.
            // Fresh, unescaped Map receivers have no own overrides; the
            // selected path has performed no property/provider mutation.
            if (key == "size") {
                proof.reads.push_back({read, factory.resources[owner.object], key.str()});
                return prefixValue::constant(ctjs::NumberAttr::get(context, 0.0));
            }
            if (key == "has" || key == "get") {
                return {prefixValue::Kind::resourceMethod,
                        ctjs::StringAttr::get(context, key),
                        {},
                        owner.object};
            }
            return {};
        }
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(operation)) {
            const auto method = values.lookup(call.getCallee());
            const auto receiver = values.lookup(call.getReceiver());
            if (method.kind != prefixValue::Kind::resourceMethod ||
                receiver.kind != prefixValue::Kind::resource || receiver.object != method.object ||
                receiver.object >= factory.resources.size() || call.getArgs().size() != 1 ||
                prefix.pendingNewTarget.contains(function)) {
                return {};
            }
            const auto key = llvm::cast<ctjs::StringAttr>(method.literal).getValue();
            // Map keys are not coerced. Once source argument evaluation has
            // completed, has/get on this empty Map cannot call JS or mutate
            // another retained resource, even for an unknown key value.
            proof.reads.push_back({call, factory.resources[receiver.object], key.str()});
            return prefixValue::constant(
                key == "has" ? mlir::Attribute(ctjs::BooleanAttr::get(context, false))
                             : mlir::Attribute(ctjs::UndefinedAttr::get(context)));
        }
        if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation)) {
            return prefixUnary(unary.getKind(), values.lookup(unary.getOperand()), context);
        }
        if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation)) {
            return prefixCompare(compare.getKind(), values.lookup(compare.getLhs()),
                                 values.lookup(compare.getRhs()), context);
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
        // No writes, allocations, object reads, arbitrary calls, globals,
        // exceptions or resource escape are supported by this provider slice.
        return {};
    }
};

} // namespace

prefixValue prefixAnalysis::providerRead(ctjs::CallOp call, ctjs::FuncOp function,
                                         prefixValue closure, environment & arguments) {
    if (!followProviderReads || !followPublication || closure.invocation == 0 ||
        closure.invocation > factories.size() ||
        call->getParentOfType<ctjs::FuncOp>().getSymName() != contract.entry ||
        !llvm::hasSingleElement(function.getBody()) ||
        !llvm::is_contained(contract.initialIntrinsics, "Map")) {
        return {};
    }
    const auto & owner = factories[closure.invocation - 1];
    if (!llvm::any_of(owner.captures,
                      [&](auto capture) { return capture.closure == closure.made; })) {
        return {};
    }
    // The factory independently checked target/cell immutability and resource
    // retention. No unsupported operation has been crossed since its return;
    // accepted summaries never mutate or leak a Map, so it is still empty.
    emptyMapReader reader{
        *this, owner, closure, function, {call, function, owner.operation, {}, {}}};
    environment values;
    for (auto [argument, value] :
         llvm::zip(function.getBody().front().getArguments().drop_front(3), call.getArgs())) {
        values[argument] = arguments.lookup(value);
    }
    auto returned = providerRegion(reader, function.getBody(), values);
    if (returned.kind != completion::Kind::returned || returned.values.size() != 1 ||
        returned.values.front().kind != prefixValue::Kind::primitive ||
        reader.proof.reads.empty()) {
        return {};
    }
    reader.proof.result = returned.values.front().literal;
    reads.push_back(std::move(reader.proof));
    discoveryOnly.insert(call->getParentOfType<ctjs::FuncOp>());
    return returned.values.front();
}

} // namespace ctcompile::ctnative::host_detail
