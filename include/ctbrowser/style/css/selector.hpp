#pragma once
#include <cstdint>
#include <span>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/style/css/value.hpp>

// Selectors, parsed from a component-value run straight into the form matching
// uses. There is no intermediate AST and no transcription step, which is what
// removes `engine::compile_selector` - and with it the bug where a selector was
// compiled once per DECLARATION rather than once per selector.
//
// THE SUPPORTED SUBSET IS DELIBERATELY UNCHANGED at this rung: type, `*`, `#id`,
// `.class`, the descendant and child combinators, and the five pseudo-classes the
// engine can actually observe. Widening it is the next rung, and keeping it fixed
// here is what makes a difference in a rendered page unambiguously a regression.
//
// WHAT AN UNSUPPORTED CONSTRUCT DOES, and why it is not "drop the rule". An
// attribute selector is perfectly valid CSS that this engine does not implement,
// which is a different thing from a syntax error. Chrome drops a rule whose
// selector list is genuinely INVALID; it certainly does not drop
// `[data-bs-theme=light]`. So an unsupported alternative is marked
// `never_matches` and its siblings in the list are unaffected -
// `.a, [x] { color: red }` still colours `.a`. The old front end did the same
// thing by accident, by folding `[x]` into a tag name no element could have; this
// does it on purpose, and without inventing an atom.

namespace ctbrowser::style::css {

// Parse a selector list, appending one compiled_selector per comma-separated
// alternative to `sheet.selectors`. Returns how many were appended.
//
// Never fails: an alternative it cannot represent is appended as one that can
// never match, so the count always equals the number of alternatives written and
// a caller does not have to reconcile them.
[[nodiscard]] std::uint32_t parse_selector_list(stylesheet & sheet,
                                               std::span<const component_value> prelude,
                                               atom_table & atoms);

} // namespace ctbrowser::style::css
