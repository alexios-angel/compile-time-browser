#include "internal.hpp"

namespace ctbrowser::style::css::color_detail {

[[nodiscard]] bool parse_time_answerable(const token_stream & ts, std::size_t begin,
                                         std::size_t end) {
    for (std::size_t i = begin; i < end; ++i) {
        const css_token & t = ts.tokens[i];
        if (t.type == token_type::dimension &&
            !ascii_iequals_any(ts.unit_of(t), context_free_units)) {
            return false;
        }
        if (t.type == token_type::function) {
            const std::string_view raw = ts.text_of(t);
            const std::string_view name = raw.substr(0, raw.size() - 1);
            if (ascii_iequals_any(name, {"sibling-index", "sibling-count", "random", "var", "env",
                                         "attr", "random-item", "inherit"})) {
                return false;
            }
        }
    }
    return true;
}

class reader {
public:
    reader(const token_stream & ts, std::size_t begin, std::size_t end)
        : ts_(ts), at_(begin), end_(end) {}

    [[nodiscard]] std::unique_ptr<parsed> read_whole() {
        skip_ws();
        std::unique_ptr<parsed> out = color();
        if (!out) { return nullptr; }
        skip_ws();
        if (at_ != end_) { return nullptr; }
        return out;
    }

private:
    [[nodiscard]] const css_token & peek() const noexcept {
        return at_ < end_ ? ts_.tokens[at_] : ts_.tokens.back(); // the eof
    }
    void skip_ws() noexcept {
        while (at_ < end_ && ts_.tokens[at_].type == token_type::whitespace) { ++at_; }
    }
    [[nodiscard]] bool at_close() const noexcept {
        return at_ >= end_ || ts_.tokens[at_].type == token_type::close_paren;
    }
    // Consumes the `)` that closes the function being read; EOF closes it too.
    [[nodiscard]] bool close() noexcept {
        skip_ws();
        if (at_ >= end_) { return true; }
        if (ts_.tokens[at_].type != token_type::close_paren) { return false; }
        ++at_;
        return true;
    }
    [[nodiscard]] bool take_comma() noexcept {
        skip_ws();
        if (peek().type != token_type::comma) { return false; }
        ++at_;
        skip_ws();
        return true;
    }
    [[nodiscard]] std::string_view function_name_at(std::size_t i) const noexcept {
        const std::string_view raw = ts_.text_of(ts_.tokens[i]);
        return raw.empty() ? raw : raw.substr(0, raw.size() - 1);
    }
    // One past the `)` matching the function at `open`, or `end_`.
    [[nodiscard]] std::size_t matching_close(std::size_t open) const noexcept {
        int depth = 0;
        for (std::size_t i = open; i < end_; ++i) {
            const token_type type = ts_.tokens[i].type;
            if (type == token_type::function || type == token_type::open_paren ||
                type == token_type::open_square || type == token_type::open_curly) {
                ++depth;
            } else if (type == token_type::close_paren || type == token_type::close_square ||
                       type == token_type::close_curly) {
                if (--depth == 0) { return i + 1; }
            }
        }
        return end_;
    }
    [[nodiscard]] std::string text_between(std::size_t begin, std::size_t end) const {
        std::string out;
        for (std::size_t i = begin; i < end; ++i) { out += ts_.text_of(ts_.tokens[i]); }
        return out;
    }

    // --- <color> ---
    [[nodiscard]] std::unique_ptr<parsed> color() {
        const css_token & t = peek();
        if (t.type == token_type::ident) {
            const std::string word = ascii_lower_copy(ts_.text_of(t));
            auto out = std::make_unique<parsed>();
            out->k = parsed::kind::keyword;
            out->keyword = word;
            if (word == "currentcolor" || word == "transparent" || word.starts_with("-webkit-") ||
                find_named(named_colors, word) != nullptr ||
                find_named(system_colors, word) != nullptr) {
                ++at_;
                return out;
            }
            return nullptr;
        }
        if (t.type == token_type::hash) {
            const std::string_view digits = ts_.text_of(t).substr(1);
            if (digits.size() != 3 && digits.size() != 4 && digits.size() != 6 &&
                digits.size() != 8) {
                return nullptr;
            }
            std::array<int, 8> v{};
            for (std::size_t i = 0; i < digits.size(); ++i) {
                v[i] = hex_value(digits[i]);
                if (v[i] < 0) { return nullptr; }
            }
            auto out = std::make_unique<parsed>();
            out->k = parsed::kind::hex;
            out->fixed.legacy = true;
            if (digits.size() <= 4) {
                for (std::size_t i = 0; i < 3; ++i) { out->fixed.c[i] = v[i] * 17 / 255.0; }
                if (digits.size() == 4) { out->fixed.alpha = v[3] * 17 / 255.0; }
            } else {
                for (std::size_t i = 0; i < 3; ++i) {
                    out->fixed.c[i] = (v[2 * i] * 16 + v[2 * i + 1]) / 255.0;
                }
                if (digits.size() == 8) { out->fixed.alpha = (v[6] * 16 + v[7]) / 255.0; }
            }
            ++at_;
            return out;
        }
        if (t.type != token_type::function) { return nullptr; }
        const std::string name = ascii_lower_copy(function_name_at(at_));
        ++at_;
        skip_ws();
        std::unique_ptr<parsed> out;
        if (name == "rgb" || name == "rgba") {
            out = color_function("rgb", space::srgb);
        } else if (name == "hsl" || name == "hsla") {
            out = color_function("hsl", space::hsl);
        } else if (name == "hwb") {
            out = color_function("hwb", space::hwb);
        } else if (name == "lab" || name == "lch" || name == "oklab" || name == "oklch") {
            out = color_function(name, name == "lab"     ? space::lab
                                       : name == "lch"   ? space::lch
                                       : name == "oklab" ? space::oklab
                                                         : space::oklch);
        } else if (name == "color") {
            out = color_function("color", space::srgb);
        } else if (name == "color-mix") {
            out = color_mix();
        } else if (name == "light-dark") {
            out = two_colors(parsed::kind::light_dark);
        } else if (name == "alpha") {
            out = alpha_function();
        } else if (name == "contrast-color") {
            out = std::make_unique<parsed>();
            out->k = parsed::kind::contrast;
            out->origin = color();
            if (!out->origin) { return nullptr; }
        } else if (name == "color-layers") {
            out = color_layers();
        } else {
            return nullptr;
        }
        if (!out || !close()) { return nullptr; }
        return out;
    }

    // rgb() hsl() hwb() lab() lch() oklab() oklch() color(), absolute or
    // relative. `at_` is past the `(` and any whitespace.
    [[nodiscard]] std::unique_ptr<parsed> color_function(std::string fn, space cs) {
        auto out = std::make_unique<parsed>();
        out->k = parsed::kind::absolute;
        out->fn = std::move(fn);
        out->cs = cs;
        out->bytes = out->fn == "rgb";
        if (peek().type == token_type::ident && ascii_iequals(ts_.text_of(peek()), "from")) {
            ++at_;
            skip_ws();
            out->k = parsed::kind::relative;
            out->origin = color();
            if (!out->origin) { return nullptr; }
            skip_ws();
        }
        if (out->fn == "color") {
            if (peek().type != token_type::ident) { return nullptr; }
            const std::optional<space> named_space = predefined_space(ts_.text_of(peek()));
            if (!named_space) { return nullptr; }
            out->cs = *named_space;
            ++at_;
            skip_ws();
        }
        const bool relative = out->k == parsed::kind::relative;
        const std::array<std::string_view, 3> keywords = channel_keywords(out->cs);
        // The first channel decides the syntax: a comma after it is the legacy
        // form, which only rgb() and hsl() have and a relative colour never does.
        for (std::size_t slot = 0; slot < 3; ++slot) {
            std::optional<channel> one = read_channel(out->cs, static_cast<int>(slot), relative,
                                                      out->legacy_syntax, keywords);
            if (!one) { return nullptr; }
            out->ch[slot] = std::move(*one);
            skip_ws();
            if (slot == 0) {
                if (peek().type == token_type::comma) {
                    if (relative || (out->fn != "rgb" && out->fn != "hsl")) { return nullptr; }
                    out->legacy_syntax = true;
                    // `none` is not part of the legacy grammar.
                    if (out->ch[0].k == channel::kind::none) { return nullptr; }
                }
            }
            if (slot < 2) {
                if (out->legacy_syntax) {
                    if (!take_comma()) { return nullptr; }
                } else if (peek().type == token_type::comma) {
                    return nullptr;
                }
            }
        }
        skip_ws();
        if (out->legacy_syntax) {
            if (peek().type == token_type::comma) {
                ++at_;
                skip_ws();
                out->alpha = read_alpha(relative, true, keywords);
                if (!out->alpha) { return nullptr; }
            }
        } else if (peek().type == token_type::delim && ts_.text_of(peek()) == "/") {
            ++at_;
            skip_ws();
            out->alpha = read_alpha(relative, false, keywords);
            if (!out->alpha) { return nullptr; }
        }
        skip_ws();
        if (!at_close()) { return nullptr; }
        // The legacy forms: rgb() takes numbers or percentages but not both,
        // hsl() a number-or-angle hue and two percentages.
        if (out->legacy_syntax) {
            if (out->fn == "rgb") {
                const auto kind_of = [](const channel & c) {
                    return c.k == channel::kind::calc ? c.resolved_kind : c.k;
                };
                std::optional<bool> percent; // decided by the first channel with an answer
                for (const channel & c : out->ch) {
                    if (c.k == channel::kind::none) { return nullptr; }
                    if (c.k == channel::kind::calc && !c.resolvable) { continue; }
                    const bool this_percent = kind_of(c) == channel::kind::percent;
                    if (percent && *percent != this_percent) { return nullptr; }
                    percent = this_percent;
                }
            } else {
                for (std::size_t slot = 1; slot < 3; ++slot) {
                    const channel & c = out->ch[slot];
                    if (c.k == channel::kind::number) { return nullptr; }
                    if (c.k == channel::kind::calc && c.resolvable &&
                        c.resolved_kind == channel::kind::number) {
                        return nullptr;
                    }
                }
            }
            if (out->alpha && out->alpha->k == channel::kind::none) { return nullptr; }
        }
        return out;
    }

    // One channel of a colour function. `slot` 0..2, or 3 for the alpha.
    [[nodiscard]] std::optional<channel> read_channel(space cs, int slot, bool relative,
                                                      bool legacy,
                                                      std::span<const std::string_view> keywords) {
        const css_token & t = peek();
        channel out;
        const bool hue = slot == hue_slot(cs);
        const bool takes_percent = cs != space::hsl && cs != space::hwb ? true : slot != 0;
        switch (t.type) {
        case token_type::number:
            out.k = channel::kind::number;
            out.value = t.number;
            ++at_;
            return out;
        case token_type::percentage:
            if (!takes_percent || hue) { return std::nullopt; }
            out.k = channel::kind::percent;
            out.value = t.number;
            ++at_;
            return out;
        case token_type::dimension: {
            if (!hue) { return std::nullopt; }
            const std::optional<double> degrees = angle_degrees(t.number, ts_.unit_of(t));
            if (!degrees) { return std::nullopt; }
            out.k = channel::kind::angle;
            out.value = *degrees;
            ++at_;
            return out;
        }
        case token_type::ident: {
            const std::string_view word = ts_.text_of(t);
            if (ascii_iequals(word, "none")) {
                if (legacy) { return std::nullopt; }
                out.k = channel::kind::none;
                ++at_;
                return out;
            }
            if (!relative) { return std::nullopt; }
            if (!ascii_iequals(word, "alpha") && !ascii_iequals_any(word, keywords)) {
                return std::nullopt;
            }
            out.k = channel::kind::keyword;
            out.text = ascii_lower_copy(word);
            ++at_;
            return out;
        }
        case token_type::function: {
            if (!ascii_iequals_any(function_name_at(at_), math_names)) { return std::nullopt; }
            const std::size_t open = at_;
            const std::size_t after = matching_close(open);
            out.k = channel::kind::calc;
            out.raw_calc = text_between(open, after);
            if (after == end_ || ts_.tokens[after - 1].type != token_type::close_paren) {
                out.raw_calc += ')';
            }
            at_ = after;
            // The relative colour's keywords are <number> terms of the
            // expression; the specified form is simplified over them.
            std::vector<std::string_view> symbols;
            if (relative) {
                symbols.assign(keywords.begin(), keywords.end());
                symbols.emplace_back("alpha");
            }
            const number_symbols_scope scope{symbols};
            // OF A TYPE THE SLOT TAKES: a number, a percentage, or in the hue
            // slot an angle. `calc(h + 1deg)` adds a number to an angle and
            // `calc(0.56turn * -0.43turn)` is an angle squared; both are
            // syntax errors wherever they sit.
            const auto [outcome, sum] = evaluate_symbolic(out.raw_calc);
            if (outcome == math_outcome::invalid) { return std::nullopt; }
            if (outcome == math_outcome::resolved) {
                if (!sum.simple()) { return std::nullopt; }
                const numeric_type type = sum.type();
                // A percentage travels through the evaluator as a length with
                // an unresolved part; one with no pixels is a <percentage>.
                const bool percentage =
                    type == numeric_type::length && sum.has_percent && sum.value == 0.0;
                if (type == numeric_type::angle) {
                    if (!hue) { return std::nullopt; }
                } else if (type != numeric_type::number && !percentage) {
                    return std::nullopt;
                }
                if (sum.has_percent && (!takes_percent || hue)) { return std::nullopt; }
            }
            out.text = simplify_math(out.raw_calc);
            if (!relative && parse_time_answerable(ts_, open, after)) {
                const math_answer answer = evaluate_math(out.raw_calc, length_context{});
                // `calc(0.56turn * -0.43turn)` is an angle squared: the
                // symbolic pass cannot type it and the evaluator refuses it.
                if (answer.outcome == math_outcome::invalid) { return std::nullopt; }
                if (answer.outcome == math_outcome::resolved) {
                    out.resolvable = true;
                    if (answer.value.has_percent && answer.value.px == 0.0) {
                        out.resolved_kind = channel::kind::percent;
                        out.resolved_value = answer.value.percent;
                    } else if (answer.value.type == numeric_type::angle) {
                        out.resolved_kind = channel::kind::angle;
                        out.resolved_value = answer.value.px;
                    } else {
                        out.resolved_kind = channel::kind::number;
                        out.resolved_value = answer.value.px;
                    }
                }
            }
            return out;
        }
        default: return std::nullopt;
        }
    }

    [[nodiscard]] std::optional<channel> read_alpha(bool relative, bool legacy,
                                                    std::span<const std::string_view> keywords) {
        // The alpha slot takes a number or a percentage, never an angle; the
        // hsl/hwb "hue slot" logic is sidestepped by asking as sRGB's slot 3.
        return read_channel(space::srgb, 3, relative, legacy, keywords);
    }

    [[nodiscard]] static std::optional<double> angle_degrees(double v, std::string_view unit) {
        if (ascii_iequals(unit, "deg")) { return v; }
        if (ascii_iequals(unit, "grad")) { return v * 0.9; }
        if (ascii_iequals(unit, "rad")) { return v * 180.0 / std::numbers::pi; }
        if (ascii_iequals(unit, "turn")) { return v * 360.0; }
        return std::nullopt;
    }

    // color-mix( <color-interpolation-method>? , [ <color> && <percentage>? ]#{1,} )
    [[nodiscard]] std::unique_ptr<parsed> color_mix() {
        auto out = std::make_unique<parsed>();
        out->k = parsed::kind::mix;
        if (peek().type == token_type::ident && ascii_iequals(ts_.text_of(peek()), "in")) {
            ++at_;
            skip_ws();
            if (peek().type != token_type::ident) { return nullptr; }
            const std::optional<space> s = interpolation_space(ts_.text_of(peek()));
            if (!s) { return nullptr; }
            out->mix_space = *s;
            ++at_;
            skip_ws();
            if (peek().type == token_type::ident) {
                const std::string method = ascii_lower_copy(ts_.text_of(peek()));
                if (!ascii_iequals_any(method, {"shorter", "longer", "increasing", "decreasing"})) {
                    return nullptr;
                }
                if (hue_slot(*s) < 0) { return nullptr; }
                ++at_;
                skip_ws();
                if (peek().type != token_type::ident ||
                    !ascii_iequals(ts_.text_of(peek()), "hue")) {
                    return nullptr;
                }
                ++at_;
                skip_ws();
                if (method != "shorter") { out->hue_method = method; }
            }
            if (!take_comma()) { return nullptr; }
        }
        for (;;) {
            mix_item item;
            // The percentage may come before or after the colour.
            if (!read_weight(item)) { return nullptr; }
            skip_ws();
            item.color = color();
            if (!item.color) { return nullptr; }
            skip_ws();
            if (!item.weight && !read_weight(item)) { return nullptr; }
            out->items.push_back(std::move(item));
            skip_ws();
            if (peek().type == token_type::comma) {
                ++at_;
                skip_ws();
                continue;
            }
            break;
        }
        if (out->items.empty()) { return nullptr; }
        return out;
    }

    // A weight for color-mix: `<percentage [0,100]>`, or a math function.
    [[nodiscard]] bool read_weight(mix_item & item) {
        const css_token & t = peek();
        if (t.type == token_type::percentage) {
            if (t.number < 0 || t.number > 100) { return false; }
            channel w;
            w.k = channel::kind::percent;
            w.value = t.number;
            item.weight = w;
            ++at_;
            return true;
        }
        if (t.type == token_type::function &&
            ascii_iequals_any(function_name_at(at_), math_names)) {
            static constexpr std::array<std::string_view, 3> none_keywords{"alpha", "alpha",
                                                                           "alpha"};
            std::optional<channel> w = read_channel(space::srgb, 3, false, false, none_keywords);
            if (!w) { return false; }
            if (w->resolvable) {
                if (w->resolved_kind != channel::kind::percent) { return false; }
                if (w->resolved_value < 0 || w->resolved_value > 100) { return false; }
            }
            item.weight = std::move(*w);
            return true;
        }
        return true; // no weight here
    }

    [[nodiscard]] std::unique_ptr<parsed> two_colors(parsed::kind k) {
        auto out = std::make_unique<parsed>();
        out->k = k;
        for (int i = 0; i < 2; ++i) {
            mix_item item;
            item.color = color();
            if (!item.color) { return nullptr; }
            out->items.push_back(std::move(item));
            if (i == 0 && !take_comma()) { return nullptr; }
        }
        return out;
    }

    // alpha( from <color> / <alpha-value> ), CSS Color 5.
    [[nodiscard]] std::unique_ptr<parsed> alpha_function() {
        auto out = std::make_unique<parsed>();
        out->k = parsed::kind::alpha_fn;
        if (peek().type != token_type::ident || !ascii_iequals(ts_.text_of(peek()), "from")) {
            return nullptr;
        }
        ++at_;
        skip_ws();
        out->origin = color();
        if (!out->origin) { return nullptr; }
        skip_ws();
        if (peek().type != token_type::delim || ts_.text_of(peek()) != "/") { return nullptr; }
        ++at_;
        skip_ws();
        static constexpr std::array<std::string_view, 3> none_keywords{"alpha", "alpha", "alpha"};
        out->alpha = read_alpha(true, false, none_keywords);
        if (!out->alpha) { return nullptr; }
        return out;
    }

    // color-layers( [ <blend-mode> , ]? <color># )
    [[nodiscard]] std::unique_ptr<parsed> color_layers() {
        auto out = std::make_unique<parsed>();
        out->k = parsed::kind::layers;
        if (peek().type == token_type::ident) {
            const std::string word = ascii_lower_copy(ts_.text_of(peek()));
            if (ascii_iequals_any(word, {"normal", "multiply", "screen", "overlay", "darken",
                                         "lighten", "color-dodge", "color-burn", "hard-light",
                                         "soft-light", "difference", "exclusion", "hue",
                                         "saturation", "color", "luminosity"})) {
                ++at_;
                if (!take_comma()) { return nullptr; }
                if (word != "normal") { out->blend_mode = word; }
            }
        }
        for (;;) {
            mix_item item;
            item.color = color();
            if (!item.color) { return nullptr; }
            out->items.push_back(std::move(item));
            skip_ws();
            if (peek().type == token_type::comma) {
                ++at_;
                skip_ws();
                continue;
            }
            break;
        }
        return out;
    }

    const token_stream & ts_;
    std::size_t at_;
    std::size_t end_;
};

// Reader entry point.
[[nodiscard]] std::unique_ptr<parsed> parse_text(std::string_view text) {
    const token_stream ts = tokenize(text);
    reader in{ts, 0, ts.tokens.size() - 1};
    return in.read_whole();
}

} // namespace ctbrowser::style::css::color_detail

namespace ctbrowser::style::css {

using namespace color_detail;

namespace detail {

bool match_color(const token_stream & ts, const scan & found, std::string_view normalized,
                 std::string & out) {
    if (found.significant.empty()) { return false; }
    // `device-cmyk()` is accepted as written: CSS Color 5 defines it and
    // nothing here computes it.
    const css_token & first = ts.tokens[found.significant.front()];
    if (first.type == token_type::function && ascii_iequals(ts.text_of(first), "device-cmyk(") &&
        ts.tokens[found.significant.back()].type == token_type::close_paren) {
        out = std::string{normalized};
        return true;
    }
    reader in{ts, 0, ts.tokens.size() - 1};
    const std::unique_ptr<parsed> tree = in.read_whole();
    if (!tree) { return false; }
    out = serialize_specified(*tree, false);
    return true;
}

} // namespace detail

} // namespace ctbrowser::style::css
