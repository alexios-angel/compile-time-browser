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

#include <cstdio>
#include <string>
#include <vector>

#include <ctbrowser.hpp>

#include "api_pawl.hpp"
#include "check.hpp"

int main() {
    const std::string probes = read_file("test/corpus/babylon/babylon-api-probe.js");
    if (probes.empty()) {
        std::printf("FAIL test/corpus/babylon/babylon-api-probe.js is missing\n");
        ++ctbrowser_test_failures;
        REPORT("babylon_api");
    }
    // THE BUNDLE IS OPTIONAL AT BUILD TIME, like plutosvg for the SVG tests: a
    // checkout without the corpus should still build and pass rather than fail
    // for a reason that has nothing to do with the code.
    const std::string bundle = read_file("vendor/babylon/babylon.js");
    if (bundle.empty()) {
        std::printf("SKIP babylon_api: vendor/babylon/babylon.js is missing\n");
        return 0;
    }

    ctbrowser::shell::browser page{ctbrowser::shell::browser_options{200, 200}};
    page.assets().add(
        "probe.js",
        std::vector<std::byte>{reinterpret_cast<const std::byte *>(probes.data()),
                               reinterpret_cast<const std::byte *>(probes.data() + probes.size())});
    page.assets().add(
        "babylon.js",
        std::vector<std::byte>{reinterpret_cast<const std::byte *>(bundle.data()),
                               reinterpret_cast<const std::byte *>(bundle.data() + bundle.size())});
    page.load_html(R"(<html><head><meta charset="utf-8">
        <script src="babylon.js"></script>
        <script src="probe.js"></script></head>
        <body><canvas id=c width=64 height=64></canvas></body></html>)");
    if (!page.script_error().empty()) {
        std::printf("FAIL loading the probes: %s\n", page.script_error().c_str());
        ++ctbrowser_test_failures;
        REPORT("babylon_api");
    }

    // THE RUNNER TAKES THE CANVAS, not a context: Babylon makes its own engine
    // and its own context, which is the whole point of running the probes
    // through the corpus rather than against the API directly.
    (void)page.run_script(R"JS(
        var __out = globalThis.__runProbes(document.getElementById('c'));
    )JS");

    std::string reported;
    page.set_alert_hook([&reported](const std::string & said) { reported = said; });
    (void)page.run_script("alert(__out);");
    if (reported.empty()) {
        std::printf("FAIL the probes did not run: %s\n", page.script_error().c_str());
        ++ctbrowser_test_failures;
        REPORT("babylon_api");
    }

    ctbrowser_test::api_pawl("Babylon API", reported, "test/corpus/babylon/babylon-api.txt",
                             "tools/corpus/babylon-api.py");
    REPORT("babylon_api");
}
