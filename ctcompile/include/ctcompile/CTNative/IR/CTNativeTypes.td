//===- CTNativeTypes.td - The ctnative type lattice, as TypeDefs ----------===//
//
// Every ctnative type is declared here as a TypeDef; there is no hand-written
// C++ type class.
//
// The scalar types form this lattice, bottom to top:
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
// strview sits below str of the same encoding. The containers (vec, tuple,
// map, set, owned, shared, weak) are not rungs on that ladder: they meet
// structurally with their own kind and widen to a variant against anything
// else, which keeps the ladder finite. symbol, dom_element, object_identity,
// global_object, method_table and closure are proof-gated nominal types
// outside the chain: they meet only with themselves and widen to a variant
// against anything else.
//
// `!ctnative.struct<name, fields>` is deliberately absent. A struct needs a
// proof that the object's shape is closed (no dynamic key write, no delete, no
// Object.keys/for-in/JSON.stringify/spread over it, no Object.assign into it,
// never passed where the shape is unknown). Without that analysis a field list
// would be taken on trust, which miscompiles. Where it will sit in the lattice
// is noted at the bottom of this file.
//
//===----------------------------------------------------------------------===//
#ifndef CTCOMPILE_CTNATIVE_TYPES
#define CTCOMPILE_CTNATIVE_TYPES

include "ctcompile/CTNative/IR/CTNativeBase.td"
include "ctcompile/CTNative/IR/CTNativeEnums.td"

// A type with one parameter, spelled `<parameter>`.
class CTNative_WrapperType<string name, string typeMnemonic, dag parameter>
    : CTNative_Type<name, typeMnemonic> {
  let parameters = parameter;
  let assemblyFormat = "`<` $" # !getdagname(parameter, 0) # " `>`";
}

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

def CTNative_DOMElementType : CTNative_Type<"DOMElement", "dom_element"> {
  let summary = "A borrowed ctbrowser document and element node handle";
  let description = [{
    Introduced by a live, fingerprint-bound native DOM entry proof, or by an
    ordinary script's page reads (TypeInference::domReadType), where the
    carrier is the runtime's ctnative::js_element_t view instead.
    The document owns the node and outlives the call; identity includes both
    the document address and node_id. This is neither an owning JavaScript
    object nor a numeric node ID. A local closest result may be a canonical
    empty handle denoting null; live proof permits only identity comparisons
    of that nullable result, never dereference. Retention and borrowed function
    results are refused.
  }];
}

def CTNative_DOMRectType : CTNative_Type<"DOMRect", "dom_rect"> {
  let summary = "A DOMRect's geometry, as ctnative::js_dom_rect";
  let description = [{
    The answer of an ordinary script's `el.getBoundingClientRect()` on a
    proved page element: left, top, width and height as Numbers, copied out
    of the laid-out page (Runtime/Page.hpp). A snapshot, like the VM's DOMRect,
    so it outlives later layout. It meets only itself.
  }];
}

def CTNative_ComputedStyleType : CTNative_Type<"ComputedStyle", "computed_style"> {
  let summary = "The result of getComputedStyle, as ctnative::js_computed_style";
  let description = [{
    The answer of an ordinary script's `getComputedStyle(el)` on a proved page
    element. Live, like the VM's object: getPropertyValue computes from the
    page as it stands (Runtime/Page.hpp). It borrows the page and names the
    element. It meets only itself.
  }];
}

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

def CTNative_SymbolType : CTNative_Type<"Symbol", "symbol"> {
  let summary = "A JavaScript primitive Symbol identity";
  let description = [{
    Introduced by a live intrinsic proof for a standard well-known Symbol read.
    Identity is distinct from String descriptions and from ordinary objects.
    The carrier is ctnative::js_symbol_t; coercion, symbol-keyed properties and
    mixed unions require separate admission proofs.
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

    PART 24 SAYS TO DEFAULT TO utf16. ctbrowser stores a JS string as WTF-8
    and, since 2b710a0d2, counts `.length` and indices in UTF-16 code units
    over those bytes, as ECMA-262 does. So the default carrier stays utf8 and
    a utf8 `.length` is `ctnative::string_length`, a code-unit count, not
    `.size()`. The default lives in exactly one place -
    `kDefaultStringEncoding` in CTNativeLattice.h - and a test asserts the
    native count against what the interpreter actually answers. See
    ctcompile/docs/native-divergences.md, entry ND-1.

    THE C++ CARRIER FOLLOWS THE ENCODING, and `cppCarrier()` below is the one
    place that says so. Part 24 Stage 53D Step 4: the reason to move a string
    to `utf16` is that `.length` was observed, and `.length` is only `.size()`
    when the container's ELEMENT is a UTF-16 code unit. A `str<utf16>` carried
    in a `std::string` would answer in bytes and be exactly the bug the
    encoding parameter exists to prevent.

    `std::u32string` IS NOT AN OPTION HERE and Step 5 says why: a `char32_t`
    is a code point, so it would answer `1` for `"\u{1F600}".length`, not
    the 2 that ECMA-262 and this engine answer. It is the right type
    for iterating a string BY CODE POINT, which is what `for...of` does, and
    that is Phase 57's business rather than this type's.
  }];
  let parameters = (ins EnumParameter<CTNative_StrEncoding>:$encoding);
  let assemblyFormat = "`<` $encoding `>`";
  let extraClassDeclaration = [{
    /// The C++ type that carries a string in this encoding. See the
    /// description: `utf16` is `std::u16string` and not `std::string`.
    ::llvm::StringRef cppCarrier() const {
      return getEncoding() == StrEncoding::UTF16 ? "std::u16string" : "std::string";
    }
  }];
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
  let extraClassDeclaration = [{
    /// The C++ type that carries a view in this encoding - the same rule as
    /// CTNative_StrType::cppCarrier, one `_view` further along.
    ::llvm::StringRef cppCarrier() const {
      return getEncoding() == StrEncoding::UTF16 ? "std::u16string_view"
                                                 : "std::string_view";
    }
  }];
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

def CTNative_OptType : CTNative_WrapperType<"Opt", "opt", (ins "::mlir::Type":$elementType)> {
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
    associative, and ctcompile/test/Analysis/Types/Lattice.cpp's property test says so by failing.

    `null` AND `undefined` COLLAPSE INTO ONE EMPTY OPTIONAL, and that is a
    real narrowing: `null == undefined` is true but `null === undefined` is
    false, and `typeof` tells them apart. So a value whose distinction
    between the two is OBSERVED cannot use this type, which is Phase 54's
    obligation to check and not something the type can enforce about itself.
  }];
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
// The containers. Their operations are lowered elsewhere; the types are here
// because the lattice has to be able to name them.
//===----------------------------------------------------------------------===//

def CTNative_VecType : CTNative_WrapperType<"Vec", "vec", (ins "::mlir::Type":$elementType)> {
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
}

def CTNative_TupleType : CTNative_Type<"Tuple", "tuple"> {
  let summary = "`std::pair<T0, T1>` - a short array literal read only at constant indices";
  let description = [{
    A JavaScript array used as a fixed-size record, such as the
    `[name, value]` rows css-dump.js keeps in a list. The proof that an
    array is one (its length never changes and every read uses a constant
    index) is the element-vector proof's, in
    lib/CTNative/Analysis/Inference/Vectors.cpp; the type records the
    conclusion, one element type per position.

    IT MEETS POSITION BY POSITION with a tuple of the same arity, like the
    other containers meet with their own kind, and widens to a variant
    against anything else, including a tuple of another arity. The C++
    spelling is `std::pair` because only two positions are lowered; the
    verifier allows any arity of at least two so the lattice does not have
    to change when a wider record is lowered.
  }];
  let parameters = (ins ArrayRefParameter<"::mlir::Type">:$elements);
  let assemblyFormat = "`<` $elements `>`";
  let genVerifyDecl = 1;
}

def CTNative_MapType : CTNative_Type<"Map", "map"> {
  let summary = "a typed JavaScript `Map`, or an open object shape";
  let description = [{
    Part 24 §8's row. Also the answer for an object whose shape Stage 56A
    could NOT close: "Anything else and it stays a map."

    A JS `Map` KEYS BY SameValueZero AND `unordered_map` KEYS BY `==` PLUS A
    HASH, and the two disagree on exactly the values this project has
    already been careful about elsewhere: `NaN` is its own key in a JS Map
    and is never equal to itself under `==`, and `0` and `-0` are the SAME
    key in a JS Map while `Object.is` distinguishes them. The first native
    Map lowering proves confined standard instances with one primitive key
    carrier and numeric values, using an owning insertion-ordered entry list
    with SameValueZero lookup. Other Map schemas and open object shapes still
    need a representation proof or a refusal; the type alone is not one.
  }];
  let parameters = (ins "::mlir::Type":$keyType, "::mlir::Type":$valueType);
  let assemblyFormat = "`<` $keyType `,` $valueType `>`";
}

def CTNative_DictType : CTNative_WrapperType<"Dict", "dict", (ins "::mlir::Type":$elementType)> {
  let summary = "`std::shared_ptr<ctnative::dictionary<V>>` - an object used as a table";
  let description = [{
    An object literal that a program uses only as a string-keyed table:
    every key may be computed, every value has the one type V, and the
    object is otherwise only deleted from, tested with `in` or enumerated.
    Bootstrap's EventHandler keeps its listener registry this way. The
    proof is prepareNativeDictionaries (Analysis/NativeDictionary.h); the
    type records its conclusion, the value type V, which is the element
    type here. A dictionary is an object, so a value of this type is a
    reference to one table, never a copy. Two dictionaries meet value type
    by value type, like the other one-element containers.
  }];
}

def CTNative_SetType : CTNative_WrapperType<"Set", "set", (ins "::mlir::Type":$elementType)> {
  let summary = "`std::unordered_set<T>` - a JavaScript `Set`";
  let description = [{
    Part 24 §8's row, and it inherits `map`'s key problem verbatim: a JS
    `Set` uses SameValueZero, so `NaN` is a member of `new Set([NaN])` and
    `-0` and `0` are one member. Same obligation, same owner.
  }];
}

def CTNative_ObjectIdentityType : CTNative_Type<"ObjectIdentity", "object_identity"> {
  let summary = "An owning identity for a proved closed object in native Map flows";
  let description = [{
    Only proved fresh empty allocations receive this type. Closed calls,
    structured control and Map payloads may join it with primitive alternatives;
    the lattice retains those alternatives rather than declaring them objects.
    Ordinary scalar fields require a separate closed-use and receiver proof;
    reflection, mutable capture and host identity remain refused.
    Equality, truthiness and typeof preserve observable identities and tags;
    coercions that might invoke object hooks remain refused by native admission.
    The carrier owns a distinct allocation with scalar leaves; aliases and Map entries
    retain it, so keys remain alive and separate allocations remain unequal.
  }];
}

def CTNative_GlobalObjectType
    : CTNative_WrapperType<"GlobalObject", "global_object", (ins StringRefParameter<>:$binding)> {
  let summary = "An ordinary object retained by a checked module global owner";
  let description = [{
    The binding identifies one source allocation in a live OwnedGlobalRoots
    query. It does not identify every object with the same field schema.
    Native lowering must independently consume that query for each global
    load and field access; this type or an input annotation never authorizes
    ownership. The source allocation still escapes through StoredGlobal.
  }];
}

def CTNative_StaticTableType
    : CTNative_WrapperType<"StaticTable", "static_table", (ins StringRefParameter<>:$binding)> {
  let summary = "The constant table a run-once static variable holds";
  let description = [{
    The binding names one static variable of a run-once function that
    markStaticTables (Analysis/Inference/StaticTables.cpp) proved holds one
    object literal of constants, never written after it is made. Its C++
    carrier is a pointer to the one namespace-scope `const` struct, so a copy
    of the value is the same object, as in JavaScript. The variable holds
    undefined, a null pointer, until the run-once function stores the table.
  }];
}

def CTNative_MethodTableType
    : CTNative_WrapperType<"MethodTable", "method_table", (ins StringRefParameter<>:$site)> {
  let summary = "An owning closed table of immutable stored callable fields";
  let description = [{
    The site is a nominal schema identity proved across all closed arguments
    and returns. Every field is initialized once before publication or use.
    Fields own concrete std::function values; object inspection, replacement,
    deletion, open boundaries and cyclic captures remain refused.
  }];
}

def CTNative_ClosureType
    : CTNative_WrapperType<"Closure", "closure", (ins StringRefParameter<>:$target)> {
  let summary = "A proved monomorphic callable with an owning capture environment";
  let description = [{
    The target is a nominal identity, not a C++ function pointer. The native
    returned-closure proof checks every producer and use before introducing
    this type. Immutable numbers, booleans, strings and owning Map handles
    are captured by value. Returned function values use tuples and direct
    invocation; proved stored method fields use std::function with a concrete
    signature and an explicit owning lambda capture list. Function identity
    inspection, mixed targets and mutable binding captures remain refused.

    Capture types are inferred from the creation site and the lifted target's
    parameters. They are not recursively embedded in this nominal type, so a
    recursive call graph cannot grow the type lattice indefinitely.
  }];
}

//===----------------------------------------------------------------------===//
// The three smart pointers. Escape analysis
// (ctcompile/CTNative/Analysis/EscapeAnalysis.h) chooses between them.
//===----------------------------------------------------------------------===//

def CTNative_OwnedType : CTNative_WrapperType<"Owned", "owned", (ins "::mlir::Type":$pointeeType)> {
  let summary = "`std::unique_ptr<T>` - one owner, proved";
  let description = [{
    Stage 55B's preferred outcome for anything that escapes its frame at
    all: stack first, then this, then `shared`.
  }];
}

def CTNative_SharedType
    : CTNative_WrapperType<"Shared", "shared", (ins "::mlir::Type":$pointeeType)> {
  let summary = "`std::shared_ptr<T>` - several owners, none of them provably last";
  let description = [{
    `owned` WIDENS INTO THIS AND NOT THE OTHER WAY. A unique owner can
    always be made shared; a shared owner cannot be made unique without
    proving no other reference exists, which is the thing the analysis
    failed to prove if it produced `shared` in the first place. That is the
    only ordering relation among the three pointers, and the lattice
    implements exactly it.
  }];
}

def CTNative_WeakType : CTNative_WrapperType<"Weak", "weak", (ins "::mlir::Type":$pointeeType)> {
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
}

// Where `!ctnative.struct<name, fields>` will go once a closed-shape proof can
// introduce it: beside `map` in the lattice and strictly below it, since a
// closed shape is an open one that something was proved about. Meeting two
// different closed shapes gives `map<str, variant<...>>`, never a third
// struct, because JavaScript's `{a: 1}` and `{a: 1, b: 2}` are two shapes, not
// a base class and a derived one.

#endif // CTCOMPILE_CTNATIVE_TYPES
