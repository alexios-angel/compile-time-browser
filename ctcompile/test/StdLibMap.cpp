// DOES THE C++ THE NATIVE BACKEND WOULD EMIT COMPUTE WHAT THE INTERPRETER
// COMPUTES?
//
// Phase 61 of `ctcompile-plan/24-native-cpp-backend.md`: the specification's
// §8 table, which maps a JavaScript global onto a C++ or Boost facility. Every
// row of that table is a semantic claim, and a claim that two functions with
// similar names compute the same thing is exactly the claim nobody checks.
//
// WHY THIS IS NOT ctcompile_differential. That test runs a body twice - once
// interpreted, once with its compiled entry installed - and compares. It cannot
// ask this question, because a `Math.abs(x)` compiled by the EmitC backend
// still CALLS the interpreter's Math.abs through ct_aot_call; both tiers reach
// the same builtin and agree no matter what this table says. The question here
// is one step earlier and does not need a backend at all: given the C++
// expression a native lowering WOULD emit, does it answer what the builtin
// answers? That is answerable today, and answering it now is what stops the
// lowering being written against a wrong table.
//
// THE INTERPRETER IS THE DEFINITION OF CORRECT, which is the dialect's own
// policy: "when a CTJS operation and the ctbrowser VM disagree, the VM is
// correct by definition." Nothing below writes an expected answer down. Each
// probe evaluates a JavaScript expression in a real context AND evaluates the
// proposed C++ beside it, and the two are compared.
//
// AND A DISAGREEMENT IS SOMETIMES THE ASSERTION. A row the map classifies
// `divergent` must DISAGREE, on the witness the map names. That is what makes
// the refusal list load-bearing rather than decorative: promote Math.round to
// `exact` without repairing its target and this test goes red; repair the
// target so std::round is no longer what is emitted, and the witness stops
// witnessing, and it goes red the other way. A refusal nobody can falsify is a
// note, not a test.
//
// THE INPUTS ARE CHOSEN TO SEPARATE, NOT TO COVER. -0, NaN and the two halfway
// cases do all the work here: they are where a plausible C++ spelling stops
// agreeing with JavaScript, and an input like `Math.floor(2.5)` would pass
// against std::floor, std::round, std::trunc and std::nearbyint alike.
#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/vm.hpp>

#include <boost/algorithm/string.hpp>
#include <boost/json.hpp>
#include <boost/regex.hpp>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <limits>
#include <numbers>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

using ctbrowser::script::context;
using ctbrowser::script::program;
using ctbrowser::script::value;

namespace {

// --- the map, read as data ------------------------------------------------

enum class verdict {
    exact,
    divergent,
    refused
};

struct row {
    std::string_view name;
    std::string_view js;
    std::string_view target;
    std::string_view header;
    ::verdict verdict;
    std::string_view witness;
    std::string_view why;
};

#define CT_STDLIB_ROW(name_, js_, target_, header_, verdict_, witness_, why_)                      \
    row{#name_, js_, target_, header_, verdict::verdict_, witness_, why_},
constexpr row map[] = {
#include "StdLibMap.inc"
};
#undef CT_STDLIB_ROW

static_assert(std::size(map) == CT_STDLIB_ROW_COUNT,
              "the generated row count and the generated table disagree, which means the emitter "
              "wrote one of them from something other than the .td");

// --- comparing two answers ------------------------------------------------

const double nan_ = std::numeric_limits<double>::quiet_NaN();
const double inf_ = std::numeric_limits<double>::infinity();

// A NUMBER'S BITS, NOT ITS TEXT, and that is the whole reason this test can see
// what it is looking for. `String(-0)` is "0" in JavaScript, so a text
// comparison cannot tell +0 from -0 - and -0 is the answer that separates
// Math.round from std::round, Math.max from std::max, and Math.sign from
// copysign. Every NaN is folded to one token because a NaN payload is not
// observable from JavaScript and the two sides have no reason to agree on it.
std::string number_token(double d) {
    if (std::isnan(d)) { return "NaN"; }
    char out[24] = {};
    std::snprintf(out, sizeof out, "%016llx",
                  static_cast<unsigned long long>(std::bit_cast<std::uint64_t>(d)));
    return out;
}

std::string token(context & cx, value v) {
    return v.is_number() ? number_token(v.as_number()) : cx.to_string(v);
}

// WHAT THE INTERPRETER SAYS. The expression is wrapped so the driver can tell
// "the answer was undefined" from "the arm never ran" - the same guard
// Differential.cpp carries, and for the same reason: a probe whose source
// throws would otherwise read back the PREVIOUS probe's global and report
// agreement about nothing.
std::string interpreted(context & cx, std::string_view expression) {
    const std::string source = "__RAN = 0; __R = (" + std::string(expression) + "); __RAN = 1;";
    const program compiled = ctbrowser::script::compiler::compile(source);
    if (!compiled.ok) { return "<did not compile: " + compiled.error + ">"; }
    if (!cx.run(compiled).ok) { return "<threw>"; }
    if (!cx.global("__RAN").is_number() || cx.global("__RAN").as_number() != 1.0) {
        return "<the expression did not finish>";
    }
    return token(cx, cx.global("__R"));
}

// --- the probes -----------------------------------------------------------
//
// One JavaScript expression, and the C++ the map says a native lowering would
// emit for it. A captureless lambda converts to the function pointer, so each
// probe reads as the pair it is.

struct probe {
    // The record in StdLibMap.td this probe is evidence about.
    const char * row;
    const char * js;
    std::string (*native)();
};

// Math.sign, as the map spells it. Written once here rather than three times
// below, because the point of the row is that this expression - and not
// std::copysign - is what has to be emitted.
double js_sign(double x) {
    return x > 0 ? 1.0 : (x < 0 ? -1.0 : x);
}

const probe probes[] = {
    // --- the rounding family, pinned by IEEE-754 ---------------------------
    {"Math_floor", "Math.floor(-0.5)", [] { return number_token(std::floor(-0.5)); }},
    {"Math_floor", "Math.floor(-0)", [] { return number_token(std::floor(-0.0)); }},
    {"Math_floor", "Math.floor(NaN)", [] { return number_token(std::floor(nan_)); }},
    // -0, WHICH IS THE ANSWER AND NOT A CURIOSITY: Math.ceil(-0.5) is -0, so a
    // lowering that normalised zeros would be caught here and nowhere else.
    {"Math_ceil", "Math.ceil(-0.5)", [] { return number_token(std::ceil(-0.5)); }},
    {"Math_ceil", "Math.ceil(4.2)", [] { return number_token(std::ceil(4.2)); }},
    {"Math_trunc", "Math.trunc(-0.5)", [] { return number_token(std::trunc(-0.5)); }},
    {"Math_trunc", "Math.trunc(-4.7)", [] { return number_token(std::trunc(-4.7)); }},
    {"Math_sqrt", "Math.sqrt(-0)", [] { return number_token(std::sqrt(-0.0)); }},
    {"Math_sqrt", "Math.sqrt(-1)", [] { return number_token(std::sqrt(-1.0)); }},
    {"Math_sqrt", "Math.sqrt(2)", [] { return number_token(std::sqrt(2.0)); }},

    // --- abs and sign ------------------------------------------------------
    {"Math_abs", "Math.abs(-0)", [] { return number_token(std::fabs(-0.0)); }},
    {"Math_abs", "Math.abs(-Infinity)", [] { return number_token(std::fabs(-inf_)); }},
    {"Math_abs", "Math.abs(NaN)", [] { return number_token(std::fabs(nan_)); }},
    // THE THREE INPUTS THAT SEPARATE THE ROW FROM copysign(1, x): -0 must come
    // back as -0, NaN as NaN, and only a nonzero input picks up a magnitude.
    {"Math_sign", "Math.sign(-0)", [] { return number_token(js_sign(-0.0)); }},
    {"Math_sign", "Math.sign(NaN)", [] { return number_token(js_sign(nan_)); }},
    {"Math_sign", "Math.sign(-3.5)", [] { return number_token(js_sign(-3.5)); }},

    // --- the transcendentals, exact against the ORACLE ---------------------
    {"Math_sin", "Math.sin(1)", [] { return number_token(std::sin(1.0)); }},
    {"Math_cos", "Math.cos(1)", [] { return number_token(std::cos(1.0)); }},
    {"Math_tan", "Math.tan(1)", [] { return number_token(std::tan(1.0)); }},
    {"Math_exp", "Math.exp(1)", [] { return number_token(std::exp(1.0)); }},
    {"Math_log", "Math.log(10)", [] { return number_token(std::log(10.0)); }},
    {"Math_atan2", "Math.atan2(1, 3)", [] { return number_token(std::atan2(1.0, 3.0)); }},

    // --- the constants -----------------------------------------------------
    {"Math_PI", "Math.PI", [] { return number_token(std::numbers::pi); }},
    {"Math_E", "Math.E", [] { return number_token(std::numbers::e); }},
    {"Math_SQRT2", "Math.SQRT2", [] { return number_token(std::numbers::sqrt2); }},
    {"Math_LN2", "Math.LN2", [] { return number_token(std::numbers::ln2); }},
    {"Math_LN10", "Math.LN10", [] { return number_token(std::numbers::ln10); }},
    {"Math_LOG2E", "Math.LOG2E", [] { return number_token(std::numbers::log2e); }},
    {"Math_LOG10E", "Math.LOG10E", [] { return number_token(std::numbers::log10e); }},

    // --- trim, against the engine's own helper -----------------------------
    {"String_trim", R"JS(" \t\n\r\f\vx y \t\n\r\f\v".trim())JS",
     [] {
         return std::string(ctbrowser::trim(" \t\n\r\f\vx y \t\n\r\f\v", ctbrowser::js_whitespace));
     }},
    // NON-ASCII WHITESPACE, WHICH NEITHER SIDE TRIMS. ECMAScript trims U+00A0;
    // this engine does not, on purpose, and the mapping inherits that. The probe
    // pins the inheritance, so a future "fix" to one side alone is caught.
    {"String_trim", R"JS(" x ".trim())JS",
     [] { return std::string(ctbrowser::trim("\xc2\xa0x\xc2\xa0", ctbrowser::js_whitespace)); }},

    // --- and now the rows that must DISAGREE -------------------------------
    //
    // THE HEADLINE ROW. std::round rounds a tie away from zero; JavaScript
    // rounds a tie toward +Infinity. -0.5 and -2.5 are where that shows.
    {"Math_round", "Math.round(-0.5)", [] { return number_token(std::round(-0.5)); }},
    {"Math_round", "Math.round(-2.5)", [] { return number_token(std::round(-2.5)); }},
    // NaN VANISHES THROUGH `<`, which is what std::min and std::max are written
    // in terms of, so the NaN never reaches the answer.
    {"Math_min", "Math.min(1, NaN)", [] { return number_token(std::min(1.0, nan_)); }},
    // AND THE ZERO, WHICH IS THE HALF A NaN TEST WOULD MISS: `-0 < 0` is false,
    // so std::max returns its first argument.
    {"Math_max", "Math.max(-0, 0)", [] { return number_token(std::max(-0.0, 0.0)); }},
    {"Math_pow", "Math.pow(1, NaN)", [] { return number_token(std::pow(1.0, nan_)); }},
    // ONE ULP, WHICH IS EXACTLY THE SIZE OF ERROR A PRINTED COMPARISON HIDES.
    {"Math_SQRT1_2", "Math.SQRT1_2", [] { return number_token(1.0 / std::numbers::sqrt2); }},
    // A DIFFERENT STREAM IS A DIFFERENT PICTURE. The engine's is a per-context
    // xorshift64*; this is the generator a transpiler would reach for.
    {"Math_random", "Math.random()",
     [] {
         std::mt19937_64 generator(1uLL);
         return number_token(static_cast<double>(generator() >> 11) / 9007199254740992.0);
     }},
    // THE FROZEN CLOCK AGAINST THE WALL CLOCK.
    {"Date_now", "Date.now()",
     [] {
         return number_token(
             static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count()));
     }},
    // SameValueZero AGAINST operator==. A JS Set holds one NaN and one zero;
    // an unordered_set holds two NaNs (because NaN != NaN) and one zero.
    {"Set_ctor",
     "(function () { var s = new Set(); s.add(NaN); s.add(NaN); s.add(0); s.add(-0); "
     "return s.size; })()",
     [] {
         std::unordered_set<double> set;
         set.insert(nan_);
         set.insert(nan_);
         set.insert(0.0);
         set.insert(-0.0);
         return number_token(static_cast<double>(set.size()));
     }},
    // AND INSERTION ORDER, which is a second and independent refutation: even a
    // set whose key equality were repaired would still iterate in the wrong
    // order.
    {"Set_ctor",
     "(function () { var s = new Set(); s.add(3); s.add(1); s.add(2); var o = ''; "
     "s.forEach(function (v) { o += v; }); return o; })()",
     [] {
         std::unordered_set<double> set;
         set.insert(3.0);
         set.insert(1.0);
         set.insert(2.0);
         std::string order;
         for (const double each : set) { order += std::to_string(static_cast<int>(each)); }
         return order;
     }},
    {"Map_ctor",
     "(function () { var m = new Map(); m.set(NaN, 1); m.set(NaN, 2); m.set(0, 3); "
     "m.set(-0, 4); return m.size; })()",
     [] {
         std::unordered_map<double, double> table;
         table.emplace(nan_, 1.0);
         table.emplace(nan_, 2.0);
         table.emplace(0.0, 3.0);
         table.emplace(-0.0, 4.0);
         return number_token(static_cast<double>(table.size()));
     }},
    // A REGEX THAT DISAGREES IN BOTH DIRECTIONS. `\1` is a backreference to
    // Boost and the literal character '1' to this engine, so each matches a
    // string the other rejects - and nothing anywhere reports an error.
    {"RegExp_ctor", R"JS(new RegExp("(.)\\1").test("aa"))JS",
     [] {
         return std::string(
             boost::regex_search(std::string("aa"), boost::regex("(.)\\1")) ? "true" : "false");
     }},
    {"RegExp_ctor", R"JS(new RegExp("(.)\\1").test("a1"))JS",
     [] {
         return std::string(
             boost::regex_search(std::string("a1"), boost::regex("(.)\\1")) ? "true" : "false");
     }},
    // LOOKBEHIND: refused by this engine's parser, so the program is not ok and
    // `test` is false for every subject. Boost implements it.
    {"RegExp_ctor", R"JS(new RegExp("(?<=a)b").test("ab"))JS",
     [] {
         return std::string(
             boost::regex_search(std::string("ab"), boost::regex("(?<=a)b")) ? "true" : "false");
     }},
    // A FAILED PARSE IS A VALUE HERE AND AN EXCEPTION THERE.
    {"JSON_parse", R"JS(JSON.parse("{oops"))JS",
     [] {
         try {
             return boost::json::serialize(boost::json::parse("{oops"));
         } catch (const std::exception &) { return std::string("<threw>"); }
     }},
    // A DIFFERENT NUMBER FORMATTER, on the most ordinary number there is.
    {"JSON_stringify", "JSON.stringify(0.1)",
     [] { return boost::json::serialize(boost::json::value(0.1)); }},
    {"JSON_stringify", "JSON.stringify(1e21)",
     [] { return boost::json::serialize(boost::json::value(1e21)); }},
};

const row * find_row(std::string_view name) {
    for (const row & each : map) {
        if (each.name == name) { return &each; }
    }
    return nullptr;
}

int failures = 0;

} // namespace

int main() {
    context cx;
    ctbrowser::script::install_builtins(cx);

    // ---- every probe names a row, and every row that can be probed is -----
    //
    // THE COVERAGE NUMBER IS DERIVED FROM THE TABLE, not written down. A row
    // added to StdLibMap.td with no probe beside it fails here, which is what
    // stops the map growing claims nothing checks - and a `refused` row that
    // acquires a probe fails too, because a refusal with a witness is a
    // `divergent` row that was filed under the wrong verdict.
    unsigned probed[std::size(map)] = {};
    for (const probe & each : probes) {
        const row * which = find_row(each.row);
        if (which == nullptr) {
            std::printf("FAILED - the probe for %s names no row in StdLibMap.td\n", each.row);
            ++failures;
            continue;
        }
        ++probed[static_cast<std::size_t>(which - map)];
    }

    unsigned rows_with_probes = 0;
    for (std::size_t i = 0; i < std::size(map); ++i) {
        const bool wanted = map[i].verdict != verdict::refused;
        if (wanted && probed[i] == 0) {
            std::printf("FAILED - %.*s is %s and no probe is evidence for it\n",
                        static_cast<int>(map[i].name.size()), map[i].name.data(),
                        map[i].verdict == verdict::exact ? "exact" : "divergent");
            ++failures;
        }
        if (!wanted && probed[i] != 0) {
            std::printf("FAILED - %.*s is refused, which means no witness exists, and %u probes "
                        "claim otherwise\n",
                        static_cast<int>(map[i].name.size()), map[i].name.data(), probed[i]);
            ++failures;
        }
        if (probed[i] != 0) { ++rows_with_probes; }
    }
    if (rows_with_probes !=
        static_cast<unsigned>(CT_STDLIB_EXACT_COUNT + CT_STDLIB_DIVERGENT_COUNT)) {
        std::printf("FAILED - %u rows carry a probe where the map has %d that must\n",
                    rows_with_probes, CT_STDLIB_EXACT_COUNT + CT_STDLIB_DIVERGENT_COUNT);
        ++failures;
    }

    // ---- and now the answers ---------------------------------------------
    unsigned agreed = 0;
    unsigned diverged = 0;
    for (const probe & each : probes) {
        const row * which = find_row(each.row);
        if (which == nullptr) { continue; }
        const std::string vm = interpreted(cx, each.js);
        const std::string cpp = each.native();
        const bool same = vm == cpp;
        if (same) {
            ++agreed;
        } else {
            ++diverged;
        }

        if (which->verdict == verdict::exact && !same) {
            std::printf("%-16s FAILED - the map calls this row EXACT and it is not\n"
                        "    javascript  %s\n    interpreter %s\n    %.*s   %s\n",
                        each.row, each.js, vm.c_str(), static_cast<int>(which->target.size()),
                        which->target.data(), cpp.c_str());
            ++failures;
        } else if (which->verdict == verdict::divergent && same) {
            // A WITNESS THAT STOPPED WITNESSING IS A FAILURE, not a quiet
            // success. Either the target was repaired - in which case the row's
            // verdict is stale and this file is the place that says so - or the
            // probe drifted onto an input that no longer separates them, which
            // is the vacuous test this project keeps finding.
            std::printf("%-16s FAILED - the map calls this row DIVERGENT and both answered %s.\n"
                        "    Either %.*s was repaired and the verdict is stale, or the witness "
                        "stopped separating them.\n",
                        each.row, vm.c_str(), static_cast<int>(which->target.size()),
                        which->target.data());
            ++failures;
        } else {
            std::printf("%-16s %-9s %-46s %s\n", each.row,
                        which->verdict == verdict::exact ? "agree" : "DIVERGE", each.js,
                        same ? vm.c_str() : (vm + "  vs  " + cpp).c_str());
        }
    }

    std::printf("\n%d rows: %d exact, %d divergent, %d refused. %zu probes, %u agreed, %u "
                "diverged.\n",
                CT_STDLIB_ROW_COUNT, CT_STDLIB_EXACT_COUNT, CT_STDLIB_DIVERGENT_COUNT,
                CT_STDLIB_REFUSED_COUNT, std::size(probes), agreed, diverged);

    // THE REFUSALS, PRINTED. They are the roadmap - part 24 §63 step 2: "every
    // function the native backend refuses records WHY... That list is the
    // roadmap." A refusal nobody ever reads is a refusal that gets rediscovered.
    std::printf("\nrefused, with no witness available:\n");
    for (const row & each : map) {
        if (each.verdict != verdict::refused) { continue; }
        std::printf("  %.*s -> %.*s\n", static_cast<int>(each.js.size()), each.js.data(),
                    static_cast<int>(each.target.size()), each.target.data());
    }

    if (failures == 0) {
        std::printf("\nthe map agrees with the interpreter everywhere it says it does\n");
    }
    return failures == 0 ? 0 : 1;
}
