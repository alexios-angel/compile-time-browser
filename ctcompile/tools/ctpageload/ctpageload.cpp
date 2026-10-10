//===- ctcompile/tools/ctpageload/ctpageload.cpp - Page-load benchmark ----===//
//
// Measures what program images save on a whole page load, the number a user
// of this compiler cares about. Loading one compiled program from an image is
// about four times faster than compiling it; this tool measures what that
// saves in `load_html`, where script compilation competes with HTML, CSS,
// style, layout and everything else a page does before it draws.
//
// It exits non-zero if the images were not used, because a run that silently
// fell back to compiling would report a saving of zero and look like a result.
//
//===----------------------------------------------------------------------===//
#include <ctbrowser.hpp>
#include <ctcompile/Support/Measure.hpp>

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

using ctcompile::median_ms;
using ctcompile::read_file;

/// Loads the page once from `images` and returns how many scripts still had
/// to be compiled from source. It is a named, non-inlined function so
/// callgrind can collect only inside it:
///
///   valgrind --tool=callgrind --collect-atstart=no \
///            --toggle-collect=ctpageload_load_from_images ... ctpageload page.html
///
/// This tool builds the images it hands over, so the compile it exists to avoid
/// happens in the same process, and profiling the whole run would measure that
/// compile rather than the load.
[[gnu::noinline]] std::size_t ctpageload_load_from_images(
    const std::string & page, const std::vector<std::vector<std::byte>> & images) {
    ctbrowser::browser browser{ctbrowser::browser_options{800, 600}};
    for (const auto & one : images) { (void)browser.add_script_image(one); }
    browser.load_html(page);
    return browser.scripts_compiled_from_source();
}

} // namespace

int main(int argc, char ** argv) {
    llvm::cl::OptionCategory category("ctpageload options");
    llvm::cl::opt<std::string> input(llvm::cl::Positional, llvm::cl::Required,
                                     llvm::cl::desc("<page.html>"), llvm::cl::cat(category));
    llvm::cl::HideUnrelatedOptions(category);
    if (!llvm::cl::ParseCommandLineOptions(
            argc, argv, "Measure image loading; run where the page's script sources resolve\n",
            &llvm::errs())) {
        return 2;
    }
    const std::string page = read_file(input.getValue());
    if (page.empty()) {
        std::fprintf(stderr, "ctpageload: cannot read %s\n", input.c_str());
        return 2;
    }

    // Ask the browser which scripts it compiles rather than reproducing the
    // rule here. A page has any number of classic scripts (p5-basic.html has
    // three: an inline flag, the src'd bundle and the sketch), and a tool that
    // guessed wrong would build images whose hashes never match, which would
    // look like "no saving" rather than a bug in the tool.
    std::vector<std::string> scripts;
    {
        ctbrowser::browser first{ctbrowser::browser_options{800, 600}};
        first.load_html(page);
        scripts = first.script_sources();
    }
    if (scripts.empty()) {
        std::fprintf(stderr, "ctpageload: the page compiled no classic scripts - is its <script "
                             "src> resolvable from this directory?\n");
        return 2;
    }

    // One image per script: the page's own sketch and the library beside it are
    // separate artefacts with separate keys.
    std::vector<std::vector<std::byte>> images;
    std::size_t functions = 0, source_bytes = 0, image_bytes = 0;
    for (const std::string & text : scripts) {
        const auto compiled = ctbrowser::script::compiler::compile(text);
        if (!compiled.ok) {
            std::fprintf(stderr, "ctpageload: a script does not compile: %s\n",
                         compiled.error.c_str());
            return 1;
        }
        images.push_back(ctbrowser::script::write_image(compiled));
        functions += compiled.functions.size();
        source_bytes += text.size();
        image_bytes += images.back().size();
    }
    std::printf("%zu classic scripts, %zu functions, %zu bytes of source, %zu bytes of image\n",
                scripts.size(), functions, source_bytes, image_bytes);
    for (std::size_t i = 0; i < scripts.size(); ++i) {
        std::printf("  script %zu: %9zu B of source -> %9zu B of image\n", i, scripts[i].size(),
                    images[i].size());
    }

    std::size_t compiles_without = 0;
    std::size_t compiles_with = 0;
    std::size_t compiles_edited = 0;
    const double without = median_ms(3, [&] {
        ctbrowser::browser browser{ctbrowser::browser_options{800, 600}};
        browser.load_html(page);
        compiles_without = browser.scripts_compiled_from_source();
    });
    const double with_images =
        median_ms(3, [&] { compiles_with = ctpageload_load_from_images(page, images); });

    std::printf("load_html from source  %8.2f ms   (compiled %zu of %zu scripts)\n", without,
                compiles_without, scripts.size());
    std::printf("load_html from images  %8.2f ms   (compiled %zu of %zu scripts)\n", with_images,
                compiles_with, scripts.size());
    std::printf("saved %.2f ms of %.2f - %.0f%% of the page load\n", without - with_images, without,
                100.0 * (without - with_images) / without);

    if (compiles_with != 0) {
        std::fprintf(stderr,
                     "ctpageload: %zu image(s) were REFUSED and the page compiled "
                     "instead - the saving above is not a measurement of anything\n",
                     compiles_with);
        return 1;
    }

    // One image per script pays off when one script changes: hand over every
    // image except the last script's (what a developer who edited their sketch
    // has) and measure what the page still avoids recompiling.
    if (scripts.size() > 1) {
        const double edited = median_ms(3, [&] {
            ctbrowser::browser browser{ctbrowser::browser_options{800, 600}};
            for (std::size_t i = 0; i + 1 < images.size(); ++i) {
                (void)browser.add_script_image(images[i]);
            }
            browser.load_html(page);
            compiles_edited = browser.scripts_compiled_from_source();
        });
        std::printf("\nEDIT THE LAST SCRIPT, then load the page:\n");
        std::printf("  before this change  %8.2f ms   (every script recompiled)\n", without);
        std::printf("  one script rebuilt  %8.2f ms   (compiled %zu of %zu)\n", edited,
                    compiles_edited, scripts.size());
        std::printf("  %.1fx\n", without / edited);
        if (compiles_edited != 1) {
            std::fprintf(stderr,
                         "ctpageload: expected exactly one script to recompile, got %zu - the "
                         "figure above is not a measurement of anything\n",
                         compiles_edited);
            return 1;
        }
    }
    return 0;
}
