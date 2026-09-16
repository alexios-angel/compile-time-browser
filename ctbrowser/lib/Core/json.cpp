#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/core/json.hpp>

#include <charconv>
#include <cstdint>
#include <unordered_map>
#include <utility>

namespace ctbrowser {
namespace {

struct json_reader {
    std::string_view text;
    std::size_t at = 0;
    bool ok = true;
    // Nesting depth, bounded so a deeply nested `[[[[...]]]]` fails as a
    // SyntaxError rather than recursing until the native stack overflows. Well
    // past any real document; browsers reject around here too.
    std::size_t depth = 0;
    static constexpr std::size_t max_depth = 1000;

    // 25.5.1: JSON whitespace is these four characters and nothing else. A form
    // feed or a vertical tab is a SyntaxError, which is what
    // parse/invalid-whitespace.js asserts.
    void skip() {
        while (at < text.size() &&
               (text[at] == ' ' || text[at] == '\t' || text[at] == '\n' || text[at] == '\r')) {
            ++at;
        }
    }
    void fail() { ok = false; }
    [[nodiscard]] bool eat(char c) {
        if (at < text.size() && text[at] == c) {
            ++at;
            return true;
        }
        fail();
        return false;
    }

    // The whole document: one value, whitespace either side, and NOTHING after
    // it. The trailing check is the one this reader did not do at all, so
    // `JSON.parse("[1,2]junk")` answered [1,2].
    [[nodiscard]] std::expected<json_value, std::size_t> parse_text() {
        json_value out = parse();
        skip();
        if (at != text.size()) { fail(); }
        if (!ok) { return std::unexpected(at); }
        return out;
    }

    [[nodiscard]] json_value parse() {
        skip();
        if (at >= text.size()) {
            fail();
            return {};
        }
        const char c = text[at];
        if (c == '{' || c == '[') {
            if (++depth > max_depth) {
                fail();
                return {};
            }
            json_value nested = c == '{' ? parse_object() : parse_array();
            --depth;
            return nested;
        }
        if (c == '"') {
            std::string s;
            if (!parse_string(s)) { return {}; }
            return {std::move(s)};
        }
        if (text.compare(at, 4, "true") == 0) {
            at += 4;
            return {true};
        }
        if (text.compare(at, 5, "false") == 0) {
            at += 5;
            return {false};
        }
        if (text.compare(at, 4, "null") == 0) {
            at += 4;
            return {nullptr};
        }
        return parse_number();
    }

    // \uXXXX, exactly four hex digits. False rather than reading past the end
    // or treating a non-hex byte as a digit, which the old arithmetic did:
    // `(h | 0x20) - 'a' + 10` turns ANY byte into a number.
    [[nodiscard]] bool read_hex4(std::uint32_t & out) {
        if (at + 4 > text.size()) { return false; }
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const int digit = hex_value(text[at + static_cast<std::size_t>(i)]);
            if (digit < 0) { return false; }
            out = out * 16 + static_cast<std::uint32_t>(digit);
        }
        at += 4;
        return true;
    }

    [[nodiscard]] bool parse_string(std::string & out) {
        if (!eat('"')) { return false; }
        while (at < text.size() && text[at] != '"') {
            const auto byte = static_cast<unsigned char>(text[at]);
            // A RAW CONTROL CHARACTER IS NOT A JSON STRING CHARACTER. A literal
            // newline between quotes has to be spelled \n, and accepting it
            // made this reader read documents no other one will.
            if (byte < 0x20) {
                fail();
                return false;
            }
            if (text[at] != '\\') {
                out += text[at++];
                continue;
            }
            ++at;
            if (at >= text.size()) {
                fail();
                return false;
            }
            const char escape = text[at++];
            switch (escape) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                std::uint32_t code = 0;
                if (!read_hex4(code)) {
                    fail();
                    return false;
                }
                // A SURROGATE PAIR IS ONE CODE POINT. Encoding each half
                // separately produces CESU-8, which is not UTF-8 and which no
                // consumer of this engine's strings can read - so an astral
                // character came out of JSON.parse as two replacement
                // characters and went into the page's own data that way.
                if (code >= 0xD800 && code <= 0xDBFF && at + 1 < text.size() && text[at] == '\\' &&
                    text[at + 1] == 'u') {
                    const std::size_t saved = at;
                    at += 2;
                    std::uint32_t low = 0;
                    if (read_hex4(low) && low >= 0xDC00 && low <= 0xDFFF) {
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    } else {
                        at = saved;
                    }
                }
                // A LONE SURROGATE cannot be spelled in UTF-8 and a string here
                // is UTF-8 bytes, so it becomes U+FFFD rather than an
                // ill-formed string. It is the same deviation that makes
                // `isWellFormed` unimplementable here.
                if (code >= 0xD800 && code <= 0xDFFF) { code = 0xFFFD; }
                append_utf8(out, code);
                break;
            }
            default: fail(); return false;
            }
        }
        if (!eat('"')) { return false; }
        return true;
    }

    // JSONNumber: an optional minus, an integer part with no leading zero, an
    // optional fraction that must have a digit after the point, and an optional
    // exponent that must have one after the marker. `+1`, `01`, `1.`, `.5` and
    // `1e` are each a SyntaxError and each used to parse.
    [[nodiscard]] json_value parse_number() {
        const std::size_t start = at;
        if (at < text.size() && text[at] == '-') { ++at; }
        if (at >= text.size() || text[at] < '0' || text[at] > '9') {
            fail();
            return {};
        }
        if (text[at] == '0') {
            ++at;
        } else {
            while (at < text.size() && text[at] >= '0' && text[at] <= '9') { ++at; }
        }
        if (at < text.size() && text[at] == '.') {
            ++at;
            if (at >= text.size() || text[at] < '0' || text[at] > '9') {
                fail();
                return {};
            }
            while (at < text.size() && text[at] >= '0' && text[at] <= '9') { ++at; }
        }
        if (at < text.size() && (text[at] == 'e' || text[at] == 'E')) {
            ++at;
            if (at < text.size() && (text[at] == '+' || text[at] == '-')) { ++at; }
            if (at >= text.size() || text[at] < '0' || text[at] > '9') {
                fail();
                return {};
            }
            while (at < text.size() && text[at] >= '0' && text[at] <= '9') { ++at; }
        }
        // from_chars, NOT strtod: strtod respects LC_NUMERIC, so on a host whose
        // locale writes decimals with a comma `JSON.parse("{\"n\":1.5}")` would
        // stop at the dot and read 1. Goldens are byte-compared across
        // platforms, so a locale-sensitive parser is a portability bug waiting
        // for the first machine that has one.
        const std::string_view digits = text.substr(start, at - start);
        double parsed = 0.0;
        std::from_chars(digits.data(), digits.data() + digits.size(), parsed);
        return {parsed};
    }

    [[nodiscard]] json_value parse_array() {
        json_value::array arr;
        ++at; // '['
        skip();
        if (at < text.size() && text[at] == ']') {
            ++at;
            return {std::move(arr)};
        }
        while (ok) {
            arr.push_back(parse());
            if (!ok) { break; }
            skip();
            if (at < text.size() && text[at] == ',') {
                ++at;
                continue;
            }
            // No comma, so the array must end here. A trailing comma lands back
            // in parse() on the next round and fails there, which is what the
            // grammar says.
            (void)eat(']');
            break;
        }
        return {std::move(arr)};
    }

    [[nodiscard]] json_value parse_object() {
        json_value::object obj;
        std::unordered_map<std::string, std::size_t> positions;
        ++at; // '{'
        skip();
        if (at < text.size() && text[at] == '}') {
            ++at;
            return {std::move(obj)};
        }
        while (ok) {
            skip();
            std::string key;
            if (!parse_string(key)) { break; }
            skip();
            if (!eat(':')) { break; }
            json_value each = parse();
            if (!ok) { break; }
            const auto [position, inserted] = positions.try_emplace(key, obj.size());
            if (inserted) {
                obj.emplace_back(std::move(key), std::move(each));
            } else {
                obj[position->second].value = std::move(each);
            }
            skip();
            if (at < text.size() && text[at] == ',') {
                ++at;
                continue;
            }
            (void)eat('}');
            break;
        }
        return {std::move(obj)};
    }
};

} // namespace

std::expected<json_value, std::size_t> parse_json(std::string_view text) {
    return json_reader{text}.parse_text();
}

} // namespace ctbrowser
