#include "ctcompile/CTNative/Analysis/HostContract.h"

#include "mlir/IR/AsmState.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"

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

} // namespace

llvm::Expected<HostContract> parseHostContract(llvm::StringRef text) {
    auto parsed = llvm::json::parse(text);
    if (!parsed) { return parsed.takeError(); }
    const auto * object = parsed->getAsObject();
    if (object == nullptr ||
        !keys(*object, {"version", "provider", "module_sha256", "entry", "roots", "observations",
                        "absent_bindings", "undefined_bindings"})) {
        return error("host contract must be an object with only supported fields");
    }
    if (object->getInteger("version") != 1) { return error("unsupported host contract version"); }
    if (object->getString("provider") != "closed-source-v1") {
        return error("unsupported host provider; expected closed-source-v1");
    }
    const auto digest = object->getString("module_sha256");
    const auto entry = object->getString("entry");
    const auto * roots = object->getArray("roots");
    if (!digest || digest->size() != 64 ||
        !llvm::all_of(*digest,
                      [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) ||
        !entry || entry->empty() || roots == nullptr || roots->empty()) {
        return error("host contract requires a lowercase SHA-256, entry symbol and roots");
    }
    HostContract result;
    result.moduleSha256 = digest->str();
    result.entry = entry->str();
    if (auto failure = names(*object, "observations", result.observations, false)) {
        return std::move(failure);
    }
    if (auto failure = names(*object, "absent_bindings", result.absentBindings, true)) {
        return std::move(failure);
    }
    if (auto failure = names(*object, "undefined_bindings", result.undefinedBindings, true)) {
        return std::move(failure);
    }
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

void clearHostContractReports(mlir::ModuleOp module) {
    module.walk([](mlir::Operation * op) {
        llvm::SmallVector<mlir::StringAttr> names;
        for (mlir::NamedAttribute attribute : op->getAttrs()) {
            if (attribute.getName().getValue().starts_with("ctnative.host_")) {
                names.push_back(attribute.getName());
            }
        }
        for (mlir::StringAttr name : names) { op->removeAttr(name); }
    });
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

} // namespace ctcompile::ctnative
