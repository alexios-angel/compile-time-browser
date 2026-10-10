// The shorthands whose grammar lives in the CSSOM declaration block and
// nowhere else - `font`, `white-space`, `animation` - reach the cascade
// through that block and read back from getComputedStyle in their canonical
// form. One case per file of css-fonts, css-text and css-animations moved.

#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

#include "../cssom/probe.hpp"
#include "check.hpp"

#include <string>
#include <string_view>

using ctbrowser::style::css::check_declaration;
using ctbrowser::style::css::declaration_block;
using ctbrowser::style::css::declaration_value;
using ctbrowser::style::css::set_declaration;

namespace {

[[nodiscard]] std::string set_and_read(std::string_view property, std::string_view value) {
    declaration_block block;
    (void)set_declaration(block, property, value, false);
    return declaration_value(block, property);
}

// white-space-shorthand, white-space-valid/invalid: CSS Text 4 §3's keywords
// are spellings of white-space-collapse and text-wrap-mode.
void test_white_space_is_a_shorthand() {
    CHECK_EQ(set_and_read("white-space", "pre"), std::string{"pre"});
    CHECK_EQ(set_and_read("white-space", "preserve nowrap"), std::string{"pre"});
    CHECK_EQ(set_and_read("white-space", "nowrap preserve"), std::string{"pre"});
    CHECK_EQ(set_and_read("white-space", "collapse wrap"), std::string{"normal"});
    CHECK_EQ(set_and_read("white-space", "preserve"), std::string{"pre-wrap"});
    CHECK_EQ(set_and_read("white-space", "preserve-breaks"), std::string{"pre-line"});
    CHECK_EQ(set_and_read("white-space", "break-spaces wrap"), std::string{"break-spaces"});
    CHECK_EQ(set_and_read("white-space", "nowrap preserve-breaks"),
             std::string{"preserve-breaks nowrap"});
    CHECK_EQ(set_and_read("white-space", "balance"), std::string{});
    CHECK_EQ(set_and_read("white-space", "preserve balance"), std::string{});
    CHECK_EQ(set_and_read("white-space", "wrap wrap"), std::string{});
    declaration_block block;
    (void)set_declaration(block, "white-space", "pre-wrap", false);
    CHECK_EQ(declaration_value(block, "white-space-collapse"), std::string{"preserve"});
    CHECK_EQ(declaration_value(block, "text-wrap-mode"), std::string{"wrap"});
    CHECK_EQ(declaration_value(block, "white-space-trim"), std::string{"none"});
    // block-step-valid: `size || insert || align || round`, written in that order.
    CHECK_EQ(set_and_read("block-step", "content-box 100px up"), std::string{"100px content-box"});
    CHECK_EQ(set_and_read("block-step", "none auto up margin-box"), std::string{"none"});
    // mask-valid: a layer's boxes need not stand together, and fold back as one.
    CHECK_EQ(set_and_read("mask", "intersect no-clip space round 1px 2px / contain stroke-box "
                                  "url(\"m.svg\") luminance"),
             std::string{"url(\"m.svg\") 1px 2px / contain space round stroke-box no-clip "
                         "intersect luminance"});
    CHECK_EQ(set_and_read("mask", "border-box no-clip"), std::string{"no-clip"});
    CHECK_EQ(set_and_read("mask", "content-box content-box"), std::string{"content-box"});
    CHECK_EQ(set_and_read("mask", "stroke-box no-clip view-box"), std::string{});
    // text-spacing-valid: the trim keyword at either end, the autospace beside it.
    CHECK_EQ(set_and_read("text-spacing", "no-autospace space-all"), std::string{"none"});
    CHECK_EQ(set_and_read("text-spacing", "normal trim-start"), std::string{"trim-start"});
    CHECK_EQ(set_and_read("text-spacing", "no-autospace trim-start"),
             std::string{"trim-start no-autospace"});
    CHECK_EQ(set_and_read("text-spacing", "normal normal no-autospace"), std::string{});
    // position-try-parsing, position-try-fallbacks-parsing.
    CHECK_EQ(set_and_read("position-try", "most-width flip-y flip-x, --foo, top left"),
             std::string{"most-width flip-y flip-x, --foo, left top"});
    CHECK_EQ(set_and_read("position-try", "normal flip-inline --foo"),
             std::string{"--foo flip-inline"});
    CHECK_EQ(set_and_read("position-try", "none normal"), std::string{});
    CHECK_EQ(set_and_read("position-try-fallbacks", "start start, --foo left"), std::string{});
    CHECK_EQ(set_and_read("position-try-fallbacks", "start start"), std::string{"start"});
}

// animation-shorthand, animation-valid, animation-name-valid/invalid.
void test_animation_is_a_shorthand() {
    declaration_block block;
    (void)set_declaration(block, "animation",
                          "anim paused both reverse 4 1s -3s cubic-bezier(0, -2, 1, 3)", false);
    CHECK_EQ(declaration_value(block, "animation-duration"), std::string{"1s"});
    CHECK_EQ(declaration_value(block, "animation-timing-function"),
             std::string{"cubic-bezier(0, -2, 1, 3)"});
    CHECK_EQ(declaration_value(block, "animation-delay"), std::string{"-3s"});
    CHECK_EQ(declaration_value(block, "animation-iteration-count"), std::string{"4"});
    CHECK_EQ(declaration_value(block, "animation-direction"), std::string{"reverse"});
    CHECK_EQ(declaration_value(block, "animation-fill-mode"), std::string{"both"});
    CHECK_EQ(declaration_value(block, "animation-play-state"), std::string{"paused"});
    CHECK_EQ(declaration_value(block, "animation-name"), std::string{"anim"});
    CHECK_EQ(declaration_value(block, "animation-timeline"), std::string{"auto"});
    CHECK_EQ(declaration_value(block, "animation-range-start"), std::string{"normal"});
    CHECK_EQ(declaration_value(block, "animation"),
             std::string{"1s cubic-bezier(0, -2, 1, 3) -3s 4 reverse both paused anim"});
    // Two items: the omitted parts of each are the defaults, listed.
    (void)set_declaration(block, "animation",
                          "anim paused both reverse, 4 1s -3s cubic-bezier(0, -2, 1, 3)", false);
    CHECK_EQ(declaration_value(block, "animation-duration"), std::string{"auto, 1s"});
    CHECK_EQ(declaration_value(block, "animation-name"), std::string{"anim, none"});
    CHECK_EQ(declaration_value(block, "animation-fill-mode"), std::string{"both, none"});
    CHECK_EQ(declaration_value(block, "animation"),
             std::string{"reverse both paused anim, 1s cubic-bezier(0, -2, 1, 3) -3s 4"});
    CHECK_EQ(set_and_read("animation", "1s"), std::string{"1s"});
    CHECK_EQ(set_and_read("animation", "1s -3s"), std::string{"1s -3s"});
    CHECK_EQ(set_and_read("animation", "none"), std::string{"none"});
    CHECK_EQ(set_and_read("animation", "ease-in-out"), std::string{"ease-in-out"});
    CHECK_EQ(set_and_read("animation", "1s 2s 3s"), std::string{});
    CHECK_EQ(set_and_read("animation", "-1s -2s"), std::string{});
    CHECK_EQ(set_and_read("animation", "cubic-bezier( 0, -2, 1, 3 )"),
             std::string{"cubic-bezier(0, -2, 1, 3)"});
    CHECK_EQ(check_declaration("animation-name", "multi\\ word").serialized,
             std::string{"multi\\ word"});
    // `[ none | <keyframes-name> ]#`: a string is the identifier it names
    // unless that would be a keyword.
    CHECK_EQ(check_declaration("animation-name", "NONE").serialized, std::string{"none"});
    CHECK_EQ(check_declaration("animation-name", "first, second").serialized,
             std::string{"first, second"});
    CHECK_EQ(check_declaration("animation-name", "\"something\"").serialized,
             std::string{"something"});
    CHECK_EQ(check_declaration("animation-name", "\"multi word\"").serialized,
             std::string{"multi\\ word"});
    CHECK_EQ(check_declaration("animation-name", "\"none\"").serialized, std::string{"\"none\""});
    CHECK_EQ(check_declaration("animation-name", "\"INITIAL\"").serialized,
             std::string{"\"INITIAL\""});
    CHECK(!check_declaration("animation-name", "12").valid);
    CHECK(!check_declaration("animation-name", "one two").valid);
    CHECK(!check_declaration("animation-name", "one, initial").valid);
    CHECK(!check_declaration("animation-name", "default, two").valid);
    CHECK(!check_declaration("animation-name", "\"\"").valid);
}

// font-computed, white-space-computed, animation-computed: through a page,
// the cascade splits each shorthand into its longhands - a `font` sets the
// font-size every `em` resolves against - and getComputedStyle folds the
// computed longhands back.
void test_what_a_page_reads_back() {
    using ctbrowser::shell::browser;
    using ctbrowser::shell::browser_options;
    using ctbrowser_test::logged;
    browser page{browser_options{400, 300}};
    page.load_html(R"html(<html><head><style>
      #a { font: italic small-caps 700 condensed 20px/2 Arial, serif; padding-top: 1em }
      #b { font: 10px sans-serif; line-height: 3 }
      #c { white-space: preserve nowrap; text-wrap: balance }
      #d { text-wrap: nowrap balance; white-space: normal }
      #e { animation: anim paused both reverse 4 1s -3s cubic-bezier(0, -2, 1, 3) }
      #f { font-weight: 800 }
    </style></head><body>
    <div id=a></div><div id=b></div><div id=c></div><div id=d></div><div id=e></div>
    <div id=f><div id=g></div></div>
    <script>
        const cs = (id) => getComputedStyle(document.getElementById(id));
        const a = cs('a');
        console.log('font=' + [a.font, a.fontSize, a.fontStyle, a.fontWeight, a.lineHeight,
                               a.fontFamily, a.paddingTop].join('|'));
        console.log('reset=' + cs('b').font + '|' + cs('b').lineHeight + '|' + cs('g').font);
        console.log('ws=' + [cs('c').whiteSpace, cs('c').textWrap, cs('d').textWrap,
                             cs('d').whiteSpace, cs('g').whiteSpace].join('|'));
        console.log('anim=' + cs('e').animation + '|' + cs('e').animationName + '|' +
                    cs('g').animation);
        const s = document.getElementById('g').style;
        s.font = '20px/1.5 fantasy';
        console.log('set=' + s.font + '|' + cs('g').font);
    </script></body></html>)html");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "font="),
             std::string{"font=italic small-caps 700 condensed 20px / 40px Arial, serif|20px|"
                         "italic|700|40px|Arial, serif|20px"});
    // `g` inherits the UA sheet's `serif` and its parent's weight.
    CHECK_EQ(logged(page, "reset="),
             std::string{"reset=10px / 30px sans-serif|30px|800 16px serif"});
    // `text-wrap: balance` after `white-space: preserve nowrap` resets the
    // wrap mode, so the pair spells `pre-wrap`.
    CHECK_EQ(logged(page, "ws="), std::string{"ws=pre-wrap|balance|balance|normal|normal"});
    CHECK_EQ(logged(page, "anim="),
             std::string{"anim=1s cubic-bezier(0, -2, 1, 3) -3s 4 reverse both paused anim|anim|"
                         "none"});
    CHECK_EQ(logged(page, "set="), std::string{"set=20px / 1.5 fantasy|20px / 30px fantasy"});
}

// grid-template and grid (CSS Grid 2 §7.4, §7.8): split into their longhands
// and folded back only when the longhands can be spelled as one value.
void test_grid_is_a_shorthand() {
    declaration_block block;
    CHECK(set_declaration(block, "grid-template", "[a] \"a\" 10px [] \"b\" / 5fr", false));
    CHECK_EQ(declaration_value(block, "grid-template-rows"), std::string{"[a] 10px auto"});
    CHECK_EQ(declaration_value(block, "grid-template-areas"), std::string{"\"a\" \"b\""});
    CHECK_EQ(declaration_value(block, "grid-template"), std::string{"[a] \"a\" 10px \"b\" / 5fr"});
    CHECK_EQ(set_and_read("grid-template", "none / none"), std::string{"none"});
    CHECK_EQ(set_and_read("grid-template", "10px"), std::string{});
    CHECK_EQ(set_and_read("grid", "auto-flow auto / 100px 100px"),
             std::string{"none / 100px 100px"});
    CHECK_EQ(set_and_read("grid", "1px / auto-flow dense 2px"),
             std::string{"1px / auto-flow dense 2px"});
    declaration_block mixed;
    CHECK(set_declaration(mixed, "grid", "30px / 50px", false));
    CHECK(set_declaration(mixed, "grid-auto-flow", "column", false));
    CHECK_EQ(declaration_value(mixed, "grid"), std::string{});
    CHECK_EQ(declaration_value(mixed, "grid-template"), std::string{"30px / 50px"});
}

// LOGIC-058/059: an escaped token made the declaration keep the author's RAW
// text, and the shorthand splitters sliced it by offsets into the preprocessed
// token pool, where each NUL is three bytes - `substr` threw past the end and
// terminated the engine. Surviving without a throw is the assertion.
void test_nul_bearing_values_split_without_throwing() {
    using namespace std::string_literals;
    const std::string nuls = "\\61\0\0\0\0/"s;
    for (const std::string_view property :
         {"background", "mask", "border-image", "border-radius", "container", "column-rule-inset",
          "rule-inset", "columns"}) {
        (void)set_and_read(property, nuls);
        (void)set_and_read(property, "\\61\0\0/"s);
        (void)set_and_read(property, "\\61\0\0\0\0/ 1px"s);
    }
    // The kept text is the preprocessed one: a NUL serialises as U+FFFD.
    CHECK_EQ(set_and_read("container", "\\61\0 / size"s), std::string{"a\xEF\xBF\xBD / size"});
}

} // namespace

int main() {
    test_nul_bearing_values_split_without_throwing();
    test_grid_is_a_shorthand();
    test_white_space_is_a_shorthand();
    test_animation_is_a_shorthand();
    test_what_a_page_reads_back();
    REPORT("css_shorthands");
}
