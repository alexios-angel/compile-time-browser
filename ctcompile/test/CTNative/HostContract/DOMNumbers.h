#pragma once

#include "HostContractFixtures.h"
#include "mlir/Dialect/SCF/IR/SCF.h"

namespace ctcompile::test::host_contract {

inline void checkDOMNumbers(mlir::MLIRContext & context) {
    using namespace ctcompile::ctnative;
    context.getOrLoadDialect<mlir::scf::SCFDialect>();
    constexpr llvm::StringLiteral source = R"MLIR(
module {
  ctjs.func @numeric$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %element: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %undefined = ctjs.constant #ctjs.undefined
    %getKey = ctjs.constant #ctjs.string<"getAttribute">
    %get = ctjs.get_property %element[%getKey]
    %name = ctjs.constant #ctjs.string<"data-bs-config">
    %saved = ctjs.call %get(%element, %name)
    %Number = ctjs.load_global "Number"
    %converted = ctjs.call %Number(%undefined, %saved)
    %stringKey = ctjs.constant #ctjs.string<"toString">
    %stringMethod = ctjs.get_property %converted[%stringKey]
    %text = ctjs.call %stringMethod(%converted)
    %same = ctjs.compare strict_eq %saved, %text
    ctjs.return %same
  }
}
)MLIR";
    auto module = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
    check(static_cast<bool>(module), "DOM Number attribute comparison fixture parses");
    if (!module) { return; }
    HostContract contract;
    contract.provider = HostContract::Provider::ctbrowserDOM;
    contract.entry = "numeric$0";
    contract.elementParameters = {0};
    contract.initialIntrinsics = {"Number"};
    contract.moduleSha256 = hostContractFingerprint(*module);
    ctjs::LoadGlobalOp intrinsic;
    ctjs::CallOp conversion, stringification;
    ctjs::GetPropertyOp method;
    module->walk([&](ctjs::LoadGlobalOp load) { intrinsic = load; });
    module->walk([&](ctjs::GetPropertyOp read) {
        if (ctjs::constantKey(read.getKey()) == "toString") { method = read; }
    });
    module->walk([&](ctjs::CallOp call) {
        if (call.getCallee() == intrinsic.getResult()) { conversion = call; }
        if (call.getCallee() == method.getResult()) { stringification = call; }
    });
    const auto noEvidence = [&](const DOMEntryAnalysis & proof) {
        return !proof.proved() && !proof.entry() && proof.parameters().empty() &&
               !proof.isNumberIntrinsic(intrinsic) && !proof.call(conversion) &&
               !proof.call(stringification) && !proof.method(method);
    };
    for (auto provider :
         {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
        contract.provider = provider;
        const std::string providerName = provider == HostContract::Provider::ctbrowserDOM
                                             ? "ctbrowser-dom-v1"
                                             : "ctbrowser-dom-session-v1";
        const std::string json = "{\"version\":1,\"provider\":\"" + providerName +
                                 "\",\"module_sha256\":\"" + contract.moduleSha256 +
                                 "\",\"entry\":\"numeric$0\",\"element_parameters\":[0],"
                                 "\"initial_intrinsics\":[\"Number\"]}";
        auto parsed = parseHostContract(json);
        check(parsed && parsed->initialIntrinsics == contract.initialIntrinsics &&
                  DOMEntryAnalysis(*module, *parsed).proved(),
              "both DOM providers accept the explicit initial Number identity");
        if (!parsed) { llvm::consumeError(parsed.takeError()); }
        for (llvm::StringRef invalid :
             {"[\"Map\"]", "[\"Number.prototype.toString\"]", "[\"Number\",\"Number\"]",
              "[\"Number\",\"Array\"]", "[false]", "null"}) {
            auto rejected = parseHostContract(replaced(json, "[\"Number\"]", invalid));
            check(!rejected, "DOM Number identity cannot import ambiguous or extra intrinsics");
            if (!rejected) { llvm::consumeError(rejected.takeError()); }
        }
        DOMEntryAnalysis proof(*module, contract);
        const auto * number = proof.call(conversion);
        const auto * text = proof.call(stringification);
        check(proof.proved() && proof.isNumberIntrinsic(intrinsic) && number && text &&
                  number->returnsNumber() && !number->returnsString() &&
                  !number->returnsBoolean() && number->element == conversion.getArgs()[0] &&
                  text->returnsString() && !text->returnsNumber() && !text->returnsBoolean() &&
                  text->element == conversion.getResult() &&
                  proof.method(method) == HostDOMMethod::numberToString,
              "numeric proof preserves exact builtin, scalar input, receiver and result kinds");
        if (!proof.proved()) { std::fprintf(stderr, "%s\n", proof.reason().str().c_str()); }
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "Number source discovery preserves the complete module");
        if (proof.proved()) {
            check(DOMEntryAnalysis(*module, contract, proof.steps()).proved(),
                  "Number proof reproduces its exact completion budget");
            for (unsigned budget = 0; budget < proof.steps(); ++budget) {
                DOMEntryAnalysis limited(*module, contract, budget);
                check(noEvidence(limited) && limited.exhausted(),
                      "every incomplete Number census withholds all live evidence");
            }
        }
    }
    for (const auto & identities : {std::vector<std::string>{}, std::vector<std::string>{"Map"},
                                    std::vector<std::string>{"Number", "Number"}}) {
        auto invalid = contract;
        invalid.initialIntrinsics = identities;
        check(noEvidence(DOMEntryAnalysis(*module, invalid)),
              "typed API cannot infer Number identity from a load name or bypass premise checks");
    }
    mlir::Builder builder(&context);
    (*module)->setAttr("ctnative.host_proved", builder.getBoolAttr(true));
    intrinsic->setAttr("ctnative.host_intrinsic", builder.getStringAttr("Number"));
    method->setAttr("ctnative.host_method", builder.getStringAttr("numberToString"));
    check(DOMEntryAnalysis(*module, contract).proved(),
          "forged reports never replace live Number discovery");
    auto receiver = stringification.getReceiver();
    stringification->setOperand(1, conversion.getArgs()[0]);
    DOMEntryAnalysis stale(*module, contract);
    check(noEvidence(stale) && stale.reason().contains("fingerprint"),
          "stale fingerprint withholds Number evidence after source receiver mutation");
    contract.moduleSha256 = hostContractFingerprint(*module);
    check(noEvidence(DOMEntryAnalysis(*module, contract)),
          "fresh fingerprint and forged Number reports cannot detach the receiver");
    stringification->setOperand(1, receiver);

    const auto query = [&](const std::string & text, bool expected) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(input), "DOM numeric census mutation fixture parses");
        if (!input) { return; }
        auto request = contract;
        request.moduleSha256 = hostContractFingerprint(*input);
        DOMEntryAnalysis proof(*input, request);
        check(proof.proved() == expected, "complete DOM Number census matches source capability");
        if (proof.proved() != expected) {
            std::fprintf(stderr, "%s\n%s\n", text.c_str(), proof.reason().str().c_str());
        }
    };
    for (llvm::StringRef result : {"%text", "%converted"}) {
        query(replaced(source.str(), "ctjs.return %same", ("ctjs.return " + result).str()), true);
    }
    for (llvm::StringRef input : {"%name", "%null"}) {
        auto text = replaced(source.str(), "%Number(%undefined, %saved)",
                             ("%Number(%undefined, " + input + ")").str());
        text = replaced(text, "%Number =", "%null = ctjs.constant #ctjs.null\n    %Number =");
        query(text, true);
    }
    for (llvm::StringRef call :
         {"%Number(%undefined)", "%Number(%element, %saved)", "%Number(%undefined, %saved, %saved)",
          "%Number(%undefined, %element)", "%Number(%undefined, %Number)",
          "%Number(%undefined, %undefined)"}) {
        query(replaced(source.str(), "%Number(%undefined, %saved)", call), false);
    }
    query(replaced(source.str(), "%stringMethod(%converted)", "%stringMethod(%converted, %name)"),
          false);
    query(replaced(source.str(), "ctjs.return %same", "ctjs.return %Number"), false);
    query(replaced(source.str(), "ctjs.return %same", "ctjs.return %stringMethod"), false);
    const std::string branch = R"MLIR(
    %false = ctjs.constant #ctjs.boolean<false>
    %never = ctjs.truthy %false
    scf.if %never {
      EFFECT
      scf.yield
    }
)MLIR";
    for (llvm::StringRef effect : {"ctjs.store_global \"Number\", %undefined",
                                   "ctjs.set_property %Number[%name], %undefined",
                                   "%reentry = ctjs.call %callee(%undefined)"}) {
        query(replaced(source.str(),
                       "    %stringKey =", replaced(branch, "EFFECT", effect) + "    %stringKey ="),
              false);
    }
    const std::string joined = R"MLIR(
    %false = ctjs.constant #ctjs.boolean<false>
    %never = ctjs.truthy %false
    %joined = scf.if %never -> (!ctjs.value) {
      scf.yield %converted : !ctjs.value
    } else {
      %zero = ctjs.constant #ctjs.number<0>
      scf.yield %zero : !ctjs.value
    }
    ctjs.return %joined
)MLIR";
    query(replaced(source.str(), "ctjs.return %same", joined), true);
    query(replaced(replaced(source.str(), "ctjs.return %same", joined), "#ctjs.number<0>",
                   "#ctjs.null"),
          false);
    auto declared = replaced(source.str(), "numeric$0", "Number$1");
    declared = replaced(declared, "module {", R"MLIR(module {
  ctjs.func @script$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %undefined = ctjs.constant #ctjs.undefined
    %function = ctjs.create_closure %callee[1] this %undefined
    ctjs.store_global "Number", %function
    ctjs.return %undefined
  }
)MLIR");
    contract.entry = "Number$1";
    query(declared, false);
}

} // namespace ctcompile::test::host_contract
