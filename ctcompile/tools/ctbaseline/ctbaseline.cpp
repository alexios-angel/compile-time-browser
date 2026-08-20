// WHAT A PACKAGED APPLICATION PAYS BEFORE IT DRAWS ANYTHING, per stage, as JSON.
//
// Phase 0's performance baseline. ctcompile's entire claim is that the work
// below happens at BUILD time instead of at startup, so this is the number that
// claim will be measured against - and a claim with no before is not a claim.
//
// IT MEASURES THE STARTUP STAGES AND SAYS SO. HTML parsing, CSS parsing,
// filing a stylesheet into the engine, JavaScript parsing and bytecode
// compilation, and executing a program's top level. It does NOT measure style
// resolution, layout, paint, raster or a first frame: those are what the
// runtime keeps doing after startup and what Principle 6 says a compiler must
// not freeze, and the engine already has benchmarks for them
// (ctbrowser/benchmarks/). Measuring them here would produce a number nobody
// should try to improve by compiling.
//
// A BASELINE WITHOUT ITS CONFIGURATION IS UNUSABLE SIX MONTHS LATER, so the
// compiler, its flags, the CPU and the corpus sizes are recorded beside every
// timing. The output is JSON on stdout; tools/check/baseline.py stores it.
#include <ctbrowser/core/atom.hpp>
#include <ctbrowser/dom/document.hpp>
#include <ctbrowser/dom/html.hpp>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/program_image.hpp>
#include <ctbrowser/script/vm.hpp>
#include <ctbrowser/style/css/parser.hpp>
#include <ctbrowser/style/engine.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using clock_type = std::chrono::steady_clock;

[[nodiscard]] std::string read_file(const std::string & path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) { return {}; }
    std::ostringstream all;
    all << in.rdbuf();
    return all.str();
}

// The median of several runs, not the mean and not the best.
//
// The mean is dragged by one scheduler hiccup and the best is a number the
// machine produced once and will not reproduce - a baseline is for comparing
// against later, so it has to be the number that comes back tomorrow.
template <typename F> [[nodiscard]] double median_ms(int runs, F && once) {
    std::vector<double> taken;
    taken.reserve(static_cast<std::size_t>(runs));
    for (int i = 0; i < runs; ++i) {
        const auto start = clock_type::now();
        once();
        const auto end = clock_type::now();
        taken.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }
    std::ranges::sort(taken);
    return taken[taken.size() / 2];
}

struct stage {
    std::string name;
    double ms;
    std::size_t bytes;
    std::size_t produced; // nodes, rules, functions - whatever the stage makes
    // WHETHER THE STAGE ACTUALLY DID THE WORK. A timing for a stage that
    // stopped at its first error is not a fast stage, it is a missing
    // measurement that LOOKS like a fast one - running p5's top level
    // "in 0.036 ms" means the program raised immediately, not that a 4.5 MB
    // bundle installed itself in microseconds. Recorded rather than inferred.
    bool completed = true;
    std::string stopped_because;
};

// A VM error carries a stack trace, and a stack trace carries newlines - which
// are not legal inside a JSON string. Escaping is three lines and forgetting it
// produces a file that every consumer rejects at load, which is exactly what
// happened the first time this ran.
[[nodiscard]] std::string json_escaped(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 16);
    for (const char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char slot[7];
                std::snprintf(slot, sizeof slot, "\\u%04x", static_cast<unsigned>(c));
                out += slot;
            } else {
                out += c;
            }
        }
    }
    return out;
}

void print_json(std::string_view corpus, const std::vector<stage> & stages) {
    std::printf("    {\n      \"corpus\": \"%.*s\",\n      \"stages\": [\n",
                static_cast<int>(corpus.size()), corpus.data());
    for (std::size_t i = 0; i < stages.size(); ++i) {
        std::printf("        { \"stage\": \"%s\", \"ms\": %.3f, \"input_bytes\": %zu, "
                    "\"produced\": %zu, \"completed\": %s",
                    stages[i].name.c_str(), stages[i].ms, stages[i].bytes, stages[i].produced,
                    stages[i].completed ? "true" : "false");
        if (!stages[i].completed) {
            std::printf(", \"stopped_because\": \"%s\"",
                        json_escaped(stages[i].stopped_because).c_str());
        }
        std::printf(" }%s\n", i + 1 == stages.size() ? "" : ",");
    }
    std::printf("      ]\n    }");
}

} // namespace

int main(int argc, char ** argv) {
    // Paths are given by the caller so this tool has no opinion about where the
    // tree is - it is run from the repository root by tools/check/baseline.py.
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: ctbaseline <name>=<path> ...\n"
                     "  a .html, .css or .js file per argument; the extension picks the stages\n");
        return 2;
    }

    std::printf("{\n  \"runs\": [\n");
    for (int i = 1; i < argc; ++i) {
        const std::string arg{argv[i]};
        const std::size_t eq = arg.find('=');
        const std::string name = eq == std::string::npos ? arg : arg.substr(0, eq);
        const std::string path = eq == std::string::npos ? arg : arg.substr(eq + 1);
        const std::string source = read_file(path);
        if (source.empty()) {
            std::fprintf(stderr, "ctbaseline: cannot read %s\n", path.c_str());
            return 1;
        }

        std::vector<stage> stages;
        const std::string_view ext = std::string_view{path}.substr(path.rfind('.') + 1);

        if (ext == "html") {
            std::size_t nodes = 0;
            const double ms = median_ms(5, [&] {
                ctbrowser::atom_table atoms;
                ctbrowser::document doc{atoms};
                (void)ctbrowser::parse_html(doc, source);
                nodes = doc.node_count();
            });
            stages.push_back(stage{"html_parse_and_dom_build", ms, source.size(), nodes, true, {}});
        } else if (ext == "css") {
            std::size_t rules = 0;
            const double parse = median_ms(5, [&] {
                ctbrowser::atom_table atoms;
                const auto sheet = ctbrowser::style::css::parse_stylesheet(source, atoms);
                rules = sheet.rules.size();
            });
            stages.push_back(stage{"css_parse", parse, source.size(), rules, true, {}});

            // add_sheet PARSES AND THEN FILES, so this is the parse above plus
            // the selector compilation and bucketing - which is the half a
            // compiled style program would replace.
            std::size_t filed = 0;
            const double file_it = median_ms(5, [&] {
                ctbrowser::atom_table atoms;
                ctbrowser::style::engine styles{atoms};
                styles.add_sheet(source, 1);
                filed = styles.rule_count();
            });
            stages.push_back(stage{"css_parse_and_file", file_it, source.size(), filed, true, {}});
        } else if (ext == "js") {
            std::size_t functions = 0;
            const double compile = median_ms(3, [&] {
                const auto program = ctbrowser::script::compiler::compile(source);
                functions = program.functions.size();
            });
            stages.push_back(
                stage{"js_parse_and_compile", compile, source.size(), functions, true, {}});

            // AND THE WHOLE POINT: loading the same program from an image
            // instead of compiling it.
            //
            // TIMED AT THE SAME BOUNDARY as the compile above - the program is
            // constructed AND DESTROYED inside the lambda either way. That is
            // not a detail: tearing down babylon's ~255,000 containers is part
            // of the 264 ms being compared against, and an image stage that
            // skipped it would be measured against a padded baseline.
            {
                const auto whole = ctbrowser::script::compiler::compile(source);
                const auto image = ctbrowser::script::write_image(whole);
                if (!image.empty()) {
                    std::size_t loaded = 0;
                    bool ok = true;
                    const double load = median_ms(3, [&] {
                        auto back = ctbrowser::script::load_image(image);
                        ok = back.ok;
                        loaded = back.value.functions.size();
                    });
                    stages.push_back(
                        stage{"js_image_load", load, image.size(), loaded, ok,
                              ok ? std::string{} : std::string{"the image did not load"}});
                    // A GUARD, NOT A STATISTIC: an image that quietly loads
                    // fewer functions than the compile produced would read as a
                    // win. The count has to match or the number means nothing.
                    if (loaded != whole.functions.size()) {
                        stages.back().completed = false;
                        stages.back().stopped_because = "the image loaded " +
                                                        std::to_string(loaded) +
                                                        " functions, the "
                                                        "compile produced " +
                                                        std::to_string(whole.functions.size());
                    }
                }
            }

            // AND RUNNING THE TOP LEVEL, which for a UMD bundle is where the
            // library installs itself - the work a packaged application still
            // has to do even once its code is native.
            const auto program = ctbrowser::script::compiler::compile(source);
            if (!program.ok) {
                stages.push_back(stage{"js_parse_and_compile", compile, source.size(), functions,
                                       false, program.error});
            } else {
                bool ran = true;
                std::string why;
                const double run = median_ms(3, [&] {
                    ctbrowser::script::context cx;
                    const auto result = cx.run(program);
                    ran = result.ok;
                    if (!result.ok) { why = result.error; }
                });
                stages.push_back(stage{"js_run_top_level", run, source.size(),
                                       program.functions.size(), ran, why});
            }
        }

        if (i > 1) { std::printf(",\n"); }
        print_json(name, stages);
    }
    std::printf("\n  ]\n}\n");
    return 0;
}
