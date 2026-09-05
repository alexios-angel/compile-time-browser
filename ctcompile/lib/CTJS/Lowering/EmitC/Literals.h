#pragma once

#include <string>
#include <string_view>

namespace ctcompile::ctjs::emitc_detail {
// A C++ STRING LITERAL FOR ARBITRARY BYTES.
//
// OCTAL ESCAPES, NOT HEX, and the difference is a real bug rather than a taste.
// A hex escape in C++ consumes as many hex digits as follow it, so a name
// containing byte 0x01 followed by the character 'F' becomes "\x01F" - one
// character, 0x1F - and the emitted program looks up a global nobody named. An
// octal escape is exactly three digits and cannot run on.
//
// A GLOBAL'S NAME IS NOT ALWAYS AN IDENTIFIER, which is why this escapes at all
// rather than trusting the input: `globalThis["\u0000"] = 1` is legal
// JavaScript, and the importer carries whatever the source said.
// WHETHER A RAW STRING LITERAL WOULD BE BETTER THAN ESCAPING.
//
// R"(...)" is not a general answer and cannot be the only path: a raw string's
// content is the LITERAL BYTES of the generated source file, so a name
// containing a zero byte or a control character would have to have that byte
// written into the .cpp - and a NUL truncates the file for most tooling. It
// also cannot contain its own terminator.
//
// AND FOR AN ORDINARY IDENTIFIER IT IS WORSE, not better: `R"(Math)"` says
// nothing that `"Math"` does not, with five more characters. The escaped form
// IS the plain form whenever nothing needs escaping.
//
// So the raw form is used exactly where escaping is the noisy one - a name
// containing a quote or a backslash, where `"a\"b\\c"` becomes `R"(a"b\c)"`.
// That is a narrow win and it is taken because the emitted code is read by
// people when something has gone wrong.
inline constexpr bool raw_string_is_clearer(std::string_view bytes) {
    bool noisy = false;
    for (const char raw : bytes) {
        const auto byte = static_cast<unsigned char>(raw);
        // A raw string can only carry what the source file can carry plainly.
        if (byte < 0x20 || byte >= 0x7f) { return false; }
        if (byte == '"' || byte == '\\') { noisy = true; }
    }
    // The terminator cannot appear inside; a custom delimiter would only move
    // the problem to choosing one nothing collides with.
    return noisy && bytes.find(")\"") == std::string_view::npos;
}

inline constexpr std::string c_string_literal(std::string_view bytes) {
    if (raw_string_is_clearer(bytes)) { return "R\"(" + std::string(bytes) + ")\""; }
    std::string spelled = "\"";
    for (const char raw : bytes) {
        const auto byte = static_cast<unsigned char>(raw);
        if (byte == '"' || byte == '\\') {
            spelled += '\\';
            spelled += raw;
        } else if (byte >= 0x20 && byte < 0x7f) {
            spelled += raw;
        } else {
            static constexpr char digits[] = "01234567";
            spelled += '\\';
            spelled += digits[(byte >> 6) & 7u];
            spelled += digits[(byte >> 3) & 7u];
            spelled += digits[byte & 7u];
        }
    }
    spelled += '"';
    return spelled;
}

// THE ESCAPE, CHECKED AT COMPILE TIME rather than by reading an emitted file.
// "Prefer a build error to a test", and these are decidable here.
static_assert(c_string_literal("Math") == "\"Math\"");
// THE ONE THAT WOULD BREAK UNDER A HEX ESCAPE: `od`, byte 0x01, `Fd`.
//
// THE INPUT IS SPELLED OCTALLY HERE FOR THE SAME REASON THE OUTPUT IS, and the
// first attempt at this assertion proved it: written "od\x01Fd" the C++
// compiler refused the source with "hex escape sequence out of range", because
// it read the escape as \x01F followed by `d`. The hazard is real enough to
// have bitten the test written to demonstrate it.
static_assert(c_string_literal("od\001Fd") == "\"od\\001Fd\"");
static_assert(c_string_literal("od\001Fd").size() == 5 + 2 + 3,
              "five source bytes, two quotes, and three extra characters for the one escape");
// QUOTES AND BACKSLASHES TAKE THE RAW FORM, because escaping them is exactly
// the case where the escaped spelling is harder to read than the name.
static_assert(c_string_literal("a\"b") == "R\"(a\"b)\"");
static_assert(c_string_literal("a\\b") == "R\"(a\\b)\"");
// AND A ZERO BYTE SURVIVES, which is why the length is emitted beside the
// pointer: strlen would stop here. It also forces the escaped path - a raw
// string cannot carry a NUL, because a NUL in the generated source truncates
// the file for most tooling.
static_assert(c_string_literal(std::string_view("a\0b", 3)) == "\"a\\000b\"");
static_assert(!raw_string_is_clearer(std::string_view("a\0b", 3)));

// THE RAW FORM IS TAKEN ONLY WHERE ESCAPING IS THE NOISY ONE.
static_assert(!raw_string_is_clearer("Math"), "nothing to escape - the plain form is clearer");
static_assert(raw_string_is_clearer("a\"b"));
static_assert(c_string_literal("a\"b\\c") == "R\"(a\"b\\c)\"");
// AND NEVER WHERE THE CONTENT WOULD CLOSE IT.
static_assert(!raw_string_is_clearer("a)\"b"), "the content contains the terminator");
static_assert(c_string_literal("a)\"b") == "\"a)\\\"b\"");

} // namespace ctcompile::ctjs::emitc_detail
