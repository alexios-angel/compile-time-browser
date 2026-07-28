// The application-facing API claim, enforced rather than asserted.
//
// "One import, one link target, no SDL" is easy to state and easy to lose: a
// stray `#include <SDL3/SDL.h>` in an example still compiles, and a second
// `import` still works. Both would quietly undo the thing this API exists for,
// and neither would fail any other test.
//
// So this test reads the sources an application author is meant to copy and
// checks them. It is a lint, and it lives here because the property it defends
// is a design promise, not a formatting rule.

#include "check.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void check(bool ok, std::string_view what) {
    if (!ok) {
        std::printf("FAIL %s\n", std::string{what}.c_str());
        ++ctbrowser_test_failures;
    }
}

[[nodiscard]] std::string read(const std::string & path) {
    std::ifstream in{path};
    if (!in) { return {}; }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// Comments talk ABOUT SDL constantly - that is most of what the design notes in
// this tree are for. A lint that matched them would fail on its own
// explanations, so strip them first. Crude but sufficient: no string literal in
// these files contains a comment opener.
[[nodiscard]] std::string strip_comments(const std::string & source) {
    std::string out;
    out.reserve(source.size());
    for (std::size_t i = 0; i < source.size();) {
        if (source.compare(i, 2, "//") == 0) {
            while (i < source.size() && source[i] != '\n') { ++i; }
            continue;
        }
        if (source.compare(i, 2, "/*") == 0) {
            i += 2;
            while (i + 1 < source.size() && source.compare(i, 2, "*/") != 0) { ++i; }
            i = std::min(i + 2, source.size());
            continue;
        }
        out += source[i++];
    }
    return out;
}

[[nodiscard]] std::vector<std::string> import_lines(const std::string & source) {
    std::vector<std::string> out;
    std::istringstream in{source};
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t at = line.find_first_not_of(" \t");
        if (at != std::string::npos && line.compare(at, 7, "import ") == 0) {
            out.push_back(line.substr(at));
        }
    }
    return out;
}

// Files an application author would copy. If any of these needs SDL or a second
// import, the API has regressed for everyone.
void check_application_source(const std::string & path) {
    const std::string raw = read(path);
    check(!raw.empty(), path + ": readable");
    if (raw.empty()) { return; }
    const std::string source = strip_comments(raw);

    check(source.find("SDL3/") == std::string::npos, path + ": no SDL header");
    check(source.find("SDL_") == std::string::npos, path + ": no SDL symbol");

    const auto imports = import_lines(source);
    check(imports.size() == 1, path + ": exactly one import");
    if (imports.size() == 1) {
        check(imports[0] == "import ctbrowser;", path + ": and it is `import ctbrowser;`");
    }
}

} // namespace

int main() {
    check_application_source("examples/counter.cpp");
    check_application_source("examples/ctbrowse.cpp");
    check_application_source("tests/package/main.cpp");

    // The engine may use SDL only where it is ALLOWED to. Everywhere else,
    // "the engine is SDL-free" - the claim that makes headless rendering and
    // this whole test suite possible - has broken.
    //
    // A SWEEP of src/, not a hand-written list. The list version could not
    // catch a NEW file that used SDL, and it did not: raster/ttf.cppm was added
    // and this test stayed green because nobody had added it to the list.
    const std::set<std::string> allowed = {
        "src/shell/app.cppm",  // the window, the event loop, audio, image decode
        "src/shell/app.cpp",   //   and its implementation
        "src/raster/ttf.cppm", // real fonts, through SDL3_ttf - see its header
        "src/gpu/device.cppm", // the SDL_GPUDevice backend
        "src/gpu/select.cppm", "src/gpu/gpu.cppm",
    };
    std::size_t swept = 0;
    for (const auto & entry : std::filesystem::recursive_directory_iterator{"src"}) {
        if (!entry.is_regular_file()) { continue; }
        const std::string ext = entry.path().extension().string();
        if (ext != ".cppm" && ext != ".cpp" && ext != ".hpp") { continue; }
        std::string path = entry.path().generic_string();
        if (allowed.contains(path)) { continue; }
        ++swept;
        const std::string raw = read(path);
        check(!raw.empty(), path + ": readable");
        check(strip_comments(raw).find("SDL") == std::string::npos,
              path + ": engine module is SDL-free");
    }
    // If the sweep found nothing it is not passing, it is not looking.
    check(swept > 20, "the sweep actually visited the engine");

    // And every file claiming the exception has to exist, or the list is
    // carrying a name that moved and is silently allowing nothing.
    for (const std::string & path : allowed) {
        check(std::filesystem::exists(path), path + ": the SDL exception list is current");
    }

    REPORT("api_surface");
}
