//===- ctcompile/tools/ctbaseline/ctbaseline.cpp - Startup-cost baseline --===//
//
// Measures what a packaged application pays before it draws anything, per
// stage, as JSON. ctcompile moves this work from startup to build time, so
// these numbers are what the compiler is compared against.
//
// It measures the startup stages: HTML parsing, CSS parsing, filing a
// stylesheet into the engine, JavaScript parsing and bytecode compilation,
// loading the same program from an image, and executing a program's top
// level. It does not measure style resolution, layout, paint, raster or a
// first frame: those depend on the window and on runtime state, so they stay
// runtime work that ctcompile does not precompute, and ctbrowser/benchmarks/
// already measures them.
//
// The output is JSON on stdout with timings only. tools/check/baseline.py runs
// this tool and adds the compiler, build commands, machine and corpus hashes,
// because a baseline without its configuration cannot be compared later.
//
//===----------------------------------------------------------------------===//
#include <ctcompile/Support/Manifest.hpp>
#include <ctcompile/Support/Measure.hpp>

#include <ctbrowser/core/atom.hpp>
#include <ctbrowser/dom/document.hpp>
#include <ctbrowser/dom/html.hpp>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/program_image.hpp>
#include <ctbrowser/script/vm.hpp>
#include <ctbrowser/style/css/parser.hpp>
#include <ctbrowser/style/engine.hpp>

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ctcompile::median_ms;
using ctcompile::read_file;

/// One measured startup stage, as printed in the JSON output.
struct stage {
    std::string name;
    double ms;
    std::size_t bytes;
    std::size_t produced; // nodes, rules, functions: whatever the stage makes
    /// Whether the stage actually did the work. A stage that stopped at its
    /// first error is a missing measurement that looks like a fast one: p5's
    /// top level "running in 0.036 ms" means the program threw immediately,
    /// not that a 4.5 MB bundle installed itself in microseconds. Recorded
    /// rather than inferred.
    bool completed = true;
    /// The error that stopped the stage when `completed` is false.
    std::string stopped_because;
};

/// Prints one corpus's stages as a JSON object (part of the "runs" array).
void print_json(std::string_view corpus, const std::vector<stage> & stages) {
    std::printf("    {\n      \"corpus\": \"%.*s\",\n      \"stages\": [\n",
                static_cast<int>(corpus.size()), corpus.data());
    for (std::size_t i = 0; i < stages.size(); ++i) {
        std::printf("        { \"stage\": \"%s\", \"ms\": %.3f, \"input_bytes\": %zu, "
                    "\"produced\": %zu, \"completed\": %s",
                    stages[i].name.c_str(), stages[i].ms, stages[i].bytes, stages[i].produced,
                    stages[i].completed ? "true" : "false");
        if (!stages[i].completed) {
            // A VM error carries a stack trace, and a stack trace carries
            // newlines, which are not legal inside a JSON string.
            std::printf(", \"stopped_because\": %s",
                        ctcompile::json_string(stages[i].stopped_because).c_str());
        }
        std::printf(" }%s\n", i + 1 == stages.size() ? "" : ",");
    }
    std::printf("      ]\n    }");
}

} // namespace

int main(int argc, char ** argv) {
    // Paths are given by the caller so this tool assumes nothing about where
    // the tree is; tools/check/baseline.py runs it from the repository root.
    llvm::cl::OptionCategory category("ctbaseline options");
    llvm::cl::list<std::string> inputs(llvm::cl::Positional, llvm::cl::OneOrMore,
                                       llvm::cl::desc("<name=path> ..."), llvm::cl::cat(category));
    llvm::cl::HideUnrelatedOptions(category);
    if (!llvm::cl::ParseCommandLineOptions(
            argc, argv, "Measure startup stages for .html, .css and .js inputs\n", &llvm::errs())) {
        return 2;
    }

    std::printf("{\n  \"runs\": [\n");
    bool first = true;
    for (const std::string & arg : inputs) {
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

            // add_sheet parses and then files, so this is the parse above plus
            // selector compilation and bucketing, the half a compiled style
            // program would replace.
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

            // One untimed compile serves the image stage and the top-level run.
            const auto program = ctbrowser::script::compiler::compile(source);

            // Loading the same program from an image instead of compiling it.
            //
            // Timed at the same boundary as the compile above: the program is
            // constructed and destroyed inside the lambda either way. Teardown
            // is part of both timings; freeing a large bundle's containers is
            // a real share of the compile number, and an image stage that
            // skipped it would look faster than it is.
            {
                const auto image = ctbrowser::script::write_image(program);
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
                    // A guard, not a statistic: an image that silently loads
                    // fewer functions than the compile produced would look
                    // like a win, so the counts must match.
                    if (loaded != program.functions.size()) {
                        stages.back().completed = false;
                        stages.back().stopped_because = "the image loaded " +
                                                        std::to_string(loaded) +
                                                        " functions, the "
                                                        "compile produced " +
                                                        std::to_string(program.functions.size());
                    }
                }
            }

            // Running the top level, which for a UMD bundle is where the
            // library installs itself: work a packaged application still does
            // even once its code is native.
            //
            // A program that failed to compile has no top level to run, so the
            // compile stage is recorded again, this time as incomplete with the
            // compile error.
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

        if (!first) { std::printf(",\n"); }
        first = false;
        print_json(name, stages);
    }
    std::printf("\n  ]\n}\n");
    return 0;
}
