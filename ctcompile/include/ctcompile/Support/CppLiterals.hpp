//===- CppLiterals.hpp - C++ literal spelling -------------------*- C++ -*-===//
//
// Spells arbitrary bytes as C++ identifiers and string literals for the code
// the backends emit, with compile-time checks of the tricky escapes.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <algorithm>
#include <string>
#include <string_view>

namespace ctcompile::cpp {
/// Whether `ch` may appear in a C++ identifier: [A-Za-z0-9_]. ASCII by hand
/// rather than `std::isalnum`, which is locale-sensitive: a symbol that depends
/// on `LC_ALL` is one the linker resolves on one machine and not on another.
[[nodiscard]] constexpr bool is_identifier_char(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
           ch == '_';
}

/// `text` with every character outside [A-Za-z0-9_] replaced by `_`.
[[nodiscard]] inline std::string identifier(std::string_view text) {
    std::string spelled{text};
    std::ranges::replace_if(spelled, [](char ch) { return !is_identifier_char(ch); }, '_');
    return spelled;
}

/// Whether `bytes` reads better as a raw string R"(...)" than escaped: true
/// only for printable ASCII that contains a quote or backslash and not the
/// `)"` terminator.
///
/// A raw string cannot be the only path: its content is the literal bytes of
/// the generated source file, so a zero byte or control character would be
/// written into the .cpp (and a NUL truncates the file for most tooling). For
/// an ordinary identifier it is worse, not better: `R"(Math)"` says nothing
/// `"Math"` does not. So the raw form is used exactly where escaping is noisy,
/// a name containing a quote or backslash, where `"a\"b\\c"` becomes
/// `R"(a"b\c)"`; the emitted code is read by people when something goes wrong.
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

/// A C++ string literal for arbitrary bytes. Non-printable bytes become
/// three-digit octal escapes, not hex: a hex escape absorbs every following
/// hex digit, so byte 0x01 then 'F' would become "\x01F" (one char, 0x1F) and
/// the emitted program would look up a global nobody named. A global's name
/// need not be an identifier (`globalThis["\u0000"] = 1`), so every name goes
/// through here.
inline constexpr std::string c_string_literal(std::string_view bytes) {
    if (raw_string_is_clearer(bytes)) { return "R\"(" + std::string(bytes) + ")\""; }
    std::string spelled = "\"";
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        const char raw = bytes[index];
        const auto byte = static_cast<unsigned char>(raw);
        // C++23 ignores trigraphs, but compilers still warn about them. Escape
        // the second question mark so warning-as-error builds accept the text.
        const bool trigraph =
            byte == '?' && index > 0 && bytes[index - 1] == '?' && index + 1 < bytes.size() &&
            std::string_view("=/'()!<>-").find(bytes[index + 1]) != std::string_view::npos;
        if (byte == '"' || byte == '\\' || trigraph) {
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

// The escaping is checked at compile time, since these cases are decidable
// here and a build error is found sooner than a failing test.
static_assert(c_string_literal("Math") == "\"Math\"");
// The case that would break under a hex escape: `od`, byte 0x01, `Fd`.
// The input is spelled in octal for the same reason: "od\x01Fd" would itself
// parse as \x01F followed by `d`.
static_assert(c_string_literal("od\001Fd") == "\"od\\001Fd\"");
static_assert(c_string_literal("od\001Fd").size() == 5 + 2 + 3,
              "five source bytes, two quotes, and three extra characters for the one escape");
// Quotes and backslashes take the raw form, because that is exactly where the
// escaped spelling is harder to read than the name.
static_assert(c_string_literal("a\"b") == "R\"(a\"b)\"");
static_assert(c_string_literal("a\\b") == "R\"(a\\b)\"");
// A zero byte survives, which is why the length is emitted beside the
// pointer: strlen would stop here. It also forces the escaped path, since a
// raw string cannot carry a NUL.
static_assert(c_string_literal(std::string_view("a\0b", 3)) == "\"a\\000b\"");
static_assert(!raw_string_is_clearer(std::string_view("a\0b", 3)));

// The raw form is taken only where escaping is the noisy one.
static_assert(!raw_string_is_clearer("Math"), "nothing to escape - the plain form is clearer");
static_assert(raw_string_is_clearer("a\"b"));
static_assert(c_string_literal("a\"b\\c") == "R\"(a\"b\\c)\"");
// It is never taken where the content would close it.
static_assert(!raw_string_is_clearer("a)\"b"), "the content contains the terminator");
static_assert(c_string_literal("a)\"b") == "\"a)\\\"b\"");
static_assert(c_string_literal("?"
                               "?/") == "\"?\\?/\"");

} // namespace ctcompile::cpp
