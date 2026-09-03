// THE NATIVE GATE'S REFERENCE SIDE - ctcompile Phase 62½-D.
//
// "When a CTJS operation and the ctbrowser VM disagree, the VM is correct by
// definition." So nothing about a native binary's answers is written down
// anywhere: this runs the SAME JavaScript under the interpreter and prints
// what it left in the globals, in the exact shape the native binary's `main`
// prints its own (native-values-fixture.emitc.mlir is the specification of
// that shape; this file mirrors it), and check-native-unit.cmake compares the
// two texts.
//
// THE SHAPE - one line per global the program created or changed, ascending
// bytewise by name, `<name>=<value>`, with the value one of:
//
//     Number      printf("%.17g", d)          1   -0   2.5   inf   -inf   nan
//     Boolean     true | false
//     undefined   undefined
//     null        null
//     String      "<percent-encoded bytes>"   "text"   ""   "a%3D1"
//
// THE NUMBER FORM IS UNCHANGED and the call that prints it is literally the
// same std::printf as before this file learned the other four, because %.17g
// round-trips a double exactly and every existing fixture's expected output is
// that text. Number-to-String is a runtime concern for a later phase; this
// comparison is on the double's VALUE.
//
// THE FIVE FORMS ARE PAIRWISE DISJOINT, which is what makes the line
// unambiguous without tagging the number. Every %.17g output is a digit
// string, `inf`, `-inf`, `nan` or `-nan`; none of those is `true`, `false`,
// `undefined` or `null`, and none of them contains a `"`. So the kind of a
// value is recoverable from its text, and a Number still prints as bare digits.
// (`nan` and `null` are a one-glyph pair to a reader but they are different
// tokens, and no %.17g output spells `null`.)
//
// WHY PERCENT-ENCODING FOR STRINGS, and why not a naive print:
//
//   * A JavaScript string here is BYTES, not text. `\uD800` with no low
//     surrogate after it is encoded as a code point in its own right
//     (compile/strings.cpp encode_code_point), which is the three bytes
//     ED A0 80 - WTF-8, and NOT valid UTF-8. Any escaping that decodes to
//     code points first would have to agree with the other side about how to
//     repair those bytes. Percent-encoding is per byte, so ED A0 80 is
//     %ED%A0%80 on both sides with no decoder in the loop.
//   * It is TOTAL AND INJECTIVE: all 256 byte values are in the domain and
//     each has exactly one representation. The unreserved set of RFC 3986
//     (A-Z a-z 0-9 - . _ ~) passes through; every other byte becomes `%` and
//     exactly two UPPERCASE hex digits. Fixed width, no shorthands, no
//     alternative spellings - so decoding needs no lookahead past two
//     characters and the encoder is a function.
//   * Embedded NUL is a legal JavaScript string character, so nothing here is
//     NUL-terminated: the loop is over `.size()` and 0x00 prints as `%00`.
//   * THE OUTPUT IS PURE PRINTABLE ASCII WITH NO WHITESPACE, and that is not
//     cosmetic. This text does not travel from one program to the other
//     directly - it is captured by execute_process into a CMake variable, cut
//     on newlines with string(REPLACE) and indexed with list(GET). A `;` in a
//     value would silently split one line into two list elements and shift
//     every comparison after it; `\` is CMake's escape character; a raw
//     newline would forge an extra global; a NUL would truncate the capture.
//     `%3B`, `%5C`, `%0A` and `%00` remove all four by construction rather
//     than by hoping no fixture contains one. `=` is escaped too (`%3D`), so
//     the first `=` on a line is always the separator.
//
// WHAT IS SKIPPED IS COUNTED, on stderr, because the gate asserts it - per
// kind, so that a reference which has lost the ability to print a boolean
// fails loudly instead of agreeing with a binary that also prints nothing:
//
//   native reference: 14 globals printed (4 number, 2 boolean, 6 string,
//   1 null, 1 undefined), 1 function globals skipped, 1 other globals skipped
//
// `other` is now only the kinds the convention has NO form for - an object, an
// array, a symbol, a bigint. The gate still requires it to be zero for a real
// program: a phase that wants one of those has to extend the convention here
// first, which is the whole point of the convention being written down.
// Functions are expected: every top-level `function` is a global too.
//
// "CREATED OR CHANGED" rather than "every global", because install_builtins
// fills the table first - Math, Object, the lot - and none of that is the
// program's. A snapshot before the run says which entries were already there
// with which value; a program that reassigns a builtin's name still counts,
// since its value changed.
#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/value.hpp>
#include <ctbrowser/script/vm.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

enum class kind {
    number,
    boolean,
    string,
    null_value,
    undefined_value
};

struct printed_global {
    std::string name;
    kind k = kind::number;
    double number = 0;
    bool boolean = false;
    std::string text;
};

// THE ESCAPING, AND IT IS THE SAME FUNCTION ON BOTH SIDES. The native binary
// gets this byte-for-byte as `ctnative::print_string` (see
// native-values-fixture.emitc.mlir); if the two ever drift, the gate reports a
// string global that differs, which is exactly what it is for. Written with
// putchar and no printf so that a percent sign in the data can never be read
// as a conversion, and so that the emitted C++'s FIRST std::printf( is still
// the first line `main` prints - which is where check-native-unit.cmake's
// MUTATE path inserts its off-by-one.
void write_percent_encoded(const std::string & s) {
    static constexpr char hex[] = "0123456789ABCDEF";
    for (const char raw : s) {
        const auto c = static_cast<unsigned char>(raw);
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '.' || c == '_' || c == '~') {
            std::putchar(raw);
        } else {
            std::putchar('%');
            std::putchar(hex[c >> 4]);
            std::putchar(hex[c & 0x0F]);
        }
    }
}

} // namespace

int main(int argc, char ** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: ctcompile-test-native-reference PROGRAM.js\n");
        return 2;
    }
    std::ifstream in{argv[1], std::ios::binary};
    if (!in) {
        std::fprintf(stderr, "native reference: cannot read %s\n", argv[1]);
        return 1;
    }
    const std::string source{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};

    // THE PROGRAM OUTLIVES THE CONTEXT - vm.hpp's rule, since closures hold
    // function_protos that point into it - so it is compiled first and declared
    // first, and the context below is destroyed before it.
    const ctbrowser::script::program prog = ctbrowser::script::compiler::compile(source);
    if (!prog.ok) {
        std::fprintf(stderr, "native reference: %s does not compile: %s\n", argv[1],
                     prog.error.c_str());
        return 1;
    }

    ctbrowser::script::context cx;
    ctbrowser::script::install_builtins(cx);
    std::unordered_map<std::string, std::uint64_t> before;
    for (const auto & [name, v] : cx.globals()) { before.emplace(name, v.bits()); }

    const ctbrowser::script::run_result result = cx.run(prog);
    if (!result.ok) {
        std::fprintf(stderr, "native reference: %s threw: %s\n", argv[1], result.error.c_str());
        return 1;
    }

    std::vector<printed_global> printed;
    std::size_t numbers = 0;
    std::size_t booleans = 0;
    std::size_t strings = 0;
    std::size_t nulls = 0;
    std::size_t undefineds = 0;
    std::size_t functions = 0;
    std::size_t others = 0;
    for (const auto & [name, v] : cx.globals()) {
        const auto was = before.find(name);
        if (was != before.end() && was->second == v.bits()) { continue; }
        // is_number() FIRST: it is "does not match the boxed pattern", so it
        // is the fast path and every tag below is a boxed bit pattern.
        if (v.is_number()) {
            printed.push_back({name, kind::number, v.as_number(), false, {}});
            ++numbers;
        } else if (v.is_boolean()) {
            printed.push_back({name, kind::boolean, 0, v.as_boolean(), {}});
            ++booleans;
        } else if (v.is_string()) {
            printed.push_back(
                {name, kind::string, 0, false,
                 static_cast<const ctbrowser::script::string_object *>(v.as_heap())->text});
            ++strings;
        } else if (v.is_null()) {
            printed.push_back({name, kind::null_value, 0, false, {}});
            ++nulls;
        } else if (v.is_undefined()) {
            printed.push_back({name, kind::undefined_value, 0, false, {}});
            ++undefineds;
        } else if (v.is_kind(ctbrowser::script::heap_kind::function)) {
            ++functions;
        } else {
            std::fprintf(stderr,
                         "native reference: the global %s has type %s, which the convention has "
                         "no form for\n",
                         name.c_str(), std::string{ctbrowser::script::context::type_of(v)}.c_str());
            ++others;
        }
    }
    std::ranges::sort(printed, {}, &printed_global::name);
    for (const printed_global & g : printed) {
        switch (g.k) {
        // THE UNCHANGED CALL. Every fixture that existed before the other four
        // kinds did prints exactly these bytes, and this line is why.
        case kind::number: std::printf("%s=%.17g\n", g.name.c_str(), g.number); break;
        case kind::boolean:
            std::printf("%s=%s\n", g.name.c_str(), g.boolean ? "true" : "false");
            break;
        case kind::null_value: std::printf("%s=null\n", g.name.c_str()); break;
        case kind::undefined_value: std::printf("%s=undefined\n", g.name.c_str()); break;
        case kind::string:
            std::printf("%s=\"", g.name.c_str());
            write_percent_encoded(g.text);
            std::printf("\"\n");
            break;
        }
    }
    std::fprintf(stderr,
                 "native reference: %zu globals printed (%zu number, %zu boolean, %zu string, "
                 "%zu null, %zu undefined), %zu function globals skipped, "
                 "%zu other globals skipped\n",
                 printed.size(), numbers, booleans, strings, nulls, undefineds, functions, others);
    return 0;
}
