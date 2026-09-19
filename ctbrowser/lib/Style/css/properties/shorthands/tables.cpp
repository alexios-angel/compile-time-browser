#include "internal.hpp"

namespace ctbrowser::style::css::shorthand_detail {

// The table with its longhand lists split once. `all` is every longhand the
// property table has except the two CSS Cascade 4 §3.1 leaves out.
const std::vector<expansion> & expansions() {
    static const std::vector<expansion> built = [] {
        std::vector<expansion> out;
        for (const shorthand_syntax & one : table) {
            expansion e{&one, {}};
            if (one.kind == shape::all) {
                for (const property_syntax & p : known_properties()) {
                    if (p.shorthand || p.name == "direction" || p.name == "unicode-bidi") {
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

[[nodiscard]] const expansion * expansion_of(std::string_view name) {
    for (const expansion & e : expansions()) {
        if (ascii_iequals(e.syntax->name, name)) { return &e; }
    }
    return nullptr;
}

// The shorthands a longhand belongs to, in PREFERRED ORDER: the one with the
// most longhands first, so `border` is tried before `border-width` before
// `border-top`, and `all` before everything.
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

[[nodiscard]] std::string_view initial_of(std::string_view longhand) {
    const property_syntax * p = find_property(longhand);
    return p == nullptr ? std::string_view{} : p->initial;
}

[[nodiscard]] logical_group group_of(std::string_view name) {
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
