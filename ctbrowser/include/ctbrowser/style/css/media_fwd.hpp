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
    print
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
    };
    // `min-` and `max-` are prefixes on a range feature rather than features of their
    // own, which is why they are an operator here and not thirty more enumerators.
    enum class compare : std::uint8_t {
        equal,
        at_least,
        at_most,
        boolean
    };

    name which = name::unknown;
    compare op = compare::boolean;
    float value = 0;
    // For the features whose value is a keyword: portrait, dark, reduce, none, fine.
    std::string keyword;
};

// One query: an optional media type, an optional `not`, and a conjunction of features.
struct media_query {
    media_type type = media_type::all;
    bool negated = false;
    bool malformed = false; // an unparseable query is `not all`: it never matches
    std::vector<media_feature> features;
};

// A comma-separated list, which is an OR, plus the ENCLOSING condition - so nesting is
// a parent index and truth ANDs up the chain.
struct media_condition {
    std::uint32_t parent = 0;
    std::vector<media_query> queries;
};

} // namespace ctbrowser::style::css
