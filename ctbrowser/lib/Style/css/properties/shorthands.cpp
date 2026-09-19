#include "shorthands/internal.hpp"

namespace ctbrowser::style::css {

using namespace shorthand_detail;

// CSS Multicol 2: auto does not choose width or count until the other
// component is known; the slash introduces a separate, optional height.
bool detail::split_columns(std::string_view value, std::vector<std::string> & out) {
    const token_stream ts = tokenize(value);
    std::size_t slash = std::string_view::npos;
    int depth = 0;
    for (const css_token & token : ts.tokens) {
        if (token.type == token_type::function || token.type == token_type::open_paren) {
            ++depth;
        } else if (token.type == token_type::close_paren) {
            --depth;
        } else if (depth == 0 && token.type == token_type::delim && ts.text_of(token) == "/") {
            if (slash != std::string_view::npos) { return false; }
            slash = token.text;
        }
    }
    const auto parts = split_top_level(value.substr(0, slash), html_whitespace);
    if (parts.empty() || parts.size() > 2) { return false; }
    const auto names = longhands_of("columns");
    out.assign(names.size(), "auto");
    for (const std::string_view part : parts) {
        if (ascii_iequals(part, "auto")) { continue; }
        bool assigned = false;
        for (std::size_t i = 0; i < 2; ++i) {
            if (out[i] != "auto") { continue; }
            const value_check checked = check_declaration(names[i], part, false);
            if (!checked.valid || is_wide_keyword(checked.serialized)) { continue; }
            out[i] = checked.serialized;
            assigned = true;
            break;
        }
        if (!assigned) { return false; }
    }
    if (slash != std::string_view::npos) {
        const value_check height = check_declaration(names[2], value.substr(slash + 1), false);
        if (!height.valid || is_wide_keyword(height.serialized)) { return false; }
        out[2] = height.serialized;
    }
    return true;
}

std::span<const std::string_view> longhands_of(std::string_view shorthand) {
    const expansion * e = expansion_of(shorthand);
    return e == nullptr ? std::span<const std::string_view>{} : e->longhands;
}

bool set_declaration(declaration_block & block, std::string_view name, std::string_view text,
                     bool important) {
    if (trim(text, html_whitespace).empty()) {
        bool removed = false;
        (void)remove_declaration(block, name, removed);
        return removed;
    }
    return put(block, name, text, important, false);
}

void parse_declaration_block(declaration_block & block, std::string_view text,
                             bool (*allow)(std::string_view, std::string_view, const void *),
                             const void * ctx) {
    atom_table atoms;
    const stylesheet parsed = parse_declaration_list(text, atoms);
    for (const raw_declaration & d : parsed.declarations) {
        const std::string_view property = atoms.text(d.property);
        if (allow != nullptr && !allow(property, parsed.text_of(d), ctx)) { continue; }
        (void)put(block, property, parsed.text_of(d), d.important, true);
    }
}

std::string remove_declaration(declaration_block & block, std::string_view name, bool & removed) {
    std::string was = declaration_value(block, name);
    const std::span<const std::string_view> longhands = longhands_of(name);
    removed = erase_named(block, std::array<std::string_view, 1>{name});
    if (!longhands.empty()) { removed = erase_named(block, longhands) || removed; }
    return was;
}

std::string declaration_value(const declaration_block & block, std::string_view name) {
    if (const std::size_t at = index_of(block, name); at < block.size()) { return block[at].value; }
    const expansion * e = name.starts_with("--") ? nullptr : expansion_of(name);
    if (e == nullptr) { return {}; }
    std::vector<std::string> values;
    bool important = false;
    for (std::size_t i = 0; i < e->longhands.size(); ++i) {
        const std::size_t at = index_of(block, e->longhands[i]);
        if (at == block.size()) { return {}; }
        if (i == 0) { important = block[at].important; }
        if (block[at].important != important) { return {}; }
        values.push_back(block[at].value);
    }
    return fold(*e, values);
}

std::string declaration_priority(const declaration_block & block, std::string_view name) {
    if (const std::size_t at = index_of(block, name); at < block.size()) {
        return block[at].important ? "important" : "";
    }
    const std::span<const std::string_view> longhands = longhands_of(name);
    if (longhands.empty()) { return {}; }
    for (const std::string_view longhand : longhands) {
        const std::size_t at = index_of(block, longhand);
        if (at == block.size() || !block[at].important) { return {}; }
    }
    return "important";
}

std::string serialize_declaration_block(const declaration_block & block) {
    std::string out;
    std::vector<bool> done(block.size(), false);
    const auto emit = [&](std::string_view name, std::string_view value, bool important) {
        if (!out.empty()) { out += ' '; }
        // A custom property's name is an identifier the tokenizer decoded, so
        // `--a\;b` has to be written back escaped to survive a re-parse.
        out += name.starts_with("--") ? serialize_identifier(name) : std::string{name};
        out += ": ";
        out += value;
        if (important) { out += " !important"; }
        out += ';';
    };
    for (std::size_t i = 0; i < block.size(); ++i) {
        if (done[i]) { continue; }
        const declaration & d = block[i];
        bool folded = false;
        // "Serialize into a shorthand form": the first shorthand, in preferred
        // order, whose every longhand is here, unserialised, of one priority,
        // and not interleaved with the same logical group's other mapping.
        for (const expansion * e :
             d.name.starts_with("--") ? std::vector<const expansion *>{} : shorthands_for(d.name)) {
            std::vector<std::size_t> used;
            bool ok = true;
            for (const std::string_view longhand : e->longhands) {
                const std::size_t at = index_of(block, longhand);
                if (at == block.size() || done[at]) {
                    ok = false;
                    break;
                }
                used.push_back(at);
            }
            if (!ok) { continue; }
            const bool important = block[used.front()].important;
            for (const std::size_t at : used) { ok = ok && block[at].important == important; }
            if (!ok) { continue; }
            const auto [first, last] = std::minmax_element(used.begin(), used.end());
            for (std::size_t at = *first; at <= *last && ok; ++at) {
                if (std::find(used.begin(), used.end(), at) != used.end()) { continue; }
                const logical_group other = group_of(block[at].name);
                if (other.logic == mapping::none) { continue; }
                for (const std::size_t u : used) {
                    const logical_group mine = group_of(block[u].name);
                    if (mine.group == other.group && mine.logic != other.logic) { ok = false; }
                }
            }
            if (!ok) { continue; }
            std::vector<std::string> values;
            for (const std::size_t at : used) { values.push_back(block[at].value); }
            const std::string value = fold(*e, values);
            if (value.empty()) { continue; }
            for (const std::size_t at : used) { done[at] = true; }
            emit(e->syntax->name, value, important);
            folded = true;
            break;
        }
        if (folded) { continue; }
        done[i] = true;
        emit(d.name, d.value, d.important);
    }
    return out;
}

} // namespace ctbrowser::style::css
