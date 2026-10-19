//===- globals.cpp - Text-shaped globals ------------------------*- C++ -*-===//
//
// Global functions that are not tied to a constructor: btoa/atob,
// encodeURI(Component)/decodeURI(Component), escape/unescape and
// structuredClone. Shared helpers and these functions' declarations are in
// ../internal.hpp.
//
//===----------------------------------------------------------------------===//

#include "../collections/typed_arrays/internal.hpp"
#include "../internal.hpp"
#include <ctbrowser/core/uri.hpp>

#include <algorithm>

namespace ctbrowser::script::builtins_detail {

namespace {
/// Throws a DOMException named `name` when the host defined DOMException
/// (lib/Shell does), so `assert_throws_dom(name, ...)` sees its `code`;
/// without a host it throws an Error with that name. btoa and atob throw
/// InvalidCharacterError through it, structuredClone DataCloneError.
void throw_dom_exception(context & c, const char * name, std::string message) {
    const value dom_exception = c.global("DOMException");
    if (dom_exception.is_callable()) {
        const value args[2] = {c.string(message), c.string(name)};
        const value made = c.construct(dom_exception, args);
        if (c.throw_pending()) { return; }
        c.throw_value(made);
        return;
    }
    c.throw_error(name, std::move(message));
}
} // namespace

/// Installs `btoa` and `atob` (base64), then calls install_uri for the URI
/// functions and escape/unescape.
///
/// Both are byte-oriented: btoa's argument is a "binary string" (code units
/// 0-255, one per byte, Infra's isomorphic encode) and atob's answer is one
/// (its isomorphic decode). A unit above 0xFF is not a byte and btoa refuses
/// it; `btoa("\u00e9")` is "6Q==", the one byte E9, not the base64 of the two
/// bytes of its UTF-8.
void install_base64(context & cx) {
    cx.define_native("btoa", [](context & c, std::span<value> a) {
        const std::string text = a.empty() ? std::string{} : c.to_string(a[0]);
        std::string in;
        in.reserve(text.size());
        for (std::size_t at = 0; at < text.size();) {
            const char32_t unit = decode_utf8(text, at);
            if (unit > 0xFF) {
                throw_dom_exception(c, "InvalidCharacterError",
                                    "btoa: the string has a character outside Latin1");
                return value::undefined();
            }
            in += static_cast<char>(unit);
        }
        return c.string(base64_encode(in));
    });
    // atob is Infra's forgiving-base64 decode (4.9) and then an isomorphic
    // decode: ASCII whitespace is dropped, one or two `=` may close a length
    // that is a multiple of four, and after that a length leaving a remainder
    // of 1 or a character outside the alphabet is an InvalidCharacterError.
    // It decodes with the same base64_decode a data: URL uses
    // (core/algorithms.hpp); that decoder is lenient, so these strict checks
    // run first.
    cx.define_native("atob", [](context & c, std::span<value> a) {
        const std::string given = a.empty() ? std::string{"undefined"} : c.to_string(a[0]);
        std::string data;
        data.reserve(given.size());
        for (const char ch : given) {
            if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\f' && ch != '\r') { data += ch; }
        }
        if (data.size() % 4 == 0 && data.ends_with('=')) {
            data.pop_back();
            if (data.ends_with('=')) { data.pop_back(); }
        }
        const bool alphabet_only = std::all_of(data.begin(), data.end(), [](char ch) {
            return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                   (ch >= '0' && ch <= '9') || ch == '+' || ch == '/';
        });
        if (data.size() % 4 == 1 || !alphabet_only) {
            throw_dom_exception(c, "InvalidCharacterError",
                                "atob: the string is not correctly encoded");
            return value::undefined();
        }
        return c.string(isomorphic_decode(base64_decode(data)));
    });
    // The URI functions and escape/unescape are installed alongside.
    install_uri(cx);
}

/// Installs encodeURI, encodeURIComponent, decodeURI and decodeURIComponent
/// (ECMA-262 19.2.6), and Annex B's escape and unescape.
///
/// Strings here are UTF-8 bytes already, so Encode is a byte loop: an
/// unreserved ASCII byte passes, everything else is %XX. Decode folds %XX
/// back (a malformed escape, or a decoded byte sequence that is not UTF-8, is
/// a URIError), and decodeURI keeps the reserved set encoded.
void install_uri(context & cx) {
    static constexpr std::string_view unreserved =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.!~*'()";
    static constexpr std::string_view reserved = ";/?:@&=+$,#";
    const auto encoder = [](std::string_view keep_extra) {
        return [keep_extra](context & c, std::span<value> a) {
            const std::string in =
                string_arg(c, arg_at(a, 0)); // ToString: undefined is "undefined".
            if (c.throw_pending()) { return value::undefined(); }
            std::string out;
            out.reserve(in.size());
            for (std::size_t i = 0; i < in.size(); ++i) {
                const char ch = in[i];
                const auto byte = static_cast<unsigned char>(ch);
                // 19.2.6.5 Encode step 4.d: a lone surrogate is a URIError.
                // The text is WTF-8, so one is ED A0..BF xx.
                if (byte == 0xED && i + 1 < in.size() &&
                    static_cast<unsigned char>(in[i + 1]) >= 0xA0) {
                    c.throw_error("URIError", "URI malformed");
                    return value::undefined();
                }
                if (byte < 0x80 && (unreserved.find(ch) != std::string_view::npos ||
                                    keep_extra.find(ch) != std::string_view::npos)) {
                    out += ch;
                    continue;
                }
                out += '%';
                out += "0123456789ABCDEF"[byte >> 4];
                out += "0123456789ABCDEF"[byte & 0xF];
            }
            return c.string(out);
        };
    };
    const auto decoder = [](auto decode) {
        return [decode](context & c, std::span<value> a) {
            const std::string in =
                string_arg(c, arg_at(a, 0)); // ToString: undefined is "undefined".
            if (c.throw_pending()) { return value::undefined(); }
            const auto out = decode(in);
            if (!out) {
                c.throw_error("URIError", "URI malformed");
                return value::undefined();
            }
            return c.string(*out);
        };
    };
    cx.define_native("encodeURIComponent", encoder(""));
    cx.define_native("encodeURI", encoder(reserved));
    cx.define_native("decodeURIComponent", decoder(decode_uri_component));
    cx.define_native("decodeURI", decoder(decode_uri));
    for (const char * name :
         {"encodeURIComponent", "encodeURI", "decodeURIComponent", "decodeURI"}) {
        auto * made = static_cast<native_object *>(cx.global(name).as_heap());
        made->is_constructor = false;
        detail::install_arity(cx, made, 1);
    }
    // Annex B.2.1 escape and unescape: the pre-URI percent encoding, over
    // UTF-16 code units, not bytes: a unit below 256 is %XX and one above
    // is %uXXXX, so `escape("\u{10401}")` is the two surrogate halves
    // "%uD801%uDC01". Hence the round trip through wtf8_to_utf16: the WTF-8
    // bytes would have answered %F0%90%90%81, which is encodeURI's answer.
    detail::global_fn(cx, "escape", 1, [](context & c, std::span<value> a) {
        const context::stack_mark before = c.mark_stack();
        const std::string in = string_arg(c, arg_at(a, 0));
        if (detail::threw_since(c, before)) { return value::undefined(); }
        static constexpr std::string_view kept =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789@*_+-./";
        static constexpr std::string_view digits = "0123456789ABCDEF";
        std::string out;
        for (const char16_t unit : wtf8_to_utf16(in)) {
            if (unit < 0x80 && kept.find(static_cast<char>(unit)) != std::string_view::npos) {
                out += static_cast<char>(unit);
                continue;
            }
            out += '%';
            if (unit >= 256) {
                out += 'u';
                out += digits[(unit >> 12) & 0xF];
                out += digits[(unit >> 8) & 0xF];
            }
            out += digits[(unit >> 4) & 0xF];
            out += digits[unit & 0xF];
        }
        return c.string(out);
    });
    detail::global_fn(cx, "unescape", 1, [](context & c, std::span<value> a) {
        const context::stack_mark before = c.mark_stack();
        const std::string in = string_arg(c, arg_at(a, 0));
        if (detail::threw_since(c, before)) { return value::undefined(); }
        const std::u16string units = wtf8_to_utf16(in);
        // The hexadecimal value of `count` units from `at`, or -1 when one of
        // them is not a hex digit (a unit above 0x7F never is).
        const auto hex_run = [&units](std::size_t at, std::size_t count) {
            int total = 0;
            for (std::size_t i = at; i < at + count; ++i) {
                const int digit = units[i] < 0x80 ? hex_value(static_cast<char>(units[i])) : -1;
                if (digit < 0) { return -1; }
                total = total * 16 + digit;
            }
            return total;
        };
        std::u16string out;
        out.reserve(units.size());
        for (std::size_t k = 0; k < units.size(); ++k) {
            const char16_t unit = units[k];
            if (unit == u'%' && k + 6 <= units.size() && units[k + 1] == u'u') {
                if (const int four = hex_run(k + 2, 4); four >= 0) {
                    out += static_cast<char16_t>(four);
                    k += 5;
                    continue;
                }
            }
            if (unit == u'%' && k + 3 <= units.size()) {
                if (const int two = hex_run(k + 1, 2); two >= 0) {
                    out += static_cast<char16_t>(two);
                    k += 2;
                    continue;
                }
            }
            out += unit;
        }
        return c.string(utf16_to_wtf8(out));
    });
}

// HTML §2.7 structured clone: StructuredSerializeInternal and
// StructuredDeserialize folded into one walk, since the copy never leaves this
// realm. What the specification serialises is copied here: the primitives, a
// primitive wrapper, Date, RegExp, ArrayBuffer and every view, Map, Set, an
// Error of the seven kinds, an Array (its length and its own enumerable
// properties, index or not) and a plain object's own enumerable string keys
// read through [[Get]], so a getter runs, and one that throws propagates. A
// symbol, a function, a proxy and an object with a private-keyed slot this
// walk does not know are the DataCloneError, as is a detached buffer or an
// out-of-bounds view. The memory map keeps identity and cycles: two references
// to one object are two references to one copy.
//
// The error is a DOMException when the host defined one (lib/Shell does), so
// `assert_throws_dom("DataCloneError", ...)` sees `code` 25; without a host it
// is an Error named DataCloneError. A platform object is told by its chain: an
// instance of an interface the host installed (a builtin global that is not an
// intrinsic) is copied when the host named that interface serializable
// (context::set_serializable_interface) and refused otherwise.
namespace {

void throw_data_clone_error(context & c, std::string message) {
    throw_dom_exception(c, "DataCloneError", std::move(message));
}

/// One structuredClone walk: the memory map from originals to copies, and the
/// lazily built table of the host's platform interfaces.
struct cloner {
    context & c;
    /// A script array that roots every copy. A copy is otherwise referenced
    /// only from `memory` and from the copy of its parent, neither of which the
    /// collector sees, and every constructor call below can collect.
    value held;
    /// Original-to-copy pairs: preserves identity and cycles.
    std::vector<std::pair<heap_object *, value>> memory;
    /// Current recursion depth, capped in copy().
    std::size_t depth = 0;
    /// The host's registered interfaces, keyed by prototype, since their global
    /// bindings may have been deleted or replaced. Built the first time an
    /// object with an unusual prototype is met, so a plain array of numbers
    /// never pays for it.
    struct interface_entry {
        heap_object * key;
        value prototype;
        std::string name;
    };
    std::vector<interface_entry> interfaces;
    bool interfaces_built = false;

    void build_interfaces() {
        interfaces_built = true;
        realm_record & realm = c.current_realm();
        for (const auto & [name, prototype] : realm.platform_prototypes) {
            // Legacy factory aliases can share an interface prototype.
            if (std::ranges::any_of(interfaces, [&](const interface_entry & known) {
                    return known.key == prototype.as_heap();
                })) {
                continue;
            }
            interfaces.push_back(interface_entry{prototype.as_heap(), prototype, name});
            static_cast<array_object *>(held.as_heap())->items.push_back(prototype);
        }
    }

    /// The interface of the nearest host interface prototype on `v`'s chain.
    [[nodiscard]] const interface_entry * platform_interface(value v) {
        value up = static_cast<object_object *>(v.as_heap())->prototype;
        const object_object * plain = c.prototype(context::proto_kind::object);
        if (!up.is_object() || up.as_heap() == plain) { return nullptr; }
        if (!interfaces_built) { build_interfaces(); }
        for (int level = 0; level < 64 && up.is_object() && up.as_heap() != plain; ++level) {
            for (const interface_entry & entry : interfaces) {
                if (entry.key == up.as_heap()) { return &entry; }
            }
            up = static_cast<object_object *>(up.as_heap())->prototype;
        }
        return nullptr;
    }

    /// A [Serializable] platform object's serialization steps: copies the own
    /// properties its interface names as state
    /// (context::set_serializable_interface), internal ones included, which is
    /// where a binding keeps what a script cannot see (a Blob's bytes, a
    /// FileList's files), and no others. A page's expando is not the platform
    /// object's state. Each value is cloned in turn; a function or an accessor
    /// is the platform's own and is shared, not copied.
    [[nodiscard]] value copy_platform(value v, object_object * source, value prototype,
                                      const std::vector<std::string> & state) {
        const value made = c.make_object();
        auto * out = static_cast<object_object *>(made.as_heap());
        out->prototype = prototype;
        (void)remember(v, made);
        std::vector<std::pair<std::string, std::uint8_t>> keys;
        source->each_own_entry_in_order([&](const std::string & key, std::uint8_t a) {
            if (std::ranges::find(state, key) != state.end()) { keys.emplace_back(key, a); }
        });
        for (const auto & [key, a] : keys) {
            if (accessor_entry * pair = source->find_accessor(key)) {
                out->define_accessor(key, pair->getter, pair->setter, a);
                continue;
            }
            const value * item = source->find(key);
            if (item == nullptr) { continue; }
            const value copied = item->is_callable() ? *item : copy(*item);
            if (c.throw_pending()) { return value::undefined(); }
            out->define(key, copied, a);
        }
        return made;
    }

    /// Throws a DataCloneError naming `what`; returns undefined.
    [[nodiscard]] value fail(std::string what) {
        throw_data_clone_error(c, "structuredClone cannot copy " + std::move(what));
        return value::undefined();
    }
    /// Records `to` as the copy of `from` and roots it; returns `to`.
    [[nodiscard]] value remember(value from, value to) {
        memory.emplace_back(from.as_heap(), to);
        static_cast<array_object *>(held.as_heap())->items.push_back(to);
        return to;
    }
    /// Constructs the global constructor `ctor` with `args`.
    [[nodiscard]] value make(const char * ctor, std::span<const value> args) {
        const value callee = c.global(ctor);
        if (!callee.is_callable()) { return fail(std::string{"a "} + ctor); }
        return c.construct(callee, args);
    }

    /// Returns the clone of `v`: a primitive as it is, an already-copied
    /// object's existing copy, otherwise a fresh copy (copy_heap).
    [[nodiscard]] value copy(value v) {
        if (!v.is_heap() || v.is_string() || v.is_kind(heap_kind::bigint)) { return v; }
        if (v.is_kind(heap_kind::symbol)) { return fail("a symbol"); }
        if (v.is_callable()) { return fail("a function"); }
        if (!v.is_object() && !v.is_array()) { return fail(std::string{context::type_of(v)}); }
        for (const auto & [from, to] : memory) {
            if (from == v.as_heap()) { return to; }
        }
        // Bounded so a deeply nested (but not self-referential) structure fails
        // rather than recursing until the native stack overflows; the memory
        // map only short-circuits genuine cycles.
        constexpr std::size_t max_depth = 1000;
        if (depth >= max_depth) { return fail("a structure nested this deeply"); }
        const context::rooted keep{c, v};
        ++depth;
        const value out = copy_heap(v);
        --depth;
        return out;
    }

    [[nodiscard]] value copy_heap(value v) {
        if (v.is_array()) {
            auto * source = static_cast<array_object *>(v.as_heap());
            if (source->elements != element_kind::none) { return copy_typed_array(v, source); }
            return copy_array(v, source);
        }
        auto * source = static_cast<object_object *>(v.as_heap());
        // A platform object (steps 25-26) is checked first: an instance of one
        // of the host's interfaces is copied when the interface is
        // [Serializable], as the closest serializable interface on its chain,
        // and is the DataCloneError otherwise (a Response, a Node, an
        // AbortSignal). First because a host object may carry a slot an intrinsic is recognised by
        // below: a Blob keeps its bytes under `__bytes`, which is what an
        // ArrayBuffer's store is found by (buffer_store).
        if (const interface_entry * platform = platform_interface(v)) {
            const std::vector<std::string> * state = c.serializable_interface(platform->name);
            if (state == nullptr) { return fail("a " + platform->name); }
            return copy_platform(v, source, platform->prototype, *state);
        }
        // A primitive wrapper: Boolean, Number, String, BigInt. A Symbol
        // wrapper is the error a symbol is.
        if (const value * slot = primitive_slot(v)) {
            if (slot->is_kind(heap_kind::symbol)) { return fail("a Symbol object"); }
            return remember(v, c.box_primitive(*slot));
        }
        if (const value * ms = source->find(date_value_key)) { // [[DateValue]]
            const value args[1] = {*ms};
            return remember(v, make("Date", args));
        }
        if (source->find(CTBROWSER_PRIVATE_KEY("RegExpSource")) != nullptr) { // [[RegExpMatcher]]
            const value args[2] = {c.lookup_property(v, "source"), c.lookup_property(v, "flags")};
            if (c.throw_pending()) { return value::undefined(); }
            return remember(v, make("RegExp", args));
        }
        if (array_object * store = buffer_store(v)) { return copy_buffer(v, store); }
        if (source->find(data_view_store_key) != nullptr) { return copy_data_view(v, source); }
        if (source->find(CTBROWSER_PRIVATE_KEY("MapData")) != nullptr) {
            return copy_keyed(v, source, true);
        }
        if (source->find(CTBROWSER_PRIVATE_KEY("SetData")) != nullptr) {
            return copy_keyed(v, source, false);
        }
        if (source->find(CTBROWSER_PRIVATE_KEY("ErrorData")) != nullptr) { return copy_error(v); }
        // Anything else carrying an internal slot (a WeakMap, a Promise, a
        // generator) is not serializable (StructuredSerializeInternal step
        // 20). A private field (`@#name:class`) is an ordinary property the
        // walk below skips, as the specification's own-enumerable read does.
        for (const auto & [key, held] : source->props) {
            if (key.starts_with(private_key_prefix) && key.find(':') == std::string::npos) {
                return fail("an object of this kind");
            }
        }
        return copy_own_enumerable(v, remember(v, c.make_object()));
    }

    /// An ArrayBuffer: a fresh one of the same length (and maxByteLength when
    /// resizable) holding the same bytes. Detached is the DataCloneError.
    [[nodiscard]] value copy_buffer(value v, array_object * store) {
        if (store_detached(store)) { return fail("a detached ArrayBuffer"); }
        const value made = make_array_buffer(c, static_cast<double>(store->items.size()),
                                             store_max_byte_length(store));
        if (c.throw_pending() || !made.is_object()) { return value::undefined(); }
        buffer_store(made)->items = store->items;
        return remember(v, made);
    }

    /// Steps 18 and 26: `new Array(length)`, then its own enumerable properties
    /// (the elements that are not holes and any named ones), as an object's.
    [[nodiscard]] value copy_array(value v, array_object * source) {
        const value made = c.make_array();
        auto * out = static_cast<array_object *>(made.as_heap());
        // Every index a hole until the walk below copies an element into it,
        // so a hole in the source stays one.
        (void)out->set_js_length_with_holes(static_cast<double>(source->items.size()));
        return copy_own_enumerable(v, remember(v, made));
    }

    /// Step 26: every own enumerable string key, in order, read through [[Get]]
    /// (a getter runs; a throw propagates), then CreateDataProperty on the copy.
    [[nodiscard]] value copy_own_enumerable(value v, value made) {
        for (const std::string & key : detail::own_property_names(c, v)) {
            context::property_descriptor d;
            if (!c.own_property(v, key, d) || !d.enumerable) { continue; }
            const value item = c.lookup_property(v, key);
            if (c.throw_pending()) { return value::undefined(); }
            const value copied = copy(item);
            if (c.throw_pending()) { return value::undefined(); }
            if (made.is_array()) {
                c.store_index(made, c.string(key), copied);
            } else {
                static_cast<object_object *>(made.as_heap())->set(key, copied);
            }
            if (c.throw_pending()) { return value::undefined(); }
        }
        return made;
    }

    /// Steps 19 and 22: the entry list is read whole, then each key and value
    /// is cloned and added through the copy's own `set`/`add`.
    [[nodiscard]] value copy_keyed(value v, object_object * source, bool map) {
        const value made = make(map ? "Map" : "Set", {});
        if (c.throw_pending()) { return value::undefined(); }
        (void)remember(v, made);
        const value * held = source->find(entries_slot_key);
        if (held == nullptr || !held->is_array()) { return made; }
        const std::vector<value> entries = static_cast<array_object *>(held->as_heap())->items;
        const context::rooted_values keep{c, entries};
        const value add = c.lookup_property(made, map ? "set" : "add");
        for (const value & entry : entries) {
            if (map) {
                // The list is the engine's own, but a pair is still checked
                // before its two items are read.
                if (!entry.is_array() ||
                    static_cast<array_object *>(entry.as_heap())->items.size() < 2) {
                    continue;
                }
                auto * pair = static_cast<array_object *>(entry.as_heap());
                const value key = copy(pair->items[0]);
                if (c.throw_pending()) { return value::undefined(); }
                const context::rooted keep_key{c, key};
                const value item = copy(pair->items[1]);
                if (c.throw_pending()) { return value::undefined(); }
                const value args[2] = {key, item};
                (void)c.call(add, args, made);
            } else {
                const value item = copy(entry);
                if (c.throw_pending()) { return value::undefined(); }
                const value args[1] = {item};
                (void)c.call(add, args, made);
            }
            if (c.throw_pending()) { return value::undefined(); }
        }
        return made;
    }

    /// Step 17: name (one of the seven, else "Error"), an own data `message`,
    /// `stack` when a string, and an own `cause` cloned; nothing else.
    [[nodiscard]] value copy_error(value v) {
        std::string name = c.to_string(c.lookup_property(v, "name"));
        if (c.throw_pending()) { return value::undefined(); }
        static constexpr std::string_view kinds[] = {"Error",          "EvalError",   "RangeError",
                                                     "ReferenceError", "SyntaxError", "TypeError",
                                                     "URIError"};
        if (std::find(std::begin(kinds), std::end(kinds), name) == std::end(kinds)) {
            name = "Error";
        }
        context::property_descriptor message;
        const bool has_message = c.own_property(v, "message", message) && message.is_data();
        std::string text;
        if (has_message) {
            text = c.to_string(message.held);
            if (c.throw_pending()) { return value::undefined(); }
        }
        const value made = c.make_error(name, text);
        auto * out = static_cast<object_object *>(made.as_heap());
        if (!has_message) { (void)out->erase("message"); }
        (void)remember(v, made);
        const value stack = c.lookup_property(v, "stack");
        if (c.throw_pending()) { return value::undefined(); }
        if (stack.is_string()) {
            out->define(CTBROWSER_PRIVATE_KEY("ErrorData"), stack, attr_none);
        }
        context::property_descriptor cause;
        if (c.own_property(v, "cause", cause) && cause.is_data()) {
            const value copied = copy(cause.held);
            if (c.throw_pending()) { return value::undefined(); }
            out->define("cause", copied, attr_builtin);
        }
        return made;
    }

    /// A typed array: an owning one is copied element by element; a view gets
    /// its buffer copied through the memory map and the same window over it.
    [[nodiscard]] value copy_typed_array(value v, array_object * source) {
        if (typed_array_out_of_bounds(source)) { return fail("an out-of-bounds TypedArray"); }
        const value ctor = typed_array_constructor(c, source->elements);
        if (!source->is_view()) {
            // Owning its elements: a fresh one of the same kind and length,
            // element by element (the values are primitives or bigints).
            const value args[1] = {value::number(static_cast<double>(source->items.size()))};
            const value made = c.construct(ctor, args);
            if (c.throw_pending() || !made.is_array()) { return value::undefined(); }
            static_cast<array_object *>(made.as_heap())->items = source->items;
            return remember(v, made);
        }
        // A view: its buffer through the memory map, so two views of one
        // buffer share one copy, then the same window over it.
        const value buffer = copy(buffer_of_store(c, typed_array_store(source)));
        if (c.throw_pending()) { return value::undefined(); }
        const context::rooted keep{c, buffer};
        const value offset = value::number(static_cast<double>(source->byte_offset));
        if (typed_array_length_tracking(source)) {
            const value args[2] = {buffer, offset};
            return remember(v, c.construct(ctor, args));
        }
        const value args[3] = {buffer, offset,
                               value::number(static_cast<double>(typed_array_length(source)))};
        return remember(v, c.construct(ctor, args));
    }

    /// A DataView: its buffer copied through the memory map and a new view
    /// with the same offset and length.
    [[nodiscard]] value copy_data_view(value v, object_object * source) {
        const value * held = source->find(data_view_store_key);
        auto * store = held != nullptr && held->is_array()
                           ? static_cast<array_object *>(held->as_heap())
                           : nullptr;
        if (store == nullptr || store_detached(store)) { return fail("a detached DataView"); }
        const value * offset = source->find(data_view_offset_key);
        const value * length = source->find(data_view_length_key);
        // IsViewOutOfBounds (25.3.1.5): a resizable buffer shrunk below the
        // view's window is the DataCloneError of step 13.c, not the
        // RangeError the DataView constructor would throw below.
        const double start = offset != nullptr && offset->is_number() ? offset->as_number() : 0;
        const double end =
            start + (length != nullptr && length->is_number() ? length->as_number() : 0);
        if (start > static_cast<double>(store->items.size()) ||
            end > static_cast<double>(store->items.size())) {
            return fail("an out-of-bounds DataView");
        }
        const value * buffer_slot = source->find(data_view_buffer_key);
        const value buffer =
            copy(buffer_slot != nullptr ? *buffer_slot : buffer_of_store(c, store));
        if (c.throw_pending()) { return value::undefined(); }
        const context::rooted keep{c, buffer};
        const value off = offset != nullptr ? *offset : value::number(0);
        if (length == nullptr || length->is_undefined()) {
            const value args[2] = {buffer, off};
            return remember(v, make("DataView", args));
        }
        const value args[3] = {buffer, off, *length};
        return remember(v, make("DataView", args));
    }
};

} // namespace

/// Installs `structuredClone`: StructuredSerializeWithTransfer (HTML 2.7.6) and
/// the deserialization after it, for `structuredClone(value, { transfer })`.
/// Every transferable is checked first (an ArrayBuffer, not detached, not
/// listed twice; this engine has no transferable platform object, so anything
/// else is the DataCloneError), and its copy is made up front and put in the
/// memory map, so a reference to it anywhere in `value` is a reference to the
/// one copy. The bytes move and the original is detached only once the walk
/// succeeded.
void install_structured_clone(context & cx) {
    cx.define_native("structuredClone", [](context & c, std::span<value> a) {
        cloner walk{c, c.make_array(), {}, 0, {}, false};
        const context::rooted keep{c, walk.held};
        std::vector<std::pair<array_object *, array_object *>> moved;
        const value options = arg_at(a, 1);
        if (!options.is_nullish() && !options.is_object_like()) {
            c.throw_error("TypeError", "structuredClone: the options are not an object");
            return value::undefined();
        }
        const value list =
            options.is_nullish() ? value::undefined() : c.lookup_property(options, "transfer");
        if (c.throw_pending()) { return value::undefined(); }
        if (!list.is_undefined()) {
            // sequence<object> (WebIDL 3.2.28): an iterable of objects.
            if (!list.is_object_like()) {
                c.throw_error("TypeError", "structuredClone: `transfer` is not iterable");
                return value::undefined();
            }
            const context::stack_mark before = c.mark_stack();
            const value items = c.iterable_values(list);
            if (c.throw_pending() || detail::threw_since(c, before)) { return value::undefined(); }
            static_cast<array_object *>(walk.held.as_heap())->items.push_back(items);
            for (const value & each : static_cast<array_object *>(items.as_heap())->items) {
                if (!each.is_object_like()) {
                    c.throw_error("TypeError", "structuredClone: a transferable is not an object");
                    return value::undefined();
                }
                // A platform object (a Blob answers buffer_store too) has
                // no transfer steps here: no interface of this host is
                // [Transferable].
                array_object * store = each.is_object() && walk.platform_interface(each) != nullptr
                                           ? nullptr
                                           : buffer_store(each);
                if (store == nullptr) { return walk.fail("a transfer of this object"); }
                if (std::ranges::any_of(walk.memory, [&](const auto & known) {
                        return known.first == each.as_heap();
                    })) {
                    return walk.fail("the same ArrayBuffer twice in a transfer list");
                }
                if (store_detached(store)) { return walk.fail("a detached ArrayBuffer"); }
                const value made = make_array_buffer(c, static_cast<double>(store->items.size()),
                                                     store_max_byte_length(store));
                if (c.throw_pending() || !made.is_object()) { return value::undefined(); }
                (void)walk.remember(each, made);
                moved.emplace_back(store, buffer_store(made));
            }
        }
        const value out = walk.copy(a.empty() ? value::undefined() : a[0]);
        if (c.throw_pending()) { return value::undefined(); }
        const context::rooted keep_out{c, out};
        for (const auto & [from, to] : moved) {
            if (store_detached(from)) { return walk.fail("a detached ArrayBuffer"); }
        }
        for (const auto & [from, to] : moved) {
            if (to->items.size() != from->items.size()) { resize_store(c, to, from->items.size()); }
            std::copy(from->items.begin(), from->items.end(), to->items.begin());
            detach_store(c, from);
        }
        return out;
    });
}

} // namespace ctbrowser::script::builtins_detail
