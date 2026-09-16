// CSS Grid 2's value grammars, parsed and serialised - no layout:
//
//   <track-list> and <auto-track-list>   grid-template-columns / -rows
//   <track-size>+                        grid-auto-columns / -rows
//   <grid-line>                          grid-row-start and the other three,
//                                        with grid-row / grid-column / grid-area
//                                        split into them
//   none | <string>+                     grid-template-areas
//   [ row | column ] || dense            grid-auto-flow
//
// The specified form drops empty line-name lists (`[] 150px []` is `150px`)
// and writes a grid line integer-first (`span 1 i` is `span i`); the
// computed form is the same list with every length in pixels and a folded
// calc() clamped to nought - which is the computed value of an element that
// is NOT a grid container. A grid container's resolved value is its used
// track sizes, which need a layout this engine does not do.

#include "internal.hpp"

namespace ctbrowser::style::css {

using namespace detail;

namespace {

struct grid_context {
    const length_context * lengths = nullptr; // computed when set
};

[[nodiscard]] bool custom_ident_ok(std::string_view word) {
    return !is_wide_keyword(word) && !ascii_iequals_any(word, {"span", "auto", "default"});
}

// A cursor over the significant tokens of one value.
struct cursor {
    const token_stream & ts;
    std::vector<std::size_t> at; // significant indices
    std::size_t i = 0;

    [[nodiscard]] bool done() const noexcept { return i >= at.size(); }
    [[nodiscard]] const css_token & peek() const noexcept { return ts.tokens[at[i]]; }
    [[nodiscard]] std::string_view text() const noexcept { return ts.text_of(peek()); }
    [[nodiscard]] bool is_ident(std::string_view word) const noexcept {
        return !done() && peek().type == token_type::ident && ascii_iequals(text(), word);
    }
    // The significant index one past the block a function at `i` opens.
    [[nodiscard]] std::size_t block_end() const noexcept {
        int depth = 0;
        for (std::size_t k = at[i]; k < ts.tokens.size(); ++k) {
            const token_type type = ts.tokens[k].type;
            if (type == token_type::eof) { break; }
            if (type == token_type::function || type == token_type::open_paren ||
                type == token_type::open_square) {
                ++depth;
            } else if (type == token_type::close_paren || type == token_type::close_square) {
                if (--depth == 0) {
                    std::size_t j = i;
                    while (j < at.size() && at[j] <= k) { ++j; }
                    return j;
                }
            }
        }
        return at.size();
    }
    [[nodiscard]] std::string_view slice(std::size_t from, std::size_t to) const noexcept {
        const css_token & first = ts.tokens[at[from]];
        const css_token & last = ts.tokens[at[to - 1]];
        if (first.text >= ts.source_length || last.text >= ts.source_length) { return {}; }
        return std::string_view{ts.pool}.substr(first.text, last.text + last.length - first.text);
    }
};

// A `<length-percentage>` or `<flex>` breadth, canonical; `nullopt` when
// not one. `allow_flex` admits `fr`. A negative literal is a syntax error;
// a folded calc() clamps.
[[nodiscard]] std::optional<std::string> breadth(cursor & c, bool allow_flex,
                                                 const grid_context & ctx) {
    if (c.done()) { return std::nullopt; }
    const css_token & t = c.peek();
    if (t.type == token_type::number && t.number == 0) {
        ++c.i;
        return "0px";
    }
    if (t.type == token_type::percentage) {
        if (t.number < 0) { return std::nullopt; }
        ++c.i;
        return serialize_number(t.number) + "%";
    }
    if (t.type == token_type::dimension) {
        const std::string unit = ascii_lower_copy(c.ts.unit_of(t));
        if (t.number < 0) { return std::nullopt; }
        if (unit == "fr") {
            if (!allow_flex) { return std::nullopt; }
            ++c.i;
            return serialize_number(t.number) + "fr";
        }
        const length_context none;
        const math_answer typed =
            evaluate_math(c.text(), ctx.lengths != nullptr ? *ctx.lengths : none);
        if (typed.outcome == math_outcome::invalid) { return std::nullopt; }
        if (typed.outcome == math_outcome::resolved) {
            if (typed.value.type != numeric_type::length) { return std::nullopt; }
            if (ctx.lengths != nullptr) {
                ++c.i;
                return serialize_calc(typed.value);
            }
        }
        ++c.i;
        return serialize_number(t.number) + unit;
    }
    if (t.type == token_type::function && !may_have_math(c.text())) { return std::nullopt; }
    if (t.type != token_type::function) { return std::nullopt; }
    const std::size_t end = c.block_end();
    const std::string_view text = c.slice(c.i, end);
    if (text.empty()) { return std::nullopt; }
    const length_context none;
    const math_answer answer = evaluate_math(text, ctx.lengths != nullptr ? *ctx.lengths : none);
    if (answer.outcome == math_outcome::invalid) { return std::nullopt; }
    c.i = end;
    if (answer.outcome == math_outcome::resolved) {
        const bool flex = answer.value.type == numeric_type::flex;
        if (answer.value.is_number || (flex && !allow_flex) ||
            (!flex && answer.value.type != numeric_type::length)) {
            return std::nullopt;
        }
        if (ctx.lengths != nullptr) {
            calc_result v = answer.value;
            if (v.px < 0 && !v.has_percent) { v.px = 0; }
            return serialize_calc(v);
        }
    }
    return simplify_math(text);
}

enum class size_kind : std::uint8_t {
    track, // <track-size>
    fixed, // <fixed-size>
};

// `<track-size>` or `<fixed-size>`, canonical.
[[nodiscard]] std::optional<std::string> track_size(cursor & c, size_kind kind,
                                                    const grid_context & ctx) {
    if (c.done()) { return std::nullopt; }
    const css_token & t = c.peek();
    if (t.type == token_type::ident) {
        const std::string word = ascii_lower_copy(c.text());
        if (kind == size_kind::track &&
            (word == "min-content" || word == "max-content" || word == "auto")) {
            ++c.i;
            return word;
        }
        return std::nullopt;
    }
    if (t.type == token_type::function) {
        const std::string_view raw = c.text();
        const std::string fn = ascii_lower_copy(raw.substr(0, raw.size() - 1));
        if (fn == "minmax" || fn == "fit-content") {
            const std::size_t end = c.block_end();
            // The arguments as their own cursor.
            cursor inner{c.ts, {}, 0};
            for (std::size_t k = c.i + 1; k + 1 < end; ++k) { inner.at.push_back(c.at[k]); }
            if (c.ts.tokens[c.at[end - 1]].type != token_type::close_paren) { return std::nullopt; }
            if (fn == "fit-content") {
                const std::optional<std::string> arg = breadth(inner, false, ctx);
                if (!arg || !inner.done() || kind == size_kind::fixed) { return std::nullopt; }
                c.i = end;
                return "fit-content(" + *arg + ")";
            }
            // minmax( <inflexible-breadth> , <track-breadth> ) for a track
            // size; a fixed size needs a <fixed-breadth> on at least one side.
            const auto side = [&](bool allow_flex,
                                  bool allow_keyword) -> std::optional<std::string> {
                if (inner.done()) { return std::nullopt; }
                if (inner.peek().type == token_type::ident) {
                    const std::string word = ascii_lower_copy(inner.text());
                    if (!allow_keyword ||
                        !(word == "min-content" || word == "max-content" || word == "auto")) {
                        return std::nullopt;
                    }
                    ++inner.i;
                    return word;
                }
                return breadth(inner, allow_flex, ctx);
            };
            const bool min_fixed = !inner.done() && inner.peek().type != token_type::ident &&
                                   !(inner.peek().type == token_type::dimension &&
                                     ascii_iequals(c.ts.unit_of(inner.peek()), "fr"));
            const std::optional<std::string> min = side(false, true);
            if (!min || inner.done() || inner.peek().type != token_type::comma) {
                return std::nullopt;
            }
            ++inner.i;
            const bool max_fixed = !inner.done() && inner.peek().type != token_type::ident &&
                                   !(inner.peek().type == token_type::dimension &&
                                     ascii_iequals(c.ts.unit_of(inner.peek()), "fr"));
            const std::optional<std::string> max = side(true, true);
            if (!max || !inner.done()) { return std::nullopt; }
            if (kind == size_kind::fixed && !min_fixed && !max_fixed) { return std::nullopt; }
            c.i = end;
            return "minmax(" + *min + ", " + *max + ")";
        }
    }
    return breadth(c, kind == size_kind::track, ctx);
}

// `[ <custom-ident>* ]`, canonical, or empty for `[]`; `nullopt` when the
// cursor is not at a bracket.
[[nodiscard]] std::optional<std::string> line_names(cursor & c) {
    if (c.done() || c.peek().type != token_type::open_square) { return std::nullopt; }
    std::string out = "[";
    ++c.i;
    bool any = false;
    while (!c.done() && c.peek().type != token_type::close_square) {
        if (c.peek().type != token_type::ident || !custom_ident_ok(c.text())) {
            return std::nullopt;
        }
        if (c.peek().text >= c.ts.source_length) { return std::nullopt; }
        out += (any ? " " : "") + std::string{c.text()};
        any = true;
        ++c.i;
    }
    if (c.done()) { return std::nullopt; }
    ++c.i;
    return any ? out + "]" : std::string{};
}

// `[ <line-names>? <size> ]+ <line-names>?` - the body of a repeat() and of
// the whole list, appended to `out` with single spaces.
[[nodiscard]] bool sized_run(cursor & c, size_kind kind, const grid_context & ctx,
                             std::string & out, bool & any_size, std::size_t stop,
                             bool allow_repeat, bool & saw_auto_repeat);

void append(std::string & out, std::string_view piece) {
    if (piece.empty()) { return; }
    if (!out.empty()) { out += ' '; }
    out += piece;
}

// repeat( ... ), at the cursor. `allow_auto` admits auto-fill/auto-fit once.
[[nodiscard]] std::optional<std::string> repeat_function(cursor & c, size_kind outer,
                                                         const grid_context & ctx, bool allow_auto,
                                                         bool & is_auto) {
    const std::size_t end = c.block_end();
    if (c.ts.tokens[c.at[end - 1]].type != token_type::close_paren) { return std::nullopt; }
    cursor inner{c.ts, {}, 0};
    for (std::size_t k = c.i + 1; k + 1 < end; ++k) { inner.at.push_back(c.at[k]); }
    if (inner.done()) { return std::nullopt; }
    std::string count;
    size_kind kind = outer;
    is_auto = false;
    if (inner.peek().type == token_type::ident) {
        const std::string word = ascii_lower_copy(inner.text());
        if (word != "auto-fill" && word != "auto-fit") { return std::nullopt; }
        if (!allow_auto) { return std::nullopt; }
        is_auto = true;
        kind = size_kind::fixed;
        count = word;
        ++inner.i;
    } else if (inner.peek().type == token_type::number) {
        const css_token & n = inner.peek();
        if ((n.flags & flag_integer) == 0 || n.number < 1) { return std::nullopt; }
        count = serialize_number(n.number);
        ++inner.i;
    } else if (inner.peek().type == token_type::function && may_have_math(inner.text())) {
        const std::size_t e = inner.block_end();
        const math_answer answer = evaluate_math(inner.slice(inner.i, e), length_context{});
        if (answer.outcome != math_outcome::resolved || !answer.value.is_number) {
            return std::nullopt;
        }
        const double rounded = std::round(answer.value.px);
        if (rounded < 1) { return std::nullopt; }
        count = serialize_number(rounded);
        inner.i = e;
    } else {
        return std::nullopt;
    }
    if (inner.done() || inner.peek().type != token_type::comma) { return std::nullopt; }
    ++inner.i;
    std::string body;
    bool any = false;
    bool nested_auto = false;
    if (!sized_run(inner, kind, ctx, body, any, inner.at.size(), false, nested_auto) || !any ||
        !inner.done()) {
        return std::nullopt;
    }
    c.i = end;
    return "repeat(" + count + ", " + body + ")";
}

bool sized_run(cursor & c, size_kind kind, const grid_context & ctx, std::string & out,
               bool & any_size, std::size_t stop, bool allow_repeat, bool & saw_auto_repeat) {
    bool names_pending = false;
    while (c.i < stop && !c.done()) {
        if (c.peek().type == token_type::open_square) {
            if (names_pending) { return false; } // `[a] [b]`
            const std::optional<std::string> names = line_names(c);
            if (!names) { return false; }
            append(out, *names);
            names_pending = true;
            continue;
        }
        if (c.peek().type == token_type::function && ascii_iequals(c.text(), "repeat(")) {
            if (!allow_repeat) { return false; }
            bool is_auto = false;
            const std::optional<std::string> r =
                repeat_function(c, kind, ctx, !saw_auto_repeat, is_auto);
            if (!r) { return false; }
            if (is_auto) { saw_auto_repeat = true; }
            append(out, *r);
            any_size = true;
            names_pending = false;
            continue;
        }
        const std::optional<std::string> size = track_size(c, kind, ctx);
        if (!size) { return false; }
        append(out, *size);
        any_size = true;
        names_pending = false;
    }
    return true;
}

// `<grid-line>` from the cursor to `stop`, canonical.
[[nodiscard]] std::optional<std::string> grid_line(cursor & c, std::size_t stop,
                                                   const grid_context & ctx) {
    bool span = false;
    bool folded = false; // the integer came from a math function
    std::optional<long long> integer;
    std::string ident;
    std::size_t words = 0;
    while (c.i < stop && !c.done()) {
        const css_token & t = c.peek();
        ++words;
        if (t.type == token_type::ident) {
            const std::string_view word = c.text();
            if (ascii_iequals(word, "auto")) {
                if (words != 1) { return std::nullopt; }
                ++c.i;
                if (c.i != stop) { return std::nullopt; }
                return "auto";
            }
            if (ascii_iequals(word, "span")) {
                if (span || integer || !ident.empty()) { return std::nullopt; }
                span = true;
                ++c.i;
                continue;
            }
            if (!custom_ident_ok(word) || !ident.empty()) { return std::nullopt; }
            if (t.text >= c.ts.source_length) {
                ident = serialize_identifier(word);
            } else {
                ident = std::string{word};
            }
            ++c.i;
            continue;
        }
        if (t.type == token_type::number) {
            if ((t.flags & flag_integer) == 0 || integer || t.number == 0) { return std::nullopt; }
            integer = static_cast<long long>(t.number);
            ++c.i;
            continue;
        }
        if (t.type == token_type::function && may_have_math(c.text())) {
            if (integer) { return std::nullopt; }
            const std::size_t end = c.block_end();
            const length_context none;
            const math_answer answer =
                evaluate_math(c.slice(c.i, end), ctx.lengths != nullptr ? *ctx.lengths : none);
            if (answer.outcome == math_outcome::invalid) { return std::nullopt; }
            folded = true;
            if (answer.outcome != math_outcome::resolved || !answer.value.is_number) {
                // Not answered here: keep the text as the integer.
                integer = 1;
            } else {
                double n = std::round(answer.value.px);
                if (std::isnan(n)) { n = 0; }
                integer = static_cast<long long>(n);
            }
            c.i = end;
            continue;
        }
        return std::nullopt;
    }
    if (words == 0) { return std::nullopt; }
    if (span) {
        if (!integer && ident.empty()) { return std::nullopt; }
        // A span below one is a syntax error as a literal and clamps as a
        // folded calc() (CSS Values 4 §10.10).
        if (integer && *integer < 1) {
            if (!folded) { return std::nullopt; }
            integer = 1;
        }
        std::string out = "span";
        if (integer && (*integer != 1 || ident.empty())) { out += " " + std::to_string(*integer); }
        if (!ident.empty()) { out += " " + ident; }
        return out;
    }
    if (integer && *integer == 0) {
        if (!folded) { return std::nullopt; }
        integer = 1;
    }
    if (!integer && ident.empty()) { return std::nullopt; }
    std::string out;
    if (integer) { out = std::to_string(*integer); }
    if (!ident.empty()) { out += (out.empty() ? "" : " ") + ident; }
    return out;
}

[[nodiscard]] std::vector<std::size_t> significant(const token_stream & ts) {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i + 1 < ts.tokens.size(); ++i) {
        if (ts.tokens[i].type != token_type::whitespace) { out.push_back(i); }
    }
    return out;
}

[[nodiscard]] std::optional<std::string> track_list(const token_stream & ts, bool auto_sizes,
                                                    const grid_context & ctx) {
    cursor c{ts, significant(ts), 0};
    if (c.done()) { return std::nullopt; }
    if (!auto_sizes && c.at.size() == 1 && c.is_ident("none")) { return "none"; }
    if (auto_sizes) {
        for (const std::size_t i : c.at) {
            if (ts.tokens[i].type == token_type::open_square) { return std::nullopt; }
        }
    }
    std::string out;
    bool any = false;
    bool saw_auto = false;
    if (!sized_run(c, size_kind::track, ctx, out, any, c.at.size(), !auto_sizes, saw_auto) ||
        !any || !c.done()) {
        return std::nullopt;
    }
    // `grid-auto-columns` is `<track-size>+`, with no line names in it at all -
    // not even the empty `[]`, which writes nothing and so cannot be caught in
    // the output (`[] 1px []`, grid-auto-columns-invalid).
    if (auto_sizes && out.find('[') != std::string::npos) { return std::nullopt; }
    // An <auto-track-list> takes fixed sizes only beside its auto repeat.
    if (saw_auto) {
        cursor check{ts, significant(ts), 0};
        std::string again;
        bool again_any = false;
        bool again_auto = false;
        if (!sized_run(check, size_kind::fixed, ctx, again, again_any, check.at.size(), true,
                       again_auto)) {
            return std::nullopt;
        }
    }
    return out;
}

// `none | <string>+`, each row's cells normalised, the areas rectangular.
[[nodiscard]] std::optional<std::string> template_areas(const token_stream & ts) {
    cursor c{ts, significant(ts), 0};
    if (c.done()) { return std::nullopt; }
    if (c.at.size() == 1 && c.is_ident("none")) { return "none"; }
    std::vector<std::vector<std::string>> rows;
    while (!c.done()) {
        if (c.peek().type != token_type::string) { return std::nullopt; }
        const std::string_view body = ts.value_of(c.peek());
        std::vector<std::string> cells;
        for (const std::string_view cell : split_top_level(body, " \t\n\r\f")) {
            if (cell.empty()) { continue; }
            // A run of `.` is one empty cell; a name is an identifier.
            bool dots = true;
            for (const char ch : cell) { dots = dots && ch == '.'; }
            if (dots) {
                cells.emplace_back(".");
                continue;
            }
            for (const char ch : cell) {
                if (!is_name(ch)) { return std::nullopt; }
            }
            cells.emplace_back(cell);
        }
        if (cells.empty()) { return std::nullopt; }
        rows.push_back(std::move(cells));
        ++c.i;
    }
    for (const auto & row : rows) {
        if (row.size() != rows.front().size()) { return std::nullopt; }
    }
    // Every named area is one rectangle.
    std::vector<std::string> seen;
    for (std::size_t r = 0; r < rows.size(); ++r) {
        for (std::size_t k = 0; k < rows[r].size(); ++k) {
            const std::string & name = rows[r][k];
            if (name == ".") { continue; }
            if (std::ranges::find(seen, name) != seen.end()) { continue; }
            seen.push_back(name);
            std::size_t width = k;
            while (width < rows[r].size() && rows[r][width] == name) { ++width; }
            std::size_t height = r;
            while (height < rows.size() && rows[height][k] == name) { ++height; }
            for (std::size_t rr = 0; rr < rows.size(); ++rr) {
                for (std::size_t kk = 0; kk < rows[rr].size(); ++kk) {
                    const bool inside = rr >= r && rr < height && kk >= k && kk < width;
                    if ((rows[rr][kk] == name) != inside) { return std::nullopt; }
                }
            }
        }
    }
    std::string out;
    for (const auto & row : rows) {
        if (!out.empty()) { out += ' '; }
        out += '"';
        for (std::size_t k = 0; k < row.size(); ++k) { out += (k == 0 ? "" : " ") + row[k]; }
        out += '"';
    }
    return out;
}

[[nodiscard]] std::optional<std::string> auto_flow(const token_stream & ts) {
    cursor c{ts, significant(ts), 0};
    std::string direction;
    bool dense = false;
    while (!c.done()) {
        if (c.peek().type != token_type::ident) { return std::nullopt; }
        const std::string word = ascii_lower_copy(c.text());
        if ((word == "row" || word == "column") && direction.empty()) {
            direction = word;
        } else if (word == "dense" && !dense) {
            dense = true;
        } else {
            return std::nullopt;
        }
        ++c.i;
    }
    if (direction.empty() && !dense) { return std::nullopt; }
    if (direction.empty()) { direction = "row"; }
    return dense ? direction + " dense" : direction;
}

} // namespace

namespace detail {

bool match_grid(std::string_view property, const token_stream & ts, const scan & found,
                std::string & out) {
    (void)found;
    std::optional<std::string> answer;
    if (ascii_iequals(property, "grid-template-columns") ||
        ascii_iequals(property, "grid-template-rows")) {
        answer = track_list(ts, false, {});
    } else if (ascii_iequals(property, "grid-auto-columns") ||
               ascii_iequals(property, "grid-auto-rows")) {
        answer = track_list(ts, true, {});
    } else if (ascii_iequals(property, "grid-template-areas")) {
        answer = template_areas(ts);
    } else if (ascii_iequals(property, "grid-auto-flow")) {
        answer = auto_flow(ts);
    } else if (ascii_iequals_any(property, {"grid-row-start", "grid-row-end", "grid-column-start",
                                            "grid-column-end"})) {
        cursor c{ts, significant(ts), 0};
        answer = grid_line(c, c.at.size(), {});
    } else {
        return false;
    }
    out = answer.value_or(std::string{});
    return true;
}

bool split_grid_lines(std::string_view shorthand, std::string_view value,
                      std::vector<std::string> & out) {
    const token_stream ts = tokenize(value);
    cursor c{ts, significant(ts), 0};
    // The `/`-separated parts.
    std::vector<std::pair<std::size_t, std::size_t>> parts;
    std::size_t start = 0;
    for (std::size_t k = 0; k <= c.at.size(); ++k) {
        const bool slash = k < c.at.size() && ts.tokens[c.at[k]].type == token_type::delim &&
                           ts.text_of(ts.tokens[c.at[k]]) == "/";
        if (k == c.at.size() || slash) {
            parts.emplace_back(start, k);
            start = k + 1;
        }
    }
    const std::size_t wanted = ascii_iequals(shorthand, "grid-area") ? 4 : 2;
    if (parts.empty() || parts.size() > wanted) { return false; }
    std::vector<std::string> lines;
    for (const auto & [from, to] : parts) {
        c.i = from;
        const std::optional<std::string> line = grid_line(c, to, {});
        if (!line || c.i != to) { return false; }
        lines.push_back(*line);
    }
    // An omitted line copies an earlier custom-ident, else `auto`
    // (CSS Grid 2 §8.4).
    const auto is_ident_only = [](const std::string & line) {
        return line != "auto" && !line.starts_with("span") &&
               !(line.front() == '-' || (line.front() >= '0' && line.front() <= '9'));
    };
    while (lines.size() < wanted) {
        const std::string & source =
            wanted == 4 && lines.size() >= 2 ? lines[lines.size() - 2] : lines.front();
        lines.push_back(is_ident_only(source) ? source : "auto");
    }
    out = std::move(lines);
    return true;
}

std::string fold_grid_lines(std::span<const std::string> lines) {
    // The shortest spelling: a trailing `auto` that the expansion would
    // restore is dropped, and so is a copied custom-ident.
    const auto is_ident_only = [](const std::string & line) {
        return line != "auto" && !line.starts_with("span") &&
               !(line.front() == '-' || (line.front() >= '0' && line.front() <= '9'));
    };
    std::vector<std::string> kept(lines.begin(), lines.end());
    const auto implied = [&](std::size_t i) {
        const std::string & source = kept.size() == 4 && i >= 2 ? kept[i - 2] : kept.front();
        return is_ident_only(source) ? source : std::string{"auto"};
    };
    while (kept.size() > 1 && kept.back() == implied(kept.size() - 1)) { kept.pop_back(); }
    std::string out;
    for (std::size_t i = 0; i < kept.size(); ++i) { out += (i == 0 ? "" : " / ") + kept[i]; }
    return out;
}

} // namespace detail

std::string computed_grid(std::string_view property, std::string_view specified,
                          const color_context & ctx) {
    const length_context fallback;
    grid_context gc;
    gc.lengths = ctx.lengths != nullptr ? ctx.lengths : &fallback;
    const token_stream ts = tokenize(trim(specified, html_whitespace));
    if (ascii_iequals(property, "grid-template-columns") ||
        ascii_iequals(property, "grid-template-rows")) {
        return track_list(ts, false, gc).value_or(std::string{});
    }
    if (ascii_iequals(property, "grid-auto-columns") || ascii_iequals(property, "grid-auto-rows")) {
        return track_list(ts, true, gc).value_or(std::string{});
    }
    if (ascii_iequals_any(
            property, {"grid-row-start", "grid-row-end", "grid-column-start", "grid-column-end"})) {
        cursor c{ts, significant(ts), 0};
        return grid_line(c, c.at.size(), gc).value_or(std::string{});
    }
    return {};
}

} // namespace ctbrowser::style::css
