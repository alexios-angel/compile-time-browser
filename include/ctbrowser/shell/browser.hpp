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

#include <ctbrowser/shell/assets.hpp>
#include <ctbrowser/shell/bindings.hpp>
#include <ctbrowser/shell/canvas.hpp>
#include <ctbrowser/shell/forms.hpp>
#include <ctbrowser/shell/images.hpp>
#include <ctbrowser/shell/input.hpp>
#include <ctbrowser/shell/metrics.hpp>

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

    // Turn on real fonts. Loads the vendored OFL faces through the asset
    // registry - so an application that baked them in never touches the disk -
    // and leaves font8x8 in place if SDL3_ttf is absent or none of them load.
    //
    // OPT-IN rather than automatic: the goldens are font8x8's pixels, and a
    // page that silently changed how it renders because a font file happened to
    // be next to the binary would be a worse default than one that looks the
    // same everywhere.
    bool use_real_fonts(std::string_view directory = "fonts");
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
    [[nodiscard]] dom_bindings & bindings() noexcept { return *bindings_; }
    [[nodiscard]] const std::string & script_error() const noexcept { return script_error_; }

    // Run a snippet in the page's own script context - the same globals, the
    // same document. This is what a devtools console types into, and what a
    // test uses to ask a page a question. Returns whether it ran.
    bool run_script(std::string_view source);

    // How many times layout has run. Observable because the whole dirty-level
    // design exists to keep this number down: a caret blink or a scroll must
    // not increment it.
    [[nodiscard]] std::size_t layout_count() const noexcept { return layouts_; }

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

    // <style> elements in the document. A real browser also fetches <link
    // rel=stylesheet>; there is no network here yet, and pretending otherwise
    // would mean silently rendering pages wrong.
    void load_inline_styles();

    [[nodiscard]] std::string extract_title();

    void resolve_styles();

    // Run every <script> in the document, in order. Errors are recorded rather
    // than thrown: a page whose script fails still has to render, which is what
    // every browser does and what makes a broken script a broken feature rather
    // than a blank window.
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

    void run_clipboard_verb(std::string_view verb);

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

    // The control a click landed in. A click on the text inside a <button> has
    // to focus the button, not the text node.
    [[nodiscard]] node_id control_ancestor(node_id from);

    bool focus(node_id id);

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
