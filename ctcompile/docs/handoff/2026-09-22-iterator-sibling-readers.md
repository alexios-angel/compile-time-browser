# Sibling iterator state readers and combined mask periods, 2026-09-22

Continued clean `fca25a05` from `entry-captured-sibling-reader`, recorded in
HANDOFF, plan 00 and the iteration 53 completion journal. There was no dirty
predecessor work; the earlier interrupted drafts had landed. The named
`codex-wip-20260907` branch was absent, and other branches were preserved.
Linux `ps` inspected 72 processes and Windows CIM inspected 342, matching
actual Claude executables, CLI paths and loop scripts. Neither found a match;
Claude was confirmed stopped. No browser, shared implementation or runtime
oracle semantics changed.

## Landed

**c18dd782** proves confined sibling readers of mutable iterator Number
cells. Each unique local reader has only state-cell captures, a complete scalar
leaf body and zero-argument calls in the entry. Complete initialization,
closure, call and symbol-use checks precede expansion of the reader family.
Both ordinary calls and exact direct targets are admitted. An arrow may carry
the enclosing receiver only when its body does not observe it. Symbol scans
reserve the complete source cost before traversal.

Capture loads become scalar parameters in the unique private body. Each call
supplies the current cell values to the existing helper inliner; the existing
entry-state rewrite then preserves their source positions and close updates.
The reader closure and body retire only after all calls expand. Native output
uses scalar values and public DOM calls without cell storage, GC or Script.
Writers, callbacks, recursive bodies, escaping readers and forwarded captures
remain refused.

The exact saved reader source is promoted byte-identically. A two-cell reader
source checks before-loop, body, post-close and repeated final observations.
All 87 earlier source bodies remain, including the additionally promoted immediate reader. Four raw twins cover ordinary/direct calls
and ordinary/zero-body traversal, reusing the ordered state and latest-close
assertions. Twenty-three new hostile mutations and existing budget searches
check initialization, target identity, symbolic observers, implicit arguments,
escapes, writes and source preservation.

**76b6f909** derives the existing output power-of-two period from bits
that can still vary after combining the input lattice and AND/OR mask. Fixed
low bits may interleave, so taking only the larger of the two earlier periods
lost precision. Signed conversion guards, full reload/store census and budgets
remain. Twenty-four CFG and twenty-four SCF rows plus 26 source witnesses
extend the unchanged 199 earlier source bodies. Retained and saved children,
nonzero residues, signed bands, overlapping reloads, later writes and near masks
are covered.

## Focused validation

| Check | Result |
| --- | --- |
| Final exact host CTest | 1/1 PASS; 1.01 s test / 1.02 s total |
| Both new complete-source admission preflights | PASS |
| Custom iteration lit | FAIL after 480 native executions / 240 Node/VM observations; 749.19 s; stale extra-captured-closure refusal |
| Final new-positive and all-refusal subset | PASS; 16 native executions / 146 refusals / 8 Node/VM observations |
| Exact escape arrays CTest | 1/1 PASS; 1.76 s test / 1.78 s total |
| AND and OR/XOR lit | 2/2 PASS; 0.14 s |

All eight final code/test hashes match their focused checks. The main custom
case finished all 30 positive sources before encountering an old
`extra-captured-closure` refusal now admitted by the reader proof. Its exact
source was promoted unchanged, with the existing preloop DOM trace assertions
also applied to it. A final subset executed that source in every existing mode
and checked all 61 remaining refusal sources. Together the runs cover 31 positive
sources and 496 native executions; the complete custom case was not replayed.
The compiler and raw header are unchanged from the passing exact host test.
The final source file received the subset check. This is not a whole-custom lit
pass.

The initial native preflight found that source arrows retain an unused lexical receiver and the
importer already emits direct calls. The proof was extended to those exact
forms with a complete symbol census. Both preserved and new reader sources
then passed admission; the final host/source gates include this fix and the
symbol-scan budget reservation.

The first combined build failed because the new escape raw tests supplied
`std::string` expectations to existing `const char *` fields. The fix uses static
literal expectations, including deferred Structured rows, without changing
source bodies, expected traces or production logic. Native validation proceeded
independently while the corrected escape gate waited for the shared build lock.

Initial final-build targets:

```bash
ctjs-opt ctjs-translate ctcompile-tool ctcompile-test-native-reference \
  ctcompile-test-host-contract ctcompile-test-escape-analysis-arrays \
  ctcompile-test-escape-claims ctcompile-test-type-oracle
```

After the test-only build failure, the native build selected the first five
targets; the escape build selected `ctjs-opt`, `ctcompile-test-escape-analysis-arrays`,
`ctcompile-test-escape-claims` and `ctcompile-test-type-oracle`.
Every build and devbox check held `/tmp/ctbrowser-devbox-build.lock`.

```bash
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_host_contract$'
PYTHONPATH=ctcompile/test python3 /tmp/ctcompile-iteration54-preflight.py
~/.lit-venv/bin/lit -sv build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-custom-iteration[.]test$'
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_escape_analysis_arrays$'
~/.lit-venv/bin/lit -sv build/ctcompile/test --filter='^ctcompile :: Analysis/Escape/escape-claims/(bitor-xor|bitand)-index-overwrite[.]test$'
```

The final subset used the locked local runner
`/tmp/ctcompile-iteration54-final-source.sh`, retained with its Python wrapper and
all native compiler flags in the artifacts. It selected only
`extra-captured-closure` for execution and kept all source refusals. Its explicit
build selected `ctjs-opt`, `ctjs-translate`, `ctcompile-tool` and
`ctcompile-test-native-reference`; Ninja reported no work.

`tools/format.sh --check` reports the same 16 pre-existing diagnostics in four
untouched files: `ctbrowser/tools/ctdrive/ctdrive.cpp`,
`ctcompile/lib/CTNative/HostContract/ProviderPaths.h`,
`ctcompile/lib/CTNative/PartialEvaluation/Heap.h` and
`ctcompile/lib/CTNative/Symbolic/Facts.cpp`. Changed C++ passes the pinned
formatter; Python passes Black/AST; diff whitespace and temporary shell runner
syntax checks pass. Local source checks cover 92 Node syntax checks and 300
exact Node observations, then the promoted immediate reader added one syntax check
and ten exact observations; escape checks execute 225 source functions.

No full CTest/compiler lit, broad corpus/native matrix, WPT/test262, Windows,
additional sanitizer, local build or push ran. Unchanged nested/dataset lit was
not replayed. The devbox idle timer is active/enabled.
Logs, runners, hashes, lit JSON, source/IR witnesses and representative emitted
C++ are at `../test-results/2026-09-22-iterator-sibling-readers/` beside the monorepo.

## Exact next boundary

Start with `refusals()["entry-captured-sibling-writer"]` in
`ctcompile/test/CTNative/Browser/native_dom_custom_iteration.py`. It retains the
complete reader source but changes the helper to:

```javascript
const read = () => { emitted += closed; return emitted; };
```

Both optimization policies refuse with
`native DOM source: DOM iterator sibling reader must be a read-only scalar leaf`.
Each policy was replayed once solely to preserve stderr, with status 1 and no
native execution claimed for this source.
The next proof must preserve ordered shared-cell writes and the ordinary scalar
return value together. The complete return remains `count + read() + closed`;
do not remove its observations to admit the helper.

Nested custom opens, body return/throw close behavior, literal range-for printing,
unguarded Bootstrap defaults and the application driver remain open. Full Bootstrap
is not admitted and no vendor coverage gain is claimed. Higher bit-mask gaps still
need a richer range representation.
