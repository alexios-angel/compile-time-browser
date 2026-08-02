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
gets on a ladder of 12 rungs and `tests/p5-ratchet.txt` records the high-water
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
