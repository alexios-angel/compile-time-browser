#include "Callbacks.h"
#include "Paths.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "llvm/ADT/APFloat.h"

namespace ctcompile::ctnative::host_detail {
namespace {

struct callbackReader {
    prefixAnalysis & prefix;
    llvm::StringMap<prefixValue> & globals;
    HostPrefixProviderCallback proof;
    using environment = prefixAnalysis::environment;

    prefixValue primitive(prefixValue value) {
        if (value.kind != prefixValue::Kind::primitive || !value.literal) { return {}; }
        value.literal = booleanFromBit(value.literal);
        if (!value.literal) { return {}; }
        if (auto string = llvm::dyn_cast<ctjs::StringAttr>(value.literal)) {
            if (string.getValue().size() > 65536 ||
                !prefix.spend(static_cast<unsigned>(string.getValue().size()))) {
                return {};
            }
        }
        return ctjs::isPrimitiveAttr(value.literal) ? value : prefixValue{};
    }

    bool writable(llvm::StringRef name) {
        if (!prefix.spendText(name) || !ctjs::ordinaryKey(name) || reservedCallbackBinding(name) ||
            !unreservedBinding(
                name, prefix.contract,
                {&prefix.contract.initialIntrinsics, &prefix.contract.realmOwnDataProperties},
                [&](llvm::StringRef reserved) {
                    return prefix.step() && prefix.spendText(reserved);
                })) {
            return false;
        }
        const auto found = globals.find(name);
        return found != globals.end() &&
               primitive(found->second).kind == prefixValue::Kind::primitive;
    }

    prefixValue operation(mlir::Operation * operation, environment & values) {
        auto * context = prefix.module.getContext();
        if (auto constant = llvm::dyn_cast<ctjs::ConstantOp>(operation)) {
            return primitive(prefixValue::constant(constant.getValue()));
        }
        if (auto constant = llvm::dyn_cast<mlir::arith::ConstantOp>(operation)) {
            return primitive(prefixValue::constant(constant.getValue()));
        }
        if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation)) {
            if (!prefix.spendText(load.getName())) { return {}; }
            const auto found = globals.find(load.getName());
            return found == globals.end() ? prefixValue{} : primitive(found->second);
        }
        if (auto binary = llvm::dyn_cast<ctjs::BinaryOp>(operation)) {
            auto lhs = primitive(values.lookup(binary.getLhs()));
            auto rhs = primitive(values.lookup(binary.getRhs()));
            auto left = llvm::dyn_cast_or_null<ctjs::NumberAttr>(lhs.literal);
            auto right = llvm::dyn_cast_or_null<ctjs::NumberAttr>(rhs.literal);
            if (!left || !right) { return {}; }
            llvm::APFloat result(left.getDouble()), operand(right.getDouble());
            switch (binary.getKind()) {
            case ctjs::BinaryKind::Add:
                (void)result.add(operand, llvm::APFloat::rmNearestTiesToEven);
                break;
            case ctjs::BinaryKind::Sub:
                (void)result.subtract(operand, llvm::APFloat::rmNearestTiesToEven);
                break;
            case ctjs::BinaryKind::Mul:
                (void)result.multiply(operand, llvm::APFloat::rmNearestTiesToEven);
                break;
            case ctjs::BinaryKind::Div:
                (void)result.divide(operand, llvm::APFloat::rmNearestTiesToEven);
                break;
            default: return {};
            }
            return prefixValue::constant(
                ctjs::NumberAttr::get(context, result.bitcastToAPInt().getZExtValue()));
        }
        if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation)) {
            auto left = primitive(values.lookup(compare.getLhs()));
            auto right = primitive(values.lookup(compare.getRhs()));
            if (left.kind != prefixValue::Kind::primitive ||
                right.kind != prefixValue::Kind::primitive) {
                return {};
            }
            return prefixCompare(compare.getKind(), left, right, context);
        }
        if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation)) {
            auto operand = primitive(values.lookup(unary.getOperand()));
            if (operand.kind != prefixValue::Kind::primitive) { return {}; }
            return prefixUnary(unary.getKind(), operand, context);
        }
        if (auto truth = llvm::dyn_cast<ctjs::TruthyOp>(operation)) {
            auto value = primitive(values.lookup(truth.getValue()));
            auto bit = prefixTruth(value);
            return bit ? prefixValue::constant(ctjs::BooleanAttr::get(context, *bit))
                       : prefixValue{};
        }
        if (llvm::isa<ctjs::FromBoolOp, mlir::arith::TruncIOp, mlir::arith::ExtUIOp>(operation)) {
            return primitive(values.lookup(operation->getOperand(0)));
        }
        if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(operation)) {
            auto value = primitive(values.lookup(store.getValue()));
            if (value.kind != prefixValue::Kind::primitive || !writable(store.getName()) ||
                !prefix.step()) {
                return {};
            }
            globals[store.getName()] = value;
            proof.writes.push_back({store, store.getName().str(), value.literal});
            return value;
        }
        // No calls, properties, allocation, upvalues, raw arguments, exception
        // paths or identity inspection can cross this callback proof.
        return {};
    }
};

} // namespace

prefixValue providerCallback(prefixAnalysis & prefix, ctjs::CallOp sourceCall, prefixValue callee,
                             prefixValue receiver, llvm::ArrayRef<prefixValue> arguments,
                             llvm::StringMap<prefixValue> & globals,
                             HostPrefixProviderSummary & proof) {
    // The raw receiver remains a runtime operand. Requiring unused implicit
    // arguments avoids assuming sloppy/strict/lexical effective-this behavior.
    (void)receiver;
    auto function = prefix.target(callee);
    if (!prefix.followProviderCallbacks || !prefix.step() || !function ||
        !llvm::hasSingleElement(function.getBody()) || function.getUpvalueCount() != 0 ||
        !callee.made.getUpvalues().empty() || function->hasAttr("ctjs.skipped") ||
        prefix.pendingNewTarget.contains(function) ||
        prefix.pendingNewTarget.contains(sourceCall->getParentOfType<ctjs::FuncOp>())) {
        return {};
    }
    const auto parameters = function.getBody().front().getArguments();
    if (parameters.size() != arguments.size() + 3) { return {}; }
    for (auto implicit : parameters.take_front(3)) {
        for (mlir::Operation * user : implicit.getUsers()) {
            if (!prefix.step() || !llvm::isa<ctjs::RootOp>(user)) { return {}; }
        }
    }
    callbackReader reader{prefix, globals, {sourceCall, function, {}, {}}};
    prefixAnalysis::environment values;
    for (auto [parameter, argument] : llvm::zip(parameters.drop_front(3), arguments)) {
        if (!prefix.step()) { return {}; }
        auto known = reader.primitive(argument);
        if (known.kind != prefixValue::Kind::primitive) { return {}; }
        values[parameter] = known;
    }
    auto returned = providerRegion(reader, function.getBody(), values);
    if (returned.kind != prefixAnalysis::completion::Kind::returned ||
        returned.values.size() != 1 ||
        returned.values.front().kind != prefixValue::Kind::primitive || !prefix.step()) {
        return {};
    }
    reader.proof.result = returned.values.front().literal;
    proof.callbacks.push_back(std::move(reader.proof));
    return returned.values.front();
}

} // namespace ctcompile::ctnative::host_detail
