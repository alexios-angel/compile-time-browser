#pragma once
#include <cstdint>
#include <string>
#include <vector>

// The media-query TYPES, split out from css/media.hpp because `css::stylesheet` owns a
// table of conditions and media.hpp needs the stylesheet to parse one - so the two
// headers would include each other. The parser and the evaluator stay in media.hpp;
// only the data is here.

namespace ctbrowser::style::css {

enum class media_type : std::uint8_t {
    all,
    screen,
    print,
    // `tv`, `aural`, a typo: a type nothing here is. It matches nothing, and
    // `not tv` therefore matches everything - which is why it is a type rather
    // than a parse error (Media Queries 4 §2.2).
    other
};

struct media_environment {
    float viewport_width = 1024;
    float viewport_height = 768;
    media_type type = media_type::screen;
    bool dark = false;           // prefers-color-scheme
    bool reduced_motion = false; // prefers-reduced-motion
    float resolution_dppx = 1;
    bool hover = true;        // a pointer that can hover
    bool fine_pointer = true; // a mouse rather than a finger
    bool monochrome = false;

    // DERIVED, never stored. Two sources of truth for one fact is how a resize leaves
    // them disagreeing.
    [[nodiscard]] bool portrait() const noexcept { return viewport_height >= viewport_width; }
    [[nodiscard]] friend bool operator==(const media_environment &,
                                         const media_environment &) = default;
};

struct media_feature {
    enum class name : std::uint8_t {
        unknown, // a feature this engine does not model: makes the query false
        width,
        height,
        orientation,
        prefers_color_scheme,
        prefers_reduced_motion,
        resolution,
        hover,
        any_hover,
        pointer,
        any_pointer,
        monochrome,
        color,
        aspect_ratio,
        // Discrete features the window answers with one fixed keyword: a
        // browser tab with scripting, a fast-updating screen, no forced or
        // inverted colours, no contrast/data/transparency preference.
        prefers_contrast,
        prefers_reduced_data,
        prefers_reduced_transparency,
        forced_colors,
        inverted_colors,
        dynamic_range,
        video_dynamic_range,
        display_mode,
        scripting,
        update,
        overflow_block,
        overflow_inline,
        color_gamut,
        grid,
        scan,
    };
    // What kind of value the feature takes, which decides whether a value is
    // VALID for it - `(width: foo)` and `(orientation: 0)` parse but are
    // unknown (Media Queries 4 §2.4), not false.
    enum class kind : std::uint8_t {
        discrete, // keywords only, no range syntax
        length,
        resolution,
        integer,
        ratio,
    };
    // `min-` and `max-` are prefixes on a range feature rather than features of their
    // own, which is why they are an operator here and not thirty more enumerators -
    // and the range syntax's `<`, `<=`, `>`, `>=` and `=` are the same operators
    // spelled out (Media Queries 4 §2.4.4), with the strict pair added.
    enum class compare : std::uint8_t {
        equal,
        at_least,
        at_most,
        boolean,
        less,
        greater
    };

    name which = name::unknown;
    compare op = compare::boolean;
    float value = 0;
    // For the features whose value is a keyword: portrait, dark, reduce, none, fine.
    std::string keyword;
};

// One query: an optional media type, an optional `not`, and a condition.
//
// THE CONDITION IS KEPT AS TEXT and read again each time it is asked. Media
// Queries 4's condition is a boolean expression - `not`, `and`, `or`,
// parentheses, ranges, and `<general-enclosed>` for whatever it cannot read -
// and the engine asks it only when the environment changes, so a few tokens
// re-read on a resize cost nothing and a tree would be a second grammar to
// keep in step with the parser's.
struct media_query {
    media_type type = media_type::all;
    bool negated = false;
    bool malformed = false; // an unparseable query is `not all`: it never matches
    std::string condition;  // the `<media-condition>` after the type, or empty
};

// A comma-separated list, which is an OR, plus the ENCLOSING condition - so nesting is
// a parent index and truth ANDs up the chain.
struct media_condition {
    std::uint32_t parent = 0;
    std::vector<media_query> queries;
};

// One `@container [<name>]? <condition>` (CSS Containment 3 §5): the name
// the query container must carry, or empty for any, and the condition as
// text - size features against the container's laid-out box, `style()`
// against its computed style - read by the engine per element, which is the
// only place both are known. Nesting is a parent index, as a media
// condition's is.
struct container_condition {
    std::uint32_t parent = 0;
    std::string name;
    std::string condition;
};

} // namespace ctbrowser::style::css
