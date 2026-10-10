// How WIDE the working Phaser surface is.
//
// test/corpus/phaser/phaser_ratchet.cpp asks how FAR the bundle gets - it reaches 10/10, so
// a scene boots, runs and paints. One number hides a great deal: every corpus
// page in this tree rendered for months while `getProgramParameter` answered 0
// to ACTIVE_UNIFORMS, because a hand-written page asks for uniforms by name and
// only a library enumerates. This is the second question, and the p5 side of it
// found five real bugs in one run.
//
// SAME SHAPE AS test/corpus/p5/p5_api.cpp, deliberately: the probes live in
// test/corpus/phaser/phaser-api-probe.js, the runner reports JSON, this parses it by hand,
// and test/corpus/phaser/phaser-api.txt records which probes pass. A probe that used to pass
// and now does not fails the test; a newly passing one is reported and recorded
// only by tools/corpus/ratchet.py phaser api --advance.
//
// phaser_api.html is the page the probes run in, and phaser_api.js the harness
// that starts them and hands back the report, called through support/engine.hpp.
//
// THE JSON IS PARSED BY HAND rather than through the JSON builtin, for the
// reason the p5 harness states: this is the test harness, and a harness that
// depends on the thing under test to report its own results can pass because
// two bugs cancelled.

#include <cstdio>
#include <string>

#include <ctbrowser.hpp>

#include "api_pawl.hpp"
#include "check.hpp"
#include "engine.hpp"

int main() {
    bool missing = read_file("vendor/phaser/phaser.js").empty();
    for (const char * path : {
             "test/corpus/phaser/phaser-api-probe/01-scene-and-rendering.js",
             "test/corpus/phaser/phaser-api-probe/02-utilities-and-plugins.js",
             "test/corpus/phaser/phaser-api-probe.js",
         }) {
        missing |= read_file(path).empty();
    }
    if (missing) {
        std::printf(
            "FAIL vendor/phaser/phaser.js or test/corpus/phaser/phaser-api-probe.js is missing\n");
        ++ctbrowser_test_failures;
        REPORT("phaser_api");
    }

    ctbrowser::shell::browser page{ctbrowser::shell::browser_options{320, 240}};
    // The page lists the bundle, the harness and the probes by their paths from
    // test/corpus/phaser/.
    page.assets().set_base_path("test/corpus/phaser");
    // UNCONFINED, explicitly: `../../../vendor/` climbs out of the base path,
    // which a confined registry refuses. Trusted in-tree fixture - never do
    // this for a page from the network.
    page.assets().set_confine(false);
    page.load_html(read_file("test/corpus/phaser/phaser_api.html"));
    if (!page.script_error().empty()) {
        std::printf("FAIL loading Phaser or the probes: %s\n", page.script_error().c_str());
        ++ctbrowser_test_failures;
        REPORT("phaser_api");
    }

    // The game the probes run against; phaser_api.js says how it is set up.
    const auto engine = ctbrowser_test::make_engine(page);
    (void)engine->call("phaser_api_start");
    // Enough turns for the boot textures to settle, the scene to be created and
    // every await in the probe list to resolve - the loader probe costs several
    // on its own.
    for (int frame = 0; frame < 80; ++frame) { page.tick(16); }

    const std::string reported = engine->call("phaser_api_report");
    if (reported.empty()) {
        // WHICH PROBE, if one of them hung. The runner records the name it is
        // on, so a report that never arrives still says where it stopped.
        const std::string at = engine->call("phaser_api_at");
        std::printf("FAIL the probes did not run: %s [%s]\n", page.script_error().c_str(),
                    at.c_str());
        ++ctbrowser_test_failures;
        REPORT("phaser_api");
    }

    ctbrowser_test::api_pawl("Phaser API", reported, "test/corpus/phaser/phaser-api.txt",
                             "tools/corpus/ratchet.py phaser api");
    REPORT("phaser_api");
}
