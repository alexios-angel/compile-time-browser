#pragma once
// Private to lib/Script/builtins/. NOT installed and in no file set:
// include/ctbrowser/script/builtins.hpp still declares exactly one function,
// install_builtins(), which is the entire public surface of the standard
// library. This header exists only so the implementation can be more than one
// file - it was 4,118 lines in one until 2026-08-09.

#include <algorithm>
#include <charconv>
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

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/script/bigint.hpp>
#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/number_format.hpp>
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

// ToIntegerOrInfinity, 7.1.5 - the coercion EVERY string and array index is
// specified to go through, and the one this file did not have.
//
// NaN BECOMES ZERO, and that is the whole point. `"abc".at(NaN)` used to cast
// NaN straight to `std::size_t`, which is undefined behaviour, and the engine
// HUNG rather than answering - so a page calling `s.at(x)` with an undefined
// `x` froze, with no error and nothing in the console. A missing argument is
// `undefined` and ToNumber(undefined) is NaN, so this path is reached far more
// often than a literal NaN would suggest: `.at()`, `.at(undefined)` and
// `.at({})` all landed on it, as did `.substr(NaN)`.
//
// Truncation is TOWARD ZERO, not floor: `"abc".at(-0.5)` is `at(0)`, not
// `at(-1)`.
[[nodiscard]] inline double index_at(std::span<value> args, std::size_t i) {
    const double n = i < args.size() ? context::to_number(args[i]) : 0.0;
    return std::isnan(n) ? 0.0 : std::trunc(n);
}

// Was an OPTIONAL index supplied at all? Absent and an explicit `undefined`
// mean the same thing, and testing the argument COUNT alone gets that wrong:
// `"abc".slice(1, undefined)` is "bc" because the end defaults to the length,
// but a count test sees two arguments, coerces `undefined` to 0 and returns "".
[[nodiscard]] inline bool has_index(std::span<value> args, std::size_t i) {
    return i < args.size() && !args[i].is_undefined();
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

// thisNumberValue (21.1.3), WHICH THIS FILE DID NOT HAVE - and the cycle that
// cost 45 of test262's 54 SIGSEGVs.
//
// Every Number.prototype method opened with `context::to_number(current_this())`,
// the STATIC coercion, which answers NaN for an object receiver. `toString` and
// `toPrecision` then fell back to `c.to_string(c.current_this())` for the NaN
// case - and ToString of an object is ToPrimitive, which calls the receiver's
// own `toString`, which is this native again:
//
//     Number.prototype.toString()      // `this` is Number.prototype, an object
//       -> to_string -> to_primitive_string -> invoke -> to_string -> ...
//
// The specification does not coerce here at all: `this` is a Number or an
// object with a [[NumberData]] slot, and anything else is a TypeError. There
// are no wrapper objects in this engine (`new Number(x)` is a conversion - see
// install_number), so the only object with [[NumberData]] is `Number.prototype`
// itself, whose slot is +0 by 21.1.3 - which is exactly why
// `Number.prototype.toString()` is specified to return "0".
[[nodiscard]] inline double this_number_value(context & cx, const char * method) {
    const value self = cx.current_this();
    if (self.is_number()) { return self.as_number(); }
    if (self.is_object() && self.as_heap() == cx.prototype(context::proto_kind::number)) {
        return 0.0;
    }
    cx.throw_error("TypeError", std::string{method} + " requires that 'this' be a Number");
    return std::nan("");
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
            // A SYMBOL-KEYED PROPERTY IS INVISIBLE TO JSON. This engine spells
            // a symbol key as "@@sym:N:description" and keeps it in the ordinary
            // property table, so the prefix is what identifies one here - and
            // without this the internal spelling was serialised into the page's
            // own data.
            if (key.starts_with(symbol_key_prefix)) { continue; }
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
    // NaN AND THE INFINITIES ARE NOT JSON. 25.5.2 serialises every non-finite
    // number as null, and emitting the bare words instead produces output that
    // NO JSON PARSER WILL READ BACK - so a page round-tripping its own data
    // through JSON.parse got a SyntaxError from bytes this engine wrote. That
    // is a data fault rather than a conformance nicety, which is why it is
    // worth the two lines.
    if (v.is_number() && !std::isfinite(v.as_number())) {
        out += "null";
        return;
    }
    // A BIGINT IS NOT JSON and there is no lossless spelling for one, so 25.5.2
    // throws rather than picking between a string and a rounded number.
    if (v.is_kind(heap_kind::bigint)) {
        cx.throw_error("TypeError", "Do not know how to serialize a BigInt");
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
        // from_chars, NOT strtod: strtod respects LC_NUMERIC, so on a host whose
        // locale writes decimals with a comma `JSON.parse("{\"n\":1.5}")` would
        // stop at the dot and read 1. Goldens are byte-compared across
        // platforms, so a locale-sensitive parser is a portability bug waiting
        // for the first machine that has one.
        const std::string_view digits = text.substr(start, at - start);
        double parsed = 0.0;
        std::from_chars(digits.data(), digits.data() + digits.size(), parsed);
        return value::number(parsed);
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

// The install_* functions, one per global the standard library defines.
//
// THESE WERE IN AN ANONYMOUS NAMESPACE when they all shared one translation
// unit. Spread across five, they need external linkage and one shared
// declaration - and `builtins_detail` rather than plain `script` so that
// nothing here can collide with a name another subsystem defines.
//
// The split is safe for the reason the original file states about itself: each
// of these "builds one table and defines one global, and none of them reads
// anything the others wrote".
namespace builtins_detail {

void install_math(context & cx, std::uint64_t seed);
void install_array(context & cx);
void install_string(context & cx);
void install_base64(context & cx);
void install_structured_clone(context & cx);
void install_boolean(context & cx);
void install_number(context & cx);
void install_object(context & cx);
void install_json(context & cx);
void install_date(context & cx);
void install_globals(context & cx);
void install_promise(context & cx);
void install_regexp(context & cx);
void install_symbol(context & cx);
void install_collections(context & cx);
void install_errors(context & cx);
void install_proxy(context & cx);
void install_function(context & cx);
void install_typed_arrays(context & cx);
void install_dynamic_function(context & cx);
void install_generator(context & cx);

// Used by more than one of those, so defined once here rather than duplicated.
// inline, because a header five translation units include may not define a
// function once per unit.
// `Type.prototype.constructor === Type`, and `Type.name` is its name.
//
// Both are how code identifies a value without trusting `instanceof` - which a
// page can defeat, and which does not work across realms.
// `Object.getPrototypeOf(x).constructor.name` is the standard walk, and with
// either half missing it yields undefined, which compares equal to the other
// undefined it is being tested against and reports a false match.
inline void link_constructor(context & cx, object_object * table, const char * name, value ctor) {
    if (table == nullptr) { return; }
    table->set("constructor", ctor);
    if (ctor.is_kind(heap_kind::native)) {
        static_cast<native_object *>(ctor.as_heap())->set("name", cx.string(name));
    } else if (ctor.is_object()) {
        static_cast<object_object *>(ctor.as_heap())->set("name", cx.string(name));
    }
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
inline void define_one(context & cx, value target, const std::string & key,
                       object_object * descriptor) {
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

} // namespace builtins_detail

} // namespace ctbrowser::script
