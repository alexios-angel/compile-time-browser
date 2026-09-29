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

## Round fifteen VM recovery — 2026-09-29

The previous `r15-vm3` draft (33 tracked edits and two new ShadowRealm files
on `31c3d8ae0`) is recovered in `a1967639e`, with the supporting linear
surrogate compaction in `4edf98dce`. Its original 26-directory comparison
remains historical: **17,464 → 17,570 PASS**, with seven regressions. The
original JSON is preserved under
`/home/alex/.ctbrowser-auto/claude/ctbrowser/r15/vm3/{m-before,m-after}/`.

Fresh focused recovery measurements cover 14 affected directories: **4,053 →
4,141 PASS**, 27 FAIL and 93 SKIP out of 4,261 files. There are **88 gained
passes and no lost passes** against the same selections from the historical
base. All runs used four workers, a 10-second timeout and 2048 MiB address-space
limit on the devbox. The RegExp directory was remeasured after the final string
fix; the other 13 rows retain their first recovery measurements.

| directory under `test/` | PASS |
|---|---:|
| `built-ins/RegExp` | 1,867 |
| `annexB/built-ins/RegExp` | 55 |
| `built-ins/Function` | 490 |
| `built-ins/Proxy` | 270 |
| `built-ins/Set` | 382 |
| `built-ins/ShadowRealm` | 61 |
| `built-ins/Symbol` | 79 |
| `built-ins/eval` | 10 |
| `language/block-scope` | 145 |
| `language/eval-code` | 345 |
| `language/expressions/call` | 82 |
| `language/statements/const` | 135 |
| `language/statements/let` | 142 |
| `language/statements/using` | 78 |

All seven original regressions are repaired. Four early closure reads now
throw ReferenceError through a captured readiness cell, using existing
bytecode instructions. The three CharacterClassEscapes timeouts came from
joining the non-Unicode match's surrogate pairs by repeatedly shifting the
remaining string. An instrumented unchanged case took 25.247 seconds; linear
in-place compaction restored all **1,867/1,867 runnable RegExp tests**, with
12 existing cross-realm skips. That complete directory took 126.7 seconds;
the per-test timeout stayed at 10 seconds.

The recovered semantics include separate ShadowRealm intrinsics, wrapped
cross-realm calls, direct-eval variable environments, captured block bindings,
RegExp code-unit versus Unicode matching, Set iterator ordering, Symbol registry
lookup and revoked Proxy call/construct identity. Existing bytecode opcodes
and public method signatures are preserved; the VM remains the standards
oracle. Frame realms still share their parent's intrinsics, and ShadowRealm
records remain allocated for the context's lifetime.

Validation passed an explicit `ct262` plus 14 test-target build and all 14
selected CTests, then a 356-step rebuild and **16/16 CTests** after the string
repair: `core_basics`, `string_basics`, `vm_control_flow`, `direct_eval`,
`eval_declarations`, `regexp_unicode`, `regexp_model`, `shadow_realm`,
`vm_realms`, `symbol_basics`, `keyed_collections`, `is_constructor`,
`vm_objects`, `gc_pressure`, `frames`, `script_gc`. The pinned full formatting
check passed 1,182 C++ files, 155 Python and 116 web files. No full test262,
CTest, compiler lit or corpus/native matrix was run.

Exact locked commands, logs and JSON are in
`/home/alex/.ctbrowser-auto/codex/ctbrowser/artifacts/2026-09-29-claude-recovery/vm3-recovery/`:
`check.sh`, `check-linear.sh`, `results/` and `linear-results/`. The command for
each selected directory is `python3 tools/check/test262.py --binary
build/tools/ct262 --dir test/<directory> --jobs 4 --timeout 10 --memory-mb 2048
--json <output>`, in `projects/ctbrowser-agentV`. See `wpt.md` for the final
combined browser validation and integration status.

The next small language cluster is the four remaining function-local closure
TDZ failures across `let`, `const` and `using`, plus `let`'s global closure
write before initialization. The recovery fixes block-local bindings but
does not yet track runtime initialization of every function-body lexical.
These failures predate the recovered draft.

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

## Detailed sections

The following sections retain the complete original text and measurements.

<a id="the-pieces"></a>
- [The pieces](test262/01-the-pieces.md#the-pieces)
<a id="running-it"></a>
- [Running it](test262/01-the-pieces.md#running-it)
<a id="the-baseline--2026-09-02"></a>
- [The baseline — 2026-09-02](test262/01-the-pieces.md#the-baseline--2026-09-02)
<a id="top-failure-causes"></a>
- [Top failure causes](test262/01-the-pieces.md#top-failure-causes)
<a id="what-262-supports"></a>
- [What `$262` supports](test262/01-the-pieces.md#what-262-supports)
<a id="the-two-leniencies-stated"></a>
- [The two leniencies, stated](test262/01-the-pieces.md#the-two-leniencies-stated)
<a id="updating-the-expectations"></a>
- [Updating the expectations](test262/01-the-pieces.md#updating-the-expectations)
<a id="the-proof-that-the-harness-is-load-bearing"></a>
- [The proof that the harness is load-bearing](test262/01-the-pieces.md#the-proof-that-the-harness-is-load-bearing)
<a id="what-the-harness-found-on-its-first-run"></a>
- [What the harness found on its first run](test262/01-the-pieces.md#what-the-harness-found-on-its-first-run)
<a id="what-the-engine-is-missing-as-the-suite-sees-it"></a>
- [What the engine is missing, as the suite sees it](test262/01-the-pieces.md#what-the-engine-is-missing-as-the-suite-sees-it)
<a id="the-three-crashes-diagnosed"></a>
- [The three crashes, diagnosed](test262/01-the-pieces.md#the-three-crashes-diagnosed)
<a id="and-what-they-measure-now---2026-09-02-same-day-same-corpus"></a>
- [...and what they measure now - 2026-09-02, same day, same corpus](test262/01-the-pieces.md#and-what-they-measure-now---2026-09-02-same-day-same-corpus)
<a id="property-attributes---2026-09-03"></a>
- [Property attributes - 2026-09-03](test262/01-the-pieces.md#property-attributes---2026-09-03)
<a id="what-was-implemented"></a>
- [What was implemented](test262/01-the-pieces.md#what-was-implemented)
<a id="what-was-deliberately-not-implemented"></a>
- [What was deliberately NOT implemented](test262/01-the-pieces.md#what-was-deliberately-not-implemented)
<a id="the-15-that-went-pass---fail-each-diagnosed"></a>
- [The 15 that went PASS -> FAIL, each diagnosed](test262/01-the-pieces.md#the-15-that-went-pass---fail-each-diagnosed)
<a id="per-constructor-error-prototypes---2026-09-03"></a>
- [Per-constructor error prototypes - 2026-09-03](test262/01-the-pieces.md#per-constructor-error-prototypes---2026-09-03)
<a id="what-was-actually-wrong-which-is-not-what-the-brief-expected"></a>
- [What was actually wrong, which is not what the brief expected](test262/01-the-pieces.md#what-was-actually-wrong-which-is-not-what-the-brief-expected)
<a id="what-was-implemented-1"></a>
- [What was implemented](test262/01-the-pieces.md#what-was-implemented-1)
<a id="measured-per-test"></a>
- [Measured, per test](test262/01-the-pieces.md#measured-per-test)
<a id="what-is-deliberately-not-done"></a>
- [What is deliberately NOT done](test262/01-the-pieces.md#what-is-deliberately-not-done)
<a id="arrayprototype-is-generic-and-a-built-in-knows-its-arity--2026-09-07"></a>
- [Array.prototype is generic, and a built-in knows its arity — 2026-09-07](test262/01-the-pieces.md#arrayprototype-is-generic-and-a-built-in-knows-its-arity--2026-09-07)
<a id="the-receiver"></a>
- [The receiver](test262/01-the-pieces.md#the-receiver)
<a id="the-descriptors"></a>
- [The descriptors](test262/01-the-pieces.md#the-descriptors)
<a id="measured-per-area"></a>
- [Measured, per area](test262/01-the-pieces.md#measured-per-area)
<a id="what-else-changed-each-its-own-correctness-fix"></a>
- [What else changed, each its own correctness fix](test262/01-the-pieces.md#what-else-changed-each-its-own-correctness-fix)
<a id="what-is-deliberately-not-done-1"></a>
- [What is deliberately NOT done](test262/01-the-pieces.md#what-is-deliberately-not-done-1)
<a id="the-largest-thing-left-unchanged"></a>
- [The largest thing left, unchanged](test262/01-the-pieces.md#the-largest-thing-left-unchanged)
<a id="measured-at-0e5cfbef--2026-09-12-before-the-days-runtime-work"></a>
- [Measured at `0e5cfbef` — 2026-09-12, before the day's runtime work](test262/01-the-pieces.md#measured-at-0e5cfbef--2026-09-12-before-the-days-runtime-work)
<a id="measured-at-b570bd29--2026-09-12-after-the-days-runtime-work"></a>
- [Measured at `b570bd29` — 2026-09-12, after the day's runtime work](test262/01-the-pieces.md#measured-at-b570bd29--2026-09-12-after-the-days-runtime-work)
<a id="what-moved-on-2026-09-12"></a>
- [What moved on 2026-09-12](test262/01-the-pieces.md#what-moved-on-2026-09-12)
<a id="measured-at-15f47064--2026-09-12-evening"></a>
- [Measured at `15f47064` — 2026-09-12, evening](test262/02-measured-at-15f47064--2026-09-12-evening.md#measured-at-15f47064--2026-09-12-evening)
<a id="measured-at-00b5ab38--2026-09-12-night"></a>
- [Measured at `00b5ab38` — 2026-09-12, night](test262/02-measured-at-15f47064--2026-09-12-evening.md#measured-at-00b5ab38--2026-09-12-night)
<a id="measured-at-b346dc0b--2026-09-13-the-whole-corpus-for-the-first-time"></a>
- [Measured at `b346dc0b` — 2026-09-13, the WHOLE corpus for the first time](test262/02-measured-at-15f47064--2026-09-12-evening.md#measured-at-b346dc0b--2026-09-13-the-whole-corpus-for-the-first-time)
<a id="measured-at-9c70aaa0--2026-09-16-the-four-round-one-branches-merged"></a>
- [Measured at `9c70aaa0` — 2026-09-16, the four round-one branches merged](test262/02-measured-at-15f47064--2026-09-12-evening.md#measured-at-9c70aaa0--2026-09-16-the-four-round-one-branches-merged)
<a id="strict-string-conversion-and-native-call-fences--2026-09-18"></a>
- [Strict String conversion and native-call fences — 2026-09-18](test262/02-measured-at-15f47064--2026-09-12-evening.md#strict-string-conversion-and-native-call-fences--2026-09-18)
<a id="string-static-coercion-errors--2026-09-18"></a>
- [String static coercion errors — 2026-09-18](test262/02-measured-at-15f47064--2026-09-12-evening.md#string-static-coercion-errors--2026-09-18)
<a id="bigint-string-grammar-and-loose-equality--2026-09-18"></a>
- [BigInt string grammar and loose equality — 2026-09-18](test262/02-measured-at-15f47064--2026-09-12-evening.md#bigint-string-grammar-and-loose-equality--2026-09-18)
<a id="bigint-measured-at-96781de5--2026-09-18"></a>
- [BigInt measured at `96781de5` — 2026-09-18](test262/02-measured-at-15f47064--2026-09-12-evening.md#bigint-measured-at-96781de5--2026-09-18)
<a id="measured-at-b722aa41--2026-09-18-round-seven-j2-regexp-and-the-async-tail"></a>
- [Measured at `b722aa41` — 2026-09-18, round seven (J2: RegExp and the async tail)](test262/02-measured-at-15f47064--2026-09-12-evening.md#measured-at-b722aa41--2026-09-18-round-seven-j2-regexp-and-the-async-tail)
<a id="measured-at-9f8da347--2026-09-17-round-six-j-the-vm-tail"></a>
- [Measured at `9f8da347` — 2026-09-17, round six (J: the VM tail)](test262/02-measured-at-15f47064--2026-09-12-evening.md#measured-at-9f8da347--2026-09-17-round-six-j-the-vm-tail)
<a id="measured-at-6edb7421--2026-09-16-round-four-t2-language--annexb"></a>
- [Measured at `6edb7421` — 2026-09-16, round four (T2: language + annexB)](test262/02-measured-at-15f47064--2026-09-12-evening.md#measured-at-6edb7421--2026-09-16-round-four-t2-language--annexb)
<a id="measured-at-273773cd--2026-09-16-rounds-two-and-three-merged"></a>
- [Measured at `273773cd` — 2026-09-16, rounds two and three merged](test262/02-measured-at-15f47064--2026-09-12-evening.md#measured-at-273773cd--2026-09-16-rounds-two-and-three-merged)
<a id="rounds-eight-to-ten-and-temporal--2026-09-25"></a>
- [Rounds eight to ten, and Temporal — 2026-09-25](test262/03-measured-2026-09-25.md#rounds-eight-to-ten-and-temporal--2026-09-25)
<a id="direct-eval--2026-09-25"></a>
- [Direct eval — 2026-09-25](test262/03-measured-2026-09-25.md#direct-eval--2026-09-25)
<a id="round-eleven-and-the-module-host--2026-09-26"></a>
- [Round eleven, and the module host — 2026-09-26](test262/03-measured-2026-09-25.md#round-eleven-and-the-module-host--2026-09-26)
<a id="the-gc-pin-and-utf-16-strings--2026-09-26"></a>
- [The GC pin, and UTF-16 strings — 2026-09-26](test262/03-measured-2026-09-25.md#the-gc-pin-and-utf-16-strings--2026-09-26)
<a id="round-thirteen--2026-09-26"></a>
- [Round thirteen — 2026-09-26](test262/03-measured-2026-09-25.md#round-thirteen--2026-09-26)
<a id="object-literals-define-through-an-opcode--2026-09-29"></a>
- [Object literals define through an opcode — 2026-09-29](test262/03-measured-2026-09-25.md#object-literals-define-through-an-opcode--2026-09-29)
