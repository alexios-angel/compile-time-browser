#include "../../Analysis/OwnedGlobalRoots.h"
#include "Emitter.h"

namespace ctcompile::ctnative::lowering_detail {

void lowering::censusOwnedGlobals(const OwnedGlobalRoots & roots,
                                  llvm::ArrayRef<ctjs::FuncOp> accepted) {
    for (const auto & root : roots.roots()) {
        auto owner = root.owner;
        if (!llvm::is_contained(accepted, owner->getParentOfType<ctjs::FuncOp>())) { continue; }
        const std::string name = spelling(shapeAt(owner.getResult()));
        const mlir::Type type =
            domDataSession.empty()
                ? mlir::Type(ec::OpaqueType::get(context, "std::shared_ptr<" + name + ">"))
                : mlir::Type(ec::PointerType::get(ec::OpaqueType::get(context, name)));
        auto field = root.fieldInitialization;
        const unsigned index = static_cast<unsigned>(ownedGlobalStoragePlans.size());
        ownedGlobalStoragePlans.push_back(
            {root.binding, name, root.property, type, accessType.lookup(field)});
        ownedGlobals[root.binding] = type;
        ownedObjectTypes[owner.getResult()] = type;
        ownedGlobalOperations[owner] = index;
        ownedGlobalOperations[root.initialization] = index;
        ownedGlobalOperations[field] = index;
        for (ctjs::LoadGlobalOp load : root.loads) {
            ownedObjectTypes[load.getResult()] = type;
            ownedGlobalOperations[load] = index;
        }
        for (ctjs::GetPropertyOp read : root.reads) { ownedGlobalOperations[read] = index; }
    }
    for (const HostObjectGlobalRead & edge : roots.objectReads()) {
        auto object = edge.object;
        if (!llvm::is_contained(accepted, object->getParentOfType<ctjs::FuncOp>())) { continue; }
        auto initialization = edge.initialization;
        auto read = edge.read;
        auto found = ownedGlobalOperations.find(initialization);
        unsigned index;
        if (found == ownedGlobalOperations.end()) {
            const auto type = carrierType(context, carrier::objectIdentity);
            index = static_cast<unsigned>(ownedGlobalStoragePlans.size());
            ownedGlobalStoragePlans.push_back(
                {initialization.getName().str(), "ctnative::identity_object", {}, type, {}});
            ownedGlobals[initialization.getName()] = type;
            ownedGlobalOperations[initialization] = index;
            needsObjectIdentity = true;
        } else {
            index = found->second;
        }
        ownedGlobalOperations[read] = index;
    }
}

bool lowering::replaceOwnedGlobal(mlir::Operation * operation) {
    const auto found = ownedGlobalOperations.find(operation);
    if (found == ownedGlobalOperations.end()) { return false; }
    const auto & storage = ownedGlobalStoragePlans[found->second];
    const bool memberRoot = !domDataSession.empty() && llvm::isa<ec::PointerType>(storage.type);
    mlir::OpBuilder at(operation);
    const auto where = operation->getLoc();
    mlir::Value result;
    if (llvm::isa<ctjs::CreateObjectOp>(operation)) {
        result = callWithConstValueOperands(
                     at, where, mlir::TypeRange{storage.type},
                     at.getStringAttr(memberRoot ? "reset_g_" + storage.binding
                                                 : "std::make_shared<" + storage.className + ">"),
                     mlir::ValueRange{})
                     .getResult(0);
    } else if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(operation)) {
        if (!memberRoot) {
            ec::AssignOp::create(at, where, lvalueOfGlobal(at, where, storage.binding),
                                 store.getValue());
        }
    } else if (llvm::isa<ctjs::LoadGlobalOp>(operation)) {
        if (!memberRoot) {
            result = ec::LoadOp::create(at, where, storage.type,
                                        lvalueOfGlobal(at, where, storage.binding));
        } else {
            result = ec::LiteralOp::create(at, where, storage.type, "&g_" + storage.binding);
        }
    } else {
        const auto member = "<&" + storage.className + "::" + storage.field + ">";
        if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
            result =
                callWithConstValueOperands(at, where, mlir::TypeRange{storage.fieldType},
                                           at.getStringAttr("ctnative::owned_global_get" + member),
                                           mlir::ValueRange{read.getObject()})
                    .getResult(0);
        } else {
            auto write = llvm::cast<ctjs::SetPropertyOp>(operation);
            callWithConstValueOperands(at, where, mlir::TypeRange{},
                                       at.getStringAttr("ctnative::owned_global_set" + member),
                                       mlir::ValueRange{write.getObject(), write.getValue()});
        }
    }
    if (result) { operation->getResult(0).replaceAllUsesWith(result); }
    ownedGlobalOperations.erase(found);
    eraseIfUnused(operation);
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
