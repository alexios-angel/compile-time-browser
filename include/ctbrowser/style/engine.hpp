#pragma once
#include <algorithm>
#include <boost/container/small_vector.hpp>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>

#include <ctbrowser/style/computed.hpp>
#include <ctbrowser/style/css/calc.hpp>
#include <ctbrowser/style/css/media.hpp>
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

// THE GUARANTEED-INVALID VALUE, CSS Variables §3 - the initial value of every
// custom property, and what `--x: initial` sets one to.
//
// It has to be REPRESENTABLE rather than merely absent, because "defined as
// invalid" and "not defined" differ in one observable way: the first must not
// let an ancestor's value show through. A byte no stylesheet can contain is the
// cheapest representation that cannot collide with a real value - a CSS value is
// filtered for NUL at parse time (§3.3), so this can never be one.
inline constexpr std::string_view guaranteed_invalid = "\x01invalid";

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
    explicit engine(atom_table & atoms)
        : atoms_(&atoms), font_size_(atoms.intern_lower("font-size")) {}

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

    // WHAT THE MEDIA QUERIES ARE ASKED ABOUT. It lives on the engine rather than in
    // the shell because a test needs to be able to pin the viewport and
    // `prefers-reduced-motion` without a browser, and because the cascade is the thing
    // that consumes it.
    //
    // Returns whether any condition's truth actually FLIPPED. That is the whole point:
    // a resize on a page with no `@media` returns false, and the caller can then skip
    // re-resolving the cascade entirely and just re-lay-out.
    bool set_environment(const css::media_environment & env) {
        environment_ = env;
        bool flipped = false;
        for (std::size_t i = 0; i < conditions_.size(); ++i) {
            const bool now = condition_holds(i);
            if (condition_truth_[i] != now) {
                condition_truth_[i] = now;
                flipped = true;
            }
        }
        return flipped;
    }
    [[nodiscard]] const css::media_environment & environment() const noexcept {
        return environment_;
    }

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
    // A shorthand's parts, up to `limit`. The splitting itself is
    // core/algorithms.hpp's, because paint needs the same rule with commas.
    [[nodiscard]] static std::vector<std::string_view> value_parts(std::string_view value,
                                                                   std::size_t limit) {
        std::vector<std::string_view> parts = split_top_level(value, " \t\n\r\f");
        if (parts.size() > limit) { parts.resize(limit); }
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

    // A property NAME that has to outlive this call. expand_shorthand returns
    // views, and every other name it returns is a string literal with static
    // storage; the four per-side border groups are the only ones built at
    // runtime, so they are interned into a set that lives as long as the process
    // - twelve strings, once.
    [[nodiscard]] static std::string_view intern_side(const std::string & name) {
        static std::vector<std::unique_ptr<const std::string>> kept;
        static std::mutex guard;
        const std::lock_guard<std::mutex> hold{guard};
        for (const auto & had : kept) {
            if (*had == name) { return *had; }
        }
        kept.push_back(std::make_unique<const std::string>(name));
        return *kept.back();
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
            const std::string_view w = width.empty() ? "medium" : width;
            const std::string_view y = style.empty() ? "none" : style;
            const std::string_view c = colour.empty() ? "currentcolor" : colour;
            out.emplace_back("border-width", w);
            out.emplace_back("border-style", y);
            out.emplace_back("border-color", c);
            // AND ALL TWELVE PER-SIDE LONGHANDS, because `border` really does set
            // them: `border: 1px solid red; border-bottom-color: blue` has to
            // leave three sides red, and it cannot if the first declaration only
            // wrote a uniform value that the second does not overwrite.
            for (const std::string_view side : {"top", "right", "bottom", "left"}) {
                const std::string prefix = "border-" + std::string{side} + "-";
                out.emplace_back(intern_side(prefix + "width"), w);
                out.emplace_back(intern_side(prefix + "style"), y);
                out.emplace_back(intern_side(prefix + "color"), c);
            }
            return out;
        }
        // `border-width`, `border-style` and `border-color` are THEMSELVES
        // shorthands over the four sides, with margin's 1-to-4-value syntax.
        // Bootstrap's `.table-bordered` is `border-width: 0 var(--bs-border-width)`
        // - no horizontal edges, a vertical one on each side - and read as a
        // single length that is `0`, so every column separator in every bordered
        // table was missing.
        for (const std::string_view which : {"width", "style", "color"}) {
            if (property != std::string("border-") + std::string{which}) { continue; }
            const std::vector<std::string_view> parts = value_parts(value, 4);
            if (parts.empty()) { return {}; }
            const std::string_view top = parts[0];
            const std::string_view right = parts.size() > 1 ? parts[1] : top;
            const std::string_view bottom = parts.size() > 2 ? parts[2] : top;
            const std::string_view left = parts.size() > 3 ? parts[3] : right;
            // The uniform property is kept as well, holding the FIRST value, so
            // the many readers that ask for it still get an answer - and every
            // one of them prefers the per-side longhand when there is one.
            return {{intern_side("border-top-" + std::string{which}), top},
                    {intern_side("border-right-" + std::string{which}), right},
                    {intern_side("border-bottom-" + std::string{which}), bottom},
                    {intern_side("border-left-" + std::string{which}), left},
                    {property, top}};
        }
        // THE PER-SIDE FORM, `border-top` and its three siblings, which is the same
        // grammar aimed at one edge. Bootstrap writes it for every divider it
        // draws - a `.card-header`'s bottom rule, a `.card-footer`'s top one, the
        // line under a navbar - and none of them appeared, because a shorthand
        // that expands to nothing sets nothing.
        //
        // Its longhands are `border-<side>-{width,style,color}`, and layout and
        // paint both read those in preference to the uniform trio - which is the
        // only honest expansion. Setting the uniform ones as well "so it draws"
        // was tried and was worse than nothing: a `border-bottom` then inset the
        // box on all four sides and drew a full ring, which cost 8 differences on
        // one fixture and 18 on another.
        for (const std::string_view side : {"top", "right", "bottom", "left"}) {
            if (property != std::string("border-") + std::string{side}) { continue; }
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
            if (width.empty()) { width = "medium"; }
            if (style.empty()) { style = "none"; }
            if (colour.empty()) { colour = "currentcolor"; }
            const std::string prefix = "border-" + std::string{side} + "-";
            return {{intern_side(prefix + "width"), width},
                    {intern_side(prefix + "style"), style},
                    {intern_side(prefix + "color"), colour}};
        }
        // `flex`, WHICH MUST BE EXPANDED RATHER THAN READ. Bootstrap's grid is built
        // on it - `.col { flex: 1 0 0 }` - and a `.flex-grow-0` utility written after
        // it has to win. A flex algorithm reading the shorthand directly would
        // reintroduce exactly the source-order bug this whole expansion mechanism
        // exists to prevent, so the longhands are produced here and flex will only
        // ever see those.
        //
        // THE SHORTHAND'S DEFAULTS ARE NOT THE LONGHANDS' INITIAL VALUES, which is
        // the part that is easy to get wrong: `flex-basis` initial is `auto`, but
        // `flex: 1` means `1 1 0%`. Flexbox 1 §7.1.1 is explicit that the omitted
        // components take these values and not the initial ones, because `flex: 1`
        // is meant to make an item flexible from nothing rather than from its
        // content.
        if (property == "flex") {
            const std::vector<std::string_view> parts = value_parts(value, 3);
            if (parts.empty()) { return {}; }
            const auto is_number = [](std::string_view part) {
                if (part.empty()) { return false; }
                std::size_t at = part.front() == '-' || part.front() == '+' ? 1 : 0;
                bool digits = false;
                for (; at < part.size(); ++at) {
                    if (part[at] >= '0' && part[at] <= '9') {
                        digits = true;
                        continue;
                    }
                    if (part[at] == '.') { continue; }
                    return false; // a unit or a `%`, so a width and not a number
                }
                return digits;
            };
            if (parts.size() == 1) {
                // The three keywords, each of which sets all three longhands.
                if (ascii_iequals(parts[0], "none")) {
                    return {{"flex-grow", "0"}, {"flex-shrink", "0"}, {"flex-basis", "auto"}};
                }
                if (ascii_iequals(parts[0], "auto")) {
                    return {{"flex-grow", "1"}, {"flex-shrink", "1"}, {"flex-basis", "auto"}};
                }
                if (ascii_iequals(parts[0], "initial")) {
                    return {{"flex-grow", "0"}, {"flex-shrink", "1"}, {"flex-basis", "auto"}};
                }
                if (is_number(parts[0])) {
                    return {{"flex-grow", parts[0]}, {"flex-shrink", "1"}, {"flex-basis", "0%"}};
                }
                return {{"flex-grow", "1"}, {"flex-shrink", "1"}, {"flex-basis", parts[0]}};
            }
            // Two or three. The first is always the grow factor; a second NUMBER is
            // the shrink factor and a second anything-else is the basis.
            const bool second_is_shrink = is_number(parts[1]);
            const std::string_view shrink = second_is_shrink ? parts[1] : "1";
            std::string_view basis = second_is_shrink ? std::string_view{"0%"} : parts[1];
            if (parts.size() > 2) { basis = parts[2]; }
            return {{"flex-grow", parts[0]}, {"flex-shrink", shrink}, {"flex-basis", basis}};
        }
        // `border-radius`, whose four values go round the box CLOCKWISE FROM THE
        // TOP LEFT - not the top/right/bottom/left of the edge shorthands, because
        // these name corners rather than sides. A two-value form is the two
        // diagonals, which has no analogue at all in `margin`.
        //
        // The elliptical `a / b` form gives horizontal radii before the slash and
        // vertical after. Only the first group is kept, which makes every corner
        // circular; Bootstrap writes no elliptical radius, and half of one is a
        // better answer than dropping the declaration. Recorded as a known
        // difference in docs/plans/bootstrap.md.
        if (property == "border-radius") {
            std::string_view circular = value;
            if (const std::size_t slash = circular.find('/'); slash != std::string_view::npos) {
                circular = circular.substr(0, slash);
            }
            const std::vector<std::string_view> parts = value_parts(circular, 4);
            if (parts.empty()) { return {}; }
            const std::string_view tl = parts[0];
            const std::string_view tr = parts.size() > 1 ? parts[1] : tl;
            const std::string_view br = parts.size() > 2 ? parts[2] : tl;
            const std::string_view bl = parts.size() > 3 ? parts[3] : tr;
            return {{"border-top-left-radius", tl},
                    {"border-top-right-radius", tr},
                    {"border-bottom-right-radius", br},
                    {"border-bottom-left-radius", bl}};
        }
        // `gap`, which is ROW then COLUMN - the opposite order to everything else
        // here, and the opposite order to how it reads. It is the one shorthand
        // whose two values are not left-to-right: `gap: 1rem 2rem` is a 1rem gap
        // BETWEEN ROWS and a 2rem one between columns, because the block axis
        // comes first in every Box Alignment shorthand. One value sets both.
        if (property == "gap") {
            const std::vector<std::string_view> parts = value_parts(value, 2);
            if (parts.empty()) { return {}; }
            const std::string_view row = parts[0];
            return {{"row-gap", row}, {"column-gap", parts.size() > 1 ? parts[1] : row}};
        }
        // `overflow` is the two physical axes, X then Y. It has to be expanded
        // in the cascade rather than interpreted beside its longhands later:
        //
        //   overflow: hidden; overflow-x: visible
        //
        // leaves Y hidden, while the reverse source order lets the shorthand
        // replace both. Keeping all three declarations and OR-ing their values
        // loses that ordering and incorrectly creates a formatting context.
        if (property == "overflow") {
            const std::vector<std::string_view> parts = split_top_level(value, " \t\n\r\f");
            if (parts.empty() || parts.size() > 2) { return {}; }
            const auto valid = [](std::string_view part) {
                for (const std::string_view keyword :
                     {"visible", "hidden", "clip", "scroll", "auto", "overlay", "inherit",
                      "initial", "unset", "revert"}) {
                    if (ascii_iequals(part, keyword)) { return true; }
                }
                return false;
            };
            if (!valid(parts[0]) || (parts.size() == 2 && !valid(parts[1]))) { return {}; }
            // CSS-wide keywords apply to the whole shorthand and cannot be
            // paired with a second component. `put()` resolves each expanded
            // longhand against the parent/initial value afterwards.
            const auto is_css_wide = [](std::string_view part) {
                return ascii_iequals(part, "inherit") || ascii_iequals(part, "initial") ||
                       ascii_iequals(part, "unset") || ascii_iequals(part, "revert");
            };
            if (parts.size() == 2 && (is_css_wide(parts[0]) || is_css_wide(parts[1]))) {
                return {};
            }
            const std::string_view y = parts.size() == 2 ? parts[1] : parts[0];
            return {{"overflow-x", parts[0]}, {"overflow-y", y}};
        }
        // `list-style` is `<type> || <position> || <image>` in any order, and the
        // only part with a consumer is the type. `none` is ambiguous between the
        // type and the image and CSS says it sets whichever is not otherwise
        // given - which for a lone `none` is both, and the type is the one that
        // matters here.
        if (property == "list-style") {
            const std::vector<std::string_view> parts = value_parts(value, 3);
            if (parts.empty()) { return {}; }
            std::string_view type = "disc";
            std::string_view position = "outside";
            for (const std::string_view part : parts) {
                if (ascii_iequals(part, "inside") || ascii_iequals(part, "outside")) {
                    position = part;
                } else if (!ascii_istarts_with(part, "url(")) {
                    type = part;
                }
            }
            return {{"list-style-type", type}, {"list-style-position", position}};
        }
        // `inset` IS the four offsets, in the side order - the one shorthand that
        // shares `margin`'s shape exactly, which is why it can share its code.
        if (property == "inset") {
            const std::vector<std::string_view> parts = value_parts(value, 4);
            if (parts.empty()) { return {}; }
            const std::string_view top = parts[0];
            const std::string_view right = parts.size() > 1 ? parts[1] : top;
            const std::string_view bottom = parts.size() > 2 ? parts[2] : top;
            const std::string_view left = parts.size() > 3 ? parts[3] : right;
            return {{"top", top}, {"right", right}, {"bottom", bottom}, {"left", left}};
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
        // `font-size` was missing from this list, and its absence was invisible for
        // as long as nothing in the cascade needed it: layout threaded the inherited
        // size through box_builder as a function parameter instead. It stopped being
        // invisible the moment `em` had to resolve, because `p { font-size: 2em }`
        // inside a 20px parent doubled 16 rather than 20.
        static constexpr std::string_view names[] = {
            "border-collapse", "border-spacing", "caption-side",    "color",
            "cursor",          "direction",      "empty-cells",     "font-family",
            "font-size",       "font-style",     "font-variant",    "font-weight",
            "letter-spacing",  "line-height",    "list-style",      "list-style-position",
            "list-style-type", "text-align",     "text-decoration", "text-indent",
            "text-transform",  "visibility",     "white-space",     "word-spacing"};
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
            if (!condition_truth_[r.condition]) { continue; }
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
                // ON A CUSTOM PROPERTY `initial` IS THE GUARANTEED-INVALID VALUE,
                // not an empty one, and the difference is the whole of Bootstrap
                // 5.3's theming layer. It writes `--bs-table-bg-type: initial` as
                // a SENTINEL that `var(--bs-table-bg-type, <fallback>)` has to
                // fall through, and `.table-striped` then overrides it with a real
                // colour. Storing an empty string instead made the var() a valid
                // EMPTY substitution, so every table cell's box-shadow lost its
                // colour and no stripe, hover or active row was ever painted.
                //
                // An empty custom property - `--bs-btn-font-family: ;` - is a
                // different thing and stays a valid empty substitution. Bootstrap
                // ships seventeen of those too, and S4a's tests pin them.
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

        // PASS ONE AND A HALF: FONT SIZE, ALONE, BEFORE ANYTHING ELSE READS IT.
        //
        // `em` means the element's own font size on every property except font-size
        // itself, where it means the parent's - so the one value everything else is
        // relative to has to be known before the general fold runs. That is not a
        // convenience: `padding: calc(.5em + 1rem)` and `font-size: 1.25em` in the
        // same rule resolve their `em` against different numbers, and a single pass
        // cannot produce both.
        //
        // Bootstrap makes this load-bearing rather than theoretical: 14 of its
        // font-size declarations are in `em`, 15 are `calc(Nrem + Nvw)` fluid type,
        // and both were previously unreadable - parse_length gave up on the `c` and
        // the element silently kept its parent's size.
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
        float own_font_size = parent_font_size;
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
            const css::length_context ctx = font_context(parent_font_size);
            fold([&](const declaration & d) {
                if (d.property != font_size_) { return; }
                std::string value{d.value};
                if (css::may_have_var(value)) {
                    const std::optional<std::string> done =
                        css::substitute_var(value, lookup, *atoms_);
                    if (!done) { return; }
                    value = *done;
                }
                // A calc that does not evaluate keeps its text here, and falls
                // through to the keyword branch below - which is right for a font
                // size, because a keyword is a real answer for one.
                if (css::may_have_calc(value)) { value = css::fold_calc(value, ctx).text; }
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
        }
        // The root's size is what every `rem` in the document resolves against, so it
        // is recorded as the tree is descended rather than looked up per element.
        if (!parent) { root_font_size_ = own_font_size; }
        const css::length_context lengths = font_context(own_font_size);

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
                const auto erase = [&out](atom property_to_erase) {
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
                    erase(d.property);
                }
            };
            const bool had_var = css::may_have_var(value);
            if (had_var) {
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
                    unset();
                    return;
                }
                value = *done;
            }
            // CALC, AFTER SUBSTITUTION AND BEFORE EXPANSION - the same ordering
            // argument as the shorthands, and for a sharper reason: 34 of
            // Bootstrap's calcs are `-1 * var(x)`, so before substitution there is
            // no arithmetic to do, and `border: calc(var(w) * 2) solid red` cannot
            // be split into longhands until its components are single tokens.
            if (css::may_have_calc(value)) {
                css::folded_value done = css::fold_calc(value, lengths);
                if (!done.ok) {
                    // A CALC THAT DOES NOT EVALUATE IS NOT A VALUE, and the
                    // declaration is invalid. WHICH KIND of invalid depends on where
                    // the value came from, which is why `had_var` is remembered: a
                    // value that went through substitution is invalid at
                    // COMPUTED-VALUE time, and §3 spells that `unset`, so it must
                    // also remove the earlier declaration it beat. One that never
                    // contained a var() is invalid at PARSE time, so the earlier
                    // declaration simply wins and this one is dropped.
                    //
                    // Bootstrap hits the first case on every `.row`:
                    // `margin-top: calc(-1 * var(--bs-gutter-y))` with a gutter of
                    // `0` is a number times a number, which is a NUMBER, and a
                    // number is not a length. Chrome reports the initial `0px`;
                    // keeping the text reported `auto`, on 24 elements of one
                    // fixture.
                    if (had_var) { unset(); }
                    return;
                }
                value = std::move(done.text);
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
            // ROOT's, and the viewport. layout/values.hpp multiplied every `rem` by
            // a hardcoded 16 and treated `vh`, `vw` and `pt` as pixels outright,
            // because a length arriving as text had nothing else to go on.
            //
            // A BARE NUMBER IS LEFT ALONE, which is the case that makes this need
            // its own function rather than a flag: `line-height: 1.5` is not 1.5px.
            const auto folded = [&](std::string text) {
                if (const auto px = css::dimension_text_to_px(text, lengths)) {
                    return css::serialize_calc(css::calc_result{*px, 0.0f, false});
                }
                return text;
            };
            const auto expanded = expand_shorthand(property, value);
            if (expanded.empty()) {
                // A substituted token stream is validated only now. If it is
                // not overflow grammar, the winning shorthand is invalid at
                // computed-value time and resets both axes; a parse-time
                // invalid declaration still leaves earlier declarations alone.
                if (had_var && property == "overflow") {
                    unset();
                    return;
                }
                put(declaration{d.property, folded(std::move(value))});
                return;
            }
            for (const auto & [name, text] : expanded) {
                put(declaration{atoms_->intern_lower(name), folded(std::string{text})});
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

    // The bases every relative length in this document resolves against. `em` is
    // the caller's, because it differs between font-size and everything else; the
    // rest are facts about the document and the window.
    [[nodiscard]] css::length_context font_context(float em_basis) const noexcept {
        css::length_context ctx;
        ctx.font_size = em_basis;
        ctx.root_font_size = root_font_size_;
        ctx.viewport_width = environment_.viewport_width;
        ctx.viewport_height = environment_.viewport_height;
        return ctx;
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
            // ONE BOOL, before any selector work. A false condition is the cheapest
            // possible rejection and it is checked first for that reason.
            if (!condition_truth_[r.condition]) { continue; }
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
    // Interned once. The font-size pre-pass compares against it per element per
    // declaration, and interning takes a shared_mutex - doing it in the loop was
    // measurably the wrong shape when the ancestor facts did the same thing.
    atom font_size_;
    // The ROOT element's computed font size, which is what every `rem` in the
    // document resolves against. Recorded as the tree is descended - the root is
    // resolved first, so by the time anything else asks, it is right. It replaces
    // a hardcoded 16 in layout/values.hpp.
    float root_font_size_ = 16.0f;
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
    // Every `@media` from every sheet, flattened into one table with parent links, plus
    // their current truth. Entry 0 is the unconditional one and is always true.
    css::media_environment environment_{};
    std::vector<css::media_condition> conditions_{css::media_condition{}};
    std::vector<bool> condition_truth_{true};

    // Truth of one condition: its own query list, ANDed with its parent's - which is
    // how a nested `@media` works without the tree being flattened at parse time.
    [[nodiscard]] bool condition_holds(std::size_t at) const {
        if (at == 0) { return true; }
        const css::media_condition & c = conditions_[at];
        if (!css::evaluate(c.queries, environment_)) { return false; }
        return c.parent == 0 || condition_holds(c.parent);
    }
    // A null inherited pointer to hand out at the root, so `from` can be a reference.
    const inherited_ptr no_inherited_{};
};

} // namespace ctbrowser::style
