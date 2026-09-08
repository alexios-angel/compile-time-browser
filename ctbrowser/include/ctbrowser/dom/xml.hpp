#pragma once
#include <string>
#include <string_view>

#include <ctbrowser/core/core.hpp>

#include <ctbrowser/dom/document.hpp>
#include <ctbrowser/dom/html.hpp>
#include <ctbrowser/dom/node.hpp>

// XML into the DOM - the second front end, and a genuinely different one.
//
// WHY A SECOND PARSER AT ALL. `.xhtml` handed to the HTML tree builder is not
// slightly wrong, it is wrong in ways that stop the page running: `viewBox`
// becomes `viewbox`, `<i>` becomes `I`, a `<script>` whose body is wrapped in
// `<![CDATA[ ... ]]>` - which is how every XHTML file in web-platform-tests
// writes one, because in XML that is the only way to write a `<` - is handed to
// the JavaScript engine with the `<![CDATA[` still on the front. That last one
// is not a subtle difference in a tree; it is twelve WPT files reporting
// `parse error: expression - at 1:1` and never running a single assertion.
//
// WHAT XML IS, next to HTML, and it is a short list because XML is the small
// language:
//
//   * NOTHING IS IMPLIED. No `<head>`, no `<body>`, no `<tbody>`, no closing a
//     `<p>` because another one opened. An element ends where its end tag says
//     and nowhere else, which is why this file has no insertion modes, no list
//     of active formatting elements and no adoption agency.
//   * EVERY TAG MAY SELF-CLOSE. `<br/>` and `<div/>` both close; there is no
//     void-element table because there are no void elements.
//   * CASE IS PRESERVED, everywhere, for elements and attributes alike.
//   * `<![CDATA[ ... ]]>` is character data with no markup in it.
//   * PREFIXES ARE REAL. `xmlns` and `xmlns:p` bind for the element they are on
//     and its descendants, and an element or attribute name is resolved against
//     that scope rather than guessed from the tag.
//   * IT IS DRACONIAN. A mismatched end tag, an unclosed element, a stray `<`
//     or an unknown entity is a fatal error and the document does not load. A
//     browser shows its own error page; this reports the error and builds the
//     tree it had, which is what lets a caller decide.
//
// WHAT IS NOT HERE, named rather than silently missing: internal DTD subsets
// are SKIPPED rather than read, so an entity an internal subset declares is
// not resolved (the five predefined ones and numeric character references
// are). There is no validation and no external entity fetching - both are
// what every browser also refuses. `CDATASection` and `ProcessingInstruction`
// have no `node_kind` in this engine, so a CDATA section becomes a text node
// and a processing instruction outside the prolog is dropped; both are
// recorded here so nobody reads their absence as an oversight. An inline
// `<svg>` is put in the SVG namespace but its SOURCE is not captured the way
// the HTML tree builder captures it, so it does not rasterise - the element and
// its children are in the DOM and nothing draws.
//
// MEASURED, rather than asserted: all 80 `.xhtml`, `.xht` and `.xml` files in
// the web-platform-tests checkout at pin `3f6b09ae` were parsed with this, and
// 76 are well-formed by it. The four that are not are each correct: two are
// zero-byte files (`Document-createElement-namespace-tests/empty.xhtml` and
// `.xml`), one declares `&tree;` in an internal subset this parser skips
// (`Element-firstElementChild-entity-xhtml.xhtml`), and one has a genuinely
// unterminated `<?start name="p">` that the test wrote on purpose
// (`html/dom/partial-updates/tentative/resources/template-for.xhtml`).
//
// Namespace URIs are interned into the document's atom table and reach the tree
// through `document::set_attribute_ns`, the same path `setAttributeNS` uses, so
// an XML-parsed attribute and a script-created one are the same object.

namespace ctbrowser {

// The XHTML and SVG namespace URIs, which decide an element's `node_ns` - the
// distinction that gates script execution, <style> collection and page text.
inline constexpr std::string_view xhtml_namespace = "http://www.w3.org/1999/xhtml";
inline constexpr std::string_view svg_namespace = "http://www.w3.org/2000/svg";
inline constexpr std::string_view xml_namespace = "http://www.w3.org/XML/1998/namespace";
inline constexpr std::string_view xmlns_namespace = "http://www.w3.org/2000/xmlns/";

struct xml_parse_result {
    parse_result tree;
    // Empty when the document is well-formed. XML has no recovery, so a
    // non-empty message means the tree stops wherever the error was found -
    // it is not a warning.
    std::string error;
    // 1-based, and only meaningful when `error` is set.
    std::size_t line = 0;
    std::size_t column = 0;
};

// Parse `source` as XML into `doc`, which is left marked as an XML document
// (`document::xml()`) whether or not it was well-formed - a document that
// failed to parse is still not an HTML one.
[[nodiscard]] xml_parse_result parse_xml(document & doc, std::string_view source);

// Does this look like XML rather than HTML? Used where the caller has no
// content type - a file on disk - and the extension is the only signal. Never
// sniffs the bytes: `<?xml` is optional in XML 1.0 and present in plenty of
// documents served as text/html.
[[nodiscard]] bool is_xml_extension(std::string_view path);

} // namespace ctbrowser
