// `DOMParser` (HTML 8.5.1) as an interface: `new` required, parseFromString on
// the prototype and branded, and a document whose URL is the PARSER's window's
// document's - a frame's constructor makes the frame's, whichever realm's
// method is called (domparsing/DOMParser-parseFromString-url.html).

#include <ctbrowser.hpp>

#include "check.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr const char * inner_html = "<!DOCTYPE html><html><body>inner</body></html>";

// The page with one frame, and what the expression logged once both loaded.
[[nodiscard]] std::string answer(const std::string & expression) {
    ctbrowser::browser page{ctbrowser::browser_options{400, 300}};
    page.set_location("file:///pages/the-page.html");
    page.assets().add("inner.html", bytes_of(inner_html));
    page.load_html("<!DOCTYPE html><body><iframe src=inner.html></iframe><script>"
                   "addEventListener('load', function () { setTimeout(function () {"
                   " try { console.log(String(" +
                   expression +
                   ")); } catch (e) { console.log('threw:' + e.name); } }, 0); });"
                   "</script></body>");
    for (int i = 0; i < 4; ++i) { (void)page.tick(16.0); }
    const std::vector<std::string> & logged = page.bindings().console_output();
    if (logged.empty()) { return "<nothing logged: " + page.script_error() + ">"; }
    return logged.back();
}

void is(const std::string & expression, const std::string & expected) {
    const std::string got = answer(expression);
    CHECK_EQ(got, expected);
    if (got != expected) { std::printf("    %s\n", expression.c_str()); }
}

void test_the_interface() {
    is("[typeof DOMParser.prototype.parseFromString, DOMParser.prototype.parseFromString.length,"
       " new DOMParser() instanceof DOMParser,"
       " Object.getOwnPropertyNames(new DOMParser()).length].join()",
       "function,2,true,0");
    is("(function () { try { DOMParser(); } catch (e) { return e.name; } })()", "TypeError");
    is("(function () { try { DOMParser.prototype.parseFromString.call({}, '', 'text/html'); }"
       " catch (e) { return e.name; } })()",
       "TypeError");
    is("(function () { try { new DOMParser().parseFromString('', 'text/plain'); }"
       " catch (e) { return e.name; } })()",
       "TypeError");
}

void test_the_parsers_window_names_the_url() {
    const std::string frame = "file:///pages/inner.html";
    is("new DOMParser().parseFromString('<p>', 'text/html').URL", "file:///pages/the-page.html");
    is("new frames[0].DOMParser().parseFromString('<p>', 'text/html').URL", frame);
    is("new frames[0].DOMParser().parseFromString('<r/>', 'text/xml').URL", frame);
    // The method of the other realm, on a parser of this one - and back.
    is("frames[0].DOMParser.prototype.parseFromString.call(new DOMParser(), '<p>',"
       " 'text/html').URL",
       "file:///pages/the-page.html");
    is("DOMParser.prototype.parseFromString.call(new frames[0].DOMParser(), '<p>',"
       " 'text/html').URL",
       frame);
}

} // namespace

int main() {
    test_the_interface();
    test_the_parsers_window_names_the_url();
    REPORT("html_dom_parser");
}
