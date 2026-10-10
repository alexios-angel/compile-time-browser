//===- ctbrowser/test/support/api_pawl.hpp - API-surface ratchet check ----===//
//
// The API-surface ratchet shared by the *_api.cpp corpus tests (p5, Phaser,
// Babylon, webgl2). Each test loads a bundle, runs the probes in its
// <corpus>-api-probe.js and gets one JSON report back (through
// support/engine.hpp's call() in p5, Phaser and Babylon, through alert() in
// webgl2). A "pawl" stops a ratchet slipping back: every probe recorded as
// passing must still pass (a regression is a FAIL), a newly passing probe
// prints ADVANCE so the record can be moved forward, and known failures are
// the work queue.
//
// The report is parsed by hand rather than through the JSON builtin: a harness
// that depends on the code under test to report its own results can pass
// because two bugs cancelled.
//
// tools/corpus/ratchet.py reads the `probes -`, `!!` and `+` lines by regex,
// so the printed text is part of the contract.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstdio>
#include <cstdlib>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "check.hpp"

namespace ctbrowser_test {

/// The probe names a report puts in each of its three lists.
struct outcome {
    std::vector<std::string> passed;
    std::vector<std::string> failed;
    std::vector<std::string> skipped;
};

/// Pulls one `"..."` list out of `"key": [ ... ]`. Enough for the shape the
/// runner emits; it does not pretend to be a JSON parser.
[[nodiscard]] inline std::vector<std::string> field(const std::string & json,
                                                    const std::string & key) {
    std::vector<std::string> out;
    const std::size_t at = json.find("\"" + key + "\"");
    if (at == std::string::npos) { return out; }
    std::size_t i = json.find('[', at);
    if (i == std::string::npos) { return out; }
    ++i;
    // A `]` ends the array only outside a string: a failure message that
    // contains a bracket must not truncate the list.
    while (i < json.size() && json[i] != ']') {
        if (json[i] != '"') {
            ++i; // a comma or whitespace between items
            continue;
        }
        std::string item;
        for (++i; i < json.size() && json[i] != '"'; ++i) {
            // The runner's messages can contain an escaped quote or backslash.
            if (json[i] == '\\' && i + 1 < json.size()) { ++i; }
            item += json[i];
        }
        ++i; // past the closing quote
        out.push_back(item);
    }
    return out;
}

/// The probe name in a "name: message" failure entry.
[[nodiscard]] inline std::string name_of(const std::string & failure) {
    const std::size_t colon = failure.find(": ");
    return colon == std::string::npos ? failure : failure.substr(0, colon);
}

/// Checks one probe report against the recorded surface and prints the
/// summary. `reported` is the runner's JSON; `label` names the corpus in the
/// summary line ("p5 API"); `record_path` is the recorded surface and `tool`
/// the script that advances it. Failures bump ctbrowser_test_failures; the
/// caller still calls REPORT().
inline void api_pawl(const char * label, const std::string & reported, const char * record_path,
                     const char * tool) {
    outcome now;
    now.passed = field(reported, "passed");
    now.failed = field(reported, "failed");
    now.skipped = field(reported, "skipped");

    const std::size_t total = now.passed.size() + now.failed.size() + now.skipped.size();
    // Every probe must be accounted for. The runner reports how many it was
    // given and each lands in exactly one of the three lists, so a mismatch
    // means the report lost some, whatever it says about the rest.
    const std::vector<std::string> declared = field(reported, "count");
    const std::size_t expected =
        declared.empty() ? total : static_cast<std::size_t>(std::atoll(declared[0].c_str()));
    if (expected != total) {
        std::printf("FAIL the report lost probes: %zu declared, %zu accounted for\n", expected,
                    total);
        ++ctbrowser_test_failures;
    }
    std::printf("     %s: %zu probes - %zu pass, %zu fail, %zu skipped\n", label, total,
                now.passed.size(), now.failed.size(), now.skipped.size());
    for (const std::string & failure : now.failed) { std::printf("     !! %s\n", failure.c_str()); }

    // The pawl: compare against the recorded surface.
    const std::string record = read_file(record_path);
    if (record.empty()) {
        std::printf("FAIL %s is missing - it is the recorded surface\n", record_path);
        ++ctbrowser_test_failures;
        return;
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
        std::printf("     ADVANCE: %zu newly passing. Run %s --advance\n", gained.size(), tool);
        for (const std::string & name : gained) { std::printf("       + %s\n", name.c_str()); }
    }
}

} // namespace ctbrowser_test
