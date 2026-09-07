// `<iframe>` - a nested browsing context, which here is a second Document.
//
// Its own file because what it pins down is a MODEL rather than a method, the
// same way unit/second_document.cpp does: a frame is a second `dom_bindings`
// over its own tree, sharing the realm, and the file that builds it is
// lib/Shell/bindings/frames.cpp. These cases are the parts of that decision
// that would otherwise drift.
//
// THREE OF THEM ASSERT THAT SOMETHING DOES NOT WORK, and those are the
// important ones. A frame's document runs no script, a frame's window is not a
// second realm, and a `src` that resolves to nothing still leaves a Document
// behind and reports `error` rather than `load`. A test that only checked the
// happy path would let each of those turn into a silent wrong answer.
//
// THE ORDERING IS THE POINT of the last two: a page's own `load` handler is
// where a test reads `frame.contentDocument`, so the frame's document has to
// exist BEFORE that event and the frame's own `load` has to arrive after it.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

// Two frame sources, seeded into the registry rather than written to disk, so
// the test is hermetic and needs no document root: `asset_registry::load`
// consults the registry before it ever looks at the filesystem.
constexpr const char * inner_html = "<!DOCTYPE html><html><head><title>inner</title></head>"
                                    "<body><p id=greeting>hello</p></body></html>";
constexpr const char * inner_xml = "<?xml version=\"1.0\"?><root viewBox=\"0 0 1 1\">"
                                   "<Leaf/>text</root>";

[[nodiscard]] std::vector<std::byte> bytes_of(std::string_view text) {
    std::vector<std::byte> out;
    out.reserve(text.size());
    for (const char c : text) { out.push_back(static_cast<std::byte>(c)); }
    return out;
}

// Load a page with the two frames available by name, run enough ticks for the
// reconcile and the frame's load event to have happened, and answer with what
// the expression logged.
[[nodiscard]] std::string answer(const std::string & body, const std::string & expression) {
    browser page{browser_options{400, 300}};
    page.assets().add("inner.html", bytes_of(inner_html));
    page.assets().add("inner.xml", bytes_of(inner_xml));
    // A `setTimeout` INSIDE the window's load handler, and the nesting is the
    // point. One tick does, in order: reconcile the frames, dispatch the page's
    // own `load`, drain the frame load events, then run the timers. So a check
    // written directly in the load handler runs BEFORE the frame's own `load`
    // has been announced, and anything a frame listener sets is not there yet.
    // The timer is the first moment both have happened.
    const std::string html =
        "<!DOCTYPE html><html><head><title>the page</title></head><body>" + body +
        "<script>window.addEventListener('load', function () {"
        " setTimeout(function () {"
        " try { console.log(String(" +
        expression +
        ")); } catch (e) { console.log('threw:' + e.name); } }, 0); });</script></body></html>";
    page.load_html(html);
    for (int i = 0; i < 4; ++i) { (void)page.tick(16.0); }
    const std::vector<std::string> & logged = page.bindings().console_output();
    if (logged.empty()) { return "<nothing logged: " + page.script_error() + ">"; }
    return logged.back();
}

void is(const std::string & body, const std::string & expression, const std::string & expected) {
    const std::string got = answer(body, expression);
    CHECK_EQ(got, expected);
    if (got != expected) { std::printf("    %s\n", expression.c_str()); }
}

constexpr const char * one_frame = "<iframe id=f src=inner.html></iframe>";

void test_a_frame_has_a_document_of_its_own() {
    is(one_frame, "document.getElementById('f').contentDocument.title", "inner");
    is(one_frame,
       "document.getElementById('f').contentDocument.getElementById('greeting').textContent",
       "hello");
    // NOT THE PAGE'S DOCUMENT. The whole model is that it is a second one, and
    // an implementation that answered with the page's would pass every
    // assertion above.
    is(one_frame, "document.getElementById('f').contentDocument === document", "false");
    is(one_frame, "document.getElementById('f').contentDocument.contentType", "text/html");
    // The element the frame is, reached back through the frame's window.
    is(one_frame, "document.getElementById('f').contentWindow.frameElement.id", "f");
    is(one_frame, "document.getElementById('f').contentWindow.document.title", "inner");
    is(one_frame, "document.getElementById('f').contentWindow.parent === window", "true");
}

void test_a_frame_whose_source_is_xml_is_parsed_as_xml() {
    constexpr const char * xml_frame = "<iframe id=f src=inner.xml></iframe>";
    is(xml_frame, "document.getElementById('f').contentDocument.contentType", "text/xml");
    // CASE IS PRESERVED, which is the whole reason the XML front end exists: the
    // HTML tree builder would answer `LEAF` here and lowercase `viewBox`.
    is(xml_frame, "document.getElementById('f').contentDocument.documentElement.tagName", "root");
    is(xml_frame,
       "document.getElementById('f').contentDocument.documentElement.getAttribute('viewBox')",
       "0 0 1 1");
}

void test_a_frame_with_no_source_is_still_a_document() {
    // about:blank, which a page uses as a scratch document: `frame.contentDocument
    // .body` has to be there to write into.
    is("<iframe id=f></iframe>", "document.getElementById('f').contentDocument.body.tagName",
       "BODY");
    is("<iframe id=f></iframe>", "document.getElementById('f').contentDocument.contentType",
       "text/html");
}

void test_a_source_that_resolves_to_nothing_reports_error() {
    // A Document is still there - a browser shows its own error page in one -
    // and it is `error` rather than `load` that the element hears.
    is("<iframe id=f src=missing.html></iframe>",
       "document.getElementById('f').contentDocument.body.tagName", "BODY");
    // The listener is registered by a script in the markup, which runs while
    // the document is still being parsed and so is in place before the first
    // reconcile. `answer`'s own script runs on the window's load event, which
    // is one moment too late for this.
    is("<iframe id=f src=missing.html></iframe><script>"
       "document.getElementById('f').addEventListener('error', function () {"
       " window.__saw = (window.__saw || 0) + 1; });"
       "document.getElementById('f').addEventListener('load', function () {"
       " window.__saw = 'load'; });</script>",
       "String(window.__saw)", "1");
}

void test_the_load_event_arrives_at_the_frame() {
    // Registered in the markup, so the handler exists before the reconcile runs
    // - which is the ordering the file's header is about.
    is("<iframe id=f src=inner.html></iframe><script>"
       "document.getElementById('f').addEventListener('load', function () {"
       " window.__loaded = (window.__loaded || 0) + 1; });</script>",
       "String(window.__loaded)", "1");
}

void test_a_frame_appended_by_script_loads_too() {
    // The one that cannot go through `answer`: the frame does not exist until
    // the page's own load handler has run, so what it reports has to be read a
    // tick later, from the frame's OWN load event.
    browser page{browser_options{400, 300}};
    page.assets().add("inner.html", bytes_of(inner_html));
    page.load_html("<!DOCTYPE html><html><body>"
                   "<script>window.addEventListener('load', function () {"
                   " var f = document.createElement('iframe');"
                   " f.onload = function () { console.log(f.contentDocument.title); };"
                   " f.src = 'inner.html';"
                   " document.body.appendChild(f); });</script></body></html>");
    for (int i = 0; i < 4; ++i) { (void)page.tick(16.0); }
    const std::vector<std::string> & logged = page.bindings().console_output();
    CHECK(!logged.empty());
    if (!logged.empty()) { CHECK_EQ(logged.back(), std::string{"inner"}); }
}

void test_a_data_url_frame_carries_its_own_type() {
    is("<iframe id=f src='data:text/html,<p>x</p>'></iframe>",
       "document.getElementById('f').contentDocument.contentType", "text/html");
}

void test_a_frame_runs_no_script() {
    // DELIBERATE, and asserted so it stays deliberate: one realm and one event
    // loop mean a frame's script would be the page's script wearing the
    // frame's name.
    browser page{browser_options{400, 300}};
    page.assets().add("scripted.html",
                      bytes_of("<html><body><script>window.__ranInFrame = 1;</script>"
                               "</body></html>"));
    page.load_html("<!DOCTYPE html><html><body><iframe src=scripted.html></iframe>"
                   "<script>window.addEventListener('load', function () {"
                   " console.log(String(window.__ranInFrame)); });</script></body></html>");
    for (int i = 0; i < 3; ++i) { (void)page.tick(16.0); }
    const std::vector<std::string> & logged = page.bindings().console_output();
    CHECK(!logged.empty());
    if (!logged.empty()) { CHECK_EQ(logged.back(), std::string{"undefined"}); }
}

} // namespace

int main() {
    test_a_frame_has_a_document_of_its_own();
    test_a_frame_whose_source_is_xml_is_parsed_as_xml();
    test_a_frame_with_no_source_is_still_a_document();
    test_a_source_that_resolves_to_nothing_reports_error();
    test_the_load_event_arrives_at_the_frame();
    test_a_frame_appended_by_script_loads_too();
    test_a_data_url_frame_carries_its_own_type();
    test_a_frame_runs_no_script();
    REPORT("frames");
}
