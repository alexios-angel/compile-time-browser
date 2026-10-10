//===- ctbrowser/lib/Style/engine/cascade.cpp - The cascade -----*- C++ -*-===//
//
// engine::resolve, the cascade for one element: match rules, sort them by
// cascade priority, substitute var()/attr()/if(), fold math, expand
// shorthands, map logical to physical properties, and split the result into
// inherited and own declarations.
//
//===----------------------------------------------------------------------===//

#include "cascade_values.hpp"

#include <ctbrowser/style/engine.hpp>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/style/css/boolean.hpp>

#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace ctbrowser::style {

namespace {

/// The most bytes a custom property's value may grow to through inherit(): the
/// same 64 KB css/substitute gives a var() substitution.
constexpr std::size_t max_custom_property_bytes = 64 * 1024;

/// Returns a font-size keyword in pixels (CSS Fonts 4 §2.5): an
/// <absolute-size> from the table at a 16px `medium` (the same table as
/// easing/numeric.cpp), a <relative-size> as the parent's times or divided by
/// 1.2, and `initial` as `medium`. Returns nullopt for anything else.
[[nodiscard]] std::optional<float> font_size_keyword_px(std::string_view word, float parent) {
    static constexpr std::pair<std::string_view, float> sizes[] = {
        {"xx-small", 9}, {"x-small", 10}, {"small", 13},    {"medium", 16},
        {"large", 18},   {"x-large", 24}, {"xx-large", 32}, {"xxx-large", 48}};
    for (const auto & [name, px] : sizes) {
        if (ascii_iequals(word, name)) { return px; }
    }
    if (ascii_iequals(word, "initial")) { return 16.0f; }
    if (ascii_iequals(word, "larger")) { return parent * 1.2f; }
    if (ascii_iequals(word, "smaller")) { return parent / 1.2f; }
    return std::nullopt;
}

} // namespace

computed_style_ptr engine::resolve(const read_txn & txn, node_id node, const element_facts & self,
                                   const ancestor_filter & ancestors, std::size_t depth,
                                   const computed_style_ptr & parent) {
    // `parent` is the parent's whole style, not just its inherited half,
    // because the `inherit` keyword takes the parent's value for any property,
    // inherited or not: `display: inherit` has to read a property the inherited
    // half never carries.
    const inherited_ptr & from = parent ? parent->inherited : no_inherited_;
    // Where this element sits among its siblings, for `sibling-index()` and
    // `sibling-count()`: facts the traversal already gathered for
    // `:nth-child`, handed to every math function this element folds.
    sibling_index_ = self.sibling_index;
    sibling_count_ = self.sibling_count;
    element_key_ = key_of(node);
    // The containers this element's `cq*` units measure, as last laid out.
    // A pseudo-element's are its originating element's, including that element
    // itself as a container.
    cq_width_ = cq_height_ = -1.0f;
    const std::size_t below = depth + (pseudo_wanted_ != atom{} ? 1 : 0);
    if (below > 0 && below - 1 < cq_chain_.size()) {
        const auto [inline_container, block_container] = cq_chain_[below - 1];
        if (inline_container) {
            if (const auto box = container_size_of(inline_container)) { cq_width_ = box->width; }
        }
        if (block_container) {
            if (const auto box = container_size_of(block_container)) { cq_height_ = box->height; }
        }
    }
    // Gather only the rules whose rightmost compound could possibly match.
    matches_.clear();
    collect(index_.by_id, self.id, txn, ancestors, depth);
    for (const atom c : self.classes) { collect(index_.by_class, c, txn, ancestors, depth); }
    collect(index_.by_tag, self.tag, txn, ancestors, depth);
    for (const rule & r : index_.universal) { consider(r, txn, ancestors, depth); }

    // The cascade order (CSS Cascade 5 §6.1): importance, then origin (reversed
    // for important declarations, where the user agent's beat the author's),
    // then the layer, whose order is likewise reversed for important
    // declarations (§6.4), then specificity, then scope proximity (Cascade 6
    // §6.3, nearer wins), then source order. Sorting ascending and applying in
    // order means the last write to a property wins, which is exactly the rule.
    std::ranges::stable_sort(matches_, [this](const rule & a, const rule & b) {
        if (a.important != b.important) { return !a.important; }
        if (a.origin != b.origin) {
            return a.important ? a.origin > b.origin : a.origin < b.origin;
        }
        if (a.layer != b.layer) {
            const std::uint32_t ra = layer_rank_[a.layer];
            const std::uint32_t rb = layer_rank_[b.layer];
            return a.important ? ra > rb : ra < rb;
        }
        const specificity sa = selectors_[a.selector].spec;
        const specificity sb = selectors_[b.selector].spec;
        if (sa != sb) { return sa < sb; }
        if (a.proximity != b.proximity) { return a.proximity > b.proximity; }
        return a.order < b.order;
    });

    declaration_list out;
    // Applying a declaration means replacing the property if it is already
    // there: the later write wins, which is what the cascade reduces to once the
    // sort has put everything in priority order.
    //
    // The explicit-defaulting keywords are resolved here, because here is where
    // the parent's value is in hand:
    //
    //   inherit   take the parent's value, whether or not the property inherits
    //   initial   an empty value, which shadows anything inherited and reads as
    //             "nothing said", so consumers fall back to the property
    //             table's initial value (`display` is written as `inline` and a
    //             custom property gets the guaranteed-invalid value, see put)
    //   unset     drop the declaration: for an inherited property the inherited
    //             value then shows through, and for a non-inherited one absence
    //             already means initial, so dropping is correct for both
    //   revert    the value the previous origin would have produced
    //   revert-layer, revert-rule
    //             likewise, one layer or one rule back - see rolled_back
    //
    // Rollbacks search the declarations produced by the fold, after variable
    // substitution, shorthand expansion and logical-to-physical mapping. The
    // raw rule may say `margin` or `margin-block`, neither of which would match
    // a rollback of `margin-top`. Each entry keeps its rule index in matches_
    // for the origin and layer tests.
    std::vector<std::pair<std::size_t, declaration>> applied;
    std::vector<std::pair<atom, std::size_t>> custom_positions;
    // `folding` is the current rule's position, or the size for inline style.
    std::size_t folding = matches_.size();
    const auto rolled_back = [this, &applied](atom property, std::size_t limit, const auto & keep) {
        for (std::size_t i = applied.size(); i-- > 0;) {
            const auto & [at, d] = applied[i];
            if (at < limit && d.property == property && keep(matches_[at])) { return i; }
        }
        return applied.size();
    };
    const auto put = [&out, &parent, &folding, &rolled_back, &applied, &custom_positions,
                      this](const declaration & d) {
        std::string value = d.value;
        const std::string_view property = atoms_->text(d.property);
        // The rollback keywords are each the cascade with some declarations
        // struck out (CSS Cascade 5 §7.3): `revert` strikes the author origin,
        // so the answer is the last user-agent declaration (important UA
        // rules that sort after the author's still apply later); `revert-layer`
        // strikes the current layer of the current origin, the style
        // attribute counting as a layer above all of them; `revert-rule` (a
        // Cascade 6 draft) strikes the current rule, meaning the declarations
        // filed consecutively under one selector. What is found may be a
        // keyword itself, so this loops, always to an earlier position; the
        // guard of 16 only bounds the loop.
        std::size_t at = folding;
        for (int guard = 0; guard < 16; ++guard) {
            std::size_t found = applied.size();
            if (value == "revert") {
                found = rolled_back(d.property, matches_.size(),
                                    [](const rule & r) { return r.origin == 0; });
            } else if (value == "revert-layer") {
                const rule * self = at < matches_.size() ? &matches_[at] : nullptr;
                found = rolled_back(d.property, at, [self](const rule & r) {
                    return self == nullptr || r.origin != self->origin || r.layer != self->layer;
                });
            } else if (value == "revert-rule") {
                const rule * self = at < matches_.size() ? &matches_[at] : nullptr;
                // add_sheet files one selector copy per block, so the same
                // selector index is the same block.
                found = rolled_back(d.property, at, [self](const rule & r) {
                    return self == nullptr || r.selector != self->selector;
                });
            } else {
                break;
            }
            if (found == applied.size()) {
                value = "unset";
                break;
            }
            at = applied[found].first;
            value = applied[found].second.value;
        }
        if (folding < matches_.size()) {
            applied.emplace_back(folding, declaration{d.property, value});
        }
        if (property.starts_with("--")) {
            const auto position = std::ranges::find(custom_positions, d.property,
                                                    &std::pair<atom, std::size_t>::first);
            if (position == custom_positions.end()) {
                custom_positions.emplace_back(d.property, at);
            } else {
                position->second = at;
            }
        }
        // `display`'s initial value is `inline`, and it must be written out: to
        // the box builder an absent display means the element's own default
        // (what the trimmed UA sheet leaves unwritten), so an empty one would
        // make `div { display: initial }` a block (WPT css-display/inheritance).
        // An `unset` that a rollback arrived at is the UA's answer and stays
        // absent.
        if (property == "display" &&
            (value == "initial" || (value == "unset" && d.value == "unset"))) {
            value = "inline";
        }
        if (value == "inherit") {
            value = std::string{parent ? parent->get(d.property) : std::string_view{}};
        } else if (value == "unset" || value == "revert") {
            for (std::size_t i = 0; i < out.size(); ++i) {
                if (out[i].property == d.property) {
                    out.erase(out.begin() + static_cast<std::ptrdiff_t>(i));
                    break;
                }
            }
            // Removing the declaration lets an inherited property show the
            // parent's value and leaves a non-inherited one at its initial value.
            if (!inherits(property)) { return; }
            return;
        } else if (value == "initial") {
            // On a custom property `initial` is the guaranteed-invalid value,
            // not an empty one: `--bs-table-bg-type: initial` is a sentinel that
            // `var(--bs-table-bg-type, <fallback>)` has to fall through, while
            // an empty custom property (`--bs-btn-font-family: ;`) is a valid
            // empty substitution.
            //
            // The declaration is kept, holding the sentinel, rather than
            // erased: erasing it would let an ancestor's value show through,
            // and `initial` means invalid here regardless of what was
            // inherited.
            if (property.starts_with("--")) {
                value = std::string{guaranteed_invalid};
            } else {
                value.clear();
            }
        }
        for (declaration & existing : out) {
            if (existing.property == d.property) {
                existing.value = std::move(value);
                return;
            }
        }
        out.push_back(declaration{d.property, std::move(value)});
    };

    // The style attribute is not a separate origin: it is author-level with a
    // specificity above every selector, so it lands between the normal
    // declarations and the important ones. Chrome and Firefox both give
    //
    //   normal selector  <  normal inline  <  important selector  <
    //   important inline
    //
    // which is why this is spliced into the fold at the importance
    // boundary rather than simply appended at the end: `!important` in a
    // stylesheet has to be able to beat a style attribute. A pseudo-element
    // has no style attribute.
    static const inline_block no_inline;
    const inline_block & own = pseudo_wanted_ ? no_inline : inline_style_of(txn, node);

    // The declarations are folded in two main passes because custom properties
    // are themselves cascaded: substitution cannot run inside the fold that
    // produces the values it needs to read. So pass one applies only custom
    // properties, and pass two substitutes everything else against them. (The
    // font-size, line-height and registered-property pre-passes in between use
    // the same fold.)
    //
    // Every pass walks the same sorted list with the same inline-style splice,
    // so priority is identical between them, and expansion in pass two keeps
    // source order for free: a shorthand's longhands land at the shorthand's
    // position in the fold, so a longhand written after it still wins and one
    // written before it is still overwritten. Expansion cannot happen earlier,
    // when a rule is recorded, because a shorthand's component count is
    // unknowable before substitution: `border: var(--w) solid var(--c)` cannot be
    // split into longhands until the var()s are gone.
    const auto fold = [&](const auto & apply) {
        bool spliced = false;
        for (std::size_t i = 0; i < matches_.size(); ++i) {
            const rule & r = matches_[i];
            if (r.important && !spliced) {
                folding = matches_.size();
                for (const declaration & d : own.normal) { apply(d); }
                spliced = true;
            }
            folding = i;
            apply(declarations_[r.declaration]);
        }
        folding = matches_.size();
        if (!spliced) {
            for (const declaration & d : own.normal) { apply(d); }
        }
        for (const declaration & d : own.important) { apply(d); }
    };

    // Pass one: the custom properties, stored verbatim. A custom property's
    // value is never parsed and never validated; it is a token stream that
    // means whatever the var() reading it makes of it.
    //
    // The exception is an `inherit()` in one, which is replaced now (CSS Values
    // 5 §inherit-notation): it names the parent's computed value, which the
    // parent's cascade has just produced. Reading it lazily later would need
    // the parent's substitution re-run in the parent's scope, and the
    // grandparent's, or `--v: e2 inherit(--v)` under `--v: e1` would not
    // accumulate (WPT inherit-function-basic). A var() in the same value stays
    // lazy, as every var() in a custom property is.
    fold([&](const declaration & d) {
        if (!atoms_->text(d.property).starts_with("--")) { return; }
        if (d.value.find("inherit(") == std::string::npos) {
            put(d);
            return;
        }
        const css::token_stream s = css::tokenize(d.value);
        std::string value;
        for (std::size_t i = 0; i + 1 < s.tokens.size(); ++i) {
            const css::css_token & t = s.tokens[i];
            if (t.type != css::token_type::function || !ascii_iequals(s.text_of(t), "inherit(")) {
                value += s.text_of(t);
                continue;
            }
            const std::size_t close = css::end_of_block(s, i);
            const std::size_t last =
                s.tokens[close - 1].type == css::token_type::close_paren ? close - 1 : close;
            std::string inner;
            for (std::size_t v = i + 1; v < last; ++v) { inner += s.text_of(s.tokens[v]); }
            const std::size_t comma = inner.find(',');
            const std::string_view name =
                trim(std::string_view{inner}.substr(0, comma), html_whitespace);
            const std::string_view held =
                parent && name.starts_with("--") ? parent->get(atoms_->intern(name)) : "";
            if (!held.empty() && held != guaranteed_invalid) {
                value += held;
                // `--v: inherit(--v) inherit(--v)` doubles at every level, so
                // thirty nested elements are gigabytes. Past the bytes a var()
                // substitution may produce (substitute's max_bytes) the
                // property is guaranteed-invalid, as a too-long var() is.
                if (value.size() > max_custom_property_bytes) {
                    value = std::string{guaranteed_invalid};
                    break;
                }
            } else if (comma != std::string::npos) {
                value += trim(std::string_view{inner}.substr(comma + 1), html_whitespace);
            } else {
                value = std::string{guaranteed_invalid};
                break;
            }
            i = close - 1;
        }
        put(declaration{d.property, std::move(value)});
    });

    // `nullopt` means not defined, which is what makes `var()` take its
    // fallback; an empty string means defined and empty, which substitutes to
    // nothing. The guaranteed-invalid sentinel reads as the first of those,
    // which is exactly what CSS Variables §3 says `initial` does to a custom
    // property.
    const auto lookup = [&out, &parent](atom name) -> std::optional<std::string_view> {
        const auto answer = [](std::string_view held) -> std::optional<std::string_view> {
            if (held == guaranteed_invalid) { return std::nullopt; }
            return held;
        };
        for (const declaration & d : out) {
            if (d.property == name) { return answer(d.value); }
        }
        if (parent && parent->inherited) {
            for (const declaration & d : parent->inherited->declarations) {
                if (d.property == name) { return answer(d.value); }
            }
        }
        return std::nullopt;
    };
    // The element's attributes, for `attr()`. Absent and empty are
    // different answers here too: `attr(data-x)` on an element without the
    // attribute takes its fallback, and one with `data-x=""` is `""`.
    const css::attribute_lookup attributes =
        [&txn, node, this](std::string_view name) -> std::optional<std::string> {
        const atom key = atoms_->intern(name);
        if (!txn.has_attribute(node, key)) { return std::nullopt; }
        return std::string{txn.attribute_value(node, key)};
    };
    // What `if()` may ask (CSS Values 5 §if-notation): the parent's
    // custom properties for `style(--x: inherit)`, substituted against the
    // parent's own scope; any property folded so far for `style(color:
    // green)`; and the window for `media()`. The bases are filled in below
    // once the font size is known, and the property per declaration.
    css::condition_environment conditions;
    conditions.inherited = [&parent, this](std::string_view name) -> std::optional<std::string> {
        if (!parent) { return std::nullopt; }
        // `get` reads the parent's own half too, where a registered
        // property that does not inherit lives.
        const std::string_view held = parent->get(atoms_->intern(name));
        if (held.empty() || held == guaranteed_invalid) { return std::nullopt; }
        const css::custom_lookup above = [&parent](atom n) -> std::optional<std::string_view> {
            const std::string_view v = parent->get(n);
            if (v.empty() || v == guaranteed_invalid) { return std::nullopt; }
            return v;
        };
        return css::substitute_var(held, above, *atoms_);
    };
    conditions.computed = [&out, &parent,
                           this](std::string_view name) -> std::optional<std::string> {
        const atom key = atoms_->intern(name);
        for (const declaration & d : out) {
            if (d.property == key) { return d.value; }
        }
        if (parent && parent->inherited) {
            const std::string_view held = parent->inherited->get(key);
            if (!held.empty()) { return std::string{held}; }
        }
        return std::nullopt;
    };
    conditions.media = [this](std::string_view text) {
        return css::evaluate_media_condition(text, environment_);
    };
    conditions.registered = [this](std::string_view name) {
        return registration_of(atoms_->intern(name));
    };
    conditions.functions = [this](std::string_view name) {
        return function_of(atoms_->intern(name));
    };

    // A custom property's substituted CSS-wide keyword belongs to its own
    // cascade, before another var() reads it. Resolve dependencies first, but
    // keep ordinary token streams lazy (including their attr() provenance).
    // Both limits of 20 bound pathological custom-property chains: past them a
    // keyword is left unresolved rather than looping.
    std::vector<atom> resolving_keywords, resolved_keywords;
    const std::function<void(atom)> resolve_keyword = [&](atom name) {
        if (resolving_keywords.size() >= 20 ||
            std::ranges::find(resolved_keywords, name) != resolved_keywords.end() ||
            std::ranges::find(resolving_keywords, name) != resolving_keywords.end()) {
            return;
        }
        resolving_keywords.push_back(name);
        for (int guard = 0; guard < 20; ++guard) {
            const auto own = std::ranges::find(out, name, &declaration::property);
            if (own == out.end() || !css::may_have_var(own->value)) { break; }
            const std::string value = own->value;
            css::condition_environment scope = conditions;
            scope.property = std::string{atoms_->text(name)};
            scope.on_read = [&](std::string_view dependency) {
                resolve_keyword(atoms_->intern(dependency));
            };
            const auto substituted =
                css::substitute_var(value, lookup, *atoms_, attributes, &scope);
            if (!substituted) { break; }
            const std::string_view word = trim(*substituted, html_whitespace);
            if (!css::is_wide_keyword(word)) { break; }
            const auto position =
                std::ranges::find(custom_positions, name, &std::pair<atom, std::size_t>::first);
            const std::size_t saved = folding;
            folding = position == custom_positions.end() ? matches_.size() : position->second;
            put(declaration{name, ascii_lower_copy(word)});
            folding = saved;
        }
        resolving_keywords.pop_back();
        resolved_keywords.push_back(name);
    };
    // put() may erase an unset property; iterate names, not declarations.
    std::vector<atom> custom_names;
    for (const declaration & d : out) { custom_names.push_back(d.property); }
    for (const atom name : custom_names) { resolve_keyword(name); }

    // A `font` shorthand carries a font-size and a line-height of its own,
    // and the font-size and line-height pre-passes below need those before
    // pass two splits it.
    const atom font_ = atoms_->intern_lower("font");
    const auto component_of = [&](const declaration & d,
                                  atom wanted) -> std::optional<std::string> {
        if (d.property == wanted) { return d.value; }
        if (d.property != font_ || css::may_have_var(d.value)) { return std::nullopt; }
        for (const css::declaration & one : cascade_detail::split_via_block("font", d.value)) {
            if (atoms_->intern_lower(one.name) == wanted) { return one.value; }
        }
        return std::nullopt;
    };

    // Font-size pre-pass: resolve font-size before any other property, because
    // every other `em` depends on it.
    //
    // `em` means the element's own font size on every property except font-size
    // itself, where it means the parent's, so the one value everything else is
    // relative to has to be known before the general fold runs. This is
    // required, not a convenience: `padding: calc(.5em + 1rem)` and
    // `font-size: 1.25em` in the
    // same rule resolve their `em` against different numbers, and a single pass
    // cannot produce both.
    float parent_font_size = 16.0f;
    if (parent && parent->inherited) {
        for (const declaration & d : parent->inherited->declarations) {
            if (d.property != font_size_) { continue; }
            if (const auto px = css::length_text_to_px(d.value, font_context(16.0f))) {
                parent_font_size = *px;
            }
            break;
        }
    }
    // The parent's line height, for `lh` (CSS Values 4 §6.1.1). It has the
    // same asymmetry as `em`: `lh` in `font-size` and in `line-height` itself
    // is the parent's, everywhere else the element's own. The parent's
    // line-height is inherited text (a number, a length the parent's cascade
    // already folded to px, a percentage or `normal`) and resolves against the
    // parent's font size.
    //
    // The root resolves against the initial values: `font-size: 1lh` on
    // `:root` is 1.25 times 16px, however the root's own font size and
    // line-height come out (WPT lh-rlh-on-root-001).
    const float parent_line_height =
        parent && parent->inherited
            ? line_height_px(parent->inherited->get(line_height_), parent_font_size)
            : 16.0f * 1.25f;
    // The root starts from the initial values: a `font-size: 3rem` on `:root`
    // is 48px however the previous resolve of this document left the root,
    // because the two members below persist across resolves (WPT
    // rem-unit-root-element).
    if (!parent) {
        root_font_size_ = 16.0f;
        root_line_height_ = parent_line_height;
    }
    // The parent's `0` advance, for `ch` in `font-size` and the other font-*
    // properties, where it is the parent's like `em` (CSS Values 4 §6.1.1):
    // the parent's inherited font-family, font-weight and font-style at the
    // parent's size. Measured only when the shell injected a measurement;
    // otherwise zero, and `ch` takes its half-em fallback.
    std::string_view face_family =
        parent && parent->inherited ? parent->inherited->get(font_family_) : std::string_view{};
    std::string_view face_weight =
        parent && parent->inherited ? parent->inherited->get(font_weight_) : std::string_view{};
    std::string_view face_style =
        parent && parent->inherited ? parent->inherited->get(font_style_) : std::string_view{};
    const float parent_zero_advance =
        measure_ ? zero_advance_of(face_family, face_weight, face_style, parent_font_size) : 0.0f;
    float own_font_size = parent_font_size;
    std::vector<atom> cyclic_registered;
    // The root's registered custom properties that measured the viewport
    // before they were computed to pixels - see the root's overflow below.
    std::vector<atom> viewport_registered;
    std::vector<atom> read; // What the font-size's substitution looked at.
    // Whether the winning font-size declaration actually resolved to a length,
    // a keyword included (see font_size_keyword_px). One that does not (`math`
    // and anything unmodelled) keeps its text, and whoever reads it decides.
    bool font_size_resolved = false;
    {
        // The em basis for resolving font-size is the parent's; the rem basis is
        // the root's, which for the root element is its own answer and so is
        // seeded from the parent's: a root `font-size: 2rem` is circular, and
        // CSS resolves it against the initial 16px.
        // A percentage in a font size is of the parent's, and the evaluator can
        // be told so: `calc(50% + 1px)` folds here rather than waiting for a
        // containing block it will never be measured against.
        css::length_context ctx =
            font_context(parent_font_size, parent_line_height, parent_zero_advance);
        ctx.percent_basis = parent_font_size;
        conditions.lengths = ctx;
        conditions.property = "font-size";
        // A registered property in font-relative units depends on this font
        // size, so a font-size reading it is a cycle (CSS Properties and
        // Values API 1 §2.4): the font-size is invalid at computed-value time
        // (so inherited) and the property takes its initial value (WPT
        // typed_arithmetic_cycle).
        read.clear();
        conditions.on_read = [&read, this](std::string_view name) {
            read.push_back(atoms_->intern(name));
        };
        fold([&](const declaration & d) {
            const std::optional<std::string> held = component_of(d, font_size_);
            if (!held) { return; }
            std::string value{*held};
            if (css::may_have_var(value)) {
                read.clear();
                const std::optional<std::string> done =
                    css::substitute_var(value, lookup, *atoms_, attributes, &conditions);
                for (const atom name : read) {
                    if (registration_of(name) == nullptr) { continue; }
                    const std::optional<std::string_view> held = lookup(name);
                    if (held && font_relative(*held, !parent)) {
                        cyclic_registered.push_back(name);
                        return;
                    }
                }
                if (!done) { return; }
                value = *done;
            }
            // A calc that does not evaluate keeps its text here and falls
            // through to the keyword branch below, which is right for a font
            // size, because a keyword is a real answer for one.
            if (css::may_have_math(value)) {
                value = css::fold_math(value, ctx, css::math_context::length).text;
            }
            const std::optional<float> px = css::length_text_to_px(value, ctx);
            // A percentage font size is the parent's, scaled: the one relative
            // form that is not a length and still has an answer here.
            const std::string_view text = trim(value, html_whitespace);
            if (px) {
                own_font_size = *px;
                font_size_resolved = true;
            } else if (text.ends_with('%')) {
                float share = 0;
                const char * begin = text.data();
                if (std::from_chars(begin, begin + text.size() - 1, share).ec == std::errc{}) {
                    own_font_size = parent_font_size * share / 100.0f;
                    font_size_resolved = true;
                }
            } else if (ascii_iequals(text, "inherit")) {
                own_font_size = parent_font_size;
                font_size_resolved = true;
            } else if (const std::optional<float> keyword =
                           font_size_keyword_px(text, parent_font_size)) {
                own_font_size = *keyword;
                font_size_resolved = true;
            } else {
                font_size_resolved = false; // Unmodelled value (e.g. `math`): keep the text.
            }
        });
        // Only the font-size cares what was read.
        conditions.on_read = nullptr;
    }
    // The root's size is what every `rem` in the document resolves against, so it
    // is recorded as the tree is descended rather than looked up per element.
    if (!parent) { root_font_size_ = own_font_size; }
    // The element's own `0` advance, now that its font size is known:
    // its own winning font-family/weight/style declarations over the
    // parent's face (a value still holding a var() is the parent's).
    float own_zero_advance = 0.0f;
    if (measure_) {
        fold([&](const declaration & d) {
            if (d.property != font_family_ && d.property != font_weight_ &&
                d.property != font_style_) {
                return;
            }
            if (css::may_have_var(d.value)) { return; }
            (d.property == font_family_   ? face_family
             : d.property == font_weight_ ? face_weight
                                          : face_style) = d.value;
        });
        own_zero_advance = zero_advance_of(face_family, face_weight, face_style, own_font_size);
        if (!parent) { root_zero_advance_ = own_zero_advance; }
    }
    // Line-height pre-pass, for `lh` in every other property.
    // The winning `line-height` declaration is substituted and folded against
    // the parent's `lh` and the element's own `em`, then resolved to px. An
    // absent declaration is the inherited text against the element's own
    // font size: a `1.5` is a factor and inherits as one.
    float own_line_height = line_height_px(
        parent && parent->inherited ? parent->inherited->get(line_height_) : "", own_font_size);
    // A percentage in a line-height is of the element's own font size, so
    // `calc(10% / 1px)` is the number 1 here (WPT typed_arithmetic).
    css::length_context line_height_lengths =
        font_context(own_font_size, parent_line_height, own_zero_advance);
    line_height_lengths.percent_basis = own_font_size;
    {
        const css::length_context & ctx = line_height_lengths;
        conditions.lengths = ctx;
        conditions.property = "line-height";
        fold([&](const declaration & d) {
            const std::optional<std::string> held = component_of(d, line_height_);
            if (!held) { return; }
            std::string value{*held};
            if (css::may_have_var(value)) {
                const std::optional<std::string> done =
                    css::substitute_var(value, lookup, *atoms_, attributes, &conditions);
                if (!done) { return; }
                value = *done;
            }
            if (css::may_have_math(value)) { value = css::fold_math(value, ctx).text; }
            // A bare number is a factor, not pixels, which is why
            // length_text_to_px is asked only after from_chars has had its
            // turn; a percentage and `normal` are line_height_px's.
            const std::string_view text = trim(value, html_whitespace);
            float number = 0;
            const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), number);
            if (ec == std::errc{} && ptr == text.data() + text.size()) {
                own_line_height = number * own_font_size;
            } else if (const auto px = css::length_text_to_px(value, ctx);
                       px && !text.ends_with('%')) {
                own_line_height = *px;
            } else if (ascii_iequals(text, "inherit")) {
                own_line_height = line_height_px(
                    parent && parent->inherited ? parent->inherited->get(line_height_) : "",
                    own_font_size);
            } else {
                own_line_height = line_height_px(value, own_font_size);
            }
        });
    }
    if (!parent) { root_line_height_ = own_line_height; }
    // The element's own writing mode, settled before any length resolves:
    // `vi`/`vb` swap axes in a vertical one (CSS Values 4 §6.1.2), and the
    // property inherits, so the parent's answer stands until a declaration
    // says otherwise. It is a keyword, never a length, so there is nothing to
    // fold. Its direction is settled the same way; the two together decide
    // which physical side a logical property lands on (CSS Logical 1 §2).
    const auto inherited_keyword = [&](atom property, std::string_view initial) {
        std::string held;
        if (parent && parent->inherited) {
            held = ascii_lower_copy(parent->inherited->get(property));
        }
        if (held.empty()) { held = initial; }
        fold([&](const declaration & d) {
            if (d.property != property) { return; }
            const std::string_view text = trim(d.value, html_whitespace);
            if (css::may_have_var(text)) { return; }
            // `initial` is the initial value; the other wide keywords keep
            // the parent's answer on an inherited property.
            if (ascii_iequals(text, "initial")) {
                held = initial;
                return;
            }
            if (css::is_wide_keyword(text)) { return; }
            held = ascii_lower_copy(text);
        });
        return held;
    };
    const std::string writing_mode = inherited_keyword(writing_mode_, "horizontal-tb");
    const std::string direction = inherited_keyword(atoms_->intern_lower("direction"), "ltr");
    const bool vertical =
        writing_mode.starts_with("vertical-") || writing_mode.starts_with("sideways-");
    css::length_context lengths = font_context(own_font_size, own_line_height, own_zero_advance);
    lengths.vertical = vertical;
    // The logical-to-physical mapping (CSS Logical 1 §4). A flow-relative
    // property is stored as the physical one it maps to on this element, at
    // the declaration's own position in the fold: `margin-left: 1px;
    // margin-inline-start: 2px` is 2px in ltr and the reverse order is 1px
    // (whichever spelling of the logical property group came later wins), and
    // layout and getComputedStyle only ever read the physical side. A
    // property that is not logical maps to itself.
    const auto physical = [&](atom property) -> atom {
        const std::string_view name = atoms_->text(property);
        if (name.find("block") == std::string_view::npos &&
            name.find("inline") == std::string_view::npos &&
            name.find("start") == std::string_view::npos &&
            name.find("end") == std::string_view::npos) {
            return property; // Nothing flow-relative in the name.
        }
        const std::string mapped = css::physical_property_of(name, writing_mode, direction);
        return mapped.empty() ? property : atoms_->intern_lower(mapped);
    };
    // The length bases each property folds with. `lengths` is the default.
    // `line-height` itself uses line_height_lengths, where `lh` is still the
    // parent's (`line-height: 2lh` folded against its own answer would double
    // it). The other font-* properties use the parent's font size for `em`, as
    // `font-size` does: `font-weight: calc(1em / 1px)` under a 10px parent is
    // 10 whatever the element's own size (WPT
    // using-font-relative-units-in-font-properties, CSS Values 4 §6.1.1).
    const css::length_context font_lengths =
        font_context(parent_font_size, parent_line_height, parent_zero_advance);
    const auto lengths_for = [&](atom property) -> const css::length_context & {
        if (property == line_height_) { return line_height_lengths; }
        return atoms_->text(property).starts_with("font-") ? font_lengths : lengths;
    };

    // Registered custom properties (CSS Properties and Values API 1 §2.4),
    // computed after font size and line height because a `<length>` needs them.
    // An unregistered custom property is a token stream stored verbatim and
    // read lazily; a registered one has a computed value like any other
    // property: its declaration is substituted, parsed against the syntax and
    // computed like the type it names, and what does not parse (or `initial`,
    // or nothing at all on a property that does not inherit) gets the initial
    // value.
    //
    // A property that inherits and is not declared here is left to the
    // parent's inherited half, which already holds its computed value:
    // pushing a copy would give every element an inherited block of its
    // own and undo the sharing the split below depends on.
    for (const auto & [id, registration] : registrations_) {
        const atom name{id};
        declaration * own = nullptr;
        for (declaration & d : out) {
            if (d.property == name) { own = &d; }
        }
        // A `random()` in a registered property is keyed on that property
        // (CSS Values 5 §random-caching): `--x` and `--y` on one element
        // draw differently, and `--len-scoped: random(property-scoped, ...)`
        // draws the same on every element (WPT random-computed).
        css::length_context registered_lengths = lengths;
        registered_lengths.property = atoms_->text(name);
        std::optional<std::string> computed;
        const bool cyclic = std::ranges::find(cyclic_registered, name) != cyclic_registered.end();
        if (cyclic) {
            // Part of a cycle through font-size: the initial value.
        } else if (own != nullptr && own->value != guaranteed_invalid) {
            std::string text = own->value;
            bool substituted = true;
            if (css::may_have_var(text)) {
                conditions.lengths = lengths;
                conditions.property = std::string{atoms_->text(name)};
                std::optional<std::string> done =
                    css::substitute_var(text, lookup, *atoms_, attributes, &conditions);
                substituted = done.has_value();
                if (done) { text = std::move(*done); }
            }
            // A substituted CSS-wide keyword is that keyword (CSS Values 5
            // §arbitrary-substitution): `attr(data-x type(*))` holding
            // `inherit` is the parent's value, and `unset` is whichever the
            // registration says (WPT attr-css-wide-keywords).
            const std::string_view word = trim(text, html_whitespace);
            const bool from_parent = ascii_iequals(word, "inherit") ||
                                     (registration.inherits && (ascii_iequals(word, "unset") ||
                                                                ascii_iequals(word, "revert")));
            if (substituted && from_parent) {
                // `get` reads the parent's own half too, where a property
                // that does not inherit lives.
                const std::string_view held = parent ? parent->get(name) : "";
                if (!held.empty()) { computed = std::string{held}; }
            } else if (substituted && !ascii_iequals(word, "initial") &&
                       !ascii_iequals(word, "unset") && !ascii_iequals(word, "revert")) {
                if (self.is_root && cascade_detail::has_viewport_unit(text)) {
                    viewport_registered.push_back(name);
                }
                computed = css::compute_registered(text, registration.syntax, registered_lengths);
            }
        } else if (own == nullptr && registration.inherits && parent) {
            continue;
        }
        if (!computed) {
            computed = css::compute_registered(registration.initial, registration.syntax, lengths)
                           .value_or(registration.initial);
        }
        if (own != nullptr) {
            own->value = std::move(*computed);
        } else {
            out.push_back(declaration{name, std::move(*computed)});
        }
    }

    // Pass two: everything else. Substitute, then expand, then put.
    fold([&](const declaration & d) {
        const std::string_view property = atoms_->text(d.property);
        if (property.starts_with("--")) { return; }
        std::string value{d.value};
        // Removes the property (or every longhand of a shorthand) from what has
        // been folded so far, for a value invalid at computed-value time: CSS
        // treats that as `unset`, so an earlier declaration must not show
        // through.
        const auto unset = [&] {
            const auto erase = [&](atom property_to_erase) {
                if (folding < matches_.size()) {
                    applied.emplace_back(folding, declaration{property_to_erase, "unset"});
                }
                for (std::size_t i = 0; i < out.size(); ++i) {
                    if (out[i].property == property_to_erase) {
                        out.erase(out.begin() + static_cast<std::ptrdiff_t>(i));
                        break;
                    }
                }
            };
            // An invalid winning shorthand resets every longhand it governs.
            const auto longhands = css::longhands_of(property);
            if (longhands.empty()) {
                erase(physical(d.property));
            } else {
                for (const auto name : longhands) { erase(physical(atoms_->intern(name))); }
            }
        };
        const bool had_var = css::may_have_var(value);
        // The font-size that was found to be a cycle above is invalid at
        // computed-value time here too.
        if (d.property == font_size_ && !cyclic_registered.empty() && had_var) {
            unset();
            return;
        }
        // The root's overflow may not depend on the viewport's size: it decides
        // the viewport's scrollbars, which decide what a viewport unit
        // measures, so a substitution there that reads one (in its own text,
        // or in a custom property it names) is a cycle, and invalid at
        // computed-value time (csswg-drafts#13189, WPT
        // viewport-units-scroll-no-cycles).
        if (had_var && self.is_root && !pseudo_wanted_ &&
            ascii_iequals_any(property, {"overflow", "overflow-x", "overflow-y"})) {
            bool cycle = cascade_detail::has_viewport_unit(value);
            const css::token_stream ts = css::tokenize(value);
            for (const css::css_token & t : ts.tokens) {
                if (cycle || t.type == css::token_type::eof) { break; }
                const std::string_view name = ts.text_of(t);
                if (t.type != css::token_type::ident || !name.starts_with("--")) { continue; }
                const atom key = atoms_->intern(name);
                const std::optional<std::string_view> held = lookup(key);
                cycle = std::ranges::find(viewport_registered, key) != viewport_registered.end() ||
                        (held && cascade_detail::has_viewport_unit(*held));
            }
            if (cycle) {
                unset();
                return;
            }
        }
        if (had_var) {
            conditions.lengths = lengths_for(d.property);
            conditions.property = std::string{property};
            const std::optional<std::string> done =
                css::substitute_var(value, lookup, *atoms_, attributes, &conditions);
            // A failed or empty substitution is invalid at computed-value time,
            // which means `unset`, not "let an earlier declaration win":
            // `color: red; color: var(--missing)` is the inherited colour in
            // Chrome. An empty token stream (Bootstrap's
            // `text-align: var(--bs-body-text-align)`) is a valid substitution
            // but not a valid value.
            if (!done || trim(*done, html_whitespace).empty()) {
                unset();
                return;
            }
            value = *done;
            // So is a result the property's grammar refuses. A value is
            // validated when it is parsed, and a substituted one was not
            // parsed until now: `width: attr(data-n type(<number>))` is the
            // number `10`, which `width` cannot take, and CSS Variables 1 §3
            // makes that invalid at computed-value time: `unset`, not ten
            // pixels (WPT attr-all-types). The property table answers, and it
            // refuses nothing for a property it does not model.
            //
            // A value it accepts is serialized as `el.style` would have:
            // `z-index: var(--n)` with a 25-digit `--n` is the same integer,
            // through the same double, as `el.style.zIndex = n` (WPT
            // serialize-custom-props).
            if (!css::may_have_math(value)) {
                css::value_check checked = css::check_declaration(property, value);
                if (!checked.valid) {
                    unset();
                    return;
                }
                value = std::move(checked.serialized);
            }
        }
        // The declaration block splits the unfolded value (`unfolded` below):
        // its grammars take a math function as a component, and each longhand
        // is folded below against the longhand's own bases and range, which a
        // shorthand has neither of (`border-block-width: 3px calc(10px - 0.5em)`
        // is 3px and a width clamped to 0px, not a refused `-10px`).
        // A container unit is resolved here, the one place that knows the
        // element's query container (CSS Conditional 5 §4.3): `left: 10cqw`
        // is 7px against a 70px container, and the small viewport's share
        // on an axis no container sizes (WPT container-units-small-viewport-
        // fallback). The computed value is the absolute length.
        value = cascade_detail::resolve_container_units(std::move(value), lengths_for(d.property));
        const std::string unfolded = value;
        // Math is folded after substitution and before expansion, for the same
        // reason as the shorthands: `-1 * var(x)` has no arithmetic to do
        // before substitution, and `border: calc(var(w) * 2) solid red` cannot
        // be split into longhands until its components are single tokens.
        if (css::may_have_math(value)) {
            // The property is passed down because the evaluator answers with
            // numbers and only the cascade knows whether one is a value here:
            // `opacity: calc(2 / 4)` is `0.5`; `width: calc(2 * 3)` is a
            // syntax error.
            css::length_context math_bases = lengths_for(d.property);
            math_bases.property = property;
            css::folded_value done =
                css::fold_math(value, math_bases, css::math_context_of(property));
            if (!done.ok) {
                // A calc that does not evaluate is not a value, and the
                // declaration is invalid. Which kind of invalid depends on where
                // the value came from, which is why `had_var` is remembered: a
                // value that went through substitution is invalid at
                // computed-value time, which CSS Variables 1 §3 spells `unset`,
                // so it must also remove the earlier declaration it beat. One
                // that never contained a var() is invalid at parse time, so the
                // earlier declaration simply wins and this one is dropped.
                if (had_var) { unset(); }
                return;
            }
            value = std::move(done.text);
            // The result is clamped to the property's range (CSS Values 4
            // §10.10): `tab-size: calc(2 * -4)` computes to 0 where a literal
            // `-8` never gets past the grammar (WPT calc-numbers). The property
            // table knows which properties have a floor at zero.
            if (const css::property_syntax * known = css::find_property(property);
                known != nullptr && known->nonnegative) {
                value = css::non_negative(value);
            }
            // A substituted value is validated once its math has an answer,
            // as the path without math was above: `font-style:
            // attr(data-foo type(<angle>), calc(10 + 20))` falls back to
            // the number 30, which font-style cannot take, and is invalid
            // at computed-value time (WPT attr-all-types). A value that is
            // still a function (`min(10px, 5%)`) is valid as it stands.
            // Image and layer sizes clamp each component separately, so their
            // substituted syntax is validated on the unfolded text even if some
            // math remains unresolved, keeping negative literals distinct from
            // negative calculation results.
            const bool image_components = ascii_iequals_any(
                property,
                {"background", "border-image", "border-image-width", "border-image-outset",
                 "border-image-slice", "background-size", "mask-size", "background-image",
                 "mask-image", "border-image-source", "list-style-image"});
            if (had_var && (!css::may_have_math(value) || image_components)) {
                const css::value_check checked =
                    css::check_declaration(property, image_components ? unfolded : value);
                if (!checked.valid) {
                    unset();
                    return;
                }
                if (!image_components) { value = std::move(checked.serialized); }
            }
        }
        // Font size is already resolved by the pre-pass above, because every
        // `em` in every other declaration needed the answer first. Emit that
        // number rather than re-deriving it here, so there is exactly one place
        // a font size is computed and no way for the two to disagree.
        if (d.property == font_size_ && font_size_resolved) {
            value = css::serialize_calc(css::calc_result{own_font_size, 0.0f, false});
        }
        // Every relative length folds to pixels here, after expansion so a
        // shorthand's components each get their own answer. `padding: 1rem 2em`
        // becomes four px longhands, which is what a computed value is.
        //
        // It happens here rather than in layout because this is the only place
        // that knows all three bases at once: the element's own font size, the
        // root's, and the viewport.
        //
        // A bare number is left alone: `line-height: 1.5` is not 1.5px.
        //
        // Every dimension family folds, not only lengths. `transition-delay:
        // 12ms` computes to `0.012s` and `rotate: 100grad` to `90deg` (CSS Values 4
        // 6.4-6.5); `canonical_dimension_text` is a superset of the length case
        // and still answers `96px` for `1in`.
        const auto folded = [&](std::string text, atom longhand) {
            return cascade_detail::fold_value(std::move(text), atoms_->text(longhand),
                                              lengths_for(longhand));
        };
        const auto expanded = expand_shorthand(property, value);
        if (expanded.empty()) {
            if (const css::declaration_block block =
                    cascade_detail::split_via_block(property, unfolded);
                !block.empty()) {
                for (const css::declaration & one : block) {
                    const atom longhand = atoms_->intern_lower(one.name);
                    std::string text = one.value;
                    if (longhand == font_size_ && font_size_resolved) {
                        // The size a `font` carries was resolved by the pre-pass.
                        text = css::serialize_calc(css::calc_result{own_font_size, 0.0f, false});
                    } else if (css::may_have_math(text)) {
                        css::length_context bases = lengths_for(longhand);
                        bases.property = one.name;
                        const css::folded_value done =
                            css::fold_math(text, bases, css::math_context_of(one.name));
                        if (done.ok) { text = std::move(done.text); }
                        if (const css::property_syntax * known = css::find_property(one.name);
                            known != nullptr && known->nonnegative) {
                            text = css::non_negative(text);
                        }
                    }
                    put(declaration{physical(longhand), folded(std::move(text), longhand)});
                }
                // ponytail: layout reads `white-space` itself (layout/box.hpp),
                // so the shorthand's canonical value stays beside its longhands
                // until it reads white-space-collapse and text-wrap-mode.
                if (property == "white-space") {
                    put(declaration{d.property, css::declaration_value(block, property)});
                }
                return;
            }
            // A substituted token stream is validated only now. If it does not
            // match the overflow grammar, the winning shorthand is invalid at
            // computed-value time and resets both axes; a parse-time
            // invalid declaration still leaves earlier declarations alone.
            if (had_var && property == "overflow") {
                unset();
                return;
            }
            put(declaration{physical(d.property), folded(std::move(value), d.property)});
            return;
        }
        for (const auto & [name, text] : expanded) {
            const atom longhand = atoms_->intern_lower(name);
            put(declaration{physical(longhand), folded(std::string{text}, longhand)});
        }
    });

    // Logical position keywords compute on their own element, before a child
    // inherits them. This runs after pass two so it sees the final writing mode
    // and direction, either of which may come from a var().
    const std::string position_mode = conditions.computed("writing-mode").value_or("horizontal-tb");
    const std::string position_direction = conditions.computed("direction").value_or("ltr");
    for (declaration & d : out) {
        const std::string_view name = atoms_->text(d.property);
        if (!ascii_iequals_any(name, {"background-position", "background-position-x",
                                      "background-position-y", "mask-position"})) {
            continue;
        }
        const css::length_context bases = lengths_for(d.property);
        const css::color_context ctx{{}, &bases};
        if (std::string computed = css::computed_background_list(name, d.value, ctx, position_mode,
                                                                 position_direction);
            !computed.empty()) {
            d.value = std::move(computed);
        }
    }

    // Split the result. Anything inherited goes into a fresh inherited half
    // built on top of the parent's; everything else stays the element's own.
    //
    // The essential shortcut: an element that declared nothing inherited keeps
    // its parent's pointer verbatim, with no copy, hash or intern. That makes a
    // large inherited object free for the thousands of elements that share one;
    // without it, splitting would cost more than it saves.
    declaration_list inherited_here;
    bool any_inherited = false;
    for (const declaration & d : out) {
        if (inherits(atoms_->text(d.property))) { any_inherited = true; }
    }
    inherited_ptr result_inherited = from;
    if (any_inherited) {
        if (from) { inherited_here = from->declarations; }
        for (const declaration & d : out) {
            if (!inherits(atoms_->text(d.property))) { continue; }
            bool replaced = false;
            for (declaration & existing : inherited_here) {
                if (existing.property == d.property) {
                    existing.value = d.value;
                    replaced = true;
                    break;
                }
            }
            if (!replaced) { inherited_here.push_back(d); }
        }
        result_inherited = table_.intern_inherited(std::move(inherited_here));
    }
    // The own half keeps the inherited properties too. They are redundant with the
    // inherited half (`get` would find them there), but removing them would make
    // an element's own `color: red` invisible to anything that enumerates its own
    // declarations, which is what getComputedStyle's key list is.
    return table_.intern(std::move(out), std::move(result_inherited));
}

} // namespace ctbrowser::style
