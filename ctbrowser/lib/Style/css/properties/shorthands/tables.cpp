//===- tables.cpp - Shorthand tables -------------------------- -*- C++ -*-===//
//
// Lookups over the shorthand table: the longhands each shorthand expands to,
// the shorthands a longhand belongs to, legacy `-webkit-` name aliases and
// the logical/physical group of a property.
//
//===----------------------------------------------------------------------===//
#include "internal.hpp"

namespace ctbrowser::style::css::shorthand_detail {

/// Maps a legacy `-webkit-` alias to the property it stands for
/// (`-webkit-border-*-radius`, the flexbox names, `-webkit-appearance`);
/// any other name is returned unchanged.
[[nodiscard]] std::string_view radius_name(std::string_view name) {
    if (ascii_istarts_with(name, "-webkit-border-") &&
        ascii_iequals(name.substr(name.size() - 7), "-radius") && find_property(name) != nullptr) {
        return name.substr(8);
    }
    // The other legacy name aliases (CSS Cascade 5, "legacy name alias"): a
    // name that is another property's name at parse time. It is set, read and
    // written back under the new name and has no property of its own. These
    // are Flexbox 1 §9's twelve `-webkit-` names and CSS UI 4's
    // `-webkit-appearance`.
    static constexpr std::string_view webkit[] = {
        "align-content",   "align-items", "align-self", "flex",        "flex-basis",
        "flex-direction",  "flex-flow",   "flex-grow",  "flex-shrink", "flex-wrap",
        "justify-content", "order",       "appearance"};
    if (ascii_istarts_with(name, "-webkit-")) {
        for (const std::string_view target : webkit) {
            if (ascii_iequals(name.substr(8), target)) { return target; }
        }
    }
    return name;
}

/// The shorthand table with each longhand list split once, built on first use.
/// `all` expands to every longhand in the property table except the two that
/// CSS Cascade 4 §3.1 leaves out (`direction`, `unicode-bidi`) and the legacy
/// aliases.
const std::vector<expansion> & expansions() {
    static const std::vector<expansion> built = [] {
        std::vector<expansion> out;
        for (const shorthand_syntax & one : table) {
            expansion e{&one, {}};
            if (one.kind == shape::all) {
                for (const property_syntax & p : known_properties()) {
                    if (p.shorthand || p.name == "direction" || p.name == "unicode-bidi" ||
                        radius_name(p.name) != p.name) {
                        continue;
                    }
                    e.longhands.push_back(p.name);
                }
            } else {
                e.longhands = split_top_level(one.longhands, " ");
            }
            out.push_back(std::move(e));
        }
        return out;
    }();
    return built;
}

/// The expansion of shorthand `name` (ASCII case-insensitive), or null.
[[nodiscard]] const expansion * expansion_of(std::string_view name) {
    for (const expansion & e : expansions()) {
        if (ascii_iequals(e.syntax->name, name)) { return &e; }
    }
    return nullptr;
}

/// The shorthands a longhand belongs to, in preferred order: the one with the
/// most longhands first, so `border` is tried before `border-width` before
/// `border-top`, and `all` before everything.
[[nodiscard]] std::vector<const expansion *> shorthands_for(std::string_view longhand) {
    std::vector<const expansion *> out;
    for (const expansion & e : expansions()) {
        if (ascii_iequals_any(longhand, e.longhands)) { out.push_back(&e); }
    }
    std::stable_sort(out.begin(), out.end(), [](const expansion * a, const expansion * b) {
        return a->longhands.size() > b->longhands.size();
    });
    return out;
}

/// The initial value of `longhand`, or empty if it is not a known property.
[[nodiscard]] std::string_view initial_of(std::string_view longhand) {
    const property_syntax * p = find_property(longhand);
    return p == nullptr ? std::string_view{} : p->initial;
}

/// The logical property group `name` belongs to (its name with the side
/// removed, e.g. `margin-inline-start` -> `margin`) and which kind of side it
/// names. A property in no group returns an empty group with mapping::none.
[[nodiscard]] logical_group group_of(std::string_view name) {
    if (name.starts_with("border-") && name.ends_with("-radius")) {
        const bool logical = name.find("-start") != std::string_view::npos ||
                             name.find("-end") != std::string_view::npos;
        return {"border-radius", logical ? mapping::block : mapping::physical};
    }
    static constexpr std::pair<std::string_view, mapping> parts[] = {
        {"-inline-start", mapping::inline_}, {"-inline-end", mapping::inline_},
        {"-block-start", mapping::block},    {"-block-end", mapping::block},
        {"-top", mapping::physical},         {"-right", mapping::physical},
        {"-bottom", mapping::physical},      {"-left", mapping::physical},
    };
    for (const auto & [part, logic] : parts) {
        const std::size_t at = name.find(part);
        if (at == std::string_view::npos) { continue; }
        std::string group{name.substr(0, at)};
        group += name.substr(at + part.size());
        return {std::move(group), logic};
    }
    for (const std::string_view side : {"top", "right", "bottom", "left"}) {
        if (name == side) { return {"inset", mapping::physical}; }
    }
    return {};
}

} // namespace ctbrowser::style::css::shorthand_detail
