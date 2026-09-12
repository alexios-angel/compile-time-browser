// WHAT `html/dom` MEASURED AND WHAT WAS CHANGED FOR IT.
//
// One case per behaviour, each written the way the corpus writes it: a page
// script does the thing and reads back in the same statement, and the console
// carries the answer out. A case here is a WPT subtest family in miniature -
// the file name beside each says which - so a regression fails a unit test on
// the devbox rather than a suite on the shared box a day later.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <string>
#include <vector>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

constexpr const char * page_html = R"(<!DOCTYPE html>
<html><head><title>t</title></head><body>
<div id=host></div>
</body></html>)";

[[nodiscard]] std::string answer(const std::string & expression) {
    browser page{browser_options{400, 300}};
    std::string html{page_html};
    const std::string tail = "<script>try { console.log(String(" + expression +
                             ")); } catch (e) { console.log('threw:' + e.name); }</script>";
    html.insert(html.find("</body>"), tail);
    page.load_html(html);
    const std::vector<std::string> & logged = page.bindings().console_output();
    if (logged.empty()) { return "<nothing logged: " + page.script_error() + ">"; }
    return logged.back();
}

void is(const std::string & expression, const std::string & expected) {
    const std::string got = answer(expression);
    CHECK_EQ(got, expected);
    if (got != expected) { std::printf("    %s\n", expression.c_str()); }
}

// --- reflection-*.html ------------------------------------------------------

void test_legacy_null_to_empty_string_rows_write_nothing_for_null() {
    // "IDL set to null": [LegacyNullToEmptyString] on `bgColor` writes "",
    // and a plain DOMString row beside it still writes the four letters.
    is("(function () { var b = document.body; b.bgColor = null;"
       " return b.getAttribute('bgcolor'); })()",
       "");
    is("(function () { var f = document.createElement('font'); f.color = null;"
       " return f.getAttribute('color'); })()",
       "");
    is("(function () { var t = document.createElement('td'); t.abbr = null;"
       " return t.getAttribute('abbr'); })()",
       "null");
    // And `undefined` is not null: the same row writes nine letters for it.
    is("(function () { var b = document.body; b.bgColor = undefined;"
       " return b.getAttribute('bgcolor'); })()",
       "undefined");
}

void test_nonce_is_a_slot_in_front_of_the_attribute() {
    // reflection-metadata.html, `link.nonce: IDL set to ...`: setting the IDL
    // attribute changes what it reads back and NOT the content attribute.
    is("(function () { var l = document.createElement('link');"
       " l.setAttribute('nonce', 'a'); l.nonce = 'b';"
       " return l.nonce + '/' + l.getAttribute('nonce'); })()",
       "b/a");
    // A content attribute change reloads the slot.
    is("(function () { var l = document.createElement('link');"
       " l.nonce = 'b'; l.setAttribute('nonce', 'c'); return l.nonce; })()",
       "c");
}

void test_the_rows_the_element_tables_name_are_all_there() {
    // elements-grouping.js, -embedded.js and -forms.js name four rows the
    // table lacked; `button.value` is the one with a trap, because a button is
    // a control here and a control's own `value` accessor shadowed the row.
    is("(function () { var b = document.createElement('button');"
       " b.setAttribute('value', 'v'); var was = b.value; b.value = 'w';"
       " return was + ',' + b.getAttribute('value'); })()",
       "v,w");
    is("(function () { var h = document.createElement('hr'); h.width = 7;"
       " return typeof h.width + ':' + h.getAttribute('width'); })()",
       "string:7");
    is("(function () { var p = document.createElement('pre'); p.setAttribute('width', ' 12x');"
       " return typeof p.width + ':' + p.width; })()",
       "number:12");
    is("(function () { var o = document.createElement('object');"
       " o.data = 'http://site.example/x'; return o.data; })()",
       "http://site.example/x");
    // reflection-tabular, `td.rowSpan: setAttribute() to -36`: a clamped
    // unsigned long parses with the NON-NEGATIVE rules, so a negative fails
    // the parse and is the default 1, not the clamp's floor of 0.
    is("(function () { var t = document.createElement('td');"
       " t.setAttribute('rowspan', '-36'); var a = t.rowSpan;"
       " t.setAttribute('rowspan', '0'); return a + ',' + t.rowSpan; })()",
       "1,0");
    // reflection-forms: `select.autocomplete` and `textarea.autocomplete`
    // reflect as strings.
    is("(function () { var s = document.createElement('select'); s.autocomplete = 'off';"
       " return typeof s.autocomplete + ':' + s.getAttribute('autocomplete'); })()",
       "string:off");
    // `progress.max` is a double limited to positive numbers over HTML's
    // float rules: " 7" and "1.e2" parse, "\v7" and "-1" are the default 1,
    // and a non-positive IDL set leaves the attribute alone. A meter's rows
    // write the number's JavaScript string.
    is("(function () { var p = document.createElement('progress'); var seen = [typeof p.max, "
       "p.max];"
       " ['  7', '1.e2', '\v7', '-1', '.5', '1e-10'].forEach(function (t) {"
       " p.setAttribute('max', t); seen.push(p.max); });"
       " p.setAttribute('max', 'kept'); p.max = -1; seen.push(p.getAttribute('max'));"
       " p.max = 1e25; seen.push(p.getAttribute('max'));"
       " var m = document.createElement('meter'); m.low = -0; m.high = 1e-10;"
       " seen.push(m.getAttribute('low'), m.getAttribute('high'), m.optimum);"
       " return seen.join(); })()",
       "number,1,7,100,1,1,0.5,1e-10,kept,1e+25,0,1e-10,0");
    // `option.label` and `option.value` fall back to the option's text.
    is("(function () { var o = document.createElement('option'); o.textContent = ' a  b ';"
       " var seen = [o.label, o.value]; o.value = 'v'; o.label = 'l';"
       " seen.push(o.value, o.getAttribute('value'), o.label); return seen.join('|'); })()",
       "a b|a b|v|v|l");
    // `form.action` with no attribute is the document's URL, not "".
    is("(function () { var f = document.createElement('form');"
       " return f.action === document.URL; })()",
       "true");
}

// --- HTMLHyperlinkElementUtils ----------------------------------------------

void test_an_anchor_reports_the_parts_of_its_url() {
    // reflection.js's resolveUrl: `protocol + "//" + host + pathname + search
    // + hash` read off an <a>, which every URL-typed reflection subtest
    // compares against `el.href`. The two have to agree.
    is("(function () { var a = document.createElement('a');"
       " a.href = 'HTTP://Site.Example:8080/p/q?x=1#frag';"
       " return [a.protocol, a.host, a.hostname, a.port, a.pathname, a.search, a.hash,"
       " a.origin].join('|'); })()",
       "http:|site.example:8080|site.example|8080|/p/q|?x=1|#frag|http://site.example:8080");
    is("(function () { var a = document.createElement('a');"
       " a.href = 'http://site.example/'; "
       " return a.protocol + '//' + a.host + a.pathname + a.search + a.hash === a.href; })()",
       "true");
    // No href: protocol is ":" and everything else is "".
    is("(function () { var a = document.createElement('a');"
       " return a.protocol + '|' + a.host + '|' + a.pathname + '|' + a.hash; })()",
       ":|||");
    // A part written rewrites the href, and an <area> has the same surface.
    is("(function () { var a = document.createElement('area');"
       " a.href = 'http://site.example/a?q'; a.hash = 'h'; a.pathname = 'b'; a.search = '';"
       " return a.href; })()",
       "http://site.example/b#h");
}

void test_a_located_document_resolves_its_url_attributes() {
    // The bulk of reflection-*.html's URL rows: with a location set before
    // the load, `img.src` and `a.href` resolve against it (the URL standard
    // trims the spaces) and read the same,
    // and the document's three names for its address all say it.
    browser page{browser_options{400, 300}};
    page.set_location("file:///srv/pages/index.html#top");
    page.load_html("<!DOCTYPE html><img id=i src=' cat.png '><a id=a href=' cat.png '>"
                   "<script>console.log([document.URL, document.baseURI, location.href,"
                   " location.hash, document.getElementById('i').src,"
                   " document.getElementById('a').href, self.origin].join('|'));</script>");
    CHECK_EQ(page.bindings().console_output().back(),
             "file:///srv/pages/index.html#top|file:///srv/pages/index.html#top|"
             "file:///srv/pages/index.html#top|#top|file:///srv/pages/cat.png|"
             "file:///srv/pages/cat.png|null");
}

void test_the_document_knows_its_running_script_and_its_ready_state() {
    // Document.currentScript.html and document-readyState.html: the <script>
    // running is `currentScript` - inside eval too - and null in a timer;
    // readyState is "loading" while the parser's scripts run, "interactive"
    // at DOMContentLoaded and "complete" at load, each announced.
    browser page{browser_options{400, 300}};
    page.load_html(
        "<!DOCTYPE html><html><body><script id=a>var seen = [document.readyState];"
        " document.onreadystatechange = function () { seen.push(document.readyState); };"
        " document.addEventListener('DOMContentLoaded', function () {"
        " seen.push('dcl:' + document.readyState); });"
        " window.onload = function () { seen.push('load:' + document.readyState);"
        " console.log(seen.join()); };"
        " console.log(document.currentScript.id + ',' + eval('document.currentScript.id'));"
        " setTimeout(function () { console.log(String(document.currentScript)); }, 0);"
        "</script><script id=b>console.log(document.currentScript.id);</script></body></html>");
    (void)page.tick(16.0);
    const std::vector<std::string> & logged = page.bindings().console_output();
    CHECK_EQ(logged.size(), std::size_t{4});
    if (logged.size() == 4) {
        CHECK_EQ(logged[0], std::string{"a,a"});
        CHECK_EQ(logged[1], std::string{"b"});
        CHECK_EQ(logged[2],
                 std::string{"loading,interactive,dcl:interactive,complete,load:complete"});
        CHECK_EQ(logged[3], std::string{"null"});
    }
}

void test_an_inserted_script_runs_when_it_connects() {
    // dom/nodes/insertion-removing-steps/ and Document.currentScript.html: a
    // script a page made runs synchronously when it becomes connected, is
    // `currentScript` while it runs, and runs nested when another script
    // inserts it - s1 fills s3 and s3's output comes first. An empty
    // parser-inserted script was never started, so text appended later runs
    // it; once run, more text does nothing. A throw is reported, not raised.
    is("(function () { var s1 = document.createElement('script');"
       " var s2 = document.createElement('script'); var s3 = document.createElement('script');"
       " window.happened = []; window.s3 = s3; s1.id = 'one';"
       " s1.textContent = \"s3.appendChild(new Text('happened.push(3)'));"
       " happened.push(document.currentScript.id)\";"
       " s2.textContent = 'happened.push(2)';"
       " var div = document.createElement('div'); div.appendChild(s1); div.appendChild(s2);"
       " div.appendChild(s3); var before = happened.length; document.body.appendChild(div);"
       " return before + '|' + happened.join() + '|' + document.currentScript.tagName; })()",
       "0|3,one,2|SCRIPT");
    is("(function () { var s = document.createElement('script'); s.textContent = 'throw 1';"
       " var t = document.createElement('script'); t.textContent = 'window.after = 1';"
       " document.body.appendChild(s); document.body.appendChild(t); return window.after; })()",
       "1");
    browser page{browser_options{400, 300}};
    page.load_html("<!DOCTYPE html><html><body><script id=empty></script><script>"
                   "var e = document.getElementById('empty');"
                   " e.appendChild(new Text('console.log(\"ran:\" + document.currentScript.id)'));"
                   " e.appendChild(new Text('console.log(\"again\")'));"
                   " console.log('after');</script></body></html>");
    const std::vector<std::string> & logged = page.bindings().console_output();
    CHECK_EQ(logged.size(), std::size_t{2});
    if (logged.size() == 2) {
        CHECK_EQ(logged[0], std::string{"ran:empty"});
        CHECK_EQ(logged[1], std::string{"after"});
    }
    // A <template>'s script never started, so its CLONE runs when the clone
    // connects; the parser's own script that ran is started, and its clone
    // does not (remove-next-sibling-during-replace-with.html).
    browser cloned{browser_options{400, 300}};
    cloned.load_html("<!DOCTYPE html><html><body><template id=t><script>window.ran = "
                     "(window.ran || 0) + 1;</script></template><script id=s>window.also = "
                     "(window.also || 0) + 1;</script><script>"
                     "document.body.appendChild(document.getElementById('t').content.cloneNode("
                     "true)); document.body.appendChild(document.getElementById('s').cloneNode("
                     "true)); console.log(window.ran + ',' + window.also);</script></body></html>");
    CHECK_EQ(cloned.bindings().console_output().back(), std::string{"1,1"});
}

void test_aria_element_references_reflect_both_ways() {
    // aria-element-reflection.html: the content attribute's ID is looked up
    // in the element's tree, an explicitly set element wins and writes "",
    // null removes the attribute, and a later content attribute write
    // supersedes the explicit element. The list shape answers the same array
    // while its elements are the same, and refuses a non-element.
    is("(function () { var h = document.getElementById('host');"
       " h.innerHTML = '<div id=p aria-activedescendant=i1><div id=i1></div><div "
       "id=i2></div></div>';"
       " var p = document.getElementById('p'), i1 = document.getElementById('i1'),"
       " i2 = document.getElementById('i2'); var seen = [];"
       " seen.push(p.ariaActiveDescendantElement === i1);"
       " p.ariaActiveDescendantElement = i2;"
       " seen.push(p.ariaActiveDescendantElement === i2, p.getAttribute('aria-activedescendant'));"
       " p.setAttribute('aria-activedescendant', 'i1');"
       " seen.push(p.ariaActiveDescendantElement === i1);"
       " p.ariaActiveDescendantElement = null;"
       " seen.push(p.hasAttribute('aria-activedescendant'), String(p.ariaActiveDescendantElement));"
       " try { p.ariaActiveDescendantElement = 'x'; seen.push('no'); } catch (e) { "
       "seen.push(e.name); }"
       " return seen.join(); })()",
       "true,true,,true,false,null,TypeError");
    is("(function () { var h = document.getElementById('host');"
       " h.innerHTML = '<input id=f aria-labelledby=\"a b\"><span id=a></span><span id=b></span>';"
       " var f = document.getElementById('f'), a = document.getElementById('a'),"
       " b = document.getElementById('b'); var seen = [];"
       " var l = f.ariaLabelledByElements; seen.push(l.length, l[0] === a, l[1] === b,"
       " f.ariaLabelledByElements === l);"
       " f.ariaLabelledByElements = [b]; seen.push(f.ariaLabelledByElements[0] === b,"
       " f.getAttribute('aria-labelledby'));"
       " f.ariaLabelledByElements = null; seen.push(String(f.ariaLabelledByElements));"
       " return seen.join(); })()",
       "2,true,true,true,true,,null");
}

// --- the translate attribute --------------------------------------------------

void test_translate_inherits_through_elements_and_stops_at_a_fragment() {
    // the-translate-attribute-0xx.html and translate-enumerated-ascii-case-
    // insensitive.html: `yes`/"" enable, `no` disables, ASCII-insensitively,
    // and anything else inherits from the parent element.
    is("(function () { var h = document.getElementById('host');"
       " h.innerHTML = '<div translate=no><span translate=YeS></span>"
       "<span translate=x></span><span translate></span></div>';"
       " var s = h.querySelectorAll('span');"
       " return [h.translate, s[0].translate, s[1].translate, s[2].translate].join(); })()",
       "true,true,false,true");
    // translate-inherit-no-parent-element.html: a fragment or shadow root is
    // not an element, so a child of one is translate-enabled whatever the
    // host says.
    is("(function () { var host = document.createElement('my-element');"
       " host.setAttribute('translate', 'no'); var d = document.createElement('div');"
       " host.attachShadow({mode: 'open'}).appendChild(d); return d.translate; })()",
       "true");
    // The setter writes the keyword, and reads back through the same walk.
    is("(function () { var d = document.createElement('div'); d.translate = false;"
       " return d.getAttribute('translate') + ',' + d.translate; })()",
       "no,false");
}

// --- innerText / outerText getters -------------------------------------------

// getter.html's shape: markup into a connected container, then the first
// child's innerText, JSON-encoded so a newline or a tab is visible.
[[nodiscard]] std::string inner_text_of(const std::string & markup) {
    return answer("(function () { var h = document.getElementById('host');"
                  " h.innerHTML = " +
                  markup +
                  "; var e = h.querySelector('#target') || h.firstChild;"
                  " return JSON.stringify(e.innerText); })()");
}

void test_inner_text_collapses_whitespace_and_breaks_at_blocks() {
    CHECK_EQ(inner_text_of("'<div> abc  def\\n ghi '"), "\"abc def ghi\"");
    CHECK_EQ(inner_text_of("'<div>abc <br> def'"), "\"abc\\ndef\"");
    CHECK_EQ(inner_text_of("'<div>123<div>abc</div>def'"), "\"123\\nabc\\ndef\"");
    CHECK_EQ(inner_text_of("'<div><p>abc<p>def'"), "\"abc\\n\\ndef\"");
    CHECK_EQ(inner_text_of("'<div>abc<div></div><div></div>def'"), "\"abc\\ndef\"");
    CHECK_EQ(inner_text_of("'<div>123<span>abc</span>def'"), "\"123abcdef\"");
    CHECK_EQ(inner_text_of("'<div>abc <input> def'"), "\"abc  def\"");
    CHECK_EQ(inner_text_of("'<div>123<span style=display:inline-block> abc </span>def'"),
             "\"123abcdef\"");
}

void test_inner_text_reads_the_inline_style_it_can_see() {
    CHECK_EQ(inner_text_of("'<pre> abc\\n  def '"), "\" abc\\n  def \"");
    CHECK_EQ(inner_text_of("'<div style=white-space:pre-line>abc  \\n  def'"), "\"abc\\ndef\"");
    CHECK_EQ(inner_text_of("'<div>123<span style=display:none>abc'"), "\"123\"");
    CHECK_EQ(inner_text_of("'<div>123<span style=visibility:hidden>abc'"), "\"123\"");
    CHECK_EQ(inner_text_of("'<div style=text-transform:uppercase>abc'"), "\"ABC\"");
    // Not rendered - a `display: none` container - is textContent, verbatim.
    CHECK_EQ(inner_text_of("'<div style=display:none>abc  def'"), "\"abc  def\"");
    CHECK_EQ(inner_text_of("'<div><table><tr><td>abc<td>def<tr><td>ghi</table>'"),
             "\"abc\\tdef\\nghi\"");
    // A replaced element has no text, and outerText reads the same as innerText.
    CHECK_EQ(inner_text_of("'<textarea>abc'"), "\"\"");
    // The input stream's CR is a newline; a hidden element adds no breaks of
    // its own; a flex container's children are blockified; a <select>'s text
    // child has no box; an SVG <defs> renders nothing.
    CHECK_EQ(inner_text_of("'<pre>abc\\rdef'"), "\"abc\\ndef\"");
    CHECK_EQ(inner_text_of("'<div style=visibility:hidden><p><span style=visibility:visible>"
                           "abc</span></p><div style=visibility:visible>def</div></div>'"),
             "\"abc\\ndef\"");
    CHECK_EQ(inner_text_of("'<div style=display:flex><span>1</span><span>2</span></div>'"),
             "\"1\\n2\"");
    CHECK_EQ(inner_text_of("'<div><select>abc<option>x</option></select></div>'"), "\"x\"");
    CHECK_EQ(inner_text_of("'<div><svg><defs><text>abc</text></defs></svg></div>'"), "\"\"");
    is("(function () { var h = document.getElementById('host'); h.innerHTML = '<p>a<br>b';"
       " return JSON.stringify(h.firstChild.outerText); })()",
       "\"a\\nb\"");
    // Detached: textContent, since nothing renders it.
    is("(function () { var d = document.createElement('div'); d.innerHTML = 'a  b';"
       " return JSON.stringify(d.innerText); })()",
       "\"a  b\"");
}

} // namespace

int main() {
    test_legacy_null_to_empty_string_rows_write_nothing_for_null();
    test_nonce_is_a_slot_in_front_of_the_attribute();
    test_the_rows_the_element_tables_name_are_all_there();
    test_an_anchor_reports_the_parts_of_its_url();
    test_a_located_document_resolves_its_url_attributes();
    test_the_document_knows_its_running_script_and_its_ready_state();
    test_an_inserted_script_runs_when_it_connects();
    test_aria_element_references_reflect_both_ways();
    test_translate_inherits_through_elements_and_stops_at_a_fragment();
    test_inner_text_collapses_whitespace_and_breaks_at_blocks();
    test_inner_text_reads_the_inline_style_it_can_see();
    REPORT("html_dom_wpt");
}
