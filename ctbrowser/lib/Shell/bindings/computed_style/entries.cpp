// dom_bindings - computed_style_entries: every property of one element as
// (css name, value) pairs, gathered from the style map, the box tree and the
// fragment tree, with the property table supplying what nothing declared.

#include "geometry.hpp"
#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

namespace {

// DOM 4.2.2.3 "find a slot", for one light-DOM child of a shadow host: the
// child's `slot` attribute names the slot it wants - the empty name is the
// default slot - and the first `<slot>` of that name in the host's shadow
// tree takes it. Nothing here assigns the slot for rendering; this answers
// only whether the child is in the flat tree at all.
[[nodiscard]] bool slotted_into(const read_txn & txn, atom_table & atoms, node_id root,
                                node_id child) {
    const std::string_view wanted = txn.attribute_value(child, atoms.intern("slot"));
    const atom slot_tag = atoms.intern_lower("slot");
    const atom name_attribute = atoms.intern("name");
    bool found = false;
    const auto walk = [&](auto && self, node_id at) -> void {
        if (found) { return; }
        if (txn.tag(at).value_or(atom{}) == slot_tag && txn.element_ns(at) == node_ns::html &&
            txn.attribute_value(at, name_attribute) == wanted) {
            found = true;
            return;
        }
        for (const node_id next : txn.children(at)) { self(self, next); }
    };
    walk(walk, root);
    return found;
}

// Everything about one element that answering a property needs, gathered ONCE.
} // namespace

// EVERY PROPERTY OF ONE ELEMENT, AS (css name, value) PAIRS: the longhands the
// table knows, lexicographically, then the shorthands that can be reassembled,
// then whatever the element declared that the table has never heard of. Empty
// for an element that is not in the document.
//
// The order is the object's: the leading run is exactly the set CSSOM's indexed
// properties enumerate, and it is already sorted.
std::vector<std::pair<std::string, std::string>> dom_bindings::computed_style_entries(node_id id) {
    return computed_style_entries(id, {});
}

std::vector<std::pair<std::string, std::string>> dom_bindings::computed_style_entries(
    node_id id, const style::computed_style_ptr & pseudo) {
    std::vector<std::pair<std::string, std::string>> answers;

    computed_probe at;
    at.pseudo = pseudo;
    at.box = box_for(boxes_, id);
    at.frag = fragment_for(fragments_, id);
    at.font_size = at.box != nullptr ? at.box->font_size : 16.0f;
    // IS THIS ELEMENT IN THE DOCUMENT AT ALL? A detached one has no computed
    // style: CSSOM leaves getComputedStyle on an element that is not being
    // rendered answering computed rather than resolved values, and every engine
    // returns an EMPTY declaration - which is what css/cssom's
    // getComputedStyle-detached-subtree asserts, `length == 0` and
    // `color === ""`. The cascade never resolved such an element either, so
    // publishing the table's 125 initial values for it would be inventing
    // an answer about a box that does not exist.
    bool connected = false;
    {
        const auto txn = doc_->read();
        for (node_id up = id; up;) {
            at.chain.push_back(up);
            const node_id next = txn.parent(up);
            if (next == up) { break; }
            up = next;
        }
        connected = !at.chain.empty() && at.chain.back() == txn.root();
        // ...AND IN THE FLAT TREE. A light-DOM child of a shadow host is
        // rendered only through a `<slot>` in the host's shadow tree that
        // takes it (DOM 4.2.2.3 "find a slot"); without one it is outside the
        // flat tree and has no computed style, which is what CSS Scoping 3 §3.2
        // says and getComputedStyle-detached-subtree asserts ("outside the flat
        // tree"). The host itself is in the tree; only what is beneath it is
        // in question.
        for (std::size_t up = 1; connected && up < at.chain.size(); ++up) {
            const node_id root = shadow_root_of(at.chain[up]);
            if (root && !slotted_into(txn, *atoms_, root, at.chain[up - 1])) { connected = false; }
        }
        at.is_root = id == txn.root();
        // A pseudo-element's parent is its originating element, so the element
        // takes the parent's place in the chain and the pseudo has no box.
        if (pseudo) {
            at.chain.insert(at.chain.begin(), id);
            at.is_root = false;
            at.box = nullptr;
            at.frag = nullptr;
        }
        // The containing-block width a percentage resolves against: the parent's
        // CONTENT width, or the viewport at the root. That is the parent's
        // fragment less its padding, which is what content_width_of computes
        // (lib/Layout/algorithm.cpp) - there are no borders in this box model yet.
        // The containing block for an absolutely positioned element: the padding
        // box of the nearest POSITIONED ancestor, or the initial containing block
        // when there is none. Same rule the layout pass uses (layout/position.hpp),
        // asked here rather than shared because the two have different inputs -
        // that one walks the fragment tree once, this one answers about one node.
        at.position = at.box != nullptr ? at.box->position : layout::position_kind::static_;
        const auto abs_rect = [&](node_id of) {
            return fragments_ != nullptr ? absolute_rect_of(*fragments_, of).value_or(rect{})
                                         : rect{};
        };
        at.box_abs = abs_rect(id);
        at.containing = rect{0, 0, static_cast<float>(viewport_width_),
                             fragments_ != nullptr ? fragments_->bounds.height : 0.0f};
        // A FIXED box's containing block is the viewport - unless an ancestor
        // establishes one for fixed boxes too: a transform, a perspective, a
        // filter, paint or layout containment, or a will-change of one of them
        // (CSS Position 3 §2.1, CSS Transforms 1 §2, Filter Effects 1 §5).
        const auto holds_fixed = [&](node_id node) {
            if (styles_ == nullptr) { return false; }
            const auto found = styles_->find(style::engine::key_of(node));
            if (found == styles_->end() || !found->second) { return false; }
            const auto value = [&](std::string_view property) {
                return collapse_keyword(found->second->get(atoms_->intern(property)));
            };
            const auto set = [](const std::string & v) { return !v.empty() && v != "none"; };
            const std::string contain = value("contain");
            const std::string will = value("will-change");
            return set(value("transform")) || set(value("perspective")) || set(value("filter")) ||
                   set(value("backdrop-filter")) || set(value("rotate")) || set(value("scale")) ||
                   set(value("translate")) || contain.find("paint") != std::string::npos ||
                   contain.find("layout") != std::string::npos || contain == "strict" ||
                   contain == "content" || will.find("transform") != std::string::npos ||
                   will.find("perspective") != std::string::npos ||
                   will.find("filter") != std::string::npos;
        };
        const bool fixed = at.position == layout::position_kind::fixed;
        // Only an out-of-flow box has one; the walk is not paid for the rest.
        const bool out_of_flow = fixed || at.position == layout::position_kind::absolute;
        for (std::size_t up = 1; out_of_flow && up < at.chain.size(); ++up) {
            const layout::box_node * ancestor = box_for(boxes_, at.chain[up]);
            if (ancestor == nullptr) { continue; }
            // ...and what holds a fixed box holds an absolute one too.
            const bool holds = holds_fixed(at.chain[up]);
            if (fixed ? !holds : !(ancestor->is_positioned() || holds)) { continue; }
            const rect outer = abs_rect(at.chain[up]);
            const layout::constraints c{outer.width, outer.height, ancestor->font_size};
            const layout::resolved_edges e = layout::resolve_edges(*ancestor, c);
            at.containing = rect{outer.x + e.border_left, outer.y + e.border_top,
                                 std::max(0.0f, outer.width - e.border_left - e.border_right),
                                 std::max(0.0f, outer.height - e.border_top - e.border_bottom)};
            break;
        }

        at.basis = static_cast<float>(viewport_width_);
        at.basis_height = fragments_ != nullptr ? fragments_->bounds.height : 0.0f;
        // The ROOT ELEMENT's font size, for a `rem` folded at used-value time:
        // the chain ends at the document, and the element before it is <html>.
        if (at.chain.size() >= 2) {
            const layout::box_node * root_box = box_for(boxes_, at.chain[at.chain.size() - 2]);
            if (root_box != nullptr) { at.root_font_size = root_box->font_size; }
        }
        if (at.chain.size() >= 2) {
            const node_id parent = at.chain[1];
            const layout::box_node * up_box = box_for(boxes_, parent);
            at.flex_item = up_box != nullptr && up_box->kind == layout::box_kind::flex;
            at.definite_height =
                up_box == nullptr || !(up_box->height.is_auto() || up_box->height.is_intrinsic());
            if (const layout::fragment * up = fragment_for(fragments_, parent)) {
                at.basis = up->bounds.width;
                at.basis_height = up->bounds.height;
                // A `display: contents` parent has no box, so neither padding
                // nor border of its own (getComputedStyle-pseudo).
                bool boxless = false;
                if (styles_ != nullptr) {
                    const auto found = styles_->find(style::engine::key_of(parent));
                    boxless = found != styles_->end() && found->second &&
                              collapse_keyword(found->second->get(atoms_->intern("display"))) ==
                                  "contents";
                }
                if (up_box != nullptr && !boxless) {
                    const layout::side_lengths & pad = up_box->padding;
                    // A PERCENTAGE PADDING RESOLVES AGAINST THE WIDTH on all four
                    // sides (CSS 2.1 §8.4), so the vertical pair is subtracted
                    // using the horizontal basis too. Taking the height as the
                    // basis there is the classic way to get a containing block
                    // that is a few pixels short.
                    at.basis -= pad.left.resolve(at.basis, up_box->font_size) +
                                pad.right.resolve(at.basis, up_box->font_size);
                    at.basis_height -= pad.top.resolve(at.basis, up_box->font_size) +
                                       pad.bottom.resolve(at.basis, up_box->font_size);
                    // ...AND THE BORDERS: the fragment is the border box, and
                    // a 2px border counted into the content height made
                    // `top: 10%` of a 100px box 10.4px
                    // (getComputedStyle-insets-relative).
                    const layout::side_lengths & border = up_box->border;
                    at.basis -= border.left.resolve(0, up_box->font_size) +
                                border.right.resolve(0, up_box->font_size);
                    at.basis_height -= border.top.resolve(0, up_box->font_size) +
                                       border.bottom.resolve(0, up_box->font_size);
                }
            }
        }
    }
    if (!connected) { return answers; }

    // The cascade's text for one property. Empty when nothing declared it, and
    // the caller substitutes the property table's initial value - which is what
    // a computed value IS for a property no rule reached.
    // BY VALUE, all of it. The probe is a handful of pointers and a short vector
    // of node ids, so a copy is nothing; the pointers are the browser's and stay
    // valid for exactly as long as this call, which is why every answer is
    // computed before it returns.
    const style::style_map * styles = styles_;
    atom_table * atoms = atoms_;
    // WHAT THE CASCADE SAID ABOUT ONE NODE. Taken as a node rather than as
    // "this element" because two of the answers below are about the PARENT:
    // whether it is a grid container, which no box kind records.
    const auto declared_on = [styles, atoms](node_id node,
                                             std::string_view property) -> std::string_view {
        if (styles == nullptr || !node) { return {}; }
        const auto found = styles->find(style::engine::key_of(node));
        if (found == styles->end() || !found->second) { return {}; }
        return found->second->get(atoms->intern(property));
    };
    // A GRID CONTAINER IS A DECLARATION, NOT A BOX KIND - `display: grid` makes a
    // block box here, so the box tree cannot be asked the way it is asked for
    // flex just above.
    if (at.chain.size() >= 2) {
        const std::string parent_display = collapse_keyword(declared_on(at.chain[1], "display"));
        at.grid_item = parent_display == "grid" || parent_display == "inline-grid";
    }
    // ONE LOOKUP. This used to walk DOM ancestors for an inherited property, because
    // the cascade produced only the declarations that matched and inheritance happened
    // in four other places downstream. The cascade inherits now, so `get` on the
    // element's own style is the whole answer - and the fifth ad-hoc inheritance
    // mechanism this file used to be is gone.
    // ...AND WHAT AN ANIMATION SAYS ON TOP OF IT. A running Web Animation's
    // effect value replaces the cascade's text for the properties it animates,
    // at the animation's current time (lib/Shell/bindings/animations.cpp), and
    // it goes in HERE so that every rule below - a percentage against its
    // containing block, an inset's used value - runs on the interpolated text
    // exactly as it runs on a declared one. The underlying value it may need
    // is the cascade's own answer for the same element.
    std::vector<std::pair<std::string, std::string>> animated =
        animated_values(id, at.font_size, [&](std::string_view property) {
            return declared_on(at.chain.front(), property);
        });
    // ...FILED UNDER THE PHYSICAL LONGHANDS the cascade stores, so an
    // animation on `margin-block` or `margin-block-start` is read where the
    // rules below look for it: a shorthand split by the declaration block, a
    // logical name mapped on this element's writing mode and direction.
    if (!animated.empty() && !at.chain.empty()) {
        const std::string writing_mode{declared_on(at.chain.front(), "writing-mode")};
        const std::string direction{declared_on(at.chain.front(), "direction")};
        std::vector<std::pair<std::string, std::string>> physical;
        for (auto & [name, text] : animated) {
            style::css::declaration_block block;
            if (style::css::longhands_of(name).empty() ||
                !style::css::set_declaration(block, name, text, false)) {
                block.push_back({name, text, false});
            }
            for (const style::css::declaration & one : block) {
                std::string mapped =
                    style::css::physical_property_of(one.name, writing_mode, direction);
                physical.emplace_back(mapped.empty() ? one.name : std::move(mapped), one.value);
            }
        }
        animated = std::move(physical);
    }
    // The one property whose INITIAL value is being asked for as though it
    // had been declared - see the longhand loop below.
    const style::css::property_syntax * as_initial = nullptr;
    const auto declared = [declared_on, at, atoms, &animated,
                           &as_initial](std::string_view property) -> std::string_view {
        if (at.chain.empty()) { return {}; }
        if (at.pseudo) { return at.pseudo->get(atoms->intern(property)); }
        for (const auto & [name, text] : animated) {
            if (std::string_view{name} == property) { return text; }
        }
        const std::string_view held = declared_on(at.chain.front(), property);
        if (held.empty() && as_initial != nullptr && as_initial->name == property) {
            return as_initial->initial;
        }
        return held;
    };

    // THE FONT SIZE OF AN ELEMENT WITH NO BOX. Every `em` below resolves against
    // it, and the box is where it normally lives - but a `display: none` element
    // has no box and still has a computed font-size, which Chrome reports and
    // which css/cssom's getComputedStyle-insets-nobox depends on: it declares
    // `font-size: 10px` beside `top: 1em` and expects `10px`. Only an absolute
    // length can be honoured here; a percentage or an em needs the parent's
    // computed size, and the cascade does not resolve one for a box that was
    // never built.
    //
    // ...AND OF THE ROOT, whose box is the viewport's and carries the default
    // size whatever `:root { font-size }` said (rem-unit-root-element). The
    // cascade folded the root's own size to pixels, so the declared text is
    // the answer there too.
    if (at.box == nullptr || at.is_root) {
        const layout::length declared_size = layout::parse_length(declared("font-size"));
        if (!declared_size.is_auto() && declared_size.u != layout::unit::percent &&
            declared_size.u != layout::unit::em) {
            at.font_size = declared_size.resolve(0.0f, 16.0f);
        }
    }
    // ...AND OF AN ELEMENT WHOSE FONT SIZE IS ANIMATED. The box carries the
    // cascade's size; a running animation's is the one getComputedStyle owes,
    // and every `em` below resolves against it. A percentage or an `em` in
    // `font-size` is the PARENT's, as it is in the cascade.
    for (const auto & [name, text] : animated) {
        if (name != "font-size" || at.pseudo) { continue; }
        const layout::box_node * parent =
            at.chain.size() >= 2 ? box_for(boxes_, at.chain[1]) : nullptr;
        style::css::length_context sizes;
        sizes.font_size = parent != nullptr ? parent->font_size : 16.0f;
        sizes.viewport_width = static_cast<float>(viewport_width_);
        const std::string_view given = trim(text, html_whitespace);
        std::optional<float> px;
        if (given.ends_with('%')) {
            float percent = 0;
            const char * end = given.data() + given.size() - 1;
            if (std::from_chars(given.data(), end, percent).ptr == end) {
                px = sizes.font_size * percent / 100.0f;
            }
        } else {
            px = style::css::length_text_to_px(given, sizes);
        }
        if (px) { at.font_size = std::max(0.0f, *px); }
    }
    // THE PRINCIPAL BOX, and the two ways there is not one.
    const std::string declared_display = collapse_keyword(declared("display"));
    at.has_box = at.box != nullptr && declared_display != "none" && declared_display != "contents";
    // A PSEUDO-ELEMENT GENERATES A BOX when its `content` says so (CSS Pseudo 4
    // §5.1): then a percentage width resolves against the element, as a box
    // of its own would; without one it answers computed values.
    if (at.pseudo) {
        const std::string content = collapse_keyword(declared("content"));
        at.has_box = !content.empty() && content != "none" && content != "normal" &&
                     declared_display != "none" && declared_display != "contents";
    }
    at.inline_non_replaced = at.box != nullptr && at.box->kind == layout::box_kind::inline_;

    // A LENGTH AS A COMPUTED VALUE - CSS Values 3 §5.2 and CSSOM's "otherwise,
    // the computed value". `auto` and every other keyword survive verbatim, a
    // percentage survives as a percentage, and an absolute or font-relative
    // length becomes px. Nothing here needs a containing block, which is the
    // whole point: this is the answer for the cases where there is not one.
    const auto computed_length = [at](std::string_view text) -> std::string {
        const std::string_view given = trim(text, html_whitespace);
        if (given.empty()) { return {}; }
        const layout::length len = layout::parse_length(given);
        // `auto`, `min-content`, a percentage and anything parse_length does not
        // model are all their own computed value.
        if (len.is_auto() || len.is_intrinsic() || len.u == layout::unit::percent) {
            return collapse_keyword(given);
        }
        return px_text(len.resolve(0.0f, at.font_size));
    };

    const auto value_of = [this, at, id, atoms, declared, declared_on,
                           computed_length](std::string_view property) -> std::string {
        // 0. A CUSTOM PROPERTY IS NOT A KEYWORD. Its value is an arbitrary token
        //    sequence whose case is significant and whose computed value is the
        //    substituted text, so it is handed back as written rather than folded
        //    the way `display: BLOCK` is.
        if (property.starts_with("--")) {
            std::string text{trim(declared(property), html_whitespace)};
            if (text == style::guaranteed_invalid) { return {}; }
            // ...WITH ITS SUBSTITUTIONS PERFORMED. The cascade keeps a custom
            //    property as written so a later `var()` can read it lazily, but
            //    its COMPUTED value has every `var()` and `attr()` replaced
            //    (CSS Variables 1 §2.2, CSS Values 5 §attr), and that is what a
            //    page reading `--x: attr(data-foo px) 11px` back is owed:
            //    `10px 11px`. The same lookups the cascade used, asked of this
            //    element - its own custom properties, inherited ones included,
            //    and its attributes.
            if (style::css::may_have_var(text)) {
                const style::css::custom_lookup custom =
                    [&](atom name) -> std::optional<std::string_view> {
                    const std::string_view held = declared(atoms->text(name));
                    if (held.empty() || held == style::guaranteed_invalid) { return std::nullopt; }
                    return held;
                };
                const style::css::attribute_lookup attributes =
                    [&](std::string_view name) -> std::optional<std::string> {
                    const auto txn = doc_->read();
                    const atom key = atoms->intern(name);
                    if (!txn.has_attribute(id, key)) { return std::nullopt; }
                    return std::string{txn.attribute_value(id, key)};
                };
                // ...AND WHAT AN `if()` IN IT MAY ASK: the parent's custom
                //    properties, substituted in the parent's own scope, for
                //    `style(--x: inherit)`; any other property's cascaded text;
                //    the window for `media()`.
                style::css::condition_environment conditions;
                conditions.property = std::string{property};
                conditions.lengths.font_size = at.font_size;
                conditions.lengths.root_font_size = at.root_font_size;
                conditions.lengths.viewport_width = static_cast<float>(viewport_width_);
                conditions.lengths.viewport_height = static_cast<float>(viewport_height_);
                conditions.lengths.element_key = style::engine::key_of(id);
                conditions.lengths.property = property;
                // ...AND WHERE THE ELEMENT SITS AMONG ITS SIBLINGS, for the
                //    tree-counting functions a custom property may hold -
                //    `ident("vtl-" sibling-index())` (ident-function-substitution).
                if (at.chain.size() >= 2) {
                    const auto txn = doc_->read();
                    std::uint32_t index = 0;
                    std::uint32_t count = 0;
                    for (const node_id sibling : txn.children(at.chain[1])) {
                        if (txn.kind(sibling).value_or(node_kind::text) != node_kind::element) {
                            continue;
                        }
                        ++count;
                        if (sibling == id) { index = count; }
                    }
                    conditions.lengths.sibling_index = index;
                    conditions.lengths.sibling_count = count;
                }
                conditions.inherited = [&](std::string_view name) -> std::optional<std::string> {
                    if (at.chain.size() < 2) { return std::nullopt; }
                    const node_id parent = at.chain[1];
                    const std::string_view held = declared_on(parent, name);
                    if (held.empty() || held == style::guaranteed_invalid) { return std::nullopt; }
                    const style::css::custom_lookup above =
                        [&](atom n) -> std::optional<std::string_view> {
                        const std::string_view v = declared_on(parent, atoms->text(n));
                        if (v.empty() || v == style::guaranteed_invalid) { return std::nullopt; }
                        return v;
                    };
                    return style::css::substitute_var(held, above, *atoms);
                };
                conditions.computed = [&](std::string_view name) -> std::optional<std::string> {
                    const std::string_view held = declared(name);
                    if (held.empty()) { return std::nullopt; }
                    return std::string{held};
                };
                if (selector_engine_ != nullptr) {
                    conditions.media = [this](std::string_view condition) {
                        return style::css::evaluate_media_condition(
                            condition, selector_engine_->environment());
                    };
                    conditions.registered = [this, atoms](std::string_view name) {
                        return selector_engine_->registration_of(atoms->intern(name));
                    };
                }
                conditions.canonical_color = [](std::string_view text) {
                    const std::string computed = style::css::computed_color(text, {});
                    if (!computed.empty()) { return computed; }
                    const std::optional<color> c = paint::parse_color(text);
                    return c ? color_text(*c) : std::string{text};
                };
                std::optional<std::string> done =
                    style::css::substitute_var(text, custom, *atoms, attributes, &conditions);
                // INVALID AT COMPUTED-VALUE TIME: the guaranteed-invalid value,
                //    which a custom property serialises as nothing at all.
                if (!done) { return {}; }
                text = std::move(*done);
                // A SUBSTITUTED CSS-WIDE KEYWORD IS THAT KEYWORD (CSS Values 5
                //    §arbitrary-substitution): on an unregistered property
                //    `inherit` and `unset` are the parent's value and `initial`
                //    the guaranteed-invalid one (attr-css-wide-keywords). A
                //    registered property had this settled in the cascade.
                const std::string_view word = trim(text, html_whitespace);
                const bool registered = conditions.registered && conditions.registered(property);
                if (!registered &&
                    (ascii_iequals(word, "inherit") || ascii_iequals(word, "unset") ||
                     ascii_iequals(word, "revert"))) {
                    return conditions.inherited(property).value_or("");
                }
                if (!registered && ascii_iequals(word, "initial")) { return {}; }
            }
            // A REGISTERED `<color>` SERIALISES AS A COLOUR - `rgb(0, 0, 255)`
            //    for `blue` - which the cascade, having no colour parser, left
            //    as the keyword (attr-security).
            if (const style::css::property_registration * registration =
                    selector_engine_ != nullptr
                        ? selector_engine_->registration_of(atoms->intern(property))
                        : nullptr;
                registration != nullptr &&
                registration->syntax.find("<color>") != std::string::npos) {
                if (std::string computed = style::css::computed_color(text, {});
                    !computed.empty()) {
                    return computed;
                }
                if (const std::optional<color> c = paint::parse_color(text)) {
                    return color_text(*c);
                }
            }
            return text;
        }
        std::string_view text;
        if (auto geometry =
                resolved_geometry(property, text, at, declared, declared_on, computed_length,
                                  boxes_, fragments_, viewport_width_)) {
            return std::move(*geometry);
        }
        // 3b. EVERY OTHER LENGTH-VALUED PROPERTY, ABSOLUTIZED. A computed value
        //     is an absolute length wherever the specified value was a relative
        //     one, and that is a rule about the property's TYPE rather than
        //     about the eleven names above - which is why it is asked of the
        //     table rather than of a third list. `letter-spacing: 1em` under a
        //     20px font computes to `20px` in every engine and was reported here
        //     as `1em`; so were `word-spacing`, `text-indent`, `vertical-align`,
        //     `flex-basis`, `row-gap`, `column-gap` and `outline-offset`.
        //
        //     A PERCENTAGE STAYS A PERCENTAGE for all of them, which is the
        //     difference from the rule above: none of these is in CSSOM's list
        //     of properties whose resolved value is the used one, so there is no
        //     containing block in the question at all. `text-indent: 10%`
        //     computes to `10%`.
        const style::css::property_syntax * known = style::css::find_property(property);
        // 3a. A `<position>`, whose keywords compute to percentages: `10% center`
        //     is `10% 50%` and `right 20px` is `calc(100% - 20px)`, CSS Values 5
        //     §position. `background-position` is not modelled as one - its
        //     grammar has a three-value form and a comma list - so those forms
        //     fall through to the background layer resolver below.
        //     ...BUT NOT perspective-origin, whose RESOLVED value is the used
        //     one in pixels against the box (CSS Transforms 2 §6.2, CSSOM's
        //     resolved-value list) - the transform branch below answers it.
        if (((known != nullptr && known->kind == style::css::value_kind::position) ||
             property == "background-position") &&
            property != "perspective-origin") {
            std::string resolved = style::css::computed_position(text, declared("writing-mode"),
                                                                 declared("direction"));
            // ...its lengths absolute, as every computed length is: `right
            // 4em` is `100% 160px` at a 40px font (offset-anchor-computed).
            if (!resolved.empty() && property != "background-position") {
                style::css::length_context lengths;
                lengths.font_size = at.font_size;
                lengths.root_font_size = at.root_font_size;
                lengths.viewport_width = static_cast<float>(viewport_width_);
                lengths.viewport_height = static_cast<float>(viewport_height_);
                std::string absolute;
                for (const std::string_view part : split_top_level(resolved, " ")) {
                    std::string one{part};
                    if (ascii_istarts_with(one, "calc(")) {
                        one = style::css::fold_math(one, lengths).text;
                    } else if (!one.ends_with('%')) {
                        if (const std::optional<float> px =
                                style::css::length_text_to_px(one, lengths)) {
                            one = px_text(*px);
                        }
                    }
                    absolute += (absolute.empty() ? "" : " ") + one;
                }
                resolved = std::move(absolute);
            }
            if (!resolved.empty()) { return resolved; }
        }
        // CSS Text 4 §7.1-7.2: `word-spacing: normal` computes to zero, and a
        // letter-spacing of zero reads back as `normal` - so an animation's
        // `0px` end and a declared `normal` agree (letter-spacing-computed,
        // word-spacing-computed, letter-spacing-interpolation).
        if (property == "word-spacing" && ascii_iequals(trim(text, html_whitespace), "normal")) {
            return "0px";
        }
        if (known != nullptr && (known->kind == style::css::value_kind::length ||
                                 known->kind == style::css::value_kind::length_percentage)) {
            std::string length = computed_length(text);
            if (property == "letter-spacing" && length == "0px") { return "normal"; }
            return length;
        }
        // 3c. THE FONT FAMILY LIST, whose case is the author's and whose quoting
        //     is CSSOM's. Answered before the keyword fold below, which would
        //     lowercase it.
        if (property == "font-family") { return style::css::serialize_font_family(text); }
        // 3d. A TRANSFORM LIST, multiplied out to the `matrix()` CSS Transforms
        //     says its resolved value is - see `transform_matrix_text`.
        if (property == "transform" && !ascii_iequals(trim(text, html_whitespace), "none")) {
            transform_basis basis{at.font_size, at.root_font_size, -1, -1};
            if (at.frag != nullptr && at.has_box) {
                basis.width = at.frag->bounds.width;
                basis.height = at.frag->bounds.height;
            }
            return transform_matrix_text(trim(text, html_whitespace), basis);
        }
        // 3e. A SHADOW LIST: colour first, resolved, and lengths in px - see
        //     shadow_text. A colour it left as `currentcolor` is substituted
        //     with the rest of them below.
        if (property == "box-shadow" || property == "text-shadow") {
            return shadow_text(text, at.font_size, property == "box-shadow");
        }
        // 4. COLOURS, CSS Color 4 §15's computed value: a legacy colour as
        //    `rgb()`, `lab()` and the rest as themselves, `color-mix()` and a
        //    relative colour resolved against this element's `color` - which
        //    is what `currentcolor` means inside one, and the parent's when
        //    the property IS `color` (the caller substituted a bare one).
        //    An `<image>` list is the same walk over each gradient's colours,
        //    with its lengths in pixels (CSS Images 4).
        const bool image_property = property == "background-image" || property == "mask-image" ||
                                    property == "border-image-source" ||
                                    property == "mask-border-source" ||
                                    property == "list-style-image";
        const bool filter_property = property == "filter" || property == "backdrop-filter";
        const bool grid_property =
            property == "grid-template-columns" || property == "grid-template-rows" ||
            property == "grid-auto-columns" || property == "grid-auto-rows" ||
            property == "grid-row-start" || property == "grid-row-end" ||
            property == "grid-column-start" || property == "grid-column-end";
        const bool transform_property = property == "rotate" || property == "scale" ||
                                        property == "translate" || property == "transform-origin" ||
                                        property == "perspective-origin";
        const bool layer_property =
            property == "background-repeat" || property == "mask-repeat" ||
            property == "background-size" || property == "mask-size" ||
            property == "background-position" || property == "background-position-x" ||
            property == "background-position-y" || property == "mask-position";
        // `outline-color: auto` is the UA's focus-ring colour, and it stays
        // `auto` only while the outline's style is `auto` too; beside any
        // other style it computes to `currentcolor` (CSS UI 4 §5.3).
        if (property == "outline-color" && ascii_iequals(trim(text, html_whitespace), "auto")) {
            return collapse_keyword(declared("outline-style")) == "auto" ? "auto" : "currentcolor";
        }
        const bool gap_property =
            ascii_iequals_any(property, {"column-rule-width", "row-rule-width", "column-rule-color",
                                         "row-rule-color"});
        if (is_color_property(property) || image_property || filter_property || grid_property ||
            transform_property || layer_property || gap_property || property == "caret-color") {
            style::css::length_context bases;
            bases.font_size = at.font_size;
            bases.root_font_size = at.root_font_size;
            bases.line_height = at.box != nullptr ? at.box->line_height : at.font_size * 1.25f;
            bases.viewport_width = static_cast<float>(viewport_width_);
            bases.viewport_height = fragments_ != nullptr ? fragments_->bounds.height : 0.0f;
            style::css::color_context ctx;
            ctx.lengths = &bases;
            // THE USED COLOUR SCHEME (CSS Color Adjust 1 §2.1): `dark` when
            // the element's `color-scheme` lists dark and not light - this
            // engine's user prefers light, so `light dark` is light.
            {
                const std::string scheme = collapse_keyword(declared("color-scheme"));
                bool light = false, dark = false;
                for (const std::string_view word : split_top_level(scheme, " ")) {
                    light = light || ascii_iequals(word, "light");
                    dark = dark || ascii_iequals(word, "dark");
                }
                ctx.dark = dark && !light;
            }
            // `currentcolor` is this element's `color` - or, when the property
            // IS `color`, the parent's (CSS Color 4 §7.1).
            std::string own_color;
            {
                const bool of_parent = property == "color";
                const std::string_view declared_color =
                    of_parent ? (at.chain.size() >= 2 ? declared_on(at.chain[1], "color")
                                                      : std::string_view{})
                              : declared("color");
                const std::string_view text = trim(declared_color, html_whitespace);
                own_color = text.empty() || ascii_iequals(text, "currentcolor")
                                ? std::string{"rgb(0, 0, 0)"}
                                : style::css::computed_color(text, {});
                if (own_color.empty()) { own_color = "rgb(0, 0, 0)"; }
                ctx.current_color = own_color;
            }
            if (property == "caret-color") {
                // CSS UI 4 §6.2.1: `auto` computes to currentcolor, and a
                // second colour that is `auto` is no second colour at all.
                std::vector<std::string> parts;
                bool second_auto = false;
                for (const std::string_view raw : split_top_level(text, html_whitespace)) {
                    const std::string_view part = trim(raw, html_whitespace);
                    if (part.empty()) { continue; }
                    const bool automatic = ascii_iequals(part, "auto");
                    second_auto = automatic && !parts.empty();
                    std::string one = automatic || ascii_iequals(part, "currentcolor")
                                          ? own_color
                                          : style::css::computed_color(part, ctx);
                    if (one.empty()) {
                        if (const std::optional<color> c = paint::parse_color(part)) {
                            one = color_text(*c);
                        } else if (const std::optional<color> s = system_color(part)) {
                            one = color_text(*s);
                        }
                    }
                    parts.push_back(one.empty() ? std::string{part} : std::move(one));
                }
                if (parts.empty()) { return own_color; }
                if (parts.size() == 2 && second_auto) { parts.pop_back(); }
                return parts.size() == 2 ? parts[0] + " " + parts[1] : parts[0];
            }
            if (gap_property) {
                // A gap rule list, item by item (lib/Style/css/properties/gaps.cpp).
                const std::string_view given = trim(text, html_whitespace);
                const bool color = property.ends_with("-color");
                std::string computed = style::css::computed_gap_list(
                    property, given.empty() ? (color ? "currentcolor" : "medium") : given, ctx);
                return computed.empty() ? collapse_keyword(text) : computed;
            }
            if (image_property) {
                if (std::string computed = style::css::computed_image(text, ctx);
                    !computed.empty()) {
                    return computed;
                }
                return collapse_keyword(text);
            }
            if (layer_property) {
                if (std::string computed = style::css::computed_background_list(
                        property, text, ctx, declared("writing-mode"), declared("direction"));
                    !computed.empty()) {
                    return computed;
                }
                return collapse_keyword(text);
            }
            if (transform_property) {
                const float box_width = at.frag != nullptr ? at.frag->bounds.width : 0.0f;
                const float box_height = at.frag != nullptr ? at.frag->bounds.height : 0.0f;
                if (std::string computed = style::css::computed_transform_property(
                        property, text, ctx, box_width, box_height);
                    !computed.empty()) {
                    return computed;
                }
                return collapse_keyword(text);
            }
            if (grid_property) {
                if (std::string computed = style::css::computed_grid(property, text, ctx);
                    !computed.empty()) {
                    return computed;
                }
                return collapse_keyword(text);
            }
            if (filter_property) {
                if (std::string computed = style::css::computed_filter(text, ctx);
                    !computed.empty()) {
                    return computed;
                }
                return collapse_keyword(text);
            }
            if (std::string computed = style::css::computed_color(text, ctx); !computed.empty()) {
                return computed;
            }
            if (const std::optional<color> c = paint::parse_color(text)) { return color_text(*c); }
            if (const std::optional<color> c = system_color(text)) { return color_text(*c); }
            return std::string{text};
        }
        // 4b. AN <alpha-value> IS A NUMBER once computed: `opacity: 90%` is `0.9`
        //     (CSS Color 4 §4.2), and so is a `calc(90%)` the cascade folded.
        //     `calc-numbers` asks for it by name.
        //     ...AND CLAMPED TO [0, 1], which is the property's range: a
        //     `calc(log(0))` the cascade folded to a very negative number is
        //     `0` here, and `opacity: 2` is `1`. exp-log-serialize asks for the
        //     first.
        if (property == "opacity" || property == "shape-image-threshold") {
            const std::string_view given = trim(text, html_whitespace);
            const bool percent = given.ends_with('%');
            float number = 0;
            const char * end = given.data() + given.size() - (percent ? 1 : 0);
            if (std::from_chars(given.data(), end, number).ec == std::errc{} &&
                (percent || end == given.data() + given.size())) {
                if (percent) { number /= 100.0f; }
                return number_text(std::min(1.0f, std::max(0.0f, number)));
            }
        }
        // 4b'''. ZOOM IS A NUMBER once computed, `normal` and the legacy `0`
        //       being 1 (CSS Viewport 1 §5, zoom-interpolation).
        if (property == "zoom") {
            if (std::string computed = style::css::computed_zoom(text, {}); !computed.empty()) {
                return computed;
            }
        }
        // 4b'''''. `match-parent` IS THE PARENT'S ALIGNMENT, a `start` or
        //         `end` there turned by the parent's direction into the side
        //         it is (CSS Text 3 §6.1) - the root's parent being the
        //         initial containing block, left to right.
        if (property == "text-align" && collapse_keyword(text) == "match-parent") {
            for (std::size_t up = at.pseudo ? 0 : 1; up < at.chain.size(); ++up) {
                std::string above = collapse_keyword(declared_on(at.chain[up], "text-align"));
                if (above == "match-parent") { continue; }
                if (above.empty()) { above = "start"; }
                if (above != "start" && above != "end") { return above; }
                const bool rtl = collapse_keyword(declared_on(at.chain[up], "direction")) == "rtl";
                return (above == "start") != rtl ? "left" : "right";
            }
            return "left";
        }
        // 4b''''''. FONT FEATURE AND VARIATION SETTINGS ARE ONE PER TAG, the
        //          last written, sorted by tag (CSS Fonts 4 §6.12, §7.2):
        //          `"tnum", "hist"` is `"hist", "tnum"`.
        if ((property == "font-feature-settings" || property == "font-variation-settings") &&
            collapse_keyword(text) != "normal") {
            std::vector<std::pair<std::string, std::string>> tags;
            bool ok = true;
            for (const std::string_view raw : split_top_level(text, ",")) {
                const std::string_view item = trim(raw, html_whitespace);
                const std::size_t close =
                    item.empty() ? std::string_view::npos : item.find(item.front(), 1);
                if (close == std::string_view::npos ||
                    (item.front() != '"' && item.front() != '\'')) {
                    ok = false;
                    break;
                }
                std::string tag{item.substr(1, close - 1)};
                std::erase_if(tags, [&tag](const auto & one) { return one.first == tag; });
                tags.emplace_back(std::move(tag),
                                  std::string{trim(item.substr(close + 1), html_whitespace)});
            }
            if (ok && !tags.empty()) {
                std::ranges::stable_sort(tags, {}, &std::pair<std::string, std::string>::first);
                std::string out;
                for (const auto & [tag, setting] : tags) {
                    out += (out.empty() ? "\"" : ", \"") + tag + "\"" +
                           (setting.empty() ? "" : " " + setting);
                }
                return out;
            }
        }
        // 4b''''. AN EMPHASIS MARK IS A SHAPE AND A FILL once computed (CSS
        //        Text Decoration 3 §3.1): a lone fill takes `circle`, or
        //        `sesame` in a vertical writing mode, and `filled` - the
        //        default - is left unwritten.
        if (property == "text-emphasis-style") {
            const std::string words = collapse_keyword(text);
            if (!words.empty() && words != "none" && words.front() != '"' &&
                words.front() != '\'') {
                bool open = false;
                std::string shape;
                for (const std::string_view word : split_top_level(words, " ")) {
                    if (word == "open") {
                        open = true;
                    } else if (word != "filled") {
                        shape = std::string{word};
                    }
                }
                if (shape.empty()) {
                    shape = collapse_keyword(declared("writing-mode")).starts_with("vertical")
                                ? "sesame"
                                : "circle";
                }
                return (open ? "open " : "") + shape;
            }
        }
        // 4b'''''''. CONTAINMENT COMPUTES TO ITS SHORT KEYWORDS (CSS Contain 2
        //           §2): `size layout style paint` is `strict` and `layout
        //           style paint` is `content`. The specified value keeps the
        //           words (contain-valid), so the collapse is made here.
        if (property == "contain") {
            const std::string words = collapse_keyword(text);
            if (words == "size layout style paint") { return "strict"; }
            if (words == "layout style paint") { return "content"; }
        }
        // 4b'. A BASIC SHAPE (lib/Style/css/properties/shapes.cpp).
        if (property == "clip-path" || property == "shape-outside" || property == "offset-path" ||
            property == "object-view-box") {
            style::css::length_context bases;
            bases.font_size = at.font_size;
            bases.root_font_size = at.root_font_size;
            bases.viewport_width = static_cast<float>(viewport_width_);
            bases.viewport_height = fragments_ != nullptr ? fragments_->bounds.height : 0.0f;
            std::string computed = style::css::computed_basic_shape(property, text, bases);
            if (!computed.empty()) { return computed; }
        }
        // 4f. A BORDER SPACING IS ONE OR TWO ABSOLUTE LENGTHS, never
        //     negative (CSS Tables 3 §border-spacing): `0` is `0px`, and
        //     `calc(10px - 0.5em)` under a 40px font is `0px`.
        if (property == "border-spacing") {
            style::css::length_context bases;
            bases.font_size = at.font_size;
            bases.root_font_size = at.root_font_size;
            bases.viewport_width = static_cast<float>(viewport_width_);
            bases.viewport_height = fragments_ != nullptr ? fragments_->bounds.height : 0.0f;
            std::string out;
            for (const std::string_view raw : split_top_level(text, html_whitespace)) {
                const std::string_view part = trim(raw, html_whitespace);
                if (part.empty()) { continue; }
                const style::css::math_answer answer = style::css::evaluate_math(part, bases);
                if (answer.outcome != style::css::math_outcome::resolved ||
                    answer.value.has_percent) {
                    return collapse_keyword(text);
                }
                out += (out.empty() ? "" : " ") +
                       px_text(static_cast<float>(std::max(answer.value.px, 0.0)));
            }
            return out.empty() ? collapse_keyword(text) : out;
        }
        // A FONT WEIGHT IS A NUMBER once computed (CSS Fonts 4 §2.2): `normal`
        // 400, `bold` 700, a folded calc() within [1, 1000], and `bolder` or
        // `lighter` the step §2.2.1's table takes from the parent's computed
        // weight.
        // ponytail: a weight whose text is the parent's is read as inherited,
        // so a `bolder` declared under a `bolder` parent steps once; ask the
        // cascade which half holds it if that pairing matters.
        if (property == "font-weight" && !at.pseudo) {
            const auto weight_at = [&](auto && self, std::size_t k,
                                       std::string_view held) -> double {
                const std::string word = collapse_keyword(held);
                if (word.empty() || word == "normal") { return 400; }
                if (word == "bold") { return 700; }
                if (word == "bolder" || word == "lighter") {
                    const std::string_view above = k + 1 < at.chain.size()
                                                       ? declared_on(at.chain[k + 1], "font-weight")
                                                       : std::string_view{};
                    const double base = k + 1 < at.chain.size() ? self(self, k + 1, above) : 400;
                    if (collapse_keyword(above) == word) { return base; }
                    if (word == "bolder") {
                        return base < 350 ? 400 : base < 550 ? 700 : base < 900 ? 900 : base;
                    }
                    return base < 100 ? base : base < 550 ? 100 : base < 750 ? 400 : 700;
                }
                double n = 0;
                if (std::from_chars(word.data(), word.data() + word.size(), n).ptr !=
                    word.data() + word.size()) {
                    return -1;
                }
                return std::clamp(n, 1.0, 1000.0);
            };
            if (const double weight = weight_at(weight_at, 0, text); weight > 0) {
                return number_text(static_cast<float>(weight));
            }
        }
        // `text-size-adjust: none` is no adjustment, which is `100%` (CSS
        // Mobile Text Size Adjustment 1 §2).
        if (property == "text-size-adjust" && collapse_keyword(text) == "none") { return "100%"; }
        // A scroll marker group's mode is `links` when none was written (CSS
        // Overflow 5 §3.2): `before` computes to `before links`.
        if (property == "scroll-marker-group") {
            const std::string words = collapse_keyword(text);
            if (words == "before" || words == "after") { return words + " links"; }
        }
        // hyphenate-limit-chars' numbers are <integer>s, so a calc() the
        // cascade folded to a fraction rounds (CSS Values 4 §10.9).
        if (property == "hyphenate-limit-chars") {
            std::string out;
            for (const std::string_view word : split_top_level(collapse_keyword(text), " ")) {
                double n = 0;
                const bool number =
                    std::from_chars(word.data(), word.data() + word.size(), n).ptr ==
                    word.data() + word.size();
                out += (out.empty() ? "" : " ") +
                       (number ? number_text(static_cast<float>(std::floor(n + 0.5)))
                               : std::string{word});
            }
            return out;
        }
        // `distribute` is the legacy spelling of `inter-character`, which is
        // what it computes to (CSS Text 3 §7.4).
        if (property == "text-justify" && collapse_keyword(text) == "distribute") {
            return "inter-character";
        }
        // text-fit's default `consistent` is left unwritten (text-fit-computed).
        if (property == "text-fit") {
            std::string words = collapse_keyword(text);
            if (const std::size_t at = words.find(" consistent"); at != std::string::npos) {
                words.erase(at, 11);
            }
            return words;
        }
        // 4h. offset-rotate's angle in degrees, `reverse` as `auto 180deg`
        //     (Motion Path 1 §4.4, offset-rotate-computed).
        if (property == "offset-rotate") {
            if (std::string computed = style::css::computed_offset_rotate(text, true);
                !computed.empty()) {
                return computed;
            }
        }
        // 4g. A FONT STYLE'S ANGLE IS IN DEGREES, and `oblique 0deg` is
        //     `normal` (CSS Fonts 4 §2.4, font-style-computed).
        if (property == "font-style") {
            const std::string words = collapse_keyword(text);
            if (words.starts_with("oblique ")) {
                const style::css::math_answer answer =
                    style::css::evaluate_math(std::string_view{words}.substr(8), {});
                if (answer.outcome == style::css::math_outcome::resolved &&
                    !answer.value.is_number &&
                    answer.value.type == style::css::numeric_type::angle) {
                    // ...and within [-90deg, 90deg], where an animation past
                    // either end stops (font-style-interpolation).
                    const double angle = std::clamp(answer.value.px, -90.0, 90.0);
                    if (angle == 0) { return "normal"; }
                    return "oblique " + number_text(static_cast<float>(angle)) + "deg";
                }
            }
            return words;
        }
        // 4b''. A FONT WIDTH IS A PERCENTAGE (CSS Fonts 4 §2.3): a keyword is
        //     the one it names, and a calc() folds and stops at zero
        //     (font-width-computed).
        if (property == "font-width" || property == "font-stretch") {
            const std::string_view given = trim(text, html_whitespace);
            if (const auto named = style::css::font_width_percentage(given)) {
                return std::string{*named};
            }
            const style::css::math_answer answer = style::css::evaluate_math(given, {});
            if (answer.outcome == style::css::math_outcome::resolved && answer.value.has_percent &&
                answer.value.px == 0) {
                return number_text(static_cast<float>(std::max(answer.value.percent, 0.0))) + "%";
            }
            return collapse_keyword(text);
        }
        // 4c. AN OVERFLOW AXIS BESIDE A SCROLLING ONE: CSS Overflow 3 §3.1 -
        //     when either axis is neither `visible` nor `clip`, `visible`
        //     computes to `auto`. `clip` stays `clip`: it may sit beside a
        //     scrolling axis since csswg-drafts#7714 (overflow-computed).
        if (property == "overflow-x" || property == "overflow-y") {
            const std::string own = collapse_keyword(text);
            std::string other =
                collapse_keyword(declared(property == "overflow-x" ? "overflow-y" : "overflow-x"));
            if (other.empty()) { other = "visible"; }
            if (other != "visible" && other != "clip" && (own.empty() || own == "visible")) {
                return "auto";
            }
            return own;
        }
        // 5. Everything else is a keyword or a list, and its computed value IS
        //    its specified text - lowercased, EXCEPT where the value names
        //    something: a <custom-ident> is case-sensitive (CSS Values 4
        //    §3.2), so `counter-reset: myCounter` and `animation-name: Both`
        //    read back as written. The grammar lowercased every keyword of
        //    these already.
        static constexpr std::string_view names_something[] = {"counter-increment",
                                                               "counter-reset",
                                                               "counter-set",
                                                               "animation-name",
                                                               "page",
                                                               "view-transition-name",
                                                               "view-transition-class",
                                                               "container-name",
                                                               "anchor-name",
                                                               "position-anchor",
                                                               "anchor-scope",
                                                               "font-palette",
                                                               "scroll-timeline-name",
                                                               "view-timeline-name",
                                                               "timeline-scope",
                                                               "will-change",
                                                               "list-style-type"};
        return collapse_keyword(text, ascii_iequals_any(property, names_something));
    };

    // `currentcolor` IS NOT A COLOUR. It is the initial value of all six border
    // and outline colours and it resolves to this element's own `color`, which is
    // what Chrome reports for a border nothing coloured. Computed first so the
    // loop below has it, and guarded against a `color` that is itself the keyword
    // - a declaration that cannot mean anything and has no answer but the initial.
    const std::string current_color = [&] {
        const std::string own = value_of("color");
        if (own.empty() || ascii_iequals(own, "currentcolor")) {
            return std::string{"rgb(0, 0, 0)"};
        }
        return own;
    }();

    // EVERY LONGHAND THIS ENGINE KNOWS ABOUT, whether or not anything declared
    // it. That is the whole difference between an object a page can read and the
    // handful of names this used to publish: computed-testcommon.js asks
    // `property in getComputedStyle(target)` before every single computed-value
    // test, and a property the cascade never reached failed there rather than on
    // its value.
    //
    // A SHORTHAND IS SKIPPED, AND THE TABLE IS ASKED WHICH ONE IS. CSSOM §6.7
    // says a computed style's INDEXED properties are the supported LONGHANDS,
    // and css/cssom's getComputedStyle-getter-v-properties asserts both halves
    // of that at once: `'border' in style` is true and `Array.from(style)` does
    // not contain it. serialize-all-longhands asserts the consequence - every
    // enumerated name must serialise to something, and a shorthand here would
    // serialise to nothing, because reconstructing one means deciding how each
    // engine rebuilds it.
    //
    // THIS FILE USED TO KEEP ITS OWN LIST, on the grounds that the table was not
    // this file's to change. The table has a `shorthand` bit now, and the two
    // had already drifted: `border-top`, `border-right`, `border-bottom` and
    // `border-left` are shorthands there and were missing from the list, so all
    // four were enumerated as longhands - which is the exact assertion
    // getComputedStyle-getter-v-properties makes about each of them by name.
    // A LOGICAL LONGHAND IS THE PHYSICAL ONE IT MAPS TO on this element (CSS
    // Logical 1 §4): the cascade stored `margin-block-start` as `margin-top`
    // under horizontal-tb, so that is where its value - used or computed,
    // by the physical property's own rule - is read from.
    const std::string writing_mode{declared("writing-mode")};
    const std::string direction{declared("direction")};
    const auto physical_of = [&](std::string_view property) -> std::string {
        return style::css::physical_property_of(property, writing_mode, direction);
    };
    for (const style::css::property_syntax & p : style::css::known_properties()) {
        if (p.shorthand) { continue; }
        const std::string physical = physical_of(p.name);
        std::string text = value_of(physical.empty() ? p.name : physical);
        // AN INITIAL VALUE THAT IS NOT YET A COMPUTED ONE goes through the
        // rules a declared one would: `normal` word spacing is `0px`, and a
        // box's `50% 50%` origins are the pixels they resolve to (CSS Text 4
        // §7.1, CSS Transforms 1 §6).
        if (text.empty() &&
            ascii_iequals_any(p.name, {"word-spacing", "transform-origin", "perspective-origin"})) {
            as_initial = &p;
            text = value_of(p.name);
            as_initial = nullptr;
        }
        if (text.empty()) { text = std::string{p.initial}; }
        // `legacy` ALONE IS THE PARENT'S LEGACY VALUE, and `normal` when the
        // parent's is not one (CSS Box Alignment 3 §6.1) - which is how a
        // `legacy center` reaches the children of the box that says it.
        if (p.name == "justify-items" && text == "legacy") {
            text = "normal";
            for (std::size_t up = at.pseudo ? 0 : 1; up < at.chain.size(); ++up) {
                const std::string above =
                    collapse_keyword(declared_on(at.chain[up], "justify-items"));
                if (above.empty() || above == "legacy") { continue; }
                if (above.starts_with("legacy ")) { text = above; }
                break;
            }
        }
        if (ascii_iequals(text, "currentcolor")) { text = current_color; }
        // caret-color's initial `auto` is currentcolor as well (CSS UI 4 §6.2.1).
        if (p.name == "caret-color" && ascii_iequals(text, "auto")) { text = current_color; }
        // ...and inside a shadow list, where it is one shadow's colour.
        if (p.name == "box-shadow" || p.name == "text-shadow") {
            for (std::size_t at = text.find("currentcolor"); at != std::string::npos;
                 at = text.find("currentcolor", at + current_color.size())) {
                text.replace(at, 12, current_color);
            }
        }
        answers.emplace_back(std::string{p.name}, std::move(text));
    }
    // SORTED, WHICH THE TABLE IS NOT. css/cssom's getComputedStyle-property-order
    // requires the indexed properties to come out in lexicographic order;
    // style/css/properties.hpp is written box-model-first because that is the
    // order a person reads it in. Sorting here keeps both true, and there are no
    // vendor-prefixed names in the table, so plain byte order is the whole rule.
    std::ranges::sort(answers, [](const auto & a, const auto & b) { return a.first < b.first; });
    const std::size_t longhand_count = answers.size();

    // THE SHORTHANDS, PRESENT BUT NOT INDEXED. CSSOM gives a computed style an
    // attribute for every SUPPORTED property, shorthands included, and
    // getComputedStyle-getter-v-properties asserts exactly that pair: `'border'
    // in style` is true while `Array.from(style)` does not contain it.
    //
    // Assembled from the longhands wherever the serialisation is a list. §6.7's
    // rule drops a trailing value that repeats the one two places before it, so
    // an ordinary <div>'s `margin` is `0px` rather than `0px 0px 0px 0px`.
    //
    // THE REST FOLD THE WAY `el.style` DOES: the longhands' computed values
    // into a declaration block, read back as the shorthand through
    // shorthands.cpp's one fold - `font` in CSS Fonts 4 §3.1's canonical form
    // (font-computed), `animation` in CSS Animations 1 §5.9's, `white-space`
    // as CSS Text 4's keyword when its longhands spell one. A shape that
    // table keeps whole - `background`, `transition`, `text-decoration` - is
    // "", which CSSOM says of a shorthand that cannot be represented.
    const auto longhand = [&answers, longhand_count](std::string_view name) -> std::string {
        for (std::size_t i = 0; i < longhand_count; ++i) {
            if (std::string_view{answers[i].first} == name) { return answers[i].second; }
        }
        return {};
    };
    const auto collapse = [](const std::array<std::string, 4> & c) -> std::string {
        if (c[0].empty() || c[1].empty() || c[2].empty() || c[3].empty()) { return {}; }
        if (c[3] != c[1]) { return c[0] + " " + c[1] + " " + c[2] + " " + c[3]; }
        if (c[2] != c[0]) { return c[0] + " " + c[1] + " " + c[2]; }
        if (c[1] != c[0]) { return c[0] + " " + c[1]; }
        return c[0];
    };
    const auto sides = [&longhand, &collapse](std::string_view t, std::string_view r,
                                              std::string_view b, std::string_view l) {
        return collapse({longhand(t), longhand(r), longhand(b), longhand(l)});
    };
    // Two components of DIFFERENT properties rather than two sides, so the only
    // collapse is the whole-value one: `gap: normal` when both axes are normal.
    const auto both = [&longhand](std::string_view a, std::string_view b) -> std::string {
        const std::string first = longhand(a);
        const std::string second = longhand(b);
        if (first.empty() || second.empty()) { return {}; }
        return first == second ? first : first + " " + second;
    };
    std::vector<std::pair<std::string, std::string>> shorthands;
    for (const style::css::property_syntax & p : style::css::known_properties()) {
        if (!p.shorthand) { continue; }
        std::string text;
        if (p.name == "margin") {
            text = sides("margin-top", "margin-right", "margin-bottom", "margin-left");
        } else if (p.name == "padding") {
            text = sides("padding-top", "padding-right", "padding-bottom", "padding-left");
        } else if (p.name == "inset") {
            text = sides("top", "right", "bottom", "left");
        } else if (p.name == "border-width") {
            text = sides("border-top-width", "border-right-width", "border-bottom-width",
                         "border-left-width");
        } else if (p.name == "border-style") {
            text = sides("border-top-style", "border-right-style", "border-bottom-style",
                         "border-left-style");
        } else if (p.name == "border-color") {
            text = sides("border-top-color", "border-right-color", "border-bottom-color",
                         "border-left-color");
        } else if (p.name == "border-radius") {
            // CORNERS, clockwise from the top left - not the top/right/bottom/left
            // of the edge shorthands, though the collapsing rule is the same one.
            // EACH CORNER IS A PAIR: a horizontal radius and a vertical one,
            // written `h v` in the longhand and as `h h h h / v v v v` in the
            // shorthand, the slash and its half omitted when the two lists agree
            // (getComputedStyle-border-radius-001 and -003).
            std::array<std::string, 4> h;
            std::array<std::string, 4> v;
            const std::array<std::string_view, 4> corners{
                "border-top-left-radius", "border-top-right-radius", "border-bottom-right-radius",
                "border-bottom-left-radius"};
            for (std::size_t i = 0; i < 4; ++i) {
                const std::string both = longhand(corners[i]);
                // The split is at the one top-level space: a `calc(25% + 10px)`
                // has spaces of its own and keeps them.
                int depth = 0;
                std::size_t split = std::string::npos;
                for (std::size_t k = 0; k < both.size(); ++k) {
                    if (both[k] == '(') { ++depth; }
                    if (both[k] == ')') { --depth; }
                    if (both[k] == ' ' && depth == 0) {
                        split = k;
                        break;
                    }
                }
                h[i] = split == std::string::npos ? both : both.substr(0, split);
                v[i] = split == std::string::npos ? both : both.substr(split + 1);
            }
            text = collapse(h);
            if (!text.empty() && h != v) { text += " / " + collapse(v); }
        } else if (p.name == "gap") {
            text = both("row-gap", "column-gap");
        } else if (p.name == "overflow") {
            text = both("overflow-x", "overflow-y");
        } else if (p.name == "flex-flow") {
            text = both("flex-direction", "flex-wrap");
        } else if (p.name == "flex") {
            // THREE COMPONENTS THAT NEVER COLLAPSE: `flex: 0 1 auto` is the
            // initial value written out, and dropping a repeat would change
            // which component the survivors are.
            const std::string grow = longhand("flex-grow");
            const std::string shrink = longhand("flex-shrink");
            const std::string basis = longhand("flex-basis");
            if (!grow.empty() && !shrink.empty() && !basis.empty()) {
                text = grow + " " + shrink + " " + basis;
            }
        } else if (p.name != "all") {
            style::css::declaration_block block;
            for (const std::string_view longhand_name : style::css::longhands_of(p.name)) {
                std::string held = longhand(longhand_name);
                // A decoration in the text's own colour is `currentcolor`,
                // which the shorthand leaves out as it does any initial part.
                if (p.name == "text-decoration" && longhand_name == "text-decoration-color" &&
                    held == current_color) {
                    held = "currentcolor";
                }
                if (held.empty()) {
                    block.clear();
                    break;
                }
                block.push_back({std::string{longhand_name}, std::move(held), false});
            }
            if (!block.empty()) { text = style::css::declaration_value(block, p.name); }
        }
        shorthands.emplace_back(std::string{p.name}, std::move(text));
    }
    answers.insert(answers.end(), std::make_move_iterator(shorthands.begin()),
                   std::make_move_iterator(shorthands.end()));

    // AND EVERY PROPERTY THE ELEMENT HAS that the table has never heard of -
    // a CUSTOM property above all, which no fixed table can enumerate: its
    // own, the ones it inherited, and the registered ones the cascade gave it
    // an initial value for. Indexed after the longhands (object.cpp), which
    // is where cssstyledeclaration-custom-properties and
    // -registered-custom-properties look for them.
    if (styles_ != nullptr) {
        const auto found = styles_->find(style::engine::key_of(id));
        if (found != styles_->end() && found->second) {
            const auto add = [&](const style::declaration_list & list) {
                for (const style::declaration & d : list) {
                    const std::string_view name = atoms_->text(d.property);
                    if (style::css::find_property(name) != nullptr) { continue; }
                    const auto seen =
                        std::find_if(answers.begin(), answers.end(), [name](const auto & e) {
                            return std::string_view{e.first} == name;
                        });
                    if (seen != answers.end()) { continue; }
                    std::string text = value_of(name);
                    if (text.empty()) { continue; }
                    answers.emplace_back(std::string{name}, std::move(text));
                }
            };
            add(found->second->declarations);
            if (found->second->inherited) { add(found->second->inherited->declarations); }
        }
    }
    // ...and an ANIMATED custom property nothing declared, which the walk
    // above cannot see because it walks declarations.
    for (const auto & [name, text] : animated) {
        if (style::css::find_property(name) != nullptr) { continue; }
        const auto seen = std::find_if(answers.begin(), answers.end(),
                                       [&name](const auto & e) { return e.first == name; });
        if (seen == answers.end()) { answers.emplace_back(name, text); }
    }
    return answers;
}

} // namespace ctbrowser::shell
