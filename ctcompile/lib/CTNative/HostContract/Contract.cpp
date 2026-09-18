#include "Analysis.h"
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Analysis/HostContract.h"

#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/AsmState.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"

#include <limits>

namespace ctcompile::ctnative {
namespace {

llvm::Error error(llvm::StringRef message) {
    return llvm::createStringError(llvm::inconvertibleErrorCode(), message);
}

llvm::Error names(const llvm::json::Object & object, llvm::StringRef field,
                  std::vector<std::string> & output, bool allowEmpty) {
    const auto * values = object.getArray(field);
    if (values == nullptr || (!allowEmpty && values->empty())) {
        return error(("host contract requires array `" + field + "`").str());
    }
    llvm::StringSet<> seen;
    for (const auto & value : *values) {
        const auto name = value.getAsString();
        if (!name || name->empty() || !seen.insert(*name).second) {
            return error(("host contract `" + field + "` needs distinct nonempty strings").str());
        }
        output.push_back(name->str());
    }
    return llvm::Error::success();
}

bool keys(const llvm::json::Object & object, llvm::ArrayRef<llvm::StringRef> allowed) {
    for (const auto & member : object) {
        if (!llvm::is_contained(allowed, member.first.str())) { return false; }
    }
    return true;
}

std::string realmReceiverProblem(const HostContract & contract) {
    if (!contract.classicScriptRealm && !contract.realmOwnDataProperties.empty()) {
        return "realm own-data properties require the classic-script-realm entry receiver";
    }
    llvm::StringSet<> seen;
    for (const auto & name : contract.realmOwnDataProperties) {
        if (!ctjs::ordinaryKey(name) || !seen.insert(name).second) {
            return "realm own-data properties require distinct ordinary property names";
        }
        if (llvm::is_contained(contract.absentBindings, name) ||
            llvm::is_contained(contract.undefinedBindings, name) ||
            llvm::is_contained(contract.initialIntrinsics, name)) {
            return "realm writable property conflicts with a fixed host binding";
        }
    }
    return {};
}

} // namespace

llvm::Expected<HostContract> parseHostContract(llvm::StringRef text) {
    auto parsed = llvm::json::parse(text);
    if (!parsed) { return parsed.takeError(); }
    const auto * object = parsed->getAsObject();
    if (object == nullptr) {
        return error("host contract must be an object with only supported fields");
    }
    if (object->getInteger("version") != 1) { return error("unsupported host contract version"); }
    const bool domSession = object->getString("provider") == "ctbrowser-dom-session-v1";
    const bool dom = domSession || object->getString("provider") == "ctbrowser-dom-v1";
    const bool domData = object->getString("provider") == "ctbrowser-dom-data-session-v1";
    const bool session = object->getString("provider") == "closed-source-session-v1";
    if (!dom && !domData && !session && object->getString("provider") != "closed-source-v1") {
        return error("unsupported host provider; expected closed-source-v1, "
                     "closed-source-session-v1, ctbrowser-dom-v1, ctbrowser-dom-session-v1 or "
                     "ctbrowser-dom-data-session-v1");
    }
    if (dom ? !keys(*object, {"version", "provider", "module_sha256", "entry", "element_parameters",
                              "initial_intrinsics", "dataset_parameters"})
            : !keys(*object,
                    {"version", "provider", "module_sha256", "entry", "roots", "observations",
                     "absent_bindings", "undefined_bindings", "initial_intrinsics",
                     "realm_global_this", "entry_receiver", "element_parameters"}) ||
                  (!dom && !domData && object->get("element_parameters"))) {
        return error("host contract must be an object with only supported fields");
    }
    const auto digest = object->getString("module_sha256");
    const auto entry = object->getString("entry");
    const auto * roots = object->getArray("roots");
    if (!digest || digest->size() != 64 ||
        !llvm::all_of(*digest,
                      [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) ||
        !entry || entry->empty() || (!dom && (roots == nullptr || roots->empty()))) {
        return error("host contract requires a lowercase SHA-256, entry symbol and roots");
    }
    HostContract result;
    if (session) { result.provider = HostContract::Provider::closedSourceSession; }
    result.moduleSha256 = digest->str();
    result.entry = entry->str();
    if (dom || domData) {
        result.provider = domData      ? HostContract::Provider::ctbrowserDOMDataSession
                          : domSession ? HostContract::Provider::ctbrowserDOMSession
                                       : HostContract::Provider::ctbrowserDOM;
        const auto * parameters = object->getArray("element_parameters");
        if (!parameters || parameters->empty()) {
            return error("DOM entry requires nonempty element_parameters");
        }
        for (const auto & value : *parameters) {
            const auto index = value.getAsInteger();
            if (!index || *index < 0 ||
                static_cast<std::uint64_t>(*index) > std::numeric_limits<unsigned>::max() ||
                static_cast<std::uint64_t>(*index) != result.elementParameters.size()) {
                return error("DOM element_parameters must list every explicit parameter in order");
            }
            result.elementParameters.push_back(static_cast<unsigned>(*index));
        }
        if (dom) {
            if (const auto * requested = object->get("dataset_parameters")) {
                const auto * indices = requested->getAsArray();
                if (!indices) { return error("DOM dataset_parameters must be an index array"); }
                for (const auto & value : *indices) {
                    const auto index = value.getAsInteger();
                    if (!index || *index < 0 ||
                        static_cast<std::uint64_t>(*index) >= result.elementParameters.size() ||
                        (!result.datasetParameters.empty() &&
                         static_cast<std::uint64_t>(*index) <= result.datasetParameters.back())) {
                        return error("DOM dataset_parameters must be an ordered element subset");
                    }
                    result.datasetParameters.push_back(static_cast<unsigned>(*index));
                }
            }
            if (object->get("initial_intrinsics")) {
                if (auto failure =
                        names(*object, "initial_intrinsics", result.initialIntrinsics, true)) {
                    return std::move(failure);
                }
                // Class preparation proves and consumes these identities before
                // the ordinary typed DOM proof, including unused throwing getters.
                const bool classOnly =
                    llvm::is_contained(result.initialIntrinsics,
                                       host_detail::classDefinedIntrinsic) &&
                    llvm::all_of(result.initialIntrinsics, [](const auto & name) {
                        return name == host_detail::classDefinedIntrinsic || name == "Error";
                    });
                if (!classOnly && llvm::any_of(result.initialIntrinsics, [](const auto & name) {
                        return name != "Object" && name != "Number" &&
                               name != "decodeURIComponent" && name != "JSON" && name != "Array" &&
                               name != "String" && name != "RegExp" &&
                               name != "__ctbrowser_regexp" && name != "__ctbrowser_for_of_open" &&
                               name != "__ctbrowser_iter_next" && name != "__ctbrowser_iter_close";
                    })) {
                    return error("unsupported DOM initial intrinsic identity");
                }
            }
            return result;
        }
    }
    if (auto failure = names(*object, "observations", result.observations, false)) {
        return std::move(failure);
    }
    if (auto failure = names(*object, "absent_bindings", result.absentBindings, true)) {
        return std::move(failure);
    }
    if (auto failure = names(*object, "undefined_bindings", result.undefinedBindings, true)) {
        return std::move(failure);
    }
    if (object->get("initial_intrinsics")) {
        if (auto failure = names(*object, "initial_intrinsics", result.initialIntrinsics, true)) {
            return std::move(failure);
        }
        for (const auto & name : result.initialIntrinsics) {
            if (name != "Map" && name != "Array" && name != "Error" &&
                name != host_detail::classDefinedIntrinsic) {
                return error("unsupported initial intrinsic identity");
            }
            if (llvm::is_contained(result.absentBindings, name) ||
                llvm::is_contained(result.undefinedBindings, name)) {
                return error("an initial intrinsic must be present with its standard value");
            }
        }
    }
    if (object->get("realm_global_this")) {
        auto realm = object->getBoolean("realm_global_this");
        if (!realm) { return error("realm_global_this must be a boolean"); }
        result.realmGlobalThis = *realm;
        if (*realm && (llvm::is_contained(result.absentBindings, "globalThis") ||
                       llvm::is_contained(result.undefinedBindings, "globalThis"))) {
            return error("the realm globalThis binding cannot be absent or undefined");
        }
    }
    if (object->get("entry_receiver")) {
        const auto * receiver = object->getObject("entry_receiver");
        if (!receiver || !keys(*receiver, {"kind", "own_data_properties"}) ||
            receiver->getString("kind") != "classic-script-realm") {
            return error(
                "entry_receiver requires kind classic-script-realm and own_data_properties");
        }
        result.classicScriptRealm = true;
        if (auto failure =
                names(*receiver, "own_data_properties", result.realmOwnDataProperties, true)) {
            return std::move(failure);
        }
    }
    if (auto problem = realmReceiverProblem(result); !problem.empty()) { return error(problem); }
    for (const std::string & name : result.undefinedBindings) {
        if (llvm::is_contained(result.absentBindings, name)) {
            return error("a host binding cannot be both absent and present undefined");
        }
    }
    llvm::StringSet<> bindings;
    for (const auto & value : *roots) {
        const auto * root = value.getAsObject();
        if (root == nullptr || !keys(*root, {"binding", "properties"})) {
            return error("host roots require only binding and properties");
        }
        const auto binding = root->getString("binding");
        if (!binding || binding->empty() || !bindings.insert(*binding).second ||
            llvm::is_contained(result.absentBindings, *binding) ||
            llvm::is_contained(result.undefinedBindings, *binding)) {
            return error("host roots need distinct present bindings");
        }
        HostRootRequest request{binding->str(), {}};
        if (auto failure = names(*root, "properties", request.properties, false)) {
            return std::move(failure);
        }
        result.roots.push_back(std::move(request));
    }
    return result;
}

void removeAttrsWithPrefix(mlir::Operation * op, llvm::StringRef prefix) {
    llvm::SmallVector<mlir::StringAttr> names;
    for (mlir::NamedAttribute attribute : op->getAttrs()) {
        if (attribute.getName().getValue().starts_with(prefix)) {
            names.push_back(attribute.getName());
        }
    }
    for (mlir::StringAttr name : names) { op->removeAttr(name); }
}

void clearHostContractReports(mlir::ModuleOp module) {
    module.walk([](mlir::Operation * op) { removeAttrsWithPrefix(op, "ctnative.host_"); });
}

std::string hostContractFingerprint(mlir::ModuleOp module) {
    const auto reports = module.walk<mlir::WalkOrder::PreOrder>([](mlir::Operation * operation) {
        for (mlir::NamedAttribute attribute : operation->getAttrs()) {
            if (attribute.getName().getValue().starts_with("ctnative.host_")) {
                return mlir::WalkResult::interrupt();
            }
        }
        return mlir::WalkResult::advance();
    });
    // Report-free source needs no copy. Each call still hashes the complete current IR.
    mlir::OwningOpRef<mlir::ModuleOp> copy;
    if (reports.wasInterrupted()) {
        copy = module.clone();
        clearHostContractReports(*copy);
        module = *copy;
    }
    std::string text;
    llvm::raw_string_ostream stream(text);
    module.print(stream, mlir::OpPrintingFlags().printGenericOpForm().enableDebugInfo(false));
    const auto digest = llvm::SHA256::hash(llvm::ArrayRef<std::uint8_t>(
        reinterpret_cast<const std::uint8_t *>(text.data()), text.size()));
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (const std::uint8_t byte : digest) {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 15]);
    }
    return result;
}

} // namespace ctcompile::ctnative

namespace ctcompile::ctnative::host_detail {

// Only this complete local declaration may be omitted by a native entry provider.
// The caller must also exclude every source reference to the published binding.
bool isInertEntryDeclaration(ctjs::FuncOp wrapper, ctjs::FuncOp target,
                             llvm::function_ref<bool()> spend) {
    if (!spend() || wrapper == target || functionIndex(wrapper) != 0 ||
        !llvm::hasSingleElement(wrapper.getBody()) || wrapper.getUpvalueCount() != 0 ||
        wrapper->hasAttr("ctjs.skipped") || wrapper->getParentOp() != target->getParentOp()) {
        return false;
    }
    auto & block = wrapper.getBody().front();
    const auto index = functionIndex(target);
    if (!index || *index == 0 || block.getNumArguments() != ctjs::implicit_arguments) {
        return false;
    }
    llvm::DenseSet<mlir::Value> values, undefined;
    for (mlir::BlockArgument argument : block.getArguments()) {
        if (!spend() || !llvm::isa<ctjs::ValueType>(argument.getType())) { return false; }
        values.insert(argument);
    }
    mlir::Value frame;
    ctjs::CreateClosureOp closure;
    bool entered = false, published = false, returned = false;
    for (mlir::Operation & operation : block) {
        if (!spend() || operation.getNumRegions() || operation.getNumSuccessors() || returned) {
            return false;
        }
        for (mlir::Value operand : operation.getOperands()) {
            if (!spend() || (!values.contains(operand) && operand != frame)) { return false; }
        }
        if (auto constant = llvm::dyn_cast<ctjs::ConstantOp>(operation)) {
            if (!llvm::isa<ctjs::StringAttr, ctjs::BooleanAttr, ctjs::UndefinedAttr>(
                    constant.getValue())) {
                return false;
            }
            values.insert(constant.getResult());
            if (llvm::isa<ctjs::UndefinedAttr>(constant.getValue())) {
                undefined.insert(constant.getResult());
            }
        } else if (auto enter = llvm::dyn_cast<ctjs::FrameEnterOp>(operation)) {
            if (entered) { return false; }
            entered = true;
            frame = enter.getContext();
        } else if (auto root = llvm::dyn_cast<ctjs::RootOp>(operation)) {
            if (!frame || root.getContext() != frame) { return false; }
        } else if (auto exit = llvm::dyn_cast<ctjs::FrameExitOp>(operation)) {
            if (!frame || exit.getContext() != frame) { return false; }
            frame = {};
        } else if (auto result = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
            if (frame || !undefined.contains(result.getValue())) { return false; }
            returned = true;
        } else if (auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(operation)) {
            if (closure || made.getFunction() < 0 ||
                static_cast<unsigned>(made.getFunction()) != *index ||
                made.getEnclosingClosure() != block.getArgument(ctjs::arg_callee) ||
                (made.getEnclosingThis() != block.getArgument(ctjs::arg_receiver) &&
                 !undefined.contains(made.getEnclosingThis())) ||
                !made.getUpvalues().empty()) {
                return false;
            }
            closure = made;
            values.insert(made.getResult());
        } else if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(operation)) {
            if (published || !closure || store.getValue() != closure.getResult() ||
                store.getName() == "undefined" ||
                store.getName() != target.getSymName().rsplit('$').first) {
                return false;
            }
            published = true;
        } else {
            return false;
        }
    }
    return returned && closure && published;
}

std::string initialBindingProblem(mlir::ModuleOp module, const HostContract & contract) {
    if ((contract.provider != HostContract::Provider::closedSource &&
         contract.provider != HostContract::Provider::closedSourceSession &&
         contract.provider != HostContract::Provider::ctbrowserDOMDataSession) ||
        (contract.provider != HostContract::Provider::ctbrowserDOMDataSession &&
         !contract.elementParameters.empty())) {
        return "closed-source analysis requires a closed-source host provider";
    }
    std::string reason = realmReceiverProblem(contract);
    if (!reason.empty()) { return reason; }
    if (auto metadata = module->getAttr("ctjs.hoisted_vars")) {
        auto declarations = llvm::dyn_cast<mlir::ArrayAttr>(metadata);
        llvm::StringSet<> names;
        if (!declarations) {
            return "hoisted global declarations require an array of distinct nonempty names";
        }
        for (mlir::Attribute declaration : declarations) {
            auto name = llvm::dyn_cast<mlir::StringAttr>(declaration);
            if (!name || name.getValue().empty() || !names.insert(name.getValue()).second) {
                return "hoisted global declarations require an array of distinct nonempty names";
            }
            if (llvm::is_contained(contract.absentBindings, name.getValue())) {
                return "source declares a fixed absent host binding";
            }
        }
    }
    if (contract.realmGlobalThis &&
        (llvm::is_contained(contract.absentBindings, "globalThis") ||
         llvm::is_contained(contract.undefinedBindings, "globalThis"))) {
        return "invalid initial realm globalThis declaration";
    }
    for (const auto & name : contract.initialIntrinsics) {
        if ((name != "Map" && name != "Array" && name != "Error" &&
             name != classDefinedIntrinsic) ||
            llvm::is_contained(contract.absentBindings, name) ||
            llvm::is_contained(contract.undefinedBindings, name)) {
            return "invalid standard initial intrinsic declaration";
        }
    }
    module.walk([&](mlir::Operation * operation) {
        if (!reason.empty()) { return; }
        if (!contract.realmOwnDataProperties.empty()) {
            // This narrow contract provides no descriptor/prototype mutation
            // model. Refuse possible changes anywhere, including a suffix or
            // alias not reached by prefix interpretation.
            mlir::Value key;
            if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) { key = read.getKey(); }
            if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(operation)) {
                key = write.getKey();
            }
            const auto name = key ? ctjs::constantKey(key) : llvm::StringRef{};
            if (llvm::isa<ctjs::DefineAccessorOp, ctjs::DeletePropertyOp, ctjs::DeleteNamedOp>(
                    operation) ||
                name == "__proto__" || name == "prototype" || name == "defineProperty" ||
                name == "defineProperties" || name == "setPrototypeOf" ||
                name == "__defineGetter__" || name == "__defineSetter__") {
                reason = "source can change contracted realm property descriptors or prototype";
                return;
            }
        }
        if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(operation)) {
            if (llvm::is_contained(contract.initialIntrinsics, store.getName())) {
                reason = "declared intrinsic binding is replaced by source";
            }
        }
        if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(operation)) {
            if (llvm::is_contained(contract.initialIntrinsics, ctjs::constantKey(write.getKey()))) {
                reason = "source property write can replace a declared intrinsic binding";
            }
        }
        auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation);
        if (!load || !llvm::is_contained(contract.initialIntrinsics, load.getName())) { return; }
        for (mlir::OpOperand & use : load.getResult().getUses()) {
            if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
            if (load.getName() == "Map") {
                auto construct = llvm::dyn_cast<ctjs::ConstructOp>(use.getOwner());
                if (construct && use.getOperandNumber() < 2 &&
                    construct.getCallee() == load.getResult() &&
                    construct.getNewTarget() == load.getResult() && construct.getArgs().empty()) {
                    continue;
                }
            } else if (load.getName() == "Error") {
                auto construct = llvm::dyn_cast<ctjs::ConstructOp>(use.getOwner());
                auto message = construct && construct.getArgs().size() == 1
                                   ? construct.getArgs().front().getDefiningOp<ctjs::ConstantOp>()
                                   : ctjs::ConstantOp{};
                if (construct && use.getOperandNumber() < 2 &&
                    construct.getCallee() == load.getResult() &&
                    construct.getNewTarget() == load.getResult() && message &&
                    llvm::isa<ctjs::StringAttr>(message.getValue())) {
                    continue;
                }
            } else if (load.getName() == classDefinedIntrinsic) {
                auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                auto receiver = call ? call.getReceiver().getDefiningOp<ctjs::ConstantOp>()
                                     : ctjs::ConstantOp{};
                if (call && use.getOperandNumber() == 0 && call.getArgs().size() == 1 && receiver &&
                    llvm::isa<ctjs::UndefinedAttr>(receiver.getValue()) &&
                    call.getArgs().front().getDefiningOp<ctjs::CreateClosureOp>()) {
                    continue;
                }
            } else if (load.getName() == "Array") {
                if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(use.getOwner());
                    read && use.getOperandNumber() == 0 &&
                    ctjs::constantKey(read.getKey()) == "from") {
                    bool closed = true;
                    for (mlir::OpOperand & methodUse : read.getResult().getUses()) {
                        if (llvm::isa<ctjs::RootOp>(methodUse.getOwner())) { continue; }
                        auto call = llvm::dyn_cast<ctjs::CallOp>(methodUse.getOwner());
                        closed &= call && methodUse.getOperandNumber() == 0 &&
                                  call.getReceiver() == load.getResult() &&
                                  call.getArgs().size() == 1;
                    }
                    if (closed) { continue; }
                }
                if (auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                    call && use.getOperandNumber() == 1 && call.getArgs().size() == 1) {
                    auto read = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                    if (read && read.getObject() == load.getResult() &&
                        ctjs::constantKey(read.getKey()) == "from") {
                        continue;
                    }
                }
            }
            reason = "declared intrinsic identity escapes, is reflected or is mutated";
            return;
        }
    });
    return reason;
}

} // namespace ctcompile::ctnative::host_detail
