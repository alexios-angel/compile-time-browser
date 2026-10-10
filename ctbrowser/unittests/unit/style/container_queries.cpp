//===- container_queries.cpp - Container query tests ------------*- C++ -*-===//
//
// Container queries (CSS Conditional 5): the `@container` prelude read and
// written back, the container a query picks by the axes it reads, the
// scroll-state() features, the container's own font in the query, and the
// `cq*` units. Also font-variant shorthand and line-clamp serialisation.
// Answering the size hook from a real layout is left to WPT; here the hook
// is a table. Comments naming a WPT file give the test the cases come from.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include "fixture.hpp"

#include <optional>
#include <string>
#include <string_view>

using namespace ctbrowser;
using namespace ctbrowser::style;

namespace {

/// Parses an `@container` prelude and serialises it back, or returns
/// "<invalid>" if it does not parse.
[[nodiscard]] std::string conditions_of(std::string_view prelude) {
    const auto parsed = css::parse_container_conditions(prelude);
    return parsed ? css::serialize_container_conditions(*parsed) : std::string{"<invalid>"};
}

// at-container-parsing and at-container-serialization.
void test_prelude() {
    CHECK_EQ(conditions_of("(width)"), std::string{"(width)"});
    CHECK_EQ(conditions_of("(  wiDTh  )"), std::string{"(width)"});
    CHECK_EQ(conditions_of("(   MAX-WIDTH:100px  )"), std::string{"(max-width: 100px)"});
    CHECK_EQ(conditions_of("(100px>WIDTH>10px)"), std::string{"(100px > width > 10px)"});
    CHECK_EQ(conditions_of("(width),(height) ,--foo ,--bar"),
             std::string{"(width), (height), --foo, --bar"});
    CHECK_EQ(conditions_of("NAMe   (inline-sizE  < 1300px )"),
             std::string{"NAMe (inline-size < 1300px)"});
    CHECK_EQ(conditions_of("STYLE(--foo: bar)"), std::string{"style(--foo: bar)"});
    CHECK_EQ(conditions_of("style(        --foo:bar   baz )"),
             std::string{"style(--foo: bar   baz)"});
    CHECK_EQ(conditions_of("style(( (  (( (--foo )) )  )))"),
             std::string{"style((((((--foo))))))"});
    CHECK_EQ(conditions_of("style(  ( --FOO: BAR) OR ( prop: val  ) )"),
             std::string{"style((--FOO: BAR) or ( prop: val  ))"});
    CHECK_EQ(conditions_of("style(100px  > --\\{foo >10px)"),
             std::string{"style(100px > --\\{foo > 10px)"});
    CHECK_EQ(conditions_of("scroll-state(        scrollable:left)"),
             std::string{"scroll-state(scrollable: left)"});
    CHECK_EQ(conditions_of("scroll-STate(scrollable:    )"),
             std::string{"scroll-STate(scrollable:    )"});
    CHECK_EQ(conditions_of("scroll-state (scrollable: right)"),
             std::string{"scroll-state (scrollable: right)"});
    for (const std::string_view bad :
         {"", "(width),", ",(width)", "(width),,(height)", "name screen", "name foo (width: 100px)",
          "none (width)", "inherit", "1px not (width)", "foo foo not (width)"}) {
        CHECK_EQ(conditions_of(bad), std::string{"<invalid>"});
    }
}

// container-selection: a query reads from the nearest container that can
// answer every axis it asks about; an axis the container lacks is unknown.
void test_selection() {
    fixture f;
    (void)parse_html(f.doc, "<div id=size><div id=inline><span>x</span></div></div>");
    f.styles.add_sheet("#size { container-type: size } #inline { container-type: inline-size } "
                       "@container (width: 100px) { span { z-index: 1 } } "
                       "@container (height: 50px) { span { order: 2 } } "
                       "@container not (height: 10px) { span { opacity: 0.5 } } "
                       "@container (hover) or (width: 100px) { span { color: green } }",
                       1);
    const node_id size = f.find_id("size");
    f.styles.set_container_size([&](node_id n) {
        return n == size ? engine::container_size{200, 50} : engine::container_size{100, 10};
    });
    f.resolved = f.styles.resolve_all(f.doc.read());
    const node_id span = f.find("span");
    expect_value(f, span, "z-index", "1", "width from the inline-size container");
    expect_value(f, span, "order", "2", "height skips it for the size container");
    expect_value(f, span, "opacity", "0.5", "the size container's height, not the inline one's");
    CHECK(f.value_of(span, "color").empty()); // (hover) is a feature no container has.
    CHECK(!f.styles.container_sizes_changed());
    f.styles.set_container_size([](node_id) { return engine::container_size{1, 1}; });
    CHECK(f.styles.container_sizes_changed());
}

// scroll-state(): `scrollable` against the box's bits, the rest `none`.
void test_scroll_state() {
    fixture f;
    (void)parse_html(f.doc, "<div id=s><span>x</span></div>");
    f.styles.add_sheet("#s { container-type: scroll-state } "
                       "@container scroll-state(scrollable: bottom) { span { z-index: 1 } } "
                       "@container scroll-state(scrollable: top) { span { order: 1 } } "
                       "@container scroll-state((stuck: none) and (snapped: none)) "
                       "{ span { opacity: 0.5 } } "
                       "@container scroll-state(scrollable: auto) or (not "
                       "scroll-state(scrollable: auto)) { span { color: red } }",
                       1);
    f.styles.set_container_size([](node_id) {
        engine::container_size s{100, 100};
        s.scrollable = css::scroll_bottom;
        return s;
    });
    f.resolved = f.styles.resolve_all(f.doc.read());
    const node_id span = f.find("span");
    expect_value(f, span, "z-index", "1", "scrollable: bottom");
    CHECK(f.value_of(span, "order").empty());
    expect_value(f, span, "opacity", "0.5", "neither stuck nor snapped");
    CHECK(f.value_of(span, "color").empty()); // Unknown stays unknown under `not`.
}

// font-relative-units and container-units-basic: an `em` in a query is the
// container's; `cqw` is the nearest size container's width.
void test_units() {
    fixture f;
    (void)parse_html(f.doc, "<div id=c><p><span>x</span></p></div>");
    f.styles.add_sheet("#c { container-type: inline-size; font-size: 20px } "
                       "@container (width: 10em) { span { z-index: 1 } } "
                       "span { padding-left: 10cqw; padding-top: 10cqh }",
                       1);
    f.styles.set_container_size([](node_id) { return engine::container_size{200, 40}; });
    f.resolved = f.styles.resolve_all(f.doc.read());
    const node_id span = f.find("span");
    expect_value(f, span, "z-index", "1", "10em of the container's 20px font");
    expect_value(f, span, "padding-left", "20px", "10cqw of 200px");
    expect_value(f, span, "padding-top", "76.8px", "10cqh: no block container, the viewport");
}

// size-query-with-var: a var() in a size query is the container's.
void test_var() {
    fixture f;
    (void)parse_html(f.doc, "<div id=c><span>x</span></div>");
    f.styles.add_sheet("#c { container-type: inline-size; --w: 150px; --n: 200 } "
                       "@container (width > var(--w)) { span { z-index: 1 } } "
                       "@container (width > var(--missing)) { span { order: 1 } } "
                       "@container (width > var(--missing, 100px)) { span { opacity: 0.5 } } "
                       "@container (width > var(--n)) { span { color: red } }",
                       1);
    f.styles.set_container_size([](node_id) { return engine::container_size{200, 40}; });
    f.resolved = f.styles.resolve_all(f.doc.read());
    const node_id span = f.find("span");
    expect_value(f, span, "z-index", "1", "var() from the container");
    CHECK(f.value_of(span, "order").empty());
    expect_value(f, span, "opacity", "0.5", "the fallback");
    CHECK(f.value_of(span, "color").empty()); // A length compared with a number.
}

// font-variant-shorthand-serialization and font-shorthand-variant.
void test_font_variant() {
    using css::declaration_block;
    using css::declaration_value;
    using css::set_declaration;
    declaration_block block;
    CHECK(set_declaration(block, "font-variant",
                          "super proportional-width jis83 tabular-nums "
                          "historical-forms all-small-caps no-contextual",
                          false));
    CHECK_EQ(declaration_value(block, "font-variant"),
             std::string{"no-contextual all-small-caps historical-forms tabular-nums jis83 "
                         "proportional-width super"});
    CHECK_EQ(declaration_value(block, "font-variant-position"), std::string{"super"});
    CHECK(!set_declaration(block, "font-variant", "sub super", false));
    CHECK(!set_declaration(block, "font-variant", "normal small-caps", false));
    block.clear();
    CHECK(set_declaration(block, "font", "medium serif", false));
    CHECK(set_declaration(block, "font-variant", "small-caps", false));
    CHECK_EQ(declaration_value(block, "font"), std::string{"small-caps medium serif"});
    CHECK(set_declaration(block, "font-variant", "titling-caps", false));
    CHECK_EQ(declaration_value(block, "font"), std::string{});
    CHECK(set_declaration(block, "font-variant", "none", false));
    CHECK_EQ(declaration_value(block, "font-variant-ligatures"), std::string{"none"});
    CHECK_EQ(declaration_value(block, "font-variant"), std::string{"none"});
}

// line-clamp-valid/-invalid, webkit-line-clamp-*, shorthand-values.
void test_line_clamp() {
    using css::declaration_block;
    using css::declaration_value;
    using css::parse_declaration_block;
    using css::serialize_declaration_block;
    using css::set_declaration;
    const auto read = [](std::string_view property, std::string_view value) {
        declaration_block block;
        (void)set_declaration(block, property, value, false);
        return declaration_value(block, property);
    };
    CHECK_EQ(read("line-clamp", "none"), std::string{"none"});
    CHECK_EQ(read("line-clamp", "ellipsis"), std::string{"auto"});
    CHECK_EQ(read("line-clamp", "no-ellipsis 10"), std::string{"10 no-ellipsis"});
    CHECK_EQ(read("line-clamp", "auto 11"), std::string{"11 auto"});
    CHECK_EQ(read("line-clamp", "no-ellipsis -webkit-legacy"),
             std::string{"auto no-ellipsis -webkit-legacy"});
    for (const std::string_view bad :
         {"0", "none 2", "3 none", "3 ellipsis auto", "-webkit-legacy", "0 -webkit-legacy"}) {
        CHECK_EQ(read("line-clamp", bad), std::string{});
    }
    CHECK_EQ(read("-webkit-line-clamp", "6"), std::string{"6"});
    CHECK_EQ(read("-webkit-line-clamp", "auto"), std::string{});
    declaration_block block;
    parse_declaration_block(block, "-webkit-line-clamp: none;");
    CHECK_EQ(serialize_declaration_block(block), std::string{"line-clamp: none;"});
}

} // namespace

int main() {
    test_prelude();
    test_selection();
    test_scroll_state();
    test_units();
    test_var();
    test_font_variant();
    test_line_clamp();
    REPORT("style_container_queries");
}
