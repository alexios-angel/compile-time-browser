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
| `tools/check/test262-baseline.sh` | the ten areas below, run one after another with identical flags. Sequential: four workers is the cap the whole devbox shares. |
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

**9,314 of the 32,863 tests that ran passed: 28.3%.** That is the number, and it
is not a good one — nor should it look like one. The engine is a deliberate
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
filesystem, which is the shape `lib/Shell/browser.cpp` uses for a page. test262's
specifiers are all `./name.js` beside the importer, so resolution is
`std::filesystem` and nothing else. `_FIXTURE` files are dependencies, never
tests, exactly as INTERPRETING.md requires. Measured: `test/language/module-code`
reads **261 of 599**, and `test/language/export` 1 of 3 — the loader is real, and
what fails there is the engine's module semantics rather than the harness's
ability to present a graph.

### The two leniencies, stated

1. **A negative test matches on `thrown.constructor.name` OR `thrown.name`.**
   The suite means the constructor. In this engine they disagree: every error
   `context::throw_error` builds is put on the one Error prototype, so an
   engine-raised TypeError has `name === "TypeError"` and
   `constructor === Error`. Requiring the constructor would fail ~40 runtime
   negatives for a prototype-wiring detail unrelated to what they test. `ct262`
   reports both fields and the runner accepts either.
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
2. **No property attributes.** There is no writable / enumerable /
   configurable, so `Object.getOwnPropertyDescriptor` cannot answer and
   `verifyProperty` — used by 13,621 tests across the corpus — fails on contact.
   This is why `built-ins/Object` reads 20.7% while `Math` reads 47.7%.
3. **Missing methods, ~3,400 tests.** `TypeError: X is undefined, not a
   function`, spread across every built-in.
4. **The error constructors are not real constructors.** `TypeError.name` is
   undefined and `(new TypeError).constructor` is `Error`, so `assert.throws`
   cannot match even when the engine throws the right kind (336 tests say so
   explicitly).
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

`docs/script.md` is the engine's own account of what it implements and what it
refuses by name; this file is the independent measurement of the same thing.
Where they disagree, this one was measured.
