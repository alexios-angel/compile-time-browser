// Lumberhack-style producer/consumer strategies, restricted to native Map
// snapshots. Conflicting consumers select identity; effects and control keep
// the original snapshot's observation time.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../native-deforestation-fixture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/native.mlir
// RUN: ctjs-opt --ctnative-deforest %t/native.mlir | FileCheck %s --check-prefix=FUSED --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-opt --ctnative-deforest='report=true' --mlir-print-op-on-diagnostic=false %t/native.mlir -o /dev/null 2>&1 | FileCheck %s --check-prefix=REPORT
// RUN: ctjs-opt --ctnative-deforest %t/native.mlir | ctjs-opt --ctnative-deforest='report=true' --mlir-print-op-on-diagnostic=false -o /dev/null 2>&1 | FileCheck %s --check-prefix=REPEAT
// RUN: ctjs-opt --ctnative-deforest='max-snapshots=0 report=true' --mlir-print-op-on-diagnostic=false %t/native.mlir -o /dev/null 2>&1 | FileCheck %s --check-prefix=BUDGET
// RUN: ctjs-opt --ctnative-deforest='max-scan=0' %t/native.mlir | FileCheck %s --check-prefix=SCAN
// RUN: ctjs-opt --ctnative-deforest %t/escape.mlir | FileCheck %s --check-prefix=ESCAPE
// RUN: ctjs-opt --ctnative-deforest %t/identity.mlir | FileCheck %s --check-prefix=ESCAPE
// RUN: ctjs-opt --ctnative-deforest %t/producer-conflict.mlir | FileCheck %s --check-prefix=PRODUCERS
// RUN: ctjs-opt --ctnative-deforest %t/before-assignment.mlir | FileCheck %s --check-prefix=BEFORE
// RUN: ctjs-opt --ctnative-deforest %t/unknown-runtime.mlir | FileCheck %s --check-prefix=ABI

// REPORT: deforestation: 9 snapshot site(s) eliminated, 6 retained with identity strategy
// REPEAT: deforestation: 0 snapshot site(s) eliminated, 6 retained with identity strategy
// BUDGET: deforestation: 0 snapshot site(s) eliminated, 15 retained with identity strategy
// SCAN: ctnative.deforest_reason = "fusion inference exceeded its scan budget"

// FUSED-LABEL: emitc.func @keyAt_
// FUSED-NOT: call_opaque "ctnative::map_keys"
// FUSED: call_opaque "ctnative::map_snapshot_at<true>"
// FUSED-LABEL: emitc.func @valueAt_
// FUSED-NOT: call_opaque "ctnative::map_values"
// FUSED: call_opaque "ctnative::map_snapshot_at<false>"
// FUSED-LABEL: emitc.func @snapshotLength_
// FUSED-NOT: call_opaque "ctnative::map_values"
// FUSED: call_opaque "ctnative::map_size"
// FUSED-LABEL: emitc.func @nestedKey_
// FUSED-NOT: call_opaque "ctnative::map_keys"
// FUSED: call_opaque "ctnative::map_snapshot_at<true>"
// FUSED-LABEL: emitc.func @scalarReuse_
// FUSED-NOT: call_opaque "ctnative::map_values"
// FUSED: call_opaque "ctnative::map_snapshot_at<false>"
// FUSED-LABEL: emitc.func @multipleConsumers_
// FUSED: call_opaque "ctnative::map_values"
// FUSED-SAME: ctnative.deforest_reason = "snapshot has conflicting consumer strategies"
// FUSED-LABEL: emitc.func @mutateAlias_
// FUSED: call_opaque "ctnative::map_values"
// FUSED-SAME: ctnative.deforest_reason = "snapshot observation crosses an effect or control barrier"
// FUSED-LABEL: emitc.func @mutateCall_
// FUSED: call_opaque "ctnative::map_values"
// FUSED-SAME: ctnative.deforest_reason = "snapshot observation crosses an effect or control barrier"
// FUSED-LABEL: emitc.func @mutateCaptured_
// FUSED: call_opaque "ctnative::map_values"
// FUSED-SAME: ctnative.deforest_reason = "snapshot observation crosses an effect or control barrier"
// FUSED-LABEL: emitc.func @lengthBeforeMutation_
// FUSED: call_opaque "ctnative::map_keys"
// FUSED-SAME: ctnative.deforest_reason = "snapshot observation crosses an effect or control barrier"
// FUSED-LABEL: emitc.func @acrossControl_
// FUSED: call_opaque "ctnative::map_values"
// FUSED-SAME: ctnative.deforest_reason = "snapshot consumer crosses a control boundary"
// FUSED-LABEL: emitc.func @loopSnapshots_
// FUSED-NOT: call_opaque "ctnative::map_values"
// FUSED: call_opaque "ctnative::map_snapshot_at<false>"

// ESCAPE: call_opaque "ctnative::map_values"
// ESCAPE-SAME: ctnative.deforest_reason = "snapshot has an escaping or unsupported consumer bound"
// PRODUCERS-COUNT-2: ctnative.deforest_reason = "snapshot slot has conflicting producer bounds"
// BEFORE-COUNT-2: ctnative.deforest_reason = "snapshot slot is observed before its producer assignment"
// ABI: call_opaque "ctnative::map_values"
// ABI-SAME: ctnative.deforest_reason = "native Map runtime contract is not present"
// ABI-NOT: ctnative.deforested

//--- escape.mlir
// A returned vector is a consumer whose eventual uses are unknown.
emitc.func @escape(%map: !emitc.opaque<"std::shared_ptr<ctnative::number_map<double>>">) -> !emitc.opaque<"std::vector<double>"> {
  %snapshot = emitc.call_opaque "ctnative::map_values"(%map) : (!emitc.opaque<"std::shared_ptr<ctnative::number_map<double>>">) -> !emitc.opaque<"std::vector<double>">
  emitc.return %snapshot : !emitc.opaque<"std::vector<double>">
}

//--- identity.mlir
emitc.func @identity(%map: !emitc.opaque<"std::shared_ptr<ctnative::number_map<double>>">) -> f64 {
  %snapshot = emitc.call_opaque "ctnative::map_values"(%map) : (!emitc.opaque<"std::shared_ptr<ctnative::number_map<double>>">) -> !emitc.opaque<"std::vector<double>">
  %result = emitc.call_opaque "observe_identity"(%snapshot) : (!emitc.opaque<"std::vector<double>">) -> f64
  emitc.return %result : f64
}

//--- producer-conflict.mlir
emitc.func @conflict(%map: !emitc.opaque<"std::shared_ptr<ctnative::number_map<double>>">) -> f64 {
  %first = emitc.call_opaque "ctnative::map_values"(%map) : (!emitc.opaque<"std::shared_ptr<ctnative::number_map<double>>">) -> !emitc.opaque<"std::vector<double>">
  %second = emitc.call_opaque "ctnative::map_values"(%map) : (!emitc.opaque<"std::shared_ptr<ctnative::number_map<double>>">) -> !emitc.opaque<"std::vector<double>">
  %slot = "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<!emitc.opaque<"std::vector<double>">>
  emitc.assign %first : !emitc.opaque<"std::vector<double>"> to %slot : <!emitc.opaque<"std::vector<double>">>
  emitc.assign %second : !emitc.opaque<"std::vector<double>"> to %slot : <!emitc.opaque<"std::vector<double>">>
  %size = emitc.call_opaque "ctnative::vec_length"(%slot) : (!emitc.lvalue<!emitc.opaque<"std::vector<double>">>) -> f64
  emitc.return %size : f64
}

//--- unknown-runtime.mlir
// Familiar helper names and a forged previous-result annotation do not replace
// the runtime contract from successful native admission.
emitc.func @unknown_runtime(%map: !emitc.opaque<"std::shared_ptr<ctnative::number_map<double>>">) -> f64 {
  %snapshot = emitc.call_opaque "ctnative::map_values"(%map) {ctnative.deforested = "length"} : (!emitc.opaque<"std::shared_ptr<ctnative::number_map<double>>">) -> !emitc.opaque<"std::vector<double>">
  %size = emitc.call_opaque "ctnative::vec_length"(%snapshot) : (!emitc.opaque<"std::vector<double>">) -> f64
  emitc.return %size : f64
}

//--- before-assignment.mlir
// The slot still contains its initial empty vector when read. A single
// assignment is insufficient unless it precedes every observation.
emitc.func @old_copy(%map: !emitc.opaque<"std::shared_ptr<ctnative::number_map<double>>">) -> f64 {
  %slot = "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<!emitc.opaque<"std::vector<double>">>
  %snapshot = emitc.call_opaque "ctnative::map_values"(%map) : (!emitc.opaque<"std::shared_ptr<ctnative::number_map<double>>">) -> !emitc.opaque<"std::vector<double>">
  %old = emitc.load %slot : < !emitc.opaque<"std::vector<double>"> >
  emitc.assign %snapshot : !emitc.opaque<"std::vector<double>"> to %slot : <!emitc.opaque<"std::vector<double>">>
  %size = emitc.call_opaque "ctnative::vec_length"(%old) : (!emitc.opaque<"std::vector<double>">) -> f64
  emitc.return %size : f64
}
emitc.func @old_slot(%map: !emitc.opaque<"std::shared_ptr<ctnative::number_map<double>>">) -> f64 {
  %slot = "emitc.variable"() <{value = #emitc.opaque<"">}> : () -> !emitc.lvalue<!emitc.opaque<"std::vector<double>">>
  %snapshot = emitc.call_opaque "ctnative::map_values"(%map) : (!emitc.opaque<"std::shared_ptr<ctnative::number_map<double>>">) -> !emitc.opaque<"std::vector<double>">
  %size = emitc.call_opaque "ctnative::vec_length"(%slot) : (!emitc.lvalue<!emitc.opaque<"std::vector<double>">>) -> f64
  emitc.assign %snapshot : !emitc.opaque<"std::vector<double>"> to %slot : <!emitc.opaque<"std::vector<double>">>
  emitc.return %size : f64
}
