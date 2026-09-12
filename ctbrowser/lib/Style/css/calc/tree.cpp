// calc() - the SPECIFIED value of a sum that will not fold: CSS Values 4 §10.12's
// simplification over the calculation TREE and §10.13's serialisation of it.
//
// The symbolic evaluator answers for a sum of terms - `calc(10px + 1em + 10%)`
// - and the evaluator for one with a magnitude. What neither can answer is a
// sum with a comparison in it that layout has to decide: `calc((min(10px, 20%)
// + max(1rem, 2%)) * 2)` is not a term, and CSS Values 4 does not say to keep
// the author's bytes; it says to simplify the tree - fold what folds around
// the min() and the max() - and print it in a canonical order:
// `calc(2 * (min(10px, 20%) + max(1rem, 2%)))`. calc-serialization-002,
// calc-nesting-002 and the minmax-*-serialize files read that order.
//
// This is that tree, and nothing else: the two evaluators are still asked
// first, and this runs only for the sums they had no answer for.

#include "internal.hpp"

namespace ctbrowser::style::css::detail {

namespace {

struct node {
    enum class kind : std::uint8_t {
        number,    // `value`
        dimension, // `value` in `unit`, which is canonical for a context-free unit
        percent,   // `value`%
        function,  // a math function this tree does not open: `text`
        sum,
        product,
        negate, // one child
        invert, // one child
    };
    kind what = kind::number;
    double value = 0.0;
    std::string unit;
    std::string text;
    std::vector<node> children;

    [[nodiscard]] bool numeric() const noexcept {
        return what == kind::number || what == kind::dimension || what == kind::percent;
    }
};

// One leaf from simplified TEXT: `calc(6px)`, `calc(0.5)`, `calc(10%)` or the
// bare forms, as simplify_math and specified_math print them. Nothing for
// anything else.
[[nodiscard]] std::optional<node> leaf_of(std::string_view text) {
    std::string_view inner = trim(text, html_whitespace);
    if (ascii_istarts_with(inner, "calc(") && inner.ends_with(')')) {
        inner = trim(inner.substr(5, inner.size() - 6), html_whitespace);
    }
    const token_stream ts = tokenize(inner);
    if (ts.tokens.size() != 2) { return std::nullopt; }
    const css_token & t = ts.tokens.front();
    node out;
    if (t.type == token_type::number) {
        out.what = node::kind::number;
        out.value = t.number;
        return out;
    }
    if (t.type == token_type::percentage) {
        out.what = node::kind::percent;
        out.value = t.number;
        return out;
    }
    if (t.type == token_type::dimension) {
        out.what = node::kind::dimension;
        out.value = t.number;
        out.unit = ascii_lower_copy(ts.unit_of(t));
        return out;
    }
    return std::nullopt;
}

// Recursive descent over the tokens of one <calc-sum>, CSS Values 4 §10.1.
// `ok` latches false at the first token that is not arithmetic.
class reader {
public:
    // With `ctx`, the tree is a COMPUTED value: a relative length becomes
    // pixels and a nested function is folded against the bases rather than
    // simplified without them.
    reader(const token_stream & ts, const length_context * ctx) : t_(ts), ctx_(ctx) {}

    [[nodiscard]] std::optional<node> read() {
        std::optional<node> out = sum();
        skip_whitespace();
        if (!out || !ok_ || peek().type != token_type::eof) { return std::nullopt; }
        return out;
    }

private:
    [[nodiscard]] const css_token & peek() const noexcept { return t_.tokens[at_]; }
    void skip_whitespace() noexcept {
        while (peek().type == token_type::whitespace) { ++at_; }
    }
    [[nodiscard]] bool is_delim(char c) const noexcept {
        return peek().type == token_type::delim && t_.text_of(peek()) == std::string_view{&c, 1};
    }
    [[nodiscard]] std::optional<node> fail() {
        ok_ = false;
        return std::nullopt;
    }

    [[nodiscard]] std::optional<node> sum() {
        node out;
        out.what = node::kind::sum;
        std::optional<node> first = product();
        if (!first) { return std::nullopt; }
        out.children.push_back(std::move(*first));
        for (;;) {
            // `+` and `-` between products need whitespace on both sides
            // (§10.1); the tokenizer has folded `-10px` into a number already.
            skip_whitespace();
            if (!is_delim('+') && !is_delim('-')) { break; }
            const bool minus = is_delim('-');
            const bool space_before = at_ > 0 && t_.tokens[at_ - 1].type == token_type::whitespace;
            ++at_;
            if (!space_before || peek().type != token_type::whitespace) { return fail(); }
            std::optional<node> next = product();
            if (!next) { return std::nullopt; }
            if (minus) {
                node negated;
                negated.what = node::kind::negate;
                negated.children.push_back(std::move(*next));
                out.children.push_back(std::move(negated));
            } else {
                out.children.push_back(std::move(*next));
            }
        }
        return out;
    }

    [[nodiscard]] std::optional<node> product() {
        node out;
        out.what = node::kind::product;
        std::optional<node> first = value();
        if (!first) { return std::nullopt; }
        out.children.push_back(std::move(*first));
        for (;;) {
            skip_whitespace();
            if (!is_delim('*') && !is_delim('/')) { break; }
            const bool divide = is_delim('/');
            ++at_;
            skip_whitespace();
            std::optional<node> next = value();
            if (!next) { return std::nullopt; }
            if (divide) {
                node inverted;
                inverted.what = node::kind::invert;
                inverted.children.push_back(std::move(*next));
                out.children.push_back(std::move(inverted));
            } else {
                out.children.push_back(std::move(*next));
            }
        }
        return out;
    }

    [[nodiscard]] std::optional<node> value() {
        skip_whitespace();
        const css_token & tok = peek();
        node out;
        switch (tok.type) {
        case token_type::number:
            out.what = node::kind::number;
            out.value = tok.number;
            ++at_;
            return out;
        case token_type::percentage:
            out.what = node::kind::percent;
            out.value = tok.number;
            ++at_;
            return out;
        case token_type::dimension: {
            const std::string_view unit = t_.unit_of(tok);
            if (!is_known_unit(unit)) { return fail(); }
            out.what = node::kind::dimension;
            out.value = tok.number;
            out.unit = ascii_lower_copy(unit);
            // A context-free unit is written in its family's canonical one:
            // `1in` is `96px` (§10.12); against bases, so is a relative one.
            if (context_free_unit(unit)) {
                if (const std::optional<term> fixed = canonical_term(tok.number, unit, {})) {
                    out.value = fixed->value;
                    out.unit = std::string{canonical_unit(fixed->type())};
                }
            } else if (ctx_ != nullptr) {
                if (const std::optional<double> px = unit_to_px(tok.number, unit, *ctx_)) {
                    out.value = *px;
                    out.unit = "px";
                }
            }
            ++at_;
            return out;
        }
        case token_type::ident: {
            const std::string_view word = t_.text_of(tok);
            out.what = node::kind::number;
            if (ascii_iequals(word, "pi")) {
                out.value = std::numbers::pi;
            } else if (ascii_iequals(word, "e")) {
                out.value = std::numbers::e;
            } else {
                return fail(); // `infinity`, `NaN`: a term the evaluators own
            }
            ++at_;
            return out;
        }
        case token_type::open_paren: {
            ++at_;
            std::optional<node> inner = sum();
            if (!inner) { return std::nullopt; }
            skip_whitespace();
            if (peek().type != token_type::close_paren) { return fail(); }
            ++at_;
            return inner;
        }
        case token_type::function: {
            // A nested calc() is a parenthesis; any other math function is a
            // leaf, simplified on its own by simplify_math.
            const std::size_t close = end_of_function(at_);
            const std::string_view name = t_.text_of(tok);
            if (ascii_iequals(name, "calc(")) {
                std::string inner;
                for (std::size_t i = at_ + 1; i + 1 < close; ++i) {
                    inner += t_.text_of(t_.tokens[i]);
                }
                const token_stream nested = tokenize(inner);
                reader sub{nested, ctx_};
                std::optional<node> read = sub.read();
                if (!read) { return fail(); }
                at_ = close;
                return read;
            }
            if (math_name_at(name, 0).empty()) { return fail(); }
            std::string whole;
            for (std::size_t i = at_; i < close; ++i) { whole += t_.text_of(t_.tokens[i]); }
            at_ = close;
            const std::string simplified =
                ctx_ != nullptr ? fold_math(whole, *ctx_).text : simplify_math(whole);
            if (std::optional<node> leaf = leaf_of(simplified)) { return leaf; }
            out.what = node::kind::function;
            out.text = simplified;
            return out;
        }
        default: return fail();
        }
    }

    // One past the `)` that closes the function at `open`, or the eof.
    [[nodiscard]] std::size_t end_of_function(std::size_t open) const noexcept {
        int depth = 0;
        for (std::size_t i = open; i < t_.tokens.size(); ++i) {
            const token_type type = t_.tokens[i].type;
            if (type == token_type::eof) { return i; }
            if (type == token_type::function || type == token_type::open_paren) { ++depth; }
            if (type == token_type::close_paren && --depth == 0) { return i + 1; }
        }
        return t_.tokens.size() - 1;
    }

    const token_stream & t_;
    const length_context * ctx_;
    std::size_t at_ = 0;
    bool ok_ = true;
};

[[nodiscard]] bool same_unit(const node & a, const node & b) noexcept {
    return a.what == b.what && a.unit == b.unit;
}

// §10.12, bottom up.
[[nodiscard]] node simplify(node root) {
    for (node & child : root.children) { child = simplify(std::move(child)); }
    switch (root.what) {
    case node::kind::negate: {
        node & child = root.children.front();
        if (child.numeric()) {
            child.value = -child.value;
            return std::move(child);
        }
        if (child.what == node::kind::negate) { return std::move(child.children.front()); }
        return root;
    }
    case node::kind::invert: {
        node & child = root.children.front();
        if (child.what == node::kind::number && child.value != 0.0) {
            child.value = 1.0 / child.value;
            return std::move(child);
        }
        if (child.what == node::kind::invert) { return std::move(child.children.front()); }
        return root;
    }
    case node::kind::sum: {
        // Flatten nested sums, then add the numeric children of one unit
        // together, each unit keeping the place of its first term.
        std::vector<node> flat;
        for (node & child : root.children) {
            if (child.what == node::kind::sum) {
                for (node & grand : child.children) { flat.push_back(std::move(grand)); }
            } else {
                flat.push_back(std::move(child));
            }
        }
        std::vector<node> combined;
        for (node & child : flat) {
            if (child.numeric()) {
                bool added = false;
                for (node & held : combined) {
                    if (held.numeric() && same_unit(held, child)) {
                        held.value += child.value;
                        added = true;
                        break;
                    }
                }
                if (added) { continue; }
            }
            combined.push_back(std::move(child));
        }
        if (combined.size() == 1) { return std::move(combined.front()); }
        root.children = std::move(combined);
        return root;
    }
    case node::kind::product: {
        std::vector<node> flat;
        for (node & child : root.children) {
            if (child.what == node::kind::product) {
                for (node & grand : child.children) { flat.push_back(std::move(grand)); }
            } else {
                flat.push_back(std::move(child));
            }
        }
        // Every number multiplied into one, placed first; a lone numeric
        // partner takes the factor into its value.
        double factor = 1.0;
        bool any_number = false;
        std::vector<node> rest;
        for (node & child : flat) {
            if (child.what == node::kind::number) {
                factor *= child.value;
                any_number = true;
            } else {
                rest.push_back(std::move(child));
            }
        }
        if (rest.empty()) {
            node number;
            number.value = factor;
            return number;
        }
        if (rest.size() == 1 && rest.front().numeric()) {
            rest.front().value *= factor;
            return std::move(rest.front());
        }
        if (rest.size() == 1 && !any_number) { return std::move(rest.front()); }
        root.children.clear();
        if (any_number) {
            node number;
            number.value = factor;
            root.children.push_back(std::move(number));
        }
        for (node & child : rest) { root.children.push_back(std::move(child)); }
        return root;
    }
    default: return root;
    }
}

[[nodiscard]] std::string number_text(double value) {
    calc_result number;
    number.px = value;
    number.is_number = true;
    number.type = numeric_type::number;
    return serialize_calc(number);
}

// §10.13.
[[nodiscard]] std::string serialize(const node & root);

[[nodiscard]] std::string serialize_leaf(const node & leaf, bool absolute) {
    const double v = absolute ? std::fabs(leaf.value) : leaf.value;
    switch (leaf.what) {
    case node::kind::number: return number_text(v);
    case node::kind::percent: return number_text(v) + "%";
    case node::kind::dimension: return number_text(v) + leaf.unit;
    default: return leaf.text;
    }
}

// A child in parentheses when it is a sum or a product: `(2 * (10px + min()))`.
[[nodiscard]] std::string serialize_child(const node & child) {
    if (child.what == node::kind::sum || child.what == node::kind::product) {
        return "(" + serialize(child) + ")";
    }
    return serialize(child);
}

[[nodiscard]] std::string serialize(const node & root) {
    switch (root.what) {
    case node::kind::number:
    case node::kind::percent:
    case node::kind::dimension:
    case node::kind::function: return serialize_leaf(root, false);
    case node::kind::negate: return "(-1 * " + serialize_child(root.children.front()) + ")";
    case node::kind::invert: return "(1 / " + serialize_child(root.children.front()) + ")";
    case node::kind::sum: {
        // Numbers, then percentages, then dimensions by unit, then the rest
        // in the order they came.
        std::vector<const node *> sorted;
        for (const node & child : root.children) { sorted.push_back(&child); }
        const auto rank = [](const node * n) {
            switch (n->what) {
            case node::kind::number: return 0;
            case node::kind::percent: return 1;
            case node::kind::dimension: return 2;
            default: return 3;
            }
        };
        std::ranges::stable_sort(sorted, [&](const node * a, const node * b) {
            if (rank(a) != rank(b)) { return rank(a) < rank(b); }
            return rank(a) == 2 && a->unit < b->unit;
        });
        std::string out;
        bool first = true;
        for (const node * child : sorted) {
            if (first) {
                out += serialize_child(*child);
                first = false;
                continue;
            }
            if (child->what == node::kind::negate) {
                out += " - " + serialize_child(child->children.front());
            } else if (child->numeric() && std::signbit(child->value)) {
                out += " - " + serialize_leaf(*child, true);
            } else {
                out += " + " + serialize_child(*child);
            }
        }
        return out;
    }
    case node::kind::product: {
        std::string out;
        bool first = true;
        for (const node & child : root.children) {
            if (first) {
                out += serialize_child(child);
                first = false;
            } else if (child.what == node::kind::invert) {
                out += " / " + serialize_child(child.children.front());
            } else {
                out += " * " + serialize_child(child);
            }
        }
        return out;
    }
    }
    return {};
}

} // namespace

std::optional<std::string> simplify_sum_text(std::string_view expression,
                                             const length_context * ctx) {
    const token_stream ts = tokenize(expression);
    reader read{ts, ctx};
    std::optional<node> tree = read.read();
    if (!tree) { return std::nullopt; }
    return serialize(simplify(std::move(*tree)));
}

} // namespace ctbrowser::style::css::detail
