# Script — the compiler, the VM, the standard library

`include/ctbrowser/script/` and `src/script/` — `compile.hpp`/`compile.cpp`
(JS -> bytecode; the header declares one function and the .cpp holds the whole
compiler), `vm.hpp`/`vm.cpp` (the register machine), `value.hpp` (NaN boxing),
`bytecode.hpp`, `builtins.hpp`/`builtins.cpp` (the standard library). The DOM
API pages call is not here - that is `shell/bindings.hpp`, in `docs/shell.md`.

## JAVASCRIPT (2026-07-25)

**The MDN breakout tutorial runs, unmodified** — `examples/pong.cpp` loads
`examples/pages/pong.html`, a byte-for-byte copy. `examples/fetchboard.html`
compiles too, and the 66 KB bundled `space-invaders.html` stops at exactly ONE
thing: a regex literal.

The compiler covers the language now: `+=` and friends, member/index `++`, real
`this`, `break`/`continue`/labels, `do..while`, `try`/`catch`/`finally`/`throw`
(VM handler stack, unwinds call frames), computed method calls (`a[m]()` keeps
its receiver), `for..of`/`for..in`, template literals, `switch` (with
fallthrough), `class` + `new` + `extends` + **`super`**, optional chaining,
spread (array and object), computed object keys, `delete`, `in`, `instanceof`,
the bitwise operators (ToInt32/ToUint32, so `-1 >>> 0` is 4294967295), and
`async`/`await`.

**Still rejected, each by name rather than mis-compiled**: `regex` (no regex
engine — this is what stops space-invaders), `yield`/generators, tagged
templates, object-literal get/set accessors. The comma operator is a ctjs
PARSER gap, not a compiler one.

**Functions are objects** — a closure carries a property table, which is where a
class keeps its statics, its `prototype` and the `__home` that makes `super`
resolve against the class a method was WRITTEN in rather than against `this`
(three-deep hierarchies recurse forever otherwise).

**Promises are SETTLED-ONLY**, like the previous engine's: no job queue, no `new Promise(executor)`,
`then` runs its callback immediately. `async function` returns a settled promise
(`op::wrap_promise`, through a factory hook the standard library installs — the
VM cannot build a promise by itself). Enough for `await fetch(url)` and
`.then(r => r.json())`; NOT enough for code that depends on ordering between a
`then` and the statements around it.

**`===` compares STRINGS BY CONTENT** — it compared the NaN-boxed words, which
is right for objects (identity) and singletons and wrong for strings, since two
strings with the same characters are almost never the same allocation. So
`e.code === "Space"` was false for every event, `switch` on a string never
matched a case, and `indexOf`/`includes` could not find a string in an array.
`==` always did compare content, which is why the one page in the suite that
uses it (pong) worked and invaders did not. `NaN`, `Infinity` and `undefined`
are defined globals now too — `NaN` was an undefined global, so `NaN === NaN`
was TRUE.

**Standard library** is `src/script/builtins.cpp` — `Math`, `Array.prototype`
(incl. map/filter/reduce/sort, which call back into the VM via
`context::call`), `String.prototype`, `Number.prototype`, `Object` statics,
`JSON` parse/stringify, `Promise` (resolve/reject/all),
`parseInt`/`parseFloat`/`isNaN`/`String`/`Number`. Reached through **prototype
tables per value kind** (`context::set_prototype`) plus **per-object prototype
chains** (`class`/`extends`). `Math.random` is seeded and DETERMINISTIC by
default — the test story is byte-comparable goldens, and a page drawing with
random cannot have one otherwise.

Top-level `var` is a GLOBAL by design (pages define functions the host calls by
name), so the only frame-0 locals are for..of items and catch parameters — and
those ARE capturable, which is what makes `for (const x of xs) fns.push(() => x)`
close over each element at the top level.

`tests/page_scripts` compiles the real example pages and asserts what each
one does; `tests/vm_basics` has a test per language feature.

## WHAT p5.js NEEDED (2026-07-29)

**p5.js v2.3.1 runs, in BOTH builds** — 4.5 MB and 138,938 lines that nobody
wrote for this engine. It lexes, parses (282,028 nodes), compiles (4,754 functions), executes
its whole top-level IIFE, builds a sketch, runs `setup()`, drives `draw()` from
`requestAnimationFrame`, and paints. `tests/p5_ratchet.cpp` measures how far it
gets on a ladder of 12 rungs and `tests/corpus/p5/p5-ratchet.txt` records the high-water
mark; the level may not go down. TWO numbers are recorded, each with its own
pawl: `level` is p5-min, where the page defines `IS_MINIFIED` as p5's own
minified build does, and `full-level` is the same ladder with the flag left
undefined - the Friendly Error System and i18next's setup both in play. Both
read 12, and neither reaches the network. "p5 runs" is a different claim when
half of p5 is switched off, which is why the second one is measured rather than
assumed. `tools/p5-ratchet.py --survey` measures each of
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

- `obj[key](...)` passed the KEY as argument 0, because the compiler evaluated
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

**The limit that remains**: there is no `Symbol.iterator` dispatch, so an object
with a `next()` of its own is not iterated. A generator would need the same
machinery and is out of scope (below).

### Reading a property of undefined does not throw

`undefined.x` gives `undefined` here rather than a TypeError. It is why the class
expression leak above took an afternoon: `p5` was undefined, `.TableRow` was
undefined, and the error surfaced one step later naming `TableRow`. Left as it is
for now, and written down because it turns a precise failure into a vague one.

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

The lexer is next and has its own document: `docs/lexer-plan.md`.

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
handler. `tests/phaser_ratchet.cpp` went 7/10 → 9/10; the tenth rung is
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
        function rejected(v)  { step(generator["throw"](v)); }
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

* **`yield*`** - delegation. Zero uses across all three corpora; it would be a
  loop over the inner iterator and is not written because nothing asks.
* **`.return(v)` does not run `finally` blocks.** It marks the generator done
  and answers `{value: v, done: true}`. The spec resumes the body to run any
  pending `finally`, which needs the unwinder rather than the resumer.
* **`for (x of gen())` MATERIALIZES.** `op::iterable` hands back an array by
  construction, so the generator is drained - up to 2^20 values - rather than
  pulled lazily. An INFINITE generator hangs there instead of looping for ever,
  which is a bounded failure rather than a silent one. Laziness means a real
  iterator protocol in the loop opcodes, which no corpus has asked for.
* **Async generators** (`async function*`) parse and run as plain generators;
  `for await` is not implemented.

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

`script/number_format.hpp` is the whole of it - five functions, each named for
its clause - and `tests/number_format.cpp` pins every case against **V8**, plus
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
  printing rendered 3.0000000000000004 as "3" and `vm_basics` asserted the
  string. It asks with `===` now. Full-precision printing makes libm
  discrepancies visible in general - see `docs/performance.md`.


## Math, differentially tested against V8 (2026-08-08)

`tests/math_basics.cpp`. Every Math function was run against node (V8) over
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

`tests/string_basics.cpp`. The whole of `String.prototype` and the relational
operators were run against node (V8) over ~1,550 expressions. The sweep found
**233 differences and 72 hangs**; the fixes below took that to **60 and 0**, and
8 of the remaining 60 are deliberate. Every fix was planted back individually
and the test caught it (12, 4, 3 and 3 assertions respectively).

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

`tests/string_basics.cpp` **pins the current (wrong) answers** in a labelled
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
* **`Symbol().description` is `""`, not `undefined`** - an absent description
  and an empty one are the same `std::string` here.
* **Array holes are materialised**: `0 in [,1]` is true and `Object.keys([,1])`
  is empty. Arrays are dense vectors, so a hole needs a representation.
* **No boxing**: `new Boolean(false)` is the primitive, so it stays falsy where
  every object is truthy. Deliberate - see `context::construct`.
* **No BigInt at all.** `1n` does not lex, and it is a PARSE error, so a bundle
  containing one fails as a whole. That is a type to add, not a bug to fix;
  `tests/bigint_basics.cpp` is the acceptance list.


## BigInt (2026-08-09)

The seventh primitive type. 61 expressions differentially tested against node
(V8) - arithmetic, comparison, conversion and every error the specification
names - with no differences. `tests/bigint_basics.cpp`, which until today
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

### The GMP backend, and why it is off (2026-08-09)

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

**The cross-build works, including the assembly.** `tools/build-gmp-mingw.sh`
builds it for llvm-mingw; the common advice that clang needs
`--disable-assembly` for GMP is not true on this toolchain, and a static
`.exe` linking it runs. Two autotools traps are worth knowing, both in that
script: under WSL, `binfmt_misc` runs `.exe` files, so configure decides it is
**not** cross-compiling and then dies on "cannot determine executable suffix"
(pass `--build` explicitly); and `CC_FOR_BUILD` must be a native compiler or
the build tries to execute the Windows helper programs it just produced.

Both backends are held to `tests/bigint_basics.cpp`, and it passes on both, as
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
wrong answer - and `tests/symbol_basics.cpp` caught it.
