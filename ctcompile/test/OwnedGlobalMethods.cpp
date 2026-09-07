#include "../lib/CTNative/Analysis/OwnedGlobalRoots.h"
#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"

#include <cstdio>
#include <string>
#include <utility>

namespace {
namespace ctjs = ctcompile::ctjs;
using ctcompile::ctnative::HostContract;
using ctcompile::ctnative::HostContractAnalysis;
using ctcompile::ctnative::hostContractFingerprint;
using ctcompile::ctnative::OwnedGlobalRoots;

constexpr const char * fixture = R"MLIR(
module {
  ctjs.func @script$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %make = ctjs.create_closure %callee[1] this %u
    %host = ctjs.create_object
    ctjs.store_global "host", %host
    %table = ctjs.call_direct @make$1(%u, %u, %make)
    %slot = ctjs.constant #ctjs.string<"slot">
    ctjs.set_property %host[%slot], %table
    %alias = ctjs.load_global "host"
    %owned = ctjs.get_property %alias[%slot]
    %key = ctjs.constant #ctjs.string<"get">
    %getter = ctjs.get_property %owned[%key]
    %answer = ctjs.call %getter(%owned)
    ctjs.store_global "trace", %answer
    ctjs.return %u
  }
  ctjs.func private @make$1(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %getter = ctjs.create_closure %callee[2] this %u
    %table = ctjs.create_object
    %key = ctjs.constant #ctjs.string<"get">
    ctjs.set_property %table[%key], %getter
    ctjs.return %table
  }
  ctjs.func private @get$2(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %answer = ctjs.constant #ctjs.number<4631107791820423168>
    ctjs.return %answer
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
    contract.entry = "script$0";
    contract.roots = {{"host", {"slot"}}};
    contract.observations = {"trace"};
    return contract;
}
bool empty(mlir::ModuleOp module, const OwnedGlobalRoots & query) {
    bool result = query.roots().empty();
    module.walk([&](mlir::Operation * operation) { result &= !query.lookup(operation); });
    return result;
}
bool complete(const OwnedGlobalRoots & query, unsigned calls = 1) {
    if (!query.proved() || query.exhausted() || query.roots().size() != 1) { return false; }
    const auto & root = query.roots().front();
    if (!root.methodTable || root.binding != "host" || root.property != "slot" ||
        root.loads.size() != 1 || root.reads.size() != 1 || query.lookup(root.owner) != &root ||
        query.lookup(root.initialization) != &root ||
        query.lookup(root.fieldInitialization) != &root ||
        query.lookup(root.loads.front()) != &root || query.lookup(root.reads.front()) != &root) {
        return false;
    }
    const auto & table = *root.methodTable;
    auto factoryCall = table.factoryCall;
    auto field = root.fieldInitialization;
    return table.calls.size() == calls && table.calls.front().function == table.method &&
           table.calls.front().closure == table.closure &&
           table.calls.front().write == table.methodInitialization &&
           factoryCall.getResult() == field.getValue() && !query.lookup(table.table) &&
           !query.lookup(table.calls.front().call);
}
} // namespace

int main() {
    mlir::MLIRContext context;
    context.getOrLoadDialect<ctjs::CTJSDialect>();
    auto module = mlir::parseSourceString<mlir::ModuleOp>(fixture, &context);
    check(static_cast<bool>(module), "owned global method fixture parses");
    if (!module) { return 1; }
    const auto contract = contractFor(*module);
    HostContractAnalysis host(*module, contract);
    check(host.proved(), "complete host analysis proves the current getter");
    OwnedGlobalRoots query(*module, contract);
    check(complete(query), "root storage and current callable share the source table family");
    if (!query.proved()) { std::fprintf(stderr, "%s\n", query.reason().str().c_str()); }
    if (query.roots().empty()) { return 1; }
    for (const auto & [call, count] :
         {std::pair{"%answer = ctjs.call_direct @get$2(%owned, %u, %getter)", 1u},
          std::pair{"%answer = ctjs.call %getter(%owned)\n"
                    "    %again = ctjs.call %getter(%owned)\n"
                    "    ctjs.store_global \"trace\", %again",
                    2u}}) {
        std::string source = fixture;
        constexpr llvm::StringLiteral original = "%answer = ctjs.call %getter(%owned)";
        source.replace(source.find(original.str()), original.size(), call);
        auto variant = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
        check(variant && complete(OwnedGlobalRoots(*variant, contractFor(*variant)), count),
              "resolved and repeated getter calls retain every checked table edge");
    }
    const unsigned completion = query.steps();
    check(completion > host.steps() && completion <= 10000,
          "owner census charges additional bounded work after the complete host proof");
    if (completion > 10000) { return 1; }
    for (unsigned budget = 0; budget < completion; ++budget) {
        OwnedGlobalRoots limited(*module, contract, budget);
        check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                  empty(*module, limited),
              "every incomplete budget withholds the entire owning table plan");
    }
    check(complete(OwnedGlobalRoots(*module, contract, completion)),
          "exact completion budget publishes the complete owner and callable graph");
    auto root = query.roots().front();
    auto table = *root.methodTable;
    mlir::Builder attributes(&context);
    (*module)->setAttr("ctnative.host_owner_proved", attributes.getBoolAttr(true));
    (*module)->setAttr("ctnative.host_callable", attributes.getStringAttr("forged"));
    check(complete(OwnedGlobalRoots(*module, contract)),
          "forged diagnostic reports do not replace the live owner census");
    const auto refused = [&](const char * message) {
        OwnedGlobalRoots stale(*module, contract);
        check(!stale.proved() && stale.reason().contains("fingerprint") && empty(*module, stale),
              "semantic mutation cannot silently rebind the supplied source manifest");
        OwnedGlobalRoots fresh(*module, contractFor(*module));
        check(!fresh.proved() && !fresh.exhausted() && empty(*module, fresh), message);
    };
    const auto restored = [&] {
        check(complete(OwnedGlobalRoots(*module, contract)),
              "restoring source restores its independently rebuilt proof");
    };
    mlir::OpBuilder builder(&context);
    builder.setInsertionPointAfter(root.fieldInitialization);
    auto * rewrite = builder.clone(*root.fieldInitialization.getOperation());
    refused("root field replacement revokes its fixed owning table");
    rewrite->erase();
    restored();

    builder.setInsertionPointAfter(root.initialization);
    auto alias =
        ctjs::StoreGlobalOp::create(builder, root.owner.getLoc(), "alias", root.owner.getResult());
    refused("a second global root alias requires a separate owner contract");
    alias.erase();
    restored();

    builder.setInsertionPointAfter(root.fieldInitialization);
    auto detached = ctjs::StoreGlobalOp::create(builder, root.owner.getLoc(), "detached",
                                                table.factoryCall.getResult());
    refused("detached table publication is not a closed fixed-field export");
    detached.erase();
    restored();

    auto getterRead = table.calls.front().read;
    builder.setInsertionPointAfter(getterRead);
    auto detachedGetter = ctjs::StoreGlobalOp::create(builder, root.owner.getLoc(),
                                                      "detachedGetter", getterRead.getResult());
    refused("publishing the loaded callable invalidates the complete owning table proof");
    detachedGetter.erase();
    restored();

    builder.setInsertionPointAfter(table.factoryCall);
    auto * secondCall = builder.clone(*table.factoryCall.getOperation());
    refused("two factory invocations cannot share a source allocation identity");
    secondCall->erase();
    restored();

    builder.setInsertionPointAfter(table.table);
    auto extra = ctjs::CreateObjectOp::create(builder, root.owner.getLoc());
    refused("equal-shaped fresh allocations are not the same owning table");
    extra.erase();
    restored();

    builder.setInsertionPointAfter(table.methodInitialization);
    auto * methodWrite = builder.clone(*table.methodInitialization.getOperation());
    refused("method replacement is outside the fixed callable storage tier");
    methodWrite->erase();
    restored();

    const auto oldValue = root.fieldInitialization.getValue();
    root.fieldInitialization->setOperand(2, root.owner.getResult());
    refused("a cyclic publication is not an owning callable field");
    root.fieldInitialization->setOperand(2, oldValue);
    restored();

    root.fieldInitialization->moveAfter(root.reads.front());
    refused("reads before owning table publication have no definite field");
    root.fieldInitialization->moveBefore(root.loads.front());
    restored();

    auto * methodTerminator = table.method.getBody().front().getTerminator();
    const auto answer = methodTerminator->getOperand(0);
    methodTerminator->setOperand(0, table.method.getBody().front().getArgument(0));
    refused("a receiver-observing getter needs an additional invocation proof");
    methodTerminator->setOperand(0, answer);
    restored();

    builder.setInsertionPointAfter(table.methodInitialization);
    auto extraKey = ctjs::ConstantOp::create(builder, root.owner.getLoc(),
                                             ctjs::StringAttr::get(&context, "extra"));
    ctjs::ConstantOp factoryUndefined;
    table.factory.walk([&](ctjs::ConstantOp constant) {
        if (llvm::isa<ctjs::UndefinedAttr>(constant.getValue())) { factoryUndefined = constant; }
    });
    auto extraField =
        ctjs::SetPropertyOp::create(builder, root.owner.getLoc(), table.table.getResult(),
                                    extraKey.getResult(), factoryUndefined.getResult());
    refused("an extended table schema is outside the one-method owner tier");
    extraField.erase();
    extraKey.erase();
    restored();

    auto changedContract = contract;
    changedContract.observations.push_back("host");
    OwnedGlobalRoots observed(*module, changedContract);
    check(!observed.proved() && empty(*module, observed),
          "driver observations cannot expose the owning root as a scalar");
    if (failures == 0) {
        std::printf("owned global method proof and all %u incomplete budgets passed\n", completion);
    }
    return failures == 0 ? 0 : 1;
}
