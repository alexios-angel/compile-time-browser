//===- Reads.cpp - Provider methods that read empty Maps --------*- C++ -*-===//
//
// Implements prefixAnalysis::providerRead and summarizedOwner: symbolic
// execution of a provider method call whose captured Maps are still empty,
// so has/get/size have known answers.
//
//===----------------------------------------------------------------------===//

#include "../Provider/Paths.h"

#include "mlir/Dialect/Arith/IR/Arith.h"

namespace ctcompile::ctnative::host_detail {
namespace {

/// Interprets one invocation of a provider method whose captured Maps are
/// still empty: only `size`, `has` and `get` are modeled. It has no hooks to
/// rewrite IR or publish heap facts, because it follows one call and never
/// specializes the method's shared body. A failed path commits nothing, not
/// even the reads it made before failing.
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
        if (auto scalar = prefixScalar(operation, values, context)) { return *scalar; }
        if (auto load = llvm::dyn_cast<ctjs::LoadUpvalueOp>(operation)) {
            const auto * capture = prefix.capture(factory, closure, function, load);
            const auto resource = capture ? llvm::find(factory.resources, capture->resource)
                                          : factory.resources.end();
            if (resource == factory.resources.end()) { return {}; }
            return prefixValue::of(prefixValue::Kind::resource,
                                   static_cast<unsigned>(resource - factory.resources.begin()));
        }
        if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
            const auto owner = values.lookup(read.getObject());
            const auto key = ctjs::constantKey(read.getKey());
            if (owner.kind != prefixValue::Kind::resource ||
                owner.object >= factory.resources.size()) {
                return {};
            }
            // The closed-source-v1 host contract guarantees unmodified
            // intrinsic prototypes. A fresh, unescaped Map has no own
            // overrides, and this path has mutated no property or provider.
            if (key == "size") {
                proof.reads.push_back({read, factory.resources[owner.object], key.str()});
                return prefixValue::constant(ctjs::numberAttr(context, 0.0));
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
            // Map keys are not coerced. Once the argument is evaluated,
            // has/get on this empty Map cannot run script or mutate another
            // retained Map, whatever the key is.
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
        // Any other operation (writes, allocations, object reads, arbitrary
        // calls, globals, exceptions, resource escape) is unsupported and ends
        // the path.
        return {};
    }
};

} // namespace

/// The summarized factory whose invocation produced `closure`, when `call` is
/// in the entry function, `function` has one block, `Map` is the initial
/// intrinsic and `closure` is one of the factory's capturing closures. Null
/// otherwise. `charged` charges the capture scan to the step budget.
const HostPrefixFactory * prefixAnalysis::summarizedOwner(ctjs::CallOp call, ctjs::FuncOp function,
                                                          prefixValue closure, bool charged) {
    if (closure.invocation == 0 || closure.invocation > factories.size() ||
        call->getParentOfType<ctjs::FuncOp>().getSymName() != contract.entry ||
        !llvm::hasSingleElement(function.getBody()) ||
        !llvm::is_contained(contract.initialIntrinsics, "Map")) {
        return nullptr;
    }
    const auto & owner = factories[closure.invocation - 1];
    if ((charged && !spend(owner.captures.size())) ||
        llvm::none_of(owner.captures,
                      [&](const auto & capture) { return capture.closure == closure.made; })) {
        return nullptr;
    }
    return &owner;
}

/// Symbolically runs a provider method that only reads its factory's still
/// empty Maps. Records a read summary and returns the primitive result, or
/// unknown on refusal.
prefixValue prefixAnalysis::providerRead(ctjs::CallOp call, ctjs::FuncOp function,
                                         prefixValue closure, environment & arguments) {
    if (!followProviderReads || !followPublication) { return {}; }
    const auto * summarized = summarizedOwner(call, function, closure, false);
    if (!summarized) { return {}; }
    const auto & owner = *summarized;
    // The factory summary already proved the target and cells immutable and
    // the Maps retained. No unsupported operation ran since the factory
    // returned, and accepted summaries never mutate or leak a Map, so each
    // Map is still empty.
    emptyMapReader reader{
        *this, owner, closure, function, {call, function, owner.operation, {}, {}}};
    environment values;
    // Skip the three implicit entry arguments (receiver, new.target, callee).
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
