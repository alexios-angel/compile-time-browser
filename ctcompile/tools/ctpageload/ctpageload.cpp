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
    // the concatenation would build an image whose hash never matches, which
    // presents as "no saving" rather than as a bug in the tool.
    std::string concatenated;
    {
        ctbrowser::browser first{ctbrowser::browser_options{800, 600}};
        first.load_html(page);
        concatenated = std::string{first.script_source()};
    }
    if (concatenated.empty()) {
        std::fprintf(stderr, "ctpageload: the page compiled no classic scripts - is its <script "
                             "src> resolvable from this directory?\n");
        return 2;
    }

    const auto compiled = ctbrowser::script::compiler::compile(concatenated);
    if (!compiled.ok) {
        std::fprintf(stderr, "ctpageload: the page's scripts do not compile: %s\n",
                     compiled.error.c_str());
        return 1;
    }
    const std::vector<std::byte> image = ctbrowser::script::write_image(compiled);
    std::printf("%zu functions, %zu bytes of source, %zu bytes of image\n",
                compiled.functions.size(), concatenated.size(), image.size());

    std::size_t compiles_without = 0;
    std::size_t compiles_with = 0;
    const double without = median_ms(3, [&] {
        ctbrowser::browser browser{ctbrowser::browser_options{800, 600}};
        browser.load_html(page);
        compiles_without = browser.scripts_compiled_from_source();
    });
    const double with_image = median_ms(3, [&] {
        ctbrowser::browser browser{ctbrowser::browser_options{800, 600}};
        browser.set_script_image(image);
        browser.load_html(page);
        compiles_with = browser.scripts_compiled_from_source();
    });

    std::printf("load_html from source  %8.2f ms   (compiled %zu times)\n", without,
                compiles_without);
    std::printf("load_html from image   %8.2f ms   (compiled %zu times)\n", with_image,
                compiles_with);
    std::printf("saved %.2f ms of %.2f - %.0f%% of the page load\n", without - with_image, without,
                100.0 * (without - with_image) / without);

    if (compiles_with != 0) {
        std::fprintf(stderr, "ctpageload: the image was REFUSED and the page compiled instead - "
                             "the saving above is not a measurement of anything\n");
        return 1;
    }
    return 0;
}
