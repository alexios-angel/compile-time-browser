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

void checkCapturedParameters(mlir::MLIRContext & context, const std::string & shared) {
    auto source =
        replaced(shared, "@put$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value)",
                 "@put$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, "
                 "%entryKey: !ctjs.value, %value: !ctjs.value)");
    source = replaced(source, "    %entryKey = ctjs.constant #ctjs.string<\"x\">\n", "");
    source = replaced(source, "    %value = ctjs.constant #ctjs.number<4607182418800017408>\n", "");
    source = replaced(source, "    %putResult = ctjs.call %putter(%owned)", R"MLIR(
    %actualKey = ctjs.constant #ctjs.string<"x">
    %actualValue = ctjs.constant #ctjs.number<4607182418800017408>
    %putResult = ctjs.call %putter(%owned, %actualKey, %actualValue)
    %nextKey = ctjs.constant #ctjs.string<"y">
    %nextValue = ctjs.constant #ctjs.number<4611686018427387904>
    %putterAgain = ctjs.get_property %owned[%putKey]
    %putAgain = ctjs.call %putterAgain(%owned, %nextKey, %nextValue)
)MLIR");
    const auto prepare = [](std::string text) {
        for (unsigned index : {2u, 3u}) {
            const auto name = index == 2 ? "get$2" : "put$3";
            text = replaced(text, "captures %cell", "captures %state");
            text = replaced(text,
                            std::string("@") + name +
                                "(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value",
                            std::string("@") + name +
                                "(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, "
                                "%state: !ctjs.value");
            text = replaced(text,
                            "attributes {upvalue_count = 1 : i32} {\n"
                            "    %state = ctjs.load_upvalue %callee[0]\n",
                            "attributes {upvalue_count = 0 : i32} {\n");
        }
        text = replaced(text, "%putResult = ctjs.call %putter(%owned, %actualKey, %actualValue)",
                        "%putEnvironment = ctjs.load_upvalue %putter[0]\n"
                        "    %putResult = ctjs.call_direct @put$3(%owned, %u, %putter, "
                        "%putEnvironment, %actualKey, %actualValue)");
        text = replaced(text, "%putAgain = ctjs.call %putterAgain(%owned, %nextKey, %nextValue)",
                        "%nextEnvironment = ctjs.load_upvalue %putterAgain[0]\n"
                        "    %putAgain = ctjs.call_direct @put$3(%owned, %u, %putterAgain, "
                        "%nextEnvironment, %nextKey, %nextValue)");
        return replaced(text, "%answer = ctjs.call %getter(%owned)",
                        "%getEnvironment = ctjs.load_upvalue %getter[0]\n"
                        "    %answer = ctjs.call_direct @get$2(%owned, %u, %getter, "
                        "%getEnvironment)");
    };
    const auto query = [&](const std::string & program, bool prepared, bool expected,
                           const char * message) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(prepared ? prepare(program) : program,
                                                              &context);
        check(static_cast<bool>(module), "two-parameter source and prepared host fixture parses");
        if (!module) { return; }
        auto requested = contractFor(*module);
        requested.initialIntrinsics = {"Map"};
        HostContractAnalysis result(*module, requested);
        check(result.proved() == expected && !result.exhausted(), message);
        check(hostContractFingerprint(*module) == requested.moduleSha256,
              "parameter classification preserves the live SSA source");
        if (!expected) {
            check(result.callables().empty(), "failed actual proof exposes no callable family");
            module->walk([&](mlir::Operation * operation) {
                check(!result.callable(operation), "failed actual exposes no callable lookup");
                if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
                    check(!result.property(read), "failed actual exposes no slot lookup");
                }
            });
            return;
        }
        if (!result.proved()) {
            std::fprintf(stderr, "parameter host: %s\n", result.reason().str().c_str());
            return;
        }
        check(result.callables().size() == 3,
              "both setter calls and the zero-argument getter remain");
        auto setter = module->lookupSymbol<ctjs::FuncOp>("put$3");
        auto getter = module->lookupSymbol<ctjs::FuncOp>("get$2");
        const auto stringTag = mlir::TypeID::get<ctjs::StringAttr>();
        const auto numberTag = mlir::TypeID::get<ctjs::NumberAttr>();
        unsigned setters = 0;
        mlir::Value previousKey, previousValue;
        for (const auto & edge : result.callables()) {
            check(edge.capturedMap && edge.capturedMap->parameters.size() == 2,
                  "every call retains the complete per-method primitive parameter family");
            if (!edge.capturedMap || edge.capturedMap->parameters.size() != 2) { continue; }
            check(static_cast<bool>(edge.capturedMap->argument) == prepared &&
                      edge.capturedMap->parameters.front().function == getter &&
                      edge.capturedMap->parameters.front().primitiveTags.empty() &&
                      edge.capturedMap->parameters.back().function == setter &&
                      edge.capturedMap->parameters.back().primitiveTags ==
                          std::vector{stringTag, numberTag},
                  "the getter remains zero-argument and setter tags retain formal order");
            if (edge.function == getter) {
                check(edge.arguments.empty(), "the Map environment is not a getter parameter");
                continue;
            }
            ++setters;
            check(edge.function == setter && edge.arguments.size() == 2,
                  "the setter retains both independently proved actual operands");
            if (edge.arguments.size() != 2) { continue; }
            auto & body = setter.getBody().front();
            check(edge.arguments[0].parameter == body.getArgument(prepared ? 4 : 3) &&
                      edge.arguments[1].parameter == body.getArgument(prepared ? 5 : 4) &&
                      edge.arguments[0].actual == edge.call->getOperand(prepared ? 4 : 2) &&
                      edge.arguments[1].actual == edge.call->getOperand(prepared ? 5 : 3) &&
                      edge.arguments[0].primitiveTag == stringTag &&
                      edge.arguments[1].primitiveTag == numberTag &&
                      edge.arguments[0].actual != previousKey &&
                      edge.arguments[1].actual != previousValue,
                  "each call keeps its own key and payload SSA values after the capture offset");
            previousKey = edge.arguments[0].actual;
            previousValue = edge.arguments[1].actual;
        }
        check(setters == 2, "current calls agree on tags without sharing their actual values");
    };
    const auto global = replaced(source, "%actualKey = ctjs.constant #ctjs.string<\"x\">",
                                 "%keyValue = ctjs.constant #ctjs.string<\"x\">\n"
                                 "    ctjs.store_global \"key\", %keyValue\n"
                                 "    %actualKey = ctjs.load_global \"key\"");
    for (bool prepared : {false, true}) {
        query(source, prepared, true,
              "two primitive formals retain ordered source/prepared evidence");
        query(global, prepared, true, "a sole definite global initialization proves an actual tag");
        query(replaced(global,
                       "    ctjs.store_global \"key\", %keyValue\n"
                       "    %actualKey = ctjs.load_global \"key\"",
                       "    %actualKey = ctjs.load_global \"key\"\n"
                       "    ctjs.store_global \"key\", %keyValue"),
              prepared, false, "a later initialization cannot type an already loaded actual");
        query(replaced(global, "    %actualKey = ctjs.load_global \"key\"",
                       "    ctjs.store_global \"key\", %keyValue\n"
                       "    %actualKey = ctjs.load_global \"key\""),
              prepared, false, "multiple global stores cannot inherit a unique primitive actual");
        query(replaced(global, "    ctjs.store_global \"trace\", %answer",
                       "    ctjs.store_global \"key\", %nextKey\n"
                       "    ctjs.store_global \"trace\", %answer"),
              prepared, false,
              "even a later same-tag reassignment withholds global actual evidence");
        query(replaced(source, "#ctjs.number<4611686018427387904>", "#ctjs.boolean<true>"),
              prepared, false,
              "a second formal tag mismatch cannot reuse the first call's evidence");
    }
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
    const auto query = [&](const std::string & program, bool expected, unsigned reads = 1,
                           unsigned calls = 0, unsigned upvalues = 1, unsigned members = 1,
                           unsigned visibleCalls = 1) {
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
            check(result.callables().size() == visibleCalls &&
                      result.callables().front().capturedMap,
                  "host callable edge includes the exact captured Map source graph");
            if (result.callables().size() != visibleCalls ||
                !result.callables().front().capturedMap) {
                return;
            }
            auto capture = *result.callables().front().capturedMap;
            check(capture.intrinsic && capture.allocation && capture.cell &&
                      capture.initialization && capture.closures.size() == members &&
                      capture.upvalues.size() == upvalues && capture.reads.size() == reads &&
                      capture.calls.size() == calls && !capture.argument &&
                      capture.initialization.getValue() == capture.allocation.getResult() &&
                      capture.reads.front().getObject() == capture.upvalues.front().getResult(),
                  "source capture retains the immutable binding and every live Map effect");
            for (const auto & edge : result.callables()) {
                check(edge.capturedMap && edge.capturedMap->closures == capture.closures &&
                          edge.capturedMap->allocation == capture.allocation &&
                          edge.capturedMap->upvalues == capture.upvalues &&
                          edge.capturedMap->reads == capture.reads &&
                          edge.capturedMap->calls == capture.calls,
                      "every current call proves the same complete captured Map family");
            }
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

    auto shared = replaced(source, "    ctjs.return %table",
                           "    %putter = ctjs.create_closure %callee[3] this %u captures %cell\n"
                           "    %putKey = ctjs.constant #ctjs.string<\"put\">\n"
                           "    ctjs.set_property %table[%putKey], %putter\n"
                           "    ctjs.return %table");
    shared = replaced(shared, "\n}\n", R"MLIR(
  ctjs.func private @put$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 1 : i32} {
    %state = ctjs.load_upvalue %callee[0]
    %setKey = ctjs.constant #ctjs.string<"set">
    %setter = ctjs.get_property %state[%setKey]
    %entryKey = ctjs.constant #ctjs.string<"x">
    %value = ctjs.constant #ctjs.number<4607182418800017408>
    %written = ctjs.call %setter(%state, %entryKey, %value)
    %u = ctjs.constant #ctjs.undefined
    ctjs.return %u
  }
}
)MLIR");
    // Even an uncalled sibling can observe and mutate the same retained Map;
    // its complete body must be checked before exposing the getter edge.
    query(shared, true, 2, 1, 2, 2);
    query(replaced(shared, "ctjs.call %setter(%state, %entryKey, %value)",
                   "ctjs.call %setter(%state, %entryKey, %state)"),
          false);
    query(replaced(shared, "    %written = ctjs.call %setter(%state, %entryKey, %value)",
                   "    ctjs.store_global \"leaked\", %state\n"
                   "    %written = ctjs.call %setter(%state, %entryKey, %value)"),
          false);
    query(replaced(shared, "%putter = ctjs.create_closure %callee[3] this %u captures %cell",
                   "%putter = ctjs.create_closure %callee[2] this %u captures %cell"),
          false);
    shared = replaced(shared, "    %answer = ctjs.call %getter(%owned)",
                      "    %putKey = ctjs.constant #ctjs.string<\"put\">\n"
                      "    %putter = ctjs.get_property %owned[%putKey]\n"
                      "    %putResult = ctjs.call %putter(%owned)\n"
                      "    %answer = ctjs.call %getter(%owned)");
    query(shared, true, 2, 1, 2, 2, 2);
    query(replaced(shared, "ctjs.call %putter(%owned)", "ctjs.call %putter(%host)"), false);
    query(replaced(shared, "ctjs.call %putter(%owned)", "ctjs.call %putter(%owned, %u)"), false);
    auto sharedModule = mlir::parseSourceString<mlir::ModuleOp>(shared, &context);
    check(static_cast<bool>(sharedModule), "shared captured Map host fixture parses");
    if (sharedModule) {
        auto requested = contractFor(*sharedModule);
        requested.initialIntrinsics = {"Map"};
        HostContractAnalysis complete(*sharedModule, requested);
        check(complete.proved(), "shared host census completes before work-budget controls");
        if (complete.proved()) {
            const unsigned completion = complete.steps();
            check(completion < 10000, "shared host work stays within the bounded fixture limit");
            if (completion < 10000) {
                for (unsigned budget = 0; budget < completion; ++budget) {
                    HostContractAnalysis limited(*sharedModule, requested, budget);
                    check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                              limited.callables().empty(),
                          "every incomplete family budget withholds all current callable edges");
                }
                check(HostContractAnalysis(*sharedModule, requested, completion).proved(),
                      "exact shared host completion budget reproduces the full family");
            }
            auto mutation = complete.callables().front().capturedMap->calls.front();
            const auto originalValue = mutation.getArgs().back();
            mutation->setOperand(3, mutation.getReceiver());
            HostContractAnalysis stale(*sharedModule, requested);
            check(!stale.proved() && stale.reason().contains("fingerprint"),
                  "a sibling mutation invalidates the original source fingerprint");
            requested.moduleSha256 = hostContractFingerprint(*sharedModule);
            HostContractAnalysis changed(*sharedModule, requested);
            check(!changed.proved() && changed.callables().empty(),
                  "a fresh fingerprint cannot hide a sibling Map cycle");
            mutation->setOperand(3, originalValue);
            requested.moduleSha256 = hostContractFingerprint(*sharedModule);
            check(HostContractAnalysis(*sharedModule, requested).proved(),
                  "restoring the sibling body restores the independently proved family");
        }
    }
    checkCapturedParameters(context, shared);

    constexpr llvm::StringLiteral set = R"MLIR(
    %entryKey = ctjs.constant #ctjs.string<"x">
    %value = ctjs.constant #ctjs.number<4607182418800017408>
    %setKey = ctjs.constant #ctjs.string<"set">
    %setter = ctjs.get_property %state[%setKey]
    %written = ctjs.call %setter(%state, %entryKey, %value)
)MLIR";
    const auto mutated = replaced(source, "    %key = ctjs.constant #ctjs.string<\"size\">",
                                  set.str() + "    %key = ctjs.constant #ctjs.string<\"size\">");
    query(mutated, true, 2, 1);
    for (const char * primitive :
         {"#ctjs.string<\"payload\">", "#ctjs.boolean<true>", "#ctjs.null", "#ctjs.undefined"}) {
        query(replaced(mutated, "#ctjs.number<4607182418800017408>", primitive), true, 2, 1);
    }
    const auto aliases = replaced(mutated, "%answer = ctjs.get_property %state[%key]",
                                  "%again = ctjs.load_upvalue %callee[0]\n"
                                  "    %answer = ctjs.get_property %again[%key]");
    query(aliases, true, 2, 1, 2);
    query(replaced(mutated, "%answer = ctjs.get_property %state[%key]",
                   "%answer = ctjs.get_property %written[%key]"),
          true, 2, 1);
    for (const char * method : {"get", "has", "delete"}) {
        const auto effects =
            replaced(mutated, "    %key = ctjs.constant #ctjs.string<\"size\">",
                     std::string("    %probeKey = ctjs.constant #ctjs.string<\"") + method +
                         "\">\n"
                         "    %probeMethod = ctjs.get_property %state[%probeKey]\n"
                         "    %probe = ctjs.call %probeMethod(%state, %entryKey)\n"
                         "    %key = ctjs.constant #ctjs.string<\"size\">");
        query(effects, true, 3, 2);
        // Host ownership proves primitive contents, not a native get result
        // carrier or a constant answer after previous invocations.
        query(replaced(effects, "ctjs.return %answer", "ctjs.return %probe"), true, 3, 2);
        query(replaced(effects, "    %key = ctjs.constant #ctjs.string<\"size\">",
                       "    %saved = ctjs.call %setter(%state, %probe, %probe)\n"
                       "    %key = ctjs.constant #ctjs.string<\"size\">"),
              true, 3, 3);
    }
    const auto growing =
        replaced(mutated, "%written = ctjs.call %setter(%state, %entryKey, %value)",
                 "%sizeKey = ctjs.constant #ctjs.string<\"size\">\n"
                 "    %before = ctjs.get_property %state[%sizeKey]\n"
                 "    %written = ctjs.call %setter(%state, %before, %value)");
    query(growing, true, 3, 1);
    query(replaced(mutated, "ctjs.call %setter(%state, %entryKey, %value)",
                   "ctjs.call %setter(%state, %entryKey)"),
          false);
    query(replaced(mutated, "ctjs.call %setter(%state, %entryKey, %value)",
                   "ctjs.call %setter(%this, %entryKey, %value)"),
          false);
    query(replaced(mutated, "ctjs.call %setter(%state, %entryKey, %value)",
                   "ctjs.call %setter(%state, %state, %value)"),
          false);
    query(replaced(mutated, "ctjs.call %setter(%state, %entryKey, %value)",
                   "ctjs.call %setter(%state, %entryKey, %state)"),
          false);
    query(replaced(mutated, "ctjs.call %setter(%state, %entryKey, %value)",
                   "ctjs.call %setter(%state, %entryKey, %setter)"),
          false);
    query(replaced(mutated, "ctjs.return %answer", "ctjs.return %written"), false);
    query(replaced(mutated, "ctjs.return %answer", "ctjs.return %setter"), false);
    query(replaced(mutated, "ctjs.return %answer",
                   "ctjs.store_global \"escape\", %written\n    ctjs.return %answer"),
          false);
    query(replaced(mutated, "ctjs.return %answer",
                   "ctjs.set_property %state[%setKey], %setter\n    ctjs.return %answer"),
          false);

    auto module = mlir::parseSourceString<mlir::ModuleOp>(mutated, &context);
    check(static_cast<bool>(module), "live Map effect mutation fixture parses");
    if (!module) { return; }
    auto contract = contractFor(*module);
    contract.initialIntrinsics = {"Map"};
    HostContractAnalysis original(*module, contract);
    check(original.proved() && original.callables().size() == 1,
          "live effect mutation starts with a complete captured source proof");
    if (!original.proved() || original.callables().size() != 1) { return; }
    auto call = original.callables().front().call;
    auto capture = *original.callables().front().capturedMap;
    const unsigned completion = original.steps();
    for (unsigned budget = 0; budget < completion; ++budget) {
        HostContractAnalysis limited(*module, contract, budget);
        check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                  limited.callables().empty() && !limited.callable(call),
              "every incomplete effect census withholds all captured callable edges");
    }
    mlir::Builder builder(&context);
    auto setCall = capture.calls.front();
    setCall->setAttr("ctnative.map_action", builder.getStringAttr("get"));
    setCall->setAttr("ctnative.host_proved", builder.getBoolAttr(true));
    // Map annotations participate in the fingerprint, unlike host reports.
    // A fresh contract still cannot let them replace the live source census.
    contract.moduleSha256 = hostContractFingerprint(*module);
    HostContractAnalysis rerun(*module, contract);
    check(rerun.proved() && rerun.callable(call) &&
              rerun.callable(call)->capturedMap->calls.front() == setCall,
          "native effect annotations cannot replace the current source operation census");
    setCall->setOperand(3, capture.upvalues.front().getResult());
    HostContractAnalysis stale(*module, contract);
    check(!stale.proved() && stale.reason().contains("fingerprint") && stale.callables().empty(),
          "changing a Map write cannot reuse an earlier host fingerprint");
    contract.moduleSha256 = hostContractFingerprint(*module);
    HostContractAnalysis cyclic(*module, contract);
    check(!cyclic.proved() && cyclic.callables().empty() && !cyclic.callable(call),
          "a fresh fingerprint and forged effect markers cannot authorize a Map cycle");
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
