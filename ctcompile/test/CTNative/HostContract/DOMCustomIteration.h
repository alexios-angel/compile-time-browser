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
    // Receiver state must survive next calls and reach the close hook only on
    // break. Its reads cannot be replaced with the allocation's initial values.
    const std::string stateInitialization =
        "    %emittedName = ctjs.constant #ctjs.string<\"emitted\">\n"
        "    %emittedInitial = ctjs.constant #ctjs.number<0>\n"
        "    ctjs.set_property %holder[%emittedName], %emittedInitial\n";
    auto receiverSource = replaced(countedSource, "    %holder = ctjs.create_object\n",
                                   "    %holder = ctjs.create_object\n" + stateInitialization);
    receiverSource = replaced(receiverSource, "    %done = ctjs.call %has(%element, %yielded)",
                              R"MLIR(    %emittedName = ctjs.constant #ctjs.string<"emitted">
    %emitted = ctjs.get_property %this[%emittedName]
    %zero = ctjs.constant #ctjs.number<0>
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %done = ctjs.compare gt %emitted, %zero
    %advanced = ctjs.binary_static add %emitted, %one
    ctjs.set_property %this[%emittedName], %advanced)MLIR");
    receiverSource =
        replaced(receiverSource, "%finish = ctjs.create_closure %callee[3] this %undefined",
                 "%finish = ctjs.create_closure %callee[3] this %undefined "
                 "captures %capture");
    receiverSource = replaced(
        receiverSource,
        R"MLIR(  ctjs.func @return$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %result = ctjs.create_object)MLIR",
        R"MLIR(  ctjs.func @return$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 1 : i32} {
    %element = ctjs.load_upvalue %callee[0]
    %emittedName = ctjs.constant #ctjs.string<"emitted">
    %emitted = ctjs.get_property %this[%emittedName]
    %setName = ctjs.constant #ctjs.string<"setAttribute">
    %set = ctjs.get_property %element[%setName]
    %closedName = ctjs.constant #ctjs.string<"data-closed-count">
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %countMatches = ctjs.compare strict_eq %emitted, %one
    %written = ctjs.call %set(%element, %closedName, %countMatches)
    %result = ctjs.create_object)MLIR");
    auto twoStateSource = replaced(
        receiverSource, stateInitialization,
        stateInitialization + "    %extraName = ctjs.constant #ctjs.string<\"extra\">\n"
                              "    %extraInitial = ctjs.constant "
                              "#ctjs.number<4619567317775286272>\n"
                              "    ctjs.set_property %holder[%extraName], %extraInitial\n");
    twoStateSource =
        replaced(twoStateSource, "    ctjs.set_property %this[%emittedName], %advanced",
                 R"MLIR(    %extraName = ctjs.constant #ctjs.string<"extra">
    %extra = ctjs.get_property %this[%extraName]
    %extraAdvanced = ctjs.binary_static add %extra, %emitted
    ctjs.set_property %this[%emittedName], %advanced
    ctjs.set_property %this[%extraName], %extraAdvanced)MLIR");
    twoStateSource = replaced(
        twoStateSource, "    %written = ctjs.call %set(%element, %closedName, %countMatches)",
        R"MLIR(    %written = ctjs.call %set(%element, %closedName, %countMatches)
    %extraName = ctjs.constant #ctjs.string<"extra">
    %extra = ctjs.get_property %this[%extraName]
    %extraClosedName = ctjs.constant #ctjs.string<"data-closed-extra">
    %seven = ctjs.constant #ctjs.number<4619567317775286272>
    %extraMatches = ctjs.compare strict_eq %extra, %seven
    %extraWritten = ctjs.call %set(%element, %extraClosedName, %extraMatches))MLIR");
    auto receiver = mlir::parseSourceString<mlir::ModuleOp>(receiverSource, &context);
    auto twoState = mlir::parseSourceString<mlir::ModuleOp>(twoStateSource, &context);
    check(receiver && twoState, "one and two-field receiver iterator witnesses parse");
    if (!receiver || !twoState) { return; }
    // The same recurrence through two closures must preserve cell identity,
    // capture positions and sequential stores without retaining boxed storage.
    const auto capturedState = [&](std::string text, bool two) {
        text = replaced(text, "ctjs.set_property %holder[%emittedName], %emittedInitial",
                        "%emittedCell = ctjs.create_cell %emittedInitial");
        if (two) {
            text = replaced(text, "ctjs.set_property %holder[%extraName], %extraInitial",
                            "%extraCell = ctjs.create_cell %extraInitial");
        }
        for (auto method : {"step", "finish"}) {
            const std::string closure = "%" + std::string(method) + " = ctjs.create_closure";
            const auto begin = text.find(closure);
            const auto end = text.find('\n', begin);
            text.insert(end, two ? ", %emittedCell, %extraCell" : ", %emittedCell");
        }
        for (unsigned i = 0; i != 2; ++i) {
            text = replaced(text, "attributes {upvalue_count = 1 : i32}",
                            two ? "attributes {upvalue_count = 3 : i32}"
                                : "attributes {upvalue_count = 2 : i32}");
            text = replaced(text, "%emitted = ctjs.get_property %this[%emittedName]",
                            "%emitted = ctjs.load_upvalue %callee[1]");
            if (two) {
                text = replaced(text, "%extra = ctjs.get_property %this[%extraName]",
                                "%extra = ctjs.load_upvalue %callee[2]");
            }
        }
        text = replaced(text, "ctjs.set_property %this[%emittedName], %advanced",
                        "ctjs.store_upvalue %callee[1], %advanced");
        if (two) {
            text = replaced(text, "ctjs.set_property %this[%extraName], %extraAdvanced",
                            "ctjs.store_upvalue %callee[2], %extraAdvanced");
        }
        return text;
    };
    const auto capturedSource = capturedState(receiverSource, false);
    const auto twoCapturedSource = capturedState(twoStateSource, true);
    const auto initializedCaptureSource =
        replaced(capturedSource, "%emittedCell = ctjs.create_cell %emittedInitial",
                 "%cellUndefined = ctjs.constant #ctjs.undefined\n"
                 "    %emittedCell = ctjs.create_cell %cellUndefined\n"
                 "    ctjs.cell_set %emittedCell, %emittedInitial");
    auto reorderedCaptureSource =
        replaced(twoCapturedSource,
                 "%finish = ctjs.create_closure %callee[3] this %undefined captures %capture, "
                 "%emittedCell, %extraCell",
                 "%finish = ctjs.create_closure %callee[3] this %undefined captures %extraCell, "
                 "%emittedCell, %capture");
    const auto returnBegin = reorderedCaptureSource.find("  ctjs.func @return$3");
    reorderedCaptureSource =
        reorderedCaptureSource.substr(0, returnBegin) +
        replaced(replaced(reorderedCaptureSource.substr(returnBegin),
                          "%element = ctjs.load_upvalue %callee[0]",
                          "%element = ctjs.load_upvalue %callee[2]"),
                 "%extra = ctjs.load_upvalue %callee[2]", "%extra = ctjs.load_upvalue %callee[0]");
    auto captured = mlir::parseSourceString<mlir::ModuleOp>(capturedSource, &context);
    auto twoCaptured = mlir::parseSourceString<mlir::ModuleOp>(twoCapturedSource, &context);
    auto initializedCapture =
        mlir::parseSourceString<mlir::ModuleOp>(initializedCaptureSource, &context);
    auto reorderedCapture =
        mlir::parseSourceString<mlir::ModuleOp>(reorderedCaptureSource, &context);
    check(captured && twoCaptured && initializedCapture && reorderedCapture,
          "cell iterator witnesses with direct/deferred initialization and reordered slots parse");
    if (!captured || !twoCaptured || !initializedCapture || !reorderedCapture) { return; }
    const auto conditionalCapturedSource =
        replaced(capturedSource, "    ctjs.store_upvalue %callee[1], %advanced",
                 "    %nestedFlag = arith.constant true\n"
                 "    scf.if %nestedFlag {\n"
                 "      ctjs.store_upvalue %callee[1], %advanced\n"
                 "      scf.yield\n"
                 "    }");
    const auto conditionalReceiverSource =
        replaced(receiverSource, "    ctjs.set_property %this[%emittedName], %advanced",
                 "    %nestedFlag = arith.constant true\n"
                 "    scf.if %nestedFlag {\n"
                 "      ctjs.set_property %this[%emittedName], %advanced\n"
                 "      scf.yield\n"
                 "    }");
    // Keep the original result prefix, read stores in source order, join an
    // unwritten arm with its incoming state, then update the joined value.
    std::string branchStores =
        R"MLIR(    %branchSetName = ctjs.constant #ctjs.string<"setAttribute">
    %branchSet = ctjs.get_property %element[%branchSetName]
    %thenName = ctjs.constant #ctjs.string<"branch-then">
    %innerName = ctjs.constant #ctjs.string<"branch-inner">
    %elseName = ctjs.constant #ctjs.string<"branch-else">
    %afterName = ctjs.constant #ctjs.string<"branch-after">
    %yesBranch = ctjs.constant #ctjs.boolean<true>
    %branchFlag = ctjs.truthy %done
    %branchValue = scf.if %branchFlag -> !ctjs.value {
      ctjs.store_upvalue %callee[1], %advanced
      %afterEmitted = ctjs.load_upvalue %callee[1]
      %nextExtra = ctjs.binary_static add %extra, %afterEmitted
      ctjs.store_upvalue %callee[2], %nextExtra
      %effect = ctjs.call %branchSet(%element, %thenName, %yesBranch)
      scf.yield %emitted : !ctjs.value
    } else {
      %nestedValue = ctjs.call %has(%element, %yielded)
      %nestedFlag = ctjs.truthy %nestedValue
      %inner = scf.if %nestedFlag -> !ctjs.value {
        ctjs.store_upvalue %callee[2], %extraAdvanced
        %afterExtra = ctjs.load_upvalue %callee[2]
        %nextEmitted = ctjs.binary_static add %advanced, %afterExtra
        ctjs.store_upvalue %callee[1], %nextEmitted
        %effect = ctjs.call %branchSet(%element, %innerName, %yesBranch)
        scf.yield %extra : !ctjs.value
      } else {
        %effect = ctjs.call %branchSet(%element, %elseName, %yesBranch)
        scf.yield %advanced : !ctjs.value
      }
      scf.yield %inner : !ctjs.value
    }
    %joined = ctjs.load_upvalue %callee[1]
    %afterJoin = ctjs.binary_static add %joined, %one
    ctjs.store_upvalue %callee[1], %afterJoin
    %branchSeen = ctjs.compare strict_eq %branchValue, %afterJoin
    %afterEffect = ctjs.call %branchSet(%element, %afterName, %branchSeen))MLIR";
    const auto branchCapturedSource = replaced(twoCapturedSource,
                                               "    ctjs.store_upvalue %callee[1], %advanced\n"
                                               "    ctjs.store_upvalue %callee[2], %extraAdvanced",
                                               branchStores);
    const auto receiverAccess = [](std::string text) {
        for (auto [from, to] : {
                 std::pair{"ctjs.load_upvalue %callee[1]", "ctjs.get_property %this[%emittedName]"},
                 std::pair{"ctjs.load_upvalue %callee[2]", "ctjs.get_property %this[%extraName]"},
                 std::pair{"ctjs.store_upvalue %callee[1],",
                           "ctjs.set_property %this[%emittedName],"},
                 std::pair{"ctjs.store_upvalue %callee[2],",
                           "ctjs.set_property %this[%extraName],"},
             }) {
            while (text.find(from) != std::string::npos) { text = replaced(text, from, to); }
        }
        return text;
    };
    const auto branchReceiverSource =
        replaced(twoStateSource,
                 "    ctjs.set_property %this[%emittedName], %advanced\n"
                 "    ctjs.set_property %this[%extraName], %extraAdvanced",
                 receiverAccess(branchStores));
    auto conditionalCaptured =
        mlir::parseSourceString<mlir::ModuleOp>(conditionalCapturedSource, &context);
    auto conditionalReceiver =
        mlir::parseSourceString<mlir::ModuleOp>(conditionalReceiverSource, &context);
    auto branchCaptured = mlir::parseSourceString<mlir::ModuleOp>(branchCapturedSource, &context);
    auto branchReceiver = mlir::parseSourceString<mlir::ModuleOp>(branchReceiverSource, &context);
    check(conditionalCaptured && conditionalReceiver && branchCaptured && branchReceiver,
          "original conditional stores and ordered nested two-state joins parse");
    if (!conditionalCaptured || !conditionalReceiver || !branchCaptured || !branchReceiver) {
        return;
    }
    // Preserve the prior zero-trip refusals intact: the before region still
    // writes state once even when the after region is never entered.
    const auto zeroCapturedSource =
        replaced(conditionalCapturedSource, "      ctjs.store_upvalue %callee[1], %advanced",
                 "      scf.while : () -> () {\n"
                 "        ctjs.store_upvalue %callee[1], %advanced\n"
                 "        %again = arith.constant false\n"
                 "        scf.condition(%again)\n"
                 "      } do {\n"
                 "        scf.yield\n"
                 "      }");
    const auto zeroReceiverSource = replaced(
        conditionalReceiverSource, "      ctjs.set_property %this[%emittedName], %advanced",
        "      scf.while : () -> () {\n"
        "        ctjs.set_property %this[%emittedName], %advanced\n"
        "        %again = arith.constant false\n"
        "        scf.condition(%again)\n"
        "      } do {\n"
        "        scf.yield\n"
        "      }");
    const std::string loopStores =
        R"MLIR(    %loopSetName = ctjs.constant #ctjs.string<"setAttribute">
    %loopSet = ctjs.get_property %element[%loopSetName]
    %beforeName = ctjs.constant #ctjs.string<"loop-before">
    %innerName = ctjs.constant #ctjs.string<"loop-inner">
    %afterName = ctjs.constant #ctjs.string<"loop-after">
    %exitName = ctjs.constant #ctjs.string<"loop-exit">
    %yesLoop = ctjs.constant #ctjs.boolean<true>
    %loopValue = scf.while (%original = %emitted) : (!ctjs.value) -> !ctjs.value {
      %beforeEmitted = ctjs.load_upvalue %callee[1]
      %beforeAdvanced = ctjs.binary_static add %beforeEmitted, %one
      ctjs.store_upvalue %callee[1], %beforeAdvanced
      %readBefore = ctjs.load_upvalue %callee[1]
      %beforeExtra = ctjs.load_upvalue %callee[2]
      %beforeExtraAdvanced = ctjs.binary_static add %beforeExtra, %readBefore
      ctjs.store_upvalue %callee[2], %beforeExtraAdvanced
      %effectBefore = ctjs.call %loopSet(%element, %beforeName, %yesLoop)
      %more = ctjs.compare lt %beforeEmitted, %one
      %again = ctjs.truthy %more
      scf.condition(%again) %beforeEmitted : !ctjs.value
    } do {
    ^bb0(%carried: !ctjs.value):
      %afterEmitted = ctjs.load_upvalue %callee[1]
      %afterExtra = ctjs.load_upvalue %callee[2]
      %first = ctjs.compare strict_eq %carried, %zero
      %branchFlag = ctjs.truthy %first
      %branchValue = scf.if %branchFlag -> !ctjs.value {
        %updated = ctjs.binary_static add %afterEmitted, %afterExtra
        ctjs.store_upvalue %callee[1], %updated
        %reloaded = ctjs.load_upvalue %callee[1]
        %added = ctjs.binary_static add %afterExtra, %reloaded
        ctjs.store_upvalue %callee[2], %added
        %effectInner = ctjs.call %loopSet(%element, %innerName, %yesLoop)
        scf.yield %carried : !ctjs.value
      } else {
        scf.yield %afterEmitted : !ctjs.value
      }
      %joinedExtra = ctjs.load_upvalue %callee[2]
      %nextExtra = ctjs.binary_static add %joinedExtra, %one
      ctjs.store_upvalue %callee[2], %nextExtra
      %afterSeen = ctjs.compare strict_eq %branchValue, %carried
      %effectAfter = ctjs.call %loopSet(%element, %afterName, %afterSeen)
      scf.yield %branchValue : !ctjs.value
    }
    %exitEmitted = ctjs.load_upvalue %callee[1]
    %exitExtra = ctjs.load_upvalue %callee[2]
    %exitUpdated = ctjs.binary_static add %exitEmitted, %one
    ctjs.store_upvalue %callee[1], %exitUpdated
    %exitSeen = ctjs.compare strict_eq %loopValue, %exitExtra
    %effectExit = ctjs.call %loopSet(%element, %exitName, %exitSeen))MLIR";
    const auto loopCapturedSource = replaced(twoCapturedSource,
                                             "    ctjs.store_upvalue %callee[1], %advanced\n"
                                             "    ctjs.store_upvalue %callee[2], %extraAdvanced",
                                             loopStores);
    const auto loopReceiverSource =
        replaced(twoStateSource,
                 "    ctjs.set_property %this[%emittedName], %advanced\n"
                 "    ctjs.set_property %this[%extraName], %extraAdvanced",
                 receiverAccess(loopStores));
    const auto zeroTwoStateSource =
        replaced(loopCapturedSource, "%again = ctjs.truthy %more", "%again = arith.constant false");
    auto zeroCaptured = mlir::parseSourceString<mlir::ModuleOp>(zeroCapturedSource, &context);
    auto zeroReceiver = mlir::parseSourceString<mlir::ModuleOp>(zeroReceiverSource, &context);
    auto loopCaptured = mlir::parseSourceString<mlir::ModuleOp>(loopCapturedSource, &context);
    auto loopReceiver = mlir::parseSourceString<mlir::ModuleOp>(loopReceiverSource, &context);
    auto zeroTwoState = mlir::parseSourceString<mlir::ModuleOp>(zeroTwoStateSource, &context);
    check(zeroCaptured && zeroReceiver && loopCaptured && loopReceiver && zeroTwoState,
          "zero-trip and ordered two-state method loops parse");
    if (!zeroCaptured || !zeroReceiver || !loopCaptured || !loopReceiver || !zeroTwoState) {
        return;
    }
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
    for (auto fixture : {*original,
                         *withoutReturn,
                         *counted,
                         *retagged,
                         *crossed,
                         *duplicated,
                         *receiver,
                         *twoState,
                         *captured,
                         *twoCaptured,
                         *initializedCapture,
                         *reorderedCapture,
                         *conditionalCaptured,
                         *conditionalReceiver,
                         *branchCaptured,
                         *branchReceiver,
                         *zeroCaptured,
                         *zeroReceiver,
                         *loopCaptured,
                         *loopReceiver,
                         *zeroTwoState}) {
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
            if (fixture == *branchCaptured || fixture == *branchReceiver) {
                auto body = input->lookupSymbol<ctjs::FuncOp>("next$2");
                mlir::scf::IfOp outer, inner;
                mlir::Value finalCount, finalExtra;
                llvm::SmallVector<llvm::StringRef> effects;
                bool effectsLocal = true;
                body.walk([&](mlir::scf::IfOp branch) {
                    (branch->getBlock() == &body.getBody().front() ? outer : inner) = branch;
                });
                body.walk([&](ctjs::SetPropertyOp set) {
                    const auto key = ctjs::constantKey(set.getKey());
                    if (key == "__ctcompile_state_0") { finalCount = set.getValue(); }
                    if (key == "__ctcompile_state_1") { finalExtra = set.getValue(); }
                });
                body.walk([&](ctjs::CallOp call) {
                    if (call.getArgs().size() != 2) { return; }
                    const auto key = ctjs::constantKey(call.getArgs()[0]);
                    if (!key.starts_with("branch-")) { return; }
                    effects.push_back(key);
                    auto * region = call->getParentRegion();
                    effectsLocal &= outer && inner &&
                                    (key == "branch-then"    ? region == &outer.getThenRegion()
                                     : key == "branch-inner" ? region == &inner.getThenRegion()
                                     : key == "branch-else"  ? region == &inner.getElseRegion()
                                                             : region == &body.getBody());
                });
                check(outer && inner && outer.getNumResults() == 3 && inner.getNumResults() == 3,
                      "nested state joins retain each original result before two state slots");
                if (outer && inner && outer.getNumResults() == 3 && inner.getNumResults() == 3) {
                    const auto then = outer.getThenRegion().front().back().getOperands();
                    const auto nested = inner.getThenRegion().front().back().getOperands();
                    const auto otherwise = inner.getElseRegion().front().back().getOperands();
                    auto count = finalCount ? finalCount.getDefiningOp<ctjs::BinaryStaticOp>()
                                            : ctjs::BinaryStaticOp{};
                    auto added = then[2].getDefiningOp<ctjs::BinaryStaticOp>();
                    auto nestedAdded = nested[1].getDefiningOp<ctjs::BinaryStaticOp>();
                    const auto args = body.getBody().front().getArguments();
                    check(then[0] == args[3] && added && added.getLhs() == args[4] &&
                              added.getRhs() == then[1] && otherwise[1] == args[3] &&
                              otherwise[2] == args[4] && nested[0] == args[4] && nestedAdded &&
                              nestedAdded.getLhs() == then[1] &&
                              nestedAdded.getRhs() == nested[2] && count &&
                              count.getLhs() == outer.getResult(1) &&
                              finalExtra == outer.getResult(2),
                          "source result, sequential reads, untouched arm and post-join update "
                          "persist");
                }
                check(effectsLocal && effects == llvm::SmallVector<llvm::StringRef>{"branch-then",
                                                                                    "branch-inner",
                                                                                    "branch-else",
                                                                                    "branch-after"},
                      "state projection preserves all branch effects in source order");
            }
            if (fixture == *loopCaptured || fixture == *loopReceiver || fixture == *zeroTwoState) {
                auto body = input->lookupSymbol<ctjs::FuncOp>("next$2");
                mlir::scf::WhileOp loop;
                mlir::scf::IfOp branch;
                mlir::Value finalCount, finalExtra;
                ctjs::CompareOp exitSeen;
                llvm::SmallVector<llvm::StringRef> effects;
                bool effectsLocal = true;
                body.walk([&](mlir::scf::WhileOp found) { loop = found; });
                body.walk([&](mlir::scf::IfOp found) { branch = found; });
                body.walk([&](ctjs::SetPropertyOp set) {
                    const auto key = ctjs::constantKey(set.getKey());
                    if (key == "__ctcompile_state_0") { finalCount = set.getValue(); }
                    if (key == "__ctcompile_state_1") { finalExtra = set.getValue(); }
                });
                body.walk([&](ctjs::CallOp call) {
                    if (call.getArgs().size() != 2) { return; }
                    const auto key = ctjs::constantKey(call.getArgs()[0]);
                    if (!key.starts_with("loop-")) { return; }
                    effects.push_back(key);
                    auto * region = call->getParentRegion();
                    effectsLocal &= loop && branch &&
                                    (key == "loop-before"  ? region == &loop.getBefore()
                                     : key == "loop-inner" ? region == &branch.getThenRegion()
                                     : key == "loop-after" ? region == &loop.getAfter()
                                                           : region == &body.getBody());
                    if (key == "loop-exit") {
                        exitSeen = call.getArgs()[1].getDefiningOp<ctjs::CompareOp>();
                    }
                });
                const bool shape =
                    loop && branch && loop.getNumOperands() == 3 && loop.getNumResults() == 3 &&
                    loop.getBeforeArguments().size() == 3 && loop.getAfterArguments().size() == 3 &&
                    branch.getNumResults() == 3;
                check(shape, "method loop and nested branch preserve the original result prefix");
                if (shape) {
                    const auto before = loop.getBeforeArguments();
                    const auto after = loop.getAfterArguments();
                    const auto args = body.getBody().front().getArguments();
                    auto condition =
                        llvm::cast<mlir::scf::ConditionOp>(loop.getBefore().front().back());
                    const auto sent = condition.getArgs();
                    const auto yielded = loop.getAfter().front().back().getOperands();
                    const auto then = branch.getThenRegion().front().back().getOperands();
                    const auto otherwise = branch.getElseRegion().front().back().getOperands();
                    auto count = sent[1].getDefiningOp<ctjs::BinaryStaticOp>();
                    auto extra = sent[2].getDefiningOp<ctjs::BinaryStaticOp>();
                    auto changedCount = then[1].getDefiningOp<ctjs::BinaryStaticOp>();
                    auto changedExtra = then[2].getDefiningOp<ctjs::BinaryStaticOp>();
                    auto nextExtra = yielded[2].getDefiningOp<ctjs::BinaryStaticOp>();
                    auto exitCount = finalCount ? finalCount.getDefiningOp<ctjs::BinaryStaticOp>()
                                                : ctjs::BinaryStaticOp{};
                    check(loop.getInits()[0] == args[3] && loop.getInits()[1] == args[3] &&
                              loop.getInits()[2] == args[4] && sent[0] == before[1] && count &&
                              count.getLhs() == before[1] && extra && extra.getLhs() == before[2] &&
                              extra.getRhs() == sent[1],
                          "before reads incoming state and conditions carry its ordered writes");
                    check(
                        then[0] == after[0] && changedCount && changedCount.getLhs() == after[1] &&
                            changedCount.getRhs() == after[2] && changedExtra &&
                            changedExtra.getLhs() == after[2] && changedExtra.getRhs() == then[1] &&
                            otherwise[0] == after[1] && otherwise[1] == after[1] &&
                            otherwise[2] == after[2] && yielded[0] == branch.getResult(0) &&
                            yielded[1] == branch.getResult(1) && nextExtra &&
                            nextExtra.getLhs() == branch.getResult(2),
                        "after reads condition state and yields nested writes in tuple order");
                    check(exitCount && exitCount.getLhs() == loop.getResult(1) &&
                              finalExtra == loop.getResult(2) && exitSeen &&
                              exitSeen.getLhs() == loop.getResult(0) &&
                              exitSeen.getRhs() == loop.getResult(2),
                          "method exit reads final condition state after every trip count");
                }
                check(effectsLocal &&
                          effects == llvm::SmallVector<llvm::StringRef>{"loop-before", "loop-inner",
                                                                        "loop-after", "loop-exit"},
                      "loop state projection keeps each effect in its source region and order");
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
            if (fixture == *receiver || fixture == *twoState || fixture == *captured ||
                fixture == *twoCaptured || fixture == *initializedCapture ||
                fixture == *reorderedCapture || fixture == *conditionalCaptured ||
                fixture == *conditionalReceiver || fixture == *branchCaptured ||
                fixture == *branchReceiver || fixture == *zeroCaptured ||
                fixture == *zeroReceiver || fixture == *loopCaptured || fixture == *loopReceiver ||
                fixture == *zeroTwoState) {
                const bool two = fixture == *twoState || fixture == *twoCaptured ||
                                 fixture == *reorderedCapture || fixture == *branchCaptured ||
                                 fixture == *branchReceiver || fixture == *loopCaptured ||
                                 fixture == *loopReceiver || fixture == *zeroTwoState;
                bool objects = false, stateProperties = false, cells = false;
                mlir::Value closedCount, closedExtra;
                llvm::SmallVector<llvm::StringRef> closeOrder;
                input->walk([&](ctjs::CreateObjectOp) { objects = true; });
                input->walk([&](mlir::Operation * operation) {
                    cells |= llvm::isa<ctjs::CreateCellOp, ctjs::CellGetOp, ctjs::CellSetOp,
                                       ctjs::LoadUpvalueOp, ctjs::StoreUpvalueOp>(operation);
                    mlir::Value key;
                    if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
                        key = get.getKey();
                    } else if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(operation)) {
                        key = set.getKey();
                    }
                    if (key) {
                        const auto name = ctjs::constantKey(key);
                        stateProperties |= name == "emitted" || name == "extra";
                    }
                    auto call = llvm::dyn_cast<ctjs::CallOp>(operation);
                    if (!call || call.getArgs().size() != 2) { return; }
                    const auto attribute = ctjs::constantKey(call.getArgs()[0]);
                    auto test = call.getArgs()[1].getDefiningOp<ctjs::CompareOp>();
                    if (!test || test.getKind() != ctjs::CompareKind::StrictEq) { return; }
                    if (attribute == "data-closed-count") {
                        closeOrder.push_back(attribute);
                        closedCount = test.getLhs();
                    }
                    if (attribute == "data-closed-extra") {
                        closeOrder.push_back(attribute);
                        closedExtra = test.getLhs();
                    }
                });
                auto count = llvm::dyn_cast_if_present<mlir::OpResult>(closedCount);
                auto extra = llvm::dyn_cast_if_present<mlir::OpResult>(closedExtra);
                check(!objects && !stateProperties && !cells && count &&
                          llvm::isa<mlir::scf::WhileOp>(count.getOwner()) &&
                          closeOrder.size() == (two ? 2U : 1U) &&
                          closeOrder.front() == "data-closed-count" &&
                          (!two || (extra && extra.getOwner() == count.getOwner() &&
                                    extra.getResultNumber() == count.getResultNumber() + 1 &&
                                    closeOrder.back() == "data-closed-extra")),
                      "close reads distinct current scalar loop results without boxed state");
            } else if (fixture != *original && fixture != *withoutReturn) {
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

    auto otherCaptureSource = replaced(
        capturedSource, "    %record = ctjs.call %open(%undefined, %holder)",
        "    %observer = ctjs.create_closure %callee[4] this %undefined captures %emittedCell\n"
        "    %record = ctjs.call %open(%undefined, %holder)");
    otherCaptureSource = replaced(otherCaptureSource, "\n}\n", R"MLIR(
  ctjs.func @observer$4(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 1 : i32} {
    %observed = ctjs.load_upvalue %callee[0]
    ctjs.return %observed
  }
}
)MLIR");
    for (const auto & invalid : {
             replaced(capturedSource,
                      "ctjs.func @next$2(%this: !ctjs.value, %new: !ctjs.value, "
                      "%callee: !ctjs.value)",
                      "ctjs.func @next$2(%callee: !ctjs.value)"),
             replaced(capturedSource, "%emitted = ctjs.load_upvalue %callee[1]",
                      "%emitted = ctjs.load_upvalue %callee[2]"),
             replaced(capturedSource, "ctjs.store_upvalue %callee[1], %advanced",
                      "ctjs.store_upvalue %callee[-1], %advanced"),
             replaced(capturedSource, "ctjs.store_upvalue %callee[1], %advanced",
                      "ctjs.store_upvalue %callee[2], %advanced"),
             replaced(capturedSource, "attributes {upvalue_count = 2 : i32}",
                      "attributes {upvalue_count = 1 : i32}"),
             replaced(capturedSource, "attributes {upvalue_count = 2 : i32}",
                      "attributes {upvalue_count = 3 : i32}"),
             replaced(capturedSource, "%emitted = ctjs.load_upvalue %callee[1]",
                      "%emitted = ctjs.load_upvalue %this[1]"),
             replaced(capturedSource, "ctjs.store_upvalue %callee[1], %advanced",
                      "ctjs.store_upvalue %this[1], %advanced"),
             replaced(capturedSource, "%emittedCell = ctjs.create_cell %emittedInitial",
                      "%emittedCell = ctjs.create_cell %element"),
             replaced(capturedSource, "%emittedInitial = ctjs.constant #ctjs.number<0>",
                      "%emittedInitial = ctjs.constant #ctjs.boolean<false>"),
             replaced(initializedCaptureSource, "    ctjs.cell_set %emittedCell, %emittedInitial\n",
                      ""),
             replaced(initializedCaptureSource, "ctjs.cell_set %emittedCell, %emittedInitial",
                      "ctjs.cell_set %emittedCell, %emittedInitial\n"
                      "    ctjs.cell_set %emittedCell, %emittedInitial"),
             replaced(capturedSource, "    %record = ctjs.call %open(%undefined, %holder)",
                      "    ctjs.cell_set %emittedCell, %emittedInitial\n"
                      "    %record = ctjs.call %open(%undefined, %holder)"),
             replaced(capturedSource, "    %record = ctjs.call %open(%undefined, %holder)",
                      "    %external = ctjs.cell_get %emittedCell\n"
                      "    %record = ctjs.call %open(%undefined, %holder)"),
             replaced(capturedSource, "    %record = ctjs.call %open(%undefined, %holder)",
                      "    ctjs.store_global \"leaked\", %emittedCell\n"
                      "    %record = ctjs.call %open(%undefined, %holder)"),
             replaced(capturedSource, "    %record = ctjs.call %open(%undefined, %holder)",
                      "    %sharedStep = ctjs.create_closure %callee[2] this %undefined "
                      "captures %capture, %emittedCell\n"
                      "    %record = ctjs.call %open(%undefined, %holder)"),
             replaced(capturedSource, "    %record = ctjs.call %open(%undefined, %holder)",
                      "    ctjs.store_global \"leaked\", %step\n"
                      "    %record = ctjs.call %open(%undefined, %holder)"),
             replaced(capturedSource, "ctjs.store_upvalue %callee[1], %advanced",
                      "ctjs.store_upvalue %callee[1], %callee"),
             otherCaptureSource,
         }) {
        auto fixture = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
        check(static_cast<bool>(fixture), "invalid captured-state witness parses");
        if (!fixture) { continue; }
        for (auto provider :
             {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
            mlir::OwningOpRef<mlir::ModuleOp> input(fixture->clone());
            auto request = contract;
            request.provider = provider;
            request.moduleSha256 = hostContractFingerprint(*input);
            auto failure = normalizeDOMCustomIteration(*input, request, completeBudget);
            check(static_cast<bool>(failure),
                  "invalid capture identity, confinement and stores refuse before projection");
            if (failure) { llvm::consumeError(std::move(failure)); }
            check(hostContractFingerprint(*input) == request.moduleSha256 &&
                      noEvidence(*input, DOMEntryAnalysis(*input, request)),
                  "refused capture proof preserves source and publishes no DOM evidence");
        }
    }

    for (const auto & invalid : {
             replaced(receiverSource, stateInitialization,
                      stateInitialization +
                          "    ctjs.set_property %holder[%emittedName], %emittedInitial\n"),
             replaced(receiverSource, "%emittedInitial = ctjs.constant #ctjs.number<0>",
                      "%emittedInitial = ctjs.constant #ctjs.boolean<false>"),
             replaced(receiverSource, "%emittedName = ctjs.constant #ctjs.string<\"emitted\">",
                      "%emittedName = ctjs.constant #ctjs.string<\"__proto__\">"),
             replaced(replaced(receiverSource, stateInitialization, ""),
                      "    %record = ctjs.call %open(%undefined, %holder)\n",
                      "    %record = ctjs.call %open(%undefined, %holder)\n" + stateInitialization),
             replaced(receiverSource, "%emitted = ctjs.get_property %this[%emittedName]",
                      "%emitted = ctjs.get_property %this[%element]"),
             replaced(receiverSource, "%emitted = ctjs.get_property %this[%emittedName]",
                      "%missingName = ctjs.constant #ctjs.string<\"missing\">\n"
                      "    %emitted = ctjs.get_property %this[%missingName]"),
             replaced(receiverSource, "    %advanced = ctjs.binary_static add %emitted, %one",
                      "    ctjs.store_global \"leaked\", %this\n"
                      "    %advanced = ctjs.binary_static add %emitted, %one"),
             replaced(receiverSource, "    %record = ctjs.call %open(%undefined, %holder)",
                      "    ctjs.store_global \"leaked\", %holder\n"
                      "    %record = ctjs.call %open(%undefined, %holder)"),
             replaced(receiverSource, "    %nextName = ctjs.constant #ctjs.string<\"next\">",
                      "    %sharedStep = ctjs.create_closure %callee[2] this %undefined "
                      "captures %capture\n"
                      "    %nextName = ctjs.constant #ctjs.string<\"next\">"),
             replaced(receiverSource, "%step = ctjs.create_closure %callee[2] this %undefined",
                      "%step = ctjs.create_closure %callee[2] this %this"),
             replaced(receiverSource, "%finish = ctjs.create_closure %callee[3] this %undefined",
                      "%finish = ctjs.create_closure %callee[3] this %this"),
         }) {
        auto fixture = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
        check(static_cast<bool>(fixture), "invalid receiver-state witness parses");
        if (!fixture) { continue; }
        for (auto provider :
             {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
            mlir::OwningOpRef<mlir::ModuleOp> input(fixture->clone());
            auto request = contract;
            request.provider = provider;
            request.moduleSha256 = hostContractFingerprint(*input);
            auto failure = normalizeDOMCustomIteration(*input, request, completeBudget);
            check(static_cast<bool>(failure),
                  "unsupported or escaping receiver state refuses before projection");
            if (failure) { llvm::consumeError(std::move(failure)); }
            check(hostContractFingerprint(*input) == request.moduleSha256 &&
                      noEvidence(*input, DOMEntryAnalysis(*input, request)),
                  "refused receiver-state proof preserves the source and publishes no evidence");
        }
    }
    for (const auto & invalid : {
             replaced(branchCapturedSource, "%afterExtra = ctjs.load_upvalue %callee[2]",
                      "%afterExtra = ctjs.load_upvalue %callee[9]"),
             replaced(branchReceiverSource, "%afterExtra = ctjs.get_property %this[%extraName]",
                      "%afterExtra = ctjs.get_property %this[%element]"),
             replaced(branchCapturedSource, "      ctjs.store_upvalue %callee[1], %advanced",
                      "      ctjs.store_global \"leaked\", %callee\n"
                      "      ctjs.store_upvalue %callee[1], %advanced"),
             replaced(branchReceiverSource,
                      "      ctjs.set_property %this[%emittedName], %advanced",
                      "      ctjs.store_global \"leaked\", %this\n"
                      "      ctjs.set_property %this[%emittedName], %advanced"),
             replaced(loopCapturedSource, "%beforeExtra = ctjs.load_upvalue %callee[2]",
                      "%beforeExtra = ctjs.load_upvalue %callee[9]"),
             replaced(loopCapturedSource, "ctjs.store_upvalue %callee[2], %nextExtra",
                      "ctjs.store_upvalue %callee[9], %nextExtra"),
             replaced(loopReceiverSource, "%afterExtra = ctjs.get_property %this[%extraName]",
                      "%afterExtra = ctjs.get_property %this[%element]"),
             replaced(loopReceiverSource, "ctjs.set_property %this[%extraName], %nextExtra",
                      "ctjs.set_property %this[%element], %nextExtra"),
             replaced(loopCapturedSource, "      %effectBefore =",
                      "      ctjs.store_global \"leaked\", %callee\n      %effectBefore ="),
             replaced(loopReceiverSource, "      %effectAfter =",
                      "      ctjs.store_global \"leaked\", %this\n      %effectAfter ="),
         }) {
        auto fixture = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
        check(static_cast<bool>(fixture), "unsupported conditional-state witness parses");
        if (!fixture) { continue; }
        for (auto provider :
             {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
            mlir::OwningOpRef<mlir::ModuleOp> input(fixture->clone());
            auto request = contract;
            request.provider = provider;
            request.moduleSha256 = hostContractFingerprint(*input);
            auto failure = normalizeDOMCustomIteration(*input, request, completeBudget);
            check(static_cast<bool>(failure), "invalid state slots and branch/loop escapes refuse");
            if (failure) { llvm::consumeError(std::move(failure)); }
            check(hostContractFingerprint(*input) == request.moduleSha256 &&
                      noEvidence(*input, DOMEntryAnalysis(*input, request)),
                  "unsupported branch-state proof preserves source and publishes no evidence");
        }
    }
    for (unsigned malformed = 0; malformed != 5; ++malformed) {
        for (auto provider :
             {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
            mlir::OwningOpRef<mlir::ModuleOp> input(loopCaptured->clone());
            auto body = input->lookupSymbol<ctjs::FuncOp>("next$2");
            mlir::scf::WhileOp loop;
            body.walk([&](mlir::scf::WhileOp found) { loop = found; });
            auto condition = llvm::cast<mlir::scf::ConditionOp>(loop.getBefore().front().back());
            if (malformed == 0) {
                condition->eraseOperands(1, 1);
            } else if (malformed == 1) {
                loop.getAfter().front().back().eraseOperands(0, 1);
            } else if (malformed == 2) {
                condition->setOperand(0, loop.getBeforeArguments()[0]);
            } else if (malformed == 3) {
                loop.getBeforeArguments()[0].setType(mlir::IntegerType::get(&context, 1));
            } else {
                loop.getAfterArguments()[0].setType(mlir::IntegerType::get(&context, 1));
            }
            auto request = contract;
            request.provider = provider;
            request.moduleSha256 = hostContractFingerprint(*input);
            auto failure = normalizeDOMCustomIteration(*input, request, completeBudget);
            check(static_cast<bool>(failure), "malformed method loop correspondence refuses");
            if (failure) { llvm::consumeError(std::move(failure)); }
            check(hostContractFingerprint(*input) == request.moduleSha256 &&
                      noEvidence(*input, DOMEntryAnalysis(*input, request)),
                  "malformed method loops preserve source and publish no evidence");
        }
    }
    for (const auto & invalid : {
             replaced(receiverSource, "ctjs.set_property %this[%emittedName], %advanced",
                      "ctjs.set_property %this[%emittedName], %element"),
             replaced(capturedSource, "ctjs.store_upvalue %callee[1], %advanced",
                      "ctjs.store_upvalue %callee[1], %element"),
             replaced(branchCapturedSource, "ctjs.store_upvalue %callee[1], %advanced",
                      "ctjs.store_upvalue %callee[1], %element"),
             replaced(branchReceiverSource, "ctjs.set_property %this[%emittedName], %advanced",
                      "ctjs.set_property %this[%emittedName], %element"),
             replaced(loopCapturedSource, "ctjs.store_upvalue %callee[1], %beforeAdvanced",
                      "ctjs.store_upvalue %callee[1], %element"),
             replaced(loopReceiverSource, "ctjs.set_property %this[%extraName], %nextExtra",
                      "ctjs.set_property %this[%extraName], %element"),
             replaced(loopCapturedSource, "      %effectAfter =",
                      "      %unknown = ctjs.load_global \"unknown\"\n"
                      "      %effect = ctjs.call %unknown(%element, %afterEmitted)\n"
                      "      %effectAfter ="),
         }) {
        auto nonScalar = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
        check(static_cast<bool>(nonScalar), "iterator-state category mutation parses");
        if (!nonScalar) { continue; }
        auto request = contract;
        request.moduleSha256 = hostContractFingerprint(*nonScalar);
        if (auto failure = normalizeDOMCustomIteration(*nonScalar, request, completeBudget)) {
            llvm::consumeError(std::move(failure));
        } else if (auto failure = expandDOMHelpers(*nonScalar, request.entry, completeBudget)) {
            llvm::consumeError(std::move(failure));
        }
        request.moduleSha256 = hostContractFingerprint(*nonScalar);
        check(noEvidence(*nonScalar, DOMEntryAnalysis(*nonScalar, request)),
              "initial Number state never authorizes a non-Number recurrence or unknown effect");
    }

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
    for (auto fixture : {*original, *counted, *crossed, *receiver, *twoState, *captured,
                         *twoCaptured, *initializedCapture, *reorderedCapture, *conditionalCaptured,
                         *conditionalReceiver, *branchCaptured, *branchReceiver, *zeroCaptured,
                         *zeroReceiver, *loopCaptured, *loopReceiver, *zeroTwoState}) {
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
