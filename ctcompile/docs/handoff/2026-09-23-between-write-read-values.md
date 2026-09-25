# Saved reads between iterator-close writes, 2026-09-23

Resumed clean **9d3ddc90** and retained source `40e597cd` from HANDOFF,
current00, the before-first-selector-values detail and iteration 111's journal.
No dirty drafts or unmerged `codex-wip-20260907` remained; unrelated branches
were preserved. Three agents handled source, raw-host and escape work. Source/raw
agents saved frozen work before rate limits; the parent reviewed and completed
both. The escape agent independently reviewed native production without findings.

**b08d5bd8** uses the existing standalone-read slots and complete use census
after the first protected write when no second write is pending. Acquiring the
second write's method still selects its original feeding-read proof. A saved
`hasAttribute` Boolean can survive both selectors and later writes for one final
write. Direct, closure and custom-iterator calls share the expansion path. Exact
suppression, single-consumer checks, source guard/order, private helper/capture
proof, original saved exception and budgets remain. Complete typed DOM/Style
reproof still precedes publication; generated C++ calls public browser APIs.

The original source is unchanged, SHA-256
`40e597cdc700f6206d15db2bd95a1c294499a78157161d678328ab7827158cdd`.
Getter `08ad1083` and order variant `6bc30f5e` also execute. The order variant saves
false from `data-unvisited` between the first two writes, then ignores a later true
`data-closed` read. Its final write retains false. Four raw positives, four typed
refusals, eight structural refusals and two budget rows cover source order, direct
calls, invalid names/receivers, leaks, reuse and the second feeding-read obligation.
All historical raw bodies remain unchanged.

**1d76a4cc** lets the early power-range guard request existing bounded
subdivision when its range is a mixed enclosure. The zero-base witness visits
1, 4 and 7: `(i % 5) - 1` is 0, 3 and 1, although its enclosure includes -1.
The unit-base witness visits 4, 8 and 12: `(i % 5) - 3` is 1, 0 and -1,
although its enclosure includes unproved bases. Existing exact scalar powers prove
every accepted subdivision. Actual poles, general powers, fractional intermediates,
later stores and overlapping reloads still refuse. Complete reload/store census,
independent reload-gap reproof, restored bounds, work budget and actual-write replay
are unchanged. Five raw admissions, five raw refusals and ten source controls were
added; historical expectations remain unchanged. The parent reviewed this change.

Focused validation on the devbox:

- Exact `ctcompile_host_contract`: **1/1 PASS, 2.53 s test / 2.54 s total**.
- Three selected saved-throw sources: **48 native executions, 76 refusals and
  24 Node/VM observations PASS**, both browser providers and optimization policies,
  GCC/Clang and explicit/deduced C++. Preflight admitted three positives and refused
  ten controls under both policies. Execution selected the promoted live fixture.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.61 s test / 2.62 s total**.
- Exact `Analysis/Escape/escape-claims/power-index-overwrite.test`:
  **1/1 PASS, 0.18 s; 355 other cases excluded**.
- `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**.
  Scoped formatting, Node/Python syntax, scratch `bash -n` and diff checks pass.
  All focused gates passed first run.
- All six changed code/test hashes match the devbox. All 24 generated C++ files
  retain the saved read between the first two protected writes, its Boolean through
  ignored reads to the final write, and the original saved exception. Script/VM
  and nullable fallback checks plus standalone/linked symbol checks pass.

Preserved 233 historical iterator bodies, 86 metadata rows, 84 saved cases,
175 retained saved refusal bodies and 84 historical oracle constructions.
Local source checks cover 12 positive Node observations and 13 syntax checks.
All 294 historical escape source bodies/CHECKs and historical raw bytes remain
unchanged. Local checks cover 304-source syntax and ten exact Node write/read,
own-key and original-child identity witnesses plus uninstrumented outcomes.
Before the escape change, all ten scratch child sites were `escapes:stored`;
the focused lit checks now prove three confined and retain seven stored verdicts.

Builds used `tools/remote-build.sh` with explicit targets under the shared lock.
Host targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-host-contract`,
`ctcompile-test-native-reference`. Escape targets: `ctjs-opt`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims`,
`ctcompile-test-type-oracle`. CTests used exact anchored names with
`--output-on-failure --no-tests=error`; lit used the generated build configuration
and the exact anchored filter above. Source selection used `throw_gate_mutable.py`
in the evidence folder. Exact commands, selected harnesses, source snapshots,
Node witnesses, logs and checksums are retained in
`../test-results/2026-09-23-between-write-read-values/` relative to the monorepo.

Initial Linux executable/CLI/loop checks inspected 15 accessible processes, with
60 executable-link errors; Windows CIM inspected 343 processes. Final checks
inspected 21 accessible Linux and 344 Windows processes, with 61 Linux errors.
No actual Claude matches appeared; status stayed uncertain and concurrent-agent
area rules remained in force. No browser/shared implementation edits, local C++
builds, pushes or history rewrites occurred.

Full CTest/compiler lit, complete iterator fixtures, unaffected native replays,
broad corpus/matrices, full Bootstrap, browser/WPT/test262, Windows and sanitizers
were skipped. No full-Bootstrap coverage gain is claimed.

**Next:** retained `unsupported-earlier-read-write-before-first-write`, SHA-256
`7ba9aa6f5a66b29ccf92e4a21e78afe2d0ffdbc040961edc539083a84d310090`,
refuses **DOM protected helper needs an independent inert-body proof** under both
policies. Preserve `hasAttribute('data-closed')` captured before the first protected
write, through both feeding writes and selectors for `data-after-terminal`, while
the original body exception wins. Single-write selector cleanup, nonterminal
exceptional state, multiple protected regions, implicit cleanup, nested custom
iterators, unguarded Bootstrap defaults, the application driver, full native
Bootstrap and general powers remain.
