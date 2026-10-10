//===- media_fwd.hpp - Media query types ------------------------*- C++ -*-===//
//
// The media-query and container-query data types, split out from css/media.hpp
// because `css::stylesheet` owns a table of conditions and media.hpp needs the
// stylesheet to parse one, so the two headers would include each other. The
// parser and the evaluator stay in media.hpp; only the data is here.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ctbrowser::style::css {

/// A media type (`all`, `screen`, `print`, or anything else).
enum class media_type : std::uint8_t {
    all,
    screen,
    print,
    // `tv`, `aural`, a typo: any other type. It matches nothing, so `not tv`
    // matches everything, which is why it is a type rather than a parse error
    // (Media Queries 4 §2.2).
    other
};

/// The window and user preferences media queries are evaluated against.
struct media_environment {
    float viewport_width = 1024;
    float viewport_height = 768;
    media_type type = media_type::screen;
    bool dark = false;           // prefers-color-scheme
    bool reduced_motion = false; // prefers-reduced-motion
    float resolution_dppx = 1;
    bool hover = true;        // A pointer that can hover.
    bool fine_pointer = true; // A mouse rather than a finger.
    bool monochrome = false;

    /// Derived rather than stored, so a resize cannot leave two copies of the
    /// fact disagreeing.
    [[nodiscard]] bool portrait() const noexcept { return viewport_height >= viewport_width; }
    [[nodiscard]] friend bool operator==(const media_environment &,
                                         const media_environment &) = default;
};

/// One parsed media feature test, such as `(min-width: 600px)`.
struct media_feature {
    enum class name : std::uint8_t {
        unknown, // A feature this engine does not model: makes the query false.
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
        color_index,
        // The windowing features of the Additional Windowing Controls and
        // Manifest Incubations explainers, answered as a normal, resizable
        // browser window with no navigation controls of its own.
        display_state,
        resizable,
        navigation_controls,
    };
    /// What kind of value the feature takes, which decides whether a value is
    /// valid for it: `(width: foo)` and `(orientation: 0)` parse but are
    /// unknown (Media Queries 4 §2.4), not false.
    enum class kind : std::uint8_t {
        discrete, // Keywords only, no range syntax.
        length,
        resolution,
        integer,
        ratio,
    };
    /// The comparison. `min-` and `max-` prefixes become at_least and at_most
    /// rather than separate features, and the range syntax's `<`, `<=`, `>`,
    /// `>=` and `=` map to the same operators plus the strict pair (Media
    /// Queries 4 §2.4.4). `boolean` is a bare `(feature)`.
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
    /// For the features whose value is a keyword: portrait, dark, reduce, none,
    /// fine.
    std::string keyword;
};

/// One query: an optional media type, an optional `not`, and a condition.
///
/// The condition is kept as text and re-parsed each time it is evaluated. It
/// is a boolean expression (`not`, `and`, `or`, parentheses, ranges, and
/// `<general-enclosed>`), and the engine evaluates it only when the
/// environment changes, so re-reading a few tokens costs nothing, while a tree
/// would be a second grammar to keep in step with the parser's.
struct media_query {
    media_type type = media_type::all;
    bool negated = false;
    bool malformed = false; // An unparseable query is `not all`: it never matches.
    std::string condition;  // The `<media-condition>` after the type, or empty.
};

/// A comma-separated query list (an OR), plus the index of the enclosing
/// condition: nesting is a parent index, and truth ANDs up the chain.
struct media_condition {
    std::uint32_t parent = 0;
    std::vector<media_query> queries;
};

/// One `[<container-name>]? <container-query>?` (CSS Conditional 5 §3): the
/// name the query container must carry (empty for any), and the query as
/// text. The engine evaluates it per element, the only place both the
/// container's laid-out box (for size features) and its computed style (for
/// `style()`) are known. Either part may be missing, not both.
struct container_query {
    std::string name;
    std::string condition;
};

/// One `@container <container-condition>#`: a comma list, which holds when any
/// of its conditions does, each choosing its own container. Nesting is a
/// parent index, as for media_condition.
struct container_condition {
    std::uint32_t parent = 0;
    std::vector<container_query> queries;
};

} // namespace ctbrowser::style::css
