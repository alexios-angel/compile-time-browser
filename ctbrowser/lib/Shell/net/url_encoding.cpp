//===- ctbrowser/lib/Shell/net/url_encoding.cpp - URL text encoding -------===//
//
// The text helpers the URL parser shares: UTF-8 to code points and back,
// percent-encoding with the WHATWG URL Standard's encode sets, and the special
// schemes with their default ports. § numbers refer to the URL Standard.
//
//===----------------------------------------------------------------------===//

#include "url_internal.hpp"

namespace ctbrowser::shell::url_detail {
// --- code points in and out ---------------------------------------------------

/// The input as code points. A malformed byte decodes as itself (decode_utf8's
/// rule), so a Latin-1 attribute value still parses; a WTF-8 lone surrogate
/// comes through as its surrogate code point and is replaced where the
/// standard says (percent-encoding, the host).
[[nodiscard]] std::u32string code_points(std::string_view utf8) {
    std::u32string out;
    out.reserve(utf8.size());
    for (std::size_t at = 0; at < utf8.size();) { out.push_back(decode_utf8(utf8, at)); }
    return out;
}

/// Appends \p c as UTF-8, writing U+FFFD for a surrogate or out-of-range value.
void append_scalar(std::string & out, char32_t c) {
    append_utf8(out, is_surrogate(c) || c > 0x10FFFF ? 0xFFFDu : c);
}

/// \p text as UTF-8, with surrogates replaced by U+FFFD.
[[nodiscard]] std::string utf8_of(std::u32string_view text) {
    std::string out;
    for (const char32_t c : text) { append_scalar(out, c); }
    return out;
}

/// Encoding Standard "UTF-8 decode without BOM": bytes to code points, with
/// every malformed sequence replaced by U+FFFD. decode_utf8 hands back the lead
/// byte and advances by one for those, so a code point >= 0x80 that took one
/// byte marks a malformed sequence.
[[nodiscard]] std::u32string decode_replacing(std::string_view bytes) {
    std::u32string out;
    for (std::size_t at = 0; at < bytes.size();) {
        const std::size_t before = at;
        char32_t c = decode_utf8(bytes, at);
        if ((c >= 0x80 && at - before == 1) || is_surrogate(c) || c > 0x10FFFF) { c = 0xFFFDu; }
        out.push_back(c);
    }
    return out;
}

// --- URL Standard §1.3: percent-encode sets -------------------------------------------------

/// True when \p c must be percent-encoded in \p set. Every set includes the
/// C0 control set (and, here, everything above U+007E).
[[nodiscard]] constexpr bool in_set(char32_t c, encode_set set) noexcept {
    if (c < 0x20 || c > 0x7E) { return true; } // the C0 control set, in every set
    const auto any = [c](std::string_view chars) {
        return chars.find(static_cast<char>(c)) != std::string_view::npos;
    };
    switch (set) {
    case encode_set::c0: return false;
    case encode_set::fragment: return any(" \"<>`");
    case encode_set::query: return any(" \"#<>");
    case encode_set::special_query: return any(" \"#<>'");
    case encode_set::path: return any(" \"#<>?^`{}");
    case encode_set::userinfo: return any(" \"#<>?^`{}/:;=@[\\]|");
    case encode_set::component: return any(" \"#<>?^`{}/:;=@[\\]|$%&+,");
    case encode_set::form: return any(" \"#<>?^`{}/:;=@[\\]|$%&+,!'()~");
    }
    return false;
}

/// Appends \p c to \p out, as `%XX` for each UTF-8 byte when it is in \p set.
void percent_encode(std::string & out, char32_t c, encode_set set) {
    if (!in_set(c, set)) {
        out.push_back(static_cast<char>(c));
        return;
    }
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string bytes;
    append_scalar(bytes, c);
    for (const char each : bytes) {
        const auto b = static_cast<unsigned char>(each);
        out.push_back('%');
        out.push_back(hex[b >> 4U]);
        out.push_back(hex[b & 0x0FU]);
    }
}

[[nodiscard]] std::string percent_encode(std::u32string_view text, encode_set set) {
    std::string out;
    for (const char32_t c : text) { percent_encode(out, c, set); }
    return out;
}

// --- URL Standard §4.2: special schemes --------------------------------------------------------

/// The default port of a special scheme; nullopt for `file` and every
/// non-special scheme.
[[nodiscard]] std::optional<std::uint16_t> default_port(std::string_view scheme) noexcept {
    if (scheme == "http" || scheme == "ws") { return 80; }
    if (scheme == "https" || scheme == "wss") { return 443; }
    if (scheme == "ftp") { return 21; }
    return std::nullopt;
}
[[nodiscard]] bool special_scheme(std::string_view scheme) noexcept {
    return scheme == "file" || default_port(scheme).has_value();
}

} // namespace ctbrowser::shell::url_detail
