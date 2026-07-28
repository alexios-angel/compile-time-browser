module;
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

export module ctbrowser.shell:browser;

import ctbrowser.core;
import ctbrowser.dom;
import ctbrowser.style;
import ctbrowser.layout;
import ctbrowser.paint;
import ctbrowser.raster;
import ctbrowser.script;
import :input;
import :metrics;
import :assets;
import :images;
import :canvas;
import :forms;
import :bindings;

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

export namespace ctbrowser::shell {

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
    // The overlay scrollbar's width, and the width a tall page gives up to it.
    // 0 hides it - which is what a fixed-size game wants.
    float scrollbar_width = 15.0f;
    // Half the caret's blink period, in milliseconds - Chrome's figure. 0 stops
    // it blinking, which is what a screenshot test wants: a caret that is
    // sometimes there is not byte-comparable.
    double caret_blink_ms = 500;
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
    void use_renderer(renderer r) {
        renderer_ = std::move(r);
        mark(dirty::paint); // the new renderer has no tiles
    }
    [[nodiscard]] const renderer & rendering_with() const noexcept { return renderer_; }

    // --- content ---------------------------------------------------------

    // Replace the document. Everything downstream is invalidated, which is the
    // one case where that is the honest answer.
    void load_html(std::string_view html) {
        source_html_ = html; // what location.reload() re-runs
        // Both the document and the cascade are rebuilt. Keeping the old style
        // engine would accumulate every page's <style> rules across navigations,
        // which shows up as the previous page bleeding into the next one.
        reset_document();
        (void)parse_html(*doc_, html);
        title_ = extract_title();
        scroll_y_ = 0;
        author_sheet_loaded_ = false;
        load_inline_styles();
        // Images are resolved BEFORE layout, because an <img> with no width
        // attribute takes its size from the decoded bitmap and layout has no
        // way to ask. The page's @font-face files, for the same reason: layout
        // measures with them.
        load_images();
        load_page_fonts();
        mark(dirty::everything);
        run_scripts();
    }

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

    [[nodiscard]] bool has_selection() const noexcept {
        return selection_anchor_.node && selection_focus_.node &&
               !(selection_anchor_.node == selection_focus_.node &&
                 selection_anchor_.code_point == selection_focus_.code_point);
    }
    void clear_selection() {
        if (!selection_anchor_.node && !selection_focus_.node) { return; }
        selection_anchor_ = {};
        selection_focus_ = {};
        mark(dirty::paint);
    }
    // What is selected, in document order, as text.
    [[nodiscard]] std::string selected_text() {
        // Extracted from each NODE'S OWN TEXT over the whole selected range,
        // not by concatenating the runs. A wrap drops the space it broke at, so
        // that space is in no run at all - joining the runs would silently
        // delete a space per line from anything copied off a wrapped paragraph.
        std::string out;
        node_id current;
        std::size_t from = 0;
        std::size_t to = 0;
        const ctbrowser::layout::fragment * owner = nullptr;
        const auto flush = [&] {
            if (owner != nullptr && owner->box != nullptr && from < to) {
                const std::string_view full{owner->box->text};
                if (from < full.size()) {
                    if (!out.empty()) { out += ' '; }
                    out += full.substr(from, std::min(to, full.size()) - from);
                }
            }
        };
        for (const text_run & run : text_runs()) {
            const auto [run_from, run_to] = selected_range(run);
            if (run_from >= run_to) { continue; }
            if (run.source != current) {
                flush();
                current = run.source;
                owner = run.fragment;
                from = run_from;
                to = run_to;
            } else {
                to = std::max(to, run_to);
            }
        }
        flush();
        return out;
    }

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
    void define_native(std::string name, script::native_fn fn) {
        for (auto & [existing, handler] : embedder_natives_) {
            if (existing == name) {
                handler = std::move(fn);
                return;
            }
        }
        embedder_natives_.emplace_back(std::move(name), std::move(fn));
        if (script_) { install_embedder_natives(); }
    }

    // Turn on real fonts. Loads the vendored OFL faces through the asset
    // registry - so an application that baked them in never touches the disk -
    // and leaves font8x8 in place if SDL3_ttf is absent or none of them load.
    //
    // OPT-IN rather than automatic: the goldens are font8x8's pixels, and a
    // page that silently changed how it renders because a font file happened to
    // be next to the binary would be a worse default than one that looks the
    // same everywhere.
    bool use_real_fonts(std::string_view directory = "fonts") {
#if CTBROWSER_WITH_TTF
        auto backend = std::make_unique<ctbrowser::raster::ttf_backend>();
        if (!backend->ok()) { return false; }
        // family, then the four (bold, italic) files that make it up.
        struct vendored {
            std::string_view family;
            std::string_view stem;
        };
        for (const vendored & face :
             {vendored{"serif", "Tinos"}, vendored{"Tinos", "Tinos"},
              vendored{"sans-serif", "FiraSans"}, vendored{"Fira Sans", "FiraSans"},
              vendored{"monospace", "Cousine"}, vendored{"Cousine", "Cousine"}}) {
            for (const auto & [bold, italic, suffix] :
                 {std::tuple{false, false, "Regular"}, std::tuple{true, false, "Bold"},
                  std::tuple{false, true, "Italic"}, std::tuple{true, true, "BoldItalic"}}) {
                const std::string path =
                    std::string{directory} + "/" + std::string{face.stem} + "-" + suffix + ".ttf";
                const std::vector<std::byte> bytes = assets_.load(path);
                if (!bytes.empty()) {
                    (void)backend->add_face(std::string{face.family}, bold, italic, bytes);
                }
            }
        }
        if (backend->face_count() == 0) { return false; }
        backend->set_default_family("serif"); // what the UA sheet gives <body>
        ttf_ = std::move(backend);
        load_page_fonts();
        fonts_ = ttf_.get();
        renderer_.set_fonts(fonts_);
        // Everything measured so far was measured with the other font.
        mark(dirty::everything);
        return true;
#else
        (void)directory;
        return false;
#endif
    }
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
    void allow_network(bool allowed) {
        network_allowed_ = allowed;
        if (bindings_) { bindings_->allow_network(allowed); }
    }

    [[nodiscard]] canvas_store & canvases() noexcept { return canvases_; }
    [[nodiscard]] form_store & forms() noexcept { return forms_; }
    [[nodiscard]] node_id focused() const noexcept { return focused_; }

    // Typed text, from the platform's text-input event rather than from key
    // codes - that is the only way to get IME, dead keys and non-Latin layouts
    // right, and it is what SDL_EVENT_TEXT_INPUT delivers.
    bool text_input(std::string_view text) {
        control_state * control = editable_focus();
        if (control == nullptr || text.empty()) { return false; }
        forms_.insert_text(*control, text);
        restart_caret_blink(); // a caret that blinks out under what you typed looks broken
        bindings_->dispatch("input", focused_);
        mark(dirty::paint);
        return true;
    }
    [[nodiscard]] dom_bindings & bindings() noexcept { return *bindings_; }
    [[nodiscard]] const std::string & script_error() const noexcept { return script_error_; }

    // Run a snippet in the page's own script context - the same globals, the
    // same document. This is what a devtools console types into, and what a
    // test uses to ask a page a question. Returns whether it ran.
    bool run_script(std::string_view source) {
        if (!script_) { return false; }
        script::program compiled = script::compiler::compile(std::string{source});
        if (!compiled.ok) {
            script_error_ = compiled.error;
            return false;
        }
        const script::run_result result = script_->run(compiled);
        if (!result.ok) { script_error_ = result.error; }
        return result.ok;
    }

    // How many times layout has run. Observable because the whole dirty-level
    // design exists to keep this number down: a caret blink or a scroll must
    // not increment it.
    [[nodiscard]] std::size_t layout_count() const noexcept { return layouts_; }

    // Collect the script heap now, and how many objects it has. Exposed
    // because "does a collection free what the page is still using" is only
    // answerable from outside, and it is the question that kept the collector
    // switched off.
    std::size_t collect_garbage() { return script_ ? script_->collect() : 0; }
    [[nodiscard]] std::size_t live_script_objects() const {
        return script_ ? script_->live_objects() : 0;
    }

    // A control's live state - value, caret, selection, checked. Read-only and
    // null for anything that is not a control: this is what a test asks where
    // the caret ended up, and what an embedder asks to read a form without
    // submitting it.
    [[nodiscard]] const control_state * control_state_of(node_id id) {
        if (!id) { return nullptr; }
        const auto txn = doc_->read();
        if (!txn.contains(id) || kind_of(txn, id) == control_kind::none) { return nullptr; }
        return &forms_.state_of(txn, atoms_, id);
    }

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
    [[nodiscard]] double next_wakeup_ms() {
        double soonest = std::numeric_limits<double>::infinity();
        if (bindings_) { soonest = std::min(soonest, bindings_->next_callback_ms()); }
        if (focused_ && options_.caret_blink_ms > 0 && has_editable_focus()) {
            const double period = options_.caret_blink_ms * 2;
            const double since = std::fmod(caret_clock_ms_ - caret_base_ms_, period);
            soonest =
                std::min(soonest, since < options_.caret_blink_ms ? options_.caret_blink_ms - since
                                                                  : period - since);
        }
        return soonest;
    }

    // THE CARET BLINKS, in Chrome's 500 ms halves. The phase is measured from
    // the last caret ACTIVITY rather than from page load: a caret that blinks
    // out from under the character you just typed looks broken, so typing,
    // moving and clicking all restart it solid.
    [[nodiscard]] bool caret_visible() const noexcept {
        if (options_.caret_blink_ms <= 0) { return true; } // blinking off: always solid
        const double since = caret_clock_ms_ - caret_base_ms_;
        const double period = options_.caret_blink_ms * 2;
        return std::fmod(since, period) < options_.caret_blink_ms;
    }
    void restart_caret_blink() noexcept { caret_base_ms_ = caret_clock_ms_; }

    // Advance the page clock and run whatever became due - timers, then
    // animation frames. An event loop calls this once per tick; the return is
    // how many callbacks ran, so a caller can tell an idle page from a busy one.
    std::size_t tick(double elapsed_ms) {
        const bool was_visible = caret_visible();
        caret_clock_ms_ += elapsed_ms;
        // Only the CARET changed, so only the paint is stale - a blink must not
        // re-run layout, which is what made the previous engine lay the page out every frame.
        if (focused_ && caret_visible() != was_visible) { mark(dirty::paint); }
        bindings_->advance_clock(elapsed_ms);
        const std::size_t ran = bindings_->run_due_callbacks();
        // Collect between callbacks, never inside one - the same reason a
        // reload is drained here. Nothing was ever collected before: the GC had
        // no way to see the bindings' listeners, so running it would have freed
        // them, and so it never ran at all.
        if (script_) { (void)script_->collect_if_due(); }
        // BETWEEN callbacks, never inside one: reloading tears down the script
        // context, and location.reload() is called from a function running in
        // it. A page that reloads on game-over would take the VM with it.
        if (bindings_->reload_requested()) { reload(); }
        return ran;
    }

    // Re-parse the page and run its script again from the top - navigation to
    // where you already are, which is the only navigation the engine has.
    void reload() {
        const std::string source = source_html_;
        load_html(source); // by value: load_html clears source_html_'s referent
    }

    // The system dialog `alert()` raises. The engine is SDL-free and has no
    // window to put a dialog in, so this is a hook like the clipboard's; without
    // one the messages are still recorded and readable, which is what makes
    // alert testable headlessly.
    void set_alert_hook(std::function<void(const std::string &)> hook) {
        alert_hook_ = std::move(hook);
    }
    // Kept HERE rather than on the bindings, because a reload replaces the
    // bindings - and the alert that caused the reload is exactly the one you
    // want to still be able to read afterwards. MDN's breakout alerts and then
    // reloads in the same breath.
    [[nodiscard]] const std::vector<std::string> & alerts() const noexcept { return alerts_; }

    // Where a link that leaves this page goes. the engine does not navigate, so the
    // embedder decides - `ctbrowse` opens a local .html, the SDL app hands an
    // http(s) URL to the system browser, and a program with no hook does
    // nothing rather than pretending it followed the link.
    void set_navigate_hook(std::function<void(const std::string &)> hook) {
        navigate_hook_ = std::move(hook);
    }
    // What the last activated link recorded. A fragment lands in the hash, like
    // the previous engine, because scrolling to an anchor IS navigation within a document.
    [[nodiscard]] const std::string & location_href() const noexcept { return location_href_; }
    [[nodiscard]] const std::string & location_hash() const noexcept { return location_hash_; }

    [[nodiscard]] std::string_view title() const noexcept { return title_; }
    [[nodiscard]] const document & doc() const noexcept { return *doc_; }
    [[nodiscard]] atom_table & atoms() noexcept { return atoms_; }

    // --- viewport --------------------------------------------------------

    void resize(int width, int height) {
        if (width == options_.width && height == options_.height) { return; }
        options_.width = std::max(1, width);
        options_.height = std::max(1, height);
        // RESIZE the renderer, do not replace it. Replacing it built a fresh
        // software backend, so an app that chose the GPU silently dropped to
        // software on its first window resize and never came back.
        renderer_.resize(options_.width, options_.height);
        mark(dirty::layout);
    }
    [[nodiscard]] int width() const noexcept { return options_.width; }
    [[nodiscard]] int height() const noexcept { return options_.height; }

    // --- scrolling -------------------------------------------------------
    //
    // The payoff of the whole architecture: this touches no stage but the
    // compositor. the previous engine's equivalent re-ran layout.
    void scroll_by(float dy) { scroll_to(scroll_y_ + dy); }
    void scroll_to(float y) {
        const float clamped = std::clamp(y, 0.0f, max_scroll());
        if (clamped == scroll_y_) { return; }
        scroll_y_ = clamped;
        layers_.scroll_to(0, scroll_y_);
        // The page's tiles survive - they are in CONTENT space, which is the
        // point of the whole design - but the scrollbar's thumb is a function
        // of where we now are, so its two rectangles are redrawn AND its tile
        // is invalidated. Redrawing the display list is not enough: a tile is
        // identified by (layer, column, row), so the cached one is served again
        // and the thumb never moves. That is the "does not update" report.
        refresh_chrome();
        if (page_layers_ < layers_.layers.size()) {
            renderer_.discard_layer(static_cast<std::uint32_t>(page_layers_));
        }
        // NOT dirty otherwise. Tiles are in content space and survive this.
    }
    [[nodiscard]] float scroll_y() const noexcept { return scroll_y_; }
    [[nodiscard]] float content_height() const noexcept { return content_height_; }
    // Whether a viewport x lands on the scrollbar. Public because the app layer
    // asks it to choose a cursor.
    // What the pointer should look like at a viewport point: the CSS `cursor`
    // of the element under it, with the UA's defaults - a link is a pointer, an
    // editable is a text beam. A name rather than a handle, so the engine needs
    // no cursor vocabulary and the app layer maps it to whatever the platform
    // has.
    [[nodiscard]] std::string_view cursor_at(float x, float y) {
        if (on_scrollbar(x)) { return "default"; }
        const node_id under = hit_test(x, y);
        if (!under) { return "default"; }
        const auto txn = doc_->read();
        // The nearest ancestor that says something, because `cursor` inherits
        // and the text inside a link is not itself the link.
        for (node_id at = under; at; at = txn.parent(at)) {
            const auto found = resolved_.find(ctbrowser::style::engine::key_of(at));
            if (found != resolved_.end() && found->second) {
                const std::string_view wanted = found->second->get(atoms_.intern("cursor"));
                if (!wanted.empty()) { return wanted; }
            }
            const control_kind kind = kind_of(txn, at);
            if (kind == control_kind::text || kind == control_kind::textarea) { return "text"; }
        }
        // Bare text is selectable, and an I-beam is how a page says so.
        return txn.kind(under).value_or(node_kind::element) == node_kind::text ? "text" : "default";
    }

    [[nodiscard]] bool on_scrollbar(float x) const noexcept {
        return max_scroll() > 0 && options_.scrollbar_width > 0 &&
               x >= static_cast<float>(options_.width) - options_.scrollbar_width;
    }
    [[nodiscard]] float max_scroll() const noexcept {
        return std::max(0.0f, content_height_ - static_cast<float>(options_.height));
    }

    // --- input -----------------------------------------------------------

    // Returns whether anything changed, so a caller can skip a frame it does
    // not need. A browser that repaints on every mouse move is a browser with a
    // hot fan.
    bool handle(const input_event & event) {
        switch (event.kind) {
        case input_kind::wheel: scroll_by(-event.wheel_y * options_.wheel_step); return true;
        case input_kind::mouse_move: {
            if (sb_dragging_) {
                // The grab offset is kept so the thumb does not jump to centre
                // itself under the pointer on the first pixel of movement.
                const float height = static_cast<float>(options_.height);
                const float thumb_height = scrollbar_thumb().height;
                const float travel = height - thumb_height;
                scroll_to(travel > 0 ? (event.y - sb_grab_) / travel * max_scroll() : 0);
                return true;
            }
            if (field_selecting_) {
                // Extending a selection INSIDE a control. The anchor is the
                // control's `selection`, so dragging back over the text shrinks
                // it rather than starting again.
                const auto txn = doc_->read();
                const control_kind kind = kind_of(txn, field_selecting_);
                control_state & state = forms_.state_of(txn, atoms_, field_selecting_);
                const std::size_t at =
                    offset_at_point(field_selecting_, state, kind, event.x, event.y);
                if (at != state.caret) {
                    state.caret = at;
                    restart_caret_blink();
                    mark(dirty::paint);
                }
                return true;
            }
            if (selecting_) {
                // Extending. The ANCHOR stays put, which is what makes dragging
                // back over the text shrink the selection rather than start a
                // new one.
                const text_position at = position_at(event.x, event.y);
                if (at && (at.node != selection_focus_.node ||
                           at.code_point != selection_focus_.code_point)) {
                    selection_focus_ = at;
                    mark(dirty::paint);
                }
                return true;
            }
            const node_id under = hit_test(event.x, event.y);
            // A page tracking the pointer - MDN's breakout moves its paddle
            // this way - needs the event whether or not the hover state moved.
            const bool dispatched = dispatch_mouse("mousemove", under, event);
            return set_hover(under) || dispatched;
        }
        case input_kind::mouse_down:
            // An OPEN POPUP takes the press before anything else - it is drawn
            // over the page, so it has to be hit-tested over the page too.
            if (select_open_ && handle_popup_press(event)) { return true; }
            // The RIGHT button opens the context menu instead of pressing
            // anything, and the page gets a cancelable `contextmenu` first -
            // which is how a page that wants its own menu suppresses ours.
            if (event.button == input_event::right_button) {
                const node_id target = hit_test(event.x, event.y);
                if (!bindings_->dispatch_mouse("contextmenu", target ? target : body_node(),
                                               event)) {
                    menu_at_ = point{event.x, event.y};
                    menu_open_ = true;
                    mark(dirty::paint);
                }
                return true;
            }
            // ...and a LEFT press anywhere closes an open one.
            if (menu_open_) {
                const bool consumed = handle_menu_press(event);
                if (consumed) { return true; }
            }
            if (on_scrollbar(event.x)) {
                const rect thumb = scrollbar_thumb();
                if (event.y >= thumb.y && event.y < thumb.y + thumb.height) {
                    sb_dragging_ = true;
                    sb_grab_ = event.y - thumb.y;
                } else {
                    // A click on the TRACK pages towards the pointer, which is
                    // what every scrollbar does with one.
                    scroll_by(event.y < thumb.y ? -static_cast<float>(options_.height) * 0.9f
                                                : static_cast<float>(options_.height) * 0.9f);
                }
                mark(dirty::paint);
                return true;
            }
            pressed_ = hit_test(event.x, event.y);
            (void)dispatch_mouse("mousedown", pressed_, event);
            // A press begins a SELECTION. Inside an editable control that is a
            // selection of ITS text, anchored where the click landed; outside
            // one it is a page selection. A control had neither: clicking in a
            // textarea put the caret wherever it already was and dragging did
            // nothing at all.
            {
                const auto txn = doc_->read();
                const node_id control = control_ancestor(pressed_);
                const control_kind kind = kind_of(txn, control);
                if (kind == control_kind::text || kind == control_kind::textarea) {
                    clear_selection();
                    (void)focus(control); // before placing the caret: focus clears it
                    control_state & state = forms_.state_of(txn, atoms_, control);
                    state.caret = offset_at_point(control, state, kind, event.x, event.y);
                    state.selection = state.caret;
                    field_selecting_ = control;
                    restart_caret_blink();
                    mark(dirty::paint);
                } else if (kind == control_kind::none) {
                    const text_position at = position_at(event.x, event.y);
                    selection_anchor_ = at;
                    selection_focus_ = at;
                    selecting_ = static_cast<bool>(at);
                    mark(dirty::paint);
                } else {
                    clear_selection();
                }
            }
            return set_state(pressed_, state_active, true);
        case input_kind::mouse_up: {
            if (sb_dragging_) {
                sb_dragging_ = false;
                mark(dirty::paint);
                return true;
            }
            selecting_ = false;
            field_selecting_ = node_id{};
            bool changed = set_state(pressed_, state_active, false);
            (void)dispatch_mouse("mouseup", hit_test(event.x, event.y), event);
            // Focus follows the press, and moves even when the click lands on
            // nothing - which is how clicking the page background blurs a field.
            changed = focus(control_ancestor(pressed_)) || changed;
            // A click fires on RELEASE, at the element the press started on -
            // which is what makes dragging off a button cancel it, the way every
            // real browser behaves.
            const node_id released_on = hit_test(event.x, event.y);
            if (pressed_ && released_on == pressed_) {
                const bool prevented = bindings_->dispatch("click", pressed_);
                changed = true;
                // Default actions run AFTER the listeners and only if none of
                // them cancelled - which is what preventDefault is for.
                if (!prevented) { activate(pressed_); }
            }
            pressed_ = node_id{};
            return changed;
        }
        case input_kind::key_down: return handle_key(event);
        case input_kind::key_up: return dispatch_key("keyup", event);
        case input_kind::text_input: return text_input(event.key);
        case input_kind::resize:
            resize(static_cast<int>(event.x), static_cast<int>(event.y));
            return true;
        }
        return false;
    }

    // What is under a viewport point. Scroll is applied here rather than in the
    // fragment tree, because the fragments are in CONTENT space - the same
    // reason a scroll does not invalidate them.
    [[nodiscard]] node_id hit_test(float x, float y) const {
        return deepest_at(fragments_, x, y + scroll_y_, 0, 0);
    }

    // --- frames ----------------------------------------------------------

    // Run whatever this frame needs and composite. Cheap when nothing is dirty,
    // which is the common case and the point.
    std::expected<void, ctbrowser::raster::gpu_error> frame(scheduler * pool = nullptr) {
        // A value the page assigned OUTSIDE an event handler - at the top of the
        // script, say - reaches the control here. Dispatch covers the rest.
        if (bindings_ && sync_controls()) { mark(dirty::paint); }
        // Anything drawn into a canvas since the last frame makes its tiles
        // stale. Asking here rather than being told keeps the bindings from
        // having to know what a tile is.
        if (canvases_.total_revision() != canvas_revision_) {
            dirty_ = worse(dirty_, dirty::raster);
            canvas_revision_ = canvases_.total_revision();
        }
        if (dirty_ >= dirty::raster) { renderer_.discard(); }
        renderer_.set_clear_color(options_.background);
        if (dirty_ >= dirty::styles) { resolve_styles(); }
        if (dirty_ >= dirty::layout) { run_layout(); }
        if (dirty_ >= dirty::paint) { record(); }
        dirty_ = dirty::nothing;
        ++frames_;
        return ctbrowser::raster::draw(renderer_, layers_, pool, options_.tile_extent, viewport());
    }

    // Push anything a script wrote into `value`/`checked` through to the
    // controls, and report whether that changed one.
    [[nodiscard]] bool sync_controls() { return bindings_->refresh_wrappers(); }

    [[nodiscard]] rect viewport() const noexcept {
        return rect{0, 0, static_cast<float>(options_.width), static_cast<float>(options_.height)};
    }
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

    // <style> elements in the document. A real browser also fetches <link
    // rel=stylesheet>; there is no network here yet, and pretending otherwise
    // would mean silently rendering pages wrong.
    void load_inline_styles() {
        if (author_sheet_loaded_) { return; }
        const auto txn = doc_->read();
        const atom style_tag = atoms_.intern_lower("style");
        std::string css;
        const auto walk = [&](auto && self, node_id at) -> void {
            if (txn.tag(at).value_or(atom{}) == style_tag) {
                for (const node_id child : txn.children(at)) { css += txn.text(child); }
                css += '\n';
            }
            for (const node_id child : txn.children(at)) { self(self, child); }
        };
        walk(walk, txn.root());
        if (!css.empty()) { styles_->add_sheet(css, ctbrowser::style::author_origin); }
        author_sheet_loaded_ = true;
    }

    [[nodiscard]] std::string extract_title() {
        const auto txn = doc_->read();
        const atom title_tag = atoms_.intern_lower("title");
        std::string found;
        const auto walk = [&](auto && self, node_id at) -> void {
            if (!found.empty()) { return; }
            if (txn.tag(at).value_or(atom{}) == title_tag) {
                for (const node_id child : txn.children(at)) { found += txn.text(child); }
            }
            for (const node_id child : txn.children(at)) { self(self, child); }
        };
        walk(walk, txn.root());
        return found;
    }

    void resolve_styles() {
        const auto txn = doc_->read();
        resolved_ = styles_->resolve_all(txn);
    }

    // Run every <script> in the document, in order. Errors are recorded rather
    // than thrown: a page whose script fails still has to render, which is what
    // every browser does and what makes a broken script a broken feature rather
    // than a blank window.
    void run_scripts() {
        // Order matters on teardown too: the old context must go before the old
        // program it was executing.
        script_.reset();
        script_program_.reset();
        script_ = std::make_unique<script::context>();
        // The standard library goes in FIRST, so a page's own globals can
        // shadow it rather than the other way round.
        script::install_builtins(*script_);
        canvases_.clear();
        forms_.clear();
        focused_ = node_id{};
        bindings_ = std::make_unique<dom_bindings>(
            *doc_, atoms_, canvases_, forms_, [this] { mark(dirty::paint); },
            [this](node_id id) { (void)focus(id); });
        bindings_->observe_viewport(options_.width, options_.height);
        bindings_->observe_resources(assets_, images_);
        bindings_->allow_network(network_allowed_);
        bindings_->set_alert_hook([this](const std::string & message) {
            alerts_.push_back(message);
            if (alert_hook_) { alert_hook_(message); }
        });
        bindings_->observe_location(location_href_, location_hash_);
        bindings_->install(*script_);
        install_embedder_natives();
        script_error_.clear();

        std::string source;
        {
            const auto txn = doc_->read();
            const atom script_tag = atoms_.intern_lower("script");
            const auto walk = [&](auto && self, node_id at) -> void {
                if (txn.tag(at).value_or(atom{}) == script_tag) {
                    for (const node_id child : txn.children(at)) { source += txn.text(child); }
                    source += '\n';
                }
                for (const node_id child : txn.children(at)) { self(self, child); }
            };
            walk(walk, txn.root());
        }
        if (source.empty()) { return; }

        script_program_ = std::make_unique<script::program>(script::compiler::compile(source));
        const script::run_result result = script_->run(*script_program_);
        if (!result.ok) { script_error_ = result.error; }
    }

    // Every <img src> in the document, decoded once. A missing or undecodable
    // image is remembered as a null so the element lays out at zero size rather
    // than being retried every frame.
    // The measure layout uses, and the fonts the rasterizer draws with, are the
    // SAME object - text lands where layout thought it would only if one thing
    // answers both questions.
    [[nodiscard]] const ctbrowser::raster::font_backend & fonts() const {
        return fonts_ != nullptr ? *fonts_ : ctbrowser::raster::font8x8_fonts();
    }
    [[nodiscard]] ctbrowser::layout::measure_text_fn measure() const {
        return metrics_for(fonts());
    }

    // The page's own @font-face rules, loaded through the asset registry like
    // any other resource. Called when real fonts are turned on and again on
    // every navigation, because the rules belong to the document.
    void load_page_fonts() {
#if CTBROWSER_WITH_TTF
        if (!ttf_) { return; }
        for (const auto & face : styles_->page_fonts()) {
            const std::vector<std::byte> bytes = assets_.load(face.source);
            if (!bytes.empty()) {
                (void)ttf_->add_face(face.family, face.bold, face.italic, bytes);
            }
        }
#endif
    }

    void load_images() {
        images_by_node_.clear();
        const auto txn = doc_->read();
        const atom img_tag = atoms_.intern_lower("img");
        const atom src_attribute = atoms_.intern("src");
        const auto walk = [&](auto && self, node_id at) -> void {
            if (txn.tag(at).value_or(atom{}) == img_tag) {
                const std::string_view src = txn.attribute_value(at, src_attribute);
                if (!src.empty()) {
                    if (auto pixels = images_.load(assets_, src)) {
                        images_by_node_.emplace_back(at, std::move(pixels));
                    }
                }
            }
            for (const node_id child : txn.children(at)) { self(self, child); }
        };
        walk(walk, txn.root());
    }

    [[nodiscard]] std::shared_ptr<const ctbrowser::paint::bitmap> image_of(node_id id) const {
        for (const auto & [at, pixels] : images_by_node_) {
            if (at == id) { return pixels; }
        }
        return nullptr;
    }

    void install_embedder_natives() {
        for (const auto & [name, fn] : embedder_natives_) { script_->define_native(name, fn); }
    }

    void run_layout() {
        ++layouts_;
        const auto txn = doc_->read();
        ctbrowser::layout::box_builder builder{atoms_, resolved_, measure()};
        // An <img> with no width/height attribute is as big as its bitmap. Only
        // the browser knows that - layout cannot decode images and should not
        // learn how.
        builder.intrinsic_image = [this](node_id id) {
            const auto pixels = image_of(id);
            return pixels ? ctbrowser::layout::box_builder::intrinsic_size{static_cast<float>(
                                                                               pixels->width),
                                                                           static_cast<float>(
                                                                               pixels->height)}
                          : ctbrowser::layout::box_builder::intrinsic_size{};
        };
        boxes_ = builder.build(txn, txn.root());
        const ctbrowser::layout::engine eng{measure()};
        fragments_ = eng.run(boxes_, static_cast<float>(options_.width));
        content_height_ = fragments_.bounds.height;

        // TWO PASSES when the page overflows: the scrollbar takes width away
        // from the content, and content laid out at the full width would run
        // under it. This terminates because narrowing a page can only make it
        // TALLER, so a page that overflowed still overflows - it never
        // oscillates between needing a bar and not.
        if (options_.scrollbar_width > 0 && content_height_ > static_cast<float>(options_.height)) {
            fragments_ =
                eng.run(boxes_, static_cast<float>(options_.width) - options_.scrollbar_width);
            content_height_ = fragments_.bounds.height;
        }
        scroll_y_ = std::clamp(scroll_y_, 0.0f, max_scroll());
        // offsetWidth and friends read the fragment tree, so they answer with
        // THIS layout rather than the one before it.
        if (bindings_) {
            bindings_->observe_layout(&fragments_);
            bindings_->observe_viewport(options_.width, options_.height);
        }
    }

    void record() {
        // The selection's highlight. Computed here rather than stored on the
        // fragment, and looked up by the fragment the recorder is drawing.
        recorder_.selection_of = [this](const ctbrowser::layout::fragment & f) {
            return highlight_for(f);
        };
        recorder_.paint_replaced =
            [this](node_id id, const rect & box, const ctbrowser::style::computed_style_ptr & style,
                   ctbrowser::paint::display_list & into) { paint_replaced(id, box, style, into); };
        layers_ = recorder_.record_layers(fragments_);
        layers_.scroll_to(0, scroll_y_);
        page_layers_ = layers_.layers.size(); // everything after this is chrome
        record_chrome();
    }

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
    void refresh_chrome() {
        layers_.layers.resize(std::min(page_layers_, layers_.layers.size()));
        record_chrome();
    }

    // Everything the BROWSER draws rather than the page: the scrollbar, and an
    // open <select>'s option list. Each is its own non-scrolling layer for the
    // same reason - chrome does not move when the page does, and the compositor
    // already knows how to hold a layer still.
    void record_chrome() {
        record_scrollbar();
        record_select_popup();
        record_context_menu();
    }

    // The context menu. Its entries are the clipboard verbs, because those are
    // the ones the browser can carry out on its own.
    static constexpr std::string_view menu_items[] = {"Copy", "Cut", "Paste", "Select All"};
    static constexpr float menu_row = 20;
    static constexpr float menu_width = 120;

    void record_context_menu() {
        if (!menu_open_) { return; }
        const rect box = menu_box();
        ctbrowser::paint::display_list list;
        list.fill(box, color{ctbrowser::style::ua_widget_field});
        const color frame{ctbrowser::style::ua_widget_frame};
        list.fill(rect{box.x, box.y, box.width, 1}, frame);
        list.fill(rect{box.x, box.bottom() - 1, box.width, 1}, frame);
        list.fill(rect{box.x, box.y, 1, box.height}, frame);
        list.fill(rect{box.right() - 1, box.y, 1, box.height}, frame);
        for (std::size_t i = 0; i < std::size(menu_items); ++i) {
            list.text(rect{box.x + 6, box.y + menu_row * static_cast<float>(i) + 4, box.width - 12,
                           menu_row - 6},
                      std::string{menu_items[i]}, 13, color{0xFF000000U});
        }
        ctbrowser::paint::layer overlay;
        overlay.contents = std::make_shared<const ctbrowser::paint::display_list>(std::move(list));
        overlay.scrolls = false;
        layers_.layers.push_back(std::move(overlay));
    }

    [[nodiscard]] rect menu_box() const {
        const float height = menu_row * static_cast<float>(std::size(menu_items));
        // Flipped when it would run off the edge, which is what a menu opened
        // near the corner has to do.
        const float x = menu_at_.x + menu_width <= static_cast<float>(options_.width)
                            ? menu_at_.x
                            : std::max(0.0f, menu_at_.x - menu_width);
        const float y = menu_at_.y + height <= static_cast<float>(options_.height)
                            ? menu_at_.y
                            : std::max(0.0f, menu_at_.y - height);
        return rect{x, y, menu_width, height};
    }

    // Returns whether the menu consumed the press. A click anywhere closes it,
    // and a click ON it runs the verb - neither reaches the page.
    bool handle_menu_press(const input_event & event) {
        const rect box = menu_box();
        menu_open_ = false;
        mark(dirty::paint);
        if (event.x < box.x || event.x >= box.right() || event.y < box.y ||
            event.y >= box.bottom()) {
            return true; // click-away: closed, and the page does not see it
        }
        const auto index = static_cast<std::size_t>((event.y - box.y) / menu_row);
        if (index < std::size(menu_items)) { run_clipboard_verb(menu_items[index]); }
        return true;
    }

    // The option list of an open <select>. THE reason a select was unusable:
    // the box drew, the popup did not exist, so there was no way to choose
    // anything with a pointer.
    void record_select_popup() {
        if (!select_open_) { return; }
        const auto txn = doc_->read();
        const std::vector<std::string> options = option_labels(txn, select_open_);
        if (options.empty()) { return; }

        const rect anchor = viewport_box_of(select_open_);
        if (anchor.empty()) { return; }
        const float row = anchor.height;
        const float height = row * static_cast<float>(options.size());
        // Opens DOWNWARD unless there is no room, which is what a select at the
        // bottom of a window has to do.
        const float top = anchor.bottom() + height <= static_cast<float>(options_.height)
                              ? anchor.bottom()
                              : std::max(0.0f, anchor.y - height);

        ctbrowser::paint::display_list list;
        const rect box{anchor.x, top, std::max(anchor.width, 60.0f), height};
        list.fill(box, color{ctbrowser::style::ua_widget_field});
        const std::string chosen = selected_option(txn, select_open_);
        for (std::size_t i = 0; i < options.size(); ++i) {
            const rect item{box.x, top + row * static_cast<float>(i), box.width, row};
            if (options[i] == chosen) {
                list.fill(item, color{ctbrowser::style::ua_widget_accent});
            }
            list.text(rect{item.x + 4, item.y + 3, item.width - 8, item.height - 6}, options[i],
                      font_size_of(select_open_),
                      options[i] == chosen ? color{ctbrowser::style::ua_widget_mark}
                                           : color{0xFF000000U},
                      select_open_, paint_face_of(select_open_));
        }
        // A frame last, so it is not painted over by the rows.
        const color frame{ctbrowser::style::ua_widget_frame};
        list.fill(rect{box.x, box.y, box.width, 1}, frame);
        list.fill(rect{box.x, box.bottom() - 1, box.width, 1}, frame);
        list.fill(rect{box.x, box.y, 1, box.height}, frame);
        list.fill(rect{box.right() - 1, box.y, 1, box.height}, frame);

        ctbrowser::paint::layer overlay;
        overlay.contents = std::make_shared<const ctbrowser::paint::display_list>(std::move(list));
        overlay.scrolls = false;
        layers_.layers.push_back(std::move(overlay));
    }

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

    [[nodiscard]] std::vector<std::string> option_labels(const read_txn & txn, node_id select) {
        std::vector<std::string> out;
        const atom option_tag = atoms_.intern_lower("option");
        for (const node_id child : txn.children(select)) {
            if (txn.tag(child).value_or(atom{}) != option_tag) { continue; }
            std::string text;
            for (const node_id grand : txn.children(child)) { text += txn.text(grand); }
            out.push_back(std::move(text));
        }
        return out;
    }

    // Where an element is ON SCREEN - the fragment tree is in content space, so
    // the scroll has to come off.
    [[nodiscard]] rect viewport_box_of(node_id id) const {
        const auto walk = [&](auto && self, const ctbrowser::layout::fragment & f, float dx,
                              float dy) -> rect {
            const rect box{f.bounds.x + dx, f.bounds.y + dy, f.bounds.width, f.bounds.height};
            if (f.source == id && !box.empty()) { return box; }
            for (const auto & child : f.children) {
                if (const rect hit = self(self, child, box.x, box.y); !hit.empty()) { return hit; }
            }
            return rect{};
        };
        rect box = walk(walk, fragments_, 0, 0);
        if (!box.empty()) { box.y -= scroll_y_; }
        return box;
    }

    void record_scrollbar() {
        if (max_scroll() <= 0 || options_.scrollbar_width <= 0) { return; }
        const float width = options_.scrollbar_width;
        const float height = static_cast<float>(options_.height);
        const float left = static_cast<float>(options_.width) - width;

        ctbrowser::paint::display_list list;
        list.fill(rect{left, 0, width, height}, color{ctbrowser::style::ua_scrollbar_track});
        const rect thumb = scrollbar_thumb();
        list.fill(thumb, color{sb_dragging_ ? ctbrowser::style::ua_scrollbar_thumb_active
                                            : ctbrowser::style::ua_scrollbar_thumb});

        ctbrowser::paint::layer overlay;
        overlay.contents = std::make_shared<const ctbrowser::paint::display_list>(std::move(list));
        overlay.scrolls = false; // chrome, not content
        layers_.layers.push_back(std::move(overlay));
    }

    // Where the thumb sits. Proportional to how much of the document is
    // visible, with a floor so a very long page still has something to grab.
    [[nodiscard]] rect scrollbar_thumb() const {
        const float width = options_.scrollbar_width;
        const float height = static_cast<float>(options_.height);
        const float left = static_cast<float>(options_.width) - width;
        const float visible = content_height_ > 0 ? height / content_height_ : 1;
        const float thumb_height = std::max(24.0f, height * std::min(1.0f, visible));
        const float travel = height - thumb_height;
        const float progress = max_scroll() > 0 ? scroll_y_ / max_scroll() : 0;
        return rect{left + 1, progress * travel, width - 2, thumb_height};
    }

    // What a <canvas> or a form control draws. Everything here is chrome the
    // UA supplies rather than anything the document asked for, which is why the
    // palette comes from :ua and not from the cascade.
    // The inset a control's text sits at. Firefox uses 1px 2px on a text input
    // and 1px 6px on a button; this is one number because the vertical inset is
    // handled by centring instead.
    static constexpr float control_padding = 6;

    void paint_replaced(node_id id, const rect & box,
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
            if (auto pixels = image_of(id)) { into.draw_image(box, std::move(pixels), id); }
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
            if (control.checked) {
                const float inset = box.width * 0.25f;
                into.fill(rect{box.x + inset, box.y + inset, box.width - 2 * inset,
                               box.height - 2 * inset},
                          color{ctbrowser::style::ua_widget_mark}, id);
            }
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
            outline(box, frame, into, id);
            const std::string label = button_label(txn, id, control, type);
            if (!label.empty()) { label_text(box, label, id, style, into); }
            break;
        }
        case control_kind::select: {
            outline(box, frame, into, id);
            // The SELECTED OPTION'S TEXT. This drew an empty rectangle before -
            // it passed an empty string as the label and never read <option> at
            // all, so a select looked like a bug rather than like a control.
            const std::string label = selected_option(txn, id);
            if (!label.empty()) {
                const float size = font_size_of(id);
                into.text(rect{box.x + control_padding, box.y + baseline_inset(box, size),
                               box.width - control_padding - 20, size * 1.25f},
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
            into.fill(box, field, id);
            outline(box, focused ? accent : frame, into, id);
            paint_field_text(box, id, control, kind, style, focused, into);
            break;
        }
        case control_kind::none: break;
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
        const float x = box.x + std::max(control_padding, (box.width - width) / 2);
        into.text(rect{x, box.y + baseline_inset(box, size), box.width - (x - box.x), size * 1.25f},
                  label, size, control_text_colour(id, style), id, paint_face_of(id));
    }

    // Where a single line of text sits inside a control: vertically centred on
    // the box rather than pinned to a constant, so a control that is taller
    // than its text (every one with a border and padding) still centres it.
    [[nodiscard]] static float baseline_inset(const rect & box, float size) {
        return std::max(0.0f, (box.height - size * 1.25f) / 2);
    }

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
    };

    [[nodiscard]] field_layout layout_of_field(const rect & box, node_id id,
                                               const control_state & control, control_kind kind) {
        field_layout out;
        out.size = font_size_of(id);
        out.metrics_face = face_of(id);
        out.line_height = out.size * 1.25f;
        out.masked = is_password(id);
        const bool multiline = kind == control_kind::textarea;
        out.inner =
            rect{box.x + control_padding, box.y + (multiline ? 3 : baseline_inset(box, out.size)),
                 box.width - 2 * control_padding, box.height - 6};
        out.lines =
            multiline ? value_lines(control.value)
                      : std::vector<std::pair<std::size_t, std::size_t>>{{0, control.value.size()}};
        return out;
    }

    // `<input type=password>` shows BULLETS. Masked per CODE POINT rather than
    // per byte, so a value with anything non-ASCII in it does not come out with
    // three bullets for one character - and the caret, which counts the same
    // way, still lands between them.
    // A control is disabled by its own attribute or by an enclosing <fieldset>,
    // which is how a form greys out a whole section at once.
    [[nodiscard]] bool is_disabled(node_id id) {
        if (!id) { return false; }
        const auto txn = doc_->read();
        const atom disabled = atoms_.intern("disabled");
        const atom fieldset = atoms_.intern_lower("fieldset");
        for (node_id at = id; at; at = txn.parent(at)) {
            if (at == id || txn.tag(at).value_or(atom{}) == fieldset) {
                if (txn.has_attribute(at, disabled)) { return true; }
            }
        }
        return false;
    }

    [[nodiscard]] bool is_password(node_id id) {
        const auto txn = doc_->read();
        return txn.attribute_value(id, atoms_.intern("type")) == "password";
    }
    [[nodiscard]] static std::string masked_text(std::string_view text) {
        std::string out;
        for (std::size_t at = 0; at < text.size(); at = next_code_point(text, at)) {
            out += "\xE2\x80\xA2"; // U+2022 BULLET
        }
        return out;
    }
    // What the user SEES for a stretch of the value.
    [[nodiscard]] static std::string shown(std::string_view text, bool masked) {
        return masked ? masked_text(text) : std::string{text};
    }
    [[nodiscard]] static std::size_t next_code_point(std::string_view text, std::size_t at) {
        std::size_t next = at + 1;
        while (next < text.size() && (static_cast<unsigned char>(text[next]) & 0xC0) == 0x80) {
            ++next;
        }
        return next;
    }

    // Where in a control's value a point falls. The nearest character boundary
    // on the line the point is on - which is the ONLY way a click can put the
    // caret where the user pointed, and a textarea had no such path at all.
    [[nodiscard]] std::size_t offset_at_point(node_id id, const control_state & control,
                                              control_kind kind, float x, float y) {
        const rect box = viewport_box_of(id);
        if (box.empty()) { return control.caret; }
        const field_layout geometry = layout_of_field(box, id, control, kind);
        if (geometry.lines.empty()) { return 0; }

        const auto line_index =
            static_cast<std::ptrdiff_t>(std::floor((y - geometry.inner.y) / geometry.line_height));
        const std::size_t index = static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(
            line_index, 0, static_cast<std::ptrdiff_t>(geometry.lines.size()) - 1));
        const auto [begin, end] = geometry.lines[index];
        const std::string_view line{control.value.data() + begin, end - begin};

        // Nearest BOUNDARY, not nearest character: clicking the right half of a
        // glyph puts the caret after it, which is what makes clicking at the
        // end of a word land where you meant.
        const float want = x - geometry.inner.x;
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
        std::size_t index = 0;
        for (std::size_t i = 0; i < geometry.lines.size(); ++i) {
            if (control.caret >= geometry.lines[i].first &&
                control.caret <= geometry.lines[i].second) {
                index = i;
                break;
            }
        }
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

        // The field clips its own contents: a value longer than the box must not
        // paint over the page beside it.
        into.push_clip(box);
        const std::size_t from = std::min(control.caret, control.selection);
        const std::size_t to = std::max(control.caret, control.selection);
        for (std::size_t index = 0; index < geometry.lines.size(); ++index) {
            const auto [begin, end] = geometry.lines[index];
            const std::string_view line{control.value.data() + begin, end - begin};
            const float y = inner.y + static_cast<float>(index) * line_height;
            if (from != to && from < end && to > begin) {
                const std::size_t a = std::max(from, begin) - begin;
                const std::size_t b = std::min(to, end) - begin;
                into.fill(rect{inner.x + advance(line.substr(0, a)), y,
                               advance(line.substr(a, b - a)), line_height},
                          color{ctbrowser::style::ua_selection_highlight}, id);
            }
            if (!line.empty()) {
                into.text(rect{inner.x, y, inner.width, line_height}, shown(line, geometry.masked),
                          size, control_text_colour(id, style), id, face);
            }
            // The caret sits on the line CONTAINING it. `<=` on the end so a
            // caret at the very end of a line is on that line rather than
            // nowhere; the first match wins, which puts a caret sitting on a
            // boundary at the end of the earlier line, as browsers do.
            if (focused && caret_visible() && control.caret >= begin && control.caret <= end) {
                into.fill(rect{inner.x + advance(line.substr(0, control.caret - begin)), y, 1,
                               line_height},
                          control_text_colour(id, style), id);
            }
        }
        into.pop_clip();
    }

    // A value's lines as [begin, end) offsets, newlines excluded. Always at
    // least one, so an empty value still has a line for the caret to be on.
    [[nodiscard]] static std::vector<std::pair<std::size_t, std::size_t>> value_lines(
        const std::string & value) {
        std::vector<std::pair<std::size_t, std::size_t>> out;
        std::size_t begin = 0;
        for (std::size_t at = 0; at <= value.size(); ++at) {
            if (at == value.size() || value[at] == '\n') {
                out.emplace_back(begin, at);
                begin = at + 1;
            }
        }
        return out;
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

    [[nodiscard]] color text_colour(const ctbrowser::style::computed_style_ptr & style) {
        if (style) {
            if (const auto c = ctbrowser::paint::parse_color(style->get(atoms_.intern("color")))) {
                return *c;
            }
        }
        return color::rgba(0, 0, 0);
    }

    // The face a control's text is drawn in - the same one layout measured it
    // with. Measuring a caret position with a different font from the one that
    // drew the text is how the caret ends up a character or two past the end of
    // what you typed.
    [[nodiscard]] ctbrowser::layout::text_face face_of(node_id id) const {
        const layout::box_node * found = find_box(boxes_, id);
        return found == nullptr ? ctbrowser::layout::text_face{} : found->face;
    }

    // The same face, as the paint layer names it. The two types are separate on
    // purpose - layout may not import paint - so the conversion is explicit,
    // and every control's text MUST go through it: drawing a control's text
    // with the default face while measuring the caret with the element's own
    // is a caret that drifts further right with every character typed. A
    // textarea is monospace by UA rule and was drawn in the default serif.
    [[nodiscard]] ctbrowser::paint::font_face paint_face_of(node_id id) const {
        const ctbrowser::layout::text_face face = face_of(id);
        return ctbrowser::paint::font_face{face.family, face.bold, face.italic};
    }

    [[nodiscard]] float font_size_of(node_id id) const {
        const layout::box_node * found = find_box(boxes_, id);
        return found == nullptr ? 16.0f : found->font_size;
    }
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

    [[nodiscard]] static node_id deepest_at(const fragment & f, float x, float y, float dx,
                                            float dy) {
        const rect box{f.bounds.x + dx, f.bounds.y + dy, f.bounds.width, f.bounds.height};
        if (!box.contains(point{x, y})) { return node_id{}; }
        // Later children paint on top, so the last one that contains the point
        // is the one a click lands on.
        for (auto it = f.children.rbegin(); it != f.children.rend(); ++it) {
            if (const node_id hit = deepest_at(*it, x, y, box.x, box.y)) { return hit; }
        }
        return f.source;
    }

    bool set_hover(node_id at) {
        if (at == hovered_) { return false; }
        bool changed = set_state(hovered_, state_hover, false);
        hovered_ = at;
        changed = set_state(hovered_, state_hover, true) || changed;
        return changed;
    }

    // Pseudo-state applies to the whole ancestor chain: hovering a <span>
    // inside an <a> must make the <a> hover too, or `a:hover` never fires on a
    // link with any markup inside it.
    bool set_state(node_id at, std::uint32_t bit, bool on) {
        if (!at) { return false; }
        const auto txn = doc_->read();
        bool changed = false;
        for (node_id n = at; n; n = txn.parent(n)) {
            if (styles_->set_state(n, bit, on)) { changed = true; }
        }
        // Conservative, and knowingly so: any state change re-resolves the whole
        // cascade and re-lays-out. Most hovers only change a colour, and a real
        // engine tracks which declarations can affect geometry so it can stop at
        // `paint`. That needs per-property invalidation the style engine does not
        // have yet - and being slow is a much smaller problem than being wrong,
        // since `a:hover { font-size: 20px }` genuinely does change layout.
        if (changed) { mark(dirty::styles); }
        return changed;
    }

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
    [[nodiscard]] std::string selected_option(const read_txn & txn, node_id id) {
        const std::string value = forms_.state_of(txn, atoms_, id).value;
        const atom option_tag = atoms_.intern_lower("option");
        std::string first;
        bool have_first = false;
        for (const node_id child : txn.children(id)) {
            if (txn.tag(child).value_or(atom{}) != option_tag) { continue; }
            std::string text;
            for (const node_id grand : txn.children(child)) { text += txn.text(grand); }
            if (!have_first) {
                first = text;
                have_first = true;
            }
            if (form_store::option_value(txn, atoms_, child) == value) { return text; }
        }
        return first;
    }

    // A press while a <select> is open. Returns whether the popup consumed it -
    // a click ANYWHERE else closes it, which is what click-away means, and that
    // click must not also reach the page.
    bool handle_popup_press(const input_event & event) {
        const auto txn = doc_->read();
        const std::vector<std::string> options = option_labels(txn, select_open_);
        const rect anchor = viewport_box_of(select_open_);
        if (options.empty() || anchor.empty()) {
            select_open_ = node_id{};
            mark(dirty::paint);
            return true;
        }
        const float row = anchor.height;
        const float height = row * static_cast<float>(options.size());
        const float top = anchor.bottom() + height <= static_cast<float>(options_.height)
                              ? anchor.bottom()
                              : std::max(0.0f, anchor.y - height);
        const rect box{anchor.x, top, std::max(anchor.width, 60.0f), height};

        const node_id select = select_open_;
        select_open_ = node_id{};
        mark(dirty::paint);
        if (event.x >= box.x && event.x < box.right() && event.y >= box.y &&
            event.y < box.bottom()) {
            const auto index = static_cast<std::size_t>((event.y - box.y) / row);
            if (index < options.size()) {
                // The chosen option becomes the control's value, and `change`
                // fires - which is what a page listens for.
                // The option's VALUE, not its label - that is what a form sends
                // and what `select.value` reads.
                forms_.state_of(txn, atoms_, select).value = option_value_at(txn, select, index);
                bindings_->dispatch("change", select);
            }
        }
        return true;
    }

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
    [[nodiscard]] std::vector<text_run> text_runs() const {
        std::vector<text_run> out;
        flat_map<std::uint64_t, std::size_t> consumed; // per source node
        const auto walk = [&](auto && self, const ctbrowser::layout::fragment & f, float dx,
                              float dy) -> void {
            const rect box{f.bounds.x + dx, f.bounds.y + dy, f.bounds.width, f.bounds.height};
            if (!f.text.empty() && f.source) {
                // WHERE this line begins in the node's text. Found by searching
                // rather than by accumulating lengths: the wrap DROPS the space
                // it broke at, so the fragments do not partition the text and
                // summing their lengths drifts by one character per line - which
                // made every position past the first line point at the wrong
                // character.
                std::size_t & at = consumed[f.source.key()];
                const std::string_view full =
                    f.box != nullptr ? std::string_view{f.box->text} : std::string_view{f.text};
                const std::size_t found = full.find(f.text, at);
                const std::size_t offset = found == std::string_view::npos ? at : found;
                out.push_back(text_run{&f, f.source, offset, f.text, box, out.size()});
                at = offset + f.text.size();
            }
            for (const auto & child : f.children) { self(self, child, box.x, box.y); }
        };
        walk(walk, fragments_, 0, 0);
        return out;
    }

    // Where in the node's text a point falls: the nearest character boundary on
    // the nearest line. Above the first line is its start and below the last is
    // its end, so dragging past the edge takes whole lines - which is what a
    // drag off the top of a paragraph has to do.
    [[nodiscard]] text_position position_at(float x, float y) {
        const std::vector<text_run> runs = text_runs();
        if (runs.empty()) { return {}; }
        const float content_y = y + scroll_y_;
        const text_run * best = nullptr;
        float best_distance = 0;
        for (const text_run & run : runs) {
            const float distance = content_y < run.box.y          ? run.box.y - content_y
                                   : content_y > run.box.bottom() ? content_y - run.box.bottom()
                                                                  : 0.0f;
            if (best == nullptr || distance < best_distance) {
                best = &run;
                best_distance = distance;
            }
        }
        if (best == nullptr) { return {}; }
        return text_position{best->source, best->offset + code_point_at(*best, x)};
    }

    // The character boundary nearest an x, by MEASURING prefixes with the same
    // function layout measured the line with - so the boundary is where the
    // glyph actually is, not where a guess put it.
    [[nodiscard]] std::size_t code_point_at(const text_run & run, float x) const {
        if (x <= run.box.x) { return 0; }
        if (x >= run.box.right()) { return run.text.size(); }
        const auto metrics = measure();
        const ctbrowser::layout::text_face face =
            run.fragment->box != nullptr ? run.fragment->box->face : ctbrowser::layout::text_face{};
        const float size = run.fragment->box != nullptr ? run.fragment->box->font_size : 16.0f;
        std::size_t best = 0;
        float best_distance = std::abs(x - run.box.x);
        for (std::size_t i = 1; i <= run.text.size(); ++i) {
            // UTF-8: a boundary is not inside a continuation byte.
            if (i < run.text.size() && (static_cast<unsigned char>(run.text[i]) & 0xC0u) == 0x80u) {
                continue;
            }
            const float edge = run.box.x + metrics(run.text.substr(0, i), size, face);
            const float distance = std::abs(x - edge);
            if (distance < best_distance) {
                best_distance = distance;
                best = i;
            }
        }
        return best;
    }

    // The part of a run that is selected, as absolute code points in its node.
    [[nodiscard]] std::pair<std::size_t, std::size_t> selected_range(const text_run & run) {
        if (!has_selection()) { return {0, 0}; }
        const std::vector<text_run> runs = text_runs();
        // Which end comes first in DOCUMENT ORDER - a drag upward selects the
        // same text as the same drag downward.
        const auto locate = [&runs](const text_position & p) -> std::size_t {
            std::size_t best = runs.size();
            for (const text_run & r : runs) {
                if (r.source != p.node) { continue; }
                if (p.code_point >= r.offset && p.code_point <= r.offset + r.text.size()) {
                    return r.order;
                }
                best = std::min(best, r.order);
            }
            return best;
        };
        const std::size_t a_order = locate(selection_anchor_);
        const std::size_t b_order = locate(selection_focus_);
        text_position first = selection_anchor_;
        text_position last = selection_focus_;
        if (b_order < a_order ||
            (b_order == a_order && selection_focus_.code_point < selection_anchor_.code_point)) {
            std::swap(first, last);
        }
        const std::size_t first_order = std::min(a_order, b_order);
        const std::size_t last_order = std::max(a_order, b_order);
        if (run.order < first_order || run.order > last_order) { return {0, 0}; }

        const std::size_t run_start = run.offset;
        const std::size_t run_end = run.offset + run.text.size();
        const std::size_t from =
            run.order == first_order ? std::max(run_start, first.code_point) : run_start;
        const std::size_t to =
            run.order == last_order ? std::min(run_end, last.code_point) : run_end;
        return {std::min(from, to), to};
    }

    // The highlighted part of one text fragment, in the fragment's own space.
    [[nodiscard]] rect highlight_for(const ctbrowser::layout::fragment & f) {
        if (!has_selection() || f.text.empty() || !f.source) { return rect{}; }
        for (const text_run & run : text_runs()) {
            if (run.fragment != &f) { continue; }
            const auto [from, to] = selected_range(run);
            if (from >= to) { return rect{}; }
            const auto metrics = measure();
            const ctbrowser::layout::text_face face =
                f.box != nullptr ? f.box->face : ctbrowser::layout::text_face{};
            const float size = f.box != nullptr ? f.box->font_size : 16.0f;
            const float left = metrics(run.text.substr(0, from - run.offset), size, face);
            const float right = metrics(run.text.substr(0, to - run.offset), size, face);
            return rect{left, 0, right - left, f.bounds.height};
        }
        return rect{};
    }

    void run_clipboard_verb(std::string_view verb) {
        control_state * control = editable_focus();
        if (verb == "Select All") {
            if (control != nullptr) {
                forms_.select_all(*control);
                mark(dirty::paint);
                return;
            }
            // Nothing editable focused: select the whole PAGE.
            const std::vector<text_run> runs = text_runs();
            if (!runs.empty()) {
                selection_anchor_ = text_position{runs.front().source, runs.front().offset};
                selection_focus_ =
                    text_position{runs.back().source, runs.back().offset + runs.back().text.size()};
                mark(dirty::paint);
            }
            return;
        }
        const std::string type = verb == "Copy" ? "copy" : verb == "Cut" ? "cut" : "paste";
        if (focused_ && bindings_->dispatch(type, focused_)) { return; } // cancelled
        if (control == nullptr) {
            // No editable focused: Copy takes the PAGE selection. Cut and paste
            // have nowhere to act, and a page is not editable.
            if (verb == "Copy" && has_selection()) {
                clipboard_ = selected_text();
                if (clipboard_write_) { clipboard_write_(clipboard_); }
            }
            return;
        }
        if (verb == "Paste") {
            const std::string text = clipboard_read_ ? clipboard_read_() : clipboard_;
            if (!text.empty()) {
                forms_.insert_text(*control, text);
                (void)edited(true);
            }
            return;
        }
        const std::string selected = form_store::selected_text(*control);
        if (selected.empty()) { return; }
        clipboard_ = selected;
        if (clipboard_write_) { clipboard_write_(selected); }
        if (verb == "Cut") {
            (void)form_store::delete_selection(*control);
            (void)edited(true);
        }
    }

    bool handle_key(const input_event & event) {
        // Escape closes an open popup before the page sees the key, which is
        // what every select does.
        if (select_open_ && event.key == "Escape") {
            select_open_ = node_id{};
            mark(dirty::paint);
            return true;
        }
        if (dispatch_key("keydown", event)) { return true; }

        // The CLIPBOARD SHORTCUTS come before the editing keys, and before the
        // editable check: Ctrl+C is not a C, and copying the PAGE selection has
        // to work when nothing is focused at all - which is the usual case for
        // someone reading a page.
        if (event.ctrl && (event.key == "KeyC" || event.key == "KeyX" || event.key == "KeyV" ||
                           event.key == "KeyA")) {
            run_clipboard_verb(event.key == "KeyC"   ? "Copy"
                               : event.key == "KeyX" ? "Cut"
                               : event.key == "KeyV" ? "Paste"
                                                     : "Select All");
            return true;
        }

        if (control_state * control = editable_focus(); control != nullptr) {
            if (edit_key(*control, event)) { return true; }
        }
        const float page = static_cast<float>(options_.height) * 0.9f;
        if (event.key == "ArrowDown") {
            scroll_by(options_.wheel_step);
            return true;
        }
        if (event.key == "ArrowUp") {
            scroll_by(-options_.wheel_step);
            return true;
        }
        if (event.key == "PageDown" || event.key == "Space") {
            scroll_by(page);
            return true;
        }
        if (event.key == "PageUp") {
            scroll_by(-page);
            return true;
        }
        if (event.key == "Home") {
            scroll_to(0);
            return true;
        }
        if (event.key == "End") {
            scroll_to(max_scroll());
            return true;
        }
        return false;
    }

    // Returns whether a listener cancelled the default action - NOT whether
    // anything was dispatched, because the caller's question is "may I still do
    // my own thing with this key".
    bool dispatch_key(std::string_view type, const input_event & event) {
        if (!bindings_) { return false; }
        // At the focused element, so a keystroke in a text field is that
        // field's event; at the body otherwise, which is where a game listens.
        const node_id target = focused_ ? focused_ : body_node();
        return bindings_->dispatch_key(type, target, event);
    }
    bool dispatch_mouse(std::string_view type, node_id target, const input_event & event) {
        if (!bindings_) { return false; }
        return bindings_->dispatch_mouse(type, target ? target : body_node(), event);
    }
    [[nodiscard]] node_id body_node() {
        const auto txn = doc_->read();
        const atom body = atoms_.intern_lower("body");
        node_id found{};
        const auto walk = [&](auto && self, node_id at) -> void {
            if (!found && txn.tag(at).value_or(atom{}) == body) { found = at; }
            for (const node_id child : txn.children(at)) { self(self, child); }
        };
        walk(walk, txn.root());
        return found ? found : txn.root();
    }

    // The DOM and the cascade are rebuilt on navigation, and neither type is
    // copyable - a slab with live epochs is not something to assign over.
    void reset_document() {
        doc_ = std::make_unique<document>(atoms_);
        styles_ = std::make_unique<ctbrowser::style::engine>(atoms_);
        styles_->add_sheet(ctbrowser::style::ua_css, ctbrowser::style::ua_origin);
        resolved_.clear();
    }

    // The focused control's editable state, or null when focus is elsewhere.
    // Whether the focused element is one that shows a caret. Distinct from
    // editable_focus(), which SEEDS the control's state - asking when the next
    // blink is due must not create anything.
    [[nodiscard]] bool has_editable_focus() {
        if (!focused_) { return false; }
        const auto txn = doc_->read();
        if (!txn.contains(focused_)) { return false; }
        const control_kind kind = kind_of(txn, focused_);
        return kind == control_kind::text || kind == control_kind::textarea;
    }

    [[nodiscard]] control_state * editable_focus() {
        if (!focused_) { return nullptr; }
        const auto txn = doc_->read();
        if (!txn.contains(focused_)) { return nullptr; }
        const control_kind kind = kind_of(txn, focused_);
        if (kind != control_kind::text && kind != control_kind::textarea) { return nullptr; }
        return &forms_.state_of(txn, atoms_, focused_);
    }

    [[nodiscard]] control_kind kind_of(const read_txn & txn, node_id id) {
        return control_kind_of(atoms_.text(txn.tag(id).value_or(atom{})),
                               txn.attribute_value(id, atoms_.intern("type")));
    }

    // The control a click landed in. A click on the text inside a <button> has
    // to focus the button, not the text node.
    [[nodiscard]] node_id control_ancestor(node_id from) {
        if (!from) { return node_id{}; }
        const auto txn = doc_->read();
        for (node_id at = from; at; at = txn.parent(at)) {
            if (kind_of(txn, at) != control_kind::none) { return at; }
        }
        return node_id{};
    }

    bool focus(node_id id) {
        if (is_disabled(id)) { return false; } // a disabled control cannot take focus
        if (id == focused_) { return false; }
        if (focused_) {
            // `change` fires on BLUR, not on every keystroke - that is the
            // difference between it and `input`, and pages rely on it.
            bindings_->dispatch("change", focused_);
            (void)set_state(focused_, state_focus, false);
            // And the outgoing field DROPS ITS SELECTION. A highlight left
            // behind in a field nobody is typing in reads as still selected,
            // and Ctrl+A followed by a click somewhere else did exactly that.
            const auto txn = doc_->read();
            if (txn.contains(focused_)) {
                const control_kind kind = kind_of(txn, focused_);
                if (kind == control_kind::text || kind == control_kind::textarea) {
                    control_state & state = forms_.state_of(txn, atoms_, focused_);
                    state.selection = state.caret;
                }
            }
        }
        focused_ = id;
        restart_caret_blink(); // a field you just clicked into shows its caret at once
        if (focused_) {
            (void)set_state(focused_, state_focus, true);
            bindings_->dispatch("focus", focused_);
        }
        mark(dirty::paint);
        return true;
    }

    bool edit_key(control_state & control, const input_event & event) {
        const std::string & key = event.key;
        const control_kind kind = kind_of(doc_->read(), focused_);
        const bool multiline = kind == control_kind::textarea;
        if (key == "Backspace") { return edited(forms_.backspace(control)); }
        if (key == "Delete") { return edited(forms_.delete_forward(control)); }
        if (key == "ArrowLeft") { return moved(forms_.move_caret(control, -1, event.shift)); }
        if (key == "ArrowRight") { return moved(forms_.move_caret(control, 1, event.shift)); }
        // UP and DOWN are visual LINES, and Home/End are the ends of one. In a
        // single-line field up and down are the whole value's ends, which is
        // what a browser does with them there.
        if (key == "ArrowUp") {
            return moved(multiline ? move_caret_by_line(focused_, control, kind, -1, event.shift)
                                   : forms_.move_to_edge(control, false, event.shift));
        }
        if (key == "ArrowDown") {
            return moved(multiline ? move_caret_by_line(focused_, control, kind, 1, event.shift)
                                   : forms_.move_to_edge(control, true, event.shift));
        }
        if (key == "Home") {
            return moved(multiline ? move_to_line_edge(focused_, control, kind, false, event.shift)
                                   : forms_.move_to_edge(control, false, event.shift));
        }
        if (key == "End") {
            return moved(multiline ? move_to_line_edge(focused_, control, kind, true, event.shift)
                                   : forms_.move_to_edge(control, true, event.shift));
        }
        // ESCAPE drops the selection and keeps the caret, which is what every
        // browser does with it in a field. Ctrl+A then Escape left the whole
        // value highlighted forever.
        if (key == "Escape") {
            if (control.selection == control.caret) { return false; }
            control.selection = control.caret;
            restart_caret_blink();
            mark(dirty::paint);
            return true;
        }
        if (event.ctrl && key == "KeyA") {
            forms_.select_all(control);
            mark(dirty::paint);
            return true;
        }
        if (key == "Enter") {
            // In a textarea this is a newline; in a single-line field it submits
            // the form, which is the implicit-submission rule every login page
            // depends on.
            const auto txn = doc_->read();
            if (kind_of(txn, focused_) == control_kind::textarea) {
                forms_.insert_text(control, "\n");
                return edited(true);
            }
            submit(form_store::owning_form(txn, atoms_, focused_));
            return true;
        }
        return false;
    }

    bool edited(bool changed) {
        if (!changed) { return false; }
        restart_caret_blink();
        bindings_->dispatch("input", focused_);
        mark(dirty::paint);
        return true;
    }
    bool moved(bool changed) {
        restart_caret_blink();
        if (changed) { mark(dirty::paint); }
        return true; // the key was consumed either way - it must not scroll the page
    }

    // What clicking a control does once no listener has cancelled it.
    void activate(node_id target) {
        // A DISABLED control does nothing and dispatches nothing - it does not
        // toggle, submit, focus or fire an event. Without this the attribute
        // was purely decorative, and it was not even that.
        if (is_disabled(control_ancestor(target))) { return; }
        if (follow_link(target)) { return; }
        if (toggle_details(target)) { return; }
        const node_id control = control_ancestor(target);
        if (!control) { return; }
        const auto txn = doc_->read();
        const control_kind kind = kind_of(txn, control);
        if (kind == control_kind::checkbox || kind == control_kind::radio) {
            forms_.toggle(txn, atoms_, control, kind);
            bindings_->dispatch("change", control);
            mark(dirty::paint);
            return;
        }
        if (kind == control_kind::select) {
            // Toggle: clicking an open select closes it again.
            select_open_ = select_open_ == control ? node_id{} : control;
            mark(dirty::paint);
            return;
        }
        if (kind != control_kind::button) { return; }
        const std::string_view type = txn.attribute_value(control, atoms_.intern("type"));
        const node_id form = form_store::owning_form(txn, atoms_, control);
        if (type == "reset") {
            forms_.reset_form(txn, form);
            mark(dirty::paint);
            return;
        }
        // A <button> with no type is a submit button, which is the default
        // people forget and then wonder why their form reloads.
        if (type.empty() || type == "submit") { submit(form); }
    }

    // Clicking a <summary> opens or closes its <details>. The state is the
    // `open` ATTRIBUTE, as the spec says, so a script that reads or sets it
    // agrees with what the user did - and layout, which builds a closed
    // details' children away, picks it up from the same place.
    bool toggle_details(node_id target) {
        node_id summary;
        node_id details;
        {
            const auto txn = doc_->read();
            const atom summary_tag = atoms_.intern_lower("summary");
            for (node_id at = target; at; at = txn.parent(at)) {
                if (txn.tag(at).value_or(atom{}) == summary_tag) {
                    summary = at;
                    details = txn.parent(at);
                    break;
                }
            }
        }
        if (!summary || !details) { return false; }
        {
            const atom open = atoms_.intern("open");
            const bool was_open = doc_->read().has_attribute(details, open);
            if (was_open) {
                (void)doc_->remove_attribute(details, open);
            } else {
                (void)doc_->set_attribute(details, open, "");
            }
        }
        bindings_->dispatch("toggle", details);
        mark(dirty::everything);
        return true;
    }

    // <a href> - the one navigation-shaped thing a document does on its own.
    // Nearest <a> ANCESTOR, not the clicked node: a link's text, and anything
    // else inside it, is what actually gets clicked.
    bool follow_link(node_id target) {
        std::string href;
        {
            const auto txn = doc_->read();
            const atom anchor = atoms_.intern_lower("a");
            const atom attribute = atoms_.intern("href");
            for (node_id at = target; at; at = txn.parent(at)) {
                if (txn.tag(at).value_or(atom{}) != anchor) { continue; }
                href = txn.attribute_value(at, attribute);
                break;
            }
        }
        if (href.empty()) { return false; }
        location_href_ = href;
        if (href.front() == '#') {
            // A FRAGMENT is not a navigation: it scrolls this document, and the
            // page can read where it went through location.hash.
            location_hash_ = href;
            scroll_to_fragment(href.substr(1));
            bindings_->observe_location(location_href_, location_hash_);
            return true;
        }
        location_hash_.clear();
        bindings_->observe_location(location_href_, location_hash_);
        if (navigate_hook_) { navigate_hook_(href); }
        return true;
    }

    // Put the element with that id at the top of the viewport, clamped the same
    // way a scroll is - an anchor near the end of a short page cannot scroll
    // past the bottom.
    void scroll_to_fragment(std::string_view id) {
        if (id.empty()) { return; }
        node_id target;
        {
            const auto txn = doc_->read();
            const atom key = atoms_.intern("id");
            const auto walk = [&](auto && self, node_id at) -> void {
                if (target) { return; }
                if (txn.attribute_value(at, key) == id) {
                    target = at;
                    return;
                }
                for (const node_id child : txn.children(at)) { self(self, child); }
            };
            walk(walk, txn.root());
        }
        if (!target) { return; }
        // Fragment bounds are relative to the containing block, so finding the
        // element is not enough - the walk has to accumulate to get an absolute
        // y, which is what a scroll offset is measured in.
        bool found = false;
        float top = 0;
        const auto walk = [&](auto && self, const ctbrowser::layout::fragment & f, float dx,
                              float dy) -> void {
            if (found) { return; }
            const rect box = f.absolute_bounds(dx, dy);
            if (f.source == target) {
                found = true;
                top = box.y;
                return;
            }
            for (const auto & child : f.children) { self(self, child, box.x, box.y); }
        };
        walk(walk, fragments_, 0, 0);
        if (found) { scroll_to(top); }
    }

    void submit(node_id form) {
        if (!form) { return; }
        if (bindings_->dispatch("submit", form)) { return; } // cancelled
        const auto txn = doc_->read();
        last_submission_ = forms_.form_data(txn, atoms_, form);
    }

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
    std::function<void(const std::string &)> alert_hook_;
    std::vector<std::string> alerts_;
    std::function<void(const std::string &)> navigate_hook_;
    std::string source_html_;
    std::size_t layouts_ = 0;
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
    canvas_store canvases_;
    form_store forms_;
    node_id focused_;
    std::uint64_t canvas_revision_ = 0;

    std::unique_ptr<script::program> script_program_;
    std::unique_ptr<script::context> script_;
    std::unique_ptr<dom_bindings> bindings_;
    std::string script_error_;
    std::string title_;
    float scroll_y_ = 0;
    float content_height_ = 0;
    node_id hovered_;
    node_id pressed_;
    dirty dirty_ = dirty::everything;
    bool author_sheet_loaded_ = false;
    std::uint64_t frames_ = 0;
};

} // namespace ctbrowser::shell
