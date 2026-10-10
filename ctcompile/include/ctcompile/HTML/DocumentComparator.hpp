//===- DocumentComparator.hpp - Compare two DOM documents -------*- C++ -*-===//
//
// Does this document match that one, field by field?
//
// This is the acceptance test for compiling an HTML document into a
// relocatable blueprint that rebuilds the ordinary mutable DOM at startup
// without reparsing: a blueprint is right exactly when what it produces
// matches what the parser produces.
//
// It compares the state parsing produces: the tree shape, and at every node
// its kind, its tag and namespace, its attributes, and its text. It does not
// compare anything the viewport decides (no layout rect, line break, computed
// length or raster output). Those are decided at run time and change when the
// window resizes, and a blueprint that froze them would be wrong. `node`
// deliberately stores none of them, which makes that separation checkable.
//
// Two things cannot be compared by identity, both because of how the DOM is
// designed:
//
//   * `node_id` is a generation-tagged handle, not an index, and its tags mean
//     nothing across documents, let alone across processes. So the walk is in
//     document order and the report names a path, never a handle. A blueprint
//     serializes ordinals for the same reason.
//   * An `atom` is an id an atom_table hands out in first-interning order at
//     run time. Two documents built with two tables can spell the same tag
//     with different ids, so every atom is compared as text through its own
//     document's table. Comparing atom ids would pass or fail on interning
//     order, which is not a property of the document.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>

#include "ctcompile/Support/Difference.hpp"

#include <ctbrowser/dom/document.hpp>

namespace ctcompile::html {

/// Where two documents first disagree. `where` is a document-order path such as
/// `/html/body/div[2]/#text`, which is enough to find the node in either tree
/// without a handle that means anything in only one of them.
using difference = ctcompile::difference;

/// Nothing when the two documents agree on every field above; otherwise the
/// first disagreement in document order. Only the first, because a structural
/// difference makes every later comparison meaningless, so a list would be one
/// real finding followed by noise.
[[nodiscard]] std::optional<difference> compare(const ctbrowser::document & expected,
                                                const ctbrowser::document & actual);

} // namespace ctcompile::html
