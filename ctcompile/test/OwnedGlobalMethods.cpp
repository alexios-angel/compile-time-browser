#include "../lib/CTNative/Analysis/OwnedGlobalRoots.h"
#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"

#include <cstdio>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

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
    return table.methods.size() == 1 && table.calls.size() == calls &&
           table.calls.front().function == table.methods.front().function &&
           table.calls.front().closure == table.methods.front().closure &&
           table.calls.front().write == table.methods.front().initialization &&
           factoryCall->getResult(0) == field.getValue() && !query.lookup(table.table) &&
           !query.lookup(table.calls.front().call);
}

std::string replaced(std::string source, llvm::StringRef from, llvm::StringRef to) {
    const auto offset = source.find(from.str());
    if (offset == std::string::npos) { return {}; }
    source.replace(offset, from.size(), to.str());
    return source;
}

void checkSharedMap(mlir::MLIRContext & context) {
    auto source = replaced(capturedFixture, "ctjs.call_direct @make$2(%u, %u, %factory)",
                           "ctjs.call %factory(%u)");
    source = replaced(source, "    ctjs.return %table",
                      "    %putter = ctjs.create_closure %callee[4] this %u captures %cell\n"
                      "    %putKey = ctjs.constant #ctjs.string<\"put\">\n"
                      "    ctjs.set_property %table[%putKey], %putter\n"
                      "    ctjs.return %table");
    source = replaced(source, "\n}\n", R"MLIR(
  ctjs.func private @put$4(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 1 : i32} {
    %state = ctjs.load_upvalue %callee[0]
    %setKey = ctjs.constant #ctjs.string<"set">
    %setter = ctjs.get_property %state[%setKey]
    %entryKey = ctjs.constant #ctjs.string<"x">
    %value = ctjs.constant #ctjs.number<4607182418800017408>
    %written = ctjs.call %setter(%state, %entryKey, %value)
    %u = ctjs.constant #ctjs.undefined
    ctjs.return %u
  }
}
)MLIR");
    source = replaced(source, "    %answer = ctjs.call %getter(%owned)",
                      "    %putKey = ctjs.constant #ctjs.string<\"put\">\n"
                      "    %putter = ctjs.get_property %owned[%putKey]\n"
                      "    %putResult = ctjs.call %putter(%owned)\n"
                      "    %answer = ctjs.call %getter(%owned)");
    const auto replaceAll = [](std::string text, llvm::StringRef from, llvm::StringRef to) {
        std::size_t offset = 0;
        while ((offset = text.find(from.str(), offset)) != std::string::npos) {
            text.replace(offset, from.size(), to.str());
            offset += to.size();
        }
        return text;
    };
    const auto prepare = [&](std::string text) {
        text =
            replaced(text, "ctjs.call %factory(%u)", "ctjs.call_direct @make$2(%u, %u, %factory)");
        text = replaced(text, "    %cell = ctjs.create_cell %u\n", "");
        text = replaced(text, "    ctjs.cell_set %cell, %state\n", "");
        text = replaceAll(text, "captures %cell", "captures %state");
        text = replaceAll(text, "    %state = ctjs.load_upvalue %callee[0]\n", "");
        text = replaceAll(text, "upvalue_count = 1 : i32", "upvalue_count = 0 : i32");
        for (const auto & [name, closure, result] : {std::tuple{"get$3", "getter", "answer"},
                                                     {"put$4", "putter", "putResult"},
                                                     {"has$5", "hasMethod", "hasResult"}}) {
            const auto signature = std::string("@") + name +
                                   "(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value";
            if (text.find(signature) == std::string::npos) { continue; }
            text = replaced(text, signature, signature + ", %state: !ctjs.value");
            const auto call = std::string("%") + result + " = ctjs.call %" + closure + "(%owned";
            const auto lifted = std::string("%") + closure + "Env = ctjs.load_upvalue %" + closure +
                                "[0]\n    %" + result + " = ctjs.call_direct @" + name +
                                "(%owned, %u, %" + closure + ", %" + closure + "Env";
            text = replaced(text, call, lifted);
        }
        return text;
    };
    auto three = replaced(source, "    ctjs.return %table",
                          "    %hasMethod = ctjs.create_closure %callee[5] this %u captures %cell\n"
                          "    %hasKey = ctjs.constant #ctjs.string<\"has\">\n"
                          "    ctjs.set_property %table[%hasKey], %hasMethod\n"
                          "    ctjs.return %table");
    three = replaced(three, "\n}\n", R"MLIR(
  ctjs.func private @has$5(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 1 : i32} {
    %state = ctjs.load_upvalue %callee[0]
    %key = ctjs.constant #ctjs.string<"has">
    %method = ctjs.get_property %state[%key]
    %entryKey = ctjs.constant #ctjs.string<"x">
    %found = ctjs.call %method(%state, %entryKey)
    ctjs.return %found
  }
}
)MLIR");
    three = replaced(three, "    %answer = ctjs.call %getter(%owned)",
                     "    %hasKey = ctjs.constant #ctjs.string<\"has\">\n"
                     "    %hasMethod = ctjs.get_property %owned[%hasKey]\n"
                     "    %hasResult = ctjs.call %hasMethod(%owned)\n"
                     "    %answer = ctjs.call %getter(%owned)");
    const auto requested = [](mlir::ModuleOp module) {
        auto contract = contractFor(module);
        contract.initialIntrinsics = {"Map"};
        return contract;
    };
    for (const auto & [program, members, lifted] : {std::tuple{source, 2u, false},
                                                    {prepare(source), 2u, true},
                                                    {three, 3u, false},
                                                    {prepare(three), 3u, true}}) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(program, &context);
        check(static_cast<bool>(module), "shared Map source and prepared fixtures parse");
        if (!module) { continue; }
        const auto contract = requested(*module);
        OwnedGlobalRoots query(*module, contract);
        if (!query.proved()) {
            std::fprintf(stderr, "shared owner: %s\n", query.reason().str().c_str());
        }
        check(query.proved() && query.roots().size() == 1,
              "every fixed method shares the same completely checked ordinary owner");
        if (!query.proved() || query.roots().empty()) { continue; }
        const auto & table = *query.roots().front().methodTable;
        const auto & capture = *table.capturedMap;
        check(table.methods.size() == members && table.calls.size() == members &&
                  capture.closures.size() == members && capture.reads.size() == members &&
                  capture.calls.size() == members - 1 &&
                  capture.upvalues.size() == (lifted ? 0 : members),
              "shared owner records all method, capture, body and current-call edges");
        for (const auto & edge : table.calls) {
            check(edge.capturedMap && edge.capturedMap->allocation == capture.allocation &&
                      edge.capturedMap->closures == capture.closures &&
                      edge.capturedMap->reads == capture.reads &&
                      edge.capturedMap->calls == capture.calls &&
                      static_cast<bool>(edge.capturedMap->argument) == lifted,
                  "all actual calls carry the same full family and their own lifted argument");
        }
        const unsigned completion = query.steps();
        check(completion < 10000, "shared Map source proof is bounded");
        if (completion < 10000) {
            for (unsigned budget = 0; budget < completion; ++budget) {
                OwnedGlobalRoots limited(*module, contract, budget);
                check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                          empty(*module, limited),
                      "every incomplete shared-owner budget exposes no partial source graph");
            }
            check(OwnedGlobalRoots(*module, contract, completion).proved(),
                  "exact shared-owner completion budget reproduces all methods");
        }
        mlir::Builder attributes(&context);
        (*module)->setAttr("ctnative.host_owner_proved", attributes.getBoolAttr(true));
        check(OwnedGlobalRoots(*module, contract).proved(),
              "shared ownership rederives its family despite forged report attributes");
        auto mutation = capture.calls.front();
        const auto originalValue = mutation.getArgs().back();
        mutation->setOperand(3, mutation.getReceiver());
        OwnedGlobalRoots stale(*module, contract);
        check(!stale.proved() && stale.reason().contains("fingerprint") && empty(*module, stale),
              "a changed sibling body invalidates the original shared-owner fingerprint");
        OwnedGlobalRoots changed(*module, requested(*module));
        check(!changed.proved() && empty(*module, changed),
              "a fresh shared-owner fingerprint cannot authorize a sibling Map cycle");
        mutation->setOperand(3, originalValue);
        check(OwnedGlobalRoots(*module, contract).proved(),
              "restoring the sibling method restores its live shared-owner proof");
        auto sibling = module->lookupSymbol<ctjs::FuncOp>("put$4");
        auto returned = llvm::cast<ctjs::ReturnOp>(sibling.getBody().front().getTerminator());
        const auto originalReturn = returned.getValue();
        for (unsigned implicit : {0u, 1u}) {
            returned->setOperand(0, sibling.getBody().front().getArgument(implicit));
            OwnedGlobalRoots oldReceiver(*module, contract);
            check(!oldReceiver.proved() && oldReceiver.reason().contains("fingerprint") &&
                      empty(*module, oldReceiver),
                  "a sibling implicit-receiver mutation invalidates the original fingerprint");
            OwnedGlobalRoots newReceiver(*module, requested(*module));
            check(!newReceiver.proved() && empty(*module, newReceiver),
                  "every sibling must remain independent of this and new.target");
            returned->setOperand(0, originalReturn);
            check(OwnedGlobalRoots(*module, contract).proved(),
                  "restoring sibling receiver independence restores the live family");
        }
        if (!lifted) {
            auto upvalue = llvm::cast<ctjs::LoadUpvalueOp>(sibling.getBody().front().front());
            upvalue.setIndexAttr(attributes.getI32IntegerAttr(1));
            OwnedGlobalRoots oldSlot(*module, contract);
            check(!oldSlot.proved() && oldSlot.reason().contains("fingerprint") &&
                      empty(*module, oldSlot),
                  "a sibling capture-index mutation invalidates the original fingerprint");
            OwnedGlobalRoots newSlot(*module, requested(*module));
            check(!newSlot.proved() && empty(*module, newSlot),
                  "an earlier sibling proof cannot authorize another environment slot");
            upvalue.setIndexAttr(attributes.getI32IntegerAttr(0));
            check(OwnedGlobalRoots(*module, contract).proved(),
                  "restoring the sibling environment index restores the live family");
        }
        std::printf("shared Map %u-method %s proof and all %u incomplete budgets checked\n",
                    members, lifted ? "prepared" : "source", completion);
    }
    const auto refuse = [&](std::string program, const char * message) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(program, &context);
        check(static_cast<bool>(module), "shared Map refusal fixture parses");
        if (!module) { return; }
        OwnedGlobalRoots query(*module, requested(*module));
        check(!query.proved() && !query.exhausted() && empty(*module, query), message);
    };
    refuse(replaced(source, "    %putResult = ctjs.call %putter(%owned)\n", ""),
           "an uncalled published sibling withholds the complete owner plan");
    refuse(replaced(source, "ctjs.call %putter(%owned)", "ctjs.call %putter(%host)"),
           "every method needs its actual receiver in the checked table family");
    refuse(replaced(source, "ctjs.call %putter(%owned)", "ctjs.call %putter(%owned, %u)"),
           "surplus actuals cannot extend the source method signature");
    refuse(replaced(source, "ctjs.set_property %table[%putKey], %putter",
                    "ctjs.set_property %table[%key], %putter"),
           "fixed shared methods cannot replace each other");
    refuse(replaced(source, "%putter = ctjs.create_closure %callee[4] this %u captures %cell",
                    "%putter = ctjs.create_closure %callee[3] this %u captures %cell"),
           "two closure creations cannot silently merge one source method identity");
    refuse(replaced(source, "ctjs.call %setter(%state, %entryKey, %value)",
                    "ctjs.call %setter(%state, %entryKey, %state)"),
           "every captured method participates in the primitive contents proof");
    refuse(replaced(source, "    %written = ctjs.call %setter(%state, %entryKey, %value)",
                    "    ctjs.store_global \"escaped\", %state\n"
                    "    %written = ctjs.call %setter(%state, %entryKey, %value)"),
           "a sibling cannot separately publish the shared Map");
    refuse(replaced(source, "    ctjs.set_property %table[%putKey], %putter",
                    "    ctjs.set_property %table[%putKey], %putter\n"
                    "    ctjs.store_global \"escapedMethod\", %putter"),
           "a sibling callable cannot acquire an unchecked export alias");
    refuse(replaced(source, "    ctjs.cell_set %cell, %state",
                    "    ctjs.cell_set %cell, %state\n    ctjs.cell_set %cell, %u"),
           "all family members depend on one immutable Map slot");
    refuse(replaced(prepare(source), "%putterEnv = ctjs.load_upvalue %putter[0]",
                    "%putterEnv = ctjs.load_upvalue %getter[0]"),
           "prepared sibling arguments must come from their own current callable");
    auto mixed =
        replaced(source, "@put$4(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value)",
                 "@put$4(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, "
                 "%state: !ctjs.value)");
    mixed = replaced(mixed,
                     "attributes {upvalue_count = 1 : i32} {\n"
                     "    %state = ctjs.load_upvalue %callee[0]\n"
                     "    %setKey = ctjs.constant",
                     "attributes {upvalue_count = 0 : i32} {\n"
                     "    %setKey = ctjs.constant");
    mixed = replaced(mixed, "%putResult = ctjs.call %putter(%owned)",
                     "%putterEnv = ctjs.load_upvalue %putter[0]\n"
                     "    %putResult = ctjs.call_direct @put$4(%owned, %u, %putter, %putterEnv)");
    refuse(mixed, "partially lifted sibling signatures cannot publish a complete family proof");
    mixed = replaced(mixed, "%putter = ctjs.create_closure %callee[4] this %u captures %cell",
                     "%putter = ctjs.create_closure %callee[4] this %u captures %state");
    refuse(mixed, "raw-resource and original-cell siblings cannot mix capture ownership stages");

    auto parameterized =
        replaced(source, "@put$4(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value)",
                 "@put$4(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, "
                 "%entryKey: !ctjs.value)");
    parameterized =
        replaced(parameterized, "    %entryKey = ctjs.constant #ctjs.string<\"x\">\n", "");
    parameterized = replaced(parameterized, "    %putResult = ctjs.call %putter(%owned)",
                             "    %actual = ctjs.constant #ctjs.string<\"x\">\n"
                             "    %putResult = ctjs.call %putter(%owned, %actual)");
    for (const bool lifted : {false, true}) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            lifted ? prepare(parameterized) : parameterized, &context);
        check(static_cast<bool>(module), "parameterized shared source and prepared forms parse");
        if (!module) { continue; }
        const auto contract = requested(*module);
        OwnedGlobalRoots query(*module, contract);
        if (!query.proved()) {
            std::fprintf(stderr, "parameter owner: %s\n", query.reason().str().c_str());
        }
        check(query.proved(), "a current primitive actual proves the explicit Map parameter");
        if (!query.proved()) { continue; }
        const auto & table = *query.roots().front().methodTable;
        check(table.calls.size() == 2 && table.capturedMap->parameters.size() == 2,
              "parameter proof retains the complete shared method family");
        auto edge = table.calls.front();
        auto setter = module->lookupSymbol<ctjs::FuncOp>("put$4");
        const auto tag = mlir::TypeID::get<ctjs::StringAttr>();
        check(edge.function == setter && edge.arguments.size() == 1 &&
                  edge.arguments.front().parameter ==
                      setter.getBody().front().getArgument(lifted ? 4 : 3) &&
                  edge.arguments.front().actual == edge.call->getOperand(lifted ? 4 : 2) &&
                  edge.arguments.front().primitiveTag == tag &&
                  table.calls.back().arguments.empty() &&
                  table.capturedMap->parameters.back().function == setter &&
                  table.capturedMap->parameters.back().primitiveTags == std::vector{tag},
              "formal/actual SSA evidence separates the Map environment from the explicit key");
        const auto actual = edge.arguments.front().actual;
        const unsigned operand = lifted ? 4u : 2u;
        for (mlir::Value replacement : {edge.read.getResult(), edge.read.getObject()}) {
            edge.call->setOperand(operand, replacement);
            OwnedGlobalRoots stale(*module, contract);
            check(!stale.proved() && stale.reason().contains("fingerprint") &&
                      empty(*module, stale),
                  "changing an actual invalidates its supplied source fingerprint");
            OwnedGlobalRoots fresh(*module, requested(*module));
            check(!fresh.proved() && empty(*module, fresh),
                  "fresh fingerprints cannot turn callable or object actuals into primitives");
        }
        edge.call->setOperand(operand, actual);
        check(OwnedGlobalRoots(*module, contract).proved(),
              "restoring the actual restores the proof");

        mlir::OpBuilder builder(&context);
        builder.setInsertionPoint(edge.call);
        auto different = ctjs::ConstantOp::create(builder, edge.call->getLoc(),
                                                  ctjs::StringAttr::get(&context, "different"));
        edge.call->setOperand(operand, different.getResult());
        OwnedGlobalRoots changed(*module, requested(*module));
        check(changed.proved() &&
                  changed.roots().front().methodTable->calls.front().arguments.front().actual ==
                      different.getResult(),
              "a different key of the same type retains its own live SSA actual");
        edge.call->setOperand(operand, actual);
        different.erase();
        const unsigned completion = query.steps();
        check(completion < 10000, "parameterized family proof remains bounded");
        if (completion < 10000) {
            for (unsigned budget = 0; budget < completion; ++budget) {
                OwnedGlobalRoots limited(*module, contract, budget);
                check(!limited.proved() && limited.exhausted() && empty(*module, limited),
                      "incomplete argument census never publishes a partial owning plan");
            }
            check(OwnedGlobalRoots(*module, contract, completion).proved(),
                  "exact argument census budget reproduces the complete proof");
        }
        std::printf("parameter Map %s proof and all %u incomplete budgets checked\n",
                    lifted ? "prepared" : "source", completion);
    }
    refuse(
        replaced(parameterized, "ctjs.call %putter(%owned, %actual)", "ctjs.call %putter(%owned)"),
        "a missing primitive actual is unproved");
    refuse(replaced(parameterized, "ctjs.call %putter(%owned, %actual)",
                    "ctjs.call %putter(%owned, %actual, %actual)"),
           "a surplus primitive actual is unproved");
    refuse(replaced(parameterized, "    %putResult = ctjs.call %putter(%owned, %actual)", ""),
           "an uncalled parameterized sibling has no independently proved parameter tags");
    refuse(replaced(parameterized, "    %answer = ctjs.call %getter(%owned)",
                    "    %second = ctjs.call %putter(%owned, %u)\n"
                    "    %answer = ctjs.call %getter(%owned)"),
           "all current actuals must agree on each parameter's primitive tag");
    auto fromResult = replaced(parameterized, "    %putResult = ctjs.call %putter(%owned, %actual)",
                               "    %priorGetter = ctjs.get_property %owned[%key]\n"
                               "    %prior = ctjs.call %priorGetter(%owned)\n"
                               "    %putResult = ctjs.call %putter(%owned, %prior)");
    for (const unsigned seeded : {0u, 1u, 2u, 3u, 4u, 5u}) {
        for (const bool lifted : {false, true}) {
            auto sourceResult = fromResult;
            if (seeded) {
                sourceResult =
                    replaced(sourceResult, "    ctjs.return %size",
                             "    %seedKey = ctjs.constant #ctjs.number<0>\n"
                             "    %seedValue = ctjs.constant #ctjs.number<4607182418800017408>\n"
                             "    %seedSetKey = ctjs.constant #ctjs.string<\"set\">\n"
                             "    %seedSet = ctjs.get_property %state[%seedSetKey]\n"
                             "    %seeded = ctjs.call %seedSet(%state, %seedKey, %seedValue)\n"
                             "    %seedGetKey = ctjs.constant #ctjs.string<\"get\">\n"
                             "    %seedGet = ctjs.get_property %state[%seedGetKey]\n"
                             "    %loaded = ctjs.call %seedGet(%state, %seedKey)\n"
                             "    ctjs.return %loaded");
            }
            if (seeded >= 2) {
                std::string mutation =
                    seeded == 2
                        ? "    %other = ctjs.call %seedSet(%state, %seedValue, %seedValue)\n"
                    : seeded == 3
                        ? "    %deleteKey = ctjs.constant #ctjs.string<\"delete\">\n"
                          "    %deleter = ctjs.get_property %state[%deleteKey]\n"
                          "    %deleted = ctjs.call %deleter(%state, %seedValue)\n"
                        : "    %otherKey = ctjs.get_property %state[%key]\n"
                          "    %other = ctjs.call %seedSet(%state, %otherKey, %seedValue)\n";
                if (seeded == 5) {
                    mutation += "    %thirdKey = ctjs.get_property %state[%key]\n"
                                "    %third = ctjs.call %seedSet(%state, %thirdKey, %seedKey)\n";
                }
                sourceResult = replaced(sourceResult, "    %seedGetKey = ctjs.constant",
                                        std::string(mutation) + "    %seedGetKey = ctjs.constant");
            }
            auto program = lifted ? prepare(sourceResult) : sourceResult;
            if (lifted) {
                program = replaced(
                    program, "%prior = ctjs.call %priorGetter(%owned)",
                    "%priorEnv = ctjs.load_upvalue %priorGetter[0]\n"
                    "    %prior = ctjs.call_direct @get$3(%owned, %u, %priorGetter, %priorEnv)");
            }
            auto module = mlir::parseSourceString<mlir::ModuleOp>(program, &context);
            check(static_cast<bool>(module), "source/prepared result-argument fixtures parse");
            if (!module) { continue; }
            const auto contract = requested(*module);
            OwnedGlobalRoots query(*module, contract);
            check(query.proved(),
                  "an independently proved result supplies the consuming formal tag");
            if (!query.proved()) { continue; }
            const auto & calls = query.roots().front().methodTable->calls;
            check(calls.size() == 3 && calls[1].arguments.size() == 1 &&
                      calls[1].arguments.front().actual == calls[0].call->getResult(0) &&
                      calls[1].arguments.front().primitiveTag ==
                          mlir::TypeID::get<ctjs::NumberAttr>() &&
                      calls[0].call->isBeforeInBlock(calls[1].call) &&
                      calls[1].call->isBeforeInBlock(calls[2].call),
                  "initial getter, setter and final getter retain their original SSA order");
            const unsigned completion = query.steps();
            check(completion < 10000, "result dependency proof remains bounded");
            if (completion < 10000) {
                for (unsigned budget = 0; budget < completion; ++budget) {
                    OwnedGlobalRoots limited(*module, contract, budget);
                    check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                              empty(*module, limited),
                          "every incomplete result budget withholds the whole family");
                }
                check(OwnedGlobalRoots(*module, contract, completion).proved(),
                      "the exact result dependency completion budget reproduces all calls");
            }
            auto getter = module->lookupSymbol<ctjs::FuncOp>("get$3");
            auto returned = llvm::cast<ctjs::ReturnOp>(getter.getBody().front().getTerminator());
            const auto saved = returned.getValue();
            mlir::OpBuilder builder(&context);
            builder.setInsertionPoint(returned);
            auto boolean = ctjs::ConstantOp::create(builder, returned.getLoc(),
                                                    ctjs::BooleanAttr::get(&context, true));
            returned->setOperand(0, boolean.getResult());
            OwnedGlobalRoots stale(*module, contract);
            check(!stale.proved() && stale.reason().contains("fingerprint") &&
                      empty(*module, stale),
                  "a producing return mutation invalidates the supplied fingerprint");
            OwnedGlobalRoots changed(*module, requested(*module));
            check(
                changed.proved() &&
                    changed.roots().front().methodTable->calls[1].arguments.front().primitiveTag ==
                        mlir::TypeID::get<ctjs::BooleanAttr>(),
                "a fresh proof rederives the producer's changed tag for the consuming formal");
            returned->setOperand(0, getter.getBody().front().getArgument(0));
            OwnedGlobalRoots external(*module, requested(*module));
            check(!external.proved() && empty(*module, external),
                  "a formerly proved producer cannot authorize an external returned value");
            returned->setOperand(0, saved);
            boolean.erase();
            check(OwnedGlobalRoots(*module, contract).proved(),
                  "restoring the producing body restores its independent result proof");
            std::printf("%s Map %s proof and all %u incomplete budgets checked\n",
                        seeded == 5   ? "repeated alias join"
                        : seeded == 4 ? "possible alias join"
                        : seeded == 3 ? "disjoint delete"
                        : seeded == 2 ? "per-key result"
                        : seeded      ? "seeded result"
                                      : "result",
                        lifted ? "prepared" : "source", completion);
        }
    }
    refuse(replaced(fromResult, "    ctjs.return %size", "    ctjs.return %state"),
           "a Map identity result cannot become a primitive argument");
    refuse(replaced(fromResult, "    %putResult = ctjs.call %putter(%owned, %prior)",
                    "    %seed = ctjs.call %putter(%owned, %actual)\n"
                    "    %putResult = ctjs.call %putter(%owned, %seed)"),
           "a method's result cannot seed its own incomplete parameter family");
    refuse(replaced(prepare(parameterized), "@put$4(%owned, %u, %putter, %putterEnv, %actual)",
                    "@put$4(%owned, %u, %putter, %actual, %putterEnv)"),
           "prepared capture and explicit actual positions are not interchangeable");

    auto distinct =
        replaced(source, "%putter = ctjs.create_closure %callee[4] this %u captures %cell",
                 "%otherState = ctjs.construct %constructor(%constructor)\n"
                 "    %otherCell = ctjs.create_cell %otherState\n"
                 "    %putter = ctjs.create_closure %callee[4] this %u captures %otherCell");
    auto distinctModule = mlir::parseSourceString<mlir::ModuleOp>(distinct, &context);
    check(static_cast<bool>(distinctModule), "distinct sibling Map environment fixture parses");
    if (distinctModule) {
        const auto contract = requested(*distinctModule);
        HostContractAnalysis host(*distinctModule, contract);
        check(host.proved() && host.callables().size() == 2,
              "each separate sibling Map can satisfy its individual live callable proof");
        if (host.proved() && host.callables().size() == 2) {
            check(host.callables()[0].capturedMap && host.callables()[1].capturedMap &&
                      host.callables()[0].capturedMap->allocation !=
                          host.callables()[1].capturedMap->allocation,
                  "the live host census retains distinct sibling allocation identities");
        }
        OwnedGlobalRoots owner(*distinctModule, contract);
        check(!owner.proved() && !owner.exhausted() && empty(*distinctModule, owner),
              "individually valid sibling Maps cannot inherit the one-shared-Map owner plan");
    }
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
        return capture.intrinsic && capture.allocation && !capture.reads.empty() &&
               capture.allocation == table.calls.front().capturedMap->allocation &&
               capture.reads == table.calls.front().capturedMap->reads &&
               capture.calls == table.calls.front().capturedMap->calls &&
               (prepared ? (!capture.cell && !capture.initialization && capture.upvalues.empty() &&
                            capture.argument)
                         : (capture.cell && capture.initialization && !capture.upvalues.empty() &&
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
    constexpr llvm::StringLiteral set = R"MLIR(
    %entryKey = ctjs.constant #ctjs.string<"x">
    %value = ctjs.constant #ctjs.number<4607182418800017408>
    %setKey = ctjs.constant #ctjs.string<"set">
    %setter = ctjs.get_property %state[%setKey]
    %written = ctjs.call %setter(%state, %entryKey, %value)
)MLIR";
    const auto withSet = [&](llvm::StringRef source) {
        return replaced(source.str(), "    %key = ctjs.constant #ctjs.string<\"size\">",
                        set.str() + "    %key = ctjs.constant #ctjs.string<\"size\">");
    };
    const auto mutated = withSet(indirect);
    const auto liftedMutation = withSet(prepared);
    auto importedMutation = replaced(importedCapturedFixture,
                                     "    %2 = ctjs.constant #ctjs.string<\"size\">\n"
                                     "    %3 = ctjs.get_property %1[%2]",
                                     "    %2 = ctjs.constant #ctjs.string<\"set\">\n"
                                     "    %3 = ctjs.get_property %1[%2]\n"
                                     "    %4 = ctjs.constant #ctjs.string<\"x\">\n"
                                     "    %5 = ctjs.constant #ctjs.number<4607182418800017408>\n"
                                     "    %6 = ctjs.call %3(%1, %4, %5)\n"
                                     "    %7 = ctjs.load_upvalue %arg2[0]\n"
                                     "    %8 = ctjs.constant #ctjs.string<\"size\">\n"
                                     "    %9 = ctjs.get_property %7[%8]");
    importedMutation = replaced(importedMutation, "ctjs.return %3", "ctjs.return %9");
    constexpr llvm::StringLiteral actions = R"MLIR(
    %getKey = ctjs.constant #ctjs.string<"get">
    %getMethod = ctjs.get_property %state[%getKey]
    %lookup = ctjs.call %getMethod(%state, %entryKey)
    %hasKey = ctjs.constant #ctjs.string<"has">
    %hasMethod = ctjs.get_property %state[%hasKey]
    %found = ctjs.call %hasMethod(%state, %entryKey)
    %deleteKey = ctjs.constant #ctjs.string<"delete">
    %deleteMethod = ctjs.get_property %state[%deleteKey]
    %removed = ctjs.call %deleteMethod(%state, %entryKey)
)MLIR";
    struct specimen {
        std::string source;
        bool lifted;
        unsigned reads;
        unsigned calls;
    };
    std::vector<specimen> specimens{{capturedFixture, false, 1, 0},
                                    {indirect, false, 1, 0},
                                    {importedCapturedFixture, false, 1, 0},
                                    {prepared, true, 1, 0},
                                    {specialized, true, 1, 0},
                                    {withSet(capturedFixture), false, 2, 1},
                                    {mutated, false, 2, 1},
                                    {importedMutation, false, 2, 1},
                                    {liftedMutation, true, 2, 1},
                                    {withSet(specialized), true, 2, 1}};
    for (const auto & [source, lifted] :
         {std::pair{mutated, false}, std::pair{liftedMutation, true}}) {
        specimens.push_back(
            {replaced(source, "    %key = ctjs.constant #ctjs.string<\"size\">",
                      actions.str() + "    %key = ctjs.constant #ctjs.string<\"size\">"),
             lifted, 5, 4});
        specimens.push_back(
            {replaced(source, "%written = ctjs.call %setter(%state, %entryKey, %value)",
                      "%sizeKey = ctjs.constant #ctjs.string<\"size\">\n"
                      "    %before = ctjs.get_property %state[%sizeKey]\n"
                      "    %written = ctjs.call %setter(%state, %before, %value)"),
             lifted, 3, 1});
        specimens.push_back({replaced(source, "%size = ctjs.get_property %state[%key]",
                                      "%size = ctjs.get_property %written[%key]"),
                             lifted, 2, 1});
    }
    for (const auto & [source, lifted, reads, calls] : specimens) {
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
        const auto & effects = *query.roots().front().methodTable->capturedMap;
        check(effects.reads.size() == reads && effects.calls.size() == calls,
              "captured source ownership records every live standard Map read and call");
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
        std::printf("captured Map %s proof (%u reads, %u calls) and all %u incomplete budgets "
                    "checked\n",
                    lifted ? "prepared" : "source", reads, calls, completion);
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
    for (const auto & source : {mutated, liftedMutation}) {
        refuse(source, "ctjs.call %setter(%state, %entryKey, %value)",
               "ctjs.call %setter(%state, %entryKey)",
               "source and prepared Map effects require the exact builtin arity");
        refuse(source, "ctjs.call %setter(%state, %entryKey, %value)",
               "ctjs.call %setter(%this, %entryKey, %value)",
               "source and prepared Map calls retain the exact method receiver");
        refuse(source, "ctjs.call %setter(%state, %entryKey, %value)",
               "ctjs.call %setter(%state, %entryKey, %state)",
               "Map cycles cannot inherit the primitive captured contents proof");
        refuse(source, "ctjs.call %setter(%state, %entryKey, %value)",
               "ctjs.call %setter(%state, %state, %value)",
               "a Map key cannot retain an owning alias through primitive contents");
        refuse(source, "ctjs.call %setter(%state, %entryKey, %value)",
               "ctjs.call %setter(%state, %entryKey, %setter)",
               "a detached method cannot enter the primitive captured contents");
        refuse(source, "ctjs.return %size", "ctjs.return %written",
               "a Map-valued return needs another owning publication proof");
        refuse(source, "ctjs.return %size", "ctjs.return %setter",
               "detached builtin method values remain outside primitive returns");
        refuse(source, "ctjs.return %size",
               "ctjs.store_global \"escapedMap\", %written\n    ctjs.return %size",
               "a fluent Map alias cannot acquire another global owner");
        refuse(source, "ctjs.return %size",
               "%opaque = ctjs.call %callee(%this)\n    ctjs.return %size",
               "proved Map mutation cannot authorize later opaque or reentrant effects");
        refuse(source, "ctjs.call %setter(%state, %entryKey, %value)",
               "ctjs.call %setter(%state, %entryKey) {ctnative.map_action = \"set\", "
               "ctnative.host_proved = true}",
               "forged native effect markers cannot repair an incomplete source call");
        refuse(source, "#ctjs.string<\"set\">", "#ctjs.string<\"clear\">",
               "other standard methods require their own captured effect boundary");
        refuse(source, "ctjs.create_closure %callee[3]", "ctjs.create_closure %u[3]",
               "effectful source and prepared methods retain their source program identity");
        refuse(source, "ctjs.create_closure %callee[2]", "ctjs.create_closure %u[2]",
               "effectful wrapper factories retain their source program identity");
        refuse(source, "ctjs.return %size",
               "ctjs.set_property %state[%setKey], %setter\n    ctjs.return %size",
               "a captured Map method replacement invalidates later builtin effects");
    }
}
} // namespace

int main() {
    mlir::MLIRContext context;
    context.getOrLoadDialect<ctjs::CTJSDialect>();
    checkCapturedMap(context);
    checkSharedMap(context);
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

    builder.setInsertionPointAfter(table.methods.front().initialization);
    auto * methodWrite = builder.clone(*table.methods.front().initialization.getOperation());
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

    auto * methodTerminator = table.methods.front().function.getBody().front().getTerminator();
    const auto answer = methodTerminator->getOperand(0);
    methodTerminator->setOperand(0,
                                 table.methods.front().function.getBody().front().getArgument(0));
    refused("a receiver-observing getter needs an additional invocation proof");
    methodTerminator->setOperand(0, answer);
    restored();

    builder.setInsertionPointAfter(table.methods.front().initialization);
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
