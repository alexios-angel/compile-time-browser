// browser - the frame: the page clock and tick(), the four stages a frame
// re-runs according to what changed, resize, the viewport and scrolling.
//
// One of ten files carved out of a 2,871-line Shell/browser.cpp on 2026-09-08.
// All are member functions of one class declared in
// include/ctbrowser/shell/browser.hpp; internal.hpp beside this carries the
// includes browser.cpp had, so every file sees exactly what it saw. Nothing
// about the public header changed.

#include "internal.hpp"

namespace ctbrowser::shell {

double browser::next_wakeup_ms() {
    double soonest = std::numeric_limits<double>::infinity();
    if (bindings_) { soonest = std::min(soonest, bindings_->next_callback_ms()); }
    if (focused_ && options_.caret_blink_ms > 0 && has_editable_focus()) {
        const double period = options_.caret_blink_ms * 2;
        const double since = std::fmod(caret_clock_ms_ - caret_base_ms_, period);
        soonest =
            std::min(soonest, since < options_.caret_blink_ms ? options_.caret_blink_ms - since
                                                              : period - since);
    }
    // Only while a step is actually DUE TO HAPPEN - autoscroll_now() reports
    // nothing once the view has hit its limit, so a pointer parked below a
    // fully-scrolled field costs no wakeups at all.
    if (autoscroll_now().live()) {
        soonest = std::min(soonest, std::max(0.0, autoscroll_due_ms_ - caret_clock_ms_));
    }
    return soonest;
}

bool browser::caret_visible() const noexcept {
    if (options_.caret_blink_ms <= 0) { return true; } // blinking off: always solid
    const double since = caret_clock_ms_ - caret_base_ms_;
    const double period = options_.caret_blink_ms * 2;
    return std::fmod(since, period) < options_.caret_blink_ms;
}

std::size_t browser::tick(double elapsed_ms) {
    const bool was_visible = caret_visible();
    caret_clock_ms_ += elapsed_ms;
    // Auto-scroll steps that came due.
    autoscroll_state at = autoscroll_now();
    if (!at.live()) {
        // Idle: keep the due time pinned to now. Letting it fall behind while
        // nothing is scrolling would make the moment it ARMS fire one step for
        // every interval it sat idle - a drag that pauses in the middle of a
        // field and then leaves it would jump instead of creeping.
        autoscroll_due_ms_ = caret_clock_ms_;
    }
    // A LOOP, not a single step: one tick covering half a second must perform
    // every step that fits in it, or the scroll rate silently becomes the frame
    // rate. It terminates because each step moves the view towards a limit and
    // autoscroll_now() reports nothing once it is there. The interval is
    // re-read each time, so dragging further away speeds it up mid-tick.
    while (at.live() && caret_clock_ms_ >= autoscroll_due_ms_) {
        autoscroll_step(at);
        autoscroll_due_ms_ += autoscroll_interval_ms(at.below != 0 ? at.below : at.beside);
        at = autoscroll_now();
    }
    // Only the CARET changed, so only the paint is stale - a blink must not
    // re-run layout, which is what made the previous engine lay the page out every frame.
    if (focused_ && caret_visible() != was_visible) { mark(dirty::paint); }
    // `DOMContentLoaded` AND THEN `load`, in that order, once per document.
    // Both go to the window - dispatch() with an empty node is the window and
    // the document at once, which is how their listeners are already stored -
    // and both are dispatched BEFORE this tick's timers, so a handler that
    // schedules `setTimeout(f, 0)` gets f on the very next tick rather than one
    // further out. testharness.js does exactly that.
    // THE FRAMES FIRST, and this is the ordering the corpus turns on: a page's
    // `load` handler is where WPT reads `frame.contentDocument`, so a frame
    // whose document is built with the timers - after that event - is a frame
    // that was never there when it was looked for. See bindings/frames.cpp.
    bindings_->reconcile_frames();
    if (load_event_pending_) {
        load_event_pending_ = false;
        (void)bindings_->dispatch("DOMContentLoaded", node_id{});
        (void)bindings_->dispatch("load", node_id{});
        if (script_error_.empty() && !bindings_->callback_error().empty()) {
            script_error_ = bindings_->callback_error();
        }
    }
    bindings_->advance_clock(elapsed_ms);
    const std::size_t ran = bindings_->run_due_callbacks();
    // A fault in a timer or an animation frame is a script error too. It was
    // not reported anywhere before, so a page whose draw loop threw looked
    // exactly like a page that had finished loading and had nothing to do.
    if (script_error_.empty() && !bindings_->callback_error().empty()) {
        script_error_ = bindings_->callback_error();
    }
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

void browser::resize(int width, int height) {
    if (width == options_.width && height == options_.height) { return; }
    options_.width = std::max(1, width);
    options_.height = std::max(1, height);
    // RESIZE the renderer, do not replace it. Replacing it built a fresh
    // software backend, so an app that chose the GPU silently dropped to
    // software on its first window resize and never came back.
    renderer_.resize(options_.width, options_.height);
    // RE-EVALUATE THE MEDIA QUERIES, and mark the cascade dirty only if one of them
    // actually FLIPPED. That distinction is the whole reason set_environment reports
    // it: dragging a window across Bootstrap's 576px breakpoint is one `dirty::styles`
    // frame and hundreds of `dirty::layout` ones, which is what Chrome does too - and a
    // page with no `@media` at all never re-resolves, which is the invariant
    // browser.hpp states about a resize.
    mark(media_environment_changed() ? dirty::styles : dirty::layout);
}

// Push the window's size and the user's preferences into the style engine. Returns
// whether any media query's truth moved as a result.
bool browser::media_environment_changed() {
    ctbrowser::style::css::media_environment env = styles_->environment();
    env.viewport_width = static_cast<float>(options_.width);
    env.viewport_height = static_cast<float>(options_.height);
    return styles_->set_environment(env);
}

void browser::scroll_to(float y) {
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

bool browser::on_scrollbar(float x) const noexcept {
    return max_scroll() > 0 && options_.scrollbar_width > 0 &&
           x >= static_cast<float>(options_.width) - options_.scrollbar_width;
}

float browser::max_scroll() const noexcept {
    return std::max(0.0f, content_height_ - static_cast<float>(options_.height));
}

rect browser::viewport() const noexcept {
    return rect{0, 0, static_cast<float>(options_.width), static_cast<float>(options_.height)};
}

std::expected<void, ctbrowser::raster::gpu_error> browser::frame(scheduler * pool) {
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
    // TIMED PER STAGE, because the four of them are what the dirty level exists
    // to choose between and the profiler could only see their sum. A stage that
    // is skipped reports 0, which is the number worth looking at: an idle page
    // or a scroll SHOULD leave three of these at zero, and "it didn't" is the
    // regression this cannot otherwise catch.
    //
    // Four clock reads on a path that then rasterises the viewport. The clock
    // is not the cost here; guessing which stage was has been, three times.
    using clock = std::chrono::steady_clock;
    const auto ms_since = [](clock::time_point from) {
        return std::chrono::duration<double, std::milli>(clock::now() - from).count();
    };
    timing_ = frame_timing{};
    auto at = clock::now();
    if (dirty_ >= dirty::styles) {
        resolve_styles();
        timing_.styles_ms = ms_since(at);
        at = clock::now();
    }
    if (dirty_ >= dirty::layout) {
        run_layout();
        timing_.layout_ms = ms_since(at);
        at = clock::now();
    }
    if (dirty_ >= dirty::paint) {
        record();
        timing_.record_ms = ms_since(at);
        at = clock::now();
    }
    dirty_ = dirty::nothing;
    ++frames_;
    auto drawn =
        ctbrowser::raster::draw(renderer_, layers_, pool, options_.tile_extent, viewport());
    timing_.raster_ms = ms_since(at);
    return drawn;
}

void browser::resolve_styles() {
    refresh_author_styles();
    const auto txn = doc_->read();
    resolved_ = styles_->resolve_all(txn);
}

void browser::run_layout() {
    ++layouts_;
    // The <img> set is re-resolved first: an image whose src a script assigned
    // has to be measurable by the layout that follows, not the one after it.
    refresh_images();
    const auto txn = doc_->read();
    ctbrowser::layout::box_builder builder{atoms_, resolved_, measure()};
    // An <img> with no width/height attribute is as big as its bitmap. Only
    // the browser knows that - layout cannot decode images and should not
    // learn how.
    builder.intrinsic_image = [this](node_id id) {
        const auto pixels = image_of(id);
        if (pixels) {
            return ctbrowser::layout::box_builder::intrinsic_size{
                static_cast<float>(pixels->width), static_cast<float>(pixels->height)};
        }
        // An SVG has no decoded bitmap to measure, and must not need one: its
        // size comes from an in-engine scan of the markup, so a build with no
        // plutosvg lays the page out identically and just draws nothing.
        const svg_natural natural = svg_.natural_of(id);
        if (natural.known()) {
            return ctbrowser::layout::box_builder::intrinsic_size{natural.width, natural.height};
        }
        return ctbrowser::layout::box_builder::intrinsic_size{};
    };
    boxes_ = builder.build(txn, txn.root());
    const ctbrowser::layout::engine eng{measure()};
    fragments_ =
        eng.run(boxes_, static_cast<float>(options_.width), static_cast<float>(options_.height));
    content_height_ = fragments_.bounds.height;

    // TWO PASSES when the page overflows: the scrollbar takes width away
    // from the content, and content laid out at the full width would run
    // under it. This terminates because narrowing a page can only make it
    // TALLER, so a page that overflowed still overflows - it never
    // oscillates between needing a bar and not.
    layout_width_ = static_cast<float>(options_.width);
    if (options_.scrollbar_width > 0 && content_height_ > static_cast<float>(options_.height)) {
        layout_width_ = static_cast<float>(options_.width) - options_.scrollbar_width;
        fragments_ = eng.run(boxes_, layout_width_, static_cast<float>(options_.height));
        content_height_ = fragments_.bounds.height;
    }
    scroll_y_ = std::clamp(scroll_y_, 0.0f, max_scroll());
    // offsetWidth and friends read the fragment tree, so they answer with
    // THIS layout rather than the one before it.
    if (bindings_) {
        bindings_->observe_layout(&fragments_);
        // getComputedStyle needs the box tree and the cascade as well as the
        // fragments: a resolved length comes from the box, a keyword from the
        // style map. All three are re-pointed together so a page can never read
        // this layout's geometry beside the previous layout's styles.
        bindings_->observe_boxes(&boxes_);
        bindings_->observe_styles(&resolved_);
        // The width LAYOUT RAN AT, not the window's. When the page overflows the
        // scrollbar takes 15px from the initial containing block, and a
        // `clientWidth` that answered with the window would tell a page it had
        // room the layout did not give it - Bootstrap's `.container` centred
        // itself in 1009px while the page was told 1024, so every script-driven
        // measurement was 15px out. This is the layout viewport, which is what
        // the spec means by the root element's client rectangle.
        bindings_->observe_viewport(layout_viewport_width(), options_.height);
        // AND PUSH THE NEW GEOMETRY INTO THE WRAPPERS. An element object holds
        // offsetWidth and friends as data properties, refreshed on access and
        // before an event dispatches - but `document.documentElement` and
        // `document.body` are wrapped once at install time and a page that only
        // reads them, never dispatching an event, saw the geometry from before
        // the first layout: offsetWidth 0 on the root of every page, forever.
        // Re-pointing the fragment tree without re-reading it is what left them
        // stale, so the two go together.
        (void)bindings_->refresh_wrappers();
    }
}

void browser::record() {
    // Bracket the SVG cache around the recording, and ONLY around a recording.
    // Every graphic still on the page is asked for during record_layers below,
    // so anything left unasked-for afterwards belongs to a size that no longer
    // exists - which is what dragging a window edge produces, one full raster
    // per pixel dragged, until something drops them.
    svg_.begin_frame();

    // The selection's highlight. Computed here rather than stored on the
    // fragment, and looked up by the fragment the recorder is drawing.
    recorder_.selection_of = [this](const ctbrowser::layout::fragment & f) {
        return highlight_for(f);
    };
    recorder_.paint_replaced = [this](node_id id, const ctbrowser::rect & box,
                                      const ctbrowser::rect & content,
                                      const ctbrowser::style::computed_style_ptr & style,
                                      ctbrowser::paint::display_list & into) {
        paint_replaced(id, box, content, style, into);
    };
    layers_ = recorder_.record_layers(fragments_);
    layers_.scroll_to(0, scroll_y_);
    page_layers_ = layers_.layers.size(); // everything after this is chrome
    record_chrome();
    svg_.end_frame();
}

void browser::refresh_chrome() {
    layers_.layers.resize(std::min(page_layers_, layers_.layers.size()));
    record_chrome();
}

void browser::record_chrome() {
    record_scrollbar();
    record_select_popup();
    record_context_menu();
}

} // namespace ctbrowser::shell
