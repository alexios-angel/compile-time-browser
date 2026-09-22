# Full monorepo validation, 2026-09-21–22 UTC

The user explicitly requested this full run. The tested source was clean
`ctcompile-v1` commit **a1d6680d8a8d2ab7da6cdf891bf8d93ef170790b**. No implementation,
test expectation or golden was changed. The build succeeded, but the regression
suite is **not green**: **321/324 CTests passed**, including **217/217 browser**
and **104/107 compiler** tests. The compiler lit registration contains **406**
discovered cases: **328 passed, 27 failed and 51 unresolved**.

The build and all seven test stages finished from **2026-09-21 22:22:22 UTC**
to **2026-09-22 01:08:48 UTC**, **2h46m26s** total. The devbox stayed up throughout,
and its idle shutdown timer was restored after the final stage. Full cached
test262 and WPT measurements completed and exposed **seven crashes** in total.

| Stage | UTC start | UTC finish | Wall time | Exit |
| --- | --- | --- | ---: | ---: |
| ctest | 2026-09-21T22:27:02Z | 2026-09-21T23:13:54Z | 2812s | 8 |
| test262-selftest | 2026-09-21T23:13:54Z | 2026-09-21T23:14:00Z | 6s | 0 |
| test262-gate | 2026-09-21T23:14:00Z | 2026-09-21T23:14:01Z | 1s | 1 |
| wpt-selftest | 2026-09-21T23:14:01Z | 2026-09-21T23:14:12Z | 11s | 0 |
| wpt-gate | 2026-09-21T23:14:12Z | 2026-09-21T23:14:23Z | 11s | 1 |
| test262-full | 2026-09-21T23:14:23Z | 2026-09-21T23:19:09Z | 286s | 0 |
| wpt-full | 2026-09-21T23:19:09Z | 2026-09-22T01:08:48Z | 6579s | 0 |

Stage wall times include discovery/setup and are rounded to whole seconds by
the launcher. Internal runner timings below exclude some of that setup.

## Scope and environment

All compilation and execution ran on the devbox under the shared build lock.
`tools/remote-build.sh all` completed **776 build actions**, from
**2026-09-21 22:22:22 UTC to 22:27:01 UTC** (4m39s). The existing default Release
configuration enabled MLIR, ANGLE and mimalloc. The devbox had eight CPUs and
31 GiB RAM, Ubuntu 24.04.5, CMake/CTest 3.28.3 and lit 18.1.8. The build used
Clang 24.0.0git at `e3986d2253e4cf600d9c55badda4ddb0ec0f0ce2`; native fixture
compilers also included GCC 13.3.0 and Clang 18, with Node 26.8.1 as an oracle.

The default CTest inventory had **324 tests, none disabled or skipped**. Full
compiler lit ran inside CTest with eight workers and its registered 5,400-second
limit. The browser and compiler corpus/matrix tests registered there were included.
WPT and test262 are opt-in CTest registrations, disabled in this configuration;
their harness self-tests, expectation gates and cached corpus measurements were
therefore run separately after CTest.

This was the default Linux configuration, not separate Windows, ASan or TSan
builds. Individual native fixtures invoked their own sanitizers where execution
reached those checks. No additional unregistered benchmark matrix was run.
Full WPT here means **all nine cached sparse roots listed below**, not every test
in upstream WPT. Harness skips remain skips, not passes.

The source submodule pins were:

- compile-time-css: `164c390486e0f4dccf21c717a07a906e1716b819`.
- compile-time-javascript: `d2664e9ab97e34f451287bb39724b900d316f561`.
- its compile-time-containers: `e122a6a4a3a81a61709378d7eaef6c2aa96037d2`.

## CTest failures

CTest ran from **22:27:02 to 23:13:54 UTC** on September 21, reporting
**2,811.14 seconds** total and exit **8**. All browser tests passed. These are the
three failing compiler registrations:

| Test | Seconds | Observed failure |
| --- | ---: | --- |
| `ctcompile_lit` | 2561.71 | 27 failed cases and 51 unresolved fixtures, detailed below. |
| `ctcompile_owned_global_shared_map` | 249.40 | Numeric source/prepared row 33 violates the expected non-Number refusal; DOM source/prepared row 6 also violates unused-input provenance and failed-proof evidence assertions. Ten `FAIL` assertions in total. |
| `ctcompile_host_contract_seeded_maps` | 50.34 | Numeric source/prepared row 33 violates `non-Number conversion cannot borrow a numeric method-result category`; two `FAIL` assertions. |

The two C++ Map tests reach their remaining budget sweeps before exiting with
failure. The DOM ownership assertions include complete source-owner revalidation
and suppression of storage/declaration evidence after a failed proof. These need
semantic review before changing expectations; this measurement does not establish
whether each changed admission is valid.

### Compiler lit

Lit completed in **2,561.62 seconds**, without hitting its timeout. The **51
UNRESOLVED** entries are `CTNative/Lowering/Objects/Inputs/class-initialization/01.mlir`
through `51.mlir`: each reports **“Test has no 'RUN:' line”**. These are split input
fixtures discovered as tests; the lit configuration does not exclude `Inputs`.

All **27 FAIL** cases follow. Paths are relative to `ctcompile/test/`. The table
records the first observed failure, not an unverified root-cause diagnosis.
Several expectations visibly retain earlier raw C++ carrier types, diagnostic
text or refusal behavior; that does not justify automatically updating the rest.

| Case | First observed failure |
| --- | --- |
| `Analysis/Escape/escape-claims/invariant-string-index.test` | Expected stored-object row 9; diagnostic output instead includes row 10. |
| `Analysis/Escape/escape-claims/invariant-string-length.test` | Expected stored-object row 8; output instead includes row 9. |
| `Analysis/Escape/escape-claims/sub-snapshot.test` | Expected object row 5 to escape via storage; output says confined. |
| `CTJS/IR/exceptions.mlir` | Expected `i1 -> f64`; emitted signature uses `js_boolean_t -> js_num`. |
| `CTNative/Exports/native-intrinsic-symbols.test` | `division-index-nested-expression`, optimization disabled: DOM property read lacks a proved receiver and supported member. |
| `CTNative/Lowering/Admission/global-undefined.mlir` | Expected mixed-string global-store refusal text is absent. |
| `CTNative/Lowering/Admission/refusal-call-graph.mlir` | Expected retained `ctjs.func` is absent after lowering. |
| `CTNative/Lowering/Admission/refusal-return-carrier.mlir` | Expected mixed nullable-return function is absent from the checked output. |
| `CTNative/Lowering/Maps/maps.mlir` | Expected optional-result refusal retains `ctjs.func`; output has an EmitC declaration. |
| `CTNative/Lowering/Maps/native-map-presence/01-guards.mlir` | Expected nested-Map presence refusal is absent. |
| `CTNative/Lowering/Maps/native-map-presence/02-size.mlir` | Expected nested-Map presence refusal is absent. |
| `CTNative/Lowering/Maps/native-map-presence/03-mutations.mlir` | Expected nested-Map presence refusal is absent. |
| `CTNative/Lowering/Objects/array-borrow.mlir` | `original.True`: source did not lower completely. |
| `CTNative/Lowering/Objects/class-dom.mlir` | `class_utf16_char_missing-False`: refused proof emitted or lost a source function. |
| `CTNative/Lowering/Objects/class-initialization.mlir` | `class-map-inherited-chain`: work budget exhausted at `max-steps=4441`. |
| `CTNative/Lowering/Objects/object-string-fields.mlir` | Expected mixed number/string call-result refusal text is absent. |
| `CTNative/Lowering/Objects/receiver-refusals.mlir` | Expected Boolean field `i1`; output uses `js_boolean_t`. |
| `CTNative/Lowering/Scalars/optional-scalars.mlir` | Expected retained `ctjs.func`; output has an EmitC declaration. |
| `CTNative/Lowering/Scalars/string-arithmetic.test` | Expected String conversion refusal; conversion is lowered to EmitC. |
| `CTNative/Lowering/Scalars/string-coercions.test` | Expected String conversion refusal; conversion is lowered to EmitC. |
| `CTNative/Optimization/default-optimizations.mlir` | Expected `f64` return; output uses `js_num`. |
| `CTNative/Ownership/global-maps-recorder.test` | Appended lifetime driver calls typed `js_string` parameters with raw string literals/`std::string`; C++ compilation fails. |
| `CTNative/Ownership/global-maps-umd.test` | Lifetime driver expects `std::string` callable signature and raw-string arguments; typed-carrier compilation fails. |
| `CTNative/Ownership/global-maps.test` | `seeded_deleted-default`: expected 0/5 native functions, observed 5. |
| `CTNative/Ownership/global-methods.test` | `ordinary/explicit`: missing expected standalone owning table/callable carriers. |
| `CTNative/Ownership/globals.test` | Lifetime driver assigns/compares raw integers against `js_num`; C++ compilation fails. |
| `CTNative/Ownership/native-data-session.test` | Lifetime driver supplies raw string literals to `js_string` parameters; C++ compilation fails. |

A failed lit case can stop before its later RUN lines or matrix arms. This is a
complete suite invocation, not a claim that those later checks passed.

## Conformance harnesses and gates

Both harness self-tests passed: **test262 11/11** planted outcomes and **WPT 5/5**.

The **test262 gate exited 1**: **zero regressions and 166 newly passing** tests
against its checked-in expectations. Its 341 files classified as 321 PASS,
17 FAIL and three SKIP. The newly passing expectations were left unchanged.

The **WPT gate exited 1** with **22 unexpected failure signatures across two
files**, not 22 separate failing files. Its 48 plans classified as 43 PASS,
one FAIL, one TIMEOUT and three SKIP. Subtests: 381 PASS, 18 FAIL, one TIMEOUT,
one NOTRUN.

- `dom/events/Event-dispatch-single-activation-behavior.html`: 18 failed
  activation subtests involving AREA or nested form controls; expected activation
  arrays differ from the observed results.
- `dom/events/Event-dispatch-throwing-multiple-globals.html`: timeout and one
  unrun subtest for delivery of listener exceptions to the listener's global.

## Full cached test262

Corpus pin: `771005236e88a909635104e03ba12559688c0172`. The full `test/` selection
completed in **261.9 seconds**, with four workers, a 10-second per-process timeout
and a 2,048 MiB address-space cap. Counts are per test file, using the worst
outcome across its requested modes.

| Selection | Total | PASS | FAIL | TIMEOUT | CRASH | SKIP |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| annexB | 1086 | 781 | 262 | 0 | 1 | 42 |
| built-ins | 23812 | 17767 | 5274 | 1 | 0 | 770 |
| harness | 116 | 112 | 4 | 0 | 0 | 0 |
| intl402 | 3357 | 0 | 0 | 0 | 0 | 3357 |
| language | 23726 | 22305 | 1399 | 1 | 0 | 21 |
| staging | 1483 | 1018 | 447 | 3 | 2 | 13 |
| **Total** | **53580** | **41983** | **7386** | **5** | **3** | **4203** |

**41,983/49,377 executed files passed (85.0%)**. The largest normalized failure
cause was missing `Temporal` (4,115 occurrences in the leading cause group).
Skips include ECMA-402 (3,357), Atomics/SharedArrayBuffer, cross-realm support and
IsHTMLDDA. Zero HOST errors were reported. The full measurement command exits
zero when it completes; that exit does not mean conformance passed.

All three crashes, in sloppy mode, require follow-up:

| Test, relative to corpus `test/` | Result |
| --- | --- |
| `annexB/built-ins/Function/createdynfn-html-open-comment-body.js` | SIGSEGV (`-11`) |
| `staging/sm/String/replace-math.js` | SIGABRT (`-6`) |
| `staging/sm/class/methDefnGen.js` | SIGSEGV (`-11`) |

The five 10-second timeouts were:

- `built-ins/Array/prototype/reduceRight/length-near-integer-limit.js`.
- `language/expressions/dynamic-import/await-import-evaluation.js`.
- `staging/sm/Array/to-length.js`.
- `staging/sm/destructuring/array-iterator-close.js`.
- `staging/sm/regress/regress-1507322-deep-weakmap.js`.

No crash stack or root cause was established by this measurement, and no
separate diagnostic rerun or repair was performed.

## Full cached WPT

Corpus pin: `3f6b09ae3ed55280074645ce38e9002f52fc60a8`. The nine cached roots
produced **8,895 plans: 6,416 runnable and 2,479 skipped**. The runner measured
**6,570.4 seconds** (1h49m30.4s), with four workers and a 4,096 MiB per-driver
address-space cap. Metadata selects 10- or 60-second deadlines, plus the runner's
cleanup margin. The engine ran headlessly with deterministic graphics, network
disabled and the cached corpus as its document root.

| Root | Total | PASS | FAIL | TIMEOUT | CRASH | HARNESS_ERROR | SKIP |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| css | 4414 | 1599 | 1111 | 25 | 0 | 191 | 1488 |
| custom-elements | 193 | 70 | 106 | 1 | 0 | 3 | 13 |
| dom | 654 | 386 | 94 | 17 | 2 | 5 | 150 |
| domparsing | 72 | 17 | 42 | 0 | 0 | 10 | 3 |
| encoding | 1301 | 26 | 836 | 394 | 2 | 3 | 40 |
| html | 1684 | 706 | 336 | 76 | 0 | 31 | 535 |
| selection | 183 | 41 | 46 | 3 | 0 | 6 | 87 |
| shadow-dom | 345 | 66 | 101 | 12 | 0 | 3 | 163 |
| url | 49 | 39 | 7 | 1 | 0 | 2 | 0 |
| **Total** | **8895** | **2950** | **2679** | **529** | **4** | **254** | **2479** |

**2,950/6,416 runnable plans passed (46.0%)**. WPT plans include query variants;
they are not all distinct source files. Subtest totals were **262,050 PASS,
857,561 FAIL, 320 TIMEOUT, 672 NOTRUN and three PRECONDITION_FAILED**. These are
separate from file/plan totals. The command's zero exit means the measurement
completed, not that those outcomes passed an expectation gate.

Encoding accounts for **394/529 timeouts**, and its long-deadline form tests
explain much of the wall time. The leading harness errors were missing support
for size container queries (110), scroll-state container queries (41), and
style container queries (20). Missing cached resources and unsupported APIs also
appear; harness errors are not all engine assertion failures. Skips were 1,524
reftests, 497 non-testharness files, 426 testdriver cases, 29 TLS-origin cases,
and three worker cases. The runner swept 41 generated wrappers left by an older
interrupted cache run before collecting this selection.

All four crashes were **SIGABRT**:

- `dom/nodes/NodeList-static-length-getter-tampered-2.html`.
- `dom/nodes/NodeList-static-length-getter-tampered-3.html`.
- `encoding/legacy-mb-japanese/iso-2022-jp/iso2022jp-decode-csiso2022jp.html?4001-5000`.
- `encoding/legacy-mb-japanese/iso-2022-jp/iso2022jp-decode.html?1-1000`.

The result records say the driver died mid-command. Their saved crash-log
fields are empty; no stack trace or root cause was established. Preserve these
cases for focused reproduction rather than treating them as ordinary conformance
failures. The separate WPT gate regressions described above also remain open.

## Formatting

The required `tools/format.sh --check` exited **1** with **16 diagnostics in
four untouched files**: `ctbrowser/tools/ctdrive/ctdrive.cpp` (2),
`ctcompile/lib/CTNative/HostContract/ProviderPaths.h` (2),
`ctcompile/lib/CTNative/PartialEvaluation/Heap.h` (4), and
`ctcompile/lib/CTNative/Symbolic/Facts.cpp` (8). It stopped at the C++ gate;
this is not a pass for the later Python/web formatting stages. No formatting
changes were made to those files.

## Devbox operation and restoration

`devbox-idle.timer` was active/enabled before the run. It was explicitly stopped
before building and repeatedly verified inactive during testing. The existing
idle service otherwise deallocates the VM after 30 minutes of inactivity. An
Azure query found no separate DevTestLab shutdown schedule. The test job ran as
a durable systemd service, `ct-full-monorepo-20260921-a1d6680d`, with an EXIT trap
to refresh activity and restore the timer even if the SSH session disconnected.

After all seven stages, the timer was verified **active and enabled**. The
boot ID was unchanged: `4dc49618-be92-4745-b4b4-c487ecd48f27`. The service exited
**1** because CTest and the two gates failed; every stage nevertheless completed.
The launcher's console labels that aggregate value “CTest exit”; the actual
CTest exit was **8**, as recorded in `stages.tsv`.

Two old orphaned lit process trees had consumed roughly one CPU each for days
without active test children. Their six processes were reversibly paused during
this run, preserving their state. An independent watcher resumed them at
**2026-09-22 01:08:59 UTC**; all six were verified out of the stopped state.
Their process records and restoration timestamp are in the artifacts. A third
sleeping old lit tree was left alone. No prior work was killed or discarded.
The build lock was held through the entire build/test run and artifact copy,
then released. Roughly 45 GiB of disk remained free at completion.

## Reproduction and retained evidence

The local build ran through `tools/remote-build.sh all` while holding
`/tmp/ctbrowser-devbox-build.lock`. The same lock remained held for the remote
commands below. `suite_results` was the devbox artifact directory listed after
the block; use a fresh output directory for a later run. Full remote runner
scripts are retained as `planned-run.sh`/`run.sh`.

```bash
cd /home/ubuntu/projects/compile-time-browser/ctbrowser
/usr/bin/ctest --preset default -j8 --no-tests=error \
  --output-junit "$suite_results/junit.xml" --output-log "$suite_results/ctest.log"
cd ..
python3 tools/check/test262.py --self-test --binary build/tools/ct262 \
  --corpus /home/ubuntu/.cache/ctbrowser/test262
python3 tools/check/test262.py --gate --binary build/tools/ct262 \
  --corpus /home/ubuntu/.cache/ctbrowser/test262 --jobs 4 \
  --json "$suite_results/test262-gate.json"
python3 tools/wpt/run-wpt.py --selftest --driver build/tools/ctdrive
python3 tools/wpt/run-wpt.py --gate --driver build/tools/ctdrive --jobs 4 \
  --json "$suite_results/wpt-gate.json"
python3 tools/check/test262.py --dir test --binary build/tools/ct262 \
  --corpus /home/ubuntu/.cache/ctbrowser/test262 --jobs 4 \
  --json "$suite_results/test262-full.json" --tsv "$suite_results/test262-full.tsv"
python3 tools/wpt/run-wpt.py --dir css --dir dom --dir html --dir shadow-dom \
  --dir custom-elements --dir domparsing --dir selection --dir url --dir encoding \
  --driver build/tools/ctdrive --jobs 4 --json "$suite_results/wpt-full.json" \
  --tsv "$suite_results/wpt-full.tsv"
```

The local directory retains `build.log`, the formatter output and a copy of all
remote artifacts. The remote directory retains the stage logs, exact commands,
CTest inventory/JUnit/LastTest output, environment and CMake cache, corpus pins,
and full conformance JSON/TSV rows:

- Local: `/home/alex/Downloads/claude/test-results/2026-09-21-full-monorepo-a1d6680d/`.
- Devbox: `/home/ubuntu/ct-test-results/2026-09-21-full-monorepo-a1d6680d/`.

`ctest.console.log` contains every failing lit diagnostic and its expanded RUN
command. `junit.xml` records all 324 CTests. `stages.tsv` records exact UTC start,
finish and exit status for each stage. `test262-full.json` and `wpt-full.json`
retain every classified result; their TSV companions make individual cases easy
to locate. Large logs and generated binaries were not committed to the repository.

## Next work

Triage the compiler proof/admission failures and broken test-driver carrier
assumptions, then repair lit's discovery of input fixtures. Investigate the three
test262 crashes, four WPT crashes and two WPT event regressions separately.
Revalidate intended new admissions before changing refusal expectations.

The native feature boundary is unchanged: custom DOM iteration still cannot
transport the live counter through `counted-break-exit`; broader iterator state,
Bootstrap defaults and the application driver remain unfinished. Future changes
return to the standing focused-test policy unless another full run is requested.
