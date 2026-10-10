//===- ctbrowser/include/ctbrowser/dom/xml.hpp - XML parser -----*- C++ -*-===//
//
// XML into the DOM: the second front end, and a genuinely different one.
//
// Why a second parser at all: `.xhtml` handed to the HTML tree builder is
// wrong in ways that stop the page running. `viewBox` becomes `viewbox`, `<i>`
// becomes `I`, and a `<script>` whose body is wrapped in `<![CDATA[ ... ]]>`
// (how every XHTML file in web-platform-tests writes one, because in XML that
// is the only way to write a `<`) reaches the JavaScript engine with the
// `<![CDATA[` still on the front, so the test fails with a parse error before
// running a single assertion.
//
// What differs from HTML is a short list, because XML is the small language:
//
//   * Nothing is implied. No `<head>`, no `<body>`, no `<tbody>`, no closing a
//     `<p>` because another one opened. An element ends where its end tag says
//     and nowhere else, which is why this file has no insertion modes, no list
//     of active formatting elements and no adoption agency.
//   * Every tag may self-close. `<br/>` and `<div/>` both close; there is no
//     void-element table because there are no void elements.
//   * Case is preserved, everywhere, for elements and attributes alike.
//   * `<![CDATA[ ... ]]>` is character data with no markup in it.
//   * Prefixes are real. `xmlns` and `xmlns:p` bind for the element they are on
//     and its descendants, and an element or attribute name is resolved against
//     that scope rather than guessed from the tag.
//   * Errors are fatal. A mismatched end tag, an unclosed element, a stray `<`
//     or an unknown entity is a fatal error and the document does not load. A
//     browser shows its own error page; this reports the error and builds the
//     tree it had, which is what lets a caller decide.
//
// Not supported: an internal DTD subset is read only for the general entities
// it declares with a literal value; those, the five predefined ones and
// numeric character references are resolved, but a parameter or external
// entity is not. There is no validation and no external entity fetching, both
// of which every browser also refuses. A doctype, a comment or a processing
// instruction outside the root element lands under the Document node (see
// document::document_node); a CDATA section is a CDATASection node and a
// processing instruction inside the tree a ProcessingInstruction. An inline
// `<svg>` is put in the SVG namespace but its source is not captured the way
// the HTML tree builder captures it, so it does not rasterise: the element and
// its children are in the DOM and nothing draws.
//
// Measured: all 80 `.xhtml`, `.xht` and `.xml` files in the web-platform-tests
// checkout at pin `3f6b09ae` were parsed with this, and 77 are well-formed by
// it. The three that are not are each correct: two are zero-byte files
// (`Document-createElement-namespace-tests/empty.xhtml` and `.xml`), and one
// has a genuinely unterminated `<?start name="p">` that the test wrote on
// purpose (`html/dom/partial-updates/tentative/resources/template-for.xhtml`).
// (`Element-firstElementChild-entity-xhtml.xhtml` declares `&tree;` in its
// internal subset; it parses because internal-subset entities are expanded.)
//
// Namespace URIs are interned into the document's atom table and reach the tree
// through `document::set_attribute_ns`, the same path `setAttributeNS` uses, so
// an XML-parsed attribute and a script-created one are the same object.
//
//===----------------------------------------------------------------------===//

#pragma once
#include <string>
#include <string_view>

#include <ctbrowser/core/core.hpp>

#include <ctbrowser/dom/document.hpp>
#include <ctbrowser/dom/html.hpp>
#include <ctbrowser/dom/node.hpp>

namespace ctbrowser {

/// The XHTML and SVG namespace URIs, which decide an element's `node_ns` (the
/// distinction that gates script execution, <style> collection and page text),
/// and MathML's, which is `node_ns::other` with the URI recorded on the
/// document (document::element_namespace): the HTML parser's second foreign
/// vocabulary, and the one `<math>` puts an element in.
inline constexpr std::string_view xhtml_namespace = "http://www.w3.org/1999/xhtml";
inline constexpr std::string_view svg_namespace = "http://www.w3.org/2000/svg";
inline constexpr std::string_view mathml_namespace = "http://www.w3.org/1998/Math/MathML";
inline constexpr std::string_view xml_namespace = "http://www.w3.org/XML/1998/namespace";
inline constexpr std::string_view xmlns_namespace = "http://www.w3.org/2000/xmlns/";

/// XML 1.0's S production: space, tab, CR, LF (HTML's whitespace minus the form
/// feed). core/algorithms.hpp takes the set as a parameter for this reason:
/// HTML, JavaScript and XML each have their own, and unifying them would be a bug.
inline constexpr std::string_view xml_whitespace = " \t\r\n";

/// The result of parse_xml: the tree built, plus the first fatal error if any.
struct xml_parse_result {
    parse_result tree;
    /// Empty when the document is well-formed. XML has no recovery, so a
    /// non-empty message means the tree stops wherever the error was found;
    /// it is not a warning.
    std::string error;
    /// Position of the error, 1-based, and only meaningful when `error` is set.
    std::size_t line = 0;
    std::size_t column = 0;
};

/// Parses `source` as XML into `doc`, which is left marked as an XML document
/// (`document::xml()`) whether or not it was well-formed: a document that
/// failed to parse is still not an HTML one.
[[nodiscard]] xml_parse_result parse_xml(document & doc, std::string_view source);

/// Whether `path`'s extension says XML rather than HTML. Used where the caller
/// has no content type (a file on disk) and the extension is the only signal.
/// Never sniffs the bytes: `<?xml` is optional in XML 1.0 and present in
/// plenty of documents served as text/html.
[[nodiscard]] bool is_xml_extension(std::string_view path);

} // namespace ctbrowser
