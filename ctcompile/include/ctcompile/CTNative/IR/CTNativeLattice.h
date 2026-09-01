#pragma once
// THE TYPE LATTICE. Stage 53B, and the one file in this phase that is C++.
//
// Part 24 §1.2 of the lexical implementation plan exempts analyses from the
// ODS-first rule - "DRR matches a pattern, it does not run a dataflow analysis"
// - and a meet over an unbounded family of parameterized types is arithmetic on
// types, which TableGen has no way to express. So this is the declared
// exception, and it is kept as small as the job allows: one function, one
// header, one translation unit.
//
// WHY IT MATTERS THAT IT IS ONE. Part 24, Stage 53B:
//
//   "Meet is a function, and it must be written once, because two places
//    computing 'what type is this' differently is how a transpiler produces a
//    program that is right on Tuesday."
//
// Everything downstream - Phase 54's inference, Phase 55's ownership, Phase
// 57's element type, Phase 63's per-function selection - calls THIS. Part 24
// §A.2 makes it a rule for the other agents too: "Nobody edits the type lattice
// but the types agent. Two meets is the bug this whole architecture exists to
// avoid."
//
// A NOTE ON THE NAME, WHICH IS THE PLAN'S AND NOT THE BEST ONE. In the
// ordering part 24 writes - `bottom -> i32 -> i64 -> f64 -> variant<...> ->
// json -> top(boxed)` - `bottom` is "nothing reached here" and `boxed` is the
// safe answer, so what this function computes is the LEAST UPPER BOUND: the
// smallest type correct for both inputs. Dataflow literature would call that a
// JOIN. It is a meet in the older compiler-textbook sense, where confluence
// moves toward the conservative answer, and that is the sense part 24 uses.
//
// It keeps the plan's name anyway, because ONE name for this function is worth
// more than a better name for it - which is the same reason there is only one
// of it.
#include "ctcompile/CTNative/IR/CTNativeTypes.h"

#include "mlir/IR/Types.h"
#include "llvm/ADT/ArrayRef.h"

namespace ctcompile::ctnative {

/// How many alternatives a `!ctnative.variant` may carry before the lattice
/// gives up and answers `!ctnative.json`.
///
/// FOUR, AND THE NUMBER IS A POLICY RATHER THAN A MEASUREMENT. Two reasons,
/// neither arithmetic. A five-way `std::variant` in generated code is not what
/// part 24 means by C++ that "looks and performs as if it were originally
/// written in C++"; and an uncapped union makes the lattice INFINITELY TALL, so
/// Phase 54's dataflow analysis would have no termination argument. The cap is
/// what makes the lattice finite-height, and finite height is what makes the
/// analysis terminate.
///
/// IT IS LOAD-BEARING AND THAT WAS CHECKED, not assumed: replacing the cap's
/// condition with `false` leaves the meet table, commutativity and all 9261
/// associativity triples green, and turns exactly one check red - "the fifth
/// falls off the cap into json" in ctcompile/test/CTNativeLattice.cpp.
inline constexpr unsigned kMaxVariantAlternatives = 4;

/// The C++ representation a JavaScript string gets when nothing has proved a
/// narrower one.
///
/// PART 24 SAYS utf16 HERE AND IT IS WRONG FOR THIS ENGINE. Its Stage 53D
/// reasons from ECMA-262 - a JS string is a sequence of UTF-16 code units, so
/// `"\u{1F600}".length` is 2 - and concludes that defaulting to UTF-16 is
/// "semantics-preserving and boring".
///
/// It is neither, here. ctbrowser stores a JavaScript string as UTF-8 BYTES.
/// `ctbrowser/docs/script.md` records the gap under "The UTF-16 gap" and
/// `ctbrowser/unittests/js/string_basics.cpp` PINS the current answers with
/// V8's beside them - `"\u{1F600}".length` is 4 here and 2 there. The
/// differential harness compares a compiled body against THIS interpreter, and
/// part 24 §A.2 makes that the standard for every phase in the track: "Every
/// phase's gate is a comparison against the interpreter."
///
/// So the semantics-preserving default is the one the engine already has.
/// Flipping this constant to UTF16 makes `stringLengthPinnedToInterpreter` in
/// ctcompile/test/CTNativeLattice.cpp fail, and it fails by asking the real
/// interpreter rather than by comparing against a number written down here.
///
/// The day the engine closes its UTF-16 gap - `string_basics.cpp` calls its
/// pinned block "the acceptance list for a UTF-16 migration" - this constant is
/// the whole change on this side.
inline constexpr StrEncoding kDefaultStringEncoding = StrEncoding::UTF8;

/// THE meet. The smallest type that is correct for both `a` and `b`.
///
/// A null Type is treated as absent, so `meet(Type(), t)` is `t`. That is not
/// the same as `!ctnative.bottom`, which is a type: it exists so a caller
/// folding over a range does not need a seed.
///
/// The rules, in the order the implementation applies them:
///
///   * `bottom` is the identity, `boxed` absorbs, `json` absorbs everything
///     except `boxed`.
///   * `opt` is a LIFT and is hoisted outermost: `meet(opt<T>, U)` is
///     `opt<meet(T, U)>`, `opt<opt<T>>` collapses, and `opt<boxed>` and
///     `opt<json>` collapse into their element because both already hold the
///     absent case.
///   * two `num` meet at the wider `NumKind`, two `str` (or `strview`) at the
///     wider `StrEncoding`, and a `strview` meeting a `str` gives the `str` -
///     widening a view into an owning string is always available, and the
///     reverse needs a lifetime proof this phase does not have.
///   * containers meet with their own kind, elementwise. `owned` widens into
///     `shared`; `weak` is unordered against both, deliberately.
///   * anything else is a union - flattened, merged pairwise, deduplicated,
///     ordered by printed form so the answer is one MLIR type and not two
///     equivalent ones, and collapsed to `json` past
///     `kMaxVariantAlternatives`.
///
/// WHAT IT IS NOT ALLOWED TO DO is answer NARROWER than either input. That is
/// the property Phase 54B's oracle checks against the running interpreter, and
/// it is why `meet` never returns `num<i32>` for anything but two `i32`s.
///
/// A STRING ENCODING THAT WIDENS CHANGES AN OBSERVABLE ANSWER, and the lattice
/// cannot prevent it. `str<utf8>` and `str<utf16>` describe the same JavaScript
/// values - so meeting them at `utf16` is sound as a statement about VALUES -
/// but `.length`, indexing and `charCodeAt` answer differently in the two. The
/// obligation therefore falls on Phase 54: on a path where any of those is
/// observed, a string whose encoding would widen must go to `boxed` instead.
/// Recorded as an obligation in ctcompile/docs/native-divergences.md rather
/// than silently assumed.
mlir::Type meet(mlir::Type a, mlir::Type b);

/// `meet` folded over a range. An empty range is `!ctnative.bottom`, which is
/// the identity - so this and the two-argument form cannot disagree.
mlir::Type meet(mlir::MLIRContext * context, ::llvm::ArrayRef<mlir::Type> types);

/// The string type an unproved JavaScript string gets. One caller-visible
/// spelling of `kDefaultStringEncoding`, so no phase has to name the encoding.
StrType defaultStringType(mlir::MLIRContext * context);

/// Does this type belong to the ctnative dialect?
///
/// The question every consumer of the StaticTyped interface actually asks: not
/// "is there a type" - there always is - but "did anything prove it". A
/// `!ctjs.value` or a builtin `i32` answers false.
bool isNativeType(mlir::Type type);

} // namespace ctcompile::ctnative
