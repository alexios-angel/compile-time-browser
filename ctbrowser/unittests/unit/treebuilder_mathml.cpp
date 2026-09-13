// MathML IS FOREIGN CONTENT, HTML 13.2.6.5: `<math>` and its descendants are
// in the MathML namespace, keep their case, get `xml:lang`/`xlink:href`
// adjusted into their namespaces and `definitionurl` into `definitionURL`, and
// are not HTML elements to anything that asks. The text integration points -
// mi, mo, mn, ms, mtext - and an `<annotation-xml>` that names HTML hand their
// children back to the HTML rules, and a breakout tag closes the whole thing.
//
// dom/nodes/Attr-prefix.html (the MathML row) and html/dom's
// document.getElementsByName-namespace.html are what a `<math>` parsed as an
// unknown HTML element failed.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/shell/shell.hpp>

#include "check.hpp"
#include <string>
#include <string_view>

using namespace ctbrowser;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

// `tag` with a namespace letter, so the expectation shows which vocabulary
// each element landed in: `m:` MathML, `s:` SVG, none for HTML.
struct dumper {
    atom_table & atoms;
    const read_txn & txn;
    [[nodiscard]] std::string dump(node_id at) const {
        const node_kind kind = txn.kind(at).value_or(node_kind::comment);
        if (kind == node_kind::text) { return "\"" + std::string{txn.text(at)} + "\""; }
        if (kind != node_kind::element) { return {}; }
        std::string out = txn.element_ns(at) == node_ns::mathml ? "m:"
                          : txn.element_ns(at) == node_ns::svg  ? "s:"
                                                                : "";
        out += atoms.text(txn.tag(at).value_or(atom{}));
        std::string inner;
        for (const node_id child : txn.children(at)) {
            const std::string part = dump(child);
            if (part.empty()) { continue; }
            if (!inner.empty()) { inner += ' '; }
            inner += part;
        }
        if (!inner.empty()) { out += "(" + inner + ")"; }
        return out;
    }
};

[[nodiscard]] std::string tree(std::string_view html) {
    atom_table atoms;
    document doc{atoms};
    (void)parse_html(doc, html);
    const auto txn = doc.read();
    return dumper{atoms, txn}.dump(txn.root());
}

void test_math_is_foreign_and_keeps_its_case() {
    CHECK_EQ(tree("<body><math><mrow><mi>a</mi><mo>+</mo></mrow></math>"),
             std::string{"html(head body(m:math(m:mrow(m:mi(\"a\") m:mo(\"+\")))))"});
    // Case survives, unlike an HTML tag's.
    CHECK_EQ(tree("<body><math><mFrac></mFrac></math><DIV></DIV>"),
             std::string{"html(head body(m:math(m:mFrac) div))"});
}

void test_integration_points_hand_children_back_to_html() {
    // An HTML tag inside <mi> is HTML; the same tag directly inside <mrow> is
    // a breakout that closes the math.
    CHECK_EQ(tree("<body><math><mi><b>x</b></mi></math>"),
             std::string{"html(head body(m:math(m:mi(b(\"x\")))))"});
    CHECK_EQ(tree("<body><math><mrow><b>x</b></mrow></math>"),
             std::string{"html(head body(m:math(m:mrow) b(\"x\")))"});
    // annotation-xml with an HTML encoding is an integration point; without
    // one it is not.
    CHECK_EQ(tree("<body><math><annotation-xml encoding=\"text/html\"><p>x</p></annotation-xml>"
                  "</math>"),
             std::string{"html(head body(m:math(m:annotation-xml(p(\"x\")))))"});
    CHECK_EQ(tree("<body><math><annotation-xml><span>x</span></annotation-xml></math>"),
             std::string{"html(head body(m:math(m:annotation-xml(m:span(\"x\")))))"});
    // An <svg> inside MathML is SVG, and a `</math>` seen inside it closes
    // both (the foreign end-tag loop pops through foreign entries).
    CHECK_EQ(tree("<body><math><svg><circle/></math></svg></math>"),
             std::string{"html(head body(m:math(s:svg(s:circle))))"});
}

void test_the_page_sees_a_mathml_element() {
    // The namespace, the interface, the adjusted attributes, and what
    // getElementsByName and innerText make of one.
    browser page{browser_options{300, 200}};
    page.load_html(R"(<!DOCTYPE html><body>
      <p name=math><math name=math xml:lang="en" definitionurl="d"><mi>a</mi></math>
      <script>
        var m = document.getElementsByTagName('math')[0];
        var xl = m.getAttributeNodeNS('http://www.w3.org/XML/1998/namespace', 'lang');
        console.log([m.namespaceURI, m instanceof MathMLElement, m instanceof HTMLElement,
                     m instanceof Element, xl.prefix, xl.localName, xl.name,
                     m.getAttribute('definitionURL'), m.hasAttribute('definitionurl'),
                     document.getElementsByName('math').length,
                     document.getElementsByName('math')[0].tagName,
                     typeof m.innerText, typeof m.focus, typeof m.dataset].join());
      </script></body>)");
    CHECK_EQ(page.bindings().console_output().back(),
             std::string{"http://www.w3.org/1998/Math/MathML,true,false,true,xml,lang,xml:lang,"
                         "d,false,1,P,undefined,function,object"});
}

} // namespace

int main() {
    test_math_is_foreign_and_keeps_its_case();
    test_integration_points_hand_children_back_to_html();
    test_the_page_sees_a_mathml_element();
    REPORT("treebuilder_mathml");
}
