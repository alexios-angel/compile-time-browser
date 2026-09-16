#pragma once

#include "HostContractFixtures.h"

namespace ctcompile::test::host_contract {

inline void checkDOMDatasetFilter(mlir::MLIRContext & context) {
    using namespace ctcompile::ctnative;
    const std::string source = R"MLIR(
module {
  ctjs.func @filter$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %element: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %Object = ctjs.load_global "Object"
    %keysName = ctjs.constant #ctjs.string<"keys">
    %keysMethod = ctjs.get_property %Object[%keysName]
    %datasetName = ctjs.constant #ctjs.string<"dataset">
    %dataset = ctjs.get_property %element[%datasetName]
    %keys = ctjs.call %keysMethod(%Object, %dataset)
    %filterName = ctjs.constant #ctjs.string<"filter">
    %filter = ctjs.get_property %keys[%filterName]
    %callback = ctjs.create_closure %callee[1] this %u
    %answer = ctjs.call %filter(%keys, %callback)
    ctjs.return %answer
  }
  ctjs.func private @predicate$1(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %key: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %startsName = ctjs.constant #ctjs.string<"startsWith">
    %starts = ctjs.get_property %key[%startsName]
    %prefix = ctjs.constant #ctjs.string<"bs">
    %first = ctjs.call %starts(%key, %prefix)
    %condition = ctjs.truthy %first
    %selected = scf.if %condition -> (!ctjs.value) {
      %again = ctjs.get_property %key[%startsName]
      %excluded = ctjs.constant #ctjs.string<"bsConfig">
      %second = ctjs.call %again(%key, %excluded)
      %not = ctjs.unary not %second
      scf.yield %not : !ctjs.value
    } else {
      scf.yield %first : !ctjs.value
    }
    ctjs.return %selected
  }
}
)MLIR";
    auto module = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
    check(static_cast<bool>(module), "original dataset filter proof fixture parses");
    if (!module) { return; }
    HostContract contract;
    contract.entry = "filter$0";
    contract.elementParameters = {0};
    contract.datasetParameters = {0};
    contract.initialIntrinsics = {"Object", "Array", "String"};
    contract.moduleSha256 = hostContractFingerprint(*module);
    const auto noEvidence = [](mlir::ModuleOp input, const DOMEntryAnalysis & proof) {
        bool empty = !proof.proved() && !proof.entry() && !proof.wrapper() &&
                     proof.parameters().empty() && proof.callbacks().empty() &&
                     proof.stringResults().empty() && proof.optionalStringJoins().empty() &&
                     proof.stringRefinements().empty();
        input.walk([&](ctjs::FuncOp function) {
            for (mlir::BlockArgument argument : function.getBody().front().getArguments()) {
                empty &= !proof.isCallbackParameter(argument) && !proof.isElement(argument) &&
                         !proof.isDatasetElement(argument);
            }
        });
        input.walk([&](ctjs::CreateClosureOp closure) { empty &= !proof.callback(closure); });
        input.walk([&](ctjs::LoadGlobalOp load) { empty &= !proof.isInitialIntrinsic(load); });
        input.walk([&](ctjs::CallOp call) { empty &= !proof.call(call); });
        input.walk([&](ctjs::GetPropertyOp read) {
            empty &= !proof.method(read) && !proof.isDataset(read.getResult()) &&
                     !proof.isTokenList(read.getResult()) && !proof.isStringVectorLength(read) &&
                     !proof.isStringVectorIndex(read);
        });
        return empty;
    };
    auto callback = module->lookupSymbol<ctjs::FuncOp>("predicate$1");
    const auto key = callback.getBody().front().getArgument(ctjs::implicit_arguments);
    ctjs::CreateClosureOp closure;
    ctjs::CallOp filter;
    module->walk([&](ctjs::CreateClosureOp op) { closure = op; });
    module->walk([&](ctjs::CallOp op) {
        if (op.getArgs().size() == 1 && op.getArgs()[0] == closure.getResult()) { filter = op; }
    });
    for (auto provider :
         {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
        contract.provider = provider;
        DOMEntryAnalysis proof(*module, contract);
        check(proof.proved(), "original pure filter callback proves under both DOM providers");
        if (!proof.proved()) {
            std::fprintf(stderr, "%s\n", proof.reason().str().c_str());
            continue;
        }
        const auto * edge = proof.call(filter);
        check(proof.callbacks().size() == 1 && proof.callbacks().front() == callback &&
                  proof.callback(closure) == callback && proof.isCallbackParameter(key) &&
                  !proof.isElement(key) && edge && edge->kind == HostDOMMethod::filterStrings &&
                  edge->callback == callback && edge->element == filter.getReceiver() &&
                  edge->returnsStringVector() && !edge->returnsBoolean(),
              "filter evidence preserves callback identity and owning vector result");
        unsigned prefixes = 0, snapshots = 0;
        module->walk([&](ctjs::CallOp call) {
            const auto * callEdge = proof.call(call);
            check(callEdge != nullptr, "every original callback and entry call has evidence");
            if (!callEdge) { return; }
            if (callEdge->kind == HostDOMMethod::startsWith) {
                ++prefixes;
                check(callEdge->returnsBoolean() && callEdge->element == key &&
                          proof.method(call.getCallee().getDefiningOp<ctjs::GetPropertyOp>()) ==
                              HostDOMMethod::startsWith,
                      "both short-circuit prefix calls preserve the exact String receiver");
            }
            snapshots += callEdge->kind == HostDOMMethod::datasetKeys;
        });
        check(prefixes == 2 && snapshots == 1,
              "original filter proof includes both callback arms and the dataset snapshot");
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "filter analysis preserves the original complete source");
        check(DOMEntryAnalysis(*module, contract, proof.steps()).proved(),
              "filter proof reproduces its exact completion budget");
        for (unsigned budget = 0; budget < proof.steps(); ++budget) {
            DOMEntryAnalysis limited(*module, contract, budget);
            check(limited.exhausted() && noEvidence(*module, limited),
                  "every incomplete filter proof withholds callback and entry evidence");
        }
    }
    const auto lengthSource = replaced(source, "ctjs.return %answer", R"MLIR(
    %lengthName = ctjs.constant #ctjs.string<"length">
    %length = ctjs.get_property %answer[%lengthName]
    ctjs.return %length
)MLIR");
    auto lengthInput = mlir::parseSourceString<mlir::ModuleOp>(lengthSource, &context);
    check(static_cast<bool>(lengthInput), "filtered dataset snapshot length fixture parses");
    if (lengthInput) {
        ctjs::GetPropertyOp length;
        lengthInput->walk([&](ctjs::GetPropertyOp read) {
            if (ctjs::constantKey(read.getKey()) == "length") { length = read; }
        });
        for (auto provider :
             {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
            auto request = contract;
            request.provider = provider;
            request.moduleSha256 = hostContractFingerprint(*lengthInput);
            DOMEntryAnalysis proof(*lengthInput, request);
            check(proof.proved() && proof.isStringVectorLength(length),
                  "length observes the exact owning filtered String vector");
            if (!proof.proved()) {
                std::fprintf(stderr, "%s\n", proof.reason().str().c_str());
                continue;
            }
            lengthInput->walk([&](ctjs::GetPropertyOp read) {
                check(proof.isStringVectorLength(read) == (read == length),
                      "snapshot length evidence does not authorize any other property read");
            });
            check(hostContractFingerprint(*lengthInput) == request.moduleSha256,
                  "snapshot length analysis preserves the complete original filter");
            check(DOMEntryAnalysis(*lengthInput, request, proof.steps()).proved(),
                  "snapshot length proof reproduces its exact completion budget");
            for (unsigned budget = 0; budget < proof.steps(); ++budget) {
                DOMEntryAnalysis limited(*lengthInput, request, budget);
                check(limited.exhausted() && noEvidence(*lengthInput, limited),
                      "every incomplete snapshot length proof withholds all evidence");
            }
        }
        for (const auto & mutation :
             {replaced(lengthSource, "%answer[%lengthName]", "%element[%lengthName]"),
              replaced(lengthSource, "#ctjs.string<\"length\">", "#ctjs.string<\"size\">"),
              replaced(lengthSource, "ctjs.return %length",
                       "ctjs.set_property %answer[%lengthName], %u\n    ctjs.return %length")}) {
            auto input = mlir::parseSourceString<mlir::ModuleOp>(mutation, &context);
            check(static_cast<bool>(input), "snapshot length refusal fixture parses");
            if (!input) { continue; }
            auto request = contract;
            request.moduleSha256 = hostContractFingerprint(*input);
            check(noEvidence(*input, DOMEntryAnalysis(*input, request)),
                  "wrong receiver/key and snapshot writes withhold all length evidence");
        }
    }
    for (const auto & names :
         {std::vector<std::string>{"Object"}, std::vector<std::string>{"Object", "Array"},
          std::vector<std::string>{"Object", "String"}, std::vector<std::string>{"Array", "String"},
          std::vector<std::string>{"Object", "Array", "String", "Array"}}) {
        auto request = contract;
        request.initialIntrinsics = names;
        check(noEvidence(*module, DOMEntryAnalysis(*module, request)),
              "filter authority requires unique Object, Array and String premises");
        if (lengthInput) {
            request.moduleSha256 = hostContractFingerprint(*lengthInput);
            check(noEvidence(*lengthInput, DOMEntryAnalysis(*lengthInput, request)),
                  "snapshot length cannot bypass original filter identity premises");
        }
    }
    const auto refused = [&](llvm::StringRef from, llvm::StringRef to) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(replaced(source, from, to), &context);
        check(static_cast<bool>(input), "filter source mutation fixture parses");
        if (!input) { return; }
        auto request = contract;
        request.moduleSha256 = hostContractFingerprint(*input);
        DOMEntryAnalysis proof(*input, request);
        const bool empty = noEvidence(*input, proof);
        check(empty, "unsupported filter source withholds every callback and entry capability");
        if (!empty) {
            std::fprintf(stderr, "%s => %s: %s\n", from.str().c_str(), to.str().c_str(),
                         proof.reason().str().c_str());
        }
        check(hostContractFingerprint(*input) == request.moduleSha256,
              "refused filter proof preserves its complete source");
    };
    for (llvm::StringRef call :
         {"%filter(%element, %callback)", "%filter(%keys, %callback, %u)", "%filter(%keys, %u)"}) {
        refused("%filter(%keys, %callback)", call);
    }
    for (llvm::StringRef call : {"%starts(%prefix, %prefix)", "%starts(%key)",
                                 "%starts(%key, %prefix, %prefix)", "%starts(%key, %key)"}) {
        refused("%starts(%key, %prefix)", call);
    }
    refused("ctjs.return %answer", "ctjs.return %callback");
    refused("ctjs.return %selected", "ctjs.return %key");
    refused("ctjs.return %selected",
            "ctjs.store_global \"saved\", %key\n    ctjs.return %selected");
    refused("%answer =", "ctjs.store_global \"saved\", %callback\n    %answer =");
    refused("%callback =", "ctjs.store_global \"Array\", %element\n    %callback =");
    refused("%callback =", "ctjs.store_global \"String\", %element\n    %callback =");
    refused("%callback =", "ctjs.set_property %keys[%filterName], %element\n    %callback =");
    refused("#ctjs.string<\"bs\">", "#ctjs.string<\"é\">");
    refused("%callee[1] this %u", "%callee[2] this %u");
    refused("%callee[1] this %u", "%element[1] this %u");
    refused("%callee[1] this %u", "%callee[1] this %element");
    refused("%callee[1] this %u", "%callee[1] this %u captures %element");

    auto negativeIndex = mlir::parseSourceString<mlir::ModuleOp>(
        replaced(replaced(source, "predicate$1", "predicate$4294967295"), "%callee[1]",
                 "%callee[-1]"),
        &context);
    check(static_cast<bool>(negativeIndex), "negative callback index fixture parses");
    if (negativeIndex) {
        auto request = contract;
        request.moduleSha256 = hostContractFingerprint(*negativeIndex);
        check(noEvidence(*negativeIndex, DOMEntryAnalysis(*negativeIndex, request)),
              "a negative callback index cannot alias an unsigned function identity");
    }

    const std::string prefix(32, 'b');
    auto longInput = mlir::parseSourceString<mlir::ModuleOp>(
        replaced(source, "#ctjs.string<\"bs\">", "#ctjs.string<\"" + prefix + "\">"), &context);
    check(static_cast<bool>(longInput), "long ASCII prefix fixture parses");
    if (longInput) {
        auto request = contract;
        request.moduleSha256 = hostContractFingerprint(*longInput);
        DOMEntryAnalysis proof(*longInput, request);
        check(proof.proved() && DOMEntryAnalysis(*longInput, request, proof.steps()).proved(),
              "long ASCII prefix reproduces its exact charged proof budget");
        for (unsigned budget = 0; proof.proved() && budget < proof.steps(); ++budget) {
            DOMEntryAnalysis limited(*longInput, request, budget);
            check(limited.exhausted() && noEvidence(*longInput, limited),
                  "prefix scan budget cutoffs publish no callback evidence");
        }
        longInput->walk([&](ctjs::ConstantOp constant) {
            if (ctjs::constantKey(constant.getResult()) == prefix) {
                constant.setValueAttr(ctjs::StringAttr::get(&context, prefix + "b"));
            }
        });
        request.moduleSha256 = hostContractFingerprint(*longInput);
        DOMEntryAnalysis extra(*longInput, request, proof.steps());
        check(extra.exhausted() && noEvidence(*longInput, extra),
              "one more ASCII prefix character needs one more proof step");
    }

    mlir::Builder builder(&context);
    (*module)->setAttr("ctnative.host_proved", builder.getBoolAttr(true));
    callback->setAttr("ctnative.host_callback", builder.getBoolAttr(true));
    closure->setAttr("ctnative.host_callback", builder.getStringAttr("predicate$1"));
    filter->setAttr("ctnative.host_method", builder.getStringAttr("filterStrings"));
    check(DOMEntryAnalysis(*module, contract).proved(),
          "printed filter reports do not replace original source discovery");
    filter->setOperand(1, closure.getResult());
    DOMEntryAnalysis stale(*module, contract);
    check(noEvidence(*module, stale) && stale.reason().contains("fingerprint"),
          "receiver mutation invalidates the original callback fingerprint");
    contract.moduleSha256 = hostContractFingerprint(*module);
    check(noEvidence(*module, DOMEntryAnalysis(*module, contract)),
          "fresh fingerprint and spoofed callback reports cannot forge a filter receiver");
}

inline void checkDOMDataset(mlir::MLIRContext & context) {
    using namespace ctcompile::ctnative;
    constexpr llvm::StringLiteral source = R"MLIR(
module {
  ctjs.func @keys$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %element: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %Object = ctjs.load_global "Object"
    %keysName = ctjs.constant #ctjs.string<"keys">
    %keys = ctjs.get_property %Object[%keysName]
    %datasetName = ctjs.constant #ctjs.string<"dataset">
    %dataset = ctjs.get_property %element[%datasetName]
    %result = ctjs.call %keys(%Object, %dataset)
    ctjs.return %result
  }
}
)MLIR";
    auto module = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
    check(static_cast<bool>(module), "dataset snapshot fixture parses");
    if (!module) { return; }
    HostContract contract;
    contract.provider = HostContract::Provider::ctbrowserDOM;
    contract.entry = "keys$0";
    contract.elementParameters = {0};
    contract.datasetParameters = {0};
    contract.initialIntrinsics = {"Object"};
    contract.moduleSha256 = hostContractFingerprint(*module);
    ctjs::LoadGlobalOp intrinsic;
    ctjs::GetPropertyOp dataset, method;
    ctjs::CallOp call;
    module->walk([&](ctjs::LoadGlobalOp op) { intrinsic = op; });
    module->walk([&](ctjs::GetPropertyOp op) {
        if (ctjs::constantKey(op.getKey()) == "keys") {
            method = op;
        } else {
            dataset = op;
        }
    });
    module->walk([&](ctjs::CallOp op) { call = op; });
    const auto noEvidence = [&](const DOMEntryAnalysis & proof) {
        return !proof.proved() && !proof.entry() && proof.parameters().empty() &&
               !proof.isInitialIntrinsic(intrinsic) && !proof.isDataset(dataset.getResult()) &&
               !proof.isDatasetElement(dataset.getObject()) && !proof.method(method) &&
               !proof.call(call);
    };
    for (auto provider :
         {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
        contract.provider = provider;
        DOMEntryAnalysis proof(*module, contract);
        const auto * edge = proof.call(call);
        check(proof.proved() && proof.isInitialIntrinsic(intrinsic) &&
                  proof.isDataset(dataset.getResult()) &&
                  proof.isDatasetElement(dataset.getObject()) && edge &&
                  edge->kind == HostDOMMethod::datasetKeys && edge->returnsStringVector() &&
                  !edge->returnsBoolean() && edge->element == dataset.getObject(),
              "dataset snapshot requires exact Object identity and the contracted element");
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "dataset proof preserves the entire source");
        if (proof.proved()) {
            check(DOMEntryAnalysis(*module, contract, proof.steps()).proved(),
                  "dataset proof reproduces its exact budget");
            for (unsigned budget = 0; budget < proof.steps(); ++budget) {
                DOMEntryAnalysis limited(*module, contract, budget);
                check(noEvidence(limited) && limited.exhausted(),
                      "every insufficient dataset budget withholds all live evidence");
            }
        }
    }
    for (auto indices :
         {std::vector<unsigned>{}, std::vector<unsigned>{1}, std::vector<unsigned>{0, 0}}) {
        auto invalid = contract;
        invalid.datasetParameters = indices;
        check(noEvidence(DOMEntryAnalysis(*module, invalid)),
              "typed dataset premises cannot bypass namespace subset validation");
    }
    contract.initialIntrinsics.clear();
    check(noEvidence(DOMEntryAnalysis(*module, contract)),
          "an Object global name cannot supply intrinsic authority");
    contract.initialIntrinsics = {"Object"};
    mlir::Builder builder(&context);
    method.getKey().getDefiningOp<ctjs::ConstantOp>().setValueAttr(
        ctjs::StringAttr::get(&context, "values"));
    check(noEvidence(DOMEntryAnalysis(*module, contract)),
          "changed dataset source invalidates the fingerprint");
    contract.moduleSha256 = hostContractFingerprint(*module);
    method->setAttr("ctnative.host_intrinsic", builder.getStringAttr("Object.keys"));
    check(noEvidence(DOMEntryAnalysis(*module, contract)),
          "forged Object.keys reports cannot replace the complete proof");
    checkDOMDatasetFilter(context);
}

inline void checkDOMStringPrefix(mlir::MLIRContext & context) {
    using namespace ctcompile::ctnative;
    const std::string source = R"MLIR(
module {
  ctjs.func @prefix$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %element: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %text = ctjs.constant #ctjs.string<"bsTitle">
    %methodName = ctjs.constant #ctjs.string<"replace">
    %method = ctjs.get_property %text[%methodName]
    %factory = ctjs.load_global "__ctbrowser_regexp"
    %pattern = ctjs.constant #ctjs.string<"^bs">
    %flags = ctjs.constant #ctjs.string<"">
    %replacement = ctjs.constant #ctjs.string<"">
    %regex = ctjs.call %factory(%u, %pattern, %flags)
    %answer = ctjs.call %method(%text, %regex, %replacement)
    ctjs.return %answer
  }
}
)MLIR";
    HostContract contract;
    contract.entry = "prefix$0";
    contract.elementParameters = {0};
    contract.initialIntrinsics = {"String", "RegExp", "__ctbrowser_regexp"};
    const auto noEvidence = [](mlir::ModuleOp input, const DOMEntryAnalysis & proof) {
        bool empty = !proof.proved() && !proof.entry() && proof.parameters().empty();
        input.walk([&](ctjs::CallOp call) {
            empty &= !proof.call(call) && !proof.isStringPrefixRegExp(call);
        });
        input.walk([&](ctjs::LoadGlobalOp load) { empty &= !proof.isInitialIntrinsic(load); });
        input.walk([&](ctjs::GetPropertyOp read) { empty &= !proof.method(read); });
        return empty;
    };
    auto input = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
    check(static_cast<bool>(input), "anchored prefix source fixture parses");
    if (!input) { return; }
    contract.moduleSha256 = hostContractFingerprint(*input);
    for (auto provider :
         {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
        contract.provider = provider;
        DOMEntryAnalysis proof(*input, contract);
        check(proof.proved(), "exact confined /^bs/ replacement proves with original identities");
        if (!proof.proved()) {
            std::fprintf(stderr, "%s\n", proof.reason().str().c_str());
            continue;
        }
        unsigned literals = 0, replacements = 0;
        input->walk([&](ctjs::CallOp call) {
            if (proof.isStringPrefixRegExp(call)) {
                ++literals;
                check(!proof.call(call), "literal bookkeeping has no runtime call edge");
            } else if (const auto * edge = proof.call(call)) {
                ++replacements;
                check(edge->kind == HostDOMMethod::removeStringPrefix && edge->returnsString() &&
                          !edge->returnsBoolean() && edge->element == call.getReceiver(),
                      "prefix removal retains its original String receiver and owning result");
            }
        });
        check(literals == 1 && replacements == 1 &&
                  hostContractFingerprint(*input) == contract.moduleSha256,
              "prefix proof covers both calls without changing source");
        check(DOMEntryAnalysis(*input, contract, proof.steps()).proved(),
              "prefix proof reproduces its exact work budget");
        for (unsigned budget = 0; budget < proof.steps(); ++budget) {
            DOMEntryAnalysis limited(*input, contract, budget);
            check(limited.exhausted() && noEvidence(*input, limited),
                  "every incomplete prefix proof withholds literal and replacement evidence");
        }
        for (const std::string & missing : contract.initialIntrinsics) {
            auto request = contract;
            std::erase(request.initialIntrinsics, missing);
            check(noEvidence(*input, DOMEntryAnalysis(*input, request)),
                  "prefix proof requires each original String/RegExp/factory identity");
        }
    }
    for (const auto & changed : {
             replaced(source, "#ctjs.string<\"^bs\">", "#ctjs.string<\"bs\">"),
             replaced(source, "#ctjs.string<\"^bs\">", "#ctjs.string<\"^b.\">"),
             replaced(source, "%flags = ctjs.constant #ctjs.string<\"\">",
                      "%flags = ctjs.constant #ctjs.string<\"g\">"),
             replaced(source, "%flags = ctjs.constant #ctjs.string<\"\">",
                      "%flags = ctjs.constant #ctjs.number<0>"),
             replaced(source, "%replacement = ctjs.constant #ctjs.string<\"\">",
                      "%replacement = ctjs.constant #ctjs.string<\"x\">"),
             replaced(source, "%replacement = ctjs.constant #ctjs.string<\"\">",
                      "%replacement = ctjs.constant #ctjs.null"),
             replaced(source, "%u, %pattern, %flags", "%element, %pattern, %flags"),
             replaced(source, "%u, %pattern, %flags", "%u, %text, %flags"),
             replaced(source, "%u, %pattern, %flags", "%u, %pattern, %text"),
             replaced(source, "%text, %regex, %replacement", "%element, %regex, %replacement"),
             replaced(source, "%text, %regex, %replacement", "%text, %regex, %text"),
             replaced(source, "%text, %regex, %replacement", "%text, %regex, %replacement, %u"),
             replaced(source, "ctjs.return %answer", "ctjs.return %regex"),
             replaced(source, "ctjs.return %answer",
                      "%twice = ctjs.call %method(%text, %regex, %replacement)\n    ctjs.return "
                      "%answer"),
             replaced(source, "ctjs.return %answer",
                      "ctjs.store_global \"saved\", %regex\n    ctjs.return %answer"),
             replaced(source, "%regex = ctjs.call",
                      "ctjs.set_property %factory[%methodName], %u\n    %regex = ctjs.call"),
         }) {
        auto negative = mlir::parseSourceString<mlir::ModuleOp>(changed, &context);
        check(static_cast<bool>(negative), "prefix refusal fixture parses");
        if (!negative) { continue; }
        auto request = contract;
        request.moduleSha256 = hostContractFingerprint(*negative);
        check(noEvidence(*negative, DOMEntryAnalysis(*negative, request)),
              "nonliteral, stateful, escaped, mutated and wrong-receiver prefix operations refuse");
    }
}

} // namespace ctcompile::test::host_contract
