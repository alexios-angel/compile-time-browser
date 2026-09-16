# Handoff: continuing ctcompile

Native work continues on `ctcompile-v1` in the `compile-time-browser` monorepo.
The application driver remains incomplete; native compiler development uses
`ctjs-translate` and `ctjs-opt`. Build on the devbox using `tools/remote-build.sh`
under `/tmp/ctbrowser-devbox-build.lock`, then run the local formatter before
committing. There is no CI. Do not build on the small local machine.

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

**Next native boundary, drafted but UNBUILT:** branch `claude-json-chain`
(efe8daa8, worktree `~/Downloads/claude/wt/claude-json-chain`) carries original
M's protected body `JSON.parse(decodeURIComponent(t))` as a two-call chain:
`inspectSingleInvocationRegion(fn, steps, maxCalls)` accepts checked calls on one
success path, `normalizeDOMURI` emits nested invokes when the contract binds the
initial `JSON`, `DOMEntryAnalysis` gains `jsonIntrinsic`/`jsonParse`/`json` kinds
with `HostDOMMethod::jsonParse`, the lattice's existing `JsonType` gets
`carrier::json = ctbrowser::json_value` (String arms convert with
`ctbrowser::json_value(text)`), the emitter lowers the parse invoke to
`ctbrowser::parse_json` with `std::expected` moves, and
`test/CTNative/Browser/native_dom_json.py` (5 sources / 6 refusals, Node + VM
oracles) is registered as `ctcompile_native_dom_json`. It predates the audit
merges below and must be rebased (EmitC/Types.cpp, ScalarConversions.cpp,
LowerToEmitC.cpp and DOMEntry.cpp all moved) before its first devbox build.
After it: M's remaining prefix (`'true'`/`'false'`/`Number(t).toString()`/
`''`/`'null'` arms joining into `json_value`), then H.getDataAttribute's
`data-bs-${F(key)}` template.

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
for who lands them. The integrated tip had NOT had one combined full gate yet;
run `tools/remote-build.sh` first. Sanitizer findings outside the audit, not
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
