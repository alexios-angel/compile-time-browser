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
//
// This file is the RULES: CSSRuleList, CSSStyleRule, the grouping and media
// rules and their MediaLists, every rule type a sheet carries and where one may
// be inserted. The sheet objects, and the `set_author_styles_hook` case the
// paragraph above calls "the last case below", are cssom_sheets.cpp.
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
    // AN @namespace's URI IS SERIALIZED AS A URL, whichever of the two forms
    // the author wrote it in - CSSOM says "serialize a URL", and
    // `css/cssom/CSSNamespaceRule.html` asserts `@namespace svg
    // url("http://servo");` for a `url()` prelude and the identical shape for a
    // quoted one. This asserted the author's bytes back, which is what an
    // at-rule with no block does for everything EXCEPT its URL.
    CHECK_EQ(logged(page, "ns="),
             std::string{"ns=@namespace svg url(\"http://www.w3.org/2000/svg\");"});
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
    test_every_rule_a_sheet_carries();
    test_the_at_rules_that_are_all_prelude();
    test_where_a_rule_may_be_inserted();
    test_an_omitted_index_is_zero();
    test_the_three_readings_of_a_media_list();
    REPORT("cssom_rules");
}
