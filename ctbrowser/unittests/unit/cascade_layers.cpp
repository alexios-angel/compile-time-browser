// The cascade's newer sort criteria and rollbacks: `@layer` (CSS Cascade 5
// §6.4) with `revert-layer`, `@scope` (CSS Cascade 6 §3) with proximity, CSS
// Nesting's `&` and implicit nesting, `@supports` decided at parse time, and
// the `revert-rule` draft keyword. Each case is one the WPT files in
// css/css-cascade and css/css-nesting assert through getComputedStyle.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include "style_fixture.hpp"

#include <string>
#include <string_view>

using namespace ctbrowser;
using namespace ctbrowser::style;

namespace {

// layer-basic.html's cases: the rule that says `green` must win.
void test_layer_order() {
    const std::string_view cases[] = {
        "@layer { } t { color: green }",
        "t { color: green } @layer { t { color: red } }",
        "@layer { t { color: red } } t { color: green }",
        "@layer { t { color: red } } @layer { t { color: green } }",
        "@layer { t { color: green } @layer { t { color: red } } }",
        "@layer { @layer { t { color: red } } t { color: green } }",
        "@layer A { t { color: red } } @layer B { t { color: green } } @layer A { t { color: red } "
        "}",
        "@layer A { t { color: green } @layer A { t { color: red } } }",
        "@layer A.B { t { color: red } } @layer A { t { color: green } }",
        "@layer B, A; @layer A { t { color: green } } @layer B { t { color: red } }",
        "@layer A, B; @layer B { t { color: red !important } } @layer A { t { color: green "
        "!important } } t { color: red !important }",
        "@layer { t { color: green !important } } t { color: red }",
        "t { color: red } @layer { t { color: green !important } }",
        "@layer { t { color: green !important } } @layer { t.first { color: red !important } } t { "
        "color: red !important }",
    };
    for (const std::string_view css : cases) {
        fixture f;
        f.load("<t class=first>x</t>", css);
        expect_value(f, f.find("t"), "color", "green", css);
    }
}

// Two sheets share a layer by name; an anonymous layer is never shared.
void test_layers_across_sheets() {
    fixture f;
    (void)parse_html(f.doc, "<t>x</t>");
    f.styles.add_sheet("@layer a { t { color: red } } @layer b { t { color: green } }", 1);
    f.styles.add_sheet("@layer a { t { color: red } }", 1);
    f.resolved = f.styles.resolve_all(f.doc.read());
    expect_value(f, f.find("t"), "color", "green", "a second sheet's @layer a is the first's");
}

void test_revert_layer() {
    fixture f;
    f.load("<t>x</t>",
           "@layer a { t { color: green } } @layer b { t { color: red; color: revert-layer } }");
    expect_value(f, f.find("t"), "color", "green", "revert-layer rolls back one layer");

    fixture g;
    g.load("<t>x</t>", "t { color: revert-layer } @layer a { t { color: green } }");
    expect_value(g, g.find("t"), "color", "green", "unlayered revert-layer rolls back to layers");

    fixture h;
    h.load("<t>x</t>", "@layer a { t { color: revert-layer } }", "t { color: ua }");
    expect_value(h, h.find("t"), "color", "ua", "with no layer below, the previous origin");

    fixture i;
    i.load("<t style='color: revert-layer'>x</t>", "@layer a { t { color: green } }");
    expect_value(i, i.find("t"), "color", "green", "the style attribute is a layer above all");
}

void test_revert_rule() {
    fixture f;
    f.load("<t>x</t>", "t { color: green } t { color: red; color: revert-rule }");
    expect_value(f, f.find("t"), "color", "green", "revert-rule rolls back one rule");

    fixture g;
    g.load("<t>x</t>",
           "t { z-index: 1 } t { z-index: 2 } t { z-index: -1; z-index: revert-rule } t { z-index: "
           "-1; z-index: revert-rule }");
    expect_value(g, g.find("t"), "z-index", "2", "a chain of revert-rule");

    fixture h;
    h.load("<t id=a>x</t>",
           "#a { color: red } #a#a#a { color: red; color: revert-rule } #a#a { color: green }");
    expect_value(h, h.find("t"), "color", "green", "cascade order, not source order");
}

void test_supports_is_decided() {
    fixture f;
    f.load("<t>x</t>",
           "@supports (color: red) { t { color: green } } @supports (colour: red) { t { color: red "
           "} } @supports not (display: nonsense) { t { z-index: 1 } }");
    expect_value(f, f.find("t"), "color", "green", "a true @supports applies");
    expect_value(f, f.find("t"), "z-index", "1", "`not` of an unsupported value is true");
}

void test_nesting() {
    fixture f;
    f.load("<div class=a><span class=b>x</span><i>y</i></div><span class=b>z</span>",
           ".a { color: red; .b { color: green } > i { color: blue } &.a { z-index: 1 } }");
    const auto txn = f.doc.read();
    const node_id a = f.find("div");
    const auto kids = txn.children(a);
    expect_value(f, a, "color", "red", "the parent's own declaration");
    expect_value(f, a, "z-index", "1", "&.a is .a.a");
    expect_value(f, kids[0], "color", "green", "a nested .b is `.a .b`");
    expect_value(f, kids[1], "color", "blue", "a relative `> i` is `.a > i`");
    // The outer .b is not under .a.
    node_id outer{};
    for (const node_id c : txn.children(txn.parent(a))) {
        if (c != a && txn.kind(c).value_or(node_kind::text) == node_kind::element) { outer = c; }
    }
    CHECK(static_cast<bool>(outer));
    expect_value(f, outer, "color", "", "implicit nesting does not leak");

    // Declarations after a nested rule keep their source order (CSS Nesting 1
    // §4): the trailing `color: blue` is written after `& { color: red }`.
    fixture g;
    g.load("<div class=a>x</div>", ".a { color: green; & { color: red } color: blue }");
    expect_value(g, g.find("div"), "color", "blue", "trailing declarations come last");

    // `&` inside a nested @media, and bare declarations in one.
    fixture h;
    h.load("<div class=a>x</div>",
           ".a { @media (min-width: 1px) { color: green; & { z-index: 2 } } }");
    expect_value(h, h.find("div"), "color", "green", "bare declarations in a nested @media");
    expect_value(h, h.find("div"), "z-index", "2", "& inside a nested @media");
}

void test_scope() {
    fixture f;
    f.load("<div class=a><span>1</span><div class=c><span>2</span></div></div><span>3</span>",
           "@scope (.a) to (.c) { span { color: green } .a { z-index: 1 } :scope { z-index: 2 } }");
    const auto txn = f.doc.read();
    const node_id a = f.find("div");
    const node_id in = txn.children(a)[0];
    const node_id c = txn.children(a)[1];
    const node_id limited = txn.children(c)[0];
    expect_value(f, in, "color", "green", "in scope");
    expect_value(f, limited, "color", "", "under the limit");
    expect_value(f, a, "z-index", "2", ":scope styles the root; .a does not");

    // Proximity beats source order, specificity beats proximity.
    fixture g;
    g.load("<div class=a><div class=b><span id=i>x</span></div></div>",
           "@scope (.b) { [id] { color: green } } @scope (.a) { [id] { color: red } } "
           "@scope (.a) { span[id] { z-index: 1 } } @scope (.b) { [id] { z-index: 2 } }");
    const node_id i = g.find("span");
    expect_value(g, i, "color", "green", "the nearer root wins");
    expect_value(g, i, "z-index", "1", "specificity first");

    // Nested scopes and `&` as :where(:scope).
    fixture h;
    h.load("<div class=a><div class=b><span>1</span></div><span>2</span></div>",
           "@scope (.a) { @scope (.b) { span { color: green } } & { z-index: 3 } }");
    const auto t = h.doc.read();
    const node_id ha = h.find("div");
    expect_value(h, t.children(t.children(ha)[0])[0], "color", "green", "nested scope");
    expect_value(h, t.children(ha)[1], "color", "", "outside the inner scope");
    expect_value(h, ha, "z-index", "3", "& in @scope is the root");
}

// `@container`: a style query needs no layout; a size query asks the hook
// for the nearest `container-type: size` ancestor's box, and is unknown -
// so its rules do not apply - until the browser installs one.
void test_container_queries() {
    fixture f;
    (void)parse_html(f.doc, "<div id=c><div id=n><span>x</span></div></div>");
    f.styles.add_sheet("#c { container-type: size; container-name: card; --theme: dark } "
                       "@container style(--theme: dark) { span { color: styled } } "
                       "@container (width > 300px) { span { z-index: 1 } } "
                       "@container card (width < 300px) { span { z-index: 2 } } "
                       "@container other (width > 300px) { span { z-index: 3 } }",
                       1);
    f.resolved = f.styles.resolve_all(f.doc.read());
    const node_id span = f.find("span");
    expect_value(f, span, "color", "styled", "a style() query against the ancestor");
    CHECK(f.value_of(span, "z-index").empty()); // no hook: a size query is unknown
    f.styles.set_container_size([](node_id) {
        return std::optional<engine::container_size>{engine::container_size{400, 100}};
    });
    f.resolved = f.styles.resolve_all(f.doc.read());
    expect_value(f, span, "z-index", "1", "the sized container's box answers the query");
}

} // namespace

int main() {
    test_layer_order();
    test_layers_across_sheets();
    test_revert_layer();
    test_revert_rule();
    test_supports_is_decided();
    test_nesting();
    test_scope();
    test_container_queries();
    REPORT("cascade_layers");
}
