// browser - the chrome drawn over the page (context menu, select popup and
// scrollbar), and the form-control helpers the chrome and the input path
// share: disabled, password masking, faces, hover and state bits.

#include "internal.hpp"

namespace ctbrowser::shell {

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
                  select_open_, face_of(select_open_));
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
    for (std::size_t at = 0; at < text.size(); at = form_store::next_code_point(text, at)) {
        out += "\xE2\x80\xA2"; // U+2022 BULLET
    }
    return out;
}

std::string browser::shown(std::string_view text, bool masked) {
    return masked ? masked_text(text) : std::string{text};
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

std::string browser::option_value_at(const read_txn & txn, node_id select, std::size_t index) {
    const atom option_tag = atoms_.intern_lower("option");
    std::size_t at = 0;
    for (const node_id child : txn.children(select)) {
        if (txn.tag(child).value_or(atom{}) != option_tag) { continue; }
        if (at++ == index) { return form_store::option_value(txn, atoms_, child); }
    }
    return {};
}

bool browser::paint_svg(node_id id, const rect & box, ctbrowser::paint::display_list & into) {
    const int width = std::max(1, static_cast<int>(std::lround(box.width)));
    const int height = std::max(1, static_cast<int>(std::lround(box.height)));
    auto pixels = svg_.pixels_for(id, width, height);
    if (!pixels) { return false; }
    into.draw_image(rect{box.x, box.y, static_cast<float>(width), static_cast<float>(height)},
                    std::move(pixels), id);
    return true;
}

void browser::paint_replaced(node_id id, const rect & box, const rect & content,
                             const ctbrowser::style::computed_style_ptr & style,
                             ctbrowser::paint::display_list & into) {
    const auto txn = doc_->read();
    const std::string_view tag = atoms_.text(txn.tag(id).value_or(atom{}));

    if (tag == "canvas") {
        if (auto pixels = canvases_.pixels_of(id)) { into.draw_image(box, std::move(pixels), id); }
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
        // it and a checkbox - the shape is what tells you one of them is
        // exclusive.
        into.fill_ellipse(box, frame, id);
        into.fill_ellipse(rect{box.x + 1, box.y + 1, box.width - 2, box.height - 2},
                          control.checked ? accent : field, id);
        if (control.checked) {
            const float inset = box.width * 0.3f;
            into.fill_ellipse(
                rect{box.x + inset, box.y + inset, box.width - 2 * inset, box.height - 2 * inset},
                color{ctbrowser::style::ua_widget_mark}, id);
        }
        break;
    }
    case control_kind::button: {
        // `<button>` does not reach here - it is not a replaced element -
        // so this is `<input type=button|submit|reset>`, whose border comes
        // from the UA sheet like every other control's.
        const std::string label = button_label(txn, id, control, type);
        if (!label.empty()) { label_text(box, label, id, style, into); }
        break;
    }
    case control_kind::select: {
        // NO FRAME OF ITS OWN. The UA sheet gives every control a real
        // border, so the recorder has already drawn one. Only the selected
        // option's text and the arrow are drawn here.
        const std::string label = selected_option(txn, id);
        if (!label.empty()) {
            const float size = font_size_of(id);
            into.text(rect{content.x, box.y + baseline_inset(box, size),
                           std::max(0.0f, content.width - 20), size * 1.25f},
                      label, size, control_text_colour(id, style), id, face_of(id));
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
        // NO BACKGROUND OR BORDER OF ITS OWN: both are the UA sheet's and
        // the recorder has already painted them. ONLY THE FOCUS RING, which
        // is a state indicator rather than a CSS border.
        if (focused) { outline(box, accent, into, id); }
        (void)field;
        paint_field_text(box, id, control, kind, style, focused, into);
        break;
    }
    case control_kind::none: break;
    }
}

void browser::check_mark(const rect & box, ctbrowser::paint::display_list & into, node_id id) {
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

void browser::label_text(const rect & box, const std::string & label, node_id id,
                         const ctbrowser::style::computed_style_ptr & style,
                         ctbrowser::paint::display_list & into) {
    const float size = font_size_of(id);
    const float width = measure()(label, size, face_of(id));
    const rect inner = content_box_of(id, box);
    const float x = inner.x + std::max(0.0f, (inner.width - width) / 2);
    into.text(rect{x, box.y + baseline_inset(box, size), box.width - (x - box.x), size * 1.25f},
              label, size, control_text_colour(id, style), id, face_of(id));
}

void browser::paint_field_text(const rect & box, node_id id, const control_state & control,
                               control_kind kind,
                               const ctbrowser::style::computed_style_ptr & style, bool focused,
                               ctbrowser::paint::display_list & into) {
    const field_layout geometry = layout_of_field(box, id, control, kind);
    const rect inner = geometry.inner;
    const float size = geometry.size;
    const float line_height = geometry.line_height;
    const ctbrowser::paint::font_face face = face_of(id);
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
                      shown(line, geometry.masked), size, control_text_colour(id, style), id, face);
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

std::string browser::button_label(const read_txn & txn, node_id id, const control_state & control,
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

} // namespace ctbrowser::shell
