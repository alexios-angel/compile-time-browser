// `!ctnative.tuple`, parsed and printed back: the two positions css-dump.js's
// `[name, value]` rows need, a nested element type, and a vector of tuples.
// The second ctjs-opt reparses what the first printed, so a printer and
// parser that disagree fail the pipeline or the expected text.

// RUN: ctjs-opt %s | ctjs-opt | FileCheck %s

// CHECK-LABEL: func.func private @pairs
// CHECK-SAME: (!ctnative.tuple<!ctnative.str<utf8>, !ctnative.variant<!ctnative.num<f64>, !ctnative.str<utf8>>>)
func.func private @pairs(!ctnative.tuple<!ctnative.str<utf8>, !ctnative.variant<!ctnative.num<f64>, !ctnative.str<utf8>>>)

// CHECK-LABEL: func.func private @rows
// CHECK-SAME: (!ctnative.vec<!ctnative.tuple<!ctnative.str<utf8>, !ctnative.dom_element>>)
func.func private @rows(!ctnative.vec<!ctnative.tuple<!ctnative.str<utf8>, !ctnative.dom_element>>)

// CHECK-LABEL: func.func private @wider
// CHECK-SAME: (!ctnative.opt<!ctnative.tuple<!ctnative.bool, !ctnative.num<i32>, !ctnative.str<utf8>>>)
func.func private @wider(!ctnative.opt<!ctnative.tuple<!ctnative.bool, !ctnative.num<i32>, !ctnative.str<utf8>>>)
