#pragma once
#include <cstdint>
#include <span>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/style/css/value.hpp>

// Selectors, parsed from a component-value run straight into the form matching
// uses. There is no intermediate AST and no transcription step.
//
// WHAT AN UNSUPPORTED CONSTRUCT DOES, and why it is not "drop the rule". An
// attribute selector is perfectly valid CSS that this engine does not implement,
// which is a different thing from a syntax error. Chrome drops a rule whose
// selector list is genuinely INVALID; it certainly does not drop
// `[data-bs-theme=light]`. So an unsupported alternative is marked
// `never_matches` and its siblings in the list are unaffected -
// `.a, [x] { color: red }` still colours `.a`.

namespace ctbrowser::style::css {

// Parse a selector list, appending one compiled_selector per comma-separated
// alternative to `sheet.selectors`. Returns how many were appended.
//
// Never fails: an alternative it cannot represent is appended as one that can
// never match, so the count always equals the number of alternatives written and
// a caller does not have to reconcile them.
//
// `invalid`, when given, is set true if any alternative was refused because it is
// not a SELECTOR AT ALL - `div >`, `a,,b`, `#0d6efd`, a bare `:`, a number where a
// compound belongs. That is a different question from "this engine cannot match
// it", and the two must not be confused: a stylesheet treats both the same way,
// but `querySelector` has to throw SyntaxError for the first and return null for
// the second. `:has(...)`, `::before`, `ns|div` and a `:not()` whose argument is
// unrepresentable are all UNSUPPORTED here and none of them sets this flag - they
// are valid CSS that a browser parses and this engine cannot answer, and throwing
// on them would fail a test that a wrong answer merely fails differently.
// WHAT `&` STANDS FOR, and what a selector without one is prefixed with.
//
// Inside a style rule (CSS Nesting 1 §2) `&` is `:is(<parent list>)`, a
// relative selector `> .a` is `& > .a`, and one naming no `&` at all is
// `& <selector>`. Inside `@scope` (CSS Cascade 6 §3.3) the parent is empty and
// `&` is `:where(:scope)` - a scoped selector is not prefixed with anything,
// since the scope's own in-scope test does that job. At the top level `&` is
// `:scope`, and this is null.
struct nesting_context {
    std::span<const compiled_selector> parent;
    bool in_scope = false;
};

[[nodiscard]] std::uint32_t parse_selector_list(stylesheet & sheet,
                                                std::span<const component_value> prelude,
                                                atom_table & atoms, bool * invalid = nullptr,
                                                const nesting_context * nesting = nullptr);

// A pseudo-element name this parser knows - `before`, `marker`, `backdrop`, a
// vendor-prefixed one - which is what `getComputedStyle(el, "::x")` asks
// before resolving one. Case-insensitive, without the colons.
[[nodiscard]] bool known_pseudo_element(std::string_view name);

} // namespace ctbrowser::style::css
