#pragma once

#include "HostContractFixtures.h"

namespace ctcompile::test::host_contract {

inline void checkDOMJSON(mlir::MLIRContext & context) {
    using namespace ctcompile::ctnative;
    const std::string source = R"MLIR(
module {
  ctjs.func @parse$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %element: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %undefined = ctjs.constant #ctjs.undefined
    %text = ctjs.constant #ctjs.string<"%7B%7D">
    %json = ctjs.load_global "JSON"
    %key = ctjs.constant #ctjs.string<"parse">
    %parse = ctjs.get_property %json[%key]
    %decode = ctjs.load_global "decodeURIComponent"
    %answer = ctjs.invoke {
      %decoded = ctjs.call %decode(%undefined, %text)
      ctjs.invoke_exit %decoded state()
    } normal {
    ^bb0(%decodedText: !ctjs.value):
      %tree = ctjs.invoke {
        %parsed = ctjs.call %parse(%json, %decodedText)
        ctjs.invoke_exit %parsed state()
      } normal {
      ^bb0(%parsedTree: !ctjs.value):
        ctjs.invoke_yield(%parsedTree)
      } unwind {
      ^bb0(%parseError: !ctjs.value):
        ctjs.invoke_yield(%text)
      } : !ctjs.value
      ctjs.invoke_yield(%tree)
    } unwind {
    ^bb0(%decodeError: !ctjs.value):
      ctjs.invoke_yield(%text)
    } : !ctjs.value
    ctjs.return %answer
  }
}
)MLIR";
    auto module = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
    check(static_cast<bool>(module), "DOM JSON chain fixture parses");
    if (!module) { return; }
    HostContract contract;
    contract.entry = "parse$0";
    contract.elementParameters = {0};
    contract.initialIntrinsics = {"JSON", "decodeURIComponent"};
    contract.moduleSha256 = hostContractFingerprint(*module);
    const auto noEvidence = [](mlir::ModuleOp input, const DOMEntryAnalysis & proof) {
        bool empty = !proof.proved() && !proof.entry() && !proof.wrapper() &&
                     proof.parameters().empty() && proof.stringResults().empty() &&
                     proof.optionalStringJoins().empty() && proof.stringRefinements().empty();
        input.walk([&](ctjs::CallOp call) { empty &= !proof.call(call); });
        input.walk([&](ctjs::LoadGlobalOp load) { empty &= !proof.isInitialIntrinsic(load); });
        input.walk([&](ctjs::InvokeOp invoke) { empty &= !proof.invocation(invoke); });
        input.walk([&](ctjs::CreateObjectOp object) { empty &= !proof.jsonObject(object); });
        input.walk([&](ctjs::CopyPropsOp copy) { empty &= !proof.jsonCopy(copy); });
        input.walk([&](ctjs::SetPropertyOp write) { empty &= !proof.jsonAssignment(write); });
        input.walk([&](ctjs::GetPropertyOp read) {
            empty &= !proof.method(read) && !proof.isTokenList(read.getResult());
        });
        return empty;
    };
    for (auto provider :
         {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
        contract.provider = provider;
        DOMEntryAnalysis proof(*module, contract);
        check(proof.proved(), "JSON follows URI success under either DOM provider");
        if (!proof.proved()) {
            std::fprintf(stderr, "%s\n", proof.reason().str().c_str());
            continue;
        }
        unsigned parsers = 0, decoders = 0, invocations = 0;
        module->walk([&](ctjs::CallOp call) {
            const auto * edge = proof.call(call);
            check(edge && edge->element == call.getArgs()[0], "JSON chain keeps exact inputs");
            parsers +=
                edge && edge->returnsJSON() && !edge->returnsBoolean() && !edge->returnsString();
            decoders += edge && edge->returnsString() && !edge->returnsJSON();
        });
        module->walk([&](ctjs::InvokeOp invoke) { invocations += proof.invocation(invoke); });
        check(parsers == 1 && decoders == 1 && invocations == 2,
              "JSON chain proves both distinct fallible calls and continuations");
        check(DOMEntryAnalysis(*module, contract, proof.steps()).proved(),
              "JSON chain reproduces its exact proof budget");
        for (unsigned budget = 0; budget < proof.steps(); ++budget) {
            DOMEntryAnalysis limited(*module, contract, budget);
            check(limited.exhausted() && noEvidence(*module, limited),
                  "every incomplete JSON proof withholds all evidence");
        }
        for (const auto & names :
             {std::vector<std::string>{"decodeURIComponent", "JSON"},
              std::vector<std::string>{"Number", "JSON", "decodeURIComponent"},
              std::vector<std::string>{"JSON", "decodeURIComponent", "Number"}}) {
            auto request = contract;
            request.initialIntrinsics = names;
            check(DOMEntryAnalysis(*module, request).proved(),
                  "JSON identities are independent of declaration order and unused Number");
        }
    }
    for (const auto & names : {std::vector<std::string>{}, std::vector<std::string>{"JSON"},
                               std::vector<std::string>{"decodeURIComponent"},
                               std::vector<std::string>{"JSON", "decodeURIComponent", "JSON"}}) {
        auto request = contract;
        request.initialIntrinsics = names;
        check(noEvidence(*module, DOMEntryAnalysis(*module, request)),
              "typed JSON contract neither infers nor duplicates intrinsic identity");
    }
    const auto refused = [&](const std::string & text) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(input), "JSON capability mutation fixture parses");
        if (!input) { return; }
        auto request = contract;
        request.moduleSha256 = hostContractFingerprint(*input);
        DOMEntryAnalysis proof(*input, request);
        check(noEvidence(*input, proof), "JSON unsupported source publishes no partial evidence");
        check(hostContractFingerprint(*input) == request.moduleSha256,
              "refused JSON proof retains the complete source");
    };
    for (llvm::StringRef args :
         {"%undefined, %decodedText", "%json, %element", "%json, %decodedText, %undefined"}) {
        refused(replaced(source, "%parse(%json, %decodedText)", ("%parse(" + args + ")").str()));
    }
    refused(replaced(source, "ctjs.invoke_yield(%parsedTree)", "ctjs.invoke_yield(%element)"));
    refused(replaced(source, "ctjs.invoke_yield(%text)", "ctjs.invoke_yield(%parseError)"));
    refused(replaced(source, "%json =", "ctjs.store_global \"JSON\", %element\n    %json ="));
    const std::string observation = R"MLIR(
    %kind = ctjs.unary typeof %answer
    %word = ctjs.constant #ctjs.string<"object">
    %same = ctjs.compare eq %word, %kind
    %isObject = ctjs.truthy %same
    %observed = scf.if %isObject -> (!ctjs.value) {
      %tag = ctjs.binary concat %kind, %word
      scf.yield %tag : !ctjs.value
    } else {
      scf.yield %kind : !ctjs.value
    }
    ctjs.return %observed)MLIR";
    const auto checkTypeOf = [&](const std::string & text) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(input), "DOM JSON typeof fixture parses");
        if (!input) { return; }
        auto request = contract;
        request.moduleSha256 = hostContractFingerprint(*input);
        for (auto provider :
             {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
            request.provider = provider;
            DOMEntryAnalysis proof(*input, request);
            check(proof.proved(), "JSON typeof supplies String concatenation and tag equality");
            if (!proof.proved()) {
                std::fprintf(stderr, "%s\n%s\n", text.c_str(), proof.reason().str().c_str());
                continue;
            }
            auto result = llvm::cast<ctjs::ReturnOp>(proof.entry().getBody().front().back());
            check(proof.stringResults().size() == 1 &&
                      proof.stringResults().front() == result.getValue() &&
                      proof.stringRefinements().empty(),
                  "JSON typeof proves a String result without narrowing its JSON operand");
            check(DOMEntryAnalysis(*input, request, proof.steps()).proved(),
                  "JSON typeof reproduces its exact proof budget");
            for (unsigned budget = 0; budget < proof.steps(); ++budget) {
                DOMEntryAnalysis limited(*input, request, budget);
                check(limited.exhausted() && noEvidence(*input, limited),
                      "every incomplete JSON typeof proof withholds all evidence");
            }
            check(hostContractFingerprint(*input) == request.moduleSha256,
                  "JSON typeof proof and budget failures retain the complete source");
        }
    };
    for (llvm::StringRef encoded : {"%7B%7D", "%5B%5D", "null"}) {
        const auto text =
            replaced(replaced(source, "%7B%7D", encoded), "ctjs.return %answer", observation);
        for (llvm::StringRef comparison : {"eq %word, %kind", "eq %kind, %word",
                                           "strict_eq %word, %kind", "strict_eq %kind, %word"}) {
            checkTypeOf(replaced(text, "eq %word, %kind", comparison));
        }
    }
    for (llvm::StringRef primitive :
         {"%primitive = ctjs.constant #ctjs.boolean<true>",
          "%primitive = ctjs.constant #ctjs.number<4631107791820423168>",
          "%primitive = ctjs.constant #ctjs.null",
          "%getKey = ctjs.constant #ctjs.string<\"getAttribute\">\n"
          "    %get = ctjs.get_property %element[%getKey]\n"
          "    %primitive = ctjs.call %get(%element, %key)",
          "%primitive = ctjs.constant #ctjs.undefined"}) {
        const bool supported = !primitive.contains("undefined");
        for (bool reverse : {false, true}) {
            const std::string scalar = reverse ? "%answer" : "%primitive";
            const std::string tree = reverse ? "%primitive" : "%answer";
            const auto text = replaced(source, "ctjs.return %answer", primitive.str() + R"MLIR(
    %zero = ctjs.constant #ctjs.number<0>
    %not = ctjs.unary not %zero
    %condition = ctjs.truthy %zero
    %joined = scf.if %condition -> (!ctjs.value) {
      scf.yield )MLIR" + scalar + R"MLIR( : !ctjs.value
    } else {
      scf.yield )MLIR" + tree + R"MLIR( : !ctjs.value
    }
    ctjs.return %joined)MLIR");
            if (!supported) {
                refused(text);
                continue;
            }
            auto input = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
            check(static_cast<bool>(input), "DOM JSON primitive join fixture parses");
            if (!input) { continue; }
            auto request = contract;
            request.moduleSha256 = hostContractFingerprint(*input);
            DOMEntryAnalysis proof(*input, request);
            check(proof.proved(), "DOM JSON joins preserve both primitive arm orders");
            if (!proof.proved()) { continue; }
            for (unsigned budget = 0; budget < proof.steps(); ++budget) {
                DOMEntryAnalysis limited(*input, request, budget);
                check(limited.exhausted() && noEvidence(*input, limited),
                      "incomplete JSON primitive join proofs publish no evidence");
            }
            const auto observed =
                replaced(text, "ctjs.return %joined", replaced(observation, "%answer", "%joined"));
            checkTypeOf(observed);
            refused(replaced(observed, "ctjs.binary concat %kind, %word",
                             "ctjs.get_property %joined[%word]"));
            for (llvm::StringRef operation :
                 {"ctjs.unary not %joined", "ctjs.truthy %joined", "ctjs.unary plus %joined",
                  "ctjs.get_property %joined[%key]", "ctjs.binary concat %joined, %key",
                  "ctjs.compare strict_eq %joined, %primitive"}) {
                refused(replaced(text, "ctjs.return %joined",
                                 "%observed = " + operation.str() + "\n    ctjs.return %joined"));
            }
            check(hostContractFingerprint(*input) == request.moduleSha256,
                  "JSON primitive proof keeps source and optional producer unchanged");
        }
    }
    const std::string spread = R"MLIR(
    %kind = ctjs.unary typeof %answer
    %word = ctjs.constant #ctjs.string<"object">
    %same = ctjs.compare eq %word, %kind
    %isObject = ctjs.truthy %same
    %selected = scf.if %isObject -> (!ctjs.value) {
      scf.yield %answer : !ctjs.value
    } else {
      %empty = ctjs.create_object
      scf.yield %empty : !ctjs.value
    }
    %target = ctjs.create_object
    ctjs.copy_props %selected into %target
    ctjs.return %target)MLIR";
    const auto spreadSource = [&](const std::string & tail) {
        return replaced(source, "ctjs.return %answer", tail);
    };
    const auto checkSpread = [&](const std::string & tail) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(spreadSource(tail), &context);
        check(static_cast<bool>(input), "DOM JSON spread fixture parses");
        if (!input) { return; }
        auto request = contract;
        request.moduleSha256 = hostContractFingerprint(*input);
        for (auto provider :
             {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
            request.provider = provider;
            DOMEntryAnalysis proof(*input, request);
            check(proof.proved(), "guarded JSON aggregates spread into fresh owning objects");
            if (!proof.proved()) {
                std::fprintf(stderr, "%s\n%s\n", tail.c_str(), proof.reason().str().c_str());
                continue;
            }
            unsigned objects = 0, copies = 0;
            input->walk([&](ctjs::CreateObjectOp object) {
                check(proof.jsonObject(object), "every fresh JSON target has exact evidence");
                ++objects;
            });
            input->walk([&](ctjs::CopyPropsOp copy) {
                check(proof.jsonCopy(copy), "every JSON spread retains its exact operation");
                ++copies;
            });
            input->walk([&](ctjs::SetPropertyOp write) {
                check(proof.jsonAssignment(write), "every JSON assignment keeps its source write");
            });
            check(objects != 0 && copies != 0, "JSON spread fixture exercises both capabilities");
            check(DOMEntryAnalysis(*input, request, proof.steps()).proved(),
                  "JSON spread reproduces its exact proof budget");
            for (unsigned budget = 0; budget < proof.steps(); ++budget) {
                DOMEntryAnalysis limited(*input, request, budget);
                check(limited.exhausted() && noEvidence(*input, limited),
                      "every incomplete JSON spread proof withholds all evidence");
            }
            check(hostContractFingerprint(*input) == request.moduleSha256,
                  "JSON spread proof and budget failures retain the complete source");
        }
    };
    checkSpread(spread);
    checkSpread(replaced(spread, "eq %word, %kind", "strict_eq %kind, %word"));
    checkSpread(replaced(spread, "ctjs.return %target",
                         "ctjs.copy_props %selected into %target\n    ctjs.return %target"));
    checkSpread(replaced(spread, "%target = ctjs.create_object",
                         "%target = ctjs.create_object\n    %early = ctjs.unary typeof %target"));
    checkSpread(replaced(spread, "scf.yield %answer : !ctjs.value",
                         "%local = ctjs.create_object\n"
                         "      ctjs.copy_props %answer into %local\n"
                         "      scf.yield %local : !ctjs.value"));
    auto inverted = replaced(spread, "%isObject = ctjs.truthy %same",
                             "%notObject = ctjs.unary not %same\n"
                             "    %isObject = ctjs.truthy %notObject");
    inverted = replaced(inverted, "scf.yield %answer : !ctjs.value",
                        "%fallback = ctjs.create_object\n"
                        "      scf.yield %fallback : !ctjs.value");
    inverted = replaced(inverted, "%empty = ctjs.create_object\n      scf.yield %empty",
                        "scf.yield %answer");
    checkSpread(inverted);
    checkSpread(replaced(spread, "ctjs.return %target",
                         "ctjs.set_property %target[%word], %text\n    ctjs.return %target"));
    checkSpread(replaced(spread, "ctjs.return %target", R"MLIR(
    scf.if %isObject {
      ctjs.set_property %target[%word], %answer
    }
    ctjs.return %target)MLIR"));
    const auto refuseSpread = [&](const std::string & tail) { refused(spreadSource(tail)); };
    for (llvm::StringRef unproved : {"%answer", "%text", "%element"}) {
        refuseSpread(replaced(spread, "ctjs.copy_props %selected into %target",
                              "ctjs.copy_props " + unproved.str() + " into %target"));
    }
    // The object tag includes null and arrays; it is no permission for arbitrary members.
    refuseSpread(replaced(spread, "ctjs.return %target",
                          "%member = ctjs.get_property %selected[%word]\n"
                          "    ctjs.return %target"));
    for (llvm::StringRef key : {"%kind", "%element"}) {
        refuseSpread(replaced(spread, "ctjs.return %target",
                              "ctjs.set_property %target[" + key.str() +
                                  "], %text\n    ctjs.return %target"));
    }
    refuseSpread(replaced(spread, "ctjs.return %target",
                          "%proto = ctjs.constant #ctjs.string<\"__proto__\">\n"
                          "    ctjs.set_property %target[%proto], %text\n"
                          "    ctjs.return %target"));
    for (llvm::StringRef value : {"%target", "%element", "%undefined"}) {
        refuseSpread(replaced(spread, "ctjs.return %target",
                              "ctjs.set_property %target[%word], " + value.str() +
                                  "\n    ctjs.return %target"));
    }
    refuseSpread(replaced(spread, "ctjs.return %target", R"MLIR(
    %snapshot = ctjs.create_object
    ctjs.copy_props %target into %snapshot
    scf.if %isObject {
      ctjs.set_property %target[%word], %text
    }
    ctjs.return %snapshot)MLIR"));
    refuseSpread(replaced(spread, "ctjs.return %target",
                          "%identity = ctjs.compare strict_eq %target, %selected\n"
                          "    ctjs.return %target"));
    refuseSpread(replaced(spread, "ctjs.copy_props %selected into %target",
                          "ctjs.copy_props %target into %target"));
    refuseSpread(replaced(spread, "ctjs.copy_props %selected into %target",
                          "ctjs.copy_props %target into %selected"));
    refuseSpread(replaced(spread, "scf.yield %answer : !ctjs.value",
                          "%local = ctjs.create_object\n"
                          "      ctjs.copy_props %local into %answer\n"
                          "      scf.yield %answer : !ctjs.value"));
    refuseSpread(replaced(spread, "%empty = ctjs.create_object\n      scf.yield %empty",
                          "scf.yield %answer"));
    refuseSpread(
        replaced(spread, "%empty = ctjs.create_object\n      scf.yield %empty", "scf.yield %text"));
    refuseSpread(replaced(spread, "ctjs.return %target", R"MLIR(
    scf.while : () -> () {
      ctjs.set_property %target[%word], %text
      %snapshot = ctjs.create_object
      ctjs.copy_props %target into %snapshot
      scf.condition(%isObject)
    } do {
      scf.yield
    }
    ctjs.return %target)MLIR"));
    // Deep-value branch joins cannot alias a target that is written afterward.
    const std::string earlyAlias = R"MLIR(
    %alias = scf.if %isObject -> (!ctjs.value) {
      scf.yield %target : !ctjs.value
    } else {
      %other = ctjs.create_object
      scf.yield %other : !ctjs.value
    }
    ctjs.copy_props %selected into %target)MLIR";
    refuseSpread(replaced(replaced(spread, "ctjs.copy_props %selected into %target", earlyAlias),
                          "ctjs.return %target", "ctjs.return %alias"));
    refuseSpread(replaced(spread, "ctjs.return %target",
                          "%later = ctjs.create_object\n"
                          "    ctjs.copy_props %target into %later\n"
                          "    ctjs.copy_props %selected into %target\n"
                          "    ctjs.return %later"));
    refuseSpread(replaced(spread, "ctjs.copy_props %selected into %target", R"MLIR(
    scf.if %isObject {
      ctjs.copy_props %selected into %target
    })MLIR"));
    check(hostContractFingerprint(*module) == contract.moduleSha256,
          "JSON proof and exhausted attempts leave original source unchanged");
    auto branchWrite = mlir::parseSourceString<mlir::ModuleOp>(R"MLIR(
module {
  ctjs.func @parse$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %element: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %undefined = ctjs.constant #ctjs.undefined
    %cell = ctjs.create_cell %undefined
    %true = ctjs.constant #ctjs.boolean<true>
    %condition = ctjs.truthy %true
    scf.if %condition {
      ctjs.cell_set %cell, %undefined
      scf.yield
    }
    %read = ctjs.cell_get %cell
    ctjs.return %read
  }
}
)MLIR",
                                                               &context);
    check(static_cast<bool>(branchWrite), "branch-local capture writer fixture parses");
    if (branchWrite) {
        auto error = expandDOMHelpers(*branchWrite, "parse$0", 100000);
        check(llvm::toString(std::move(error)).find("nonlocal or unordered uses") !=
                  std::string::npos,
              "capture write locality is checked before comparing its order with any read");
    }

    const std::string holderSource = R"MLIR(
module {
  ctjs.func @entry$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %element: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %flag = ctjs.constant #ctjs.boolean<true>
    %condition = ctjs.truthy %flag
    %key = ctjs.constant #ctjs.string<"same">
    %holder = ctjs.create_object
    %helper = ctjs.create_closure %callee[1] this %u
    ctjs.set_property %holder[%key], %helper
    %answer = scf.if %condition -> (!ctjs.value) {
      %method = ctjs.get_property %holder[%key]
      %called = ctjs.call %method(%holder, %element)
      scf.yield %called : !ctjs.value
    } else {
      scf.yield %flag : !ctjs.value
    }
    ctjs.return %answer
  }
  ctjs.func private @same$1(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %element: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %answer = ctjs.compare strict_eq %element, %element
    ctjs.return %answer
  }
}
)MLIR";
    auto holder = mlir::parseSourceString<mlir::ModuleOp>(holderSource, &context);
    check(static_cast<bool>(holder), "guarded callable holder fixture parses");
    if (holder) {
        auto error = expandDOMHelpers(*holder, "entry$0", 100000);
        check(!error, "immutable own callable slots remain known inside a source branch");
        if (error) {
            llvm::consumeError(std::move(error));
        } else {
            auto bound = contract;
            bound.entry = "entry$0";
            bound.moduleSha256 = hostContractFingerprint(*holder);
            for (auto provider : {HostContract::Provider::ctbrowserDOM,
                                  HostContract::Provider::ctbrowserDOMSession}) {
                bound.provider = provider;
                check(DOMEntryAnalysis(*holder, bound).proved(),
                      "guarded callable expansion receives complete DOM reproof");
            }
        }
    }

    const auto protectedHolder = replaced(
        replaced(holderSource, "      %called = ctjs.call %method(%holder, %element)", R"MLIR(
      "ctjs.invoke"() ({
        %called = ctjs.call %method(%holder, %element)
        ctjs.invoke_exit %called state()
      }, {
      ^bb0(%ignored: !ctjs.value):
        ctjs.invoke_yield()
      }, {
      ^bb0(%error: !ctjs.value):
        ctjs.invoke_yield()
      }) : () -> ()
)MLIR"),
        "scf.yield %called : !ctjs.value", "scf.yield %flag : !ctjs.value");
    for (const auto & valid :
         {protectedHolder,
          replaced(protectedHolder, "ctjs.call %method(%holder, %element)",
                   "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
          replaced(protectedHolder, "%answer = ctjs.compare strict_eq %element, %element",
                   "%answer = ctjs.unary typeof %element")}) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(valid, &context);
        check(static_cast<bool>(input), "protected immutable callable fixture parses");
        if (!input) { continue; }
        auto error = expandDOMHelpers(*input, "entry$0", 100000);
        check(!error, "independently inert helper discharges exact unused-result suppression");
        if (error) {
            llvm::consumeError(std::move(error));
            continue;
        }
        bool retired = true;
        input->walk([&](mlir::Operation * operation) {
            retired &=
                !llvm::isa<ctjs::InvokeOp, ctjs::CallOp, ctjs::CallDirectOp, ctjs::CreateClosureOp>(
                    operation);
        });
        check(retired && mlir::succeeded(mlir::verify(*input)),
              "protected expansion never leaves a flattened invalid invocation body");
        auto bound = contract;
        bound.entry = "entry$0";
        bound.moduleSha256 = hostContractFingerprint(*input);
        for (auto provider :
             {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
            bound.provider = provider;
            // typeof is inert for every value, but this DOM proof has no
            // Element typeof contract. Normalization must not grant one.
            const bool typed = valid.find("ctjs.unary typeof") == std::string::npos;
            check(DOMEntryAnalysis(*input, bound).proved() == typed,
                  "discharged inert call still requires complete DOM entry reproof");
        }
    }
    const auto protectedAttribute =
        replaced(protectedHolder, "%answer = ctjs.compare strict_eq %element, %element", R"MLIR(
    %attributeKey = ctjs.constant #ctjs.string<"setAttribute">
    %name = ctjs.constant #ctjs.string<"data-closed">
    %text = ctjs.constant #ctjs.string<"yes">
    %attribute = ctjs.get_property %element[%attributeKey]
    %effect = ctjs.call %attribute(%element, %name, %text)
    %answer = ctjs.create_object)MLIR");
    const auto capturedAttribute = replaced(
        replaced(replaced(protectedAttribute, "%helper = ctjs.create_closure %callee[1] this %u",
                          "%cell = ctjs.create_cell %element\n"
                          "    %helper = ctjs.create_closure %callee[1] this %u captures %cell"),
                 "ctjs.call %method(%holder, %element)", "ctjs.call %method(%holder)"),
        "ctjs.func private @same$1(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, "
        "%element: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {",
        "ctjs.func private @same$1(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> "
        "!ctjs.value attributes {upvalue_count = 1 : i32} {\n"
        "    %element = ctjs.load_upvalue %callee[0]");
    for (const auto & valid : {protectedAttribute, capturedAttribute,
                               replaced(protectedAttribute, "ctjs.call %method(%holder, %element)",
                                        "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                               replaced(protectedAttribute, "data-closed", "bad name")}) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(valid, &context);
        check(static_cast<bool>(input), "protected attribute helper fixture parses");
        if (!input) { continue; }
        auto error = expandDOMHelpers(*input, "entry$0", 100000);
        check(!error, "one effectful attribute call expands with its suppression intact");
        if (error) {
            llvm::consumeError(std::move(error));
            continue;
        }
        unsigned invocations = 0, calls = 0, allocations = 0;
        input->walk([&](ctjs::InvokeOp invoke) {
            ++invocations;
            auto call = llvm::dyn_cast<ctjs::CallOp>(invoke.getBody().front().front());
            auto method = call ? call.getCallee().getDefiningOp<ctjs::GetPropertyOp>()
                               : ctjs::GetPropertyOp{};
            check(method && method->getBlock() == invoke->getBlock() &&
                      method->isBeforeInBlock(invoke) &&
                      ctjs::constantKey(method.getKey()) == "setAttribute" &&
                      call.getReceiver() == method.getObject(),
                  "original attribute call stays protected under the original guard");
        });
        input->walk([&](ctjs::CallOp) { ++calls; });
        input->walk([&](ctjs::CreateObjectOp) { ++allocations; });
        check(invocations == 1 && calls == 1 && allocations == 0 &&
                  mlir::succeeded(mlir::verify(*input)),
              "protected expansion retains call-plus-exit and only elides unused objects");
        auto bound = contract;
        bound.entry = "entry$0";
        bound.moduleSha256 = hostContractFingerprint(*input);
        for (auto provider :
             {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
            bound.provider = provider;
            const bool typed = valid.find("bad name") == std::string::npos;
            DOMEntryAnalysis proof(*input, bound);
            check(proof.proved() == typed,
                  "protected attribute needs complete DOM proof and a valid literal name");
            if (!typed) {
                check(noEvidence(*input, proof), "invalid attribute withholds all evidence");
                continue;
            }
            if (!proof.proved()) {
                std::fprintf(stderr, "%s\n", proof.reason().str().c_str());
                continue;
            }
            input->walk([&](ctjs::InvokeOp invoke) {
                auto call = llvm::cast<ctjs::CallOp>(invoke.getBody().front().front());
                check(proof.invocation(invoke) && proof.call(call) &&
                          proof.call(call)->kind == HostDOMMethod::setAttribute,
                      "typed suppression retains the exact attribute call evidence");
            });
            check(DOMEntryAnalysis(*input, bound, proof.steps()).proved(),
                  "protected attribute reproduces its exact proof budget");
            for (unsigned budget : {0u, proof.steps() - 1}) {
                DOMEntryAnalysis limited(*input, bound, budget);
                check(limited.exhausted() && noEvidence(*input, limited),
                      "incomplete protected attribute proof withholds all evidence");
            }
        }
    }
    for (const auto & invalid :
         {replaced(protectedAttribute, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %text"),
          replaced(protectedAttribute, "%answer = ctjs.create_object",
                   "%second = ctjs.call %attribute(%element, %name, %text)\n"
                   "    %answer = ctjs.create_object"),
          replaced(protectedAttribute, "%answer = ctjs.create_object",
                   "%answer = ctjs.get_property %element[%name]"),
          replaced(protectedAttribute, "%answer = ctjs.create_object",
                   "%answer = ctjs.unary typeof %effect"),
          replaced(protectedAttribute, "ctjs.call %attribute(%element, %name, %text)",
                   "ctjs.call %attribute(%text, %name, %text)"),
          replaced(protectedAttribute, "setAttribute", "unknownMethod")}) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
        check(static_cast<bool>(input), "unsupported protected attribute fixture parses");
        if (!input) { continue; }
        auto error = expandDOMHelpers(*input, "entry$0", 100000);
        check(static_cast<bool>(error),
              "extra calls, object observations, getters and receiver changes refuse");
        if (error) { llvm::consumeError(std::move(error)); }
        check(mlir::succeeded(mlir::verify(*input)),
              "refused attribute expansion retains a valid protected source");
    }
    for (const auto & invalid :
         {replaced(protectedHolder, "    ctjs.set_property %holder[%key], %helper\n", ""),
          replaced(protectedHolder, "      %method =",
                   "      ctjs.set_property %holder[%key], %helper\n      %method ="),
          replaced(protectedHolder, "%answer = ctjs.compare strict_eq %element, %element",
                   "%answer = ctjs.binary add %element, %element"),
          replaced(protectedHolder, "%answer = ctjs.compare strict_eq %element, %element",
                   "%answer = ctjs.get_property %element[%element]"),
          replaced(protectedHolder, "    ctjs.return %answer\n  }\n}",
                   "    ctjs.store_global \"effect\", %element\n"
                   "    ctjs.return %answer\n  }\n}"),
          replaced(protectedHolder, "^bb0(%ignored: !ctjs.value):",
                   "^bb0(%ignored: !ctjs.value):\n"
                   "        ctjs.store_global \"effect\", %ignored"),
          replaced(protectedHolder, "^bb0(%error: !ctjs.value):",
                   "^bb0(%error: !ctjs.value):\n"
                   "        ctjs.store_global \"effect\", %error"),
          replaced(protectedHolder, "^bb0(%error: !ctjs.value):",
                   "^bb0(%error: !ctjs.value):\n"
                   "        %escaped = ctjs.get_property %holder[%key]"),
          replaced(
              replaced(protectedHolder, "state()", "state(%element)"),
              "^bb0(%error: !ctjs.value):", "^bb0(%error: !ctjs.value, %state: !ctjs.value):")}) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
        check(static_cast<bool>(input), "unproved protected helper fixture parses");
        if (!input) { continue; }
        auto error = expandDOMHelpers(*input, "entry$0", 100000);
        check(static_cast<bool>(error),
              "coercions, getters, effects, observed continuations and unwind state refuse");
        if (error) { llvm::consumeError(std::move(error)); }
        unsigned invocations = 0;
        input->walk([&](ctjs::InvokeOp) { ++invocations; });
        check(invocations == 1 && mlir::succeeded(mlir::verify(*input)),
              "refused protected expansion retains its valid suppression region");
    }
    for (unsigned budget : {0U, 64U, 128U}) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(protectedHolder, &context);
        check(static_cast<bool>(input), "protected helper budget fixture parses");
        if (!input) { continue; }
        auto error = expandDOMHelpers(*input, "entry$0", budget);
        check(llvm::toString(std::move(error)).find("budget") != std::string::npos,
              "incomplete protected helper proof refuses within its existing budget");
    }

    const std::string slot = "    ctjs.set_property %holder[%key], %helper\n";
    for (const auto & invalid :
         {replaced(holderSource, slot, ""),
          replaced(replaced(holderSource, slot, ""), "      %method =", slot + "      %method ="),
          replaced(replaced(holderSource, slot, ""), "    ctjs.return %answer",
                   slot + "    ctjs.return %answer"),
          replaced(holderSource, "      %method =", slot + "      %method ="),
          replaced(holderSource, "%method(%holder, %element)", "%method(%element, %element)")}) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
        check(static_cast<bool>(input), "invalid guarded callable holder fixture parses");
        if (!input) { continue; }
        auto error = expandDOMHelpers(*input, "entry$0", 100000);
        check(static_cast<bool>(error),
              "missing, conditional, late or replaced slots and wrong receivers refuse");
        if (error) { llvm::consumeError(std::move(error)); }
    }
}

} // namespace ctcompile::test::host_contract
