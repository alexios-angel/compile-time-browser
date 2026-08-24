# Handoff: continuing ctcompile

You are picking up work on `ctcompile`, an ahead-of-time browser-application
compiler being built inside `compile-time-browser`. Everything below is
committed and green; nothing is pushed.

## Where things are

* Repo: `/mnt/c/Users/aange/Downloads/claude/compile-time-browser`
* Branch: **`ctcompile-v1`**, working tree clean
* Suite: **108/108** via `./tools/remote-build.sh`, and **asan 58/58**
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
* **Phase 2** — the AOT ABI, and its gate is met: `ctbrowser/include/ctbrowser/aot/aot_helpers.def`,
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
* **A WHOLE FUNCTION RUNS THROUGH THE ABI.** `unittests/unit/aot_program`
  hand-compiles `total(items, scale)` — a loop, an interned name, a property
  read through a getter, an indexed read, a comparison, both binary families, a
  call back into the interpreter, the failure poll — and checks it against the
  interpreter running the same source, including under forced GC. **That is the
  Phase 12A oracle's shape, working on one function.** It also found the
  safepoint in `context::invoke` sitting before arguments were rooted.
* **Phase 10 — the argument strategy is DECIDED and its experiment passed.** A
  three-way panel chose: Phase 10 normalises calls and each backend materialises
  the arguments, with each parameter's ROLE **derived constexpr from
  `aot_helpers.def`** rather than written down twice. The decisive experiment —
  classify all 69 rows before writing any MLIR — gives **zero unknowns**, and
  `ctcompile_inventories` asserts it.
  **The finding that justifies the whole design:** `uint32_t op_kind` is a
  `ctbrowser::script::op` **bytecode opcode**, not a CTJS enum ordinal —
  `aot_bridge.cpp` does `static_cast<op>(op_kind)`. Passing the CTJS ordinal
  would compile `**` into whatever `op(5)` is. A backend must spell it by name.
* **Phase 10 — started: one conversion pattern, matching on the INTERFACE.**
  `ctjs-opt --ctjs-lower-to-runtime` turns CTJS operations into `func.call`s on
  the real helper symbols, and the pass **names no operation** — which the plan
  calls the acceptance criterion. **Its arity check fired immediately and was
  right**: most helpers are not "frame + operands" (`ct_aot_binary_op` is
  `(fr, op_kind, lhs, rhs, out)` where the kind is an attribute and `out` is an
  out-parameter). A mismatch declines the match rather than failing the module,
  so what it declines is the work list for the rest of the phase.
* **Phase 9 — THE IMPORTER WORKS AND ITS GATE IS MET.** Real bytecode functions
  translate into CTJS MLIR: **p5.js imports 3,200 of its functions and phaser
  6,069**, and both modules verify. `ctjs-translate --ctbrowser-js-to-ctjs f.js`
  is the fastest way to see it; `--ctbrowser-bytecode-to-ctjs` takes an image.
  What still refuses is counted, not guessed: `closure` 556 (needs a producer
  for `!ctjs.program`), `gather_rest` 173, `iterable` 117, `make_arguments` 111.
* **Phase 8 — the CTJS dialect exists: 36 operations in ODS**, round-tripped,
  every verifier diagnostic tested under `-verify-diagnostics`, docs building.
  The operations name real `ctbrowser::aot::helper_id` enumerators through
  `CTJS_RuntimeOp`, so **an operation cannot claim a helper the runtime does not
  declare** — which is what ties the dialect to the ABI Phases 2–6 built.
* **Phase 7 — MLIR is stood up and its gate is met.** `ctjs-opt` and
  `ctjs-translate` build and run, the CTJS dialect's five types round-trip, and
  the lit suite runs as ctest #108 so the gate this repo actually uses covers
  it. **MLIR is not built by default and must not be**: `CTCOMPILE_ENABLE_MLIR`
  is OFF, and a runtime-only configure was verified WITH MLIR installed, which
  is the case that matters.
  To build it: `-DCTBROWSER_ENABLE_PROJECTS=ctcompile -DCTCOMPILE_ENABLE_MLIR=ON
  -DCMAKE_PREFIX_PATH="/home/linuxbrew/.linuxbrew;/home/linuxbrew/.linuxbrew/opt/llvm"`.
* **Phase 6 — the throwing tier works.** `ct_aot_catch_land` was recorded in the
  ABI as **unimplementable as written**, found by trying, with two possible
  fixes written down and neither taken "without a compiled `try` to test it".
  `unittests/unit/aot_throw` is that `try`, and the fix is a third:
  `call_frame::landed_slot`, which keeps the helper's signature. `CT_AOT_PAD_BIT`
  is defined now too, with the measurement `aot.hpp` was waiting for.
* **Phase 5** — in progress, and further than it looks. **29 of the ABI's 69
  rows have bodies** (was 4), including the interned-name pool the whole
  property family was blocked on. Two real extractions with the plan's discipline —
  `context::binary_op_static` and `context::binary_op`, the fourteen binary
  operations, one commit each with the suite green — and sixteen rows that
  needed no extraction, only a shim over a function the runtime already had.
  The flags-consistency test the plan asks for is in `Inventories.cpp`.
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
3. **FINISH PHASE 10 FROM THE DECIDED DESIGN.** The next steps, in order, are in
   the panel's verdict: an `OpcodeMapping.hpp` giving `BinaryKind ->
   script::op` spelled by name (never a literal); a `ctjs.runtime_call`
   operation carrying the helper, its role vector and its literals, with a
   verifier that the roles consume the operands and literals exactly; then the
   EmitC slice — `!ctjs.value -> !emitc.opaque<"ctbrowser::script::value">`,
   out-parameters as `emitc.variable` plus `emitc.apply "&"`, and the status
   compared against `ct_aot_status::ok` **by name**, never a baked number
   (`aot.hpp` says outright "THE PRECEDENCE IS THE CONTRACT; THE NUMBERS ARE
   NOT"). Note `ct_aot_enter` fails with a NULL POINTER, not a status.
4. **WIDEN THE IMPORTER.** The importer's refusals are
   counted in `ctjs.skipped` and printed as warnings, so the work list writes
   itself — run it over a corpus and read the histogram. `closure` is the
   largest single item and needs a producer for `!ctjs.program`, which is a
   design question rather than a mapping.
   **Handlers are NOT imported yet**: `push_handler`/`pop_handler` map to
   operations but the importer has no handler-stack reconstruction, so any
   function with a `try` is refused. The design for it is in the Phase 9 brief —
   abstract interpretation over the CFG with a stack of push offsets, since
   there is **no handler table** in `function_proto`.
4. **The rest of Phase 5, and Phase 6.** Phases 1–4 are done and their gates
   are met; Phase 5 is 26 of 69 rows.
   **`ct_aot_intern_name` is the one hard blocker on the path to a minimal
   compiled function.** Every property helper's key is a `const ct_aot_name *`,
   the row asks for an owning immortal pool that does not exist, and
   `lookup_property` today takes a `const std::string &`. Until it exists,
   `o.x` cannot be emitted at all — which is why `ct_aot_get_index` is
   implemented and `ct_aot_get_prop` is not.
   After that, the cheapest real extractions per opcode bought are
   `ct_aot_cell_get`/`ct_aot_cell_set` (four opcodes for eight lines, and they
   unblock every captured variable). Leave `ct_aot_construct` (~90 lines),
   `ct_aot_instance_of` (~56) and `ct_aot_set_index` (~36) until last.
   PREVIOUSLY: **Phases 4, 5 and 6** / **Phases 1–6**, the runtime preparation. Phase 2's gate is MET as of
   2026-08-22 — `ctbrowser/lib/Script/aot_bridge.cpp` has four helper bodies and
   `unittests/unit/aot_basics` calls a hand-authored compiled function from
   interpreted JavaScript. Doing it falsified `ct_aot_catch_land`, which cannot
   be implemented as written; the row says so now. The throwing tier and Phases
   1, 3–6 are still open. WAS: Phase 2's TABLE is done and its GATE is not — nothing has ever called a hand-authored AOT function through
   the ABI, which is the cheapest way to find out whether 1,881 lines of
   contract are right before 68 helper bodies depend on them.

## Known problems, not yet acted on

* **lit LIVES IN A VIRTUAL ENVIRONMENT.** brew's llvm bottle ships FileCheck but
  no llvm-lit, and both Ubuntu's python and brew's refuse `pip install` under
  PEP 668. `python3 -m venv ~/.lit-venv && ~/.lit-venv/bin/pip install lit`, and
  `tools/Brewfile` says so where somebody provisioning a box will read it. With
  no lit, `check-ctcompile` reports that it is unavailable rather than silently
  running nothing.
* **THE ABI TABLE'S LINE CITATIONS ARE SYSTEMATICALLY STALE.** Every row cites
  the runtime that owns its semantics by file and line; Phases 3–5 moved several
  hundred lines of `run_loop.cpp`, `call.cpp` and `vm.hpp`. Six citations
  pointed past the end of a file and are repaired **as names**;
  `ctcompile_def_citations` keeps that class out. **Many more still land inside
  their file while naming a handler that has since moved**, and no machine can
  see that. If you follow a citation and find something else, the row is stale
  rather than wrong about the semantics — the claims were checked, the addresses
  were not re-checked afterwards. Cite by name in anything you touch.

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

## Phase 10: what was decided, and what was refuted

**`ctjs.runtime_call` was designed, reviewed and NOT BUILT.** A three-lens
adversarial panel refuted it against the checkout. Its only novel content was
carrying the helper as a string, which converts the project's one *build-error*
ABI check — `CTJS_RuntimeOp` concatenates the name into a `helper_id`
enumerator — into a pass-time lookup. Worse, an `OpInterfaceRewritePattern` that
matches every implementer and *produces* an implementer re-matches its own
output until the iteration cap. The role walk it existed to hold is right and
belongs in a header both backends call, not in an IR node.

**The role table now reads the ABI's *failure tier*, not just `may_throw`.**
37 rows declare `may_throw` and only 24 return a status; the rest fail in the
RAISE tier, where the result is always well-formed and the caller polls
`ct_aot_failed` at back-edges. `ct_aot_enter` is in neither tier — it returns
NULL. Emitting a status test after `ct_aot_new_object` tests nothing.

**Two committed checks were wrong and are corrected.** `classify_return` missed
`ct_aot_to_int32`, the row the `.def` exempts by name ("a signed int32 return
that is DATA, not a status"); the mechanical tell is that it takes no frame
handle, and it is the only int32_t row that does not. And `values_only` admitted
the out-parameters it claimed to exclude, because `"uint64_t *out"` starts with
`"uint64_t "` — six rows passed a check whose comment said they could not.

**The shape trait found four live defects the moment it existed.**
`CTJS_ABIShaped` compares every runtime operation's ODS declaration against its
helper's row. It caught `load_upvalue`/`store_upvalue` (an `$index` attribute
against a helper with nowhere to put it — every captured-variable read compiled
to `undefined`), `instanceof` (a `!ctjs.value` result against a `uint32_t` 0/1),
and `delete_property` (a result against a helper that answers with a status).
**There is deliberately no operand-count rule**: the dialect is higher-level
than the ABI, so supplying *fewer* arguments is normal and only excess is
checkable.

**The EmitC entry shape is pinned and compiles.**
`test/Lowering/EmitC/entry-shape.mlir` is the target, not any pass's output.
Callees must be **qualified** (`ctbrowser::aot::ct_aot_*`) because the
`extern "C"` prototypes live inside that namespace — the table's `symbol` is the
LINKER name, not the callee string. `emitc.call_opaque` emits no declaration, so
the TU just includes `aot.hpp`; `emitc.declare_func` is broken in this LLVM
(drops parameter types). `--declare-variables-at-top` is mandatory, because the
NULL test gives every body two blocks.

**THE PIPELINE IS CONNECTED.** `echo 'function f(a) { return a; }' |
ctjs-translate | ctjs-opt --ctjs-lower-to-emitc | mlir-translate --mlir-to-cpp`
produces a translation unit that compiles against the real `aot.hpp`.
`test/Lowering/EmitC/end-to-end.mlir` runs all four stages and the last one is a
C++ compiler. The backend can barely do anything - it refuses almost every
function and records why as `ctjs.not_lowered` - and that is the point: every
operation added from here is an increment on something that demonstrably works.

**Three runtime facts the backend had to be told, none guessable from the IR:**

* **`argv` dies at `ct_aot_enter`.** It is an interior pointer into
  `context::registers_` and `enter` resizes that vector. Parameters are read
  before the call; `ct_aot_slots` cannot recover it, since that hands back the
  compiled frame's own span rather than the caller's window.
* **`new.target` and the callee cannot be delivered at all.** The importer
  prepends three implicit arguments and only `receiver` is in the entry
  signature. `ct_aot_new_target` and `ct_aot_callee` are declared in `aot.hpp`
  and **defined nowhere** — a call to either is a link error. Two more gaps sit
  behind that: `ct_aot_enter` never sets `call_frame::closure`, so `callee`
  would answer `undefined` anyway, and nothing sets `pending_new_target_` on the
  compiled `new C()` path.
* **`--mlir-to-cpp` miscompiles a parallel copy on a block-argument edge** in
  LLVM 22.1.8 — measured by compiling and running it, see
  `block-argument-hazard.mlir`. The importer's register file *is* block
  arguments, so this is every function with a loop that permutes two registers.
  Non-entry block arguments must become `emitc.variable`, reads before writes.
  Until that exists the backend refuses any function with more than one block.

**Smaller things worth not rediscovering:** `--declare-variables-at-top` is
mandatory (EmitC refuses multi-block functions without it) and it declares
every value at the top, so a `const` local is a build error — the argv pointer
is cast once instead, because the signature must stay assignable to
`ct_aot_entry_fn`. `$` in a symbol compiles only as a GCC/Clang extension. And
`%cxx` in `test/lit.cfg.py` is what makes the compile step available to any
EmitC test.

**Control flow compiles now too.** `function g(a) { if (a) { return 1; } return
2; }` reaches a translation unit that compiles. The pipeline is
`ctjs-opt --ctjs-lower-to-emitc --emitc-eliminate-block-arguments`, and **that
order is a correctness requirement**: the first pass emits block arguments, the
second removes them, and what reaches `mlir-translate` must have none.

`--emitc-eliminate-block-arguments` gives each non-entry block argument an
`emitc.variable`, reads it at the top of its block and writes it on each
incoming edge — so every read precedes every write. **Edges are split rather
than assigned in place**, because `cf.cond_br %c, ^B(%x), ^B(%y)` is legal and
carries different values into one block; assigning both sets before the branch
runs both on whichever path is taken. In-place assignment is correct for every
single-successor terminator, which is exactly why the swap test does not catch
it — that case has its own function.

**Number constants are spelled from bits**, never as a decimal literal:
`value::number(std::bit_cast<double>(UINT64_C(...)))`. The attribute carries the
double's bit pattern precisely because `-0.0` and NaN payloads do not survive a
decimal round-trip, and printing decimal would discard that at the last step.

**The out-parameter/status pattern is done**, which is the shape most of the ABI
has. `status_call()` writes it once: a local for the result, its address, the
call, a test against `ct_aot_status::ok` **by name**, and a block split so the
result is loaded only on the surviving path — which the row requires, not merely
permits (`*out` is written only on `CT_AOT_OK`). The failure edge is shared per
function and **tests for `unwound` before leaving**: on that status the unwinder
has already destroyed this frame, so an unconditional `ct_aot_leave` pops
somebody else's.

`a + b`, `!a`, `+a`, `void a`, `a === b`, `a == b` and the four relational
operators all compile now.

**A GAP IN THE ABI, worth knowing before designing against it:** *no row boxes a
machine quantity into a JavaScript value.* `ct_aot_strict_equals` returns a
`uint32_t`, `ct_aot_compare` an `int32_t` ordering, `ct_aot_to_number` a
`double` — and there is no `ct_aot_from_bool` and no `ct_aot_from_double`. In
C++ that boxing is `value::boolean(b).bits()`, a **member call on a temporary**,
which `emitc.call_opaque` cannot spell because its entire output is
`callee(args)`. The backend therefore emits two `static inline` shims into its
own translation unit rather than adding rows to a runtime ABI for a compiler's
convenience. If the ABI ever grows those rows, the shims go.

**The relational operators are not negations of one another.** `ct_aot_compare`
can answer `unordered` — a NaN on either side — which makes all four false,
`>=` included. `a >= b` as `!(a < b)` makes `NaN >= NaN` true. Each is built
from equality tests against the orderings that make it true. Unlike the status
enum, **the ordering's numbers are contractual** and `aot.hpp` says so.

**COMPILED VALUES ARE ROOTED IN THE FRAME, and this was a real shipped bug.**
`a + b + c` kept the first addition's result in a plain C++ local across the
second `ct_aot_binary_op`, which is a safepoint. The collector is precise; a
value in a native frame is reachable from nothing. Under `set_gc_stress` the
compiled body returned six characters where the interpreter returned
sixty-five, and ASan called it a heap-use-after-free. **Without stress it was
correct every time**, which is why every other test passed.

The tell was an inconsistency in our own file: it refused string constants and
`typeof` because "ct_aot_new_string is a safepoint, and nothing roots the result
yet" — while admitting six operations with exactly that property.

Every produced value now goes into a frame slot immediately, and **the span is
re-fetched at every store**: the row says the pointer "IS VALID UNTIL THE NEXT
SAFEPOINT AND NOT ONE INSTRUCTION LONGER". Storing once suffices because the
collector marks and deletes rather than moving. Slots are never reused — a leak
bounded by the frame beats a liveness analysis that is wrong once.

**`ctcompile_gc_roots` is the only test that runs generated code against the
real runtime with the collector hostile**, and it is the only kind that can see
this class of defect: a use-after-free nothing collects is invisible, because
the freed memory still holds the right bytes. It compiles `gc-roots.js` through
the real pipeline at build time. Removing the parking makes it report 6
characters against 65 while "collector idle" still passes.

**Two ordering hazards are now refused rather than documented.**
`--emitc-eliminate-block-arguments` walks `emitc.func` only, so run *before* the
lowering it silently does nothing and the block arguments reach `mlir-translate`
— measured: `sl(10,20,2)` answers 20 where 10 is correct, every tool exiting 0.
It now refuses to run when a `ctjs.func` remains. And `ctjs.frame_exit` must be
the last thing before the return, or the shared failure path leaves the frame
twice (harmless — `leave` truncates to its own index — but unchecked).

**Next**: property access and calls, which is where the Phaser corpus starts —
`ctjs.get_property` needs an inline cache, and `ctjs.call` needs an argv span
marshalled into the frame's **GC-rooted slots**, so it is the first operation
that cannot be done without `ct_aot_slots`. That is also the point at which
rooting stops being deferrable: everything compiled so far keeps its live values
in C++ locals, which the precise collector cannot see.

## Using subagents

The last two sessions used `Workflow` heavily and it paid for itself. Ask agents
to REFUTE a design against the code, not to agree with it. Their best output has
been defects in already-committed work: two memory-safety holes in the image
loader, and two stale standing decisions in `ctbrowser/docs/` that this session's
Boost floor change had invalidated.
