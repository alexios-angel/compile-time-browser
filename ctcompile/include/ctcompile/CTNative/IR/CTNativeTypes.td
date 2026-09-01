//===- CTNativeTypes.td - the ctnative type lattice, as TypeDefs ----------===//
//
// FIFTEEN TYPES AND NO HAND-WRITTEN C++ TYPE CLASS. Part 24's Stage 53A:
// "every one a TypeDef, no C++."
//
// THE ORDER OF THE FILE IS THE ORDER OF THE LATTICE, bottom to top:
//
//     bottom
//       |
//     bool   num<i32> -> num<i64> -> num<f64>   str<utf8> -> str<utf16>
//       \        \                        |          /
//        `------- variant<T...> (at most four alternatives) -------'
//                        |
//                      json                      (opt<T> lifts any of these)
//                        |
//                      boxed  (top)
//
// The containers - vec, map, set, owned, shared, weak - are not RUNGS on that
// ladder. They meet structurally with their own kind and widen to a variant
// against anything else, which is what makes the ladder finite.
//
// ONE TYPE FROM PART 24's TABLE IS DELIBERATELY ABSENT: `!ctnative.struct<name,
// fields>`. Its parameter is a proved-closed shape, and Stage 56A is the proof:
// "no dynamic key write, no delete, no Object.keys/for-in/JSON.stringify/spread
// over it, no Object.assign into it, and it never flows into a parameter whose
// shape is TOP." A TypeDef written before that analysis exists would have to
// take a field list on trust, and a shape taken on trust is precisely the
// structification bug part 24 §1.1 lists. Phase 56 owns it; the space it will
// occupy in the lattice is documented at the bottom of this file.
//
//===----------------------------------------------------------------------===//
#ifndef CTCOMPILE_CTNATIVE_TYPES
#define CTCOMPILE_CTNATIVE_TYPES

include "ctcompile/CTNative/IR/CTNativeBase.td"
include "ctcompile/CTNative/IR/CTNativeEnums.td"

//===----------------------------------------------------------------------===//
// The two ends of the lattice
//===----------------------------------------------------------------------===//

def CTNative_BottomType : CTNative_Type<"Bottom", "bottom"> {
  let summary = "No value reaches here - the lattice's identity element";
  let description = [{
    The starting point of every dataflow fact and the answer for an
    unreachable definition. `meet(bottom, T)` is `T` for every `T`,
    including `boxed`, which is what makes it the identity and what makes
    the initial state of the Phase 54 analysis writable at all.

    It is NOT `undefined`. `undefined` is a JavaScript VALUE and lowers to
    an empty `opt<T>`; `bottom` is the absence of any value whatever, and no
    C++ declaration corresponds to it.
  }];
}

def CTNative_BoxedType : CTNative_Type<"Boxed", "boxed"> {
  let summary = "The top of the lattice - a `!ctjs.value`, and a refusal";
  let description = [{
    Reaching this type is how the native backend says no. It corresponds
    exactly to `!ctjs.value` and to the runtime's `script::value`, and an
    operation whose result is `boxed` is an operation Phase 63 hands to the
    lexical or EmitC backend instead.

    THAT IS THE DESIGN, NOT A DEFICIENCY. Part 24 §1.2: "A proof that fails
    is a fallback, not a bug. The program still runs." The measurement that
    matters is not how rarely this type appears but whether it ever appears
    where it SHOULD NOT - a value the interpreter observed as monomorphic
    that inference gave up on is a precision gap and goes in the backlog; a
    value inference typed NARROWER than the interpreter observed is a
    soundness violation and is a defect. Phase 54B's oracle separates them.
  }];
}

//===----------------------------------------------------------------------===//
// The scalars
//===----------------------------------------------------------------------===//

def CTNative_BoolType : CTNative_Type<"Bool", "bool"> {
  let summary = "A JavaScript boolean, as a C++ `bool`";
  let description = [{
    The one scalar with no parameter and no refinement chain, because C++
    has exactly one type for it and JavaScript has exactly two values.

    It does NOT meet with `num`. `true + 1` is 2 in JavaScript, but that is
    a COERCION performed by an operator, not evidence that the two share a
    representation; a register holding a boolean on one path and a number on
    the other is a `variant<bool, num<...>>` and its uses have to switch.
    Folding it into the numeric chain would silently make `typeof` wrong.
  }];
}

def CTNative_NumType : CTNative_Type<"Num", "num"> {
  let summary = "A JavaScript number, as one of three C++ arithmetic types";
  let description = [{
    `f64` is the type of every JavaScript number and is always correct.
    `i32` and `i64` are refinements, and part 24 admits them "ONLY behind
    Phase 55's bound proof" - which is the phase that can show the value
    never leaves the range, never sees `-0`, and never sees NaN.

    The counterexample part 24 §1.1 gives is the whole reason the parameter
    is not simply assumed: `for (let i = 0; i < n; i++)` with `n = 2**31`
    overflows an `int32_t` and does not overflow a `double`. So this type
    records what was PROVED, and CTNativeEnums.td orders the three so that
    meeting two of them is a `max` rather than a table.
  }];
  let parameters = (ins EnumParameter<CTNative_NumKind>:$kind);
  let assemblyFormat = "`<` $kind `>`";
  let extraClassDeclaration = [{
    /// The always-correct member of the chain: what a JavaScript number is
    /// before any bound proof narrows it.
    static NumType getDouble(::mlir::MLIRContext * context) {
      return get(context, NumKind::F64);
    }
  }];
}

def CTNative_StrType : CTNative_Type<"Str", "str"> {
  let summary = "An owning JavaScript string, in a stated encoding";
  let description = [{
    THE ENCODING IS A PARAMETER BECAUSE THE ANSWER TO `.length` DEPENDS ON
    IT, and this is the first entry of the declared-divergence list.

    ECMA-262 says a string is a sequence of UTF-16 code units, so `.length`,
    indexing and `charCodeAt` all count code units and lone surrogates are
    legal. `std::string` is bytes. `"\u{1F600}".length` is 2 under UTF-16
    and 4 under UTF-8.

    PART 24 SAYS TO DEFAULT TO utf16 AND THAT IS WRONG FOR THIS ENGINE.
    ctbrowser stores a JS string as UTF-8 bytes and `ctbrowser/docs/script.md`
    records the gap under "The UTF-16 gap"; `unittests/js/string_basics.cpp`
    PINS the current answers, including `"\u{1F600}".length` is 4 with
    "V8: 2" beside it. The differential harness compares a compiled body
    against THIS interpreter, so a native backend that defaulted to UTF-16
    would disagree with its own oracle on every non-ASCII `.length`.

    The default therefore lives in exactly one place -
    `kDefaultStringEncoding` in CTNativeLattice.h - it is `utf8` today, and
    a test asserts it against what the interpreter actually answers. The day
    the engine closes its UTF-16 gap, that constant flips and the test says
    so. See ctcompile/docs/native-divergences.md, entry ND-1.
  }];
  let parameters = (ins EnumParameter<CTNative_StrEncoding>:$encoding);
  let assemblyFormat = "`<` $encoding `>`";
}

def CTNative_StrViewType : CTNative_Type<"StrView", "strview"> {
  let summary = "A non-owning view of a JavaScript string";
  let description = [{
    `std::string_view` or `std::u16string_view`, for read-only parameters
    and for literals, which live in the program image and never need
    copying.

    It carries the same encoding parameter as `str` and for the same reason:
    a view of UTF-8 bytes and a view of UTF-16 code units answer `.length`
    differently, and the view being non-owning changes nothing about that.

    IT IS BELOW `str` IN THE LATTICE, not beside it. A view can always be
    widened into an owning string; an owning string cannot be narrowed into
    a view without a lifetime proof this phase does not have. So meeting the
    two gives `str`, which is the direction that is always safe.
  }];
  let parameters = (ins EnumParameter<CTNative_StrEncoding>:$encoding);
  let assemblyFormat = "`<` $encoding `>`";
}

def CTNative_JsonType : CTNative_Type<"Json", "json"> {
  let summary = "`boost::json::value` - a tagged union the emitted code can switch on";
  let description = [{
    The rung below `boxed`: a value inference could not type, but which
    still has a C++ representation the generated code can interrogate
    without re-entering the runtime.

    `boost::any` IS REJECTED AND THIS IS WHERE THE REASON LIVES. Part 24's
    Stage 53A offers either it or `boost::json::value` for "completely
    un-inferable" values. `boost::any` gives no way to ask what is inside
    without already knowing the type - which is exactly the thing an
    un-inferable value cannot tell you - so every use of it would be a
    guessed `any_cast` and a throw when the guess is wrong.
    `boost::json::value` is a tagged union with a `kind()`, so a switch over
    it is total and checkable. Where even that is not enough, the answer is
    `boxed` and the boxed backend, not a cleverer container.
  }];
}

//===----------------------------------------------------------------------===//
// The lifts and the unions
//===----------------------------------------------------------------------===//

def CTNative_OptType : CTNative_Type<"Opt", "opt"> {
  let summary = "`std::optional<T>` - `T`, or `null`/`undefined`";
  let description = [{
    A LIFT, NOT A RUNG. `opt` does not sit at a height in the chain; it
    wraps whatever height its element has, and meeting `opt<T>` with `U`
    gives `opt<meet(T, U)>`. That rule is what makes nullability travel
    through the lattice without multiplying it by two.

    THE LIFT IS HOISTED OUTERMOST AND KEPT THERE. `opt<opt<T>>` collapses to
    `opt<T>`, and a `variant` may not have an `opt` alternative - the
    verifier on CTNative_VariantType rejects one. The canonical form of "a
    number or a string, possibly absent" is
    `opt<variant<num<f64>, str<utf8>>>` and never
    `variant<opt<num<f64>>, str<utf8>>`. Without that rule meet is not
    associative, and CTNativeLattice.cpp's property test says so by failing.

    `null` AND `undefined` COLLAPSE INTO ONE EMPTY OPTIONAL, and that is a
    real narrowing: `null == undefined` is true but `null === undefined` is
    false, and `typeof` tells them apart. So a value whose distinction
    between the two is OBSERVED cannot use this type, which is Phase 54's
    obligation to check and not something the type can enforce about itself.
  }];
  let parameters = (ins "::mlir::Type":$elementType);
  let assemblyFormat = "`<` $elementType `>`";
}

def CTNative_VariantType : CTNative_Type<"Variant", "variant"> {
  let summary = "`std::variant<T...>` - the polymorphic fallback below `json`";
  let description = [{
    Where two unrelated static types meet. A register that holds a boolean
    on one path and a string on the other is genuinely both, and a
    `std::variant` says so in a form the emitted C++ can `visit`.

    IT IS CAPPED, and the cap is in one place -
    `kMaxVariantAlternatives` in CTNativeLattice.h. Beyond it the answer is
    `json`. Two reasons, and neither is arithmetic: a five-way `std::variant`
    in generated code is not what part 24 means by "looks and performs as if
    it were originally written in C++", and an uncapped union makes the
    lattice infinitely tall, so the Phase 54 dataflow analysis has no
    termination argument. A finite height is what makes it terminate.

    THE CANONICAL FORM IS ENFORCED BY THE VERIFIER, not by convention: at
    least two alternatives, all distinct, and none of them `variant`,
    `bottom`, `opt`, `json` or `boxed`. Each of those five would be a
    different spelling of a type that already exists - `variant` nests,
    `bottom` and `opt` lift out, `json` and `boxed` absorb - and a lattice
    with two spellings for one element does not have a working `==`.

    The alternatives are ORDERED by their printed form so that
    `meet(a, b)` and `meet(b, a)` are the same MLIR type and not merely
    equivalent ones. MLIR uniques types structurally, so an unstable order
    would make commutativity fail at the pointer level while looking correct
    in a printout.
  }];
  let parameters = (ins ArrayRefParameter<"::mlir::Type">:$alternatives);
  let assemblyFormat = "`<` $alternatives `>`";
  let genVerifyDecl = 1;
}

//===----------------------------------------------------------------------===//
// The containers. Phase 57 and part 24 §8 own their operations; the types are
// here because the lattice has to be able to name them.
//===----------------------------------------------------------------------===//

def CTNative_VecType : CTNative_Type<"Vec", "vec"> {
  let summary = "`std::vector<T>` - a dense JavaScript array of uniform element type";
  let description = [{
    TWO PROOFS STAND BETWEEN A JS ARRAY AND THIS TYPE, and Phase 57 owns
    both. Uniformity, because `std::vector` has one element type and a JS
    array has none; and DENSITY, because a JS array is sparse-capable -
    `a[100] = 1` on an empty array gives `length` 101 and one element, so
    `.length` is not `.size()` in general.

    The type records the conclusion. It cannot record the proof, which is
    why every construction of it in a later phase has to be able to point at
    one.
  }];
  let parameters = (ins "::mlir::Type":$elementType);
  let assemblyFormat = "`<` $elementType `>`";
}

def CTNative_MapType : CTNative_Type<"Map", "map"> {
  let summary = "`std::unordered_map<K, V>` - a JavaScript `Map`, or an open object shape";
  let description = [{
    Part 24 §8's row. Also the answer for an object whose shape Stage 56A
    could NOT close: "Anything else and it stays a map."

    A JS `Map` KEYS BY SameValueZero AND `unordered_map` KEYS BY `==` PLUS A
    HASH, and the two disagree on exactly the values this project has
    already been careful about elsewhere: `NaN` is its own key in a JS Map
    and is never equal to itself under `==`, and `0` and `-0` are the SAME
    key in a JS Map while `Object.is` distinguishes them. Phase 61 owes a
    key adaptor or a refusal; the type does not pretend the question is
    settled.
  }];
  let parameters = (ins "::mlir::Type":$keyType, "::mlir::Type":$valueType);
  let assemblyFormat = "`<` $keyType `,` $valueType `>`";
}

def CTNative_SetType : CTNative_Type<"Set", "set"> {
  let summary = "`std::unordered_set<T>` - a JavaScript `Set`";
  let description = [{
    Part 24 §8's row, and it inherits `map`'s key problem verbatim: a JS
    `Set` uses SameValueZero, so `NaN` is a member of `new Set([NaN])` and
    `-0` and `0` are one member. Same obligation, same owner.
  }];
  let parameters = (ins "::mlir::Type":$elementType);
  let assemblyFormat = "`<` $elementType `>`";
}

//===----------------------------------------------------------------------===//
// The three smart pointers. Phase 55 owns the escape analysis that CHOOSES
// between them; these are the vocabulary that analysis will speak in.
//===----------------------------------------------------------------------===//

def CTNative_OwnedType : CTNative_Type<"Owned", "owned"> {
  let summary = "`std::unique_ptr<T>` - one owner, proved";
  let description = [{
    Stage 55B's preferred outcome for anything that escapes its frame at
    all: stack first, then this, then `shared`.
  }];
  let parameters = (ins "::mlir::Type":$pointeeType);
  let assemblyFormat = "`<` $pointeeType `>`";
}

def CTNative_SharedType : CTNative_Type<"Shared", "shared"> {
  let summary = "`std::shared_ptr<T>` - several owners, none of them provably last";
  let description = [{
    `owned` WIDENS INTO THIS AND NOT THE OTHER WAY. A unique owner can
    always be made shared; a shared owner cannot be made unique without
    proving no other reference exists, which is the thing the analysis
    failed to prove if it produced `shared` in the first place. That is the
    only ordering relation among the three pointers, and the lattice
    implements exactly it.
  }];
  let parameters = (ins "::mlir::Type":$pointeeType);
  let assemblyFormat = "`<` $pointeeType `>`";
}

def CTNative_WeakType : CTNative_Type<"Weak", "weak"> {
  let summary = "`std::weak_ptr<T>` - ONLY where the JavaScript said `WeakRef`";
  let description = [{
    THIS TYPE MAY NOT BE INTRODUCED BY THE COMPILER TO BREAK A CYCLE, and
    Stage 55C is unusually blunt about why: "a tracing collector keeps a
    cycle alive as long as any member is reachable from a root, and
    `weak_ptr` does not. The weak side can be destroyed while JavaScript
    would still see it."

    So a proved cycle is a REFUSAL - it falls back to the boxed backend and
    its real collector - or, where the cycle is proved confined to one
    frame, a stack allocation. `weak` is reachable only from source that
    said `WeakRef` or `WeakMap`, which is the programmer declaring the
    lifetime rather than the compiler guessing it.

    IT IS UNORDERED AGAINST THE OTHER TWO. `owned` and `shared` are both
    strong and one widens into the other; `weak` is a different kind of
    reference, so meeting it with either gives a `variant` and not a
    pointer. That is deliberately awkward, because silently unifying a
    strong and a weak reference is the miscompile above.
  }];
  let parameters = (ins "::mlir::Type":$pointeeType);
  let assemblyFormat = "`<` $pointeeType `>`";
}

// WHERE `!ctnative.struct<name, fields>` WILL GO, when Phase 56 has the proof
// that lets it be written: beside `map` in the lattice and strictly below it,
// since a closed shape is an open one that something was proved about. Meeting
// two DIFFERENT closed shapes gives `map<str, variant<...>>` - never a third
// struct, because JavaScript's `{a: 1}` and `{a: 1, b: 2}` are two shapes and
// not a base class and a derived one.

#endif // CTCOMPILE_CTNATIVE_TYPES
