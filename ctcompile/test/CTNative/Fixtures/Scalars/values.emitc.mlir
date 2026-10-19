// The reference for how native code prints globals that are not numbers.
//
// Hand-written: this is the C++ that LowerToEmitC.cpp is expected to emit for
// values.js, kept as a fixed reference so the compile-and-compare harness
// (Checks/compilation-unit.py) has a known-good module to run. The harness
// compiles it standalone, runs it, and compares every printed line with the
// interpreter's output for the same JavaScript. Because the module is fixed,
// a failure here points at the harness or the interpreter's printing, not at
// the lowering. If the print format changes, this module and the interpreter
// side must change with it.
//
// The output convention, shared with the reference interpreter
// (test/Runtime/Reference/Interpreter.cpp) and produced here:
//
//     Number      ctnative::print_number("<name>", (double) <global>)
//     Boolean     ctnative::print_boolean("<name>", <global>)
//     undefined   ctnative::print_undefined("<name>")
//     null        ctnative::print_null("<name>")
//     String      ctnative::print_string("<name>", <global>)
//
// one line per global, sorted bytewise by name, and `main` returns 0. The
// Number line goes through ctnative::print_number so the native and the
// interpreter agree on Number::toString (-0 kept on both sides).
//
// Five kinds need only three representations:
//
//   * a Number is `double` (or `int32_t`, widened to double at the print)
//   * a Boolean is `bool`
//   * a String is `std::string`
//   * `undefined` and `null` get no storage at all. A global whose proved type
//     is one of those has exactly one possible value, a compile-time fact, so
//     there is nothing to keep at runtime and nothing to load; the print is a
//     constant. Giving them a tagged slot would box a value whose type is
//     already proved, which the native target never does.
//
// The emitted C++ has no runtime type tag anywhere: the kind of every global
// is fixed at compile time by the type inference gave it, and decided again,
// independently, by the interpreter at run time. The test passes only when
// the two agree.
//
// Nothing from the engine is used: no include, no helper reaching into it, no
// `ct_aot_*`. The harness compiles this standalone and runs `nm` on the
// result.

// As a lit test this file checks only the shape of the emitted C++, never a
// value: the values are judged against the interpreter by compilation-unit.py.
//
// RUN: ctjs-translate --mlir-to-cpp %s | FileCheck %s --implicit-check-not=ctbrowser
//
// CHECK: #include <cstdint>
// CHECK: #include <cstdio>
// CHECK: #include <cmath>
// CHECK: #include <format>
// CHECK: #include <string>
// CHECK: namespace ctnative {
// CHECK: inline void print_string(const char * name, const std::string & s) {
// CHECK: inline void print_boolean(const char * name, bool b) {
// CHECK: inline void print_undefined(const char * name) {
// CHECK: inline void print_null(const char * name) {
// CHECK: inline void print_number(const char * name, double value) {
// CHECK: } // namespace ctnative
//
// Exactly three representations, no fourth.
// CHECK: int32_t answer;
// CHECK: double third;
// CHECK: bool yes;
// CHECK: bool no;
// CHECK: std::string greeting;
//
// No storage for the other two kinds: `nothing` and `missing` are globals of
// the program, but neither may appear as a declaration.
// CHECK-NOT: nothing;
// CHECK-NOT: missing;
//
// CHECK-LABEL: int32_t main() {
// The prints, sorted bytewise by name. The interpreter side sorts the same
// way, and a line out of place fails the comparison.
// CHECK: ctnative::print_number("answer", {{v[0-9]+}});
// CHECK: ctnative::print_string("empty", {{v[0-9]+}});
// CHECK: ctnative::print_string("greeting", {{v[0-9]+}});
// CHECK: ctnative::print_string("looks_null", {{v[0-9]+}});
// CHECK: ctnative::print_string("looks_numeric", {{v[0-9]+}});
// CHECK: ctnative::print_undefined("missing");
// CHECK: ctnative::print_boolean("no", {{v[0-9]+}});
// CHECK: ctnative::print_null("nothing");
// CHECK: ctnative::print_string("raw", {{v[0-9]+}});
// CHECK: ctnative::print_number("third", {{v[0-9]+}});
// CHECK: ctnative::print_boolean("yes", {{v[0-9]+}});
// CHECK: return {{v[0-9]+}};
//
// The RUN lines: one comparison over all five kinds, then one mutation per
// kind that must fail naming its global (compilation-unit.py --mutate-as),
// because a Number mutation like `g = g + 1` does not compile on a bool, means
// nothing on a std::string, and has nothing to act on for a storage-less
// undefined or null global. The Number mutation targets `third`, not
// `answer`: the edit is inserted before the first print, which has already
// read the first global in sorted order, so mutating `answer` would change
// nothing. The driver refuses that case by name, and the VACUOUS line checks
// that it does. Then a symbol check: a binary built from this module plus one
// object that reaches the interpreter must be rejected. Last, a warning-free
// compile of the value-printing helpers on both toolchains
// (compile-clean.py).
//
// print_number is a local helper over std::format (see the verbatim block), so
// the module keeps its header-only link; it does not need --core-build.
//
//
// RUN: %compilation_unit --module %s --js %S/values.js --work %t --name values
// RUN: not %compilation_unit --module %s --js %S/values.js --work %t --name values_number --mutate third --mutate-as number 2>&1 | FileCheck %s --check-prefix=NUMBER
// NUMBER: global 'third' differs
// RUN: not %compilation_unit --module %s --js %S/values.js --work %t --name values_boolean --mutate no --mutate-as boolean 2>&1 | FileCheck %s --check-prefix=BOOLEAN
// BOOLEAN: global 'no' differs
// RUN: not %compilation_unit --module %s --js %S/values.js --work %t --name values_string --mutate greeting --mutate-as string 2>&1 | FileCheck %s --check-prefix=STRING
// STRING: global 'greeting' differs
// RUN: not %compilation_unit --module %s --js %S/values.js --work %t --name values_undefined --mutate missing --mutate-as undefined 2>&1 | FileCheck %s --check-prefix=UNDEFINED
// UNDEFINED: global 'missing' differs
// RUN: not %compilation_unit --module %s --js %S/values.js --work %t --name values_null --mutate nothing --mutate-as null 2>&1 | FileCheck %s --check-prefix=NULL
// NULL: global 'nothing' differs
// RUN: not %compilation_unit --module %s --js %S/values.js --work %t --name values_vacuous_mutation --mutate answer --mutate-as number 2>&1 | FileCheck %s --check-prefix=VACUOUS
// VACUOUS: VACUOUS
// RUN: not %compilation_unit --module %s --js %S/values.js --work %t --name values_vm_linked --prebuilt ctcompile-test-native-values-vm-linked 2>&1 | FileCheck %s --check-prefix=VM-LINKED
// VM-LINKED: reaches the interpreter
// RUN: %compile_clean --module %s --work %t --name values

module {
  emitc.include <"cstdint">
  emitc.include <"cstdio">
  emitc.include <"cmath">
  emitc.include <"format">
  emitc.include <"string">

  // The value-printing prelude, one `emitc.verbatim` per C++ line, because an
  // MLIR string attribute cannot hold an unescaped newline and a single
  // 1,500-character line is not reviewable. Only the text has to match what
  // the lowering emits, not how it is assembled.
  //
  // No std::printf in here. A `%` in the data would be read as a conversion
  // by printf, and compilation-unit.py's --mutate anchors its edit on the
  // first print_* call in `main` (the helpers it recognises are the ctnative
  // print_* set and, for older fixtures, printf), so the helper has to be
  // the module's own line, not a line of a helper defined above it. The
  // driver asserts that placement; this module keeps its side of it.
  // (The verbatim comment lines below name two files that have since been
  // replaced by Checks/compilation-unit.py and Runtime/Reference/Interpreter.cpp.)
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
  emitc.verbatim "// ctcompile: a Number global, spelled Number::toString (-0 kept) so the"
  emitc.verbatim "// interpreter side, which prints the same way, agrees on the double's"
  emitc.verbatim "// value. std::format's shortest round-trip agrees with Number::toString"
  emitc.verbatim "// on this fixture's values (42 and 1/3) only; do not copy this helper."
  emitc.verbatim "// Other values need the runtime's general ctnative::print_number."
  emitc.verbatim "inline void print_number(const char * name, double value) {"
  emitc.verbatim "  const std::string text = std::isnan(value) ? std::string{\"NaN\"}"
  emitc.verbatim "                           : std::isinf(value) ? std::string{value > 0 ? \"Infinity\" : \"-Infinity\"}"
  emitc.verbatim "                           : (value == 0.0 && std::signbit(value)) ? std::string{\"-0\"}"
  emitc.verbatim "                                               : std::format(\"{}\", value);"
  emitc.verbatim "  std::fputs(name, stdout);"
  emitc.verbatim "  std::fputc('=', stdout);"
  emitc.verbatim "  std::fputs(text.c_str(), stdout);"
  emitc.verbatim "  std::fputc('\\n', stdout);"
  emitc.verbatim "}"
  emitc.verbatim "} // namespace ctnative"

  // --- The globals: one per top-level `var` that needs storage ---------------
  // `nothing` and `missing` are absent on purpose; see the header.
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
    // --- The program, in source order -------------------------------------
    // var answer = 6 * 7;  computed, not copied: a native side that printed
    // 42 from a table would still be wrong about the arithmetic.
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

    // var no = 1 > 2;  computed, not copied, for the same reason as above.
    %one_i = "emitc.constant"() <{value = 1 : i32}> : () -> i32
    %two_i = "emitc.constant"() <{value = 2 : i32}> : () -> i32
    %greater = emitc.cmp gt, %one_i, %two_i : (i32, i32) -> i1
    %g_no = emitc.get_global @no : !emitc.lvalue<i1>
    emitc.assign %greater : i1 to %g_no : !emitc.lvalue<i1>

    // var looks_null = "null";  var looks_numeric = "42";
    // The two strings that look like other kinds. Their lines are
    // `looks_null="null"` and `looks_numeric="42"`; without the quotes the
    // first would be the line a `null` global prints and the second the line
    // the number 42 prints, and the comparison could not tell them apart.
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
    // Constructed with an explicit length, 17, because the seventeen bytes
    // include a NUL at index 11 and std::string(const char *) would stop
    // there and lose the last five. The three bytes ED A0 80 are the lone
    // surrogate's WTF-8.
    %lit_raw = emitc.literal "std::string(\"a=1;b\\\\c\\\"d%e\\x00\\xC3\\xBF\\xED\\xA0\\x80\", 17)" : !emitc.opaque<"std::string">
    %g_raw = emitc.get_global @raw : !emitc.lvalue<!emitc.opaque<"std::string">>
    emitc.assign %lit_raw : !emitc.opaque<"std::string"> to %g_raw : !emitc.lvalue<!emitc.opaque<"std::string">>

    // var nothing = null;  var missing;   No storage, nothing to assign.

    // --- The output convention: every global, sorted bytewise by name -------
    %o_answer = emitc.load %g_answer : !emitc.lvalue<i32>
    %d_answer = emitc.cast %o_answer : i32 to f64
    %n_answer = emitc.literal "\"answer\"" : !emitc.ptr<!emitc.opaque<"const char">>
    emitc.call_opaque "ctnative::print_number"(%n_answer, %d_answer) : (!emitc.ptr<!emitc.opaque<"const char">>, f64) -> ()

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

    // undefined: a name and nothing else. No load, because there is no storage.
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
    emitc.call_opaque "ctnative::print_number"(%n_third, %o_third) : (!emitc.ptr<!emitc.opaque<"const char">>, f64) -> ()

    %o_yes = emitc.load %g_yes : !emitc.lvalue<i1>
    %n_yes = emitc.literal "\"yes\"" : !emitc.ptr<!emitc.opaque<"const char">>
    emitc.call_opaque "ctnative::print_boolean"(%n_yes, %o_yes) : (!emitc.ptr<!emitc.opaque<"const char">>, i1) -> ()

    %rc = "emitc.constant"() <{value = 0 : i32}> : () -> i32
    emitc.return %rc : i32
  }
}
