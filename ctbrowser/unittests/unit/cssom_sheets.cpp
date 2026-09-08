// THE SHEET OBJECTS, AND THE DECLARATION BLOCKS A SCRIPT READS OFF AN ELEMENT.
//
// `new CSSStyleSheet`, `styleElement.sheet`, `adoptedStyleSheets` and `replace`,
// the text the cascade would be handed, a `<style>` a script appends actually
// applying - then `getComputedStyle` with a pseudo-element argument, the
// automatic minimum size, a font family keeping its case, and `el.style`
// following the attribute. cssom_rules.cpp is the other half, and carries the
// original file's header: what a RULE serialises to, and what this cannot test.
//
// One of two files carved out of unittests/unit/cssom.cpp on 2026-09-07, when it
// passed 1,000 lines. Every case is verbatim and in the order it had; `logged`,
// which both need, is in cssom_probe.hpp beside this.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include "cssom_probe.hpp"

#include <memory>
#include <string>
#include <string_view>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser_test::logged;

namespace {

void test_a_constructed_sheet() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style>.z { color: red }</style></head><body><script>
        const sheet = new CSSStyleSheet({disabled: true, media: 'screen, print'});
        console.log('made=' + (sheet instanceof CSSStyleSheet) + ',' + sheet.title + ',' +
                    sheet.ownerNode + ',' + sheet.ownerRule + ',' + sheet.disabled + ',' +
                    sheet.cssRules.length);
        console.log('media=' + sheet.media.length + ',' + sheet.media.item(0) + ',' +
                    sheet.media.item(1));
        // The constructor ignores `title` on purpose - a constructed sheet has
        // none however it was made.
        console.log('titled=' + new CSSStyleSheet({title: 'x'}).title);
        sheet.replaceSync('.a { color: red } .b { color: blue }');
        console.log('replaced=' + sheet.cssRules.length + ',' + sheet.cssRules[0].cssText);
        sheet.insertRule('.c { color: green }');
        console.log('inserted=' + sheet.cssRules.length + ',' + sheet.cssRules[0].cssText);
        console.log('adopted=' + document.adoptedStyleSheets.length);
        document.adoptedStyleSheets = [sheet];
        console.log('adoptedNow=' + document.adoptedStyleSheets.length + ',' +
                    (document.adoptedStyleSheets[0] === sheet));
        // replace() answers a promise for the sheet; replaceSync on a sheet
        // that is NOT constructed is a NotAllowedError.
        let caught = '';
        try { document.styleSheets[0].replaceSync('.q { color: red }'); }
        catch (e) { caught = e.name; }
        console.log('regular=' + caught);
        sheet.replace('.d { color: pink }').then(function (back) {
            console.log('promised=' + (back === sheet) + ',' + back.cssRules.length);
        });
    </script></body></html>)");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "made="), std::string{"made=true,null,null,null,true,0"});
    CHECK_EQ(logged(page, "media="), std::string{"media=2,screen,print"});
    CHECK_EQ(logged(page, "titled="), std::string{"titled=null"});
    CHECK_EQ(logged(page, "replaced="), std::string{"replaced=2,.a { color: red; }"});
    // insertRule with no index inserts at 0, which is what the four
    // `dom/events` animation tests rely on.
    CHECK_EQ(logged(page, "inserted="), std::string{"inserted=3,.c { color: green; }"});
    CHECK_EQ(logged(page, "adopted="), std::string{"adopted=0"});
    CHECK_EQ(logged(page, "adoptedNow="), std::string{"adoptedNow=1,true"});
    // `replace`/`replaceSync` belong to a CONSTRUCTED sheet and to nothing else.
    CHECK_EQ(logged(page, "regular="), std::string{"regular=NotAllowedError"});
    CHECK_EQ(logged(page, "promised="), std::string{"promised=true,1"});
}

void test_the_sheet_of_an_element() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style id=s>.a { color: red }</style></head><body><script>
        const el = document.getElementById('s');
        console.log('sheet=' + (el.sheet === document.styleSheets[0]) + ',' +
                    (el.sheet.ownerNode === el));
        // A <style> the SCRIPT created and appended has a sheet too - which is
        // what `dom/events`' four animation tests do before they touch it.
        const made = document.createElement('style');
        document.head.appendChild(made);
        made.sheet.insertRule('.b { color: blue }');
        console.log('made=' + made.sheet.cssRules.length + ',' + made.sheet.cssRules[0].cssText);
        console.log('grew=' + document.styleSheets.length);
    </script></body></html>)");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "sheet="), std::string{"sheet=true,true"});
    CHECK_EQ(logged(page, "made="), std::string{"made=1,.b { color: blue; }"});
    CHECK_EQ(logged(page, "grew="), std::string{"grew=2"});
}

// WHAT THE CASCADE WOULD BE HANDED. `insertRule` cannot reach the style engine
// from the bindings - the browser loads the author sheet once per page - so the
// thing to assert is the TEXT `set_author_styles_hook` publishes, which is the
// whole of the contract between this rung and the one that wires it up.
void test_the_text_the_cascade_would_get() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style>body { width: 50%; }</style></head><body><script>
        document.styleSheets[0].insertRule('#bar { margin: 10px; }', 1);
    </script></body></html>)");
    CHECK(page.script_error().empty());
    CHECK_EQ(page.bindings().author_style_text(),
             std::string{"body { width: 50%; }\n#bar { margin: 10px; }\n"});
    // A DISABLED sheet contributes nothing, which is the other half of what the
    // hook has to get right.
    browser off{browser_options{400, 200}};
    off.load_html(R"(<html><head><style>body { width: 50%; }</style></head><body><script>
        document.styleSheets[0].disabled = true;
    </script></body></html>)");
    CHECK(off.script_error().empty());
    CHECK_EQ(off.bindings().author_style_text(), std::string{});
}

// AN ADOPTED SHEET REACHES THE AUTHOR CSS, and in the order it was adopted in.
//
// It was in the object model and in nothing else: `document.styleSheets`
// correctly does not include a constructed sheet, so a sheet a page adopted
// appeared in no serialisation of the document's styles at all. CSSOM puts the
// adopted sheets LAST in the final list, which is what makes
// `adoptedStyleSheets = [red, green]` green and `[green, red]` red -
// `adoptedstylesheets-cascade-order.html` asserts that pair and then asserts it
// again for a rotation that adds and removes nothing.
//
// THE TEXT AND NOT A PIXEL, for the same reason the case above asserts text:
// `set_author_styles_hook` is still unfilled, so `browser::refresh_author_styles`
// re-collects the `<style>` elements' own bytes and an adopted sheet is not one
// of them. What this pins is the contract between the two rungs.
void test_an_adopted_sheet_reaches_the_author_css() {
    const auto adopt = [](const char * order) {
        auto page = std::make_unique<browser>(browser_options{400, 200});
        page->load_html(std::string{R"(<html><head><style>#t { color: rgb(1, 2, 3) }</style>
        </head><body><p id=t>x</p><script>
            const red = new CSSStyleSheet();
            red.replaceSync('#t { color: rgb(255, 0, 0) }');
            const green = new CSSStyleSheet();
            green.replaceSync('#t { color: rgb(0, 128, 0) }');
            document.adoptedStyleSheets = [)"} +
                        order + R"(];
        </script></body></html>)");
        return page;
    };
    const std::unique_ptr<browser> first = adopt("red, green");
    CHECK(first->script_error().empty());
    CHECK_EQ(first->bindings().author_style_text(),
             std::string{"#t { color: rgb(1, 2, 3); }\n#t { color: rgb(255, 0, 0); }\n"
                         "#t { color: rgb(0, 128, 0); }\n"});
    const std::unique_ptr<browser> second = adopt("green, red");
    CHECK(second->script_error().empty());
    CHECK_EQ(second->bindings().author_style_text(),
             std::string{"#t { color: rgb(1, 2, 3); }\n#t { color: rgb(0, 128, 0); }\n"
                         "#t { color: rgb(255, 0, 0); }\n"});
    const std::unique_ptr<browser> none = adopt("");
    CHECK(none->script_error().empty());
    CHECK_EQ(none->bindings().author_style_text(), std::string{"#t { color: rgb(1, 2, 3); }\n"});
}

// `replace` and `replaceSync` REFUSE DIFFERENTLY: one throws and one rejects.
void test_replace_refuses_a_regular_sheet() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style>#t { color: rgb(1, 2, 3) }</style></head>
    <body><p id=t>x</p><script>
        let caught = '';
        try { document.styleSheets[0].replaceSync('#t { color: red }'); }
        catch (e) { caught = e.name; }
        console.log('sync=' + caught);
        document.styleSheets[0].replace('#t { color: red }').then(
            function () { console.log('async=resolved'); },
            function (e) { console.log('async=' + e.name); });
    </script></body></html>)");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "sync="), std::string{"sync=NotAllowedError"});
    // A throw out of `replace` fails the suite's test with an uncaught exception
    // rather than the rejection it is waiting for.
    CHECK_EQ(logged(page, "async="), std::string{"async=NotAllowedError"});
}

// A `<style>` A SCRIPT APPENDS ACTUALLY APPLIES. `browser::load_author_styles`
// latched, so until `refresh_author_styles` this did nothing at all - injecting
// a stylesheet and then reading `getComputedStyle` is how a great many tests
// and no few libraries work, and the page simply kept the styles it loaded with.
void test_an_injected_style_element_restyles() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><body><p id=t>x</p><script>
        const before = getComputedStyle(document.getElementById('t')).color;
        const s = document.createElement('style');
        s.textContent = '#t { color: rgb(1, 2, 3) }';
        document.body.appendChild(s);
        const after = getComputedStyle(document.getElementById('t')).color;
        console.log('color=' + before + '|' + after);
    </script></body></html>)");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "color="), std::string{"color=rgb(0, 0, 0)|rgb(1, 2, 3)"});

    // ...and REMOVING it puts the page back, which is the half a one-way
    // "append to the sheet" hook would have got wrong.
    browser undo{browser_options{400, 200}};
    undo.load_html(R"(<html><head><style id=s>#t { color: rgb(1, 2, 3) }</style></head>
    <body><p id=t>x</p><script>
        const el = document.getElementById('t');
        const was = getComputedStyle(el).color;
        document.getElementById('s').remove();
        console.log('gone=' + was + '|' + getComputedStyle(el).color);
    </script></body></html>)");
    CHECK(undo.script_error().empty());
    CHECK_EQ(logged(undo, "gone="), std::string{"gone=rgb(1, 2, 3)|rgb(0, 0, 0)"});
}

// A PSEUDO-ELEMENT ARGUMENT IS NOT AN ARGUMENT TO IGNORE. `getComputedStyle`
// took a second argument and threw it away, so `getComputedStyle(el,
// '::before')` reported the ORIGINATING ELEMENT's style as the
// pseudo-element's - `100px` where every engine says `""`. CSSOM says a
// pseudo-element that does not exist reports an EMPTY declaration, and this
// engine has no pseudo-element styling at all, so every colon-prefixed argument
// is that case.
//
// THE THREE SHAPES, which are what css/cssom's getComputedStyle-pseudo,
// -pseudo-with-argument, -pseudo-picker and -pseudo-checkmark divide their
// assertions into: no colon is IGNORED, one colon and two colons are both
// pseudo-element requests, and an empty declaration is still a
// CSSStyleDeclaration - it answers the empty string rather than `undefined`,
// and it still refuses to be written to.
void test_a_pseudo_element_argument_is_not_the_element() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style>#t { color: rgb(255, 0, 0); width: 100px }
    </style></head><body><div id=t></div><script>
        const el = document.getElementById('t');
        const own = getComputedStyle(el);
        // No colon: the argument is ignored and the ELEMENT answers.
        const ignored = getComputedStyle(el, 'totallynotapseudo');
        const before = getComputedStyle(el, '::before');
        const legacy = getComputedStyle(el, ':checkmark');
        // A trailing token makes it unparseable, which is the same answer.
        const broken = getComputedStyle(el, '::before,::after');
        console.log('own=' + own.color + '|' + (own.length > 0));
        console.log('ignored=' + ignored.color + '|' + (ignored.length > 0));
        console.log('before=' + before.length + '|' + before.color + '|' + before.width);
        console.log('legacy=' + legacy.length + '|' + broken.length);
        // ...and null, undefined and the empty string are not pseudo-elements.
        console.log('absent=' + getComputedStyle(el, null).color + '|' +
                    getComputedStyle(el, '').color + '|' +
                    getComputedStyle(el, undefined).color);
        let threw = '';
        try { before.color = 'blue'; } catch (e) { threw = e.name; }
        let refused = '';
        try { before.setProperty('color', 'blue'); } catch (e) { refused = e.name; }
        console.log('readonly=' + threw + '|' + refused);
    </script></body></html>)");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "own="), std::string{"own=rgb(255, 0, 0)|true"});
    CHECK_EQ(logged(page, "ignored="), std::string{"ignored=rgb(255, 0, 0)|true"});
    // Empty - and empty means the EMPTY STRING for every property, not the
    // `undefined` a missing accessor would give.
    CHECK_EQ(logged(page, "before="), std::string{"before=0||"});
    CHECK_EQ(logged(page, "legacy="), std::string{"legacy=0|0"});
    CHECK_EQ(logged(page, "absent="),
             std::string{"absent=rgb(255, 0, 0)|rgb(255, 0, 0)|rgb(255, 0, 0)"});
    CHECK_EQ(logged(page, "readonly="),
             std::string{"readonly=NoModificationAllowedError|NoModificationAllowedError"});
}

// `min-width: auto` RESOLVES TO ZERO unless something makes the automatic
// minimum mean a size, and css/cssom's getComputedStyle-resolved-min-size-auto
// asserts every element in its fixture twice - once as the initial value and
// once with the same `auto` written down. Only the first of the pair used to
// reach this rule: an `auto` the author wrote fell through to the length branch
// and came back as the keyword.
//
// THREE THINGS PRESERVE IT: a flex item, a grid item, and a specified
// `aspect-ratio` - and none of them if the element generates no box at all.
void test_the_automatic_minimum_size() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style>#f { display: flex } #g { display: grid }
    </style></head><body>
    <div id=plain style="min-width: auto; min-height: 5px"></div>
    <div id=ratio style="aspect-ratio: 1/1; min-width: auto"></div>
    <div id=degenerate style="aspect-ratio: 0/1"></div>
    <div id=twopart style="aspect-ratio: auto 1/1"></div>
    <div id=f><div id=fi style="min-width: auto"></div></div>
    <div id=g><div id=gi></div></div>
    <div style="display: none"><div id=hidden style="min-width: auto"></div></div>
    <script>
        const min = (id) => getComputedStyle(document.getElementById(id)).minWidth;
        console.log('zero=' + min('plain') + '|' + min('hidden'));
        console.log('ratio=' + min('ratio') + '|' + min('degenerate') + '|' + min('twopart'));
        console.log('items=' + min('fi') + '|' + min('gi'));
        // The property still answers what the author wrote when it is not `auto`.
        console.log('given=' + getComputedStyle(document.getElementById('plain')).minHeight);
    </script></body></html>)");
    CHECK(page.script_error().empty());
    // A written `auto` and an absent one are the same `auto`, and neither
    // survives on an ordinary block or on an element with no box.
    CHECK_EQ(logged(page, "zero="), std::string{"zero=0px|0px"});
    // A degenerate ratio and the two-part form preserve it too: the rule is
    // "an aspect-ratio was specified", not "it is usable".
    CHECK_EQ(logged(page, "ratio="), std::string{"ratio=auto|auto|auto"});
    CHECK_EQ(logged(page, "items="), std::string{"items=auto|auto"});
    CHECK_EQ(logged(page, "given="), std::string{"given=5px"});
}

// A FONT FAMILY IS NOT A KEYWORD, and `getComputedStyle` folded it like one:
// `Twisty Tie` came back `twisty tie` on every element of every page that names
// a font. The case is the author's. So is the QUOTING, which CSSOM decides
// rather than copies: a name that is a valid identifier sequence loses its
// quotes, one that is not keeps them, and the quotes it keeps are double ones
// whichever the author wrote. css/cssom/font-family-serialization-001 makes
// five assertions about the computed value and these are them.
void test_the_computed_font_family_keeps_its_case() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><body>
    <div id=a style="font-family: Twisty Tie"></div>
    <div id=b style="font-family: 'Times New Roman'"></div>
    <div id=c style="font-family: '34J'"></div>
    <div id=d style='font-family: "serif"'></div>
    <div id=e style='font-family: Twisty Tie, "34J", "serif", Veronica, sans-serif'></div>
    <div id=f style="font-family: 'A  B'"></div>
    <script>
        const fam = (id) => getComputedStyle(document.getElementById(id)).fontFamily;
        console.log('bare=' + fam('a'));
        console.log('unquoted=' + fam('b'));
        console.log('digits=' + fam('c'));
        console.log('generic=' + fam('d'));
        console.log('list=' + fam('e'));
        console.log('spaces=' + fam('f'));
    </script></body></html>)");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "bare="), std::string{"bare=Twisty Tie"});
    // A quoted name that IS an identifier sequence loses its quotes...
    CHECK_EQ(logged(page, "unquoted="), std::string{"unquoted=Times New Roman"});
    // ...and one that is not keeps them: `34J` starts with a digit, `serif`
    // would become the generic family, and the double space in `A  B` would not
    // survive being written as identifiers.
    CHECK_EQ(logged(page, "digits="), std::string{"digits=\"34J\""});
    CHECK_EQ(logged(page, "generic="), std::string{"generic=\"serif\""});
    CHECK_EQ(logged(page, "spaces="), std::string{"spaces=\"A  B\""});
    CHECK_EQ(logged(page, "list="),
             std::string{"list=Twisty Tie, \"34J\", \"serif\", Veronica, sans-serif"});
}

// `el.style` FOLLOWS THE ATTRIBUTE, and the attribute follows `el.style`.
//
// The declaration store was filled once, when the wrapper was made, and never
// again - so `el.setAttribute("style", ...)`, which writes the attribute
// directly and never touches the proxy, left the read side answering with what
// the element had at construction. `css-style-attr-decl-block.html` says it in
// its own subtest name ("Changes to style attribute should reflect on CSS
// declaration block"), and `serialize-values.html` is 697 subtests of exactly
// that shape: createElement, setAttribute("style", ...), read the IDL attribute.
void test_the_inline_style_follows_the_attribute() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><body><p id=t>x</p><script>
        var t = document.getElementById('t');
        console.log('before=' + t.style.color + '|' + t.style.length);
        t.setAttribute('style', 'color: rgb(1, 2, 3); width: 10px');
        console.log('after=' + t.style.color + '|' + t.style.length + '|' +
                    t.style.getPropertyValue('width') + '|' + t.style.item(0));
        // A SECOND CHANGE, so it is not a one-off refresh on first read.
        t.setAttribute('style', 'height: 2px');
        console.log('again=' + t.style.color + '|' + t.style.height + '|' + t.style.length);
        // And a write through the proxy still reaches the attribute.
        t.style.color = 'rgb(4, 5, 6)';
        console.log('wrote=' + t.getAttribute('style'));
        // A SUPPORTED PROPERTY THAT IS NOT SET IS "", not undefined; a name
        // that is not a property at all is still undefined.
        console.log('unset=' + t.style.marginTop + '|' + (t.style.marginTop === '') + '|' +
                    (t.style.notAProperty === undefined));
      </script></body></html>)");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "before="), std::string{"before=|0"});
    CHECK_EQ(logged(page, "after="), std::string{"after=rgb(1, 2, 3)|2|10px|color"});
    CHECK_EQ(logged(page, "again="), std::string{"again=|2px|1"});
    // The trailing space is `style_attribute`'s and deliberate - see the note
    // above `css_text_of`, which is the one CSSOM does specify.
    CHECK_EQ(logged(page, "wrote="), std::string{"wrote=height: 2px; color: rgb(4, 5, 6); "});
    CHECK_EQ(logged(page, "unset="), std::string{"unset=|true|true"});
}

} // namespace

int main() {
    test_a_constructed_sheet();
    test_the_sheet_of_an_element();
    test_the_text_the_cascade_would_get();
    test_an_adopted_sheet_reaches_the_author_css();
    test_replace_refuses_a_regular_sheet();
    test_an_injected_style_element_restyles();
    test_a_pseudo_element_argument_is_not_the_element();
    test_the_automatic_minimum_size();
    test_the_computed_font_family_keeps_its_case();
    test_the_inline_style_follows_the_attribute();
    REPORT("cssom_sheets");
}
