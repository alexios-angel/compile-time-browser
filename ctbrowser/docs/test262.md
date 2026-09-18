# test262 — the official ECMAScript conformance suite

**What it is for.** The three vendored corpora (p5.js, Phaser, Babylon.js) ask
whether a real library RUNS. They are the right question and they cannot answer
a second one: *which parts of the language are wrong*. A ratchet that reads
10/10 says a bundle got to the end, not that `Array.prototype.sort` is stable or
that `typeof` on an undeclared name is `"undefined"`. test262 is 53,580 files
that each assert one thing, with machine-readable metadata saying what should
happen — so for the first time there is a number for "how much of ECMAScript
does this engine implement", and a per-directory table saying where the holes
are.

It is not a gate on the whole suite and never will be: the engine is a subset by
design and most of these tests fail. What it gates is REGRESSION, over a fixed
subset, plus a self-test proving the harness itself is load-bearing.

---

## The decision: the official corpus, and a runner of our own

`https://github.com/Izhido/test262_harness_cpp` was evaluated first, as asked,
and turned down. It is honest work and MIT-licensed, but it is not for this
engine:

| | |
|---|---|
| **What it assumes** | Three specific embeddable engines — Duktape, TinyJS, and TinyJS's `42tiny_js` fork — each behind a `runtime_*.h/.cpp` pair. You pick one by RENAMING its file to `runtime.h`/`runtime.cpp`. There is no engine-agnostic interface to implement; the abstraction is a filename convention. |
| **How it builds** | Xcode 8.2+ project files and Visual Studio 2017+ project files. There is no CMake. This repository is CMake + Ninja and cross-compiles to Windows through llvm-mingw. |
| **How much it does** | 8 commits, no releases. It parses metadata and runs each test strict and non-strict. `negative` phase/type matching, `module`, `async`/`$DONE`, `includes` ordering, the `$262` host object and a feature skip-list are not documented and, as far as its README goes, not implemented. |
| **What we would keep** | The idea. Everything else — the engine binding, the build, the metadata handling, the classification — would be rewritten. |

Adopting it would mean writing a fourth `runtime.cpp` against an interface
designed for Duktape's `duk_peval_string`, adding a CMake build it does not
have, and then implementing the metadata handling it does not do — to end up
with the same runner in a shape that does not match anything else in this
repository. The cost of writing our own was one file of C++ (`tools/ct262`) and
one of Python (`tools/check/test262.py`), and both fit the conventions the rest
of the tooling already uses (`tools/check/*.py`, `ctbrowser_test()`, the devbox).

**So: the OFFICIAL corpus, at a pinned commit, never vendored, plus a two-part
runner of our own.** The corpus is the part that must not be reinvented; a
runner is 460 lines of C++ and 610 of Python.

---

## The pieces

| | |
|---|---|
| `tools/fetch-test262.sh` | shallow-fetches tc39/test262 at a **pinned commit** into `~/.cache/ctbrowser/test262` (override with `TEST262_DIR`) and VERIFIES the hash. Outside the source tree, never committed, not synced by `remote-build.sh`. |
| `ctbrowser/tools/ct262/ct262.cpp` | the HOST. One process, one test, one realm: `$262`, `print`, the harness preludes as separate programs, the strict transformation, the ES-module loader, and a one-line machine-readable failure report. |
| `tools/check/test262.py` | the RUNNER. Frontmatter, mode selection, the process cap, classification, the per-directory table, the gate, and its own self-test. |
| `tools/check/test262-baseline.sh` | THE WHOLE OF ECMAScript since 2026-09-12: `test/language`, `test/built-ins` and `test/annexB`, one after another with identical flags (intl402 is ECMA-402, staging is not the specification; neither runs). Before that, ten hand-picked areas - the rows of the tables below, still readable out of the built-ins JSON by directory. Sequential: four workers is the cap the whole devbox shares. |
| `ctbrowser/test/test262/expectations.txt` | what the gate's subset does today. Written by `--update-expectations`, never by hand. |

The pinned commit is **`771005236e88a909635104e03ba12559688c0172`** (tc39/test262
`main`, fetched 2026-09-02). It is in `tools/fetch-test262.sh`, which is the one
place that decides it. **A conformance number is only comparable to another
number against the same corpus**: bumping the pin means re-running the baseline
below and re-recording the expectations, in the same commit.

---

## Running it

```bash
tools/fetch-test262.sh                      # once per machine (~200 MB, shallow)
# and on the devbox, where the binary is:
ssh devbox 'cd ~/projects/<dir> && tools/fetch-test262.sh'

# ONE DIRECTORY, A TABLE. This is the everyday command.
tools/check/test262.py --dir test/language/statements/for-of
tools/check/test262.py --dir test/built-ins/Array --json /tmp/array.json

# The harness's own proof, and the regression gate.
tools/check/test262.py --self-test
tools/check/test262.py --gate

# What is skipped, and why. Nothing is skipped for being unimplemented.
tools/check/test262.py --list-skips
```

Every run is capped: **4 workers** (the devbox is shared and builds run on it),
a **10 s timeout per test**, and a **2 GB address-space limit per process**
(`RLIMIT_AS`), so a runaway test cannot take the box down. `--jobs` is clamped to
4 on purpose.

As a ctest, opt in — it needs a corpus that is not in the repository:

```bash
cmake --preset default -DCTBROWSER_TEST262=ON && ctest --preset default -L test262
```

That registers exactly two tests, `test262_selftest` and `test262_gate`, and
both are labelled `test262` so `ctest -LE test262` skips them. Without the
fetch, configure says so and registers nothing.

---

## The baseline — 2026-09-02

Measured on the devbox (GCC 13, Release, mimalloc), engine at the commit that
added this document, corpus at the pinned hash above. `ct262` runs each test the
way the metadata says: both strict and non-strict unless the file says
otherwise, so the run is about 1.8 processes per test.

| area | tests | pass | fail | timeout | crash | host | skip | pass rate of those run | wall |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `test/language` | 23,726 | 6,915 | 16,781 | 0 | 5 | 4 | 21 | **29.2%** | 117.1 s |
| `built-ins/Array` | 3,082 | 641 | 2,406 | 0 | 19 | 0 | 16 | **20.9%** | 42.9 s |
| `built-ins/Object` | 3,411 | 707 | 2,702 | 0 | 0 | 0 | 2 | **20.7%** | 12.8 s |
| `built-ins/Number` | 340 | 133 | 161 | 0 | 45 | 0 | 1 | **39.2%** | 92.4 s |
| `built-ins/Math` | 327 | 156 | 171 | 0 | 0 | 0 | 0 | **47.7%** | 1.3 s |
| `built-ins/String` | 1,223 | 557 | 655 | 2 | 6 | 0 | 3 | **45.7%** | 30.0 s |
| `built-ins/Boolean` | 51 | 17 | 33 | 0 | 0 | 0 | 1 | **34.0%** | 0.2 s |
| `built-ins/Function` | 509 | 145 | 351 | 0 | 0 | 0 | 13 | **29.2%** | 1.5 s |
| `built-ins/Error` | 93 | 12 | 76 | 0 | 0 | 0 | 5 | **13.6%** | 0.3 s |
| `built-ins/JSON` | 165 | 31 | 130 | 0 | 2 | 0 | 2 | **19.0%** | 4.6 s |
| **total** | **32,927** | **9,314** | **23,466** | **2** | **77** | **4** | **64** | **28.3%** | **5m 03s** |

**9,314 of the 32,863 tests that ran passed: 28.3%.** (Three later measurements
supersede this: 9,380 after the crash fixes below, 10,813 - 32.9% after property
attributes, and **11,210 - 34.1%** after the per-constructor error prototypes,
both measured 2026-09-03 and tabulated at the bottom of this file.) That is the number, and it is not a good one — nor should it look like one. The engine is a deliberate
subset (`docs/script.md` says which), and this is the first measurement of how
large the rest is.

`--dir` any row of that table to see it broken down one level further; the
per-directory table is the point of the tool.

The four statuses that are not PASS or FAIL are each worth naming, because they
are not "the engine got an answer wrong":

* **77 CRASH** — the engine died: 54 SIGSEGV and 23 SIGABRT. Diagnosed below.
* **2 TIMEOUT** — `String.prototype.repeat` with a `count` whose coercion is
  observable does not terminate within 10 s.
* **4 HOST** — `harness/deepEqual.js` does not PARSE here ("parse error: ) - at
  125:60"), so the four tests that include it could not be run at all. That is a
  parser gap presenting as a harness failure, and it is reported as HOST rather
  than FAIL precisely so it cannot be mistaken for a verdict on those tests.
* **64 SKIP** — 61 `cross-realm` and 3 `SharedArrayBuffer`, both host
  capabilities this runner cannot present. **No skip in this run is a language
  feature.**

**The verdicts hold when re-run alone.** Every TIMEOUT, CRASH and HOST row was
re-run serially on an idle box (83 rows): **78 kept their verdict**, and the 5
that moved went CRASH → TIMEOUT (`String.prototype.padStart`/`padEnd`/`repeat`
with a huge length — they abort under a memory cap and spin without one). None
became a PASS. A crash or a hang measured on a loaded box is worth exactly this
much scepticism, and it was cheaper to answer than to argue about.

**Read the pass rate as an upper bound, not a score.** Three things inflate it
and are named here rather than discovered later:

1. **There is no strict mode.** Neither half of the front end has one: `"use
   strict"` appears nowhere in `lib/Script/` and the string `strict` appears
   nowhere in ctjs's `vparse.hpp`. The directive is a string-literal expression
   statement that the engine evaluates and discards, so the strict run of a test
   behaves identically to the sloppy run. The runner still performs the suite's
   transformation and still runs both — being correct about the day the engine
   grows a strict mode costs one process — but today every one of the 678
   `onlyStrict` tests is being run in sloppy mode, and the 2,687 `noStrict`
   tests are the only ones whose mode is honestly respected.
2. **`negative: {phase: parse}` is 4,660 of the tests** and passing one only
   requires refusing the source. Where the refusal comes from the COMPILER
   rather than the parser — "tagged template literals are not in this VM subset"
   — `ct262` reports phase `refusal`, which never matches `SyntaxError`, so
   those score as failures. What it cannot separate is a ctjs PARSER gap from a
   genuine early error: both are "parse error", and both count as a pass.
3. **A skipped test is not a failure but it is not a pass either.** The skip
   count is in the table and the reasons are one command away (`--list-skips`):
   six features the host cannot present, one harness include, one flag, and the
   `intl402` paths the suite itself says to skip without ECMA-402. Only 64 tests
   in the whole baseline were skipped, and every one names a HOST capability.

### Top failure causes

Counted over all ten areas, with line numbers and quoted names normalised away
so that instances of one gap collapse into one row (the runner prints this
itself, per directory, under every table):

| count | cause |
|---:|---|
| 2,812 | `negative parse/SyntaxError: got runtime <string>` — **the engine PARSED source that must be a SyntaxError.** The test then reached `$DONOTEVALUATE()`, which throws a string. This is the single largest cause and it is one thing: no early-error checking |
| 2,670 | `Expected a undefined to be thrown but no exception was thrown at all` — the engine did not throw where the spec requires. ("a undefined" because the error constructors have no `.name`, which is its own gap) |
| 2,010 + 537 + 513 + 368 | `TypeError: X is undefined, not a function` — the method does not exist. 3,428 in total, and the biggest single bucket after early errors |
| 1,201 | `parse error: (` — valid syntax the ctjs parser rejects |
| 924 | `parse error: expression` — likewise |
| 777 | `Expected a Test262Error to be thrown but no exception was thrown at all` |
| 478 | `negative parse/SyntaxError: got refusal` — the COMPILER refused the construct instead of the parser rejecting it. Scored as a failure on purpose; see inflation (2) above |
| 352 + 290 + 240 + 240 | `Expected SameValue(«x», «y») to be true` — a wrong answer, which is what a conformance suite is for |
| 336 | `Expected a undefined but got a different error constructor with the same name` — `thrown.constructor !== TypeError`: the one-Error-prototype problem, from the test's side |
| 320 + 231 | `descriptor should not be enumerable` / `name should be an own property` — **property attributes.** The engine has no writable/enumerable/configurable, so `verifyProperty` fails wherever it is used |
| 226 | `vm: dynamic import() has no loader installed` — a HOST gap, not an engine one: `ct262` installs no `set_module_loader` for classic scripts. Named here with its cost; it is the first thing worth fixing in this harness |
| 198 | `asyncTest called without async flag` — the harness's own guard, from `asyncHelpers.js` |

---

## What `$262` supports

| property | status |
|---|---|
| `print` | real — stdout, flushed. This is how `async` tests report. |
| `$262.global` | **an ordinary object, and NOT the global object.** This engine's globals are a `string_flat_map` on the `script::context`, not properties of an object; `globalThis` exists only as a DOM binding the Shell installs. Tests that reach the global through `$262.global` or `fnGlobalObject()` FAIL rather than silently measuring a different object. |
| `$262.evalScript` | real — compiles a classic script and runs it in this realm, the same path `new Function` takes. |
| `$262.gc` | real — `context::collect()`, a precise mark-sweep over the live roots. |
| `$262.createRealm` | **throws.** A realm here is a `script::context`, which owns its heap and sweeps it in its destructor, so a value cannot cross between two without a use-after-free. Tests with the `cross-realm` feature are SKIPPED, named. |
| `$262.detachArrayBuffer` | **throws** — the engine has no detach operation. Tests including `detachArrayBuffer.js` are SKIPPED, named. |
| `$262.agent` | **an object whose every method throws.** One agent, one thread, no SharedArrayBuffer. Tests with `SharedArrayBuffer`, `Atomics`, `Atomics.pause` or `Atomics.waitAsync` are SKIPPED, named. |
| `$262.IsHTMLDDA` | absent. `IsHTMLDDA` tests are SKIPPED, named. |
| `$262.AbstractModuleSource` | absent, and not skipped: those tests fail. |

**Modules work.** `--module` runs `ct262`'s own two-pass loader — instantiate
the whole graph, create every export cell, then evaluate post-order — over the
filesystem, which is the shape `lib/Shell/browser/scripts.cpp` uses for a page. test262's
specifiers are all `./name.js` beside the importer, so resolution is
`std::filesystem` and nothing else. `_FIXTURE` files are dependencies, never
tests, exactly as INTERPRETING.md requires. Measured: `test/language/module-code`
reads **261 of 599**, and `test/language/export` 1 of 3 — the loader is real, and
what fails there is the engine's module semantics rather than the harness's
ability to present a graph.

### The two leniencies, stated

1. ~~**A negative test matches on `thrown.constructor.name` OR `thrown.name`.**~~
   **The reason for this one is GONE as of 2026-09-03** - `context::make_error`
   now puts an engine-raised error on the prototype its kind names, so
   `thrown.constructor` is `TypeError` for a TypeError. The leniency is still in
   the runner and still harmless (the two fields now agree wherever the engine
   raises), and tightening it is a runner change rather than an engine one:
   `tools/check/test262.py` and `ctbrowser/tools/ct262/ct262.cpp` both belong to
   whoever owns the harness. Recorded here so it is removed deliberately rather
   than inherited. The original reason: every error `throw_error` built was put
   on the one Error prototype, so an engine-raised TypeError had
   `name === "TypeError"` and `constructor === Error`.
2. **A parse failure is reported as `SyntaxError` without checking that the
   engine would have produced one**, because the engine has no way to say. See
   inflation (2) above; the `refusal` phase is what keeps it from being worse.

---

## Updating the expectations

```bash
tools/check/test262.py --gate                          # what changed
tools/check/test262.py --gate --update-expectations    # record it
```

The gate fails on a **regression** and on an **unexpected pass**, and the second
is the point: an expectations file that only ratchets one way rots into a list
of excuses nobody has re-measured, and the day a fix makes forty of them pass,
nothing says so. Re-recording is one command and the diff is the evidence for
the commit message.

Recorded 2026-09-02: **145 of the gate's 341 tests pass, 196 do not**, and both
directions were proved rather than assumed —

| planted | gate says | exit |
|---|---|---|
| one `FAIL` line changed to `CRASH` | `REGRESSED ...: expected CRASH, got FAIL` | **1** |
| a `FAIL` line added for a test that passes | `NOW PASSES ...: recorded as FAIL` | **1** |
| the file as recorded | `gate ok: 341 tests match` | 0 |

And through ctest, which is where it has to bite: with a `FAIL` line planted for
a passing test, `ctest -R test262_gate` reports **`54 - test262_gate (Failed)`**;
with the file restored, `100% tests passed`. `test262_selftest` and
`test262_gate` together take **11.8 seconds**, and the whole suite with them
registered is 118 tests green on the devbox (2026-09-02).

---

## The proof that the harness is load-bearing

`tools/check/test262.py --self-test` plants eleven answers and asserts every
classification. It is registered as `test262_selftest` and it exists because a
harness that reports PASS for everything is indistinguishable, from its output,
from a conforming engine.

| planted | must be | why it is in the list |
|---|---|---|
| `assert.sameValue(1, 1)` | PASS | the floor |
| `assert.sameValue(1, 2)` | **FAIL** | a false assertion has to be reported failing. This one was reported CRASH until the host was fixed — see below |
| `negative parse/SyntaxError` over `var a = ;` | PASS | the negative path works at all |
| `negative parse/**TypeError**` over `var a = ;` | **FAIL** | the `type` is checked, not just "something went wrong" |
| `negative parse/SyntaxError` over valid source | **FAIL** | a negative test that throws nothing is a failure |
| `negative runtime/Test262Error` over a real throw | PASS | the runtime path, on a constructor the engine gets right |
| `negative **runtime**/SyntaxError` over `var a = ;` | **FAIL** | the `phase` is checked: this fails at parse |
| `flags: [async]` + `$DONE()` | PASS | the async protocol |
| `flags: [async]` + nothing | **FAIL** | silence is not success — an engine whose promises never settle exits 0 having done nothing |
| `features: [cross-realm]` | SKIP | the skip list fires, with its reason |
| `while (true) {}` | TIMEOUT | the cap fires instead of hanging the run |

Measured 2026-09-02: **11/11 classified correctly.**

### What the harness found on its first run

**Every failing test crashed instead of failing** — exit 139, SIGSEGV — and the
cause was in `ct262`, not the engine: each harness file was compiled into a
`program` local to the loop that ran it, so `assert` and `Test262Error` were
globals holding closures whose `function_proto *` pointed into freed memory. A
test that PASSED never touched the freed pages; the first one to fail an
assertion segfaulted while building its own error message. `context::own_program`
is the fix, and it is why the self-test's second row exists.

This is worth keeping in mind for anything else that embeds the engine: **a
`program` must outlive every closure compiled from it**, and the failure mode is
silence followed by a crash somewhere unrelated.

---

## What the engine is missing, as the suite sees it

Ranked by how many tests each accounts for, from the run above.

1. **No early errors.** 2,812 tests assert that source text is a SyntaxError and
   this engine runs it. `var var var;` parses and executes here. Everything
   under `test/language/eval-code` (7 of 347 pass), `global-code` (5 of 42) and
   `directive-prologue` (23 of 62) is mostly this.
2. ~~**No property attributes.**~~ **CLOSED 2026-09-03** - see the section at the
   bottom of this file. There was no writable / enumerable / configurable, so
   `Object.getOwnPropertyDescriptor` could not answer and `verifyProperty` —
   used by 13,621 tests across the corpus — failed on contact. That is why
   `built-ins/Object` read 20.7% while `Math` read 47.7%; it now reads 50.0%
   and Math 60.9%.
3. **Missing methods, ~3,400 tests.** `TypeError: X is undefined, not a
   function`, spread across every built-in.
4. ~~**The error constructors are not real constructors.**~~ **CLOSED
   2026-09-03** - see the section at the bottom of this file. `TypeError.name`
   was undefined and `(new TypeError).constructor` was `Error`, so
   `assert.throws` could not match even when the engine threw the right kind
   (336 tests said so explicitly).
5. **Parser gaps.** 2,381 tests fail with `parse error:` on valid source, and
   `harness/deepEqual.js` is one of the casualties.
6. **No strict mode at all**, as above.

### The three crashes, diagnosed

**A native `toString` recurses until the C++ stack overflows (54 SIGSEGVs).**
`gdb -batch -ex run -ex bt` on
`test/built-ins/Number/prototype/toString/S15.7.4.2_A1_T01.js` shows 47,000
frames of exactly this cycle:

    context::to_string
      -> context::to_primitive_string
        -> context::invoke        (Number.prototype.toString, a native)
          -> context::to_string   ...

The native asks the context to stringify its own receiver; for an object
receiver `to_string` goes to `to_primitive_string`, which calls the object's
`toString` — the same native. There is no depth guard on that path, so it is a
segfault rather than a RangeError. 45 of the 54 are in `Number.prototype`.

**A dense array materialises every index (23 SIGABRTs).**
`test/built-ins/Array/15.4.5.1-5-1.js` is `a[4294967295] = "x"`, and
`array_object` is a `std::vector<value>`: the engine tries to allocate 4.29
billion slots and `std::bad_alloc` takes the process. **This is what the runner's
2 GB `RLIMIT_AS` is for** — without it that allocation is 34 GB of a shared
box's memory, and one test would take the machine down rather than itself.

**`String.prototype.padStart`/`padEnd`/`repeat` do not terminate** for a length
that is huge or whose coercion is observable: 2 TIMEOUTs, plus the 5 rows that
are CRASH under memory pressure and TIMEOUT without it.

None of these three is in `docs/script.md`, and all three took one run to find.
That is the argument for the corpus.

### ...and what they measure now - 2026-09-02, same day, same corpus

All three are fixed. The three areas were re-run with the same flags (4 workers,
10 s, 2 GB) before and after, on the devbox:

| area | tests | pass | fail | crash | timeout |
|---|---:|---:|---:|---:|---:|
| `built-ins/Number` | 340 | 133 → **180** | 161 → 159 | 45 → **0** | 0 → 0 |
| `built-ins/String` | 1,223 | 558 → **566** | 654 → 654 | 6 → **0** | 2 → **0** |
| `built-ins/Array` | 3,082 | 641 → **651** | 2,406 → 2,414 | 19 → **1** | 0 → 0 |
| `test/language` | 23,726 | 6,915 → 6,915 | 16,781 → 16,782 | 5 → **4** | 0 → 0 |

**77 crashes → 7, and 2 timeouts → 0.** The seven left are 4 in `test/language`,
2 in `built-ins/JSON` and one in `built-ins/Array`
(`prototype/at/coerced-index-resize.js`, a SIGSEGV in resizable ArrayBuffers);
none of them is any of the three diagnosed above. `built-ins/Object`, `Math`,
`Boolean`, `Function` and `Error` are unchanged to the test, and **no test that
passed before fails now**: for the three areas above that was checked PER TEST
against the before run rather than inferred from the totals, and for
`test/language` the pass count is identical to the row. The ten-area total is
**9,380 passing of 32,863 run, 28.5%**, up from 9,315.

Two corrections to the numbers above, both measured rather than argued:

* the table says `built-ins/String` passed **557**; re-measured on the unmodified
  binary it is **558**, so the ten-area total before the fixes is 9,315 and the
  rate 28.34% rather than 28.3%.
* "23 SIGABRTs from dense arrays" is the count across ALL ten areas, and they
  are not all arrays. `built-ins/Array` holds 19 crashes, of which **18** are
  the SIGABRT and one is a SIGSEGV that is a different defect
  (`prototype/at/coerced-index-resize.js`); the other **5** SIGABRTs are in
  `built-ins/String`, and they are the pad/repeat allocation, not an array.

The engine's own regression net for all three is
`unittests/js/crash_guards.cpp`, not this suite: these crashes are the kind that
kill a process rather than print a wrong answer, and a net for them has to run
with `ctest` rather than behind a 273 MB opt-in corpus. Each of the three was
proved load-bearing by reverting it and watching that file go red - exit 139 for
the recursion, 134 for both the array and the pad.

## Property attributes - 2026-09-03

**The second measurement, and the largest single move so far: 9,380 -> 10,813,
28.5% -> 32.9% of the ten areas.** `[[Writable]]`, `[[Enumerable]]`,
`[[Configurable]]` and `[[Extensible]]` are gap 2 in the list above, and closing
it moved every area in the table rather than one, which is what "it gates
thousands of tests across every built-in area" meant.

Same corpus (pinned hash above), same devbox, same flags (4 workers, 10 s
timeout, 2 GB address-space cap). Both runs are the ten baseline areas.

| area | tests | pass before | pass after | delta | PASS -> FAIL |
|---|---:|---:|---:|---:|---:|
| `test/language` | 23,726 | 6,915 | **7,202** | +287 | 6 |
| `built-ins/Array` | 3,082 | 651 | **689** | +38 | 0 |
| `built-ins/Object` | 3,411 | 707 | **1,706** | +999 | 9 |
| `built-ins/Number` | 340 | 180 | **205** | +25 | 0 |
| `built-ins/Math` | 327 | 156 | **199** | +43 | 0 |
| `built-ins/String` | 1,223 | 566 | **584** | +18 | 0 |
| `built-ins/Boolean` | 51 | 17 | **20** | +3 | 0 |
| `built-ins/Function` | 509 | 145 | **155** | +10 | 0 |
| `built-ins/Error` | 93 | 12 | **14** | +2 | 0 |
| `built-ins/JSON` | 165 | 31 | **39** | +8 | 0 |
| **total** | **32,927** | **9,380** | **10,813** | **+1,433** | **15** |

CRASH stayed at 7, TIMEOUT at 0, HOST at 4 and SKIP at 64. The comparison is
**per test, not per total**: the runner's `--tsv` was captured before and after
and diffed row by row, because a total that rises can hide a test that fell.
1,448 tests went FAIL -> PASS and 15 went PASS -> FAIL.

### What was implemented

* Three attribute bits per property, in a vector PARALLEL to each property
  table and grown lazily, so an object whose properties all have the default
  attributes carries no extra memory and the property-store fast path is one
  hash lookup rather than the two it was. All four tables have them:
  `object_object`, `closure_object` (a class's statics), `native_object` (a
  built-in's statics), and - as three bools rather than three bits per element -
  `array_object`.
* `context::own_property` / `define_own_property` / `delete_own_property` /
  `is_extensible` / `prevent_extensions`: one [[GetOwnProperty]] and one
  [[DefineOwnProperty]] over all four tables plus the synthesised slots (an
  array's `length` and indices, a string's `length` and characters, a function's
  `name`, `length` and `prototype`, a native's `name`). `defineProperty`,
  `getOwnPropertyDescriptor(s)`, `getOwnPropertyNames`, `hasOwnProperty`,
  `hasOwn`, `propertyIsEnumerable`, `freeze`, `seal`, `preventExtensions`,
  `isFrozen`, `isSealed`, `isExtensible` and five `Reflect` methods are now that
  one answer rather than twelve separate ones against `object_object` alone.
* 10.1.6.3 ValidateAndApplyPropertyDescriptor, with the absent-field rules that
  make `defineProperty`'s defaults the opposite of a literal's.
* Enumerability honoured by `Object.keys`/`values`/`entries`, `for-in`,
  `JSON.stringify`, `Object.assign` and object spread - with symbol keys kept in
  the last two, which is the one place OwnPropertyKeys and EnumerableOwnProperties
  differ and which cost four tests before it was noticed.
* Writability and extensibility honoured by assignment, including an INHERITED
  non-writable data property, which is the half of 10.1.9 that surprises people.
* Enumeration order: integer-index keys first and ascending (6.1.7.1), behind a
  flag so an object with no index-shaped key takes the straight-line walk it did.
* Clause 17 applied to the standard library: every built-in method is
  `{ true, false, true }`, every built-in value property (`Math.PI`,
  `Number.MAX_VALUE`, `X.prototype`, the well-known symbols) is
  `{ false, false, false }`, and `X.prototype.constructor` and `F.prototype` are
  no longer enumerable. `Object.keys(Math)` was 44 entries.

Three defects were found on the way and are fixed here because the measurement
would otherwise be reporting them as progress:

* **`in` answered about own data properties only.** `'toString' in {}` was
  false, so was `'length' in [1]`, and so was any accessor - and
  ToPropertyDescriptor is specified in terms of HasProperty, so a descriptor
  object that INHERITED its `writable` described nothing at all. `has_property`
  is now `own_property` walked up the explicit chain, then the implicit tables,
  then a function's static chain.
* **A function's prototype chain stopped at `Function.prototype`.**
  `f.hasOwnProperty` was undefined on every function, which is the gap numbers,
  booleans and strings had until they were fixed and functions were left out of.
* **A labelled `break` could cross a function boundary in the COMPILER.**
  `compile_function_body` (now `lib/Script/compile/statements/functions.cpp`) kept
  one `loops_` stack for the whole
  compilation, and its own comment said the invariant was worth checking rather
  than inheriting. It was checked and it was wrong: `L: do { (function(){ break
  L; })(); } while(0)` pushed a jump site onto the enclosing function's break
  list, which was then patched with the OUTER proto's offsets into the INNER
  proto's code. test262's `language/statements/break/S12.8_A6.js` landed on a
  `load_string` with an out-of-range slot and allocated until `std::bad_alloc`
  killed the process. `loops_` is now swapped at a function boundary, the same
  fix the optional chain and the open `finally` beside it already had.
  **This is the only change outside `lib/Script/vm`, `lib/Script/builtins` and
  the headers**, and it is named here because a strict-mode agent will be in
  that file next.

### What was deliberately NOT implemented

* **Strict mode, still.** A write to a non-writable property, a `delete` of a
  non-configurable one and an addition to a non-extensible object are each
  SILENT here. That is correct sloppy behaviour and it is what
  `unittests/js/property_attributes.cpp` asserts; the three `return`s where a
  strict mode throws instead set `store_rejected_` in
  `lib/Script/vm/objects/store.cpp` (strict `delete` is still `TODO(strict)`
  in `descriptors.cpp`).
* **Per-element attributes on an array.** `array_object` holds its elements in a
  `std::vector<value>` with nowhere to put three bits each, so it carries
  `extensible`, `elements_writable` and `elements_configurable` instead - enough
  for `freeze`, `seal` and their queries, and not enough for
  `Object.defineProperty(a, 0, {writable: false})`, which stores the value and
  drops the attributes.
* **An accessor on an array or on a native.** Neither has an accessor table.
  `Object.defineProperty(arr, "0", {get() {...}})` is a no-op that answers true,
  which is what it has always been; answering FALSE instead was measured and
  cost 6 tests that had been passing, so the previous behaviour is kept and
  named rather than changed.
* **A native's `length`.** A `native_fn` takes a span and its declared arity is
  recorded nowhere, so `Object.getOwnPropertyDescriptor(Object.keys, 'length')`
  is `undefined` rather than wrong.
* **Class methods are still enumerable.** `class C { m(){} }` puts `m` on the
  prototype through the ordinary property-store opcode, so `Object.keys(C.prototype)`
  reports it where the specification says it must not. Fixing it needs a
  `is_method` flag through the compiler, which is a strict-mode-shaped change to
  `lib/Script/compile/`.

### The 15 that went PASS -> FAIL, each diagnosed

None is an attribute answering wrongly; every one is a DIFFERENT gap that the
new behaviour now reaches. They are listed rather than summarised because "the
total went up" is not an answer to "did anything break".

| count | tests | why |
|---:|---|---|
| 9 | `eval-code/{direct,indirect}/non-definable-global-var`, `global-code/unscopables-ignored`, `Object/isFrozen/15.2.3.12-3-1`, `Object/defineProperties/15.2.3.7-5-b-{188,190,248}`, `Object/keys/15.2.3.14-5-{15,16}` | **there is no global object and no String wrapper.** `this` at top level is `undefined` here, so `Object.defineProperty(this, ...)` is now the TypeError the spec requires and `Object.isExtensible(this)` is now the `false` the spec requires - both of which these tests read as "the global object is non-extensible" and proceed on. Each passed before by calling a method that did nothing. |
| 2 | `class/elements/private-method-is-not-a-own-property` (both forms) | **a private method is an ordinary property named `"#m"`.** `"#m" in this` is now true because `in` walks the prototype chain, which it must. The bug is the private-field model, not the operator. |
| 1 | `Object/isFrozen/15.2.3.12-3-18` | `RegExp.prototype` is not exposed, so `Object.isFrozen(undefined)` is `true` - correct for the argument it was given. |
| 1 | `Object/prototype/toString/symbol-tag-non-str-bigint` | `BigInt.prototype[Symbol.toStringTag]` does not exist, so the test's first `defineProperty` CREATES it non-configurable and its second is correctly refused. |
| 1 | `object/method-definition/name-prototype-prop` | a method gets a synthesised `prototype` descriptor; see "class methods" above. |
| 1 | `Object/defineProperties/15.2.3.7-5-a-16` | an Error instance's `stack` is still enumerable. Fixed by the error-prototype work below. |

`unittests/js/property_attributes.cpp` is the engine's own regression net for
all of this - 90 assertions, in the suite, no corpus needed.

---

## Per-constructor error prototypes - 2026-09-03

**10,813 -> 11,210 of the ten areas, 32.9% -> 34.1%, and NOTHING that passed
before fails now.** Gap 4 in the list above: `thrown.constructor !== TypeError`,
336 tests reporting it by name.

### What was actually wrong, which is not what the brief expected

The six NativeError constructors and their prototypes ALREADY EXISTED and were
already chained to `Error.prototype` - `install_errors` has built them since
p5.js needed `throw new TypeError` to work. And every internal throw site was
already raising the right KIND: all 19 `throw_error` calls in `lib/Script/` were
audited against the specification and **not one of them was wrong**. The brief
expected that audit to be the work; it was half an hour and it changed nothing.

The whole gap was one function. `context::make_error` put every error the ENGINE
raised on `prototype(proto_kind::error)` - Error's - because `proto_kind` is a
fixed enum over the value kinds property lookup falls back to and there was no
way to ask it for "the RangeError prototype". So a VM-raised TypeError had the
right `name` (written as an own property) and `Error` as its constructor.

### What was implemented

* A small keyed list of error prototypes on the context, `register_error_prototype`
  / `error_prototype`, rather than seven more `proto_kind` enumerators - that
  array is indexed by every property read on a primitive and does not need to grow.
* `make_error` looks the kind up in it, and **stops writing an own `name`**:
  20.5.6.5 puts `name` on the prototype, and an own one made
  `e.hasOwnProperty('name')` true and put it in `Object.keys(e)`.
* A kind with no constructor falls back to `Error.prototype` PLUS an own `name`.
  `structuredClone` raises `DataCloneError`, which is a DOMException name rather
  than an ECMAScript one; the fallback keeps the name right and
  `e instanceof Error` true, and `unittests/js/error_types.cpp` asserts it.
* Each `NativeError.prototype` gets its own `name` AND its own `message` (`""`,
  20.5.6.3), both `{ true, false, true }`; each constructor gets `name`,
  `length: 1` and a non-writable, non-configurable `prototype`.
* `native_object` gains a `proto_link`, so `Object.getPrototypeOf(TypeError)` is
  `Error` (20.5.6.2) rather than `Function.prototype`, statics inherit through
  it, and the collector marks it.
* An instance's `message` and `stack` are `{ true, false, true }` and an ABSENT
  argument installs no `message` at all, so `JSON.stringify(new TypeError('m'))`
  is `{}` and `new TypeError().hasOwnProperty('message')` is false.
* A built-in function's `name` is answered from the C++ object rather than
  installed on 400 natives, which is what made `assert.throws`'s own message read
  "Expected a undefined to be thrown" 2,670 times.

### Measured, per test

| area | tests | pass before | pass after | delta | PASS -> FAIL |
|---|---:|---:|---:|---:|---:|
| `test/language` | 23,726 | 7,202 | **7,296** | +94 | 0 |
| `built-ins/Array` | 3,082 | 689 | **741** | +52 | 0 |
| `built-ins/Object` | 3,411 | 1,706 | **1,913** | +207 | 0 |
| `built-ins/Number` | 340 | 205 | **213** | +8 | 0 |
| `built-ins/Math` | 327 | 199 | 199 | 0 | 0 |
| `built-ins/String` | 1,223 | 584 | **596** | +12 | 0 |
| `built-ins/Boolean` | 51 | 20 | 20 | 0 | 0 |
| `built-ins/Function` | 509 | 155 | **164** | +9 | 0 |
| `built-ins/Error` | 93 | 14 | **27** | +13 | 0 |
| `built-ins/JSON` | 165 | 39 | **41** | +2 | 0 |
| **total** | **32,927** | **10,813** | **11,210** | **+397** | **0** |

CRASH 7, TIMEOUT 0, HOST 4, SKIP 64, all unchanged. The gate matched its
expectations file exactly, so nothing was re-recorded for this change.

It also fixed four of the fifteen regressions the property-attribute work left:
`Object/defineProperties/15.2.3.7-5-a-{13,14,16}` and `-5-b-245`, all of which
were an engine-made object carrying an enumerable own property that was not a
descriptor. **Eleven remain**, and every one is the same list of gaps as before -
no global object (5), the private-field model (2), `RegExp.prototype` not
exposed (1), `BigInt.prototype[Symbol.toStringTag]` absent (1), a method's
synthesised `prototype` (1), a String wrapper (1).

### What is deliberately NOT done

* **`AggregateError`, `Error.prototype.stack` as an accessor, and `cause`.** The
  seven constructors here are the seven the specification names as `Error` plus
  the six NativeErrors; `AggregateError` is a separate clause and needs
  `errors`.
* **`ct262`'s leniency is not tightened.** It matches on `thrown.name` OR
  `thrown.constructor.name` and could now require the constructor; both halves
  of the harness belong to whoever owns it. See the leniency note above.
* **The 512-frame `invoke` ceiling still ends the run rather than throwing.**
  `try { f() } catch (e)` around an infinite recursion sees nothing, so there is
  no constructor to be right about. The CONVERSION ceiling does throw a
  catchable RangeError and `unittests/js/crash_guards.cpp` covers that one.
* **Reading a property of `null` is still `undefined`, not a TypeError.** A
  different gap - the engine has no nullish guard on member access - and
  `unittests/js/error_types.cpp` says so where it would otherwise have used
  `null.x` as its example of an engine-raised TypeError.

---

## Array.prototype is generic, and a built-in knows its arity — 2026-09-07

**11,210 -> 12,745 of the ten areas, 34.1% -> 38.8%, measured on the devbox
against the same pinned corpus.** Two seams, both counted over the corpus before
they were touched rather than guessed at.

### The receiver

Every `Array.prototype` method is specified GENERIC: it reads `this` through
[[Get]] and `length` through ToLength, whatever `this` is. Every one of them here
opened with `this_array()`, which answers nullptr for anything that is not a real
Array and made the method return a default. `Array.prototype.some.call(obj, f)`
was `false` for every object literal in the corpus.

Counted over `~/.cache/ctbrowser/test262` at the pin: **583 files** call one of
the nine iteration methods on a plain object literal — `reduce` 91, `reduceRight`
89, `filter` 69, `map` 68, `some` 60, `every` 60, `forEach` 59, `lastIndexOf` 44,
`indexOf` 43. And **598 more** could not run a line because the method did not
exist at all: `reduceRight` 260, `lastIndexOf` 198, `copyWithin` 39, `toSpliced`
30, `with` 21, `toSorted` 21, `toReversed` 17, `toLocaleString` 12.

### The descriptors

A built-in function's `length` was absent everywhere and its `name` was
synthesised from the C++ object rather than being an own property, so
`verifyProperty` — which test262 uses on every one — failed on both: **157 files**
for `length`, **178** for `name`.

The `name` half had a second half. `context::own_property` synthesises the name
when the table has none, and the synthesised slot has no memory: after `delete
f.name` it uncovered and `hasOwnProperty("name")` stayed true, which is exactly
what `verifyProperty`'s `isConfigurable()` asks. `native_object::name_erased`
closes it — the fallback still answers for the 400 natives `define_native` makes
with no own entry, and stops answering the moment one is deleted.

### Measured, per area

| area | tests | pass 2026-09-03 | pass 2026-09-07 | delta |
|---|---:|---:|---:|---:|
| `test/language` | 23,726 | 7,296 | **7,310** | +14 |
| `built-ins/Array` | 3,082 | 741 | **1,905** | **+1,164** |
| `built-ins/Object` | 3,411 | 1,913 | **1,997** | +84 |
| `built-ins/Number` | 340 | 213 | **239** | +26 |
| `built-ins/Math` | 327 | 199 | **271** | +72 |
| `built-ins/String` | 1,223 | 596 | **730** | +134 |
| `built-ins/Boolean` | 51 | 20 | **25** | +5 |
| `built-ins/Function` | 509 | 164 | **193** | +29 |
| `built-ins/Error` | 93 | 27 | **30** | +3 |
| `built-ins/JSON` | 165 | 41 | **45** | +4 |
| **total** | **32,927** | **11,210** | **12,745** | **+1,535** |

`built-ins/Array` is 62.1% of the tests that ran, from 24.2%. `built-ins/Math`
moving +72 with nothing written for it is the descriptors: `Math.max.length` is
2 and every `verifyProperty` in that directory was failing on the arity.

CRASH went 7 -> 10 and TIMEOUT 0 -> 1, both in `built-ins/Array` and
`built-ins/JSON` and neither diagnosed. **That is a regression in the one column
this suite has never had one in, and it is recorded here rather than left for
someone to find.**

### What else changed, each its own correctness fix

`sort` puts `undefined` last and never hands one to the comparator;
`sort`/`toSorted` refuse a comparator that is neither undefined nor callable;
`includes` uses SameValueZero so `[NaN].includes(NaN)` is true;
`indexOf`/`includes` honour `fromIndex`; `join(undefined)` is `"1,2"`; `at`
coerces through ToIntegerOrInfinity so an object's `valueOf` is seen; a bound
function's name is `"bound f"` and its length is the target's less the bound
arguments; `String.raw` exists; `String.fromCodePoint` throws RangeError instead
of wrapping through ToUint32; `Number.parseInt`/`parseFloat` exist and are the
SAME function objects as the globals; and the `Number` predicates and the
`String` statics are non-enumerable, so `Object.keys(Number)` is empty.

`unittests/js/array_generics.cpp` is the regression net — 96 assertions, in the
suite, no corpus needed — and `property_attributes.cpp` gained 32 more.

### What is deliberately NOT done

* **`push`/`pop`/`shift`/`unshift`/`splice`/`concat`/`reverse`/`flat`/`flatMap`
  are still array-only** — about 330 files. They MUTATE, so a generic version has
  to do Set/Delete in the right order with a `length` write-back; that is a
  second pass, not a mechanical one.
* **A real Array is iterated to `items.size()`, not to its `length` property.**
  `array_object` records an index it refused to materialise and raises `length`
  over it, so `a[4294967295] = 'x'` makes `length` four billion and iterating to
  it turns an answer into a TIMEOUT. The deviation is `array_object`'s own and it
  was kept rather than widened.
* **`Symbol.species`, ArraySpeciesCreate, `Symbol.isConcatSpreadable` and
  `Array.prototype[Symbol.unscopables]`** — the copying methods build a plain
  Array whatever they were called on.
* **`String.prototype.isWellFormed`/`toWellFormed`**, 16 files: strings here are
  UTF-8 bytes, so a lone surrogate cannot be represented and both would be the
  identity. Adding them buys the descriptor tests and gives wrong answers to the
  rest. `normalize` is already the identity for the same reason and a second one
  was not wanted.
* **`Array.fromAsync`** — 95 files, 0 passing, and it needs async iteration.

### The largest thing left, unchanged

`negative parse/SyntaxError: got runtime` is still the single biggest cause in
the corpus and still 2,812 tests: **the engine parses source that must be an
early error.** It is the ctjs parser and the compiler rather than the standard
library, and nothing in this session touched it.

---

`docs/script.md` is the engine's own account of what it implements and what it
refuses by name; this file is the independent measurement of the same thing.
Where they disagree, this one was measured.

## Measured at `0e5cfbef` — 2026-09-12, before the day's runtime work

The 09-10 sessions moved the VM (block scoping, `__proto__`, proxy traps,
`for-in` over proxies, label chains) without re-running this suite. Same
instrument (`tools/check/test262-baseline.sh`, 4 workers, 10 s, 2 GB), corpus
at the pinned commit, engine at `0e5cfbef` on `ctbrowser-wpt`:

| area | tests | pass 2026-09-07 | pass 2026-09-12 | delta | fail | crash |
|---|---:|---:|---:|---:|---:|---:|
| `test/language` | 23,726 | 7,310 | **8,754** | +1,444 | 14,943 | 4 (+4 host) |
| `built-ins/Array` | 3,082 | 1,905 | **1,983** | +78 | 1,078 | 4 (+1 timeout) |
| `built-ins/Object` | 3,411 | 1,997 | **2,459** | +462 | 950 | 0 |
| `built-ins/Number` | 340 | 239 | **260** | +21 | 79 | 0 |
| `built-ins/Math` | 327 | 271 | **274** | +3 | 53 | 0 |
| `built-ins/String` | 1,223 | 730 | **860** | +130 | 360 | 0 |
| `built-ins/Boolean` | 51 | 25 | **27** | +2 | 23 | 0 |
| `built-ins/Function` | 509 | 193 | **231** | +38 | 265 | 0 |
| `built-ins/Error` | 93 | 30 | **35** | +5 | 53 | 0 |
| `built-ins/JSON` | 165 | 45 | **98** | +53 | 65 | 0 |
| **total** | **32,927** | **12,745** | **14,981** | **+2,236** | | |

**14,981 of the 32,927 (45.5%).** `test/language` is where the work is; its top
causes, counted from the JSON and named by what they are:

| count | cause | what it is |
|---:|---|---|
| 2,225 | `X is undefined, not a function` | 1,501 of them `then`: **async generators** ran as plain generators, so `.next()` answered a record, not a promise; 571 `eval`; 242 `with` |
| 1,801 | negative parse: got runtime | early errors the checker does not know (class 491, regexp literals 181, dynamic-import 86, object literals 86, strings 35) |
| 1,201 | `parse error: (` | 1,142 of them `for await` — not parsed |
| 1,211 | Expected a ReferenceError | reading an undeclared name is `undefined` here, not a throw (`get_global`'s row says may_throw 0 — an ABI change) |
| 924 | `parse error: expression` | 861 in class tests: `\u{6F}`-escaped identifiers and private names |
| 959 | Expected a TypeError | strict-mode writes to non-writable properties (no strict mode), destructuring `null`, private-name access on the wrong object |
| 466 | descriptor should not be enumerable | class methods were enumerable |

## Measured at `b570bd29` — 2026-09-12, after the day's runtime work

Same instrument, same corpus, engine at `b570bd29` (browser gate 186/186):

| area | tests | pass at `0e5cfbef` | pass at `b570bd29` | delta | fail | crash / host |
|---|---:|---:|---:|---:|---:|---:|
| `test/language` | 23,726 | 8,754 | **12,624** | +3,870 | 11,075 | 0 / 6 |
| `built-ins/Array` | 3,082 | 1,983 | **2,161** | +178 | 886 | 6 / 11 |
| `built-ins/Object` | 3,411 | 2,459 | **2,519** | +60 | 890 | 0 |
| `built-ins/Number` | 340 | 260 | **261** | +1 | 78 | 0 |
| `built-ins/Math` | 327 | 274 | **275** | +1 | 52 | 0 |
| `built-ins/String` | 1,223 | 860 | **893** | +33 | 327 | 0 |
| `built-ins/Boolean` | 51 | 27 | **28** | +1 | 22 | 0 |
| `built-ins/Function` | 509 | 231 | **286** | +55 | 210 | 0 |
| `built-ins/Error` | 93 | 35 | **31** | -4 | 57 | 0 |
| `built-ins/JSON` | 165 | 98 | **101** | +3 | 62 | 0 |
| **total** | **32,927** | **14,981** | **19,179** | **+4,198** | | |

**19,179 of the 32,927 (58.2%), from 45.5% in the morning** (19,190 = 58.3% at
`d27d8f36` three hours later, with the `fromAsync` crashes fixed). `test/language`
carries it: the async generators (+~1,500 files by themselves), `for await`
(+~1,000), the class member attributes (+~450), the escaped identifiers
(+~800 class files), `eval`, the null/undefined TypeError. The 6 `host` rows in
`test/language` are `harness/deepEqual.js` (a parse error at 125:60 — the
harness uses syntax the parser lacks) and `harness/testTypedArray.js`, and
the 11 in `Array` are the same `testTypedArray.js` prelude, which now
reaches a property of an undefined constructor (`BigInt64Array`) and throws
where it used to read undefined. **`built-ins/Error` -4**: the four
`prototype/stack/setter-*` files read `.set` off a descriptor the engine does
not have (no `Error.prototype.stack` accessor) — a silent undefined until
today, an honest TypeError now. The 6 crashes in `Array` are two
`fromAsync` files running the box to `std::bad_alloc` under the 2 GB cap —
fixed after this run (`30dcd8e0`: a constructor `this` with an unsettable
element, and an array-like of length 2^53) — and the four `slice` files that
were crashing before.

## What moved on 2026-09-12

Landed on `ctbrowser-wpt`, each named so the delta above can be read:

- **async functions reject** instead of throwing synchronously or faulting
  after an `await` (the compiler's fence; `05ece7bc..ad116e42`).
- **async generators** — `%AsyncGeneratorPrototype%`, the request queue,
  `yield`/`return` awaiting their operands.
- **`for await (x of y)`** — parsed (ctjs `8980c01`), lowered to the async
  iteration protocol.
- **class members are non-enumerable**; static fields initialise after the
  methods; `static x;` exists.
- **a property of `null`/`undefined` throws** TypeError (was undefined).
- **`eval`**, as an indirect eval with a completion value.
- **arrays have named own properties**; `for-in` over a function sees its
  enumerable statics.
- **identifiers with unicode escapes** decode (ctjs `8eb3375`).
- **a throw crossing a native is thrown once**, at the native's call site;
  a throwing promise reaction rejects the next promise.
- **a captured parameter is boxed before its default runs** — this one was a
  wrong answer, not a missing feature (`f(cls, p = cls.name)` read undefined).
- collection inside a turn (the reflection TIMEOUTs), with a native's own
  allocations pinned and a native's C++-held arguments rooted.

## Measured at `15f47064` — 2026-09-12, evening

Same instrument, same corpus, engine at `15f47064` on `ctbrowser-wpt`
(browser gate 540/540 at that commit; `tools/check/test262-baseline.sh` on the
devbox, 4 workers, 10 s timeout, 2 GB cap):

| area | tests | pass at `d27d8f36` | pass at `15f47064` | delta | fail | crash / host |
|---|---:|---:|---:|---:|---:|---:|
| `test/language` | 23,726 | 12,624 | **17,226** | +4,602 | 6,446 | 26 / 6 |
| `built-ins/Array` | 3,082 | 2,167 | **2,624** | +457 | 426 | 4 / 11 |
| `built-ins/Object` | 3,411 | 2,519 | **3,128** | +609 | 281 | 0 / 0 |
| `built-ins/Number` | 340 | 261 | **273** | +12 | 66 | 0 / 0 |
| `built-ins/Math` | 327 | 275 | **279** | +4 | 48 | 0 / 0 |
| `built-ins/String` | 1,223 | 893 | **975** | +82 | 245 | 0 / 0 |
| `built-ins/Boolean` | 51 | 28 | **42** | +14 | 8 | 0 / 0 |
| `built-ins/Function` | 509 | 291 | **331** | +40 | 165 | 0 / 0 |
| `built-ins/Error` | 93 | 31 | **72** | +41 | 16 | 0 / 0 |
| `built-ins/JSON` | 165 | 101 | **101** | +0 | 62 | 0 / 0 |
| **total** | **32,927** | **19,190** | **25,051** | **+5,861** | | |

**25,051 of 32,927 (76.1%), from 58.3% at `d27d8f36`.** Not one PASS became
a FAIL. `test/language` +4,602 is the compiler and VM work of the evening
(the list below) plus agent B's built-ins round one; the built-ins columns
are agent B (`d27d8f36..502c691f`: ArraySpeciesCreate, IsConstructor, the
`Object` descriptors) — agent G's round two (+650 measured on its own
branch: Date, encode/decodeURI, `@@toPrimitive`, JSON.rawJSON) merged AFTER
this run and is in the next row.

**The 30 crashes are all diagnosed and fixed after this run**, which is why
they are named rather than left as a number: 13 `dynamic-import` files are
`import('')` resolving to the test's directory, which ct262's loader opened
and then aborted on (`9dd67566`); 13 `for-of`/`derived-class-return-override`
files are a GC hole — `iterable_values` drained a page's `[Symbol.iterator]`
into an array reachable from nothing while `next()` ran, and a collection
under the call freed it (`e3344344`); the 4 `slice` files push 2^32 elements
until the cap kills the process (`e5772310`, then agent G's ArraySpeciesCreate
which throws the RangeError first).

**What moved, by name** (`d27d8f36..15f47064`, `ctbrowser-wpt`):

- **an unresolvable name is the ReferenceError** — `get_global` throws; the
  `typeof x` case is silenced by a run-loop peek at the following opcode, and
  the AOT bridge grew `ct_aot_global_get_soft` for the same case.
- **strict mode, the runtime half**: a rejected write throws in strict code,
  an assignment to an undeclared name throws (the probe runs before the RHS;
  the three `toFixed` files that want RHS-then-ReferenceError are the cost).
- **the early errors of strict code**: `eval`/`arguments` as bindings, the
  reserved words, duplicate simple parameters, `delete x`, legacy octal.
- **every `await` in a function takes a job**; a script's top level keeps the
  synchronous read and drains the queue for a pending promise.
- **`yield*`** (delegation through `__ctbrowser_delegate_*`, `.throw`/`.return`
  forwarded), **generator `.return()` running `finally`** (the `{@#return}`
  marker), the eager generator prologue.
- **NamedEvaluation** (`function_proto::inferred_name`, image format 5), the
  `name` of every anonymous function and class expression.
- **array destructuring through the iterator protocol** (`__ctbrowser_iter_*`,
  IteratorClose on the normal exit), empty and rest-first object patterns
  throw on `null`/`undefined`, computed accessors, computed class methods.
- **array literal elisions are holes**, `Object.keys([,1])` is `["1"]`.
- **`var` hoists across the whole script** (`program::hoisted_vars`), for-in
  walks the prototype chain, `iterable_values` honours `[Symbol.iterator]`.
- **private names are one name** (`@#x`), never a property key.

**The next clearest failures**, from the causes at this commit: 1,527
"negative parse expected" files (early errors still missing: `arguments`
in field initialisers, `await`/`yield` as names, `import()` arity — the
next commits take the first three), 482 parse errors at `;` (the
`for ([a, b] of xs)` and `catch ({x})` heads, 600 files, taken next), 350
`with`-statement files, ~200 `using` declarations, the private brand checks
(~150, taken next), `import.defer` (~150), TCO (30).

## Measured at `00b5ab38` — 2026-09-12, night

Same instrument, same corpus, engine at `00b5ab38` on `ctbrowser-wpt`
(browser gate 540/541 at that commit — the one red is `ctcompile_lit`, repaired
by Codex's `f57cab15`; `tools/check/test262-baseline.sh` on the devbox, 4
workers, 10 s timeout, 2 GB cap):

| area | tests | pass at `15f47064` | pass at `00b5ab38` | delta | fail | crash / host |
|---|---:|---:|---:|---:|---:|---:|
| `test/language` | 23,726 | 17,226 | **18,350** | +1,124 | 5,348 | 0 / 6 |
| `built-ins/Array` | 3,082 | 2,624 | **2,774** | +150 | 280 | 0 / 11 |
| `built-ins/Object` | 3,411 | 3,128 | **3,275** | +147 | 134 | 0 / 0 |
| `built-ins/Number` | 340 | 273 | **333** | +60 | 6 | 0 / 0 |
| `built-ins/Math` | 327 | 279 | **326** | +47 | 1 | 0 / 0 |
| `built-ins/String` | 1,223 | 975 | **1,096** | +121 | 124 | 0 / 0 |
| `built-ins/Boolean` | 51 | 42 | **49** | +7 | 1 | 0 / 0 |
| `built-ins/Function` | 509 | 331 | **410** | +79 | 86 | 0 / 0 |
| `built-ins/Error` | 93 | 72 | **83** | +11 | 5 | 0 / 0 |
| `built-ins/JSON` | 165 | 101 | **137** | +36 | 26 | 0 / 0 |
| **total** | **32,927** | **25,051** | **26,833** | **+1,782** | | |

**26,833 of 32,927 (81.5%), from 76.1% at `15f47064`.** Not one crash left:
the 30 of the previous row are the fixes named there. The built-ins columns
are agent G's round two (Date, `encodeURI`/`decodeURI`, `@@toPrimitive`,
`JSON.rawJSON`, `Number`/`Math` edge cases), merged after the previous
measurement; `test/language` +1,124 is `15f47064..00b5ab38`: the `for ([a, b]
of xs)` and `catch ({x})` heads (`99879d88`, the 482 "parse error at `;`"
files), the early errors of `516e7522` and `00b5ab38`, the private-name brand
check (`308b8172`), `import()` arity (`11018eea`), `yield` as a name outside a
generator (`6559e566`).

**53 files went PASS -> FAIL**, two causes, both named so that they are not
read as noise:

- **12 early errors the new destructuring heads skip** —
  `statements/for-{in,of}/dstr/{array-elem-target-simple-strict,
  array-rest-before-elision, obj-id-init-simple-strict, obj-id-simple-strict,
  obj-rest-before-comma-invalid}.js`, the `for-await-of` twin and
  `statements/try/early-catch-duplicates.js`: the pattern checks (strict
  `eval`/`arguments` targets, a rest element before an elision, duplicate
  catch bindings) ran on declarations and not on a `for` head or a catch
  parameter. The compiler's, and next.
- **~40 bitwise and shift files** (`expressions/bitwise-*`, `left-shift`,
  `right-shift`, `unsigned-right-shift`, `compound-assignment/S11.13.2_A4.*`,
  `prefix-increment/S11.4.4_A4_T3`): `new String("1") & "1"` is 1 in the
  specification and is 0 here since `new String()` became a real wrapper
  object (agent B, `d27d8f36..502c691f`). `binary_op_static` converts with the
  STATIC `to_int32`, which never runs `valueOf` — a documented deviation
  carried in `include/ctbrowser/aot/aot_helpers.def` (the non-re-entering
  family, `may_reenter 0`) as a contract with ctcompile's native backend. The
  fix is to move the six bitwise opcodes (and `add`) to the re-entering
  family, which is an ABI change made together with Codex and is proposed in
  the journal, not made here.

**The next clearest failures** at this commit (test/language, by cause):
1,326 "negative parse expected" (`identifiers` 114, `literals/regexp` 179,
class elements ~180, `module-code` 111), 350 `with` files, 139
`eval-code/direct` "Expected a SyntaxError", the `dynamic-import` parse error
at `import.` (154), `using`/`await-using` (~130). Built-ins: `Array` 280
(60 are the BigInt typed arrays being absent, 41 "Expected a TypeError"),
`Object` 134, `String` 124, `Function` 86.

## Measured at `b346dc0b` — 2026-09-13, the WHOLE corpus for the first time

`tools/check/test262-baseline.sh` widened on 2026-09-12 from ten hand-picked
areas to `test/language`, `test/built-ins` and `test/annexB` - every file the
corpus holds for the language and its library, 48,624 of them against 32,927
before - because the old list scored the areas that were being worked on and
made the rest invisible: RegExp at 24%, Promise at 34% and TypedArray at 0.1%
were never a number in this file. Same instrument otherwise (devbox, 4 workers,
10 s timeout, 2 GB cap), engine at `b346dc0b` on `ctbrowser-wpt` (the merge of
`ctcompile-v1` `3e803401` into the audit cuts of the evening). Areas with
fewer than 93 files are in the JSON and not in the table:

| area | tests | pass before | pass now | delta | fail | crash/timeout/host | skip |
|---|---:|---:|---:|---:|---:|---:|---:|
| `language` | 23,726 | 18350 | **19,829** | +1479 | 3,869 | 0/1/6 | 21 |
| `annexB` | 1,086 | - | **369** | - | 675 | 0/0/0 | 42 |
| `built-ins/Temporal` | 4,603 | - | **0** | - | 4,603 | 0/0/0 | 0 |
| `built-ins/Object` | 3,411 | 3275 | **3,284** | +9 | 125 | 0/0/0 | 2 |
| `built-ins/Array` | 3,082 | 2774 | **2,774** | +0 | 280 | 0/1/11 | 16 |
| `built-ins/RegExp` | 1,879 | - | **938** | - | 924 | 3/0/2 | 12 |
| `built-ins/TypedArray` | 1,446 | - | **1** | - | 114 | 0/0/1192 | 139 |
| `built-ins/String` | 1,223 | 1096 | **1,149** | +53 | 71 | 0/0/0 | 3 |
| `built-ins/TypedArrayConstructors` | 738 | - | **74** | - | 39 | 0/0/495 | 130 |
| `built-ins/Promise` | 732 | - | **257** | - | 474 | 0/0/0 | 1 |
| `built-ins/Iterator` | 654 | - | **13** | - | 640 | 0/0/0 | 1 |
| `built-ins/Date` | 594 | - | **580** | - | 11 | 0/0/0 | 3 |
| `built-ins/DataView` | 561 | - | **0** | - | 442 | 0/0/0 | 119 |
| `built-ins/Function` | 509 | 410 | **432** | +22 | 64 | 0/0/0 | 13 |
| `built-ins/Atomics` | 389 | - | **0** | - | 0 | 0/0/0 | 389 |
| `built-ins/Set` | 383 | - | **368** | - | 14 | 0/0/0 | 1 |
| `built-ins/Number` | 340 | 333 | **333** | +0 | 6 | 0/0/0 | 1 |
| `built-ins/Math` | 327 | 326 | **327** | +1 | 0 | 0/0/0 | 0 |
| `built-ins/Proxy` | 311 | - | **146** | - | 129 | 0/0/0 | 36 |
| `built-ins/ArrayBuffer` | 221 | - | **24** | - | 161 | 3/0/5 | 28 |
| `built-ins/Map` | 204 | - | **190** | - | 13 | 0/0/0 | 1 |
| `built-ins/JSON` | 165 | 137 | **137** | +0 | 26 | 0/0/0 | 2 |
| `built-ins/Reflect` | 153 | - | **115** | - | 38 | 0/0/0 | 0 |
| `built-ins/WeakMap` | 141 | - | **135** | - | 5 | 0/0/0 | 1 |
| `built-ins/AsyncDisposableStack` | 104 | - | **0** | - | 101 | 0/0/2 | 1 |
| `built-ins/SharedArrayBuffer` | 104 | - | **0** | - | 0 | 0/0/0 | 104 |
| `built-ins/Symbol` | 98 | - | **48** | - | 31 | 0/0/0 | 19 |
| `built-ins/NativeErrors` | 94 | - | **82** | - | 6 | 0/0/0 | 6 |
| `built-ins/DisposableStack` | 93 | - | **0** | - | 91 | 0/0/1 | 1 |
| `built-ins/Error` | 93 | 83 | **83** | +0 | 5 | 0/0/0 | 5 |
| **total** | **48,624** | 26,833 | **32,295** | | 13,485 | 6/2/1718 | 1118 |

**32,295 of 48,624 (66.4%); of the 47,506 that ran, 68.0%.** The ten old
areas alone are 28,397 of 32,927 (86.2%), from 26,833 (81.5%) at `00b5ab38`.
`test/language` +1,479 is agent J's evening (`3fad5e7c`: the `with`
statement, restricted-production ASI, import attributes, hashbang - ctjs
`b2b5155`) and the class/destructuring early errors; `String` +53 and
`Function` +22 are the RegExp 22.2 work landing in `split`/`replace` and
`Function.prototype.toString`. **6 files went PASS -> FAIL** across the
same period: `comments/hashbang/multi-line-comment.js` (a refusal where the
parser must reject), the three `module-code/import-attributes/early-dup-
attribute-key-*` files (the duplicate-key early error the ctjs bump was
meant to carry - lost between the submodule and the checker), `class/
elements/privatefieldset-evaluation-order-3.js` and `class/subclass/derived-
class-return-override-for-of-arrow.js`. All six are in the language agent's
brief for the next round.

**Where the corpus says the holes are**, in files, largest first:
`Temporal` 4,603 (not implemented, not planned - it is a calendar library
the size of the rest of the standard library); the typed arrays - 1,707
files across `TypedArray`, `TypedArrayConstructors`, `Array` and `DataView`
die in `harness/testTypedArray.js` before the test runs, on
`BigInt64Array` being absent, and `DataView` is not defined (388); `Iterator`
is not defined (407, the ES2025 iterator helpers); `RegExp` 924 FAIL of
1,879; `Promise` 474 of 732; `Proxy` 129 of 311; `DisposableStack` /
`AsyncDisposableStack` / `SuppressedError` (ES2026 explicit resource
management, 201 files, none passing); `annexB` 675 of 1,044 - 192 of them
the "initialized binding is not created" family, which is Annex B.3.2's
web-compat block-level function hoisting, and 27 `escape`/`unescape`.
In `test/language` the top causes are the ones the `00b5ab38` row named,
each smaller: 544 early errors, 348 wrong `SameValue`s (classes), 193
"Expected a SyntaxError" (139 in direct eval), 154 `import.source`/
`import.defer` (parsed as a broken `import.meta`), 126 missing TypeErrors,
94 missing ReferenceErrors.

## Measured at `9c70aaa0` — 2026-09-16, the four round-one branches merged

The four agent branches of 2026-09-13 (`docs/plans/wpt-next.md` §1: T typed
arrays, P promise/proxy/iterator/disposable, W dom/html, E `test/language`)
merged onto `ctcompile-v1` `09341902` - which carries the ponytail audit of
2026-09-15/16 (Script dedups, plain DOM node payloads, the bindings helpers) -
as `c5682f32`, `20f17da4`, `247a7c53`, `9c70aaa0`, the ctjs gitlink at
`69cc3d8`. Same instrument (devbox, 4 workers, 10 s timeout, 2 GB cap,
`tools/check/test262-baseline.sh` over the whole corpus). Areas with fewer
than 93 files are in the JSON and not in the table:

| area | tests | pass before | pass now | delta | fail | crash/timeout/host | skip |
|---|---:|---:|---:|---:|---:|---:|---:|
| `language` | 23,726 | 19829 | **21,402** | +1573 | 2,302 | 0/1/0 | 21 |
| `annexB` | 1,086 | 369 | **378** | +9 | 665 | 1/0/0 | 42 |
| `built-ins/Temporal` | 4,603 | 0 | **0** | +0 | 4,603 | 0/0/0 | 0 |
| `built-ins/Object` | 3,411 | 3284 | **3,300** | +16 | 109 | 0/0/0 | 2 |
| `built-ins/Array` | 3,082 | 2774 | **2,800** | +26 | 265 | 0/1/0 | 16 |
| `built-ins/RegExp` | 1,879 | 938 | **939** | +1 | 925 | 3/0/0 | 12 |
| `built-ins/TypedArray` | 1,446 | 1 | **752** | +751 | 555 | 0/0/0 | 139 |
| `built-ins/String` | 1,223 | 1149 | **1,156** | +7 | 64 | 0/0/0 | 3 |
| `built-ins/TypedArrayConstructors` | 738 | 74 | **280** | +206 | 328 | 0/0/0 | 130 |
| `built-ins/Promise` | 732 | 257 | **721** | +464 | 10 | 0/0/0 | 1 |
| `built-ins/Iterator` | 654 | 13 | **608** | +595 | 45 | 0/0/0 | 1 |
| `built-ins/Date` | 594 | 580 | **583** | +3 | 8 | 0/0/0 | 3 |
| `built-ins/DataView` | 561 | 0 | **438** | +438 | 4 | 0/0/0 | 119 |
| `built-ins/Function` | 509 | 432 | **438** | +6 | 58 | 0/0/0 | 13 |
| `built-ins/Atomics` | 389 | 0 | **0** | +0 | 0 | 0/0/0 | 389 |
| `built-ins/Set` | 383 | 368 | **368** | +0 | 14 | 0/0/0 | 1 |
| `built-ins/Number` | 340 | 333 | **335** | +2 | 4 | 0/0/0 | 1 |
| `built-ins/Math` | 327 | 327 | **327** | +0 | 0 | 0/0/0 | 0 |
| `built-ins/Proxy` | 311 | 146 | **173** | +27 | 102 | 0/0/0 | 36 |
| `built-ins/ArrayBuffer` | 221 | 24 | **190** | +166 | 3 | 0/0/0 | 28 |
| `built-ins/Map` | 204 | 190 | **190** | +0 | 13 | 0/0/0 | 1 |
| `built-ins/JSON` | 165 | 137 | **137** | +0 | 26 | 0/0/0 | 2 |
| `built-ins/Reflect` | 153 | 115 | **150** | +35 | 3 | 0/0/0 | 0 |
| `built-ins/WeakMap` | 141 | 135 | **135** | +0 | 5 | 0/0/0 | 1 |
| `built-ins/AsyncDisposableStack` | 104 | 0 | **103** | +103 | 0 | 0/0/0 | 1 |
| `built-ins/SharedArrayBuffer` | 104 | 0 | **0** | +0 | 0 | 0/0/0 | 104 |
| `built-ins/Symbol` | 98 | 48 | **76** | +28 | 3 | 0/0/0 | 19 |
| `built-ins/NativeErrors` | 94 | 82 | **82** | +0 | 6 | 0/0/0 | 6 |
| `built-ins/DisposableStack` | 93 | 0 | **92** | +92 | 0 | 0/0/0 | 1 |
| `built-ins/Error` | 93 | 83 | **85** | +2 | 3 | 0/0/0 | 5 |
| **total** | **48,624** | 32,295 | **36,962** | | 10,538 | 4/2/0 | 1118 |

**36,962 of 48,624 (76.0%); of the 47,506 that ran, 77.8%** - from 32,295
(66.4%) at `b346dc0b`, **+4,717 FAIL -> PASS and 50 PASS -> FAIL**. The
agents' own numbers held through the merge: `TypedArray` 1 -> 752,
`TypedArrayConstructors` 74 -> 280, `DataView` 0 -> 438, `ArrayBuffer`
24 -> 190, `Uint8Array` 4 -> 66; `Promise` 257 -> 721 of 732, `Iterator`
13 -> 608, `DisposableStack` 0 -> 92, `AsyncDisposableStack` 0 -> 103,
`SuppressedError` 0 -> 20, `AggregateError` 0 -> 23, `Proxy` 146 -> 173,
`Reflect` 115 -> 150, `Symbol` 48 -> 76; `test/language` 19,829 -> 21,402.

**The 50 lost, each read:** 19 `annexB/language/function-code` (`block-decl-
func-*`, `if-decl-*`, `switch-case-*`: agent E made a block's function
declaration block-local per 14.2.1 and dropped the sloppy-mode var binding
B.3.3 gives it - fixed in `09798323` on top of this row, unmeasured until the
next); 11 `using`/`await using` use-before-initialization (the TDZ of a
`using` binding reads as a TypeError from the disposal register rather than
a ReferenceError; agent E's own report named these); 5 `Array/fromAsync`
(rejections that the two-tick thenable resolution of `20f17da4` now surfaces
in a different order); 5 decorator syntax files (`decorators` is a stage-3
feature test262 gates behind a flag; the parser refuses `@` on purpose now -
the runner should skip the feature); `tagged-template/tco-*` (2, tail calls);
`for-in`/`for-of` `head-var-bound-names-in-stmt` (a `var x;` inside the body
wrote undefined over the element - fixed in `09798323`); `S10.6_A5_T3`,
`namespace/internals/super-set-to-tdz-binding-with-accessor`, `AsyncFunction-
construct` (the `new AsyncFunction` body parse), `Object/prototype/toString/
proxy-revoked`, `TypedArrayConstructors/internals/Set/key-is-out-of-bounds-
receiver-is-not-object`, `annexB/language/statements` (1).

**Where the corpus says the holes are now**, in files: `Temporal` 4,603 (not
planned); `RegExp` 925 - 469 of them `property-escapes/generated` (the
Unicode property tables behind `\p{...}`, a data-generation job), 57
`unicodeSets/generated` and 28 `prototype/unicodeSets` (the `v` flag), and
175 "negative parse/SyntaxError" the regexp early-error pass does not raise;
`TypedArray` 555 + `TypedArrayConstructors` 328 + 60 in `Array/prototype` -
`BigInt64Array`, `BigUint64Array` and `Float16Array` are not defined
(`value.hpp`'s `element_kind` has no `big_i64`/`big_u64`/`f16`), which is
now the single largest lever in `built-ins`; `annexB` 665 (192 the B.3.3
family above, 27 `escape`/`unescape`, RegExp legacy statics, `__proto__`
and the `__defineGetter__` family); `language` 2,302 (decorators 300-odd
behind the feature flag, `import.source`/`import.defer`, the rest early
errors and class edge cases); `Array/prototype` 265; `Object` 109;
`Function` 58; `JSON` 26; `BigInt` 42 (`asIntN`/`asUintN` landed in
`2b9d7071` on top of this row); `parseInt` 16; `isFinite`/`isNaN` 9 each.

**And a note on the gate at this SHA.** The ctbrowser half of the build and
every ctbrowser test the run reached were green, but `tools/remote-build.sh`
stopped in ctcompile's `map_flow` pipeline fixture, which was no longer
provably native: agent E's compiler gave every object-literal method an own
`__home` property (a closure -> literal edge the escape analysis refuses).
`5865c08b` emits the link only for a method that says `super`; the next row
is the one measured behind a green gate.

## Measured at `b722aa41` — 2026-09-18, round seven (J2: RegExp and the async tail)

**40,832 of 48,624 (84.0%; 85.4% of the 47,791 that ran)**, from 39,731 at
`9f8da347`: **+1,101 files, PASS->FAIL 0**. Round seven's agent J2: `\p{..}`
/ `\P{..}` property escapes over a generated UCD table
(`lib/Script/regex_properties.inc`, `tools/gen/unicode_properties.py`),
Canonicalize under `iu`, the `v` flag's ClassSetExpression (nested
classes, `--`, `&&`, `\q{..}`, properties of strings), `new RegExp` judged
by the literal's early-error scan, async generator `.return(v)` awaiting
`v` and running `finally`, `yield*` forwarding return/throw, `import defer
* as ns`, the test262 host resolving re-exports over the whole graph, an
object rest that leaves out computed keys. Same instrument (devbox, 4
workers, 10 s, 2 GB); the rows are every area that moved:

| area | tests | pass before | pass now | delta | fail | crash/timeout/host | skip |
|---|---:|---:|---:|---:|---:|---:|---:|
| `built-ins/RegExp` | 1,879 | 940 | **1,822** | +882 | 45 | 0/0/0 | 12 |
| `language` | 23,726 | 22107 | **22,300** | +193 | 1,404 | 0/1/0 | 21 |
| `built-ins/AsyncFromSyncIteratorPrototype` | 38 | 18 | **30** | +12 | 8 | 0/0/0 | 0 |
| `built-ins/AsyncGeneratorPrototype` | 48 | 40 | **48** | +8 | 0 | 0/0/0 | 0 |
| `annexB` | 1,086 | 776 | **781** | +5 | 262 | 1/0/0 | 42 |
| `built-ins/String` | 1,223 | 1168 | **1,169** | +1 | 51 | 0/0/0 | 3 |
| **total** | **48,624** | 39,731 | **40,832** | +1,101 | 6,956 | 1/2/0 | 833 |

## Measured at `9f8da347` — 2026-09-17, round six (J: the VM tail)

Round six's agent J worked the VM: an `await` no longer truncates the
register stack below its caller (the "default parameter read as undefined"
bug that HARNESS_ERRORed every WPT file calling `promise_setup`), a boxed
block-level `let` whose initialiser holds a closure is a cell before the
initialiser runs, `export * as ns from` and `export` of a destructuring
declaration, structuredClone per HTML 2.7, `class A extends Array` instances
ARE arrays, typed arrays without an own `length`, %ThrowTypeError%, the
arguments object's shape, ToNumeric before the BigInt decision, proxy traps
as GetMethod with `[[Construct]]` carrying new.target and the get/set/has
invariants, an eval program's completion value, well-formed
`JSON.stringify`, `%AsyncIteratorPrototype%[Symbol.asyncDispose]`, `class
F extends Function`. Same instrument (devbox, 4 workers, 10 s, 2 GB), before
= `6edb7421` for `language`/`annexB` and `273773cd` for `built-ins` (which
were byte-identical through round five); the rows shown are those plus every
`built-ins` area that moved by five or more files:

| area | tests | pass before | pass now | delta | fail | crash/timeout/host | skip |
|---|---:|---:|---:|---:|---:|---:|---:|
| `language` | 23,726 | 21904 | **22,107** | +203 | 1,597 | 0/1/0 | 21 |
| `annexB` | 1,086 | 776 | **776** | +0 | 267 | 1/0/0 | 42 |
| `built-ins/Object` | 3,411 | 3321 | **3,332** | +11 | 77 | 0/0/0 | 2 |
| `built-ins/Array` | 3,082 | 2825 | **2,896** | +71 | 169 | 0/1/0 | 16 |
| `built-ins/TypedArray` | 1,446 | 1267 | **1,419** | +152 | 20 | 0/0/0 | 7 |
| `built-ins/TypedArrayConstructors` | 738 | 569 | **606** | +37 | 56 | 0/0/0 | 76 |
| `built-ins/Proxy` | 311 | 174 | **218** | +44 | 57 | 0/0/0 | 36 |
| **total** | **48,624** | 39,175 | **39,731** | +556 | 8,054 | 4/2/0 | 833 |
39,731 of 48,624 (81.7%); of the 47,791 that ran 83.1%

**39,731 of 48,624 (81.7%); of the 47,791 that ran, 83.1%** - from 39,175
(80.6%): **+556**. J's own per-directory measure on its gate-4 binary
(before its last two commits) saw +203 / -2 in `language` and +343 / -27 in
`built-ins`, the 27 lost all in `bc8b3f7f`'s proxy `[[Construct]]` and
addressed by `e80643ea`; this row, on the merged tree, is the first measure
with those in and the per-area totals above are the whole of it. `annexB`
is unchanged at 776.

**BYTECODE SHAPE** (for the native backend): a boxed block-level `let`/`const`
whose initialiser contains a function or class now emits `load_undef;
new_cell; init -> tmp; cell_set` where it was `init; new_cell`; other
declarations are unchanged. And an `await` no longer shrinks `registers_`
below the caller's `frame_size + 8`. AGENT-SYNC.md carries the full JOURNAL
line; ctjs gitlink unchanged at `3cb2ef9`.

**Still the biggest holes** (`docs/plans/wpt-next.md` has the briefs):
`Temporal` 4,603; `RegExp` 924 - 469 `property-escapes/generated` (the UCD
tables behind `\p{...}`, a generated-data job, round seven's J2), 57 + 28
the `v` flag; `language` 1,597 - 173 "Expected SameValue" (class fields,
async generators), 93 negative-parse SyntaxErrors the compiler does not
raise, 91 "Expected a TypeError", 64 "Expected a ReferenceError", 60 `import
defer` (the loader takes `defer` as a default import's name), 32 "call stack
exhausted" (tail calls), the eval-code cluster (76: direct eval's scope);
`annexB` 267; `Array` 169; `Proxy` 57.

## Measured at `6edb7421` — 2026-09-16, round four (T2: language + annexB)

Round four's agent T2 worked `test/language` and `test/annexB`; the other
three agents (S, G2, U2) touch no `test262` path, so `test/built-ins` is
byte-identical to `273773cd`. Same instrument (devbox, 4 workers, 10 s, 2 GB),
before = `273773cd`; only the two areas that moved plus the total:

| area | tests | pass before | pass now | delta | fail | crash/timeout/host | skip |
| `language` | 23,726 | 21766 | **21,904** | +138 | 1,800 | 0/1/0 | 21 |
| `annexB` | 1,086 | 469 | **776** | +307 | 267 | 1/0/0 | 42 |
| **total** | **48,624** | 38,730 | **39,175** | | 8,610 | 4/2/0 | 833 |

**39,175 of 48,624 (80.6%); of the 47,791 that ran, 82.0%** - from 38,730
(79.7%) at `273773cd`: **+445 FAIL -> PASS, 0 PASS -> FAIL** (full PASS-set
diff, all three areas). `annexB` **469 -> 776** is the whole of it plus 138:
Annex B.3.3's block-level function declarations, done as the spec's two steps
(the lexical binding in the block, then the var write to the enclosing scope
as a DECLARATION's write) - which retired the 232-file "An initialized
binding is not created" cluster that was the single largest in the corpus.
`language` **21,766 -> 21,904**: a catch parameter block-scoped in a script,
`import`/`export` as ModuleItems with a module's own top-level rules, and
`export default function f(){}` binding `f`.

**BYTECODE SHAPE** (recorded for the native backend, which pins it): `5dddb500`
- a script's block-level function declaration now binds a local first, so the
sequence for `{ function f(){} }` at script top level changed. AGENT-SYNC.md
carries the full JOURNAL line; ctjs gitlink unchanged at `3cb2ef9`.

**Still the biggest holes:** `Temporal` 4,603; `RegExp` ~924; `language`
1,800 (class/async SameValue clusters, module early errors, `import.source`/
`import.defer`); `annexB` 267 (the remaining B.3.2 "value is not updated"
cases, `escape`/`unescape`, the `__defineGetter__` family); `Array` 240.

## Measured at `273773cd` — 2026-09-16, rounds two and three merged

`docs/plans/wpt-next.md` §3 (round two: A animations, G properties, L
layout, C cascade, merged by session 13 as `c5c660cb`..`e29e197f`) and §5
(round three: B typed-array kinds, U URL, D parser, F forms/range, merged
by session 14 as `cb2cdcdf`), then the fixes the merged gate demanded
(`d626b2d6`..`273773cd`). Same run as the `docs/wpt.md` row of this SHA:
devbox, `tools/check/test262-baseline.sh`, 4 workers, 10 s, 2 GB. The
before column is `9c70aaa0`, the previous row; the rows shown are
`language`, `annexB` and every `built-ins` area that moved by five or more
files.

| area | tests | pass before | pass now | delta | fail | crash/timeout/host | skip |
| `language` | 23,726 | 21402 | **21,766** | +364 | 1,938 | 0/1/0 | 21 |
| `annexB` | 1,086 | 378 | **469** | +91 | 574 | 1/0/0 | 42 |
| `built-ins/Object` | 3,411 | 3300 | **3,321** | +21 | 88 | 0/0/0 | 2 |
| `built-ins/Array` | 3,082 | 2800 | **2,825** | +25 | 240 | 0/1/0 | 16 |
| `built-ins/TypedArray` | 1,446 | 752 | **1,267** | +515 | 172 | 0/0/0 | 7 |
| `built-ins/String` | 1,223 | 1156 | **1,168** | +12 | 52 | 0/0/0 | 3 |
| `built-ins/TypedArrayConstructors` | 738 | 280 | **569** | +289 | 93 | 0/0/0 | 76 |
| `built-ins/Iterator` | 654 | 608 | **649** | +41 | 4 | 0/0/0 | 1 |
| `built-ins/DataView` | 561 | 438 | **516** | +78 | 5 | 0/0/0 | 40 |
| `built-ins/Function` | 509 | 438 | **467** | +29 | 29 | 0/0/0 | 13 |
| `built-ins/ArrayBuffer` | 221 | 190 | **204** | +14 | 4 | 0/0/0 | 13 |
| `built-ins/Map` | 204 | 190 | **198** | +8 | 5 | 0/0/0 | 1 |
| `built-ins/JSON` | 165 | 137 | **146** | +9 | 17 | 0/0/0 | 2 |
| `built-ins/WeakMap` | 141 | 135 | **140** | +5 | 0 | 0/0/0 | 1 |
| `built-ins/BigInt` | 77 | 34 | **63** | +29 | 13 | 0/0/0 | 1 |
| `built-ins/GeneratorPrototype` | 61 | 47 | **61** | +14 | 0 | 0/0/0 | 0 |
| `built-ins/parseInt` | 55 | 39 | **55** | +16 | 0 | 0/0/0 | 0 |
| `built-ins/AsyncGeneratorPrototype` | 48 | 23 | **40** | +17 | 8 | 0/0/0 | 0 |
| `built-ins/FinalizationRegistry` | 47 | 0 | **46** | +46 | 0 | 0/0/0 | 1 |
| `built-ins/encodeURI` | 31 | 23 | **30** | +7 | 1 | 0/0/0 | 0 |
| `built-ins/encodeURIComponent` | 31 | 23 | **30** | +7 | 1 | 0/0/0 | 0 |
| `built-ins/WeakRef` | 29 | 0 | **28** | +28 | 0 | 0/0/0 | 1 |
| `built-ins/ArrayIteratorPrototype` | 27 | 15 | **22** | +7 | 5 | 0/0/0 | 0 |
| `built-ins/AsyncGeneratorFunction` | 23 | 8 | **20** | +12 | 1 | 0/0/0 | 2 |
| `built-ins/GeneratorFunction` | 23 | 8 | **19** | +11 | 2 | 0/0/0 | 2 |
| `built-ins/AsyncFunction` | 18 | 9 | **16** | +7 | 1 | 0/0/0 | 1 |
| `built-ins/isFinite` | 15 | 6 | **15** | +9 | 0 | 0/0/0 | 0 |
| `built-ins/isNaN` | 15 | 6 | **15** | +9 | 0 | 0/0/0 | 0 |
| `built-ins/MapIteratorPrototype` | 11 | 1 | **10** | +9 | 1 | 0/0/0 | 0 |
| `built-ins/SetIteratorPrototype` | 11 | 1 | **10** | +9 | 1 | 0/0/0 | 0 |
| **total** | **48,624** | 36,962 | **38,730** | | 9,055 | 4/2/0 | 833 |

**38,730 of 48,624 (79.7%); of the 47,791 that ran, 81.0%** - from 36,962
(76.0%) at `9c70aaa0`: **+1,818 FAIL -> PASS, 50 PASS -> FAIL**. Of the
gain, 1,048 is round three alone (`e29e197f` -> `273773cd`: 37,682 ->
38,730, and NOT ONE file lost between those two) - agent B's `BigInt64Array`,
`BigUint64Array` and `Float16Array` (`TypedArray` 826 -> 1,267,
`TypedArrayConstructors` 303 -> 569, `BigInt` 34 -> 63), and session 13's
VM rows: `Function` 452 -> 467 (`new (f.bind(o, 1))(2)`), `language`
21,563 -> 21,766 (the block-level static TDZ, the 19.1 refusal for a plain
assignment to NaN/Infinity/undefined, ToNumeric of an object operand for the
bitwise operators and ++/--, two lone surrogates that meet are one code
point, encodeURI's URIError).

**The 50 lost against `9c70aaa0` are all session 12/13's, read at the time:**
30 `annexB/language/function-code` "An initialized binding is not created
prior to evaluation" (Annex B.3.2's `if`/`switch`/`for` function
declarations: `09798323` made a block's function declaration block-local and
`07637be0` withheld the var binding when an enclosing block declares the
name lexically - the `*-skip-early-err-*` files want the var binding created
and left uninitialised, which the compiler does not distinguish from "not
created"); 4 negative-parse files the parser accepts; 2 `JSON/stringify`
through a revoked proxy; `S10.4.3-1-17/20-s` (`typeof this` under a direct
eval in strict code); and 12 singles. None of them moved in this session.

**Still the biggest holes, in files:** `Temporal` 4,603; `RegExp` 924 (469
`property-escapes/generated`, the `v` flag 85, ~175 early errors the regexp
pass does not raise); `language` 1,938; `annexB` 574; `Array` 240;
`TypedArray` 172 + `TypedArrayConstructors` 93 (a subclass instance is not a
typed array, `ta.buffer === ta.buffer` is false, `$262.detachArrayBuffer`
throws); `Proxy` 101; `Object` 88.
