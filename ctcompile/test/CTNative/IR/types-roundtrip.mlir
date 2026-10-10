// Every ctnative type, parsed and printed back.
//
// The driver is ctjs-opt, which also registers the ctnative dialect (see
// lib/Support/MLIRContextSetup.cpp); there is no separate ctnative-opt.
//
// A round trip proves more than it seems to. The dialect sets
// useDefaultTypePrinterParser and every parameterized type carries an
// assemblyFormat, so nothing here is hand-written: this test checks that the
// ODS definitions are right. An EnumParameter whose enum was declared with the
// wrong cppNamespace, an assemblyFormat that omits a parameter, or an
// ArrayRefParameter with no separator each generates a printer and a parser
// that disagree, and each shows up here as a line that comes back different
// from the way it went in.
//
// The types are carried in func.func signatures so the test needs no ctnative
// operation to hold them; a signature is enough to push a type through one
// parse and one print. The second ctjs-opt reparses what the first printed, so
// a printer and parser that disagree fail the pipeline or the expected text.


// RUN: ctjs-opt %s | ctjs-opt | FileCheck %s

// --- the two ends ------------------------------------------------------------

// CHECK-LABEL: func.func private @ends
// CHECK-SAME: (!ctnative.bottom, !ctnative.boxed) -> !ctnative.boxed
func.func private @ends(!ctnative.bottom, !ctnative.boxed) -> !ctnative.boxed

// --- the scalars, with their parameters --------------------------------------

// The three numeric kinds print as the .td file spells them. `i32`, `i64` and
// `f64` are the enum's assembly strings; getting one wrong would mean the
// lattice's `max` orders something other than what a reader sees.
// CHECK-LABEL: func.func private @numbers
// CHECK-SAME: (!ctnative.num<i32>, !ctnative.num<i64>, !ctnative.num<f64>)
func.func private @numbers(!ctnative.num<i32>, !ctnative.num<i64>, !ctnative.num<f64>)

// CHECK-LABEL: func.func private @scalars
// CHECK-SAME: (!ctnative.bool, !ctnative.json)
func.func private @scalars(!ctnative.bool, !ctnative.json)

// Both encodings, and both spellings of a string. The encoding parameter is the
// reason this type exists; a strview that dropped it would print as
// `!ctnative.strview` and still parse, which is exactly the silent ODS mistake.
// CHECK-LABEL: func.func private @strings
// CHECK-SAME: (!ctnative.str<utf8>, !ctnative.str<utf16>, !ctnative.strview<utf8>, !ctnative.strview<utf16>)
func.func private @strings(!ctnative.str<utf8>, !ctnative.str<utf16>,
                           !ctnative.strview<utf8>, !ctnative.strview<utf16>)

// --- the lift and the union --------------------------------------------------

// CHECK-LABEL: func.func private @lifted
// CHECK-SAME: (!ctnative.opt<!ctnative.num<f64>>, !ctnative.opt<!ctnative.vec<!ctnative.bool>>)
func.func private @lifted(!ctnative.opt<!ctnative.num<f64>>,
                          !ctnative.opt<!ctnative.vec<!ctnative.bool>>)

// An ArrayRefParameter with four entries, which is the cap. A separator the
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

// --- nominal callable targets -----------------------------------------------

// The imported symbol suffix is part of the target identity. Losing it would
// make two distinct callees indistinguishable after printing and reparsing.
// CHECK-LABEL: func.func private @callables
// CHECK-SAME: (!ctnative.closure<"get$1">, !ctnative.closure<"get$2">) -> !ctnative.closure<"get$1">
func.func private @callables(!ctnative.closure<"get$1">,
                             !ctnative.closure<"get$2">) -> !ctnative.closure<"get$1">

// Targets are MLIR strings, including names that cannot be C++ identifiers.
// Both legal input spellings must print the same quoted target and retain it
// through the second parse. This catches dropped or double-escaped bytes.
// CHECK-LABEL: func.func private @escaped_callable
// CHECK-SAME: (!ctnative.closure<"get \22quoted\22 \\owner$7">)
func.func private @escaped_callable(!ctnative.closure<"get \"quoted\" \\owner$7">)

// CHECK-LABEL: func.func private @hex_escaped_callable
// CHECK-SAME: (!ctnative.closure<"get \22quoted\22 \\owner$7">)
func.func private @hex_escaped_callable(!ctnative.closure<"get \22quoted\22 \5Cowner$7">)

// CHECK-LABEL: func.func private @optional_callables
// CHECK-SAME: (!ctnative.opt<!ctnative.variant<!ctnative.closure<"get$1">, !ctnative.closure<"get$2">>>)
func.func private @optional_callables(
    !ctnative.opt<!ctnative.variant<!ctnative.closure<"get$1">, !ctnative.closure<"get$2">>>)

// --- nominal method-table sites --------------------------------------------

// A site identifies the full field schema, not a runtime object identity.
// Both sites and the distinction from a callable survive a round trip.
// CHECK-LABEL: func.func private @method_tables
// CHECK-SAME: (!ctnative.method_table<"table$1">, !ctnative.method_table<"table$2">, !ctnative.closure<"table$1">) -> !ctnative.method_table<"table$1">
func.func private @method_tables(!ctnative.method_table<"table$1">,
    !ctnative.method_table<"table$2">, !ctnative.closure<"table$1">) -> !ctnative.method_table<"table$1">

// CHECK-LABEL: func.func private @optional_method_tables
// CHECK-SAME: (!ctnative.opt<!ctnative.variant<!ctnative.method_table<"table$1">, !ctnative.method_table<"table$2">>>)
func.func private @optional_method_tables(
    !ctnative.opt<!ctnative.variant<!ctnative.method_table<"table$1">, !ctnative.method_table<"table$2">>>)

// CHECK-LABEL: func.func private @escaped_method_table
// CHECK-SAME: (!ctnative.method_table<"make \22table\22 \\site$7">)
func.func private @escaped_method_table(!ctnative.method_table<"make \"table\" \\site$7">)

// CHECK-LABEL: func.func private @hex_escaped_method_table
// CHECK-SAME: (!ctnative.method_table<"make \22table\22 \\site$7">)
func.func private @hex_escaped_method_table(!ctnative.method_table<"make \22table\22 \5Csite$7">)

// --- property-free object identities ----------------------------------------

// Runtime allocations remain distinct even though their carrier has no type
// parameter. Optionality and use as a Map key survive parsing and printing.
// CHECK-LABEL: func.func private @object_identities
// CHECK-SAME: (!ctnative.object_identity, !ctnative.opt<!ctnative.object_identity>) -> !ctnative.object_identity
func.func private @object_identities(!ctnative.object_identity,
    !ctnative.opt<!ctnative.object_identity>) -> !ctnative.object_identity

// CHECK-LABEL: func.func private @identity_keyed_maps
// CHECK-SAME: (!ctnative.map<!ctnative.object_identity, !ctnative.num<f64>>, !ctnative.map<!ctnative.object_identity, !ctnative.map<!ctnative.str<utf8>, !ctnative.num<f64>>>)
func.func private @identity_keyed_maps(!ctnative.map<!ctnative.object_identity, !ctnative.num<f64>>,
    !ctnative.map<!ctnative.object_identity, !ctnative.map<!ctnative.str<utf8>, !ctnative.num<f64>>>)

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
