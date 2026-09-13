#include "Analysis.h"
#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "ctcompile/CTNative/Analysis/HostContract.h"

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
    const bool dom = object->getString("provider") == "ctbrowser-dom-v1";
    if (!dom && object->getString("provider") != "closed-source-v1") {
        return error("unsupported host provider; expected closed-source-v1 or ctbrowser-dom-v1");
    }
    if (dom ? !keys(*object,
                    {"version", "provider", "module_sha256", "entry", "element_parameters"})
            : !keys(*object, {"version", "provider", "module_sha256", "entry", "roots",
                              "observations", "absent_bindings", "undefined_bindings",
                              "initial_intrinsics", "realm_global_this", "entry_receiver"})) {
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
    result.moduleSha256 = digest->str();
    result.entry = entry->str();
    if (dom) {
        result.provider = HostContract::Provider::ctbrowserDOM;
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
        return result;
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
            if (name != "Map" && name != "Array") {
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
    mlir::OwningOpRef<mlir::ModuleOp> copy = module.clone();
    clearHostContractReports(*copy);
    std::string text;
    llvm::raw_string_ostream stream(text);
    copy->print(stream, mlir::OpPrintingFlags().printGenericOpForm().enableDebugInfo(false));
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

DOMEntryAnalysis::DOMEntryAnalysis(mlir::ModuleOp module, const HostContract & contract,
                                   unsigned maxSteps) {
    if (hostContractFingerprint(module) != contract.moduleSha256) {
        refusal = "host contract module fingerprint mismatch";
        return;
    }
    if (module->hasAttr("ctjs.skipped")) {
        refusal = "DOM entry source contains unimported functions";
        return;
    }
    if (contract.provider != HostContract::Provider::ctbrowserDOM ||
        contract.elementParameters.empty() || !contract.roots.empty() ||
        !contract.observations.empty() || !contract.absentBindings.empty() ||
        !contract.undefinedBindings.empty() || !contract.initialIntrinsics.empty() ||
        contract.realmGlobalThis || contract.classicScriptRealm ||
        !contract.realmOwnDataProperties.empty()) {
        refusal = "DOM entry requires the isolated ctbrowser-dom-v1 declaration";
        return;
    }
    const auto spend = [&] {
        if (workSteps == maxSteps) {
            budgetExhausted = true;
            refusal = "DOM entry analysis work budget exhausted";
            return false;
        }
        ++workSteps;
        return true;
    };
    if (!spend()) { return; }
    auto target = module.lookupSymbol<ctjs::FuncOp>(contract.entry);
    if (!target || target.getBody().empty()) {
        refusal = "DOM entry function is missing or external";
        return;
    }
    ctjs::FuncOp declaration;
    for (mlir::Operation & operation : module.getBody()->getOperations()) {
        if (!spend()) { return; }
        auto function = llvm::dyn_cast<ctjs::FuncOp>(operation);
        if (!function || !llvm::hasSingleElement(function.getBody()) ||
            function.getUpvalueCount() != 0 || function->hasAttr("ctjs.skipped") ||
            function.getBody().front().getNumArguments() < ctjs::implicit_arguments) {
            refusal = "DOM entry requires complete single-block functions without captures";
            return;
        }
        if (function != target) {
            if (declaration || functionIndex(function) != 0 ||
                function.getBody().front().getNumArguments() != ctjs::implicit_arguments) {
                refusal = "DOM entry permits only its own source declaration wrapper";
                return;
            }
            declaration = function;
        }
    }
    if (declaration && functionIndex(target) == 0) {
        refusal = "DOM entry has an ambiguous source function identity";
        return;
    }
    auto & targetBlock = target.getBody().front();
    if (contract.elementParameters.size() !=
        targetBlock.getNumArguments() - ctjs::implicit_arguments) {
        refusal = "DOM entry must declare every explicit parameter as an element";
        return;
    }
    std::vector<mlir::BlockArgument> provedElements;
    for (const auto [position, index] : llvm::enumerate(contract.elementParameters)) {
        if (!spend()) { return; }
        if (position != index) {
            refusal = "DOM element_parameters must list every explicit parameter in order";
            return;
        }
        provedElements.push_back(targetBlock.getArgument(index + ctjs::implicit_arguments));
    }

    enum class Kind {
        implicit,
        element,
        tokenList,
        toggle,
        attribute,
        toggleAttribute,
        hasAttribute,
        removeAttribute,
        string,
        boolean,
        undefined,
        declaration
    };
    std::vector<ctjs::GetPropertyOp> provedTokens;
    std::vector<std::pair<ctjs::GetPropertyOp, HostDOMMethod>> provedMethods;
    std::vector<HostDOMCall> provedCalls;
    // ponytail: straight-line entry only; extend with a checked SSA join when
    // an actual browser action needs branches, never with report attributes.
    for (ctjs::FuncOp function : {declaration, target}) {
        if (!function) { continue; }
        auto & block = function.getBody().front();
        const bool wrapper = function == declaration;
        llvm::DenseMap<mlir::Value, Kind> values;
        for (mlir::BlockArgument argument : block.getArguments()) {
            if (!spend()) { return; }
            if (!llvm::isa<ctjs::ValueType>(argument.getType())) {
                refusal = "DOM entry requires original JavaScript parameter types";
                return;
            }
            values[argument] =
                argument.getArgNumber() < ctjs::implicit_arguments ? Kind::implicit : Kind::element;
        }
        const auto hasKind = [&](mlir::Value value, Kind kind) {
            const auto found = values.find(value);
            return found != values.end() && found->second == kind;
        };
        mlir::Value frame;
        ctjs::CreateClosureOp closure;
        bool entered = false, published = false, returned = false;
        for (mlir::Operation & operation : block) {
            if (!spend()) { return; }
            if (operation.getNumRegions() != 0 || operation.getNumSuccessors() != 0 || returned) {
                refusal = "DOM entry does not admit nested control flow or a source continuation";
                return;
            }
            for (mlir::Value operand : operation.getOperands()) {
                if (!spend()) { return; }
                if (!values.contains(operand) && operand != frame) {
                    refusal = "DOM entry operand has no preceding local definition";
                    return;
                }
            }
            if (auto constant = llvm::dyn_cast<ctjs::ConstantOp>(operation)) {
                if (llvm::isa<ctjs::StringAttr>(constant.getValue())) {
                    values[constant.getResult()] = Kind::string;
                } else if (llvm::isa<ctjs::BooleanAttr>(constant.getValue())) {
                    values[constant.getResult()] = Kind::boolean;
                } else if (llvm::isa<ctjs::UndefinedAttr>(constant.getValue())) {
                    values[constant.getResult()] = Kind::undefined;
                } else {
                    refusal = "DOM entry constant has no supported scalar contract";
                    return;
                }
                continue;
            }
            if (auto enter = llvm::dyn_cast<ctjs::FrameEnterOp>(operation)) {
                if (entered) {
                    refusal = "DOM entry has more than one shadow frame";
                    return;
                }
                entered = true;
                frame = enter.getContext();
                continue;
            }
            if (auto root = llvm::dyn_cast<ctjs::RootOp>(operation)) {
                if (!frame || root.getContext() != frame) {
                    refusal = "DOM entry root is outside its local shadow frame";
                    return;
                }
                continue;
            }
            if (auto exit = llvm::dyn_cast<ctjs::FrameExitOp>(operation)) {
                if (!frame || exit.getContext() != frame) {
                    refusal = "DOM entry exits an unknown shadow frame";
                    return;
                }
                frame = {};
                continue;
            }
            if (auto result = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
                if (frame || (!hasKind(result.getValue(), Kind::undefined) &&
                              (wrapper || (!hasKind(result.getValue(), Kind::boolean) &&
                                           !hasKind(result.getValue(), Kind::string))))) {
                    refusal = "DOM entry return must be a scalar with no borrowed browser handle";
                    return;
                }
                returned = true;
                continue;
            }
            if (wrapper) {
                if (auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(operation)) {
                    const auto index = functionIndex(target);
                    if (closure || !index || made.getFunction() < 0 ||
                        static_cast<unsigned>(made.getFunction()) != *index ||
                        made.getEnclosingClosure() != block.getArgument(ctjs::arg_callee) ||
                        (made.getEnclosingThis() != block.getArgument(ctjs::arg_receiver) &&
                         !hasKind(made.getEnclosingThis(), Kind::undefined)) ||
                        !made.getUpvalues().empty()) {
                        refusal = "DOM entry wrapper does not declare the exact uncaptured entry";
                        return;
                    }
                    closure = made;
                    values[made.getResult()] = Kind::declaration;
                    continue;
                }
                if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(operation);
                    store && !published && closure && store.getValue() == closure.getResult() &&
                    store.getName() != "undefined" &&
                    store.getName() == target.getSymName().rsplit('$').first) {
                    published = true;
                    continue;
                }
                refusal = "DOM entry wrapper contains observable source operations";
                return;
            }
            if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation);
                load && load.getName() == "undefined") {
                // The DOM provider fixes this initial binding. The complete
                // source census admits no replacement or script reentry.
                values[load.getResult()] = Kind::undefined;
                continue;
            }
            if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
                const auto key = ctjs::constantKey(read.getKey());
                if (hasKind(read.getObject(), Kind::element) && key == "classList") {
                    values[read.getResult()] = Kind::tokenList;
                    provedTokens.push_back(read);
                    continue;
                }
                if (hasKind(read.getObject(), Kind::element) && key == "setAttribute") {
                    values[read.getResult()] = Kind::attribute;
                    provedMethods.emplace_back(read, HostDOMMethod::setAttribute);
                    continue;
                }
                if (hasKind(read.getObject(), Kind::element) &&
                    (key == "toggleAttribute" || key == "hasAttribute" ||
                     key == "removeAttribute")) {
                    values[read.getResult()] = key == "toggleAttribute" ? Kind::toggleAttribute
                                               : key == "hasAttribute"  ? Kind::hasAttribute
                                                                        : Kind::removeAttribute;
                    provedMethods.emplace_back(
                        read, key == "toggleAttribute" ? HostDOMMethod::toggleAttribute
                              : key == "hasAttribute"  ? HostDOMMethod::hasAttribute
                                                       : HostDOMMethod::removeAttribute);
                    continue;
                }
                if (hasKind(read.getObject(), Kind::tokenList) && key == "toggle") {
                    values[read.getResult()] = Kind::toggle;
                    provedMethods.emplace_back(read, HostDOMMethod::toggleClass);
                    continue;
                }
                refusal = "DOM property read lacks a proved receiver and supported member";
                return;
            }
            if (auto invoke = llvm::dyn_cast<ctjs::CallOp>(operation)) {
                auto method = invoke.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                if (!method || method.getObject() != invoke.getReceiver()) {
                    refusal = "DOM call does not preserve its proved method receiver";
                    return;
                }
                auto arguments = invoke.getArgs();
                if ((hasKind(invoke.getCallee(), Kind::toggle) ||
                     hasKind(invoke.getCallee(), Kind::toggleAttribute)) &&
                    (arguments.size() == 1 ||
                     (arguments.size() == 2 && (hasKind(arguments[1], Kind::boolean) ||
                                                hasKind(arguments[1], Kind::undefined)))) &&
                    hasKind(arguments[0], Kind::string)) {
                    const bool classes = hasKind(invoke.getCallee(), Kind::toggle);
                    auto element =
                        classes
                            ? invoke.getReceiver().getDefiningOp<ctjs::GetPropertyOp>().getObject()
                            : invoke.getReceiver();
                    provedCalls.push_back(
                        {invoke,
                         classes ? HostDOMMethod::toggleClass : HostDOMMethod::toggleAttribute,
                         element});
                    values[invoke.getResult()] = Kind::boolean;
                    continue;
                }
                if (hasKind(invoke.getCallee(), Kind::hasAttribute) && arguments.size() == 1 &&
                    hasKind(arguments[0], Kind::string)) {
                    provedCalls.push_back(
                        {invoke, HostDOMMethod::hasAttribute, invoke.getReceiver()});
                    values[invoke.getResult()] = Kind::boolean;
                    continue;
                }
                const bool sets = hasKind(invoke.getCallee(), Kind::attribute);
                if ((sets && arguments.size() == 2 && hasKind(arguments[0], Kind::string) &&
                     (hasKind(arguments[1], Kind::string) ||
                      hasKind(arguments[1], Kind::boolean))) ||
                    (hasKind(invoke.getCallee(), Kind::removeAttribute) && arguments.size() == 1 &&
                     hasKind(arguments[0], Kind::string))) {
                    for (mlir::Operation * user : invoke.getResult().getUsers()) {
                        if (!spend()) { return; }
                        if (!llvm::isa<ctjs::RootOp>(user)) {
                            refusal = "DOM attribute write result must be unused";
                            return;
                        }
                    }
                    provedCalls.push_back(
                        {invoke,
                         sets ? HostDOMMethod::setAttribute : HostDOMMethod::removeAttribute,
                         invoke.getReceiver()});
                    values[invoke.getResult()] = Kind::undefined;
                    continue;
                }
                refusal = "DOM call arguments lack the supported primitive contract";
                return;
            }
            if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation);
                compare && compare.getKind() == ctjs::CompareKind::StrictEq &&
                hasKind(compare.getLhs(), Kind::element) &&
                hasKind(compare.getRhs(), Kind::element)) {
                values[compare.getResult()] = Kind::boolean;
                continue;
            }
            if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation);
                unary && unary.getKind() == ctjs::UnaryKind::Not &&
                hasKind(unary.getOperand(), Kind::boolean)) {
                values[unary.getResult()] = Kind::boolean;
                continue;
            }
            refusal = ("DOM entry operation lacks a typed browser contract: " +
                       operation.getName().getStringRef())
                          .str();
            return;
        }
        if (!returned || (wrapper && (!closure || !published))) {
            refusal = "DOM entry requires a complete return and exact source declaration";
            return;
        }
    }
    checkedEntry = target;
    checkedWrapper = declaration;
    elements = std::move(provedElements);
    tokenLists = std::move(provedTokens);
    methods = std::move(provedMethods);
    calls = std::move(provedCalls);
}

bool DOMEntryAnalysis::isElement(mlir::Value value) const {
    return llvm::is_contained(elements, value);
}

bool DOMEntryAnalysis::isTokenList(mlir::Value value) const {
    return llvm::any_of(tokenLists,
                        [&](ctjs::GetPropertyOp read) { return read.getResult() == value; });
}

std::optional<HostDOMMethod> DOMEntryAnalysis::method(ctjs::GetPropertyOp read) const {
    for (const auto & [candidate, kind] : methods) {
        if (candidate == read) { return kind; }
    }
    return {};
}

const HostDOMCall * DOMEntryAnalysis::call(ctjs::CallOp operation) const {
    for (const auto & call : calls) {
        if (call.operation == operation) { return &call; }
    }
    return nullptr;
}

} // namespace ctcompile::ctnative

namespace ctcompile::ctnative::host_detail {

std::string initialBindingProblem(mlir::ModuleOp module, const HostContract & contract) {
    if (contract.provider != HostContract::Provider::closedSource ||
        !contract.elementParameters.empty()) {
        return "closed-source analysis requires the closed-source-v1 host provider";
    }
    std::string reason = realmReceiverProblem(contract);
    if (!reason.empty()) { return reason; }
    if (contract.realmGlobalThis &&
        (llvm::is_contained(contract.absentBindings, "globalThis") ||
         llvm::is_contained(contract.undefinedBindings, "globalThis"))) {
        return "invalid initial realm globalThis declaration";
    }
    for (const auto & name : contract.initialIntrinsics) {
        if ((name != "Map" && name != "Array") ||
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
            } else {
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
