#pragma once

#include "HostContractFixtures.h"
#include "mlir/Dialect/SCF/IR/SCF.h"

namespace ctcompile::test::host_contract {

inline void checkDOMPrototypeQuery(mlir::MLIRContext & context) {
    using namespace ctcompile::ctnative;
    const std::string source = R"MLIR(
module {
  ctjs.func @query$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %element: !ctjs.value, %expected: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %Element = ctjs.load_global "Element"
    %prototypeName = ctjs.constant #ctjs.string<"prototype">
    %prototype = ctjs.get_property %Element[%prototypeName]
    %queryName = ctjs.constant #ctjs.string<"querySelector">
    %method = ctjs.get_property %prototype[%queryName]
    %callName = ctjs.constant #ctjs.string<"call">
    %invoke = ctjs.get_property %method[%callName]
    %selector = ctjs.constant #ctjs.string<"button">
    %found = ctjs.call %invoke(%method, %element, %selector)
    %answer = ctjs.compare strict_eq %found, %expected
    ctjs.return %answer
  }
}
)MLIR";
    const auto collection = replaced(
        replaced(source, "#ctjs.string<\"querySelector\">", "#ctjs.string<\"querySelectorAll\">"),
        "%answer = ctjs.compare strict_eq %found, %expected",
        "%lengthName = ctjs.constant #ctjs.string<\"length\">\n"
        "    %answer = ctjs.get_property %found[%lengthName]");
    HostContract contract;
    contract.entry = "query$0";
    contract.elementParameters = {0, 1};
    contract.initialIntrinsics = {"Element", "Function"};
    const auto noEvidence = [](mlir::ModuleOp input, const DOMEntryAnalysis & proof) {
        bool empty = !proof.proved() && !proof.entry() && !proof.wrapper() &&
                     !proof.documentParameter() && proof.parameters().empty();
        input.walk([&](ctjs::CallOp call) { empty &= !proof.call(call); });
        input.walk([&](ctjs::LoadGlobalOp load) {
            empty &= !proof.isInitialIntrinsic(load) && !proof.isCurrentDocument(load);
        });
        input.walk([&](ctjs::GetPropertyOp read) {
            empty &= !proof.method(read) && !proof.isElementPrototype(read) &&
                     !proof.isDocumentElement(read) && !proof.isElementVectorLength(read) &&
                     !proof.isElementVectorIndex(read);
        });
        return empty;
    };
    const auto query = [&](const std::string & text, bool expected, bool rootGuard = false) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(input), "independent prototype selector fixture parses");
        if (!input) { return; }
        auto request = contract;
        request.moduleSha256 = hostContractFingerprint(*input);
        DOMEntryAnalysis proof(*input, request);
        check(proof.proved() == expected, "prototype selector requires exact source identities");
        if (proof.proved() != expected) {
            std::fprintf(stderr, "%s\n%s\n", text.c_str(), proof.reason().str().c_str());
        }
        check(hostContractFingerprint(*input) == request.moduleSha256,
              "prototype selector proof does not change source");
        if (!expected) {
            check(noEvidence(*input, proof), "refused prototype call publishes no evidence");
            return;
        }
        if (!proof.proved()) { return; }
        const bool document = request.currentDocumentParameter.has_value();
        unsigned intrinsics = 0, documents = 0, prototypes = 0, methods = 0, calls = 0, lengths = 0,
                 roots = 0;
        input->walk([&](ctjs::LoadGlobalOp load) {
            intrinsics += proof.isInitialIntrinsic(load);
            documents += proof.isCurrentDocument(load);
        });
        input->walk([&](ctjs::GetPropertyOp read) {
            prototypes += proof.isElementPrototype(read);
            methods += proof.method(read).has_value();
            lengths += proof.isElementVectorLength(read);
            roots += proof.isDocumentElement(read);
        });
        input->walk([&](ctjs::CallOp call) {
            const auto * edge = proof.call(call);
            if (!edge) { return; }
            ++calls;
            if (rootGuard) {
                check(!edge->explicitReceiver && edge->element == call.getReceiver() &&
                          edge->kind == HostDOMMethod::matches && edge->usesStyle(),
                      "a guarded root reread retains its receiver and Style association");
                return;
            }
            if (document) {
                check(!edge->explicitReceiver && edge->element == proof.documentParameter() &&
                          edge->kind == HostDOMMethod::documentQuerySelectorAll &&
                          edge->returnsElementVector() && edge->usesStyle(),
                      "document snapshot retains the exact current document's owner and Style");
                return;
            }
            check(edge->explicitReceiver && edge->element == call.getArgs().front() &&
                      edge->usesStyle() &&
                      (text == collection ? edge->returnsElementVector() : edge->returnsElement()),
                  "prototype call binds its explicit element, Style and result carrier");
        });
        check(document ? documents == (rootGuard ? 2u : 1u) && intrinsics == 0 && prototypes == 0 &&
                             methods == 1 && calls == 1 && lengths == (rootGuard ? 0u : 1u) &&
                             roots == (rootGuard ? 2u : 0u) &&
                             proof.documentParameter() ==
                                 proof.parameters()[*request.currentDocumentParameter]
                       : documents == 0 && intrinsics == 1 && prototypes == 1 && methods == 2 &&
                             calls == 1,
              "only the original browser binding, method and call chain receive evidence");
        check(DOMEntryAnalysis(*input, request, proof.steps()).proved(),
              "prototype proof reproduces its exact completion budget");
        for (unsigned budget = 0; budget < proof.steps(); ++budget) {
            DOMEntryAnalysis limited(*input, request, budget);
            check(limited.exhausted() && noEvidence(*input, limited),
                  "every incomplete budget withholds all prototype and call evidence");
        }
        auto stale = request;
        stale.moduleSha256.assign(64, '0');
        check(noEvidence(*input, DOMEntryAnalysis(*input, stale)),
              "stale source fingerprint never grants prototype identity");
    };
    for (auto provider :
         {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
        contract.provider = provider;
        query(source, true);
        query(collection, true);
        for (const auto & intrinsics : {std::vector<std::string>{},
                                        {"Element"},
                                        {"Function"},
                                        {"Element", "Function", "Element"}}) {
            contract.initialIntrinsics = intrinsics;
            query(source, false);
        }
        contract.initialIntrinsics = {"Element", "Function"};
    }
    for (const auto & invalid :
         {replaced(source, "%invoke(%method, %element, %selector)",
                   "%invoke(%prototype, %element, %selector)"),
          replaced(source, "%invoke(%method, %element, %selector)",
                   "%invoke(%element, %element, %selector)"),
          replaced(source, "%invoke(%method, %element, %selector)",
                   "%invoke(%method, %selector, %selector)"),
          replaced(source, "%invoke(%method, %element, %selector)",
                   "%invoke(%method, %element, %expected)"),
          replaced(source, "%invoke(%method, %element, %selector)", "%invoke(%method, %element)"),
          replaced(source, "%invoke(%method, %element, %selector)",
                   "%invoke(%method, %element, %selector, %selector)"),
          replaced(source, "%invoke(%method, %element, %selector)",
                   "%method(%prototype, %selector)"),
          replaced(source, "#ctjs.string<\"call\">", "#ctjs.string<\"apply\">"),
          replaced(source, "#ctjs.string<\"querySelector\">", "#ctjs.string<\"getAttribute\">"),
          replaced(source, "    %found =",
                   "    ctjs.set_property %method[%callName], %element\n    %found ="),
          replaced(source, "    %found =",
                   "    ctjs.set_property %prototype[%queryName], %element\n    %found ="),
          replaced(source,
                   "    %found =", "    ctjs.store_global \"Element\", %element\n    %found ="),
          replaced(source, "ctjs.return %answer", "ctjs.return %prototype"),
          replaced(source, "ctjs.return %answer", "ctjs.return %method"),
          replaced(source, "ctjs.return %answer", "ctjs.return %invoke")}) {
        query(invalid, false);
    }

    const auto document =
        replaced(replaced(collection, R"MLIR(    %Element = ctjs.load_global "Element"
    %prototypeName = ctjs.constant #ctjs.string<"prototype">
    %prototype = ctjs.get_property %Element[%prototypeName]
    %queryName = ctjs.constant #ctjs.string<"querySelectorAll">
    %method = ctjs.get_property %prototype[%queryName]
    %callName = ctjs.constant #ctjs.string<"call">
    %invoke = ctjs.get_property %method[%callName])MLIR",
                          R"MLIR(    %document = ctjs.load_global "document"
    %queryName = ctjs.constant #ctjs.string<"querySelectorAll">
    %method = ctjs.get_property %document[%queryName])MLIR"),
                 "%invoke(%method, %element, %selector)", "%method(%document, %selector)");
    contract.initialIntrinsics.clear();
    for (auto provider :
         {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
        contract.provider = provider;
        contract.currentDocumentParameter.reset();
        query(document, false);
        for (unsigned anchor : {0u, 1u}) {
            contract.currentDocumentParameter = anchor;
            query(document, true);
        }
    }
    for (const auto & invalid : {
             replaced(document, "%method(%document, %selector)", "%method(%element, %selector)"),
             replaced(document, "load_global \"document\"", "load_global \"window\""),
             replaced(document, "#ctjs.string<\"querySelectorAll\">",
                      "#ctjs.string<\"querySelector\">"),
             replaced(document, "ctjs.return %answer", "ctjs.return %found"),
         }) {
        query(invalid, false);
    }
    auto input = mlir::parseSourceString<mlir::ModuleOp>(document, &context);
    check(static_cast<bool>(input), "mutable document snapshot fixture parses");
    if (!input) { return; }
    ctjs::CallOp call;
    input->walk([&](ctjs::CallOp found) { call = found; });
    auto method = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
    mlir::Builder builder(&context);
    (*input)->setAttr("ctnative.host_proved", builder.getBoolAttr(true));
    method->setAttr("ctnative.dom_method", builder.getStringAttr("documentQuerySelectorAll"));
    contract.moduleSha256 = hostContractFingerprint(*input);
    check(DOMEntryAnalysis(*input, contract).proved(),
          "printed reports do not replace live document proof");
    const auto receiver = call.getReceiver();
    call->setOperand(1, input->lookupSymbol<ctjs::FuncOp>(contract.entry)
                            .getBody()
                            .front()
                            .getArgument(ctjs::implicit_arguments));
    check(noEvidence(*input, DOMEntryAnalysis(*input, contract)),
          "changed document receiver invalidates a stale source contract");
    contract.moduleSha256 = hostContractFingerprint(*input);
    check(noEvidence(*input, DOMEntryAnalysis(*input, contract)),
          "fresh fingerprint and forged report cannot authorize a detached document method");
    call->setOperand(1, receiver);
    contract.moduleSha256 = hostContractFingerprint(*input);
    check(DOMEntryAnalysis(*input, contract).proved(),
          "restoring the original document receiver restores the live proof");

    context.getOrLoadDialect<mlir::scf::SCFDialect>();
    const std::string guardedRoot = R"MLIR(
module {
  ctjs.func @query$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %element: !ctjs.value, %expected: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %document = ctjs.load_global "document"
    %rootName = ctjs.constant #ctjs.string<"documentElement">
    %selector = ctjs.constant #ctjs.string<"button">
    %root = ctjs.get_property %document[%rootName]
    %test = ctjs.truthy %root
    %answer = scf.if %test -> (!ctjs.value) {
      %current = ctjs.load_global "document"
      %reread = ctjs.get_property %current[%rootName]
      %methodName = ctjs.constant #ctjs.string<"matches">
      %method = ctjs.get_property %reread[%methodName]
      %matched = ctjs.call %method(%reread, %selector)
      scf.yield %matched : !ctjs.value
    } else {
      %absent = ctjs.constant #ctjs.boolean<false>
      scf.yield %absent : !ctjs.value
    }
    ctjs.return %answer
  }
}
)MLIR";
    for (auto provider :
         {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
        contract.provider = provider;
        contract.currentDocumentParameter.reset();
        query(guardedRoot, false);
        for (unsigned anchor : {0u, 1u}) {
            contract.currentDocumentParameter = anchor;
            query(guardedRoot, true, true);
        }
    }
    for (const auto & invalid : {
             replaced(guardedRoot, "%test = ctjs.truthy %root",
                      "%not = ctjs.unary not %root\n    %test = ctjs.truthy %not"),
             replaced(guardedRoot, "%test = ctjs.truthy %root",
                      "%flag = ctjs.constant #ctjs.boolean<true>\n    %test = ctjs.truthy %flag"),
             replaced(guardedRoot, "%root = ctjs.get_property %document[%rootName]",
                      "%queryName = ctjs.constant #ctjs.string<\"querySelector\">\n"
                      "    %query = ctjs.get_property %document[%queryName]\n"
                      "    %root = ctjs.call %query(%document, %selector)"),
             replaced(guardedRoot, "    ctjs.return %answer",
                      "    %later = ctjs.get_property %document[%rootName]\n"
                      "    %name = ctjs.constant #ctjs.string<\"matches\">\n"
                      "    %method = ctjs.get_property %later[%name]\n"
                      "    %matched = ctjs.call %method(%later, %selector)\n"
                      "    ctjs.return %matched"),
         }) {
        query(invalid, false);
    }
    input = mlir::parseSourceString<mlir::ModuleOp>(guardedRoot, &context);
    check(static_cast<bool>(input), "mutable document root guard fixture parses");
    if (!input) { return; }
    ctjs::TruthyOp guard;
    ctjs::GetPropertyOp reread;
    input->walk([&](ctjs::TruthyOp found) { guard = found; });
    input->walk([&](ctjs::GetPropertyOp found) {
        if (ctjs::constantKey(found.getKey()) == "documentElement") { reread = found; }
    });
    const auto guarded = guard.getValue();
    mlir::OpBuilder at(guard);
    auto forged =
        ctjs::ConstantOp::create(at, guard.getLoc(), ctjs::BooleanAttr::get(&context, true));
    reread->setAttr("ctnative.document_root_present", builder.getBoolAttr(true));
    contract.moduleSha256 = hostContractFingerprint(*input);
    check(DOMEntryAnalysis(*input, contract).proved(), "live root guard ignores printed reports");
    guard->setOperand(0, forged.getResult());
    check(noEvidence(*input, DOMEntryAnalysis(*input, contract)),
          "changed document root guard invalidates its source fingerprint");
    contract.moduleSha256 = hostContractFingerprint(*input);
    check(noEvidence(*input, DOMEntryAnalysis(*input, contract)),
          "fresh fingerprint and forged root report cannot replace a presence guard");
    guard->setOperand(0, guarded);
    contract.moduleSha256 = hostContractFingerprint(*input);
    check(DOMEntryAnalysis(*input, contract).proved(),
          "restoring the original root guard restores its live presence proof");
}

} // namespace ctcompile::test::host_contract
