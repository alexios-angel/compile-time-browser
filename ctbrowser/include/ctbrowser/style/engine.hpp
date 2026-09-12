#pragma once
#include <algorithm>
#include <array>
#include <boost/container/small_vector.hpp>
#include <charconv>
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
#include <ctbrowser/style/css/calc.hpp>
#include <ctbrowser/style/css/media.hpp>
#include <ctbrowser/style/css/parser.hpp>
#include <ctbrowser/style/css/properties.hpp>
#include <ctbrowser/style/css/substitute.hpp>
#include <ctbrowser/style/selector.hpp>

// Style resolution. An element is resolved ONCE, producing its whole computed style,
// and layout then reads properties out of a small vector.
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
// about it without going back to the tree: facts_of interns the element's id and
// every class under a shared_mutex, and the DFS visits exactly the chain matching
// needs.
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
        : atoms_(&atoms), font_size_(atoms.intern_lower("font-size")),
          line_height_(atoms.intern_lower("line-height")) {}

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
    // State lives HERE rather than on the node: it is a style input, not document
    // content.
    bool set_state(node_id id, std::uint32_t bits, bool on);

    [[nodiscard]] std::uint32_t state_of(node_id id) const;

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

    // The table every atom in this engine was interned into. Needed by anything
    // comparing two engines: an atom id is meaningless outside the table that
    // issued it, so a comparison resolves each side's atoms through its own.
    [[nodiscard]] atom_table & atoms() const noexcept { return *atoms_; }

    // origin 0 = user agent, 1 = author. Author wins ties, per the cascade.
    void add_sheet(std::string_view css, std::uint8_t origin = 1);

    // DROP EVERY RULE OF ONE ORIGIN, so the author's half can be rebuilt without
    // rebuilding the engine and re-parsing the user-agent sheet beside it.
    //
    // It filters the rule index, which is what matching consults; the compiled
    // selectors and declarations those rules pointed at STAY in their vectors and
    // are simply unreachable, because every other rule's indices point into the
    // same two and renumbering them would be the expensive half of a rebuild. A
    // page that edits its stylesheet in a loop therefore grows, and that is the
    // trade recorded rather than hidden.
    void clear_origin(std::uint8_t origin);

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
    // A `background` component that names a longhand OTHER than the colour or
    // the image: repeat, attachment, position, size, clip/origin boxes.
    [[nodiscard]] static bool is_background_keyword(std::string_view part) {
        for (const std::string_view name :
             {"none",       "repeat",      "repeat-x",    "repeat-y", "no-repeat", "space",
              "round",      "scroll",      "fixed",       "local",    "center",    "top",
              "bottom",     "left",        "right",       "cover",    "contain",   "auto",
              "border-box", "padding-box", "content-box", "text"}) {
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

    // The per-side border longhands, spelled out so expand_shorthand can return
    // views into static storage like it does for every other name.
    static constexpr std::string_view border_sides[4] = {"top", "right", "bottom", "left"};
    static constexpr std::string_view border_longhands[4][3] = {
        {"border-top-width", "border-top-style", "border-top-color"},
        {"border-right-width", "border-right-style", "border-right-color"},
        {"border-bottom-width", "border-bottom-style", "border-bottom-color"},
        {"border-left-width", "border-left-style", "border-left-color"},
    };

    [[nodiscard]] static std::vector<std::pair<std::string_view, std::string_view>>
    expand_shorthand(std::string_view property, std::string_view value) {
        // `border: var(--bs-border-width) solid var(--bs-border-color)` has an
        // unknowable component count before substitution, which is why this runs at
        // cascade time rather than when a rule is recorded.
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
            for (const auto & side : border_longhands) {
                out.emplace_back(side[0], w);
                out.emplace_back(side[1], y);
                out.emplace_back(side[2], c);
            }
            return out;
        }
        // `border-width`, `border-style` and `border-color` are THEMSELVES
        // shorthands over the four sides, with margin's 1-to-4-value syntax:
        // `border-width: 0 var(--bs-border-width)` is no horizontal edges and a
        // vertical one on each side.
        for (std::size_t which = 0; which < 3; ++which) {
            if (property != std::array{"border-width", "border-style", "border-color"}[which]) {
                continue;
            }
            const std::vector<std::string_view> parts = value_parts(value, 4);
            if (parts.empty()) { return {}; }
            const std::string_view top = parts[0];
            const std::string_view right = parts.size() > 1 ? parts[1] : top;
            const std::string_view bottom = parts.size() > 2 ? parts[2] : top;
            const std::string_view left = parts.size() > 3 ? parts[3] : right;
            // The uniform property is kept as well, holding the FIRST value, so
            // the many readers that ask for it still get an answer - and every
            // one of them prefers the per-side longhand when there is one.
            return {{border_longhands[0][which], top},
                    {border_longhands[1][which], right},
                    {border_longhands[2][which], bottom},
                    {border_longhands[3][which], left},
                    {property, top}};
        }
        // THE PER-SIDE FORM, `border-top` and its three siblings, which is the same
        // grammar aimed at one edge. Its longhands are `border-<side>-{width,style,color}`,
        // and layout and paint both read those in preference to the uniform trio. Do
        // NOT set the uniform ones as well "so it draws": a `border-bottom` then
        // insets the box on all four sides and draws a full ring.
        for (std::size_t side = 0; side < 4; ++side) {
            if (property != std::string("border-") + std::string{border_sides[side]}) { continue; }
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
            return {{border_longhands[side][0], width},
                    {border_longhands[side][1], style},
                    {border_longhands[side][2], colour}};
        }
        // `flex`, WHICH MUST BE EXPANDED RATHER THAN READ: `.col { flex: 1 0 0 }` and
        // a `.flex-grow-0` utility written after it has to win, so the longhands are
        // produced here and flex only ever sees those.
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
        // `background` is `<bg-layer>#? , <final-bg-layer>`, and the two parts
        // with a consumer are the COLOUR and the IMAGE. Every other component is
        // a keyword of some other longhand, a position or size (a number, a
        // percentage or the `/` between them), or the layer comma - so the
        // colour is whatever is left, and only the final layer may carry one.
        // An omitted colour is `transparent`: `background: url(x)` resets a
        // colour set elsewhere, as every shorthand resets what it does not name.
        if (property == "background") {
            const std::vector<std::string_view> parts = value_parts(value, 32);
            if (parts.empty()) { return {}; }
            std::string_view colour = "transparent";
            std::string_view image = "none";
            for (std::string_view part : parts) {
                // `red,` - a layer boundary glued to the part before it.
                const bool comma = !part.empty() && part.back() == ',';
                if (comma) { part.remove_suffix(1); }
                if (ascii_istarts_with(part, "url(") ||
                    part.find("gradient(") != std::string_view::npos) {
                    image = part;
                } else if (!part.empty() && part != "/" && !is_border_width(part) &&
                           !is_background_keyword(part)) {
                    colour = part;
                }
                // The colour belongs to the LAST layer only.
                if (comma) { colour = "transparent"; }
            }
            return {{"background-color", colour}, {"background-image", image}};
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

    // --- WHAT THE ENGINE ACTUALLY FILED -----------------------------------
    // Every rule this engine holds, in a deterministic order, with the parts
    // of it that nothing else exposes.
    //
    // `rule_count()` and `selector_count()` say HOW MANY; this says WHICH, and
    // the difference matters to anything comparing one engine against another.
    // A rule's origin, the condition ordinal it was remapped to, and above all
    // WHICH BUCKET it landed in are decided inside add_sheet and were readable
    // from nowhere - so a filed-in-the-wrong-bucket rule and a rule that simply
    // never matched looked identical from outside. They are not the same
    // defect: the first one silently never matches anything, and the only
    // symptom is a page that renders slightly wrong.
    //
    // ORDERED BY (source order, selector), NOT by bucket. The buckets are
    // unordered maps keyed by atom id, and an atom id is handed out in
    // first-interning order at run time - so a bucket walk is neither stable
    // across runs nor comparable across processes. Source order is a property
    // of the stylesheet and nothing else.
    //
    // For inspection and for differential comparison, not for matching: the
    // matcher reads the buckets directly, which is the whole point of having
    // them. Nothing here is on a hot path.
    struct filed_rule {
        enum class bucket : std::uint8_t {
            id,
            class_name,
            tag,
            universal
        };
        bucket where = bucket::universal;
        atom key;                   // the bucket's key; empty for universal
        std::uint32_t selector = 0; // into this engine's selector table
        specificity spec;
        atom property;
        std::string_view value;
        std::int32_t order = 0;
        std::uint32_t condition = 0;
        std::uint8_t origin = 0;
        bool important = false;
    };

    template <typename F> void for_each_rule(F && visit) const {
        std::vector<filed_rule> filed;
        filed.reserve(index_.rule_count());
        const auto take = [&](const rule & r, filed_rule::bucket where, atom key) {
            const declaration & d = declarations_[r.declaration];
            filed.push_back(filed_rule{where, key, r.selector, selectors_[r.selector].spec,
                                       d.property, d.value, r.order, r.condition, r.origin,
                                       r.important});
        };
        for (const rule & r : index_.universal) { take(r, filed_rule::bucket::universal, atom{}); }
        for (const auto & [key, rules] : index_.by_tag) {
            for (const rule & r : rules) { take(r, filed_rule::bucket::tag, atom{key}); }
        }
        for (const auto & [key, rules] : index_.by_class) {
            for (const rule & r : rules) { take(r, filed_rule::bucket::class_name, atom{key}); }
        }
        for (const auto & [key, rules] : index_.by_id) {
            for (const rule & r : rules) { take(r, filed_rule::bucket::id, atom{key}); }
        }
        std::ranges::sort(filed, [](const filed_rule & a, const filed_rule & b) {
            return a.order != b.order ? a.order < b.order : a.selector < b.selector;
        });
        for (const filed_rule & r : filed) { visit(r); }
    }
    // How many COMPILED SELECTORS are retained: one per selector that can match,
    // never one per declaration.
    [[nodiscard]] std::size_t selector_count() const noexcept { return selectors_.size(); }
    [[nodiscard]] style_table & styles() noexcept { return table_; }

    // --- element facts -----------------------------------------------------
    [[nodiscard]] element_facts facts_of(const read_txn & txn, node_id id) const;

    // Which properties INHERIT. Not the whole CSS list - the ones a consumer in this
    // tree can produce a value for, plus custom properties, caught by the `--` prefix
    // rather than by name. `font-size` inherits as the px the pre-pass in resolve()
    // computed, so a relative unit never compounds down the tree.
    [[nodiscard]] static bool inherits(std::string_view property) {
        if (property.starts_with("--")) { return true; }
        static constexpr std::string_view names[] = {
            "border-collapse", "border-spacing", "caption-side",    "color",
            "cursor",          "direction",      "empty-cells",     "font-family",
            "font-size",       "font-style",     "font-variant",    "font-weight",
            "letter-spacing",  "line-height",    "list-style",      "list-style-position",
            "list-style-type", "text-align",     "text-decoration", "text-indent",
            "text-transform",  "visibility",     "white-space",     "word-spacing",
            "writing-mode"};
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
        // Where this element sits among its siblings, for `sibling-index()` and
        // `sibling-count()`: facts the traversal already gathered for
        // `:nth-child`, handed to every math function this element folds.
        sibling_index_ = self.sibling_index;
        sibling_count_ = self.sibling_count;
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
        // written before it is still overwritten. Expansion cannot happen earlier,
        // when a rule is recorded, because a shorthand's component count is
        // unknowable before substitution: `border: var(--w) solid var(--c)` cannot be
        // split into longhands until the var()s are gone.
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
        // ...AND THE ELEMENT'S ATTRIBUTES, for `attr()`. Absent and empty are
        // different answers here too: `attr(data-x)` on an element without the
        // attribute takes its fallback, and one with `data-x=""` is `""`.
        const css::attribute_lookup attributes =
            [&txn, node, this](std::string_view name) -> std::optional<std::string> {
            const atom key = atoms_->intern(name);
            if (!txn.has_attribute(node, key)) { return std::nullopt; }
            return std::string{txn.attribute_value(node, key)};
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
            // A PERCENTAGE IN A FONT SIZE IS OF THE PARENT'S, and the evaluator
            // can be told so: `calc(50% + 1px)` folds here rather than waiting
            // for a containing block it will never be measured against.
            css::length_context ctx = font_context(parent_font_size, parent_line_height);
            ctx.percent_basis = parent_font_size;
            fold([&](const declaration & d) {
                if (d.property != font_size_) { return; }
                std::string value{d.value};
                if (css::may_have_var(value)) {
                    const std::optional<std::string> done =
                        css::substitute_var(value, lookup, *atoms_, attributes);
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
        }
        // The root's size is what every `rem` in the document resolves against, so it
        // is recorded as the tree is descended rather than looked up per element.
        if (!parent) { root_font_size_ = own_font_size; }
        // PASS ONE AND THREE QUARTERS: LINE HEIGHT, for `lh` in everything else.
        // The winning `line-height` declaration, substituted and folded against
        // the PARENT's `lh` and the element's own `em`, then resolved to px. An
        // absent declaration is the inherited text against the element's OWN
        // font size - a `1.5` is a factor and inherits as one.
        float own_line_height = line_height_px(
            parent && parent->inherited ? parent->inherited->get(line_height_) : "", own_font_size);
        // ...and a percentage in a line-height is of the element's own font size,
        // so `calc(10% / 1px)` is the number 1 here (typed_arithmetic).
        css::length_context line_height_lengths = font_context(own_font_size, parent_line_height);
        line_height_lengths.percent_basis = own_font_size;
        {
            const css::length_context & ctx = line_height_lengths;
            fold([&](const declaration & d) {
                if (d.property != line_height_) { return; }
                std::string value{d.value};
                if (css::may_have_var(value)) {
                    const std::optional<std::string> done =
                        css::substitute_var(value, lookup, *atoms_, attributes);
                    if (!done) { return; }
                    value = *done;
                }
                if (css::may_have_math(value)) { value = css::fold_math(value, ctx).text; }
                // A bare number is a factor, not pixels - which is why
                // length_text_to_px is asked only after from_chars has had its
                // turn; a percentage and `normal` are line_height_px's.
                const std::string_view text = trim(value, html_whitespace);
                float number = 0;
                const auto [ptr, ec] =
                    std::from_chars(text.data(), text.data() + text.size(), number);
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
        const css::length_context lengths = font_context(own_font_size, own_line_height);
        // ...EXCEPT IN `line-height` ITSELF, where `lh` is still the parent's -
        // `line-height: 2lh` folded against its own answer would double it -
        // and `line_height_lengths` above is what that property folds with; and
        // EXCEPT IN THE OTHER font-* PROPERTIES, where an `em` is the PARENT's
        // font size as it is in `font-size`: `font-weight: calc(1em / 1px)`
        // under a 10px parent is 10 whatever the element's own size
        // (using-font-relative-units-in-font-properties, CSS Values 4 §6.1.1).
        const css::length_context font_lengths = font_context(parent_font_size, parent_line_height);
        const auto lengths_for = [&](atom property) -> const css::length_context & {
            if (property == line_height_) { return line_height_lengths; }
            return atoms_->text(property).starts_with("font-") ? font_lengths : lengths;
        };

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
                const std::optional<std::string> done =
                    css::substitute_var(value, lookup, *atoms_, attributes);
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
            // argument as the shorthands: `-1 * var(x)` has no arithmetic to do
            // before substitution, and `border: calc(var(w) * 2) solid red` cannot
            // be split into longhands until its components are single tokens.
            if (css::may_have_math(value)) {
                // WHAT THE PROPERTY WILL TAKE, passed down, because the evaluator
                // answers with numbers and only the cascade knows whether one is a
                // value here: `opacity: calc(2 / 4)` is `0.5`; `width: calc(2 * 3)`
                // is a syntax error.
                css::folded_value done =
                    css::fold_math(value, lengths_for(d.property), css::math_context_of(property));
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
            const auto folded = [&](std::string text) {
                if (auto canonical = css::canonical_dimension_text(text, lengths_for(d.property))) {
                    return std::move(*canonical);
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
    [[nodiscard]] css::length_context font_context(float em_basis,
                                                   float lh_basis = 20.0f) const noexcept {
        css::length_context ctx;
        ctx.font_size = em_basis;
        ctx.root_font_size = root_font_size_;
        ctx.line_height = lh_basis;
        ctx.root_line_height = root_line_height_;
        ctx.viewport_width = environment_.viewport_width;
        ctx.viewport_height = environment_.viewport_height;
        ctx.sibling_index = sibling_index_;
        ctx.sibling_count = sibling_count_;
        return ctx;
    }

    // A `line-height` VALUE IN PIXELS, the way layout reads one: a number is a
    // factor on the font size, a percentage likewise, `normal` and anything
    // unreadable are 1.25 times it (layout's factor for `normal`, see
    // layout::box_builder::resolve_line_height), and a length is itself -
    // which by the time it is inherited the cascade has folded to px.
    [[nodiscard]] static float line_height_px(std::string_view text, float font_size) noexcept {
        const std::string_view value = trim(text, html_whitespace);
        if (value.empty() || ascii_iequals(value, "normal")) { return font_size * 1.25f; }
        float number = 0;
        const bool percent = value.ends_with('%');
        const char * end = value.data() + value.size() - (percent ? 1 : 0);
        const auto [ptr, ec] = std::from_chars(value.data(), end, number);
        if (ec != std::errc{}) { return font_size * 1.25f; }
        if (ptr == end) { return number * (percent ? font_size / 100.0f : font_size); }
        if (ascii_iequals(std::string_view{ptr, static_cast<std::size_t>(end - ptr)}, "px")) {
            return number;
        }
        return font_size * 1.25f;
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

    // --- selector matching, for `querySelector` -------------------------------
    //
    // EVERY ELEMENT MATCHING ONE OF `list`, in document order. The same traversal
    // resolve_all runs and the same `matches_from` a rule goes through, so a
    // selector cannot mean one thing in a stylesheet and another in a script.
    //
    // It is a WALK rather than a lookup through the rule index on purpose: the
    // index is keyed by a selector's rightmost compound and this list is not in
    // it, and a document walk is what a browser does for querySelectorAll anyway.
    //
    // `root` empty means the whole document. A GIVEN ROOT RESTRICTS THE RESULTS to
    // its descendants and nothing else - the match still sees the whole tree above
    // it, because `div.querySelectorAll("body p")` must find the paragraphs inside
    // that div whose ancestor chain runs through a body. Scoping the traversal
    // instead would silently answer a different question.
    //
    // `first_only` stops at the first match, which is what `querySelector` wants
    // and what keeps it from walking a large document to build a list of one.
    [[nodiscard]] std::vector<node_id> select(const read_txn & txn, node_id root,
                                              std::span<const compiled_selector> list,
                                              bool first_only);

    // WHETHER ONE ELEMENT MATCHES, without walking the document to find out - what
    // `matches` and `closest` ask, per call, so `select` would make them
    // O(document). Matching ONE element needs its ancestor chain and the earlier
    // siblings at each step of it, and nothing else: this builds exactly that
    // cursor and then runs the same matcher.
    [[nodiscard]] bool element_matches(const read_txn & txn, node_id node,
                                       std::span<const compiled_selector> list);

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

    // string_flat_map, so the lookup can be asked with the attribute's string_view:
    // the key is the WHOLE `style="..."` text, and a hover re-resolves the entire
    // document.
    string_flat_map<inline_block> inline_cache_;

    // `'Press Start 2P'` -> `Press Start 2P`. A family name arrives with its
    // quotes on, and registering it that way files the face under a name no
    // element can ever ask for.
    //
    // The `src` url is NOT extracted by scanning text - see add_sheet. `url(`
    // with an unquoted body is its own token and a quoted one is a function
    // plus a string, so the two spellings do not resemble each other by the
    // time a declaration is reassembled into text. Only the first url is taken:
    // this loads one file per face, and a list of alternatives is about formats
    // a browser might not support rather than about different fonts.
    [[nodiscard]] static std::string_view unquoted(std::string_view text);

    std::vector<page_font> fonts_;

    [[nodiscard]] atom id_name() const { return atoms_->intern("id"); }
    [[nodiscard]] atom class_name() const { return atoms_->intern("class"); }

public:
    // --- `:lang()` and `:dir()` -----------------------------------------------
    //
    // THE LANGUAGE OF AN ELEMENT, per HTML §3.2.6. The nearest ancestor-or-self
    // carrying a `lang` attribute wins, and an EMPTY value wins too: `lang=""`
    // means "unknown", so it stops the search rather than being skipped over.
    //
    // `xml:lang` is deliberately consulted only OUTSIDE the HTML namespace. In an
    // HTML document the parser's foreign-attribute adjustment does not run on an
    // HTML element, so `<html xml:lang="ko">` holds an attribute whose qualified
    // name is literally `xml:lang` in no namespace and which sets no language at
    // all - which is exactly what `the-lang-attribute-002.html` asserts by
    // expecting `:lang(ko)` to MISS.
    //
    // With nothing on the chain the document default applies: the pragma set by
    // `<meta http-equiv="content-language">`, which beats the HTTP header.
    //
    // COST. This walks to the root per candidate compound rather than inheriting a
    // fact down the traversal, because `:lang()` appears in no sheet in the corpora
    // and a rule carrying one is filed under its rightmost compound - so only the
    // handful of elements that already match the rest of it ever ask. If a real
    // sheet ever puts `:lang()` on a bare type selector, the fix is an inherited
    // atom in element_facts, not a cache here.
    [[nodiscard]] std::string_view language_of(const read_txn & txn, node_id node) const {
        const atom lang = atoms_->intern("lang");
        for (node_id at = node; at; at = txn.parent(at)) {
            if (txn.kind(at).value_or(node_kind::text) != node_kind::element) { break; }
            if (const attribute * a = txn.find_attribute(at, lang)) { return a->value; }
            if (txn.element_ns(at) != node_ns::html) {
                if (const attribute * a =
                        txn.find_attribute_ns(at, "http://www.w3.org/XML/1998/namespace", "lang")) {
                    return a->value;
                }
            }
        }
        return pragma_language(txn);
    }

    // The pragma-set default language. Memoised on the document VERSION, because a
    // page with `:lang()` and no `lang` attribute anywhere would otherwise rescan
    // the head once per candidate element.
    //
    // Only `<head>`'s children are scanned. The spec processes the pragma wherever
    // the meta is inserted, so a `<meta http-equiv>` in the body would count too;
    // no such page exists and scanning the whole document per miss would not be
    // worth what it buys. A value containing a comma is IGNORED rather than split -
    // HTML §4.2.5.3 says a Content-Language pragma naming more than one language
    // sets no default at all.
    [[nodiscard]] std::string_view pragma_language(const read_txn & txn) const {
        const std::uint64_t version = txn.version();
        if (pragma_language_version_ == version) { return pragma_language_; }
        pragma_language_version_ = version;
        pragma_language_.clear();
        const atom head_tag = atoms_->intern("head");
        const atom equiv = atoms_->intern("http-equiv");
        const atom content = atoms_->intern("content");
        // The root IS `<html>` - tree_builder::parse sets it as such - so `<head>`
        // is one of its children, not a grandchild.
        for (const node_id child : txn.children(txn.root())) {
            if (txn.tag(child).value_or(atom{}) != head_tag) { continue; }
            for (const node_id meta : txn.children(child)) {
                if (!ascii_iequals(txn.attribute_value(meta, equiv), "content-language")) {
                    continue;
                }
                const std::string_view value = txn.attribute_value(meta, content);
                if (value.find(',') != std::string_view::npos) { continue; }
                const std::string_view tag = first_word(value);
                // Each meta sets the pragma as it is processed, so a later one
                // replaces an earlier one - hence no early exit.
                if (!tag.empty()) { pragma_language_ = tag; }
            }
        }
        return pragma_language_;
    }

    // RFC 4647 §3.3.2 extended filtering, which is what Selectors 4 §7.2 says
    // `:lang()` compares with. It is NOT string equality and it is not a prefix
    // test either: `de` matches `de-DE`, `*-CH` matches `de-CH`, and a one-letter
    // subtag in the language is a singleton that ends the match rather than being
    // skipped past.
    [[nodiscard]] static bool language_matches(std::string_view range, std::string_view lang) {
        if (lang.empty() || range.empty()) { return false; }
        std::size_t ri = 0;
        std::size_t li = 0;
        const std::string_view first_range = next_subtag(range, ri);
        const std::string_view first_lang = next_subtag(lang, li);
        if (first_range != "*" && !ascii_iequals(first_range, first_lang)) { return false; }
        while (ri <= range.size()) {
            const std::string_view want = next_subtag(range, ri);
            if (want.empty()) { return true; }
            if (want == "*") { continue; }
            for (;;) {
                if (li > lang.size()) { return false; }
                const std::string_view have = next_subtag(lang, li);
                if (have.empty()) { return false; }
                if (ascii_iequals(want, have)) { break; }
                if (have.size() == 1) { return false; } // a singleton subtag
            }
        }
        return true;
    }

    // `:dir()`, Selectors 4 §7.1 over HTML §3.2.6's directionality. The nearest
    // ancestor-or-self with a `dir` of `ltr` or `rtl` decides; `auto` and `<bdi>`
    // resolve from the first STRONG character of the subtree's text, and the
    // default with nothing found at all is `ltr`.
    //
    // The strong-character scan classifies UTF-8 by RANGE rather than by a Unicode
    // bidi table: the right-to-left scripts are contiguous blocks, so the ranges
    // are exact for them, and everything outside is treated as left-to-right or as
    // neutral. That is the whole of the difference from a full bidi implementation
    // and it is enough for `dir=auto` on real text.
    [[nodiscard]] bool direction_is_rtl(const read_txn & txn, node_id node) const {
        const atom dir = atoms_->intern("dir");
        for (node_id at = node; at; at = txn.parent(at)) {
            if (txn.kind(at).value_or(node_kind::text) != node_kind::element) { break; }
            const std::string_view value = txn.attribute_value(at, dir);
            if (ascii_iequals(value, "rtl")) { return true; }
            if (ascii_iequals(value, "ltr")) { return false; }
            const bool bdi = txn.tag(at).value_or(atom{}) == atoms_->intern("bdi") &&
                             txn.element_ns(at) == node_ns::html;
            if (ascii_iequals(value, "auto") || bdi) {
                // Nothing strong anywhere in the subtree leaves `rtl` false, which
                // is the spec's answer too: `dir=auto` over digits alone is ltr.
                bool rtl = false;
                (void)first_strong(txn, at, rtl);
                return rtl;
            }
        }
        return false;
    }

private:
    // The first character of `node`'s text with a strong direction, depth first.
    // Returns whether one was found, so the walk can stop at it.
    [[nodiscard]] static bool first_strong(const read_txn & txn, node_id node, bool & rtl) {
        const node_kind kind = txn.kind(node).value_or(node_kind::comment);
        if (is_text_kind(kind)) { return first_strong_in(txn.text(node), rtl); }
        if (kind != node_kind::element) { return false; }
        for (const node_id child : txn.children(node)) {
            if (first_strong(txn, child, rtl)) { return true; }
        }
        return false;
    }

    // The same question of one run of UTF-8. Decoding is by lead byte: only the
    // code point's VALUE matters, and the right-to-left scripts sit in blocks that
    // a range test answers exactly.
    [[nodiscard]] static bool first_strong_in(std::string_view text, bool & rtl) {
        for (std::size_t i = 0; i < text.size();) {
            const auto lead = static_cast<unsigned char>(text[i]);
            std::size_t width = 1;
            std::uint32_t cp = lead;
            if (lead >= 0xF0) {
                width = 4;
                cp = lead & 0x07u;
            } else if (lead >= 0xE0) {
                width = 3;
                cp = lead & 0x0Fu;
            } else if (lead >= 0xC0) {
                width = 2;
                cp = lead & 0x1Fu;
            }
            if (i + width > text.size()) { return false; } // truncated: nothing strong left
            for (std::size_t k = 1; k < width; ++k) {
                cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3Fu);
            }
            i += width;
            // Hebrew, Arabic, Syriac, Thaana, NKo, Samaritan and Mandaic; then the
            // Arabic Extended, presentation and supplement blocks; then the RTL
            // planes - Cypriot through Adlam - in the SMP.
            const bool is_rtl = (cp >= 0x0590 && cp <= 0x08FF) || (cp >= 0xFB1D && cp <= 0xFDFF) ||
                                (cp >= 0xFE70 && cp <= 0xFEFF) ||
                                (cp >= 0x10800 && cp <= 0x10FFF) ||
                                (cp >= 0x1E800 && cp <= 0x1EFFF);
            if (is_rtl) {
                rtl = true;
                return true;
            }
            // Strong left-to-right is every letter that is not one of the above.
            // Digits, punctuation and whitespace are neutral and keep the scan
            // going, which is the entire point of `dir=auto`.
            const bool is_ltr =
                (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || cp >= 0x00C0;
            if (is_ltr) {
                rtl = false;
                return true;
            }
        }
        return false;
    }

    // Everything up to the first ASCII whitespace, leading whitespace skipped.
    [[nodiscard]] static std::string_view first_word(std::string_view text) {
        const std::size_t begin = text.find_first_not_of(" \t\n\f\r");
        if (begin == std::string_view::npos) { return {}; }
        const std::size_t end = text.find_first_of(" \t\n\f\r", begin);
        return text.substr(begin, end == std::string_view::npos ? end : end - begin);
    }
    // The subtag beginning at `at`, advancing `at` past its separator. An `at` of
    // one past the end means the previous subtag was the last one.
    [[nodiscard]] static std::string_view next_subtag(std::string_view tag, std::size_t & at) {
        if (at > tag.size()) { return {}; }
        const std::size_t dash = tag.find('-', at);
        const std::size_t end = dash == std::string_view::npos ? tag.size() : dash;
        const std::string_view out = tag.substr(at, end - at);
        at = end + 1;
        return out;
    }

    // The pragma-set default language and the document version it was read at.
    // Mutable because matching is const and this is a cache, not a fact about the
    // engine - see pragma_language.
    mutable std::string pragma_language_;
    mutable std::uint64_t pragma_language_version_ = 0;

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
    // declaration, and interning takes a shared_mutex.
    atom font_size_;
    atom line_height_;
    // The ROOT element's computed font size, which is what every `rem` in the
    // document resolves against. Recorded as the tree is descended - the root is
    // resolved first, so by the time anything else asks, it is right. The root's
    // line height is `rlh`'s basis the same way.
    float root_font_size_ = 16.0f;
    float root_line_height_ = 20.0f;
    // The element being resolved, among its siblings: set by resolve() from the
    // facts the traversal gathered, read by font_context() for the tree-counting
    // functions. Zero outside a resolve, which leaves them unresolved.
    std::uint32_t sibling_index_ = 0;
    std::uint32_t sibling_count_ = 0;
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
