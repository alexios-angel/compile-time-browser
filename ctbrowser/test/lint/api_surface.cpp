//===- ctbrowser/test/lint/api_surface.cpp - Public API boundary lint -----===//
//
// Enforces the application-facing API promise: "one include, one link target,
// no SDL". A stray `#include <SDL3/SDL.h>` in an example still compiles and a
// second engine include still works, so no other test would notice either.
// This lint reads the sources an application author is meant to copy, and
// sweeps the engine for SDL outside the files allowed to use it.
//
//===----------------------------------------------------------------------===//

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

[[nodiscard]] std::string read(const std::string & path) {
    std::ifstream in{path};
    if (!in) { return {}; }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

/// Removes // and /* */ comments, which mention SDL constantly; a lint that
/// matched them would fail on its own explanations. Crude but sufficient: no
/// string literal in these files contains a comment opener.
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

/// Returns every #include that names the engine. An application may include
/// <cstdio> and friends, but must not reach past the umbrella header into the
/// subsystems: that is what "exactly one include" means.
[[nodiscard]] std::vector<std::string> engine_includes(const std::string & source) {
    std::vector<std::string> out;
    std::istringstream in{source};
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t at = line.find_first_not_of(" \t");
        // Match both <ctbrowser/...> and the umbrella <ctbrowser.hpp>, which
        // is outside that directory; otherwise the one legal include is missed.
        const bool names_engine = line.find("<ctbrowser/") != std::string::npos ||
                                  line.find("<ctbrowser.hpp>") != std::string::npos;
        if (at != std::string::npos && names_engine && line.compare(at, 8, "#include") == 0) {
            out.push_back(line.substr(at));
        }
    }
    return out;
}

/// Checks one file an application author would copy: no SDL, and exactly one
/// engine include, `<ctbrowser.hpp>`. A failure means the API has regressed
/// for everyone.
void check_application_source(const std::string & path) {
    const std::string raw = read(path);
    check(!raw.empty(), path + ": readable");
    if (raw.empty()) { return; }
    const std::string source = strip_comments(raw);

    check(source.find("SDL3/") == std::string::npos, path + ": no SDL header");
    check(source.find("SDL_") == std::string::npos, path + ": no SDL symbol");

    const auto includes = engine_includes(source);
    check(includes.size() == 1, path + ": exactly one engine include");
    if (includes.size() == 1) {
        check(includes[0] == "#include <ctbrowser.hpp>",
              path + ": and it is `#include <ctbrowser.hpp>`");
    }
}

} // namespace

int main() {
    check_application_source("examples/demos/counter.cpp");
    check_application_source("tools/ctbrowse/ctbrowse.cpp");
    check_application_source("test/package/main.cpp");

    // The engine may use SDL only in the files listed in `allowed`. Anywhere
    // else breaks "the engine is SDL-free", the property that makes headless
    // rendering and this whole test suite possible.
    //
    // Sweep every .cpp/.hpp under include/ and lib/ rather than a hand-kept
    // list, so a new file that uses SDL is caught too. Paths are relative to
    // ctbrowser/, the WORKING_DIRECTORY ctest gives every test.
    const std::set<std::string> allowed = {
        "include/ctbrowser/app/app.hpp", // the window, the event loop, audio, image decode
        "lib/App/app/internal.hpp",      //   and its implementation: the includes
        "lib/App/app/hosts.cpp",         //   and the host interface, the hosts,
        "lib/App/app/run.cpp",           //   and the loop that drives one
        "lib/Raster/text/ttf.cpp",       // real fonts, through SDL3_ttf - see its header
    };
    std::size_t swept = 0;
    for (const char * tree : {"include", "lib"}) {
        for (const auto & entry : std::filesystem::recursive_directory_iterator{tree}) {
            if (!entry.is_regular_file()) { continue; }
            const std::string ext = entry.path().extension().string();
            if (ext != ".cpp" && ext != ".hpp") { continue; }
            std::string path = entry.path().generic_string();
            if (allowed.contains(path)) { continue; }
            ++swept;
            const std::string raw = read(path);
            check(!raw.empty(), path + ": readable");
            check(strip_comments(raw).find("SDL") == std::string::npos,
                  path + ": engine source is SDL-free");
        }
    }
    // A sweep that visited nothing would pass without having looked.
    check(swept > 20, "the sweep actually visited the engine");

    // Every allowed file must exist; a name that moved would silently allow
    // nothing.
    for (const std::string & path : allowed) {
        check(std::filesystem::exists(path), path + ": the SDL exception list is current");
    }

    REPORT("api_surface");
}
