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
    // report that still looked complete.
    while (i < json.size() && json[i] != ']') {
        if (json[i] != '"') {
            ++i;
            continue;
        }
        std::string item;
        for (++i; i < json.size() && json[i] != '"'; ++i) {
            if (json[i] == '\\' && i + 1 < json.size()) { ++i; }
            item += json[i];
        }
        ++i;
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
    std::printf("     WebGL 2 API: %zu probes - %zu pass, %zu fail, %zu skipped\n", total,
                now.passed.size(), now.failed.size(), now.skipped.size());
    for (const std::string & failure : now.failed) { std::printf("     !! %s\n", failure.c_str()); }

    // --- the pawl ----------------------------------------------------------
    const std::string record = read_file("test/corpus/webgl2/webgl2-api.txt");
    if (record.empty()) {
        std::printf(
            "FAIL test/corpus/webgl2/webgl2-api.txt is missing - it is the recorded surface\n");
        ++ctbrowser_test_failures;
        REPORT("webgl2_api");
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
        std::printf("     ADVANCE: %zu newly passing. Run tools/corpus/webgl2-api.py --advance\n",
                    gained.size());
        for (const std::string & name : gained) { std::printf("       + %s\n", name.c_str()); }
    }

    REPORT("webgl2_api");
}
