[Back to script.md](../script.md)

## WHAT p5.js NEEDED (2026-07-29)

**p5.js v2.3.1 runs, in BOTH builds** — 4.5 MB and 138,938 lines that nobody
wrote for this engine. It lexes, parses (282,028 nodes), compiles (4,754 functions), executes
its whole top-level IIFE, builds a sketch, runs `setup()`, drives `draw()` from
`requestAnimationFrame`, and paints. `ctbrowser/test/corpus/p5/p5_ratchet.cpp` measures how far it
gets on a ladder of 12 rungs and `ctbrowser/test/corpus/p5/p5-ratchet.txt` records the high-water
mark; the level may not go down. TWO numbers are recorded, each with its own
pawl: `level` is p5-min, where the page defines `IS_MINIFIED` as p5's own
minified build does, and `full-level` is the same ladder with the flag left
undefined - the Friendly Error System and i18next's setup both in play. Both
read 12, and neither reaches the network. "p5 runs" is a different claim when
half of p5 is switched off, which is why the second one is measured rather than
assumed. `tools/corpus/ratchet.py p5 ratchet --survey` measures each of
the bundle's 71 rollup modules independently, `--bisect NAME` carves one out as
a reproducer, and `--source N` prints the text of compiled function N - a stack
trace names functions as `fn#3778`, and most of a bundle's functions are
anonymous.

Several sections above are now out of date and are left as history: regex has an
engine, promises have pending state, and object-literal accessors compile.

**The language gained**: destructuring, `#private` fields, the comma operator,
default and rest parameters, `??`/`??=`, class fields as per-instance
initialisers, arrow `this`, spread calls, `arguments`, named function
expressions binding their own name, `f.name`/`f.length`, and 8-byte
instructions with 16-bit operands (a 4.5 MB bundle passes the 256-name and
256-register marks in its first few hundred lines, and every one of those caps
used to truncate silently).

**The object model gained** accessors and descriptors, `Proxy` with get/set/has/
construct traps, `Symbol`, `Map`/`Set`/`WeakMap`, typed arrays, real `Error`
objects that unwind to a handler, and prototypes that are reachable as
`Object.prototype`, `Array.prototype` and the rest rather than only consulted by
lookup.

**The bugs that mattered were silent, not loud.** Worth reading as a class:

- `obj[key](../...)` passed the KEY as argument 0, because the compiler evaluated
  the key into the register the argument window starts at. Only the form WITH
  arguments was wrong, so `xs[0]()` looked fine.
- `(220).toString(16)` returned `"220"` — the radix was accepted and ignored, so
  every colour p5 computed became an unreadable string and every fill came out
  white.
- `Object.getPrototypeOf` returned null for a primitive, a prototype had no
  `constructor`, and a class had no `name`. `Object.getPrototypeOf(x)
  .constructor.name` is the standard way to identify a value where `instanceof`
  cannot; each hole yields `undefined`, and undefined compares EQUAL to the other
  undefined it is being tested against — so a plain string reported itself as an
  instance of a colour space.
- `Object.prototype.toString` returned `"[object Object]"` for everything. That
  is the type tag libraries parse.
- `split(/re/)` coerced its pattern to a string, so it never matched and the
  input came back as one element.
- `arguments` did not exist, and when first added was materialised where the
  name was MENTIONED - by then the surrounding expression had reused the
  registers holding the arguments past the last declared parameter.
- A destructured parameter that a nested function CAPTURED was bound to a cell
  inside a cell, so reading it gave the inner cell - an object with no
  properties. Two things boxed it: the pattern binding, which boxes the names it
  declares, and the parameter loop, which boxed everything the frame had
  declared by then. Only visible when the name was captured, because an
  uncaptured local is never boxed at all.
- An object converted through the TAG rather than through its own `toString`
  and `valueOf`. A class defines them precisely because it expects `'' + x` and
  `${x}` to use them.
- A name used ONLY inside a template substitution was never captured. A
  template is one node carrying its whole source, so every walk over the tree
  was blind to the holes - including the two that decide whether a local is
  boxed. The nested function resolved the name as a global and read undefined.
  And a hole was parsed in STATEMENT context, so `${ {v: 1}.v }` read its object
  literal as a block.

**Calling a non-function is a catchable `TypeError`** rather than the end of the
run. Pages catch it — feature detection is written as `try { thing() } catch {}`
at least as often as a `typeof` test — and an uncatchable fault also unwinds
nothing, so a probe wrapped in try/catch reports no error at all and the failure
appears to come from wherever the run happened to stop. That one property is
what made the bugs above findable.

**`await` SUSPENDS, and handlers are microtasks (2026-07-29).** Both were listed
here as missing and both are done.

A promise handler runs at the end of the turn, not when the promise settles:
jobs are queued on `context::microtasks_` and drained after the top-level
script, and in the event loop after timers, before animation frames, and again
after each event dispatch. Ordering matches V8, including that every
first-round handler runs before any second-round one.

`await` on a PENDING promise lifts the frame out of the register stack into a
`coroutine_object` - registers, ip, receiver, closure and its own handler
entries, with `reg_top` made relative because the frame comes back somewhere
else - hands the caller a promise, and registers the coroutine on the awaited
promise's own handler list. A resumption IS a promise handler, so it queues and
orders with every `then` rather than being a second mechanism racing them. Only
the TOP frame can suspend, which is sufficient: every frame below is either
already suspended or a synchronous caller that must itself unwind. A rejection
throws AT the await, so `try { await p } catch` spans a real suspension, and an
uncaught one rejects the function's own promise rather than ending the run. The
saved window is a GC ROOT - it is out of the stack the collector walks, so
without tracing it everything a waiting function held is freed.

What is still a deviation: `await` on an ALREADY-SETTLED promise, or on a plain
value, reads it straight out instead of yielding a turn. The spec queues a job
either way, so `async function f(){ log+='1'; await 1; log+='2'; } f(); log+='|'`
gives `12|` here and `1|2` in a browser. Suspending unconditionally would also
suspend TOP-LEVEL await, which this engine allows in a classic script and whose
value `context::run` returns.

**Still missing, by name.** No generators, so no `yield`. `new Function(body)` exists as a global and refuses when called - the
VM must own programs compiled at run time. `arguments` is a real Array rather
than the spec's array-like, so `Array.isArray(arguments)` is true here and false
in a browser. `structuredClone` covers data only. An ArrayBuffer is shared storage for a view
over the WHOLE of it; a sub-range view (`new Uint8Array(buf, 4, 8)`) refuses
with a RangeError, because expressing it wants a view to address a span of
someone else's storage rather than own its elements. Regex has no lookbehind and
no backreferences. Strings are BYTES, so `normalize` is the identity and
`codePointAt` agrees with `charCodeAt` rather than pretending to a UTF-16 view
nothing else here has.


## TWO SILENT WRONG ANSWERS, FOUND CHASING loadImage (2026-07-29)

Neither had anything to do with images. Both are the shape this codebase keeps
finding: a plausible value where an error belonged.

### A `value` captured by a native lambda is not a GC root

`new Promise(fn)` handed its executor a `resolve` that held the promise in a C++
lambda capture. The collector walks a native's PROPERTIES, not its captures, so a
promise nothing else referenced was freed while the page still held the resolve
that would settle it - and settling a recycled cell does nothing, silently,
because `settle()` checks `is_object()` first.

The cost was that **an async function could suspend exactly once.** The first
await's promise was still in a live frame's registers; a promise created DURING
the resumption existed only in those captures and in its own handler list, a
cycle with no root, so the second await never came back. Every real loader awaits
twice - `await fetch(u)` then `await response.bytes()`.

`test_await_suspends_and_resumes` has two awaits and passed throughout, because
its gates are top-level consts and therefore rooted. Only a promise created during
a resumption shows it; `test_a_promise_made_during_a_resumption_survives` is that
test.

The fix is a property the page never reads, because a native's props ARE traced.
**Anything else that captures a `value` in a native lambda needs the same
treatment** - the style proxy's target and an AbortController's signal are safe
only because they are reachable through an object the page holds.

### A destructured name is a local, not a temporary

`const { data } = f()` allocated `data`'s register INSIDE the `reg_mark` that
exists to free the temporary holding `f()`'s result, so `release_to` handed the
local's slot back and the next temporary in the same scope wrote over it. Inside
a `try` block, `const { data } = f(); return 'len=' + data.length` read `data` as
the string `"len="`.

It only bit inside a BLOCK. In a function's top scope the name is hoisted, so
`find_local_in_current_scope` finds it and nothing is allocated - which is why
every destructuring test until now passed. `declare_pattern_names` is now called
before the mark.

This is what stopped p5 loading an image: `loadImage` destructures its fetch
result inside a try, so the image bytes were a fragment of an error message.

### Escapes that carry a code point

`'\x41'` was the three-character string `x41`. The escape decoder had cases for
`\n \t \r \0 \b \f \v` and a default that pushes whatever followed the
backslash. It surfaced through `btoa`, whose argument is a binary string of bytes
and not text: `btoa('\x00')` encoded the letter x, so a page hand-writing an
image got a corrupt one.

A code point becomes its UTF-8, the same choice `String.fromCharCode` makes,
because strings here are bytes. A surrogate PAIR decodes as one code point, so an
escaped emoji equals the same emoji written literally. `\u{...}` and the line
continuation came with it.


## ITERATION, OPTIONAL CALLS, AND EVERY COLOUR STRING (2026-07-29)

`fill('#ff0000')` did not work. Nor did `color('red')`, `background('#fff')` or
any other string colour - every one threw "Invalid color string". Both ratchets
read 12/12 throughout, because the corpus pages all pass numbers.

Two independent bugs, either of which was enough.

### An optional call is still a method call

`x?.m()` parses as call(opt_member(x, 'm')), and the call compiler had cases for
`member` and `index` callees but not the optional ones - so it fell through to a
plain call with NO RECEIVER. `this` was undefined inside the method, which for a
primitive receiver is a wrong answer rather than an error:

    ' x '?.trim()      -> undefined
    (5)?.toFixed(1)    -> NaN
    true?.toString()   -> false
    [1, 2]?.join('-')  -> ""

An object receiver hid it, because a method that ignores `this` works either way.
p5's colour parser opens with `String(str)?.trim()`, so every colour string became
undefined before anything looked at it.

### Nothing could iterate a Map or a Set

for-of is an index loop over `length`, and a Map and a Set have neither - so
`for (const x of set)` ran ZERO times, `[...new Set(v)]` was empty, and
`Array.from(set)` was empty. Silently, all three.

That is what actually broke colour: p5's colour-space registry is
`[...new Set(Object.values(registry))]`, so no colour space was ever registered
and no format could match. And `new Set([1, 2, 2, 3]).size` was 4 - a Set that
does not dedupe is not a Set.

`context::iterable_values` is now the ONE answer to "what can this iterate",
shared by for-of (through `op::iterable`), spread, `Array.from` and the Map/Set
constructors - so `new Set(otherSet)` and `f(...map.keys())` work, and all of them
agree. It covers arrays, strings, Maps, Sets, the views those hand out, and
anything array-LIKE.

`Symbol.iterator` dispatch came on 2026-09-12: `iterable_values` runs a page's
own `[Symbol.iterator]()` through the protocol, and for-of pulls such an
iterator lazily (the generators section below).

### Reading a property of undefined throws (since 2026-09-12)

`undefined.x` gave `undefined` here rather than a TypeError, and it is why the
class expression leak above took an afternoon: `p5` was undefined, `.TableRow`
was undefined, and the error surfaced one step later naming `TableRow`. It is a
TypeError now, in `lookup_property`/`store_property` so both tiers agree:
"Cannot read properties of undefined (reading 'x')". `?.` is the way to ask
without one.

### Every `await` in a function is a job (since 2026-09-12)

`await x` on a settled promise or a plain value read it straight out and
carried on in the same turn; 27.7.5.3 makes the continuation a job even
then. The frame is now lifted out exactly as for a pending promise and put
back by one native job (`context::await_job`) queued at the await, so `await
1` runs after the microtasks queued before it - which is what every ordering
test and every MutationObserver callback relies on. A classic script's top
level keeps the synchronous read (`return await 3` in a test script has no
caller to hand a promise to) - and when what it awaits is a PENDING promise
it drains the microtask queue first, because with every inner await a job,
`return await g()` for an async `g` is pending until those jobs run.

### Private names are keys per class, and a read is a brand check (since 2026-09-12)

`#x` compiles to the property key `@#x:N`, N numbering the class body that
declares it (`compiler_impl::private_scopes_`), so an inner class's `#x` is
never an outer instance's and two classes' `#x` never alias. `lookup_property`
treats any `@#` key as 7.3.31 PrivateGet: an object the class did not
initialise - or any proxy - is the TypeError "Cannot read private member #x
from an object whose class did not declare it". A write is the same check
(7.3.32), a private method is not writable, and `#x in obj` is the check as a
boolean. Since 2026-09-12 (later that day) the check is
`context::private_element_present`, PrivateElementFind for this spelling: an
own `@#x:N` key - a FIELD, added once by `__ctbrowser_private_add`, which is
the TypeError when the object already carries it or is not extensible - or a
METHOD/ACCESSOR key up the prototype chain (up the static chain for a static
one) PLUS the class's brand, the own `@#:N` key the class's `<fields>`
initialiser adds to every instance it constructs and the definition adds to
the constructor for the statics. So `Object.create(C.prototype)` and a
subclass constructor fail a method's brand check. Not yet: two evaluations of
one class expression share N, so their private names alias where the
specification gives each evaluation its own.

### Strict mode, the part that changes what runs (since 2026-09-12)

`function_proto::is_strict` is set by a `"use strict"` directive, inherited by
nested functions, and always on inside a class body or a module. What it
changes: a rejected [[Set]] - non-writable, inherited non-writable,
non-extensible receiver, getter without setter, primitive receiver - is a
TypeError (`store_rejected_`, read by `set_prop`/`set_index` and by the AOT
bridge off the frame's proto) instead of the silent drop sloppy code gets, and
an assignment to an unresolvable name is a ReferenceError (the compiler emits
a call of `__ctbrowser_strict_assign(name)` before the `set_global`, since
op::set_global's contract says it cannot throw; a declaration's own first
write - `let x = 1`, `class C {}`, a declared pattern at a script's top level -
sets `compiler_impl::declaring_` and is not probed). NOT in a module's top
level, deliberately: ctcompile's module fixtures publish to their host through
`OUT = ...` and rely on the write. Still sloppy everywhere: `this` in a plain
call (undefined, not globalThis - the AOT contract pins it), `arguments`
aliasing, `delete` of a non-configurable property, the early errors.

### The language forms that landed with test/language (2026-09-12)

Measured before/after in `docs/test262.md`. The front end (ctjs `vparse.hpp`,
gitlink 945b60e): the whitespace and line-terminator sets of 12.2/12.3 on
their UTF-8 bytes; ASI at statement ends (12.10 - `a b` on one line is the
SyntaxError it always was, a do-while takes its virtual `;`); `await` as a
name outside an async body and `yield`/`await`/`let`/`async` as labels and
arrow parameters where they are names; escaped words never keywords; class
static blocks; class fields ending at `;`, `}` or a line break; any
LeftHandSideExpression as a for-in/of head (`for (o.p of xs)`, `for ([a.b]
of pairs)`); `{ a = 1 }` as a pattern (CoverInitializedName); tagged
templates; `import.source(x)` / `import.defer(x)`; `using` and `await using`;
the `**` / `??` grammar; division after an object literal's `}`; an unknown
byte as a token rather than a silent skip. The checker (`compile/early_errors/`):
string and template escapes, the whole numeric grammar, static-block rules,
class names, labels, lexical for-in/of heads, labelled function bodies, strict
Annex B, `using` rules. The compiler and VM: static fields and blocks run in
one `<static>` function with the class as `this`; `using` lowers to a
disposal region (`compile/statements/using.cpp` - needs `Symbol.dispose`, not
installed yet); `import.source` is a rejected promise; tagged templates cache
one frozen strings array per site; a class heritage is checked and chains the
constructor (`Object.getPrototypeOf(D) === B`); `super.x` reads with `this`
as receiver; accessors and object-literal methods have a home object; a
deleted synthesised `name`/`length` stays deleted; `f.length` is
ExpectedArgumentCount.

### Session 12 (2026-09-16): what moved after the round-one merges

All in `docs/test262.md`'s `9c70aaa0` and later rows; each is a JS-semantics
change the native backend sees as a divergence until it follows.

* **The temporal dead zone, statically.** `local::initialized_at` records
  where a function body's own `let`/`const`/`class` is initialised (the
  declarator's end - ctjs identifiers and declarators carry spans since
  `3cb2ef9`), and `compile_ident` throws the ReferenceError of 9.1.1.1.6 for
  a read of the same frame textually before it: `let x = x + 1`, `use(y);
  const y = 1`, `new K(); class K {}`, `typeof` included. NOT decided: a
  read from a nested function, or of a block-level `let` (declared at its
  statement, not at block entry) - a runtime hole and a check on captured
  reads would be an ABI change.
* **A declaration's write is only its own.** `declaring_` covered every
  initialiser expression and pattern default, so strict code assigning to an
  undeclared name inside one made a global; a `not_declaring` guard clears
  it for defaults, computed keys, member targets and every nested frame.
* **Annex B.3.3 is two steps, and they are separate.** A function declared
  in a nested block of sloppy code also has a var binding - of the enclosing
  function, or a global of the script - which is CREATED at entry when
  nothing of that name exists yet (`predeclare_locals` hoists it; a classic
  script lists the names in `hoisted_vars`) and WRITTEN when the declaration
  is evaluated (`compile_function_decl` copies the block binding through
  `annex_b_decls`, which lists DECLARATIONS rather than names - two blocks
  may declare one name and only one of them be applicable). Only a PARAMETER or a LEXICAL declaration stops
  both; a `var` or another function declaration of the name means the
  binding is already there and the write still happens, and `arguments` is
  never given one. The shadow walk (`each_block_function`) counts every
  construct that binds lexically between the body and the declaration: a
  block's `let`/`const`/`class`, a block's own function declarations (14.2.1,
  so a deeper one gets nothing), a `for` head, a DESTRUCTURING catch
  parameter (a simple one is B.3.5's relaxation and lets the extension
  through), the whole body of a `switch` as one scope, and a script's own
  top-level lexical names. `if (x) function f() {}` is B.3.4 and gets a
  scope of its own, so the declaration never writes an enclosing binding.
  A BLOCK OF A CLASSIC SCRIPT IS A SCOPE like any other - only
  `scope_marks.size() <= 1` is the script's own level, where a declaration
  IS the global. `var x;` at a script's top level STILL writes undefined
  (`statements/dispatch.cpp` says which native prover needs the write).
  **BYTECODE SHAPE.** That last part changed one: a function declared in a
  block of a classic script used to compile to `closure; set_global` and now
  compiles to `load_undef` (plus `new_cell` when captured), `closure`,
  `move`/`cell_set` into the block's own register, and `set_global` only
  when B.3.3 applies. No new opcode, and no other construct moved - but a
  consumer that pins the sequence for this one (the native backend does)
  sees a different one.
* **A Module's ModuleItemList.** `import` and `export` are ModuleItems and
  not Statements (16.2.1), so the early-error pass refuses either one
  nested in a block, a clause or a function body, and refuses both outright
  in a classic script. A module's top-level function declarations are
  LEXICALLY declared there, so two of a name are a redeclaration where a
  script allows them; its ExportedNames may not repeat; and every
  ExportedBinding without a `from` must be something the module declares.
* **%GeneratorFunction%, %AsyncGeneratorFunction%, %AsyncFunction%** exist
  (`proto_kind::generator_function` etc.; `context::function_proto_kind`):
  a `function*`'s [[Prototype]] and `.constructor` are its intrinsic, its
  own `prototype` inherits %GeneratorPrototype% with no `constructor`, its
  instances inherit THAT (read after the parameters ran), an async function
  has no `prototype`. GeneratorValidate throws for a non-generator receiver.
  %ArrayIteratorPrototype% and its siblings are one shared prototype per
  kind under %Iterator.prototype% (`list_iterator`), kept under a private
  key on Array.prototype - not a global, which `window` enumerated.
* **`await` adopts a thenable** through PromiseResolve (a non-promise object
  is resolved into a promise, so `then` runs and its rejection throws at the
  await). Array.fromAsync's helper follows GetMethod/ToLength.
* **Garbage collection.** The 40,000,000-object allocation ceiling counts
  since the last collection, not for life; a heap past its threshold collects
  at a native call site too (interpreter and `invoke`), not only at an
  interpreted entry - neither is a stress point.
* **Builtins:** `parseInt` per 19.2.5 (ToInt32 radix, [2, 36], the Unicode
  spaces, base 10 exact); `isNaN`/`isFinite` through ToPrimitive; a Symbol
  out of ToPrimitive is 7.1.4's TypeError; `BigInt.asIntN`/`asUintN`;
  `match`/`matchAll`/`replace`/`replaceAll`/`search`/`split` ask their
  argument before ToString(this); `bind` takes the target's [[Prototype]]
  and keeps an infinite length; `Object.prototype.toString` asks IsArray
  through a proxy; `JSON.stringify` reads its replacer list through Get,
  unwraps String/Number objects, serialises a proxy; a proxy trap receives a
  Symbol key as a Symbol (`context::key_value`); a sloppy function called
  with a nullish receiver sees `globalThis` (10.2.1.2 step 5.a); `NaN`/
  `Infinity`/`undefined` refuse a write; `Symbol().description` is undefined
  (the symbol's reads go through Symbol.prototype's accessors); Map/Set
  constructors step their iterable and close it on an abrupt completion;
  `Function.prototype.toString` spans include `async` and exclude `static`;
  `$262.detachArrayBuffer` is `ArrayBuffer.prototype.transfer`.

### Reading an unresolvable name throws (since 2026-09-12)

A bare identifier that is neither a local, a global binding nor a property of
the global object read `undefined`; it is a ReferenceError now, "x is not
defined", catchable, in `context::global_or_named` so both tiers agree (the
`get_global` row and `ct_aot_global_get` became may_throw with a status and an
out-slot in the same change). The global object is the global environment's
object record, so an inherited name resolves - a bare `toString` is
`Object.prototype.toString`, as in every browser - and the shell's named-access
hook (an element with an `id`) is still asked before the throw. The original
unresolvable-name correction accounted for 1,180 files of test262 by itself.

Since 2026-09-13, source `typeof x` (including parentheses around `x`) emits
`get_global_typeof` for its global fallback, followed by `type_of`. Only this
lookup mode suppresses an unresolved-name error (13.5.3); `typeof (0, x)` and
`var saved = x; typeof saved` keep ordinary throwing reads. Local TDZ checks and
property getters retain their normal behavior, including inside `with`. CTJS
preserves the lookup mode and the boxed AOT tier selects `ct_aot_global_get_soft`
from it, then checks the frame status because hooks and proxy traps can still
throw. Neither instruction adjacency nor a later TypeOf user grants permission
to silence an ordinary lookup.

### THE FRONT END COSTS MORE THAN THE VM (2026-07-31)

Callgrind on a whole page render: `ctjs::vp::lex` 23.7%, the compiler's own
passes another ~12%, and `context::run_loop` - actually executing the program -
**1.4%**. Reading JavaScript costs an order of magnitude more than running it
here, which is not where anyone would guess the time goes.

Two compiler faults are already fixed and were the same shape as each other,
both linear where they should not have been: `is_captured` scanned a
`std::vector<std::string>` once per local declaration (15.1% of a page render),
and `kids()` built a vector per AST node visit for children already contiguous
in the pool (3.9%). Together **-26.5% instructions** on that render and -33% on
the p5 bundle compile.

The lexer is next and has its own document: `docs/history/lexer.md`.

### CAN THE SCRIPT ENGINE USE THREADS? Yes - the front end, not the runtime (2026-07-31)

Asked and answered from the code, because the two halves have opposite answers.

**The runtime cannot, and it is not a limitation to fix.** Values are NaN-boxed,
heap objects are shared and unguarded, and the GC is mark-and-sweep over precise
roots per context. More fundamentally, JavaScript's memory model is
single-threaded: two threads running one context's bytecode is not slow, it is
*wrong*. No engine does it.

**The front end can, and that is where the time is.** Callgrind on a page render
puts lexing plus compilation above 50% and `run_loop` at 1.4%. And
`compiler::compile` is a **static function with no mutable state** - checked, not
assumed: no statics, no thread_locals, no atomics anywhere in `src/script`. It is
a pure `source -> program`, which is exactly the shape that parallelises.

Where that could go, in increasing order of work:

1. **Independent programs concurrently.** `new Function` bodies, and worker
   scripts if they arrive. Free today - the function is already pure.
2. **Lexing pipelined with parsing.** The lexer produces a token vector the
   parser then consumes; they need not be sequential.
3. **Nested function bodies in parallel.** Each produces an independent
   `function_proto`. The blocker is that they share `out_.functions` and the
   frame stack, not anything semantic. **The capture index was deliberately
   built read-only for this** - a lazily-filled memo would have needed a lock,
   an Euler tour built once does not.
4. **Web Workers**, which is the only runtime parallelism the language actually
   sanctions - and it is feasible precisely because a context already owns
   everything it touches. A worker is another `script::context`, with structured
   cloning across the boundary and no shared heap.

**One thing to know before attempting 1 or 3**: every `<script>` on a page is
currently CONCATENATED into a single source string and compiled as one program
(`browser::run_scripts`). Splitting them to compile in parallel is a semantic
change - `var` hoisting and function declarations are shared across scripts -
so it is not the free win it looks like.

Not planned yet. Written down because the read-only shape of the capture index
above only makes sense in this light.

### collect_captured_names: TWO attempts, both measured, both reverted (2026-07-31)

It is the obvious next target and it looks quadratic, so this is written down to
stop the next person - or the next session - rediscovering it.

It **is** quadratic in nesting depth: instrumented on the p5 bundle, **18,906
calls and 16,529,682 node visits**, about eighty visits per node, because each
function's subtree is re-walked once per enclosing function.

**Attempt 1, memoise `all_names` at function boundaries.** Visits fell 16.5M ->
9.6M, 42% fewer. Instructions fell **0.3%**. The walk was never the cost: the
cost is the set data, and unioning each function's memoised names into its
ancestors moves exactly as many strings as re-walking did. The quadratic moved
from the traversal to the copying rather than going away, which is why an
asymptotic argument was not enough on its own.

**Attempt 2, insert into the set during the walk** instead of building a
duplicate-filled vector and inserting once at the end. **3.1% WORSE.** Hashing
every identifier occurrence costs more than appending it and deduplicating once.
It does save 12 MB of peak RSS (156 -> 144 MB), which was not worth 3% CPU.

Measured with callgrind, because wall clock on this machine varies ±10% and the
first attempt looked like a 10% win by that measure and was not.

**What would actually work**, if this is ever worth the effort: stop
materialising a set per function at all. `is_captured` is only ever asked about
the handful of names being declared as locals of the current function. Give each
function node an Euler-tour interval, record for each name the entry times of
the innermost functions mentioning it, and answer the query with a binary search
- O(occurrences) memory rather than O(names x depth), and no per-function set.
That is a different algorithm, not a tweak, which is why it was not attempted
here.

### WEBGL WORKS (2026-07-31), and getting there was four wrong answers

`createCanvas(w, h, WEBGL)` selects p5's RendererGL, and a sketch drawing `box()`
and `sphere()` renders — `examples/pages/p5-webgl.html` has a golden that matches
byte-for-byte on Linux and on the Windows cross-build.

Every step of the way was a silent wrong answer in this engine, and the record is
kept because the SHAPE repeats:

* `getContext('webgl')` returned null. p5 kept the null and fell back to 2D, so a
  WEBGL sketch drew nothing and reported nothing.
* So it threw instead — and that was worse, for a reason the comment defending it
  got backwards. RendererGL is `getContext('webgl2') || getContext('webgl')` and
  needs a FALSY value to fall through, so the throw escaped the constructor and
  left the sketch on the Renderer2D it already had: the exact outcome throwing was
  meant to prevent. **`webgl2` now returns null**, which is also what the
  specification says for an unsupported context id. Feature detection is built on
  that; it is a documented "not supported" signal, not a plausible wrong answer.
* `Float32Array.from` did not exist, so RendererGL's constructor died. Typed
  arrays now have `from` and `of`, and they are NOT the `Array` ones — they
  coerce into the view's element kind, so `Uint8Array.from([1.5])` must not keep
  the 1.5.
* `getProgramParameter` answered 0 to anything it did not recognise, so
  ACTIVE_UNIFORMS and ACTIVE_ATTRIBUTES were zero and p5 concluded the shader
  declared nothing. See `docs/raster.md` — that one is the interesting hole.

For three of those, the diagnosis blamed p5 first and p5 was innocent each time.

## WHAT PHASER 4 NEEDED (2026-08-01)

A second corpus, and it earned its keep in an afternoon. Phaser stopped at
`new Phaser.Game()` with `isBooted` true, `isRunning` false and **no error any
page could see** — the throw happened four callbacks deep inside an image
handler. `ctbrowser/test/corpus/phaser/phaser_ratchet.cpp` went 7/10 → 9/10; the tenth rung is
honestly unreached, because no corpus page draws through Phaser yet.

Four engine bugs, none of them about games, and **p5.js could not have found
any of them** — which is the entire argument for a second library.

### `+x` was a register copy, not a conversion

`compile_unary` emitted `op::move` for unary plus, so `+"2"` was still the
string `"2"`. There is now an `op::to_number`.

**It hid because it usually cannot be seen.** `+x` is nearly always written into
a string concatenation, and `"2" + "/"` and `2 + "/"` are the same characters.
The first test written for it passed *with the bug in*. It shows only where the
result is used as a number — indexing `d[(+y * 8 + +x) * 4]` read `undefined`
from pixel data that was perfectly correct.

`op::to_number` and `op::negate` both go through `to_number_value`, the
ToPrimitive-then-ToNumber that `-`, `*` and `/` already used; before that, `[] -
0` was `0` while `-[]` was `NaN`.

### `a.length = n` was silently dropped

`store_property` had no array branch at all: the engine read `length` and
ignored every write to it. `a.length = 0` is how a great deal of code empties an
array — Phaser's scene manager ends boot with `this._pending.length = 0`, so the
queue it had just drained was still full, the next frame added the same scene
again, and it threw `Cannot add Scene with duplicate key`. Growing pads with
undefined; a typed array's length is fixed and the write is a no-op.

### `window.hasOwnProperty` was undefined

`window` is a proxy — the `get` trap is what makes `window.foo` and a bare `foo`
the same variable — and the trap answered own properties, then globals, then
gave up. It never reached `Object.prototype`. `window.hasOwnProperty('X')` is
the most common feature-detection idiom there is, and Phaser asks it before it
will build **any** texture.

Two halves: the trap now falls through to the prototype chain, and
`Object.prototype.hasOwnProperty` answers through a proxy's own `has` handler,
the way the `in` operator already did. Reaching past the handler to the bare
target would say "no" about every global there is.

### The diagnosis itself was the bottleneck

The VM said ``` `get` is undefined, not a function, on undefined ``` — which
names the *method* and says nothing about the object that was missing it, and
the object is always the bug. A method call keeps its receiver in the callee's
own register, so the walk that already named a plain call's callee names the
receiver too. It now says ``` from `texture` ```, and that one word ended a
search that had run through four wrong hypotheses.

### And a primitive could not box

Found by the API probe rather than the ratchet, which is the distinction those
two instruments exist for: the ratchet read **10/10 while this was broken**,
because nothing on the ladder asks a number for a property.

`(5).hasOwnProperty` was `undefined`. A primitive boxes on property access —
`Number.prototype`'s own prototype **is** `Object.prototype` — and numbers,
booleans *and strings* all stopped at their own prototype table and answered
undefined past it. Only arrays chained, and the comment there called it "the
chain JavaScript actually has", which was true of arrays and of nothing else.
Phaser's tween manager asks `hasOwnProperty` of a number while working out which
properties of a target to animate.

The regression test caught the second half **by accident**: it asserted the
string case on the assumption that one already worked, and it did not. Worth
remembering when writing a test around a fix — the assertions you add for
completeness are the ones that find the next thing.

## GENERATORS, AND WHAT BABYLON.JS ACTUALLY NEEDED (2026-08-02)

`function*` and `yield` work. They were refused by name until Babylon.js asked
for them, and the measurement that made the work bounded came first: **622
`function*` bodies in that bundle, 803 `yield`, and ZERO `yield*`.**

None of those 622 is an author writing a generator. TypeScript compiles every
`async` function into a generator driven by an `__awaiter` helper:

```js
function __awaiter(thisArg, args, P, generator) {
    return new P(function (resolve, reject) {
        function fulfilled(v) { step(generator.next(v)); }
        function rejected(v)  { step(generator["throw"](../v)); }
        function step(r) { r.done ? resolve(r.value)
                                  : Promise.resolve(r.value).then(fulfilled, rejected); }
        step((generator = generator.apply(thisArg, args)).next());
    });
}
```

So `yield` in that bundle is what `await` became, and the feature needed is
exactly `.next(v)`, `.throw(e)` and a `{value, done}` record. Counting `yield*`
and finding none turned an open-ended language feature into a bounded one.

### It is the SAME suspension `await` already had

A generator is a paused frame, and `coroutine_object` was already exactly that:
a proto, an ip, a register window, a receiver and the frame's own handlers, with
`reg_top` made relative so it can come back somewhere else in the stack. `await`
lifts a frame into one when it hits a pending promise. `yield` lifts it into one
too. **The only difference is who puts it back** - a settling promise for
`await`, an explicit `.next()` for a generator - so there is no second
suspension mechanism, and `op::yield_value` is `op::await_value` with the
promise machinery removed.

### THREE places had to learn it, and two were found only by the blocker moving

The ratchet's level stayed at 8 while its blocker string changed three times,
which is precisely what a pawl that records the blocker is for:

1. **The compiler** refused `yield` by name. Now it emits `op::yield_value`, and
   a generator's `return` is NOT promise-wrapped even when the function is also
   `async` - the record's `value` is the return, and making a promise of it is
   the driver's job.
2. **ctjs parsed `*method() {}` and threw the star away.** `eat_kw("async");
   eat_p("*");` - the async bit was kept and the generator bit was discarded, in
   both classes and object literals, so a generator method compiled as an
   ordinary function whose `yield` had nowhere to go. Babylon has **162** of
   them. Object-literal `{ *g() {} }` did not parse at all.
3. **`context::call`**, the C++ entry a native takes, built an ordinary frame.
   That is the path `Function.prototype.apply` uses - and __awaiter starts every
   generator with `(g = g.apply(thisArg, args)).next()`, so this was the one
   that mattered most and the last to be found.

### What is NOT implemented, by name

* **`yield*` delegates since 2026-09-12** (14.4.14): a bytecode loop around
  three hidden natives (`__ctbrowser_delegate_open/call/settle`); a sync
  generator hands the inner result object out as it is, and `.throw()` /
  `.return()` while delegating reach the inner iterator first
  (`generator_resume` keeps the record on the coroutine). An ASYNC generator
  delegates on the `next` path only; throw/return into one still take the old
  path.
* **`.return(v)` runs `finally` blocks since 2026-09-12** - for a SYNC
  generator: the return completion is a marker object (`@#return`) thrown at
  the yield under a fence `generator_resume` pushes beneath the frame, every
  catch clause in a generator starts with `__ctbrowser_catch_filter` which
  hands a marker on, and the marker escaping the frame is what answers
  `{value: v, done: true}`. A `yield` inside the finally suspends again; a
  finally that returns overrides. An async generator's `.return()` still
  finishes on the spot.
* **`for (x of gen())` IS LAZY (since 2026-09-12).** The loop opens its
  source through `__ctbrowser_for_of_open`, which answers undefined for what
  `op::iterable` materialises exactly (arrays, strings, proxies, Map/Set and
  their views, array-likes) and an iterator record for everything else; one
  register tested per iteration picks the index loop or `__ctbrowser_iter_next`,
  and `break` runs `__ctbrowser_iter_close`. So an infinite generator ends at
  `break`, a page's own `[Symbol.iterator]` is pulled one value at a time, and
  a non-iterable is the TypeError. Spread, `Array.from` and the Map/Set
  constructors still drain through `iterable_values` (bounded at 2^20 steps,
  then a RangeError). Not closed on a `return` or a throw out of the body -
  that needs a handler, which ctcompile's importer refuses a function for.
* **Async generators and `for await` exist since 2026-09-12** — the request
  queue lives on `coroutine_object`, `yield`/`return` await their operands,
  and `for await` lowers to the real protocol (see `docs/test262.md`). Array
  DESTRUCTURING runs the real protocol since 2026-09-12 (three natives,
  `__ctbrowser_iter_open/next/close`, IteratorClose on the normal early exit;
  not on a throw out of a default - a handler there costs the function its
  native body in ctcompile), and a
  page's own `[Symbol.iterator]()` iterates everywhere `iterable_values`
  is asked - still eagerly.

## THE TWO INSTRUMENTS, AND WHY BOTH

| | asks | Phaser | p5 |
|---|---|---|---|
| `*_ratchet` | how FAR — one number up a ladder | 10/10 | 12/12 |
| `*_api` | how WIDE — does each call work | 76/77 | 169/179 |

A ratchet at its ceiling says nothing about width, and this is not theoretical
in either corpus: p5's read 12/12 for days while `colorMode(HSB)` was broken,
and Phaser's read 10/10 while primitives could not reach `Object.prototype`.
Both were found by the wide, shallow instrument within one run of writing it.


## Numbers print as the specification says, since 2026-08-08

`context::to_string` was `std::to_string(double)` and `to_number` was
`std::stod`. Neither is the function ECMA-262 names, and the gap was not
cosmetic:

| | was | is |
|---|---|---|
| `String(1/3)` | `"0.333333"` | `"0.3333333333333333"` |
| `String(0.1+0.2)` | `"0.3"` | `"0.30000000000000004"` |
| `String(1e-7)` | `"0"` — as was **everything smaller** | `"1e-7"` |
| `String(1e21)` | `"1000000000000000000000"` | `"1e+21"` |
| `String(Number.MAX_VALUE)` | 309 literal digits | `"1.7976931348623157e+308"` |
| `(0.5).toFixed(0)` | `"0"` | `"1"` |
| `(5).toExponential()` | `"5.000000e+0"` | `"5e+0"` |
| `(1.5).toPrecision(3)` | `"1.5"` | `"1.50"` |
| `(1234.5).toPrecision(2)` | `"1.2e+03"` | `"1.2e+3"` |
| `Number("+5")` | `5` (via `strtod`) | `5` |
| `Number("inf")` | `Infinity` | `NaN` |

`core/number_format.hpp` is the whole of it - five functions, each named for
its clause - and `ctbrowser/unittests/js/number_format.cpp` pins every case against **V8**, plus
the round-trip property `Number(String(x)) === x` over twenty literals. The
expectations were taken from node before the code was written, which is the only
reason the file failed on the day it was committed rather than agreeing with the
bug.

Three things worth knowing:

* **`Number::toString` is not `%g`.** The switch to exponential is a rule about
  the decimal exponent - positional while `-6 < n <= 21` - so `1e20` prints
  twenty-one digits and `0.000001` prints in full. `std::to_chars` supplies the
  shortest round-tripping DIGITS; the notation is decided here.
* **`toFixed` rounds a tie away from zero** where correctly-rounded conversion
  rounds to even, so `to_chars` alone gives the wrong answer for `0.5`, `8.5`,
  `0.25` and `0.0625`. The tie is detected exactly rather than approximately:
  writing the magnitude as `m * 2^e` with `m` odd, `m * 10^f` is a half-integer
  precisely when `e + f == -1`.
* **`Math.cbrt` was an ulp out and nothing noticed**, because six-decimal
  printing rendered 3.0000000000000004 as "3" and `vm_stdlib` asserted the
  string. It asks with `===` now. Full-precision printing makes libm
  discrepancies visible in general - see `docs/performance.md`.


## Math, differentially tested against V8 (2026-08-08)

`ctbrowser/unittests/js/math_basics.cpp`. Every Math function was run against node (V8) over
~27,000 expressions; the ~90 differences collapsed to **seven defects**, all now
fixed and pinned. Each was planted back individually and the test caught it —
10, 8, 8, 2, 30, 7 and 7 assertions respectively.

| | was | is |
|---|---|---|
| `Math.round(0.49999999999999994)` | `1` | `0` |
| `Math.round(-0.5)`, `(-0.1)`, `(-0)` | `+0` | `-0` |
| `Math.round(2**53-1)` | `9007199254740992` | `9007199254740991` |
| `Math.min(1,NaN)` / `Math.max(1,NaN)` | `1` | `NaN` |
| `Math.min(0,-0)` | `+0` | `-0` |
| `Math.hypot(1e300,1e300)` | `Infinity` | `1.4142135623730952e+300` |
| `Math.hypot(3e-300,4e-300)` | `0` | `5e-300` |
| `Math.hypot(Infinity,NaN)` | `NaN` | `Infinity` |
| `Math.pow(1,NaN)`, `1 ** NaN` | `1` | `NaN` |
| `Math.SQRT1_2` | `0.7071067811865475` | `0.7071067811865476` |
| `Math.sin()`, `Math.cos()`, … (27 of them) | `sin(0)`, `1`, … | `NaN` |
| `1e400` | `0` | `Infinity` |

The root causes are worth knowing because six of the seven are a *correct-looking
line*:

* **`floor(x + 0.5)` is not `Math.round`** and fails three separate clauses. The
  addition rounds (`0.49999999999999994 + 0.5` is exactly halfway and ties-to-even
  lifts it to 1.0); it destroys the sign of zero; and above 2^52 it is inexact, so
  an integral Number gets *moved* where the spec returns it unchanged.
* **`std::min`/`std::max` are the wrong function.** Both are written on `<`, which
  is false for every comparison involving NaN — so the NaN silently vanished —
  and `-0 < +0` is false, so which zero came back depended on argument order.
* **`sqrt(Σx²)` overflows and underflows.** Squaring passes 1.8e308 above ~1.34e154
  and flushes to zero below ~1e-162. Scaling by the largest magnitude fixes both
  and keeps `hypot(3,4)` exactly 5.
* **C99 says `pow(±1, y)` is 1 for every `y`**; `Number::exponentiate` says NaN
  when the exponent is NaN or infinite. `context::exponentiate` now owns that, and
  the `**` opcode shares it — it had the same bug in a second place.
* **`1/sqrt2` rounds twice.** `sqrt2/2` rounds once, because halving is exact.
  The engine used to disagree with its own `Math.sqrt(0.5)`.
* **A missing argument is `undefined`, so `ToNumber` gives NaN**, not 0. The
  shared `num_at` defaults to 0 — correct for `"abc".slice()` — so Math got its
  own accessor rather than thirty other call sites changing underneath.
* **`std::from_chars` reports overflow without writing the value**, and the error
  was unchecked, so `1e400` compiled to `0`. `out_of_range_value` decides which
  way it went and is shared with `ToNumber`, which had the identical hole.

### What the test deliberately does NOT assert

21.3.2 marks the *value* of `sin cos tan asin acos atan atan2 exp expm1 log log1p
log2 log10 pow cbrt sinh cosh tanh asinh acosh atanh hypot` **implementation-
approximated**. Ours is libm's answer, and Linux links glibc while the Windows
cross-build links Microsoft's UCRT — so no decimal string for any of those values
appears in the file. Asserting `Math.sin(1) === 0.8414709848078965` would be
asserting which libc the machine has, and would fail the cross-build for a reason
that is not a bug. What is asserted instead: the mandated special cases (NaN, ±0,
±Infinity, domain edges), the exact-by-spec functions in full, results that are
exactly representable (`sqrt(16)`, `cbrt(27)`, `log2(8)`, `hypot(3,4)`), and for
`Math.random` a *contract* — range and finiteness — never a value.

**Boost.Math was considered for the same reason and turned down** — see
`docs/build.md`.

### Known and NOT fixed here

Found by the same sweep, left alone because none is a Math defect and each is its
own piece of work: natives carry no `length` or `name` (engine-wide); `Math` has
no property attributes, so `Object.keys(Math)` is 43 rather than 0 and `Math.PI =
42` sticks; `Object.is`, `Object.seal`, `Object.isExtensible` and `globalThis` do
not exist; `Object.prototype.toString` ignores `@@toStringTag`; every native is
constructible (`new Math.abs(1)` does not throw); `ToNumber` on an object skips
ToPrimitive, so `Math.abs([])` is NaN rather than 0; the JS whitespace set is
ASCII-only, so `Number(" 1.5")` is NaN; and `Math.f16round` is absent —
deliberately, since `_Float16` support varies across the aarch64 and armv7 mingw
targets and no corpus uses it.


## Strings, differentially tested against V8 (2026-08-09)

`ctbrowser/unittests/js/string_basics.cpp`. The whole of `String.prototype` and the relational
operators were run against node (V8) over ~1,550 expressions. The sweep found
**233 differences and 72 hangs**; the fixes below took that to **60 and 0**, and
8 of the remaining 60 are deliberate. Every fix was planted back individually
and the test caught it (12, 4, 3 and 3 assertions respectively).
