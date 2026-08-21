# Handoff: continuing ctcompile

You are picking up work on `ctcompile`, an ahead-of-time browser-application
compiler being built inside `compile-time-browser`. Everything below is
committed and green; nothing is pushed.

## Where things are

* Repo: `/mnt/c/Users/aange/Downloads/claude/compile-time-browser`
* Branch: **`ctcompile-v1`**, working tree clean
* Suite: **97/97** via `./tools/remote-build.sh`
* **Read `ctcompile/docs/plans/ctcompile.md` first** — it is the running plan in
  the house style (Done / Next, measured rungs) and it records every decision
  below with its reasoning. This file is a pointer to it, not a substitute.

The master plan is 21 markdown files in `../ctcompile-plan/` (NOT in the repo).
`00-START-HERE.md` routes by phase; **`01-objective-and-ground-truth.md`
overrides every other file**. Its six non-negotiables matter: `ctjs` is the
parser only, the bytecode is a REGISTER machine, there is no CI, EmitC is the
primary backend, sources are in `lib/`, build on the devbox.

## What is done

* **Phase -1** — the monorepo split. `ctbrowser/` is the CMake configure root;
  `ctcompile/` is a sibling project.
* **Phase 0** — six inventories the build checks, two differential comparators,
  and a recorded startup baseline. See `ctcompile/docs/baseline/*.json`.
* **Phase 2** — the AOT ABI: `ctbrowser/include/ctbrowser/aot/aot_helpers.def`,
  68 helpers over 83 of the 93 opcodes (84 `CT_AOT_COVERS` rows; `type_of` is
  served by two helpers on purpose). **THE TABLE IS DONE; PHASE 2'S GATE IS
  NOT** — the plan's gate is "VM code calls a hand-authored AOT closure through
  the real runtime ABI", and no helper has a body, no `function_proto` has a
  native entry, and nothing has ever called one. The record said "done" without
  that distinction; it is a contract, and a good one, that has not been
  executed.
  The runtime now at least COMPILES it: `ctbrowser/lib/Script/aot_contract.cpp`
  is a translation unit of nothing but `static_assert`s, in `ctbrowser-script`,
  so every preset checks it. Until 2026-08-21 the only file in the repository
  that included `aot.hpp` was `ctcompile/test/Inventories.cpp`, and `browser`,
  `browser-no-llvm`, `asan`, `tsan` and `windows` all build with
  `CTBROWSER_ENABLE_PROJECTS` empty — so the ABI and `EngineContract.hpp` were
  parsed in exactly one configuration out of six.
* **Phase 15** — a working program image, wired into the page load.
  `ctbrowser/{include,lib}/…/program_image.*` writes and reads a compiled
  `script::program`, validated exhaustively, and `browser::set_script_image()`
  uses it.

## The number that justifies the project

From `ctbrowser/docs/performance.md`: a whole p5 page load is 17.5% lexing,
15.1% `declare_local`, 7.6% `collect_captured_names` — and **1.4%
`context::run_loop`, the entire interpreter**. About forty percent of a page
load is READING JavaScript; 1.4% is executing it. So this compiler's value is
overwhelmingly in what it **deletes from startup**.

Measured, `ctcompile/docs/baseline/page-load.json`, p5-basic.html on the devbox:

| p5-basic.html, three classic scripts | ms |
|---|---|
| `load_html` compiling its own scripts | **69.65** |
| `load_html` handed one image per `<script>` | **19.93** |
| | **71% of the page load** |
| **editing the sketch only** | **19.77 — 3.5x, 1 of 3 recompiled** |

## What the last session did

1. **The source hash was 4.16 ms of that page load.** It is now
   `boost::hash2::xxhash_64`, 0.181 ms. **Do not "improve" it to a four-lane
   FNV over 64-bit words** — that was tried, it is faster (0.127 ms), and it
   collides on 50,678 of 262,145 single-byte edits of real p5.js. The plan
   explains why, and `ctcompile/test/ProgramImage.cpp` keeps that hash as a
   blinded control so the case that catches it can be watched failing.
2. **`page-load.json` re-recorded**, 53% → 72%.
3. **Operand validation**: the per-operand switch became a bound table, 19.74 →
   19.18 ms, 15 of 15 paired runs. The "suspect fast path" the previous handoff
   proposed was NOT implemented and should not be — see the plan.
4. **`function_proto::nested` deleted.** Nothing ever wrote it; its only reader
   was a ratchet check that could not fire. Image format 1 → 2, and the image is
   19 KB smaller for p5, 128 KB for babylon.
5. **The measurement tools are built by `all` now** — see the trap below.
6. **ONE PROGRAM PER `<script>`.** The image is keyed per script, so p5 is baked
   once and editing a sketch no longer invalidates 4.5 MB. It is also a
   conformance fix: a parse error or a throw in one script no longer stops the
   next, and each script is its own microtask checkpoint. What it removed is a
   forward call from an earlier script to a later script's function — Chrome
   makes that a ReferenceError too.
7. **Five defects found by adversarially reviewing that split**, three of them
   the split's own and two older: a dead script's `try` catching the next
   script's `throw` (`context::execute` never cleared `handlers_`), and a
   use-after-free on synchronous navigation, now fixed by queueing the load.
8. **The `asan` preset works again** — 29 of 52 tests were failing on a
   heap-use-after-free in the CSS parser that fires on every browser
   construction. 52 of 52 now.

9. **`finally` was wrong on six of nine specified behaviours** and is rewritten
   as a completion record. One of them lost exceptions outright. p5_api moved
   172 → 175.
10. **The 65,535 proto ceiling was three stray casts**, and Babylon sat at 49%
    of it. Gone; 140,001 functions verified.
11. **The fingerprint now hashes what the compiler EMITS**, not only which
    opcodes exist — a canary compiled and folded. The `finally` rewrite is
    exactly the change it was blind to.

## Do these next

1. **NOT Phase 16A or 16B, on this corpus.** `docs/baseline/page-load-profile.json`
   profiles what an image-loaded page load actually spends: HTML parsing is
   0.0%, CSS and style 0.5%, layout and paint absent. A compiled DOM blueprint
   and a compiled style program target under one percent between them. 16B is
   still *unblocked* — `engine::for_each_rule` exists — it is just not worth
   doing next for these pages.
2. **The image LOADER is now the largest single item on the path**, at 26%, and
   its operand pass alone is 7.49% — fifteen times the whole CSS engine. That
   is where the next startup millisecond is.
3. **Phases 1–6**, the runtime preparation. Phase 2's TABLE is done and its
   GATE is not — nothing has ever called a hand-authored AOT function through
   the ABI, which is the cheapest way to find out whether 1,881 lines of
   contract are right before 68 helper bodies depend on them.

## Known problems, not yet acted on

* ~~The 65,535 proto ceiling~~ — FIXED 2026-08-21, it was three casts.
* **OLD, KEPT FOR THE REASONING:** the 65,535 proto ceiling was at 49% on a
  corpus that already existed. Three
  of four `op::closure` emitters cast the function index to `uint16` before the
  32-bit `with_bx` (`statements.cpp:613`, `expressions.cpp:95`,
  `classes.cpp:156`; `classes.cpp:109` does not). Above 65,535 protos the
  COMPILER builds the wrong closure. Babylon is 31,905. The image writer refuses
  such a program rather than freezing the bug into a file.
* **The image is keyed to a whole page's concatenated scripts**, because
  `browser::run_scripts` compiles every classic `<script>` into ONE program. So
  editing an inline sketch invalidates the image for the 4.5 MB bundle beside
  it. Splitting per-script is an engine change: `compile_program` hoists
  function declarations across the whole concatenation, so a call in the first
  script to a function declared in a later one works today and would stop.
  **This is what stands between the current win and "bake p5 once, reuse it",
  and it is the highest-value thing left on this path.**
* **THE `asan` AND `tsan` PRESETS ARE NOT IN THE GATE.** `tools/remote-build.sh`
  runs the default preset only, and the CSS use-after-free above sat there
  through every green run until somebody built asan by hand. It is 52 of 52 now
  and nothing will notice when that stops being true. Running asan in the gate
  costs a second configure and build; deciding that is the next person's call.
* **`ctbrowser`'s benchmarks are still `EXCLUDE_FROM_ALL` with no aggregate**,
  which is the defect that invalidated the first computed-goto measurement and
  then this session's first page-load reading. Fixing them is the same three
  lines as `ctcompile-tools`.
* **The corruption fuzz prints a count it does not assert** —
  `ProgramImage.cpp` reports "1615 of 3205 offsets still loaded" and nothing
  pins it. Pinning it was considered and not done: the number depends on the
  fixture's compiled bytecode, which Phases 13 and 14 renumber deliberately, so
  a ratchet there would churn without signal. If validation changes, prove
  equivalence differentially instead — see the plan's note on the 60,000-mutation
  digest, which is how the bound-table rewrite was shown to be the same function.
* **`@font-face`** — fixed for `url()` in `a2ef736`, but the style engine still
  records a page font only when the family and url are string tokens elsewhere;
  check before assuming.

## How to work here (learned the hard way)

* **BUILD ON THE DEVBOX, ALWAYS**: `./tools/remote-build.sh` from the repo root.
  The WSL box has 7.5 GiB and has been taken down by local builds twice.
* **The devbox self-deallocates after 30 idle minutes.** When ssh times out:
  `cd ../infra/azure-build-server && ./server.sh start`.
* **The devbox shell is zsh, which does NOT word-split unquoted variables.**
  `CXX="clang++ -O2"; $CXX foo.cpp` fails as one word. Inline your flags.
* **Chain gates with `&&`, never `;` — AND NEVER THROUGH A PIPE.** A `;` after
  `tools/format.sh --check` let an unformatted commit through once; on
  2026-08-21 `./tools/format.sh --check | tail -1 && git commit` did it again,
  because a pipeline's exit status is the LAST command's and `tail` always
  succeeds. Redirect to a file and read it, or check the status first.
* **A green build does not mean the binary you are about to run was built.**
  `EXCLUDE_FROM_ALL` targets are not in `all` AT ALL, so `cmake --build`
  rebuilds the engine, relinks every test, reports 97/97 — and leaves an
  excluded executable at whatever revision someone last built by hand. That has
  now produced a wrong number in this tree three times
  (`docs/history/computed-goto.md`, `docs/performance.md`, and
  `ctcompile/docs/baseline/page-load.json`). The ctcompile measurement tools are
  fixed — `ctcompile-tools ALL` in `ctcompile/tools/CMakeLists.txt` — but
  **`ctbrowser`'s benchmarks still have it**, so anything measured with
  `ctbrowser-test-bench_*` must be built explicitly and checksummed.
  Separately, `rsync -az` preserves mtimes, so restoring a file can leave ninja
  thinking it is current; `touch` it. Distrust any figure that exactly matches
  the arm you were replacing.
* **No hardware perf counters on the devbox** (it is a VM) — `perf stat` reports
  `<not supported>`. Use callgrind for attribution, dhat for allocation, and an
  **interleaved A/B of two binaries** for wall clock. Not a before-and-after
  across sessions: the from-source page-load arm moved 7 ms between sessions
  with no commit that could explain it.
* **Profile the thing itself.** One callgrind run profiled a binary that
  compiled the program to build the image, and the compile drowned the load.

## The discipline that has been earning its keep

* **The positive case is one line; the negative cases are the file.** Every
  comparator here is verified against a deliberately BLINDED implementation, and
  every negative case must be seen going red. The source hash's cases go
  further: the blinded hashes live in the test permanently, and each case
  asserts that its control DOES collide, so a case that stops proving anything
  says so instead of passing.
* **Prove a guard is load-bearing by removing it, and say so plainly when it
  does not go red.** Done for both validation fixes in `59d0339` (one did, one
  did not) and for the Boost.Hash2 configure check, which was verified by
  pointing `CTBROWSER_BOOST_INCLUDE_DIR` at a Boost without Hash2.
* **Silence is not success.** Assert counters, never trust output. That guard
  caught a "58x speedup" that was a loader refusing every corpus.
* **Correct yourself in the record.** Four claims have now been committed and
  later corrected here. The most recent: a hoist that "the compiler cannot do"
  and measurably did not need, reverted with the measurement in the plan.
* **A fast algorithm that is quietly wrong is worse than a slow one.** The
  four-lane FNV was faster than what shipped and would have made the image cache
  accept stale code on one edit in five. Prefer somebody else's algorithm AND
  somebody else's code; check it against a third party's answers.

## Using subagents

The last two sessions used `Workflow` heavily and it paid for itself. Ask agents
to REFUTE a design against the code, not to agree with it. Their best output has
been defects in already-committed work: two memory-safety holes in the image
loader, and two stale standing decisions in `ctbrowser/docs/` that this session's
Boost floor change had invalidated.
