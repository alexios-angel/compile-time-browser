// HTMLHyperlinkElementUtils (HTML 4.6.3) on <a> and <area>, against a live
// page - one case per rule url/a-element.html and
// url/url-setters-a-area.window.js found the other answer to. The pattern is
// unit/document_api_wpt.cpp's: one expression against a fresh page, and
// whatever it logged.

#include <ctbrowser.hpp>

#include "check.hpp"
#include "dom_probe.hpp"

#include <string>

namespace {

constexpr const char * page_html = R"(<!DOCTYPE html>
<html><head><base id=base href="http://example.org/foo/bar"></head><body>
<a id=a href="http://user:pass@host:8080/p/q?x=1#frag">link</a>
<area id=area href="http://example.org/one/two">
<a id=bare>no href at all</a>
<a id=bad href="http://[::1">unparseable</a>
</body></html>)";

// Each case is an IIFE, since most of them need more than one statement.
void is(const std::string & body, const std::string & expected) {
    ctbrowser_test::is_in(page_html, "(function () { " + body + " })()", expected);
}

// --- the getters ------------------------------------------------------------

void test_every_member_of_the_mixin_reports() {
    is("var a = document.getElementById('a');"
       " return [a.protocol, a.username, a.password, a.host, a.hostname, a.port,"
       " a.pathname, a.search, a.hash, a.origin].join('|');",
       "http:|user|pass|host:8080|host|8080|/p/q|?x=1|#frag|http://host:8080");
    // `username` and `password` are the two the hand-cut accessors this
    // replaced did not have at all, and are the first thing a-element.js reads.
    is("return typeof document.getElementById('area').username;", "string");
    is("var a = document.getElementById('area');"
       " return [a.protocol, a.host, a.pathname, a.search, a.hash].join('|');",
       "http:|example.org|/one/two||");
}

void test_no_href_and_a_href_that_does_not_parse() {
    // "If url is null and this has no href content attribute, return the empty
    // string" - and every other member answers its own null answer.
    is("var b = document.getElementById('bare');"
       " return [b.href, b.protocol, b.host, b.pathname].join('|');",
       "|:||");
    // A href that does not parse: `href` is the CONTENT ATTRIBUTE and
    // `protocol` is ":", which is exactly how a-element.js tells a failure.
    is("var b = document.getElementById('bad'); return b.href + '|' + b.protocol;",
       "http://[::1|:");
}

void test_the_base_element_is_the_base() {
    // HTML 4.2.3: a relative href resolves against <base href>, not against
    // the document's own address.
    is("var a = document.createElement('a'); a.setAttribute('href', 'c');"
       " return a.href;",
       "http://example.org/foo/c");
    // And a base rewritten by script is seen at once, which is what
    // a-element.js does before every one of its cases.
    is("document.getElementById('base').href = 'https://other.test/dir/';"
       " var a = document.createElement('a'); a.setAttribute('href', 'c');"
       " return a.href;",
       "https://other.test/dir/c");
}

// --- the setters ------------------------------------------------------------

void test_a_setter_runs_the_url_standards_steps_and_writes_href_back() {
    is("var a = document.getElementById('a'); a.protocol = 'https'; return a.href;",
       "https://user:pass@host:8080/p/q?x=1#frag");
    is("var a = document.getElementById('a'); a.host = 'other.test:99'; return a.href;",
       "http://user:pass@other.test:99/p/q?x=1#frag");
    is("var a = document.getElementById('a'); a.hostname = 'other.test';"
       " return a.port + '|' + a.host;",
       "8080|other.test:8080");
    is("var a = document.getElementById('a'); a.search = 'y=2'; return a.href;",
       "http://user:pass@host:8080/p/q?y=2#frag");
    // The empty string takes the `?` and the `#` with it.
    is("var a = document.getElementById('a'); a.search = ''; a.hash = ''; return a.href;",
       "http://user:pass@host:8080/p/q");
    is("var a = document.getElementById('area'); a.pathname = 'three'; return a.href;",
       "http://example.org/three");
    is("var a = document.getElementById('a'); a.username = 'me'; a.password = '';"
       " return a.href;",
       "http://me@host:8080/p/q?x=1#frag");
    // Non-ASCII in a protocol is rejected and the URL is untouched - one of
    // url-setters-a-area.window.js's first cases.
    is("var a = document.getElementById('area'); a.protocol = '\\u00e9'; return a.protocol;",
       "http:");
    // And the content attribute is what changed, not a private copy.
    is("var a = document.getElementById('area'); a.hash = 'top';"
       " return a.getAttribute('href');",
       "http://example.org/one/two#top");
}

void test_a_setter_on_an_element_with_no_url_does_nothing() {
    // "If url is null, return" - no throw, no attribute written.
    is("var b = document.getElementById('bare'); b.protocol = 'https';"
       " return b.hasAttribute('href') + '|' + b.href;",
       "false|");
    // `href` itself is the exception: it writes the attribute unparsed.
    is("var b = document.getElementById('bare'); b.href = '  spaced  ';"
       " return b.getAttribute('href');",
       "  spaced  ");
}

void test_origin_is_readonly() {
    is("var a = document.getElementById('a'); a.origin = 'https://elsewhere.test';"
       " return a.origin;",
       "http://host:8080");
}

} // namespace

int main() {
    test_every_member_of_the_mixin_reports();
    test_no_href_and_a_href_that_does_not_parse();
    test_the_base_element_is_the_base();
    test_a_setter_runs_the_url_standards_steps_and_writes_href_back();
    test_a_setter_on_an_element_with_no_url_does_nothing();
    test_origin_is_readonly();
    REPORT("hyperlink_utils");
}
