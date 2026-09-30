// The XML front end - dom/xml.hpp - and the six places it disagrees with the
// HTML tree builder. Every assertion here is a thing that was measurably wrong
// when an `.xhtml` file was handed to `parse_html`.
#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>

#include "check.hpp"
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;

namespace {

// The tag of the first descendant of `from` whose text matches, or "" - a
// tiny walker, because these documents are five nodes deep.
[[nodiscard]] std::vector<node_id> descendants(const read_txn & r, node_id from) {
    std::vector<node_id> out;
    std::vector<node_id> stack{from};
    while (!stack.empty()) {
        const node_id id = stack.back();
        stack.pop_back();
        out.push_back(id);
        const auto kids = r.children(id);
        for (std::size_t i = kids.size(); i-- > 0;) { stack.push_back(kids[i]); }
    }
    return out;
}

[[nodiscard]] std::string tag_of(const atom_table & atoms, const read_txn & r, node_id id) {
    return std::string{atoms.text(r.tag(id).value_or(atom{}))};
}

// THE ONE THAT COST TWELVE WPT FILES. In XML a `<script>` body is written
// inside a marked section, because `<` is not allowed raw; the HTML tokenizer
// has no marked sections outside foreign content, so the script the engine
// compiled began with the six characters `<![CDA` and every one of those files
// reported `parse error: expression - at 1:1` without running an assertion.
void test_cdata_is_character_data() {
    atom_table atoms;
    document doc{atoms};
    const xml_parse_result out = parse_xml(doc, R"(<?xml version="1.0"?>
<html xmlns="http://www.w3.org/1999/xhtml"><body><script><![CDATA[
var a = 1 < 2;
]]></script></body></html>)");
    CHECK(out.error.empty());
    const auto r = doc.read();
    const auto all = descendants(r, out.tree.root);
    std::string script_text;
    for (const node_id id : all) {
        if (tag_of(atoms, r, id) == "script") {
            for (const node_id kid : r.children(id)) {
                // A CDATASection is a Text node - `is_text_kind` - and that is
                // exactly what lets a script written the XML way run.
                if (is_text_kind(r.kind(kid).value_or(node_kind::element))) {
                    script_text += std::string{r.text(kid)};
                }
            }
        }
    }
    CHECK(script_text.find("<![CDATA[") == std::string::npos);
    CHECK(script_text.find("var a = 1 < 2;") != std::string::npos);
}

// A CDATA section is character data, legal only inside the document element.
// After a self-closed root there is no open element, and `cdata()` read an
// empty `open_` stack - UB, a crash under asan (security review OOBREAD-001).
// It is a well-formedness error now, reported rather than crashed on.
void test_cdata_outside_the_root_is_an_error_not_a_crash() {
    atom_table atoms;
    document doc{atoms};
    const xml_parse_result out = parse_xml(doc, R"(<?xml version="1.0"?><r/><![CDATA[x]]>)");
    CHECK(!out.error.empty());
}

// `viewBox` stays `viewBox` and `<i>` stays `i`. The HTML tokenizer folds both
// and `dom/nodes/Node-nodeName-xhtml.xhtml` asserts the second by name.
void test_case_is_preserved() {
    atom_table atoms;
    document doc{atoms};
    const xml_parse_result out = parse_xml(doc, R"(<html xmlns="http://www.w3.org/1999/xhtml">
<body><i id="Mixed" viewBox="0 0 1 1"/></body></html>)");
    CHECK(out.error.empty());
    const auto r = doc.read();
    bool saw = false;
    for (const node_id id : descendants(r, out.tree.root)) {
        if (tag_of(atoms, r, id) != "i") { continue; }
        saw = true;
        bool saw_viewbox = false;
        for (const attribute & a : r.attributes(id)) {
            if (atoms.text(a.name) == "viewBox") { saw_viewbox = true; }
        }
        CHECK(saw_viewbox);
    }
    CHECK(saw);
}

// Nothing is implied. `<p>a<p>b` is ONE paragraph containing text and a
// paragraph, because in XML an element ends where its end tag says.
void test_nothing_is_implied() {
    atom_table atoms;
    document doc{atoms};
    const xml_parse_result out =
        parse_xml(doc, R"(<r xmlns="http://www.w3.org/1999/xhtml"><p>a<p>b</p></p></r>)");
    CHECK(out.error.empty());
    const auto r = doc.read();
    const auto kids = r.children(out.tree.root);
    CHECK_EQ(kids.size(), 1u);
    CHECK_EQ(tag_of(atoms, r, kids[0]), std::string{"p"});
    // text "a" and the inner <p>
    CHECK_EQ(r.children(kids[0]).size(), 2u);
}

// Every tag may self-close, not only the fourteen HTML void elements.
void test_any_tag_self_closes() {
    atom_table atoms;
    document doc{atoms};
    const xml_parse_result out =
        parse_xml(doc, R"(<r xmlns="http://www.w3.org/1999/xhtml"><div/><span/>x</r>)");
    CHECK(out.error.empty());
    const auto r = doc.read();
    const auto kids = r.children(out.tree.root);
    CHECK_EQ(kids.size(), 3u);
    CHECK_EQ(tag_of(atoms, r, kids[0]), std::string{"div"});
    CHECK(r.children(kids[0]).empty());
    CHECK_EQ(tag_of(atoms, r, kids[1]), std::string{"span"});
    CHECK(r.children(kids[1]).empty());
}

// A prefix is resolved against the scope it is written in, and an UNPREFIXED
// attribute is in no namespace however the default namespace is bound.
void test_namespaces() {
    atom_table atoms;
    document doc{atoms};
    const xml_parse_result out = parse_xml(doc, R"(<html xmlns="http://www.w3.org/1999/xhtml"
      xmlns:x="urn:example"><body class="c" x:k="v" xml:lang="en"/></html>)");
    CHECK(out.error.empty());
    const auto r = doc.read();
    bool saw = false;
    for (const node_id id : descendants(r, out.tree.root)) {
        if (tag_of(atoms, r, id) != "body") { continue; }
        saw = true;
        for (const attribute & a : r.attributes(id)) {
            const std::string_view name = atoms.text(a.name);
            const std::string_view ns = a.ns ? atoms.text(a.ns) : std::string_view{};
            if (name == "class") { CHECK(ns.empty()); }
            if (name == "x:k") { CHECK_EQ(std::string{ns}, std::string{"urn:example"}); }
            if (name == "xml:lang") {
                CHECK_EQ(std::string{ns}, std::string{"http://www.w3.org/XML/1998/namespace"});
            }
        }
    }
    CHECK(saw);
    CHECK(r.element_ns(out.tree.root) == node_ns::html);
}

// XML is draconian, and this is the half a browser shows an error page for.
void test_wellformedness_is_fatal() {
    {
        atom_table atoms;
        document doc{atoms};
        const xml_parse_result out = parse_xml(doc, "<a><b></a></b>");
        CHECK(!out.error.empty());
    }
    {
        atom_table atoms;
        document doc{atoms};
        const xml_parse_result out = parse_xml(doc, "<a>&nosuch;</a>");
        CHECK(!out.error.empty());
    }
    {
        atom_table atoms;
        document doc{atoms};
        const xml_parse_result out = parse_xml(doc, "<a><b unquoted=x/></a>");
        CHECK(!out.error.empty());
    }
    {
        atom_table atoms;
        document doc{atoms};
        // No boolean attributes: `<input checked>` is a well-formedness error.
        const xml_parse_result out = parse_xml(doc, "<a><input checked/></a>");
        CHECK(!out.error.empty());
    }
    // Namespace well-formedness: a name is a QName, one colon at most and
    // neither half empty.
    for (const std::string_view bad :
         {"<a :x=\"1\"/>", "<a ::=\"1\"/>", "<a xmlns:=\"urn:x\"/>", "<:a/>", "<a:b:c/>"}) {
        atom_table atoms;
        document doc{atoms};
        const xml_parse_result out = parse_xml(doc, bad);
        CHECK(!out.error.empty());
    }
    {
        atom_table atoms;
        document doc{atoms};
        const xml_parse_result out = parse_xml(doc, "<a>x</a>");
        CHECK(out.error.empty());
    }
    // A lone surrogate in character data is U+FFFD, not a fatal error.
    {
        atom_table atoms;
        document doc{atoms};
        const xml_parse_result out =
            parse_xml(doc, "<a>x\xED\xA0\xBCy<![CDATA[\xED\xB0\x80]]></a>");
        CHECK(out.error.empty());
        const auto r = doc.read();
        const auto kids = r.children(r.root());
        CHECK_EQ(std::string{r.text(kids[0])}, std::string{"x\xEF\xBF\xBDy"});
        CHECK_EQ(std::string{r.text(kids[1])}, std::string{"\xEF\xBF\xBD"});
    }
}

// The five predefined entities and both spellings of a character reference.
// `&copy;` is NOT one of them - that is an HTML entity, and an XML document
// that uses it without declaring it does not load.
void test_references() {
    atom_table atoms;
    document doc{atoms};
    const xml_parse_result out = parse_xml(doc, "<a>&lt;&amp;&gt;&quot;&apos;&#65;&#x42;</a>");
    CHECK(out.error.empty());
    const auto r = doc.read();
    std::string text;
    for (const node_id kid : r.children(out.tree.root)) { text += std::string{r.text(kid)}; }
    CHECK_EQ(text, std::string{"<&>\"'AB"});
}

// A general entity the internal subset declares expands where it is
// referenced - as CONTENT, so markup in it makes elements - and in an
// attribute value; character references in its value are resolved when it is
// declared; the first declaration wins; a parameter entity, an external one
// and the other declaration kinds are passed over; and a self-reference is
// an error rather than a loop. (Element-firstElementChild-entity-xhtml.xhtml.)
void test_internal_subset_entities() {
    atom_table atoms;
    document doc{atoms};
    const xml_parse_result out = parse_xml(doc, R"(<!DOCTYPE a [
  <!ELEMENT a ANY>
  <!ATTLIST a x CDATA ">">
  <!-- a comment with > in it -->
  <!ENTITY tree "<b id='k'>leaf</b>">
  <!ENTITY amp2 "&#38;lt;">
  <!ENTITY amp2 "second">
  <!ENTITY % pe "ignored">
  <!ENTITY ext SYSTEM "x.ent">
  <!NOTATION n SYSTEM "n">
]>
<a t="&amp2;">x&tree;y</a>)");
    CHECK(out.error.empty());
    const auto r = doc.read();
    std::string shape;
    for (const node_id kid : r.children(out.tree.root)) {
        shape += r.kind(kid) == node_kind::element
                     ? "<" + std::string{tag_of(atoms, r, kid)} + ">" +
                           std::string{r.text(r.children(kid)[0])}
                     : std::string{r.text(kid)};
        shape += '|';
    }
    CHECK_EQ(shape, std::string{"x|<b>leaf|y|"});
    CHECK_EQ(std::string{r.attribute_value(out.tree.root, atoms.intern("t"))}, std::string{"<"});

    atom_table atoms2;
    document doc2{atoms2};
    const xml_parse_result looped = parse_xml(doc2, "<!DOCTYPE a [<!ENTITY a \"&a;\">]><a>&a;</a>");
    CHECK(!looped.error.empty());
    atom_table atoms3;
    document doc3{atoms3};
    const xml_parse_result undeclared =
        parse_xml(doc3, "<!DOCTYPE a [<!ENTITY b \"\">]><a>&c;</a>");
    CHECK(!undeclared.error.empty());
}

// DOS-019: a 100 KB entity referenced ten thousand times is a gigabyte of
// text; the expansion stops at 16 MB. Many small references, nested ones
// included, still expand exactly - they are written into the consumed prefix
// rather than re-copying the whole input each time.
void test_entity_expansion_is_bounded() {
    std::string bomb = "<!DOCTYPE a [<!ENTITY big \"" + std::string(100 * 1024, 'x') + "\">]><a>";
    for (int i = 0; i < 10000; ++i) { bomb += "&big;"; }
    bomb += "</a>";
    atom_table atoms;
    document doc{atoms};
    CHECK_EQ(parse_xml(doc, bomb).error, std::string{"entity expansion too large"});

    std::string many =
        "<!DOCTYPE a [<!ENTITY i \"<i>&j;</i>\"><!ENTITY j \"ab\">]><a q=\"&j;&j;\">";
    for (int i = 0; i < 4000; ++i) { many += "&i;"; }
    many += "</a>";
    atom_table atoms2;
    document doc2{atoms2};
    const xml_parse_result out = parse_xml(doc2, many);
    CHECK(out.error.empty());
    const auto r = doc2.read();
    CHECK_EQ(r.children(out.tree.root).size(), std::size_t{4000});
    CHECK_EQ(std::string{r.text(r.children(r.children(out.tree.root)[3999])[0])},
             std::string{"ab"});
    CHECK_EQ(std::string{r.attribute_value(out.tree.root, atoms2.intern("q"))},
             std::string{"abab"});
}

// The document knows what it is, and an XML document is never in quirks mode.
void test_document_is_marked() {
    atom_table atoms;
    document doc{atoms};
    CHECK(!doc.xml());
    (void)parse_xml(doc, "<a/>");
    CHECK(doc.xml());
    CHECK(!doc.quirks());
}

void test_extension() {
    CHECK(is_xml_extension("a.xhtml"));
    CHECK(is_xml_extension("a.XHTML"));
    CHECK(is_xml_extension("a.xht"));
    CHECK(is_xml_extension("a.xml"));
    CHECK(!is_xml_extension("a.html"));
    CHECK(!is_xml_extension("a.htm"));
    CHECK(!is_xml_extension("noextension"));
}

// Comments and processing instructions in the prolog do not stop the parse -
// `ProcessingInstruction-escapes-1.xhtml` opens with one.
void test_prolog() {
    atom_table atoms;
    document doc{atoms};
    const xml_parse_result out = parse_xml(doc, R"(<?xml version="1.0" encoding="UTF-8"?>
<?xml-stylesheet href="x.css" type="text/css"?>
<!-- a comment -->
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Strict//EN" "x.dtd">
<html xmlns="http://www.w3.org/1999/xhtml"><body/></html>
<!-- and one after -->)");
    CHECK(out.error.empty());
    const auto r = doc.read();
    CHECK_EQ(tag_of(atoms, r, out.tree.root), std::string{"html"});
}
// THE DEPTH CAP (max_dom_depth): 100000 nested elements are a fatal error
// rather than a tree deeper than every recursive walk downstream can take.
// 511 nested elements still parse (the last sits 511 below the Document), a
// comment inside the 511th would sit at 512 and is refused, and text inside
// the 510th parses.
void test_nesting_is_capped() {
    const auto nested = [](std::size_t levels, std::string_view inner) {
        std::string out;
        for (std::size_t i = 0; i < levels; ++i) { out += "<a>"; }
        out += inner;
        for (std::size_t i = 0; i < levels; ++i) { out += "</a>"; }
        return out;
    };
    atom_table atoms;
    document doc{atoms};
    CHECK_EQ(parse_xml(doc, nested(100000, "")).error, std::string{"elements nested too deeply"});

    atom_table atoms2;
    document doc2{atoms2};
    const xml_parse_result deepest = parse_xml(doc2, nested(max_dom_depth - 1, "<!---->"));
    CHECK_EQ(deepest.error, std::string{"elements nested too deeply"});
    atom_table atoms3;
    document doc3{atoms3};
    const xml_parse_result fits = parse_xml(doc3, nested(max_dom_depth - 1, ""));
    CHECK(fits.error.empty());
    CHECK_EQ(doc3.depth(fits.tree.root), std::size_t{1});
    atom_table atoms4;
    document doc4{atoms4};
    CHECK(parse_xml(doc4, nested(max_dom_depth - 2, "x")).error.empty());
}

} // namespace

int main() {
    test_entity_expansion_is_bounded();
    test_cdata_is_character_data();
    test_cdata_outside_the_root_is_an_error_not_a_crash();
    test_case_is_preserved();
    test_nothing_is_implied();
    test_any_tag_self_closes();
    test_namespaces();
    test_wellformedness_is_fatal();
    test_references();
    test_internal_subset_entities();
    test_document_is_marked();
    test_extension();
    test_prolog();
    test_nesting_is_capped();
    REPORT("xml_parse");
}
