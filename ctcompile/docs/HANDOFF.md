# Handoff: continuing ctcompile

Native work continues on `ctcompile-v1` in the `compile-time-browser` monorepo.
The application driver remains incomplete; native compiler development uses
`ctjs-translate` and `ctjs-opt`. Build on the devbox using `tools/remote-build.sh`
under `/tmp/ctbrowser-devbox-build.lock`, then run the local formatter before
committing. There is no CI. Do not build on the small local machine.

## Native JSON chain recovered and gated, 2026-09-16 UTC

Resumed **efe8daa8**, identified in the previous handoff and AGENT-SYNC, by
replaying its draft over the audit landings in `codex-json-resume-20260916`.
**53b9f68a** and **0a7c0302** finish that thread; the old `claude-json-chain`
draft is superseded. Three agents split recovery review, native regressions and
the Bootstrap boundary survey. September 7 WIP is already an ancestor.

**53b9f68a** recovers bounded checked-call chains in source order. JSON member
origins follow complete register flow; both failure paths retain the exact saved
input. Zero-call limits, every insufficient budget and failed proofs publish no
partial evidence or source mutation. **0a7c0302** adds explicit original JSON/parse
identity, reuses the existing `JsonType` only behind a complete DOM proof, and
emits public `ctbrowser::parse_json` with owning `ctbrowser::json_value` results.
Success moves from `std::expected`; String failure arms own their bytes. No Script,
VM, collector, generic JSON fallback or second parser is emitted. **47d6e275**
documents the contract in [native DOM entries](native-dom-entry.md).

Focused gates passed **2/2 proof tests (3.92s)** and **3/3 DOM drivers (178.24s)**.
JSON covers **7 sources / 14 Node-VM observations / 8 GCC-Clang binaries / 72
refusals**, both providers, policies and layouts, plus a lifetime sanitizer after
document destruction. DOM Strings now reports **775 Node-VM observations / 8
binaries / 1,060 source refusals**, with its separate provenance/budget checks.
Complete **310-step build / 605/605 CTests (1706.00s) / 177/177 lit (1305.06s)
PASS**; full wrapper exit **0**. All **1,561 frozen input files / 113 submodule
files** match the devbox and implementation commit **0a7c0302**; API and checkpoint
docs were updated afterward. Evidence: `/tmp/ctcompile-json-resume/`, including
`full.log`, `full.exit`, `full-last-test.log`, manifests and measured JSON reports.

**a244a2f9** fixes remote sync with `rsync --checksum --no-times`: changed older
worktree contents invalidate Ninja, while identical files keep their timestamps.
Standalone rsync/shell checks pass. The first candidate run mixed stale objects
and is invalid; the fresh compile's const-MLIR-handle test error was fixed before
the green gates. The integrated **09341902** baseline also passed **604/604 CTests
(1503.00s)**. Formatting with 23.1.1 passes **844 C++ / 95 Python / 106 web**;
the required pinned formatter retains the same nine-file/26-diagnostic baseline.

Fresh Bootstrap remains **19/574 native / 0 of 43 globals**, both policies, without
skips or pruning. DOM Data remains **7/7**, Button **4/86**, with 22 Node lifecycle
observations and the unchanged VM inheritance failure. Those reports are identical
to the preceding measurements. All **1,123 escape rows match the current pinned
baseline**; only the already-landed audit program hash differs from the older
pre-format evidence. No browser/runtime or escape-analysis source changed.

**Exact next native boundary:** original `H.getDataAttribute -> M` now reaches the
complete typed DOM proof and refuses at **`ctjs.unary`**, under all four
provider/policy combinations. Original M retains **24 nine-register blocks** and
its handler at **^bb12**. Start with its Number truthiness (`!0`/`!1`), then prove
the full Boolean/Number/null prefix and mixed JSON result ownership, including the
saved optional-String guard. F's original regexp/callback key conversion and H's
attribute-key construction follow. Full dataset/config, initialization/inheritance,
retained callbacks and the application driver remain open; no full-bundle gain
is claimed.

**Independent next compiler task:** Claude's 2026-09-16T03:53 journal records a
temporary restoration of top-level `var x;` writes in **7ad52ce2** to satisfy
`bootstrap-host-prefix.py`'s wrapper proof. Import and prove the existing
`program::hoisted_vars` declaration metadata instead of depending on those writes;
keep runtime semantics as the oracle. The CTJS importer currently carries no such
metadata. Claude's pending runtime/audit branches remain his to land. Earlier
sections below are historical checkpoints.

## Helper/URI composition, the JSON chain draft and the audit, 2026-09-15 UTC

**7a337ee2** composes helper expansion with URI normalization: every
handler-owning function in the fingerprinted DOMSource clone is normalized by
`normalizeDOMURI` (which now takes the function name; the working contract is
re-fingerprinted between functions) and `expandDOMHelpers` then inlines the
structured invoke, treating it as opaque in the helper body check because the
complete DOM entry proof reproves the inlined result. **1392f435** adds three
helper-shaped nullable URI sources (function, saved read, arrow) and three
refusals (payload observed, unguarded nullable read, call inside a branch arm).
Focused gate: `ctcompile_native_dom_strings` PASS 133.48 s; the full gate on
that tip was 605/606 with `ctcompile_native_dom_entry` at its 300 s cap under
`-j8` (223 s in the previous green run) — both DOM driver caps are 900 s now.

The **efe8daa8** JSON draft at this checkpoint has been recovered and gated as
**53b9f68a / 0a7c0302**, described above. Do not resume the old draft again.

**Operator-directed ponytail audit** (this session, all by locked merge, each
branch gated in its own devbox dir): `41d0185a` tools/cmake (mingw builders in
one table, snapshot.sh and its selftest gone, shaderc/gen-shaders/ratchet
shims/CTProject.cmake/LLVMVersion.cmake/GLM deleted, compare.py on Pillow),
`d47a8dd8` Script dedups, `2eae1dc7` Core/DOM/Raster (plain in-place node
payloads, deque slab, one-queue scheduler, abstract ttf backend, GL probes
gone; tsan clean), `11185599` ctcompile lowering (the three PDLL files are
`OpRewritePattern`s, `mlir-pdll` is no longer needed, `withProvedClone`
replaces four host-preparation transactions, `ctjs::functionIndex`/
`isPrimitiveAttr`/`sameValueZero`, `--mode` and `manifest::mode` gone,
`DOMEntryAnalysis` charges its module census before the fingerprint — 604/604),
`4e0b3b77` Style (leading_imports gone, resolve() in engine.cpp, small helper
dedups; the two shorthand expanders were left as two contracts on purpose),
`c3108dc5` Shell (installer helpers, 600 lines out of public headers, WebGL
X-macro, `<canvas width=0>` per spec, ctx.font through the CSS parser, ANGLE
preference deleted). `24eeb654`/`7662b763`/`3c50bc67` format the test
JS/HTML/CSS and repin what that moved (`escape-claims/Initialize.cmake` hashes,
`expected.txt` program row; `Exports/boundary.js` stays byte-exact under
js-beautify ignore markers because 27 pinned hashes derive from it).
Two audit branches were still in their final gates at hand-over —
`audit-ctcompile-emit` (nine string-literal helper headers → compiled
`include/ctcompile/CTNative/Runtime/ctnative.hpp`; source-name provenance for
emitted identifiers removed, locals are `v<N>`) and `audit-ctcompile-tests`
(24 `cmake -P` checks and 7 driver registrations → lit, ~320 CTests → ~250 lit
tests; `check()` copies → `ctbrowser/test/support/check.hpp`) — see AGENT-SYNC
for who lands them. The integrated **09341902** baseline has since passed the combined gate above. Sanitizer findings outside the audit, not
fixed: `Script/builtins/collections/keyed.cpp:593` UAF,
`Style/css/calc/units.cpp:36` UAF, `Core/number_format.cpp:194` UB cast.

## Saved nullable URI guards and fingerprinting, 2026-09-15 UTC

Resumed the interrupted **6caa728b** full-validation thread, found in this
handoff and AGENT-SYNC. **16b1660d** records its recovered 605 non-lit and
177 lit passes without inventing the disconnected wrapper's missing exit status.
Both histories/unmerged branches were checked; September 7 WIP was already an
ancestor. Three agents reviewed the nullable proof, strict/API quality and repo
complexity/Boost opportunities; root recovered their service-limited drafts,
integrated the changes and ran the gates. No browser/runtime edits.

**b13382ec** separates the complete DOM entry proof from manifest parsing and
fingerprinting. **9517ff21** avoids cloning report-free IR during fingerprinting;
report-bearing input retains clone/clear behavior. Every freshness check remains,
with complete hashing and no cache. Legacy-hash equivalence, nested reports,
source nonmutation and changed-source fingerprints have a regression.

**bb7ba402** compiles the original M guard `if ('string' != typeof t) return t`
on a saved `getAttribute` result before one URI try/catch. The producer remains
`std::optional<std::string>`. A complete branch proof records the exact dominated
String uses, and emission copies its value only inside the selected arm. Null,
empty String, an independent reread, aliases, later DOM mutation and the original
catch snapshot remain distinct. Loose equality is accepted only for two proved
Strings; String-only branch/Invoke results use ordinary owning Strings. No Script,
AOT, generic nullable carrier, handle table or new decoder is emitted.

Focused **3/3 CTests in 131.04s PASS**. DOM Strings now covers **745 Node/VM
observations / eight GCC-Clang binaries / 1,048 source refusals**; the nullable
slice adds **80 observations / 52 refusals**. Both providers, policies and layouts
pass, including result lifetime after document destruction and every incomplete
host-proof budget. A positive-arm source exposed an unreachable importer epilogue
that rejoins a live return; its dead branch is now accepted while every source
operation/effect remains censused. Stable formatting passes **844 C++ / 104 Python /
33 web**; the required pinned formatter's **nine-file / 26-diagnostic** baseline
is byte-identical.

Timing uses unchanged Bootstrap IR, saved baseline/candidate tools, warm-up and
11 alternating pairs on the devbox. Fingerprint command median: **159.20 →
152.11 ms (4.45%)**; instrumented pass: **60.4 → 51.0 ms (15.56%)**. Output and
fingerprints match exactly. Tiny URI lowering measured **8.263 → 8.579 ms**,
so there is no measured general transcompilation speedup. The CLI clears supplied
reports before fingerprinting; its decorated-input timing does not measure the
clone fallback. See [the quality review](native-quality-review-2026-09-15.md).

Complete **310-step build / 606/606 CTests in 1454.14s / 177/177 lit in
962.95s PASS**, with the full wrapper's exit status **0** retained.
All **1,462 source / 113 submodule hashes** match devbox, local and committed
source. Fresh Bootstrap remains **19/574 native / 0 of 43 globals**, both policies,
no skips/prunes; DOM Data **7/7**, Button **4/86**, 22 Node observations and the
existing VM inheritance failure. Button/Data reports and all **1,123 escape rows**
are unchanged. Original M and the isolated nullable URI helper each refuse all
four provider/policy combinations at the helper source-shape check; M retains
**24 nine-slot blocks** and handler **^bb12**. No full-bundle gain is claimed.
Full WPT/test262 were not remeasured; browser/runtime and expectations are unchanged.
Evidence and replay scripts: `/tmp/ctcompile-nullable-uri/`, including durable
`full.log`, `full.exit`, `measured.json` and the isolated `next-probe.py`.

**Exact next boundary:** compose the existing helper expansion and URI recovery
inside the fingerprinted DOMSource transaction. The current driver chooses one
based only on a handler in the selected entry; a handler inside M reaches helper
expansion instead. Preserve the original call-site actual/capture/receiver proof,
handler vectors, checked status edges and all-budget rollback; reprove the complete
result before publication. Then original M needs JSON/parse identity and original
lookup order, sequential URI/JSON failure continuations, and mixed primitive/JSON
ownership. JSON lookup precedes decoding; either failure returns the saved input.
Full H, initialization/inheritance, retained config/callbacks and the native
application driver remain open. Evidence: `/tmp/ctcompile-nullable-uri/`.

## Earlier measurements

This file holds the current checkpoint. When replacing it, move superseded entries
to the dated history below; keep each file under 1,000 lines. Historical claims and
next steps retain their original context and are not current instructions.

| Date | History |
| --- | --- |
| 2026-09-15 | [URI continuations, shared cores and DOM factories](handoff/2026-09-15-01.md) |
| 2026-09-15 | [DOM captures and Number index evidence](handoff/2026-09-15-02.md) |
| 2026-09-14 | [DOM helpers, attributes and class methods](handoff/2026-09-14-01.md) |
| 2026-09-14 | [Original Bootstrap Data, DOM sessions and arrays](handoff/2026-09-14-02.md) |
| 2026-09-13 | [Browser cores, UMD and Data observations](handoff/2026-09-13-01.md) |
| 2026-09-13 | [Recorder callbacks, captured snapshots and array indices](handoff/2026-09-13-02.md) |
| 2026-09-12 | [Child Maps, nullable values and dense arrays](handoff/2026-09-12.md) |
| 2026-09-11 | [Caller-owned Maps, global aliases and accessors](handoff/2026-09-11.md) |
| 2026-09-10 | [Object keys, mixed carriers and Map sizes](handoff/2026-09-10.md) |
| 2026-09-09 | [Saved Map sizes, scalar globals and BigInt operations](handoff/2026-09-09-01.md) |
| 2026-09-09 | [Scalar initialization and arithmetic provenance](handoff/2026-09-09-02.md) |
| 2026-09-08 | [Map absence, object fields and child ownership](handoff/2026-09-08-01.md) |
| 2026-09-08 | [Finite Map results, nullable values and short-circuit reads](handoff/2026-09-08-02.md) |
| 2026-09-08 | [Map writes, payloads, keys and array retention](handoff/2026-09-08-03.md) |
| 2026-09-07 | [Map effects, source calls and exported getters](handoff/2026-09-07-01.md) |
| 2026-09-07 | [Host ownership, protected helpers and native groundwork](handoff/2026-09-07-02.md) |
| earlier | [Compiler bring-up and original AOT decisions](handoff/earlier.md) |
