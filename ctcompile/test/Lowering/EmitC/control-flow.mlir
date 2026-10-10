// A function with a branch, compiled through the full pipeline, including the
// pass that routes around the C++ emitter's block-argument bug.
//
// end-to-end.mlir covers a straight-line function. Here an `if` gives the
// importer four blocks and puts the register file into block arguments, which
// is precisely the shape mlir-translate miscompiles. The RUN line below is
// therefore the real pipeline (lower, then eliminate block arguments, then
// translate), and the order of those two passes is a correctness requirement,
// not a preference.
//
// This one line of JavaScript needs four lowerings:
//
//   ctjs.truthy, and the comparison that goes with it. ct_aot_truthy answers
//   with a uint32_t because the ABI has no bool, while cf.cond_br takes an i1.
//   The `!= 0` is the conversion, written out rather than left to C++'s
//   implicit narrowing.
//
//   Block arguments as variables, written on split edges. The importer emits
//   `cf.cond_br %c, ^bb1(%x, %y), ^bb2(%x, %y)`: two edges into two blocks,
//   each carrying the live registers.
//
//   Number constants, spelled from their bits. `1` and `2` arrive as
//   #ctjs.number<4607182418800017408> and <4611686018427387904>, the double's
//   bit pattern rather than a float, so that -0.0 and NaN payloads survive.
//   Emitting a decimal literal would throw that away at the last step;
//   bit_cast keeps it exact, and value::number does the boxing so nothing
//   here depends on the NaN-boxing scheme.
//
//   Multi-block lowering, which relies on --emitc-eliminate-block-arguments
//   running afterwards to remove the block arguments it leaves on non-entry
//   blocks.

// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-lower-to-emitc --emitc-eliminate-block-arguments \
// RUN:   | mlir-translate --mlir-to-cpp --declare-variables-at-top \
// RUN:   | FileCheck %s

// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-lower-to-emitc --emitc-eliminate-block-arguments \
// RUN:   | mlir-translate --mlir-to-cpp --declare-variables-at-top > %t.cpp \
// RUN:   && %cxx %t.cpp

function g(a) { if (a) { return 1; } return 2; }

// CHECK: extern "C" int32_t g_1(

// The condition: a helper call, then an explicit test against zero.
// CHECK: [[BOOL:v[0-9]+]] = ctbrowser::aot::ct_aot_truthy(
// CHECK-NEXT: {{v[0-9]+}} = [[BOOL]] != 0;

// Both constants are exact, from their bits. 4607182418800017408 is 1.0 and
// 4611686018427387904 is 2.0; neither ever becomes a decimal literal.
// CHECK-DAG: ct_aot_return_value(ctbrowser::script::value::number(std::bit_cast<double>(UINT64_C(4607182418800017408))).bits()
// CHECK-DAG: ct_aot_return_value(ctbrowser::script::value::number(std::bit_cast<double>(UINT64_C(4611686018427387904))).bits()

// No block arguments survive into the translated output; if any had, the
// emitter would have serialised their copies and this file would be pinning a
// miscompile rather than a compilation. There is nothing to assert positively
// here: the evidence is that %cxx accepts the result and that
// block-arguments-eliminated.mlir runs the same transformation and checks its
// answers.
// CHECK: return static_cast<int32_t>(ctbrowser::aot::ct_aot_status::ok);
