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

} // namespace

int main() {
    test_legacy_null_to_empty_string_rows_write_nothing_for_null();
    test_nonce_is_a_slot_in_front_of_the_attribute();
    test_an_anchor_reports_the_parts_of_its_url();
    REPORT("html_dom_wpt");
}
