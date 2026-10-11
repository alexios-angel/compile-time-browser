//===- CTNativeLattice.h - The native type lattice --------------*- C++ -*-===//
//
// The native type lattice: the single `meet` every analysis uses to combine
// two types.
//
// The ctnative dialect is otherwise declared in TableGen, but a meet over an
// unbounded family of parameterized types is arithmetic on types, which
// TableGen cannot express. So this is C++, kept as small as the job allows:
// one function, one header, one translation unit.
//
// There must be exactly one meet. Type inference, ownership, container element
// types and per-function native selection all call it; two places computing
// "what type is this" differently would produce programs that are only
// sometimes right.
//
// On the name: in the order `bottom -> i32 -> i64 -> f64 -> variant<...> ->
// json -> top(boxed)`, `bottom` is "nothing reached here" and `boxed` is the
// safe answer, so this function computes the least upper bound, the smallest
// type correct for both inputs. Dataflow literature would call that a join. It
// is a meet in the older compiler-textbook sense, where confluence moves toward
// the conservative answer. The name stays because one name for this function
// is worth more than a better one.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctcompile/CTNative/IR/CTNativeTypes.h"

#include "mlir/IR/Types.h"
#include "llvm/ADT/ArrayRef.h"

namespace ctcompile::ctnative {

/// How many alternatives a `!ctnative.variant` may carry before the lattice
/// gives up and answers `!ctnative.json`.
///
/// Four is a policy, not a measurement. A wider std::variant makes generated
/// code no one would write by hand, and an uncapped union makes the lattice
/// infinitely tall, so the type-inference dataflow would have no termination
/// guarantee. The cap makes the lattice finite-height.
///
/// The cap is tested: replacing its condition with `false` leaves the meet
/// table, commutativity and all 9261 associativity triples passing, and turns
/// exactly one check red ("the fifth falls off the cap into json" in
/// ctcompile/test/Analysis/Types/Lattice.cpp).
inline constexpr unsigned kMaxVariantAlternatives = 4;

/// How many targets a `!ctnative.closure_set` may name before the lattice
/// answers `!ctnative.boxed`.
///
/// Sixteen is a policy with a measured floor: Popper's modifier engine, the
/// largest closed callable set in the workloads, calls through a field that
/// holds one of ten functions. Each target is one `std::visit` arm in the
/// emitted C++, so a much larger set is generated code no one would write.
/// Like the variant cap it bounds the lattice's height, which the
/// type-inference dataflow needs to terminate. Past the cap the answer is
/// `boxed` rather than `json`: `json` describes data, and no data value can
/// be called.
inline constexpr unsigned kMaxClosureSetTargets = 16;

/// The C++ representation a JavaScript string gets when nothing has proved a
/// narrower one.
///
/// UTF-8 (`std::string`), not UTF-16. ECMA-262 defines a string as UTF-16 code
/// units, so `"\u{1F600}".length` is 2; that is the right answer but not the
/// right carrier for this engine. ctbrowser stores JavaScript strings as WTF-8
/// and counts `length` and indices in UTF-16 code units over those bytes
/// (ctbrowser/include/ctbrowser/script/utf16.hpp). The DOM and the native
/// runtime use the same bytes, and a native `.length` is
/// `ctnative::string_length`, which gives the same count. The ND-1 block of
/// ctcompile/test/Analysis/Types/Lattice.cpp checks this against the real
/// interpreter; ctcompile/docs/native-divergences.md entry ND-1 has the
/// history.
inline constexpr StrEncoding kDefaultStringEncoding = StrEncoding::UTF8;

/// The meet: the smallest type that is correct for both `a` and `b`.
///
/// A null Type is treated as absent, so `meet(Type(), t)` is `t`. That is not
/// the same as `!ctnative.bottom`, which is a type: it exists so a caller
/// folding over a range does not need a seed.
///
/// The rules, in the order the implementation applies them:
///
///   * `bottom` is the identity, `boxed` absorbs, `json` absorbs everything
///     except `boxed`.
///   * `opt` is a lift and is hoisted outermost: `meet(opt<T>, U)` is
///     `opt<meet(T, U)>`, `opt<opt<T>>` collapses, and `opt<boxed>` and
///     `opt<json>` collapse into their element because both already hold the
///     absent case.
///   * two `num` meet at the wider `NumKind`, two `str` (or `strview`) at the
///     wider `StrEncoding`, and a `strview` meeting a `str` gives the `str`:
///     widening a view into an owning string is always possible, and the
///     reverse needs a lifetime proof the compiler does not have.
///   * containers meet with their own kind, elementwise. `owned` widens into
///     `shared`; `weak` is unordered against both, deliberately.
///   * two callables (`closure` or `closure_set`) meet at the set of their
///     targets, and at `boxed` past `kMaxClosureSetTargets`.
///   * anything else is a union: flattened, merged pairwise, deduplicated,
///     ordered by printed form so the answer is one MLIR type and not two
///     equivalent ones, and collapsed to `json` past
///     `kMaxVariantAlternatives`.
///
/// The result is never narrower than either input. The type oracle
/// (ctcompile/test/Analysis/Types/Oracle/) checks this against the running
/// interpreter, which is why `meet` returns `num<i32>` only for two `i32`s.
///
/// A widened string encoding keeps its observable answers. `str<utf8>` and
/// `str<utf16>` describe the same JavaScript values, so meeting them at
/// `utf16` is sound as a statement about values, and both count `.length` in
/// UTF-16 code units (utf8 through `ctnative::string_length`). Obligation O-1 in
/// ctcompile/docs/native-divergences.md records when that was not so.
mlir::Type meet(mlir::Type a, mlir::Type b);

/// `meet` folded over a range. An empty range is `!ctnative.bottom`, which is
/// the identity, so this and the two-argument form cannot disagree.
mlir::Type meet(mlir::MLIRContext * context, ::llvm::ArrayRef<mlir::Type> types);

/// The string type an unproved JavaScript string gets. One caller-visible
/// spelling of `kDefaultStringEncoding`, so no caller has to name the encoding.
StrType defaultStringType(mlir::MLIRContext * context);

} // namespace ctcompile::ctnative
