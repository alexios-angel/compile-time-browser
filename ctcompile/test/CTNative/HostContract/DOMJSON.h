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
        const bool trailingReads = valid.find("%afterReadKey") != std::string::npos;
        const bool secondWrites = valid.find("%secondKey") != std::string::npos;
        const bool selectorReads = valid.find("%selectorKey") != std::string::npos;
        const bool finalWrites = valid.find("%finalKey") != std::string::npos;
        const bool selectedWrites = valid.find("%finalName, %selected)") != std::string::npos;
        const bool finalReads = valid.find("%finalReadKey") != std::string::npos;
        const bool terminalReads = valid.find("%terminalReadKey") != std::string::npos;
        const bool afterTerminalReads = valid.find("%afterTerminalKey") != std::string::npos;
        const bool beforeSelectorRead =
            valid.find("%afterTerminalKey =") < valid.find("%terminalReadKey =");
        const bool beforeFirstSelectorRead =
            valid.find("%afterTerminalKey =") < valid.find("%selectorKey =");
        const bool secondTerminalReads = valid.find("%secondTerminalKey") != std::string::npos;
        const bool alternatingReads = valid.find("%alternateTerminalKey") != std::string::npos;
        const bool lateWrites = valid.find("%lateMethod") != std::string::npos;
        const bool lateReads = valid.find("%lateReadKey") != std::string::npos;
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
        const unsigned suffixWrites =
            static_cast<unsigned>(valid.find("%fourthKey") != std::string::npos) +
            static_cast<unsigned>(valid.find("%fifthKey") != std::string::npos);
        const unsigned suffixReads =
            static_cast<unsigned>(valid.find("%fourthReadKey") != std::string::npos) +
            static_cast<unsigned>(valid.find("%fifthReadKey") != std::string::npos);
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
                    check(readMethod && ctjs::constantKey(readMethod.getKey()) == "hasAttribute" &&
                              precedingReads == 3 && read->getBlock() == invoke->getBlock() &&
                              read->isBeforeInBlock(invoke),
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
                if (ctjs::constantKey(call.getArgs()[0]) == "data-after-terminal") { saved = call; }
                auto method = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                if (ctjs::constantKey(method.getKey()) != "matches" ||
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
                                       static_cast<unsigned>(savedSelectorValue) &&
                          retainsValue && llvm::isa<mlir::scf::IfOp>(invoke->getParentOp()),
                      "late write retains its value, selector order and source guard");
                for (ctjs::CallOp read : invoke->getBlock()->getOps<ctjs::CallOp>()) {
                    if (read.getResult().use_empty()) {
                        check(read->isBeforeInBlock(method),
                              "every unused terminal read remains before the late write lookup");
                    }
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
                        beforeFirstSelectorRead
                            ? 2u
                            : 6u - static_cast<unsigned>(savedTerminalSelector) -
                                  static_cast<unsigned>(beforeSelectorRead);
                    check(preceding == expected &&
                              (savedTerminalValue && !savedSecondTerminalValue
                                   ? call.getResult().hasOneUse()
                                   : call.getResult().use_empty()) &&
                              method->isBeforeInBlock(call) &&
                              llvm::isa<mlir::scf::IfOp>(call->getParentOp()),
                          "saved attribute read retains its exact selector/write order and guard");
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
                    for (ctjs::InvokeOp invoke : call->getBlock()->getOps<ctjs::InvokeOp>()) {
                        ordered |= method->getBlock() == invoke->getBlock() &&
                                   invoke->isBeforeInBlock(method) && method->isBeforeInBlock(call);
                    }
                    check(trailingReads && ordered &&
                              llvm::isa<mlir::scf::IfOp>(call->getParentOp()),
                          "unused trailing read stays after the protected write at its guard");
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
                auto invoke = llvm::dyn_cast_or_null<ctjs::InvokeOp>(next);
                auto write = invoke ? llvm::dyn_cast<ctjs::CallOp>(invoke.getBody().front().front())
                                    : ctjs::CallOp{};
                auto writeMethod = write ? write.getCallee().getDefiningOp<ctjs::GetPropertyOp>()
                                         : ctjs::GetPropertyOp{};
                check(write && write.getArgs()[1] == call.getResult() && writeMethod &&
                          writeMethod->getBlock() == method->getBlock() &&
                          writeMethod->isBeforeInBlock(method) &&
                          llvm::isa<mlir::scf::IfOp>(call->getParentOp()),
                      "member lookup, read and protected write retain source order and guard");
            });
        }
        check(invocations == 1u + static_cast<unsigned>(secondWrites) +
                                 static_cast<unsigned>(selectorReads && !selectedWrites) +
                                 static_cast<unsigned>(finalWrites) + suffixWrites +
                                 static_cast<unsigned>(terminalMatches) +
                                 static_cast<unsigned>(alternatingReads) +
                                 static_cast<unsigned>(lateWrites) -
                                 static_cast<unsigned>(savedSelectorValue) &&
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
                          (!selector || selectedWrites || savedSelectorValue),
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
                                      alternatingReadBeforeFirstSelector}) {
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
         {replaced(readBeforeFirstSelector, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %afterTerminalPresent"),
          replaced(readBeforeFirstSelector, "%answer = ctjs.create_object",
                   "ctjs.store_global \"leaked\", %afterTerminalPresent\n"
                   "    %answer = ctjs.create_object"),
          replaced(readBeforeFirstSelector, "%answer = ctjs.create_object",
                   "%again = ctjs.call %afterTerminalMethod(%element, %afterTerminalName)\n"
                   "    %answer = ctjs.create_object"),
          replaced(appendWrite(readBeforeFirstSelector, finalWrite, "reuse"),
                   "ctjs.call %reuseMethod(%element, %reuseName, %reuseValue)",
                   "ctjs.call %reuseMethod(%element, %reuseName, %afterTerminalPresent)"),
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
          replaced(appendWrite(readBeforeSelector, finalWrite, "reuse"),
                   "ctjs.call %reuseMethod(%element, %reuseName, %reuseValue)",
                   "ctjs.call %reuseMethod(%element, %reuseName, %afterTerminalPresent)"),
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
          replaced(appendWrite(terminalSelectorValue, finalWrite, "reuse"),
                   "ctjs.call %reuseMethod(%element, %reuseName, %reuseValue)",
                   "ctjs.call %reuseMethod(%element, %reuseName, %terminalPresent)"),
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
          replaced(appendWrite(earlierTerminalValue, finalWrite, "reuse"),
                   "ctjs.call %reuseMethod(%element, %reuseName, %reuseValue)",
                   "ctjs.call %reuseMethod(%element, %reuseName, %afterTerminalPresent)"),
          replaced(earlierTerminalValue,
                   "ctjs.call %lateMethod(%element, %lateName, %afterTerminalPresent)",
                   "ctjs.call %lateMethod(%element, %afterTerminalPresent, %afterTerminalPresent)"),
          replaced(earlierTerminalValue,
                   "ctjs.call %lateMethod(%element, %lateName, %afterTerminalPresent)",
                   "ctjs.call %lateMethod(%text, %lateName, %afterTerminalPresent)"),
          replaced(earlierTerminalValue,
                   "ctjs.call %afterTerminalMethod(%element, %afterTerminalName)",
                   "ctjs.call %afterTerminalMethod(%element, %afterTerminalName, %name)"),
          replaced(terminalReadValue, "ctjs.call %lateMethod(%element, %lateName, %latePresent)",
                   "ctjs.call %lateMethod(%element, %lateName, %secondTerminalPresent)"),
          replaced(terminalReadValue, "%answer = ctjs.create_object",
                   "%answer = ctjs.create_object\n"
                   "    ctjs.set_property %answer[%name], %latePresent"),
          replaced(terminalReadValue, "%answer = ctjs.create_object",
                   "%again = ctjs.call %lateReadMethod(%element, %lateReadName)\n"
                   "    %answer = ctjs.create_object"),
          replaced(terminalReadValue, "#ctjs.string<\"hasAttribute\">\n    %lateReadName",
                   "#ctjs.string<\"matches\">\n    %lateReadName"),
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
          replaced(replaced(finalRead,
                            "%finalReadKey = ctjs.constant #ctjs.string<\"hasAttribute\">",
                            "%finalReadKey = ctjs.constant #ctjs.string<\"matches\">"),
                   "%finalReadName = ctjs.constant #ctjs.string<\"data-closed\">",
                   "%finalReadName = ctjs.constant #ctjs.string<\"[data-closed]\">"),
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
          replaced(fourthRead, "ctjs.call %fourthMethod(%element, %fourthName, %fourthPresent)",
                   "ctjs.call %fourthMethod(%element, %fourthName, %finalPresent)"),
          replaced(
              fourthRead, "%answer = ctjs.create_object",
              "ctjs.store_global \"leaked\", %fourthPresent\n    %answer = ctjs.create_object"),
          replaced(
              fourthRead, "%answer = ctjs.create_object",
              "%answer = ctjs.create_object\n    ctjs.set_property %answer[%name], %fourthEffect"),
          replaced(fifthRead, "ctjs.call %fifthMethod(%element, %fifthName, %fifthPresent)",
                   "ctjs.call %fifthMethod(%element, %fifthName, %fourthPresent)"),
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
