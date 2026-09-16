#include "Tests.h"

#include "check.hpp"

using namespace ctcompile::test::owned_global_shared_map;

namespace {

void checkSessionOwner(mlir::MLIRContext & context) {
    for (const char * source : {capturedFixture, importedCapturedFixture}) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
        check(static_cast<bool>(module), "session captured-table fixture parses");
        if (!module) { continue; }
        auto contract = contractFor(*module);
        contract.initialIntrinsics = {"Map"};
        OwnedGlobalRoots ordinary(*module, contract);
        contract.provider = HostContract::Provider::closedSourceSession;
        OwnedGlobalRoots session(*module, contract);
        check(ordinary.proved() && session.proved() && session.roots().size() == 1 &&
                  session.roots().front().methodTable &&
                  session.roots().front().methodTable->capturedMap &&
                  session.roots().front().methodTable->calls.size() == 1 &&
                  ordinary.steps() == session.steps(),
              "session admission reuses the exact complete captured-table owner proof");
        if (!session.proved()) {
            std::fprintf(stderr, "session owner: %s\n", session.reason().str().c_str());
            continue;
        }
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "session proof leaves every source operation intact");
        for (unsigned budget : {0u, session.steps() - 1}) {
            OwnedGlobalRoots limited(*module, contract, budget);
            check(!limited.proved() && limited.exhausted() && empty(*module, limited),
                  "incomplete session proof exposes no owner or callable evidence");
        }
        check(OwnedGlobalRoots(*module, contract, session.steps()).proved(),
              "the exact completed session budget admits the owner");
    }
    for (const auto & source :
         {std::string(fixture),
          replaced(capturedFixture, "ctjs.store_global \"trace\", %answer",
                   "ctjs.store_global \"trace\", %getter"),
          replaced(capturedFixture, "%getter(%owned)", "%getter(%host)"),
          replaced(capturedFixture, "ctjs.return %size", "ctjs.return %state"),
          replaced(capturedFixture, "captures %cell", "captures %u"),
          replaced(capturedFixture, "    ctjs.return %table",
                   "    ctjs.set_property %table[%key], %getter\n    ctjs.return %table")}) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
        check(static_cast<bool>(module), "session refusal fixture parses");
        if (!module) { continue; }
        auto contract = contractFor(*module);
        contract.initialIntrinsics = {"Map"};
        contract.provider = HostContract::Provider::closedSourceSession;
        mlir::Builder builder(&context);
        (*module)->setAttr("ctnative.host_proved", builder.getBoolAttr(true));
        OwnedGlobalRoots refused(*module, contract);
        check(!refused.proved() && !refused.exhausted() && empty(*module, refused),
              "session refuses uncaptured tables, escaping methods, wrong receivers and captures");
    }
    auto module = mlir::parseSourceString<mlir::ModuleOp>(capturedFixture, &context);
    check(static_cast<bool>(module), "session fingerprint fixture parses");
    if (!module) { return; }
    auto contract = contractFor(*module);
    contract.initialIntrinsics = {"Map"};
    contract.provider = HostContract::Provider::closedSourceSession;
    auto getter = module->lookupSymbol<ctjs::FuncOp>("get$3");
    getter.walk([&](ctjs::ReturnOp returned) {
        returned->setOperand(0, getter.getBody().front().getArgument(0));
    });
    OwnedGlobalRoots stale(*module, contract);
    check(!stale.proved() && stale.reason().contains("fingerprint") && empty(*module, stale),
          "session cannot reuse a manifest after method semantics change");
    contract.moduleSha256 = hostContractFingerprint(*module);
    OwnedGlobalRoots changed(*module, contract);
    check(!changed.proved() && empty(*module, changed),
          "a fresh session manifest cannot authorize the method's receiver escape");
}

} // namespace

int main() {
    mlir::MLIRContext context;
    context.getOrLoadDialect<ctjs::CTJSDialect>();
    context.getOrLoadDialect<mlir::scf::SCFDialect>();
    checkSharedMap(context);
    checkSessionOwner(context);
    if (ctbrowser_test_failures == 0) { std::puts("owned global shared Map proofs passed"); }
    return ctbrowser_test_failures == 0 ? 0 : 1;
}
