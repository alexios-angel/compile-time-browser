//===- internal.hpp - Shared name rules -------------------------*- C++ -*-===//
//
// Private to lib/Shell/bindings/document/ and not installed. Holds the element,
// doctype and XML Name rules shared by the files here, plus the NodeIterator
// slot names; all stateless and small, so inline. The rules element/ shares
// (qualified names, attribute names, namespace prefixes) are in ../names.hpp,
// and the namespace URIs are in dom/xml.hpp.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "../names.hpp"

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/bindings.hpp>
#include <ctbrowser/shell/net/url.hpp>
#include <ctbrowser/style/css/parser.hpp>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <locale>
#include <memory>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ctbrowser::shell::detail {

/// Characters that end a tag name in the HTML tokenizer, so an element name
/// may not contain them.
///
/// The name rules here are not the XML `Name` production. What the platform
/// enforces is a serialisation rule: a name has to survive being written into
/// markup and read back, so the only characters it bans are the ones that
/// would end a tag name, plus a first character that would stop the name being
/// a tag name at all. The WPT tables under dom/nodes/ fix the details:
///
///   Document-createElement.html: "f}oo", "f<oo", a lone U+0300 combining
///       accent and "\uFFFFfoo" are valid names, though none matches `Name`.
///       "1foo", "-foo", ".foo", "}foo", "fo o" and "foo>" are not.
///   productions.js: an attribute may be called "0", "~", "'" or "\\"; the
///       first-character rule is for elements only, which is why the two have
///       separate spellings.
///   DOMImplementation-createDocumentType.html: of 81 doctype names, exactly
///       two throw, the one with a `>` and the one with a space. Not even the
///       first-character rule applies there, and "" is legal.
///
/// The checks are byte-wise, and that is exact: every character these rules
/// name is ASCII, and no byte of a multi-byte UTF-8 sequence is ASCII. So "the
/// first code point is not ASCII" is precisely "the first byte is >= 0x80",
/// and a byte scan never sees the interior of a character.
inline constexpr std::string_view element_name_breaks = "\t\n\f\r />";
/// A doctype name is written between `<!DOCTYPE` and `>`, where a `/` is
/// ordinary. Hence the shorter set, and hence `edi:/` being a legal doctype
/// name and an illegal element local name.
inline constexpr std::string_view doctype_name_breaks = "\t\n\f\r >";

/// Whether `c` may start an element name: an ASCII letter, `:`, `_`, or any
/// non-ASCII byte.
[[nodiscard]] inline bool is_element_name_start(unsigned char c) {
    return c >= 0x80 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == ':' || c == '_';
}

/// A "valid element local name": at least one code point, an element name
/// start first, and nothing after it that would end a tag name.
/// U+0000 is refused by all three name rules, because it is the one character
/// the tokenizer cannot carry (it becomes U+FFFD); a string_view literal cannot
/// hold it, hence the separate find.
///
/// The rest of the name has two rules, DOM 4.9 (whatwg/dom#1079): after an
/// ASCII alpha start anything goes but a tag-name break; after `:`, `_` or a
/// non-ASCII start only ASCII alphanumerics, `-`, `.`, `:`, `_` and non-ASCII
/// may follow. So `:!` and `_ ` are refused where `a!` is not (WPT
/// name-validation.html's second table).
[[nodiscard]] inline bool is_valid_element_local_name(std::string_view name) {
    if (name.empty() || !is_element_name_start(static_cast<unsigned char>(name.front()))) {
        return false;
    }
    const auto alpha = [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    };
    if (alpha(static_cast<unsigned char>(name.front()))) {
        return name.find_first_of(element_name_breaks) == std::string_view::npos &&
               name.find('\0') == std::string_view::npos;
    }
    for (const char signed_c : name.substr(1)) {
        const auto c = static_cast<unsigned char>(signed_c);
        if (!(c >= 0x80 || alpha(c) || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == ':' ||
              c == '_')) {
            return false;
        }
    }
    return true;
}

/// Whether `name` is a valid doctype name: anything without a doctype-name
/// break or U+0000, including "".
[[nodiscard]] inline bool is_valid_doctype_name(std::string_view name) {
    return name.find_first_of(doctype_name_breaks) == std::string_view::npos &&
           name.find('\0') == std::string_view::npos;
}

// The third name rule, an attribute's, is looser than both of the above and
// lives in ../names.hpp with the namespace-prefix rule, because element/
// applies the same two.

/// The XML `Name` production's NameStartChar, in full. The only user is
/// `createProcessingInstruction`: unlike createElement it really is checked
/// against XML's Name, character by character. U+00B7 MIDDLE DOT is legal in
/// the middle of a target and not at the start, and U+00D7 MULTIPLICATION SIGN
/// is legal nowhere, which no serialisation rule would distinguish. A
/// processing instruction is XML syntax that HTML merely tolerates, so XML's
/// rule applies.
[[nodiscard]] inline bool is_xml_name_start(char32_t c) {
    return c == U':' || c == U'_' || (c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z') ||
           (c >= 0xC0u && c <= 0xD6u) || (c >= 0xD8u && c <= 0xF6u) ||
           (c >= 0xF8u && c <= 0x2FFu) || (c >= 0x370u && c <= 0x37Du) ||
           (c >= 0x37Fu && c <= 0x1FFFu) || (c >= 0x200Cu && c <= 0x200Du) ||
           (c >= 0x2070u && c <= 0x218Fu) || (c >= 0x2C00u && c <= 0x2FEFu) ||
           (c >= 0x3001u && c <= 0xD7FFu) || (c >= 0xF900u && c <= 0xFDCFu) ||
           (c >= 0xFDF0u && c <= 0xFFFDu) || (c >= 0x10000u && c <= 0xEFFFFu);
}

/// The XML `Name` production's NameChar.
[[nodiscard]] inline bool is_xml_name_char(char32_t c) {
    return is_xml_name_start(c) || c == U'-' || c == U'.' || (c >= U'0' && c <= U'9') ||
           c == 0xB7u || (c >= 0x300u && c <= 0x36Fu) || (c >= 0x203Fu && c <= 0x2040u);
}

/// Whether UTF-8 `text` matches the XML `Name` production.
[[nodiscard]] inline bool is_xml_name(std::string_view text) {
    if (text.empty()) { return false; }
    std::size_t at = 0;
    if (!is_xml_name_start(decode_utf8(text, at))) { return false; }
    while (at < text.size()) {
        if (!is_xml_name_char(decode_utf8(text, at))) { return false; }
    }
    return true;
}

/// A NodeIterator's hidden own properties. traversal.cpp owns them, and the
/// live-range settle (range/live.cpp) moves them when the tree is edited: DOM
/// 6.1's "NodeIterator pre-removing steps". A NodeIterator is registered with
/// the live ranges for exactly that.
inline constexpr std::string_view iterator_root_slot = "__root";
inline constexpr std::string_view iterator_reference_slot = "__referenceNode";
inline constexpr std::string_view iterator_before_slot = "__pointerBeforeReferenceNode";
/// The position a traversal is at while its filter runs (the candidate and the
/// pointer), which the pre-removing steps move too.
inline constexpr std::string_view iterator_inflight_slot = "__inflightNode";
inline constexpr std::string_view iterator_inflight_before_slot = "__inflightBefore";

/// An object's own data property, undefined when it has none.
[[nodiscard]] inline value slot(script::object_object * self, std::string_view name) {
    const value * held = self->find(name);
    return held == nullptr ? value::undefined() : *held;
}

} // namespace ctbrowser::shell::detail
