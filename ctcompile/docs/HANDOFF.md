# Handoff: continuing ctcompile

You are picking up work on `ctcompile`, an ahead-of-time browser-application
compiler being built inside `compile-time-browser`. The previous session ran out
of context. Everything below is committed and green; nothing is pushed.

## Where things are

* Repo: `/mnt/c/Users/aange/Downloads/claude/compile-time-browser`
* Branch: **`ctcompile-v1`**, 26 commits ahead of `main`, working tree clean
* Suite: **97/97** via `./tools/remote-build.sh`
* **Read `ctcompile/docs/plans/ctcompile.md` first** — it is the running plan in
  the house style (Done / Next, measured rungs) and it records every decision
  below with its reasoning.

The master plan is 21 markdown files in `../ctcompile-plan/` (NOT in the repo).
`00-START-HERE.md` routes by phase; **`01-objective-and-ground-truth.md`
overrides every other file**. Its six non-negotiables matter: `ctjs` is the
parser only, the bytecode is a REGISTER machine, there is no CI, EmitC is the
primary backend, sources are in `src/`→now `lib/`, build on the devbox.

## What is done

* **Phase -1** — the monorepo split. `ctbrowser/` is the CMake configure root
  (`CMakePresets.json` lives there); `ctcompile/` is a sibling project.
* **Phase 0** — six inventories the build checks, two differential comparators,
  and a recorded startup baseline. See `ctcompile/docs/baseline/*.json`.
* **Phase 2** — the AOT ABI: `ctbrowser/include/ctbrowser/aot/aot_helpers.def`,
  68 helpers over 84 of the 93 opcodes, expanded by both projects so drift is a
  compile error.
* **Phase 15** — a working program image. `ctbrowser/{include,lib}/…/program_image.*`
  writes and reads a compiled `script::program`, validated exhaustively, and
  `browser::set_script_image()` uses it on the page-load path.

## The numbers that justify the project

From `ctbrowser/docs/performance.md`: a whole p5 page load is 17.5% lexing,
15.1% `declare_local`, 7.6% `collect_captured_names` — and **1.4%
`context::run_loop`, the entire interpreter**. About forty percent of a page
load is READING JavaScript; 1.4% is executing it.

So this compiler's value is overwhelmingly in what it **deletes from startup**,
not in what it speeds up at run time. Measured (devbox, `ctcompile/docs/baseline/`):

| | parse+compile | image load |
|---|---|---|
| p5 | 56.9 ms | 14.3 → **5.3 ms** |
| phaser | 79.2 ms | 19.5 → **6.7 ms** |
| babylon | 300.4 ms | 77.5 → **26.6 ms** |

A whole `load_html` of `p5-basic.html` was 70.4 → 34.3 ms (51%) **before** the
loader optimisations — that figure is now stale (see below).

## Do these next, in this order

1. **`image_source_hash` is 4.45 ms for p5 and nobody noticed it was in the page
   path.** It is byte-at-a-time FNV-1a in `program_image.cpp`. Four lanes over
   64-bit words takes it to **0.138 ms** (p5), 0.265 (phaser), 0.349 (babylon) —
   measured by a subagent, prototype was `scratchpad/h3.cpp`, `w64x4`. This is
   the single largest remaining win and it is bigger than anything left inside
   the loader. Keep it little-endian by construction. Changing the algorithm
   must change what an old image hashes to, so add a hash tag to
   `image_fingerprint()` rather than touching `format_version` (the version
   check runs first and would give a misleading message).

2. **Re-measure and re-record `ctcompile/docs/baseline/page-load.json`.** It was
   taken at commit `e4aed22`, before both loader optimisation commits, so its
   51% is out of date. Use `ctcompile/tools/ctpageload`, run from
   `ctbrowser/examples/pages/` (the page's `<script src>` only resolves from
   there). It exits non-zero unless the image was actually used.

3. **Validation is 57% of a load** (3.16 ms of 5.52 for p5), measured by
   building the loader with the operand pass removed. Roughly 40 instructions go
   into checking each bytecode instruction. If you optimise it: a subagent
   prototyped a "suspect" fast path that skips the per-operand switch when a
   function's tables make every index trivially in range, at
   `scratchpad/v_fastval.cpp` — **implement that prototype, not its prose
   description**, which was ambiguous about wide operands (a wide `bx()` must
   not be truncated to `uint16`, and its error messages differ).
   Note: folding validation into the read pass was tried and is **1% worse,
   reproducibly**; it was reverted. Validation is compute, not cache misses.

4. **`function_proto::nested` is dead code** — nothing writes it, and its only
   reader is a check in `p5_ratchet.cpp` that can therefore never fire. The
   image serializes it as a count so the day it starts being filled is a test
   failure. Consider deleting it outright.

## Known problems, not yet acted on

* **The 65,535 proto ceiling is at 49% on a corpus that already exists.** Three
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
  This is what stands between the current win and "bake p5 once, reuse it".
* **`@font-face`** — fixed for `url()` in `a2ef736`, but the style engine still
  records a page font only when the family and url are string tokens elsewhere;
  check before assuming.
* **Phase 16B is blocked on an engine enumerator that now exists**
  (`engine::for_each_rule`), so the CSS comparator can be extended from parse-
  only to compile-checking whenever 16B starts.

## How to work here (learned the hard way)

* **BUILD ON THE DEVBOX, ALWAYS**: `./tools/remote-build.sh` from the repo root.
  The WSL box has 7.5 GiB and has been taken down by local builds twice.
* **The devbox self-deallocates after 30 idle minutes.** When ssh times out:
  `cd ../infra/azure-build-server && ./server.sh start`. It happened four times
  in one session, once mid-measurement.
* **The devbox shell is zsh, which does NOT word-split unquoted variables.**
  `CXX="clang++ -O2"; $CXX foo.cpp` fails as one word. Inline your flags.
* **Chain gates with `&&`, never `;`** — a `;` after `tools/format.sh --check`
  let an unformatted commit through once.
* `tools/format.sh --check` covers the whole monorepo. `.def` files are not
  formatted by it.
* **`compare.py` drives `ctdrive`, not `ctbrowse`**, and binaries now live in
  `build/tools/`, `build/test/`, `build/unittests/`, `build/benchmarks/`.
* **`rsync -az` preserves mtimes**, so restoring a file can leave ninja thinking
  it is up to date. `touch` it.
* **No hardware perf counters on the devbox** (it is a VM) — `perf stat` reports
  `<not supported>` for cycles and instructions. Use callgrind for attribution,
  dhat for allocation, and an A/B of two builds for wall clock. `/usr/bin/time`
  rounds to 10 ms and these loads are ~5 ms, so time inside the probe.
* **Profile the thing itself.** The first callgrind run profiled a binary that
  compiled the program to build the image, and the compile drowned the load.

## The discipline that has been earning its keep

* **The positive case is one line; the negative cases are the file.** A
  comparator that is too lenient does not fail to catch a bad artefact — it
  CERTIFIES one. Every comparator here (`DocumentComparator`,
  `StyleProgramComparator`, `ProgramComparator`) is verified by linking its test
  against a deliberately BLINDED implementation and requiring every negative
  case to go red.
* **Prove a guard is load-bearing by removing it.** Done for both validation
  fixes in `59d0339`; one case failed without its fix and one did not, and the
  commit says so rather than claiming two proven fixes.
* **Silence is not success.** Several tests and tools assert a counter
  (`scripts_compiled_from_source()`, `produced`) rather than trusting the
  output, because a cache that silently misses looks exactly like one that
  works. That guard caught a "58x speedup" that was a loader refusing every
  corpus, and a "-48%" that was a tool guessing the script concatenation wrong.
* **Correct yourself in the record.** Three claims were committed and later
  corrected here (an overstated header term, "the load is memory-bound", and a
  validation rule stricter than the compiler that 23 real p5 functions
  violated). Measurement refuted each; argument did not.
* Commit messages are prose in the repo's voice: state the insight, the
  measurement, and what was NOT done. Read `git log` for the register.

## Using subagents

The previous session used `Workflow` heavily and it paid for itself: the ABI
table, the image format design, and both validation bugs above came from
adversarial fan-outs (propose → refute → consolidate). Ask agents to REFUTE a
design against the code, not to agree with it. Their best output has been
finding defects in already-committed work.
