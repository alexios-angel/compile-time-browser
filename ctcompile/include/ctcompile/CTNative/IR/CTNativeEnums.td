//===- CTNativeEnums.td - the two parameters a ctnative type carries ------===//
//
// TWO ENUMS, AND IN BOTH OF THEM THE NUMERIC VALUE IS THE LATTICE RANK.
//
// That is the whole reason they are here rather than three separate `num` types
// and two separate `str` types. Phase 53B needs a meet, and a meet over a total
// order is `max`. Writing the order into the enumerator values makes that ONE
// LINE of C++ instead of a table - and a table is the thing part 24 says must
// not exist twice:
//
//   "Meet is a function, and it must be written once, because two places
//    computing 'what type is this' differently is how a transpiler produces a
//    program that is right on Tuesday."
//
// So THE ORDER OF THESE CASES IS LOAD-BEARING, and reordering them silently
// changes the type lattice. CTNativeLattice.cpp static_asserts both chains so
// that a reorder is a build error rather than a wrong answer.
//
//===----------------------------------------------------------------------===//
#ifndef CTCOMPILE_CTNATIVE_ENUMS
#define CTCOMPILE_CTNATIVE_ENUMS

include "mlir/IR/EnumAttr.td"

// THE NUMERIC CHAIN, `i32 -> i64 -> f64`, exactly as part 24 §53B writes it.
//
// Every JavaScript number IS an IEEE-754 double, so f64 is the only member of
// this chain that is unconditionally correct; i32 and i64 are REFINEMENTS and
// part 24 says so in as many words - "ONLY behind Phase 55's bound proof".
// A refinement sits BELOW the truth in a lattice whose top is "boxed", so f64
// is the largest of the three, and a value that is i32 on one path and f64 on
// another is f64.
//
// i64 -> f64 IS NOT VALUE-PRESERVING above 2^53, and that is not a defect here:
// both operands descend from a JavaScript double, so an i64 that meets an f64
// was only ever an f64 that a bound proof had narrowed. The unsound direction
// is the other one - f64 narrowing to i64 - and this chain cannot express it.
def CTNative_NumKind : I32EnumAttr<"NumKind", "The C++ arithmetic type a JS number lowers to", [
    I32EnumAttrCase<"I32", 0, "i32">,
    I32EnumAttrCase<"I64", 1, "i64">,
    I32EnumAttrCase<"F64", 2, "f64">,
  ]> {
  let cppNamespace = "::ctcompile::ctnative";
  // NO SPECIALIZED ATTRIBUTE. These enums parameterize TYPES; they are not
  // attributes on operations. Generating a NumKindAttr the dialect never
  // registers would be a class nobody can parse and a registration hook this
  // dialect does not have. NVGPU's TensorMapSwizzleKind does the same thing for
  // the same reason.
  let genSpecializedAttr = 0;
}

// THE ENCODING CHAIN, `utf8 -> utf16`, ordered by REPRESENTATIONAL WIDTH rather
// than by preference.
//
// UTF-16 is the wider representation: every UTF-8 sequence has a UTF-16 form,
// and UTF-16 additionally holds LONE SURROGATES, which UTF-8 cannot encode at
// all and which ECMA-262 makes legal inside a string. So utf8 is the refinement
// and utf16 is the widening, and meet is max exactly as it is for the numbers.
//
// WHICH OF THE TWO IS THE DEFAULT IS A SEPARATE QUESTION, AND PART 24 GETS IT
// WRONG FOR THIS ENGINE. See kDefaultStringEncoding in CTNativeLattice.h and
// entry ND-1 of ctcompile/docs/native-divergences.md.
def CTNative_StrEncoding : I32EnumAttr<"StrEncoding", "How a JS string is represented in C++", [
    I32EnumAttrCase<"UTF8",  0, "utf8">,
    I32EnumAttrCase<"UTF16", 1, "utf16">,
  ]> {
  let cppNamespace = "::ctcompile::ctnative";
  let genSpecializedAttr = 0;
}

// PHASE 55'S ONE NEW DEF: WHY A SITE IS NOT CONFINED.
//
// NOT A LATTICE, and the order carries nothing - which is the opposite of the
// two enums above and worth saying so nobody static_asserts a chain over it.
// `Confined` is the only non-escaping case. Every other case is the FIRST
// reason the analysis found that an object born at a site can outlive its
// activation, and Phase 55B reads that reason to choose an OWNER: `returned`
// is a candidate for std::unique_ptr moved out, `stored`/`captured`/`passed`
// for std::shared_ptr, and `arguments`, `suspended`, `unknown_op`,
// `unvisited*` and the like are outside the native subset and become a
// compile-time diagnostic naming the site and the reason. There is no
// collector on the native target to fall back on, so the reason is
// load-bearing for the lowering and the analysis keeps the first one it finds
// rather than the last.
//
// The tail of the list is the analysis talking about ITSELF: `unknown_op` is
// the default rule (an operation with no escape annotation sinks every
// operand), `unvisited` is a site the solver never reached in a live block,
// `unvisited_operand` is a sink whose operand it never reached. Both are
// counted and gated at zero; neither is ever confined.
//
// NO ATTRIBUTE, like the two above: this dialect registers none and has no
// attribute printer/parser, and a generated EscapeReasonAttr would be a class
// nobody can parse. stringifyEscapeReason is what the claims file, the unit
// test and the histogram print; symbolizeEscapeReason is how the analysis
// turns a ctjs escape route's name back into one of these.
def CTNative_EscapeReason : I32EnumAttr<"EscapeReason", "why a site is not confined", [
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
  ]> {
  let cppNamespace = "::ctcompile::ctnative";
  let genSpecializedAttr = 0;
}

#endif // CTCOMPILE_CTNATIVE_ENUMS
