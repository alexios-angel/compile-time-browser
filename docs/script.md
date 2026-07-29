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
in a browser. `structuredClone` covers data only. Regex has no lookbehind and no
backreferences. Strings are BYTES, so `normalize` is the identity and
`codePointAt` agrees with `charCodeAt` rather than pretending to a UTF-16 view
nothing else here has.
