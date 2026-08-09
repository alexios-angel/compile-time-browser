// Fonts: which face a run is drawn in, and how it gets there.
//
// The engine used to have no answer at all - `measure_text_fn` was
// (text, size), so a page could ask for bold 20px Fira Sans and be measured in
// whatever the rasterizer felt like, then drawn in something else again. The
// interesting tests are therefore about AGREEMENT: the face layout resolved is
// the face the command carries, and the width layout measured is the width the
// rasterizer draws.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace ctbrowser;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

void check(bool ok, std::string_view what) {
    if (!ok) {
        std::printf("FAIL %s\n", std::string{what}.c_str());
        ++ctbrowser_test_failures;
    }
}

// Only the glyph-cache test reads a face off disk, and that test compiles
// away without SDL3_ttf - so this is unused, not dead, on such a build.
[[maybe_unused, nodiscard]] std::vector<std::byte> read_font(const char * path) {
    std::ifstream in{path, std::ios::binary};
    std::vector<std::byte> out;
    if (!in) { return out; }
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    out.resize(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        out[i] = static_cast<std::byte>(static_cast<unsigned char>(text[i]));
    }
    return out;
}

// Every text command the page draws, in order.
[[nodiscard]] std::vector<paint::paint_command> text_commands(browser & page) {
    std::vector<paint::paint_command> out;
    for (const auto & layer : page.layers().layers) {
        if (!layer.contents) { continue; }
        for (const auto & command : layer.contents->commands()) {
            if (command.op == paint::paint_op::text_run) { out.push_back(command); }
        }
    }
    return out;
}

[[nodiscard]] const paint::paint_command * run_saying(browser & page, std::string_view text) {
    static std::vector<paint::paint_command> held;
    held = text_commands(page);
    for (const auto & command : held) {
        if (command.text.find(text) != std::string::npos) { return &command; }
    }
    return nullptr;
}

[[nodiscard]] node_id find_id(browser & page, std::string_view want) {
    const auto txn = page.doc().read();
    const atom key = page.atoms().intern("id");
    node_id found{};
    const auto walk = [&](auto && self, node_id at) -> void {
        if (!found && txn.attribute_value(at, key) == want) { found = at; }
        for (const node_id c : txn.children(at)) { self(self, c); }
    };
    walk(walk, txn.root());
    return found;
}

// --- the face reaches the command -----------------------------------------

void test_family_weight_and_style_resolve() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<body>
      <p style="font-family: Fira Sans; font-weight: bold">bolded</p>
      <p style="font-family: 'Cousine', monospace; font-style: italic">sloped</p>
      <p style="font-weight: 300">light</p>
      <p style="font-weight: 700">heavy</p>
    </body>)");
    check(page.frame().has_value(), "the page renders");

    const paint::paint_command * bolded = run_saying(page, "bolded");
    check(bolded != nullptr, "the bold run was recorded");
    if (bolded != nullptr) {
        check(bolded->face.family == "Fira Sans", "the family reaches the command");
        check(bolded->face.bold, "and the weight");
        check(!bolded->face.italic, "which does not turn on italic");
    }

    const paint::paint_command * sloped = run_saying(page, "sloped");
    if (sloped != nullptr) {
        // The FIRST name of the list, unquoted: choosing among the alternatives
        // is layout's job, and a quoted name is the same name.
        check(sloped->face.family == "Cousine", "the first family of a list, unquoted");
        check(sloped->face.italic, "and the style");
    }

    // The numeric weights, at the 600 boundary CSS draws.
    const paint::paint_command * light = run_saying(page, "light");
    if (light != nullptr) { check(!light->face.bold, "300 is not bold"); }
    const paint::paint_command * heavy = run_saying(page, "heavy");
    if (heavy != nullptr) { check(heavy->face.bold, "700 is"); }
}

void test_the_face_inherits() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<body>
      <div style="font-family: Fira Sans; font-weight: bold">
        outer
        <span>nested</span>
        <span style="font-weight: normal">reset</span>
      </div>
    </body>)");
    check(page.frame().has_value(), "the page renders");

    // A child with no font of its own is drawn in its parent's, which is what
    // makes `body { font-family: ... }` mean anything at all.
    const paint::paint_command * nested = run_saying(page, "nested");
    check(nested != nullptr, "the nested run exists");
    if (nested != nullptr) {
        check(nested->face.family == "Fira Sans", "the family is inherited");
        check(nested->face.bold, "and so is the weight");
    }
    // ...and a child that states its own overrides only that.
    const paint::paint_command * reset = run_saying(page, "reset");
    if (reset != nullptr) {
        check(reset->face.family == "Fira Sans", "the family still comes from the parent");
        check(!reset->face.bold, "the weight it stated wins");
    }
}

void test_decoration() {
    browser page{browser_options{400, 200}};
    // The UA sheet underlines links, so this is also the check that the UA
    // stylesheet's decoration reaches the rasterizer.
    page.load_html(R"(<body>
      <a href="#">linked</a>
      <p style="text-decoration: line-through">struck</p>
      <p style="text-decoration: none">plain</p>
    </body>)");
    check(page.frame().has_value(), "the page renders");

    if (const paint::paint_command * linked = run_saying(page, "linked")) {
        check(linked->decoration == paint::text_decoration::underline, "a link is underlined");
    }
    if (const paint::paint_command * struck = run_saying(page, "struck")) {
        check(struck->decoration == paint::text_decoration::line_through, "line-through");
    }
    if (const paint::paint_command * plain = run_saying(page, "plain")) {
        check(plain->decoration == paint::text_decoration::none, "and none is none");
    }
}

void test_the_underline_is_actually_drawn() {
    // The band is the rasterizer's job, so the proof is pixels: a link's row of
    // pixels below the glyphs is its colour, and the same page without the
    // decoration has nothing there.
    const auto ink_below_text = [](std::string_view html) {
        browser page{browser_options{200, 80}};
        page.load_html(std::string{html});
        (void)page.frame();
        const auto image = page.read_pixels();
        std::size_t found = 0;
        if (image) {
            for (int y = 0; y < image->height(); ++y) {
                const auto row = image->row(y);
                for (int x = 0; x < image->width(); ++x) {
                    // The UA link colour, #0000ee.
                    if ((row[static_cast<std::size_t>(x)] & 0x00FFFFFFU) == 0x0000EEU) { ++found; }
                }
            }
        }
        return found;
    };
    const std::size_t underlined = ink_below_text("<body><a href='#'>link</a></body>");
    const std::size_t bare =
        ink_below_text("<body><a href='#' style='text-decoration: none'>link</a></body>");
    check(underlined > bare, "the underline puts ink on the page");
    check(bare > 0, "and the text itself is still drawn without it");
}

// --- layout and raster agree ----------------------------------------------

void test_layout_measures_with_the_drawing_font() {
    // font8x8 advances by 8 * scale per code point, and layout must measure
    // with THAT - a run whose recorded box is narrower than what gets drawn is
    // text that overflows its own line.
    browser page{browser_options{600, 120}};
    page.load_html("<body><p>abcdef</p></body>");
    check(page.frame().has_value(), "the page renders");
    if (const paint::paint_command * run = run_saying(page, "abcdef")) {
        const float drawn = raster::font8x8_advance("abcdef", run->font_size);
        check(run->bounds.width == drawn, "the recorded box is exactly the drawn width");
    }
}

void test_font8x8_quantises_and_says_so() {
    // font8x8 scales an 8x8 cell by an INTEGER factor - round(size/8) - so every
    // size in a bucket renders identically: 12px through 19px are all scale 2.
    // That is a property of the font rather than a rounding bug, and it is
    // asserted here so it cannot be mistaken for one. An outline backend
    // removes it.
    //
    // (The plan said "16px and 20px render identically". They do not: 20/8
    // rounds to 3. The bucket boundary is at 20px, not past it.)
    check(raster::font8x8_advance("x", 12) == raster::font8x8_advance("x", 19),
          "12px and 19px are the same in font8x8");
    check(raster::font8x8_advance("x", 16) == raster::font8x8_advance("x", 18),
          "and so are 16px and 18px");
    check(raster::font8x8_advance("x", 19) < raster::font8x8_advance("x", 20),
          "20px is the next bucket up");
}

// font8x8 SYNTHESISES bold and italic - a smear and a shear over the one set of
// bitmaps it has. Before this it had a single face, so `<b>` and `<h1>` drew
// identically to body text and the goldens could not see a font-weight bug at
// all.
//
// Counted in INK rather than compared to a reference image: the point is that
// the four styles are four different things, and that bold is heavier and
// italic is not.
void test_font8x8_has_bold_and_italic() {
    // How much ink, and WHERE. Both are needed: bold adds pixels, but a shear
    // only MOVES them - italic draws exactly as much ink as regular does, so a
    // count alone cannot tell them apart and the positions have to be hashed.
    struct drawn {
        std::size_t ink = 0;
        std::size_t shape = 0;
    };
    const auto render = [](bool bold, bool italic) {
        browser page{browser_options{300, 120}};
        const char * style = bold && italic ? "font-weight: bold; font-style: italic"
                             : bold         ? "font-weight: bold"
                             : italic       ? "font-style: italic"
                                            : "";
        page.load_html(std::string{"<body style='font-size: 32px'><p style='"} + style +
                       "'>Hamburg</p></body>");
        (void)page.frame();
        const auto image = page.read_pixels();
        drawn out;
        if (image) {
            for (int y = 0; y < image->height(); ++y) {
                const auto row = image->row(y);
                for (int x = 0; x < image->width(); ++x) {
                    if ((row[static_cast<std::size_t>(x)] & 0x00FFFFFFU) == 0x00FFFFFFU) {
                        continue;
                    }
                    ++out.ink;
                    out.shape = out.shape * 1000003u + static_cast<std::size_t>(y * 4096 + x);
                }
            }
        }
        return out;
    };

    const drawn plain = render(false, false);
    const drawn bold = render(true, false);
    const drawn italic = render(false, true);
    const drawn both = render(true, true);

    check(plain.ink > 0, "regular text draws");
    // The smear roughly doubles a one-pixel stroke, so bold is markedly darker.
    check(bold.ink > plain.ink, "bold draws more ink than regular");
    // Italic is the same amount of ink in DIFFERENT PLACES.
    check(italic.shape != plain.shape, "italic puts its pixels somewhere else");
    check(both.ink > italic.ink, "and bold italic is heavier than italic alone");
    check(both.shape != bold.shape, "and slanted, unlike bold alone");

    // THE ADVANCE DOES NOT MOVE. Layout measures with font8x8_advance and the
    // rasterizer draws with the above; a style that advanced differently from
    // how it draws would put every caret and every wrap in the wrong place.
    // The styles overhang their cell instead, which is what italics do anyway.
    check(raster::font8x8_fonts().advance("Hamburg", 32, "", true, false) ==
              raster::font8x8_fonts().advance("Hamburg", 32, "", false, false),
          "bold advances exactly as regular does");
    check(raster::font8x8_fonts().advance("Hamburg", 32, "", false, true) ==
              raster::font8x8_fonts().advance("Hamburg", 32, "", false, false),
          "and so does italic");
}

// The faces baked into the binary, where a build asked for them.
//
// Asserted as a CONTRACT rather than a fact, because it is a build option and
// this test has to mean something either way: if the build embedded them there
// are twelve, each loadable by the name use_real_fonts asks for; if it did not,
// there are none and the loader reads the directory. What must never happen is
// the in-between - `have_embedded_fonts()` saying yes while nothing registers,
// which is how a font path silently falls back and nobody notices.
void test_embedded_fonts_match_what_the_build_promised() {
    // A directory that CANNOT exist. asset_registry::load falls back to the
    // filesystem when the registry misses, and this test runs from the source
    // root where a real fonts/ sits - so asking for "fonts/..." would find the
    // file on disk and prove nothing about what was embedded.
    shell::asset_registry registry;
    const std::size_t registered = shell::register_embedded_fonts(registry, "not-a-real-directory");

    if (shell::have_embedded_fonts()) {
        check(registered == 12, "an embedding build registers all twelve faces");
        // Under the names the loader builds - stem, style, .ttf - or they are
        // registered somewhere nothing will look for them.
        check(!registry.load("not-a-real-directory/Tinos-Regular.ttf").empty(),
              "the serif regular is there");
        check(!registry.load("not-a-real-directory/Cousine-BoldItalic.ttf").empty(),
              "and the mono bold italic");
        const std::vector<std::byte> bytes =
            registry.load("not-a-real-directory/FiraSans-Bold.ttf");
        check(bytes.size() > 1000, "a face is a real file, not an empty placeholder");
    } else {
        check(registered == 0, "a build without #embed registers nothing");
        check(registry.load("not-a-real-directory/Tinos-Regular.ttf").empty(),
              "and nothing is findable");
    }
}

// --- the real font backend ------------------------------------------------
//
// Skipped, loudly, where SDL3_ttf is absent - the same shape the GPU
// test uses. A test that silently passes on a machine that cannot run it is
// worse than one that says so.

void test_real_fonts() {
    if (!raster::ttf_available()) {
        std::printf("     no SDL3_ttf in this build - real fonts skipped\n");
        return;
    }
    browser page{browser_options{400, 200}};
    // ASSERTED, not skipped: the OFL faces are checked into this repository and
    // the tests run from its root, so a failure here is a real failure. A skip
    // that cannot be told apart from a pass is how a whole feature quietly
    // stops being tested.
    check(page.use_real_fonts(), "the vendored faces load");
    check(page.has_real_fonts(), "and the backend took");

    page.load_html("<body><p>Hamburgefonstiv</p></body>");
    check(page.frame().has_value(), "the page renders");

    const paint::paint_command * run = run_saying(page, "Hamburgefonstiv");
    check(run != nullptr, "the run was recorded");
    if (run == nullptr) { return; }

    // A proportional face is NOT font8x8's fixed 8-per-character grid, and the
    // recorded box has to be the real width or the text overflows its line.
    const float grid = raster::font8x8_advance("Hamburgefonstiv", run->font_size);
    check(run->bounds.width != grid, "a real face does not measure on font8x8's grid");
    check(run->bounds.width > 0, "and it measures something");

    // It puts ANTIALIASED ink on the page: font8x8 is on or off, so a partly
    // covered pixel can only come from an outline.
    const auto image = page.read_pixels();
    check(image.has_value(), "the frame composited");
    std::size_t partial = 0;
    if (image) {
        for (int y = 0; y < image->height(); ++y) {
            const auto row = image->row(y);
            for (int x = 0; x < image->width(); ++x) {
                const std::uint32_t px = row[static_cast<std::size_t>(x)] & 0x00FFFFFFU;
                if (px != 0x000000U && px != 0xFFFFFFU) { ++partial; }
            }
        }
    }
    check(partial > 0, "the glyphs are antialiased, which font8x8 cannot be");
}

void test_real_fonts_distinguish_faces() {
    if (!raster::ttf_available()) { return; }
    const auto width_of = [](std::string_view style) {
        browser page{browser_options{600, 200}};
        check(page.use_real_fonts(), "the faces load");
        page.load_html("<body><p style='" + std::string{style} + "'>Hamburgefonstiv</p></body>");
        (void)page.frame();
        const paint::paint_command * run = run_saying(page, "Hamburgefonstiv");
        return run != nullptr ? run->bounds.width : 0.0f;
    };
    const float regular = width_of("font-family: Fira Sans");
    check(regular > 0, "regular measures something");
    // Bold is wider than regular in a real face, and a monospace family is a
    // different width again. If any of these matched, the face was being
    // ignored - which is exactly the bug this whole stage is about.
    check(width_of("font-family: Fira Sans; font-weight: bold") != regular,
          "bold measures differently from regular");
    check(width_of("font-family: Cousine") != regular, "and monospace differently again");
    check(width_of("font-family: Fira Sans; font-style: italic") > 0, "italic loads too");
}

void test_unknown_family_falls_back() {
    if (!raster::ttf_available()) { return; }
    browser page{browser_options{400, 200}};
    check(page.use_real_fonts(), "the faces load");
    // A family nobody has must still draw - in the default face, not in
    // nothing. A page asking for "Comic Sans MS" gets text.
    page.load_html("<body><p style='font-family: Nonexistent Face'>fallback</p></body>");
    check(page.frame().has_value(), "the page renders");
    if (const paint::paint_command * run = run_saying(page, "fallback")) {
        check(run->bounds.width > 0, "an unknown family still measures something");
    }
}

// CANVAS text goes through the same backend as the page around it.
//
// It did not: canvas_context::fill_text drew 8x8 bitmap cells scaled by an
// integer whatever `ctx.font` said, and measureText answered from the same
// table - so "16px Arial" advanced a monospaced 16px a glyph where real Arial
// takes about 7. That is what clipped pong's HUD: MDN positions "Lives: N" at
// canvas.width - 65, which is right for the metrics it asked for and 63px short
// of the metrics it got.
void test_canvas_text_uses_the_real_font() {
    if (!raster::ttf_available()) { return; }
    browser page{browser_options{520, 360}};
    check(page.use_real_fonts(), "the faces load");
    // pong's HUD, reproduced exactly: same canvas width, same font, same offset.
    page.load_html("<body><canvas id=c width=480 height=320></canvas><script>"
                   "var ctx = document.getElementById('c').getContext('2d');"
                   "ctx.font = '16px Arial';"
                   "ctx.fillStyle = '#000000';"
                   "console.log('w=' + ctx.measureText('Lives: 3').width);"
                   "ctx.fillText('Lives: 3', 480 - 65, 20);"
                   "</script></body>");
    check(page.frame().has_value(), "the page renders");

    // font8x8 gives exactly 8 glyphs * 8px * scale(16)=2 = 128. A real 16px
    // face is nowhere near that, and 65 of room is enough for it.
    const auto & log = page.bindings().console_output();
    check(log.size() == 1, "one console line");
    if (log.size() != 1) { return; }
    float width = 0;
    check(std::sscanf(log[0].c_str(), "w=%f", &width) == 1, "the width parses");
    check(width > 0, "the text measures something");
    check(width < 65, "and fits in the 65px pong leaves for it (font8x8 wanted 128)");

    // Now the pixels, because a width that is merely a smaller NUMBER proves
    // nothing about what was drawn. The run must start inside the HUD area and
    // must END before the canvas edge - clipping is precisely what it did not.
    const shell::canvas_context * canvas = page.canvases().find(find_id(page, "c"));
    check(canvas != nullptr, "the canvas exists");
    if (canvas == nullptr) { return; }
    const paint::bitmap * pixels = canvas->surface().get();
    check(pixels != nullptr, "it has pixels");
    if (pixels == nullptr) { return; }

    int leftmost = pixels->width;
    int rightmost = -1;
    for (int y = 0; y < pixels->height; ++y) {
        for (int x = 0; x < pixels->width; ++x) {
            if ((pixels->at(x, y) >> 24) == 0) { continue; } // untouched
            if (x < leftmost) { leftmost = x; }
            if (x > rightmost) { rightmost = x; }
        }
    }
    check(rightmost >= 0, "something was drawn");
    check(leftmost >= 480 - 65, "the run starts where the page put it");
    check(rightmost < 479, "and ends INSIDE the canvas rather than against its edge");

    // And the invariant behind all of it: what measured the text is what drew
    // it, so the ink cannot run past where the measurement said it would.
    check(static_cast<float>(rightmost) <= (480 - 65) + width + 1,
          "the ink ends where measureText said it would");
}

// The glyph cache, hit CONCURRENTLY AND COLD.
//
// Going through the browser does not test this: layout measures every run
// before raster draws it, so by the time tiles fan out the cache is warm and
// the parallel path only reads. Removing the lock and watching TSan stay silent
// is what showed that up. This drives the backend directly, from several
// threads, on glyphs nobody has asked for yet - which is the only arrangement
// where the lock is load bearing.
void test_the_glyph_cache_is_thread_safe() {
#if CTBROWSER_WITH_TTF
    if (!raster::ttf_available()) { return; }
    raster::ttf_backend fonts;
    check(fonts.ok(), "SDL3_ttf started");
    const std::vector<std::byte> regular = read_font("fonts/FiraSans-Regular.ttf");
    const std::vector<std::byte> bold = read_font("fonts/FiraSans-Bold.ttf");
    check(!regular.empty(), "the vendored face is readable");
    check(fonts.add_face("Fira Sans", false, false, regular), "the face loads");
    check(fonts.add_face("Fira Sans", true, false, bold), "and its bold");

    // Every thread measures a DIFFERENT size, so every one of them misses and
    // rasterizes - which is the state the lock exists for.
    const auto measure_all = [&fonts](int size) {
        std::string out;
        for (const char * word : {"Hamburgefonstiv", "quick brown fox", "0123456789"}) {
            out += std::to_string(
                fonts.advance(word, static_cast<float>(size), "Fira Sans", size % 2 == 0, false));
            out += ' ';
        }
        return out;
    };
    std::vector<std::string> expected;
    for (int size = 8; size < 8 + 12; ++size) { expected.push_back(measure_all(size)); }

    raster::ttf_backend concurrent;
    check(concurrent.add_face("Fira Sans", false, false, regular), "the face loads again");
    check(concurrent.add_face("Fira Sans", true, false, bold), "and its bold");
    std::vector<std::string> got(expected.size());
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        threads.emplace_back([&, i] {
            std::string out;
            for (const char * word : {"Hamburgefonstiv", "quick brown fox", "0123456789"}) {
                const int size = static_cast<int>(i) + 8;
                out += std::to_string(concurrent.advance(word, static_cast<float>(size),
                                                         "Fira Sans", size % 2 == 0, false));
                out += ' ';
            }
            got[i] = std::move(out);
        });
    }
    for (std::thread & t : threads) { t.join(); }
    check(got == expected, "twelve threads measuring cold glyphs agree with one thread");
#endif
}

// A page's OWN font, from its @font-face rule. The file is named by the page
// and loaded through the asset registry like any other resource, so a page can
// ship a face the machine has never heard of.
void test_page_font_face() {
    if (!raster::ttf_available()) { return; }
    const auto width_in = [](std::string_view family) {
        browser page{browser_options{600, 200}};
        check(page.use_real_fonts(), "the vendored faces load");
        page.load_html(
            "<head><style>@font-face { font-family: 'Press Start 2P';"
            "  src: url(\"examples/assets/fonts/PressStart2P-Regular.ttf\"); }</style></head>"
            "<body><p style='font-family: " +
            std::string{family} + "'>ARCADE</p></body>");
        (void)page.frame();
        const paint::paint_command * run = run_saying(page, "ARCADE");
        return run != nullptr ? run->bounds.width : 0.0f;
    };
    const float arcade = width_in("Press Start 2P");
    const float serif = width_in("serif");
    check(arcade > 0, "the page's own face measures something");
    // Press Start 2P is a fixed-width arcade face and Tinos is not, so if these
    // matched the @font-face file was never loaded and the request fell back.
    check(arcade != serif, "and it is not just the default face under another name");
}

void test_real_fonts_are_deterministic_and_thread_safe() {
    if (!raster::ttf_available()) { return; }
    // Tiles raster in PARALLEL and an FT_Face is not reentrant, so the glyph
    // cache is the one piece of shared mutable state in the whole text path.
    // Rendering the same page twice - once across the scheduler - must give the
    // same pixels, and TSan runs this suite.
    const auto render = [](bool parallel) {
        browser page{browser_options{800, 600}};
        check(page.use_real_fonts(), "the faces load");
        std::string html = "<body>";
        for (int i = 0; i < 40; ++i) {
            html += "<p style='font-size:" + std::to_string(12 + i % 9) + "px'>Hamburgefonstiv " +
                    std::to_string(i) + "</p>";
        }
        html += "</body>";
        page.load_html(html);
        scheduler pool;
        (void)page.frame(parallel ? &pool : nullptr);
        std::vector<std::uint32_t> pixels;
        if (const auto image = page.read_pixels()) {
            for (int y = 0; y < image->height(); ++y) {
                const auto row = image->row(y);
                pixels.insert(pixels.end(), row.begin(), row.end());
            }
        }
        return pixels;
    };
    const std::vector<std::uint32_t> once = render(false);
    check(!once.empty(), "the page rendered");
    check(render(false) == once, "the same page renders the same pixels twice");
    check(render(true) == once, "and rastering across threads changes nothing");
}

void test_real_fonts_do_not_quantise() {
    if (!raster::ttf_available()) { return; }
    // The quantisation asserted above for font8x8 is a property of THAT font.
    // An outline face has a distinct size for every pixel size, which is most
    // of the reason to want one.
    const auto width_at = [](int size) {
        browser page{browser_options{600, 200}};
        check(page.use_real_fonts(), "the faces load");
        page.load_html("<body><p style='font-size:" + std::to_string(size) +
                       "px'>Hamburgefonstiv</p></body>");
        (void)page.frame();
        const paint::paint_command * run = run_saying(page, "Hamburgefonstiv");
        return run != nullptr ? run->bounds.width : 0.0f;
    };
    check(width_at(16) != width_at(18), "16px and 18px differ with a real face");
    check(width_at(16) < width_at(24), "and bigger is bigger");
}

} // namespace

int main() {
    test_family_weight_and_style_resolve();
    test_the_face_inherits();
    test_decoration();
    test_the_underline_is_actually_drawn();
    test_layout_measures_with_the_drawing_font();
    test_font8x8_quantises_and_says_so();
    test_font8x8_has_bold_and_italic();
    test_embedded_fonts_match_what_the_build_promised();

    test_real_fonts();
    test_real_fonts_distinguish_faces();
    test_unknown_family_falls_back();
    test_canvas_text_uses_the_real_font();
    test_page_font_face();
    test_the_glyph_cache_is_thread_safe();
    test_real_fonts_are_deterministic_and_thread_safe();
    test_real_fonts_do_not_quantise();

    REPORT("fonts_basics");
}
