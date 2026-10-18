//===- ctcompile/test/Runtime/Reference/Interpreter.cpp - VM reference ----===//
//
// Reference side of the native-output check: runs a JavaScript program in the
// ctbrowser VM and prints the globals it created or changed, in the same text
// format a compiled native binary prints, so the two outputs can be diffed.
//
// "When a CTJS operation and the ctbrowser VM disagree, the VM is correct by
// definition." So nothing about a native binary's answers is written down
// anywhere: this runs the same JavaScript under the interpreter and prints
// what it left in the globals, in the exact shape the native binary's `main`
// prints its own (the ctnative::print_* helpers in
// ctcompile/include/ctcompile/CTNative/Runtime/ctnative.hpp define that shape;
// this file mirrors them), and ctcompile/test/CTNative/Checks/compilation-unit.py
// compares the two texts.
//
// The shape: one line per global the program created or changed, ascending
// bytewise by name, `<name>=<value>`, with the value one of:
//
//     Number      printf("%.17g", d)          1   -0   2.5   inf   -inf   nan
//     Boolean     true | false
//     undefined   undefined
//     null        null
//     String      "<percent-encoded bytes>"   "text"   ""   "a%3D1"
//
// Numbers print with %.17g, which round-trips a double exactly, so the
// comparison is on the double's value rather than on JavaScript's
// Number-to-String text.
//
// The five forms are pairwise disjoint, which is what makes the line
// unambiguous without tagging the number. Every %.17g output is a digit
// string, `inf`, `-inf`, `nan` or `-nan`; none of those is `true`, `false`,
// `undefined` or `null`, and none of them contains a `"`. So the kind of a
// value is recoverable from its text, and a Number still prints as bare digits.
// (`nan` and `null` are a one-glyph pair to a reader but they are different
// tokens, and no %.17g output spells `null`.)
//
// Why percent-encoding for strings, and not a naive print:
//
//   * A JavaScript string here is bytes, not text. `\uD800` with no low
//     surrogate after it is encoded as a code point in its own right
//     (core/algorithms.hpp append_utf8), which is the three bytes
//     ED A0 80 - WTF-8, and not valid UTF-8. Any escaping that decodes to
//     code points first would have to agree with the other side about how to
//     repair those bytes. Percent-encoding is per byte, so ED A0 80 is
//     %ED%A0%80 on both sides with no decoder in the loop.
//   * It is total and injective: all 256 byte values are in the domain and
//     each has exactly one representation. The unreserved set of RFC 3986
//     (A-Z a-z 0-9 - . _ ~) passes through; every other byte becomes `%` and
//     exactly two uppercase hex digits. Fixed width, no shorthands, no
//     alternative spellings - so decoding needs no lookahead past two
//     characters and the encoder is a function.
//   * Embedded NUL is a legal JavaScript string character, so nothing here is
//     NUL-terminated: the loop is over `.size()` and 0x00 prints as `%00`.
//   * The output is printable ASCII with no whitespace, so the comparing
//     script can split it on newlines and on the first `=`: a raw newline in
//     a value would forge an extra global, and `=` is escaped (`%3D`) so the
//     first `=` on a line is always the separator.
//
// Skipped globals are counted per kind on stderr, and compilation-unit.py
// asserts the counts, so a reference that loses the ability to print a
// boolean fails loudly instead of agreeing with a binary that also prints
// nothing:
//
//   native reference: 14 globals printed (4 number, 2 boolean, 6 string,
//   1 null, 1 undefined), 1 function globals skipped, 1 other globals skipped
//
// `other` counts the kinds this format has no form for: an object, an array,
// a symbol, a bigint. The check requires it to be zero for a real program; a
// test that needs one of those kinds must extend the format here (and in
// ctnative.hpp) first. Functions are expected: every top-level `function` is
// a global too.
//
// "Created or changed" rather than "every global", because install_builtins
// fills the table first - Math, Object, the lot - and none of that is the
// program's. A snapshot before the run says which entries were already there
// with which value; a program that reassigns a builtin's name still counts,
// since its value changed.
//
//===----------------------------------------------------------------------===//
#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/value.hpp>
#include <ctbrowser/script/vm.hpp>
#include <ctbrowser/shell/bindings.hpp>
#include <ctbrowser/shell/browser.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "options.hpp"

namespace {

/// The kind of a printed global; each has its own output form (see the file
/// header).
enum class kind {
    number,
    boolean,
    string,
    null_value,
    undefined_value
};

/// One global to print, with the field its `k` selects filled in.
struct printed_global {
    std::string name;
    kind k = kind::number;
    double number = 0;
    bool boolean = false;
    std::string text;
};

/// Percent-encodes \p s exactly as ctnative::print_string (ctnative.hpp) does
/// on the native side; if the two drift, compilation-unit.py reports a string
/// global that differs. Written with putchar, not printf, so a `%` in the data
/// is never a conversion and so the emitted C++'s first printf( in `main` stays
/// the line compilation-unit.py --mutate anchors its off-by-one edit on.
void write_percent_encoded(const std::string & s) {
    static constexpr char hex[] = "0123456789ABCDEF";
    for (const char raw : s) {
        const auto c = static_cast<unsigned char>(raw);
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            std::string_view("-._~").contains(raw)) {
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
    namespace po = boost::program_options;
    std::string path;
    std::vector<std::string> scripts;
    po::options_description options{"usage: ctcompile-test-native-reference "
                                    "[--page PAGE.html [--script SCRIPT.js]... | PROGRAM.js]"};
    auto add = options.add_options();
    // Either one fills `path`. Which one did is the mode, and it must be exactly one.
    add("page", po::value(&path), "load PAGE.html into a live document, print its console.log");
    add("script", po::value(&scripts),
        "with --page: then run SCRIPT.js in the page's context, in order");
    add("program", po::value(&path), "run PROGRAM.js, print the globals it created or changed");
    po::positional_options_description positional;
    positional.add("program", 1);
    const auto args = ctbrowser_cli::parse_options(argc, argv, options, positional);
    if (!args) { return 2; }
    const bool page = args->count("page") != 0;
    if (page == (args->count("program") != 0)) {
        std::cerr << "give exactly one of --page PAGE.html and PROGRAM.js\n" << options;
        return 2;
    }
    if (!page && !scripts.empty()) {
        std::cerr << "--script needs --page\n" << options;
        return 2;
    }
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        std::fprintf(stderr, "native reference: cannot read %s\n", path.c_str());
        return 1;
    }
    const std::string source{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};

    // --page: the same VM behind a live document, for questions about the DOM
    // bindings themselves (element_names.py enumerates the element surface this
    // way). Each --script then runs in the page's own context, as a classic
    // script would (check-workload.py's script-run loads a library and a probe
    // that calls it on the page's elements). Prints each console.log line; a
    // script error fails, after the lines: each page script is its own program,
    // so the scripts after one that threw still ran, and their lines show it.
    if (page) {
        ctbrowser::shell::browser live{ctbrowser::shell::browser_options{400, 300}};
        const auto print_lines = [&live] {
            for (const std::string & line : live.bindings().console_output()) {
                std::printf("%s\n", line.c_str());
            }
        };
        live.load_html(source);
        if (!live.script_error().empty()) {
            print_lines();
            std::fprintf(stderr, "native reference: %s threw: %s\n", path.c_str(),
                         live.script_error().c_str());
            return 1;
        }
        for (const std::string & script : scripts) {
            std::ifstream file{script, std::ios::binary};
            if (!file) {
                std::fprintf(stderr, "native reference: cannot read %s\n", script.c_str());
                return 1;
            }
            const std::string text{std::istreambuf_iterator<char>{file},
                                   std::istreambuf_iterator<char>{}};
            if (!live.run_script(text) || !live.script_error().empty()) {
                print_lines();
                std::fprintf(stderr, "native reference: %s threw in %s: %s\n", script.c_str(),
                             path.c_str(), live.script_error().c_str());
                return 1;
            }
        }
        print_lines();
        return 0;
    }

    // The program outlives the context - vm.hpp's rule, since closures hold
    // function_protos that point into it - so it is compiled first and declared
    // first, and the context below is destroyed before it.
    const ctbrowser::script::program prog = ctbrowser::script::compiler::compile(source);
    if (!prog.ok) {
        std::fprintf(stderr, "native reference: %s does not compile: %s\n", path.c_str(),
                     prog.error.c_str());
        return 1;
    }

    ctbrowser::script::context cx;
    ctbrowser::script::install_builtins(cx);
    std::unordered_map<std::string, std::uint64_t> before;
    for (const auto & [name, v] : cx.globals()) { before.emplace(name, v.bits()); }

    const ctbrowser::script::run_result result = cx.run(prog);
    if (!result.ok) {
        std::fprintf(stderr, "native reference: %s threw: %s\n", path.c_str(),
                     result.error.c_str());
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
        // is_number() first: it is "does not match the boxed pattern", so it
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
        // Keep this exact format: existing expected outputs depend on it.
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
