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
                if (r.kind(kid).value_or(node_kind::element) == node_kind::text) {
                    script_text += std::string{r.text(kid)};
                }
            }
        }
    }
    CHECK(script_text.find("<![CDATA[") == std::string::npos);
    CHECK(script_text.find("var a = 1 < 2;") != std::string::npos);
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
    {
        atom_table atoms;
        document doc{atoms};
        const xml_parse_result out = parse_xml(doc, "<a>x</a>");
        CHECK(out.error.empty());
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

} // namespace

int main() {
    test_cdata_is_character_data();
    test_case_is_preserved();
    test_nothing_is_implied();
    test_any_tag_self_closes();
    test_namespaces();
    test_wellformedness_is_fatal();
    test_references();
    test_document_is_marked();
    test_extension();
    test_prolog();
    REPORT("xml_parse");
}
