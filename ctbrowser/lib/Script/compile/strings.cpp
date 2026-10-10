//===- strings.cpp - String and number literal decoding ---------*- C++ -*-===//
//
// compiler_impl members that decode literals: string escapes (\u{...},
// surrogate pairs, legacy octal, line continuations) and numeric literals
// (separators, radix prefixes, overflow to Infinity). The class is declared
// in compiler_impl.hpp beside this file.
//
//===----------------------------------------------------------------------===//

#include "compiler_impl.hpp"

namespace ctbrowser::script::detail {

/// Reads up to \p count hex digits starting at s[at + 1] and returns their
/// value. On return \p at indexes the last digit consumed (it is unchanged
/// when none is), matching callers whose loop index sits on the escape letter.
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

std::string compiler_impl::decode_string_literal(std::string_view lexeme) {
    if (lexeme.size() >= 2 &&
        (lexeme.front() == '\'' || lexeme.front() == '"' || lexeme.front() == '`')) {
        lexeme = lexeme.substr(1, lexeme.size() - 2);
    }
    return decode_string_body(lexeme);
}

/// Decodes the escapes of a string's body: the text between the quotes, or a
/// template's chunk, which has no quotes to strip and may begin with one.
std::string compiler_impl::decode_string_body(std::string_view lexeme) {
    std::string out;
    out.reserve(lexeme.size());
    // U+2028 and U+2029 in UTF-8, the two line terminators past ASCII.
    const auto separator_at = [&](std::size_t at) {
        return at + 2 < lexeme.size() && lexeme[at] == '\xE2' && lexeme[at + 1] == '\x80' &&
               (lexeme[at + 2] == '\xA8' || lexeme[at + 2] == '\xA9');
    };
    for (std::size_t i = 0; i < lexeme.size(); ++i) {
        // A template's raw <CR><LF> or <CR> is <LF> (12.9.6 TV of a
        // LineTerminatorSequence), as its raw string already reads it. A
        // string literal cannot hold one, so this is the template's alone.
        if (lexeme[i] == '\r') {
            out.push_back('\n');
            if (i + 1 < lexeme.size() && lexeme[i + 1] == '\n') { ++i; }
            continue;
        }
        if (lexeme[i] != '\\' || i + 1 >= lexeme.size()) {
            out.push_back(lexeme[i]);
            continue;
        }
        // A backslash before U+2028 or U+2029 is a LineContinuation too
        // (12.9.4), which contributes nothing.
        if (separator_at(i + 1)) {
            i += 3;
            continue;
        }
        switch (lexeme[++i]) {
        case 'n': out.push_back('\n'); break;
        case 't': out.push_back('\t'); break;
        case 'r': out.push_back('\r'); break;
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7': {
            // 12.9.4.3 LegacyOctalEscapeSequence: up to three octal digits,
            // two after a leading 0-3 and one after 4-7; `\0` alone is NUL.
            std::uint32_t code = static_cast<std::uint32_t>(lexeme[i] - '0');
            const std::size_t more = lexeme[i] <= '3' ? 2 : 1;
            for (std::size_t k = 0;
                 k < more && i + 1 < lexeme.size() && lexeme[i + 1] >= '0' && lexeme[i + 1] <= '7';
                 ++k) {
                code = code * 8 + static_cast<std::uint32_t>(lexeme[++i] - '0');
            }
            append_utf8(out, code);
            break;
        }
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'v': out.push_back('\v'); break;
        // \xHH, \uHHHH, \u{H..}: the escapes that carry a code point.
        // Strings here are bytes, so the code point is appended as UTF-8,
        // the same choice String.fromCharCode makes, so the two round-trip
        // consistently.
        case 'x': append_utf8(out, read_hex(lexeme, i, 2)); break;
        case 'u': {
            std::uint32_t code = 0;
            if (i + 1 < lexeme.size() && lexeme[i + 1] == '{') {
                i += 2; // Past the u and the {.
                for (; i < lexeme.size() && lexeme[i] != '}'; ++i) {
                    code = code * 16 + static_cast<std::uint32_t>(hex_value(lexeme[i]));
                }
            } else {
                code = read_hex(lexeme, i, 4);
                // A high surrogate followed by a low one is one code point.
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
            append_utf8(out, code);
            break;
        }
        // A backslash before a real newline is a line continuation and
        // contributes nothing, not even the newline.
        case '\n': break;
        case '\r':
            if (i + 1 < lexeme.size() && lexeme[i + 1] == '\n') { ++i; }
            break;
        default: out.push_back(lexeme[i]); break; // \\, \', \" and anything else.
        }
    }
    return out;
}

double compiler_impl::number_literal(std::string_view text) {
    // Numeric separators come off first. `1_000_000` is one token from the
    // lexer, and `std::from_chars` accepts no underscore (it would stop at the
    // first one and read 1). Removing them here serves both parse paths
    // below.
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
    // A literal too big for a double is Infinity, not zero: from_chars
    // reports result_out_of_range and leaves `d` untouched, so the overflow
    // must be handled explicitly.
    const auto failed = std::from_chars(lex.data(), lex.data() + lex.size(), d).ec;
    if (failed == std::errc::result_out_of_range) { return out_of_range_value(lex); }
    return d;
}

} // namespace ctbrowser::script::detail
