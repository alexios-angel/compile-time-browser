#include "Emitter.h"

namespace ctcompile::ctnative::lowering_detail {
namespace {

std::string callableSourceName(mlir::Value value) {
    std::string result;
    bool conflict = false;
    value.getLoc()->walk([&](mlir::Location location) -> mlir::WalkResult {
        auto fused = llvm::dyn_cast<mlir::FusedLoc>(location);
        if (!fused) { return mlir::WalkResult::advance(); }
        auto metadata = llvm::dyn_cast_or_null<mlir::DictionaryAttr>(fused.getMetadata());
        if (!metadata) { return mlir::WalkResult::advance(); }
        auto name = metadata.getAs<mlir::StringAttr>("ctnative.source_name");
        if (auto index = llvm::dyn_cast<mlir::OpResult>(value)) {
            if (auto array = metadata.getAs<mlir::ArrayAttr>("ctnative.source_names")) {
                if (index.getResultNumber() < array.size()) {
                    name = llvm::dyn_cast<mlir::StringAttr>(array[index.getResultNumber()]);
                }
            }
        }
        if (!name || name.empty()) { return mlir::WalkResult::advance(); }
        conflict |= !result.empty() && result != name.getValue();
        result = name.str();
        return mlir::WalkResult::advance();
    });
    return conflict ? std::string{} : result;
}

std::string callableLocal(llvm::StringRef prefix, llvm::StringRef hint, unsigned fallback,
                          llvm::StringSet<> & occupied) {
    // A generated prefix prevents keywords and standard-header macros from
    // shadowing C++ code. Encoding every non-alphanumeric byte also prevents
    // reserved double underscores; a suffix resolves sanitized collisions.
    std::string base = prefix.str();
    constexpr char hex[] = "0123456789abcdef";
    for (char ch : hint) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
            base += ch;
        } else {
            const auto byte = static_cast<unsigned char>(ch);
            base += 'u';
            base += hex[byte >> 4];
            base += hex[byte & 15];
        }
    }
    if (hint.empty()) { base += std::to_string(fallback); }
    std::string result = base;
    unsigned suffix = 2;
    while (!occupied.insert(result).second) { result = base + "_" + std::to_string(suffix++); }
    return result;
}

} // namespace

bool lowering::hasConcreteCallableSignature(ctjs::CreateClosureOp made) const {
    const auto supported = [](mlir::Type type) {
        switch (carrierOf(type)) {
        case carrier::nullable:
        case carrier::number:
        case carrier::boolean:
        case carrier::string:
        case carrier::objectValue:
        case carrier::objectIdentity:
        case carrier::map: return true;
        default: return false;
        }
    };
    auto fn = mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
        made, mlir::FlatSymbolRefAttr::get(context, environmentTarget(made)));
    if (!fn || fn.getBody().empty() || !supported(joinedReturnType(fn))) { return false; }
    for (mlir::Value capture : made.getUpvalues()) {
        if (!supported(typeOf(capture))) { return false; }
    }
    for (mlir::BlockArgument argument : fn.getBody().front().getArguments().drop_front(3)) {
        if (!supported(typeOf(argument))) { return false; }
    }
    return true;
}

std::string lowering::callableTypeSpelling(mlir::Type type) {
    switch (carrierOf(type)) {
    case carrier::nullable: needsNullable = true; return kNullableType.str();
    case carrier::number: return "js_num";
    case carrier::boolean: return "bool";
    case carrier::string: needsString = true; return "std::string";
    case carrier::objectValue: needsObjectValue = true; return kObjectValueType.str();
    case carrier::objectIdentity: needsObjectIdentity = true; return kObjectIdentityType.str();
    case carrier::map: {
        needsMap = true;
        const auto map = llvm::cast<MapType>(type);
        needsString |= mapNeedsString(map);
        needsObjectValue |= mapNeedsObjectValues(map);
        return llvm::cast<ec::OpaqueType>(mapCarrierType(map)).getValue().str();
    }
    default: llvm::report_fatal_error("stored callable has an unproved concrete signature");
    }
}

void lowering::censusStoredCallable(ctjs::CreateClosureOp made, bool namedLambda) {
    const auto target = environmentTarget(made);
    auto fn = mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(
        made, mlir::FlatSymbolRefAttr::get(context, target));
    const auto captures = made.getUpvalues().size();
    auto & entry = fn.getBody().front();
    const std::string result = callableTypeSpelling(joinedReturnType(fn));
    llvm::SmallVector<std::string> params;
    llvm::SmallVector<std::string> paramNames;
    llvm::SmallVector<std::string> captureNames;
    llvm::StringSet<> occupied;
    for (const auto & named : names) { occupied.insert(named.second); }
    for (auto [i, capture] : llvm::enumerate(made.getUpvalues())) {
        captureNames.push_back(namedLambda ? callableLocal("capture_", callableSourceName(capture),
                                                           static_cast<unsigned>(i), occupied)
                                           : "cap" + std::to_string(i));
    }
    for (unsigned i = 3 + static_cast<unsigned>(captures); i < entry.getNumArguments(); ++i) {
        params.push_back(callableTypeSpelling(typeOf(entry.getArgument(i))));
        const auto index = static_cast<unsigned>(paramNames.size());
        paramNames.push_back(namedLambda ? callableLocal("argument_",
                                                         callableSourceName(entry.getArgument(i)),
                                                         index, occupied)
                                         : "arg" + std::to_string(index));
    }
    const auto lambdaName = callableLocal("ctn_", "lambda", 0, occupied);
    const auto name = cIdentifier(target);
    std::string alias =
        "namespace ctnative {\nusing ctn_env_" + name + " = std::function<" + result + "(";
    for (auto [i, type] : llvm::enumerate(params)) {
        if (i) { alias += ", "; }
        alias += type;
    }
    environments.push_back(alias + ")>;\n}\n");
    if (namedLambda) {
        // Keep the body as IR until the final printer. Canonicalization,
        // deduction, const/constexpr analysis and source names must apply to
        // the actual lambda body, not to an earlier serialized copy.
        mlir::Builder attributes(context);
        const auto strings = [&](llvm::ArrayRef<std::string> values) {
            llvm::SmallVector<mlir::Attribute> array;
            for (const auto & value : values) { array.push_back(attributes.getStringAttr(value)); }
            return attributes.getArrayAttr(array);
        };
        callableBodies.try_emplace(
            names.lookup(target),
            attributes.getDictionaryAttr(
                {attributes.getNamedAttr("type",
                                         attributes.getStringAttr("ctnative::ctn_env_" + name)),
                 attributes.getNamedAttr("binder", attributes.getStringAttr("ctn_bind_" + name)),
                 attributes.getNamedAttr("name", attributes.getStringAttr(lambdaName)),
                 attributes.getNamedAttr("captures", strings(captureNames)),
                 attributes.getNamedAttr("parameters", strings(paramNames))}));
        return;
    }
    std::string builder = "// ctcompile: stored callable " + target.str() + ", " +
                          siteOfFunction(fn) + "\ninline ctnative::ctn_env_" + name + " ctn_bind_" +
                          name + "(";
    for (auto [i, capture] : llvm::enumerate(made.getUpvalues())) {
        if (i) { builder += ", "; }
        builder += callableTypeSpelling(typeOf(capture)) + " " + captureNames[i];
    }
    builder += ") {\n  return [";
    for (size_t i = 0; i < captures; ++i) {
        if (i) { builder += ", "; }
        const auto & slot = captureNames[i];
        builder += slot + " = std::move(" + slot + ")";
    }
    builder += "](";
    for (auto [i, type] : llvm::enumerate(params)) {
        if (i) { builder += ", "; }
        builder += type + " " + paramNames[i];
    }
    builder += ") -> " + result + " {\n    return " + names.lookup(target) + "(";
    for (size_t i = 0; i < captures; ++i) {
        if (i) { builder += ", "; }
        builder += captureNames[i];
    }
    for (size_t i = 0; i < params.size(); ++i) {
        if (captures || i) { builder += ", "; }
        builder += paramNames[i];
    }
    builder += ");\n  };\n";
    callableBuilders.push_back(builder + "}\n");
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
        auto invoked = callWithConstValueOperands(
            at, op->getLoc(), mlir::TypeRange{call.getResult().getType()},
            at.getStringAttr("ctnative::invoke_callable"), args);
        call.getResult().replaceAllUsesWith(invoked.getResult(0));
        eraseIfUnused(op);
        return true;
    }
    const auto site = methodTableName(op);
    if (site.empty()) { return false; }
    const auto name = "ctnative::method_" + cIdentifier(site);
    if (auto made = llvm::dyn_cast<ctjs::CreateObjectOp>(op)) {
        auto created = callWithConstValueOperands(
            at, op->getLoc(), mlir::TypeRange{made.getResult().getType()},
            at.getStringAttr("std::make_shared<" + name + ">"), mlir::ValueRange{});
        made.getResult().replaceAllUsesWith(created.getResult(0));
    } else {
        const auto key = op->getAttrOfType<mlir::StringAttr>(kNativeTableField).getValue();
        const auto member = "<&" + name + "::m_" + key.str() + ">";
        if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
            auto value = callWithConstValueOperands(
                at, op->getLoc(), mlir::TypeRange{get.getResult().getType()},
                at.getStringAttr("ctnative::method_get" + member),
                mlir::ValueRange{get.getObject()});
            get.getResult().replaceAllUsesWith(value.getResult(0));
        } else if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(op)) {
            callWithConstValueOperands(at, op->getLoc(), mlir::TypeRange{},
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
