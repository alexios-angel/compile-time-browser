#pragma once
// Private to lib/Shell/bindings/document/. NOT installed and in no file set:
// include/ctbrowser/shell/bindings.hpp declares dom_bindings whole, and this
// exists only so its document half can be more than one file - it was 3,071
// lines in one until 2026-09-08. The includes are document.cpp's, so every
// file here sees exactly what that one saw.
//
// What follows is the name-production namespace that file opened with: the
// namespace URIs, qualified names, and the element, doctype, attribute and
// XML Name rules. Stateless and small, so they are inline here rather than
// declared here and defined in a ninth file.

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/bindings.hpp>
#include <ctbrowser/shell/net/url.hpp>
#include <ctbrowser/style/css/parser.hpp>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ctbrowser::shell::detail {

// The parts of a URL, as `location` reports them.
//
// href alone is not enough for a library: `location.search` is where a page
// reads its own query string and `location.pathname` is what a router matches
// on, and both are read WITHOUT a guard - the idiom is
// `location.search.substring(1)`, so an absent one is not a missing feature but
// a TypeError on the first line of the library's setup.
//
// Parsed rather than tracked, because href is the one thing the browser
// actually knows and keeping seven fields in step with it by hand is how they
// drift apart.
// url_parts and split_url USED TO BE HERE. They are gone, and the reason is
// worth keeping: this one reached for the last colon in the authority with no
// bracket guard, so `http://[::1]/` reported hostname `[:` and port `1]`. Its
// twin in net.cpp guarded exactly that case. Two parsers for one job, written
// apart, drifted apart - and nothing compared them because nothing could.
//
// shell/net/url.hpp parses once now, for both.

// The four namespaces the DOM names by URI. Spelled out rather than derived,
// because getting one character wrong makes a NamespaceError fire on the valid
// case and not on the invalid one, and nothing about the failure would say so.
inline constexpr std::string_view html_namespace = "http://www.w3.org/1999/xhtml";
inline constexpr std::string_view svg_namespace = "http://www.w3.org/2000/svg";
inline constexpr std::string_view xml_namespace = "http://www.w3.org/XML/1998/namespace";
inline constexpr std::string_view xmlns_namespace = "http://www.w3.org/2000/xmlns/";

// The prefix and the local part of a qualified name, split at the FIRST colon.
// `a:b:c` is prefix `a` and local `b:c`, which is what the DOM says and is not
// what the XML QName production says - the two disagree and the DOM is what a
// page is measured against.
struct qualified_name {
    std::string_view prefix; // empty when there is no colon
    std::string_view local;
    bool has_colon = false;
};

[[nodiscard]] inline qualified_name split_qualified(std::string_view name) {
    const std::size_t colon = name.find(':');
    if (colon == std::string_view::npos) { return qualified_name{{}, name, false}; }
    return qualified_name{name.substr(0, colon), name.substr(colon + 1), true};
}

// WHAT A NAME MAY CONTAIN, AND WHY IT IS NOT THE XML `Name` PRODUCTION.
//
// The obvious reading of "createElement throws unless the name matches Name"
// is wrong in both directions, and the DOM's own conformance tests are what
// say so. What the platform enforces is a SERIALISATION rule: a name has to
// survive being written into markup and read back, so the only characters it
// bans are the ones that would end a tag name in the HTML tokenizer, plus a
// first character that would stop the name being a tag name at all.
//
// Measured against the tables rather than inferred from prose, because the
// tables are what an implementation is scored on:
//
//   dom/nodes/Document-createElement.html       "f}oo", "f<oo", a lone
//       U+0300 combining accent and "\uFFFFfoo" are VALID names - none of
//       which matches `Name`. "1foo", "-foo", ".foo", "}foo", "fo o" and
//       "foo>" are not.
//   dom/nodes/productions.js                    an ATTRIBUTE may be called
//       "0", "~", "'" or "\\": the first-character rule is the ELEMENT one
//       only, which is why the two have separate spellings below.
//   dom/nodes/DOMImplementation-createDocumentType.html   of 81 doctype names,
//       exactly two throw - the one with a `>` and the one with a space. Not
//       even the first-character rule applies there, and "" is legal.
//
// BYTE-WISE ON PURPOSE, and it is exact rather than an approximation: every
// character these rules name is ASCII, and no byte of a multi-byte UTF-8
// sequence is ASCII. So "the first code point is not an ASCII code point" is
// precisely "the first byte is >= 0x80", and scanning the rest of the string
// byte by byte can never see the interior of a character. No decoder, and no
// dependence on how the VM happens to store a string.
inline constexpr std::string_view element_name_breaks = "\t\n\f\r />";
// A doctype name is written between `<!DOCTYPE` and `>`, where a `/` is
// ordinary - hence the shorter set, and hence `edi:/` being a legal doctype
// name and an illegal element local name.
inline constexpr std::string_view doctype_name_breaks = "\t\n\f\r >";

[[nodiscard]] inline bool is_element_name_start(unsigned char c) {
    return c >= 0x80 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == ':' || c == '_';
}

// A "valid element local name": at least one code point, an element name start
// first, and nothing after it that would end a tag name.
[[nodiscard]] inline bool is_valid_element_local_name(std::string_view name) {
    if (name.empty() || !is_element_name_start(static_cast<unsigned char>(name.front()))) {
        return false;
    }
    return name.find_first_of(element_name_breaks) == std::string_view::npos;
}

[[nodiscard]] inline bool is_valid_doctype_name(std::string_view name) {
    return name.find_first_of(doctype_name_breaks) == std::string_view::npos;
}

// AND THE THIRD NAME RULE, WHICH IS AN ATTRIBUTE'S, and it is LOOSER than
// both of the two above rather than stricter.
//
// `dom/nodes/productions.js` is the whole of the evidence and it is blunt:
//
//     var invalid_names = [""]
//     var valid_names = ["x", "X", ":", "a:0", "invalid^Name", "\\", "'",
//                        '"', "0", "0:a", ":a", "x:y:x", "~"]
//
// Thirteen names, every one of which the XML `Name` production refuses, and
// every one of which `Document-createAttribute.html` and `attributes.html`
// require to SUCCEED. Only the empty string throws. That is not an oversight
// in the corpus: an attribute name is measured by whether it survives being
// written into a start tag and read back, and the HTML tokenizer's attribute
// name state ends the name on whitespace, `/`, `>` and `=` and on nothing
// else. `"` and `'` inside one are a parse error the tokenizer explicitly
// recovers from BY INCLUDING THE CHARACTER, so they round-trip; `~` and `^`
// are not special at all.
//
// Hence a break set of its own rather than a share of `element_name_breaks`,
// and NO first-character rule: `"0"` and `":a"` are legal attribute names and
// illegal element names, which is exactly the pair productions.js draws.
//
// NOT the rule `valid_attribute_name` in bindings/element/attributes.cpp applies to
// `setAttribute` and `toggleAttribute` - that one is an ASCII approximation of
// `Name` and refuses twelve of the thirteen above. The two disagree, this one
// is the one the corpus scores, and reconciling them is element/attributes.cpp's to do.
inline constexpr std::string_view attribute_name_breaks = "\t\n\f\r /=>";

[[nodiscard]] inline bool is_valid_attribute_name(std::string_view name) {
    // U+0000 is the one character the tokenizer cannot carry: it becomes
    // U+FFFD, so a name containing one does not read back as itself.
    return !name.empty() && name.find_first_of(attribute_name_breaks) == std::string_view::npos &&
           name.find('\0') == std::string_view::npos;
}

// One code point out of UTF-8, and the byte count it took. A truncated or
// malformed sequence yields the lead byte itself, which is not a code point
// any name production admits - so bad input is REJECTED rather than
// approximated, which is the answer a name check wants.
[[nodiscard]] inline char32_t next_code_point(std::string_view text, std::size_t & at) {
    const auto lead = static_cast<unsigned char>(text[at]);
    std::size_t extra = 0;
    char32_t built = 0;
    if (lead < 0x80u) {
        ++at;
        return static_cast<char32_t>(lead);
    }
    if ((lead & 0xE0u) == 0xC0u) {
        extra = 1;
        built = static_cast<char32_t>(lead & 0x1Fu);
    } else if ((lead & 0xF0u) == 0xE0u) {
        extra = 2;
        built = static_cast<char32_t>(lead & 0x0Fu);
    } else if ((lead & 0xF8u) == 0xF0u) {
        extra = 3;
        built = static_cast<char32_t>(lead & 0x07u);
    } else {
        ++at;
        return static_cast<char32_t>(lead);
    }
    if (at + extra >= text.size()) {
        ++at;
        return static_cast<char32_t>(lead);
    }
    for (std::size_t i = 1; i <= extra; ++i) {
        const auto byte = static_cast<unsigned char>(text[at + i]);
        if ((byte & 0xC0u) != 0x80u) {
            ++at;
            return static_cast<char32_t>(lead);
        }
        built = static_cast<char32_t>((built << 6) | (byte & 0x3Fu));
    }
    at += extra + 1;
    return built;
}

// THE XML `Name` PRODUCTION, in full, and the one place the engine needs it.
//
// `createProcessingInstruction` is the outlier: unlike createElement it really
// is measured against XML's Name, and the test proves it character by
// character - U+00B7 MIDDLE DOT is legal in the middle of a target and not at
// the start, and U+00D7 MULTIPLICATION SIGN is legal nowhere, which no
// serialisation rule would ever distinguish. A processing instruction is XML
// syntax that HTML merely tolerates, so it is XML's rule that applies.
[[nodiscard]] inline bool is_xml_name_start(char32_t c) {
    return c == U':' || c == U'_' || (c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z') ||
           (c >= 0xC0u && c <= 0xD6u) || (c >= 0xD8u && c <= 0xF6u) ||
           (c >= 0xF8u && c <= 0x2FFu) || (c >= 0x370u && c <= 0x37Du) ||
           (c >= 0x37Fu && c <= 0x1FFFu) || (c >= 0x200Cu && c <= 0x200Du) ||
           (c >= 0x2070u && c <= 0x218Fu) || (c >= 0x2C00u && c <= 0x2FEFu) ||
           (c >= 0x3001u && c <= 0xD7FFu) || (c >= 0xF900u && c <= 0xFDCFu) ||
           (c >= 0xFDF0u && c <= 0xFFFDu) || (c >= 0x10000u && c <= 0xEFFFFu);
}

[[nodiscard]] inline bool is_xml_name_char(char32_t c) {
    return is_xml_name_start(c) || c == U'-' || c == U'.' || (c >= U'0' && c <= U'9') ||
           c == 0xB7u || (c >= 0x300u && c <= 0x36Fu) || (c >= 0x203Fu && c <= 0x2040u);
}

[[nodiscard]] inline bool is_xml_name(std::string_view text) {
    if (text.empty()) { return false; }
    std::size_t at = 0;
    if (!is_xml_name_start(next_code_point(text, at))) { return false; }
    while (at < text.size()) {
        if (!is_xml_name_char(next_code_point(text, at))) { return false; }
    }
    return true;
}

} // namespace ctbrowser::shell::detail
