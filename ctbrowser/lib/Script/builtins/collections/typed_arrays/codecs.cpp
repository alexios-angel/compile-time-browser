// ctbrowser.script builtins - Uint8Array.fromBase64 / fromHex and
// Uint8Array.prototype.toBase64 / toHex / setFromBase64 / setFromHex
// (ES2025, 23.2.6.1-2 and 23.2.7.x). internal.hpp says how a typed array is
// laid out.
//
// NOT core/algorithms.hpp's base64_decode: that one is `atob`'s, LENIENT by
// specification (padding optional, unknown characters ignored), and this one
// is STRICT by specification - an unknown character is a SyntaxError, the
// base64url alphabet is an option, and how a partial last chunk is treated is
// another. One decoder cannot answer both.

#include "internal.hpp"

namespace ctbrowser::script::builtins_detail {

namespace {

constexpr std::string_view base64_alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr std::string_view base64url_alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

enum class chunk_handling : std::uint8_t {
    loose,
    strict,
    stop_before_partial
};

struct decode_result {
    std::size_t read = 0;
    std::vector<std::uint8_t> bytes;
    bool error = false;
};

[[nodiscard]] bool ascii_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r';
}

[[nodiscard]] int base64_value(char c) {
    const std::size_t at = base64_alphabet.find(c);
    return at == std::string_view::npos ? -1 : static_cast<int>(at);
}

// DecodeBase64Chunk: 2, 3 or 4 characters into 1, 2 or 3 bytes.
[[nodiscard]] bool decode_chunk(std::string chunk, bool throw_on_extra_bits,
                                std::vector<std::uint8_t> & out) {
    const std::size_t n = chunk.size();
    while (chunk.size() < 4) { chunk += 'A'; }
    std::uint32_t triplet = 0;
    for (const char c : chunk) {
        triplet = (triplet << 6) | static_cast<std::uint32_t>(base64_value(c));
    }
    const auto b0 = static_cast<std::uint8_t>(triplet >> 16);
    const auto b1 = static_cast<std::uint8_t>((triplet >> 8) & 0xFF);
    const auto b2 = static_cast<std::uint8_t>(triplet & 0xFF);
    if (n == 2) {
        if (throw_on_extra_bits && b1 != 0) { return false; }
        out.push_back(b0);
        return true;
    }
    if (n == 3) {
        if (throw_on_extra_bits && b2 != 0) { return false; }
        out.push_back(b0);
        out.push_back(b1);
        return true;
    }
    out.push_back(b0);
    out.push_back(b1);
    out.push_back(b2);
    return true;
}

// FromBase64 (the proposal's 10.2.1 / spec 23.2.6.1.x), maxLength included.
[[nodiscard]] decode_result from_base64(std::string_view text, bool url, chunk_handling handling,
                                        std::size_t max_length) {
    decode_result result;
    if (max_length == 0) { return result; }
    std::string chunk;
    std::size_t index = 0;
    const std::size_t length = text.size();
    const auto skip = [&] {
        while (index < length && ascii_whitespace(text[index])) { ++index; }
    };
    const auto fail = [&] {
        result.error = true;
        return result;
    };
    for (;;) {
        skip();
        if (index == length) {
            if (!chunk.empty()) {
                if (handling == chunk_handling::stop_before_partial) { return result; }
                if (handling == chunk_handling::strict || chunk.size() == 1) { return fail(); }
                if (!decode_chunk(chunk, false, result.bytes)) { return fail(); }
            }
            result.read = length;
            return result;
        }
        char c = text[index++];
        if (c == '=') {
            if (chunk.size() < 2) { return fail(); }
            skip();
            if (chunk.size() == 2) {
                if (index == length) {
                    if (handling == chunk_handling::stop_before_partial) { return result; }
                    return fail();
                }
                if (text[index] == '=') {
                    ++index;
                    skip();
                }
            }
            if (index < length) { return fail(); }
            if (!decode_chunk(chunk, handling == chunk_handling::strict, result.bytes)) {
                return fail();
            }
            result.read = length;
            return result;
        }
        if (url) {
            if (c == '+' || c == '/') { return fail(); }
            if (c == '-') { c = '+'; }
            if (c == '_') { c = '/'; }
        }
        if (base64_value(c) < 0) { return fail(); }
        const std::size_t remaining = max_length - result.bytes.size();
        if ((remaining == 1 && chunk.size() == 2) || (remaining == 2 && chunk.size() == 3)) {
            return result;
        }
        chunk += c;
        if (chunk.size() == 4) {
            (void)decode_chunk(chunk, false, result.bytes);
            chunk.clear();
            result.read = index;
            if (result.bytes.size() == max_length) { return result; }
        }
    }
}

[[nodiscard]] decode_result from_hex(std::string_view text, std::size_t max_length) {
    decode_result result;
    if (text.size() % 2 != 0) {
        result.error = true;
        return result;
    }
    std::size_t index = 0;
    while (index < text.size() && result.bytes.size() < max_length) {
        const int hi = hex_value(text[index]);
        const int lo = hex_value(text[index + 1]);
        if (hi < 0 || lo < 0) {
            result.error = true;
            return result;
        }
        result.bytes.push_back(static_cast<std::uint8_t>(hi * 16 + lo));
        index += 2;
    }
    result.read = index;
    return result;
}

[[nodiscard]] std::string to_base64(const std::vector<std::uint8_t> & bytes, bool url,
                                    bool omit_padding) {
    const std::string_view alphabet = url ? base64url_alphabet : base64_alphabet;
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned b0 = bytes[i];
        const unsigned b1 = i + 1 < bytes.size() ? bytes[i + 1] : 0;
        const unsigned b2 = i + 2 < bytes.size() ? bytes[i + 2] : 0;
        const unsigned triple = (b0 << 16) | (b1 << 8) | b2;
        out += alphabet[(triple >> 18) & 0x3F];
        out += alphabet[(triple >> 12) & 0x3F];
        if (i + 1 < bytes.size()) {
            out += alphabet[(triple >> 6) & 0x3F];
        } else if (!omit_padding) {
            out += '=';
        }
        if (i + 2 < bytes.size()) {
            out += alphabet[triple & 0x3F];
        } else if (!omit_padding) {
            out += '=';
        }
    }
    return out;
}

// GetOptionsObject: undefined is no options, an object is the options, and
// anything else is a TypeError. False with the throw in flight.
[[nodiscard]] bool options_arg(context & cx, value v, value & out) {
    if (v.is_undefined()) {
        out = value::undefined();
        return true;
    }
    if (v.is_object_like()) {
        out = v;
        return true;
    }
    cx.throw_error("TypeError", "options must be an object");
    return false;
}

// One option of `name`, which must be undefined (then `fallback`) or one of
// `allowed`. `out` is the index into allowed; false with the throw in flight.
[[nodiscard]] bool string_option(context & cx, value options, const char * name,
                                 std::span<const std::string_view> allowed, std::size_t fallback,
                                 std::size_t & out) {
    out = fallback;
    if (options.is_undefined()) { return true; }
    const value v = cx.lookup_property(options, name);
    if (cx.throw_pending()) { return false; }
    if (v.is_undefined()) { return true; }
    if (v.is_string()) {
        const std::string & text = static_cast<string_object *>(v.as_heap())->text;
        for (std::size_t i = 0; i < allowed.size(); ++i) {
            if (text == allowed[i]) {
                out = i;
                return true;
            }
        }
    }
    cx.throw_error("TypeError", std::string{"invalid "} + name + " option");
    return false;
}

constexpr std::string_view alphabets[] = {"base64", "base64url"};
constexpr std::string_view handlings[] = {"loose", "strict", "stop-before-partial"};

[[nodiscard]] bool require_string(context & cx, value v, const char * method) {
    if (v.is_string()) { return true; }
    cx.throw_error("TypeError", std::string{method} + ": argument must be a string");
    return false;
}

// ValidateUint8Array: a Uint8Array specifically, whatever its buffer's state.
[[nodiscard]] array_object * this_uint8_array(context & cx, const char * method) {
    const value self = cx.current_this();
    if (is_typed_array(self) &&
        static_cast<array_object *>(self.as_heap())->elements == element_kind::u8) {
        return static_cast<array_object *>(self.as_heap());
    }
    cx.throw_error("TypeError", std::string{method} + ": this is not a Uint8Array");
    return nullptr;
}

[[nodiscard]] std::vector<std::uint8_t> bytes_of(array_object * arr) {
    std::vector<std::uint8_t> out(arr->length());
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<std::uint8_t>(context::to_number(typed_array_get(arr, i)));
    }
    return out;
}

[[nodiscard]] value fresh_uint8_array(context & cx, const std::vector<std::uint8_t> & bytes) {
    const value out = allocate_typed_array(cx, element_kind::u8, static_cast<double>(bytes.size()));
    if (out.is_undefined()) { return out; }
    auto * arr = static_cast<array_object *>(out.as_heap());
    for (std::size_t i = 0; i < bytes.size(); ++i) { typed_array_set(arr, i, bytes[i]); }
    return out;
}

[[nodiscard]] value read_written(context & cx, std::size_t read, std::size_t written) {
    const value out = cx.make_object();
    auto * obj = static_cast<object_object *>(out.as_heap());
    obj->set("read", value::number(static_cast<double>(read)));
    obj->set("written", value::number(static_cast<double>(written)));
    return out;
}

} // namespace

void install_uint8array_codecs(context & cx, native_object * ctor, object_object * proto) {
    using detail::method;

    method(cx, ctor, "fromBase64", 1, [](context & c, std::span<value> a) {
        const value text = arg_at(a, 0);
        if (!require_string(c, text, "Uint8Array.fromBase64")) { return value::undefined(); }
        value options = value::undefined();
        if (!options_arg(c, arg_at(a, 1), options)) { return value::undefined(); }
        std::size_t alphabet = 0, handling = 0;
        if (!string_option(c, options, "alphabet", alphabets, 0, alphabet) ||
            !string_option(c, options, "lastChunkHandling", handlings, 0, handling)) {
            return value::undefined();
        }
        const decode_result result = from_base64(
            static_cast<string_object *>(text.as_heap())->text, alphabet == 1,
            static_cast<chunk_handling>(handling), std::numeric_limits<std::size_t>::max());
        if (result.error) {
            c.throw_error("SyntaxError", "Uint8Array.fromBase64: invalid base64 string");
            return value::undefined();
        }
        return fresh_uint8_array(c, result.bytes);
    });
    method(cx, ctor, "fromHex", 1, [](context & c, std::span<value> a) {
        const value text = arg_at(a, 0);
        if (!require_string(c, text, "Uint8Array.fromHex")) { return value::undefined(); }
        const decode_result result = from_hex(static_cast<string_object *>(text.as_heap())->text,
                                              std::numeric_limits<std::size_t>::max());
        if (result.error) {
            c.throw_error("SyntaxError", "Uint8Array.fromHex: invalid hex string");
            return value::undefined();
        }
        return fresh_uint8_array(c, result.bytes);
    });
    method(cx, proto, "toBase64", 0, [](context & c, std::span<value> a) {
        if (this_uint8_array(c, "Uint8Array.prototype.toBase64") == nullptr) {
            return value::undefined();
        }
        value options = value::undefined();
        if (!options_arg(c, arg_at(a, 0), options)) { return value::undefined(); }
        std::size_t alphabet = 0;
        if (!string_option(c, options, "alphabet", alphabets, 0, alphabet)) {
            return value::undefined();
        }
        bool omit_padding = false;
        if (!options.is_undefined()) {
            const value v = c.lookup_property(options, "omitPadding");
            if (c.throw_pending()) { return value::undefined(); }
            omit_padding = context::truthy(v);
        }
        array_object * arr = this_typed_array(c, "Uint8Array.prototype.toBase64");
        if (arr == nullptr) { return value::undefined(); }
        return c.string(to_base64(bytes_of(arr), alphabet == 1, omit_padding));
    });
    method(cx, proto, "toHex", 0, [](context & c, std::span<value>) {
        if (this_uint8_array(c, "Uint8Array.prototype.toHex") == nullptr) {
            return value::undefined();
        }
        array_object * arr = this_typed_array(c, "Uint8Array.prototype.toHex");
        if (arr == nullptr) { return value::undefined(); }
        static constexpr char digits[] = "0123456789abcdef";
        std::string out;
        for (const std::uint8_t b : bytes_of(arr)) {
            out += digits[b >> 4];
            out += digits[b & 0xF];
        }
        return c.string(std::move(out));
    });
    method(cx, proto, "setFromBase64", 1, [](context & c, std::span<value> a) {
        if (this_uint8_array(c, "Uint8Array.prototype.setFromBase64") == nullptr) {
            return value::undefined();
        }
        const value text = arg_at(a, 0);
        if (!require_string(c, text, "Uint8Array.prototype.setFromBase64")) {
            return value::undefined();
        }
        value options = value::undefined();
        if (!options_arg(c, arg_at(a, 1), options)) { return value::undefined(); }
        std::size_t alphabet = 0, handling = 0;
        if (!string_option(c, options, "alphabet", alphabets, 0, alphabet) ||
            !string_option(c, options, "lastChunkHandling", handlings, 0, handling)) {
            return value::undefined();
        }
        array_object * arr = this_typed_array(c, "Uint8Array.prototype.setFromBase64");
        if (arr == nullptr) { return value::undefined(); }
        const decode_result result =
            from_base64(static_cast<string_object *>(text.as_heap())->text, alphabet == 1,
                        static_cast<chunk_handling>(handling), arr->length());
        // The bytes decoded before the error land FIRST (SetUint8ArrayBytes
        // precedes the throw).
        for (std::size_t i = 0; i < result.bytes.size(); ++i) {
            typed_array_set(arr, i, result.bytes[i]);
        }
        if (result.error) {
            c.throw_error("SyntaxError",
                          "Uint8Array.prototype.setFromBase64: invalid base64 string");
            return value::undefined();
        }
        return read_written(c, result.read, result.bytes.size());
    });
    method(cx, proto, "setFromHex", 1, [](context & c, std::span<value> a) {
        if (this_uint8_array(c, "Uint8Array.prototype.setFromHex") == nullptr) {
            return value::undefined();
        }
        const value text = arg_at(a, 0);
        if (!require_string(c, text, "Uint8Array.prototype.setFromHex")) {
            return value::undefined();
        }
        array_object * arr = this_typed_array(c, "Uint8Array.prototype.setFromHex");
        if (arr == nullptr) { return value::undefined(); }
        const decode_result result =
            from_hex(static_cast<string_object *>(text.as_heap())->text, arr->length());
        for (std::size_t i = 0; i < result.bytes.size(); ++i) {
            typed_array_set(arr, i, result.bytes[i]);
        }
        if (result.error) {
            c.throw_error("SyntaxError", "Uint8Array.prototype.setFromHex: invalid hex string");
            return value::undefined();
        }
        return read_written(c, result.read, result.bytes.size());
    });
}

} // namespace ctbrowser::script::builtins_detail
