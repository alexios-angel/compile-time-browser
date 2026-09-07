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

constexpr const char * capturedFixture = R"MLIR(
module {
  ctjs.func @script$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %host = ctjs.create_object
    ctjs.store_global "host", %host
    %wrapper = ctjs.create_closure %callee[1] this %u
    %factory = ctjs.create_closure %callee[2] this %u
    %invoked = ctjs.call_direct @publish$1(%u, %u, %wrapper, %factory)
    %alias = ctjs.load_global "host"
    %slot = ctjs.constant #ctjs.string<"slot">
    %owned = ctjs.get_property %alias[%slot]
    %key = ctjs.constant #ctjs.string<"get">
    %getter = ctjs.get_property %owned[%key]
    %answer = ctjs.call %getter(%owned)
    ctjs.store_global "trace", %answer
    ctjs.return %u
  }
  ctjs.func private @publish$1(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %factory: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %host = ctjs.load_global "host"
    %table = ctjs.call_direct @make$2(%u, %u, %factory)
    %slot = ctjs.constant #ctjs.string<"slot">
    ctjs.set_property %host[%slot], %table
    ctjs.return %u
  }
  ctjs.func private @make$2(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %cell = ctjs.create_cell %u
    %constructor = ctjs.load_global "Map"
    %state = ctjs.construct %constructor(%constructor)
    ctjs.cell_set %cell, %state
    %table = ctjs.create_object
    %getter = ctjs.create_closure %callee[3] this %u captures %cell
    %key = ctjs.constant #ctjs.string<"get">
    ctjs.set_property %table[%key], %getter
    ctjs.return %table
  }
  ctjs.func private @get$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 1 : i32} {
    %state = ctjs.load_upvalue %callee[0]
    %key = ctjs.constant #ctjs.string<"size">
    %size = ctjs.get_property %state[%key]
    ctjs.return %size
  }
}
)MLIR";

// The actual imported publication specimen, retaining public functions, frame
// bookkeeping and the indirect wrapper callback. Only symbol names are unified
// with the small fixture above so both use the same assertions.
constexpr const char * importedCapturedFixture = R"MLIR(
module {
  ctjs.func @script$0(%arg0: !ctjs.value, %arg1: !ctjs.value, %arg2: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %0 = ctjs.frame_enter 3
    %1 = ctjs.create_object
    ctjs.store_global "host", %1
    %2 = ctjs.constant #ctjs.undefined
    %3 = ctjs.create_closure %arg2[1] this %2
    %4 = ctjs.constant #ctjs.undefined
    %5 = ctjs.create_closure %arg2[2] this %4
    %6 = ctjs.constant #ctjs.undefined
    %7 = ctjs.constant #ctjs.undefined
    %8 = ctjs.call_direct @publish$1(%6, %7, %3, %5)
    %9 = ctjs.load_global "host"
    %10 = ctjs.constant #ctjs.string<"slot">
    %11 = ctjs.get_property %9[%10]
    %12 = ctjs.constant #ctjs.string<"get">
    %13 = ctjs.get_property %11[%12]
    %14 = ctjs.call %13(%11)
    ctjs.store_global "trace", %14
    %15 = ctjs.constant #ctjs.undefined
    ctjs.frame_exit %0
    ctjs.return %15
  }
  ctjs.func @publish$1(%arg0: !ctjs.value, %arg1: !ctjs.value, %arg2: !ctjs.value, %arg3: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %0 = ctjs.frame_enter 4
    %1 = ctjs.load_global "host"
    %2 = ctjs.constant #ctjs.undefined
    %3 = ctjs.call %arg3(%2)
    %4 = ctjs.constant #ctjs.string<"slot">
    ctjs.set_property %1[%4], %3
    %5 = ctjs.constant #ctjs.undefined
    ctjs.frame_exit %0
    ctjs.return %5
  }
  ctjs.func @make$2(%arg0: !ctjs.value, %arg1: !ctjs.value, %arg2: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %0 = ctjs.frame_enter 3
    %1 = ctjs.constant #ctjs.undefined
    %2 = ctjs.create_cell %1
    %3 = ctjs.load_global "Map"
    %4 = ctjs.construct %3(%3)
    ctjs.cell_set %2, %4
    %5 = ctjs.create_object
    %6 = ctjs.constant #ctjs.undefined
    %7 = ctjs.create_closure %arg2[3] this %6 captures %2
    %8 = ctjs.constant #ctjs.string<"get">
    ctjs.set_property %5[%8], %7
    ctjs.frame_exit %0
    ctjs.return %5
  }
  ctjs.func @get$3(%arg0: !ctjs.value, %arg1: !ctjs.value, %arg2: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 1 : i32} {
    %0 = ctjs.frame_enter 1
    %1 = ctjs.load_upvalue %arg2[0]
    %2 = ctjs.constant #ctjs.string<"size">
    %3 = ctjs.get_property %1[%2]
    ctjs.frame_exit %0
    ctjs.return %3
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
           factoryCall->getResult(0) == field.getValue() && !query.lookup(table.table) &&
           !query.lookup(table.calls.front().call);
}

std::string replaced(std::string source, llvm::StringRef from, llvm::StringRef to) {
    const auto offset = source.find(from.str());
    if (offset == std::string::npos) { return {}; }
    source.replace(offset, from.size(), to.str());
    return source;
}

void checkCapturedMap(mlir::MLIRContext & context) {
    const auto requested = [](mlir::ModuleOp module) {
        auto contract = contractFor(module);
        contract.initialIntrinsics = {"Map"};
        return contract;
    };
    const auto proved = [](const OwnedGlobalRoots & query, bool prepared) {
        if (!query.proved() || query.roots().size() != 1) { return false; }
        const auto & root = query.roots().front();
        if (!root.methodTable || root.loads.size() != 2 || root.reads.size() != 1) { return false; }
        const auto & table = *root.methodTable;
        if (!table.wrapper || !table.wrapperCall || !table.capturedMap || table.calls.size() != 1 ||
            !table.calls.front().capturedMap) {
            return false;
        }
        const auto & capture = *table.capturedMap;
        return capture.intrinsic && capture.allocation && capture.size &&
               capture.allocation == table.calls.front().capturedMap->allocation &&
               (prepared ? (!capture.cell && !capture.initialization && !capture.upvalue &&
                            capture.argument)
                         : (capture.cell && capture.initialization && capture.upvalue &&
                            !capture.argument));
    };
    auto prepared = replaced(capturedFixture, "    %cell = ctjs.create_cell %u\n", "");
    prepared = replaced(prepared, "    ctjs.cell_set %cell, %state\n", "");
    prepared = replaced(prepared, "captures %cell", "captures %state");
    prepared = replaced(prepared, "    %state = ctjs.load_upvalue %callee[0]\n", "");
    prepared =
        replaced(prepared, "@get$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value)",
                 "@get$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, "
                 "%state: !ctjs.value)");
    prepared = replaced(prepared, "upvalue_count = 1 : i32", "upvalue_count = 0 : i32");
    prepared = replaced(prepared, "%answer = ctjs.call %getter(%owned)",
                        "%environment = ctjs.load_upvalue %getter[0]\n"
                        "    %answer = ctjs.call_direct @get$3(%owned, %u, %getter, %environment)");
    auto specialized =
        replaced(prepared, "    %factory = ctjs.create_closure %callee[2] this %u\n", "");
    specialized = replaced(specialized, "@publish$1(%u, %u, %wrapper, %factory)",
                           "@publish$1(%u, %u, %wrapper)");
    specialized =
        replaced(specialized,
                 "@publish$1(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, "
                 "%factory: !ctjs.value)",
                 "@publish$1(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value)");
    specialized = replaced(specialized, "    %host = ctjs.load_global \"host\"",
                           "    %factory = ctjs.create_closure %callee[2] this %u\n"
                           "    %host = ctjs.load_global \"host\"");
    const auto indirect = replaced(capturedFixture, "ctjs.call_direct @make$2(%u, %u, %factory)",
                                   "ctjs.call %factory(%u)");
    for (const auto & [source, lifted] :
         {std::pair{std::string(capturedFixture), false}, std::pair{indirect, false},
          std::pair{std::string(importedCapturedFixture), false}, std::pair{prepared, true},
          std::pair{specialized, true}}) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
        check(static_cast<bool>(module), "captured Map source and prepared fixtures parse");
        if (!module) { continue; }
        const auto contract = requested(*module);
        HostContractAnalysis host(*module, contract);
        OwnedGlobalRoots query(*module, contract);
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "raw factory resolution preserves all fingerprinted source operations");
        if (!host.proved()) {
            std::fprintf(stderr, "capture host: %s\n", host.reason().str().c_str());
        }
        if (!query.proved()) {
            std::fprintf(stderr, "capture owner: %s\n", query.reason().str().c_str());
        }
        check(proved(query, lifted),
              "live capture proof preserves wrapper publication and the sole Map identity");
        if (!query.proved()) { continue; }
        const unsigned completion = query.steps();
        check(completion > host.steps() && completion < 10000,
              "capture ownership charges bounded work after its host proof");
        if (completion >= 10000) { continue; }
        for (unsigned budget = 0; budget < completion; ++budget) {
            OwnedGlobalRoots limited(*module, contract, budget);
            check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                      empty(*module, limited),
                  "every incomplete capture budget withholds all owning source edges");
        }
        check(proved(OwnedGlobalRoots(*module, contract, completion), lifted),
              "exact capture completion budget publishes its source graph");
        check(!OwnedGlobalRoots(*module, contractFor(*module)).proved(),
              "a Map capture requires the explicit standard intrinsic identity");
        auto method = module->lookupSymbol<ctjs::FuncOp>("get$3");
        auto returned = llvm::cast<ctjs::ReturnOp>(method.getBody().front().getTerminator());
        returned->setOperand(0, method.getBody().front().getArgument(0));
        OwnedGlobalRoots stale(*module, contract);
        check(!stale.proved() && stale.reason().contains("fingerprint") && empty(*module, stale),
              "changed capture source cannot silently refresh its original fingerprint");
        OwnedGlobalRoots changed(*module, requested(*module));
        check(!changed.proved() && empty(*module, changed),
              "a fresh fingerprint cannot authorize a receiver-observing captured method");
        std::printf("captured Map %s proof and all %u incomplete budgets checked\n",
                    lifted ? "prepared" : "source", completion);
    }
    const auto refuse = [&](llvm::StringRef source, llvm::StringRef from, llvm::StringRef to,
                            const char * message) {
        auto module =
            mlir::parseSourceString<mlir::ModuleOp>(replaced(source.str(), from, to), &context);
        check(static_cast<bool>(module), "captured Map refusal variant parses");
        if (!module) { return; }
        OwnedGlobalRoots result(*module, requested(*module));
        check(!result.proved() && !result.exhausted() && empty(*module, result), message);
    };
    refuse(capturedFixture, "ctjs.cell_set %cell, %state",
           "ctjs.cell_set %cell, %state\n    ctjs.cell_set %cell, %u",
           "multiple writes revoke immutable captured environment ownership");
    refuse(capturedFixture, "ctjs.cell_set %cell, %state", "ctjs.cell_set %cell, %cell",
           "a cyclic capture cannot inherit the Map allocation identity");
    refuse(capturedFixture, "ctjs.cell_set %cell, %state",
           "ctjs.cell_set %cell, %state\n    ctjs.store_global \"escape\", %state",
           "separate Map publication is outside the captured owner graph");
    refuse(capturedFixture, "ctjs.cell_set %cell, %state",
           "ctjs.cell_set %cell, %state\n    ctjs.store_global \"escape\", %cell",
           "a published capture cell cannot inherit a private environment proof");
    refuse(capturedFixture, "%state = ctjs.construct %constructor(%constructor)",
           "%state = ctjs.construct %constructor(%constructor, %u)",
           "Map iterables retain their iteration and exception boundary");
    refuse(capturedFixture, "%constructor = ctjs.load_global \"Map\"",
           "ctjs.store_global \"Map\", %u\n    %constructor = ctjs.load_global \"Map\"",
           "source replacement revokes the standard Map identity");
    refuse(capturedFixture, "ctjs.return %size", "ctjs.throw %size",
           "throwing captured methods need an explicit exceptional boundary");
    refuse(capturedFixture, "ctjs.return %size",
           "%again = ctjs.call %callee(%this)\n    ctjs.return %size",
           "method reentry cannot be treated as an inert size getter");
    refuse(capturedFixture, "#ctjs.string<\"size\">", "#ctjs.string<\"get\">",
           "other Map methods do not inherit the checked size read");
    refuse(indirect, "ctjs.call %factory(%u)", "ctjs.call %factory(%u, %u)",
           "an indirect factory argument window is outside the captured Map proof");
    refuse(indirect, "ctjs.call %factory(%u)", "ctjs.call %factory(%host)",
           "an indirect factory retains its source receiver convention");
    refuse(indirect, "ctjs.call %factory(%u)",
           "ctjs.call %factory(%u)\n    %again = ctjs.call %factory(%u)",
           "multiple indirect factory invocations cannot merge Map identities");
    refuse(indirect, "ctjs.cell_set %cell, %state",
           "ctjs.cell_set %cell, %state\n    ctjs.cell_set %cell, %u",
           "indirect callback resolution cannot authorize mutable capture state");
    refuse(indirect, "%factory = ctjs.create_closure %callee[2] this %u",
           "%factory = ctjs.create_closure %u[2] this %u",
           "indirect callbacks retain their supplied source program identity");
    refuse(capturedFixture, "%table = ctjs.call_direct @make$2(%u, %u, %factory)",
           "%table = ctjs.call_direct @make$2(%u, %u, %factory)\n"
           "    %another = ctjs.call_direct @make$2(%u, %u, %factory)",
           "repeated factory invocations cannot merge fresh Map identities");
    refuse(capturedFixture, "%invoked = ctjs.call_direct @publish$1(%u, %u, %wrapper, %factory)",
           "%invoked = ctjs.call_direct @publish$1(%u, %u, %wrapper, %factory)\n"
           "    %again = ctjs.call_direct @publish$1(%u, %u, %wrapper, %factory)",
           "repeated wrapper invocations cannot merge descendant Map identities");
    refuse(capturedFixture, "%table = ctjs.call_direct @make$2(%u, %u, %factory)",
           "%table = ctjs.call_direct @make$2(%u, %u, %factory)\n"
           "    ctjs.store_global \"escapedFactory\", %factory",
           "transported factory parameters cannot acquire an exported alias");
    refuse(capturedFixture, "%factory = ctjs.create_closure %callee[2] this %u",
           "%factory = ctjs.create_closure %u[2] this %u",
           "transported factories retain their creating activation's source identity");
    refuse(capturedFixture, "ctjs.set_property %host[%slot], %table",
           "ctjs.set_property %host[%slot], %table\n    ctjs.store_global \"table\", %table",
           "additional table publication remains outside the fixed owning field");
    refuse(prepared, "ctjs.load_upvalue %getter[0]", "ctjs.load_upvalue %getter[1]",
           "prepared capture operands need the actual stored environment slot");
    refuse(prepared, "@get$3(%owned, %u, %getter, %environment)", "@get$3(%owned, %u, %getter, %u)",
           "a forged prepared capture argument cannot replace the owning Map");
    refuse(prepared, "captures %state", "captures %u",
           "native environment markers cannot supply a missing Map producer");
}
} // namespace

int main() {
    mlir::MLIRContext context;
    context.getOrLoadDialect<ctjs::CTJSDialect>();
    checkCapturedMap(context);
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
                                                table.factoryCall->getResult(0));
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
    auto * secondCall = builder.clone(*table.factoryCall);
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
