#pragma once

#include <optional>
#include <string>

#include <ctbrowser/style/engine.hpp>

// DOES THIS COMPILED STYLE PROGRAM MATCH THAT ONE, RULE BY FILED RULE.
//
// Phase 0 deliverable and the acceptance test for Phase 16B, which compiles a
// page's CSS at build time into runtime-ready structures so a packaged
// application never reparses a stylesheet it already knows.
//
// IT COMPARES WHAT WAS FILED, NOT WHAT WAS PARSED, and that distinction is the
// whole reason this exists. Everything `parse_stylesheet` produces was already
// readable and could always have been diffed; what `engine::add_sheet` then
// does with it - which bucket each rule lands in, which origin it carries,
// which condition ordinal it was remapped to - was readable from nowhere, so a
// rule filed in the WRONG BUCKET and a rule that simply never matched looked
// identical from outside. They are not the same defect: the first one silently
// never matches, and the only symptom is a page that renders slightly wrong.
// `engine::for_each_rule` exists for this, and this reads it.
//
// WHY NOT COMPARE resolve() ON A PINNED VIEWPORT INSTEAD, which is the obvious
// end-to-end alternative: it cannot tell a rule filed in the wrong bucket from
// one that never matched, and it drags viewport-decided state into an
// acceptance test that must not compare viewport-decided things (Principle 6).
//
// EVERY ATOM IS COMPARED AS TEXT, through the table of the engine it came from.
// Ids are handed out in first-interning order at run time, so two engines spell
// the same property with different numbers - and `atom_table::text()` returns
// an EMPTY view for an id it does not own rather than throwing, so a comparison
// that crossed the tables would fail open with both sides equal to "".
namespace ctcompile::css {

struct difference {
    std::string where; // "rule 12", "selector count", "font 0"
    std::string what;
};

[[nodiscard]] std::optional<difference> compare(const ctbrowser::style::engine & expected,
                                                const ctbrowser::style::engine & actual);

} // namespace ctcompile::css
