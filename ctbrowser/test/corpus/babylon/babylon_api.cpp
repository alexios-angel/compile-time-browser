// HOW WIDE THE WORKING BABYLON SURFACE IS - the companion to
// test/corpus/babylon/babylon_ratchet.cpp, which measures how FAR one scene gets.
//
// SAME SHAPE AS test/corpus/p5/p5_api.cpp, test/corpus/phaser/phaser_api.cpp AND
// test/corpus/webgl2/webgl2_api.cpp: probes in a .js file, a JSON report parsed BY HAND here, and
// a recorded surface that may not shrink. Parsed by hand because this is the test harness, and a
// harness that depends on the thing under test to report its own results can pass because two bugs
// cancelled.
//
// THE PROBES INCLUDE THINGS EXPECTED TO FAIL, deliberately. docs/plans/babylon.md
// measured five of them - textures sampling black, post-processes blanking the
// canvas, wireframe drawing nothing, PBRMaterial throwing, and no GUI in the
// bundle - and recording "not implemented" as a fact beats discovering it later
// as a wrong answer. test/corpus/babylon/babylon-api.txt is what says which are which.
//
// THE PAGE IS babylon_api.html: the bundle, the probes, and babylon_api.js, the
// harness that runs them and returns the report.

#include <cstdio>
#include <filesystem>
#include <string>

#include <ctbrowser.hpp>

#include "api_pawl.hpp"
#include "check.hpp"
#include "engine.hpp"

int main() {
    const std::string html = read_file("test/corpus/babylon/babylon_api.html");
    if (html.empty()) {
        std::printf("FAIL test/corpus/babylon/babylon_api.html is missing\n");
        ++ctbrowser_test_failures;
        REPORT("babylon_api");
    }
    // THE BUNDLE IS OPTIONAL AT BUILD TIME, like plutosvg for the SVG tests: a
    // checkout without the corpus should still build and pass rather than fail
    // for a reason that has nothing to do with the code.
    if (!std::filesystem::exists("vendor/babylon/babylon.js")) {
        std::printf("SKIP babylon_api: vendor/babylon/babylon.js is missing\n");
        return 0;
    }

    ctbrowser::shell::browser page{ctbrowser::shell::browser_options{200, 200}};
    // WHERE THE PAGE'S SCRIPTS RESOLVE FROM: its own directory, so
    // `../../../vendor/babylon/babylon.js` is the bundle and the probes are the
    // files beside the page.
    page.assets().set_base_path("test/corpus/babylon");
    // UNCONFINED, explicitly: `../../../vendor/` climbs out of the base path,
    // which a confined registry refuses. Trusted in-tree fixture - never do
    // this for a page from the network.
    page.assets().set_confine(false);
    page.load_html(html);
    if (!page.script_error().empty()) {
        std::printf("FAIL loading the probes: %s\n", page.script_error().c_str());
        ++ctbrowser_test_failures;
        REPORT("babylon_api");
    }

    const std::string reported = ctbrowser_test::make_engine(page)->call("babylon_api_report");
    // A REPORT IS A JSON OBJECT. Anything else - "threw: ...", "<no answer>" -
    // is the call failing before the probes could report.
    if (!reported.starts_with('{')) {
        std::printf("FAIL the probes did not run: %s\n", reported.c_str());
        ++ctbrowser_test_failures;
        REPORT("babylon_api");
    }

    ctbrowser_test::api_pawl("Babylon API", reported, "test/corpus/babylon/babylon-api.txt",
                             "tools/corpus/ratchet.py babylon api");
    REPORT("babylon_api");
}
