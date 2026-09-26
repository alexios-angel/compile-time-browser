// FORM SUBMISSION THAT LEAVES THE PAGE: HTML 4.10.21.3 from the entry list
// on - the enctype's encoding of the list, the submitter's formaction,
// formmethod, formenctype and formtarget, the navigation PLANNED at the frame
// the target names, and the request an embedder answers
// (browser_options::on_request) before the registry is asked.
//
// The handler here stands in for a server the way tools/ctdrive's WPT
// emulation does: it records what arrived and answers with it, so a case
// asserts on both halves - the bytes the engine sent, and the document the
// frame got back.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <optional>
#include <string>
#include <vector>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser::shell::resource_reply;
using ctbrowser::shell::resource_request;

namespace {

struct run {
    std::vector<resource_request> seen;
    std::string logged;
};

// Load `body`, let the page's load handler submit, tick until the frame's
// `load` has been heard, and answer with what arrived and what was logged.
[[nodiscard]] run submit(const std::string & body) {
    run out;
    browser_options options{400, 300};
    options.on_request = [&out](const resource_request & request) -> std::optional<resource_reply> {
        if (!request.url.ends_with(".py") && request.url.find(".py?") == std::string::npos) {
            return std::nullopt;
        }
        out.seen.push_back(request);
        resource_reply reply;
        reply.content_type = "text/plain; charset=UTF-8";
        reply.body = request.method + " " + request.content_type + "\n" + request.body;
        return reply;
    };
    browser page{options};
    page.set_location("file:///pages/the-page.html");
    page.assets().add("blank.html", bytes_of("<!DOCTYPE html><title>blank</title>"));
    page.load_html("<!DOCTYPE html><html><body>" + body + "</body></html>");
    for (int i = 0; i < 8; ++i) { (void)page.tick(16.0); }
    for (const std::string & line : page.bindings().console_output()) { out.logged += line + "|"; }
    return out;
}

void test_a_post_carries_its_urlencoded_body() {
    const run got =
        submit("<iframe name=t></iframe>"
               "<form id=f method=post action=echo.py target=t accept-charset=utf-8>"
               "<input name='a b' value='c&amp;d'><input name=e value='\xC3\xA9'></form>"
               "<script>addEventListener('load', function () {"
               " var t = document.querySelector('iframe');"
               " t.onload = function () { console.log(t.contentDocument.body.textContent); };"
               " document.getElementById('f').submit(); });</script>");
    CHECK_EQ(got.seen.size(), std::size_t{1});
    if (!got.seen.empty()) {
        CHECK_EQ(got.seen[0].method, std::string{"POST"});
        CHECK_EQ(got.seen[0].url, std::string{"file:///pages/echo.py"});
        CHECK_EQ(got.seen[0].content_type, std::string{"application/x-www-form-urlencoded"});
        CHECK_EQ(got.seen[0].body, std::string{"a+b=c%26d&e=%C3%A9"});
    }
    // ...and the frame's document is the answer, as text/plain: a <pre>.
    CHECK_EQ(got.logged,
             std::string{"POST application/x-www-form-urlencoded\na+b=c%26d&e=%C3%A9|"});
}

void test_the_encodings_follow_the_forms_charset() {
    // windows-1252 has é and not ə: the one is its byte, the other an NCR.
    const run got =
        submit("<iframe name=t></iframe>"
               "<form id=f method=post action=echo.py target=t accept-charset=windows-1252"
               " enctype=text/plain><input name='\xC3\xA9' value='\xC9\x99'></form>"
               "<form id=g method=post action=echo.py target=t accept-charset=windows-1252"
               " enctype=multipart/form-data><input type=hidden name='a\"\nb' value='x\ny'></form>"
               "<script>addEventListener('load', function () {"
               " document.getElementById('f').submit(); });"
               "setTimeout(function () { document.getElementById('g').submit(); }, 50);</script>");
    CHECK_EQ(got.seen.size(), std::size_t{2});
    if (got.seen.size() == 2) {
        CHECK_EQ(got.seen[0].content_type, std::string{"text/plain"});
        CHECK_EQ(got.seen[0].body, std::string{"\xE9=&#601;\r\n"});
        const std::string & multipart = got.seen[1].body;
        const std::string boundary = multipart.substr(0, multipart.find("\r\n"));
        CHECK_EQ(got.seen[1].content_type, "multipart/form-data; boundary=" + boundary.substr(2));
        CHECK_EQ(multipart, boundary +
                                "\r\nContent-Disposition: form-data; name=\"a%22%0D%0Ab\"\r\n"
                                "\r\nx\r\ny\r\n" +
                                boundary + "--\r\n");
    }
}

void test_the_submitters_attributes_win() {
    // formaction, formmethod, formenctype and formtarget on the button that
    // submitted - and a GET's entry list REPLACES the action's query.
    const run got =
        submit("<iframe name=one></iframe><iframe name=two></iframe>"
               "<form id=f action=blank.html?old=1 target=one method=post>"
               "<input name=q value=1>"
               "<button id=b formaction=blank.html?x=y#frag formmethod=get formtarget=two"
               " name=by value=button>go</button></form>"
               "<script>addEventListener('load', function () {"
               " var two = document.getElementsByName('two')[0];"
               " two.onload = function () {"
               "  console.log(two.contentWindow.location.search + two.contentWindow.location.hash"
               "   + ',' + two.contentDocument.title); };"
               " document.getElementById('b').click(); });</script>");
    CHECK(got.seen.empty());
    CHECK_EQ(got.logged, std::string{"?q=1&by=button#frag,blank|"});
}

void test_a_later_submission_replaces_the_planned_one() {
    // form-double-submit.html: an onclick submit() at one frame, then the
    // button's own submission at another - the form's one planned navigation
    // is the later one, and only it navigates.
    const run got =
        submit("<iframe name=one></iframe><iframe name=two></iframe>"
               "<form id=f action=blank.html target=one><input type=hidden name=n value=1>"
               "<input id=s type=submit></form>"
               "<script>var heard = [];"
               "addEventListener('load', function () {"
               " var f = document.getElementById('f');"
               " document.getElementsByName('one')[0].onload = function () { heard.push('one'); };"
               " document.getElementsByName('two')[0].onload = function () { heard.push('two');"
               "  console.log(heard.join()); };"
               " f.addEventListener('click', function () { f.submit(); f.target = 'two'; });"
               " document.getElementById('s').click(); });</script>");
    CHECK_EQ(got.logged, std::string{"two|"});
}

void test_a_button_in_a_frame_submits_its_form() {
    // submit-entity-body.html: the form is written into the frame the test
    // loaded and its button clicked there - the frame's own navigable is
    // `_self`, and the request goes out from the frame's document.
    const run got =
        submit("<iframe id=t src=blank.html></iframe>"
               "<script>addEventListener('load', function () {"
               " var t = document.getElementById('t');"
               " t.onload = function () { console.log(t.contentDocument.body.textContent); };"
               " t.contentDocument.body.innerHTML = '<form method=post action=echo.py"
               "  enctype=text/plain><textarea name=qux>baz</textarea>"
               "  <button id=go>go</button></form>';"
               " t.contentDocument.getElementById('go').click(); });</script>");
    CHECK_EQ(got.seen.size(), std::size_t{1});
    if (!got.seen.empty()) { CHECK_EQ(got.seen[0].body, std::string{"qux=baz\r\n"}); }
    CHECK_EQ(got.logged, std::string{"POST text/plain\nqux=baz\n|"});
}

void test_a_disabled_fieldset_disables_a_frames_button() {
    // HTML 4.10.18.5: a button in a disabled <fieldset> is actually disabled,
    // in a frame as in the page - and one in its first <legend> is not. A
    // DISPATCHED click, which reaches the activation behaviour whatever the
    // element (click() itself refuses a disabled control first).
    const run got =
        submit("<iframe id=t src=blank.html></iframe>"
               "<script>addEventListener('load', function () {"
               " var t = document.getElementById('t');"
               " t.onload = function () { console.log(t.contentDocument.body.textContent); };"
               " t.contentDocument.body.innerHTML = '<form method=post action=echo.py"
               "  enctype=text/plain><input name=a value=b><fieldset disabled>"
               "  <legend><button id=in>in</button></legend><button id=out>out</button>"
               "  </fieldset></form>';"
               " var d = t.contentDocument, click = function (id) {"
               "  d.getElementById(id).dispatchEvent(new MouseEvent('click', {bubbles: true})); };"
               " click('out'); click('in'); });</script>");
    CHECK_EQ(got.seen.size(), std::size_t{1});
    if (!got.seen.empty()) { CHECK_EQ(got.seen[0].body, std::string{"a=b\r\n"}); }
}

} // namespace

int main() {
    test_a_post_carries_its_urlencoded_body();
    test_the_encodings_follow_the_forms_charset();
    test_the_submitters_attributes_win();
    test_a_later_submission_replaces_the_planned_one();
    test_a_button_in_a_frame_submits_its_form();
    test_a_disabled_fieldset_disables_a_frames_button();
    REPORT("forms_submission");
}
