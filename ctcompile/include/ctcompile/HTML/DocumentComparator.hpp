#pragma once

#include <optional>
#include <string>

#include <ctbrowser/dom/document.hpp>

// DOES THIS DOCUMENT MATCH THAT ONE, FIELD BY FIELD.
//
// Phase 0 deliverable, and it is written BEFORE the thing it accepts: Phase 16A
// compiles an HTML document into a relocatable blueprint that reconstructs the
// ordinary mutable DOM at startup without reparsing, and the only honest way to
// know a blueprint is right is to compare what it produces against what the
// parser produces. That comparison is this function, and it exists now so that
// 16A is written against a test rather than followed by one.
//
// WHAT IT COMPARES is the state parsing produces: the tree shape, and at every
// node its kind, its tag AND NAMESPACE, its attributes, and its text. What it
// does NOT compare is anything the viewport decides - no layout rect, no line
// break, no computed length, no raster output. Those are runtime, they change
// when the window resizes, and a blueprint that froze them would be wrong
// (Principle 6). `node` deliberately stores none of them, which is what makes
// that separation checkable rather than aspirational.
//
// TWO THINGS IT CANNOT COMPARE BY IDENTITY, and both are consequences of the
// DOM's design rather than choices here:
//
//   * `node_id` is a GENERATION-TAGGED HANDLE, not an index, and its tags are
//     meaningless across documents - let alone across processes. So the walk is
//     by DOCUMENT ORDER and the report names a path, never a handle. A
//     blueprint serializes ordinals for exactly the same reason.
//   * An `atom` is an id handed out by an atom_table IN FIRST-INTERNING ORDER
//     at run time. Two documents built by two tables can spell the same tag
//     with different ids, so every atom is compared as TEXT through its own
//     document's table. A comparator that compared atom ids would pass or fail
//     on interning order, which is not a property of the document at all.
namespace ctcompile::html {

// Where two documents first disagree. `where` is a document-order path such as
// `/html/body/div[2]/#text`, which is enough to find the node in either tree
// without a handle that means anything in only one of them.
struct difference {
    std::string where;
    std::string what;
};

// Nothing when the two documents agree on every field above; otherwise the
// FIRST disagreement in document order. First rather than all of them: a
// structural difference makes every later comparison meaningless, so a list
// would be one real finding followed by noise.
[[nodiscard]] std::optional<difference> compare(const ctbrowser::document & expected,
                                                const ctbrowser::document & actual);

} // namespace ctcompile::html
