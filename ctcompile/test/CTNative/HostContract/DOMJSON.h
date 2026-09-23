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
    const auto readThenWrite = [&](const std::string & source) {
        return replaced(source, "%effect = ctjs.call %attribute(%element, %name, %text)", R"MLIR(
    %readKey = ctjs.constant #ctjs.string<"hasAttribute">
    %readName = ctjs.constant #ctjs.string<"data-visited">
    %readMethod = ctjs.get_property %element[%readKey]
    %present = ctjs.call %readMethod(%element, %readName)
    %effect = ctjs.call %attribute(%element, %name, %present))MLIR");
    };
    const auto protectedRead = readThenWrite(protectedAttribute);
    const auto readAfterWrite = [&](const std::string & source) {
        return replaced(source, "%answer = ctjs.create_object", R"MLIR(
    %afterReadKey = ctjs.constant #ctjs.string<"hasAttribute">
    %afterReadName = ctjs.constant #ctjs.string<"data-closed">
    %afterReadMethod = ctjs.get_property %element[%afterReadKey]
    %afterPresent = ctjs.call %afterReadMethod(%element, %afterReadName)
    %answer = ctjs.create_object)MLIR");
    };
    const auto trailingRead = readAfterWrite(protectedRead);
    const auto writeAfterRead = [&](const std::string & source) {
        return replaced(replaced(source, "%afterReadKey =", R"MLIR(
    %secondKey = ctjs.constant #ctjs.string<"setAttribute">
    %secondName = ctjs.constant #ctjs.string<"data-closed">
    %secondMethod = ctjs.get_property %element[%secondKey]
    %afterReadKey =)MLIR"),
                        "%answer = ctjs.create_object", R"MLIR(
    %secondEffect = ctjs.call %secondMethod(%element, %secondName, %afterPresent)
    %answer = ctjs.create_object)MLIR");
    };
    const auto secondWrite = writeAfterRead(trailingRead);
    const auto selectorRead = replaced(secondWrite, "%answer = ctjs.create_object", R"MLIR(
    %selectorKey = ctjs.constant #ctjs.string<"matches">
    %selectorText = ctjs.constant #ctjs.string<"[data-closed]">
    %selectorMethod = ctjs.get_property %element[%selectorKey]
    %selected = ctjs.call %selectorMethod(%element, %selectorText)
    %answer = ctjs.create_object)MLIR");
    const auto finalWrite = replaced(selectorRead, "%answer = ctjs.create_object", R"MLIR(
    %finalKey = ctjs.constant #ctjs.string<"setAttribute">
    %finalName = ctjs.constant #ctjs.string<"data-final">
    %finalValue = ctjs.constant #ctjs.boolean<false>
    %finalMethod = ctjs.get_property %element[%finalKey]
    %finalEffect = ctjs.call %finalMethod(%element, %finalName, %finalValue)
    %answer = ctjs.create_object)MLIR");
    const auto selectedWrite =
        replaced(finalWrite, "ctjs.call %finalMethod(%element, %finalName, %finalValue)",
                 "ctjs.call %finalMethod(%element, %finalName, %selected)");
    const auto finalRead = replaced(replaced(finalWrite, "%finalEffect = ctjs.call", R"MLIR(
    %finalReadKey = ctjs.constant #ctjs.string<"hasAttribute">
    %finalReadName = ctjs.constant #ctjs.string<"data-closed">
    %finalReadMethod = ctjs.get_property %element[%finalReadKey]
    %finalPresent = ctjs.call %finalReadMethod(%element, %finalReadName)
    %finalEffect = ctjs.call)MLIR"),
                                    "ctjs.call %finalMethod(%element, %finalName, %finalValue)",
                                    "ctjs.call %finalMethod(%element, %finalName, %finalPresent)");
    const auto appendWrite = [&](const std::string & source, const std::string & pattern,
                                 llvm::StringRef label) {
        const auto start = pattern.find("    %finalKey =");
        auto suffix =
            pattern.substr(start, pattern.find("    %answer = ctjs.create_object") - start);
        while (suffix.find("final") != std::string::npos) {
            suffix = replaced(suffix, "final", label);
        }
        return replaced(source, "    %answer = ctjs.create_object",
                        suffix + "    %answer = ctjs.create_object");
    };
    const auto fourthRead = appendWrite(finalRead, finalRead, "fourth");
    const auto fourthWrite = appendWrite(finalRead, finalWrite, "fourth");
    const auto fifthRead = appendWrite(fourthRead, finalRead, "fifth");
    const auto terminalRead = replaced(fourthRead, "%answer = ctjs.create_object", R"MLIR(
    %terminalReadKey = ctjs.constant #ctjs.string<"hasAttribute">
    %terminalReadName = ctjs.constant #ctjs.string<"data-terminal">
    %terminalReadMethod = ctjs.get_property %element[%terminalReadKey]
    %terminalPresent = ctjs.call %terminalReadMethod(%element, %terminalReadName)
    %answer = ctjs.create_object)MLIR");
    const auto terminalMatch = replaced(
        replaced(terminalRead, "%terminalReadKey = ctjs.constant #ctjs.string<\"hasAttribute\">",
                 "%terminalReadKey = ctjs.constant #ctjs.string<\"matches\">"),
        "data-terminal", "[data-terminal]");
    const auto terminalMatchRead = replaced(terminalMatch, "%answer = ctjs.create_object", R"MLIR(
    %afterTerminalKey = ctjs.constant #ctjs.string<"hasAttribute">
    %afterTerminalName = ctjs.constant #ctjs.string<"data-after-terminal">
    %afterTerminalMethod = ctjs.get_property %element[%afterTerminalKey]
    %afterTerminalPresent = ctjs.call %afterTerminalMethod(%element, %afterTerminalName)
    %answer = ctjs.create_object)MLIR");
    const auto terminalMatchReads =
        replaced(terminalMatchRead, "%answer = ctjs.create_object", R"MLIR(
    %secondTerminalKey = ctjs.constant #ctjs.string<"hasAttribute">
    %secondTerminalName = ctjs.constant #ctjs.string<"data-second-terminal">
    %secondTerminalMethod = ctjs.get_property %element[%secondTerminalKey]
    %secondTerminalPresent = ctjs.call %secondTerminalMethod(%element, %secondTerminalName)
    %answer = ctjs.create_object)MLIR");
    const auto alternatingTerminal =
        replaced(terminalMatchReads, "%answer = ctjs.create_object", R"MLIR(
    %alternateTerminalKey = ctjs.constant #ctjs.string<"matches">
    %alternateTerminalName = ctjs.constant #ctjs.string<"[data-alternate-terminal]">
    %alternateTerminalMethod = ctjs.get_property %element[%alternateTerminalKey]
    %alternateTerminalPresent = ctjs.call %alternateTerminalMethod(%element, %alternateTerminalName)
    %lastTerminalKey = ctjs.constant #ctjs.string<"hasAttribute">
    %lastTerminalName = ctjs.constant #ctjs.string<"data-last-terminal">
    %lastTerminalMethod = ctjs.get_property %element[%lastTerminalKey]
    %lastTerminalPresent = ctjs.call %lastTerminalMethod(%element, %lastTerminalName)
    %answer = ctjs.create_object)MLIR");
    const auto terminalReadWrite = appendWrite(terminalMatchReads, finalWrite, "late");
    const auto terminalReadValue = appendWrite(terminalMatchReads, finalRead, "late");
    const auto alternatingReadValue = appendWrite(alternatingTerminal, finalRead, "late");
    const auto earlierTerminalValue =
        replaced(terminalReadWrite, "ctjs.call %lateMethod(%element, %lateName, %lateValue)",
                 "ctjs.call %lateMethod(%element, %lateName, %afterTerminalPresent)");
    const auto secondTerminalValue =
        replaced(terminalReadWrite, "ctjs.call %lateMethod(%element, %lateName, %lateValue)",
                 "ctjs.call %lateMethod(%element, %lateName, %secondTerminalPresent)");
    const auto alternatingEarlierValue =
        replaced(appendWrite(alternatingTerminal, finalWrite, "late"),
                 "ctjs.call %lateMethod(%element, %lateName, %lateValue)",
                 "ctjs.call %lateMethod(%element, %lateName, %afterTerminalPresent)");
    const auto terminalSelectorValue = replaced(
        earlierTerminalValue, "ctjs.call %lateMethod(%element, %lateName, %afterTerminalPresent)",
        "ctjs.call %lateMethod(%element, %lateName, %terminalPresent)");
    const auto alternatingSelectorValue =
        replaced(alternatingEarlierValue,
                 "ctjs.call %lateMethod(%element, %lateName, %afterTerminalPresent)",
                 "ctjs.call %lateMethod(%element, %lateName, %terminalPresent)");
    const auto lastSelectorValue =
        replaced(alternatingEarlierValue,
                 "ctjs.call %lateMethod(%element, %lateName, %afterTerminalPresent)",
                 "ctjs.call %lateMethod(%element, %lateName, %alternateTerminalPresent)");
    const auto savedReadStart = earlierTerminalValue.find("    %afterTerminalKey =");
    const auto savedRead = earlierTerminalValue.substr(
        savedReadStart, earlierTerminalValue.find("    %secondTerminalKey =") - savedReadStart);
    const auto readBeforeSelector =
        replaced(replaced(earlierTerminalValue, savedRead, ""),
                 "    %terminalReadKey =", savedRead + "    %terminalReadKey =");
    const auto readBeforeFirstSelector =
        replaced(replaced(earlierTerminalValue, savedRead, ""),
                 "    %selectorKey =", savedRead + "    %selectorKey =");
    const auto alternatingReadBeforeFirstSelector =
        replaced(replaced(alternatingEarlierValue, savedRead, ""),
                 "    %selectorKey =", savedRead + "    %selectorKey =");
    const auto readBeforeSecondWrite = replaced(replaced(earlierTerminalValue, savedRead, ""),
                                                "    %secondKey =", savedRead + "    %secondKey =");
    const auto alternatingReadBeforeSecondWrite =
        replaced(replaced(alternatingEarlierValue, savedRead, ""),
                 "    %secondKey =", savedRead + "    %secondKey =");
    const auto readBeforeFirstWrite = replaced(replaced(earlierTerminalValue, savedRead, ""),
                                               "    %attribute =", savedRead + "    %attribute =");
    const auto alternatingReadBeforeFirstWrite =
        replaced(replaced(alternatingEarlierValue, savedRead, ""),
                 "    %attribute =", savedRead + "    %attribute =");
    const auto readInsideFirstWrite = replaced(replaced(earlierTerminalValue, savedRead, ""),
                                               "    %readKey =", savedRead + "    %readKey =");
    const auto readInsideSecondWrite =
        replaced(replaced(earlierTerminalValue, savedRead, ""),
                 "    %afterReadKey =", savedRead + "    %afterReadKey =");
    const auto readAfterFirstFeeding = replaced(replaced(earlierTerminalValue, savedRead, ""),
                                                "    %effect =", savedRead + "    %effect =");
    const auto readAfterSecondFeeding =
        replaced(replaced(earlierTerminalValue, savedRead, ""),
                 "    %secondEffect =", savedRead + "    %secondEffect =");
    const auto readThroughFinalArgument =
        replaced(terminalReadValue, "ctjs.call %lateMethod(%element, %lateName, %latePresent)",
                 "ctjs.call %lateMethod(%element, %lateName, %afterTerminalPresent)");
    const auto earlyReadThroughFinalArgument =
        replaced(replaced(readThroughFinalArgument, savedRead, ""),
                 "    %effect =", savedRead + "    %effect =");
    const auto selectorThroughFinalArgument =
        replaced(terminalReadValue, "ctjs.call %lateMethod(%element, %lateName, %latePresent)",
                 "ctjs.call %lateMethod(%element, %lateName, %terminalPresent)");
    const auto alternateSelectorThroughFinalArgument =
        replaced(alternatingReadValue, "ctjs.call %lateMethod(%element, %lateName, %latePresent)",
                 "ctjs.call %lateMethod(%element, %lateName, %alternateTerminalPresent)");
    const auto selectorInsideFinalArgument = replaced(
        replaced(selectorThroughFinalArgument, "#ctjs.string<\"hasAttribute\">\n    %lateReadName",
                 "#ctjs.string<\"matches\">\n    %lateReadName"),
        "%lateReadName = ctjs.constant #ctjs.string<\"data-closed\">",
        "%lateReadName = ctjs.constant #ctjs.string<\"[data-late-argument]\">");
    const auto readThroughFinalSelector =
        replaced(selectorInsideFinalArgument, "%lateName, %terminalPresent)",
                 "%lateName, %afterTerminalPresent)");
    const auto firstWriteSelector =
        replaced(replaced(finalRead, "%readKey = ctjs.constant #ctjs.string<\"hasAttribute\">",
                          "%readKey = ctjs.constant #ctjs.string<\"matches\">"),
                 "data-visited", "[data-visited]");
    const auto secondWriteSelector =
        replaced(replaced(finalRead, "%afterReadKey = ctjs.constant #ctjs.string<\"hasAttribute\">",
                          "%afterReadKey = ctjs.constant #ctjs.string<\"matches\">"),
                 "%afterReadName = ctjs.constant #ctjs.string<\"data-closed\">",
                 "%afterReadName = ctjs.constant #ctjs.string<\"[data-closed]\">");
    const auto firstSelectorWithSavedRead = replaced(
        replaced(readAfterFirstFeeding, "%readKey = ctjs.constant #ctjs.string<\"hasAttribute\">",
                 "%readKey = ctjs.constant #ctjs.string<\"matches\">"),
        "data-visited", "[data-visited]");
    const auto ignoredFirstSelector = replaced(
        replaced(firstSelectorWithSavedRead, "ctjs.call %attribute(%element, %name, %present)",
                 "ctjs.call %attribute(%element, %name, %afterTerminalPresent)"),
        "ctjs.call %lateMethod(%element, %lateName, %afterTerminalPresent)",
        "ctjs.call %lateMethod(%element, %lateName, %lateValue)");
    const auto moveReadBefore = [&](const std::string & source, llvm::StringRef start,
                                    llvm::StringRef end, llvm::StringRef before) {
        const auto first = source.find(start.str());
        const auto read = source.substr(first, source.find(end.str(), first) - first);
        return replaced(replaced(source, read, ""), before, read + before.str());
    };
    const auto selectorBeforeFirstWrite =
        moveReadBefore(firstWriteSelector, "    %readKey =", "    %effect =", "    %attribute =");
    const auto selectorBeforeSecondWrite = moveReadBefore(
        secondWriteSelector, "    %afterReadKey =", "    %secondEffect =", "    %secondMethod =");
    const auto earlySavedSelector =
        moveReadBefore(selectorThroughFinalArgument,
                       "    %terminalReadKey =", "    %afterTerminalKey =", "    %attribute =");
    const auto ignoredEarlySelector =
        replaced(earlySavedSelector, "%lateName, %terminalPresent)", "%lateName, %latePresent)");
    const auto reusedInitialSelector = replaced(
        selectorBeforeFirstWrite, "ctjs.call %secondMethod(%element, %secondName, %afterPresent)",
        "ctjs.call %secondMethod(%element, %secondName, %present)");
    const auto reusedArgumentSelector = replaced(
        firstWriteSelector, "ctjs.call %secondMethod(%element, %secondName, %afterPresent)",
        "ctjs.call %secondMethod(%element, %secondName, %present)");
    const auto reusedInitialRead =
        replaced(secondWrite, "ctjs.call %secondMethod(%element, %secondName, %afterPresent)",
                 "ctjs.call %secondMethod(%element, %secondName, %present)");
    const auto reusedSecondSelector = replaced(
        selectorBeforeSecondWrite, "ctjs.call %finalMethod(%element, %finalName, %finalPresent)",
        "ctjs.call %finalMethod(%element, %finalName, %afterPresent)");
    const auto secondReadStart = reusedInitialRead.find("    %afterReadKey =");
    const auto secondRead = reusedInitialRead.substr(
        secondReadStart, reusedInitialRead.find("    %secondEffect =") - secondReadStart);
    const auto conditionalRead =
        replaced(replaced(replaced(reusedInitialRead, secondRead, ""), "    %secondKey =",
                          "    %closeCondition = ctjs.truthy %present\n"
                          "    scf.if %closeCondition {\n    %secondKey ="),
                 "    %answer = ctjs.create_object",
                 "    scf.yield\n    }\n    %answer = ctjs.create_object");
    const auto conditionalSelector =
        moveReadBefore(replaced(replaced(conditionalRead, "#ctjs.string<\"hasAttribute\">",
                                         "#ctjs.string<\"matches\">"),
                                "data-visited", "[data-visited]"),
                       "    %readKey =", "    %effect =", "    %attribute =");
    const auto conditionalDistinct =
        replaced(replaced(conditionalSelector, "    %attribute =", secondRead + "    %attribute ="),
                 "ctjs.call %secondMethod(%element, %secondName, %present)",
                 "ctjs.call %secondMethod(%element, %secondName, %afterPresent)");
    const auto conditionalIgnoredRead =
        replaced(conditionalSelector, "    %secondKey =", secondRead + "    %secondKey =");
    const auto conditionalBranchRead =
        replaced(conditionalIgnoredRead, "ctjs.call %secondMethod(%element, %secondName, %present)",
                 "ctjs.call %secondMethod(%element, %secondName, %afterPresent)");
    const auto conditionalArgumentRead = moveReadBefore(
        conditionalBranchRead, "    %afterReadKey =", "    %secondKey =", "    %secondEffect =");
    const auto conditionalBranchSelector =
        replaced(replaced(conditionalArgumentRead,
                          "%afterReadKey = ctjs.constant #ctjs.string<\"hasAttribute\">",
                          "%afterReadKey = ctjs.constant #ctjs.string<\"matches\">"),
                 "%afterReadName = ctjs.constant #ctjs.string<\"data-closed\">",
                 "%afterReadName = ctjs.constant #ctjs.string<\"[data-closed]\">");
    const auto conditionalIgnoredSelector = replaced(
        conditionalBranchSelector, "ctjs.call %secondMethod(%element, %secondName, %afterPresent)",
        "ctjs.call %secondMethod(%element, %secondName, %present)");
    const auto conditionalTwoReads =
        replaced(conditionalArgumentRead, "    %secondEffect =",
                 "    %anotherReadMethod = ctjs.get_property %element[%afterReadKey]\n"
                 "    %anotherPresent = ctjs.call %anotherReadMethod(%element, %afterReadName)\n"
                 "    %secondEffect =");
    const auto conditionalLastRead = replaced(
        conditionalTwoReads, "ctjs.call %secondMethod(%element, %secondName, %afterPresent)",
        "ctjs.call %secondMethod(%element, %secondName, %anotherPresent)");
    const auto conditionalMixedReads =
        replaced(replaced(conditionalLastRead, "    %anotherReadMethod =",
                          "    %anotherReadKey = ctjs.constant #ctjs.string<\"matches\">\n"
                          "    %anotherReadName = ctjs.constant #ctjs.string<\"[data-next]\">\n"
                          "    %anotherReadMethod ="),
                 "ctjs.get_property %element[%afterReadKey]\n    %anotherPresent = ctjs.call "
                 "%anotherReadMethod(%element, %afterReadName)",
                 "ctjs.get_property %element[%anotherReadKey]\n    %anotherPresent = ctjs.call "
                 "%anotherReadMethod(%element, %anotherReadName)");
    const auto conditionalEarlySelectors =
        replaced(replaced(conditionalTwoReads,
                          "%afterReadKey = ctjs.constant #ctjs.string<\"hasAttribute\">",
                          "%afterReadKey = ctjs.constant #ctjs.string<\"matches\">"),
                 "%afterReadName = ctjs.constant #ctjs.string<\"data-closed\">",
                 "%afterReadName = ctjs.constant #ctjs.string<\"[data-closed]\">");
    const auto conditionalThreeReads =
        replaced(conditionalMixedReads, "    %secondEffect =",
                 "    %thirdReadMethod = ctjs.get_property %element[%afterReadKey]\n"
                 "    %thirdPresent = ctjs.call %thirdReadMethod(%element, %afterReadName)\n"
                 "    %secondEffect =");
    const auto conditionalPostRead =
        moveReadBefore(conditionalIgnoredRead,
                       "    %afterReadKey =", "    %secondKey =", "    scf.yield\n    }\n");
    const auto conditionalPostSecondRead =
        moveReadBefore(conditionalTwoReads,
                       "    %anotherReadMethod =", "    %secondEffect =", "    scf.yield\n    }\n");
    const auto conditionalPostSelector =
        replaced(replaced(conditionalPostRead,
                          "%afterReadKey = ctjs.constant #ctjs.string<\"hasAttribute\">",
                          "%afterReadKey = ctjs.constant #ctjs.string<\"matches\">"),
                 "%afterReadName = ctjs.constant #ctjs.string<\"data-closed\">",
                 "%afterReadName = ctjs.constant #ctjs.string<\"[data-closed]\">");
    const auto conditionalPostReads =
        moveReadBefore(replaced(conditionalThreeReads,
                                "ctjs.call %secondMethod(%element, %secondName, %anotherPresent)",
                                "ctjs.call %secondMethod(%element, %secondName, %present)"),
                       "    %afterReadKey =", "    %secondEffect =", "    scf.yield\n    }\n");
    const auto conditionalMixedPostReads =
        moveReadBefore(replaced(conditionalThreeReads,
                                "ctjs.call %secondMethod(%element, %secondName, %anotherPresent)",
                                "ctjs.call %secondMethod(%element, %secondName, %afterPresent)"),
                       "    %anotherReadKey =", "    %secondEffect =", "    scf.yield\n    }\n");
    const auto conditionalThirdWrite =
        replaced(replaced(conditionalPostRead, "    %afterReadKey =",
                          "    %thirdName = ctjs.constant #ctjs.string<\"data-after-second\">\n"
                          "    %thirdMethod = ctjs.get_property %element[%secondKey]\n"
                          "    %afterReadKey ="),
                 "    scf.yield\n    }\n",
                 "    %thirdEffect = ctjs.call %thirdMethod(%element, %thirdName, %afterPresent)\n"
                 "    scf.yield\n    }\n");
    const auto conditionalThirdSavedWrite = replaced(
        conditionalThirdWrite, "ctjs.call %thirdMethod(%element, %thirdName, %afterPresent)",
        "ctjs.call %thirdMethod(%element, %thirdName, %present)");
    const auto conditionalThirdSelector =
        replaced(replaced(conditionalThirdWrite,
                          "%afterReadKey = ctjs.constant #ctjs.string<\"hasAttribute\">",
                          "%afterReadKey = ctjs.constant #ctjs.string<\"matches\">"),
                 "%afterReadName = ctjs.constant #ctjs.string<\"data-closed\">",
                 "%afterReadName = ctjs.constant #ctjs.string<\"[data-closed]\">");
    const auto conditionalFourthWrite = replaced(
        conditionalThirdWrite, "    scf.yield\n    }\n",
        "    %fourthName = ctjs.constant #ctjs.string<\"data-fourth\">\n"
        "    %fourthMethod = ctjs.get_property %element[%secondKey]\n"
        "    %fourthEffect = ctjs.call %fourthMethod(%element, %fourthName, %afterPresent)\n"
        "    scf.yield\n    }\n");
    const auto conditionalNestedWrite =
        replaced(replaced(conditionalThirdWrite, "    %thirdName =",
                          "    %guardReadKey = ctjs.constant #ctjs.string<\"hasAttribute\">\n"
                          "    %guardReadName = ctjs.constant #ctjs.string<\"data-third-guard\">\n"
                          "    %guardMethod = ctjs.get_property %element[%guardReadKey]\n"
                          "    %guardPresent = ctjs.call %guardMethod(%element, %guardReadName)\n"
                          "    %nestedCondition = ctjs.truthy %guardPresent\n"
                          "    scf.if %nestedCondition {\n    %thirdName ="),
                 "    scf.yield\n    }\n", "    scf.yield\n    }\n    scf.yield\n    }\n");
    const auto conditionalNestedSelector =
        replaced(replaced(conditionalNestedWrite,
                          "%guardReadKey = ctjs.constant #ctjs.string<\"hasAttribute\">",
                          "%guardReadKey = ctjs.constant #ctjs.string<\"matches\">"),
                 "#ctjs.string<\"data-third-guard\">", "#ctjs.string<\"[data-third-guard]\">");
    const auto conditionalDeeperWrite =
        replaced(replaced(conditionalNestedWrite, "    %thirdName =",
                          "    %deeperCondition = ctjs.truthy %guardPresent\n"
                          "    scf.if %deeperCondition {\n    %thirdName ="),
                 "    scf.yield\n    }\n", "    scf.yield\n    }\n    scf.yield\n    }\n");
    const auto conditionalNestedElse =
        replaced(conditionalNestedWrite, "    scf.yield\n    }\n",
                 "    scf.yield\n    } else {\n    scf.yield\n    }\n");
    const auto conditionalElseWrite =
        replaced(conditionalNestedElse, "    } else {\n    scf.yield",
                 "    } else {\n"
                 "    %elseName = ctjs.constant #ctjs.string<\"data-else\">\n"
                 "    %elseValue = ctjs.constant #ctjs.boolean<false>\n"
                 "    %elseMethod = ctjs.get_property %element[%secondKey]\n"
                 "    %elseEffect = ctjs.call %elseMethod(%element, %elseName, %elseValue)\n"
                 "    scf.yield");
    const auto conditionalElseRead =
        replaced(replaced(conditionalElseWrite, "    %elseMethod =",
                          "    %elseReadKey = ctjs.constant #ctjs.string<\"hasAttribute\">\n"
                          "    %elseReadName = ctjs.constant #ctjs.string<\"data-else-read\">\n"
                          "    %elseReadMethod = ctjs.get_property %element[%elseReadKey]\n"
                          "    %elsePresent = ctjs.call %elseReadMethod(%element, %elseReadName)\n"
                          "    %elseMethod ="),
                 "ctjs.call %elseMethod(%element, %elseName, %elseValue)",
                 "ctjs.call %elseMethod(%element, %elseName, %elsePresent)");
    const auto conditionalElseSelector = replaced(
        replaced(conditionalElseRead, "%elseReadKey = ctjs.constant #ctjs.string<\"hasAttribute\">",
                 "%elseReadKey = ctjs.constant #ctjs.string<\"matches\">"),
        "#ctjs.string<\"data-else-read\">", "#ctjs.string<\"[data-else-read]\">");
    const auto conditionalElseNested =
        replaced(replaced(conditionalElseRead, "    %elseMethod =",
                          "    %elseCondition = ctjs.truthy %elsePresent\n"
                          "    scf.if %elseCondition {\n    %elseMethod ="),
                 "    %elseEffect = ctjs.call %elseMethod(%element, %elseName, %elsePresent)\n",
                 "    %elseEffect = ctjs.call %elseMethod(%element, %elseName, %elsePresent)\n"
                 "    scf.yield\n    }\n");
    const auto singleTerminalValue =
        replaced(terminalMatchRead, "%answer = ctjs.create_object",
                 "%lateMethod = ctjs.get_property %element[%finalKey]\n"
                 "    %late = ctjs.call %lateMethod(%element, %name, %afterTerminalPresent)\n"
                 "    %answer = ctjs.create_object");
    const auto alternatingTerminalWrite =
        replaced(alternatingTerminal, "%answer = ctjs.create_object",
                 "%lateMethod = ctjs.get_property %element[%finalKey]\n"
                 "    %late = ctjs.call %lateMethod(%element, %name, %text)\n"
                 "    %answer = ctjs.create_object");
    const auto discardedState =
        replaced(protectedAttribute, "%answer = ctjs.create_object",
                 "%answer = ctjs.create_object\n    ctjs.set_property %answer[%name], %text");
    const auto scalarState = replaced(discardedState, "%effect = ctjs.call", R"MLIR(
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %advanced = ctjs.binary add %one, %one
    %effect = ctjs.call)MLIR");
    for (const auto & [valid, typed] : {
             std::pair{protectedAttribute, true},
             std::pair{replaced(protectedAttribute, "%answer = ctjs.create_object",
                                "%answer = ctjs.constant #ctjs.number<4611686018427387904>"),
                       true},
             std::pair{replaced(capturedAttribute, "%answer = ctjs.create_object",
                                "%answer = ctjs.constant #ctjs.null"),
                       true},
             std::pair{replaced(replaced(protectedAttribute, "%answer = ctjs.create_object",
                                         "%answer = ctjs.constant #ctjs.undefined"),
                                "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(protectedRead, "%answer = ctjs.create_object",
                                "%answer = ctjs.constant #ctjs.boolean<false>"),
                       true},
             std::pair{replaced(protectedAttribute, "%answer = ctjs.create_object",
                                "%answer = ctjs.constant #ctjs.string<\"ignored\">"),
                       true},
             std::pair{
                 replaced(replaced(scalarState,
                                   "%answer = ctjs.create_object\n    ctjs.set_property "
                                   "%answer[%name], %text",
                                   "%answer = ctjs.constant #ctjs.number<4611686018427387904>"),
                          "ctjs.binary add %one, %one", "ctjs.binary add %element, %element"),
                 false},
             std::pair{discardedState, true},
             std::pair{scalarState, true},
             std::pair{replaced(scalarState, "ctjs.binary add %one, %one",
                                "ctjs.binary add %element, %element"),
                       false},
             std::pair{capturedAttribute, true},
             std::pair{replaced(protectedAttribute, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{
                 replaced(protectedAttribute, "#ctjs.string<\"yes\">", "#ctjs.boolean<false>"),
                 true},
             std::pair{protectedRead, true},
             std::pair{trailingRead, true},
             std::pair{secondWrite, true},
             std::pair{selectorRead, true},
             std::pair{finalWrite, true},
             std::pair{selectedWrite, true},
             std::pair{finalRead, true},
             std::pair{
                 replaced(replaced(finalRead,
                                   "%finalReadKey = ctjs.constant #ctjs.string<\"hasAttribute\">",
                                   "%finalReadKey = ctjs.constant #ctjs.string<\"matches\">"),
                          "%finalReadName = ctjs.constant #ctjs.string<\"data-closed\">",
                          "%finalReadName = ctjs.constant #ctjs.string<\"[data-closed]\">"),
                 true},
             std::pair{fourthWrite, true},
             std::pair{fourthRead, true},
             std::pair{fifthRead, true},
             std::pair{terminalRead, true},
             std::pair{terminalMatch, true},
             std::pair{terminalMatchRead, true},
             std::pair{terminalMatchReads, true},
             std::pair{alternatingTerminal, true},
             std::pair{terminalReadWrite, true},
             std::pair{alternatingTerminalWrite, true},
             std::pair{terminalReadValue, true},
             std::pair{alternatingReadValue, true},
             std::pair{earlierTerminalValue, true},
             std::pair{secondTerminalValue, true},
             std::pair{alternatingEarlierValue, true},
             std::pair{terminalSelectorValue, true},
             std::pair{alternatingSelectorValue, true},
             std::pair{lastSelectorValue, true},
             std::pair{readBeforeSecondWrite, true},
             std::pair{alternatingReadBeforeSecondWrite, true},
             std::pair{replaced(readBeforeSecondWrite, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(readBeforeSecondWrite,
                                "ctjs.call %lateMethod(%element, %lateName, %afterTerminalPresent)",
                                "ctjs.call %lateMethod(%element, %lateName, %lateValue)"),
                       true},
             std::pair{replaced(readBeforeSecondWrite, "data-late", "bad name"), false},
             std::pair{replaced(readBeforeSecondWrite, "[data-closed]", "["), false},
             std::pair{replaced(readBeforeSecondWrite, "#ctjs.string<\"data-after-terminal\">",
                                "#ctjs.number<0>"),
                       false},
             std::pair{replaced(replaced(readBeforeSecondWrite,
                                         "ctjs.get_property %element[%afterTerminalKey]",
                                         "ctjs.get_property %text[%afterTerminalKey]"),
                                "ctjs.call %afterTerminalMethod(%element, %afterTerminalName)",
                                "ctjs.call %afterTerminalMethod(%text, %afterTerminalName)"),
                       false},
             std::pair{readBeforeFirstWrite, true},
             std::pair{alternatingReadBeforeFirstWrite, true},
             std::pair{replaced(readBeforeFirstWrite, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(readBeforeFirstWrite,
                                "ctjs.call %lateMethod(%element, %lateName, %afterTerminalPresent)",
                                "ctjs.call %lateMethod(%element, %lateName, %lateValue)"),
                       true},
             std::pair{replaced(readBeforeFirstWrite, "data-late", "bad name"), false},
             std::pair{replaced(readBeforeFirstWrite, "[data-closed]", "["), false},
             std::pair{replaced(readBeforeFirstWrite, "#ctjs.string<\"data-after-terminal\">",
                                "#ctjs.number<0>"),
                       false},
             std::pair{replaced(replaced(readBeforeFirstWrite,
                                         "ctjs.get_property %element[%afterTerminalKey]",
                                         "ctjs.get_property %text[%afterTerminalKey]"),
                                "ctjs.call %afterTerminalMethod(%element, %afterTerminalName)",
                                "ctjs.call %afterTerminalMethod(%text, %afterTerminalName)"),
                       false},
             std::pair{readInsideFirstWrite, true},
             std::pair{readInsideSecondWrite, true},
             std::pair{replaced(readInsideFirstWrite, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(readInsideFirstWrite,
                                "ctjs.call %lateMethod(%element, %lateName, %afterTerminalPresent)",
                                "ctjs.call %lateMethod(%element, %lateName, %lateValue)"),
                       true},
             std::pair{replaced(readInsideFirstWrite, "data-late", "bad name"), false},
             std::pair{replaced(readInsideSecondWrite, "[data-closed]", "["), false},
             std::pair{replaced(readInsideFirstWrite, "#ctjs.string<\"data-after-terminal\">",
                                "#ctjs.number<0>"),
                       false},
             std::pair{replaced(replaced(readInsideSecondWrite,
                                         "ctjs.get_property %element[%afterTerminalKey]",
                                         "ctjs.get_property %text[%afterTerminalKey]"),
                                "ctjs.call %afterTerminalMethod(%element, %afterTerminalName)",
                                "ctjs.call %afterTerminalMethod(%text, %afterTerminalName)"),
                       false},
             std::pair{readAfterFirstFeeding, true},
             std::pair{readAfterSecondFeeding, true},
             std::pair{
                 replaced(terminalReadValue,
                          "ctjs.call %lateMethod(%element, %lateName, %latePresent)",
                          "ctjs.call %lateMethod(%element, %lateName, %secondTerminalPresent)"),
                 true},
             std::pair{readThroughFinalArgument, true},
             std::pair{earlyReadThroughFinalArgument, true},
             std::pair{selectorThroughFinalArgument, true},
             std::pair{alternateSelectorThroughFinalArgument, true},
             std::pair{selectorInsideFinalArgument, true},
             std::pair{readThroughFinalSelector, true},
             std::pair{firstWriteSelector, true},
             std::pair{secondWriteSelector, true},
             std::pair{firstSelectorWithSavedRead, true},
             std::pair{selectorBeforeFirstWrite, true},
             std::pair{selectorBeforeSecondWrite, true},
             std::pair{earlySavedSelector, true},
             std::pair{ignoredEarlySelector, true},
             std::pair{replaced(selectorBeforeFirstWrite, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{
                 replaced(replaced(selectorBeforeFirstWrite, "ctjs.get_property %element[%readKey]",
                                   "ctjs.get_property %text[%readKey]"),
                          "ctjs.call %readMethod(%element, %readName)",
                          "ctjs.call %readMethod(%text, %readName)"),
                 false},
             std::pair{replaced(selectorBeforeSecondWrite, "data-final", "bad name"), false},
             std::pair{replaced(earlySavedSelector, "#ctjs.string<\"data-after-terminal\">",
                                "#ctjs.number<0>"),
                       false},
             std::pair{replaced(replaced(ignoredEarlySelector,
                                         "ctjs.get_property %element[%terminalReadKey]",
                                         "ctjs.get_property %text[%terminalReadKey]"),
                                "ctjs.call %terminalReadMethod(%element, %terminalReadName)",
                                "ctjs.call %terminalReadMethod(%text, %terminalReadName)"),
                       false},
             std::pair{replaced(firstWriteSelector, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(replaced(firstWriteSelector, "ctjs.get_property %element[%readKey]",
                                         "ctjs.get_property %text[%readKey]"),
                                "ctjs.call %readMethod(%element, %readName)",
                                "ctjs.call %readMethod(%text, %readName)"),
                       false},
             std::pair{
                 replaced(replaced(secondWriteSelector, "ctjs.get_property %element[%afterReadKey]",
                                   "ctjs.get_property %text[%afterReadKey]"),
                          "ctjs.call %afterReadMethod(%element, %afterReadName)",
                          "ctjs.call %afterReadMethod(%text, %afterReadName)"),
                 false},
             std::pair{replaced(firstWriteSelector, "data-final", "bad name"), false},
             std::pair{replaced(firstSelectorWithSavedRead, "#ctjs.string<\"data-after-terminal\">",
                                "#ctjs.number<0>"),
                       false},
             std::pair{replaced(selectorInsideFinalArgument, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(terminalReadValue,
                                "#ctjs.string<\"hasAttribute\">\n    %lateReadName",
                                "#ctjs.string<\"matches\">\n    %lateReadName"),
                       true},
             std::pair{replaced(selectorInsideFinalArgument, "[data-late-argument]", "["), false},
             std::pair{replaced(selectorInsideFinalArgument,
                                "#ctjs.string<\"[data-late-argument]\">", "#ctjs.number<0>"),
                       false},
             std::pair{replaced(replaced(selectorInsideFinalArgument,
                                         "ctjs.get_property %element[%lateReadKey]",
                                         "ctjs.get_property %text[%lateReadKey]"),
                                "ctjs.call %lateReadMethod(%element, %lateReadName)",
                                "ctjs.call %lateReadMethod(%text, %lateReadName)"),
                       false},
             std::pair{replaced(selectorInsideFinalArgument, "data-late", "bad name"), false},
             std::pair{replaced(selectorThroughFinalArgument,
                                "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(selectorThroughFinalArgument, "[data-closed]", "["), false},
             std::pair{replaced(alternateSelectorThroughFinalArgument, "[data-terminal]", "["),
                       false},
             std::pair{replaced(replaced(selectorThroughFinalArgument,
                                         "ctjs.get_property %element[%terminalReadKey]",
                                         "ctjs.get_property %text[%terminalReadKey]"),
                                "ctjs.call %terminalReadMethod(%element, %terminalReadName)",
                                "ctjs.call %terminalReadMethod(%text, %terminalReadName)"),
                       false},
             std::pair{replaced(selectorThroughFinalArgument,
                                "%lateReadName = ctjs.constant #ctjs.string<\"data-closed\">",
                                "%lateReadName = ctjs.constant #ctjs.number<0>"),
                       false},
             std::pair{replaced(earlyReadThroughFinalArgument,
                                "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(readThroughFinalArgument, "data-late", "bad name"), false},
             std::pair{replaced(earlyReadThroughFinalArgument, "[data-closed]", "["), false},
             std::pair{replaced(readThroughFinalArgument,
                                "%lateReadName = ctjs.constant #ctjs.string<\"data-closed\">",
                                "%lateReadName = ctjs.constant #ctjs.number<0>"),
                       false},
             std::pair{replaced(replaced(readThroughFinalArgument,
                                         "ctjs.get_property %element[%lateReadKey]",
                                         "ctjs.get_property %text[%lateReadKey]"),
                                "ctjs.call %lateReadMethod(%element, %lateReadName)",
                                "ctjs.call %lateReadMethod(%text, %lateReadName)"),
                       false},
             std::pair{replaced(readAfterFirstFeeding, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(readAfterSecondFeeding,
                                "ctjs.call %lateMethod(%element, %lateName, %afterTerminalPresent)",
                                "ctjs.call %lateMethod(%element, %lateName, %lateValue)"),
                       true},
             std::pair{replaced(readAfterFirstFeeding, "data-late", "bad name"), false},
             std::pair{replaced(readAfterSecondFeeding, "[data-closed]", "["), false},
             std::pair{replaced(readAfterFirstFeeding, "#ctjs.string<\"data-after-terminal\">",
                                "#ctjs.number<0>"),
                       false},
             std::pair{replaced(replaced(readAfterSecondFeeding,
                                         "ctjs.get_property %element[%afterTerminalKey]",
                                         "ctjs.get_property %text[%afterTerminalKey]"),
                                "ctjs.call %afterTerminalMethod(%element, %afterTerminalName)",
                                "ctjs.call %afterTerminalMethod(%text, %afterTerminalName)"),
                       false},
             std::pair{readBeforeFirstSelector, true},
             std::pair{alternatingReadBeforeFirstSelector, true},
             std::pair{replaced(readBeforeFirstSelector, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(readBeforeFirstSelector,
                                "ctjs.call %lateMethod(%element, %lateName, %afterTerminalPresent)",
                                "ctjs.call %lateMethod(%element, %lateName, %lateValue)"),
                       true},
             std::pair{replaced(readBeforeFirstSelector, "data-late", "bad name"), false},
             std::pair{replaced(readBeforeFirstSelector, "[data-closed]", "["), false},
             std::pair{replaced(readBeforeFirstSelector, "[data-terminal]", "["), false},
             std::pair{replaced(replaced(readBeforeFirstSelector,
                                         "ctjs.get_property %element[%afterTerminalKey]",
                                         "ctjs.get_property %text[%afterTerminalKey]"),
                                "ctjs.call %afterTerminalMethod(%element, %afterTerminalName)",
                                "ctjs.call %afterTerminalMethod(%text, %afterTerminalName)"),
                       false},
             std::pair{readBeforeSelector, true},
             std::pair{replaced(readBeforeSelector, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(readBeforeSelector, "data-late", "bad name"), false},
             std::pair{replaced(readBeforeSelector, "[data-terminal]", "["), false},
             std::pair{replaced(replaced(readBeforeSelector,
                                         "ctjs.get_property %element[%afterTerminalKey]",
                                         "ctjs.get_property %text[%afterTerminalKey]"),
                                "ctjs.call %afterTerminalMethod(%element, %afterTerminalName)",
                                "ctjs.call %afterTerminalMethod(%text, %afterTerminalName)"),
                       false},
             std::pair{replaced(terminalSelectorValue, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(terminalSelectorValue, "data-late", "bad name"), false},
             std::pair{replaced(terminalSelectorValue, "#ctjs.string<\"data-after-terminal\">",
                                "#ctjs.number<0>"),
                       false},
             std::pair{replaced(replaced(terminalSelectorValue,
                                         "ctjs.get_property %element[%terminalReadKey]",
                                         "ctjs.get_property %text[%terminalReadKey]"),
                                "ctjs.call %terminalReadMethod(%element, %terminalReadName)",
                                "ctjs.call %terminalReadMethod(%text, %terminalReadName)"),
                       false},
             std::pair{singleTerminalValue, true},
             std::pair{replaced(earlierTerminalValue, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(earlierTerminalValue, "data-late", "bad name"), false},
             std::pair{replaced(earlierTerminalValue, "[data-terminal]", "["), false},
             std::pair{replaced(earlierTerminalValue, "#ctjs.string<\"data-after-terminal\">",
                                "#ctjs.number<0>"),
                       false},
             std::pair{replaced(replaced(earlierTerminalValue,
                                         "ctjs.get_property %element[%afterTerminalKey]",
                                         "ctjs.get_property %text[%afterTerminalKey]"),
                                "ctjs.call %afterTerminalMethod(%element, %afterTerminalName)",
                                "ctjs.call %afterTerminalMethod(%text, %afterTerminalName)"),
                       false},
             std::pair{replaced(terminalReadValue, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(terminalReadValue, "data-late", "bad name"), false},
             std::pair{replaced(terminalReadValue, "[data-terminal]", "["), false},
             std::pair{
                 replaced(replaced(terminalReadValue, "ctjs.get_property %element[%lateReadKey]",
                                   "ctjs.get_property %text[%lateReadKey]"),
                          "ctjs.call %lateReadMethod(%element, %lateReadName)",
                          "ctjs.call %lateReadMethod(%text, %lateReadName)"),
                 false},
             std::pair{replaced(terminalReadWrite, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(terminalReadWrite, "data-late", "bad name"), false},
             std::pair{
                 replaced(terminalReadWrite, "#ctjs.string<\"data-late\">", "#ctjs.number<0>"),
                 false},
             std::pair{replaced(replaced(terminalReadWrite, "ctjs.get_property %element[%lateKey]",
                                         "ctjs.get_property %text[%lateKey]"),
                                "ctjs.call %lateMethod(%element, %lateName, %lateValue)",
                                "ctjs.call %lateMethod(%text, %lateName, %lateValue)"),
                       false},
             std::pair{replaced(terminalReadWrite, "[data-terminal]", "["), false},
             std::pair{replaced(terminalMatchReads, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(alternatingTerminal, "[data-alternate-terminal]", "["), false},
             std::pair{replaced(terminalMatchReads, "#ctjs.string<\"data-second-terminal\">",
                                "#ctjs.number<0>"),
                       false},
             std::pair{replaced(terminalMatchReads,
                                "ctjs.call %secondTerminalMethod(%element, %secondTerminalName)",
                                "ctjs.call %secondTerminalMethod(%element, %element)"),
                       false},
             std::pair{replaced(replaced(terminalMatchReads,
                                         "ctjs.get_property %element[%secondTerminalKey]",
                                         "ctjs.get_property %text[%secondTerminalKey]"),
                                "ctjs.call %secondTerminalMethod(%element, %secondTerminalName)",
                                "ctjs.call %secondTerminalMethod(%text, %secondTerminalName)"),
                       false},
             std::pair{replaced(terminalMatchRead, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(terminalMatchRead, "[data-terminal]", "["), false},
             std::pair{replaced(terminalMatchRead, "#ctjs.string<\"data-after-terminal\">",
                                "#ctjs.number<0>"),
                       false},
             std::pair{replaced(replaced(terminalMatchRead,
                                         "ctjs.get_property %element[%afterTerminalKey]",
                                         "ctjs.get_property %text[%afterTerminalKey]"),
                                "ctjs.call %afterTerminalMethod(%element, %afterTerminalName)",
                                "ctjs.call %afterTerminalMethod(%text, %afterTerminalName)"),
                       false},
             std::pair{replaced(terminalMatch, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(terminalMatch, "[data-terminal]", "["), false},
             std::pair{
                 replaced(terminalMatch, "#ctjs.string<\"[data-terminal]\">", "#ctjs.number<0>"),
                 false},
             std::pair{
                 replaced(replaced(terminalMatch, "ctjs.get_property %element[%terminalReadKey]",
                                   "ctjs.get_property %text[%terminalReadKey]"),
                          "ctjs.call %terminalReadMethod(%element, %terminalReadName)",
                          "ctjs.call %terminalReadMethod(%text, %terminalReadName)"),
                 false},
             std::pair{replaced(terminalRead, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(terminalRead, "#ctjs.string<\"data-terminal\">", "#ctjs.number<0>"),
                       false},
             std::pair{
                 replaced(replaced(terminalRead, "ctjs.get_property %element[%terminalReadKey]",
                                   "ctjs.get_property %text[%terminalReadKey]"),
                          "ctjs.call %terminalReadMethod(%element, %terminalReadName)",
                          "ctjs.call %terminalReadMethod(%text, %terminalReadName)"),
                 false},
             std::pair{replaced(fourthRead, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(fourthRead, "data-fourth", "bad name"), false},
             std::pair{replaced(fourthWrite, "#ctjs.string<\"data-fourth\">", "#ctjs.number<0>"),
                       false},
             std::pair{replaced(replaced(fourthRead, "ctjs.get_property %element[%fourthKey]",
                                         "ctjs.get_property %text[%fourthKey]"),
                                "ctjs.call %fourthMethod(%element, %fourthName, %fourthPresent)",
                                "ctjs.call %fourthMethod(%text, %fourthName, %fourthPresent)"),
                       false},
             std::pair{replaced(replaced(fourthRead, "ctjs.get_property %element[%fourthReadKey]",
                                         "ctjs.get_property %text[%fourthReadKey]"),
                                "ctjs.call %fourthReadMethod(%element, %fourthReadName)",
                                "ctjs.call %fourthReadMethod(%text, %fourthReadName)"),
                       false},
             std::pair{replaced(fifthRead, "data-fifth", "bad name"), false},
             std::pair{replaced(finalRead, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(finalRead, "data-final", "bad name"), false},
             std::pair{replaced(finalRead, "[data-closed]", "["), false},
             std::pair{replaced(finalRead,
                                "%finalReadName = ctjs.constant #ctjs.string<\"data-closed\">",
                                "%finalReadName = ctjs.constant #ctjs.string<\"bad name\">"),
                       true},
             std::pair{replaced(finalRead,
                                "%finalReadName = ctjs.constant #ctjs.string<\"data-closed\">",
                                "%finalReadName = ctjs.constant #ctjs.number<0>"),
                       false},
             std::pair{replaced(replaced(finalRead, "ctjs.get_property %element[%finalReadKey]",
                                         "ctjs.get_property %text[%finalReadKey]"),
                                "ctjs.call %finalReadMethod(%element, %finalReadName)",
                                "ctjs.call %finalReadMethod(%text, %finalReadName)"),
                       false},
             std::pair{replaced(selectedWrite, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(selectedWrite, "data-final", "bad name"), false},
             std::pair{replaced(replaced(selectedWrite, "ctjs.get_property %element[%selectorKey]",
                                         "ctjs.get_property %text[%selectorKey]"),
                                "ctjs.call %selectorMethod(%element, %selectorText)",
                                "ctjs.call %selectorMethod(%text, %selectorText)"),
                       false},
             std::pair{replaced(replaced(selectedWrite, "ctjs.get_property %element[%finalKey]",
                                         "ctjs.get_property %text[%finalKey]"),
                                "ctjs.call %finalMethod(%element, %finalName, %selected)",
                                "ctjs.call %finalMethod(%text, %finalName, %selected)"),
                       false},
             std::pair{replaced(finalWrite, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(finalWrite, "data-final", "bad name"), false},
             std::pair{replaced(finalWrite, "[data-closed]", "["), false},
             std::pair{replaced(finalWrite, "#ctjs.string<\"data-final\">", "#ctjs.number<0>"),
                       false},
             std::pair{replaced(replaced(finalWrite, "ctjs.get_property %element[%finalKey]",
                                         "ctjs.get_property %text[%finalKey]"),
                                "ctjs.call %finalMethod(%element, %finalName, %finalValue)",
                                "ctjs.call %finalMethod(%text, %finalName, %finalValue)"),
                       false},
             std::pair{replaced(finalWrite,
                                "ctjs.call %finalMethod(%element, %finalName, %finalValue)",
                                "ctjs.call %finalMethod(%element, %finalName, %element)"),
                       false},
             std::pair{replaced(selectorRead, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(selectorRead, "[data-closed]", "["), false},
             std::pair{replaced(selectorRead, "#ctjs.string<\"[data-closed]\">", "#ctjs.number<0>"),
                       false},
             std::pair{replaced(replaced(selectorRead, "ctjs.get_property %element[%selectorKey]",
                                         "ctjs.get_property %text[%selectorKey]"),
                                "ctjs.call %selectorMethod(%element, %selectorText)",
                                "ctjs.call %selectorMethod(%text, %selectorText)"),
                       false},
             std::pair{replaced(selectorRead, "#ctjs.string<\"data-closed\">",
                                "#ctjs.string<\"bad name\">"),
                       false},
             std::pair{writeAfterRead(readAfterWrite(capturedAttribute)), true},
             std::pair{replaced(secondWrite, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(secondWrite,
                                "%secondName = ctjs.constant #ctjs.string<\"data-closed\">",
                                "%secondName = ctjs.constant #ctjs.string<\"bad name\">"),
                       false},
             std::pair{replaced(secondWrite,
                                "%secondName = ctjs.constant #ctjs.string<\"data-closed\">",
                                "%secondName = ctjs.constant #ctjs.number<0>"),
                       false},
             std::pair{replaced(secondWrite, "%name = ctjs.constant #ctjs.string<\"data-closed\">",
                                "%name = ctjs.constant #ctjs.string<\"bad name\">"),
                       false},
             std::pair{replaced(replaced(secondWrite, "ctjs.get_property %element[%secondKey]",
                                         "ctjs.get_property %text[%secondKey]"),
                                "ctjs.call %secondMethod(%element, %secondName, %afterPresent)",
                                "ctjs.call %secondMethod(%text, %secondName, %afterPresent)"),
                       false},
             std::pair{replaced(trailingRead,
                                "%afterReadName = ctjs.constant #ctjs.string<\"data-closed\">",
                                "%afterReadName = ctjs.constant #ctjs.string<\"bad name\">"),
                       true},
             std::pair{replaced(trailingRead,
                                "%afterReadName = ctjs.constant #ctjs.string<\"data-closed\">",
                                "%afterReadName = ctjs.constant #ctjs.number<0>"),
                       false},
             std::pair{readAfterWrite(capturedAttribute), true},
             std::pair{replaced(trailingRead, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(replaced(trailingRead, "ctjs.get_property %element[%afterReadKey]",
                                         "ctjs.get_property %text[%afterReadKey]"),
                                "ctjs.call %afterReadMethod(%element, %afterReadName)",
                                "ctjs.call %afterReadMethod(%text, %afterReadName)"),
                       false},
             std::pair{replaced(trailingRead, "data-closed", "bad name"), false},
             std::pair{readThenWrite(capturedAttribute), true},
             std::pair{replaced(protectedRead, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(protectedRead, "data-visited", "bad name"), true},
             std::pair{replaced(protectedRead, "#ctjs.string<\"data-visited\">", "#ctjs.number<1>"),
                       false},
             std::pair{replaced(replaced(protectedRead, "ctjs.get_property %element[%readKey]",
                                         "ctjs.get_property %text[%readKey]"),
                                "ctjs.call %readMethod(%element, %readName)",
                                "ctjs.call %readMethod(%text, %readName)"),
                       false},
             std::pair{replaced(protectedRead, "data-closed", "bad name"), false},
             std::pair{replaced(protectedAttribute, "data-closed", "bad name"), false},
         }) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(valid, &context);
        check(static_cast<bool>(input), "protected attribute helper fixture parses");
        if (!input) { continue; }
        auto error = expandDOMHelpers(*input, "entry$0", 100000);
        check(!error, "attribute preparation expands with write suppression intact");
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
                      (ctjs::constantKey(method.getKey()) == "setAttribute" ||
                       ctjs::constantKey(method.getKey()) == "matches") &&
                      call.getReceiver() == method.getObject(),
                  "original attribute call stays protected under the original guard");
        });
        input->walk([&](ctjs::CallOp) { ++calls; });
        input->walk([&](ctjs::CreateObjectOp) { ++allocations; });
        const bool reads = valid.find("%readKey") != std::string::npos;
        const bool firstReadMatches =
            valid.find("%readKey = ctjs.constant #ctjs.string<\"matches\">") != std::string::npos;
        const bool firstReadBeforeLookup = valid.find("%present =") < valid.find("%attribute =");
        const bool secondReadMatches =
            valid.find("%afterReadKey = ctjs.constant #ctjs.string<\"matches\">") !=
            std::string::npos;
        const bool secondReadBeforeLookup =
            valid.find("%afterPresent =") < valid.find("%secondMethod =");
        const bool trailingReads = valid.find("%afterReadKey") != std::string::npos;
        const bool secondWrites = valid.find("%secondKey") != std::string::npos;
        const bool selectorReads = valid.find("%selectorKey") != std::string::npos;
        const bool finalWrites = valid.find("%finalKey") != std::string::npos;
        const bool selectedWrites = valid.find("%finalName, %selected)") != std::string::npos;
        const bool finalReads = valid.find("%finalReadKey") != std::string::npos;
        const bool finalReadMatches =
            valid.find("%finalReadKey = ctjs.constant #ctjs.string<\"matches\">") !=
            std::string::npos;
        const bool terminalReads = valid.find("%terminalReadKey") != std::string::npos;
        const bool afterTerminalReads = valid.find("%afterTerminalKey") != std::string::npos;
        const bool beforeSelectorRead =
            valid.find("%afterTerminalKey =") < valid.find("%terminalReadKey =");
        const bool beforeFirstSelectorRead =
            valid.find("%afterTerminalKey =") < valid.find("%selectorKey =");
        const bool beforeSecondWriteRead =
            valid.find("%afterTerminalKey =") < valid.find("%secondKey =");
        const bool beforeFirstWriteRead =
            valid.find("%afterTerminalKey =") < valid.find("%attribute =");
        const bool insideFirstWriteRead =
            valid.find("%attribute =") < valid.find("%afterTerminalKey =") &&
            valid.find("%afterTerminalKey =") < valid.find("%effect =");
        const bool insideSecondWriteRead =
            valid.find("%secondMethod =") < valid.find("%afterTerminalKey =") &&
            valid.find("%afterTerminalKey =") < valid.find("%secondEffect =");
        const bool afterFirstFeedingRead =
            insideFirstWriteRead && valid.find("%present =") < valid.find("%afterTerminalKey =");
        const bool afterSecondFeedingRead =
            insideSecondWriteRead &&
            valid.find("%afterPresent =") < valid.find("%afterTerminalKey =");
        const bool secondTerminalReads = valid.find("%secondTerminalKey") != std::string::npos;
        const bool alternatingReads = valid.find("%alternateTerminalKey") != std::string::npos;
        const bool lateWrites = valid.find("%lateMethod") != std::string::npos;
        const bool lateReads = valid.find("%lateReadKey") != std::string::npos;
        const bool lateMatches =
            valid.find("%lateReadKey = ctjs.constant #ctjs.string<\"matches\">") !=
            std::string::npos;
        const bool lateSelectorFeedsWrite =
            lateMatches && valid.find("%lateName, %latePresent)") != std::string::npos;
        const bool savedTerminalValue =
            valid.find("%lateName, %afterTerminalPresent)") != std::string::npos ||
            valid.find("%lateName, %secondTerminalPresent)") != std::string::npos ||
            valid.find("%name, %afterTerminalPresent)") != std::string::npos;
        const bool savedSecondTerminalValue =
            valid.find("%lateName, %secondTerminalPresent)") != std::string::npos;
        const bool savedTerminalSelector =
            valid.find("%lateName, %terminalPresent)") != std::string::npos;
        const bool savedAlternateSelector =
            valid.find("%lateName, %alternateTerminalPresent)") != std::string::npos;
        const bool savedSelectorValue = savedTerminalSelector || savedAlternateSelector;
        const bool terminalMatches =
            valid.find("%terminalReadKey = ctjs.constant #ctjs.string<\"matches\">") !=
            std::string::npos;
        const bool standaloneTerminalSelector =
            terminalMatches && valid.find("%terminalPresent =") < valid.find("%attribute =");
        const unsigned suffixWrites =
            static_cast<unsigned>(valid.find("%fourthKey") != std::string::npos) +
            static_cast<unsigned>(valid.find("%fifthKey") != std::string::npos);
        const unsigned suffixReads =
            static_cast<unsigned>(valid.find("%fourthReadKey") != std::string::npos) +
            static_cast<unsigned>(valid.find("%fifthReadKey") != std::string::npos);
        if (firstReadMatches || secondReadMatches) {
            unsigned writes = 0;
            input->walk([&](ctjs::InvokeOp invoke) {
                auto write = llvm::cast<ctjs::CallOp>(invoke.getBody().front().front());
                auto method = write.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                if (ctjs::constantKey(method.getKey()) != "setAttribute") { return; }
                ++writes;
                if ((writes != 1 || !firstReadMatches) && (writes != 2 || !secondReadMatches)) {
                    return;
                }
                auto read = write.getArgs()[1].getDefiningOp<ctjs::CallOp>();
                auto readMethod = read ? read.getCallee().getDefiningOp<ctjs::GetPropertyOp>()
                                       : ctjs::GetPropertyOp{};
                const bool beforeLookup =
                    writes == 1 ? firstReadBeforeLookup : secondReadBeforeLookup;
                unsigned preceding = 0;
                if (read) {
                    for (ctjs::InvokeOp prior : invoke->getBlock()->getOps<ctjs::InvokeOp>()) {
                        preceding += prior->isBeforeInBlock(read);
                    }
                }
                check(readMethod && ctjs::constantKey(readMethod.getKey()) == "matches" &&
                          ctjs::constantKey(read.getArgs()[0]) ==
                              (writes == 1 ? "[data-visited]" : "[data-closed]") &&
                          preceding == writes - 1 && read.getResult().hasOneUse() &&
                          read->getBlock() == invoke->getBlock() &&
                          (beforeLookup ? read->isBeforeInBlock(method)
                                        : method->isBeforeInBlock(readMethod)) &&
                          readMethod->isBeforeInBlock(read) && read->isBeforeInBlock(invoke) &&
                          llvm::isa<mlir::scf::IfOp>(invoke->getParentOp()),
                      "initial write consumes exactly its source-ordered selector at the "
                      "source guard");
            });
            check(writes >= 2, "argument selectors retain both initial protected writes");
        }
        if (standaloneTerminalSelector) {
            unsigned earlySelectors = 0;
            input->walk([&](ctjs::CallOp read) {
                if (ctjs::constantKey(read.getArgs()[0]) != "[data-terminal]") { return; }
                ++earlySelectors;
                auto method = read.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                auto first = *read->getBlock()->getOps<ctjs::InvokeOp>().begin();
                auto write = llvm::cast<ctjs::CallOp>(first.getBody().front().front());
                auto writeMethod = write.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                check(method && ctjs::constantKey(method.getKey()) == "matches" &&
                          method->isBeforeInBlock(read) && read->isBeforeInBlock(writeMethod) &&
                          writeMethod->isBeforeInBlock(first) &&
                          llvm::isa<mlir::scf::IfOp>(read->getParentOp()) &&
                          (savedTerminalSelector ? read.getResult().hasOneUse()
                                                 : read.getResult().use_empty()),
                      "early saved or ignored selector retains its identity before the first "
                      "write lookup and under the source guard");
            });
            check(earlySelectors == 1, "the complete census retains exactly one early selector");
        }
        if (suffixWrites) {
            input->walk([&](ctjs::InvokeOp invoke) {
                auto call = llvm::cast<ctjs::CallOp>(invoke.getBody().front().front());
                const auto name = ctjs::constantKey(call.getArgs()[0]);
                if (name != "data-fourth" && name != "data-fifth") { return; }
                unsigned preceding = 0;
                for (ctjs::InvokeOp prior : invoke->getBlock()->getOps<ctjs::InvokeOp>()) {
                    preceding += prior->isBeforeInBlock(invoke);
                }
                check(preceding == (name == "data-fourth" ? 4u : 5u) &&
                          llvm::isa<mlir::scf::IfOp>(invoke->getParentOp()),
                      "suffix writes retain their order and source guard");
            });
        }
        if (finalWrites) {
            input->walk([&](ctjs::InvokeOp invoke) {
                auto call = llvm::cast<ctjs::CallOp>(invoke.getBody().front().front());
                auto method = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                if (ctjs::constantKey(call.getArgs()[0]) != "data-final") { return; }
                unsigned preceding = 0;
                for (ctjs::InvokeOp prior : invoke->getBlock()->getOps<ctjs::InvokeOp>()) {
                    preceding += prior->isBeforeInBlock(method);
                }
                check(preceding == (selectedWrites ? 2u : 3u) &&
                          llvm::isa<mlir::scf::IfOp>(invoke->getParentOp()),
                      "final write follows the selector and both prior writes at the source guard");
                if (selectedWrites) {
                    auto selected = call.getArgs()[1].getDefiningOp<ctjs::CallOp>();
                    auto selector = selected
                                        ? selected.getCallee().getDefiningOp<ctjs::GetPropertyOp>()
                                        : ctjs::GetPropertyOp{};
                    check(selector && ctjs::constantKey(selector.getKey()) == "matches" &&
                              selected->getBlock() == invoke->getBlock() &&
                              selected->isBeforeInBlock(method),
                          "final write consumes the ordered selector Boolean directly");
                }
                if (finalReads) {
                    auto read = call.getArgs()[1].getDefiningOp<ctjs::CallOp>();
                    auto readMethod = read ? read.getCallee().getDefiningOp<ctjs::GetPropertyOp>()
                                           : ctjs::GetPropertyOp{};
                    unsigned precedingReads = 0;
                    if (readMethod) {
                        for (ctjs::InvokeOp prior : invoke->getBlock()->getOps<ctjs::InvokeOp>()) {
                            precedingReads += prior->isBeforeInBlock(readMethod);
                        }
                    }
                    check(readMethod &&
                              ctjs::constantKey(readMethod.getKey()) ==
                                  (finalReadMatches ? "matches" : "hasAttribute") &&
                              precedingReads == 3 && read->getBlock() == invoke->getBlock() &&
                              method->isBeforeInBlock(readMethod) &&
                              readMethod->isBeforeInBlock(read) && read->isBeforeInBlock(invoke) &&
                              read.getResult().hasOneUse(),
                          "final write consumes the read after both writes and the selector");
                }
            });
        }
        if (selectorReads) {
            unsigned selectors = 0;
            input->walk([&](ctjs::InvokeOp invoke) {
                auto call = llvm::cast<ctjs::CallOp>(invoke.getBody().front().front());
                auto method = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                if (ctjs::constantKey(method.getKey()) != "matches") { return; }
                unsigned preceding = 0;
                for (ctjs::InvokeOp prior : invoke->getBlock()->getOps<ctjs::InvokeOp>()) {
                    preceding += prior->isBeforeInBlock(method);
                }
                const unsigned expected = selectors == 0 ? 2u : selectors == 1 ? 5u : 6u;
                ++selectors;
                check(preceding == expected && method->isBeforeInBlock(invoke) &&
                          llvm::isa<mlir::scf::IfOp>(invoke->getParentOp()),
                      "selector lookup and evaluation retain their write order and source guard");
            });
        }
        if (secondTerminalReads && typed) {
            llvm::SmallVector<std::string> sequence;
            input->walk([&](ctjs::CallOp call) {
                const auto name = ctjs::constantKey(call.getArgs()[0]);
                if (name == "data-after-terminal" || name == "data-second-terminal" ||
                    name == "[data-alternate-terminal]" || name == "data-last-terminal") {
                    sequence.push_back(name.str());
                }
            });
            llvm::SmallVector<std::string> expected{"data-after-terminal", "data-second-terminal"};
            if (alternatingReads) {
                expected.append({"[data-alternate-terminal]", "data-last-terminal"});
            }
            check(sequence == expected, "terminal reads and selectors retain exact source order");
        }
        if (beforeSelectorRead) {
            ctjs::CallOp saved;
            input->walk([&](ctjs::CallOp call) {
                auto method = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                if (ctjs::constantKey(call.getArgs()[0]) == "data-after-terminal" ||
                    (beforeSecondWriteRead && !typed &&
                     ctjs::constantKey(method.getKey()) == "hasAttribute" &&
                     ctjs::constantKey(call.getArgs()[0]).empty())) {
                    saved = call;
                }
                if (ctjs::constantKey(method.getKey()) != "matches" ||
                    (firstReadMatches &&
                     ctjs::constantKey(call.getArgs()[0]) == "[data-visited]") ||
                    (!beforeFirstSelectorRead &&
                     ctjs::constantKey(call.getArgs()[0]) == "[data-closed]")) {
                    return;
                }
                auto invoke = call->getParentOfType<ctjs::InvokeOp>();
                check(saved && invoke && saved->getBlock() == invoke->getBlock() &&
                          saved->isBeforeInBlock(invoke),
                      "saved read remains before the intervening selector");
            });
        }
        if (lateWrites) {
            unsigned writes = 0;
            input->walk([&](ctjs::InvokeOp invoke) {
                auto call = llvm::cast<ctjs::CallOp>(invoke.getBody().front().front());
                auto method = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                if (ctjs::constantKey(method.getKey()) != "setAttribute" || ++writes != 5) {
                    return;
                }
                unsigned preceding = 0;
                for (ctjs::InvokeOp prior : invoke->getBlock()->getOps<ctjs::InvokeOp>()) {
                    preceding += prior->isBeforeInBlock(method);
                }
                auto value = call.getArgs()[1].getDefiningOp<ctjs::ConstantOp>();
                auto read = call.getArgs()[1].getDefiningOp<ctjs::CallOp>();
                const bool retainsValue =
                    savedSelectorValue
                        ? read && read.getResult().hasOneUse() && read->isBeforeInBlock(method) &&
                              ctjs::constantKey(read.getArgs()[0]) ==
                                  (savedTerminalSelector ? "[data-terminal]"
                                                         : "[data-alternate-terminal]")
                    : savedTerminalValue
                        ? read && read.getResult().hasOneUse() && read->isBeforeInBlock(method) &&
                              (!typed || ctjs::constantKey(read.getArgs()[0]) ==
                                             (savedSecondTerminalValue ? "data-second-terminal"
                                                                       : "data-after-terminal"))
                    : lateReads
                        ? read && read.getResult().hasOneUse() && method->isBeforeInBlock(read) &&
                              read->isBeforeInBlock(invoke)
                        : value &&
                              value.getValue() ==
                                  (alternatingReads
                                       ? mlir::Attribute(ctjs::StringAttr::get(&context, "yes"))
                                       : ctjs::BooleanAttr::get(&context, false));
                check(preceding == (alternatingReads ? 7u : 6u) -
                                       static_cast<unsigned>(savedSelectorValue ||
                                                             standaloneTerminalSelector) &&
                          retainsValue && llvm::isa<mlir::scf::IfOp>(invoke->getParentOp()),
                      "late write retains its value, selector order and source guard");
                for (ctjs::CallOp read : invoke->getBlock()->getOps<ctjs::CallOp>()) {
                    if (read.getResult().use_empty()) {
                        auto readMethod = read.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                        const auto readName = ctjs::constantKey(read.getArgs()[0]);
                        const bool argumentRead =
                            lateReads && (savedTerminalValue || savedSelectorValue) &&
                            (readName == "data-closed" ||
                             (!typed && readName.empty() && !standaloneTerminalSelector));
                        check(argumentRead ? method->isBeforeInBlock(readMethod) &&
                                                 readMethod->isBeforeInBlock(read) &&
                                                 read->isBeforeInBlock(invoke) &&
                                                 call.getArgs()[1] != read.getResult()
                                           : read->isBeforeInBlock(method),
                              "unused reads retain their position around the late write lookup "
                              "and the saved write value stays distinct");
                    }
                }
                if (lateMatches && !lateSelectorFeedsWrite) {
                    unsigned argumentSelectors = 0;
                    for (ctjs::InvokeOp selected : invoke->getBlock()->getOps<ctjs::InvokeOp>()) {
                        auto read = llvm::cast<ctjs::CallOp>(selected.getBody().front().front());
                        auto lookup = read.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                        if (ctjs::constantKey(lookup.getKey()) != "matches" ||
                            !method->isBeforeInBlock(lookup)) {
                            continue;
                        }
                        ++argumentSelectors;
                        auto exit = llvm::dyn_cast<ctjs::InvokeExitOp>(read->getNextNode());
                        check(lookup->isBeforeInBlock(selected) &&
                                  selected->isBeforeInBlock(invoke) && exit &&
                                  exit.getNormalResult() == read.getResult() &&
                                  read.getResult().hasOneUse() &&
                                  call.getArgs()[1] != read.getResult() &&
                                  selected.getNumResults() == 0 &&
                                  selected.getNormalBody().front().getArgument(0).use_empty() &&
                                  selected.getUnwindBody().front().getArgument(0).use_empty(),
                              "ignored argument selector stays protected after the write lookup "
                              "and before the write of the distinct saved Boolean");
                    }
                    check(argumentSelectors == 1,
                          "exactly the original argument selector retains suppression");
                }
            });
        }
        if (reads || trailingReads) {
            input->walk([&](ctjs::CallOp call) {
                auto method = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                if (!method || ctjs::constantKey(method.getKey()) != "hasAttribute") { return; }
                if (ctjs::constantKey(call.getArgs()[0]) == "data-terminal") {
                    unsigned preceding = 0;
                    for (ctjs::InvokeOp invoke : call->getBlock()->getOps<ctjs::InvokeOp>()) {
                        preceding += invoke->isBeforeInBlock(method);
                    }
                    check(preceding == 5 && call.getResult().use_empty() &&
                              method->isBeforeInBlock(call) &&
                              llvm::isa<mlir::scf::IfOp>(call->getParentOp()),
                          "unused terminal read follows every write and selector at its guard");
                }
                if (ctjs::constantKey(call.getArgs()[0]) == "data-after-terminal") {
                    unsigned preceding = 0;
                    for (ctjs::InvokeOp invoke : call->getBlock()->getOps<ctjs::InvokeOp>()) {
                        preceding += invoke->isBeforeInBlock(method);
                    }
                    const unsigned expected =
                        beforeFirstWriteRead || insideFirstWriteRead     ? 0u
                        : beforeSecondWriteRead || insideSecondWriteRead ? 1u
                        : beforeFirstSelectorRead
                            ? 2u
                            : 6u -
                                  static_cast<unsigned>(savedTerminalSelector ||
                                                        standaloneTerminalSelector) -
                                  static_cast<unsigned>(beforeSelectorRead);
                    check(preceding == expected &&
                              (savedTerminalValue && !savedSecondTerminalValue
                                   ? call.getResult().hasOneUse()
                                   : call.getResult().use_empty()) &&
                              method->isBeforeInBlock(call) &&
                              llvm::isa<mlir::scf::IfOp>(call->getParentOp()),
                          "saved attribute read retains its exact selector/write order and guard");
                    if (insideFirstWriteRead || insideSecondWriteRead) {
                        unsigned writes = 0;
                        for (ctjs::InvokeOp invoke : call->getBlock()->getOps<ctjs::InvokeOp>()) {
                            if (++writes != (insideFirstWriteRead ? 1u : 2u)) { continue; }
                            auto write = llvm::cast<ctjs::CallOp>(invoke.getBody().front().front());
                            auto writeMethod =
                                write.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                            auto feedingRead = write.getArgs()[1].getDefiningOp<ctjs::CallOp>();
                            check(writeMethod && feedingRead &&
                                      writeMethod->isBeforeInBlock(method) &&
                                      writeMethod->isBeforeInBlock(feedingRead) &&
                                      (afterFirstFeedingRead || afterSecondFeedingRead
                                           ? feedingRead->isBeforeInBlock(method) &&
                                                 call->isBeforeInBlock(invoke)
                                           : call->isBeforeInBlock(feedingRead)) &&
                                      feedingRead->isBeforeInBlock(invoke) &&
                                      feedingRead.getResult() != call.getResult() &&
                                      feedingRead.getResult().hasOneUse(),
                                  "saved argument and distinct feeding read retain source order "
                                  "after the pending write lookup");
                        }
                    }
                }
                if (secondWrites && ctjs::constantKey(call.getArgs()[0]) == "data-closed") {
                    bool followsWrite = false;
                    for (ctjs::InvokeOp invoke : call->getBlock()->getOps<ctjs::InvokeOp>()) {
                        followsWrite |= invoke->isBeforeInBlock(method);
                    }
                    check(followsWrite,
                          "second write reads the state after the first protected write");
                }
                if (call.getResult().use_empty()) {
                    bool ordered = false;
                    const bool early =
                        (beforeFirstWriteRead || insideFirstWriteRead) &&
                        ctjs::constantKey(call.getArgs()[0]) == "data-after-terminal";
                    for (ctjs::InvokeOp invoke : call->getBlock()->getOps<ctjs::InvokeOp>()) {
                        ordered |= method->getBlock() == invoke->getBlock() &&
                                   method->isBeforeInBlock(call) &&
                                   (early ? call->isBeforeInBlock(invoke)
                                          : invoke->isBeforeInBlock(method));
                    }
                    check(trailingReads && ordered &&
                              llvm::isa<mlir::scf::IfOp>(call->getParentOp()),
                          "unused read retains its source position and guard");
                    return;
                }
                auto consumer =
                    call.getResult().hasOneUse()
                        ? llvm::dyn_cast<ctjs::CallOp>(*call.getResult().getUsers().begin())
                        : ctjs::CallOp{};
                auto consumerMethod =
                    consumer ? consumer.getCallee().getDefiningOp<ctjs::GetPropertyOp>()
                             : ctjs::GetPropertyOp{};
                if (savedTerminalValue && consumerMethod &&
                    call->getBlock() == consumerMethod->getBlock() &&
                    call->isBeforeInBlock(consumerMethod)) {
                    auto write = consumer;
                    auto writeMethod = consumerMethod;
                    check(write && write.getArgs()[1] == call.getResult() && writeMethod &&
                              call.getResult().hasOneUse() &&
                              writeMethod->getBlock() == method->getBlock() &&
                              call->isBeforeInBlock(writeMethod) &&
                              llvm::isa<ctjs::InvokeOp>(write->getParentOp()) &&
                              llvm::isa<mlir::scf::IfOp>(call->getParentOp()),
                          "saved terminal Boolean retains its identity before the later write "
                          "lookup");
                    return;
                }
                auto * next = call->getNextNode();
                while (next && llvm::isa<ctjs::ConstantOp>(next)) { next = next->getNextNode(); }
                const bool earlierFeeding =
                    (afterFirstFeedingRead &&
                     ctjs::constantKey(call.getArgs()[0]) == "data-visited") ||
                    (afterSecondFeedingRead &&
                     ctjs::constantKey(call.getArgs()[0]) == "data-closed");
                auto invoke = earlierFeeding && consumer
                                  ? llvm::dyn_cast<ctjs::InvokeOp>(consumer->getParentOp())
                                  : llvm::dyn_cast_or_null<ctjs::InvokeOp>(next);
                auto write = invoke ? llvm::dyn_cast<ctjs::CallOp>(invoke.getBody().front().front())
                                    : ctjs::CallOp{};
                auto writeMethod = write ? write.getCallee().getDefiningOp<ctjs::GetPropertyOp>()
                                         : ctjs::GetPropertyOp{};
                check(
                    write && write.getArgs()[1] == call.getResult() && writeMethod &&
                        writeMethod->getBlock() == method->getBlock() &&
                        writeMethod->isBeforeInBlock(method) &&
                        call->getBlock() == invoke->getBlock() && call->isBeforeInBlock(invoke) &&
                        (!earlierFeeding || (consumer == write && call.getResult().hasOneUse())) &&
                        llvm::isa<mlir::scf::IfOp>(call->getParentOp()),
                    "member lookup, read and protected write retain source order and guard");
            });
        }
        check(invocations ==
                      1u + static_cast<unsigned>(secondWrites) +
                          static_cast<unsigned>(selectorReads && !selectedWrites) +
                          static_cast<unsigned>(finalWrites) + suffixWrites +
                          static_cast<unsigned>(terminalMatches) +
                          static_cast<unsigned>(alternatingReads) +
                          static_cast<unsigned>(lateWrites) -
                          static_cast<unsigned>(savedSelectorValue) -
                          static_cast<unsigned>(standaloneTerminalSelector && !savedSelectorValue) +
                          static_cast<unsigned>(lateMatches && !lateSelectorFeedsWrite) &&
                  calls ==
                      1u + static_cast<unsigned>(reads) + static_cast<unsigned>(trailingReads) +
                          static_cast<unsigned>(secondWrites) +
                          static_cast<unsigned>(selectorReads) + static_cast<unsigned>(finalReads) +
                          static_cast<unsigned>(finalWrites) + suffixWrites + suffixReads +
                          static_cast<unsigned>(terminalReads) +
                          static_cast<unsigned>(afterTerminalReads) +
                          static_cast<unsigned>(secondTerminalReads) +
                          2u * static_cast<unsigned>(alternatingReads) +
                          static_cast<unsigned>(lateWrites) + static_cast<unsigned>(lateReads) &&
                  allocations == 0 && mlir::succeeded(mlir::verify(*input)),
              "protected expansion retains call-plus-exit and only elides unused objects");
        auto bound = contract;
        bound.entry = "entry$0";
        bound.moduleSha256 = hostContractFingerprint(*input);
        for (auto provider :
             {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
            bound.provider = provider;
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
                auto method = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                const auto kind = ctjs::constantKey(method.getKey()) == "matches"
                                      ? HostDOMMethod::matches
                                      : HostDOMMethod::setAttribute;
                check(proof.invocation(invoke) && proof.call(call) &&
                          proof.call(call)->kind == kind,
                      "typed suppression retains the exact attribute call evidence");
            });
            input->walk([&](ctjs::CallOp call) {
                if (llvm::isa<ctjs::InvokeOp>(call->getParentOp())) { return; }
                auto method = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                const bool selector = ctjs::constantKey(method.getKey()) == "matches";
                check(proof.call(call) &&
                          proof.call(call)->kind ==
                              (selector ? HostDOMMethod::matches : HostDOMMethod::hasAttribute) &&
                          (!selector || selectedWrites || finalReadMatches || savedSelectorValue ||
                           lateSelectorFeedsWrite || firstReadMatches || secondReadMatches ||
                           standaloneTerminalSelector),
                      "the moved read has exact typed no-source-throw evidence");
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
    // Reusing a snapshot keeps one read and every protected write in source order.
    for (const auto & valid : {
             reusedInitialSelector,
             reusedArgumentSelector,
             reusedInitialRead,
             reusedSecondSelector,
             replaced(reusedInitialSelector, "ctjs.call %method(%holder, %element)",
                      "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
             replaced(appendWrite(earlySavedSelector, finalWrite, "reuse"),
                      "ctjs.call %reuseMethod(%element, %reuseName, %reuseValue)",
                      "ctjs.call %reuseMethod(%element, %reuseName, %terminalPresent)"),
             replaced(appendWrite(readBeforeSecondWrite, finalWrite, "reuse"),
                      "ctjs.call %reuseMethod(%element, %reuseName, %reuseValue)",
                      "ctjs.call %reuseMethod(%element, %reuseName, %afterTerminalPresent)"),
             replaced(readBeforeSecondWrite,
                      "ctjs.call %secondMethod(%element, %secondName, %afterPresent)",
                      "ctjs.call %secondMethod(%element, %secondName, %afterTerminalPresent)"),
             replaced(appendWrite(readBeforeFirstWrite, finalWrite, "reuse"),
                      "ctjs.call %reuseMethod(%element, %reuseName, %reuseValue)",
                      "ctjs.call %reuseMethod(%element, %reuseName, %afterTerminalPresent)"),
             replaced(readBeforeFirstWrite,
                      "ctjs.call %secondMethod(%element, %secondName, %afterPresent)",
                      "ctjs.call %secondMethod(%element, %secondName, %afterTerminalPresent)"),
             replaced(readBeforeFirstWrite, "ctjs.call %attribute(%element, %name, %present)",
                      "ctjs.call %attribute(%element, %name, %afterTerminalPresent)"),
             replaced(appendWrite(readInsideSecondWrite, finalWrite, "reuse"),
                      "ctjs.call %reuseMethod(%element, %reuseName, %reuseValue)",
                      "ctjs.call %reuseMethod(%element, %reuseName, %afterTerminalPresent)"),
             replaced(readInsideFirstWrite, "ctjs.call %attribute(%element, %name, %present)",
                      "ctjs.call %attribute(%element, %name, %afterTerminalPresent)"),
             replaced(readInsideSecondWrite,
                      "ctjs.call %secondMethod(%element, %secondName, %afterPresent)",
                      "ctjs.call %secondMethod(%element, %secondName, %afterTerminalPresent)"),
             replaced(appendWrite(readAfterSecondFeeding, finalWrite, "reuse"),
                      "ctjs.call %reuseMethod(%element, %reuseName, %reuseValue)",
                      "ctjs.call %reuseMethod(%element, %reuseName, %afterTerminalPresent)"),
             replaced(readAfterFirstFeeding,
                      "ctjs.call %lateMethod(%element, %lateName, %afterTerminalPresent)",
                      "ctjs.call %lateMethod(%element, %lateName, %present)"),
             replaced(appendWrite(readThroughFinalArgument, finalWrite, "reuse"),
                      "ctjs.call %reuseMethod(%element, %reuseName, %reuseValue)",
                      "ctjs.call %reuseMethod(%element, %reuseName, %afterTerminalPresent)"),
             replaced(earlyReadThroughFinalArgument,
                      "ctjs.call %lateMethod(%element, %lateName, %afterTerminalPresent)",
                      "ctjs.call %lateMethod(%element, %lateName, %present)"),
             replaced(appendWrite(selectorThroughFinalArgument, finalWrite, "reuse"),
                      "ctjs.call %reuseMethod(%element, %reuseName, %reuseValue)",
                      "ctjs.call %reuseMethod(%element, %reuseName, %terminalPresent)"),
             replaced(appendWrite(selectorInsideFinalArgument, finalWrite, "reuse"),
                      "ctjs.call %reuseMethod(%element, %reuseName, %reuseValue)",
                      "ctjs.call %reuseMethod(%element, %reuseName, %terminalPresent)"),
             replaced(firstWriteSelector,
                      "ctjs.call %finalMethod(%element, %finalName, %finalPresent)",
                      "ctjs.call %finalMethod(%element, %finalName, %present)"),
             replaced(appendWrite(readBeforeFirstSelector, finalWrite, "reuse"),
                      "ctjs.call %reuseMethod(%element, %reuseName, %reuseValue)",
                      "ctjs.call %reuseMethod(%element, %reuseName, %afterTerminalPresent)"),
             replaced(appendWrite(readBeforeSelector, finalWrite, "reuse"),
                      "ctjs.call %reuseMethod(%element, %reuseName, %reuseValue)",
                      "ctjs.call %reuseMethod(%element, %reuseName, %afterTerminalPresent)"),
             replaced(appendWrite(terminalSelectorValue, finalWrite, "reuse"),
                      "ctjs.call %reuseMethod(%element, %reuseName, %reuseValue)",
                      "ctjs.call %reuseMethod(%element, %reuseName, %terminalPresent)"),
             replaced(appendWrite(earlierTerminalValue, finalWrite, "reuse"),
                      "ctjs.call %reuseMethod(%element, %reuseName, %reuseValue)",
                      "ctjs.call %reuseMethod(%element, %reuseName, %afterTerminalPresent)"),
             replaced(fourthRead, "ctjs.call %fourthMethod(%element, %fourthName, %fourthPresent)",
                      "ctjs.call %fourthMethod(%element, %fourthName, %finalPresent)"),
             replaced(fifthRead, "ctjs.call %fifthMethod(%element, %fifthName, %fifthPresent)",
                      "ctjs.call %fifthMethod(%element, %fifthName, %fourthPresent)"),
         }) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(valid, &context);
        check(static_cast<bool>(input), "shared cleanup snapshot fixture parses");
        if (!input) { continue; }
        unsigned originalReads = 0, originalWrites = 0;
        input->walk([&](ctjs::CallOp call) {
            auto method = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            if (!method) { return; }
            const auto key = ctjs::constantKey(method.getKey());
            originalReads += key == "matches" || key == "hasAttribute";
            originalWrites += key == "setAttribute";
        });
        auto error = expandDOMHelpers(*input, "entry$0", 100000);
        check(!error, "one saved cleanup read may feed several ordered writes");
        if (error) {
            std::fprintf(stderr, "%s\n", llvm::toString(std::move(error)).c_str());
            continue;
        }
        unsigned reads = 0, writes = 0, shared = 0;
        input->walk([&](ctjs::CallOp call) {
            auto method = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            if (!method) { return; }
            const auto key = ctjs::constantKey(method.getKey());
            if (key == "setAttribute") {
                ++writes;
                check(llvm::isa<ctjs::InvokeOp>(call->getParentOp()),
                      "every shared snapshot write keeps its suppression");
                return;
            }
            if (key != "matches" && key != "hasAttribute") { return; }
            ++reads;
            if (call.getResult().use_empty() || call.getResult().hasOneUse()) { return; }
            ++shared;
            for (mlir::OpOperand & use : call.getResult().getUses()) {
                auto write = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                auto invoke =
                    write ? llvm::dyn_cast<ctjs::InvokeOp>(write->getParentOp()) : ctjs::InvokeOp{};
                check(write && invoke && use.getOperandNumber() == 3 &&
                          write.getArgs()[1] == call.getResult() &&
                          call->getBlock() == invoke->getBlock() &&
                          llvm::isa<mlir::scf::IfOp>(call->getParentOp()) &&
                          call->isBeforeInBlock(invoke),
                      "all shared-value uses retain snapshot identity, guard and read-before-write "
                      "order");
            }
        });
        check(reads == originalReads && writes == originalWrites && shared > 0 &&
                  mlir::succeeded(mlir::verify(*input)),
              "shared cleanup expansion neither repeats reads nor drops writes");
        auto bound = contract;
        bound.entry = "entry$0";
        bound.moduleSha256 = hostContractFingerprint(*input);
        for (auto provider :
             {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
            bound.provider = provider;
            DOMEntryAnalysis proof(*input, bound);
            check(proof.proved(), "shared snapshots receive complete typed DOM reproof");
            if (!proof.proved()) { continue; }
            input->walk([&](ctjs::CallOp call) {
                check(proof.call(call), "every shared snapshot call retains typed evidence");
            });
            check(DOMEntryAnalysis(*input, bound, proof.steps()).proved(),
                  "shared snapshots reproduce their exact typed proof budget");
            DOMEntryAnalysis limited(*input, bound, proof.steps() - 1);
            check(limited.exhausted() && noEvidence(*input, limited),
                  "incomplete shared snapshot proof withholds all evidence");
        }
    }
    for (const auto & [valid, typed] : {
             std::pair{conditionalSelector, true},
             std::pair{conditionalRead, true},
             std::pair{conditionalDistinct, true},
             std::pair{conditionalIgnoredRead, true},
             std::pair{conditionalBranchRead, true},
             std::pair{conditionalArgumentRead, true},
             std::pair{conditionalBranchSelector, true},
             std::pair{conditionalIgnoredSelector, true},
             std::pair{conditionalTwoReads, true},
             std::pair{conditionalLastRead, true},
             std::pair{conditionalMixedReads, true},
             std::pair{conditionalEarlySelectors, true},
             std::pair{conditionalThreeReads, true},
             std::pair{conditionalThirdWrite, true},
             std::pair{conditionalThirdSavedWrite, true},
             std::pair{conditionalThirdSelector, true},
             std::pair{conditionalFourthWrite, true},
             std::pair{replaced(conditionalThirdWrite, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(conditionalThirdWrite, "data-after-second", "bad name"), false},
             std::pair{
                 replaced(replaced(conditionalThirdWrite,
                                   "ctjs.get_property %element[%secondKey]\n    %afterReadKey",
                                   "ctjs.get_property %text[%secondKey]\n    %afterReadKey"),
                          "ctjs.call %thirdMethod(%element, %thirdName, %afterPresent)",
                          "ctjs.call %thirdMethod(%text, %thirdName, %afterPresent)"),
                 false},
             std::pair{replaced(replaced(conditionalThirdSelector,
                                         "ctjs.get_property %element[%afterReadKey]",
                                         "ctjs.get_property %text[%afterReadKey]"),
                                "ctjs.call %afterReadMethod(%element, %afterReadName)",
                                "ctjs.call %afterReadMethod(%text, %afterReadName)"),
                       false},
             std::pair{conditionalPostRead, true},
             std::pair{conditionalPostSecondRead, true},
             std::pair{conditionalPostSelector, true},
             std::pair{conditionalPostReads, true},
             std::pair{conditionalMixedPostReads, true},
             std::pair{replaced(conditionalPostReads, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{
                 replaced(replaced(conditionalPostRead, "ctjs.get_property %element[%afterReadKey]",
                                   "ctjs.get_property %text[%afterReadKey]"),
                          "ctjs.call %afterReadMethod(%element, %afterReadName)",
                          "ctjs.call %afterReadMethod(%text, %afterReadName)"),
                 false},
             std::pair{replaced(replaced(conditionalPostSelector,
                                         "ctjs.get_property %element[%afterReadKey]",
                                         "ctjs.get_property %text[%afterReadKey]"),
                                "ctjs.call %afterReadMethod(%element, %afterReadName)",
                                "ctjs.call %afterReadMethod(%text, %afterReadName)"),
                       false},
             std::pair{replaced(conditionalPostRead,
                                "%afterReadName = ctjs.constant #ctjs.string<\"data-closed\">",
                                "%afterReadName = ctjs.constant #ctjs.number<0>"),
                       false},
             std::pair{replaced(conditionalThreeReads,
                                "ctjs.call %secondMethod(%element, %secondName, %anotherPresent)",
                                "ctjs.call %secondMethod(%element, %secondName, %thirdPresent)"),
                       true},
             std::pair{replaced(conditionalTwoReads,
                                "ctjs.call %secondMethod(%element, %secondName, %afterPresent)",
                                "ctjs.call %secondMethod(%element, %secondName, %present)"),
                       true},
             std::pair{moveReadBefore(conditionalTwoReads, "    %afterReadKey =",
                                      "    %secondEffect =", "    %secondMethod ="),
                       true},
             std::pair{replaced(conditionalTwoReads, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(replaced(conditionalMixedReads,
                                         "ctjs.get_property %element[%anotherReadKey]",
                                         "ctjs.get_property %text[%anotherReadKey]"),
                                "ctjs.call %anotherReadMethod(%element, %anotherReadName)",
                                "ctjs.call %anotherReadMethod(%text, %anotherReadName)"),
                       false},
             std::pair{replaced(conditionalArgumentRead, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(replaced(conditionalArgumentRead,
                                         "ctjs.get_property %element[%afterReadKey]",
                                         "ctjs.get_property %text[%afterReadKey]"),
                                "ctjs.call %afterReadMethod(%element, %afterReadName)",
                                "ctjs.call %afterReadMethod(%text, %afterReadName)"),
                       false},
             std::pair{replaced(replaced(conditionalBranchSelector,
                                         "ctjs.get_property %element[%afterReadKey]",
                                         "ctjs.get_property %text[%afterReadKey]"),
                                "ctjs.call %afterReadMethod(%element, %afterReadName)",
                                "ctjs.call %afterReadMethod(%text, %afterReadName)"),
                       false},
             std::pair{replaced(conditionalArgumentRead,
                                "%afterReadName = ctjs.constant #ctjs.string<\"data-closed\">",
                                "%afterReadName = ctjs.constant #ctjs.number<0>"),
                       false},
             std::pair{replaced(conditionalSelector, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(conditionalSelector,
                                "%secondName = ctjs.constant #ctjs.string<\"data-closed\">",
                                "%secondName = ctjs.constant #ctjs.string<\"bad name\">"),
                       false},
             std::pair{
                 replaced(replaced(conditionalSelector, "ctjs.get_property %element[%secondKey]",
                                   "ctjs.get_property %text[%secondKey]"),
                          "ctjs.call %secondMethod(%element, %secondName, %present)",
                          "ctjs.call %secondMethod(%text, %secondName, %present)"),
                 false},
         }) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(valid, &context);
        check(static_cast<bool>(input), "conditional cleanup snapshot fixture parses");
        if (!input) { continue; }
        const auto readOrder = [&] {
            std::vector<std::pair<std::string, std::string>> order;
            input->walk([&](ctjs::CallOp call) {
                auto method = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                if (!method) { return; }
                const auto key = ctjs::constantKey(method.getKey());
                if (key == "hasAttribute" || key == "matches") {
                    order.emplace_back(key.str(), ctjs::constantKey(call.getArgs()[0]).str());
                }
            });
            return order;
        };
        const auto originalReads = readOrder();
        auto error = expandDOMHelpers(*input, "entry$0", 100000);
        check(!error, "saved Boolean keeps the second cleanup write conditional");
        if (error) {
            std::fprintf(stderr, "%s\n", llvm::toString(std::move(error)).c_str());
            continue;
        }
        ctjs::CallOp snapshot, firstWrite, secondWrite;
        std::vector<ctjs::CallOp> otherReads, laterWrites;
        unsigned reads = 0, writes = 0;
        input->walk([&](ctjs::CallOp call) {
            auto method = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            if (!method) { return; }
            if (ctjs::constantKey(method.getKey()) == "setAttribute") {
                ++writes;
                if (writes <= 2) {
                    (writes == 1 ? firstWrite : secondWrite) = call;
                } else {
                    laterWrites.push_back(call);
                }
            } else {
                ++reads;
                const auto name = ctjs::constantKey(call.getArgs()[0]);
                if (name == "data-visited" || name == "[data-visited]") {
                    snapshot = call;
                } else {
                    otherReads.push_back(call);
                }
            }
        });
        const unsigned extraWrites = valid.find("%fourthEffect =") != std::string::npos  ? 2u
                                     : valid.find("%thirdEffect =") != std::string::npos ? 1u
                                                                                         : 0u;
        const bool savedThird = valid.find("%thirdName, %present)") != std::string::npos;
        const bool extraRead = valid.find("%afterReadKey =") != std::string::npos;
        const bool distinct = valid.find("%secondName, %present)") == std::string::npos;
        const bool branchRead =
            extraRead && valid.find("%closeCondition =") < valid.find("%afterReadKey =");
        check(snapshot && firstWrite && secondWrite && reads == originalReads.size() &&
                  otherReads.empty() != extraRead && writes == 2 + extraWrites &&
                  laterWrites.size() == extraWrites && readOrder() == originalReads &&
                  mlir::succeeded(mlir::verify(*input)),
              "conditional expansion keeps every read once and every original write");
        if (!snapshot || !firstWrite || !secondWrite) { continue; }
        auto first = llvm::dyn_cast<ctjs::InvokeOp>(firstWrite->getParentOp());
        auto second = llvm::dyn_cast<ctjs::InvokeOp>(secondWrite->getParentOp());
        auto guard =
            second ? llvm::dyn_cast<mlir::scf::IfOp>(second->getParentOp()) : mlir::scf::IfOp{};
        auto truth =
            guard ? guard.getCondition().getDefiningOp<ctjs::TruthyOp>() : ctjs::TruthyOp{};
        auto firstMethod = firstWrite.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
        auto secondMethod = secondWrite.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
        auto valueRead = secondWrite.getArgs()[1].getDefiningOp<ctjs::CallOp>();
        check(first && guard && truth && truth.getValue() == snapshot.getResult() &&
                  truth.getResult().hasOneUse() && !guard.getNumResults() &&
                  guard.getElseRegion().empty() && guard->getBlock() == first->getBlock() &&
                  snapshot->getBlock() == first->getBlock() &&
                  llvm::isa<mlir::scf::IfOp>(first->getParentOp()) &&
                  snapshot->isBeforeInBlock(first) && first->isBeforeInBlock(truth) &&
                  truth->isBeforeInBlock(guard) &&
                  second->getParentRegion() == &guard.getThenRegion() &&
                  secondMethod->getBlock() == second->getBlock() &&
                  secondMethod->isBeforeInBlock(second) &&
                  firstWrite.getArgs()[1] == snapshot.getResult() && valueRead &&
                  (distinct ? valueRead != snapshot : valueRead == snapshot),
              "saved snapshot guards the ordered second lookup and suppression at its source arm");
        const bool beforeLookup = valid.find("%present =") < valid.find("%attribute =");
        check(snapshot->getBlock() == firstMethod->getBlock() &&
                  (beforeLookup ? snapshot->isBeforeInBlock(firstMethod)
                                : firstMethod->isBeforeInBlock(snapshot)),
              "guard snapshot retains its original position around the first write lookup");
        unsigned uses = 0;
        for (mlir::OpOperand & use : snapshot.getResult().getUses()) {
            ++uses;
            check((use.getOwner() == truth && use.getOperandNumber() == 0) ||
                      ((use.getOwner() == firstWrite ||
                        (!distinct && use.getOwner() == secondWrite) ||
                        (savedThird && !laterWrites.empty() &&
                         use.getOwner() == laterWrites.front())) &&
                       use.getOperandNumber() == 3),
                  "every snapshot use is its original write value or exact guard");
        }
        check(uses == (distinct ? 2u : 3u) + static_cast<unsigned>(savedThird),
              "conditional snapshot has a complete use census");
        for (std::size_t index = 0; branchRead && guard && second && index < otherReads.size();
             ++index) {
            auto otherRead = otherReads[index];
            const std::string valueName = index == 0   ? "%afterPresent"
                                          : index == 1 ? "%anotherPresent"
                                                       : "%thirdPresent";
            const bool feeds = valid.find("%secondName, " + valueName + ")") != std::string::npos;
            auto method = otherRead.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            const bool beforeLookup = valid.find(valueName + " =") < valid.find("%secondMethod =");
            const bool beforeWrite = valid.find(valueName + " =") < valid.find("%secondEffect =");
            check(
                method && otherRead->getParentRegion() == &guard.getThenRegion() &&
                    method->getBlock() == second->getBlock() &&
                    otherRead->getBlock() == second->getBlock() &&
                    method->isBeforeInBlock(otherRead) &&
                    (beforeWrite ? otherRead->isBeforeInBlock(second)
                                 : second->isBeforeInBlock(method)) &&
                    (beforeLookup ? otherRead->isBeforeInBlock(secondMethod)
                                  : secondMethod->isBeforeInBlock(method)) &&
                    method.getResult().hasOneUse() &&
                    (index == 0 || otherReads[index - 1]->isBeforeInBlock(method)) &&
                    (feeds ? valueRead == otherRead : valueRead != otherRead),
                "branch-local reads retain source order, guard, lookup order and feeding identity");
            unsigned localUses = 0;
            for (mlir::OpOperand & use : otherRead.getResult().getUses()) {
                ++localUses;
                const bool laterFeed =
                    index == 0 && llvm::any_of(laterWrites, [&](ctjs::CallOp write) {
                        return use.getOwner() == write &&
                               (!savedThird || write != laterWrites.front());
                    });
                check(((feeds && use.getOwner() == secondWrite) || laterFeed) &&
                          use.getOperandNumber() == 3,
                      "branch-local read has only its original write value uses");
            }
            check(localUses ==
                      (feeds ? 1u : 0u) +
                          (index == 0 ? extraWrites - static_cast<unsigned>(savedThird) : 0u),
                  "ignored or consumed branch-local snapshot has a complete use census");
        }
        std::vector<ctjs::InvokeOp> invocations{first, second};
        auto previous = second;
        if (!laterWrites.empty() && otherReads.empty()) { continue; }
        for (std::size_t index = 0; index < laterWrites.size(); ++index) {
            auto write = laterWrites[index];
            auto invocation = llvm::dyn_cast<ctjs::InvokeOp>(write->getParentOp());
            auto method = write.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            auto value = index == 0 && savedThird ? snapshot : otherReads.front();
            check(invocation && guard && previous && method &&
                      invocation->getParentRegion() == &guard.getThenRegion() &&
                      method->getBlock() == invocation->getBlock() &&
                      previous->getBlock() == invocation->getBlock() &&
                      previous->isBeforeInBlock(method) && method->isBeforeInBlock(invocation) &&
                      method.getResult().hasOneUse() && write.getResult().hasOneUse() &&
                      write.getArgs()[1] == value.getResult() &&
                      (index != 0 || (method->isBeforeInBlock(otherReads.front()) &&
                                      otherReads.front()->isBeforeInBlock(invocation))),
                  "later writes retain their guard, lookup/read/write order and Boolean identity");
            invocations.push_back(invocation);
            previous = invocation;
        }
        for (auto invocation : invocations) {
            if (!invocation) { continue; }
            for (auto * continuation : {&invocation.getNormalBody(), &invocation.getUnwindBody()}) {
                auto & arm = continuation->front();
                auto yield = llvm::hasSingleElement(arm)
                                 ? llvm::dyn_cast<ctjs::InvokeYieldOp>(arm.front())
                                 : ctjs::InvokeYieldOp{};
                check(!invocation.getNumResults() && arm.getNumArguments() == 1 &&
                          arm.getArgument(0).use_empty() && yield && yield.getValues().empty(),
                      "every write retains exact discarded-result and exception suppression");
            }
        }
        auto bound = contract;
        bound.entry = "entry$0";
        bound.moduleSha256 = hostContractFingerprint(*input);
        for (auto provider :
             {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
            bound.provider = provider;
            DOMEntryAnalysis proof(*input, bound);
            check(proof.proved() == typed,
                  "guarded writes require complete DOM receiver and valid-name reproof");
            if (!typed) {
                check(noEvidence(*input, proof), "invalid guarded write publishes no evidence");
                continue;
            }
            if (!proof.proved()) { continue; }
            input->walk([&](ctjs::CallOp call) {
                check(proof.call(call), "every conditional cleanup call has typed evidence");
            });
            for (auto otherRead : otherReads) {
                auto method = otherRead.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                const auto kind = ctjs::constantKey(method.getKey()) == "matches"
                                      ? HostDOMMethod::matches
                                      : HostDOMMethod::hasAttribute;
                check(proof.call(otherRead) && proof.call(otherRead)->kind == kind &&
                          proof.call(otherRead)->returnsBoolean() && proof.method(method),
                      "branch-local read receives exact DOM or Style Boolean evidence");
            }
            check(llvm::all_of(
                      invocations,
                      [&](ctjs::InvokeOp invocation) { return proof.invocation(invocation); }) &&
                      DOMEntryAnalysis(*input, bound, proof.steps()).proved(),
                  "all conditional suppressions reproduce complete typed proof");
            for (unsigned budget : {0u, proof.steps() - 1}) {
                DOMEntryAnalysis limited(*input, bound, budget);
                check(limited.exhausted() && noEvidence(*input, limited),
                      "incomplete conditional cleanup proof publishes no partial evidence");
            }
        }
    }
    for (const auto & [valid, typed] : {
             std::pair{conditionalNestedWrite, true},
             std::pair{conditionalNestedSelector, true},
             std::pair{conditionalDeeperWrite, true},
             std::pair{conditionalNestedElse, true},
             std::pair{conditionalElseWrite, true},
             std::pair{conditionalElseRead, true},
             std::pair{conditionalElseSelector, true},
             std::pair{conditionalElseNested, true},
             std::pair{replaced(conditionalElseWrite,
                                "ctjs.call %elseMethod(%element, %elseName, %elseValue)",
                                "ctjs.call %elseMethod(%element, %elseName, %present)"),
                       true},
             std::pair{replaced(conditionalElseRead, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(conditionalElseWrite, "data-else", "bad name"), false},
             std::pair{replaced(replaced(conditionalElseSelector,
                                         "ctjs.get_property %element[%elseReadKey]",
                                         "ctjs.get_property %text[%elseReadKey]"),
                                "ctjs.call %elseReadMethod(%element, %elseReadName)",
                                "ctjs.call %elseReadMethod(%text, %elseReadName)"),
                       false},
             std::pair{replaced(conditionalNestedWrite,
                                "ctjs.call %thirdMethod(%element, %thirdName, %afterPresent)",
                                "ctjs.call %thirdMethod(%element, %thirdName, %present)"),
                       true},
             std::pair{moveReadBefore(conditionalNestedWrite, "    %afterReadKey =",
                                      "    %thirdEffect =", "    %thirdMethod ="),
                       true},
             std::pair{replaced(conditionalNestedWrite, "ctjs.call %method(%holder, %element)",
                                "ctjs.call_direct @same$1(%holder, %u, %method, %element)"),
                       true},
             std::pair{replaced(conditionalNestedWrite, "data-after-second", "bad name"), false},
             std::pair{replaced(replaced(conditionalNestedSelector,
                                         "ctjs.get_property %element[%guardReadKey]",
                                         "ctjs.get_property %text[%guardReadKey]"),
                                "ctjs.call %guardMethod(%element, %guardReadName)",
                                "ctjs.call %guardMethod(%text, %guardReadName)"),
                       false},
         }) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(valid, &context);
        check(static_cast<bool>(input), "nested cleanup fixture parses");
        if (!input) { continue; }
        // Compare every lookup, read, value use and guard before/after expansion.
        // The caller's surrounding guard adds one common level of nesting.
        const auto trace = [&](ctjs::FuncOp function) {
            std::vector<std::string> events;
            llvm::DenseMap<mlir::Value, unsigned> producers;
            unsigned baseDepth = 0;
            function.walk<mlir::WalkOrder::PreOrder>([&](mlir::Operation * operation) {
                std::string event;
                if (auto method = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
                    const auto key = ctjs::constantKey(method.getKey());
                    if (key != "setAttribute" && key != "hasAttribute" && key != "matches") {
                        return;
                    }
                    event = "lookup " + key.str();
                } else if (auto call = llvm::dyn_cast<ctjs::CallOp>(operation)) {
                    if (!producers.contains(call.getCallee())) { return; }
                    event = "call " + std::to_string(producers.lookup(call.getCallee()));
                    for (mlir::Value argument : call.getArgs()) {
                        event += " " + ctjs::constantKey(argument).str() + ":" +
                                 std::to_string(producers.lookup(argument));
                        if (auto literal = argument.getDefiningOp<ctjs::ConstantOp>()) {
                            llvm::raw_string_ostream output(event);
                            literal.getValue().print(output);
                        }
                    }
                } else if (auto truth = llvm::dyn_cast<ctjs::TruthyOp>(operation)) {
                    if (!producers.contains(truth.getValue())) { return; }
                    event = "truth " + std::to_string(producers.lookup(truth.getValue()));
                } else if (auto guard = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
                    if (!producers.contains(guard.getCondition())) { return; }
                    event = "guard " + std::to_string(producers.lookup(guard.getCondition())) +
                            (guard.getElseRegion().empty() ? "" : " else");
                } else {
                    return;
                }
                std::string arms;
                for (auto * child = operation; child->getParentOp(); child = child->getParentOp()) {
                    if (auto parent = llvm::dyn_cast<mlir::scf::IfOp>(child->getParentOp())) {
                        arms.insert(arms.begin(),
                                    child->getParentRegion() == &parent.getThenRegion() ? 'T'
                                                                                        : 'E');
                    }
                }
                if (events.empty()) { baseDepth = static_cast<unsigned>(arms.size()); }
                events.push_back(arms.substr(baseDepth) + " " + event);
                if (operation->getNumResults() == 1) {
                    producers[operation->getResult(0)] = static_cast<unsigned>(events.size());
                }
            });
            return events;
        };
        const auto original = trace(input->lookupSymbol<ctjs::FuncOp>("same$1"));
        unsigned originalCalls = 0, originalWrites = 0;
        input->lookupSymbol<ctjs::FuncOp>("same$1").walk([&](ctjs::CallOp call) {
            ++originalCalls;
            auto method = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            originalWrites += method && ctjs::constantKey(method.getKey()) == "setAttribute";
        });
        auto error = expandDOMHelpers(*input, "entry$0", 100000);
        check(!error, "nested cleanup guards preserve independently protected writes");
        if (error) {
            std::fprintf(stderr, "%s\n", llvm::toString(std::move(error)).c_str());
            continue;
        }
        check(!original.empty() &&
                  original == trace(input->lookupSymbol<ctjs::FuncOp>("entry$0")) &&
                  mlir::succeeded(mlir::verify(*input)),
              "nested expansion retains lookup/read/write order, source arms and saved values");
        unsigned calls = 0, invocations = 0;
        input->walk([&](ctjs::CallOp call) {
            ++calls;
            auto method = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            check(method && method.getResult().hasOneUse() &&
                      method.getObject() == call.getReceiver(),
                  "nested method lookup retains its unique call and receiver");
            for (mlir::OpOperand & use : call.getResult().getUses()) {
                check((llvm::isa<ctjs::TruthyOp, ctjs::InvokeExitOp>(use.getOwner()) &&
                       use.getOperandNumber() == 0) ||
                          (llvm::isa<ctjs::CallOp>(use.getOwner()) && use.getOperandNumber() == 3),
                      "every nested snapshot use remains an exact guard or write value");
            }
        });
        input->walk([&](ctjs::InvokeOp invocation) {
            ++invocations;
            for (auto * continuation : {&invocation.getNormalBody(), &invocation.getUnwindBody()}) {
                auto & arm = continuation->front();
                auto yield = llvm::hasSingleElement(arm)
                                 ? llvm::dyn_cast<ctjs::InvokeYieldOp>(arm.front())
                                 : ctjs::InvokeYieldOp{};
                check(!invocation.getNumResults() && arm.getNumArguments() == 1 &&
                          arm.getArgument(0).use_empty() && yield && yield.getValues().empty(),
                      "each nested write keeps exact unused-result and exception suppression");
            }
        });
        check(calls == originalCalls && invocations == originalWrites,
              "nested expansion keeps every original read and protected write");
        auto bound = contract;
        bound.entry = "entry$0";
        bound.moduleSha256 = hostContractFingerprint(*input);
        for (auto provider :
             {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
            bound.provider = provider;
            DOMEntryAnalysis proof(*input, bound);
            check(proof.proved() == typed,
                  "nested expansion requires complete DOM and Style reproof");
            if (!typed) {
                check(noEvidence(*input, proof),
                      "invalid nested receiver or name grants no evidence");
                continue;
            }
            if (!proof.proved()) { continue; }
            input->walk([&](ctjs::CallOp call) {
                check(proof.call(call), "nested call has typed evidence");
            });
            input->walk([&](ctjs::InvokeOp invocation) {
                check(proof.invocation(invocation), "nested suppression has typed evidence");
            });
            check(DOMEntryAnalysis(*input, bound, proof.steps()).proved(),
                  "nested cleanup reproduces its complete typed budget");
            for (unsigned budget : {0u, proof.steps() - 1}) {
                DOMEntryAnalysis limited(*input, bound, budget);
                check(limited.exhausted() && noEvidence(*input, limited),
                      "incomplete nested cleanup proof publishes no evidence");
            }
        }
    }
    for (unsigned mutation = 0; mutation != 4; ++mutation) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(conditionalNestedElse, &context);
        check(static_cast<bool>(input), "nested malformed-region fixture parses before mutation");
        if (!input) { continue; }
        mlir::scf::IfOp nested;
        input->walk([&](mlir::scf::IfOp guard) {
            auto truth = guard.getCondition().getDefiningOp<ctjs::TruthyOp>();
            auto read = truth ? truth.getValue().getDefiningOp<ctjs::CallOp>() : ctjs::CallOp{};
            if (read && ctjs::constantKey(read.getArgs()[0]) == "data-third-guard") {
                nested = guard;
            }
        });
        check(static_cast<bool>(nested), "malformed fixture finds its nested DOM guard");
        if (!nested) { continue; }
        auto & region = mutation < 2 ? nested.getThenRegion() : nested.getElseRegion();
        if (mutation % 2) {
            auto & block = region.emplaceBlock();
            auto at = mlir::OpBuilder::atBlockEnd(&block);
            mlir::scf::YieldOp::create(at, nested.getLoc());
        } else {
            region.front().addArgument(ctjs::ValueType::get(&context), nested.getLoc());
        }
        auto error = expandDOMHelpers(*input, "entry$0", 100000);
        check(static_cast<bool>(error), "nested guards refuse region arguments and extra blocks");
        if (error) { llvm::consumeError(std::move(error)); }
    }
    for (const auto & budgetSource : {protectedRead,
                                      trailingRead,
                                      secondWrite,
                                      selectorRead,
                                      finalWrite,
                                      selectedWrite,
                                      finalRead,
                                      fourthWrite,
                                      fourthRead,
                                      fifthRead,
                                      terminalRead,
                                      terminalMatch,
                                      terminalMatchRead,
                                      terminalMatchReads,
                                      alternatingTerminal,
                                      terminalReadWrite,
                                      alternatingTerminalWrite,
                                      terminalReadValue,
                                      alternatingReadValue,
                                      earlierTerminalValue,
                                      alternatingEarlierValue,
                                      terminalSelectorValue,
                                      alternatingSelectorValue,
                                      readBeforeSelector,
                                      readBeforeFirstSelector,
                                      alternatingReadBeforeFirstSelector,
                                      readBeforeSecondWrite,
                                      alternatingReadBeforeSecondWrite,
                                      readBeforeFirstWrite,
                                      alternatingReadBeforeFirstWrite,
                                      readInsideFirstWrite,
                                      readInsideSecondWrite,
                                      readAfterFirstFeeding,
                                      readAfterSecondFeeding,
                                      readThroughFinalArgument,
                                      earlyReadThroughFinalArgument,
                                      selectorThroughFinalArgument,
                                      alternateSelectorThroughFinalArgument,
                                      selectorInsideFinalArgument,
                                      readThroughFinalSelector,
                                      firstWriteSelector,
                                      secondWriteSelector,
                                      firstSelectorWithSavedRead,
                                      selectorBeforeFirstWrite,
                                      selectorBeforeSecondWrite,
                                      earlySavedSelector,
                                      ignoredEarlySelector,
                                      reusedInitialSelector,
                                      reusedInitialRead,
                                      reusedSecondSelector,
                                      conditionalSelector,
                                      conditionalRead,
                                      conditionalDistinct,
                                      conditionalIgnoredRead,
                                      conditionalBranchRead,
                                      conditionalArgumentRead,
                                      conditionalBranchSelector,
                                      conditionalIgnoredSelector,
                                      conditionalTwoReads,
                                      conditionalMixedReads,
                                      conditionalThreeReads,
                                      conditionalPostRead,
                                      conditionalPostSelector,
                                      conditionalMixedPostReads,
                                      conditionalThirdWrite,
                                      conditionalThirdSelector,
                                      conditionalFourthWrite,
                                      conditionalNestedWrite,
                                      conditionalNestedSelector,
                                      conditionalDeeperWrite,
                                      conditionalElseWrite,
                                      conditionalElseSelector,
                                      conditionalElseNested}) {
        auto completeRead = mlir::parseSourceString<mlir::ModuleOp>(budgetSource, &context);
        check(static_cast<bool>(completeRead), "protected read budget fixture parses");
        if (completeRead) {
            unsigned steps = 0;
            auto error = expandDOMHelpers(*completeRead, "entry$0", 100000, &steps);
            check(!error && steps > 0, "protected read records a finite expansion budget");
            if (error) {
                llvm::consumeError(std::move(error));
            } else {
                for (unsigned budget : {0u, steps - 1, steps}) {
                    auto input = mlir::parseSourceString<mlir::ModuleOp>(budgetSource, &context);
                    auto limited = expandDOMHelpers(*input, "entry$0", budget);
                    check(static_cast<bool>(limited) == (budget < steps),
                          "protected read requires its complete expansion budget");
                    if (limited) { llvm::consumeError(std::move(limited)); }
                }
            }
        }
    }
    for (const auto & invalid :
         {replaced(conditionalElseSelector, "[data-else-read]", "["),
          replaced(conditionalElseSelector, "#ctjs.string<\"[data-else-read]\">",
                   "#ctjs.number<0>"),
          replaced(conditionalElseWrite, "    %elseEffect =",
                   "    ctjs.store_global \"leaked\", %elseMethod\n    %elseEffect ="),
          replaced(conditionalElseRead, "    %elseMethod =",
                   "    ctjs.store_global \"leaked\", %elsePresent\n    %elseMethod ="),
          replaced(conditionalElseWrite, "ctjs.call %elseMethod(%element, %elseName, %elseValue)",
                   "ctjs.call %elseMethod(%text, %elseName, %elseValue)"),
          replaced(conditionalElseWrite, "ctjs.call %elseMethod(%element, %elseName, %elseValue)",
                   "ctjs.call %elseMethod(%element, %elseName, %elseValue, %name)"),
          replaced(conditionalElseWrite,
                   "    %elseEffect = ctjs.call %elseMethod(%element, %elseName, %elseValue)\n",
                   ""),
          replaced(conditionalElseRead,
                   "    %elsePresent = ctjs.call %elseReadMethod(%element, %elseReadName)\n",
                   "    %elsePresent = ctjs.constant #ctjs.boolean<false>\n"),
          replaced(conditionalNestedSelector, "[data-third-guard]", "["),
          replaced(conditionalNestedSelector, "#ctjs.string<\"[data-third-guard]\">",
                   "#ctjs.number<0>"),
          replaced(conditionalNestedWrite, "ctjs.truthy %guardPresent", "ctjs.truthy %element"),
          replaced(conditionalNestedWrite, "ctjs.truthy %guardPresent",
                   "ctjs.truthy %secondEffect"),
          replaced(conditionalNestedWrite, "scf.if %nestedCondition", "scf.if %closeCondition"),
          replaced(conditionalNestedWrite, "    %thirdName =",
                   "    ctjs.store_global \"leaked\", %guardPresent\n    %thirdName ="),
          replaced(conditionalNestedWrite, "    %thirdName =",
                   "    ctjs.store_global \"leaked\", %guardMethod\n    %thirdName ="),
          replaced(conditionalNestedWrite, "    scf.yield\n    }\n",
                   "    %unfinished = ctjs.get_property %element[%afterReadKey]\n"
                   "    scf.yield\n    }\n"),
          replaced(conditionalNestedElse, "    } else {\n    scf.yield",
                   "    } else {\n    ctjs.store_global \"leaked\", %guardPresent\n    scf.yield"),
          replaced(replaced(conditionalNestedWrite,
                            "    %thirdMethod = ctjs.get_property %element[%secondKey]\n", ""),
                   "    %nestedCondition =",
                   "    %thirdMethod = ctjs.get_property %element[%secondKey]\n"
                   "    %nestedCondition ="),
          replaced(conditionalNestedWrite, "^bb0(%error: !ctjs.value):",
                   "^bb0(%error: !ctjs.value):\n"
                   "        ctjs.store_global \"effect\", %error"),
          replaced(conditionalNestedWrite,
                   "ctjs.call %thirdMethod(%element, %thirdName, %afterPresent)",
                   "ctjs.call %thirdMethod(%element, %afterPresent, %afterPresent)"),
          replaced(conditionalThirdSelector, "[data-closed]", "["),
          replaced(conditionalThirdSelector, "#ctjs.string<\"[data-closed]\">", "#ctjs.number<0>"),
          replaced(conditionalThirdWrite,
                   "ctjs.call %thirdMethod(%element, %thirdName, %afterPresent)",
                   "ctjs.call %thirdMethod(%text, %thirdName, %afterPresent)"),
          replaced(conditionalThirdWrite,
                   "ctjs.call %thirdMethod(%element, %thirdName, %afterPresent)",
                   "ctjs.call %thirdMethod(%element, %thirdName, %afterPresent, %name)"),
          replaced(conditionalThirdWrite,
                   "ctjs.call %thirdMethod(%element, %thirdName, %afterPresent)",
                   "ctjs.call %thirdMethod(%element, %afterPresent, %afterPresent)"),
          replaced(conditionalThirdWrite,
                   "ctjs.call %thirdMethod(%element, %thirdName, %afterPresent)",
                   "ctjs.call %thirdMethod(%element, %thirdName, %text)"),
          replaced(conditionalThirdWrite, "    scf.yield\n    }\n",
                   "    %unfinished = ctjs.get_property %element[%secondKey]\n"
                   "    scf.yield\n    }\n"),
          replaced(conditionalThirdWrite, "    scf.yield\n    }\n",
                   "    %unfinished = ctjs.get_property %element[%afterReadKey]\n"
                   "    scf.yield\n    }\n"),
          replaced(conditionalThirdWrite, "    scf.yield\n    }\n",
                   "    ctjs.store_global \"leaked\", %thirdEffect\n    scf.yield\n    }\n"),
          replaced(conditionalThirdWrite, "    scf.yield\n    }\n",
                   "    ctjs.store_global \"leaked\", %thirdMethod\n    scf.yield\n    }\n"),
          replaced(conditionalThirdWrite, "    scf.yield\n    }\n",
                   "    ctjs.store_global \"leaked\", %afterPresent\n    scf.yield\n    }\n"),
          replaced(conditionalThirdWrite, "    scf.yield\n    }\n",
                   "    %again = ctjs.call %thirdMethod(%element, %thirdName, %afterPresent)\n"
                   "    scf.yield\n    }\n"),
          replaced(conditionalThirdWrite,
                   "%afterPresent = ctjs.call %afterReadMethod(%element, %afterReadName)",
                   "%afterPresent = ctjs.constant #ctjs.boolean<false>"),
          replaced(replaced(conditionalThirdWrite,
                            "    %thirdName =", "    scf.if %closeCondition {\n    %thirdName ="),
                   "    scf.yield\n    }\n", "    scf.yield\n    }\n    scf.yield\n    }\n"),
          replaced(conditionalFourthWrite, "^bb0(%error: !ctjs.value):",
                   "^bb0(%error: !ctjs.value):\n"
                   "        ctjs.store_global \"effect\", %error"),
          replaced(conditionalPostSelector, "[data-closed]", "["),
          replaced(conditionalPostReads, "[data-next]", "["),
          replaced(conditionalPostSelector, "#ctjs.string<\"[data-closed]\">", "#ctjs.number<0>"),
          replaced(conditionalPostRead,
                   "%afterPresent = ctjs.call %afterReadMethod(%element, %afterReadName)",
                   "%afterPresent = ctjs.constant #ctjs.boolean<false>"),
          replaced(conditionalPostReads,
                   "%anotherPresent = ctjs.call %anotherReadMethod(%element, %anotherReadName)",
                   "%anotherPresent = ctjs.constant #ctjs.boolean<false>"),
          replaced(conditionalPostRead, "    scf.yield\n    }\n",
                   "    ctjs.store_global \"leaked\", %afterPresent\n    scf.yield\n    }\n"),
          replaced(conditionalPostRead, "    scf.yield\n    }\n",
                   "    ctjs.store_global \"leaked\", %afterReadMethod\n    scf.yield\n    }\n"),
          replaced(conditionalPostRead, "    scf.yield\n    }\n",
                   "    %again = ctjs.call %afterReadMethod(%element, %afterReadName)\n"
                   "    scf.yield\n    }\n"),
          replaced(conditionalPostRead, "ctjs.call %afterReadMethod(%element, %afterReadName)",
                   "ctjs.call %afterReadMethod(%text, %afterReadName)"),
          replaced(conditionalPostRead, "ctjs.call %afterReadMethod(%element, %afterReadName)",
                   "ctjs.call %afterReadMethod(%element, %afterReadName, %name)"),
          replaced(conditionalPostRead, "    scf.yield\n    }\n",
                   "    %unfinished = ctjs.get_property %element[%secondKey]\n"
                   "    scf.yield\n    }\n"),
          replaced(conditionalPostRead, "^bb0(%error: !ctjs.value):",
                   "^bb0(%error: !ctjs.value):\n"
                   "        ctjs.store_global \"effect\", %error"),
          replaced(conditionalMixedReads, "[data-next]", "["),
          replaced(conditionalEarlySelectors, "[data-closed]", "["),
          replaced(conditionalThreeReads, "[data-next]", "["),
          replaced(conditionalMixedReads, "#ctjs.string<\"[data-next]\">", "#ctjs.number<0>"),
          replaced(conditionalTwoReads,
                   "%afterPresent = ctjs.call %afterReadMethod(%element, %afterReadName)",
                   "%afterPresent = ctjs.constant #ctjs.boolean<false>"),
          replaced(conditionalTwoReads,
                   "%anotherPresent = ctjs.call %anotherReadMethod(%element, %afterReadName)",
                   "%anotherPresent = ctjs.constant #ctjs.boolean<false>"),
          replaced(conditionalTwoReads, "    %secondEffect =",
                   "    ctjs.store_global \"leaked\", %afterPresent\n    %secondEffect ="),
          replaced(conditionalTwoReads, "    %secondEffect =",
                   "    ctjs.store_global \"leaked\", %anotherPresent\n    %secondEffect ="),
          replaced(conditionalTwoReads, "    %secondEffect =",
                   "    ctjs.store_global \"leaked\", %afterReadMethod\n    %secondEffect ="),
          replaced(conditionalTwoReads, "    %secondEffect =",
                   "    %again = ctjs.call %anotherReadMethod(%element, %afterReadName)\n"
                   "    %secondEffect ="),
          replaced(replaced(conditionalTwoReads, "    %afterReadKey =",
                            "    scf.if %closeCondition {\n    %afterReadKey ="),
                   "    scf.yield\n    }\n", "    scf.yield\n    }\n    scf.yield\n    }\n"),
          replaced(conditionalBranchSelector, "[data-closed]", "["),
          replaced(conditionalIgnoredSelector, "[data-closed]", "["),
          replaced(conditionalBranchSelector, "#ctjs.string<\"[data-closed]\">", "#ctjs.number<0>"),
          replaced(conditionalArgumentRead, "ctjs.call %afterReadMethod(%element, %afterReadName)",
                   "ctjs.call %afterReadMethod(%text, %afterReadName)"),
          replaced(conditionalArgumentRead, "ctjs.call %afterReadMethod(%element, %afterReadName)",
                   "ctjs.call %afterReadMethod(%element, %afterReadName, %name)"),
          replaced(conditionalArgumentRead, "    %secondEffect =",
                   "    %again = ctjs.call %afterReadMethod(%element, %afterReadName)\n"
                   "    %secondEffect ="),
          replaced(conditionalArgumentRead,
                   "%afterPresent = ctjs.call %afterReadMethod(%element, %afterReadName)",
                   "%afterPresent = ctjs.constant #ctjs.boolean<false>"),
          replaced(conditionalArgumentRead, "    %secondEffect =",
                   "    ctjs.store_global \"leaked\", %afterPresent\n"
                   "    %secondEffect ="),
          replaced(conditionalArgumentRead,
                   "ctjs.call %secondMethod(%element, %secondName, %afterPresent)",
                   "ctjs.call %secondMethod(%element, %afterPresent, %afterPresent)"),
          replaced(conditionalArgumentRead, "    %secondEffect =",
                   "    %record = ctjs.create_object\n"
                   "    ctjs.set_property %record[%name], %afterPresent\n"
                   "    %secondEffect ="),
          replaced(conditionalArgumentRead, "    %secondEffect =",
                   "    ctjs.store_global \"leaked\", %afterReadMethod\n"
                   "    %secondEffect ="),
          replaced(conditionalArgumentRead, "^bb0(%error: !ctjs.value):",
                   "^bb0(%error: !ctjs.value):\n"
                   "        ctjs.store_global \"effect\", %error"),
          replaced(conditionalSelector, "[data-visited]", "["),
          replaced(conditionalSelector, "ctjs.call %readMethod(%element, %readName)",
                   "ctjs.call %readMethod(%element, %element)"),
          replaced(conditionalSelector, "ctjs.truthy %present", "ctjs.truthy %element"),
          replaced(conditionalSelector, "ctjs.truthy %present", "ctjs.truthy %effect"),
          replaced(conditionalSelector, "    %answer = ctjs.create_object",
                   "    ctjs.store_global \"leaked\", %present\n"
                   "    %answer = ctjs.create_object"),
          replaced(conditionalSelector, "    scf.yield\n    }\n",
                   "    scf.yield\n    } else {\n"
                   "    ctjs.store_global \"leaked\", %present\n    scf.yield\n    }\n"),
          replaced(replaced(conditionalSelector,
                            "    %secondKey =", "    scf.if %closeCondition {\n    %secondKey ="),
                   "    scf.yield\n    }\n", "    scf.yield\n    }\n    scf.yield\n    }\n"),
          replaced(conditionalSelector, "    %secondEffect =",
                   "    %extra = ctjs.call %secondMethod(%element, %secondName, %present)\n"
                   "    %secondEffect ="),
          replaced(conditionalSelector, "    %secondEffect =",
                   "    ctjs.store_global \"leaked\", %present\n    %secondEffect ="),
          replaced(reusedInitialSelector, "[data-visited]", "["),
          replaced(reusedInitialSelector, "#ctjs.string<\"[data-visited]\">", "#ctjs.number<0>"),
          replaced(reusedInitialSelector, "ctjs.call %readMethod(%element, %readName)",
                   "ctjs.call %readMethod(%element, %readName, %name)"),
          replaced(reusedInitialSelector, "%answer = ctjs.create_object",
                   "ctjs.store_global \"leaked\", %present\n"
                   "    %answer = ctjs.create_object"),
          replaced(reusedInitialSelector, "%answer = ctjs.create_object",
                   "ctjs.store_global \"leaked\", %afterPresent\n"
                   "    %answer = ctjs.create_object"),
          replaced(reusedInitialSelector,
                   "ctjs.call %secondMethod(%element, %secondName, %present)",
                   "ctjs.call %secondMethod(%element, %present, %present)"),
          replaced(reusedInitialSelector, "%answer = ctjs.create_object",
                   "%again = ctjs.call %readMethod(%element, %readName)\n"
                   "    %answer = ctjs.create_object"),
          replaced(reusedInitialSelector,
                   "    %afterPresent = ctjs.call %afterReadMethod(%element, %afterReadName)\n",
                   ""),
          replaced(reusedInitialSelector, "^bb0(%error: !ctjs.value):",
                   "^bb0(%error: !ctjs.value):\n"
                   "        ctjs.store_global \"effect\", %error"),
          replaced(selectorBeforeFirstWrite, "[data-visited]", "["),
          replaced(selectorBeforeFirstWrite, "ctjs.call %readMethod(%element, %readName)",
                   "ctjs.call %readMethod(%element, %element)"),
          replaced(ignoredEarlySelector, "[data-terminal]", "["),
          replaced(ignoredEarlySelector,
                   "ctjs.call %terminalReadMethod(%element, %terminalReadName)",
                   "ctjs.call %terminalReadMethod(%element, %element)"),
          replaced(earlySavedSelector, "#ctjs.string<\"[data-terminal]\">", "#ctjs.number<0>"),
          replaced(selectorBeforeFirstWrite, "%answer = ctjs.create_object",
                   "ctjs.store_global \"leaked\", %present\n"
                   "    %answer = ctjs.create_object"),
          replaced(earlySavedSelector, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %terminalPresent"),
          replaced(selectorBeforeSecondWrite, "%answer = ctjs.create_object",
                   "%again = ctjs.call %afterReadMethod(%element, %afterReadName)\n"
                   "    %answer = ctjs.create_object"),
          replaced(selectorBeforeFirstWrite, "ctjs.call %readMethod(%element, %readName)",
                   "ctjs.call %readMethod(%element, %readName, %name)"),
          replaced(selectorBeforeSecondWrite,
                   "ctjs.call %afterReadMethod(%element, %afterReadName)",
                   "ctjs.call %afterReadMethod(%text, %afterReadName)"),
          replaced(selectorBeforeFirstWrite,
                   "%present = ctjs.call %readMethod(%element, %readName)",
                   "%present = ctjs.constant #ctjs.boolean<false>"),
          replaced(earlySavedSelector, "^bb0(%error: !ctjs.value):",
                   "^bb0(%error: !ctjs.value):\n"
                   "        ctjs.store_global \"effect\", %error"),
          replaced(readBeforeSecondWrite, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %afterTerminalPresent"),
          replaced(readBeforeSecondWrite, "%answer = ctjs.create_object",
                   "ctjs.store_global \"leaked\", %afterTerminalPresent\n"
                   "    %answer = ctjs.create_object"),
          replaced(readBeforeSecondWrite, "%answer = ctjs.create_object",
                   "%again = ctjs.call %afterTerminalMethod(%element, %afterTerminalName)\n"
                   "    %answer = ctjs.create_object"),
          replaced(readBeforeSecondWrite,
                   "ctjs.call %afterTerminalMethod(%element, %afterTerminalName)",
                   "ctjs.call %afterTerminalMethod(%element, %afterTerminalName, %name)"),
          replaced(readBeforeSecondWrite,
                   "ctjs.call %lateMethod(%element, %lateName, %afterTerminalPresent)",
                   "ctjs.call %lateMethod(%element, %afterTerminalPresent, %afterTerminalPresent)"),
          replaced(readBeforeSecondWrite, "^bb0(%error: !ctjs.value):",
                   "^bb0(%error: !ctjs.value):\n"
                   "        ctjs.store_global \"effect\", %error"),
          replaced(readBeforeFirstWrite, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %afterTerminalPresent"),
          replaced(readBeforeFirstWrite, "%answer = ctjs.create_object",
                   "ctjs.store_global \"leaked\", %afterTerminalPresent\n"
                   "    %answer = ctjs.create_object"),
          replaced(readBeforeFirstWrite, "%answer = ctjs.create_object",
                   "%again = ctjs.call %afterTerminalMethod(%element, %afterTerminalName)\n"
                   "    %answer = ctjs.create_object"),
          replaced(readBeforeFirstWrite,
                   "ctjs.call %afterTerminalMethod(%element, %afterTerminalName)",
                   "ctjs.call %afterTerminalMethod(%element, %afterTerminalName, %name)"),
          replaced(readBeforeFirstWrite,
                   "ctjs.call %lateMethod(%element, %lateName, %afterTerminalPresent)",
                   "ctjs.call %lateMethod(%element, %afterTerminalPresent, %afterTerminalPresent)"),
          replaced(readBeforeFirstWrite, "^bb0(%error: !ctjs.value):",
                   "^bb0(%error: !ctjs.value):\n"
                   "        ctjs.store_global \"effect\", %error"),
          replaced(readInsideFirstWrite, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %afterTerminalPresent"),
          replaced(readInsideSecondWrite, "%answer = ctjs.create_object",
                   "ctjs.store_global \"leaked\", %afterTerminalPresent\n"
                   "    %answer = ctjs.create_object"),
          replaced(readInsideFirstWrite, "%answer = ctjs.create_object",
                   "%again = ctjs.call %afterTerminalMethod(%element, %afterTerminalName)\n"
                   "    %answer = ctjs.create_object"),
          replaced(readInsideFirstWrite,
                   "ctjs.call %afterTerminalMethod(%element, %afterTerminalName)",
                   "ctjs.call %afterTerminalMethod(%element, %afterTerminalName, %name)"),
          replaced(readInsideSecondWrite,
                   "ctjs.call %lateMethod(%element, %lateName, %afterTerminalPresent)",
                   "ctjs.call %lateMethod(%element, %afterTerminalPresent, %afterTerminalPresent)"),
          replaced(readInsideFirstWrite,
                   "%afterTerminalPresent = ctjs.call %afterTerminalMethod(%element, "
                   "%afterTerminalName)",
                   "%afterTerminalPresent = ctjs.constant #ctjs.boolean<false>"),
          replaced(readAfterFirstFeeding, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %afterTerminalPresent"),
          replaced(readAfterSecondFeeding, "%answer = ctjs.create_object",
                   "ctjs.store_global \"leaked\", %afterPresent\n"
                   "    %answer = ctjs.create_object"),
          replaced(readAfterFirstFeeding, "%answer = ctjs.create_object",
                   "%again = ctjs.call %afterTerminalMethod(%element, %afterTerminalName)\n"
                   "    %answer = ctjs.create_object"),
          replaced(readAfterFirstFeeding, "%present = ctjs.call %readMethod(%element, %readName)",
                   "%present = ctjs.constant #ctjs.boolean<false>"),
          replaced(readAfterSecondFeeding,
                   "%afterPresent = ctjs.call %afterReadMethod(%element, %afterReadName)",
                   "%afterPresent = ctjs.constant #ctjs.boolean<false>"),
          replaced(readAfterFirstFeeding,
                   "%afterTerminalPresent = ctjs.call %afterTerminalMethod(%element, "
                   "%afterTerminalName)",
                   "%afterTerminalPresent = ctjs.constant #ctjs.boolean<false>"),
          replaced(readAfterSecondFeeding,
                   "ctjs.call %afterTerminalMethod(%element, %afterTerminalName)",
                   "ctjs.call %afterTerminalMethod(%element, %afterTerminalName, %name)"),
          replaced(readThroughFinalArgument, "%answer = ctjs.create_object",
                   "ctjs.store_global \"leaked\", %latePresent\n"
                   "    %answer = ctjs.create_object"),
          replaced(earlyReadThroughFinalArgument, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %afterTerminalPresent"),
          replaced(readThroughFinalArgument, "%answer = ctjs.create_object",
                   "%again = ctjs.call %lateReadMethod(%element, %lateReadName)\n"
                   "    %answer = ctjs.create_object"),
          replaced(readThroughFinalArgument, "ctjs.call %lateReadMethod(%element, %lateReadName)",
                   "ctjs.call %lateReadMethod(%text, %lateReadName)"),
          replaced(readThroughFinalArgument, "ctjs.call %lateReadMethod(%element, %lateReadName)",
                   "ctjs.call %lateReadMethod(%element, %lateReadName, %name)"),
          replaced(readThroughFinalArgument,
                   "ctjs.call %lateMethod(%element, %lateName, %afterTerminalPresent)",
                   "ctjs.call %lateMethod(%element, %latePresent, %afterTerminalPresent)"),
          replaced(readThroughFinalArgument, "^bb0(%error: !ctjs.value):",
                   "^bb0(%error: !ctjs.value):\n"
                   "        ctjs.store_global \"effect\", %error"),
          replaced(selectorThroughFinalArgument, "[data-terminal]", "["),
          replaced(alternateSelectorThroughFinalArgument, "[data-alternate-terminal]", "["),
          replaced(selectorThroughFinalArgument, "#ctjs.string<\"[data-terminal]\">",
                   "#ctjs.number<0>"),
          replaced(selectorThroughFinalArgument, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %terminalPresent"),
          replaced(selectorThroughFinalArgument, "%answer = ctjs.create_object",
                   "ctjs.store_global \"leaked\", %latePresent\n"
                   "    %answer = ctjs.create_object"),
          replaced(selectorThroughFinalArgument, "%answer = ctjs.create_object",
                   "%again = ctjs.call %terminalReadMethod(%element, %terminalReadName)\n"
                   "    %answer = ctjs.create_object"),
          replaced(selectorThroughFinalArgument,
                   "ctjs.call %terminalReadMethod(%element, %terminalReadName)",
                   "ctjs.call %terminalReadMethod(%element, %terminalReadName, %name)"),
          replaced(selectorThroughFinalArgument,
                   "ctjs.call %lateMethod(%element, %lateName, %terminalPresent)",
                   "ctjs.call %lateMethod(%element, %terminalPresent, %terminalPresent)"),
          replaced(selectorThroughFinalArgument, "^bb0(%error: !ctjs.value):",
                   "^bb0(%error: !ctjs.value):\n"
                   "        ctjs.store_global \"effect\", %error"),
          replaced(selectorInsideFinalArgument, "[data-terminal]", "["),
          replaced(selectorInsideFinalArgument, "#ctjs.string<\"[data-terminal]\">",
                   "#ctjs.number<0>"),
          replaced(selectorInsideFinalArgument, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %terminalPresent"),
          replaced(selectorInsideFinalArgument, "%answer = ctjs.create_object",
                   "ctjs.store_global \"leaked\", %latePresent\n"
                   "    %answer = ctjs.create_object"),
          replaced(selectorInsideFinalArgument, "%answer = ctjs.create_object",
                   "%again = ctjs.call %lateReadMethod(%element, %lateReadName)\n"
                   "    %answer = ctjs.create_object"),
          replaced(selectorInsideFinalArgument,
                   "ctjs.call %lateReadMethod(%element, %lateReadName)",
                   "ctjs.call %lateReadMethod(%element, %lateReadName, %name)"),
          replaced(selectorInsideFinalArgument,
                   "ctjs.call %lateReadMethod(%element, %lateReadName)",
                   "ctjs.call %lateReadMethod(%text, %lateReadName)"),
          replaced(selectorInsideFinalArgument,
                   "ctjs.call %lateMethod(%element, %lateName, %terminalPresent)",
                   "ctjs.call %lateMethod(%element, %terminalPresent, %terminalPresent)"),
          replaced(selectorInsideFinalArgument, "^bb0(%error: !ctjs.value):",
                   "^bb0(%error: !ctjs.value):\n"
                   "        ctjs.store_global \"effect\", %error"),
          replaced(firstWriteSelector, "[data-visited]", "["),
          replaced(firstWriteSelector, "#ctjs.string<\"[data-visited]\">", "#ctjs.number<0>"),
          replaced(ignoredFirstSelector, "[data-visited]", "["),
          replaced(ignoredFirstSelector, "ctjs.call %readMethod(%element, %readName)",
                   "ctjs.call %readMethod(%element, %element)"),
          replaced(firstWriteSelector, "ctjs.call %readMethod(%element, %readName)",
                   "ctjs.call %readMethod(%element, %element)"),
          replaced(secondWriteSelector,
                   "%afterReadName = ctjs.constant #ctjs.string<\"[data-closed]\">",
                   "%afterReadName = ctjs.constant #ctjs.string<\"[\">"),
          replaced(firstWriteSelector, "%answer = ctjs.create_object",
                   "ctjs.store_global \"leaked\", %present\n"
                   "    %answer = ctjs.create_object"),
          replaced(firstSelectorWithSavedRead, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %present"),
          replaced(secondWriteSelector, "%answer = ctjs.create_object",
                   "%again = ctjs.call %afterReadMethod(%element, %afterReadName)\n"
                   "    %answer = ctjs.create_object"),
          replaced(firstWriteSelector, "ctjs.call %readMethod(%element, %readName)",
                   "ctjs.call %readMethod(%element, %readName, %name)"),
          replaced(secondWriteSelector, "ctjs.call %afterReadMethod(%element, %afterReadName)",
                   "ctjs.call %afterReadMethod(%text, %afterReadName)"),
          replaced(firstWriteSelector, "^bb0(%error: !ctjs.value):",
                   "^bb0(%error: !ctjs.value):\n"
                   "        ctjs.store_global \"effect\", %error"),
          replaced(readBeforeFirstSelector, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %afterTerminalPresent"),
          replaced(readBeforeFirstSelector, "%answer = ctjs.create_object",
                   "ctjs.store_global \"leaked\", %afterTerminalPresent\n"
                   "    %answer = ctjs.create_object"),
          replaced(readBeforeFirstSelector, "%answer = ctjs.create_object",
                   "%again = ctjs.call %afterTerminalMethod(%element, %afterTerminalName)\n"
                   "    %answer = ctjs.create_object"),
          replaced(readBeforeFirstSelector,
                   "ctjs.call %afterTerminalMethod(%element, %afterTerminalName)",
                   "ctjs.call %afterTerminalMethod(%element, %afterTerminalName, %name)"),
          replaced(readBeforeFirstSelector,
                   "ctjs.call %lateMethod(%element, %lateName, %afterTerminalPresent)",
                   "ctjs.call %lateMethod(%element, %afterTerminalPresent, %afterTerminalPresent)"),
          replaced(readBeforeFirstSelector, "^bb0(%error: !ctjs.value):",
                   "^bb0(%error: !ctjs.value):\n"
                   "        ctjs.store_global \"effect\", %error"),
          replaced(readBeforeSelector, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %afterTerminalPresent"),
          replaced(readBeforeSelector,
                   "ctjs.call %afterTerminalMethod(%element, %afterTerminalName)",
                   "ctjs.call %afterTerminalMethod(%element, %afterTerminalName, %name)"),
          replaced(terminalSelectorValue, "[data-terminal]", "["),
          replaced(terminalSelectorValue, "#ctjs.string<\"[data-terminal]\">", "#ctjs.number<0>"),
          replaced(lastSelectorValue, "[data-alternate-terminal]", "["),
          replaced(terminalSelectorValue,
                   "ctjs.call %terminalReadMethod(%element, %terminalReadName)",
                   "ctjs.call %terminalReadMethod(%element, %terminalReadName, %name)"),
          replaced(terminalSelectorValue, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %terminalPresent"),
          replaced(terminalSelectorValue, "%answer = ctjs.create_object",
                   "ctjs.store_global \"leaked\", %terminalPresent\n"
                   "    %answer = ctjs.create_object"),
          replaced(terminalSelectorValue,
                   "ctjs.call %lateMethod(%element, %lateName, %terminalPresent)",
                   "ctjs.call %lateMethod(%element, %terminalPresent, %terminalPresent)"),
          replaced(terminalSelectorValue, "%answer = ctjs.create_object",
                   "%again = ctjs.call %terminalReadMethod(%element, %terminalReadName)\n"
                   "    %answer = ctjs.create_object"),
          replaced(terminalSelectorValue, "^bb0(%error: !ctjs.value):",
                   "^bb0(%error: !ctjs.value):\n"
                   "        ctjs.store_global \"effect\", %error"),
          replaced(earlierTerminalValue, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %afterTerminalPresent"),
          replaced(earlierTerminalValue, "%answer = ctjs.create_object",
                   "%again = ctjs.call %afterTerminalMethod(%element, %afterTerminalName)\n"
                   "    %answer = ctjs.create_object"),
          replaced(earlierTerminalValue,
                   "ctjs.call %lateMethod(%element, %lateName, %afterTerminalPresent)",
                   "ctjs.call %lateMethod(%element, %afterTerminalPresent, %afterTerminalPresent)"),
          replaced(earlierTerminalValue,
                   "ctjs.call %lateMethod(%element, %lateName, %afterTerminalPresent)",
                   "ctjs.call %lateMethod(%text, %lateName, %afterTerminalPresent)"),
          replaced(earlierTerminalValue,
                   "ctjs.call %afterTerminalMethod(%element, %afterTerminalName)",
                   "ctjs.call %afterTerminalMethod(%element, %afterTerminalName, %name)"),
          replaced(terminalReadValue, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %latePresent"),
          replaced(terminalReadValue, "%answer = ctjs.create_object",
                   "%again = ctjs.call %lateReadMethod(%element, %lateReadName)\n"
                   "    %answer = ctjs.create_object"),
          replaced(terminalReadWrite, "ctjs.call %lateMethod(%element, %lateName, %lateValue)",
                   "ctjs.call %lateMethod(%text, %lateName, %lateValue)"),
          replaced(terminalReadWrite, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %lateEffect"),
          replaced(terminalReadWrite, "%answer = ctjs.create_object",
                   "%again = ctjs.call %lateMethod(%element, %lateName, %lateValue)\n"
                   "    %answer = ctjs.create_object"),
          replaced(terminalReadWrite, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %afterTerminalPresent"),
          replaced(terminalMatchReads,
                   "ctjs.call %secondTerminalMethod(%element, %secondTerminalName)",
                   "ctjs.call %secondTerminalMethod(%element, %secondTerminalName, %name)"),
          replaced(terminalMatchReads,
                   "ctjs.call %secondTerminalMethod(%element, %secondTerminalName)",
                   "ctjs.call %secondTerminalMethod(%text, %secondTerminalName)"),
          replaced(terminalMatchReads, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %afterTerminalPresent"),
          replaced(terminalMatchReads, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %secondTerminalPresent"),
          replaced(terminalMatchReads, "%answer = ctjs.create_object",
                   "%again = ctjs.call %afterTerminalMethod(%element, %afterTerminalName)\n"
                   "    %answer = ctjs.create_object"),
          replaced(alternatingTerminal, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %alternateTerminalPresent"),
          replaced(alternatingTerminal, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %lastTerminalPresent"),
          replaced(terminalMatchRead,
                   "ctjs.call %afterTerminalMethod(%element, %afterTerminalName)",
                   "ctjs.call %afterTerminalMethod(%element, %afterTerminalName, %name)"),
          replaced(terminalMatchRead, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %afterTerminalPresent"),
          replaced(terminalMatchRead, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %terminalPresent"),
          replaced(terminalMatchRead, "%answer = ctjs.create_object",
                   "%again = ctjs.call %afterTerminalMethod(%element, %afterTerminalName)\n"
                   "    %answer = ctjs.create_object"),
          replaced(terminalMatch, "ctjs.call %terminalReadMethod(%element, %terminalReadName)",
                   "ctjs.call %terminalReadMethod(%text, %terminalReadName)"),
          replaced(terminalMatch, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %terminalPresent"),
          replaced(terminalMatch, "%answer = ctjs.create_object",
                   "%again = ctjs.call %terminalReadMethod(%element, %terminalReadName)\n"
                   "    %answer = ctjs.create_object"),
          replaced(terminalRead, "ctjs.call %terminalReadMethod(%element, %terminalReadName)",
                   "ctjs.call %terminalReadMethod(%text, %terminalReadName)"),
          replaced(terminalRead,
                   "%terminalPresent = ctjs.call %terminalReadMethod(%element, "
                   "%terminalReadName)",
                   "%terminalPresent = ctjs.constant #ctjs.boolean<false>"),
          replaced(terminalRead, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %terminalPresent"),
          replaced(terminalRead, "%answer = ctjs.create_object",
                   "%again = ctjs.call %terminalReadMethod(%element, %terminalReadName)\n"
                   "    %answer = ctjs.create_object"),
          replaced(
              fourthRead, "%answer = ctjs.create_object",
              "ctjs.store_global \"leaked\", %fourthPresent\n    %answer = ctjs.create_object"),
          replaced(
              fourthRead, "%answer = ctjs.create_object",
              "%answer = ctjs.create_object\n    ctjs.set_property %answer[%name], %fourthEffect"),
          replaced(fourthWrite, "ctjs.call %fourthMethod(%element, %fourthName, %fourthValue)",
                   "ctjs.call %fourthMethod(%element, %fourthName, %selected)"),
          replaced(finalRead, "ctjs.call %finalReadMethod(%element, %finalReadName)",
                   "ctjs.call %finalReadMethod(%text, %finalReadName)"),
          replaced(finalRead, "ctjs.call %finalReadMethod(%element, %finalReadName)",
                   "ctjs.call %finalReadMethod(%element, %finalReadName, %name)"),
          replaced(finalRead,
                   "%finalPresent = ctjs.call %finalReadMethod(%element, %finalReadName)",
                   "%finalPresent = ctjs.constant #ctjs.boolean<false>"),
          replaced(finalRead, "ctjs.call %finalMethod(%element, %finalName, %finalPresent)",
                   "ctjs.call %finalMethod(%element, %finalName, %finalValue)"),
          replaced(finalRead, "%answer = ctjs.create_object",
                   "ctjs.store_global \"leaked\", %finalPresent\n"
                   "    %answer = ctjs.create_object"),
          replaced(finalRead, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %finalPresent"),
          replaced(finalRead, "%answer = ctjs.create_object",
                   "%again = ctjs.call %finalReadMethod(%element, %finalReadName)\n"
                   "    %answer = ctjs.create_object"),
          replaced(selectedWrite, "[data-closed]", "["),
          replaced(selectedWrite, "ctjs.call %selectorMethod(%element, %selectorText)",
                   "ctjs.call %selectorMethod(%element, %element)"),
          replaced(selectedWrite, "ctjs.call %finalMethod(%element, %finalName, %selected)",
                   "ctjs.call %finalMethod(%element, %selected, %selected)"),
          replaced(selectedWrite, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %selected"),
          replaced(selectedWrite, "%answer = ctjs.create_object",
                   "ctjs.store_global \"leaked\", %selected\n"
                   "    %answer = ctjs.create_object"),
          replaced(finalWrite, "ctjs.call %finalMethod(%element, %finalName, %finalValue)",
                   "ctjs.call %finalMethod(%text, %finalName, %finalValue)"),
          replaced(finalWrite, "ctjs.call %finalMethod(%element, %finalName, %finalValue)",
                   "ctjs.call %finalMethod(%element, %finalName)"),
          replaced(finalWrite, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %finalEffect"),
          replaced(finalWrite, "%answer = ctjs.create_object",
                   "%fourth = ctjs.call %finalMethod(%element, %finalName, %text)\n"
                   "    %answer = ctjs.create_object"),
          replaced(selectorRead, "ctjs.call %selectorMethod(%element, %selectorText)",
                   "ctjs.call %selectorMethod(%text, %selectorText)"),
          replaced(selectorRead, "ctjs.call %selectorMethod(%element, %selectorText)",
                   "ctjs.call %selectorMethod(%element)"),
          replaced(selectorRead, "ctjs.call %selectorMethod(%element, %selectorText)",
                   "ctjs.call %selectorMethod(%element, %selectorText, %name)"),
          replaced(selectorRead, "%selected = ctjs.call %selectorMethod(%element, %selectorText)",
                   ""),
          replaced(selectorRead, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %selected"),
          replaced(selectorRead, "%answer = ctjs.create_object",
                   "%again = ctjs.call %selectorMethod(%element, %selectorText)\n"
                   "    %answer = ctjs.create_object"),
          replaced(selectorRead, "%answer = ctjs.create_object",
                   "%late = ctjs.call %secondMethod(%element, %secondName, %afterPresent)\n"
                   "    %answer = ctjs.create_object"),
          replaced(secondWrite, "ctjs.call %secondMethod(%element, %secondName, %afterPresent)",
                   "ctjs.call %secondMethod(%text, %secondName, %afterPresent)"),
          replaced(secondWrite, "ctjs.call %secondMethod(%element, %secondName, %afterPresent)",
                   "ctjs.call %secondMethod(%element, %secondName, %text)"),
          replaced(secondWrite, "ctjs.call %secondMethod(%element, %secondName, %afterPresent)",
                   "ctjs.call %secondMethod(%element, %secondName)"),
          replaced(secondWrite,
                   "%secondEffect = ctjs.call %secondMethod(%element, %secondName, %afterPresent)",
                   ""),
          replaced(secondWrite, "%answer = ctjs.create_object",
                   "%third = ctjs.call %secondMethod(%element, %secondName, %afterPresent)\n"
                   "    %answer = ctjs.create_object"),
          replaced(secondWrite, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %secondEffect"),
          replaced(secondWrite, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %afterPresent"),
          replaced(trailingRead, "ctjs.call %afterReadMethod(%element, %afterReadName)",
                   "ctjs.call %afterReadMethod(%text, %afterReadName)"),
          replaced(trailingRead, "ctjs.call %afterReadMethod(%element, %afterReadName)",
                   "ctjs.call %afterReadMethod(%element, %afterReadName, %name)"),
          replaced(trailingRead,
                   "%afterPresent = ctjs.call %afterReadMethod(%element, %afterReadName)", ""),
          replaced(trailingRead, "%answer = ctjs.create_object",
                   "%again = ctjs.call %afterReadMethod(%element, %afterReadName)\n"
                   "    %answer = ctjs.create_object"),
          replaced(trailingRead, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %afterPresent"),
          replaced(protectedRead, "hasAttribute", "getAttribute"),
          replaced(replaced(protectedRead,
                            "    %present = ctjs.call %readMethod(%element, %readName)\n", ""),
                   "ctjs.call %attribute(%element, %name, %present)",
                   "ctjs.call %attribute(%element, %name, %text)"),
          replaced(protectedRead, "ctjs.call %readMethod(%element, %readName)",
                   "ctjs.call %readMethod(%text, %readName)"),
          replaced(protectedRead, "ctjs.call %readMethod(%element, %readName)",
                   "ctjs.call %readMethod(%element, %readName, %name)"),
          replaced(protectedRead, "%effect = ctjs.call %attribute(%element, %name, %present)",
                   "%secondRead = ctjs.call %readMethod(%element, %readName)\n"
                   "    %effect = ctjs.call %attribute(%element, %name, %present)"),
          replaced(protectedRead, "ctjs.call %attribute(%element, %name, %present)",
                   "ctjs.call %attribute(%element, %present, %present)"),
          replaced(protectedRead, "ctjs.call %attribute(%element, %name, %present)",
                   "ctjs.call %attribute(%element, %name, %text)"),
          replaced(protectedRead, "%answer = ctjs.create_object",
                   "%late = ctjs.call %readMethod(%element, %readName)\n"
                   "    %answer = ctjs.create_object"),
          replaced(protectedRead, "%answer = ctjs.create_object",
                   "ctjs.store_global \"leaked\", %present\n"
                   "    %answer = ctjs.create_object"),
          replaced(discardedState, "ctjs.set_property %answer[%name], %text",
                   "ctjs.set_property %answer[%element], %text"),
          replaced(discardedState, "ctjs.set_property %answer[%name], %text",
                   "ctjs.set_property %answer[%name], %answer"),
          replaced(discardedState, "#ctjs.string<\"data-closed\">", "#ctjs.string<\"__proto__\">"),
          replaced(scalarState, "ctjs.binary add %one, %one",
                   "ctjs.call %attribute(%element, %name, %text)"),
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
        if (!error) { llvm::errs() << "Unexpected protected admission:\n" << invalid << "\n"; }
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

    const std::string throwingSource = R"MLIR(
module {
  ctjs.func @saved$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %element: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %frame = ctjs.frame_enter 1
    %payload = ctjs.constant #ctjs.number<4607182418800017408>
    %normal = ctjs.constant #ctjs.number<4611686018427387904>
    %equal = ctjs.compare strict_eq %element, %element
    %condition = ctjs.truthy %equal
    %answer = scf.if %condition -> (!ctjs.value) {
      scf.execute_region {
        ctjs.throw %payload
      } {no_inline}
      scf.yield %normal : !ctjs.value
    } else {
      ctjs.frame_exit %frame
      scf.yield %normal : !ctjs.value
    }
    ctjs.return %answer
  }
}
)MLIR";
    for (auto payload :
         {"#ctjs.number<4607182418800017408>", "#ctjs.string<\"saved\">", "#ctjs.boolean<true>"}) {
        auto text = replaced(throwingSource, "#ctjs.number<4607182418800017408>", payload);
        auto input = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(input), "saved primitive throw fixture parses");
        if (!input) { continue; }
        auto bound = contract;
        bound.entry = "saved$0";
        bound.moduleSha256 = hostContractFingerprint(*input);
        for (auto provider :
             {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
            bound.provider = provider;
            DOMEntryAnalysis proof(*input, bound);
            check(proof.proved() && proof.savedThrows().size() == 1 &&
                      proof.unreachableYields().size() == 1,
                  "saved primitive throws preserve the reachable sibling frame join");
            if (!proof.proved()) { std::fprintf(stderr, "%s\n", proof.reason().str().c_str()); }
            if (!proof.proved()) { continue; }
            check(DOMEntryAnalysis(*input, bound, proof.steps()).proved(),
                  "saved primitive throw reproduces its exact proof budget");
            for (unsigned budget = 0; budget < proof.steps(); ++budget) {
                DOMEntryAnalysis limited(*input, bound, budget);
                check(!limited.proved() && limited.exhausted() && limited.savedThrows().empty() &&
                          limited.unreachableYields().empty() && !limited.entry(),
                      "incomplete throw proof publishes no evidence");
            }
        }
    }
    for (const auto & invalid :
         {replaced(throwingSource, "ctjs.throw %payload", "ctjs.throw %element"),
          replaced(throwingSource, "#ctjs.number<4607182418800017408>", "#ctjs.null"),
          replaced(throwingSource, "#ctjs.number<4607182418800017408>", "#ctjs.undefined"),
          replaced(throwingSource, " {no_inline}", ""),
          replaced(throwingSource, "ctjs.throw %payload",
                   "%local = ctjs.constant #ctjs.number<4613937818241073152>\n        ctjs.throw "
                   "%local"),
          replaced(throwingSource, "      } {no_inline}",
                   "      } {no_inline}\n      ctjs.frame_exit %frame"),
          replaced(throwingSource, "      ctjs.frame_exit %frame\n", ""),
          replaced(throwingSource, "    ctjs.return %answer",
                   "    ctjs.store_global \"escaped\", %element\n    ctjs.return %answer")}) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
        check(static_cast<bool>(input), "unsupported saved throw fixture parses");
        if (!input) { continue; }
        auto bound = contract;
        bound.entry = "saved$0";
        bound.moduleSha256 = hostContractFingerprint(*input);
        DOMEntryAnalysis proof(*input, bound);
        check(!proof.proved() && proof.savedThrows().empty() && !proof.entry(),
              "unproved payloads, regions, continuations and late effects publish no throw proof");
    }
}

} // namespace ctcompile::test::host_contract
