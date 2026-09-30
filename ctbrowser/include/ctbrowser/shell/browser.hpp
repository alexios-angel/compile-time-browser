#pragma once
#include <algorithm>
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
// The pipeline is split at the points where work can be reused, and `dirty_`
// is the record of how far back the current frame has to start.
//
// Deliberately SDL-FREE. The window and the event loop live in :app, gated on
// SDL3, and this is what they drive - which is what makes the whole engine
// testable headlessly.

namespace ctbrowser::shell {

using ctbrowser::layout::box_node;
using ctbrowser::layout::fragment;
using ctbrowser::paint::layer_tree;

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
    // The page canvas, behind everything the document draws. White by default,
    // because that is what a browser with no page background shows.
    color background = color{ctbrowser::style::ua_canvas};
    // The overlay scrollbar's width, and the width a tall page gives up to it.
    // 0 hides it - which is what a fixed-size game wants.
    float scrollbar_width = 15.0f;
    // Half the caret's blink period, in milliseconds - Chrome's figure. 0 stops
    // it blinking, which is what a screenshot test wants: a caret that is
    // sometimes there is not byte-comparable.
    double caret_blink_ms = 500;
    // AN EMBEDDER'S ANSWER to a request, asked before the asset registry by a
    // frame's load, a form submission's navigation and fetch()/XHR - see
    // resource_request in page/forms.hpp. Empty for every application;
    // ctdrive fills it with the WPT handler emulation under a document root.
    request_handler on_request{};
};

// Detailed contracts: docs/reference/header-contracts/shell-browser-*.md
class browser {
public:
    explicit browser(browser_options options = {})
        : options_(options), recorder_(atoms_),
          renderer_(options.width, options.height, ctbrowser::raster::default_tile_extent) {
        reset_document();
    }

    browser(const browser &) = delete;
    browser & operator=(const browser &) = delete;
    browser(browser &&) = delete;
    browser & operator=(browser &&) = delete;

    [[nodiscard]] const ctbrowser::raster::software_backend & rendering_with() const noexcept {
        return renderer_;
    }

    // --- content ---------------------------------------------------------

    // Replace the document. Everything downstream is invalidated, which is the
    // one case where that is the honest answer.
    void load_html(std::string_view html);

    enum class source_kind : std::uint8_t {
        html,
        xml
    };

    // The same load, told what it is reading. `load_html(s)` is
    // `load_document(s, source_kind::html)`.
    void load_document(std::string_view source, source_kind kind);

    // Why the last XML parse failed, empty when it did not or when the last
    // document was HTML. XML is draconian - there is no recovery - so this is
    // the difference between a document and an error page.
    [[nodiscard]] const std::string & xml_error() const noexcept { return xml_error_; }

    // --- script ----------------------------------------------------------

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

    void define_native(std::string name, script::native_fn fn);

    bool add_script_image(std::vector<std::byte> image);

    // How many classic scripts this browser compiled rather than loaded from an
    // image. Zero after a load whose every script was cached; without this a
    // cache that silently misses looks exactly like one that works.
    [[nodiscard]] std::size_t scripts_compiled_from_source() const noexcept {
        return scripts_compiled_from_source_;
    }

    // How many module programs this browser is holding alive. One per
    // <script type="module"> on the CURRENT page and none from any earlier one
    // - a module's functions close over its top-level frame, so its program has
    // to outlive the load. A count is the only way to see a leak from outside.
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

    // AND THE MODULE SCRIPTS, WHICH CANNOT BE PACKAGED YET: `load_module`
    // compiles from source every time and there is no image path into it.
    // Published so the packager and the launcher can refuse rather than ship
    // an application that parses all of its JavaScript at every start.
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

    // Collect the script heap now, and how many objects it has. Exposed
    // because "does a collection free what the page is still using" is only
    // answerable from outside.
    std::size_t collect_garbage() { return script_ ? script_->collect() : 0; }
    [[nodiscard]] std::size_t live_script_objects() const;

    [[nodiscard]] const control_state * control_state_of(node_id id);

    // Whether anything has changed that a frame would show. An event loop asks
    // this rather than assuming: a caret blink and a scrollbar that appears
    // after a relayout both change what should be on screen without any event
    // having arrived, and a loop that only redraws when IT did something shows
    // neither until the user happens to move the mouse.
    [[nodiscard]] bool needs_frame() const noexcept { return dirty_ != dirty::nothing; }

    [[nodiscard]] double next_wakeup_ms();

    [[nodiscard]] bool caret_visible() const noexcept;

    void restart_caret_blink() noexcept { caret_base_ms_ = caret_clock_ms_; }

    // Advance the page clock and run whatever became due - timers, then
    // animation frames. An event loop calls this once per tick; the return is
    // how many callbacks ran, so a caller can tell an idle page from a busy one.
    std::size_t tick(double elapsed_ms);

    // Re-parse the page and run its script again from the top - navigation to
    // where you already are, which is the only navigation the engine has.
    void reload();

    void set_alert_hook(std::function<void(const std::string &)> hook);

    // Kept HERE rather than on the bindings, because a reload replaces the
    // bindings - and the alert that caused the reload is exactly the one you
    // want to still be able to read afterwards.
    [[nodiscard]] const std::vector<std::string> & alerts() const noexcept { return alerts_; }

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

    // REAL TIME, if the embedder wants it. Without one, `Date.now()` is a fixed
    // base plus the page's own elapsed time - deterministic, which is what makes
    // a golden possible, and the same reason Math.random is seeded here. An
    // interactive application installs the wall clock instead, because showing
    // the wrong date is a bug no golden cares about; `run_app` does exactly that.
    void set_clock(std::function<double()> clock) { clock_ = std::move(clock); }

    // A LINK THAT LEAVES THE DOCUMENT: (resolved url, the author's raw href,
    // whether the page had transient user activation - which following it
    // consumed). The embedder decides what leaving means; see app_options.
    void set_navigate_hook(
        std::function<void(const std::string & url, const std::string & href, bool user_activated)>
            hook);

    // EVERY CLASSIC SCRIPT'S PROGRAM, ONCE, BEFORE IT RUNS - which is where a
    // packaged application stamps its compiled bodies on.
    //
    // The runtime enters native code only when `function_proto::aot_entry` is
    // set, and a page's programs are built inside `run_scripts` and never leave
    // it: `classic_programs_` is private and only its size is published. So a
    // compiler that generates perfect bodies had nowhere to install them, which
    // made every generated APPLICATION interpret its own JavaScript while every
    // count on the way in read a truthful zero.
    //
    // ENGINE TYPES ONLY, deliberately. The dependency runs one way - ctcompile
    // depends on ctbrowser and never the reverse - so this hands over a
    // `script::program` and the text it was compiled from and knows nothing
    // about entry tables. `ctcompile::aot::install` is the caller.
    //
    // The TEXT and not the hash, because an installer has to know WHICH script
    // this is, and `script_sources()` is the list it was given. It is the same
    // string this browser hashed to look for an image.
    //
    // AFTER the image lookup, so the hook sees the program that will actually
    // run whether it came from an image or from a compile - a hook that only
    // fired on one of those paths would work until the page was packaged.
    void set_script_prepared_hook(std::function<void(script::program &, std::string_view)> hook) {
        script_prepared_hook_ = std::move(hook);
    }
    void set_location(std::string href);

    // What the last activated link recorded. A fragment lands in the hash,
    // because scrolling to an anchor IS navigation within a document.
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
    // compositor.
    void scroll_by(float dy) { scroll_to(scroll_y_ + dy); }

    void scroll_to(float y) { scroll_to(scroll_x_, y); }
    // Both axes. The wheel and the keys move y alone; `window.scrollTo` and
    // the CSSOM View setters (through the bindings' viewport scroll hooks)
    // move either. Clamped to the viewport's scrolling area.
    void scroll_to(float x, float y);

    [[nodiscard]] float scroll_y() const noexcept { return scroll_y_; }

    [[nodiscard]] float scroll_x() const noexcept { return scroll_x_; }

    [[nodiscard]] point scroll_position() const noexcept { return point{scroll_x_, scroll_y_}; }

    [[nodiscard]] float content_height() const noexcept { return content_height_; }

    [[nodiscard]] float content_width() const noexcept { return content_width_; }
    [[nodiscard]] std::string_view cursor_at(float x, float y);

    // Whether a viewport x lands on the scrollbar. Public because the app layer
    // asks it to choose a cursor.
    [[nodiscard]] bool on_scrollbar(float x) const noexcept;
    [[nodiscard]] float max_scroll() const noexcept;
    [[nodiscard]] float max_scroll_x() const noexcept;

    // Whether the page shows its scrollbar: it overflows, the chrome has one,
    // and the viewport's overflow is not hidden (run_layout decides that).
    [[nodiscard]] bool has_scrollbar() const noexcept {
        return scrollbar_shown_ && max_scroll() > 0 && options_.scrollbar_width > 0;
    }

    // --- input -----------------------------------------------------------

    // Returns whether anything changed, so a caller can skip a frame it does
    // not need. A browser that repaints on every mouse move is a browser with a
    // hot fan.
    bool handle(const input_event & event);

    // What is under a viewport point. Scroll is applied here rather than in the
    // fragment tree, because the fragments are in CONTENT space - the same
    // reason a scroll does not invalidate them.
    [[nodiscard]] node_id hit_test(float x, float y) const;

    // A FINGER, from a driver - there is no touchscreen in the app layer yet:
    // `phase` is "start", "move" or "end". Touch Events and the touch pointer's
    // Pointer Events, and a drag nobody cancelled scrolls (browser/input.cpp).
    bool touch(std::string_view phase, float x, float y);

    // --- frames ----------------------------------------------------------

    // Run whatever this frame needs and composite. Cheap when nothing is dirty,
    // which is the common case and the point.
    void frame(scheduler * pool = nullptr);

    [[nodiscard]] rect viewport() const noexcept;

    [[nodiscard]] std::uint64_t frames() const noexcept { return frames_; }

    [[nodiscard]] const fragment & fragments() const noexcept { return fragments_; }

    [[nodiscard]] const layer_tree & layers() const noexcept { return layers_; }

    // The composited image, for goldens and for headless runs.
    [[nodiscard]] const ctbrowser::raster::surface & read_pixels() const noexcept {
        return renderer_.target();
    }

private:
    // The same bits ctcss compiles :hover/:active/:focus into, named here so
    // the browser and the selector matcher cannot drift apart.
    static constexpr std::uint32_t state_hover = ctbrowser::style::engine::state_hover;
    static constexpr std::uint32_t state_active = ctbrowser::style::engine::state_active;
    static constexpr std::uint32_t state_focus = ctbrowser::style::engine::state_focus;
    static constexpr std::uint32_t state_focus_visible = ctbrowser::style::state_focus_visible;

    // Tiles are discarded in frame(), not here: every level at or above
    // `raster` invalidates them, and doing it in one place is what stops a new
    // dirty level from silently forgetting to.
    void mark(dirty d) { dirty_ = worse(dirty_, d); }

    void load_author_styles();
    // The `<style>` and `<link>` text of the document as it is NOW, concatenated
    // in document order. One sheet rather than one per element, for the source-
    // order reason load_author_styles' comment gives.
    [[nodiscard]] std::string collect_author_styles();
    // The same walk over ANY document: the page's (announcing each sheet's
    // `load`) or a frame's, whose bindings answer the CSSOM's edited text.
    [[nodiscard]] std::string collect_author_styles(document & doc, dom_bindings * bindings,
                                                    bool announce);
    // ...and rebuild the author origin from it if it has changed. See the
    // definition for why it does not go through the CSSOM.
    void refresh_author_styles();
    void note_resource_load(node_id id, bool ok);
    void announce_resource_loads();

    // Push the window size and the user's preferences into the style engine, and say
    // whether any media query's truth moved. A resize calls it and only re-resolves the
    // cascade when the answer is yes.
    bool media_environment_changed();

    [[nodiscard]] std::string extract_title();

    void resolve_styles();

    // The body of one load. load_html wraps it so that a navigation asked for
    // by a script is queued rather than performed under that script's feet.
    void load_one_page(std::string_view html, source_kind kind);
    void run_scripts();

    // The measure layout uses, and the fonts the rasterizer draws with, are the
    // SAME object - text lands where layout thought it would only if one thing
    // answers both questions.
    [[nodiscard]] const ctbrowser::raster::font_backend & fonts() const;
    [[nodiscard]] ctbrowser::layout::measure_text_fn measure() const;

    // The page's own @font-face rules, loaded through the asset registry like
    // any other resource. Called when real fonts are turned on and again on
    // every navigation, because the rules belong to the document.
    void load_page_fonts();
    // The style engine's `ch` measurement, re-handed whenever the faces change:
    // the engine caches the advance of `0` per face and size, and this is what
    // clears that cache (engine::set_text_measure).
    void install_text_measure();

    // Every <img src> in the document, decoded once. A missing or undecodable
    // image is remembered as a null so the element lays out at zero size rather
    // than being retried every frame.
    void load_images();

    // Re-resolve every <img>'s bitmap, without clearing the SVG sources. Runs
    // before each layout, because a script can change a src at any time.
    void refresh_images();

    [[nodiscard]] std::shared_ptr<const ctbrowser::paint::bitmap> image_of(node_id id) const;

    void install_embedder_natives();

    void run_layout();
    void settle_container_queries(); // container queries' style -> layout loop; styles.cpp

    struct frame_layout {
        node_id element;         // the <iframe>, in its owner's document
        dom_bindings * bindings; // over the frame's document
        // What this layout was made from, so a read knows when it is stale.
        std::uint64_t version = 0;
        std::uint64_t style_stamp = 0;
        float width = -1; // -1: never laid out, or no box
        float height = -1;
        std::unique_ptr<ctbrowser::style::engine> styles;
        ctbrowser::style::style_map resolved;
        box_node boxes;
        fragment fragments;
    };
    std::vector<std::unique_ptr<frame_layout>> frame_layouts_;
    // After run_layout: every loaded frame, recursively, at its box's size.
    void layout_frames();
    // Whether any frame document moved since layout_frames last saw it.
    [[nodiscard]] bool frames_stale() const;
    // The stages a script's read needs NOW - getComputedStyle, offsetWidth -
    // page first, then the frames. The level is left at `paint` afterwards:
    // the display list still has to be re-recorded before anything is drawn.
    void flush_for_read();

    void record();

    void refresh_chrome();

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

    // The option list of an open <select>.
    void record_select_popup();

    // The value of the nth <option>, for the popup's pick.
    [[nodiscard]] std::string option_value_at(const read_txn & txn, node_id select,
                                              std::size_t index);

    // Every <option>'s text, in document order.
    [[nodiscard]] std::vector<std::string> option_labels(const read_txn & txn, node_id select);

    // Where an element is ON SCREEN - the fragment tree is in content space, so
    // the scroll has to come off.
    [[nodiscard]] rect viewport_box_of(node_id id) const;

    void record_scrollbar();

    // Where the thumb sits. Proportional to how much of the document is
    // visible, with a floor so a very long page still has something to grab.
    [[nodiscard]] rect scrollbar_thumb() const;

    bool paint_svg(node_id id, const rect & box, ctbrowser::paint::display_list & into);

    void paint_replaced(node_id id, const rect & box, const rect & content,
                        const ctbrowser::style::computed_style_ptr & style,
                        ctbrowser::paint::display_list & into);

    void check_mark(const rect & box, ctbrowser::paint::display_list & into, node_id id);

    // A button's label is CENTRED, horizontally and on the box's middle. Left
    // aligned at a fixed inset it drifts off centre the moment the button is
    // wider than its text, which is every button with a width.
    void label_text(const rect & box, const std::string & label, node_id id,
                    const ctbrowser::style::computed_style_ptr & style,
                    ctbrowser::paint::display_list & into);

    // Where a single line of text sits inside a control: vertically centred on
    // the box rather than pinned to a constant, so a control that is taller
    // than its text (every one with a border and padding) still centres it.
    [[nodiscard]] static float baseline_inset(const rect & box, float size);

    struct field_layout {
        rect inner;
        float size = 16;
        float line_height = 20;
        ctbrowser::layout::text_face metrics_face;
        std::vector<std::pair<std::size_t, std::size_t>> lines; // [begin, end) in the VALUE
        bool masked = false;
        std::size_t visible_lines = 1;
        std::size_t scroll_line = 0;
        float scroll_x = 0;
        // The widest line, which is how far right there is to scroll.
        float content_width = 0;
    };

    // THE CONTENT BOX OF A CONTROL, in ONE place.
    //
    // The caret, the hit test, the selection and the painter all have to agree
    // about where a field's text starts: they all ask this, and it asks the
    // cascade.
    [[nodiscard]] rect content_box_of(node_id id, const rect & box) const {
        const layout::box_node * found = find_box(boxes_, id);
        if (found == nullptr) { return box; }
        const layout::resolved_edges e = layout::resolve_edges(
            *found, layout::constraints{box.width, box.height, found->font_size});
        return rect{box.x + e.content_left(), box.y + e.content_top(),
                    std::max(0.0f, box.width - e.horizontal_inner()),
                    std::max(0.0f, box.height - e.vertical_inner())};
    }

    [[nodiscard]] field_layout layout_of_field(const rect & border_box, node_id id,
                                               const control_state & control, control_kind kind);

    // A control is disabled by its own attribute or by an enclosing <fieldset>,
    // which is how a form greys out a whole section at once.
    [[nodiscard]] bool is_disabled(node_id id);

    [[nodiscard]] bool is_password(node_id id);
    [[nodiscard]] static std::string masked_text(std::string_view text);
    // What the user SEES for a stretch of the value.
    [[nodiscard]] static std::string shown(std::string_view text, bool masked);

    // Where in a control's value a point falls. The nearest character boundary
    // on the line the point is on - which is the ONLY way a click can put the
    // caret where the user pointed.
    [[nodiscard]] std::size_t offset_at_point(node_id id, const control_state & control,
                                              control_kind kind, float x, float y);

    // Move the caret one VISUAL LINE, keeping the column. A textarea without
    // this has arrow keys that walk character by character through a newline,
    // which is not what up and down mean.
    bool move_caret_by_line(node_id id, control_state & control, control_kind kind, int direction,
                            bool extend);

    void reveal_caret(node_id id, control_state & control, control_kind kind);

    // Home and End are per LINE in a textarea, as they are in every editor.
    bool move_to_line_edge(node_id id, control_state & control, control_kind kind, bool to_end,
                           bool extend);

    void paint_field_text(const rect & box, node_id id, const control_state & control,
                          control_kind kind, const ctbrowser::style::computed_style_ptr & style,
                          bool focused, ctbrowser::paint::display_list & into);

    [[nodiscard]] static std::vector<std::pair<std::size_t, std::size_t>> value_lines(
        const std::string & value, float wrap_width, float size,
        const ctbrowser::layout::text_face & face,
        const ctbrowser::layout::measure_text_fn & measure);

    // The line a caret is ON, as an index into `lines`. FIRST match wins: a
    // soft break makes end == begin, so a caret sitting on a wrap boundary
    // matches BOTH lines, and the painter would draw two carets.
    [[nodiscard]] static std::size_t caret_line(const field_layout & geometry, std::size_t caret) {
        for (std::size_t index = 0; index < geometry.lines.size(); ++index) {
            const auto [begin, end] = geometry.lines[index];
            if (caret >= begin && caret <= end) { return index; }
        }
        return geometry.lines.empty() ? 0 : geometry.lines.size() - 1;
    }

    [[nodiscard]] std::string button_label(const read_txn & txn, node_id id,
                                           const control_state & control,
                                           std::string_view type) const;

    // The colour a control's own text is drawn in. A DISABLED control ignores
    // the cascade here: `color` on a disabled button is not what a user needs
    // to see, and greyed-out is the only signal the control is dead.
    [[nodiscard]] color control_text_colour(node_id id,
                                            const ctbrowser::style::computed_style_ptr & style) {
        return is_disabled(id) ? color{ctbrowser::style::ua_widget_disabled_text}
                               : text_colour(style);
    }

    [[nodiscard]] color text_colour(const ctbrowser::style::computed_style_ptr & style);

    [[nodiscard]] ctbrowser::layout::text_face face_of(node_id id) const;

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

    [[nodiscard]] std::string selected_option(const read_txn & txn, node_id id);

    // A press while a <select> is open. Returns whether the popup consumed it -
    // a click ANYWHERE else closes it, which is what click-away means, and that
    // click must not also reach the page.
    bool handle_popup_press(const input_event & event);

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
    void clipboard_verb(std::string_view verb);

    // A key event reaches SCRIPT FIRST, and the built-in behaviour - scrolling,
    // caret movement - is the DEFAULT ACTION that runs only if no listener
    // cancelled it.
    bool handle_key(const input_event & event);

    // Returns whether a listener cancelled the default action - NOT whether
    // anything was dispatched, because the caller's question is "may I still do
    // my own thing with this key".
    bool dispatch_key(std::string_view type, const input_event & event);
    bool dispatch_mouse(std::string_view type, node_id target, const input_event & event);
    [[nodiscard]] node_id body_node();

    // The DOM and the cascade are rebuilt on navigation, and neither type is
    // copyable - a slab is not something to assign over.
    void reset_document();

    // Whether the focused element is one that shows a caret. Distinct from
    // editable_focus(), which SEEDS the control's state - asking when the next
    // blink is due must not create anything.
    [[nodiscard]] bool has_editable_focus();

    // The focused control's editable state, or null when focus is elsewhere.
    [[nodiscard]] control_state * editable_focus();

    [[nodiscard]] control_kind kind_of(const read_txn & txn, node_id id);

    [[nodiscard]] node_id labelled_control(const read_txn & txn, node_id from);

    [[nodiscard]] node_id control_ancestor(node_id from);

    [[nodiscard]] bool via_label(node_id from);

    // HTML 6.6, browser/focus.cpp. `focus` runs the focusing steps for any
    // target - null is the viewport - and `focus_update` moves focus to a
    // focusable area it is handed. The origin is what :focus-visible asks.
    enum class focus_origin : std::uint8_t {
        script,
        pointer,
        keyboard
    };
    bool focus(node_id id, focus_origin origin = focus_origin::script);
    bool focus_update(node_id id, focus_origin origin = focus_origin::script);
    [[nodiscard]] bool supports_keyboard_input(const read_txn & txn, node_id id);
    // The user's last say in focus - a key pressed, or a pointer that moved
    // it; `script` until either - which is what a script's focus() shows.
    focus_origin last_focus_by_ = focus_origin::script;
    [[nodiscard]] bool focus_was_moved() const;
    [[nodiscard]] std::optional<long long> tabindex_value(const read_txn & txn, node_id id);
    [[nodiscard]] bool focusable_by_default(const read_txn & txn, node_id id);
    [[nodiscard]] bool is_editing_host(const read_txn & txn, node_id id);
    [[nodiscard]] bool is_scroller(node_id id) const;
    [[nodiscard]] bool holds_focusable(const read_txn & txn, node_id at);
    [[nodiscard]] node_id host_of(const read_txn & txn, node_id id) const;
    [[nodiscard]] bool being_rendered(const read_txn & txn, node_id id) const;
    [[nodiscard]] bool is_focusable_area(const read_txn & txn, node_id id);
    [[nodiscard]] node_id focusable_area_for(const read_txn & txn, node_id target, bool click,
                                             bool & viewport);
    [[nodiscard]] node_id focus_delegate(const read_txn & txn, node_id target, bool click);
    [[nodiscard]] node_id flat_parent(const read_txn & txn, node_id at);
    [[nodiscard]] node_id click_focus_target(node_id from);
    [[nodiscard]] std::vector<node_id> sequential_order(const read_txn & txn, node_id also,
                                                        bool backwards);
    [[nodiscard]] bool sequentially_reachable(const read_txn & txn, node_id candidate,
                                              node_id from);

    // Sequential focus navigation, Tab and Shift+Tab. Wraps at both ends, so
    // Tab off the last control returns to the first.
    bool focus_next(bool backwards);

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

    [[nodiscard]] bool scroll_field_under(const input_event & event);

    bool edit_key(control_state & control, const input_event & event);

    bool edited(bool changed);
    // The same, telling the page WHAT the edit was (Input Events' inputType
    // and data) - the `beforeinput` before it is before_edit's.
    bool edited(bool changed, std::string_view input_type, std::optional<std::string_view> data);
    // `beforeinput` for an edit about to happen: true when a listener
    // cancelled it, or when the field it was aimed at is no longer focused.
    bool before_edit(std::string_view input_type, std::optional<std::string_view> data);
    // A focused <select>'s arrow keys (browser/chrome.cpp).
    bool select_key(const input_event & event);
    // `change` on blur is owed only for a value the user edited since focus.
    bool edited_since_focus_ = false;
    std::string value_at_focus_;
    bool moved(bool changed);

    // What clicking a control does once no listener has cancelled it.
    void activate(node_id target);

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

    // Put the element with that id at the top of the viewport, clamped the same
    // way a scroll is - an anchor near the end of a short page cannot scroll
    // past the bottom.
    void scroll_to_fragment(std::string_view id);

    void submit(node_id form, node_id submitter = {});
    // "Reset the form": fire a cancelable `reset` event and, only if it is not
    // cancelled, clear the controls. A reset button's activation used to skip
    // the event, so `onreset` never ran.
    void reset(node_id form);

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
    ctbrowser::raster::software_backend renderer_;

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
    // The wheel event transaction's target and when its last notch came.
    node_id wheel_target_;
    double wheel_last_ms_ = 0;
    // The element the finger went down on, and where it is now.
    node_id touch_target_;
    point touch_at_;
    // Whether this finger's drag has started panning - its pointer was
    // cancelled then (Pointer Events' "suppress a pointer").
    bool touch_panning_ = false;
    // The checkbox, radio or button a held Space made :active.
    node_id space_active_;
    // The ELEMENT under a viewport point - a text run's hit is its parent's -
    // or the body over nothing: what a wheel or a finger is aimed at.
    [[nodiscard]] node_id element_at(float x, float y);
    // When the next auto-scroll step falls due, on caret_clock_ms_'s scale.
    double autoscroll_due_ms_ = 0;
    std::function<void(const std::string &)> alert_hook_;
    std::vector<std::string> alerts_;
    std::function<void(const std::string &, const std::string &, bool)> navigate_hook_;
    std::function<void(script::program &, std::string_view)> script_prepared_hook_;
    std::string source_html_;
    // Which front end read `source_html_`, so `location.reload()` re-reads it
    // the same way it was read the first time.
    source_kind source_kind_ = source_kind::html;
    std::string xml_error_;
    std::size_t layouts_ = 0;
    // The layout the focus fixup last looked at (frame.cpp).
    std::size_t layouts_fixed_up_ = 0;
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
    // Null means font8x8, which is always available and always identical - so a
    // build with no font files still renders and its goldens still compare.
    const ctbrowser::raster::font_backend * fonts_ = nullptr;
    // Owned so its glyph cache outlives any one frame; the renderer only
    // borrows it. Null in a build without SDL3_ttf.
    std::unique_ptr<ctbrowser::raster::ttf_backend> ttf_;
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

    std::vector<std::unique_ptr<script::program>> classic_programs_;
    struct held_image {
        std::uint64_t source_hash = 0;
        script::script_kind kind = script::script_kind::classic;
        script::image_option option = script::image_option::keep_source;
        std::vector<std::byte> bytes;
    };
    std::vector<held_image> script_images_;
    std::vector<std::string> script_sources_;
    std::vector<std::string> module_sources_;
    bool loading_ = false;
    std::optional<std::string> pending_load_;
    source_kind pending_kind_ = source_kind::html;
    bool load_event_pending_ = false;
    std::size_t scripts_compiled_from_source_ = 0;
    std::vector<std::unique_ptr<script::program>> module_programs_;
    void load_module(const std::string & source, const std::string & specifier);
    void instantiate_module(const std::string & source, const std::string & specifier);
    void evaluate_module(const std::string & specifier);
    // `export ... from` and `export *`: aliases wired once the dependencies are
    // instantiated and before anything evaluates. See the definition.
    void wire_reexports(const std::string & specifier);
    std::vector<std::unique_ptr<script::program>> extra_programs_;
    std::unique_ptr<script::context> script_;
    std::unique_ptr<dom_bindings> bindings_;
    std::string script_error_;
    std::string style_error_;
    // Elements whose load/error is owed but not yet queued, and every element
    // already announced this page. See note_resource_load.
    std::vector<std::pair<node_id, bool>> resource_loads_;
    std::vector<node_id> announced_loads_;
    std::string title_;
    float scroll_y_ = 0;
    float scroll_x_ = 0;
    float content_height_ = 0;
    // False when the viewport's propagated overflow is hidden or clip: the
    // page still scrolls programmatically but reserves and draws no bar.
    bool scrollbar_shown_ = true;
    // The viewport scrolling area's width (CSSOM View §2): the layout width,
    // or further right when something overflows it. content_height_ stays
    // the document's own height, which is what the scrollbar is drawn from.
    float content_width_ = 0;
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
    // Whether the page cancelled that press - then it moved no selection and
    // gives nothing focus.
    bool press_cancelled_ = false;
    dirty dirty_ = dirty::everything;
    bool author_sheet_loaded_ = false;
    // What the author sheet was built from, so a restyle can tell whether the
    // page has changed its stylesheets since.
    std::string author_css_;
    std::uint64_t frames_ = 0;
};

} // namespace ctbrowser::shell
