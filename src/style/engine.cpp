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
    const ctcss::value_sheet sheet = ctcss::parse_value(css);
    for (const ctcss::value_sheet::font_face & face : sheet.font_faces) {
        page_font entry;
        // UNQUOTED here: ctcss leaves `font-family: 'Press Start 2P'` with
        // its quotes on, so registering the name as it comes back files the
        // face under a name no element can ever ask for.
        entry.family = std::string{unquoted(face.get("font-family"))};
        entry.source = std::string{url_of(face.get("src"))};
        const std::string_view weight = face.get("font-weight");
        entry.bold = weight == "bold" || weight == "700" || weight == "800" || weight == "900" ||
                     weight == "600";
        const std::string_view style = face.get("font-style");
        entry.italic = style == "italic" || style == "oblique";
        if (!entry.family.empty() && !entry.source.empty()) { fonts_.push_back(std::move(entry)); }
    }
    for (const ctcss::value_sheet::entry & e : sheet.entries) {
        if (e.selector < 0 || static_cast<std::size_t>(e.selector) >= sheet.selectors.size()) {
            continue;
        }
        const ctcss::value_sheet::selector & src =
            sheet.selectors[static_cast<std::size_t>(e.selector)];
        const std::uint32_t sel_index = compile_selector(src);
        if (selectors_[sel_index].parts.empty()) { continue; }
        if (selectors_[sel_index].parts.front().never_matches) { continue; }

        const auto add = [&](std::string_view property, std::string_view value) {
            declarations_.push_back(
                declaration{atoms_->intern_lower(property), std::string{value}});
            index_.add(selectors_[sel_index],
                       rule{sel_index, static_cast<std::uint32_t>(declarations_.size() - 1),
                            e.order, origin, e.important});
        };
        const auto expanded = expand_shorthand(e.property, e.value);
        if (expanded.empty()) {
            add(e.property, e.value);
        } else {
            for (const auto & [property, value] : expanded) { add(property, value); }
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

    // Through the SHEET parser, wrapped in a dummy rule, rather than
    // ctcss's declaration splitter: the latter peels `!important` off and
    // throws the flag away, and that flag is the entire question of what a
    // style attribute beats.
    inline_block parsed;
    const ctcss::value_sheet sheet = ctcss::parse_value("*{" + std::string{text} + "}");
    for (const ctcss::value_sheet::entry & e : sheet.entries) {
        auto & into = e.important ? parsed.important : parsed.normal;
        const auto expanded = expand_shorthand(e.property, e.value);
        if (expanded.empty()) {
            into.push_back(declaration{atoms_->intern_lower(e.property), e.value});
        } else {
            for (const auto & [property, value] : expanded) {
                into.push_back(declaration{atoms_->intern_lower(property), std::string{value}});
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

std::uint32_t engine::compile_selector(const ctcss::value_sheet::selector & src) {
    compiled_selector out;
    out.specificity = src.spec;
    // ctcss stores steps left to right; matching wants right to left.
    for (std::size_t i = src.steps.size(); i-- > 0;) {
        const ctcss::value_sheet::step & s = src.steps[i];
        compound c;
        if (!s.comp.tag.empty() && s.comp.tag != "*") { c.tag = atoms_->intern_lower(s.comp.tag); }
        if (!s.comp.id.empty()) { c.id = atoms_->intern(s.comp.id); }
        for (const std::string & cls : s.comp.classes) { c.classes.push_back(atoms_->intern(cls)); }
        c.states = s.comp.states;
        c.never_matches = s.comp.impossible;
        out.parts.push_back(std::move(c));
        // The relation belongs to the step on its LEFT, so reversing the
        // list means each link is read from the step we just left.
        if (i > 0) {
            out.links.push_back(s.relation == ctcss::rel::child ? combinator::child
                                                                : combinator::descendant);
        }
    }
    selectors_.push_back(std::move(out));
    return static_cast<std::uint32_t>(selectors_.size() - 1);
}

} // namespace ctbrowser::style
