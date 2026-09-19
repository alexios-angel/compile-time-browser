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
// THE JSON IS PARSED BY HAND rather than through the JSON builtin, for the
// reason the p5 harness states: this is the test harness, and a harness that
// depends on the thing under test to report its own results can pass because
// two bugs cancelled.

#include <cstdio>
#include <string>
#include <vector>

#include <ctbrowser.hpp>

#include "api_pawl.hpp"
#include "check.hpp"

int main() {
    const std::string bundle = read_file("vendor/phaser/phaser.js");
    std::string probes = "globalThis.__probes = [];\n";
    bool missing_probes = false;
    for (const char * path : {
             "test/corpus/phaser/phaser-api-probe/01-scene-and-rendering.js",
             "test/corpus/phaser/phaser-api-probe/02-utilities-and-plugins.js",
             "test/corpus/phaser/phaser-api-probe.js",
         }) {
        const std::string part = read_file(path);
        missing_probes |= part.empty();
        probes += part;
    }
    if (bundle.empty() || missing_probes) {
        std::printf(
            "FAIL vendor/phaser/phaser.js or test/corpus/phaser/phaser-api-probe.js is missing\n");
        ++ctbrowser_test_failures;
        REPORT("phaser_api");
    }

    ctbrowser::shell::browser page{ctbrowser::shell::browser_options{320, 240}};
    const auto add = [&page](const char * name, const std::string & text) {
        page.assets().add(
            name,
            std::vector<std::byte>{reinterpret_cast<const std::byte *>(text.data()),
                                   reinterpret_cast<const std::byte *>(text.data() + text.size())});
    };
    add("phaser.js", bundle);
    add("probe.js", probes);

    page.load_html(R"(<html><head><meta charset="utf-8">
        <script src="phaser.js"></script>
        <script src="probe.js"></script></head><body></body></html>)");
    if (!page.script_error().empty()) {
        std::printf("FAIL loading Phaser or the probes: %s\n", page.script_error().c_str());
        ++ctbrowser_test_failures;
        REPORT("phaser_api");
    }

    // The game the probes run against. CANVAS by name, so a probe that asserts
    // the renderer type is asserting something; noAudio because a headless test
    // has no business asking for a device.
    //
    // The probes run from CREATE, which is the first moment a scene is fully
    // built - `scene.add`, `scene.textures` and the camera are all in place by
    // then and not before.
    (void)page.run_script(R"JS(
        var __out = '';
        new Phaser.Game({
            type: Phaser.CANVAS, width: 200, height: 150, banner: false,
            audio: { noAudio: true },
            // ARCADE PHYSICS IS ON, or `scene.physics` does not exist and the
            // physics probes could only assert that it is absent. Gravity is
            // zero so a body moves only where a probe pushes it - a probe that
            // has to subtract gravity to check its own arithmetic is testing
            // the probe.
            physics: { default: 'arcade', arcade: { gravity: { y: 0 }, debug: false } },
            scene: {
                create: function () {
                    globalThis.__runProbes(this).then(function (json) { __out = json; });
                },
                update: function () {}
            }
        });
    )JS");
    // Enough turns for the boot textures to settle, the scene to be created and
    // every await in the probe list to resolve - the loader probe costs several
    // on its own.
    for (int frame = 0; frame < 80; ++frame) { page.tick(16); }

    std::string reported;
    page.set_alert_hook([&reported](const std::string & said) { reported = said; });
    (void)page.run_script("alert(__out);");
    if (reported.empty()) {
        // WHICH PROBE, if one of them hung. The runner records the name it is
        // on, so a report that never arrives still says where it stopped.
        std::string at;
        page.set_alert_hook([&at](const std::string & said) { at = said; });
        (void)page.run_script("alert('at=' + globalThis.__at + ' probes=' + "
                              "(globalThis.__probes ? globalThis.__probes.length : 'none'));");
        std::printf("FAIL the probes did not run: %s [%s]\n", page.script_error().c_str(),
                    at.c_str());
        ++ctbrowser_test_failures;
        REPORT("phaser_api");
    }

    ctbrowser_test::api_pawl("Phaser API", reported, "test/corpus/phaser/phaser-api.txt",
                             "tools/corpus/ratchet.py phaser api");
    REPORT("phaser_api");
}
