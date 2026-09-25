# Sibling iterator writers and low-bit rounding, 2026-09-22

Resumed clean `8c04990c` and its exact sibling-writer boundary from HANDOFF,
plan 00 and iteration 54's journal. No dirty predecessor work remained;
`codex-wip-20260907` was absent and other branches were preserved. Linux `/proc`
found no Claude identity among 14 readable executable records, but 57 processes
were permission-denied. Windows CIM inspected 345 processes with no Claude
match. Status remained uncertain; concurrent-agent rules applied. No browser,
shared implementation or runtime-oracle semantics changed.

## Landed

**8848217e** extends the existing confined sibling-helper proof to writes.
The complete family, initialized cell identities, local bodies, ordinary/direct
call sites and symbolic observers are checked before expansion. Capture slots
become temporary explicit cell parameters. The existing inliner places reads
and writes at each call; the existing entry rewrite turns those accesses into
ordered scalar state. An ordinary return can retain an earlier observation
while subsequent state writes remain visible. Branches inside helper loops,
repeated calls and the latest iterator-close state use the existing transport.
Complete DOM reproof still validates scalar producers and borrowed lifetimes.
No closure/cell storage, GC, Script or VM protocol appears in native output.

The exact saved writer source is promoted unchanged. Two new complete sources
check two-cell update order, saved return values, loop-local branches and calls
before/body/after iteration. All 92 prior source bodies and 31 prior positive
metadata tuples remain unchanged. Two former raw self-store refusals are
promoted unchanged; four ordinary/direct and normal/zero-body writer twins
check current state separately from returned snapshots. Existing budgets,
malformed-call/capture/symbol controls and complete-DOM category refusals remain.

**ec206acd** keeps exact same-ToInt32-band endpoints for AND clearing or OR
setting a contiguous low suffix. These operations round monotonically; only
the proved power-of-two output period is retained when the operation is not
affine. Other masks retain the existing conservative enclosure. Complete
reload/store/mutation census, signed conversion guards and budgets remain.
Twenty-six CFG rows, 26 SCF rows and 26 source witnesses extend all 225 earlier
source bodies/calls without changing them.

## Focused validation

| Check | Result |
| --- | --- |
| Explicit affected devbox build | PASS |
| Exact `ctcompile_host_contract` | 1/1 PASS; 1.24 s test / 1.25 s total |
| Three new writer admission preflights | PASS |
| All 68 source refusal preflights | PASS |
| Eight-positive custom source subset, including all 68 refusals | PASS; 128 native executions / 328 refusals / 64 Node/VM observations |
| Exact `ctcompile_escape_analysis_arrays` | 1/1 PASS; 1.92 s test / 1.93 s total |
| AND and OR/XOR index-overwrite lit | 2/2 PASS; 0.16 s |

The source subset selects `mutable-captured-counter`,
`entry-captured-ordered-state`, `entry-captured-sibling-reader`,
`entry-captured-sibling-ordered-reader`, `extra-captured-closure`, and the three
new writer positives. Each runs through borrowed/owned DOM, both optimization
policies, explicit/deduced output and the existing two C++ compilers. Its refusal
checks include all 68 source programs under both policies. The complete custom
case was not run; no full-custom or full-suite pass is claimed.

All eight final code/test hashes match local files and the devbox. Native
production/raw files stayed unchanged after the passing host check. Source-only
corrections then received the final execution subset. The initial explicit
`ctjs-opt` build and the final build both passed; final targets were:

```bash
ctjs-opt ctjs-translate ctcompile-tool ctcompile-test-native-reference \
  ctcompile-test-host-contract ctcompile-test-escape-analysis-arrays \
  ctcompile-test-escape-claims ctcompile-test-type-oracle
```

Every devbox operation held `/tmp/ctbrowser-devbox-build.lock`. The exact tests:

```bash
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_escape_analysis_arrays$'
~/.lit-venv/bin/lit -sv build/ctcompile/test --filter='^ctcompile :: Analysis/Escape/escape-claims/(bitand|bitor-xor)-index-overwrite[.]test$' -o /tmp/ctcompile-iteration55-escape-lit.json
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_host_contract$'
PYTHONPATH=ctcompile/test python3 /tmp/ctcompile-iteration55-preflight.py
```

The final source execution used `/tmp/ctcompile-iteration55-native-source.sh`
and its eight-label Python wrapper, retained in the artifacts with every
compiler flag. Its dependencies were already built and verified. Two additional
stderr-only captures retain the next refusal under both policies, status 1;
no native execution is claimed for that program.

New source drafts initially used nested subtraction outside the existing DOM
arithmetic policy and a unary-negative initializer outside the literal-initializer
proof. Only the new witnesses changed to saved scalar returns and direct zero
literals; their ordered state observations remain. The later pre-traversal
conditional exposed the real next boundary and was preserved byte-identically
as a refusal. Its supported sibling joins the conditional inside its local loop.
The original saved writer and all earlier sources remain intact. The first lit
command used an unsupported `--json-output` option; it was corrected to `-o`
and the already-passing arrays CTest was not replayed.

`tools/format.sh --check` retains 16 pre-existing diagnostics in four untouched
files: `ctbrowser/tools/ctdrive/ctdrive.cpp`,
`ctcompile/lib/CTNative/HostContract/ProviderPaths.h`,
`ctcompile/lib/CTNative/PartialEvaluation/Heap.h` and
`ctcompile/lib/CTNative/Symbolic/Facts.cpp`. Changed C++ passes the pinned
formatter; changed Python passes Black/AST; diff whitespace and temporary shell
syntax checks pass. Local source checks cover 102 Node syntax checks and 340
exact traces. Escape source checks execute 251 functions and check 26 new exact
outputs. These local checks are separate from the 64 measured Node/VM observations.

No full CTest/compiler lit, full custom case, unchanged nested/dataset lit,
broad corpus/native matrix, WPT/test262, Windows, additional sanitizer, local
build or push ran. The devbox idle timer is active/enabled. Logs, runners,
hashes, source/IR witnesses and emitted C++ are retained at
`../test-results/2026-09-22-iterator-sibling-writers/` beside the monorepo.

## Exact next boundary

Use `refusals()["entry-captured-sibling-preloop-branch-writer"]` in
`ctcompile/test/CTNative/Browser/native_dom_custom_iteration.py`.
Its complete source SHA-256 is
`1be68cb4664fab97ad590d6da4df378305c7581b43f7a2a5a0e6b9c26c704711`.
The helper retains its saved return observation and updates two cells in a local
loop, followed by an `if/else`. It is called before the custom traversal as well
as inside and after it. Existing completion normalization duplicates the later
custom protocol into the pre-traversal branch arms, so both policies refuse:

```text
native DOM source: DOM custom iterator next call is ambiguous after completion
```

The next proof must join the helper's ordinary result and latest state before
that shared continuation. Keep all before/body/close/final observations and the
complete source; do not remove the conditional to admit this boundary.
Argument-taking helpers, sibling method breaks, nested custom opens, abrupt
close, literal range-for printing, unguarded Bootstrap defaults and the
application driver also remain open. Full Bootstrap is not admitted; no vendor
coverage gain is claimed. Higher nonmonotone mask gaps still need a richer range
representation.
