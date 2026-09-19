#include "url_internal.hpp"

namespace ctbrowser::shell::url_detail {
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
// CheckBidi (5.4) IS done - a Bidi_Class table (tools/gen/idna_table.py) and
// `valid_bidi_label` below - but ONLY for a domain that carries a non-ASCII
// code point. An all-ASCII domain is never a Bidi domain, and browsers do not
// run IDNA on one at all: `xn--a` is left verbatim rather than decoded to a
// disallowed U+0080 and refused. That ASCII short-circuit is also what carries
// `IgnoreInvalidPunycode`: an invalid `xn--` label survives when the whole
// domain is ASCII, but in a domain that already holds non-ASCII an `xn--` that
// does not decode to a valid label fails it (url/toascii `xn--a.ß` -> failure).
// CheckHyphens is false and VerifyDnsLength is false: the URL Standard says so.

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
struct bidi_range {
    char32_t first;
    char32_t last;
    char kind; // R, AL='A', AN='N', EN='E', ES='S', CS='C', ET='T', ON='O', BN='B', NSM='M'
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

// Bidi_Class as CheckBidi reads it; 'L' for everything the table does not name.
[[nodiscard]] char bidi_class(char32_t c) {
    std::size_t lo = 0;
    std::size_t hi = std::size(bidi_ranges);
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (c < bidi_ranges[mid].first) {
            hi = mid;
        } else if (c > bidi_ranges[mid].last) {
            lo = mid + 1;
        } else {
            return bidi_ranges[mid].kind;
        }
    }
    return 'L';
}

// CheckBidi (RFC 5893 / UTS #46 5.4), one label. Only reached for a label of a
// Bidi domain - one where SOME label carries an R, AL ('A') or AN ('N') point.
[[nodiscard]] bool valid_bidi_label(std::u32string_view label) {
    if (label.empty()) { return true; }
    const char first = bidi_class(label.front());
    if (first == 'R' || first == 'A') {
        // RTL label: rule 2 (allowed set), rule 4 (no EN with AN), rule 3 (end).
        bool seen_en = false, seen_an = false;
        for (const char32_t c : label) {
            const char k = bidi_class(c);
            // R AL AN EN ES CS ET ON BN NSM
            if (k != 'R' && k != 'A' && k != 'N' && k != 'E' && k != 'S' && k != 'C' && k != 'T' &&
                k != 'O' && k != 'B' && k != 'M') {
                return false;
            }
            if (k == 'E') { seen_en = true; }
            if (k == 'N') { seen_an = true; }
        }
        if (seen_en && seen_an) { return false; }
        std::size_t end = label.size();
        while (end > 0 && bidi_class(label[end - 1]) == 'M') { --end; }
        if (end == 0) { return false; }
        const char last = bidi_class(label[end - 1]);
        return last == 'R' || last == 'A' || last == 'E' || last == 'N';
    }
    if (first == 'L') {
        // LTR label: rule 5 (allowed set), rule 6 (end).
        for (const char32_t c : label) {
            const char k = bidi_class(c);
            // L EN ES CS ET ON BN NSM
            if (k != 'L' && k != 'E' && k != 'S' && k != 'C' && k != 'T' && k != 'O' && k != 'B' &&
                k != 'M') {
                return false;
            }
        }
        std::size_t end = label.size();
        while (end > 0 && bidi_class(label[end - 1]) == 'M') { --end; }
        if (end == 0) { return false; }
        const char last = bidi_class(label[end - 1]);
        return last == 'L' || last == 'E';
    }
    // Rule 1: the first character must be L, R or AL.
    return false;
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
    // A domain with no non-ASCII code point is never a Bidi domain and never
    // needs Punycode decoded: browsers leave it verbatim. That is what keeps an
    // invalid `xn--` label (IgnoreInvalidPunycode) and skips CheckBidi below.
    const bool all_ascii =
        std::all_of(domain.begin(), domain.end(), [](char32_t c) { return c < 0x80; });
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
    // Steps 3-4: break on U+002E, convert and validate each label into its
    // Unicode form. `verbatim` labels (an invalid `xn--` kept in an all-ASCII
    // domain) hold their ASCII bytes; the rest hold the validated code points.
    struct label_form {
        std::u32string unicode; // the validated Unicode label (verbatim: empty)
        std::string verbatim;   // the ASCII bytes to emit as they stand
        bool keep = false;
    };
    std::vector<label_form> labels;
    for (std::u32string_view label : split_dots(mapped)) {
        const bool ascii =
            std::all_of(label.begin(), label.end(), [](char32_t c) { return c < 0x80; });
        const bool is_punycode = label.size() >= 4 && lower(label[0]) == 'x' &&
                                 lower(label[1]) == 'n' && label[2] == '-' && label[3] == '-';
        if (is_punycode) {
            // "If the label contains any non-ASCII code point, record an
            // error": `xn--te\u0161la` is a failure and not a label to decode.
            if (!ascii) { return std::nullopt; }
            std::string as_ascii;
            for (const char32_t c : label) { as_ascii.push_back(static_cast<char>(c)); }
            const std::optional<std::u32string> converted =
                punycode_decode(std::string_view{as_ascii}.substr(4));
            // A conversion that fails - and an all-ASCII result is a failed
            // conversion, there being nothing for Punycode to have encoded - or
            // one whose result does not meet the validity criteria is an error.
            // IgnoreInvalidPunycode leaves such a label verbatim, but only when
            // the whole domain is ASCII; otherwise the domain fails.
            const bool decodes = converted && std::any_of(converted->begin(), converted->end(),
                                                          [](char32_t c) { return c >= 0x80; });
            if (!decodes || !valid_label(*converted)) {
                if (!all_ascii) { return std::nullopt; }
                labels.push_back({{}, std::move(as_ascii), true});
                continue;
            }
            labels.push_back({*converted, {}, false});
        } else {
            if (!valid_label(label)) { return std::nullopt; }
            labels.push_back({std::u32string{label}, {}, false});
        }
    }
    // Step 4.2, CheckBidi (RFC 5893): a non-ASCII domain that carries an R, AL
    // or AN point in any label is a Bidi domain, and then EVERY label must obey
    // the RTL/LTR rules. All-ASCII domains are handled above and never reach it.
    if (!all_ascii) {
        const bool bidi_domain =
            std::any_of(labels.begin(), labels.end(), [](const label_form & l) {
                return std::any_of(l.unicode.begin(), l.unicode.end(), [](char32_t c) {
                    const char k = bidi_class(c);
                    return k == 'R' || k == 'A' || k == 'N';
                });
            });
        if (bidi_domain) {
            for (const label_form & l : labels) {
                if (!l.keep && !valid_bidi_label(l.unicode)) { return std::nullopt; }
            }
        }
    }
    // Step 5: re-encode the labels that are not ASCII and join on U+002E.
    std::string out;
    for (const label_form & l : labels) {
        if (l.keep) {
            out += l.verbatim;
        } else if (std::all_of(l.unicode.begin(), l.unicode.end(),
                               [](char32_t c) { return c < 0x80; })) {
            for (const char32_t c : l.unicode) { out.push_back(static_cast<char>(c)); }
        } else {
            const std::optional<std::string> encoded = punycode(l.unicode);
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

} // namespace ctbrowser::shell::url_detail
