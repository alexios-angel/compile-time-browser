//===- keyframes.hpp - @keyframes rules -------------------------*- C++ -*-===//
//
// CSS Animations 1 §4: `@keyframes <name> { <keyframe-selector># {
// <declarations> } ... }` as the parser captured it, the shape a stylesheet
// holds before the cascade files it under its name (style::keyframes_rule in
// engine.hpp).
//
// A keyframe block is a declaration range like a @font-face, plus the offsets
// its selector list named: `from` is 0, `to` is 1, a `<percentage>` is divided
// by 100, and `0%, 50%` is one block with two offsets. A selector the grammar
// rejects drops its whole block, as §4 says, and nothing else in the rule.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ctbrowser::style::css {

/// One keyframe block: its offsets and its declarations.
struct keyframe_block {
    std::vector<double> offsets;         // Each in [0, 1].
    std::uint32_t first_declaration = 0; // Index into stylesheet::declarations.
    std::uint32_t declaration_count = 0;
    [[nodiscard]] friend bool operator==(const keyframe_block &, const keyframe_block &) = default;
};

/// One `@keyframes` rule.
struct keyframes_block {
    std::string name; // The <custom-ident> or <string>, unquoted, case preserved.
    /// The `@media` this rule sits inside (stylesheet::conditions), 0 for none.
    std::uint32_t condition = 0;
    /// The `@layer` this rule sits in (stylesheet::layers, 1-based), 0 for
    /// none. A name defined in two layers resolves to the rule of the stronger
    /// layer (CSS Cascade 5 §6.4.3).
    std::uint32_t layer = 0;
    std::vector<keyframe_block> frames; // In source order.
    [[nodiscard]] friend bool operator==(const keyframes_block &,
                                         const keyframes_block &) = default;
};

} // namespace ctbrowser::style::css
