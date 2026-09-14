#include "../../Analysis/OwnedGlobalRoots.h"
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
        case carrier::nullableString:
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
    case carrier::nullableString: needsNullableString = true; return kNullableStringType.str();
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

bool lowering::censusSession(const OwnedGlobalRoots & roots,
                             llvm::ArrayRef<ctjs::FuncOp> accepted) {
    if (!roots.proved()) { return false; }
    for (const auto & root : roots.roots()) {
        if (!root.methodTable) { continue; }
        const auto & table = *root.methodTable;
        const auto site = methodTableName(table.table);
        if (!table.capturedMap || site.empty() || !llvm::is_contained(accepted, table.factory)) {
            return false;
        }
        sessionTables.insert(site);
        for (const auto & method : table.methods) {
            if (!llvm::is_contained(accepted, method.function)) { return false; }
            auto closure = method.closure;
            sessionTargets.try_emplace(environmentTarget(closure),
                                       static_cast<unsigned>(closure.getUpvalues().size()));
        }
        auto allocation = table.capturedMap->allocation;
        auto map = llvm::dyn_cast_or_null<MapType>(typeOf(allocation.getResult()));
        if (map && llvm::isa<MapType>(map.getValueType())) {
            auto object = table.table;
            if (allocation->getBlock() != object->getBlock()) { return false; }
            const auto mapName = "ctnative::map_storage<" + mapKeySpelling(map.getKeyType()).str() +
                                 ", " + mapValueSpelling(map.getValueType()) + ">";
            const auto borrowed = ec::PointerType::get(ec::OpaqueType::get(context, mapName));
            const auto index = static_cast<unsigned>(sessionMaps.size());
            sessionMaps.push_back({"ctnative::method_" + cIdentifier(site), mapName, borrowed, {}});
            sessionAllocations[allocation] = index;
            sessionAllocations[object] = index;
            llvm::SmallVector<mlir::Value> aliases{allocation.getResult()};
            for (const auto & method : table.methods) {
                auto closure = method.closure;
                auto function = method.function;
                if (closure.getUpvalues().size() != 1 ||
                    closure.getUpvalues().front() != allocation.getResult() ||
                    function.getBody().front().getNumArguments() < 4) {
                    return false;
                }
                sessionMapTargets.insert(environmentTarget(closure));
                aliases.push_back(function.getBody().front().getArgument(3));
            }
            for (ctjs::LoadUpvalueOp read : table.capturedMap->upvalues) {
                aliases.push_back(read.getResult());
            }
            // Only this live captured identity borrows the member. Child Maps
            // retain their owners: saved children can outlive parent deletion.
            for (size_t i = 0; i < aliases.size(); ++i) {
                const auto value = aliases[i];
                if (!ownedObjectTypes.try_emplace(value, borrowed).second) { continue; }
                for (mlir::Operation * user : value.getUsers()) {
                    auto call = llvm::dyn_cast<ctjs::CallOp>(user);
                    if (call && call.getReceiver() == value && nativeMapAction(call) == "set" &&
                        llvm::is_contained(table.capturedMap->calls, call)) {
                        aliases.push_back(call.getResult());
                    }
                }
            }
        }
        for (const auto & edge : table.calls) {
            auto call = llvm::dyn_cast<ctjs::CallDirectOp>(edge.call);
            auto read = edge.read;
            if (!call || call.getCalleeValue() != read.getResult() ||
                !sessionTargets.contains(call.getCallee()) ||
                !llvm::is_contained(accepted, call->getParentOfType<ctjs::FuncOp>())) {
                return false;
            }
            const auto key = ctjs::constantKey(read.getKey());
            sessionCalls[call] = "ctnative::invoke_session<&ctnative::method_" + cIdentifier(site) +
                                 "::m_" + key.str() + ">";
            // The source proof admits this read only as the exact call's callee.
            // Its emitted value is the table receiver, never an extracted callable.
            ownedObjectTypes[read.getResult()] =
                methodTableCarrierType(llvm::cast<MethodTableType>(typeOf(read.getObject())));
        }
    }
    return !sessionTables.empty();
}

void lowering::censusMethodTables(llvm::ArrayRef<ctjs::FuncOp> accepted) {
    for (ctjs::FuncOp fn : accepted) {
        fn.getBody().walk([&](ctjs::CreateObjectOp made) {
            const auto site = methodTableName(made);
            if (site.empty()) { return; }
            const bool session = sessionTables.contains(site);
            std::vector<std::pair<std::string, std::string>> fields;
            for (mlir::Operation * user : made.getResult().getUsers()) {
                auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
                if (!set || methodTableName(set) != site) { continue; }
                const auto key = set->getAttrOfType<mlir::StringAttr>(kNativeTableField).getValue();
                const auto target = llvm::cast<ClosureType>(typeOf(set.getValue())).getTarget();
                fields.emplace_back(key.str(), target.str());
            }
            llvm::sort(fields);
            std::string definition = "namespace ctnative {\n// ctcompile: returned method table, " +
                                     siteOf(made.getLoc()) + "\nstruct method_" +
                                     cIdentifier(site) + " {\n";
            if (session) {
                const auto name = "method_" + cIdentifier(site);
                definition += "  " + name + "() = default;\n  " + name + "(const " + name +
                              " &) = delete;\n  " + name + " & operator=(const " + name +
                              " &) = delete;\n  " + name + "(" + name + " &&) = delete;\n  " +
                              name + " & operator=(" + name + " &&) = delete;\n";
                if (auto found = sessionAllocations.find(made); found != sessionAllocations.end()) {
                    const auto & storage = sessionMaps[found->second];
                    definition +=
                        "private:\n  " + storage.mapName +
                        " captured_map;\npublic:\n  // ctcompile: borrow the session-owned "
                        "outer Map\n  " +
                        storage.mapName + " * capture_map() { return &captured_map; }\n";
                }
            }
            for (const auto & [key, target] : fields) {
                const auto environment = "ctn_env_" + cIdentifier(target);
                if (!session) {
                    definition += "  " + environment + " m_" + key + ";\n";
                    continue;
                }
                auto function = module.lookupSymbol<ctjs::FuncOp>(target);
                const auto captures = sessionTargets.lookup(target);
                const auto result = callableTypeSpelling(joinedReturnType(function));
                std::string parameters, arguments;
                for (auto [index, argument] : llvm::enumerate(
                         function.getBody().front().getArguments().drop_front(3 + captures))) {
                    if (index) { parameters += ", "; }
                    const auto name = "arg" + std::to_string(index);
                    parameters += callableTypeSpelling(typeOf(argument)) + " " + name;
                    if (!arguments.empty()) { arguments += ", "; }
                    arguments += name;
                }
                const std::string provenance = target + ", " + siteOfFunction(function);
                const bool memberMap = sessionMapTargets.contains(target);
                if (!memberMap) {
                    definition += "private:\n  " + environment + " capture_" + key +
                                  ";\npublic:\n  // ctcompile: initialize capture for " + target +
                                  "\n  void initialize_" + key + "(" + environment +
                                  " value) { capture_" + key + " = std::move(value); }\n";
                }
                definition += "  " + result + " m_" + key + "(" + parameters + ");\n";
                std::string body = "// ctcompile: session method " + provenance + "\ninline " +
                                   result + " ctnative::method_" + cIdentifier(site) + "::m_" +
                                   key + "(" + parameters + ") {\n  return " +
                                   names.lookup(target) + "(";
                for (unsigned index = 0; index < captures; ++index) {
                    if (index) { body += ", "; }
                    body += memberMap
                                ? "&captured_map"
                                : "std::get<" + std::to_string(index) + ">(capture_" + key + ")";
                }
                if (captures && !arguments.empty()) { body += ", "; }
                callableBuilders.push_back(body + arguments + ");\n}\n");
            }
            methodTables.push_back(definition + "};\n}\n");
        });
    }
}

bool lowering::replaceSessionAllocation(mlir::Operation * operation) {
    const auto found = sessionAllocations.find(operation);
    if (found == sessionAllocations.end()) { return false; }
    auto & storage = sessionMaps[found->second];
    mlir::OpBuilder at(operation);
    const auto where = operation->getLoc();
    if (!storage.owner) {
        // The complete factory proof excludes callbacks and publication here.
        // Construct once at the earlier source allocation, in either order.
        storage.owner =
            callWithConstValueOperands(
                at, where,
                mlir::TypeRange{
                    ec::OpaqueType::get(context, "std::shared_ptr<" + storage.tableName + ">")},
                at.getStringAttr("std::make_shared<" + storage.tableName + ">"), mlir::ValueRange{})
                .getResult(0);
    }
    mlir::Value value = storage.owner;
    if (llvm::isa<ctjs::ConstructOp>(operation)) {
        if (llvm::all_of(operation->getResult(0).getUsers(), [](mlir::Operation * user) {
                return user->hasAttr(kNativeStoredRead) || llvm::isa<ctjs::RootOp>(user);
            })) {
            // The Map was constructed with its table above. Only erased
            // closures borrow it here; no pointer temporary is needed.
            operation->setAttr(kNativeStoredRead, mlir::UnitAttr::get(context));
            sessionAllocations.erase(found);
            return true;
        }
        value = callWithConstValueOperands(at, where, mlir::TypeRange{storage.borrowedType},
                                           at.getStringAttr("ctnative::invoke_session<&" +
                                                            storage.tableName + "::capture_map>"),
                                           mlir::ValueRange{storage.owner})
                    .getResult(0);
    }
    operation->getResult(0).replaceAllUsesWith(value);
    sessionAllocations.erase(found);
    eraseIfUnused(operation);
    return true;
}

bool lowering::replaceMethodTable(mlir::Operation * op) {
    if (replaceSessionAllocation(op)) { return true; }
    mlir::OpBuilder at(op);
    if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(op);
        call && op->hasAttr(kNativeStoredCall)) {
        const auto captures = op->getAttrOfType<mlir::IntegerAttr>(kNativeStoredCall).getInt();
        llvm::SmallVector<mlir::Value> args{call.getCalleeValue()};
        llvm::append_range(args, op->getOperands().drop_front(3 + static_cast<size_t>(captures)));
        const auto session = sessionCalls.find(op);
        auto invoked = callWithConstValueOperands(
            at, op->getLoc(), mlir::TypeRange{call.getResult().getType()},
            at.getStringAttr(session == sessionCalls.end() ? "ctnative::invoke_callable"
                                                           : session->second),
            args);
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
            if (sessionTables.contains(site)) {
                get.getResult().replaceAllUsesWith(get.getObject());
                eraseIfUnused(op);
                return true;
            }
            auto value = callWithConstValueOperands(
                at, op->getLoc(), mlir::TypeRange{get.getResult().getType()},
                at.getStringAttr("ctnative::method_get" + member),
                mlir::ValueRange{get.getObject()});
            get.getResult().replaceAllUsesWith(value.getResult(0));
        } else if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(op)) {
            auto closure = set.getValue().getDefiningOp<ctjs::CreateClosureOp>();
            if (closure && sessionTables.contains(site) &&
                sessionMapTargets.contains(environmentTarget(closure))) {
                eraseIfUnused(op);
                return true;
            }
            const auto helper = sessionTables.contains(site) ? "ctnative::invoke_session<&" + name +
                                                                   "::initialize_" + key.str() + ">"
                                                             : "ctnative::method_set" + member;
            callWithConstValueOperands(at, op->getLoc(), mlir::TypeRange{},
                                       at.getStringAttr(helper),
                                       mlir::ValueRange{set.getObject(), set.getValue()});
        } else {
            return false;
        }
    }
    eraseIfUnused(op);
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
