#include "Prefix.h"

#include "mlir/Dialect/Arith/IR/Arith.h"

#include <cmath>

namespace ctcompile::ctnative::host_detail {

std::optional<bool> prefixTruth(prefixValue value) {
    if (value.kind == prefixValue::Kind::object || value.kind == prefixValue::Kind::realm ||
        value.kind == prefixValue::Kind::resource || value.kind == prefixValue::Kind::closure) {
        return true;
    }
    if (value.kind != prefixValue::Kind::primitive) { return {}; }
    auto literal = value.literal;
    if (llvm::isa<ctjs::UndefinedAttr, ctjs::NullAttr>(literal)) { return false; }
    if (auto boolean = llvm::dyn_cast<ctjs::BooleanAttr>(literal)) { return boolean.getValue(); }
    if (auto number = llvm::dyn_cast<ctjs::NumberAttr>(literal)) {
        return number.getDouble() != 0 && !std::isnan(number.getDouble());
    }
    if (auto text = llvm::dyn_cast<ctjs::StringAttr>(literal)) { return !text.getValue().empty(); }
    if (auto integer = llvm::dyn_cast<mlir::IntegerAttr>(literal)) {
        if (integer.getValue().isZero() || integer.getValue().isOne()) {
            return integer.getValue().isOne();
        }
    }
    return {};
}

prefixValue prefixUnary(ctjs::UnaryKind kind, prefixValue operand, mlir::MLIRContext * context) {
    if (kind == ctjs::UnaryKind::Void) {
        return prefixValue::constant(ctjs::UndefinedAttr::get(context));
    }
    if (kind == ctjs::UnaryKind::Not) {
        const auto bit = prefixTruth(operand);
        return bit ? prefixValue::constant(ctjs::BooleanAttr::get(context, !*bit)) : prefixValue{};
    }
    if (kind != ctjs::UnaryKind::TypeOf) { return {}; }
    llvm::StringRef name;
    if (operand.kind == prefixValue::Kind::closure) {
        name = "function";
    } else if (operand.kind == prefixValue::Kind::object ||
               operand.kind == prefixValue::Kind::realm ||
               operand.kind == prefixValue::Kind::resource) {
        name = "object";
    } else if (operand.kind == prefixValue::Kind::absent ||
               llvm::isa_and_nonnull<ctjs::UndefinedAttr>(operand.literal)) {
        name = "undefined";
    } else if (llvm::isa_and_nonnull<ctjs::NullAttr>(operand.literal)) {
        name = "object";
    } else if (llvm::isa_and_nonnull<ctjs::StringAttr>(operand.literal)) {
        name = "string";
    } else if (llvm::isa_and_nonnull<ctjs::BooleanAttr>(operand.literal)) {
        name = "boolean";
    } else if (llvm::isa_and_nonnull<ctjs::NumberAttr>(operand.literal)) {
        name = "number";
    }
    return name.empty() ? prefixValue{}
                        : prefixValue::constant(ctjs::StringAttr::get(context, name));
}

prefixValue prefixCompare(ctjs::CompareKind kind, prefixValue left, prefixValue right,
                          mlir::MLIRContext * context) {
    // Strict primitive equality cannot call JS. In particular null and
    // undefined must stay distinct when advancing source observation guards.
    if (kind == ctjs::CompareKind::StrictEq && left.kind == prefixValue::Kind::primitive &&
        right.kind == prefixValue::Kind::primitive) {
        auto supported = [](mlir::Attribute value) {
            return llvm::isa<ctjs::UndefinedAttr, ctjs::NullAttr, ctjs::BooleanAttr,
                             ctjs::NumberAttr, ctjs::StringAttr>(value);
        };
        if (!supported(left.literal) || !supported(right.literal)) { return {}; }
        bool equal = left.literal == right.literal;
        auto a = llvm::dyn_cast<ctjs::NumberAttr>(left.literal);
        auto b = llvm::dyn_cast<ctjs::NumberAttr>(right.literal);
        if (a && b) { equal = a.getDouble() == b.getDouble(); }
        return prefixValue::constant(ctjs::BooleanAttr::get(context, equal));
    }
    // Loose equality stays limited to exact strings; no object conversion.
    if (kind != ctjs::CompareKind::Eq && kind != ctjs::CompareKind::StrictEq) { return {}; }
    const auto a = llvm::dyn_cast_if_present<ctjs::StringAttr>(left.literal);
    const auto b = llvm::dyn_cast_if_present<ctjs::StringAttr>(right.literal);
    return a && b ? prefixValue::constant(ctjs::BooleanAttr::get(context, a == b)) : prefixValue{};
}

prefixValue prefixAnalysis::operation(mlir::Operation * operation, environment & values,
                                      unsigned depth) {
    auto * context = module.getContext();
    if (auto constant = llvm::dyn_cast<ctjs::ConstantOp>(operation)) {
        return prefixValue::constant(constant.getValue());
    }
    if (auto constant = llvm::dyn_cast<mlir::arith::ConstantOp>(operation)) {
        return prefixValue::constant(constant.getValue());
    }
    if (llvm::isa<ctjs::CreateObjectOp>(operation)) {
        const unsigned id = static_cast<unsigned>(objects.size());
        objects.emplace_back();
        return {prefixValue::Kind::object, {}, {}, id};
    }
    if (auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(operation)) {
        prefixValue closure{prefixValue::Kind::closure, {}, made, 0};
        return target(closure) ? closure : stop(operation, "unknown source closure identity");
    }
    if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation)) {
        auto found = globals.find(load.getName());
        if (found == globals.end()) { return stop(operation, "unproved host binding"); }
        if (found->second.kind == prefixValue::Kind::absent) {
            for (mlir::Operation * user : load.getResult().getUsers()) {
                if (llvm::isa<ctjs::RootOp>(user)) { continue; }
                auto unary = llvm::dyn_cast<ctjs::UnaryOp>(user);
                if (!unary || unary.getKind() != ctjs::UnaryKind::TypeOf) {
                    return stop(operation, "absent binding is read outside typeof");
                }
            }
        }
        return found->second;
    }
    if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(operation)) {
        globals[store.getName()] = values.lookup(store.getValue());
        return {};
    }
    if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
        auto owner = values.lookup(read.getObject());
        auto key = values.lookup(read.getKey());
        auto text = llvm::dyn_cast_if_present<ctjs::StringAttr>(key.literal);
        if (followPublication && owner.kind == prefixValue::Kind::realm && text &&
            llvm::is_contained(contract.realmOwnDataProperties, text.getValue())) {
            const auto field = globals.find(text.getValue());
            return field == globals.end() || field->second.kind == prefixValue::Kind::unknown
                       ? stop(operation, "realm property value has not been initialized by source")
                       : field->second;
        }
        if (owner.kind != prefixValue::Kind::object || !text || !ordinaryKey(text.getValue())) {
            return stop(operation, "property read lacks a fresh ordinary receiver/key");
        }
        auto field = objects[owner.object].find(text.getValue());
        // Missing properties could reach an intrinsic prototype. The first
        // slice reads only already initialized own fields.
        return field == objects[owner.object].end()
                   ? stop(operation, "property read lacks an initialized own slot")
                   : field->second;
    }
    if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(operation)) {
        auto owner = values.lookup(write.getObject());
        auto key = values.lookup(write.getKey());
        auto text = llvm::dyn_cast_if_present<ctjs::StringAttr>(key.literal);
        if (followPublication && owner.kind == prefixValue::Kind::realm && text &&
            llvm::is_contained(contract.realmOwnDataProperties, text.getValue())) {
            const auto value = values.lookup(write.getValue());
            globals[text.getValue()] = value;
            publication(write, owner, value);
            return {};
        }
        if (owner.kind != prefixValue::Kind::object || !text || !ordinaryKey(text.getValue())) {
            return stop(operation, "property write lacks a fresh ordinary receiver/key");
        }
        objects[owner.object][text.getValue()] = values.lookup(write.getValue());
        publication(write, owner, values.lookup(write.getValue()));
        return {};
    }
    if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation)) {
        auto result = prefixUnary(unary.getKind(), values.lookup(unary.getOperand()), context);
        return result.kind == prefixValue::Kind::unknown
                   ? stop(operation, "unproved unary conversion")
                   : result;
    }
    if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation)) {
        auto result = prefixCompare(compare.getKind(), values.lookup(compare.getLhs()),
                                    values.lookup(compare.getRhs()), context);
        return result.kind == prefixValue::Kind::unknown
                   ? stop(operation, "unproved comparison behavior")
                   : result;
    }
    if (auto truth = llvm::dyn_cast<ctjs::TruthyOp>(operation)) {
        auto bit = prefixTruth(values.lookup(truth.getValue()));
        return bit ? prefixValue::constant(ctjs::BooleanAttr::get(context, *bit)) : prefixValue{};
    }
    if (llvm::isa<mlir::arith::TruncIOp, mlir::arith::ExtUIOp>(operation)) {
        auto bit = prefixTruth(values.lookup(operation->getOperand(0)));
        return bit ? prefixValue::constant(ctjs::BooleanAttr::get(context, *bit)) : prefixValue{};
    }
    if (auto invoked = llvm::dyn_cast<ctjs::CallDirectOp>(operation)) {
        auto closure = values.lookup(invoked.getCalleeValue());
        auto callee = target(closure);
        auto lexical = closure.made
                           ? closure.made.getEnclosingThis().getDefiningOp<ctjs::ConstantOp>()
                           : ctjs::ConstantOp{};
        if (!callee || callee.getSymName() != invoked.getCallee() || !lexical ||
            !llvm::isa<ctjs::UndefinedAttr>(lexical.getValue()) ||
            !uniqueContext(callee, invoked, closure.made)) {
            return stop(operation, "call lacks one closed source invocation context");
        }
        llvm::SmallVector<prefixValue> arguments;
        for (auto [index, argument] : llvm::enumerate(invoked.getOperands())) {
            // A call's raw receiver is not a proof of effective JS `this`:
            // sloppy substitution/boxing and lexical receivers need their
            // own contract. Preserve the operand but do not interpret it.
            arguments.push_back(index == 0 ? prefixValue{} : values.lookup(argument));
        }
        auto returned = function(callee, arguments, depth + 1);
        return returned.kind == completion::Kind::returned && returned.values.size() == 1
                   ? returned.values.front()
                   : prefixValue{};
    }
    if (auto invoked = llvm::dyn_cast<ctjs::CallOp>(operation)) {
        auto closure = values.lookup(invoked.getCallee());
        auto callee = target(closure);
        auto lexical = closure.made
                           ? closure.made.getEnclosingThis().getDefiningOp<ctjs::ConstantOp>()
                           : ctjs::ConstantOp{};
        const bool summarizedClosure =
            followPublication && closure.made && factoryClosures.contains(closure.made);
        if (!callee || callee.getBody().empty() ||
            ((!lexical || !llvm::isa<ctjs::UndefinedAttr>(lexical.getValue())) &&
             !summarizedClosure) ||
            callee.getBody().front().getNumArguments() != invoked.getArgs().size() + 3 ||
            pendingNewTarget.contains(invoked->getParentOfType<ctjs::FuncOp>())) {
            return stop(operation, "indirect call lacks exact source target/argument state");
        }
        calls.push_back({invoked, callee});
        if (followPublication) {
            const auto result = factory(invoked, callee);
            if (result.kind != prefixValue::Kind::unknown) { return result; }
        }
        if (followProviderReads) {
            // A mutating prefix must never consult the old empty-Map model.
            const auto result = followProviderMutations
                                    ? providerMutation(invoked, callee, closure, values)
                                    : providerRead(invoked, callee, closure, values);
            if (result.kind != prefixValue::Kind::unknown) { return result; }
        }
        // Naming this invocation requires its actual closure value, not a
        // reusable-body proof. Interpreting the target would additionally need
        // that body's unique context; stop before any of its effects instead.
        return stop(operation, "resolved call body remains a runtime effect boundary");
    }
    if (llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp>(operation)) { return {}; }
    return stop(operation, "unsupported prefix operation");
}

} // namespace ctcompile::ctnative::host_detail
