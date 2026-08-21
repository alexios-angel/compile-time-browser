// WHAT AN IMAGE IS WORTH ON A WHOLE PAGE LOAD, which is the only number a user
// of this compiler would care about.
//
// The per-stage baseline says loading a program from an image is about four
// times faster than compiling it. That is a stage; this loads a real page both
// ways and reports the difference in `load_html`, where parsing competes with
// HTML, CSS, style, layout and everything else a page does before it draws.
//
// It exits non-zero if the image was NOT used, because a run that quietly fell
// back to compiling would report a saving of zero and look like a finding.
#include <ctbrowser.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using clock_type = std::chrono::steady_clock;

[[nodiscard]] std::string read_file(const std::string & path) {
    std::ifstream in{path, std::ios::binary};
    std::ostringstream all;
    all << in.rdbuf();
    return all.str();
}

template <typename F> [[nodiscard]] double median_ms(int runs, F && once) {
    std::vector<double> taken;
    taken.reserve(static_cast<std::size_t>(runs));
    for (int i = 0; i < runs; ++i) {
        const auto start = clock_type::now();
        once();
        taken.push_back(
            std::chrono::duration<double, std::milli>(clock_type::now() - start).count());
    }
    std::ranges::sort(taken);
    return taken[taken.size() / 2];
}

} // namespace

int main(int argc, char ** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: ctpageload <page.html>\n"
                             "  run from a directory where the page's <script src> resolves\n");
        return 2;
    }
    const std::string page = read_file(argv[1]);
    if (page.empty()) {
        std::fprintf(stderr, "ctpageload: cannot read %s\n", argv[1]);
        return 2;
    }

    // ASK THE BROWSER WHAT IT COMPILES rather than reproducing it here. A page
    // has as many classic scripts as it likes - p5-basic.html has three, an
    // inline flag, the src'd bundle and the sketch - and a tool that guessed
    // the rule would build images whose hashes never match, which presents as
    // "no saving" rather than as a bug in the tool.
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

    // ONE IMAGE PER SCRIPT, which is the whole point: the page's own sketch and
    // the library beside it are separate artefacts with separate keys.
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
    const double with_images = median_ms(3, [&] {
        ctbrowser::browser browser{ctbrowser::browser_options{800, 600}};
        for (const auto & one : images) { (void)browser.add_script_image(one); }
        browser.load_html(page);
        compiles_with = browser.scripts_compiled_from_source();
    });

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

    // AND THE NUMBER THE SPLIT WAS FOR. Hand over every image EXCEPT the last
    // script's - which is what a developer who edited their own sketch has -
    // and see what the page still avoids recompiling.
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
