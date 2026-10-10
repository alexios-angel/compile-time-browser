//===- StyleProgramComparator.hpp - Compare filed CSS rules -----*- C++ -*-===//
//
// Checks whether one compiled style program matches another, rule by filed
// rule. This is the acceptance test for compiling a page's CSS at build time
// into runtime-ready structures, so a packaged application never reparses a
// stylesheet it already knows.
//
// It compares what was filed, not what was parsed. `parse_stylesheet` output
// was always diffable; what `engine::add_sheet` does with it (which bucket each
// rule lands in, which origin it carries, which condition ordinal it was
// remapped to) is visible only through `engine::for_each_rule`, which this
// reads. Without it a rule filed in the wrong bucket looks identical to one
// that simply never matched, and the only symptom is a page that renders
// slightly wrong.
//
// Comparing resolve() on a pinned viewport would not work: it cannot tell a
// rule filed in the wrong bucket from one that never matched, and it pulls in
// viewport-dependent state, which a build-time artefact must never freeze.
//
// Every atom is compared as text, through the table of the engine it came
// from. Ids are handed out in first-interning order at run time, so two engines
// spell the same property with different numbers, and `atom_table::text()`
// returns an empty view (rather than throwing) for an id it does not own, so a
// comparison that crossed tables would pass with both sides equal to "".
//
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>

#include "ctcompile/Support/Difference.hpp"

#include <ctbrowser/style/engine.hpp>

namespace ctcompile::css {

/// A mismatch; `where` reads like "rule 12", "selector count" or "font 0".
using difference = ctcompile::difference;

/// Returns the first difference between the rules filed in `expected` and
/// `actual`, or nothing when they match.
[[nodiscard]] std::optional<difference> compare(const ctbrowser::style::engine & expected,
                                                const ctbrowser::style::engine & actual);

} // namespace ctcompile::css
