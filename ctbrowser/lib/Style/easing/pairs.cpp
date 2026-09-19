#include "internal.hpp"

namespace ctbrowser::style::easing_detail {

// --- ratios ---

// A `<ratio>` as one number: `1 / 2`, `0.5`, `2 / 0` is infinite. Nothing
// for `auto` or a degenerate pair, which stay discrete.
[[nodiscard]] std::optional<double> ratio_of(std::string_view text) {
    const std::vector<std::string_view> parts = split_top_level(text, "/");
    if (parts.empty() || parts.size() > 2) { return std::nullopt; }
    double n[2] = {1, 1};
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const std::optional<double> v = number_of(trim(parts[i], html_whitespace));
        if (!v || *v < 0) { return std::nullopt; }
        n[i] = *v;
    }
    if (n[1] == 0 || n[0] == 0) { return std::nullopt; }
    return n[0] / n[1];
}

// CSS Sizing 4 §7.1: a ratio interpolates as the logarithm of its value,
// and the answer is written as `<number> / 1`.
[[nodiscard]] std::optional<std::string> interpolate_ratio(std::string_view from,
                                                           std::string_view to, double p) {
    const std::optional<double> a = ratio_of(from);
    const std::optional<double> b = ratio_of(to);
    if (!a || !b) { return std::nullopt; }
    const double mixed = std::exp((1 - p) * std::log(*a) + p * std::log(*b));
    return css::serialize_number(mixed) + " / 1";
}

// --- the pair ---

// calc-size(A, B)'s basis and calculation, or nothing when `text` is not one.
[[nodiscard]] std::optional<std::pair<std::string_view, std::string_view>> calc_size_args(
    std::string_view text) {
    text = trim(text, html_whitespace);
    if (!ascii_iequals(text.substr(0, 10), "calc-size(") || !text.ends_with(')')) {
        return std::nullopt;
    }
    std::size_t depth = 1;
    std::size_t comma = std::string_view::npos;
    for (std::size_t i = 10; i + 1 < text.size() && comma == std::string_view::npos; ++i) {
        if (text[i] == '(') {
            ++depth;
        } else if (text[i] == ')') {
            --depth;
        } else if (text[i] == ',' && depth == 1) {
            comma = i;
        }
    }
    if (comma == std::string_view::npos) { return std::nullopt; }
    return std::pair{trim(text.substr(10, comma - 10), html_whitespace),
                     trim(text.substr(comma + 1, text.size() - comma - 2), html_whitespace)};
}

// CSS Values 4 §4.1: two values interpolate when they are one number, length
// or percentage each; two colours; or LISTS of the same shape -
// comma-separated, then space-separated - whose items pair off as one of
// those or as identical text (`inset`, `/`, `auto`). `border-width: 20px
// 40px`, `box-shadow: red 2px 2px`, `background-size: 10px 20%` are all that.
// When the pair does not, `interpolable` is false and the answer flips at the
// midpoint.
[[nodiscard]] std::string interpolate_pair(std::string_view property, std::string_view from,
                                           std::string_view to, double p,
                                           const css::length_context & ctx, bool & interpolable) {
    from = trim(from, html_whitespace);
    to = trim(to, html_whitespace);
    interpolable = true;
    if (property == "transform") {
        if (const std::optional<std::string> t = interpolate_transform(from, to, p)) { return *t; }
    }
    if (property == "aspect-ratio") {
        // `auto <ratio>` on both sides keeps `auto` and interpolates the
        // ratio; `auto` on one side only is discrete.
        const bool auto_a = ascii_istarts_with(from, "auto");
        const bool auto_b = ascii_istarts_with(to, "auto");
        if (auto_a == auto_b) {
            const std::string_view ra = auto_a ? trim(from.substr(4), html_whitespace) : from;
            const std::string_view rb = auto_b ? trim(to.substr(4), html_whitespace) : to;
            if (const std::optional<std::string> r = interpolate_ratio(ra, rb, p)) {
                return auto_a ? "auto " + *r : *r;
            }
        }
    }
    if (property == "rotate") {
        if (const std::optional<std::string> r = interpolate_rotate(from, to, p, ctx)) {
            return *r;
        }
    }
    if (property == "filter" || property == "backdrop-filter") {
        if (const std::optional<std::string> f = interpolate_filter(from, to, p, ctx)) {
            return *f;
        }
    }
    if (const auto a = css::resolve_color(from, {}), b = css::resolve_color(to, {}); a && b) {
        return legacy_color(from) && legacy_color(to) ? lerp_color(*a, *b, p)
                                                      : lerp_oklab(*a, *b, p);
    }
    // calc-size(A, B) with calc-size(C, D) interpolates A with C and B with D
    // (CSS Values 5 §10.3); a calc-size() against a plain value is discrete.
    if (const auto a = calc_size_args(from), b = calc_size_args(to); a || b) {
        if (!a || !b) {
            interpolable = false;
            return std::string{p < 0.5 ? from : to};
        }
        const std::string basis =
            interpolate_pair(property, a->first, b->first, p, ctx, interpolable);
        if (!interpolable) { return std::string{p < 0.5 ? from : to}; }
        const std::string calc =
            interpolate_pair(property, a->second, b->second, p, ctx, interpolable);
        if (!interpolable) { return std::string{p < 0.5 ? from : to}; }
        return "calc-size(" + basis + ", " + calc + ")";
    }
    if (const std::optional<numeric_pair> n = numeric_of(from, to, ctx)) {
        return numeric_text(property, mix(n->a, n->b, p));
    }
    std::vector<std::string> lists_a;
    std::vector<std::string> lists_b;
    for (const std::string_view item : comma_items(from)) { lists_a.emplace_back(item); }
    for (const std::string_view item : comma_items(to)) { lists_b.emplace_back(item); }
    const auto discrete = [&] {
        interpolable = false;
        return std::string{p < 0.5 ? from : to};
    };
    if (repeatable(property)) { repeat_to_match(lists_a, lists_b); }
    const bool shadow = is_shadow(property);
    if (shadow) {
        // The shorter shadow list is padded at its end to the longer one's
        // length, each blank shadow inset when its partner is.
        while (lists_a.size() < lists_b.size()) {
            lists_a.push_back(blank_shadow(property == "box-shadow", lists_b[lists_a.size()]));
        }
        while (lists_b.size() < lists_a.size()) {
            lists_b.push_back(blank_shadow(property == "box-shadow", lists_a[lists_b.size()]));
        }
    }
    if (lists_a.size() != lists_b.size() || lists_a.empty()) { return discrete(); }
    std::string out;
    for (std::size_t i = 0; i < lists_a.size(); ++i) {
        const std::vector<std::string_view> items_a = split_top_level(lists_a[i], html_whitespace);
        const std::vector<std::string_view> items_b = split_top_level(lists_b[i], html_whitespace);
        if (items_a.size() != items_b.size() || items_a.empty()) { return discrete(); }
        // A single item on each side is the pair itself, already refused above.
        if (items_a.size() == 1 && lists_a.size() == 1) { return discrete(); }
        if (i != 0) { out += ", "; }
        std::size_t k_out = 0;
        for (std::size_t k = 0; k < items_a.size(); ++k) {
            const std::string_view x = trim(items_a[k], html_whitespace);
            const std::string_view y = trim(items_b[k], html_whitespace);
            if (x.empty() && y.empty()) { continue; }
            if (k_out++ != 0) { out += ' '; }
            if (x == y) {
                out += x;
                continue;
            }
            bool item_ok = true;
            std::string piece = interpolate_pair(property, x, y, p, ctx, item_ok);
            if (!item_ok) { return discrete(); }
            // A shadow's blur - the third length, in computed shape - cannot
            // go negative (CSS Backgrounds 3 §7.2): the extrapolated one is zero.
            if (shadow && k_out == 4 && piece.starts_with('-')) { piece = "0px"; }
            out += piece;
        }
    }
    return out;
}

// Web Animations 1 §4.5.1 over one item: numerics sum, colours sum, identical
// keywords keep, a list adds item by item, and anything else is `value`.
[[nodiscard]] std::string add_pair(std::string_view property, std::string_view underlying,
                                   std::string_view value, const css::length_context & ctx) {
    underlying = trim(underlying, html_whitespace);
    value = trim(value, html_whitespace);
    if (underlying.empty()) { return std::string{value}; }
    if (const auto a = css::resolve_color(underlying, {}), b = css::resolve_color(value, {});
        a && b) {
        return add_color(*a, *b);
    }
    if (const std::optional<numeric_pair> n = numeric_of(underlying, value, ctx)) {
        return numeric_text(property, sum(n->a, n->b));
    }
    std::vector<std::string_view> lists_a = comma_items(underlying);
    std::vector<std::string_view> lists_b = comma_items(value);
    if (repeatable(property)) { repeat_to_match(lists_a, lists_b); }
    if (lists_a.size() != lists_b.size() || lists_a.empty()) { return std::string{value}; }
    std::string out;
    for (std::size_t i = 0; i < lists_a.size(); ++i) {
        const std::vector<std::string_view> items_a = split_top_level(lists_a[i], html_whitespace);
        const std::vector<std::string_view> items_b = split_top_level(lists_b[i], html_whitespace);
        if (items_a.size() != items_b.size() || items_a.empty()) { return std::string{value}; }
        if (items_a.size() == 1 && lists_a.size() == 1) { return std::string{value}; }
        if (i != 0) { out += ", "; }
        std::size_t k_out = 0;
        for (std::size_t k = 0; k < items_a.size(); ++k) {
            const std::string_view x = trim(items_a[k], html_whitespace);
            const std::string_view y = trim(items_b[k], html_whitespace);
            if (x.empty() && y.empty()) { continue; }
            if (k_out++ != 0) { out += ' '; }
            if (x == y) {
                out += x;
                continue;
            }
            bool item_ok = true;
            (void)interpolate_pair(property, x, y, 0.5, ctx, item_ok);
            if (!item_ok) { return std::string{value}; }
            out += add_pair(property, x, y, ctx);
        }
    }
    return out;
}

} // namespace ctbrowser::style::easing_detail
