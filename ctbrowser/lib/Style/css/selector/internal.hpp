//===- internal.hpp - Selector parser internals --------------- -*- C++ -*-===//
//
// selector_parser, which compiles a selector prelude into compiled_selector
// entries on the stylesheet, and the leaf readers it uses (components.cpp).
// Private to the selector sources; the public entry points are in selector.cpp.
// Test names cited without a path are WPT tests, mostly under css/selectors/.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <ctbrowser/style/css/selector.hpp>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/core/depth.hpp>

namespace ctbrowser::style::css::selector_detail {

/// See components.cpp for these readers' contracts.
[[nodiscard]] std::uint32_t state_bit_of(std::string_view name);

[[nodiscard]] std::uint32_t structural_bit_of(std::string_view name);

[[nodiscard]] bool known_pseudo_class(std::string_view name);

[[nodiscard]] bool known_functional_pseudo_class(std::string_view name);

/// True for the CSS 2 pseudo-elements that may still be written with a single
/// colon: `:before`, `:after`, `:first-line`, `:first-letter`.
[[nodiscard]] bool legacy_pseudo_element(std::string_view name);

/// The functional pseudo-elements whose one <ident> argument names a distinct
/// pseudo-element with a cascade of its own (CSS Pseudo 4 §3.3, View
/// Transitions 1 §4.6). The rest take a selector or a keyword.
inline constexpr std::string_view functional_pseudo_elements_named[] = {
    "highlight", "view-transition-group", "view-transition-image-pair", "view-transition-old",
    "view-transition-new"};

[[nodiscard]] bool known_functional_pseudo_element(std::string_view name);

[[nodiscard]] bool parse_nth(const stylesheet & sheet, std::span<const component_value> inner,
                             std::int32_t & a, std::int32_t & b);

[[nodiscard]] bool parse_attribute(const stylesheet & sheet, std::span<const component_value> inner,
                                   atom_table & atoms, attribute_match & out);

/// One compound selector under construction, plus how it contributes to
/// specificity.
struct building {
    compound part;
    int ids = 0;
    int classes = 0; // Classes, attributes and pseudo-classes all count here.
    int tags = 0;
};

[[nodiscard]] specificity specificity_of(const building & b);

/// Compiles a selector prelude (already split into component values) into
/// compiled_selector entries appended to the sheet's `selectors`, one per
/// top-level comma-separated alternative. An alternative that is valid but
/// unmatchable here is kept with `never_matches` set; one that is not a
/// selector at all also sets invalid(). Functional pseudo-classes recurse into
/// a nested selector_parser.
class selector_parser {
public:
    selector_parser(stylesheet & sheet, atom_table & atoms) : sheet_(&sheet), atoms_(&atoms) {}

    /// Whether anything in the list was not a selector at all, as opposed to one
    /// this engine cannot match. See parse_selector_list's declaration.
    [[nodiscard]] bool invalid() const noexcept { return invalid_; }
    void set_nesting(const nesting_context * nesting) noexcept { nesting_ = nesting; }

    /// Parses every alternative of `prelude`; returns how many were appended.
    [[nodiscard]] std::uint32_t run(std::span<const component_value> prelude) {
        std::uint32_t written = 0;
        std::size_t at = 0;
        while (at <= prelude.size()) {
            // Split on top-level commas only. A comma inside `:not(a, b)` is a
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

    /// Reads the argument list of `:lang()` (`[ <ident> | <string> ]#`) or, with
    /// `single`, of `:dir()` (one bare ident) into `out`. Written out rather than
    /// reusing the value parser because neither argument is a CSS value: they
    /// are compared as text, and `:lang("*-Latn")` has a wildcard in it that no
    /// ident may hold.
    ///
    /// An unquoted wildcard is several tokens. `*` is a delim and no ident may
    /// contain one, so `:lang(*-CH)` arrives as delim `*` then ident `-CH` and
    /// `:lang(*)` as a delim alone. Selectors 4 §7.2 takes both spellings, so
    /// adjacent tokens (no whitespace between them) are one range.
    ///
    /// A `:dir()` keyword this engine does not know is accepted and never
    /// matches, as Selectors 4 §7.2 asks; a malformed `:lang()` argument makes
    /// the compound unmatchable rather than a syntax error. (A malformed
    /// `:dir()` is a syntax error; see parse_functional.)
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
                // `:lang()` takes a string; `:dir()` an <ident> and nothing else.
                if (single || !want_argument) { return false; }
                if (body.size() < 2) { return false; }
                body = body.substr(1, body.size() - 2);
            } else if (t.type != token_type::ident && !wildcard) {
                return false;
            }
            if (body.empty()) { return false; }
            // A language range is kept as written: it is matched ASCII
            // case-insensitively (Selectors 4 §7.2) but `selectorText` answers
            // `:lang(zh-CN)`. A direction keyword is compared as-is and folds.
            const std::string piece = single ? ascii_lower_copy(body) : std::string{body};
            if (want_argument) {
                out.push_back(piece);
                want_argument = false;
            } else if (spaced) {
                return false; // Two arguments with no comma.
            } else {
                out.back() += piece;
            }
            spaced = false;
        }
        return !want_argument && !(single && out.size() != 1);
    }

    /// Reads exactly one identifier between `fn`'s parentheses, whitespace
    /// aside, into `out`, case-folded as the pseudo-element's name is interned.
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

    /// Checks a view transition pseudo-element's argument (CSS View Transitions
    /// 2 §3.2): `*` or a <custom-ident> (not a CSS-wide keyword, not `default`)
    /// then `.class`es, or the classes alone, with nothing between the parts.
    /// `(*.*)`, `(*. cls)`, `(foo bar)` and `(default)` are invalid.
    [[nodiscard]] bool view_transition_argument(const component_value & fn) const {
        std::vector<const component_value *> parts;
        for (const component_value & v : sheet_->children_of(fn)) { parts.push_back(&v); }
        const auto space = [&](const component_value * v) {
            return v->kind == cv_kind::token && token(*v).type == token_type::whitespace;
        };
        while (!parts.empty() && space(parts.front())) { parts.erase(parts.begin()); }
        while (!parts.empty() && space(parts.back())) { parts.pop_back(); }
        const auto ident = [&](std::size_t k) {
            return k < parts.size() && parts[k]->kind == cv_kind::token &&
                   token(*parts[k]).type == token_type::ident &&
                   !ascii_iequals_any(text(*parts[k]), {"default", "initial", "inherit", "unset",
                                                        "revert", "revert-layer"});
        };
        const auto delim = [&](std::size_t k, std::string_view d) {
            return k < parts.size() && parts[k]->kind == cv_kind::token &&
                   token(*parts[k]).type == token_type::delim && text(*parts[k]) == d;
        };
        std::size_t k = 0;
        if (delim(0, "*") || ident(0)) { k = 1; }
        while (k < parts.size()) {
            if (!delim(k, ".") || !ident(k + 1)) { return false; }
            k += 2;
        }
        return k > 0;
    }

    /// Checks that `inner` is one <compound-selector>, the argument of `:host()`
    /// and `::slotted()`. Parsed for validity only (neither is matched here); a
    /// combinator is a syntax error at any depth (compound_only_).
    [[nodiscard]] bool compound_argument(std::span<const component_value> inner) {
        const std::size_t before = sheet_->selectors.size();
        selector_parser nested{*sheet_, *atoms_};
        nested.depth_ = depth_;
        nested.nesting_ = nesting_;
        nested.in_argument_ = true;
        nested.compound_only_ = true;
        const std::uint32_t count = nested.run(inner);
        sheet_->selectors.resize(before);
        return !nested.invalid() && count == 1;
    }

    /// Compiles functional pseudo-class `name` (the `fn` component value) into
    /// `into`. Returns false if the compound cannot match here, and sets
    /// `invalid` if it is not a selector at all.
    ///
    /// `:not()`, `:is()`, `:where()` and `:has()` recurse into a nested
    /// selector_parser over the argument, so a nested selector gets the whole
    /// grammar: `:not(.a .b)` and `:is(div > p)` work, because a nested
    /// selector's subject is the same element and its combinators walk from the
    /// same cursor.
    [[nodiscard]] bool parse_functional(std::string_view name, const component_value & fn,
                                        building & into, bool & invalid) {
        // Every functional pseudo nests through one count, carried into each
        // nested parser: `:not(:is(:where(:has(...))))` recurses here, and the
        // matcher recurses over what it builds. Past the cap the selector is
        // a syntax error, not a partial parse.
        if (depth_ >= max_depth) {
            invalid = true;
            return false;
        }
        const depth_guard level{depth_};
        pseudo_ref ref;
        const auto inner = sheet_->children_of(fn);
        if (ascii_iequals(name, "nth-child") || ascii_iequals(name, "nth-last-child") ||
            ascii_iequals(name, "nth-of-type") || ascii_iequals(name, "nth-last-of-type")) {
            ref.kind = ascii_iequals(name, "nth-child")        ? pseudo_kind::nth_child
                       : ascii_iequals(name, "nth-last-child") ? pseudo_kind::nth_last_child
                       : ascii_iequals(name, "nth-of-type")    ? pseudo_kind::nth_of_type
                                                               : pseudo_kind::nth_last_of_type;
            // `of S` (Selectors 4) is not supported. parse_nth rejects it along
            // with any argument that is not An+B (`:nth-child(fred)`), and both
            // are treated as a syntax error. Ignoring `of S` instead would count
            // the wrong siblings.
            if (!parse_nth(*sheet_, inner, ref.a, ref.b)) {
                invalid = true;
                return false;
            }
            ++into.classes; // A pseudo-class is class-level.
            into.part.pseudos.push_back(std::move(ref));
            return true;
        }
        // `:heading(<integer>#)`, Selectors 5: a comma-separated list of
        // integers and nothing else; `:heading(2n)`, `:heading(1.0)` and an
        // empty list are syntax errors (parse-heading.html).
        if (ascii_iequals(name, "heading")) {
            bool want_level = true;
            for (const component_value & v : inner) {
                if (v.kind != cv_kind::token) {
                    invalid = true;
                    return false;
                }
                const css_token & t = token(v);
                if (t.type == token_type::whitespace) { continue; }
                if (want_level) {
                    if (t.type != token_type::number || (t.flags & flag_integer) == 0) {
                        invalid = true;
                        return false;
                    }
                    const std::string_view digits = text(v);
                    std::int32_t level = 0;
                    const auto [end, ec] =
                        std::from_chars(digits.data() + (digits.front() == '+' ? 1 : 0),
                                        digits.data() + digits.size(), level);
                    if (ec != std::errc{} || end != digits.data() + digits.size()) {
                        invalid = true;
                        return false;
                    }
                    ref.levels.push_back(level);
                } else if (t.type != token_type::comma) {
                    invalid = true;
                    return false;
                }
                want_level = !want_level;
            }
            if (ref.levels.empty() || want_level) {
                invalid = true;
                return false;
            }
            ref.kind = pseudo_kind::heading;
            ++into.classes;
            into.part.pseudos.push_back(std::move(ref));
            return true;
        }
        // `:lang()` and `:dir()` carry text rather than a selector or an An+B: a
        // language range is not an identifier (`*-Latn` is a legal one) and a
        // direction keyword is answered from an ancestor's attribute, not from
        // anything the compound already knows about this element.
        if (ascii_iequals(name, "lang") || ascii_iequals(name, "dir")) {
            const bool want_dir = ascii_iequals(name, "dir");
            if (!parse_text_arguments(inner, want_dir, ref.ranges)) {
                // `:dir(<ident>)` has exactly one grammar, and `:dir()`,
                // `:dir(ltr, rtl)` and `:dir('ltr')` are outside it: a syntax
                // error (Selectors 4 §7.1), not an unmatchable compound.
                if (want_dir) { invalid = true; }
                return false;
            }
            ref.kind = want_dir ? pseudo_kind::dir : pseudo_kind::lang;
            ++into.classes; // A pseudo-class is class-level.
            into.part.pseudos.push_back(std::move(ref));
            return true;
        }
        // `:has-slotted(<compound>)`, CSS Scoping (csswg-drafts#10586), is a
        // question about a <slot>'s assigned nodes. The assignment lives in the
        // shell and the style matcher is not told it, so this is parsed for
        // validity and left unmatchable. The argument is a selector over slotted
        // nodes, which are a flat sibling list: sibling combinators are meaningful
        // and the child/descendant combinators are not, so `:has-slotted(div +
        // div)` is valid and `:has-slotted(div > span)` is not.
        if (ascii_iequals(name, "has-slotted")) {
            const std::size_t before = sheet_->selectors.size();
            selector_parser nested{*sheet_, *atoms_};
            nested.depth_ = depth_;
            nested.nesting_ = nesting_;
            const std::uint32_t count = nested.run(inner);
            const bool nested_bad = nested.invalid();
            std::vector<compiled_selector> args;
            for (std::size_t i = 0; i < count; ++i) {
                args.push_back(std::move(sheet_->selectors[before + i]));
            }
            sheet_->selectors.resize(before);
            // `:has-slotted()` is a syntax error, as is a malformed argument or one
            // joined by a descendant or child combinator.
            bool ok = !nested_bad && count != 0;
            for (const compiled_selector & arg : args) {
                for (const combinator link : arg.links) {
                    if (link == combinator::descendant || link == combinator::child) { ok = false; }
                }
            }
            if (!ok) {
                invalid = true;
                return false;
            }
            // Valid, and never matches: the caller marks the compound dead and lossy,
            // which keeps it out of the rule index and sends selectorText to the
            // author's bytes.
            return false;
        }
        const bool is_not = ascii_iequals(name, "not");
        const bool is_is = ascii_iequals(name, "is");
        const bool is_where = ascii_iequals(name, "where");
        // `:has()` takes relative selectors, and may not nest (Selectors 4 §4.5).
        const bool is_has = ascii_iequals(name, "has");
        if (is_has && (relative_ || in_has_)) {
            invalid = true;
            return false;
        }
        // `:host()` and `:state()` are real CSS this engine cannot answer, and
        // come back unmatchable; a name CSS has never defined is a syntax error,
        // and the colon branch of `emit` refused it before this was reached.
        if (ascii_iequals(name, "state")) {
            // `:state(<ident>)`, CSS Custom State: one identifier, or a syntax
            // error (`:state()`, `:state(0)`, `:state(foo(1))`).
            std::string argument;
            if (!single_ident_argument(fn, argument)) { invalid = true; }
            return false;
        }
        // `:host(<compound-selector>)` and `:host-context()` (CSS Scoping 1
        // §3.2.1): real and unmatchable here, like `:state()`, and a syntax
        // error unless the argument is one compound, as in `:host(:not(.a .b))`
        // (parse-not). See compound_argument.
        if (ascii_iequals(name, "host") || ascii_iequals(name, "host-context")) {
            if (!compound_argument(inner)) { invalid = true; }
            return false;
        }
        if (!is_not && !is_is && !is_where && !is_has) { return false; }
        ref.kind = is_not ? pseudo_kind::not_ : is_where ? pseudo_kind::where_ : pseudo_kind::is_;
        ref.relative = is_has;

        // Nested parsers append their results to sheet_->selectors. They are
        // moved onto the pseudo and the vector is truncated back to `before`,
        // because a nested selector is not one of the rule's own alternatives.
        const std::size_t before = sheet_->selectors.size();
        if (is_is || is_where) {
            // A forgiving selector list (Selectors 4 §4.2): each alternative is
            // parsed on its own, and one that does not parse is dropped rather
            // than sinking the rule. `:is(:total-nonsense, #a)` is `:is(#a)`, and
            // `:is()` is valid and matches nothing. selectorText keeps the
            // author's bytes for a list that lost something. Where the sheet is
            // not forgiving (`sheet_->forgiving` false), a dropped alternative
            // is still a syntax error.
            std::size_t at = 0;
            bool dropped = false;
            for (;;) {
                std::size_t end = at;
                while (end < inner.size() && !is_comma(inner[end])) { ++end; }
                selector_parser one{*sheet_, *atoms_};
                one.depth_ = depth_;
                one.nesting_ = nesting_;
                one.in_argument_ = true;
                one.in_has_ = in_has_ || relative_;
                one.compound_only_ = compound_only_;
                const std::uint32_t n = one.run(inner.subspan(at, end - at));
                if (!one.invalid() && n == 1) {
                    ref.args.push_back(std::move(sheet_->selectors[before]));
                    saw_nesting_ = saw_nesting_ || one.saw_nesting_;
                    saw_scope_ = saw_scope_ || one.saw_scope_;
                } else {
                    dropped = true;
                    invalid = invalid || !sheet_->forgiving;
                }
                sheet_->selectors.resize(before);
                if (end >= inner.size()) { break; }
                at = end + 1;
            }
            if (dropped) { into.part.dropped = true; }
            if (ref.args.empty()) {
                if (!sheet_->forgiving) {
                    invalid = true;
                    return false;
                }
                // Nothing left is still a selector, one known to match nothing,
                // unlike an argument this engine cannot represent:
                // `:not(:is(svg|div))` matches every element (is-where-not).
                // The empty argument list says so to the matcher.
                into.part.pseudos.push_back(std::move(ref));
                return true;
            }
        } else {
            // The nested list's own syntax errors are this selector's: `:not(>)` is
            // not a selector, and the flag has to come back out of the recursion.
            selector_parser nested{*sheet_, *atoms_};
            nested.depth_ = depth_;
            nested.relative_ = is_has;
            nested.in_has_ = in_has_ || relative_;
            nested.nesting_ = nesting_;
            nested.in_argument_ = true;
            // A `:has()`'s argument is relative, anchored by a combinator of
            // its own, so it is not held to a compound.
            nested.compound_only_ = compound_only_ && !is_has;
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
        }
        // The CSSOM has no field for `:has()`, so `selectorText` gives back the
        // author's bytes for a compound holding one - see pseudo_ref::relative.
        if (is_has) { into.part.dropped = true; }
        // An argument this engine cannot represent makes the whole compound
        // unmatchable. The direction matters: for `:is()` a dead branch could be
        // dropped, but for `:not()` a dead branch would wrongly become "matches
        // nothing, therefore :not passes". Refusing both is the safe reading.
        for (const compiled_selector & arg : ref.args) {
            if (arg.parts.empty() || arg.parts.front().never_matches) { return false; }
        }
        // Specificity: `:is()`, `:not()` and `:has()` take their most specific
        // argument; `:where()` contributes nothing, which is why it exists.
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

    /// Appends an alternative that never matches, standing for one that was
    /// not a selector at all.
    void push_dead() {
        compiled_selector dead;
        compound c;
        c.never_matches = true;
        dead.parts.push_back(std::move(c));
        sheet_->selectors.push_back(std::move(dead));
    }

    /// Compiles the nesting selector `&` into `b` as what it stands for (see
    /// nesting_context): `:is(<parent selectors>)`, or `:scope` with no parent.
    /// A parent alternative that can never match is left out of the `:is()`,
    /// since `:is()` refuses a dead argument; with none left, `dead` is set.
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

    /// Compiles one comma-separated alternative and appends it to the sheet.
    void emit(std::span<const component_value> run) {
        boost::container::small_vector<building, 2> compounds;
        boost::container::small_vector<combinator, 2> links; // Left to right; size n-1.
        bool dead = false;
        saw_nesting_ = false;
        saw_scope_ = false;
        // Whether something was lost on the way. A dead alternative keeps its
        // compounds (`ns|e` and `::before` are unmatchable and still the
        // author's selector, which `selectorText` has to give back), and
        // `lossy` records that one of them holds a construct the compiled form
        // has no field for, so the serialiser knows not to try.
        bool lossy = false;
        // A combinator still waiting for its right-hand side. `div >` and `div ~ `
        // are syntax errors, and `pending` alone cannot tell: it is overwritten
        // by the next compound, so a combinator with nothing after it would
        // leave no trace.
        bool dangling_combinator = false;
        bool want_new_compound = true;
        bool pending_pseudo = false; // A `:` was seen and a function follows it.
        combinator pending = combinator::none;
        // After a pseudo-element (Selectors 4 §3.6.3) nothing may follow in the
        // compound but a user-action pseudo-class (`::before:hover`) or, for
        // CSS View Transitions' pseudo-elements, `:only-child`; and no
        // combinator may follow it at all. `::before.a`, `::before div` and
        // `::view-transition:hover` are syntax errors.
        bool after_pe = false;
        // Which pseudo-element it was, lowercased: what may follow one depends
        // on it (CSS Pseudo 4 §4.1-§4.3).
        std::string last_pe;
        bool view_transition_pe = false;
        // A ::part(), however, takes any pseudo-class that is not about the tree
        // (CSS Shadow Parts 1 §4.2): `::part(x):state(y)`, `::part(x):disabled`.
        // So does the other element-backed pseudo-element, ::details-content.
        bool part_pe = false;
        // A ::scroll-button() is a button: `:enabled` and `:disabled` may follow
        // it as well as the user actions (CSS Overflow 5 §3.1).
        bool button_pe = false;

        const auto start_compound = [&] {
            if (!compounds.empty() && (after_pe || compound_only_)) { dead = invalid_ = true; }
            after_pe = false;
            last_pe.clear();
            view_transition_pe = false;
            part_pe = false;
            button_pe = false;
            if (!compounds.empty()) { links.push_back(pending); }
            compounds.push_back(building{});
            pending = combinator::descendant;
            want_new_compound = false;
            dangling_combinator = false;
        };
        // A relative selector (the argument of `:has()`) is anchored on `:scope`:
        // `> .a` is `:scope > .a` and a bare `.a` is `:scope .a`. The anchor is
        // written in as a compound of its own, so the rest of the grammar and the
        // matcher need know nothing about relative selectors at all. It contributes
        // no specificity, which is what Selectors 4 §16 says of it.
        if (relative_) {
            start_compound();
            compounds.back().part.structural |= structural_scope;
            want_new_compound = true;
        } else if (nesting_ != nullptr) {
            // A nested rule may be relative too: `> .a` in a style rule is
            // `& > .a` and in `@scope` it is `:where(:scope) > .a` (CSS
            // Nesting 1 §2.1). The anchor is `&`, written in the same way.
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
        // `|` immediately after run[at], which makes run[at] a namespace prefix.
        // Whitespace is a token, so `a | b` does not qualify, and cannot, since
        // a namespace separator is written with nothing on either side of it.
        const auto bar_follows = [&](std::size_t at) {
            return at + 1 < run.size() && run[at + 1].kind == cv_kind::token &&
                   token(run[at + 1]).type == token_type::delim && text(run[at + 1]) == "|";
        };
        // The prefix, then the local name after the `|`. Selectors 4 §3.2: a
        // named prefix that no `@namespace` declared is a syntax error, and the
        // prefix must open its compound. `at` is left on the local name.
        const auto read_namespaced = [&](building & b, ns_prefix kind, std::string_view prefix,
                                         std::size_t & at) {
            if (b.part.ns != ns_prefix::unset || b.part.tag || b.tags != 0 || b.ids != 0 ||
                b.classes != 0) {
                return false;
            }
            // Checked for every caller, even when `prefixes_checked` is false: the
            // only such callers are querySelector and matches, and Selectors API
            // §2 gives them no namespace resolver, so `ns|div` there is a
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
            // knows an element's namespace only as html, svg or other, so `|e`
            // never matches. `*|` constrains nothing and matches as the bare
            // name does.
            if (kind == ns_prefix::none) { dead = true; }
            return true;
        };

        for (std::size_t i = 0; i < run.size(); ++i) {
            const component_value & v = run[i];
            if (v.kind == cv_kind::block) {
                // An attribute selector arrives as one block, because the prelude
                // was parsed as component values, so a `]` inside a quoted value
                // cannot end it early and there is nothing to scan for.
                if (v.open != '[') {
                    dead = invalid_ = true; // A stray `(` or `{` in a prelude.
                    continue;
                }
                if (want_new_compound || compounds.empty()) { start_compound(); }
                if (after_pe) {
                    dead = invalid_ = true;
                    continue;
                }
                attribute_match match;
                if (!parse_attribute(*sheet_, sheet_->children_of(v), *atoms_, match)) {
                    dead = invalid_ = true;
                    continue;
                }
                building & b = compounds.back();
                b.part.attributes.push_back(std::move(match));
                ++b.classes; // An attribute selector is class-level for specificity.
                continue;
            }
            if (v.kind == cv_kind::function) {
                // A functional pseudo-class. The leading `:` was consumed by the
                // colon branch below, which set `pending_pseudo` so this knows the
                // function is one rather than a stray `f(...)` in a prelude.
                if (!pending_pseudo || compounds.empty()) {
                    dead = invalid_ = true;
                    continue;
                }
                pending_pseudo = false;
                std::string_view name = text(v);
                if (!name.empty() && name.back() == '(') { name.remove_suffix(1); }
                if (after_pe &&
                    (view_transition_pe ||
                     !ascii_iequals_any(
                         name,
                         part_pe
                             ? std::initializer_list<std::string_view>{"not", "is", "where",
                                                                       "state", "lang", "dir"}
                             : std::initializer_list<std::string_view>{"not", "is", "where"}))) {
                    dead = invalid_ = true;
                    continue;
                }
                if (!parse_functional(name, v, compounds.back(), invalid_)) { dead = lossy = true; }
                continue;
            }
            const css_token & t = token(v);
            switch (t.type) {
            case token_type::whitespace:
                // Whitespace is a combinator only if a compound follows it, which
                // is decided when the next thing arrives: a trailing space is
                // not a descendant combinator.
                if (!compounds.empty()) { want_new_compound = true; }
                continue;
            case token_type::ident: {
                // A type selector opens its compound or is not one: Selectors 4
                // §4's <compound-selector> is `<type-selector>? <subclass-
                // selector>* ...`, so `[foo='bar']i`, `.a*` and `&div` are
                // syntax errors (attribute-case/syntax.html, CSS Nesting 1 §2)
                // rather than a tag added to what came before.
                const bool opens = want_new_compound || compounds.empty();
                if (opens) { start_compound(); }
                if (after_pe || !opens) {
                    dead = invalid_ = true;
                    continue;
                }
                building & b = compounds.back();
                if (bar_follows(i)) {
                    const std::string_view prefix = text(v);
                    ++i; // The `|`.
                    if (!read_namespaced(b, ns_prefix::named, prefix, i)) {
                        dead = invalid_ = true;
                    }
                    continue;
                }
                // Tags fold to lowercase: HTML tag names are ASCII
                // case-insensitive and the DOM interns them lowercased. The
                // author's spelling is kept beside it because a foreign element
                // does not fold: `linearGradient` is a different name from
                // `lineargradient` and only one of them exists in an SVG.
                b.part.tag = atoms_->intern_lower(text(v));
                b.part.tag_exact = atoms_->intern(text(v));
                b.tags = 1;
                continue;
            }
            case token_type::hash: {
                if (want_new_compound || compounds.empty()) { start_compound(); }
                if (after_pe) {
                    dead = invalid_ = true;
                    continue;
                }
                building & b = compounds.back();
                // A hash whose body could not be an identifier (`#0d6efd`, `#999`)
                // is not a valid id selector, because an identifier may not begin
                // with a digit. `#fff` is one, and does select id="fff".
                if ((t.flags & flag_id_hash) == 0) {
                    dead = invalid_ = true;
                    continue;
                }
                std::string_view name = text(v);
                if (!name.empty()) { name.remove_prefix(1); } // The '#'.
                b.part.id = atoms_->intern(name);
                b.ids = 1;
                continue;
            }
            case token_type::delim: {
                const std::string_view d = text(v);
                if (d == ".") {
                    // The class name is the next token, which must be an ident.
                    if (i + 1 >= run.size() || run[i + 1].kind != cv_kind::token ||
                        token(run[i + 1]).type != token_type::ident) {
                        dead = invalid_ = true;
                        continue;
                    }
                    if (want_new_compound || compounds.empty()) { start_compound(); }
                    if (after_pe) {
                        dead = invalid_ = true;
                        continue;
                    }
                    building & b = compounds.back();
                    b.part.classes.push_back(atoms_->intern(text(run[i + 1])));
                    ++b.classes;
                    ++i; // The ident.
                    continue;
                }
                if (d == "&") {
                    // The nesting selector, CSS Nesting 1 §2: a compound of its
                    // own or part of one; `&.a` and `.a&` are both legal.
                    if (want_new_compound || compounds.empty()) { start_compound(); }
                    if (after_pe) {
                        dead = invalid_ = true;
                        continue;
                    }
                    make_nesting(compounds.back(), dead);
                    continue;
                }
                if (d == "*") {
                    const bool opens = want_new_compound || compounds.empty();
                    if (opens) { start_compound(); }
                    if (after_pe || !opens) {
                        dead = invalid_ = true;
                        continue;
                    }
                    if (bar_follows(i)) {
                        ++i; // The `|`.
                        if (!read_namespaced(compounds.back(), ns_prefix::any, {}, i)) {
                            dead = invalid_ = true;
                        }
                        continue;
                    }
                    // The universal selector constrains nothing and contributes no
                    // specificity: an empty tag atom is universal here.
                    continue;
                }
                // The three explicit combinators. Whitespace either side is
                // irrelevant, so the pending relation is simply overwritten, which
                // is what makes `a > b`, `a>b` and `a >b` one selector.
                if (d == ">" || d == "+" || d == "~") {
                    // Invalid with nothing on its left, or two in a row (`div ++ p`).
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
                // A `|` with nothing before it is the null namespace: `|e` is an
                // element in no namespace at all. The two prefixed forms were read
                // from their prefix and never reach here.
                if (d == "|") {
                    const bool opens = want_new_compound || compounds.empty();
                    if (opens) { start_compound(); }
                    if (after_pe || !opens) {
                        dead = invalid_ = true;
                        continue;
                    }
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
                // `::` starts a pseudo-element. The name after it is kept so
                // `selectorText` can give it back; a name CSS has not defined, or
                // one with no argument that needs one, is a syntax error.
                const bool doubled = i + 1 < run.size() && run[i + 1].kind == cv_kind::token &&
                                     token(run[i + 1]).type == token_type::colon;
                const std::size_t name_at = i + (doubled ? 2 : 1);
                if (name_at >= run.size()) {
                    dead = invalid_ = true; // A bare `:` or `::`.
                    continue;
                }
                const component_value & next = run[name_at];
                if (next.kind == cv_kind::function) {
                    std::string_view name = text(next);
                    if (!name.empty() && name.back() == '(') { name.remove_suffix(1); }
                    if (doubled) {
                        // Every functional pseudo-element takes an argument:
                        // `::picker()` is invalid (CSS-supports-selector-picker).
                        const auto args = sheet_->children_of(next);
                        const bool empty =
                            std::all_of(args.begin(), args.end(), [&](const auto & v) {
                                return v.kind == cv_kind::token &&
                                       token(v).type == token_type::whitespace;
                            });
                        if (!known_functional_pseudo_element(name) || empty) {
                            dead = invalid_ = true;
                            continue;
                        }
                        i = name_at;
                        // `::highlight(x)` and the four `::view-transition-*(x)`
                        // take one identifier and name a pseudo-element the
                        // cascade can run: the compound carries it as
                        // `name(argument)`, which is the spelling
                        // getComputedStyle(el, "::highlight(x)") asks for
                        // (bindings/computed_style, resolvable_pseudo), so
                        // `::highlight(name) { color: green }` is that read's
                        // answer. `::part(x)`, `::slotted(y)`, `::picker(select)`:
                        // real, and nothing here holds the argument, so the
                        // author's bytes are the only record.
                        std::string argument;
                        // A part of a pseudo-element is no part at all:
                        // `::details-content::part(a)` is invalid, and so is
                        // one slotted into it: `::column::slotted(*)`.
                        if (after_pe && ascii_iequals_any(name, {"part", "slotted"})) {
                            dead = invalid_ = true;
                            continue;
                        }
                        // `::slotted(<compound-selector>)`: `::slotted(0)` is
                        // invalid (parse-slotted).
                        if (ascii_iequals(name, "slotted") &&
                            !compound_argument(sheet_->children_of(next))) {
                            dead = invalid_ = true;
                            continue;
                        }
                        button_pe = ascii_iequals(name, "scroll-button");
                        if (ascii_istarts_with(name, "view-transition-") &&
                            !view_transition_argument(next)) {
                            dead = invalid_ = true;
                            continue;
                        }
                        if (ascii_iequals_any(name, functional_pseudo_elements_named) &&
                            single_ident_argument(next, argument)) {
                            b.part.pseudo_element =
                                atoms_->intern_lower(ascii_lower_copy(name) + "(" + argument + ")");
                            ++b.tags;
                            after_pe = true;
                            last_pe = ascii_lower_copy(name);
                            view_transition_pe = ascii_istarts_with(name, "view-transition");
                            part_pe = false;
                            if (in_argument_) { dead = invalid_ = true; }
                            continue;
                        }
                        after_pe = true;
                        last_pe = ascii_lower_copy(name);
                        view_transition_pe = ascii_istarts_with(name, "view-transition");
                        part_pe = ascii_iequals(name, "part");
                        if (in_argument_) { invalid_ = true; }
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
                    // ponytail: @supports knows no vendor's pseudo-elements;
                    // list the real ones (`::-webkit-scrollbar`) if a page asks.
                    if (!known_pseudo_element(name) ||
                        (!sheet_->forgiving && name.starts_with('-'))) {
                        dead = invalid_ = true;
                        continue;
                    }
                    // A pseudo-element after another is ::marker of ::before or
                    // ::after (CSS Pseudo 4 §4.3), or one of an element-backed
                    // pseudo-element (::part(), ::slotted(), ::picker() or
                    // ::details-content); `::before::before`, `::selection::after`
                    // and `::placeholder::marker` are invalid.
                    if (after_pe) {
                        const bool marker_of = ascii_iequals(name, "marker") &&
                                               (last_pe == "before" || last_pe == "after");
                        if (marker_of) {
                            b.part.pseudo_element = atoms_->intern_lower(last_pe + "::marker");
                            last_pe = "marker";
                            dead = true;
                            continue;
                        }
                        // CSS Multicol 2's column is allowed too: `::before::column`,
                        // and its scroll marker, `::column::scroll-marker`.
                        // Nothing here generates either box.
                        const bool column_of =
                            (ascii_iequals(name, "column") &&
                             (last_pe == "before" || last_pe == "after")) ||
                            (ascii_iequals(name, "scroll-marker") && last_pe == "column");
                        if (column_of) {
                            last_pe = ascii_lower_copy(name);
                            dead = lossy = true;
                            continue;
                        }
                        if (!ascii_iequals_any(last_pe,
                                               {"part", "slotted", "picker", "details-content"})) {
                            dead = invalid_ = true;
                            continue;
                        }
                    }
                    b.part.pseudo_element = atoms_->intern_lower(name);
                    ++b.tags; // A pseudo-element is type-level for specificity.
                    after_pe = true;
                    last_pe = ascii_lower_copy(name);
                    // ::view-transition itself is the tree's root: `:only-child`
                    // follows its descendants, not it (pseudo-elements-invalid).
                    view_transition_pe = ascii_istarts_with(name, "view-transition-");
                    part_pe = ascii_iequals(name, "details-content");
                    button_pe = false;
                    if (in_argument_) { dead = invalid_ = true; }
                    // A pseudo-element has a cascade (engine::resolve_pseudo runs
                    // it for getComputedStyle(el, "::before")), and the matcher
                    // keeps the compound from every element (`pseudo_wanted_`). A
                    // vendor's is whatever the vendor says it is, and matches nothing.
                    if (name.starts_with('-')) { dead = true; }
                    continue;
                }
                // A highlight pseudo-element takes no pseudo-class after it at
                // all (CSS Pseudo 4 §3.6): `::selection:hover` is invalid. Nor
                // does the view transition tree's root, a column or a slotted
                // element (pseudo-elements-invalid, column-pseudo-invalid,
                // parse-slotted).
                const bool highlight_pe = ascii_iequals_any(
                    last_pe, {"selection", "target-text", "spelling-error", "grammar-error",
                              "search-text", "highlight", "view-transition", "column", "slotted"});
                if (after_pe &&
                    (highlight_pe ||
                     !(view_transition_pe ? ascii_iequals(name, "only-child")
                       // Not about the tree: `:disabled` and `:checked` are
                       // bits here too, and a part takes both (parse-part).
                       : part_pe
                           ? !ascii_iequals_any(name, {"root", "empty", "first-child", "last-child",
                                                       "only-child", "first-of-type",
                                                       "last-of-type", "only-of-type"})
                           : ascii_iequals_any(name, {"hover", "active", "focus", "focus-visible",
                                                      "focus-within"}) ||
                                 (button_pe &&
                                  ascii_iequals_any(name, {"enabled", "disabled"}))))) {
                    dead = invalid_ = true;
                    continue;
                }
                if (const std::uint32_t bit = state_bit_of(name); bit != 0) {
                    b.part.states |= bit;
                    if (after_pe) { b.part.states_after_pe |= bit; }
                    ++b.classes; // A pseudo-class is class-level for specificity.
                    continue;
                }
                if (const std::uint32_t bit = structural_bit_of(name); bit != 0) {
                    b.part.structural |= bit;
                    if (after_pe) { b.part.structural_after_pe |= bit; }
                    ++b.classes;
                    if (bit == structural_scope) { saw_scope_ = true; }
                    // The CSSOM serialiser has no name for these four bits, so a
                    // selector holding one serialises from the author's bytes
                    // (`dropped`'s job) rather than losing the pseudo-class.
                    if (bit == structural_defined || bit == structural_placeholder_shown ||
                        bit == structural_valid || bit == structural_invalid) {
                        b.part.dropped = true;
                    }
                    continue;
                }
                // `:heading` matches every h1-h6 (Selectors 5).
                if (ascii_iequals(name, "heading")) {
                    pseudo_ref ref;
                    ref.kind = pseudo_kind::heading;
                    b.part.pseudos.push_back(std::move(ref));
                    ++b.classes;
                    continue;
                }
                // A pseudo-class CSS defines but this engine cannot observe is
                // accepted and never matches; a name CSS does not define, such as
                // `:gibberish`, is a syntax error.
                if (!known_pseudo_class(name)) {
                    dead = invalid_ = true;
                    continue;
                }
                dead = lossy = true;
                continue;
            }
            default:
                // A number, a string, a percentage: none of them can appear in a
                // selector at this level.
                dead = invalid_ = true;
                continue;
            }
        }

        // An empty alternative is a syntax error, and it is how
        // `querySelector("")`, `a,,b` and a trailing comma all arrive here: `run`
        // holds nothing but whitespace, so no compound was ever started (or, for
        // a relative one, nothing but the synthetic `:scope` anchor).
        if (compounds.size() <= (relative_ ? 1u : 0u) || dangling_combinator) {
            invalid_ = true;
            push_dead();
            return;
        }
        // Implicit nesting, CSS Nesting 1 §2.1: a selector in a nested style
        // rule that names no `&` is `& <selector>`. Not inside `@scope`, whose
        // in-scope test already restricts the subject, and not for the
        // synthetic anchor of a `:has()` argument.
        if (nesting_ != nullptr && !nesting_->parent.empty() && !saw_nesting_ && !relative_) {
            building anchor;
            make_nesting(anchor, dead);
            compounds.insert(compounds.begin(), std::move(anchor));
            links.insert(links.begin(), combinator::descendant);
        }
        // A dead alternative keeps its compounds. The rightmost is what the
        // matcher and the rule index consult first, so the flag goes there; the
        // CSSOM reads `dropped` to decide between the canonical serialisation
        // and the author's bytes.
        if (dead) {
            compounds.back().part.never_matches = true;
            compounds.back().part.dropped = lossy;
        }

        // A default namespace puts every unprefixed compound in it, the implied
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
        // Rightmost first, because that is the order matching walks them. `links`
        // was built left to right; reversing means each link is read from the
        // compound to its right, which is the direction the walk moves.
        for (std::size_t i = compounds.size(); i-- > 0;) {
            out.parts.push_back(compounds[i].part);
            if (i > 0) { out.links.push_back(links[i - 1]); }
        }
        sheet_->selectors.push_back(std::move(out));
    }

    stylesheet * sheet_;
    atom_table * atoms_;
    bool invalid_ = false;
    bool relative_ = false; // Parsing the argument of `:has()`.
    bool in_has_ = false;   // Parsing anything inside one; `:has()` may not nest.
    /// Parsing the argument of `:not()`, `:is()`, `:where()` or `:has()`, where
    /// a pseudo-element is a syntax error (Selectors 4 §3.6.3).
    bool in_argument_ = false;
    /// Parsing a <compound-selector> (the argument of `:host()` or
    /// `::slotted()`), where a combinator is a syntax error at any depth: a
    /// `:not()` of one fails, an `:is()` forgives by dropping it
    /// (parse-not, parse-is-where; csswg-drafts#5093).
    bool compound_only_ = false;
    const nesting_context * nesting_ = nullptr;
    static constexpr int max_depth = 32;
    int depth_ = 0; // Functional pseudo-classes around this parse, capped at max_depth.
    /// Per alternative: whether it wrote `&`, and whether it names the scoping
    /// root (`:scope`, or `&` where that is what `&` means).
    bool saw_nesting_ = false;
    bool saw_scope_ = false;
};

} // namespace ctbrowser::style::css::selector_detail
