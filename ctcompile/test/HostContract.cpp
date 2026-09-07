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

constexpr const char * callableFixture = R"MLIR(
module {
  ctjs.func @script$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %make = ctjs.create_closure %callee[1] this %u
    %host = ctjs.create_object
    ctjs.store_global "host", %host
    %table = ctjs.call_direct @make$1(%u, %u, %make)
    %slot = ctjs.constant #ctjs.string<"slot">
    ctjs.set_property %host[%slot], %table
    %alias = ctjs.load_global "host"
    %owned = ctjs.get_property %alias[%slot]
    %key = ctjs.constant #ctjs.string<"get">
    %getter = ctjs.get_property %owned[%key]
    %answer = ctjs.call %getter(%owned)
    ctjs.store_global "trace", %answer
    ctjs.return %u
  }
  ctjs.func private @make$1(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %getter = ctjs.create_closure %callee[2] this %u
    %table = ctjs.create_object
    %key = ctjs.constant #ctjs.string<"get">
    ctjs.set_property %table[%key], %getter
    ctjs.return %table
  }
  ctjs.func private @get$2(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %answer = ctjs.constant #ctjs.number<4631107791820423168>
    ctjs.return %answer
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

std::string replaced(std::string source, llvm::StringRef from, llvm::StringRef to) {
    const auto offset = source.find(from.str());
    if (offset == std::string::npos) { return {}; }
    source.replace(offset, from.size(), to.str());
    return source;
}

void checkCallables(mlir::MLIRContext & context) {
    auto module = mlir::parseSourceString<mlir::ModuleOp>(callableFixture, &context);
    check(static_cast<bool>(module), "uncaptured exported getter fixture parses");
    if (!module) { return; }
    const auto contract = contractFor(*module);
    ctjs::CallOp call;
    ctjs::GetPropertyOp rootRead;
    ctjs::CreateClosureOp closure;
    module->walk([&](ctjs::CallOp operation) { call = operation; });
    module->walk([&](ctjs::GetPropertyOp operation) {
        if (operation.getObject().getDefiningOp<ctjs::LoadGlobalOp>()) { rootRead = operation; }
    });
    module->walk([&](ctjs::CreateClosureOp operation) {
        if (operation.getFunction() == 2) { closure = operation; }
    });
    HostContractAnalysis query(*module, contract);
    check(query.proved() && query.callables().size() == 1,
          "complete environment proof includes the current exported getter call");
    if (!query.proved()) { std::fprintf(stderr, "%s\n", query.reason().str().c_str()); }
    const auto * edge = query.callable(call);
    auto actualRead = edge ? edge->read : ctjs::GetPropertyOp{};
    auto actualWrite = edge ? edge->write : ctjs::SetPropertyOp{};
    check(edge && actualRead.getResult() == call.getCallee() && edge->closure == closure &&
              actualWrite.getValue() == closure.getResult() &&
              edge->function == module->lookupSymbol<ctjs::FuncOp>("get$2") &&
              query.property(rootRead),
          "call evidence retains the actual read, preceding write, closure and source body");
    check(hostContractFingerprint(*module) == contract.moduleSha256,
          "callee discovery leaves the fingerprinted source unchanged");
    const unsigned steps = query.steps();
    check(steps > 0 && steps < 10000, "callable proof charges a bounded traversal");
    for (unsigned budget = 0; budget < steps; ++budget) {
        HostContractAnalysis limited(*module, contract, budget);
        check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                  limited.callables().empty() && !limited.callable(call) &&
                  !limited.property(rootRead),
              "every incomplete budget atomically withholds slot and callable evidence");
    }
    HostContractAnalysis exact(*module, contract, steps);
    check(exact.proved() && exact.callable(call) && exact.steps() == steps,
          "the exact charged completion budget reproduces the callable proof");
    mlir::Builder builder(&context);
    (*module)->setAttr("ctnative.host_proved", builder.getBoolAttr(true));
    (*module)->setAttr("ctnative.host_callables", builder.getStringAttr("forged"));
    HostContractAnalysis rerun(*module, contract);
    check(rerun.proved() && rerun.callable(call) && rerun.callable(call)->closure == closure,
          "forged callable reports are ignored and the current source closure is rederived");

    auto getter = module->lookupSymbol<ctjs::FuncOp>("get$2");
    auto returned = llvm::cast<ctjs::ReturnOp>(getter.getBody().front().getTerminator());
    mlir::OpBuilder at(returned);
    auto mutation =
        ctjs::StoreGlobalOp::create(at, returned.getLoc(), "sideEffect", returned.getValue());
    HostContractAnalysis stale(*module, contract);
    check(!stale.proved() && stale.reason().contains("fingerprint") && stale.callables().empty() &&
              !stale.callable(call),
          "a late semantic mutation cannot reuse the earlier manifest");
    HostContractAnalysis effectful(*module, contractFor(*module));
    check(!effectful.proved() && !effectful.callable(call) && !effectful.property(rootRead),
          "a fresh manifest and forged reports cannot authorize an effectful getter");
    mutation.erase();

    const auto checkVariant = [&](llvm::StringRef from, llvm::StringRef to, bool expected,
                                  const char * message) {
        auto variant =
            mlir::parseSourceString<mlir::ModuleOp>(replaced(callableFixture, from, to), &context);
        check(static_cast<bool>(variant), "callable variant parses");
        if (!variant) { return; }
        HostContractAnalysis result(*variant, contractFor(*variant));
        check(result.proved() == expected &&
                  (expected ? result.callables().size() == 1 : result.callables().empty()),
              message);
    };
    checkVariant("ctjs.call %getter(%owned)", "ctjs.call_direct @get$2(%owned, %u, %getter)", true,
                 "a direct property call requires the same exact current closure");
    checkVariant("ctjs.call %getter(%owned)", "ctjs.call_direct @make$1(%owned, %u, %getter)",
                 false, "a forged direct symbol cannot override the actual stored closure");
    checkVariant("ctjs.call %getter(%owned)", "ctjs.call_direct @get$2(%owned, %owned, %getter)",
                 false, "a property call cannot forge a constructor new-target");
    checkVariant("ctjs.call %getter(%owned)", "ctjs.call %getter(%host)", false,
                 "an unproved effective receiver does not get inferred from the target body");
    checkVariant("ctjs.call %getter(%owned)", "ctjs.call %getter(%owned, %u)", false,
                 "argument windows remain outside the first getter slice");
    checkVariant("ctjs.set_property %table[%key], %getter", "ctjs.set_property %table[%key], %u",
                 false, "a primitive field replacement cannot keep old callable evidence");
    checkVariant("ctjs.return %answer", "ctjs.return %this", false,
                 "a receiver observation cannot inherit the literal-getter proof");
    checkVariant("%getter = ctjs.create_closure %callee[2]",
                 "%getter = ctjs.create_closure %callee[9]", false,
                 "an unknown numeric source identity refuses callable evidence");
    checkVariant("%getter = ctjs.create_closure %callee[2] this %u",
                 "%getter = ctjs.create_closure %callee[2] this %this", false,
                 "a captured lexical receiver is outside the uncaptured getter slice");
    checkVariant("%getter = ctjs.create_closure %callee[2] this %u",
                 "%getter = ctjs.create_closure %u[2] this %u", false,
                 "a numeric index cannot prove the source program of a forged enclosing closure");
    checkVariant("%make = ctjs.create_closure %callee[1] this %u",
                 "%make = ctjs.create_closure %u[1] this %u", false,
                 "the factory also requires its creating activation's source program identity");
    checkVariant("ctjs.set_property %table[%key], %getter",
                 "ctjs.set_property %table[%key], %getter\n"
                 "    ctjs.store_global \"escapedGetter\", %getter",
                 false, "the stored closure cannot acquire an additional exported alias");
    std::printf("host callable proof: %u charged steps and incomplete budgets checked\n", steps);
}

void checkCapturedCallables(mlir::MLIRContext & context) {
    auto source = replaced(callableFixture, "%getter = ctjs.create_closure %callee[2] this %u",
                           "%cell = ctjs.create_cell %u\n"
                           "    %constructor = ctjs.load_global \"Map\"\n"
                           "    %state = ctjs.construct %constructor(%constructor)\n"
                           "    ctjs.cell_set %cell, %state\n"
                           "    %getter = ctjs.create_closure %callee[2] this %u captures %cell");
    source = replaced(source,
                      "attributes {upvalue_count = 0 : i32} {\n"
                      "    %answer = ctjs.constant #ctjs.number<4631107791820423168>",
                      "attributes {upvalue_count = 1 : i32} {\n"
                      "    %state = ctjs.load_upvalue %callee[0]\n"
                      "    %key = ctjs.constant #ctjs.string<\"size\">\n"
                      "    %answer = ctjs.get_property %state[%key]");
    const auto query = [&](const std::string & program, bool expected) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(program, &context);
        check(static_cast<bool>(module), "captured host getter fixture parses");
        if (!module) { return; }
        auto contract = contractFor(*module);
        contract.initialIntrinsics = {"Map"};
        HostContractAnalysis result(*module, contract);
        check(result.proved() == expected && !result.exhausted(),
              "captured getter host proof follows the complete current source environment");
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "captured getter evidence does not rewrite source operations");
        if (expected && result.proved()) {
            check(result.callables().size() == 1 && result.callables().front().capturedMap,
                  "host callable edge includes the exact captured Map source graph");
            if (result.callables().size() != 1 || !result.callables().front().capturedMap) {
                return;
            }
            auto capture = *result.callables().front().capturedMap;
            check(capture.intrinsic && capture.allocation && capture.cell &&
                      capture.initialization && capture.upvalue && capture.size &&
                      !capture.argument &&
                      capture.initialization.getValue() == capture.allocation.getResult() &&
                      capture.size.getObject() == capture.upvalue.getResult(),
                  "source capture retains allocation, immutable binding and size-read identity");
        } else if (!expected) {
            check(result.callables().empty(), "failed environment publishes no captured calls");
            module->walk([&](mlir::Operation * operation) {
                check(!result.callable(operation), "failed capture exposes no usable call lookup");
                if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
                    check(!result.property(read), "failed capture exposes no usable slot lookup");
                }
            });
        }
    };
    query(source, true);
    query(replaced(source, "ctjs.call %getter(%owned)",
                   "ctjs.call_direct @get$2(%owned, %u, %getter)"),
          true);
    query(replaced(source, "ctjs.call %getter(%owned)", "ctjs.call %getter(%host)"), false);
    query(replaced(source, "ctjs.load_upvalue %callee[0]", "ctjs.load_upvalue %callee[1]"), false);
    query(replaced(source, "ctjs.create_closure %callee[2]", "ctjs.create_closure %u[2]"), false);
    query(replaced(source, "ctjs.cell_set %cell, %state",
                   "ctjs.cell_set %cell, %state\n    ctjs.cell_set %cell, %u"),
          false);
    query(replaced(source, "ctjs.store_global \"trace\", %answer",
                   "ctjs.store_global \"trace\", %answer\n"
                   "    %external = ctjs.load_global \"external\""),
          false);
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
    auto invalidIntrinsic = contractFor(*module);
    invalidIntrinsic.initialIntrinsics = {"unknown"};
    HostContractAnalysis invalidProvider(*module, invalidIntrinsic);
    check(!invalidProvider.proved() && !invalidProvider.property(read),
          "typed API cannot bypass supported initial intrinsic identities");
    auto invalidRealm = contractFor(*module);
    invalidRealm.realmOwnDataProperties = {"slot"};
    HostContractAnalysis orphanRealmSlots(*module, invalidRealm);
    check(!orphanRealmSlots.proved() && !orphanRealmSlots.property(read),
          "typed API cannot supply realm slots without the entry receiver");
    invalidRealm.classicScriptRealm = true;
    invalidRealm.realmOwnDataProperties = {"__proto__"};
    HostContractAnalysis prototypeRealmSlot(*module, invalidRealm);
    check(!prototypeRealmSlot.proved() && !prototypeRealmSlot.property(read),
          "typed API cannot turn a prototype operation into a realm data slot");
    invalidRealm.realmOwnDataProperties = {"slot"};
    invalidRealm.absentBindings = {"slot"};
    HostContractAnalysis conflictingRealmSlot(*module, invalidRealm);
    check(!conflictingRealmSlot.proved() && !conflictingRealmSlot.property(read),
          "typed API rejects a writable realm slot declared absent");
    contract.observations = {"missing"};
    HostContractAnalysis missing(*module, contract);
    check(!missing.proved() && missing.reason().contains("observation"),
          "a missing declared output root refuses the contract");
    checkCallables(context);
    checkCapturedCallables(context);
    if (failures == 0) { std::puts("host contract live proof queries passed"); }
    return failures == 0 ? 0 : 1;
}
