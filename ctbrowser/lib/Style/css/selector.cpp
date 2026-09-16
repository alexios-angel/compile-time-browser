#include <ctbrowser/style/css/selector.hpp>

#include <cstdint>
#include <span>
#include <string>
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
// by the shell and cleared when the pointer moves. `:checked` and `:disabled` are
// NOT among them: they are facts about the element - see structural_disabled.
[[nodiscard]] std::uint32_t state_bit_of(std::string_view name) {
    if (ascii_iequals(name, "hover")) { return state_hover; }
    if (ascii_iequals(name, "active")) { return state_active; }
    if (ascii_iequals(name, "focus")) { return state_focus; }
    // `:target` is UI state of the same shape: one element at a time, set by the
    // shell (from the URL fragment rather than the pointer), no document fact.
    if (ascii_iequals(name, "target")) { return state_target; }
    if (ascii_iequals(name, "focus-within")) { return state_focus_within; }
    if (ascii_iequals(name, "focus-visible")) { return state_focus_visible; }
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
    if (ascii_iequals(name, "scope")) { return structural_scope; }
    return 0;
}

// THE NAMES CSS HAS DEFINED, because Selectors 4 §3.1 says a pseudo-class or
// pseudo-element it has not is a syntax error: `:gibberish` and `::part` with no
// argument are not selectors in any browser, and `css/cssom/invalid-pseudo-elements`
// asserts a rule carrying one is not in the sheet at all. What these lists decide
// is only VALID or NOT - a name here that the matcher does not model still makes
// the compound unmatchable, exactly as before.

[[nodiscard]] bool known_pseudo_class(std::string_view name) {
    static constexpr std::string_view names[] = {"active",
                                                 "any-link",
                                                 "autofill",
                                                 "blank",
                                                 "buffering",
                                                 "checked",
                                                 "current",
                                                 "default",
                                                 "defined",
                                                 "disabled",
                                                 "empty",
                                                 "enabled",
                                                 "first",
                                                 "first-child",
                                                 "first-of-type",
                                                 "focus",
                                                 "focus-visible",
                                                 "focus-within",
                                                 "fullscreen",
                                                 "future",
                                                 "host",
                                                 "hover",
                                                 "in-range",
                                                 "indeterminate",
                                                 "invalid",
                                                 "last-child",
                                                 "last-of-type",
                                                 "left",
                                                 "link",
                                                 "local-link",
                                                 "modal",
                                                 "muted",
                                                 "only-child",
                                                 "only-of-type",
                                                 "open",
                                                 "optional",
                                                 "out-of-range",
                                                 "past",
                                                 "paused",
                                                 "picture-in-picture",
                                                 "placeholder-shown",
                                                 "playing",
                                                 "popover-open",
                                                 "read-only",
                                                 "read-write",
                                                 "required",
                                                 "right",
                                                 "root",
                                                 "scope",
                                                 "seeking",
                                                 "stalled",
                                                 "target",
                                                 "target-within",
                                                 "user-invalid",
                                                 "user-valid",
                                                 "valid",
                                                 "visited",
                                                 "volume-locked",
                                                 "active-view-transition",
                                                 "-webkit-any-link",
                                                 "-webkit-autofill"};
    return ascii_iequals_any(name, names);
}

[[nodiscard]] bool known_functional_pseudo_class(std::string_view name) {
    static constexpr std::string_view names[] = {"not",         "is",
                                                 "where",       "has",
                                                 "nth-child",   "nth-last-child",
                                                 "nth-of-type", "nth-last-of-type",
                                                 "nth-col",     "nth-last-col",
                                                 "lang",        "dir",
                                                 "host",        "host-context",
                                                 "state",       "active-view-transition-type",
                                                 "current",     "heading",
                                                 "-webkit-any"};
    return ascii_iequals_any(name, names);
}

// The four CSS 2 pseudo-elements may be written with one colon, and serialise
// with two either way.
[[nodiscard]] bool legacy_pseudo_element(std::string_view name) {
    static constexpr std::string_view names[] = {"before", "after", "first-line", "first-letter"};
    return ascii_iequals_any(name, names);
}

} // namespace

bool known_pseudo_element(std::string_view name) {
    static constexpr std::string_view names[] = {"before",
                                                 "after",
                                                 "first-line",
                                                 "first-letter",
                                                 "marker",
                                                 "placeholder",
                                                 "selection",
                                                 "backdrop",
                                                 "file-selector-button",
                                                 "spelling-error",
                                                 "grammar-error",
                                                 "target-text",
                                                 "cue",
                                                 "details-content",
                                                 "view-transition",
                                                 "scroll-marker",
                                                 "scroll-marker-group",
                                                 "checkmark",
                                                 "picker-icon",
                                                 "column"};
    // A vendor-prefixed pseudo-element - `::-webkit-scrollbar`, `::-moz-selection` -
    // is whatever that vendor says it is, and every browser parses the others'.
    return ascii_iequals_any(name, names) || ascii_istarts_with(name, "-webkit-") ||
           ascii_istarts_with(name, "-moz-");
}

namespace {

// The functional pseudo-elements whose one <ident> argument names a distinct
// pseudo-element with a cascade of its own (CSS Pseudo 4 §3.3, View
// Transitions 1 §4.6). The rest take a selector or a keyword.
constexpr std::string_view functional_pseudo_elements_named[] = {
    "highlight", "view-transition-group", "view-transition-image-pair", "view-transition-old",
    "view-transition-new"};

[[nodiscard]] bool known_functional_pseudo_element(std::string_view name) {
    static constexpr std::string_view names[] = {"part",
                                                 "slotted",
                                                 "highlight",
                                                 "cue",
                                                 "cue-region",
                                                 "view-transition-group",
                                                 "view-transition-image-pair",
                                                 "view-transition-old",
                                                 "view-transition-new",
                                                 "picker",
                                                 "scroll-button"};
    return ascii_iequals_any(name, names);
}

// `An+B`, from the component values inside an `:nth-child()` - Syntax 3 §6.
//
// The tokenizer has already decided where the numbers are, and not helpfully:
// `4n-1` is ONE dimension token whose unit is `n-1`, `-n+3` an ident and a signed
// number, `2n + 1` a dimension, two runs of whitespace, a delim and a number. So
// the token texts are joined back into the author's spelling - a single space
// standing for each run of whitespace - and that string is read against the
// grammar directly. The grammar allows whitespace in exactly one place, either
// side of the sign that separates `An` from `B`, so a space anywhere else is the
// syntax error it should be.
//
// Returns false for anything it cannot read, which the caller reports as a
// syntax error: `:nth-child(fred)` is not a selector in any browser.
[[nodiscard]] bool parse_nth(const stylesheet & sheet, std::span<const component_value> inner,
                             std::int32_t & a, std::int32_t & b) {
    std::string spelled;
    for (const component_value & v : inner) {
        if (v.kind != cv_kind::token) { return false; } // a nested block or function
        const css_token & t = sheet.tokens[v.token];
        if (t.type == token_type::whitespace) {
            if (!spelled.empty() && spelled.back() != ' ') { spelled += ' '; }
            continue;
        }
        if (t.type == token_type::comma || t.type == token_type::string) { return false; }
        spelled += ascii_lower_copy(sheet.text_of(t));
    }
    while (!spelled.empty() && spelled.back() == ' ') { spelled.pop_back(); }
    if (spelled.empty()) { return false; }
    if (spelled == "odd") {
        a = 2;
        b = 1;
        return true;
    }
    if (spelled == "even") {
        a = 2;
        b = 0;
        return true;
    }

    std::string_view rest = spelled;
    // An optional sign, then optional digits, from the front of `rest`. The two
    // are reported apart because `n` and `-n` have a coefficient with no digits.
    const auto read_sign = [&] {
        if (rest.empty() || (rest.front() != '+' && rest.front() != '-')) { return 0; }
        const int sign = rest.front() == '-' ? -1 : 1;
        rest.remove_prefix(1);
        return sign;
    };
    const auto read_digits = [&](std::int32_t & out) {
        std::size_t digits = 0;
        std::int64_t value = 0;
        while (digits < rest.size() && rest[digits] >= '0' && rest[digits] <= '9') {
            value = value * 10 + (rest[digits] - '0');
            if (value > INT32_MAX) { return false; }
            ++digits;
        }
        rest.remove_prefix(digits);
        if (digits != 0) { out = static_cast<std::int32_t>(value); }
        return digits != 0;
    };
    const int sign = read_sign();
    std::int32_t magnitude = 1;
    const bool digits = read_digits(magnitude);
    if (rest.empty()) {
        // Just `B`: `:nth-child(3)`.
        if (!digits) { return false; }
        a = 0;
        b = sign < 0 ? -magnitude : magnitude;
        return true;
    }
    if (rest.front() != 'n') { return false; }
    rest.remove_prefix(1);
    a = sign < 0 ? -magnitude : magnitude;
    if (rest.empty()) {
        b = 0;
        return true;
    }
    // `+B` or `-B`, the sign mandatory and whitespace allowed either side of it.
    if (rest.front() == ' ') { rest.remove_prefix(1); }
    const int b_sign = read_sign();
    if (b_sign == 0) { return false; }
    if (!rest.empty() && rest.front() == ' ') { rest.remove_prefix(1); }
    if (!read_digits(magnitude) || !rest.empty()) { return false; }
    b = b_sign < 0 ? -magnitude : magnitude;
    return true;
}

// `[name op "value" i]`, from the component values INSIDE the square block. The
// block itself was already delimited by the tokenizer, so there is no scanning for
// a `]` here and a `]` inside a quoted value cannot end it early.
[[nodiscard]] bool parse_attribute(const stylesheet & sheet, std::span<const component_value> inner,
                                   atom_table & atoms, attribute_match & out) {
    const auto tok = [&](const component_value & v) -> const css_token & {
        return sheet.tokens[v.token];
    };
    // Trim, then read: an optional namespace prefix, the name, then optionally an
    // operator and a value, then optionally a flag.
    inner = sheet.trimmed(inner);
    const auto is_delim = [&](std::size_t at, std::string_view what) {
        return at < inner.size() && inner[at].kind == cv_kind::token &&
               tok(inner[at]).type == token_type::delim && sheet.text_of(tok(inner[at])) == what;
    };
    const auto is_ident = [&](std::size_t at) {
        return at < inner.size() && inner[at].kind == cv_kind::token &&
               tok(inner[at]).type == token_type::ident;
    };
    // `*|name`, `|name` and `prefix|name`. The last is told from `[lang|=en]` by
    // what FOLLOWS the bar: a local name, not an `=`. Selectors 4 §3.2: a prefix
    // no `@namespace` declared is a syntax error - and `querySelector` declares
    // none, so `[ns|a]` throws there exactly as `ns|div` does.
    if (is_delim(0, "*") && is_delim(1, "|")) {
        out.ns = ns_prefix::any;
        inner = inner.subspan(2);
    } else if (is_delim(0, "|")) {
        out.ns = ns_prefix::none;
        inner = inner.subspan(1);
    } else if (is_ident(0) && is_delim(1, "|") && is_ident(2)) {
        const std::string_view prefix = sheet.text_of(tok(inner[0]));
        const namespace_declaration * bound = nullptr;
        for (const namespace_declaration & each : sheet.namespaces) {
            if (!each.prefix.empty() && each.prefix == prefix) { bound = &each; }
        }
        if (bound == nullptr) { return false; }
        out.ns = ns_prefix::named;
        out.ns_uri = atoms.intern(bound->uri);
        inner = inner.subspan(2);
    }
    if (!is_ident(0)) { return false; }
    // Attribute names are ASCII case-insensitive in HTML, and the DOM interns them
    // lowercased - so folding here is what makes `[HREF]` match `href`.
    out.name = atoms.intern_lower(sheet.text_of(tok(inner.front())));
    out.name_exact = atoms.intern(sheet.text_of(tok(inner.front())));
    inner = inner.subspan(1);
    while (!inner.empty() && sheet.is_space(inner.front())) { inner = inner.subspan(1); }
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
    while (!inner.empty() && sheet.is_space(inner.front())) { inner = inner.subspan(1); }
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
    while (!inner.empty() && sheet.is_space(inner.front())) { inner = inner.subspan(1); }
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

    // Whether anything in the list was not a selector at all, as opposed to one
    // this engine cannot match. See parse_selector_list's declaration.
    [[nodiscard]] bool invalid() const noexcept { return invalid_; }
    void set_nesting(const nesting_context * nesting) noexcept { nesting_ = nesting; }

    [[nodiscard]] std::uint32_t run(std::span<const component_value> prelude) {
        std::uint32_t written = 0;
        std::size_t at = 0;
        while (at <= prelude.size()) {
            // Split on TOP-LEVEL commas only. A comma inside `:not(a, b)` is a
            // child of the function's component value and is never seen here,
            // which is the whole reason the prelude is parsed as component values
            // rather than scanned as text.
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
    // The argument list of `:lang()` - `[ <ident> | <string> ]#` - and of `:dir()`,
    // which is one bare ident. Written out rather than reusing the value parser
    // because neither argument is a CSS VALUE: they are compared as text, and
    // `:lang("*-Latn")` has a wildcard in it that no ident may hold.
    //
    // AN UNQUOTED WILDCARD IS SEVERAL TOKENS. `*` is a delim and no ident may
    // contain one, so `:lang(*-CH)` arrives as delim `*` then ident `-CH` and
    // `:lang(*)` as a delim alone. Selectors 4 §7.2 takes both spellings, so
    // adjacent tokens - no whitespace between them - are one range.
    //
    // A `:dir()` keyword this engine does not know is accepted and simply never
    // matches, which is what Selectors 4 §7.2 asks for; a malformed argument makes
    // the compound unmatchable rather than reporting a syntax error, on the same
    // reading `:nth-child(of S)` is refused under.
    [[nodiscard]] bool parse_text_arguments(std::span<const component_value> inner, bool single,
                                            std::vector<std::string> & out) const {
        bool want_argument = true;
        bool spaced = false;
        for (const component_value & v : inner) {
            if (v.kind != cv_kind::token) { return false; }
            const css_token & t = token(v);
            if (t.type == token_type::whitespace) {
                spaced = true;
                continue;
            }
            if (t.type == token_type::comma) {
                if (single || want_argument) { return false; } // `:lang(,)`, `:lang(a,,b)`
                want_argument = true;
                spaced = false;
                continue;
            }
            std::string_view body = text(v);
            const bool wildcard = !single && t.type == token_type::delim && body == "*";
            if (t.type == token_type::string) {
                if (!want_argument) { return false; } // two arguments with no comma
                if (body.size() < 2) { return false; }
                body = body.substr(1, body.size() - 2);
            } else if (t.type != token_type::ident && !wildcard) {
                return false;
            }
            if (body.empty()) { return false; }
            // A LANGUAGE RANGE IS KEPT AS WRITTEN: it is matched ASCII
            // case-insensitively (Selectors 4 §7.2) but `selectorText` answers
            // `:lang(zh-CN)`. A direction keyword is compared as-is and folds.
            const std::string piece = single ? ascii_lower_copy(body) : std::string{body};
            if (want_argument) {
                out.push_back(piece);
                want_argument = false;
            } else if (spaced) {
                return false; // two arguments with no comma
            } else {
                out.back() += piece;
            }
            spaced = false;
        }
        return !want_argument && !(single && out.size() != 1);
    }

    // Exactly one identifier between the parentheses, whitespace aside; its
    // text case-folded, as the pseudo-element's name is interned.
    [[nodiscard]] bool single_ident_argument(const component_value & fn, std::string & out) const {
        for (const component_value & v : sheet_->children_of(fn)) {
            if (v.kind != cv_kind::token) { return false; }
            const css_token & t = token(v);
            if (t.type == token_type::whitespace) { continue; }
            if (t.type != token_type::ident || !out.empty()) { return false; }
            out = ascii_lower_copy(text(v));
        }
        return !out.empty();
    }

    [[nodiscard]] bool parse_functional(std::string_view name, const component_value & fn,
                                        building & into, bool & invalid) {
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
            // `of S` is refused above; anything else that will not read as An+B is a
            // SYNTAX error rather than an unsupported feature - `:nth-child(fred)` is
            // not a selector in any browser.
            if (!parse_nth(*sheet_, inner, ref.a, ref.b)) {
                invalid = true;
                return false;
            }
            ++into.classes; // a pseudo-class is class-level
            into.part.pseudos.push_back(std::move(ref));
            return true;
        }
        // `:lang()` and `:dir()` carry TEXT rather than a selector or an An+B: a
        // language range is not an identifier (`*-Latn` is a legal one) and a
        // direction keyword is answered from an ancestor's attribute, not from
        // anything the compound already knows about this element.
        if (ascii_iequals(name, "lang") || ascii_iequals(name, "dir")) {
            const bool want_dir = ascii_iequals(name, "dir");
            if (!parse_text_arguments(inner, want_dir, ref.ranges)) { return false; }
            ref.kind = want_dir ? pseudo_kind::dir : pseudo_kind::lang;
            ++into.classes; // a pseudo-class is class-level
            into.part.pseudos.push_back(std::move(ref));
            return true;
        }
        const bool is_not = ascii_iequals(name, "not");
        const bool is_is = ascii_iequals(name, "is");
        const bool is_where = ascii_iequals(name, "where");
        // `:has()` takes RELATIVE selectors, and may not nest - Selectors 4 §4.5.
        const bool is_has = ascii_iequals(name, "has");
        if (is_has && relative_) {
            invalid = true;
            return false;
        }
        // `:host()`, `:state()` are real CSS this engine cannot answer, and come
        // back unmatchable; a name CSS has never defined is a syntax error, and the
        // colon branch of `emit` refused it before the function was reached.
        if (!is_not && !is_is && !is_where && !is_has) { return false; }
        ref.kind = is_not ? pseudo_kind::not_ : is_where ? pseudo_kind::where_ : pseudo_kind::is_;
        ref.relative = is_has;

        // Parse the argument into a SCRATCH sheet's selector list, then move the
        // results onto the pseudo. A scratch sheet rather than the real one because
        // the real one's `selectors` vector is the rule's own list and a nested
        // selector is not an alternative of it.
        const std::size_t before = sheet_->selectors.size();
        // The nested list's own syntax errors are this selector's: `:not(>)` is not a
        // selector, and the flag has to come back out of the recursion to say so.
        selector_parser nested{*sheet_, *atoms_};
        nested.relative_ = is_has;
        nested.nesting_ = nesting_;
        const std::uint32_t count = nested.run(inner);
        invalid = invalid || nested.invalid();
        // `:is(&.a)` contains the nesting selector, and `:not(:scope)` names
        // the scoping root, as surely as the outer compound would.
        saw_nesting_ = saw_nesting_ || nested.saw_nesting_;
        saw_scope_ = saw_scope_ || nested.saw_scope_;
        for (std::size_t i = 0; i < count; ++i) {
            ref.args.push_back(std::move(sheet_->selectors[before + i]));
        }
        sheet_->selectors.resize(before);
        if (ref.args.empty()) { return false; }
        // The CSSOM has no field for `:has()`, so `selectorText` gives back the
        // author's bytes for a compound holding one - see pseudo_ref::relative.
        if (is_has) { into.part.dropped = true; }
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

    // THE NESTING SELECTOR, compiled into `b` as what it stands for - see
    // nesting_context. A parent alternative that can never match is left out
    // of the `:is()`, since `:is()` refuses a dead argument; with none left the
    // compound is dead too.
    void make_nesting(building & b, bool & dead) {
        b.part.nesting = true;
        saw_nesting_ = true;
        if (nesting_ == nullptr || nesting_->parent.empty()) {
            // `:scope` at the top level, with a pseudo-class's specificity;
            // `:where(:scope)` inside `@scope`, with none.
            b.part.structural |= structural_scope;
            if (nesting_ == nullptr || !nesting_->in_scope) { ++b.classes; }
            saw_scope_ = true;
            return;
        }
        pseudo_ref ref;
        ref.kind = pseudo_kind::is_;
        ref.nesting = true;
        specificity most;
        for (const compiled_selector & parent : nesting_->parent) {
            if (parent.parts.empty() || parent.parts.front().never_matches) { continue; }
            if (most < parent.spec) { most = parent.spec; }
            if (parent.explicit_scope) { saw_scope_ = true; }
            ref.args.push_back(parent);
        }
        if (ref.args.empty()) {
            dead = true;
            return;
        }
        b.ids += static_cast<int>(most.ids());
        b.classes += static_cast<int>(most.classes());
        b.tags += static_cast<int>(most.types());
        b.part.pseudos.push_back(std::move(ref));
    }

    // One comma-separated alternative.
    void emit(std::span<const component_value> run) {
        boost::container::small_vector<building, 2> compounds;
        boost::container::small_vector<combinator, 2> links; // left-to-right, size = n-1
        bool dead = false;
        saw_nesting_ = false;
        saw_scope_ = false;
        // ...AND WHETHER SOMETHING WAS LOST ON THE WAY. A dead alternative keeps
        // its compounds - `ns|e` and `::before` are unmatchable and still the
        // author's selector, which is what `selectorText` has to give back - and
        // `lossy` records that one of them holds a construct the compiled form
        // has no field for, so the serialiser knows not to try.
        bool lossy = false;
        // A COMBINATOR STILL WAITING FOR ITS RIGHT-HAND SIDE. `div >` and `div ~ ` are
        // syntax errors, and without this they parsed as plain `div`: `pending` is
        // simply overwritten by the next compound, so a combinator with nothing after
        // it left no trace at all.
        bool dangling_combinator = false;
        bool want_new_compound = true;
        bool pending_pseudo = false; // a `:` was seen and a function follows it
        combinator pending = combinator::none;

        const auto start_compound = [&] {
            if (!compounds.empty()) { links.push_back(pending); }
            compounds.push_back(building{});
            pending = combinator::descendant;
            want_new_compound = false;
            dangling_combinator = false;
        };
        // A RELATIVE selector - the argument of `:has()` - is anchored on `:scope`:
        // `> .a` is `:scope > .a` and a bare `.a` is `:scope .a`. The anchor is
        // written in as a compound of its own, so the rest of the grammar and the
        // matcher need know nothing about relative selectors at all. It contributes
        // no specificity, which is what Selectors 4 §16 says of it.
        if (relative_) {
            start_compound();
            compounds.back().part.structural |= structural_scope;
            want_new_compound = true;
        } else if (nesting_ != nullptr) {
            // A NESTED RULE MAY BE RELATIVE TOO: `> .a` in a style rule is
            // `& > .a` and in `@scope` it is `:where(:scope) > .a` (CSS
            // Nesting 1 §2.1) - the anchor is `&`, written in the same way.
            std::size_t first = 0;
            while (first < run.size() && sheet_->is_space(run[first])) { ++first; }
            if (first < run.size() && run[first].kind == cv_kind::token &&
                token(run[first]).type == token_type::delim &&
                (text(run[first]) == ">" || text(run[first]) == "+" || text(run[first]) == "~")) {
                start_compound();
                make_nesting(compounds.back(), dead);
                want_new_compound = true;
            }
        }
        // `|` IMMEDIATELY after run[at], which makes run[at] a namespace prefix.
        // Whitespace is a token, so `a | b` does not qualify - and cannot, since
        // a namespace separator is written with nothing on either side of it.
        const auto bar_follows = [&](std::size_t at) {
            return at + 1 < run.size() && run[at + 1].kind == cv_kind::token &&
                   token(run[at + 1]).type == token_type::delim && text(run[at + 1]) == "|";
        };
        // The prefix, then the local name after the `|`. Selectors 4 §3.2: a
        // named prefix that no `@namespace` declared is a syntax error, and the
        // prefix must open its compound. `at` is left ON the local name.
        const auto read_namespaced = [&](building & b, ns_prefix kind, std::string_view prefix,
                                         std::size_t & at) {
            if (b.part.ns != ns_prefix::unset || b.part.tag || b.tags != 0 || b.ids != 0 ||
                b.classes != 0) {
                return false;
            }
            // ...EVERY time, whoever asked. `prefixes_checked` was meant to let a
            // caller with no `@namespace` rules to offer take a prefix on trust, but
            // the only such callers are `querySelector` and `matches`, and Selectors
            // API §2 gives them no namespace resolver at all: `ns|div` there is a
            // SyntaxError in every browser.
            const namespace_declaration * bound = nullptr;
            if (kind == ns_prefix::named) {
                for (const namespace_declaration & each : sheet_->namespaces) {
                    if (!each.prefix.empty() && each.prefix == prefix) { bound = &each; }
                }
                if (bound == nullptr) { return false; }
            }
            if (at + 1 >= run.size() || run[at + 1].kind != cv_kind::token) { return false; }
            const css_token & local = token(run[at + 1]);
            ++at;
            if (local.type == token_type::ident) {
                b.part.tag = atoms_->intern_lower(text(run[at]));
                b.part.tag_exact = atoms_->intern(text(run[at]));
                b.tags = 1;
            } else if (local.type != token_type::delim || text(run[at]) != "*") {
                return false;
            }
            b.part.ns = kind;
            if (kind == ns_prefix::named) {
                b.part.ns_name = atoms_->intern(prefix);
                b.part.ns_uri = atoms_->intern(bound->uri);
            }
            // The null namespace is answered by nothing in the matcher, which
            // knows an element's namespace only as html, svg or other; `*|`
            // constrains nothing and matches as the bare name does.
            if (kind == ns_prefix::none) { dead = true; }
            return true;
        };

        for (std::size_t i = 0; i < run.size(); ++i) {
            const component_value & v = run[i];
            if (v.kind == cv_kind::block) {
                // An attribute selector arrives as ONE block, because the prelude
                // was parsed as component values - so a `]` inside a quoted value
                // cannot end it early and there is nothing to scan for.
                if (v.open != '[') {
                    dead = invalid_ = true; // a stray `(` or `{` in a prelude
                    continue;
                }
                if (want_new_compound || compounds.empty()) { start_compound(); }
                attribute_match match;
                if (!parse_attribute(*sheet_, sheet_->children_of(v), *atoms_, match)) {
                    dead = invalid_ = true;
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
                    dead = invalid_ = true;
                    continue;
                }
                pending_pseudo = false;
                std::string_view name = text(v);
                if (!name.empty() && name.back() == '(') { name.remove_suffix(1); }
                if (!parse_functional(name, v, compounds.back(), invalid_)) { dead = lossy = true; }
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
                if (bar_follows(i)) {
                    const std::string_view prefix = text(v);
                    ++i; // the `|`
                    if (!read_namespaced(b, ns_prefix::named, prefix, i)) {
                        dead = invalid_ = true;
                    }
                    continue;
                }
                if (b.part.tag || b.tags != 0) {
                    // Two type selectors in one compound - `divp` cannot happen
                    // from the tokenizer, so this means something upstream is
                    // wrong rather than that the author wrote something odd.
                    dead = invalid_ = true;
                    continue;
                }
                // Tags fold to lowercase: HTML tag names are ASCII
                // case-insensitive and the DOM interns them lowercased. The
                // author's spelling is kept beside it because a FOREIGN element
                // does not fold - `linearGradient` is a different name from
                // `lineargradient` and only one of them exists in an SVG.
                b.part.tag = atoms_->intern_lower(text(v));
                b.part.tag_exact = atoms_->intern(text(v));
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
                    dead = invalid_ = true;
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
                        dead = invalid_ = true;
                        continue;
                    }
                    if (want_new_compound || compounds.empty()) { start_compound(); }
                    building & b = compounds.back();
                    b.part.classes.push_back(atoms_->intern(text(run[i + 1])));
                    ++b.classes;
                    ++i; // the ident
                    continue;
                }
                if (d == "&") {
                    // The nesting selector, CSS Nesting 1 §2: a compound of its
                    // own or part of one - `&.a` and `.a&` are both legal.
                    if (want_new_compound || compounds.empty()) { start_compound(); }
                    make_nesting(compounds.back(), dead);
                    continue;
                }
                if (d == "*") {
                    if (want_new_compound || compounds.empty()) { start_compound(); }
                    if (bar_follows(i)) {
                        ++i; // the `|`
                        if (!read_namespaced(compounds.back(), ns_prefix::any, {}, i)) {
                            dead = invalid_ = true;
                        }
                        continue;
                    }
                    // The universal selector constrains nothing and contributes no
                    // specificity - an empty tag atom IS universal here.
                    continue;
                }
                // The three explicit combinators. Whitespace either side is
                // irrelevant, so the pending relation is simply overwritten - which
                // is what makes `a > b`, `a>b` and `a >b` one selector.
                if (d == ">" || d == "+" || d == "~") {
                    // ...with nothing on its left, or `div ++ p`: two in a row.
                    if (compounds.empty() || dangling_combinator) {
                        dead = invalid_ = true;
                        continue;
                    }
                    pending = d == ">"   ? combinator::child
                              : d == "+" ? combinator::next_sibling
                                         : combinator::subsequent_sibling;
                    want_new_compound = true;
                    dangling_combinator = true;
                    continue;
                }
                // A `|` with nothing before it is the NULL namespace - `|e` is an
                // element in no namespace at all. The two prefixed forms were read
                // from their prefix and never reach here.
                if (d == "|") {
                    if (want_new_compound || compounds.empty()) { start_compound(); }
                    if (!read_namespaced(compounds.back(), ns_prefix::none, {}, i)) {
                        dead = invalid_ = true;
                    }
                    continue;
                }
                dead = invalid_ = true;
                continue;
            }
            case token_type::colon: {
                if (want_new_compound || compounds.empty()) { start_compound(); }
                building & b = compounds.back();
                // `::` is a pseudo-ELEMENT. The name after it is kept - the engine
                // generates no boxes for one, so the compound never matches, but
                // `selectorText` gives it back - and a name CSS has not defined,
                // or one with no argument that needs one, is a syntax error.
                const bool doubled = i + 1 < run.size() && run[i + 1].kind == cv_kind::token &&
                                     token(run[i + 1]).type == token_type::colon;
                const std::size_t name_at = i + (doubled ? 2 : 1);
                if (name_at >= run.size()) {
                    dead = invalid_ = true; // a bare `:` or `::`
                    continue;
                }
                const component_value & next = run[name_at];
                if (next.kind == cv_kind::function) {
                    std::string_view name = text(next);
                    if (!name.empty() && name.back() == '(') { name.remove_suffix(1); }
                    if (doubled) {
                        if (!known_functional_pseudo_element(name)) {
                            dead = invalid_ = true;
                            continue;
                        }
                        i = name_at;
                        // `::highlight(x)` and the four `::view-transition-*(x)`
                        // take one identifier and NAME a pseudo-element the
                        // cascade can run: the compound carries it as
                        // `name(argument)`, which is the spelling
                        // getComputedStyle(el, "::highlight(x)") asks for
                        // (bindings/computed_style, resolvable_pseudo), so
                        // `::highlight(name) { color: green }` is that read's
                        // answer. `::part(x)`, `::slotted(y)`, `::picker(select)`:
                        // real, and nothing here holds the argument, so the
                        // author's bytes are the only record.
                        std::string argument;
                        if (ascii_iequals_any(name, functional_pseudo_elements_named) &&
                            single_ident_argument(next, argument)) {
                            b.part.pseudo_element =
                                atoms_->intern_lower(ascii_lower_copy(name) + "(" + argument + ")");
                            ++b.tags;
                            continue;
                        }
                        dead = lossy = true;
                        continue;
                    }
                    if (!known_functional_pseudo_class(name)) {
                        dead = invalid_ = true;
                        continue;
                    }
                    // `:not(`, `:nth-child(` and friends: the tokenizer folded the
                    // name and the `(` into one token, so the colon and the function
                    // are two component values. Flag it and let the function branch
                    // handle it on the next iteration.
                    pending_pseudo = true;
                    continue;
                }
                if (next.kind != cv_kind::token || token(next).type != token_type::ident) {
                    dead = invalid_ = true; // `:::`, `:1`, `: hover`
                    continue;
                }
                const std::string_view name = text(next);
                i = name_at;
                if (doubled || legacy_pseudo_element(name)) {
                    if (!known_pseudo_element(name)) {
                        dead = invalid_ = true;
                        continue;
                    }
                    b.part.pseudo_element = atoms_->intern_lower(name);
                    ++b.tags; // a pseudo-element is type-level for specificity
                    // A PSEUDO-ELEMENT HAS A CASCADE - engine::resolve_pseudo runs
                    // it for getComputedStyle(el, "::before") - and the matcher
                    // keeps the compound from every element (`pseudo_wanted_`). A
                    // vendor's is whatever the vendor says it is, and matches nothing.
                    if (name.starts_with('-')) { dead = true; }
                    continue;
                }
                if (const std::uint32_t bit = state_bit_of(name); bit != 0) {
                    b.part.states |= bit;
                    ++b.classes; // a pseudo-class is class-level for specificity
                    continue;
                }
                if (const std::uint32_t bit = structural_bit_of(name); bit != 0) {
                    b.part.structural |= bit;
                    ++b.classes;
                    if (bit == structural_scope) { saw_scope_ = true; }
                    continue;
                }
                // `:focus-visible` and `:defined` are real and this engine cannot
                // observe either; `:gibberish` is not a selector.
                if (!known_pseudo_class(name)) {
                    dead = invalid_ = true;
                    continue;
                }
                dead = lossy = true;
                continue;
            }
            default:
                // A number, a string, a percentage - none of them can appear in a
                // selector at this level.
                dead = invalid_ = true;
                continue;
            }
        }

        // AN EMPTY ALTERNATIVE IS A SYNTAX ERROR, and it is how `querySelector("")`,
        // `a,,b` and a trailing comma all arrive here: `run` holds nothing but
        // whitespace, so no compound was ever started - or, for a relative one,
        // nothing but the synthetic `:scope` anchor.
        if (compounds.size() <= (relative_ ? 1u : 0u) || dangling_combinator) {
            invalid_ = true;
            push_dead();
            return;
        }
        // IMPLICIT NESTING, CSS Nesting 1 §2.1: a selector in a nested style
        // rule that names no `&` is `& <selector>`. Not inside `@scope`, whose
        // in-scope test already restricts the subject, and not for the
        // synthetic anchor of a `:has()` argument.
        if (nesting_ != nullptr && !nesting_->parent.empty() && !saw_nesting_ && !relative_) {
            building anchor;
            make_nesting(anchor, dead);
            compounds.insert(compounds.begin(), std::move(anchor));
            links.insert(links.begin(), combinator::descendant);
        }
        // A DEAD ALTERNATIVE KEEPS ITS COMPOUNDS. The rightmost is what the
        // matcher and the rule index consult first, so the flag goes there; the
        // CSSOM reads `dropped` to decide between the canonical serialisation
        // and the author's bytes.
        if (dead) {
            compounds.back().part.never_matches = true;
            compounds.back().part.dropped = lossy;
        }

        // A DEFAULT NAMESPACE PUTS EVERY UNPREFIXED COMPOUND IN IT, the implied
        // `*` of `.style1` included (Selectors 4 §6.1.1): with `@namespace
        // url(xhtml)` declared, `.style1` no longer names an <svg>.
        for (const namespace_declaration & each : sheet_->namespaces) {
            if (!each.prefix.empty()) { continue; }
            for (building & b : compounds) {
                if (b.part.ns == ns_prefix::unset) { b.part.ns_uri = atoms_->intern(each.uri); }
            }
        }

        compiled_selector out;
        specificity spec;
        for (const building & b : compounds) { spec = spec + specificity_of(b); }
        out.spec = spec;
        out.explicit_scope = saw_scope_;
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
    bool invalid_ = false;
    bool relative_ = false; // parsing the argument of `:has()`
    const nesting_context * nesting_ = nullptr;
    // Per alternative: whether it wrote `&`, and whether it names the scoping
    // root (`:scope`, or `&` where that is what `&` means).
    bool saw_nesting_ = false;
    bool saw_scope_ = false;
};

} // namespace

std::uint32_t parse_selector_list(stylesheet & sheet, std::span<const component_value> prelude,
                                  atom_table & atoms, bool * invalid,
                                  const nesting_context * nesting) {
    selector_parser parser{sheet, atoms};
    parser.set_nesting(nesting);
    const std::uint32_t written = parser.run(prelude);
    if (invalid && parser.invalid()) { *invalid = true; }
    return written;
}

} // namespace ctbrowser::style::css
