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
// The three pseudo-classes that are genuinely transient UI state, tracked per node
// by the shell and cleared when the pointer moves.
//
// `:checked` and `:disabled` used to be here and are NOT any more: they are facts
// about the element, and as state bits nothing ever set them - see the note beside
// structural_disabled.
[[nodiscard]] std::uint32_t state_bit_of(std::string_view name) {
    if (ascii_iequals(name, "hover")) { return state_hover; }
    if (ascii_iequals(name, "active")) { return state_active; }
    if (ascii_iequals(name, "focus")) { return state_focus; }
    return 0;
}

// The structural pseudo-classes, which are a question about POSITION rather than
// about UI state - so they need no bit set on the element and no invalidation when
// one changes. `:root` is the whole set at this rung; the rest arrive with the
// facts stack that makes them cheap to answer.
[[nodiscard]] std::uint32_t structural_bit_of(std::string_view name) {
    if (ascii_iequals(name, "root")) { return structural_root; }
    if (ascii_iequals(name, "empty")) { return structural_empty; }
    if (ascii_iequals(name, "first-child")) { return structural_first_child; }
    if (ascii_iequals(name, "last-child")) { return structural_last_child; }
    if (ascii_iequals(name, "only-child")) { return structural_only_child; }
    if (ascii_iequals(name, "first-of-type")) { return structural_first_of_type; }
    if (ascii_iequals(name, "last-of-type")) { return structural_last_of_type; }
    if (ascii_iequals(name, "only-of-type")) { return structural_only_of_type; }
    if (ascii_iequals(name, "disabled")) { return structural_disabled; }
    if (ascii_iequals(name, "enabled")) { return structural_enabled; }
    if (ascii_iequals(name, "checked")) { return structural_checked; }
    if (ascii_iequals(name, "link") || ascii_iequals(name, "any-link")) { return structural_link; }
    if (ascii_iequals(name, "visited")) { return structural_visited; }
    return 0;
}

// `An+B`, from the component values inside an `:nth-child()`. The grammar is
// genuinely awkward because the tokenizer has already decided where the numbers
// are: `2n+1` is a DIMENSION `2n` then a NUMBER `+1`, while `2n + 1` is a dimension,
// whitespace, a delim and a number - and `n+1` starts with an IDENT. So this reads
// the pieces rather than pattern-matching a spelling.
//
// Returns false for anything it cannot read, which makes the whole selector
// unmatchable rather than guessing at a step of 1.
[[nodiscard]] bool parse_nth(const stylesheet & sheet, std::span<const component_value> inner,
                             std::int32_t & a, std::int32_t & b) {
    const auto tok = [&](const component_value & v) -> const css_token & {
        return sheet.tokens[v.token];
    };
    const auto is_ws = [&](const component_value & v) {
        return v.kind == cv_kind::token && tok(v).type == token_type::whitespace;
    };
    // Flatten to the non-whitespace tokens, since whitespace is only a separator
    // here and never significant.
    boost::container::small_vector<const css_token *, 6> parts;
    for (const component_value & v : inner) {
        if (v.kind != cv_kind::token || is_ws(v)) {
            if (v.kind != cv_kind::token) { return false; } // a nested block or function
            continue;
        }
        parts.push_back(&tok(v));
    }
    if (parts.empty()) { return false; }

    // `odd` and `even` are the two keyword forms.
    if (parts.size() == 1 && parts[0]->type == token_type::ident) {
        const std::string_view word = sheet.text_of(*parts[0]);
        if (ascii_iequals(word, "odd")) {
            a = 2;
            b = 1;
            return true;
        }
        if (ascii_iequals(word, "even")) {
            a = 2;
            b = 0;
            return true;
        }
        // A bare `n`, or `-n`: a step of 1 or -1 with no offset.
        if (ascii_iequals(word, "n")) {
            a = 1;
            b = 0;
            return true;
        }
        if (ascii_iequals(word, "-n")) {
            a = -1;
            b = 0;
            return true;
        }
        return false;
    }
    std::size_t at = 0;
    a = 0;
    b = 0;
    bool saw_n = false;
    // The `An` part. A DIMENSION whose unit is `n` (`2n`), or an ident `n`/`-n`.
    if (parts[at]->type == token_type::dimension) {
        const std::string_view unit =
            sheet.pool.empty() ? std::string_view{}
                               : std::string_view{sheet.pool}.substr(
                                     parts[at]->text + parts[at]->length - parts[at]->unit_length,
                                     parts[at]->unit_length);
        if (!ascii_iequals(unit, "n")) { return false; }
        a = static_cast<std::int32_t>(parts[at]->number);
        saw_n = true;
        ++at;
    } else if (parts[at]->type == token_type::ident) {
        const std::string_view word = sheet.text_of(*parts[at]);
        if (ascii_iequals(word, "n")) {
            a = 1;
        } else if (ascii_iequals(word, "-n")) {
            a = -1;
        } else {
            return false;
        }
        saw_n = true;
        ++at;
    } else if (parts[at]->type == token_type::number) {
        // Just `B`, as in `:nth-child(3)`.
        b = static_cast<std::int32_t>(parts[at]->number);
        return at + 1 == parts.size();
    } else {
        return false;
    }
    if (at == parts.size()) { return saw_n; }
    // The `+B` or `-B` part. Either one signed NUMBER token (`2n+1` tokenizes that
    // way), or a `+`/`-` delim followed by an unsigned number (`2n + 1` does).
    if (parts[at]->type == token_type::number) {
        b = static_cast<std::int32_t>(parts[at]->number);
        return at + 1 == parts.size();
    }
    if (parts[at]->type == token_type::delim && at + 1 < parts.size() &&
        parts[at + 1]->type == token_type::number) {
        const std::string_view sign = sheet.text_of(*parts[at]);
        if (sign != "+" && sign != "-") { return false; }
        const auto magnitude = static_cast<std::int32_t>(parts[at + 1]->number);
        b = sign == "-" ? -magnitude : magnitude;
        return at + 2 == parts.size();
    }
    return false;
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

    // `:not(...)`, `:is(...)`, `:where(...)` and the four `nth` forms.
    //
    // The nested selector list is parsed by RECURSING into a fresh selector parser
    // over a scratch sheet-relative run, so a nested selector gets the whole grammar
    // - `:not(.a .b)` and `:is(div > p)` work, because a nested selector's subject is
    // the same element and its combinators walk from the same cursor.
    //
    // `:has()` is deliberately absent. It looks FORWARD at descendants, and the
    // traversal that answers everything else here has not visited them yet - so it
    // would need a second pass over the subtree rather than a lookup. Bootstrap uses
    // none. It stays unmatchable rather than silently wrong.
    [[nodiscard]] bool parse_functional(std::string_view name, const component_value & fn,
                                        building & into) {
        pseudo_ref ref;
        const auto inner = sheet_->children_of(fn);
        if (ascii_iequals(name, "nth-child") || ascii_iequals(name, "nth-last-child") ||
            ascii_iequals(name, "nth-of-type") || ascii_iequals(name, "nth-last-of-type")) {
            ref.kind = ascii_iequals(name, "nth-child")        ? pseudo_kind::nth_child
                       : ascii_iequals(name, "nth-last-child") ? pseudo_kind::nth_last_child
                       : ascii_iequals(name, "nth-of-type")    ? pseudo_kind::nth_of_type
                                                               : pseudo_kind::nth_last_of_type;
            // `of S` is Selectors 4 and changes which elements are counted. Refused
            // rather than ignored: ignoring it would count the wrong set and match
            // the wrong elements, which is worse than not matching at all.
            if (!parse_nth(*sheet_, inner, ref.a, ref.b)) { return false; }
            ++into.classes; // a pseudo-class is class-level
            into.part.pseudos.push_back(std::move(ref));
            return true;
        }
        const bool is_not = ascii_iequals(name, "not");
        const bool is_is = ascii_iequals(name, "is");
        const bool is_where = ascii_iequals(name, "where");
        if (!is_not && !is_is && !is_where) { return false; }
        ref.kind = is_not ? pseudo_kind::not_ : is_is ? pseudo_kind::is_ : pseudo_kind::where_;

        // Parse the argument into a SCRATCH sheet's selector list, then move the
        // results onto the pseudo. A scratch sheet rather than the real one because
        // the real one's `selectors` vector is the rule's own list and a nested
        // selector is not an alternative of it.
        const std::size_t before = sheet_->selectors.size();
        const std::uint32_t count = parse_selector_list(*sheet_, inner, *atoms_);
        for (std::size_t i = 0; i < count; ++i) {
            ref.args.push_back(std::move(sheet_->selectors[before + i]));
        }
        sheet_->selectors.resize(before);
        if (ref.args.empty()) { return false; }
        // An argument this engine cannot represent makes the WHOLE thing
        // unmatchable, and the direction matters: for `:is()` a dead branch could be
        // dropped, but for `:not()` a dead branch would wrongly become "matches
        // nothing, therefore :not passes". Refusing both is the safe reading.
        for (const compiled_selector & arg : ref.args) {
            if (arg.parts.empty() || arg.parts.front().never_matches) { return false; }
        }
        // SPECIFICITY. `:is()` and `:not()` take their most specific argument;
        // `:where()` contributes nothing at all, which is the entire reason it
        // exists.
        if (!is_where) {
            specificity most;
            for (const compiled_selector & arg : ref.args) {
                if (most < arg.spec) { most = arg.spec; }
            }
            into.ids += static_cast<int>(most.ids());
            into.classes += static_cast<int>(most.classes());
            into.tags += static_cast<int>(most.types());
        }
        into.part.pseudos.push_back(std::move(ref));
        return true;
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
        bool pending_pseudo = false; // a `:` was seen and a function follows it
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
                // A FUNCTIONAL PSEUDO-CLASS, and the leading `:` was consumed by the
                // colon branch below - which set `pending_pseudo` so this knows the
                // function is one rather than a stray `f(...)` in a prelude.
                if (!pending_pseudo || compounds.empty()) {
                    dead = true;
                    continue;
                }
                pending_pseudo = false;
                std::string_view name = text(v);
                if (!name.empty() && name.back() == '(') { name.remove_suffix(1); }
                if (!parse_functional(name, v, compounds.back())) { dead = true; }
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
                // A FUNCTION next means `:not(`, `:nth-child(` and friends: the
                // tokenizer folded the name and the `(` into one token, so the colon
                // and the function are two component values. Flag it and let the
                // function branch handle it on the next iteration.
                if (i + 1 < run.size() && run[i + 1].kind == cv_kind::function) {
                    pending_pseudo = true;
                    continue;
                }
                if (i + 1 >= run.size() || run[i + 1].kind != cv_kind::token ||
                    token(run[i + 1]).type != token_type::ident) {
                    dead = true; // a bare `:`
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
