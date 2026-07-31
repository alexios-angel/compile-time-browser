#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/compile.hpp>
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
// The same, on a NATIVE. A built-in that is both callable and a namespace -
// `Object(x)` coerces and `Object.keys` is a static - has to be a native
// carrying properties, and its statics are installed exactly like a table's.
inline void method(context & cx, native_object * table, std::string name, native_fn fn) {
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
    // A RESUMPTION IS A PROMISE HANDLER. `await` registers the suspended frame
    // on the awaited promise's own handler list, so it queues and orders with
    // every `then` rather than being a second mechanism that races them.
    if (value * waiting = record->find("co"); waiting != nullptr) {
        cx.resume(*waiting, settled, rejected);
        return;
    }
    if (next == nullptr) { return; }
    // `finally` RUNS EITHER WAY AND CHANGES NOTHING. Its callback takes no
    // argument, its return value is ignored, and the outcome - value or
    // rejection - passes straight through to the next promise. It used to call
    // its callback the moment it was registered and hand back the SAME promise,
    // so it ran before the rejection it was supposed to follow and a chain
    // after it saw the wrong link.
    if (value * on_finally = record->find("fin"); on_finally != nullptr) {
        if (on_finally->is_callable()) { (void)cx.call(*on_finally, std::span<const value>{}); }
        settle(cx, *next, settled, rejected);
        return;
    }
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

// THE JOB. Delivery is queued rather than run, and a job is a callable plus
// values so the collector traces it - so the C++ work has to be reachable
// through a value, which is what this native is. One per context, made on
// demand and remembered.
[[nodiscard]] inline value delivery_job(context & cx) {
    static const std::string slot = "__deliverJob";
    if (const value existing = cx.global(slot); existing.is_callable()) { return existing; }
    value made =
        value::object(cx.allocate<native_object>(slot, [](context & c, std::span<value> a) {
            if (a.size() >= 3 && a[0].is_object()) {
                deliver(c, a[0], a[1], context::truthy(a[2]));
            }
            return value::undefined();
        }));
    cx.define_global(slot, made);
    return made;
}

// Queue one delivery for the end of the turn.
inline void enqueue_delivery(context & cx, value record, value settled, bool rejected) {
    cx.queue_microtask(delivery_job(cx), {record, settled, value::boolean(rejected)});
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
    // QUEUED, not called. `p.then(f); after();` must run `after` first, and a
    // handler that runs the instant a promise settles can also reenter code
    // that is halfway through its own work.
    for (const value & record : pending) { enqueue_delivery(cx, record, with, rejected); }
}

// `then`/`catch`/`finally` all reduce to: remember what to do for each way this
// can settle, and either do it now or when it settles.
//
// `on_finally` is the third form: one callback for BOTH outcomes, with no say
// in either - its return value is ignored and the outcome passes through. It
// goes in the same record so it queues and orders like everything else, rather
// than being a special case at the call site.
inline value settle_with(context & cx, value on_ok, value on_err,
                         value on_finally = value::undefined()) {
    const value self = cx.current_this();
    if (!self.is_object()) { return self; }
    auto * promise = static_cast<object_object *>(self.as_heap());

    const value next = make_promise(cx, value::undefined(), false);
    static_cast<object_object *>(next.as_heap())->set("__settled", value::boolean(false));
    object_object * record = new_table(cx);
    record->set("ok", on_ok);
    record->set("err", on_err);
    record->set("next", next);
    if (!on_finally.is_undefined()) { record->set("fin", on_finally); }

    value * settled = promise->find("__settled");
    if (settled != nullptr && context::truthy(*settled)) {
        value * held = promise->find("__value");
        value * state = promise->find("__rejected");
        // Already settled, so there is nothing to wait FOR - but the handler
        // still runs at the end of the turn rather than here. `Promise
        // .resolve(1).then(f); after();` orders them the same way as the
        // pending case, which is the whole point of a queue.
        enqueue_delivery(cx, value::object(record), held == nullptr ? value::undefined() : *held,
                         state != nullptr && context::truthy(*state));
        return next;
    }
    value * handlers = promise->find("__handlers");
    if (handlers != nullptr && handlers->is_array()) {
        static_cast<array_object *>(handlers->as_heap())->items.push_back(value::object(record));
    }
    return next;
}

// then/catch/finally, once for the program rather than three natives per
// promise. They were already receiver-based - settle_with reads current_this -
// so nothing about them was per-instance; and having them here is what makes `p
// instanceof Promise` answerable at all, since a promise then has a prototype to
// walk. Built lazily so a context with no promise ever made pays nothing.
[[nodiscard]] inline object_object * promise_prototype(context & cx) {
    if (object_object * existing = cx.prototype(context::proto_kind::promise)) { return existing; }
    object_object * table = new_table(cx);
    method(cx, table, "then", [](context & c, std::span<value> a) {
        return settle_with(c, a.empty() ? value::undefined() : a[0],
                           a.size() > 1 ? a[1] : value::undefined());
    });
    method(cx, table, "catch", [](context & c, std::span<value> a) {
        return settle_with(c, value::undefined(), a.empty() ? value::undefined() : a[0]);
    });
    method(cx, table, "finally", [](context & c, std::span<value> a) {
        return settle_with(c, value::undefined(), value::undefined(), arg_at(a, 0));
    });
    cx.set_prototype(context::proto_kind::promise, table);
    return table;
}

[[nodiscard]] inline value make_promise(context & cx, value v, bool rejected) {
    object_object * promise = new_table(cx);
    promise->prototype = value::object(promise_prototype(cx));
    promise->set("__value", v);
    promise->set("__rejected", value::boolean(rejected));
    promise->set("__settled", value::boolean(true));
    promise->set("__handlers", cx.make_array());
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

// `Type.prototype.constructor === Type`, and `Type.name` is its name.
//
// Both are how code identifies a value without trusting `instanceof` - which a
// page can defeat, and which does not work across realms.
// `Object.getPrototypeOf(x).constructor.name` is the standard walk, and with
// either half missing it yields undefined, which compares equal to the other
// undefined it is being tested against and reports a false match.
void link_constructor(context & cx, object_object * table, const char * name, value ctor) {
    if (table == nullptr) { return; }
    table->set("constructor", ctor);
    if (ctor.is_kind(heap_kind::native)) {
        static_cast<native_object *>(ctor.as_heap())->set("name", cx.string(name));
    } else if (ctor.is_object()) {
        static_cast<object_object *>(ctor.as_heap())->set("name", cx.string(name));
    }
}

// Math
void install_math(context & cx, std::uint64_t seed) {
    using detail::method;
    using detail::new_table;
    object_object * math = new_table(cx);
    // FROM <numbers>, not written out by hand. A transcribed constant is a digit
    // waiting to be wrong, and one that is wrong in its last few places is
    // invisible: it agrees with every printed value a test is likely to check
    // and disagrees with the real one by an amount that accumulates.
    math->set("PI", value::number(std::numbers::pi));
    math->set("E", value::number(std::numbers::e));
    const auto unary = [&](std::string name, double (*fn)(double)) {
        method(cx, math, name,
               [fn](context &, std::span<value> a) { return value::number(fn(num_at(a, 0))); });
    };
    math->set("SQRT2", value::number(std::numbers::sqrt2));
    // <numbers> has no SQRT1_2 or LN10, so those two are DERIVED rather than
    // transcribed - which is exact for the reciprocal and one division for the
    // other, and neither can be typed wrong.
    math->set("SQRT1_2", value::number(1.0 / std::numbers::sqrt2));
    math->set("LN2", value::number(std::numbers::ln2));
    math->set("LN10", value::number(std::numbers::ln10));
    math->set("LOG2E", value::number(std::numbers::log2e));
    math->set("LOG10E", value::number(std::numbers::log10e));
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
        // Through the one conversion for..of and spread use, so all three agree
        // about what "iterable" means - a Map, a Set, a string, an array or
        // anything array-LIKE (a NodeList, `arguments`, a typed-array shim).
        const value from = c.iterable_values(arg_at(a, 0));
        if (from.is_array()) { made->items = static_cast<array_object *>(from.as_heap())->items; }
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
    // `Array.prototype.toString` IS join(','). The C++ conversion always knew
    // that; the prototype did not, so once conversion started going through an
    // object's own toString an array fell back to Object.prototype's and
    // stringified as "[object Array]".
    method(cx, array_proto, "toString", [](context & c, std::span<value>) {
        auto * self = detail::this_array(c);
        if (self == nullptr) { return c.string(""); }
        std::string out;
        for (std::size_t i = 0; i < self->items.size(); ++i) {
            if (i != 0) { out += ','; }
            if (!self->items[i].is_nullish()) { out += c.to_string(self->items[i]); }
        }
        return c.string(out);
    });
    // `entries`, `keys` and `values`. Each hands back an ARRAY where the spec
    // says an iterator, for the same reason matchAll does: `for (const [i, v] of
    // xs.entries())` and a spread both work over one, which is everything
    // anybody does with them. p5's Table walks its rows with entries().
    method(cx, array_proto, "entries", [](context & c, std::span<value>) {
        auto * self = detail::this_array(c);
        value out = c.make_array();
        if (self == nullptr) { return out; }
        auto * pairs = static_cast<array_object *>(out.as_heap());
        for (std::size_t i = 0; i < self->items.size(); ++i) {
            value pair = c.make_array();
            auto * both = static_cast<array_object *>(pair.as_heap());
            both->items.push_back(value::number(static_cast<double>(i)));
            both->items.push_back(self->items[i]);
            pairs->items.push_back(pair);
        }
        return out;
    });
    method(cx, array_proto, "keys", [](context & c, std::span<value>) {
        auto * self = detail::this_array(c);
        value out = c.make_array();
        if (self == nullptr) { return out; }
        auto * items = static_cast<array_object *>(out.as_heap());
        for (std::size_t i = 0; i < self->items.size(); ++i) {
            items->items.push_back(value::number(static_cast<double>(i)));
        }
        return out;
    });
    method(cx, array_proto, "values", [](context & c, std::span<value>) {
        auto * self = detail::this_array(c);
        value out = c.make_array();
        if (self == nullptr) { return out; }
        static_cast<array_object *>(out.as_heap())->items = self->items;
        return out;
    });
    link_constructor(cx, array_proto, "Array", value::object(array_ctor));
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
    // `toString` and `valueOf` on a primitive. Both exist so that generic code
    // written against "any value" works on one: `String(x)`, `'' + x` and a
    // template literal all reach for toString, and a library that calls it
    // directly - p5.js does, on its own colour objects and on plain strings
    // through the same path - got "undefined is not a function".
    method(cx, string_proto, "toString",
           [](context & c, std::span<value>) { return c.string(detail::this_string(c)); });
    method(cx, string_proto, "valueOf",
           [](context & c, std::span<value>) { return c.string(detail::this_string(c)); });
    method(cx, string_proto, "codePointAt", [](context & c, std::span<value> a) {
        const std::string str = detail::this_string(c);
        const auto i = static_cast<std::size_t>(std::max(0.0, num_at(a, 0)));
        if (i >= str.size()) { return value::undefined(); }
        // BYTES, not code points - strings are bytes in this engine
        // (docs/script.md), so this agrees with charCodeAt rather than
        // pretending to a UTF-16 view that nothing else here has.
        return value::number(static_cast<double>(static_cast<unsigned char>(str[i])));
    });
    // `normalize` is the IDENTITY here, and says so: strings are bytes, so
    // there is no decomposition to compose. Returning the string unchanged is
    // what a page that calls it defensively expects; refusing would break
    // pages that only ever pass ASCII, which is all of them here.
    method(cx, string_proto, "normalize",
           [](context & c, std::span<value>) { return c.string(detail::this_string(c)); });
    method(cx, string_proto, "localeCompare", [](context & c, std::span<value> a) {
        // Byte order, which is the locale this engine has.
        const std::string self = detail::this_string(c);
        const std::string other = str_at(c, a, 0);
        return value::number(self < other ? -1 : (self == other ? 0 : 1));
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
        // A REGEXP SEPARATOR. `str.split(/\r?\n/)` is how essentially every
        // library splits lines, and coercing the pattern to a string made it a
        // separator that never matched - so the whole input came back as one
        // element and nothing said anything. p5.js splits text into lines that
        // way, then takes Math.max over the widths, which for an unsplit line
        // is fine and for an EMPTY list is -Infinity.
        //
        // Driven through the pattern's own `exec`, like matchAll, so there is
        // one regex path and split cannot disagree with match about a boundary.
        // Searching the remainder each round rather than moving lastIndex means
        // a non-global pattern works too, which is the form this is written in.
        if (a[0].is_object() && c.lookup_property(a[0], "exec").is_callable()) {
            const value exec = c.lookup_property(a[0], "exec");
            std::string rest = s;
            while (!rest.empty()) {
                // RESET EVERY ROUND, because the subject shrinks. A global
                // pattern's exec resumes from `lastIndex`, and this hands it a
                // fresh remainder each time - so a stale index points past the
                // start of the new string and finds nothing. `'a b,c'
                // .split(/[ ,]/g)` split once and stopped.
                c.store_property(a[0], "lastIndex", value::number(0));
                const value args[1] = {c.string(rest)};
                const value m = c.call(exec, args, a[0]);
                if (!m.is_array()) { break; }
                auto * parts = static_cast<array_object *>(m.as_heap());
                if (parts->items.empty()) { break; }
                const auto index =
                    static_cast<std::size_t>(std::max(0.0, context::to_number(parts->index)));
                const std::string whole = c.to_string(parts->items[0]);
                if (index >= rest.size()) { break; }
                result->items.push_back(c.string(rest.substr(0, index)));
                // A CAPTURE IN THE SEPARATOR IS KEPT, which is the one reason to
                // put a group in a split pattern at all.
                for (std::size_t g = 1; g < parts->items.size(); ++g) {
                    result->items.push_back(parts->items[g]);
                }
                // An empty match would otherwise never advance.
                rest = rest.substr(index + std::max<std::size_t>(whole.size(), 1));
            }
            result->items.push_back(c.string(rest));
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
    // ONE REPLACEMENT, shared by `replace` and `replaceAll`.
    //
    // The pattern may be a STRING or a REGEXP, and the replacement may be a
    // string with `$` references or a FUNCTION - four combinations, and the
    // only difference between the two methods is how many matches they take.
    //
    // Regexes did not work here at all: the pattern was coerced with to_string
    // and looked for with std::string::find, so `s.replace(/ /g, '|')` searched
    // for the literal text of the regex object and, finding none, returned the
    // string unchanged. Silent, and it is the commonest use of the method -
    // acorn builds its keyword tables with exactly that call, so every keyword
    // matcher inside p5's own parser was a pattern that could never match.
    const auto substitute = [](const std::string & replacement, const std::string & matched,
                               const std::vector<std::string> & groups, std::size_t at,
                               const std::string & subject) {
        std::string out;
        for (std::size_t i = 0; i < replacement.size(); ++i) {
            if (replacement[i] != '$' || i + 1 >= replacement.size()) {
                out += replacement[i];
                continue;
            }
            const char next = replacement[i + 1];
            if (next == '$') {
                out += '$';
            } else if (next == '&') {
                out += matched;
            } else if (next == '`') {
                out += subject.substr(0, at);
            } else if (next == '\'') {
                out += subject.substr(std::min(subject.size(), at + matched.size()));
            } else if (next >= '1' && next <= '9') {
                // Two digits when there is a group to match them - `$12` is
                // group 12 where one exists and group 1 followed by "2" where
                // it does not.
                std::size_t which = static_cast<std::size_t>(next - '0');
                std::size_t used = 1;
                if (i + 2 < replacement.size() && replacement[i + 2] >= '0' &&
                    replacement[i + 2] <= '9') {
                    const std::size_t wider =
                        which * 10 + static_cast<std::size_t>(replacement[i + 2] - '0');
                    if (wider <= groups.size()) {
                        which = wider;
                        used = 2;
                    }
                }
                if (which <= groups.size()) { out += groups[which - 1]; }
                i += used;
                continue;
            } else {
                out += replacement[i];
                continue;
            }
            ++i;
        }
        return out;
    };

    const auto replace_with = [substitute](context & c, std::span<value> a, bool all) {
        const std::string self = detail::this_string(c);
        const value pattern = a.empty() ? value::undefined() : a[0];
        const value replacement = a.size() > 1 ? a[1] : value::undefined();

        // A REGEXP PATTERN, driven through its own `exec` so there is one regex
        // path and replace cannot disagree with match about a boundary.
        if (pattern.is_object() && c.lookup_property(pattern, "exec").is_callable()) {
            const value exec = c.lookup_property(pattern, "exec");
            // `g` on the pattern means every match even for `replace`; a plain
            // `replace` with a non-global pattern takes the first only.
            const bool every = all || context::truthy(c.lookup_property(pattern, "global"));
            std::string out;
            std::string rest = self;
            std::size_t consumed = 0;
            // An empty pattern matches at the END position too - `'ab'
            // .replace(/x*/g, '-')` is "-a-b-" and not "-a-b" - so one pass
            // runs with nothing left, and only a second one stops.
            bool tail_seen = false;
            for (std::size_t guard = 0; guard <= self.size() + 2; ++guard) {
                if (rest.empty()) {
                    if (tail_seen) { break; }
                    tail_seen = true;
                }
                c.store_property(pattern, "lastIndex", value::number(0));
                const value subject = c.string(rest);
                const value found = c.call(exec, std::span<const value>{&subject, 1}, pattern);
                if (!found.is_array()) { break; }
                auto * parts = static_cast<array_object *>(found.as_heap());
                if (parts->items.empty()) { break; }
                const std::string matched = c.to_string(parts->items[0]);
                const auto at =
                    static_cast<std::size_t>(std::max(0.0, context::to_number(parts->index)));
                if (at > rest.size()) { break; }
                std::vector<std::string> groups;
                for (std::size_t g = 1; g < parts->items.size(); ++g) {
                    groups.push_back(parts->items[g].is_undefined() ? std::string{}
                                                                    : c.to_string(parts->items[g]));
                }
                out += rest.substr(0, at);
                if (replacement.is_callable()) {
                    // (match, p1..pn, offset, whole) - the offset is into the
                    // ORIGINAL string, not into what is left of it.
                    std::vector<value> args;
                    args.push_back(c.string(matched));
                    for (const std::string & g : groups) { args.push_back(c.string(g)); }
                    args.push_back(value::number(static_cast<double>(consumed + at)));
                    args.push_back(c.string(self));
                    out += c.to_string(c.call(replacement, args));
                } else {
                    out +=
                        substitute(c.to_string(replacement), matched, groups, consumed + at, self);
                }
                // An empty match must still advance, or this never terminates.
                const std::size_t step = at + std::max<std::size_t>(matched.size(), 1);
                if (matched.empty() && at < rest.size()) { out += rest[at]; }
                consumed += step;
                rest = step >= rest.size() ? std::string{} : rest.substr(step);
                if (!every) { break; }
            }
            out += rest;
            return c.string(out);
        }

        // A STRING pattern: the first occurrence, or all of them.
        const std::string from = c.to_string(pattern);
        if (from.empty()) { return c.string(self); }
        std::string out;
        std::size_t at = 0;
        while (true) {
            const std::size_t found = self.find(from, at);
            if (found == std::string::npos) {
                out += self.substr(at);
                break;
            }
            out += self.substr(at, found - at);
            if (replacement.is_callable()) {
                const value args[3] = {c.string(from), value::number(static_cast<double>(found)),
                                       c.string(self)};
                out += c.to_string(c.call(replacement, args));
            } else {
                out += substitute(c.to_string(replacement), from, {}, found, self);
            }
            at = found + from.size();
            if (!all) {
                out += self.substr(at);
                break;
            }
        }
        return c.string(out);
    };

    method(cx, string_proto, "replace",
           [replace_with](context & c, std::span<value> a) { return replace_with(c, a, false); });
    // `match` - the single commonest thing done with a regular expression, and
    // it simply was not here. A page calling it got "undefined is not a
    // function" from inside whatever library it was using.
    //
    // The two forms return DIFFERENT SHAPES, which is the part that is easy to
    // get wrong: with `g` it is a flat list of the matched strings and nothing
    // else, and without it a single exec result carrying index, input and the
    // capture groups. Code branches on that difference.
    method(cx, string_proto, "match", [](context & c, std::span<value> a) {
        if (a.empty() || !a[0].is_object()) { return value::null(); }
        const value pattern = a[0];
        const value exec = c.lookup_property(pattern, "exec");
        if (!exec.is_callable()) { return value::null(); }
        const std::string self = detail::this_string(c);
        const value subject = c.string(self);
        const bool all = context::truthy(c.lookup_property(pattern, "global"));
        c.store_property(pattern, "lastIndex", value::number(0));
        if (!all) {
            const value found = c.call(exec, std::span<const value>{&subject, 1}, pattern);
            return found.is_nullish() ? value::null() : found;
        }
        value list = c.make_array();
        auto * items = static_cast<array_object *>(list.as_heap());
        double previous = -1;
        for (std::size_t guard = 0; guard <= self.size() + 1; ++guard) {
            const value found = c.call(exec, std::span<const value>{&subject, 1}, pattern);
            if (!found.is_array()) { break; }
            auto * parts = static_cast<array_object *>(found.as_heap());
            if (parts->items.empty()) { break; }
            items->items.push_back(parts->items[0]);
            const double now = context::to_number(c.lookup_property(pattern, "lastIndex"));
            if (!(now > previous)) { break; }
            previous = now;
        }
        // A global match that found nothing is NULL, not an empty array - the
        // usual guard is `if (m)`, and an empty array is truthy.
        return items->items.empty() ? value::null() : list;
    });
    // `matchAll` is exec RUN TO EXHAUSTION, which is exactly what it means -
    // so it is built on exec rather than on a second copy of the regex
    // plumbing, and it cannot disagree with `match` about what matched.
    //
    // It hands back an ARRAY where the spec says an iterator. Both work with
    // `for (const m of ...)` and with a spread, which is all anyone does with
    // one; a real iterator would only differ for a caller that stops early on a
    // pattern expensive enough to notice.
    method(cx, string_proto, "matchAll", [](context & c, std::span<value> a) {
        value list = c.make_array();
        auto * items = static_cast<array_object *>(list.as_heap());
        if (a.empty() || !a[0].is_object()) { return list; }
        const value pattern = a[0];
        const value exec = c.lookup_property(pattern, "exec");
        if (!exec.is_callable()) { return list; }
        const value subject = c.string(detail::this_string(c));
        c.store_property(pattern, "lastIndex", value::number(0));
        double previous = -1;
        // Bounded by the subject: each round must advance lastIndex, and a
        // pattern that matches empty - or one without `g`, whose exec always
        // restarts at 0 - would otherwise spin forever.
        for (std::size_t guard = 0; guard <= detail::this_string(c).size() + 1; ++guard) {
            const value args[1] = {subject};
            const value found = c.call(exec, args, pattern);
            if (found.is_nullish()) { break; }
            items->items.push_back(found);
            const double now = context::to_number(c.lookup_property(pattern, "lastIndex"));
            if (!(now > previous)) { break; }
            previous = now;
        }
        return list;
    });
    method(cx, string_proto, "replaceAll",
           [replace_with](context & c, std::span<value> a) { return replace_with(c, a, true); });
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
    method(cx, string_proto, "padEnd", [](context & c, std::span<value> a) {
        std::string self = detail::this_string(c);
        const auto want = static_cast<std::size_t>(std::clamp(num_at(a, 0), 0.0, 1000000.0));
        const std::string pad = a.size() > 1 ? c.to_string(a[1]) : " ";
        if (pad.empty() || self.size() >= want) { return c.string(self); }
        while (self.size() < want) { self += pad; }
        self.resize(want);
        return c.string(self);
    });
    method(cx, string_proto, "trimStart", [](context & c, std::span<value>) {
        const std::string self = detail::this_string(c);
        const std::size_t from = self.find_first_not_of(" \t\n\r\f\v");
        return c.string(from == std::string::npos ? std::string{} : self.substr(from));
    });
    method(cx, string_proto, "trimEnd", [](context & c, std::span<value>) {
        const std::string self = detail::this_string(c);
        const std::size_t to = self.find_last_not_of(" \t\n\r\f\v");
        return c.string(to == std::string::npos ? std::string{} : self.substr(0, to + 1));
    });
    method(cx, string_proto, "toLocaleUpperCase", [](context & c, std::span<value>) {
        std::string self = detail::this_string(c);
        for (char & ch : self) {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
        return c.string(self);
    });
    method(cx, string_proto, "toLocaleLowerCase", [](context & c, std::span<value>) {
        std::string self = detail::this_string(c);
        for (char & ch : self) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        return c.string(self);
    });
    method(cx, string_proto, "concat", [](context & c, std::span<value> a) {
        std::string s = detail::this_string(c);
        for (std::size_t i = 0; i < a.size(); ++i) { s += c.to_string(a[i]); }
        return c.string(s);
    });
    cx.set_prototype(context::proto_kind::string, string_proto);
}

// Number.prototype
// `Boolean`, and the two methods a boolean has.
//
// Small, and it closes a hole rather than adding a feature: `true.toString()`
// found nothing, so generic code that converts "any value" by calling toString
// on it failed on exactly one of the primitive types.
// `structuredClone` - a DEEP copy of plain data.
//
// A page uses it to take a snapshot it can then mutate without disturbing the
// original; p5.js clones a colour's coordinate array before scaling it, so
// without this every conversion mutated the colour it was reading.
//
// Data only, which is what the algorithm covers: objects, arrays, typed arrays
// and primitives are copied, and anything with behaviour - a function, a DOM
// node - is not clonable. Cycles are preserved through a seen-list, because a
// structure that points back at itself is exactly what a naive recursive copy
// cannot survive.
// `btoa` and `atob` - base64, which is how bytes travel inside a string.
//
// A data: URL is base64, `canvas.toDataURL()` produces one, and a page that
// hand-rolls a download encodes with btoa. Byte-oriented, which is what these
// two actually are: btoa's argument is a "binary string" of bytes 0-255 and not
// text, and treating it as text is how a page's image comes out corrupted.
void install_base64(context & cx) {
    static constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    cx.define_native("btoa", [](context & c, std::span<value> a) {
        const std::string in = a.empty() ? std::string{} : c.to_string(a[0]);
        std::string out;
        out.reserve((in.size() + 2) / 3 * 4);
        for (std::size_t i = 0; i < in.size(); i += 3) {
            const unsigned b0 = static_cast<unsigned char>(in[i]);
            const unsigned b1 = i + 1 < in.size() ? static_cast<unsigned char>(in[i + 1]) : 0;
            const unsigned b2 = i + 2 < in.size() ? static_cast<unsigned char>(in[i + 2]) : 0;
            const unsigned triple = (b0 << 16) | (b1 << 8) | b2;
            out += alphabet[(triple >> 18) & 0x3F];
            out += alphabet[(triple >> 12) & 0x3F];
            // The padding is what says how many of the last three bytes were
            // real, so it is not optional.
            out += i + 1 < in.size() ? alphabet[(triple >> 6) & 0x3F] : '=';
            out += i + 2 < in.size() ? alphabet[triple & 0x3F] : '=';
        }
        return c.string(out);
    });
    cx.define_native("atob", [](context & c, std::span<value> a) {
        const std::string in = a.empty() ? std::string{} : c.to_string(a[0]);
        std::string out;
        unsigned accumulator = 0;
        int bits = 0;
        for (const char ch : in) {
            if (ch == '=' || ch == '\n' || ch == '\r' || ch == ' ') { continue; }
            const std::size_t at = alphabet.find(ch);
            if (at == std::string_view::npos) { continue; }
            accumulator = (accumulator << 6) | static_cast<unsigned>(at);
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                out += static_cast<char>((accumulator >> bits) & 0xFF);
            }
        }
        return c.string(out);
    });
}

void install_structured_clone(context & cx) {
    cx.define_native("structuredClone", [](context & c, std::span<value> a) {
        std::vector<std::pair<heap_object *, value>> seen;
        const auto copy = [&](auto && self, value v) -> value {
            if (!v.is_heap()) { return v; }
            for (const auto & [from, to] : seen) {
                if (from == v.as_heap()) { return to; }
            }
            if (v.is_array()) {
                auto * source = static_cast<array_object *>(v.as_heap());
                value made = c.make_array();
                auto * out = static_cast<array_object *>(made.as_heap());
                out->elements = source->elements;
                seen.emplace_back(v.as_heap(), made);
                out->items.reserve(source->items.size());
                for (const value & item : source->items) { out->items.push_back(self(self, item)); }
                return made;
            }
            if (v.is_object()) {
                auto * source = static_cast<object_object *>(v.as_heap());
                value made = c.make_object();
                auto * out = static_cast<object_object *>(made.as_heap());
                seen.emplace_back(v.as_heap(), made);
                for (const auto & [key, item] : source->props) { out->set(key, self(self, item)); }
                return made;
            }
            // A string is immutable, so sharing it IS a copy. Everything else -
            // a function, a symbol - is not clonable, and a browser throws
            // DataCloneError rather than quietly handing back the original.
            if (v.is_string()) { return v; }
            c.throw_error("DataCloneError", std::string{"structuredClone cannot copy a "} +
                                                std::string{context::type_of(v)});
            return value::undefined();
        };
        return copy(copy, a.empty() ? value::undefined() : a[0]);
    });
}

void install_boolean(context & cx) {
    using detail::method;
    using detail::new_table;
    object_object * boolean_proto = new_table(cx);
    method(cx, boolean_proto, "toString", [](context & c, std::span<value>) {
        return c.string(context::truthy(c.current_this()) ? "true" : "false");
    });
    method(cx, boolean_proto, "valueOf", [](context & c, std::span<value>) {
        return value::boolean(context::truthy(c.current_this()));
    });
    cx.set_prototype(context::proto_kind::boolean, boolean_proto);
    auto * boolean_ctor = cx.allocate<native_object>("Boolean", [](context &, std::span<value> a) {
        return value::boolean(!a.empty() && context::truthy(a[0]));
    });
    // A CONVERSION, not a constructor of wrappers - see context::construct. `new
    // Boolean(x)` evaluates to the converted value here rather than to a wrapper
    // object; before the flag it evaluated to an empty object and the value was
    // gone.
    boolean_ctor->set("__conversion", value::boolean(true));
    boolean_ctor->set("prototype", value::object(boolean_proto));
    link_constructor(cx, boolean_proto, "Boolean", value::object(boolean_ctor));
    cx.define_global("Boolean", value::object(boolean_ctor));
}

void install_number(context & cx) {
    using detail::method;
    using detail::new_table;

    // `Number` as a namespace as well as a coercion. It was only the latter,
    // so every `Number.isFinite(x)` guard in a page read undefined and called
    // it - the failure landing well away from the test that caused it.
    auto * number_ctor = cx.allocate<native_object>("Number", [](context &, std::span<value> a) {
        return value::number(a.empty() ? 0.0 : context::to_number(a[0]));
    });
    // A CONVERSION, not a constructor of wrappers - see context::construct. `new
    // Number(x)` evaluates to the converted value here rather than to a wrapper
    // object; before the flag it evaluated to an empty object and the value was
    // gone.
    number_ctor->set("__conversion", value::boolean(true));
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
    method(cx, number_proto, "valueOf", [](context & c, std::span<value>) {
        return value::number(context::to_number(c.current_this()));
    });
    method(cx, number_proto, "toExponential", [](context & c, std::span<value> a) {
        const double v = context::to_number(c.current_this());
        const int places = a.empty() || a[0].is_undefined()
                               ? 6
                               : std::clamp(static_cast<int>(context::to_number(a[0])), 0, 100);
        std::array<char, 64> out{};
        const int written = std::snprintf(out.data(), out.size(), "%.*e", places, v);
        std::string text{out.data(), static_cast<std::size_t>(std::max(0, written))};
        // C prints at least two exponent digits and JavaScript prints the
        // fewest that suffice: 1e+2, not 1e+02.
        const std::size_t e = text.find('e');
        if (e != std::string::npos && e + 2 < text.size()) {
            std::size_t digits = e + 2;
            while (digits + 1 < text.size() && text[digits] == '0') { text.erase(digits, 1); }
        }
        return c.string(text);
    });
    method(cx, number_proto, "toPrecision", [](context & c, std::span<value> a) {
        const double v = context::to_number(c.current_this());
        // No argument at all is toString, not zero significant digits.
        if (a.empty() || a[0].is_undefined()) { return c.string(c.to_string(c.current_this())); }
        const int digits = std::clamp(static_cast<int>(context::to_number(a[0])), 1, 100);
        std::array<char, 64> out{};
        const int written = std::snprintf(out.data(), out.size(), "%.*g", digits, v);
        return c.string(std::string{out.data(), static_cast<std::size_t>(std::max(0, written))});
    });
    number_ctor->set("prototype", value::object(number_proto));
    link_constructor(cx, number_proto, "Number", value::object(number_ctor));
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
    // `[object Type]`, for whatever the receiver actually is.
    //
    // Returning "[object Object]" unconditionally is right for a plain object
    // and wrong for every other receiver, and the reason this matters is that
    // `Object.prototype.toString.call(x)` is THE type-detection idiom - the one
    // way to tell an array from a plain object from a null from a string
    // without trusting a constructor a page may have replaced. Libraries then
    // parse the result: colorjs, bundled inside p5.js, does
    // `str.match(/^\[object\s+(.*?)\]$/)[1].toLowerCase()`, which against a
    // string that is not in that shape indexes null.
    method(cx, object_proto, "toString", [](context & c, std::span<value>) {
        const value self = c.current_this();
        std::string_view tag = "Object";
        if (self.is_undefined()) {
            tag = "Undefined";
        } else if (self.is_null()) {
            tag = "Null";
        } else if (self.is_array()) {
            tag = "Array";
        } else if (self.is_string()) {
            tag = "String";
        } else if (self.is_number()) {
            tag = "Number";
        } else if (self.is_boolean()) {
            tag = "Boolean";
        } else if (self.is_callable()) {
            tag = "Function";
        } else if (self.is_kind(heap_kind::symbol)) {
            tag = "Symbol";
        }
        return c.string("[object " + std::string{tag} + "]");
    });
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

    // `Object` IS CALLABLE. `Object(x)` coerces to an object and is what a
    // spread helper reaches for - Babel's `_objectSpread` opens with
    // `Object(source)` - so a plain namespace table is not enough: it has the
    // statics and cannot be called, which fails as "Object is not a function"
    // from inside a helper that has nothing to do with Object.
    auto * object_ctor = cx.allocate<native_object>("Object", [](context & c, std::span<value> a) {
        // An object passes through; a primitive is boxed, which here means the
        // nearest thing this engine has - an empty object - because there are
        // no wrapper types. Nothing but identity is observable either way for
        // the uses that matter, and `Object(x) === x` for an object is the
        // property helpers actually depend on.
        const value v = arg_at(a, 0);
        return v.is_object_like() ? v : c.make_object();
    });
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
    link_constructor(cx, object_proto, "Object", value::object(object_ctor));
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
    // A PRIMITIVE HAS A PROTOTYPE TOO. `Object.getPrototypeOf('x')` is
    // String.prototype, not null - the primitive is boxed for the lookup, which
    // is the same reason `'x'.toUpperCase()` works at all.
    //
    // Returning null made an ordinary type-detection loop draw the wrong
    // conclusion rather than none: colorjs walks
    // `Object.getPrototypeOf(arg)?.constructor?.name` and compares it to the
    // constructor's name. With null on one side and a nameless class on the
    // other, undefined === undefined reported a MATCH - so a plain string
    // passed as an instance of a colour space, and every conversion through it
    // silently handed the string straight back.
    method(cx, object_ctor, "getPrototypeOf", [](context & c, std::span<value> a) {
        const value of = arg_at(a, 0);
        if (of.is_object()) { return static_cast<object_object *>(of.as_heap())->prototype; }
        if (of.is_kind(heap_kind::function)) {
            return static_cast<closure_object *>(of.as_heap())->proto_link;
        }
        const auto table = [&](context::proto_kind kind) {
            object_object * found = c.prototype(kind);
            return found == nullptr ? value::null() : value::object(found);
        };
        if (of.is_string()) { return table(context::proto_kind::string); }
        if (of.is_number()) { return table(context::proto_kind::number); }
        if (of.is_boolean()) { return table(context::proto_kind::boolean); }
        if (of.is_array()) { return table(context::proto_kind::array); }
        if (of.is_kind(heap_kind::native)) { return table(context::proto_kind::function); }
        if (of.is_kind(heap_kind::symbol)) { return table(context::proto_kind::symbol); }
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
// `Date` - CONSTRUCTIBLE, and reading a calendar out of a millisecond count.
//
// It was a namespace with `now()` on it and nothing else, so `new Date()` was
// "Date is not a function". p5 exposes day()/month()/year()/hour() and every
// one of them builds a Date, so a sketch showing a clock - which is most
// beginners' second sketch - failed on its first line.
//
// UTC only, and no parsing: `new Date(string)` is a calendar and a timezone
// database, which is a different project. What is here is the civil date
// arithmetic that turns a millisecond count into fields and back, which is what
// a page reading the clock actually needs.
void install_date(context & cx) {
    using detail::method;
    using detail::new_table;

    object_object * date_proto = new_table(cx);

    // Days since the epoch to y/m/d, by Howard Hinnant's civil_from_days - the
    // standard branch-free algorithm, valid for any year a double can hold.
    // Written out rather than reached for through <chrono>'s calendar types
    // because those are C++20 library, and this file is the standard library
    // for a different language.
    const auto civil_from_days = [](long long z, int & y, unsigned & m, unsigned & d) {
        z += 719468;
        const long long era = (z >= 0 ? z : z - 146096) / 146097;
        const auto doe = static_cast<unsigned long long>(z - era * 146097);
        const unsigned long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        const long long yr = static_cast<long long>(yoe) + era * 400;
        const unsigned long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const unsigned long long mp = (5 * doy + 2) / 153;
        d = static_cast<unsigned>(doy - (153 * mp + 2) / 5 + 1);
        m = static_cast<unsigned>(mp < 10 ? mp + 3 : mp - 9);
        y = static_cast<int>(yr + (m <= 2 ? 1 : 0));
    };
    const auto days_from_civil = [](int y, unsigned m, unsigned d) -> long long {
        y -= m <= 2 ? 1 : 0;
        const long long era = (y >= 0 ? y : y - 399) / 400;
        const auto yoe = static_cast<unsigned long long>(y - era * 400);
        const unsigned long long doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
        const unsigned long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + static_cast<long long>(doe) - 719468;
    };

    // The instant this Date holds, in milliseconds. Kept as an ordinary
    // property so the object is inspectable and the collector needs to know
    // nothing new about it.
    const auto epoch_ms = [](context & c) {
        const value self = c.current_this();
        if (!self.is_object()) { return 0.0; }
        const value * held = static_cast<object_object *>(self.as_heap())->find("__ms");
        return held == nullptr ? 0.0 : context::to_number(*held);
    };
    // The civil fields of that instant.
    struct fields {
        int year;
        unsigned month; // 1-12
        unsigned day;
        int hour;
        int minute;
        int second;
        int weekday; // 0 = Sunday
    };
    const auto split = [civil_from_days](double ms) {
        fields out{};
        const auto total = static_cast<long long>(std::floor(ms));
        long long days = total / 86400000;
        long long rest = total % 86400000;
        if (rest < 0) {
            rest += 86400000;
            --days;
        }
        civil_from_days(days, out.year, out.month, out.day);
        out.hour = static_cast<int>(rest / 3600000);
        out.minute = static_cast<int>(rest / 60000 % 60);
        out.second = static_cast<int>(rest / 1000 % 60);
        // 1970-01-01 was a Thursday, which is what anchors the cycle.
        out.weekday = static_cast<int>(((days % 7) + 11) % 7);
        return out;
    };
    const auto field_method = [&](const char * name, int fields::* which) {
        method(cx, date_proto, name, [epoch_ms, split, which](context & c, std::span<value>) {
            return value::number(split(epoch_ms(c)).*which);
        });
    };
    field_method("getHours", &fields::hour);
    field_method("getMinutes", &fields::minute);
    field_method("getSeconds", &fields::second);
    field_method("getDay", &fields::weekday);
    field_method("getFullYear", &fields::year);
    method(cx, date_proto, "getMonth", [epoch_ms, split](context & c, std::span<value>) {
        // ZERO-BASED, which is the wart every calendar bug starts with and
        // which a page's arithmetic is written against.
        return value::number(static_cast<double>(split(epoch_ms(c)).month) - 1);
    });
    method(cx, date_proto, "getDate", [epoch_ms, split](context & c, std::span<value>) {
        return value::number(static_cast<double>(split(epoch_ms(c)).day));
    });
    method(cx, date_proto, "getMilliseconds", [epoch_ms](context & c, std::span<value>) {
        const double ms = epoch_ms(c);
        return value::number(std::fmod(std::fmod(ms, 1000.0) + 1000.0, 1000.0));
    });
    method(cx, date_proto, "getTime",
           [epoch_ms](context & c, std::span<value>) { return value::number(epoch_ms(c)); });
    method(cx, date_proto, "valueOf",
           [epoch_ms](context & c, std::span<value>) { return value::number(epoch_ms(c)); });
    // No timezone here, so the local getters ARE the UTC ones and say so rather
    // than pretending to a zone this engine does not have.
    method(cx, date_proto, "getTimezoneOffset",
           [](context &, std::span<value>) { return value::number(0); });
    method(cx, date_proto, "toISOString", [epoch_ms, split](context & c, std::span<value>) {
        const fields f = split(epoch_ms(c));
        std::array<char, 40> out{};
        const int written = std::snprintf(
            out.data(), out.size(), "%04d-%02u-%02uT%02d:%02d:%02d.%03dZ", f.year, f.month, f.day,
            f.hour, f.minute, f.second,
            static_cast<int>(std::fmod(std::fmod(epoch_ms(c), 1000.0) + 1000.0, 1000.0)));
        return c.string(std::string{out.data(), static_cast<std::size_t>(std::max(0, written))});
    });
    method(cx, date_proto, "toString", [epoch_ms, split](context & c, std::span<value>) {
        const fields f = split(epoch_ms(c));
        std::array<char, 48> out{};
        const int written = std::snprintf(out.data(), out.size(), "%04d-%02u-%02u %02d:%02d:%02d",
                                          f.year, f.month, f.day, f.hour, f.minute, f.second);
        return c.string(std::string{out.data(), static_cast<std::size_t>(std::max(0, written))});
    });

    auto * ctor = cx.allocate<native_object>(
        "Date", [date_proto, days_from_civil](context & c, std::span<value> a) {
            value self = c.current_this();
            if (!self.is_object()) { self = c.make_object(); }
            auto * made = static_cast<object_object *>(self.as_heap());
            made->prototype = value::object(date_proto);
            double ms = 0;
            if (a.size() == 1 && a[0].is_number()) {
                ms = a[0].as_number();
            } else if (a.size() >= 2) {
                // (year, monthIndex, day, hours, minutes, seconds, ms)
                const auto part = [&](std::size_t i, double fallback) {
                    return i < a.size() ? context::to_number(a[i]) : fallback;
                };
                const long long days = days_from_civil(static_cast<int>(part(0, 1970)),
                                                       static_cast<unsigned>(part(1, 0)) + 1,
                                                       static_cast<unsigned>(part(2, 1)));
                ms = static_cast<double>(days) * 86400000.0 + part(3, 0) * 3600000.0 +
                     part(4, 0) * 60000.0 + part(5, 0) * 1000.0 + part(6, 0);
            }
            // `new Date()` with no argument is NOW, and now comes from the
            // context's clock - see context::set_clock. It used to be the literal
            // epoch, so every page here believed it was 1970.
            if (a.empty()) { ms = c.clock_ms(); }
            made->set("__ms", value::number(ms));
            return self;
        });
    ctor->set("prototype", value::object(date_proto));
    date_proto->set("constructor", value::object(ctor));
    method(cx, ctor, "now",
           [](context & c, std::span<value>) { return value::number(c.clock_ms()); });
    method(cx, ctor, "UTC", [days_from_civil](context & c, std::span<value> a) {
        const auto part = [&](std::size_t i, double fallback) {
            return i < a.size() ? context::to_number(a[i]) : fallback;
        };
        const long long days =
            days_from_civil(static_cast<int>(part(0, 1970)), static_cast<unsigned>(part(1, 0)) + 1,
                            static_cast<unsigned>(part(2, 1)));
        (void)c;
        return value::number(static_cast<double>(days) * 86400000.0 + part(3, 0) * 3600000.0 +
                             part(4, 0) * 60000.0 + part(5, 0) * 1000.0 + part(6, 0));
    });
    cx.define_global("Date", value::object(ctor));
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
    // What `await` needs to suspend: a promise that has not settled, and a way
    // to settle one. The VM can READ a promise - it always could - but making
    // and settling run this library's own logic, queue included.
    cx.set_pending_promise_factory([](context & c) {
        const value made = detail::make_promise(c, value::undefined(), false);
        static_cast<object_object *>(made.as_heap())->set("__settled", value::boolean(false));
        return made;
    });
    cx.set_promise_settler([](context & c, value promise, value with, bool rejected) {
        detail::settle(c, promise, with, rejected);
    });
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
        // A `value` CAPTURED BY A C++ LAMBDA IS INVISIBLE TO THE COLLECTOR.
        //
        // These two hold the only reference to the promise that outlives the
        // constructor: a page keeps `resolve`, not the promise. The lambda
        // capture is not a root, so the promise was collected out from under it
        // and calling resolve() later settled freed memory - which failed
        // SILENTLY, because settle() checks is_object() and a recycled cell is
        // usually not one.
        //
        // The cost was an async function that could suspend exactly ONCE. The
        // first await's promise was still in a live frame's registers; the
        // second one's existed only inside these captures and in the awaited
        // promise's handler list - a cycle with no root - so it went, and the
        // frame never came back. Every p5 loader awaits twice.
        //
        // The fix is to make the reference REACHABLE rather than to stop
        // capturing: a native's props are traced, so a property the page never
        // reads is exactly the root this needs. Anything else that captures a
        // value in a native lambda needs the same treatment.
        auto * resolve_fn =
            c.allocate<native_object>("resolve", [promise](context & inner, std::span<value> args) {
                detail::settle(inner, promise, args.empty() ? value::undefined() : args[0], false);
                return value::undefined();
            });
        auto * reject_fn =
            c.allocate<native_object>("reject", [promise](context & inner, std::span<value> args) {
                detail::settle(inner, promise, args.empty() ? value::undefined() : args[0], true);
                return value::undefined();
            });
        resolve_fn->set("__promise", promise);
        reject_fn->set("__promise", promise);
        const value resolve = value::object(resolve_fn);
        const value reject = value::object(reject_fn);
        const value args[2] = {resolve, reject};
        (void)c.call(a[0], args);
        return promise;
    });
    for (const auto & [key, item] : promise_ctor->props) { promise_new->set(key, item); }
    promise_new->set("prototype", value::object(detail::promise_prototype(cx)));
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
        // A CONVERSION, not a constructor of wrappers - see context::construct. `new
        // String(x)` evaluates to the converted value here rather than to a wrapper
        // object; before the flag it evaluated to an empty object and the value was
        // gone.
        string_ctor->set("__conversion", value::boolean(true));
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
            link_constructor(cx, table, "String", value::object(string_ctor));
        }
        cx.define_global("String", value::object(string_ctor));
    }
    // `Number` is installed by install_number, which gives it the statics as
    // well as the coercion. Defining it again here would replace the whole
    // thing with a bare function and silently drop Number.isFinite and its
    // siblings - which is exactly what it used to do.
    //
    // `Boolean` is the same, and it WAS being redefined here, one line under
    // that warning: install_boolean gives it a prototype and this replaced it
    // with a bare coercion, so `Object.getPrototypeOf(true) === Boolean.prototype`
    // was false and `true.toString()` found nothing.
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
        auto * ctor = cx.allocate<native_object>(name, [keyed, same_value_zero](
                                                           context & c, std::span<value> a) {
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
            // `new Map([[k, v], ...])` and `new Set([...])` seed from ANY
            // iterable, through the same conversion for..of uses - so `new
            // Set(otherSet)` and `new Map(map.entries())` work, which is how a page
            // copies one.
            const value seed = a.empty() ? value::undefined() : c.iterable_values(a[0]);
            if (seed.is_array()) {
                auto * entries = static_cast<array_object *>(
                    static_cast<object_object *>(self.as_heap())->find("__entries")->as_heap());
                for (const value & item : static_cast<array_object *>(seed.as_heap())->items) {
                    if (keyed) {
                        if (!item.is_array()) { continue; }
                        const auto & pair = static_cast<array_object *>(item.as_heap())->items;
                        const value key = pair.empty() ? value::undefined() : pair[0];
                        const value held = pair.size() > 1 ? pair[1] : value::undefined();
                        // A REPEATED KEY REPLACES, it does not append. `new Map([['a',
                        // 1], ['a', 2]])` has ONE entry and it holds 2 - and a
                        // duplicate that merely sat there would be found by get()
                        // and missed by size(), which is two answers to one question.
                        bool replaced = false;
                        for (const value & existing : entries->items) {
                            if (!existing.is_array()) { continue; }
                            auto & cell = static_cast<array_object *>(existing.as_heap())->items;
                            if (!cell.empty() && same_value_zero(cell[0], key)) {
                                if (cell.size() > 1) {
                                    cell[1] = held;
                                } else {
                                    cell.push_back(held);
                                }
                                replaced = true;
                                break;
                            }
                        }
                        if (replaced) { continue; }
                        value entry = c.make_array();
                        auto * cell = static_cast<array_object *>(entry.as_heap());
                        cell->items.push_back(key);
                        cell->items.push_back(held);
                        entries->items.push_back(entry);
                        continue;
                    }
                    // A SET DEDUPES, which is the entire reason to use one.
                    // `new Set([1, 2, 2, 3]).size` was 4 - so `[...new Set(v)]`
                    // was not a unique list, and p5's colour-space registry is
                    // exactly that idiom.
                    bool seen = false;
                    for (const value & existing : entries->items) {
                        if (same_value_zero(existing, item)) {
                            seen = true;
                            break;
                        }
                    }
                    if (!seen) { entries->items.push_back(item); }
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
    // A Set's `entries` pairs each member WITH ITSELF, which looks odd and is
    // the spec: it exists so a Set and a Map can be walked by the same code.
    method(cx, set_proto, "entries", [members](context & c, std::span<value>) {
        const value all = members(c);
        value out = c.make_array();
        if (!all.is_array()) { return out; }
        auto * pairs = static_cast<array_object *>(out.as_heap());
        for (const value & member : static_cast<array_object *>(all.as_heap())->items) {
            value pair = c.make_array();
            auto * both = static_cast<array_object *>(pair.as_heap());
            both->items.push_back(member);
            both->items.push_back(member);
            pairs->items.push_back(pair);
        }
        return out;
    });
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
            // `stack` CARRIES THE FRAMES, not just the message.
            //
            // It said "TypeError: whatever" and stopped there, which reads as a
            // stack to code that only prints it and is useless to code that
            // wants to know WHERE - and a library that reports the site of an
            // error is the normal reason to look at one. p5's Friendly Error
            // System is exactly that.
            //
            // No frame is skipped: a native pushes none of its own, so the top
            // of the stack is already the JS function that wrote `new Error`,
            // which is the line a reader wants named first.
            made->set("stack", c.string(c.to_string(c.lookup_property(self, "name")) +
                                        (a.empty() ? std::string{} : ": " + c.to_string(a[0])) +
                                        c.current_stack()));
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
// AN ARRAYBUFFER IS SHARED STORAGE. A view over the WHOLE of one is that
// storage rather than a copy, so two views see each other's writes - which is
// the entire reason a page wraps `await res.arrayBuffer()` in one.
//
// The gap that remains is a SUB-RANGE view: `new Uint8Array(buf, 4, 8)` cannot
// be expressed while a view owns its own elements, and it REFUSES with a
// RangeError rather than handing back a copy that would silently not alias.
// Expressing it wants a view to address a span of someone else's storage, which
// is a change to every one of the ~176 places that reach for `array_object
// ::items` - worth doing when something needs it, and worth refusing rather
// than faking until then.
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
                // AN ARRAYBUFFER IS SHARED STORAGE, so a view over the whole of
                // one IS that storage rather than a copy of it: two views over
                // a buffer see each other's writes, which is the entire reason
                // a page wraps `await res.arrayBuffer()` in one.
                //
                // A SUB-RANGE view - `new Uint8Array(buf, 4, 8)` - cannot be
                // expressed while a view owns its own elements, so it REFUSES
                // rather than handing back a silently independent copy. That is
                // the same choice WEBGL and `new Function` were given: a page
                // that reaches the gap is told.
                const value bytes = c.lookup_property(from, "__bytes");
                if (bytes.is_array()) {
                    if (a.size() > 1) {
                        c.throw_error("RangeError",
                                      "a typed array over PART of an ArrayBuffer is not "
                                      "implemented - a view owns its elements here, so an "
                                      "offset view could not see writes through the buffer");
                        return value::undefined();
                    }
                    auto * shared = static_cast<array_object *>(bytes.as_heap());
                    shared->elements = kind;
                    return bytes;
                }
                // Anything else with a length: a fresh zeroed view of that size.
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
    // AN ARRAYBUFFER OWNS BYTES, and hands the same storage to every view made
    // over the whole of it. It used to be a length and nothing else, so two
    // views were silently independent and a page that wrote through one and
    // read through the other got zeroes.
    cx.define_native("ArrayBuffer", [](context & c, std::span<value> a) {
        value out = c.make_object();
        auto * made = static_cast<object_object *>(out.as_heap());
        const auto n = static_cast<std::size_t>(std::max(0.0, num_at(a, 0)));
        made->set("byteLength", value::number(static_cast<double>(n)));
        made->set("length", value::number(static_cast<double>(n)));
        value bytes = c.make_array();
        auto * store = static_cast<array_object *>(bytes.as_heap());
        store->elements = element_kind::u8;
        store->items.assign(n, value::number(0));
        made->set("__bytes", bytes);
        return out;
    });
}

// `new Function(body)` - A COMPILER AT RUN TIME.
//
// It existed and refused, because a closure holds a `const function_proto *`
// into the program it came from and nothing owned a program compiled here. Two
// things closed that: `closure_object::owner` records which program a closure's
// nested functions live in, so a frame from one program can call into another;
// and the context now OWNS the programs it compiles, so they outlive the
// closures that point into them.
//
// The body is wrapped in a function expression and returned, so the parameters
// and the body go through exactly the path a written-out function does. p5.js
// builds three of these for shader source; a bundle may build any number.
void install_dynamic_function(context & cx) {
    cx.define_native("Function", [](context & c, std::span<value> a) {
        // `new Function(a, b, 'return a + b')` - every argument but the last
        // names a parameter, and the last is the body. `new Function()` is a
        // function that does nothing, which is what the spec says.
        std::string params;
        for (std::size_t i = 0; i + 1 < a.size(); ++i) {
            if (!params.empty()) { params += ","; }
            params += c.to_string(a[i]);
        }
        const std::string body = a.empty() ? std::string{} : c.to_string(a[a.size() - 1]);
        // RETURNED, not left as an expression statement: the program's value is
        // what its top level returns, and a bare expression yields nothing.
        // The newlines are the spec's own formatting, and they matter - they
        // keep a `//` comment at the end of the body from swallowing the brace.
        const std::string source =
            "return (function anonymous(" + params + "\n) {\n" + body + "\n});";

        program compiled = compiler::compile(source);
        if (!compiled.ok) {
            // A SyntaxError a page can catch, because `new Function` on
            // user-supplied text is exactly where one is expected.
            c.throw_error("SyntaxError", compiled.error);
            return value::undefined();
        }
        const program & kept = c.own_program(std::move(compiled));
        return c.run_nested(kept);
    });
    // `Function.prototype`, reachable from script rather than only consulted by
    // lookup. `Function.prototype.call.bind(...)` and
    // `Function.prototype.hasOwnProperty` are ordinary idioms, and this is the
    // same table lookup already walks - so a page that adds to it is seen by
    // every function, which is what a page doing that expects.
    if (object_object * table = cx.prototype(context::proto_kind::function)) {
        static_cast<native_object *>(cx.global("Function").as_heap())
            ->set("prototype", value::object(table));
        link_constructor(cx, table, "Function", cx.global("Function"));
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
    install_boolean(cx);
    install_structured_clone(cx);
    install_base64(cx);
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
