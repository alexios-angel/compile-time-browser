// The smaller value grammars: `display`'s two-value form, the box alignment
// longhands and `place-*` shorthands, and the filter function list. Every
// expectation is one of css/css-display, css/css-align or
// css/filter-effects's parsing files.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <string>
#include <string_view>

using ctbrowser::style::css::check_declaration;
using ctbrowser::style::css::color_context;
using ctbrowser::style::css::computed_filter;
using ctbrowser::style::css::declaration_block;
using ctbrowser::style::css::declaration_value;
using ctbrowser::style::css::length_context;
using ctbrowser::style::css::set_declaration;

namespace {

void ok(std::string_view property, std::string_view value, std::string_view serialized) {
    const auto answer = check_declaration(property, value);
    CHECK(answer.valid);
    CHECK_EQ(answer.serialized, std::string{serialized});
}

void bad(std::string_view property, std::string_view value) {
    CHECK(!check_declaration(property, value).valid);
}

void test_display() {
    ok("display", "flow", "block");
    ok("display", "inline flow-root", "inline-block");
    ok("display", "flow-root list-item", "flow-root list-item");
    ok("display", "list-item block flow", "list-item");
    ok("display", "inline list-item flow-root", "inline flow-root list-item");
    ok("display", "block ruby", "block ruby");
    ok("display", "ruby inline", "ruby");
    ok("display", "run-in flex", "run-in flex");
    ok("display", "flow run-in", "run-in");
    ok("display", "table-row", "table-row");
    ok("display", "BLOCK", "block");
    bad("display", "grid inline-grid");
    bad("display", "none grid");
    bad("display", "flex list-item");
    bad("display", "block block");
    bad("display", "contents list-item");
}

void test_box_alignment() {
    ok("align-content", "first baseline", "baseline");
    ok("align-content", "last baseline", "last baseline");
    ok("align-content", "safe flex-start", "safe flex-start");
    ok("align-content", "space-evenly", "space-evenly");
    ok("justify-content", "unsafe right", "unsafe right");
    ok("align-self", "anchor-center", "anchor-center");
    ok("justify-items", "center legacy", "legacy center");
    ok("justify-items", "legacy", "legacy");
    ok("justify-items", "safe left", "safe left");
    ok("justify-self", "auto", "auto");
    bad("align-content", "auto");
    bad("align-content", "left");
    bad("align-content", "self-start");
    bad("align-content", "baseline last");
    bad("align-content", "start safe");
    bad("align-self", "space-around");
    bad("justify-content", "baseline");
    bad("justify-items", "anchor-center");
    bad("justify-items", "auto");
    bad("justify-self", "legacy");
    // The place-* shorthands split into their two longhands and fold back.
    declaration_block block;
    CHECK(set_declaration(block, "place-content", "first baseline", false));
    CHECK_EQ(declaration_value(block, "align-content"), std::string{"baseline"});
    CHECK_EQ(declaration_value(block, "justify-content"), std::string{"start"});
    CHECK_EQ(declaration_value(block, "place-content"), std::string{"baseline start"});
    CHECK(set_declaration(block, "place-content", "space-evenly unsafe end", false));
    CHECK_EQ(declaration_value(block, "place-content"), std::string{"space-evenly unsafe end"});
    CHECK(set_declaration(block, "place-content", "normal normal", false));
    CHECK_EQ(declaration_value(block, "place-content"), std::string{"normal"});
    CHECK(!set_declaration(block, "place-content", "first baseline first baseline", false));
    CHECK(!set_declaration(block, "place-content", "left", false));
    CHECK(set_declaration(block, "place-items", "first baseline right legacy", false));
    CHECK_EQ(declaration_value(block, "place-items"), std::string{"baseline legacy right"});
    CHECK(set_declaration(block, "place-items", "stretch stretch", false));
    CHECK_EQ(declaration_value(block, "place-items"), std::string{"stretch"});
    CHECK(!set_declaration(block, "place-items", "legacy", false));
    CHECK(set_declaration(block, "place-self", "auto center", false));
    CHECK_EQ(declaration_value(block, "align-self"), std::string{"auto"});
    CHECK_EQ(declaration_value(block, "justify-self"), std::string{"center"});
}

void test_the_small_shorthands() {
    declaration_block block;
    CHECK(set_declaration(block, "scroll-padding", "1px 2px", false));
    CHECK_EQ(declaration_value(block, "scroll-padding-left"), std::string{"2px"});
    CHECK_EQ(declaration_value(block, "scroll-padding"), std::string{"1px 2px"});
    CHECK(set_declaration(block, "scroll-margin-block", "3px", false));
    CHECK_EQ(declaration_value(block, "scroll-margin-block-end"), std::string{"3px"});
    CHECK(set_declaration(block, "overscroll-behavior", "contain none", false));
    CHECK_EQ(declaration_value(block, "overscroll-behavior-y"), std::string{"none"});
    CHECK(set_declaration(block, "columns", "10em 2", false));
    CHECK_EQ(declaration_value(block, "column-width"), std::string{"10em"});
    CHECK_EQ(declaration_value(block, "column-count"), std::string{"2"});
    CHECK(set_declaration(block, "column-rule", "thin solid red", false));
    CHECK_EQ(declaration_value(block, "column-rule-color"), std::string{"red"});
    CHECK(set_declaration(block, "text-wrap", "nowrap balance", false));
    CHECK_EQ(declaration_value(block, "text-wrap-style"), std::string{"balance"});
    CHECK_EQ(declaration_value(block, "text-wrap"), std::string{"nowrap balance"});
}

void test_filters() {
    ok("filter", "blur()", "blur()");
    ok("filter", "blur(0)", "blur(0px)");
    ok("filter", "grayscale(300%)", "grayscale(100%)");
    ok("filter", "invert(2)", "invert(1)");
    ok("filter", "brightness(300%)", "brightness(300%)");
    ok("filter", "hue-rotate(0)", "hue-rotate(0deg)");
    ok("filter", "drop-shadow(1px 2px rgb(4, 5, 6))", "drop-shadow(rgb(4, 5, 6) 1px 2px)");
    ok("filter", "drop-shadow(0 0 0)", "drop-shadow(0px 0px 0px)");
    ok("backdrop-filter", "none", "none");
    ok("filter", "url(#f) blur(2px)", "url(\"#f\") blur(2px)");
    bad("filter", "auto");
    bad("filter", "blur(-100px)");
    bad("filter", "blur(10)");
    bad("filter", "brightness(-20)");
    bad("filter", "brightness(30px)");
    bad("filter", "drop-shadow()");
    bad("filter", "drop-shadow(1px)");
    bad("filter", "drop-shadow(1px 2px 3px 4px)");
    bad("filter", "drop-shadow(10% 20%)");
    bad("filter", "hue-rotate(90)");
    bad("filter", "none hue-rotate(0deg)");
    length_context lengths;
    color_context ctx;
    ctx.lengths = &lengths;
    ctx.current_color = "rgb(0, 255, 0)";
    CHECK_EQ(computed_filter("blur()", ctx), std::string{"blur(0px)"});
    CHECK_EQ(computed_filter("brightness(300%)", ctx), std::string{"brightness(3)"});
    CHECK_EQ(computed_filter("grayscale(calc(3))", ctx), std::string{"grayscale(1)"});
    CHECK_EQ(computed_filter("brightness(calc(-10%))", ctx), std::string{"brightness(0)"});
    CHECK_EQ(computed_filter("hue-rotate()", ctx), std::string{"hue-rotate(0deg)"});
    CHECK_EQ(computed_filter("drop-shadow(1px 2px)", ctx),
             std::string{"drop-shadow(rgb(0, 255, 0) 1px 2px 0px)"});
    CHECK_EQ(computed_filter("drop-shadow(rgb(4, 5, 6) calc(1px) 2px 0px)", ctx),
             std::string{"drop-shadow(rgb(4, 5, 6) 1px 2px 0px)"});
}

} // namespace

int main() {
    test_display();
    test_box_alignment();
    test_the_small_shorthands();
    test_filters();
    REPORT("css_grammar_values");
}
