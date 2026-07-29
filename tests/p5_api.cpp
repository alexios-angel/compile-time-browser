// How much of p5.js works.
//
// tests/p5_ratchet.cpp measures how FAR the bundle gets: it climbs a ladder and
// records one number. This measures how WIDE the working surface is - it calls
// as much of p5's 417-function API as can be run headlessly and records which
// calls pass.
//
// The two ask different questions and the difference is the point. The ratchet
// read 12/12 - loads, constructs, runs setup, runs draw, paints - for days
// while `colorMode(HSB)` was broken, because nothing on the ladder called it.
// A wide, shallow probe found five real bugs in one run.
//
// THE PAWL, same as the ratchet's and for the same reason:
//
//   a recorded PASS that now fails   FAIL. Something regressed.
//   a new pass                       print ADVANCE; recording is deliberate.
//   a failure that was already known listed, not fatal - it is the work queue.
//
// A test that edits its own expectations cannot fail, so `tools/p5-api.py
// --advance` is the only thing that writes tests/p5-api.txt.
//
// The probes live in tests/p5-api-probe.js rather than in a string here, so
// adding one needs no rebuild and `tools/p5-api.py --coverage` can list which
// of the bundle's `fn.*` no probe mentions - that list is what to write next.

#include <ctbrowser.hpp>

#include "check.hpp"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

[[nodiscard]] std::string read_file(const std::string & path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) { return {}; }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// The probe result, as the runner in the JS reports it. Parsed by hand rather
// than through the JSON builtin: this is the test harness, and a harness that
// depends on the thing under test to report its own results is a harness that
// can pass because two bugs cancelled.
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
    const std::size_t open = json.find('[', at);
    const std::size_t close = json.find(']', open);
    if (open == std::string::npos || close == std::string::npos) { return out; }
    std::size_t i = open;
    while (true) {
        const std::size_t start = json.find('"', i);
        if (start == std::string::npos || start > close) { break; }
        std::string item;
        std::size_t j = start + 1;
        for (; j < json.size() && json[j] != '"'; ++j) {
            // The runner's messages can contain an escaped quote or backslash.
            if (json[j] == '\\' && j + 1 < json.size()) { ++j; }
            item += json[j];
        }
        out.push_back(item);
        i = j + 1;
    }
    return out;
}

[[nodiscard]] std::string name_of(const std::string & failure) {
    const std::size_t colon = failure.find(": ");
    return colon == std::string::npos ? failure : failure.substr(0, colon);
}

} // namespace

int main() {
    const std::string bundle = read_file("examples/assets/p5.js");
    const std::string probes = read_file("tests/p5-api-probe.js");
    if (bundle.empty() || probes.empty()) {
        std::printf("FAIL examples/assets/p5.js or tests/p5-api-probe.js is missing\n");
        ++ctbrowser_test_failures;
        REPORT("p5_api");
    }

    ctbrowser::shell::browser page{ctbrowser::shell::browser_options{400, 400}};
    const auto add = [&](const char * name, const std::string & text) {
        page.assets().add(
            name,
            std::vector<std::byte>{reinterpret_cast<const std::byte *>(text.data()),
                                   reinterpret_cast<const std::byte *>(text.data() + text.size())});
    };
    add("p5.js", bundle);
    add("probe.js", probes);

    // IS_MINIFIED, like the ratchet: the probe measures the drawing surface,
    // and the translator fetch is a different question answered elsewhere.
    page.load_html(R"(<html><head><meta charset="utf-8">
        <script>var IS_MINIFIED = true;</script>
        <script src="p5.js"></script>
        <script src="probe.js"></script></head><body></body></html>)");
    if (!page.script_error().empty()) {
        std::printf("FAIL loading p5 or the probes: %s\n", page.script_error().c_str());
        ++ctbrowser_test_failures;
        REPORT("p5_api");
    }

    // The sketch the probes run against. Small, because several read every
    // pixel back and the probe is run on every build.
    (void)page.run_script(R"(
        var __out = '';
        new p5(function (s) {
          s.setup = function () {
            s.createCanvas(24, 24);
            s.noLoop();
            s.pixelDensity(1);
            __out = globalThis.__runProbes(s);
          };
          s.draw = function () {};
        });
    )");
    for (int frame = 0; frame < 5; ++frame) { page.tick(16); }

    std::string reported;
    page.set_alert_hook([&reported](const std::string & said) { reported = said; });
    (void)page.run_script("alert(__out);");
    if (reported.empty()) {
        std::printf("FAIL the probes did not run: %s\n", page.script_error().c_str());
        ++ctbrowser_test_failures;
        REPORT("p5_api");
    }

    outcome now;
    now.passed = field(reported, "passed");
    now.failed = field(reported, "failed");
    now.skipped = field(reported, "skipped");

    const std::size_t total = now.passed.size() + now.failed.size() + now.skipped.size();
    std::printf("     p5 API: %zu probes - %zu pass, %zu fail, %zu skipped\n", total,
                now.passed.size(), now.failed.size(), now.skipped.size());
    for (const std::string & failure : now.failed) { std::printf("     !! %s\n", failure.c_str()); }

    // --- the pawl ----------------------------------------------------------
    const std::string record = read_file("tests/p5-api.txt");
    if (record.empty()) {
        std::printf("FAIL tests/p5-api.txt is missing - it is the recorded surface\n");
        ++ctbrowser_test_failures;
        REPORT("p5_api");
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
        std::printf("     ADVANCE: %zu newly passing. Run tools/p5-api.py --advance\n",
                    gained.size());
        for (const std::string & name : gained) { std::printf("       + %s\n", name.c_str()); }
    }

    REPORT("p5_api");
}
