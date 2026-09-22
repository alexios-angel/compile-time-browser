#pragma once

#include "HostContractFixtures.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Verifier.h"

namespace ctcompile::test::host_contract {

inline void checkDOMNestedHelpers(mlir::MLIRContext & context) {
    using namespace ctcompile::ctnative;
    const std::string source = R"MLIR(
module {
  ctjs.func @entry$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %element: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %text = ctjs.constant #ctjs.string<"%7B%7D">
    %flag = ctjs.constant #ctjs.boolean<false>
    %condition = ctjs.truthy %flag
    %helper = ctjs.create_closure %callee[1] this %u
    %answer = scf.if %condition -> (!ctjs.value) {
      %parsed = ctjs.call %helper(%u, %text)
      scf.yield %parsed : !ctjs.value
    } else {
      scf.yield %text : !ctjs.value
    }
    ctjs.return %answer
  }
  ctjs.func private @parse$1(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %text: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %json = ctjs.load_global "JSON"
    %key = ctjs.constant #ctjs.string<"parse">
    %parse = ctjs.get_property %json[%key]
    %decode = ctjs.load_global "decodeURIComponent"
    %answer = ctjs.invoke {
      %decoded = ctjs.call %decode(%u, %text)
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
    const auto withCapture = [](std::string input) {
        input = replaced(input, "%helper = ctjs.create_closure %callee[1] this %u",
                         "%cell = ctjs.create_cell %u\n"
                         "    %helper = ctjs.create_closure %callee[1] this %u captures %cell\n"
                         "    ctjs.cell_set %cell, %text");
        input = replaced(input, "%text: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0",
                         "%unused: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 1");
        return replaced(input,
                        "    %json =", "    %text = ctjs.load_upvalue %callee[0]\n    %json =");
    };
    const auto captured = withCapture(source);
    auto loop = replaced(source, "%answer = scf.if", R"MLIR(
    %answer = scf.while (%state = %text) : (!ctjs.value) -> !ctjs.value {
      %selected = scf.if
)MLIR");
    loop = replaced(loop, "    ctjs.return %answer", R"MLIR(
      scf.condition(%condition) %selected : !ctjs.value
    } do {
    ^bb0(%state: !ctjs.value):
      scf.yield %state : !ctjs.value
    }
    ctjs.return %answer
)MLIR");
    auto loopAfter = replaced(source, "%answer = scf.if", R"MLIR(
    %answer = scf.while (%state = %text) : (!ctjs.value) -> !ctjs.value {
      scf.condition(%condition) %state : !ctjs.value
    } do {
    ^bb0(%state: !ctjs.value):
      %selected = scf.if
)MLIR");
    loopAfter = replaced(loopAfter, "    ctjs.return %answer", R"MLIR(
      scf.yield %selected : !ctjs.value
    }
    ctjs.return %answer
)MLIR");
    const auto capturedLoop = withCapture(loopAfter);
    for (const auto & valid : {source, captured, loop, loopAfter, capturedLoop,
                               replaced(source, "ctjs.call %helper(%u, %text)",
                                        "ctjs.call_direct @parse$1(%u, %u, %helper, %text)")}) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(valid, &context);
        check(static_cast<bool>(input), "nested helper source fixture parses");
        if (!input) { continue; }
        auto error = expandDOMHelpers(*input, "entry$0", 100000);
        check(!error, "an enclosing helper and its initialized capture precede the nested call");
        if (error) {
            std::fprintf(stderr, "%s\n", llvm::toString(std::move(error)).c_str());
            continue;
        }
        check(mlir::succeeded(mlir::verify(*input)) &&
                  !input->lookupSymbol<ctjs::FuncOp>("parse$1"),
              "nested expansion leaves valid IR and retires only the expanded helper");
        unsigned calls = 0, invocations = 0, closures = 0;
        input->walk([&](ctjs::CreateClosureOp) { ++closures; });
        input->walk([&](ctjs::CallOp) { ++calls; });
        input->walk([&](ctjs::InvokeOp invoke) {
            ++invocations;
            check(static_cast<bool>(invoke->getParentOfType<mlir::scf::IfOp>()),
                  "helper effects remain inside the original selected arm");
            auto fallback =
                llvm::cast<ctjs::InvokeYieldOp>(invoke.getUnwindBody().front().getTerminator());
            auto constant = fallback.getValues().front().getDefiningOp<ctjs::ConstantOp>();
            check(constant && ctjs::constantKey(constant.getResult()) == "%7B%7D",
                  "both URI and JSON failures keep the original saved input");
        });
        check(calls == 2 && invocations == 2 && closures == 0,
              "nested expansion preserves both fallible calls and their continuations");
        if (valid == loop || valid == loopAfter || valid == capturedLoop) { continue; }
        HostContract contract;
        contract.entry = "entry$0";
        contract.elementParameters = {0};
        contract.initialIntrinsics = {"JSON", "decodeURIComponent"};
        contract.moduleSha256 = hostContractFingerprint(*input);
        for (auto provider :
             {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
            contract.provider = provider;
            DOMEntryAnalysis proof(*input, contract);
            check(proof.proved(), "nested helper invokes receive complete DOM reproof");
            if (!proof.proved()) { std::fprintf(stderr, "%s\n", proof.reason().str().c_str()); }
        }
    }
    auto varying = replaced(source, "      scf.yield %text : !ctjs.value",
                            "      %other = ctjs.call %helper(%u, %element)\n"
                            "      scf.yield %other : !ctjs.value");
    auto varied = mlir::parseSourceString<mlir::ModuleOp>(varying, &context);
    check(static_cast<bool>(varied), "differing nested arguments fixture parses");
    if (varied) {
        auto error = expandDOMHelpers(*varied, "entry$0", 100000);
        check(!error, "a nonconstant nested input prevents shared argument specialization");
        if (error) {
            llvm::consumeError(std::move(error));
        } else {
            unsigned known = 0, unknown = 0;
            auto argument =
                varied->lookupSymbol<ctjs::FuncOp>("entry$0").getBody().front().getArgument(3);
            varied->walk([&](ctjs::InvokeOp invoke) {
                auto fallback =
                    llvm::cast<ctjs::InvokeYieldOp>(invoke.getUnwindBody().front().getTerminator());
                auto value = fallback.getValues().front();
                known += ctjs::constantKey(value) == "%7B%7D";
                unknown += value == argument;
            });
            check(known == 2 && unknown == 2 && mlir::succeeded(mlir::verify(*varied)),
                  "each nested call retains its own saved input through both exception paths");
        }
    }
    for (const auto & omitted :
         {replaced(source, "ctjs.call %helper(%u, %text)", "ctjs.call %helper(%u)"),
          replaced(varying, "ctjs.call %helper(%u, %element)", "ctjs.call %helper(%u)")}) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(omitted, &context);
        check(static_cast<bool>(input), "omitted helper argument fixture parses");
        if (!input) { continue; }
        auto error = expandDOMHelpers(*input, "entry$0", 100000);
        check(!error, "omitted helper arguments expand without specializing other calls");
        if (error) {
            llvm::consumeError(std::move(error));
            continue;
        }
        unsigned missing = 0;
        input->walk([&](ctjs::InvokeOp invoke) {
            auto fallback =
                llvm::cast<ctjs::InvokeYieldOp>(invoke.getUnwindBody().front().getTerminator());
            auto constant = fallback.getValues().front().getDefiningOp<ctjs::ConstantOp>();
            missing += constant && llvm::isa<ctjs::UndefinedAttr>(constant.getValue());
        });
        check(missing == 2 && mlir::succeeded(mlir::verify(*input)),
              "both exception continuations retain the omitted argument as undefined");
    }
    auto lateCapture = replaced(captured, "    ctjs.cell_set %cell, %text\n", "");
    lateCapture = replaced(lateCapture, "    ctjs.return %answer",
                           "    ctjs.cell_set %cell, %text\n    ctjs.return %answer");
    auto callInInvoke = replaced(source, "%parsed = ctjs.call %helper(%u, %text)", R"MLIR(
      %parsed = ctjs.invoke {
        %called = ctjs.call %helper(%u, %text)
        ctjs.invoke_exit %called state()
      } normal {
      ^bb0(%returned: !ctjs.value):
        ctjs.invoke_yield(%returned)
      } unwind {
      ^bb0(%error: !ctjs.value):
        ctjs.invoke_yield(%text)
      } : !ctjs.value
)MLIR");
    for (const auto & invalid :
         {lateCapture, callInInvoke,
          replaced(captured, "    %answer = scf.if",
                   "    ctjs.cell_set %cell, %u\n    %answer = scf.if"),
          replaced(source, "ctjs.call %helper(%u, %text)", "ctjs.call %helper(%element, %text)"),
          replaced(source, "ctjs.call %helper(%u, %text)",
                   "ctjs.call %helper(%u, %text, %element)"),
          replaced(source, "    %answer = scf.if",
                   "    ctjs.store_global \"saved\", %helper\n    %answer = scf.if")}) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
        check(static_cast<bool>(input), "unsupported nested helper fixture parses");
        if (!input) { continue; }
        auto error = expandDOMHelpers(*input, "entry$0", 100000);
        check(static_cast<bool>(error),
              "late or mutable captures, receivers, arity, escapes and Invoke crossings refuse");
        if (error) { llvm::consumeError(std::move(error)); }
    }
    bool completed = false;
    for (unsigned budget = 0; budget < 10000; ++budget) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(loop, &context);
        auto error = expandDOMHelpers(*input, "entry$0", budget);
        if (!error) {
            completed = true;
            check(!input->lookupSymbol<ctjs::FuncOp>("parse$1") &&
                      mlir::succeeded(mlir::verify(*input)),
                  "the first complete nested budget produces fully expanded valid IR");
            break;
        }
        check(llvm::toString(std::move(error)).find("budget") != std::string::npos,
              "every incomplete nested ordering and cloning budget fails closed");
    }
    check(completed, "bounded nested helper expansion reaches completion");

    const std::string throwingHelper = R"MLIR(
module {
  ctjs.func @entry$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %flag: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %saved = ctjs.constant #ctjs.string<"saved">
    %helper = ctjs.create_closure %callee[1] this %u
    %result = ctjs.call %helper(%u, %flag, %saved)
    ctjs.return %result
  }
  ctjs.func private @throwing$1(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %flag: !ctjs.value, %payload: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %frame = ctjs.frame_enter 2
    %condition = ctjs.truthy %flag
    %inactive = ctjs.constant #ctjs.undefined
    %result = scf.if %condition -> (!ctjs.value) {
      scf.execute_region {
        ctjs.throw %payload
      } {no_inline}
      scf.yield %inactive : !ctjs.value
    } else {
      ctjs.frame_exit %frame
      scf.yield %payload : !ctjs.value
    }
    ctjs.return %result
  }
}
)MLIR";
    const std::string throwOnly = R"MLIR(      scf.execute_region {
        ctjs.throw %payload
      } {no_inline}
)MLIR";
    const auto throwOnElse = replaced(
        replaced(throwingHelper, throwOnly + "      scf.yield %inactive : !ctjs.value",
                 "      ctjs.frame_exit %frame\n      scf.yield %payload : !ctjs.value"),
        "    } else {\n      ctjs.frame_exit %frame\n      scf.yield %payload : !ctjs.value",
        "    } else {\n" + throwOnly + "      scf.yield %inactive : !ctjs.value");
    const auto nestedThrow = replaced(throwingHelper, throwOnly,
                                      "      scf.if %condition {\n" + throwOnly +
                                          "        scf.yield\n"
                                          "      } else {\n" +
                                          throwOnly + "        scf.yield\n      }\n");
    for (const auto & valid : {throwingHelper, throwOnElse, nestedThrow}) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(valid, &context);
        check(static_cast<bool>(input), "saved helper throw fixture parses");
        if (!input) { continue; }
        auto error = expandDOMHelpers(*input, "entry$0", 100000);
        check(!error, "only the returning sibling supplies the helper shadow frame join");
        if (error) {
            std::fprintf(stderr, "%s\n", llvm::toString(std::move(error)).c_str());
            continue;
        }
        unsigned throws = 0, frames = 0, calls = 0;
        input->walk([&](ctjs::ThrowOp thrown) {
            ++throws;
            check(ctjs::constantKey(thrown.getValue()) == "saved",
                  "inlined throw preserves its invocation's saved payload");
        });
        input->walk([&](ctjs::FrameEnterOp) { ++frames; });
        input->walk([&](ctjs::CallOp) { ++calls; });
        check(throws == (valid == nestedThrow ? 2U : 1U) && frames == 0 && calls == 0 &&
                  !input->lookupSymbol<ctjs::FuncOp>("throwing$1") &&
                  mlir::succeeded(mlir::verify(*input)),
              "helper expansion retains every throw and retires the proved call and frame");
    }
    for (const auto & invalid : {replaced(throwingHelper, " {no_inline}", ""),
                                 replaced(throwingHelper, "        ctjs.throw %payload",
                                          "        ctjs.store_global \"effect\", %payload\n"
                                          "        ctjs.throw %payload"),
                                 replaced(throwingHelper, "      scf.yield %inactive",
                                          "      ctjs.store_global \"effect\", %payload\n"
                                          "      scf.yield %inactive"),
                                 replaced(throwingHelper, "      ctjs.frame_exit %frame\n", ""),
                                 replaced(throwingHelper, throwOnly, ""),
                                 replaced(nestedThrow, "      scf.yield %inactive",
                                          "      ctjs.store_global \"effect\", %payload\n"
                                          "      scf.yield %inactive")}) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
        check(static_cast<bool>(input), "unproved helper abrupt branch fixture parses");
        if (!input) { continue; }
        auto error = expandDOMHelpers(*input, "entry$0", 100000);
        check(static_cast<bool>(error),
              "unproved regions, dead effects and live or mismatched normal frames refuse");
        if (error) { llvm::consumeError(std::move(error)); }
    }
    for (unsigned budget : {0U, 64U, 128U}) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(throwingHelper, &context);
        auto error = expandDOMHelpers(*input, "entry$0", budget);
        check(llvm::toString(std::move(error)).find("budget") != std::string::npos,
              "incomplete saved-throw helper proof cannot publish an expansion");
    }
}

inline void checkDOMDataSource(mlir::MLIRContext & context) {
    using namespace ctcompile::ctnative;
    const std::string source = R"MLIR(
module {
  ctjs.func @entry$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %element: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %zero = ctjs.constant #ctjs.number<0>
    %key = ctjs.constant #ctjs.string<"value">
    %flag = ctjs.constant #ctjs.boolean<true>
    %condition = ctjs.truthy %flag
    %object = ctjs.create_object
    ctjs.set_property %object[%key], %zero
    %answer = scf.if %condition -> (!ctjs.value) {
      ctjs.set_property %object[%key], %flag
      scf.yield %object : !ctjs.value
    } else {
      scf.yield %zero : !ctjs.value
    }
    ctjs.return %answer
  }
}
)MLIR";
    const auto local = replaced(source, "    %object = ctjs.create_object\n", "");
    const auto branch = replaced(
        replaced(local, "    ctjs.set_property %object[%key], %zero\n", ""),
        "      ctjs.set_property", "      %object = ctjs.create_object\n      ctjs.set_property");
    const auto nested = replaced(source, "      ctjs.set_property %object[%key], %flag", R"MLIR(
      %loop = scf.while (%state = %zero) : (!ctjs.value) -> !ctjs.value {
        ctjs.set_property %object[%key], %state
        scf.condition(%condition) %state : !ctjs.value
      } do {
      ^bb0(%state: !ctjs.value):
        ctjs.set_property %object[%key], %flag
        scf.yield %state : !ctjs.value
      }
)MLIR");
    for (const auto & valid :
         {source, branch, nested, replaced(source, "%object[%key]", "%object[%element]"),
          replaced(source, "\"value\"", "\"__proto__\"")}) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(valid, &context);
        check(static_cast<bool>(input), "fresh data-object source fixture parses");
        if (!input) { continue; }
        const auto before = hostContractFingerprint(*input);
        auto prepared = expandDOMHelpers(*input, "entry$0", 100000);
        check(!prepared && before == hostContractFingerprint(*input) &&
                  mlir::succeeded(mlir::verify(*input)),
              "source preparation preserves every ordered data write for complete DOM proof");
        if (prepared) { llvm::consumeError(std::move(prepared)); }
    }
    for (const auto & invalid :
         {replaced(source, "%object[%key], %zero", "%element[%object], %zero"),
          replaced(source, "%object[%key], %zero", "%element[%key], %object"),
          replaced(source,
                   "    %answer =", "    %read = ctjs.get_property %object[%key]\n    %answer ="),
          replaced(source,
                   "    %answer =", "    ctjs.store_global \"saved\", %object\n    %answer =")}) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
        check(static_cast<bool>(input), "unsupported data-object source fixture parses");
        if (!input) { continue; }
        const auto before = hostContractFingerprint(*input);
        auto refused = expandDOMHelpers(*input, "entry$0", 100000);
        check(static_cast<bool>(refused) && before == hostContractFingerprint(*input),
              "data-object keys, escapes and member reads gain no source-use authority");
        if (refused) { llvm::consumeError(std::move(refused)); }
    }
    bool completed = false;
    for (unsigned budget = 0; budget < 10000; ++budget) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(nested, &context);
        const auto before = hostContractFingerprint(*input);
        auto limited = expandDOMHelpers(*input, "entry$0", budget);
        check(before == hostContractFingerprint(*input),
              "every nested data-object preparation budget preserves exact assignment order");
        if (!limited) {
            completed = true;
            break;
        }
        check(llvm::toString(std::move(limited)).find("budget") != std::string::npos,
              "incomplete nested data-object preparation fails closed");
    }
    check(completed, "bounded nested data-object preparation reaches completion");
}

inline void checkDOMBranchFilter(mlir::MLIRContext & context) {
    using namespace ctcompile::ctnative;
    context.getOrLoadDialect<mlir::scf::SCFDialect>();
    checkDOMNestedHelpers(context);
    checkDOMDataSource(context);
    const std::string source = R"MLIR(
module {
  ctjs.func @branch$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %element: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %zero = ctjs.constant #ctjs.number<0>
    %flag = ctjs.constant #ctjs.boolean<false>
    %condition = ctjs.truthy %flag
    %answer = scf.if %condition -> (!ctjs.value) {
      scf.yield %zero : !ctjs.value
    } else {
      %Object = ctjs.load_global "Object"
      %keysName = ctjs.constant #ctjs.string<"keys">
      %keysMethod = ctjs.get_property %Object[%keysName]
      %datasetName = ctjs.constant #ctjs.string<"dataset">
      %dataset = ctjs.get_property %element[%datasetName]
      %keys = ctjs.call %keysMethod(%Object, %dataset)
      %filterName = ctjs.constant #ctjs.string<"filter">
      %filter = ctjs.get_property %keys[%filterName]
      %callback = ctjs.create_closure %callee[1] this %u
      %filtered = ctjs.call %filter(%keys, %callback)
      %lengthName = ctjs.constant #ctjs.string<"length">
      %length = ctjs.get_property %filtered[%lengthName]
      scf.yield %length : !ctjs.value
    }
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
                     !proof.isStringVectorLength(read) && !proof.isStringVectorIndex(read);
        });
        return empty;
    };
    const auto checkPrepared = [&](mlir::ModuleOp input, llvm::StringRef entry) {
        HostContract contract;
        contract.entry = entry.str();
        contract.elementParameters = {0};
        contract.datasetParameters = {0};
        contract.initialIntrinsics = {"Object", "Array", "String"};
        contract.moduleSha256 = hostContractFingerprint(input);
        for (auto provider :
             {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
            contract.provider = provider;
            DOMEntryAnalysis proof(input, contract);
            check(proof.proved(), "branch-local source callback receives complete DOM reproof");
            if (!proof.proved()) {
                std::fprintf(stderr, "%s\n", proof.reason().str().c_str());
                continue;
            }
            unsigned callbacks = 0, calls = 0;
            input.walk([&](ctjs::CreateClosureOp closure) {
                ++callbacks;
                check(closure->getParentOfType<mlir::scf::IfOp>() &&
                          proof.callback(closure) ==
                              input.lookupSymbol<ctjs::FuncOp>("predicate$1"),
                      "retained callback keeps its exact identity inside the selected arm");
            });
            input.walk([&](ctjs::CallOp call) {
                ++calls;
                check(proof.call(call) != nullptr,
                      "every preserved callback and dataset call has independent evidence");
            });
            check(callbacks == 1 && calls == 4 && proof.callbacks().size() == 1,
                  "source expansion preserves the filter and both original predicate calls");
            check(hostContractFingerprint(input) == contract.moduleSha256,
                  "complete branch callback proof does not rewrite its source");
            check(DOMEntryAnalysis(input, contract, proof.steps()).proved(),
                  "branch callback proof reproduces its exact completion budget");
            for (unsigned budget = 0; budget < proof.steps(); ++budget) {
                DOMEntryAnalysis limited(input, contract, budget);
                check(limited.exhausted() && noEvidence(input, limited),
                      "every incomplete branch callback proof withholds all evidence");
            }
            for (const auto & names : {std::vector<std::string>{"Object", "Array"},
                                       std::vector<std::string>{"Object", "String"}}) {
                auto missing = contract;
                missing.initialIntrinsics = names;
                check(noEvidence(input, DOMEntryAnalysis(input, missing)),
                      "branch scheduling does not supply absent Array or String authority");
            }
        }
    };

    auto module = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
    check(static_cast<bool>(module), "branch-local filter scheduling fixture parses");
    if (!module) { return; }
    const auto fingerprint = hostContractFingerprint(*module);
    auto error = expandDOMHelpers(*module, "branch$0", 100000);
    check(!error, "source preparation visits the confined callback inside the continuation");
    if (error) {
        std::fprintf(stderr, "%s\n", llvm::toString(std::move(error)).c_str());
        return;
    }
    check(fingerprint == hostContractFingerprint(*module),
          "scheduling retains the complete source body and both function identities");
    checkPrepared(*module, "branch$0");

    bool completed = false;
    for (unsigned budget = 0; budget < 10000; ++budget) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
        auto limited = expandDOMHelpers(*input, "branch$0", budget);
        if (!limited) {
            completed = true;
            break;
        }
        const auto reason = llvm::toString(std::move(limited));
        check(reason.find("budget") != std::string::npos &&
                  fingerprint == hostContractFingerprint(*input),
              "every incomplete callback scheduling budget preserves the source");
    }
    check(completed, "bounded callback scheduling reaches its complete proof");

    auto helperSource = replaced(source, "ctjs.func @branch$0", "ctjs.func private @helper$2");
    helperSource = replaced(helperSource, "module {", R"MLIR(
module {
  ctjs.func @entry$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %element: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %helper = ctjs.create_closure %callee[2] this %u
    %answer = ctjs.call %helper(%u, %element)
    ctjs.return %answer
  }
)MLIR");
    auto helper = mlir::parseSourceString<mlir::ModuleOp>(helperSource, &context);
    check(static_cast<bool>(helper), "helper-owned branch callback fixture parses");
    if (helper) {
        auto expanded = expandDOMHelpers(*helper, "entry$0", 100000);
        check(!expanded, "helper expansion retains its branch-local callback source identity");
        if (expanded) {
            std::fprintf(stderr, "%s\n", llvm::toString(std::move(expanded)).c_str());
        } else {
            check(!helper->lookupSymbol<ctjs::FuncOp>("helper$2"),
                  "only the completely expanded helper is retired");
            checkPrepared(*helper, "entry$0");
        }
    }

    const auto unusedSource = replaced(source, "\n}\n", R"MLIR(
  ctjs.func private @unused$2(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    ctjs.return %u
  }
}
)MLIR");
    auto unused = mlir::parseSourceString<mlir::ModuleOp>(unusedSource, &context);
    check(static_cast<bool>(unused), "original unvisited literal body parses");
    if (unused) {
        auto proved = expandDOMHelpers(*unused, "branch$0", 100000);
        check(!proved && !unused->lookupSymbol<ctjs::FuncOp>("unused$2") &&
                  hostContractFingerprint(*unused) == fingerprint,
              "an unreferenced original inert body proves independently before retirement");
        if (proved) { llvm::consumeError(std::move(proved)); }
        checkPrepared(*unused, "branch$0");
    }

    for (const auto & invalid :
         {replaced(source,
                   "%filtered =", "ctjs.store_global \"saved\", %callback\n      %filtered ="),
          replaced(source, "%filter(%keys, %callback)", "%callback(%u, %element)"),
          replaced(source, "%filter(%keys, %callback)", "%filter(%keys, %callback, %u)"),
          replaced(source, "%filtered = ctjs.call %filter(%keys, %callback)", R"MLIR(
      %filtered = scf.if %condition -> (!ctjs.value) {
        %left = ctjs.call %filter(%keys, %callback)
        scf.yield %left : !ctjs.value
      } else {
        %right = ctjs.call %filter(%keys, %callback)
        scf.yield %right : !ctjs.value
      }
)MLIR"),
          replaced(source, "%callee[1] this %u", "%element[1] this %u"),
          replaced(source, "%callee[1] this %u", "%callee[1] this %element"),
          replaced(source, "%callee[1] this %u", "%callee[1] this %u captures %element"),
          replaced(source, "%callee[1] this %u", "%callee[2] this %u"),
          replaced(source, "%callback =", "%cell = ctjs.create_cell %u\n      %callback ="),
          replaced(source, "%callback =",
                   "%duplicate = ctjs.create_closure %callee[1] this %u\n      %callback ="),
          replaced(source, "%filtered =",
                   "%result = ctjs.create_object\n      ctjs.set_property %result[%filterName], "
                   "%callback\n      %filtered ="),
          replaced(unusedSource, "%u = ctjs.constant #ctjs.undefined\n    ctjs.return %u",
                   "%u = ctjs.load_global \"unknown\"\n    ctjs.return %u"),
          replaced(unusedSource, "attributes {upvalue_count = 0 : i32}",
                   "attributes {upvalue_count = 0 : i32, test.reference = @unused$2}")}) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
        check(static_cast<bool>(input), "unsupported branch-local identity fixture parses");
        if (!input) { continue; }
        const auto before = hostContractFingerprint(*input);
        auto refused = expandDOMHelpers(*input, "branch$0", 100000);
        if (!refused) {
            std::fprintf(stderr, "unexpected source admission:\n%s\n", invalid.c_str());
        }
        check(static_cast<bool>(refused) && before == hostContractFingerprint(*input),
              "escaping, captured, repeated, generic or unproved unused identities remain refused");
        if (refused) { llvm::consumeError(std::move(refused)); }
    }

    auto loopSource = replaced(source, "%answer = scf.if", R"MLIR(
    %loop = scf.while (%state = %zero) : (!ctjs.value) -> !ctjs.value {
    %answer = scf.if
)MLIR");
    loopSource = replaced(loopSource, "ctjs.return %answer", R"MLIR(
      scf.condition(%condition) %answer : !ctjs.value
    } do {
    ^bb0(%state: !ctjs.value):
      scf.yield %state : !ctjs.value
    }
    ctjs.return %loop
)MLIR");
    auto loop = mlir::parseSourceString<mlir::ModuleOp>(loopSource, &context);
    check(static_cast<bool>(loop), "repeated branch callback fixture parses");
    if (loop) {
        const auto before = hostContractFingerprint(*loop);
        auto refused = expandDOMHelpers(*loop, "branch$0", 100000);
        check(static_cast<bool>(refused) && before == hostContractFingerprint(*loop),
              "loop-local callback creation remains refused without rewriting its source");
        if (refused) { llvm::consumeError(std::move(refused)); }
    }

    // Scheduling supplies no authority for effects or the original !element
    // guard. Those remain the complete entry proof's responsibility.
    for (const auto & invalid :
         {replaced(source, "%callback =",
                   "%result = ctjs.create_object\n      ctjs.set_property %result[%filterName], "
                   "%u\n      %callback ="),
          replaced(source, "ctjs.return %selected",
                   "ctjs.store_global \"saved\", %key\n    ctjs.return %selected"),
          replaced(source, "%condition = ctjs.truthy %flag",
                   "%not = ctjs.unary not %element\n    %condition = ctjs.truthy %not"),
          replaced(source,
                   "%filtered =", "ctjs.set_property %keys[%filterName], %u\n      %filtered =")}) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
        check(static_cast<bool>(input), "complete branch callback refusal fixture parses");
        if (!input) { continue; }
        const auto before = hostContractFingerprint(*input);
        auto prepared = expandDOMHelpers(*input, "branch$0", 100000);
        check(!prepared && before == hostContractFingerprint(*input),
              "preparation preserves every unsupported source observation for complete reproof");
        if (prepared) {
            llvm::consumeError(std::move(prepared));
            continue;
        }
        HostContract contract;
        contract.provider = HostContract::Provider::ctbrowserDOM;
        contract.entry = "branch$0";
        contract.elementParameters = {0};
        contract.datasetParameters = {0};
        contract.initialIntrinsics = {"Object", "Array", "String"};
        contract.moduleSha256 = before;
        check(noEvidence(*input, DOMEntryAnalysis(*input, contract)),
              "complete reproof rejects unsupported stored values, effects, mutation and guards");
    }
}

} // namespace ctcompile::test::host_contract
