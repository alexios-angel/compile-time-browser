// How WIDE the working Phaser surface is.
//
// tests/phaser_ratchet.cpp asks how FAR the bundle gets - it reaches 10/10, so
// a scene boots, runs and paints. One number hides a great deal: every corpus
// page in this tree rendered for months while `getProgramParameter` answered 0
// to ACTIVE_UNIFORMS, because a hand-written page asks for uniforms by name and
// only a library enumerates. This is the second question, and the p5 side of it
// found five real bugs in one run.
//
// SAME SHAPE AS tests/p5_api.cpp, deliberately: the probes live in
// tests/phaser-api-probe.js, the runner reports JSON, this parses it by hand,
// and tests/phaser-api.txt records which probes pass. A probe that used to pass
// and now does not fails the test; a newly passing one is reported and recorded
// only by tools/phaser-api.py --advance.
//
// THE JSON IS PARSED BY HAND rather than through the JSON builtin, for the
// reason the p5 harness states: this is the test harness, and a harness that
// depends on the thing under test to report its own results can pass because
// two bugs cancelled.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <ctbrowser.hpp>

#include "check.hpp"

namespace {

[[nodiscard]] std::string read_file(const std::string & path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) { return {}; }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

struct outcome {
    std::vector<std::string> passed;
    std::vector<std::string> failed;
    std::vector<std::string> skipped;
};

// Pull one `"..."` list out of `"key": [ ... ]`. Enough for the shape the
// runner emits, and it does not pretend to be a JSON parser.
[[nodiscard]] std::vector<std::string> field(const std::string & json, const std::string & key) {
    std::vector<std::string> out;
    const std::size_t at = json.find("\"" + key + "\"");
    if (at == std::string::npos) { return out; }
    std::size_t i = json.find('[', at);
    if (i == std::string::npos) { return out; }
    ++i;
    // A `]` ENDS THE ARRAY ONLY OUTSIDE A STRING. The p5 harness learned this
    // the expensive way: it found the first one anywhere, so a probe whose
    // failure MESSAGE contained a bracket truncated the list - silently, and
    // only for the probes that sorted after it. Five failures vanished from a
    // report that still looked complete. Written correctly here from the start.
    while (i < json.size() && json[i] != ']') {
        if (json[i] != '"') {
            ++i; // a comma or whitespace between items
            continue;
        }
        std::string item;
        for (++i; i < json.size() && json[i] != '"'; ++i) {
            if (json[i] == '\\' && i + 1 < json.size()) { ++i; }
            item += json[i];
        }
        ++i; // past the closing quote
        out.push_back(item);
    }
    return out;
}

[[nodiscard]] std::string name_of(const std::string & failure) {
    const std::size_t colon = failure.find(": ");
    return colon == std::string::npos ? failure : failure.substr(0, colon);
}

} // namespace

int main() {
    const std::string bundle = read_file("examples/assets/phaser/phaser.js");
    const std::string probes = read_file("tests/phaser-api-probe.js");
    if (bundle.empty() || probes.empty()) {
        std::printf(
            "FAIL examples/assets/phaser/phaser.js or tests/phaser-api-probe.js is missing\n");
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

    outcome now;
    now.passed = field(reported, "passed");
    now.failed = field(reported, "failed");
    now.skipped = field(reported, "skipped");

    const std::size_t total = now.passed.size() + now.failed.size() + now.skipped.size();
    // EVERY PROBE MUST BE ACCOUNTED FOR. The runner reports how many it was
    // given and every one lands in exactly one of the three lists, so a
    // mismatch means the report lost some whatever it says about the rest.
    const std::vector<std::string> declared = field(reported, "count");
    const std::size_t expected =
        declared.empty() ? total : static_cast<std::size_t>(std::atoll(declared[0].c_str()));
    if (expected != total) {
        std::printf("FAIL the report lost probes: %zu declared, %zu accounted for\n", expected,
                    total);
        ++ctbrowser_test_failures;
    }
    std::printf("     Phaser API: %zu probes - %zu pass, %zu fail, %zu skipped\n", total,
                now.passed.size(), now.failed.size(), now.skipped.size());
    for (const std::string & failure : now.failed) { std::printf("     !! %s\n", failure.c_str()); }

    // --- the pawl ----------------------------------------------------------
    const std::string record = read_file("tests/phaser-api.txt");
    if (record.empty()) {
        std::printf("FAIL tests/phaser-api.txt is missing - it is the recorded surface\n");
        ++ctbrowser_test_failures;
        REPORT("phaser_api");
    }
    std::set<std::string> recorded;
    {
        std::istringstream lines{record};
        std::string line;
        while (std::getline(lines, line)) {
            if (line.empty() || line[0] == '#') { continue; }
            recorded.insert(line);
        }
    }
    const std::set<std::string> passing{now.passed.begin(), now.passed.end()};

    std::vector<std::string> lost;
    for (const std::string & was : recorded) {
        if (!passing.contains(was)) { lost.push_back(was); }
    }
    if (!lost.empty()) {
        std::printf("FAIL %zu probe(s) that used to pass now do not:\n", lost.size());
        for (const std::string & name : lost) {
            std::printf("     - %s\n", name.c_str());
            for (const std::string & failure : now.failed) {
                if (name_of(failure) == name) { std::printf("       %s\n", failure.c_str()); }
            }
        }
        ++ctbrowser_test_failures;
    }

    std::vector<std::string> gained;
    for (const std::string & name : now.passed) {
        if (!recorded.contains(name)) { gained.push_back(name); }
    }
    if (!gained.empty()) {
        std::printf("     ADVANCE: %zu newly passing. Run tools/phaser-api.py --advance\n",
                    gained.size());
        for (const std::string & name : gained) { std::printf("       + %s\n", name.c_str()); }
    }

    REPORT("phaser_api");
}
