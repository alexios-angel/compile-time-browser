[Back to script.md](../script.md)

### `"a" < "b"` was `false`

All four relational opcodes were `to_number(a) < to_number(b)`. ToNumber of a
non-numeric string is NaN and every comparison against NaN is false, so **every
relational comparison between two strings was false** — `<`, `>`, `<=` and `>=`
alike. `["b","a","c"].sort((x, y) => x < y ? -1 : 1)` handed back its input
untouched. `===` was unaffected, and the default `sort()` compares in C++, which
is why this survived three JS corpora.

`context::compare_relational` is 7.2.13 properly: ToPrimitive both sides with the
NUMBER hint, compare as TEXT when both are strings, numerically otherwise. It
returns `std::partial_ordering` so all four operators are one comparison asked
four ways, and `unordered` — the specification's `undefined` — makes each of them
false, which is exactly the required NaN behaviour.

### `s.at(undefined)` hung the engine

`"abc".at(NaN)` cast NaN to `std::size_t`, which is undefined behaviour, and the
engine **hung** — no crash, no error, just a page that stopped. A missing
argument is `undefined` and ToNumber(undefined) is NaN, so `.at()`,
`.at(undefined)` and `.at({})` all reached it, as did `.substr(NaN)`.

The cause was that this file had no **ToIntegerOrInfinity** (7.1.5), which is the
coercion every string index is specified to go through. `index_at` is that, and
NaN becoming zero is the whole point of it. The range checks were also rewritten
from `i < 0 || i >= size` — both false for NaN, so it fell through to an
out-of-bounds read — to `!(i >= 0 && i < size)`, which no NaN survives. That
belt-and-braces matters: planting the coercion bug back now produces wrong
answers rather than a hang.

### The rest

| | was | is |
|---|---|---|
| `"abc".indexOf("a", 1)` | `0` | `-1` — the position was ignored by all five of indexOf/lastIndexOf/includes/startsWith/endsWith |
| `"abc".slice(1, undefined)` | `""` | `"bc"` — an explicit `undefined` end means "to the end", and a count test cannot tell it from a supplied 0 |
| `"abc".charAt(-1)` | `"a"` | `""` — negatives were clamped to 0; that is `at`'s job, not `charAt`'s |
| `"abc".charCodeAt(-1)` | `97` | `NaN` |
| `"a-b-c".split("-", 2)` | all three | `["a","b"]` — the limit was ignored entirely |
| `"abc".indexOf()` | `0` | `-1` — a missing needle is `"undefined"`, not `""` |

### Known and NOT fixed

60 differences remain. Eight are **deliberate**: `core/algorithms.hpp` folds ASCII
only, so `"Straße".toUpperCase()` is `"STRAßE"` where V8 gives `"STRASSE"`, and
`localeCompare` orders by byte. That is the same determinism argument the file
already makes — a table- or locale-driven fold would make a byte-compared golden
depend on the host.

The rest are real and untouched: `replace`'s `$`-patterns and its empty-pattern
case (30); `search` with a string argument, which is specified to build a RegExp
and instead returns -1 (10); `replaceAll` not throwing TypeError for a non-global
regexp (3); and the absent `String.raw`, string boxing (`typeof new String(1)` is
`"string"`), `Symbol.iterator`, `isWellFormed`/`toWellFormed`, and string index
properties (`Object.keys("abc")` is empty).

### The UTF-16 gap

**A JS string is a sequence of UTF-16 code units; this engine stores UTF-8
bytes.** So `"é".length` is 2 here and 1 in V8, a non-BMP emoji is 4 and 2, and
`charCodeAt` returns a byte. Closing it means changing `string_object`
engine-wide and touching every method that takes an index.

`ctbrowser/unittests/js/string_basics.cpp` **pins the current (wrong) answers** in a labelled
section rather than omitting them, with V8's answer in each comment. That is the
acceptance list for a migration: the day the representation changes, those lines
fail and say exactly what to update.


## The bugs those suites found, fixed (2026-08-09)

Six defects, one of them in the ctjs submodule. Each was planted back and the
tests caught it.

### `for (let i = ...)` now binds per iteration

A C-style `for` with a `let` head gives every iteration its own binding, so
closures made in the body capture 0, 1, 2 - where `var` shares one and they all
capture 3. This engine had only the `var` behaviour. `for (let x of ...)` was
already correct, so it was specifically the C-style loop, and modern minified
output leans on the difference constantly.

`compile_for` collects the BOXED locals the init opened - only a boxed one can
be observed by a closure, and `var` is hoisted to the function scope so it never
appears among them, which keeps the two loops apart without tracking declaration
kinds. Between the body and the update (ForBodyEvaluation step 3.e) it reads
each cell, moves the value to a raw register and re-boxes it. Putting it after
the update instead would shift every captured value by one. `continue` lands on
that instruction, so it flows through the copy too.

### ToNumber of an object goes through ToPrimitive

`context::to_number` is static and cannot re-enter the VM to call `valueOf`, so
it answered NaN for every object. `to_number_value` always did it properly and
was private. It is public now, and the built-ins whose spec text reads
`? ToNumber(x)` use it: `Number([])` is 0, `Math.abs([])` is 0,
`Math.max([1],[2])` is 2.

`loose_equals` needed the same and became a member to get it. Two subtleties
worth keeping: an object compared against a primitive is ToPrimitive'd and the
comparison RETRIED (7.2.15 steps 10-11), guarded against re-entering forever by
the fact that `to_primitive` hands the object back unchanged when neither
`valueOf` nor `toString` yields a primitive; and **a string is on the heap in
this engine too**, so "both on the heap means compare identity" was the wrong
test - it caught `"" == []` and answered false before ToPrimitive ever ran.

That one defect was eleven of the type sweep's nineteen differences; the sweep
now reads 4.

### The rest

* **`0o17`, `0b101`, `1_000`** did not lex. ctjs special-cased 0x alone and
  stopped a number token at `_`. Fixed in the submodule, with cases in its own
  `tests/vparse.cpp`; `number_literal` strips the separators here, because
  `std::from_chars` accepts none.
* **`parseInt("0xFF")` was 0** - it stopped at the `x`, which is how a colour
  parser reads black without erroring. A leading 0x is hexadecimal when no
  radix is demanded (19.2.5 step 8).

### Still known and not fixed

`Object.is`, `String(function)` returning source text, `String(Symbol)`,
legacy octal (`"\101"`, `017` - Annex B), and the String gaps already listed
above: `replace`'s `$`-patterns, `search` with a string argument, `replaceAll`'s
missing TypeError, `String.raw`, boxing, `Symbol.iterator`. All pinned in the
test files with V8's answer in the comment.


## What the per-type suites found, fixed (2026-08-09)

Six defects across four areas. Each planted back individually and caught.

* **`JSON.stringify` emitted invalid JSON.** NaN came out as `NaN` and the
  infinities as `Infinity`, which no JSON parser will read back - so a page
  round-tripping its own data through `JSON.parse` got a SyntaxError from bytes
  this engine wrote. 25.5.2 serialises every non-finite number as `null`.
* **`JSON.stringify(undefined)` was the string `"null"`.** At the TOP LEVEL an
  unserialisable value yields `undefined`; inside an array the same value
  becomes `null`. The writer cannot decide that, so the caller does.
* **Symbol keys leaked into every enumeration.** A symbol key is spelled
  `@@sym:N:description` and lives in the ordinary property table, so
  `Object.keys`, `Object.values`, `for-in`, `getOwnPropertyNames` and
  `JSON.stringify` all reported the internal spelling. `each_own_string_key`
  filters it. It is a SECOND method rather than a change to `each_own_key`
  because `Object.assign`, object spread and `Reflect.ownKeys` are specified to
  see symbols and keep the unfiltered walk.
* **`Symbol.for` did not intern.** It minted a fresh symbol per call, so
  `Symbol.for("k") === Symbol.for("k")` was false - the one guarantee a registry
  exists to give. It holds the symbols now, and `Symbol.keyFor` reads the same
  table. `Symbol.prototype` is reachable from the constructor.
* **`String(sym)` exposed the internal key.** It describes now -
  `"Symbol(desc)"`. Note `to_string` of a symbol still returns the KEY and must:
  computed property access resolves `o[sym]` through the same call, so the
  general conversion cannot change without separating ToPropertyKey from
  ToString. Special-casing the explicit `String()` is the part available cheaply.
* **`Object.is` was missing** - SameValue, which is `===` plus the two questions
  it cannot answer (the two zeros, and NaN against itself).

### Still known, and why

* **`Symbol` does not refuse implicit conversion.** `"" + sym` and `sym + 1` are
  specified to throw TypeError, and that is the feature - it is what stops a
  symbol reaching page output by accident. Fixing it needs ToPropertyKey split
  from ToString, because `o[sym]` goes through the latter today.
* **`Symbol().description` is `""` through property access, `undefined`
  through the getter** - the key tells the two apart since 2026-09-12
  (`@@sym:<n>` carries no description, `@@sym:<n>:` an empty one) and
  `Symbol.prototype.description`'s getter answers right, but
  `vm/objects/lookup.cpp` synthesises `description` for a symbol receiver
  before consulting the prototype's accessors. `new Symbol()` is a TypeError,
  `Symbol.keyFor` refuses a non-symbol, well-known symbols describe themselves
  as `Symbol.iterator`, and `Symbol.unscopables`/`dispose`/`asyncDispose`
  exist.
* **Array holes are materialised**: `0 in [,1]` is true and `Object.keys([,1])`
  is empty. Arrays are dense vectors, so a hole needs a representation.
* **No boxing**: `new Boolean(false)` is the primitive, so it stays falsy where
  every object is truthy. Deliberate - see `context::construct`.
* **No BigInt at all.** `1n` does not lex, and it is a PARSE error, so a bundle
  containing one fails as a whole. That is a type to add, not a bug to fix;
  `ctbrowser/unittests/js/bigint_basics.cpp` is the acceptance list.


## BigInt (2026-08-09)

The seventh primitive type. 61 expressions differentially tested against node
(V8) - arithmetic, comparison, conversion and every error the specification
names - with no differences. `ctbrowser/unittests/js/bigint_basics.cpp`, which until today
recorded the type's ABSENCE and said it should be rewritten as a conformance
suite the day it arrived; 17 of its assertions failed together and it was.

### The representation

`boost::multiprecision::cpp_int`, held directly in `bigint_object`. Signed and
unbounded, which is the BigInt semantic exactly: no width, no wrapping, no
rounding.

**That puts a third-party header in `value.hpp`, which is a documented
exception rather than an oversight.** The rule exists for compile time, and the
measured cost is `value.hpp` going from 3271 ms to 3869 ms per translation unit
(+598 ms - far less than cpp_int's 4691 ms standalone, because value.hpp
already pulls in much of what it needs), across the 11 TUs that include it. The
alternative considered was storing decimal TEXT and converting inside one
`.cpp`: it keeps the header light and makes every operation parse and re-format
its operands, which is the wrong shape for a numeric type.

### The GMP backend, and why it is off (2026-08-09; the option itself was removed 2026-09-10)

`-DCTBROWSER_WITH_GMP=ON` swaps `cpp_int` for `mpz_int` — the same
Boost.Multiprecision interface over GNU GMP. It works, on both platforms, and
it is **off by default for two independent reasons**.

**It is slower for this workload.** cpp_int against GMP, both compiled for the
same modern arch, on a Core Ultra 9 185H. `>1` means GMP wins:

| op | 64 bits | 256 | 1024 | 8192 | 65536 |
|---|---|---|---|---|---|
| multiply (Linux) | **0.34x** | 1.15x | 1.84x | 1.09x | 0.53x |
| add (Linux) | **0.42x** | 0.91x | 1.68x | 2.51x | 3.21x |
| to string (Linux) | 1.00x | 1.50x | 2.19x | 7.28x | 20.1x |
| multiply (Windows) | **0.18x** | 1.17x | 1.65x | 1.21x | 0.59x |
| add (Windows) | **0.18x** | 0.80x | 0.78x | 1.40x | 2.55x |
| to string (Windows) | 0.95x | 1.99x | 3.42x | 14.7x | 39.0x |

GMP is emphatically the better library *on the right*. The left column is the
one that describes JavaScript: a BigInt is reached for to hold an id, a
nanosecond timestamp or a 64-bit hash exactly, and those are one or two limbs.
There GMP is 2.9x slower on Linux and **5.5x slower on Windows**, because every
`mpz_t` is a heap allocation while cpp_int keeps a small value inline. The
Windows gap is the wider one for the reason `docs/build.md` already records
about mimalloc: that platform's allocator is the further behind.

**Tuning does not rescue it, which was measured rather than assumed.** GMP
6.3.0's own `config.guess` reads this CPU as `nehalem` — a 2008 part — so the
obvious build is badly mistuned; brew's is built for `core2`. Rebuilding it
correctly for `alderlake` (the `x86_64/alderlake → icelake → skylake` path,
with the `mulx`/`adcx`/`adox` code) moved the 64-bit numbers *not at all*. The
cost there is allocation, not instruction selection, and no assembly fixes
that. Where the tuned build did help — 1024-bit add went 0.87x → 1.68x — it
helped in the column this engine does not live in.

**It changes the licence of the binary.** GMP is LGPLv3+ or GPLv2+, and this
engine ships statically linked self-contained `.exe` files under Apache-2.0
with LLVM exceptions. That is a distribution obligation, so the option is
explicit opt-in rather than "on when GMP is found" — the latter would attach it
to anyone who happened to have GMP installed. `NOTICE` states the position.

**The cross-build works, including the assembly.** `tools/mingw/build-gmp-mingw.sh`
builds it for llvm-mingw; the common advice that clang needs
`--disable-assembly` for GMP is not true on this toolchain, and a static
`.exe` linking it runs. Two autotools traps are worth knowing, both in that
script: under WSL, `binfmt_misc` runs `.exe` files, so configure decides it is
**not** cross-compiling and then dies on "cannot determine executable suffix"
(pass `--build` explicitly); and `CC_FOR_BUILD` must be a native compiler or
the build tries to execute the Windows helper programs it just produced.

Both backends are held to `ctbrowser/unittests/js/bigint_basics.cpp`, and it passes on both, as
do `number_basics`, `type_basics`, `number_format`, `symbol_basics`,
`boolean_basics` and `obfuscated`. The switch is a performance choice, not a
semantic one — and `bigint.hpp` may therefore use nothing backend-specific.

**Boost.Multiprecision was turned down for `Math` earlier and is right here**,
which is not a contradiction. The objections there were 400x slower than
hardware and, being correctly rounded, further from V8's fdlibm rather than
nearer. Neither transfers: arbitrary precision has no hardware alternative -
that is the point of the type - and integer arithmetic is exact, so there is
nothing to round and no cross-platform question. Header-only, so the
cross-build needs nothing.

### What it took

* **The lexer** (ctjs submodule): `1n` lexed as `1` then the identifier `n`, so
  a bundle carrying one BigInt literal anywhere failed to parse AS A WHOLE. The
  suffix rides on the number token; whether the digits are a valid BigInt is
  decided where the value is built, so `1.5n` and `1e3n` are refused there.
* **`op::load_bigint`**, carrying the literal text in the strings table and
  parsing once per site into a cache beside the string cache - rooted by the
  collector and cleared per run, or a literal a loop is about to re-read gets
  swept.
* **`context::bigint_binary`**, the arithmetic dispatch: both bigint, do it;
  one bigint and one anything-else, throw.
* **Comparison crosses types where arithmetic does not**, and does so EXACTLY -
  `9007199254740993n == 9007199254740992` is false, which going through a
  double would get wrong, and that is the one loss the type exists to prevent.

### The refusals are the feature

`1n + 1` is a TypeError. An engine that coerced instead would round at exactly
the point the type was reached for, so the throw is the semantic rather than a
missing case. Everything reaching ToNumber implicitly refuses the same way -
`+1n`, `Math.abs(1n)` - while `Number(1n)` is the explicit conversion and is
allowed. `JSON.stringify(1n)` throws because there is no lossless spelling.
`>>>` throws because an unsigned shift needs a width.

### One trap this uncovered

Giving `BigInt.prototype` a `toString` immediately broke `1n + 2n`, which began
evaluating to "12". `to_primitive` walks valueOf/toString for anything on the
heap, and a bigint IS on the heap though it is a primitive - so the moment the
method existed, `+` saw two strings and concatenated. A bigint now passes
through `to_primitive` unchanged.

A symbol has the same shape and is deliberately NOT in that guard: it should
refuse the conversion outright, and cannot until ToPropertyKey is separated
from ToString, because `o[sym]` resolves through the same call. Adding it there
made `"" + Symbol("x")` yield the internal key instead of "Symbol(x)" - a worse
wrong answer - and `ctbrowser/unittests/js/symbol_basics.cpp` caught it.

## Typed arrays, ArrayBuffer and DataView (2026-09-12)

The typed arrays were one 273-line file that knew nine constructors, `set`,
`subarray`, `from`/`of`, and an `ArrayBuffer` that was a `byteLength` and a
`__bytes` array. It is now `builtins/collections/typed_arrays/` - clauses 23.2,
25.1 and 25.3 as written: `%TypedArray%` as the [[Prototype]] of the nine
constructors with `from`, `of` and `@@species`, every `%TypedArray%.prototype`
method and accessor, `ArrayBuffer` with `maxByteLength`, `resize`, `slice`,
`transfer`, `transferToFixedLength`, `detached` and a real detach - plus the
immutable-arraybuffer proposal's `transferToImmutable`, `sliceToImmutable`
and `immutable`, which every writing method refuses - `DataView` for every
element type in both byte orders (Float16 and the BigInt64 pair included),
and the ES2025 `Uint8Array` base64/hex codecs. The header in that
directory is the storage model; two decisions in it are worth knowing:

* **A typed array made from a length or a list OWNS its elements** (in
  `array_object::items`, as before) and becomes a view over a fresh buffer
  only when something asks for its `buffer` or a `subarray` - in place, so the
  aliasing a page then relies on is real. Not always-a-view, because lib/Shell
  reads `items` off the typed arrays a page hands it (`readPixels`,
  `putImageData`), and a view's `items` is empty by design.
* **A buffer keeps a list of the views whose length has to follow it.** The
  VM reads `view_length` off a view without asking anybody, so `resize` and
  detach re-bound every registered view; a `subarray` over a fixed-length
  buffer is not registered, since nothing can shrink under it.

**The twelve kinds (2026-09-16).** `element_kind` gained `f16`, `big_i64`
and `big_u64`, so `Float16Array`, `BigInt64Array` and `BigUint64Array` are
installed. A BigInt kind's element VALUE is a bigint (Table 71): owning
`items` hold `bigint_object`s (one shared `0n` at allocation), a view holds
eight little-endian bytes through `view_get_raw`/`view_set_raw`, and every
read and write in the directory and in the VM's index paths goes through
`typed_element_get`/`typed_element_set` (declared in value.hpp, defined in
constructors.cpp) - a `value` in, a `value` out, the kind deciding the
coercion: ToNumber, or ToBigInt (`bigint.hpp`'s `to_bigint`, shared with
`BigInt.asIntN` and `DataView`), where a Number is a TypeError. The Number
kinds keep their double path and Shell still reads their `items` as
numbers. binary16 is the compiler's `_Float16` (`double_to_half`/
`half_to_double`, one correctly rounded step, shared with `DataView`).
[[ContentType]] is enforced where 23.2.4 says: the constructor from a typed
array, `set` from one, and a species result of the other content type are
TypeErrors. `[[DefineOwnProperty]]` over a typed array's integer index is
10.4.5.3 now, for every kind: false past the length or for a descriptor that
would make the element non-configurable, non-enumerable, read-only or an
accessor, the value coerced to the kind - before, a view answered true for
anything and an owning array could be grown or handed an uncoerced element.

What the VM cannot answer yet, and where: a typed array's [[Prototype]] is
found through its kind's global, so a subclass instance cannot be one; and
`vm/objects/lookup.cpp` answers `buffer`, `length`, `byteLength` and
`byteOffset` for a view before any prototype getter is asked, building a
fresh wrapper for `buffer` each read, so `ta.buffer === ta.buffer` is false.
`$262.detachArrayBuffer` in tools/ct262 still throws; the engine's detach is
`ArrayBuffer.prototype.transfer`'s, one call away. Smaller, found by the same
measurement and also the VM's: `ta[i] = v` on a Number kind runs the STATIC
ToNumber (an object's `valueOf` is not called; a BigInt kind's ToBigInt does
run it), `ta[i] = -0` on an integer kind keeps the -0 (`coerce_element`'s
wrap), `Object.defineProperty(ta, "length", ...)` resizes an owning typed
array, a typed array reports `length` as an own key, `Object.freeze` of a
non-empty typed array does not throw, and `class X extends Uint8Array`
neither inherits the statics nor produces a typed array from `super()`.

Measured on the devbox at the commit that landed this (test262 files, PASS
before -> after, 4 workers, 10 s, 2 GB, the detachArrayBuffer.js tests still
skipped): TypedArray 1 -> 745 of 1,446, TypedArrayConstructors 74 -> 275 of
738, ArrayBuffer 24 -> 190 of 221, DataView 0 -> 438 of 561, Uint8Array 4 ->
64 of 70, and built-ins/Array 2,774 -> 2,787 of 3,082 from its
testTypedArray.js rows. Nothing went PASS -> FAIL. Of what is left, ~750
files want `BigInt64Array` and ~30 the subclass `super()`.
