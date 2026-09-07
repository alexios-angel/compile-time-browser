// The CSSOM object model - `document.styleSheets` and everything under it.
//
// EVERY STRING COMPARED HERE IS A STRING `css/cssom` COMPARES. The suite's
// `CSSRuleList.html`, `CSSStyleSheet.html` and `CSSStyleRule.html` assert
// `cssText` and `selectorText` byte for byte, so the interesting question is
// never "did a rule come back" but "did it come back spelled the way the
// specification spells it". That is why this file is not more of
// `css_values.cpp`: that one asks what a VALUE is worth, this one asks what the
// object model SERIALISES to.
//
// The serialisation is CANONICAL rather than the author's bytes, and the two
// cases that prove it are here: a rule written across two indented lines comes
// back as one line, and a selector written `[type=checkbox]` comes back
// `[type="checkbox"]`. Neither is reachable from the source text - a `raw_rule`
// records no source span at all - and both are what a browser answers.
//
// THE THREE THINGS THIS CANNOT TEST, said here rather than discovered:
//
//   * the cascade does not observe insertRule. `set_author_styles_hook` is the
//     slot for that and the browser does not fill it yet, so the last case
//     below asserts the TEXT the hook would be handed rather than a pixel.
//   * `styleElement.sheet` needs one call in `install_element_views`, and
//     `document.styleSheets` needs one in `dom_bindings::install`. Both are in
//     files the CSSOM rung does not own; without them these cases fail with
//     "undefined", which is the honest way for a missing wire to report.
//   * an at-rule whose block this engine does not model keeps the author's
//     bytes. `@namespace` and `@import` have no block at all and nothing to
//     reconstruct a prelude from, so `cssText` is the source text and says so.
//     Every at-rule whose block IS rules or IS declarations serialises like any
//     other rule; `test_every_rule_a_sheet_carries` asserts which is which.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"

#include <memory>
#include <string>
#include <string_view>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

// A logged line by its prefix, so a case that adds a `console.log` in the
// middle does not renumber every assertion after it.
[[nodiscard]] std::string logged(browser & page, std::string_view prefix) {
    for (const std::string & line : page.bindings().console_output()) {
        if (line.starts_with(prefix)) { return line; }
    }
    return std::string{"<no line beginning "} + std::string{prefix} + ">";
}

void test_the_list_and_the_rules() {
    browser page{browser_options{400, 200}};
    // INDENTED, ACROSS TWO LINES, exactly as `css/cssom/CSSRuleList.html`
    // writes it - so a serialisation made of the author's bytes gets this
    // wrong and a canonical one gets it right.
    page.load_html(R"(<html><head>
    <style>
        body { width: 50%; }
        #foo { height: 100px; }
    </style>
    <style>
        .a { color: red }
    </style>
    </head><body><script>
        const sheets = document.styleSheets;
        console.log('sheets=' + sheets.length);
        console.log('counts=' + sheets[0].cssRules.length + ',' + sheets[1].cssRules.length);
        console.log('past=' + sheets[2] + ',' + sheets.item(2));
        console.log('text0=' + sheets[0].cssRules[0].cssText);
        console.log('text1=' + sheets[0].cssRules.item(1).cssText);
        console.log('rulepast=' + sheets[0].cssRules[2] + ',' + sheets[0].cssRules.item(2));
        console.log('type=' + sheets[0].type + ',' + sheets[0].href + ',' + sheets[0].title);
        // [SameObject], three ways: the list, the rule list and its alias.
        sheets.marked = 7;
        console.log('same=' + (document.styleSheets === sheets) + ',' +
                    (document.styleSheets.marked === 7) + ',' +
                    (sheets[0].cssRules === sheets[0].rules));
    </script></body></html>)");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "sheets="), std::string{"sheets=2"});
    CHECK_EQ(logged(page, "counts="), std::string{"counts=2,1"});
    // An indexed getter past the end is `undefined`; `item()` is `null`. Two
    // different answers to the same question, and `CSSRuleList.html` asserts
    // both.
    CHECK_EQ(logged(page, "past="), std::string{"past=undefined,null"});
    CHECK_EQ(logged(page, "text0="), std::string{"text0=body { width: 50%; }"});
    CHECK_EQ(logged(page, "text1="), std::string{"text1=#foo { height: 100px; }"});
    CHECK_EQ(logged(page, "rulepast="), std::string{"rulepast=undefined,null"});
    // A `<style>` has no href and no title, and both report `null` rather than
    // an empty string.
    CHECK_EQ(logged(page, "type="), std::string{"type=text/css,null,null"});
    CHECK_EQ(logged(page, "same="), std::string{"same=true,true,true"});
}

void test_a_style_rule() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style id=s>
        input[type=checkbox]:checked ~ label { color: red; margin: 10px }
    </style></head><body><script>
        const rule = document.styleSheets[0].cssRules[0];
        console.log('type=' + rule.type + ',' + rule.STYLE_RULE + ',' + rule.MEDIA_RULE);
        // The author wrote `[type=checkbox]`; CSSOM serialises an attribute
        // value as a string whatever it was written as.
        console.log('sel=' + rule.selectorText);
        console.log('css=' + rule.cssText);
        console.log('parent=' + rule.parentRule + ',' +
                    (rule.parentStyleSheet === document.styleSheets[0]));
        console.log('style=' + rule.style.color + ',' + rule.style.margin + ',' +
                    rule.style.length + ',' + rule.style[0] + ',' + rule.style.item(1));
        console.log('same=' + (rule.style === rule.style));
        console.log('is=' + (rule instanceof CSSRule) + ',' + (rule instanceof CSSStyleRule) +
                    ',' + (rule.style instanceof CSSStyleDeclaration));
        rule.style.color = 'blue';
        rule.style.setProperty('padding', '2px');
        console.log('after=' + rule.cssText);
        console.log('priority=' + rule.style.getPropertyPriority('color'));
        // [PutForwards=cssText]: assigning to `style` assigns to its cssText
        // and the object itself never changes.
        const held = rule.style;
        rule.style = 'margin: 42px';
        console.log('forwarded=' + rule.style.margin + ',' + (rule.style === held) + ',' +
                    rule.cssText);
        rule.selectorText = 'a > b';
        console.log('resel=' + rule.selectorText);
    </script></body></html>)");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "type="), std::string{"type=1,1,4"});
    CHECK_EQ(logged(page, "sel="), std::string{"sel=input[type=\"checkbox\"]:checked ~ label"});
    CHECK_EQ(logged(page, "css="), std::string{"css=input[type=\"checkbox\"]:checked ~ label "
                                               "{ color: red; margin: 10px; }"});
    CHECK_EQ(logged(page, "parent="), std::string{"parent=null,true"});
    // An INDEX names a property, not a value - CSSOM 6.7.1.
    CHECK_EQ(logged(page, "style="), std::string{"style=red,10px,2,color,margin"});
    CHECK_EQ(logged(page, "same="), std::string{"same=true"});
    CHECK_EQ(logged(page, "is="), std::string{"is=true,true,true"});
    CHECK_EQ(logged(page, "after="), std::string{"after=input[type=\"checkbox\"]:checked ~ label "
                                                 "{ color: blue; margin: 10px; padding: 2px; }"});
    CHECK_EQ(logged(page, "priority="), std::string{"priority="});
    CHECK_EQ(logged(page, "forwarded="),
             std::string{"forwarded=42px,true,input[type=\"checkbox\"]:checked ~ label "
                         "{ margin: 42px; }"});
    CHECK_EQ(logged(page, "resel="), std::string{"resel=a > b"});
}

void test_insert_and_delete() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style>
        body { width: 50%; }
        #foo { height: 100px; }
    </style></head><body><script>
        const sheet = document.styleSheets[0];
        sheet.cssRules[0].marked = 1;
        sheet.cssRules[1].marked = 2;
        console.log('at=' + sheet.insertRule('#bar { margin: 10px; }', 1));
        console.log('texts=' + sheet.cssRules[0].cssText + '|' + sheet.cssRules[1].cssText +
                    '|' + sheet.cssRules[2].cssText);
        // [SameObject] across an insertion: the two rules the page marked must
        // be the same objects afterwards, at their new indices.
        console.log('kept=' + sheet.cssRules[0].marked + ',' + sheet.cssRules[2].marked);
        sheet.deleteRule(1);
        console.log('back=' + sheet.cssRules.length + ',' + sheet.cssRules[0].marked + ',' +
                    sheet.cssRules[1].marked);
        let caught = '';
        try { sheet.insertRule('#x { color: red }', 99); } catch (e) { caught = e.name; }
        console.log('big=' + caught);
        caught = '';
        try { sheet.deleteRule(99); } catch (e) { caught = e.name; }
        console.log('gone=' + caught);
        caught = '';
        try { sheet.insertRule('not a rule at all'); } catch (e) { caught = e.name; }
        console.log('bad=' + caught);
        caught = '';
        try { sheet.insertRule(); } catch (e) { caught = e.name; }
        console.log('none=' + caught);
        // AN EMPTY BLOCK IS A RULE. The CSS front end drops one - it keeps a
        // rule only when it has both a selector and a declaration - so
        // `insertRule` splits the text at the brace itself and parses the two
        // halves separately. `addRule()` with no arguments cannot produce
        // anything else, and `css/cssom/CSSStyleSheet.html` asserts its answer.
        console.log('empty=' + sheet.insertRule('div { }', 0) + ',' +
                    sheet.cssRules[0].cssText);
        console.log('added=' + sheet.addRule() + ',' +
                    sheet.cssRules[sheet.cssRules.length - 1].cssText);
        caught = '';
        try { sheet.insertRule('a {} b {}'); } catch (e) { caught = e.name; }
        console.log('two=' + caught);
    </script></body></html>)");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "at="), std::string{"at=1"});
    CHECK_EQ(logged(page, "texts="), std::string{"texts=body { width: 50%; }|#bar "
                                                 "{ margin: 10px; }|#foo { height: 100px; }"});
    CHECK_EQ(logged(page, "kept="), std::string{"kept=1,2"});
    CHECK_EQ(logged(page, "back="), std::string{"back=2,1,2"});
    CHECK_EQ(logged(page, "big="), std::string{"big=IndexSizeError"});
    CHECK_EQ(logged(page, "gone="), std::string{"gone=IndexSizeError"});
    CHECK_EQ(logged(page, "bad="), std::string{"bad=SyntaxError"});
    CHECK_EQ(logged(page, "none="), std::string{"none=TypeError"});
    CHECK_EQ(logged(page, "empty="), std::string{"empty=0,div { }"});
    CHECK_EQ(logged(page, "added="), std::string{"added=-1,undefined { }"});
    // One rule means ONE rule: text with a second one after the first is a
    // syntax error rather than a silent truncation.
    CHECK_EQ(logged(page, "two="), std::string{"two=SyntaxError"});
}

// A SELECTOR IS PARSED, NOT PROBED. `css/cssom/CSSStyleRule-set-selectorText.html`
// spends nineteen of its subtests on selectors that are not selectors, and the
// rule for all of them is one line of CSSOM 6.4.2: if the parse fails, do
// nothing. What that test also demands is the OTHER half - a selector this
// engine cannot MATCH is not a failure, and it must come back spelled the way
// the author wrote it rather than as the `*` an empty compound serialises to.
void test_selector_text_is_parsed_and_escaped() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style id=s>.style0 { color: red }</style></head><body><script>
        const sheet = document.styleSheets[0];
        const rule = sheet.cssRules[0];
        const invalid = ['', ' ', '!!', '123', '-', '$', ':', '.', '#', '[]', '(', '{}'];
        let kept = 0;
        for (const bad of invalid) {
            rule.selectorText = bad;
            if (rule.selectorText === '.style0') { kept++; }
        }
        console.log('invalid=' + kept + '/' + invalid.length);
        rule.selectorText = '  span   div  ';
        console.log('spaces=' + rule.selectorText);
        rule.selectorText = 'div:not(:active)';
        console.log('not=' + rule.selectorText);
        rule.selectorText = ':nth-child( 1n + 5 )';
        console.log('nth=' + rule.selectorText);
        // A pseudo-element compiles to a compound that matches nothing, and
        // there is nothing left in it to serialise.
        rule.selectorText = '::before';
        console.log('pseudo=' + rule.selectorText);
        sheet.insertRule('[\\30zonk] { color: red }', 0);
        console.log('esc=' + sheet.cssRules[0].selectorText);
    </script></body></html>)");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "invalid="), std::string{"invalid=12/12"});
    CHECK_EQ(logged(page, "spaces="), std::string{"spaces=span div"});
    CHECK_EQ(logged(page, "not="), std::string{"not=div:not(:active)"});
    CHECK_EQ(logged(page, "nth="), std::string{"nth=:nth-child(n+5)"});
    CHECK_EQ(logged(page, "pseudo="), std::string{"pseudo=::before"});
    // The tokenizer DECODES an escape, so the name that reaches the compiled
    // selector is `0zonk` - and an identifier may not begin with a digit, so
    // serialising it back without the escape produces a selector that is not
    // one. CSSOM §2.1 escapes the digit numerically, trailing space and all.
    CHECK_EQ(logged(page, "esc="), std::string{"esc=[\\30 zonk]"});
}

// insertRule/deleteRule ON A GROUP. The same pair of methods CSSStyleSheet has
// and a different list - `css/cssom/serialize-media-rule.html` builds every one
// of its fixtures this way, and `CSSGroupingRule-insertRule.html` asserts all
// four of the ways it can refuse.
void test_a_grouping_rule_inserts_and_deletes() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style id=s></style></head><body><script>
        const sheet = document.getElementById('s').sheet;
        sheet.insertRule('@media print {}', 0);
        const group = sheet.cssRules[0];
        console.log('at=' + group.insertRule('#foo { z-index: 23; float: left; }', 0));
        console.log('at2=' + group.insertRule('#bar { float: none; z-index: 45; }', 0));
        console.log('text=' + group.cssText);
        console.log('kids=' + group.cssRules.length + ',' + (group.cssRules === group.cssRules));
        console.log('parent=' + (group.cssRules[0].parentRule.cssText === group.cssText));
        console.log('is=' + (group instanceof CSSMediaRule) + ',' +
                    (group instanceof CSSConditionRule) + ',' +
                    (group instanceof CSSGroupingRule) + ',' + (group instanceof CSSRule));
        let caught = '';
        // The INDEX is checked before the text is parsed, so a bad index and a
        // bad rule together report the index.
        try { group.insertRule('???', 9); } catch (e) { caught = e.name; }
        console.log('range=' + caught + ',' + group.cssRules.length);
        caught = '';
        try { group.insertRule('???', 0); } catch (e) { caught = e.name; }
        console.log('bad=' + caught);
        caught = '';
        try { group.insertRule('@import url("foo.css");', 0); } catch (e) { caught = e.name; }
        console.log('import=' + caught);
        group.deleteRule(0);
        console.log('gone=' + group.cssRules.length + ',' + group.cssText);
    </script></body></html>)");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "at="), std::string{"at=0"});
    CHECK_EQ(logged(page, "at2="), std::string{"at2=0"});
    CHECK_EQ(logged(page, "text="),
             std::string{"text=@media print {\n  #bar { float: none; z-index: 45; }\n"
                         "  #foo { z-index: 23; float: left; }\n}"});
    CHECK_EQ(logged(page, "kids="), std::string{"kids=2,true"});
    CHECK_EQ(logged(page, "parent="), std::string{"parent=true"});
    CHECK_EQ(logged(page, "is="), std::string{"is=true,true,true,true"});
    CHECK_EQ(logged(page, "range="), std::string{"range=IndexSizeError,2"});
    CHECK_EQ(logged(page, "bad="), std::string{"bad=SyntaxError"});
    // `@import` and `@namespace` are TOP-LEVEL rules, so a grouping rule refuses
    // them with a HierarchyRequestError rather than a SyntaxError.
    CHECK_EQ(logged(page, "import="), std::string{"import=HierarchyRequestError"});
    CHECK_EQ(logged(page, "gone="),
             std::string{"gone=1,@media print {\n  #foo { z-index: 23; float: left; }\n}"});
}

// THE MEDIA QUERY LIST, SERIALISED FROM THE AUTHOR'S TEXT. Every string here is
// one `css/cssom/serialize-media-rule.html` compares byte for byte, and the two
// that look like typos are not: `@media  {` really does carry two spaces when
// the query list is empty, and a grouping rule really is the one multi-line
// serialisation in the CSSOM.
void test_media_rules_and_their_lists() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style id=s></style></head><body><script>
        const sheet = document.getElementById('s').sheet;
        sheet.insertRule('@media aLL and (Color) { #a { color: red } }', 0);
        sheet.insertRule('@media {}', 1);
        sheet.insertRule('@media not all and (color) {}', 2);
        sheet.insertRule('@media screen and (max-width: 23px) and (max-width: 45px) {}', 3);
        console.log('n=' + sheet.cssRules.length);
        console.log('c0=' + sheet.cssRules[0].conditionText);
        console.log('t0=' + sheet.cssRules[0].cssText);
        console.log('t1=' + sheet.cssRules[1].cssText);
        console.log('t2=' + sheet.cssRules[2].cssText);
        console.log('t3=' + sheet.cssRules[3].cssText);
        const media = sheet.cssRules[0].media;
        console.log('m=' + media.length + ',' + media[0] + ',' + media[3] + ',' +
                    media.item(1) + ',' + (media === sheet.cssRules[0].media));
        media.appendMedium('PRINT');
        media.appendMedium('print');
        console.log('app=' + media.mediaText + ',' + media.length);
        media.deleteMedium('print');
        console.log('del=' + media.mediaText + ',' + media.toString());
        let caught = '';
        try { media.deleteMedium('speech'); } catch (e) { caught = e.name; }
        console.log('missing=' + caught);
        media.mediaText = null;
        console.log('nulled=[' + media.mediaText + '],' + media.length);
        console.log('emptied=' + sheet.cssRules[0].cssText);
    </script></body></html>)");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "n="), std::string{"n=4"});
    // `all and <features>` drops the `all`; a NEGATED one keeps it, because
    // dropping it there would invert the query.
    CHECK_EQ(logged(page, "c0="), std::string{"c0=(color)"});
    CHECK_EQ(logged(page, "t0="), std::string{"t0=@media (color) {\n  #a { color: red; }\n}"});
    CHECK_EQ(logged(page, "t1="), std::string{"t1=@media  {\n}"});
    CHECK_EQ(logged(page, "t2="), std::string{"t2=@media not all and (color) {\n}"});
    // A feature written twice is KEPT twice: de-duplicating is an open CSSWG
    // issue and the suite asserts the author's list survives.
    CHECK_EQ(logged(page, "t3="),
             std::string{"t3=@media screen and (max-width: 23px) and (max-width: 45px) {\n}"});
    CHECK_EQ(logged(page, "m="), std::string{"m=1,(color),undefined,null,true"});
    CHECK_EQ(logged(page, "app="), std::string{"app=(color), print,2"});
    CHECK_EQ(logged(page, "del="), std::string{"del=(color),(color)"});
    CHECK_EQ(logged(page, "missing="), std::string{"missing=NotFoundError"});
    CHECK_EQ(logged(page, "nulled="), std::string{"nulled=[],0"});
    CHECK_EQ(logged(page, "emptied="), std::string{"emptied=@media  {\n  #a { color: red; }\n}"});
}

// A SHEET HAS A MediaList TOO, and it is the same interface over the same kind
// of list - `css/cssom/medialist-interfaces-001.html` drives the `<style>`'s
// `media` attribute through exactly these four steps.
void test_the_media_list_of_a_sheet() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style id=s media=all>.a { color: red }</style></head>
    <body><script>
        const sheet = document.getElementById('s').sheet;
        const list = sheet.media;
        console.log('m0=' + list.mediaText + ',' + list.length);
        list.appendMedium('screen');
        console.log('m1=' + list.mediaText);
        list.deleteMedium('all');
        console.log('m2=' + list.mediaText);
        sheet.media = 'print, Screen and (Min-Width: 10px)';
        console.log('m3=' + sheet.media.mediaText + ',' + (sheet.media === list));
    </script></body></html>)");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "m0="), std::string{"m0=all,1"});
    CHECK_EQ(logged(page, "m1="), std::string{"m1=all, screen"});
    CHECK_EQ(logged(page, "m2="), std::string{"m2=screen"});
    // A feature NAME folds and a media type folds; a feature's VALUE does not,
    // being a string, a url() or a number with a unit rather than an identifier.
    CHECK_EQ(logged(page, "m3="), std::string{"m3=print, screen and (min-width: 10px),true"});
}

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

// EVERY RULE THE AUTHOR WROTE, AND ITS TYPE.
//
// The CSSOM was built from `style::css::parse_stylesheet`, whose `stylesheet`
// models the three shapes the CASCADE needs - a qualified rule, `@media`, a
// `@font-face` - and discards every other at-rule outright. So an `@import`, an
// `@namespace`, a `@page` or a `@keyframes` was not a rule with less in it, it
// was absent, and the damage was not the missing rule: `cssRules[0]` was the
// WRONG rule and every index after it was off by one.
// `css/cssom/cssom-ruleTypeAndOrder.html` asserts exactly that indexing over
// seven sheets, and `rule-restrictions.html` asserts two of the types by number.
//
// A qualified rule with an EMPTY block was dropped by the same front end, which
// this file used to record as a deviation. It is not one any more: the sheet is
// split into rules by a byte scanner and each span goes through the same
// `parse_one_rule` that `insertRule` uses, and that function already split at
// the brace for exactly this reason. `@media all { * {} }` is the setup of
// every fixture in `css/cssom/CSSGroupingRule-*.html`.
void test_every_rule_a_sheet_carries() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style>
        @namespace svg "http://www.w3.org/2000/svg";
        @import url("main.css");
        @page :left { margin: 1px; }
        div { }
        @media print { * {} }
        @keyframes spin { from { opacity: 0 } to { opacity: 1 } }
    </style></head><body><script>
        const rules = document.styleSheets[0].cssRules;
        console.log('count=' + rules.length);
        let types = [];
        for (let i = 0; i < rules.length; i++) { types.push(rules[i].type); }
        console.log('types=' + types.join(','));
        console.log('ns=' + rules[0].cssText);
        console.log('import=' + rules[1].cssText);
        console.log('page=' + rules[2].cssText);
        console.log('empty=' + rules[3].cssText);
        console.log('media=' + rules[4].cssRules.length + '|' + rules[4].cssRules[0].cssText);
        console.log('frames=' + rules[5].cssRules.length + '|' + rules[5].cssRules[0].cssText);
        console.log('is=' + (rules[4] instanceof CSSMediaRule) + ',' +
                    (rules[0] instanceof CSSNamespaceRule) + ',' +
                    (rules[5].cssRules[0] instanceof CSSKeyframeRule));
    </script></body></html>)");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "count="), std::string{"count=6"});
    // CSSRule.NAMESPACE_RULE, IMPORT_RULE, PAGE_RULE, STYLE_RULE, MEDIA_RULE
    // and KEYFRAMES_RULE - in the order the author wrote them.
    CHECK_EQ(logged(page, "types="), std::string{"types=10,3,6,1,4,7"});
    // AN AT-RULE WITH NO BLOCK KEEPS THE AUTHOR'S BYTES, because there is
    // nothing to reconstruct from and an invented prelude would be a claim
    // about a rule nobody parsed.
    CHECK_EQ(logged(page, "ns="), std::string{"ns=@namespace svg \"http://www.w3.org/2000/svg\";"});
    CHECK_EQ(logged(page, "import="), std::string{"import=@import url(\"main.css\");"});
    // `@page`'s block IS declarations, so it serialises like any other block,
    // and its prelude is the page selector.
    CHECK_EQ(logged(page, "page="), std::string{"page=@page :left { margin: 1px; }"});
    CHECK_EQ(logged(page, "empty="), std::string{"empty=div { }"});
    CHECK_EQ(logged(page, "media="), std::string{"media=1|* { }"});
    // A `<keyframe-selector>` is not a selector, so a keyframe's prelude is its
    // keyText and `from` keeps the spelling the author used.
    CHECK_EQ(logged(page, "frames="), std::string{"frames=2|from { opacity: 0; }"});
    CHECK_EQ(logged(page, "is="), std::string{"is=true,true,true"});
}

// THE PIECES OF A PRELUDE, AND THE `.style` OF EVERY RULE THAT HAS ONE.
//
// `.style` lived on CSSStyleRule alone, and CSSOM gives one to five rules.
// `css/cssom/property-accessors.html` reaches straight for
// `document.styleSheets[0].cssRules[0].style` where rule zero is a `@font-face`
// and all nine of its subtests died on `getPropertyValue is undefined`, before
// they had asked anything about a property.
//
// `@import` and `@namespace` have no block, so their prelude IS the rule and
// the interface reports its pieces one at a time. Neither can be answered from
// the author's bytes: `url(a.css)` and `"a.css"` are the same URL and CSSOM
// serialises both as `url("a.css")`.
void test_the_at_rules_that_are_all_prelude() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style>
        @namespace svg "http://servo";
        @namespace "http://servo1";
        @import url(a.css);
        @import "b.css" supports((display: flex) or (display: block)) screen;
        @font-face { font-family: Foo; }
        @page :Left { margin-top: 1px }
    </style></head><body><script>
        const rules = document.styleSheets[0].cssRules;
        console.log('ns=' + rules[0].prefix + '|' + rules[0].namespaceURI + '|' +
                    rules[0].cssText);
        console.log('default=' + rules[1].prefix + '|' + rules[1].cssText);
        console.log('import=' + rules[2].href + '|' + rules[2].cssText + '|' +
                    rules[2].supportsText + '|' + rules[2].styleSheet);
        console.log('supports=' + rules[3].supportsText + '|' + rules[3].media.mediaText +
                    '|' + rules[3].cssText);
        console.log('face=' + rules[4].style.getPropertyValue('font-family') + '|' +
                    rules[4].style.length);
        console.log('page=' + rules[5].selectorText + '|' + rules[5].style.marginTop);
        rules[5].selectorText = 'named:First';
        console.log('renamed=' + rules[5].selectorText);
        // `named :first` is not two selectors, it is a parse failure - a
        // `<page-selector>` has no whitespace in it anywhere - and CSSOM says a
        // refused selector leaves the rule alone.
        rules[5].selectorText = 'named :first';
        console.log('refused=' + rules[5].selectorText);
        rules[5].selectorText = ':notapagepseudo';
        console.log('bogus=' + rules[5].selectorText);
    </script></body></html>)");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "ns="), std::string{"ns=svg|http://servo|"
                                              "@namespace svg url(\"http://servo\");"});
    // A DEFAULT namespace has no prefix, and CSSOM reports that as the empty
    // string rather than as null.
    CHECK_EQ(logged(page, "default="), std::string{"default=|@namespace url(\"http://servo1\");"});
    // `styleSheet` is null because nothing here fetches an `@import`, which a
    // page must be able to find out rather than be handed an empty sheet that
    // claims the import succeeded.
    CHECK_EQ(logged(page, "import="),
             std::string{"import=a.css|@import url(\"a.css\");|null|null"});
    CHECK_EQ(logged(page, "supports="),
             std::string{"supports=(display: flex) or (display: block)|screen|"
                         "@import url(\"b.css\") supports((display: flex) or (display: block)) "
                         "screen;"});
    CHECK_EQ(logged(page, "face="), std::string{"face=Foo|1"});
    // A pseudo-page is lowercased and a page NAME keeps the author's case.
    CHECK_EQ(logged(page, "page="), std::string{"page=:left|1px"});
    CHECK_EQ(logged(page, "renamed="), std::string{"renamed=named:first"});
    CHECK_EQ(logged(page, "refused="), std::string{"refused=named:first"});
    CHECK_EQ(logged(page, "bogus="), std::string{"bogus=named:first"});
}

// WHAT MAY PRECEDE WHAT, and the index that was never given.
//
// CSSOM 6.3.3 steps 4 and 5 answer two different questions. Step 4 is about the
// POSITION - an `@import` may only go where everything before it is another
// `@import`, a `@namespace` may also follow those, and everything else may only
// go after all of them. Step 5 is about the WHOLE LIST: a `@namespace` may not
// be added to a sheet that already has a style rule in it at all, wherever the
// insertion point is, because it would change what the existing selectors mean.
// `css/cssom/at-namespace.html` is one assertion of exactly that, and
// `delete-namespace-rule-when-child-rule-exists.html` is its mirror.
//
// And `insertRule(text)` and `insertRule(text, undefined)` are the SAME CALL.
// `index` is an optional unsigned long defaulting to 0, and Web IDL says an
// `undefined` passed for one means the default was not supplied - it is not
// `ToNumber(undefined)`, which is NaN and was answering IndexSizeError for a
// perfectly ordinary insertion.
void test_where_a_rule_may_be_inserted() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style>
        @import url("a.css");
        @namespace svg url(http://servo);
    </style></head><body><script>
        const sheet = document.styleSheets[0];
        const shout = (name, fn) => {
            let caught = 'none';
            try { fn(); } catch (e) { caught = e.name; }
            console.log(name + '=' + caught + ',' + sheet.cssRules.length);
        };
        // A style rule may not go before the @import or the @namespace...
        shout('early', () => sheet.insertRule('p { color: green }'));
        // ...and at the end it is fine, with no index given at all.
        shout('late', () => sheet.insertRule('p { color: green }', 2));
        // A @namespace may follow an @import; a second one may not now that
        // there is a style rule in the list.
        shout('nsnow', () => sheet.insertRule('@namespace foo url(http://x);', 1));
        // Nor may the @namespace be deleted while that style rule is there.
        shout('undelete', () => sheet.deleteRule(1));
        console.log('order=' + sheet.cssRules[0].type + ',' + sheet.cssRules[1].type + ',' +
                    sheet.cssRules[2].type);
    </script></body></html>)");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "early="), std::string{"early=HierarchyRequestError,2"});
    CHECK_EQ(logged(page, "late="), std::string{"late=none,3"});
    CHECK_EQ(logged(page, "nsnow="), std::string{"nsnow=InvalidStateError,3"});
    CHECK_EQ(logged(page, "undelete="), std::string{"undelete=InvalidStateError,3"});
    CHECK_EQ(logged(page, "order="), std::string{"order=3,10,1"});
}

void test_an_omitted_index_is_zero() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style>
        nosuchelement { color: red; }
    </style></head><body><script>
        const sheet = document.styleSheets[0];
        sheet.insertRule('p { color: green; }');
        console.log('omitted=' + sheet.cssRules.length + '|' + sheet.cssRules[0].cssText);
        sheet.insertRule('p { color: yellow; }', undefined);
        console.log('explicit=' + sheet.cssRules.length + '|' + sheet.cssRules[0].cssText);
        sheet.insertRule('@media print { p {} }', undefined);
        sheet.cssRules[0].insertRule('b {}', undefined);
        console.log('group=' + sheet.cssRules[0].cssRules.length);
    </script></body></html>)");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "omitted="), std::string{"omitted=2|p { color: green; }"});
    CHECK_EQ(logged(page, "explicit="), std::string{"explicit=3|p { color: yellow; }"});
    CHECK_EQ(logged(page, "group="), std::string{"group=2"});
}

// A MEDIUM IS ONE QUERY, AND DELETING ONE DELETES ALL OF THEM.
//
// Three separate readings of CSSOM 6.5's three-line methods, each of which the
// suite has a file for. `appendMedium` parses A media query, singular, so a
// comma-separated argument parses to null and the call is a no-op rather than
// two appends or one query called `screen, print`. `deleteMedium` removes EVERY
// query that matches, because a list may hold the same one twice and removing
// the first leaves behind the one the page just asked to be rid of. And its
// argument is REQUIRED, so calling it with none is a TypeError and not a
// NotFoundError about the empty string.
void test_the_three_readings_of_a_media_list() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style>
        @media screen, print, screen { * {} }
    </style></head><body><script>
        const media = document.styleSheets[0].cssRules[0].media;
        console.log('start=' + media.length + '|' + media.mediaText);
        media.appendMedium('speech, tv');
        console.log('comma=' + media.length + '|' + media.mediaText);
        media.appendMedium('print');
        console.log('dup=' + media.length);
        media.deleteMedium('screen');
        console.log('all=' + media.length + '|' + media.mediaText);
        let caught = 'none';
        try { media.deleteMedium(); } catch (e) { caught = e.name; }
        console.log('bare=' + caught);
        caught = 'none';
        try { media.deleteMedium('nosuchmedium'); } catch (e) { caught = e.name; }
        console.log('absent=' + caught);
    </script></body></html>)");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "start="), std::string{"start=3|screen, print, screen"});
    CHECK_EQ(logged(page, "comma="), std::string{"comma=3|screen, print, screen"});
    CHECK_EQ(logged(page, "dup="), std::string{"dup=3"});
    CHECK_EQ(logged(page, "all="), std::string{"all=1|print"});
    CHECK_EQ(logged(page, "bare="), std::string{"bare=TypeError"});
    // The one place in the CSSOM where deleting something absent is an error.
    CHECK_EQ(logged(page, "absent="), std::string{"absent=NotFoundError"});
}

} // namespace

int main() {
    test_the_list_and_the_rules();
    test_a_style_rule();
    test_insert_and_delete();
    test_selector_text_is_parsed_and_escaped();
    test_a_grouping_rule_inserts_and_deletes();
    test_media_rules_and_their_lists();
    test_the_media_list_of_a_sheet();
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
    test_every_rule_a_sheet_carries();
    test_the_at_rules_that_are_all_prelude();
    test_where_a_rule_may_be_inserted();
    test_an_omitted_index_is_zero();
    test_the_three_readings_of_a_media_list();
    REPORT("cssom");
}
