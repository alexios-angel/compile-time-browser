//===- ctcompile/test/CSS/EmitRoundTrip.cpp - Compiled CSS round trip -----===//
//
// The build runs ctcss-translate over features.css and over Bootstrap's
// stylesheet and compiles the C++ it wrote into this test. Each generated
// constant must materialize into exactly the stylesheet ctbrowser's parser
// makes from the same file: a field the emitter drops, misspells or rounds
// shows up as a difference. The generated files are compiled under the same
// warnings as hand-written code, which is the other half of the check.
//
//   ctcompile-test-css_emit <features.css> <bootstrap.css>
//
//===----------------------------------------------------------------------===//

#include "bootstrap.hpp"
#include "features.hpp"

#include <ctbrowser/style/css/compiled.hpp>
#include <ctbrowser/style/css/parser.hpp>

#include "check.hpp"

#include <cstdio>
#include <string>
#include <string_view>

namespace css = ctbrowser::style::css;

namespace {

/// The first table of `a` and `b` that differs, or empty.
[[nodiscard]] std::string first_difference(const css::stylesheet & a, const css::stylesheet & b) {
    const auto table = [](std::string_view name, const auto & x, const auto & y) -> std::string {
        if (x.size() != y.size()) {
            return std::string{name} + " (size " + std::to_string(x.size()) + " vs " +
                   std::to_string(y.size()) + ")";
        }
        for (std::size_t i = 0; i < x.size(); ++i) {
            if (!(x[i] == y[i])) { return std::string{name} + "[" + std::to_string(i) + "]"; }
        }
        return {};
    };
    if (a.pool != b.pool) { return "pool"; }
    for (std::string found :
         {table("namespaces", a.namespaces, b.namespaces), table("imports", a.imports, b.imports),
          table("tokens", a.tokens, b.tokens), table("values", a.values, b.values),
          table("selectors", a.selectors, b.selectors),
          table("declarations", a.declarations, b.declarations), table("rules", a.rules, b.rules),
          table("keyframes", a.keyframes, b.keyframes),
          table("conditions", a.conditions, b.conditions), table("layers", a.layers, b.layers),
          table("scopes", a.scopes, b.scopes), table("containers", a.containers, b.containers)}) {
        if (!found.empty()) { return found; }
    }
    if (!(a == b)) { return "a member not listed here"; }
    return {};
}

void expect_generated_matches(const char * path, const css::compiled_sheet & generated) {
    const std::string source = read_file(path);
    check(!source.empty(), std::string{path} + " reads");
    if (source.empty()) { return; }
    ctbrowser::atom_table atoms;
    const css::stylesheet parsed = css::parse_stylesheet(source, atoms);
    const css::stylesheet loaded = css::materialize(generated, atoms);
    const std::string where = first_difference(parsed, loaded);
    check(where.empty(), std::string{path} + ": the generated sheet differs at " + where);
    check(generated.selector_count == parsed.selectors.size(),
          std::string{path} + ": the sheet's own selectors come first");
}

} // namespace

int main(int argc, char ** argv) {
    if (argc != 3) {
        std::printf("FAIL usage: ctcompile-test-css_emit <features.css> <bootstrap.css>\n");
        return 1;
    }
    expect_generated_matches(argv[1], ctcss::features);
    expect_generated_matches(argv[2], ctcss::bootstrap);
    // Bootstrap is here on purpose: its pool is past the 64 KiB a single
    // literal may hold, so the generated file has to cut it into chunks.
    std::size_t bootstrap_pool = 0;
    for (const std::string_view chunk : ctcss::bootstrap.pool) { bootstrap_pool += chunk.size(); }
    check(bootstrap_pool > 65536 && ctcss::bootstrap.pool.size() > 2,
          "bootstrap.css's pool is past 64 KiB and written as several chunks");
    REPORT("css_emit");
}
