//===- ArrayIteration.h - Fold iteration over known arrays ------*- C++ -*-===//
//
// Folds the importer's iteration protocol (the `__ctbrowser_*` natives a
// for-of and an array destructuring call) where the iterated value is an
// array made before the loop, so what is left is the index loop over that
// array.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "mlir/Interfaces/FunctionInterfaces.h"

namespace ctcompile::ctjs {

/// Folds the protocol calls in \p body that iterate a known array, and returns
/// how many operations it changed. A known array is the result of
/// `Object.keys`, `Object.values` or `Object.entries` of an object literal
/// (each makes a new Array), or an element of such an `Object.entries` (a new
/// [key, value] pair). Array literals and other objects keep the protocol
/// for now: the DOM and class-initialization proofs match it.
///  - `__ctbrowser_for_of_open(a)` returns undefined for an array, which
///    sends the loop down its index path; `ctjs.iterable of a` is `a` itself.
///  - `__ctbrowser_iter_close(undefined, ...)` does nothing, so it goes, and
///    with it the handler that only guarded it (foldIteratorCloseHandlers).
///  - Destructuring an entry, `const [k, v] = pair`, opens an iterator on the
///    pair and steps it; the n-th step is `pair[n]`, and its close does
///    nothing because an Array Iterator has no `return` method.
/// The folds assume the realm's own Array iterator and `Object`, which native
/// lowering assumes everywhere: a page that stores `Object` keeps the calls.
unsigned foldArrayIteration(mlir::FunctionOpInterface body);

/// Removes the exception handlers of \p body when no operation inside them
/// can throw any more: each `ctjs.check` whose block holds nothing before it
/// but constants and reads of `__ctbrowser_*` natives becomes a branch, and
/// when no check is left, every `ctjs.push_handler` becomes a branch and every
/// `ctjs.pop_handler` goes. Returns how many operations it changed. This is
/// the fold a loop needs once its iterator close is known to do nothing.
unsigned foldIteratorCloseHandlers(mlir::FunctionOpInterface body);

} // namespace ctcompile::ctjs
