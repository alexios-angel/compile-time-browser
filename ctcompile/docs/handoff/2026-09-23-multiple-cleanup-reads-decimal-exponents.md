# Multiple cleanup reads and decimal exponents, 2026-09-23

Resumed clean **7ca5d604** and retained source `04f5317c` from HANDOFF,
Current native work and iteration 123's final journal. No dirty drafts or
unmerged `codex-wip-20260907` remained. Other branches were preserved.
Source, raw-proof and escape agents worked in parallel. Rate limits and user
interruptions were resumed from saved candidates, hashes and completed checks.

## Native change

**4770f5c1** retains completed branch-local DOM reads before accepting the next
read in a guarded second cleanup write. The existing method/result use census
and feeding-read selection preserve every snapshot, including ignored reads.
There is no new read-count limit. The original guard, lookup/read/write order,
write suppression and body exception remain. Every selector requires valid
literal syntax; private complete DOM/Style reproof still precedes publication.
No cloning, runtime or ownership interface changed.

Original source SHA-256:
`04f5317cc53d459eac286e6b1d21336ce25187865623072739a9505ba8c68f1b`.
Getter `6f0f4f05`, order `2a40082a` and three-read selector `13fb1ada` variants
also execute. A permanent emitted-value assertion checks that the second write
uses its final read's Boolean, even when later writes overwrite the attribute.
The focused runner passes **64 native executions, 92 refusals and 32 Node/VM
observations**, with complete final stdout. All 32 generated C++ files contain
no Script types; the fixture checks standalone and linked output.

Source preflight passes 30 policy checks. Local checks cover 15 distinct sources
and 16 distinct Node observations; the amended three-read variant's syntax and
four observations were rerun. Historical source bodies and oracle constructions
remain. Raw coverage promotes the exact prior two-read refusal, preserves all
28 MLIR literals and 97 original constant constructions, and adds snapshot
identity/order, invalid selector, incomplete method, observer and budget checks.
An independent production review found no actionable issue.

## Escape change

**3eb265b9** extends shared `boundedConvertedNumber` to decimal-point and exponent
Strings whose converted values have an exact, finite uint32 magnitude. It
validates the complete grammar before calling public Core conversion. Original
Strings/property keys, String addition, table mutation checks, receiver reload
gaps and actual-write replay remain unchanged. Input remains limited to 32 bytes.

Review found that Core's existing `out_of_range_value` ignores an overflowing
integer exponent parse and can overflow when adding exponent and mantissa order.
The compiler now requires exponent magnitude at most `INT_MAX - 32` before
calling Core. The source-byte bound limits the mantissa order. Huge positive and
negative exponents, unsafe near-limit values and malformed suffixes have refusal
controls; safe underflow remains covered. No browser/runtime code was changed.

Baseline `ec741b716d70f17f` classified all six child sites as stored. The same six
bodies in final program `5264f75d9ac8a00c`, functions 61–66, measure **three
confined and three stored**. All six children are observed once, with zero
unresolved or unchecked instances. Permanent recording assertions enforce this;
their calls precede the existing intentional BigInt throw. Six Node witnesses
check original Strings, array/child identity, own keys and read/write sequences.
All 60 historical source bodies, six baseline bodies and 139 original raw input
constructions remain. Historical `.0`/`e0` expectations were promoted with their
original inputs. An independent review of the final exponent guard is clean.

## Focused validation

- `ctcompile_host_contract`: 1/1 PASS, 2.58 s / 2.59 s total.
- `ctcompile_escape_analysis_arrays`: 1/1 PASS, 2.97 s / 2.98 s total.
- `Analysis/Escape/escape-claims/table-index-overwrite.test`: 1/1 PASS,
  0.13 s, 356 excluded.
- `tools/format.sh --check`: final 1126 C++, 157 Python, 114 web files PASS.
- `git diff --check` passes; all 11 final code/test hashes match the devbox.

Explicit devbox targets: `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. Every remote operation held the shared build lock.
The host build was interrupted after its library compile/link; incremental
resumption built the remaining host translation unit and executable, then ran
the first host CTest. Native checks were not replayed after the separate escape
change. Escape checks passed their first run with the final safety guard.

Initial process checks found no Claude executable/CLI/loop matches among 14
readable Linux executables and 345 Windows process records. Final counts were
14 and 347. Sixty Linux executable permission errors kept status uncertain.
Concurrent-area rules applied; there were no browser/shared implementation
edits, local C++ builds, pushes or history rewrites.

Full CTest/compiler lit, complete iterator fixtures, unaffected native replays,
broad corpus/matrices, full Bootstrap, browser/WPT/test262, Windows and sanitizers
were skipped. These focused results are not full-Bootstrap coverage measurements.

Evidence: `../test-results/2026-09-23-multiple-cleanup-reads-decimal-exponents/`,
relative to the repo, with `SHA256SUMS`. Working checkpoints are
`/tmp/ctcompile-native124`, `/tmp/ctcompile-tests124`, `/tmp/ctcompile-raw124` and
`/tmp/ctcompile-escape124`.

## Next boundary

`unsupported-selector-guards-second-write-postread`, SHA-256
`77866d32652bc2438cc59ec6c912041b95c319be573d0f1c54d002a676558c0f`, refuses
**DOM protected helper needs an independent inert-body proof** under both
policies. It reads `hasAttribute` after the guarded second cleanup write.
Preserve that read's original guard/order and the saved body exception.
Broader cleanup/control flow, nested custom iterators, unguarded Bootstrap
defaults, the application driver, full native Bootstrap, general powers and
legacy SCF retention remain unfinished. Nonintegral numeric results, unsafe
exponents and Strings over 32 bytes remain outside this conversion proof.
