//===- dom_reads.cpp - dom-reads.js on either engine ----------------------===//
//
// Loads a page and dom-reads.js, calls dom_reads(FROM) and prints its answer,
// or `threw: <message>` when the call throws. Built
// twice by cmake/NativeWorkloads.cmake, as bootstrap_layout is: over the
// interpreter, and with CTBROWSER_TEST_NATIVE over a static page and the C++
// ctcompile generated from the script. dom-reads.test diffs the two.
//
//   ctcompile-test-dom-reads[-native] PAGE.html SCRIPT.js [FROM]
//
//===----------------------------------------------------------------------===//

#include "engine.hpp"

#include <cstdio>
#include <string>

#ifdef CTBROWSER_TEST_NATIVE
using fixture_page = ctbrowser::page::static_page;
using fixture_options = ctbrowser::page::static_page_options;
#else
using fixture_page = ctbrowser::shell::browser;
using fixture_options = ctbrowser::shell::browser_options;
#endif

int main(int argc, char ** argv) {
    if (argc != 3 && argc != 4) {
        std::fprintf(stderr, "usage: %s PAGE.html SCRIPT.js [FROM]\n", argv[0]);
        return 2;
    }
    const std::string html = read_file(argv[1]);
    if (html.empty()) {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    fixture_page page{fixture_options{400, 300}};
    page.load_html(html);
    // Layout has to have run before clientWidth is asked.
    page.frame();
    const auto engine = ctbrowser_test::make_engine(page);
    if (const std::string error = engine->load(argv[2]); !error.empty()) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    std::printf("%s\n", engine->call("dom_reads", {argc == 4 ? argv[3] : "0"}).c_str());
    return 0;
}
