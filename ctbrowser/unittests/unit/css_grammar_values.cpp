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
using ctbrowser::style::css::computed_grid;
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

void test_grid() {
    ok("grid-template-columns", "repeat(1, [] 10px [])", "repeat(1, 10px)");
    ok("grid-template-columns", "[] 150px [] 1fr []", "150px 1fr");
    ok("grid-template-columns", "repeat(auto-fit, [three] minmax(max-content, 6em) [four])",
       "repeat(auto-fit, [three] minmax(max-content, 6em) [four])");
    ok("grid-template-columns", "minmax(calc(0.5em + 10px), 5fr)",
       "minmax(calc(0.5em + 10px), 5fr)");
    ok("grid-auto-rows", "fit-content(1px) minmax(2px, 3px) 4px",
       "fit-content(1px) minmax(2px, 3px) 4px");
    bad("grid-template-columns", "-10px");
    bad("grid-template-columns", "minmax(5fr, 10px)");
    bad("grid-template-columns", "[one]");
    bad("grid-template-columns", "[one] 10px [two] [three]");
    bad("grid-template-columns", "repeat(auto-fill, 10px) repeat(auto-fit, 20%)");
    bad("grid-template-columns", "[auto] 1px");
    bad("grid-template-columns", "auto repeat(auto-fill, auto) auto");
    bad("grid-template-columns", "repeat(20%)");
    bad("grid-auto-columns", "[a] 1px");
    ok("grid-row-start", "span 1 i", "span i");
    ok("grid-row-start", "calc(1.1) -a-", "1 -a-");
    ok("grid-column-end", "\\31st", "\\31 st");
    bad("grid-column-start", "0");
    bad("grid-column-start", "5 5");
    bad("grid-column-start", "first last");
    ok("grid-template-areas", "\"a  b\" \"c d\"", "\"a b\" \"c d\"");
    bad("grid-template-areas", "\"a a\" \"a b\"");
    bad("grid-template-areas", "\"a b\" \"c\"");
    ok("grid-auto-flow", "dense", "row dense");
    bad("grid-auto-flow", "row row");
    length_context lengths;
    color_context ctx;
    ctx.lengths = &lengths;
    CHECK_EQ(computed_grid("grid-template-columns",
                           "[a] 1em repeat(auto-fill, 2em [b] 3em) 4em [d]", ctx),
             std::string{"[a] 16px repeat(auto-fill, 32px [b] 48px) 64px [d]"});
    lengths.font_size = 40;
    CHECK_EQ(computed_grid("grid-auto-columns", "calc(10px - 0.5em)", ctx), std::string{"0px"});
    CHECK_EQ(computed_grid("grid-auto-columns", "calc(10px + 0.5em)", ctx), std::string{"30px"});
    CHECK_EQ(computed_grid("grid-row-start", "span calc(-1)", ctx), std::string{"span 1"});
    declaration_block block;
    CHECK(set_declaration(block, "grid-column", "10", false));
    CHECK_EQ(declaration_value(block, "grid-column-start"), std::string{"10"});
    CHECK_EQ(declaration_value(block, "grid-column-end"), std::string{"auto"});
    CHECK_EQ(declaration_value(block, "grid-column"), std::string{"10"});
    CHECK(set_declaration(block, "grid-column", "first", false));
    CHECK_EQ(declaration_value(block, "grid-column-end"), std::string{"first"});
    CHECK_EQ(declaration_value(block, "grid-column"), std::string{"first"});
    CHECK(set_declaration(block, "grid-area", "auto / i / 2 j", false));
    CHECK_EQ(declaration_value(block, "grid-column-end"), std::string{"i"});
    CHECK_EQ(declaration_value(block, "grid-area"), std::string{"auto / i / 2 j"});
    CHECK(!set_declaration(block, "grid-column", "0 / 5", false));
    CHECK(!set_declaration(block, "grid-area", "auto / auto / auto / auto / auto", false));
}

void test_keyword_combinations() {
    ok("text-decoration-line", "overline underline", "underline overline");
    ok("text-decoration-line", "blink line-through", "line-through blink");
    ok("text-decoration-line", "spelling-error", "spelling-error");
    bad("text-decoration-line", "none underline");
    bad("text-decoration-line", "underline underline");
    bad("text-decoration-line", "spelling-error overline");
    ok("text-transform", "full-width capitalize", "capitalize full-width");
    ok("contain", "size style layout paint", "strict");
    ok("contain", "style layout paint", "content");
    ok("contain", "paint layout", "layout paint");
    bad("contain", "size inline-size");
    ok("font-variant-numeric", "slashed-zero ordinal", "ordinal slashed-zero");
    ok("text-underline-position", "left under", "under left");
    ok("hanging-punctuation", "allow-end first", "allow-end first");
    bad("hanging-punctuation", "none first");
    ok("counter-reset", "foo", "foo 0");
    ok("counter-increment", "foo 2 bar", "foo 2 bar 1");
    ok("counter-reset", "reversed(foo) 3", "reversed(foo) 3");
    ok("counter-set", "foo calc(1.6)", "foo 2");
    bad("counter-reset", "none foo");
    ok("will-change", "TRANSFORM, transform", "TRANSFORM, transform");
    ok("will-change", "auto", "auto");
    bad("will-change", "auto, transform");
    bad("will-change", "none");
    ok("scroll-snap-type", "inline proximity", "inline");
    ok("scroll-snap-type", "x mandatory", "x mandatory");
    bad("scroll-snap-type", "x y");
    ok("scroll-snap-align", "start start", "start");
    ok("scroll-snap-align", "center end", "center end");
    ok("font-size-adjust", "ex-height 0.5", "0.5");
    ok("font-size-adjust", "cap-height calc(0.5 + 1)", "cap-height calc(1.5)");
    ok("font-size-adjust", "from-font", "from-font");
    bad("font-size-adjust", "0.5 ex-height");
    bad("font-size-adjust", "ex-height");
    bad("font-size-adjust", "-10");
}

void test_transforms() {
    ok("rotate", "-0.5 0 0 400grad", "x -400grad");
    ok("rotate", "0 0 -1 400grad", "-400grad");
    ok("rotate", "0 0 0 400grad", "0 0 0 400grad");
    ok("rotate", "400grad 100 200 300", "100 200 300 400grad");
    ok("rotate", "400grad z", "400grad");
    bad("rotate", "1 2 3");
    bad("rotate", "45deg x y");
    bad("rotate", "100px");
    ok("scale", "-100% -100% 1", "-1");
    ok("scale", "100 200 1", "100 200");
    ok("scale", "1%", "0.01");
    bad("scale", "100px");
    bad("scale", "calc(180deg) 2 3");
    ok("translate", "0", "0px");
    ok("translate", "100px 0px 0px", "100px");
    ok("translate", "100px 0%", "100px 0%");
    bad("translate", "100px 200px 300%");
    bad("translate", "100deg");
    ok("transform-origin", "bottom right 7px", "right bottom 7px");
    ok("transform-origin", "bottom", "bottom");
    ok("transform-origin", "-1px bottom 5px", "-1px bottom 5px");
    bad("transform-origin", "top 1px");
    bad("transform-origin", "bottom 10% right 20%");
    bad("transform-origin", "1px 2px 3%");
    length_context lengths;
    lengths.font_size = 40;
    color_context ctx;
    ctx.lengths = &lengths;
    using ctbrowser::style::css::computed_transform_property;
    CHECK_EQ(computed_transform_property("rotate", "-1 0 0 400grad", ctx, 0, 0),
             std::string{"x -360deg"});
    CHECK_EQ(computed_transform_property("scale", "2 calc(300%)", ctx, 0, 0), std::string{"2 3"});
    CHECK_EQ(computed_transform_property("translate", "0em 0em 100px", ctx, 0, 0),
             std::string{"0px 0px 100px"});
    CHECK_EQ(computed_transform_property("transform-origin", "10%", ctx, 200, 300),
             std::string{"20px 150px"});
    CHECK_EQ(computed_transform_property("transform-origin", "-1px bottom 5px", ctx, 200, 300),
             std::string{"-1px 300px 5px"});
    CHECK_EQ(
        computed_transform_property("perspective-origin", "right 30% top -60px", ctx, 200, 300),
        std::string{"140px -60px"});
    CHECK_EQ(computed_transform_property(
                 "transform-origin",
                 "calc(-100% + 10px - 0.5em) calc(10px - 0.5em) calc(10px - 0.5em)", ctx, 200, 300),
             std::string{"-210px -10px -10px"});
}

void test_shadows() {
    ok("box-shadow", "-4px 4px 0 0 green", "green -4px 4px 0px 0px");
    ok("box-shadow", "inset 1px -2px, -3px 4px red", "1px -2px inset, red -3px 4px");
    ok("box-shadow", "green inset 4px -4px 0", "green 4px -4px 0px inset");
    ok("box-shadow", "1px 1px calc(1em - 2px)", "1px 1px calc(1em - 2px)");
    ok("text-shadow", "1px 2px 3px red", "red 1px 2px 3px");
    bad("box-shadow", "-4px 4px red 0");
    bad("box-shadow", "1px 1px -1px");
    bad("box-shadow", "1px 2px 3px 4px 5px");
    bad("box-shadow", "1px calc(2px + 2%)");
    bad("box-shadow", "4px inset -4px");
    bad("box-shadow", "red 1px 2px blue");
    bad("box-shadow", "inset 4px -4px inset");
    bad("text-shadow", "1px 2px inset");
    bad("text-shadow", "1px 2px 3px 4px");
}

void test_background_layers() {
    ok("background-repeat", "repeat no-repeat", "repeat-x");
    ok("background-repeat", "no-repeat repeat", "repeat-y");
    ok("background-repeat", "repeat space, round no-repeat, repeat-x",
       "repeat space, round no-repeat, repeat-x");
    bad("background-repeat", "repeat repeat-x");
    ok("background-size", "1px", "1px auto");
    ok("background-size", "auto auto", "auto");
    ok("background-size", "auto 1px, 2% 3%, contain", "auto 1px, 2% 3%, contain");
    bad("background-size", "-1px");
    bad("background-size", "1px 2px 3px");
    ok("background-position-x", "right 10px", "right 10px");
    ok("background-position-x", "center, left, right", "center, left, right");
    bad("background-position-x", "20% left");
    bad("background-position-x", "bottom");
    ok("background-position", "center right 7%", "right 7% center");
    ok("background-position", "top 15px center", "center top 15px");
    ok("background-position", "bottom 10% right 20%", "right 20% bottom 10%");
    ok("mask-position", "10% 20%, center", "10% 20%, center center");
    bad("mask-position", "bottom 7% left");
    ok("background-attachment", "scroll, fixed, local", "scroll, fixed, local");
    bad("background-attachment", "local, none");
    ok("background-clip", "text border-area", "border-area text");
    bad("background-clip", "margin-box");
    using ctbrowser::style::css::computed_background_list;
    length_context lengths;
    lengths.font_size = 40;
    color_context ctx;
    ctx.lengths = &lengths;
    CHECK_EQ(computed_background_list("background-size", "100%", ctx), std::string{"100% auto"});
    CHECK_EQ(
        computed_background_list("background-size", "calc(10px + 0.5em) calc(10px - 0.5em)", ctx),
        std::string{"30px 0px"});
    CHECK_EQ(computed_background_list("background-position-x",
                                      "calc(10px - 0.5em), left -20%, right 10px", ctx),
             std::string{"-10px, -20%, calc(100% - 10px)"});
    CHECK_EQ(computed_background_list("background-position-y", "bottom -10px", ctx),
             std::string{"calc(100% + 10px)"});
    CHECK_EQ(computed_background_list("mask-position", "bottom 10% right 20%", ctx),
             std::string{"80% 90%"});
    CHECK_EQ(computed_background_list("background-position", "right 1em center", ctx),
             std::string{"calc(100% - 40px) 50%"});
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

// The module longhands that were `freeform` rows until they had a grammar.
// Every line is an assertion of a css/<module>/parsing/*-{valid,invalid} file.
void test_module_longhands() {
    ok("continue", "discard", "discard");
    bad("continue", "auto");
    bad("continue", "normal collapse");
    ok("text-spacing-trim", "trim-both", "trim-both");
    bad("text-spacing-trim", "allow-end");
    ok("scroll-target-group", "auto", "auto");
    bad("scroll-target-group", "default");
    bad("scroll-target-group", "auto, auto");
    ok("image-orientation", "none", "none");
    bad("image-orientation", "0deg flip");
    ok("text-size-adjust", "200%", "200%");
    bad("text-size-adjust", "-100%");
    bad("text-size-adjust", "10px");
    ok("-webkit-line-clamp", "6", "6");
    bad("-webkit-line-clamp", "0");
    bad("column-count", "0");
    ok("column-count", "2", "2");
    ok("ruby-position", "inter-character", "inter-character");
    ok("ruby-position", "alternate over", "alternate over");
    bad("ruby-position", "over under");
    ok("text-autospace", "insert punctuation ideograph-alpha",
       "ideograph-alpha punctuation insert");
    bad("text-autospace", "normal insert");

    ok("page", "xyzabc", "xyzabc");
    bad("page", "not valid");
    bad("page", "default");
    ok("view-transition-class", "foo bar", "foo bar");
    bad("view-transition-class", "foo none");
    ok("view-transition-group", "nearest", "nearest");
    bad("view-transition-group", "foo 12px");
    ok("counter-reset", "chapter 2", "chapter 2");
    bad("counter-reset", "default 0");
    bad("will-change", "revert-rule, transform");

    ok("hyphenate-character", "\"=\"", "\"=\"");
    bad("hyphenate-character", "1400");
    ok("block-ellipsis", "ellipsis", "ellipsis");
    bad("block-ellipsis", "auto");
    ok("font-language-override", "\"ENG \"", "\"ENG\"");
    bad("font-language-override", "\"turkish\"");
    bad("font-language-override", "\"\"");

    ok("color-scheme", "only light dark", "light dark only");
    bad("color-scheme", "light only dark");
    bad("color-scheme", "only");
    ok("scrollbar-gutter", "both-edges stable", "stable both-edges");
    bad("scrollbar-gutter", "force both");
    ok("text-combine-upright", "digits 3", "digits 3");
    bad("text-combine-upright", "none all");

    ok("offset-rotate", "0rad reverse", "reverse 0rad");
    bad("offset-rotate", "auto reverse");
    ok("offset-anchor", "auto", "auto");
    bad("offset-anchor", "left 10% top");
    ok("offset-position", "10px 20%", "10px 20%");
    ok("background-blend-mode", "luminosity", "luminosity");
    bad("background-blend-mode", "normal luminosity");

    ok("animation-range-start", "exit 1%, cover 2%, contain 0%", "exit 1%, cover 2%, contain");
    ok("animation-range-end", "cover 100%", "cover");
    bad("animation-range-start", "50% contain");
    bad("animation-range-end", "none");
    ok("image-resolution", "snap from-image 0dppx", "snap from-image 0dppx");
    bad("image-resolution", "3dpi snap from-image");
    ok("clip", "rect(10px, -20px, auto, auto)", "rect(10px, -20px, auto, auto)");
    bad("clip", "rect(10px 20px, 30px 40px)");
}

} // namespace

int main() {
    test_display();
    test_box_alignment();
    test_the_small_shorthands();
    test_grid();
    test_keyword_combinations();
    test_transforms();
    test_shadows();
    test_background_layers();
    test_filters();
    test_module_longhands();
    REPORT("css_grammar_values");
}
