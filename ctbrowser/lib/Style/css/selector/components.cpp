#include "internal.hpp"

namespace ctbrowser::style::css::selector_detail {

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
    // `:defined` is answered from the element's name and namespace - a built-in is
    // always defined - so it belongs here rather than in the state bits nothing sets.
    if (ascii_iequals(name, "defined")) { return structural_defined; }
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
                                                 "has-slotted",
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
    static constexpr std::string_view names[] = {"not",
                                                 "is",
                                                 "where",
                                                 "has",
                                                 "nth-child",
                                                 "nth-last-child",
                                                 "nth-of-type",
                                                 "nth-last-of-type",
                                                 "nth-col",
                                                 "nth-last-col",
                                                 "lang",
                                                 "dir",
                                                 "has-slotted",
                                                 "host",
                                                 "host-context",
                                                 "state",
                                                 "active-view-transition-type",
                                                 "current",
                                                 "heading",
                                                 "-webkit-any"};
    return ascii_iequals_any(name, names);
}

// The four CSS 2 pseudo-elements may be written with one colon, and serialise
// with two either way.
[[nodiscard]] bool legacy_pseudo_element(std::string_view name) {
    static constexpr std::string_view names[] = {"before", "after", "first-line", "first-letter"};
    return ascii_iequals_any(name, names);
}

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
            // CLAMPED, not refused: a coefficient past the integer range is
            // INT_MAX (nth-child-large-anplusb-clamp), as the CSSOM reads it.
            if (value <= INT32_MAX) { value = value * 10 + (rest[digits] - '0'); }
            if (value > INT32_MAX) { value = INT32_MAX; }
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
        } else if (ascii_iequals(flag, "s")) {
            out.case_sensitive_flag = true;
        } else {
            return false;
        }
    }
    // An EMPTY value is not the same as no value: `[a=""]` matches an attribute
    // whose value is empty, which `[a]` also does - but `[a^=""]` matches nothing
    // at all, per the spec, and that is the matcher's business rather than ours.
    return true;
}

// The spec's (a, b, c): ids, then class-level conditions, then type-level ones.
// An ATTRIBUTE selector and a pseudo-class are both class-level, which is why they
// share the counter.
[[nodiscard]] specificity specificity_of(const building & b) {
    return specificity::of(static_cast<std::uint32_t>(b.ids), static_cast<std::uint32_t>(b.classes),
                           static_cast<std::uint32_t>(b.tags));
}

} // namespace ctbrowser::style::css::selector_detail
