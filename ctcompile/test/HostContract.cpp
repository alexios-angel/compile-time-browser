#include "ctcompile/CTNative/Analysis/HostContract.h"
#include "ctcompile/CTJS/IR/CTJSDialect.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"

#include <cstdio>
#include <string>

namespace {
namespace ctjs = ctcompile::ctjs;
using ctcompile::ctnative::HostContract;
using ctcompile::ctnative::HostContractAnalysis;
using ctcompile::ctnative::hostContractFingerprint;

constexpr const char * fixture = R"MLIR(
module {
  ctjs.func @script$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %publish = ctjs.create_closure %callee[1] this %u
    %host = ctjs.create_object
    ctjs.store_global "host", %host
    %invoked = ctjs.call_direct @publish$1(%u, %u, %publish, %host)
    %alias = ctjs.load_global "host"
    %key = ctjs.constant #ctjs.string<"slot">
    %read = ctjs.get_property %alias[%key]
    ctjs.store_global "trace", %read
    ctjs.return %u
  }
  ctjs.func private @publish$1(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %host: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %key = ctjs.constant #ctjs.string<"slot">
    %value = ctjs.constant #ctjs.number<4631107791820423168>
    ctjs.set_property %host[%key], %value
    ctjs.return %u
  }
}
)MLIR";

int failures = 0;
void check(bool value, const char * message) {
    if (value) { return; }
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}

HostContract contractFor(mlir::ModuleOp module) {
    HostContract contract;
    contract.moduleSha256 = hostContractFingerprint(module);
    contract.entry = "script$0";
    contract.roots = {{"host", {"slot"}}};
    contract.observations = {"trace"};
    return contract;
}

} // namespace

int main() {
    mlir::MLIRContext context;
    context.getOrLoadDialect<ctjs::CTJSDialect>();
    auto module = mlir::parseSourceString<mlir::ModuleOp>(fixture, &context);
    check(static_cast<bool>(module), "publication fixture parses");
    if (!module) { return 1; }
    auto contract = contractFor(*module);
    HostContractAnalysis analysis(*module, contract);
    check(analysis.proved(), "closed publication across a direct call is proved");
    if (!analysis.proved()) { std::fprintf(stderr, "%s\n", analysis.reason().str().c_str()); }
    ctjs::GetPropertyOp read;
    ctjs::SetPropertyOp write;
    module->walk([&](ctjs::GetPropertyOp operation) { read = operation; });
    module->walk([&](ctjs::SetPropertyOp operation) { write = operation; });
    const auto * edge = analysis.property(read);
    check(edge && edge->write == write,
          "live query follows the helper's write to the entry's read");
    check(analysis.observations().size() == 1, "only the declared observation is a root");

    mlir::Builder builder(&context);
    (*module)->setAttr("ctnative.host_proved", builder.getBoolAttr(true));
    (*module)->setAttr("ctnative.host_slot", builder.getStringAttr("forged"));
    check(hostContractFingerprint(*module) == contract.moduleSha256,
          "printed proof claims do not become bound semantic input");
    HostContractAnalysis rerun(*module, contract);
    check(rerun.proved() && rerun.property(read), "a repeated live analysis rederives slot flow");

    HostContractAnalysis limited(*module, contract, 0);
    check(!limited.proved() && !limited.property(read) && limited.reason().contains("budget"),
          "work exhaustion withholds every usable edge");

    mlir::OpBuilder insertion(read);
    auto bad = ctjs::CallOp::create(insertion, read.getLoc(), read.getType(), read.getObject(),
                                    read.getObject(), mlir::ValueRange{});
    HostContractAnalysis stale(*module, contract);
    check(!stale.proved() && !stale.property(read) && stale.reason().contains("fingerprint"),
          "the original contract cannot authorize changed program/driver IR");
    auto changedContract = contractFor(*module);
    HostContractAnalysis unsafe(*module, changedContract);
    check(!unsafe.proved() && !unsafe.property(read),
          "a fresh manifest and forged success tags cannot authorize an unknown call");
    bad.erase();
    contract.observations = {"missing"};
    HostContractAnalysis missing(*module, contract);
    check(!missing.proved() && missing.reason().contains("observation"),
          "a missing declared output root refuses the contract");
    if (failures == 0) { std::puts("host contract live proof queries passed"); }
    return failures == 0 ? 0 : 1;
}
