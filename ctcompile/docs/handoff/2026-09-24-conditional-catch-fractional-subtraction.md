# Conditional caught-node state and fractional subtraction, 2026-09-24 UTC

Resumed clean `3e1ea9d7` from the latest handoff, Current native work and journal.
The previous session's changes were committed; `codex-wip-20260907` is already
an ancestor. Recent compiler/browser/Shell commits and unmerged branches were
inspected. The original next native source and arithmetic witness were retained.
Three agents handled source regressions, escape work and independent review.
After interruption/rate limits, the parent finished their saved checkpoints;
the reviewer resumed and completed native production and escape reviews.

## Landed

`6765f9bc` extends local caught-node normalization to conditional protected bodies
whose every path explicitly throws. The existing recovery supplies the original
catch payload and saved register state. A bounded proof follows the exact
completion result through both SCF yields; a separate recursive whitelist checks
both protected arms for nonthrowing operations. The original branches remain in
place, catch operands retain their corresponding values, and existing structured
exit cleanup removes unused completion slots. Full DOM proof precedes publication.
The output needs no borrowed C++ exception or VM fallback.

Three frozen sources execute unchanged:

- Conditional identity: `a618fc04b599acfc1c80c608471ffe8d1a840c6a47b02fa0ef576879a96e931a`.
- Conditional catch read: `973eee7ed758bb8f63c427a05100db75919beccc1f1cb93207ba6f06797946f5`.
- Conditional saved Boolean: `0226456963f6b44a3a5ff96f700076ce9bef9692797e1c2d2423138ba7e66cd6`.

The original iterator getter has one observing outer handler, rather than an
unobserved suppression landing. Its diagnostic now says **DOM iterator observing
catch requires call/check payload and state proof**. Neither it nor the method
twin is admitted. Their exact sources and cleanup/exhaustion traces are now
permanent controls in the caught-node source fixture. Mixed normal/throw local
completion, protected calls/reads, caught rethrows and borrowed returns still
refuse. Raw rejection checks retain the source and contract fingerprint.

`dd894f23` computes subtraction of two original Number literals with LLVM APFloat
binary64 arithmetic and round-to-nearest, ties-to-even. Only the result's ToUint32
snapshot is added. The counted-loop proof requests an unchanged table singleton
under bitwise demand and retains the complete receiver, mutation and reload census.
Computed operands and Strings cannot borrow literal Number authority; ordinary
arithmetic and property keys still require their separate exact evidence.

Identical source SHA-256
`4f39da6ed4065c61435949dcb4118d2203dedd247169d668b9d0b47df9d43196`, program
`362f023c9aaba83c`, changes function 1/site 4 from Stored to Confined. Its key table
also becomes confined: **two of four total sites**, versus zero before. The child
is observed once with zero unresolved or unchecked instances; both runs have zero
soundness violations. All **149 historical source function bodies** remain
unchanged; function 150 adds the same fractional subtraction body. One historical
raw Sub1 refusal now succeeds while correctly retaining its child. New controls
cover saved children, fractional properties, subsequent arithmetic, String and
computed inputs, and table writes before/after reads.

## Focused validation

- Exact `ctcompile_exception_recovery`: **1/1 PASS**, 4.87 s, 4.89 s total.
- Five native preflights: three new conditional catches admitted; the original
  observing getter/method refused with the precise diagnostic.
- Dedicated native source checks: **80 executions, 28 refusals, 14 distinct
  Node/VM observations** across both providers, optimization policies,
  explicit/deduced output and GCC/Clang. All **40 generated C++ files** contain
  no Script namespace; standalone clients also check linked symbols.
- The native lit invocation completed 80 executions, 20 refusals and the first
  ten Node/VM observations before failing on the new outer-catch trace comparison:
  the reference tool percent-escapes punctuation. The harness now decodes that
  representation. Only `check_outer_catches` was then run, passing the remaining
  eight refusals and four Node/VM observations. **No final whole native-lit pass
  is claimed.** No compiler production changed after the recovery pass.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS**, 3.55 s, 3.56 s total.
- Selected `Analysis/Escape/escape-claims/table-index-overwrite.test`:
  **1/1 PASS**, 0.16 s, 357 excluded.
- Identical standalone escape baseline/final oracle checks pass. The next
  computed-subtraction witness was measured separately, without a corpus replay.
- `tools/format.sh --check`: **1126 C++, 158 Python, 114 web files PASS**;
  `git diff --check` passes. All **eight final code/test hashes** match the devbox.
- Independent native production and four-file escape reviews are complete and
  clean. The parent reviewed the final source harness, including its corrected
  reference-output comparison.

Affected targets were built through locked `tools/remote-build.sh` commands:
`ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
`ctcompile-test-exception-recovery`, `ctcompile-test-escape-analysis-arrays`,
`ctcompile-test-escape-claims` and `ctcompile-test-type-oracle`.
The first build exposed const MLIR wrapper handles in the new escape proof;
removing their const qualification fixed compilation. A new formatting violation
was corrected before the final formatter pass. The first arrays and selected
escape lit runs passed. Successful gates were not replayed after unrelated changes.

Skipped: full CTest/compiler lit, complete iterator fixture, unaffected native
replays, broad corpus/matrices, full Bootstrap, browser WPT/test262, Windows and
sanitizers. No browser/runtime/shared implementation changed, no C++ was built
locally, and nothing was pushed or history-rewritten. Availability inspection
found 11 readable Linux identities with 61 access errors, plus 344 Windows
Get-CimInstance records and no actual Claude identity. Status was uncertain,
so concurrent-area rules applied throughout.

## Exact next boundaries

Native `caught-node-getter-identity.js`, SHA-256
`1a7fb1661163b19674ee92084bc52ded3f96417279039131df31282ca6d813b4`, and method
`7acf503b5734264de1d4d390cfb239a63273cd9052c078c66fd60166bdc2c6d3` still need
original open/next/close call/check correspondence, caught payload/state transport
and independent nonthrowing authority for every removed status edge. Do not
relax the unobserved suppression proof to admit them. Preserve cleanup writes,
node identity, saved completions and exhaustion. Uncaught `67bd3ad9`/`f38a8b8a`
retain their exception-owner lifetime refusal. Mixed local completions still need
path-correlated normal/caught state; the new proof covers all-throw bodies only.

Escape's next frozen source SHA-256
`8ecfabd88996593271957b58a8a68eecffd10fe99abe1b3b9b73e91e9276589a`, program
`51d9a9499f905cba`, uses `(keys[i % 2] - 0.25 - 0.25) | 0`. The child remains
Stored; Node returns `[0,0,0]` and the VM observes it confined once with zero
unresolved or unchecked instances. The second subtraction needs independent
computed Number-result evidence. Converted bits are insufficient. The earlier
fractional-property Node/VM discrepancy remains separate.

Protected observers, broader/nested iterators, unguarded Bootstrap defaults, the
application driver and full native Bootstrap remain unfinished. No full-Bootstrap
admission or coverage improvement is claimed.

Evidence: `../test-results/2026-09-24-conditional-catch-fractional-subtraction/`,
with SHA256SUMS. Checkpoints: `/tmp/ctcompile-native141`,
`/tmp/ctcompile-tests141`, `/tmp/ctcompile-escape141` and
`/tmp/ctcompile-review141`.
