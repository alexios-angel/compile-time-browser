// LEAVING THE PAGE, AND WHAT THE EMBEDDER IS TOLD: `location`'s parts and
// `document.cookie`, `alert`, `location.reload`, a link handed to the embedder
// or scrolled to as a fragment - and the three cases that drive `run_app`
// rather than a bare browser, because the wiring between the application's
// hooks and the browser's was what had gone wrong. Those three initialise SDL,
// which is why this test carries the sanitizer suppressions in
// unittests/CMakeLists.txt.
//
// One of eight files carved out of unittests/unit/bindings_basics.cpp on
// 2026-09-07, when it had reached 3,365 lines. Every case is verbatim and in
// the order it had; the helpers more than one of the eight needs are in
// page_probe.hpp beside this, and `find_id` is test/support/dom_probe.hpp's.

#include <ctbrowser/app/app.hpp>
#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include "page_probe.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser::shell::input_event;
using ctbrowser_test::check;
using ctbrowser_test::log_of;

namespace {

// `location`'s parts, and `document.cookie`.
//
// Neither is exotic and both are read WITHOUT a guard: the idiom is
// `location.search.substring(1)` and `document.cookie.split(';')`, so an absent
// one is not a missing feature but a TypeError on the first line of whatever
// library reached for it.
void test_location_parts_and_cookies() {
    browser page{browser_options{300, 200}};
    page.load_html(R"(<html><body><script>
        console.log('parts=' + [typeof location.protocol, typeof location.host,
                                typeof location.hostname, typeof location.port,
                                typeof location.pathname, typeof location.search,
                                typeof location.origin].join(','));
        // Reading gives every pair; writing sets ONE of them, so a page that
        // stores two things has both.
        console.log('empty=[' + document.cookie + ']');
        document.cookie = 'a=1';
        document.cookie = 'b=2; path=/; SameSite=Lax';
        console.log('two=' + document.cookie);
        document.cookie = 'a=9';
        console.log('replaced=' + document.cookie);
    </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log[0] == "parts=string,string,string,string,string,string,string",
          "location reports every part of its URL: " + log[0]);
    check(log[1] == "empty=[]", "no cookies is the empty string, not undefined: " + log[1]);
    // The attributes after the first `;` are not part of the value.
    check(log[2] == "two=a=1; b=2", "a write ADDS a cookie rather than replacing them: " + log[2]);
    check(log[3] == "replaced=a=9; b=2", "...and writing the same name replaces it: " + log[3]);
}

// --- alert, location, and <a href> -------------------------------------
//
// The last three things the previous engine's script surface had and this engine's did not. MDN's
// breakout calls alert() and document.location.reload() the moment the game
// ends, so a page could win and then die on an undefined identifier.

void test_alert_is_recorded() {
    browser page{browser_options{200, 100}};
    page.load_html("<body><script>alert('hello'); alert('again');</script></body>");
    check(page.script_error().empty(), "the script ran");
    check(page.alerts().size() == 2, "both alerts were recorded");
    if (page.alerts().size() == 2) {
        check(page.alerts()[0] == "hello" && page.alerts()[1] == "again", "in order, with text");
    }
}

void test_alert_reaches_the_hook() {
    browser page{browser_options{200, 100}};
    std::vector<std::string> seen;
    page.set_alert_hook([&seen](const std::string & message) { seen.push_back(message); });
    page.load_html("<body><script>alert('modal');</script></body>");
    check(seen.size() == 1 && !seen.empty() && seen[0] == "modal", "the hook saw it");
}

void test_location_reload_reruns_the_page() {
    browser page{browser_options{200, 100}};
    // The script appends a paragraph, so a reload is visible as a page that
    // has ONE again rather than two - a reload re-parses the source, it does
    // not re-run the script over the mutated document.
    page.load_html(R"(<body><div id=host></div><script>
    var p = document.createElement('p');
    p.setAttribute('id', 'added');
    document.getElementById('host').appendChild(p);
    </script></body>)");
    check(page.script_error().empty(), "the script ran");

    // A page that reloads itself from a timer: the request is recorded and
    // drained BETWEEN callbacks, because a reload inside one would destroy the
    // context the callback is running in.
    page.load_html(R"(<body><p>page</p><script>
    setTimeout(function () { document.location.reload(); }, 5);
    var runs = 0;
    </script></body>)");
    check(page.script_error().empty(), "the reloading page ran");
    (void)page.frame();
    check(page.tick(10) == 1, "the timer fired");
    // After the reload the page is fresh: the same timer is armed again.
    check(page.script_error().empty(), "the reloaded page ran too");
    check(page.tick(10) == 1, "and its timer fired, so the script really re-ran");
}

void test_window_and_document_share_one_location() {
    browser page{browser_options{200, 100}};
    page.load_html(R"(<body><script>
    console.log(String(document.location === window.location));
    console.log(String(location === window.location));
    </script></body>)");
    check(log_of(page).size() == 2, "both comparisons logged");
    if (log_of(page).size() == 2) {
        check(log_of(page)[0] == "true" && log_of(page)[1] == "true",
              "document.location, window.location and location are ONE object");
    }
}

void test_a_link_is_handed_to_the_embedder() {
    browser page{browser_options{400, 200}};
    std::vector<std::string> visited;
    page.set_navigate_hook([&visited](const std::string & url) { visited.push_back(url); });
    page.load_html("<body><a href='https://example.com/x'>a link</a></body>");
    check(page.frame().has_value(), "the page renders");

    // Clicked on the link's TEXT, which is a different node from the <a>.
    (void)page.handle(input_event::mouse_down_at(8, 8));
    (void)page.handle(input_event::mouse_up_at(8, 8));
    check(visited.size() == 1, "the link was followed");
    if (!visited.empty()) { check(visited[0] == "https://example.com/x", "with its href"); }
    check(page.location_href() == "https://example.com/x", "and location.href records it");
}

void test_a_fragment_scrolls_instead_of_navigating() {
    browser page{browser_options{300, 200}};
    std::vector<std::string> visited;
    page.set_navigate_hook([&visited](const std::string & url) { visited.push_back(url); });
    page.load_html(R"(<body><a href='#far'>jump</a>
    <div style='height:1200px'>tall</div>
    <p id=far>the target</p></body>)");
    check(page.frame().has_value(), "the page renders");
    check(page.scroll_y() == 0, "starts at the top");

    (void)page.handle(input_event::mouse_down_at(8, 8));
    (void)page.handle(input_event::mouse_up_at(8, 8));
    check(visited.empty(), "a fragment is NOT handed to the embedder");
    check(page.scroll_y() > 1000, "it scrolled to the target instead");
    check(page.location_hash() == "#far", "and location.hash says where");
}

void test_a_page_can_read_where_a_link_went() {
    browser page{browser_options{300, 200}};
    page.set_navigate_hook([](const std::string &) {});
    page.load_html(R"(<body><a href='/first'>go</a><script>
    document.addEventListener('click', function () { console.log(location.href); });
    </script></body>)");
    check(page.frame().has_value(), "the page renders");

    // A listener runs BEFORE the default action, so the first click logs the
    // href from before it - empty - and the second logs the first link's.
    // That second value is the point: `href` was written once when the object
    // was built, so a page could never see a link it had already followed.
    (void)page.handle(input_event::mouse_down_at(12, 12));
    (void)page.handle(input_event::mouse_up_at(12, 12));
    check(page.location_href() == "/first", "the browser recorded the href");
    (void)page.handle(input_event::mouse_down_at(12, 12));
    (void)page.handle(input_event::mouse_up_at(12, 12));
    check(page.script_error().empty(), "reading location.href from script works");
    check(log_of(page).size() == 2, "the listener fired twice");
    if (log_of(page).size() == 2) {
        check(log_of(page)[1] == "/first", "and location.href is LIVE, not a page-load snapshot");
    }
}

// A LINK LEAVES THE PAGE THROUGH run_app, and lands in the SYSTEM BROWSER.
//
// Driven through run_app rather than the browser directly, because the wiring
// is what was wrong: `ctbrowse` set browser::set_navigate_hook itself, which
// REPLACES run_app's hook rather than chaining with it, so every http:// link
// it was handed was silently swallowed. The application gets first refusal
// now and anything it does not claim goes to SDL_OpenURL - the system default
// browser, whatever that is.
void test_a_link_reaches_the_application_through_run_app() {
    std::vector<std::string> asked;
    ctbrowser::app_options options;
    options.width = 300;
    options.height = 200;
    options.max_frames = 3;
    options.real_fonts = false;
    options.network = false;
    // Claimed, so the run does not actually open a browser mid-test. Returning
    // FALSE is what sends it to the system one.
    options.on_navigate = [&asked](const std::string & url) {
        asked.push_back(url);
        return true;
    };
    options.on_ready = [](shell::browser & page) {
        // The click has to happen inside the run: run_app owns the browser.
        (void)page.frame();
        (void)page.handle(input_event::mouse_down_at(12, 12));
        (void)page.handle(input_event::mouse_up_at(12, 12));
    };
    const int code =
        ctbrowser::run_app("<body><a href='https://example.com/here'>a link</a></body>", options);
    check(code == 0, "the application ran");
    check(asked.size() == 1, "the link reached the application");
    if (!asked.empty()) { check(asked[0] == "https://example.com/here", "with its href"); }
}

// A PAGE THAT DIES MUST SAY SO. `run_app` ticked the clock and never looked at
// what came back, so a page whose callbacks throw kept rendering whatever it
// last drew - it looked FROZEN and reported nothing at all. That is how the
// Phaser invaders page hid an undefined method for an afternoon: update() threw
// on every frame and the window showed create()'s output forever.
//
// The message arrives ONCE PER DISTINCT FAULT, not once per frame. A callback
// that faults every frame at 60 Hz would otherwise bury the first and only
// useful line under thousands of copies of itself, which is the same as saying
// nothing.
void test_run_app_reports_a_script_error() {
    std::vector<std::string> reported;
    ctbrowser::app_options options;
    options.width = 200;
    options.height = 150;
    // Several frames, so a fault that repeats is given every chance to repeat.
    options.max_frames = 8;
    options.real_fonts = false;
    options.network = false;
    options.on_script_error = [&reported](const std::string & message) {
        reported.push_back(message);
    };
    // The fault is in an ANIMATION FRAME rather than in the page's top level:
    // a top-level throw is reported by load_html and was never the gap. What
    // was missing is the one that happens later, on a turn nobody is watching.
    const int code = ctbrowser::run_app(R"(<body><script>
        function spin() { window.requestAnimationFrame(spin); missingFunction(); }
        window.requestAnimationFrame(spin);
    </script></body>)",
                                        options);
    check(code == 0, "a page that throws does not take the application down");
    check(reported.size() == 1,
          "reported once, not once per frame: " + std::to_string(reported.size()));
    if (!reported.empty()) {
        check(reported[0].find("missingFunction") != std::string::npos,
              "and it names what was missing: " + reported[0]);
        check(reported[0].find("requestAnimationFrame") != std::string::npos,
              "and which kind of callback it was in: " + reported[0]);
    }
}

// AND STAYS SILENT WHEN NOTHING IS WRONG, or the hook is noise a caller learns
// to ignore - which is the same as not having it.
void test_run_app_is_silent_for_a_healthy_page() {
    std::vector<std::string> reported;
    ctbrowser::app_options options;
    options.width = 200;
    options.height = 150;
    options.max_frames = 8;
    options.real_fonts = false;
    options.network = false;
    options.on_script_error = [&reported](const std::string & message) {
        reported.push_back(message);
    };
    const int code = ctbrowser::run_app(R"(<body><script>
        var n = 0;
        function spin() { n++; window.requestAnimationFrame(spin); }
        window.requestAnimationFrame(spin);
    </script></body>)",
                                        options);
    check(code == 0, "the application ran");
    check(reported.empty(), "a healthy page reports nothing");
}

} // namespace

int main() {
    test_alert_is_recorded();
    test_alert_reaches_the_hook();
    test_location_reload_reruns_the_page();
    test_window_and_document_share_one_location();
    test_a_link_is_handed_to_the_embedder();
    test_a_fragment_scrolls_instead_of_navigating();
    test_a_page_can_read_where_a_link_went();
    test_a_link_reaches_the_application_through_run_app();
    test_run_app_reports_a_script_error();
    test_run_app_is_silent_for_a_healthy_page();
    test_location_parts_and_cookies();
    REPORT("navigation");
}
