//===- tools/check/workloads/page-program-main.cpp - Native page host -----===//
//
// The native side of check-workload.py's script-run probe: the C++ ctcompile
// generated from one page program (a library, then the probe that calls it;
// see ctcompile/docs/native-page-programs.md), run on one page.
//
// It is compiled with -DCTCOMPILE_PAGE_PROGRAM="<generated .cpp>", whose
// `int main()` is the program's top level. That main is renamed, and this one
// loads argv[1] into a ctbrowser::page::static_page laid out at the VM
// reference's window size (400 x 300), binds it with ctnative::page_scope so
// `document` and the page views answer for it, and runs the top level. What
// the program prints on stdout is what script-run compares with the VM
// reference's console.log lines for the same page.
//
//===----------------------------------------------------------------------===//

#ifndef CTNATIVE_DOM
#define CTNATIVE_DOM 1
#endif
#include "ctcompile/CTNative/Runtime/Page.hpp"

#define main ctcompile_page_program_top_level
#include CTCOMPILE_PAGE_PROGRAM
#undef main

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

int main(int argc, char ** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s PAGE.html\n", argv[0]);
        return 2;
    }
    const std::filesystem::path path{argv[1]};
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        std::fprintf(stderr, "page program: cannot read %s\n", argv[1]);
        return 2;
    }
    const std::string html{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    ctbrowser::page::static_page page{ctbrowser::page::static_page_options{400, 300}};
    page.assets().set_base_path(path.parent_path());
    page.assets().set_confine(false);
    page.load_html(html);
    page.frame();
    const ctnative::page_scope scope{page};
    return ctcompile_page_program_top_level();
}
