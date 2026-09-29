#include "Body.hpp"

#include "ctcompile/CTNative/Analysis/NativeMap.h"

namespace ctcompile::ctnative::dom_entry_detail {

// A LOCAL STANDARD MAP, admitted here as source shape only. The provider
// supplies the original constructor, and the complete census admits no
// binding write, prototype write or script reentry, so these are the standard
// methods. prepareNativeMaps proves storage, presence and carriers on the same
// module. Keys are elements, compared by document and node identity, or
// primitives; payloads are primitives or, one level deep, fresh child Maps.
// Neither side can run a conversion hook.
std::optional<bool> Body::mapOperation(mlir::Operation & operation) {
    if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation)) {
        if (load.getName() != "Map" || !supplied("Map")) { return std::nullopt; }
        values[load.getResult()] = Kind::mapIntrinsic;
        proved.intrinsics.insert(load);
        return true;
    }
    if (auto made = llvm::dyn_cast<ctjs::ConstructOp>(operation)) {
        if (!hasKind(made.getCallee(), Kind::mapIntrinsic)) { return std::nullopt; }
        if (!plainMapConstruction(made)) {
            refusal = "DOM Map requires an empty construction by its own constructor";
            return false;
        }
        // A parent stores fresh child Maps. Its fluent aliases share the
        // family; its reads are children, whose own payloads stay primitive.
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
        if (!arity) {
            refusal = "DOM Map property is not a supported constant method or size";
            return false;
        }
        for (mlir::OpOperand & use : read.getResult().getUses()) {
            if (!spend()) { return false; }
            if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
            auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
            if (!call || use.getOperandNumber() != 0 || call.getReceiver() != read.getObject() ||
                call.getArgs().size() != *arity) {
                refusal = "DOM Map method is detached, escapes or has another receiver";
                return false;
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
            refusal = "DOM Map payload comparison requires strict equality with a primitive";
            return false;
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
        refusal = "DOM Map key requires an element or a Number, String or Boolean";
        return false;
    }
    const bool parent = mapParents.contains(call.getReceiver());
    if (action == "set" && parent) {
        // Each child is its own fresh allocation: no instance is stored twice.
        auto child = call.getArgs()[1].getDefiningOp<ctjs::ConstructOp>();
        if (!child || !plainMapConstruction(child) || !hasKind(child.getResult(), Kind::map) ||
            !child.getResult().hasOneUse()) {
            refusal = "DOM parent Map payload requires one fresh child Map";
            return false;
        }
    } else if (action == "set" &&
               !hasKind(call.getArgs()[1], {Kind::number, Kind::string, Kind::boolean})) {
        refusal = "DOM Map payload requires a Number, String or Boolean";
        return false;
    }
    values[call.getResult()] = action == "set"     ? Kind::map
                               : action == "get"   ? (parent ? Kind::map : Kind::mapPayload)
                               : action == "clear" ? Kind::undefined
                                                   : Kind::boolean;
    return true;
}

} // namespace ctcompile::ctnative::dom_entry_detail
