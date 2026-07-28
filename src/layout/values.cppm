module;
#include <charconv>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <ctbrowser/core/core.hpp>

export module ctbrowser.layout:values;

// Turning style strings into layout numbers.
//
// This is where "12px" becomes 12, and it happens HERE rather than in the
// style engine on purpose: a computed style holds many declarations the box
// tree never asks about, and parsing every value at resolution time would be
// work done for properties nobody reads. Layout parses what it needs, when it
// needs it.

export namespace ctbrowser::layout {

// How text is measured. Injected rather than assumed, because the real answer
// needs a font stack that belongs to the raster layer - and because a
// deterministic stub is what makes layout testable without fonts at all.
//
// It lives HERE, in the partition that depends on nothing, because both the box
// tree and the fragment tree need it. Putting it with the fragments made :box
// import :fragment, which imports :box - a cycle the module system rejects
// outright rather than letting it become a subtle build-order problem.
// The face a run is measured in. Identical in shape to paint::font_face and
// deliberately NOT that type: :values depends on nothing, and layout importing
// the paint module to name a struct would invert the dependency the whole
// pipeline is built on. The recorder converts.
struct text_face {
    std::string family; // "" = the backend's default
    bool bold = false;
    bool italic = false;

    [[nodiscard]] friend bool operator==(const text_face &, const text_face &) = default;
};

// What layout needs to know about a font: how wide a run is, and where its
// BASELINE sits inside a line.
//
// Bundled rather than passed as separate callables because they travel
// together through every formatting context - adding a second parameter to
// measure/arrange and to every wrap helper would be the same information with
// more places to get it wrong.
//
// The ascent is what makes text of different sizes line up: a line's items are
// placed so that `y + ascent` is the same for all of them, which is the
// definition of sharing a baseline. Aligning their boxes instead - tops or
// bottoms - is only right when every item has the same metrics.
//
// Callable directly, so a measurement reads the same as it did when this was a
// bare std::function.
struct text_metrics {
    std::function<float(std::string_view, float, const text_face &)> measure;
    std::function<float(float, const text_face &)> ascent_of;
    std::function<float(float, const text_face &)> descent_of;

    [[nodiscard]] float operator()(std::string_view text, float size,
                                   const text_face & face) const {
        // The fallback is a deterministic monospace stand-in: layout has to be
        // testable with no fonts at all.
        return measure ? measure(text, size, face) : static_cast<float>(text.size()) * size * 0.6f;
    }
    [[nodiscard]] float ascent(float size, const text_face & face) const {
        return ascent_of ? ascent_of(size, face) : size * 0.8f;
    }
    [[nodiscard]] float descent(float size, const text_face & face) const {
        return descent_of ? descent_of(size, face) : size * 0.2f;
    }
    [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(measure); }
};

// The FACE is part of the question. It was not - the signature was
// (text, size) - so a page could ask for bold 20px Fira Sans, get measured in
// whatever the rasterizer felt like, and lay its text out at the wrong width.
using measure_text_fn = text_metrics;

enum class unit : std::uint8_t {
    px,
    percent,
    em,
    rem,
    auto_,
    none
};

struct length {
    float value = 0;
    unit u = unit::auto_;

    // `auto` is the ONLY value a caller has to special-case. Every other unit
    // answers resolve() given a basis, so there is deliberately no
    // "is_definite" predicate here - one existed, and every call site used it
    // to mean "not auto", which silently dropped percentages and em.
    [[nodiscard]] constexpr bool is_auto() const noexcept { return u == unit::auto_; }
    // Resolve against a containing-block basis. `auto` has no answer here -
    // the caller decides what auto means for the property it is resolving,
    // which differs between width (fill) and height (fit content).
    [[nodiscard]] constexpr float resolve(float basis, float font_size) const noexcept {
        switch (u) {
        case unit::px:
        case unit::none: return value;
        case unit::percent: return value / 100.0f * basis;
        case unit::em: return value * font_size;
        case unit::rem: return value * 16.0f;
        case unit::auto_: return 0;
        }
        return 0;
    }
};

[[nodiscard]] inline length parse_length(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) { ++i; }
    text.remove_prefix(i);
    if (text.empty()) { return length{}; }
    if (text == "auto") { return length{0, unit::auto_}; }

    float value = 0;
    const auto [rest, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{}) { return length{}; }
    const std::string_view suffix{rest, static_cast<std::size_t>(text.data() + text.size() - rest)};
    if (suffix.starts_with("px")) { return length{value, unit::px}; }
    if (suffix.starts_with('%')) { return length{value, unit::percent}; }
    if (suffix.starts_with("rem")) { return length{value, unit::rem}; }
    if (suffix.starts_with("em")) { return length{value, unit::em}; }
    return length{value, unit::none}; // unitless: treated as px, like the previous engine did
}

// The `display` values the box tree distinguishes. Everything else collapses
// into one of these for now - a box tree that models `display` exhaustively
// before there is a flex or grid algorithm to consume it would be modelling
// nothing.
enum class display_kind : std::uint8_t {
    none,
    block,
    inline_level,
    inline_block
};

[[nodiscard]] inline display_kind parse_display(std::string_view text, display_kind fallback) {
    if (text == "none") { return display_kind::none; }
    if (text == "block") { return display_kind::block; }
    if (text == "inline") { return display_kind::inline_level; }
    if (text == "inline-block") { return display_kind::inline_block; }
    if (!text.empty()) { return display_kind::block; } // flex/grid/table: block for now
    return fallback;
}

// The CSS box sides, from a 1-to-4-value shorthand plus per-side overrides.
struct side_lengths {
    length top, right, bottom, left;
};

// Tags that generate NO box unless a sheet overrides them. This is not an
// optimisation - without it a page's <script> source and <style> rules render
// as visible text, which is what the previous engine's layout::detail::skipped_tag existed to
// prevent. It is a stand-in for the UA stylesheet's display:none rules, which
// arrive with ua.hpp's port.
[[nodiscard]] inline bool generates_no_box(std::string_view tag) {
    constexpr std::string_view hidden[] = {"head", "style", "script", "title",
                                           "meta", "link",  "base",   "template"};
    for (const std::string_view t : hidden) {
        if (t == tag) { return true; }
    }
    return false;
}

// The tag list HTML renders inline by default, when the sheet says nothing.
[[nodiscard]] inline bool is_inline_by_default(std::string_view tag) {
    constexpr std::string_view inline_tags[] = {
        "a",    "span", "b",   "i",   "u",  "s",    "em",     "strong", "code", "small",
        "big",  "mark", "sub", "sup", "tt", "kbd",  "samp",   "cite",   "var",  "dfn",
        "abbr", "ins",  "del", "img", "q",  "time", "output", "label",  "br"};
    for (const std::string_view t : inline_tags) {
        if (t == tag) { return true; }
    }
    return false;
}

// Elements sized by what they ARE rather than by what they contain. A <canvas>
// is its pixel buffer; an <input> is a field wide enough to type in. Laying
// either out from its children gives a box of zero height, which is what
// happens to every parser that does not know about replaced elements.
[[nodiscard]] inline bool is_replaced_tag(std::string_view tag) {
    constexpr std::string_view names[] = {"canvas", "img",   "input",  "select", "textarea",
                                          "button", "video", "iframe", "embed",  "object"};
    for (const std::string_view t : names) {
        if (t == tag) { return true; }
    }
    return false;
}

// What `display` a tag has before any sheet speaks.
[[nodiscard]] inline display_kind default_display_for(std::string_view tag) {
    if (generates_no_box(tag)) { return display_kind::none; }
    return is_inline_by_default(tag) ? display_kind::inline_level : display_kind::block;
}

} // namespace ctbrowser::layout
