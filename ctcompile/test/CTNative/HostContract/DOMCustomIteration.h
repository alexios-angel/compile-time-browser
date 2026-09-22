#pragma once

#include "HostContractFixtures.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"

namespace ctcompile::test::host_contract {

inline void checkDOMCustomIteration(mlir::MLIRContext & context) {
    using namespace ctcompile::ctnative;
    context.getOrLoadDialect<mlir::arith::ArithDialect>();
    context.getOrLoadDialect<mlir::scf::SCFDialect>();
    context.getOrLoadDialect<mlir::ub::UBDialect>();
    // Independent protocol IR: the next method reads and updates the captured
    // element before yielding it. Exhaustion must bypass the item observation.
    const std::string source = R"MLIR(
module {
  ctjs.func @custom$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %element: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %frame = ctjs.frame_enter 16
    %capture = ctjs.create_cell %element
    %holder = ctjs.create_object
    %undefined = ctjs.constant #ctjs.undefined
    %identity = ctjs.create_closure %callee[1] this %undefined
    %Symbol = ctjs.load_global "Symbol"
    %iteratorName = ctjs.constant #ctjs.string<"iterator">
    %iterator = ctjs.get_property %Symbol[%iteratorName]
    ctjs.set_property %holder[%iterator], %identity
    %step = ctjs.create_closure %callee[2] this %undefined captures %capture
    %nextName = ctjs.constant #ctjs.string<"next">
    ctjs.set_property %holder[%nextName], %step
    %finish = ctjs.create_closure %callee[3] this %undefined
    %returnName = ctjs.constant #ctjs.string<"return">
    ctjs.set_property %holder[%returnName], %finish
    %open = ctjs.load_global "__ctbrowser_for_of_open"
    %next = ctjs.load_global "__ctbrowser_iter_next"
    %close = ctjs.load_global "__ctbrowser_iter_close"
    %doneName = ctjs.constant #ctjs.string<"done">
    %attributeName = ctjs.constant #ctjs.string<"setAttribute">
    %visited = ctjs.constant #ctjs.string<"data-visited">
    %yes = ctjs.constant #ctjs.string<"yes">
    %zero = ctjs.constant #ctjs.number<0>
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %normal = ctjs.constant #ctjs.boolean<false>
    %record = ctjs.call %open(%undefined, %holder)
    %loop = scf.while (%count = %zero) : (!ctjs.value) -> !ctjs.value {
      %item = ctjs.call %next(%undefined, %record)
      %done = ctjs.get_property %record[%doneName]
      %test = ctjs.truthy %done
      %selected:2 = scf.if %test -> (i1, !ctjs.value) {
        %stop = arith.constant false
        scf.yield %stop, %count : i1, !ctjs.value
      } else {
        %attribute = ctjs.get_property %item[%attributeName]
        %written = ctjs.call %attribute(%item, %visited, %yes)
        %increment = ctjs.binary_static add %count, %one
        %again = arith.constant true
        scf.yield %again, %increment : i1, !ctjs.value
      }
      scf.condition(%selected#0) %selected#1 : !ctjs.value
    } do {
    ^bb0(%count: !ctjs.value):
      scf.yield %count : !ctjs.value
    }
    %closed = ctjs.call %close(%undefined, %record, %normal)
    ctjs.frame_exit %frame
    ctjs.return %loop
  }
  ctjs.func @identity$1(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %identityFrame = ctjs.frame_enter 1
    ctjs.frame_exit %identityFrame
    ctjs.return %this
  }
  ctjs.func @next$2(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 1 : i32} {
    %frame = ctjs.frame_enter 7
    %element = ctjs.load_upvalue %callee[0]
    %hasName = ctjs.constant #ctjs.string<"hasAttribute">
    %has = ctjs.get_property %element[%hasName]
    %yielded = ctjs.constant #ctjs.string<"data-yielded">
    %done = ctjs.call %has(%element, %yielded)
    %setName = ctjs.constant #ctjs.string<"setAttribute">
    %set = ctjs.get_property %element[%setName]
    %yes = ctjs.constant #ctjs.string<"yes">
    %written = ctjs.call %set(%element, %yielded, %yes)
    %result = ctjs.create_object
    %doneName = ctjs.constant #ctjs.string<"done">
    ctjs.set_property %result[%doneName], %done
    %valueName = ctjs.constant #ctjs.string<"value">
    ctjs.set_property %result[%valueName], %element
    ctjs.frame_exit %frame
    ctjs.return %result
  }
  ctjs.func @return$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %result = ctjs.create_object
    ctjs.return %result
  }
}
)MLIR";
    auto original = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
    check(static_cast<bool>(original), "independent custom iterator fixture parses");
    if (!original) { return; }
    const auto withoutReturnSource = replaced(
        replaced(source,
                 "    %finish = ctjs.create_closure %callee[3] this %undefined\n"
                 "    %returnName = ctjs.constant #ctjs.string<\"return\">\n"
                 "    ctjs.set_property %holder[%returnName], %finish\n",
                 ""),
        R"MLIR(  ctjs.func @return$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %result = ctjs.create_object
    ctjs.return %result
  }
)MLIR",
        "");
    auto withoutReturn = mlir::parseSourceString<mlir::ModuleOp>(withoutReturnSource, &context);
    check(static_cast<bool>(withoutReturn), "custom iterator with no return hook parses");
    if (!withoutReturn) { return; }
    // Independent break/exhaustion mux: each exit defines only its own count.
    // The other slot is poison, so retaining a prior count cannot prove it.
    const std::string countedLoop = R"MLIR(
    %poison = ub.poison : !ctjs.value
    %tagPoison = ub.poison : i32
    %normalTag = arith.constant 7 : i32
    %breakTag = arith.constant 11 : i32
    %hasName = ctjs.constant #ctjs.string<"hasAttribute">
    %has = ctjs.get_property %element[%hasName]
    %stopName = ctjs.constant #ctjs.string<"stop">
    %loop:4 = scf.while (%count = %zero, %normalCount = %poison, %breakCount = %poison, %tag = %tagPoison) : (!ctjs.value, !ctjs.value, !ctjs.value, i32) -> (!ctjs.value, !ctjs.value, !ctjs.value, i32) {
      %item = ctjs.call %next(%undefined, %record)
      %done = ctjs.get_property %record[%doneName]
      %test = ctjs.truthy %done
      %selected:5 = scf.if %test -> (i1, !ctjs.value, !ctjs.value, !ctjs.value, i32) {
        %stop = arith.constant false
        scf.yield %stop, %poison, %count, %poison, %normalTag : i1, !ctjs.value, !ctjs.value, !ctjs.value, i32
      } else {
        %attribute = ctjs.get_property %item[%attributeName]
        %written = ctjs.call %attribute(%item, %visited, %yes)
        %increment = ctjs.binary_static add %count, %one
        %stopping = ctjs.call %has(%element, %stopName)
        %breakTest = ctjs.truthy %stopping
        %branch:5 = scf.if %breakTest -> (i1, !ctjs.value, !ctjs.value, !ctjs.value, i32) {
          %stop = arith.constant false
          scf.yield %stop, %poison, %poison, %increment, %breakTag : i1, !ctjs.value, !ctjs.value, !ctjs.value, i32
        } else {
          %again = arith.constant true
          scf.yield %again, %increment, %poison, %poison, %normalTag : i1, !ctjs.value, !ctjs.value, !ctjs.value, i32
        }
        scf.yield %branch#0, %branch#1, %branch#2, %branch#3, %branch#4 : i1, !ctjs.value, !ctjs.value, !ctjs.value, i32
      }
      scf.condition(%selected#0) %selected#1, %selected#2, %selected#3, %selected#4 : !ctjs.value, !ctjs.value, !ctjs.value, i32
    } do {
    ^bb0(%count: !ctjs.value, %normalCount: !ctjs.value, %breakCount: !ctjs.value, %tag: i32):
      scf.yield %count, %normalCount, %breakCount, %tag : !ctjs.value, !ctjs.value, !ctjs.value, i32
    }
    %selector = arith.index_castui %loop#3 : i32 to index
    %answer = scf.index_switch %selector -> !ctjs.value
    case 7 {
      scf.yield %loop#1 : !ctjs.value
    }
    default {
      scf.yield %loop#2 : !ctjs.value
    }
)MLIR";
    const auto loopBegin = source.find("    %loop =");
    const auto loopEnd = source.find("    %closed =");
    const auto countedSource =
        replaced(source.substr(0, loopBegin) + countedLoop + source.substr(loopEnd),
                 "ctjs.return %loop", "ctjs.return %answer");
    const auto retaggedSource = replaced(
        replaced(countedSource, "%normalTag = arith.constant 7", "%normalTag = arith.constant 19"),
        "case 7 {", "case 19 {");
    auto counted = mlir::parseSourceString<mlir::ModuleOp>(countedSource, &context);
    auto retagged = mlir::parseSourceString<mlir::ModuleOp>(retaggedSource, &context);
    check(counted && retagged, "independent counted break and renamed exit tags parse");
    if (!counted || !retagged) { return; }
    // The exhaustion arm swaps two exit slots. Selecting and writing one
    // result at a time would overwrite the second result's original value.
    const std::string crossedLoop = R"MLIR(
    %poison = ub.poison : !ctjs.value
    %tagPoison = ub.poison : i32
    %normalTag = arith.constant 7 : i32
    %breakTag = arith.constant 11 : i32
    %extraInitial = ctjs.constant #ctjs.number<4619567317775286272>
    %extraStep = ctjs.constant #ctjs.number<4613937818241073152>
    %hasName = ctjs.constant #ctjs.string<"hasAttribute">
    %has = ctjs.get_property %element[%hasName]
    %stopName = ctjs.constant #ctjs.string<"stop">
    %loop:7 = scf.while (%count = %zero, %extra = %extraInitial, %normalCount = %poison, %normalExtra = %poison, %breakCount = %poison, %breakExtra = %poison, %tag = %tagPoison) : (!ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, i32) -> (!ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, i32) {
      %item = ctjs.call %next(%undefined, %record)
      %done = ctjs.get_property %record[%doneName]
      %test = ctjs.truthy %done
      %selected:8 = scf.if %test -> (i1, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, i32) {
        %stop = arith.constant false
        scf.yield %stop, %poison, %poison, %count, %extra, %poison, %poison, %normalTag : i1, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, i32
      } else {
        %attribute = ctjs.get_property %item[%attributeName]
        %written = ctjs.call %attribute(%item, %visited, %yes)
        %increment = ctjs.binary_static add %count, %one
        %extraIncrement = ctjs.binary_static add %extra, %extraStep
        %stopping = ctjs.call %has(%element, %stopName)
        %breakTest = ctjs.truthy %stopping
        %branch:8 = scf.if %breakTest -> (i1, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, i32) {
          %stop = arith.constant false
          scf.yield %stop, %poison, %poison, %poison, %poison, %increment, %extraIncrement, %breakTag : i1, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, i32
        } else {
          %again = arith.constant true
          scf.yield %again, %increment, %extraIncrement, %poison, %poison, %poison, %poison, %normalTag : i1, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, i32
        }
        scf.yield %branch#0, %branch#1, %branch#2, %branch#3, %branch#4, %branch#5, %branch#6, %branch#7 : i1, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, i32
      }
      scf.condition(%selected#0) %selected#1, %selected#2, %selected#3, %selected#4, %selected#5, %selected#6, %selected#7 : !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, i32
    } do {
    ^bb0(%count: !ctjs.value, %extra: !ctjs.value, %normalCount: !ctjs.value, %normalExtra: !ctjs.value, %breakCount: !ctjs.value, %breakExtra: !ctjs.value, %tag: i32):
      scf.yield %count, %extra, %normalCount, %normalExtra, %breakCount, %breakExtra, %tag : !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, !ctjs.value, i32
    }
    %selector = arith.index_castui %loop#6 : i32 to index
    %exits:2 = scf.index_switch %selector -> !ctjs.value, !ctjs.value
    case 7 {
      scf.yield %loop#3, %loop#2 : !ctjs.value, !ctjs.value
    }
    default {
      scf.yield %loop#5, %loop#4 : !ctjs.value, !ctjs.value
    }
    %answer = ctjs.binary_static add %exits#0, %exits#1
)MLIR";
    const auto crossedSource =
        replaced(source.substr(0, loopBegin) + crossedLoop + source.substr(loopEnd),
                 "ctjs.return %loop", "ctjs.return %answer");
    const auto duplicatedSource = replaced(
        replaced(crossedSource, "scf.yield %loop#3, %loop#2", "scf.yield %loop#3, %loop#3"),
        "scf.yield %loop#5, %loop#4", "scf.yield %loop#5, %loop#5");
    auto crossed = mlir::parseSourceString<mlir::ModuleOp>(crossedSource, &context);
    auto duplicated = mlir::parseSourceString<mlir::ModuleOp>(duplicatedSource, &context);
    check(crossed && duplicated, "crossed and repeated two-result exit projections parse");
    if (!crossed || !duplicated) { return; }
    HostContract contract;
    contract.entry = "custom$0";
    contract.elementParameters = {0};
    contract.initialIntrinsics = {"Object", "Symbol", "__ctbrowser_for_of_open",
                                  "__ctbrowser_iter_next", "__ctbrowser_iter_close"};
    contract.moduleSha256 = hostContractFingerprint(*original);
    const auto noEvidence = [](mlir::ModuleOp input, const DOMEntryAnalysis & proof) {
        bool empty = !proof.proved() && !proof.entry() && !proof.wrapper() &&
                     proof.parameters().empty() && proof.callbacks().empty();
        input.walk([&](ctjs::CallOp call) { empty &= !proof.call(call); });
        input.walk([&](ctjs::GetPropertyOp read) {
            empty &= !proof.method(read) && !proof.isElementVectorIndex(read);
        });
        return empty;
    };
    constexpr unsigned completeBudget = 100000;
    for (auto fixture : {*original, *withoutReturn, *counted, *retagged, *crossed, *duplicated}) {
        for (auto provider :
             {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
            contract.provider = provider;
            contract.moduleSha256 = hostContractFingerprint(fixture);
            mlir::OwningOpRef<mlir::ModuleOp> input(fixture.clone());
            if (auto failure = normalizeDOMCustomIteration(*input, contract, completeBudget)) {
                check(false,
                      "custom protocol with or without return normalizes for both providers");
                std::fprintf(stderr, "%s\n", llvm::toString(std::move(failure)).c_str());
                continue;
            }
            bool protocolCall = false;
            input->walk([&](ctjs::CallOp call) {
                auto load = call.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
                protocolCall |= load && load.getName().starts_with("__ctbrowser_");
            });
            check(!protocolCall && !input->lookupSymbol<ctjs::FuncOp>("identity$1"),
                  "normalization retires protocol calls and the proved identity method");
            if (fixture == *crossed || fixture == *duplicated) {
                bool selected = false;
                input->walk([&](ctjs::BinaryStaticOp sum) {
                    if (sum.getKind() != ctjs::BinaryKind::Add) { return; }
                    auto lhs = llvm::dyn_cast<mlir::OpResult>(sum.getLhs());
                    auto rhs = llvm::dyn_cast<mlir::OpResult>(sum.getRhs());
                    auto loop = lhs ? llvm::dyn_cast<mlir::scf::WhileOp>(lhs.getOwner())
                                    : mlir::scf::WhileOp{};
                    if (!loop || !rhs || rhs.getOwner() != loop ||
                        loop.getBeforeArguments().size() < 2) {
                        return;
                    }
                    auto condition =
                        llvm::cast<mlir::scf::ConditionOp>(loop.getBefore().front().back());
                    const auto normalValue = [&](mlir::OpResult result) -> mlir::Value {
                        auto value = llvm::dyn_cast<mlir::OpResult>(
                            condition.getArgs()[result.getResultNumber()]);
                        auto branch = value ? llvm::dyn_cast<mlir::scf::IfOp>(value.getOwner())
                                            : mlir::scf::IfOp{};
                        if (!branch) { return {}; }
                        return branch.getThenRegion().front().back().getOperand(
                            value.getResultNumber());
                    };
                    selected = normalValue(lhs) == loop.getBeforeArguments()[1] &&
                               normalValue(rhs) ==
                                   loop.getBeforeArguments()[fixture == *duplicated ? 1 : 0];
                });
                check(selected, "each projected result retains its original selected SSA value");
            }
            if (auto failure = expandDOMHelpers(*input, contract.entry, completeBudget)) {
                check(false, "custom iterator methods expand without boxed protocol records");
                std::fprintf(stderr, "%s\n", llvm::toString(std::move(failure)).c_str());
                continue;
            }
            contract.moduleSha256 = hostContractFingerprint(*input);
            DOMEntryAnalysis proof(*input, contract);
            check(proof.proved(),
                  "projected custom iterator reproves element lifetime and effects");
            if (!proof.proved()) { std::fprintf(stderr, "%s\n", proof.reason().str().c_str()); }
            if (fixture != *original && fixture != *withoutReturn) {
                unsigned loops = 0, calls = 0, poison = 0, switches = 0;
                input->walk([&](mlir::scf::WhileOp) { ++loops; });
                input->walk([&](ctjs::CallOp) { ++calls; });
                input->walk([&](mlir::ub::PoisonOp) { ++poison; });
                input->walk([&](mlir::scf::IndexSwitchOp) { ++switches; });
                check(loops == 1 && calls == 4 && !poison && !switches,
                      "counted exit preserves all source calls without inactive values or muxes");
            }
        }
    }
    contract.moduleSha256 = hostContractFingerprint(*original);

    for (const auto & invalid : {
             replaced(countedSource, "scf.yield %loop#1 : !ctjs.value",
                      "scf.yield %loop#2 : !ctjs.value"),
             replaced(countedSource,
                      "    %selector =", "    %observed = ctjs.unary not %loop#2\n    %selector ="),
             replaced(countedSource, "      scf.yield %count, %normalCount, %breakCount, %tag",
                      "      %observed = ctjs.unary not %normalCount\n"
                      "      scf.yield %count, %normalCount, %breakCount, %tag"),
             replaced(countedSource, "%count = %zero", "%count = %poison"),
             replaced(countedSource, "%normalTag = arith.constant 7 : i32",
                      "%unknown = ctjs.truthy %element\n"
                      "    %normalTag = arith.extui %unknown : i1 to i32"),
             replaced(countedSource, "    case 7 {",
                      "    case 7 {\n"
                      "      %method = ctjs.get_property %element[%attributeName]\n"
                      "      %effect = ctjs.call %method(%element, %visited, %yes)"),
             replaced(countedSource, "    %selector =",
                      "    %observedTag = arith.index_castui %loop#3 : i32 to index\n"
                      "    %selector ="),
             replaced(crossedSource, "scf.yield %loop#3, %loop#2", "scf.yield %loop#3, %loop#4"),
             replaced(crossedSource,
                      "    %selector =", "    %observed = ctjs.unary not %loop#2\n    %selector ="),
             replaced(crossedSource, "      scf.yield %count, %extra, %normalCount",
                      "      %observed = ctjs.unary not %normalCount\n"
                      "      scf.yield %count, %extra, %normalCount"),
             replaced(crossedSource, "%extra = %extraInitial", "%extra = %poison"),
             replaced(crossedSource, "%normalTag = arith.constant 7 : i32",
                      "%unknown = ctjs.truthy %element\n"
                      "    %normalTag = arith.extui %unknown : i1 to i32"),
             replaced(crossedSource, "    case 7 {",
                      "    case 7 {\n"
                      "      %method = ctjs.get_property %element[%attributeName]\n"
                      "      %effect = ctjs.call %method(%element, %visited, %yes)"),
             replaced(crossedSource, "    %selector =",
                      "    %observedTag = arith.index_castui %loop#6 : i32 to index\n"
                      "    %selector ="),
         }) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
        check(static_cast<bool>(input), "invalid counted break continuation parses");
        if (!input) { continue; }
        auto request = contract;
        request.moduleSha256 = hostContractFingerprint(*input);
        auto failure = normalizeDOMCustomIteration(*input, request, completeBudget);
        check(static_cast<bool>(failure),
              "poison observations, unknown tags and non-projecting exits refuse");
        if (failure) { llvm::consumeError(std::move(failure)); }
        request.moduleSha256 = hostContractFingerprint(*input);
        check(noEvidence(*input, DOMEntryAnalysis(*input, request)),
              "invalid counted exits publish no DOM evidence");
    }
    const std::string close = "    %closed = ctjs.call %close(%undefined, %record, %normal)\n";
    const std::string identityHeader =
        "ctjs.func @identity$1(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) "
        "-> !ctjs.value attributes {upvalue_count = 0 : i32}";
    for (const auto & [invalid, diagnostic] : {
             std::pair{replaced(replaced(source, close, ""), "    %loop =", close + "    %loop ="),
                       "direct loop test"},
             std::pair{replaced(source, "        %stop =",
                                "        %observed = ctjs.unary not %item\n        %stop ="),
                       "not-done continuation"},
             std::pair{replaced(replaced(source,
                                         "      %item = ctjs.call %next(%undefined, %record)\n"
                                         "      %done = ctjs.get_property %record[%doneName]",
                                         "      %done = ctjs.get_property %record[%doneName]\n"
                                         "      %item = ctjs.call %next(%undefined, %record)"),
                                "%again = arith.constant true", "%again = arith.constant false"),
                       "not-done continuation"},
             std::pair{
                 replaced(source, identityHeader,
                          replaced(identityHeader, "attributes {", "attributes {ctjs.skipped, ")),
                 "closed identity method"},
             std::pair{replaced(source, "    ctjs.frame_exit %identityFrame\n", ""),
                       "live shadow frame"},
             std::pair{replaced(source,
                                "%identity = ctjs.create_closure %callee[1] this %undefined",
                                "%identity = ctjs.create_closure %callee[1] this %this"),
                       "closed identity method"},
             std::pair{
                 replaced(source, "%stop = arith.constant false", "%stop = arith.constant true"),
                 "stop before next"},
             std::pair{replaced(source, "    ctjs.set_property %result[%doneName], %done\n", ""),
                       "own done and value fields"},
             std::pair{replaced(source, "    %loop =",
                                "    ctjs.store_global \"leaked\", %record\n    %loop ="),
                       "unsupported observer"},
             std::pair{
                 replaced(replaced(source, "    ctjs.set_property %result[%doneName], %done",
                                   "    %numericDone = ctjs.constant "
                                   "#ctjs.number<4607182418800017408>\n"
                                   "    ctjs.set_property %result[%doneName], %numericDone"),
                          close,
                          "    %rawDone = ctjs.get_property %record[%doneName]\n"
                          "    %observe = ctjs.get_property %element[%attributeName]\n"
                          "    %observed = ctjs.call %observe(%element, %visited, %rawDone)\n" +
                              close),
                 "truth-only observations"},
         }) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
        check(static_cast<bool>(input), "invalid custom iterator witness parses");
        if (!input) { continue; }
        auto request = contract;
        request.moduleSha256 = hostContractFingerprint(*input);
        const auto reason =
            llvm::toString(normalizeDOMCustomIteration(*input, request, completeBudget));
        check(reason.find(diagnostic) != std::string::npos,
              "invalid custom protocol preserves its precise refusal");
        if (reason.find(diagnostic) == std::string::npos) {
            std::fprintf(stderr, "expected %s, got %s\n", diagnostic, reason.c_str());
        }
        request.moduleSha256 = hostContractFingerprint(*input);
        check(noEvidence(*input, DOMEntryAnalysis(*input, request)),
              "refused private custom protocol exposes no DOM evidence");
    }

    auto stale = mlir::parseSourceString<mlir::ModuleOp>(
        replaced(source, "#ctjs.string<\"data-yielded\">", "#ctjs.string<\"changed\">"), &context);
    check(static_cast<bool>(stale), "stale custom iterator witness parses");
    if (stale) {
        const auto fingerprint = hostContractFingerprint(*stale);
        const auto reason =
            llvm::toString(normalizeDOMCustomIteration(*stale, contract, completeBudget));
        check(reason.find("fingerprint") != std::string::npos &&
                  hostContractFingerprint(*stale) == fingerprint,
              "stale custom iterator fingerprint refuses before mutation");
    }
    // Locate the completion threshold instead of baking in today's scan count.
    // Sample early, middle and last incomplete budgets on fresh private clones.
    for (auto fixture : {*original, *counted, *crossed}) {
        auto request = contract;
        request.moduleSha256 = hostContractFingerprint(fixture);
        unsigned low = 0, high = completeBudget;
        while (low < high) {
            const unsigned middle = low + (high - low) / 2;
            mlir::OwningOpRef<mlir::ModuleOp> input(fixture.clone());
            if (auto failure = normalizeDOMCustomIteration(*input, request, middle)) {
                llvm::consumeError(std::move(failure));
                low = middle + 1;
            } else {
                high = middle;
            }
        }
        check(low > 64 && low < completeBudget,
              "ordinary and counted iterators have finite charged completion budgets");
        if (low <= 64 || low == completeBudget) { continue; }
        for (unsigned budget : {0U, 64U, low / 2, low - 1}) {
            mlir::OwningOpRef<mlir::ModuleOp> input(fixture.clone());
            const auto reason =
                llvm::toString(normalizeDOMCustomIteration(*input, request, budget));
            check(reason.find("budget") != std::string::npos,
                  "sampled incomplete custom normalization cuts refuse");
            auto observed = request;
            observed.moduleSha256 = hostContractFingerprint(*input);
            check(noEvidence(*input, DOMEntryAnalysis(*input, observed)),
                  "incomplete custom normalization cannot publish DOM evidence");
            if (budget <= 64) {
                check(observed.moduleSha256 == request.moduleSha256,
                      "early custom normalization budgets leave the input untouched");
            }
        }
    }
}

} // namespace ctcompile::test::host_contract
