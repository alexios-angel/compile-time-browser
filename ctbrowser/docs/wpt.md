# web-platform-tests

**The suite every browser is measured against, pointed at this one.** WPT is the
shared conformance corpus for HTML, the DOM, CSS and the rest of the platform;
Chrome, Firefox and WebKit all run it, and their scores are public. This is what
it says about ctbrowser.

It is a MEASURING INSTRUMENT, not a pass/fail gate on the whole engine. The
engine is a long way from passing WPT and that is not news — what the instrument
is for is knowing *how far*, in numbers, per suite, and noticing the day one of
them moves.

    tools/wpt/fetch-wpt.sh                     the corpus, once
    tools/wpt/run-wpt.py --selftest            prove the harness works
    tools/wpt/run-wpt.py --dir dom/nodes       one directory, one table

## The baseline — 2026-09-03

**1,632 tests, 275.6 s, four workers, and still not one crash.** Measured on the
devbox against WPT `3f6b09ae`, engine at this branch.

| suite | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP | files |
|---|---:|---:|---:|---:|---:|---:|---:|
| `dom/nodes` | 42 | 199 | 32 | 0 | 36 | 53 | 362 |
| `dom/events` | 24 | 45 | 11 | 0 | 11 | 85 | 176 |
| `html/dom` | 27 | 126 | 19 | 0 | 55 | 138 | 365 |
| `css/cssom` | 10 | 145 | 17 | 0 | 20 | 29 | 221 |
| `css/css-values` | 16 | 213 | 3 | 0 | 39 | 237 | 508 |
| **total** | **119** | **728** | **82** | **0** | **161** | **542** | **1,632** |

Subtests: **2,262 PASS, 16,331 FAIL, 746 NOTRUN, 92 TIMEOUT.**

**119 of 1,090 tests that ran, which is 10.9%.** The first measurement, one day
earlier, was 52 of 1,090 — 4.8%. What moved it is in the next section and every
number in it was measured rather than estimated.

**Zero crashes** again, now over a much larger executed surface: the suites run
thousands of pages written to break browsers under a 4 GB `ulimit -v`, and the
engine has never once segfaulted, aborted or exhausted the cap.

### The previous baseline, 2026-09-02, for comparison

| suite | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP |
|---|---:|---:|---:|---:|---:|---:|
| `dom/nodes` | 14 | 215 | 29 | 0 | 51 | 53 |
| `dom/events` | 6 | 62 | 10 | 0 | 13 | 85 |
| `html/dom` | 11 | 135 | 7 | 0 | 74 | 138 |
| `css/cssom` | 8 | 148 | 15 | 0 | 21 | 29 |
| `css/css-values` | 13 | 128 | 2 | 0 | 128 | 237 |
| **total** | **52** | **688** | **63** | **0** | **287** | **542** |

**Two of the columns are not comparable across the two tables, and saying which
is the point of keeping both.**

- **`css/css-values` moved for a reason that is not the engine.** `css/support/`
  was not in the sparse checkout on 2026-09-02, so 94 of that suite's 128
  harness errors were one missing helper file each. It is fetched now, which
  converted 84 of them into real failures and three into passes with the engine
  UNCHANGED between the two runs. FAIL going up by 84 is the instrument working.
- **HARNESS_ERROR falling and TIMEOUT rising is mostly the same story.** A test
  that used to die on the first missing function now runs. `html/dom`'s eight
  `reflection-*.html` files each run thousands of subtests and run out of clock;
  three `NodeList-static-length-getter-tampered` files reach far enough to hang.
  Both were failing before and are failing now, differently and more usefully.

### What moved, and what it cost

Measured one API at a time, each with its own commit and its own before/after:

| change | measured effect |
|---|---|
| `getElementsByClassName`, `getElementsByName` — **live** collections through a Proxy | `dom/nodes` 14 → 38, `html/dom` 11 → 26. **39 files** FAIL → PASS |
| the event **path**: `createEvent`, `dispatchEvent`, `currentTarget`, `eventPhase`, the propagation flags, `Event`/`CustomEvent` | `dom/events` 6 → 19 PASS, subtests 28 → 139 |
| `new EventTarget()`, and the duplicate-listener rule | `dom/events` 19 → 23 |
| fragments, comments, `append`/`prepend`/`before`/`after`/`replaceWith`, `cloneNode` | `dom/nodes` 38 → 41, twelve files converted from HARNESS_ERROR to a measurement |
| `document.implementation` + `hasFeature` | one file, **136 assertions**, FAIL → PASS |
| `createElementNS` and the namespace an element remembers | `dom/nodes` subtests 340 → 486; no new file passes |

### What is actually failing now

| count | cause |
|---:|---|
| 121 | `assert_equals: expected (string) … but got (undefined)` — mostly CSS values |
| 85 | `assert_equals: expected … but got …` — wrong answers, not missing ones |
| 50 | `assert_true: expected true got false` |
| 39 | `document.write`, from `generateParserDelay` |
| 32 | `assert_false: expected false got true` |
| 27 | `createHTMLDocument` |
| 19 | `assert_array_equals: value is …, expected array` — see below |
| 18 | `new MutationObserver` |
| 17 | `createDocument` |
| 14 | `attachShadow` |
| 14 | `setProperty` on a Web Animations keyframe |
| 11 | `new CSSStyleSheet` |
| 10 | `setAttributeNS` |

**Three of these are worth naming precisely, because each is one defect standing
in front of many tests:**

- **`"length" in []` is FALSE in this engine** while `[].hasOwnProperty("length")`
  is true — measured through `ctdrive`, not inferred. `context::has_property` in
  `lib/Script/vm/objects.cpp` parses an array key as an index and never answers
  `"length"`. `assert_array_equals` opens with `"length" in actual`, so **every
  comparison against an array a page built fails before it compares anything**.
  It is the sole remaining cause in eleven `dom/events` files and in eight more
  elsewhere.
- **A native cannot throw a DOMException.** `assert_throws_dom` requires the
  thrown object's `code` and `name` AND `e.constructor === DOMException`. The
  only way to throw from a native here is `context::throw_error(kind, message)`,
  which builds an Error with the right `name`, no `code`, and Error.prototype.
  A `throw_value(value)` on the context would make `createElementNS`'s 110
  throwing assertions, `createProcessingInstruction`'s eight and every other
  `assert_throws_dom` reachable.
- **`createHTMLDocument` and `createDocument` need a SECOND Document**, which
  the bindings cannot hold: there is one `document *` and every element wrapper
  is keyed on a node id that means nothing against any other. 27 files.

### Skips, all 542 of them

| count | reason |
|---:|---|
| 331 | reftest: needs a reference render |
| 120 | not a testharness test |
| 76 | testdriver: needs WebDriver input injection |
| 8 | variant: the driver opens a file and has no query string |
| 6 | https: needs a TLS origin |
| 1 | `global=dedicatedworker`: no Worker |

Every one names a feature. **No skip in this list exists to improve a number.**


## The corpus

Fetched by `tools/wpt/fetch-wpt.sh` into `~/.cache/wpt` (`$WPT_DIR` overrides),
**sparse and shallow, at a pinned commit**, and never committed here.

| | |
|---|---|
| pin | `3f6b09ae3ed55280074645ce38e9002f52fc60a8` |
| where | `~/.cache/wpt`, outside the source tree |
| size | ~47 MB, 2,465 files on disk out of WPT's 162,834 |
| verify | `tools/wpt/fetch-wpt.sh --verify` — checks the SHA *and* that the sparse patterns matched something |

Vendoring it was never on the table: WPT is ~1.5 million files, it changes every
day, and what has to be reproducible is the **commit**, not a copy of the bytes.
`~/.cache` rather than `build/` because a reconfigure wipes the build tree and
re-downloading a corpus because somebody deleted a `CMakeCache.txt` is a bad
trade.

**THE CHECKOUT IS SHARED, on the devbox.** One `~/.cache/wpt` serves every agent
and every worktree on that machine, and it is not covered by the per-directory
isolation that keeps two builds apart. On 2026-09-03 one agent added
`/css/support/` to it and another agent's next measurement moved by 87 files in
a suite its change could not have touched. **If a number moves in a suite the
change cannot explain, check `.git/info/sparse-checkout` and its mtime before
believing the engine did it** — that is what identified this one. The fetch list
in `fetch-wpt.sh` and the expectations file are kept in step deliberately so a
fresh checkout and a shared one agree.

`--verify` checks two things and the second is the one that matters: a sparse
checkout whose patterns matched nothing leaves a directory that exists, has the
right `HEAD`, and cannot run a single test. That is a green verify followed by a
suite of identical harness errors, so `resources/testharness.js` and `dom/nodes`
are checked **by name**.

### Which suites, and why

The sparse list is in `fetch-wpt.sh`. It is chosen by what this engine
implements, because a suite for a feature the engine has never heard of reports
the same thing for every file in it and that is noise in a table rather than a
finding.

| path | why it is fetched |
|---|---|
| `resources/` | testharness.js itself. Not optional |
| `common/` | the fixtures the suites import |
| `dom/nodes/` | the tree: `Node`, `Element`, `Attr`, `Document`. The engine has its own WHATWG tokenizer and tree builder, so this is the closest thing to a direct measurement of it |
| `dom/events/` | `EventTarget`, dispatch, capture/bubble, listener options — all of which `lib/Shell/bindings/events.cpp` implements |
| `html/dom/` | reflection: does `el.id = "x"` change the attribute, and back |
| `css/cssom/` | `getComputedStyle` and the style declaration objects, which `lib/Shell/bindings/computed_style.cpp` answers and `tools/check/css-parity.py` already measures against Chrome a different way |
| `css/css-values/` | value parsing and computation — `calc()`, lengths, units — against the CSS Syntax Level 3 front end in `lib/Style/css/` |
| `css/support/` | the helpers the two `css/` suites import. **Not optional either**: `test_valid_value`, `test_computed_value`, `test_math_used`, `test_interpolation` and `test_specified_serialization` all live here, and a test that cannot load one reports HARNESS_ERROR without running a subtest. Adding it on 2026-09-03 converted 87 harness errors into real measurements |

**Left out on purpose**, and each for a reason rather than a score: everything
needing a network origin (`fetch/`, `xhr/`, `service-workers/` — the runner has
no server and `CTBROWSER_NETWORK=0`), everything needing a second browsing
context (`html/browsers/`, `webmessaging/` — no iframes, no `window.open`, no
workers), and the reftest suites (`css/css-flexbox/` and friends are render
comparisons, which is `tools/check/check-render.cmake`'s instrument, not this
one).

## The results hook

`tools/wpt/testharnessreport.js` is **the slot WPT leaves for the
implementation**. Every test loads two scripts: `testharness.js`, which is the
harness and is the same for everybody, and `testharnessreport.js`, whose copy in
the checkout does nothing at all. `run-wpt.py` copies ours over it on **every**
run, so a re-fetch can never leave the corpus silently reporting nothing. This
is exactly what every browser vendor does.

**Where the results go**: onto `window.__wpt_state`, as a string that is already
JSON, with `window.__wpt_done` set after it. The runner reads it back through
`ctdrive`'s `eval`, which returns whatever the snippet *logged* — so one
`console.log` of one string is the whole channel, and it needs nothing from the
engine a page could not do.

Three decisions in that file are worth stating:

- **The JSON is built by hand.** `JSON.stringify` would make every result in the
  suite depend on this engine's `JSON.stringify` being right about nested
  objects, string escaping and lone surrogates — and when it was not, the
  failure would arrive as a hundred harness errors that look like the DOM is
  broken. The one thing that must not be under test is the instrument. Non-ASCII
  is escaped too, so the payload crossing a socket, a `std::string` and a Python
  decode is pure ASCII and none of the three has to agree about an encoding.
- **`setup({output: false})`**, as wptrunner's own hook does. The results table
  testharness builds into `#log` is for a human with a browser window; building
  it puts `createElementNS`, `appendChild` and `textContent` between every test
  and its result, so a defect in any of the three would arrive as a corpus-wide
  failure saying nothing about the test that found it.
- **A missing harness is reported, not silent.** If `add_completion_callback` is
  not a function the hook publishes a `HARNESS_ERROR` saying so. Without that,
  a page that failed to load the harness reports zero subtests — and zero
  failing subtests is exactly what a naive runner scores as a pass.

### How console output and errors reach the terminal today

They already did, and nothing new was needed for the channel itself:

- `console.log` accumulates in `browser::bindings().console_output()`, and
  `ctdrive`'s `eval` returns everything logged during the snippet. That is the
  mechanism `tools/check/compare.py` has used all along.
- an uncaught throw in a `<script>` lands in `browser::script_error()`, which
  `ctdrive`'s `info` command returns and which `ctbrowse` prints. The runner
  reads it when a page dies without publishing, so a timeout can say *why*.

What was missing was the **page's** half of that — see the next section.

## What WPT found in the engine

**Seven gaps**, in the order they blocked things. Each is a genuine engine
defect rather than harness scaffolding, each is fixed on this branch, and the
full suite (118 tests, goldens included) still passes with all seven fixed.

Two of them were found by the negative proofs rather than by the corpus, which
is the argument for having them: `--selftest` reported the wrong outcome for
three of its five fixtures the first time it ran, and `window.parent` and the
stuck VM fault were what those misses turned out to be. The regex defect came
the other way round - the corpus ran, the numbers looked plausible, and the
FAILURE MESSAGES were gibberish.

| | what was wrong | what it looked like |
|---|---|---|
| `self` was not defined | `window` and `globalThis` were, `self` was not | testharness.js is `(function (global_scope) { … }(self))`, so `global_scope` was `undefined` — and this engine treats a property **store** on undefined as a no-op rather than a TypeError, so the harness ran to completion, reported no error, and defined not one of its globals. The page then failed with "`test` is undefined", forty lines from the cause. **100% of WPT was unrunnable for one missing alias.** |
| a leading `/` had no document root | `<script src="/resources/testharness.js">` was read off the root of the disk | every test in the corpus loaded with no harness at all, and every one of them reported the same error — none of which was about the engine. `shell::asset_registry::set_document_root`, set from `CTBROWSER_DOC_ROOT`, is what a server would have resolved it against |
| `addEventListener` was not a bare global | it was a property of `window` only, and the window proxy's fallback goes `window.x` → global, never the other way | testharness installs its uncaught-exception handler as a receiverless `addEventListener("error", …)`. That threw a TypeError at the very end of the harness's own initialisation — after the asserts were exposed and before `on_tests_ready()`, which is the worst possible place: the file half-ran and said nothing |
| there was no `load` event | the engine sets `document.readyState` to `"complete"` from the start, so every library that *asks* before it listens — p5, Phaser, Babylon — takes its already-loaded branch and never needed one | testharness does the opposite: it listens unconditionally and sets `all_loaded` in the handler, and `Tests.all_done()` requires that flag. **Every test ran its subtests, passed them, and then sat until the harness's own 10-second timeout and reported TIMEOUT.** `browser::tick` now fires `DOMContentLoaded` and then `load` at the window, once per document |
| an uncaught throw was invisible to the page | it reached `browser::script_error()` — an *embedder* channel — and no `error` event was ever dispatched | a test that threw during load could not be told from one that never finished. `dom_bindings::dispatch_error` gives the page `window.onerror` and `addEventListener("error", …)`, which is what turns that case into a `HARNESS_ERROR` |
| `window.parent` was undefined | there are no iframes, so nothing had ever needed the browsing-context chain | testharness walks `[self … top, opener]` to broadcast state. With `parent` undefined the walk ran one step past the end and called `postMessage` on `undefined` — **inside a completion callback**, and `notify_complete` runs those in a bare `forEach` with no try/catch, so the throw killed every later callback including the one that reports results. Every test that ran perfectly reported nothing and was recorded as a TIMEOUT. `parent`/`top` are now this window and `opener` is null, which is what the spec says a top-level browsing context reports |
| a thrown script left the VM refusing to run | `run()` leaves `failed_` set, and every C++ entry into JavaScript declines while it is | so dispatching the new `error` event ran **no listener at all**, reported the original error a second time as a "callback fault", and left the page believing nothing had gone wrong. The harness then finished normally and **a page that threw during load was reported as a PASS** — the single worst answer this instrument can give. The fault is now taken before the event is dispatched: `script_error_` already holds the text, and the page cannot be handed anything while the VM is refusing to run its code |
| `\uXXXX` and `\xHH` in a regex were not decoded | `rx_escape_char` fell through to "the char itself", so `\u` was the letter `u` and the four hex digits became four more class members. `[\udc00-\udfff]` parsed as `{u,d,c,0,f}` plus the **range `'0'`–`'u'`** | which matches most of ASCII, silently. testharness sanitises every test **name** and every assertion **message** through `str.replace(/([\ud800-\udbff]+)…/g, …)`, so that bogus range hit nearly every character of every string the harness had to say: 2,343 failing subtests in `dom/nodes` came back with their names rewritten into runs of `U+61U+73U+73…`. A corpus-wide corruption of the **results**, from one escape. Now decoded; above `0xFF` the byte-based matcher refuses the pattern rather than approximating it, which makes the sanitiser the no-op it should always have been |

### And six more, found in the second pass

The gaps above stopped the corpus running at all. These were found by reading
what it then said, one API at a time — each is measured in the table further up,
and each has a commit of its own.

| | what was wrong | what it looked like |
|---|---|---|
| `getElementsByClassName` did not exist | neither did `getElementsByName`, `removeAttribute`, `hasAttribute`, `nodeName`, `nodeType` or `localName` | 39 test files, and it was the largest single cause in the first baseline. The hard half was not the search but the COLLECTION: `getElementsBy*` returns a live view, and five of the suite's own tests take one, mutate the document and read it again |
| the document and the window were ONE listener bucket | `listener::target` empty meant "one of the two, we cannot tell" | invisible until an event carried `currentTarget`, and then both reported the same object. `removeEventListener` on one could take the other's listener away |
| dispatch had no path, no phase and no flags | it fired the global bucket, walked the ancestors, and fired the global bucket again | `currentTarget` and `eventPhase` read `undefined`, `stopPropagation` was a no-op, and nothing a page CONSTRUCTED could be dispatched at all — `document.createEvent` and `dispatchEvent` did not exist |
| a `once` listener was reaped during a NESTED dispatch | the compaction ran at the end of every dispatch, including one started inside a listener | it shifts the vector the outer dispatch is indexing, so the listener after the removed one is skipped. Reproduced by `AddEventListenerOptions-once.any.js`, whose second case dispatches from inside a `once` listener that re-registers itself |
| a listener registered twice was registered twice | the DOM says (type, callback, capture) on one target is a listener's IDENTITY | a page that registers defensively in a function it calls twice got two calls per event, and a `once` listener registered twice fired twice |
| `document.implementation` did not exist | so `hasFeature` did not, and the test's `.apply(...)` on `undefined` did not either | 136 assertions in one file, reported as "`apply` is not a function" — a message about a method nobody was missing, forty lines from the cause |

### And two that are NOT fixed, both outside the DOM

Named here because each stands in front of many tests and neither is a DOM gap:

- **`"length" in []` is false.** `context::has_property` parses an array key as
  an index and never answers `"length"`, while `hasOwnProperty("length")`
  answers true — measured through `ctdrive`. `assert_array_equals` opens with
  exactly that test, so no comparison against a page-built array can pass.
- **A native cannot throw a DOMException.** `context::throw_error(kind, message)`
  is the only way to throw from one, and it builds an Error: right `name`, no
  `code`, `constructor === Error`. `assert_throws_dom` checks all three.


## Running it

**One directory at a time is the interface.** A batch that runs everything and
prints one number hides exactly what you need.

```bash
tools/wpt/fetch-wpt.sh                            # once; ~40 MB
tools/wpt/run-wpt.py --selftest                   # FIRST: does the harness fail?
tools/wpt/run-wpt.py --dir dom/nodes              # a table
tools/wpt/run-wpt.py --dir dom/events --filter Event-dispatch
tools/wpt/run-wpt.py --dir css/cssom --json /tmp/cssom.json --tsv /tmp/cssom.tsv
```

Useful flags: `--jobs N` (4 by default — **the devbox has 8 vCPUs and is
shared**), `--memory-mb N` (4096, applied as `ulimit -v` to each driver),
`--limit N`, `--driver PATH`.

Each test is **one `ctdrive` process**, which is what makes a crash a crash: the
process dies, the runner sees the signal and names it, and the next test is
unaffected. The environment is pinned the way the render checks pin theirs —
`CTBROWSER_GL_DRIVER=deterministic`, `SDL_VIDEODRIVER=offscreen`,
`CTBROWSER_FONTS=font8x8`, `CTBROWSER_NETWORK=0` — so a run says the same thing
twice and a box with no GPU is not a variable.

### Timeouts and the memory cap

WPT's own metadata is the source of truth: `<meta name="timeout" content="long">`
means 60 s and everything else means 10 s. The runner adds a 5 s margin on top,
and the margin is the point — the harness has its **own** 10-second timeout and
must be given the chance to report it, because a page killed at exactly its
deadline loses the subtest detail the harness was about to publish.

`ulimit -v` is applied by a one-line `/bin/sh` wrapper rather than by Python's
`preexec_fn`, which the standard library documents as unsafe from a thread pool
— and this runner has four workers, so that is not a theoretical objection.
`ulimit -c 0` goes with it: hundreds of multi-GB cores is a build machine down,
not a finding.

### `.any.js` and the wrappers

A `.any.js` test has **no HTML on disk at all** — wptrunner synthesises
`<test>.any.html` when the browser asks for it. The runner builds the same page,
reading the `// META:` lines the way the manifest does (`global=`, `script=`,
`timeout=`), and writes it *beside* the script so its relative `<script src>`
resolves. Wrappers are named `*.ctwpt.html`, deleted at the end of a run, and
swept at the start of the next one so an interrupted run cannot leave the corpus
with tests nobody wrote.

## What is skipped, and why

Every skip names a **feature this engine does not implement**, never a number.
The counts are in the baseline table above; the reasons are these and only these:

| reason | what it means |
|---|---|
| `reftest: needs a reference render, not a harness result` | `<link rel=match>`. A different instrument — `tools/check/check-render.cmake` |
| `not a testharness test` | the file never loads `testharness.js`; usually a fixture or a helper page |
| `testdriver: needs WebDriver input injection` | `test_driver.click()` and friends. `ctdrive` can synthesise input, but not through testdriver's protocol |
| `variant: the driver opens a file and has no query string` | `<meta name="variant" content="?1-10">`. The driver takes a path, not a URL |
| `global=…: no Worker/ServiceWorker in this engine` | a `.any.js` whose declared scopes exclude `window` |
| `Worker:` / `SharedWorker:` / `ServiceWorker: not implemented` | the `.worker.` / `.sharedworker.` / `.serviceworker.` filename spellings |
| `https: needs a TLS origin` / `h2: needs an HTTP/2 server` | WPT encodes its server requirements in the filename, and there is no server here |

A test that this engine simply fails is **not** skipped. It is run, it fails, and
the failure is recorded — which is the difference between a measurement and a
score.

## Expectations, and the gate

`tools/wpt/expectations.txt` is **every deviation from "it passes"**, one line
each, sorted, keyed on the test path:

    dom/nodes/Node-cloneNode.html	FAIL
    dom/nodes/Node-cloneNode.html	SUBTEST	FAIL	"Node.cloneNode() on a DocumentType"
    dom/nodes/Node-baseURI.html	SKIP	not a testharness test

A test with **no** line is expected to pass with every subtest passing, so the
file only ever grows when the engine is wrong and only shrinks when it is fixed.
Subtest names are JSON-quoted because WPT has names containing tabs and
newlines. Assertion *messages* are deliberately absent: they carry values that
differ run to run, and a file that churns is a file nobody re-reads.

**The gate fails in both directions.**

```bash
tools/wpt/run-wpt.py --dir dom/nodes --check              # against expectations
tools/wpt/run-wpt.py --dir dom/nodes --update-expectations
```

A new failure is a regression. **A line that has started passing is a failure
too**, and that is the half that keeps the file honest: an expectations file
that only ever grows is a file that tells lies about the engine, and a gate that
checks one direction would keep them there forever. `--update-expectations`
records a change deliberately, and **merges** — re-measuring one directory
rewrites that directory's lines and leaves every other suite exactly as it was.

Both `--check` and `--gate` scope the comparison to the tests the run actually
ran, so one file serves the two-minute gate and a full-suite sweep.

## The ctest entries

Off by default, because the corpus is not in this repository and a test that
silently passes when the corpus is absent is worse than one that is not
registered.

```bash
cmake --preset default -DCTBROWSER_WPT=ON
ctest --preset default -L wpt          # both, ~2 minutes
ctest --preset default -LE wpt         # everything else
```

| test | what it does |
|---|---|
| `wpt-selftest` | the **negative proofs** — five fixtures whose outcomes are asserted. Seconds |
| `wpt-gate` | a fixed subset against `expectations.txt`, budgeted at two minutes |

The **full** run is a documented command and not a build step: it takes tens of
minutes and belongs to a person.

## The negative proofs

**A harness that has only ever been seen to report passes is not evidence of
anything.** `tools/wpt/run-wpt.py --selftest` runs five pages from
`tools/wpt/selftest/` and **asserts** each outcome and each subtest count —
never reads them off a table.

    fixture                              want            got   subtests
    must-pass.html                       PASS           PASS   2 {PASS: 2}      ok
    must-fail.html                       FAIL           FAIL   2 {FAIL:1,PASS:1} ok
    never-done.html                   TIMEOUT        TIMEOUT   1 {PASS: 1}      ok
    throws-on-load.html         HARNESS_ERROR  HARNESS_ERROR   0                ok
    no-harness.html             HARNESS_ERROR  HARNESS_ERROR   0                ok

**All five reported the outcome they must**, measured 2026-09-02 and again on
2026-09-03 after six DOM changes. Three of them did not, the first time they
were run, and each miss was a real engine defect — see the table above.

### And the gate itself, proved both ways

A gate nobody has watched fail is not a gate. Re-proved on 2026-09-03 against
the current expectations file, on the `--gate` subset:

| what was done to `expectations.txt` | gate says | exit |
|---|---|---:|
| nothing | `matches expectations.txt exactly` | 0 |
| deleted one SUBTEST line of a test that fails | `+ dom/events/Event-dispatch-click.html SUBTEST FAIL "basic with click()"` | 1 |
| added a `FAIL` line for a test that passes | `1 UNEXPECTED PASS(ES): - dom/events/Event-type.html FAIL` | 1 |
| restored | `matches expectations.txt exactly` | 0 |

**A line that names a test outside the `--gate` subset proves nothing**, which
the first attempt at this found: deleting
`dom/events/AddEventListenerOptions-passive.any.js FAIL` left the gate green,
because `--check` and `--gate` scope the comparison to the tests the run
actually ran. That is the documented behaviour and it is what lets one file
serve both the two-minute gate and a full-suite sweep — but it means a
falsification has to pick a line the gate will actually visit, or it silently
asserts nothing.


`tools/wpt/selftest/README.md` says what each one is for. The two that matter
most: `must-fail.html` contains one assertion that is false on purpose beside one
that passes, so a runner that called the whole file bad would be caught as
surely as one that called it good; and `no-harness.html` loads the report hook
*without* `testharness.js`, which is exactly the shape every test in the corpus
had before the document root existed — zero subtests, no error, and a naive
runner scores it green.

## Moving the pin

Deliberate, with a re-baseline attached. WPT adds and renames tests every day and
`expectations.txt` is keyed on test paths.

```bash
# 1. edit WPT_COMMIT in tools/wpt/fetch-wpt.sh
tools/wpt/fetch-wpt.sh
# 2. re-measure every suite in the baseline table, one at a time
tools/wpt/run-wpt.py --dir dom/nodes --update-expectations
# 3. update the table in this file, with the date and the real numbers
```

`run-wpt.py` refuses to run if the checkout's `HEAD` is not the commit
`fetch-wpt.sh` names. A table read against the wrong corpus is worse than no
table, so the two are compared rather than assumed.
