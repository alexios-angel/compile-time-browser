#pragma once
#include <cstdint>
#include <string>
#include <vector>

// CSS Animations 1 §4: `@keyframes <name> { <keyframe-selector># { <declarations> } ... }`
// as the parser CAPTURED it - the shape a stylesheet holds, before the cascade
// files it under its name (style::keyframes_rule, engine.hpp).
//
// A keyframe block is a declaration range like a @font-face, plus the offsets
// its selector list named: `from` is 0, `to` is 1, `<percentage>` is /100, and
// `0%, 50%` is ONE block with two offsets. A selector the grammar refuses drops
// its whole block, as §4 says, and nothing else in the rule.

namespace ctbrowser::style::css {

struct keyframe_block {
    std::vector<double> offsets;         // each in [0, 1]
    std::uint32_t first_declaration = 0; // into stylesheet::declarations
    std::uint32_t declaration_count = 0;
};

struct keyframes_block {
    std::string name; // the <custom-ident> or <string>, unquoted, case preserved
    // The `@media` this rule sits inside (stylesheet::conditions), 0 for none.
    std::uint32_t condition = 0;
    std::vector<keyframe_block> frames; // in source order
};

} // namespace ctbrowser::style::css
