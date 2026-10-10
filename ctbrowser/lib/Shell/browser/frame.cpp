//===- ctbrowser/lib/Shell/browser/frame.cpp - Frames, ticks, scrolling ---===//
//
// The browser's frame loop: the page clock and tick(), the four stages a
// frame re-runs according to what changed (styles, layout, paint recording,
// raster), resize, the viewport and scrolling.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::shell {

namespace {

/// Whether the viewport's overflow hides its scrollbar (CSS Overflow 3 §3.3).
/// The root's `overflow-y` propagates to the viewport, and when the root's is
/// `visible` the body's does instead. A viewport at `hidden` or `clip` shows
/// no scrollbar but still scrolls programmatically, which is what WPT
/// scrollintoview.html's `body { padding: 4000px; overflow: hidden }`
/// measures against innerWidth.
[[nodiscard]] bool viewport_hides_overflow(atom_table & atoms,
                                           const ctbrowser::style::style_map & resolved,
                                           const read_txn & txn) {
    const auto overflow_y = [&](node_id id) -> std::string_view {
        if (!id) { return {}; }
        const auto found = resolved.find(ctbrowser::style::engine::key_of(id));
        if (found == resolved.end() || !found->second) { return {}; }
        return trim(found->second->get(atoms.intern("overflow-y")), html_whitespace);
    };
    const node_id root = txn.root();
    std::string_view used = overflow_y(root);
    if (used.empty() || ascii_iequals(used, "visible")) {
        for (const node_id child : txn.children(root)) {
            if (txn.element_ns(child) == node_ns::html && txn.local_name(child) == "body") {
                used = overflow_y(child);
                break;
            }
        }
    }
    return ascii_iequals(used, "hidden") || ascii_iequals(used, "clip");
}

} // namespace

double browser::next_wakeup_ms() {
    double soonest = std::numeric_limits<double>::infinity();
    if (bindings_) {
        soonest = std::min(soonest, bindings_->next_callback_ms());
        // A CSS animation or transition with an event still to fire (its
        // start, an iteration, its end). Nothing else about one needs a frame:
        // the overlay is sampled when read, not painted. A finished page with
        // no running animation asks for nothing, so the loop goes idle again.
        soonest = std::min(soonest, bindings_->next_animation_event_ms());
    }
    if (focused_ && options_.caret_blink_ms > 0 && has_editable_focus()) {
        const double period = options_.caret_blink_ms * 2;
        const double since = std::fmod(caret_clock_ms_ - caret_base_ms_, period);
        soonest =
            std::min(soonest, since < options_.caret_blink_ms ? options_.caret_blink_ms - since
                                                              : period - since);
    }
    // Only while a step can actually happen: autoscroll_now() reports nothing
    // once the view has hit its limit, so a pointer parked below a fully
    // scrolled field costs no wakeups at all.
    if (autoscroll_now().live()) {
        soonest = std::min(soonest, std::max(0.0, autoscroll_due_ms_ - caret_clock_ms_));
    }
    return soonest;
}

bool browser::caret_visible() const noexcept {
    if (options_.caret_blink_ms <= 0) { return true; } // Blinking off: always solid.
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
        // Idle: keep the due time pinned to now. If it fell behind while
        // nothing was scrolling, the moment auto-scroll starts would fire one
        // step for every interval it sat idle, and a drag that pauses in the
        // middle of a field and then leaves it would jump instead of creeping.
        autoscroll_due_ms_ = caret_clock_ms_;
    }
    // A loop, not a single step: one tick covering half a second must perform
    // every step that fits in it, or the scroll rate silently becomes the
    // frame rate. It terminates because each step moves the view towards a
    // limit and autoscroll_now() reports nothing once it is there. The
    // interval is re-read each time, so dragging further away speeds it up
    // mid-tick.
    while (at.live() && caret_clock_ms_ >= autoscroll_due_ms_) {
        autoscroll_step(at);
        autoscroll_due_ms_ += autoscroll_interval_ms(at.below != 0 ? at.below : at.beside);
        at = autoscroll_now();
    }
    // Only the caret changed, so only the paint is stale: a blink must not
    // re-run layout.
    if (focused_ && caret_visible() != was_visible) { mark(dirty::paint); }
    // Order within a tick, before this tick's timers:
    //  1. Navigations that form submissions planned on an earlier turn, so a
    //     frame a submission navigates has its new document before this tick
    //     announces anything (run_planned_navigations in bindings/frames.cpp).
    //  2. Frames, built before `load`: a page's `load` handler is where WPT
    //     reads `frame.contentDocument`, so a frame whose document were built
    //     later would not be there when it is looked for.
    //  3. `DOMContentLoaded` then `load`, once per document. Both go to the
    //     window (dispatch() with an empty node is the window and the document
    //     at once, which is how their listeners are stored). Dispatching them
    //     before the timers means a handler that schedules `setTimeout(f, 0)`
    //     gets f on the very next tick, as testharness.js expects.
    bindings_->run_planned_navigations();
    bindings_->reconcile_frames();
    if (load_event_pending_) {
        load_event_pending_ = false;
        bindings_->set_ready_state("interactive");
        // The tasks the parse queued run before the two that "the end" queues
        // (HTML 13.2.7 queues DOMContentLoaded, then load, as tasks). So the
        // `unhandledrejection` a parser-run script's checkpoint queued comes
        // between `interactive` and `complete` (WPT
        // promise-rejection-event-during-parse.html), and a script inserted
        // by a script, which delays the load event, has run. The page's own
        // setTimeouts are a separate task source and do not run here.
        (void)bindings_->run_timers(true);
        (void)bindings_->dispatch("DOMContentLoaded", node_id{});
        bindings_->set_ready_state("complete");
        (void)bindings_->dispatch("load", node_id{});
        if (script_error_.empty() && !bindings_->callback_error().empty()) {
            script_error_ = bindings_->callback_error();
        }
    }
    bindings_->advance_clock(elapsed_ms);
    const std::size_t ran = bindings_->run_due_callbacks();
    // Then "update animations and send events" (Web Animations §5.3.2): the
    // animationstart/animationend/transitionend crossings the clock's move
    // produced, after the timers and the frame callbacks of this tick.
    bindings_->tick_animations();
    // A fault in a timer or animation-frame callback is a script error too;
    // without this, a page whose draw loop threw would look merely idle.
    if (script_error_.empty() && !bindings_->callback_error().empty()) {
        script_error_ = bindings_->callback_error();
    }
    // Collect between callbacks, never inside one, for the same reason a
    // reload is handled here.
    if (script_) { (void)script_->collect_if_due(); }
    // Reload between callbacks, never inside one: reloading tears down the
    // script context, and location.reload() is called from a function running
    // in it. A page that reloads on game-over would take the VM with it.
    if (bindings_->reload_requested()) { reload(); }
    return ran;
}

void browser::resize(int width, int height) {
    if (width == options_.width && height == options_.height) { return; }
    options_.width = std::max(1, width);
    options_.height = std::max(1, height);
    // Resize the renderer, do not replace it: the fonts and the raster
    // counter stay, only the tiles are thrown away.
    renderer_.resize(options_.width, options_.height);
    // Re-evaluate the media queries, and mark the cascade dirty only if one of
    // them actually flipped (which is why set_environment reports it).
    // Dragging a window across Bootstrap's 576px breakpoint is then one
    // `dirty::styles` frame and hundreds of `dirty::layout` ones, as in
    // Chrome, and a page with no `@media` at all never re-resolves, the
    // invariant browser.hpp states about a resize.
    const bool flipped = media_environment_changed();
    // Report the change to the page's MediaQueryLists (CSSOM View §13) whether
    // or not a style sheet's query flipped: a list's query need not be in any
    // sheet. The `change` events go out on the next tick.
    if (bindings_) { bindings_->report_media_query_changes(); }
    mark(flipped ? dirty::styles : dirty::layout);
}

/// Pushes the window's size and the user's preferences into the style engine.
/// Returns whether any media query's result changed as a result.
bool browser::media_environment_changed() {
    ctbrowser::style::css::media_environment env = styles_->environment();
    env.viewport_width = static_cast<float>(options_.width);
    env.viewport_height = static_cast<float>(options_.height);
    return styles_->set_environment(env);
}

void browser::scroll_to(float x, float y) {
    const float clamped_x = std::clamp(x, 0.0f, max_scroll_x());
    const float clamped = std::clamp(y, 0.0f, max_scroll());
    if (clamped == scroll_y_ && clamped_x == scroll_x_) { return; }
    scroll_y_ = clamped;
    scroll_x_ = clamped_x;
    layers_.scroll_to(scroll_x_, scroll_y_);
    // The page hears about it on the next tick, whoever moved the view: the
    // wheel, a key, the scrollbar or window.scrollTo (CSSOM View §13.1).
    if (bindings_) { bindings_->queue_scroll_event(node_id{}); }
    // Page tiles are in content space and survive a scroll, but the scrollbar
    // thumb depends on the scroll position. Redrawing its display list is not
    // enough: tiles are keyed by (layer, column, row), so the cached tile would
    // be served again. Discard the chrome layer's tiles as well.
    refresh_chrome();
    if (page_layers_ < layers_.layers.size()) {
        renderer_.discard_layer(static_cast<std::uint32_t>(page_layers_));
    }
    // Nothing else is marked dirty: tiles are in content space and survive.
}

bool browser::on_scrollbar(float x) const noexcept {
    return has_scrollbar() && x >= static_cast<float>(options_.width) - options_.scrollbar_width;
}

float browser::max_scroll() const noexcept {
    return std::max(0.0f, content_height_ - static_cast<float>(options_.height));
}

float browser::max_scroll_x() const noexcept {
    return std::max(0.0f, content_width_ - layout_width_);
}

rect browser::viewport() const noexcept {
    return rect{0, 0, static_cast<float>(options_.width), static_cast<float>(options_.height)};
}

void browser::frame(scheduler * pool) {
    // Push values a script wrote into `value`/`checked` outside an event handler
    // (at the top of a script, say) to the controls; dispatch covers the rest.
    if (bindings_ && bindings_->refresh_wrappers()) { mark(dirty::paint); }
    // Anything drawn into a canvas since the last frame makes its tiles
    // stale. Asking here rather than being told keeps the bindings from
    // having to know what a tile is.
    if (canvases_.total_revision() != canvas_revision_) {
        dirty_ = worse(dirty_, dirty::raster);
        canvas_revision_ = canvases_.total_revision();
    }
    if (dirty_ >= dirty::raster) { renderer_.discard(); }
    renderer_.clear_color = options_.background;
    // The four stages: style resolution, layout, paint recording and (in draw()
    // below) rasterising. Each runs only when the dirty level reaches it, so a
    // scroll skips the first three; layout_count() lets tests check that.
    if (dirty_ >= dirty::styles) { resolve_styles(); }
    if (dirty_ >= dirty::layout) {
        run_layout();
    } else if (frames_stale()) {
        layout_frames();
    }
    if (dirty_ >= dirty::paint) { record(); }
    dirty_ = dirty::nothing;
    // The focus fixup (HTML, "update the rendering"): a focused element that a
    // layout since the last check left no longer a focusable area (hidden,
    // disabled, made inert) gives focus to the viewport, `blur` and all. This
    // runs after the dirty reset, so what it changes is drawn next frame.
    if (std::exchange(layouts_fixed_up_, layouts_) != layouts_ && focused_) {
        bool lost = false;
        bool typing = false;
        {
            const auto txn = doc_->read();
            lost = !txn.contains(focused_) || !is_focusable_area(txn, focused_);
            typing = !lost && supports_keyboard_input(txn, focused_);
        }
        if (lost) {
            (void)focus_update(node_id{});
        } else if (typing) {
            // A focused control that became a text field (its type changed
            // while focused) shows its focus from now on (WPT
            // focus-visible-027).
            (void)set_state(focused_, state_focus_visible, true);
        }
    }
    ++frames_;
    // Paint Timing's first-paint: the first rendering update this document
    // has had. Idempotent, and the bindings are rebuilt per document, so a
    // navigation gets its own.
    if (bindings_) { bindings_->record_first_paint(); }
    ctbrowser::raster::draw(renderer_, layers_, pool, ctbrowser::raster::default_tile_extent,
                            viewport());
}

void browser::resolve_styles() {
    refresh_author_styles();
    const auto txn = doc_->read();
    // The before-change style (CSS Transitions 1 §3) is the previous
    // resolution's map, kept for the length of this one so the bindings can
    // compare each element's two computed styles once the new map is whole.
    const ctbrowser::style::style_map before = std::move(resolved_);
    resolved_ = styles_->resolve_all(txn);
    // A root with `overflow: scroll` always has its scrollbars, and the
    // viewport units exclude them (viewport_less_scroll_root). The cascade is
    // re-run at that size when it differs from the window's, which is the one
    // extra pass a page that asked for scrollbars pays.
    const auto [width, height] =
        viewport_less_scroll_root(atoms_, resolved_, txn.root(), static_cast<float>(options_.width),
                                  static_cast<float>(options_.height), options_.scrollbar_width);
    ctbrowser::style::css::media_environment env = styles_->environment();
    if (env.viewport_width != width || env.viewport_height != height) {
        env.viewport_width = width;
        env.viewport_height = height;
        (void)styles_->set_environment(env);
        resolved_ = styles_->resolve_all(txn);
    }
    // The style change event: every `animation-name` and every changed
    // property with a matching `transition-property` becomes an animation
    // record now, sampled at this clock, so a read that asked for this flush
    // sees the cascade's animations on the same call
    // (bindings/animations/css.cpp).
    if (bindings_) { bindings_->update_css_animations(txn, before, resolved_); }
}

void browser::run_layout() {
    ++layouts_;
    // The <img> set is re-resolved first: an image whose src a script assigned
    // has to be measurable by the layout that follows, not the one after it.
    refresh_images();
    const auto txn = doc_->read();
    ctbrowser::layout::box_builder builder{atoms_, resolved_, measure()};
    // An <img> with no width/height attribute is as big as its bitmap. Only
    // the browser knows that: layout cannot decode images and should not
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
    // The page is as tall as its scrolling area (CSSOM View §2), not as its
    // root box: an absolutely positioned box below the fold is reachable by
    // scrolling in every browser, and the WPT scroll-behavior-main-frame-window
    // tests lay their whole content out that way. There is no viewport floor:
    // a short page stays shorter than the window.
    const auto page_height = [&](float width) {
        return std::max(fragments_.bounds.height,
                        ctbrowser::layout::viewport_scrolling_area(fragments_, width, 0).height);
    };
    fragments_ =
        eng.run(boxes_, static_cast<float>(options_.width), static_cast<float>(options_.height));
    content_height_ = page_height(static_cast<float>(options_.width));

    // Two passes when the page overflows: the scrollbar takes width away from
    // the content, and content laid out at the full width would run under it.
    // A second pass is enough because narrowing a page can only make it
    // taller, so a page that overflowed still overflows; it never oscillates
    // between needing a bar and not.
    layout_width_ = static_cast<float>(options_.width);
    scrollbar_shown_ = !viewport_hides_overflow(atoms_, resolved_, txn);
    if (options_.scrollbar_width > 0 && content_height_ > static_cast<float>(options_.height) &&
        scrollbar_shown_) {
        layout_width_ = static_cast<float>(options_.width) - options_.scrollbar_width;
        fragments_ = eng.run(boxes_, layout_width_, static_cast<float>(options_.height));
        content_height_ = page_height(layout_width_);
    }
    content_width_ = ctbrowser::layout::viewport_scrolling_area(fragments_, layout_width_,
                                                                static_cast<float>(options_.height))
                         .width;
    scroll_y_ = std::clamp(scroll_y_, 0.0f, max_scroll());
    scroll_x_ = std::clamp(scroll_x_, 0.0f, max_scroll_x());
    // offsetWidth and friends read the fragment tree, so they answer with
    // this layout rather than the one before it.
    if (bindings_) {
        bindings_->observe_layout(&fragments_);
        // getComputedStyle needs the box tree and the cascade as well as the
        // fragments: a resolved length comes from the box, a keyword from the
        // style map. All three are re-pointed together so a page can never read
        // this layout's geometry beside the previous layout's styles.
        bindings_->observe_boxes(&boxes_);
        bindings_->observe_styles(&resolved_);
        // The width layout ran at, not the window's: when the page overflows,
        // the scrollbar takes `scrollbar_width` from the initial containing
        // block, and `clientWidth` must report the room layout actually gave
        // the page. This is the layout viewport, which is what the spec means
        // by the root element's client rectangle.
        bindings_->observe_viewport(layout_viewport_width(), options_.height);
        // Refresh existing wrappers too: offsetWidth and friends are cached
        // data properties, and `document.documentElement`/`document.body` are
        // wrapped once and may never see an event that would refresh them.
        (void)bindings_->refresh_wrappers();
    }
    // The frames inside, at the sizes this layout just gave them.
    layout_frames();
    settle_container_queries();
}

void browser::record() {
    // Bracket the SVG cache around the recording, and only around a recording.
    // Every graphic still on the page is asked for during record_layers below,
    // so anything not asked for belongs to a size that no longer exists and
    // is evicted. Dragging a window edge would otherwise keep one full raster
    // per pixel dragged.
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
    layers_.scroll_to(scroll_x_, scroll_y_);
    page_layers_ = layers_.layers.size(); // Layers after this are chrome.
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
