//===- Values.cpp - Value evaluation for the host-entry prefix --*- C++ -*-===//
//
// Evaluates scalar operations (truthiness, unary operators, comparisons) on
// analysis values, and implements prefixAnalysis::operation, the per-operation
// interpreter of the host-entry prefix.
//
//===----------------------------------------------------------------------===//

#include "../Provider/Objects.h"
#include "Analysis.h"

#include "mlir/Dialect/Arith/IR/Arith.h"

#include <cmath>

namespace ctcompile::ctnative::host_detail {

/// The JS truthiness of `value`, or nullopt when it is not known.
std::optional<bool> prefixTruth(prefixValue value) {
    if (value.kind == prefixValue::Kind::object || value.kind == prefixValue::Kind::realm ||
        value.kind == prefixValue::Kind::resource || value.kind == prefixValue::Kind::closure) {
        return true;
    }
    if (value.kind != prefixValue::Kind::primitive) { return {}; }
    if (auto number = llvm::dyn_cast_if_present<ctjs::NumberAttr>(value.literal)) {
        return number.getDouble() != 0 && !std::isnan(number.getDouble());
    }
    return literalTruth(value.literal);
}

/// Evaluates constants and truthiness conversions. nullopt when `operation`
/// is not one of those; an unknown value when its operand's truth is unknown.
std::optional<prefixValue> prefixScalar(mlir::Operation * operation,
                                        const prefixAnalysis::environment & values,
                                        mlir::MLIRContext * context) {
    if (auto constant = llvm::dyn_cast<ctjs::ConstantOp>(operation)) {
        return prefixValue::constant(constant.getValue());
    }
    if (auto constant = llvm::dyn_cast<mlir::arith::ConstantOp>(operation)) {
        return prefixValue::constant(constant.getValue());
    }
    if (!llvm::isa<ctjs::TruthyOp, mlir::arith::TruncIOp, mlir::arith::ExtUIOp>(operation)) {
        return std::nullopt;
    }
    auto bit = prefixTruth(values.lookup(operation->getOperand(0)));
    return bit ? prefixValue::constant(ctjs::BooleanAttr::get(context, *bit)) : prefixValue{};
}

/// Evaluates `void`, `!` and `typeof` on a known operand; unknown otherwise.
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
    } else if (operand.kind == prefixValue::Kind::absent) {
        name = "undefined";
    } else {
        name = typeofLiteral(operand.literal);
    }
    return name.empty() ? prefixValue{}
                        : prefixValue::constant(ctjs::StringAttr::get(context, name));
}

/// Evaluates `===` on primitives and `==`/`===` on two strings; unknown
/// otherwise.
prefixValue prefixCompare(ctjs::CompareKind kind, prefixValue left, prefixValue right,
                          mlir::MLIRContext * context) {
    // Strict primitive equality cannot run script. It must also keep null and
    // undefined distinct, since the guards it decides often test for one.
    if (kind == ctjs::CompareKind::StrictEq && left.kind == prefixValue::Kind::primitive &&
        right.kind == prefixValue::Kind::primitive) {
        auto supported = [](mlir::Attribute value) { return ctjs::isPrimitiveAttr(value); };
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

/// Interprets one operation of the host-entry prefix. Returns its value, or
/// calls stop() with a reason (returning unknown) when it is not modeled.
prefixValue prefixAnalysis::operation(mlir::Operation * operation, environment & values,
                                      unsigned depth) {
    auto * context = module.getContext();
    if (auto scalar = prefixScalar(operation, values, context)) { return *scalar; }
    if (auto made = llvm::dyn_cast<ctjs::CreateObjectOp>(operation)) {
        const unsigned id = static_cast<unsigned>(objects.size());
        objects.push_back({{}, made, made->getParentOfType<ctjs::FuncOp>(), false, false});
        return prefixValue::of(prefixValue::Kind::object, id);
    }
    if (auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(operation)) {
        prefixValue closure{prefixValue::Kind::closure, {}, made, 0};
        return target(closure) ? closure : stop(operation, "unknown source closure identity");
    }
    if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation)) {
        auto found = globals.find(load.getName());
        if (found == globals.end()) { return stop(operation, "unproved host binding"); }
        if (found->second.kind == prefixValue::Kind::absent) {
            if (!load.getTypeofLookup()) {
                return stop(operation, "absent binding lacks source typeof lookup mode");
            }
            return prefixValue::constant(ctjs::UndefinedAttr::get(context));
        }
        return found->second;
    }
    if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(operation)) {
        const auto value = values.lookup(store.getValue());
        if (followProviderObjects) {
            for (const auto & root : contract.roots) {
                if (!step() ||
                    (root.binding == store.getName() && !providerPublishObject(*this, value))) {
                    return stop(operation, "provider object reaches host publication");
                }
            }
        }
        globals[store.getName()] = value;
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
        if (owner.kind != prefixValue::Kind::object || !text ||
            !ctjs::ordinaryKey(text.getValue())) {
            return stop(operation, "property read lacks a fresh ordinary receiver/key");
        }
        auto field = objects[owner.object].fields.find(text.getValue());
        // A missing property could be found on an intrinsic prototype, so
        // only own fields this prefix has already initialized are read.
        return field == objects[owner.object].fields.end()
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
            if (followProviderObjects && !providerPublishObject(*this, value)) {
                return stop(operation, "provider object reaches realm publication");
            }
            globals[text.getValue()] = value;
            publication(write, owner, value);
            return {};
        }
        if (owner.kind != prefixValue::Kind::object || !text ||
            !ctjs::ordinaryKey(text.getValue())) {
            return stop(operation, "property write lacks a fresh ordinary receiver/key");
        }
        const auto value = values.lookup(write.getValue());
        if (followProviderObjects && objects[owner.object].retained && !prefixPrimitive(value)) {
            return stop(operation, "provider object field no longer has scalar own data");
        }
        if (followProviderObjects && providerPublicationOwner(*this, owner) &&
            !providerPublishObject(*this, value)) {
            return stop(operation, "provider object reaches host publication");
        }
        objects[owner.object].fields[text.getValue()] = value;
        publication(write, owner, value);
        return {};
    }
    if (followProviderObjects &&
        llvm::isa<ctjs::DeletePropertyOp, ctjs::DeleteNamedOp>(operation)) {
        auto computed = llvm::dyn_cast<ctjs::DeletePropertyOp>(operation);
        auto named = llvm::dyn_cast<ctjs::DeleteNamedOp>(operation);
        const auto owner = values.lookup(computed ? computed.getObject() : named.getObject());
        const auto key = computed ? ctjs::constantKey(computed.getKey()) : named.getName();
        if (owner.kind != prefixValue::Kind::object || !ctjs::ordinaryKey(key)) {
            return stop(operation, "property deletion lacks a fresh ordinary receiver/key");
        }
        objects[owner.object].fields.erase(key);
        return prefixValue::constant(ctjs::BooleanAttr::get(context, true));
    }
    if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation)) {
        auto result = prefixUnary(unary.getKind(), values.lookup(unary.getOperand()), context);
        return result.kind == prefixValue::Kind::unknown
                   ? stop(operation, "unproved unary conversion")
                   : result;
    }
    if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation)) {
        auto result = followProviderObjects
                          ? prefixObjectCompare(compare.getKind(), values.lookup(compare.getLhs()),
                                                values.lookup(compare.getRhs()), context)
                          : prefixCompare(compare.getKind(), values.lookup(compare.getLhs()),
                                          values.lookup(compare.getRhs()), context);
        return result.kind == prefixValue::Kind::unknown
                   ? stop(operation, "unproved comparison behavior")
                   : result;
    }
    if (auto invoked = llvm::dyn_cast<ctjs::CallDirectOp>(operation)) {
        auto closure = values.lookup(invoked.getCalleeValue());
        auto callee = target(closure);
        if (!callee || callee.getSymName() != invoked.getCallee() || !closure.made ||
            !ctjs::isUndefinedConstant(closure.made.getEnclosingThis()) ||
            !uniqueContext(callee, invoked, closure.made)) {
            return stop(operation, "call lacks one closed source invocation context");
        }
        llvm::SmallVector<prefixValue> arguments;
        for (auto [index, argument] : llvm::enumerate(invoked.getOperands())) {
            // The receiver operand is not necessarily the callee's `this`:
            // sloppy-mode substitution and boxing, and lexical `this`, would
            // need their own modeling. Pass it as unknown.
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
        const bool lexical =
            closure.made && ctjs::isUndefinedConstant(closure.made.getEnclosingThis());
        const bool summarizedClosure =
            followPublication && closure.made && factoryClosures.contains(closure.made);
        // The callee's entry block has the three implicit arguments (receiver,
        // new.target, callee) before the declared ones.
        if (!callee || callee.getBody().empty() || (!lexical && !summarizedClosure) ||
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
            // A prefix that mutates provider Maps must use providerMutation:
            // providerRead assumes the captured Maps are still empty.
            const auto result = followProviderMutations
                                    ? providerMutation(invoked, callee, closure, values)
                                    : providerRead(invoked, callee, closure, values);
            if (result.kind != prefixValue::Kind::unknown) { return result; }
        }
        // The call target is known from the closure value, so the call is
        // recorded above. Interpreting the body would also need proof that
        // this is its only calling context, so stop before any of its effects.
        return stop(operation, "resolved call body remains a runtime effect boundary");
    }
    if (llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp>(operation)) { return {}; }
    return stop(operation, "unsupported prefix operation");
}

} // namespace ctcompile::ctnative::host_detail
