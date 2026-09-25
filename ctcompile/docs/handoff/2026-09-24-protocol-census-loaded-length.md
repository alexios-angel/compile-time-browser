# Iterator completion census and loaded length guards, 2026-09-24 UTC

Resumed clean `9b2b21b0` from HANDOFF/detail157, Current native work, both areas'
history and the synchronization journal. The old `codex-wip-20260907` was already
an ancestor. No abandoned patch remained at start. After a rate-limit interruption,
this same iteration resumed the six-file candidate and completed focused gates.

## Landed

**e3e6f76f** extends the existing completion census to verified result-bearing
iterator helper invocations under Try. It reuses exact intrinsic arities and
requires the original undefined receiver, call-result dispatch, Boolean close
flag and inert continuation projections. Entry verification checks every normal
and unwind tuple and pre-call saved state. No call or edge is erased, and observed
close(false) never enters the suppressed-cleanup map.

Original getter `1a7fb166` and method `7acf503b` now pass this census and refuse at
`DOM custom iterator requires one root-local open`. Raw and lifted imports retain
four calls, all 33 original checks and 15 saved registers per call. The new matrix
checks changed helper identity, non-Boolean flags, continuation effects, forged
failure state, wrong arity and zero budget without modifying the module. Complete
preparation still rolls back the entire module and contract. No source admission
or full-Bootstrap coverage gain is claimed.

**5cece494** reuses held array identity for a cached length whose receiver was
loaded by an own-property read. The allocation, receiver read and length read
must share one preheader block; cross-block origins remain refused. The cached
Number must equal the current extent and survive backedges unchanged. Existing
full mutation checks, bounded induction and exact replay remain. Replacing the
holder later does not replace a previously read array. Saved children remain
escaping; changing bounds/receivers, resizing, unrelated loop writes and broader
origins remain conservative.

Frozen source SHA-256
`0d5c48f86325024263c54b0bc4ca8e950a9114189e350a6e65e3006e1a527809`,
program `28b9564421efedd4`, had zero confined sites before the change. Afterwards
its child and holder are Confined: **2/4 total allocation sites**, all four observed,
zero violations, unlimited VM recording, two frame pops. The returned array stays
Stored. Node returns `[0,0,0]`; six source/control Node outcomes are preserved in
the evidence. The original witness is byte-identical to the new lit function.

## Exact focused validation

- All devbox commands held `/tmp/ctbrowser-devbox-build.lock`. Explicit build
  targets across the session were `ctjs-opt`, `ctjs-translate`,
  `ctcompile-test-native-reference`, `ctcompile-test-exception-recovery`,
  `ctcompile-test-host-contract`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims` and `ctcompile-test-type-oracle`.
- Final `ctcompile_exception_recovery`: **1/1 PASS, 5.59 s**. Retained
  `ctcompile_host_contract`: **1/1 PASS, 3.10 s**. The initial recovery check
  passed in 5.85 s; that initial two-test run took 8.97 s.
- Final formatted `ctcompile_escape_analysis_arrays`: **1/1 PASS, 4.87 s**,
  **4.89 s total**. The preceding arrays check passed in 4.73 s alongside final
  recovery; that two-test run took 10.34 s.
- Selected `Analysis/Escape/escape-claims/cached-loaded-length-guard.test`:
  **1/1 PASS, 0.12 s**, **359 excluded**. The frozen source separately passed
  the before/after compiler-claims/VM-recorder oracle. No corpus run occurred.
- Selected native sources `invocation-return`, `write-boolean-snapshot` and
  `helper-next-record`, retaining the harness's refusal and original outer
  controls: **PASS**. This is not a whole-fixture or lit pass. The exact selector
  and command are retained with the logs; both DOM providers, optimization and
  printing choices were exercised with GCC/Clang and Node/VM comparisons.
- `tools/format.sh --check`: **1126 C++, 158 Python, 114 web files PASS**;
  `git diff --check` passes. The initial formatter found only layout in the
  escape additions; final formatted arrays were rebuilt and checked.
- Independent six-file static review is clean. All six final hashes match the
  devbox. Twenty-four generated C++ files contain no Script/AOT/ctjs namespace;
  the native harness also ran its binary-symbol checks. Independent preservation
  checks retain all 34 frozen native artifacts, every historical Induction
  validation byte and the original raw JavaScript literals. Of 302 Python string
  constants only the intended refusal diagnostic changed.

Skipped: full CTest/compiler lit, whole native fixtures, unaffected native replay,
broad corpus/matrices, full Bootstrap, WPT/test262, Windows and sanitizers. No
browser/shared implementation, local C++ build, push or history rewrite occurred.
Source tracing and the escape candidate were parallelized; rate-limited agents'
checkpoints were resumed by the parent. The restarted final review completed.

## Exact next boundary

The completion census now accepts the original recovered protocol shape. Open
still must be root-local, and record users must be direct. Follow successful open
aliases through normal payload/state tuple positions without treating a failure
payload as the record. Reuse the existing full holder/slot/identity proof before
selecting a normal open edge; tuple shape alone does not prove nonthrowing effects
or that the holder could not change. Then carry next/exhaustion, cleanup and both
close(false) failure paths through the retained handler with saved returns and
caught-node identity. General result-bearing Invoke/Try state rewriting is still
missing. Suppressed-close handling does not implement observed close(false).

The loaded-length witness is complete. Broader cached origins need separate
execution provenance. Escaping-node exception owners, broader iterators,
unguarded Bootstrap defaults, application driver and full native Bootstrap remain.

Initial Linux inspection read 11 executable identities with 60 permission
failures; Windows Get-CimInstance returned 343 records. Neither found Claude, but
status remained uncertain and concurrent-agent rules stayed in force.

Evidence: `../test-results/2026-09-24-protocol-census-loaded-length/`, with frozen
sources, before/after escape claims and recordings, selected generated C++, exact
logs, final hashes, independent review and verified `SHA256SUMS`.
