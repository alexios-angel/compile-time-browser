//===- ctbrowser/lib/Core/json.cpp - Strict JSON reader -------------------===//
//
// parse_json: a strict recursive-descent reader for the JSON grammar of
// ECMA-262 25.5.1 (what JSON.parse accepts), producing a json_value tree or
// the byte offset of the first error. quote_json: QuoteJSONString (25.5.2.3),
// shared by the VM's JSON.stringify and the native runtime.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/core/json.hpp>

#include <charconv>
#include <cstdint>
#include <unordered_map>
#include <utility>

namespace ctbrowser {
namespace {

/// The reader's state: the input, the cursor, and whether an error was seen.
/// After the first failure `ok` stays false and the result is discarded.
struct json_reader {
    std::string_view text;
    std::size_t at = 0;
    bool ok = true;
    /// Nesting depth, bounded so a deeply nested `[[[[...]]]]` fails as a
    /// SyntaxError rather than recursing until the native stack overflows. Well
    /// past any real document; browsers reject around here too.
    std::size_t depth = 0;
    static constexpr std::size_t max_depth = 1000;

    /// Skips JSON whitespace: exactly space, tab, LF and CR (ECMA-262 25.5.1).
    /// A form feed or vertical tab is a SyntaxError (test262
    /// built-ins/JSON/parse/invalid-whitespace.js).
    void skip() {
        while (at < text.size() &&
               (text[at] == ' ' || text[at] == '\t' || text[at] == '\n' || text[at] == '\r')) {
            ++at;
        }
    }
    void fail() { ok = false; }
    /// Consumes `c` if it is next; otherwise records a failure.
    [[nodiscard]] bool eat(char c) {
        if (at < text.size() && text[at] == c) {
            ++at;
            return true;
        }
        fail();
        return false;
    }

    /// Parses a whole document: one value with optional whitespace on either
    /// side and nothing after it, so `JSON.parse("[1,2]junk")` is a
    /// SyntaxError. On failure, returns the offset where reading stopped.
    [[nodiscard]] std::expected<json_value, std::size_t> parse_text() {
        const json_value out = parse();
        skip();
        if (at != text.size()) { fail(); }
        if (!ok) { return std::unexpected(at); }
        return out;
    }

    /// Parses one value of any type, after leading whitespace.
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
            const json_value nested = c == '{' ? parse_object() : parse_array();
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

    /// Reads the four hex digits of a \uXXXX escape. Returns false, without
    /// consuming input, on a short or non-hex sequence.
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

    /// Parses a quoted string at the cursor into `out` as WTF-8.
    [[nodiscard]] bool parse_string(std::string & out) {
        if (!eat('"')) { return false; }
        while (at < text.size() && text[at] != '"') {
            const auto byte = static_cast<unsigned char>(text[at]);
            // A raw control character (below U+0020) is not allowed inside a
            // JSON string; it must be escaped, e.g. as \n.
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
                // A high surrogate followed by a \u low surrogate is one code
                // point; combine them so the output is UTF-8, not CESU-8 (which
                // nothing reading this engine's strings understands). An
                // unpaired high surrogate is left for the lone-surrogate path
                // below.
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
                // A lone surrogate stays one, as the three-byte WTF-8 form a
                // script's string holds it in (script/utf16.hpp): `JSON.parse
                // ('"\ud800"').charCodeAt(0)` is 0xD800.
                append_utf8(out, code);
                break;
            }
            default: fail(); return false;
            }
        }
        if (!eat('"')) { return false; }
        return true;
    }

    /// JSONNumber: an optional minus, an integer part with no leading zero, an
    /// optional fraction with at least one digit, and an optional exponent with
    /// at least one digit. `+1`, `01`, `1.`, `.5` and `1e` are SyntaxErrors.
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
        // Use from_chars, not strtod: strtod obeys LC_NUMERIC, so a
        // comma-decimal locale would read "1.5" as 1 and break byte-compared
        // goldens.
        const std::string_view digits = text.substr(start, at - start);
        double parsed = 0.0;
        std::from_chars(digits.data(), digits.data() + digits.size(), parsed);
        return {parsed};
    }

    /// Parses an array; the cursor is on its `[`.
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

    /// Parses an object; the cursor is on its `{`. A repeated key keeps its
    /// first position and takes the last value, as JSON.parse does.
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

void quote_json(std::string_view text, std::string & out) {
    out += '"';
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        // 25.5.2.3 QuoteJSONString step 2.b: a lone surrogate is escaped as
        // \uXXXX (well-formed JSON.stringify). The text is WTF-8, so one is the
        // three bytes ED A0..BF xx; a pair's halves arrive as one four-byte
        // scalar and never take this arm.
        if (static_cast<unsigned char>(c) == 0xED && i + 2 < text.size() &&
            static_cast<unsigned char>(text[i + 1]) >= 0xA0) {
            const unsigned cp = ((static_cast<unsigned char>(c) & 0x0Fu) << 12) |
                                ((static_cast<unsigned char>(text[i + 1]) & 0x3Fu) << 6) |
                                (static_cast<unsigned char>(text[i + 2]) & 0x3Fu);
            static constexpr char hex[] = "0123456789abcdef";
            out += "\\u";
            out += hex[(cp >> 12) & 0xF];
            out += hex[(cp >> 8) & 0xF];
            out += hex[(cp >> 4) & 0xF];
            out += hex[cp & 0xF];
            i += 2;
            continue;
        }
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
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
}

} // namespace ctbrowser
