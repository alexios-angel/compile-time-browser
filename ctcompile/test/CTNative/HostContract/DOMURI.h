#pragma once

#include "HostContractFixtures.h"

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
    auto module = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
    check(static_cast<bool>(module), "DOM URI completion fixture parses");
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
        auto input = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(input), "URI capability mutation fixture parses");
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
}

} // namespace ctcompile::test::host_contract
