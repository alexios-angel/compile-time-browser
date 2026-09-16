// `TextEncoder` and `TextDecoder` - the Encoding Standard §8, as bindings over
// plain byte loops. Nothing in the engine had them, so a page that reached for
// either got "TextDecoder is not defined": every file of WPT's encoding/ (1,261
// of them) timed out on the first line, and url/percent-encoding.window.js died
// before its first assertion.
//
// WHAT IS HERE, WHOLE: the UTF-8 decoder of §4.2 as its state machine (so a
// truncated sequence is one U+FFFD and not one per byte), UTF-16LE and UTF-16BE
// with their surrogate pairing, and the twenty-seven legacy SINGLE-BYTE indexes
// the Standard publishes, generated into encoding_tables.inc by
// tools/gen/encoding_tables.py. `fatal`, `ignoreBOM`, `stream: true` and
// `encodeInto` are each the specification's algorithm.
//
// WHAT IS NOT: the legacy MULTI-byte encodings - Big5, EUC-JP, EUC-KR, GBK,
// gb18030, Shift_JIS. Their indexes are tens of thousands of entries and no
// part of this engine reads one yet; `new TextDecoder("big5")` is a RangeError
// that names the gap rather than a decoder that quietly returns mojibake.
// TextDecoderStream and TextEncoderStream want the Streams API and are not here
// either.
//
// STATE LIVES IN PRIVATE SLOTS, the device the rest of bindings/ uses: this
// engine has no internal-slot mechanism, so a decoder's encoding, its two flags
// and its half-finished sequence sit under names no author writes.

#include "internal.hpp"

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/net/url.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace ctbrowser::shell {

namespace {

constexpr std::string_view encoding_key = "__ctbrowser_decoder_encoding";
constexpr std::string_view fatal_key = "__ctbrowser_decoder_fatal";
constexpr std::string_view ignore_bom_key = "__ctbrowser_decoder_ignore_bom";
// The streaming decoder's carry: the bytes of a sequence that the last call
// ended in the middle of, and whether a BOM has already been looked for.
constexpr std::string_view pending_key = "__ctbrowser_decoder_pending";
constexpr std::string_view bom_seen_key = "__ctbrowser_decoder_bom_seen";
constexpr std::string_view encoder_key = "__ctbrowser_encoder";

#include "encoding_tables.inc"

// --- the label table, §4.1 -------------------------------------------------
//
// A LABEL IS NOT A NAME. `latin1`, `us-ascii` and `iso-8859-1` are all
// windows-1252, and getting that wrong is a whole class of mojibake. The
// Standard's table is data; it is spelled out here because it is short, unlike
// the indexes above.

enum class encoding_id : std::uint8_t {
    utf_8,
    utf_16le,
    utf_16be,
    x_user_defined,
    // Anything at or past `single_byte` indexes single_byte_indexes, in the
    // order tools/gen/encoding_tables.py lists.
    single_byte,
};

struct encoding_row {
    const char * name; // the Standard's name, which is what `encoding` answers
    encoding_id id;
    std::uint8_t index;  // into single_byte_indexes, for `single_byte`
    const char * labels; // space-separated, lower case
};

constexpr encoding_row encodings[] = {
    {"utf-8", encoding_id::utf_8, 0,
     "unicode-1-1-utf-8 unicode11utf8 unicode20utf8 utf-8 utf8 x-unicode20utf8"},
    {"utf-16le", encoding_id::utf_16le, 0,
     "csunicode iso-10646-ucs-2 ucs-2 unicode unicodefeff utf-16 utf-16le"},
    {"utf-16be", encoding_id::utf_16be, 0, "unicodefffe utf-16be"},
    {"x-user-defined", encoding_id::x_user_defined, 0, "x-user-defined"},
    {"IBM866", encoding_id::single_byte, 0, "866 cp866 csibm866 ibm866"},
    {"ISO-8859-2", encoding_id::single_byte, 1,
     "csisolatin2 iso-8859-2 iso-ir-101 iso8859-2 iso88592 iso_8859-2 iso_8859-2:1987 l2 latin2"},
    {"ISO-8859-3", encoding_id::single_byte, 2,
     "csisolatin3 iso-8859-3 iso-ir-109 iso8859-3 iso88593 iso_8859-3 iso_8859-3:1988 l3 latin3"},
    {"ISO-8859-4", encoding_id::single_byte, 3,
     "csisolatin4 iso-8859-4 iso-ir-110 iso8859-4 iso88594 iso_8859-4 iso_8859-4:1988 l4 latin4"},
    {"ISO-8859-5", encoding_id::single_byte, 4,
     "csisolatincyrillic cyrillic iso-8859-5 iso-ir-144 iso8859-5 iso88595 iso_8859-5 "
     "iso_8859-5:1988"},
    {"ISO-8859-6", encoding_id::single_byte, 5,
     "arabic asmo-708 csiso88596e csiso88596i csisolatinarabic ecma-114 iso-8859-6 iso-8859-6-e "
     "iso-8859-6-i iso-ir-127 iso8859-6 iso88596 iso_8859-6 iso_8859-6:1987"},
    {"ISO-8859-7", encoding_id::single_byte, 6,
     "csisolatingreek ecma-118 elot_928 greek greek8 iso-8859-7 iso-ir-126 iso8859-7 iso88597 "
     "iso_8859-7 iso_8859-7:1987 sun_eu_greek"},
    {"ISO-8859-8", encoding_id::single_byte, 7,
     "csiso88598e csisolatinhebrew hebrew iso-8859-8 iso-8859-8-e iso-ir-138 iso8859-8 iso88598 "
     "iso_8859-8 iso_8859-8:1988 visual"},
    // iso-8859-8-i is a SEPARATE name sharing iso-8859-8's index: a page reads
    // `decoder.encoding` back and the two answers differ.
    {"ISO-8859-8-I", encoding_id::single_byte, 7, "csiso88598i iso-8859-8-i logical"},
    {"ISO-8859-10", encoding_id::single_byte, 8,
     "csisolatin6 iso-8859-10 iso-ir-157 iso8859-10 iso885910 l6 latin6"},
    {"ISO-8859-13", encoding_id::single_byte, 9, "iso-8859-13 iso8859-13 iso885913"},
    {"ISO-8859-14", encoding_id::single_byte, 10, "iso-8859-14 iso8859-14 iso885914"},
    {"ISO-8859-15", encoding_id::single_byte, 11,
     "csisolatin9 iso-8859-15 iso8859-15 iso885915 iso_8859-15 l9"},
    {"ISO-8859-16", encoding_id::single_byte, 12, "iso-8859-16"},
    {"KOI8-R", encoding_id::single_byte, 13, "cskoi8r koi koi8 koi8-r koi8_r"},
    {"KOI8-U", encoding_id::single_byte, 14, "koi8-ru koi8-u"},
    {"macintosh", encoding_id::single_byte, 15, "csmacintosh mac macintosh x-mac-roman"},
    {"windows-874", encoding_id::single_byte, 16,
     "dos-874 iso-8859-11 iso8859-11 iso885911 tis-620 windows-874"},
    {"windows-1250", encoding_id::single_byte, 17, "cp1250 windows-1250 x-cp1250"},
    {"windows-1251", encoding_id::single_byte, 18, "cp1251 windows-1251 x-cp1251"},
    {"windows-1252", encoding_id::single_byte, 19,
     "ansi_x3.4-1968 ascii cp1252 cp819 csisolatin1 ibm819 iso-8859-1 iso-ir-100 iso8859-1 "
     "iso88591 iso_8859-1 iso_8859-1:1987 l1 latin1 us-ascii windows-1252 x-cp1252"},
    {"windows-1253", encoding_id::single_byte, 20, "cp1253 windows-1253 x-cp1253"},
    {"windows-1254", encoding_id::single_byte, 21,
     "cp1254 csisolatin5 iso-8859-9 iso-ir-148 iso8859-9 iso88599 iso_8859-9 iso_8859-9:1989 l5 "
     "latin5 windows-1254 x-cp1254"},
    {"windows-1255", encoding_id::single_byte, 22, "cp1255 windows-1255 x-cp1255"},
    {"windows-1256", encoding_id::single_byte, 23, "cp1256 windows-1256 x-cp1256"},
    {"windows-1257", encoding_id::single_byte, 24, "cp1257 windows-1257 x-cp1257"},
    {"windows-1258", encoding_id::single_byte, 25, "cp1258 windows-1258 x-cp1258"},
    {"x-mac-cyrillic", encoding_id::single_byte, 26, "x-mac-cyrillic x-mac-ukrainian"},
};

// §4.1 "get an encoding": strip leading and trailing ASCII whitespace, fold to
// lower case, then match a label exactly.
[[nodiscard]] const encoding_row * encoding_for_label(std::string_view label) {
    const std::string folded = ascii_lower_copy(trim(label, html_whitespace));
    if (folded.empty()) { return nullptr; }
    for (const encoding_row & row : encodings) {
        std::string_view rest{row.labels};
        while (!rest.empty()) {
            const std::size_t space = rest.find(' ');
            const std::string_view one = rest.substr(0, space);
            if (one == folded) { return &row; }
            if (space == std::string_view::npos) { break; }
            rest.remove_prefix(space + 1);
        }
    }
    return nullptr;
}

// The labels the Standard maps to `replacement`, which a TextDecoder must
// REFUSE rather than decode - the whole point of that encoding is that a page
// cannot be talked into interpreting these bytes.
[[nodiscard]] bool is_replacement_label(std::string_view label) {
    static constexpr std::string_view names[] = {"csiso2022kr",     "hz-gb-2312",  "iso-2022-cn",
                                                 "iso-2022-cn-ext", "iso-2022-kr", "replacement"};
    const std::string folded = ascii_lower_copy(trim(label, html_whitespace));
    return std::ranges::find(names, folded) != std::end(names);
}

// --- UTF-8 ------------------------------------------------------------------

void push_code_point(std::string & out, char32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// The decoders all answer the same two things: the text, and whether an error
// was met (which `fatal` turns into a TypeError and everything else into
// U+FFFD, already substituted).
struct decoded {
    std::string text;
    bool error = false;
    std::vector<std::uint8_t> pending; // an unfinished sequence, for `stream`
};

// §4.2 "UTF-8 decoder", as its state machine and not as a re-scan: `bytes
// needed`, `lower boundary` and `upper boundary` are what make an overlong
// sequence, a surrogate and a value above U+10FFFF each ONE error, and what
// let a code point be split across two `decode(..., {stream: true})` calls.
[[nodiscard]] decoded decode_utf8_stream(std::span<const std::uint8_t> bytes, bool flush) {
    decoded out;
    char32_t code_point = 0;
    int seen = 0;
    int needed = 0;
    std::uint8_t lower = 0x80;
    std::uint8_t upper = 0xBF;
    std::vector<std::uint8_t> sequence;
    const auto fail = [&](std::size_t back) {
        out.error = true;
        push_code_point(out.text, 0xFFFD);
        code_point = 0;
        seen = 0;
        needed = 0;
        lower = 0x80;
        upper = 0xBF;
        sequence.clear();
        return back;
    };
    for (std::size_t at = 0; at < bytes.size(); ++at) {
        const std::uint8_t byte = bytes[at];
        if (needed == 0) {
            if (byte <= 0x7F) {
                out.text.push_back(static_cast<char>(byte));
            } else if (byte >= 0xC2 && byte <= 0xDF) {
                needed = 1;
                code_point = byte & 0x1Fu;
            } else if (byte >= 0xE0 && byte <= 0xEF) {
                if (byte == 0xE0) { lower = 0xA0; }
                if (byte == 0xED) { upper = 0x9F; }
                needed = 2;
                code_point = byte & 0x0Fu;
            } else if (byte >= 0xF0 && byte <= 0xF4) {
                if (byte == 0xF0) { lower = 0x90; }
                if (byte == 0xF4) { upper = 0x8F; }
                needed = 3;
                code_point = byte & 0x07u;
            } else {
                (void)fail(0);
                continue;
            }
            if (needed != 0) { sequence.push_back(byte); }
            continue;
        }
        if (byte < lower || byte > upper) {
            // THE OFFENDING BYTE IS RESTORED, §4.2: it is not part of this
            // sequence, so it is decoded again from scratch - which is what
            // makes "\xE0\x41" two characters and not one.
            (void)fail(0);
            --at;
            continue;
        }
        lower = 0x80;
        upper = 0xBF;
        code_point = (code_point << 6) | (byte & 0x3Fu);
        sequence.push_back(byte);
        if (++seen == needed) {
            push_code_point(out.text, code_point);
            code_point = 0;
            seen = 0;
            needed = 0;
            sequence.clear();
        }
    }
    if (needed != 0) {
        // Mid-sequence at the end of the input: the bytes wait for the next
        // call while streaming, and are one error when there is no next call.
        if (flush) {
            out.error = true;
            push_code_point(out.text, 0xFFFD);
        } else {
            out.pending = sequence;
        }
    }
    return out;
}

// §4.3, UTF-16, both byte orders: pairs of bytes, with the surrogate pairing
// the standard spells out - an unpaired surrogate on either side is an error.
[[nodiscard]] decoded decode_utf16(std::span<const std::uint8_t> bytes, bool big_endian,
                                   bool flush) {
    decoded out;
    char32_t lead = 0;
    bool have_lead = false;
    std::size_t at = 0;
    for (; at + 1 < bytes.size(); at += 2) {
        const auto unit = static_cast<char32_t>(big_endian ? (bytes[at] << 8) | bytes[at + 1]
                                                           : (bytes[at + 1] << 8) | bytes[at]);
        if (have_lead) {
            have_lead = false;
            if (unit >= 0xDC00 && unit <= 0xDFFF) {
                push_code_point(out.text, 0x10000 + ((lead - 0xD800) << 10) + (unit - 0xDC00));
                continue;
            }
            // The lead was unpaired; this unit is decoded again from scratch.
            out.error = true;
            push_code_point(out.text, 0xFFFD);
        }
        if (unit >= 0xD800 && unit <= 0xDBFF) {
            lead = unit;
            have_lead = true;
            continue;
        }
        if (unit >= 0xDC00 && unit <= 0xDFFF) {
            out.error = true;
            push_code_point(out.text, 0xFFFD);
            continue;
        }
        push_code_point(out.text, unit);
    }
    // A lone trailing byte, or a lead surrogate with nothing after it: both
    // carry over while streaming and are an error at the end.
    if (have_lead || at < bytes.size()) {
        if (flush) {
            out.error = true;
            push_code_point(out.text, 0xFFFD);
        } else {
            if (have_lead) {
                out.pending.push_back(bytes[at - 2]);
                out.pending.push_back(bytes[at - 1]);
            }
            if (at < bytes.size()) { out.pending.push_back(bytes[at]); }
        }
    }
    return out;
}

// §9.2 "single-byte decoder" and §14.1 x-user-defined: ASCII passes through
// and the top half is one table lookup. Neither can be split across a call, so
// neither has a pending state.
[[nodiscard]] decoded decode_single_byte(std::span<const std::uint8_t> bytes,
                                         std::u16string_view index) {
    decoded out;
    for (const std::uint8_t byte : bytes) {
        if (byte <= 0x7F) {
            out.text.push_back(static_cast<char>(byte));
            continue;
        }
        const char16_t mapped = index[byte - 0x80u];
        if (mapped == 0) {
            out.error = true;
            push_code_point(out.text, 0xFFFD);
        } else {
            push_code_point(out.text, mapped);
        }
    }
    return out;
}

[[nodiscard]] decoded decode_user_defined(std::span<const std::uint8_t> bytes) {
    decoded out;
    for (const std::uint8_t byte : bytes) {
        if (byte <= 0x7F) {
            out.text.push_back(static_cast<char>(byte));
        } else {
            push_code_point(out.text, static_cast<char32_t>(0xF780 + byte - 0x80u));
        }
    }
    return out;
}

// --- the bytes behind a BufferSource ---------------------------------------

// An ArrayBuffer carries its bytes in `__bytes`; a view answers `buffer`,
// `byteOffset` and `byteLength`, and ASKING for `buffer` is what moves an
// owning typed array's elements into a store (see
// lib/Script/builtins/collections/typed_arrays/internal.hpp), so this one path
// takes every shape - Uint8Array, Float32Array, DataView, ArrayBuffer.
[[nodiscard]] bool buffer_source_bytes(context & c, value source, std::vector<std::uint8_t> & out) {
    if (!source.is_object_like()) { return false; }
    const auto copy = [&out](script::array_object * store, std::size_t from, std::size_t count) {
        out.clear();
        out.reserve(count);
        for (std::size_t i = from; i < from + count && i < store->items.size(); ++i) {
            out.push_back(static_cast<std::uint8_t>(
                static_cast<std::uint32_t>(context::to_number(store->items[i])) & 0xFFu));
        }
    };
    if (const value own = c.lookup_property(source, "__bytes"); own.is_array()) {
        auto * store = static_cast<script::array_object *>(own.as_heap());
        copy(store, 0, store->items.size());
        return true;
    }
    const value buffer = c.lookup_property(source, "buffer");
    if (!buffer.is_object_like()) { return false; }
    const value store_value = c.lookup_property(buffer, "__bytes");
    if (!store_value.is_array()) { return false; }
    auto * store = static_cast<script::array_object *>(store_value.as_heap());
    const double offset = context::to_number(c.lookup_property(source, "byteOffset"));
    const double length = context::to_number(c.lookup_property(source, "byteLength"));
    if (!(offset >= 0) || !(length >= 0)) { return false; }
    copy(store, static_cast<std::size_t>(offset), static_cast<std::size_t>(length));
    return true;
}

// --- the two interfaces ------------------------------------------------------

[[nodiscard]] script::object_object * branded(context & c, std::string_view slot,
                                              const char * interface) {
    const value self = c.current_this();
    auto * obj = self.is_object() ? static_cast<script::object_object *>(self.as_heap()) : nullptr;
    if (obj == nullptr || obj->find(slot) == nullptr) {
        c.throw_error("TypeError", std::string{interface} + ": illegal invocation");
        return nullptr;
    }
    return obj;
}

struct interface_pair {
    script::native_object * ctor;
    script::object_object * proto;
};

[[nodiscard]] interface_pair make_interface(context & cx, const char * name) {
    auto * proto = cx.allocate<script::object_object>();
    auto * ctor = cx.allocate<script::native_object>(name, script::native_fn{});
    ctor->define("prototype", value::object(proto), script::attr_none);
    proto->define("constructor", value::object(ctor), script::attr_builtin);
    proto->define("@@toStringTag", cx.string(name), script::attr_configurable);
    cx.define_global(name, value::object(ctor));
    return {ctor, proto};
}

void install_text_encoder(context & cx) {
    const interface_pair made = make_interface(cx, "TextEncoder");
    auto * ctor = made.ctor;
    ctor->fn = [ctor](context & c, std::span<value>) {
        const value self = c.current_this();
        if (!self.is_object() || !c.instance_of(self, value::object(ctor))) {
            c.throw_error("TypeError", "TextEncoder constructor: 'new' is required");
            return value::undefined();
        }
        static_cast<script::object_object *>(self.as_heap())
            ->define(encoder_key, value::boolean(true), script::attr_none);
        return self;
    };
    define_getter(cx, *made.proto, "encoding", [](context & c, std::span<value>) {
        if (branded(c, encoder_key, "TextEncoder") == nullptr) { return value::undefined(); }
        return c.string("utf-8");
    });
    set_method(
        cx, *made.proto, "encode",
        [](context & c, std::span<value> a) {
            if (branded(c, encoder_key, "TextEncoder") == nullptr) { return value::undefined(); }
            // USVString: the lone surrogates a JavaScript string may hold
            // become U+FFFD before anything is encoded, §8.1.
            const std::string text =
                a.empty() || a[0].is_undefined() ? std::string{} : to_usv_string(c.to_string(a[0]));
            const value out = c.make_array();
            auto * bytes = static_cast<script::array_object *>(out.as_heap());
            bytes->elements = script::element_kind::u8;
            bytes->items.reserve(text.size());
            for (const char ch : text) {
                bytes->items.push_back(
                    value::number(static_cast<double>(static_cast<unsigned char>(ch))));
            }
            return out;
        },
        script::attr_builtin);
    set_method(
        cx, *made.proto, "encodeInto",
        [](context & c, std::span<value> a) {
            if (branded(c, encoder_key, "TextEncoder") == nullptr) { return value::undefined(); }
            const std::string text = to_usv_string(c.to_string(arg(a, 0)));
            const value target = arg(a, 1);
            auto * into =
                target.is_array() ? static_cast<script::array_object *>(target.as_heap()) : nullptr;
            if (into == nullptr || into->elements != script::element_kind::u8) {
                c.throw_error("TypeError",
                              "TextEncoder.encodeInto: the destination is not a Uint8Array");
                return value::undefined();
            }
            // §8.1: whole code points only. A destination with three bytes left
            // and a four-byte character in hand writes NOTHING and stops, so a
            // caller can hand the same string to the next buffer.
            std::size_t written = 0;
            std::size_t read = 0;
            for (std::size_t at = 0; at < text.size();) {
                const std::size_t start = at;
                const char32_t cp = decode_utf8(text, at);
                const std::size_t width = at - start;
                if (written + width > into->items.size()) { break; }
                for (std::size_t i = 0; i < width; ++i) {
                    into->items[written + i] = value::number(
                        static_cast<double>(static_cast<unsigned char>(text[start + i])));
                }
                written += width;
                // `read` counts UTF-16 CODE UNITS, which is two for anything
                // above the BMP - the number the caller slices its string by.
                read += cp >= 0x10000 ? 2u : 1u;
            }
            auto * result = static_cast<script::object_object *>(c.make_object().as_heap());
            result->set("read", value::number(static_cast<double>(read)));
            result->set("written", value::number(static_cast<double>(written)));
            return value::object(result);
        },
        script::attr_builtin);
}

void install_text_decoder(context & cx) {
    const interface_pair made = make_interface(cx, "TextDecoder");
    auto * ctor = made.ctor;
    ctor->fn = [ctor](context & c, std::span<value> a) {
        const value self_value = c.current_this();
        if (!self_value.is_object() || !c.instance_of(self_value, value::object(ctor))) {
            c.throw_error("TypeError", "TextDecoder constructor: 'new' is required");
            return value::undefined();
        }
        const std::string label =
            a.empty() || a[0].is_undefined() ? std::string{"utf-8"} : c.to_string(a[0]);
        if (c.throw_pending()) { return value::undefined(); }
        if (is_replacement_label(label)) {
            c.throw_error("RangeError", "TextDecoder: '" + label +
                                            "' is the replacement encoding and cannot decode");
            return value::undefined();
        }
        const encoding_row * row = encoding_for_label(label);
        if (row == nullptr) {
            c.throw_error("RangeError", "TextDecoder: '" + label +
                                            "' is not a supported encoding (the legacy "
                                            "multi-byte encodings are not implemented)");
            return value::undefined();
        }
        const value options = arg(a, 1);
        auto * self = static_cast<script::object_object *>(self_value.as_heap());
        self->define(encoding_key, value::number(static_cast<double>(row - std::begin(encodings))),
                     script::attr_none);
        self->define(fatal_key, value::boolean(dict_flag(c, options, "fatal")), script::attr_none);
        self->define(ignore_bom_key, value::boolean(dict_flag(c, options, "ignoreBOM")),
                     script::attr_none);
        self->define(pending_key, c.string(std::string{}), script::attr_none);
        self->define(bom_seen_key, value::boolean(false), script::attr_none);
        return self_value;
    };

    const auto row_of = [](script::object_object & self) -> const encoding_row & {
        const value * held = self.find(encoding_key);
        const auto at = static_cast<std::size_t>(std::max(0.0, context::to_number(*held)));
        return encodings[std::min(at, std::size(encodings) - 1)];
    };
    define_getter(cx, *made.proto, "encoding", [row_of](context & c, std::span<value>) {
        script::object_object * self = branded(c, encoding_key, "TextDecoder");
        if (self == nullptr) { return value::undefined(); }
        // LOWER CASE, §8.2: the name is `UTF-8` in the table and `utf-8` here.
        return c.string(ascii_lower_copy(row_of(*self).name));
    });
    for (const auto & [name, slot] :
         {std::pair<const char *, std::string_view>{"fatal", fatal_key},
          std::pair<const char *, std::string_view>{"ignoreBOM", ignore_bom_key}}) {
        define_getter(cx, *made.proto, name, [key = slot](context & c, std::span<value>) {
            script::object_object * self = branded(c, encoding_key, "TextDecoder");
            if (self == nullptr) { return value::undefined(); }
            const value * held = self->find(key);
            return value::boolean(held != nullptr && context::truthy(*held));
        });
    }
    set_method(
        cx, *made.proto, "decode",
        [row_of](context & c, std::span<value> a) {
            script::object_object * self = branded(c, encoding_key, "TextDecoder");
            if (self == nullptr) { return value::undefined(); }
            const encoding_row & row = row_of(*self);
            const bool fatal =
                self->find(fatal_key) != nullptr && context::truthy(*self->find(fatal_key));
            const bool ignore_bom = self->find(ignore_bom_key) != nullptr &&
                                    context::truthy(*self->find(ignore_bom_key));
            const bool stream = dict_flag(c, arg(a, 1), "stream");

            std::vector<std::uint8_t> bytes;
            // THE CARRY COMES FIRST. A streaming call ended mid-sequence leaves
            // its bytes in the slot, and they belong in front of this call's.
            if (const value * held = self->find(pending_key);
                held != nullptr && held->is_string()) {
                for (const char ch : static_cast<script::string_object *>(held->as_heap())->text) {
                    bytes.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(ch)));
                }
            }
            if (!a.empty() && !a[0].is_undefined()) {
                std::vector<std::uint8_t> given;
                if (!buffer_source_bytes(c, a[0], given)) {
                    c.throw_error("TypeError",
                                  "TextDecoder.decode: the argument is not a BufferSource");
                    return value::undefined();
                }
                bytes.insert(bytes.end(), given.begin(), given.end());
            }

            // §8.2 step 5, the BOM, once per decoder and not once per call: it
            // is stripped only when it matches the decoder's own encoding.
            const bool bom_seen =
                self->find(bom_seen_key) != nullptr && context::truthy(*self->find(bom_seen_key));
            if (!bom_seen && !ignore_bom) {
                const auto starts = [&](std::initializer_list<std::uint8_t> prefix) {
                    return bytes.size() >= prefix.size() &&
                           std::equal(prefix.begin(), prefix.end(), bytes.begin());
                };
                if ((row.id == encoding_id::utf_8 && starts({0xEF, 0xBB, 0xBF}))) {
                    bytes.erase(bytes.begin(), bytes.begin() + 3);
                } else if (row.id == encoding_id::utf_16le && starts({0xFF, 0xFE})) {
                    bytes.erase(bytes.begin(), bytes.begin() + 2);
                } else if (row.id == encoding_id::utf_16be && starts({0xFE, 0xFF})) {
                    bytes.erase(bytes.begin(), bytes.begin() + 2);
                }
            }
            // Only once there are bytes to look at: an empty streaming call
            // must not spend the decoder's one chance at a BOM.
            if (!bytes.empty()) {
                self->define(bom_seen_key, value::boolean(true), script::attr_none);
            }

            decoded out;
            switch (row.id) {
            case encoding_id::utf_8: out = decode_utf8_stream(bytes, !stream); break;
            case encoding_id::utf_16le: out = decode_utf16(bytes, false, !stream); break;
            case encoding_id::utf_16be: out = decode_utf16(bytes, true, !stream); break;
            case encoding_id::x_user_defined: out = decode_user_defined(bytes); break;
            case encoding_id::single_byte:
                out = decode_single_byte(bytes, single_byte_indexes[row.index]);
                break;
            }
            std::string carry;
            for (const std::uint8_t byte : out.pending) {
                carry.push_back(static_cast<char>(byte));
            }
            self->define(pending_key, c.string(carry), script::attr_none);
            if (!stream) {
                // A finished decode forgets everything: the next call is a new
                // stream, BOM and all.
                self->define(bom_seen_key, value::boolean(false), script::attr_none);
            }
            if (fatal && out.error) {
                c.throw_error("TypeError", "TextDecoder.decode: the input is not valid " +
                                               ascii_lower_copy(row.name));
                return value::undefined();
            }
            return c.string(out.text);
        },
        script::attr_builtin);
}

} // namespace

void install_encoding(context & cx) {
    install_text_encoder(cx);
    install_text_decoder(cx);
}

} // namespace ctbrowser::shell
