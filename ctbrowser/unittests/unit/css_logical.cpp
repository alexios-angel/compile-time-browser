// CSS Logical 1: a flow-relative property is the physical one it maps to on
// its element, decided by that element's writing mode and direction (§2), at
// computed-value time, with the later declaration of a logical property group
// winning whichever spelling it used (§4). One case per rule, each named for
// the css/css-logical file it moves.

#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include "cssom_probe.hpp"

#include <string>
#include <string_view>

using ctbrowser::style::css::physical_property_of;

namespace {

// §2: the mapping itself, for every writing mode and both directions.
void test_the_mapping() {
    const auto h = [](std::string_view p) {
        return physical_property_of(p, "horizontal-tb", "ltr");
    };
    CHECK_EQ(h("margin-block-start"), std::string{"margin-top"});
    CHECK_EQ(h("margin-block-end"), std::string{"margin-bottom"});
    CHECK_EQ(h("padding-inline-start"), std::string{"padding-left"});
    CHECK_EQ(h("inset-inline-end"), std::string{"right"});
    CHECK_EQ(h("inset-block-start"), std::string{"top"});
    CHECK_EQ(h("border-inline-end-width"), std::string{"border-right-width"});
    CHECK_EQ(h("border-block-start-style"), std::string{"border-top-style"});
    CHECK_EQ(h("inline-size"), std::string{"width"});
    CHECK_EQ(h("block-size"), std::string{"height"});
    CHECK_EQ(h("min-inline-size"), std::string{"min-width"});
    CHECK_EQ(h("max-block-size"), std::string{"max-height"});
    CHECK_EQ(h("border-start-start-radius"), std::string{"border-top-left-radius"});
    CHECK_EQ(h("border-end-start-radius"), std::string{"border-bottom-left-radius"});
    CHECK_EQ(h("overflow-block"), std::string{"overflow-y"});
    CHECK_EQ(h("overscroll-behavior-inline"), std::string{"overscroll-behavior-x"});
    CHECK_EQ(h("scroll-margin-inline-start"), std::string{"scroll-margin-left"});
    // The empty writing mode and direction are the initial values.
    CHECK_EQ(physical_property_of("margin-inline-start", "", ""), std::string{"margin-left"});
    // Not logical: nothing to map.
    CHECK_EQ(h("margin-top"), std::string{});
    CHECK_EQ(h("margin-block"), std::string{});
    CHECK_EQ(h("display"), std::string{});
    CHECK_EQ(h("block-step-size"), std::string{});
    // rtl flips the inline axis only.
    CHECK_EQ(physical_property_of("margin-inline-start", "horizontal-tb", "rtl"),
             std::string{"margin-right"});
    CHECK_EQ(physical_property_of("margin-block-start", "horizontal-tb", "rtl"),
             std::string{"margin-top"});
    CHECK_EQ(physical_property_of("border-start-start-radius", "horizontal-tb", "rtl"),
             std::string{"border-top-right-radius"});
    // vertical-rl: block runs right to left, inline top to bottom.
    CHECK_EQ(physical_property_of("margin-block-start", "vertical-rl", "ltr"),
             std::string{"margin-right"});
    CHECK_EQ(physical_property_of("margin-inline-start", "vertical-rl", "ltr"),
             std::string{"margin-top"});
    CHECK_EQ(physical_property_of("margin-inline-start", "vertical-rl", "rtl"),
             std::string{"margin-bottom"});
    CHECK_EQ(physical_property_of("inline-size", "vertical-rl", "ltr"), std::string{"height"});
    CHECK_EQ(physical_property_of("block-size", "vertical-rl", "ltr"), std::string{"width"});
    CHECK_EQ(physical_property_of("border-start-start-radius", "vertical-rl", "ltr"),
             std::string{"border-top-right-radius"});
    CHECK_EQ(physical_property_of("overflow-block", "vertical-rl", "ltr"),
             std::string{"overflow-x"});
    // vertical-lr: block runs left to right.
    CHECK_EQ(physical_property_of("inset-block-start", "vertical-lr", "ltr"), std::string{"left"});
    CHECK_EQ(physical_property_of("inset-inline-end", "vertical-lr", "ltr"), std::string{"bottom"});
    // sideways-lr is the one mode whose inline axis runs upward.
    CHECK_EQ(physical_property_of("inset-inline-start", "sideways-lr", "ltr"),
             std::string{"bottom"});
    CHECK_EQ(physical_property_of("inset-inline-start", "sideways-lr", "rtl"), std::string{"top"});
    CHECK_EQ(physical_property_of("inset-block-start", "sideways-rl", "ltr"), std::string{"right"});
}

// margin-block-inline-computed, border-block-style-computed, inline-size-
// computed, inheritance: the cascade stores the physical side and both
// spellings read it back; §4's group rule orders logical against physical by
// source position; a vertical writing mode maps to the other axis.
void test_what_a_page_reads_back() {
    using ctbrowser::shell::browser;
    using ctbrowser::shell::browser_options;
    using ctbrowser_test::logged;
    browser page{browser_options{400, 300}};
    page.load_html(R"html(<html><head><style>
      #a { margin-block-start: 20px; margin-inline: 30px 40px; border-block-style: dotted;
           padding-inline-end: 12px; inset-block-end: 3px; border-inline-start: 2px solid red }
      #b { margin-left: 1px; margin-inline-start: 2px }
      #c { margin-inline-start: 2px; margin-left: 1px }
      #d { writing-mode: vertical-rl; margin-block-start: 5px;
           border-inline-start-width: 4px; border-inline-start-style: solid }
      #e { direction: rtl; padding-inline-start: 7px }
      #f { margin-left: 8px; margin-inline-start: var(--nothing) }
      #g { inline-size: 50px; block-size: 20px; max-inline-size: 70px; max-block-size: none;
           border-end-end-radius: 10px 20px }
    </style></head><body style="width: 200px">
    <div id=a></div><div id=b></div><div id=c></div><div id=d></div><div id=e></div>
    <div id=f></div><div id=g></div>
    <script>
        const cs = (id) => getComputedStyle(document.getElementById(id));
        const a = cs('a');
        console.log('a=' + [a.marginTop, a.marginBlockStart, a.marginLeft, a.marginRight,
                            a.marginInline, a.borderTopStyle, a.borderBlockStyle,
                            a.paddingRight, a.paddingInlineEnd, a.bottom, a.insetBlockEnd,
                            a.borderLeftWidth, a.borderLeftColor].join('|'));
        console.log('group=' + cs('b').marginLeft + '|' + cs('c').marginLeft);
        const d = cs('d');
        console.log('vertical=' + [d.marginRight, d.marginBlockStart, d.marginTop,
                                   d.borderTopWidth].join('|'));
        const g = cs('g');
        console.log('size=' + [g.width, g.inlineSize, g.height, g.blockSize, g.maxWidth,
                               g.maxInlineSize, g.maxBlockSize, g.borderBottomRightRadius,
                               g.borderEndEndRadius].join('|'));
        console.log('rtl=' + cs('e').paddingRight + '|' + cs('e').paddingLeft);
        console.log('unset=' + cs('f').marginLeft);
        const s = document.getElementById('a').style;
        s.borderBlock = '1px dotted red';
        s.borderInlineEnd = 'green double thin';
        const whole = s.borderBlock;
        s.borderBlockWidth = '2px 3px';
        console.log('cssom=' + [whole, s.borderBlockStartStyle, s.borderInlineEnd,
                                s.borderBlockWidth, s.borderBlockEndWidth, s.borderBlock].join('|'));
        console.log('mapped=' + a.borderTopStyle + '|' + a.borderBottomWidth + '|' +
                    a.borderRightStyle);
    </script></body></html>)html");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "a="),
             std::string{"a=20px|20px|30px|40px|30px 40px|dotted|dotted|12px|12px|3px|3px|2px|"
                         "rgb(255, 0, 0)"});
    CHECK_EQ(logged(page, "group="), std::string{"group=2px|1px"});
    CHECK_EQ(logged(page, "vertical="), std::string{"vertical=5px|5px|0px|4px"});
    CHECK_EQ(logged(page, "size="),
             std::string{"size=50px|50px|20px|20px|70px|70px|none|10px 20px|10px 20px"});
    CHECK_EQ(logged(page, "rtl="), std::string{"rtl=7px|0px"});
    // A logical declaration invalid at computed-value time unsets the
    // physical side it maps to, the inline `margin-left` included.
    CHECK_EQ(logged(page, "unset="), std::string{"unset=0px"});
    CHECK_EQ(logged(page, "cssom="),
             std::string{"cssom=1px dotted red|dotted|thin double green|2px 3px|3px|"});
    CHECK_EQ(logged(page, "mapped="), std::string{"mapped=dotted|3px|double"});
}

} // namespace

int main() {
    test_the_mapping();
    test_what_a_page_reads_back();
    REPORT("css_logical");
}
