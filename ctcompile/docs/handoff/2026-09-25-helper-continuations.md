# Iterator helper continuations and conditional confinement across CFG blocks

Resumed clean `ctcompile-v1` at `a97678f2` from the raw-helper-tails handoff,
both commit histories and the synchronization journal. The old WIP branch was
already an ancestor. This iteration was interrupted during the escape build;
the preserved candidates and incremental build resumed without replaying the
already-passed native checks. Agents developed the escape change and native
tests in parallel; rate limits left the parent to complete validation.

## Changes

`9ca2eeca` verifies and merges unconditional entry-chain
edges through MLIR's existing `cf::BranchOp::canonicalize`. Each destination
must have exactly one predecessor. Branch arguments forward their original
values and operations retain their order. The existing bounded census and
quadratic work reservation cover normalization. No DCE or conditional-branch
folding is added. Empty helper declarations refuse before body access.
Every callable, capture, source effect, lifetime and thrown-payload proof still
runs before a prepared module and its fingerprint can publish.

The 23 raw-helper controls include identity/next/getter continuations, explicit
block arguments, effects after the edge, publication, unknown branches, loops,
wrong payloads/receivers, invalid names, empty declarations and work ceilings.
Successful controls compare the complete prepared module with the original;
refusals preserve the module and contract. Two derived JavaScript witnesses add
`do { break; } while (false)` to each helper; both original witnesses remain
unchanged.

`608b638a` removes the redundant single-root-block restriction on conditional
allocation confinement. The mandatory complete contents query already refuses
repeated root CFG blocks. Its counted-loop exception has a complete whitelist
excluding allocations and regions. Conditional ancestry, initialized lattices,
every return and all-write acyclicity remain required. Thirteen new controls
cover diamonds, selected/saved identities, duplicate edges, multiple returns,
overwrite, publication, cycles and backedges. This proves frame confinement
only, with no region lifetime, unique ownership or native lowering authority.
No raw escape-oracle precision measurement was made.

## Focused validation

All C++ builds and remote checks held `/tmp/ctbrowser-devbox-build.lock`.
Explicit targets were `ctcompile-test-exception-recovery`,
`ctcompile-test-host-contract`, `ctcompile-test-escape-analysis-arrays`,
`ctcompile-test-escape-analysis-completion`, `ctjs-opt`, `ctjs-translate` and
`ctcompile-test-native-reference`.

- Final `ctcompile_exception_recovery`: **1/1, 5.62 s** (5.63 s total).
- `ctcompile_host_contract`: **1/1, 2.74 s**, using final native production.
- `ctcompile_escape_analysis_completion`: **1/1, 0.01 s**.
- `ctcompile_escape_analysis_arrays`: **1/1, 4.71 s** (4.73 s combined total).
- `outer-selection.py`: only the two original getter/method witnesses and two
  derived helper-edge witnesses. All four pass raw and structured native
  execution, with both printers and GCC/Clang; structured checks also use both
  providers and optimization modes. Node and VM traces agree. Native assertions
  check exhausted/active iterators, visited state, caught identity and write
  order on two documents. Zero-budget refusals remain checked.
- All **40** generated C++ files pass namespace checks, and the harness checks
  execution and absence of Script/AOT symbols for their **80** binaries.
  Each derived raw helper contains three `cf.br` operations before preparation.
- All **six** final implementation/test hashes match the devbox.
  `tools/format.sh --check`: **1127 C++, 159 Python, 114 web files pass**;
  `git diff --check` passes.
- All ten original C++ raw literals and 71 Python newline-bearing literal
  values are preserved.

Independent review found that a valid private helper declaration could reach
an empty-body dereference; the guard and three regression controls fix it.
The initial construction of those controls cleared blocks without dropping
cross-block references. GDB located the resulting corruption in a later
allocation. The fixture now uses `FuncOp::eraseBody()`; final recovery passes.
The parent reviewed that test correction after the reviewer hit a rate limit.
Two preliminary debugger runs had missing/wrong fixture arguments and exercised
no test. The initial 20-control native recovery/host run passed; the later
declaration-control crash is not counted as a pass.

Process checks inspected 11 Linux identities and 351 Windows processes with no
Claude identity match. Sixty Linux permission failures made availability
uncertain, so concurrent-agent rules applied. No browser/runtime/shared
implementation changed; no local C++ build, push or history rewrite ran.

Skipped: full CTest/compiler lit, whole native fixtures, broad corpora/matrices,
raw escape oracle, full Bootstrap, WPT/test262, Windows/macOS, sanitizers and
performance. Focused results are not a full-suite pass.

## Next boundary

Reachable conditional helper CFGs still fail the one-block getter, identity or
capture checks. The unknown return-versus-throw and live-loop controls preserve
that boundary. Prove their source branch, payload and effect correspondence
before extending normalization. Repeated nested allocation instances still need
distinct lifetime correspondence; frame confinement grants no region destruction
or unique ownership. Mutable exceptional state, escaping owners, unguarded
Bootstrap defaults, the application driver and full native Bootstrap remain.

Evidence: `../test-results/2026-09-25-helper-continuations` relative to the
monorepo, with exact logs, source selectors, generated text artifacts, source
hashes, preservation and review records, and verified `SHA256SUMS`.
