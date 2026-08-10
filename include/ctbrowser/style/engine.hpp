#pragma once
#include <algorithm>
#include <boost/container/small_vector.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>

#include <ctbrowser/style/computed.hpp>
#include <ctbrowser/style/css/parser.hpp>
#include <ctbrowser/style/css/substitute.hpp>
#include <ctbrowser/style/selector.hpp>

// Style resolution.
//
// The shape of the work is different from the previous engine's, not just the speed. the previous
// engine asked for ONE PROPERTY at a time and rescanned the sheet for each: layout would ask for
// `display`, then `width`, then `margin`, then `color`, and every one of those walked every rule.
// This resolves an element ONCE, producing its whole computed style, and layout then reads
// properties out of a small vector.
//
// Matching is a pure function of (document snapshot, element) - it writes
// nothing shared except the intern table - which is what lets it run across
// the scheduler with no synchronisation on the hot path.

namespace ctbrowser::style {

using ctbrowser::node_id;

// What matching needs to know about an element. Gathered once per element
// rather than re-derived per candidate rule.
struct element_facts {
    atom tag;
    atom id;
    boost::container::small_vector<atom, 4> classes;
    std::uint32_t states = 0;
    // `:root`. A position fact rather than a name one, and the cheapest of them -
    // one parent lookup, no sibling walk - which is why it lands before the rest.
    bool is_root = false;
    // `:empty` - no element children and no non-whitespace text. A property of the
    // element's own children, so it is answered where they are already being walked.
    bool is_empty = false;
    // WHERE THIS ELEMENT SITS AMONG ITS SIBLINGS, one-based, as `:nth-child` counts.
    // The totals are what `:last-child` and `:nth-last-child` need, and they cannot
    // come from the traversal - it has only seen the earlier siblings - so they are
    // counted ONCE when the level is entered rather than per element, which is the
    // difference between O(n) and O(n^2) on a wide level.
    std::uint32_t sibling_index = 0;
    std::uint32_t sibling_count = 0;
    // The same, counting only siblings with the SAME TAG, for the `-of-type` family.
    std::uint32_t type_index = 0;
    std::uint32_t type_count = 0;
    // `:disabled` / `:enabled`, from the `disabled` ATTRIBUTE. Only elements that can
    // carry it are `:enabled` at all - `:enabled` is false of a <div>, not true.
    //
    // NOT MODELLED: a disabled <fieldset> or <optgroup> disables its descendants, so
    // a control inside one is `:disabled` without the attribute of its own. That
    // needs an inherited flag, which the cascade rung is already building.
    bool can_be_disabled = false;
    bool is_disabled = false;
    // `:checked`, from the `checked` attribute OR the live control state. The
    // attribute is the initial render; the state bit is what a click would set, and
    // nothing sets it yet - so this is right on load and stale after an interaction.
    bool is_checked = false;
    // `:link` / `:any-link` - an <a>, <area> or <link> with an href.
    bool is_link = false;
};

// One element the traversal has already reached, kept so that matching can ask
// about it without going back to the tree.
//
// THE POINT OF STORING THESE. `matches` used to call `facts_of` for every ancestor
// of every candidate rule, and facts_of interns the element's id and every one of
// its classes - each intern taking a shared_mutex. For an element twelve deep with
// four classes and forty candidate rules that is up to 2,400 lock acquisitions to
// answer questions the traversal had already answered on its way down. The DFS
// visits exactly the chain matching needs, so it keeps it.
//
// It is also the only way to answer `+` and `~` at all: a sibling combinator needs
// the FACTS of a previous sibling, and there is no previous-sibling link in the
// document to walk back along.
struct visited_element {
    node_id node;
    element_facts facts;
};

using style_map = flat_map<std::uint64_t, computed_style_ptr>;

class engine {
public:
    explicit engine(atom_table & atoms) : atoms_(&atoms) {}

    // Interactive state, matching ctcss's pseudo_state bits so a compiled
    // selector's requirement and an element's actual state are the same
    // vocabulary.
    static constexpr std::uint32_t state_hover = style::state_hover;
    static constexpr std::uint32_t state_active = style::state_active;
    static constexpr std::uint32_t state_focus = style::state_focus;
    static constexpr std::uint32_t state_checked = style::state_checked;
    static constexpr std::uint32_t state_disabled = style::state_disabled;

    // Set or clear one element's interactive bits. Returns whether anything
    // changed, so a caller can skip re-resolving when a mouse move lands on the
    // same element it was already on - which is most mouse moves.
    //
    // State lives HERE rather than on the node. It is a style input, not
    // document content, and putting it on the node is exactly what left the previous engine's
    // node struct carrying UI caches that layout and paint both had opinions
    // about.
    bool set_state(node_id id, std::uint32_t bits, bool on);

    [[nodiscard]] std::uint32_t state_of(node_id id) const;

    void clear_states() { states_.clear(); }

    // What a page's @font-face rules asked for: a family name and the file it
    // should come from. The cascade has no opinion about these - they are a
    // resource list - so they are collected rather than matched.
    struct page_font {
        std::string family;
        std::string source; // the url(), unquoted
        bool bold = false;
        bool italic = false;
    };
    [[nodiscard]] const std::vector<page_font> & page_fonts() const noexcept { return fonts_; }

    // origin 0 = user agent, 1 = author. Author wins ties, per the cascade.
    void add_sheet(std::string_view css, std::uint8_t origin = 1);

    // SHORTHAND EXPANSION. `margin: 1px 2px` becomes four longhand
    // declarations, emitted in place of the shorthand.
    //
    // Expanding HERE rather than where the property is read is what makes the
    // cascade come out right: the four carry the shorthand's source order, so
    // a `padding-left` written after it sorts later and wins, and one written
    // BEFORE it is overwritten - which is what CSS says and what reading
    // "shorthand, then longhand if present" gets backwards.
    //
    // Before this, only the shorthand was read at all: `summary { padding-left:
    // 18px }` and `ul { padding-left: 40px }` were in the UA sheet and did
    // nothing, so a disclosure triangle was drawn on top of its own label and
    // list markers sat outside the page.
    // Split a value on top-level whitespace. `at most four` because that is the
    // longest side list; `border` asks for three and takes them in any order.
    [[nodiscard]] static std::vector<std::string_view> value_parts(std::string_view value,
                                                                   std::size_t limit) {
        std::vector<std::string_view> parts;
        std::size_t at = 0;
        while (at < value.size() && parts.size() < limit) {
            const std::size_t begin = value.find_first_not_of(" \t\n\r\f", at);
            if (begin == std::string_view::npos) { break; }
            const std::size_t end = value.find_first_of(" \t\n\r\f", begin);
            parts.push_back(value.substr(
                begin, end == std::string_view::npos ? std::string_view::npos : end - begin));
            at = end == std::string_view::npos ? value.size() : end;
        }
        return parts;
    }

    // Is this part of a `border` shorthand a STYLE keyword? The `border` grammar is
    // `<width> || <style> || <color>` in ANY order, so its parts are classified by
    // what they are rather than by where they sit - unlike the side lists, which are
    // positional.
    [[nodiscard]] static bool is_border_style(std::string_view part) {
        for (const std::string_view name : {"none", "hidden", "dotted", "dashed", "solid", "double",
                                            "groove", "ridge", "inset", "outset"}) {
            if (ascii_iequals(part, name)) { return true; }
        }
        return false;
    }
    [[nodiscard]] static bool is_border_width(std::string_view part) {
        if (ascii_iequals(part, "thin") || ascii_iequals(part, "medium") ||
            ascii_iequals(part, "thick")) {
            return true;
        }
        return !part.empty() &&
               (part.front() == '.' || part.front() == '-' || part.front() == '+' ||
                (part.front() >= '0' && part.front() <= '9'));
    }

    [[nodiscard]] static std::vector<std::pair<std::string_view, std::string_view>>
    expand_shorthand(std::string_view property, std::string_view value) {
        // `border` FIRST, because it is the one Bootstrap actually writes - 34 times -
        // and it produced NOTHING before: paint reads `border-width` and
        // `border-color`, and the shorthand set neither, so every card, input, table
        // and button border was invisible.
        //
        // It could not be expanded at all until var() resolved: `border:
        // var(--bs-border-width) solid var(--bs-border-color)` has an unknowable
        // component count before substitution, which is why this now runs at cascade
        // time rather than when a rule is recorded.
        if (property == "border") {
            const std::vector<std::string_view> parts = value_parts(value, 3);
            if (parts.empty()) { return {}; }
            std::string_view width, style, colour;
            for (const std::string_view part : parts) {
                if (style.empty() && is_border_style(part)) {
                    style = part;
                } else if (width.empty() && is_border_width(part)) {
                    width = part;
                } else if (colour.empty()) {
                    colour = part;
                }
            }
            // A shorthand sets every longhand it governs, including the ones it did not
            // mention - so an omitted part becomes its initial value rather than being
            // left alone. That is what makes `border: 0` reset a style set elsewhere.
            std::vector<std::pair<std::string_view, std::string_view>> out;
            out.emplace_back("border-width", width.empty() ? "medium" : width);
            out.emplace_back("border-style", style.empty() ? "none" : style);
            out.emplace_back("border-color", colour.empty() ? "currentcolor" : colour);
            return out;
        }
        if (property != "margin" && property != "padding") { return {}; }
        const std::vector<std::string_view> parts = value_parts(value, 4);
        if (parts.empty()) { return {}; }
        // 1 value: all four. 2: vertical, horizontal. 3: top, horizontal,
        // bottom. 4: top, right, bottom, left.
        const std::string_view top = parts[0];
        const std::string_view right = parts.size() > 1 ? parts[1] : top;
        const std::string_view bottom = parts.size() > 2 ? parts[2] : top;
        const std::string_view left = parts.size() > 3 ? parts[3] : right;
        if (property == "margin") {
            return {{"margin-top", top},
                    {"margin-right", right},
                    {"margin-bottom", bottom},
                    {"margin-left", left}};
        }
        return {{"padding-top", top},
                {"padding-right", right},
                {"padding-bottom", bottom},
                {"padding-left", left}};
    }

    [[nodiscard]] std::size_t rule_count() const noexcept { return index_.rule_count(); }
    // How many COMPILED SELECTORS are retained. Observable because the count is a
    // correctness property, not just a size: it must be one per selector that can
    // match, and the front end this replaced compiled one per DECLARATION and kept
    // the dead ones. On Bootstrap that was 6,289 where 2,965 exist.
    [[nodiscard]] std::size_t selector_count() const noexcept { return selectors_.size(); }
    [[nodiscard]] style_table & styles() noexcept { return table_; }

    // --- element facts -----------------------------------------------------
    [[nodiscard]] element_facts facts_of(const read_txn & txn, node_id id) const;

    // Which properties INHERIT. Not the whole CSS list - the ones a consumer in this
    // tree can produce a value for, plus custom properties, caught by the `--` prefix
    // rather than by name.
    //
    // `font-size` IS ABSENT ON PURPOSE, and it is the one real gap here. Its computed
    // value is an absolute length, so inheriting the TEXT would let `1.5em` compound
    // against each descendant's own size instead of being resolved once. box_builder
    // already resolves it correctly against the parent's px as it builds the box tree,
    // so leaving it there is right until the unit-folding rung moves that resolution
    // into the cascade - at which point this list gains it and box_builder loses a
    // parameter. The same argument covers any inherited property carrying a relative
    // unit: `letter-spacing: 0.1em` would compound, and nothing reads it.
    [[nodiscard]] static bool inherits(std::string_view property) {
        if (property.starts_with("--")) { return true; }
        static constexpr std::string_view names[] = {
            "border-collapse", "border-spacing",  "caption-side",        "color",
            "cursor",          "direction",       "empty-cells",         "font-family",
            "font-style",      "font-variant",    "font-weight",         "letter-spacing",
            "line-height",     "list-style",      "list-style-position", "list-style-type",
            "text-align",      "text-decoration", "text-indent",         "text-transform",
            "visibility",      "white-space",     "word-spacing"};
        for (const std::string_view name : names) {
            if (name == property) { return true; }
        }
        return false;
    }

    // --- the single-element path -------------------------------------------
    [[nodiscard]] computed_style_ptr resolve(const read_txn & txn, node_id node,
                                             const element_facts & self,
                                             const ancestor_filter & ancestors, std::size_t depth,
                                             const computed_style_ptr & parent) {
        // THE PARENT'S WHOLE STYLE, not just its inherited half, and the difference is
        // `inherit` itself: the keyword takes the parent's value for ANY property,
        // inherited or not, so `display: inherit` has to be able to read a property
        // that never travels on its own. The inherited half alone cannot answer that.
        const inherited_ptr & from = parent ? parent->inherited : no_inherited_;
        // Gather only the rules whose RIGHTMOST compound could possibly match.
        matches_.clear();
        collect(index_.by_id, self.id, txn, ancestors, depth);
        for (const atom c : self.classes) { collect(index_.by_class, c, txn, ancestors, depth); }
        collect(index_.by_tag, self.tag, txn, ancestors, depth);
        for (const rule & r : index_.universal) {
            if (matches(txn, ancestors, selectors_[r.selector], depth)) { matches_.push_back(r); }
        }

        // The cascade: origin, then importance, then specificity, then source
        // order. Sorting ascending and applying in order means the last write
        // to a property wins, which is exactly the rule.
        std::ranges::stable_sort(matches_, [this](const rule & a, const rule & b) {
            if (a.important != b.important) { return !a.important; }
            if (a.origin != b.origin) { return a.origin < b.origin; }
            const specificity sa = selectors_[a.selector].spec;
            const specificity sb = selectors_[b.selector].spec;
            if (sa != sb) { return sa < sb; }
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
        //   revert    treated as `unset`. Doing it properly needs the value the
        //             PREVIOUS origin would have produced, which means keeping the
        //             cascade's intermediate states rather than folding as it goes
        //
        // Before this they reached layout as the literal strings, and
        // `display: inherit` silently became `block` because parse_display mapped
        // everything it did not know to that.
        const auto put = [&out, &parent, this](const declaration & d) {
            std::string value = d.value;
            const std::string_view property = atoms_->text(d.property);
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
                value.clear();
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
        const inline_block & own = inline_style_of(txn, node);

        // TWO PASSES, and the reason is that custom properties are themselves
        // cascaded: substitution cannot run inside the fold that produces the values
        // it needs to read. So pass one applies ONLY custom properties, and pass two
        // substitutes everything else against them.
        //
        // Both passes walk the same sorted list with the same inline-style splice, so
        // priority is identical between them - and expansion happening in pass two
        // keeps source order for free: a shorthand's longhands land at the shorthand's
        // position in the fold, so a longhand written after it still wins and one
        // written before it is still overwritten. That is the property
        // test_shorthands_expand exists to pin, and it is why expansion used to happen
        // when a rule was RECORDED. It has to move here now, because a shorthand's
        // component count is unknowable before substitution:
        // `border: var(--w) solid var(--c)` cannot be split into longhands until the
        // var()s are gone.
        const auto fold = [&](const auto & apply) {
            bool spliced = false;
            for (const rule & r : matches_) {
                if (r.important && !spliced) {
                    for (const declaration & d : own.normal) { apply(d); }
                    spliced = true;
                }
                apply(declarations_[r.declaration]);
            }
            if (!spliced) {
                for (const declaration & d : own.normal) { apply(d); }
            }
            for (const declaration & d : own.important) { apply(d); }
        };

        // PASS ONE: the custom properties, stored verbatim. A custom property's value
        // is never parsed and never validated - it is a token stream that means
        // whatever the var() reading it makes of it.
        fold([&](const declaration & d) {
            if (!atoms_->text(d.property).starts_with("--")) { return; }
            put(d);
        });

        const auto lookup = [&out, &parent](atom name) -> std::optional<std::string_view> {
            for (const declaration & d : out) {
                if (d.property == name) { return std::string_view{d.value}; }
            }
            if (parent && parent->inherited) {
                for (const declaration & d : parent->inherited->declarations) {
                    if (d.property == name) { return std::string_view{d.value}; }
                }
            }
            return std::nullopt;
        };

        // PASS TWO: everything else. Substitute, then expand, then put.
        fold([&](const declaration & d) {
            const std::string_view property = atoms_->text(d.property);
            if (property.starts_with("--")) { return; }
            std::string value{d.value};
            if (css::may_have_var(value)) {
                const std::optional<std::string> done = css::substitute_var(value, lookup, *atoms_);
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
                    for (std::size_t i = 0; i < out.size(); ++i) {
                        if (out[i].property == d.property) {
                            out.erase(out.begin() + static_cast<std::ptrdiff_t>(i));
                            break;
                        }
                    }
                    return;
                }
                value = *done;
            }
            const auto expanded = expand_shorthand(property, value);
            if (expanded.empty()) {
                put(declaration{d.property, std::move(value)});
                return;
            }
            for (const auto & [name, text] : expanded) {
                put(declaration{atoms_->intern_lower(name), std::string{text}});
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

    // --- whole-document resolution ------------------------------------------
    // Sequential DFS, maintaining three things as it descends: the ancestor
    // filter, the chain of ancestors, and the siblings already seen at each depth.
    //
    // The filter is why descendant selectors are fast - a few counter updates per
    // element buy a rejection without walking. The other two are why matching asks
    // the tree nothing: `levels_[d]` holds every element visited so far at depth d
    // and `path_[d]` says which of them the current chain runs through, so an
    // ancestor's facts are an array index rather than a fresh facts_of call, and a
    // previous sibling is reachable at all.
    //
    // NON-ELEMENT NODES DO NOT OCCUPY A DEPTH: a text node between two elements is
    // not their sibling as far as `+` is concerned, and the document root is not an
    // ancestor anything can select. Both recurse at the SAME depth, which is what
    // makes `<html>` depth 0.
    void resolve_subtree(const read_txn & txn, node_id node, ancestor_filter & ancestors,
                         style_map & out, std::size_t depth = 0,
                         const computed_style_ptr & parent = {}) {
        if (txn.kind(node).value_or(node_kind::text) != node_kind::element) {
            for (const node_id child : txn.children(node)) {
                resolve_subtree(txn, child, ancestors, out, depth, parent);
            }
            return;
        }
        if (levels_.size() <= depth) { levels_.resize(depth + 1); }
        if (path_.size() <= depth) { path_.resize(depth + 1); }
        if (totals_.size() <= depth) { totals_.resize(depth + 1); }
        // APPENDED BEFORE RESOLVING, so the chain and the sibling list agree about
        // where this element is. Only earlier indices are ever read - a sibling
        // combinator looks backwards only - so being in the list already is safe.
        element_facts my_facts = facts_of(txn, node);
        my_facts.sibling_index = static_cast<std::uint32_t>(levels_[depth].size()) + 1;
        my_facts.sibling_count = totals_[depth].elements;
        my_facts.type_index = totals_[depth].next_for(my_facts.tag);
        my_facts.type_count = totals_[depth].total_for(my_facts.tag);
        levels_[depth].push_back(visited_element{node, std::move(my_facts)});
        path_[depth] = levels_[depth].size() - 1;
        const element_facts & self = levels_[depth].back().facts;

        const computed_style_ptr resolved = resolve(txn, node, self, ancestors, depth, parent);
        out[key_of(node)] = resolved;

        // The tag, id and classes are read from the stored facts rather than from
        // `self`, because resolve() may have grown levels_ and reallocated it.
        const visited_element & me = levels_[depth][path_[depth]];
        const atom my_tag = me.facts.tag;
        const atom my_id = me.facts.id;
        const boost::container::small_vector<atom, 4> my_classes = me.facts.classes;
        ancestors.push(my_tag, my_id, my_classes);
        // A FRESH SIBLING LIST for this element's children. Cleared once, before the
        // loop: the children accumulate into it as they are visited, which is
        // exactly what `~` needs, and clear() keeps the capacity so a wide document
        // stops allocating after the widest level it has seen.
        enter_level(txn, node, depth + 1);
        for (const node_id child : txn.children(node)) {
            resolve_subtree(txn, child, ancestors, out, depth + 1, resolved);
        }
        ancestors.pop(my_tag, my_id, my_classes);
    }

    [[nodiscard]] style_map resolve_all(const read_txn & txn);

    // Start a level: clear the siblings seen at that depth and count what the
    // traversal cannot know from them alone - the level's element total and its
    // per-tag totals, which `:last-child` and the `-of-type` family need.
    //
    // ONE PASS over the children, here, rather than a walk per element: doing it per
    // element would make a level of n siblings cost O(n^2), and a `<body>` with a
    // few thousand children is an ordinary page.
    //
    // Called ONLY from the element branch, for its children. Calling it from the
    // non-element branch would clear the level a text node happens to sit in and
    // lose every sibling before it.
    void enter_level(const read_txn & txn, node_id parent, std::size_t depth) {
        if (levels_.size() <= depth) { levels_.resize(depth + 1); }
        if (totals_.size() <= depth) { totals_.resize(depth + 1); }
        levels_[depth].clear();
        level_totals & t = totals_[depth];
        t.elements = 0;
        t.per_tag.clear();
        t.seen_per_tag.clear();
        for (const node_id child : txn.children(parent)) {
            if (txn.kind(child).value_or(node_kind::text) != node_kind::element) { continue; }
            ++t.elements;
            const atom tag = txn.tag(child).value_or(atom{});
            bool found = false;
            for (auto & [seen, n] : t.per_tag) {
                if (seen == tag) {
                    ++n;
                    found = true;
                    break;
                }
            }
            if (!found) { t.per_tag.emplace_back(tag, 1u); }
        }
    }

    [[nodiscard]] static constexpr std::uint64_t key_of(node_id id) noexcept { return id.key(); }

private:
    // One element's `style` attribute, split by importance.
    struct inline_block {
        declaration_list normal;
        declaration_list important;
    };

    // Parsed at most once per DISTINCT attribute text. Keyed by the text rather
    // than by the element, because a page that styles forty rows inline usually
    // writes the same declaration twice - and because a re-resolve after a
    // hover must not re-parse anything.
    [[nodiscard]] const inline_block & inline_style_of(const read_txn & txn, node_id id);
    [[nodiscard]] atom style_name() const { return atoms_->intern("style"); }

    // string_flat_map, so the lookup can be asked with the attribute's
    // string_view. The plain flat_map's hasher is not transparent, so `find`
    // took the key type - and the key here is the WHOLE `style="..."` text,
    // well past any small-string buffer, so that temporary was a heap
    // allocation per styled element per resolve. A hover re-resolves the entire
    // document (browser::resolve_styles -> resolve_all), so it was one per
    // element per interaction.
    string_flat_map<inline_block> inline_cache_;

    // `src: url("x.ttf") format("truetype")` -> `x.ttf`. Only the first url is
    // taken: this loads one file per face, and a list of alternatives is about
    // formats a browser might not support rather than different fonts.
    [[nodiscard]] static std::string_view unquoted(std::string_view text);

    [[nodiscard]] static std::string_view url_of(std::string_view src);

    std::vector<page_font> fonts_;

    [[nodiscard]] atom id_name() const { return atoms_->intern("id"); }
    [[nodiscard]] atom class_name() const { return atoms_->intern("class"); }

    void split_classes(std::string_view list, boost::container::small_vector<atom, 4> & out) const;

    template <typename Map>
    void collect(const Map & bucket, atom key, const read_txn & txn,
                 const ancestor_filter & ancestors, std::size_t depth) {
        if (!key) { return; }
        const auto it = bucket.find(key.id);
        if (it == bucket.end()) { return; }
        for (const rule & r : it->second) {
            if (matches(txn, ancestors, selectors_[r.selector], depth)) { matches_.push_back(r); }
        }
    }

    // Everything a compound can require, of the element at (depth, index).
    //
    // THE CURSOR RATHER THAN A NODE, because a nested selector list - the argument
    // of `:not()` or `:is()` - runs the whole matcher again from this same position,
    // and that needs the position rather than just the element. Every element this
    // is ever asked about is one the traversal has visited, so it is always in
    // `levels_`.
    //
    // Attributes are read on demand rather than gathered into element_facts: there
    // are arbitrarily many and almost none are ever asked about.
    [[nodiscard]] bool compound_matches(const read_txn & txn, const ancestor_filter & ancestors,
                                        const compound & c, std::size_t depth,
                                        std::size_t index) const;

    // Right to left, which is the whole reason bucketing works: the rightmost
    // compound is checked first and fails immediately for most candidates.
    [[nodiscard]] bool matches(const read_txn & txn, const ancestor_filter & ancestors,
                               const compiled_selector & sel, std::size_t depth) const {
        return matches_from(txn, ancestors, sel, depth, path_[depth]);
    }

    // The walk, from an arbitrary cursor - which is what lets a nested selector list
    // re-enter it. `:not(.a > .b)` has the same subject as the compound it sits in,
    // so its combinators walk from that same position.
    //
    // The CURSOR is a (depth, index) pair into `levels_` rather than a node_id,
    // because a sibling combinator moves sideways: after `.a + .b` has matched `.b`,
    // the next compound is measured from `.a`, at the same depth and a lower index.
    [[nodiscard]] bool matches_from(const read_txn & txn, const ancestor_filter & ancestors,
                                    const compiled_selector & sel, std::size_t at_depth,
                                    std::size_t at_index) const {
        if (!compound_matches(txn, ancestors, sel.parts.front(), at_depth, at_index)) {
            return false;
        }
        for (std::size_t i = 1; i < sel.parts.size(); ++i) {
            const compound & want = sel.parts[i];
            switch (sel.links[i - 1]) {
            case combinator::child: {
                if (at_depth == 0) { return false; }
                --at_depth;
                at_index = path_[at_depth];
                if (!compound_matches(txn, ancestors, want, at_depth, at_index)) { return false; }
                break;
            }
            case combinator::descendant: {
                // The filter's whole job: reject a descendant selector before walking
                // a single ancestor. It has no false negatives, so a `false` here is
                // conclusive.
                //
                // CONSULTED FOR DESCENDANT ONLY, and that is not an oversight. The
                // filter holds the SUBJECT's ancestors; a sibling is not one of them,
                // so asking it about a sibling combinator would be a false NEGATIVE -
                // it would reject a selector that does match, and the page would
                // render wrong. The saturating counters exist to prevent exactly that
                // class of error from the other direction.
                if (!ancestors.may_match(want)) { return false; }
                bool found = false;
                for (std::size_t up = at_depth; up-- > 0;) {
                    if (compound_matches(txn, ancestors, want, up, path_[up])) {
                        at_depth = up;
                        at_index = path_[up];
                        found = true;
                        break;
                    }
                }
                if (!found) { return false; }
                break;
            }
            case combinator::next_sibling: {
                if (at_index == 0) { return false; } // nothing precedes it
                --at_index;
                if (!compound_matches(txn, ancestors, want, at_depth, at_index)) { return false; }
                break;
            }
            case combinator::subsequent_sibling: {
                bool found = false;
                for (std::size_t k = at_index; k-- > 0;) {
                    if (compound_matches(txn, ancestors, want, at_depth, k)) {
                        at_index = k;
                        found = true;
                        break;
                    }
                }
                if (!found) { return false; }
                break;
            }
            case combinator::none: return false; // only ever the rightmost compound
            }
        }
        return true;
    }

    // Sparse on purpose: at most a handful of elements are hovered, pressed or
    // focused at once, so a per-node field would be megabytes of zeroes.
    flat_map<std::uint64_t, std::uint32_t> states_;
    atom_table * atoms_;
    std::vector<compiled_selector> selectors_;
    std::vector<declaration> declarations_;
    rule_index index_;
    style_table table_;
    std::vector<rule> matches_; // reused across elements, so no per-element allocation
    // The traversal's memory of where it is: `levels_[d]` is every element visited
    // so far at depth d, and `path_[d]` which of them the current chain runs
    // through. Members rather than parameters because resolve_subtree recurses and
    // matching reads them from the bottom of that recursion. Both are reused across
    // elements and across documents, so a steady-state resolve allocates nothing.
    std::vector<std::vector<visited_element>> levels_;
    std::vector<std::size_t> path_;
    // Per level, the totals the traversal cannot know from what it has already seen:
    // how many element children the level has in all, and how many of each tag.
    // Counted once when the level is entered.
    struct level_totals {
        std::uint32_t elements = 0;
        boost::container::small_vector<std::pair<atom, std::uint32_t>, 8> per_tag;
        boost::container::small_vector<std::pair<atom, std::uint32_t>, 8> seen_per_tag;

        [[nodiscard]] std::uint32_t total_for(atom tag) const {
            for (const auto & [t, n] : per_tag) {
                if (t == tag) { return n; }
            }
            return 0;
        }
        [[nodiscard]] std::uint32_t next_for(atom tag) {
            for (auto & [t, n] : seen_per_tag) {
                if (t == tag) { return ++n; }
            }
            seen_per_tag.emplace_back(tag, 1u);
            return 1;
        }
    };
    std::vector<level_totals> totals_;
    // A null inherited pointer to hand out at the root, so `from` can be a reference.
    const inherited_ptr no_inherited_{};
};

} // namespace ctbrowser::style
