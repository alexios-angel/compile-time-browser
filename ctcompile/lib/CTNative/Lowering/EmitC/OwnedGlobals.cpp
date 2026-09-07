#include "../../Analysis/OwnedGlobalRoots.h"
#include "Emitter.h"

namespace ctcompile::ctnative::lowering_detail {

void lowering::censusOwnedGlobals(const OwnedGlobalRoots & roots,
                                  llvm::ArrayRef<ctjs::FuncOp> accepted) {
    for (const auto & root : roots.roots()) {
        auto owner = root.owner;
        if (!llvm::is_contained(accepted, owner->getParentOfType<ctjs::FuncOp>())) { continue; }
        const std::string name = spelling(shapeAt(owner.getResult()));
        const auto type = ec::OpaqueType::get(context, "std::shared_ptr<" + name + ">");
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
}

bool lowering::replaceOwnedGlobal(mlir::Operation * operation) {
    const auto found = ownedGlobalOperations.find(operation);
    if (found == ownedGlobalOperations.end()) { return false; }
    const auto & storage = ownedGlobalStoragePlans[found->second];
    mlir::OpBuilder at(operation);
    const auto where = operation->getLoc();
    mlir::Value result;
    if (llvm::isa<ctjs::CreateObjectOp>(operation)) {
        result =
            callWithConstValueOperands(
                at, where, mlir::TypeRange{storage.type},
                at.getStringAttr("std::make_shared<" + storage.className + ">"), mlir::ValueRange{})
                .getResult(0);
    } else if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(operation)) {
        ec::AssignOp::create(at, where, lvalueOfGlobal(at, where, storage.binding),
                             store.getValue());
    } else if (llvm::isa<ctjs::LoadGlobalOp>(operation)) {
        result =
            ec::LoadOp::create(at, where, storage.type, lvalueOfGlobal(at, where, storage.binding));
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
