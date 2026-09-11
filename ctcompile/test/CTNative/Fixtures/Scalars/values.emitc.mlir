// THE SPECIFICATION OF THE PRINTING, for values that are not numbers.
//
// HAND-WRITTEN, for the same reason native-fixture.emitc.mlir is: it is what
// the Phase 62½-C lowering is expected to produce for native-values-fixture.js,
// written BEFORE that lowering can produce it, so that the GATE is proved on
// something before there is anything to gate. LowerToEmitC.cpp emits only
// `<name>=%.17g` today and declares every global `double`; this module is the
// one-hunk description of what it has to emit instead, and
// check-native-unit.cmake compiles it standalone, runs it, and compares every
// line with the interpreter's answer for the same JavaScript.
//
// THE CONVENTION - defined in check-native-unit.cmake, mirrored in
// NativeReference.cpp, produced here:
//
//     Number      std::printf("%s=%.17g\n", "<name>", (double) <global>)
//     Boolean     ctnative::print_boolean("<name>", <global>)
//     undefined   ctnative::print_undefined("<name>")
//     null        ctnative::print_null("<name>")
//     String      ctnative::print_string("<name>", <global>)
//
// one line per global, ascending bytewise by name, and `main` returns 0. THE
// NUMBER CASE IS THE CALL IT ALWAYS WAS, unchanged to the byte, because every
// fixture that predates the other four kinds expects exactly that text.
//
// THE REPRESENTATIONS, and why there are only three of them for five kinds:
//
//   * a Number is `double` (or `int32_t` widened at the print, as before)
//   * a Boolean is `bool`
//   * a String is `std::string`
//   * `undefined` and `null` GET NO STORAGE AT ALL. A global whose proven type
//     is one of those has one possible value, which is a compile-time fact, so
//     there is nothing to keep at runtime and nothing to load - the printing is
//     a constant. Giving them a tagged slot would be boxing a value this tier
//     has already proved, which is the one thing it does not do.
//
// There is NO discriminator anywhere in the emitted C++: the kind of every
// global is settled at compile time by the type the inference gave it, and it
// is settled again, independently, by the interpreter at run time. The gate
// passing is the two agreeing.
//
// WHAT IS NOT HERE, deliberately: nothing from the engine. No include, no
// helper reaching into it, no `ct_aot_*`. The gate compiles this standalone and
// runs `nm` on the result.

// AS A LIT TEST this file asserts the SHAPE of the emitted C++ and never a
// value: the values are the interpreter's to judge, in check-native-unit.cmake.
//
// RUN: ctjs-translate --mlir-to-cpp %s | FileCheck %s --implicit-check-not=ctbrowser
//
// CHECK: #include <cstdint>
// CHECK: #include <cstdio>
// CHECK: #include <string>
// CHECK: namespace ctnative {
// CHECK: inline void print_string(const char * name, const std::string & s) {
// CHECK: inline void print_boolean(const char * name, bool b) {
// CHECK: inline void print_undefined(const char * name) {
// CHECK: inline void print_null(const char * name) {
// CHECK: } // namespace ctnative
//
// THE THREE REPRESENTATIONS, and no fourth.
// CHECK: int32_t answer;
// CHECK: double third;
// CHECK: bool yes;
// CHECK: bool no;
// CHECK: std::string greeting;
//
// AND NO STORAGE FOR THE OTHER TWO KINDS. `nothing` and `missing` are globals
// of the program; neither may appear as a declaration.
// CHECK-NOT: nothing;
// CHECK-NOT: missing;
//
// CHECK-LABEL: int32_t main() {
// THE PRINTING, sorted bytewise by name - which is also the order the
// interpreter's side sorts in, and a line out of place is a gate failure.
// CHECK: std::printf("%s=%.17g\n", "answer", {{v[0-9]+}});
// CHECK: ctnative::print_string("empty", {{v[0-9]+}});
// CHECK: ctnative::print_string("greeting", {{v[0-9]+}});
// CHECK: ctnative::print_string("looks_null", {{v[0-9]+}});
// CHECK: ctnative::print_string("looks_numeric", {{v[0-9]+}});
// CHECK: ctnative::print_undefined("missing");
// CHECK: ctnative::print_boolean("no", {{v[0-9]+}});
// CHECK: ctnative::print_null("nothing");
// CHECK: ctnative::print_string("raw", {{v[0-9]+}});
// CHECK: std::printf("%s=%.17g\n", "third", {{v[0-9]+}});
// CHECK: ctnative::print_boolean("yes", {{v[0-9]+}});
// CHECK: return {{v[0-9]+}};

module {
  emitc.include <"cstdint">
  emitc.include <"cstdio">
  emitc.include <"string">

  // THE VALUE-PRINTING PRELUDE, one `emitc.verbatim` per line of C++ because
  // an MLIR string attribute cannot hold a newline unescaped and a single
  // 1,500-character line is not reviewable. The lowering will emit this as one
  // `emitc.verbatim` built from a StringLiteral, the way kVectorHelpers already
  // is; the TEXT is what has to match, not how it was assembled.
  //
  // NOT std::printf, ANYWHERE IN HERE, and that is load-bearing twice over.
  // A `%` in the data would be read as a conversion by a printf, and
  // check-native-unit.cmake's MUTATE path inserts its off-by-one in front of
  // the first `std::printf(` in the emitted C++ - which has to be the first
  // line `main` prints, not a line of a helper defined above it. The driver
  // asserts that placement; this is the half of the bargain the module keeps.
  emitc.verbatim "// ctcompile: the differential gate's value forms - the convention is stated"
  emitc.verbatim "// in ctcompile/test/check-native-unit.cmake and mirrored by the interpreter"
  emitc.verbatim "// side in ctcompile/test/NativeReference.cpp. The two must agree byte for"
  emitc.verbatim "// byte, and the gate failing on a string global is what says they do not."
  emitc.verbatim "namespace ctnative {"
  emitc.verbatim "// ctcompile: a String global. Percent-encoded per RFC 3986: the unreserved"
  emitc.verbatim "// set passes through and every other byte becomes % and two uppercase hex"
  emitc.verbatim "// digits. Total over all 256 byte values and injective, so a lone"
  emitc.verbatim "// surrogate's WTF-8 - which is not valid UTF-8 - and an embedded NUL both"
  emitc.verbatim "// survive, and the loop is over size() because a JavaScript string is not"
  emitc.verbatim "// NUL-terminated."
  emitc.verbatim "inline void print_string(const char * name, const std::string & s) {"
  emitc.verbatim "  static constexpr char hex[] = \"0123456789ABCDEF\";"
  emitc.verbatim "  std::fputs(name, stdout);"
  emitc.verbatim "  std::putchar('=');"
  emitc.verbatim "  std::putchar('\"');"
  emitc.verbatim "  for (const char raw : s) {"
  emitc.verbatim "    const unsigned char c = static_cast<unsigned char>(raw);"
  emitc.verbatim "    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||"
  emitc.verbatim "        (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||"
  emitc.verbatim "        c == '~') {"
  emitc.verbatim "      std::putchar(raw);"
  emitc.verbatim "    } else {"
  emitc.verbatim "      std::putchar('%');"
  emitc.verbatim "      std::putchar(hex[c >> 4]);"
  emitc.verbatim "      std::putchar(hex[c & 0x0F]);"
  emitc.verbatim "    }"
  emitc.verbatim "  }"
  emitc.verbatim "  std::putchar('\"');"
  emitc.verbatim "  std::putchar('\\n');"
  emitc.verbatim "}"
  emitc.verbatim "// ctcompile: a Boolean global - the two spellings JavaScript uses"
  emitc.verbatim "inline void print_boolean(const char * name, bool b) {"
  emitc.verbatim "  std::fputs(name, stdout);"
  emitc.verbatim "  std::fputs(b ? \"=true\\n\" : \"=false\\n\", stdout);"
  emitc.verbatim "}"
  emitc.verbatim "// ctcompile: a global whose proven type is `undefined`. It has no storage:"
  emitc.verbatim "// the value is a compile-time fact, so there is nothing to load."
  emitc.verbatim "inline void print_undefined(const char * name) {"
  emitc.verbatim "  std::fputs(name, stdout);"
  emitc.verbatim "  std::fputs(\"=undefined\\n\", stdout);"
  emitc.verbatim "}"
  emitc.verbatim "// ctcompile: a global whose proven type is `null` - same, and not the same"
  emitc.verbatim "// as undefined, which is why the gate has a negative proof for each."
  emitc.verbatim "inline void print_null(const char * name) {"
  emitc.verbatim "  std::fputs(name, stdout);"
  emitc.verbatim "  std::fputs(\"=null\\n\", stdout);"
  emitc.verbatim "}"
  emitc.verbatim "} // namespace ctnative"

  // --- the globals: one per top-level `var` THAT NEEDS STORAGE ---------------
  // `nothing` and `missing` are absent on purpose - see the header.
  emitc.global @answer : i32
  emitc.global @third : f64
  emitc.global @yes : i1
  emitc.global @no : i1
  emitc.global @looks_null : !emitc.opaque<"std::string">
  emitc.global @looks_numeric : !emitc.opaque<"std::string">
  emitc.global @greeting : !emitc.opaque<"std::string">
  emitc.global @empty : !emitc.opaque<"std::string">
  emitc.global @raw : !emitc.opaque<"std::string">

  emitc.func @main() -> i32 attributes {ctnative.provenance = "the top level of native-values-fixture.js (module written by hand)"} {
    // --- the program, in source order -------------------------------------
    // var answer = 6 * 7;  - MULTIPLIED, not memorised: a native side that
    // printed 42 from a table would still be wrong about the arithmetic.
    %six = "emitc.constant"() <{value = 6 : i32}> : () -> i32
    %seven = "emitc.constant"() <{value = 7 : i32}> : () -> i32
    %product = emitc.mul %six, %seven : (i32, i32) -> i32
    %g_answer = emitc.get_global @answer : !emitc.lvalue<i32>
    emitc.assign %product : i32 to %g_answer : !emitc.lvalue<i32>

    // var third = 1 / 3;  - `/` in JavaScript is always the float division,
    // so this is f64 even though both operands are integer literals.
    %one_f = "emitc.constant"() <{value = 1.000000e+00 : f64}> : () -> f64
    %three_f = "emitc.constant"() <{value = 3.000000e+00 : f64}> : () -> f64
    %quotient = emitc.div %one_f, %three_f : (f64, f64) -> f64
    %g_third = emitc.get_global @third : !emitc.lvalue<f64>
    emitc.assign %quotient : f64 to %g_third : !emitc.lvalue<f64>

    // var yes = true;
    %true = "emitc.constant"() <{value = 1 : i1}> : () -> i1
    %g_yes = emitc.get_global @yes : !emitc.lvalue<i1>
    emitc.assign %true : i1 to %g_yes : !emitc.lvalue<i1>

    // var no = 1 > 2;  - COMPARED, not memorised, for the same reason as above.
    %one_i = "emitc.constant"() <{value = 1 : i32}> : () -> i32
    %two_i = "emitc.constant"() <{value = 2 : i32}> : () -> i32
    %greater = emitc.cmp gt, %one_i, %two_i : (i32, i32) -> i1
    %g_no = emitc.get_global @no : !emitc.lvalue<i1>
    emitc.assign %greater : i1 to %g_no : !emitc.lvalue<i1>

    // var looks_null = "null";  var looks_numeric = "42";
    // THE TWO STRINGS THAT LOOK LIKE OTHER KINDS. Their lines are
    // `looks_null="null"` and `looks_numeric="42"`; without the quotes the
    // first would be the line a `null` global prints and the second the line
    // the number 42 prints, and the gate could not tell them apart.
    %lit_looks_null = emitc.literal "\"null\"" : !emitc.opaque<"std::string">
    %g_looks_null = emitc.get_global @looks_null : !emitc.lvalue<!emitc.opaque<"std::string">>
    emitc.assign %lit_looks_null : !emitc.opaque<"std::string"> to %g_looks_null : !emitc.lvalue<!emitc.opaque<"std::string">>

    %lit_looks_numeric = emitc.literal "\"42\"" : !emitc.opaque<"std::string">
    %g_looks_numeric = emitc.get_global @looks_numeric : !emitc.lvalue<!emitc.opaque<"std::string">>
    emitc.assign %lit_looks_numeric : !emitc.opaque<"std::string"> to %g_looks_numeric : !emitc.lvalue<!emitc.opaque<"std::string">>

    // var greeting = "hello";
    %lit_greeting = emitc.literal "\"hello\"" : !emitc.opaque<"std::string">
    %g_greeting = emitc.get_global @greeting : !emitc.lvalue<!emitc.opaque<"std::string">>
    emitc.assign %lit_greeting : !emitc.opaque<"std::string"> to %g_greeting : !emitc.lvalue<!emitc.opaque<"std::string">>

    // var empty = "";
    %lit_empty = emitc.literal "\"\"" : !emitc.opaque<"std::string">
    %g_empty = emitc.get_global @empty : !emitc.lvalue<!emitc.opaque<"std::string">>
    emitc.assign %lit_empty : !emitc.opaque<"std::string"> to %g_empty : !emitc.lvalue<!emitc.opaque<"std::string">>

    // var raw = "a=1;b\\c\"d%e ÿ\ud800";
    //
    // CONSTRUCTED WITH AN EXPLICIT LENGTH, 17, and that is not a style
    // choice: the seventeen bytes include a NUL at index 11, and
    // std::string(const char *) would stop there and lose the last five. The
    // three bytes ED A0 80 are the lone surrogate's WTF-8.
    %lit_raw = emitc.literal "std::string(\"a=1;b\\\\c\\\"d%e\\x00\\xC3\\xBF\\xED\\xA0\\x80\", 17)" : !emitc.opaque<"std::string">
    %g_raw = emitc.get_global @raw : !emitc.lvalue<!emitc.opaque<"std::string">>
    emitc.assign %lit_raw : !emitc.opaque<"std::string"> to %g_raw : !emitc.lvalue<!emitc.opaque<"std::string">>

    // var nothing = null;  var missing;   - no storage, nothing to assign.

    // --- THE OUTPUT CONVENTION: every global, sorted bytewise by name -------
    %fmt = emitc.literal "\"%s=%.17g\\n\"" : !emitc.ptr<!emitc.opaque<"const char">>

    %o_answer = emitc.load %g_answer : !emitc.lvalue<i32>
    %d_answer = emitc.cast %o_answer : i32 to f64
    %n_answer = emitc.literal "\"answer\"" : !emitc.ptr<!emitc.opaque<"const char">>
    emitc.call_opaque "std::printf"(%fmt, %n_answer, %d_answer) : (!emitc.ptr<!emitc.opaque<"const char">>, !emitc.ptr<!emitc.opaque<"const char">>, f64) -> ()

    %o_empty = emitc.load %g_empty : !emitc.lvalue<!emitc.opaque<"std::string">>
    %n_empty = emitc.literal "\"empty\"" : !emitc.ptr<!emitc.opaque<"const char">>
    emitc.call_opaque "ctnative::print_string"(%n_empty, %o_empty) : (!emitc.ptr<!emitc.opaque<"const char">>, !emitc.opaque<"std::string">) -> ()

    %o_greeting = emitc.load %g_greeting : !emitc.lvalue<!emitc.opaque<"std::string">>
    %n_greeting = emitc.literal "\"greeting\"" : !emitc.ptr<!emitc.opaque<"const char">>
    emitc.call_opaque "ctnative::print_string"(%n_greeting, %o_greeting) : (!emitc.ptr<!emitc.opaque<"const char">>, !emitc.opaque<"std::string">) -> ()

    %o_looks_null = emitc.load %g_looks_null : !emitc.lvalue<!emitc.opaque<"std::string">>
    %n_looks_null = emitc.literal "\"looks_null\"" : !emitc.ptr<!emitc.opaque<"const char">>
    emitc.call_opaque "ctnative::print_string"(%n_looks_null, %o_looks_null) : (!emitc.ptr<!emitc.opaque<"const char">>, !emitc.opaque<"std::string">) -> ()

    %o_looks_numeric = emitc.load %g_looks_numeric : !emitc.lvalue<!emitc.opaque<"std::string">>
    %n_looks_numeric = emitc.literal "\"looks_numeric\"" : !emitc.ptr<!emitc.opaque<"const char">>
    emitc.call_opaque "ctnative::print_string"(%n_looks_numeric, %o_looks_numeric) : (!emitc.ptr<!emitc.opaque<"const char">>, !emitc.opaque<"std::string">) -> ()

    // undefined: A NAME AND NOTHING ELSE. No load, because there is no storage.
    %n_missing = emitc.literal "\"missing\"" : !emitc.ptr<!emitc.opaque<"const char">>
    emitc.call_opaque "ctnative::print_undefined"(%n_missing) : (!emitc.ptr<!emitc.opaque<"const char">>) -> ()

    %o_no = emitc.load %g_no : !emitc.lvalue<i1>
    %n_no = emitc.literal "\"no\"" : !emitc.ptr<!emitc.opaque<"const char">>
    emitc.call_opaque "ctnative::print_boolean"(%n_no, %o_no) : (!emitc.ptr<!emitc.opaque<"const char">>, i1) -> ()

    %n_nothing = emitc.literal "\"nothing\"" : !emitc.ptr<!emitc.opaque<"const char">>
    emitc.call_opaque "ctnative::print_null"(%n_nothing) : (!emitc.ptr<!emitc.opaque<"const char">>) -> ()

    %o_raw = emitc.load %g_raw : !emitc.lvalue<!emitc.opaque<"std::string">>
    %n_raw = emitc.literal "\"raw\"" : !emitc.ptr<!emitc.opaque<"const char">>
    emitc.call_opaque "ctnative::print_string"(%n_raw, %o_raw) : (!emitc.ptr<!emitc.opaque<"const char">>, !emitc.opaque<"std::string">) -> ()

    %o_third = emitc.load %g_third : !emitc.lvalue<f64>
    %n_third = emitc.literal "\"third\"" : !emitc.ptr<!emitc.opaque<"const char">>
    emitc.call_opaque "std::printf"(%fmt, %n_third, %o_third) : (!emitc.ptr<!emitc.opaque<"const char">>, !emitc.ptr<!emitc.opaque<"const char">>, f64) -> ()

    %o_yes = emitc.load %g_yes : !emitc.lvalue<i1>
    %n_yes = emitc.literal "\"yes\"" : !emitc.ptr<!emitc.opaque<"const char">>
    emitc.call_opaque "ctnative::print_boolean"(%n_yes, %o_yes) : (!emitc.ptr<!emitc.opaque<"const char">>, i1) -> ()

    %rc = "emitc.constant"() <{value = 0 : i32}> : () -> i32
    emitc.return %rc : i32
  }
}
