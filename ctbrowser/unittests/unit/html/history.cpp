// `window.history`, HTML 7.4.2: pushState and replaceState move the document's
// address and keep a structured clone of the state; a URL the document cannot
// be rewritten to is a SecurityError; back() is a later task that restores the
// entry's address and state and fires `popstate`, then `hashchange` when the
// fragment moved.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <string>
#include <vector>

namespace {

// The page at an http: address, run for a few ticks; what it logged.
std::vector<std::string> run(const std::string & script, const char * url) {
    ctbrowser::browser page{ctbrowser::browser_options{400, 300}};
    page.set_location(url);
    page.load_html("<!DOCTYPE html><body><script>try {" + script +
                   "} catch (e) { console.log('threw:' + e.name); }</script></body>");
    for (int i = 0; i < 4; ++i) { (void)page.tick(16.0); }
    return page.bindings().console_output();
}

// The lines, `|`-joined: a mismatch prints every one of them.
std::string joined(const std::vector<std::string> & lines) {
    std::string out;
    for (const std::string & line : lines) { out += (out.empty() ? "" : "|") + line; }
    return out;
}

void test_push_and_replace() {
    const std::vector<std::string> logged =
        run("var d = {a: [1]}; history.pushState(d, '', 'next.html?q=1');"
            "console.log([history.length, location.href, document.URL,"
            " history.state.a[0], history.state !== d, history.state === history.state,"
            " history instanceof History, typeof History.prototype.pushState].join());"
            "history.replaceState('s', '', '#frag');"
            "console.log([history.length, location.hash, history.state,"
            " history.scrollRestoration].join());"
            "history.scrollRestoration = 'manual'; history.scrollRestoration = 'bogus';"
            "console.log(history.scrollRestoration);"
            "try { history.pushState(null, '', 'https://elsewhere.test/'); }"
            " catch (e) { console.log(e.name, location.pathname); }"
            "try { history.pushState(function () {}, ''); }"
            " catch (e) { console.log(e.name); }"
            "try { new History(); } catch (e) { console.log(e.name); }",
            "http://example.test/dir/page.html");
    CHECK_EQ(joined(logged),
             std::string{"2,http://example.test/dir/next.html?q=1,http://example.test/dir/"
                         "next.html?q=1,1,true,true,true,function|2,#frag,s,auto|manual|"
                         "SecurityError /dir/next.html|DataCloneError|TypeError"});
}

// back() is a TASK: nothing moves in the statement, then `popstate` carries
// the entry's state and `hashchange` follows when only the fragment differed.
void test_back_is_a_task() {
    const std::vector<std::string> logged =
        run("history.replaceState({n: 1}, '', '#one'); history.pushState({n: 2}, '', '#two');"
            "addEventListener('popstate', function (e) {"
            " console.log('popstate ' + e.state.n + ' ' + location.hash + ' ' + history.state.n);"
            "});"
            "addEventListener('hashchange', function (e) {"
            " console.log('hashchange ' + e.oldURL.split('#')[1] + ' ' + e.newURL.split('#')[1]);"
            "});"
            "history.back(); console.log('after back ' + location.hash);",
            "http://example.test/page.html");
    CHECK_EQ(joined(logged), std::string{"after back #two|popstate 1 #one 1|hashchange two one"});
}

// A file: document may move its query and fragment, not its path (7.4.1.3).
void test_file_urls_keep_their_path() {
    const std::vector<std::string> logged =
        run("history.pushState(null, '', '?x#y'); console.log(location.search + location.hash);"
            "try { history.pushState(null, '', 'other.html'); }"
            " catch (e) { console.log(e.name); }",
            "file:///pages/page.html");
    CHECK_EQ(joined(logged), std::string{"?x#y|SecurityError"});
}

} // namespace

int main() {
    test_push_and_replace();
    test_back_is_a_task();
    test_file_urls_keep_their_path();
    REPORT("html_history");
}
