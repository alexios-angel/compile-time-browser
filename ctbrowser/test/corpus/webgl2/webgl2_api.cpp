// How WIDE the working WebGL 2 surface is.
//
// test/corpus/webgl2/webgl2_ratchet.cpp asks how FAR a WebGL 2 page gets - one number up a
// ladder, and it stops at the first thing missing. This asks the broader
// question: of everything WebGL 2 offers, what works, what does not, and what
// is deliberately not implemented.
//
// SAME SHAPE AS test/corpus/p5/p5_api.cpp AND test/corpus/phaser/phaser_api.cpp: probes in a .js
// file, a JSON report parsed BY HAND here, and a recorded surface that may not shrink. Parsed by
// hand because this is the test harness, and a harness that depends on the thing under test to
// report its own results can pass because two bugs cancelled.
//
// THE PROBES INCLUDE THINGS EXPECTED TO FAIL, which is unusual and deliberate.
// docs/history/webgl2.md puts uniform buffer objects, 3D textures, MRT, samplers,
// queries, sync and transform feedback OUT of scope - and Babylon.js calls every
// one of them. Recording "not implemented" as a fact is better than discovering
// it later as a wrong answer, and the probe is then already written for the day
// one lands.

#include <cstdio>
#include <string>
#include <vector>

#include <ctbrowser.hpp>

#include "api_pawl.hpp"
#include "check.hpp"

int main() {
    const std::string probes = read_file("test/corpus/webgl2/webgl2-api-probe.js");
    if (probes.empty()) {
        std::printf("FAIL test/corpus/webgl2/webgl2-api-probe.js is missing\n");
        ++ctbrowser_test_failures;
        REPORT("webgl2_api");
    }

    ctbrowser::shell::browser page{ctbrowser::shell::browser_options{200, 200}};
    page.assets().add(
        "probe.js",
        std::vector<std::byte>{reinterpret_cast<const std::byte *>(probes.data()),
                               reinterpret_cast<const std::byte *>(probes.data() + probes.size())});
    page.load_html(R"(<html><head><meta charset="utf-8">
        <script src="probe.js"></script></head>
        <body><canvas id=c width=64 height=64></canvas></body></html>)");
    if (!page.script_error().empty()) {
        std::printf("FAIL loading the probes: %s\n", page.script_error().c_str());
        ++ctbrowser_test_failures;
        REPORT("webgl2_api");
    }

    // THE CONTEXT MAY BE NULL, and that is a measurement rather than an error:
    // `getContext('webgl2')` returns null today by an explicit decision. The
    // probes still run - every one of them fails, which is exactly the surface
    // this file exists to record - so the report is a complete list of what is
    // missing rather than a refusal to look.
    (void)page.run_script(R"JS(
        var __out = '';
        var __gl = document.getElementById('c').getContext('webgl2');
        __out = globalThis.__runProbes(__gl);
    )JS");

    std::string reported;
    page.set_alert_hook([&reported](const std::string & said) { reported = said; });
    (void)page.run_script("alert(__out);");
    if (reported.empty()) {
        std::printf("FAIL the probes did not run: %s\n", page.script_error().c_str());
        ++ctbrowser_test_failures;
        REPORT("webgl2_api");
    }

    ctbrowser_test::api_pawl("WebGL 2 API", reported, "test/corpus/webgl2/webgl2-api.txt",
                             "tools/corpus/ratchet.py webgl2 api");
    REPORT("webgl2_api");
}
