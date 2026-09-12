// THE THREE NODE KINDS A PAGE HOLDS AND NEVER SEES DRAWN: DocumentType,
// ProcessingInstruction and CDATASection, and the Document node's child list
// they live in.
//
// What these pin down is the SHAPE, not the bindings: that `<!DOCTYPE html>`
// becomes a node ahead of `<html>` under the Document node, that the document
// element sits in that list WITHOUT a parent pointer (document::document_node
// says why), that the XML front end produces all three kinds where XML has
// them, and that each kind carries exactly what DOM 4.4 says it carries. The
// script-visible half - nodeType, nodeName, `document.doctype`, the insertion
// rules - is in unit/document_node.cpp and unit/document_api_wpt.cpp.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>

#include "check.hpp"
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;

namespace {

std::vector<node_kind> kinds_of(const read_txn & txn, node_id parent) {
    std::vector<node_kind> out;
    for (const node_id child : txn.children(parent)) {
        out.push_back(txn.kind(child).value_or(node_kind::comment));
    }
    return out;
}

void test_created_nodes_carry_what_the_dom_says() {
    atom_table atoms;
    document doc{atoms};
    const node_id doctype = doc.create_document_type(atoms.intern("HTML"), "-//W3C//DTD//EN",
                                                     "http://example.test/x.dtd");
    const node_id pi =
        doc.create_processing_instruction(atoms.intern("xml-stylesheet"), "href=\"a.css\"");
    const node_id cdata = doc.create_cdata_section("<b>not markup</b>");
    const auto r = doc.read();
    CHECK(r.kind(doctype).value() == node_kind::document_type);
    CHECK(r.name(doctype) == atoms.intern("HTML")); // as written, never folded
    CHECK_EQ(std::string{r.public_id(doctype)}, std::string{"-//W3C//DTD//EN"});
    CHECK_EQ(std::string{r.system_id(doctype)}, std::string{"http://example.test/x.dtd"});
    // NOT an element: `tag()` is how half the engine asks "is this an element".
    CHECK(!r.tag(doctype).has_value());
    CHECK(!r.tag(pi).has_value());
    CHECK(r.kind(pi).value() == node_kind::processing_instruction);
    CHECK(r.name(pi) == atoms.intern("xml-stylesheet"));
    CHECK_EQ(std::string{r.text(pi)}, std::string{"href=\"a.css\""});
    CHECK(r.kind(cdata).value() == node_kind::cdata_section);
    CHECK(is_text_kind(r.kind(cdata).value()));
    CHECK(!is_text_kind(r.kind(pi).value()));
    CHECK_EQ(std::string{r.text(cdata)}, std::string{"<b>not markup</b>"});
    // The identifiers may be empty and stay separate.
    const node_id bare = doc.create_document_type(atoms.intern("html"), "", "");
    CHECK(r.public_id(bare).empty());
    CHECK(r.system_id(bare).empty());
    const node_id system_only = doc.create_document_type(atoms.intern("x"), "", "sys");
    CHECK(r.public_id(system_only).empty());
    CHECK_EQ(std::string{r.system_id(system_only)}, std::string{"sys"});
}

void test_the_html_parser_puts_the_doctype_ahead_of_html() {
    atom_table atoms;
    document doc{atoms};
    (void)parse_html(doc, "<!DOCTYPE html PUBLIC '-//W3C//DTD XHTML 1.0 Strict//EN' "
                          "\"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">"
                          "<html><body>x</body></html>");
    const auto r = doc.read();
    const node_id html = r.root();
    CHECK(r.kind(html).value() == node_kind::element);
    // The Document node's children: the doctype, then the element.
    const std::vector<node_kind> kinds = kinds_of(r, r.document_node());
    CHECK_EQ(kinds.size(), 2u);
    CHECK(kinds.size() == 2 && kinds[0] == node_kind::document_type);
    CHECK(kinds.size() == 2 && kinds[1] == node_kind::element);
    const node_id doctype = r.children(r.document_node())[0];
    CHECK(r.name(doctype) == atoms.intern("html"));
    CHECK_EQ(std::string{r.public_id(doctype)}, std::string{"-//W3C//DTD XHTML 1.0 Strict//EN"});
    CHECK_EQ(std::string{r.system_id(doctype)},
             std::string{"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd"});
    // THE DOCTYPE'S PARENT IS THE DOCUMENT NODE; THE ELEMENT'S IS EMPTY. The
    // second half is the invariant every engine walk depends on.
    CHECK(r.parent(doctype) == r.document_node());
    CHECK(!r.parent(html));
    CHECK(is_document_child(r, doctype));
    CHECK(is_document_child(r, html));
    CHECK(!is_document_child(r, r.children(html)[0]));
    CHECK(!doc.quirks());
}

void test_a_document_without_a_doctype_has_one_child() {
    atom_table atoms;
    document doc{atoms};
    (void)parse_html(doc, "<p>x</p>");
    const auto r = doc.read();
    CHECK_EQ(kinds_of(r, r.document_node()).size(), 1u);
    CHECK(r.children(r.document_node())[0] == r.root());
    CHECK(doc.quirks());
    // A doctype AFTER content is a parse error and ignored - HTML 13.2.6.4.
    atom_table atoms2;
    document late{atoms2};
    (void)parse_html(late, "<p>x</p><!DOCTYPE html>");
    CHECK_EQ(kinds_of(late.read(), late.document_node()).size(), 1u);
}

void test_the_xml_parser_produces_all_three_kinds() {
    atom_table atoms;
    document doc{atoms};
    const xml_parse_result out = parse_xml(doc, R"(<?xml version="1.0"?>
<?xml-stylesheet href="a.css"?>
<!DOCTYPE root PUBLIC "-//P//" "sys.dtd" [ <!ENTITY x "y"> ]>
<!-- before -->
<root><?inner  data here ?><![CDATA[<raw>]]><a/></root>
<!-- after -->
<?tail?>)");
    CHECK_EQ(out.error, std::string{});
    const auto r = doc.read();
    // The XML declaration is NOT a processing instruction and leaves nothing.
    const std::vector<node_kind> kinds = kinds_of(r, r.document_node());
    const std::vector<node_kind> expected{node_kind::processing_instruction,
                                          node_kind::document_type,
                                          node_kind::comment,
                                          node_kind::element,
                                          node_kind::comment,
                                          node_kind::processing_instruction};
    CHECK(kinds == expected);
    if (kinds != expected) { return; }
    const std::span<const node_id> top = r.children(r.document_node());
    CHECK(r.name(top[0]) == atoms.intern("xml-stylesheet"));
    CHECK_EQ(std::string{r.text(top[0])}, std::string{"href=\"a.css\""});
    CHECK(r.name(top[1]) == atoms.intern("root"));
    CHECK_EQ(std::string{r.public_id(top[1])}, std::string{"-//P//"});
    CHECK_EQ(std::string{r.system_id(top[1])}, std::string{"sys.dtd"});
    CHECK(top[3] == r.root());
    CHECK(!r.parent(top[3]));
    CHECK(r.parent(top[5]) == r.document_node());
    CHECK(r.name(top[5]) == atoms.intern("tail"));
    CHECK(r.text(top[5]).empty());
    // Inside the root: a PI whose data starts after the first white space and
    // keeps its trailing space, a CDATA section, an element.
    const std::vector<node_kind> inner = kinds_of(r, r.root());
    const std::vector<node_kind> inner_expected{node_kind::processing_instruction,
                                                node_kind::cdata_section, node_kind::element};
    CHECK(inner == inner_expected);
    if (inner != inner_expected) { return; }
    CHECK_EQ(std::string{r.text(r.children(r.root())[0])}, std::string{"data here "});
    CHECK_EQ(std::string{r.text(r.children(r.root())[1])}, std::string{"<raw>"});
}

void test_document_children_move_like_any_node() {
    atom_table atoms;
    document doc{atoms};
    (void)parse_html(doc, "<!DOCTYPE html><p>x</p>");
    const node_id html = doc.root();
    const node_id doctype = doc.read().children(doc.document_node())[0];
    // Remove the doctype: the list follows, the element stays put.
    CHECK(doc.remove_child(doctype).has_value());
    {
        const auto r = doc.read();
        CHECK_EQ(r.children(r.document_node()).size(), 1u);
        CHECK(!r.parent(doctype));
    }
    // Put it back ahead of the element, and a comment after it.
    CHECK(doc.insert_before(doc.document_node(), doctype, html).has_value());
    const node_id comment = doc.create_comment("tail");
    CHECK(doc.append_child(doc.document_node(), comment).has_value());
    {
        const auto r = doc.read();
        const std::vector<node_kind> kinds = kinds_of(r, r.document_node());
        const std::vector<node_kind> expected{node_kind::document_type, node_kind::element,
                                              node_kind::comment};
        CHECK(kinds == expected);
        CHECK(r.parent(comment) == r.document_node());
    }
    // Replacing the document element swaps it in place: the doctype is still
    // first, the comment still last, and the new element has no parent.
    const node_id fresh = doc.create_element(atoms.intern("root"));
    doc.set_document_element(fresh);
    {
        const auto r = doc.read();
        CHECK(r.root() == fresh);
        CHECK(!r.parent(fresh));
        const std::span<const node_id> kids = r.children(r.document_node());
        CHECK_EQ(kids.size(), 3u);
        CHECK(kids.size() == 3 && kids[1] == fresh);
        CHECK(!is_document_child(r, html)); // the old one is out of the list
    }
    // And on an EMPTY document the element goes where it is asked to.
    atom_table atoms2;
    document empty{atoms2};
    CHECK(empty.root() == empty.document_node());
    const node_id c = empty.create_comment("c");
    CHECK(empty.append_child(empty.document_node(), c).has_value());
    const node_id el = empty.create_element(atoms2.intern("x"));
    empty.set_document_element(el, c);
    {
        const auto r = empty.read();
        CHECK(r.root() == el);
        const std::span<const node_id> kids = r.children(r.document_node());
        CHECK(kids.size() == 2 && kids[0] == el && kids[1] == c);
    }
}

// DOM 4.13: a PI's attribute map is parsed from its data by the xml-stylesheet
// pseudo-attribute rules, and an attribute write is serialised back into the
// data - see document::update_pi_attributes and update_pi_data.
void test_a_processing_instruction_keeps_its_attributes_in_its_data() {
    atom_table atoms;
    document doc{atoms};
    const auto names = [&](node_id id) {
        std::string out;
        for (const attribute & a : doc.read().attributes(id)) {
            out += atoms.text(a.name);
            out += '=';
            out += a.value;
            out += ';';
        }
        return out;
    };
    const node_id pi = doc.create_processing_instruction(
        atoms.intern("t"), "a=\"b\"\tx='y &amp; &lt;&#65;&#x42;&apos;'");
    CHECK_EQ(names(pi), std::string{"a=b;x=y & <AB';"});
    // The parse is all or nothing: an unquoted value, a duplicate, a bare
    // `&`, a `<`, no space between two, a name that is no Name, no close.
    for (const char * bad :
         {"a=b", "a=\"1\" a=\"2\"", "a=\"&\"", "a=\"<\"", "a=\"1\"b=\"2\"", "$=\"1\"", "a=\"1"}) {
        CHECK(doc.set_text(pi, bad).has_value());
        CHECK_EQ(names(pi), std::string{});
    }
    CHECK(doc.set_text(pi, " one=\"1\"  two='2' ").has_value());
    CHECK_EQ(names(pi), std::string{"one=1;two=2;"});
    // A write goes back into the data, escaped, and a name the parser would
    // refuse SURVIVES - the map is stored, not re-derived.
    CHECK(doc.set_attribute(pi, atoms.intern("two"), "a<b>&\"c\"").has_value());
    CHECK(doc.set_attribute(pi, atoms.intern("$"), "").has_value());
    CHECK_EQ(std::string{doc.read().text(pi)},
             std::string{"one=\"1\" two=\"a&lt;b&gt;&amp;&quot;c&quot;\" $=\"\""});
    CHECK_EQ(names(pi), std::string{"one=1;two=a<b>&\"c\";$=;"});
    CHECK(doc.remove_attribute(pi, atoms.intern("one")).has_value());
    CHECK_EQ(std::string{doc.read().text(pi)},
             std::string{"two=\"a&lt;b&gt;&amp;&quot;c&quot;\" $=\"\""});
    // A comment is still not an element to set_attribute.
    const node_id comment = doc.create_comment("c");
    CHECK(!doc.set_attribute(comment, atoms.intern("a"), "b").has_value());
}

// HTML 13.2.5.72-76: `<?target data?>` is a ProcessingInstruction node now, and
// `<?xml ...?>` - any target that is `xml` or `xml-stylesheet`, or no name at
// all - is the bogus comment it always was.
void test_the_html_parser_makes_processing_instructions() {
    atom_table atoms;
    document doc{atoms};
    (void)parse_html(doc, "<!DOCTYPE html><?xml version=\"1.0\"?><?XML-Stylesheet x?><?t a=\"b\"?>"
                          "<?_u ?"
                          "?><?v x?y><?9 no?><body><p><?w  data ?></p>");
    const auto r = doc.read();
    const std::span<const node_id> top = r.children(r.document_node());
    const std::vector<node_kind> expected{node_kind::document_type,
                                          node_kind::comment,
                                          node_kind::comment,
                                          node_kind::processing_instruction,
                                          node_kind::processing_instruction,
                                          node_kind::processing_instruction,
                                          node_kind::comment,
                                          node_kind::element};
    CHECK(kinds_of(r, r.document_node()) == expected);
    if (kinds_of(r, r.document_node()) != expected) { return; }
    CHECK_EQ(std::string{r.text(top[1])}, std::string{"?xml version=\"1.0\"?"});
    CHECK(r.name(top[3]) == atoms.intern("t"));
    CHECK_EQ(std::string{r.text(top[3])}, std::string{"a=\"b\""});
    CHECK_EQ(std::string{r.attribute_value(top[3], atoms.intern("a"))}, std::string{"b"});
    CHECK(r.name(top[4]) == atoms.intern("_u"));
    CHECK_EQ(std::string{r.text(top[4])}, std::string{"?"}); // `??>`: one `?` is data
    CHECK_EQ(std::string{r.text(top[5])}, std::string{"x?y"});
    CHECK_EQ(std::string{r.text(top[6])}, std::string{"?9 no?"});
    // Inside the body, where a comment would go, with the target's trailing
    // white space skipped and the data's kept.
    const node_id p = r.children(r.children(r.root()).back())[0];
    const std::span<const node_id> in_p = r.children(p);
    CHECK_EQ(in_p.size(), 1u);
    CHECK(!in_p.empty() && r.kind(in_p[0]).value() == node_kind::processing_instruction);
    CHECK(!in_p.empty() && r.name(in_p[0]) == atoms.intern("w"));
    CHECK_EQ(std::string{r.text(in_p[0])}, std::string{"data "});
}

} // namespace

int main() {
    test_created_nodes_carry_what_the_dom_says();
    test_the_html_parser_puts_the_doctype_ahead_of_html();
    test_a_document_without_a_doctype_has_one_child();
    test_the_xml_parser_produces_all_three_kinds();
    test_document_children_move_like_any_node();
    test_a_processing_instruction_keeps_its_attributes_in_its_data();
    test_the_html_parser_makes_processing_instructions();
    REPORT("dom_special_nodes");
}
