// browser - the chrome drawn over the page (context menu, select popup and
// scrollbar), and the form-control helpers the chrome and the input path
// share: disabled, password masking, faces, hover and state bits.
//
// One of ten files carved out of a 2,871-line Shell/browser.cpp on 2026-09-08.
// All are member functions of one class declared in
// include/ctbrowser/shell/browser.hpp; internal.hpp beside this carries the
// includes browser.cpp had, so every file sees exactly what it saw. Nothing
// about the public header changed.

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

} // namespace ctbrowser::shell
