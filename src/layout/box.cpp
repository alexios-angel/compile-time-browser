#include <ctbrowser/layout/box.hpp>

// box: the method bodies.
// The header says what these do; this says how.

namespace ctbrowser::layout {

box_node box_builder::build(const read_txn & txn, node_id root) {
    box_node out;
    out.kind = box_kind::block;
    out.source = root;
    out.style = style_of(root);
    build_children(txn, root, out, 16.0f);
    drop_collapsible_spaces(out);
    normalise(out);
    return out;
}

computed_style_ptr box_builder::style_of(node_id id) const {
    const auto it = styles_->find(style::engine::key_of(id));
    return it == styles_->end() ? computed_style_ptr{} : it->second;
}

std::string_view box_builder::prop(const computed_style_ptr & s, atom name) const {
    return s ? s->get(name) : std::string_view{};
}

void box_builder::drop_collapsible_spaces(box_node & parent) {
    const auto renders_inline = [](const box_node & b) {
        return !b.is_block_level() && !b.collapsible_space;
    };
    std::vector<box_node> kept;
    kept.reserve(parent.children.size());
    for (std::size_t i = 0; i < parent.children.size(); ++i) {
        if (parent.children[i].collapsible_space) {
            const bool inline_before = !kept.empty() && renders_inline(kept.back());
            bool inline_after = false;
            for (std::size_t j = i + 1; j < parent.children.size(); ++j) {
                if (parent.children[j].collapsible_space) { continue; }
                inline_after = renders_inline(parent.children[j]);
                break;
            }
            if (!inline_before || !inline_after) { continue; }
        }
        kept.push_back(std::move(parent.children[i]));
    }
    parent.children = std::move(kept);
}

void box_builder::normalise(box_node & parent) {
    bool has_block = false;
    bool has_inline = false;
    for (const box_node & c : parent.children) {
        (c.is_block_level() ? has_block : has_inline) = true;
    }
    if (!has_block || !has_inline) { return; } // homogeneous: nothing to do

    std::vector<box_node> rebuilt;
    std::vector<box_node> run;
    const auto flush = [&] {
        if (run.empty()) { return; }
        box_node wrapper;
        wrapper.kind = box_kind::anonymous;
        wrapper.style = parent.style; // anonymous boxes inherit, per spec
        wrapper.font_size = parent.font_size;
        wrapper.children = std::move(run);
        run.clear();
        rebuilt.push_back(std::move(wrapper));
    };
    for (box_node & c : parent.children) {
        if (c.is_block_level()) {
            flush();
            rebuilt.push_back(std::move(c));
        } else {
            run.push_back(std::move(c));
        }
    }
    flush();
    parent.children = std::move(rebuilt);
}

side_lengths box_builder::parse_sides(std::string_view shorthand) {
    side_lengths out;
    length parts[4];
    std::size_t count = 0;
    std::size_t i = 0;
    while (i < shorthand.size() && count < 4) {
        while (i < shorthand.size() && (shorthand[i] == ' ' || shorthand[i] == '\t')) { ++i; }
        const std::size_t start = i;
        while (i < shorthand.size() && shorthand[i] != ' ' && shorthand[i] != '\t') { ++i; }
        if (i > start) { parts[count++] = parse_length(shorthand.substr(start, i - start)); }
    }
    switch (count) {
    case 1: out = {parts[0], parts[0], parts[0], parts[0]}; break;
    case 2: out = {parts[0], parts[1], parts[0], parts[1]}; break;
    case 3: out = {parts[0], parts[1], parts[2], parts[1]}; break;
    case 4: out = {parts[0], parts[1], parts[2], parts[3]}; break;
    default: break;
    }
    return out;
}

std::string_view box_builder::trimmed(std::string_view v) {
    const std::size_t b = v.find_first_not_of(" \t\n\r");
    if (b == std::string_view::npos) { return {}; }
    return v.substr(b, v.find_last_not_of(" \t\n\r") - b + 1);
}

std::string box_builder::collapse_whitespace(std::string_view text) {
    const auto is_space = [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
    };
    std::string out;
    out.reserve(text.size());
    bool in_run = false;
    for (const char c : text) {
        if (is_space(c)) {
            if (!in_run) { out.push_back(' '); }
            in_run = true;
        } else {
            out.push_back(c);
            in_run = false;
        }
    }
    return out;
}

float box_builder::parse_number(std::string_view text) {
    float value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} ? value : 0;
}

std::string box_builder::first_family(std::string_view list) {
    std::size_t start = 0;
    while (start < list.size() && (list[start] == ' ' || list[start] == '\t')) { ++start; }
    std::size_t end = list.find(',', start);
    if (end == std::string_view::npos) { end = list.size(); }
    std::string_view first = list.substr(start, end - start);
    while (!first.empty() && (first.back() == ' ' || first.back() == '\t')) {
        first.remove_suffix(1);
    }
    // Quoted names - `font-family: "Fira Sans"` - are the same name.
    if (first.size() >= 2 && (first.front() == '"' || first.front() == '\'') &&
        first.back() == first.front()) {
        first = first.substr(1, first.size() - 2);
    }
    return std::string{first};
}

} // namespace ctbrowser::layout
