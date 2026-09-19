#include <ctbrowser/style/engine.hpp>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/style/css/boolean.hpp>

#include <span>
#include <string>
#include <vector>

// engine: the method bodies.
// The header says what these do; this says how.

namespace ctbrowser::style {

computed_style_ptr engine::resolve(const read_txn & txn, node_id node, const element_facts & self,
                                   const ancestor_filter & ancestors, std::size_t depth,
                                   const computed_style_ptr & parent) {
    // THE PARENT'S WHOLE STYLE, not just its inherited half, and the difference is
    // `inherit` itself: the keyword takes the parent's value for ANY property,
    // inherited or not, so `display: inherit` has to be able to read a property
    // that never travels on its own. The inherited half alone cannot answer that.
    const inherited_ptr & from = parent ? parent->inherited : no_inherited_;
    // Where this element sits among its siblings, for `sibling-index()` and
    // `sibling-count()`: facts the traversal already gathered for
    // `:nth-child`, handed to every math function this element folds.
    sibling_index_ = self.sibling_index;
    sibling_count_ = self.sibling_count;
    element_key_ = key_of(node);
    // Gather only the rules whose RIGHTMOST compound could possibly match.
    matches_.clear();
    collect(index_.by_id, self.id, txn, ancestors, depth);
    for (const atom c : self.classes) { collect(index_.by_class, c, txn, ancestors, depth); }
    collect(index_.by_tag, self.tag, txn, ancestors, depth);
    for (const rule & r : index_.universal) { consider(r, txn, ancestors, depth); }

    // THE CASCADE, CSS Cascade 5 §6.1: importance, then origin - reversed for
    // important declarations, where the user agent's beat the author's - then
    // the layer, whose order is likewise reversed for important declarations
    // (§6.4), then specificity, then scope proximity (Cascade 6 §6.3, nearer
    // wins), then source order. Sorting ascending and applying in order means
    // the last write to a property wins, which is exactly the rule.
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
    // Applying a declaration means REPLACING the property if it is already
    // there - the later write wins, which is what "the cascade" reduces to
    // once the sort has put everything in priority order.
    //
    // THE EXPLICIT-DEFAULTING KEYWORDS are resolved here, because here is where
    // the parent's value is in hand:
    //
    //   inherit   take the parent's value, whether or not the property inherits
    //   initial   an EMPTY value, which shadows anything inherited and reads as
    //             "nothing said" to every consumer - the closest thing to a real
    //             initial value until the property table carries them
    //   unset     drop the declaration: for an inherited property the inherited
    //             value then shows through, and for a non-inherited one absence
    //             already means initial, so dropping is correct for both
    //   revert    the value the PREVIOUS ORIGIN would have produced
    //   revert-layer, revert-rule
    //             likewise, one layer or one rule back - see rolled_back
    //
    // Rollbacks search the declarations produced by the fold, after variable
    // substitution, shorthand expansion and logical-to-physical mapping. The
    // raw rule may say `margin` or `margin-block`, neither of which would match
    // a rollback of `margin-top`. Keep the rule index for origin/layer tests.
    std::vector<std::pair<std::size_t, declaration>> applied;
    // `folding` is the current rule's position, or the size for inline style.
    std::size_t folding = matches_.size();
    const auto rolled_back = [this, &applied](atom property, std::size_t limit, const auto & keep) {
        for (std::size_t i = applied.size(); i-- > 0;) {
            const auto & [at, d] = applied[i];
            if (at < limit && d.property == property && keep(matches_[at])) { return i; }
        }
        return applied.size();
    };
    const auto put = [&out, &parent, &folding, &rolled_back, &applied,
                      this](const declaration & d) {
        std::string value = d.value;
        const std::string_view property = atoms_->text(d.property);
        // THE ROLLBACK KEYWORDS, each the cascade with some declarations
        // struck out (CSS Cascade 5 §7.3): `revert` strikes the author origin,
        // so the answer is the last user-agent declaration (important UA
        // rules that sort after the author's still apply later); `revert-layer`
        // strikes the current layer of the current origin, the style
        // attribute counting as a layer above all of them; `revert-rule` (a
        // Cascade 6 draft) strikes the current rule - the declarations filed
        // consecutively under one selector. What is found may be a keyword
        // itself, so this loops, always to an earlier position.
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
        if (value == "inherit") {
            value = std::string{parent ? parent->get(d.property) : std::string_view{}};
        } else if (value == "unset" || value == "revert") {
            for (std::size_t i = 0; i < out.size(); ++i) {
                if (out[i].property == d.property) {
                    out.erase(out.begin() + static_cast<std::ptrdiff_t>(i));
                    break;
                }
            }
            if (!inherits(property)) { return; }
            // An inherited property must be actively removed from the own half so
            // the inherited value shows through; it already is.
            return;
        } else if (value == "initial") {
            // ON A CUSTOM PROPERTY `initial` IS THE GUARANTEED-INVALID VALUE,
            // not an empty one: `--bs-table-bg-type: initial` is a SENTINEL that
            // `var(--bs-table-bg-type, <fallback>)` has to fall through, where an
            // empty custom property - `--bs-btn-font-family: ;` - is a valid
            // empty substitution.
            //
            // The declaration is KEPT, holding the sentinel, rather than
            // erased: erasing it would let an ancestor's value show through,
            // and `initial` means invalid HERE regardless of what was
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

    // The style ATTRIBUTE. Not a separate origin: it is author-level with a
    // specificity above every selector, so it lands between the normal
    // declarations and the important ones. Chrome and Firefox both give
    //
    //   normal selector  <  normal inline  <  important selector  <
    //   important inline
    //
    // which is why this is spliced into the fold at the importance
    // boundary rather than simply appended at the end - `!important` in a
    // stylesheet has to be able to beat a style attribute.
    // ...AND NOT FOR A PSEUDO-ELEMENT, which has no style attribute.
    static const inline_block no_inline;
    const inline_block & own = pseudo_wanted_ ? no_inline : inline_style_of(txn, node);

    // TWO PASSES, and the reason is that custom properties are themselves
    // cascaded: substitution cannot run inside the fold that produces the values
    // it needs to read. So pass one applies ONLY custom properties, and pass two
    // substitutes everything else against them.
    //
    // Both passes walk the same sorted list with the same inline-style splice, so
    // priority is identical between them - and expansion happening in pass two
    // keeps source order for free: a shorthand's longhands land at the shorthand's
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

    // PASS ONE: the custom properties, stored verbatim. A custom property's value
    // is never parsed and never validated - it is a token stream that means
    // whatever the var() reading it makes of it.
    //
    // ...EXCEPT FOR AN `inherit()` IN ONE, which is replaced NOW (CSS Values
    // 5 §inherit-notation): it names the PARENT's computed value, and the
    // parent's cascade has just produced it - a lazy reading later would
    // have to re-run the parent's substitution in the parent's scope, and
    // its grandparent's, which is how `--v: e2 inherit(--v)` under `--v:
    // e1` failed to accumulate (inherit-function-basic). A var() in the
    // same value stays lazy, as every var() in a custom property is.
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

    // `nullopt` means NOT DEFINED, which is what makes `var()` take its
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
    // ...AND THE ELEMENT'S ATTRIBUTES, for `attr()`. Absent and empty are
    // different answers here too: `attr(data-x)` on an element without the
    // attribute takes its fallback, and one with `data-x=""` is `""`.
    const css::attribute_lookup attributes =
        [&txn, node, this](std::string_view name) -> std::optional<std::string> {
        const atom key = atoms_->intern(name);
        if (!txn.has_attribute(node, key)) { return std::nullopt; }
        return std::string{txn.attribute_value(node, key)};
    };
    // ...AND WHAT `if()` MAY ASK (CSS Values 5 §if-notation): the parent's
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

    // EVERY SHORTHAND expand_shorthand DOES NOT SPLIT goes through the CSSOM's
    // declaration block, which knows each one's grammar
    // (properties/shorthands.cpp): `font`, `white-space`, `animation`, the
    // flow-relative margins, insets and borders, `place-*`, `columns` ... What
    // comes back is validated and canonical; a value the grammar refuses, or a
    // shape the block keeps whole, is an empty block and the declaration
    // stays as it was.
    const auto split_via_block = [](std::string_view name, std::string_view text) {
        css::declaration_block block;
        if (css::longhands_of(name).empty()) { return block; } // a longhand
        (void)css::set_declaration(block, name, text, false);
        if (block.size() == 1 && ascii_iequals(block[0].name, name)) { block.clear(); }
        return block;
    };
    // A `font` shorthand carries a font-size and a line-height of its own,
    // and the two pre-passes below need those before pass two splits it.
    const atom font_ = atoms_->intern_lower("font");
    const auto component_of = [&](const declaration & d,
                                  atom wanted) -> std::optional<std::string> {
        if (d.property == wanted) { return d.value; }
        if (d.property != font_ || css::may_have_var(d.value)) { return std::nullopt; }
        for (const css::declaration & one : split_via_block("font", d.value)) {
            if (atoms_->intern_lower(one.name) == wanted) { return one.value; }
        }
        return std::nullopt;
    };

    // PASS ONE AND A HALF: FONT SIZE, ALONE, BEFORE ANYTHING ELSE READS IT.
    //
    // `em` means the element's own font size on every property except font-size
    // itself, where it means the parent's - so the one value everything else is
    // relative to has to be known before the general fold runs. That is not a
    // convenience: `padding: calc(.5em + 1rem)` and `font-size: 1.25em` in the
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
    // ...AND THE PARENT'S LINE HEIGHT, for `lh` (CSS Values 4 §6.1.1). The
    // same asymmetry as `em`: `lh` in `font-size` and in `line-height`
    // itself is the parent's, everywhere else the element's own. The
    // parent's line-height is inherited text - a number, a length the
    // parent's cascade already folded to px, a percentage or `normal` - and
    // resolves against the parent's font size.
    //
    // THE ROOT RESOLVES AGAINST THE INITIAL VALUES: `font-size: 1lh` on
    // `:root` is 1.25 times 16px, however the root's own font size and
    // line-height come out (lh-rlh-on-root-001).
    const float parent_line_height =
        parent && parent->inherited
            ? line_height_px(parent->inherited->get(line_height_), parent_font_size)
            : 16.0f * 1.25f;
    // THE ROOT STARTS FROM THE INITIAL VALUES - a `font-size: 3rem` on `:root`
    // is 48px however the previous resolve of this document left the root,
    // and the two members below persist across resolves (rem-unit-root-element).
    if (!parent) {
        root_font_size_ = 16.0f;
        root_line_height_ = parent_line_height;
    }
    // THE PARENT'S `0` ADVANCE, for `ch` in `font-size` and the other font-*
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
    std::vector<atom> read; // what the font-size's substitution looked at
    // Whether the WINNING font-size declaration actually resolved to a length.
    // `font-size: larger` and the other relative keywords are not modelled, and
    // rewriting one to a pixel value would be inventing an answer - so the text
    // survives and whoever reads it decides.
    bool font_size_resolved = false;
    {
        // The em basis for resolving font-size is the PARENT's; the rem basis is
        // the root's, which for the root element is its own answer and so is
        // seeded from the parent's - a root `font-size: 2rem` is circular and CSS
        // resolves it against the initial 16px.
        // A PERCENTAGE IN A FONT SIZE IS OF THE PARENT'S, and the evaluator
        // can be told so: `calc(50% + 1px)` folds here rather than waiting
        // for a containing block it will never be measured against.
        css::length_context ctx =
            font_context(parent_font_size, parent_line_height, parent_zero_advance);
        ctx.percent_basis = parent_font_size;
        conditions.lengths = ctx;
        conditions.property = "font-size";
        // A REGISTERED PROPERTY IN FONT-RELATIVE UNITS DEPENDS ON THIS
        // FONT SIZE, so a font-size reading it is a cycle (CSS Properties
        // and Values API 1 §2.4): the font-size is invalid at computed-value
        // time - inherited - and the property takes its initial value
        // (typed_arithmetic_cycle).
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
            // A calc that does not evaluate keeps its text here, and falls
            // through to the keyword branch below - which is right for a font
            // size, because a keyword is a real answer for one.
            if (css::may_have_math(value)) {
                value = css::fold_math(value, ctx, css::math_context::length).text;
            }
            const std::optional<float> px = css::length_text_to_px(value, ctx);
            // A percentage font size is the parent's, scaled - the one relative
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
            } else {
                font_size_resolved = false; // a keyword: leave the text alone
            }
        });
        // Only the font-size cares what was read.
        conditions.on_read = nullptr;
    }
    // The root's size is what every `rem` in the document resolves against, so it
    // is recorded as the tree is descended rather than looked up per element.
    if (!parent) { root_font_size_ = own_font_size; }
    // ...AND THE ELEMENT'S OWN `0` ADVANCE, now that its font size is known:
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
    // PASS ONE AND THREE QUARTERS: LINE HEIGHT, for `lh` in everything else.
    // The winning `line-height` declaration, substituted and folded against
    // the PARENT's `lh` and the element's own `em`, then resolved to px. An
    // absent declaration is the inherited text against the element's OWN
    // font size - a `1.5` is a factor and inherits as one.
    float own_line_height = line_height_px(
        parent && parent->inherited ? parent->inherited->get(line_height_) : "", own_font_size);
    // ...and a percentage in a line-height is of the element's own font size,
    // so `calc(10% / 1px)` is the number 1 here (typed_arithmetic).
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
            // A bare number is a factor, not pixels - which is why
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
    // THE ELEMENT'S OWN WRITING MODE, settled before any length resolves:
    // `vi`/`vb` swap axes in a vertical one (CSS Values 4 §6.1.2), and the
    // property inherits, so the parent's answer stands until a declaration
    // says otherwise - a keyword, never a length, so nothing to fold.
    // ...AND ITS DIRECTION, settled the same way: the two together decide
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
    // THE LOGICAL -> PHYSICAL MAPPING, CSS Logical 1 §4. A flow-relative
    // property is stored as the physical one it maps to on THIS element, at
    // the declaration's own position in the fold: `margin-left: 1px;
    // margin-inline-start: 2px` is 2px in ltr and the reverse order is 1px -
    // whichever spelling of the logical property group came later wins - and
    // layout and getComputedStyle only ever read the physical side. A
    // property that is not logical maps to itself.
    const auto physical = [&](atom property) -> atom {
        const std::string_view name = atoms_->text(property);
        if (name.find("block") == std::string_view::npos &&
            name.find("inline") == std::string_view::npos &&
            name.find("start") == std::string_view::npos &&
            name.find("end") == std::string_view::npos) {
            return property; // nothing flow-relative in the name
        }
        const std::string mapped = css::physical_property_of(name, writing_mode, direction);
        return mapped.empty() ? property : atoms_->intern_lower(mapped);
    };
    // ...EXCEPT IN `line-height` ITSELF, where `lh` is still the parent's -
    // `line-height: 2lh` folded against its own answer would double it -
    // and `line_height_lengths` above is what that property folds with; and
    // EXCEPT IN THE OTHER font-* PROPERTIES, where an `em` is the PARENT's
    // font size as it is in `font-size`: `font-weight: calc(1em / 1px)`
    // under a 10px parent is 10 whatever the element's own size
    // (using-font-relative-units-in-font-properties, CSS Values 4 §6.1.1).
    const css::length_context font_lengths =
        font_context(parent_font_size, parent_line_height, parent_zero_advance);
    const auto lengths_for = [&](atom property) -> const css::length_context & {
        if (property == line_height_) { return line_height_lengths; }
        return atoms_->text(property).starts_with("font-") ? font_lengths : lengths;
    };

    // PASS ONE AND SEVEN EIGHTHS: THE REGISTERED CUSTOM PROPERTIES, CSS
    // Properties and Values API 1 §2.4. An unregistered custom property is
    // a token stream stored verbatim and read lazily; a registered one has
    // a computed value like any other property: its declaration is
    // substituted, parsed against the syntax and computed like the type
    // it names, and what does not parse - or `initial`, or nothing at all
    // on a property that does not inherit - is the initial value. It sits
    // here because a `<length>` computes against the element's own font
    // size, which the passes above have just settled.
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
        // A `random()` in a registered property is keyed on THAT property
        // (CSS Values 5 §random-caching): `--x` and `--y` on one element
        // draw differently, and `--len-scoped: random(property-scoped, ...)`
        // draws the same on every element (random-computed).
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
            // A SUBSTITUTED CSS-WIDE KEYWORD IS THAT KEYWORD (CSS Values 5
            // §arbitrary-substitution): `attr(data-x type(*))` holding
            // `inherit` is the parent's value, `unset` whichever the
            // registration says (attr-css-wide-keywords).
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

    // PASS TWO: everything else. Substitute, then expand, then put.
    fold([&](const declaration & d) {
        const std::string_view property = atoms_->text(d.property);
        if (property.starts_with("--")) { return; }
        std::string value{d.value};
        // `unset`: actively REMOVE the property from what has been folded so
        // far, rather than merely declining to add it. The two are different
        // whenever an earlier declaration set the same property, and which one
        // is right depends on when the value became invalid - see both callers.
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
            // Invalid-at-computed-value time applies to every longhand a
            // shorthand governs. Removing a raw `overflow` declaration
            // would leave an earlier overflow-x/y active, even though the
            // later shorthand won the cascade and became `unset`.
            if (property == "overflow") {
                erase(atoms_->intern("overflow-x"));
                erase(atoms_->intern("overflow-y"));
            } else {
                erase(physical(d.property));
            }
        };
        const bool had_var = css::may_have_var(value);
        // The font-size that was found to be a cycle above is invalid at
        // computed-value time here too.
        if (d.property == font_size_ && !cyclic_registered.empty() && had_var) {
            unset();
            return;
        }
        if (had_var) {
            conditions.lengths = lengths_for(d.property);
            conditions.property = std::string{property};
            const std::optional<std::string> done =
                css::substitute_var(value, lookup, *atoms_, attributes, &conditions);
            // INVALID AT COMPUTED-VALUE TIME means `unset`, which for an inherited
            // property lets the inherited value through and otherwise means absent.
            // NOT "drop it and let an earlier declaration win" - that is the classic
            // wrong reading, and it is observable: `color: red; color: var(--x)`
            // renders as the INHERITED colour in Chrome, not red. So the property is
            // actively removed from what has been folded so far.
            //
            // AN EMPTY RESULT is invalid too, and it is the case Bootstrap hits:
            // it ships seventeen empty-but-valid custom properties, and
            // `body { text-align: var(--bs-body-text-align) }` reads one. An empty
            // token stream is a valid substitution but not a valid VALUE.
            if (!done || trim(*done, html_whitespace).empty()) {
                unset();
                return;
            }
            value = *done;
            // ...AND SO IS A RESULT THE PROPERTY'S GRAMMAR REFUSES. A value
            // is validated when it is parsed, and a substituted one was not
            // parsed until now: `width: attr(data-n type(<number>))` is the
            // number `10`, which `width` cannot take, and CSS Variables 1 §3
            // makes that invalid at computed-value time - `unset`, not
            // ten pixels (attr-all-types). Asked of the property table,
            // which refuses nothing for a property it does not model.
            //
            // ...AND WHAT IT ACCEPTS IT SPELLS, as `el.style` would have:
            // `z-index: var(--n)` with a 25-digit `--n` is the same
            // integer, through the same double, as `el.style.zIndex = n`
            // (serialize-custom-props).
            if (!css::may_have_math(value)) {
                css::value_check checked = css::check_declaration(property, value);
                if (!checked.valid) {
                    unset();
                    return;
                }
                value = std::move(checked.serialized);
            }
        }
        // ...BUT THE DECLARATION BLOCK SPLITS THE UNFOLDED VALUE: its grammars
        // take a math function as a component, and each longhand's is folded
        // below against the LONGHAND's own bases and range - a shorthand has
        // neither (`border-block-width: 3px calc(10px - 0.5em)` is 3px and a
        // width clamped to 0px, not a refused `-10px`).
        const std::string unfolded = value;
        // CALC, AFTER SUBSTITUTION AND BEFORE EXPANSION - the same ordering
        // argument as the shorthands: `-1 * var(x)` has no arithmetic to do
        // before substitution, and `border: calc(var(w) * 2) solid red` cannot
        // be split into longhands until its components are single tokens.
        if (css::may_have_math(value)) {
            // WHAT THE PROPERTY WILL TAKE, passed down, because the evaluator
            // answers with numbers and only the cascade knows whether one is a
            // value here: `opacity: calc(2 / 4)` is `0.5`; `width: calc(2 * 3)`
            // is a syntax error.
            css::length_context math_bases = lengths_for(d.property);
            math_bases.property = property;
            css::folded_value done =
                css::fold_math(value, math_bases, css::math_context_of(property));
            if (!done.ok) {
                // A CALC THAT DOES NOT EVALUATE IS NOT A VALUE, and the
                // declaration is invalid. WHICH KIND of invalid depends on where
                // the value came from, which is why `had_var` is remembered: a
                // value that went through substitution is invalid at
                // COMPUTED-VALUE time, and §3 spells that `unset`, so it must
                // also remove the earlier declaration it beat. One that never
                // contained a var() is invalid at PARSE time, so the earlier
                // declaration simply wins and this one is dropped.
                if (had_var) { unset(); }
                return;
            }
            value = std::move(done.text);
            // ...AND CLAMPED TO THE PROPERTY'S RANGE, CSS Values 4 §10.10:
            // `tab-size: calc(2 * -4)` computes to 0 where a literal `-8`
            // never got past the grammar. `calc-numbers` asks for exactly
            // that, and the table already knows which properties have a
            // floor at zero.
            if (const css::property_syntax * known = css::find_property(property);
                known != nullptr && known->nonnegative) {
                value = css::non_negative(value);
            }
            // ...AND A SUBSTITUTED VALUE IS VALIDATED once its math has an
            // answer, as the path without math was above: `font-style:
            // attr(data-foo type(<angle>), calc(10 + 20))` falls back to
            // the number 30, which font-style cannot take, and is invalid
            // at computed-value time (attr-all-types). A value that is
            // still a function - `min(10px, 5%)` - is valid as it stands.
            if (had_var && !css::may_have_math(value)) {
                const css::value_check checked = css::check_declaration(property, value);
                if (!checked.valid) {
                    unset();
                    return;
                }
                value = std::move(checked.serialized);
            }
        }
        // FONT SIZE IS ALREADY RESOLVED - the pre-pass above did it, because
        // every `em` in every other declaration needed the answer first. Emit
        // that number rather than re-deriving it here, so there is exactly one
        // place a font size is computed and no way for the two to disagree.
        if (d.property == font_size_ && font_size_resolved) {
            value = css::serialize_calc(css::calc_result{own_font_size, 0.0f, false});
        }
        // EVERY RELATIVE LENGTH FOLDS TO PIXELS HERE, after expansion so a
        // shorthand's components each get their own answer. `padding: 1rem 2em`
        // becomes four px longhands, which is what a computed value is.
        //
        // It happens here rather than in layout because this is the only place
        // that knows all three bases at once: the element's own font size, the
        // ROOT's, and the viewport.
        //
        // A BARE NUMBER IS LEFT ALONE: `line-height: 1.5` is not 1.5px.
        //
        // EVERY DIMENSION FAMILY, not only lengths. `transition-delay: 12ms`
        // computes to `0.012s` and `rotate: 100grad` to `90deg` (CSS Values 4
        // 6.4-6.5); `canonical_dimension_text` is a superset of the length case
        // and still answers `96px` for `1in`.
        const auto folded = [&](std::string text, atom longhand) {
            if (auto canonical = css::canonical_dimension_text(text, lengths_for(longhand))) {
                return std::move(*canonical);
            }
            return text;
        };
        const auto expanded = expand_shorthand(property, value);
        if (expanded.empty()) {
            if (const css::declaration_block block = split_via_block(property, unfolded);
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
            // A substituted token stream is validated only now. If it is
            // not overflow grammar, the winning shorthand is invalid at
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

    // SPLIT THE RESULT. Anything inherited goes into a fresh inherited half built
    // on top of the parent's; everything else stays the element's own.
    //
    // THE LOAD-BEARING SHORTCUT: an element that declared nothing inherited keeps
    // its PARENT'S POINTER verbatim - no copy, no hash, no intern. That is what
    // makes a 128-entry inherited object free for the thousands of elements that
    // share one, and it is the difference between this being an optimisation and
    // being a regression.
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
    // inherited half - `get` would find them there - but removing them would make
    // an element's own `color: red` invisible to anything that enumerates its own
    // declarations, which is what getComputedStyle's key list is.
    return table_.intern(std::move(out), std::move(result_inherited));
}

} // namespace ctbrowser::style
