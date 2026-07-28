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

// `then`/`catch`/`finally` all reduce to: pick the callback matching how this
// promise settled, run it, and settle the result the same way. One function, so
// `then(f, g)` and `catch(g)` cannot disagree about what "rejected" means.
inline value settle_with(context & cx, value on_ok, value on_err) {
    const value self = cx.current_this();
    if (!self.is_object()) { return self; }
    auto * promise = static_cast<object_object *>(self.as_heap());
    value * held = promise->find("__value");
    value * state = promise->find("__rejected");
    const value settled = held != nullptr ? *held : value::undefined();
    const bool rejected = state != nullptr && context::truthy(*state);

    const value handler = rejected ? on_err : on_ok;
    // No handler for how this settled: the promise passes straight through, so
    // `p.then(f)` on a rejected p stays rejected and a later `.catch` sees it.
    if (!handler.is_callable()) { return self; }
    const value args[1] = {settled};
    return make_promise(cx, cx.call(handler, args), false);
}

[[nodiscard]] inline value make_promise(context & cx, value v, bool rejected) {
    object_object * promise = new_table(cx);
    promise->set("__value", v);
    promise->set("__rejected", value::boolean(rejected));
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
    object_object * array_proto = new_table(cx);
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
    object_object * number_proto = new_table(cx);
    method(cx, number_proto, "toFixed", [](context & c, std::span<value> a) {
        const double self = context::to_number(c.current_this());
        const auto digits = static_cast<int>(std::clamp(num_at(a, 0), 0.0, 20.0));
        std::string out(64, '\0');
        const int written = std::snprintf(out.data(), out.size(), "%.*f", digits, self);
        out.resize(written > 0 ? static_cast<std::size_t>(written) : 0);
        return c.string(out);
    });
    method(cx, number_proto, "toString",
           [](context & c, std::span<value>) { return c.string(c.to_string(c.current_this())); });
    cx.set_prototype(context::proto_kind::number, number_proto);
}

// Object
void install_object(context & cx) {
    using detail::method;
    using detail::new_table;
    object_object * object_ctor = new_table(cx);
    method(cx, object_ctor, "keys", [](context & c, std::span<value> a) {
        value out = c.make_array();
        auto * result = static_cast<array_object *>(out.as_heap());
        if (arg_at(a, 0).is_object()) {
            for (const auto & [key, item] : static_cast<object_object *>(a[0].as_heap())->props) {
                result->items.push_back(c.string(key));
            }
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
    cx.define_global("Promise", value::object(promise_ctor));

    cx.define_native("isNaN", [](context &, std::span<value> a) {
        return value::boolean(std::isnan(num_at(a, 0)));
    });
    cx.define_native("isFinite", [](context &, std::span<value> a) {
        return value::boolean(std::isfinite(num_at(a, 0)));
    });
    cx.define_native("String", [](context & c, std::span<value> a) {
        return c.string(a.empty() ? std::string{} : c.to_string(a[0]));
    });
    cx.define_native("Number", [](context &, std::span<value> a) {
        return value::number(a.empty() ? 0.0 : context::to_number(a[0]));
    });
    cx.define_native("Boolean", [](context &, std::span<value> a) {
        return value::boolean(!a.empty() && context::truthy(a[0]));
    });
}

} // namespace

// Everything the standard library defines, in the order it defines it.
void install_builtins(context & cx, std::uint64_t seed) {
    install_math(cx, seed);
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
