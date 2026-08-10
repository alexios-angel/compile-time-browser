// dom_bindings - `getComputedStyle`. What the cascade and layout between them
// decided about one element, which is the channel tools/check/css-parity.py
// compares against Chrome property by property.
//
// WHY THIS NEEDS THREE SOURCES. `style::computed_style` is not a computed style
// despite the name: engine::resolve produces only the declarations that MATCHED,
// still as text, with no inheritance and no initial values
// (include/ctbrowser/style/computed.hpp, and tests/unit/style_basics.cpp asserts
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
// INHERITANCE IS NOT DONE HERE ANY MORE. This file used to walk DOM ancestors for an
// inherited property, because the cascade produced only the declarations that MATCHED
// and inheritance happened in four other places downstream. That made this the fifth
// ad-hoc mechanism for one idea. The cascade inherits now, so an inherited property is
// a plain `get` on the element's own style, and the walk is gone.

#include <ctbrowser/shell/bindings.hpp>

#include <ctbrowser/core/algorithms.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ctbrowser::shell {
namespace {

// The properties whose value is a LENGTH resolved against the containing block.
// Each is resolved with the same parse_length + length::resolve that layout used,
// so a percentage or an em gives layout's answer rather than a second one. Note
// what that inherits from layout: `rem` has a hardcoded 16px basis and vh/vw/pt
// fall through to pixels (include/ctbrowser/layout/values.hpp). Those are real
// differences from Chrome and they are supposed to show up in the diff.
constexpr std::array<std::string_view, 16> length_properties{
    "margin-top",  "margin-right",  "margin-bottom",  "margin-left",
    "padding-top", "padding-right", "padding-bottom", "padding-left",
    "top",         "right",         "bottom",         "left",
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

[[nodiscard]] bool is_color_property(std::string_view property) {
    return property == "color" || property == "background-color" || property == "border-color" ||
           property == "border-top-color" || property == "border-right-color" ||
           property == "border-bottom-color" || property == "border-left-color" ||
           property == "outline-color";
}

// A CSS number, serialised so a person can read the report: no trailing zeros and
// an integer prints as an integer. Rounded to 1/64 - Chrome's LayoutUnit quantum,
// and the same rounding the harness compares at - so the text and the comparison
// agree about what this number is. Without that, two values that differ only
// below the quantum print as different strings and read as a difference.
[[nodiscard]] std::string number_text(float value) {
    if (!std::isfinite(value)) { return "0"; }
    const float snapped = std::round(value * 64.0f) / 64.0f;
    std::string out = std::to_string(static_cast<double>(snapped));
    if (out.find('.') != std::string::npos) {
        while (!out.empty() && out.back() == '0') { out.pop_back(); }
        if (!out.empty() && out.back() == '.') { out.pop_back(); }
    }
    if (out.empty() || out == "-0") { return "0"; }
    return out;
}

[[nodiscard]] std::string px_text(float value) {
    return number_text(value) + "px";
}

// A colour in the one form both engines normalise to. Chrome prints
// `rgb(13, 110, 253)` and has drifted across versions, so the harness normalises
// both sides rather than either engine imitating the other. Resolving here still
// earns its keep: `#0d6efd` and `rgb(13,110,253)` then compare equal without the
// tool knowing Bootstrap's palette, and a colour that does NOT parse comes back
// as its raw text rather than silently as black.
[[nodiscard]] std::string color_text(color c) {
    const auto channel = [](std::uint8_t v) { return std::to_string(static_cast<int>(v)); };
    const std::string rgb = channel(c.red()) + ", " + channel(c.green()) + ", " + channel(c.blue());
    if (c.opaque()) { return "rgb(" + rgb + ")"; }
    return "rgba(" + rgb + ", " + number_text(static_cast<float>(c.alpha()) / 255.0f) + ")";
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

// Everything about one element that answering a property needs, gathered ONCE.
// Per-property tree walks would be O(properties x boxes): the harness asks ~48
// properties of a few hundred elements, which is tens of thousands of walks over
// the whole box tree.
//
// The ancestor CHAIN is held as ids rather than as a read transaction, so the
// snapshot does not pin a document version for as long as a page keeps the
// object alive.
struct probe {
    const layout::box_node * box = nullptr;
    const layout::fragment * frag = nullptr;
    float basis = 0; // the containing block's content width
    float font_size = 16;
    // Is this element a FLEX ITEM? A fact about its parent rather than about it,
    // and the two properties whose reported value depends on it - `min-width`
    // and `min-height` - are answered nowhere else, so it is gathered with the
    // rest of the parent lookup rather than costing a second tree walk.
    bool flex_item = false;
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

} // namespace

value dom_bindings::computed_style_object(context & cx, node_id id) {
    auto * held = static_cast<script::object_object *>(cx.make_object().as_heap());

    probe at;
    at.box = box_for(boxes_, id);
    at.frag = fragment_for(fragments_, id);
    at.font_size = at.box != nullptr ? at.box->font_size : 16.0f;
    {
        const auto txn = doc_->read();
        for (node_id up = id; up;) {
            at.chain.push_back(up);
            const node_id next = txn.parent(up);
            if (next == up) { break; }
            up = next;
        }
        // The containing-block width a percentage resolves against: the parent's
        // CONTENT width, or the viewport at the root. That is the parent's
        // fragment less its padding, which is what content_width_of computes
        // (src/layout/algorithm.cpp) - there are no borders in this box model yet.
        at.basis = static_cast<float>(viewport_width_);
        if (at.chain.size() >= 2) {
            const node_id parent = at.chain[1];
            const layout::box_node * up_box = box_for(boxes_, parent);
            at.flex_item = up_box != nullptr && up_box->kind == layout::box_kind::flex;
            if (const layout::fragment * up = fragment_for(fragments_, parent)) {
                at.basis = up->bounds.width;
                if (up_box != nullptr) {
                    at.basis -= up_box->padding.left.resolve(at.basis, up_box->font_size) +
                                up_box->padding.right.resolve(at.basis, up_box->font_size);
                }
            }
        }
    }

    // The cascade's text for one property, walking the chain when it inherits.
    // Empty when nothing declared it: the harness substitutes the CSS initial
    // value and counts how often it had to, which is how "not modelled" stays
    // visible instead of reading as "no difference".
    // BY VALUE, all of it. `getPropertyValue` below stores this lambda on the
    // returned object, which outlives this function - capturing the probe or the
    // style-map pointer by reference dangles the moment the call returns, and it
    // does so silently until a page asks for a property. The probe is a handful
    // of pointers and a short vector of node ids, so a copy is nothing; the
    // pointers are the browser's and stay valid until the next layout, which is
    // exactly the lifetime this object documents itself as having.
    const style::style_map * styles = styles_;
    atom_table * atoms = atoms_;
    // ONE LOOKUP. This used to walk DOM ancestors for an inherited property, because
    // the cascade produced only the declarations that matched and inheritance happened
    // in four other places downstream. The cascade inherits now, so `get` on the
    // element's own style is the whole answer - and the fifth ad-hoc inheritance
    // mechanism this file used to be is gone.
    const auto declared = [styles, atoms, at](std::string_view property) -> std::string_view {
        if (styles == nullptr || at.chain.empty()) { return {}; }
        const auto found = styles->find(style::engine::key_of(at.chain.front()));
        if (found == styles->end() || !found->second) { return {}; }
        return found->second->get(atoms->intern(property));
    };

    const auto value_of = [at, declared](std::string_view property) -> std::string {
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
        if (property == "width" || property == "height") {
            if (at.frag == nullptr) { return {}; }
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
            return px_text(std::max(0.0f, used));
        }
        // 2. FONT SIZE and LINE HEIGHT, already absolute on the box - the two
        //    lengths this engine resolves eagerly, and font-size is the basis every
        //    `em` here resolves against.
        //
        //    LINE HEIGHT MUST COME FROM THE BOX, not from the cascade. Chrome always
        //    reports it as a length, so a unitless `line-height: 1.5` answered as
        //    `1.5` differs from `24px` on every text-bearing element on the page -
        //    which it did, on 34 of 40 elements of one fixture, for a reason that had
        //    nothing to do with layout being wrong.
        if (property == "font-size") {
            return at.box != nullptr ? px_text(at.box->font_size) : std::string{};
        }
        if (property == "line-height") {
            return at.box != nullptr ? px_text(at.box->line_height) : std::string{};
        }
        const std::string_view text = declared(property);
        // 2b. DISPLAY, from the box tree when nothing declared it. The CSS initial
        //     value is `inline`, but almost nothing renders as its initial value -
        //     a <div> is `block` because a UA SHEET says so, and this engine's UA
        //     sheet (include/ctbrowser/style/ua.hpp) is deliberately trimmed to
        //     the properties that have a consumer, so it does not say `display`
        //     for a div at all. Leaving the answer empty and letting the harness
        //     substitute `inline` would then report a difference on every block
        //     element on the page - hundreds of lines about one absent
        //     declaration.
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
            case layout::box_kind::replaced: break; // no display to report
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
        //     the harness's initial-value table fix it - substituting `auto`
        //     there moved 566 differences the WRONG way, because most elements on
        //     most pages are not flex items.
        if (text.empty() && (property == "min-width" || property == "min-height")) {
            return at.flex_item ? "auto" : "0px";
        }
        if (text.empty()) { return {}; }
        // 3. LENGTHS, through layout's own parser against layout's own basis.
        //    `auto` stays `auto`, which is what Chrome returns for a margin that
        //    was never resolved to a used value.
        if (is_length_property(property)) {
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
        // 4. COLOURS, resolved so the two engines' spellings converge.
        if (is_color_property(property)) {
            if (const std::optional<color> c = paint::parse_color(text)) { return color_text(*c); }
            return std::string{text};
        }
        // 5. Everything else is a keyword or a list, and its computed value IS
        //    its specified text.
        return collapse_keyword(text);
    };

    // The enumerable set: what this file can compute, plus every property the
    // element ITSELF declared. Not the inherited ones - an element that declares
    // nothing would otherwise enumerate its whole ancestry, and `Object.keys`
    // would claim far more than the engine knows. `getPropertyValue` still
    // answers for those, which is the spelling the dump uses.
    std::vector<std::string> names;
    const auto offer = [&](std::string_view property) {
        const std::string text = value_of(property);
        if (text.empty()) { return; }
        std::string name{property};
        if (std::find(names.begin(), names.end(), name) != names.end()) { return; }
        held->set(name, cx.string(text));
        names.push_back(std::move(name));
    };
    offer("display");
    offer("width");
    offer("height");
    offer("font-size");
    offer("line-height");
    for (const std::string_view property : length_properties) { offer(property); }
    if (styles_ != nullptr) {
        const auto found = styles_->find(style::engine::key_of(id));
        if (found != styles_->end() && found->second) {
            for (const style::declaration & d : found->second->declarations) {
                offer(atoms_->text(d.property));
            }
        }
    }

    const auto method = [&](std::string name, script::native_fn fn) {
        held->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    // THE SPELLING THE DUMP USES, and the only one both engines agree on. It
    // takes a CSS name and accepts the IDL one too, because a page holding
    // `backgroundColor` should not have to hyphenate it itself. `value_of` is
    // captured by value along with the probe, so this needs no second tree walk
    // and no second read transaction - consistent with the object being a
    // documented snapshot rather than live.
    method("getPropertyValue", [value_of](context & c, std::span<value> args) {
        if (args.empty()) { return c.string(std::string{}); }
        const std::string asked = c.to_string(args[0]);
        std::string css;
        for (const char ch : asked) {
            if (ch >= 'A' && ch <= 'Z') {
                css += '-';
                css += static_cast<char>(ch - 'A' + 'a');
            } else {
                css += ch;
            }
        }
        return c.string(value_of(css));
    });
    // Always empty. Importance is a cascade INPUT, and by the time a value is
    // computed the question has been settled; this engine does not keep which
    // declaration won past resolve().
    method("getPropertyPriority", [](context & c, std::span<value>) { return c.string(""); });
    method("item", [names](context & c, std::span<value> args) {
        if (args.empty()) { return c.string(std::string{}); }
        const double i = context::to_number(args[0]);
        if (!(i >= 0) || static_cast<std::size_t>(i) >= names.size()) { return c.string(""); }
        return c.string(names[static_cast<std::size_t>(i)]);
    });
    held->set("length", value::number(static_cast<double>(names.size())));
    // Empty rather than reconstructed. Serialising a whole computed style means
    // deciding how to rebuild every shorthand, and engines - including Chrome
    // across its own versions - disagree there. Which is exactly why the compared
    // property set is longhands only.
    held->set("cssText", cx.string(std::string{}));
    return value::object(held);
}

void dom_bindings::install_computed_style(context & cx) {
    // A BARE GLOBAL IS ENOUGH for `window.getComputedStyle` as well: `window` is
    // a proxy whose handler falls back to the globals (install_window), so it
    // does not need its own copy. p5.js reads the bare form
    // (`getComputedStyle(el)` in vendor/p5/p5.js) and pages write both.
    cx.define_native("getComputedStyle", [this](context & c, std::span<value> args) {
        // A second argument names a pseudo-element. Accepted and IGNORED rather
        // than rejected: ::before and ::after generate no boxes yet, and throwing
        // here would make the dump script engine-specific, which is the one thing
        // a parity harness must never be.
        const node_id id = args.empty() ? node_id{} : handle_of(args[0]);
        if (!id) { return c.make_object(); }
        return computed_style_object(c, id);
    });
}

} // namespace ctbrowser::shell
