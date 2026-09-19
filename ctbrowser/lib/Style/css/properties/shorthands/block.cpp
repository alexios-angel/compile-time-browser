#include "internal.hpp"

namespace ctbrowser::style::css::shorthand_detail {

// --- the block ----------------------------------------------------------------

[[nodiscard]] std::size_t index_of(const declaration_block & block, std::string_view name) {
    for (std::size_t i = 0; i < block.size(); ++i) {
        if (block[i].name == name) { return i; }
    }
    return block.size();
}

// CSSOM §6.6 "set a CSS declaration", by the algorithm the specification
// suggests: update in place, unless a declaration of the same logical group
// with a different mapping logic follows - then the new one must land after it.
bool set_one(declaration_block & block, std::string_view name, std::string value, bool important) {
    const std::size_t at = index_of(block, name);
    if (at < block.size()) {
        bool needs_append = false;
        const logical_group mine = group_of(name);
        if (mine.logic != mapping::none) {
            for (std::size_t i = at + 1; i < block.size() && !needs_append; ++i) {
                const logical_group other = group_of(block[i].name);
                needs_append = other.group == mine.group && other.logic != mine.logic;
            }
        }
        if (!needs_append) {
            if (block[at].value == value && block[at].important == important) { return false; }
            block[at].value = std::move(value);
            block[at].important = important;
            return true;
        }
        block.erase(block.begin() + static_cast<std::ptrdiff_t>(at));
    }
    block.push_back(declaration{std::string{name}, std::move(value), important});
    return true;
}

// The PARSED form of the same: a later declaration replaces an earlier one and
// takes its place at the end, unless the earlier was important and it is not -
// CSS Cascade 4 §6.1 within one block.
bool add_parsed(declaration_block & block, std::string_view name, std::string value,
                bool important) {
    const std::size_t at = index_of(block, name);
    if (at < block.size()) {
        if (block[at].important && !important) { return false; }
        block.erase(block.begin() + static_cast<std::ptrdiff_t>(at));
    }
    // ...and an important WHOLE shorthand already speaks for this longhand:
    // `background: red !important; background-color: green` keeps the red.
    if (!important) {
        for (const expansion & e : expansions()) {
            if (!ascii_iequals_any(name, e.longhands)) { continue; }
            const std::size_t whole = index_of(block, e.syntax->name);
            if (whole < block.size() && block[whole].important) { return false; }
        }
    }
    block.push_back(declaration{std::string{name}, std::move(value), important});
    return true;
}

// A whole shorthand entry replaces its longhands, and longhands replace a whole
// entry of their shorthand, so the two forms never both speak for one property.
bool erase_named(declaration_block & block, std::span<const std::string_view> names) {
    bool any = false;
    for (const std::string_view name : names) {
        const std::size_t at = index_of(block, name);
        if (at == block.size()) { continue; }
        block.erase(block.begin() + static_cast<std::ptrdiff_t>(at));
        any = true;
    }
    return any;
}

// One declaration into the block, from either path.
bool put(declaration_block & block, std::string_view name, std::string_view text, bool important,
         bool parsed) {
    const value_check checked = check_declaration(name, text, false);
    if (!checked.valid) { return false; }
    const auto add = parsed ? add_parsed : set_one;
    const expansion * e = name.starts_with("--") ? nullptr : expansion_of(name);
    if (e == nullptr) { return add(block, name, checked.serialized, important); }
    std::vector<std::string> values;
    // The SERIALISED shorthand is what is split, not the author's text: a
    // `random()` in it has had its key spelled against the shorthand's name -
    // `margin: random(property-index-scoped, ...)` is `ua-margin-1` on every
    // side, not `ua-margin-top-1` on one of them (random-computed).
    const split result =
        checked.substituted ? split::whole : split_value(*e, checked.serialized, values);
    if (result == split::invalid) { return false; }
    if (result == split::whole) {
        bool changed = erase_named(block, e->longhands);
        changed = add(block, name, checked.serialized, important) || changed;
        return changed;
    }
    bool changed = erase_named(block, std::array<std::string_view, 1>{name});
    // ...and every whole entry of another shorthand these longhands belong to:
    // `all: revert` after `font: 12px serif` speaks for `font` now.
    for (const expansion & other : expansions()) {
        if (&other == e || index_of(block, other.syntax->name) == block.size()) { continue; }
        for (const std::string_view longhand : other.longhands) {
            if (!ascii_iequals_any(longhand, e->longhands)) { continue; }
            changed =
                erase_named(block, std::array<std::string_view, 1>{other.syntax->name}) || changed;
            break;
        }
    }
    for (std::size_t i = 0; i < e->longhands.size(); ++i) {
        changed = add(block, e->longhands[i], values[i], important) || changed;
    }
    return changed;
}

} // namespace ctbrowser::style::css::shorthand_detail
