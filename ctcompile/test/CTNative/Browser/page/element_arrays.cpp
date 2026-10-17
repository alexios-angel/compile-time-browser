//===- element_arrays.cpp - element_arrays.js on either engine ------------===//
//
// Loads a page and element_arrays.js, calls element_arrays_report() and
// prints its answer.
// Built twice by cmake/NativeWorkloads.cmake (ctcompile_page_workload): over
// the interpreter, and with CTBROWSER_TEST_NATIVE over a static page and the
// C++ ctcompile generated from the script. element_arrays.test diffs the two.
//
//   ctcompile-test-element_arrays[-native] PAGE.html SCRIPT.js
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
    const auto engine = ctbrowser_test::make_engine(page);
    if (const std::string error = engine->load(argv[2]); !error.empty()) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    std::printf("%s\n", engine->call("element_arrays_report").c_str());
    return 0;
}
