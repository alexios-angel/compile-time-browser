//===- CTNativeEnums.td - The parameters a ctnative type carries ----------===//
//
// NumKind and StrEncoding are enums whose numeric values are their lattice
// rank. That is why they exist instead of three separate `num` types and two
// separate `str` types: a meet over a total order is `max` (see `meet` in
// CTNativeLattice.h), so no separate ordering table can drift from the enum.
//
// The order of their cases therefore defines the type lattice, and reordering
// them silently changes it. ctcompile/test/Analysis/Types/Lattice.cpp
// static_asserts both chains, so a reorder is a build error rather than a
// wrong answer. EscapeReason, at the end, is not ordered.
//
//===----------------------------------------------------------------------===//
#ifndef CTCOMPILE_CTNATIVE_ENUMS
#define CTCOMPILE_CTNATIVE_ENUMS

include "mlir/IR/EnumAttr.td"

// No specialized attribute: these enums parameterize types; they are not
// attributes on operations. Generating a NumKindAttr the dialect never
// registers would be a class nobody can parse and a registration hook this
// dialect does not have. NVGPU's TensorMapSwizzleKind does the same thing for
// the same reason.
class CTNative_I32Enum<string name, string summary, list<I32EnumAttrCase> cases>
    : I32EnumAttr<name, summary, cases> {
  let cppNamespace = "::ctcompile::ctnative";
  let genSpecializedAttr = 0;
}

// The numeric chain, `i32 -> i64 -> f64`.
//
// Every JavaScript number is an IEEE-754 double, so f64 is the only member of
// this chain that is always correct; i32 and i64 are refinements, used only
// when a range proof bounds the value. A refinement sits below the truth in a
// lattice whose top is "boxed", so f64 is the largest of the three, and a
// value that is i32 on one path and f64 on another is f64.
//
// i64 -> f64 is not value-preserving above 2^53, and that is not a defect here:
// both operands descend from a JavaScript double, so an i64 that meets an f64
// was only ever an f64 that a bound proof had narrowed. The unsound direction
// is the other one - f64 narrowing to i64 - and this chain cannot express it.
def CTNative_NumKind : CTNative_I32Enum<"NumKind", "The C++ arithmetic type a JS number lowers to", [
    I32EnumAttrCase<"I32", 0, "i32">,
    I32EnumAttrCase<"I64", 1, "i64">,
    I32EnumAttrCase<"F64", 2, "f64">,
  ]>;

// The encoding chain, `utf8 -> utf16`, ordered by representational width
// rather than by preference.
//
// UTF-16 is the wider representation: every UTF-8 sequence has a UTF-16 form,
// and UTF-16 additionally holds lone surrogates, which UTF-8 cannot encode at
// all and which ECMA-262 makes legal inside a string. So utf8 is the refinement
// and utf16 is the widening, and meet is max exactly as it is for the numbers.
//
// Which one is the default is a separate question (utf8, for this engine);
// see kDefaultStringEncoding in CTNativeLattice.h and entry ND-1 of
// ctcompile/docs/native-divergences.md.
def CTNative_StrEncoding : CTNative_I32Enum<"StrEncoding", "How a JS string is represented in C++", [
    I32EnumAttrCase<"UTF8",  0, "utf8">,
    I32EnumAttrCase<"UTF16", 1, "utf16">,
  ]>;

// Why an allocation site is not confined to its function's activation.
//
// Not a lattice: unlike the two enums above, the order of these cases means
// nothing, so do not static_assert a chain over it. `Confined` is the only
// non-escaping case. Every other case is the first reason escape analysis
// found that an object allocated at the site can outlive its activation, and
// the ownership lowering uses it to pick an owner: `returned` can become a
// std::unique_ptr moved out, `stored`/`captured`/`passed` a std::shared_ptr,
// and `arguments`, `suspended`, `unknown_op`, `unvisited*` and similar are
// outside the native subset and become a compile-time diagnostic naming the
// site and the reason. The native target has no garbage collector to fall back
// on, so the lowering depends on the reason, and the analysis keeps the first
// one it finds rather than the last.
//
// `phase59` marks a closure or captured-variable cell: escape analysis does not
// track these yet, so such a site is reported as escaping with this reason.
// `not_tracked`, `runtime_array` and `arguments` are the other routes CTJS
// ops declare (see CTJSEscapeEffects.h).
//
// The tail of the list is the analysis talking about itself: `unknown_op` is
// the default rule (an operation with no escape annotation sinks every
// operand), `unvisited` is a site the solver never reached in a live block,
// `unvisited_operand` is a sink whose operand it never reached. Both are
// counted and gated at zero; neither is ever confined.
//
// No attribute, like the two above: this dialect registers none and has no
// attribute printer/parser, and a generated EscapeReasonAttr would be a class
// nobody can parse. stringifyEscapeReason is what the claims file, the unit
// test and the histogram print; symbolizeEscapeReason is how the analysis
// turns a ctjs escape route's name back into one of these.
def CTNative_EscapeReason : CTNative_I32Enum<"EscapeReason", "why a site is not confined", [
    I32EnumAttrCase<"Confined",          0,  "confined">,
    I32EnumAttrCase<"Returned",          1,  "returned">,
    I32EnumAttrCase<"Thrown",            2,  "thrown">,
    I32EnumAttrCase<"Stored",            3,  "stored">,
    I32EnumAttrCase<"StoredGlobal",      4,  "stored_global">,
    I32EnumAttrCase<"Captured",          5,  "captured">,
    I32EnumAttrCase<"Passed",            6,  "passed">,
    I32EnumAttrCase<"Converted",         7,  "converted">,
    I32EnumAttrCase<"ProtoMutated",      8,  "proto_mutated">,
    I32EnumAttrCase<"AccessorDefined",   9,  "accessor_defined">,
    I32EnumAttrCase<"Arguments",         10, "arguments">,
    I32EnumAttrCase<"RuntimeArray",      11, "runtime_array">,
    I32EnumAttrCase<"Phase59",           12, "phase59">,
    I32EnumAttrCase<"NotTracked",        13, "not_tracked">,
    I32EnumAttrCase<"Suspended",         14, "suspended">,
    I32EnumAttrCase<"ArgumentsLate",     15, "arguments_late">,
    I32EnumAttrCase<"UnknownOp",         16, "unknown_op">,
    I32EnumAttrCase<"Unvisited",         17, "unvisited">,
    I32EnumAttrCase<"UnvisitedOperand",  18, "unvisited_operand">,
  ]>;

#endif // CTCOMPILE_CTNATIVE_ENUMS
