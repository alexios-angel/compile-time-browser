// compiler_impl - literal decoding.
//
// String escapes and numeric literals: \u{...}, surrogate pairs,
// the radix prefixes. Nothing here touches a register; it was filed under
// `frames and registers` because that banner had become a junk drawer.
//
// One of the files carved out of a 3,845-line compile.cpp on 2026-08-09.
// The class is declared whole in compiler_impl.hpp beside this.

#include "compiler_impl.hpp"

namespace ctbrowser::script::detail {

std::uint32_t compiler_impl::read_hex(std::string_view s, std::size_t & at, std::size_t count) {
    std::uint32_t value = 0;
    for (std::size_t n = 0; n < count && at + 1 < s.size(); ++n) {
        const int digit = hex_value(s[at + 1]);
        if (digit < 0) { break; }
        value = value * 16 + static_cast<std::uint32_t>(digit);
        ++at;
    }
    return value;
}

std::string compiler_impl::encode_code_point(std::uint32_t code) {
    std::string out;
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
    return out;
}

std::string compiler_impl::decode_string_literal(std::string_view lexeme) {
    if (lexeme.size() >= 2 &&
        (lexeme.front() == '\'' || lexeme.front() == '"' || lexeme.front() == '`')) {
        lexeme = lexeme.substr(1, lexeme.size() - 2);
    }
    std::string out;
    out.reserve(lexeme.size());
    for (std::size_t i = 0; i < lexeme.size(); ++i) {
        if (lexeme[i] != '\\' || i + 1 >= lexeme.size()) {
            out.push_back(lexeme[i]);
            continue;
        }
        switch (lexeme[++i]) {
        case 'n': out.push_back('\n'); break;
        case 't': out.push_back('\t'); break;
        case 'r': out.push_back('\r'); break;
        case '0': out.push_back('\0'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'v': out.push_back('\v'); break;
        // \xHH, \uHHHH, \u{H..} - the escapes that carry a code point.
        // Missing them was not a missing feature but a WRONG ANSWER:
        // '\x41' came out as the three characters x41, so btoa('\x00')
        // encoded the letter x. Strings here are bytes, so a code point
        // becomes its UTF-8 - the same choice String.fromCharCode makes,
        // which is what keeps a round trip through String.prototype honest.
        case 'x': out += encode_code_point(read_hex(lexeme, i, 2)); break;
        case 'u': {
            std::uint32_t code = 0;
            if (i + 1 < lexeme.size() && lexeme[i + 1] == '{') {
                i += 2; // past the u and the {
                for (; i < lexeme.size() && lexeme[i] != '}'; ++i) {
                    code = code * 16 + static_cast<std::uint32_t>(hex_value(lexeme[i]));
                }
            } else {
                code = read_hex(lexeme, i, 4);
                // A high surrogate followed by a low one is ONE code point.
                // Encoding the halves separately would give WTF-8, and an
                // emoji written as an escape would not equal the same emoji
                // written literally in the source.
                if (code >= 0xD800 && code <= 0xDBFF && i + 6 < lexeme.size() &&
                    lexeme[i + 1] == '\\' && lexeme[i + 2] == 'u') {
                    std::size_t peek = i + 2;
                    const std::uint32_t low = read_hex(lexeme, peek, 4);
                    if (low >= 0xDC00 && low <= 0xDFFF) {
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                        i = peek;
                    }
                }
            }
            out += encode_code_point(code);
            break;
        }
        // A backslash before a real newline is a line continuation and
        // contributes nothing - not even the newline.
        case '\n': break;
        case '\r':
            if (i + 1 < lexeme.size() && lexeme[i + 1] == '\n') { ++i; }
            break;
        default: out.push_back(lexeme[i]); break; // \\ \' \" and anything else
        }
    }
    return out;
}

double compiler_impl::number_literal(std::string_view text) {
    // NUMERIC SEPARATORS COME OFF FIRST. `1_000_000` is one token from the
    // lexer, and `std::from_chars` accepts no underscore in either overload
    // - it would stop at the first one and read 1. Removing them here keeps
    // that knowledge in one place rather than in both parse paths below.
    std::string lex{text};
    std::erase(lex, '_');
    const auto radix_of = [](char c) -> int {
        if (c == 'x' || c == 'X') { return 16; }
        if (c == 'o' || c == 'O') { return 8; }
        if (c == 'b' || c == 'B') { return 2; }
        return 0;
    };
    if (lex.size() > 2 && lex[0] == '0') {
        if (const int radix = radix_of(lex[1]); radix != 0) {
            std::uint64_t bits = 0;
            const char * const begin = lex.data() + 2;
            const char * const end = lex.data() + lex.size();
            if (std::from_chars(begin, end, bits, radix).ec == std::errc{}) {
                return static_cast<double>(bits);
            }
            return 0;
        }
    }
    double d = 0;
    // A LITERAL TOO BIG FOR A DOUBLE IS Infinity, NOT ZERO. from_chars
    // reports result_out_of_range and leaves `d` UNTOUCHED, so ignoring the
    // error made `1e400` compile to 0 - silently, and at the opposite end of
    // the number line from the truth. Runtime overflow was always right
    // (`1e308 * 10` is Infinity); only the lexer disagreed.
    const auto failed = std::from_chars(lex.data(), lex.data() + lex.size(), d).ec;
    if (failed == std::errc::result_out_of_range) { return out_of_range_value(lex); }
    return d;
}

} // namespace ctbrowser::script::detail
