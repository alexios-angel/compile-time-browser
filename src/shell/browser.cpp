#include <ctbrowser/shell/browser.hpp>

// The browser's method bodies.
//
// browser.hpp was 2,356 lines because the class was defined with every body
// inline, so reading "what can a browser do" meant scrolling past how each
// answer works - and every translation unit that included it parsed the lot.
// The header is the list now; this is the how.

namespace ctbrowser::shell {

void browser::use_renderer(renderer r) {
    renderer_ = std::move(r);
    mark(dirty::paint); // the new renderer has no tiles
}

void browser::load_html(std::string_view html) {
    source_html_ = html; // what location.reload() re-runs
    // Both the document and the cascade are rebuilt. Keeping the old style
    // engine would accumulate every page's <style> rules across navigations,
    // which shows up as the previous page bleeding into the next one.
    reset_document();
    const parse_result parsed = parse_html(*doc_, html);
    title_ = extract_title();
    scroll_y_ = 0;
    author_sheet_loaded_ = false;
    load_inline_styles();
    // Images are resolved BEFORE layout, because an <img> with no width
    // attribute takes its size from the decoded bitmap and layout has no
    // way to ask. The page's @font-face files, for the same reason: layout
    // measures with them.
    load_images();
    // AFTER load_images, which clears the store before walking for <img>. An
    // inline <svg>'s source came from the parse rather than from a file, but
    // from here on the two are the same thing: a graphic to rasterise at
    // whatever size its box turns out to be.
    for (const auto & [id, source] : parsed.svg_sources) { svg_.set_source(id, source); }
    load_page_fonts();
    mark(dirty::everything);
    run_scripts();
}

bool browser::has_selection() const noexcept {
    return selection_anchor_.node && selection_focus_.node &&
           !(selection_anchor_.node == selection_focus_.node &&
             selection_anchor_.code_point == selection_focus_.code_point);
}

void browser::clear_selection() {
    if (!selection_anchor_.node && !selection_focus_.node) { return; }
    selection_anchor_ = {};
    selection_focus_ = {};
    mark(dirty::paint);
}

std::string browser::selected_text() {
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

void browser::define_native(std::string name, script::native_fn fn) {
    for (auto & [existing, handler] : embedder_natives_) {
        if (existing == name) {
            handler = std::move(fn);
            return;
        }
    }
    embedder_natives_.emplace_back(std::move(name), std::move(fn));
    if (script_) { install_embedder_natives(); }
}

bool browser::use_real_fonts(std::string_view directory) {
#if CTBROWSER_WITH_TTF
    auto backend = std::make_unique<ctbrowser::raster::ttf_backend>();
    if (!backend->ok()) { return false; }
    // The baked-in faces first, if this build has any. They go into the same
    // registry the loop below reads, under the same names, so nothing after
    // this point knows or cares whether a face came from the binary or the
    // disk - which is what the registry-before-filesystem order was always
    // for. A build without them registers nothing and the loop reads the
    // directory, exactly as before.
    (void)register_embedded_fonts(assets_, directory);
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
    // The canvas measures and draws its own text, so it needs the same backend
    // - otherwise a page's canvas keeps the bitmap font while the document
    // around it switches to real faces.
    canvases_.set_fonts(fonts_);
    // Everything measured so far was measured with the other font.
    mark(dirty::everything);
    return true;
#else
    (void)directory;
    return false;
#endif
}

void browser::allow_network(bool allowed) {
    network_allowed_ = allowed;
    if (bindings_) { bindings_->allow_network(allowed); }
}

bool browser::text_input(std::string_view text) {
    control_state * control = editable_focus();
    if (control == nullptr || text.empty()) { return false; }
    // This path is for PRINTABLE text; a control character is a KEY, and one
    // arriving here would be inserted literally - a Tab dropped into the very
    // field Tab is supposed to leave. SDL never sends one, so this is a guard
    // on the headless path, which is exactly where a test driving Tab would
    // produce it by accident. Bytes >= 0x80 are left alone: they are UTF-8
    // continuation bytes, not controls.
    std::string printable;
    printable.reserve(text.size());
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        // '\n' survives: a textarea wants it, and insert_text is where a
        // newline in a pasted value has to land.
        if (byte == '\n' || byte >= 0x20) {
            if (byte != 0x7F) { printable.push_back(c); }
        }
    }
    if (printable.empty()) { return false; }
    forms_.insert_text(*control, printable);
    // Typing past the last visible row of a textarea scrolls it, or the caret
    // walks off a box that cannot grow to follow it.
    reveal_caret(focused_, *control, kind_of(doc_->read(), focused_));
    restart_caret_blink(); // a caret that blinks out under what you typed looks broken
    bindings_->dispatch("input", focused_);
    mark(dirty::paint);
    return true;
}

bool browser::run_script(std::string_view source) {
    if (!script_) { return false; }
    script::program compiled = script::compiler::compile(std::string{source});
    if (!compiled.ok) {
        script_error_ = compiled.error;
        return false;
    }
    const script::run_result result = script_->run(compiled);
    // CLEARED ON SUCCESS, not only set on failure. It was only ever assigned,
    // so one broken script made every later good one report that same error for
    // the rest of the page's life - and callers use this to decide whether the
    // script ran at all.
    script_error_ = result.ok ? std::string{} : result.error;
    return result.ok;
}

std::size_t browser::live_script_objects() const {
    return script_ ? script_->live_objects() : 0;
}

const control_state * browser::control_state_of(node_id id) {
    if (!id) { return nullptr; }
    const auto txn = doc_->read();
    if (!txn.contains(id) || kind_of(txn, id) == control_kind::none) { return nullptr; }
    return &forms_.state_of(txn, atoms_, id);
}

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

void browser::reload() {
    const std::string source = source_html_;
    load_html(source); // by value: load_html clears source_html_'s referent
}

void browser::set_alert_hook(std::function<void(const std::string &)> hook) {
    alert_hook_ = std::move(hook);
}

void browser::set_navigate_hook(std::function<void(const std::string &)> hook) {
    navigate_hook_ = std::move(hook);
}

void browser::resize(int width, int height) {
    if (width == options_.width && height == options_.height) { return; }
    options_.width = std::max(1, width);
    options_.height = std::max(1, height);
    // RESIZE the renderer, do not replace it. Replacing it built a fresh
    // software backend, so an app that chose the GPU silently dropped to
    // software on its first window resize and never came back.
    renderer_.resize(options_.width, options_.height);
    mark(dirty::layout);
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

std::string_view browser::cursor_at(float x, float y) {
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

bool browser::on_scrollbar(float x) const noexcept {
    return max_scroll() > 0 && options_.scrollbar_width > 0 &&
           x >= static_cast<float>(options_.width) - options_.scrollbar_width;
}

float browser::max_scroll() const noexcept {
    return std::max(0.0f, content_height_ - static_cast<float>(options_.height));
}

bool browser::handle(const input_event & event) {
    // Remembered for anything that has to keep aiming at the pointer after the
    // events stop - the drag auto-scroll, which runs off tick() and is handed
    // no coordinates of its own.
    if (event.kind == input_kind::mouse_move || event.kind == input_kind::mouse_down ||
        event.kind == input_kind::mouse_up ||
        (event.kind == input_kind::wheel && event.has_pointer)) {
        pointer_ = point{event.x, event.y};
        have_pointer_ = true;
    }
    switch (event.kind) {
    case input_kind::wheel:
        // The field under the pointer takes the notch first, and only what it
        // cannot use falls through to the page - which is what makes a textarea
        // at its last line stop swallowing the wheel.
        if (scroll_field_under(event)) { return true; }
        scroll_by(-event.wheel_y * options_.wheel_step);
        return true;
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
            const std::size_t at = offset_at_point(field_selecting_, state, kind, event.x, event.y);
            if (at != state.caret) {
                state.caret = at;
                // Rule 1 while the pointer is still INSIDE the box: the caret
                // leads and the view follows it. Once the pointer leaves, rule
                // 3 takes over in tick() and the view leads instead - the two
                // must never both run, or they oscillate.
                reveal_caret(field_selecting_, state, kind);
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
            if (!bindings_->dispatch_mouse("contextmenu", target ? target : body_node(), event)) {
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
                // A click that reached the field through its LABEL focuses it
                // and stops there. The pointer is over the label's text, not
                // over any glyph of the value, so mapping it through
                // offset_at_point would drop the caret at whichever end the
                // label sits on and begin a drag-selection from there.
                if (!via_label(pressed_)) {
                    control_state & state = forms_.state_of(txn, atoms_, control);
                    state.caret = offset_at_point(control, state, kind, event.x, event.y);
                    state.selection = state.caret;
                    field_selecting_ = control;
                    // Clicking near an edge of a scrolled field nudges the view
                    // so the caret you just placed is actually on screen.
                    reveal_caret(control, state, kind);
                }
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

node_id browser::hit_test(float x, float y) const {
    return deepest_at(fragments_, x, y + scroll_y_, 0, 0);
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
    if (dirty_ >= dirty::styles) { resolve_styles(); }
    if (dirty_ >= dirty::layout) { run_layout(); }
    if (dirty_ >= dirty::paint) { record(); }
    dirty_ = dirty::nothing;
    ++frames_;
    return ctbrowser::raster::draw(renderer_, layers_, pool, options_.tile_extent, viewport());
}

rect browser::viewport() const noexcept {
    return rect{0, 0, static_cast<float>(options_.width), static_cast<float>(options_.height)};
}

void browser::load_inline_styles() {
    if (author_sheet_loaded_) { return; }
    const auto txn = doc_->read();
    const atom style_tag = atoms_.intern_lower("style");
    std::string css;
    const auto walk = [&](auto && self, node_id at) -> void {
        // HTML <style> ONLY. An SVG carries its own <style>, scoped to the
        // graphic, and it interns to the same atom - so without the namespace
        // check an `<svg><style>p { color: red }</style>` restyles every
        // paragraph on the page.
        if (txn.tag(at).value_or(atom{}) == style_tag &&
            txn.element_ns(at) == ctbrowser::node_ns::html) {
            for (const node_id child : txn.children(at)) { css += txn.text(child); }
            css += '\n';
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    if (!css.empty()) { styles_->add_sheet(css, ctbrowser::style::author_origin); }
    author_sheet_loaded_ = true;
}

std::string browser::extract_title() {
    const auto txn = doc_->read();
    const atom title_tag = atoms_.intern_lower("title");
    std::string found;
    const auto walk = [&](auto && self, node_id at) -> void {
        if (!found.empty()) { return; }
        // HTML <title> ONLY. In SVG a <title> is the graphic's accessible name
        // - a tooltip - and it is extremely common; interning to the same atom,
        // it would otherwise become the WINDOW title of any page whose own
        // <title> is missing or comes later.
        if (txn.tag(at).value_or(atom{}) == title_tag &&
            txn.element_ns(at) == ctbrowser::node_ns::html) {
            for (const node_id child : txn.children(at)) { found += txn.text(child); }
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return found;
}

void browser::resolve_styles() {
    const auto txn = doc_->read();
    resolved_ = styles_->resolve_all(txn);
}

void browser::run_scripts() {
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
    // The in-flight drag goes with the document. These are HANDLES into a slab
    // that has just been rebuilt, and forms_.state_of would happily seed fresh
    // state for a stale one - a page that reloads mid-drag would otherwise keep
    // auto-scrolling a field that no longer exists.
    field_selecting_ = node_id{};
    pressed_ = node_id{};
    selecting_ = false;
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
            // HTML <script> ONLY, for the same reason as <style> and <title>
            // above: SVG has a <script> of its own and it interns to the same
            // atom, so without this a graphic's script would run as the page's.
            if (txn.tag(at).value_or(atom{}) == script_tag &&
                txn.element_ns(at) == ctbrowser::node_ns::html) {
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

const ctbrowser::raster::font_backend & browser::fonts() const {
    return fonts_ != nullptr ? *fonts_ : ctbrowser::raster::font8x8_fonts();
}

ctbrowser::layout::measure_text_fn browser::measure() const {
    return metrics_for(fonts());
}

void browser::load_page_fonts() {
#if CTBROWSER_WITH_TTF
    if (!ttf_) { return; }
    for (const auto & face : styles_->page_fonts()) {
        const std::vector<std::byte> bytes = assets_.load(face.source);
        if (!bytes.empty()) { (void)ttf_->add_face(face.family, face.bold, face.italic, bytes); }
    }
#endif
}

// Whether these bytes are SVG. Content first, name second, because a file
// served as `chart` is still an SVG and a `.svg` that turns out to be a PNG is
// not - and because this decides which of two rasterisers sees the bytes, which
// is not a decision to make on a file extension alone.
namespace {

[[nodiscard]] bool looks_like_svg(std::string_view bytes) {
    std::size_t at = 0;
    // A BOM, then whitespace. An SVG written by a tool that emits UTF-8 BOMs is
    // otherwise not recognised, and the failure - a blank box - says nothing.
    if (bytes.size() >= 3 && bytes.compare(0, 3, "\xEF\xBB\xBF") == 0) { at = 3; }
    while (at < bytes.size() &&
           (bytes[at] == ' ' || bytes[at] == '\t' || bytes[at] == '\n' || bytes[at] == '\r')) {
        ++at;
    }
    const std::string_view rest = bytes.substr(at);
    // `<?xml` counts: the root <svg> is then a line or two further in, past a
    // declaration and possibly a DOCTYPE, and plutosvg reads all of it.
    return rest.starts_with("<svg") || rest.starts_with("<?xml") ||
           rest.starts_with("<!DOCTYPE svg");
}

} // namespace

void browser::load_images() {
    images_by_node_.clear();
    // HERE, not beside canvases_.clear() in run_scripts. A canvas is created BY
    // a script, so clearing before scripts run is right for one; an SVG source
    // is found by this walk, which happens BEFORE scripts, so clearing there
    // deletes what was just loaded. Both are per-document - they just have
    // different producers, and this is the one that resets with its own writer.
    svg_.clear();
    const auto txn = doc_->read();
    const atom img_tag = atoms_.intern_lower("img");
    const atom src_attribute = atoms_.intern("src");
    const auto walk = [&](auto && self, node_id at) -> void {
        if (txn.tag(at).value_or(atom{}) == img_tag) {
            const std::string_view src = txn.attribute_value(at, src_attribute);
            if (!src.empty()) {
                // SVG IS INTERCEPTED BEFORE image_store EVER SEES IT. Not a
                // decoder plugged into image_store: its cache is keyed by name
                // and decodes once, which cannot produce a raster at the size
                // the box turns out to be. It also means SDL3_image's own SVG
                // loader is never reached, so both platforms rasterise through
                // plutosvg and one golden serves both.
                const std::vector<std::byte> bytes = assets_.load(src);
                const std::string_view text{reinterpret_cast<const char *>(bytes.data()),
                                            bytes.size()};
                if (!bytes.empty() && looks_like_svg(text)) {
                    svg_.set_source(at, std::string{text});
                } else if (auto pixels = images_.load(assets_, src)) {
                    images_by_node_.emplace_back(at, std::move(pixels));
                }
            }
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
}

std::shared_ptr<const ctbrowser::paint::bitmap> browser::image_of(node_id id) const {
    for (const auto & [at, pixels] : images_by_node_) {
        if (at == id) { return pixels; }
    }
    return nullptr;
}

void browser::install_embedder_natives() {
    for (const auto & [name, fn] : embedder_natives_) { script_->define_native(name, fn); }
}

void browser::run_layout() {
    ++layouts_;
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
    fragments_ = eng.run(boxes_, static_cast<float>(options_.width));
    content_height_ = fragments_.bounds.height;

    // TWO PASSES when the page overflows: the scrollbar takes width away
    // from the content, and content laid out at the full width would run
    // under it. This terminates because narrowing a page can only make it
    // TALLER, so a page that overflowed still overflows - it never
    // oscillates between needing a bar and not.
    if (options_.scrollbar_width > 0 && content_height_ > static_cast<float>(options_.height)) {
        fragments_ = eng.run(boxes_, static_cast<float>(options_.width) - options_.scrollbar_width);
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
    recorder_.paint_replaced =
        [this](node_id id, const rect & box, const ctbrowser::style::computed_style_ptr & style,
               ctbrowser::paint::display_list & into) { paint_replaced(id, box, style, into); };
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

void browser::record_context_menu() {
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

rect browser::menu_box() const {
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

bool browser::handle_menu_press(const input_event & event) {
    const rect box = menu_box();
    menu_open_ = false;
    mark(dirty::paint);
    if (event.x < box.x || event.x >= box.right() || event.y < box.y || event.y >= box.bottom()) {
        return true; // click-away: closed, and the page does not see it
    }
    const auto index = static_cast<std::size_t>((event.y - box.y) / menu_row);
    if (index < std::size(menu_items)) { run_clipboard_verb(menu_items[index]); }
    return true;
}

void browser::record_select_popup() {
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
        if (options[i] == chosen) { list.fill(item, color{ctbrowser::style::ua_widget_accent}); }
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

std::vector<std::string> browser::option_labels(const read_txn & txn, node_id select) {
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

rect browser::viewport_box_of(node_id id) const {
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

void browser::record_scrollbar() {
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

rect browser::scrollbar_thumb() const {
    const float width = options_.scrollbar_width;
    const float height = static_cast<float>(options_.height);
    const float left = static_cast<float>(options_.width) - width;
    const float visible = content_height_ > 0 ? height / content_height_ : 1;
    const float thumb_height = std::max(24.0f, height * std::min(1.0f, visible));
    const float travel = height - thumb_height;
    const float progress = max_scroll() > 0 ? scroll_y_ / max_scroll() : 0;
    return rect{left + 1, progress * travel, width - 2, thumb_height};
}

float browser::baseline_inset(const rect & box, float size) {
    return std::max(0.0f, (box.height - size * 1.25f) / 2);
}

bool browser::is_disabled(node_id id) {
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

bool browser::is_password(node_id id) {
    const auto txn = doc_->read();
    return txn.attribute_value(id, atoms_.intern("type")) == "password";
}

std::string browser::masked_text(std::string_view text) {
    std::string out;
    for (std::size_t at = 0; at < text.size(); at = next_code_point(text, at)) {
        out += "\xE2\x80\xA2"; // U+2022 BULLET
    }
    return out;
}

std::string browser::shown(std::string_view text, bool masked) {
    return masked ? masked_text(text) : std::string{text};
}

std::size_t browser::next_code_point(std::string_view text, std::size_t at) {
    std::size_t next = at + 1;
    while (next < text.size() && (static_cast<unsigned char>(text[next]) & 0xC0) == 0x80) {
        ++next;
    }
    return next;
}

color browser::text_colour(const ctbrowser::style::computed_style_ptr & style) {
    if (style) {
        if (const auto c = ctbrowser::paint::parse_color(style->get(atoms_.intern("color")))) {
            return *c;
        }
    }
    return color::rgba(0, 0, 0);
}

ctbrowser::layout::text_face browser::face_of(node_id id) const {
    const layout::box_node * found = find_box(boxes_, id);
    return found == nullptr ? ctbrowser::layout::text_face{} : found->face;
}

ctbrowser::paint::font_face browser::paint_face_of(node_id id) const {
    const ctbrowser::layout::text_face face = face_of(id);
    return ctbrowser::paint::font_face{face.family, face.bold, face.italic};
}

float browser::font_size_of(node_id id) const {
    const layout::box_node * found = find_box(boxes_, id);
    return found == nullptr ? 16.0f : found->font_size;
}

bool browser::set_hover(node_id at) {
    if (at == hovered_) { return false; }
    bool changed = set_state(hovered_, state_hover, false);
    hovered_ = at;
    changed = set_state(hovered_, state_hover, true) || changed;
    return changed;
}

bool browser::set_state(node_id at, std::uint32_t bit, bool on) {
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

std::string browser::selected_option(const read_txn & txn, node_id id) {
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

bool browser::handle_popup_press(const input_event & event) {
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
    if (event.x >= box.x && event.x < box.right() && event.y >= box.y && event.y < box.bottom()) {
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

std::vector<browser::text_run> browser::text_runs() const {
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

browser::text_position browser::position_at(float x, float y) {
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

std::size_t browser::code_point_at(const text_run & run, float x) const {
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

std::pair<std::size_t, std::size_t> browser::selected_range(const text_run & run) {
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
    const std::size_t to = run.order == last_order ? std::min(run_end, last.code_point) : run_end;
    return {std::min(from, to), to};
}

rect browser::highlight_for(const ctbrowser::layout::fragment & f) {
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

void browser::run_clipboard_verb(std::string_view verb) {
    clipboard_verb(verb);
    // EVERY verb that acts on a field moves its caret - Select All to the end
    // of the value, Paste to past what it inserted, Cut back to where the
    // deletion started - and none of them did anything about the view, so a
    // paste into a scrolled field left the caret off screen. One place rather
    // than three returns, so a verb added later cannot forget.
    if (control_state * control = editable_focus(); control != nullptr) {
        reveal_caret(focused_, *control, kind_of(doc_->read(), focused_));
    }
}

void browser::clipboard_verb(std::string_view verb) {
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

bool browser::handle_key(const input_event & event) {
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

    // TAB moves focus, and it is a DEFAULT ACTION like every other key here -
    // the page already saw this keydown above, and a preventDefault returned
    // before we got here.
    //
    // Before edit_key deliberately: edit_key has no "Tab" arm today, but one
    // added later would silently eat the key in the very control Tab exists to
    // LEAVE. It returns true whatever focus_next decides, so Tab can never fall
    // through to the page-scrolling keys below.
    if (event.key == "Tab") {
        (void)focus_next(event.shift);
        return true;
    }

    if (control_state * control = editable_focus(); control != nullptr) {
        if (edit_key(*control, event)) {
            // One place rather than an arm-by-arm sprinkle: every editing key
            // either moves the caret or changes the value, and both can put it
            // outside a textarea's visible rows.
            reveal_caret(focused_, *control, kind_of(doc_->read(), focused_));
            return true;
        }
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

bool browser::dispatch_key(std::string_view type, const input_event & event) {
    if (!bindings_) { return false; }
    // At the focused element, so a keystroke in a text field is that
    // field's event; at the body otherwise, which is where a game listens.
    const node_id target = focused_ ? focused_ : body_node();
    return bindings_->dispatch_key(type, target, event);
}

bool browser::dispatch_mouse(std::string_view type, node_id target, const input_event & event) {
    if (!bindings_) { return false; }
    return bindings_->dispatch_mouse(type, target ? target : body_node(), event);
}

node_id browser::body_node() {
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

void browser::reset_document() {
    doc_ = std::make_unique<document>(atoms_);
    styles_ = std::make_unique<ctbrowser::style::engine>(atoms_);
    styles_->add_sheet(ctbrowser::style::ua_css, ctbrowser::style::ua_origin);
    resolved_.clear();
}

bool browser::has_editable_focus() {
    if (!focused_) { return false; }
    const auto txn = doc_->read();
    if (!txn.contains(focused_)) { return false; }
    const control_kind kind = kind_of(txn, focused_);
    return kind == control_kind::text || kind == control_kind::textarea;
}

control_state * browser::editable_focus() {
    if (!focused_) { return nullptr; }
    const auto txn = doc_->read();
    if (!txn.contains(focused_)) { return nullptr; }
    const control_kind kind = kind_of(txn, focused_);
    if (kind != control_kind::text && kind != control_kind::textarea) { return nullptr; }
    return &forms_.state_of(txn, atoms_, focused_);
}

control_kind browser::kind_of(const read_txn & txn, node_id id) {
    return control_kind_of(atoms_.text(txn.tag(id).value_or(atom{})),
                           txn.attribute_value(id, atoms_.intern("type")));
}

node_id browser::node_by_id(const read_txn & txn, std::string_view want) {
    if (want.empty()) { return node_id{}; }
    const atom key = atoms_.intern("id");
    node_id found{};
    const auto walk = [&](auto && self, node_id at) -> void {
        if (found) { return; }
        if (txn.attribute_value(at, key) == want) {
            found = at;
            return;
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return found;
}

node_id browser::labelled_control(const read_txn & txn, node_id from) {
    if (!from) { return node_id{}; }
    const atom label_tag = atoms_.intern_lower("label");
    node_id label;
    for (node_id at = from; at; at = txn.parent(at)) {
        if (txn.tag(at).value_or(atom{}) == label_tag) {
            label = at;
            break;
        }
    }
    if (!label) { return node_id{}; }

    // `for` wins when it is there, even if it names nothing - an explicit
    // reference that does not resolve labels NOTHING, rather than quietly
    // falling back to whatever the label happens to contain.
    const std::string_view target = txn.attribute_value(label, atoms_.intern("for"));
    if (!target.empty()) {
        const node_id named = node_by_id(txn, target);
        return kind_of(txn, named) != control_kind::none ? named : node_id{};
    }

    // Otherwise the first labelable DESCENDANT, in document order. The label
    // itself cannot be the answer - a <label> is not a control - so the walk
    // starts at its children.
    node_id found{};
    const auto walk = [&](auto && self, node_id at) -> void {
        if (found) { return; }
        if (at != label && kind_of(txn, at) != control_kind::none) {
            found = at;
            return;
        }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, label);
    return found;
}

node_id browser::control_ancestor(node_id from) {
    if (!from) { return node_id{}; }
    const auto txn = doc_->read();
    for (node_id at = from; at; at = txn.parent(at)) {
        if (kind_of(txn, at) != control_kind::none) { return at; }
    }
    // Nothing up the tree IS a control, so the click may still be on a label
    // FOR one. Second, not first: a press on the control inside a label finds
    // it on the walk above and never reaches here, so a click can never resolve
    // twice and toggle a checkbox back off again.
    return labelled_control(txn, from);
}

bool browser::via_label(node_id from) {
    if (!from) { return false; }
    const auto txn = doc_->read();
    for (node_id at = from; at; at = txn.parent(at)) {
        if (kind_of(txn, at) != control_kind::none) { return false; }
    }
    return static_cast<bool>(labelled_control(txn, from));
}

bool browser::focus(node_id id) {
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
    // Told to the bindings BEFORE the event fires, so a `focus` listener asking
    // document.activeElement gets the element it was just handed rather than
    // the one that had focus a moment ago.
    bindings_->observe_focus(focused_);
    restart_caret_blink(); // a field you just clicked into shows its caret at once
    if (focused_) {
        (void)set_state(focused_, state_focus, true);
        bindings_->dispatch("focus", focused_);
    }
    mark(dirty::paint);
    return true;
}

bool browser::is_focusable(const read_txn & txn, node_id id) {
    // Every focusable kind is exactly "is a control", so there is no per-kind
    // list to keep in step with control_kind_of - and adding one would be the
    // obvious wrong edit here.
    if (kind_of(txn, id) == control_kind::none) { return false; }
    if (is_disabled(id)) { return false; }
    return !viewport_box_of(id).empty();
}

std::vector<node_id> browser::focusable_controls() {
    const auto txn = doc_->read();
    std::vector<node_id> out;
    const auto walk = [&](auto && self, node_id at) -> void {
        if (is_focusable(txn, at)) { out.push_back(at); }
        for (const node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    return out;
}

browser::autoscroll_state browser::autoscroll_now() {
    autoscroll_state out;
    if (!field_selecting_ || !have_pointer_ || options_.autoscroll_ms <= 0) { return out; }
    const rect box = viewport_box_of(field_selecting_);
    if (box.empty()) { return out; }
    const auto txn = doc_->read();
    if (!txn.contains(field_selecting_)) { return out; }
    const control_kind kind = kind_of(txn, field_selecting_);
    control_state & state = forms_.state_of(txn, atoms_, field_selecting_);
    const field_layout geometry = layout_of_field(box, field_selecting_, state, kind);

    // How far outside, per axis. Inside on an axis means no motion on it, so a
    // drag straight down does not also creep sideways.
    const float below = pointer_.y > box.bottom() ? pointer_.y - box.bottom()
                        : pointer_.y < box.y      ? pointer_.y - box.y
                                                  : 0;
    const float beside = pointer_.x > box.right() ? pointer_.x - box.right()
                         : pointer_.x < box.x     ? pointer_.x - box.x
                                                  : 0;

    // ...and only where the view can still MOVE that way. Without this the
    // wakeup below is scheduled forever and an idle loop with the pointer
    // parked below a fully-scrolled field spins at the step interval.
    const std::size_t most = geometry.lines.size() > geometry.visible_lines
                                 ? geometry.lines.size() - geometry.visible_lines
                                 : 0;
    if ((below > 0 && geometry.scroll_line < most) || (below < 0 && geometry.scroll_line > 0)) {
        out.below = below;
    }
    const float widest = std::max(0.0f, geometry.content_width + 1 - geometry.inner.width);
    if ((beside > 0 && geometry.scroll_x < widest) || (beside < 0 && geometry.scroll_x > 0)) {
        out.beside = beside;
    }
    if (out.below != 0 || out.beside != 0) { out.field = field_selecting_; }
    return out;
}

double browser::autoscroll_interval_ms(float distance) const {
    const float d = std::fabs(distance);
    const double ramp = options_.autoscroll_ramp_px > 0
                            ? 1.0 + static_cast<double>(d) / options_.autoscroll_ramp_px
                            : 1.0;
    return std::max(options_.autoscroll_min_ms, options_.autoscroll_ms / ramp);
}

void browser::autoscroll_step(const autoscroll_state & at) {
    const rect box = viewport_box_of(at.field);
    if (box.empty()) { return; }
    const auto txn = doc_->read();
    const control_kind kind = kind_of(txn, at.field);
    control_state & state = forms_.state_of(txn, atoms_, at.field);
    {
        const field_layout geometry = layout_of_field(box, at.field, state, kind);
        // THE VIEW MOVES FIRST. One line, one character - the smallest step
        // there is, so the rate alone decides how fast it goes.
        if (at.below > 0) {
            state.scroll_line = geometry.scroll_line + 1;
        } else if (at.below < 0 && geometry.scroll_line > 0) {
            state.scroll_line = geometry.scroll_line - 1;
        }
        if (at.beside != 0) {
            // A character's worth, measured rather than assumed: "one column"
            // means nothing in a proportional face.
            const float step = measure()("n", geometry.size, geometry.metrics_face);
            state.scroll_x = geometry.scroll_x + (at.beside > 0 ? step : -step);
        }
    }
    // ...and THEN the caret follows it, derived from where the pointer actually
    // is against the view we just moved. Not reveal_caret, which is the inverse
    // and would drag the view back to the caret it is trying to lead.
    const std::size_t caret = offset_at_point(at.field, state, kind, pointer_.x, pointer_.y);
    if (caret != state.caret) { state.caret = caret; }
    restart_caret_blink();
    mark(dirty::paint);
}

bool browser::scroll_field_under(const input_event & event) {
    if (!event.has_pointer || event.wheel_y == 0) { return false; }
    const node_id under = control_ancestor(hit_test(event.x, event.y));
    if (!under) { return false; }
    const auto txn = doc_->read();
    const control_kind kind = kind_of(txn, under);
    // Only a textarea scrolls vertically. A single-line field's overflow is
    // sideways, and a wheel is not how anyone asks for that.
    if (kind != control_kind::textarea) { return false; }

    control_state & state = forms_.state_of(txn, atoms_, under);
    const rect box = viewport_box_of(under);
    if (box.empty()) { return false; }
    const field_layout geometry = layout_of_field(box, under, state, kind);
    const std::size_t most = geometry.lines.size() > geometry.visible_lines
                                 ? geometry.lines.size() - geometry.visible_lines
                                 : 0;
    if (most == 0) { return false; } // nothing to scroll: the page takes it

    // Negative wheel_y is towards the user, which is down the document.
    const auto step = static_cast<std::ptrdiff_t>(options_.wheel_lines);
    const auto delta = event.wheel_y > 0 ? -step : step;
    const auto want = static_cast<std::ptrdiff_t>(geometry.scroll_line) + delta;
    const auto next = static_cast<std::size_t>(
        std::clamp<std::ptrdiff_t>(want, 0, static_cast<std::ptrdiff_t>(most)));
    // ALREADY AT THAT END: the notch is not ours, so the page gets it. Without
    // this a textarea scrolled to its bottom swallows every further notch and
    // the page appears stuck.
    if (next == geometry.scroll_line) { return false; }

    state.scroll_line = next;
    mark(dirty::paint);
    return true;
}

bool browser::focus_next(bool backwards) {
    const std::vector<node_id> order = focusable_controls();
    if (order.empty()) { return false; }
    const auto at = std::find(order.begin(), order.end(), focused_);
    std::size_t next = 0;
    if (at == order.end()) {
        // Nothing focused, or focus is on something that is not a control:
        // Tab starts at the top of the document and Shift+Tab at the bottom.
        next = backwards ? order.size() - 1 : 0;
    } else {
        const auto here = static_cast<std::size_t>(at - order.begin());
        next = backwards ? (here + order.size() - 1) % order.size() : (here + 1) % order.size();
    }
    // Discarded: focus() reports "nothing changed" on a page with one control,
    // and that must not read as "Tab did nothing" - the key was still handled.
    (void)focus(order[next]);
    return true;
}

bool browser::edit_key(control_state & control, const input_event & event) {
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
    // PAGE UP AND DOWN BELONG TO THE FIELD when a multi-line one has focus.
    // They fell through to the page-scrolling keys, so paging inside a textarea
    // scrolled the document out from under it.
    if (multiline && (key == "PageUp" || key == "PageDown")) {
        const rect box = viewport_box_of(focused_);
        int lines = 1;
        if (!box.empty()) {
            const field_layout geometry = layout_of_field(box, focused_, control, kind);
            lines = std::max(1, static_cast<int>(geometry.visible_lines) - 1);
        }
        const int direction = key == "PageDown" ? 1 : -1;
        bool any = false;
        for (int i = 0; i < lines; ++i) {
            if (!move_caret_by_line(focused_, control, kind, direction, event.shift)) { break; }
            any = true;
        }
        // Consumed either way: a caret already at the top or bottom of the
        // value must not hand the key to the page and scroll the document.
        (void)moved(any);
        return true;
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

bool browser::edited(bool changed) {
    if (!changed) { return false; }
    restart_caret_blink();
    bindings_->dispatch("input", focused_);
    mark(dirty::paint);
    return true;
}

bool browser::moved(bool changed) {
    restart_caret_blink();
    if (changed) { mark(dirty::paint); }
    return true; // the key was consumed either way - it must not scroll the page
}

void browser::activate(node_id target) {
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

bool browser::toggle_details(node_id target) {
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

bool browser::follow_link(node_id target) {
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

void browser::scroll_to_fragment(std::string_view id) {
    if (id.empty()) { return; }
    node_id target;
    {
        const auto txn = doc_->read();
        target = node_by_id(txn, id);
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

void browser::submit(node_id form) {
    if (!form) { return; }
    if (bindings_->dispatch("submit", form)) { return; } // cancelled
    const auto txn = doc_->read();
    last_submission_ = forms_.form_data(txn, atoms_, form);
}

} // namespace ctbrowser::shell
