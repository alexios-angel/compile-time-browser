// WHAT css/cssom ASKS OF THE SELECTOR AND VALUE SERIALISERS, one case per
// behaviour that moved on 2026-09-10.
//
// Beside cssom_wpt.cpp rather than in it: every case here is about how a
// selector or a declaration value is WRITTEN BACK - `selectorText` and
// `el.style.x` - and each names the `css/cssom` file it stands in for.
// `logged` is shared through cssom_probe.hpp.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include "cssom_probe.hpp"

#include <string>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser_test::logged;

namespace {

// invalid-pseudo-elements, CSSStyleRule-set-selectorText: a pseudo-class or
// pseudo-element CSS has not defined, and a functional one with no argument,
// is a syntax error - the rule is not in the sheet, and the setter does nothing.
void test_undefined_pseudos_are_syntax_errors() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style>
        ::part {} ::slotted {} ::highlight {} ::gibberish {} :gibberish {} .style0 {}
        </style></head><body><script>
        const sheet = document.styleSheets[0];
        console.log('rules=' + sheet.cssRules.length + ',' + sheet.cssRules[0].selectorText);
        const rule = sheet.cssRules[0];
        const kept = [];
        for (const bad of ['::', ':::', '::gibberish', ':gibberish', '::part', ':']) {
            rule.selectorText = bad;
            kept.push(rule.selectorText);
        }
        console.log('kept=' + kept.join(','));
    </script></body></html>)");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "rules="), std::string{"rules=1,.style0"});
    CHECK_EQ(logged(page, "kept="),
             std::string{"kept=.style0,.style0,.style0,.style0,.style0,.style0"});
}

// CSSStyleRule-set-selectorText, serialize-namespaced-type-selectors: a
// pseudo-element is kept and written with two colons; a functional one whose
// argument the compiled form cannot hold falls back to the author's bytes; a
// pseudo-class the matcher does not model is still a selector.
void test_pseudo_elements_serialise_canonically() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style>.s {}</style></head><body><script>
        const rule = document.styleSheets[0].cssRules[0];
        const out = [];
        for (const sel of [':before', '::after', '*::before', 'e::first-line', 'a:hover::before',
                           '::part(x)', 'p:focus-visible', 'li:has(a)']) {
            rule.selectorText = sel;
            out.push(rule.selectorText);
        }
        console.log('pe=' + out.join('|'));
    </script></body></html>)");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "pe="),
             std::string{"pe=::before|::after|::before|e::first-line|a:hover::before|"
                         "::part(x)|p:focus-visible|li:has(a)"});
}

// serialize-namespaced-type-selectors, selectorSerialize: with no default
// namespace `*|e` is `e` and `*|*` is `*`, the null namespace keeps its `|`, a
// named prefix is written, and a type name goes through serialize-an-identifier
// - `\\` comes back escaped, with or without a prefix. The default-namespace
// half of that file needs the sheet's `@namespace` rules handed to the
// serialiser, which is the CSSOM's side of the change.
void test_namespace_prefixes_serialise_canonically() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><style>.s {}</style></head><body><script>
        const rule = document.styleSheets[0].cssRules[0];
        const out = [];
        for (const sel of ['*|e', '*|*', '|e', '|*.c', '*|e.c', '*|*#id', '\\\\', '*|\\\\',
                           '|\\\\', ':lang( j\\ a )', ':lang(ja)', '\\31 a']) {
            rule.selectorText = sel;
            out.push(rule.selectorText);
        }
        console.log('ns=' + out.join('|'));
    </script></body></html>)");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "ns="),
             std::string{"ns=e|*||e||*.c|e.c|#id|\\\\|\\\\||\\\\|:lang(j\\ a)|:lang(ja)|\\31 a"});
}

// serialize-values, cssom-fontfacerule: a number, a string and a URL are spelled
// CSSOM's way inside a value whose grammar the table does not model, and every
// other byte is the author's.
void test_freeform_values_respell_numbers_strings_and_urls() {
    browser page{browser_options{400, 200}};
    page.load_html(R"js(<html><body><div id=d></div><script>
        const d = document.getElementById('d');
        const read = (p, v) => { d.style.cssText = p + ': ' + v; return d.style[p]; };
        console.log('pos=' + read('backgroundPosition', '5% .5%') + '|' +
                    read('backgroundPosition', '5% -0px') + '|' +
                    read('backgroundPosition', '5% -.1em'));
        console.log('url=' + read('backgroundImage', 'url(http://localhost/)') + '|' +
                    read('backgroundImage', "url('http://localhost/')"));
    console.log('str=' + read('content', "'string'") + '|' + read('content', 'attr( |bar )'));
    console.log('num=' + read('transform', 'translate(1e3px, +5PX)') + '|' +
                read('opacity', '0.1234567') + '|' + read('width', '1e3px'));
    </script></body></html>)js");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "pos="), std::string{"pos=5% 0.5%|5% 0px|5% -0.1em"});
    CHECK_EQ(logged(page, "url="),
             std::string{"url=url(\"http://localhost/\")|url(\"http://localhost/\")"});
    CHECK_EQ(logged(page, "str="), std::string{"str=\"string\"|attr( |bar )"});
    CHECK_EQ(logged(page, "num="), std::string{"num=translate(1000px, 5px)|0.1234567|1000px"});
}

} // namespace

int main() {
    test_undefined_pseudos_are_syntax_errors();
    test_pseudo_elements_serialise_canonically();
    test_namespace_prefixes_serialise_canonically();
    test_freeform_values_respell_numbers_strings_and_urls();
    REPORT("style_serialisation_wpt");
}
