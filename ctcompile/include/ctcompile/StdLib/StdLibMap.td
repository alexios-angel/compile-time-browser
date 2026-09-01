//===- StdLibMap.td - the JavaScript-global to C++/Boost map --------------===//
//
// Phase 61 of the NATIVE backend (`ctcompile-plan/24-native-cpp-backend.md`).
// The specification's §8 table, one record per row, with a VERDICT on each.
//
// THE POINT OF THIS FILE IS THE VERDICTS, NOT THE TARGETS. Every row of the
// specification's table is a semantic claim, and of its eight rows exactly ONE
// is correct. A mapping that quietly computes something else is the worst failure
// available to a transpiler - it compiles, it links, it is fast, and it is
// wrong - so nothing here is believed on the strength of the two names looking
// alike. Each row is classified, and the classification is CHECKED:
//
//   exact      the C++ target and the interpreter agree, on inputs chosen to
//              separate them. `ctcompile_stdlib_map` asserts they AGREE.
//   divergent  the C++ target and the interpreter DISAGREE, and Witness is the
//              expression that shows it. The test asserts they disagree, so a
//              row promoted to `exact` without repairing the target goes red,
//              and so does a witness that stops witnessing.
//   refused    no lowering, and no witness is possible - the divergence is
//              structural, conditional on the host, or conditional on process
//              state. These are the rows a test CANNOT pin, so the reason is
//              all there is and it has to be written properly.
//
// WHY TABLEGEN AND NOT A .def. This tree already has four X-macro tables
// (aot_helpers.def, bytecode_opcodes.def, CallPaths.def, GCRoots.def) and they
// are the right shape for a table only C++ reads. This one has a second reader
// coming: Phase 53's `ctnative` dialect turns the `exact` rows into declarative
// rewrite rules, and a DRR `Pat<>` is TableGen or it is a hundred hand-written
// matchAndRewrites - which is precisely what §1 of part 23 exists to prevent.
// Putting the data in TableGen NOW means that phase adds `Pat<>`s beside these
// records rather than transcribing them into a second table that can disagree
// with this one.
//
// It deliberately includes NOTHING - not OpBase.td, not a dialect. These are
// plain records, so `llvm-tblgen --dump-json` reads them with no MLIR in the
// build at all, which is what lets the conformance test run in a
// CTCOMPILE_ENABLE_MLIR=OFF configuration. The rewrite file that consumes them
// will include this one AND OpBase.td; that is the layer where MLIR belongs.
//
//===----------------------------------------------------------------------===//

// One row of the map.
//
//   js        what the JavaScript source says.
//   target    the C++ expression a native lowering would emit. For a `divergent`
//             or `refused` row this is the target the SPECIFICATION named, kept
//             so the refusal names the thing it refuses.
//   header    what that target needs included.
//   verdict   exact | divergent | refused. See the file header.
//   witness   the JavaScript expression on which the two answers differ, for a
//             `divergent` row; empty otherwise.
//   why       for `exact`, what makes it exact and what the exactness is
//             RELATIVE TO. For the other two, the whole reason - this string is
//             the roadmap entry, and it is written in the shape
//             `ctjs.not_lowered` already uses.
class Row<string js, string target, string header, string verdict, string witness, string why> {
  string JS = js;
  string Target = target;
  string Header = header;
  string Verdict = verdict;
  string Witness = witness;
  string Why = why;
}

class Exact<string js, string target, string header, string why>
    : Row<js, target, header, "exact", "", why>;
class Divergent<string js, string target, string header, string witness, string why>
    : Row<js, target, header, "divergent", witness, why>;
class Refused<string js, string target, string header, string why>
    : Row<js, target, header, "refused", "", why>;

//===----------------------------------------------------------------------===//
// Math - the exact rows
//
// EXACT AGAINST THE INTERPRETER, WHICH IS NOT THE SAME AS EXACT AGAINST
// ECMASCRIPT, and the difference matters enough to say once here rather than in
// nine rows. The dialect's policy is that "when a CTJS operation and the
// ctbrowser VM disagree, the VM is correct by definition", so the oracle is
// `ctbrowser/lib/Script/builtins/values.cpp` and not the standard. Where the
// engine itself deviates from ECMAScript the deviation is INHERITED by the
// mapping, which is the right answer for a backend whose job is to agree with
// the tier beside it.
//===----------------------------------------------------------------------===//

// THE ROUNDING FAMILY IS THE EASY HALF and it is easy for a reason worth
// stating: floor, ceil, trunc and sqrt are pinned bit-for-bit by IEEE-754, so
// every implementation of them agrees, INCLUDING on the two cases a rounding
// helper usually gets wrong - the sign of zero and the propagation of NaN.
// `Math.ceil(-0.5)` is -0 and not 0, and std::ceil answers -0 too.
def Math_floor : Exact<"Math.floor", "std::floor", "<cmath>",
    "IEEE-754 roundToIntegralTowardNegative: exact for every double, including "
    "-0 and NaN, and the engine's own builtin is this same call">;
def Math_ceil : Exact<"Math.ceil", "std::ceil", "<cmath>",
    "IEEE-754 roundToIntegralTowardPositive; Math.ceil(-0.5) is -0 in both">;
def Math_trunc : Exact<"Math.trunc", "std::trunc", "<cmath>",
    "IEEE-754 roundToIntegralTowardZero; Math.trunc(-0.5) is -0 in both">;
def Math_sqrt : Exact<"Math.sqrt", "std::sqrt", "<cmath>",
    "IEEE-754 squareRoot is correctly rounded and required to be, so this is "
    "the one transcendental-looking row that is exact on every host">;

// std::fabs, NOT std::abs - see the Math_abs_std_abs refusal below.
def Math_abs : Exact<"Math.abs", "std::fabs", "<cmath>",
    "IEEE-754 abs: clears the sign bit and nothing else, so -0 becomes +0 and a "
    "NaN keeps its payload. The engine's builtin is this same call">;

// NOT std::copysign, and <cmath> has no sign function at all. Math.sign(-0) is
// -0 and Math.sign(NaN) is NaN, so `copysign(1, x)` is wrong twice over - it
// answers -1 for -0 and 1 for NaN. The ternary below is the engine's own
// expression, and it is exact because it returns x UNCHANGED for every input
// that is neither positive nor negative, which is exactly the two zeros and NaN.
def Math_sign : Exact<"Math.sign", "(x > 0 ? 1.0 : (x < 0 ? -1.0 : x))", "",
    "21.3.2.29 steps 1-4 as one expression; the fall-through returns x itself, "
    "which is what makes -0 and NaN come back unchanged">;

// THE TRANSCENDENTALS, AND WHAT THEIR EXACTNESS RESTS ON. ECMAScript leaves
// these implementation-approximated, so there is no standard to be exact
// against - but the engine calls the host's libm and so would the generated
// code, in the same process, against the same libm. That makes them exact
// against the ORACLE, which is the bar this project sets.
//
// MEASURED, NOT ASSUMED: on the devbox (GCC 13.3.0, glibc 2.39) the value GCC
// folds for a literal argument is bit-identical to what glibc computes at run
// time for all eight of sin, cos, tan, exp, log, atan2, pow and sqrt. That is
// what makes a lowering safe to emit for a CONSTANT argument; it is a property
// of this host pair and the divergence document says so.
def Math_sin : Exact<"Math.sin", "std::sin", "<cmath>",
    "the engine's builtin is this same libm call, so the two agree bit-for-bit "
    "in one process; exact against the oracle, not against ECMAScript, which "
    "leaves sin implementation-approximated">;
def Math_cos : Exact<"Math.cos", "std::cos", "<cmath>",
    "as Math.sin: the same libm call the engine makes">;
def Math_tan : Exact<"Math.tan", "std::tan", "<cmath>",
    "as Math.sin: the same libm call the engine makes">;
def Math_exp : Exact<"Math.exp", "std::exp", "<cmath>",
    "as Math.sin: the same libm call the engine makes">;
def Math_log : Exact<"Math.log", "std::log", "<cmath>",
    "as Math.sin: the same libm call the engine makes">;
def Math_atan2 : Exact<"Math.atan2", "std::atan2", "<cmath>",
    "as Math.sin, and binary: the engine passes both arguments through "
    "ToNumber and then calls std::atan2">;

//===----------------------------------------------------------------------===//
// Math - the constants
//
// EXACT BY CONSTRUCTION, and that is checkable rather than assumed: the engine
// sets them from <numbers> too, so the mapping and the oracle read the same
// bits out of the same header. `values.cpp` says why they are not written out
// by hand - "a transcribed constant is a digit waiting to be wrong, and one
// that is wrong in its last few places is invisible".
//===----------------------------------------------------------------------===//

def Math_PI : Exact<"Math.PI", "std::numbers::pi", "<numbers>",
    "the engine sets Math.PI from std::numbers::pi, so this is the same bits">;
def Math_E : Exact<"Math.E", "std::numbers::e", "<numbers>",
    "the engine sets Math.E from std::numbers::e">;
def Math_SQRT2 : Exact<"Math.SQRT2", "std::numbers::sqrt2", "<numbers>",
    "the engine sets Math.SQRT2 from std::numbers::sqrt2">;
def Math_LN2 : Exact<"Math.LN2", "std::numbers::ln2", "<numbers>",
    "the engine sets Math.LN2 from std::numbers::ln2">;
def Math_LN10 : Exact<"Math.LN10", "std::numbers::ln10", "<numbers>",
    "the engine sets Math.LN10 from std::numbers::ln10 - despite a comment two "
    "lines above it in values.cpp claiming <numbers> has no ln10">;
def Math_LOG2E : Exact<"Math.LOG2E", "std::numbers::log2e", "<numbers>",
    "the engine sets Math.LOG2E from std::numbers::log2e">;
def Math_LOG10E : Exact<"Math.LOG10E", "std::numbers::log10e", "<numbers>",
    "the engine sets Math.LOG10E from std::numbers::log10e">;

//===----------------------------------------------------------------------===//
// String.prototype.trim - mapped, but NOT to Boost
//===----------------------------------------------------------------------===//

// THE ENGINE ALREADY OWNS THIS FUNCTION, and it is not the one the
// specification named. `core/algorithms.hpp` has a constexpr `trim(text, set)`
// whose whitespace SET is a parameter, because "HTML, JavaScript and the GLSL
// preprocessor genuinely disagree about what whitespace is and unifying them
// would be a bug" - and `js_whitespace` is the JavaScript one. It returns a
// string_view rather than a string, which the same header records as **7x
// faster** over 200,000 trims (2.0 ms against 14.2 ms) than the Boost call.
//
// It is BYTE-FOR-BYTE the interpreter's set: builtins/text.cpp's `trim` uses
// the literal " \t\n\r\f\v", which is `js_whitespace` spelled out.
def String_trim : Exact<"String.prototype.trim",
    "ctbrowser::trim(s, ctbrowser::js_whitespace)",
    "<ctbrowser/core/algorithms.hpp>",
    "the same character set the interpreter's builtin uses, spelled once; ASCII "
    "only, which is deliberate - CLAUDE.md keeps this set locale-free so a "
    "render cannot depend on LC_ALL">;

//===----------------------------------------------------------------------===//
// The divergent rows - a witness each, and the test pins it
//===----------------------------------------------------------------------===//

// THE HEADLINE. JavaScript rounds a tie toward POSITIVE INFINITY; std::round
// rounds a tie AWAY FROM ZERO. They are different functions that agree on
// positive inputs, which is what makes the mistake survive review.
def Math_round : Divergent<"Math.round", "std::round", "<cmath>",
    "Math.round(-0.5)",
    "21.3.2.28 rounds a tie toward +Infinity, so Math.round(-0.5) is -0 and "
    "Math.round(-2.5) is -2; std::round rounds a tie away from zero and answers "
    "-1 and -3. `std::floor(x + 0.5)` is not the repair either - values.cpp "
    "lists the three separate ways that one is wrong">;

// std::min and std::max are the wrong function TWICE OVER, and values.cpp
// already says so about the interpreter's own implementation.
def Math_min : Divergent<"Math.min", "std::min", "<algorithm>",
    "Math.min(1, NaN)",
    "std::min is written in terms of `<`, and IEEE comparison says no to "
    "everything involving NaN - so std::min(1, NaN) is 1 where 21.3.2.24 step "
    "4.a returns NaN the moment it sees one. It is also wrong on -0, where the "
    "answer depends on argument order, and Math.min() with no arguments is "
    "+Infinity, which std::min cannot express at all">;
def Math_max : Divergent<"Math.max", "std::max", "<algorithm>",
    "Math.max(-0, 0)",
    "as Math.min, and the zero case is live rather than theoretical: "
    "std::max(-0, 0) answers -0 because `-0 < 0` is false, where 21.3.2.25 step "
    "4.b requires +0. Math.max() with no arguments is -Infinity">;

// C99 AND ECMASCRIPT DISAGREE ABOUT pow, and values.cpp fixed it in the
// interpreter for exactly this reason.
def Math_pow : Divergent<"Math.pow", "std::pow", "<cmath>",
    "Math.pow(1, NaN)",
    "C99 says pow(+-1, y) is 1 for EVERY y including NaN and the infinities; "
    "Number::exponentiate says NaN for exactly those. std::pow(1, NaN) is 1 and "
    "std::pow(-1, Infinity) is 1, where JavaScript answers NaN for both - a "
    "value a page will do arithmetic on rather than checking with isNaN">;

// <numbers> HAS NO inv_sqrt2 AT ALL - it has inv_sqrt3 and inv_sqrtpi - so the
// natural spelling of this row does not compile, and the obvious replacement is
// one ulp out.
def Math_SQRT1_2 : Divergent<"Math.SQRT1_2", "1.0 / std::numbers::sqrt2", "<numbers>",
    "Math.SQRT1_2",
    "the reciprocal rounds TWICE - once in sqrt2 and again in the division - "
    "and lands one ulp low, 0x3fe6a09e667f3bcc against the correctly rounded "
    "0x3fe6a09e667f3bcd. The engine derives it as sqrt2/2, where halving only "
    "decrements the exponent and cannot round. 21.3.1.8 asks for the Number "
    "value NEAREST the true root, so this is an exact requirement">;

// DETERMINISM, WHICH IS A PROJECT INVARIANT AND NOT A PREFERENCE.
def Math_random : Divergent<"Math.random", "std::mt19937_64", "<random>",
    "Math.random()",
    "CLAUDE.md: `Math.random` is seeded and DETERMINISTIC by default, because "
    "three example pages byte-compare their render against a golden and a page "
    "drawing with random cannot have one otherwise. The stream is a xorshift64* "
    "held per context in builtins/values.cpp; ANY other generator is a "
    "different stream and every golden that touches it moves">;

// THE SAME INVARIANT, THROUGH THE OTHER GLOBAL THE SPECIFICATION NAMES.
def Date_now : Divergent<"Date.now", "std::chrono::system_clock::now()", "<chrono>",
    "Date.now()",
    "Date.now() reads context::clock_ms(), whose default is the FIXED base "
    "1767225600000 (2026-01-01T00:00:00Z) - vm.hpp: 'the frozen clock was "
    "deliberate, for the reason Math.random is seeded'. An embedder that wants "
    "real time installs one with set_clock, and browser::set_clock is that "
    "embedder. A wall-clock call in generated code bypasses BOTH: it breaks "
    "every golden, and it ignores the clock the host installed">;

// INSERTION ORDER AND SameValueZero, either of which alone refutes the row.
def Set_ctor : Divergent<"new Set()", "std::unordered_set<T>", "<unordered_set>",
    "new Set() with NaN and both zeros",
    "TWO independent divergences. (1) A JS Set keys by SameValueZero: NaN is "
    "equal to itself and +0 is equal to -0, so {NaN, NaN, +0, -0} has size 2. "
    "std::unordered_set<double> uses operator==, where NaN != NaN, so the same "
    "four inserts give size 3. (2) JS iterates in INSERTION order - the engine "
    "stores entries in a vector - and unordered_set does not: 3,1,2 comes back "
    "as 2,1,3 on libstdc++. An insertion-ordered structure with a "
    "SameValueZero hash is the lowering; unordered_set is not">;
def Map_ctor : Divergent<"new Map()", "std::unordered_map<K,V>", "<unordered_map>",
    "new Map() with NaN and both zeros",
    "as new Set(), and the key-equality half is worse here because a second NaN "
    "key does not merely add an element - it makes the FIRST one unreachable by "
    "lookup, since no probe for NaN can ever equal a stored NaN">;

// A REGEX THAT SILENTLY MATCHES DIFFERENTLY IS THE WORST FAILURE IN THIS FILE.
def RegExp_ctor : Divergent<"new RegExp(...)", "boost::regex", "<boost/regex.hpp>",
    "/(.)\\1/.test('aa')",
    "boost::regex is not this engine's regex. script/regex.hpp is a "
    "self-contained backtracker ported from ctjs's rxd, and its header says "
    "outright what it does not have: 'NOT here, and said out loud rather than "
    "mis-matched: lookbehind and backreferences'. A lookbehind makes the "
    "program not-ok so `test` is always false; a `\\1` falls through "
    "rx_escape_char's default and becomes the LITERAL character '1'. Boost "
    "implements both. So /(.)\\1/ matches 'a1' here and 'aa' there - it "
    "disagrees in BOTH directions, and neither direction fails loudly. There is "
    "no proved subset yet: `.` and the flags would each need their own proof">;

// JSON, WHERE THE PARSER AND THE SERIALISER FAIL DIFFERENTLY.
def JSON_parse : Divergent<"JSON.parse", "boost::json::parse", "<boost/json.hpp>",
    "JSON.parse('{oops')",
    "the FAILURE PATH is a different mechanism. builtins/async.cpp answers "
    "`undefined` for a document that does not parse - this VM has no exceptions "
    "- where boost::json::parse throws boost::system::system_error. Which path "
    "a call takes depends on the INPUT, so no static proof can rule the failure "
    "path out, and a page testing `if (parsed === undefined)` would instead see "
    "an exception unwind through it">;
def JSON_stringify : Divergent<"JSON.stringify", "boost::json::serialize", "<boost/json.hpp>",
    "JSON.stringify(0.1)",
    "a DIFFERENT NUMBER FORMATTER. JavaScript's Number::toString is the "
    "shortest decimal that round-trips, which the engine implements in "
    "Script/number_format.cpp: 0.1 is \"0.1\" and 1e21 is \"1e+21\". "
    "boost::json::serialize writes \"1E-1\" and \"1E21\". Every number in every "
    "serialised document changes. The top-level `undefined` rule diverges too: "
    "25.5.2 step 12 makes JSON.stringify(undefined) the value undefined and not "
    "any string, which a boost::json::value cannot represent">;

//===----------------------------------------------------------------------===//
// The refused rows - no witness is possible, so the reason is all there is
//===----------------------------------------------------------------------===//

// THE SPELLING IN THE SPECIFICATION'S TABLE, kept as its own row so the
// refusal names the thing it refuses. See Math_abs for what to emit instead.
def Math_abs_std_abs : Refused<"Math.abs", "std::abs", "<cmath>",
    "`std::abs` is an overload SET, and <cstdlib> puts int and long overloads "
    "in it. Phase 55 narrows a bounded number to int32_t on purpose, and the "
    "moment it does, `std::abs(x)` selects the integer overload - where "
    "std::abs(INT_MIN) is undefined behaviour and returns INT_MIN on this "
    "target, against JavaScript's 2147483648. std::fabs has no integer overload "
    "and cannot be captured this way. Refused rather than witnessed because the "
    "witness is the undefined behaviour itself">;

// A CORRECTION THE ENGINE APPLIES ON TOP OF libm, whose visibility depends on
// the host - which is precisely the shape a byte-compared golden cannot carry.
def Math_cbrt : Refused<"Math.cbrt", "std::cbrt", "<cmath>",
    "the engine does not call std::cbrt: it rounds the result to the nearest "
    "integer and returns that when the cube is exact, because values.cpp "
    "measured glibc answering 3.0000000000000004 for cbrt(27). On the devbox "
    "today (glibc 2.39) std::cbrt(27) IS exactly 3, so the two agree HERE and "
    "the correction is invisible. A mapping whose exactness is a property of "
    "the host's libm cannot be proved, and this project byte-compares renders "
    "across Linux and Windows. No witness exists on this box, which is the "
    "reason this row is refused rather than divergent">;

// THE ROW THE SPECIFICATION NAMED FOR trim. It is not observably wrong today,
// and that is not the same as being right.
def String_trim_boost : Refused<"String.prototype.trim",
    "boost::algorithm::trim_copy", "<boost/algorithm/string.hpp>",
    "trim_copy classifies with std::isspace against the GLOBAL locale, so what "
    "it trims is a property of the process rather than of the program. Under "
    "the classic locale that set is \" \\t\\n\\v\\f\\r\" - byte-for-byte the "
    "engine's - so it agrees today and there is no witness to write. It stops "
    "agreeing the moment anything calls std::locale::global, and CLAUDE.md "
    "rejects exactly this class of dependency: the engine's own helpers are "
    "ASCII-only ON PURPOSE, because 'goldens are byte-compared across Linux and "
    "Windows, so a locale-aware fold would make a render depend on LC_ALL'. "
    "Refused for unprovability, not for observed disagreement. Note also that "
    "the engine and Boost agree with each other and BOTH diverge from "
    "ECMAScript, which trims U+00A0 and U+FEFF as well - a declared divergence "
    "inherited from the engine rather than introduced by the mapping">;
