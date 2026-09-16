#include <ctbrowser/shell/net/url.hpp>

// The WHATWG URL Standard (https://url.spec.whatwg.org/), section numbers as
// of 2025 in the comments. Boost.URL, which used to be here, is gone: it parses
// RFC 3986, which is not the specification a browser implements - see url.hpp.

#include <ctbrowser/core/algorithms.hpp>

#include <algorithm>
#include <cstring>
#include <string>

namespace ctbrowser::shell {

std::string percent_decode(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const int high = i + 2 < text.size() && text[i] == '%' ? hex_value(text[i + 1]) : -1;
        const int low = high < 0 ? -1 : hex_value(text[i + 2]);
        if (low < 0) {
            out += text[i];
            continue;
        }
        out += static_cast<char>(high * 16 + low);
        i += 2;
    }
    return out;
}

namespace {

constexpr char32_t eof = 0xFFFFFFFFu;

[[nodiscard]] constexpr bool is_alpha(char32_t c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
[[nodiscard]] constexpr bool is_digit(char32_t c) noexcept {
    return c >= '0' && c <= '9';
}
[[nodiscard]] constexpr bool is_hex(char32_t c) noexcept {
    return c < 0x80 && hex_value(static_cast<char>(c)) >= 0;
}
[[nodiscard]] constexpr char32_t lower(char32_t c) noexcept {
    return c >= 'A' && c <= 'Z' ? c + 0x20 : c;
}

// --- code points in and out ---------------------------------------------------

// The input as code points. A malformed byte decodes as itself (decode_utf8's
// rule), so a Latin-1 attribute value still parses; a WTF-8 lone surrogate
// comes through as its surrogate code point and is replaced where the
// standard says (percent-encoding, the host).
[[nodiscard]] std::u32string code_points(std::string_view utf8) {
    std::u32string out;
    out.reserve(utf8.size());
    for (std::size_t at = 0; at < utf8.size();) { out.push_back(decode_utf8(utf8, at)); }
    return out;
}

[[nodiscard]] constexpr bool is_surrogate(char32_t c) noexcept {
    return c >= 0xD800 && c <= 0xDFFF;
}

void append_scalar(std::string & out, char32_t c) {
    append_utf8(out, is_surrogate(c) || c > 0x10FFFF ? 0xFFFDu : c);
}

[[nodiscard]] std::string utf8_of(std::u32string_view text) {
    std::string out;
    for (const char32_t c : text) { append_scalar(out, c); }
    return out;
}

// "UTF-8 decode without BOM": bytes to code points, every malformed sequence
// U+FFFD. decode_utf8 hands back the lead byte and advances by one for those,
// so a code point >= 0x80 that took one byte is the tell.
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

// --- §1.3 percent-encode sets --------------------------------------------------

enum class encode_set {
    c0,
    fragment,
    query,
    special_query,
    path,
    userinfo,
    component,
    form
};

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

// --- §4.2 special schemes --------------------------------------------------------

[[nodiscard]] std::optional<std::uint16_t> default_port(std::string_view scheme) noexcept {
    if (scheme == "http" || scheme == "ws") { return 80; }
    if (scheme == "https" || scheme == "wss") { return 443; }
    if (scheme == "ftp") { return 21; }
    return std::nullopt;
}
[[nodiscard]] bool special_scheme(std::string_view scheme) noexcept {
    return scheme == "file" || default_port(scheme).has_value();
}

// --- §3.5 hosts --------------------------------------------------------------------

[[nodiscard]] constexpr bool forbidden_host(char32_t c) noexcept {
    return c == 0 || c == '\t' || c == '\n' || c == '\r' || c == ' ' || c == '#' || c == '/' ||
           c == ':' || c == '<' || c == '>' || c == '?' || c == '@' || c == '[' || c == '\\' ||
           c == ']' || c == '^' || c == '|';
}
[[nodiscard]] constexpr bool forbidden_domain(char32_t c) noexcept {
    return forbidden_host(c) || c < 0x20 || c == '%' || c == 0x7F;
}

// §3.5 IPv4 number parser: (value, ok). `input` is one dotted part.
[[nodiscard]] std::optional<std::uint64_t> ipv4_number(std::u32string_view input) {
    if (input.empty()) { return std::nullopt; }
    unsigned radix = 10;
    if (input.size() >= 2 && input[0] == '0' && lower(input[1]) == 'x') {
        input.remove_prefix(2);
        radix = 16;
    } else if (input.size() >= 2 && input[0] == '0') {
        input.remove_prefix(1);
        radix = 8;
    }
    if (input.empty()) { return 0; }
    std::uint64_t value = 0;
    for (const char32_t c : input) {
        const int digit = c < 0x80 ? hex_value(static_cast<char>(c)) : -1;
        if (digit < 0 || static_cast<unsigned>(digit) >= radix) { return std::nullopt; }
        // Saturate rather than wrap: anything past 2^40 already fails every
        // range check below, and a 200-digit part must not overflow into a
        // small number that passes.
        value = std::min<std::uint64_t>(value * radix + static_cast<unsigned>(digit),
                                        std::uint64_t{1} << 40U);
    }
    return value;
}

[[nodiscard]] std::vector<std::u32string_view> split_dots(std::u32string_view input) {
    std::vector<std::u32string_view> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= input.size(); ++i) {
        if (i == input.size() || input[i] == '.') {
            parts.push_back(input.substr(start, i - start));
            start = i + 1;
        }
    }
    return parts;
}

// §3.5 "ends in a number checker".
[[nodiscard]] bool ends_in_a_number(std::u32string_view input) {
    std::vector<std::u32string_view> parts = split_dots(input);
    if (parts.back().empty()) {
        if (parts.size() == 1) { return false; }
        parts.pop_back();
    }
    const std::u32string_view last = parts.back();
    if (!last.empty() && std::all_of(last.begin(), last.end(), is_digit)) { return true; }
    return ipv4_number(last).has_value();
}

// §3.5 IPv4 parser, to its serialisation.
[[nodiscard]] std::optional<std::string> parse_ipv4(std::u32string_view input) {
    std::vector<std::u32string_view> parts = split_dots(input);
    if (parts.back().empty() && parts.size() > 1) { parts.pop_back(); }
    if (parts.size() > 4) { return std::nullopt; }
    std::vector<std::uint64_t> numbers;
    for (const std::u32string_view part : parts) {
        const std::optional<std::uint64_t> n = ipv4_number(part);
        if (!n) { return std::nullopt; }
        numbers.push_back(*n);
    }
    for (std::size_t i = 0; i + 1 < numbers.size(); ++i) {
        if (numbers[i] > 255) { return std::nullopt; }
    }
    const std::uint64_t limit = std::uint64_t{1} << (8U * (5U - numbers.size()));
    if (numbers.back() >= limit) { return std::nullopt; }
    std::uint64_t ipv4 = numbers.back();
    for (std::size_t i = 0; i + 1 < numbers.size(); ++i) { ipv4 += numbers[i] << (8U * (3U - i)); }
    std::string out;
    for (int shift = 24; shift >= 0; shift -= 8) {
        out += std::to_string((ipv4 >> static_cast<unsigned>(shift)) & 0xFFU);
        if (shift != 0) { out += '.'; }
    }
    return out;
}

// §3.5 IPv6 parser, to its serialisation with the brackets.
[[nodiscard]] std::optional<std::string> parse_ipv6(std::u32string_view input) {
    std::uint16_t address[8] = {};
    std::size_t piece = 0;
    std::optional<std::size_t> compress;
    std::size_t p = 0;
    const auto at = [&](std::size_t i) { return i < input.size() ? input[i] : eof; };
    if (at(p) == ':') {
        if (at(p + 1) != ':') { return std::nullopt; }
        p += 2;
        ++piece;
        compress = piece;
    }
    while (at(p) != eof) {
        if (piece == 8) { return std::nullopt; }
        if (at(p) == ':') {
            if (compress) { return std::nullopt; }
            ++p;
            ++piece;
            compress = piece;
            continue;
        }
        std::uint32_t value = 0;
        std::size_t length = 0;
        while (length < 4 && is_hex(at(p))) {
            value = value * 16 + static_cast<std::uint32_t>(hex_value(static_cast<char>(at(p))));
            ++p;
            ++length;
        }
        if (at(p) == '.') {
            if (length == 0) { return std::nullopt; }
            p -= length;
            if (piece > 6) { return std::nullopt; }
            std::size_t numbers_seen = 0;
            while (at(p) != eof) {
                std::optional<std::uint32_t> ipv4_piece;
                if (numbers_seen > 0) {
                    if (at(p) == '.' && numbers_seen < 4) {
                        ++p;
                    } else {
                        return std::nullopt;
                    }
                }
                if (!is_digit(at(p))) { return std::nullopt; }
                while (is_digit(at(p))) {
                    const std::uint32_t number = at(p) - '0';
                    if (!ipv4_piece) {
                        ipv4_piece = number;
                    } else if (*ipv4_piece == 0) {
                        return std::nullopt;
                    } else {
                        ipv4_piece = *ipv4_piece * 10 + number;
                    }
                    if (*ipv4_piece > 255) { return std::nullopt; }
                    ++p;
                }
                address[piece] = static_cast<std::uint16_t>(address[piece] * 0x100 + *ipv4_piece);
                ++numbers_seen;
                if (numbers_seen == 2 || numbers_seen == 4) { ++piece; }
            }
            if (numbers_seen != 4) { return std::nullopt; }
            break;
        }
        if (at(p) == ':') {
            ++p;
            if (at(p) == eof) { return std::nullopt; }
        } else if (at(p) != eof) {
            return std::nullopt;
        }
        address[piece] = static_cast<std::uint16_t>(value);
        ++piece;
    }
    if (compress) {
        std::size_t swaps = piece - *compress;
        piece = 7;
        while (piece != 0 && swaps > 0) {
            std::swap(address[piece], address[*compress + swaps - 1]);
            --piece;
            --swaps;
        }
    } else if (piece != 8) {
        return std::nullopt;
    }
    // §3.5 IPv6 serializer: the first longest run of two or more zero pieces
    // becomes `::`.
    std::size_t best_start = 8;
    std::size_t best_length = 1;
    for (std::size_t i = 0; i < 8;) {
        if (address[i] != 0) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < 8 && address[j] == 0) { ++j; }
        if (j - i > best_length) {
            best_start = i;
            best_length = j - i;
        }
        i = j;
    }
    std::string out = "[";
    bool ignore_zero = false;
    for (std::size_t i = 0; i < 8; ++i) {
        if (ignore_zero && address[i] == 0) { continue; }
        ignore_zero = false;
        if (best_start == i) {
            out += i == 0 ? "::" : ":";
            ignore_zero = true;
            continue;
        }
        static constexpr char digits[] = "0123456789abcdef";
        std::string hex;
        std::uint16_t v = address[i];
        do {
            hex.insert(hex.begin(), digits[v & 0xFU]);
            v = static_cast<std::uint16_t>(v >> 4U);
        } while (v != 0);
        out += hex;
        if (i != 7) { out += ':'; }
    }
    return out + "]";
}

// RFC 3492 punycode, the encoder only: what `xn--` labels are made of.
[[nodiscard]] std::optional<std::string> punycode(std::u32string_view input) {
    constexpr std::uint32_t base = 36, tmin = 1, tmax = 26, skew = 38, damp = 700;
    const auto adapt = [](std::uint32_t delta, std::uint32_t points, bool first) {
        delta = first ? delta / damp : delta / 2;
        delta += delta / points;
        std::uint32_t k = 0;
        while (delta > ((base - tmin) * tmax) / 2) {
            delta /= base - tmin;
            k += base;
        }
        return k + (((base - tmin + 1) * delta) / (delta + skew));
    };
    const auto digit = [](std::uint32_t d) {
        return static_cast<char>(d < 26 ? 'a' + d : '0' + (d - 26));
    };
    std::string out;
    for (const char32_t c : input) {
        if (c < 0x80) { out.push_back(static_cast<char>(c)); }
    }
    const std::uint32_t basic = static_cast<std::uint32_t>(out.size());
    std::uint32_t handled = basic;
    if (basic > 0) { out.push_back('-'); }
    std::uint32_t n = 128, delta = 0, bias = 72;
    while (handled < input.size()) {
        char32_t m = 0x110000;
        for (const char32_t c : input) {
            if (c >= n && c < m) { m = c; }
        }
        if ((m - n) > (0xFFFFFFFFu - delta) / (handled + 1)) { return std::nullopt; }
        delta += (m - n) * (handled + 1);
        n = m;
        for (const char32_t c : input) {
            if (c < n && ++delta == 0) { return std::nullopt; }
            if (c != n) { continue; }
            std::uint32_t q = delta;
            for (std::uint32_t k = base;; k += base) {
                const std::uint32_t t = k <= bias ? tmin : (k >= bias + tmax ? tmax : k - bias);
                if (q < t) { break; }
                out.push_back(digit(t + (q - t) % (base - t)));
                q = (q - t) / (base - t);
            }
            out.push_back(digit(q));
            bias = adapt(delta, handled + 1, handled == basic);
            delta = 0;
            ++handled;
        }
        ++delta;
        ++n;
    }
    return out;
}

// --- NFC, Unicode Annex #15 -------------------------------------------------
//
// UTS #46 step 2 normalises the mapped string, and 4.1's first validity
// criterion is that a label already be NFC. Both want the canonical
// decomposition, the combining classes and the composition table, and nothing
// else in the engine has ever needed them - text is compared byte for byte
// everywhere else, deliberately (core/algorithms.hpp is ASCII-only so a render
// cannot depend on a locale). This is Unicode's data, not a locale's, so it is
// the same answer on every host.

struct nfd_entry {
    char32_t cp;
    std::uint16_t at;
    std::uint8_t len;
};
struct ccc_range {
    char32_t first;
    char32_t last;
    std::uint8_t ccc;
};
struct compose_pair {
    char32_t a;
    char32_t b;
    char32_t cp;
};

#include "nfc_table.inc"

// Hangul is ALGORITHMIC (3.12): 11,172 syllables that would be 11,172 table
// rows and are three multiplications instead.
constexpr char32_t hangul_sbase = 0xAC00;
constexpr char32_t hangul_lbase = 0x1100;
constexpr char32_t hangul_vbase = 0x1161;
constexpr char32_t hangul_tbase = 0x11A7;
constexpr std::uint32_t hangul_lcount = 19;
constexpr std::uint32_t hangul_vcount = 21;
constexpr std::uint32_t hangul_tcount = 28;
constexpr std::uint32_t hangul_ncount = hangul_vcount * hangul_tcount;
constexpr std::uint32_t hangul_scount = hangul_lcount * hangul_ncount;

[[nodiscard]] std::uint8_t combining_class(char32_t c) {
    std::size_t lo = 0;
    std::size_t hi = std::size(ccc_ranges);
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (c < ccc_ranges[mid].first) {
            hi = mid;
        } else if (c > ccc_ranges[mid].last) {
            lo = mid + 1;
        } else {
            return ccc_ranges[mid].ccc;
        }
    }
    return 0;
}

void decompose_into(std::u32string & out, char32_t c) {
    if (c >= hangul_sbase && c < hangul_sbase + hangul_scount) {
        const std::uint32_t index = c - hangul_sbase;
        out.push_back(hangul_lbase + index / hangul_ncount);
        out.push_back(hangul_vbase + (index % hangul_ncount) / hangul_tcount);
        if (const std::uint32_t t = index % hangul_tcount; t != 0) {
            out.push_back(hangul_tbase + t);
        }
        return;
    }
    std::size_t lo = 0;
    std::size_t hi = std::size(nfd_table);
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (c < nfd_table[mid].cp) {
            hi = mid;
        } else if (c > nfd_table[mid].cp) {
            lo = mid + 1;
        } else {
            out.append(&nfd_data[nfd_table[mid].at], nfd_table[mid].len);
            return;
        }
    }
    out.push_back(c);
}

// "Compose a starter with what follows it", or 0 for a pair that does not.
[[nodiscard]] char32_t compose(char32_t a, char32_t b) {
    if (a >= hangul_lbase && a < hangul_lbase + hangul_lcount && b >= hangul_vbase &&
        b < hangul_vbase + hangul_vcount) {
        return hangul_sbase +
               ((a - hangul_lbase) * hangul_vcount + (b - hangul_vbase)) * hangul_tcount;
    }
    if (a >= hangul_sbase && a < hangul_sbase + hangul_scount &&
        (a - hangul_sbase) % hangul_tcount == 0 && b > hangul_tbase &&
        b < hangul_tbase + hangul_tcount) {
        return a + (b - hangul_tbase);
    }
    std::size_t lo = 0;
    std::size_t hi = std::size(compose_pairs);
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (a < compose_pairs[mid].a || (a == compose_pairs[mid].a && b < compose_pairs[mid].b)) {
            hi = mid;
        } else if (a > compose_pairs[mid].a ||
                   (a == compose_pairs[mid].a && b > compose_pairs[mid].b)) {
            lo = mid + 1;
        } else {
            return compose_pairs[mid].cp;
        }
    }
    return 0;
}

[[nodiscard]] std::u32string to_nfc(std::u32string_view text) {
    // ASCII IS ALREADY NFC: no ASCII code point has a canonical decomposition
    // and no pair of them composes. Worth the scan because `domain to ASCII`
    // runs on EVERY host the engine parses, and almost all of them are ASCII.
    if (std::all_of(text.begin(), text.end(), [](char32_t c) { return c < 0x80; })) {
        return std::u32string{text};
    }
    std::u32string parts;
    parts.reserve(text.size());
    for (const char32_t c : text) { decompose_into(parts, c); }
    // Canonical ordering (3.11): an insertion sort, which IS the stable sort
    // the algorithm asks for and is cheaper than one on a sequence that is
    // almost always already ordered.
    for (std::size_t i = 1; i < parts.size(); ++i) {
        const std::uint8_t klass = combining_class(parts[i]);
        if (klass == 0) { continue; }
        std::size_t j = i;
        while (j > 0 && combining_class(parts[j - 1]) > klass) {
            std::swap(parts[j], parts[j - 1]);
            --j;
        }
    }
    if (parts.empty()) { return parts; }
    // Canonical composition (3.11), the annex's own loop: each character is
    // either folded into the last STARTER or appended, and a character is
    // blocked from its starter by one of equal or higher class before it.
    std::u32string out;
    out.push_back(parts[0]);
    std::size_t starter = 0;
    auto last_class = static_cast<int>(combining_class(parts[0]));
    if (last_class != 0) { last_class = 256; }
    for (std::size_t i = 1; i < parts.size(); ++i) {
        const char32_t c = parts[i];
        const auto klass = static_cast<int>(combining_class(c));
        const char32_t composed = compose(out[starter], c);
        if (composed != 0 && (last_class < klass || last_class == 0)) {
            out[starter] = composed;
            continue;
        }
        if (klass == 0) { starter = out.size(); }
        last_class = klass;
        out.push_back(c);
    }
    return out;
}

// --- UTS #46, "domain to ASCII" ---------------------------------------------
//
// THE WHOLE PROCESSING, not a hand-picked subset. What was here before mapped
// the handful of code points the corpus happened to reach - the ideographic
// full stops, full-width ASCII, the Latin-1 capitals - and passed an `xn--`
// label through unverified. url/IdnaTestV2.any.js drives 2,671 domains through
// this and 1,276 of them disagreed.
//
// The tables are Unicode's, generated by tools/gen/idna_table.py: the mapping
// table collapsed to the four statuses the URL Standard's parameters leave
// (UseSTD3ASCIIRules false, Transitional_Processing false, so a deviation is
// valid and a disallowed_STD3_* is its permissive self), General_Category=M,
// Canonical_Combining_Class=Virama and Joining_Type.
//
// Step 2's NFC is above, over Annex #15's data (tools/gen/nfc_table.py).
//
// NOT DONE: CheckBidi (5.4), which costs THREE of IdnaTestV2's 2,671 cases -
// which is why a table of every code point's Bidi_Class is not carried for it.
// CheckHyphens is false and VerifyDnsLength is false: the URL Standard says so.
// IgnoreInvalidPunycode is TRUE, which is what leaves `xn--ASCII-` alone
// instead of failing the domain.

struct idna_range {
    char32_t first;
    char32_t last;
    std::uint8_t status; // 0 valid, 1 mapped, 2 ignored, 3 disallowed
    std::uint16_t map_at;
    std::uint8_t map_len;
};
struct joining_range {
    char32_t first;
    char32_t last;
    char kind;
};

#include "idna_table.inc"

constexpr std::uint8_t idna_valid = 0;
constexpr std::uint8_t idna_mapped = 1;
constexpr std::uint8_t idna_ignored = 2;
constexpr std::uint8_t idna_disallowed = 3;

[[nodiscard]] const idna_range * idna_lookup(char32_t c) {
    std::size_t lo = 0;
    std::size_t hi = std::size(idna_ranges);
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (c < idna_ranges[mid].first) {
            hi = mid;
        } else if (c > idna_ranges[mid].last) {
            lo = mid + 1;
        } else {
            return &idna_ranges[mid];
        }
    }
    return nullptr; // outside every range: disallowed
}

template <std::size_t N> [[nodiscard]] bool in_pairs(const char32_t (&table)[N][2], char32_t c) {
    std::size_t lo = 0;
    std::size_t hi = N;
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (c < table[mid][0]) {
            hi = mid;
        } else if (c > table[mid][1]) {
            lo = mid + 1;
        } else {
            return true;
        }
    }
    return false;
}

// Joining_Type; 'U' for everything the table does not name.
[[nodiscard]] char joining_type(char32_t c) {
    std::size_t lo = 0;
    std::size_t hi = std::size(joining_ranges);
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (c < joining_ranges[mid].first) {
            hi = mid;
        } else if (c > joining_ranges[mid].last) {
            lo = mid + 1;
        } else {
            return joining_ranges[mid].kind;
        }
    }
    return 'U';
}

// RFC 3492's decoder - the half `xn--` needs and the half this file did not
// have, which is why every `xn--` label used to be taken on trust.
[[nodiscard]] std::optional<std::u32string> punycode_decode(std::string_view input) {
    constexpr std::uint32_t base = 36, tmin = 1, tmax = 26, skew = 38, damp = 700;
    const auto adapt = [](std::uint32_t delta, std::uint32_t points, bool first) {
        delta = first ? delta / damp : delta / 2;
        delta += delta / points;
        std::uint32_t k = 0;
        while (delta > ((base - tmin) * tmax) / 2) {
            delta /= base - tmin;
            k += base;
        }
        return k + (((base - tmin + 1) * delta) / (delta + skew));
    };
    const auto digit = [](char c) -> std::uint32_t {
        if (c >= 'a' && c <= 'z') { return static_cast<std::uint32_t>(c - 'a'); }
        if (c >= 'A' && c <= 'Z') { return static_cast<std::uint32_t>(c - 'A'); }
        if (c >= '0' && c <= '9') { return static_cast<std::uint32_t>(c - '0') + 26; }
        return base;
    };
    std::u32string out;
    std::size_t at = 0;
    // The basic code points, up to the LAST delimiter; everything before it
    // must be ASCII and is copied out as it stands.
    if (const std::size_t last = input.rfind('-'); last != std::string_view::npos) {
        for (std::size_t i = 0; i < last; ++i) {
            const auto byte = static_cast<unsigned char>(input[i]);
            if (byte >= 0x80) { return std::nullopt; }
            out.push_back(byte);
        }
        at = last + 1;
    }
    std::uint32_t n = 128, i = 0, bias = 72;
    while (at < input.size()) {
        const std::uint32_t old = i;
        std::uint32_t w = 1;
        for (std::uint32_t k = base;; k += base) {
            if (at >= input.size()) { return std::nullopt; }
            const std::uint32_t d = digit(input[at++]);
            if (d >= base) { return std::nullopt; }
            if (d > (0xFFFFFFFFu - i) / w) { return std::nullopt; }
            i += d * w;
            const std::uint32_t t = k <= bias ? tmin : (k >= bias + tmax ? tmax : k - bias);
            if (d < t) { break; }
            if (w > 0xFFFFFFFFu / (base - t)) { return std::nullopt; }
            w *= base - t;
        }
        const auto points = static_cast<std::uint32_t>(out.size() + 1);
        bias = adapt(i - old, points, old == 0);
        if (i / points > 0x10FFFFu - n) { return std::nullopt; }
        n += i / points;
        i %= points;
        if (n > 0x10FFFF || is_surrogate(n)) { return std::nullopt; }
        out.insert(out.begin() + static_cast<std::ptrdiff_t>(i), static_cast<char32_t>(n));
        ++i;
    }
    return out;
}

// 4.1 "Validity Criteria", the parts this engine can answer: no U+002E, no
// leading combining mark, every code point valid, and the two ContextJ rules.
[[nodiscard]] bool valid_label(std::u32string_view label) {
    if (label.empty()) { return true; }
    // Criterion 1: the label must ALREADY be in NFC. It is, for a label that
    // came through the step above; it need not be for one punycode just
    // decoded, which is the case this catches.
    if (std::any_of(label.begin(), label.end(), [](char32_t c) { return c >= 0x80; }) &&
        to_nfc(label) != label) {
        return false;
    }
    if (in_pairs(combining_mark_ranges, label.front())) { return false; }
    for (const char32_t c : label) {
        if (c == '.') { return false; }
        const idna_range * row = idna_lookup(c);
        if (row == nullptr || row->status != idna_valid) { return false; }
    }
    // ContextJ (RFC 5892 appendix A.1 and A.2). Both joiners are `deviation`
    // and so VALID above with Transitional_Processing false, which is exactly
    // why they need a rule of their own.
    for (std::size_t at = 0; at < label.size(); ++at) {
        const char32_t c = label[at];
        if (c != 0x200C && c != 0x200D) { continue; }
        if (at > 0 && in_pairs(virama_ranges, label[at - 1])) { continue; }
        if (c == 0x200D) { return false; } // ZWJ has only the Virama rule
        // ZWNJ, rule 2: (L|D) T* ZWNJ T* (R|D).
        std::size_t before = at;
        while (before > 0 && joining_type(label[before - 1]) == 'T') { --before; }
        if (before == 0) { return false; }
        const char left = joining_type(label[before - 1]);
        if (left != 'L' && left != 'D') { return false; }
        std::size_t after = at + 1;
        while (after < label.size() && joining_type(label[after]) == 'T') { ++after; }
        if (after >= label.size()) { return false; }
        const char right = joining_type(label[after]);
        if (right != 'R' && right != 'D') { return false; }
    }
    return true;
}

// §3.3 "domain to ASCII", beStrict false - UTS #46 ToASCII with the URL
// Standard's parameters.
[[nodiscard]] std::optional<std::string> domain_to_ascii(std::u32string_view domain) {
    // Step 1, "Map": disallowed fails the whole domain, ignored disappears,
    // mapped is replaced.
    std::u32string mapped;
    for (const char32_t c : domain) {
        const idna_range * row = idna_lookup(c);
        if (row == nullptr || row->status == idna_disallowed) { return std::nullopt; }
        if (row->status == idna_ignored) { continue; }
        if (row->status == idna_mapped) {
            mapped.append(&idna_mappings[row->map_at], row->map_len);
        } else {
            mapped.push_back(c);
        }
    }
    // Step 2, "Normalize": NFC over the whole mapped string, BEFORE it is cut
    // into labels - a combining mark can only compose with what precedes it,
    // and the label boundary is a U+002E that composes with nothing.
    mapped = to_nfc(mapped);
    // Steps 3-5: break on U+002E, convert and validate each label, then
    // re-encode the ones that are not ASCII.
    std::string out;
    for (std::u32string_view label : split_dots(mapped)) {
        const bool ascii =
            std::all_of(label.begin(), label.end(), [](char32_t c) { return c < 0x80; });
        std::string as_ascii;
        if (ascii) {
            for (const char32_t c : label) { as_ascii.push_back(static_cast<char>(c)); }
        }
        std::u32string decoded;
        const bool is_punycode = label.size() >= 4 && lower(label[0]) == 'x' &&
                                 lower(label[1]) == 'n' && label[2] == '-' && label[3] == '-';
        if (is_punycode) {
            // "If the label contains any non-ASCII code point, record an
            // error": `xn--te\u0161la` is a failure and not a label to decode.
            if (!ascii) { return std::nullopt; }
            const std::optional<std::u32string> converted =
                punycode_decode(std::string_view{as_ascii}.substr(4));
            const bool decodes = converted && std::any_of(converted->begin(), converted->end(),
                                                          [](char32_t c) { return c >= 0x80; });
            if (!decodes) {
                // IgnoreInvalidPunycode, which the URL Standard sets: a
                // CONVERSION that fails - and an all-ASCII result is a failed
                // conversion, there being nothing for Punycode to have encoded
                // - leaves the label exactly as it is and is not an error.
                // Without it `xn--ASCII-` takes the whole domain down.
                out += as_ascii;
                out.push_back('.');
                continue;
            }
            // And a converted label that does not MEET the validity criteria
            // is left alone too, by the same flag: browsers show `xn--a`
            // rather than refusing the domain it sits in, and IdnaTestV2 loses
            // 741 of its cases when this is a failure instead.
            if (!valid_label(*converted)) {
                out += as_ascii;
                out.push_back('.');
                continue;
            }
            decoded = *converted;
        }
        const std::u32string_view checked = decoded.empty() ? label : std::u32string_view{decoded};
        if (!valid_label(checked)) { return std::nullopt; }
        if (std::all_of(checked.begin(), checked.end(), [](char32_t c) { return c < 0x80; })) {
            for (const char32_t c : checked) { out.push_back(static_cast<char>(c)); }
        } else {
            const std::optional<std::string> encoded = punycode(checked);
            if (!encoded) { return std::nullopt; }
            out += "xn--" + *encoded;
        }
        out.push_back('.');
    }
    out.pop_back(); // the joining dot after the last label
    if (out.empty()) { return std::nullopt; }
    return out;
}

// §3.5 "host parser", to the serialised host.
[[nodiscard]] std::optional<std::string> parse_host(std::u32string_view input, bool is_opaque) {
    if (!input.empty() && input.front() == '[') {
        if (input.back() != ']') { return std::nullopt; }
        return parse_ipv6(input.substr(1, input.size() - 2));
    }
    if (is_opaque) {
        if (std::any_of(input.begin(), input.end(), forbidden_host)) { return std::nullopt; }
        return percent_encode(input, encode_set::c0);
    }
    const std::u32string domain = decode_replacing(percent_decode(utf8_of(input)));
    const std::optional<std::string> ascii = domain_to_ascii(domain);
    if (!ascii) { return std::nullopt; }
    const std::u32string ascii_points = code_points(*ascii);
    if (std::any_of(ascii_points.begin(), ascii_points.end(), forbidden_domain)) {
        return std::nullopt;
    }
    if (ends_in_a_number(ascii_points)) { return parse_ipv4(ascii_points); }
    return ascii;
}

// --- §4.4 the basic URL parser -------------------------------------------------------

enum class state {
    scheme_start,
    scheme,
    no_scheme,
    special_relative_or_authority,
    path_or_authority,
    relative,
    relative_slash,
    special_authority_slashes,
    special_authority_ignore_slashes,
    authority,
    host,
    hostname,
    port,
    file,
    file_slash,
    file_host,
    path_start,
    path,
    opaque_path,
    query,
    fragment,
};

[[nodiscard]] bool windows_drive_letter(std::u32string_view s) noexcept {
    return s.size() == 2 && is_alpha(s[0]) && (s[1] == ':' || s[1] == '|');
}
[[nodiscard]] bool normalized_windows_drive_letter(std::string_view s) noexcept {
    return s.size() == 2 && is_alpha(static_cast<unsigned char>(s[0])) && s[1] == ':';
}
[[nodiscard]] bool starts_with_windows_drive_letter(std::u32string_view s) noexcept {
    return s.size() >= 2 && windows_drive_letter(s.substr(0, 2)) &&
           (s.size() == 2 || s[2] == '/' || s[2] == '\\' || s[2] == '?' || s[2] == '#');
}
[[nodiscard]] bool single_dot(std::string_view s) noexcept {
    return s == "." || ascii_iequals(s, "%2e");
}
[[nodiscard]] bool double_dot(std::string_view s) noexcept {
    return s == ".." || ascii_iequals(s, ".%2e") || ascii_iequals(s, "%2e.") ||
           ascii_iequals(s, "%2e%2e");
}

// §4.4 "shorten a URL's path".
void shorten_path(url_record & url) {
    if (url.scheme == "file" && url.path.size() == 1 &&
        normalized_windows_drive_letter(url.path[0])) {
        return;
    }
    if (!url.path.empty()) { url.path.pop_back(); }
}

// The state machine. `url` is fresh for a plain parse and the record being
// written for a setter (`override` given); false is the standard's failure.
[[nodiscard]] bool basic_parse(std::string_view raw, const url_record * base, url_record & url,
                               std::optional<state> override) {
    std::string cleaned;
    if (!override) {
        // Steps 1-2: leading and trailing C0 controls and spaces go.
        while (!raw.empty() && static_cast<unsigned char>(raw.front()) <= 0x20) {
            raw.remove_prefix(1);
        }
        while (!raw.empty() && static_cast<unsigned char>(raw.back()) <= 0x20) {
            raw.remove_suffix(1);
        }
    }
    // Step 3: tabs and newlines go from anywhere.
    cleaned.reserve(raw.size());
    for (const char c : raw) {
        if (c != '\t' && c != '\n' && c != '\r') { cleaned.push_back(c); }
    }
    const std::u32string input = code_points(cleaned);
    const auto n = static_cast<std::ptrdiff_t>(input.size());

    state st = override.value_or(state::scheme_start);
    std::u32string buffer;
    bool at_sign_seen = false;
    bool inside_brackets = false;
    bool password_token_seen = false;
    std::ptrdiff_t p = 0;

    const auto remaining_starts_with = [&](std::u32string_view prefix) {
        return p + 1 + static_cast<std::ptrdiff_t>(prefix.size()) <= n &&
               std::u32string_view{input}.substr(static_cast<std::size_t>(p + 1), prefix.size()) ==
                   prefix;
    };
    const auto from_pointer = [&]() {
        return std::u32string_view{input}.substr(
            static_cast<std::size_t>(std::max<std::ptrdiff_t>(p, 0)));
    };

    while (true) {
        const char32_t c = p >= 0 && p < n ? input[static_cast<std::size_t>(p)] : eof;
        const bool special = url.is_special();
        switch (st) {
        case state::scheme_start:
            if (is_alpha(c)) {
                buffer.push_back(lower(c));
                st = state::scheme;
            } else if (!override) {
                st = state::no_scheme;
                --p;
            } else {
                return false;
            }
            break;

        case state::scheme:
            if (is_alpha(c) || is_digit(c) || c == '+' || c == '-' || c == '.') {
                buffer.push_back(lower(c));
            } else if (c == ':') {
                const std::string scheme = utf8_of(buffer);
                if (override) {
                    if (special_scheme(url.scheme) != special_scheme(scheme)) { return true; }
                    if ((url.has_credentials() || url.port) && scheme == "file") { return true; }
                    if (url.scheme == "file" && url.host && url.host->empty()) { return true; }
                }
                url.scheme = scheme;
                if (override) {
                    if (url.port && url.port == default_port(url.scheme)) { url.port.reset(); }
                    return true;
                }
                buffer.clear();
                if (url.scheme == "file") {
                    st = state::file;
                } else if (url.is_special() && base != nullptr && base->scheme == url.scheme) {
                    st = state::special_relative_or_authority;
                } else if (url.is_special()) {
                    st = state::special_authority_slashes;
                } else if (remaining_starts_with(U"/")) {
                    st = state::path_or_authority;
                    ++p;
                } else {
                    url.opaque_path = true;
                    url.path = {std::string{}};
                    st = state::opaque_path;
                }
            } else if (!override) {
                buffer.clear();
                st = state::no_scheme;
                p = -1;
            } else {
                return false;
            }
            break;

        case state::no_scheme:
            if (base == nullptr || (base->opaque_path && c != '#')) { return false; }
            if (base->opaque_path && c == '#') {
                url.scheme = base->scheme;
                url.path = base->path;
                url.opaque_path = true;
                url.query = base->query;
                url.fragment = std::string{};
                st = state::fragment;
            } else if (base->scheme != "file") {
                st = state::relative;
                --p;
            } else {
                st = state::file;
                --p;
            }
            break;

        case state::special_relative_or_authority:
            if (c == '/' && remaining_starts_with(U"/")) {
                st = state::special_authority_ignore_slashes;
                ++p;
            } else {
                st = state::relative;
                --p;
            }
            break;

        case state::path_or_authority:
            if (c == '/') {
                st = state::authority;
            } else {
                st = state::path;
                --p;
            }
            break;

        case state::relative:
            url.scheme = base->scheme;
            if (c == '/' || (url.is_special() && c == '\\')) {
                st = state::relative_slash;
            } else {
                url.username = base->username;
                url.password = base->password;
                url.host = base->host;
                url.port = base->port;
                url.path = base->path;
                url.opaque_path = base->opaque_path;
                url.query = base->query;
                if (c == '?') {
                    url.query = std::string{};
                    st = state::query;
                } else if (c == '#') {
                    url.fragment = std::string{};
                    st = state::fragment;
                } else if (c != eof) {
                    url.query.reset();
                    shorten_path(url);
                    st = state::path;
                    --p;
                }
            }
            break;

        case state::relative_slash:
            if (url.is_special() && (c == '/' || c == '\\')) {
                st = state::special_authority_ignore_slashes;
            } else if (c == '/') {
                st = state::authority;
            } else {
                url.username = base->username;
                url.password = base->password;
                url.host = base->host;
                url.port = base->port;
                st = state::path;
                --p;
            }
            break;

        case state::special_authority_slashes:
            st = state::special_authority_ignore_slashes;
            if (c == '/' && remaining_starts_with(U"/")) {
                ++p;
            } else {
                --p;
            }
            break;

        case state::special_authority_ignore_slashes:
            if (c != '/' && c != '\\') {
                st = state::authority;
                --p;
            }
            break;

        case state::authority:
            if (c == '@') {
                if (at_sign_seen) { buffer.insert(0, U"%40"); }
                at_sign_seen = true;
                for (const char32_t each : buffer) {
                    if (each == ':' && !password_token_seen) {
                        password_token_seen = true;
                        continue;
                    }
                    percent_encode(password_token_seen ? url.password : url.username, each,
                                   encode_set::userinfo);
                }
                buffer.clear();
            } else if (c == eof || c == '/' || c == '?' || c == '#' || (special && c == '\\')) {
                if (at_sign_seen && buffer.empty()) { return false; }
                p -= static_cast<std::ptrdiff_t>(buffer.size()) + 1;
                buffer.clear();
                st = state::host;
            } else {
                buffer.push_back(c);
            }
            break;

        case state::host:
        case state::hostname:
            if (override && url.scheme == "file") {
                --p;
                st = state::file_host;
            } else if (c == ':' && !inside_brackets) {
                if (buffer.empty()) { return false; }
                if (override == state::hostname) { return true; }
                const std::optional<std::string> host = parse_host(buffer, !special);
                if (!host) { return false; }
                url.host = *host;
                buffer.clear();
                st = state::port;
            } else if (c == eof || c == '/' || c == '?' || c == '#' || (special && c == '\\')) {
                --p;
                if (special && buffer.empty()) { return false; }
                if (override && buffer.empty() && (url.has_credentials() || url.port)) {
                    return true;
                }
                const std::optional<std::string> host = parse_host(buffer, !special);
                if (!host) { return false; }
                url.host = *host;
                buffer.clear();
                st = state::path_start;
                if (override) { return true; }
            } else {
                if (c == '[') { inside_brackets = true; }
                if (c == ']') { inside_brackets = false; }
                buffer.push_back(c);
            }
            break;

        case state::port:
            if (is_digit(c)) {
                buffer.push_back(c);
            } else if (c == eof || c == '/' || c == '?' || c == '#' || (special && c == '\\') ||
                       override) {
                if (!buffer.empty()) {
                    std::uint32_t port = 0;
                    for (const char32_t d : buffer) {
                        port = std::min<std::uint32_t>(port * 10 + (d - '0'), 70000);
                    }
                    if (port > 65535) { return false; }
                    const auto value = static_cast<std::uint16_t>(port);
                    url.port = default_port(url.scheme) == value
                                   ? std::nullopt
                                   : std::optional<std::uint16_t>{value};
                    buffer.clear();
                }
                if (override) { return true; }
                st = state::path_start;
                --p;
            } else {
                return false;
            }
            break;

        case state::file:
            url.scheme = "file";
            url.host = std::string{};
            if (c == '/' || c == '\\') {
                st = state::file_slash;
            } else if (base != nullptr && base->scheme == "file") {
                url.host = base->host;
                url.path = base->path;
                url.opaque_path = base->opaque_path;
                url.query = base->query;
                if (c == '?') {
                    url.query = std::string{};
                    st = state::query;
                } else if (c == '#') {
                    url.fragment = std::string{};
                    st = state::fragment;
                } else if (c != eof) {
                    url.query.reset();
                    if (!starts_with_windows_drive_letter(from_pointer())) {
                        shorten_path(url);
                    } else {
                        url.path.clear();
                    }
                    st = state::path;
                    --p;
                }
            } else {
                st = state::path;
                --p;
            }
            break;

        case state::file_slash:
            if (c == '/' || c == '\\') {
                st = state::file_host;
            } else {
                if (base != nullptr && base->scheme == "file") {
                    url.host = base->host;
                    if (!starts_with_windows_drive_letter(from_pointer()) && !base->path.empty() &&
                        normalized_windows_drive_letter(base->path[0])) {
                        url.path.push_back(base->path[0]);
                    }
                }
                st = state::path;
                --p;
            }
            break;

        case state::file_host:
            if (c == eof || c == '/' || c == '\\' || c == '?' || c == '#') {
                --p;
                if (!override && windows_drive_letter(buffer)) {
                    st = state::path;
                } else if (buffer.empty()) {
                    url.host = std::string{};
                    if (override) { return true; }
                    st = state::path_start;
                } else {
                    std::optional<std::string> host = parse_host(buffer, !special);
                    if (!host) { return false; }
                    if (*host == "localhost") { host->clear(); }
                    url.host = *host;
                    if (override) { return true; }
                    buffer.clear();
                    st = state::path_start;
                }
            } else {
                buffer.push_back(c);
            }
            break;

        case state::path_start:
            if (special) {
                st = state::path;
                if (c != '/' && c != '\\') { --p; }
            } else if (!override && c == '?') {
                url.query = std::string{};
                st = state::query;
            } else if (!override && c == '#') {
                url.fragment = std::string{};
                st = state::fragment;
            } else if (c != eof) {
                st = state::path;
                if (c != '/') { --p; }
            } else if (override && !url.host) {
                url.path.emplace_back();
            }
            break;

        case state::path: {
            const bool slash = c == '/' || (special && c == '\\');
            if (c == eof || slash || (!override && (c == '?' || c == '#'))) {
                const std::string segment = utf8_of(buffer);
                if (double_dot(segment)) {
                    shorten_path(url);
                    if (!slash) { url.path.emplace_back(); }
                } else if (single_dot(segment) && !slash) {
                    url.path.emplace_back();
                } else if (!single_dot(segment)) {
                    std::string kept = segment;
                    if (url.scheme == "file" && url.path.empty() && windows_drive_letter(buffer)) {
                        kept[1] = ':';
                    }
                    url.path.push_back(std::move(kept));
                }
                buffer.clear();
                if (c == '?') {
                    url.query = std::string{};
                    st = state::query;
                }
                if (c == '#') {
                    url.fragment = std::string{};
                    st = state::fragment;
                }
            } else {
                // Encoded into the buffer as code points so the dot-segment
                // tests above see `%2e` spelled as the input spelled it.
                std::string encoded;
                percent_encode(encoded, c, encode_set::path);
                for (const char each : encoded) {
                    buffer.push_back(static_cast<unsigned char>(each));
                }
            }
            break;
        }

        case state::opaque_path:
            if (c == '?') {
                url.query = std::string{};
                st = state::query;
            } else if (c == '#') {
                url.fragment = std::string{};
                st = state::fragment;
            } else if (c == ' ') {
                url.path[0] +=
                    remaining_starts_with(U"?") || remaining_starts_with(U"#") ? "%20" : " ";
            } else if (c != eof) {
                percent_encode(url.path[0], c, encode_set::c0);
            }
            break;

        case state::query:
            if ((!override && c == '#') || c == eof) {
                *url.query +=
                    percent_encode(buffer, special ? encode_set::special_query : encode_set::query);
                buffer.clear();
                if (c == '#') {
                    url.fragment = std::string{};
                    st = state::fragment;
                }
            } else {
                buffer.push_back(c);
            }
            break;

        case state::fragment:
            if (c != eof) { percent_encode(*url.fragment, c, encode_set::fragment); }
            break;
        }
        if (p >= n) { break; }
        ++p;
    }
    return true;
}

} // namespace

// --- url_record ------------------------------------------------------------------------

bool url_record::is_special() const noexcept {
    return special_scheme(scheme);
}

bool url_record::cannot_have_credentials_or_port() const noexcept {
    return !host || host->empty() || scheme == "file";
}

std::string url_record::host_and_port() const {
    if (!host) { return {}; }
    return port ? *host + ":" + std::to_string(*port) : *host;
}

std::string url_record::port_text() const {
    return port ? std::to_string(*port) : std::string{};
}

std::string url_record::pathname() const {
    if (opaque_path) { return path.empty() ? std::string{} : path[0]; }
    std::string out;
    for (const std::string & segment : path) {
        out += '/';
        out += segment;
    }
    return out;
}

std::string url_record::search() const {
    return query && !query->empty() ? "?" + *query : std::string{};
}

std::string url_record::hash() const {
    return fragment && !fragment->empty() ? "#" + *fragment : std::string{};
}

std::string url_record::serialize(bool exclude_fragment) const {
    std::string out = scheme + ":";
    if (host) {
        out += "//";
        if (has_credentials()) {
            out += username;
            if (!password.empty()) { out += ":" + password; }
            out += '@';
        }
        out += host_and_port();
    } else if (!opaque_path && path.size() > 1 && path[0].empty()) {
        // `web+demo:/.//not-a-host/`: without this a path starting `//` would
        // read back as an authority.
        out += "/.";
    }
    out += pathname();
    if (query) { out += "?" + *query; }
    if (!exclude_fragment && fragment) { out += "#" + *fragment; }
    return out;
}

std::string url_record::origin() const {
    if (scheme == "blob") {
        // §4.7: the origin of the URL the path names, for http(s) only.
        const std::optional<url_record> inner = parse_url(pathname());
        if (inner && (inner->scheme == "http" || inner->scheme == "https")) {
            return inner->origin();
        }
        return "null";
    }
    if (scheme == "ftp" || scheme == "http" || scheme == "https" || scheme == "ws" ||
        scheme == "wss") {
        return scheme + "://" + host_and_port();
    }
    return "null";
}

// --- parsing, setting, form-urlencoded -------------------------------------------------

std::optional<url_record> parse_url(std::string_view input, const url_record * base) {
    url_record url;
    if (!basic_parse(input, base, url, std::nullopt)) { return std::nullopt; }
    return url;
}

std::optional<url_record> parse_url(std::string_view input, std::string_view base) {
    const std::optional<url_record> parsed_base = parse_url(base);
    if (!parsed_base) { return std::nullopt; }
    return parse_url(input, &*parsed_base);
}

bool set_url_part(url_record & url, url_part part, std::string_view value) {
    const auto with_override = [&](std::string_view text, state override) {
        // IN PLACE, as the standard has it: `host = "example.com:65536"` sets
        // the host and THEN fails on the port, and the host stays set.
        (void)basic_parse(text, nullptr, url, override);
    };
    switch (part) {
    case url_part::href: {
        std::optional<url_record> parsed = parse_url(value);
        if (!parsed) { return false; }
        url = std::move(*parsed);
        return true;
    }
    case url_part::protocol:
        with_override(std::string{value} + ":", state::scheme_start);
        return true;
    case url_part::username:
        if (url.cannot_have_credentials_or_port()) { return true; }
        url.username = percent_encode(code_points(value), encode_set::userinfo);
        return true;
    case url_part::password:
        if (url.cannot_have_credentials_or_port()) { return true; }
        url.password = percent_encode(code_points(value), encode_set::userinfo);
        return true;
    case url_part::host:
        if (url.opaque_path) { return true; }
        with_override(value, state::host);
        return true;
    case url_part::hostname:
        if (url.opaque_path) { return true; }
        with_override(value, state::hostname);
        return true;
    case url_part::port:
        if (url.cannot_have_credentials_or_port()) { return true; }
        if (value.empty()) {
            url.port.reset();
        } else {
            with_override(value, state::port);
        }
        return true;
    case url_part::pathname:
        if (url.opaque_path) { return true; }
        url.path.clear();
        with_override(value, state::path_start);
        return true;
    case url_part::search:
        if (value.empty()) {
            url.query.reset();
            return true;
        }
        if (value.front() == '?') { value.remove_prefix(1); }
        url.query = std::string{};
        with_override(value, state::query);
        return true;
    case url_part::hash:
        if (value.empty()) {
            url.fragment.reset();
            return true;
        }
        if (value.front() == '#') { value.remove_prefix(1); }
        url.fragment = std::string{};
        with_override(value, state::fragment);
        return true;
    }
    return true;
}

std::string to_usv_string(std::string_view text) {
    return utf8_of(code_points(text));
}

form_pairs parse_form_urlencoded(std::string_view query) {
    form_pairs out;
    std::size_t start = 0;
    while (start <= query.size()) {
        std::size_t end = query.find('&', start);
        if (end == std::string_view::npos) { end = query.size(); }
        const std::string_view sequence = query.substr(start, end - start);
        start = end + 1;
        if (sequence.empty()) { continue; }
        const std::size_t equals = sequence.find('=');
        std::string name{sequence.substr(0, equals)};
        std::string value{equals == std::string_view::npos ? std::string_view{}
                                                           : sequence.substr(equals + 1)};
        std::replace(name.begin(), name.end(), '+', ' ');
        std::replace(value.begin(), value.end(), '+', ' ');
        out.emplace_back(utf8_of(decode_replacing(percent_decode(name))),
                         utf8_of(decode_replacing(percent_decode(value))));
    }
    return out;
}

std::string serialize_form_urlencoded(const form_pairs & pairs) {
    // §5.2, the byte serializer: space is `+`, the form set is escaped.
    const auto append = [](std::string & out, std::string_view text) {
        for (const char32_t c : code_points(text)) {
            if (c == ' ') {
                out.push_back('+');
            } else {
                percent_encode(out, c, encode_set::form);
            }
        }
    };
    std::string out;
    for (const auto & [name, value] : pairs) {
        if (!out.empty()) { out.push_back('&'); }
        append(out, name);
        out.push_back('=');
        append(out, value);
    }
    return out;
}

// --- the two consumers' views ---------------------------------------------------------------

fetch_url parse_absolute(std::string_view text) {
    fetch_url out;
    const std::optional<url_record> url = parse_url(text);
    // ONLY http AND https ARE FETCHABLE. A `file:` or `blob:` URL arriving on
    // the socket path is a bug in the caller, and answering with a
    // plausible-looking struct would let it stay one.
    if (!url || (url->scheme != "http" && url->scheme != "https") || !url->host ||
        url->host->empty()) {
        return out;
    }
    out.scheme = url->scheme;
    // WITHOUT the brackets: this is the address a resolver and a socket want.
    // location_parts keeps them, because that is what the DOM reports - see
    // the note in url.hpp about why the two types differ here.
    out.host = *url->host;
    if (out.host.size() > 2 && out.host.front() == '[') {
        out.host = out.host.substr(1, out.host.size() - 2);
    }
    out.port = url->port ? std::to_string(*url->port) : (out.scheme == "https" ? "443" : "80");
    out.target = url->pathname();
    if (url->query) { out.target += "?" + *url->query; }
    // THE FRAGMENT IS NEVER APPENDED. It is client-side state; sending it leaks
    // it to the server and is a specification violation besides.
    //
    // The Host header's form: bracketed for IPv6, and carrying the port only
    // when it is not the default - which is exactly the record's `host:port`,
    // since the parser already dropped a default port.
    out.authority = url->host_and_port();
    out.valid = true;
    return out;
}

location_url location_parts(std::string_view href) {
    location_url out;
    // NEVER FAILS. `location.*` has no channel for "unparseable", and a page
    // reading location.pathname mid-navigation wants an empty string rather than
    // an exception. Everything below is simply left empty.
    const std::optional<url_record> url = parse_url(href);
    if (!url) { return out; }
    out.href = url->serialize();
    out.protocol = url->protocol();
    out.username = url->username;
    out.password = url->password;
    out.host = url->host_and_port();
    out.hostname = url->hostname();
    out.port = url->port_text();
    out.pathname = url->pathname();
    out.search = url->search();
    out.hash = url->hash();
    out.origin = url->origin();
    return out;
}

std::string resolve(std::string_view base, std::string_view reference) {
    const std::optional<url_record> resolved = parse_url(reference, base);
    // LENIENT ON THE WAY OUT. An unparseable base or reference gives back the
    // reference as it arrived, which is the answer that loses the least - an
    // empty string would discard information the caller still has a use for.
    return resolved ? resolved->serialize() : std::string{reference};
}

// NOT THROUGH THE URL PARSER, and that is deliberate rather than an oversight:
// everything interesting lives in the opaque path, which the parser hands back
// as one string - and it would percent-encode a raw byte in the payload where
// RFC 2397 wants it decoded as written.
bool is_data_url(std::string_view url) {
    constexpr std::string_view scheme = "data:";
    if (url.size() < scheme.size()) { return false; }
    for (std::size_t i = 0; i < scheme.size(); ++i) {
        if (ascii_lower(url[i]) != scheme[i]) { return false; }
    }
    return true;
}

bool parse_data_url(std::string_view url, data_url & out) {
    if (!is_data_url(url)) { return false; }
    url.remove_prefix(std::string_view{"data:"}.size());
    // RFC 2397's comma separates the metadata from the payload, and a URL
    // without one is not a data URL - there is no payload to be lenient about.
    const std::size_t comma = url.find(',');
    if (comma == std::string_view::npos) { return false; }
    std::string_view meta = url.substr(0, comma);
    const std::string_view payload = url.substr(comma + 1);

    // `;base64` is the LAST parameter or it is not the encoding marker: a
    // media type parameter that merely contains the word is not one.
    constexpr std::string_view marker = ";base64";
    bool is_base64 = false;
    if (meta.size() >= marker.size()) {
        const std::string_view tail = meta.substr(meta.size() - marker.size());
        if (ascii_iequals(tail, marker)) {
            is_base64 = true;
            meta.remove_suffix(marker.size());
        }
    }

    // The media type is everything up to the first parameter. Its parameters -
    // `;charset=utf-8` - are dropped: nothing in this engine dispatches on
    // them, and a type that lies about its bytes is decided by the decoder
    // sniffing the bytes anyway.
    const std::size_t semicolon = meta.find(';');
    const std::string_view type = meta.substr(0, semicolon);
    // Empty means the RFC's default, which is already in the struct.
    if (!type.empty()) { out.mime = ascii_lower_copy(type); }

    const std::string decoded = is_base64 ? base64_decode(payload) : percent_decode(payload);
    out.bytes.resize(decoded.size());
    // `data:,` IS A VALID DATA URL and decodes to no bytes at all - and memcpy
    // is declared never-null in both arguments, so the empty case is UB rather
    // than a harmless no-op. UBSan caught this; a release build would not have.
    if (!decoded.empty()) { std::memcpy(out.bytes.data(), decoded.data(), decoded.size()); }
    return true;
}

} // namespace ctbrowser::shell
