// `querySelector` and friends, on the Selectors engine rather than beside it.
//
// This file exists because there were TWO MATCHERS. `lib/Style/css/selector.cpp`
// compiled the whole grammar for the cascade - combinators, attribute selectors,
// `:not`, `:is`, `:nth-child`, the sibling forms - and `dom_bindings::query` was a
// separate hand-rolled one that gave up on any selector containing a space or a
// `>`. Script reached the weaker one, so `document.querySelector('ul > li')`
// answered null on a document with a `<li>` in a `<ul>`, and `el.matches(s)` and
// `[...root.querySelectorAll(s)].includes(el)` could disagree with each other.
//
// Every case below is a selector the old matcher got wrong, a rule of the DOM
// specification about which method throws, or a guard on the one thing that must
// NOT happen: `querySelector` throwing SyntaxError for a valid selector this
// engine merely cannot match. `::before` and `:focus-visible` are real CSS;
// refusing to answer is a missing answer, and throwing is a wrong one.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <string>
#include <vector>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

// The document every case below runs against. Deliberately shaped so that a
// matcher without combinators cannot fake its way through: two `<p>`s that differ
// only by their parent, siblings that differ only by position, and an attribute
// that appears on one element of a tag that appears three times.
constexpr const char * page_html = R"(<html><body>
<div id=outer class="box wide">
  <p id=p1 class=lead data-kind="a">one</p>
  <p id=p2 data-kind="ab">two</p>
  <span id=s1>span</span>
</div>
<ul id=list>
  <li id=li1 class=item>first</li>
  <li id=li2 class="item chosen">second</li>
  <li id=li3 class=item>third</li>
</ul>
<p id=p3>outside</p>
</body></html>)";

// A second document, because `:lang()` and `:dir()` are the only selectors here
// that ask about an ANCESTOR's attribute rather than about the subject. Shaped so
// that every rule of HTML §3.2.6 has an element that turns on it: an inherited
// language, an overriding one, an `lang=""` that means "unknown" rather than
// "keep looking", an `xml:lang` that must NOT count in an HTML document, and a
// `Content-Language` pragma that the root's own `lang` beats.
constexpr const char * lang_html = R"(<html lang="en-GB"><head>
<meta http-equiv="Content-Language" content="ko">
</head><body>
<div id=a>a</div>
<div id=b lang="de">b<div id=c>c</div><div id=d lang="">d</div></div>
<div id=e lang="de-CH-1901">e</div>
<div id=f xml:lang="ko">f</div>
<div id=g dir=rtl><span id=h>h</span></div>
<div id=i dir=auto>&#1488;</div>
<div id=j dir=auto>hello</div>
</body></html>)";

// The two helpers every page below needs, appended to whichever document a case
// runs against so that no case has to carry them.
constexpr const char * helpers = R"(<script>
function ids(selector) {
  try {
    return [].map.call(document.querySelectorAll(selector), function (e) { return e.id; })
             .join(',');
  } catch (e) {
    return 'threw:' + e.name;
  }
}
function one(selector) {
  try {
    const found = document.querySelector(selector);
    return found === null ? 'null' : found.id;
  } catch (e) {
    return 'threw:' + e.name;
  }
}
</script>)";

// Run one expression against a page and answer what it logged. A fresh page
// per case: a selector that mutates nothing cannot affect the next one, but a
// page that failed to load would otherwise report every case as the same silence.
[[nodiscard]] std::string answer_in(const std::string & document_html,
                                    const std::string & expression) {
    browser page{browser_options{400, 300}};
    std::string html{document_html};
    const std::string tail =
        std::string{helpers} + "<script>console.log(String(" + expression + "));</script>";
    html.insert(html.find("</body>"), tail);
    page.load_html(html);
    const std::vector<std::string> & logged = page.bindings().console_output();
    if (logged.empty()) { return "<nothing logged: " + page.script_error() + ">"; }
    return logged.back();
}

void is_in(const std::string & document_html, const std::string & expression,
           const std::string & expected) {
    const std::string got = answer_in(document_html, expression);
    CHECK_EQ(got, expected);
    if (got != expected) { std::printf("    %s\n", expression.c_str()); }
}

void is(const std::string & expression, const std::string & expected) {
    is_in(page_html, expression, expected);
}

// --- what the old matcher could already do --------------------------------

void test_the_compound_selectors_still_work() {
    is("ids('p')", "p1,p2,p3");
    is("ids('#p2')", "p2");
    is("ids('.item')", "li1,li2,li3");
    is("ids('.item.chosen')", "li2");
    is("ids('li.chosen')", "li2");
    // The universal selector reaches <html>, <head>, <body> and both <script>s as
    // well, and how many of those a tree builder makes is its own business - so
    // this asks about the ones the document names.
    is("ids('*').split(',').filter(function (s) { return s; }).join(',')",
       "outer,p1,p2,s1,list,li1,li2,li3,p3");
    is("ids('p, span')", "p1,p2,s1,p3");
    is("one('.item')", "li1");
    is("one('.nothing-here')", "null");
}

// --- the combinators, none of which matched anything before ----------------

void test_the_four_combinators() {
    is("ids('div p')", "p1,p2");         // descendant
    is("ids('ul > li')", "li1,li2,li3"); // child
    is("ids('body > p')", "p3");         // ...and the child combinator EXCLUDES p1/p2
    is("ids('#li1 + li')", "li2");       // next sibling
    is("ids('#li1 ~ li')", "li2,li3");   // subsequent siblings
    is("ids('div.box p.lead')", "p1");
    is("ids('body div p')", "p1,p2");
    // A descendant chain that does not hold anywhere.
    is("ids('span p')", "");
}

// --- attribute selectors, `:not`, `:is` and the structural pseudo-classes ---

void test_the_rest_of_the_grammar() {
    is("ids('[data-kind]')", "p1,p2");
    is("ids('[data-kind=a]')", "p1");
    is("ids('[data-kind^=a]')", "p1,p2");
    is("ids('[data-kind$=b]')", "p2");
    is("ids('[data-kind*=b]')", "p2");
    is("ids('p:not(.lead)')", "p2,p3");
    is("ids('li:not(.chosen)')", "li1,li3");
    is("ids('li:first-child')", "li1");
    is("ids('li:last-child')", "li3");
    is("ids('li:nth-child(2)')", "li2");
    is("ids('li:nth-child(odd)')", "li1,li3");
    is("ids(':is(#p1, #li2)')", "p1,li2");
    is("ids('ul li:nth-of-type(3)')", "li3");
}

// `:heading` and `:heading(<integer>#)`, Selectors 5 (css/selectors/heading.html,
// parsing/parse-heading.html): h1-h6 by level, integers only in the list.
void test_heading() {
    is_in("<html><body><h1 id=a></h1><h2 id=b></h2><section><h6 id=c></h6></section>"
          "<h7 id=d></h7><p id=e role=heading aria-level=1></p>"
          "<style>h2:heading(2) { color: rgb(1, 2, 3) }</style></body></html>",
          "(function () { var q = function (s) { return Array.from(document.querySelectorAll(s))"
          ".map(function (e) { return e.id; }).join(); };"
          " var bad = function (s) { try { document.querySelector(s); return 'ok'; }"
          " catch (e) { return e.name; } };"
          " return [q(':heading'), q(':heading(1)'), q(':heading(0, 2, 6)'), q(':heading(7)'),"
          " q('h1:heading(2)'), bad(':heading()'), bad(':heading(1.0)'), bad(':heading(2n)'),"
          " bad(':heading(-1)'), document.styleSheets[0].cssRules[0].selectorText,"
          " getComputedStyle(document.getElementById('b')).color].join('|'); })()",
          "a,b,c|a|b,c||ok|SyntaxError|SyntaxError|SyntaxError|ok|h2:heading(2)|rgb(1, 2, 3)");
}

// --- every spelling of An+B ------------------------------------------------
//
// The tokenizer has no An+B token: `4n-1` is one dimension whose unit is `n-1`,
// `-n+3` an ident and a signed number, `2n + 1` five tokens. The reader joins
// the spelling back together, and each of these is a shape that broke one way
// of reading the tokens.
void test_an_plus_b_spellings() {
    is("ids('li:nth-child(4n-1)')", "li3");
    is("ids('li:nth-child(4n - 1)')", "li3");
    is("ids('li:nth-child(2n+1)')", "li1,li3");
    is("ids('li:nth-child(2n + 1)')", "li1,li3");
    is("ids('li:nth-child(2n +1)')", "li1,li3");
    is("ids('li:nth-child(2N+1)')", "li1,li3");
    is("ids('li:nth-child(-n+2)')", "li1,li2");
    is("ids('li:nth-child(-n + 2)')", "li1,li2");
    is("ids('li:nth-child(n)')", "li1,li2,li3");
    is("ids('li:nth-child(+n)')", "li1,li2,li3");
    is("ids('li:nth-child(n+2)')", "li2,li3");
    is("ids('li:nth-child(0n+2)')", "li2");
    is("ids('li:nth-child(-2n+3)')", "li1,li3");
    is("ids('li:nth-child( even )')", "li2");
    is("ids('li:nth-child(EVEN)')", "li2");
    is("ids('li:nth-last-child(-n+1)')", "li3");
    is("ids('li:nth-child(+3)')", "li3");
    is("ids('li:nth-child(-1)')", "");
    // Whitespace is allowed only around the sign between `An` and `B`.
    is("one('li:nth-child(2 n)')", "threw:SyntaxError");
    is("one('li:nth-child(- n)')", "threw:SyntaxError");
    is("one('li:nth-child(2n+)')", "threw:SyntaxError");
    is("one('li:nth-child(n+1+1)')", "threw:SyntaxError");
    is("one('li:nth-child()')", "threw:SyntaxError");
    is("one('li:nth-child(2n+1, 3)')", "threw:SyntaxError");
    is("one('li:nth-child(1.5)')", "threw:SyntaxError");
}

// --- `:scope` and `:has()` -----------------------------------------------------

void test_scope_and_has() {
    // `:scope` is the element a scoped query runs from, and `:root` when there is
    // no such element - a whole-document query, or a stylesheet.
    is("ids(':scope')", "");
    is("document.querySelector(':scope').tagName", "HTML");
    // `:root` is the DOCUMENT's root element and nothing else: not a fragment's
    // top-level children, not a detached element.
    is("document.querySelector(':root').tagName", "HTML");
    is("(function () { var f = document.createDocumentFragment();"
       " f.appendChild(document.createElement('div'));"
       " return f.querySelectorAll(':root').length; })()",
       "0");
    is("document.createElement('p').matches(':root')", "false");
    is("[].map.call(document.getElementById('outer').querySelectorAll(':scope > p'), "
       "function (e) { return e.id; }).join(',')",
       "p1,p2");
    is("document.getElementById('outer').querySelectorAll(':scope > li').length", "0");
    is("document.getElementById('outer').querySelectorAll(':scope').length", "0");
    is("document.getElementById('p1').matches(':scope')", "true");
    is("document.getElementById('p1').matches('div > :scope')", "true");
    is("document.getElementById('p1').matches('ul > :scope')", "false");
    is("document.getElementById('p1').closest(':scope').id", "p1");
    // `:has()` takes a relative selector list: a child, a descendant, a sibling.
    is("ids('div:has(> .lead)')", "outer");
    is("ids('div:has(.lead)')", "outer");
    is("ids('body > :has(.chosen)')", "list");
    is("ids('li:has(+ .chosen)')", "li1");
    is("ids('li:has(~ li)')", "li1,li2");
    is("ids('p:has(+ span)')", "p2");
    is("ids(':has(> #p1, > #li3)')", "outer,list");
    is("ids('div:has(> span):has(> p)')", "outer");
    is("ids('div:has(> ul)')", "");
    is("ids('li:not(:has(~ li))')", "li3");
    // An ancestor beneath body must not replace the original subject as :scope.
    is("document.getElementById('p1').closest('body > :scope')", "null");
    is("document.getElementById('p1').closest('div > :scope').id", "p1");
    is("one(':has()')", "threw:SyntaxError");
    is("one(':has(> )')", "threw:SyntaxError");
    is("one('div:has(:has(p))')", "threw:SyntaxError");
    // `:has()` in a stylesheet restyles like any other selector.
    is_in("<html><head><style>div:has(> .x) { color: rgb(1, 2, 3); }</style></head>"
          "<body><div id=a><span class=x></span></div><div id=b></div></body></html>",
          "getComputedStyle(document.getElementById('a')).color + '/' + "
          "getComputedStyle(document.getElementById('b')).color",
          "rgb(1, 2, 3)/rgb(0, 0, 0)");
}

// --- the subtree forms: an element's own query, matches, closest ------------

void test_scoped_queries_and_matches() {
    // A search rooted at an element is over its DESCENDANTS and never itself.
    is("[].map.call(document.getElementById('list').querySelectorAll('li'), "
       "function (e) { return e.id; }).join(',')",
       "li1,li2,li3");
    is("document.getElementById('list').querySelectorAll('ul').length", "0");
    // ...but the selector is still matched against the WHOLE tree above the root:
    // `body li` holds of these three because the body is an ancestor of the list.
    is("document.getElementById('list').querySelectorAll('body li').length", "3");
    is("document.getElementById('p1').matches('div > p.lead')", "true");
    is("document.getElementById('p1').matches('body > p')", "false");
    is("document.getElementById('li2').closest('ul').id", "list");
    is("document.getElementById('li2').closest('.box')", "null");
    is("document.getElementById('li2').closest('ul, li').id", "li2");
    is("(function () { var el = document.createElement('button');"
       " return el.closest('button') === el && el.closest('body') === null; })()",
       "true");
    is("(function () { var host = document.createElement('div');"
       " var root = host.attachShadow({mode: 'open'});"
       " var button = document.createElement('button'); root.appendChild(button);"
       " return button.closest('button') === button && button.closest('div') === null; })()",
       "true");
    is("(function () { try { document.getElementById('li2').closest('['); }"
       " catch (e) { return e.name; } })()",
       "SyntaxError");
    // matches() and querySelectorAll() are the same matcher, which is the whole
    // point of defining one in terms of the other.
    is("[].every.call(document.querySelectorAll('div p'), "
       "function (e) { return e.matches('div p'); })",
       "true");
}

// --- what throws, and much more importantly what does not ------------------

void test_syntax_errors_and_the_selectors_that_are_merely_unsupported() {
    // Not selectors at all: the DOM says SyntaxError, a DOMException.
    is("one('')", "threw:SyntaxError");
    is("one('div >')", "threw:SyntaxError");
    is("one('p,,span')", "threw:SyntaxError");
    is("one('>p')", "threw:SyntaxError");
    is("one(':')", "threw:SyntaxError");
    is("one('#0d6efd')", "threw:SyntaxError");
    is("one('4')", "threw:SyntaxError");
    is("one('li:nth-child(fred)')", "threw:SyntaxError");
    // VALID CSS THIS ENGINE CANNOT MATCH. Every one of these must answer null
    // rather than throw: a page feature-detecting `:has()` gets "no match", which
    // is a missing answer, where a throw would be a wrong one.
    is("one('p::before')", "null");
    is("one('p:focus-visible')", "null");
    // A NAMESPACE PREFIX IS A SYNTAX ERROR HERE. Selectors API §2 gives
    // `querySelector` no namespace resolver, so `svg|rect` is undeclared in every
    // browser, and so is `[xlink|href]`. `*|` and `|` need no declaration.
    is("one('svg|rect')", "threw:SyntaxError");
    is("one('[xlink|href]')", "threw:SyntaxError");
    is("one(':not(ns|div)')", "threw:SyntaxError");
    is("ids('*|p')", "p1,p2,p3");
    is("ids('[*|data-kind=a]')", "p1");
    is("ids('[|data-kind=a]')", "p1");
    is("one('|p')", "null"); // every element the HTML parser makes has a namespace
    // Two combinators in a row are not one combinator.
    is("one('div ++ p')", "threw:SyntaxError");
    is("one('div ~~ p')", "threw:SyntaxError");
    is("one('div > > p')", "threw:SyntaxError");
    is("one('div >p')", "p1");
    // A `:lang()` this engine parses but whose argument is malformed is
    // unmatchable rather than a syntax error, on the same reading
    // `:nth-child(of S)` is refused under.
    is("one('p:lang()')", "null");
    is("one('p:lang(en,)')", "null");
    // ...and this document declares no language at all, so `:lang(en)` has
    // nothing to match - see test_lang_and_dir for the document that does.
    is("one(':lang(en)')", "null");
    // And an alternative that cannot be matched must not take its siblings with
    // it: `#p1, :has(a)` still finds #p1, exactly as a stylesheet would.
    is("one('#p1, li:has(a)')", "p1");
    // `:has-slotted` (csswg-drafts#10586): a valid pseudo-class this engine cannot
    // match - slot assignment lives in the shell - so it answers null, and its
    // malformed forms are syntax errors. Sibling combinators are legal in the
    // argument, child and descendant are not.
    is("one(':has-slotted')", "null");
    is("one(':has-slotted(bar)')", "null");
    is("one(':has-slotted(*)')", "null");
    is("one(':has-slotted(:not(foo))')", "null");
    is("one(':has-slotted(div + div)')", "null");
    is("one(':has-slotted(div:has(> span))')", "null");
    is("one(':not(:has-slotted(foo))')", "null");
    is("one('::has-slotted(foo)')", "threw:SyntaxError");
    is("one(':has-slotted()')", "threw:SyntaxError");
    is("one(':has-slotted(0)')", "threw:SyntaxError");
    is("one(':has-slotted(div > span)')", "threw:SyntaxError");
}

// A DETACHED ELEMENT IS MATCHED AGAINST ITSELF, and it was not.
//
// `engine::element_matches` entered depth 0 from `txn.root()` unconditionally.
// A `document.createElement("div")` is not among the root's children, so the
// sibling walk ran to the end and the subject came out as the LAST element
// child of `<html>` - `document.createElement("div").matches("div")` was
// answered about `<body>`. A silent wrong answer, and the one a
// `querySelector` inside a shadow tree runs into first.
void test_a_detached_element_matches_against_itself() {
    is("document.createElement('div').matches('div')", "true");
    is("document.createElement('div').matches('span')", "false");
    is("(function () { var e = document.createElement('div'); e.className = 'x';"
       " return e.matches('.x') + ',' + e.matches('div.x') + ',' + e.matches('#nope'); })()",
       // `div.x` is the SAME COMPOUND as `.x` with a tag on it, and the element
       // is a div carrying that class - so it matches. The expectation used to
       // say false, which was written to the behaviour of a `matches` that ran
       // a document query and could never find a detached element at all.
       "true,true,false");
    // A parentless subject is an only child, which is what the structural
    // pseudo-classes have to say about a node that is in no tree at all.
    is("document.createElement('p').matches(':only-child')", "true");
    is("document.createElement('p').matches(':first-child')", "true");
    // A DESCENDANT COMBINATOR CANNOT MATCH one: there is no ancestor.
    is("document.createElement('p').matches('body p')", "false");
    // And a detached SUBTREE still matches within itself.
    is("(function () {"
       " var box = document.createElement('div');"
       " box.innerHTML = '<span class=inner>x</span>';"
       " var inner = box.firstChild;"
       " return inner.matches('span') + ',' + inner.matches('div > span') + ',' +"
       "        (box.querySelector('.inner') === inner); })()",
       "true,true,true");
}

// --- `:lang()` and `:dir()` -------------------------------------------------
//
// Both used to be unmatchable, which the ten
// `html/dom/elements/global-attributes/the-lang-attribute-*.html` tests detect
// by putting `:lang(xx)` on an element they then expect to be `display: none`.
void test_lang_and_dir() {
    // An INHERITED language. #a declares none, so the root's `en-GB` is its.
    is_in(lang_html, "ids('div:lang(en-GB)')", "a,f,g,i,j");
    // ...and RFC 4647 extended filtering, not string equality: the range `en`
    // matches the language `en-GB`, which is the whole reason `:lang()` is not
    // spelled `[lang^=en]`.
    is_in(lang_html, "ids('div:lang(en)')", "a,f,g,i,j");
    is_in(lang_html, "ids('div:lang(de)')", "b,c,e");
    is_in(lang_html, "ids('div:lang(de-CH)')", "e");
    // A wildcard subtag is skipped over, and a range of `*` is any language at
    // all - which #d, whose `lang=""` means "unknown", is not.
    is_in(lang_html, "ids('div:lang(*-CH)')", "e");
    is_in(lang_html, "ids('div:lang(*)')", "a,b,c,e,f,g,i,j");
    // `lang=""` STOPS the search rather than being skipped past: #d is inside a
    // `lang="de"` and is still not German.
    is_in(lang_html, "one('#d:lang(de)')", "null");
    // `xml:lang` DOES NOT COUNT in an HTML document - the parser's foreign
    // attribute adjustment does not run on an HTML element, so #f holds an
    // attribute literally named `xml:lang` and inherits `en-GB` regardless.
    // `the-lang-attribute-002.html` is exactly this, and asserts the MISS.
    is_in(lang_html, "one('#f:lang(ko)')", "null");
    // The `Content-Language` pragma is the DOCUMENT DEFAULT, so the root's own
    // `lang` beats it. Above it loses; here, with no `lang` anywhere, it wins.
    is_in(lang_html, "ids('div:lang(ko)')", "");
    is_in("<html><head><meta http-equiv='Content-Language' content='ko'></head>"
          "<body><div id=a>a</div></body></html>",
          "ids('div:lang(ko)')", "a");
    // A pragma naming more than one language sets no default at all.
    is_in("<html><head><meta http-equiv='Content-Language' content='ko, en'></head>"
          "<body><div id=a>a</div></body></html>",
          "ids('div:lang(ko)')", "");
    // ...and an empty `lang` on the root beats the pragma, which is what
    // `the-lang-attribute-010.html` asserts.
    is_in("<html lang=''><head><meta http-equiv='Content-Language' content='ko'></head>"
          "<body><div id=a>a</div></body></html>",
          "ids('div:lang(ko)')", "");

    // `:dir()`. An explicit `dir` is inherited by descendants...
    is_in(lang_html, "one('#h:dir(rtl)')", "h");
    is_in(lang_html, "one('#h:dir(ltr)')", "null");
    // ...the default with no `dir` anywhere is left-to-right...
    is_in(lang_html, "one('#a:dir(ltr)')", "a");
    // ...and `dir=auto` reads the first STRONG character of the subtree, so a
    // Hebrew aleph makes #i right-to-left and `hello` leaves #j left-to-right.
    is_in(lang_html, "one('#i:dir(rtl)')", "i");
    is_in(lang_html, "one('#j:dir(ltr)')", "j");
    // A direction this engine does not know is valid and simply never matches -
    // Selectors 4 §7.1 says so rather than making it a syntax error.
    is_in(lang_html, "one('#a:dir(sideways)')", "null");
}

} // namespace

// --- `:defined`, the honest subset the style matcher can see ----------------
//
// A built-in element and a non-HTML element are always defined; a name that is a
// potential custom element (autonomous) or a customized built-in `is=` naming one
// is undefined until the shell's registry upgrades it, which this layer cannot
// see. Every element the parser makes here is un-upgraded, so a custom name reads
// as :not(:defined) and everything else as :defined.
constexpr const char * defined_html = R"(<html><body>
<div id=d1>built-in</div>
<font-face id=ff>reserved, not a custom name</font-face>
<my-el id=me>autonomous custom name</my-el>
<a-a id=aa>another custom name</a-a>
<button id=cb is="my-button">customized built-in</button>
</body></html>)";

void test_defined() {
    // Built-ins and reserved SVG/MathML names are defined; `font-face` is on the
    // list HTML §4.13.2 forbids as a custom element name, so it is a plain element.
    is_in(defined_html, "document.getElementById('d1').matches(':defined')", "true");
    is_in(defined_html, "document.getElementById('ff').matches(':defined')", "true");
    // A potential custom element name is undefined until upgraded, and nothing
    // upgraded it here.
    is_in(defined_html, "document.getElementById('me').matches(':defined')", "false");
    is_in(defined_html, "document.getElementById('me').matches(':not(:defined)')", "true");
    is_in(defined_html, "document.getElementById('aa').matches(':defined')", "false");
    // A customized built-in whose `is=` names a custom element is undefined too.
    is_in(defined_html, "document.getElementById('cb').matches(':defined')", "false");
    // The query form agrees with matches(), and the built-ins carry the two helper
    // <script>s and <html>/<head>/<body> with them - so this asks about the ones
    // the page names rather than counting the tree builder's own elements.
    is_in(defined_html,
          "[].map.call(document.querySelectorAll(':defined'), function (e) { return e.id; })"
          ".filter(function (s) { return s; }).join(',')",
          "d1,ff");
    is_in(defined_html,
          "[].map.call(document.querySelectorAll(':not(:defined)'), function (e) "
          "{ return e.id; }).filter(function (s) { return s; }).join(',')",
          "me,aa,cb");
}

int main() {
    test_the_compound_selectors_still_work();
    test_the_four_combinators();
    test_the_rest_of_the_grammar();
    test_an_plus_b_spellings();
    test_heading();
    test_scope_and_has();
    test_scoped_queries_and_matches();
    test_a_detached_element_matches_against_itself();
    test_syntax_errors_and_the_selectors_that_are_merely_unsupported();
    test_lang_and_dir();
    test_defined();
    REPORT("selectors");
}
