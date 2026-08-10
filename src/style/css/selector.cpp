#include <ctbrowser/style/css/selector.hpp>

#include <cstdint>
#include <span>
#include <string_view>

#include <ctbrowser/core/algorithms.hpp>

namespace ctbrowser::style::css {
namespace {

// The pseudo-classes the engine can actually observe, mapped to the state bits in
// style/selector.hpp. Everything else - `:not()`, `:first-child`, `::before`,
// `:root` - makes the alternative unmatchable at this rung.
//
// `:visited` deserves a word: it is unmatchable here and will STAY unmatchable.
// Chrome restricts it to colour for privacy reasons, and "never matches" is the
// honest subset rather than a gap.
[[nodiscard]] std::uint32_t state_bit_of(std::string_view name) {
    if (ascii_iequals(name, "hover")) { return state_hover; }
    if (ascii_iequals(name, "active")) { return state_active; }
    if (ascii_iequals(name, "focus")) { return state_focus; }
    if (ascii_iequals(name, "checked")) { return state_checked; }
    if (ascii_iequals(name, "disabled")) { return state_disabled; }
    return 0;
}

// The structural pseudo-classes, which are a question about POSITION rather than
// about UI state - so they need no bit set on the element and no invalidation when
// one changes. `:root` is the whole set at this rung; the rest arrive with the
// facts stack that makes them cheap to answer.
[[nodiscard]] std::uint32_t structural_bit_of(std::string_view name) {
    if (ascii_iequals(name, "root")) { return structural_root; }
    return 0;
}

// `[name op "value" i]`, from the component values INSIDE the square block. The
// block itself was already delimited by the tokenizer, so there is no scanning for
// a `]` here and a `]` inside a quoted value cannot end it early.
[[nodiscard]] bool parse_attribute(const stylesheet & sheet, std::span<const component_value> inner,
                                   atom_table & atoms, attribute_match & out) {
    const auto tok = [&](const component_value & v) -> const css_token & {
        return sheet.tokens[v.token];
    };
    const auto is_ws = [&](const component_value & v) {
        return v.kind == cv_kind::token && tok(v).type == token_type::whitespace;
    };
    // Trim, then read: name, then optionally an operator and a value, then
    // optionally a flag.
    while (!inner.empty() && is_ws(inner.front())) { inner = inner.subspan(1); }
    while (!inner.empty() && is_ws(inner.back())) { inner = inner.subspan(0, inner.size() - 1); }
    if (inner.empty() || inner.front().kind != cv_kind::token) { return false; }
    if (tok(inner.front()).type != token_type::ident) { return false; }
    // Attribute names are ASCII case-insensitive in HTML, and the DOM interns them
    // lowercased - so folding here is what makes `[HREF]` match `href`.
    out.name = atoms.intern_lower(sheet.text_of(tok(inner.front())));
    inner = inner.subspan(1);
    while (!inner.empty() && is_ws(inner.front())) { inner = inner.subspan(1); }
    if (inner.empty()) {
        out.op = attr_op::present;
        return true;
    }
    // The operator. `=` is one delim; the others are a delim followed by `=`,
    // because the tokenizer has no compound-operator tokens - `~=` is `~` then `=`.
    if (inner.front().kind != cv_kind::token) { return false; }
    const std::string_view first = sheet.text_of(tok(inner.front()));
    if (first == "=") {
        out.op = attr_op::exact;
        inner = inner.subspan(1);
    } else {
        if (inner.size() < 2 || inner[1].kind != cv_kind::token ||
            sheet.text_of(tok(inner[1])) != "=") {
            return false;
        }
        if (first == "~") {
            out.op = attr_op::includes;
        } else if (first == "|") {
            out.op = attr_op::dash;
        } else if (first == "^") {
            out.op = attr_op::prefix;
        } else if (first == "$") {
            out.op = attr_op::suffix;
        } else if (first == "*") {
            out.op = attr_op::substring;
        } else {
            return false;
        }
        inner = inner.subspan(2);
    }
    while (!inner.empty() && is_ws(inner.front())) { inner = inner.subspan(1); }
    if (inner.empty() || inner.front().kind != cv_kind::token) { return false; }
    // The value is a string or an ident. `[href$=.pdf]` is legal unquoted, and
    // arrives as a dimension-ish run rather than one ident - so anything that is
    // not plainly one token of the right kind is refused rather than guessed at.
    const css_token & value = tok(inner.front());
    if (value.type == token_type::string) {
        out.value = std::string{sheet.text_of(value).substr(1, sheet.text_of(value).size() - 2)};
    } else if (value.type == token_type::ident) {
        out.value = std::string{sheet.text_of(value)};
    } else {
        return false;
    }
    inner = inner.subspan(1);
    // An `i` or `s` flag. `s` is the default, so only `i` changes anything.
    while (!inner.empty() && is_ws(inner.front())) { inner = inner.subspan(1); }
    if (!inner.empty()) {
        if (inner.size() != 1 || inner.front().kind != cv_kind::token ||
            tok(inner.front()).type != token_type::ident) {
            return false;
        }
        const std::string_view flag = sheet.text_of(tok(inner.front()));
        if (ascii_iequals(flag, "i")) {
            out.case_insensitive = true;
        } else if (!ascii_iequals(flag, "s")) {
            return false;
        }
    }
    // An EMPTY value is not the same as no value: `[a=""]` matches an attribute
    // whose value is empty, which `[a]` also does - but `[a^=""]` matches nothing
    // at all, per the spec, and that is the matcher's business rather than ours.
    return true;
}

// One compound selector under construction, plus how it contributes to
// specificity.
struct building {
    compound part;
    int ids = 0;
    int classes = 0; // classes and recognised pseudo-classes both count here
    int tags = 0;
};

// The spec's (a, b, c): ids, then class-level conditions, then type-level ones.
// An ATTRIBUTE selector and a pseudo-class are both class-level, which is why they
// share the counter.
[[nodiscard]] specificity specificity_of(const building & b) {
    return specificity::of(static_cast<std::uint32_t>(b.ids), static_cast<std::uint32_t>(b.classes),
                           static_cast<std::uint32_t>(b.tags));
}

class selector_parser {
public:
    selector_parser(stylesheet & sheet, atom_table & atoms) : sheet_(&sheet), atoms_(&atoms) {}

    [[nodiscard]] std::uint32_t run(std::span<const component_value> prelude) {
        std::uint32_t written = 0;
        std::size_t at = 0;
        while (at <= prelude.size()) {
            // Split on TOP-LEVEL commas only. A comma inside `:not(a, b)` is a
            // child of the function's component value and is never seen here,
            // which is the whole reason the prelude is parsed as component values
            // rather than scanned as text - the old front end split on a bare
            // comma and fragmented such a selector into two halves.
            std::size_t end = at;
            while (end < prelude.size() && !is_comma(prelude[end])) { ++end; }
            emit(prelude.subspan(at, end - at));
            ++written;
            if (end >= prelude.size()) { break; }
            at = end + 1;
        }
        return written;
    }

private:
    [[nodiscard]] bool is_comma(const component_value & v) const {
        return v.kind == cv_kind::token && token(v).type == token_type::comma;
    }
    [[nodiscard]] const css_token & token(const component_value & v) const {
        return sheet_->tokens[v.token];
    }
    [[nodiscard]] std::string_view text(const component_value & v) const {
        return sheet_->text_of(token(v));
    }

    void push_dead() {
        compiled_selector dead;
        compound c;
        c.never_matches = true;
        dead.parts.push_back(std::move(c));
        sheet_->selectors.push_back(std::move(dead));
    }

    // One comma-separated alternative.
    void emit(std::span<const component_value> run) {
        boost::container::small_vector<building, 2> compounds;
        boost::container::small_vector<combinator, 2> links; // left-to-right, size = n-1
        bool dead = false;
        bool want_new_compound = true;
        combinator pending = combinator::none;

        const auto start_compound = [&] {
            if (!compounds.empty()) { links.push_back(pending); }
            compounds.push_back(building{});
            pending = combinator::descendant;
            want_new_compound = false;
        };

        for (std::size_t i = 0; i < run.size(); ++i) {
            const component_value & v = run[i];
            if (v.kind == cv_kind::block) {
                // An attribute selector arrives as ONE block, because the prelude
                // was parsed as component values - so a `]` inside a quoted value
                // cannot end it early and there is nothing to scan for.
                if (v.open != '[') {
                    dead = true; // a stray `(` or `{` in a prelude
                    continue;
                }
                if (want_new_compound || compounds.empty()) { start_compound(); }
                attribute_match match;
                if (!parse_attribute(*sheet_, sheet_->children_of(v), *atoms_, match)) {
                    dead = true;
                    continue;
                }
                building & b = compounds.back();
                b.part.attributes.push_back(std::move(match));
                ++b.classes; // an attribute selector is class-level for specificity
                continue;
            }
            if (v.kind == cv_kind::function) {
                dead = true; // :not(), :is(), :nth-child(), ...
                continue;
            }
            const css_token & t = token(v);
            switch (t.type) {
            case token_type::whitespace:
                // Whitespace is a combinator only if a compound follows it, which
                // is decided when the next thing arrives - a trailing space is
                // not a descendant combinator.
                if (!compounds.empty()) { want_new_compound = true; }
                continue;
            case token_type::ident: {
                if (want_new_compound || compounds.empty()) { start_compound(); }
                building & b = compounds.back();
                if (b.part.tag || b.tags != 0) {
                    // Two type selectors in one compound - `divp` cannot happen
                    // from the tokenizer, so this means something upstream is
                    // wrong rather than that the author wrote something odd.
                    dead = true;
                    continue;
                }
                // Tags fold to lowercase: HTML tag names are ASCII
                // case-insensitive and the DOM interns them lowercased.
                b.part.tag = atoms_->intern_lower(text(v));
                b.tags = 1;
                continue;
            }
            case token_type::hash: {
                if (want_new_compound || compounds.empty()) { start_compound(); }
                building & b = compounds.back();
                // A hash whose body could not be an identifier - `#0d6efd`, `#999` -
                // is not a valid id selector, because an identifier may not begin
                // with a digit. `#fff` IS one, and really does select id="fff".
                if ((t.flags & flag_id_hash) == 0) {
                    dead = true;
                    continue;
                }
                std::string_view name = text(v);
                if (!name.empty()) { name.remove_prefix(1); } // the '#'
                b.part.id = atoms_->intern(name);
                b.ids = 1;
                continue;
            }
            case token_type::delim: {
                const std::string_view d = text(v);
                if (d == ".") {
                    // The class NAME is the next token, which must be an ident.
                    if (i + 1 >= run.size() || run[i + 1].kind != cv_kind::token ||
                        token(run[i + 1]).type != token_type::ident) {
                        dead = true;
                        continue;
                    }
                    if (want_new_compound || compounds.empty()) { start_compound(); }
                    building & b = compounds.back();
                    b.part.classes.push_back(atoms_->intern(text(run[i + 1])));
                    ++b.classes;
                    ++i; // the ident
                    continue;
                }
                if (d == "*") {
                    if (want_new_compound || compounds.empty()) { start_compound(); }
                    // The universal selector constrains nothing and contributes no
                    // specificity - an empty tag atom IS universal here.
                    continue;
                }
                // The three explicit combinators. Whitespace either side is
                // irrelevant, so the pending relation is simply overwritten - which
                // is what makes `a > b`, `a>b` and `a >b` one selector.
                if (d == ">" || d == "+" || d == "~") {
                    if (compounds.empty()) {
                        dead = true; // a combinator with nothing on its left
                        continue;
                    }
                    pending = d == ">"   ? combinator::child
                              : d == "+" ? combinator::next_sibling
                                         : combinator::subsequent_sibling;
                    want_new_compound = true;
                    continue;
                }
                // `|` is a namespace separator, and the rest cannot appear in a
                // selector at all.
                dead = true;
                continue;
            }
            case token_type::colon: {
                if (want_new_compound || compounds.empty()) { start_compound(); }
                // `::` is a pseudo-ELEMENT. Never matches an element, and the
                // engine generates no boxes for one yet.
                if (i + 1 < run.size() && run[i + 1].kind == cv_kind::token &&
                    token(run[i + 1]).type == token_type::colon) {
                    dead = true;
                    ++i;
                    if (i + 1 < run.size()) { ++i; } // and its name
                    continue;
                }
                if (i + 1 >= run.size() || run[i + 1].kind != cv_kind::token ||
                    token(run[i + 1]).type != token_type::ident) {
                    dead = true; // `:` alone, or `:not(` which arrived as a function
                    continue;
                }
                const std::string_view name = text(run[i + 1]);
                ++i;
                building & b = compounds.back();
                if (const std::uint32_t bit = state_bit_of(name); bit != 0) {
                    b.part.states |= bit;
                    ++b.classes; // a pseudo-class is class-level for specificity
                    continue;
                }
                if (const std::uint32_t bit = structural_bit_of(name); bit != 0) {
                    b.part.structural |= bit;
                    ++b.classes;
                    continue;
                }
                dead = true;
                continue;
            }
            default:
                // A number, a string, a percentage - none of them can appear in a
                // selector at this level.
                dead = true;
                continue;
            }
        }

        if (dead || compounds.empty()) {
            push_dead();
            return;
        }

        compiled_selector out;
        specificity spec;
        for (const building & b : compounds) { spec = spec + specificity_of(b); }
        out.spec = spec;
        // RIGHTMOST FIRST, because that is the order matching walks them. `links`
        // was built left-to-right, and reversing means each link is read from the
        // compound to its right - which is the direction the walk moves.
        for (std::size_t i = compounds.size(); i-- > 0;) {
            out.parts.push_back(compounds[i].part);
            if (i > 0) { out.links.push_back(links[i - 1]); }
        }
        sheet_->selectors.push_back(std::move(out));
    }

    stylesheet * sheet_;
    atom_table * atoms_;
};

} // namespace

std::uint32_t parse_selector_list(stylesheet & sheet, std::span<const component_value> prelude,
                                  atom_table & atoms) {
    selector_parser parser{sheet, atoms};
    return parser.run(prelude);
}

} // namespace ctbrowser::style::css
