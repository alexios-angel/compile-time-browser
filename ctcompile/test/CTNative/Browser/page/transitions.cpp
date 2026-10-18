//===- transitions.cpp - transitions.js on either engine ------------------===//
//
// Loads transitions.html and transitions.js, moves the page clock in steps and
// prints what transitions_report() answers after each. The script's timers
// dispatch the transitionend events, so moving the clock is all the test does.
// Built twice by cmake/NativeWorkloads.cmake, as timers.cpp is: over the
// interpreter, where the browser's tick runs the due timers, and with
// CTBROWSER_TEST_NATIVE over a static page, where the native runtime's timer
// table does (Runtime/Timers.hpp). transitions.test diffs the two.
//
//   ctcompile-test-transitions[-native] PAGE.html SCRIPT.js
//
//===----------------------------------------------------------------------===//

#include "engine.hpp"

#include <cstdio>
#include <string>

#ifdef CTBROWSER_TEST_NATIVE
#define CTNATIVE_DOM 1
#include "ctcompile/CTNative/Runtime/Page.hpp"
#include "ctcompile/CTNative/Runtime/Timers.hpp"
using fixture_page = ctbrowser::page::static_page;
using fixture_options = ctbrowser::page::static_page_options;
#else
using fixture_page = ctbrowser::shell::browser;
using fixture_options = ctbrowser::shell::browser_options;
#endif

namespace {

/// Moves the page clock by `ms` and runs the timers that came due, as the host
/// does between frames.
void advance(fixture_page & page, double ms) {
#ifdef CTBROWSER_TEST_NATIVE
    // Handlers run with the page bound, as the program's entries do.
    const ctnative::page_scope scope{page};
    (void)ctnative::timers::program().advance(ms);
#else
    (void)page.tick(ms);
#endif
}

} // namespace

int main(int argc, char ** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s PAGE.html SCRIPT.js\n", argv[0]);
        return 2;
    }
    const std::string html = read_file(argv[1]);
    if (html.empty()) {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    fixture_page page{fixture_options{400, 300}};
    page.load_html(html);
    page.frame();
    const auto engine = ctbrowser_test::make_engine(page);
    if (const std::string error = engine->load(argv[2]); !error.empty()) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    std::printf("%s\n", engine->call("transitions_report").c_str());
    for (const double ms : {5.0, 5.0, 10.0, 10.0, 10.0}) {
        advance(page, ms);
        std::printf("+%g: %s\n", ms, engine->call("transitions_report").c_str());
    }
    return 0;
}
