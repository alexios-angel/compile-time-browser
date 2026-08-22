# Handoff: continuing ctcompile

You are picking up work on `ctcompile`, an ahead-of-time browser-application
compiler being built inside `compile-time-browser`. Everything below is
committed and green; nothing is pushed.

## Where things are

* Repo: `/mnt/c/Users/aange/Downloads/claude/compile-time-browser`
* Branch: **`ctcompile-v1`**, working tree clean
* Suite: **103/103** via `./tools/remote-build.sh`, and **asan 55/55**
* **`ctcompile/docs/ctcompile.md` is the tool's own documentation** — the CLI,
  what it refuses and why, the manifest, and the gaps.
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
* **Phase 1** — the product. CLI documented in `ctcompile/docs/ctcompile.md`,
  a JSON manifest (`--manifest`, and a copy inside every bundle), stable program
  identities (`program_id` is the source hash the runtime matches on), and both
  format versions exposed rather than copied. `--mode` declares Phase 1's three
  modes and refuses the two that need code generation.
* **Phase 3** — mixed-mode dispatch, centralised. `script/dispatch.hpp` is the
  one read of `aot_entry` in the engine and carries the six transition
  counters; `unittests/unit/aot_dispatch` asserts all six, because every arm
  returns the same answer whether it dispatched or not. Before it, `aot_entry`
  was read at ONE line - `op::call` - so a compiled body was reachable from
  interpreted JavaScript and from nowhere else: not from `context::call` (every
  DOM event, timer, promise job and `apply`), not from `new`, and not from a
  program's top level, which for ctcompile is the ordinary case.
* **Phase 4** — AOT GC shadow frames. `context::set_gc_stress` collects at every
  safepoint, which is the only way the ABI's `is_safepoint` obligations can be
  exercised at all: nothing collects while script runs in an ordinary build.
  **It found two real use-after-frees on `new` the first time it ran**, neither
  in code this phase wrote — see the plan. `ct_aot_slots` is the ABI row a body
  needs to keep a value where the collector can see it, and `context::rooted` is
  the general "a C++ scope is holding this across a call" mechanism.
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

## There is an MVP, and it works

```
ctcompile app/ -o myapp        then ./myapp
```

`ctcompile` loads the entry page once with the engine that will run it, asks
that engine which scripts it compiled and which resources it reached for,
compiles each classic `<script>` to a program image, packs page + resources +
images into a bundle, and appends the bundle to a copy of `ctrun`, a fixed
launcher built like any other tool. The output is one executable. Nothing is
generated and no linker runs — a linked ELF does not care what follows its last
section, so packaging is a file copy plus a trailer.

Measured on the devbox, p5-basic.html, seven runs each, whole-process wall clock
including startup and rendering a frame:

| | ms |
|---|---|
| `ctbrowse p5-basic.html`, reading the JavaScript | **78.0** |
| the packaged executable, run from `/tmp` | **47.3** |

That is the honest end-to-end figure, and it also settles a reasonable
objection: the packaged binary is 15 MB against ctbrowse's 3 MB, because
`this_executable_bytes()` reads the whole launcher back at every start to find
its own trailer. Reading 12 MB more still wins by 30 ms.

**IT IS VALIDATED BY COMPARING RENDERS, not by exit codes.** Seven example
pages package and run; six render byte-identically to the same page loaded from
source, and the seventh did too once a real defect was fixed. That comparison is
the only thing that found the defect, and it is now `ctcompile_package`'s last
arm:

> A packaged application is SEALED - it answers from what it carries and never
> from the disk - and the vendored OFL faces are loaded THROUGH the asset
> registry. So the first sealed build silently dropped to the bitmap font.
> Exit 0, rendered, looked worse. The packager now asks for the faces the way
> `run_app` does, which puts them in `requested()`, and records the DIRECTORY in
> the bundle because it is part of the registry key.

The test took two tries to mean anything, which is worth remembering: the first
version compared two bitmap-font runs (everything in that file sets
`CTBROWSER_FONTS=font8x8`), and the second still passed with the fonts blinded,
because the packaged arm inherited `CTBROWSER_FONT_PATH` and found the faces
under the names the packaging machine had recorded. **The packaged arm is now
given nothing** - no font path, and a working directory that is not the
application's, which is what "copy it and run it" means.

**WHAT IT DOES NOT DO IS GENERATE NATIVE CODE.** The bytecode still runs on the
interpreter. This deletes the *parse*, which is ~40% of a page load; the
interpreter is 1.4%. Phases 7–12A are the rest and are not started.

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

12. **The MVP above**, and then an adversarial review of it that found eight
    defects in the packaging path — every one of them SILENT, in the sense that
    the application ran and produced the right document:
    * **module scripts were invisible.** `script_sources()` lists classic
      scripts only and there is no image path into `load_module`, so a page of
      modules packaged as "0 scripts compiled" and the guard that asks whether
      packaging worked read a truthful, useless zero. `module_sources()`
      publishes them now; the packager refuses them and so does the launcher.
    * **the guard was gated on "some images arrived"**, so the case where NONE
      arrived — the most obviously broken package there is — was the one case it
      skipped.
    * **the probe never ticked the page.** `fetch` and `img.src` queue their
      requests and are drained from `tick`; p5 loads in `preload` and Phaser in
      the first game step. Every sprite, atlas and level was missed with no
      warning. It now runs the page until it stops asking (ceiling 60 frames);
      p5-basic settles after one.
    * **the packager resolved assets through a second, base-less registry**
      whose probe order differed from the one that answered the page — the exact
      second copy of the rule `assets.hpp` spends a paragraph forbidding.
    * **a packaged application fell back to the filesystem**, probing the
      working directory first, so a missing resource was answered by whatever
      sat next to the user. Registries can be SEALED now, and `run_bundle` does.
    * `read_bundle` bounded each blob and not the total; `bundle_write_error()`
      was a channel nothing ever wrote to, behind a header promising a check
      that was never implemented.

    All six new guards were removed one at a time and watched going red, each
    for its own message. The one that could NOT be falsified is `write_bundle`'s
    refusal of >4G entries or a >4G name — reaching it needs a bundle no machine
    here can hold. It is written and untested, and that is better said than
    implied.

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
3. **Phases 5 and 6**, which are what is left of the runtime preparation:
   extracting the shared JavaScript runtime helpers (a refactor with a strong
   invariant — the VM's observable behaviour must not change, one helper per
   commit) and explicit completion semantics. Phases 1–4 are done and their
   gates are met.
   PREVIOUSLY: **Phases 4, 5 and 6** / **Phases 1–6**, the runtime preparation. Phase 2's gate is MET as of
   2026-08-22 — `ctbrowser/lib/Script/aot_bridge.cpp` has four helper bodies and
   `unittests/unit/aot_basics` calls a hand-authored compiled function from
   interpreted JavaScript. Doing it falsified `ct_aot_catch_land`, which cannot
   be implemented as written; the row says so now. The throwing tier and Phases
   1, 3–6 are still open. WAS: Phase 2's TABLE is done and its GATE is not — nothing has ever called a hand-authored AOT function through
   the ABI, which is the cheapest way to find out whether 1,881 lines of
   contract are right before 68 helper bodies depend on them.

## Known problems, not yet acted on

* **A `<script src>` that ships its source TWICE.** `write_image` defaults to
  `keep_source` and `ctcompile` takes the default, so p5.js is 4.5 MB as an
  `asset` (which the run-time walk must re-read to reproduce the hash) and again
  inside its 7.3 MB image. Dropping the source is not free — it is whether
  `f.toString()` returns the text or `[native code]`, and p5's own error system
  reads it — so this is a real decision, not an oversight to tidy.
* **`ctrun` ignores `argv` once a bundle is appended.** `myapp --help` silently
  starts the application.
* **`this_executable_bytes()` is `/proc/self/exe` only**, so a packaged
  application on Windows finds no bundle and prints usage. The cross build
  exists; this half of it does not.
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
* **`aot_gc` PROVES MUCH LESS OUTSIDE `asan`.** It asserts correct answers under
  forced GC in every build, but a rooting bug is a use-after-free, and reading
  freed memory usually returns the right bytes. Every one of its guards was
  falsified under `asan`, and that is where a regression in them will show.
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
