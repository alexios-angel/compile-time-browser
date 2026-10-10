//===- ctbrowser/lib/Style/easing/pairs.cpp - Pairwise blending -*- C++ -*-===//
//
// Pairwise interpolation (interpolate_pair) and addition (add_pair) of two
// computed CSS values for transitions and animations, with the
// property-specific rules: ratios, gap-rule lists, clip rect(), grid track
// lists, calc-size() and the intrinsic size keywords.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::style::easing_detail {

// --- ratios ---

/// A `<ratio>` as one number (`1 / 2` and `0.5` are both 0.5), or nullopt for
/// `auto` or a degenerate ratio with a zero on either side, which interpolates
/// discretely.
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

/// CSS Sizing 4 §7.1: a ratio interpolates as the logarithm of its value,
/// and the answer is written as `<number> / 1`.
[[nodiscard]] std::optional<std::string> interpolate_ratio(std::string_view from,
                                                           std::string_view to, double p) {
    const std::optional<double> a = ratio_of(from);
    const std::optional<double> b = ratio_of(to);
    if (!a || !b) { return std::nullopt; }
    const double mixed = std::exp((1 - p) * std::log(*a) + p * std::log(*b));
    return css::serialize_number(mixed) + " / 1";
}

// --- gap rule lists (CSS Gap Decorations 1) ---

/// Whether `property` takes a gap-rule list (`column-rule-width` and friends).
[[nodiscard]] bool gap_list_property(std::string_view property) {
    return property == "column-rule-width" || property == "row-rule-width" ||
           property == "column-rule-color" || property == "row-rule-color";
}

/// A gap list in the shape it interpolates in: the integer repeats written
/// out, and the one `repeat(auto, ...)`, if any, kept apart from the values
/// before and after it.
struct gap_shape {
    std::vector<std::string> leading, repeated, trailing;
    bool has_auto = false;
};

/// Parses a gap list into a gap_shape, or nullopt for an empty item, a second
/// auto repeat, or a repeat count outside 1 to 10000.
[[nodiscard]] std::optional<gap_shape> gap_shape_of(std::string_view text) {
    gap_shape out;
    for (const std::string_view raw : split_top_level(text, ",")) {
        const std::string_view item = trim(raw, html_whitespace);
        if (item.empty()) { return std::nullopt; }
        std::vector<std::string> & into = out.has_auto ? out.trailing : out.leading;
        if (!ascii_istarts_with(item, "repeat(") || !item.ends_with(')')) {
            into.emplace_back(item);
            continue;
        }
        const std::vector<std::string_view> args =
            split_top_level(item.substr(7, item.size() - 8), ",");
        if (args.size() < 2) { return std::nullopt; }
        const std::string_view count = trim(args[0], html_whitespace);
        if (ascii_iequals(count, "auto")) {
            if (out.has_auto) { return std::nullopt; }
            out.has_auto = true;
            for (std::size_t i = 1; i < args.size(); ++i) {
                out.repeated.emplace_back(trim(args[i], html_whitespace));
            }
            continue;
        }
        const std::optional<double> n = number_of(count);
        if (!n || *n < 1 || *n > 10000 || std::floor(*n) != *n) { return std::nullopt; }
        for (int k = 0; k < static_cast<int>(*n); ++k) {
            for (std::size_t i = 1; i < args.size(); ++i) {
                into.emplace_back(trim(args[i], html_whitespace));
            }
            // Ten thousand repeats of a long argument list could still be huge;
            // apply the same cap repeat_to_match puts on the paired lists.
            if (into.size() > max_repeated_items) { return std::nullopt; }
        }
    }
    return out;
}

/// CSS Gap Decorations 1 §animation: two lists interpolate item by item once
/// their integer repeats are written out, the shorter repeated to the least
/// common multiple of the two lengths. With a `repeat(auto, ...)` on both
/// sides the values before it must pair one to one, and so must those after
/// it, and the two auto repeats' values match by the same multiple; with one
/// on only one side, the lists do not interpolate.
[[nodiscard]] std::optional<std::string> interpolate_gap_list(std::string_view property,
                                                              std::string_view from,
                                                              std::string_view to, double p,
                                                              const css::length_context & ctx) {
    std::optional<gap_shape> a = gap_shape_of(from);
    std::optional<gap_shape> b = gap_shape_of(to);
    if (!a || !b || a->has_auto != b->has_auto) { return std::nullopt; }
    if (a->has_auto) {
        if (a->leading.size() != b->leading.size() || a->trailing.size() != b->trailing.size()) {
            return std::nullopt;
        }
        if (!repeat_to_match(a->repeated, b->repeated)) { return std::nullopt; }
    } else if (!repeat_to_match(a->leading, b->leading)) {
        return std::nullopt;
    }
    bool ok = true;
    const auto items = [&](const std::vector<std::string> & x, const std::vector<std::string> & y) {
        std::string out;
        for (std::size_t i = 0; i < x.size() && ok; ++i) {
            if (!out.empty()) { out += ", "; }
            out += interpolate_pair(property, computed_shape(property, x[i], ctx),
                                    computed_shape(property, y[i], ctx), p, ctx, ok);
        }
        return out;
    };
    std::string out = items(a->leading, b->leading);
    if (a->has_auto) {
        const std::string repeated = "repeat(auto, " + items(a->repeated, b->repeated) + ")";
        out += (out.empty() ? "" : ", ") + repeated;
        const std::string trailing = items(a->trailing, b->trailing);
        if (!trailing.empty()) { out += ", " + trailing; }
    }
    if (!ok) { return std::nullopt; }
    return out;
}

// --- clip's rect() (CSS Masking 1 §7.1) ---

/// The four components of a `rect()`, comma- or space-separated as CSS 2.1
/// allowed; nullopt for anything else.
[[nodiscard]] std::optional<std::vector<std::string_view>> rect_parts(std::string_view text) {
    text = trim(text, html_whitespace);
    if (!ascii_istarts_with(text, "rect(") || !text.ends_with(')')) { return std::nullopt; }
    const std::string_view body = text.substr(5, text.size() - 6);
    std::vector<std::string_view> parts = split_top_level(body, ",");
    if (parts.size() == 1) { parts = split_top_level(body, html_whitespace); }
    std::vector<std::string_view> out;
    for (const std::string_view part : parts) {
        if (!trim(part, html_whitespace).empty()) { out.push_back(trim(part, html_whitespace)); }
    }
    if (out.size() != 4) { return std::nullopt; }
    return out;
}

/// Blends two rects component by component as `wa * a + wb * b`; nullopt when
/// an `auto` meets a length, which makes the pair discrete.
[[nodiscard]] std::optional<std::string> blend_rects(std::string_view from, std::string_view to,
                                                     double wa, double wb,
                                                     const css::length_context & ctx) {
    const std::optional<std::vector<std::string_view>> a = rect_parts(from);
    const std::optional<std::vector<std::string_view>> b = rect_parts(to);
    if (!a || !b) { return std::nullopt; }
    std::string out = "rect(";
    for (std::size_t i = 0; i < 4; ++i) {
        std::string one;
        if (ascii_iequals((*a)[i], "auto") || ascii_iequals((*b)[i], "auto")) {
            if (!ascii_iequals((*a)[i], (*b)[i])) { return std::nullopt; }
            one = "auto";
        } else {
            const std::optional<numeric_pair> n = numeric_of((*a)[i], (*b)[i], ctx);
            if (!n) { return std::nullopt; }
            css::calc_result v = n->a;
            v.px = wa * n->a.px + wb * n->b.px;
            v.percent = wa * n->a.percent + wb * n->b.percent;
            v.has_percent = n->a.has_percent || n->b.has_percent;
            one = css::serialize_calc(v);
        }
        out += (i == 0 ? "" : ", ") + one;
    }
    return out + ")";
}

// --- grid track lists (CSS Grid 2 §7.2.5) ---

/// The top-level words of a track list: a `[names]` block, a `repeat(...)` or
/// one track size, each whole.
[[nodiscard]] std::vector<std::string_view> track_words(std::string_view text) {
    std::vector<std::string_view> out;
    int depth = 0;
    std::size_t start = std::string_view::npos;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        const char c = i < text.size() ? text[i] : ' ';
        const bool space = html_whitespace.contains(c);
        if (depth == 0 && space) {
            if (start != std::string_view::npos) { out.push_back(text.substr(start, i - start)); }
            start = std::string_view::npos;
            continue;
        }
        if (start == std::string_view::npos) { start = i; }
        if (c == '(' || c == '[') { ++depth; }
        if (c == ')' || c == ']') { --depth; }
    }
    return out;
}

/// Blends one track size of each list as `wa * x + wb * y`, with weights
/// (1 - p, p) for an interpolation and (1, 1) for an addition. Two calls of the
/// same function blend argument by argument, two numbers of one type blend as
/// numbers (never below zero), and anything else is discrete: the nearer end,
/// or the added value.
[[nodiscard]] std::string blend_track(std::string_view x, std::string_view y, double wa, double wb,
                                      const css::length_context & ctx) {
    if (x == y && !(wa == 1 && wb == 1)) { return std::string{x}; }
    const auto function = [](std::string_view text, std::string_view name) {
        return ascii_istarts_with(text, name) && text.ends_with(')');
    };
    for (const std::string_view name : {"minmax(", "fit-content("}) {
        if (!function(x, name) || !function(y, name)) { continue; }
        const std::vector<std::string_view> a =
            split_top_level(x.substr(name.size(), x.size() - name.size() - 1), ",");
        const std::vector<std::string_view> b =
            split_top_level(y.substr(name.size(), y.size() - name.size() - 1), ",");
        if (a.size() != b.size()) { break; }
        std::string out{name};
        for (std::size_t i = 0; i < a.size(); ++i) {
            out += (i == 0 ? "" : ", ") + blend_track(trim(a[i], html_whitespace),
                                                      trim(b[i], html_whitespace), wa, wb, ctx);
        }
        return out + ")";
    }
    if (const std::optional<numeric_pair> n = numeric_of(x, y, ctx)) {
        css::calc_result out = n->a;
        out.px = wa * n->a.px + wb * n->b.px;
        out.percent = wa * n->a.percent + wb * n->b.percent;
        out.has_percent = n->a.has_percent || n->b.has_percent;
        if (!out.has_percent) { out.px = std::max(out.px, 0.0); }
        return css::serialize_calc(out);
    }
    return std::string{wb < 0.5 ? x : y};
}

/// Blends two track lists of one shape (the same number of tracks, the same
/// repeat()s with the same counts) track by track as blend_track weighs them,
/// keeping the line names of the nearer end (§7.2.5). Returns nullopt for two
/// different shapes or an auto repeat, which are discrete.
[[nodiscard]] std::optional<std::string> interpolate_tracks(std::string_view from,
                                                            std::string_view to, double wa,
                                                            double wb,
                                                            const css::length_context & ctx) {
    const auto shape_of = [](std::string_view text, std::vector<std::string_view> & sizes,
                             std::string & shape) {
        for (const std::string_view word : track_words(text)) {
            if (word.starts_with('[')) { continue; }
            if (ascii_istarts_with(word, "repeat(") && word.ends_with(')')) {
                const std::vector<std::string_view> args =
                    split_top_level(word.substr(7, word.size() - 8), ",");
                if (args.size() != 2) { return false; }
                const std::string_view count = trim(args[0], html_whitespace);
                if (ascii_iequals_any(count, {"auto-fill", "auto-fit"})) { return false; }
                // Length-prefixed: a count is raw text, and `repeat(a(t)rb, X)`
                // must not spell the shape of `repeat(a, X) repeat(b, Y)`.
                shape += "r" + std::to_string(count.size()) + ":" + std::string{count} + "(";
                for (const std::string_view inner : track_words(args[1])) {
                    if (inner.starts_with('[')) { continue; }
                    sizes.push_back(inner);
                    shape += 't';
                }
                shape += ')';
                continue;
            }
            sizes.push_back(word);
            shape += 't';
        }
        return true;
    };
    std::vector<std::string_view> a, b;
    std::string shape_a, shape_b;
    if (!shape_of(from, a, shape_a) || !shape_of(to, b, shape_b) || shape_a != shape_b ||
        a.empty() || a.size() != b.size() || ascii_iequals(from, "none") ||
        ascii_iequals(to, "none")) {
        return std::nullopt;
    }
    // The nearer end's words, its sizes replaced by the blend in order.
    const std::string_view skeleton = wb < 0.5 ? from : to;
    std::size_t next = 0;
    std::string out;
    const auto size_text = [&] {
        std::string one = blend_track(a[next], b[next], wa, wb, ctx);
        ++next;
        return one;
    };
    for (const std::string_view word : track_words(skeleton)) {
        if (!out.empty()) { out += ' '; }
        if (word.starts_with('[')) {
            out += word;
            continue;
        }
        if (ascii_istarts_with(word, "repeat(") && word.ends_with(')')) {
            const std::vector<std::string_view> args =
                split_top_level(word.substr(7, word.size() - 8), ",");
            std::string inner;
            for (const std::string_view piece : track_words(args[1])) {
                if (!inner.empty()) { inner += ' '; }
                inner += piece.starts_with('[') ? std::string{piece} : size_text();
            }
            out += "repeat(" + std::string{trim(args[0], html_whitespace)} + ", " + inner + ")";
            continue;
        }
        out += size_text();
    }
    return out;
}

/// calc-size(A, B)'s basis and calculation, or nullopt when `text` is not one.
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

// --- calc-size() and the intrinsic size keywords (CSS Values 5 §calc-size) ---

/// The properties calc-size() and interpolate-size apply to: the widths and
/// heights, their logical names, their minimums and maximums, and flex-basis.
[[nodiscard]] bool sizing_property(std::string_view property) {
    if (property.starts_with("min-") || property.starts_with("max-")) { property.remove_prefix(4); }
    return property == "width" || property == "height" || property == "inline-size" ||
           property == "block-size" || property == "flex-basis";
}

/// Whether `text` is an intrinsic size keyword: one a calc-size() may take as
/// its basis, and one interpolate-size lets interpolate.
[[nodiscard]] bool size_keyword(std::string_view text) {
    return ascii_iequals_any(
        text, {"auto", "min-content", "max-content", "fit-content", "stretch", "content"});
}

/// Returns `calc` with every `size` in it replaced by `(by)`: how a basis that
/// is a calc-size() or a length folds into the calculation over it. Returns
/// nullopt when the result would exceed `limit` bytes, checked before anything
/// is built: a nest of calc-size()s doubles its text at every level, and CSS
/// Values 5 ("substitute into a calc-size calculation", step 3) lets the UA
/// refuse an expansion past its limit, which makes the pair not interpolable
/// (csswg-drafts#10369).
[[nodiscard]] std::optional<std::string> substitute_size(std::string_view calc, std::string_view by,
                                                         std::size_t limit) {
    const auto name = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '-' || c == '_';
    };
    const auto size_at = [&](std::size_t i) {
        return ascii_iequals(calc.substr(i, 4), "size") && (i == 0 || !name(calc[i - 1])) &&
               (i + 4 == calc.size() || !name(calc[i + 4]));
    };
    std::size_t count = 0;
    for (std::size_t i = 0; i < calc.size(); ++i) { count += size_at(i) ? 1 : 0; }
    if (calc.size() + count * (by.size() + 2) > limit + count * 4) { return std::nullopt; }
    std::string out;
    out.reserve(calc.size() + count * (by.size() + 2));
    for (std::size_t i = 0; i < calc.size();) {
        if (size_at(i)) {
            out += '(';
            out += by;
            out += ')';
            i += 4;
            continue;
        }
        out += calc[i++];
    }
    return out;
}

/// A size as calc-size() interpolates it: a basis and a calculation over it.
struct size_form {
    std::string basis;
    std::string calc;
};

/// The innermost basis of a nest of calc-size()s: the size it measures.
[[nodiscard]] std::string_view innermost_basis(std::string_view text) {
    while (const auto args = calc_size_args(text)) { text = args->first; }
    return trim(text, html_whitespace);
}

/// Canonicalizes a size for interpolation (CSS Values 5 §calc-size). A basis
/// that is itself a calc-size() folds its calculation in for `size`; a basis
/// that is a length becomes `any`, the length standing for `size`; a basis with
/// a percentage in it becomes `100%`, its share of that standing for `size`
/// (a percentage the containing block cannot resolve makes the value `auto`, so
/// it may not become `any`). A keyword is calc-size(keyword, size), and any
/// other <length-percentage> is calc-size(any, it). Returns nullopt past the
/// expansion limit.
[[nodiscard]] std::optional<size_form> canonical_size(std::string_view text,
                                                      const css::length_context & ctx,
                                                      int depth = 0) {
    // ponytail: a fixed text budget and a nesting depth are the UA's
    // expansion limit; the specification leaves its measure to the UA. The
    // depth limit is the one that matters in practice, because the cascade has
    // already folded each level's `size / 2 + size / 2` to `size`, while a
    // twenty-deep nest of those would expand a million-fold as written
    // (csswg-drafts#10369).
    constexpr std::size_t expansion_limit = 4096;
    constexpr int depth_limit = 16;
    if (depth > depth_limit) { return std::nullopt; }
    text = trim(text, html_whitespace);
    const auto args = calc_size_args(text);
    if (!args) {
        if (size_keyword(text)) { return size_form{ascii_lower_copy(text), "size"}; }
        return size_form{"any", std::string{text}};
    }
    const std::string_view basis = args->first;
    const std::string_view calc = args->second;
    if (calc_size_args(basis)) {
        std::optional<size_form> inner = canonical_size(basis, ctx, depth + 1);
        if (!inner) { return std::nullopt; }
        std::optional<std::string> folded = substitute_size(calc, inner->calc, expansion_limit);
        if (!folded) { return std::nullopt; }
        return size_form{std::move(inner->basis), std::move(*folded)};
    }
    if (size_keyword(basis) || ascii_iequals(basis, "any")) {
        return size_form{ascii_lower_copy(basis), std::string{calc}};
    }
    const std::optional<numeric_pair> n = numeric_of(basis, basis, ctx);
    if (!n || n->a.is_number) { return std::nullopt; }
    std::string by = css::serialize_calc(n->a);
    if (n->a.has_percent) {
        // `P%` is P/100 of `100%`, and `calc(P% + L)` adds its length to that.
        by = css::serialize_number(n->a.percent / 100) + " * size";
        if (n->a.px != 0) {
            by += (n->a.px < 0 ? " - " : " + ") + css::serialize_number(std::fabs(n->a.px)) + "px";
        }
    }
    std::optional<std::string> folded = substitute_size(calc, by, expansion_limit);
    if (!folded) { return std::nullopt; }
    return size_form{n->a.has_percent ? "100%" : "any", std::move(*folded)};
}

/// Whether `text` is a <length-percentage>, the plain side a calc-size() meets.
[[nodiscard]] bool length_like(std::string_view text, const css::length_context & ctx) {
    const std::optional<numeric_pair> n = numeric_of(text, text, ctx);
    return n && (!n->a.is_number || n->a.px == 0);
}

/// Two sizes on one basis, each with its calculation over it.
struct size_pair_forms {
    std::string basis;
    std::string calc_a, calc_b;
};

/// Puts two sizes on one basis, or returns nullopt when calc-size() cannot: a
/// calc-size() pairs with another or with a <length-percentage>; an intrinsic
/// keyword pairs with a calc-size() over that keyword or over `any`, and, under
/// `interpolate-size: allow-keywords`, with a <length-percentage>. Two keywords
/// never interpolate. `sized` is set to false when neither side is a
/// calc-size() or a keyword, which leaves the pair to the ordinary numeric
/// rules.
[[nodiscard]] std::optional<size_pair_forms> size_pair(std::string_view a, std::string_view b,
                                                       const css::length_context & ctx,
                                                       bool & sized) {
    a = trim(a, html_whitespace);
    b = trim(b, html_whitespace);
    const bool calc_a = calc_size_args(a).has_value();
    const bool calc_b = calc_size_args(b).has_value();
    const bool key_a = size_keyword(a);
    const bool key_b = size_keyword(b);
    sized = calc_a || calc_b || key_a || key_b;
    if (!sized || (key_a && key_b)) { return std::nullopt; }
    const auto meets = [&](bool key, std::string_view keyword, bool calc, std::string_view other) {
        if (!key) { return true; }
        if (calc) {
            const std::string_view inner = innermost_basis(other);
            return ascii_iequals(inner, keyword) || ascii_iequals(inner, "any");
        }
        return ctx.interpolate_keywords && length_like(other, ctx);
    };
    if (!meets(key_a, a, calc_b, b) || !meets(key_b, b, calc_a, a)) { return std::nullopt; }
    if ((calc_a && !calc_b && !key_b && !length_like(b, ctx)) ||
        (calc_b && !calc_a && !key_a && !length_like(a, ctx))) {
        return std::nullopt;
    }
    std::optional<size_form> fa = canonical_size(a, ctx);
    std::optional<size_form> fb = canonical_size(b, ctx);
    if (!fa || !fb) { return std::nullopt; }
    if (fa->basis != fb->basis && fa->basis != "any" && fb->basis != "any") { return std::nullopt; }
    std::string basis = fa->basis == "any" ? std::move(fb->basis) : std::move(fa->basis);
    return size_pair_forms{std::move(basis), std::move(fa->calc), std::move(fb->calc)};
}

/// A calc-size() calculation read as `k * size + lengths`.
struct size_sum {
    double size = 0;
    css::calc_result lengths;
    bool has_size = false, has_lengths = false, number = false;
};
/// Reads a calc-size() calculation into a size_sum: the grammar of a
/// <calc-sum> with `size` as a term, kept linear (a product has at most one
/// side that is not a number). Fails for anything else (`sign(size) * size`),
/// which then blends as written.
struct size_sum_reader : css::linear_sum<size_sum_reader, size_sum> {
    const css::length_context & ctx;

    static double number_of(const size_sum & s) { return s.lengths.px; }
    static void scale(size_sum & s, double by) {
        s.size *= by;
        s.lengths.px *= by;
        s.lengths.percent *= by;
    }
    static void add(size_sum & s, double sign, const size_sum & rhs) {
        s.size += sign * rhs.size;
        s.lengths.px += sign * rhs.lengths.px;
        s.lengths.percent += sign * rhs.lengths.percent;
        s.lengths.has_percent = s.lengths.has_percent || rhs.lengths.has_percent;
        s.has_size = s.has_size || rhs.has_size;
        s.has_lengths = s.has_lengths || rhs.has_lengths;
    }
    [[nodiscard]] std::optional<size_sum> factor() {
        skip_space();
        if (at >= text.size()) { return std::nullopt; }
        if (text[at] == '(' || ascii_iequals(text.substr(at, 5), "calc(")) {
            at += text[at] == '(' ? 1 : 5;
            std::optional<size_sum> inner = sum();
            skip_space();
            if (!inner || at >= text.size() || text[at] != ')') { return std::nullopt; }
            ++at;
            return inner;
        }
        // One token (`size`, a number, a dimension, a percentage) or a whole
        // function, which numeric_of either evaluates or rejects.
        const std::size_t start = at;
        int depth = 0;
        while (at < text.size()) {
            const char c = text[at];
            if (depth == 0 && (c == ' ' || c == '\t' || c == '\n' || c == ')' || c == '*' ||
                               c == '/' || c == ',')) {
                break;
            }
            if (c == '(') { ++depth; }
            if (c == ')') { --depth; }
            ++at;
        }
        const std::string_view token = text.substr(start, at - start);
        if (ascii_iequals(token, "size")) {
            size_sum out;
            out.size = 1;
            out.has_size = true;
            return out;
        }
        const std::optional<numeric_pair> n = numeric_of(token, token, ctx);
        if (!n || (!n->a.is_number && n->a.type != css::numeric_type::length)) {
            return std::nullopt;
        }
        size_sum out;
        out.lengths = n->a;
        out.number = n->a.is_number;
        out.has_lengths = !out.number;
        if (!out.number) {
            out.lengths.type = css::numeric_type::length;
            out.lengths.is_number = false;
        }
        return out;
    }
};

/// Reads `calc` as a size_sum; nullopt unless it parses whole and is not a
/// bare number.
[[nodiscard]] std::optional<size_sum> size_sum_of(std::string_view calc,
                                                  const css::length_context & ctx) {
    size_sum_reader reader{{calc, " \t\n"}, ctx};
    std::optional<size_sum> out = reader.sum();
    reader.skip_space();
    if (!out || reader.at != calc.size() || out->number) { return std::nullopt; }
    return out;
}

/// Blends two calculations over one basis as `a * (1 - p) + b * p`, simplified
/// as calc() simplifies a sum (CSS Values 4 §10.10.1): the lengths fold into
/// one, and `size` keeps a factor of its own, so `auto` to `20px` at 0.3 is
/// `6px + size * 0.7` (WPT interpolate-size-min-height-interpolation). A
/// calculation that is not linear in `size` is kept whole, each side times its
/// weight.
[[nodiscard]] std::string blend_calcs(std::string_view a, std::string_view b, double p,
                                      const css::length_context & ctx) {
    const std::optional<size_sum> x = size_sum_of(a, ctx);
    const std::optional<size_sum> y = size_sum_of(b, ctx);
    if (!x || !y) {
        return "(" + std::string{a} + ") * " + css::serialize_number(1 - p) + " + (" +
               std::string{b} + ") * " + css::serialize_number(p);
    }
    std::string out;
    if (x->has_lengths || y->has_lengths) {
        css::calc_result lengths = mix(x->lengths, y->lengths, p);
        if (lengths.px == 0) { lengths.px = 0; } // no `-0px`
        std::string text = css::serialize_calc(lengths);
        if (ascii_istarts_with(text, "calc(") && text.ends_with(')')) {
            text = text.substr(5, text.size() - 6);
        }
        out = std::move(text);
    }
    if (x->has_size || y->has_size) {
        double k = (1 - p) * x->size + p * y->size;
        if (k == 0) { k = 0; }
        out += (out.empty() ? "size * " : " + size * ") + css::serialize_number(k);
    }
    return out.empty() ? std::string{"0px"} : out;
}

/// Serializes a calc-size() the way the cascade writes the same declaration
/// (calc_size_text), so an animated size reads back as a declared one does:
/// `size * 0.7` is `(0.7 * size)` in both.
[[nodiscard]] std::string sized_text(std::string_view basis, std::string_view calc) {
    // Every keyword a basis can be here; the canonical forms only ever carry a
    // keyword the property accepted.
    constexpr std::string_view sizes = "auto content min-content max-content fit-content stretch";
    std::string text = "calc-size(" + std::string{basis} + ", " + std::string{calc} + ")";
    std::optional<std::string> canonical = css::calc_size_text(text, sizes);
    return canonical ? std::move(*canonical) : text;
}

/// Adds two calculations over one basis; calc-size() is additive (CSS Values 5
/// §calc-size). The lengths are summed and `size`'s factors are summed, so
/// `max-content` with `100px` added is `calc-size(max-content, 100px + size)`
/// (WPT interpolate-size-*-composition).
[[nodiscard]] std::string sum_calcs(std::string_view a, std::string_view b,
                                    const css::length_context & ctx) {
    const std::optional<size_sum> x = size_sum_of(a, ctx);
    const std::optional<size_sum> y = size_sum_of(b, ctx);
    if (!x || !y) { return "(" + std::string{a} + ") + (" + std::string{b} + ")"; }
    std::string out;
    if (x->has_lengths || y->has_lengths) {
        std::string text = css::serialize_calc(sum(x->lengths, y->lengths));
        if (ascii_istarts_with(text, "calc(") && text.ends_with(')')) {
            text = text.substr(5, text.size() - 6);
        }
        out = std::move(text);
    }
    if (x->has_size || y->has_size) {
        const double k = x->size + y->size;
        out += out.empty() ? "" : " + ";
        out += k == 1 ? std::string{"size"} : "size * " + css::serialize_number(k);
    }
    return out.empty() ? std::string{"0px"} : out;
}

/// Interpolates `from` to `to` at progress `p` for `property`. CSS Values 4
/// §4.1: two values interpolate when they are one number, length or percentage
/// each; two colours; or lists of the same shape (comma-separated, then
/// space-separated) whose items pair off as one of those or as identical text
/// (`inset`, `/`, `auto`). `border-width: 20px 40px`, `box-shadow: red 2px 2px`
/// and `background-size: 10px 20%` are all lists. When the pair does not
/// interpolate, `interpolable` is set to false and the answer flips at the
/// midpoint.
[[nodiscard]] std::string interpolate_pair(std::string_view property, std::string_view from,
                                           std::string_view to, double p,
                                           const css::length_context & ctx, bool & interpolable) {
    from = trim(from, html_whitespace);
    to = trim(to, html_whitespace);
    interpolable = true;
    // `display: none` holds only at its own end (CSS Display 4 §2.9): from
    // `none`, the other value shows past 0; to `none`, until 1
    // (display-interpolation-none).
    if (property == "display" && ascii_iequals(from, "none") != ascii_iequals(to, "none")) {
        interpolable = false;
        if (ascii_iequals(from, "none")) { return std::string{p <= 0 ? from : to}; }
        return std::string{p >= 1 ? to : from};
    }
    // A gap rule list. A single value is not one; it pairs below as the lone
    // length or colour it is. Only a top-level comma makes a list:
    // `rgb(170, 70, 70)` is one colour, and treating its inner commas as a list
    // would hand the one item straight back here and recurse forever.
    const auto listed = [](std::string_view text) {
        return split_top_level(text, ",").size() > 1 || ascii_istarts_with(text, "repeat(");
    };
    if (gap_list_property(property) && (listed(from) || listed(to))) {
        if (std::optional<std::string> list = interpolate_gap_list(property, from, to, p, ctx)) {
            return *list;
        }
        interpolable = false;
        return std::string{p < 0.5 ? from : to};
    }
    if (property == "transform") {
        bool singular = false;
        if (const std::optional<std::string> t = interpolate_transform(from, to, p, &singular)) {
            return *t;
        }
        if (singular) {
            interpolable = false;
            return std::string{p < 0.5 ? from : to};
        }
    }
    // Two palettes mix (CSS Fonts 4 §9.1): between the ends the value is
    // `palette-mix(in oklab, from, to p%)`, the percentage left out at the
    // even share, and each end is itself (WPT font-palette-interpolation).
    if (property == "font-palette") {
        if (p <= 0) { return std::string{from}; }
        if (p >= 1) { return std::string{to}; }
        const double percent = p * 100;
        return "palette-mix(in oklab, " + std::string{from} + ", " + std::string{to} +
               (percent == 50 ? std::string{} : " " + css::serialize_number(percent) + "%") + ")";
    }
    // `vertical-align` is three values in one (CSS Inline 3 §4.4), written
    // with its defaults left out: when the keywords agree, the baseline shift
    // interpolates as a length, an omitted one being zero, so `baseline` to
    // `100px` is 0px to 100px (WPT vertical-align-interpolation).
    if (property == "vertical-align") {
        const auto parts = [](std::string_view v) {
            std::string words;
            std::string shift = "0px";
            for (const std::string_view part : split_top_level(v, html_whitespace)) {
                const char c = part.empty() ? ' ' : part.front();
                const bool length = (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' ||
                                    ascii_istarts_with(part, "calc(");
                if (length) {
                    shift = std::string{part};
                } else if (!ascii_iequals(part, "baseline")) {
                    words += (words.empty() ? "" : " ") + ascii_lower_copy(part);
                }
            }
            return std::pair{words, shift};
        };
        const auto [words_a, shift_a] = parts(from);
        const auto [words_b, shift_b] = parts(to);
        if (words_a == words_b) {
            bool ok = true;
            const std::string shift = interpolate_pair("margin-left", shift_a, shift_b, p, ctx, ok);
            if (ok) {
                std::string out = words_a;
                if (shift != "0px") { out += (out.empty() ? "" : " ") + shift; }
                return out.empty() ? std::string{"baseline"} : out;
            }
        }
        interpolable = false;
        return std::string{p < 0.5 ? from : to};
    }
    // Two basic shapes of one function blend (CSS Shapes 1 §3.3); anything
    // else in clip-path, shape-outside, offset-path or object-view-box (a
    // url(), `none`, two different functions) is discrete.
    if (property == "clip-path" || property == "shape-outside" || property == "offset-path" ||
        property == "object-view-box") {
        if (std::optional<std::string> s = css::blend_basic_shapes(property, from, to, 1 - p, p)) {
            return *s;
        }
        interpolable = false;
        return std::string{p < 0.5 ? from : to};
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
        // A degenerate ratio (`1 / 0`, `0 / 1`) is discrete too (CSS Sizing 4
        // §7.1), not the quotient the numeric path below would fold it to
        // (WPT aspect-ratio-interpolation).
        interpolable = false;
        return std::string{p < 0.5 ? from : to};
    }
    if (property == "rotate") {
        if (const std::optional<std::string> r = interpolate_rotate(from, to, p, ctx)) {
            return *r;
        }
    }
    // Two rects blend component by component (WPT clip-interpolation).
    if (property == "clip") {
        if (const std::optional<std::string> r = blend_rects(from, to, 1 - p, p, ctx)) {
            return *r;
        }
        interpolable = false;
        return std::string{p < 0.5 ? from : to};
    }
    // Two track lists of one shape blend track by track (CSS Grid 2 §7.2.5);
    // two different shapes are discrete.
    if (property == "grid-template-columns" || property == "grid-template-rows") {
        if (const std::optional<std::string> t = interpolate_tracks(from, to, 1 - p, p, ctx)) {
            return *t;
        }
        interpolable = false;
        return std::string{p < 0.5 ? from : to};
    }
    // Two filter lists interpolate as filter lists or not at all (Filter
    // Effects 1 §11.2): a url() in either, or two functions that do not pair,
    // is discrete. The generic item-by-item blend below must not see them, as
    // it would keep the url() and blend the functions beside it (WPT
    // backdrop-filter-composition).
    if (property == "filter" || property == "backdrop-filter") {
        if (const std::optional<std::string> f = interpolate_filter(from, to, p, ctx)) {
            return *f;
        }
        interpolable = false;
        return std::string{p < 0.5 ? from : to};
    }
    // A single colour. A comma list of colours is not one: `rgb(1, 2, 3), red`
    // is two items, each paired below, not the first of them alone.
    const bool single = comma_items(from).size() == 1 && comma_items(to).size() == 1;
    if (const auto a = single ? css::resolve_color(from, {}) : std::nullopt,
        b = single ? css::resolve_color(to, {}) : std::nullopt;
        a && b) {
        return legacy_color(from) && legacy_color(to) ? lerp_color(*a, *b, p)
                                                      : lerp_oklab(*a, *b, p);
    }
    // A calc-size() or an intrinsic keyword interpolates as calc-size() does
    // (CSS Values 5 §calc-size): both sides are canonicalized onto one basis
    // and their calculations blended; if they cannot be, the pair is discrete.
    if (sizing_property(property)) {
        bool sized = false;
        if (const std::optional<size_pair_forms> pair = size_pair(from, to, ctx, sized)) {
            return sized_text(pair->basis, blend_calcs(pair->calc_a, pair->calc_b, p, ctx));
        }
        if (sized) {
            interpolable = false;
            return std::string{p < 0.5 ? from : to};
        }
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
            // A shadow's blur (the third length, in computed shape) cannot go
            // negative (CSS Backgrounds 3 §7.2), so an extrapolated one is zero.
            if (shadow && k_out == 4 && piece.starts_with('-')) { piece = "0px"; }
            out += piece;
        }
    }
    return out;
}

/// Adds `value` to `underlying` (Web Animations 1 §4.5.1) for one item:
/// numerics sum, colours sum, identical keywords keep, a list adds item by
/// item, and anything else is `value`.
[[nodiscard]] std::string add_pair(std::string_view property, std::string_view underlying,
                                   std::string_view value, const css::length_context & ctx) {
    underlying = trim(underlying, html_whitespace);
    value = trim(value, html_whitespace);
    if (underlying.empty()) { return std::string{value}; }
    if (property == "clip-path" || property == "shape-outside" || property == "offset-path" ||
        property == "object-view-box") {
        return css::blend_basic_shapes(property, underlying, value, 1, 1)
            .value_or(std::string{value});
    }
    if (property == "clip") {
        return blend_rects(underlying, value, 1, 1, ctx).value_or(std::string{value});
    }
    // Two track lists of one shape add track by track, inside their
    // repeat()s too (WPT grid-template-columns-composition).
    if (property == "grid-template-columns" || property == "grid-template-rows") {
        return interpolate_tracks(underlying, value, 1, 1, ctx).value_or(std::string{value});
    }
    // Two sizes calc-size() puts on one basis add as calc-size()s; a keyword
    // pair it cannot put on one basis is not additive, so the added value
    // replaces.
    if (sizing_property(property)) {
        bool sized = false;
        if (const std::optional<size_pair_forms> pair = size_pair(underlying, value, ctx, sized)) {
            return sized_text(pair->basis, sum_calcs(pair->calc_a, pair->calc_b, ctx));
        }
        if (sized) { return std::string{value}; }
    }
    const bool single = comma_items(underlying).size() == 1 && comma_items(value).size() == 1;
    if (const auto a = single ? css::resolve_color(underlying, {}) : std::nullopt,
        b = single ? css::resolve_color(value, {}) : std::nullopt;
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
                out += add_pair(property, x, y, ctx);
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
