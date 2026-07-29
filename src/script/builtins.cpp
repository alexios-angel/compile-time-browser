#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/regex.hpp>

#include <map>
#include <memory>

// The JavaScript standard library.
//
// the engine had NONE of this. The whole property surface was `.length`, numeric
// indexing and named lookup on plain objects - no `arr.push`, no `str.split`,
// no `Math.floor`, no `JSON.parse`. A VM can be complete and still useless if
// nothing can be done with a value once you have one, and that is what this
// closes.
//
// It is a SUBSET, chosen by what pages actually call rather than by what the
// spec lists. The omissions that matter are named at the bottom of this file
// rather than left to be discovered.

namespace ctbrowser::script {

// --- argument helpers, the same vocabulary the DOM bindings use -----------

[[nodiscard]] inline value arg_at(std::span<value> args, std::size_t i) {
    return i < args.size() ? args[i] : value::undefined();
}
[[nodiscard]] inline double num_at(std::span<value> args, std::size_t i) {
    return i < args.size() ? context::to_number(args[i]) : 0.0;
}
[[nodiscard]] inline std::string str_at(context & cx, std::span<value> args, std::size_t i) {
    return i < args.size() ? cx.to_string(args[i]) : std::string{};
}

// Clamp a possibly-negative, possibly-huge index the way the array and string
// methods all do: negative counts from the end, out of range clamps.
[[nodiscard]] inline std::size_t clamp_index(double raw, std::size_t length) {
    if (std::isnan(raw)) { return 0; }
    if (raw < 0) {
        const double from_end = static_cast<double>(length) + raw;
        return from_end < 0 ? 0 : static_cast<std::size_t>(from_end);
    }
    return raw > static_cast<double>(length) ? length : static_cast<std::size_t>(raw);
}

namespace detail {

// The receiver as its concrete type, or null when a method was called on
// something else. Every prototype method starts with one of these, and
// returning undefined rather than crashing is what makes `[].push.call(3)`
// harmless instead of fatal.
[[nodiscard]] inline array_object * this_array(context & cx) {
    const value self = cx.current_this();
    return self.is_array() ? static_cast<array_object *>(self.as_heap()) : nullptr;
}
[[nodiscard]] inline std::string this_string(context & cx) {
    const value self = cx.current_this();
    return self.is_string() ? static_cast<string_object *>(self.as_heap())->text
                            : cx.to_string(self);
}

[[nodiscard]] inline object_object * new_table(context & cx) {
    return static_cast<object_object *>(cx.make_object().as_heap());
}

// A native allocated from inside another native - `bind` returns one.
[[nodiscard]] inline native_object * cx_native(context & cx, std::string name, native_fn fn) {
    return cx.allocate<native_object>(std::move(name), std::move(fn));
}

inline void method(context & cx, object_object * table, std::string name, native_fn fn) {
    table->set(name, value::object(cx.allocate<native_object>(name, std::move(fn))));
}

// --- promises ---------------------------------------------------------------
//
// SETTLED-ONLY, like the previous engine's. A promise here is an ordinary object carrying
// `__value` and `__rejected`, created ALREADY settled: there is no job queue,
// no microtask checkpoint, and nothing pending. `then` therefore runs its
// callback IMMEDIATELY rather than after the current turn.
//
// That is enough for the shape real pages are written in - `await fetch(url)`,
// `.then(r => r.json())`, `try { await f() } catch (e)` - because every source
// of asynchrony ctbrowser has (assets, timers, rAF) either resolves at once or
// goes through the event loop instead. It is NOT enough for code that depends
// on ordering between a `then` and the surrounding statements, and code written
// against a real event loop can observe the difference.
[[nodiscard]] inline value make_promise(context & cx, value v, bool rejected);

// A promise can be PENDING now.
//
// It was settled-only: created already resolved, `then` running its callback
// immediately, `new Promise(executor)` absent entirely because an executor
// implies pending state. That was enough for `await fetch(url)` and not enough
// for anything that waits - and p5.js starts with
// `Promise.all([waitForDocumentReady(), waitingForTranslator]).then(_globalInit)`,
// so the library could not begin without it.
//
// TODO: a microtask queue. A handler should run at the end of the turn, not
// the moment the promise settles - drain it from run_due_callbacks, after
// timers and before rAF, and again after each event dispatch.
// What is still missing, and it is a real difference: there is no MICROTASK
// QUEUE. A handler runs the moment the promise settles rather than at the end
// of the turn, so code that depends on ordering between a `then` and the
// statements after it sees them in the wrong order. Every promise here settles
// either synchronously or from the event loop, where the distinction does not
// arise; a page written against a real queue can observe it.
//
// State lives on the object: `__value` and `__rejected` as before, plus
// `__settled` and `__handlers`. Keeping the old two means everything that read
// them still works.
[[nodiscard]] inline value make_promise(context & cx, value v, bool rejected);
inline void settle(context & cx, value promise, value with, bool rejected);

// Run one registered handler and settle the promise it produced.
inline void deliver(context & cx, value handler_record, value settled, bool rejected) {
    auto * record = static_cast<object_object *>(handler_record.as_heap());
    value * on_ok = record->find("ok");
    value * on_err = record->find("err");
    value * next = record->find("next");
    const value handler = rejected ? (on_err == nullptr ? value::undefined() : *on_err)
                                   : (on_ok == nullptr ? value::undefined() : *on_ok);
    if (next == nullptr) { return; }
    if (!handler.is_callable()) {
        // No handler for how this settled: it passes straight through, so a
        // rejection survives a bare `.then(f)` and a later `.catch` sees it.
        settle(cx, *next, settled, rejected);
        return;
    }
    const value args[1] = {settled};
    const value produced = cx.call(handler, args);
    // A handler returning a promise ADOPTS it, which is what makes a chain of
    // `then`s that each do async work run in order rather than all at once.
    if (produced.is_object()) {
        auto * inner = static_cast<object_object *>(produced.as_heap());
        if (inner->find("__settled") != nullptr) {
            value * inner_settled = inner->find("__settled");
            value * inner_value = inner->find("__value");
            value * inner_rejected = inner->find("__rejected");
            if (context::truthy(*inner_settled)) {
                settle(cx, *next, inner_value == nullptr ? value::undefined() : *inner_value,
                       inner_rejected != nullptr && context::truthy(*inner_rejected));
            } else {
                // still pending: chain onto it
                value * handlers = inner->find("__handlers");
                if (handlers != nullptr && handlers->is_array()) {
                    object_object * record2 = new_table(cx);
                    record2->set("next", *next);
                    static_cast<array_object *>(handlers->as_heap())
                        ->items.push_back(value::object(record2));
                }
            }
            return;
        }
    }
    settle(cx, *next, produced, false);
}

inline void settle(context & cx, value promise, value with, bool rejected) {
    if (!promise.is_object()) { return; }
    auto * p = static_cast<object_object *>(promise.as_heap());
    value * already = p->find("__settled");
    if (already != nullptr && context::truthy(*already)) { return; } // settle once
    p->set("__value", with);
    p->set("__rejected", value::boolean(rejected));
    p->set("__settled", value::boolean(true));
    value * handlers = p->find("__handlers");
    if (handlers == nullptr || !handlers->is_array()) { return; }
    // COPIED before draining: a handler may register another on this same
    // promise, and appending to the vector being walked invalidates it.
    const std::vector<value> pending = static_cast<array_object *>(handlers->as_heap())->items;
    static_cast<array_object *>(handlers->as_heap())->items.clear();
    for (const value & record : pending) { deliver(cx, record, with, rejected); }
}

// `then`/`catch`/`finally` all reduce to: remember what to do for each way this
// can settle, and either do it now or when it settles.
inline value settle_with(context & cx, value on_ok, value on_err) {
    const value self = cx.current_this();
    if (!self.is_object()) { return self; }
    auto * promise = static_cast<object_object *>(self.as_heap());

    const value next = make_promise(cx, value::undefined(), false);
    static_cast<object_object *>(next.as_heap())->set("__settled", value::boolean(false));
    object_object * record = new_table(cx);
    record->set("ok", on_ok);
    record->set("err", on_err);
    record->set("next", next);

    value * settled = promise->find("__settled");
    if (settled != nullptr && context::truthy(*settled)) {
        value * held = promise->find("__value");
        value * state = promise->find("__rejected");
        deliver(cx, value::object(record), held == nullptr ? value::undefined() : *held,
                state != nullptr && context::truthy(*state));
        return next;
    }
    value * handlers = promise->find("__handlers");
    if (handlers != nullptr && handlers->is_array()) {
        static_cast<array_object *>(handlers->as_heap())->items.push_back(value::object(record));
    }
    return next;
}

[[nodiscard]] inline value make_promise(context & cx, value v, bool rejected) {
    object_object * promise = new_table(cx);
    promise->set("__value", v);
    promise->set("__rejected", value::boolean(rejected));
    promise->set("__settled", value::boolean(true));
    promise->set("__handlers", cx.make_array());
    method(cx, promise, "then", [](context & c, std::span<value> a) {
        return settle_with(c, a.empty() ? value::undefined() : a[0],
                           a.size() > 1 ? a[1] : value::undefined());
    });
    method(cx, promise, "catch", [](context & c, std::span<value> a) {
        return settle_with(c, value::undefined(), a.empty() ? value::undefined() : a[0]);
    });
    method(cx, promise, "finally", [](context & c, std::span<value> a) {
        if (!a.empty() && a[0].is_callable()) { (void)c.call(a[0], std::span<const value>{}); }
        return c.current_this();
    });
    return value::object(promise);
}

// --- JSON -----------------------------------------------------------------

inline void write_json(context & cx, value v, std::string & out) {
    if (v.is_string()) {
        out += '"';
        for (const char c : static_cast<string_object *>(v.as_heap())->text) {
            switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    static constexpr char hex[] = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[(static_cast<unsigned char>(c) >> 4) & 0xF];
                    out += hex[static_cast<unsigned char>(c) & 0xF];
                } else {
                    out += c;
                }
            }
        }
        out += '"';
        return;
    }
    if (v.is_array()) {
        out += '[';
        const auto & items = static_cast<array_object *>(v.as_heap())->items;
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i > 0) { out += ','; }
            write_json(cx, items[i], out);
        }
        out += ']';
        return;
    }
    if (v.is_object()) {
        out += '{';
        const auto & props = static_cast<object_object *>(v.as_heap())->props;
        bool first = true;
        for (const auto & [key, item] : props) {
            // undefined and functions are OMITTED from an object, per spec -
            // which is why round-tripping a value through JSON can lose fields.
            if (item.is_undefined() || item.is_callable()) { continue; }
            if (!first) { out += ','; }
            first = false;
            write_json(cx, cx.string(key), out);
            out += ':';
            write_json(cx, item, out);
        }
        out += '}';
        return;
    }
    if (v.is_undefined()) {
        out += "null"; // undefined inside an array becomes null, per spec
        return;
    }
    out += cx.to_string(v);
}

struct json_reader {
    context & cx;
    std::string_view text;
    std::size_t at = 0;
    bool ok = true;

    void skip() {
        while (at < text.size() &&
               (text[at] == ' ' || text[at] == '\t' || text[at] == '\n' || text[at] == '\r')) {
            ++at;
        }
    }
    [[nodiscard]] value parse() {
        skip();
        if (at >= text.size()) {
            ok = false;
            return value::undefined();
        }
        const char c = text[at];
        if (c == '{') { return parse_object(); }
        if (c == '[') { return parse_array(); }
        if (c == '"') { return cx.string(parse_string()); }
        if (text.compare(at, 4, "true") == 0) {
            at += 4;
            return value::boolean(true);
        }
        if (text.compare(at, 5, "false") == 0) {
            at += 5;
            return value::boolean(false);
        }
        if (text.compare(at, 4, "null") == 0) {
            at += 4;
            return value::null();
        }
        return parse_number();
    }
    [[nodiscard]] std::string parse_string() {
        std::string out;
        ++at; // opening quote
        while (at < text.size() && text[at] != '"') {
            if (text[at] == '\\' && at + 1 < text.size()) {
                ++at;
                switch (text[at]) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    // \uXXXX, encoded as UTF-8. Surrogate pairs are not joined:
                    // a lone high surrogate becomes U+FFFD rather than silently
                    // producing invalid UTF-8.
                    std::uint32_t code = 0;
                    for (int i = 0; i < 4 && at + 1 < text.size(); ++i) {
                        ++at;
                        const char h = text[at];
                        code = code * 16 + static_cast<std::uint32_t>(
                                               h <= '9' ? h - '0' : (h | 0x20) - 'a' + 10);
                    }
                    if (code >= 0xD800 && code <= 0xDFFF) { code = 0xFFFD; }
                    if (code < 0x80) {
                        out += static_cast<char>(code);
                    } else if (code < 0x800) {
                        out += static_cast<char>(0xC0 | (code >> 6));
                        out += static_cast<char>(0x80 | (code & 0x3F));
                    } else {
                        out += static_cast<char>(0xE0 | (code >> 12));
                        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (code & 0x3F));
                    }
                    break;
                }
                default: out += text[at];
                }
                ++at;
                continue;
            }
            out += text[at++];
        }
        if (at < text.size()) { ++at; } // closing quote
        return out;
    }
    [[nodiscard]] value parse_number() {
        const std::size_t start = at;
        if (at < text.size() && (text[at] == '-' || text[at] == '+')) { ++at; }
        while (at < text.size() &&
               ((text[at] >= '0' && text[at] <= '9') || text[at] == '.' || text[at] == 'e' ||
                text[at] == 'E' || text[at] == '-' || text[at] == '+')) {
            ++at;
        }
        if (at == start) {
            ok = false;
            return value::undefined();
        }
        return value::number(
            std::strtod(std::string{text.substr(start, at - start)}.c_str(), nullptr));
    }
    [[nodiscard]] value parse_array() {
        auto * arr = static_cast<array_object *>(cx.make_array().as_heap());
        const value held = value::object(arr);
        ++at; // '['
        skip();
        if (at < text.size() && text[at] == ']') {
            ++at;
            return held;
        }
        while (at < text.size() && ok) {
            arr->items.push_back(parse());
            skip();
            if (at < text.size() && text[at] == ',') {
                ++at;
                continue;
            }
            break;
        }
        if (at < text.size() && text[at] == ']') { ++at; }
        return held;
    }
    [[nodiscard]] value parse_object() {
        auto * obj = new_table(cx);
        const value held = value::object(obj);
        ++at; // '{'
        skip();
        if (at < text.size() && text[at] == '}') {
            ++at;
            return held;
        }
        while (at < text.size() && ok) {
            skip();
            if (at >= text.size() || text[at] != '"') {
                ok = false;
                break;
            }
            const std::string key = parse_string();
            skip();
            if (at < text.size() && text[at] == ':') { ++at; }
            obj->set(key, parse());
            skip();
            if (at < text.size() && text[at] == ',') {
                ++at;
                continue;
            }
            break;
        }
        if (at < text.size() && text[at] == '}') { ++at; }
        return held;
    }
};

} // namespace detail

// Install the standard library into a context.
//
// `seed` fixes Math.random. It is DETERMINISTIC by default, and that is a
// deliberate choice rather than an oversight: this engine's test story is
// byte-comparable golden images, and a page that draws with Math.random cannot
// have one otherwise. An application that wants unpredictability passes a real
// seed - the clock, or anything else.
namespace {

// install_builtins was 644 lines: every global the standard library defines,
// in one function. The sections below are exactly the ones its `// --- Math ---`
// banners already marked - each builds one table and defines one global, and
// none of them reads anything the others wrote.

// Math
void install_math(context & cx, std::uint64_t seed) {
    using detail::method;
    using detail::new_table;
    object_object * math = new_table(cx);
    math->set("PI", value::number(3.14159265358979323846));
    math->set("E", value::number(2.71828182845904523536));
    const auto unary = [&](std::string name, double (*fn)(double)) {
        method(cx, math, name,
               [fn](context &, std::span<value> a) { return value::number(fn(num_at(a, 0))); });
    };
    math->set("SQRT2", value::number(1.4142135623730951));
    math->set("SQRT1_2", value::number(0.7071067811865476));
    math->set("LN2", value::number(0.6931471805599453));
    math->set("LN10", value::number(2.302585092994046));
    math->set("LOG2E", value::number(1.4426950408889634));
    math->set("LOG10E", value::number(0.4342944819032518));
    unary("cbrt", [](double x) { return std::cbrt(x); });
    unary("log2", [](double x) { return std::log2(x); });
    unary("log10", [](double x) { return std::log10(x); });
    unary("log1p", [](double x) { return std::log1p(x); });
    unary("expm1", [](double x) { return std::expm1(x); });
    unary("sinh", [](double x) { return std::sinh(x); });
    unary("cosh", [](double x) { return std::cosh(x); });
    unary("tanh", [](double x) { return std::tanh(x); });
    unary("asinh", [](double x) { return std::asinh(x); });
    unary("acosh", [](double x) { return std::acosh(x); });
    unary("atanh", [](double x) { return std::atanh(x); });
    unary("fround", [](double x) { return static_cast<double>(static_cast<float>(x)); });
    method(cx, math, "clz32", [](context &, std::span<value> a) {
        const std::uint32_t x = context::to_uint32(arg_at(a, 0));
        int n = 0;
        for (std::uint32_t bit = 0x80000000u; bit != 0 && (x & bit) == 0; bit >>= 1) { ++n; }
        return value::number(x == 0 ? 32 : n);
    });
    method(cx, math, "imul", [](context &, std::span<value> a) {
        return value::number(static_cast<double>(static_cast<std::int32_t>(
            context::to_uint32(arg_at(a, 0)) * context::to_uint32(arg_at(a, 1)))));
    });
    unary("floor", [](double x) { return std::floor(x); });
    unary("ceil", [](double x) { return std::ceil(x); });
    unary("abs", [](double x) { return std::fabs(x); });
    unary("sqrt", [](double x) { return std::sqrt(x); });
    unary("sin", [](double x) { return std::sin(x); });
    unary("cos", [](double x) { return std::cos(x); });
    unary("tan", [](double x) { return std::tan(x); });
    unary("asin", [](double x) { return std::asin(x); });
    unary("acos", [](double x) { return std::acos(x); });
    unary("atan", [](double x) { return std::atan(x); });
    unary("log", [](double x) { return std::log(x); });
    unary("exp", [](double x) { return std::exp(x); });
    unary("trunc", [](double x) { return std::trunc(x); });
    unary("sign", [](double x) { return x > 0 ? 1.0 : (x < 0 ? -1.0 : x); });
    // JS rounds .5 toward POSITIVE infinity, so Math.round(-0.5) is -0 and not
    // -1. std::round rounds away from zero and gets that wrong.
    method(cx, math, "round", [](context &, std::span<value> a) {
        return value::number(std::floor(num_at(a, 0) + 0.5));
    });
    method(cx, math, "pow", [](context &, std::span<value> a) {
        return value::number(std::pow(num_at(a, 0), num_at(a, 1)));
    });
    method(cx, math, "atan2", [](context &, std::span<value> a) {
        return value::number(std::atan2(num_at(a, 0), num_at(a, 1)));
    });
    method(cx, math, "hypot", [](context &, std::span<value> a) {
        double total = 0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            const double v = context::to_number(a[i]);
            total += v * v;
        }
        return value::number(std::sqrt(total));
    });
    // min/max with no arguments are Infinity and -Infinity, which is what makes
    // `Math.max(...list)` on an empty list behave.
    method(cx, math, "min", [](context &, std::span<value> a) {
        double best = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < a.size(); ++i) {
            best = std::min(best, context::to_number(a[i]));
        }
        return value::number(best);
    });
    method(cx, math, "max", [](context &, std::span<value> a) {
        double best = -std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < a.size(); ++i) {
            best = std::max(best, context::to_number(a[i]));
        }
        return value::number(best);
    });
    // xorshift64*, held in the closure so each context has its own stream.
    auto state = std::make_shared<std::uint64_t>(seed == 0 ? 1 : seed);
    method(cx, math, "random", [state](context &, std::span<value>) {
        std::uint64_t x = *state;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        *state = x;
        const std::uint64_t bits = x * 0x2545F4914F6CDD1DULL;
        return value::number(static_cast<double>(bits >> 11) / 9007199254740992.0);
    });
    cx.define_global("Math", value::object(math));
}

// Array.prototype
void install_array(context & cx) {
    using detail::method;
    using detail::new_table;

    // `Array` itself. 88 uses of isArray in p5.js alone - it is how every
    // overloaded signature in the library decides what it was handed.
    auto * array_ctor = cx.allocate<native_object>("Array", [](context & c, std::span<value> a) {
        value out = c.make_array();
        auto * made = static_cast<array_object *>(out.as_heap());
        // `Array(n)` is a length, `Array(a, b, ...)` is the elements.
        if (a.size() == 1 && a[0].is_number()) {
            made->items.assign(static_cast<std::size_t>(std::max(0.0, a[0].as_number())),
                               value::undefined());
        } else {
            made->items.assign(a.begin(), a.end());
        }
        return out;
    });
    const auto static_method = [&](const char * name, native_fn fn) {
        array_ctor->set(name, value::object(cx.allocate<native_object>(name, std::move(fn))));
    };
    static_method("isArray", [](context &, std::span<value> a) {
        return value::boolean(arg_at(a, 0).is_array());
    });
    static_method("of", [](context & c, std::span<value> a) {
        value out = c.make_array();
        static_cast<array_object *>(out.as_heap())->items.assign(a.begin(), a.end());
        return out;
    });
    static_method("from", [](context & c, std::span<value> a) {
        value out = c.make_array();
        auto * made = static_cast<array_object *>(out.as_heap());
        const value from = arg_at(a, 0);
        if (from.is_array()) {
            made->items = static_cast<array_object *>(from.as_heap())->items;
        } else if (from.is_string()) {
            for (const char ch : static_cast<string_object *>(from.as_heap())->text) {
                made->items.push_back(c.string(std::string{ch}));
            }
        } else if (from.is_object()) {
            // array-LIKE: anything with a length, which is what most callers
            // actually pass (a NodeList, `arguments`, a typed-array shim).
            const double length = context::to_number(c.lookup_property(from, "length"));
            for (double i = 0; i < length; ++i) {
                made->items.push_back(c.lookup_index(from, value::number(i)));
            }
        }
        if (a.size() > 1 && a[1].is_callable()) {
            for (std::size_t i = 0; i < made->items.size(); ++i) {
                const value args[2] = {made->items[i], value::number(static_cast<double>(i))};
                made->items[i] = c.call(a[1], args);
            }
        }
        return out;
    });
    cx.define_global("Array", value::object(array_ctor));

    object_object * array_proto = new_table(cx);
    // The prototype methods p5.js uses that were not here. `at` and `fill` are
    // the ones it leans on hardest - 43 and 31 uses - because a typed-array
    // shim reaches for both.
    method(cx, array_proto, "at", [](context & c, std::span<value> a) {
        auto * self = detail::this_array(c);
        if (self == nullptr) { return value::undefined(); }
        double i = num_at(a, 0);
        if (i < 0) { i += static_cast<double>(self->items.size()); }
        if (i < 0 || i >= static_cast<double>(self->items.size())) { return value::undefined(); }
        return self->items[static_cast<std::size_t>(i)];
    });
    method(cx, array_proto, "fill", [](context & c, std::span<value> a) {
        auto * self = detail::this_array(c);
        if (self == nullptr) { return c.current_this(); }
        const std::size_t n = self->items.size();
        const std::size_t from = a.size() > 1 ? clamp_index(num_at(a, 1), n) : 0;
        const std::size_t to = a.size() > 2 ? clamp_index(num_at(a, 2), n) : n;
        for (std::size_t i = from; i < to; ++i) { self->items[i] = arg_at(a, 0); }
        return c.current_this();
    });
    method(cx, array_proto, "flat", [](context & c, std::span<value> a) {
        // depth defaults to 1, which is what every use in p5 wants
        const double depth = a.empty() ? 1.0 : num_at(a, 0);
        auto * self = detail::this_array(c);
        value out = c.make_array();
        if (self == nullptr) { return out; }
        auto * result = static_cast<array_object *>(out.as_heap());
        // An explicit worklist rather than recursion: `flat(Infinity)` on a
        // deep structure must not be bounded by the C++ stack.
        std::vector<std::pair<value, double>> pending;
        for (std::size_t i = self->items.size(); i-- > 0;) {
            pending.emplace_back(self->items[i], depth);
        }
        while (!pending.empty()) {
            const auto [item, left] = pending.back();
            pending.pop_back();
            if (item.is_array() && left > 0) {
                const auto & inner = static_cast<array_object *>(item.as_heap())->items;
                for (std::size_t i = inner.size(); i-- > 0;) {
                    pending.emplace_back(inner[i], left - 1);
                }
            } else {
                result->items.push_back(item);
            }
        }
        return out;
    });
    method(cx, array_proto, "flatMap", [](context & c, std::span<value> a) {
        auto * self = detail::this_array(c);
        value out = c.make_array();
        if (self == nullptr || a.empty() || !a[0].is_callable()) { return out; }
        auto * result = static_cast<array_object *>(out.as_heap());
        const std::size_t n = self->items.size();
        for (std::size_t i = 0; i < n && i < self->items.size(); ++i) {
            const value args[3] = {self->items[i], value::number(static_cast<double>(i)),
                                   c.current_this()};
            const value mapped = c.call(a[0], args);
            if (mapped.is_array()) {
                for (const value & item : static_cast<array_object *>(mapped.as_heap())->items) {
                    result->items.push_back(item);
                }
            } else {
                result->items.push_back(mapped);
            }
        }
        return out;
    });
    method(cx, array_proto, "findLast", [](context & c, std::span<value> a) {
        auto * self = detail::this_array(c);
        if (self == nullptr || a.empty() || !a[0].is_callable()) { return value::undefined(); }
        for (std::size_t i = self->items.size(); i-- > 0;) {
            const value args[3] = {self->items[i], value::number(static_cast<double>(i)),
                                   c.current_this()};
            if (context::truthy(c.call(a[0], args))) { return self->items[i]; }
        }
        return value::undefined();
    });
    method(cx, array_proto, "findLastIndex", [](context & c, std::span<value> a) {
        auto * self = detail::this_array(c);
        if (self == nullptr || a.empty() || !a[0].is_callable()) { return value::number(-1); }
        for (std::size_t i = self->items.size(); i-- > 0;) {
            const value args[3] = {self->items[i], value::number(static_cast<double>(i)),
                                   c.current_this()};
            if (context::truthy(c.call(a[0], args))) {
                return value::number(static_cast<double>(i));
            }
        }
        return value::number(-1);
    });
    method(cx, array_proto, "push", [](context & c, std::span<value> a) {
        array_object * self = detail::this_array(c);
        if (self == nullptr) { return value::number(0); }
        for (std::size_t i = 0; i < a.size(); ++i) { self->items.push_back(a[i]); }
        return value::number(static_cast<double>(self->items.size()));
    });
    method(cx, array_proto, "pop", [](context & c, std::span<value>) {
        array_object * self = detail::this_array(c);
        if (self == nullptr || self->items.empty()) { return value::undefined(); }
        const value out = self->items.back();
        self->items.pop_back();
        return out;
    });
    method(cx, array_proto, "shift", [](context & c, std::span<value>) {
        array_object * self = detail::this_array(c);
        if (self == nullptr || self->items.empty()) { return value::undefined(); }
        const value out = self->items.front();
        self->items.erase(self->items.begin());
        return out;
    });
    method(cx, array_proto, "unshift", [](context & c, std::span<value> a) {
        array_object * self = detail::this_array(c);
        if (self == nullptr) { return value::number(0); }
        self->items.insert(self->items.begin(), a.begin(), a.end());
        return value::number(static_cast<double>(self->items.size()));
    });
    method(cx, array_proto, "slice", [](context & c, std::span<value> a) {
        array_object * self = detail::this_array(c);
        value out = c.make_array();
        if (self == nullptr) { return out; }
        const std::size_t n = self->items.size();
        const std::size_t from = clamp_index(a.empty() ? 0 : num_at(a, 0), n);
        const std::size_t to = a.size() > 1 ? clamp_index(num_at(a, 1), n) : n;
        auto * result = static_cast<array_object *>(out.as_heap());
        for (std::size_t i = from; i < to; ++i) { result->items.push_back(self->items[i]); }
        return out;
    });
    method(cx, array_proto, "splice", [](context & c, std::span<value> a) {
        array_object * self = detail::this_array(c);
        value removed = c.make_array();
        if (self == nullptr) { return removed; }
        const std::size_t n = self->items.size();
        const std::size_t from = clamp_index(num_at(a, 0), n);
        const std::size_t count =
            a.size() > 1 ? std::min(n - from, static_cast<std::size_t>(std::max(0.0, num_at(a, 1))))
                         : n - from;
        auto * out = static_cast<array_object *>(removed.as_heap());
        for (std::size_t i = 0; i < count; ++i) { out->items.push_back(self->items[from + i]); }
        self->items.erase(self->items.begin() + static_cast<std::ptrdiff_t>(from),
                          self->items.begin() + static_cast<std::ptrdiff_t>(from + count));
        if (a.size() > 2) {
            self->items.insert(self->items.begin() + static_cast<std::ptrdiff_t>(from),
                               a.begin() + 2, a.end());
        }
        return removed;
    });
    method(cx, array_proto, "indexOf", [](context & c, std::span<value> a) {
        array_object * self = detail::this_array(c);
        if (self == nullptr) { return value::number(-1); }
        for (std::size_t i = 0; i < self->items.size(); ++i) {
            if (self->items[i].strict_equals(arg_at(a, 0))) {
                return value::number(static_cast<double>(i));
            }
        }
        return value::number(-1);
    });
    method(cx, array_proto, "includes", [](context & c, std::span<value> a) {
        array_object * self = detail::this_array(c);
        if (self == nullptr) { return value::boolean(false); }
        for (const value & item : self->items) {
            if (item.strict_equals(arg_at(a, 0))) { return value::boolean(true); }
        }
        return value::boolean(false);
    });
    method(cx, array_proto, "join", [](context & c, std::span<value> a) {
        array_object * self = detail::this_array(c);
        if (self == nullptr) { return c.string(std::string{}); }
        const std::string sep = a.empty() ? "," : c.to_string(a[0]);
        std::string out;
        for (std::size_t i = 0; i < self->items.size(); ++i) {
            if (i > 0) { out += sep; }
            // null and undefined join as EMPTY, not as "null"/"undefined".
            if (!self->items[i].is_undefined() && !self->items[i].is_null()) {
                out += c.to_string(self->items[i]);
            }
        }
        return c.string(out);
    });
    method(cx, array_proto, "concat", [](context & c, std::span<value> a) {
        array_object * self = detail::this_array(c);
        value out = c.make_array();
        auto * result = static_cast<array_object *>(out.as_heap());
        if (self != nullptr) { result->items = self->items; }
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i].is_array()) {
                const auto & other = static_cast<array_object *>(a[i].as_heap())->items;
                result->items.insert(result->items.end(), other.begin(), other.end());
            } else {
                result->items.push_back(a[i]);
            }
        }
        return out;
    });
    method(cx, array_proto, "reverse", [](context & c, std::span<value>) {
        array_object * self = detail::this_array(c);
        if (self != nullptr) { std::ranges::reverse(self->items); }
        return c.current_this();
    });
    // The iteration methods call back INTO the VM, which is what
    // context::call() exists for. Each snapshots the item first, because the
    // callback may mutate the array underneath it.
    const auto each = [](context & c, std::span<value> a, auto && body) {
        array_object * self = detail::this_array(c);
        if (self == nullptr) { return; }
        const value callback = arg_at(a, 0);
        for (std::size_t i = 0; i < self->items.size(); ++i) {
            const value call_args[3] = {self->items[i], value::number(static_cast<double>(i)),
                                        c.current_this()};
            if (!body(i, c.call(callback, call_args))) { return; }
        }
    };
    method(cx, array_proto, "forEach", [each](context & c, std::span<value> a) {
        each(c, a, [](std::size_t, value) { return true; });
        return value::undefined();
    });
    method(cx, array_proto, "map", [each](context & c, std::span<value> a) {
        value out = c.make_array();
        auto * result = static_cast<array_object *>(out.as_heap());
        each(c, a, [&](std::size_t, value produced) {
            result->items.push_back(produced);
            return true;
        });
        return out;
    });
    method(cx, array_proto, "filter", [](context & c, std::span<value> a) {
        array_object * self = detail::this_array(c);
        value out = c.make_array();
        if (self == nullptr) { return out; }
        auto * result = static_cast<array_object *>(out.as_heap());
        const value callback = arg_at(a, 0);
        for (std::size_t i = 0; i < self->items.size(); ++i) {
            const value item = self->items[i];
            const value call_args[3] = {item, value::number(static_cast<double>(i)),
                                        c.current_this()};
            if (context::truthy(c.call(callback, call_args))) { result->items.push_back(item); }
        }
        return out;
    });
    method(cx, array_proto, "find", [](context & c, std::span<value> a) {
        array_object * self = detail::this_array(c);
        if (self == nullptr) { return value::undefined(); }
        const value callback = arg_at(a, 0);
        for (std::size_t i = 0; i < self->items.size(); ++i) {
            const value item = self->items[i];
            const value call_args[3] = {item, value::number(static_cast<double>(i)),
                                        c.current_this()};
            if (context::truthy(c.call(callback, call_args))) { return item; }
        }
        return value::undefined();
    });
    method(cx, array_proto, "findIndex", [](context & c, std::span<value> a) {
        array_object * self = detail::this_array(c);
        if (self == nullptr) { return value::number(-1); }
        const value callback = arg_at(a, 0);
        for (std::size_t i = 0; i < self->items.size(); ++i) {
            const value call_args[3] = {self->items[i], value::number(static_cast<double>(i)),
                                        c.current_this()};
            if (context::truthy(c.call(callback, call_args))) {
                return value::number(static_cast<double>(i));
            }
        }
        return value::number(-1);
    });
    method(cx, array_proto, "some", [](context & c, std::span<value> a) {
        array_object * self = detail::this_array(c);
        if (self == nullptr) { return value::boolean(false); }
        const value callback = arg_at(a, 0);
        for (std::size_t i = 0; i < self->items.size(); ++i) {
            const value call_args[3] = {self->items[i], value::number(static_cast<double>(i)),
                                        c.current_this()};
            if (context::truthy(c.call(callback, call_args))) { return value::boolean(true); }
        }
        return value::boolean(false);
    });
    method(cx, array_proto, "every", [](context & c, std::span<value> a) {
        array_object * self = detail::this_array(c);
        if (self == nullptr) { return value::boolean(true); }
        const value callback = arg_at(a, 0);
        for (std::size_t i = 0; i < self->items.size(); ++i) {
            const value call_args[3] = {self->items[i], value::number(static_cast<double>(i)),
                                        c.current_this()};
            if (!context::truthy(c.call(callback, call_args))) { return value::boolean(false); }
        }
        return value::boolean(true);
    });
    method(cx, array_proto, "reduce", [](context & c, std::span<value> a) {
        array_object * self = detail::this_array(c);
        if (self == nullptr) { return value::undefined(); }
        const value callback = arg_at(a, 0);
        std::size_t i = 0;
        value total;
        if (a.size() > 1) {
            total = a[1];
        } else {
            if (self->items.empty()) { return value::undefined(); }
            total = self->items[0];
            i = 1;
        }
        for (; i < self->items.size(); ++i) {
            const value call_args[4] = {total, self->items[i],
                                        value::number(static_cast<double>(i)), c.current_this()};
            total = c.call(callback, call_args);
        }
        return total;
    });
    method(cx, array_proto, "sort", [](context & c, std::span<value> a) {
        array_object * self = detail::this_array(c);
        if (self == nullptr) { return c.current_this(); }
        const value comparator = arg_at(a, 0);
        if (comparator.is_callable()) {
            // std::sort would be UB with an inconsistent comparator, and a
            // comparator written in JS can be anything at all. A stable
            // insertion sort cannot be talked into reading out of bounds.
            for (std::size_t i = 1; i < self->items.size(); ++i) {
                value item = self->items[i];
                std::size_t j = i;
                while (j > 0) {
                    const value pair[2] = {self->items[j - 1], item};
                    if (context::to_number(c.call(comparator, pair)) <= 0) { break; }
                    self->items[j] = self->items[j - 1];
                    --j;
                }
                self->items[j] = item;
            }
        } else {
            // The default really is lexicographic on the STRING form, which is
            // why [10, 9].sort() is [10, 9].
            std::ranges::stable_sort(self->items, [&](const value & x, const value & y) {
                return c.to_string(x) < c.to_string(y);
            });
        }
        return c.current_this();
    });
    array_ctor->set("prototype", value::object(array_proto));
    cx.set_prototype(context::proto_kind::array, array_proto);
}

// String.prototype
void install_string(context & cx) {
    using detail::method;
    using detail::new_table;
    object_object * string_proto = new_table(cx);
    method(cx, string_proto, "charAt", [](context & c, std::span<value> a) {
        const std::string s = detail::this_string(c);
        const auto i = static_cast<std::size_t>(std::max(0.0, num_at(a, 0)));
        return c.string(i < s.size() ? std::string{s[i]} : std::string{});
    });
    // `at` is charAt that counts from the END for a negative index, which is
    // the whole reason to reach for it - `s.at(-1)` is the last character.
    // Arrays had it and strings did not, and the two are meant to match.
    method(cx, string_proto, "at", [](context & c, std::span<value> a) {
        const std::string s = detail::this_string(c);
        double i = num_at(a, 0);
        if (i < 0) { i += static_cast<double>(s.size()); }
        if (i < 0 || i >= static_cast<double>(s.size())) { return value::undefined(); }
        return c.string(std::string{s[static_cast<std::size_t>(i)]});
    });
    method(cx, string_proto, "charCodeAt", [](context & c, std::span<value> a) {
        const std::string s = detail::this_string(c);
        const auto i = static_cast<std::size_t>(std::max(0.0, num_at(a, 0)));
        if (i >= s.size()) { return value::number(std::nan("")); }
        return value::number(static_cast<double>(static_cast<unsigned char>(s[i])));
    });
    method(cx, string_proto, "indexOf", [](context & c, std::span<value> a) {
        const std::string s = detail::this_string(c);
        const std::size_t found = s.find(str_at(c, a, 0));
        return value::number(found == std::string::npos ? -1 : static_cast<double>(found));
    });
    method(cx, string_proto, "lastIndexOf", [](context & c, std::span<value> a) {
        const std::string s = detail::this_string(c);
        const std::size_t found = s.rfind(str_at(c, a, 0));
        return value::number(found == std::string::npos ? -1 : static_cast<double>(found));
    });
    method(cx, string_proto, "includes", [](context & c, std::span<value> a) {
        return value::boolean(detail::this_string(c).find(str_at(c, a, 0)) != std::string::npos);
    });
    method(cx, string_proto, "startsWith", [](context & c, std::span<value> a) {
        return value::boolean(detail::this_string(c).starts_with(str_at(c, a, 0)));
    });
    method(cx, string_proto, "endsWith", [](context & c, std::span<value> a) {
        return value::boolean(detail::this_string(c).ends_with(str_at(c, a, 0)));
    });
    method(cx, string_proto, "slice", [](context & c, std::span<value> a) {
        const std::string s = detail::this_string(c);
        const std::size_t from = clamp_index(a.empty() ? 0 : num_at(a, 0), s.size());
        const std::size_t to = a.size() > 1 ? clamp_index(num_at(a, 1), s.size()) : s.size();
        return c.string(to > from ? s.substr(from, to - from) : std::string{});
    });
    method(cx, string_proto, "substring", [](context & c, std::span<value> a) {
        const std::string s = detail::this_string(c);
        // substring CLAMPS negatives to 0 and swaps its arguments if they are
        // backwards, which is the whole difference from slice.
        std::size_t from = static_cast<std::size_t>(std::max(0.0, num_at(a, 0)));
        std::size_t to =
            a.size() > 1 ? static_cast<std::size_t>(std::max(0.0, num_at(a, 1))) : s.size();
        from = std::min(from, s.size());
        to = std::min(to, s.size());
        if (from > to) { std::swap(from, to); }
        return c.string(s.substr(from, to - from));
    });
    method(cx, string_proto, "split", [](context & c, std::span<value> a) {
        const std::string s = detail::this_string(c);
        value out = c.make_array();
        auto * result = static_cast<array_object *>(out.as_heap());
        if (a.empty() || a[0].is_undefined()) {
            result->items.push_back(c.string(s));
            return out;
        }
        const std::string sep = c.to_string(a[0]);
        if (sep.empty()) {
            for (const char ch : s) { result->items.push_back(c.string(std::string{ch})); }
            return out;
        }
        std::size_t at = 0;
        while (true) {
            const std::size_t found = s.find(sep, at);
            if (found == std::string::npos) {
                result->items.push_back(c.string(s.substr(at)));
                break;
            }
            result->items.push_back(c.string(s.substr(at, found - at)));
            at = found + sep.size();
        }
        return out;
    });
    method(cx, string_proto, "replace", [](context & c, std::span<value> a) {
        std::string s = detail::this_string(c);
        const std::string from = str_at(c, a, 0);
        const std::string to = str_at(c, a, 1);
        if (!from.empty()) {
            const std::size_t found = s.find(from);
            if (found != std::string::npos) { s.replace(found, from.size(), to); }
        }
        return c.string(s);
    });
    method(cx, string_proto, "replaceAll", [](context & c, std::span<value> a) {
        std::string s = detail::this_string(c);
        const std::string from = str_at(c, a, 0);
        const std::string to = str_at(c, a, 1);
        if (from.empty()) { return c.string(s); }
        std::string out;
        std::size_t at = 0;
        while (true) {
            const std::size_t found = s.find(from, at);
            if (found == std::string::npos) {
                out += s.substr(at);
                break;
            }
            out += s.substr(at, found - at);
            out += to;
            at = found + from.size();
        }
        return c.string(out);
    });
    method(cx, string_proto, "toUpperCase", [](context & c, std::span<value>) {
        std::string s = detail::this_string(c);
        for (char & ch : s) {
            if (ch >= 'a' && ch <= 'z') { ch = static_cast<char>(ch - 'a' + 'A'); }
        }
        return c.string(s);
    });
    method(cx, string_proto, "toLowerCase", [](context & c, std::span<value>) {
        std::string s = detail::this_string(c);
        for (char & ch : s) {
            if (ch >= 'A' && ch <= 'Z') { ch = static_cast<char>(ch - 'A' + 'a'); }
        }
        return c.string(s);
    });
    method(cx, string_proto, "trim", [](context & c, std::span<value>) {
        const std::string s = detail::this_string(c);
        const std::size_t from = s.find_first_not_of(" \t\n\r\f\v");
        if (from == std::string::npos) { return c.string(std::string{}); }
        return c.string(s.substr(from, s.find_last_not_of(" \t\n\r\f\v") - from + 1));
    });
    method(cx, string_proto, "repeat", [](context & c, std::span<value> a) {
        const std::string s = detail::this_string(c);
        const double raw = num_at(a, 0);
        // A huge count is a page bug, and allocating for it is a hang. Cap it.
        const auto count = static_cast<std::size_t>(std::clamp(raw, 0.0, 1000000.0));
        std::string out;
        out.reserve(s.size() * count);
        for (std::size_t i = 0; i < count; ++i) { out += s; }
        return c.string(out);
    });
    method(cx, string_proto, "padStart", [](context & c, std::span<value> a) {
        std::string s = detail::this_string(c);
        const auto want = static_cast<std::size_t>(std::clamp(num_at(a, 0), 0.0, 1000000.0));
        const std::string pad = a.size() > 1 ? c.to_string(a[1]) : " ";
        if (pad.empty()) { return c.string(s); }
        std::string prefix;
        while (prefix.size() + s.size() < want) { prefix += pad; }
        prefix.resize(want > s.size() ? want - s.size() : 0);
        return c.string(prefix + s);
    });
    method(cx, string_proto, "concat", [](context & c, std::span<value> a) {
        std::string s = detail::this_string(c);
        for (std::size_t i = 0; i < a.size(); ++i) { s += c.to_string(a[i]); }
        return c.string(s);
    });
    cx.set_prototype(context::proto_kind::string, string_proto);
}

// Number.prototype
void install_number(context & cx) {
    using detail::method;
    using detail::new_table;

    // `Number` as a namespace as well as a coercion. It was only the latter,
    // so every `Number.isFinite(x)` guard in a page read undefined and called
    // it - the failure landing well away from the test that caused it.
    auto * number_ctor = cx.allocate<native_object>("Number", [](context &, std::span<value> a) {
        return value::number(a.empty() ? 0.0 : context::to_number(a[0]));
    });
    const auto constant = [&](const char * name, double v) {
        number_ctor->set(name, value::number(v));
    };
    constant("EPSILON", 2.220446049250313e-16);
    constant("MAX_SAFE_INTEGER", 9007199254740991.0);
    constant("MIN_SAFE_INTEGER", -9007199254740991.0);
    constant("MAX_VALUE", 1.7976931348623157e308);
    constant("MIN_VALUE", 5e-324);
    constant("POSITIVE_INFINITY", std::numeric_limits<double>::infinity());
    constant("NEGATIVE_INFINITY", -std::numeric_limits<double>::infinity());
    constant("NaN", std::nan(""));
    const auto predicate = [&](const char * name, bool (*fn)(const value &)) {
        number_ctor->set(name, value::object(cx.allocate<native_object>(
                                   name, [fn](context &, std::span<value> a) {
                                       const value v = arg_at(a, 0);
                                       return value::boolean(fn(v));
                                   })));
    };
    // These do NOT coerce - `Number.isFinite("1")` is false where the global
    // `isFinite("1")` is true, and code uses the difference deliberately.
    predicate("isFinite",
              [](const value & v) { return v.is_number() && std::isfinite(v.as_number()); });
    predicate("isNaN", [](const value & v) { return v.is_number() && std::isnan(v.as_number()); });
    predicate("isInteger", [](const value & v) {
        return v.is_number() && std::isfinite(v.as_number()) &&
               v.as_number() == std::trunc(v.as_number());
    });
    predicate("isSafeInteger", [](const value & v) {
        return v.is_number() && std::isfinite(v.as_number()) &&
               v.as_number() == std::trunc(v.as_number()) &&
               std::abs(v.as_number()) <= 9007199254740991.0;
    });
    cx.define_global("Number", value::object(number_ctor));

    object_object * number_proto = new_table(cx);
    method(cx, number_proto, "toFixed", [](context & c, std::span<value> a) {
        const double self = context::to_number(c.current_this());
        const auto digits = static_cast<int>(std::clamp(num_at(a, 0), 0.0, 20.0));
        std::string out(64, '\0');
        const int written = std::snprintf(out.data(), out.size(), "%.*f", digits, self);
        out.resize(written > 0 ? static_cast<std::size_t>(written) : 0);
        return c.string(out);
    });
    // `toString(radix)` HONOURS ITS RADIX. Ignoring it is not a small gap:
    // `n.toString(16)` is how essentially every program turns a colour channel
    // into hex, and dropping the argument returned the DECIMAL digits - so
    // `'#' + (220).toString(16)` came out as "#220" rather than "#dc". That is
    // a string a colour parser can neither reject nor read correctly, which is
    // how p5.js ended up filling a sketch's background with white.
    method(cx, number_proto, "toString", [](context & c, std::span<value> a) {
        const double v = context::to_number(c.current_this());
        const int radix = a.empty() ? 10 : static_cast<int>(context::to_number(a[0]));
        if (radix == 10 || radix < 2 || radix > 36 || std::isnan(v) || std::isinf(v)) {
            return c.string(c.to_string(c.current_this()));
        }
        const bool negative = v < 0;
        double magnitude = std::fabs(v);
        constexpr std::string_view digits = "0123456789abcdefghijklmnopqrstuvwxyz";
        double whole = std::floor(magnitude);
        std::string out;
        if (whole == 0) {
            out = "0";
        } else {
            while (whole >= 1) {
                const auto digit =
                    static_cast<std::size_t>(std::fmod(whole, static_cast<double>(radix)));
                out.insert(out.begin(), digits[digit]);
                whole = std::floor(whole / radix);
            }
        }
        // The fraction, to as many places as a double can distinguish. A
        // fixed count would print 0.1 in binary as 0.0999... or drop it.
        double fraction = magnitude - std::floor(magnitude);
        if (fraction > 0) {
            out += '.';
            for (int place = 0; place < 52 && fraction > 0; ++place) {
                fraction *= radix;
                const auto digit = static_cast<std::size_t>(std::floor(fraction));
                out += digits[std::min<std::size_t>(digit, 35)];
                fraction -= std::floor(fraction);
            }
        }
        return c.string(negative ? "-" + out : out);
    });
    number_ctor->set("prototype", value::object(number_proto));
    cx.set_prototype(context::proto_kind::number, number_proto);
}

// Object
// One `Object.defineProperty`, used by both it and defineProperties.
//
// Two things it has to get right, and both were wrong:
//
// A descriptor with NO `value`, `get` or `set` describes ATTRIBUTES ONLY, and
// must leave the existing value alone. Writing undefined instead is how
// `Object.defineProperty(C, "prototype", {writable: false})` - which is what
// every Babel-transpiled class emits - wiped the prototype it had just filled
// in, and the class's methods vanished with it.
//
// And a FUNCTION IS AN OBJECT. Babel defines onto the constructor as well as
// onto its prototype, and `is_object()` is false for a closure, so half of
// every transpiled class was silently dropped.
void define_one(context & cx, value target, const std::string & key, object_object * descriptor) {
    value * getter = descriptor->find("get");
    value * setter = descriptor->find("set");
    value * held = descriptor->find("value");
    const bool describes_a_value = getter != nullptr || setter != nullptr || held != nullptr;

    if (target.is_object()) {
        auto * obj = static_cast<object_object *>(target.as_heap());
        if (getter != nullptr || setter != nullptr) {
            obj->define_accessor(key, getter == nullptr ? value::undefined() : *getter,
                                 setter == nullptr ? value::undefined() : *setter);
        } else if (held != nullptr) {
            obj->erase_accessor(key);
            obj->set(key, *held);
        }
        return;
    }
    if (target.is_kind(heap_kind::function)) {
        auto * closure = static_cast<closure_object *>(target.as_heap());
        if (getter != nullptr || setter != nullptr) {
            closure->define_accessor(key, getter == nullptr ? value::undefined() : *getter,
                                     setter == nullptr ? value::undefined() : *setter);
        } else if (held != nullptr) {
            closure->set(key, *held);
        }
        return;
    }
    if (target.is_kind(heap_kind::native) && held != nullptr) {
        static_cast<native_object *>(target.as_heap())->set(key, *held);
    }
    (void)cx;
    (void)describes_a_value;
}

void install_object(context & cx) {
    using detail::method;
    using detail::new_table;

    // `Object.prototype`. There was NO table for it, so every object in the
    // engine was missing hasOwnProperty, toString and valueOf - and
    // `hasOwnProperty` in particular is how half the library code in existence
    // asks whether a key is really there rather than inherited.
    object_object * object_proto = new_table(cx);
    method(cx, object_proto, "hasOwnProperty", [](context & c, std::span<value> a) {
        const value self = c.current_this();
        const std::string key = str_at(c, a, 0);
        if (self.is_object()) {
            auto * obj = static_cast<object_object *>(self.as_heap());
            return value::boolean(obj->find(key) != nullptr || obj->find_accessor(key) != nullptr);
        }
        if (self.is_array()) {
            auto * arr = static_cast<array_object *>(self.as_heap());
            if (key == "length") { return value::boolean(true); }
            char * end = nullptr;
            const double at = std::strtod(key.c_str(), &end);
            return value::boolean(end != nullptr && *end == '\0' && at >= 0 &&
                                  at < static_cast<double>(arr->items.size()));
        }
        if (self.is_kind(heap_kind::function)) {
            return value::boolean(static_cast<closure_object *>(self.as_heap())->find(key) !=
                                  nullptr);
        }
        return value::boolean(false);
    });
    method(cx, object_proto, "toString",
           [](context & c, std::span<value>) { return c.string("[object Object]"); });
    method(cx, object_proto, "valueOf",
           [](context & c, std::span<value>) { return c.current_this(); });
    method(cx, object_proto, "isPrototypeOf", [](context & c, std::span<value> a) {
        const value self = c.current_this();
        value walk = arg_at(a, 0);
        for (int depth = 0; depth < 64 && walk.is_object(); ++depth) {
            walk = static_cast<object_object *>(walk.as_heap())->prototype;
            if (walk.is_heap() && self.is_heap() && walk.as_heap() == self.as_heap()) {
                return value::boolean(true);
            }
        }
        return value::boolean(false);
    });
    method(cx, object_proto, "propertyIsEnumerable", [](context & c, std::span<value> a) {
        const value self = c.current_this();
        if (!self.is_object()) { return value::boolean(false); }
        return value::boolean(static_cast<object_object *>(self.as_heap())->find(str_at(c, a, 0)) !=
                              nullptr);
    });
    cx.set_prototype(context::proto_kind::object, object_proto);

    object_object * object_ctor = new_table(cx);
    // `Object.prototype` REACHABLE FROM SCRIPT, not just consulted by lookup.
    //
    // The tables existed and property lookup fell back to them, but nothing
    // exposed them - so `Object.prototype` was undefined, and the very common
    // `var hasOwnProperty = Object.prototype.hasOwnProperty` read undefined and
    // called it. acorn opens with exactly that, which is where p5's error
    // system stopped.
    //
    // It is the same object lookup uses, so a page that adds to it is seen by
    // every object, which is what a page doing that expects.
    object_ctor->set("prototype", value::object(object_proto));
    method(cx, object_ctor, "hasOwn", [](context & c, std::span<value> a) {
        const value target = arg_at(a, 0);
        const std::string key = str_at(c, a, 1);
        if (target.is_object()) {
            auto * obj = static_cast<object_object *>(target.as_heap());
            return value::boolean(obj->find(key) != nullptr || obj->find_accessor(key) != nullptr);
        }
        return value::boolean(false);
    });

    // `Object.defineProperty(o, key, descriptor)` - 51 uses in p5.js, and the
    // reason the object model grew accessors at all. A descriptor is either
    // data (`value`) or accessor (`get`/`set`); the two are the same property
    // described two ways, so defining one removes the other.
    method(cx, object_ctor, "defineProperty", [](context & c, std::span<value> a) {
        if (!arg_at(a, 2).is_object()) { return arg_at(a, 0); }
        auto * descriptor = static_cast<object_object *>(a[2].as_heap());
        define_one(c, arg_at(a, 0), c.to_string(arg_at(a, 1)), descriptor);
        return arg_at(a, 0);
    });
    method(cx, object_ctor, "defineProperties", [](context & c, std::span<value> a) {
        if (!arg_at(a, 1).is_object()) { return arg_at(a, 0); }
        for (const auto & [key, descriptor] : static_cast<object_object *>(a[1].as_heap())->props) {
            if (!descriptor.is_object()) { continue; }
            define_one(c, arg_at(a, 0), key, static_cast<object_object *>(descriptor.as_heap()));
        }
        return arg_at(a, 0);
    });
    method(cx, object_ctor, "getOwnPropertyDescriptor", [](context & c, std::span<value> a) {
        if (!arg_at(a, 0).is_object()) { return value::undefined(); }
        auto * target = static_cast<object_object *>(a[0].as_heap());
        const std::string key = c.to_string(arg_at(a, 1));
        object_object * out = new_table(c);
        if (accessor_entry * entry = target->find_accessor(key)) {
            out->set("get", entry->getter);
            out->set("set", entry->setter);
        } else if (value * held = target->find(key)) {
            out->set("value", *held);
            out->set("writable", value::boolean(true));
        } else {
            return value::undefined();
        }
        out->set("enumerable", value::boolean(true));
        out->set("configurable", value::boolean(true));
        return value::object(out);
    });
    // `Object.create(proto)` and the two prototype accessors. A real chain has
    // existed since `extends`; what was missing was any way for a page to reach
    // it. p5.js uses create 19 times.
    method(cx, object_ctor, "create", [](context & c, std::span<value> a) {
        object_object * out = new_table(c);
        if (arg_at(a, 0).is_object()) { out->prototype = a[0]; }
        return value::object(out);
    });
    // A FUNCTION HAS A [[Prototype]] TOO, and it is not its `prototype`
    // property. Babel's `_inherits` sets both - the subclass's prototype
    // property for instance methods, the subclass FUNCTION for static ones -
    // and answering null for a function broke every transpiled `extends`.
    method(cx, object_ctor, "getPrototypeOf", [](context &, std::span<value> a) {
        const value of = arg_at(a, 0);
        if (of.is_object()) { return static_cast<object_object *>(of.as_heap())->prototype; }
        if (of.is_kind(heap_kind::function)) {
            return static_cast<closure_object *>(of.as_heap())->proto_link;
        }
        return value::null();
    });
    method(cx, object_ctor, "setPrototypeOf", [](context &, std::span<value> a) {
        const value of = arg_at(a, 0);
        if (of.is_object()) {
            static_cast<object_object *>(of.as_heap())->prototype = arg_at(a, 1);
        } else if (of.is_kind(heap_kind::function)) {
            static_cast<closure_object *>(of.as_heap())->proto_link = arg_at(a, 1);
        }
        return of;
    });
    method(cx, object_ctor, "getOwnPropertyNames", [](context & c, std::span<value> a) {
        value out = c.make_array();
        auto * result = static_cast<array_object *>(out.as_heap());
        if (arg_at(a, 0).is_object()) {
            static_cast<object_object *>(a[0].as_heap())->each_own_key([&](const std::string & k) {
                result->items.push_back(c.string(k));
            });
        }
        return out;
    });
    method(cx, object_ctor, "getOwnPropertyDescriptors", [](context & c, std::span<value> a) {
        object_object * out = new_table(c);
        if (arg_at(a, 0).is_object()) {
            auto * from = static_cast<object_object *>(a[0].as_heap());
            from->each_own_key([&](const std::string & key) {
                object_object * d = new_table(c);
                if (accessor_entry * entry = from->find_accessor(key)) {
                    d->set("get", entry->getter);
                    d->set("set", entry->setter);
                } else if (value * held = from->find(key)) {
                    d->set("value", *held);
                    d->set("writable", value::boolean(true));
                }
                d->set("enumerable", value::boolean(true));
                d->set("configurable", value::boolean(true));
                out->set(key, value::object(d));
            });
        }
        return value::object(out);
    });
    method(cx, object_ctor, "fromEntries", [](context & c, std::span<value> a) {
        object_object * out = new_table(c);
        if (arg_at(a, 0).is_array()) {
            for (const value & pair : static_cast<array_object *>(a[0].as_heap())->items) {
                if (!pair.is_array()) { continue; }
                const auto & items = static_cast<array_object *>(pair.as_heap())->items;
                if (items.size() >= 2) { out->set(c.to_string(items[0]), items[1]); }
            }
        }
        return value::object(out);
    });
    // Not modelled: this engine has no writability, so a frozen object is not
    // actually protected. Returning the object keeps the idiom working; saying
    // so here is better than a page believing it did something.
    method(cx, object_ctor, "freeze", [](context &, std::span<value> a) { return arg_at(a, 0); });
    method(cx, object_ctor, "isFrozen",
           [](context &, std::span<value>) { return value::boolean(false); });
    method(cx, object_ctor, "keys", [](context & c, std::span<value> a) {
        value out = c.make_array();
        auto * result = static_cast<array_object *>(out.as_heap());
        if (arg_at(a, 0).is_object()) {
            // An accessor IS a property, and definition order is observable.
            static_cast<object_object *>(a[0].as_heap())->each_own_key([&](const std::string & k) {
                result->items.push_back(c.string(k));
            });
        }
        return out;
    });
    method(cx, object_ctor, "values", [](context & c, std::span<value> a) {
        value out = c.make_array();
        auto * result = static_cast<array_object *>(out.as_heap());
        if (arg_at(a, 0).is_object()) {
            for (const auto & [key, item] : static_cast<object_object *>(a[0].as_heap())->props) {
                result->items.push_back(item);
            }
        }
        return out;
    });
    method(cx, object_ctor, "entries", [](context & c, std::span<value> a) {
        value out = c.make_array();
        auto * result = static_cast<array_object *>(out.as_heap());
        if (arg_at(a, 0).is_object()) {
            for (const auto & [key, item] : static_cast<object_object *>(a[0].as_heap())->props) {
                value pair = c.make_array();
                auto * entry = static_cast<array_object *>(pair.as_heap());
                entry->items.push_back(c.string(key));
                entry->items.push_back(item);
                result->items.push_back(pair);
            }
        }
        return out;
    });
    method(cx, object_ctor, "assign", [](context & c, std::span<value> a) {
        const value target = arg_at(a, 0);
        if (!target.is_object()) { return target; }
        auto * into = static_cast<object_object *>(target.as_heap());
        for (std::size_t i = 1; i < a.size(); ++i) {
            if (!a[i].is_object()) { continue; }
            for (const auto & [key, item] : static_cast<object_object *>(a[i].as_heap())->props) {
                into->set(key, item);
            }
        }
        (void)c;
        return target;
    });
    cx.define_global("Object", value::object(object_ctor));
}

// JSON
void install_json(context & cx) {
    using detail::method;
    using detail::new_table;
    object_object * json = new_table(cx);
    method(cx, json, "stringify", [](context & c, std::span<value> a) {
        std::string out;
        detail::write_json(c, arg_at(a, 0), out);
        return c.string(out);
    });
    method(cx, json, "parse", [](context & c, std::span<value> a) {
        // The source is held in a NAMED local: json_reader keeps a string_view
        // into it, and passing the temporary directly leaves the view dangling
        // for the whole parse.
        const std::string source = str_at(c, a, 0);
        detail::json_reader reader{c, source, 0, true};
        const value out = reader.parse();
        return reader.ok ? out : value::undefined();
    });
    cx.define_global("JSON", value::object(json));
}

// Date
void install_date(context & cx) {
    using detail::method;
    using detail::new_table;
    // Only what a frame clock needs. A full Date is calendars, time zones and
    // parsing, and no page in this tree asks for one.
    object_object * date = new_table(cx);
    method(cx, date, "now", [](context &, std::span<value>) { return value::number(0); });
    cx.define_global("Date", value::object(date));
}

// global functions
void install_globals(context & cx) {
    using detail::method;
    using detail::new_table;
    cx.define_native("parseInt", [](context & c, std::span<value> a) {
        const std::string s = str_at(c, a, 0);
        const int base = a.size() > 1 ? static_cast<int>(num_at(a, 1)) : 10;
        try {
            std::size_t used = 0;
            const long long out = std::stoll(s, &used, base == 0 ? 10 : base);
            return used == 0 ? value::number(std::nan(""))
                             : value::number(static_cast<double>(out));
        } catch (...) {
            // parseInt("abc") is NaN, not an error - a page must not blow up on
            // a malformed number it is about to check with isNaN.
            return value::number(std::nan(""));
        }
    });
    cx.define_native("parseFloat", [](context & c, std::span<value> a) {
        const std::string s = str_at(c, a, 0);
        try {
            std::size_t used = 0;
            const double out = std::stod(s, &used);
            return used == 0 ? value::number(std::nan("")) : value::number(out);
        } catch (...) { return value::number(std::nan("")); }
    });

    // The value globals. Missing entirely before, so `NaN` was an undefined
    // global that read as `undefined` - and `NaN === NaN` was therefore TRUE,
    // because two undefineds are equal.
    cx.define_global("NaN", value::number(std::nan("")));
    cx.define_global("Infinity", value::number(std::numeric_limits<double>::infinity()));
    cx.define_global("undefined", value::undefined());
}

// Promise
void install_promise(context & cx) {
    using detail::method;
    using detail::new_table;
    cx.set_promise_factory(
        [](context & c, value v, bool rejected) { return detail::make_promise(c, v, rejected); });
    object_object * promise_ctor = new_table(cx);
    method(cx, promise_ctor, "resolve", [](context & c, std::span<value> a) {
        return detail::make_promise(c, a.empty() ? value::undefined() : a[0], false);
    });
    method(cx, promise_ctor, "reject", [](context & c, std::span<value> a) {
        return detail::make_promise(c, a.empty() ? value::undefined() : a[0], true);
    });
    // Settled promises make `all` a plain unwrap-each: the first rejection wins,
    // otherwise the result is an array of the values in order.
    method(cx, promise_ctor, "all", [](context & c, std::span<value> a) {
        const value out = c.make_array();
        auto * items = static_cast<array_object *>(out.as_heap());
        if (!a.empty() && a[0].is_array()) {
            for (const value & entry : static_cast<array_object *>(a[0].as_heap())->items) {
                if (!entry.is_object()) {
                    items->items.push_back(entry);
                    continue;
                }
                auto * promise = static_cast<object_object *>(entry.as_heap());
                value * state = promise->find("__rejected");
                value * held = promise->find("__value");
                if (state != nullptr && context::truthy(*state)) {
                    return detail::make_promise(c, held != nullptr ? *held : value::undefined(),
                                                true);
                }
                items->items.push_back(held != nullptr ? *held : entry);
            }
        }
        return detail::make_promise(c, out, false);
    });
    // `new Promise(executor)`. The executor runs IMMEDIATELY and is handed
    // resolve and reject; a promise it does not settle stays pending until
    // something later calls one of them. That is the whole of what was missing,
    // and p5.js opens with it:
    //
    //   new Promise((resolve) => {
    //     if (document.readyState === 'complete') { resolve(); }
    //     else { window.addEventListener('load', resolve, false); }
    //   })
    //
    // Callable AND a namespace, so `Promise.resolve` still reads off it.
    auto * promise_new = cx.allocate<native_object>("Promise", [](context & c, std::span<value> a) {
        const value promise = detail::make_promise(c, value::undefined(), false);
        auto * made = static_cast<object_object *>(promise.as_heap());
        made->set("__settled", value::boolean(false)); // pending until told otherwise
        if (a.empty() || !a[0].is_callable()) { return promise; }
        const value resolve = value::object(
            c.allocate<native_object>("resolve", [promise](context & inner, std::span<value> args) {
                detail::settle(inner, promise, args.empty() ? value::undefined() : args[0], false);
                return value::undefined();
            }));
        const value reject = value::object(
            c.allocate<native_object>("reject", [promise](context & inner, std::span<value> args) {
                detail::settle(inner, promise, args.empty() ? value::undefined() : args[0], true);
                return value::undefined();
            }));
        const value args[2] = {resolve, reject};
        (void)c.call(a[0], args);
        return promise;
    });
    for (const auto & [key, item] : promise_ctor->props) { promise_new->set(key, item); }
    cx.define_global("Promise", value::object(promise_new));

    cx.define_native("isNaN", [](context &, std::span<value> a) {
        return value::boolean(std::isnan(num_at(a, 0)));
    });
    cx.define_native("isFinite", [](context &, std::span<value> a) {
        return value::boolean(std::isfinite(num_at(a, 0)));
    });
    // `String` is a NAMESPACE as well as a coercion, the same way Number is.
    // `String.fromCharCode.apply(null, bytes)` is how a page turns a byte array
    // into text - 27 uses in p5.js - and it read undefined and applied it.
    {
        auto * string_ctor =
            cx.allocate<native_object>("String", [](context & c, std::span<value> a) {
                return c.string(a.empty() ? std::string{} : c.to_string(a[0]));
            });
        const auto stat = [&](const char * name, native_fn fn) {
            string_ctor->set(name, value::object(cx.allocate<native_object>(name, std::move(fn))));
        };
        // UTF-8 out, because strings here are bytes: a code point above 0x7F
        // becomes its encoding rather than one char, which is what makes the
        // round trip through String.prototype work.
        const auto encode = [](std::string & out, std::uint32_t code) {
            if (code < 0x80) {
                out += static_cast<char>(code);
            } else if (code < 0x800) {
                out += static_cast<char>(0xC0 | (code >> 6));
                out += static_cast<char>(0x80 | (code & 0x3F));
            } else if (code < 0x10000) {
                out += static_cast<char>(0xE0 | (code >> 12));
                out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (code & 0x3F));
            } else {
                out += static_cast<char>(0xF0 | (code >> 18));
                out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (code & 0x3F));
            }
        };
        stat("fromCharCode", [encode](context & c, std::span<value> a) {
            std::string out;
            for (std::size_t i = 0; i < a.size(); ++i) {
                encode(out, static_cast<std::uint32_t>(context::to_uint32(a[i]) & 0xFFFFu));
            }
            return c.string(out);
        });
        stat("fromCodePoint", [encode](context & c, std::span<value> a) {
            std::string out;
            for (std::size_t i = 0; i < a.size(); ++i) { encode(out, context::to_uint32(a[i])); }
            return c.string(out);
        });
        if (object_object * table = cx.prototype(context::proto_kind::string)) {
            string_ctor->set("prototype", value::object(table));
        }
        cx.define_global("String", value::object(string_ctor));
    }
    // `Number` is installed by install_number, which gives it the statics as
    // well as the coercion. Defining it again here would replace the whole
    // thing with a bare function and silently drop Number.isFinite and its
    // siblings - which is exactly what it used to do.
    cx.define_native("Boolean", [](context &, std::span<value> a) {
        return value::boolean(!a.empty() && context::truthy(a[0]));
    });
}

} // namespace

// Everything the standard library defines, in the order it defines it.
// --- regular expressions ---------------------------------------------------
//
// A RegExp is an ordinary object carrying its source and flags; the compiled
// program lives in a cache beside it, keyed by `source\0flags`, so the same
// literal in a loop compiles once. The cache is shared by every native this
// function installs and dies with them.
//
// `lastIndex` is a plain property, which is what `g` and `y` need and what
// pages read and write directly.
namespace {

using regex_cache = std::map<std::string, std::shared_ptr<rx::rx_prog>>;

[[nodiscard]] std::shared_ptr<rx::rx_prog> compiled(const std::shared_ptr<regex_cache> & cache,
                                                    const std::string & source,
                                                    const std::string & flags) {
    const std::string key = source + '\0' + flags;
    const auto it = cache->find(key);
    if (it != cache->end()) { return it->second; }
    auto program = std::make_shared<rx::rx_prog>(rx::rx_compile(source, flags));
    cache->emplace(key, program);
    return program;
}

// The regex a native was called on, or that was passed to a string method.
[[nodiscard]] bool regex_parts(context & cx, value v, std::string & source, std::string & flags) {
    if (!v.is_object()) { return false; }
    auto * o = static_cast<object_object *>(v.as_heap());
    value * src = o->find("source");
    if (src == nullptr) { return false; }
    source = cx.to_string(*src);
    value * fl = o->find("flags");
    flags = fl == nullptr ? std::string{} : cx.to_string(*fl);
    return true;
}

// The array `exec` and a non-global `match` both return: the whole match, then
// each group, with `index`, `input` and `groups` alongside. ctjs's wrapper
// dropped the positions; they were always in the match.
[[nodiscard]] value exec_result(context & cx, const rx::rx_prog & program, const std::string & s,
                                const rx::rx_match & m) {
    value out = cx.make_array();
    auto * arr = static_cast<array_object *>(out.as_heap());
    arr->items.push_back(cx.string(s.substr(m.begin, m.end - m.begin)));
    for (const auto & [from, to] : m.caps) {
        arr->items.push_back(from < 0 ? value::undefined()
                                      : cx.string(s.substr(static_cast<std::size_t>(from),
                                                           static_cast<std::size_t>(to - from))));
    }
    if (!program.names.empty()) {
        value groups = cx.make_object();
        auto * g = static_cast<object_object *>(groups.as_heap());
        for (const auto & [name, slot] : program.names) {
            const auto at = static_cast<std::size_t>(slot);
            const auto & cap = m.caps[at];
            g->set(name,
                   cap.first < 0
                       ? value::undefined()
                       : cx.string(s.substr(static_cast<std::size_t>(cap.first),
                                            static_cast<std::size_t>(cap.second - cap.first))));
        }
        arr->groups = groups;
    }
    arr->is_match = true;
    arr->index = value::number(static_cast<double>(m.begin));
    arr->input = cx.string(s);
    return out;
}

} // namespace

void install_regexp(context & cx) {
    using detail::method;
    using detail::new_table;
    auto cache = std::make_shared<regex_cache>();

    object_object * regexp_proto = new_table(cx);
    cx.set_prototype(context::proto_kind::regexp, regexp_proto);

    method(cx, regexp_proto, "test", [cache](context & c, std::span<value> a) {
        std::string source;
        std::string flags;
        if (!regex_parts(c, c.current_this(), source, flags)) { return value::boolean(false); }
        const std::shared_ptr<rx::rx_prog> program = compiled(cache, source, flags);
        if (!program->ok) { return value::boolean(false); }
        const std::string subject = a.empty() ? std::string{} : c.to_string(a[0]);
        rx::rx_match m;
        return value::boolean(rx::rx_search(*program, subject, 0, m));
    });

    method(cx, regexp_proto, "exec", [cache](context & c, std::span<value> a) {
        std::string source;
        std::string flags;
        if (!regex_parts(c, c.current_this(), source, flags)) { return value::null(); }
        const std::shared_ptr<rx::rx_prog> program = compiled(cache, source, flags);
        if (!program->ok) { return value::null(); }
        const std::string subject = a.empty() ? std::string{} : c.to_string(a[0]);
        auto * self = static_cast<object_object *>(c.current_this().as_heap());
        // `g` and `y` resume from lastIndex and write it back; without either,
        // exec always starts at 0.
        const bool stateful = program->global || program->sticky;
        std::size_t from = 0;
        if (stateful) {
            if (value * li = self->find("lastIndex")) {
                from = static_cast<std::size_t>(std::max(0.0, context::to_number(*li)));
            }
        }
        rx::rx_match m;
        if (from > subject.size() || !rx::rx_search(*program, subject, from, m) ||
            (program->sticky && m.begin != from)) {
            if (stateful) { self->set("lastIndex", value::number(0)); }
            return value::null();
        }
        if (stateful) { self->set("lastIndex", value::number(static_cast<double>(m.end))); }
        return exec_result(c, *program, subject, m);
    });

    method(cx, regexp_proto, "toString", [](context & c, std::span<value>) {
        std::string source;
        std::string flags;
        if (!regex_parts(c, c.current_this(), source, flags)) { return c.string("/(?:)/"); }
        return c.string("/" + source + "/" + flags);
    });

    // The constructor, and what a regex LITERAL compiles to. Installed under
    // both names: `RegExp` for pages, and a reserved one the compiler emits, so
    // a page that shadows RegExp cannot change what its own literals mean.
    const auto make = [cache](context & c, std::span<value> a) {
        const std::string source = a.empty() ? std::string{} : c.to_string(a[0]);
        const std::string flags = a.size() > 1 ? c.to_string(a[1]) : std::string{};
        value out = c.make_object();
        auto * o = static_cast<object_object *>(out.as_heap());
        const std::shared_ptr<rx::rx_prog> program = compiled(cache, source, flags);
        o->set("source", c.string(source));
        o->set("flags", c.string(flags));
        o->set("global", value::boolean(program->global));
        o->set("ignoreCase", value::boolean(program->icase));
        o->set("multiline", value::boolean(program->multi));
        o->set("sticky", value::boolean(program->sticky));
        o->set("lastIndex", value::number(0));
        o->set("__regex", value::boolean(true));
        if (object_object * table = c.prototype(context::proto_kind::regexp)) {
            o->prototype = value::object(table);
        }
        return out;
    };
    cx.define_native("RegExp", make);
    cx.define_native(std::string{regexp_factory_name}, make);
}

// `Symbol`. p5.js needs it to EXIST before anything else - the bundled zod
// calls `Symbol(...)` at load, and that one undefined global stopped the whole
// bundle with nothing but "attempted to call a non-function" to say so.
//
// A symbol's identity is a STRING KEY no source literal collides with, so a
// symbol-keyed property works through the existing string-keyed machinery
// without touching the object model. The well-known ones get fixed keys, which
// is what lets `Symbol.iterator` mean the same thing to two different pieces of
// code that never met.
void install_symbol(context & cx) {
    using detail::method;
    using detail::new_table;
    auto counter = std::make_shared<std::uint64_t>(0);

    object_object * symbol_proto = new_table(cx);
    method(cx, symbol_proto, "toString", [](context & c, std::span<value>) {
        const value self = c.current_this();
        if (!self.is_kind(heap_kind::symbol)) { return c.string("Symbol()"); }
        return c.string("Symbol(" + static_cast<symbol_object *>(self.as_heap())->description +
                        ")");
    });
    cx.set_prototype(context::proto_kind::symbol, symbol_proto);

    // Callable AND a namespace: `Symbol('x')` and `Symbol.iterator` are both
    // ordinary uses, which is why a native carries a property table now.
    auto * symbol =
        cx.allocate<native_object>("Symbol", [counter](context & c, std::span<value> a) {
            const std::string description = a.empty() ? std::string{} : c.to_string(a[0]);
            const std::string key = "@@sym:" + std::to_string((*counter)++) + ":" + description;
            return value::object(c.allocate<symbol_object>(description, key));
        });
    const auto well_known = [&](const char * name, const char * key) {
        symbol->set(name, value::object(cx.allocate<symbol_object>(name, key)));
    };
    well_known("iterator", "@@iterator");
    well_known("asyncIterator", "@@asyncIterator");
    well_known("hasInstance", "@@hasInstance");
    well_known("toPrimitive", "@@toPrimitive");
    well_known("toStringTag", "@@toStringTag");
    // A registry, so `Symbol.for('x')` twice is the same key both times.
    symbol->set(
        "for", value::object(cx.allocate<native_object>("for", [](context & c, std::span<value> a) {
            const std::string d = a.empty() ? std::string{} : c.to_string(a[0]);
            return value::object(c.allocate<symbol_object>(d, "@@for:" + d));
        })));
    cx.define_global("Symbol", value::object(symbol));
}

// `Map`, `Set`, `WeakMap`, `WeakSet`. 20 and 59 uses in p5.js, and `new Map()`
// is what stopped it once Array and Number were there.
//
// Both are built on the object model rather than on a new heap kind: entries
// live in a plain array on the instance, which makes lookup linear. That is the
// wrong complexity and it is written down rather than hidden - the maps p5
// builds are small and keyed by strings, and a hash keyed on a NaN-boxed value
// wants SameValueZero over every value kind, which is a bigger piece of work
// than this needs to be today.
//
// TODO: Map and Set lookup is LINEAR - a hash keyed on a NaN-boxed value wants
// SameValueZero over every value kind. Fine for the small string-keyed maps p5
// builds; wrong complexity for anything larger.
// TODO: WeakMap and WeakSet keep their keys alive. That is a leak, not a wrong
// answer, and it needs weak references the collector understands.
// WeakMap and WeakSet are the strong versions under different names: nothing
// here has weak references, so an entry keeps its key alive. Said out loud
// because the difference is a leak, not a wrong answer.
void install_collections(context & cx) {
    using detail::method;
    using detail::new_table;

    // The entry list of the receiver, or null when called on something else.
    const auto entries_of = [](context & c) -> array_object * {
        const value self = c.current_this();
        if (!self.is_object()) { return nullptr; }
        value * held = static_cast<object_object *>(self.as_heap())->find("__entries");
        return held != nullptr && held->is_array() ? static_cast<array_object *>(held->as_heap())
                                                   : nullptr;
    };
    // SameValueZero, which is what Map and Set key on: like ===, except NaN
    // matches NaN. A page that uses NaN as a key is doing something odd, but
    // getting it wrong here would be a silent miss.
    const auto same_value_zero = [](value a, value b) {
        if (a.is_number() && b.is_number()) {
            const double x = a.as_number();
            const double y = b.as_number();
            return (std::isnan(x) && std::isnan(y)) || x == y;
        }
        return a.strict_equals(b);
    };

    const auto build = [&](const char * name, bool keyed) {
        object_object * proto = new_table(cx);
        auto * ctor = cx.allocate<native_object>(name, [keyed](context & c, std::span<value> a) {
            // INITIALISE THE RECEIVER when there is one. `class MySet extends
            // Set {}` reaches here through super() with the new instance as
            // `this`; making a fresh object instead left the instance with none
            // of Set's state, and every method on it then failed somewhere
            // else entirely.
            value self = c.current_this();
            if (!self.is_object()) { self = c.make_object(); }
            auto * made = static_cast<object_object *>(self.as_heap());
            made->set("__entries", c.make_array());
            if (!made->prototype.is_object()) {
                if (object_object * table =
                        c.prototype(keyed ? context::proto_kind::map : context::proto_kind::set)) {
                    made->prototype = value::object(table);
                }
            }
            // `new Map([[k, v], ...])` and `new Set([...])` both seed from an
            // iterable, and an array is the only iterable that reaches here.
            if (!a.empty() && a[0].is_array()) {
                auto * entries = static_cast<array_object *>(
                    static_cast<object_object *>(self.as_heap())->find("__entries")->as_heap());
                for (const value & item : static_cast<array_object *>(a[0].as_heap())->items) {
                    if (keyed && item.is_array()) {
                        const auto & pair = static_cast<array_object *>(item.as_heap())->items;
                        value entry = c.make_array();
                        auto * cell = static_cast<array_object *>(entry.as_heap());
                        cell->items.push_back(pair.empty() ? value::undefined() : pair[0]);
                        cell->items.push_back(pair.size() > 1 ? pair[1] : value::undefined());
                        entries->items.push_back(entry);
                    } else if (!keyed) {
                        entries->items.push_back(item);
                    }
                }
            }
            return self;
        });
        // `class X extends Map` calls `super()`, which resolves through the
        // parent prototype's `constructor` - absent, and the class could not be
        // instantiated at all.
        proto->set("constructor", value::object(ctor));
        ctor->set("prototype", value::object(proto));
        cx.set_prototype(keyed ? context::proto_kind::map : context::proto_kind::set, proto);
        cx.define_global(name, value::object(ctor));
        return proto;
    };

    // --- Map ---------------------------------------------------------------
    object_object * map_proto = build("Map", true);
    const auto find_entry = [entries_of, same_value_zero](context & c, value key) -> value * {
        array_object * entries = entries_of(c);
        if (entries == nullptr) { return nullptr; }
        for (value & entry : entries->items) {
            auto * pair = static_cast<array_object *>(entry.as_heap());
            if (!pair->items.empty() && same_value_zero(pair->items[0], key)) { return &entry; }
        }
        return nullptr;
    };
    method(cx, map_proto, "get", [find_entry](context & c, std::span<value> a) {
        value * entry = find_entry(c, arg_at(a, 0));
        if (entry == nullptr) { return value::undefined(); }
        const auto & pair = static_cast<array_object *>(entry->as_heap())->items;
        return pair.size() > 1 ? pair[1] : value::undefined();
    });
    method(cx, map_proto, "has", [find_entry](context & c, std::span<value> a) {
        return value::boolean(find_entry(c, arg_at(a, 0)) != nullptr);
    });
    method(cx, map_proto, "set", [find_entry, entries_of](context & c, std::span<value> a) {
        if (value * entry = find_entry(c, arg_at(a, 0))) {
            static_cast<array_object *>(entry->as_heap())->items[1] = arg_at(a, 1);
            return c.current_this();
        }
        if (array_object * entries = entries_of(c)) {
            value pair = c.make_array();
            auto * cell = static_cast<array_object *>(pair.as_heap());
            cell->items.push_back(arg_at(a, 0));
            cell->items.push_back(arg_at(a, 1));
            entries->items.push_back(pair);
        }
        return c.current_this();
    });
    method(cx, map_proto, "delete", [entries_of, same_value_zero](context & c, std::span<value> a) {
        array_object * entries = entries_of(c);
        if (entries == nullptr) { return value::boolean(false); }
        for (std::size_t i = 0; i < entries->items.size(); ++i) {
            auto * pair = static_cast<array_object *>(entries->items[i].as_heap());
            if (!pair->items.empty() && same_value_zero(pair->items[0], arg_at(a, 0))) {
                entries->items.erase(entries->items.begin() + static_cast<std::ptrdiff_t>(i));
                return value::boolean(true);
            }
        }
        return value::boolean(false);
    });
    method(cx, map_proto, "clear", [entries_of](context & c, std::span<value>) {
        if (array_object * entries = entries_of(c)) { entries->items.clear(); }
        return value::undefined();
    });
    method(cx, map_proto, "forEach", [entries_of](context & c, std::span<value> a) {
        array_object * entries = entries_of(c);
        if (entries == nullptr || a.empty() || !a[0].is_callable()) { return value::undefined(); }
        // A copy: a callback that mutates the map must not invalidate the walk.
        const std::vector<value> snapshot = entries->items;
        for (const value & entry : snapshot) {
            const auto & pair = static_cast<array_object *>(entry.as_heap())->items;
            const value args[3] = {pair.size() > 1 ? pair[1] : value::undefined(),
                                   pair.empty() ? value::undefined() : pair[0], c.current_this()};
            (void)c.call(a[0], args);
        }
        return value::undefined();
    });
    const auto column = [entries_of](context & c, int which) {
        value out = c.make_array();
        auto * result = static_cast<array_object *>(out.as_heap());
        if (array_object * entries = entries_of(c)) {
            for (const value & entry : entries->items) {
                const auto & pair = static_cast<array_object *>(entry.as_heap())->items;
                if (which == 2) {
                    result->items.push_back(entry);
                } else if (static_cast<std::size_t>(which) < pair.size()) {
                    result->items.push_back(pair[static_cast<std::size_t>(which)]);
                }
            }
        }
        return out;
    };
    // Arrays, not iterators: for..of walks an array, and that is what these are
    // for. `[...map.keys()]` works; `map.keys().next()` does not.
    method(cx, map_proto, "keys", [column](context & c, std::span<value>) { return column(c, 0); });
    method(cx, map_proto, "values",
           [column](context & c, std::span<value>) { return column(c, 1); });
    method(cx, map_proto, "entries",
           [column](context & c, std::span<value>) { return column(c, 2); });
    map_proto->define_accessor(
        "size",
        value::object(cx.allocate<native_object>(
            "size",
            [entries_of](context & c, std::span<value>) {
                array_object * e = entries_of(c);
                return value::number(e == nullptr ? 0.0 : static_cast<double>(e->items.size()));
            })),
        value::undefined());

    // --- Set ---------------------------------------------------------------
    object_object * set_proto = build("Set", false);
    const auto set_index = [entries_of, same_value_zero](context & c, value v) -> std::ptrdiff_t {
        array_object * entries = entries_of(c);
        if (entries == nullptr) { return -1; }
        for (std::size_t i = 0; i < entries->items.size(); ++i) {
            if (same_value_zero(entries->items[i], v)) { return static_cast<std::ptrdiff_t>(i); }
        }
        return -1;
    };
    method(cx, set_proto, "has", [set_index](context & c, std::span<value> a) {
        return value::boolean(set_index(c, arg_at(a, 0)) >= 0);
    });
    method(cx, set_proto, "add", [set_index, entries_of](context & c, std::span<value> a) {
        if (set_index(c, arg_at(a, 0)) < 0) {
            if (array_object * entries = entries_of(c)) { entries->items.push_back(arg_at(a, 0)); }
        }
        return c.current_this();
    });
    method(cx, set_proto, "delete", [set_index, entries_of](context & c, std::span<value> a) {
        const std::ptrdiff_t at = set_index(c, arg_at(a, 0));
        if (at < 0) { return value::boolean(false); }
        array_object * entries = entries_of(c);
        entries->items.erase(entries->items.begin() + at);
        return value::boolean(true);
    });
    method(cx, set_proto, "clear", [entries_of](context & c, std::span<value>) {
        if (array_object * entries = entries_of(c)) { entries->items.clear(); }
        return value::undefined();
    });
    method(cx, set_proto, "forEach", [entries_of](context & c, std::span<value> a) {
        array_object * entries = entries_of(c);
        if (entries == nullptr || a.empty() || !a[0].is_callable()) { return value::undefined(); }
        const std::vector<value> snapshot = entries->items;
        for (const value & item : snapshot) {
            const value args[3] = {item, item, c.current_this()};
            (void)c.call(a[0], args);
        }
        return value::undefined();
    });
    const auto members = [entries_of](context & c) {
        value out = c.make_array();
        if (array_object * entries = entries_of(c)) {
            static_cast<array_object *>(out.as_heap())->items = entries->items;
        }
        return out;
    };
    method(cx, set_proto, "values",
           [members](context & c, std::span<value>) { return members(c); });
    method(cx, set_proto, "keys", [members](context & c, std::span<value>) { return members(c); });
    set_proto->define_accessor(
        "size",
        value::object(cx.allocate<native_object>(
            "size",
            [entries_of](context & c, std::span<value>) {
                array_object * e = entries_of(c);
                return value::number(e == nullptr ? 0.0 : static_cast<double>(e->items.size()));
            })),
        value::undefined());

    // Strong, under a weak name - see the note at the top.
    cx.define_global("WeakMap", cx.global("Map"));
    cx.define_global("WeakSet", cx.global("Set"));
}

// `Error` and the standard subclasses. 239 `throw new` in p5.js, and every one
// of them used to construct undefined - which raised "attempted to construct a
// non-function" and killed the run outright, uncatchably, from inside a `try`
// that was there to handle exactly that.
//
// An error is an ordinary object with `name`, `message` and `stack`, and each
// constructor's `prototype` chains to Error's so `e instanceof Error` holds for
// a TypeError and for a page's own `class MyError extends Error`.
void install_errors(context & cx) {
    using detail::method;
    using detail::new_table;

    object_object * error_proto = new_table(cx);
    error_proto->set("name", cx.string("Error"));
    error_proto->set("message", cx.string(""));
    method(cx, error_proto, "toString", [](context & c, std::span<value>) {
        const value self = c.current_this();
        const std::string name = c.to_string(c.lookup_property(self, "name"));
        const std::string message = c.to_string(c.lookup_property(self, "message"));
        return c.string(message.empty() ? name : name + ": " + message);
    });

    // One constructor shape, five names. `parent` is Error's prototype for the
    // subclasses, so the chain a page walks is the one it expects.
    const auto define = [&](const char * name, object_object * parent) {
        object_object * proto = parent == nullptr ? error_proto : new_table(cx);
        if (parent != nullptr) {
            proto->prototype = value::object(parent);
            proto->set("name", cx.string(name));
        }
        auto * ctor = cx.allocate<native_object>(name, [proto](context & c, std::span<value> a) {
            // `this` is the instance when called through `new`; a bare
            // `Error(...)` makes one anyway, which is what the spec says.
            value self = c.current_this();
            if (!self.is_object()) { self = c.make_object(); }
            auto * made = static_cast<object_object *>(self.as_heap());
            if (!made->prototype.is_object()) { made->prototype = value::object(proto); }
            if (!a.empty()) { made->set("message", c.string(c.to_string(a[0]))); }
            // `stack` is a string a page prints, so it exists and says what it
            // knows rather than being absent and read as undefined.
            made->set("stack", c.string(c.to_string(c.lookup_property(self, "name")) + ": " +
                                        (a.empty() ? std::string{} : c.to_string(a[0]))));
            return self;
        });
        proto->set("constructor", value::object(ctor));
        ctor->set("prototype", value::object(proto));
        cx.define_global(name, value::object(ctor));
        return proto;
    };
    define("Error", nullptr);
    for (const char * name :
         {"TypeError", "RangeError", "ReferenceError", "SyntaxError", "EvalError", "URIError"}) {
        (void)define(name, error_proto);
    }
    cx.set_prototype(context::proto_kind::error, error_proto);
}

// `Proxy` and `Reflect`. p5.js has three proxies, and one of them runs at the
// bundle's top level - `p5.renderers['p2d-p3'] = new Proxy(Renderer2D,
// {construct(...) {...}})` - so the bundle could not finish loading without it.
//
// Three traps: `get`, `has` and `construct`, which are the three p5 uses. A
// trap that is not implemented is not silently skipped - the operation falls
// through to the target, which is what an absent trap means anyway. The traps
// that ARE missing (set, deleteProperty, ownKeys, apply and the rest) behave
// the same way, so a page using one gets the target's behaviour rather than a
// wrong answer; that is a real gap and it is named here rather than discovered.
void install_proxy(context & cx) {
    using detail::method;
    using detail::new_table;

    cx.define_native("Proxy", [](context & c, std::span<value> a) {
        return value::object(c.allocate<proxy_object>(arg_at(a, 0), arg_at(a, 1)));
    });

    // Reflect is the un-trapped operation a handler calls to do the default
    // thing - `Reflect.get(t, k)` inside a `get` trap is how a proxy adds
    // behaviour instead of replacing it.
    object_object * reflect = new_table(cx);
    method(cx, reflect, "get", [](context & c, std::span<value> a) {
        return c.lookup_index(arg_at(a, 0), arg_at(a, 1));
    });
    method(cx, reflect, "set", [](context & c, std::span<value> a) {
        if (arg_at(a, 0).is_object()) {
            static_cast<object_object *>(a[0].as_heap())
                ->set(c.to_string(arg_at(a, 1)), arg_at(a, 2));
        }
        return value::boolean(true);
    });
    method(cx, reflect, "has", [](context & c, std::span<value> a) {
        return value::boolean(!c.lookup_index(arg_at(a, 0), arg_at(a, 1)).is_undefined());
    });
    method(cx, reflect, "construct", [](context & c, std::span<value> a) {
        std::vector<value> args;
        if (arg_at(a, 1).is_array()) { args = static_cast<array_object *>(a[1].as_heap())->items; }
        return c.construct(arg_at(a, 0), args);
    });
    method(cx, reflect, "apply", [](context & c, std::span<value> a) {
        std::vector<value> args;
        if (arg_at(a, 2).is_array()) { args = static_cast<array_object *>(a[2].as_heap())->items; }
        return c.call(arg_at(a, 0), args, arg_at(a, 1));
    });
    method(cx, reflect, "ownKeys", [](context & c, std::span<value> a) {
        value out = c.make_array();
        auto * result = static_cast<array_object *>(out.as_heap());
        if (arg_at(a, 0).is_object()) {
            static_cast<object_object *>(a[0].as_heap())->each_own_key([&](const std::string & k) {
                result->items.push_back(c.string(k));
            });
        }
        return out;
    });
    method(cx, reflect, "getPrototypeOf", [](context &, std::span<value> a) {
        if (!arg_at(a, 0).is_object()) { return value::null(); }
        return static_cast<object_object *>(a[0].as_heap())->prototype;
    });
    cx.define_global("Reflect", value::object(reflect));
}

// `Function.prototype`. 84 `.call(`, 78 `.apply(` and 26 `.bind(` in p5.js -
// and it cannot install one event listener without bind:
// `window.addEventListener(e, this['_on' + e].bind(this), {...})`.
void install_function(context & cx) {
    using detail::method;
    using detail::new_table;
    object_object * function_proto = new_table(cx);

    method(cx, function_proto, "call", [](context & c, std::span<value> a) {
        const value self = c.current_this();
        if (!self.is_callable()) { return value::undefined(); }
        const std::vector<value> rest(a.begin() + (a.empty() ? 0 : 1), a.end());
        return c.call(self, rest, arg_at(a, 0));
    });
    method(cx, function_proto, "apply", [](context & c, std::span<value> a) {
        const value self = c.current_this();
        if (!self.is_callable()) { return value::undefined(); }
        std::vector<value> args;
        if (arg_at(a, 1).is_array()) { args = static_cast<array_object *>(a[1].as_heap())->items; }
        return c.call(self, args, arg_at(a, 0));
    });
    method(cx, function_proto, "bind", [](context & c, std::span<value> a) {
        const value self = c.current_this();
        if (!self.is_callable()) { return value::undefined(); }
        const value receiver = arg_at(a, 0);
        // The arguments bound NOW are prepended to the ones supplied later,
        // which is what makes `f.bind(o, 1)` a partial application rather than
        // just a receiver change.
        const auto bound =
            std::make_shared<std::vector<value>>(a.begin() + (a.empty() ? 0 : 1), a.end());
        return value::object(detail::cx_native(
            c, "bound", [self, receiver, bound](context & inner, std::span<value> later) {
                std::vector<value> args = *bound;
                args.insert(args.end(), later.begin(), later.end());
                return inner.call(self, args, receiver);
            }));
    });
    // TODO: return the REAL source. p5's Friendly Error System parses a sketch
    // with `f.toString()` and gets "[native code]", which is where the ratchet
    // stops. Needs a source span on function_proto - the ctjs node's `text` is
    // a string_view INTO the source, so the offset is a subtraction - plus the
    // program keeping its source string. Error.stack wants the same thing, and
    // would get real line numbers from it.
    method(cx, function_proto, "toString", [](context & c, std::span<value>) {
        // THE REAL SOURCE, when there is any. A closure knows which program its
        // protos came from, and the program kept the text - so this is a
        // substring, not a reconstruction, and what comes back is exactly what
        // was written.
        const value self = c.current_this();
        if (self.is_kind(heap_kind::function)) {
            auto * closure = static_cast<closure_object *>(self.as_heap());
            if (closure->owner != nullptr && closure->proto != nullptr) {
                const struct function_proto & fp = *closure->proto;
                const std::string & text = closure->owner->source;
                if (fp.source_end > fp.source_begin && fp.source_end <= text.size()) {
                    return c.string(text.substr(fp.source_begin, fp.source_end - fp.source_begin));
                }
            }
        }
        // A native has no source; saying so the way every engine does keeps a
        // caller that concatenates the result from producing something strange.
        return c.string("function () { [native code] }");
    });
    cx.set_prototype(context::proto_kind::function, function_proto);
}

// TYPED ARRAYS. 123 uses in p5.js - Uint8Array for pixels, Float32Array for
// matrices - and `new Uint32Array(n)` is what stopped the bundle once
// localStorage was there.
//
// Stored as ordinary arrays of values rather than packed bytes: that costs
// memory and buys the whole existing array machinery - indexing, length,
// iteration, every prototype method - for nothing. What it does NOT cost is
// correctness on write, which is where a shortcut would have hurt: the element
// coercion is real, so a Uint8ClampedArray clamps and a Uint8Array wraps.
//
// TODO: an ArrayBuffer should be SHARED storage. Two views over one buffer do
// not see each other's writes today, because each view owns its elements.
// The gap that remains, and it is a real one: an ArrayBuffer here is not
// shared storage. Two views over the same buffer do not see each other's
// writes, because each view owns its elements. p5 uses views over their own
// buffers, and a page that aliases two would get wrong answers - so that is
// said here rather than discovered.
void install_typed_arrays(context & cx) {
    using detail::method;
    using detail::new_table;

    struct spec {
        const char * name;
        element_kind kind;
        int bytes;
    };
    static constexpr spec kinds[] = {
        {"Int8Array", element_kind::i8, 1},
        {"Uint8Array", element_kind::u8, 1},
        {"Uint8ClampedArray", element_kind::u8_clamped, 1},
        {"Int16Array", element_kind::i16, 2},
        {"Uint16Array", element_kind::u16, 2},
        {"Int32Array", element_kind::i32, 4},
        {"Uint32Array", element_kind::u32, 4},
        {"Float32Array", element_kind::f32, 4},
        {"Float64Array", element_kind::f64, 8},
    };

    object_object * typed_proto = new_table(cx);
    method(cx, typed_proto, "set", [](context & c, std::span<value> a) {
        auto * self = detail::this_array(c);
        if (self == nullptr || !arg_at(a, 0).is_array()) { return value::undefined(); }
        const auto & from = static_cast<array_object *>(a[0].as_heap())->items;
        const auto at = static_cast<std::size_t>(std::max(0.0, num_at(a, 1)));
        for (std::size_t i = 0; i < from.size() && at + i < self->items.size(); ++i) {
            self->items[at + i] =
                value::number(coerce_element(self->elements, context::to_number(from[i])));
        }
        return value::undefined();
    });
    method(cx, typed_proto, "subarray", [](context & c, std::span<value> a) {
        auto * self = detail::this_array(c);
        value out = c.make_array();
        if (self == nullptr) { return out; }
        auto * made = static_cast<array_object *>(out.as_heap());
        made->elements = self->elements;
        const std::size_t n = self->items.size();
        const std::size_t from = a.empty() ? 0 : clamp_index(num_at(a, 0), n);
        const std::size_t to = a.size() > 1 ? clamp_index(num_at(a, 1), n) : n;
        for (std::size_t i = from; i < to; ++i) { made->items.push_back(self->items[i]); }
        return out;
    });
    cx.set_prototype(context::proto_kind::typed_array, typed_proto);

    for (const spec & each : kinds) {
        const element_kind kind = each.kind;
        auto * ctor = cx.allocate<native_object>(each.name, [kind](context & c,
                                                                   std::span<value> a) {
            value out = c.make_array();
            auto * made = static_cast<array_object *>(out.as_heap());
            made->elements = kind;
            const value from = arg_at(a, 0);
            if (from.is_array()) {
                // from another array, coerced element by element
                for (const value & v : static_cast<array_object *>(from.as_heap())->items) {
                    made->items.push_back(
                        value::number(coerce_element(kind, context::to_number(v))));
                }
            } else if (from.is_object()) {
                // an ArrayBuffer, or anything else with a byteLength/length
                const double length = context::to_number(c.lookup_property(from, "length"));
                const double n = std::isnan(length)
                                     ? context::to_number(c.lookup_property(from, "byteLength"))
                                     : length;
                made->items.assign(static_cast<std::size_t>(std::max(0.0, n)), value::number(0));
            } else {
                made->items.assign(static_cast<std::size_t>(std::max(0.0, num_at(a, 0))),
                                   value::number(0));
            }
            return out;
        });
        ctor->set("BYTES_PER_ELEMENT", value::number(each.bytes));
        cx.define_global(each.name, value::object(ctor));
    }

    // An ArrayBuffer is a LENGTH here, not storage - see the note above.
    cx.define_native("ArrayBuffer", [](context & c, std::span<value> a) {
        value out = c.make_object();
        auto * made = static_cast<object_object *>(out.as_heap());
        made->set("byteLength", value::number(std::max(0.0, num_at(a, 0))));
        made->set("length", value::number(std::max(0.0, num_at(a, 0))));
        return out;
    });
}

// `new Function(body)` - a compiler at run time.
//
// TODO: this can be implemented NOW. The blocker cited below - that run_loop
// read nested protos out of a single `program_` - was fixed when a closure
// gained an `owner`. What remains is for the context to OWN the programs it
// compiles at run time, the way browser::run_script now keeps its own.
// It EXISTS, because p5.js builds one while loading and a missing global stops
// the bundle outright; and it REFUSES when called, because compiling one here
// properly is a VM change rather than a library one. A closure holds a
// `const function_proto *` into the program it came from, and `run_loop` reads
// nested function protos out of a single `program_` - so a program compiled at
// run time needs the VM to know which program each frame belongs to. That is
// worth doing and it is not this.
//
// The refusal is the same shape as WEBGL's: the page loads, and only a page
// that actually reaches the feature is told. All three uses in p5.js generate
// shader source, which a 2D sketch never touches.
void install_dynamic_function(context & cx) {
    cx.define_native("Function", [](context & c, std::span<value>) {
        return value::object(
            c.allocate<native_object>("anonymous", [](context & inner, std::span<value>) {
                inner.refuse("TypeError", "`new Function(...)` is not implemented - compiling a "
                                          "body at run time needs the VM to track which program "
                                          "each frame belongs to");
                return value::undefined();
            }));
    });
    // `Function.prototype`, reachable from script rather than only consulted by
    // lookup. `Function.prototype.call.bind(...)` and
    // `Function.prototype.hasOwnProperty` are ordinary idioms, and this is the
    // same table lookup already walks - so a page that adds to it is seen by
    // every function, which is what a page doing that expects.
    if (object_object * table = cx.prototype(context::proto_kind::function)) {
        static_cast<native_object *>(cx.global("Function").as_heap())
            ->set("prototype", value::object(table));
    }
}

void install_builtins(context & cx, std::uint64_t seed) {
    install_math(cx, seed);
    install_regexp(cx);
    install_symbol(cx);
    install_collections(cx);
    install_errors(cx);
    install_proxy(cx);
    install_function(cx);
    install_typed_arrays(cx);
    install_dynamic_function(cx);
    install_array(cx);
    install_string(cx);
    install_number(cx);
    install_object(cx);
    install_json(cx);
    install_date(cx);
    install_globals(cx);
    install_promise(cx);
}

// NOT here, and deliberately:
//
//   * `Date` beyond `now` - calendars, time zones and date parsing, which no
//     page in this tree uses.
//   * regular expressions, and therefore `String.match`, `String.search` and
//     the RegExp forms of `replace`/`split`. Those need a regex engine; the
//     compiler still rejects a regex literal with a clear message rather than
//     mis-compiling one.
//   * `Map`, `Set`, `Symbol`, `Proxy`, typed arrays, generators.
//   * PENDING promises, a job queue and `new Promise(executor)` - see the note
//     above `make_promise`. Promises here are settled when they are made.
//   * a real prototype CHAIN: one level, no `__proto__`, no `Object.create`.
//     Everything a page does with builtins works; user-defined inheritance
//     arrives with `class` in a later stage.

} // namespace ctbrowser::script
