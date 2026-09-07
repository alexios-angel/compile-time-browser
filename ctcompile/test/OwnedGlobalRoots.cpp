#include "../lib/CTNative/Analysis/OwnedGlobalRoots.h"
#include "ctcompile/CTJS/IR/CTJSDialect.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"

#include <cstdio>
#include <string>

namespace {
namespace ctjs = ctcompile::ctjs;
using ctcompile::ctnative::HostContract;
using ctcompile::ctnative::HostContractAnalysis;
using ctcompile::ctnative::hostContractFingerprint;
using ctcompile::ctnative::OwnedGlobalRoots;

constexpr const char * fixture = R"MLIR(
module {
  ctjs.func @_script_(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %key = ctjs.constant #ctjs.string<"slot">
    %number = ctjs.constant #ctjs.number<4631107791820423168>
    %host = ctjs.create_object
    ctjs.store_global "host", %host
    %loaded = ctjs.load_global "host"
    ctjs.set_property %loaded[%key], %number
    %alias = ctjs.load_global "host"
    %read = ctjs.get_property %alias[%key]
    ctjs.store_global "trace", %read
    %again = ctjs.get_property %host[%key]
    ctjs.store_global "trace", %again
    ctjs.return %u
  }
}
)MLIR";

int failures = 0;
void check(bool value, const char * message) {
    if (value) { return; }
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}

HostContract contractFor(mlir::ModuleOp module) {
    HostContract contract;
    contract.moduleSha256 = hostContractFingerprint(module);
    contract.entry = "_script_";
    contract.roots = {{"host", {"slot"}}};
    contract.observations = {"trace"};
    return contract;
}

bool empty(mlir::ModuleOp module, const OwnedGlobalRoots & query) {
    bool clean = query.roots().empty();
    module.walk([&](mlir::Operation * operation) { clean &= !query.lookup(operation); });
    return clean;
}

bool complete(mlir::ModuleOp module, const OwnedGlobalRoots & query) {
    if (!query.proved() || query.exhausted() || query.roots().size() != 1) { return false; }
    const auto & root = query.roots().front();
    if (root.binding != "host" || root.property != "slot" || root.loads.size() != 2 ||
        root.reads.size() != 2 || query.lookup(root.owner) != &root ||
        query.lookup(root.initialization) != &root ||
        query.lookup(root.fieldInitialization) != &root) {
        return false;
    }
    for (ctjs::LoadGlobalOp load : root.loads) {
        if (query.lookup(load) != &root) { return false; }
    }
    for (ctjs::GetPropertyOp read : root.reads) {
        if (query.lookup(read) != &root) { return false; }
    }
    bool unrelated = true;
    module.walk([&](ctjs::StoreGlobalOp store) {
        if (store.getName() != "host") { unrelated &= !query.lookup(store); }
    });
    return unrelated;
}

std::string replaced(std::string source, llvm::StringRef from, llvm::StringRef to) {
    const auto offset = source.find(from.str());
    if (offset == std::string::npos) { return {}; }
    source.replace(offset, from.size(), to.str());
    return source;
}
} // namespace

int main() {
    mlir::MLIRContext context;
    context.getOrLoadDialect<ctjs::CTJSDialect>();
    context.getOrLoadDialect<mlir::arith::ArithDialect>();
    context.getOrLoadDialect<mlir::scf::SCFDialect>();
    auto module = mlir::parseSourceString<mlir::ModuleOp>(fixture, &context);
    check(static_cast<bool>(module), "owned global fixture parses");
    if (!module) { return 1; }
    auto contract = contractFor(*module);
    HostContractAnalysis host(*module, contract);
    check(host.proved() && !host.exhausted() && host.steps() > 0,
          "the complete host proof exposes its consumed work");
    OwnedGlobalRoots normal(*module, contract);
    check(complete(*module, normal), "global aliases and direct field reads share one owner");
    if (!normal.proved()) { std::fprintf(stderr, "%s\n", normal.reason().str().c_str()); }
    if (normal.roots().empty()) { return 1; }

    const unsigned completionBudget = normal.steps();
    check(completionBudget > host.steps() && completionBudget <= 4096,
          "owner proof accounts for complete host work and its additional census");
    for (unsigned budget = 0; budget < completionBudget; ++budget) {
        OwnedGlobalRoots limited(*module, contract, budget);
        check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                  empty(*module, limited),
              "every incomplete budget atomically withholds every ownership edge");
    }
    OwnedGlobalRoots exact(*module, contract, completionBudget);
    check(complete(*module, exact) && exact.steps() == completionBudget,
          "the exact deterministic completion budget reproduces the full proof");
    OwnedGlobalRoots hostOnly(*module, contract, host.steps());
    check(hostOnly.exhausted() && empty(*module, hostOnly),
          "completing only the host proof exposes no partially checked owner");

    auto owner = normal.roots().front().owner;
    auto initialization = normal.roots().front().initialization;
    auto write = normal.roots().front().fieldInitialization;
    auto loads = normal.roots().front().loads;
    auto reads = normal.roots().front().reads;
    mlir::Builder attributes(&context);
    (*module)->setAttr("ctnative.host_proved", attributes.getBoolAttr(true));
    (*module)->setAttr("ctnative.host_slot", attributes.getStringAttr("forged"));
    check(hostContractFingerprint(*module) == contract.moduleSha256 &&
              complete(*module, OwnedGlobalRoots(*module, contract)),
          "forged host reports are ignored and a live rerun rederives ownership");

    const auto refused = [&](const char * message) {
        OwnedGlobalRoots stale(*module, contract);
        check(!stale.proved() && stale.reason().contains("fingerprint") && empty(*module, stale),
              "semantic mutation invalidates the supplied manifest without rebinding it");
        OwnedGlobalRoots changed(*module, contractFor(*module));
        check(!changed.proved() && !changed.exhausted() && empty(*module, changed), message);
    };
    const auto restored = [&] {
        check(complete(*module, OwnedGlobalRoots(*module, contract)),
              "restoring the exact source restores its live ownership proof");
    };

    mlir::OpBuilder builder(&context);
    builder.setInsertionPointAfter(reads.back());
    auto * replacement = builder.clone(*write.getOperation());
    check(HostContractAnalysis(*module, contractFor(*module)).proved(),
          "the broader host contract permits an ordered field replacement");
    refused("native ownership refuses field replacement through a loaded alias");
    replacement->erase();
    restored();

    builder.setInsertionPointAfter(initialization);
    auto * rebound = builder.clone(*initialization.getOperation());
    refused("root binding replacement revokes every ownership edge");
    rebound->erase();
    restored();

    builder.setInsertionPointAfter(reads.back());
    auto alias = ctjs::StoreGlobalOp::create(builder, owner.getLoc(), "other", owner.getResult());
    check(HostContractAnalysis(*module, contractFor(*module)).proved(),
          "the broader host contract can follow a second source global alias");
    refused("publication into another global is outside the single-root owner tier");
    alias.erase();
    restored();

    builder.setInsertionPointAfter(reads.back());
    auto extraKey =
        ctjs::ConstantOp::create(builder, owner.getLoc(), ctjs::StringAttr::get(&context, "other"));
    auto extra = ctjs::SetPropertyOp::create(builder, owner.getLoc(), owner.getResult(),
                                             extraKey.getResult(), write.getValue());
    check(HostContractAnalysis(*module, contractFor(*module)).proved(),
          "the broader host contract does not describe unrelated own-data fields");
    refused("a second field is outside the fixed one-field owner tier");
    extra.erase();
    extraKey.erase();
    restored();

    const auto scalar = write.getValue();
    write->setOperand(2, owner.getResult());
    refused("a self-reference is not a scalar ownership edge");
    write->setOperand(2, scalar);
    restored();

    write->moveAfter(reads.front());
    refused("a field read before initialization revokes the proof");
    write->moveBefore(loads.back());
    restored();

    loads.front()->moveBefore(initialization);
    refused("a global alias read before initialization revokes the proof");
    loads.front()->moveAfter(initialization);
    restored();

    builder.setInsertionPointAfter(reads.back());
    auto call = ctjs::CallOp::create(builder, owner.getLoc(), owner.getType(), owner.getResult(),
                                     owner.getResult(), mlir::ValueRange{});
    refused("an unknown call withholds ownership despite forged host reports");
    call.erase();
    restored();

    const auto fixedKey = write.getKey();
    write->setOperand(1, scalar);
    refused("dynamic or non-string property keys cannot initialize the native field");
    write->setOperand(1, fixedKey);
    restored();

    auto keyConstant = fixedKey.getDefiningOp<ctjs::ConstantOp>();
    const auto oldKey = keyConstant.getValue();
    keyConstant.setValueAttr(ctjs::StringAttr::get(&context, "__proto__"));
    refused("prototype mutation cannot be reclassified as own-data initialization");
    keyConstant.setValueAttr(oldKey);
    restored();

    auto entry = owner->getParentOfType<ctjs::FuncOp>();
    entry->setAttr("ctjs.skipped", attributes.getUnitAttr());
    refused("unimported source cannot supply a complete owned-root proof");
    entry->removeAttr("ctjs.skipped");
    restored();

    auto ownerObservation = contract;
    ownerObservation.observations.push_back("host");
    OwnedGlobalRoots observedOwner(*module, ownerObservation);
    check(!observedOwner.proved() && empty(*module, observedOwner),
          "an owning root cannot be emitted as a scalar observation");
    auto missingObservation = contract;
    missingObservation.observations.push_back("missing");
    OwnedGlobalRoots incomplete(*module, missingObservation);
    check(!incomplete.proved() && empty(*module, incomplete),
          "a partial host report cannot authorize an otherwise valid owner");
    auto duplicate = contract;
    duplicate.roots.push_back({"host", {"slot"}});
    OwnedGlobalRoots duplicated(*module, duplicate);
    check(!duplicated.proved() && empty(*module, duplicated),
          "one root request cannot silently expand into multiple owner plans");

    const auto variant = [&](llvm::StringRef from, llvm::StringRef to, bool hostProves,
                             const char * message) {
        auto text = replaced(fixture, from, to);
        auto changed = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(changed), "owned global refusal variant parses");
        if (!changed) { return; }
        auto changedContract = contractFor(*changed);
        if (hostProves) {
            check(HostContractAnalysis(*changed, changedContract).proved(),
                  "the broader host proof completes for the stricter owner control");
        }
        OwnedGlobalRoots query(*changed, changedContract);
        check(!query.proved() && !query.exhausted() && empty(*changed, query), message);
    };
    variant("ctjs.store_global \"host\", %host",
            "%yes = arith.constant true\n"
            "    scf.if %yes {\n"
            "      ctjs.store_global \"host\", %host\n"
            "    }",
            true, "conditional root binding is outside the first unconditional owner tier");
    variant("ctjs.set_property %loaded[%key], %number",
            "%yes = arith.constant true\n"
            "    scf.if %yes {\n"
            "      ctjs.set_property %loaded[%key], %number\n"
            "    }",
            true, "conditional field initialization is outside the first owner tier");
    variant("%host = ctjs.create_object",
            "%second = ctjs.create_object\n    %host = ctjs.create_object", true,
            "distinct allocations never collapse into one root because their schemas agree");
    variant("ctjs.return %u",
            "%closure = ctjs.create_closure %callee[0] this %u\n    ctjs.return %u", false,
            "callable creation cannot enter the scalar owner tier");
    variant("\n}\n",
            "\n  ctjs.func private @unused(%this: !ctjs.value, %new: !ctjs.value, %callee: "
            "!ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {\n"
            "    %u = ctjs.constant #ctjs.undefined\n"
            "    ctjs.return %u\n"
            "  }\n}\n",
            true, "multiple source functions remain outside the single-entry owner tier");

    if (failures == 0) {
        std::printf("owned global live proof and all %u incomplete budgets passed\n",
                    completionBudget);
    }
    return failures == 0 ? 0 : 1;
}
