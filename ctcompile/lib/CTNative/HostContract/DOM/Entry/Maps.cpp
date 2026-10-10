//===- Maps.cpp - DOM entry Maps and records ------------------------------===//
//
// Body::mapOperation and Body::recordOperation: local `new Map()` instances and
// the fixed-shape record objects stored in them.
//
//===----------------------------------------------------------------------===//

#include "Body.hpp"

#include "ctcompile/CTNative/Analysis/NativeMap.h"

namespace ctcompile::ctnative::dom_entry_detail {

/// Admits a `new Map()` local to the entry function by its source shape only.
/// The host provider supplies the original Map constructor, and the
/// whole-module scan has already ruled out writes to bindings or prototypes
/// and any script reentry, so the methods called are the standard ones.
/// prepareNativeMaps then proves storage, presence and carriers on the same
/// module. Keys are elements (compared by document and node identity) or
/// primitives; values are primitives or, one level deep, fresh child Maps.
/// No key or value can run a conversion hook. Returns std::nullopt for an
/// operation that is not a Map or record operation.
std::optional<bool> Body::mapOperation(mlir::Operation & operation) {
    if (const auto handled = recordOperation(operation)) { return handled; }
    if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation)) {
        if (load.getName() != "Map" || !supplied("Map")) { return std::nullopt; }
        values[load.getResult()] = Kind::mapIntrinsic;
        proved.intrinsics.insert(load);
        return true;
    }
    if (auto made = llvm::dyn_cast<ctjs::ConstructOp>(operation)) {
        if (!hasKind(made.getCallee(), Kind::mapIntrinsic)) { return std::nullopt; }
        if (!plainMapConstruction(made)) {
            return fail("DOM Map requires an empty construction by its own constructor");
        }
        // A parent Map stores fresh child Maps. The results of its chained
        // set() calls are aliases of the same parent; its get() results are
        // children, whose own values stay primitive.
        llvm::SmallVector<mlir::Value> aliases{made.getResult()};
        bool parent = false;
        for (std::size_t index = 0; index < aliases.size(); ++index) {
            for (mlir::Operation * user : aliases[index].getUsers()) {
                if (!spend()) { return false; }
                auto set = llvm::dyn_cast<ctjs::CallOp>(user);
                if (!set || set.getReceiver() != aliases[index] || mapMethodName(set) != "set" ||
                    set.getArgs().size() != 2) {
                    continue;
                }
                aliases.push_back(set.getResult());
                auto child = set.getArgs()[1].getDefiningOp<ctjs::ConstructOp>();
                parent |= child && plainMapConstruction(child);
            }
        }
        if (parent) { mapParents.insert(aliases.begin(), aliases.end()); }
        values[made.getResult()] = Kind::map;
        return true;
    }
    if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
        if (!hasKind(read.getObject(), Kind::map)) { return std::nullopt; }
        const auto key = ctjs::constantKey(read.getKey());
        if (key == "size") {
            values[read.getResult()] = Kind::number;
            return true;
        }
        const auto arity = mapMethodArity(key);
        if (!arity) { return fail("DOM Map property is not a supported constant method or size"); }
        for (mlir::OpOperand & use : read.getResult().getUses()) {
            if (!spend()) { return false; }
            if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
            auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
            if (!call || use.getOperandNumber() != 0 || call.getReceiver() != read.getObject() ||
                call.getArgs().size() != *arity) {
                return fail("DOM Map method is detached, escapes or has another receiver");
            }
        }
        values[read.getResult()] = Kind::mapMethod;
        return true;
    }
    if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation)) {
        const auto payload = [&](mlir::Value value) { return hasKind(value, Kind::mapPayload); };
        if (!payload(compare.getLhs()) && !payload(compare.getRhs())) { return std::nullopt; }
        const auto primitive = [&](mlir::Value value) {
            return hasKind(value, {Kind::mapPayload, Kind::number, Kind::string, Kind::boolean,
                                   Kind::null, Kind::undefined});
        };
        if (compare.getKind() != ctjs::CompareKind::StrictEq || !primitive(compare.getLhs()) ||
            !primitive(compare.getRhs())) {
            return fail("DOM Map payload comparison requires strict equality with a primitive");
        }
        values[compare.getResult()] = Kind::boolean;
        return true;
    }
    auto call = llvm::dyn_cast<ctjs::CallOp>(operation);
    if (!call || !hasKind(call.getCallee(), Kind::mapMethod)) { return std::nullopt; }
    // The method read proved this receiver and the exact argument count.
    const auto action = mapMethodName(call);
    if (action != "clear" &&
        !hasKind(call.getArgs()[0], {Kind::element, Kind::number, Kind::string, Kind::boolean})) {
        return fail("DOM Map key requires an element or a Number, String or Boolean");
    }
    const bool parent = mapParents.contains(call.getReceiver());
    const auto schema = recordSchemas.find(call.getReceiver());
    if (action == "set" && hasKind(call.getArgs()[1], Kind::record)) {
        // A record goes only into its own Map, never a parent Map, and all
        // records of one Map have the same fields.
        const auto record = call.getArgs()[1];
        const auto & fields = recordFields[record];
        if (recordMaps.lookup(record) != call.getReceiver() || parent ||
            primitiveMaps.contains(call.getReceiver())) {
            return fail("DOM record Map stores only its own records");
        }
        if (schema == recordSchemas.end()) {
            recordSchemas[call.getReceiver()] = fields;
        } else if (schema->second.size() != fields.size() ||
                   llvm::any_of(fields, [&](const auto & field) {
                       return schema->second.lookup(field.getKey()) != field.getValue();
                   })) {
            return fail("DOM record fields differ from their Map's schema");
        }
        storedRecords.insert(record);
    } else if (action == "set" && parent) {
        // Each child is a fresh allocation used only here, so no instance is
        // stored twice.
        auto child = call.getArgs()[1].getDefiningOp<ctjs::ConstructOp>();
        if (!child || !plainMapConstruction(child) || !hasKind(child.getResult(), Kind::map) ||
            !child.getResult().hasOneUse()) {
            return fail("DOM parent Map payload requires one fresh child Map");
        }
    } else if (action == "set" &&
               (schema != recordSchemas.end() ||
                !hasKind(call.getArgs()[1], {Kind::number, Kind::string, Kind::boolean}))) {
        return fail("DOM Map payload requires a Number, String or Boolean");
    } else if (action == "set") {
        primitiveMaps.insert(call.getReceiver());
    }
    if (action == "get" && schema != recordSchemas.end()) {
        // NativeMap's proveRecords must prove the record is present, so the
        // result has no absent case.
        values[call.getResult()] = Kind::recordAlias;
        recordMaps[call.getResult()] = call.getReceiver();
        return true;
    }
    values[call.getResult()] = action == "set"     ? Kind::map
                               : action == "get"   ? (parent ? Kind::map : Kind::mapPayload)
                               : action == "clear" ? Kind::undefined
                                                   : Kind::boolean;
    return true;
}

/// Admits a record: a fresh object used only through constant ordinary field
/// reads and writes and as the value of `set` on one local Map. The fields
/// written before it is first stored are its whole shape; every record of that
/// Map has the same scalar fields, and later writes through the record or a
/// `get` alias keep each field's kind. The object keeps a single identity:
/// NativeMap::proveRecords makes it a stack owner that the Map borrows, which
/// also proves every read present. Returns std::nullopt for an operation that
/// does not involve a record.
std::optional<bool> Body::recordOperation(mlir::Operation & operation) {
    const auto fieldKey = [&](mlir::Value key) {
        auto name = ctjs::constantAttr<ctjs::StringAttr>(key);
        return name && ctjs::ordinaryKey(key) && name.getValue() != "__proto__" ? name.getValue()
                                                                                : llvm::StringRef{};
    };
    if (auto object = llvm::dyn_cast<ctjs::CreateObjectOp>(operation)) {
        // The `set` of a local `new Map()`, called on that Map, storing this
        // object. The Map may be built after the record; visiting it proves
        // its constructor, and the `set` itself must be its proved method.
        const auto stored = [&](mlir::OpOperand & use) {
            auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
            auto method = call ? call.getCallee().getDefiningOp<ctjs::GetPropertyOp>()
                               : ctjs::GetPropertyOp{};
            auto made =
                call ? call.getReceiver().getDefiningOp<ctjs::ConstructOp>() : ctjs::ConstructOp{};
            auto map =
                made ? made.getCallee().getDefiningOp<ctjs::LoadGlobalOp>() : ctjs::LoadGlobalOp{};
            return method && use.getOperandNumber() == 3 && call.getArgs().size() == 2 &&
                           method.getObject() == call.getReceiver() &&
                           ctjs::constantKey(method.getKey()) == "set" && map &&
                           map.getName() == "Map" && plainMapConstruction(made)
                       ? call.getReceiver()
                       : mlir::Value{};
        };
        // This first scan does not spend the analysis budget: the main walk
        // charges each use when it visits it, so an object that turns out not
        // to be a record still has the full budget for the JSON object proof
        // in Body::visit.
        mlir::Value owner;
        for (mlir::OpOperand & use : object.getResult().getUses()) {
            const auto map = stored(use);
            if (!map) { continue; }
            if (owner && owner != map) { return fail("DOM record belongs to one local Map"); }
            owner = map;
        }
        if (!owner) { return std::nullopt; }
        for (mlir::OpOperand & use : object.getResult().getUses()) {
            if (!spend()) { return false; }
            auto * user = use.getOwner();
            if (llvm::isa<ctjs::RootOp>(user) ||
                (use.getOperandNumber() == 0 &&
                 llvm::isa<ctjs::GetPropertyOp, ctjs::SetPropertyOp>(user)) ||
                stored(use) == owner) {
                continue;
            }
            return fail("DOM record escapes its fields and its Map");
        }
        values[object.getResult()] = Kind::record;
        recordMaps[object.getResult()] = owner;
        recordFields[object.getResult()] = {};
        proved.records.push_back(object);
        return true;
    }
    auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(operation);
    auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation);
    const auto object = write ? write.getObject() : read ? read.getObject() : mlir::Value{};
    if (!object || !hasKind(object, {Kind::record, Kind::recordAlias})) { return std::nullopt; }
    const auto key = fieldKey(write ? write.getKey() : read.getKey());
    if (key.empty() || (write && write.getValue() == object)) {
        return fail("DOM record field requires a constant ordinary String key");
    }
    // Before its first `set`, a record defines its own fields; after it, and
    // through any alias, only its Map's schema exists.
    const bool initializing = hasKind(object, Kind::record) && !storedRecords.contains(object);
    auto & fields = initializing ? recordFields[object] : recordSchemas[recordMaps.lookup(object)];
    const auto slot = fields.find(key);
    if (read) {
        if (slot == fields.end()) { return fail("DOM record field read precedes its write"); }
        values[read.getResult()] = slot->getValue();
        return true;
    }
    const auto stored = values.find(write.getValue());
    const auto kind = stored == values.end() ? Kind::implicit : stored->second;
    if (!llvm::is_contained({Kind::number, Kind::string, Kind::boolean}, kind) ||
        (slot == fields.end() ? !initializing : slot->getValue() != kind)) {
        return fail("DOM record field requires one Number, String or Boolean kind in its schema");
    }
    fields[key] = kind;
    return true;
}

} // namespace ctcompile::ctnative::dom_entry_detail
