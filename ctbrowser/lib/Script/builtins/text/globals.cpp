// ctbrowser.script builtins - the two text-shaped globals: btoa/atob and
// structuredClone.
//
// One of four files carved out of a 1,333-line builtins/text.cpp on 2026-09-08
// - which was itself one of five carved out of builtins.cpp on 2026-08-09.
// Everything shared - the argument helpers, namespace detail, and these
// functions' declarations - is in ../internal.hpp; nothing is shared between
// these four alone, so there is no second header.

#include "../collections/typed_arrays/internal.hpp"
#include "../internal.hpp"
#include <ctbrowser/core/uri.hpp>

#include <algorithm>

namespace ctbrowser::script::builtins_detail {

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
    // THE SAME DECODER A data: URL GOES THROUGH (core/algorithms.hpp). These
    // were one loop retyped twice for a while, which is the shape of bug this
    // tree has already paid for once in its two URL parsers.
    cx.define_native("atob", [](context & c, std::span<value> a) {
        return c.string(base64_decode(a.empty() ? std::string{} : c.to_string(a[0])));
    });
    // The other four text-shaped globals, installed alongside.
    install_uri(cx);
}

// 19.2.6: encodeURI / encodeURIComponent / decodeURI / decodeURIComponent.
// Strings here are UTF-8 bytes already, so Encode is a byte loop: an
// unreserved ASCII byte passes, everything else is %XX. Decode folds %XX
// back - a malformed escape, or a decoded byte sequence that is not UTF-8,
// is a URIError - and decodeURI keeps the reserved set encoded.
void install_uri(context & cx) {
    static constexpr std::string_view unreserved =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.!~*'()";
    static constexpr std::string_view reserved = ";/?:@&=+$,#";
    const auto encoder = [](std::string_view keep_extra) {
        return [keep_extra](context & c, std::span<value> a) {
            const std::string in =
                string_arg(c, arg_at(a, 0)); // ToString: undefined is "undefined"
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
                string_arg(c, arg_at(a, 0)); // ToString: undefined is "undefined"
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
}

// HTML §2.7 STRUCTURED CLONE: StructuredSerializeInternal and
// StructuredDeserialize folded into one walk, since the copy never leaves this
// realm. What the specification serialises is copied here: the primitives, a
// primitive wrapper, Date, RegExp, ArrayBuffer and every view, Map, Set, an
// Error of the seven kinds, an Array (its length and its own enumerable
// properties, index or not) and a plain object's own enumerable string keys
// read through [[Get]] - so a getter runs, and one that throws propagates. A
// symbol, a function, a proxy and an object with a private-keyed slot this
// walk does not know are the DataCloneError, as is a detached buffer or an
// out-of-bounds view. The memory map keeps identity and cycles: two references
// to one object are two references to one copy.
//
// The error is a DOMException when the host defined one (lib/Shell does), so
// `assert_throws_dom("DataCloneError", ...)` sees `code` 25; without a host it
// is an Error named DataCloneError. Platform objects (Blob, File, ImageData)
// are the host's: this engine cannot tell one from a plain object and copies
// it as one.
namespace {

void throw_data_clone_error(context & c, std::string message) {
    const value dom_exception = c.global("DOMException");
    if (dom_exception.is_callable()) {
        const value args[2] = {c.string(message), c.string("DataCloneError")};
        const value made = c.construct(dom_exception, args);
        if (c.throw_pending()) { return; }
        c.throw_value(made);
        return;
    }
    c.throw_error("DataCloneError", std::move(message));
}

struct cloner {
    context & c;
    // THE COPIES ARE ROOTED THROUGH `held`: a copy is referenced from this C++
    // vector and from the copy of its parent, neither of which the collector
    // sees, and every constructor call below can collect.
    value held;
    std::vector<std::pair<heap_object *, value>> memory;
    std::size_t depth = 0;

    [[nodiscard]] value fail(std::string what) {
        throw_data_clone_error(c, "structuredClone cannot copy " + std::move(what));
        return value::undefined();
    }
    [[nodiscard]] value remember(value from, value to) {
        memory.emplace_back(from.as_heap(), to);
        static_cast<array_object *>(held.as_heap())->items.push_back(to);
        return to;
    }
    [[nodiscard]] value make(const char * ctor, std::span<const value> args) {
        const value callee = c.global(ctor);
        if (!callee.is_callable()) { return fail(std::string{"a "} + ctor); }
        return c.construct(callee, args);
    }

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
        // A primitive wrapper: Boolean, Number, String, BigInt. A Symbol
        // wrapper is the error a symbol is.
        if (const value * slot = primitive_slot(v)) {
            if (slot->is_kind(heap_kind::symbol)) { return fail("a Symbol object"); }
            return remember(v, detail::box_primitive(c, *slot));
        }
        if (const value * ms = source->find("__ms")) { // [[DateValue]]
            const value args[1] = {*ms};
            return remember(v, make("Date", args));
        }
        if (source->find("@#RegExpSource") != nullptr) { // [[RegExpMatcher]]
            const value args[2] = {c.lookup_property(v, "source"), c.lookup_property(v, "flags")};
            if (c.throw_pending()) { return value::undefined(); }
            return remember(v, make("RegExp", args));
        }
        if (array_object * store = buffer_store(v)) { return copy_buffer(v, store); }
        if (source->find(data_view_store_key) != nullptr) { return copy_data_view(v, source); }
        if (source->find("@#MapData") != nullptr) { return copy_keyed(v, source, true); }
        if (source->find("@#SetData") != nullptr) { return copy_keyed(v, source, false); }
        if (source->find("@#ErrorData") != nullptr) { return copy_error(v); }
        // Anything else carrying an internal slot - a WeakMap, a Promise, a
        // generator - is not serializable (StructuredSerializeInternal step
        // 20). A private FIELD (`@#name:class`) is an ordinary property the
        // walk below skips, as the specification's own-enumerable read does.
        for (const auto & [key, held] : source->props) {
            if (key.starts_with(private_key_prefix) && key.find(':') == std::string::npos) {
                return fail("an object of this kind");
            }
        }
        return copy_own_enumerable(v, remember(v, c.make_object()));
    }

    // An ArrayBuffer: a fresh one of the same length (and maxByteLength when
    // resizable) holding the same bytes. Detached is the DataCloneError.
    [[nodiscard]] value copy_buffer(value v, array_object * store) {
        if (store_detached(store)) { return fail("a detached ArrayBuffer"); }
        const value made = make_array_buffer(c, static_cast<double>(store->items.size()),
                                             store_max_byte_length(store));
        if (c.throw_pending() || !made.is_object()) { return value::undefined(); }
        buffer_store(made)->items = store->items;
        return remember(v, made);
    }

    // Step 18/26: `new Array(length)`, then its own enumerable properties -
    // the elements that are not holes and any named ones - as an object's.
    [[nodiscard]] value copy_array(value v, array_object * source) {
        const value made = c.make_array();
        (void)static_cast<array_object *>(made.as_heap())
            ->set_js_length(static_cast<double>(source->items.size()));
        return copy_own_enumerable(v, remember(v, made));
    }

    // Step 26: every own enumerable string key, in order, read through [[Get]]
    // (a getter runs; a throw propagates), then CreateDataProperty on the copy.
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

    // Steps 19 and 22: the entry list is read whole, then each key and value
    // is cloned and added through the copy's own `set`/`add`.
    [[nodiscard]] value copy_keyed(value v, object_object * source, bool map) {
        const value made = make(map ? "Map" : "Set", {});
        if (c.throw_pending()) { return value::undefined(); }
        (void)remember(v, made);
        const value * held = source->find("__entries");
        if (held == nullptr || !held->is_array()) { return made; }
        const std::vector<value> entries = static_cast<array_object *>(held->as_heap())->items;
        const context::rooted_values keep{c, entries};
        const value add = c.lookup_property(made, map ? "set" : "add");
        for (const value & entry : entries) {
            if (map) {
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

    // Step 17: name (one of the seven, else "Error"), an own data `message`,
    // `stack` when a string, and an own `cause` cloned - nothing else.
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
        if (stack.is_string()) { out->define("@#ErrorData", stack, attr_none); }
        context::property_descriptor cause;
        if (c.own_property(v, "cause", cause) && cause.is_data()) {
            const value copied = copy(cause.held);
            if (c.throw_pending()) { return value::undefined(); }
            out->define("cause", copied, attr_builtin);
        }
        return made;
    }

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

    [[nodiscard]] value copy_data_view(value v, object_object * source) {
        const value * held = source->find(data_view_store_key);
        auto * store = held != nullptr && held->is_array()
                           ? static_cast<array_object *>(held->as_heap())
                           : nullptr;
        if (store == nullptr || store_detached(store)) { return fail("a detached DataView"); }
        const value * buffer_slot = source->find(data_view_buffer_key);
        const value buffer =
            copy(buffer_slot != nullptr ? *buffer_slot : buffer_of_store(c, store));
        if (c.throw_pending()) { return value::undefined(); }
        const context::rooted keep{c, buffer};
        const value * offset = source->find(data_view_offset_key);
        const value * length = source->find(data_view_length_key);
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

void install_structured_clone(context & cx) {
    cx.define_native("structuredClone", [](context & c, std::span<value> a) {
        cloner walk{c, c.make_array(), {}, 0};
        const context::rooted keep{c, walk.held};
        return walk.copy(a.empty() ? value::undefined() : a[0]);
    });
}

} // namespace ctbrowser::script::builtins_detail
