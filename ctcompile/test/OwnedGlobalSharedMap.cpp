// Owned global method tables: the shared two- and three-method Map family -
// source and prepared forms, parameterized siblings, result-fed actuals, the
// seeded/per-key/alias-join variants, and every refusal control.
//
// SPLIT 2026-09-08: this is checkSharedMap, verbatim, out of a 1,099-line
// test/OwnedGlobalMethods.cpp; the fixtures it reads are in
// OwnedGlobalMethodsFixtures.h beside this.

#include "OwnedGlobalMethodsFixtures.h"

using namespace ctcompile::test::owned_global_methods;

namespace {

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

} // namespace

int main() {
    mlir::MLIRContext context;
    context.getOrLoadDialect<ctjs::CTJSDialect>();
    checkSharedMap(context);
    if (failures == 0) { std::puts("owned global shared Map proofs passed"); }
    return failures == 0 ? 0 : 1;
}
