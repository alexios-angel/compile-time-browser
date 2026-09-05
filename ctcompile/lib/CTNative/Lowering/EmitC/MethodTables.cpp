#include "Emitter.h"

namespace ctcompile::ctnative::lowering_detail {

std::string lowering::callableTypeSpelling(mlir::Type type) {
    switch (carrierOf(type)) {
    case carrier::nullable: needsNullable = true; return kNullableType.str();
    case carrier::number: return "double";
    case carrier::boolean: return "bool";
    case carrier::string: needsString = true; return "std::string";
    case carrier::objectIdentity: needsObjectIdentity = true; return kObjectIdentityType.str();
    case carrier::map: {
        needsMap = true;
        const auto map = llvm::cast<MapType>(type);
        needsString |= mapNeedsString(map);
        return llvm::cast<ec::OpaqueType>(mapCarrierType(map)).getValue().str();
    }
    default: llvm::report_fatal_error("stored callable has an unproved concrete signature");
    }
}

void lowering::censusStoredCallable(ctjs::CreateClosureOp made) {
    const auto target = environmentTarget(made);
    auto fn = mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
        made, mlir::FlatSymbolRefAttr::get(context, target));
    const auto captures = made.getUpvalues().size();
    auto & entry = fn.getBody().front();
    std::string result = "double";
    result = callableTypeSpelling(joinedReturnType(fn));
    llvm::SmallVector<std::string> params;
    for (unsigned i = 3 + static_cast<unsigned>(captures); i < entry.getNumArguments(); ++i) {
        params.push_back(callableTypeSpelling(typeOf(entry.getArgument(i))));
    }
    const auto name = cIdentifier(target);
    std::string alias =
        "namespace ctnative {\nusing ctn_env_" + name + " = std::function<" + result + "(";
    for (auto [i, type] : llvm::enumerate(params)) {
        if (i) { alias += ", "; }
        alias += type;
    }
    environments.push_back(alias + ")>;\n}\n");
    std::string builder = "// ctcompile: stored callable " + target.str() + ", " +
                          siteOfFunction(fn) + "\ninline ctnative::ctn_env_" + name + " ctn_bind_" +
                          name + "(";
    for (auto [i, capture] : llvm::enumerate(made.getUpvalues())) {
        if (i) { builder += ", "; }
        builder += callableTypeSpelling(typeOf(capture)) + " cap" + std::to_string(i);
    }
    builder += ") {\n  return [";
    for (size_t i = 0; i < captures; ++i) {
        if (i) { builder += ", "; }
        const auto slot = "cap" + std::to_string(i);
        builder += slot + " = std::move(" + slot + ")";
    }
    builder += "](";
    for (auto [i, type] : llvm::enumerate(params)) {
        if (i) { builder += ", "; }
        builder += type + " arg" + std::to_string(i);
    }
    builder += ") -> " + result + " {\n    return " + names.lookup(target) + "(";
    for (size_t i = 0; i < captures; ++i) {
        if (i) { builder += ", "; }
        builder += "cap" + std::to_string(i);
    }
    for (size_t i = 0; i < params.size(); ++i) {
        if (captures || i) { builder += ", "; }
        builder += "arg" + std::to_string(i);
    }
    callableBuilders.push_back(builder + ");\n  };\n}\n");
}

void lowering::censusMethodTables(llvm::ArrayRef<ctjs::FuncOp> accepted) {
    for (ctjs::FuncOp fn : accepted) {
        fn.getBody().walk([&](ctjs::CreateObjectOp made) {
            const auto site = methodTableName(made);
            if (site.empty()) { return; }
            std::vector<std::pair<std::string, std::string>> fields;
            for (mlir::Operation * user : made.getResult().getUsers()) {
                auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
                if (!set || methodTableName(set) != site) { continue; }
                const auto key = set->getAttrOfType<mlir::StringAttr>(kNativeTableField).getValue();
                const auto target = llvm::cast<ClosureType>(typeOf(set.getValue())).getTarget();
                fields.emplace_back(key.str(), cIdentifier(target));
            }
            llvm::sort(fields);
            std::string definition = "namespace ctnative {\n// ctcompile: returned method table, " +
                                     siteOf(made.getLoc()) + "\nstruct method_" +
                                     cIdentifier(site) + " {\n";
            for (const auto & [key, target] : fields) {
                definition += "  ctn_env_" + target + " m_" + key + ";\n";
            }
            methodTables.push_back(definition + "};\n}\n");
        });
    }
}

bool lowering::replaceMethodTable(mlir::Operation * op) {
    mlir::OpBuilder at(op);
    if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(op);
        call && op->hasAttr(kNativeStoredCall)) {
        const auto captures = op->getAttrOfType<mlir::IntegerAttr>(kNativeStoredCall).getInt();
        llvm::SmallVector<mlir::Value> args{call.getCalleeValue()};
        llvm::append_range(args, op->getOperands().drop_front(3 + static_cast<size_t>(captures)));
        auto invoked =
            ec::CallOpaqueOp::create(at, op->getLoc(), mlir::TypeRange{call.getResult().getType()},
                                     at.getStringAttr("ctnative::invoke_callable"), args);
        call.getResult().replaceAllUsesWith(invoked.getResult(0));
        eraseIfUnused(op);
        return true;
    }
    const auto site = methodTableName(op);
    if (site.empty()) { return false; }
    const auto name = "ctnative::method_" + cIdentifier(site);
    if (auto made = llvm::dyn_cast<ctjs::CreateObjectOp>(op)) {
        auto created = ec::CallOpaqueOp::create(
            at, op->getLoc(), mlir::TypeRange{made.getResult().getType()},
            at.getStringAttr("std::make_shared<" + name + ">"), mlir::ValueRange{});
        made.getResult().replaceAllUsesWith(created.getResult(0));
    } else {
        const auto key = op->getAttrOfType<mlir::StringAttr>(kNativeTableField).getValue();
        const auto member = "<&" + name + "::m_" + key.str() + ">";
        if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
            auto value = ec::CallOpaqueOp::create(at, op->getLoc(),
                                                  mlir::TypeRange{get.getResult().getType()},
                                                  at.getStringAttr("ctnative::method_get" + member),
                                                  mlir::ValueRange{get.getObject()});
            get.getResult().replaceAllUsesWith(value.getResult(0));
        } else if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(op)) {
            ec::CallOpaqueOp::create(at, op->getLoc(), mlir::TypeRange{},
                                     at.getStringAttr("ctnative::method_set" + member),
                                     mlir::ValueRange{set.getObject(), set.getValue()});
        } else {
            return false;
        }
    }
    eraseIfUnused(op);
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
