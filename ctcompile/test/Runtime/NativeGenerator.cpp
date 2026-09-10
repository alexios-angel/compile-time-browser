// DOES A C++ COROUTINE PRODUCE WHAT A JS GENERATOR PRODUCES?
//
// Phase 58's gate, and the whole of Stage 58A. `24-native-cpp-backend.md` calls
// `function*` -> coroutine and `yield` -> `co_yield` "the cleanest mapping in
// the whole specification". Nothing lowers yet - the `ctnative` dialect does
// not exist and the importer refuses every suspending opcode - so this test
// asks the question the lowering will have to answer, by hand: for each JS
// generator shape, is there a `ctnative::generator<T>` that produces the same
// sequence the interpreter does?
//
// NOTHING HERE WRITES AN EXPECTED ANSWER DOWN for the JS side. The interpreter
// is correct by definition ("when a CTJS operation and the ctbrowser VM
// disagree, the VM is correct by definition"), so every JS answer is whatever
// the VM says and the C++ coroutine is compared against it. Numbers are
// formatted by the ENGINE'S own `to_string` on both sides, so a difference in
// how a double prints can never masquerade as a difference in what was
// produced.
//
// THE FOUR SHAPES ARE CHOSEN TO SEPARATE THE MAPPING FROM ITS LIMITS, and two
// of them are DECLARED DIVERGENCES rather than agreements. A test that only
// showed the cases that agree would certify a mapping that does not exist for
// the corpus this project actually has: all 622 `function*` bodies in
// ctbrowser/vendor/babylon/babylon.js are driven by the minified TypeScript
// `__awaiter`, which uses `.next(v)`, `.throw(e)` and the done-value - the
// three things shapes 3 and 4 below show a C++ generator cannot do.
//
//   1. ids        an infinite generator taken with a bound   MUST AGREE
//   2. early      `return;` before the loop ends             MUST AGREE
//   3. earlyValue `return 99;` - the value is observable     MUST DIVERGE, once
//   4. echo       `const got = yield sum` - a sent value     MUST DIVERGE
//
// A divergence case asserts the divergence, and asserts that it is confined to
// the field it is supposed to be in. If somebody makes `return v` observable,
// case 3 goes red and says so - which is the point of writing it down as a test
// rather than as a paragraph.
#include <ctbrowser/aot/generator.hpp>
#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/vm.hpp>

#include <cstdio>
#include <span>
#include <string>
#include <string_view>

namespace ctnative = ctbrowser::ctnative;

using ctbrowser::script::context;
using ctbrowser::script::program;
using ctbrowser::script::value;

namespace {

// THE JS SIDE. `.next()` rather than `for...of` on purpose: `for...of` cannot
// see `done`, cannot see a returned value and cannot send one in, so it would
// hide exactly the three properties this test exists to measure.
constexpr std::string_view fixture = R"JS(
function* ids() { let i = 0; while (true) { yield i++; } }

function* early(stop) {
  let i = 0;
  while (i < 8) { if (i === stop) { return; } yield i * 3; i = i + 1; }
}

function* earlyValue(stop) {
  let i = 0;
  while (i < 8) { if (i === stop) { return 99; } yield i * 3; i = i + 1; }
}

function* echo() { let sum = 0; while (true) { const got = yield sum; sum = sum + got; } }

function record(g, steps) {
  var out = "";
  for (var k = 0; k < steps; k = k + 1) {
    var r = g.next();
    out = out + r.value + "/" + r.done + ";";
  }
  return out;
}

function driveIds(steps) { return record(ids(), steps); }
function driveEarly(stop, steps) { return record(early(stop), steps); }
function driveEarlyValue(stop, steps) { return record(earlyValue(stop), steps); }

// THE SENT VALUE IS LOAD-BEARING HERE. Each resume adds what it was sent, so a
// generator that discarded the sent value answers 0 for ever.
function driveEcho(steps) {
  var g = echo();
  var out = "" + g.next().value + "/false;";
  for (var k = 1; k < steps; k = k + 1) {
    var r = g.next(k);
    out = out + r.value + "/" + r.done + ";";
  }
  return out;
}
)JS";

// ---- the C++ mirrors, written the way Stage 58B would emit them -------------

ctnative::generator<double> ids_native() {
    double i = 0;
    while (true) {
        co_yield i;
        i += 1;
    }
}

ctnative::generator<double> early_native(double stop) {
    double i = 0;
    while (i < 8) {
        if (i == stop) { co_return; }
        co_yield i * 3;
        i += 1;
    }
}

// SHAPE 3'S MIRROR IS THE SAME BODY. `return 99` has nowhere to go: the promise
// type has `return_void`, exactly as std::generator does, so the 99 is dropped
// and this is `early_native` again. Written out rather than aliased so the
// divergence is visible at the site that causes it.
ctnative::generator<double> early_value_native(double stop) {
    double i = 0;
    while (i < 8) {
        if (i == stop) { co_return; }
        co_yield i * 3;
        i += 1;
    }
}

// SHAPE 4'S MIRROR CANNOT BE WRITTEN, and this is the closest thing to it that
// compiles: `co_yield` produces nothing, so there is no `got` to add. The
// header's last static_assert is the proof that this is a property of the
// language rather than of this file; this function is what that costs.
ctnative::generator<double> echo_native() {
    double sum = 0;
    while (true) { co_yield sum; }
}

// ---- the JS iterator protocol, emulated over the range interface ------------
//
// This IS the mapping's cost, made concrete. A JS consumer asks `.next()` and
// gets `{value, done}`; a C++ consumer has `begin()`, `++` and a sentinel, and
// `done` is "the iterator reached the sentinel" while `value` is "there is
// nothing to read". The emulation is what any lowering of a `.next()`-driven
// generator would have to synthesise.
std::string record_native(context & cx, ctnative::generator<double> produced, unsigned steps) {
    std::string out;
    auto it = produced.begin();
    const auto finished = produced.end();
    for (unsigned step = 0; step < steps; ++step) {
        // NOT `++it` UNCONDITIONALLY. "A generator that has finished keeps
        // answering rather than running its body again" is free in JavaScript
        // and is UNDEFINED BEHAVIOUR in C++ - resuming a coroutine whose
        // `done()` is true. The consumer carries that difference, which is one
        // more thing a lowering of `.next()` would have to synthesise.
        if (step > 0 && it != finished) { ++it; }
        if (it == finished) {
            out += "undefined/true;";
        } else {
            out += cx.to_string(value::number(*it));
            out += "/false;";
        }
    }
    return out;
}

std::string drive(context & cx, const char * entry, std::span<const value> args) {
    return cx.to_string(cx.call(cx.global(entry), args, value::undefined()));
}

// HOW MANY `a/b;` FIELDS THE TWO ANSWERS DIFFER IN, and the first index where.
// A divergence case that only said "they differ" would pass just as happily if
// the whole answer were wrong.
struct difference {
    unsigned fields = 0;
    unsigned differing = 0;
    int first = -1;
};

difference compare_fields(const std::string & left, const std::string & right) {
    difference found;
    std::size_t at_left = 0;
    std::size_t at_right = 0;
    while (at_left < left.size() || at_right < right.size()) {
        const std::size_t end_left = left.find(';', at_left);
        const std::size_t end_right = right.find(';', at_right);
        if (end_left == std::string::npos || end_right == std::string::npos) {
            // A DIFFERENT NUMBER OF FIELDS is a difference in every remaining
            // one, and never a silent pass.
            found.differing += 1;
            if (found.first < 0) { found.first = static_cast<int>(found.fields); }
            break;
        }
        const std::string one = left.substr(at_left, end_left - at_left);
        const std::string other = right.substr(at_right, end_right - at_right);
        if (one != other) {
            found.differing += 1;
            if (found.first < 0) { found.first = static_cast<int>(found.fields); }
        }
        found.fields += 1;
        at_left = end_left + 1;
        at_right = end_right + 1;
    }
    return found;
}

unsigned failures = 0;
unsigned compared = 0;
unsigned agreed = 0;
unsigned diverged = 0;

void must_agree(const char * name, const std::string & interpreted, const std::string & native) {
    ++compared;
    if (interpreted == native) {
        ++agreed;
        std::printf("%-12s agrees   %s\n", name, interpreted.c_str());
        return;
    }
    ++failures;
    std::printf("%-12s FAILED\n    interpreted %s\n    coroutine   %s\n", name, interpreted.c_str(),
                native.c_str());
}

void must_diverge(const char * name, const std::string & interpreted, const std::string & native,
                  unsigned expected_fields, int expected_first, const char * because) {
    ++compared;
    const difference found = compare_fields(interpreted, native);
    if (found.differing == expected_fields && found.first == expected_first) {
        ++diverged;
        std::printf("%-12s DIVERGES in %u field(s) from index %d - %s\n    interpreted %s\n"
                    "    coroutine   %s\n",
                    name, found.differing, found.first, because, interpreted.c_str(),
                    native.c_str());
        return;
    }
    ++failures;
    std::printf("%-12s FAILED - expected %u differing field(s) from index %d, got %u from %d\n"
                "    interpreted %s\n    coroutine   %s\n",
                name, expected_fields, expected_first, found.differing, found.first,
                interpreted.c_str(), native.c_str());
}

} // namespace

int main() {
    program compiled = ctbrowser::script::compiler::compile(std::string(fixture));
    if (!compiled.ok) {
        std::printf("the fixture did not compile\n");
        return 1;
    }

    context cx;
    ctbrowser::script::install_builtins(cx);
    if (!cx.run(compiled).ok) {
        std::printf("the fixture did not run\n");
        return 1;
    }

    constexpr unsigned steps = 6;
    const value bound[] = {value::number(static_cast<double>(steps))};
    const value stop_and_steps[] = {value::number(3.0), value::number(static_cast<double>(steps))};

    // 1. THE GATE THE PLAN NAMES: an infinite generator taken with a bound.
    must_agree("ids", drive(cx, "driveIds", bound), record_native(cx, ids_native(), steps));

    // 2. AND A GENERATOR THAT RETURNS EARLY. Six steps against a body that
    // stops after three, so the answer contains the transition to done AND the
    // "a finished generator keeps answering" behaviour after it.
    must_agree("early", drive(cx, "driveEarly", stop_and_steps),
               record_native(cx, early_native(3.0), steps));

    // 3. THE FIRST DECLARED DIVERGENCE. `return 99` is the value of the DONE
    // record in JavaScript and is invisible in C++, so exactly ONE field
    // differs - the fourth, the one where the generator finishes - and every
    // field before and after it is identical. The "after" matters: it separates
    // "the return value is lost" from "the whole tail is wrong".
    must_diverge("earlyValue", drive(cx, "driveEarlyValue", stop_and_steps),
                 record_native(cx, early_value_native(3.0), steps), 1, 3,
                 "`return v` is observable through .next() and has no C++ spelling");

    // 4. THE SECOND, AND THE ONE THAT DECIDES PHASE 58'S SCOPE. `const got =
    // yield sum` reads what `.next(v)` sent in. `co_yield` produces nothing, so
    // the C++ mirror can only ever answer 0. Every field from the second one on
    // differs, which is what "this shape is not expressible" looks like.
    must_diverge("echo", drive(cx, "driveEcho", bound), record_native(cx, echo_native(), steps),
                 steps - 1, 1, "`.next(v)` sends a value in and co_yield yields nothing back");

    // THE COUNTERS, ASSERTED. Without this a fixture that stopped compiling one
    // of the four - a renamed global, a `drive` that threw - would report a
    // clean run of the cases that remained.
    if (compared != 4 || agreed != 2 || diverged != 2) {
        std::printf("\nFAILED - expected 4 comparisons, 2 agreements and 2 declared "
                    "divergences; got %u, %u and %u\n",
                    compared, agreed, diverged);
        ++failures;
    }

    // AND THE SWITCH IS REPORTED rather than assumed. The devbox has no
    // <generator> in libstdc++ 13.3 and this line is what says so out loud on
    // whatever machine the suite next runs on.
    std::printf("\nstd::generator available=%d adopted=%d\n", CTNATIVE_HAS_STD_GENERATOR,
                CTNATIVE_USE_STD_GENERATOR);

    if (failures == 0) {
        std::printf("%u shapes: %u agree with the interpreter, %u diverge as declared\n", compared,
                    agreed, diverged);
    }
    return failures == 0 ? 0 : 1;
}
