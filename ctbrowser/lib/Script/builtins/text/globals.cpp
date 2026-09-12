// ctbrowser.script builtins - the two text-shaped globals: btoa/atob and
// structuredClone.
//
// One of four files carved out of a 1,333-line builtins/text.cpp on 2026-09-08
// - which was itself one of five carved out of builtins.cpp on 2026-08-09.
// Everything shared - the argument helpers, namespace detail, and these
// functions' declarations - is in ../internal.hpp; nothing is shared between
// these four alone, so there is no second header.

#include "../internal.hpp"

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
            const std::string in = str_at(c, a, 0);
            if (c.throw_pending()) { return value::undefined(); }
            std::string out;
            out.reserve(in.size());
            for (const char ch : in) {
                const auto byte = static_cast<unsigned char>(ch);
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
    const auto decoder = [](std::string_view keep_encoded) {
        return [keep_encoded](context & c, std::span<value> a) {
            const std::string in = str_at(c, a, 0);
            if (c.throw_pending()) { return value::undefined(); }
            std::string out;
            out.reserve(in.size());
            const auto refuse = [&] {
                c.throw_error("URIError", "URI malformed");
                return value::undefined();
            };
            for (std::size_t i = 0; i < in.size(); ++i) {
                if (in[i] != '%') {
                    out += in[i];
                    continue;
                }
                if (i + 2 >= in.size() || hex_value(in[i + 1]) < 0 || hex_value(in[i + 2]) < 0) {
                    return refuse();
                }
                const auto byte =
                    static_cast<unsigned char>(hex_value(in[i + 1]) * 16 + hex_value(in[i + 2]));
                if (byte < 0x80) {
                    if (keep_encoded.find(static_cast<char>(byte)) != std::string_view::npos) {
                        out.append(in, i, 3);
                    } else {
                        out += static_cast<char>(byte);
                    }
                    i += 2;
                    continue;
                }
                // A multi-byte sequence: the lead says how many continuation
                // escapes follow, and each must be one (steps 4.d.vii-x).
                const int n = (byte & 0xE0) == 0xC0   ? 2
                              : (byte & 0xF0) == 0xE0 ? 3
                              : (byte & 0xF8) == 0xF0 ? 4
                                                      : 0;
                if (n == 0 || byte < 0xC2 || byte > 0xF4 ||
                    i + static_cast<std::size_t>(n) * 3 > in.size()) {
                    return refuse();
                }
                std::string bytes{static_cast<char>(byte)};
                std::uint32_t code = byte & (0xFFu >> (n + 1));
                for (int k = 1; k < n; ++k) {
                    const std::size_t at = i + static_cast<std::size_t>(k) * 3;
                    if (in[at] != '%' || hex_value(in[at + 1]) < 0 || hex_value(in[at + 2]) < 0) {
                        return refuse();
                    }
                    const auto cont = static_cast<unsigned char>(hex_value(in[at + 1]) * 16 +
                                                                 hex_value(in[at + 2]));
                    if ((cont & 0xC0) != 0x80) { return refuse(); }
                    bytes += static_cast<char>(cont);
                    code = (code << 6) | (cont & 0x3F);
                }
                // Overlong, surrogate and out-of-range code points are not UTF-8.
                static constexpr std::uint32_t floor[5] = {0, 0, 0x80, 0x800, 0x10000};
                if (code < floor[n] || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
                    return refuse();
                }
                out += bytes;
                i += static_cast<std::size_t>(n) * 3 - 1;
            }
            return c.string(out);
        };
    };
    cx.define_native("encodeURIComponent", encoder(""));
    cx.define_native("encodeURI", encoder(reserved));
    cx.define_native("decodeURIComponent", decoder(""));
    cx.define_native("decodeURI", decoder(reserved));
    for (const char * name :
         {"encodeURIComponent", "encodeURI", "decodeURIComponent", "decodeURI"}) {
        auto * made = static_cast<native_object *>(cx.global(name).as_heap());
        made->is_constructor = false;
        detail::install_arity(cx, made, 1);
    }
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

} // namespace ctbrowser::script::builtins_detail
