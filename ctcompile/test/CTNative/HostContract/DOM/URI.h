#pragma once

#include "../Fixtures.h"
#include "mlir/Dialect/SCF/IR/SCF.h"

namespace ctcompile::test::host_contract {

inline void checkDOMURI(mlir::MLIRContext & context) {
    using namespace ctcompile::ctnative;
    const std::string source = R"MLIR(
module {
  ctjs.func @decode$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %element: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %undefined = ctjs.constant #ctjs.undefined
    %text = ctjs.constant #ctjs.string<"A%20B">
    %decode = ctjs.load_global "decodeURIComponent"
    %answer = ctjs.invoke {
      %called = ctjs.call %decode(%undefined, %text)
      ctjs.invoke_exit %called state()
    } normal {
    ^bb0(%returned: !ctjs.value):
      ctjs.invoke_yield(%returned)
    } unwind {
    ^bb0(%error: !ctjs.value):
      ctjs.invoke_yield(%text)
    } : !ctjs.value
    ctjs.return %answer
  }
}
)MLIR";
    auto module = parsed(context, source, "DOM URI completion fixture parses");
    if (!module) { return; }
    HostContract contract;
    contract.provider = HostContract::Provider::ctbrowserDOM;
    contract.entry = "decode$0";
    contract.elementParameters = {0};
    contract.initialIntrinsics = {"decodeURIComponent"};
    contract.moduleSha256 = hostContractFingerprint(*module);
    ctjs::LoadGlobalOp intrinsic;
    ctjs::CallOp call;
    ctjs::InvokeOp invocation;
    module->walk([&](ctjs::LoadGlobalOp op) { intrinsic = op; });
    module->walk([&](ctjs::CallOp op) { call = op; });
    module->walk([&](ctjs::InvokeOp op) { invocation = op; });
    const auto noEvidence = [&](const DOMEntryAnalysis & proof) {
        return !proof.proved() && !proof.entry() && proof.parameters().empty() &&
               !proof.isInitialIntrinsic(intrinsic) && !proof.call(call) &&
               !proof.invocation(invocation);
    };
    for (auto provider :
         {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
        contract.provider = provider;
        DOMEntryAnalysis proof(*module, contract);
        const auto * edge = proof.call(call);
        check(proof.proved() && proof.isInitialIntrinsic(intrinsic) &&
                  !proof.isNumberIntrinsic(intrinsic) && proof.invocation(invocation) && edge &&
                  edge->kind == HostDOMMethod::decodeURIComponent && edge->returnsString() &&
                  !edge->returnsNumber() && !edge->returnsBoolean() &&
                  edge->element == call.getArgs()[0],
              "URI proof preserves exact String input and both original continuations");
        if (!proof.proved()) { std::fprintf(stderr, "%s\n", proof.reason().str().c_str()); }
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "URI discovery leaves the complete source unchanged");
        if (proof.proved()) {
            check(DOMEntryAnalysis(*module, contract, proof.steps()).proved(),
                  "URI proof reproduces its exact completion budget");
            for (unsigned budget = 0; budget < proof.steps(); ++budget) {
                DOMEntryAnalysis limited(*module, contract, budget);
                check(noEvidence(limited) && limited.exhausted(),
                      "every incomplete URI proof withholds all live evidence");
            }
        }
        const std::string providerName = provider == HostContract::Provider::ctbrowserDOM
                                             ? "ctbrowser-dom-v1"
                                             : "ctbrowser-dom-session-v1";
        const std::string json = "{\"version\":1,\"provider\":\"" + providerName +
                                 "\",\"module_sha256\":\"" + contract.moduleSha256 +
                                 "\",\"entry\":\"decode$0\",\"element_parameters\":[0],"
                                 "\"initial_intrinsics\":[\"decodeURIComponent\"]}";
        for (llvm::StringRef names :
             {"[\"decodeURIComponent\"]", "[\"Number\",\"decodeURIComponent\"]",
              "[\"decodeURIComponent\",\"Number\"]"}) {
            auto parsed = parseHostContract(replaced(json, "[\"decodeURIComponent\"]", names));
            check(parsed && DOMEntryAnalysis(*module, *parsed).proved(),
                  "URI identity is independent of Number and declaration order");
            if (!parsed) { llvm::consumeError(parsed.takeError()); }
        }
    }
    for (const auto & names :
         {std::vector<std::string>{}, std::vector<std::string>{"Number"},
          std::vector<std::string>{"decodeURIComponent", "decodeURIComponent"}}) {
        auto request = contract;
        request.initialIntrinsics = names;
        check(noEvidence(DOMEntryAnalysis(*module, request)),
              "typed URI contract cannot infer or duplicate a supplied intrinsic");
    }
    mlir::Builder builder(&context);
    intrinsic->setAttr("ctnative.host_intrinsic", builder.getStringAttr("decodeURIComponent"));
    invocation->setAttr("ctnative.host_proved", builder.getBoolAttr(true));
    check(DOMEntryAnalysis(*module, contract).proved(),
          "advisory URI reports do not replace live discovery");
    auto argument = call.getArgs()[0];
    call->setOperand(
        2, module->lookupSymbol<ctjs::FuncOp>(contract.entry).getBody().front().getArgument(3));
    DOMEntryAnalysis stale(*module, contract);
    check(noEvidence(stale) && stale.reason().contains("fingerprint"),
          "stale URI source fingerprint withholds all evidence");
    auto fresh = contract;
    fresh.moduleSha256 = hostContractFingerprint(*module);
    check(noEvidence(DOMEntryAnalysis(*module, fresh)),
          "forged URI reports cannot authorize object coercion");
    call->setOperand(2, argument);
    auto payload = invocation.getUnwindBody().front().getArgument(0);
    payload.setType(builder.getI32Type());
    fresh.moduleSha256 = hostContractFingerprint(*module);
    check(noEvidence(DOMEntryAnalysis(*module, fresh)),
          "typed API rejects a fabricated primitive semantic payload");
    payload.setType(ctjs::ValueType::get(&context));

    const auto refused = [&](const std::string & text) {
        auto input = parsed(context, text, "URI capability mutation fixture parses");
        if (!input) { return; }
        auto request = contract;
        request.moduleSha256 = hostContractFingerprint(*input);
        DOMEntryAnalysis proof(*input, request);
        check(!proof.proved() && !proof.entry(), "complete URI census rejects unproved source");
    };
    for (llvm::StringRef arguments :
         {"%undefined", "%element, %text", "%undefined, %element", "%undefined, %text, %text"}) {
        refused(
            replaced(source, "%decode(%undefined, %text)", ("%decode(" + arguments + ")").str()));
    }
    refused(replaced(source, "ctjs.invoke_yield(%text)", "ctjs.invoke_yield(%error)"));
    refused(replaced(source, "ctjs.invoke_yield(%text)", "ctjs.invoke_yield(%undefined)"));
    refused(replaced(source, "%decode =", "%number = ctjs.load_global \"Number\"\n    %decode ="));
    refused(replaced(
        source, "%decode =", "ctjs.store_global \"decodeURIComponent\", %element\n    %decode ="));
    refused(replaced(source, "%called = ctjs.call %decode", "%called = ctjs.call %callee"));

    context.getOrLoadDialect<mlir::scf::SCFDialect>();
    const std::string numeric = R"MLIR(
module {
  ctjs.func @numeric$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %element: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %undefined = ctjs.constant #ctjs.undefined
    %value = ctjs.constant #ctjs.number<4607182418800017408>
    %name = ctjs.constant #ctjs.string<"data-number">
    %setName = ctjs.constant #ctjs.string<"setAttribute">
    %set = ctjs.get_property %element[%setName]
    %getName = ctjs.constant #ctjs.string<"getAttribute">
    %get = ctjs.get_property %element[%getName]
    %before = ctjs.call %get(%element, %name)
    WRITE
    %after = ctjs.call %get(%element, %name)
    %same = ctjs.compare strict_eq %before, %after
    ctjs.return %same
  }
}
)MLIR";
    const std::string write = "%written = ctjs.call %set(%element, %name, %value)";
    const std::string protectedWrite = R"MLIR(ctjs.invoke {
      %written = ctjs.call %set(%element, %name, %value)
      ctjs.invoke_exit %written state()
    } normal {
    ^bb0(%returned: !ctjs.value):
      ctjs.invoke_yield()
    } unwind {
    ^bb0(%error: !ctjs.value):
      ctjs.invoke_yield()
    })MLIR";
    const auto numericQuery = [&](const std::string & text, bool expected, bool budgets = false,
                                  bool declareNumber = true) {
        auto input = parsed(context, text, "numeric attribute completion fixture parses");
        if (!input) { return; }
        HostContract request;
        request.entry = "numeric$0";
        request.elementParameters = {0};
        request.moduleSha256 = hostContractFingerprint(*input);
        ctjs::CallOp setter;
        input->walk([&](ctjs::CallOp op) {
            auto method = op.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            if (method && ctjs::constantKey(method.getKey()) == "setAttribute") { setter = op; }
        });
        input->walk([&](ctjs::LoadGlobalOp op) {
            if (declareNumber && op.getName() == "Number") {
                request.initialIntrinsics = {"Number"};
            }
        });
        const auto empty = [&](const DOMEntryAnalysis & proof) {
            bool absent = !proof.proved() && !proof.entry() && proof.parameters().empty() &&
                          proof.savedThrows().empty() && proof.unreachableYields().empty();
            input->walk([&](ctjs::CallOp op) { absent &= !proof.call(op); });
            input->walk([&](ctjs::GetPropertyOp op) { absent &= !proof.method(op); });
            input->walk([&](ctjs::InvokeOp op) { absent &= !proof.invocation(op); });
            input->walk([&](ctjs::LoadGlobalOp op) { absent &= !proof.isNumberIntrinsic(op); });
            return absent;
        };
        for (auto provider :
             {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
            request.provider = provider;
            DOMEntryAnalysis proof(*input, request);
            check(proof.proved() == expected,
                  "numeric attribute admission retains the complete source contract");
            if (proof.proved() != expected) {
                std::fprintf(stderr, "%s\n%s\n", text.c_str(), proof.reason().str().c_str());
            }
            check(hostContractFingerprint(*input) == request.moduleSha256,
                  "numeric attribute discovery preserves both reads and the write order");
            if (!expected) {
                check(empty(proof), "refused numeric completion publishes no partial evidence");
                continue;
            }
            if (!proof.proved()) { continue; }
            input->walk([&](ctjs::CallOp op) {
                check(proof.call(op), "numeric attribute retains every read, conversion and write");
            });
            input->walk([&](ctjs::InvokeOp op) {
                check(proof.invocation(op), "numeric suppression retains its original wrapper");
            });
            input->walk([&](ctjs::ThrowOp op) {
                check(proof.savedThrows().size() == 1 && proof.savedThrows().front() == op,
                      "numeric attribute write preserves the exact saved exception");
            });
            const auto * edge = proof.call(setter);
            check(edge && edge->kind == HostDOMMethod::setAttribute && !edge->returnsNumber() &&
                      !edge->returnsBoolean() && !edge->returnsString() &&
                      edge->element == setter.getReceiver(),
                  "numeric argument does not change the setter receiver or undefined result");
            if (!budgets) { continue; }
            check(DOMEntryAnalysis(*input, request, proof.steps()).proved(),
                  "numeric completion reproduces its exact proof budget");
            for (unsigned budget = 0; budget < proof.steps(); ++budget) {
                DOMEntryAnalysis limited(*input, request, budget);
                check(limited.exhausted() && empty(limited),
                      "incomplete numeric completion withholds every effect and throw fact");
            }
            setter->setAttr("ctnative.host_proved", builder.getBoolAttr(true));
            const auto value = setter.getArgs()[1];
            setter->setOperand(3, setter.getReceiver());
            DOMEntryAnalysis stale(*input, request);
            check(empty(stale) && stale.reason().contains("fingerprint"),
                  "stale numeric completion fingerprint withholds all evidence");
            auto freshRequest = request;
            freshRequest.moduleSha256 = hostContractFingerprint(*input);
            check(empty(DOMEntryAnalysis(*input, freshRequest)),
                  "fresh fingerprint and forged reports cannot authorize object coercion");
            setter->setOperand(3, value);
            setter->removeAttr("ctnative.host_proved");
        }
    };
    for (const auto & operation : {write, protectedWrite}) {
        const auto base = replaced(numeric, "WRITE", operation);
        // Exact IEEE-754 bits: 1, +0, -0, +Infinity, -Infinity and NaN.
        for (llvm::StringRef bits :
             {"4607182418800017408", "0", "9223372036854775808", "9218868437227405312",
              "18442240474082181120", "9221120237041090560"}) {
            numericQuery(replaced(base, "4607182418800017408", bits), true, bits == "0");
        }
        for (llvm::StringRef expression :
             {"ctjs.unary neg %operand", "ctjs.unary plus %operand", "ctjs.unary bitnot %operand",
              "ctjs.binary add %operand, %operand", "ctjs.binary sub %operand, %operand",
              "ctjs.binary mul %operand, %operand", "ctjs.binary div %operand, %operand",
              "ctjs.binary mod %operand, %operand", "ctjs.binary pow %operand, %operand",
              "ctjs.binary shl %operand, %operand", "ctjs.binary shr %operand, %operand",
              "ctjs.binary ushr %operand, %operand", "ctjs.binary bitand %operand, %operand",
              "ctjs.binary bitor %operand, %operand", "ctjs.binary bitxor %operand, %operand"}) {
            const auto expressionSource = replaced(
                base, "%value = ctjs.constant #ctjs.number<4607182418800017408>",
                ("%operand = ctjs.constant #ctjs.number<4607182418800017408>\n    %value = " +
                 expression)
                    .str());
            numericQuery(expressionSource, true);
            numericQuery(replaced(expressionSource,
                                  "%operand = ctjs.constant #ctjs.number<4607182418800017408>",
                                  "%operand = ctjs.create_object"),
                         false);
        }
        auto fromRead = replaced(base, "%value = ctjs.constant #ctjs.number<4607182418800017408>",
                                 "%Number = ctjs.load_global \"Number\"");
        fromRead =
            replaced(fromRead, "    " + operation,
                     "    %value = ctjs.call %Number(%undefined, %before)\n    " + operation);
        numericQuery(fromRead, true);
        numericQuery(fromRead, false, false, false);
        auto transported = replaced(fromRead, "    " + operation, R"MLIR(
    %condition = ctjs.truthy %before
    %joined = scf.if %condition -> (!ctjs.value) {
      scf.yield %value : !ctjs.value
    } else {
      %zero = ctjs.constant #ctjs.number<0>
      scf.yield %zero : !ctjs.value
    }
    )MLIR" + operation);
        transported = replaced(transported, "%set(%element, %name, %value)",
                               "%set(%element, %name, %joined)");
        numericQuery(transported, true);
        // A Number/null join converts without user code. Suppression still
        // requires one exact scalar kind.
        numericQuery(replaced(transported, "%zero = ctjs.constant #ctjs.number<0>",
                              "%zero = ctjs.constant #ctjs.null"),
                     operation == write);
        for (llvm::StringRef argument : {"%element", "%undefined"}) {
            numericQuery(replaced(base, "%set(%element, %name, %value)",
                                  ("%set(%element, %name, " + argument + ")").str()),
                         false);
        }
        numericQuery(replaced(base, "ctjs.call %set", "ctjs.call %callee"), false);
    }
    const auto suppressed = replaced(numeric, "WRITE", protectedWrite);
    for (const auto & invalid :
         {replaced(suppressed, "data-number", "bad name"),
          replaced(suppressed, "%set(%element, %name, %value)", "%set(%element, %value, %value)"),
          replaced(suppressed, "%set(%element, %name, %value)", "%set(%element, %before, %value)"),
          replaced(suppressed, "%set(%element, %name, %value)", "%set(%before, %name, %value)"),
          replaced(suppressed, "%set(%element, %name, %value)", "%set(%element, %name, %before)"),
          replaced(suppressed, "#ctjs.number<4607182418800017408>", "#ctjs.null"),
          replaced(suppressed, "ctjs.constant #ctjs.number<4607182418800017408>",
                   "ctjs.create_object"),
          replaced(suppressed, "^bb0(%error: !ctjs.value):",
                   "^bb0(%error: !ctjs.value):\n      %observed = ctjs.unary typeof %error"),
          replaced(suppressed, "^bb0(%returned: !ctjs.value):",
                   "^bb0(%returned: !ctjs.value):\n      %observed = ctjs.unary typeof %returned"),
          replaced(suppressed, "    ctjs.return %same",
                   "    ctjs.store_global \"leaked\", %after\n    ctjs.return %same")}) {
        numericQuery(invalid, false);
    }
    check(!mlir::parseSourceString<mlir::ModuleOp>(
              replaced(suppressed, "ctjs.invoke_exit %written state()",
                       "%extra = ctjs.call %get(%element, %name)\n"
                       "      ctjs.invoke_exit %written state()"),
              &context),
          "numeric attribute invocation rejects an extra call before proof");
    const auto saved = replaced(suppressed, "    %after =", R"MLIR(
    %condition = ctjs.truthy %before
    scf.if %condition {
      scf.execute_region {
        ctjs.throw %value
      } {no_inline}
      scf.yield
    }
    %after =)MLIR");
    numericQuery(saved, true, true);
    numericQuery(replaced(saved, "ctjs.throw %value", "ctjs.throw %element"), false);
}

} // namespace ctcompile::test::host_contract
