#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/style/style.hpp>

#include <ctbrowser/shell/bindings.hpp>
#include <ctbrowser/shell/embedded_fonts.hpp>
#include <ctbrowser/shell/image/images.hpp>
#include <ctbrowser/shell/input.hpp>
#include <ctbrowser/shell/metrics.hpp>
#include <ctbrowser/shell/page/assets.hpp>
#include <ctbrowser/shell/page/canvas.hpp>
#include <ctbrowser/shell/page/forms.hpp>
#include <ctbrowser/shell/page/svg_cache.hpp>

// The engine, assembled.
//
// Everything before this stage was a subsystem with a test. This is the object
// that owns them all and knows what order they run in - and, more importantly,
// what a frame is ALLOWED to skip. That is the whole design:
//
//   load     parse -> style -> box tree -> layout -> record   (all of it)
//   resize                     box tree -> layout -> record   (styles survive)
//   scroll                                          composite (nothing else)
//
// the previous engine had one path. engine::frame() re-ran layout every frame, so a caret blink
// cost a full layout of the document. Here the pipeline is split at the points
// where work can be reused, and `dirty_` is the record of how far back the
// current frame has to start.
//
// Deliberately SDL-FREE. The window and the event loop live in :app, gated on
// SDL3, and this is what they drive - which is what makes the whole engine
// testable headlessly, exactly as the previous engine kept engine.hpp separate from app.hpp.

namespace ctbrowser::shell {

using ctbrowser::layout::box_node;
using ctbrowser::layout::fragment;
using ctbrowser::paint::layer_tree;
using ctbrowser::raster::renderer;

// How much of the pipeline the next frame has to re-run. Ordered: a later stage
// implies every earlier one is still valid.
enum class dirty : std::uint8_t {
    nothing,   // composite only - a scroll
    raster,    // the tiles are stale but the display list is not - a <canvas>
               // drawn into. Its pixels are shared, so what has to happen is a
               // re-raster and NOT a re-record; an animation that re-records
               // (or worse, re-lays-out) every frame is why canvas pages jank.
    paint,     // re-record the display list
    layout,    // geometry changed - a resize
    styles,    // the cascade changed
    everything // a new document
};

[[nodiscard]] constexpr dirty worse(dirty a, dirty b) noexcept {
    return static_cast<std::uint8_t>(a) > static_cast<std::uint8_t>(b) ? a : b;
}

struct browser_options {
    int width = 1024;
    int height = 768;
    int tile_extent = ctbrowser::raster::default_tile_extent;
    // The page canvas, behind everything the document draws. White by default,
    // because that is what a browser with no page background shows.
    color background = color{ctbrowser::style::ua_canvas};
    // A page taller than the window scrolls; this is how far one wheel notch
    // moves it. the previous engine used the same figure.
    float wheel_step = 53.0f;
    // How many VISUAL LINES one notch moves a textarea. Lines rather than
    // pixels, because a field scrolls by whole lines - a half-line offset would
    // need the painter to carry a sub-line origin for no gain.
    int wheel_lines = 3;
    // The overlay scrollbar's width, and the width a tall page gives up to it.
    // 0 hides it - which is what a fixed-size game wants.
    float scrollbar_width = 15.0f;
    // Half the caret's blink period, in milliseconds - Chrome's figure. 0 stops
    // it blinking, which is what a screenshot test wants: a caret that is
    // sometimes there is not byte-comparable.
    double caret_blink_ms = 500;
    // AUTO-SCROLL WHILE DRAG-SELECTING, when the pointer is held outside the
    // field. The rate rises with how far outside it is, so a small overshoot
    // creeps and a big one races:
    //
    //   interval = autoscroll_ms / (1 + distance / autoscroll_ramp_px)
    //
    // clamped to autoscroll_min_ms. At the defaults: 1px out is ~95ms a step,
    // 20px is 50ms, 60px is 25ms, and past ~110px it is the floor. 0 for
    // autoscroll_ms turns the whole thing off.
    double autoscroll_ms = 100;
    double autoscroll_min_ms = 16;
    float autoscroll_ramp_px = 20;
};

class browser {
public:
    explicit browser(browser_options options = {})
        : options_(options), recorder_(atoms_),
          renderer_(renderer::software(options.width, options.height, options.tile_extent)) {
        reset_document();
    }

    // Neither copyable nor movable. run_scripts() hands dom_bindings two
    // `this`-capturing callbacks and record() installs a third on the recorder,
    // so a moved-from browser leaves three lambdas pointing at the old address.
    // The implicit move was available and would have done exactly that.
    browser(const browser &) = delete;
    browser & operator=(const browser &) = delete;
    browser(browser &&) = delete;
    browser & operator=(browser &&) = delete;

    // Render with something other than the software backend - the GPU one, or
    // whatever gpu::create_renderer() decided this machine can run.
    void use_renderer(renderer r);
    [[nodiscard]] const renderer & rendering_with() const noexcept { return renderer_; }

    // --- content ---------------------------------------------------------

    // Replace the document. Everything downstream is invalidated, which is the
    // one case where that is the honest answer.
    void load_html(std::string_view html);

    // --- script ----------------------------------------------------------

    // PAGE-LEVEL TEXT SELECTION.
    //
    // A position is (node, code point WITHIN THAT NODE'S TEXT) rather than a
    // fragment pointer: a node's text is broken across as many fragments as it
    // has visual lines, and a relayout rebuilds all of them - a selection has
    // to survive a window resize, and pointers do not.
    //
    // The GLYPH GEOMETRY is not stored on the fragment. It is derived on demand
    // from the same measure layout used, which costs a few measurements per
    // click and keeps the fragment tree exactly the shape it was - the plan
    // called publishing per-line glyph geometry the one item most likely to
    // spill, and this is why it did not have to.
    struct text_position {
        node_id node;
        std::size_t code_point = 0;
        [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(node); }
    };

    [[nodiscard]] bool has_selection() const noexcept;
    void clear_selection();
    // What is selected, in document order, as text.
    [[nodiscard]] std::string selected_text();

    // THE CLIPBOARD, as two hooks. The engine is SDL-free, so it cannot own a
    // system clipboard - the app layer installs these, and without them copy
    // and paste still work WITHIN the page, which is what makes the whole thing
    // testable headlessly.
    void set_clipboard_hooks(std::function<void(const std::string &)> write,
                             std::function<std::string()> read) {
        clipboard_write_ = std::move(write);
        clipboard_read_ = std::move(read);
    }

    // Natives the EMBEDDER supplies - `playSound` from the SDL layer is the
    // reason this exists. Re-installed on every navigation, because each page
    // gets a fresh script context and a hook registered once would silently
    // stop existing after the first `location.reload()`.
    void define_native(std::string name, script::native_fn fn);

    // HAND THE PAGE ITS SCRIPTS ALREADY COMPILED.
    //
    // Parsing and compiling JavaScript is about forty percent of a page load
    // and running it is 1.4% (docs/performance.md), so this is the largest
    // saving available to a packaged application: measured on the devbox,
    // loading babylon from an image is 77 ms against 300 ms to compile it.
    //
    // ONE IMAGE PER <script>, NOT ONE PER PAGE, and that is the whole point of
    // the shape. A page's classic scripts used to be glued into one string and
    // compiled as one program, so the cache was keyed on the concatenation -
    // and editing a two-line sketch beside a 4.5 MB library invalidated the
    // library. Each script is now its own program keyed on its own bytes, so
    // p5.js is baked once and answers on every page that loads it. Measured on
    // p5-basic.html: editing the sketch cost 51.44 ms to recompile the page and
    // now costs 4.89 ms, which is 10.5x.
    //
    // AN IMAGE IS USED ONLY IF IT MATCHES, and this is where a mistake becomes
    // wrong code rather than a slow page: an image whose source hash is not
    // this script's, or which was compiled as a module, is refused rather than
    // run. `add_script_image` refuses at the door too - bytes that are not an
    // image this build would load return false immediately, so a packager hears
    // about it when it hands them over rather than as a cache that never hits.
    //
    // A refusal at USE is silent by design and countable by
    // scripts_compiled_from_source(): the page still works, it just paid for
    // the compile. That counter is how a test proves the fast path was taken
    // rather than assuming it.
    bool add_script_image(std::vector<std::byte> image);

    // Forget every image. A page keyed on old bytes should stop matching when a
    // packager says so, rather than when the browser happens to be destroyed.
    void clear_script_images() noexcept { script_images_.clear(); }

    // How many classic scripts this browser compiled rather than loaded from an
    // image. Zero after a load whose every script was cached; without this a
    // cache that silently misses looks exactly like one that works.
    [[nodiscard]] std::size_t scripts_compiled_from_source() const noexcept {
        return scripts_compiled_from_source_;
    }

    // How many module programs this browser is holding alive. One per
    // <script type="module"> on the CURRENT page and none from any earlier one
    // - a module's functions close over its top-level frame, so its program has
    // to outlive the load, and for a while nothing emptied the vector on the
    // way in. A count is the only way to see that from outside, because a leak
    // and a working cache look identical from the page.
    [[nodiscard]] std::size_t module_programs_held() const noexcept {
        return module_programs_.size();
    }

    // THE TEXT THE LAST LOAD COMPILED, one entry per classic <script> on the
    // page in document order: the resolved `src` bytes followed by a newline
    // when the element has one, then the element's own text, then a newline.
    // Exactly what run_scripts assembles, because it IS what run_scripts
    // assembled.
    //
    // This is what a packaging tool needs and the reason it is public. An image
    // is accepted only when its source hash matches one of these strings, so
    // anything BUILDING an image has to know precisely what the browser will
    // hash - and a second implementation of that rule, in the packager, would
    // be a copy free to drift from the one that matters. Load the page once,
    // take these, compile and write one image each; the next load matches by
    // construction.
    [[nodiscard]] const std::vector<std::string> & script_sources() const noexcept {
        return script_sources_;
    }

    // AND THE MODULE SCRIPTS, WHICH CANNOT BE PACKAGED YET.
    //
    // `script_sources()` above lists the CLASSIC scripts and nothing else,
    // because those are the ones an image can stand in for: `load_module`
    // compiles from source every time and there is no image path into it. That
    // made a module page invisible to a packager and, worse, invisible to the
    // check that asks whether packaging worked - zero classic scripts compiled
    // from source is trivially true when there are no classic scripts.
    //
    // So the modules are published too, and both the packager and the launcher
    // refuse rather than shipping an application that parses all of its
    // JavaScript at every start and says nothing.
    [[nodiscard]] const std::vector<std::string> & module_sources() const noexcept {
        return module_sources_;
    }

    // How many classic scripts the last load ran as their own programs. One per
    // non-empty <script> on the page, and the denominator that makes
    // scripts_compiled_from_source() a ratio rather than a number.
    [[nodiscard]] std::size_t classic_programs_held() const noexcept {
        return classic_programs_.size();
    }

    // Where the vendored faces are looked for when `use_real_fonts` is given no
    // directory: $CTBROWSER_FONT_PATH, or `fonts` beside the executable. Public
    // because a packager needs the same answer - see browser.cpp.
    [[nodiscard]] static std::string default_font_directory();

    // Turn on real fonts. Loads the vendored OFL faces through the asset
    // registry - so an application that baked them in never touches the disk -
    // and leaves font8x8 in place if SDL3_ttf is absent or none of them load.
    //
    // OPT-IN rather than automatic: the goldens are font8x8's pixels, and a
    // page that silently changed how it renders because a font file happened to
    // be next to the binary would be a worse default than one that looks the
    // same everywhere.
    //
    // DEFAULTED TO NOTHING, which means "wherever this build keeps them":
    // $CTBROWSER_FONT_PATH if it is set, and `fonts` beside the executable
    // otherwise. Those are two different places on purpose - a shipped
    // application carries `fonts/` next to its binary, while in the source tree
    // the faces are `ctbrowser/resources/fonts/` and the build sets the
    // variable for anything it runs. Passing a directory explicitly overrides
    // both and is what a caller with its own faces wants.
    bool use_real_fonts(std::string_view directory = {});
    [[nodiscard]] bool has_real_fonts() const noexcept { return fonts_ != nullptr; }
    // The metrics layout measured with, so a caller can ask where a run's
    // baseline is. The same object the rasterizer draws with.
    [[nodiscard]] ctbrowser::layout::measure_text_fn metrics() const { return measure(); }

    // Where the page's resources come from. An application seeds this from
    // app_options::assets; `ctbrowse` points its base path at the page's
    // directory so `<img src="cat.bmp">` resolves next to the html.
    [[nodiscard]] asset_registry & assets() noexcept { return assets_; }
    [[nodiscard]] image_store & images() noexcept { return images_; }
    // Whether fetch() may open a socket when the registry misses.
    void allow_network(bool allowed);

    [[nodiscard]] canvas_store & canvases() noexcept { return canvases_; }
    [[nodiscard]] form_store & forms() noexcept { return forms_; }
    [[nodiscard]] node_id focused() const noexcept { return focused_; }

    // Typed text, from the platform's text-input event rather than from key
    // codes - that is the only way to get IME, dead keys and non-Latin layouts
    // right, and it is what SDL_EVENT_TEXT_INPUT delivers.
    bool text_input(std::string_view text);
    // THE BINDINGS DO NOT EXIST UNTIL A PAGE IS LOADED - they are made in
    // run_scripts - so this dereferences null before then. Worth knowing rather
    // than discovering: the symptom is a segfault inside what looks like a
    // getter.
    [[nodiscard]] dom_bindings & bindings() noexcept { return *bindings_; }

    // WHETHER A PAGE'S WebGL SHOULD RUN ON ANGLE, set BEFORE load_html.
    //
    // It lives here rather than on the bindings for exactly the reason above: a
    // caller would have to know that the bindings are built during the load,
    // which is a detail of when scripts run. Stage 2 of docs/plans/angle.md
    // keeps both back ends alive, and this is how a caller chooses.
    void prefer_angle_webgl(bool on) noexcept { prefer_angle_webgl_ = on; }
    [[nodiscard]] bool angle_webgl_preferred() const noexcept { return prefer_angle_webgl_; }
    [[nodiscard]] const std::string & script_error() const noexcept { return script_error_; }

    // A <link rel=stylesheet> that did not resolve. Separate from
    // script_error() because it is a different failure with a different cause,
    // and NOT silent the way a real browser's is: a fixture whose href has a
    // typo lays out as if the stylesheet were empty, which reads as thousands
    // of engine differences rather than as one broken path. The parity harness
    // asks this first and reports a rig failure instead of a diff.
    [[nodiscard]] const std::string & style_error() const noexcept { return style_error_; }

    // Run a snippet in the page's own script context - the same globals, the
    // same document. This is what a devtools console types into, and what a
    // test uses to ask a page a question. Returns whether it ran.
    bool run_script(std::string_view source);

    // How many times layout has run. Observable because the whole dirty-level
    // design exists to keep this number down: a caret blink or a scroll must
    // not increment it.
    [[nodiscard]] std::size_t layout_count() const noexcept { return layouts_; }

    // WHAT THE LAST FRAME SPENT ITS TIME ON, in milliseconds.
    //
    // The runtime profiler (`CTBROWSER_PROFILE`) recorded one `frame_ms` for
    // style, layout, record and raster together, so it could say a frame was
    // slow and never which part of it was - and those four are exactly the
    // stages the dirty level decides between. A scroll that re-rasters when it
    // should only re-composite looked identical to one that did not.
    //
    // Zero for a stage the frame SKIPPED, which is the interesting half: the
    // dirty-level design is about not running these, so a zero is the design
    // working rather than missing data.
    struct frame_timing {
        double styles_ms = 0;
        double layout_ms = 0;
        double record_ms = 0;
        double raster_ms = 0;
    };
    [[nodiscard]] const frame_timing & last_frame_timing() const noexcept { return timing_; }

    // Collect the script heap now, and how many objects it has. Exposed
    // because "does a collection free what the page is still using" is only
    // answerable from outside, and it is the question that kept the collector
    // switched off.
    std::size_t collect_garbage() { return script_ ? script_->collect() : 0; }
    [[nodiscard]] std::size_t live_script_objects() const;

    // A control's live state - value, caret, selection, checked. Read-only and
    // null for anything that is not a control: this is what a test asks where
    // the caret ended up, and what an embedder asks to read a form without
    // submitting it.
    [[nodiscard]] const control_state * control_state_of(node_id id);

    // Whether anything has changed that a frame would show. An event loop asks
    // this rather than assuming: a caret blink and a scrollbar that appears
    // after a relayout both change what should be on screen without any event
    // having arrived, and a loop that only redraws when IT did something shows
    // neither until the user happens to move the mouse.
    [[nodiscard]] bool needs_frame() const noexcept { return dirty_ != dirty::nothing; }

    // Milliseconds until this page next has something to do on its own - a
    // timer, an animation frame, or the caret's next blink. Infinity when it
    // has nothing, which is what lets an idle application BLOCK instead of
    // waking up sixty times a second to discover there was nothing to do.
    [[nodiscard]] double next_wakeup_ms();

    // THE CARET BLINKS, in Chrome's 500 ms halves. The phase is measured from
    // the last caret ACTIVITY rather than from page load: a caret that blinks
    // out from under the character you just typed looks broken, so typing,
    // moving and clicking all restart it solid.
    [[nodiscard]] bool caret_visible() const noexcept;
    void restart_caret_blink() noexcept { caret_base_ms_ = caret_clock_ms_; }

    // Advance the page clock and run whatever became due - timers, then
    // animation frames. An event loop calls this once per tick; the return is
    // how many callbacks ran, so a caller can tell an idle page from a busy one.
    std::size_t tick(double elapsed_ms);

    // Re-parse the page and run its script again from the top - navigation to
    // where you already are, which is the only navigation the engine has.
    void reload();

    // The system dialog `alert()` raises. The engine is SDL-free and has no
    // window to put a dialog in, so this is a hook like the clipboard's; without
    // one the messages are still recorded and readable, which is what makes
    // alert testable headlessly.
    void set_alert_hook(std::function<void(const std::string &)> hook);
    // Kept HERE rather than on the bindings, because a reload replaces the
    // bindings - and the alert that caused the reload is exactly the one you
    // want to still be able to read afterwards. MDN's breakout alerts and then
    // reloads in the same breath.
    [[nodiscard]] const std::vector<std::string> & alerts() const noexcept { return alerts_; }

    // WHAT A PAGE EXPORTED, and the one behaviour this engine invents rather than
    // copies. A browser shows a save dialog for an `<a download>`; there is
    // nobody here to show one to, so the bytes are written out. See
    // browser::save_download for why the alternative was not "do nothing".
    struct download_record {
        std::string name;  // what the page asked it be called, sanitised
        std::string path;  // where it actually went
        std::size_t bytes; // how big it was; 0 means the href resolved to nothing
        bool written;      // whether the write itself succeeded
    };
    // Where exports land. Empty means the process's working directory, which is
    // where a command-line tool writes. A test points this at its build dir.
    void set_download_directory(std::filesystem::path where) {
        download_directory_ = std::move(where);
    }
    [[nodiscard]] const std::vector<download_record> & downloads() const noexcept {
        return downloads_;
    }
    // Told as it happens, for an embedder that wants to report or redirect one.
    void set_download_hook(std::function<void(const download_record &)> hook) {
        download_hook_ = std::move(hook);
    }

    // REAL TIME, if the embedder wants it. Without one, `Date.now()` is a fixed
    // base plus the page's own elapsed time - deterministic, which is what makes
    // a golden possible, and the same reason Math.random is seeded here. An
    // interactive application installs the wall clock instead, because showing
    // the wrong date is a bug no golden cares about; `run_app` does exactly that.
    void set_clock(std::function<double()> clock) { clock_ = std::move(clock); }

    // Where a link that leaves this page goes. the engine does not navigate, so the
    // embedder decides - `ctbrowse` opens a local .html, the SDL app hands an
    // http(s) URL to the system browser, and a program with no hook does
    // nothing rather than pretending it followed the link.
    void set_navigate_hook(std::function<void(const std::string &)> hook);
    // What the last activated link recorded. A fragment lands in the hash, like
    // the previous engine, because scrolling to an anchor IS navigation within a document.
    [[nodiscard]] const std::string & location_href() const noexcept { return location_href_; }
    [[nodiscard]] const std::string & location_hash() const noexcept { return location_hash_; }

    [[nodiscard]] std::string_view title() const noexcept { return title_; }
    [[nodiscard]] const document & doc() const noexcept { return *doc_; }
    [[nodiscard]] atom_table & atoms() noexcept { return atoms_; }

    // --- viewport --------------------------------------------------------

    void resize(int width, int height);
    [[nodiscard]] int width() const noexcept { return options_.width; }
    [[nodiscard]] int height() const noexcept { return options_.height; }

    // --- scrolling -------------------------------------------------------
    //
    // The payoff of the whole architecture: this touches no stage but the
    // compositor. the previous engine's equivalent re-ran layout.
    void scroll_by(float dy) { scroll_to(scroll_y_ + dy); }
    void scroll_to(float y);
    [[nodiscard]] float scroll_y() const noexcept { return scroll_y_; }
    [[nodiscard]] float content_height() const noexcept { return content_height_; }
    // Whether a viewport x lands on the scrollbar. Public because the app layer
    // asks it to choose a cursor.
    // What the pointer should look like at a viewport point: the CSS `cursor`
    // of the element under it, with the UA's defaults - a link is a pointer, an
    // editable is a text beam. A name rather than a handle, so the engine needs
    // no cursor vocabulary and the app layer maps it to whatever the platform
    // has.
    [[nodiscard]] std::string_view cursor_at(float x, float y);

    [[nodiscard]] bool on_scrollbar(float x) const noexcept;
    [[nodiscard]] float max_scroll() const noexcept;

    // --- input -----------------------------------------------------------

    // Returns whether anything changed, so a caller can skip a frame it does
    // not need. A browser that repaints on every mouse move is a browser with a
    // hot fan.
    bool handle(const input_event & event);

    // What is under a viewport point. Scroll is applied here rather than in the
    // fragment tree, because the fragments are in CONTENT space - the same
    // reason a scroll does not invalidate them.
    [[nodiscard]] node_id hit_test(float x, float y) const;

    // --- frames ----------------------------------------------------------

    // Run whatever this frame needs and composite. Cheap when nothing is dirty,
    // which is the common case and the point.
    std::expected<void, ctbrowser::raster::gpu_error> frame(scheduler * pool = nullptr);

    // Push anything a script wrote into `value`/`checked` through to the
    // controls, and report whether that changed one.
    [[nodiscard]] bool sync_controls() { return bindings_->refresh_wrappers(); }

    [[nodiscard]] rect viewport() const noexcept;
    [[nodiscard]] std::uint64_t frames() const noexcept { return frames_; }
    [[nodiscard]] const fragment & fragments() const noexcept { return fragments_; }
    [[nodiscard]] const layer_tree & layers() const noexcept { return layers_; }

    // The composited image, for goldens and for headless runs.
    [[nodiscard]] std::expected<ctbrowser::raster::surface, ctbrowser::raster::gpu_error>
    read_pixels() {
        return renderer_.read_target();
    }

private:
    // The same bits ctcss compiles :hover/:active/:focus into, named here so
    // the browser and the selector matcher cannot drift apart.
    static constexpr std::uint32_t state_hover = ctbrowser::style::engine::state_hover;
    static constexpr std::uint32_t state_active = ctbrowser::style::engine::state_active;
    static constexpr std::uint32_t state_focus = ctbrowser::style::engine::state_focus;

    // Tiles are discarded in frame(), not here: every level at or above
    // `raster` invalidates them, and doing it in one place is what stops a new
    // dirty level from silently forgetting to.
    void mark(dirty d) { dirty_ = worse(dirty_, d); }

    // The author's sheets: <style> elements AND <link rel=stylesheet>, in
    // DOCUMENT ORDER, because the cascade's last tie-break is source order and
    // a <style> after a <link> has to be able to override it.
    //
    // <link> resolves through the same asset_registry as <script src> and
    // <img src> - registry, then data:, then the filesystem - so there is still
    // no socket here. That is enough for a page that ships its own stylesheet
    // beside it, which is what every fixture and every real local page is.
    void load_author_styles();

    // Push the window size and the user's preferences into the style engine, and say
    // whether any media query's truth moved. A resize calls it and only re-resolves the
    // cascade when the answer is yes.
    bool media_environment_changed();

    [[nodiscard]] std::string extract_title();

    void resolve_styles();

    // Run every <script> in the document, in order. Errors are recorded rather
    // than thrown: a page whose script fails still has to render, which is what
    // every browser does and what makes a broken script a broken feature rather
    // than a blank window.
    // The body of one load. load_html wraps it so that a navigation asked for
    // by a script is queued rather than performed under that script's feet.
    void load_one_page(std::string_view html);
    void run_scripts();

    // Every <img src> in the document, decoded once. A missing or undecodable
    // image is remembered as a null so the element lays out at zero size rather
    // than being retried every frame.
    // The measure layout uses, and the fonts the rasterizer draws with, are the
    // SAME object - text lands where layout thought it would only if one thing
    // answers both questions.
    [[nodiscard]] const ctbrowser::raster::font_backend & fonts() const;
    [[nodiscard]] ctbrowser::layout::measure_text_fn measure() const;

    // The page's own @font-face rules, loaded through the asset registry like
    // any other resource. Called when real fonts are turned on and again on
    // every navigation, because the rules belong to the document.
    void load_page_fonts();

    void load_images();

    // Re-resolve every <img>'s bitmap, without clearing the SVG sources. Runs
    // before each layout, because a script can change a src at any time.
    void refresh_images();

    [[nodiscard]] std::shared_ptr<const ctbrowser::paint::bitmap> image_of(node_id id) const;

    void install_embedder_natives();

    void run_layout();

    void record();

    // The scrollbar, as its OWN non-scrolling layer.
    //
    // A layer rather than a paint into the page: it must not move when the page
    // does, and the compositor already knows how to hold a layer still. That is
    // also why it survives a scroll without re-recording anything - a scroll
    // moves the page layer and leaves this one where it is.
    // Rebuilt on every frame whose scroll moved, NOT only when the page
    // re-records. A scroll marks dirty::composite, which deliberately skips
    // recording - so the thumb was drawn once and then stayed where it was
    // until something else forced a re-record. That is the delay: the bar was
    // always one edit behind.
    //
    // Cheap enough to do unconditionally: it is two rectangles.
    void refresh_chrome();

    // Everything the BROWSER draws rather than the page: the scrollbar, and an
    // open <select>'s option list. Each is its own non-scrolling layer for the
    // same reason - chrome does not move when the page does, and the compositor
    // already knows how to hold a layer still.
    void record_chrome();

    // The context menu. Its entries are the clipboard verbs, because those are
    // the ones the browser can carry out on its own.
    static constexpr std::string_view menu_items[] = {"Copy", "Cut", "Paste", "Select All"};
    static constexpr float menu_row = 20;
    static constexpr float menu_width = 120;

    void record_context_menu();

    [[nodiscard]] rect menu_box() const;

    // Returns whether the menu consumed the press. A click anywhere closes it,
    // and a click ON it runs the verb - neither reaches the page.
    bool handle_menu_press(const input_event & event);

    // The option list of an open <select>. THE reason a select was unusable:
    // the box drew, the popup did not exist, so there was no way to choose
    // anything with a pointer.
    void record_select_popup();

    // Every <option>'s text, in document order.
    // The value of the nth <option>, for the popup's pick.
    [[nodiscard]] std::string option_value_at(const read_txn & txn, node_id select,
                                              std::size_t index) {
        const atom option_tag = atoms_.intern_lower("option");
        std::size_t at = 0;
        for (const node_id child : txn.children(select)) {
            if (txn.tag(child).value_or(atom{}) != option_tag) { continue; }
            if (at++ == index) { return form_store::option_value(txn, atoms_, child); }
        }
        return {};
    }

    [[nodiscard]] std::vector<std::string> option_labels(const read_txn & txn, node_id select);

    // Where an element is ON SCREEN - the fragment tree is in content space, so
    // the scroll has to come off.
    [[nodiscard]] rect viewport_box_of(node_id id) const;

    void record_scrollbar();

    // Where the thumb sits. Proportional to how much of the document is
    // visible, with a floor so a very long page still has something to grab.
    [[nodiscard]] rect scrollbar_thumb() const;

    // What a <canvas> or a form control draws. Everything here is chrome the
    // UA supplies rather than anything the document asked for, which is why the
    // palette comes from :ua and not from the cascade.
    // The inset a control's text sits at. Firefox uses 1px 2px on a text input
    // and 1px 6px on a button; this is one number because the vertical inset is
    // handled by centring instead.
    // THE SAME NUMBER layout reserves - see the note on the constant. Two
    // copies is how the painter came to draw text 6 in on a box that had set
    // aside 4.
    // GONE, and deliberately not replaced by another constant: a control's text
    // starts at its CONTENT box, which paint_replaced is now handed. The two
    // copies of this number - here and in layout - are exactly why a field's text
    // once started inside its own reserved room, and why Bootstrap's padding did
    // nothing.

    // A vector graphic, rasterised for THIS box rather than scaled into it.
    //
    // The rect handed to draw_image is the SNAPPED size, not `box`. That is the
    // point of the whole size-aware path: draw_image scales nearest-neighbour,
    // so passing the unsnapped box would have it resample a bitmap that is
    // already the right size to within a fraction of a pixel - reintroducing
    // exactly the stair-stepping this exists to remove. Snapped, it is a 1:1
    // blit.
    bool paint_svg(node_id id, const rect & box, ctbrowser::paint::display_list & into) {
        const int width = std::max(1, static_cast<int>(std::lround(box.width)));
        const int height = std::max(1, static_cast<int>(std::lround(box.height)));
        auto pixels = svg_.pixels_for(id, width, height);
        if (!pixels) { return false; }
        into.draw_image(rect{box.x, box.y, static_cast<float>(width), static_cast<float>(height)},
                        std::move(pixels), id);
        return true;
    }

    // `box` is the BORDER box and `content` the content box - the recorder
    // resolved the padding and the border to draw them, so it hands over where
    // they left off rather than the shell keeping a constant of its own. That
    // constant was `control_padding`, layout had a second copy, and a sheet could
    // change neither.
    void paint_replaced(node_id id, const rect & box, const rect & content,
                        const ctbrowser::style::computed_style_ptr & style,
                        ctbrowser::paint::display_list & into) {
        const auto txn = doc_->read();
        const std::string_view tag = atoms_.text(txn.tag(id).value_or(atom{}));

        if (tag == "canvas") {
            if (auto pixels = canvases_.pixels_of(id)) {
                into.draw_image(box, std::move(pixels), id);
            }
            return;
        }
        if (tag == "img") {
            // A missing image draws NOTHING - not a broken-image icon, which is
            // chrome this browser does not have yet, and not a filled box,
            // which would look like a rendering bug.
            if (auto pixels = image_of(id)) {
                into.draw_image(box, std::move(pixels), id);
            } else {
                paint_svg(id, box, into);
            }
            return;
        }
        if (tag == "svg") {
            // Same route as an <img> pointing at an .svg, and deliberately so:
            // the source got here differently - sliced out of the document
            // rather than loaded from a file - but from `svg_store` on it is
            // the same graphic rasterised the same way at the same size.
            paint_svg(id, box, into);
            return;
        }
        const std::string_view type = txn.attribute_value(id, atoms_.intern("type"));
        const control_kind kind = control_kind_of(tag, type);
        if (kind == control_kind::none) { return; }

        control_state & control = forms_.state_of(txn, atoms_, id);
        const bool focused = focused_ == id;
        const bool disabled = is_disabled(id);
        const color frame{ctbrowser::style::ua_widget_frame};
        const color field{disabled ? color{ctbrowser::style::ua_widget_disabled_face}
                                   : color{ctbrowser::style::ua_widget_field}};
        const color accent{disabled ? color{ctbrowser::style::ua_widget_disabled_text}
                                    : color{ctbrowser::style::ua_widget_accent}};

        switch (kind) {
        case control_kind::checkbox: {
            into.fill(box, control.checked ? accent : field, id);
            outline(box, frame, into, id);
            if (control.checked) { check_mark(box, into, id); }
            break;
        }
        case control_kind::radio: {
            // A RADIO IS ROUND, and that is the whole visual difference between
            // it and a checkbox - drawn as a square, the two controls are
            // indistinguishable and the shape is what tells you one of them is
            // exclusive. The display list had no ellipse until now.
            into.fill_ellipse(box, frame, id);
            into.fill_ellipse(rect{box.x + 1, box.y + 1, box.width - 2, box.height - 2},
                              control.checked ? accent : field, id);
            if (control.checked) {
                const float inset = box.width * 0.3f;
                into.fill_ellipse(rect{box.x + inset, box.y + inset, box.width - 2 * inset,
                                       box.height - 2 * inset},
                                  color{ctbrowser::style::ua_widget_mark}, id);
            }
            break;
        }
        case control_kind::button: {
            // A BUTTON IS NOT A SELECT. Sharing this arm gave every button the
            // drop-down arrow and asked selected_option() for its label - which
            // looks for <option> children a button does not have, so every
            // button on the page was an empty box with an arrow in it.
            //
            // `<button>` no longer reaches here at all - it is not a replaced
            // element - so this is `<input type=button|submit|reset>`, whose
            // border comes from the UA sheet like every other control's.
            const std::string label = button_label(txn, id, control, type);
            if (!label.empty()) { label_text(box, label, id, style, into); }
            break;
        }
        case control_kind::select: {
            // NO FRAME OF ITS OWN. The UA sheet gives every control a real
            // border, so the recorder has already drawn one - and drawing a
            // second here put two lines on every field and made a styled control
            // impossible to restyle.
            // The SELECTED OPTION'S TEXT. This drew an empty rectangle before -
            // it passed an empty string as the label and never read <option> at
            // all, so a select looked like a bug rather than like a control.
            const std::string label = selected_option(txn, id);
            if (!label.empty()) {
                const float size = font_size_of(id);
                into.text(rect{content.x, box.y + baseline_inset(box, size),
                               std::max(0.0f, content.width - 20), size * 1.25f},
                          label, size, control_text_colour(id, style), id, paint_face_of(id));
            }
            // The drop-down arrow, in the gutter the intrinsic width reserves.
            const float arrow = 4;
            const float cx = box.x + box.width - 12;
            const float cy = box.y + box.height / 2 - arrow / 2;
            for (float row = 0; row < arrow; ++row) {
                into.fill(rect{cx - (arrow - row), cy + row, 2 * (arrow - row), 1}, frame, id);
            }
            break;
        }
        case control_kind::text:
        case control_kind::textarea: {
            // NO BACKGROUND OF ITS OWN. `input, textarea { background-color:
            // #ffffff }` is in the UA sheet and the recorder has already painted
            // it - underneath the border it also painted. Filling the border box
            // again here covered that border, so every field lost its frame the
            // moment the frame became a real CSS border.
            // ONLY THE FOCUS RING. The resting border is the UA sheet's and the
            // recorder drew it; this is the one line here that is not a CSS
            // border but a state indicator, and it belongs to the widget.
            if (focused) { outline(box, accent, into, id); }
            (void)field;
            paint_field_text(box, id, control, kind, style, focused, into);
            break;
        }
        case control_kind::none: break;
        }
    }

    // THE TICK IN A CHECKED CHECKBOX.
    //
    // What was here was a white square inset a quarter of the box - at 13x13, a
    // 6px white square inside a blue one, which reads as an empty blue RING.
    // Checked and unchecked differed by a thin border and nothing else, so a
    // checked box looked unchecked.
    //
    // Drawn as a staircase of 1px rows, the same way the select's drop-down
    // triangle is: `paint_op` has fill_rect, fill_ellipse, text_run, image and
    // the two clips, and nothing else - no line, no path, no transform - so a
    // stroke at 45 degrees is not expressible and a stack of short rows is what
    // a diagonal IS here. A glyph is not an option either: the goldens render
    // with font8x8, which has no U+2713.
    //
    // Two rows per step so the stroke reads as a stroke at 13px rather than as
    // a dotted line, and the short arm rises half as far as the long one, which
    // is what makes it a tick rather than a V.
    void check_mark(const rect & box, ctbrowser::paint::display_list & into, node_id id) {
        const color mark{ctbrowser::style::ua_widget_mark};
        const float unit = std::max(1.0f, std::round(box.width / 13.0f));
        // The elbow sits below and left of centre, where a drawn tick's does.
        const float elbow_x = box.x + box.width * 0.42f;
        const float elbow_y = box.y + box.height * 0.72f;
        const float thick = unit * 2;
        // Down-right into the elbow, then up-right and twice as far.
        for (float step = 0; step < 3; ++step) {
            into.fill(rect{elbow_x - (3 - step) * unit, elbow_y - (3 - step) * unit - thick, unit,
                           thick + unit},
                      mark, id);
        }
        for (float step = 0; step < 5; ++step) {
            into.fill(rect{elbow_x + step * unit, elbow_y - step * unit - thick, unit, thick}, mark,
                      id);
        }
    }

    // A button's label is CENTRED, horizontally and on the box's middle. Left
    // aligned at a fixed inset it drifts off centre the moment the button is
    // wider than its text, which is every button with a width.
    void label_text(const rect & box, const std::string & label, node_id id,
                    const ctbrowser::style::computed_style_ptr & style,
                    ctbrowser::paint::display_list & into) {
        const float size = font_size_of(id);
        const float width = measure()(label, size, face_of(id));
        const rect inner = content_box_of(id, box);
        const float x = inner.x + std::max(0.0f, (inner.width - width) / 2);
        into.text(rect{x, box.y + baseline_inset(box, size), box.width - (x - box.x), size * 1.25f},
                  label, size, control_text_colour(id, style), id, paint_face_of(id));
    }

    // Where a single line of text sits inside a control: vertically centred on
    // the box rather than pinned to a constant, so a control that is taller
    // than its text (every one with a border and padding) still centres it.
    [[nodiscard]] static float baseline_inset(const rect & box, float size);

    // A control's text geometry, in ONE place. The painter draws from it and a
    // click is mapped through it, so a caret cannot land where the text is not:
    // two copies of "where does line 2 start" is how a click ends up putting
    // the caret somewhere the glyphs never were.
    struct field_layout {
        rect inner;
        float size = 16;
        float line_height = 20;
        ctbrowser::layout::text_face metrics_face;
        std::vector<std::pair<std::size_t, std::size_t>> lines; // [begin, end) in the VALUE
        bool masked = false;
        // How many lines fit in the box. A textarea is a replaced box sized by
        // its `rows`, so wrapping can produce more lines than it can show and
        // the rest are scrolled to rather than grown into. At least one, or a
        // box too short for a single line could never show the caret.
        std::size_t visible_lines = 1;
        // THE EFFECTIVE VIEW ORIGIN - control_state's, clamped to what the
        // value and the box currently are. Everything that draws or hit-tests
        // reads these rather than the stored ones, so a scroll left stale by a
        // shrinking value, a scripted `el.value =`, or a resize that rewraps to
        // fewer lines can never be OBSERVED: it is corrected here, once, where
        // the geometry is derived. The stored value is only ever a request.
        std::size_t scroll_line = 0;
        float scroll_x = 0;
        // The widest line, which is how far right there is to scroll.
        float content_width = 0;
    };

    // THE CONTENT BOX OF A CONTROL, in ONE place.
    //
    // The caret, the hit test, the selection and the painter all have to agree
    // about where a field's text starts, and they used to agree only because
    // each repeated the same `control_padding` constant - which layout had a
    // second copy of and which no stylesheet could change. They agree now
    // because they all ask this, and it asks the cascade.
    [[nodiscard]] rect content_box_of(node_id id, const rect & box) const {
        const layout::box_node * found = nullptr;
        const auto walk = [&](auto && self, const layout::box_node & at) -> void {
            if (found != nullptr) { return; }
            if (at.source == id) {
                found = &at;
                return;
            }
            for (const layout::box_node & c : at.children) { self(self, c); }
        };
        walk(walk, boxes_);
        if (found == nullptr) { return box; }
        const layout::resolved_edges e = layout::resolve_edges(
            *found, layout::constraints{box.width, box.height, found->font_size});
        return rect{box.x + e.content_left(), box.y + e.content_top(),
                    std::max(0.0f, box.width - e.horizontal_inner()),
                    std::max(0.0f, box.height - e.vertical_inner())};
    }

    [[nodiscard]] field_layout layout_of_field(const rect & border_box, node_id id,
                                               const control_state & control, control_kind kind) {
        // EVERY caller hands over the border box and this converts once, which is
        // what stops the four of them drifting apart again.
        const rect box = content_box_of(id, border_box);
        field_layout out;
        out.size = font_size_of(id);
        out.metrics_face = face_of(id);
        out.line_height = out.size * 1.25f;
        out.masked = is_password(id);
        const bool multiline = kind == control_kind::textarea;
        out.inner = rect{box.x, box.y + (multiline ? 0 : baseline_inset(box, out.size)), box.width,
                         box.height};
        // A single-line field does not wrap - it scrolls horizontally, which it
        // has always done by clipping. Only a textarea gets visual lines.
        out.lines =
            multiline
                ? value_lines(control.value, out.inner.width, out.size, out.metrics_face, measure())
                : std::vector<std::pair<std::size_t, std::size_t>>{{0, control.value.size()}};
        out.visible_lines = out.line_height > 0
                                ? std::max<std::size_t>(1, static_cast<std::size_t>(
                                                               out.inner.height / out.line_height))
                                : 1;
        // Clamp the requested view to what there is to look at.
        const std::size_t most =
            out.lines.size() > out.visible_lines ? out.lines.size() - out.visible_lines : 0;
        out.scroll_line = std::min(control.scroll_line, most);
        // The WIDEST line, not the current one: a field scrolls its content as
        // a whole, so moving between a long line and a short one must not jerk
        // the view sideways.
        for (const auto & [begin, end] : out.lines) {
            const std::string_view line{control.value.data() + begin, end - begin};
            out.content_width = std::max(
                out.content_width, measure()(shown(line, out.masked), out.size, out.metrics_face));
        }
        // `+ 1` because the caret is a 1px bar drawn AT the caret's x: without
        // the extra pixel a caret at the very end of the value sits exactly on
        // the clip's right edge and is invisible.
        out.scroll_x = std::clamp(control.scroll_x, 0.0f,
                                  std::max(0.0f, out.content_width + 1 - out.inner.width));
        return out;
    }

    // `<input type=password>` shows BULLETS. Masked per CODE POINT rather than
    // per byte, so a value with anything non-ASCII in it does not come out with
    // three bullets for one character - and the caret, which counts the same
    // way, still lands between them.
    // A control is disabled by its own attribute or by an enclosing <fieldset>,
    // which is how a form greys out a whole section at once.
    [[nodiscard]] bool is_disabled(node_id id);

    [[nodiscard]] bool is_password(node_id id);
    [[nodiscard]] static std::string masked_text(std::string_view text);
    // What the user SEES for a stretch of the value.
    [[nodiscard]] static std::string shown(std::string_view text, bool masked);
    [[nodiscard]] static std::size_t next_code_point(std::string_view text, std::size_t at);

    // Where in a control's value a point falls. The nearest character boundary
    // on the line the point is on - which is the ONLY way a click can put the
    // caret where the user pointed, and a textarea had no such path at all.
    [[nodiscard]] std::size_t offset_at_point(node_id id, const control_state & control,
                                              control_kind kind, float x, float y) {
        const rect box = viewport_box_of(id);
        if (box.empty()) { return control.caret; }
        const field_layout geometry = layout_of_field(box, id, control, kind);
        if (geometry.lines.empty()) { return 0; }

        // Relative to the FIRST VISIBLE line, not to the first line: a click
        // maps through the same scroll offset the painter drew with, or a
        // scrolled textarea puts the caret several lines from where you
        // pointed.
        const auto line_index =
            static_cast<std::ptrdiff_t>(std::floor((y - geometry.inner.y) / geometry.line_height)) +
            static_cast<std::ptrdiff_t>(geometry.scroll_line);
        const std::size_t index = static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(
            line_index, 0, static_cast<std::ptrdiff_t>(geometry.lines.size()) - 1));
        const auto [begin, end] = geometry.lines[index];
        const std::string_view line{control.value.data() + begin, end - begin};

        // Nearest BOUNDARY, not nearest character: clicking the right half of a
        // glyph puts the caret after it, which is what makes clicking at the
        // end of a word land where you meant.
        //
        // `+ scroll_x` puts the click back into VALUE space: `where` below is
        // measured from the start of the line, `x` arrives in box space, and
        // the scroll is exactly the difference. Same sign convention as the
        // `+ scroll_line` above, and the exact inverse of the `- dx` the
        // painter applies.
        const float want = x - geometry.inner.x + geometry.scroll_x;
        std::size_t best = 0;
        float best_distance = std::numeric_limits<float>::infinity();
        for (std::size_t at = 0; at <= line.size();
             at = at < line.size() ? next_code_point(line, at) : line.size() + 1) {
            const float where = measure()(shown(line.substr(0, at), geometry.masked), geometry.size,
                                          geometry.metrics_face);
            if (const float distance = std::fabs(where - want); distance < best_distance) {
                best_distance = distance;
                best = at;
            }
            if (at == line.size()) { break; }
        }
        return begin + best;
    }

    // Move the caret one VISUAL LINE, keeping the column. A textarea without
    // this has arrow keys that walk character by character through a newline,
    // which is not what up and down mean.
    bool move_caret_by_line(node_id id, control_state & control, control_kind kind, int direction,
                            bool extend) {
        const rect box = viewport_box_of(id);
        if (box.empty()) { return false; }
        const field_layout geometry = layout_of_field(box, id, control, kind);
        // The same rule the painter uses, so Up/Down start from the line the
        // caret is DRAWN on. Two copies of "which line is this" is how the
        // caret and the arrow keys start disagreeing at a wrap boundary.
        const std::size_t index = caret_line(geometry, control.caret);
        const auto target = static_cast<std::ptrdiff_t>(index) + direction;
        if (target < 0 || target >= static_cast<std::ptrdiff_t>(geometry.lines.size())) {
            return false;
        }
        // The COLUMN is a distance, not a character count: two lines of the
        // same text in a proportional font do not share character positions.
        const auto [begin, end] = geometry.lines[index];
        const float column =
            measure()(shown(std::string_view{control.value}.substr(begin, control.caret - begin),
                            geometry.masked),
                      geometry.size, geometry.metrics_face);
        const auto [to_begin, to_end] = geometry.lines[static_cast<std::size_t>(target)];
        const std::string_view line{control.value.data() + to_begin, to_end - to_begin};
        std::size_t best = 0;
        float best_distance = std::numeric_limits<float>::infinity();
        for (std::size_t at = 0; at <= line.size();
             at = at < line.size() ? next_code_point(line, at) : line.size() + 1) {
            const float where = measure()(shown(line.substr(0, at), geometry.masked), geometry.size,
                                          geometry.metrics_face);
            if (const float distance = std::fabs(where - column); distance < best_distance) {
                best_distance = distance;
                best = at;
            }
            if (at == line.size()) { break; }
        }
        control.caret = to_begin + best;
        if (!extend) { control.selection = control.caret; }
        return true;
    }

    // THREE THINGS MOVE A FIELD'S VIEW, and they must never run on the same
    // event or they fight each other. Every bug in this area is a violation of
    // this split, so it is written here once:
    //
    //   1. The CARET moves and the view follows - typing, editing keys, paste,
    //      cut, select-all, `el.value =`. That is reveal_caret, below. The
    //      caret drives.
    //   2. The USER moves the view directly - the wheel. The scroll moves and
    //      the caret does NOT; it is left off screen if that is where it was,
    //      which is what every browser does. Nothing re-reveals here: calling
    //      reveal_caret on a wheel would snap the view straight back and make
    //      the wheel useless. The next keystroke brings it back, by rule 1.
    //   3. AUTO-SCROLL during a drag - the scroll steps and the caret is then
    //      re-derived from where the pointer is. The view drives.

    // Scroll a field the MINIMUM needed to bring the caret back into view, in
    // BOTH axes. Called after anything that moves the caret or changes the
    // value - typing off the edge of a box that cannot grow is otherwise typing
    // into somewhere you cannot see.
    //
    // Both axes for both kinds. The vertical half is a natural no-op for a
    // single-line field, which has exactly one line; the horizontal half is NOT
    // a no-op for a textarea, because an unbreakable word longer than the line
    // overflows sideways there too.
    void reveal_caret(node_id id, control_state & control, control_kind kind) {
        if (kind != control_kind::text && kind != control_kind::textarea) { return; }
        const rect box = viewport_box_of(id);
        if (box.empty()) { return; }
        const field_layout geometry = layout_of_field(box, id, control, kind);
        if (geometry.lines.empty()) { return; }
        const std::size_t on_line = caret_line(geometry, control.caret);
        bool moved = false;

        // --- vertical ---
        const std::size_t visible = std::max<std::size_t>(1, geometry.visible_lines);
        // From the EFFECTIVE origin, not the stored one, so this converges on
        // the clamp in layout_of_field instead of arguing with it.
        std::size_t first = geometry.scroll_line;
        if (on_line < first) {
            first = on_line;
        } else if (on_line >= first + visible) {
            first = on_line - visible + 1;
        }
        // Never leave blank rows below a value that would fill them: deleting
        // a long value while scrolled down otherwise shows an empty box.
        const std::size_t most =
            geometry.lines.size() > visible ? geometry.lines.size() - visible : 0;
        first = std::min(first, most);
        if (first != control.scroll_line) {
            control.scroll_line = first;
            moved = true;
        }

        // --- horizontal ---
        const auto [begin, end] = geometry.lines[on_line];
        const std::string_view line{control.value.data() + begin, end - begin};
        const auto advance = [&](std::size_t upto) {
            return measure()(shown(line.substr(0, upto), geometry.masked), geometry.size,
                             geometry.metrics_face);
        };
        const float caret_x = advance(control.caret >= begin ? control.caret - begin : 0);
        float scroll = geometry.scroll_x;
        // A pixel of slack on the right so the caret bar itself is inside the
        // box rather than drawn on its edge.
        if (caret_x < scroll) {
            scroll = caret_x;
        } else if (caret_x > scroll + geometry.inner.width - 1) {
            scroll = caret_x - geometry.inner.width + 1;
        }
        // ...and never scrolled past the end of the content, for the same
        // reason as `most` above: a shrinking value must not leave an empty box.
        scroll = std::clamp(scroll, 0.0f,
                            std::max(0.0f, geometry.content_width + 1 - geometry.inner.width));
        if (scroll != control.scroll_x) {
            control.scroll_x = scroll;
            moved = true;
        }
        if (moved) { mark(dirty::paint); }
    }

    // Home and End are per LINE in a textarea, as they are in every editor.
    bool move_to_line_edge(node_id id, control_state & control, control_kind kind, bool to_end,
                           bool extend) {
        const rect box = viewport_box_of(id);
        if (box.empty()) { return false; }
        const field_layout geometry = layout_of_field(box, id, control, kind);
        for (const auto & [begin, end] : geometry.lines) {
            if (control.caret < begin || control.caret > end) { continue; }
            control.caret = to_end ? end : begin;
            if (!extend) { control.selection = control.caret; }
            return true;
        }
        return false;
    }

    void paint_field_text(const rect & box, node_id id, const control_state & control,
                          control_kind kind, const ctbrowser::style::computed_style_ptr & style,
                          bool focused, ctbrowser::paint::display_list & into) {
        const field_layout geometry = layout_of_field(box, id, control, kind);
        const rect inner = geometry.inner;
        const float size = geometry.size;
        const float line_height = geometry.line_height;
        const ctbrowser::paint::font_face face = paint_face_of(id);
        // MEASURED WITH THE FONT THAT DRAWS IT, and with the text that IS
        // drawn - a password's bullets are wider than its letters, so measuring
        // the letters puts the caret inside the bullets.
        const auto advance = [&](std::string_view text) {
            return measure()(shown(text, geometry.masked), size, geometry.metrics_face);
        };

        // Scrolled out to the left. Subtracted from every x below; offset_at_point
        // adds the same number back, and those two must always agree - it is
        // what the one-place-for-geometry rule exists to protect.
        const float dx = geometry.scroll_x;

        // The field clips its own contents: a value longer than the box must not
        // paint over the page beside it. Clipped to the inner box HORIZONTALLY,
        // because a run's rect width does not bound it - the clip is the only
        // thing that does - so with a scroll offset the glyphs would otherwise
        // slide into the padding gutter and under the border.
        //
        // Vertically it stays the full BOX. `inner` is six pixels shorter, and
        // clipping to that shaves the descenders off the bottom row and cuts
        // the caret's line-height bar short.
        into.push_clip(rect{inner.x, box.y, inner.width, box.height});
        const std::size_t from = std::min(control.caret, control.selection);
        const std::size_t to = std::max(control.caret, control.selection);
        // Where the caret is, decided ONCE. Drawn per-line without this, a
        // caret sitting on a soft-wrap boundary belongs to two lines and gets a
        // bar on each.
        const std::size_t on_line = caret_line(geometry, control.caret);
        // Lines above the scroll offset are not drawn at all, and the rest are
        // positioned relative to it. This and offset_at_point below must agree,
        // which is the whole reason the geometry lives in one place.
        const std::size_t first = geometry.scroll_line;
        for (std::size_t index = first; index < geometry.lines.size(); ++index) {
            const auto [begin, end] = geometry.lines[index];
            const std::string_view line{control.value.data() + begin, end - begin};
            const float y = inner.y + static_cast<float>(index - first) * line_height;
            // Past the bottom of the box: the clip would drop it anyway, and
            // stopping here means a long value costs nothing to skip.
            if (y > inner.y + inner.height) { break; }
            if (from != to && from < end && to > begin) {
                const std::size_t a = std::max(from, begin) - begin;
                const std::size_t b = std::min(to, end) - begin;
                into.fill(rect{inner.x - dx + advance(line.substr(0, a)), y,
                               advance(line.substr(a, b - a)), line_height},
                          color{ctbrowser::style::ua_selection_highlight}, id);
            }
            if (!line.empty()) {
                // The rect's WIDTH is the run's true advance, not the box's.
                // Nothing clips a glyph to it - both backends read only
                // `where.x`/`where.y` - but display_list::intersecting culls by
                // these bounds per tile, so a rect narrower than the glyphs
                // drops the whole run in a tile it visibly covers. It is also
                // what an underline band is measured against.
                into.text(rect{inner.x - dx, y, advance(line), line_height},
                          shown(line, geometry.masked), size, control_text_colour(id, style), id,
                          face);
            }
            // The caret sits on the line CONTAINING it - see caret_line(),
            // which puts a caret on a boundary at the end of the earlier line,
            // as browsers do.
            if (focused && caret_visible() && index == on_line) {
                into.fill(rect{inner.x - dx + advance(line.substr(0, control.caret - begin)), y, 1,
                               line_height},
                          control_text_colour(id, style), id);
            }
        }
        into.pop_clip();
    }

    // A value's VISUAL lines as [begin, end) offsets: split on newlines, then
    // soft-wrapped within each of those to `wrap_width`. Always at least one,
    // so an empty value still has a line for the caret to be on.
    //
    // THE TWO BREAKS DIFFER, and everything downstream depends on how. A hard
    // '\n' is CONSUMED, so the next line begins at end + 1. A soft break
    // consumes nothing, so the next line begins exactly AT end - which is how a
    // consumer tells them apart without a flag, and why caret_line() below has
    // to exist. Trailing spaces stay on the earlier line, inside [begin, end):
    // browsers hang them the same way, and it is what keeps end == begin true.
    //
    // A zero or negative width means no wrapping - a degenerate box must not
    // turn into an infinite loop of empty lines.
    [[nodiscard]] static std::vector<std::pair<std::size_t, std::size_t>> value_lines(
        const std::string & value, float wrap_width, float size,
        const ctbrowser::layout::text_face & face,
        const ctbrowser::layout::measure_text_fn & measure) {
        std::vector<std::pair<std::size_t, std::size_t>> out;
        std::size_t begin = 0;
        for (std::size_t at = 0; at <= value.size(); ++at) {
            if (at != value.size() && value[at] != '\n') { continue; }
            // One hard segment, [begin, at). Wrapped greedily with the SAME
            // rule the page's inline layout uses, so a field and the text
            // around it never disagree about where a line breaks.
            std::size_t from = begin;
            while (wrap_width > 0 && from < at) {
                const std::string_view rest{value.data() + from, at - from};
                if (measure(rest, size, face) <= wrap_width) { break; }
                const std::size_t take =
                    ctbrowser::layout::words_that_fit(rest, wrap_width, size, face, measure);
                // words_that_fit only returns 0 when there is no room at all,
                // and it returns an over-long word whole otherwise - but guard
                // anyway, because a 0 here would never terminate.
                if (take == 0 || take >= rest.size()) { break; }
                out.emplace_back(from, from + take);
                from += take;
            }
            out.emplace_back(from, at);
            begin = at + 1;
        }
        return out;
    }

    // The line a caret is ON, as an index into `lines`. FIRST match wins.
    //
    // This has to be a real guard rather than a convention, now that lines soft
    // wrap. With hard breaks alone a line's end was the '\n' and the next
    // line's begin was one past it, so `caret >= begin && caret <= end` could
    // only ever match once and a loop with no `break` got away with it. A soft
    // break makes end == begin, so a caret sitting on a wrap boundary matches
    // BOTH lines - and the painter, which draws per line, drew TWO carets.
    [[nodiscard]] static std::size_t caret_line(const field_layout & geometry, std::size_t caret) {
        for (std::size_t index = 0; index < geometry.lines.size(); ++index) {
            const auto [begin, end] = geometry.lines[index];
            if (caret >= begin && caret <= end) { return index; }
        }
        return geometry.lines.empty() ? 0 : geometry.lines.size() - 1;
    }

    [[nodiscard]] std::string button_label(const read_txn & txn, node_id id,
                                           const control_state & control,
                                           std::string_view type) const {
        if (!control.value.empty()) { return control.value; }
        if (type == "submit") { return "Submit"; }
        if (type == "reset") { return "Reset"; }
        std::string text;
        const auto walk = [&](auto && self, node_id at) -> void {
            text += txn.text(at);
            for (const node_id child : txn.children(at)) { self(self, child); }
        };
        walk(walk, id);
        return text;
    }

    // The colour a control's own text is drawn in. A DISABLED control ignores
    // the cascade here: `color` on a disabled button is not what a user needs
    // to see, and greyed-out is the only signal the control is dead.
    [[nodiscard]] color control_text_colour(node_id id,
                                            const ctbrowser::style::computed_style_ptr & style) {
        return is_disabled(id) ? color{ctbrowser::style::ua_widget_disabled_text}
                               : text_colour(style);
    }

    [[nodiscard]] color text_colour(const ctbrowser::style::computed_style_ptr & style);

    // The face a control's text is drawn in - the same one layout measured it
    // with. Measuring a caret position with a different font from the one that
    // drew the text is how the caret ends up a character or two past the end of
    // what you typed.
    [[nodiscard]] ctbrowser::layout::text_face face_of(node_id id) const;

    // The same face, as the paint layer names it. The two types are separate on
    // purpose - layout may not import paint - so the conversion is explicit,
    // and every control's text MUST go through it: drawing a control's text
    // with the default face while measuring the caret with the element's own
    // is a caret that drifts further right with every character typed. A
    // textarea is monospace by UA rule and was drawn in the default serif.
    [[nodiscard]] ctbrowser::paint::font_face paint_face_of(node_id id) const;

    [[nodiscard]] float font_size_of(node_id id) const;
    [[nodiscard]] static const layout::box_node * find_box(const layout::box_node & at,
                                                           node_id id) {
        if (at.source == id) { return &at; }
        for (const layout::box_node & child : at.children) {
            if (const layout::box_node * hit = find_box(child, id)) { return hit; }
        }
        return nullptr;
    }

    static void outline(const rect & box, color c, ctbrowser::paint::display_list & into,
                        node_id id) {
        into.fill(rect{box.x, box.y, box.width, 1}, c, id);
        into.fill(rect{box.x, box.bottom() - 1, box.width, 1}, c, id);
        into.fill(rect{box.x, box.y, 1, box.height}, c, id);
        into.fill(rect{box.right() - 1, box.y, 1, box.height}, c, id);
    }

    bool set_hover(node_id at);

    // Pseudo-state applies to the whole ancestor chain: hovering a <span>
    // inside an <a> must make the <a> hover too, or `a:hover` never fires on a
    // link with any markup inside it.
    bool set_state(node_id at, std::uint32_t bit, bool on);

    // A key event reaches SCRIPT FIRST, and the built-in behaviour - scrolling,
    // caret movement - is the DEFAULT ACTION that runs only if no listener
    // cancelled it. Handling the key first and never telling the page was why a
    // game could not read the keyboard at all: Space scrolled the document
    // instead of firing the gun.
    // The text of a <select>'s selected option: the one marked `selected`, or
    // the first, which is what a browser shows for a select nobody has touched.
    // Reads the DOM directly rather than caching, because the option list is
    // document content and a script may have just changed it.
    // The LABEL a <select> shows: the text of the option whose value is the
    // control's. Label and value are different things - `<option value=g>green
    // </option>` is worth "g" to a form and shows "green" to a reader - so the
    // control stores the value and this maps it back for display.
    [[nodiscard]] std::string selected_option(const read_txn & txn, node_id id);

    // A press while a <select> is open. Returns whether the popup consumed it -
    // a click ANYWHERE else closes it, which is what click-away means, and that
    // click must not also reach the page.
    bool handle_popup_press(const input_event & event);

    // Copy / Cut / Paste / Select All, from the context menu or from Ctrl+key.
    // The page gets a CANCELABLE event first for the three that correspond to
    // one, which is how an editor takes them over.
    // One visual line of one text node, with where it starts inside that node.
    struct text_run {
        const ctbrowser::layout::fragment * fragment = nullptr;
        node_id source;
        std::size_t offset = 0; // where this line begins in the node's text
        std::string_view text;
        rect box; // absolute, in content space
        std::size_t order = 0;
    };

    // Every text run in DOCUMENT ORDER, which is the order a selection spans.
    [[nodiscard]] std::vector<text_run> text_runs() const;

    // Where in the node's text a point falls: the nearest character boundary on
    // the nearest line. Above the first line is its start and below the last is
    // its end, so dragging past the edge takes whole lines - which is what a
    // drag off the top of a paragraph has to do.
    [[nodiscard]] text_position position_at(float x, float y);

    // The character boundary nearest an x, by MEASURING prefixes with the same
    // function layout measured the line with - so the boundary is where the
    // glyph actually is, not where a guess put it.
    [[nodiscard]] std::size_t code_point_at(const text_run & run, float x) const;

    // The part of a run that is selected, as absolute code points in its node.
    [[nodiscard]] std::pair<std::size_t, std::size_t> selected_range(const text_run & run);

    // The highlighted part of one text fragment, in the fragment's own space.
    [[nodiscard]] rect highlight_for(const ctbrowser::layout::fragment & f);

    // Copy / Cut / Paste / Select All. The wrapper reveals the caret afterwards
    // whatever the verb did; `clipboard_verb` is the verb itself.
    void run_clipboard_verb(std::string_view verb);
    void clipboard_verb(std::string_view verb);

    bool handle_key(const input_event & event);

    // Returns whether a listener cancelled the default action - NOT whether
    // anything was dispatched, because the caller's question is "may I still do
    // my own thing with this key".
    bool dispatch_key(std::string_view type, const input_event & event);
    bool dispatch_mouse(std::string_view type, node_id target, const input_event & event);
    [[nodiscard]] node_id body_node();

    // The DOM and the cascade are rebuilt on navigation, and neither type is
    // copyable - a slab with live epochs is not something to assign over.
    void reset_document();

    // The focused control's editable state, or null when focus is elsewhere.
    // Whether the focused element is one that shows a caret. Distinct from
    // editable_focus(), which SEEDS the control's state - asking when the next
    // blink is due must not create anything.
    [[nodiscard]] bool has_editable_focus();

    [[nodiscard]] control_state * editable_focus();

    [[nodiscard]] control_kind kind_of(const read_txn & txn, node_id id);

    // The element with this `id`, or nothing. One walk, because there were
    // three: the bindings' getElementById, scroll_to_fragment's own copy, and a
    // test helper.
    [[nodiscard]] node_id node_by_id(const read_txn & txn, std::string_view want);

    // The control a <label> labels, per HTML: its `for` attribute resolved by
    // id, or failing that the FIRST labelable element inside it.
    //
    // `from` is where the click landed, which for label text is the TEXT NODE -
    // hit testing returns the deepest fragment's source, never the <label>
    // itself - so this walks up to the enclosing label first.
    //
    // LABELABLE is exactly "is a control": control_kind_of already maps
    // `input type=hidden` to none, so HTML's notion falls out of what is here
    // rather than needing a second list to keep in step.
    [[nodiscard]] node_id labelled_control(const read_txn & txn, node_id from);

    // The control a click landed in. A click on the text inside a <button> has
    // to focus the button, not the text node - and a click on a <label>'s text
    // has to reach the control that label names, which is a SIBLING of the text
    // rather than an ancestor of it, so the upward walk alone cannot find it.
    [[nodiscard]] node_id control_ancestor(node_id from);

    // Whether `from` reaches its control only THROUGH a label. A label click
    // focuses and activates, but must not place a caret or begin a selection:
    // the pointer is over the label's text, nowhere near the field's glyphs,
    // so mapping the click through offset_at_point would put the caret at
    // whichever end of the value the label happened to sit on.
    [[nodiscard]] bool via_label(node_id from);

    bool focus(node_id id);

    // Every control the user can Tab to, in DOCUMENT ORDER.
    //
    // Document order IS the tab order here, because there is no `tabindex` -
    // which is also what a document without one gets in a real browser. Two
    // deliberate gaps, so nobody has to rediscover them: a positive `tabindex`
    // does not reorder anything, and each radio button is its own stop rather
    // than a group being one.
    [[nodiscard]] std::vector<node_id> focusable_controls();

    // A control takes focus if it IS one, is not disabled (by its own attribute
    // or an enclosing <fieldset>'s), and is actually RENDERED. `display: none`
    // leaves no fragment, and tabbing into something nobody can see is how
    // focus appears to vanish. Note a control scrolled off-screen still HAS a
    // fragment and so stays tabbable, which is correct - it is reachable, just
    // not visible yet.
    [[nodiscard]] bool is_focusable(const read_txn & txn, node_id id);

    // Sequential focus navigation. Wraps at both ends, so Tab off the last
    // control returns to the first rather than dropping focus into nothing.
    bool focus_next(bool backwards);

    // AUTO-SCROLL WHILE DRAG-SELECTING - rule 3 of the three at reveal_caret.
    //
    // Holding the pointer outside a field you are selecting in has to keep
    // scrolling it, with no further mouse events: offset_at_point clamps to
    // what the value has, so a stationary pointer one pixel below the box picks
    // the same offset forever and the selection freezes one line short.
    //
    // Built on the caret blink's shape - the one clock tick() already advances,
    // a due time, and a next_wakeup_ms contribution - because that is how this
    // engine does "happens on a timer" and an idle loop must still block.
    //
    // THE VIEW DRIVES HERE, the caret follows. reveal_caret is the other way
    // round, so the drag path must never call it: one moves the scroll to
    // follow the caret and the other moves the caret to follow the scroll, and
    // together they oscillate.

    // How far outside its field the pointer is, on each axis. Zero when inside.
    // A step is due only when this is non-zero AND the scroll can still move
    // that way - otherwise the wakeup is never scheduled and an idle loop with
    // a pointer parked below a fully-scrolled field does not spin.
    struct autoscroll_state {
        node_id field;
        float below = 0;  // >0 down, <0 up
        float beside = 0; // >0 right, <0 left
        [[nodiscard]] bool live() const noexcept { return field && (below != 0 || beside != 0); }
    };
    [[nodiscard]] autoscroll_state autoscroll_now();
    [[nodiscard]] double autoscroll_interval_ms(float distance) const;
    // One step. Moves the view, then re-derives the caret from where the
    // pointer actually is.
    void autoscroll_step(const autoscroll_state & state);

    // A wheel notch aimed at a scrollable field. True when the field took it.
    //
    // Rule 2 of the three at reveal_caret: this moves the VIEW and leaves the
    // caret alone, off screen if that is where it was. Revealing the caret here
    // would snap the view straight back and make the wheel useless; the next
    // keystroke brings it back instead, which is what browsers do.
    [[nodiscard]] bool scroll_field_under(const input_event & event);

    bool edit_key(control_state & control, const input_event & event);

    bool edited(bool changed);
    bool moved(bool changed);

    // What clicking a control does once no listener has cancelled it.
    void activate(node_id target);

    // Clicking a <summary> opens or closes its <details>. The state is the
    // `open` ATTRIBUTE, as the spec says, so a script that reads or sets it
    // agrees with what the user did - and layout, which builds a closed
    // details' children away, picks it up from the same place.
    bool toggle_details(node_id target);

    // <a href> - the one navigation-shaped thing a document does on its own.
    // Nearest <a> ANCESTOR, not the clicked node: a link's text, and anything
    // else inside it, is what actually gets clicked.
    bool follow_link(node_id target);

    // An `<a download>` writes its bytes out instead of navigating. See the
    // definition: this is the one behaviour this engine invents.
    bool save_download(const std::string & href, const std::string & suggested);

    std::function<double()> clock_;
    std::filesystem::path download_directory_;
    std::vector<download_record> downloads_;
    std::function<void(const download_record &)> download_hook_;

    // Put the element with that id at the top of the viewport, clamped the same
    // way a scroll is - an anchor near the end of a short page cannot scroll
    // past the bottom.
    void scroll_to_fragment(std::string_view id);

    void submit(node_id form);

public:
    // What the last submission would have sent. There is no network, so
    // producing the data and stopping is the honest half of submitting - and it
    // is what a test can check.
    [[nodiscard]] const std::vector<std::pair<std::string, std::string>> & last_submission()
        const noexcept {
        return last_submission_;
    }

private:
    std::vector<std::pair<std::string, std::string>> last_submission_;

    browser_options options_;
    atom_table atoms_;
    std::unique_ptr<document> doc_;
    std::unique_ptr<ctbrowser::style::engine> styles_;
    ctbrowser::style::style_map resolved_;
    ctbrowser::paint::recorder recorder_;
    box_node boxes_;
    fragment fragments_;
    layer_tree layers_;
    renderer renderer_;

    // Declared BEFORE the context, so it is destroyed AFTER it. Closures hold
    // `const function_proto *` into the program, and a timer or a listener runs
    // long after run_scripts() returned - so the program has to outlive both the
    // call that compiled it and the context that executes it.
    // Null means font8x8, which is always available and always identical - so a
    // build with no font files still renders and its goldens still compare.
    std::size_t page_layers_ = 0; // how many of layers_ are the page's
    node_id select_open_;         // the <select> whose popup is showing
    // The page's own clipboard, used when no system one is installed - which is
    // every headless run.
    std::string clipboard_;
    std::function<void(const std::string &)> clipboard_write_;
    std::function<std::string()> clipboard_read_;
    // The control a drag is selecting inside, if any. Empty means the drag is
    // a page selection, or there is no drag.
    node_id field_selecting_;
    // Where the pointer last was. tick() gets no coordinates, so an auto-repeat
    // that has to keep aiming at the pointer needs it remembered.
    point pointer_;
    bool have_pointer_ = false;
    // When the next auto-scroll step falls due, on caret_clock_ms_'s scale.
    double autoscroll_due_ms_ = 0;
    std::function<void(const std::string &)> alert_hook_;
    std::vector<std::string> alerts_;
    std::function<void(const std::string &)> navigate_hook_;
    std::string source_html_;
    std::size_t layouts_ = 0;
    frame_timing timing_;
    double caret_clock_ms_ = 0;
    double caret_base_ms_ = 0;
    std::string location_href_;
    std::string location_hash_;
    bool menu_open_ = false;
    point menu_at_;
    // The two ends of the page selection, and whether the pointer is currently
    // dragging one of them.
    text_position selection_anchor_;
    text_position selection_focus_;
    bool selecting_ = false;
    bool sb_dragging_ = false;
    float sb_grab_ = 0; // where in the thumb the drag started
    const ctbrowser::raster::font_backend * fonts_ = nullptr;
#if CTBROWSER_WITH_TTF
    // Owned so its glyph cache outlives any one frame; the renderer only
    // borrows it.
    std::unique_ptr<ctbrowser::raster::ttf_backend> ttf_;
#endif
    std::vector<std::pair<std::string, script::native_fn>> embedder_natives_;
    asset_registry assets_;
    image_store images_;
    // Decoded once per document, and held here rather than on the node: the DOM
    // stays free of rendering state, which is the whole reason forms and canvas
    // pixels live outside it too.
    std::vector<std::pair<node_id, std::shared_ptr<const ctbrowser::paint::bitmap>>>
        images_by_node_;
    bool network_allowed_ = true;
    // SVG sources and the rasters made from them. Beside images_ rather than
    // inside it: see shell/page/svg_cache.hpp for why a vector graphic cannot share a
    // decode-once-by-name cache.
    svg_store svg_;
    canvas_store canvases_;
    form_store forms_;
    node_id focused_;
    std::uint64_t canvas_revision_ = 0;

    // ONE PROGRAM PER CLASSIC <script>, kept alive for the page's lifetime, for
    // the same reason the modules below are: a function declared at a script's
    // top level holds a `const function_proto *` into its program, and a timer
    // or an event listener dereferences it long after run_scripts returned.
    std::vector<std::unique_ptr<script::program>> classic_programs_;
    // PRECOMPILED IMAGES, KEYED BY WHAT THEY WERE BUILT FROM. See
    // add_script_image. A vector rather than a map on purpose: a page has a
    // handful of scripts and a packager hands over a few dozen images, so a
    // linear scan over 64-bit keys is nothing next to a hash - and the public
    // header gains no include for a container it does not need.
    struct held_image {
        std::uint64_t source_hash = 0;
        script::script_kind kind = script::script_kind::classic;
        script::image_option option = script::image_option::keep_source;
        std::vector<std::byte> bytes;
    };
    std::vector<held_image> script_images_;
    std::vector<std::string> script_sources_;
    std::vector<std::string> module_sources_;
    // A LOAD IS IN FLIGHT, AND A SCRIPT ASKED FOR ANOTHER ONE. See load_html:
    // a navigation from inside a script is queued and performed after the
    // scripts stop, because doing it where it was asked for destroys the
    // context the asking script is running on.
    bool loading_ = false;
    std::optional<std::string> pending_load_;
    std::size_t scripts_compiled_from_source_ = 0;
    // ONE PROGRAM PER MODULE, kept alive for the page's lifetime. A module's
    // top-level declarations live in its own frame and its functions close over
    // them, so the program cannot be a temporary the way a classic script's
    // could be. See docs/plans/modules.md.
    std::vector<std::unique_ptr<script::program>> module_programs_;
    // Load a module and everything it imports, depth-first, evaluating each
    // once. See the definition for why post-order is the only order that works.
    // TWO PASSES over the graph, because a cycle cannot be done in one - see
    // the definitions. load_module runs both.
    void load_module(const std::string & source, const std::string & specifier);
    void instantiate_module(const std::string & source, const std::string & specifier);
    void evaluate_module(const std::string & specifier);
    // `export ... from` and `export *`: aliases wired once the dependencies are
    // instantiated and before anything evaluates. See the definition.
    void wire_reexports(const std::string & specifier);
    // Every program run by run_script AFTER the page's own. They accumulate for
    // the life of the page because a closure from any of them may still be
    // reachable - a listener, a timer, a rAF callback - and a program that
    // outlives nothing is a use-after-free waiting for the first callback.
    std::vector<std::unique_ptr<script::program>> extra_programs_;
    std::unique_ptr<script::context> script_;
    std::unique_ptr<dom_bindings> bindings_;
    bool prefer_angle_webgl_ = false;
    std::string script_error_;
    std::string style_error_;
    std::string title_;
    float scroll_y_ = 0;
    float content_height_ = 0;
    // The width the last layout ran at: the window's, less the scrollbar when the
    // page overflows. `clientWidth` and getComputedStyle percentages are relative
    // to this and not to options_.width.
    float layout_width_ = 0;
    // The layout viewport as an integer, falling back to the window before the
    // first layout has run. Both callers of observe_viewport go through this:
    // bindings are installed LAZILY, the first time a page runs script, which is
    // after run_layout - so a setup path that reported options_.width silently
    // clobbered the narrower number layout had already used.
    [[nodiscard]] int layout_viewport_width() const noexcept {
        return layout_width_ > 0 ? static_cast<int>(layout_width_) : options_.width;
    }
    node_id hovered_;
    node_id pressed_;
    dirty dirty_ = dirty::everything;
    bool author_sheet_loaded_ = false;
    std::uint64_t frames_ = 0;
};

} // namespace ctbrowser::shell
