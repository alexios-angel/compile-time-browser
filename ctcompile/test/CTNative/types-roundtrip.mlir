// EVERY ctnative TYPE, PARSED AND PRINTED BACK.
//
// Part 24's gate for Phase 53 opens with "the types round-trip through
// ctnative-opt". This is that, and the driver is ctjs-opt rather than a second
// executable - see MLIRContextSetup.cpp, where the dialect is registered and
// the deviation is written down.
//
// WHAT A ROUND TRIP ACTUALLY PROVES, and it is more than it looks. The dialect
// sets useDefaultTypePrinterParser and every parameterized type carries an
// assemblyFormat, so nothing here is hand-written - which means this test is
// checking that the ODS is RIGHT, not that a parser is. An EnumParameter whose
// enum was declared with the wrong cppNamespace, an assemblyFormat that omits a
// parameter, an ArrayRefParameter with no separator: each of those generates a
// printer and a parser that disagree, and each shows up here as a line that
// comes back different from the way it went in.
//
// THE TYPES APPEAR IN FUNCTION SIGNATURES because Phase 53 has NO OPERATIONS -
// that is the phase, not an omission - and func.func is enough to carry a type
// through a parse and a print without one.

// RUN: ctjs-opt %s | ctjs-opt | FileCheck %s

// --- the two ends ------------------------------------------------------------

// CHECK-LABEL: func.func private @ends
// CHECK-SAME: (!ctnative.bottom, !ctnative.boxed) -> !ctnative.boxed
func.func private @ends(!ctnative.bottom, !ctnative.boxed) -> !ctnative.boxed

// --- the scalars, with their parameters --------------------------------------

// THE THREE NUMERIC KINDS PRINT AS THE td SPELLS THEM. `i32`, `i64` and `f64`
// are the enum's assembly strings, and getting one wrong here would mean the
// lattice's `max` was ordering something other than what a reader sees.
// CHECK-LABEL: func.func private @numbers
// CHECK-SAME: (!ctnative.num<i32>, !ctnative.num<i64>, !ctnative.num<f64>)
func.func private @numbers(!ctnative.num<i32>, !ctnative.num<i64>, !ctnative.num<f64>)

// CHECK-LABEL: func.func private @scalars
// CHECK-SAME: (!ctnative.bool, !ctnative.json)
func.func private @scalars(!ctnative.bool, !ctnative.json)

// BOTH ENCODINGS, AND BOTH SPELLINGS OF A STRING. The parameter is the reason
// this type exists; a strview that dropped it would print as `!ctnative.strview`
// and still parse, which is exactly the silent kind of ODS mistake.
// CHECK-LABEL: func.func private @strings
// CHECK-SAME: (!ctnative.str<utf8>, !ctnative.str<utf16>, !ctnative.strview<utf8>, !ctnative.strview<utf16>)
func.func private @strings(!ctnative.str<utf8>, !ctnative.str<utf16>,
                           !ctnative.strview<utf8>, !ctnative.strview<utf16>)

// --- the lift and the union --------------------------------------------------

// CHECK-LABEL: func.func private @lifted
// CHECK-SAME: (!ctnative.opt<!ctnative.num<f64>>, !ctnative.opt<!ctnative.vec<!ctnative.bool>>)
func.func private @lifted(!ctnative.opt<!ctnative.num<f64>>,
                          !ctnative.opt<!ctnative.vec<!ctnative.bool>>)

// AN ArrayRefParameter WITH FOUR ENTRIES, which is the cap. A separator the
// printer emits and the parser does not accept is the classic failure of this
// parameter kind, and it needs more than two elements to show up.
// CHECK-LABEL: func.func private @unions
// CHECK-SAME: !ctnative.variant<!ctnative.bool, !ctnative.num<f64>>
// CHECK-SAME: !ctnative.variant<!ctnative.bool, !ctnative.num<f64>, !ctnative.str<utf8>, !ctnative.vec<!ctnative.bool>>
func.func private @unions(!ctnative.variant<!ctnative.bool, !ctnative.num<f64>>,
                          !ctnative.variant<!ctnative.bool, !ctnative.num<f64>,
                                            !ctnative.str<utf8>, !ctnative.vec<!ctnative.bool>>)

// --- the containers ----------------------------------------------------------

// CHECK-LABEL: func.func private @containers
// CHECK-SAME: !ctnative.vec<!ctnative.num<f64>>
// CHECK-SAME: !ctnative.map<!ctnative.str<utf8>, !ctnative.num<f64>>
// CHECK-SAME: !ctnative.set<!ctnative.str<utf8>>
func.func private @containers(!ctnative.vec<!ctnative.num<f64>>,
                              !ctnative.map<!ctnative.str<utf8>, !ctnative.num<f64>>,
                              !ctnative.set<!ctnative.str<utf8>>)

// --- the three pointers ------------------------------------------------------

// CHECK-LABEL: func.func private @pointers
// CHECK-SAME: !ctnative.owned<!ctnative.num<f64>>
// CHECK-SAME: !ctnative.shared<!ctnative.num<f64>>
// CHECK-SAME: !ctnative.weak<!ctnative.num<f64>>
func.func private @pointers(!ctnative.owned<!ctnative.num<f64>>,
                            !ctnative.shared<!ctnative.num<f64>>,
                            !ctnative.weak<!ctnative.num<f64>>)

// --- and one nested deeply enough to catch a recursive printer ---------------

// CHECK-LABEL: func.func private @nested
// CHECK-SAME: !ctnative.opt<!ctnative.map<!ctnative.str<utf16>, !ctnative.vec<!ctnative.variant<!ctnative.bool, !ctnative.num<i32>>>>>
func.func private @nested(!ctnative.opt<!ctnative.map<!ctnative.str<utf16>,
    !ctnative.vec<!ctnative.variant<!ctnative.bool, !ctnative.num<i32>>>>>)
