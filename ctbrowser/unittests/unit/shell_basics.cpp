// ctbrowser.shell: the engine assembled, and what a frame is allowed to skip.
//
// Every earlier test checked one subsystem. This checks that they compose into
// something that behaves like a browser - and the claims worth pinning are the
// ones about WORK NOT DONE, because those are the whole point of the
// architecture and the only ones a screenshot cannot show:
//
//   a scroll        re-composites, and does not re-layout or re-raster
//   an idle frame   does nothing at all
//   a resize        re-lays-out, and does not re-parse
//   a new document  starts clean, with no rules left over from the last one
//
// Plus the things a browser is simply expected to do: apply the UA stylesheet,
// hit-test through scroll, and render the same bytes twice.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/program_image.hpp>
#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include "dom_probe.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

using namespace ctbrowser;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser::shell::input_event;
using ctbrowser_test::find_id;

namespace {

void check(bool ok, std::string_view what) {
    if (!ok) {
        std::printf("FAIL %s\n", std::string{what}.c_str());
        ++ctbrowser_test_failures;
    }
}

constexpr std::string_view demo_page = R"(
<!doctype html><html><head><title>A Title</title><style>
.card { background-color: #ffffff; padding: 8px }
#hero { background-color: #103070; color: #ffffff; padding: 12px }
</style></head><body>
<div id=hero><h1>Heading</h1></div>
<div class=card><p>Some text that is long enough to wrap onto more than one line at a narrow width.</p></div>
<div class=card><p>A second card, so the page is taller than a short viewport and can scroll.</p></div>
<div class=card><p>A third card for the same reason.</p></div>
<div class=card><p>And a fourth.</p></div>
</body></html>)";

// How many tiles the renderer has drawn. The evidence for "this frame did no
// raster" has to come from the backend, not from a stopwatch.
[[nodiscard]] std::size_t raster_calls(browser & page) {
    const raster::software_backend * backend =
        const_cast<raster::renderer &>(page.rendering_with()).get_if<raster::software_backend>();
    return backend == nullptr ? 0 : backend->raster_calls();
}

// --- the UA stylesheet ----------------------------------------------------

void test_ua_stylesheet_applies() {
    browser page{browser_options{400, 300}};
    page.load_html("<html><body><h1>Big</h1><p>Small</p><script>var x = 1;</script></body></html>");
    check(page.frame().has_value(), "the page renders");

    // An <h1> is 32px and a <p> is 16px because the UA sheet says so, not
    // because anything in the document does. Without it every document is one
    // undifferentiated size, which is what "unstyled" actually looks like.
    const layout::fragment & root = page.fragments();
    float widest_glyph_row = 0;
    const auto walk = [&](auto && self, const layout::fragment & f) -> void {
        if (!f.text.empty()) { widest_glyph_row = std::max(widest_glyph_row, f.bounds.height); }
        for (const auto & c : f.children) { self(self, c); }
    };
    walk(walk, root);
    check(widest_glyph_row >= 32, "the h1 renders larger than body text (UA font-size)");
    check(page.content_height() > 40, "and the document has real height");
}

void test_script_and_style_render_nothing() {
    browser page{browser_options{400, 300}};
    page.load_html("<html><head><style>p { color: red }</style></head>"
                   "<body><script>var secret = 'do not render me';</script>"
                   "<p>visible</p></body></html>");
    check(page.frame().has_value(), "the page renders");

    std::string all_text;
    const auto walk = [&](auto && self, const layout::fragment & f) -> void {
        all_text += f.text;
        for (const auto & c : f.children) { self(self, c); }
    };
    walk(walk, page.fragments());
    // The classic failure of a browser without a UA sheet: it renders its own
    // scripts and stylesheets as page text.
    check(all_text.find("secret") == std::string::npos, "script source is not rendered");
    check(all_text.find("color: red") == std::string::npos, "stylesheet text is not rendered");
    check(all_text.find("visible") != std::string::npos, "...but the page content is");
}

void test_title_is_extracted() {
    browser page{browser_options{200, 200}};
    page.load_html(demo_page);
    check(page.title() == "A Title", "the document title is read");
}

// --- what a frame skips ---------------------------------------------------

void test_an_idle_frame_does_nothing() {
    browser page{browser_options{400, 300}};
    page.load_html(demo_page);
    check(page.frame().has_value(), "the first frame renders");
    const std::size_t after_first = raster_calls(page);
    check(after_first > 0, "and it rastered something");

    check(page.frame().has_value(), "an idle frame renders");
    // A browser that repaints when nothing changed is a browser that never
    // idles. This is the cheapest possible check that it does.
    check(raster_calls(page) == after_first, "an idle frame rasters NOTHING");
}

void test_a_scroll_only_recomposites() {
    browser page{browser_options{400, 200}};
    page.load_html(demo_page);
    check(page.frame().has_value(), "the first frame renders");
    const std::size_t after_first = raster_calls(page);
    check(page.max_scroll() > 0, "the page is taller than the viewport");

    page.scroll_by(20);
    check(page.scroll_y() == 20, "the scroll took effect");
    check(page.frame().has_value(), "the scrolled frame renders");
    // THE claim of the whole architecture: the PAGE's tiles are in content
    // space and survive a scroll. the previous engine re-ran layout here.
    //
    // The scrollbar's own tile does not survive, and must not: its thumb is a
    // function of where the page now is, and a tile is identified by (layer,
    // column, row) - so leaving it cached serves the old thumb forever, which
    // is what "the scrollbar does not update" looked like. ONE tile, the
    // chrome's, is the price.
    const std::size_t scrolled = raster_calls(page);
    check(scrolled > after_first, "the scrollbar's tile is redrawn");
    check(scrolled - after_first <= 2, "and only the scrollbar's - not the page's");

    // With no scrollbar there is nothing to redraw at all, which is the
    // original claim with the chrome taken out of it.
    browser_options no_chrome;
    no_chrome.width = 400;
    no_chrome.height = 200;
    no_chrome.scrollbar_width = 0;
    browser bare{no_chrome};
    bare.load_html(demo_page);
    check(bare.frame().has_value(), "the bare page renders");
    const std::size_t bare_first = raster_calls(bare);
    bare.scroll_by(20);
    check(bare.frame().has_value(), "the bare scrolled frame renders");
    check(raster_calls(bare) == bare_first, "a scroll with no scrollbar rasters NOTHING");
}

void test_scroll_clamps_to_the_document() {
    browser page{browser_options{400, 200}};
    page.load_html(demo_page);
    check(page.frame().has_value(), "the frame renders");
    page.scroll_to(-100);
    check(page.scroll_y() == 0, "scrolling above the top clamps");
    page.scroll_to(1e9f);
    check(page.scroll_y() == page.max_scroll(), "scrolling past the end clamps to max_scroll");
    check(page.max_scroll() == page.content_height() - 200,
          "which is the content minus a viewport");
}

void test_a_resize_relayouts() {
    browser page{browser_options{600, 300}};
    page.load_html(demo_page);
    check(page.frame().has_value(), "the wide frame renders");
    const float wide_height = page.content_height();

    page.resize(200, 300);
    check(page.frame().has_value(), "the narrow frame renders");
    // Narrower means more wrapping means taller. If the resize had not
    // re-laid-out, this would be unchanged - which is the bug a resize test
    // exists to catch.
    check(page.content_height() > wide_height, "a narrower viewport makes the document taller");
    check(page.width() == 200, "and the viewport followed");
}

void test_a_new_document_starts_clean() {
    browser page{browser_options{300, 300}};
    page.load_html("<html><head><style>p { background-color: #ff0000 }</style></head>"
                   "<body><p>first</p></body></html>");
    check(page.frame().has_value(), "the first document renders");

    page.load_html("<html><body><p>second</p></body></html>");
    check(page.frame().has_value(), "the second document renders");
    check(page.title().empty(), "the old title is gone");

    // The first page's author rules must not survive. They would, if the style
    // engine were reused - which is exactly the bug this checks for, and it
    // shows up as one page bleeding into the next.
    std::size_t red_fills = 0;
    for (const auto & layer : page.layers().layers) {
        if (!layer.contents) { continue; }
        for (const auto & c : layer.contents->commands()) {
            if (c.fill == color::rgba(255, 0, 0)) { ++red_fills; }
        }
    }
    check(red_fills == 0, "the previous document's stylesheet does not apply to this one");
}

// --- input ----------------------------------------------------------------

void test_hit_testing_follows_the_scroll() {
    browser page{browser_options{400, 200}};
    page.load_html(demo_page);
    check(page.frame().has_value(), "the frame renders");

    const node_id top = page.hit_test(10, 10);
    check(static_cast<bool>(top), "something is under the top-left corner");

    // The property, stated so it does not depend on the page's exact geometry:
    // scrolling by S and looking at y is the same as not scrolling and looking
    // at y + S. Fragments are in CONTENT space; the scroll is applied at
    // hit-test time.
    const float step = 120;
    const node_id deeper_unscrolled = page.hit_test(10, 10 + step);
    page.scroll_by(step);
    check(page.scroll_y() == step, "the scroll took effect");
    check(page.hit_test(10, 10) == deeper_unscrolled, "hit testing follows the scroll exactly");

    // And it really did move to a different element - otherwise the check above
    // would pass on a page where scrolling changes nothing.
    check(deeper_unscrolled != top, "...onto a different element than before");
}

void test_transparent_boxes_are_hit_regions() {
    browser page{browser_options{100, 100}};
    page.load_html(R"(<html><head><style>
        body { margin: 0 }
        #target { width: 40px; height: 40px }
        </style></head><body><div id=target></div></body></html>)");
    check(page.frame().has_value(), "the transparent hit-test frame renders");

    // A transparent box emits no fill command. It remains an event target:
    // hit regions describe layout participation, independently of visible ink.
    check(page.hit_test(10, 10) == find_id(page, "target"),
          "a transparent box remains hit-testable");
}

void test_hit_testing_follows_stacking_order() {
    browser page{browser_options{100, 100}};
    page.load_html(R"(<html><head><style>
        body { margin: 0 }
        #stage { position: relative; width: 50px; height: 50px }
        #high, #low { position: absolute; left: 0; top: 0; width: 40px; height: 40px }
        #high { z-index: 3 }
        #low { z-index: 1 }
        </style></head><body><div id=stage>
        <div id=high></div><div id=low></div>
        </div></body></html>)");
    check(page.frame().has_value(), "the stacked hit-test frame renders");

    // `low` is later in source order but `high` is later in paint order. A
    // reverse fragment-tree walk therefore gives the wrong event target.
    check(page.hit_test(10, 10) == find_id(page, "high"),
          "the frontmost stacking context receives the hit");
}

void test_hit_testing_respects_escaped_context_clips() {
    browser page{browser_options{100, 100}};
    page.load_html(R"(<html><head><style>
        body { margin: 0 }
        #clip { position: relative; overflow: hidden; width: 20px; height: 20px }
        #high, #one { position: absolute; left: 0; top: 0; width: 40px; height: 40px }
        #high { z-index: 2 }
        #one { z-index: 1 }
        </style></head><body>
        <div id=clip><div id=high></div></div><div id=one></div>
        </body></html>)");
    check(page.frame().has_value(), "the clipped hit-test frame renders");

    // The z=2 child escapes its ordinary ancestor into the root stacking
    // context, but its ancestor's overflow clip still applies. It wins inside
    // that clip; outside it, the lower z=1 sibling is exposed.
    check(page.hit_test(10, 10) == find_id(page, "high"),
          "an escaped context wins inside its ancestor clip");
    check(page.hit_test(30, 10) == find_id(page, "one"),
          "an escaped context cannot receive hits outside its ancestor clip");
}

// The text a node carries, read straight from the document. `find_id` comes
// from dom_probe.hpp; there is no shared text helper, and one line of read
// transaction is clearer than another header.
[[nodiscard]] std::string text_of(browser & page, std::string_view id) {
    const ctbrowser::node_id want = find_id(page, id);
    if (!want) { return {}; }
    const auto txn = page.doc().read();
    std::string out;
    for (const ctbrowser::node_id child : txn.children(want)) { out += txn.text(child); }
    return out;
}

// A PAGE THAT DOES NOT COMPILE ITS OWN JAVASCRIPT.
//
// Compiling is about forty percent of a page load and running is 1.4%
// (docs/performance.md), so handing the page a precompiled image is the largest
// saving a packaged application gets. This proves the three things that make it
// usable rather than merely fast: the page behaves identically, the fast path
// was actually taken, and an image built from OTHER source is refused instead
// of run.
void test_a_page_can_load_its_scripts_from_an_image() {
    // TWO CLASSIC SCRIPTS, because one is the case that cannot show what this
    // is for: a page's scripts are compiled and cached one at a time, so
    // editing the second must not invalidate the first.
    constexpr std::string_view page =
        "<html><body><div id=out></div>"
        "<script>function shared() { return 'from ' + (1 + 1); }</script>"
        "<script>document.getElementById('out').textContent = shared();</script>"
        "</body></html>";

    // WHAT THE BROWSER WILL HASH, asked of the browser rather than written out
    // here. A second implementation of that rule is a copy free to drift from
    // the one that matters, and a drifted copy presents as "the cache does
    // nothing" rather than as a broken test.
    std::vector<std::string> scripts;
    {
        browser probe{browser_options{200, 100}};
        probe.load_html(page);
        scripts = probe.script_sources();
    }
    check(scripts.size() == 2, "the page has two classic scripts");
    if (scripts.size() != 2) { return; }

    std::vector<std::vector<std::byte>> images;
    for (const std::string & text : scripts) {
        const auto compiled = ctbrowser::script::compiler::compile(text);
        check(compiled.ok, "each script compiles");
        images.push_back(ctbrowser::script::write_image(compiled));
        check(!images.back().empty(), "and an image is written for it");
    }

    std::string without;
    {
        browser page_browser{browser_options{200, 100}};
        page_browser.load_html(page);
        check(page_browser.scripts_compiled_from_source() == 2,
              "with no images the page compiles both scripts");
        check(page_browser.classic_programs_held() == 2, "and holds one program per script");
        without = text_of(page_browser, "out");
        check(!without.empty(), "and the page produced something");
    }

    {
        browser page_browser{browser_options{200, 100}};
        for (const auto & one : images) {
            check(page_browser.add_script_image(one), "the image is admitted");
        }
        page_browser.load_html(page);
        // THE COUNTER IS THE PROOF. Without it a cache that silently misses
        // looks exactly like one that works: the page renders either way.
        check(page_browser.scripts_compiled_from_source() == 0,
              "with matching images the page compiles nothing");
        check(text_of(page_browser, "out") == without,
              "and produces the same result it did from source");
    }

    {
        // THE POINT OF THE SPLIT. Hand over only the FIRST script's image - the
        // developer edited the second - and exactly one script recompiles. Under
        // the concatenated scheme this was impossible: the two shared one key,
        // so editing either threw away both.
        browser page_browser{browser_options{200, 100}};
        check(page_browser.add_script_image(images[0]), "the first script's image is admitted");
        page_browser.load_html(page);
        check(page_browser.scripts_compiled_from_source() == 1,
              "ONE script recompiles and the other still comes from its image");
        check(text_of(page_browser, "out") == without, "and the page is unchanged");
    }

    {
        // AN IMAGE OF DIFFERENT SOURCE IS REFUSED, NOT RUN. This is the whole
        // safety property: a stale image is not a slow path, it is yesterday's
        // JavaScript at full speed.
        const auto other = ctbrowser::script::compiler::compile(
            "document.getElementById('out').textContent = 'STALE';\n");
        browser page_browser{browser_options{200, 100}};
        check(page_browser.add_script_image(ctbrowser::script::write_image(other)),
              "the stale image is a valid image, so it is admitted");
        page_browser.load_html(page);
        check(page_browser.scripts_compiled_from_source() == 2,
              "but it matches no script on this page, so both compile");
        check(text_of(page_browser, "out") == without,
              "and the page shows what its own scripts produce, not the image's");
    }

    {
        // BYTES THAT ARE NOT AN IMAGE ARE REFUSED AT THE DOOR, so a packager
        // hears about it when it hands them over rather than as a cache that
        // never hits.
        browser page_browser{browser_options{200, 100}};
        check(!page_browser.add_script_image(std::vector<std::byte>{}),
              "an empty buffer is not an image");
        std::vector<std::byte> corrupt = images[0];
        corrupt[0] = std::byte{0};
        check(!page_browser.add_script_image(corrupt), "and neither is one with no magic");
        std::vector<std::byte> wrong_engine = images[0];
        wrong_engine[8] ^= std::byte{0xFF};
        check(!page_browser.add_script_image(wrong_engine),
              "nor one from a different engine build");
    }

    {
        // A MODULE'S IMAGE IS NOT A CLASSIC SCRIPT'S, even when the text is
        // identical. It is admitted - it is a valid image - and it must never
        // answer a classic script's lookup, because a module's declarations go
        // to its own scope and the page would see nothing.
        const auto as_module = ctbrowser::script::compiler::compile(
            scripts[0], ctbrowser::script::script_kind::module_);
        browser page_browser{browser_options{200, 100}};
        check(page_browser.add_script_image(ctbrowser::script::write_image(as_module)),
              "a module image is a valid image");
        check(page_browser.add_script_image(images[1]), "and so is the second script's");
        page_browser.load_html(page);
        check(page_browser.scripts_compiled_from_source() == 1,
              "the module image does NOT answer the first script's lookup");
        check(text_of(page_browser, "out") == without, "and the page is unchanged");
    }
}

// THE THREE THINGS AN ADVERSARIAL REVIEW FOUND WRONG WITH THE SPLIT ITSELF.
// Each was introduced by giving every <script> its own program, each is invisible
// from a rendered page, and none had a test.
void test_what_the_split_got_wrong_the_first_time() {
    // ONE. `script_sources()` IS WHAT GETS COMPILED, exactly. It used to list
    // every <script>'s contribution including the ones the loop then skipped -
    // a `<script></script>` or a `<script src>` that did not resolve leaves
    // nothing but the newlines the walk added - so a packager building an image
    // per entry built images nothing would ever look up, and a tool checking
    // "one script recompiled" counted a script that never ran.
    {
        browser page{browser_options{200, 100}};
        page.load_html("<html><body>"
                       "<script>var a = 1;</script>"
                       "<script></script>"
                       "<script src='does-not-resolve.js'></script>"
                       "<script>   \n  </script>"
                       "<script>var b = 2;</script>"
                       "</body></html>");
        check(page.script_sources().size() == 2, "only the two scripts with content are listed");
        check(page.classic_programs_held() == page.script_sources().size(),
              "and there is exactly one program per listed script");
        check(page.scripts_compiled_from_source() == 2, "and exactly two compiles");
    }

    // TWO. A MISSING <script src> MUST NOT MASK EVERY LATER ERROR. The walk
    // writes its complaint into the same field the first-failure-wins rule
    // reads, so the field was already full before any script ran and no script
    // could ever report anything again.
    {
        browser page{browser_options{200, 100}};
        page.load_html("<html><body><script src='does-not-resolve.js'></script>"
                       "<script>throw new Error('the script itself failed');</script>"
                       "</body></html>");
        check(page.script_error().find("the script itself failed") != std::string::npos,
              "a script's own failure outranks a missing src");
    }
    {
        // ...and with no script failure the missing src is still reported,
        // which is the half that would be lost by simply clearing the field.
        browser page{browser_options{200, 100}};
        page.load_html("<html><body><script src='does-not-resolve.js'></script>"
                       "<script>var fine = 1;</script></body></html>");
        check(page.script_error().find("does-not-resolve.js") != std::string::npos,
              "and a missing src is still reported when nothing else failed");
    }

    // THREE. THE COMPILE COUNTER IS PER LOAD. It never reset, so the second
    // load of a fully cached page still reported the first load's misses - and
    // it is read against classic_programs_held(), which is per load, so the
    // ratio was nonsense after the first navigation.
    {
        constexpr std::string_view page_html =
            "<html><body><script>var counted = 1;</script></body></html>";
        browser page{browser_options{200, 100}};
        page.load_html(page_html);
        check(page.scripts_compiled_from_source() == 1, "one script, one compile");
        page.load_html(page_html);
        check(page.scripts_compiled_from_source() == 1,
              "and loading the same page again reports one compile, not two");
        check(page.classic_programs_held() == 1, "with one program held, not two");
    }
}

// TWO MORE THINGS THAT ONLY BECAME VISIBLE ONCE A PAGE'S SCRIPTS WERE SEPARATE
// PROGRAMS. Both cross a boundary that did not exist when the page was one.
void test_nothing_leaks_from_one_script_into_the_next() {
    // A DEAD SCRIPT'S MICROTASKS DO NOT RUN INSIDE THE NEXT SCRIPT'S TURN.
    // drain_microtasks stops on failure, so a script that threw used to leave
    // its queued handlers behind - and the next script's checkpoint ran them,
    // after that script's own code. See call.cpp for why they are dropped
    // rather than run, and where that differs from Chrome.
    {
        browser page{browser_options{200, 100}};
        page.load_html("<html><body>"
                       "<script>Promise.resolve().then(function () { alert('ghost'); });"
                       " throw new Error('first script dies');</script>"
                       "<script>alert('second');</script></body></html>");
        std::string said;
        for (const std::string & one : page.alerts()) { said += one + ";"; }
        check(said == "second;", "the dead script's handler does not surface in the next script");
    }

    // A SCRIPT THAT NAVIGATES SYNCHRONOUSLY ENDS ITS PAGE. The remaining
    // <script>s belong to a document that is gone; running them against the new
    // one let a discarded page read and overwrite it.
    {
        browser page{browser_options{200, 100}};
        constexpr std::string_view next_page = "<html><body><div id=out>NEW</div>"
                                               "<script>alert('new-page');</script></body></html>";
        page.set_navigate_hook(
            [&page, next_page](const std::string &) { page.load_html(next_page); });
        page.load_html("<html><body><div id=out>OLD</div><a id=go href='next.html'>go</a>"
                       "<script>alert('old-1'); document.getElementById('go').click();</script>"
                       "<script>alert('old-2'); "
                       "document.getElementById('out').textContent = 'CLOBBERED';</script>"
                       "</body></html>");
        std::string said;
        for (const std::string & one : page.alerts()) { said += one + ";"; }
        check(said.find("old-2") == std::string::npos,
              "the abandoned page's later scripts do not run");
        check(text_of(page, "out") != "CLOBBERED",
              "and cannot write into the document that replaced theirs");
        check(page.classic_programs_held() == 1, "and the new page holds only its own program");
    }
}

// A PACKAGED APPLICATION CARRIES ITS COMPILED SCRIPTS, and a packaging mistake
// has to be loud.
//
// `app_options` could bake in every PNG a page needs and not the one thing that
// actually costs: reading its JavaScript is about forty percent of a page load.
// The interesting half of this is the failure, though. An image built by
// another engine build is refused at the door and countable; an image that
// simply matches NO script on the page is refused by nothing at all - it is
// never looked up, the page compiles from source, and the application works
// perfectly and slowly with nothing said anywhere. That is the shape of failure
// this project treats as worst, and the counter is the only thing that sees it.
void test_a_page_reports_when_its_images_did_not_match() {
    constexpr std::string_view page =
        "<html><body><div id=out></div>"
        "<script>document.getElementById('out').textContent = 'ran';</script></body></html>";
    std::vector<std::string> scripts;
    {
        browser probe{browser_options{200, 100}};
        probe.load_html(page);
        scripts = probe.script_sources();
    }
    check(scripts.size() == 1, "the fixture has one script");
    if (scripts.empty()) { return; }

    {
        // THE PACKAGED CASE: an image built from exactly what the page runs.
        browser packaged{browser_options{200, 100}};
        check(packaged.add_script_image(
                  ctbrowser::script::write_image(ctbrowser::script::compiler::compile(scripts[0]))),
              "the image is admitted");
        packaged.load_html(page);
        check(packaged.scripts_compiled_from_source() == 0, "and nothing was compiled from source");
        check(text_of(packaged, "out") == "ran", "and the page ran");
    }
    {
        // THE MISPACKAGED CASE: a valid image of some OTHER script. Nothing
        // refuses it and the page still works - which is exactly why the count
        // has to be checked rather than the output.
        browser wrong{browser_options{200, 100}};
        check(wrong.add_script_image(ctbrowser::script::write_image(
                  ctbrowser::script::compiler::compile("var unrelated = 1;\n"))),
              "an image of another script is still a valid image");
        wrong.load_html(page);
        check(wrong.scripts_compiled_from_source() == 1,
              "THE MISMATCH IS COUNTABLE - the page compiled its own script");
        check(text_of(wrong, "out") == "ran",
              "while the page itself is perfectly fine, which is the problem");
    }
}

// TWO IMAGES OF ONE SCRIPT, AND WHICH ONE WINS MUST NOT DEPEND ON THE ORDER.
//
// An image can keep `program::source` or drop it, and both are valid images of
// the same text with the same hash and the same kind - so they collide in the
// cache. They are NOT interchangeable: one makes `f.toString()` return the
// function's text and the other "[native code]", and p5's own error system
// reads its source. Last-writer-wins made the answer depend on the order a
// packager happened to call add_script_image in, which is measured here in both
// directions.
void test_an_image_that_keeps_its_source_outranks_one_that_drops_it() {
    constexpr std::string_view page =
        "<html><body><script>function shown() { return 'body'; }</script></body></html>";
    std::vector<std::string> scripts;
    {
        browser probe{browser_options{200, 100}};
        probe.load_html(page);
        scripts = probe.script_sources();
    }
    check(scripts.size() == 1, "the fixture has one script");
    if (scripts.size() != 1) { return; }

    const auto compiled = ctbrowser::script::compiler::compile(scripts[0]);
    const auto kept = ctbrowser::script::write_image(compiled);
    const auto lean =
        ctbrowser::script::write_image(compiled, ctbrowser::script::image_option::drop_source);
    check(!kept.empty() && lean.size() < kept.size(), "dropping the source shrinks the image");

    const auto says = [&page](const std::vector<std::vector<std::byte>> & images) {
        browser p{browser_options{200, 100}};
        for (const auto & one : images) { (void)p.add_script_image(one); }
        p.load_html(page);
        (void)p.run_script("alert(shown.toString());");
        // THE COUNTER FIRST: a run that fell back to compiling would answer
        // from source and look like a pass.
        if (p.scripts_compiled_from_source() != 0) { return std::string{"<not from an image>"}; }
        return p.alerts().empty() ? std::string{"<nothing>"} : p.alerts().back();
    };

    const std::string from_kept = says({kept});
    check(from_kept.find("return 'body'") != std::string::npos,
          "an image that kept its source gives toString the text");
    check(says({lean}).find("native code") != std::string::npos,
          "and one that dropped it does not");

    // THE POINT. Both orders must agree, and both must agree with the better of
    // the two - taking the degraded image when the other was also offered is
    // the wrong default, not merely a different one.
    check(says({kept, lean}) == from_kept, "keep then drop keeps the source");
    check(says({lean, kept}) == from_kept, "and so does drop then keep");
}

// A SCRIPT THAT DIED INSIDE A `try` MUST NOT CATCH THE NEXT SCRIPT'S THROW.
//
// The VM's handler stack is not part of a call frame, and `context::execute`
// cleared `frames_` without clearing it. A VM-level raise - the call-stack
// ceiling here - returns out of the interpreter WITHOUT unwinding, so a `try`
// that was live at that moment stays recorded against frame 0. The next
// top-level program has a frame 0 as well, so the dead handler looked live: the
// throw was swallowed and the interpreter jumped to an address out of the
// PREVIOUS program's bytecode and carried on from there.
//
// Found by an adversarial review of the script split, which is what made it
// reachable - one program per page meant a raise in the first script ended the
// only top level.
void test_a_dead_script_cannot_catch_the_next_scripts_throw() {
    browser page{browser_options{200, 100}};
    page.load_html("<html><body>"
                   // Exhausts the call stack INSIDE a try, so the raise leaves
                   // a handler behind. The catch never runs: a raise is not
                   // catchable, which is the whole reason the handler is stale.
                   "<script>try { var p0 = 0; (function r() { return r(); })(); } "
                   "catch (e) { }</script>"
                   "<script>alert('1');alert('2');alert('3');alert('4');"
                   "alert('5');alert('6');alert('7');alert('8');"
                   "throw 'x';alert('X');</script>"
                   "</body></html>");
    std::string said;
    for (const std::string & one : page.alerts()) { said += one; }
    // Each statement runs ONCE and the throw ends that script. The measured
    // failure was "12345678345678" - eight alerts, then six of them again -
    // with the throw never reported at all.
    check(said == "12345678", "the second script runs once and stops at its throw");
    check(said.find("345678345678") == std::string::npos,
          "and does not re-execute a suffix of itself");
    check(page.script_error().find("x") != std::string::npos ||
              page.script_error().find("exhaust") != std::string::npos,
          "and an error is reported rather than swallowed");
}

// A MODULE'S PROGRAM OUTLIVES THE LOAD, AND ONLY THIS LOAD. Each
// <script type="module"> is compiled to its own program and kept, because its
// functions close over its top-level frame - so the vector holding them cannot
// be emptied at the end of a load. It was emptied at the start of one either,
// which made every navigation add another page's programs to a list nothing
// could reach and nothing would free.
//
// The count is the only way to see it. A leak and a working cache look
// identical from the page: the modules run, the DOM updates, and the memory
// behind them never comes back.
void test_module_programs_do_not_accumulate_across_loads() {
    constexpr std::string_view page = "<html><body><div id=out></div>"
                                      "<script type=\"module\">"
                                      "document.getElementById('out').textContent = 'module ran';"
                                      "</script>"
                                      "<script type=\"module\">globalThis.__second = 1;</script>"
                                      "</body></html>";
    browser page_browser{browser_options{200, 100}};
    page_browser.load_html(page);
    check(text_of(page_browser, "out") == "module ran", "the module runs on the first load");
    const std::size_t after_one = page_browser.module_programs_held();
    check(after_one == 2, "and the page's two modules are the two programs held");

    for (int again = 0; again < 4; ++again) { page_browser.load_html(page); }
    check(text_of(page_browser, "out") == "module ran", "the modules still run after four reloads");
    check(page_browser.module_programs_held() == after_one,
          "and five loads of one page hold one page's modules, not five pages' worth");

    // A page with no modules at all must end holding none, which is the case
    // that catches a clear() placed after the loop instead of before it.
    page_browser.load_html("<html><body><script>var x = 1;</script></body></html>");
    check(page_browser.module_programs_held() == 0,
          "and a page with no modules holds no module programs");
    // AND THE SAME FOR CLASSIC PROGRAMS, which are kept for the same reason and
    // were given the same treatment.
    check(page_browser.classic_programs_held() == 1,
          "and one classic script leaves exactly one classic program");
}

// WHAT SPLITTING THE PAGE'S SCRIPTS CHANGED, and it is not only caching. Per the
// HTML specification each classic <script> is its own Script Record; gluing them
// into one program made the page MORE permissive than the web platform in one
// direction and LESS in two others. All four cases below were measured against
// the concatenating engine before the change, and three of them behaved
// differently.
void test_each_classic_script_is_its_own_program() {
    const auto said = [](std::string_view html) {
        browser page{browser_options{200, 100}};
        page.load_html(html);
        std::string all;
        for (const std::string & one : page.alerts()) { all += one + ";"; }
        return all;
    };

    // A PARSE ERROR IS THAT SCRIPT'S PROBLEM. It used to silence the rest of the
    // page: one concatenation, one compile, nothing ran.
    check(said("<html><body><script>function ( {</script>"
               "<script>alert('second ran');</script></body></html>") == "second ran;",
          "a script that does not parse does not stop the next one");

    // AND SO IS AN UNCAUGHT THROW.
    check(said("<html><body><script>throw new Error('boom');</script>"
               "<script>alert('second ran');</script></body></html>") == "second ran;",
          "a script that throws does not stop the next one");

    // WHAT WAS LOST, and it is what the specification says should be lost: a
    // call in an earlier script to a function declared in a later one. It
    // returned 42 when the page was one program. Chrome makes it a
    // ReferenceError; this engine reports a TypeError on calling an undefined
    // global, which is a separate difference that predates this change.
    check(said("<html><body>"
               "<script>try { alert('got ' + f()); } catch (e) { alert('threw'); }</script>"
               "<script>function f() { return 42; }</script></body></html>") == "threw;",
          "an earlier script cannot call a function declared in a later one");

    // AND WHAT MUST NOT CHANGE: everything that goes through the global object.
    check(said("<html><body><script>function g() { return 7; }</script>"
               "<script>alert('got ' + g());</script></body></html>") == "got 7;",
          "a later script still sees an earlier one's function");
    check(said("<html><body><script>var v = 'shared';</script>"
               "<script>alert(v);</script></body></html>") == "shared;",
          "and its var");
    check(said("<html><body><script>let L = 'lexical';</script>"
               "<script>alert(L);</script></body></html>") == "lexical;",
          "and its let");
    check(said("<html><body><script>class K { hi() { return 'k'; } }</script>"
               "<script>alert(new K().hi());</script></body></html>") == "k;",
          "and its class");
    check(said("<html><body><script>alert('one');</script><script>alert('two');</script>"
               "<script>alert('three');</script></body></html>") == "one;two;three;",
          "and three scripts still run in document order");

    // A MICROTASK CHECKPOINT PER SCRIPT, which is what the split makes true and
    // what the specification asks for: "run a classic script" ends by cleaning
    // up, which drains the microtask queue once the stack is empty. One program
    // per page meant ONE checkpoint, after the last script; a promise resolved
    // in the first script had its handler run after the last one. Chrome runs it
    // before the second script, and now so does this.
    check(said("<html><body>"
               "<script>Promise.resolve().then(function () { alert('then'); });</script>"
               "<script>alert('two');</script></body></html>") == "then;two;",
          "a promise resolved in one script settles before the next script runs");
    // And the checkpoint is still at the END of a script rather than between two
    // of its statements, which is the other half of the same rule.
    check(said("<html><body><script>"
               "Promise.resolve().then(function () { alert('then'); }); alert('sync');"
               "</script></body></html>") == "sync;then;",
          "while inside one script the handler still waits for its last statement");
}

void test_wheel_and_keys_scroll() {
    browser page{browser_options{400, 200}};
    page.load_html(demo_page);
    check(page.frame().has_value(), "the frame renders");

    check(page.handle(input_event::wheel_by(-1)), "a wheel notch is handled");
    check(page.scroll_y() > 0, "and scrolls down");

    page.scroll_to(0);
    check(page.handle(input_event::key_press("End")), "End is handled");
    check(page.scroll_y() == page.max_scroll(), "and goes to the bottom");
    check(page.handle(input_event::key_press("Home")), "Home is handled");
    check(page.scroll_y() == 0, "and comes back to the top");

    check(!page.handle(input_event::key_press("F13")),
          "an unhandled key reports that it did nothing");
}

void test_hover_restyles() {
    browser page{browser_options{400, 300}};
    page.load_html("<html><head><style>#a { background-color: #00ff00; padding: 20px } "
                   "#a:hover { background-color: #ff0000 } </style></head>"
                   "<body><div id=a>hover me</div></body></html>");
    check(page.frame().has_value(), "the frame renders");

    const auto count_fill = [&](color want) {
        std::size_t n = 0;
        for (const auto & layer : page.layers().layers) {
            if (!layer.contents) { continue; }
            for (const auto & c : layer.contents->commands()) {
                if (c.fill == want) { ++n; }
            }
        }
        return n;
    };
    check(count_fill(color::rgba(0, 255, 0)) == 1, "the element paints its normal background");

    check(page.handle(input_event::mouse_move_to(10, 30)), "moving onto it is a change");
    check(page.frame().has_value(), "the hovered frame renders");
    check(count_fill(color::rgba(255, 0, 0)) == 1, ":hover applied");
    check(count_fill(color::rgba(0, 255, 0)) == 0, "and the normal background is gone");

    check(page.handle(input_event::mouse_move_to(390, 290)), "moving off it is a change");
    check(page.frame().has_value(), "the unhovered frame renders");
    check(count_fill(color::rgba(0, 255, 0)) == 1, ":hover unapplied when the pointer leaves");
}

void test_resize_keeps_the_renderer() {
    browser page{browser_options{400, 300}};
    page.load_html(demo_page);
    check(page.frame().has_value(), "the page renders");

    // Stand in for "the app chose the GPU": adopt a *named* renderer and check
    // the name survives. resize() used to build a fresh software backend, so an
    // app that picked hardware dropped to software on its first window resize
    // and never came back.
    page.use_renderer(raster::renderer::software(400, 300));
    const std::string before{page.rendering_with().name()};
    page.resize(700, 500);
    check(page.frame().has_value(), "the resized frame renders");
    check(page.rendering_with().name() == before, "resize keeps the renderer it was given");
    check(page.width() == 700 && page.height() == 500, "and the viewport followed");

    const auto image = page.read_pixels();
    check(image.has_value() && image->width() == 700 && image->height() == 500,
          "and the target really is the new size");
}

void test_background_is_honoured() {
    browser_options options{80, 60};
    options.background = color::rgba(0, 0, 255);
    browser page{options};
    // A page with no body background shows the canvas colour. browser_options
    // carried this field and nothing read it.
    page.load_html("<html><body></body></html>");
    check(page.frame().has_value(), "the page renders");
    const auto image = page.read_pixels();
    check(image.has_value(), "the frame reads back");
    if (!image) { return; }
    check(image->row(30)[40] == 0xFF0000FFu, "browser_options::background is the page canvas");
}

// A browser holds three `this`-capturing lambdas; moving one would leave them
// pointing at the old address. The implicit move was available until it was
// deleted, so this is worth stating as a compile-time fact.
static_assert(!std::is_move_constructible_v<browser>, "browser must not be movable");
static_assert(!std::is_copy_constructible_v<browser>, "browser must not be copyable");

// --- determinism ----------------------------------------------------------

void test_rendering_is_reproducible() {
    const auto render = [](std::vector<std::uint32_t> & into) {
        browser page{browser_options{320, 240}};
        page.load_html(demo_page);
        (void)page.frame();
        const auto image = page.read_pixels();
        if (!image) { return false; }
        into.assign(image->pixels().begin(), image->pixels().end());
        return true;
    };
    std::vector<std::uint32_t> first, second;
    check(render(first) && render(second), "two independent renders succeed");
    // A browser whose output depends on allocation addresses or map iteration
    // order cannot have a golden image, and cannot be reviewed by diffing one.
    check(first == second, "two renders of the same page are byte-identical");
}

} // namespace

int main() {
    test_ua_stylesheet_applies();
    test_script_and_style_render_nothing();
    test_title_is_extracted();

    test_an_idle_frame_does_nothing();
    test_a_scroll_only_recomposites();
    test_scroll_clamps_to_the_document();
    test_a_resize_relayouts();
    test_a_new_document_starts_clean();
    test_resize_keeps_the_renderer();
    test_background_is_honoured();

    test_hit_testing_follows_the_scroll();
    test_transparent_boxes_are_hit_regions();
    test_hit_testing_follows_stacking_order();
    test_hit_testing_respects_escaped_context_clips();
    test_a_page_can_load_its_scripts_from_an_image();
    test_each_classic_script_is_its_own_program();
    test_a_dead_script_cannot_catch_the_next_scripts_throw();
    test_what_the_split_got_wrong_the_first_time();
    test_nothing_leaks_from_one_script_into_the_next();
    test_an_image_that_keeps_its_source_outranks_one_that_drops_it();
    test_a_page_reports_when_its_images_did_not_match();
    test_module_programs_do_not_accumulate_across_loads();
    test_wheel_and_keys_scroll();
    test_hover_restyles();

    test_rendering_is_reproducible();

    REPORT("shell_basics");
}
