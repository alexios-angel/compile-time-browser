//===- selector.hpp - Selector list parsing ---------------------*- C++ -*-===//
//
// Selectors, parsed from a component-value run straight into the form matching
// uses (style/selector.hpp), with no intermediate AST.
//
// An unsupported construct does not drop the rule. Valid CSS this engine
// cannot represent is different from a syntax error: Chrome drops a rule whose
// selector list is invalid, not one using a feature it lacks. So an
// unsupported alternative is marked `never_matches` and its siblings in the
// list are unaffected: in `.a, <unsupported> { color: red }`, `.a` is still
// coloured.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <cstdint>
#include <span>

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/style/css/value.hpp>

namespace ctbrowser::style::css {

/// What `&` stands for, and what a selector without one is prefixed with.
///
/// Inside a style rule (CSS Nesting 1 §2) `&` is `:is(<parent list>)`, a
/// relative selector `> .a` is `& > .a`, and one naming no `&` at all is
/// `& <selector>`. Inside `@scope` (CSS Cascade 6 §3.3) the parent is empty and
/// `&` is `:where(:scope)`; a scoped selector is not prefixed with anything,
/// since the scope's own in-scope test does that job. At the top level `&` is
/// `:scope` and parse_selector_list's `nesting` argument is null.
struct nesting_context {
    std::span<const compiled_selector> parent;
    bool in_scope = false;
};

/// Parses a selector list, appending one compiled_selector per comma-separated
/// alternative to `sheet.selectors`. Returns how many were appended.
///
/// Never fails: an alternative it cannot represent is appended as one that can
/// never match, so the count always equals the number of alternatives written.
///
/// `invalid`, when given, is set true if any alternative was rejected because
/// it is not a selector at all: `div >`, `a,,b`, `#0d6efd`, a bare `:`, a number
/// where a compound belongs. That differs from "this engine cannot match it":
/// a stylesheet treats both the same, but `querySelector` must throw
/// SyntaxError for the first and return null for the second. Valid CSS this
/// engine cannot represent does not set the flag, because throwing on it would
/// turn a merely wrong answer into an exception.
[[nodiscard]] std::uint32_t parse_selector_list(stylesheet & sheet,
                                                std::span<const component_value> prelude,
                                                atom_table & atoms, bool * invalid = nullptr,
                                                const nesting_context * nesting = nullptr);

/// Whether this parser knows the pseudo-element `name` (`before`, `marker`,
/// `backdrop`, a vendor-prefixed one), which `getComputedStyle(el, "::x")`
/// checks before resolving one. Case-insensitive, without the colons.
[[nodiscard]] bool known_pseudo_element(std::string_view name);

} // namespace ctbrowser::style::css
