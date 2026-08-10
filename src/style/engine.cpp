#include <ctbrowser/style/engine.hpp>

// engine: the method bodies.
// The header says what these do; this says how.

namespace ctbrowser::style {

bool engine::set_state(node_id id, std::uint32_t bits, bool on) {
    if (!id || bits == 0) { return false; }
    const std::uint64_t key = key_of(id);
    const auto it = states_.find(key);
    const std::uint32_t before = it == states_.end() ? 0u : it->second;
    const std::uint32_t after = on ? (before | bits) : (before & ~bits);
    if (after == before) { return false; }
    if (after == 0) {
        states_.erase(key);
    } else {
        states_[key] = after;
    }
    return true;
}

std::uint32_t engine::state_of(node_id id) const {
    const auto it = states_.find(key_of(id));
    return it == states_.end() ? 0u : it->second;
}

void engine::add_sheet(std::string_view css, std::uint8_t origin) {
    const css::stylesheet sheet = css::parse_stylesheet(css, *atoms_);
    for (const css::font_face & face : sheet.font_faces) {
        const auto value_of = [&](std::string_view property) -> std::string_view {
            const atom want = atoms_->intern_lower(property);
            for (const css::raw_declaration & d : sheet.declarations_of(face)) {
                if (d.property == want) { return sheet.text_of(d); }
            }
            return {};
        };
        page_font entry;
        // UNQUOTED here: `font-family: 'Press Start 2P'` arrives with its quotes
        // on, so registering the name as it comes back files the face under a name
        // no element can ever ask for.
        entry.family = std::string{unquoted(value_of("font-family"))};
        entry.source = std::string{url_of(value_of("src"))};
        const std::string_view weight = value_of("font-weight");
        entry.bold = weight == "bold" || weight == "700" || weight == "800" || weight == "900" ||
                     weight == "600";
        const std::string_view style = value_of("font-style");
        entry.italic = style == "italic" || style == "oblique";
        if (!entry.family.empty() && !entry.source.empty()) { fonts_.push_back(std::move(entry)); }
    }

    // ONE COMPILED SELECTOR PER SELECTOR, and one rule per (selector,
    // declaration). The old path compiled a selector once per DECLARATION - and
    // pushed it before deciding whether to keep it - so Bootstrap produced ~6,289
    // compiled selectors where ~2,965 exist, ~650 of them permanently retained for
    // rules that were then rejected. Hoisting the push out of the declaration loop
    // is the whole fix; the dead ones are simply never pushed.
    for (const css::raw_rule & r : sheet.rules) {
        for (const compiled_selector & compiled : sheet.selectors_of(r)) {
            if (compiled.parts.empty() || compiled.parts.front().never_matches) { continue; }
            selectors_.push_back(compiled);
            const std::uint32_t sel_index = static_cast<std::uint32_t>(selectors_.size() - 1);

            for (const css::raw_declaration & d : sheet.declarations_of(r)) {
                const std::string_view property = atoms_->text(d.property);
                const std::string_view value = sheet.text_of(d);
                const auto add = [&](std::string_view name, std::string_view text) {
                    declarations_.push_back(
                        declaration{atoms_->intern_lower(name), std::string{text}});
                    index_.add(selectors_[sel_index],
                               rule{sel_index, static_cast<std::uint32_t>(declarations_.size() - 1),
                                    d.order, origin, d.important});
                };
                const auto expanded = expand_shorthand(property, value);
                if (expanded.empty()) {
                    add(property, value);
                } else {
                    for (const auto & [name, text] : expanded) { add(name, text); }
                }
            }
        }
    }
}

element_facts engine::facts_of(const read_txn & txn, node_id id) const {
    element_facts f;
    f.tag = txn.tag(id).value_or(atom{});
    const std::string_view id_attr = txn.attribute_value(id, id_name());
    if (!id_attr.empty()) { f.id = atoms_->intern(id_attr); }
    split_classes(txn.attribute_value(id, class_name()), f.classes);
    f.states = state_of(id);
    return f;
}

style_map engine::resolve_all(const read_txn & txn) {
    style_map out;
    ancestor_filter ancestors;
    resolve_subtree(txn, txn.root(), ancestors, out);
    return out;
}

const engine::inline_block & engine::inline_style_of(const read_txn & txn, node_id id) {
    static const inline_block none;
    const std::string_view text = txn.attribute_value(id, style_name());
    if (text.empty()) { return none; }
    const auto cached = inline_cache_.find(text);
    if (cached != inline_cache_.end()) { return cached->second; }

    // A declaration list directly, no `*{...}` wrap. The wrap existed to avoid a
    // declaration splitter that peeled `!important` off and threw the flag away -
    // and that flag is the entire question of what a style attribute beats, which
    // docs/style-layout.md spells out. The flag survives here either way, so the
    // wrap is gone and a `}` inside an attribute value can no longer end the dummy
    // rule early.
    inline_block parsed;
    const css::stylesheet sheet = css::parse_declaration_list(text, *atoms_);
    for (const css::raw_declaration & d : sheet.declarations) {
        auto & into = d.important ? parsed.important : parsed.normal;
        const std::string_view property = atoms_->text(d.property);
        const std::string_view value = sheet.text_of(d);
        const auto expanded = expand_shorthand(property, value);
        if (expanded.empty()) {
            into.push_back(declaration{atoms_->intern_lower(property), std::string{value}});
        } else {
            for (const auto & [name, text_of] : expanded) {
                into.push_back(declaration{atoms_->intern_lower(name), std::string{text_of}});
            }
        }
    }
    return inline_cache_.emplace(std::string{text}, std::move(parsed)).first->second;
}

std::string_view engine::unquoted(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) { text.remove_suffix(1); }
    if (text.size() >= 2 && (text.front() == '"' || text.front() == '\'') &&
        text.back() == text.front()) {
        text = text.substr(1, text.size() - 2);
    }
    return text;
}

std::string_view engine::url_of(std::string_view src) {
    const std::size_t open = src.find("url(");
    if (open == std::string_view::npos) { return {}; }
    const std::size_t start = open + 4;
    const std::size_t close = src.find(')', start);
    if (close == std::string_view::npos) { return {}; }
    std::string_view inner = src.substr(start, close - start);
    while (!inner.empty() &&
           (inner.front() == ' ' || inner.front() == '"' || inner.front() == '\'')) {
        inner.remove_prefix(1);
    }
    while (!inner.empty() && (inner.back() == ' ' || inner.back() == '"' || inner.back() == '\'')) {
        inner.remove_suffix(1);
    }
    return inner;
}

void engine::split_classes(std::string_view list,
                           boost::container::small_vector<atom, 4> & out) const {
    std::size_t i = 0;
    while (i < list.size()) {
        while (i < list.size() && (list[i] == ' ' || list[i] == '\t' || list[i] == '\n')) { ++i; }
        const std::size_t start = i;
        while (i < list.size() && list[i] != ' ' && list[i] != '\t' && list[i] != '\n') { ++i; }
        if (i > start) { out.push_back(atoms_->intern(list.substr(start, i - start))); }
    }
}

bool engine::compound_matches(const element_facts & f, const compound & c) const {
    if (c.never_matches) { return false; }
    if (c.tag && c.tag != f.tag) { return false; }
    if (c.id && c.id != f.id) { return false; }
    for (const atom want : c.classes) {
        if (std::ranges::find(f.classes, want) == f.classes.end()) { return false; }
    }
    if ((c.states & f.states) != c.states) { return false; }
    return true;
}

} // namespace ctbrowser::style
