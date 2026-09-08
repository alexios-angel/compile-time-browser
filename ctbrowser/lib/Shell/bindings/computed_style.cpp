// dom_bindings - `getComputedStyle`. What the cascade and layout between them
// decided about one element, which is the channel tools/check/css-parity.py
// compares against Chrome property by property.
//
// WHY THIS NEEDS THREE SOURCES. `style::computed_style` is not a computed style
// despite the name: engine::resolve produces only the declarations that MATCHED,
// still as text, with no inheritance and no initial values
// (include/ctbrowser/style/computed.hpp, and unittests/unit/style_basics.cpp asserts
// that an unmatched element "resolves to nothing"). So:
//
//   the style map   every keyword-valued property - display, position, ...
//   the box tree    lengths, resolved the way LAYOUT resolved them
//   the fragment    used sizes, i.e. what the element actually got
//
// Reading the box tree rather than re-parsing the text is the point: asking "how
// wide is 50%" twice, in two places, is how the two answers start to differ.
// What layout used IS the computed value here.
//
// ...AND A FOURTH: style/css/properties.hpp's table says WHICH properties exist,
// which of them are shorthands, and what each one's initial value is. The three
// sources above can only answer about a property something DECLARED, so every
// other property was absent from the object entirely - which is what
// `getComputedStyle(el).someProperty === undefined` meant in 1,034 css/cssom
// subtests (docs/css-conformance.md §4). A property nothing declared has a
// computed value all the same, and it is the initial one.
//
// THE OBJECT PUBLISHES BOTH SPELLINGS. `css/css-values` asks with the hyphenated
// CSS name and `css/cssom` with the IDL one, and computed-testcommon.js asserts
// `property in getComputedStyle(target)` BEFORE it reads - so a name absent under
// either spelling fails the test before a value is ever compared. The conversion
// itself is style/css/properties.hpp's, because `el.style` needs the same one.
//
// --- WHAT IS RESOLVED AND WHAT IS COMPUTED -------------------------------
//
// CSSOM §6.7.2's resolved value is the USED value for a handful of properties
// and the COMPUTED value for everything else - AND for those same few whenever
// there is no used value to report. That "otherwise" is not an edge case: an
// element with `display: none`, one with `display: contents`, and a
// non-replaced inline all generate no box that has a width, and CSSOM says each
// of them reports the computed value. So does an inset on a static box, and so
// does an inset on a box that is OVER-CONSTRAINED. `computed_length` below is
// that half - a percentage survives, `auto` survives, and everything else
// becomes an absolute px - and it is asked for by name at each of those points
// rather than being the fallback when a pointer happens to be null.
//
// THE OBJECT IS LIVE. CSSOM says `getComputedStyle` returns a live
// CSSStyleDeclaration, and every property on it is an ACCESSOR that re-derives
// from the current trees. It was a snapshot, so a test holding
// `let cs = gcs(el)` across a write read the state from before it -
// getComputedStyle-display-none-001, -002 and -resolved-min-max-clamping are
// all written exactly that way and could not pass at all. What outlives the
// call is the element's node_id and a shared cache; the raw pointers into the
// box and fragment trees are gathered inside `computed_style_entries` and never
// escape it, because the next layout - which now happens INSIDE a script turn -
// frees them.
//
// INHERITANCE IS NOT DONE HERE ANY MORE. This file used to walk DOM ancestors for an
// inherited property, because the cascade produced only the declarations that MATCHED
// and inheritance happened in four other places downstream. That made this the fifth
// ad-hoc mechanism for one idea. The cascade inherits now, so an inherited property is
// a plain `get` on the element's own style, and the walk is gone.

#include <ctbrowser/shell/bindings.hpp>

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/style/css/properties.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace ctbrowser::shell {
namespace {

// The properties whose value is a LENGTH resolved against the containing block.
// Each is resolved with the same parse_length + length::resolve that layout used,
// so a percentage or an em gives layout's answer rather than a second one. Note
// what that inherits from layout: `rem` has a hardcoded 16px basis and vh/vw/pt
// fall through to pixels (include/ctbrowser/layout/values.hpp). Those are real
// differences from Chrome and they are supposed to show up in the diff.
//
// THE FOUR INSETS ARE NOT HERE any more: their resolved value is a used value
// for a positioned box and a computed value otherwise, which is a rule of its
// own further down rather than a length lookup.
constexpr std::array<std::string_view, 12> length_properties{
    "margin-top",  "margin-right",  "margin-bottom",  "margin-left",
    "padding-top", "padding-right", "padding-bottom", "padding-left",
    "min-width",   "max-width",     "min-height",     "max-height"};

// The four sizing constraints, whose percentages survive into the computed value.
[[nodiscard]] bool is_min_or_max_property(std::string_view property) {
    return property == "min-width" || property == "max-width" || property == "min-height" ||
           property == "max-height";
}

[[nodiscard]] bool is_length_property(std::string_view property) {
    return std::find(length_properties.begin(), length_properties.end(), property) !=
           length_properties.end();
}

[[nodiscard]] bool is_inset_property(std::string_view property) {
    return property == "top" || property == "right" || property == "bottom" || property == "left";
}

[[nodiscard]] bool is_color_property(std::string_view property) {
    return property == "color" || property == "background-color" || property == "border-color" ||
           property == "border-top-color" || property == "border-right-color" ||
           property == "border-bottom-color" || property == "border-left-color" ||
           property == "outline-color";
}

// A BORDER WIDTH IS NOT REPORTED AS THE KEYWORD IT WAS WRITTEN AS, and it is not
// reported as what the sheet asked for either: CSS Backgrounds 3 §4.3 makes the
// used width zero when the matching style is `none` or `hidden`, which is every
// element on a page with no borders. The initial value in the property table is
// `medium`, so without this an ordinary <div> would answer `medium` for all four
// sides where Chrome answers `0px`.
[[nodiscard]] bool is_border_width_property(std::string_view property) {
    return property == "border-top-width" || property == "border-right-width" ||
           property == "border-bottom-width" || property == "border-left-width" ||
           property == "outline-width";
}

// `border-top-width` -> `border-top-style`, `outline-width` -> `outline-style`.
[[nodiscard]] std::string style_property_for_width(std::string_view width_property) {
    return std::string{width_property.substr(0, width_property.size() - 5)} + "style";
}

// `thin` / `medium` / `thick` are 1, 3 and 5 CSS pixels - the figures every
// engine uses, and the reason the keyword has to become a number at all is that
// getComputedStyle reports a border width as a length and never as its keyword.
[[nodiscard]] float border_width_px(std::string_view text, float font_size) {
    if (text.empty() || ascii_iequals(text, "medium")) { return 3.0f; }
    if (ascii_iequals(text, "thin")) { return 1.0f; }
    if (ascii_iequals(text, "thick")) { return 5.0f; }
    const layout::length len = layout::parse_length(text);
    if (len.is_auto()) { return 0.0f; }
    // A percentage is not a valid border width; resolving one against a zero
    // basis is the honest answer to a declaration that got through anyway.
    return len.resolve(0.0f, font_size);
}

// A CSS number, serialised as CSSOM §6.7.2 requires: the SHORTEST decimal that
// reads back as the same number, with no exponent, no trailing zeros, and an
// integer printed as an integer. `std::to_chars` in `fixed` format is exactly
// that definition, and it is asked about a FLOAT rather than a double on
// purpose - every value here is one, and the shortest string for the float is
// the author's `20.7` where the shortest string for the double it widens to is
// `20.700000762939453`.
//
// IT DOES NOT ROUND. It used to snap to 1/64 - Chrome's LayoutUnit quantum - so
// that two values differing below the quantum could not print as a difference.
// That is right for a value layout produced and wrong for one the author wrote:
// `margin-left: 20.7px` came back as `20.703125px`, which is
// getComputedStyle-margins-roundtrip and getComputedStyle-insets-absolute-roundtrip
// in full, 8 subtests, and is the Chromium bug both files are named after. The
// snap lives in `used_px_text` below, and only the values that genuinely come
// out of layout arithmetic go through it. tools/check/css-parity.py quantises
// BOTH sides to 1/64 itself before comparing (EPSILON_PX), so nothing in the
// parity report depends on this rounding here.
[[nodiscard]] std::string number_text(float value) {
    if (!std::isfinite(value)) { return "0"; }
    std::array<char, 64> buffer{};
    const std::to_chars_result written = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                                       value, std::chars_format::fixed);
    if (written.ec != std::errc{}) { return "0"; }
    std::string out{buffer.data(), static_cast<std::size_t>(written.ptr - buffer.data())};
    if (out.empty() || out == "-0") { return "0"; }
    return out;
}

// A COMPUTED length: the number as it is.
[[nodiscard]] std::string px_text(float value) {
    return number_text(value) + "px";
}

// A USED length - one layout arrived at by adding and subtracting fragment
// bounds - rounded to 1/64. Chrome's LayoutUnit cannot represent anything finer,
// so a used value that differs from Chrome's below the quantum is not a
// difference; and our own arithmetic drifts there too, so `784 - 763.3 - 0`
// prints as `20.70001` without this and as `20.703125` with it. Both are honest
// about a number nobody wrote down.
[[nodiscard]] std::string used_px_text(float value) {
    if (!std::isfinite(value)) { return "0px"; }
    return number_text(std::round(value * 64.0f) / 64.0f) + "px";
}

// A colour in the one form both engines normalise to. Chrome prints
// `rgb(13, 110, 253)` and has drifted across versions, so the harness normalises
// both sides rather than either engine imitating the other. Resolving here still
// earns its keep: `#0d6efd` and `rgb(13,110,253)` then compare equal without the
// tool knowing Bootstrap's palette, and a colour that does NOT parse comes back
// as its raw text rather than silently as black.
//
// THE ALPHA IS QUANTISED, unlike every other number here. It is not a length and
// it is not the author's number either: an 8-bit channel divided by 255 is
// 0.5019608 for the `rgba(0, 0, 0, .5)` a page wrote, and rounding to 1/64
// recovers the `0.5` every engine prints. The precision that is lost was never
// there - the colour is stored in eight bits.
[[nodiscard]] std::string color_text(color c) {
    const auto channel = [](std::uint8_t v) { return std::to_string(static_cast<int>(v)); };
    const std::string rgb = channel(c.red()) + ", " + channel(c.green()) + ", " + channel(c.blue());
    if (c.opaque()) { return "rgb(" + rgb + ")"; }
    const float alpha = static_cast<float>(c.alpha()) / 255.0f;
    return "rgba(" + rgb + ", " + number_text(std::round(alpha * 64.0f) / 64.0f) + ")";
}

// --- A FONT FAMILY IS NOT A KEYWORD --------------------------------------
//
// `collapse_keyword` below ASCII-lowercases, which is right for `display: BLOCK`
// and wrong for every family name a page has ever written: Chrome answers
// `Twisty Tie` and this answered `twisty tie`, on every element of every page
// that names a font. The case is the author's and it is significant.
//
// AND THE LIST IS SERIALISED, not handed back. CSSOM says a family name that is
// a valid IDENTIFIER SEQUENCE serialises without quotes and one that is not
// serialises as a string - so `'Times New Roman'` loses its quotes, `'34J'`
// keeps them because `34J` is not an identifier, `"A  B"` keeps them because
// the double space would not survive, and `"serif"` keeps them because dropping
// them would turn a family CALLED serif into the generic one. The quotes a name
// keeps are always DOUBLE ones, whichever the author used.
// css/cssom/font-family-serialization-001 is fourteen assertions about exactly
// this, and the five it makes about the COMPUTED value are the ones here.

[[nodiscard]] bool is_identifier_start(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80;
}

[[nodiscard]] bool is_identifier_char(unsigned char c) {
    return is_identifier_start(c) || (c >= '0' && c <= '9') || c == '-';
}

// A CSS identifier that needs no escaping to be written down. A LEADING RUN OF
// HYPHENS is allowed - `-webkit-serif` is an identifier and so is `--x` - but a
// hyphen run with nothing after it is not, and neither is anything starting with
// a digit, which is the whole reason `34J` has to stay a string.
[[nodiscard]] bool is_bare_identifier(std::string_view word) {
    std::size_t start = 0;
    while (start < word.size() && word[start] == '-') { ++start; }
    if (start >= word.size() || !is_identifier_start(static_cast<unsigned char>(word[start]))) {
        return false;
    }
    for (const char c : word) {
        if (!is_identifier_char(static_cast<unsigned char>(c))) { return false; }
    }
    return true;
}

// The words CSS has already spoken for. A family name that IS one of them can
// only be said as a string, because unquoting it would change what it means.
[[nodiscard]] bool is_reserved_family_word(std::string_view name) {
    for (const std::string_view reserved :
         {"inherit", "initial", "unset", "revert", "revert-layer", "default", "serif", "sans-serif",
          "monospace", "cursive", "fantasy", "system-ui", "math", "fangsong", "ui-serif",
          "ui-sans-serif", "ui-monospace", "ui-rounded", "emoji"}) {
        if (ascii_iequals(name, reserved)) { return true; }
    }
    return false;
}

[[nodiscard]] bool family_can_drop_its_quotes(std::string_view name) {
    if (name.empty() || is_reserved_family_word(name)) { return false; }
    for (std::size_t at = 0;;) {
        const std::size_t space = name.find(' ', at);
        const std::string_view word =
            space == std::string_view::npos ? name.substr(at) : name.substr(at, space - at);
        if (!is_bare_identifier(word)) { return false; }
        if (space == std::string_view::npos) { return true; }
        at = space + 1;
    }
}

[[nodiscard]] std::string quoted_family(std::string_view name) {
    std::string out{"\""};
    for (const char c : name) {
        if (c == '"' || c == '\\') { out += '\\'; }
        out += c;
    }
    out += '"';
    return out;
}

[[nodiscard]] std::string font_family_text(std::string_view text) {
    // (name, was it written as a string). The pair is the whole of the rule: an
    // unquoted name is already an identifier sequence and is handed back as it
    // stands, and only a QUOTED one has a decision to make.
    std::vector<std::pair<std::string, bool>> families;
    std::string name;
    bool quoted = false;
    bool any = false;
    bool gap = false;
    // One past the end is read as a comma, so the last family is finished by the
    // same branch as every other one.
    for (std::size_t i = 0; i <= text.size();) {
        const char c = i < text.size() ? text[i] : ',';
        if (c == '"' || c == '\'') {
            const char close = c;
            ++i;
            quoted = true;
            any = true;
            while (i < text.size() && text[i] != close) {
                // A backslash escapes the next byte and is not itself part of
                // the name. This does not decode `\61` - a hex escape in a font
                // name is rare enough that carrying the digits through is a
                // better answer than a half-implemented decoder.
                if (text[i] == '\\' && i + 1 < text.size()) { ++i; }
                name += text[i];
                ++i;
            }
            if (i < text.size()) { ++i; }
            continue;
        }
        if (c == ',') {
            if (any) { families.emplace_back(name, quoted); }
            name.clear();
            quoted = false;
            any = false;
            gap = false;
            ++i;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') {
            gap = !name.empty();
            ++i;
            continue;
        }
        if (gap) {
            name += ' ';
            gap = false;
        }
        name += c;
        any = true;
        ++i;
    }
    std::string out;
    for (const auto & [family, was_string] : families) {
        if (!out.empty()) { out += ", "; }
        out += !was_string || family_can_drop_its_quotes(family) ? family : quoted_family(family);
    }
    return out;
}

[[nodiscard]] const layout::box_node * box_for(const layout::box_node * root, node_id id) {
    if (root == nullptr || !id) { return nullptr; }
    if (root->source == id) { return root; }
    for (const layout::box_node & child : root->children) {
        if (const layout::box_node * hit = box_for(&child, id)) { return hit; }
    }
    return nullptr;
}

[[nodiscard]] const layout::fragment * fragment_for(const layout::fragment * root, node_id id) {
    if (root == nullptr || !id) { return nullptr; }
    if (root->source == id) { return root; }
    for (const layout::fragment & child : root->children) {
        if (const layout::fragment * hit = fragment_for(&child, id)) { return hit; }
    }
    return nullptr;
}

// The same lookup, but in ABSOLUTE coordinates. A fragment's bounds are relative
// to its parent, so "where is this element on the page" is a different question
// from "which fragment is it" - and it is the question the used value of `top`
// and `left` is asked against.
[[nodiscard]] rect absolute_rect_of(const layout::fragment * root, node_id id) {
    rect found{};
    bool got = false;
    const auto walk = [&](auto && self, const layout::fragment & at, float dx, float dy) -> void {
        if (got) { return; }
        if (at.source == id) {
            found = rect{dx + at.bounds.x, dy + at.bounds.y, at.bounds.width, at.bounds.height};
            got = true;
            return;
        }
        for (const layout::fragment & c : at.children) {
            self(self, c, dx + at.bounds.x, dy + at.bounds.y);
        }
    };
    if (root != nullptr && id) { walk(walk, *root, 0, 0); }
    return found;
}

// Everything about one element that answering a property needs, gathered ONCE.
// Per-property tree walks would be O(properties x boxes): the object publishes
// every property in the table, and rebuilding this for each of them would mean
// 125 walks over the whole box tree per getComputedStyle call.
//
// The ancestor CHAIN is held as ids rather than as a read transaction, so
// gathering does not pin a document version for as long as a page keeps the
// object alive.
struct probe {
    const layout::box_node * box = nullptr;
    const layout::fragment * frag = nullptr;
    float basis = 0;        // the containing block's content width
    float basis_height = 0; // and its content height, for a relative inset
    float font_size = 16;
    // Is this element a FLEX ITEM? A fact about its parent rather than about it,
    // and the two properties whose reported value depends on it - `min-width`
    // and `min-height` - are answered nowhere else, so it is gathered with the
    // rest of the parent lookup rather than costing a second tree walk.
    bool flex_item = false;
    // ...AND IS IT A GRID ITEM? The same question and the same two properties,
    // but it cannot be asked of the box tree the way `flex_item` is: there is no
    // grid box kind, because there is no grid algorithm - `display: grid` lays
    // out as a block. The parent's DECLARED display is where the fact survives,
    // and getComputedStyle-resolved-min-size-auto asks about a grid item by name.
    bool grid_item = false;
    // DOES THIS ELEMENT GENERATE A PRINCIPAL BOX? Everything CSSOM reports as a
    // USED value has no answer when it does not - `display: none`,
    // `display: contents`, and anything inside such a subtree - and the rule
    // then is the computed value. Read from the box tree AND from the cascade,
    // because the two disagree about `display: contents`: it generates no box by
    // definition and this engine's layout makes one anyway, which is a layout
    // gap rather than a licence to report a used width for a box the page
    // cannot see.
    bool has_box = false;
    // A NON-REPLACED INLINE has no used width or height either (CSS 2.1 §10.3.1),
    // and css/cssom's getComputedStyle-resolved-min-max-clamping asserts that
    // for a <span> beside the two boxless cases.
    bool inline_non_replaced = false;
    // POSITIONING, for the four inset properties. Chrome reports their USED
    // values for a positioned element - a number, not the `auto` that was
    // written - and `auto` only for a static one, so answering the declared text
    // differed on 104 of one fixture's 222 remaining values. Deriving them needs
    // the element's absolute rectangle and its containing block's, which are two
    // tree walks, so they are gathered here with everything else.
    layout::position_kind position = layout::position_kind::static_;
    rect box_abs{};
    rect containing{};          // the padding box of the nearest positioned ancestor
    std::vector<node_id> chain; // self first, then ancestors
};

[[nodiscard]] std::string collapse_keyword(std::string_view text) {
    std::string out;
    bool gap = false;
    for (const char c : trim(text, html_whitespace)) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') {
            gap = true;
            continue;
        }
        if (gap && !out.empty()) { out += ' '; }
        gap = false;
        out += ascii_lower(c);
    }
    return out;
}

// ONE ELEMENT'S ANSWERS, CACHED FOR AS LONG AS THE DOCUMENT DOES NOT MOVE.
//
// The object below is live, so every property read has to be able to re-derive -
// and re-deriving means the probe's three tree walks plus 148 values, which a
// page reading one property per element after another would pay 148 times over.
// `stamp` is the document version the answers were computed at: a script cannot
// change what the cascade says without changing the document, and
// `document::version()` counts exactly that. A read at the same version is the
// same answer.
//
// WHAT THE STAMP DOES NOT SEE: a stylesheet edited through the CSSOM
// (insertRule, replaceSync) changes which rules match without touching the DOM.
// The browser is told about those through `set_author_styles_hook` and nothing
// reaches this file, so a page that edits a sheet and then reads a computed
// style it is already holding reads the previous answer. Naming it here because
// the fix is the same shared flush hook `refresh` below wants.
struct computed_cache {
    std::vector<std::pair<std::string, std::string>> entries;
    std::uint64_t stamp = 0;
};

// DOES `getComputedStyle`'s SECOND ARGUMENT NAME A PSEUDO-ELEMENT?
//
// CSSOM §5.1 asks the question in exactly this order, and the order is the
// whole rule: an argument that is null, absent, empty, or does NOT begin with a
// colon is IGNORED - `getComputedStyle(div, "before")` and
// `getComputedStyle(div, "totallynotapseudo")` both answer about the element
// itself, which is why the first assertion of getComputedStyle-pseudo,
// -pseudo-checkmark and -pseudo-picker-icon is "the argument is ignored (due to
// no colon)". Anything that DOES begin with a colon is a pseudo-element
// request, and it gets one of two answers: the pseudo-element's style, or - when
// it does not parse, or names a pseudo-element the engine has no styles for - an
// EMPTY CSSStyleDeclaration.
//
// THIS ENGINE HAS NO PSEUDO-ELEMENT STYLING AT ALL. `style/selector.hpp` models
// pseudo-CLASSES and nothing else, so `::before` matches no rule, generates no
// box and has no style to report. That makes the second answer the right one for
// every colon-prefixed argument, and it is a very different answer from the one
// this used to give: ignoring the argument reported the ORIGINATING ELEMENT's
// style as the pseudo-element's, so `getComputedStyle(div, "::before").width`
// came back as the div's `100px` where every engine says `""`. CSSOM is explicit
// that a pseudo-element that does not exist reports an empty declaration -
// `length === 0`, every property the empty string - and that is what
// getComputedStyle-pseudo's "Unknown pseudo-elements",
// -pseudo-with-argument's seventeen "should not parse" cases and -pseudo-picker's
// six "invalid pseudo-element" cases all assert.
//
// It costs one subtest to say it this bluntly: `::picker(select)` is a real
// pseudo-element that Chrome resolves, and we answer empty for it too. That is
// the honest report of an engine that does not implement it, and the moment a
// pseudo-element grows a cascade this becomes a lookup rather than a `true`.
[[nodiscard]] bool names_a_pseudo_element(context & c, value given) {
    if (given.is_nullish()) { return false; }
    const std::string text = c.to_string(given);
    return !text.empty() && text.front() == ':';
}

} // namespace

// EVERY PROPERTY OF ONE ELEMENT, AS (css name, value) PAIRS: the longhands the
// table knows, lexicographically, then the shorthands that can be reassembled,
// then whatever the element declared that the table has never heard of. Empty
// for an element that is not in the document.
//
// The order is the object's: the leading run is exactly the set CSSOM's indexed
// properties enumerate, and it is already sorted.
std::vector<std::pair<std::string, std::string>> dom_bindings::computed_style_entries(node_id id) {
    std::vector<std::pair<std::string, std::string>> answers;

    probe at;
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
        at.box_abs = absolute_rect_of(fragments_, id);
        at.containing = rect{0, 0, static_cast<float>(viewport_width_),
                             fragments_ != nullptr ? fragments_->bounds.height : 0.0f};
        for (std::size_t up = 1; up < at.chain.size(); ++up) {
            const layout::box_node * ancestor = box_for(boxes_, at.chain[up]);
            if (ancestor == nullptr || !ancestor->is_positioned()) { continue; }
            const rect outer = absolute_rect_of(fragments_, at.chain[up]);
            const layout::constraints c{outer.width, outer.height, ancestor->font_size};
            const layout::resolved_edges e = layout::resolve_edges(*ancestor, c);
            at.containing = rect{outer.x + e.border_left, outer.y + e.border_top,
                                 std::max(0.0f, outer.width - e.border_left - e.border_right),
                                 std::max(0.0f, outer.height - e.border_top - e.border_bottom)};
            break;
        }

        at.basis = static_cast<float>(viewport_width_);
        at.basis_height = fragments_ != nullptr ? fragments_->bounds.height : 0.0f;
        if (at.chain.size() >= 2) {
            const node_id parent = at.chain[1];
            const layout::box_node * up_box = box_for(boxes_, parent);
            at.flex_item = up_box != nullptr && up_box->kind == layout::box_kind::flex;
            if (const layout::fragment * up = fragment_for(fragments_, parent)) {
                at.basis = up->bounds.width;
                at.basis_height = up->bounds.height;
                if (up_box != nullptr) {
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
    const auto declared = [declared_on, at](std::string_view property) -> std::string_view {
        if (at.chain.empty()) { return {}; }
        return declared_on(at.chain.front(), property);
    };

    // THE FONT SIZE OF AN ELEMENT WITH NO BOX. Every `em` below resolves against
    // it, and the box is where it normally lives - but a `display: none` element
    // has no box and still has a computed font-size, which Chrome reports and
    // which css/cssom's getComputedStyle-insets-nobox depends on: it declares
    // `font-size: 10px` beside `top: 1em` and expects `10px`. Only an absolute
    // length can be honoured here; a percentage or an em needs the parent's
    // computed size, and the cascade does not resolve one for a box that was
    // never built.
    if (at.box == nullptr) {
        const layout::length declared_size = layout::parse_length(declared("font-size"));
        if (!declared_size.is_auto() && declared_size.u != layout::unit::percent &&
            declared_size.u != layout::unit::em) {
            at.font_size = declared_size.resolve(0.0f, 16.0f);
        }
    }
    // THE PRINCIPAL BOX, and the two ways there is not one.
    const std::string declared_display = collapse_keyword(declared("display"));
    at.has_box = at.box != nullptr && declared_display != "none" && declared_display != "contents";
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
        if (len.is_auto() || len.u == layout::unit::percent) { return collapse_keyword(given); }
        return px_text(len.resolve(0.0f, at.font_size));
    };

    const auto value_of = [at, declared,
                           computed_length](std::string_view property) -> std::string {
        // 0. A CUSTOM PROPERTY IS NOT A KEYWORD. Its value is an arbitrary token
        //    sequence whose case is significant and whose computed value is the
        //    substituted text, so it is handed back as written rather than folded
        //    the way `display: BLOCK` is.
        if (property.starts_with("--")) {
            return std::string{trim(declared(property), html_whitespace)};
        }
        // 1. USED SIZES, from the fragment - and WHICH BOX depends on box-sizing.
        //
        //    `width` names the content box by default and the border box under
        //    `box-sizing: border-box`, and getComputedStyle reports the used value
        //    of `width` - so the same element answers with a different rectangle
        //    depending on one other property. Measured rather than assumed: Chrome
        //    answers 960 for a `.container` whose fragment is 960 wide and whose
        //    horizontal padding is 12 a side, and 936 would be the content box.
        //
        //    Bootstrap sets `box-sizing: border-box` on `*`, so this is not an edge
        //    case on any page that uses it - it was 399 of 3,518 differences, all of
        //    them exactly the padding.
        //
        //    ...UNLESS THERE IS NO USED SIZE. An element with no box and a
        //    non-replaced inline both report the computed value instead, and
        //    getComputedStyle-resolved-min-max-clamping asserts it for all three
        //    of them: `width: 10%` reads back `10%`, and a `width` outside its own
        //    min/max is NOT clamped, because clamping is something the used value
        //    goes through and there is no used value here.
        if (property == "width" || property == "height") {
            if (!at.has_box || at.inline_non_replaced || at.frag == nullptr) {
                return computed_length(declared(property));
            }
            const bool horizontal = property == "width";
            float used = horizontal ? at.frag->bounds.width : at.frag->bounds.height;
            const bool border_box = ascii_iequals(declared("box-sizing"), "border-box");
            if (at.box != nullptr && !border_box) {
                const layout::side_lengths & pad = at.box->padding;
                used -= horizontal ? pad.left.resolve(at.basis, at.font_size) +
                                         pad.right.resolve(at.basis, at.font_size)
                                   : pad.top.resolve(at.basis, at.font_size) +
                                         pad.bottom.resolve(at.basis, at.font_size);
            }
            return used_px_text(std::max(0.0f, used));
        }
        // 2. FONT SIZE and LINE HEIGHT, already absolute on the box - the two
        //    lengths this engine resolves eagerly, and font-size is the basis every
        //    `em` here resolves against.
        //
        //    LINE HEIGHT MUST COME FROM THE BOX when a sheet gave it one. Chrome
        //    reports a declared line-height as a length, so a unitless
        //    `line-height: 1.5` answered as `1.5` differs from `24px` on every
        //    text-bearing element on the page - which it did, on 34 of 40 elements
        //    of one fixture, for a reason that had nothing to do with layout being
        //    wrong.
        //
        //    `normal` IS NOT A LENGTH, though, and that is the other half of the
        //    same rule: Chrome answers the keyword verbatim for it, and answers it
        //    for an element no rule reached, because `normal` is the initial value.
        //    The box carries a resolved number there whatever the cascade said -
        //    it has to, to lay text out - so asking the box alone reported a px
        //    figure for every element on a page that declares no line-height at
        //    all. css/cssom's getComputedStyle-line-height is four assertions
        //    about exactly this.
        if (property == "font-size") {
            // NOT `at.box->font_size` directly: a boxless element's size is the
            // one the cascade declared, gathered above.
            return px_text(at.font_size);
        }
        if (property == "line-height") {
            const std::string_view given = declared(property);
            if (given.empty() || ascii_iequals(given, "normal")) { return "normal"; }
            return at.box != nullptr ? px_text(at.box->line_height) : std::string{};
        }
        const std::string_view text = declared(property);
        // 2b. DISPLAY, from the box tree when nothing declared it. The CSS initial
        //     value is `inline`, but almost nothing renders as its initial value -
        //     a <div> is `block` because a UA SHEET says so, and this engine's UA
        //     sheet (include/ctbrowser/style/ua.hpp) is deliberately trimmed to
        //     the properties that have a consumer, so it does not say `display`
        //     for a div at all. Falling through to the property table's `inline`
        //     would then report a difference on every block element on the page -
        //     hundreds of lines about one absent declaration.
        //
        //     What layout USED is the honest answer, and the box knows it. Note
        //     the deliberate ordering: a DECLARED `display: flex` is reported as
        //     `flex` even though parse_display collapses it to a block box, so
        //     `display` agrees with Chrome and the missing flex algorithm shows up
        //     where it actually bites - in the geometry.
        if (property == "display" && text.empty()) {
            if (at.box == nullptr) { return "none"; } // display:none generates no box
            switch (at.box->kind) {
            case layout::box_kind::block:
            case layout::box_kind::anonymous:
                // `inline_level` is the OUTSIDE of the display value, and it is
                // what makes a `<button>` report `inline-block` rather than
                // `block` now that it is not a replaced element.
                return at.box->inline_level ? "inline-block" : "block";
            case layout::box_kind::inline_: return "inline";
            case layout::box_kind::table: return "table";
            case layout::box_kind::flex: return at.box->inline_level ? "inline-flex" : "flex";
            case layout::box_kind::text:
            case layout::box_kind::replaced: break; // the table's `inline` is right
            }
            return {};
        }
        // 2c. THE AUTOMATIC MINIMUM SIZE, reported the way Chrome reports it. The
        //     initial value of `min-width`/`min-height` is `auto`, and Chrome
        //     answers that verbatim for a FLEX ITEM - where `auto` means the
        //     content-based minimum and really is not a length - while resolving
        //     it to `0px` everywhere else, because outside a flex or grid
        //     container `auto` behaves as zero.
        //
        //     Answering one of the two for both is not a small error: it was 139
        //     of the grid fixture's 490 differences, every one of them about
        //     which parent the element has rather than about the element. Nor can
        //     the property table's initial value fix it - it says `auto`, which is
        //     what the specification says and what moved 566 differences the WRONG
        //     way when css-parity.py tried it, because most elements on most pages
        //     are not flex items.
        //
        //     `auto` "resolves to zero when no box is generated" whatever the
        //     parent is (CSS Sizing 3), which is the second half of
        //     getComputedStyle-resolved-min-size-auto.
        //
        //     AN `auto` THE AUTHOR WROTE IS THE SAME `auto`. This asked
        //     `text.empty()`, so the rule applied to the INITIAL value and to
        //     nothing else: `min-width: auto` in a style attribute fell through
        //     to the length branch below and came back as the keyword. That is
        //     half of getComputedStyle-resolved-min-size-auto by construction -
        //     the file asserts each element twice, once as the initial value and
        //     once with the same `auto` set through `style.setProperty`, and
        //     seven of its fourteen failures were the second assertion of a pair
        //     whose first one passed.
        //
        //     AND THREE THINGS PRESERVE IT, not one. A flex item, a GRID item -
        //     which is a declaration on the parent rather than a box kind, since
        //     `display: grid` lays out as a block here - and any element with a
        //     specified `aspect-ratio`, where the automatic minimum is the
        //     transferred size and genuinely is not zero. A degenerate ratio
        //     (`0/1`) and the two-part form (`auto 1/1`) preserve it too: the
        //     rule is "an aspect-ratio was specified", not "it is usable".
        if (property == "min-width" || property == "min-height") {
            const std::string given = collapse_keyword(text);
            if (given.empty() || given == "auto") {
                if (!at.has_box) { return "0px"; }
                if (at.flex_item || at.grid_item) { return "auto"; }
                const std::string ratio = collapse_keyword(declared("aspect-ratio"));
                return !ratio.empty() && ratio != "auto" ? "auto" : "0px";
            }
        }
        // 2d. THE INSETS. CSSOM §6.7.2 gives them the longest special case in the
        //     specification, and all three of its arms are here:
        //
        //       no box, or not positioned  -> the COMPUTED value. An inset does
        //         not apply to a static box, so `top: 10%` reads back `10%` and
        //         `top: auto` reads back `auto`. This answered `auto` for all
        //         four sides of every static element, which is every element on
        //         most pages, and is what getComputedStyle-insets-static and
        //         -insets-nobox assert 216 times each.
        //       over-constrained           -> the COMPUTED value, again. When
        //         both sides and the size in that axis are given, one of the
        //         three is ignored, and CSSOM refuses to report a used value it
        //         would have to pick a loser for.
        //       otherwise                  -> the USED value.
        //
        //     A SIDE THE AUTHOR GAVE A LENGTH IS ITS OWN USED VALUE. Deriving it
        //     from the geometry instead - `containing.width - left - width` -
        //     is arithmetic on floats that the author's `20.7px` does not
        //     survive, which is getComputedStyle-insets-absolute-roundtrip. The
        //     geometry is still what answers for a side that says `auto`,
        //     because that one really was decided by layout.
        if (is_inset_property(property)) {
            const bool horizontal = property == "left" || property == "right";
            const bool start = property == "top" || property == "left";
            const std::string_view start_text = declared(horizontal ? "left" : "top");
            const std::string_view end_text = declared(horizontal ? "right" : "bottom");
            const layout::length mine = layout::parse_length(text);
            const bool start_auto = layout::parse_length(start_text).is_auto();
            const bool end_auto = layout::parse_length(end_text).is_auto();
            const float basis = horizontal ? at.containing.width : at.containing.height;

            if (!at.has_box || at.position == layout::position_kind::static_) {
                return computed_length(text);
            }
            if (at.position == layout::position_kind::relative ||
                at.position == layout::position_kind::sticky) {
                // A relative box's containing block is the one it would have had
                // staying put - its parent's content box - and NOT the nearest
                // positioned ancestor, which is the absolute rule.
                const float relative_basis = horizontal ? at.basis : at.basis_height;
                // Both sides given: CSS 9.4.3 ignores one of them, so this is
                // over-constrained and the computed value is the answer.
                if (!start_auto && !end_auto) { return computed_length(text); }
                if (!mine.is_auto()) { return px_text(mine.resolve(relative_basis, at.font_size)); }
                // `auto` against a given opposite side is its negation: the box
                // moved, and this side records the move from the other end.
                const layout::length other = layout::parse_length(start ? end_text : start_text);
                if (other.is_auto()) { return "0px"; }
                return px_text(-other.resolve(relative_basis, at.font_size));
            }
            // Absolute or fixed. Over-constrained needs the SIZE as well: with
            // both insets and a definite size, CSS 10.3.7 ignores `right` (and
            // 10.6.4 ignores `bottom`), and CSSOM reports computed values for
            // the whole axis rather than a used value with a loser in it.
            const bool size_auto =
                layout::parse_length(declared(horizontal ? "width" : "height")).is_auto();
            if (!start_auto && !end_auto && !size_auto) { return computed_length(text); }
            if (!mine.is_auto()) { return px_text(mine.resolve(basis, at.font_size)); }
            const float left = at.box_abs.x - at.containing.x;
            const float top = at.box_abs.y - at.containing.y;
            if (property == "left") { return used_px_text(left); }
            if (property == "top") { return used_px_text(top); }
            if (property == "right") {
                return used_px_text(at.containing.width - left - at.box_abs.width);
            }
            return used_px_text(at.containing.height - top - at.box_abs.height);
        }
        // 2e. THE BORDER AND OUTLINE WIDTHS, whose used value is zero unless the
        //     matching style draws something. Answered here rather than left to
        //     the initial value because the initial value is `medium` and no box
        //     with no border is three pixels wide.
        if (is_border_width_property(property)) {
            const std::string style_name = style_property_for_width(property);
            const std::string_view drawn = declared(style_name);
            if (drawn.empty() || ascii_iequals(drawn, "none") || ascii_iequals(drawn, "hidden")) {
                return "0px";
            }
            return px_text(border_width_px(text, at.font_size));
        }
        if (text.empty()) { return {}; }
        // 3. LENGTHS, through layout's own parser against layout's own basis.
        //    `auto` stays `auto`, which is what Chrome returns for a margin that
        //    was never resolved to a used value.
        if (is_length_property(property)) {
            // ...WHEN THERE IS A BOX. A margin and a padding are on CSSOM's list
            // of properties whose resolved value is the USED one, and that list
            // is conditioned on the element generating a box: a `display: none`
            // element has no used margin, so `margin-left: 10%` reads back as
            // `10%` rather than as a share of a containing block it does not
            // have. Same sentence, same rule, as `width` above.
            if (!at.has_box) { return computed_length(text); }
            const layout::length len = layout::parse_length(text);
            if (len.is_auto()) { return "auto"; }
            // A PERCENTAGE MIN OR MAX STAYS A PERCENTAGE. Their computed value is the
            // percentage as specified - resolving it needs a containing block, which
            // is a used-value question, and unlike width there is no used value to
            // report because a max is a constraint rather than a size. Chrome answers
            // `100%` for `.row > * { max-width: 100% }` and this answered 960px, on
            // 62 of the grid fixture's 111 elements.
            //
            // Margin and padding are NOT in this exception, deliberately: Chrome
            // resolves their percentages to pixels, because there a percentage does
            // have a used value and reporting it is what makes an auto margin
            // comparable at all.
            if (len.u == layout::unit::percent && is_min_or_max_property(property)) {
                return std::string{text};
            }
            return px_text(len.resolve(at.basis, at.font_size));
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
        if (known != nullptr && (known->kind == style::css::value_kind::length ||
                                 known->kind == style::css::value_kind::length_percentage)) {
            return computed_length(text);
        }
        // 3c. THE FONT FAMILY LIST, whose case is the author's and whose quoting
        //     is CSSOM's. Answered before the keyword fold below, which would
        //     lowercase it - see `font_family_text`.
        if (property == "font-family") { return font_family_text(text); }
        // 4. COLOURS, resolved so the two engines' spellings converge.
        if (is_color_property(property)) {
            if (const std::optional<color> c = paint::parse_color(text)) { return color_text(*c); }
            return std::string{text};
        }
        // 5. Everything else is a keyword or a list, and its computed value IS
        //    its specified text.
        return collapse_keyword(text);
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
    for (const style::css::property_syntax & p : style::css::known_properties()) {
        if (p.shorthand) { continue; }
        std::string text = value_of(p.name);
        if (text.empty()) { text = std::string{p.initial}; }
        if (ascii_iequals(text, "currentcolor")) { text = current_color; }
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
    // EMPTY FOR THE REST, deliberately. `font`, `background`, `outline`,
    // `list-style`, `transition`, `animation` and `text-decoration` are
    // grammars rather than lists; engines disagree about how to rebuild them -
    // Chrome disagrees with itself across versions - and CSSOM already says a
    // shorthand that cannot be represented serialises to the empty string. That
    // is the same answer, and the same reason, as `cssText` below.
    const auto longhand = [&answers, longhand_count](std::string_view name) -> std::string {
        for (std::size_t i = 0; i < longhand_count; ++i) {
            if (std::string_view{answers[i].first} == name) { return answers[i].second; }
        }
        return {};
    };
    const auto sides = [&longhand](std::string_view t, std::string_view r, std::string_view b,
                                   std::string_view l) -> std::string {
        const std::string top = longhand(t);
        const std::string right = longhand(r);
        const std::string bottom = longhand(b);
        const std::string left = longhand(l);
        if (top.empty() || right.empty() || bottom.empty() || left.empty()) { return {}; }
        if (left != right) { return top + " " + right + " " + bottom + " " + left; }
        if (bottom != top) { return top + " " + right + " " + bottom; }
        if (right != top) { return top + " " + right; }
        return top;
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
            text = sides("border-top-left-radius", "border-top-right-radius",
                         "border-bottom-right-radius", "border-bottom-left-radius");
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
        }
        shorthands.emplace_back(std::string{p.name}, std::move(text));
    }
    answers.insert(answers.end(), std::make_move_iterator(shorthands.begin()),
                   std::make_move_iterator(shorthands.end()));

    // AND EVERY PROPERTY THE ELEMENT ITSELF DECLARED that the table has never
    // heard of - a CUSTOM property above all, which no fixed table can enumerate.
    // Readable, and answering to `in`, but NOT indexed: CSSOM's indexed
    // properties are the supported longhands and a page's `--brand` is not one.
    if (styles_ != nullptr) {
        const auto found = styles_->find(style::engine::key_of(id));
        if (found != styles_->end() && found->second) {
            for (const style::declaration & d : found->second->declarations) {
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
        }
    }
    return answers;
}

value dom_bindings::computed_style_object(context & cx, node_id id) {
    auto * held = static_cast<script::object_object *>(cx.make_object().as_heap());

    const auto cached = std::make_shared<computed_cache>();
    cached->entries = computed_style_entries(id);
    cached->stamp = doc_->version();

    // THE LIVE READ, and the flush it needs.
    //
    // Every property below re-derives, so a page holding the object across a
    // write sees the write - which is the whole of CSSOM's "live" and of
    // getComputedStyle-display-none-001/-002. Re-deriving is only correct if the
    // pipeline has caught up first, though: `el.style.color = 'green'` marks the
    // cascade stale and nothing resolves it until the next frame, so a read
    // that skipped the flush would answer from before the page's own write -
    // exactly the defect docs/css-conformance.md §6 measured for the call
    // itself.
    //
    // REACHED THROUGH THE GLOBAL, which is the only channel a binding has.
    // browser::run_scripts wraps `getComputedStyle` with a native that runs
    // precisely the stages `dirty_` says are stale; the bindings deliberately do
    // not know that layout exists ("a native that changes the document calls
    // on_mutation, and the browser decides what that invalidates", at the top of
    // shell/bindings.hpp), so calling that wrapper with no arguments is how a
    // property read asks for the same flush. With no argument the inner native
    // makes one empty object and returns, so the call costs the flush and
    // nothing else. A page that has REPLACED the global gets no flush rather
    // than a call into its own function: the kind check is what makes that safe.
    //
    // It is a stand-in for the shared flush hook on dom_bindings that
    // getBoundingClientRect, offsetWidth and clientHeight all want too, and it
    // is written to be deleted the moment that exists.
    const auto refresh = [this, id, cached](context & c) {
        const std::uint64_t now = doc_->version();
        if (now == cached->stamp) { return; }
        const value flush = c.global("getComputedStyle");
        if (flush.is_kind(script::heap_kind::native)) {
            (void)c.call(flush, std::span<const value>{});
        }
        cached->entries = computed_style_entries(id);
        cached->stamp = doc_->version();
    };
    const auto answer = [cached](std::string_view name) -> std::string {
        for (const auto & [key, text] : cached->entries) {
            if (std::string_view{key} == name) { return text; }
        }
        return {};
    };

    // A COMPUTED STYLE IS READ-ONLY, and CSSOM §6.7.2 says how: every mutating
    // member throws NoModificationAllowedError. Doing nothing instead let a page
    // write to it and believe the write had landed.
    //
    // ONE SETTER OBJECT FOR ALL OF THEM. The property-assignment half of this
    // rule used to be left out on the grounds that it would cost "a setter
    // accessor on each of the 125 published names, per call" - which was true
    // while the names were data properties. They are accessors now, for
    // liveness, so each already has a descriptor to hang this on and the whole
    // rule costs one extra allocation. computed-style-001 and
    // computed-style-set-property assert it three ways: `style.color = 'blue'`,
    // `style.cssText = '...'` and `style.setProperty(...)`.
    const value refuse = value::object(
        cx.allocate<script::native_object>("set", [this](context & c, std::span<value>) {
            throw_dom_exception(c, "NoModificationAllowedError",
                                "a computed style declaration is read-only");
            return value::undefined();
        }));
    const auto reader = [&cx, refresh, answer](const std::string & name) {
        return value::object(cx.allocate<script::native_object>(
            name, [refresh, answer, name](context & c, std::span<value>) {
                refresh(c);
                return c.string(answer(name));
            }));
    };

    // EVERY NAME THE OBJECT ANSWERS TO: the whole table - longhands and
    // shorthands alike, because CSSOM gives a computed style an attribute for
    // every SUPPORTED property - and then whatever this element declared that
    // the table has never heard of.
    //
    // BOTH SPELLINGS SHARE ONE GETTER. `background-color` and `backgroundColor`
    // are one property read two ways, and a second native per name would double
    // the allocations a getComputedStyle call costs on a page that asks for one
    // per element.
    const auto publish = [&](const std::string & css_name) {
        const value get = reader(css_name);
        held->define_accessor(css_name, get, refuse);
        const std::string idl = style::css::idl_name_of(css_name);
        if (idl != css_name) { held->define_accessor(idl, get, refuse); }
    };
    for (const style::css::property_syntax & p : style::css::known_properties()) {
        publish(std::string{p.name});
    }
    for (const auto & [name, text] : cached->entries) {
        if (style::css::find_property(name) == nullptr) { publish(name); }
    }

    // THE INDEXED GETTER AND `length`, CSSOM §6.7 - as DATA properties, which is
    // also what makes the object ITERABLE. `[...style]` and `for (const p of
    // style)` go through context::iterable_values, and that recognises anything
    // carrying a numeric `length` beside indexed properties; there is no
    // Symbol.iterator dispatch in this VM to hook instead. serialize-all-longhands
    // and getComputedStyle-property-order both spread one.
    //
    // THE SUPPORTED LONGHANDS, in the lexicographic order computed_style_entries
    // put them in - and none of the shorthands or custom properties that follow
    // them there. Empty for an element that is not being rendered, which is
    // `length === 0` in getComputedStyle-detached-subtree.
    //
    // FIXED WHEN THE OBJECT IS MADE, unlike every value on it. The set of
    // supported longhands cannot change under a page; what can is whether the
    // element is in the document at all, so a declaration taken for a detached
    // element keeps `length === 0` after the element is appended. Making the
    // index list live too would mean an accessor per index and a `length` that
    // is not a data property, and `context::iterable_values` finds the object
    // iterable by reading exactly that data property.
    std::vector<std::string> indexed;
    for (const auto & [name, text] : cached->entries) {
        const style::css::property_syntax * known = style::css::find_property(name);
        if (known != nullptr && !known->shorthand) { indexed.push_back(name); }
    }
    for (std::size_t i = 0; i < indexed.size(); ++i) {
        held->set(std::to_string(i), cx.string(indexed[i]));
    }
    held->set("length", value::number(static_cast<double>(indexed.size())));

    const auto method = [&](std::string name, script::native_fn fn) {
        held->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    // THE SPELLING THE DUMP USES, and the only one both engines agree on. It
    // takes a CSS name and accepts the IDL one too, because a page holding
    // `backgroundColor` should not have to hyphenate it itself. Live, like the
    // accessors: css-style-declaration-modifications edits a stylesheet rule and
    // reads the computed value back through this.
    method("getPropertyValue", [refresh, answer](context & c, std::span<value> args) {
        if (args.empty()) { return c.string(std::string{}); }
        const std::string asked = style::css::css_name_of(c.to_string(args[0]));
        refresh(c);
        return c.string(answer(asked));
    });
    // Always empty. Importance is a cascade INPUT, and by the time a value is
    // computed the question has been settled; this engine does not keep which
    // declaration won past resolve().
    method("getPropertyPriority", [](context & c, std::span<value>) { return c.string(""); });
    method("item", [indexed](context & c, std::span<value> args) {
        if (args.empty()) { return c.string(std::string{}); }
        const double i = context::to_number(args[0]);
        if (!(i >= 0) || static_cast<std::size_t>(i) >= indexed.size()) { return c.string(""); }
        return c.string(indexed[static_cast<std::size_t>(i)]);
    });
    const auto refuse_method = [this](context & c, std::span<value>) {
        throw_dom_exception(c, "NoModificationAllowedError",
                            "a computed style declaration is read-only");
        return value::undefined();
    };
    method("setProperty", refuse_method);
    method("removeProperty", refuse_method);
    // Empty rather than reconstructed - which is also what the specification
    // says: a computed style's cssText getter returns the empty string, because
    // serialising one means deciding how to rebuild every shorthand and engines,
    // including Chrome across its own versions, disagree there. Which is exactly
    // why the compared property set is longhands only. An ACCESSOR so that the
    // setter can refuse: `cs.cssText = "color: blue"` is the first of
    // computed-style-001's three read-only assertions.
    held->define_accessor(
        "cssText",
        value::object(cx.allocate<script::native_object>(
            "cssText", [](context & c, std::span<value>) { return c.string(std::string{}); })),
        refuse);
    // A computed style belongs to no rule.
    held->set("parentRule", value::null());
    return value::object(held);
}

void dom_bindings::install_computed_style(context & cx) {
    // A BARE GLOBAL IS ENOUGH for `window.getComputedStyle` as well: `window` is
    // a proxy whose handler falls back to the globals (install_window), so it
    // does not need its own copy. p5.js reads the bare form
    // (`getComputedStyle(el)` in vendor/p5/p5.js) and pages write both.
    //
    // browser::run_scripts WRAPS THIS GLOBAL to flush a pending restyle before
    // it runs - the bindings deliberately do not know that layout exists, and
    // without the flush every answer below is the one from before the script's
    // own write. See the note there, and the one on `refresh` above: a LIVE read
    // needs the same flush and reaches it back through this same global.
    cx.define_native("getComputedStyle", [this](context & c, std::span<value> args) {
        const node_id id = args.empty() ? node_id{} : handle_of(args[0]);
        if (!id) { return c.make_object(); }
        // A SECOND ARGUMENT NAMING A PSEUDO-ELEMENT gets an EMPTY declaration,
        // for the reason `names_a_pseudo_element` sets out - and it is spelled
        // as an empty node handle rather than as a flag, because
        // `computed_style_entries` ALREADY answers nothing for an element that
        // is not in the document. A pseudo-element this engine does not style is
        // the same case, gets the same answer, and gets it through the same
        // object: every property reads back the empty string, `length` is zero,
        // and every write still throws NoModificationAllowedError.
        if (args.size() > 1 && names_a_pseudo_element(c, args[1])) {
            return computed_style_object(c, node_id{});
        }
        return computed_style_object(c, id);
    });
}

} // namespace ctbrowser::shell
