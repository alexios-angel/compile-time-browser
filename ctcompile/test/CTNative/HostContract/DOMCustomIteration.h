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
    // A method-local break has an empty exit dispatch, but its live trip count
    // and sequential state writes must survive removal of the inactive tag.
    const std::string methodBreakStores = R"MLIR(    %methodPoison = ub.poison : i32
    %methodNormal = arith.constant 7 : i32
    %methodBreak = arith.constant 11 : i32
    %methodLimit = ctjs.constant #ctjs.number<4611686018427387904>
    %methodSetName = ctjs.constant #ctjs.string<"setAttribute">
    %methodSet = ctjs.get_property %element[%methodSetName]
    %methodBreakName = ctjs.constant #ctjs.string<"method-break">
    %methodContinueName = ctjs.constant #ctjs.string<"method-continue">
    %methodExitName = ctjs.constant #ctjs.string<"method-exit">
    %methodYes = ctjs.constant #ctjs.boolean<true>
    %methodLoop:2 = scf.while (%trip = %zero, %tag = %methodPoison) : (!ctjs.value, i32) -> (!ctjs.value, i32) {
      %within = ctjs.compare lt %trip, %methodLimit
      %active = ctjs.truthy %within
      %selected:3 = scf.if %active -> (i1, !ctjs.value, i32) {
        %beforeEmitted = ctjs.load_upvalue %callee[1]
        %beforeAdvanced = ctjs.binary_static add %beforeEmitted, %one
        ctjs.store_upvalue %callee[1], %beforeAdvanced
        %readBefore = ctjs.load_upvalue %callee[1]
        %beforeExtra = ctjs.load_upvalue %callee[2]
        %beforeExtraAdvanced = ctjs.binary_static add %beforeExtra, %readBefore
        ctjs.store_upvalue %callee[2], %beforeExtraAdvanced
        %stopping = ctjs.call %has(%element, %yielded)
        %breaking = ctjs.truthy %stopping
        %nextTrip = ctjs.binary_static add %trip, %one
        %branch:3 = scf.if %breaking -> (i1, !ctjs.value, i32) {
          %effectBreak = ctjs.call %methodSet(%element, %methodBreakName, %methodYes)
          %stop = arith.constant false
          scf.yield %stop, %nextTrip, %methodBreak : i1, !ctjs.value, i32
        } else {
          %effectContinue = ctjs.call %methodSet(%element, %methodContinueName, %methodYes)
          %again = arith.constant true
          scf.yield %again, %nextTrip, %methodNormal : i1, !ctjs.value, i32
        }
        scf.yield %branch#0, %branch#1, %branch#2 : i1, !ctjs.value, i32
      } else {
        %stop = arith.constant false
        scf.yield %stop, %trip, %methodNormal : i1, !ctjs.value, i32
      }
      scf.condition(%selected#0) %selected#1, %selected#2 : !ctjs.value, i32
    } do {
    ^bb0(%carried: !ctjs.value, %inactiveTag: i32):
      scf.yield %carried, %inactiveTag : !ctjs.value, i32
    }
    %methodSelector = arith.index_castui %methodLoop#1 : i32 to index
    scf.index_switch %methodSelector
    case 7 {
      scf.yield
    }
    default {
      scf.yield
    }
    %exitEmitted = ctjs.load_upvalue %callee[1]
    %exitExtra = ctjs.load_upvalue %callee[2]
    %exitUpdated = ctjs.binary_static add %exitEmitted, %methodLoop#0
    ctjs.store_upvalue %callee[1], %exitUpdated
    %exitSeen = ctjs.compare strict_eq %methodLoop#0, %exitExtra
    %effectExit = ctjs.call %methodSet(%element, %methodExitName, %exitSeen))MLIR";
    const auto methodBreakCapturedSource =
        replaced(twoCapturedSource,
                 "    ctjs.store_upvalue %callee[1], %advanced\n"
                 "    ctjs.store_upvalue %callee[2], %extraAdvanced",
                 methodBreakStores);
    const auto methodBreakReceiverSource =
        replaced(twoStateSource,
                 "    ctjs.set_property %this[%emittedName], %advanced\n"
                 "    ctjs.set_property %this[%extraName], %extraAdvanced",
                 receiverAccess(methodBreakStores));
    auto methodBreakCaptured =
        mlir::parseSourceString<mlir::ModuleOp>(methodBreakCapturedSource, &context);
    auto methodBreakReceiver =
        mlir::parseSourceString<mlir::ModuleOp>(methodBreakReceiverSource, &context);
    check(methodBreakCaptured && methodBreakReceiver,
          "paired two-state method break witnesses with empty exit dispatch parse");
    if (!methodBreakCaptured || !methodBreakReceiver) { return; }
    // Preserve the former external-access refusals as positive witnesses.
    const auto reinitializedCaptureSource =
        replaced(initializedCaptureSource, "ctjs.cell_set %emittedCell, %emittedInitial",
                 "ctjs.cell_set %emittedCell, %emittedInitial\n"
                 "    ctjs.cell_set %emittedCell, %emittedInitial");
    const auto externalWriteSource =
        replaced(capturedSource, "    %record = ctjs.call %open(%undefined, %holder)",
                 "    ctjs.cell_set %emittedCell, %emittedInitial\n"
                 "    %record = ctjs.call %open(%undefined, %holder)");
    const auto externalReadSource =
        replaced(capturedSource, "    %record = ctjs.call %open(%undefined, %holder)",
                 "    %external = ctjs.cell_get %emittedCell\n"
                 "    %record = ctjs.call %open(%undefined, %holder)");
    auto reinitializedCapture =
        mlir::parseSourceString<mlir::ModuleOp>(reinitializedCaptureSource, &context);
    auto externalWrite = mlir::parseSourceString<mlir::ModuleOp>(externalWriteSource, &context);
    auto externalRead = mlir::parseSourceString<mlir::ModuleOp>(externalReadSource, &context);
    // The entry and both methods share two cells. Reads after close must see
    // return's writes on break and the final next call's writes on exhaustion.
    auto entryCapturedSource =
        replaced(twoCapturedSource, "    %record = ctjs.call %open(%undefined, %holder)",
                 R"MLIR(    %entrySet = ctjs.get_property %element[%attributeName]
    %entryBeforeName = ctjs.constant #ctjs.string<"entry-before">
    %entryBodyName = ctjs.constant #ctjs.string<"entry-body">
    %entryCloseName = ctjs.constant #ctjs.string<"entry-before-close">
    %entryBefore = ctjs.cell_get %emittedCell
    %entryStart = ctjs.binary_static add %entryBefore, %zero
    ctjs.cell_set %emittedCell, %entryStart
    %entryBeforeSeen = ctjs.compare strict_eq %entryBefore, %zero
    %entryBeforeEffect = ctjs.call %entrySet(%element, %entryBeforeName, %entryBeforeSeen)
    %record = ctjs.call %open(%undefined, %holder))MLIR");
    entryCapturedSource =
        replaced(entryCapturedSource, "        %increment = ctjs.binary_static add %count, %one",
                 R"MLIR(        %bodyEmitted = ctjs.cell_get %emittedCell
        %bodyExtra = ctjs.cell_get %extraCell
        %bodyNext = ctjs.binary_static add %bodyExtra, %bodyEmitted
        ctjs.cell_set %extraCell, %bodyNext
        %bodyRead = ctjs.cell_get %extraCell
        %bodySeen = ctjs.compare strict_eq %bodyRead, %bodyNext
        %entryBodyEffect = ctjs.call %entrySet(%element, %entryBodyName, %bodySeen)
        %increment = ctjs.binary_static add %count, %one)MLIR");
    entryCapturedSource = replaced(entryCapturedSource,
                                   "    %closed = ctjs.call %close(%undefined, %record, %normal)",
                                   R"MLIR(    %preCloseCount = ctjs.cell_get %emittedCell
    %preCloseExtra = ctjs.cell_get %extraCell
    %preCloseSeen = ctjs.compare strict_eq %preCloseCount, %preCloseExtra
    %entryCloseEffect = ctjs.call %entrySet(%element, %entryCloseName, %preCloseSeen)
    %closed = ctjs.call %close(%undefined, %record, %normal)
    %finalCount = ctjs.cell_get %emittedCell
    %finalExtra = ctjs.cell_get %extraCell
    %finalTotal = ctjs.binary_static add %finalCount, %finalExtra
    %answerWithState = ctjs.binary_static add %answer, %finalTotal)MLIR");
    entryCapturedSource =
        replaced(entryCapturedSource, "ctjs.return %answer", "ctjs.return %answerWithState");
    entryCapturedSource = replaced(
        entryCapturedSource,
        "    %extraWritten = ctjs.call %set(%element, %extraClosedName, %extraMatches)",
        R"MLIR(    %extraWritten = ctjs.call %set(%element, %extraClosedName, %extraMatches)
    %closeCount = ctjs.binary_static add %emitted, %one
    ctjs.store_upvalue %callee[1], %closeCount
    %closeRead = ctjs.load_upvalue %callee[1]
    %closeExtra = ctjs.binary_static add %extra, %closeRead
    ctjs.store_upvalue %callee[2], %closeExtra)MLIR");
    const auto zeroEntryCapturedSource =
        replaced(entryCapturedSource, "%emittedInitial = ctjs.constant #ctjs.number<0>",
                 "%emittedInitial = ctjs.constant #ctjs.number<4607182418800017408>");
    auto entryCaptured = mlir::parseSourceString<mlir::ModuleOp>(entryCapturedSource, &context);
    auto zeroEntryCaptured =
        mlir::parseSourceString<mlir::ModuleOp>(zeroEntryCapturedSource, &context);
    check(reinitializedCapture && externalWrite && externalRead && entryCaptured &&
              zeroEntryCaptured,
          "external cell reads, ordered writes and zero-body traversal witnesses parse");
    if (!reinitializedCapture || !externalWrite || !externalRead || !entryCaptured ||
        !zeroEntryCaptured) {
        return;
    }
    // The same reader is called before open, in the loop and on both sides of
    // close. Each invocation must read current state, including preceding writes.
    auto siblingReaderSource = replaced(
        entryCapturedSource, "    %entrySet = ctjs.get_property %element[%attributeName]",
        "    %readCount = ctjs.create_closure %callee[4] this %undefined captures %emittedCell\n"
        "    %readExtra = ctjs.create_closure %callee[5] this %undefined captures %extraCell\n"
        "    %entrySet = ctjs.get_property %element[%attributeName]");
    const auto siblingEntryEnd = siblingReaderSource.find("  ctjs.func @identity$1");
    auto siblingEntry = siblingReaderSource.substr(0, siblingEntryEnd);
    for (unsigned i = 0; i != 4; ++i) {
        siblingEntry = replaced(siblingEntry, "ctjs.cell_get %emittedCell",
                                "ctjs.call %readCount(%undefined)");
    }
    for (unsigned i = 0; i != 4; ++i) {
        siblingEntry =
            replaced(siblingEntry, "ctjs.cell_get %extraCell", "ctjs.call %readExtra(%undefined)");
    }
    siblingReaderSource = siblingEntry + siblingReaderSource.substr(siblingEntryEnd);
    siblingReaderSource = replaced(siblingReaderSource, "\n}\n", R"MLIR(
  ctjs.func @readCount$4(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 1 : i32} {
    %observed = ctjs.load_upvalue %callee[0]
    ctjs.return %observed
  }
  ctjs.func @readExtra$5(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 1 : i32} {
    %observedExtra = ctjs.load_upvalue %callee[0]
    ctjs.return %observedExtra
  }
}
)MLIR");
    const auto zeroSiblingReaderSource =
        replaced(siblingReaderSource, "%emittedInitial = ctjs.constant #ctjs.number<0>",
                 "%emittedInitial = ctjs.constant #ctjs.number<4607182418800017408>");
    auto siblingReader = mlir::parseSourceString<mlir::ModuleOp>(siblingReaderSource, &context);
    auto zeroSiblingReader =
        mlir::parseSourceString<mlir::ModuleOp>(zeroSiblingReaderSource, &context);
    check(siblingReader && zeroSiblingReader,
          "repeated sibling readers with ordinary and zero-body traversal parse");
    if (!siblingReader || !zeroSiblingReader) { return; }
    auto directSiblingReaderSource = replaced(
        replaced(siblingReaderSource, "%readCount = ctjs.create_closure %callee[4] this %undefined",
                 "%readCount = ctjs.create_closure %callee[4] this %this"),
        "%readExtra = ctjs.create_closure %callee[5] this %undefined",
        "%readExtra = ctjs.create_closure %callee[5] this %this");
    for (unsigned i = 0; i != 4; ++i) {
        directSiblingReaderSource =
            replaced(directSiblingReaderSource, "ctjs.call %readCount(%undefined)",
                     "ctjs.call_direct @readCount$4(%undefined, %undefined, %readCount)");
        directSiblingReaderSource =
            replaced(directSiblingReaderSource, "ctjs.call %readExtra(%undefined)",
                     "ctjs.call_direct @readExtra$5(%undefined, %undefined, %readExtra)");
    }
    const auto zeroDirectSiblingReaderSource =
        replaced(directSiblingReaderSource, "%emittedInitial = ctjs.constant #ctjs.number<0>",
                 "%emittedInitial = ctjs.constant #ctjs.number<4607182418800017408>");
    auto directSiblingReader =
        mlir::parseSourceString<mlir::ModuleOp>(directSiblingReaderSource, &context);
    auto zeroDirectSiblingReader =
        mlir::parseSourceString<mlir::ModuleOp>(zeroDirectSiblingReaderSource, &context);
    check(directSiblingReader && zeroDirectSiblingReader,
          "direct sibling readers with unobserved lexical receivers parse");
    if (!directSiblingReader || !zeroDirectSiblingReader) { return; }
    // Keep both former self-store refusals byte-identical as positive witnesses.
    const auto siblingCountStoreSource =
        replaced(siblingReaderSource, "%observed = ctjs.load_upvalue %callee[0]",
                 "%observed = ctjs.load_upvalue %callee[0]\n"
                 "    ctjs.store_upvalue %callee[0], %observed");
    const auto siblingExtraStoreSource =
        replaced(siblingReaderSource, "%observedExtra = ctjs.load_upvalue %callee[0]",
                 "%observedExtra = ctjs.load_upvalue %callee[0]\n"
                 "    ctjs.store_upvalue %callee[0], %observedExtra");
    auto siblingCountStore =
        mlir::parseSourceString<mlir::ModuleOp>(siblingCountStoreSource, &context);
    auto siblingExtraStore =
        mlir::parseSourceString<mlir::ModuleOp>(siblingExtraStoreSource, &context);
    // The ordinary result is the OLD count, while both captures receive new
    // values. The second write reads the first; repeated calls must see both.
    auto siblingWriterSource = replaced(
        siblingReaderSource,
        "%readCount = ctjs.create_closure %callee[4] this %undefined captures %emittedCell",
        "%readCount = ctjs.create_closure %callee[4] this %undefined captures %emittedCell, "
        "%extraCell");
    siblingWriterSource = replaced(
        siblingWriterSource,
        "ctjs.func @readCount$4(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> "
        "!ctjs.value attributes {upvalue_count = 1 : i32}",
        "ctjs.func @readCount$4(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> "
        "!ctjs.value attributes {upvalue_count = 2 : i32}");
    siblingWriterSource = replaced(siblingWriterSource,
                                   "    %observed = ctjs.load_upvalue %callee[0]\n"
                                   "    ctjs.return %observed",
                                   R"MLIR(    %observed = ctjs.load_upvalue %callee[0]
    %oldExtra = ctjs.load_upvalue %callee[1]
    %step = ctjs.constant #ctjs.number<4607182418800017408>
    %updated = ctjs.binary_static add %observed, %step
    ctjs.store_upvalue %callee[0], %updated
    %current = ctjs.load_upvalue %callee[0]
    %updatedExtra = ctjs.binary_static add %oldExtra, %current
    ctjs.store_upvalue %callee[1], %updatedExtra
    ctjs.return %observed)MLIR");
    for (const auto & [value, suffix] :
         {std::pair{"entryBefore", "before"}, std::pair{"bodyEmitted", "body"},
          std::pair{"preCloseCount", "close"}, std::pair{"finalCount", "final"}}) {
        const auto call = std::string("%") + value + " = ctjs.call %readCount(%undefined)";
        siblingWriterSource =
            replaced(siblingWriterSource, call,
                     call + "\n    %writerExtra_" + suffix + " = ctjs.cell_get %extraCell\n" +
                         "    %writerSeen_" + suffix + " = ctjs.compare strict_eq %" + value +
                         ", %writerExtra_" + suffix + "\n" + "    %writerName_" + suffix +
                         " = ctjs.constant #ctjs.string<\"writer-" + suffix + "\">\n" +
                         "    %writerEffect_" + suffix + " = ctjs.call %entrySet(%element, " +
                         "%writerName_" + suffix + ", %writerSeen_" + suffix + ")");
    }
    siblingWriterSource =
        replaced(siblingWriterSource, "    %finalExtra = ctjs.call %readExtra(%undefined)",
                 R"MLIR(    %againCount = ctjs.call %readCount(%undefined)
    %finalExtra = ctjs.call %readExtra(%undefined)
    %againSeen = ctjs.compare strict_eq %againCount, %finalExtra
    %againName = ctjs.constant #ctjs.string<"writer-again">
    %againEffect = ctjs.call %entrySet(%element, %againName, %againSeen))MLIR");
    auto directSiblingWriterSource = siblingWriterSource;
    for (unsigned i = 0; i != 5; ++i) {
        directSiblingWriterSource =
            replaced(directSiblingWriterSource, "ctjs.call %readCount(%undefined)",
                     "ctjs.call_direct @readCount$4(%undefined, %undefined, %readCount)");
    }
    const auto zeroSiblingWriterSource =
        replaced(siblingWriterSource, "%emittedInitial = ctjs.constant #ctjs.number<0>",
                 "%emittedInitial = ctjs.constant #ctjs.number<4607182418800017408>");
    const auto zeroDirectSiblingWriterSource =
        replaced(directSiblingWriterSource, "%emittedInitial = ctjs.constant #ctjs.number<0>",
                 "%emittedInitial = ctjs.constant #ctjs.number<4607182418800017408>");
    auto siblingWriter = mlir::parseSourceString<mlir::ModuleOp>(siblingWriterSource, &context);
    auto directSiblingWriter =
        mlir::parseSourceString<mlir::ModuleOp>(directSiblingWriterSource, &context);
    auto zeroSiblingWriter =
        mlir::parseSourceString<mlir::ModuleOp>(zeroSiblingWriterSource, &context);
    auto zeroDirectSiblingWriter =
        mlir::parseSourceString<mlir::ModuleOp>(zeroDirectSiblingWriterSource, &context);
    check(siblingCountStore && siblingExtraStore && siblingWriter && directSiblingWriter &&
              zeroSiblingWriter && zeroDirectSiblingWriter,
          "self-store and ordinary/direct two-cell writer witnesses parse");
    if (!siblingCountStore || !siblingExtraStore || !siblingWriter || !directSiblingWriter ||
        !zeroSiblingWriter || !zeroDirectSiblingWriter) {
        return;
    }
    // A helper loop followed by a root branch must join its saved ordinary
    // result and ordered state before the one shared custom continuation.
    auto branchWriterSource =
        replaced(siblingWriterSource,
                 "    %updated = ctjs.binary_static add %observed, %step\n"
                 "    ctjs.store_upvalue %callee[0], %updated\n"
                 "    %current = ctjs.load_upvalue %callee[0]\n"
                 "    %updatedExtra = ctjs.binary_static add %oldExtra, %current\n"
                 "    ctjs.store_upvalue %callee[1], %updatedExtra\n"
                 "    ctjs.return %observed",
                 R"MLIR(    %zero = ctjs.constant #ctjs.number<0>
    %rounds = scf.while (%round = %zero) : (!ctjs.value) -> !ctjs.value {
      %more = ctjs.compare lt %round, %step
      %again = ctjs.truthy %more
      scf.condition(%again) %round : !ctjs.value
    } do {
    ^bb0(%round: !ctjs.value):
      %loopCount = ctjs.load_upvalue %callee[0]
      %loopExtra = ctjs.load_upvalue %callee[1]
      %advanced = ctjs.binary_static add %loopCount, %step
      ctjs.store_upvalue %callee[0], %advanced
      %liveCount = ctjs.load_upvalue %callee[0]
      %advancedExtra = ctjs.binary_static add %loopExtra, %liveCount
      ctjs.store_upvalue %callee[1], %advancedExtra
      %nextRound = ctjs.binary_static add %round, %step
      scf.yield %nextRound : !ctjs.value
    }
    %afterCount = ctjs.load_upvalue %callee[0]
    %afterExtra = ctjs.load_upvalue %callee[1]
    %first = ctjs.compare strict_eq %observed, %zero
    %condition = ctjs.truthy %first
    %saved = scf.if %condition -> (!ctjs.value) {
      %updated = ctjs.binary_static add %afterCount, %rounds
      ctjs.store_upvalue %callee[0], %updated
      %current = ctjs.load_upvalue %callee[0]
      %updatedExtra = ctjs.binary_static add %afterExtra, %current
      ctjs.store_upvalue %callee[1], %updatedExtra
      scf.yield %observed : !ctjs.value
    } else {
      %updated = ctjs.binary_static add %afterCount, %oldExtra
      ctjs.store_upvalue %callee[0], %updated
      %current = ctjs.load_upvalue %callee[0]
      %updatedExtra = ctjs.binary_static add %afterExtra, %current
      ctjs.store_upvalue %callee[1], %updatedExtra
      scf.yield %oldExtra : !ctjs.value
    }
    ctjs.return %saved)MLIR");
    for (const auto & [value, suffix] :
         {std::pair{"entryBefore", "before"}, std::pair{"bodyEmitted", "body"},
          std::pair{"preCloseCount", "close"}, std::pair{"finalCount", "final"}}) {
        const auto extra = std::string("%writerExtra_") + suffix;
        const auto count = std::string("%writerCount_") + suffix;
        const auto state = std::string("%writerState_") + suffix;
        branchWriterSource = replaced(branchWriterSource, extra + " = ctjs.cell_get %extraCell",
                                      extra + " = ctjs.cell_get %extraCell\n    " + count +
                                          " = ctjs.cell_get %emittedCell\n    " + state +
                                          " = ctjs.binary_static add " + count + ", " + extra);
        branchWriterSource = replaced(
            branchWriterSource, std::string("ctjs.compare strict_eq %") + value + ", " + extra,
            std::string("ctjs.compare strict_eq %") + value + ", " + state);
    }
    branchWriterSource = replaced(
        branchWriterSource, "    %againSeen = ctjs.compare strict_eq %againCount, %finalExtra",
        R"MLIR(    %againCurrent = ctjs.cell_get %emittedCell
    %againState = ctjs.binary_static add %againCurrent, %finalExtra
    %againSeen = ctjs.compare strict_eq %againCount, %againState)MLIR");
    auto directBranchWriterSource = branchWriterSource;
    for (unsigned i = 0; i != 5; ++i) {
        directBranchWriterSource =
            replaced(directBranchWriterSource, "ctjs.call %readCount(%undefined)",
                     "ctjs.call_direct @readCount$4(%undefined, %undefined, %readCount)");
    }
    auto branchWriter = mlir::parseSourceString<mlir::ModuleOp>(branchWriterSource, &context);
    auto directBranchWriter =
        mlir::parseSourceString<mlir::ModuleOp>(directBranchWriterSource, &context);
    check(branchWriter && directBranchWriter, "ordinary/direct helper branch join twins parse");
    if (!branchWriter || !directBranchWriter) { return; }
    // Evaluate both arguments before changing a captured cell. The helper's
    // later return must keep that snapshot while its loads see the new state.
    auto argumentWriterSource = replaced(
        siblingWriterSource,
        "ctjs.func @readCount$4(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value)",
        "ctjs.func @readCount$4(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, "
        "%step: !ctjs.value, %snapshot: !ctjs.value)");
    argumentWriterSource = replaced(
        argumentWriterSource, "    %step = ctjs.constant #ctjs.number<4607182418800017408>\n", "");
    argumentWriterSource =
        replaced(argumentWriterSource, "ctjs.return %observed", "ctjs.return %snapshot");
    for (const auto & [value, suffix] :
         {std::pair{"entryBefore", "before"}, std::pair{"bodyEmitted", "body"},
          std::pair{"preCloseCount", "close"}, std::pair{"finalCount", "final"},
          std::pair{"againCount", "again"}}) {
        const auto count = std::string("%argumentCount_") + suffix;
        const auto extra = std::string("%argumentExtra_") + suffix;
        const auto step = std::string("%argumentStep_") + suffix;
        const auto current = std::string("%argumentCurrent_") + suffix;
        argumentWriterSource = replaced(
            argumentWriterSource, std::string("%") + value + " = ctjs.call %readCount(%undefined)",
            count + " = ctjs.cell_get %emittedCell\n    " + extra +
                " = ctjs.cell_get %extraCell\n    " + step + " = ctjs.binary_static add " + extra +
                ", %one\n    " + current + " = ctjs.binary_static add " + count +
                ", %one\n    ctjs.cell_set %emittedCell, " + current + "\n    %" + value +
                " = ctjs.call %readCount(%undefined, " + step + ", " + count + ")");
    }
    auto directArgumentWriterSource = argumentWriterSource;
    for (unsigned i = 0; i != 5; ++i) {
        directArgumentWriterSource =
            replaced(directArgumentWriterSource, "ctjs.call %readCount(%undefined, ",
                     "ctjs.call_direct @readCount$4(%undefined, %undefined, %readCount, ");
    }
    auto argumentWriter = mlir::parseSourceString<mlir::ModuleOp>(argumentWriterSource, &context);
    auto directArgumentWriter =
        mlir::parseSourceString<mlir::ModuleOp>(directArgumentWriterSource, &context);
    check(argumentWriter && directArgumentWriter,
          "ordinary/direct helper arguments retain snapshots across intervening state writes");
    if (!argumentWriter || !directArgumentWriter) { return; }
    // The same argument snapshots cross a helper-local break. Its empty exit
    // dispatch is removable, but both ordered cell writes remain on each edge.
    const auto breakWriterSource =
        replaced(argumentWriterSource,
                 "    %updated = ctjs.binary_static add %observed, %step\n"
                 "    ctjs.store_upvalue %callee[0], %updated\n"
                 "    %current = ctjs.load_upvalue %callee[0]\n"
                 "    %updatedExtra = ctjs.binary_static add %oldExtra, %current\n"
                 "    ctjs.store_upvalue %callee[1], %updatedExtra",
                 R"MLIR(    %helperZero = ctjs.constant #ctjs.number<0>
    %helperOne = ctjs.constant #ctjs.number<4607182418800017408>
    %helperLimit = ctjs.constant #ctjs.number<4611686018427387904>
    %helperPoison = ub.poison : i32
    %helperNormal = arith.constant 7 : i32
    %helperBreak = arith.constant 11 : i32
    %helperLoop:2 = scf.while (%trip = %helperZero, %tag = %helperPoison) : (!ctjs.value, i32) -> (!ctjs.value, i32) {
      %within = ctjs.compare lt %trip, %helperLimit
      %active = ctjs.truthy %within
      %selected:3 = scf.if %active -> (i1, !ctjs.value, i32) {
        %beforeCount = ctjs.load_upvalue %callee[0]
        %beforeExtra = ctjs.load_upvalue %callee[1]
        %updated = ctjs.binary_static add %beforeCount, %step
        ctjs.store_upvalue %callee[0], %updated
        %current = ctjs.load_upvalue %callee[0]
        %updatedExtra = ctjs.binary_static add %beforeExtra, %current
        ctjs.store_upvalue %callee[1], %updatedExtra
        %stopping = ctjs.compare strict_eq %trip, %snapshot
        %breaking = ctjs.truthy %stopping
        %nextTrip = ctjs.binary_static add %trip, %helperOne
        %branch:3 = scf.if %breaking -> (i1, !ctjs.value, i32) {
          %stop = arith.constant false
          scf.yield %stop, %nextTrip, %helperBreak : i1, !ctjs.value, i32
        } else {
          %again = arith.constant true
          scf.yield %again, %nextTrip, %helperNormal : i1, !ctjs.value, i32
        }
        scf.yield %branch#0, %branch#1, %branch#2 : i1, !ctjs.value, i32
      } else {
        %stop = arith.constant false
        scf.yield %stop, %trip, %helperNormal : i1, !ctjs.value, i32
      }
      scf.condition(%selected#0) %selected#1, %selected#2 : !ctjs.value, i32
    } do {
    ^bb0(%carried: !ctjs.value, %inactiveTag: i32):
      scf.yield %carried, %inactiveTag : !ctjs.value, i32
    }
    %helperSelector = arith.index_castui %helperLoop#1 : i32 to index
    scf.index_switch %helperSelector
    case 7 {
      scf.yield
    }
    default {
      scf.yield
    })MLIR");
    auto directBreakWriterSource = breakWriterSource;
    for (unsigned i = 0; i != 5; ++i) {
        directBreakWriterSource =
            replaced(directBreakWriterSource, "ctjs.call %readCount(%undefined, ",
                     "ctjs.call_direct @readCount$4(%undefined, %undefined, %readCount, ");
    }
    auto breakWriter = mlir::parseSourceString<mlir::ModuleOp>(breakWriterSource, &context);
    auto directBreakWriter =
        mlir::parseSourceString<mlir::ModuleOp>(directBreakWriterSource, &context);
    check(breakWriter && directBreakWriter, "ordinary/direct argument-taking helper breaks parse");
    if (!breakWriter || !directBreakWriter) { return; }
    // The same writer is reached through a captured helper and directly from
    // the entry. Both paths must keep the old argument beside current cells.
    const auto writerBegin = argumentWriterSource.find("  ctjs.func @readCount$4");
    const auto writerEnd = argumentWriterSource.find("  ctjs.func @readExtra$5", writerBegin);
    const auto writerBody = argumentWriterSource.substr(writerBegin, writerEnd - writerBegin);
    auto nestedWriterSource = replaced(
        argumentWriterSource, writerBody,
        R"MLIR(  ctjs.func @readCount$4(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %step: !ctjs.value, %snapshot: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 1 : i32} {
    %nestedWriter = ctjs.load_upvalue %callee[0]
    %nestedUndefined = ctjs.constant #ctjs.undefined
    %nestedResult = ctjs.call %nestedWriter(%nestedUndefined, %step, %snapshot)
    ctjs.return %nestedResult
  }
)MLIR" + replaced(writerBody, "@readCount$4", "@writeState$6"));
    nestedWriterSource = replaced(
        nestedWriterSource,
        "    %readCount = ctjs.create_closure %callee[4] this %undefined captures %emittedCell, "
        "%extraCell",
        "    %writeState = ctjs.create_closure %callee[6] this %undefined captures %emittedCell, "
        "%extraCell\n"
        "    %writerCell = ctjs.create_cell %writeState\n"
        "    %readCount = ctjs.create_closure %callee[4] this %undefined captures %writerCell");
    nestedWriterSource = replaced(nestedWriterSource, "%againCount = ctjs.call %readCount(",
                                  "%againCount = ctjs.call %writeState(");
    auto directNestedWriterSource = nestedWriterSource;
    for (unsigned i = 0; i != 4; ++i) {
        directNestedWriterSource =
            replaced(directNestedWriterSource, "ctjs.call %readCount(%undefined, ",
                     "ctjs.call_direct @readCount$4(%undefined, %undefined, %readCount, ");
    }
    directNestedWriterSource =
        replaced(directNestedWriterSource, "ctjs.call %writeState(%undefined, ",
                 "ctjs.call_direct @writeState$6(%undefined, %undefined, %writeState, ");
    directNestedWriterSource = replaced(
        directNestedWriterSource, "ctjs.call %nestedWriter(%nestedUndefined, ",
        "ctjs.call_direct @writeState$6(%nestedUndefined, %nestedUndefined, %nestedWriter, ");
    auto nestedWriter = mlir::parseSourceString<mlir::ModuleOp>(nestedWriterSource, &context);
    auto directNestedWriter =
        mlir::parseSourceString<mlir::ModuleOp>(directNestedWriterSource, &context);
    check(nestedWriter && directNestedWriter, "ordinary/direct shared nested writer twins parse");
    if (!nestedWriter || !directNestedWriter) { return; }
    // Passing the same writer explicitly must preserve each scalar snapshot
    // beside its current captures, just as the captured-callable path does.
    auto callableWriterSource = replaced(
        nestedWriterSource,
        "    %writerCell = ctjs.create_cell %writeState\n"
        "    %readCount = ctjs.create_closure %callee[4] this %undefined captures %writerCell",
        "    %readCount = ctjs.create_closure %callee[4] this %undefined");
    callableWriterSource = replaced(
        callableWriterSource,
        "ctjs.func @readCount$4(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, "
        "%step: !ctjs.value, %snapshot: !ctjs.value) -> !ctjs.value attributes {upvalue_count = "
        "1 : i32} {\n    %nestedWriter = ctjs.load_upvalue %callee[0]",
        "ctjs.func @readCount$4(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, "
        "%nestedWriter: !ctjs.value, %step: !ctjs.value, %snapshot: !ctjs.value) -> !ctjs.value "
        "attributes {upvalue_count = 0 : i32} {");
    for (unsigned i = 0; i != 4; ++i) {
        callableWriterSource =
            replaced(callableWriterSource, "ctjs.call %readCount(%undefined, %argumentStep_",
                     "ctjs.call %readCount(%undefined, %writeState, %argumentStep_");
    }
    auto directCallableWriterSource = callableWriterSource;
    for (unsigned i = 0; i != 4; ++i) {
        directCallableWriterSource =
            replaced(directCallableWriterSource, "ctjs.call %readCount(%undefined, ",
                     "ctjs.call_direct @readCount$4(%undefined, %undefined, %readCount, ");
    }
    directCallableWriterSource =
        replaced(directCallableWriterSource, "ctjs.call %writeState(%undefined, ",
                 "ctjs.call_direct @writeState$6(%undefined, %undefined, %writeState, ");
    directCallableWriterSource = replaced(
        directCallableWriterSource, "ctjs.call %nestedWriter(%nestedUndefined, ",
        "ctjs.call_direct @writeState$6(%nestedUndefined, %nestedUndefined, %nestedWriter, ");
    auto callableWriter = mlir::parseSourceString<mlir::ModuleOp>(callableWriterSource, &context);
    auto directCallableWriter =
        mlir::parseSourceString<mlir::ModuleOp>(directCallableWriterSource, &context);
    check(callableWriter && directCallableWriter,
          "ordinary/direct callable arguments and scalar snapshots parse");
    if (!callableWriter || !directCallableWriter) { return; }
    // Returning the writer preserves its identity at every call position;
    // invoking it still receives old arguments alongside current shared cells.
    auto returnedWriterSource =
        replaced(callableWriterSource,
                 "%nestedWriter: !ctjs.value, %step: !ctjs.value, %snapshot: !ctjs.value)",
                 "%nestedWriter: !ctjs.value)");
    returnedWriterSource =
        replaced(returnedWriterSource,
                 "    %nestedResult = ctjs.call %nestedWriter(%nestedUndefined, %step, %snapshot)\n"
                 "    ctjs.return %nestedResult",
                 "    ctjs.return %nestedWriter");
    for (const auto & [value, suffix] :
         {std::pair{"entryBefore", "before"}, std::pair{"bodyEmitted", "body"},
          std::pair{"preCloseCount", "close"}, std::pair{"finalCount", "final"}}) {
        const auto result = std::string("%returnedWriter_") + suffix;
        const auto arguments =
            std::string("%argumentStep_") + suffix + ", %argumentCount_" + suffix;
        returnedWriterSource =
            replaced(returnedWriterSource,
                     std::string("%") + value +
                         " = ctjs.call %readCount(%undefined, %writeState, " + arguments + ")",
                     result + " = ctjs.call %readCount(%undefined, %writeState)\n    %" + value +
                         " = ctjs.call " + result + "(%undefined, " + arguments + ")");
    }
    auto directReturnedWriterSource = returnedWriterSource;
    for (const auto * suffix : {"before", "body", "close", "final"}) {
        directReturnedWriterSource =
            replaced(directReturnedWriterSource, "ctjs.call %readCount(%undefined, ",
                     "ctjs.call_direct @readCount$4(%undefined, %undefined, %readCount, ");
        const auto result = std::string("%returnedWriter_") + suffix;
        directReturnedWriterSource =
            replaced(directReturnedWriterSource, "ctjs.call " + result + "(%undefined, ",
                     "ctjs.call_direct @writeState$6(%undefined, %undefined, " + result + ", ");
    }
    directReturnedWriterSource =
        replaced(directReturnedWriterSource, "ctjs.call %writeState(%undefined, ",
                 "ctjs.call_direct @writeState$6(%undefined, %undefined, %writeState, ");
    auto returnedWriter = mlir::parseSourceString<mlir::ModuleOp>(returnedWriterSource, &context);
    auto directReturnedWriter =
        mlir::parseSourceString<mlir::ModuleOp>(directReturnedWriterSource, &context);
    check(returnedWriter && directReturnedWriter,
          "ordinary/direct returned callable and scalar snapshot twins parse");
    if (!returnedWriter || !directReturnedWriter) { return; }
    // One helper formal receives distinct writers at different source calls.
    // A doubled step at body/final makes reusing the first identity observable.
    const auto otherWriterBody =
        replaced(replaced(writerBody, "@readCount$4", "@otherWriter$7"),
                 "%updated = ctjs.binary_static add %observed, %step",
                 "%doubleStep = ctjs.binary_static add %step, %step\n"
                 "    %updated = ctjs.binary_static add %observed, %doubleStep");
    auto differentCallableWriterSource = replaced(
        callableWriterSource, "    %readCount = ctjs.create_closure",
        "    %otherWriter = ctjs.create_closure %callee[7] this %undefined captures %emittedCell, "
        "%extraCell\n"
        "    %readCount = ctjs.create_closure");
    differentCallableWriterSource =
        replaced(differentCallableWriterSource, "\n}\n", "\n" + otherWriterBody + "}\n");
    for (const auto * suffix : {"body", "final"}) {
        differentCallableWriterSource = replaced(
            differentCallableWriterSource, std::string("%writeState, %argumentStep_") + suffix,
            std::string("%otherWriter, %argumentStep_") + suffix);
    }
    auto directDifferentCallableWriterSource = differentCallableWriterSource;
    for (unsigned i = 0; i != 4; ++i) {
        directDifferentCallableWriterSource =
            replaced(directDifferentCallableWriterSource, "ctjs.call %readCount(%undefined, ",
                     "ctjs.call_direct @readCount$4(%undefined, %undefined, %readCount, ");
    }
    directDifferentCallableWriterSource =
        replaced(directDifferentCallableWriterSource, "ctjs.call %writeState(%undefined, ",
                 "ctjs.call_direct @writeState$6(%undefined, %undefined, %writeState, ");
    // The forwarding helper returns the identity helper's result. Each call
    // must resolve both returns with its own argument, including after close.
    auto differentReturnedWriterSource = replaced(
        returnedWriterSource, "    %readCount = ctjs.create_closure",
        "    %otherWriter = ctjs.create_closure %callee[7] this %undefined captures %emittedCell, "
        "%extraCell\n"
        "    %readCount = ctjs.create_closure");
    differentReturnedWriterSource =
        replaced(differentReturnedWriterSource, "    %readExtra = ctjs.create_closure",
                 "    %returnerCell = ctjs.create_cell %readCount\n"
                 "    %forwardWriter = ctjs.create_closure %callee[8] this %undefined captures "
                 "%returnerCell\n"
                 "    %readExtra = ctjs.create_closure");
    differentReturnedWriterSource = replaced(
        differentReturnedWriterSource, "\n}\n",
        "\n" + otherWriterBody +
            R"MLIR(  ctjs.func @forwardWriter$8(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %writer: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 1 : i32} {
    %returner = ctjs.load_upvalue %callee[0]
    %forwardUndefined = ctjs.constant #ctjs.undefined
    %forwardResult = ctjs.call %returner(%forwardUndefined, %writer)
    ctjs.return %forwardResult
  }
}
)MLIR");
    for (const auto * suffix : {"before", "body", "close", "final"}) {
        const auto result = std::string("%returnedWriter_") + suffix;
        const auto target = llvm::StringRef(suffix) == "body" || llvm::StringRef(suffix) == "final"
                                ? "%otherWriter"
                                : "%writeState";
        differentReturnedWriterSource =
            replaced(differentReturnedWriterSource,
                     result + " = ctjs.call %readCount(%undefined, %writeState)",
                     result + " = ctjs.call %forwardWriter(%undefined, " + target + ")");
    }
    auto directDifferentReturnedWriterSource = differentReturnedWriterSource;
    for (const auto * suffix : {"before", "body", "close", "final"}) {
        directDifferentReturnedWriterSource =
            replaced(directDifferentReturnedWriterSource, "ctjs.call %forwardWriter(%undefined, ",
                     "ctjs.call_direct @forwardWriter$8(%undefined, %undefined, %forwardWriter, ");
        const auto result = std::string("%returnedWriter_") + suffix;
        const auto target = llvm::StringRef(suffix) == "body" || llvm::StringRef(suffix) == "final"
                                ? "@otherWriter$7"
                                : "@writeState$6";
        directDifferentReturnedWriterSource =
            replaced(directDifferentReturnedWriterSource, "ctjs.call " + result + "(%undefined, ",
                     std::string("ctjs.call_direct ") + target + "(%undefined, %undefined, " +
                         result + ", ");
    }
    directDifferentReturnedWriterSource =
        replaced(directDifferentReturnedWriterSource, "ctjs.call %writeState(%undefined, ",
                 "ctjs.call_direct @writeState$6(%undefined, %undefined, %writeState, ");
    directDifferentReturnedWriterSource =
        replaced(directDifferentReturnedWriterSource, "ctjs.call %returner(%forwardUndefined, ",
                 "ctjs.call_direct @readCount$4(%forwardUndefined, %forwardUndefined, %returner, ");
    auto differentCallableWriter =
        mlir::parseSourceString<mlir::ModuleOp>(differentCallableWriterSource, &context);
    auto directDifferentCallableWriter =
        mlir::parseSourceString<mlir::ModuleOp>(directDifferentCallableWriterSource, &context);
    auto differentReturnedWriter =
        mlir::parseSourceString<mlir::ModuleOp>(differentReturnedWriterSource, &context);
    auto directDifferentReturnedWriter =
        mlir::parseSourceString<mlir::ModuleOp>(directDifferentReturnedWriterSource, &context);
    check(differentCallableWriter && directDifferentCallableWriter && differentReturnedWriter &&
              directDifferentReturnedWriter,
          "ordinary/direct distinct callable arguments and forwarded returns parse");
    if (!differentCallableWriter || !directDifferentCallableWriter || !differentReturnedWriter ||
        !directDifferentReturnedWriter) {
        return;
    }
    // A captured-state branch returns either writer. The invocation must keep
    // its evaluated arguments and perform only the selected arm's state writes.
    auto joinedWriterSource = replaced(
        returnedWriterSource, "    %readCount = ctjs.create_closure %callee[4] this %undefined",
        "    %otherWriter = ctjs.create_closure %callee[7] this %undefined captures %emittedCell, "
        "%extraCell\n"
        "    %readCount = ctjs.create_closure %callee[4] this %undefined captures %emittedCell");
    joinedWriterSource = replaced(joinedWriterSource, "\n}\n", "\n" + otherWriterBody + "}\n");
    joinedWriterSource = replaced(
        joinedWriterSource,
        "%nestedWriter: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32}",
        "%nestedWriter: !ctjs.value, %alternateWriter: !ctjs.value) -> !ctjs.value attributes "
        "{upvalue_count = 1 : i32}");
    joinedWriterSource = replaced(joinedWriterSource, "    ctjs.return %nestedWriter",
                                  R"MLIR(    %selectedState = ctjs.load_upvalue %callee[0]
    %selectZero = ctjs.constant #ctjs.number<0>
    %selectPositive = ctjs.compare gt %selectedState, %selectZero
    %selectCondition = ctjs.truthy %selectPositive
    %selectedWriter = scf.if %selectCondition -> (!ctjs.value) {
      scf.yield %nestedWriter : !ctjs.value
    } else {
      scf.yield %alternateWriter : !ctjs.value
    }
    ctjs.return %selectedWriter)MLIR");
    for (const auto * suffix : {"before", "body", "close", "final"}) {
        const auto result = std::string("%returnedWriter_") + suffix;
        joinedWriterSource =
            replaced(joinedWriterSource,
                     result + " = ctjs.call %readCount(%undefined, %writeState)\n    ", "");
        const auto current = std::string("%argumentCurrent_") + suffix;
        joinedWriterSource = replaced(
            joinedWriterSource, current + " = ",
            result + " = ctjs.call %readCount(%undefined, %writeState, %otherWriter)\n    " +
                current + " = ");
    }
    auto directJoinedWriterSource = joinedWriterSource;
    for (unsigned i = 0; i != 4; ++i) {
        directJoinedWriterSource =
            replaced(directJoinedWriterSource, "ctjs.call %readCount(%undefined, ",
                     "ctjs.call_direct @readCount$4(%undefined, %undefined, %readCount, ");
    }
    directJoinedWriterSource =
        replaced(directJoinedWriterSource, "ctjs.call %writeState(%undefined, ",
                 "ctjs.call_direct @writeState$6(%undefined, %undefined, %writeState, ");
    auto joinedWriter = mlir::parseSourceString<mlir::ModuleOp>(joinedWriterSource, &context);
    auto directJoinedWriter =
        mlir::parseSourceString<mlir::ModuleOp>(directJoinedWriterSource, &context);
    check(joinedWriter && directJoinedWriter,
          "ordinary/direct branch-selected returned writers and scalar snapshots parse");
    if (!joinedWriter || !directJoinedWriter) { return; }
    // A loop carries the initial writer on zero trips and the last selected
    // writer on its backedge. The caller still evaluates scalar arguments once.
    const auto loopSelection = R"MLIR(    %selectZero = ctjs.constant #ctjs.number<0>
    %selectLimit = ctjs.constant #ctjs.number<4611686018427387904>
    %selectedLoop:2 = scf.while (%initialWriter = %nestedWriter, %trip = %selectZero) :
        (!ctjs.value, !ctjs.value) -> (!ctjs.value, !ctjs.value) {
      %keepGoing = ctjs.compare lt %trip, %selectLimit
      %loopCondition = ctjs.truthy %keepGoing
      scf.condition(%loopCondition) %initialWriter, %trip : !ctjs.value, !ctjs.value
    } do {
    ^bb0(%carriedWriter: !ctjs.value, %carriedTrip: !ctjs.value):
      %selectedState = ctjs.load_upvalue %callee[0]
      %selectPositive = ctjs.compare gt %selectedState, %selectZero
      %selectCondition = ctjs.truthy %selectPositive
      %selectedWriter = scf.if %selectCondition -> (!ctjs.value) {
        scf.yield %alternateWriter : !ctjs.value
      } else {
        scf.yield %carriedWriter : !ctjs.value
      }
      %oneTrip = ctjs.constant #ctjs.number<4607182418800017408>
      %nextTrip = ctjs.binary_static add %carriedTrip, %oneTrip
      scf.yield %selectedWriter, %nextTrip : !ctjs.value, !ctjs.value
    }
    ctjs.return %selectedLoop#0)MLIR";
    const auto loopWriterSource = [&](const std::string & source) {
        auto text = source;
        const auto begin = text.find("    %selectedState =");
        const auto end = text.find("    ctjs.return %selectedWriter", begin);
        text.replace(begin, end + std::string("    ctjs.return %selectedWriter").size() - begin,
                     loopSelection);
        return text;
    };
    const auto loopJoinedWriterSource = loopWriterSource(joinedWriterSource);
    const auto directLoopJoinedWriterSource = loopWriterSource(directJoinedWriterSource);
    auto loopJoinedWriter =
        mlir::parseSourceString<mlir::ModuleOp>(loopJoinedWriterSource, &context);
    auto directLoopJoinedWriter =
        mlir::parseSourceString<mlir::ModuleOp>(directLoopJoinedWriterSource, &context);
    auto zeroLoopJoinedWriter = mlir::parseSourceString<mlir::ModuleOp>(
        replaced(loopJoinedWriterSource,
                 "%selectLimit = ctjs.constant #ctjs.number<4611686018427387904>",
                 "%selectLimit = ctjs.constant #ctjs.number<0>"),
        &context);
    auto zeroDirectLoopJoinedWriter = mlir::parseSourceString<mlir::ModuleOp>(
        replaced(directLoopJoinedWriterSource,
                 "%selectLimit = ctjs.constant #ctjs.number<4611686018427387904>",
                 "%selectLimit = ctjs.constant #ctjs.number<0>"),
        &context);
    check(loopJoinedWriter && directLoopJoinedWriter && zeroLoopJoinedWriter &&
              zeroDirectLoopJoinedWriter,
          "ordinary/direct loop-carried and zero-trip callable snapshots parse");
    if (!loopJoinedWriter || !directLoopJoinedWriter || !zeroLoopJoinedWriter ||
        !zeroDirectLoopJoinedWriter) {
        return;
    }
    // Returning the selected writer through another helper preserves the same
    // loop dependency and all five caller snapshots, including zero trips.
    auto loopCallWriterSource = replaced(
        loopJoinedWriterSource,
        "    %readCount = ctjs.create_closure %callee[4] this %undefined captures %emittedCell",
        "    %keepWriter = ctjs.create_closure %callee[8] this %undefined\n"
        "    %keepCell = ctjs.create_cell %keepWriter\n"
        "    %readCount = ctjs.create_closure %callee[4] this %undefined captures %emittedCell, "
        "%keepCell");
    loopCallWriterSource = replaced(
        loopCallWriterSource,
        "%alternateWriter: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 1 : i32}",
        "%alternateWriter: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 2 : i32}");
    loopCallWriterSource = replaced(loopCallWriterSource, "    %selectZero =",
                                    "    %keepWriter = ctjs.load_upvalue %callee[1]\n"
                                    "    %selectZero =");
    loopCallWriterSource =
        replaced(loopCallWriterSource, "      scf.yield %selectedWriter, %nextTrip",
                 "      %keptWriter = ctjs.call %keepWriter(%nestedUndefined, %selectedWriter)\n"
                 "      scf.yield %keptWriter, %nextTrip");
    loopCallWriterSource = replaced(loopCallWriterSource, "\n}\n", R"MLIR(
  ctjs.func @keepWriter$8(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %keptWriter: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    ctjs.return %keptWriter
  }
}
)MLIR");
    auto directLoopCallWriterSource = loopCallWriterSource;
    for (unsigned i = 0; i != 4; ++i) {
        directLoopCallWriterSource =
            replaced(directLoopCallWriterSource, "ctjs.call %readCount(%undefined, ",
                     "ctjs.call_direct @readCount$4(%undefined, %undefined, %readCount, ");
    }
    directLoopCallWriterSource =
        replaced(directLoopCallWriterSource, "ctjs.call %writeState(%undefined, ",
                 "ctjs.call_direct @writeState$6(%undefined, %undefined, %writeState, ");
    directLoopCallWriterSource = replaced(
        directLoopCallWriterSource, "ctjs.call %keepWriter(%nestedUndefined, ",
        "ctjs.call_direct @keepWriter$8(%nestedUndefined, %nestedUndefined, %keepWriter, ");
    auto loopCallWriter = mlir::parseSourceString<mlir::ModuleOp>(loopCallWriterSource, &context);
    auto directLoopCallWriter =
        mlir::parseSourceString<mlir::ModuleOp>(directLoopCallWriterSource, &context);
    auto zeroLoopCallWriter = mlir::parseSourceString<mlir::ModuleOp>(
        replaced(loopCallWriterSource,
                 "%selectLimit = ctjs.constant #ctjs.number<4611686018427387904>",
                 "%selectLimit = ctjs.constant #ctjs.number<0>"),
        &context);
    auto zeroDirectLoopCallWriter = mlir::parseSourceString<mlir::ModuleOp>(
        replaced(directLoopCallWriterSource,
                 "%selectLimit = ctjs.constant #ctjs.number<4611686018427387904>",
                 "%selectLimit = ctjs.constant #ctjs.number<0>"),
        &context);
    check(loopCallWriter && directLoopCallWriter && zeroLoopCallWriter && zeroDirectLoopCallWriter,
          "ordinary/direct loop return dependencies and zero-trip snapshots parse");
    if (!loopCallWriter || !directLoopCallWriter || !zeroLoopCallWriter ||
        !zeroDirectLoopCallWriter) {
        return;
    }
    // The returned dependency crosses another fixed helper before reaching
    // the backedge; all existing loop and scalar snapshot assertions still apply.
    auto nestedLoopCallWriterSource = replaced(
        loopCallWriterSource, "    %keepWriter = ctjs.create_closure %callee[8] this %undefined",
        "    %forwardWriter = ctjs.create_closure %callee[9] this %undefined\n"
        "    %forwardCell = ctjs.create_cell %forwardWriter\n"
        "    %keepWriter = ctjs.create_closure %callee[8] this %undefined captures %forwardCell");
    nestedLoopCallWriterSource =
        replaced(nestedLoopCallWriterSource,
                 "%keptWriter: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32}",
                 "%keptWriter: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 1 : i32}");
    nestedLoopCallWriterSource = replaced(nestedLoopCallWriterSource, "    ctjs.return %keptWriter",
                                          R"MLIR(    %forwardWriter = ctjs.load_upvalue %callee[0]
    %forwardUndefined = ctjs.constant #ctjs.undefined
    %forwardResult = ctjs.call %forwardWriter(%forwardUndefined, %keptWriter)
    ctjs.return %forwardResult)MLIR");
    nestedLoopCallWriterSource = replaced(nestedLoopCallWriterSource, "\n}\n", R"MLIR(
  ctjs.func @forwardWriter$9(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %forwardedWriter: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    ctjs.return %forwardedWriter
  }
}
)MLIR");
    auto directNestedLoopCallWriterSource = nestedLoopCallWriterSource;
    for (unsigned i = 0; i != 4; ++i) {
        directNestedLoopCallWriterSource =
            replaced(directNestedLoopCallWriterSource, "ctjs.call %readCount(%undefined, ",
                     "ctjs.call_direct @readCount$4(%undefined, %undefined, %readCount, ");
    }
    directNestedLoopCallWriterSource =
        replaced(directNestedLoopCallWriterSource, "ctjs.call %writeState(%undefined, ",
                 "ctjs.call_direct @writeState$6(%undefined, %undefined, %writeState, ");
    directNestedLoopCallWriterSource = replaced(
        directNestedLoopCallWriterSource, "ctjs.call %keepWriter(%nestedUndefined, ",
        "ctjs.call_direct @keepWriter$8(%nestedUndefined, %nestedUndefined, %keepWriter, ");
    directNestedLoopCallWriterSource = replaced(
        directNestedLoopCallWriterSource, "ctjs.call %forwardWriter(%forwardUndefined, ",
        "ctjs.call_direct @forwardWriter$9(%forwardUndefined, %forwardUndefined, %forwardWriter, ");
    auto nestedLoopCallWriter =
        mlir::parseSourceString<mlir::ModuleOp>(nestedLoopCallWriterSource, &context);
    auto directNestedLoopCallWriter =
        mlir::parseSourceString<mlir::ModuleOp>(directNestedLoopCallWriterSource, &context);
    auto zeroNestedLoopCallWriter = mlir::parseSourceString<mlir::ModuleOp>(
        replaced(nestedLoopCallWriterSource,
                 "%selectLimit = ctjs.constant #ctjs.number<4611686018427387904>",
                 "%selectLimit = ctjs.constant #ctjs.number<0>"),
        &context);
    auto zeroDirectNestedLoopCallWriter = mlir::parseSourceString<mlir::ModuleOp>(
        replaced(directNestedLoopCallWriterSource,
                 "%selectLimit = ctjs.constant #ctjs.number<4611686018427387904>",
                 "%selectLimit = ctjs.constant #ctjs.number<0>"),
        &context);
    check(nestedLoopCallWriter && directNestedLoopCallWriter && zeroNestedLoopCallWriter &&
              zeroDirectNestedLoopCallWriter,
          "ordinary/direct nested loop return dependencies and zero-trip snapshots parse");
    if (!nestedLoopCallWriter || !directNestedLoopCallWriter || !zeroNestedLoopCallWriter ||
        !zeroDirectNestedLoopCallWriter) {
        return;
    }
    // Move the original selection into the nested return helper. Both formal
    // dependencies cross keep's return and the loop backedge, preserving the
    // existing state, scalar-argument and zero-trip assertions unchanged.
    const auto branchLoopSource = [&](const std::string & source) {
        auto text = replaced(source,
                             "      %selectPositive = ctjs.compare gt %selectedState, %selectZero\n"
                             "      %selectCondition = ctjs.truthy %selectPositive\n"
                             "      %selectedWriter = scf.if %selectCondition -> (!ctjs.value) {\n"
                             "        scf.yield %alternateWriter : !ctjs.value\n"
                             "      } else {\n"
                             "        scf.yield %carriedWriter : !ctjs.value\n"
                             "      }\n",
                             "");
        text = replaced(text, "%selectedWriter)\n      scf.yield %keptWriter",
                        "%carriedWriter, %alternateWriter, %selectedState)\n"
                        "      scf.yield %keptWriter");
        text = replaced(text, "%keptWriter: !ctjs.value) -> !ctjs.value attributes",
                        "%keptWriter: !ctjs.value, %keptAlternate: !ctjs.value, "
                        "%keptState: !ctjs.value) -> !ctjs.value attributes");
        text = replaced(text, "%keptWriter)\n    ctjs.return %forwardResult",
                        "%keptWriter, %keptAlternate, %keptState)\n"
                        "    ctjs.return %forwardResult");
        text = replaced(text, "%forwardedWriter: !ctjs.value) -> !ctjs.value attributes",
                        "%forwardedWriter: !ctjs.value, %forwardedAlternate: !ctjs.value, "
                        "%forwardedState: !ctjs.value) -> !ctjs.value attributes");
        return replaced(text, "    ctjs.return %forwardedWriter",
                        R"MLIR(    %selectZero = ctjs.constant #ctjs.number<0>
    %selectPositive = ctjs.compare gt %forwardedState, %selectZero
    %selectCondition = ctjs.truthy %selectPositive
    %selectedWriter = scf.if %selectCondition -> (!ctjs.value) {
      scf.yield %forwardedAlternate : !ctjs.value
    } else {
      scf.yield %forwardedWriter : !ctjs.value
    }
    ctjs.return %selectedWriter)MLIR");
    };
    const auto branchLoopCallWriterSource = branchLoopSource(nestedLoopCallWriterSource);
    const auto directBranchLoopCallWriterSource =
        branchLoopSource(directNestedLoopCallWriterSource);
    auto branchLoopCallWriter =
        mlir::parseSourceString<mlir::ModuleOp>(branchLoopCallWriterSource, &context);
    auto directBranchLoopCallWriter =
        mlir::parseSourceString<mlir::ModuleOp>(directBranchLoopCallWriterSource, &context);
    auto zeroBranchLoopCallWriter = mlir::parseSourceString<mlir::ModuleOp>(
        replaced(branchLoopCallWriterSource,
                 "%selectLimit = ctjs.constant #ctjs.number<4611686018427387904>",
                 "%selectLimit = ctjs.constant #ctjs.number<0>"),
        &context);
    auto zeroDirectBranchLoopCallWriter = mlir::parseSourceString<mlir::ModuleOp>(
        replaced(directBranchLoopCallWriterSource,
                 "%selectLimit = ctjs.constant #ctjs.number<4611686018427387904>",
                 "%selectLimit = ctjs.constant #ctjs.number<0>"),
        &context);
    check(branchLoopCallWriter && directBranchLoopCallWriter && zeroBranchLoopCallWriter &&
              zeroDirectBranchLoopCallWriter,
          "ordinary/direct branch return dependencies and zero-trip snapshots parse");
    if (!branchLoopCallWriter || !directBranchLoopCallWriter || !zeroBranchLoopCallWriter ||
        !zeroDirectBranchLoopCallWriter) {
        return;
    }
    // The return callee arrives through a formal instead of a closure capture.
    // The original loop selection, scalar snapshots and zero-trip checks apply.
    const auto formalCalleeSource = [&](const std::string & source) {
        auto text = replaced(source, "%callee[8] this %undefined captures %forwardCell",
                             "%callee[8] this %undefined");
        text = replaced(text, "%emittedCell, %keepCell", "%emittedCell, %keepCell, %forwardCell");
        text = replaced(text,
                        "%alternateWriter: !ctjs.value) -> !ctjs.value attributes "
                        "{upvalue_count = 2 : i32}",
                        "%alternateWriter: !ctjs.value) -> !ctjs.value attributes "
                        "{upvalue_count = 3 : i32}");
        text = replaced(text, "    %keepWriter = ctjs.load_upvalue %callee[1]\n",
                        "    %keepWriter = ctjs.load_upvalue %callee[1]\n"
                        "    %forwardWriter = ctjs.load_upvalue %callee[2]\n");
        text = replaced(text, "%carriedWriter, %alternateWriter, %selectedState)\n",
                        "%carriedWriter, %alternateWriter, %selectedState, %forwardWriter)\n");
        text = replaced(text,
                        "%keptState: !ctjs.value) -> !ctjs.value attributes "
                        "{upvalue_count = 1 : i32}",
                        "%keptState: !ctjs.value, %keptChooser: !ctjs.value) -> !ctjs.value "
                        "attributes {upvalue_count = 0 : i32}");
        text = replaced(text, "    %forwardWriter = ctjs.load_upvalue %callee[0]\n", "");
        if (source == branchLoopCallWriterSource) {
            return replaced(text, "ctjs.call %forwardWriter(%forwardUndefined, ",
                            "ctjs.call %keptChooser(%forwardUndefined, ");
        }
        return replaced(text, "%forwardUndefined, %forwardUndefined, %forwardWriter, ",
                        "%forwardUndefined, %forwardUndefined, %keptChooser, ");
    };
    const auto formalCalleeWriterSource = formalCalleeSource(branchLoopCallWriterSource);
    const auto directFormalCalleeWriterSource =
        formalCalleeSource(directBranchLoopCallWriterSource);
    check(!formalCalleeWriterSource.empty() && !directFormalCalleeWriterSource.empty(),
          "formal return-callee twins retain complete source modules");
    if (formalCalleeWriterSource.empty() || directFormalCalleeWriterSource.empty()) { return; }
    auto formalCalleeWriter =
        mlir::parseSourceString<mlir::ModuleOp>(formalCalleeWriterSource, &context);
    auto directFormalCalleeWriter =
        mlir::parseSourceString<mlir::ModuleOp>(directFormalCalleeWriterSource, &context);
    auto zeroFormalCalleeWriter = mlir::parseSourceString<mlir::ModuleOp>(
        replaced(formalCalleeWriterSource,
                 "%selectLimit = ctjs.constant #ctjs.number<4611686018427387904>",
                 "%selectLimit = ctjs.constant #ctjs.number<0>"),
        &context);
    auto zeroDirectFormalCalleeWriter = mlir::parseSourceString<mlir::ModuleOp>(
        replaced(directFormalCalleeWriterSource,
                 "%selectLimit = ctjs.constant #ctjs.number<4611686018427387904>",
                 "%selectLimit = ctjs.constant #ctjs.number<0>"),
        &context);
    check(formalCalleeWriter && directFormalCalleeWriter && zeroFormalCalleeWriter &&
              zeroDirectFormalCalleeWriter,
          "ordinary/direct formal return callees and zero-trip snapshots parse");
    if (!formalCalleeWriter || !directFormalCalleeWriter || !zeroFormalCalleeWriter ||
        !zeroDirectFormalCalleeWriter) {
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
    // An unconditional body return reduces the traversal to one next. Both
    // completion arms must close after the saved return expression is evaluated.
    std::string returningSource = source;
    const auto returnLoopBegin = returningSource.find("    %loop = scf.while");
    const auto returnLoopEnd = returningSource.find("    ctjs.frame_exit %frame", returnLoopBegin);
    check(returnLoopBegin != std::string::npos && returnLoopEnd != std::string::npos,
          "acyclic iterator fixture has complete replacement bounds");
    if (returnLoopBegin == std::string::npos || returnLoopEnd == std::string::npos) { return; }
    returningSource.replace(returnLoopBegin, returnLoopEnd - returnLoopBegin, R"MLIR(
    %item = ctjs.call %next(%undefined, %record)
    %done = ctjs.get_property %record[%doneName]
    %test = ctjs.truthy %done
    %saved = scf.if %test -> !ctjs.value {
      scf.yield %zero : !ctjs.value
    } else {
      %attribute = ctjs.get_property %item[%attributeName]
      %written = ctjs.call %attribute(%item, %visited, %yes)
      scf.yield %one : !ctjs.value
    }
    %loop = scf.if %test -> !ctjs.value {
      %exhaustedClose = ctjs.call %close(%undefined, %record, %normal)
      scf.yield %saved : !ctjs.value
    } else {
      %returnClose = ctjs.call %close(%undefined, %record, %normal)
      scf.yield %saved : !ctjs.value
    }
)MLIR");
    auto returning = mlir::parseSourceString<mlir::ModuleOp>(returningSource, &context);
    check(static_cast<bool>(returning), "acyclic return completion parses");
    if (!returning) { return; }
    for (auto provider :
         {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
        auto request = contract;
        request.provider = provider;
        request.moduleSha256 = hostContractFingerprint(*returning);
        mlir::OwningOpRef<mlir::ModuleOp> input(returning->clone());
        auto failure = normalizeDOMCustomIteration(*input, request, completeBudget);
        if (!failure) { failure = expandDOMHelpers(*input, request.entry, completeBudget); }
        check(!failure, "acyclic return closes normalize and expand");
        if (failure) {
            std::fprintf(stderr, "%s\n", llvm::toString(std::move(failure)).c_str());
            continue;
        }
        request.moduleSha256 = hostContractFingerprint(*input);
        const DOMEntryAnalysis proof(*input, request);
        check(proof.proved(), "acyclic return preserves complete DOM lifetime and effect proof");
        if (!proof.proved()) { std::fprintf(stderr, "%s\n", proof.reason().str().c_str()); }
    }
    for (const auto & invalid : {
             replaced(returningSource,
                      "      %returnClose = ctjs.call %close(%undefined, %record, %normal)\n", ""),
             replaced(returningSource, "    %item =",
                      "    %early = ctjs.call %close(%undefined, %record, %normal)\n    %item ="),
             replaced(returningSource, "    %item =",
                      "    %extra = ctjs.call %next(%undefined, %record)\n    %item ="),
             replaced(returningSource, "#ctjs.boolean<false>", "#ctjs.boolean<true>"),
         }) {
        check(!invalid.empty(), "invalid acyclic fixture replacement matched");
        if (invalid.empty()) { continue; }
        auto input = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
        check(static_cast<bool>(input), "invalid acyclic completion parses");
        if (!input) { continue; }
        auto request = contract;
        request.moduleSha256 = hostContractFingerprint(*input);
        auto failure = normalizeDOMCustomIteration(*input, request, completeBudget);
        check(static_cast<bool>(failure), "incomplete or reordered acyclic protocol refuses");
        if (failure) { llvm::consumeError(std::move(failure)); }
        check(hostContractFingerprint(*input) == request.moduleSha256,
              "acyclic protocol refusal retains original source");
        check(noEvidence(*input, DOMEntryAnalysis(*input, request)),
              "acyclic protocol refusal publishes no DOM evidence");
    }
    // Unlike the pure projections above, both arms observe the DOM after the
    // loop. Exhaustion closes before its read; return saves its read before close.
    std::string effectfulSource = source;
    effectfulSource.replace(returnLoopBegin, returnLoopEnd - returnLoopBegin, R"MLIR(
    %tagPoison = ub.poison : i32
    %normalTag = arith.constant 7 : i32
    %breakTag = arith.constant 11 : i32
    %hasName = ctjs.constant #ctjs.string<"hasAttribute">
    %has = ctjs.get_property %element[%hasName]
    %stopName = ctjs.constant #ctjs.string<"stop">
    %closedName = ctjs.constant #ctjs.string<"data-closed">
    %loop:2 = scf.while (%count = %zero, %tag = %tagPoison) : (!ctjs.value, i32) -> (!ctjs.value, i32) {
      %item = ctjs.call %next(%undefined, %record)
      %done = ctjs.get_property %record[%doneName]
      %test = ctjs.truthy %done
      %selected:3 = scf.if %test -> (i1, !ctjs.value, i32) {
        %stop = arith.constant false
        scf.yield %stop, %count, %normalTag : i1, !ctjs.value, i32
      } else {
        %attribute = ctjs.get_property %item[%attributeName]
        %written = ctjs.call %attribute(%item, %visited, %yes)
        %increment = ctjs.binary_static add %count, %one
        %stopping = ctjs.call %has(%element, %stopName)
        %breakTest = ctjs.truthy %stopping
        %branch:3 = scf.if %breakTest -> (i1, !ctjs.value, i32) {
          %stop = arith.constant false
          scf.yield %stop, %increment, %breakTag : i1, !ctjs.value, i32
        } else {
          %again = arith.constant true
          scf.yield %again, %increment, %normalTag : i1, !ctjs.value, i32
        }
        scf.yield %branch#0, %branch#1, %branch#2 : i1, !ctjs.value, i32
      }
      scf.condition(%selected#0) %selected#1, %selected#2 : !ctjs.value, i32
    } do {
    ^bb0(%count: !ctjs.value, %tag: i32):
      scf.yield %count, %tag : !ctjs.value, i32
    }
    %selector = arith.index_castui %loop#1 : i32 to index
    %answer = scf.index_switch %selector -> !ctjs.value
    case 7 {
      %normalClose = ctjs.call %close(%undefined, %record, %normal)
      %normalRead = ctjs.call %has(%element, %visited)
      scf.yield %normalRead : !ctjs.value
    }
    default {
      %returnRead = ctjs.call %has(%element, %closedName)
      %returnClose = ctjs.call %close(%undefined, %record, %normal)
      scf.yield %returnRead : !ctjs.value
    }
    %countName = ctjs.constant #ctjs.string<"data-count">
    %countSet = ctjs.get_property %element[%attributeName]
    %hasCount = ctjs.compare gt %loop#0, %zero
    %countWritten = ctjs.call %countSet(%element, %countName, %hasCount)
)MLIR");
    effectfulSource = replaced(effectfulSource, "ctjs.return %loop", "ctjs.return %answer");
    auto effectful = mlir::parseSourceString<mlir::ModuleOp>(effectfulSource, &context);
    check(static_cast<bool>(effectful), "effectful loop completion parses");
    if (!effectful) { return; }
    for (auto provider :
         {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
        auto request = contract;
        request.provider = provider;
        request.moduleSha256 = hostContractFingerprint(*effectful);
        mlir::OwningOpRef<mlir::ModuleOp> input(effectful->clone());
        auto failure = normalizeDOMCustomIteration(*input, request, completeBudget);
        check(!failure, "effectful loop completion normalizes for both providers");
        if (failure) {
            std::fprintf(stderr, "%s\n", llvm::toString(std::move(failure)).c_str());
            continue;
        }
        auto body = input->lookupSymbol<ctjs::FuncOp>(request.entry);
        auto join = llvm::cast<ctjs::ReturnOp>(body.getBody().front().back())
                        .getValue()
                        .getDefiningOp<mlir::scf::IfOp>();
        ctjs::CallOp countWrite;
        body.walk([&](ctjs::CallOp call) {
            if (call.getArgs().size() == 2 &&
                ctjs::constantKey(call.getArgs()[0]) == "data-count") {
                countWrite = call;
            }
        });
        auto countTest = countWrite ? countWrite.getArgs()[1].getDefiningOp<ctjs::CompareOp>()
                                    : ctjs::CompareOp{};
        auto count =
            countTest ? llvm::dyn_cast<mlir::OpResult>(countTest.getLhs()) : mlir::OpResult{};
        auto loop =
            count ? llvm::dyn_cast<mlir::scf::WhileOp>(count.getOwner()) : mlir::scf::WhileOp{};
        check(join && loop && countWrite && join->getBlock() == loop->getBlock() &&
                  countWrite->getBlock() == join->getBlock() && loop->isBeforeInBlock(join) &&
                  join->isBeforeInBlock(countWrite),
              "effectful completion remains after the traversal and retains its count");
        if (join && loop) {
            for (auto [index, arm] : llvm::enumerate(join->getRegions())) {
                llvm::SmallVector<llvm::StringRef> order;
                arm.walk([&](ctjs::CallOp call) {
                    auto method = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                    if (!method) { return; }
                    if (ctjs::constantKey(method.getKey()) == "return") {
                        order.push_back("close");
                    } else if (call.getArgs().size() == 1) {
                        order.push_back(ctjs::constantKey(call.getArgs().front()));
                    }
                });
                check(order == (index == 0
                                    ? llvm::SmallVector<llvm::StringRef>{"close", "data-visited"}
                                    : llvm::SmallVector<llvm::StringRef>{"data-closed", "close"}),
                      "normal and return arms keep their distinct read and close order");
            }
        }
        failure = expandDOMHelpers(*input, request.entry, completeBudget);
        check(!failure, "effectful loop completion expands to public DOM effects");
        if (failure) {
            std::fprintf(stderr, "%s\n", llvm::toString(std::move(failure)).c_str());
            continue;
        }
        request.moduleSha256 = hostContractFingerprint(*input);
        const DOMEntryAnalysis proof(*input, request);
        check(proof.proved(), "effectful loop completion retains the complete DOM proof");
        if (!proof.proved()) { std::fprintf(stderr, "%s\n", proof.reason().str().c_str()); }
    }
    for (const auto & invalid : {
             replaced(effectfulSource, "    %selector =",
                      "    %observer = arith.index_castui %loop#1 : i32 to index\n"
                      "    %selector ="),
             replaced(effectfulSource, "      scf.yield %count, %tag",
                      "      %observer = arith.index_castui %tag : i32 to index\n"
                      "      scf.yield %count, %tag"),
             replaced(effectfulSource, "%normalTag = arith.constant 7 : i32",
                      "%unknown = ctjs.truthy %element\n"
                      "    %normalTag = arith.extui %unknown : i1 to i32"),
             replaced(replaced(effectfulSource, "    %tagPoison =",
                               "    %payloadPoison = ub.poison : !ctjs.value\n    %tagPoison ="),
                      "%stop, %count, %normalTag", "%stop, %payloadPoison, %normalTag"),
             replaced(effectfulSource, "    default {",
                      "    case 11 {\n      scf.yield %loop#0 : !ctjs.value\n    }\n"
                      "    default {"),
         }) {
        check(!invalid.empty() && invalid != effectfulSource,
              "hostile effectful completion changes the complete fixture");
        if (invalid.empty()) { continue; }
        auto fixture = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
        check(static_cast<bool>(fixture), "hostile effectful completion parses");
        if (!fixture) { continue; }
        for (auto provider :
             {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
            auto request = contract;
            request.provider = provider;
            request.moduleSha256 = hostContractFingerprint(*fixture);
            mlir::OwningOpRef<mlir::ModuleOp> input(fixture->clone());
            auto failure = normalizeDOMCustomIteration(*input, request, completeBudget);
            check(static_cast<bool>(failure), "unproved effectful loop completion refuses");
            if (failure) { llvm::consumeError(std::move(failure)); }
            check(hostContractFingerprint(*fixture) == request.moduleSha256,
                  "refused private effectful completion preserves the original source fixture");
            if (invalid.find("case 11 {") != std::string::npos) {
                check(hostContractFingerprint(*input) == request.moduleSha256,
                      "extra-case refusal precedes private source rewriting");
            }
            request.moduleSha256 = hostContractFingerprint(*input);
            check(noEvidence(*input, DOMEntryAnalysis(*input, request)),
                  "refused private effectful completion publishes no DOM evidence");
        }
    }
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
                         *zeroTwoState,
                         *methodBreakCaptured,
                         *methodBreakReceiver,
                         *reinitializedCapture,
                         *externalWrite,
                         *externalRead,
                         *entryCaptured,
                         *zeroEntryCaptured,
                         *siblingReader,
                         *zeroSiblingReader,
                         *directSiblingReader,
                         *zeroDirectSiblingReader,
                         *siblingCountStore,
                         *siblingExtraStore,
                         *siblingWriter,
                         *directSiblingWriter,
                         *zeroSiblingWriter,
                         *zeroDirectSiblingWriter,
                         *branchWriter,
                         *directBranchWriter,
                         *argumentWriter,
                         *directArgumentWriter,
                         *breakWriter,
                         *directBreakWriter,
                         *nestedWriter,
                         *directNestedWriter,
                         *callableWriter,
                         *directCallableWriter,
                         *returnedWriter,
                         *directReturnedWriter,
                         *differentCallableWriter,
                         *directDifferentCallableWriter,
                         *differentReturnedWriter,
                         *directDifferentReturnedWriter,
                         *joinedWriter,
                         *directJoinedWriter,
                         *loopJoinedWriter,
                         *directLoopJoinedWriter,
                         *zeroLoopJoinedWriter,
                         *zeroDirectLoopJoinedWriter,
                         *loopCallWriter,
                         *directLoopCallWriter,
                         *zeroLoopCallWriter,
                         *zeroDirectLoopCallWriter,
                         *nestedLoopCallWriter,
                         *directNestedLoopCallWriter,
                         *zeroNestedLoopCallWriter,
                         *zeroDirectNestedLoopCallWriter,
                         *branchLoopCallWriter,
                         *directBranchLoopCallWriter,
                         *zeroBranchLoopCallWriter,
                         *zeroDirectBranchLoopCallWriter,
                         *formalCalleeWriter,
                         *directFormalCalleeWriter,
                         *zeroFormalCalleeWriter,
                         *zeroDirectFormalCalleeWriter}) {
        const bool siblingReads = fixture == *siblingReader || fixture == *zeroSiblingReader ||
                                  fixture == *directSiblingReader ||
                                  fixture == *zeroDirectSiblingReader ||
                                  fixture == *siblingCountStore || fixture == *siblingExtraStore;
        const bool siblingWrites = fixture == *siblingWriter || fixture == *directSiblingWriter ||
                                   fixture == *zeroSiblingWriter ||
                                   fixture == *zeroDirectSiblingWriter;
        const bool branchWrites = fixture == *branchWriter || fixture == *directBranchWriter;
        const bool argumentWrites = fixture == *argumentWriter || fixture == *directArgumentWriter;
        const bool breakWrites = fixture == *breakWriter || fixture == *directBreakWriter;
        const bool nestedWrites = fixture == *nestedWriter || fixture == *directNestedWriter;
        const bool differentWrites =
            fixture == *differentCallableWriter || fixture == *directDifferentCallableWriter ||
            fixture == *differentReturnedWriter || fixture == *directDifferentReturnedWriter;
        const bool zeroLoopWrites =
            fixture == *zeroLoopJoinedWriter || fixture == *zeroDirectLoopJoinedWriter ||
            fixture == *zeroLoopCallWriter || fixture == *zeroDirectLoopCallWriter ||
            fixture == *zeroNestedLoopCallWriter || fixture == *zeroDirectNestedLoopCallWriter ||
            fixture == *zeroBranchLoopCallWriter || fixture == *zeroDirectBranchLoopCallWriter ||
            fixture == *zeroFormalCalleeWriter || fixture == *zeroDirectFormalCalleeWriter;
        const bool loopWrites =
            fixture == *loopJoinedWriter || fixture == *directLoopJoinedWriter ||
            fixture == *loopCallWriter || fixture == *directLoopCallWriter ||
            fixture == *nestedLoopCallWriter || fixture == *directNestedLoopCallWriter ||
            fixture == *branchLoopCallWriter || fixture == *directBranchLoopCallWriter ||
            fixture == *formalCalleeWriter || fixture == *directFormalCalleeWriter ||
            zeroLoopWrites;
        const bool joinedWrites =
            fixture == *joinedWriter || fixture == *directJoinedWriter || loopWrites;
        const bool callableWrites =
            fixture == *callableWriter || fixture == *directCallableWriter ||
            fixture == *returnedWriter || fixture == *directReturnedWriter || differentWrites ||
            joinedWrites;
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
            if (branchWrites || breakWrites || nestedWrites || callableWrites) {
                auto body = input->lookupSymbol<ctjs::FuncOp>(contract.entry);
                unsigned nextCalls = 0;
                body.walk([&](ctjs::CallOp call) {
                    auto method = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                    if (!method || ctjs::constantKey(method.getKey()) != "next") { return; }
                    ++nextCalls;
                    auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(call->getParentOp());
                    check(loop && loop->getBlock() == &body.getBody().front() &&
                              call->getParentRegion() == &loop.getBefore(),
                          "helper branch joins before the root custom next continuation");
                });
                check(nextCalls == 1, "helper branches do not duplicate the custom next call");
            }
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
            if (fixture == *methodBreakCaptured || fixture == *methodBreakReceiver) {
                auto body = input->lookupSymbol<ctjs::FuncOp>("next$2");
                mlir::scf::WhileOp loop;
                mlir::scf::IfOp outer, branch;
                mlir::Value finalCount, finalExtra;
                ctjs::CompareOp exitSeen;
                llvm::SmallVector<llvm::StringRef> effects;
                bool effectsLocal = true, completion = false;
                body.walk([&](mlir::scf::WhileOp found) { loop = found; });
                body.walk([&](mlir::scf::IfOp found) {
                    (found->getParentOp() == loop ? outer : branch) = found;
                });
                body.walk([&](mlir::Operation * operation) {
                    completion |= llvm::isa<mlir::ub::PoisonOp, mlir::scf::IndexSwitchOp,
                                            mlir::arith::IndexCastUIOp>(operation);
                    if (auto constant = llvm::dyn_cast<mlir::arith::ConstantOp>(operation)) {
                        completion |= !constant.getType().isInteger(1);
                    }
                });
                body.walk([&](ctjs::SetPropertyOp set) {
                    const auto key = ctjs::constantKey(set.getKey());
                    if (key == "__ctcompile_state_0") { finalCount = set.getValue(); }
                    if (key == "__ctcompile_state_1") { finalExtra = set.getValue(); }
                });
                body.walk([&](ctjs::CallOp call) {
                    if (call.getArgs().size() != 2) { return; }
                    const auto key = ctjs::constantKey(call.getArgs()[0]);
                    if (!key.starts_with("method-")) { return; }
                    effects.push_back(key);
                    auto * region = call->getParentRegion();
                    effectsLocal &=
                        branch && (key == "method-break"      ? region == &branch.getThenRegion()
                                   : key == "method-continue" ? region == &branch.getElseRegion()
                                                              : region == &body.getBody());
                    if (key == "method-exit") {
                        exitSeen = call.getArgs()[1].getDefiningOp<ctjs::CompareOp>();
                    }
                });
                const bool shape = loop && outer && branch && loop.getNumOperands() == 3 &&
                                   loop.getNumResults() == 3 &&
                                   loop.getBeforeArguments().size() == 3 &&
                                   loop.getAfterArguments().size() == 3 &&
                                   outer.getNumResults() == 4 && branch.getNumResults() == 4;
                check(shape && !completion,
                      "empty method exit dispatch drops only the dead completion tag");
                if (shape) {
                    const auto before = loop.getBeforeArguments();
                    const auto after = loop.getAfterArguments();
                    const auto then = branch.getThenRegion().front().back().getOperands();
                    const auto otherwise = branch.getElseRegion().front().back().getOperands();
                    const auto exhausted = outer.getElseRegion().front().back().getOperands();
                    const auto yielded = loop.getAfter().front().back().getOperands();
                    auto trip = then[1].getDefiningOp<ctjs::BinaryStaticOp>();
                    auto count = then[2].getDefiningOp<ctjs::BinaryStaticOp>();
                    auto extra = then[3].getDefiningOp<ctjs::BinaryStaticOp>();
                    auto exitCount = finalCount ? finalCount.getDefiningOp<ctjs::BinaryStaticOp>()
                                                : ctjs::BinaryStaticOp{};
                    check(trip && trip.getLhs() == before[0] && count &&
                              count.getLhs() == before[1] && extra && extra.getLhs() == before[2] &&
                              extra.getRhs() == then[2] &&
                              llvm::equal(then.drop_front(), otherwise.drop_front()) &&
                              exhausted[1] == before[0] && exhausted[2] == before[1] &&
                              exhausted[3] == before[2] && llvm::equal(yielded, after),
                          "break and continue retain ordered writes and exhaustion keeps state");
                    check(exitCount && exitCount.getLhs() == loop.getResult(1) &&
                              exitCount.getRhs() == loop.getResult(0) &&
                              finalExtra == loop.getResult(2) && exitSeen &&
                              exitSeen.getLhs() == loop.getResult(0) &&
                              exitSeen.getRhs() == loop.getResult(2),
                          "post-break observations use the latest counter and both state slots");
                }
                check(effectsLocal && effects ==
                                          llvm::SmallVector<llvm::StringRef>{
                                              "method-break", "method-continue", "method-exit"},
                      "method break and continue keep distinct effects before the exit effect");
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
            if (branchWrites) {
                auto body = input->lookupSymbol<ctjs::FuncOp>(contract.entry);
                llvm::SmallVector<llvm::StringRef> effects;
                bool ordered = true, boxed = false;
                body.walk([&](mlir::Operation * operation) {
                    boxed |=
                        llvm::isa<ctjs::CreateObjectOp, ctjs::CreateCellOp, ctjs::CreateClosureOp,
                                  ctjs::CellGetOp, ctjs::CellSetOp, ctjs::LoadUpvalueOp,
                                  ctjs::StoreUpvalueOp, ctjs::CallDirectOp>(operation);
                    auto call = llvm::dyn_cast<ctjs::CallOp>(operation);
                    if (!call || call.getArgs().size() != 2) { return; }
                    const auto name = ctjs::constantKey(call.getArgs()[0]);
                    if (!name.starts_with("writer-")) { return; }
                    effects.push_back(name);
                    auto seen = call.getArgs()[1].getDefiningOp<ctjs::CompareOp>();
                    auto saved =
                        seen ? llvm::dyn_cast<mlir::OpResult>(seen.getLhs()) : mlir::OpResult{};
                    auto state = seen ? seen.getRhs().getDefiningOp<ctjs::BinaryStaticOp>()
                                      : ctjs::BinaryStaticOp{};
                    auto count =
                        state ? llvm::dyn_cast<mlir::OpResult>(state.getLhs()) : mlir::OpResult{};
                    auto extra =
                        state ? llvm::dyn_cast<mlir::OpResult>(state.getRhs()) : mlir::OpResult{};
                    auto join = saved ? llvm::dyn_cast<mlir::scf::IfOp>(saved.getOwner())
                                      : mlir::scf::IfOp{};
                    if (!join || join.getNumResults() < 3 || join.getNumResults() > 4 || !count ||
                        !extra || count.getOwner() != join || extra.getOwner() != join ||
                        saved.getResultNumber() != 0 ||
                        count.getResultNumber() != join.getNumResults() - 2 ||
                        extra.getResultNumber() != join.getNumResults() - 1) {
                        ordered = false;
                        return;
                    }
                    auto truth = join.getCondition().getDefiningOp<ctjs::TruthyOp>();
                    auto first = truth ? truth.getValue().getDefiningOp<ctjs::CompareOp>()
                                       : ctjs::CompareOp{};
                    for (auto & region : join->getRegions()) {
                        auto values = region.front().back().getOperands();
                        const auto current = values.take_back(2);
                        auto count = current[0].getDefiningOp<ctjs::BinaryStaticOp>();
                        auto nextExtra = current[1].getDefiningOp<ctjs::BinaryStaticOp>();
                        auto loopCount = count ? llvm::dyn_cast<mlir::OpResult>(count.getLhs())
                                               : mlir::OpResult{};
                        auto loopExtra = nextExtra
                                             ? llvm::dyn_cast<mlir::OpResult>(nextExtra.getLhs())
                                             : mlir::OpResult{};
                        auto loop = loopCount
                                        ? llvm::dyn_cast<mlir::scf::WhileOp>(loopCount.getOwner())
                                        : mlir::scf::WhileOp{};
                        ordered &= first && loop && loopExtra && loopExtra.getOwner() == loop &&
                                   loopExtra != loopCount && nextExtra.getRhs() == current[0] &&
                                   llvm::is_contained(loop.getInits(), values[0]);
                        if (join.getNumResults() == 4) {
                            ordered &=
                                values[1] == join.getThenRegion().front().back().getOperand(1);
                        }
                        if (&region == &join.getThenRegion()) {
                            ordered &= first && values[0] == first.getLhs();
                        } else {
                            ordered &= count && values[0] == count.getRhs();
                        }
                    }
                });
                check(ordered && effects ==
                                     llvm::SmallVector<llvm::StringRef>{
                                         "writer-before", "writer-body", "writer-close",
                                         "writer-final", "writer-again"},
                      "every helper branch joins old returns and ordered cells at its call site");
                check(!boxed && !input->lookupSymbol<ctjs::FuncOp>("readCount$4") &&
                          !input->lookupSymbol<ctjs::FuncOp>("readExtra$5"),
                      "joined helper results and state need no boxed protocol or closures");
            }
            if (fixture == *entryCaptured || fixture == *zeroEntryCaptured || siblingReads) {
                auto body = input->lookupSymbol<ctjs::FuncOp>(contract.entry);
                auto returned = llvm::cast<ctjs::ReturnOp>(body.getBody().front().back());
                auto answer = returned.getValue().getDefiningOp<ctjs::BinaryStaticOp>();
                auto total = answer ? answer.getRhs().getDefiningOp<ctjs::BinaryStaticOp>()
                                    : ctjs::BinaryStaticOp{};
                auto count =
                    total ? llvm::dyn_cast<mlir::OpResult>(total.getLhs()) : mlir::OpResult{};
                auto extra =
                    total ? llvm::dyn_cast<mlir::OpResult>(total.getRhs()) : mlir::OpResult{};
                auto close =
                    count ? llvm::dyn_cast<mlir::scf::IfOp>(count.getOwner()) : mlir::scf::IfOp{};
                ctjs::CompareOp beforeClose, bodySeen;
                llvm::SmallVector<llvm::StringRef> effects;
                body.walk([&](ctjs::CallOp call) {
                    if (call.getArgs().size() != 2) { return; }
                    const auto name = ctjs::constantKey(call.getArgs()[0]);
                    if (!name.starts_with("entry-")) { return; }
                    effects.push_back(name);
                    auto seen = call.getArgs()[1].getDefiningOp<ctjs::CompareOp>();
                    if (name == "entry-before-close") { beforeClose = seen; }
                    if (name == "entry-body") { bodySeen = seen; }
                });
                check(close && close.getNumResults() == 2 && extra && extra.getOwner() == close &&
                          count.getResultNumber() == 0 && extra.getResultNumber() == 1,
                      "post-close observations use the joined current count and extra state");
                if (close && close.getNumResults() == 2) {
                    const auto exhausted = close.getThenRegion().front().back().getOperands();
                    const auto closed = close.getElseRegion().front().back().getOperands();
                    auto closedCount = closed[0].getDefiningOp<ctjs::BinaryStaticOp>();
                    auto closedExtra = closed[1].getDefiningOp<ctjs::BinaryStaticOp>();
                    check(
                        beforeClose && closedCount && closedExtra && exhausted[0] != exhausted[1] &&
                            beforeClose.getLhs() == exhausted[0] &&
                            beforeClose.getRhs() == exhausted[1] &&
                            closedCount.getLhs() == exhausted[0] &&
                            closedExtra.getLhs() == exhausted[1] &&
                            closedExtra.getRhs() == closed[0],
                        "exhaustion keeps final next state and break keeps ordered return writes");
                }
                check(bodySeen && bodySeen.getLhs() == bodySeen.getRhs() &&
                          bodySeen.getLhs().getDefiningOp<ctjs::BinaryStaticOp>() &&
                          effects == llvm::SmallVector<llvm::StringRef>{"entry-before",
                                                                        "entry-body",
                                                                        "entry-before-close"},
                      "entry body reload sees its preceding write without moving observations");
                if (siblingReads) {
                    check(!input->lookupSymbol<ctjs::FuncOp>("readCount$4") &&
                              !input->lookupSymbol<ctjs::FuncOp>("readExtra$5"),
                          "repeated sibling readers retire after all call positions expand");
                }
            }
            if (argumentWrites || breakWrites || nestedWrites || callableWrites) {
                auto body = input->lookupSymbol<ctjs::FuncOp>(contract.entry);
                llvm::SmallVector<llvm::StringRef> effects;
                mlir::Value finalSnapshot, finalCount, finalExtra;
                bool ordered = true, boxed = false;
                body.walk([&](mlir::Operation * operation) {
                    boxed |=
                        llvm::isa<ctjs::CreateObjectOp, ctjs::CreateCellOp, ctjs::CreateClosureOp,
                                  ctjs::CellGetOp, ctjs::CellSetOp, ctjs::LoadUpvalueOp,
                                  ctjs::StoreUpvalueOp, ctjs::CallDirectOp>(operation);
                    if (breakWrites) {
                        boxed |= llvm::isa<mlir::ub::PoisonOp, mlir::scf::IndexSwitchOp,
                                           mlir::arith::IndexCastUIOp>(operation);
                    }
                    auto call = llvm::dyn_cast<ctjs::CallOp>(operation);
                    if (!call || call.getArgs().size() != 2) { return; }
                    const auto name = ctjs::constantKey(call.getArgs()[0]);
                    if (!name.starts_with("writer-")) { return; }
                    effects.push_back(name);
                    auto seen = call.getArgs()[1].getDefiningOp<ctjs::CompareOp>();
                    if (joinedWrites && name != "writer-again") {
                        auto extra =
                            seen ? llvm::dyn_cast<mlir::OpResult>(seen.getRhs()) : mlir::OpResult{};
                        auto join = extra ? llvm::dyn_cast<mlir::scf::IfOp>(extra.getOwner())
                                          : mlir::scf::IfOp{};
                        if (!join || join.getElseRegion().empty()) {
                            ordered = false;
                            return;
                        }
                        auto returned = llvm::dyn_cast<mlir::OpResult>(seen.getLhs());
                        auto truth = join.getCondition().getDefiningOp<ctjs::TruthyOp>();
                        auto dispatch = truth ? truth.getValue().getDefiningOp<ctjs::CompareOp>()
                                              : ctjs::CompareOp{};
                        auto selected = dispatch ? llvm::dyn_cast<mlir::OpResult>(dispatch.getLhs())
                                                 : mlir::OpResult{};
                        auto selector = selected
                                            ? llvm::dyn_cast<mlir::scf::IfOp>(selected.getOwner())
                                            : mlir::scf::IfOp{};
                        auto selectorLoop =
                            selected ? llvm::dyn_cast<mlir::scf::WhileOp>(selected.getOwner())
                                     : mlir::scf::WhileOp{};
                        if (loopWrites) {
                            if (!selectorLoop || selectorLoop.getNumResults() != 5 ||
                                selected.getResultNumber() != 0) {
                                ordered = false;
                                return;
                            }
                            auto carried = selectorLoop.getAfter()
                                               .front()
                                               .back()
                                               .getOperand(0)
                                               .getDefiningOp<mlir::scf::IfOp>();
                            if (!carried || carried.getNumResults() != 4) {
                                ordered = false;
                                return;
                            }
                            selector = carried;
                            auto initialTag =
                                selectorLoop.getInits()[0].getDefiningOp<ctjs::ConstantOp>();
                            auto otherTag = selector.getThenRegion()
                                                .front()
                                                .back()
                                                .getOperand(0)
                                                .getDefiningOp<ctjs::ConstantOp>();
                            auto initial =
                                initialTag ? llvm::dyn_cast<ctjs::NumberAttr>(initialTag.getValue())
                                           : ctjs::NumberAttr{};
                            auto other = otherTag
                                             ? llvm::dyn_cast<ctjs::NumberAttr>(otherTag.getValue())
                                             : ctjs::NumberAttr{};
                            auto loopTruth = llvm::cast<mlir::scf::ConditionOp>(
                                                 selectorLoop.getBefore().front().back())
                                                 .getCondition()
                                                 .getDefiningOp<ctjs::TruthyOp>();
                            auto loopTest =
                                loopTruth ? loopTruth.getValue().getDefiningOp<ctjs::CompareOp>()
                                          : ctjs::CompareOp{};
                            auto limit = loopTest
                                             ? loopTest.getRhs().getDefiningOp<ctjs::ConstantOp>()
                                             : ctjs::ConstantOp{};
                            auto number = limit ? llvm::dyn_cast<ctjs::NumberAttr>(limit.getValue())
                                                : ctjs::NumberAttr{};
                            const auto incoming = selectorLoop.getBeforeArguments();
                            auto condition = llvm::cast<mlir::scf::ConditionOp>(
                                selectorLoop.getBefore().front().back());
                            const auto outgoing = selectorLoop.getAfterArguments();
                            const auto yield = selectorLoop.getAfter().front().back().getOperands();
                            ordered &=
                                initial && other && initial != other && number &&
                                number.getDouble() == (zeroLoopWrites ? 0.0 : 2.0) &&
                                condition.getArgs()[0] == incoming[0] &&
                                selector.getElseRegion().front().back().getOperand(0) ==
                                    outgoing[0] &&
                                llvm::equal(condition.getArgs().take_back(3),
                                            incoming.take_back(3)) &&
                                llvm::equal(yield.take_back(3),
                                            selector.getResults().take_back(3)) &&
                                llvm::all_of(selector->getRegions(), [&](mlir::Region & arm) {
                                    return llvm::equal(
                                        arm.front().back().getOperands().take_back(3),
                                        outgoing.take_back(3));
                                });
                        }
                        auto predicate =
                            selector ? selector.getCondition().getDefiningOp<ctjs::TruthyOp>()
                                     : ctjs::TruthyOp{};
                        auto choice = predicate
                                          ? predicate.getValue().getDefiningOp<ctjs::CompareOp>()
                                          : ctjs::CompareOp{};
                        if (selector && !loopWrites) {
                            for (auto [index, region] : llvm::enumerate(selector->getRegions())) {
                                auto tag = region.front()
                                               .back()
                                               .getOperand(selected.getResultNumber())
                                               .getDefiningOp<ctjs::ConstantOp>();
                                auto number = tag ? llvm::dyn_cast<ctjs::NumberAttr>(tag.getValue())
                                                  : ctjs::NumberAttr{};
                                ordered &=
                                    number && number.getDouble() == static_cast<double>(index);
                            }
                        }
                        llvm::SmallVector<mlir::Value> counts, snapshots;
                        unsigned doubled = 0;
                        for (auto & region : join->getRegions()) {
                            const auto values = region.front().back().getOperands();
                            auto updatedExtra = values[extra.getResultNumber()]
                                                    .getDefiningOp<ctjs::BinaryStaticOp>();
                            auto count =
                                updatedExtra
                                    ? updatedExtra.getRhs().getDefiningOp<ctjs::BinaryStaticOp>()
                                    : ctjs::BinaryStaticOp{};
                            auto current =
                                count ? count.getLhs().getDefiningOp<ctjs::BinaryStaticOp>()
                                      : ctjs::BinaryStaticOp{};
                            auto step = count ? count.getRhs().getDefiningOp<ctjs::BinaryStaticOp>()
                                              : ctjs::BinaryStaticOp{};
                            if (step && step.getLhs() == step.getRhs()) {
                                ++doubled;
                                ordered &= step.getKind() == ctjs::BinaryKind::Add;
                                step = step.getLhs().getDefiningOp<ctjs::BinaryStaticOp>();
                            }
                            const auto snapshot = returned && returned.getOwner() == join
                                                      ? values[returned.getResultNumber()]
                                                      : seen.getLhs();
                            auto carried =
                                updatedExtra ? llvm::dyn_cast<mlir::OpResult>(updatedExtra.getLhs())
                                             : mlir::OpResult{};
                            const bool preserved =
                                carried && selector && step &&
                                (loopWrites
                                     ? carried.getOwner() == selectorLoop &&
                                           carried.getResultNumber() == 4 &&
                                           selectorLoop.getInits()[4] == step.getLhs() &&
                                           selectorLoop.getInits()[3] == snapshot
                                     : carried.getOwner() == selector &&
                                           llvm::all_of(selector->getRegions(),
                                                        [&](mlir::Region & arm) {
                                                            return arm.front().back().getOperand(
                                                                       carried.getResultNumber()) ==
                                                                   step.getLhs();
                                                        }));
                            const bool mapped =
                                updatedExtra && count && current && step &&
                                updatedExtra.getKind() == ctjs::BinaryKind::Add &&
                                count.getKind() == ctjs::BinaryKind::Add &&
                                current.getKind() == ctjs::BinaryKind::Add &&
                                step.getKind() == ctjs::BinaryKind::Add &&
                                current.getLhs() == snapshot && current.getResult() != snapshot &&
                                preserved && choice && choice.getKind() == ctjs::CompareKind::Gt &&
                                (loopWrites
                                     ? choice.getLhs() == selectorLoop.getAfterArguments()[3] &&
                                           selectorLoop->isBeforeInBlock(current)
                                     : choice.getLhs() == snapshot &&
                                           selector->isBeforeInBlock(current));
                            ordered &= mapped;
                            if (!mapped) { return; }
                            counts.push_back(count);
                            snapshots.push_back(snapshot);
                        }
                        ordered &= doubled == 1 && snapshots[0] == snapshots[1];
                        if (name == "writer-final") {
                            finalSnapshot = seen.getLhs();
                            finalExtra = extra;
                            const auto then = join.getThenRegion().front().back().getOperands();
                            const auto otherwise =
                                join.getElseRegion().front().back().getOperands();
                            for (unsigned i = 0; i != join.getNumResults(); ++i) {
                                if (then[i] == counts[0] && otherwise[i] == counts[1]) {
                                    finalCount = join.getResult(i);
                                }
                            }
                            ordered &= static_cast<bool>(finalCount);
                        }
                        return;
                    }
                    if (breakWrites) {
                        auto extra =
                            seen ? llvm::dyn_cast<mlir::OpResult>(seen.getRhs()) : mlir::OpResult{};
                        auto loop = extra ? llvm::dyn_cast<mlir::scf::WhileOp>(extra.getOwner())
                                          : mlir::scf::WhileOp{};
                        mlir::scf::IfOp outer, branch;
                        if (loop) {
                            for (auto found : loop.getBefore().front().getOps<mlir::scf::IfOp>()) {
                                outer = found;
                            }
                        }
                        if (outer) {
                            for (auto found :
                                 outer.getThenRegion().front().getOps<mlir::scf::IfOp>()) {
                                branch = found;
                            }
                        }
                        if (!loop || !outer || !branch || loop.getNumResults() < 3 ||
                            loop.getNumResults() > 4 ||
                            extra.getResultNumber() != loop.getNumResults() - 1) {
                            ordered = false;
                            return;
                        }
                        const auto before = loop.getBeforeArguments();
                        const auto initial = loop.getInits().take_back(2);
                        const auto then = branch.getThenRegion().front().back().getOperands();
                        const auto otherwise = branch.getElseRegion().front().back().getOperands();
                        const auto exhausted = outer.getElseRegion().front().back().getOperands();
                        if (then.size() != before.size() + 1 || otherwise.size() != then.size() ||
                            exhausted.size() != then.size()) {
                            ordered = false;
                            return;
                        }
                        const auto state = then.take_back(2);
                        auto current = initial[0].getDefiningOp<ctjs::BinaryStaticOp>();
                        auto count = state[0].getDefiningOp<ctjs::BinaryStaticOp>();
                        auto nextExtra = state[1].getDefiningOp<ctjs::BinaryStaticOp>();
                        auto step = count ? count.getRhs().getDefiningOp<ctjs::BinaryStaticOp>()
                                          : ctjs::BinaryStaticOp{};
                        auto truth = branch.getCondition().getDefiningOp<ctjs::TruthyOp>();
                        auto stopping = truth ? truth.getValue().getDefiningOp<ctjs::CompareOp>()
                                              : ctjs::CompareOp{};
                        ordered &= current && count && nextExtra && step && stopping &&
                                   current.getLhs() == seen.getLhs() &&
                                   count.getLhs() == before[before.size() - 2] &&
                                   nextExtra.getLhs() == before.back() &&
                                   nextExtra.getRhs() == state[0] && step.getLhs() == initial[1] &&
                                   stopping.getLhs() == before[0] &&
                                   stopping.getRhs() == seen.getLhs() &&
                                   llvm::equal(then.drop_front(), otherwise.drop_front()) &&
                                   llvm::equal(exhausted.drop_front(), before) &&
                                   llvm::equal(loop.getAfter().front().back().getOperands(),
                                               loop.getAfterArguments());
                        if (name == "writer-final") {
                            finalSnapshot = seen.getLhs();
                            finalCount = loop.getResults()[loop.getNumResults() - 2];
                            finalExtra = extra;
                        } else if (name == "writer-again") {
                            ordered &= seen.getLhs() == finalCount && initial[1] == finalExtra;
                        }
                        return;
                    }
                    auto extra = seen ? seen.getRhs().getDefiningOp<ctjs::BinaryStaticOp>()
                                      : ctjs::BinaryStaticOp{};
                    auto count = extra ? extra.getRhs().getDefiningOp<ctjs::BinaryStaticOp>()
                                       : ctjs::BinaryStaticOp{};
                    auto current = count ? count.getLhs().getDefiningOp<ctjs::BinaryStaticOp>()
                                         : ctjs::BinaryStaticOp{};
                    auto step = count ? count.getRhs().getDefiningOp<ctjs::BinaryStaticOp>()
                                      : ctjs::BinaryStaticOp{};
                    if (differentWrites && (name == "writer-body" || name == "writer-final")) {
                        ordered &= step && step.getKind() == ctjs::BinaryKind::Add &&
                                   step.getLhs() == step.getRhs();
                        step = step ? step.getLhs().getDefiningOp<ctjs::BinaryStaticOp>()
                                    : ctjs::BinaryStaticOp{};
                    }
                    const bool mapped = extra && count && current && step &&
                                        extra.getKind() == ctjs::BinaryKind::Add &&
                                        count.getKind() == ctjs::BinaryKind::Add &&
                                        current.getKind() == ctjs::BinaryKind::Add &&
                                        step.getKind() == ctjs::BinaryKind::Add &&
                                        current.getLhs() == seen.getLhs() &&
                                        current.getResult() != seen.getLhs() &&
                                        step.getLhs() == extra.getLhs();
                    ordered &= mapped;
                    if (!mapped) { return; }
                    if (name == "writer-final") {
                        finalSnapshot = seen.getLhs();
                        finalCount = count;
                        finalExtra = extra;
                    } else if (name == "writer-again") {
                        ordered &= seen.getLhs() == finalCount && step.getLhs() == finalExtra;
                    }
                });
                auto returned = llvm::cast<ctjs::ReturnOp>(body.getBody().front().back());
                auto answer = returned.getValue().getDefiningOp<ctjs::BinaryStaticOp>();
                auto total = answer ? answer.getRhs().getDefiningOp<ctjs::BinaryStaticOp>()
                                    : ctjs::BinaryStaticOp{};
                check(ordered && finalSnapshot && total && total.getLhs() == finalSnapshot &&
                          effects ==
                              llvm::SmallVector<llvm::StringRef>{"writer-before", "writer-body",
                                                                 "writer-close", "writer-final",
                                                                 "writer-again"},
                      "each argument keeps its evaluated SSA value beside the latest shared state");
                check(!boxed && !input->lookupSymbol<ctjs::FuncOp>("readCount$4") &&
                          !input->lookupSymbol<ctjs::FuncOp>("readExtra$5") &&
                          (!(nestedWrites || callableWrites) ||
                           !input->lookupSymbol<ctjs::FuncOp>("writeState$6")) &&
                          (!(differentWrites || joinedWrites) ||
                           (!input->lookupSymbol<ctjs::FuncOp>("otherWriter$7") &&
                            !input->lookupSymbol<ctjs::FuncOp>("forwardWriter$8"))) &&
                          !input->lookupSymbol<ctjs::FuncOp>("keepWriter$8"),
                      "argument-taking helpers retire without closures or boxed state");
            }
            if (siblingWrites) {
                auto body = input->lookupSymbol<ctjs::FuncOp>(contract.entry);
                llvm::SmallVector<llvm::StringRef> effects;
                bool ordered = true, boxed = false;
                body.walk([&](mlir::Operation * operation) {
                    boxed |=
                        llvm::isa<ctjs::CreateObjectOp, ctjs::CreateCellOp, ctjs::CreateClosureOp,
                                  ctjs::CellGetOp, ctjs::CellSetOp, ctjs::LoadUpvalueOp,
                                  ctjs::StoreUpvalueOp, ctjs::CallDirectOp>(operation);
                    auto call = llvm::dyn_cast<ctjs::CallOp>(operation);
                    if (!call || call.getArgs().size() != 2) { return; }
                    const auto name = ctjs::constantKey(call.getArgs()[0]);
                    if (!name.starts_with("writer-")) { return; }
                    effects.push_back(name);
                    auto seen = call.getArgs()[1].getDefiningOp<ctjs::CompareOp>();
                    auto extra = seen ? seen.getRhs().getDefiningOp<ctjs::BinaryStaticOp>()
                                      : ctjs::BinaryStaticOp{};
                    auto count = extra ? extra.getRhs().getDefiningOp<ctjs::BinaryStaticOp>()
                                       : ctjs::BinaryStaticOp{};
                    ordered &= extra && count && extra.getKind() == ctjs::BinaryKind::Add &&
                               count.getKind() == ctjs::BinaryKind::Add &&
                               count.getLhs() == seen.getLhs() &&
                               count.getResult() != seen.getLhs();
                });
                check(ordered && effects ==
                                     llvm::SmallVector<llvm::StringRef>{
                                         "writer-before", "writer-body", "writer-close",
                                         "writer-final", "writer-again"},
                      "each sibling call returns its old snapshot and publishes ordered writes");
                auto returned = llvm::cast<ctjs::ReturnOp>(body.getBody().front().back());
                auto answer = returned.getValue().getDefiningOp<ctjs::BinaryStaticOp>();
                auto total = answer ? answer.getRhs().getDefiningOp<ctjs::BinaryStaticOp>()
                                    : ctjs::BinaryStaticOp{};
                auto extra = total ? total.getRhs().getDefiningOp<ctjs::BinaryStaticOp>()
                                   : ctjs::BinaryStaticOp{};
                auto count = extra ? extra.getRhs().getDefiningOp<ctjs::BinaryStaticOp>()
                                   : ctjs::BinaryStaticOp{};
                auto firstExtra = extra ? extra.getLhs().getDefiningOp<ctjs::BinaryStaticOp>()
                                        : ctjs::BinaryStaticOp{};
                auto firstCount = count ? count.getLhs().getDefiningOp<ctjs::BinaryStaticOp>()
                                        : ctjs::BinaryStaticOp{};
                check(total && firstExtra && firstCount &&
                          firstExtra.getRhs() == firstCount.getResult() &&
                          firstCount.getLhs() == total.getLhs(),
                      "repeated final calls retain both latest cells and the first result");
                auto initial =
                    total ? llvm::dyn_cast<mlir::OpResult>(total.getLhs()) : mlir::OpResult{};
                auto close = initial ? llvm::dyn_cast<mlir::scf::IfOp>(initial.getOwner())
                                     : mlir::scf::IfOp{};
                check(close && close.getNumResults() == 2 && initial.getResultNumber() == 0 &&
                          firstExtra && firstExtra.getLhs() == close.getResult(1),
                      "repeated sibling writes start from both joined close-state results");
                if (close && close.getNumResults() == 2) {
                    const auto exhausted = close.getThenRegion().front().back().getOperands();
                    const auto closed = close.getElseRegion().front().back().getOperands();
                    auto closedCount = closed[0].getDefiningOp<ctjs::BinaryStaticOp>();
                    auto closedExtra = closed[1].getDefiningOp<ctjs::BinaryStaticOp>();
                    check(closedCount && closedExtra && closedCount.getLhs() == exhausted[0] &&
                              closedExtra.getLhs() == exhausted[1] &&
                              closedExtra.getRhs() == closed[0],
                          "sibling close joins preserve exhaustion and ordered return writes");
                }
                check(!boxed && !input->lookupSymbol<ctjs::FuncOp>("readCount$4") &&
                          !input->lookupSymbol<ctjs::FuncOp>("readExtra$5"),
                      "mutable siblings retire without boxed state, closures or direct calls");
            } else if (fixture == *receiver || fixture == *twoState || fixture == *captured ||
                       fixture == *twoCaptured || fixture == *initializedCapture ||
                       fixture == *reorderedCapture || fixture == *conditionalCaptured ||
                       fixture == *conditionalReceiver || fixture == *branchCaptured ||
                       fixture == *branchReceiver || fixture == *zeroCaptured ||
                       fixture == *zeroReceiver || fixture == *loopCaptured ||
                       fixture == *loopReceiver || fixture == *zeroTwoState ||
                       fixture == *methodBreakCaptured || fixture == *methodBreakReceiver ||
                       fixture == *reinitializedCapture || fixture == *externalWrite ||
                       fixture == *externalRead || fixture == *entryCaptured ||
                       fixture == *zeroEntryCaptured || siblingReads) {
                const bool two = fixture == *twoState || fixture == *twoCaptured ||
                                 fixture == *reorderedCapture || fixture == *branchCaptured ||
                                 fixture == *branchReceiver || fixture == *loopCaptured ||
                                 fixture == *loopReceiver || fixture == *zeroTwoState ||
                                 fixture == *methodBreakCaptured ||
                                 fixture == *methodBreakReceiver || fixture == *entryCaptured ||
                                 fixture == *zeroEntryCaptured || siblingReads;
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
            } else if (!branchWrites && !argumentWrites && !breakWrites && !nestedWrites &&
                       !callableWrites && fixture != *original && fixture != *withoutReturn) {
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
             replaced(nestedWriterSource, "    ctjs.return %snapshot",
                      "    %recursiveUndefined = ctjs.constant #ctjs.undefined\n"
                      "    %recursive = ctjs.call %callee(%recursiveUndefined, %step, %snapshot)\n"
                      "    ctjs.return %snapshot"),
             replaced(nestedWriterSource, "ctjs.return %nestedResult", "ctjs.return %nestedWriter"),
             replaced(nestedWriterSource, "    %entrySet =",
                      "    ctjs.cell_set %writerCell, %undefined\n    %entrySet ="),
             replaced(directNestedWriterSource, "    %nestedUndefined =",
                      "    ctjs.store_upvalue %callee[0], %nestedWriter\n"
                      "    %nestedUndefined ="),
             replaced(nestedWriterSource,
                      "%writeState = ctjs.create_closure %callee[6] this %undefined captures "
                      "%emittedCell, %extraCell",
                      "%writeState = ctjs.create_closure %callee[6] this %undefined captures "
                      "%emittedCell, %capture"),
             replaced(directNestedWriterSource,
                      "ctjs.call_direct @writeState$6(%nestedUndefined, %nestedUndefined, "
                      "%nestedWriter, ",
                      "ctjs.call_direct @readCount$4(%nestedUndefined, %nestedUndefined, "
                      "%nestedWriter, "),
             replaced(callableWriterSource, "ctjs.return %nestedResult",
                      "ctjs.return %nestedWriter"),
             replaced(directCallableWriterSource, "    %nestedUndefined =",
                      "    ctjs.store_global \"leaked\", %nestedWriter\n"
                      "    %nestedUndefined ="),
             replaced(replaced(callableWriterSource, "    %entrySet =",
                               "    %mutableWriter = ctjs.create_cell %writeState\n"
                               "    ctjs.cell_set %mutableWriter, %readCount\n"
                               "    %changedWriter = ctjs.cell_get %mutableWriter\n"
                               "    %entrySet ="),
                      "%writeState, %argumentStep_before", "%changedWriter, %argumentStep_before"),
             replaced(callableWriterSource, "%writeState, %argumentStep_before",
                      "%element, %argumentStep_before"),
             replaced(
                 replaced(directCallableWriterSource, "\n}\n",
                          "\n" + replaced(writerBody, "@readCount$4", "@otherWriter$7") + "}\n"),
                 "ctjs.call_direct @writeState$6(%nestedUndefined, %nestedUndefined, "
                 "%nestedWriter, ",
                 "ctjs.call_direct @otherWriter$7(%nestedUndefined, %nestedUndefined, "
                 "%nestedWriter, "),
             replaced(directCallableWriterSource, "%writeState, %argumentStep_final",
                      "%readExtra, %argumentStep_final"),
             replaced(callableWriterSource, "    ctjs.return %nestedResult",
                      "    %observedCallable = ctjs.binary_static add %nestedWriter, %step\n"
                      "    ctjs.return %nestedResult"),
             replaced(returnedWriterSource, "    %entryBefore =",
                      "    ctjs.store_global \"leaked\", %returnedWriter_before\n"
                      "    %entryBefore ="),
             replaced(directReturnedWriterSource, "    %finalCount =",
                      "    %observedReturned = ctjs.binary_static add %returnedWriter_final, "
                      "%argumentStep_final\n"
                      "    %finalCount ="),
             replaced(returnedWriterSource, "ctjs.return %nestedWriter",
                      "ctjs.return %nestedUndefined"),
             replaced(directReturnedWriterSource, "ctjs.return %nestedWriter",
                      "ctjs.return %callee"),
             replaced(returnedWriterSource, "    ctjs.return %nestedWriter",
                      R"MLIR(    %returnCondition = ctjs.truthy %nestedUndefined
    %mixedReturn = scf.if %returnCondition -> (!ctjs.value) {
      scf.yield %nestedWriter : !ctjs.value
    } else {
      scf.yield %nestedUndefined : !ctjs.value
    }
    ctjs.return %mixedReturn)MLIR"),
             replaced(
                 replaced(directReturnedWriterSource, "\n}\n",
                          "\n" + replaced(writerBody, "@readCount$4", "@otherWriter$7") + "}\n"),
                 "ctjs.call_direct @writeState$6(%undefined, %undefined, %returnedWriter_final, ",
                 "ctjs.call_direct @otherWriter$7(%undefined, %undefined, %returnedWriter_final, "),
             replaced(differentReturnedWriterSource,
                      "%returnedWriter_final = ctjs.call %forwardWriter(%undefined, %otherWriter)",
                      "%returnedWriter_final = ctjs.call %forwardWriter(%undefined, %element)"),
             replaced(
                 replaced(differentReturnedWriterSource, "    %entrySet =",
                          "    %mutableWriter = ctjs.create_cell %otherWriter\n"
                          "    ctjs.cell_set %mutableWriter, %writeState\n"
                          "    %changedWriter = ctjs.cell_get %mutableWriter\n"
                          "    %entrySet ="),
                 "%returnedWriter_final = ctjs.call %forwardWriter(%undefined, %otherWriter)",
                 "%returnedWriter_final = ctjs.call %forwardWriter(%undefined, %changedWriter)"),
             replaced(differentReturnedWriterSource, "    %finalCount =",
                      "    ctjs.store_global \"leaked\", %returnedWriter_final\n"
                      "    %finalCount ="),
             replaced(directDifferentReturnedWriterSource, "    %finalCount =",
                      "    %observedReturned = ctjs.binary_static add %returnedWriter_final, "
                      "%argumentStep_final\n"
                      "    %finalCount ="),
             replaced(
                 directDifferentReturnedWriterSource,
                 "ctjs.call_direct @otherWriter$7(%undefined, %undefined, %returnedWriter_final, ",
                 "ctjs.call_direct @writeState$6(%undefined, %undefined, %returnedWriter_final, "),
             replaced(differentReturnedWriterSource,
                      "%forwardResult = ctjs.call %returner(%forwardUndefined, %writer)",
                      "%forwardResult = ctjs.call %callee(%forwardUndefined, %writer)"),
             replaced(
                 joinedWriterSource,
                 "%returnedWriter_final = ctjs.call %readCount(%undefined, %writeState, "
                 "%otherWriter)",
                 "%returnedWriter_final = ctjs.call %readCount(%undefined, %writeState, %element)"),
             replaced(joinedWriterSource, "scf.yield %alternateWriter : !ctjs.value",
                      "scf.yield %nestedUndefined : !ctjs.value"),
             replaced(replaced(joinedWriterSource, "    %entrySet =",
                               "    %mutableWriter = ctjs.create_cell %otherWriter\n"
                               "    ctjs.cell_set %mutableWriter, %writeState\n"
                               "    %changedWriter = ctjs.cell_get %mutableWriter\n"
                               "    %entrySet ="),
                      "%returnedWriter_final = ctjs.call %readCount(%undefined, %writeState, "
                      "%otherWriter)",
                      "%returnedWriter_final = ctjs.call %readCount(%undefined, %writeState, "
                      "%changedWriter)"),
             replaced(joinedWriterSource, "    %finalCount =",
                      "    ctjs.store_global \"leaked\", %returnedWriter_final\n"
                      "    %finalCount ="),
             replaced(directJoinedWriterSource, "    %finalCount =",
                      "    %observedReturned = ctjs.binary_static add %returnedWriter_final, "
                      "%argumentStep_final\n"
                      "    %finalCount ="),
             replaced(
                 directJoinedWriterSource, "ctjs.call %returnedWriter_final(%undefined, ",
                 "ctjs.call_direct @writeState$6(%undefined, %undefined, %returnedWriter_final, "),
         }) {
        auto fixture = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
        check(static_cast<bool>(fixture), "hostile nested helper witness parses");
        if (!fixture) { continue; }
        for (auto provider :
             {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
            mlir::OwningOpRef<mlir::ModuleOp> input(fixture->clone());
            auto request = contract;
            request.provider = provider;
            request.moduleSha256 = hostContractFingerprint(*input);
            auto failure = normalizeDOMCustomIteration(*input, request, completeBudget);
            check(static_cast<bool>(failure),
                  "nested recursion, escaped or mutable callables and unproved targets refuse");
            if (failure) { llvm::consumeError(std::move(failure)); }
            check(hostContractFingerprint(*input) == request.moduleSha256 &&
                      noEvidence(*input, DOMEntryAnalysis(*input, request)),
                  "refused nested helpers preserve source and publish no evidence");
        }
    }
    for (const auto & source : {loopJoinedWriterSource, directLoopJoinedWriterSource}) {
        for (const auto & invalid : {
                 replaced(source, "%initialWriter = %nestedWriter",
                          "%initialWriter = %nestedUndefined"),
                 replaced(replaced(source, "    %selectZero =",
                                   "    %unknownWriter = ctjs.load_global \"unknownWriter\"\n"
                                   "    %selectZero ="),
                          "%initialWriter = %nestedWriter", "%initialWriter = %unknownWriter"),
                 replaced(source, "scf.yield %selectedWriter, %nextTrip",
                          "scf.yield %selectZero, %nextTrip"),
                 replaced(replaced(source, "    %selectZero =",
                                   "    %unknownWriter = ctjs.load_global \"unknownWriter\"\n"
                                   "    %selectZero ="),
                          "scf.yield %selectedWriter, %nextTrip",
                          "scf.yield %unknownWriter, %nextTrip"),
                 replaced(source, "      %keepGoing =",
                          "      %observedInitial = ctjs.compare eq %initialWriter, %nestedWriter\n"
                          "      %keepGoing ="),
                 replaced(source, "      %oneTrip =",
                          "      ctjs.store_global \"leaked\", %carriedWriter\n"
                          "      %oneTrip ="),
                 replaced(source, "      %oneTrip =",
                          "      %observedSelected = ctjs.binary_static add %selectedWriter, "
                          "%carriedTrip\n"
                          "      %oneTrip ="),
                 replaced(source, "    %finalCount =",
                          "    ctjs.store_global \"leaked\", %returnedWriter_final\n"
                          "    %finalCount ="),
                 replaced(source, "ctjs.call %returnedWriter_final(%undefined, ",
                          "ctjs.call_direct @writeState$6(%undefined, %undefined, "
                          "%returnedWriter_final, "),
             }) {
            auto fixture = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
            check(static_cast<bool>(fixture), "hostile loop-carried helper witness parses");
            if (!fixture) { continue; }
            for (auto provider : {HostContract::Provider::ctbrowserDOM,
                                  HostContract::Provider::ctbrowserDOMSession}) {
                mlir::OwningOpRef<mlir::ModuleOp> input(fixture->clone());
                auto request = contract;
                request.provider = provider;
                request.moduleSha256 = hostContractFingerprint(*input);
                auto failure = normalizeDOMCustomIteration(*input, request, completeBudget);
                check(static_cast<bool>(failure),
                      "loop callable initializers, backedges and every observer must be proved");
                if (failure) { llvm::consumeError(std::move(failure)); }
                check(hostContractFingerprint(*input) == request.moduleSha256 &&
                          noEvidence(*input, DOMEntryAnalysis(*input, request)),
                      "refused loop helper preserves source and publishes no evidence");
            }
        }
    }
    for (const auto & source : {loopCallWriterSource, directLoopCallWriterSource}) {
        for (const auto & invalid : {
                 replaced(source, "    ctjs.return %keptWriter",
                          "    %unknownWriter = ctjs.load_global \"unknownWriter\"\n"
                          "    ctjs.return %unknownWriter"),
                 replaced(source, "    ctjs.return %keptWriter",
                          R"MLIR(    %unknownCondition = ctjs.truthy %keptWriter
    %notCallable = ctjs.constant #ctjs.number<0>
    %mixedWriter = scf.if %unknownCondition -> (!ctjs.value) {
      scf.yield %keptWriter : !ctjs.value
    } else {
      scf.yield %notCallable : !ctjs.value
    }
    ctjs.return %mixedWriter)MLIR"),
                 replaced(source, "    ctjs.return %keptWriter",
                          "    ctjs.store_global \"leaked\", %keptWriter\n"
                          "    ctjs.return %keptWriter"),
                 replaced(source, "    ctjs.return %keptWriter",
                          "    %observedWriter = ctjs.compare eq %keptWriter, %keptWriter\n"
                          "    ctjs.return %keptWriter"),
                 replaced(source, "    ctjs.return %keptWriter",
                          "    %undefined = ctjs.constant #ctjs.undefined\n"
                          "    %recursiveEffect = ctjs.call %callee(%undefined, %keptWriter)\n"
                          "    ctjs.return %keptWriter"),
                 // A resolved direct call carries the resolver's undefined padding.
                 source == loopCallWriterSource
                     ? replaced(source, ", %selectedWriter)\n      scf.yield %keptWriter",
                                ")\n      scf.yield %keptWriter")
                     : replaced(source, "%selectedWriter)\n      scf.yield %keptWriter",
                                "%nestedUndefined)\n      scf.yield %keptWriter"),
                 replaced(source, "%selectedWriter)\n      scf.yield %keptWriter",
                          "%selectZero)\n      scf.yield %keptWriter"),
                 replaced(replaced(source, "    %selectZero =",
                                   "    %unknownWriter = ctjs.load_global \"unknownWriter\"\n"
                                   "    %selectZero ="),
                          "%selectedWriter)\n      scf.yield %keptWriter",
                          "%unknownWriter)\n      scf.yield %keptWriter"),
                 replaced(source, "      scf.yield %keptWriter, %nextTrip",
                          "      ctjs.store_global \"leaked\", %keptWriter\n"
                          "      scf.yield %keptWriter, %nextTrip"),
                 replaced(source, "    %finalCount =",
                          "    ctjs.store_global \"leaked\", %returnedWriter_final\n"
                          "    %finalCount ="),
                 source == loopCallWriterSource
                     ? replaced(source, "ctjs.call %keepWriter(%nestedUndefined, ",
                                "ctjs.call_direct @custom$0(%nestedUndefined, %nestedUndefined, "
                                "%keepWriter, ")
                     : replaced(source, "ctjs.call_direct @keepWriter$8(",
                                "ctjs.call_direct @custom$0("),
             }) {
            check(!invalid.empty() && invalid != source,
                  "loop call-result control changes its source");
            if (invalid.empty() || invalid == source) { continue; }
            auto fixture = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
            check(static_cast<bool>(fixture), "hostile loop call-result dependency parses");
            if (!fixture) { continue; }
            for (auto provider : {HostContract::Provider::ctbrowserDOM,
                                  HostContract::Provider::ctbrowserDOMSession}) {
                mlir::OwningOpRef<mlir::ModuleOp> input(fixture->clone());
                auto request = contract;
                request.provider = provider;
                request.moduleSha256 = hostContractFingerprint(*input);
                auto failure = normalizeDOMCustomIteration(*input, request, completeBudget);
                check(static_cast<bool>(failure),
                      "every loop return dependency, actual and observer must be proved");
                if (failure) { llvm::consumeError(std::move(failure)); }
                check(hostContractFingerprint(*input) == request.moduleSha256 &&
                          noEvidence(*input, DOMEntryAnalysis(*input, request)),
                      "refused loop return dependency preserves source and publishes no evidence");
            }
        }
    }
    for (const auto & source : {nestedLoopCallWriterSource, directNestedLoopCallWriterSource}) {
        for (const auto & invalid : {
                 replaced(source, "    ctjs.return %forwardedWriter",
                          "    %unknownWriter = ctjs.load_global \"unknownWriter\"\n"
                          "    ctjs.return %unknownWriter"),
                 replaced(replaced(source, "    %forwardResult =",
                                   "    %unknownWriter = ctjs.load_global \"unknownWriter\"\n"
                                   "    %forwardResult ="),
                          "%keptWriter)\n    ctjs.return %forwardResult",
                          "%unknownWriter)\n    ctjs.return %forwardResult"),
                 // Direct calls retain the resolver's undefined argument padding.
                 source == nestedLoopCallWriterSource
                     ? replaced(source, ", %keptWriter)\n    ctjs.return %forwardResult",
                                ")\n    ctjs.return %forwardResult")
                     : replaced(source, "%keptWriter)\n    ctjs.return %forwardResult",
                                "%forwardUndefined)\n    ctjs.return %forwardResult"),
                 replaced(source, "    ctjs.return %forwardResult",
                          "    ctjs.store_global \"leaked\", %forwardResult\n"
                          "    ctjs.return %forwardResult"),
                 replaced(
                     source, "    ctjs.return %forwardedWriter",
                     "    %observedWriter = ctjs.compare eq %forwardedWriter, %forwardedWriter\n"
                     "    ctjs.return %forwardedWriter"),
                 replaced(source, "    ctjs.return %forwardedWriter",
                          "    %undefined = ctjs.constant #ctjs.undefined\n"
                          "    %recursiveEffect = ctjs.call %callee(%undefined, %forwardedWriter)\n"
                          "    ctjs.return %forwardedWriter"),
                 source == nestedLoopCallWriterSource
                     ? replaced(
                           source, "ctjs.call %forwardWriter(%forwardUndefined, ",
                           "ctjs.call_direct @keepWriter$8(%forwardUndefined, %forwardUndefined, "
                           "%forwardWriter, ")
                     : replaced(source, "ctjs.call_direct @forwardWriter$9(",
                                "ctjs.call_direct @keepWriter$8("),
             }) {
            check(!invalid.empty() && invalid != source,
                  "nested loop call-result control changes its source");
            if (invalid.empty() || invalid == source) { continue; }
            auto fixture = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
            check(static_cast<bool>(fixture), "hostile nested loop return dependency parses");
            if (!fixture) { continue; }
            for (auto provider : {HostContract::Provider::ctbrowserDOM,
                                  HostContract::Provider::ctbrowserDOMSession}) {
                mlir::OwningOpRef<mlir::ModuleOp> input(fixture->clone());
                auto request = contract;
                request.provider = provider;
                request.moduleSha256 = hostContractFingerprint(*input);
                auto failure = normalizeDOMCustomIteration(*input, request, completeBudget);
                check(static_cast<bool>(failure),
                      "nested return summaries retain actual, observer and direct-target checks");
                if (failure) { llvm::consumeError(std::move(failure)); }
                check(hostContractFingerprint(*input) == request.moduleSha256 &&
                          noEvidence(*input, DOMEntryAnalysis(*input, request)),
                      "refused nested return preserves source and publishes no evidence");
            }
        }
    }
    for (const auto & source : {branchLoopCallWriterSource, directBranchLoopCallWriterSource}) {
        const auto unknown = replaced(source, "    %selectPositive =",
                                      "    %unknownWriter = ctjs.load_global \"unknownWriter\"\n"
                                      "    %selectPositive =");
        for (const auto & invalid : {
                 replaced(unknown, "scf.yield %forwardedWriter :", "scf.yield %unknownWriter :"),
                 replaced(unknown, "scf.yield %forwardedAlternate :", "scf.yield %unknownWriter :"),
                 replaced(source, "scf.yield %forwardedWriter :", "scf.yield %selectZero :"),
                 replaced(source, "scf.yield %forwardedAlternate :", "scf.yield %selectZero :"),
                 // A direct missing actual is padded with undefined by resolution.
                 replaced(source,
                          "%keptWriter, %keptAlternate, %keptState)\n"
                          "    ctjs.return %forwardResult",
                          source == branchLoopCallWriterSource
                              ? "%keptWriter)\n    ctjs.return %forwardResult"
                              : "%keptWriter, %forwardUndefined, %keptState)\n"
                                "    ctjs.return %forwardResult"),
                 replaced(source, "    ctjs.return %selectedWriter",
                          "    ctjs.store_global \"leaked\", %selectedWriter\n"
                          "    ctjs.return %selectedWriter"),
                 replaced(
                     source, "    ctjs.return %selectedWriter",
                     "    %observedWriter = ctjs.compare eq %selectedWriter, %forwardedWriter\n"
                     "    ctjs.return %selectedWriter"),
                 replaced(source, "    ctjs.return %selectedWriter",
                          "    %undefined = ctjs.constant #ctjs.undefined\n"
                          "    %recursiveEffect = ctjs.call %callee(%undefined, %forwardedWriter, "
                          "%forwardedAlternate, %forwardedState)\n"
                          "    ctjs.return %selectedWriter"),
                 source == branchLoopCallWriterSource
                     ? replaced(
                           source, "ctjs.call %forwardWriter(%forwardUndefined, ",
                           "ctjs.call_direct @keepWriter$8(%forwardUndefined, %forwardUndefined, "
                           "%forwardWriter, ")
                     : replaced(source, "ctjs.call_direct @forwardWriter$9(",
                                "ctjs.call_direct @keepWriter$8("),
             }) {
            check(!invalid.empty() && invalid != source,
                  "branch loop call-result control changes its source");
            if (invalid.empty() || invalid == source) { continue; }
            auto fixture = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
            check(static_cast<bool>(fixture), "hostile branch loop return dependency parses");
            if (!fixture) { continue; }
            for (auto provider : {HostContract::Provider::ctbrowserDOM,
                                  HostContract::Provider::ctbrowserDOMSession}) {
                mlir::OwningOpRef<mlir::ModuleOp> input(fixture->clone());
                auto request = contract;
                request.provider = provider;
                request.moduleSha256 = hostContractFingerprint(*input);
                auto failure = normalizeDOMCustomIteration(*input, request, completeBudget);
                check(static_cast<bool>(failure),
                      "every branch return arm, actual, observer and direct target must be proved");
                if (failure) { llvm::consumeError(std::move(failure)); }
                check(hostContractFingerprint(*input) == request.moduleSha256 &&
                          noEvidence(*input, DOMEntryAnalysis(*input, request)),
                      "refused branch return preserves source and publishes no evidence");
            }
        }
    }
    for (const auto & source : {formalCalleeWriterSource, directFormalCalleeWriterSource}) {
        auto selected = replaced(source, "    %forwardResult =",
                                 R"MLIR(    %unknownChooser = ctjs.load_global "unknownChooser"
    %chooserCondition = ctjs.truthy %keptState
    %selectedChooser = scf.if %chooserCondition -> (!ctjs.value) {
      scf.yield %keptChooser : !ctjs.value
    } else {
      scf.yield %unknownChooser : !ctjs.value
    }
    %forwardResult =)MLIR");
        selected = source == formalCalleeWriterSource
                       ? replaced(selected, "ctjs.call %keptChooser(%forwardUndefined, ",
                                  "ctjs.call %selectedChooser(%forwardUndefined, ")
                       : replaced(selected, "%forwardUndefined, %forwardUndefined, %keptChooser, ",
                                  "%forwardUndefined, %forwardUndefined, %selectedChooser, ");
        const auto unknown =
            replaced(source, "      %oneTrip =",
                     "      %unknownChooser = ctjs.load_global \"unknownChooser\"\n"
                     "      %oneTrip =");
        for (const auto & invalid : {
                 selected,
                 replaced(selected, "scf.yield %unknownChooser :", "scf.yield %forwardUndefined :"),
                 replaced(unknown, "%selectedState, %forwardWriter)\n",
                          "%selectedState, %unknownChooser)\n"),
                 replaced(source, "%selectedState, %forwardWriter)\n",
                          "%selectedState, %selectZero)\n"),
                 replaced(source, "%selectedState, %forwardWriter)\n",
                          source == formalCalleeWriterSource
                              ? "%selectedState)\n"
                              : "%selectedState, %nestedUndefined)\n"),
                 replaced(source, "    %forwardResult =",
                          "    ctjs.store_global \"leaked\", %keptChooser\n"
                          "    %forwardResult ="),
                 replaced(source, "    %forwardResult =",
                          "    %observedChooser = ctjs.compare eq %keptChooser, %keptChooser\n"
                          "    %forwardResult ="),
                 replaced(source, "%selectedState, %forwardWriter)\n",
                          "%selectedState, %keepWriter)\n"),
                 // Keep the wrong direct target structurally valid with its fourth actual.
                 replaced(source == formalCalleeWriterSource
                              ? replaced(source, "ctjs.call %keptChooser(%forwardUndefined, ",
                                         "ctjs.call_direct @keepWriter$8(%forwardUndefined, "
                                         "%forwardUndefined, %keptChooser, ")
                              : replaced(source, "ctjs.call_direct @forwardWriter$9(",
                                         "ctjs.call_direct @keepWriter$8("),
                          "%keptWriter, %keptAlternate, %keptState)\n",
                          "%keptWriter, %keptAlternate, %keptState, %forwardUndefined)\n"),
                 replaced(source, "    ctjs.return %selectedWriter",
                          "    %undefined = ctjs.constant #ctjs.undefined\n"
                          "    %recursiveEffect = ctjs.call %callee(%undefined, %forwardedWriter, "
                          "%forwardedAlternate, %forwardedState)\n"
                          "    ctjs.return %selectedWriter"),
             }) {
            check(!invalid.empty() && invalid != source,
                  "formal return-callee control changes its source");
            if (invalid.empty() || invalid == source) { continue; }
            auto fixture = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
            check(static_cast<bool>(fixture), "hostile formal return-callee dependency parses");
            if (!fixture) { continue; }
            for (auto provider : {HostContract::Provider::ctbrowserDOM,
                                  HostContract::Provider::ctbrowserDOMSession}) {
                mlir::OwningOpRef<mlir::ModuleOp> input(fixture->clone());
                auto request = contract;
                request.provider = provider;
                request.moduleSha256 = hostContractFingerprint(*input);
                auto failure = normalizeDOMCustomIteration(*input, request, completeBudget);
                check(static_cast<bool>(failure),
                      "return callees retain actual, observer, recursion and direct-target checks");
                if (failure) { llvm::consumeError(std::move(failure)); }
                check(hostContractFingerprint(*input) == request.moduleSha256 &&
                          noEvidence(*input, DOMEntryAnalysis(*input, request)),
                      "refused formal return callee preserves source and publishes no evidence");
            }
        }
    }
    for (const auto & source : {breakWriterSource, directBreakWriterSource}) {
        const auto poisoned =
            replaced(source, "    %helperPoison =",
                     "    %helperValuePoison = ub.poison : !ctjs.value\n    %helperPoison =");
        for (const auto & invalid : {
                 replaced(source, "    %helperSelector =",
                          "    %observedTag = arith.index_castui %helperLoop#1 : i32 to index\n"
                          "    %helperSelector ="),
                 replaced(poisoned, "%trip = %helperZero", "%trip = %helperValuePoison"),
             }) {
            auto fixture = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
            check(static_cast<bool>(fixture), "invalid sibling break completion parses");
            if (!fixture) { continue; }
            for (auto provider : {HostContract::Provider::ctbrowserDOM,
                                  HostContract::Provider::ctbrowserDOMSession}) {
                mlir::OwningOpRef<mlir::ModuleOp> input(fixture->clone());
                auto request = contract;
                request.provider = provider;
                request.moduleSha256 = hostContractFingerprint(*input);
                auto failure = normalizeDOMCustomIteration(*input, request, completeBudget);
                check(static_cast<bool>(failure),
                      "sibling break with an observed tag or live poisoned counter refuses");
                if (failure) { llvm::consumeError(std::move(failure)); }
                check(hostContractFingerprint(*input) == request.moduleSha256 &&
                          noEvidence(*input, DOMEntryAnalysis(*input, request)),
                      "refused sibling completion preserves source and publishes no evidence");
            }
        }
    }
    for (auto fixture : {*argumentWriter, *directArgumentWriter}) {
        for (unsigned malformed = 0; malformed != 4; ++malformed) {
            for (auto provider : {HostContract::Provider::ctbrowserDOM,
                                  HostContract::Provider::ctbrowserDOMSession}) {
                mlir::OwningOpRef<mlir::ModuleOp> input(fixture.clone());
                auto helper = input->lookupSymbol<ctjs::FuncOp>("readCount$4");
                auto body = input->lookupSymbol<ctjs::FuncOp>(contract.entry);
                if (malformed == 0) {
                    helper.getBody()
                        .front()
                        .getArgument(ctjs::implicit_arguments)
                        .setType(mlir::IntegerType::get(&context, 32));
                } else if (malformed == 1) {
                    const auto type = helper.getFunctionType();
                    helper.setFunctionTypeAttr(mlir::TypeAttr::get(mlir::FunctionType::get(
                        &context, type.getInputs().drop_back(), type.getResults())));
                } else if (malformed == 2) {
                    mlir::Operation * invocation = nullptr;
                    body.walk([&](mlir::Operation * operation) {
                        if (invocation) { return; }
                        if (auto call = llvm::dyn_cast<ctjs::CallOp>(operation)) {
                            auto closure = call.getCallee().getDefiningOp<ctjs::CreateClosureOp>();
                            if (closure && closure.getFunction() == 4) { invocation = call; }
                        } else if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(operation);
                                   call && call.getTarget() == helper) {
                            invocation = call;
                        }
                    });
                    check(invocation != nullptr, "argument fixture has a helper invocation");
                    if (!invocation) { continue; }
                    mlir::OpBuilder at(invocation);
                    auto wrong =
                        mlir::arith::ConstantIntOp::create(at, invocation->getLoc(), 7, 32);
                    invocation->setOperand(invocation->getNumOperands() - 2, wrong);
                } else {
                    body->setAttr("observer",
                                  mlir::FlatSymbolRefAttr::get(&context, "readCount$4"));
                }
                auto request = contract;
                request.provider = provider;
                request.moduleSha256 = hostContractFingerprint(*input);
                auto failure = normalizeDOMCustomIteration(*input, request, completeBudget);
                check(static_cast<bool>(failure),
                      "malformed helper arguments, signatures and symbolic observers refuse");
                if (failure) { llvm::consumeError(std::move(failure)); }
                check(hostContractFingerprint(*input) == request.moduleSha256 &&
                          noEvidence(*input, DOMEntryAnalysis(*input, request)),
                      "malformed argument helpers preserve source and publish no evidence");
            }
        }
    }
    for (auto fixture : {*branchWriter, *directBranchWriter}) {
        for (unsigned malformed = 0; malformed != 2; ++malformed) {
            for (auto provider : {HostContract::Provider::ctbrowserDOM,
                                  HostContract::Provider::ctbrowserDOMSession}) {
                mlir::OwningOpRef<mlir::ModuleOp> input(fixture.clone());
                auto body = input->lookupSymbol<ctjs::FuncOp>("readCount$4");
                auto branch = *body.getBody().front().getOps<mlir::scf::IfOp>().begin();
                if (malformed == 0) {
                    branch.getThenRegion().front().back().eraseOperands(0, 1);
                } else {
                    branch->setOperand(0, branch.getElseRegion().front().back().getOperand(0));
                }
                auto request = contract;
                request.provider = provider;
                request.moduleSha256 = hostContractFingerprint(*input);
                auto failure = normalizeDOMCustomIteration(*input, request, completeBudget);
                check(static_cast<bool>(failure), "malformed helper branch joins refuse");
                if (failure) { llvm::consumeError(std::move(failure)); }
                check(hostContractFingerprint(*input) == request.moduleSha256 &&
                          noEvidence(*input, DOMEntryAnalysis(*input, request)),
                      "malformed helper joins preserve source and publish no evidence");
            }
        }
    }
    const auto poisonedJoinSource = replaced(
        entryCapturedSource, "    %entryStart = ctjs.binary_static add %entryBefore, %zero",
        R"MLIR(    %joinPoison = ub.poison : !ctjs.value
    %joinTest = ctjs.truthy %entryBefore
    %entryStart = scf.if %joinTest -> (!ctjs.value) {
      scf.yield %joinPoison : !ctjs.value
    } else {
      scf.yield %entryBefore : !ctjs.value
    })MLIR");
    auto poisonedJoin = mlir::parseSourceString<mlir::ModuleOp>(poisonedJoinSource, &context);
    check(static_cast<bool>(poisonedJoin), "ordinary branch with an outer live poison parses");
    if (poisonedJoin) {
        for (auto provider :
             {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
            mlir::OwningOpRef<mlir::ModuleOp> input(poisonedJoin->clone());
            auto request = contract;
            request.provider = provider;
            request.moduleSha256 = hostContractFingerprint(*input);
            auto failure = normalizeDOMCustomIteration(*input, request, completeBudget);
            check(static_cast<bool>(failure), "joining ordinary results cannot hide live poison");
            if (failure) { llvm::consumeError(std::move(failure)); }
            request.moduleSha256 = hostContractFingerprint(*input);
            check(noEvidence(*input, DOMEntryAnalysis(*input, request)),
                  "live poison branch cannot publish evidence after a partial normalization");
        }
    }

    auto uninitializedSiblingSource =
        replaced(siblingReaderSource, "%emittedCell = ctjs.create_cell %emittedInitial",
                 "%cellUndefined = ctjs.constant #ctjs.undefined\n"
                 "    %emittedCell = ctjs.create_cell %cellUndefined");
    uninitializedSiblingSource =
        replaced(uninitializedSiblingSource, "%entryBefore = ctjs.call %readCount(%undefined)",
                 "%entryBefore = ctjs.call %readCount(%undefined)\n"
                 "    ctjs.cell_set %emittedCell, %emittedInitial");
    for (const auto & source : {argumentWriterSource, directArgumentWriterSource}) {
        unsigned index = 0;
        for (const auto & invalid : {
                 replaced(source, "%argumentStep_before, %argumentCount_before)",
                          "%argumentStep_before)"),
                 replaced(source, "%argumentStep_before, %argumentCount_before)",
                          "%argumentStep_before, %argumentCount_before, %one)"),
                 replaced(source, "%argumentStep_before, %argumentCount_before)",
                          "%readCount, %argumentCount_before)"),
                 replaced(source, "%argumentStep_before, %argumentCount_before)",
                          "%emittedCell, %argumentCount_before)"),
                 replaced(source, "    %argumentCount_before = ctjs.cell_get %emittedCell",
                          "    ctjs.store_global \"argument-helper-leaked\", %readCount\n"
                          "    %argumentCount_before = ctjs.cell_get %emittedCell"),
                 replaced(source, "    %argumentCount_before = ctjs.cell_get %emittedCell",
                          "    %symbolOnly = ctjs.call_direct @readCount$4(%undefined, "
                          "%undefined, %undefined, %one, %one)\n"
                          "    %argumentCount_before = ctjs.cell_get %emittedCell"),
             }) {
            const bool directArity = source == directArgumentWriterSource && index < 2;
            ++index;
            auto fixture = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
            if (directArity) {
                check(!fixture, "direct helper argument-count mismatches refuse during parsing");
                continue;
            }
            check(static_cast<bool>(fixture), "hostile argument-taking helper witness parses");
            if (!fixture) { continue; }
            for (auto provider : {HostContract::Provider::ctbrowserDOM,
                                  HostContract::Provider::ctbrowserDOMSession}) {
                mlir::OwningOpRef<mlir::ModuleOp> input(fixture->clone());
                auto request = contract;
                request.provider = provider;
                request.moduleSha256 = hostContractFingerprint(*input);
                auto failure = normalizeDOMCustomIteration(*input, request, completeBudget);
                check(static_cast<bool>(failure),
                      "helper argument counts, escaping values and unknown call sites refuse");
                if (failure) { llvm::consumeError(std::move(failure)); }
                check(hostContractFingerprint(*input) == request.moduleSha256 &&
                          noEvidence(*input, DOMEntryAnalysis(*input, request)),
                      "refused argument helpers preserve source and publish no evidence");
            }
        }
    }
    for (const auto & invalid : {
             replaced(siblingReaderSource, "%entryBefore = ctjs.call %readCount(%undefined)",
                      "%entryBefore = ctjs.call %readCount(%element)"),
             replaced(siblingReaderSource, "%entryBefore = ctjs.call %readCount(%undefined)",
                      "%entryBefore = ctjs.call %readCount(%undefined, %one)"),
             replaced(siblingReaderSource, "ctjs.return %observed", "ctjs.return %this"),
             replaced(siblingReaderSource, "ctjs.return %observed", "ctjs.return %new"),
             replaced(siblingReaderSource, "%readCount = ctjs.create_closure %callee[4]",
                      "%readCount = ctjs.create_closure %element[4]"),
             replaced(siblingReaderSource, "%entryBefore = ctjs.call %readCount(%undefined)",
                      "ctjs.store_global \"reader-leaked\", %readCount\n"
                      "    %entryBefore = ctjs.call %readCount(%undefined)"),
             replaced(siblingReaderSource, "%entryBefore = ctjs.call %readCount(%undefined)",
                      "%readTwin = ctjs.create_closure %callee[4] this %undefined captures "
                      "%emittedCell\n"
                      "    %entryBefore = ctjs.call %readCount(%undefined)"),
             replaced(siblingReaderSource, "ctjs.func @readExtra$5", "ctjs.func @readExtra$4"),
             replaced(siblingReaderSource, "%observed = ctjs.load_upvalue %callee[0]",
                      "%observed = ctjs.load_upvalue %callee[1]"),
             replaced(siblingReaderSource, "%observed = ctjs.load_upvalue %callee[0]",
                      "%observed = ctjs.load_upvalue %callee[-1]"),
             replaced(siblingReaderSource, "%observed = ctjs.load_upvalue %callee[0]",
                      "%observed = ctjs.load_upvalue %this[0]"),
             replaced(siblingReaderSource,
                      "%readExtra = ctjs.create_closure %callee[5] this %undefined captures "
                      "%extraCell",
                      "%readExtra = ctjs.create_closure %callee[5] this %undefined captures "
                      "%extraCell, %capture"),
             replaced(siblingReaderSource, "%observed = ctjs.load_upvalue %callee[0]",
                      "%observed = ctjs.load_upvalue %callee[0]\n"
                      "    %recursiveUndefined = ctjs.constant #ctjs.undefined\n"
                      "    %recursive = ctjs.call %callee(%recursiveUndefined)"),
             replaced(siblingReaderSource, "%entryBefore = ctjs.call %readCount(%undefined)",
                      "%entryBefore = ctjs.call %readCount(%undefined)\n"
                      "    ctjs.store_global \"state-leaked\", %emittedCell"),
             uninitializedSiblingSource,
             replaced(directSiblingReaderSource,
                      "ctjs.call_direct @readCount$4(%undefined, %undefined, %readCount)",
                      "ctjs.call_direct @readExtra$5(%undefined, %undefined, %readCount)"),
             replaced(directSiblingReaderSource,
                      "ctjs.call_direct @readCount$4(%undefined, %undefined, %readCount)",
                      "ctjs.call_direct @readCount$4(%undefined, %undefined, %undefined)"),
             replaced(directSiblingReaderSource,
                      "ctjs.call_direct @readCount$4(%undefined, %undefined, %readCount)",
                      "ctjs.call_direct @readCount$4(%element, %undefined, %readCount)"),
             replaced(directSiblingReaderSource,
                      "ctjs.call_direct @readCount$4(%undefined, %undefined, %readCount)",
                      "ctjs.call_direct @readCount$4(%undefined, %element, %readCount)"),
             replaced(directSiblingReaderSource,
                      "ctjs.call_direct @readCount$4(%undefined, %undefined, %readCount)",
                      "ctjs.call_direct @readCount$4(%undefined, %undefined, %readCount)\n"
                      "    %symbolOnly = ctjs.call_direct @readCount$4(%undefined, %undefined, "
                      "%undefined)"),
             replaced(directSiblingReaderSource,
                      "%readCount = ctjs.create_closure %callee[4] this %this",
                      "%readCount = ctjs.create_closure %callee[4] this %element"),
             replaced(siblingWriterSource, "ctjs.store_upvalue %callee[0], %updated",
                      "ctjs.store_upvalue %callee[-1], %updated"),
             replaced(siblingWriterSource, "ctjs.store_upvalue %callee[0], %updated",
                      "ctjs.store_upvalue %callee[2], %updated"),
             replaced(siblingWriterSource, "ctjs.store_upvalue %callee[0], %updated",
                      "ctjs.store_upvalue %this[0], %updated"),
             replaced(siblingWriterSource, "ctjs.store_upvalue %callee[0], %updated",
                      "ctjs.store_upvalue %callee[0], %callee"),
             replaced(siblingWriterSource, "%observedExtra = ctjs.load_upvalue %callee[0]",
                      "%observedExtra = ctjs.load_upvalue %callee[0]\n"
                      "    ctjs.store_upvalue %callee[-1], %observedExtra"),
             replaced(siblingWriterSource, "ctjs.store_upvalue %callee[0], %updated",
                      "ctjs.store_upvalue %callee[0], %updated\n"
                      "    %nestedUndefined = ctjs.constant #ctjs.undefined\n"
                      "    %nested = ctjs.call %callee(%nestedUndefined)"),
             replaced(directSiblingWriterSource, "ctjs.store_upvalue %callee[0], %updated",
                      "ctjs.store_upvalue %callee[0], %updated\n"
                      "    %nestedUndefined = ctjs.constant #ctjs.undefined\n"
                      "    %nested = ctjs.call_direct @readExtra$5(%nestedUndefined, "
                      "%nestedUndefined, %callee)"),
             replaced(siblingWriterSource, "%entryBefore = ctjs.call %readCount(%undefined)",
                      "ctjs.store_global \"writer-leaked\", %readCount\n"
                      "    %entryBefore = ctjs.call %readCount(%undefined)"),
             replaced(siblingWriterSource, "%emittedCell = ctjs.create_cell %emittedInitial",
                      "%uninitialized = ctjs.constant #ctjs.undefined\n"
                      "    %emittedCell = ctjs.create_cell %uninitialized"),
         }) {
        auto fixture = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
        check(static_cast<bool>(fixture), "hostile sibling helper witness parses");
        if (!fixture) { continue; }
        for (auto provider :
             {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
            mlir::OwningOpRef<mlir::ModuleOp> input(fixture->clone());
            auto request = contract;
            request.provider = provider;
            request.moduleSha256 = hostContractFingerprint(*input);
            auto failure = normalizeDOMCustomIteration(*input, request, completeBudget);
            check(static_cast<bool>(failure),
                  "sibling identity, confinement, receiver, initialization and body refusals hold");
            if (failure) { llvm::consumeError(std::move(failure)); }
            check(hostContractFingerprint(*input) == request.moduleSha256 &&
                      noEvidence(*input, DOMEntryAnalysis(*input, request)),
                  "refused sibling helper preserves source and publishes no DOM evidence");
        }
    }

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
             replaced(initializedCaptureSource, "    ctjs.cell_set %emittedCell, %emittedInitial",
                      "    %uninitialized = ctjs.cell_get %emittedCell\n"
                      "    ctjs.cell_set %emittedCell, %emittedInitial"),
             replaced(initializedCaptureSource, "    ctjs.cell_set %emittedCell, %emittedInitial",
                      "    %readFlag = arith.constant true\n"
                      "    scf.if %readFlag {\n"
                      "      %uninitialized = ctjs.cell_get %emittedCell\n"
                      "      scf.yield\n"
                      "    }\n"
                      "    ctjs.cell_set %emittedCell, %emittedInitial"),
             replaced(replaced(initializedCaptureSource,
                               "    ctjs.cell_set %emittedCell, %emittedInitial\n", ""),
                      "    %record = ctjs.call %open(%undefined, %holder)",
                      "    ctjs.cell_set %emittedCell, %emittedInitial\n"
                      "    %record = ctjs.call %open(%undefined, %holder)"),
             replaced(initializedCaptureSource, "    ctjs.cell_set %emittedCell, %emittedInitial",
                      "    %initializeFlag = arith.constant true\n"
                      "    scf.if %initializeFlag {\n"
                      "      ctjs.cell_set %emittedCell, %emittedInitial\n"
                      "      scf.yield\n"
                      "    }"),
             replaced(entryCapturedSource, "%bodyRead = ctjs.cell_get %extraCell",
                      "%bodyRead = ctjs.cell_get %extraCell\n"
                      "        ctjs.store_global \"leaked\", %emittedCell"),
             replaced(entryCapturedSource, "%bodyRead = ctjs.cell_get %extraCell",
                      "%bodyRead = ctjs.cell_get %extraCell\n"
                      "        %alias = ctjs.create_cell %emittedCell"),
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
    // This malformed callable passes method arity/capture checks. Normalizing
    // its entry dispatch would invalidate the cached protocol operations.
    const auto aliasedEntrySource =
        replaced(replaced(replaced(countedSource, ", %element: !ctjs.value)", ")"),
                          "    %frame = ctjs.frame_enter 16",
                          "    %element = ctjs.constant #ctjs.undefined\n"
                          "    %frame = ctjs.frame_enter 16"),
                 "%step = ctjs.create_closure %callee[2] this %undefined captures %capture",
                 "%step = ctjs.create_closure %callee[0] this %undefined");
    auto aliasedEntry = mlir::parseSourceString<mlir::ModuleOp>(aliasedEntrySource, &context);
    check(static_cast<bool>(aliasedEntry), "iterator method aliasing its dispatching entry parses");
    if (aliasedEntry) {
        for (auto provider :
             {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
            mlir::OwningOpRef<mlir::ModuleOp> input(aliasedEntry->clone());
            auto request = contract;
            request.provider = provider;
            request.moduleSha256 = hostContractFingerprint(*input);
            const auto reason =
                llvm::toString(normalizeDOMCustomIteration(*input, request, completeBudget));
            check(reason.find("alias the entry") != std::string::npos,
                  "entry alias refuses before method completion replaces cached protocol ops");
            check(hostContractFingerprint(*input) == request.moduleSha256 &&
                      noEvidence(*input, DOMEntryAnalysis(*input, request)),
                  "entry alias refusal preserves source and publishes no evidence");
        }
    }
    for (const auto & source : {methodBreakCapturedSource, methodBreakReceiverSource}) {
        const auto effectfulMethodSource =
            replaced(source, "    scf.index_switch %methodSelector\n    case 7 {",
                     "    scf.index_switch %methodSelector\n    case 7 {\n"
                     "      %effectDispatch = ctjs.call "
                     "%methodSet(%element, %methodExitName, %methodYes)");
        auto effectfulMethod =
            mlir::parseSourceString<mlir::ModuleOp>(effectfulMethodSource, &context);
        check(static_cast<bool>(effectfulMethod), "historical effectful method completion parses");
        if (effectfulMethod) {
            for (auto provider : {HostContract::Provider::ctbrowserDOM,
                                  HostContract::Provider::ctbrowserDOMSession}) {
                auto request = contract;
                request.provider = provider;
                request.moduleSha256 = hostContractFingerprint(*effectfulMethod);
                mlir::OwningOpRef<mlir::ModuleOp> input(effectfulMethod->clone());
                auto failure = normalizeDOMCustomIteration(*input, request, completeBudget);
                check(!failure, "historical effectful method completion normalizes");
                if (failure) {
                    std::fprintf(stderr, "%s\n", llvm::toString(std::move(failure)).c_str());
                    continue;
                }
                auto body = input->lookupSymbol<ctjs::FuncOp>("next$2");
                mlir::scf::WhileOp loop;
                llvm::SmallVector<ctjs::CallOp> exitEffects;
                body.walk([&](mlir::scf::WhileOp found) { loop = found; });
                body.walk([&](ctjs::CallOp call) {
                    if (call.getArgs().size() == 2 &&
                        ctjs::constantKey(call.getArgs()[0]) == "method-exit") {
                        exitEffects.push_back(call);
                    }
                });
                check(loop && exitEffects.size() == 2,
                      "method completion retains both the conditional and common exit writes");
                if (loop && exitEffects.size() == 2) {
                    auto selected = llvm::dyn_cast<mlir::scf::IfOp>(exitEffects[0]->getParentOp());
                    check(selected && selected->getBlock() == loop->getBlock() &&
                              loop->isBeforeInBlock(selected) &&
                              exitEffects[0]->getParentRegion() == &selected.getThenRegion() &&
                              exitEffects[1]->getBlock() == selected->getBlock() &&
                              selected->isBeforeInBlock(exitEffects[1]),
                          "conditional method exit effect stays after the loop and before the "
                          "common exit effect");
                }
                failure = expandDOMHelpers(*input, request.entry, completeBudget);
                check(!failure, "historical effectful method completion expands");
                if (failure) {
                    std::fprintf(stderr, "%s\n", llvm::toString(std::move(failure)).c_str());
                    continue;
                }
                request.moduleSha256 = hostContractFingerprint(*input);
                const DOMEntryAnalysis proof(*input, request);
                check(proof.proved(), "historical effectful method retains complete DOM proof");
                if (!proof.proved()) { std::fprintf(stderr, "%s\n", proof.reason().str().c_str()); }
            }
        }
        const auto poisoned =
            replaced(source, "    %methodPoison =",
                     "    %methodValuePoison = ub.poison : !ctjs.value\n    %methodPoison =");
        for (const auto & invalid : {
                 replaced(source, "    %exitEmitted =",
                          "    %observedTag = arith.index_castui %methodLoop#1 : i32 to index\n"
                          "    %exitEmitted ="),
                 replaced(source, "%methodNormal = arith.constant 7 : i32",
                          "%unknownTag = ctjs.truthy %element\n"
                          "    %methodNormal = arith.extui %unknownTag : i1 to i32"),
                 replaced(source, "      scf.yield %carried, %inactiveTag",
                          "      %observedTag = arith.index_castui %inactiveTag : i32 to index\n"
                          "      scf.yield %carried, %inactiveTag"),
                 replaced(poisoned, "%trip = %zero", "%trip = %methodValuePoison"),
                 replaced(poisoned, "scf.yield %stop, %nextTrip, %methodBreak",
                          "scf.yield %stop, %methodValuePoison, %methodBreak"),
             }) {
            auto fixture = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
            check(static_cast<bool>(fixture), "invalid method break completion parses");
            if (!fixture) { continue; }
            for (auto provider : {HostContract::Provider::ctbrowserDOM,
                                  HostContract::Provider::ctbrowserDOMSession}) {
                mlir::OwningOpRef<mlir::ModuleOp> input(fixture->clone());
                auto request = contract;
                request.provider = provider;
                request.moduleSha256 = hostContractFingerprint(*input);
                auto failure = normalizeDOMCustomIteration(*input, request, completeBudget);
                check(static_cast<bool>(failure),
                      "unknown or observed method tags and live poison refuse");
                if (failure) { llvm::consumeError(std::move(failure)); }
                check(hostContractFingerprint(*input) == request.moduleSha256 &&
                          noEvidence(*input, DOMEntryAnalysis(*input, request)),
                      "incomplete method completion preserves source and publishes no evidence");
            }
        }
    }
    for (auto fixture : {*loopCaptured, *methodBreakCaptured, *methodBreakReceiver, *entryCaptured,
                         *zeroEntryCaptured}) {
        for (unsigned malformed = 0; malformed != 5; ++malformed) {
            for (auto provider : {HostContract::Provider::ctbrowserDOM,
                                  HostContract::Provider::ctbrowserDOMSession}) {
                mlir::OwningOpRef<mlir::ModuleOp> input(fixture.clone());
                auto body = input->lookupSymbol<ctjs::FuncOp>(
                    fixture == *entryCaptured || fixture == *zeroEntryCaptured ? "custom$0"
                                                                               : "next$2");
                mlir::scf::WhileOp loop;
                body.walk([&](mlir::scf::WhileOp found) { loop = found; });
                auto condition =
                    llvm::cast<mlir::scf::ConditionOp>(loop.getBefore().front().back());
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
             replaced(entryCapturedSource, "ctjs.cell_set %extraCell, %bodyNext",
                      "ctjs.cell_set %extraCell, %element"),
             replaced(entryCapturedSource, "ctjs.cell_set %emittedCell, %entryStart",
                      "ctjs.cell_set %emittedCell, %yes"),
             replaced(siblingWriterSource, "ctjs.store_upvalue %callee[0], %updated",
                      "%wrong = ctjs.constant #ctjs.boolean<false>\n"
                      "    ctjs.store_upvalue %callee[0], %wrong"),
             replaced(siblingWriterSource, "%observedExtra = ctjs.load_upvalue %callee[0]",
                      "%observedExtra = ctjs.load_upvalue %callee[0]\n"
                      "    %wrong = ctjs.constant #ctjs.undefined\n"
                      "    ctjs.store_upvalue %callee[0], %wrong"),
             replaced(argumentWriterSource, "%argumentStep_before, %argumentCount_before)",
                      "%element, %argumentCount_before)"),
             replaced(directArgumentWriterSource, "%argumentStep_before, %argumentCount_before)",
                      "%element, %argumentCount_before)"),
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
    for (auto fixture : {*original,
                         *effectful,
                         *counted,
                         *crossed,
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
                         *zeroTwoState,
                         *methodBreakCaptured,
                         *methodBreakReceiver,
                         *reinitializedCapture,
                         *externalWrite,
                         *externalRead,
                         *entryCaptured,
                         *zeroEntryCaptured,
                         *siblingReader,
                         *zeroSiblingReader,
                         *directSiblingReader,
                         *zeroDirectSiblingReader,
                         *siblingCountStore,
                         *siblingExtraStore,
                         *siblingWriter,
                         *directSiblingWriter,
                         *zeroSiblingWriter,
                         *zeroDirectSiblingWriter,
                         *branchWriter,
                         *directBranchWriter,
                         *argumentWriter,
                         *directArgumentWriter,
                         *breakWriter,
                         *directBreakWriter,
                         *nestedWriter,
                         *directNestedWriter,
                         *callableWriter,
                         *directCallableWriter,
                         *returnedWriter,
                         *directReturnedWriter,
                         *differentCallableWriter,
                         *directDifferentCallableWriter,
                         *differentReturnedWriter,
                         *directDifferentReturnedWriter,
                         *joinedWriter,
                         *directJoinedWriter,
                         *loopJoinedWriter,
                         *directLoopJoinedWriter,
                         *zeroLoopJoinedWriter,
                         *zeroDirectLoopJoinedWriter,
                         *loopCallWriter,
                         *directLoopCallWriter,
                         *zeroLoopCallWriter,
                         *zeroDirectLoopCallWriter,
                         *nestedLoopCallWriter,
                         *directNestedLoopCallWriter,
                         *zeroNestedLoopCallWriter,
                         *zeroDirectNestedLoopCallWriter,
                         *branchLoopCallWriter,
                         *directBranchLoopCallWriter,
                         *zeroBranchLoopCallWriter,
                         *zeroDirectBranchLoopCallWriter,
                         *formalCalleeWriter,
                         *directFormalCalleeWriter,
                         *zeroFormalCalleeWriter,
                         *zeroDirectFormalCalleeWriter}) {
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
