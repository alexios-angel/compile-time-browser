//===- Objects.cpp - Objects seen by provider methods -----------*- C++ -*-===//
//
// Implements providerObjectState and the publication helpers declared in
// Objects.h.
//
//===----------------------------------------------------------------------===//

#include "Objects.h"

namespace ctcompile::ctnative::host_detail {

/// True when `value` is a known JS primitive literal.
bool prefixPrimitive(prefixValue value) {
    return value.kind == prefixValue::Kind::primitive && value.literal &&
           ctjs::isPrimitiveAttr(value.literal);
}

/// prefixCompare extended with `===` on objects, which compares identity.
prefixValue prefixObjectCompare(ctjs::CompareKind kind, prefixValue left, prefixValue right,
                                mlir::MLIRContext * context) {
    if (kind != ctjs::CompareKind::StrictEq ||
        (left.kind != prefixValue::Kind::object && right.kind != prefixValue::Kind::object)) {
        return prefixCompare(kind, left, right, context);
    }
    const auto supported = [](prefixValue value) {
        return value.kind == prefixValue::Kind::object || prefixPrimitive(value);
    };
    if (!supported(left) || !supported(right)) { return {}; }
    return prefixValue::constant(
        ctjs::BooleanAttr::get(context, left.kind == right.kind && left.object == right.object));
}

bool providerPublicationOwner(prefixAnalysis & prefix, prefixValue owner) {
    if (owner.kind == prefixValue::Kind::realm) { return true; }
    if (owner.kind != prefixValue::Kind::object || owner.object >= prefix.objects.size()) {
        return false;
    }
    if (prefix.objects[owner.object].published || prefix.factoryTables.contains(owner.object)) {
        return true;
    }
    for (const auto & root : prefix.contract.roots) {
        // Out of budget: answer "published" so callers refuse the object
        // rather than assume it is private.
        if (!prefix.step()) { return true; }
        const auto value = prefix.globals.lookup(root.binding);
        if (value.kind == prefixValue::Kind::object && value.object == owner.object) {
            return true;
        }
    }
    return false;
}

bool providerPublishObject(prefixAnalysis & prefix, prefixValue value) {
    if (value.kind != prefixValue::Kind::object) { return true; }
    llvm::SmallVector<unsigned> pending{value.object};
    llvm::DenseSet<unsigned> seen;
    while (!pending.empty()) {
        if (!prefix.step()) { return false; }
        const auto id = pending.pop_back_val();
        if (!seen.insert(id).second) { continue; }
        if (id >= prefix.objects.size() || prefix.objects[id].retained) { return false; }
        auto & object = prefix.objects[id];
        object.published = true;
        for (const auto & field : object.fields) {
            if (!prefix.step()) { return false; }
            if (field.second.kind == prefixValue::Kind::object) {
                pending.push_back(field.second.object);
            }
        }
    }
    return true;
}

bool providerObjectState::initialize() {
    for (const auto & object : prefix.objects) {
        if (!prefix.step()) { return false; }
        for (const auto & field : object.fields) {
            if (!prefix.step() || !prefix.spendText(field.first())) { return false; }
        }
    }
    objects = prefix.objects;
    return true;
}

bool providerObjectState::accepts(unsigned id) {
    if (!prefix.followProviderObjects || id >= objects.size() || !prefix.step()) { return false; }
    const auto & object = objects[id];
    auto allocation = object.allocation;
    if (!allocation || object.published || prefix.factoryTables.contains(id) ||
        allocation->getParentOfType<ctjs::FuncOp>().getSymName() != prefix.contract.entry ||
        object.invocation != allocation->getParentOfType<ctjs::FuncOp>().getOperation() ||
        providerPublicationOwner(prefix, prefixValue::of(prefixValue::Kind::object, id))) {
        return false;
    }
    // Only primitive fields: an object, Map or closure field could point back
    // into provider state, and only primitives avoid coercion. A missing
    // field stays missing; it is never assumed undefined or read from a
    // prototype.
    for (const auto & field : object.fields) {
        if (!prefix.step() || !prefix.spendText(field.first()) ||
            !ctjs::ordinaryKey(field.first()) || !prefixPrimitive(field.second)) {
            return false;
        }
    }
    return true;
}

bool providerObjectState::record(mlir::Operation * operation, unsigned id, llvm::StringRef member,
                                 llvm::StringRef action, mlir::Attribute result) {
    if (!prefix.step() || !prefix.spendText(member)) { return false; }
    const auto & object = objects[id];
    proof.objectOperations.push_back({operation, object.allocation, object.invocation, id + 1,
                                      member.str(), action.str(), result});
    return true;
}

bool providerObjectState::retain(unsigned id, mlir::Operation * operation) {
    if (!accepts(id) || !record(operation, id, {}, "retain", {})) { return false; }
    objects[id].retained = true;
    return true;
}

prefixValue providerObjectState::operation(mlir::Operation * operation,
                                           prefixAnalysis::environment & values) {
    mlir::Value receiver, key;
    if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
        receiver = read.getObject();
        key = read.getKey();
    } else if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(operation)) {
        receiver = write.getObject();
        key = write.getKey();
    } else if (auto erase = llvm::dyn_cast<ctjs::DeletePropertyOp>(operation)) {
        receiver = erase.getObject();
        key = erase.getKey();
    } else if (auto erase = llvm::dyn_cast<ctjs::DeleteNamedOp>(operation)) {
        receiver = erase.getObject();
    } else {
        return {};
    }
    const auto owner = values.lookup(receiver);
    auto named = llvm::dyn_cast<ctjs::DeleteNamedOp>(operation);
    const auto name = named ? named.getName() : ctjs::constantKey(key);
    if (owner.kind != prefixValue::Kind::object || !ctjs::ordinaryKey(name) ||
        !prefix.spendText(name) || !accepts(owner.object)) {
        return {};
    }
    auto & fields = objects[owner.object].fields;
    if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(operation)) {
        const auto value = values.lookup(write.getValue());
        if (!prefixPrimitive(value) ||
            !record(operation, owner.object, name, "write", value.literal)) {
            return {};
        }
        fields[name] = value;
        return prefixValue::constant(ctjs::UndefinedAttr::get(prefix.module.getContext()));
    }
    if (llvm::isa<ctjs::DeletePropertyOp, ctjs::DeleteNamedOp>(operation)) {
        const auto result = ctjs::BooleanAttr::get(prefix.module.getContext(), true);
        if (!record(operation, owner.object, name, "delete", result)) { return {}; }
        fields.erase(name);
        return prefixValue::constant(result);
    }
    const auto field = fields.find(name);
    if (field == fields.end() ||
        !record(operation, owner.object, name, "read", field->second.literal)) {
        return {};
    }
    return field->second;
}

} // namespace ctcompile::ctnative::host_detail
