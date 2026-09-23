# Handoff: continuing ctcompile

Synchronization now uses `~/Downloads/claude/AGENT-SYNC.jsonl`: read its `PROTOCOL`
records, current claims and recent `JOURNAL` records before editing. The shared
`~/Downloads/claude/agent-sync.py` writer appends locked JSON records. This format
migration preserved history. The latest native work and measured next boundary
are recorded below.

> NOTE (Claude, 2026-09-16): `ctcompile-v1` history was **reworded** while Codex
> was stopped - every unpushed commit from `f7966251` (origin) forward now has a
> `ctcompile(<area>): ...` message, but **the trees are byte-identical**, only
> messages and SHAs changed. The browser rounds 2-5 and five security fixes are
> integrated at the current tip. Pre-reword tips are kept as
> `ctcompile-v1-backup-premsg2` / `-premsg`. Full detail is in the
> `SESSION HANDOFF` journal in the history referenced by
> `~/Downloads/claude/AGENT-SYNC.jsonl`. Just branch from the current `ctcompile-v1`
> tip - nothing about the native work changed.

Native work continues on `ctcompile-v1` in the `compile-time-browser` monorepo.
The application driver remains incomplete; native compiler development uses
`ctjs-translate` and `ctjs-opt`. Build on the devbox using `tools/remote-build.sh`
under `/tmp/ctbrowser-devbox-build.lock`, then run the local formatter before
committing. There is no CI. Do not build on the small local machine.

## Iterator-close feeding-read selection and invariant shift bases, 2026-09-23 UTC

Resumed clean **0ca10958** and retained `005c36e5`. **8768efa0** selects the
read feeding either initial protected write from its actual operand, preserving
a later argument read in the existing saved-value and complete-use census.
Both snapshots, source order/guard, typed DOM/Style reproof, suppression, original
body exception and budgets remain. The unchanged original, getter and order
variant execute. An independent review found no native issues.

**0241f5fd** proves invariant shift bases with varying counts through existing
bounded subdivision and ordered scalar bitwise conversion. Five new source child
sites become confined; five controls remain stored. Signed shifts, modulo-32
counts, bounded integral intermediates, reload/store checks, independent gaps and
actual-write replay remain. Two historical raw refusal inputs now have exact
admission assertions; all 324 historical source bodies/CHECKs are unchanged.

Focused validation passes: 26 source preflight checks, 48 existing native assertion
binaries, the final refusal, exact host/arrays CTests and one power-index lit case.
The interrupted fail-fast native run had reached its final manifest after 24
Node/VM observations and 75 preceding refusals; recovery verified the existing
binaries and final refusal without recompilation. Full formatting passes
**1126 C++, 157 Python, 114 web files**; all six code/test hashes match the devbox.
Three parallel tasks survived interruptions through saved candidates and artifacts.
No browser implementation changed; Claude remained uncertain. Full suites skipped.

**Next native boundary:** retained
`unsupported-saved-read-through-final-write-argument-read`, source `40cadb13`,
refuses **DOM protected helper needs an independent inert-body proof** under both
policies. Preserve an earlier saved read through a later argument read inside the
final write, while the original body exception wins. Single-write selector cleanup,
nonterminal exceptional state, multiple protected regions, implicit cleanup,
nested custom iterators, unguarded Bootstrap defaults, the application driver,
full native Bootstrap, general powers and legacy SCF retention remain unfinished.
No full-Bootstrap coverage gain is claimed.

[Exact checks and next boundary](handoff/2026-09-23-feeding-read-selection.md).

## Saved reads inside iterator-close arguments and invariant numerators, 2026-09-23 UTC

Resumed clean **254fed05** and retained inside-first-write source `9533d479`.
After interruption, reused the saved compiler build, preflight and agent edits.
**8bd4c58b** retains completed earlier argument reads in the existing saved-value
and complete-use census. The final read still feeds its pending first or second
write. Source order/guard, typed DOM/Style reproof, suppression, original body
exception and budgets remain. The unchanged original, getter and order variant
execute with distinct saved and feeding snapshots.

**9e5ee141** proves invariant numerators with varying divisors through existing
bounded subdivision and ordered scalar division/remainder. For example,
`6 / ((i % 3) + 1)` writes keys 6, 3, 2, 6, 3, 2, 6. Exact bounded arithmetic,
nonzero divisors, integral quotients, reload/store checks, independent reload gaps
and actual-write replay remain. Four new source child sites become confined;
six retained/saved/overlap/pole/fractional controls stay stored.

Focused checks pass: **48 native executions, 76 refusals, 24 Node/VM observations**,
exact host/arrays CTests and one power-index lit case. All six final code/test
hashes match the devbox; 24 generated C++ files retain the saved Boolean and
original exception without Script/VM or nullable fallback. Full formatting passes
**1126 C++, 157 Python, 114 web files**. Parallel source/raw/escape work survived
interruption; an independent reviewer found no native issues. Corrected two new
SCF test assertions that wrongly expected the legacy region-free retention census
to be complete; exact contents assertions and production remained unchanged.
Full suites were skipped. No browser implementation changed; Claude stayed uncertain.

**Next native boundary:** retained
`unsupported-feeding-read-before-saved-read-inside-first-write`, source `005c36e5`,
refuses **DOM protected helper needs an independent inert-body proof** under both
policies. Preserve the feeding read when a later argument read is saved for a
subsequent write, while the original body exception wins. Single-write selector
cleanup, nonterminal exceptional state, multiple protected regions, implicit
cleanup, nested custom iterators, unguarded Bootstrap defaults, the application
driver, full native Bootstrap and general powers remain. SCF contents support
does not establish a complete legacy retention census. No full-Bootstrap coverage
gain is claimed.

[Exact checks and next boundary](handoff/2026-09-23-inside-write-read-values.md).

## Saved reads before iterator-close writes and exact product distances, 2026-09-23 UTC

Resumed clean **ec34d448** and retained before-first-write source `7ba9aa6f`.
**e4f19f95** uses the existing standalone-read census whenever no feeding write
is pending. A Boolean captured before the first protected write survives both
feeding writes, selectors and later writes. Pending feeding-read obligations,
complete use checks, typed DOM/Style reproof, source guard/order, original saved
exception and budgets remain. The unchanged original, getter and order variant
execute; repeated calls preserve the earlier false/true snapshot.

**9854d3a8** reuses the exact endpoint-pair representation for multiplication
with at most two input values. At visits 0 and 2, `(i - 1) * 4294967295` has two
bounded signed results whose distance exceeds the scalar bound. That distance
is an internal stride; every source product still needs the existing scalar proof.
Negative factors, reload gaps and saved identity pass; actual unbounded/fractional
intermediates, overlapping reloads and later stores remain refused.

Focused checks pass: **48 native executions, 76 refusals, 24 Node/VM observations**,
exact host/arrays CTests and one power-index lit case. All six final code/test
hashes match the devbox; all 24 generated C++ files retain the early Boolean and
original exception without Script/VM or nullable fallback. Full formatting passes
**1126 C++, 157 Python, 114 web files**. Three agents investigated source/raw/escape
work before rate limits; the parent completed implementation, review and gates.
One new host order assertion and one new escape-test variable collision were
corrected; production stayed unchanged after first gates. Full suites were skipped.
No browser implementation changed; Claude availability stayed uncertain.

**Next native boundary:** retained `unsupported-earlier-read-inside-first-write`
source `9533d479` refuses **DOM protected helper needs an independent inert-body
proof** under both policies. Preserve the saved read evaluated inside the first
write's argument list, separately from that write's feeding read, while the original
body exception wins. Single-write selector cleanup, nonterminal exceptional state,
multiple protected regions, implicit cleanup, nested custom iterators, unguarded
Bootstrap defaults, the application driver, full native Bootstrap and general
powers remain. No full-Bootstrap coverage gain is claimed.

[Exact checks and next boundary](handoff/2026-09-23-before-first-write-read-values.md).

## Saved reads between iterator-close writes and mixed power ranges, 2026-09-23 UTC

Resumed clean **9d3ddc90** and retained before-second-write source `40e597cd`.
**b08d5bd8** distinguishes standalone reads after the first protected write from
the read feeding the second write. The existing saved-value slots and complete
use census now preserve that earlier Boolean through both selectors and later
writes. Pending feeding-write proof, typed DOM/Style reproof, source guard/order,
original saved exception and budgets remain. The unchanged original, getter and
order-sensitive variant execute.

**1d76a4cc** reuses bounded whole-key subdivision when a mixed power range
includes unvisited bases or exponents. At visits 1, 4 and 7, `0 ** ((i % 5) - 1)`
uses exponents 0, 3 and 1; its enclosing range includes an unvisited negative
exponent. Every accepted subdivision still requires the existing scalar identity
proof. Actual poles, general powers and fractional intermediates stay refused;
complete reload/store checks, independent reload gaps and actual-write replay remain.

Focused checks pass: **48 native executions, 76 refusals, 24 Node/VM observations**,
exact host/arrays CTests and one power-index lit case. All six changed code/test
hashes match the devbox; 24 generated C++ files retain the earlier Boolean and
original exception without Script/VM or nullable fallback. Full formatting passes
**1126 C++, 157 Python, 114 web files**. Three agents worked on source/raw/escape
items; the parent completed rate-limited source/raw work. The escape agent
independently reviewed native production. All focused gates passed first run;
full suites were skipped. No browser implementation changed; Claude stayed uncertain.

**Next native boundary:** retained `unsupported-earlier-read-write-before-first-write`
source `7ba9aa6f` refuses **DOM protected helper needs an independent inert-body
proof** under both policies. Preserve the Boolean captured before the first
protected write, through both feeding writes, selectors and later writes, while
the original body exception wins. Single-write selector cleanup, nonterminal
exceptional state, multiple protected regions, implicit cleanup, nested custom
iterators, unguarded Bootstrap defaults, the application driver, full native
Bootstrap and general powers remain. No full-Bootstrap coverage gain is claimed.

[Exact checks and next boundary](handoff/2026-09-23-between-write-read-values.md).

## Reads before iterator-close selectors and scalar-bound refinement, 2026-09-23 UTC

Resumed clean **3111e52e** and retained before-first-selector source `b3f9a6ca`.
**795982ce** starts the existing standalone read census after the second protected
write. A saved `hasAttribute` Boolean can now precede the first `matches`, survive
both selectors and intervening writes, and feed one later write. Initial feeding
writes, complete use checks, typed DOM/Style reproof, source guard/order, saved body
exception and budgets remain. The unchanged original, getter and order variant run.

**27ae4a64** reuses bounded whole-key subdivision when a mixed index enclosure
makes an exact scalar transfer fail. At visits 0, 3 and 6, `i % 5` produces 0, 3
and 1; adding 4294967292 is bounded at every visit, although the enclosing residue
4 would exceed the bound. Exact visited arithmetic, reload/store checks, independent
reload gaps, restored bounds and actual-write replay remain required. Seven raw
admissions, four raw refusals and nine source controls cover sums, products,
signed differences, retained children and unsupported actual intermediates.

Focused checks pass: **48 native executions, 76 refusals, 24 Node/VM observations**,
exact host/arrays CTests and one power-index lit case. All six changed code/test
hashes match the devbox; 24 generated C++ files retain the earlier Boolean and
original exception without Script/VM or nullable fallback. Full formatting passes
**1126 C++, 157 Python, 114 web files**. Three agents began source/raw/escape work;
the parent finished rate-limited source/escape tasks. The raw agent independently
reviewed native production. All focused gates passed first run; full suites were
skipped. No browser implementation changed; Claude availability stayed uncertain.

**Next native boundary:** retained `unsupported-earlier-read-write-before-second-write`
source `40e597cd` refuses **DOM protected helper needs an independent inert-body
proof** under both policies. Preserve the Boolean captured after the first protected
write but before the second, through both selectors and later writes, while the
original body exception wins. Single-write selector cleanup, nonterminal exceptional
state, multiple protected regions, implicit cleanup, nested custom iterators,
unguarded Bootstrap defaults, the application driver, full native Bootstrap and
general powers remain. No full-Bootstrap coverage gain is claimed.

[Exact checks and next boundary](handoff/2026-09-23-before-first-selector-values.md).

## Saved reads across iterator-close selectors and varying addition, 2026-09-23 UTC

Resumed clean **97222f8e** and retained before-selector read source `abc08f6e`.
**4b063cb7** preserves a standalone `hasAttribute` Boolean across a later
`matches` call and ignored read for one protected write. Removing the unnecessary
terminal-selector state reuses the existing complete read/use census. Original
guard/order, typed DOM/Style reproof, saved body exception and budgets remain.
The unchanged original, getter and order-sensitive variant execute.

Parallel **388db8e0** extends bounded whole-key refinement to numeric addition,
using the existing exact scalar sum proof. String concatenation, fractional and
unbounded intermediates remain refused; complete reload/store checks, independent
reload gaps and actual-write replay remain. Two historical raw `i/2+i` refusals
now admit unchanged after exact Node key/read/own-key/identity witnesses.

Focused checks pass: **48 native executions, 76 refusals, 24 Node/VM observations**,
exact host/arrays CTests and one power-index lit case. All seven final code/test
hashes match the devbox; 24 generated C++ files retain the earlier Boolean and
original exception without Script/VM or nullable fallback. Full formatting passes
**1126 C++, 157 Python, 114 web files**. Corrected one new raw order assertion and
one new escape-fixture SSA-name collision; production was unchanged after the
first gates. Three agents worked in parallel before rate limits; the parent
completed their saved work, review, integration and validation. Full suites were
skipped; no browser implementation changed. Claude availability stayed uncertain.

**Next native boundary:** retained `unsupported-earlier-read-write-before-first-selector`
source `b3f9a6ca` refuses **DOM protected helper needs an independent inert-body
proof** under both policies. Preserve the `hasAttribute('data-closed')` Boolean
captured before the first selector, across both selectors and intervening writes,
for `data-after-terminal`, while the original body exception wins. Single-write
selector cleanup, nonterminal exceptional state, multiple protected regions,
implicit cleanup, nested custom iterators, unguarded Bootstrap defaults, the
application driver, full native Bootstrap and general powers remain.
No full-Bootstrap coverage gain is claimed.

[Exact checks and next boundary](handoff/2026-09-23-before-selector-read-values.md).

## Terminal iterator-close selector values and varying multiplication, 2026-09-23 UTC

Resumed clean **78aacd8e** and retained selector-value source `f3f1e6c6`.
**8fcad4df** preserves an earlier terminal `matches` Boolean for one protected
write. It reuses the complete read/use census and literal selector validation;
cloning retains source order/guard, typed DOM/Style reproof and the original saved
body exception. The unchanged original, getter and order-sensitive variant execute.

Parallel **7ad4962a** reuses bounded whole-key subdivision for varying
multiplication, preserving exact scalar products, intermediate bounds, complete
reload/store census, independent reload gaps and actual-write replay. Eighteen raw
and eleven source controls cover interior extrema, saved identity and refusals.
All historical raw bytes and 263 source bodies/CHECKs remain unchanged.

Focused checks pass: **48 native executions, 76 refusals, 24 Node/VM observations**,
exact host/arrays CTests and one power-index lit case. Seven final hashes match
the devbox; all 24 generated C++ files retain the saved selector Boolean and
original exception without Script/VM or nullable fallback. Full formatting passes
**1126 C++, 157 Python, 114 web files**. All gates passed first run. Three parallel
agents completed source/raw/escape work and independently reviewed native production.
The interrupted iteration resumed saved work without replaying completed checks.
Full suites were skipped; no browser implementation changed. Claude stayed uncertain.

**Next native boundary:** retained `unsupported-terminal-earlier-read-write-before-selector`
source `abc08f6e` refuses **DOM protected helper needs an independent inert-body
proof** under both policies. Preserve the `hasAttribute('data-closed')` Boolean
captured immediately before the second selector through that selector and an
ignored read, then feed `data-after-terminal` while the original exception wins.
Single-write selector cleanup, nonterminal exceptional state, multiple protected
regions, implicit cleanup, nested custom iterators, unguarded Bootstrap defaults,
the application driver, full native Bootstrap and general powers remain.
No full-Bootstrap coverage gain is claimed.

[Exact checks and next boundary](handoff/2026-09-23-terminal-selector-values.md).

## Earlier terminal iterator-close read values and varying subtraction, 2026-09-23 UTC

Resumed clean **6b908f6b** and retained earlier-read source `378326b6`.
**9ba39cb4** retains an earlier terminal `hasAttribute` Boolean across
ignored reads and selectors for one later `setAttribute`. The existing complete
use census rejects leaks and reuse; typed DOM/Style reproof, source guard/order,
saved exception and budgets remain. The unchanged original, getter and reordered
read variant execute.

Parallel **ad45ffe5** reuses bounded whole-key subdivision for varying
subtraction operands, preserving source order and existing exact scalar transfers.
Complete reload/store checks, independent reload-gap proof and actual-write replay
remain. Eight source controls were added; all 255 historical source bodies and
expectations are unchanged. Two historical raw `i-i` refusals now admit after
exact Node write/read/own-key/retained-identity witnesses.

Focused checks pass: **48 native executions, 76 refusals, 24 Node/VM observations**,
exact host/arrays CTests and one power-index lit case. Seven final hashes match
the devbox; all 24 generated C++ files preserve the earlier Boolean and original
exception without Script/VM or nullable fallback. Full formatting passes **1126
C++, 157 Python, 114 web files**. Three parallel agents saved their work; rate
limits interrupted raw/escape completion and final source review, which the parent
finished. The source agent found no defect in the native production diff. Full
suites were skipped; no browser implementation changed. Claude status stayed uncertain.

**Next native boundary:** retained `unsupported-terminal-selector-write-earlier-value`
source `f3f1e6c6` refuses **DOM protected helper needs an independent inert-body
proof** under both policies. Preserve the saved second `matches` Boolean across
two ignored `hasAttribute` calls for `data-after-terminal`, while the original
body exception wins. Reads before terminal mode, single-write selector cleanup,
nonterminal exceptional state, multiple protected regions, implicit cleanup,
nested custom iterators, unguarded Bootstrap defaults, the application driver,
full native Bootstrap and general powers remain. No full-Bootstrap coverage gain
is claimed.

[Exact checks and next boundary](handoff/2026-09-23-earlier-terminal-read-values.md).

## Terminal iterator-close read values and correlated powers, 2026-09-23 UTC

Resumed clean **dde07a16** and retained read-to-write source `b1b67287`.
**0024c099** preserves a final `hasAttribute` Boolean inside the pending
`setAttribute` after terminal selectors. Existing complete use checks, typed
DOM/Style reproof, source guard/order, saved exception and budgets remain.
The unchanged original, getter and reordered-read variant execute.

Parallel **81a3673d** uses bounded whole-key subdivision for correlated powers,
requiring existing exact scalar identities at every accepted subrange. Complete
reload/store checks, independent reload gaps and actual-write replay remain.
Eleven raw and ten source controls were added; all 245 historical source bodies
are unchanged. Two raw refusals and CHECK109 were promoted after exact Node
write/read/own-key/identity witnesses. General powers remain unproved.

Focused checks pass: **48 native executions, 76 refusals, 24 Node/VM observations**,
exact host/arrays CTests and one power-index lit case. Seven final hashes match
the devbox; all 24 generated C++ files retain the final read-to-write Boolean
without Script/VM or nullable fallback. Full formatting passes **1126 C++,
157 Python, 114 web files**. Two new SCF expectation labels were corrected;
production was unchanged. The parent completed rate-limited source/raw work;
the escape agent independently reviewed the native change. Full suites were
skipped, and no browser implementation changed. Claude status stayed uncertain.

**Next native boundary:** retained `unsupported-terminal-read-write-earlier-value`
source `378326b6` refuses **DOM protected helper needs an independent inert-body
proof** under both policies. Preserve the earlier saved `hasAttribute` Boolean
across an intervening ignored read, then feed it to `data-after-terminal`, while
the original body exception wins. Single-write selector cleanup, nonterminal
exceptional state, multiple protected regions, implicit cleanup, nested custom
iterators, unguarded Bootstrap defaults, the application driver, full native
Bootstrap and general powers remain. No full-Bootstrap coverage gain is claimed.

[Exact checks and next boundary](handoff/2026-09-23-terminal-read-values.md).

## Writes after terminal iterator-close reads and varying divisors, 2026-09-23 UTC

Resumed clean **8d66dca6** and retained terminal-read/write source `ce6d790d`.
**c00c569d** lets the existing suffix-write proof preserve writes after unused
terminal reads and selectors. Every write retains exact suppression, typed
DOM/Style validation, complete use checks, source order and the original guard.
The original saved body exception still wins. The unchanged source, getter and
reordered-read variant execute.

Parallel **8a013b5e** reuses bounded whole-key subdivision for varying division
and remainder divisors. Accepted subranges require the existing exact singleton
arithmetic proof; complete reload/store checks, independent reload-gap proof and
actual-write replay remain. Ten source controls were added; all 235 historical
source bodies and expectations are unchanged. Two historical raw refusals now
admit after exact Node write/read/own-key/retained-identity witnesses.

Three selected sources pass **48 native executions, 76 refusals and 24 Node/VM
observations**. Exact host/arrays CTests and the selected power-index lit case pass.
Seven final source hashes match the devbox; all 24 generated C++ files retain the
terminal reads followed by the write without Script/VM or nullable fallback.
Full formatting passes **1126 C++, 157 Python, 114 web files**. All three agents
saved their changes before rate limits; the parent completed integration and
review. Full suites were skipped; no browser implementation changed. Claude
availability remained uncertain, so concurrent-agent area rules stayed in force.

**Next native boundary:** retained `unsupported-terminal-read-write-value`
source `b1b67287` refuses **DOM protected helper needs an independent inert-body
proof** under both policies. Preserve the final `hasAttribute('data-unvisited')`
Boolean as the value of `setAttribute('data-after-terminal', ...)`, together with
the original saved body exception. Single-write selector cleanup, nonterminal
exceptional state, multiple protected regions, implicit cleanup, nested custom
iterators, unguarded Bootstrap defaults, the application driver, full native
Bootstrap and general powers remain. Wider arithmetic proofs retain the existing
work budget. No full-Bootstrap coverage gain is claimed.

[Exact checks and next boundary](handoff/2026-09-23-terminal-writes-varying-divisors.md).

## Terminal iterator-close read sequences and varying shift counts, 2026-09-23 UTC

Resumed clean **71e24614** and retained second-read source `5cd57a59`.
**294f2e21** retains bounded sequences of unused `hasAttribute` reads and
`matches` calls after the terminal selector. Each read enters the existing
complete use census immediately; only selectors need exact suppression. Source
order/guard, typed DOM/Style reproof, saved Boolean ownership, budgets and the
prohibition on subsequent writes remain. The unchanged original, getter, missing
read, reordered reads, four-read and alternating read/selector sources execute.

Parallel **7e081e79** reuses bounded whole-key subdivision for varying shift
counts. Existing singleton transfers retain operand order, conversion and count
masking, complete reload/store census, independent reload-gap proof and actual
write replay. Eleven source controls were added. All 224 historical source bodies
survive; exact source 198 and two historical raw refusals now admit after Node
write/read/own-key/identity witnesses.

Six selected sources pass **96 native executions, 120 refusals and 48 Node/VM
observations**. Exact host/arrays CTests and the selected power-index lit case pass.
Seven final source hashes match the devbox; all 48 generated C++ files retain the
terminal operation sequence after every write without Script/VM or nullable
fallback. Full formatting passes **1126 C++, 157 Python, 114 web files**.
Two subagents hit rate limits; the parent completed their work and another agent
reviewed both changes. Full suites were skipped; no browser implementation changed.
Linux process checks were incomplete, so concurrent-agent area rules remained in force.

**Next native boundary:** retained `unsupported-terminal-read-write` source
`ce6d790d` refuses **DOM protected helper needs an independent inert-body proof**
under both policies. Preserve the `setAttribute('data-after-terminal', false)`
after both terminal reads, together with the original saved body exception.
Single-write selector cleanup, nonterminal exceptional state, multiple protected
regions, implicit cleanup, nested custom iterators, unguarded Bootstrap defaults,
the application driver, full native Bootstrap and general powers remain. Wider
shift/mask proofs retain the existing work budget. No full-Bootstrap coverage
gain is claimed.

[Exact checks and next boundary](handoff/2026-09-23-terminal-read-sequences.md).

## Reads after terminal iterator-close selectors and varying masks, 2026-09-23 UTC

Resumed clean **b9a880eb** and retained terminal-selector/read source `54c46eea`.
**b0745168** preserves its final `hasAttribute` after the second `matches` call.
Unused terminal selectors enter the existing suppression and complete use census,
leaving the final read slot available. Following writes still refuse; source
order/guard, typed DOM/Style reproof, saved Boolean ownership and budgets remain.
The original source, getter, missing-read and order-sensitive variants execute.

Parallel **ecdb8d0e** reuses bounded whole-key subdivision when both bitwise
mask operands vary. Each accepted subrange still requires the existing singleton
transfer, complete reload/store census, independent reload-gap proof and actual
write replay. Twenty raw and fourteen source controls were added; all 210 historical
source bodies/expectations remain unchanged. Eight historical raw refusals now
have exact admission checks supported by Node write/read/identity witnesses.

Four selected sources pass **64 native executions, 96 refusals and 32 Node/VM
observations**. Exact host/arrays CTests and the selected power-index lit case pass.
Eight final source hashes match the devbox; all 32 generated C++ files retain the
final read after both selectors and every write, without Script/VM or nullable
fallback. Full formatting passes **1126 C++, 157 Python, 114 web files**.
The initial array gate found six stale refusals and one new test's missing scalar
label; corrected expectations pass without a production change. Full suites were
skipped; no browser/runtime implementation changed.

**Next native boundary:** retained `unsupported-terminal-match-second-read`
source `5cd57a59` refuses **DOM protected helper needs an independent inert-body
proof** under both policies. Preserve both final `hasAttribute` calls after the
second selector and the original saved body exception. Single-write selector
cleanup, nonterminal exceptional state, multiple protected regions, implicit
cleanup, nested custom iterators, unguarded Bootstrap defaults, the application
driver, full native Bootstrap and general powers remain. Wider bitwise proofs
retain the existing work budget. No full-Bootstrap coverage gain is claimed.

[Exact checks and next boundary](handoff/2026-09-23-terminal-selector-read-close.md).

## Terminal iterator-close selectors and singleton bitwise masks, 2026-09-23 UTC

Resumed clean **3fe78026** and retained terminal-selector source `5f783ab2`.
**76d69d1e** keeps its second `matches` call after all close writes using the
existing exact unused-result suppression and complete typed DOM/Style proof.
The selector result remains unobserved; literal validation, original guard/order,
private callable/frame checks, saved Boolean ownership and budgets remain.
The original source, getter and order-sensitive variant execute unchanged.

Parallel **82f62403** reuses the singleton range proof for AND/OR/XOR masks,
including commuted operands. Existing bitwise conversions, complete reload/store
census, independent reload-gap proof and actual-write replay remain. Seventeen
raw and eleven source controls were added; all 199 historical source bodies and
expectations are unchanged.

Three selected sources pass **48 native executions, 76 refusals and 24 Node/VM
observations**. Exact host/arrays CTests and the selected power-index lit case pass.
Seven final source hashes match the devbox; all 24 generated C++ files retain both
selectors and the ordered writes, without Script/VM or nullable fallback.
Full formatting passes **1126 C++, 157 Python, 114 web files**. Review corrected
one raw refusal to exercise the new guard; the final host rebuild passes.
Full suites were skipped; no browser/runtime implementation changed.

**Next native boundary:** retained `unsupported-terminal-match-read` source
`54c46eea` refuses **DOM protected helper needs an independent inert-body proof**
under both policies. Preserve the final `hasAttribute` after the second selector,
used as the ignored close throw, together with the original saved body exception.
Single-write selector cleanup, nonterminal exceptional state, multiple protected
regions, implicit cleanup, nested custom iterators, unguarded Bootstrap defaults,
the application driver, full native Bootstrap, general powers and genuinely
varying bitwise masks remain. No full-Bootstrap coverage gain is claimed.

[Exact checks and next boundary](handoff/2026-09-23-terminal-selector-close.md).

## Terminal iterator-close reads and singleton shift counts, 2026-09-23 UTC

Resumed clean **3df6cedd** and retained terminal-read source `51fc580e`.
**0714925c** keeps a final unused `hasAttribute` after the close's selector and
ordered writes. The existing complete use census rejects observed results;
ordered cloning, exact write suppression, typed DOM/Style reproof, saved Boolean
ownership and budgets remain. The original source, getter, missing-read and
order-sensitive variants execute unchanged.

Parallel **b57811e7** reuses the singleton RHS range proof for signed, unsigned
and left shift counts. Existing conversion/masking, complete reload/store census,
independent reload-gap proof and actual-write replay remain. Fourteen raw and
eleven source controls were added; all 188 historical source bodies and
expectations are unchanged.

Four selected sources pass **64 native executions, 96 refusals and 32 Node/VM
observations**. Exact host/arrays CTests and the selected power-index lit case pass.
Seven final source hashes match the devbox; all 32 generated C++ files retain the
terminal read after every write, without Script/VM or nullable fallback.
Full formatting passes **1126 C++, 157 Python, 114 web files**. Full suites were
skipped; no browser/runtime implementation changed.

**Next native boundary:** retained `unsupported-terminal-postselector-match`
source `5f783ab2` refuses **DOM protected helper needs an independent inert-body
proof** under both policies. Preserve the second `matches` call after all writes,
used as the ignored close throw, together with the original saved body exception.
Single-write selector cleanup, nonterminal exceptional state, multiple protected
regions, implicit cleanup, nested custom iterators, unguarded Bootstrap defaults,
the application driver, full native Bootstrap and general powers remain.
No full-Bootstrap coverage gain is claimed.

[Exact checks and next boundary](handoff/2026-09-23-terminal-read-close.md).

## Ordered iterator-close suffixes and singleton divisors, 2026-09-23 UTC

Resumed clean **382d1677** and retained fourth-write source `c6857b84`.
**7e094e1b** applies the existing post-selector read/write proof to bounded
sequences. Each optional read feeds only its following write; complete use census,
source order/guard, exact write suppression, typed DOM/Style reproof, saved Boolean
ownership and budgets remain. Original source, getter, read-dependent fourth
write and six-write sequence execute unchanged.

Parallel **5f92f68c** proves division/remainder indices when a syntactically
varying divisor has one exact bounded value. Existing arithmetic, complete
reload/store census, independent reload-gap proof and actual-write replay remain.
Eighteen raw and eleven source controls were added; all 177 historical source
bodies and expectations are unchanged.

Four selected sources pass **64 native executions, 96 refusals and 32 Node/VM
observations**. Exact host/arrays CTests and the selected power-index lit case pass.
Seven source hashes match the devbox; all 32 generated C++ files retain the selector
and ordered suffix writes consuming actual read Booleans, without Script/VM or
nullable fallback. Full formatting passes **1126 C++, 157 Python, 114 web files**.
The first host gate exposed incomplete SSA renaming in new test construction;
corrected test setup passes, with production unchanged. Full suites were skipped.

**Next native boundary:** retained `unsupported-terminal-postselector-read`
source `51fc580e` refuses **DOM protected helper needs an independent inert-body
proof** under both policies. Preserve its final `hasAttribute` after all writes,
used as the ignored close throw, together with the original saved body exception.
Single-write selector cleanup, nonterminal exceptional state, multiple protected
regions, implicit cleanup, nested custom iterators, unguarded Bootstrap defaults,
the application driver, full native Bootstrap and general powers remain.
No full-Bootstrap coverage gain is claimed.

[Exact checks and next boundary](handoff/2026-09-23-ordered-suffix-close.md).

## Reads after iterator-close selectors and correlated division, 2026-09-23 UTC

Resumed clean **44416c8e** and retained post-selector read source `fcad547a`.
**2c05286e** preserves its final `hasAttribute` read after the selector and before
its consuming attribute write. The existing ordered clone and complete typed
DOM/Style reproof retain receiver/argument validation, exact write suppression,
private callable checks, saved Boolean ownership and budgets. The original
source, getter and false-read variant execute unchanged.

Parallel **325a1b09** reuses bounded whole-key subdivision when correlated
integer division has a coarse enclosure containing fractional quotients. Every
accepted subrange still requires exact arithmetic and own-array bounds; complete
reload/store census, independent reload-gap proof and actual-write replay remain.
Fifteen raw and ten source controls were added; all 167 historical source bodies
and expectations are unchanged.

Three selected sources pass **48 native executions, 84 refusals and 24 Node/VM
observations**. Exact host/arrays CTests and the selected power-index lit case pass.
Seven source hashes match the devbox; all 24 generated C++ files retain the selector,
then pass the actual attribute-read Boolean to the following write, with no
Script/VM protocol or nullable-scalar fallback. Full formatting passes
**1126 C++, 157 Python, 114 web files**. Full suites were skipped.

**Next native boundary:** retained `unsupported-fourth-postselector-effect`
source `c6857b84` refuses **DOM protected helper needs an independent inert-body
proof** under both policies. Preserve its fourth write after the post-selector
read and third write, together with the original saved body exception.
Single-write selector cleanup, nonterminal exceptional state, multiple protected
regions, implicit cleanup, nested custom iterators, unguarded Bootstrap defaults,
the application driver, full native Bootstrap and general powers remain.
No full-Bootstrap coverage gain is claimed.

[Exact checks and next boundary](handoff/2026-09-23-postselector-read-close.md).

## Selector results in iterator closes and correlated array bounds, 2026-09-23 UTC

Resumed clean **de58563d** and retained selector-result source `8038c1f8`.
**a78a11ed** preserves its Boolean as an ordinary ordered read feeding the final
attribute write. Expansion validates the literal through the public Style parser
with byte charging before lifting the read; complete typed DOM/Style reproof still
checks receiver identity, arguments and reentry. Final-write suppression, private
callable/frame checks, original saved Boolean ownership and budgets remain.
The original source, getter and false-result ordering variant execute unchanged.

Parallel **31649d7e** reuses bounded whole-key subdivision to prove array
indices whose coarse enclosure includes unreachable out-of-bounds values.
Complete reload/store census, independent reload-gap proof and actual-write
replay remain. Ten source and fourteen raw controls were added. Four historical
raw refusals and one source expectation were promoted after exact Node witnesses
confirmed their admissions; all 157 historical source bodies remain unchanged.

Three selected sources pass **48 native executions, 84 refusals and 24 Node/VM
observations**. Focused host/arrays CTests and the exact power-index lit case pass.
Eight final source hashes match the devbox; all 24 generated C++ files pass their
selector Boolean directly to the following write, with no Script/VM protocol or
nullable-scalar fallback. Full formatting passes **1126 C++, 157 Python, 114 web
files**. Full suites were skipped.

**Next native boundary:** retained `unsupported-third-postselector-read` source
`fcad547a` still refuses **DOM protected helper needs an independent inert-body
proof** under both policies. Preserve its additional `hasAttribute` read after
the selector and before the final write, together with the saved body exception.
Single-write selector cleanup, nonterminal exceptional state, multiple protected
regions, implicit cleanup, nested custom iterators, unguarded Bootstrap defaults,
the application driver, full native Bootstrap and general powers remain.
No full-Bootstrap coverage gain is claimed.

[Exact checks and next boundary](handoff/2026-09-23-selector-result-close.md).

## Writes after iterator-close selectors and power congruence, 2026-09-23 UTC

Resumed clean **1bf155c0** and retained third-write source `6d1b5000`.
**ca257905** preserves its final attribute write after the selector, in its own
exact unused-result suppression region. Complete typed DOM/Style reproof still
validates each receiver, literal attribute name and selector. Private callable
checks, result confinement, source order, saved Boolean ownership and budgets
remain. The original source, getter and order-sensitive literal-write variant
execute. A selector result consumed by the following write remains refused.

Parallel **f31c88dc** preserves the common congruence of a bounded base and one
when its exponent is zero or one. Existing exact division can then prove composed
array indices. Complete reload/store census, whole-key refinement and actual-write
replay remain. All 145 historical power source bodies/CHECKs survive; twelve
source controls were added. General powers remain separate.

Three selected sources pass **48 native executions, 80 refusals and 24 Node/VM
observations**. Focused host/arrays CTests and the exact power-index lit case pass.
Seven final hashes match the devbox; all 24 generated C++ files retain the selector
and following write, with no Script/VM protocol or nullable-scalar fallback.
Full formatting passes **1126 C++, 157 Python, 114 web files**. Full suites were
skipped.

**Next native boundary:** `unsupported-third-postselector-result` source
`8038c1f8` still refuses **DOM protected helper needs an independent inert-body
proof** under both policies. Preserve its selector Boolean for the subsequent
attribute write while retaining the saved body exception. An additional
post-selector `hasAttribute` producer (`fcad547a`), nonterminal exceptional state,
multiple protected regions, implicit cleanup, nested custom iterators, unguarded
Bootstrap defaults, the application driver and full native Bootstrap remain.
No full-Bootstrap coverage gain is claimed.

[Exact checks and next boundary](handoff/2026-09-23-postselector-close.md).

## Selector reads in iterator closes and zero/unit power exponents, 2026-09-23 UTC

Resumed clean **740279da** and retained selector source `f4005c21`.
**1f353fa9** keeps its final `matches` call after both close writes in an exact
unused-result suppression region. The existing public Style parser validates the
literal selector after charging its bytes; complete typed DOM/Style reproof,
private callable/frame checks, saved Boolean ownership and budgets remain.
The original source, getter and order-sensitive missing-read variant execute
unchanged. Invalid/computed selectors and ordinary throwing closes still refuse.

Parallel **8ba1a605** proves powers of varying bounded nonunit bases when the
exponent is exactly zero or one. It encloses the base and one, retaining existing
whole-key refinement, complete reload/store census and actual-write replay.
All 135 historical power source bodies/CHECKs survive; ten source cases were added.
General powers remain separate.

Three selected sources pass **48 native executions, 68 refusals and 24 Node/VM
observations**. Focused host/arrays CTests and the exact power-index lit case pass.
Nine final hashes match the devbox; all 24 generated C++ files retain a selector
call and contain no Script/VM protocol or nullable-scalar fallback. Full formatting
passes **1126 C++, 157 Python, 114 web files**. Full suites were skipped.

**Next native boundary:** `unsupported-after-selector-effect` source `6d1b5000`
still refuses **DOM protected helper needs an independent inert-body proof** in
both policies. Its third write follows the selector; preserve that order and the
saved Boolean while proving the complete exceptional sequence. Nonterminal
exceptional state, multiple protected regions, implicit cleanup, nested custom
iterators, unguarded Bootstrap defaults, the application driver and full native
Bootstrap remain unfinished. No full-Bootstrap coverage gain is claimed.

[Exact checks and next boundary](handoff/2026-09-23-selector-close.md).

## Ordered iterator close writes and tight odd-power bounds, 2026-09-23 UTC

Resumed clean **98a8f335** and retained two-write source `442af76b`.
**3098b880** keeps the second `setAttribute` in its own exact suppression region,
with its feeding read after the first write at the same source guard. Both writes
require complete typed DOM reproof and independent valid-name checks. Saved
Boolean snapshots, private callable/frame checks, confined results and budgets
remain. The original source, getter and order-sensitive missing-read variant
execute unchanged.

Parallel **a7342199** tightens the existing odd-power bound for negative-or-zero
bases. It no longer invents a positive-unit array index; even/zero exponents and
positive bases retain their wider bounds. Complete reload/store census and
actual-write replay remain. All 122 historical power bodies/CHECKs and raw cases
survive; thirteen source controls were added.

Three selected sources pass **48 native executions, 68 refusals and 24 Node/VM
observations**. Focused host/arrays CTests and the exact power-index lit case pass.
Seven final hashes match the devbox; 24 generated C++ files contain no Script/VM
protocol or nullable-scalar fallback. Full formatting passes
**1126 C++, 157 Python, 114 web files**. Full suites were skipped.

**Next native boundary:** selector-read source `f4005c21` still refuses
**DOM protected helper needs an independent inert-body proof** in both policies.
It calls `matches('[data-closed]')` after both close writes; preserve that evaluation
and the saved Boolean while proving exceptional behavior. Nonterminal exceptional
state, multiple protected regions, implicit cleanup, nested custom iterators,
unguarded Bootstrap defaults, the application driver and full native Bootstrap
remain. General powers need a separate arithmetic proof.

[Exact checks and next boundary](handoff/2026-09-23-second-postwrite-close.md).

## Post-write iterator close reads and singleton power bases, 2026-09-23 UTC

Resumed clean **00e7f1cb** and retained Boolean source `4def4ec7`.
**e7cbb817** keeps an optional trailing `hasAttribute` read after the protected
attribute write, under the same source guard. Prefix producers stay before the
write; exact suppression, private callable checks, saved exception ownership and
complete typed DOM reproof remain. The original source now executes unchanged,
including its saved Boolean snapshot; getter and missing-attribute variants pass.

Parallel **11d95228** reuses the existing invariant-base power proof when a
syntactically varying base has one proved value. Complete reload/store census,
exact scalar restrictions and actual-write replay remain. All 110 historical
power sources/CHECKs and raw cases are preserved; twelve source controls were added.

Three selected sources pass **48 native executions, 68 refusals and 24 Node/VM
observations**. Focused host/arrays CTests and the exact power-index lit case pass.
Seven final hashes match the devbox; 24 generated C++ files contain no Script/VM
protocol or nullable-scalar fallback. Full formatting passes
**1126 C++, 157 Python, 114 web files**. Full suites were skipped.

**Next native boundary:** `second-postwrite-effect` source `442af76b` still refuses
**DOM protected helper needs an independent inert-body proof** in both policies.
It performs a second attribute write after reading the first write's result;
retain both writes, their order and the original saved Boolean. Nonterminal
exceptional state, multiple protected regions, implicit cleanup, nested custom
iterators, unguarded Bootstrap defaults, the application driver and full native
Bootstrap remain. General powers still require a separate arithmetic proof.

[Exact checks and next boundary](handoff/2026-09-23-postwrite-close.md).

## Nonliteral iterator close throws and singleton power exponents, 2026-09-23 UTC

Resumed clean **ed41f1e5** and retained nonliteral mutable close `822a327b`.
**bdeaa5b9** validates the original ignored payload's definitions, dominance
and frame before replacing its value with undefined. All producers remain for
state transport and complete helper/DOM reproof. Exact suppression, private
closure/holder checks, saved exception and ordinary-close validation remain.
The original source now executes unchanged: close throws current count 13 while
the saved exception remains Number 3. Three derived cases cover a state-observing
attribute, a return getter and a comma-expression payload effect.

Parallel **425e7d47** reuses the existing invariant power proof when a varying
exponent has a proved singleton range. Complete reload/store census and replay
remain. All 101 historical power sources/CHECKs and raw cases are preserved;
nine source controls were added.

Four selected sources pass **64 native executions, 120 refusals and 16 Node/VM
observations**. Focused host/arrays CTests and the exact power-index lit case
pass. Seven final hashes match the devbox; 32 generated C++ files contain no
Script/VM protocol or nullable-scalar fallback. Full formatting passes
**1126 C++, 157 Python, 114 web files**. Full suites were skipped.

**Next native boundary:** Boolean source `4def4ec7` still refuses
**DOM protected helper needs an independent inert-body proof** in both policies.
Its close writes an attribute, then reads it with `hasAttribute` as the ignored
throw payload. Preserve that post-write producer and the saved Boolean while
extending the protected effect ordering proof. Nonterminal exceptional state,
multiple protected regions, implicit cleanup, nested custom iterators, unguarded
Bootstrap defaults, the application driver and full native Bootstrap remain.

[Exact checks and next boundary](handoff/2026-09-23-nonliteral-close.md).

## Mutable throwing iterator closes and negative varying powers, 2026-09-23 UTC

Resumed clean **011b60ec** and retained mutable throwing closes `cd9eb979` and
`83b34eed`. **f94cc18d** moves the existing terminal literal throw conversion
before mutable-state body validation. Frame validation, private state transport,
original/final observer checks, exact suppression and complete typed DOM reproof
remain. Both original sources execute unchanged. A new witness confirms close
sees count 13 while the saved exception remains Number 3; conditional getter
`2b727264` is now executed too.

Parallel **19c4e507** proves bounded negative varying exponents when the
signed-unit base lattice excludes zero. Its output retains the nonzero-unit
congruence, preserving invariant reloads from the unwritten middle slot.
Complete reload/store census and actual-write replay remain. All 89 historical
power source bodies/CHECKs and raw witnesses are unchanged; twelve source
controls were added.

Four selected sources pass **64 native executions, 104 refusals and 24 Node/VM
observations**. Focused host/arrays CTests and the exact power-index lit case
pass. Seven final code/test hashes match the devbox; 32 generated C++ files
contain no Script/VM protocol or nullable-scalar fallback. Full formatting
passes **1126 C++, 157 Python, 114 web files**. Full suites were skipped.

**Next native boundary:** nonliteral mutable close `822a327b` still refuses
**DOM iterator method must return one fresh own-field record** in both policies.
It throws current count 13 after its effects; the saved body exception must
remain Number 3. Prove the ignored scalar throw while retaining its producers
and ordinary-close checks. Nonterminal exceptional state, multiple protected
regions, implicit cleanup, nested custom iterators, unguarded Bootstrap defaults,
the application driver and full native Bootstrap remain unfinished.

[Exact checks and next boundary](handoff/2026-09-23-mutable-throwing-close.md).

## Conditional iterator closes and powers with two varying operands, 2026-09-23 UTC

Resumed clean **b73eba63** and retained conditional primitive/throwing closes
`1fe1a3e8`/`fd04cbc3`. **7b28e708** admits both unchanged bodies by preserving
Boolean facts shared by every path to a selected loop exit. Unknown or conflicting
facts remain unknown; facts are collected before inactive-slot padding and
materialized only inside the selected continuation. Source producers, saved
Boolean snapshots, suppression and ordinary-close result validation remain.

Parallel **0d1ce73b** proves powers with two varying operands only when the base
stays within `-1, 0, 1` and the exponent is a bounded nonnegative integer.
The existing reload/store census, whole-key refinement and actual-write replay
retain correlations and unvisited children. All 77 historical power sources and
CHECKs remain; an existing raw SCF body now proves unchanged, with twelve source
controls added.

The two conditional sources pass **32 native executions, 64 refusals and
16 Node/VM observations**. The affected mixed throw/return source separately
passes **16 executions, 40 refusals and 16 Node/VM observations**.
Focused host/arrays CTests and the exact power-index lit case pass. Seven final
code/test hashes match the devbox; 24 generated C++ files contain no Script/VM
protocol or nullable-scalar fallback. Full formatting passes
**1126 C++, 157 Python, 114 web files**. Full suites were skipped.

**Next native boundary:** mutable throwing-close sources `cd9eb979` and
`83b34eed` refuse **DOM helper has no complete return or yield** under both
policies. Preserve the saved Number and current private state while proving
a terminal close throw. Conditional getter `2b727264` now admits in both
policies, but was only compiled, not executed. Nonterminal exceptional state,
multiple protected regions, implicit cleanup, nested custom iterators, unguarded
Bootstrap defaults, the application driver and full native Bootstrap remain.

[Exact checks and next boundary](handoff/2026-09-23-conditional-close.md).

## Suppressed iterator return getters and signed-unit power ranges, 2026-09-23 UTC

Resumed clean **155fd28b** and original getter close `68ea7208`.
**807cd24b** admits its unchanged body: the return getter writes `data-closed=yes`
and throws 2, while the saved body exception remains Number 1. An exact own
accessor with no setter and a terminal throw uses the existing private close
callable proof. Literal payload, closure and holder confinement, source order,
exhaustion, suppression and complete DOM reproof remain required. A Boolean
snapshot getter also executes. Returning getters and ordinary closes still refuse.

Parallel **5d6676f4** proves positive integer powers over bases `-1, 0, 1`.
Even powers include the interior zero despite equal endpoint results; odd powers
preserve all three values. Existing scalar transfer, invariant reload/store census
and actual-write replay remain. All 65 historical source bodies survive; source 63
now proves unchanged, with twelve controls added. General powers remain refused.

The two affected getter sources pass **32 native executions, 48 refusals and
eight Node/VM observations**. Focused host/arrays CTests and the exact power-index
lit case pass. All seven final code/test hashes match the devbox; sixteen generated
C++ files contain no Script/VM protocol or nullable-scalar fallback. Full formatting
passes **1126 C++, 157 Python, 114 web files**. Full suites were skipped.

**Next native boundary:** conditional primitive close `1fe1a3e8`, then throwing
close `fd04cbc3`, still refuse **DOM iterator primitive close requires saved-throw
suppression** in both policies. Prove the correlation between completion and done
state without losing the saved Boolean or permitting an ordinary primitive close.
Mutable throwing-close state, nonterminal exceptional state, multiple protected
regions, implicit cleanup, nested custom iterators, unguarded Bootstrap defaults,
the application driver and full native Bootstrap remain unfinished.

[Exact checks and next boundary](handoff/2026-09-23-getter-close.md).

## Suppressed iterator close throws and two-value power indices, 2026-09-23 UTC

Resumed clean **6d6d026d** and original throwing close `fa88f9d6`.
**3d221b61** proves its terminal literal throw is unobserved under exact
saved-throw suppression. The close method must have one confined closure and
no symbolic callers; exhaustion removes only a proved unreachable close.
All preceding effects and payload producers remain for complete helper and
DOM reproof. The original body exception wins over the close exception, with
ordinary owned native values and public DOM calls. No browser code changed.

Parallel **abddf2d3** proves power indices with at most two possible
varying values through existing scalar transfer and endpoint bounds.
Operand order, signed bounds, complete reload/store checks and actual-write
replay remain. All 53 historical source bodies survive; source 13 now proves
unchanged, and twelve controls were appended.

Fifteen saved-throw sources pass **240 native executions, 116 refusals and
88 Node/VM observations**. After the final own-store check, the two affected
sources pass **32 native executions, 32 refusals and eight Node/VM observations**.
Focused host/arrays CTests and the exact power-index lit case pass. All seven
final code/test hashes match the devbox; 144 generated C++ files contain no
Script/VM protocol or nullable-scalar fallback. Full formatting passes
**1126 C++, 157 Python, 114 web files**. Full suites were skipped.

**Next native boundary:** original getter close `68ea7208` still refuses
**DOM helper has no complete return or yield** in both policies. Its return
property getter writes an attribute and throws during method lookup; preserve
those effects and the original saved exception under the existing suppression.
Derived conditional primitive/throwing closes `1fe1a3e8`/`fd04cbc3` still need
loop completion/done correlation. Mutable throwing-close state, nonterminal
exceptional state, multiple protected regions, implicit cleanup, nested custom
iterators, unguarded Bootstrap defaults, the application driver and full native
Bootstrap remain unfinished.

[Exact checks and next boundary](handoff/2026-09-23-throwing-close.md).

## Primitive iterator close results and negative-unit power indices, 2026-09-23 UTC

Resumed clean **563a6a4a** and the retained primitive close `5c738524`.
**9386caf3** admits its ignored literal result during a saved throw.
The existing done truth test supplies arm-local Boolean state; completion
elides a normal close only when its generated guard proves exhaustion.
Every remaining primitive close must retain exact unused-result suppression.
Normal break/return still requires a valid object result. Complete typed DOM
reproof, source effects, saved payloads and budgets remain. The original source
executes unchanged, alongside a Boolean snapshot with a primitive close result.

Parallel **beee2449** proves bounded negative-unit power indices through the
existing scalar power transfer. Even exponent strides retain one parity;
odd strides enclose both signs, including interior values when endpoints agree.
The unwritten gap, reload/store census and actual-write replay remain.
All 40 historical power source bodies/CHECKs survive; 13 controls were appended.

Focused host and arrays CTests and the exact power-index lit case pass.
Thirteen saved-throw sources pass **208 native executions, 84 refusals and
80 Node/VM observations**; three prior sources pass **48 native executions,
74 refusals, zero nonexecuted admissions and 24 Node/VM observations**.
Historical throw oracles agree on **20 observations per engine**. Nine final
code/test hashes match the devbox; 128 generated C++ files contain no Script/VM
protocol or nullable-scalar fallback.
Full formatting passes **1126 C++, 157 Python, 114 web files**, with a final
scoped Python check. All 233 historical iterator bodies and 86 metadata rows
remain unchanged. Full suites were skipped; no browser implementation changed.

**Next native boundary:** original throwing close `fa88f9d6` still refuses the
fresh own-field record requirement in both policies. Preserve the original body
exception while suppressing the close's throw and retaining its preceding effects.
The derived conditional primitive close `1fe1a3e8` still needs correlation between
loop completion and done state; getter `68ea7208` lacks a complete return/yield.
Nonterminal exceptional state, multiple protected regions, implicit cleanup,
nested custom iterators, unguarded Bootstrap defaults, the application driver
and full native Bootstrap remain unfinished.

[Exact checks and next boundary](handoff/2026-09-23-primitive-close.md).

## Terminal mutable iterator closes and zero-base power indices, 2026-09-23 UTC

Resumed clean **2046e48b** and the retained Number snapshots `7c0874c9` and
`8ee2040d`. **74f07bcc** proves that a protected close immediately followed
by the saved throw cannot expose its final private state. Close receives current
scalar state; the earlier throw payload remains a separate owning Number.
Pre-close scalar producers remain for complete typed DOM reproof, the attribute
call keeps suppression, and only the unused fresh result record is elided.
Both original sources now execute unchanged. A derived witness verifies close
sees count 13 while the throw still carries 3.

Parallel **8d789d96** proves zero-base power indices over bounded nonnegative
exponents, retaining `0 ** 0 === 1`. Existing primitive conversion, complete
reload/store census, endpoint bounds, actual-write replay and budgets remain.
Negative exponents and general powers still refuse. All 27 historical source
bodies survive, with one unchanged source promoted and 13 controls appended.

Focused host and arrays CTests and the exact power-index lit case pass.
Eleven saved-throw sources pass **176 native executions, 60 refusals and
72 Node/VM observations**; three prior sources pass **48 native executions,
74 refusals, zero nonexecuted admissions and 24 Node/VM observations**.
Historical throw oracles agree on **20 observations per engine**. Full formatting passes **1126 C++, 157 Python, 114 web files**.
Nine final code/test hashes match the devbox; 112 generated C++ files contain
no Script/VM protocol or nullable-scalar fallback. All 233 historical iterator
bodies and 86 metadata rows remain unchanged. Full suites were skipped;
no browser implementation changed.

**Next native boundary:** original primitive close `5c738524` still refuses the
fresh own-field record requirement in both policies. Its primitive result is
unobserved during saved-throw completion; prove that exact path without losing
normal-close validation or the original throw. Throwing close `fa88f9d6` has the
same structural refusal; getter `68ea7208` lacks a complete return/yield.
Nonterminal exceptional state, multiple protected regions, implicit cleanup,
nested custom iterators, unguarded Bootstrap defaults, the application driver
and full native Bootstrap remain unfinished.

[Exact checks and next boundary](handoff/2026-09-23-terminal-mutable-close.md).

## Mixed iterator completions and unit-base power indices, 2026-09-23 UTC

Resumed clean **e4e06268** and the retained mixed throw/return source `43ab2b64`.
**e2dee4cc** proves its three effectful exits with a bounded ordinal discriminator.
Each exit keeps its tuple until the continuation selects the live payload;
selected-arm use checks, source effects, saved pre-close values, frame cleanup
and complete DOM reproof remain. The unchanged original now executes throw,
return and exhaustion, including both Boolean payloads and throw precedence
when both guards are set. Generated code uses public DOM and ordinary ownership.

Parallel **43392dca** proves unit-base power indices such as `1 ** i`
through existing bounded conversion and power transfer. Operand order, reload
and store census, actual-write replay and budgets remain; general powers refuse.
Two historical raw source bodies now prove unchanged. All 17 historical power
source functions and CHECKs remain, with ten controls appended.

Focused host and arrays CTests and the exact power-index lit case pass.
Eight saved-throw sources pass **128 native executions, 24 refusals and
56 Node/VM observations**; three prior sources pass **48 executions, 76 refusals
and 24 Node/VM observations**, with no nonexecuted admissions. Five historical
throw oracles agree on **20 observations per engine**. Full formatting passes
**1126 C++, 157 Python, 114 web files**, followed by the final scoped C++ check.
Seven final hashes match the devbox; 88 generated C++ files contain no Script/VM
or nullable-scalar fallback. All 233 historical iterator bodies and 86 metadata
rows are preserved. Full suites were skipped; no browser implementation changed.

**Next native boundary:** original Number snapshot `7c0874c9`, then conditional
`8ee2040d`, still refuse **DOM iterator protected close needs a mutable-state
proof** in both policies. Preserve the saved pre-close Number while transporting
the close method's captured-state writes through suppression and complete reproof.
Throwing/primitive close methods, multiple protected regions, implicit cleanup,
nested custom iterators, unguarded Bootstrap defaults, the application driver
and full native Bootstrap remain unfinished.

[Exact checks and next boundary](handoff/2026-09-23-mixed-completion.md).

## Protected attribute reads and zero/unit power indices, 2026-09-23 UTC

Resumed clean **cdd54063** and retained conditional Boolean throw `9c10be3d`.
**bb25b945** proves one exact `hasAttribute` read feeding a protected
`setAttribute`. Both method lookups and the read retain their source order and
original guard. Complete DOM reproof establishes receiver and primitive facts;
a valid literal name and String/Boolean value discharge the write suppression
through existing lowering. The original conditional source now executes unchanged,
including saved pre-close payloads, both guard outcomes, exhaustion and reentry.
The prior literal witness remains, with an additional false-read variant.

Parallel **6e1a2eeb** proves power indices with invariant exponent zero or one
through existing bounded conversion and power transfer. Unit exponents preserve
the index lattice; zero exponents write only index one, including `0 ** 0`.
Complete reload/store census, actual-write replay and budgets remain unchanged.
General powers remain refused. Historical raw tests are preserved; a new
17-function source fixture checks exact observations and conservative refusals.

Focused host and arrays CTests and the exact power-index lit case pass.
Seven throw sources pass **112 native executions, 24 refusals and 40 Node/VM
observations**; three prior sources pass **48 executions, 78 refusals and
24 Node/VM observations**, with no nonexecuted admissions. Five historical throw
oracles agree on **20 observations per engine**. Full formatting passes
**1126 C++, 157 Python, 114 web files**. Eight final hashes match the devbox;
80 generated C++ files contain no Script/VM or nullable-scalar fallback.
All 233 historical iterator bodies and 86 metadata rows remain unchanged.
Full suites were skipped; no browser/shared implementation changed.

**Next native boundary:** unchanged mixed throw/return `43ab2b64` still refuses
**DOM helper completion observes an inactive value** in both policies. Prove its
selected completion slots without losing saved payloads or source effects.
Mutable Number snapshots, throwing/primitive close methods, multiple protected
regions, implicit cleanup, nested custom iterators, unguarded Bootstrap defaults,
the application driver and full native Bootstrap remain unfinished.

[Exact checks and next boundary](handoff/2026-09-23-protected-attribute-read.md).

## Compared iterator exits and additive offsets, 2026-09-23 UTC

Resumed clean **51f90cf6** and retained conditional Boolean throw `9c10be3d`.
**ba5e3163** recognizes exact integer equality/inequality followed by a conditional
as a loop-exit dispatch. It reuses the existing selected-arm use census, source
order, dominance and budget checks; observed inactive values still refuse.
An independent conditional witness with a literal close value now executes,
preserving pre-close Boolean payloads, natural exhaustion and DOM write order.
The original conditional source remains unchanged and refuses its larger
protected close helper; it has not been promoted.

Parallel **c30db6aa** removes the redundant Number-only guard for additive
invariant offsets. Existing bounded numeric conversion admits Boolean/null;
String addition still refuses because it concatenates. Reload/store checks,
actual-write replay and budgets remain. All 22 historical offset source bodies
and CHECKs are unchanged; eight controls were appended.

Focused host and arrays CTests and the exact offset-index lit case pass.
Five throw sources pass **80 native executions, 12 refusals and 24 Node/VM
observations**; three prior iterator sources pass **48 executions, 80 refusals
and 24 Node/VM observations**, with no nonexecuted admissions. Five historical
throw oracles agree on **20 observations per engine**. Full formatting passes
**1126 C++, 157 Python, 114 web files**. Seven final hashes match the devbox;
64 generated C++ files contain no Script/VM or nullable-scalar fallback.
All 233 historical iterator bodies and 86 metadata rows remain unchanged.
Full suites were skipped; no browser/shared implementation changed.

**Next native boundary:** original conditional `9c10be3d` now refuses
**DOM protected helper needs an independent inert-body proof**. Its close
method reads `hasAttribute('data-visited')` before `setAttribute`; preserve
suppression while proving both calls and their order. Mixed throw/return
`43ab2b64` still refuses **DOM helper completion observes an inactive value**.
Mutable Number snapshots, throwing/primitive close methods, multiple protected
regions, implicit cleanup, nested custom iterators, unguarded Bootstrap defaults,
the application driver and full native Bootstrap remain unfinished.

[Exact checks and next boundary](handoff/2026-09-23-compared-completion.md).

## Saved primitive iterator throws and subtraction offsets, 2026-09-23 UTC

Resumed clean **9729cf46** and unchanged `body-throw`, `f1b3f6b8`.
**87abc570** proves its exact nonreturning region and owning primitive payload,
then emits an ordinary C++ throw through the existing `CppThrowOp`. Only reachable
branches contribute frame/result joins. Primitive padding for unreachable yields
keeps the normal Boolean return typed; source effects and complete DOM reproof
remain. The payload/terminator verifier is unchanged. Original `body-throw` and
historical Boolean snapshot `92e23bbe` now execute unchanged, with Number,
Boolean and String coverage. Generated code uses public DOM/Core and ordinary
ownership, with no Script/VM or nullable-scalar fallback.

Parallel **430d9620** admits bounded primitive invariant subtraction offsets
through existing conversion, bounds, reload census and actual-write replay.
The historical reversed String-offset source now admits unchanged; all 34 old
source bodies survive, with twelve controls appended. Addition retains its
Number-only invariant rule because String addition concatenates.

Focused host and arrays CTests, both offset-index lit cases and the exception
printer lit case pass. Four throw sources pass **64 native executions, 12 refusals
and 16 Node/VM observations**; protected attributes pass **32 executions and
20 refusals**; three prior normal/return sources pass **48 executions, 80 refusals
and 24 Node/VM observations**. Five saved-throw oracles agree on **20 observations
per engine**. Full formatting passes **1126 C++, 157 Python, 114 web files**;
final scoped formatting passes. Seventeen final hashes match the devbox and
72 final generated C++ files contain no Script/VM protocol or nullable-scalar
fallback. All 233 historical iterator bodies and 86 metadata rows are preserved.
Full suites were skipped; no browser/shared implementation changed.

**Next native boundary:** retained conditional Boolean snapshot `9c10be3d`
refuses **DOM helper completion observes an inactive value** in both policies.
Prove its carried completion value without losing the saved pre-close payload.
Number snapshots still need mutable protected-close state proof; throwing or
primitive close methods still fail the fresh-record contract. Seven related
throw sources remain refused. Multiple protected regions, implicit cleanup,
nested custom iterators, unguarded Bootstrap defaults, the application driver
and full native Bootstrap remain unfinished.

[Exact checks and next boundary](handoff/2026-09-23-saved-primitive-throws.md).

## Typed protected attributes and primitive shift counts, 2026-09-23 UTC

Resumed clean **42c0502c** and unchanged `body-throw`, `f1b3f6b8`.
**5cd8002a** proves an exact zero-result protected `setAttribute` call:
one Element receiver, two Strings, a valid literal name checked by the public
DOM validator, and empty unobserved continuations. Complete DOM proof retains
method identity, source order and mutation checks. Lowering removes only the
proved unnecessary suppression wrapper and emits the ordinary DOM call under
its original guard. Invalid names, computed names and observed payloads refuse.

Parallel **dad6a885** admits bounded primitive invariant shift counts through
existing conversion, modulo-32 shift bounds, complete reload/store census and
actual-write replay. Two historical String-count sources now admit unchanged;
all 233 previous shift-source bodies are preserved.

Focused host and arrays CTests and both shift-index lit cases pass. New raw
attribute checks pass **32 native executions and 20 refusals**, including both
guard outcomes and both providers. Three existing source positives pass
**48 native executions, 82 refusals and 24 Node/VM observations**; another
**20 saved-throw/effect observations agree per engine**. No source admission was
left unexecuted. Full formatting passes **1126 C++, 157 Python, 114 web files**.
Eleven final hashes match the devbox; 40 generated C++ files contain no Script/VM
protocol. All 233 historical iterator bodies and 86 positive metadata rows remain
unchanged. Full suites were skipped; no browser implementation changed.

**Next native boundary:** original `f1b3f6b8` now passes typed suppressed-call
admission but both policies refuse **DOM entry does not admit nested control
flow or a source continuation**. Prove its exact nonreturning `scf.execute_region`
and saved primitive throw, reconcile branch/frame joins, and add proof-gated
admission and emission through the existing `CppThrowOp`. Keep its payload and
terminator verifier intact; retain complete prefix/global/reentry reproof.
No native execution of the throw source is claimed. Mutable exceptional state,
multiple protected regions, implicit cleanup, nested custom iterators, unguarded
Bootstrap defaults, the application driver and full native Bootstrap remain.

[Exact checks and next boundary](handoff/2026-09-23-protected-attribute-admission.md).

## Protected attribute expansion and primitive OR/XOR masks, 2026-09-23 UTC

Resumed clean **16d0dea5** and unchanged `body-throw`, `f1b3f6b8`.
**7cc06580** expands the exact captured attribute helper while keeping its
single DOM call inside the original suppression region. Literal/capture/method
preparation stays under the original guard; only the unused empty return object
is elided. Complete typed DOM reproof must justify the moved method lookup.
Suppression is not discharged, and no new native throw execution is claimed.

Parallel **bab3f790** admits bounded primitive invariant OR/XOR masks through
existing conversion, endpoint bounds, reload/store checks and actual-write
replay. Four historical raw String/Boolean constructions now admit unchanged.
All 179 previous source bodies survive. Historical Number expectation 134 was
corrected for earlier direct-gap refinement, with an exact Node witness.

Focused host and arrays CTests and the exact OR/XOR lit case pass. Three selected
native sources pass **48 executions, 82 refusals and 24 Node/VM observations**;
**20 saved-throw/effect observations agree per engine**. No admission was left
unexecuted. Full formatting passes **1126 C++, 157 Python, 114 web files**.
Six final code/test hashes match the devbox; 24 generated C++ files contain no
Script/VM protocol. All 233 historical iterator bodies and 86 positive metadata
rows are unchanged. Full suites were skipped; no browser implementation changed.

**Next native boundary:** original `f1b3f6b8` now passes helper expansion but
both policies refuse **DOM URI invocation requires complete unnested
continuations**. Add complete typed admission for the zero-result suppressed
attribute call, preserving receiver, arguments, source order and failure
behavior. Discharging suppression additionally requires the public DOM
attribute-name validator; String facts alone are insufficient. Typed saved
primitive throw emission and prefix/global/reentry proof remain. Mutable
exceptional state, multiple protected regions, implicit cleanup, nested custom
iterators, unguarded Bootstrap defaults, the application driver and full native
Bootstrap remain unfinished.

[Exact checks and next boundary](handoff/2026-09-23-protected-attribute.md).

## Protected callable ordering and primitive AND masks, 2026-09-23 UTC

Resumed clean **d1a4c47f** and unchanged `body-throw`, `f1b3f6b8`.
**8a6f33b5** proves immutable callable use inside an invocation's call body,
while excluding its continuations. Exact unused-result suppression can be
removed only after the existing independent inert-body proof succeeds. Calls,
coercions, getters, captures and source throws remain outside that proof;
refused expansion retains a valid invocation. Complete DOM admission is still
required, including for inert operations without a supported host contract.

Parallel **88bd5c80** admits bounded primitive invariant AND masks through the
existing conversion and bitwise bounds. Complete reload/store checks,
actual-write replay and budgets remain. The historical String mask and raw
String/Boolean constructions now admit unchanged. All 121 previous source
bodies survive; the older Number-mask expectation 87 was also corrected for
previously landed direct-gap refinement, with an exact source observation.

Focused host and arrays CTests and the bitand-index lit case pass. Three selected
native sources pass **48 executions, 82 refusals and 24 Node/VM observations**;
another **20 throw observations agree per engine**. No admission was left
unexecuted. Full formatting passes **1126 C++, 157 Python, 114 web files**, with
scoped checks after final test corrections. Eight final hashes match the devbox;
24 generated C++ files contain no Script/VM protocol. All 233 existing iterator
bodies and 86 positive metadata rows are unchanged. Full suites were skipped.

**Next native boundary:** original `f1b3f6b8` now passes protected callable ordering but both policies
refuse **DOM protected helper needs an independent inert-body proof**. Its
captured `anchor.setAttribute('data-closed', 'yes')` is effectful. Preserve
suppression while expanding it, or discharge it only after complete typed
receiver, argument, no-throw and prefix/global/reentry proof. DOM type facts
alone are insufficient: invalid attribute names throw. Saved primitive throw
admission/emission, mutable exceptional state, multiple protected regions,
implicit cleanup, nested custom iterators, unguarded Bootstrap defaults, the
application driver and full native Bootstrap remain unfinished. No native
execution of this throw source is claimed.

[Exact checks and next boundary](handoff/2026-09-23-protected-callable.md).

## Helper throw joins and primitive remainder divisors, 2026-09-22 UTC

Resumed clean **444fc1bd** and unchanged `body-throw`, `f1b3f6b8`.
**843f3a4f** proves the exact non-returning helper region: one saved throw,
no results or region arguments, and only a structural yield afterward. Only a
reachable sibling supplies the shadow-frame join. Nested throw-only branches,
local definitions and dominance remain checked; normal frame mismatches and
unproved effects still refuse. Helper expansion preserves the saved actual
payload. Invocation and typed C++ throw verifiers are unchanged.

Parallel **b1186fc5** admits bounded primitive remainder divisors through the
existing conversion and remainder proof. Nonzero bounds, signed endpoints,
complete reload/store checks, actual-write replay and budgets remain. Two
historical source expectations and String/Boolean raw expectations now admit
with their original constructions; all 68 previous source bodies survive.

Focused host and arrays CTests and the remainder-index lit case pass. Three
selected native sources pass **48 executions, 82 refusals and 24 Node/VM
observations**, with no nonexecuted admissions. Another **20 throw observations
agree per engine**. Full formatting passes **1126 C++, 157 Python, 114 web files**.
Seven final hashes match the devbox; 24 generated C++ files contain no Script/VM
protocol. All 233 existing iterator source bodies and 86 positive metadata rows
are unchanged. Full suites were skipped; no browser implementation changed.

**Next native boundary:** original `f1b3f6b8` now passes helper branch/frame
proof, but both policies refuse **DOM helper object has nonlocal or unordered
uses**. Prove the immutable callable holder's use inside the protected close,
then expand or retain its exact return-method call without breaking the
invocation's required call-plus-exit shape. Complete typed DOM effects, saved
primitive throw emission and prefix/global/reentry proof remain required.
Native execution of this throw source is not yet admitted. Mutable exceptional
state, multiple protected regions, implicit cleanup, nested custom iterators,
unguarded Bootstrap defaults, the application driver and full native Bootstrap
remain unfinished.

[Exact checks and next boundary](handoff/2026-09-22-iterator-helper-throw.md).

## Protected iterator completion and primitive divisors, 2026-09-22 UTC

Resumed clean **81999dbb** and unchanged `body-throw`, `f1b3f6b8`.
**a78c1b80** carries an exact suppressed close through custom-iterator
normalization. The done guard surrounds the invocation; its original return
method remains protected. The saved throw stays in its non-returning SCF region.
Completion proof replaces unreachable poison padding, retaining every
reachable source effect and requiring a real normal return. Mutable exceptional
state still refuses; invocation and typed C++ throw verifiers are unchanged.

Parallel **530a8e4b** admits bounded primitive division factors through the
existing conversion and exact-divisibility proof. Complete reload/store checks,
actual-write replay and budgets remain. The historical String-divisor source
and raw construction now admit unchanged; all 17 previous source bodies survive.

Focused host and arrays CTests and the quotient-index lit case pass. Three
selected native sources pass **48 executions, 82 refusals and 24 Node/VM
observations**, with no nonexecuted admissions. Another **20 throw observations
agree per engine**. Full formatting passes **1126 C++, 157 Python, 114 web files**.
Seven final hashes match the devbox; 24 generated C++ files have no Script/VM
protocol. All 233 existing iterator source bodies and 86 positive metadata rows
are unchanged. Full suites were skipped; no browser implementation changed.

**Next native boundary:** original `f1b3f6b8` now passes custom protocol and
completion normalization, but both policies refuse **DOM helper requires complete
structured branches**. `DOMSource::checkBody` must prove the exact non-returning
region and reconcile shadow-frame state with the reachable normal arm. Then
expand the protected return method without losing suppression and prove its
complete DOM effects, primitive throw payload and typed emission. Prefix/global/
reentry proof remains required. Native execution of this throw source is not yet
admitted. Mutable exceptional state, multiple protected regions, implicit cleanup,
nested custom iterators, unguarded Bootstrap defaults, the application driver and
full native Bootstrap remain unfinished.

[Exact checks and next boundary](handoff/2026-09-22-iterator-abrupt-protocol.md).

## Iterator terminal dispatch and primitive factors, 2026-09-22 UTC

Resumed clean **3a77bcdd** and unchanged `body-throw`, `f1b3f6b8`.
**fd97e48d** structures its mixed throw/return terminal dispatch after close
suppression. Existing SCF regions retain the original throw, saved payload,
protected close and normal-path effects. Only the real return supplies a result;
the throwing arm's required yield remains unreachable. No new IR operation or
runtime carrier was added, and typed C++ throw verification is unchanged.

Parallel **c16ee82d** admits invariant multiplication by bounded primitive
String, Boolean and null factors through the existing conversion. Endpoint
bounds, complete reload/store census, replay and budgets remain. Historical
String-factor source and raw constructions now admit unchanged; all 30 prior
escape source bodies and 229 prior native source bodies are preserved.

Focused host and arrays CTests and the scaled-index lit case pass. Three selected
native sources pass **48 executions, 82 refusals and 24 Node/VM observations**,
with zero nonexecuted admissions. Another **20 throw observations agree in Node
and VM**. Full formatting passes **1126 C++, 157 Python, 114 web files**. All eight
final hashes match the devbox; 24 generated C++ files have no Script/VM protocol.
Full suites were skipped; no browser implementation changed.

**Next native boundary:** `f1b3f6b8` now has one structured entry, but both
policies refuse **DOM custom iterator abrupt completion needs a handler proof**.
Prove the close invocation and exact throw-only region through custom iteration,
completion normalization and typed DOM admission. Discharge its unreachable
yield only from the non-returning throw proof, preserving the saved value and
suppression. Native execution of this source is not yet admitted. Multiple
protected regions, implicit cleanup, nested custom iterators, unguarded Bootstrap
defaults, the application driver and full native Bootstrap remain unfinished.

[Exact checks and next boundary](handoff/2026-09-22-iterator-terminal-dispatch.md).

## Iterator close regions and singleton products, 2026-09-22 UTC

Resumed clean **6d04e448** and unchanged `body-throw`, `f1b3f6b8`.
**f568bb86** represents its close-only suppression handler with the existing
zero-result invocation region. Both close outcomes resume the original saved
throw; exact predecessor, observer, intrinsic and budget checks remain.
Complete prefix/global/reentry and DOM reproof is still required. This is
structural progress; native abrupt completion is not yet admitted.

Parallel **9c727b92** proves bounded singleton products without scaling an
unvisited stride. Intermediate Number bounds, complete reload/store census,
actual-write replay and budgets remain. One unchanged historical CFG witness
now admits; all 25 historical escape source bodies and CHECKs remain intact.

Focused host and arrays CTests plus the scaled-index lit case pass. The initial
selected-source run completed **48 native executions, 66 refusals and 24 Node/VM
observations** before a newly added catch sample failed complete import. Its
source is retained as a boundary artifact; only its new fixture row was removed.
The final follow-up passes **10 refusals**, **12 throw Node/VM observations** and
an existing JSON/URI source's **four native executions and two observations**.
Full formatting passes **1126 C++, 157 Python, 114 web files**. All 12 final
hashes match the devbox; 28 generated C++ files have no Script/VM protocol.
Full suites were skipped; no browser implementation changed.

**Next native boundary:** `f1b3f6b8` now passes handler recognition but both
policies refuse **DOM custom iterator requires one complete entry**. Prove its
remaining mixed throw/return terminal dispatch and consume the close invocation
under the custom-protocol proof. Preserve the saved value, suppression and close
effects; do not invent a return or relax typed C++ throw verification. Multiple
protected regions, implicit cleanup, nested custom iterators, unguarded Bootstrap
defaults, the application driver and full native Bootstrap remain unfinished.

[Exact checks and next boundary](handoff/2026-09-22-iterator-close-regions.md).

## Explicit iterator throws and zero-factor indices, 2026-09-22 UTC

Resumed clean **45cb1fc1** and unchanged `body-throw` source `f1b3f6b8`.
**3d404e67** closes synchronous iterators on explicit throws and rethrows,
preserving the saved exception, inner-to-outer close order and catch/finally
scope. Separate suppression-handler landings keep the source importable.
The original source now agrees with Node: one close before throwing `1`,
and no close when already exhausted. Implicit body exceptions remain separate.

Parallel **3878ce1e** proves exact zero-factor own-index ranges using the
existing singleton lattice. Endpoint checks, complete reload/store census,
actual-write replay and budgets remain. Two historical CFG and one SCF
expectations now admit unchanged; all 18 prior source bodies remain intact.

Focused checks pass: `vm_control_flow`, `vm_async`, host and arrays CTests,
plus two selected scaled/composed-index lit cases. Three existing native
sources pass **48 executions, 58 refusals and 24 Node/VM observations**, with
zero nonexecuted admissions. All nine final code/test hashes match the devbox;
24 generated C++ files contain no Script/VM dependency names. Complete
formatting passes **1124 C++, 157 Python and 114 web files**. Full suites skipped.

**Next native boundary:** `f1b3f6b8` imports completely but both policies refuse
**DOM URI requires its fingerprinted initial provider binding**. DOM preparation
routes the cleanup handler through URI recovery. Prove the close-only suppression
handler and explicit primitive throw completion, using existing C++ throw emission;
do not add a URI intrinsic to bypass the proof. Implicit exception cleanup,
nested custom iterators, unguarded Bootstrap defaults, the application driver
and full native Bootstrap remain unfinished.

[Exact checks and next boundary](handoff/2026-09-22-iterator-throw-close.md).

## Mixed-input iterator selectors and signed bitwise gaps, 2026-09-22 UTC

Resumed clean **6bfe8e48** and exact saved mixed-input selector `987dfd90`.
**05a007b6** proves all original Style inputs and carries an ordinary borrowed
engine pointer beside the selected element through branch and loop edges.
Initialization, backedges, condition/result slots and selected return snapshots
retain their association. Each input validates before DOM writes; no runtime
value model or browser implementation is added. The saved source, before-close
selector and explicit prototype call execute under both providers and policies.

Parallel **f687c5d9** reuses bounded lattice refinement for XOR and sign-preserving
AND/OR across signed conversion boundaries. Complete reload/store checks,
actual-write replay and budgets remain. Four historical CFG and four SCF
expectations now admit with their original constructions and retained children.

Focused checks pass: host **1/1, 2.31 s total**, arrays **1/1, 2.19 s total**,
right-shift lit **1/1, 0.14 s**. Four selected sources pass **64 native executions,
318 refusals and 32 Node/VM observations**, with no nonexecuted admissions.
Complete formatting passes **1124 C++, 157 Python and 114 web files**.
All eleven final code/test hashes match the devbox; 32 generated C++ files
contain no Script/VM protocol. Full suites were skipped.

**Next native boundary:** original `body-throw`, `f1b3f6b8`, refuses under both
policies: **DOM custom iterator requires one complete entry**. Node closes once
before propagating the body exception; the VM currently omits that close.
Zero-trip exhaustion agrees. Repair the source compiler's body-throw cleanup,
then prove native abrupt completion without changing the source or weakening
its oracle. Broader finally cleanup, nested custom iterators, unguarded Bootstrap
defaults, the application driver and full native Bootstrap remain unfinished.

[Exact checks and next boundary](handoff/2026-09-22-iterator-mixed-style.md).

## Iterator Style associations and direct conversion gaps, 2026-09-22 UTC

Resumed clean **5e921715** and exact saved selector source `7ea457e6`.
**71a8654b** proves one original document/Style input through complete
branch and loop transport, including initialization, backedges, selector results,
indexed snapshots and document roots. Generated calls retain the selected element
and use that input's Style engine. Mixed roots, escaping handles, invalidation
and reentry still refuse. The original source, a read-before-close witness and
an explicit prototype call execute under both providers and policies.

Parallel **10a53c8b** enables existing bounded gap refinement for direct
integer conversions and fixes exact singleton division within refinement.
Endpoint integrality, complete reload/store checks, actual-write replay and
budgets remain. Historical sources and raw constructions are preserved.

Focused checks pass: host **1/1, 2.30 s total**, arrays **1/1, 2.17 s total**,
right-shift and quotient lit **2/2, 0.14 s**, existing query-all **1/1, 46.65 s**.
Selected custom sources pass **64 native executions, 374 refusals and 32 Node/VM
observations**, with no nonexecuted admissions. Complete formatting passes
**1124 C++, 157 Python and 114 web files**. Full suites were skipped;
no browser/runtime implementation changed.

**Next native boundary:** mixed-input selector `987dfd90` refuses under both
policies: **DOM selector requires one original Style association**. Eight Node/VM
observations agree. Supporting it requires carrying the selected element's Style
association with its control flow; raw input documents cannot share an engine
by assumption. Broader abrupt cleanup, nested custom iterators, unguarded
Bootstrap defaults, the application driver and full native Bootstrap remain.

[Exact checks and next boundary](handoff/2026-09-22-iterator-style-associations.md).

## Saved element payloads and direct remainder gaps, 2026-09-22 UTC

Resumed clean **4c132a5b** and unchanged saved source `2b611964`.
**995bdf39** carries homogeneous DOM elements through branches and loops using
existing `element_ref` owner/id pairs. Private inactive-slot provenance survives
custom normalization and helper expansion; only a dominating value from the
same branch slot can replace padding. Complete kind, observer and lifetime
checks remain. Return reads stay before close. The saved source, its two-read
snapshot, an identity witness and the original multiple-return source execute.
The latter also checks fallthrough without either return and zero-trip exhaustion.

Parallel **46d716a4** enables existing bounded refinement for direct remainder
gaps. Complete reload/store checks, actual-write replay and budgets remain.
Seven source witnesses preserve all 61 historical remainder bodies; one original
coprime-stride construction now admits unchanged. All 218 original native source
bodies and 79 existing positive metadata rows survive.

Focused validation: exact host **1/1, 2.30 s total**, exact arrays **1/1, 2.12 s
total**, remainder lit **1/1, 0.12 s**. Five selected sources complete **80 native
executions and 42 Node/VM observations** across two runs. The first finishes
64 executions, 130 refusal checks and two nonexecuted admissions before detecting
the now-admitted multiple-return source; its corrected follow-up passes
16 executions and 294 refusal checks. Full suites were skipped. Complete
formatting passes **1124 C++, 157 Python and 114 web files**. Fifteen final hashes
match the devbox; 40 generated C++ files contain no Script/VM protocol.
No browser implementation or runtime-oracle changes were made.

**Next native boundary:** source `7ea457e6`, derived from the unchanged saved
source by returning `node.matches('button')`, refuses under both policies:
**DOM selector on a carried element needs its Style association**. Prove and
transport the matching document/Style association through the same branch/loop
path before enabling selector calls. Eight Node/VM observations agree; this
source has no native execution claim. Broader completion shapes, abrupt cleanup,
nested custom iterators, unguarded Bootstrap defaults, the application driver
and full native Bootstrap remain unfinished.

[Exact checks and next boundary](handoff/2026-09-22-iterator-element-payloads.md).

## Selected iterator payloads and direct mask gaps, 2026-09-22 UTC

Resumed clean **ee141a82** and unchanged saved source `2b611964`.
**2618a169** proves that an inactive loop-exit payload has no observer in the
selected completion arm or outside its dispatch. Selected, nested, external and
after-region observers remain checked; reads and close stay in source order.
Two historical effectful constructions now admit unchanged. **e16b8352** adds
an explicit loop-local Number snapshot: **1 normally, 3 on stop, 0 when already
exhausted**, preserved across close. No runtime completion representation is added.

Parallel **bcf17b3a** enables existing bounded gap refinement for direct
non-affine AND/OR/XOR masks. Complete reload/store checks, actual-write replay
and budgets remain. Seven source witnesses retain all 134 historical bodies;
two historical unit-stride constructions now admit with exact Node-backed traces.
All 214 original native source bodies remain; the final source-only follow-up
also preserves all 217 sources already committed during this iteration.

Focused validation passes: exact host **1/1, 2.09 s total**, exact arrays
**1/1, 2.14 s total**, right-shift lit **1/1, 0.14 s**. The initial four-source run
and one-source follow-up total **80 native executions, 396 refusal checks, two
nonexecuted admissions and 40 Node/VM observations**. Complete formatting passes
**1124 C++, 157 Python and 114 web files**. Seven final hashes match the devbox;
40 generated C++ files contain no Script/VM protocol. Full suites were skipped;
no browser implementation changed.

**Next native boundary:** the original `body-return-branch-expression`,
`2b611964`, now passes inactive-slot proof but both policies refuse **DOM entry
branch cannot carry a borrowed or callable value**. Its payload is a saved DOM
element. Prove compatible inactive slots and the element's document/lifetime
through branch/loop transport, preserving the return read before close. Eight
Node/VM observations agree; no native execution of that source is claimed.
Further completion arms, abrupt cleanup, nested custom iterators, unguarded
Bootstrap defaults, the application driver and full native Bootstrap remain.

[Exact checks and next boundary](handoff/2026-09-22-iterator-selected-payloads.md).

## Effectful iterator exits and low-suffix mask gaps, 2026-09-22 UTC

Resumed clean **27abbc26** and saved conditional-return source `dc6fd4c5`.
**591db50c** keeps effectful two-arm completion dispatch after its loop,
carrying the proved exit choice as an ordinary Boolean. Return reads stay before
close, exhaustion reads stay after close, and live loop values retain their
observers. Unknown tags, observed selectors and inactive payloads still refuse.
The saved source and a close-read witness execute unchanged. Two historical
method-effect refusals now pass with their original constructions.

Parallel work identified the Part 25 gap landed as **2f8ad550**: low-suffix AND/OR
rounding can reuse existing bounded range refinement. Complete reload/store
checks, actual-write replay and budgets remain unchanged. Six source regressions
retain all 128 historical source bodies; all 212 historical native bodies and
75 metadata rows are preserved.

Focused validation passes: exact host **1/1, 2.14 s total**; four native sources
plus 136 refusal sources **64 executions, 368 refusals, two nonexecuted admissions
and 32 Node/VM observations**. Exact arrays pass **1/1, 2.04 s total** and the
right-shift lit case **1/1, 0.13 s**. Complete formatting passes **1124 C++, 157
Python and 114 web files**. All five final hashes match the devbox; 32 generated
C++ files contain no Script/VM protocol. Interrupted drafts and evidence were
preserved. No browser implementation changed; full suites were skipped.

**Next native boundary:** `body-return-branch-expression`, source `2b611964`,
saves a comma-expression return value in a loop exit slot that is inactive on
other exits. Both policies refuse **DOM helper completion observes an inactive
value**. Eight Node/VM observations agree: true on normal completion, false on
stop with one close, false when already exhausted without close. Prove the
selected saved payload without moving its read across close. More completion
arms, broader abrupt cleanup, nested custom iterators, unguarded Bootstrap
defaults, the application driver and full native Bootstrap remain unfinished.

[Exact checks and next boundary](handoff/2026-09-22-iterator-effectful-exits.md).

## Iterator return close and converted shift gaps, 2026-09-22 UTC

Resumed clean **9e7ba1f9** and the saved `body-return` source `9bd2d5ba`.
**380ee61d** fixes the source compiler's missing close on explicit synchronous
iterator returns, preserving return snapshots, nested close error order and
catch/finally scope. Node exposed 15 disagreements in 17 initial VM witnesses;
those and two further finally regressions now pass. General body-throw cleanup
remains separate. Claude was confirmed stopped before browser edits and landing.

**1d5b4e55** admits acyclic traversal with close calls in separate completion
arms after every traversal exit. The exact saved source and an ordered
return-before-close witness execute unchanged. Missing closes and reordered
protocols refuse; no VM protocol is emitted. Parallel **fdbae846** preserves
mixed-shift gap refinement through composed conversion jumps without changing
budgets, complete reload/store checks or actual-write replay.

Focused checks pass: exact host **1/1, 2.12 s total**; four native sources plus
136 refusal sources **64 executions, 368 refusals, two nonexecuted admissions
and 32 Node/VM observations**. Exact arrays **1/1, 2.06 s total** and right-shift
lit **1/1, 0.14 s** pass. Four selected browser CTests pass **4/4, 0.11 s total**.
Complete formatting passes **1124 C++, 157 Python and 114 web files**.
All 13 code/test hashes match the devbox; 32 generated C++ files have no Script
symbols. All 210 historical native and 120 shift source bodies remain unchanged.
Interrupted drafts and completed evidence were preserved. Full suites skipped.

**Next native boundary:** `body-return-branch`, source `dc6fd4c5`, has a
conditional return whose post-loop completion switch contains close effects.
Both native policies refuse **DOM helper completion selector is not an exact
constant**. Node and VM agree on four states: normal true/no close, stop
false/one close, already exhausted false/no close. Prove the effectful switch
without moving its saved return read across close. Nested custom opens, broader
abrupt close, unguarded Bootstrap defaults, the application driver and full native
Bootstrap remain unfinished; difficult shift subranges remain budget-limited.

[Exact checks and next boundary](handoff/2026-09-22-iterator-return-close.md).

## Formal return callees and partitioned shift gaps, 2026-09-22 UTC

Continued clean **2bfd3fbd** and saved formal-callee source `ee851de6`.
**a1d1c85f** proves return dependencies through formal and selected callees per
invocation, including loop backedges. An immutable graph survives capture erasure;
complete call, effect, recursion and observer checks remain. Saved source,
zero-trip and state/argument snapshot witnesses execute unchanged. No runtime
callable representation or increased proof budget is introduced.

Parallel **ab9d43b1** proves mixed-shift gaps by bisecting aligned induction
ranges through the existing Number transfer. Complete reload/store census and
actual-write replay retain unwritten children and saved snapshots. A 120-visit
budget regression passes. All 200 native and 113 shift historical sources remain
unchanged.

Focused checks pass: exact host **1/1, 2.12 s total**; four source programs plus
all 136 refusal sources **64 native executions, 368 refusals, two nonexecuted
admissions and 32 Node/VM observations**. Exact arrays pass **1/1, 2.05 s total**;
right-shift lit **1/1, 0.13 s**. Complete formatting passes **1124 C++, 157 Python
and 114 web files**. All seven final hashes match the devbox; 32 generated C++
files contain no Script/VM symbols. New raw fixture construction, arity and write
count mistakes were repaired; production and JS sources stayed unchanged.
Full suites were skipped. No browser/runtime-oracle changes; idle timer active/enabled.

**Next native boundary:** existing `body-return`, source `9bd2d5ba`, must close
the iterator on early return. Node returns false and writes `data-closed=yes`
when the body runs; exhausted iteration does not close. Both native policies
refuse **DOM iterator close must follow its complete traversal**. Prove abrupt
IteratorClose through source completion before admitting return/throw. Nested
custom opens, unguarded Bootstrap defaults, the driver and full native Bootstrap
remain unfinished; difficult shift subranges can still exhaust the shared budget.

[Exact checks and next boundary](handoff/2026-09-22-iterator-formal-callee-returns.md).

## Branch-dependent callable returns and composed shift gaps, 2026-09-22 UTC

Continued clean **4e25be51** and saved branch-dependent return source `191c2e50`.
**802c0d1a** records every formal and fixed-helper return dependency through
branches and loops, retaining complete invocation, effect, recursion and observer
proofs. Sharing the SSA clone mapping removes repeated region copies and saves
**29,003 proof steps** on the saved source without raising the budget. The saved
source, zero-trip and branch/argument/state snapshot witnesses execute unchanged.

Parallel **80bd870b** preserves exact mixed-shift gap refinement through remainder
and bit-mask enclosures. Complete reload/store checks and actual-write replay
retain untouched children and earlier snapshots. All 191 historical native and
105 right-shift source bodies remain unchanged.

Focused checks pass: exact host **1/1, 2.26 s total**; four source programs plus
all 129 refusal sources **64 native executions, 354 refusals, two nonexecuted
admissions and 32 Node/VM observations**. Exact arrays pass **1/1, 2.21 s total**;
right-shift lit **1/1, 0.14 s**. The complete formatter passes **1124 C++, 157
Python and 114 web files**. All seven final code/test hashes match the devbox;
32 generated C++ files contain no Script/VM symbols. Full suites and broad
matrices were skipped. No browser/runtime-oracle changes; the idle timer is
active/enabled. Interrupted agents resumed preserved drafts. A new raw fixture
spelling was corrected; production did not change after its gate failure.

**Next native boundary:** `entry-captured-sibling-returned-loop-branch-formal-callee`
uses `keep(writer, chooser) => chooser(writer)` on the existing loop backedge.
Node returns **2729 normally / 3603 on stop**, `data-closed=false`; both native
policies refuse an unproved arm. Prove return dependencies through formal callees
with complete invocation checks. Nested custom opens, abrupt close, unguarded
Bootstrap defaults, the application driver and full native Bootstrap remain
unfinished. Larger composed shift gaps remain proof-budget limited.

[Exact checks and next boundary](handoff/2026-09-22-iterator-branch-callable-returns.md).

## Nested callable returns and exact mixed shift gaps, 2026-09-22 UTC

Continued clean **94371e90** and the saved nested-return source `bd1a8aa5`.
**4eec52db** composes exact argument-return dependencies through known helpers.
The complete invocation, effect, recursion and observer proofs remain. Both
symbol scopes are now scanned once per helper family, removing repeated work
without raising the proof budget. The saved source, zero-trip and nested
state/argument snapshot witnesses execute unchanged.

Parallel **3ec40a4f** resolves mixed rounded-shift gaps by evaluating the existing
index proof at exact induction visits under the shared budget. Complete
reload/store checks and actual-write replay retain untouched children and saved
snapshots. All 183 historical native and 101 right-shift source bodies remain
unchanged; newly proved historical cases retain their original inputs.

Focused checks pass: exact host **1/1, 2.10 s total**; four source programs plus
all 123 refusal sources **64 native executions, 342 refusals, two nonexecuted
admissions and 32 Node/VM observations**. Exact arrays pass **1/1, 1.97 s total**;
right-shift lit **1/1, 0.13 s**. The complete formatter passes **1124 C++, 157
Python and 114 web files**. All seven final code/test hashes match the devbox;
32 generated C++ files contain no Script/VM symbols. Full suites and broad
matrices were skipped. No browser/runtime-oracle changes; the idle timer is
active/enabled. Interrupted agents resumed their preserved drafts.

**Next native boundary:** `entry-captured-sibling-returned-loop-forwarded-branch`
returns a callable selected by captured state inside `forward(writer)`. Node
returns **2729 normally / 3603 on stop**, `data-closed=false`; both native policies
refuse an unproved arm. Prove branch-dependent return summaries around the loop
without dropping effects, snapshots or observers. Nested custom opens, abrupt
close, unguarded Bootstrap defaults, the application driver and full native
Bootstrap remain unfinished. Larger mixed shift gaps remain proof-budget limited.

[Exact checks and next boundary](handoff/2026-09-22-iterator-nested-callable-returns.md).

## Callable loop return dependencies and unsigned residues, 2026-09-22 UTC

Continued clean **f99f5aea** and its exact saved `keep(selected)` backedge source.
**c2cb26fa** records explicit argument-return dependencies, then follows their
actual values through the existing loop proof. Complete invocation, effect,
observer and recursion checks still precede expansion. The saved source executes
unchanged; zero-trip and observable state/argument snapshot witnesses also pass.
Parallel **3e06bd9a** preserves unsigned right-shift residues across ToUint32
conversion jumps using the existing lattice and complete reload/store checks.
All 173 historical native and 93 right-shift source bodies remain unchanged.

Focused checks pass: exact host **1/1, 1.98 s total**; four source programs plus
all 118 refusals **64 native executions, 332 refusals, two nonexecuted admissions
and 32 Node/VM observations**. Exact arrays pass **1/1, 2.19 s total**;
right-shift lit **1/1, 0.14 s**. The complete formatter passes **1124 C++, 157
Python and 114 web files**. All seven final code/test hashes match the devbox;
32 generated C++ files contain no Script symbols. Full suites and broad matrices
were skipped. No browser/runtime-oracle changes; the idle timer is active/enabled.
Interrupted agents resumed their preserved drafts. New malformed raw controls
were repaired; a new snapshot witness was reduced within the existing budget
while retaining actual iterator execution. Production was unchanged after the
initial admission proof.

**Next native boundary:** `entry-captured-sibling-returned-loop-call-forwarded`
changes `keep(writer)` to return `forward(writer)`. Node returns **2729 normally /
3603 on stop**, `data-closed=false`; both native policies refuse an unproved arm.
Prove nested return dependencies around the loop while retaining all effects,
snapshots and observers. Nested custom opens, abrupt close, unguarded Bootstrap
defaults, the application driver and full native Bootstrap remain unfinished.
Part 25 retains mixed sparse-gap precision as separate work.

[Exact checks and next boundary](handoff/2026-09-22-iterator-callable-loop-returns.md).

## Callable loop transport and interrupted crash recovery, 2026-09-22 UTC

Resumed iteration 65's frozen replacement repair first: **b6f6f963** bounds
replacement expansion and accumulation using the existing string ceiling.
Parser safety **4dafb9c4** was already committed. Focused normal/sanitizer checks
pass; the replacement test262 crash now passes, while two parser conformance
failures remain ordinary failures without sanitizer crashes. Claude was confirmed
stopped before browser landing. See the crash-recovery handoff for exact evidence.

**e24bf692** proves callable identities through loop initializers, backedges,
condition arguments and results, retaining every incoming edge and observer.
The saved loop source executes unchanged; zero-trip and state/argument snapshot
witnesses also pass. **9a036e1a** preserves signed right-shift residues across
conversion jumps using the existing lattice and complete reload/store checks.
All 165 historical native and 84 right-shift source bodies remain unchanged.

Focused native checks pass: exact host **1/1, 1.65 s total**; four source programs
plus all 111 refusals **64 native executions, 318 refusals, two nonexecuted
admissions and 32 Node/VM observations**. Exact arrays pass **1/1, 1.93 s total**;
right-shift lit **1/1, 0.13 s**. The complete formatter passes **1124 C++, 157
Python and 114 web files**. All seven final code/test hashes match the devbox;
32 generated C++ files contain no Script symbols. Full suites and broad matrices
were skipped. The idle timer is active/enabled.

**Next native boundary:** `entry-captured-sibling-returned-loop-call-result`
feeds `keep(selected)` back into the loop. Node returns **2729 normally / 3603
on stop**, `data-closed=false`; both native policies refuse an unproved arm.
Prove callable return dependencies around the loop without losing effects,
snapshots or complete observers. Nested custom opens, abrupt close, unguarded
Bootstrap defaults, the application driver and full native Bootstrap remain
unfinished. Part 25 still conservatively encloses unsigned conversion jumps.

[Exact native checks and next boundary](handoff/2026-09-22-iterator-callable-loops.md); [crash recovery](handoff/2026-09-22-crash-recovery.md).

## Test failure repairs, 2026-09-22 UTC

The user redirected this iteration to test failures. **8207bc70** and **25e68e9d**
clear all 16 formatter diagnostics in the three native files and `ctdrive.cpp`.
**6aec6d46** fixes an ASan-proven use-after-free during Set builtin installation:
adding `keys` could invalidate the property pointer reused for `Symbol.iterator`.
Both aliases now use a copied function value; an identity assertion accompanies
the fix. A parallel audit found no matching defect in sibling builtin aliases.
Claude was confirmed stopped through Linux and Windows process checks before
browser edits and landing. No JS behavior or native admission changed.

The final focused `aot_gc` and `keyed_collections` checks pass **2/2 normally,
0.03 s total**, and **2/2 with ASan/UBSan, 0.16 s total**. Both sanitizer tests
reproduced the invalid read before the fix. Earlier, normal `aot_gc` and
`ctcompile_host_contract` passed **2/2, 1.51 s total**; `ctjs-opt` and `ctdrive`
also built. The complete formatter passes **1124 C++, 157 Python and 114 web
files**, superseding the historical outstanding-format reports below. All six
code/test hashes match the devbox; its idle timer is active/enabled. Full CTest,
compiler lit, WPT/test262 and corpus/matrix runs were skipped.

Exact commands, logs and hashes are in
`../../../test-results/2026-09-22-validation-cleanup/README.md`.
The next native boundary remains
`entry-captured-sibling-returned-callable-loop-join` below. Full native Bootstrap
and the application driver remain unfinished.

## Callable branch joins and wrapped complements, 2026-09-22 UTC

Continued clean **efbab738** and the saved branch-selected writer source.
**4ad7e94c** proves every callable identity through complete `if` arms and
expands selected invocations into ordinary branches. Selection, argument snapshots,
current captured state and ordinary results retain their source order. Follow-up
**8d6cb131** encodes the private discriminator as ordinary integer Numbers.
No callable object, lookup table or browser/runtime-oracle change is introduced.

Parallel **78040bbc** reuses wrapped-shift residue bounds for larger complements
across signed conversion jumps. Complete reload/store checks, actual-write replay,
unwritten children and prior snapshots remain. Eight new source witnesses retain
all 32 historical complement bodies.

Focused checks pass: final exact host **1/1, 1.50 s total**; the initial seven-source
subset and all 106 refusal programs **112 native executions, 380 refusals, two
nonexecuted admissions and 56 Node/VM observations**. After integer-tag encoding,
the affected three-source subset passes **48 executions, 284 refusals, two
nonexecuted admissions and 24 Node/VM observations**. Exact arrays pass **1/1,
1.93 s total**; bitnot/left-shift lit **2/2, 0.13 s**. All seven final code/test
hashes match the devbox. Required formatting retains **16 existing diagnostics
in four untouched files**; changed scopes pass. Full suites and the complete
custom case were skipped. The idle timer is active/enabled.

**Next native boundary:** `entry-captured-sibling-returned-callable-loop-join`
carries its selected writer through a bounded two-trip loop. Node returns
**2729 normally / 3603 on stop**, with `data-closed=false`; both native policies
refuse an unproved branch arm. Prove loop-carried callable identities without
losing selection time, effects, snapshots or complete observers. Nested custom
opens, abrupt close, unguarded Bootstrap defaults and the application driver
remain unfinished. Full Bootstrap is not admitted.

[Exact checks and next boundary](handoff/2026-09-22-iterator-callable-branches.md).

## Callable targets per invocation and wrapped left shifts, 2026-09-22 UTC

Continued clean **f6496ace** and the saved differing-target source.
**cd6d9ab7** proves callable identities separately at each helper invocation,
including forwarded arguments and single root returns. Direct-target and complete
observer checks precede expansion; scalar snapshots, current shared state and
ordinary results survive. The saved source executes unchanged. No browser or
runtime-oracle semantics changed.

Parallel **9f01bba4** encloses larger left shifts across conversion/output wraps
with a proved residue and stride. Exact two-point and affine paths remain;
actual-write replay retains unwritten children and earlier snapshots. Thirteen
new source functions extend all 62 historical left-shift bodies unchanged.

Focused checks pass: exact host **1/1, 1.46 s total**; seven custom sources plus
all 100 refusal programs **112 native executions, 368 refusals, two nonexecuted
admissions and 56 Node/VM observations**; exact arrays **1/1, 1.98 s total**;
left/right-shift lit **2/2, 0.13 s**. Two earlier host failures came from unsupported
operations in the new distinct-writer fixture; doubled-step addition preserves
its purpose and passes. Production was unchanged. All seven final code/test hashes
match the devbox. Required formatting retains **16 existing diagnostics in four
untouched files**; changed scopes pass. Full suites, the complete custom case and
unchanged nested/dataset cases were skipped. The idle timer is active/enabled.

**Next native boundary:** `entry-captured-sibling-returned-callable-branch-join`
selects a writer according to captured state before returning it. Node returns
**2729 normally / 3603 on stop**, with `data-closed=false`; both native policies
refuse an unproved callable. Carry exact identities through branch/loop joins
while retaining effects, snapshots and complete observers. Nested custom opens,
abrupt close, unguarded Bootstrap defaults and the application driver remain
unfinished. Full Bootstrap is not admitted.

[Exact checks and next boundary](handoff/2026-09-22-iterator-callable-targets.md).

## Returned iterator callables and conversion-jump shifts, 2026-09-22 UTC

Continued clean **5631b2d7** and its saved returned-callable source.
**d3767892** propagates a confined callable identity through a helper's single
root return and checks every result observer and invocation. Producers expand
before returned-callable consumers in source order, preserving scalar argument
snapshots, current shared state and ordinary results. The exact saved source
executes unchanged. No browser or runtime-oracle semantics changed.

Parallel **48838685** encloses larger signed/unsigned right shifts across
conversion jumps in the existing dense interval. Exact two-point results remain;
replay records actual writes, retaining unwritten children and earlier snapshots.
Twelve new source witnesses extend all 72 historical right-shift bodies.

Focused checks pass: exact host **1/1, 1.57 s total**; seven custom sources plus
all 94 refusal programs **112 native executions, 356 refusals, two nonexecuted
admissions and 56 Node/VM observations**; exact arrays **1/1, 1.93 s total**;
left/right-shift lit **2/2, 0.13 s**. Initial host failures exposed a missing
pre-mutation refusal for undefined returned callees and variable dependency-scan
cost; both were fixed without changing the tests. All seven final code/test hashes
match the devbox. Required formatting retains **16 existing diagnostics in four
untouched files**; changed scopes pass. Full suites, the complete custom case and
unchanged nested/dataset cases were skipped. The idle timer is active/enabled.

**Next native boundary:** `entry-captured-sibling-returned-callable-different-targets`
passes two distinct known writers through one returning helper. Node returns
**2729 normally / 3603 on stop**, with `data-closed=false`; both native policies
refuse the single-target requirement. Prove identities per invocation while
retaining every caller/observer, scalar snapshot and state update. Callable
branch/loop joins, nested custom opens, abrupt close, unguarded Bootstrap defaults
and the application driver remain unfinished. Full Bootstrap is not admitted.

[Exact checks and next boundary](handoff/2026-09-22-iterator-callable-returns.md).

## Callable iterator arguments and rounded shift gaps, 2026-09-22 UTC

Continued clean **6e2082e9** and the exact callable-argument boundary recorded
in the previous handoff. **a9fe119c** proves one confined callable identity per
helper parameter across all callers and forwarding edges. The existing inliner
preserves each evaluated argument, current shared state and ordinary result.
The saved source executes unchanged; new witnesses cover interleaved scalar
arguments and shared forwarding. Native output retains ordinary scalar ownership
and public browser calls. No browser or runtime-oracle semantics changed.

**ad228463** preserves right-shift gaps when every rounded increment is equal,
using the existing interval, stride and conversion-band proof. Mixed increments
remain dense; complete reload/store and work-budget checks remain. Six new source
witnesses extend all 66 historical right-shift function bodies.

Focused checks pass: exact host **1/1, 1.35 s total**; seven custom sources plus
all 88 refusal programs **112 native executions, 344 refusals, two nonexecuted
admissions and 56 Node/VM observations**; exact arrays **1/1, 1.84 s total**;
left-shift lit passed, corrected right-shift lit **1/1, 0.12 s**. Two new negative
escape sources initially did not terminate; their fixtures were corrected without
changing production or raw tests. All seven final code/test hashes match the
devbox. Required formatting retains **16 existing diagnostics in four untouched
files**; changed scopes pass. Full suites, the complete custom case and unchanged
nested/dataset cases were skipped. The devbox idle timer is active/enabled.

**Next native boundary:** `entry-captured-sibling-returned-callable-writer`
returns `writer` from `identity(writer)` and then invokes it. Node returns
**2724 normally / 3598 on stop**, with `data-closed=false`; both native policies
refuse the unproved returned identity. Preserve callable return provenance and
complete caller/observer checks before expansion. Differing targets at one
parameter, nested custom opens, abrupt close, unguarded Bootstrap defaults and
the application driver remain unfinished. Full Bootstrap is not admitted.

[Exact checks and next boundary](handoff/2026-09-22-iterator-callable-arguments.md).

## Sibling iterator call trees and two-value shifts, 2026-09-22 UTC

Continued clean **6ebf1280** and its saved nested sibling-helper boundary.
**d4e18328** proves confined helper call trees through single-initialized local
callable cells. Complete capture, call, symbol and cycle checks precede expansion.
The existing inliner and scalar rewrite preserve argument snapshots, current
shared state and ordinary results through nested calls, branches, loops and close.
The original source executes unchanged; new witnesses cover a shared callee and
an inner argument that changes the first argument's captured cell. No browser or
runtime-oracle semantics changed.

Parallel **9d5506d4** reuses exact endpoint bounds for shifts with at most two
possible inputs, including conversion jumps, left-shift wraps and uneven right
shifts. Larger ranges retain the existing conservative proof. Twenty-two source
witnesses extend all 106 previous bodies; complete store/reload census and budgets
remain.

Focused checks pass: exact host **1/1, 1.31 s total**; seven custom sources plus
all 82 refusal programs **112 native executions, 332 refusals, two nonexecuted
admissions and 56 Node/VM observations**; exact arrays **1/1, 1.81 s total**;
left/right-shift lit **2/2, 0.13 s**. All eight final code/test hashes match
local/devbox files. Required formatting retains **16 pre-existing diagnostics in
four untouched files**; changed scopes pass. Full suites, the complete custom
case and unchanged nested/dataset cases were skipped. The devbox idle timer is
active/enabled.

**Next native boundary:** `entry-captured-sibling-callable-argument-writer`
passes `advance` to `relay(writer)`, which calls its argument. Node returns
**2724 normally / 3598 on stop**, with `data-closed=false`; both native policies
refuse the unproved helper identity. Carry a confined callable argument through
the complete call census while preserving state and value snapshots. Nested
custom opens, abrupt close, unguarded Bootstrap defaults and the application
driver remain unfinished. Full Bootstrap is not admitted.

[Exact checks and next boundary](handoff/2026-09-22-iterator-sibling-calls.md).

## Sibling iterator helper breaks and two-value complements, 2026-09-22 UTC

Continued clean **e26b8342** and its exact finite sibling-helper break boundary.
**5b6c909a** reuses the existing completion proof before sibling scalar-body
validation. Helper breaks preserve explicit argument snapshots, ordered shared
state and ordinary results through the existing inliner and scalar rewrite.
The saved finite source executes unchanged. The original zero-state source also
admits but cannot progress at runtime; it remains byte-identical in a separate
compile-only check. No browser or runtime-oracle semantics changed.

Parallel **11c87d97** reuses exact two-value endpoint bounds for unary `~`
across signed-conversion jumps. Larger ranges retain the existing same-band
requirement, complete store/reload census and budget. Twelve source witnesses
extend all twenty previous bitnot function bodies.

Focused checks pass: exact host **1/1, 1.41 s total**; seven custom sources plus
all 76 refusal programs **112 native executions, 320 refusals, two nonexecuted
admissions and 56 Node/VM observations**; exact arrays **1/1, 1.83 s total**;
bitnot/AND/OR-XOR lit **3/3, 0.19 s**. All seven final code/test hashes match
local/devbox files. A new raw poison control was corrected to poison a live
loop input; production stayed unchanged. Required formatting retains **16
pre-existing diagnostics in four untouched files**; changed scopes pass.
Full suites, the complete custom case and unchanged nested/dataset cases were
skipped. The devbox idle timer is active/enabled.

**Next native boundary:** `entry-captured-sibling-nested-call-writer` moves an
ordered state update into another helper called by the first. Node returns
**2724 normally / 3598 on stop**, with `data-closed=false`; both native policies
refuse the unsupported sibling call. Prove that confined call tree while
retaining current state, argument snapshots and ordinary results. Nested custom
opens, abrupt close, unguarded Bootstrap defaults and the application driver
remain unfinished. Full Bootstrap is not admitted.

[Exact checks and next boundary](handoff/2026-09-22-iterator-sibling-breaks.md).

## Iterator argument snapshots and sparse bitwise bounds, 2026-09-22 UTC

Continued clean **9ad10593** and its exact
`entry-captured-sibling-argument-writer` boundary. **8b5a9ebc** binds each
confined sibling helper's explicit arguments before its captured state cells,
reusing the existing inliner and scalar rewrite. Exact arity, source evaluation
order, snapshots, ordinary results and ordered writes survive branches, loops
and close. Complete typed DOM reproof remains required. The saved source executes
unchanged; new sources check a later argument writing the first argument's cell
and argument-driven control flow. All 105 earlier complete sources remain.

Parallel **2b3f6d24** evaluates exact AND/OR/XOR endpoint images when the
existing interval and stride contain at most two possible values. It retains
sparse output gaps across signed-conversion jumps without a new range domain.
Thirty-one new source witnesses extend all 269 earlier bodies. Thirteen older
raw refusals and four source claims now admit with unchanged source bodies and
checked owner/read expectations. No browser or runtime-oracle semantics changed.

Focused checks pass: exact host **1/1, 1.20 s total**; seven custom sources plus
all 74 refusal programs **112 native executions, 316 refusals and 56 Node/VM
observations**; exact arrays **1/1, 1.84 s total**; AND/OR-XOR lit **2/2, 0.15 s**.
All eight final code/test hashes match local/devbox files. Required formatting
retains **16 pre-existing diagnostics in four untouched files**; changed scopes
pass. Full suites, the complete custom case and unchanged nested/dataset cases
were skipped. The devbox idle timer is active/enabled.

**Next native boundary:** sibling helper loop-break completion. Both the original
`entry-captured-sibling-loop-break-writer` and the separately labeled
`entry-captured-sibling-loop-break-finite-writer` refuse the scalar-leaf proof
under both policies. The original zero-state source cannot progress after its
break; preserve it as a refusal. Use the finite variant for execution work:
Node returns **1258 normally / 1651 when stopping**, with `data-closed=false`
on stop. Compose the existing completion proof before sibling leaf validation.
Nested custom opens, abrupt close, unguarded Bootstrap defaults and the application
driver remain unfinished. Full Bootstrap is not admitted.

[Exact checks and next boundary](handoff/2026-09-22-iterator-arguments.md).

## Iterator branch joins and fixed-bit rounding, 2026-09-22 UTC

Continued clean **6f7ee572** and its saved
`entry-captured-sibling-preloop-branch-writer` boundary. **63b36a15** joins
fully defined JavaScript branch results and scalar state before one common
continuation. Completion tags, switches and inactive values retain the existing
bounded path proof. The exact saved source now executes unchanged; additional
sources check branch-dependent results and early helper returns. Post-close
observations execute once after the selected state joins. No runtime storage or
browser semantics changed.

Parallel **45658831** recognizes monotone AND/OR rounding through fixed input
bits, reusing exact endpoints, signed-conversion guards and the existing lattice.
Eighteen new source witnesses extend all 251 previous bodies; complete
reload/store census and budget checks remain.

Focused checks pass: exact host **1/1, 1.15 s total**; seven custom sources
**112 native executions, 304 refusals and 56 Node/VM observations**, including
all 68 refusal programs; nested iteration lit **1/1, 132.99 s**, with **48 native
executions, two previous-source checks and 94 refusals**; exact arrays **1/1,
1.83 s total**; AND/OR-XOR lit **2/2, 0.14 s**. All eight final code/test hashes
match local files and the devbox. Required formatting retains **16 pre-existing
diagnostics in four untouched files**; changed scopes pass. Full suites and the
complete custom case were skipped. The devbox idle timer is active/enabled.

**Next native boundary:** the unchanged
`entry-captured-sibling-argument-writer` passes `closed` to its sibling helper
and observes `emitted + amount` before ordered writes. Both policies refuse
`DOM iterator sibling helper requires an exact local leaf`. Prove explicit
arguments at each call while retaining that argument snapshot, ordinary return
and current state. Sibling method breaks, nested custom opens, abrupt close,
unguarded Bootstrap defaults and the application driver also remain open.
Full Bootstrap is not admitted.

[Exact checks and next boundary](handoff/2026-09-22-iterator-branch-joins.md).

## Sibling iterator writers and low-bit rounding, 2026-09-22 UTC

Continued clean **8c04990c** from the recorded
`entry-captured-sibling-writer` boundary. **8848217e** admits confined sibling
helpers that update iterator state. Complete family, capture, call and symbol
checks precede inlining. The existing entry rewrite carries ordered writes and
the ordinary return value through scalar state; repeated calls see the latest
values, including iterator close. No new runtime storage or browser semantics.

The exact saved writer source executes unchanged. Two new sources check ordered
two-cell writes and loop-local branches; all 92 prior source bodies remain.
Parallel **ec206acd** tightens same-band AND/OR index bounds when a mask clears
or sets a contiguous low suffix, preserving the proved output period. Twenty-six
new source witnesses extend the unchanged 225 earlier bodies.

Focused validation passes: exact host **1/1, 1.25 s total**; eight relevant custom
sources **128 native executions, 328 refusals and 64 Node/VM observations**;
exact arrays **1/1, 1.93 s total**; AND/OR-XOR lit **2/2, 0.16 s**. The source
subset includes all 68 refusal programs. All eight final code/test hashes match.
The complete custom case and broad suites were not run. Required formatting
retains **16 pre-existing diagnostics in four untouched files**; changed scopes
pass. The devbox idle timer is active/enabled.

**Next native boundary:** `entry-captured-sibling-preloop-branch-writer` preserves
the complete new helper whose conditional follows its local loop. Completion
normalization duplicates the later custom iterator under that pre-traversal
branch; both policies refuse `DOM custom iterator next call is ambiguous after
completion`. Join that helper's ordinary result and state before the shared
continuation, retaining every observation. Argument-taking helpers, sibling
method breaks, nested custom opens, abrupt close, unguarded Bootstrap defaults
and the application driver remain open. Full Bootstrap is not admitted.

[Exact checks and next boundary](handoff/2026-09-22-iterator-sibling-writers.md).

## Sibling iterator state readers and combined mask periods, 2026-09-22 UTC

Continued clean **fca25a05** from the committed
`entry-captured-sibling-reader` boundary. **c18dd782** admits unique local
scalar reader closures over iterator Number cells. The complete family, call
sites, initialization and symbolic uses are proved before expansion. Each
invocation passes its current state to the existing helper inliner, preserving
before/body/after reads and latest close state without closure or cell storage.
Ordinary calls and exact direct targets are supported; unused lexical receivers
retain their source identity during proof. Mutation and escaping readers refuse.

Two original complete sources now execute unchanged. A two-cell reader source
checks repeated calls and ordered state updates; four raw twins cover ordinary
and direct calls with normal and zero-body traversal. All 87 earlier source
bodies remain. Parallel **76b6f909** composes fixed input and AND/OR mask
bits to tighten the existing output stride. Twenty-six source witnesses extend
the unchanged 199 earlier bodies. No browser/oracle semantics changed.

Focused validation covers **496 native executions** across two runs. The main
custom lit completed 480 executions, then stopped on the old
`extra-captured-closure` refusal after **749.19 s**. That now-supported source
was promoted unchanged; its focused follow-up passes **16 native executions,
146 refusals and 8 Node/VM observations**. The main run had already completed
240 Node/VM observations. The complete custom case was not replayed after this
test correction. Exact host **1/1, 1.02 s total**, exact arrays **1/1, 1.78 s total**
and AND/OR-XOR lit **2/2, 0.14 s** pass. Final source hashes match their checks.

The first combined build caught string-valued test expectations where existing
rows require literal pointers; corrected tests retain their sources and ownership
checks. Required formatting retains **16 pre-existing diagnostics in four
untouched files**; changed scopes pass. Full suites, unchanged nested/dataset lit,
broad replays, Windows and additional sanitizers were skipped. The devbox idle
timer is active/enabled.

**Next native boundary:** `entry-captured-sibling-writer` retains the reader
source but changes the helper to `emitted += closed; return emitted`. Both
optimization policies refuse the read-only scalar leaf requirement. The next
proof must return the helper's updated shared state as well as its ordinary
result. Nested custom opens, abrupt close, literal range-for printing, unguarded
Bootstrap defaults and the application driver remain open. Full Bootstrap is
not admitted.

[Exact checks and next boundary](handoff/2026-09-22-iterator-sibling-readers.md).

## Enclosing iterator capture state and XOR complements, 2026-09-22 UTC

Continued clean **e28b03b8** from the recorded
`loop-break-external-captured-read` boundary. **35f59014** carries the
iterator's captured Number cells through enclosing-entry reads and writes,
including before, inside and after iteration. The existing scalar tuple and
structured control-flow rewrite preserve shared identity and source order.
Close joins retain `return` updates on break and final `next` state on exhaustion.
The existing MLIR verifier and dominance analysis check entry shape and
initialization before rewriting. Other capturing closures remain refused.

Six original complete-source refusals now execute unchanged. A new two-cell
source checks entry loops, conditional body updates, close-hook state and ordered
post-loop observations. All 82 earlier source bodies remain. Generated C++ uses
typed scalar state and public DOM calls without cell storage or Script.
Parallel escape **c2254fc8** preserves full strides when XOR flips every proved
varying input bit within one signed conversion band. Fifteen source witnesses
extend the unchanged 108 OR/XOR sources. No browser/oracle semantics changed.

Focused checks pass: exact host **1/1, 0.96 s total**; custom iteration lit
**1/1, 745.61 s**, with **448 native executions, 790 refusals and
224 Node/VM observations**; exact arrays **1/1, 1.68 s total**; AND and OR/XOR
lit **2/2, 0.14 s**. All seven final code/test hashes match their gates.
The initial raw assertion expected arithmetic outside the close branch;
inspection confirmed existing completion normalization moves it into each arm.
The new combined source also required dominance across imported switch regions.
Required formatting retains **16 pre-existing diagnostics in four untouched
files**; changed files pass scoped checks. Full suites, unchanged nested/dataset
lit, broad replays, Windows and sanitizers were skipped. The devbox idle timer is
active/enabled.

**Next native boundary:** committed `entry-captured-sibling-reader` preserves
the new entry-state source but reads `emitted` through a separate local closure
after iteration. Both optimization policies refuse with
`DOM iterator capture cell has an external reader or writer`. The next proof
must preserve that closure's observation of the latest cell. Nested custom opens,
body abrupt-close behavior, literal range-for printing, unguarded Bootstrap
defaults and the application driver remain open. Full Bootstrap is not admitted.

[Exact checks and next boundary](handoff/2026-09-22-iterator-entry-state.md).

## Iterator method breaks and fixed-bit mask strides, 2026-09-22 UTC

Continued **ccbdf5d6** and resumed iteration 52's interrupted edits.
**dba0df1c** normalizes method-local `break` completion before iterator
state analysis, using the existing complete continuation proof. Pure exit
selections may have no results. Existing tuple cleanup removes unused method
completion tags while preserving ordered effects, live counters and latest
captured/receiver state. Method identities are cached before entry replacement;
a method aliasing the entry is refused before normalization.

The original `loop-captured-break` source executes unchanged. Paired two-state
sources check before-break updates, skipped suffixes, exhaustion, close-hook
state and invocation reset. Generated output uses typed scalars and public DOM
calls without boxed state or Script. Parallel escape **4e3acdc9** preserves full
strides when AND/OR/XOR masks change only proved fixed input bits within one
signed conversion band. All 163 previous escape sources are unchanged; 21 were
added. No browser/oracle semantics changed.

Focused validation: exact arrays **1/1, 1.85 s total**; AND and OR/XOR lit
**2/2, 0.15 s**; custom iteration lit **1/1, 528.43 s**, with
**336 native executions, 626 refusals and 168 Node/VM observations**. After the
final entry-alias guard, exact host **1/1, 0.88 s total** and the three
new source cases pass with **48 native executions, 84 refusals and 24 Node/VM
observations**. The full custom case used the pre-guard implementation; the final
subset and host test use the final nine code/test hashes. Required formatting
retains **16 pre-existing diagnostics in four untouched files**; changed files
pass scoped checks. Full suites, unchanged nested/dataset lit, broad replays,
Windows and sanitizers were skipped. The devbox idle timer is active/enabled.

**Next native boundary:** committed `loop-break-external-captured-read` retains
an observation of the captured cell after iteration (`return count + emitted`).
Both optimization policies refuse with
`DOM iterator capture cell has an external reader or writer`. Scalar state must
remain observable outside the confined iterator before this source can pass.
Nested custom opens, body abrupt-close behavior, literal range-for printing,
unguarded Bootstrap defaults and the application driver remain open.
Full Bootstrap is not admitted.

[Exact checks and next boundary](handoff/2026-09-22-iterator-method-break.md).

## Iterator method loops and fixed-bit AND bounds, 2026-09-22 UTC

Continued clean **25a6edff** from its recorded `loop-captured-store` boundary.
**cf0e4580** carries confined captured/receiver Number state through method
`while` loops using the existing conditional scalar transport. Complete loop
preflight precedes rewriting; initial values, both regions, condition/yield edges
and exits retain ordered state and original result positions. The final condition
provides exit state even with zero body trips. Close-hook loops use the same proof.

The original loop source executes unchanged. Paired two-state sources and raw
controls cover ordinary local counters, nested branches, ordered effects, latest
close values and invocation reset. Generated C++ uses typed scalars and public
DOM calls without boxed state or Script. Parallel escape **06814db5** preserves
proved fixed upper input bits in low-bit AND bounds, retaining the previous safe
fallback across sign/conversion boundaries. Twelve AND witnesses extend the
unchanged original fifty-five. No browser/oracle semantics changed.

Focused checks pass: exact host **1/1, 0.84 s total**; custom iteration lit
**1/1, 488.40 s**, with **288 native executions, 544 refusals and 144 Node/VM
observations**; exact arrays **1/1, 1.68 s total**; AND and OR/XOR lit **2/2,
0.13 s**. All seven final code/test hashes match their gates; no fixes were needed
after the initial build. Required formatting retains **16 pre-existing diagnostics
in four untouched files**; changed files pass scoped checks. Full suites,
unchanged nested/dataset lit, broad replays, Windows and sanitizers were skipped.
The devbox idle timer is active/enabled.

**Next native boundary:** committed `loop-captured-break` remains refused under
both optimization policies. Its method-local break dispatch needs completion
normalization/proof before scalar state transport; captured diagnostics say
`DOM helper requires complete structured branches`. External cell observations,
nested custom opens, body abrupt-close behavior, literal range-for printing,
unguarded Bootstrap defaults and the application driver remain open.
Full Bootstrap is not admitted.

[Exact checks and next boundary](handoff/2026-09-22-iterator-loop-state.md).

## Conditional iterator state and XOR complements, 2026-09-22 UTC

Continued clean **f1eb9201** from the recorded `conditional-captured-store`
boundary. **a4c2d5d2** admits conditional updates to confined iterator Number
cells and receiver fields. Each `if` arm retains its original results and effects,
then yields its current scalar state. Unwritten arms preserve incoming values;
nested joins and close-hook updates retain source order. Initialization, capture
identity, confinement, type, lifetime and budget proofs remain required.

Both original conditional sources execute unchanged. Two-state source and raw
controls check nested branches, sequential reads, exhaustion and latest close
values. Generated C++ uses typed scalars and public DOM calls without boxed
iterator state or Script. Parallel escape work **5805be25** preserves odd strides
through all-one XOR complements within one signed conversion band, reversing
endpoints through the existing bitwise evaluator. Original 84 source witnesses
remain unchanged; twelve were added. No browser/oracle semantics changed.

Focused checks pass: exact host **1/1, 0.89 s total**; custom iteration lit
**1/1, 404.35 s**, completing **240 native executions, 460 refusals and 120
Node/VM observations**; exact arrays **1/1, 1.64 s total**; OR/XOR and AND lit
**2/2, 0.12 s**. All seven final code/test hashes match their gates. The initial
arrays run found one malformed new near-mask fixture; an exact `-2` literal
fixed its definition order without changing production. Required formatting
retains **16 pre-existing diagnostics in four untouched files**; changed files
pass scoped checks. Full suites, unchanged nested/dataset lit, broad replays,
Windows and sanitizers were skipped. The devbox idle timer is active/enabled.

**Next native boundary:** the committed `loop-captured-store` source needs state
transport through an iterator method's `while` recurrence. Its finite loop
increments the captured cell until one; it remains refused under both optimization
policies. External cell observations, nested custom opens, body abrupt-close
behavior, literal range-for printing, unguarded Bootstrap defaults and the
application driver remain open. Full Bootstrap is not admitted.

[Exact checks and next boundary](handoff/2026-09-22-iterator-conditional-state.md).

## Confined iterator captures and identity masks, 2026-09-22 UTC

Continued clean **830181ec** from the recorded captured-counter boundary.
**76a7933e** admits initialized Number cells shared only by a confined iterator's
unique `next` and `return` methods. Complete cell/capture checks precede rewriting;
ordered scalar transport preserves shared identity, fresh invocation state and
latest close values. Remaining immutable capture slots are reindexed. Generic
helper capture admission stays immutable. Conditional writes, external cell
observations and nested closures remain refused.

The original captured-counter source executes unchanged. A two-cell source and
raw reordered-capture controls check update order and close state. Generated C++
uses typed scalar loops and public DOM calls, without boxed state or Script.
Parallel escape work **d66f0bc0** preserves odd strides through identity bitwise
masks within one signed conversion band, including zero crossings. Original OR
and AND zero-crossing sources are preserved and now admitted. Complete reload,
store and budget checks remain. No browser/oracle semantics changed.

Focused checks pass: exact host **1/1, 0.82 s total**; custom/nested/dataset lit
**3/3, 315.89 s**; exact arrays **1/1, 1.79 s total**; OR/XOR and AND lit
**2/2, 0.14 s**. Custom iteration completed **176 native executions, 356 refusals
and 66 Node/VM observations**. Nested iteration completed **48 native/two
previous-source checks/94 refusals**; dataset completed **112 Node/VM observations,
eight binaries, lifetime sanitizer and 432 refusals**. All nine final code/test
hashes match their gates. An initial stale OR refusal was corrected with its
source unchanged. Formatting retains **16 pre-existing diagnostics in four
untouched files**; changed files pass scoped checks. Full suites and broad
replays were skipped. The devbox idle timer is active/enabled.

**Next native boundary:** the committed `conditional-captured-store` source needs
branch-local state updates and scalar joins that preserve effects and close
values. Conditional receiver writes are its sibling boundary. External cell
observations, nested custom opens, body abrupt-close behavior, literal range-for
printing, unguarded Bootstrap defaults and the application driver remain open.
Full Bootstrap is not admitted.

[Exact checks and next boundary](handoff/2026-09-22-iterator-captured-state.md).

## Confined iterator receiver state and signed OR ranges, 2026-09-22 UTC

Continued clean **0bbb961f** from the receiver-counter boundary recorded in
HANDOFF, plan 00 and the iteration 47 journal. **dfb27eae** admits Number state on
one confined custom iterator. Unique ordinary methods access initialized own
fields directly; explicit scalar arguments and loop results preserve mutations,
reset state per allocation and deliver the latest values to the close hook.
Existing helper expansion removes private result records. Lexical-`this` arrows,
unknown fields, escaped holders and conditional writes remain refused. Typed
Number greater-than now passes the complete DOM proof.

The saved receiver-counter source executes unchanged. A two-field source checks
update order and the value seen by `return`; the saved captured-counter source
is now a committed refusal. Generated C++ uses typed scalar loops and public DOM
calls, with no VM iterator, boxed state or Script dependency.

Parallel escape work landed **8649d10d**: a sign-setting OR mask bounds outputs
across zero and signed conversion boundaries using one conservative negative
interval. Existing same-band precision, low-bit gaps, budgets and complete
reload/store checks remain. Eleven sources extend the unchanged original 58.
No browser implementation or runtime-oracle semantics changed.

Focused checks pass: exact arrays **1/1, 1.59 s total**; exact host **1/1,
0.77 s total**; OR/XOR and AND lit **2/2, 0.12 s**; custom/nested/dataset lit
**3/3, 246.53 s**. Custom iteration completed **144 native executions,
290 refusals and 54 Node/VM observations**. Nested iteration completed
**48 native/two previous-source checks/94 refusals**; dataset completed
**112 Node/VM observations, eight binaries, lifetime sanitizer and 432 refusals**.
Final native hashes match its gate; escape hashes match its earlier gate.
Formatting retains **16 pre-existing diagnostics in four untouched files**;
changed files pass scoped checks. Full suites and broad replays were skipped.
The devbox idle timer is active/enabled.

**Next native boundary:** the committed `mutable-captured-counter` source uses
an initialized local cell and `load_upvalue`/`store_upvalue` in `next`; it still
refuses under both optimization policies. Prove cell identity, mutation,
confinement and complete capture uses before scalarizing it. Conditional receiver
writes, body abrupt-close behavior, nested custom opens, literal range-for
printing, unguarded Bootstrap defaults and the application driver remain open.
Full Bootstrap is not admitted.

[Exact checks and next boundary](handoff/2026-09-22-iterator-receiver-state.md).

## Multiple loop exit values and constant OR masks, 2026-09-22 UTC

Continued clean **5f79f5fc** from its recorded two-projected-exit boundary.
**d0db67ad** transports multiple independently selected values through pure
break/exhaustion dispatch. Distinct compatible loop slots and simultaneous
selection preserve crossed and repeated outputs; complete use/effect/ownership
proofs and budgets remain required. Iterator close stays after the loop.
The original two-output source executes unchanged, and a three-output weighted
source distinguishes every output permutation. Generated C++ keeps typed scalar
loops and public DOM calls with no VM iterator or Script dependency.

Parallel escape work landed **11230889**: an exact all-one OR mask remains
constant across zero and signed conversion boundaries. CFG/SCF tests retain
near-mask and later-store controls; eight sources extend the unchanged original
50. No browser implementation or runtime-oracle semantics changed.

Focused checks pass: exact arrays **1/1, 1.57 s total**; exact host **1/1,
0.70 s total**; OR/XOR and AND lit **2/2, 0.12 s**; custom/nested/dataset lit
**3/3, 203.19 s**. Custom iteration completed **112 native executions,
212 refusals and 42 Node/VM observations**. Nested iteration completed
**48 native/two previous-source checks/94 refusals**; dataset completed
**112 Node/VM observations, eight binaries, lifetime sanitizer and 432 refusals**.
All seven final code/test hashes match the devbox. Formatting retains the same
**16 pre-existing diagnostics in four untouched files**; changed files pass
scoped checks. Full suites and broad replays were skipped; idle timer is active.

**Next native boundary:** state on a confined custom iterator. Saved complete
receiver-counter and captured-counter sources refuse for both providers and
both optimization policies (**eight probes**): the former lacks a proved mutable
holder slot, the latter an implicit-argument/capture proof. Sources, IR and exact
diagnostics are in `../test-results/2026-09-22-multiple-break-exits/state-probes/`
beside the monorepo. Body abrupt-close behavior, nested custom opens, literal
range-for printing, unguarded Bootstrap defaults and the application driver
remain unfinished. Full Bootstrap is not admitted.

[Exact checks and next boundary](handoff/2026-09-22-multiple-break-exits.md).

## Counted break exits and OR-mask proofs, 2026-09-22 UTC

Continued clean **51425445** from the recorded counted-break boundary; the
completed compiler-repair drivers were not repeated. **45029d8e** now transports
one live scalar through a pure break/exhaustion exit selection, preserving
updates before or after the break condition and other ordinary carried counters.
Exact tags, complete use checks and inactive-slot proofs retain the existing
helper/DOM authority. Iterator close stays after the loop. Output remains typed
C++ scalar loops and public DOM calls, without a VM iterator or Script dependency.

Parallel escape work landed **cc0fc749**: OR-mask trailing-one bits preserve
array-index gaps, combined with the input lattice by the larger power-of-two
period. CFG/SCF and ten new source witnesses retain overlap and signed controls.
No browser implementation or runtime-oracle semantics changed.

Focused checks pass: exact host **1/1, 0.69 s total**; arrays **1/1, 1.66 s total**;
OR/XOR and AND lit **2/2, 0.13 s**; final custom lit **1/1, 127.06 s**, with
**80 native executions, 166 refusals and 30 Node/VM observations**. The earlier
three-case run passed nested iteration (**48 native/two previous-source
checks/94 refusals**) and dataset (**112 Node/VM observations, eight binaries,
lifetime sanitizer, 432 refusals**), while custom failed a corrected fixture
ordering check. Production did not change afterward; those passes were retained.
All eight changed code/test hashes match the devbox. Required formatting retains
**16 pre-existing diagnostics in four untouched files**; changed files pass scoped
checks. Full suites and broad replays were skipped.

**Next native boundary:** the retained `two-projected-break-exits` source needs
two independently selected loop-exit values. Its two-result imported switch is
measured refused. A second ordinarily carried counter already executes and is
a positive test. Broader iterator state, body abrupt-close behavior, literal
range-for printing, unguarded Bootstrap defaults and the application driver
remain unfinished. Full Bootstrap is not admitted. The earlier conformance
crashes and WPT event regressions remain separate work.

[Exact checks and next boundary](handoff/2026-09-22-counted-break-exits.md).

## Compiler failure repairs, 2026-09-22 UTC

Continued the failures recorded by the full run of **a1d6680d**, then resumed
its interrupted focused repair drivers. Fixed array-contents handling of pure
`arith.constant` producers and nondeterministic class-heritage budget accounting.
Updated stale carrier, specialization, equality and DOM/class expectations while
preserving original JavaScript sources, oracle observations and refusal controls.
Lit now excludes the 51 `Inputs` fixtures; discovery contains 355 real cases.
No browser implementation or runtime-oracle semantics changed.

The three selected CTests pass: `ctcompile_escape_analysis_arrays` **1.61 s**,
`ctcompile_owned_global_shared_map` **258.39 s**, and
`ctcompile_host_contract_seeded_maps` **42.00 s**. **25 of the original 27 failing
lit cases have whole-case PASS results**. The other two drivers completed through
preserved prefixes and focused continuations: class initialization covers all
**836 source observations**; Global Map completed its remaining controls, with
**681 further rollback cutoffs** in its final **26 s** continuation. The final
Map access/trace guard check passes **16 deliberate mutations** in **9 s**.
These are focused results, not a fresh full CTest or compiler-lit pass.

All repairs are committed in small concerns. Required formatting still reports
**16 pre-existing diagnostics in four untouched files**; changed files pass
scoped formatting. Full suites, conformance, broad corpora and matrices were
not rerun. Logs, exact commands, source hashes and continuation scripts are in
`../test-results/2026-09-22-compiler-repairs/` beside the monorepo. The devbox idle
timer was stopped during tests and verified **active/enabled afterward**.

**Next native boundary:** `counted-break-exit`, transporting the live counter
across the importer's conditional break exit. Broader iterator state, literal
range-for printing, unguarded Bootstrap defaults and the application driver
remain unfinished. The earlier seven conformance crashes and two WPT event
regressions remain separate follow-up work. Do not repeat the completed compiler
repair drivers on the next iteration.

[Repair findings and exact validation](handoff/2026-09-22-compiler-failures.md).

## Full monorepo validation, 2026-09-21–22 UTC

The user explicitly requested full validation of clean **a1d6680d**. The devbox
build completed **776 actions**; all seven test stages finished in **2h46m26s**
including the build. **321/324 CTests passed** in **2811.14s**: browser **217/217**,
compiler **104/107**. Compiler lit finished in **2561.62s**, with **328 passed,
27 failed and 51 unresolved** among 406 discovered cases. The unresolved cases
are class-initialization input fixtures without RUN lines. The other failing
CTests are `ctcompile_owned_global_shared_map` and
`ctcompile_host_contract_seeded_maps`; the report enumerates all failures.

Both harness self-tests pass (test262 **11/11**, WPT **5/5**). The test262 gate
fails only for **166 newly passing expectations**, with zero regressions. WPT's
gate reports **22 unexpected failure signatures across two event tests**.
Full cached test262: **41983 PASS, 7386 FAIL, five TIMEOUT, three CRASH, 4203 SKIP**
(53580 files, **85.0%** of executed files pass). Full cached WPT selection:
**2950 PASS, 2679 FAIL, 529 TIMEOUT, four CRASH, 254 HARNESS_ERROR, 2479 SKIP**
(8895 plans). These are conformance measurements, not passing gates; the WPT
selection covers the nine cached sparse roots, not all upstream WPT.

The devbox did not reboot or shut down. Its idle timer was stopped for testing
and verified **active/enabled afterward**; six paused stale lit processes were
resumed. All logs, JSON/TSV rows and JUnit results are saved in
`../test-results/2026-09-21-full-monorepo-a1d6680d/` beside the monorepo, and in
`/home/ubuntu/ct-test-results/2026-09-21-full-monorepo-a1d6680d/` on the devbox.
Required formatting still reports **16 diagnostics in four untouched files**.
No implementation, expectation or golden changed.

**Next work:** triage the compiler proof/admission failures and old C++ carrier
assumptions, repair lit input discovery, and investigate the seven conformance
crashes and two WPT event regressions. Revalidate semantics before accepting
changed refusals. The native feature boundary remains `counted-break-exit`:
transport the live counter across the conditional break exit. Broader iterator
state, Bootstrap defaults and the application driver remain unfinished.
Future implementation work returns to the standing focused-test policy.

[Full findings and exact commands](handoff/2026-09-21-full-monorepo-tests.md).

## Confined custom DOM iteration, 2026-09-21 UTC

Continued clean **6e8cb697** and its recorded custom-iterator boundary.
**e52bec52** admits one confined self-iterating object with an exact ordinary
`Symbol.iterator` identity hook, `next` results with own done/value fields and
an optional empty-object return result. Immutable captured elements retain
their owner. Complete helper/DOM proofs check producers, effects and lifetimes.
Exhaustion skips return; a proved effect-only break closes once.

Private normalization replaces the protocol with ordinary calls and scalar
completion state before helper expansion. Empty dispatch and dead tuple
positions are removed without discarding source effects. Generated output uses
existing C++ loops and public DOM calls, with no VM iterator, boxed result record,
new runtime type or Script dependency. No browser/shared implementation changed.

Final focused checks pass: exact host-contract CTest **1/1, 0.67 s total** and
custom-iteration lit **1/1, 49.12 s**, with **32 native executions, 94 refusals
and 12 Node/VM source-double observations**. The preceding three-case run passed
nested iteration (**48 native/two prior-source checks/94 refusals**) and dataset
(**112 Node/VM observations, eight binaries, lifetime sanitizer, 432 refusals**)
while custom counted break failed; **two passed, one failed, 141.19 s**. That
source is preserved as a refusal. All ten final code/test hashes match devbox.
Scoped checks pass; required formatting retains 16 untouched diagnostics.
Full suites and broad replays were skipped.

**Next browser boundary:** resolve the live counter/exit alternatives in the
new fixture's `counted-break-exit` source. Existing completion proof cannot yet
transport that changed counter across the importer's conditional break exit.
Mutable iterator state, factories, nested custom opens, generators and body
return/throw close behavior remain unproved. Literal range-for printing,
unguarded Bootstrap defaults, broader Array behavior and the application driver
also remain unfinished. Full Bootstrap is not admitted.

[Exact checks and next boundary](handoff/2026-09-21-custom-dom-iteration.md).

## Nested DOM and Bootstrap iteration, 2026-09-21 UTC

Continued clean **ac5c984a** and its recorded nested-iteration boundary.
**44b2e8f6** admits nested `for…of` over direct query snapshots and unchanged
Bootstrap `R.find`, including guarded default outer roots and explicit inner
receivers. Private complete-entry proofs retain original loop state, query
placement, document guards, effects and owner/Style association. Materialized
indices need their own length guard; direct iteration and concat retain the
2^24 cap while original NodeList aliases stay uncapped.

Immutable helper holders now permit repeated reads inside structured loops,
with unique unconditional slot initialization and escape checks. Exact bounded
members select explicit helper receivers over their undefined defaults only
after complete mapped index reproof. Nullable values gain no such fact.
Generated C++ uses typed vectors, indexed loops and public DOM/Style calls;
literal range-for printing remains unfinished. No browser/shared implementation
or runtime type changed, and native output has no Script dependency.

Final focused checks pass: exact host-contract CTest **1/1, 0.63 s total** and
selected nested-iteration/dataset lit **2/2, 136.90 s**. Nested traversal completed
**48 native executions, two formerly refused source checks and 94 refusals**.
Dataset completed **112 Node/VM source-double observations**, eight GCC/Clang
binaries, its lifetime sanitizer and **432 refusals**. Earlier affected flat,
indexed and default-root cases passed **64/116**, **48/114** and **32/92**;
their accompanying nested failures were fixed before the final run. All eight
code/test hashes match devbox. Scoped checks pass; required formatting retains
16 untouched diagnostics. Full suites and broad replays were skipped.

**Next browser boundary:** prove a closed custom iterator's callable, typed `next`
result, state, effects, escape and abrupt-close behavior. Add a valid iterator
fixture while retaining the current custom-protocol refusal. Literal range-for
emission, unguarded default roots, broader Array behavior and the application
driver also remain unfinished.
Full Bootstrap is not admitted. No above-cap collection or new element-specific
Node/VM differential execution was measured.

[Exact checks and next boundary](handoff/2026-09-21-nested-element-iteration.md).

## Native element for-of, 2026-09-21 UTC

Continued clean **a4db6abb** and its recorded element-iteration boundary.
**6a8415be** admits `for…of` over proved element query snapshots and unchanged
Bootstrap `R.find` results, including guarded omitted/undefined receivers.
Private prefix proofs retain document-root guards and observe snapshot length
without returning borrowed elements. Direct NodeList iteration and confined
spread/concat preserve the 2^24 materialization cap; original NodeList aliases
remain uncapped. Complete live reproof checks effects, bounds and ownership.

Generated code uses the existing C++ indexed loop lowering over
`std::vector<js_element_t>`, with checked member access and public DOM/Style calls.
It does not yet print C++ range-for syntax. No VM iterator, Script dependency,
browser/shared implementation change or new runtime type was introduced.

Focused checks pass: exact host-contract CTest **1/1, 0.64 s total**; selected
element-iteration/find-elements/dataset lit **3/3, 196.05 s**. The new case completed
**64 native executions/118 refusals**; indexed `R.find` completed **48/114**.
Dataset regression completed **112 Node/VM source-double observations**, eight
GCC/Clang binaries, its lifetime sanitizer and **432 refusals**. All seven code/test
hashes match the devbox. Scoped checks pass; required formatting retains 16
untouched diagnostics. Full suites and broad replays were skipped.

**Next browser boundary:** loop-nested iterator opens need a prefix/state proof;
custom protocols need callable/close/escape proofs. Literal range-for emission is
also unfinished. Unguarded default roots, broader Array behavior and the application
driver remain open, along with the remaining native plan. Full Bootstrap is not
admitted. No above-cap collection or new element Node/VM comparison was executed.

[Exact checks and next boundary](handoff/2026-09-21-element-for-of.md).

## Bootstrap indexed element consumers, 2026-09-21 UTC

Continued clean **0ce0bdf3** from its recorded indexed-consumer boundary.
**6c32120f** admits canonical indexed loops over the unchanged Bootstrap `R.find`
result, including explicit receivers and guarded omitted/undefined defaults.
Each copied index must have its own `copied.length` guard before slots are
rebound to the original query snapshot. Complete live proof checks zero/unit-step
indices and the exact `min(snapshot.length, 16777216)` bound. Original NodeList
aliases keep their full length; array identity, mutation and escapes stay refused.

**bebdc40d** separately emits element query-all through the existing typed
`std::vector<js_element_t>` view, with checked member extraction and live Style.
Browser behavior remains in public ctbrowser DOM/Style, ownership stays with the
caller/session and generated output has no Script dependency. No browser/shared
implementation changed.

Focused validation passes: exact host-contract CTest **1/1, 0.66 s total**;
selected find-elements/query-all/spread-length lit **3/3, 141.75 s**, respectively
**48 native executions/114 refusals**, **16/44** and **32/52**. All eight code/test
hashes match the devbox. Scoped checks pass; required formatting retains 16
untouched diagnostics. Full suites and broad replays were skipped; no new Node/VM
or above-cap collection execution is claimed.

**Next browser boundary:** prove `for…of` over these element results. The existing
iterator normalizer proves String snapshots; element iteration must retain the
copied cap, owner/Style association, source iterator identities and guarded root
authority. Unguarded defaults, general Array behavior and the application driver
remain unfinished, along with the remaining native plan. Full Bootstrap is not
admitted.

[Exact checks and next boundary](handoff/2026-09-21-bootstrap-element-consumers.md).

## Guarded Bootstrap document defaults, 2026-09-21 UTC

Continued **8b6ff9d7** and resumed the interrupted default-root/typed-selector
draft recorded in the iteration 38 journal. **4500fada** admits the unchanged
vendor-pinned Bootstrap `R.find` with an omitted or explicit-`undefined` receiver
inside a source document-root guard. Missing helper arguments become exact
`undefined`; the original default arm selects `document.documentElement`.
Root presence is scoped to the proved guard, and immutable local callable slots
can be read inside `if` arms after unconditional initialization. Complete live
effect, receiver, escape and bounded-work proofs remain required.

**e6279f23** separately emits admitted `closest`/`querySelector` calls through
`js_element_t`, optional typed results and the existing checked nullable bridge.
Guarded chains preserve their live Style association. Both changes reuse public
ctbrowser DOM/Style and the existing scoped global document; no browser/shared
implementation changed and native output has no Script dependency.

Focused checks pass: exact host-contract CTest **1/1, 0.57 s total**; new
document-default lit **32 native executions/92 refusals, 86.49 s**; existing
spread-length lit **32/52, 84.42 s**; query/prototype-query lit **2/2, 168.70 s**,
respectively **16/14** and **64/100**. All 13 code/test hashes match the devbox.
Scoped checks pass; required formatting retains 16 untouched diagnostics.
Full suites and broad replays were skipped; no new Node/VM result is claimed.

**Next browser boundary:** `R.find` spread/concat results still permit only length
observations. Prove indexed/iterated element consumers using existing snapshot
ownership and bounds checks, preserving the original helper. An unguarded
default still lacks a root guarantee. Remaining typed snapshot emission, the
application driver, overlapping DOM Map keys, original B/Data+B, object-valued
fields, tagged Map snapshots, broader Strings, conditional callees and mutable
cells remain unfinished. Full Bootstrap is not admitted.

[Exact checks and next boundary](handoff/2026-09-21-guarded-bootstrap-root.md).

## Global native document, 2026-09-21 UTC

Continued clean **21c65d1c** and implemented the user's global-document request.
**096e3959** adds `ctnative::document` alongside `Element`, forwarding root/query
methods to the existing borrowed `js_document_t`. A noncopyable, nonmovable
`document_scope` binds the current document and Style for synchronous calls,
restoring the previous binding on return or exception. Separate threads have
independent bindings; explicit views and snapshots retain their own document.
Access outside a scope throws, and invalid Style cannot replace the active view.

**42c1406f** makes generated document entries establish that scope after all input
validation and call `ctnative::document` directly. The existing explicit owner
contract, null guards, snapshot bounds and escape/effect proofs remain intact.
Ownership stays with the caller/session, and browser behavior stays in public
ctbrowser DOM/Style. No Script dependency or new source admission was added.

Focused checks pass: exact native-runtime/host CTests **2/2, 0.58 s total**;
selected document/document-all lit **2/2, 128.07 s**, respectively **48 native
executions/82 refusals** and **32/84**. Clients verify outer-document restoration
on success, exceptions and rejected inputs; runtime checks include thread
isolation and saved views. All seven code/test hashes match the devbox. Scoped
checks pass; required formatting retains 16 untouched diagnostics. Full suites
and broad replays were skipped; no new Node/VM or full-Bootstrap result is claimed.
No browser/shared implementation changed.

**Next browser boundary:** the original Bootstrap `R.find` omitted receiver still
needs complete document-root presence and helper/concat proof. Global syntax
does not guarantee a root exists. Remaining selector carrier migration, the
application driver, overlapping DOM Map keys, original B/Data+B, object-valued
fields, tagged Map snapshots, broader Strings, conditional callees and mutable
cells remain unfinished.

[Exact checks and next boundary](handoff/2026-09-21-global-document.md).

## Typed document query snapshots, 2026-09-21 UTC

Continued **c786ecd6** and resumed the interrupted query-all draft.
**d5149745** admits source `document.querySelectorAll(String)` through the explicit
document binding. Native output keeps `std::vector<js_element_t>` snapshots and
extracts checked members for existing element operations. Exact receiver, bounds,
document/Style ownership, mutation and escape proofs remain enforced. Public
ctbrowser DOM/Style supplies the browser behavior; native output has no Script
dependency.

**607cf1be** separately corrects Shell collection reads to use actual membership,
removing the inconsistent numeric-index cap above 1,000,000. Overflow, canonical
parsing and actual-length checks remain; `ownKeys` and proxy-spread caps are
unchanged. This fixes supported-index consistency with `length`/`item()`. The
browser check covers huge absent indices, not a million-member VM collection.

Focused checks pass: new document-all lit **32 native executions, 84 refusals,
87.38 s**; existing document/element-query-all lit **2/2, 122.39 s**, with
**48/82** and **16/44** executions/refusals. Exact host-contract and dom_nodes_wpt
CTests each pass **1/1**, respectively **0.56 s** and **0.07 s** total. All eleven
code/test hashes match the devbox. Scoped checks pass; required formatting retains
16 untouched diagnostics. Full suites and broad replays were skipped. No new
Node/VM differential result or full-Bootstrap admission is claimed.

**Next browser boundary:** the original Bootstrap `R.find` helper's omitted
receiver needs a complete `document.documentElement` presence and helper/concat
proof. The binding alone does not guarantee a root exists. Remaining element
selector carrier migration, the application driver, overlapping DOM Map keys,
original B/Data+B, object-valued fields, tagged Map snapshots, broader Strings,
conditional callees and mutable cells remain unfinished.

[Exact checks and next default-root boundary](handoff/2026-09-21-document-snapshots.md).

## Explicit source document binding, 2026-09-21 UTC

Continued clean **8420a066** and implemented its next browser boundary.
**dbc75a2b** adds `current_document_parameter` to the DOM entry/session contracts.
It binds source `document` to a validated element input's owning document and live
Style engine. Proved `document.documentElement` and `document.querySelector(String)`
emit ordinary `js_document_t` calls over public ctbrowser DOM/Style. Results retain
null-only identity and guarded use; replacement, unguarded dereference and escape
remain refused. Ownership stays with the caller or existing nonmovable session.
No ambient document state or Script/VM dependency was added.

Focused checks pass: new document lit **48 native executions, 82 refusals,
121.50 s**; existing prototype-selector lit **64 executions, 100 refusals,
167.73 s**; exact host-contract CTest **1/1, 0.57 s total**. All 17 code/test hashes
match the devbox. Scoped checks pass; required repository formatting retains the
same 16 untouched diagnostics. Full suites and broad replays were skipped; no new
Node/VM measurement or browser/shared implementation change is claimed.

**Next browser boundary:** prove source `document.querySelectorAll` snapshots and
their existing element-vector consumers with the same owner/Style association.
Bootstrap's default root still needs a complete presence/nullability proof; the
new binding does not guarantee a root exists. Remaining typed selector carriers,
the application driver, overlapping DOM Map keys, original B/Data+B, object-valued
class fields, tagged Map snapshots, broader Strings, conditional callees and
mutable cells remain unfinished. Full Bootstrap is not admitted.

[Exact checks and next document boundary](handoff/2026-09-21-source-document.md).

## Typed browser views and DOM key lifetimes, 2026-09-21 UTC

Continued clean **4b76250f** and finished the alias thread left by a steering
interruption. **dee4ff0a** adds borrowed C++ `js_document_t`/`js_element_t` views over
public ctbrowser DOM/Style, with explicit null-only results, snapshots and receiver-only
prototype selectors. **32f5311b** emits proved `matches` calls through the typed
receiver and `js_string`. Ownership remains ordinary C++; no Script/VM fallback.

**c90e4545** permits sequential DOM-input Map keys after the prior key is retired,
while retaining saved child aliases and all original class/Data observations.
Four complete sources execute for equal/distinct inputs with results **15927,
59112, 15939 and 15951**. The provider admits fully unobserved declared inputs,
but native sessions still validate every input before effects. The two original
alias-dependent witnesses remain refused: distinct inputs throw before publication.

Focused validation: alias fixture **32 native executions, 23 refusals, six Node/VM
observations, four preparations and four provider proofs**; final alias/DOM-input
lit selection **2/2, 51.58 s**. Prototype-selector lit passes **64 executions and
100 refusals**. Exact native-runtime/host-contract CTests pass **2/2, 0.57 s**;
later host-contract check **1/1, 0.55 s**. All eleven code/test hashes match the
devbox. Scoped checks pass; required repository formatting retains 16 untouched
diagnostics. Full suites and broad replays were skipped. No browser/shared edits.

**Next browser boundary:** prove an explicit source `document` binding and guarded
root/query results through the new C++ view, reusing the live document/Style owner.
The application driver and remaining typed selector-result emission are incomplete.
Overlapping DOM-key lifetimes, original B/Data+B, object-valued fields, tagged Map
snapshots, broader Strings, conditional callees and mutable cells remain. Full
Bootstrap is unfinished.

[Exact checks and next source-document boundary](handoff/2026-09-21-browser-views-dom-key-lifetimes.md).

## Native Bootstrap results and browser prototype calls, 2026-09-21 UTC

Continued clean **d74968ab**, then resumed the saved draft after an interruption.
**58803194** publishes the unchanged original vendor-derived class/Data results
through native DOM sessions: holder **15927**, constructor **59112**. Preparation
reuses the proved local Map operations and read-time field categories, including
read-only branch arms and Boolean arithmetic. Five previous refusal sources now
execute unchanged; a sixth complete-source witness observes Map has/delete/clear.
The provider and owner proofs still run independently; no VM/GC fallback was added.

Following the user's browser-integration direction, **95ea1b19** connects source
`Element.prototype.matches.call` and `closest.call` to their existing typed C++
method objects over public ctbrowser DOM/Style. Boolean results, nullable element
borrows, document identity and explicit Style association remain checked.
**616ea0f6** separately admits two literal negations in String indices.

Focused validation: composite/constructor selectors **48 native executions,
46 refusals and six Node/VM observations**; prototype-selector lit **64 executions,
100 refusals, 167.72 s**; two selected DOM-input/provider lit cases pass; final
exact host-contract CTest **1/1, 0.60 s total**. String selection passes **64
executions, 148 refusals, six Node/VM agreements and one known UTF-16 difference**.
The whole native-class-DOM replay was interrupted; it is **not a full-case pass**.
Scoped checks pass; repository formatting retains 16 untouched diagnostics.
No browser/shared implementation changed; full suites and broad replays were skipped.

**Next:** the existing two-input class witnesses still require
`class nested Map DOM inputs require an alias proof` in
`ClassInitialization/CapturedMapHelpers.cpp`. Prove both equal/distinct DOM-input
partitions without treating different formals as distinct nodes or discarding
Map observers. Browser integration also needs the planned typed document/element
views and application driver. Original B/Data+B, object-field ownership, tagged
Map snapshots, broader Strings, conditional callees and mutable cells remain.
Full Bootstrap is unfinished.

[Exact checks, interruption scope and next boundary](handoff/2026-09-21-composite-results-double-negation.md).

## Optional Map payloads and negated primitive indices, 2026-09-21 UTC

Continued clean **72040551**. **c3dc2da0** carries closed optional scalar
Map payloads through existing tagged storage, arguments and reads, preserving
null, undefined, missing entries, Boolean identity, NaN and negative zero.
Nine unchanged class/Data sources now execute in addition to the previous six:
**15 vendor-derived sources, 120 native class executions**. Constructor-only
fields, saved reads and Boolean fields retain their original class computations.
Source/provider/ownership proofs are unchanged; the two object-valued field
mutation witnesses still refuse. Tagged scalar value snapshots also remain refused.
**ffb18caa** admits exact negated null/Boolean String indices and supplies the
missing exact host-null-to-Number conversion. Dynamic coercion and literal-only
dataset authority remain unchanged.

Focused checks pass: native DOM lit **296.24 s**, including **120 class, 56 record
and 24 object-key executions**; two selected Map-key lit cases **10.83 s**;
mixed-Map lit **73.63 s**; exact runtime/host CTests **2/2, 0.57 s total**.
The scalar Map selector passes **20 executions and ten Node/VM agreements**.
String selection passes **64 executions, 136 refusals, six Node/VM agreements
and one known UTF-16 difference**. All eleven code/test hashes match the devbox;
scoped formatting passes, while the required repository check retains the same
16 untouched diagnostics. Full suites and broad replays were skipped. No browser
or shared implementation changed.

**Next:** both unchanged original composite-result publications still refuse
preparation with `class DOM input has an observer outside its proved Map keys`.
Extend category evidence for the complete local Map/record computation, including
Map observations and Boolean numeric coercion, without dropping any observer or
owner. Multiple DOM-input alias partitions, original B/Data+B, tagged Map value
snapshots, broader Strings, conditional callees, mutable cells, String ordering,
document views and the application driver remain. Full Bootstrap is unfinished.

[Exact validation and next preparation boundary](handoff/2026-09-21-optional-map-payloads.md).

## Constructor lifting and native class sessions, 2026-09-21 UTC

Continued clean **0bbed840**. **6a333142** lifts exact proved constructors in the
DOM Data host path, preserving their original callable and fresh instance.
The provider rebuilds the allocation, initializer, field, Map and symbol census;
initialization must precede every instance observation. Final ownership is
reproved after rewriting. Generic constructor guards remain unchanged.
**Six existing vendor-derived class/Data sources now execute natively**, including
class-field publication and the complete retained vendor Data declaration.
Their independently observed class results remain 15927 or 59112. This is not
full Bootstrap admission. **e2bf7729** adds two-level ASCII prefix concatenation
for startsWith without widening literal-only dataset authority.

Focused validation passes: final DOM lit **181.45 s**, with **48 class, 56 record
and 24 object-key native executions**; exact ownership/host CTests **2/2,
246.78 s total**; and the three selected DOM-input/generic-constructor lit cases.
New live-IR controls cover exact callees, receivers, initialization order,
repeated initialization, escaped results, fingerprints, budgets and forged
reports. String checks pass **56 native executions, 112 refusals and six
Node/VM agreements**. Existing source generators, source tables and hostile
source bodies are unchanged. All six code/test hashes match the devbox; scoped
formatting passes. The repository formatter retains 16 untouched diagnostics.
No browser/shared implementation changed; full suites and broad replays were skipped.

**Next:** the unchanged constructor-only saved-field witness now refuses the
native carrier `Map<DOMElement, Optional<Number>>`, after successful constructor
lifting and fresh ownership proof. Extend closed optional scalar Map storage,
arguments and reads while retaining null/undefined/missing-entry distinctions,
or prove field presence before narrowing. Original composite-result publication
still refuses preparation. Multiple DOM-input alias partitions, original
B/Data+B, broader Strings, conditional callees, mutable cells, String ordering,
document views and the application driver remain unfinished.

[Exact checks and remaining carrier boundary](handoff/2026-09-21-constructor-lifting-prefixes.md).

## Local constructor ownership and ASCII prefixes, 2026-09-21 UTC

Continued clean **0f8628da**. **32a1ca2f** composes the final DOM Data provider's
complete local constructor/Map graph with native source ownership. The owner
census retains exact functions, allocations, calls and field reads, including
numeric observations in read-only branches and unobserved holder/prototype
allocations left by preparation. Every field store must have a primitive category;
an unread object field still refuses ownership. The unchanged complete vendor
constructor witness now reaches constructor lifting and native type admission.
**Native class-session emission still refuses.** **fc440f29** admits one
concatenation of ASCII literals in `startsWith`, preserving ordinary String
addition and direct-literal dataset authority.

Final focused checks pass: exact host-contract and shared-Map ownership CTests
**2/2, 248.05 s total**, two selected DOM lit cases **2/2, 109.42 s total**,
and the final constructor selector **nine observations/preparations/provider
proofs and 33 refusals**, plus its 16 prepared-IR controls. New ownership tests
cover 28 source/prepared rows, complete censuses and budget/fingerprint controls.
Existing DOM checks retain **56 record and 24 object-key native executions**.
String checks pass **40 native executions, 76 refusals and four Node/VM
agreements**; all 147 earlier complete cases and 33 general refusals are unchanged.
All nine final code/test hashes match the devbox. Scoped checks pass; repository
formatting retains 16 pre-existing diagnostics. Full suites, broad corpus/matrices,
full Bootstrap, WPT/test262, Windows and new sanitizers were skipped. No browser
or shared implementation changed.

**Next:** explicitly lift these proved constructors in the host method-table
path, retaining their exact callee provenance and recomputing the source graph
after rewriting. Both policies now report `standard Map identity is unproved
across an unknown constructor`, alongside unlifted closure/receiver diagnostics.
Do not relax that guard globally: the ordinary constructor lift still rejects
Map payload uses and substitutes an undefined callee. Original composite-result
publication still refuses class preparation. Multiple DOM-input alias partitions,
original B/Data+B, broader Strings, conditional callees, mutable cells, String
ordering, document views and the application driver remain. No full-Bootstrap
admission or corpus coverage gain is claimed.

[Exact checks and next constructor boundary](handoff/2026-09-21-local-owner-prefixes.md).

## Constructor provider graphs and nested indices, 2026-09-21 UTC

Continued clean **955c764d**. **aded05d2** independently proves the prepared
constructor/local-Map graph in the final DOM Data provider: exact closures and
primitive actuals, nonreplacement returns, complete Map/record uses and ordered
field categories. Seventeen existing complete vendor witnesses now prove all five
public call edges while retaining five constructions, eight functions and the
original class computation. **Native class-session emission still refuses.**
**ccd97336** admits two binary levels of signed literal arithmetic as String
indices, with a complete intermediate-use census. Ten former refusal sources
execute unchanged; ordinary Number addition retains its existing behavior.

Focused checks pass: exact host-contract CTest **1/1**, three selected DOM/provider
lit cases **3/3, 105.21 s total**, final provider **16 prepared-IR refusals** plus
forged-report recomputation, and String **128 native executions, 174 refusals**.
Existing DOM checks retain **56 record and 24 object-key native executions**.
String observations have 12 Node/VM agreements and one known UTF-16 difference;
all 134 prior complete String cases and 33 general refusals are unchanged.
All eight final code/test hashes match the devbox. Scoped checks pass; repository
formatting retains 16 pre-existing diagnostics. Full suites, broad corpus/matrices,
full Bootstrap, WPT/test262, Windows and new sanitizers were skipped. No browser
or shared implementation changed.

**Next:** the measured native owner refusal is `owned global method table requires
unconditional straight-line operations`. Extend its branch, function, allocation,
call and field census through the complete live constructor/local-Map graph;
then lift proved constructors while preserving exact callee provenance. Provider
categories authorize no native lifetime. Original composite-result sources still
refuse class preparation. Multiple DOM-input alias partitions, original B/Data+B,
broader Strings, conditional callees, mutable cells, String ordering, document
views and the application driver remain. No full-Bootstrap admission or corpus
coverage gain is claimed.

[Exact checks and next native ownership boundary](handoff/2026-09-21-provider-records-nested-indices.md).

## Constructor field initialization and signed indices, 2026-09-21 UTC

Continued clean **7b055fd6**. **49eeb949** retains primitive constructor field
categories at each original allocation for public DOM Data preparation. Literal
stores and formals with exact literal actuals no longer require an entry overwrite;
separate instances, repeated stores and saved reads preserve their own categories
and source order. Three former refusal sources now prepare unchanged. All five
constructions and eight functions remain; native class-session ownership still
refuses. **91e64124** admits signed Number-literal operands in the existing
arithmetic String bounds, preserving the complete use census and literal-only
casing authority. Eleven exact former refusal sources now execute.

Focused checks pass: exact host-contract CTest **1/1**, selected DOM lit **2/2,
110.27 s total**, and the String selector **128 native executions, 84 refusals**.
New constructor checks pass **nine Node/VM observations, nine preparations and
33 refusals**. Existing DOM tests retain **56 record and 24 object-key native
executions**. String observations have 13 Node/VM agreements and one known UTF-16
difference. All four final code/test hashes match the devbox; scoped formatting
passes. Repository formatting retains 16 pre-existing diagnostics. Full suites,
broad corpus/matrices, full Bootstrap, WPT/test262, Windows and new sanitizers
were skipped. No browser/runtime implementation changed.

**Next:** the measured provider refusal is `unsupported provider behavior through
ctjs.construct`. Recompute constructor/local-Map/field evidence in the final
provider analysis, extend the native owner function/allocation census, then
explicitly lift the proved constructors in the host path. Preparation categories
alone authorize none of those steps. The full vendor composite result still needs
local Map observations and Boolean-to-Number category proof. Multiple DOM alias
partitions, original B/Data+B, broader Strings, conditional callees, mutable cells,
String ordering, document views and the application driver remain. No full-Bootstrap
admission or corpus coverage gain is claimed.

[Exact checks and next ownership sequence](handoff/2026-09-21-constructor-fields-signed-indices.md).

## Class field payloads and negated indices, 2026-09-21 UTC

Continued clean **b3c7edae**. **356373ed** prepares public DOM Data calls whose
payload comes from an explicitly overwritten class field. The existing local
Map proof now retains each saved read's original constructor allocation; a
bounded source-order field walk supplies category evidence after the complete
class examination. Five witnesses retain the full vendor Data declaration,
original class computation, five constructions and eight functions. Saved reads
survive later object-valued mutation; other records, missing writes, escaping
aliases and replacement-object constructors supply no evidence.
**81e2c057** admits one outer negation of an already-proved Number-literal
arithmetic String index, reusing existing UTF-16 lowering.

Focused checks pass: exact host-contract CTest **1/1** and selected DOM lit
**2/2, 103.68 s total**. New class-field checks pass **five Node/VM observations,
five preparations and 29 refusals**. Existing DOM tests retain **56 record and
24 object-key native executions**. The String selector passes **80 native
executions and 70 related refusals**, with eight Node/VM agreements and one known
UTF-16/byte-index difference. All ten final code/test hashes match the devbox.
Scoped formatting passes; the repository formatter retains 16 pre-existing
diagnostics. Full suites, broad corpus/matrices, full Bootstrap, WPT/test262,
Windows and new sanitizers were skipped. No browser/runtime code changed.

**Next:** native class-session ownership still refuses. Compose the retained
constructors/local Maps with the provider source and native owner function/allocation
censuses. Constructor-initialized fields without an explicit entry overwrite,
and the complete vendor composite `result`, still lack scalar proof;
the unchanged `published_source()` witnesses retain those refusals.
Multiple DOM alias partitions, original B/Data+B, broader Strings, conditional
callees, mutable cells, String ordering, document views and the application
driver remain. No full-Bootstrap admission or coverage gain is claimed.

[Exact checks and remaining native ownership boundary](handoff/2026-09-21-class-field-payloads-negated-indices.md).


## Class/public-family preparation and power indices, 2026-09-21 UTC

Continued clean **ae517a4b**. **87789ef2** composes remaining DOM input uses
with a complete published captured-Map family during class preparation. Exact
input operands, the root slot, wrapper/factory calls and initialization order
are checked; wrapper/factory bodies retain the complete source census. Three
witnesses preserve complete vendor Data, observable class results, five
constructions and eight functions. Their public payload is independently proved;
**actual class-result publication and native class-session ownership still refuse**.
**b3cf5e4c** adds direct Number-literal power String bounds through existing
JavaScript exponentiation, preserving the bounds-use and literal-only casing gates.

Focused validation passes: exact host-contract CTest **1/1** and selected DOM lit
**2/2, 101.80 s total**. New family checks pass **five Node/VM observations,
three preparations and 29 refusals**; the existing DOM fixture passes **56 record
and 24 object-key native executions**. The power selector passes **40 native
executions and 60 related refusals**, with three Node/VM agreements and one known
UTF-16/byte-index difference. All eight code/test hashes match the devbox. Scoped
checks pass; repository formatting retains 16 pre-existing diagnostics. Full
suites, broad corpus/matrices, full Bootstrap, WPT/test262, Windows and new
sanitizers were skipped. No browser/runtime code changed.

**Next:** carry the actual class-derived scalar into the public family using
original constructor/record allocation and read-time evidence, then compose the
native provider and owner function/allocation census. The new
`published_families()` supplies the complete preparation shape;
`published_source()` retains the unchanged actual-result refusals. Multiple DOM
alias partitions in local class Maps, original B/Data+B, broader Strings,
conditional callees, mutable cells, String ordering, document views and the
application driver remain. No full-Bootstrap admission or coverage gain is claimed.

[Exact checks and next ownership boundary](handoff/2026-09-21-class-public-family-power.md).

## Saved field snapshots and addition indices, 2026-09-21 UTC

Continued clean **5d790a74**. **fbba77fa** admits global snapshots of scalar
own-field reads from exact caller records in a completed DOM Data family. Both
the source and owner proofs require the original allocation and exact read;
read-time values survive later field mutation. The former global-snapshot refusal
executes unchanged, with alias/mutation and arithmetic witnesses.
**f5d36487** admits direct Number-literal addition as a UTF-16 String bound,
reusing existing numeric addition. Direct-literal casing authority is unchanged.

Focused validation passes: exact host-contract CTest **1/1**, final DOM lit
**101.25 s** with **56 record and 24 object-key native executions**, plus DOM input
and provider-object proof cases. The String selector passes **40 native
executions, 868 proof refusals, 118 Node/VM agreements and 15 known differences**;
four strengthened casing refusals pass separately. All eight final code/test
hashes match the devbox. Scoped checks pass; repository formatting retains 16
pre-existing diagnostics. All 107 earlier complete intrinsic cases and 33 general
refusals are preserved. Full suites, broad corpus/matrices, full Bootstrap,
WPT/test262, Windows and new sanitizers were skipped. No browser/runtime code
changed; the initial fixture expectation was corrected to compare the saved global.

**Next:** the complete vendor published class holder/constructor still refuses.
Compose residual DOM uses with the completed public family, then the existing
constructor/local-Map/record proof and exact owner function/allocation census.
Global field snapshots are now supported; they do not establish class ownership.
Multiple DOM-input alias partitions, original B/Data+B, broader Strings,
conditional callees, mutable cells, String ordering, document views and the
application driver remain. No full-Bootstrap admission or coverage gain is claimed.

[Exact checks and remaining class boundary](handoff/2026-09-21-field-snapshots-addition.md).

## Published record fields and remainder indices, 2026-09-21 UTC

Continued clean **c475dde8**. **f65dedd5** admits scalar own-field reads from exact
fresh local records as arguments to a real published DOM Data family. The
existing complete caller-leaf census retains all fields, aliases and observers;
read-time category evidence does not replace the original read. Four witnesses
execute with real document owners, including aliases, saved reads before mutation
and arithmetic. The complete vendor Data/class publication witnesses now have a
real wrapper/factory/root, but **still refuse class preparation and emission**.
**5bdb9644** admits remainder of two direct Number literals as
UTF-16 `charAt`/`slice` bounds through the existing numeric and clamping path.

Focused validation passes: DOM class/record lit **66.21 s**, with **32 new record
and 24 existing object-key native executions**; DOM input proof **1.11 s**;
provider object proof **3.34 s**; exact host-contract CTest **1/1**. The added global
field-snapshot control separately refuses in both native policies; no whole DOM
fixture replay after that addition is claimed. The String selector passes
**40 native executions, 822 proof refusals, 115 Node/VM agreements and 14 known
differences**. All seven final hashes match the devbox; scoped checks pass and
repository formatting retains 16 pre-existing diagnostics. Earlier class source
generators, 104 complete intrinsic cases and 33 general refusals are preserved.
Full suites, broad corpus/matrices, full Bootstrap, WPT/test262, Windows and new
sanitizers were skipped. No browser/runtime implementation changed.

**Next:** compose the class/local-Map proof with the now-tested public family.
Class preparation still rejects residual DOM input uses; retained constructors,
local Maps and the complete owner function/allocation census remain separate
requirements. Preserve those gates; `published_source()` supplies the complete
vendor witnesses. Global field snapshots, multiple DOM-input alias partitions,
original B/Data+B, broader Strings, conditional callees, mutable cells, String
ordering, document views and the application driver remain unfinished.
No full-Bootstrap admission or coverage gain is claimed.

[Exact checks and next boundary](handoff/2026-09-21-published-record-fields.md).

## Constructor identity keys and multiplication indices, 2026-09-21 UTC

Continued clean **4b10fb96**. **37bfa1f5** preserves exact fresh-object or declared
DOM outer keys through terminal constructor Data registration. The second key
stays a literal String; a key formal with any other direct observer cannot be
removed. Original evaluation, helper expansion, child/record owners and complete
callee/symbol proofs remain. The old constructor-object-key source now executes
unchanged. Three complete-vendor object witnesses execute, and two DOM constructor
witnesses prepare. **Native DOM session emission still refuses.**
**0a9755ee** adds one direct Number-literal multiplication as a UTF-16 String bound,
including produced NaN, overflow and underflow through the existing path.

Focused checks pass: exact host-contract CTest **1/1**; final DOM preparation lit
with **24 native object-key executions**, four prepared entries, 30 preparation
and eight session refusals; constructor selector **56 main native executions,
38 unprepared and 26 preparation refusals**; String selector **40 executions,
768 proof refusals, 112 Node/VM agreements and 13 known differences**. All seven
final hashes match the devbox; scoped checks pass and repository formatting
retains 16 pre-existing diagnostics. All 51 earlier class sources and 101 prior
complete intrinsic cases are preserved. Full suites, complete captured/export
execution replays, DOM Strings replay, broad corpus/matrices, full Bootstrap,
WPT/test262, Windows and new sanitizers were skipped. No browser/runtime
implementation changed.

**Next:** compose the local class/Map proof with a real published DOM Data family.
Class preparation currently consumes all DOM key uses, while the session requires
a completed captured input family and actual factory/root publication. Its source
census also rejects the retained constructors. Preserve all three checks; start
with scalar-result publication through the existing real wrapper/factory shape.
Multiple DOM inputs, original B/Data+B, broader Strings, conditional callees,
mutable cells, document views and the application driver remain unfinished.
No full-Bootstrap admission or coverage gain is claimed.

[Exact checks, corrected setup and next boundary](handoff/2026-09-21-constructor-identity-keys.md).

## Object keys, DOM Data preparation and subtraction indices, 2026-09-21 UTC

Continued clean **c455efb0**. **1add0418** proves fresh object outer-key identity
through the complete vendor Data holder, retaining aliases, distinct objects,
String-key separation and record/child owners. Three new witnesses and the
unchanged prior nullable-object-key source execute natively. The same pass now
prepares one exact DOM element input and its aliases, rejecting ambiguous inputs
and residual observers. **DOM preparation still has no native session admission.**
**30ce626e** adds one direct Number-literal subtraction as a UTF-16 String bound,
using the existing NaN/default/clamping path and preserving literal-only authority.

Focused checks pass: exact host-contract CTest 1/1; new DOM preparation lit;
class selector **48 main native executions, 18 unprepared and 19 preparation
refusals**; String selector **40 executions, 722 refusals, 109 Node/VM agreements
and 12 known differences**. All 12 final hashes match the devbox. Scoped checks
pass; repository formatting retains 16 existing diagnostics. Earlier complete
source fixtures are preserved. Full suites, complete captured/export execution
replays, DOM Strings replay, broad corpus/matrices, full Bootstrap, WPT/test262,
Windows and new sanitizers were skipped. No browser/runtime implementation changed.

**Next:** compose prepared class/local-Map ownership with the DOM Data provider.
Its first measured refusal is `unsupported provider behavior through ctjs.construct`;
the existing session proof also requires a published method-table owner. Multiple
DOM input aliases and constructor transport of nonliteral keys remain unproved.
Original B/Data+B, broader Strings, conditional callees, mutable cells, document
views and the application driver remain unfinished. No full-Bootstrap admission
or coverage gain is claimed.

[Exact checks, corrected expectations and next boundary](handoff/2026-09-21-object-keys-dom-preparation.md).

## Complete vendor Data holders and division String indices, 2026-09-21 UTC

Continued clean **29057f08**. **a96d7d2a** preserves the complete Data conflict
observer arm until every invocation proves it unreachable. **fb5b8173** removes
only the redundant guard on an unread arrow receiver. Two native fixtures now
retain the entire vendor Data declaration, including its arrow getter and
early-return removal, with literal String element keys. Live receiver reads,
unused observers and reachable conflicts still refuse. **cd8fb905** admits one
division of Number literals as a UTF-16 String index, normalizing NaN to zero;
direct-literal casing/dataset authority is unchanged.

Three selected lit cases pass. Captured helpers measured **235 observations,
464 main native executions, 470 unprepared and 311 preparation refusals** before
the arrow extension. Its final focused selector passes **32 native executions,
12 unprepared and 11 preparation refusals**, with vendor budget **5952**.
Final String checks pass **48 executions, 678 refusals, 106 Node/VM agreements
and 11 known differences**. Final host-contract CTest passes 1/1; all eight final
code/test hashes match the devbox. Scoped checks pass; repository formatting
retains 16 existing diagnostics. No browser/runtime code changed. Full suites,
broad corpus/matrices, full Bootstrap, WPT/test262, Windows and new sanitizers
were skipped; the complete class selection was not replayed after the arrow-only
extension.

**Next:** actual DOM element-key identity and ownership through public
DOM/HostContract APIs. The original B/Data+B specimen still refuses; reachable
conflicts, broader String operations, conditional callees, mutable cells, String
ordering, document views and the application driver remain. No full-Bootstrap
admission or new corpus coverage measurement is claimed.

[Exact checks, corrected probes and next boundary](handoff/2026-09-21-data-conflicts-division-indices.md).

## Nullable Data getters and primitive String indices, 2026-09-21 UTC

Continued clean **b2d1a4af** from its recorded nullable Data boundary.
**cdbc468e** follows the original `has && get || null` getter,
retaining selected-child identity, read-time absence and present record owners.
Only consumed lookup truths require the complete child observer census; missing
reads disappear only after every non-root observer is discharged. Three new
admissions cover direct helpers, Data holders and constructor publication.
**b64244a7** adds literal null and Boolean String indices through UTF-16; null ends
select zero while undefined ends retain their default. The original Number-only
first-unit/dataset-tail authority is unchanged.

Captured helpers pass with **224 source observations, 440 main native executions,
448 unprepared and 295 preparation refusals**, with new first-complete budget
**6600**, followed by the strengthened raw-absence control's targeted replay.
Terminal publication and DOM Strings pass. The final String selector passes
**56 native executions, 638 proof refusals, 102 Node/VM agreements and ten known
differences**; the final exact host-contract CTest passes 1/1. All eight final
code/test hashes match the devbox; all 47 earlier class sources are unchanged.
Scoped formatting passes; repository formatting retains 16 existing diagnostics
in untouched files.
No browser/runtime implementation changed. Full suites, broad corpus/matrices,
full Bootstrap, WPT/test262, Windows and new sanitizers were skipped.

**Next:** original Data's intact conflict-reporting observer arm, then actual DOM
element-key ownership. Original B/Data+B remains a complete-source refusal.
Dynamic/coercing String indices, NaN origins, broader methods, conditional callees,
mutable cells, String ordering, document views and the application driver remain.
No full-Bootstrap admission or coverage gain is claimed.

[Exact checks, corrected failures and next proof seam](handoff/2026-09-21-nullable-data-primitive-indices.md).

## Child Map cleanup and explicit undefined String indices, 2026-09-21 UTC

Continued clean **d8ec7a5a** from the recorded child cleanup boundary.
**0bdef7c9** proves literal-key child `has`, `size` and `delete` observations
for guarded removal and empty-parent cleanup. Child operations remain for the
existing owner proof; both branch arms and every saved/captured alias are checked.
Shared children published to another outer Map remain refused. The new budget
regression also exposed invalidated LLVM DenseSet iteration; `remove_if` now
removes dead-arm operations safely, with three identical **6217** budget replays.
**17509a81** admits proved explicit undefined String indices, preserving zero
starts and omitted ends through the existing UTF-16 path. Three original refusal
bodies now execute unchanged; direct-literal method authority is unchanged.

Focused validation: **one exact CTest, three distinct lit cases and targeted
intrinsic exports**. Captured classes measure **207 source observations, 416 main
native executions, 414 unprepared and 273 preparation refusals**. String checks completed **64 native
executions** before a new refusal expectation was corrected; the final proof-only
rerun passes **596 refusals, 98 Node/VM agreements and nine known differences**.
No final whole-export-fixture pass is claimed. All eight final code/test hashes
match the devbox; all 46 earlier class sources are byte-identical. Scoped checks
pass; repository formatting retains 16 existing diagnostics in untouched files.
No browser/runtime implementation changed. Full suites, broad corpus/matrices,
full Bootstrap, WPT/test262, Windows and new sanitizers were skipped.

**Next:** original Data's nullable getter, then conflict reporting and actual
DOM element-key ownership. The getter must retain selected-child identity and
record-or-null results; the conflict observer arm must remain intact. Original
B/Data+B still refuses with complete sources. Dynamic/coercing String indices,
NaN origins, broader methods, conditional callees, mutable cells, String ordering,
document views and the application driver remain. No full-Bootstrap admission or
coverage gain is claimed.

[Exact checks, corrected failures and next proof seam](handoff/2026-09-21-child-cleanup-undefined-indices.md).

## Inherited Data publication and omitted String indices, 2026-09-21 UTC

Continued clean **97f32ae8** from its recorded inherited Data boundary.
**5ab02710** reuses super normalization to move three-argument registration past
one unique leaf's proved literal/numeric completion. Both key arguments and the
original capture cell retain their identities. Captured helper expansion, nested
Map routing and the complete record-owner/observer census remain required.
The original file-45 inherited witness executes unchanged; three new bodies cover
saved aliases, replacement/recreation and a numeric leaf suffix.
**19d43e03** admits omitted `charAt()`/`slice()` indices as zero through the existing
UTF-16 path. Length is emitted only for explicit indices; first-unit lowercase
and dataset-tail authority retain their direct-literal restrictions.

Focused validation passes: **one exact CTest, three distinct lit cases and a
targeted intrinsic-export check**. Captured classes measure **187 observations,
392 main native executions, 374 unprepared and 248 preparation refusals**, with
new budget **4712**. The final export selector measures **48 native executions,
564 refusals, 93 Node/VM agreements and eight known differences**. The whole export
case was not replayed after fixing its unused length variable; its preceding run
completed 648 earlier executions and two mutations before that new-case failure.
An earlier expectation-ordering failure at observation 100 was also corrected.
All **11 final code/test hashes** match the devbox; all 45 earlier class source
files are unchanged. Scoped checks pass; repository formatting retains 16 existing
diagnostics. No browser/runtime code changed. Full suites, broad corpus/matrices,
full Bootstrap, WPT/test262, Windows and new sanitizers were skipped.

**Next:** child `has`/`size`/`delete` evidence for original Data's guarded removal
and empty-parent cleanup, retaining every unused slot, owner and exception/reentry
observer. Current nested routing evaluates outer topology and requires a present
child. Conflict reporting, nullable lookup, actual element keys and DOM ownership
remain. Original B/Data+B still refuses with complete sources. Dynamic/coercing
String indices, NaN origins, broader methods, conditional callees, branch-mutated
boxed locals, String ordering, document views and the application driver remain.
No full-Bootstrap admission or coverage gain is claimed.

[Exact checks, failures and next proof seam](handoff/2026-09-21-inherited-data-string-defaults.md).

## Constructor Data publication and wide String indices, 2026-09-21 UTC

Continued clean **d5631824** from the recorded constructor-time Data boundary.
**f5b9fa07** moves an exact terminal three-argument registration call
immediately after each ordinary construction, preserving both literal key
arguments and the constructed receiver. The original holder/cell/callee/symbol
census remains required. The unchanged captured Map expander must consume every
moved helper before the candidate can pass; nested routing and concrete record
ownership still run afterward. Saved child and record aliases survive replacement
and recreation. The original file-44 constructor witness now executes unchanged.
**cf37fb24** admits Number-literal String indices beyond uint32, including
literal overflow infinities. Unsigned conversion is guarded before clamping;
fractional truncation and exact first-unit/dataset-tail authority remain intact.

Focused devbox validation passes: **one exact CTest and four distinct lit cases**.
The corrected captured class fixture measures **169 source observations, 360 main
native executions, 338 unprepared refusals and 219 preparation refusals**; the new
direct constructor's first complete budget is **3619**. Intrinsic exports measure
**648 native executions, 536 refusals and two mutations**, with **89 Node/VM
agreements and seven explicit known differences**. DOM Strings measure 809
observations and eight native binaries. All **ten final code/test hashes** match
the devbox. Scoped formatting and all 44 earlier class source files pass;
repository formatting retains 16 existing diagnostics in untouched files.
No browser/runtime implementation changed. Full suites, broad corpus/matrices,
full Bootstrap, WPT/test262, Windows and new sanitizers were skipped.

**Next:** compose three-argument registration with base-to-leaf completion and
the original Data holder, retaining every observer and exception/reentry path.
The current new slice requires an ordinary constructor and two literal String
keys. Original Data still needs element-identity keys, conflict reporting,
nullable lookup and conditional child/empty-parent cleanup. Original B/Data+B
remains refused with complete sources. Dynamic/coercing String indices, NaN
origins, broader String methods, conditional callees, branch-mutated boxed
locals, String ordering, document views and the application driver remain.
No full-Bootstrap admission or coverage gain is claimed.

[Exact checks and next proof seam](handoff/2026-09-21-constructor-data-wide-indices.md).

## Captured Data helpers and fractional String indices, 2026-09-21 UTC

Continued clean **5599188c** from the recorded captured Data helper boundary.
**e7ba01dc** expands exact three-argument captured Map helpers in their owning
entry before nested routing. Both literal key dimensions, conditional children,
called holder slots and saved aliases retain their existing owner proofs.
Recursive work is charged before cloning; branch-local captures, fixed-cell reads
and cached call results are resolved before their producers retire.
**72a99948** admits bounded fractional literal `charAt`/`slice` indices through
the existing UTF-16 path. Truncation precedes sign testing and unsigned conversion;
`-0.5` selects zero. Exact first-unit and dataset-tail authority stays unchanged.

Focused devbox validation passes: **one exact CTest and four distinct lit cases**
across corrected runs. Captured class checks measure **155 source observations,
328 main native executions, 310 unprepared refusals and 198 preparation refusals**;
the new direct helper's first complete budget is **1810**. Intrinsic exports measure
**504 native executions, 502 refusals and two mutations**, with **71 Node/VM
agreements and seven explicit known differences**. DOM Strings measure 809
observations and eight native binaries. All **nine final code/test hashes** match
the devbox; scoped checks and 43 original class source files pass. Repository
formatting retains 16 existing diagnostics in untouched files. No browser/runtime
implementation changed. Full suites, broad corpus/matrices, full Bootstrap,
WPT/test262, Windows and new sanitizers were skipped.

**Next:** constructor-time three-argument registration into the selected child
Map, preserving both key origins, all holder observers and constructor
exception/reentry order. Current publication normalization still expects a
one/two-argument helper and terminal direct `Map.set`. Original Data also needs
element-identity keys, conflict reporting, nullable lookup and conditional
child/empty-parent cleanup. Original B/Data+B remains refused with complete sources.
Dynamic/coercing String indices, broader methods, conditional callees,
branch-mutated boxed locals, String ordering, document views and the application
driver remain. No full-Bootstrap admission or coverage gain is claimed.

[Exact checks and next proof seam](handoff/2026-09-21-captured-data-fractional-indices.md).

## Short-circuit Maps and signed charAt indices, 2026-09-21 UTC

Continued clean **3e3ebde3** from the recorded Data short-circuit boundary.
**f5ac5a7a** preserves selected results of single-result, Map-only branches in
an exact owning entry. Nested `has || set` and `has && get` retain child identity;
cached results survive cleanup and replacement. Both arms and every outer mutation
pass the original use/effect census. An outer `set` result may travel only through
unused yields; observable outer-result transport remains refused.
**d8f532c1** admits signed literal `charAt` indices with uint32 magnitudes through
the existing UTF-16 path. Negative indices return an empty String; negative zero
selects index zero. Exact first-unit and dataset-tail authority is unchanged.

Focused devbox validation passes: **one exact CTest and four distinct lit cases**.
Captured class checks measure **136 source observations, 296 main native executions,
272 unprepared refusals and 174 preparation refusals**, with new budget **1704**.
Intrinsic exports measure **432 native executions, 466 refusals and two mutations**,
with **62 Node/VM agreements and six explicit known casing/indexing differences**.
DOM Strings measure 809 observations and eight native binaries. All **eight final
code/test hashes** match the devbox; scoped checks and 42 original class source
files pass. Repository formatting retains 16 existing diagnostics in untouched files.
No browser/runtime implementation changed. Full suites, broad corpus/matrices,
full Bootstrap, WPT/test262, Windows and new sanitizers were skipped.

**Next:** expand exact captured three-argument Data helper calls in their owning
entry before resolving nested Maps. Current helper expansion accepts straight-line
bodies, and the pass runs nested routing first. Constructor publication still needs
both key origins and the selected child owner, preserving all holder observers.
Original Data also needs element-identity keys, conflict reporting, nullable lookup
and cleanup with complete owner, exception and reentry proofs. Original B/Data+B
remains refused with its complete source intact. Dynamic/coercing String indices,
broader methods, conditional callees, branch-mutated boxed locals, String ordering,
document views and the application driver remain. No full-Bootstrap coverage gain
is claimed.

[Exact checks and next proof seam](handoff/2026-09-21-shortcircuit-maps-signed-charat.md).

## Conditional child Maps and signed String slices, 2026-09-21 UTC

Continued clean **9fd8337c** from the recorded conditional child-Map boundary.
**e7cff7c2** proves result-free, Map-only branches from known outer `has`, `size`
and `delete` observations. Selected child allocations move into their existing
owner frame; skipped allocations disappear. Both arms pass the original use/effect
census, and the unchanged concrete record-owner proof still checks every borrow.
**1f315db4** admits signed literal `slice` bounds with uint32 magnitudes through
the existing UTF-16 path. Clamping precedes unsigned subtraction; `charAt` and
exact dataset-tail/first-unit authority retain their previous restrictions.

Focused devbox validation passes: **one exact CTest and four distinct lit cases**.
Captured class checks measure **122 source observations, 272 main native executions,
244 unprepared refusals and 155 preparation refusals**, with new budget **994**.
Intrinsic exports measure **408 native executions, 430 refusals and two mutations**,
with **59 Node/VM agreements and five explicit known casing/indexing differences**.
DOM Strings measure 809 observations and eight native binaries. All **eight final
code/test hashes** match the devbox; scoped checks and 41 original class source
files pass. Repository formatting retains 16 existing diagnostics in untouched files.
No browser/runtime implementation changed. Full suites, broad corpus/matrices,
full Bootstrap, WPT/test262, Windows and new sanitizers were skipped.

**Next:** compose captured Data helper/constructor calls with conditional child
origins and result-carrying short-circuit control flow. Original
`Data.set(element, componentKey, this)` still needs both key dimensions, conflicts,
nullable lookup and cleanup, with complete owner, observer, exception and reentry
proofs. Original B/Data+B remains refused with its complete source intact.
Negative `charAt`, dynamic/coercing String indices, broader String methods,
conditional callees, branch-mutated boxed locals, String ordering, document views
and the application driver remain. No full-Bootstrap coverage gain is claimed.

[Exact checks and next proof seam](handoff/2026-09-21-conditional-maps-signed-slices.md).

## Finite nested Maps and String slice bounds, 2026-09-21 UTC

Continued clean **a9eb0f33** from the recorded Data boundary.
**8bbcf23b** resolves finite outer-Map routes to exact preallocated child Maps
before the unchanged record-owner proof. Literal String keys, complete outer
use/cell census and same-entry operations are required. Saved child/record
aliases retain their original owners across replacement and deletion.
**7f226c55** admits literal uint32 `slice(start, end)` through the existing
UTF-16 path. Dataset-tail and first-unit authority retain their original
one-argument `slice(1)` and `charAt(0)` proofs.

Focused devbox validation passes: **one exact CTest, four distinct lit cases and
one targeted UTF16/H class subset**. Captured class checks measure **106 source
observations, 248 main native executions, 212 unprepared refusals and 134
preparation refusals**, with budget **830** for the new direct witness.
Intrinsic exports measure **376 executions, 394 refusals and two mutations**,
with **55 Node/VM agreements and four explicit known differences**.
The class subset measures 100 observations, eight native executions and 466
refusals; DOM Strings also pass. All **11 final code/test hashes** match the
devbox. Scoped checks pass; repository formatting retains 16 existing diagnostics.
No browser/runtime implementation changed. Full suites, broad corpus/matrices,
full Bootstrap, WPT/test262, Windows and new sanitizers were skipped.

**Next:** conditional child-Map creation and original three-argument Data
registration using both key dimensions, conflicts, nullable lookup and cleanup
still need complete owner, observer, exception and reentry proofs. This slice
resolves fixed routes among existing children. Original B/Data+B remains refused
with its complete bodies intact. Broader String methods/indices, conditional
callees, branch-mutated boxed locals, String ordering, document views and the
application driver remain. No full-Bootstrap coverage gain is claimed.

[Exact checks and next proof seam](handoff/2026-09-21-nested-maps-slice-bounds.md).

## Captured keys and literal String indices, 2026-09-21 UTC

Continued clean **641c8ee5** from the recorded Data/key boundary.
**e7b45436** admits the unchanged file-30 captured outer-key witness. A constructor
may capture one local Map and one immutable String key; a wrapper parameter must
have the same literal String at every closed, visible call. Original callee,
cell, symbol, publication and record-owner checks remain required.
**2e6dc90a** admits literal uint32 `charAt`/`slice` indices through existing Core
UTF-16 conversion and checked bounds. Dataset reconstruction and isolated-unit
lowercase retain their original index-0/index-1 authority.

Focused devbox validation passes: **one exact CTest, four distinct lit cases and
one UTF-16 class subset**. Captured class checks measure **95 source observations,
224 main native executions, 190 unprepared refusals and 118 preparation refusals**;
the original witness plus four new bodies add 40 executions. Intrinsic exports
measure **352 native executions, 356 refusals and two mutations**, with **52 Node/VM
agreements and three explicit known casing/indexing differences**. The class
subset measures 92 observations, eight native binaries and 404 refusals; the
existing DOM String fixture also passes. All **11 code/test hashes** match the
devbox. Scoped formatting passes; repository formatting retains 16 existing
diagnostics. No browser/runtime implementation changed. Full suites, broad
corpus/matrices, full Bootstrap, WPT/test262, Windows and new sanitizers were skipped.

**Next:** original Data still needs its real three-argument registration using
both key dimensions, conditional nested Maps, conflicts, nullable lookup and
child/empty-parent cleanup, with complete owner, observer, exception and reentry
proofs. Original B/Data+B and unused holder slots remain unchanged refusals.
Different caller keys, computed keys, captured helper keys and broader families
remain separate. Broader String methods/indices, conditional callees, branch-mutated
boxed locals, String ordering, document views and the application driver remain.
No full-Bootstrap admission or coverage gain is claimed.

[Exact checks and next proof seam](handoff/2026-09-21-captured-keys-string-indices.md).

## Multi-slot holders and intrinsic Strings, 2026-09-21 UTC

Continued clean **c2dc4554** from the recorded Data holder boundary.
**95c290a2** expands called Map-holder siblings in their owning entry block
before the existing constructor registration proof. Complete fixed-cell, slot,
constructor-capture and initialization-order checks precede expansion;
publication and concrete record-owner checks remain unchanged. Four new bodies
cover registration, saved aliases, removal and a keyed inherited base.
**8e6e2e4d** admits already-proved ASCII-prefix `startsWith` and
`charAt(0).toLowerCase()` in intrinsic exports through existing String/Core paths.

Focused devbox validation passes: **one exact CTest and three distinct lit cases**
across corrected runs. Class checks measure **82 source observations, 184 main
native executions, 164 unprepared refusals and 97 preparation refusals**;
four new admissions add 32 executions. Intrinsic exports measure **328 native
executions, 314 refusals and two mutations**, with **49 Node/VM agreements and
two explicit known VM ASCII-casing differences**. Native/Node Unicode expectations
remain intact. All **seven final code/test hashes** match the devbox. Scoped
formatting passes; repository formatting retains 16 existing diagnostics.
Full suites, broad corpus/matrices, full Bootstrap, WPT/test262, Windows and
new sanitizers were skipped. No browser or runtime implementation changed.

**Next:** original Data's three-argument registration, conditional nested Maps,
conflict reporting, nullable lookup and child/empty-parent cleanup still need
complete owner, observer, exception and reentry proofs. Original B/Data+B,
the unused second holder slot and captured outer key remain unchanged refusals.
This slice handles called siblings in their owning entry block. Broader families,
method-frame operations, intrinsic String methods/indices, conditional callees,
branch-mutated boxed locals, String ordering, document views and the application
driver remain unfinished. No full-Bootstrap admission or coverage gain is claimed.

[Exact checks and next proof seam](handoff/2026-09-21-multi-slot-holders-intrinsic-strings.md).

## Keyed publication and description guards, 2026-09-21 UTC

Continued clean **210bbb2d** from its constructor-holder/Data boundary.
**346d69e0** proves keyed constructor registration through a pure direct or
single-slot holder helper. Every exact construction supplies its own literal
String key; base publication stays deferred until the sole leaf's substituted
arguments and complete owner/observer proof pass. Four new admissions retain
saved aliases across replacement and deletion.
**c6ea7670** narrows saved Symbol descriptions under exact String/undefined
guards. Explicit String identity permits `charAt(0)` and `slice(1)` through
existing UTF-16 operations and the correct owning carrier extraction.

Focused devbox validation passes: **one exact CTest and five distinct lit cases**
across corrected runs. Class checks measure **70 source observations, 152 main
native executions, 140 unprepared refusals and 81 preparation refusals**;
new admissions add 32 executions. Intrinsic exports measure **304 native
executions, 278 refusals and two mutations**, with **46 Node/VM observations**.
The affected DOM String fixture also passes. All **14 final code/test hashes**
match the devbox. Scoped formatting passes; the repository formatter retains
16 existing diagnostics. Full suites, broad corpus/matrices, full Bootstrap,
WPT/test262, Windows and new sanitizers were skipped. No browser or runtime implementation changed.

**Next:** compose Data's `set/get/remove` slots and three-argument registration
with conditional nested Maps, conflict handling, nullable lookup and cleanup,
proving every owner, observer, exception and reentry path. Original B/Data+B and
the captured outer-key witness remain unchanged refusals. Computed keys,
shared/deeper families, broader intrinsic String methods/indices, conditional
callees, branch-mutated boxed locals, String ordering, document views and the
application driver remain unfinished. No full-Bootstrap coverage gain is claimed.

[Exact checks and next proof seam](handoff/2026-09-21-keyed-publication-description-guards.md).

## Constructor holders and intrinsic captures, 2026-09-21 UTC

Continued clean **1cae9bf5** from its constructor-holder/Data boundary.
**6fcfbbbf** proves terminal constructor publication through one exact captured
holder slot. Complete holder, helper and cell-use checks run before normalization;
existing Map identity and concrete record-owner proofs remain unchanged.
**7fde70e4** admits exact local primitive/Symbol captures through the
existing immutable-cell and closure proof, including nested captures inside
closed global helpers. Generated values and runtime implementations are unchanged.

Focused devbox validation passes: **one exact CTest and four distinct lit cases**
across the selected runs. The holder fixture checks **55 source observations,
120 main native executions, 110 unprepared refusals and 62 preparation refusals**;
three new admissions add 24 executions. Intrinsic exports check
**280 native executions, 252 refusals and two mutations**, with **43 Node/VM observations**.
All **eight final code/test hashes** match the devbox. Scoped formatting passes;
required repository formatting retains 16 existing diagnostics. Full suites,
broad corpus/matrices, full Bootstrap, Windows and new sanitizers were skipped.
No browser or runtime implementation changed.

**Next:** compose Data's multiple holder slots and keyed base-constructor calls with
conditional outer/nested Map creation, conflict checks, nullable lookup and
child/empty-parent cleanup. Prove every concrete owner, observer, exception and
reentry path; preserve the complete original B/Data+B bodies, which still refuse.
The new holder slice requires one ordinary slot, a literal String registration key
and terminal publication by one constructor. Intrinsic conditional callees,
branch-mutated boxed locals, description String-method narrowing, String
ordering, document views and the application driver remain unfinished.
No full-Bootstrap admission or coverage gain is claimed.

[Exact checks and next proof seam](handoff/2026-09-21-constructor-holders-intrinsic-captures.md).

## Local Map holders and global helper declarations, 2026-09-21 UTC

Continued **ab3fe6c6**, recovering drafts through two process interruptions.
**ba1edab6** expands exact local holder methods that capture a Map at their
original owner-frame calls. The shared callable-object census proves every slot
and use; existing concrete record-owner checks remain unchanged.
The former direct-plus-holder helper refusal now executes with its source intact.
**7660166b** lets exact global intrinsic helpers contain local declarations by
using the existing complete closure proof when their receiver is unobserved.

Focused devbox validation passes: **one distinct exact CTest and four distinct
lit cases** across the corrected runs. The holder fixture checks **37 source
observations, 96 main native executions, 74 unprepared refusals and 43 preparation
refusals**; five new admissions add 40 executions. Intrinsic exports check
**232 executions, 221 refusals and two mutations**, with 37 Node/VM observations.
All **seven final code/test hashes** match the devbox. Scoped formatting passes;
required repository formatting retains 16 existing diagnostics. Full suites,
broad corpus/matrices, full Bootstrap, Windows and new sanitizers were skipped.
No browser or runtime implementation changed.

**Next:** carry exact holder identities into constructor-time Data calls, then
prove conditional nested Map creation, conflicts, nullable lookup and cleanup
against every concrete owner, observer, exception and reentry path.
Original Bootstrap B/Data+B still refuses; its bodies remain intact.
Holder captures, region calls and publication before receiver completion remain
outside this new entry-block proof. Captured intrinsic helpers, description
String-method narrowing, String ordering, document views and the application
driver remain unfinished. No full-Bootstrap admission or coverage gain is claimed.

[Exact checks and next proof seam](handoff/2026-09-21-map-holders-local-declarations.md).

## Captured Map helpers and global intrinsic helpers, 2026-09-21 UTC

Continued clean **fb5b8c04** from its captured/nested Data Map handoff.
**de86d5ba** expands exact local helpers capturing one Map at their original
entry-block calls. Registration, lookup, size and deletion then pass the existing
concrete record-owner proof; saved aliases keep their original owners.
**fdf79a61** admits exact uncaptured global helpers in intrinsic exports through
closed declaration checks, private expansion and complete typed reproof.

Focused devbox validation passes: **one distinct exact CTest and four distinct
lit cases** across corrected runs. Captured Map checks cover **23 source
observations and 56 main native executions**; four new cases add 32.
Intrinsic exports check **200 executions, 200 refusals and two mutations**,
with 33 Node/VM observations. All **11 final code/test hashes** match the devbox.
Scoped formatting passes; required repository formatting retains 16 existing
diagnostics. Full suites, broad corpus/matrices, new sanitizers and full Bootstrap
were skipped. No browser or runtime implementation changed.

**Next:** original B/Data+B still needs constructor-time calls through the Data
holder, conditional nested Map creation, conflict checks, nullable gets and cleanup,
with complete owner, observer, exception and reentry proofs. The new helper
normalization does not transport borrowed records across frames or nested Maps.
Preserve the original Bootstrap bodies. Captured intrinsic helpers and global
helpers containing local declarations remain separate, as do description
String-method narrowing, String ordering, document views and the application driver.
No full-Bootstrap admission or coverage gain is claimed.

[Exact checks and next proof seam](handoff/2026-09-21-captured-map-global-helpers.md).

## Numeric publication and scalar equality, 2026-09-21 UTC

Continued clean **0e96b1dd**, resuming the retained file-33 arithmetic witness.
**836ea47f** admits numeric leaf initialization after base Map publication:
every exact construction must supply literal Numbers for the used parameters,
and the complete tail must contain only proved numeric operations and own-field
writes. Existing family, observer and stack-owner restrictions remain.
**ac33706d** admits Number/Boolean strict and loose equality through the existing
typed intrinsic-entry proof and native operators/helpers.

Focused devbox validation passes: **two exact CTests and three distinct lit
cases** across corrected runs. The class gate checks **58 source observations
and 160 main native executions**; four new admissions add 32. Intrinsic exports
check **168 executions, 161 refusals and two mutations**, with 29 Node/VM
observations. All **eight final code/test hashes** match the devbox.
Scoped formatting passes; required repository formatting retains 16 existing
diagnostics. Full suites, broad corpus/matrices, new sanitizers and full Bootstrap
were skipped. No browser or runtime implementation changed.

**Next:** compose captured/nested Data Map origin tracking with concrete record
owner lifetime proofs for original B/Data+B, preserving registration, conflicts,
nullable gets and cleanup. File 34 retains nonnumber inputs, coercion/reentry,
field reads and helper results; shared/deeper families and String fields remain
separate. Global/captured intrinsic helpers and description String-method
narrowing remain. No full-Bootstrap admission or coverage gain is claimed.

[Exact checks and next proof seam](handoff/2026-09-21-numeric-publication-scalar-equality.md).

## Base publication and intrinsic helpers, 2026-09-21 UTC

Continued clean **d45c82d4**. **9e0c7f05** admits the unchanged file-30
inherited-publication source: one unconstructed base, one leaf and only literal
own-field writes after publication. Both constructor environments retire together;
existing Map identity, observer and stack-owner proofs remain required.
**990b752f** admits exact uncaptured local helpers in intrinsic exports using
charged private expansion and complete typed reproof, preserving argument order,
Symbol identities, descriptions and undefined padding.

Focused devbox validation passes: **two exact CTests and three distinct lit
cases** across corrected runs. The class gate checks **50 observations and
128 main native executions**; four newly admitted cases add 32. Intrinsic exports
check **152 executions, 141 refusals and two mutations**. All **13 code/test hashes**
match the devbox. Scoped formatting passes; required repository formatting retains
16 existing diagnostics. Full suites, broad corpus/matrices, new sanitizers and
full Bootstrap were skipped. No browser or runtime implementation changed.

**Next:** original B/Data+B captured outer/nested Data Map origins, helper
publication, conflicts, nullable gets, cleanup and owner lifetime. File 33 retains
nonliteral child work, shared/deeper base families and a prepared String-field
refusal. Global/captured intrinsic helpers, provider Number/Boolean equality and
description narrowing remain separate. String ordering, document views and the
application driver remain unfinished. No full-Bootstrap admission or coverage gain
is claimed.

[Exact checks and corrections](handoff/2026-09-21-base-publication-intrinsic-helpers.md).

## Constructor helper publication and intrinsic parameters, 2026-09-21 UTC

Continued clean **ef37639a**, preserving drafts through interruptions.
**7b2f93f8** admits the unchanged constructor-helper Map publication source:
complete helper/cell/symbol-use proof precedes normalization, then existing
terminal-registration and stack-owner checks apply. Hoisted captures remain
deferred until that proof discharges them; other early captures stay refused.
**a35e18ef** adds exact Boolean, Number, String and Symbol parameters to the
strict intrinsic-export contract through existing typed values.

Focused devbox validation passes: **two exact CTests and three distinct lit
cases** across corrected runs. The class gate checks **33 observations and
96 main native executions**; three newly admitted helper sources add 24.
Intrinsic exports check **120 executions, 107 refusals and two mutations**.
All **17 code/test hashes** match the devbox. Scoped formatting passes; required
repository formatting retains 16 existing diagnostics. Full suites, broad
corpus/matrices, new sanitizers and full Bootstrap were skipped. No browser or
runtime implementation changed.

**Next:** original base publication before leaf completion, beginning with the
unchanged file-30 `class-map-record-constructor-inherited.js`; prove partial
initialization, exceptions/reentry and lifetime. Original B/Data+B also requires
captured outer/nested Data Map origins, conflicts, nullable gets and cleanup.
Shared/effectful helper publication and intrinsic helper calls remain separate.
String ordering, document views and the application driver remain unfinished.
No full-Bootstrap admission or coverage gain is claimed.

[Exact checks and corrections](handoff/2026-09-21-helper-publication-intrinsic-parameters.md).

## Inherited leaf publication and Symbol descriptions, 2026-09-21 UTC

Continued clean **97e96751**. **744ebe85** proves an inherited leaf's terminal
Map registration after its complete super chain, reusing the existing stack-owner
and borrowed-record proof. Base publication followed by child work remains
refused. **be96b029** admits source Symbol `.description` with an owning
undefined/String result distinct from DOM null/String, including equality,
truthiness, branches, loops and returns.

Focused devbox validation passes: **two distinct CTests and six distinct lit
cases** across corrected runs. Four new leaf cases add **32 native executions**;
the class gate selects 21 source observations. Symbol exports check **72 native
executions, 84 refusals and two mutations**; DOM Symbols check **40 executions,
42 refusals and one mutation**. All **19 code/test hashes** match the devbox.
Scoped formatting passes; required repository formatting retains 16 existing
diagnostics. Full suites, broad corpus/matrices, new sanitizers and full Bootstrap
were skipped. No browser or runtime implementation changed.

**Next:** original helper and base-constructor publication, beginning with the
unchanged `class-map-record-constructor-helper.js` in class fixture file 30.
Prove complete Map origins/observers, partial initialization, exceptions/reentry
and owner lifetime before widening. Original B/Data+B also needs captured outer
and nested Data Maps, conflict checks, nullable gets and cleanup. Useful intrinsic
entry parameters/helpers are the next Symbol slice. Deeper sibling relays,
String ordering, document views and the application driver remain unfinished.
No full-Bootstrap gain is claimed.

[Exact checks and corrections](handoff/2026-09-21-leaf-publication-symbol-description.md).

## Symbol construction and sibling captures, 2026-09-21 UTC

Continued **aebfa918**, including the interrupted in-flight work.
**cb07719d** admits fresh Symbol construction with absent/String descriptions
and direct `toString`/`valueOf` calls through the existing fingerprinted DOM and
intrinsic entry proofs. Generated C++ uses the existing native factory and typed
methods. **46ae2476** admits the unchanged `nested-sibling.js`: bound sibling
calls forward identical existing captures only after both functions lift. A
failed attempt retains executable closure bindings.

Focused devbox validation passes: **two distinct CTests and seven distinct lit
cases** across corrected runs. Symbol exports check **48 native executions,
74 refusals and one mutation**; DOM Symbols check **32 executions, 36 refusals
and one mutation**. Sibling captures check **five observations in four native
executions**, four refusals, a mutation and execution of all four boxed entries
for a refused closure. All **16 code/test hashes** match the devbox. Scoped
formatting passes; required repository formatting retains 16 existing diagnostics.
Full suites, broad corpus/matrix, new sanitizers and full Bootstrap were skipped.
No browser or runtime implementation changed.
[Exact checks and corrections](handoff/2026-09-21-symbol-construction-sibling-captures.md).

**Next:** Symbol `.description` needs undefined/String transport distinct from
DOM null/String, then useful intrinsic parameters/helpers. Bootstrap's original
B/Data+B still needs constructor registration through helpers and inheritance,
followed by captured outer/nested Data Map origins, conflict checks, nullable
gets, cleanup and owner lifetime. Preserve its original bodies. Deeper sibling
relays, custom Symbol hooks, String ordering, document views and the application
driver remain unfinished. No full-Bootstrap gain is claimed.

## Symbol transport, intrinsic exports and class tests, 2026-09-21 UTC

Continued clean **2e505136**, resuming the retained Symbol transport witnesses.
**29039575** carries identities through branches, loops and returns.
**0c2a6657** keeps ordinary `js_symbol_t` values and optional temporary state,
with GCC 13's false-positive warning suppressed only around the Symbol class.
A real uninitialized caller variable still fails compilation.
**4400f0c4** adds the strict `ctbrowser-intrinsics-v1` contract for named
zero-argument Symbol/primitive exports without DOM inputs or Script dependencies.
**8091bb51** proves ordinary class `instanceof` from exact source constructors
and heritage before structural lowering. It preserves constructor effects;
unrelated equal shapes do not match. Custom hooks remain refused.

Focused devbox checks pass: **two distinct CTests and five distinct lit cases**
across the corrected runs. Symbol DOM transport checks **28 native executions,
38 refusals and one mutation**; primitive exports add **16 executions and 56
refusals**. Ordinary class tests add **six Node/VM cases, 48 native executions
and 26 refusals**. A generated Symbol loop passes GCC ASan/UBSan. All **27 final
code/test hashes** match the devbox; scoped formatting and Black pass. Required
repository formatting retains 16 existing diagnostics. Full suites and broad
corpus/matrix runs were skipped. No browser or VM implementation changed.
[Exact checks and next boundaries](handoff/2026-09-21-native-symbol-transport.md).

**Next:** prove fresh Symbol source construction and primitive methods, then
broaden intrinsic exports to useful parameters/helper calls. Custom
`Symbol.hasInstance` requires coherent VM lookup, reentry/escape and exception
work before native hook dispatch; the historical custom-hook oracle gap remains.
The type plan records a reentrant current-document accessor borrowing invocation
owners with scoped save/restore; it is not implemented. String ordering, sibling
captures, document views and full Bootstrap/application work remain unfinished.

## Proved Symbol reads in native DOM entries, 2026-09-21 UTC

Continued clean **a11c4878**. **083349ac** connects the native Symbol API to
source compilation through the existing fingerprinted DOM entry proof.
An explicit `initial_intrinsics: ["Symbol"]` contract admits direct reads of
all 15 well-known keys. Inference retains a distinct `!ctnative.symbol`; generated
C++ uses `ctnative::js_symbol_t` and properties such as `ctnative::Symbol.hasInstance`.
Local identity comparisons, truthiness and `typeof` can drive public DOM calls.
Mutation, construction, registry access, property-key use and hooks stay refused.

Focused devbox validation passes: **two exact CTests and four distinct lit cases**.
The new source fixture checks **21 attribute observations and two return paths**
against Node/VM, then real DOM execution in **eight native modes**, with
**38 refusals and one identity mutation**. All **17 code/test hashes** match the
devbox. Scoped formatting passes; required repository formatting retains 16
existing diagnostics. Full suites and broad corpus/matrix runs were skipped.
[Exact checks and corrections](handoff/2026-09-21-native-symbol-source.md).

**Next:** resume `join`, `loop` and `symbol-return` in
`Browser/native_dom_symbols.py`. Prove Symbol state/return transport without a
fabricated default or absent identity; `js_symbol_t` has no default constructor.
General Symbol-only scripts still need a contract independent of the DOM
provider's element inputs. Fresh construction, primitive methods, registry,
symbol-keyed fields and custom/default `instanceof` proofs remain separate.
The measured VM custom-hook gap remains unchanged. String ordering, sibling
captures, document views and full Bootstrap/application work also remain.
No browser, VM or runtime implementation changed.

## Native Symbol object and values, 2026-09-21 UTC

Continued clean **d2a75ef0**, following the user's Symbol priority.
**36bfb67e** extracts existing identity, description and formatting behavior into
public Core; VM creation, equality and explicit String methods are adapters.
**498a34bc** adds `js_symbol_t` and the native `Symbol` object, including
`Symbol.hasInstance` and 14 other immutable well-known keys. Fresh construction,
absent/empty descriptions, owning snapshots and `toString`/`valueOf` prototype
calls are implemented. Well-known values require no startup allocation; fresh
identities remain distinct across translation units and factory copies.

Focused devbox checks pass: **three distinct selected CTests**, the three-case
`script_gc symbol` filter, and **three distinct selected lit cases**. The new API
fixture compares **25 observations** with Node/VM under GCC and Clang, including
cross-translation-unit identity, standalone-header use, mutation and source
refusals. All **nine final code/test hashes** match the devbox. Scoped C++
formatting and whitespace pass; required repository formatting retains 16
existing diagnostics. Full suites and broad corpus/matrix runs were skipped.
[Exact checks and oracle probe](handoff/2026-09-21-native-symbols.md).

**Next:** prove direct well-known Symbol reads through the fingerprint-bound
initial-intrinsic contract, then add a distinct source Symbol carrier for
identity equality, truthiness and `typeof`. `Symbol.hasInstance` is a key;
constructor hook lookup and `instanceof` remain unimplemented in native code.
The current VM ignores custom hooks: a measured Node true/one-call witness is
false/zero-call in the VM. Registry, symbol-keyed fields and hook/default-prototype
proofs remain separate. The retained sibling-capture boundary, String ordering,
collections/document views, full Bootstrap and application driver also remain.

## Typed Number bitwise operations and return dispatch, 2026-09-21 UTC

Continued clean **0587aa5d**. **2526d714** extracts the VM's existing
ToInt32/ToUint32 into public Core, with VM and Math adapters retaining coercion
order. **827eadff** adds typed `js_num` bitwise operators and
`.unsigned_shift_right(...)`; **279436f1** lowers proved primitive operands to
those operations. Native output remains independent of Script.

**b2c99d93** finishes the previous handoff's ordinary return-dispatch boundary.
It reuses bounded normalization on a disposable clone, charges copying before
allocation, reruns inference/admission and restores the original body on refusal.
Unused String-union slots now receive their destination type. The preserved
`index-switch.js` executes unchanged. **0636ced2** promotes the unchanged
exception concatenation witness that earlier String arithmetic already admitted.

Focused devbox validation passes: **three selected CTests and seven distinct
selected lit cases** across corrected runs. Bitwise checks cover **76 observations
in eight native modes**; return dispatch covers **eight observations in six modes**,
mutation, budget and rollback controls. Existing primitive-cell and exception
regressions pass, including the latter's focused sanitizer checks. All **19 final
code/test hashes** match the devbox. Scoped C++/Python formatting and whitespace
pass; required repository formatting retains 16 existing diagnostics. Full suites,
broad corpus/matrix runs and full Bootstrap were not run.
[Exact checks and corrections](handoff/2026-09-21-native-bitwise-return-dispatch.md).

**Next:** the preserved `nested-sibling.js` captured-function binding refusal in
`Closures/primitive-cells.test`. The next primitive type work is relational
conversion and String ordering, preserving UTF-16 semantics and measured oracle
boundaries. Broader unions, containers, object hooks, collections/document views,
`Symbol.hasInstance`, NodeList indexing above 1,000,000 (separate from the 2^24
spread cap), full Bootstrap and the application driver remain unfinished.

## Primitive union shared cells, 2026-09-21 UTC

Continued clean **3a0fb32f**. **cdf2c0a5** carries the five admitted
String-containing primitive families through shared variables and nested
capture pointers. Stores reuse the existing type join and scalar conversions;
reads own their String copies. Initial undefined is omitted only under the
existing proof that a write precedes every read. Observable undefined and null
remain distinct. The original `shared-mixed.js` now executes unchanged.

Focused devbox checks pass: a seven-step explicit build and **four selected
lit cases**. The new fixture checks **44 observations in eight native modes**,
the original witness on GCC/Clang, five refusals and two mutations. All five
code/test hashes match the devbox; both changed C++ files pass scoped formatting.
Required repository formatting retains 16 existing diagnostics. Full suites and
separate CTest/sanitizer runs were skipped.
[Exact checks and source adaptations](handoff/2026-09-21-native-primitive-cells.md).

**Next:** normalize ordinary return dispatch, beginning with the preserved
`index-switch.js` refusal in `Closures/primitive-cells.test`. Reuse bounded
`normalizeStructuredExits` on a disposable function clone and publish only on
success. It already normalizes switch regions for specialized paths. The
`nested-sibling.js` captured-function binding refusal is separate. String
ordering, broader unions, containers, object hooks, Unicode alignment,
`Symbol.hasInstance`, indexed Bootstrap and the application driver remain.
No browser, VM or runtime implementation changed.

## Typed primitive equality, 2026-09-20 UTC

Continued clean **92c0628e**, resuming the three preserved equality refusals.
**0111e8e3** adds strict/loose equality over the nine typed primitive carriers;
**12f34e16** admits and emits it for String-containing pairs. The helpers visit
existing variants, borrow String storage and reuse scalar equality/public Core
parsing. Strict equality retains JavaScript kinds; loose equality compares two
Strings before coercion and keeps absence distinct from false, zero and empty
text. NaN and signed-zero behavior are preserved. Exact String pairs retain
C++ comparison; object hooks and wider unions remain refused.

Focused checks pass: runtime CTest **1/1** and **seven distinct selected lit
cases** across corrected runs. The new fixture checks **48 main observations
in eight native modes**, six further original-witness observations on GCC/Clang,
five refusals and two mutations. Existing compiled String-snapshot checks pass.
All **10 final code/test hashes** match the devbox; four scoped C++ format
checks and whitespace pass. Required repository formatting retains 16 existing
diagnostics. Full suites and separate sanitizer runs were skipped.
[Exact checks and corrected historical pins](handoff/2026-09-20-native-primitive-equality.md).

**Next:** finite String-containing unions in shared cells/captures, beginning
with unchanged `shared-mixed.js` in `Lowering/Scalars/strings.mlir`. Its equality
is representable; String/Number cell assignments still need proved widening.
Preserve owning snapshots, captured writes and absence on initial reads.
Ordering, broader unions, mixed containers, object hooks, Core parser gaps and
UTF-16 alignment remain separate. Collections/document views, `Symbol.hasInstance`,
indexed Bootstrap `R.find` and the application driver remain unfinished.
No browser or VM implementation changed in this slice.

## Boolean/String transport, 2026-09-20 UTC

Continued clean **435aa9b8**, resuming the preserved parameter/return refusals.
**9b234233** adds typed `boolean_string` and `nullable_boolean_string` carriers;
**0e1928ce** carries them through parameters, explicit returns, branches, loops
and global reads/writes. Strings remain owning typed values and optional storage
keeps undefined and null distinct. Numeric conversion, generic addition,
concatenation, truthiness and `typeof` reuse the existing primitives and public
Core. Map storage and exact per-operation proof requirements remain unchanged.

Focused checks pass: runtime CTest **1/1** and **eight distinct selected lit
cases** across corrected runs. The new source gate checks **50 main observations
in eight native modes**, two original-return observations on GCC/Clang, four
refusals and two mutations. All **19 final code/test hashes** match the devbox.
Scoped C++/Python formatting and whitespace pass; required repository formatting
retains 16 pre-existing C++ diagnostics. Full suites were skipped. The selected
Map case includes its existing focused ASan/UBSan and lifetime checks.
[Exact checks, initial build correction and updated refusal pins](handoff/2026-09-20-native-boolean-string.md).

**Next:** strict and loose equality for admitted String-containing primitive
unions, starting with the unchanged `equality.js` controls in
`boolean-string-transport.test` and `optional-number-string.test`. Preserve type
identity for strict equality and JavaScript conversion for loose equality,
including null/undefined, Boolean/Number/String, NaN and signed zero.
String ordering, broader unions, mixed containers, object hooks, Core parser
gaps and UTF-16 alignment remain separate. Collections/document views,
`Symbol.hasInstance`, indexed Bootstrap `R.find` and the application driver
remain unfinished. No browser or VM implementation changed in this slice.

## Optional Number/String transport, 2026-09-20 UTC

Continued clean **61b688c3**, resuming the preserved changing-global refusal.
**1ff01365** adds `nullable_number_string`, exactly
`std::variant<undefined_t, js_null_t, js_num, js_string>`. **7ac81d52** carries
those values through source global reads/writes, parameters, explicit returns,
branches, loops and primitive coercions. Global storage starts as undefined;
saved String values remain owning copies. Definite observations retain checked
extraction. The former optional-global program now runs unchanged in native C++.

Focused checks pass: runtime CTest **1/1** twice and **six distinct lit cases**.
The new fixture checks **53 Node/VM/native observations** across eight native
modes, four refusals and two mutations. All **14** final code/test hashes match
the devbox. Scoped formatting and whitespace pass; global formatting retains
16 pre-existing diagnostics. Full suites were skipped.
[Exact checks and the corrected initial build](handoff/2026-09-20-native-optional-number-string.md).

**Next:** closed Boolean/String parameters, returns and globals, starting with
`boolean-string-parameter.js` and `boolean-string-return.js` in
`generic-addition.test`. Keep typed String storage and distinct absence tags.
Wider unions, mixed containers, equality/ordering, object hooks, Core parser gaps
and UTF-16 alignment remain separate. Collections/document views,
`Symbol.hasInstance`, indexed Bootstrap `R.find` and the application driver remain
unfinished. No browser or VM implementation changed in this slice.

## Generic primitive addition, 2026-09-20 UTC

Continued clean **a7abf61b**, resuming its documented generic-`+` boundary.
**bede47ad** adds `ctnative::number_string`, exactly
`std::variant<js_num, js_string>`, and tag-dependent primitive addition.
**b9aa1580** infers and lowers that result through parameters, returns,
conditional/loop edges and global stores. Later numeric conversion, truthiness,
`typeof` and concatenation preserve JavaScript behavior. Public Core and the
existing Number/String operations supply conversion and text joining; no browser,
Script, VM or GC implementation changed.

Focused validation passes: **two CTests** and **eight distinct lit cases** across
corrected runs. The new source fixture checks **48 Node/VM/native observations**,
eight native modes, nine refusals and two mutations. All **18** code/test hashes
match the devbox. Scoped formatting and whitespace pass; required global
formatting retains the same 16 pre-existing diagnostics. Full suites were
skipped. [Exact checks and retained source controls](handoff/2026-09-20-native-generic-addition.md).

**Next:** optional Number/String source transport, starting with the complete
changing-global read/copy/reassign refusal in `generic-addition.test`. Preserve
null and undefined separately. `std::optional<number_string>` currently guards
uninitialized global storage only; it does not represent source absence.
Boolean/String signatures, wider unions, equality/ordering, object hooks, Core
parser gaps and UTF-16 alignment remain separate. Collections/document views,
`Symbol.hasInstance`, indexed Bootstrap `R.find` and the application driver
remain unfinished.

## Optional and union primitive coercions, 2026-09-20 UTC

Continued clean **c83df6b8**. **d29f1bf2** adds `nullable_string.to_number()` and
Boolean/String text conversion over the existing finite carriers. **1e87b5ad**
admits optional String unary `+`/`-`, numeric `-`, `*`, `/`, `%`, `**`, and exact
String concatenation with local Boolean/String unions. **5c556051** also admits
numeric arithmetic on those local unions. `std::visit` selects the existing
Boolean conversion or public Core String parser. Numeric conversion maps null
to positive zero and undefined to NaN; String `"false"` differs from Boolean false.

Focused checks pass: runtime CTest **1/1** for each implementation step, **eight
distinct lit cases**, and GCC compilation of the runtime checks. The final new
fixture checks **51 Node/VM/native observations** across eight native modes,
four refusals and two mutations. All **nine** final code/test hashes match the
devbox. Scoped formatting passes; required global formatting retains the same
16 pre-existing diagnostics. Full suites were skipped. [Exact checks and retained controls](handoff/2026-09-20-native-string-coercions.md#optional-and-union-primitive-coercions-2026-09-20-utc).

**Next:** generic optional/union `+` needs a proved closed String/Number result
carrier and a choice between numeric addition and concatenation based on the
actual alternatives. It stays refused until that boundary is implemented.
Boolean/String parameter/return/global support, String ordering, loose equality,
object hooks, shared Core parser gaps and UTF-16 alignment remain separate.
Collections/document views, `Symbol.hasInstance`, indexed Bootstrap `R.find`
and the application driver remain unfinished.

## String arithmetic and C++ primitive operands, 2026-09-20 UTC

**9c23d2e6** adds constrained `js_string` concatenation with built-in arithmetic
and `js_boolean_t` operands in either order. Boolean operands spell `true`/`false`;
raw numbers enter the binary64 Number domain, with extended floating-point
overflow checked before narrowing. Pointers, enums and merely convertible
classes are excluded. `nullable_scalar.to_string()` preserves all four tags.
**dd5021d5** enables exact String numeric `-`, `*`, `/`, `%`, `**` and concatenation
with Boolean/finite nullable scalars through the existing Number/Core operations.

Focused validation passes: runtime CTest **1/1**, **eight distinct lit cases**,
GCC compilation of the runtime checks and a standalone String header probe.
The new source fixture checks **26 observations**, eight native modes, four
refusals and one mutation; the existing 18-observation coercion fixture also
passes. All **11** code/test hashes match the devbox. Global formatting retains
16 pre-existing diagnostics; scoped formatting and whitespace pass. Full suites
were skipped. [Exact checks and fixture corrections](handoff/2026-09-20-native-string-coercions.md#extended-arithmetic-and-primitive-operands-2026-09-20-utc).

**Next:** optional String numeric conversion and closed Boolean/String union
concatenation, preserving absence tags and finite-alternative proofs. String
ordering, loose equality, object conversion hooks, shared Core parser gaps and
UTF-16 alignment remain separate. Collections/document views, the planned
`Symbol.hasInstance` wrapper and indexed Bootstrap `R.find` remain unfinished.

## Primitive String coercions, 2026-09-20 UTC

Continued clean **2fe9a029**. **d1f98f2a** fixes public Core Number formatting's
range check before its integer cast. **ff023451** adds `js_string.to_number()`
and typed String/Number addition overloads; **0d5cdec0** admits exact String unary
`+`/`-` and String/Number `+` in either order, emitting those class operations.
The VM and native classes share public Core conversion/formatting. Object hooks,
optional/union String numeric conversion, loose equality and ordering retain
their existing refusals.

Focused checks pass: **three CTests**, **five distinct lit cases**, including
18 distinct Node/VM/native observations across eight native compiler/printing/
optimization modes, six refusal controls and one `baNaNa` mutation. The former
Core cast fails a sanitizer control at `1e20`; the corrected formatter passes
six extreme roundtrips. All **13** code/test hashes match the devbox. Scoped
formatting/syntax passes; global formatting retains 16 pre-existing diagnostics.
Full suites and a complete wtfjs replay were skipped.
[Exact changes, checks and initial link failure](handoff/2026-09-20-native-string-coercions.md).

**Next:** proved String numeric arithmetic (`-`, `*`, `/`, `%`, `**`), then
remaining Boolean/absence-to-String pairs. Keep relational conversion, String
ordering and loose equality distinct. Shared Core parser gaps and general UTF-16
alignment require separate fixes and comparisons. Raw Number alias retirement,
collections/document views and the planned `Symbol.hasInstance` wrapper remain.
Indexed Bootstrap `R.find` still needs NodeList >1,000,000 undefined slots resolved,
with the independent 2^24 spread cap preserved. Full Bootstrap and the application
driver remain unfinished.

## String value carrier, 2026-09-20 UTC

Continued clean **2bc6cc6d**. **8cc0cf6e** introduces owning
`js_basic_string<char>` / `js_string`, exact const methods and String exceptions.
**ac3cdaa2** carries String literals, concatenation, calls/captures and fields in
that class; generated prefix calls use `.startsWith(...)`. Map/vector/nullable/
JSON storage and public browser operations retain explicit raw text adapters.
**98786531** updates owning C++ clients and restores pinned formatting.
No source admission or browser/VM behavior changed.

Measured focused checks: runtime CTest **1/1**, **17 distinct lit cases**, and
**eight admitted ownership cases** passed across corrected runs. Six selected
class cases passed **24 Node/interpreter observations, eight native executions
and 118 refusals**. A dynamic nested Map control remains refused, with two source
observations and six distinguishing mutations. All **43** code/test hashes match
the devbox; scoped formatting/syntax passes. Global formatting retains 16
pre-existing diagnostics. Full suites and wtfjs replay were skipped.
[Exact changes, checks and intermediate failures](handoff/2026-09-20-native-string-carrier.md).

**Next:** proved primitive String numeric conversion and mixed String/Number
addition, beginning with runtime `baNaNa` and negative controls. General String
length still uses bytes (ND-1); existing admitted DOM UTF-16/casing paths retain
their separately recorded VM differences. Resolve that alignment separately.
Raw Number alias retirement, collections/document views and the planned
`Symbol.hasInstance` wrapper remain. Indexed Bootstrap `R.find` still needs the
NodeList >1,000,000 undefined-slot boundary resolved, preserving the independent
2^24 spread cap. Full Bootstrap and the application driver remain unfinished.

## Number value carrier and semantic edge-case plan, 2026-09-20 UTC

Continued clean **5d084ecf**. **dbc82d9a** preserves typed Number values through
const qualification, deduction and exceptions. **25472dd7** migrates Number
literals, arithmetic, comparisons, fields, calls, captures and browser counts to
`ctnative::js_num`. Map/vector/JSON storage and public Core/C formatting keep
explicit binary64 adapters; Map SameValueZero is unchanged. **ae8124d6** updates
15 further signature/shape checks and retains their intended runtime proof paths.
No source admission or browser/VM behavior changed.

Measured focused validation: runtime CTest **1/1**, **36 distinct selected lit
cases** and **8 selected ownership cases** passed across corrected runs. All
**62** final code/test hashes matched the devbox. Scoped formatting/syntax passes;
required global formatting retains the same 16 pre-existing diagnostics. Full
CTest/lit, broad corpus/matrix and WPT/test262 were skipped.
[Exact changes, checks and intermediate failures](handoff/2026-09-20-native-number-carrier.md).

The user's wtfjs reference now has a [semantic edge-case plan](plans/native-js-semantics.md),
pinned to its README revision. It assigns missing coercion/equality, array,
prototype, evaluation-order, parser and host work to the correct layer. No wtfjs
corpus has been executed or claimed supported.

**Next:** implement `js_basic_string<char>` / `js_string` with an admitted String
operation group and primitive-coercion witnesses. Retire the raw global
`js_num = double` alias only with its remaining printer/storage clients. Preserve
the scheduled `Symbol.hasInstance` / equivalent `std::holds_alternative` wrapper.
Before indexed Bootstrap `R.find`, resolve NodeList slots above 1,000,000 returning
undefined in Shell; retain the separate 2^24 spread cap. Full Bootstrap and the
application driver remain unfinished.

## Typed Number conversions and NaN, 2026-09-20 UTC

**5c2f4332** implements `js_nan_t` and makes Boolean/nullable numeric conversions
return `ctnative::js_num`. Undefined converts to a Number NaN; null/false to
positive zero; numeric payloads retain signed zero. Lowering explicitly extracts
`.value()` for existing f64 arithmetic/storage. Exact Number extraction remains
pure for dead-code pruning and Map snapshot fusion; unknown members remain
barriers. **30e9e3c7** prints native binary64 NaN literals through the token.

Focused checks pass: runtime CTest **1/1**, **10 distinct lit cases** and
**4 selected ownership cases**, including existing differential, mutation and
sanitizer checks. The new NaN test's missing type-pin macro was fixed before its
successful rerun. All **13** code/test hashes matched the devbox. Scoped formatting
passes; global formatting retains the same 16 pre-existing diagnostics. Full
suites were skipped. [Exact changes and validation](handoff/2026-09-20-native-number-coercions.md).

**Next:** migrate the Number value carrier in `LoweringSupport.cpp::carrierType`,
remaining literals/arithmetic/math and call/capture signatures together, with
explicit Map/vector/JSON storage adapters. Then introduce `js_basic_string<char>`
and `js_string` using public Core string algorithms. Preserve the existing
`instanceof`/`Symbol.hasInstance` schedule and NodeList >1,000,000 Bootstrap boundary.

## Basic Number class and default type aliases, 2026-09-20 UTC

**21b2dee2** names the planned templates `ctnative::js_basic_num<T>` and
`ctnative::js_basic_string<T>`, with default aliases `js_num` for `double` and
`js_string` for `char`. **bb265540** implements the Number class and uses it for
numeric global observations, extracting `.value()` before C formatting.
Existing arithmetic, Maps and capture signatures remain binary64; generated
declarations inside `ctnative` qualify their compatibility alias as `::js_num`.

Focused validation passed: runtime CTest **1/1**, **9 selected lit cases** and
**8 selected ownership cases**, including existing differential, mutation and
sanitizer checks. All **20** code/test hashes matched the devbox. Scoped formatting
passes; global formatting retains the same 16 pre-existing diagnostics. Full
suites were skipped. [Implementation and exact checks](handoff/2026-09-20-native-number.md).

**Next:** Number literals, conversions, arithmetic and signature adoption, then
String and collections/document views. Add the user's planned `js_nan_t` explicit
Number-NaN construction token with numeric literals/conversions; it is not yet
implemented and must preserve Number semantics and distinguish absence. Keep the
typed `instanceof`/`Symbol.hasInstance` schedule with class/prototype proofs.
NodeList slots above 1,000,000 remain the next indexed Bootstrap boundary.

## JavaScript Boolean carrier migration, 2026-09-20 UTC

**92024490** continued **30a74866** with `ctnative::js_boolean_t` across native values,
signatures, comparisons, Maps, captures and browser result adapters. C++ control
conditions and public JSON storage retain `bool`. No VM or source-proof change.
Focused validation: runtime CTest **1/1**, **17 distinct lit cases** and **11
selected ownership cases** passed across corrected runs, including their existing
sanitizer checks. All 35 code/test hashes matched the devbox. Global formatting
retains 16 pre-existing diagnostics; scoped checks pass. Full suites were skipped.
[Implementation, exact focused validation and remaining boundaries](handoff/2026-09-20-native-boolean.md).

Next migrate Number/String carriers, then collections and document views. The
user's typed `instanceof` wrapper is scheduled with class/prototype proofs:
invoke a proved `Symbol.hasInstance` hook, otherwise use `std::holds_alternative`
where equivalent. Preserve receiver, inheritance, effects and exceptions.
NodeList slots above 1,000,000 remain the next indexed Bootstrap boundary.

## Typed JavaScript interface implementation, 2026-09-20 UTC

Resumed the first implementation milestone from **8ab0394c** and the design
below; the starting tree was clean and no interrupted branch needed landing.
**1576f18f** composes the existing four selector method objects under
`ctnative::Element.prototype`. Generated calls now use, for example,
`ctnative::Element.prototype.querySelector.call(element, styles, selector)`.
The old flat names reference those same immutable objects. Public Style behavior,
explicit borrowed inputs and all JavaScript identity/mutation proofs remain in
place. No dynamic lookup, allocation or virtual dispatch was added.

**bd727da7** introduces distinct `ctnative::undefined_t` and
`ctnative::js_null_t` construction tokens. The shared `absentConstant` emitter
constructs `nullable_scalar` from them for literals, initial scalar storage,
Map.clear results and used void DOM results. Default construction and `.null()`
remain compatible. Tags, payload, calls/returns and optional joins retain the
existing carrier; both absence literals still infer `Opt<Bottom>`. This starts
the primitive migration; exact standalone token signatures are not implemented.

Selector validation passed: exact `ctcompile_native_runtime` **1/1** (0.02s
CTest total), and closest/query/query-all/prototype-query/spread-length lit
**5/5** (103.22s). All seven changed code/test hashes matched the devbox before
**1576f18f**. The selector tests compile/run against public DOM/Core/Style and
check generated source and linked symbols for Script/AOT dependencies; they
are not VM/browser end-to-end differential measurements.

Absence-type validation passed: explicit targets built, runtime CTest **1/1**
(0.02s total), both optional-scalars/scalar-unions differential fixture lit
cases, and native-dom action lit. The fixtures compare generated observations
with the interpreter, compile both printing layouts under GCC/Clang, audit
symbols and exercise negative mutation/type-pin controls. Two raw lowering
checks initially failed because default precomputation erased their intended
unions (**3 passes / 2 failures**, 245.62s for that selected run). Requesting
`optimize=false` preserves every JavaScript body and refusal expectation;
the corrected two-case raw rerun passed **2/2** (0.22s), with no rebuild needed.
All **11** final changed code/test SHA-256 hashes match the devbox. Parallel
review found no additional issue in the constructors, callers or refusal paths.

Exact validation commands, local build then devbox test commands, all serialized
under `/tmp/ctbrowser-devbox-build.lock`:

```sh
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-runtime
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_native_runtime$'
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-(closest|query|query-all|prototype-query|spread-length)[.]test$'
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-runtime ctcompile-test-native-reference ctcompile-native-pipeline-optional_scalars ctcompile-native-pipeline-scalar_unions
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_native_runtime$'
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/((Lowering/Scalars/(optional-scalars|scalar-unions)[.]mlir)|(Fixtures/Scalars/(optional-scalars|scalar-unions)[.]test)|(Browser/native-dom[.]test))$'
tools/remote-build.sh ctjs-opt ctjs-translate
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/Lowering/Scalars/(optional-scalars|scalar-unions)[.]mlir$'
```

Changed C++ formatting, Python Black/syntax and whitespace checks pass.
Required `tools/format.sh --check` reports the same **16** pre-existing errors
in four untouched files: `ctdrive.cpp`, `ProviderPaths.h`, `Heap.h` and
`Symbolic/Facts.cpp`. No full CTest/lit, broad corpus/matrix, WPT/test262 or
sanitizer run was requested or performed. Browser code and the VM oracle are
unchanged. Process checks found no Claude identity in 70 Linux / 354 Windows
processes, but 57 Linux executable paths were unreadable; status was treated as
uncertain and work stayed in ctcompile and the requested external plan.

**Next type boundary:** introduce `js_boolean_t` while separating JavaScript
Boolean values from C++ `bool` control-flow conditions. The current carrier,
SCF i1, browser results and callable/shape spellings share `bool`; migrate
Boolean literals, comparisons, signatures, optional conversions and printing
together. Then migrate Number/String and collection/document views. The global
`using js_num = double` still needs a deliberate qualified-template migration.

**Next browser boundary:** reconcile native NodeList indexed slots above
1,000,000 with Shell returning undefined before admitting indexed `R.find`
consumers. Keep the separate 2^24 spread cap and current count-only integration.
Default document roots, Object/Array prototypes, BigInt/Symbol, full Bootstrap
and the application driver remain unfinished.

## Typed JavaScript interface plan, 2026-09-20 UTC

The user requested purpose-built native C++ types with JavaScript methods,
operators and `Element.prototype.*`, `Object.prototype.*`, `Array.prototype.*`
syntax. [The maintained design](plans/native-js-types.md) records the exact
vocabulary, primitive versus identity semantics, null/undefined distinction,
String encoding rules, operator limitations, typed prototype objects and
borrowed document/element views. Standard containers remain internal storage;
all browser behavior uses ctbrowser's public subsystems. No universal runtime
value, collector or reference-counted object graph is introduced.

External master-plan parts **00, 01, 24 and 25** now point to this design.
Part 24's future-facing type table, array methods, union semantics and prototype
rules reflect it; part 25 retains owner/alias/effect proof requirements. Dated
measurements and historical implementation examples are preserved. Native DOM
documentation distinguishes the current `.call` interface from the planned one.
These are documentation changes only: no runtime class, emitter behavior,
source admission or measured native coverage changed.

**Next implementation:** compose `Element.prototype` from the existing four
selector method objects and migrate the exact proven EmitC callees/type checks,
retaining explicit Style input initially. Then introduce the qualified
`ctnative::js_num<double>` and other primitive classes in coherent batches,
with their literals, signatures, conversions, optional joins and type pins.
The current global `using js_num = double` cannot be renamed to a template
blindly. Collections/closed shapes and document views follow; BigInt needs
public-core extraction and Symbol needs a separate identity/registry proof.

Carry forward the existing NodeList indexing defect: above 1,000,000 the VM
returns undefined, while direct native indexing currently supplies an element.
Resolve that before adding indexed `R.find` consumers; retain the separate
2^24 spread cap. Default document roots, full Bootstrap and the application
driver remain unfinished. Do not change the runtime oracle to match new wrappers.

Validation passed: 14 requested type/prototype names, seven document fence
balances, seven design links, six master-plan consistency assertions and
whitespace; parallel read-only review found no blocking issues. The required
formatter reports 16 pre-existing diagnostics in four untouched files. Build,
CTest, lit, differential, corpus/matrix, WPT/test262 and sanitizer checks were
skipped for this documentation-only change; no full-suite pass is claimed. The process check
found no Windows Claude identity among 365 processes; Linux executable reads
were denied for 57 processes, so availability was treated as uncertain. Work
stayed in compiler docs and the explicitly requested external plan; no browser
or shared-code edits, no broader authorization used, and no push.

## Bootstrap R.find count integration, 2026-09-20 UTC

Resumed the explicit-element `R.find` boundary recorded below and in the master
plan; the starting tree at **987dafbe** was clean. Linux `/proc` and Windows
`Get-CimInstance Win32_Process` checks found no Claude executable, CLI or loop
(65 Linux / 362 Windows processes, no unknowns); this was journaled before work.
Parallel inspection traced the Shell/VM limits; after delegated agents hit their
rate limit, Codex completed the implementation and native tests under its claims.

**323fea4c** proves the exact imported NodeList spread into an empty concat
receiver when the result has only `.length` observations. It retains the public
Style query and emits a scalar minimum with **16,777,216**, the VM's proxy-spread
cap. Original Array concat/species and absent receiver/element spreadability hooks
are explicit embedding guarantees. Every source use, loop state and copied slot
is checked; mutation, extra arguments, result indexing/identity/escape, stale
fingerprints and exhausted budgets refuse without publishing partial changes.
**a002f180** extends existing element guards to strict equality with `undefined`
in either operand order. This removes the original helper's unused
`document.documentElement` default only for an explicit validated element.
Unproved cells, joins and nullable query results receive no new element facts.

**454cfc3e** runs normalization after helper/default-guard expansion.
Its source-pinned fixture calls the unchanged Bootstrap `find` arrow from a local
holder and observes its count across DOM mutations. Direct spread tests also
exercise scope/root/shadow boundaries, detached roots, invalid-selector effect
order, and borrowed/owned document validation. Generated C++ uses existing
`ctnative::querySelectorAll.call` and links DOM/Core/Style; no VM, GC or new
native runtime carrier was added. Browser files are unchanged. Default document
roots and indexed concat consumers remain refused.

Final focused validation passed: host CTest **1/1** (0.55s), both existing
selector lit cases, and corrected spread-length lit **1/1** (**75.54s**). The new
case reports **32 native executions / 52 expected refusals** across borrowed and
owned providers, GCC/Clang, both optimization modes and both printing layouts.
Generated source and linked binaries pass Script/AOT symbol audits. All **11**
final code/test source hashes match the devbox; the final synchronization needed
no compilation. A whitespace-only fixture cleanup after its initial sync did
not change JavaScript tokens or test behavior.

Commands ran under `/tmp/ctbrowser-devbox-build.lock`, with helper and SSH stdin
from `/dev/null`. From the local checkout and then the devbox project directory:

```sh
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-host-contract
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_host_contract$'
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-(spread-length|prototype-query|query-all)[.]test$'
# Test-only iterations synchronized with these two explicit targets:
tools/remote-build.sh ctjs-opt ctjs-translate
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-spread-length[.]test$'
```

The first build caught ambiguous MLIR dominance overloads; explicit operation
pointers fixed them. The next host check caught a NumberAttr bit-pattern error;
using the encoded double fixed the cap before the first commit. Initial source
fixtures exposed existing same-block helper restrictions, then the missing
parameter guard above. The final production build passed host CTest **1/1**
(**0.55s**) and both existing selector lit cases; the new case then exposed only
a duplicate local name in the owned test client (three-case run **81.76s**).
The corrected client scopes its owned checks independently. Earlier the core
normalizer also passed host CTest **1/1** (0.53s) and both existing selector cases
(three-case run 79.03s, new fixture failed before native execution).

Required `tools/format.sh --check` retains **16** pre-existing diagnostics in
four untouched files: `ctdrive.cpp`, `ProviderPaths.h`, `Heap.h`, and
`Symbolic/Facts.cpp`. Changed C++/Python formatting and whitespace checks pass.
These are standalone native API checks and symbol audits, not VM/browser
end-to-end differential observations. Full CTest/compiler lit, broad corpus/matrix,
WPT/test262 and sanitizers were skipped. No push.

**Exact next boundary:** reconcile direct query-all indexed reads with Shell's
`collections.cpp:584` limit: indices above **1,000,000** return `undefined` in
the oracle, while the existing native indexed path returns an element. Before
admitting indexed `R.find` consumers, prove a distinct element-or-undefined slot
and guard its dereference; preserve the separate **2^24** spread cap from
`Script/vm/call/invoke.cpp:363`. Current count-only lowering does not observe
slot values and retains their contribution to length. Large collections were
not allocated in these tests; the cap and branch direction are checked in raw
IR proof tests. Default document roots need a separate document/session identity,
nullable-root and Style contract. Full Bootstrap initialization, retained events
and the application driver remain unfinished.

## String-to-number lookup cleanup, 2026-09-20 UTC

**38e51ca6** replaces `classIntrinsicArity` and `iteratorIntrinsicArity` branches
with `llvm::StringMap<unsigned>` lookups. **313fa8ef** converts six Map-method
arity expressions in native analysis, host preparation and closure lifting;
**cd029817** converts the leaf-object test call-count selector to `dict.get`.
The scan found **nine** matching sites across **eight** files. Each table retains
its original accepted names, fallback (0, 1, 7 or 99) and bounded string work.
No shared policy API, browser/oracle changes or generated LLVM dependency.

The explicit devbox build and **4/4** selected CTests passed (281.97s). Four
selected Map/object lit cases passed; the fifth, class initialization, stopped
at a stale preparation expectation (selected lit run 455.65s).
Earlier **622f1ca8** already allowed preparing the read-only helper in
`captured-holder-receiver-escape`, while native method forwarding still refuses.
**dea36b1f** moves that control to `PREPARED_ONLY`, preserves its construction
count and checks the exact `this`-argument native refusal in both modes. Source
bytes and Node/interpreter observations remain unchanged.

After correction, **12/12** selected existing class controls passed (30.36s):
12 source observations, 32 native executions, 24 unprepared refusals, 12
preparation refusals and four prepared native refusals. The same driver also
passed its constructed-method checks (16 executions/20 refusals) and original
`r` guards (eight executions/four refusals). The entire class aggregate was
**not replayed**. All **ten** final code/test source hashes match the devbox.
The Python metadata change also passed a before/after comparison of source,
refusal, call/function/field metadata and string literals.

Commands below ran under `/tmp/ctbrowser-devbox-build.lock`; local helper and
non-heredoc SSH commands used stdin from `/dev/null`:

```sh
# Local helper, initial build:
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-host-contract ctcompile-test-host-contract-seeded-maps ctcompile-test-exception-recovery ctcompile-test-owned-global-shared-map ctcompile-test-native-reference
# On devbox, from projects/compile-time-browser:
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_(host_contract|host_contract_seeded_maps|exception_recovery|owned_global_shared_map)$'
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/Lowering/(Maps/(maps[.]mlir|map-proof[.]mlir|map-iterators[.]test)|Objects/(object-fields|class-initialization)[.]mlir)$'
# Local helper after the test-only correction (no compilation needed):
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-reference
```

The final focused driver invocation ran on the devbox against the split source
fixtures left by that lit case. Only its observation selection changed:

```python
import sys
import time
from pathlib import Path
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path('ctcompile/test').resolve()))
from CTNative.Lowering.Objects import class_initialization as test
names = (
    'captured-holder-receiver-escape', 'captured-holder-unused',
    'captured-holder-receiver', 'inherited-own-fields-iterate-borrow-store',
    'inherited-own-fields-iterate-borrow-return',
    'inherited-own-fields-iterate-borrow-write', 'class-map-direct',
    'class-map-helper-holder', 'class-map-record-direct', 'class-map-record-clear',
    'class-map-member-replaced', 'class-map-stored-receiver',
)
test.OBSERVATIONS = {name: test.OBSERVATIONS[name] for name in names}
sys.argv = [test.__file__,
    '--translate', 'build/ctcompile/tools/ctjs-translate/ctjs-translate',
    '--opt', 'build/ctcompile/tools/ctjs-opt/ctjs-opt',
    '--node', '/home/ubuntu/tools/node-v26.8.1/bin/node',
    '--reference', 'build/ctcompile/test/ctcompile-test-native-reference',
    '--fixtures', 'build/ctcompile/test/CTNative/Lowering/Objects/Output/class-initialization.mlir.tmp',
    '--work', 'build/ctcompile/test/CTNative/Lowering/Objects/Output/class-initialization.arity-focused',
]
print('Selected class controls: ' + ', '.join(names), flush=True)
start = time.monotonic()
test.main()
print(f'Focused class controls passed in {time.monotonic() - start:.2f}s', flush=True)
```

Required formatting still reports **16** pre-existing diagnostics in the same
four untouched files listed below. Changed C++/Python formatting and whitespace
checks pass. Full CTest/compiler lit, broad corpus/matrix, WPT/test262 and
sanitizers were skipped; no full-suite pass is claimed. No push.
**Next native integration boundary remains:** confined Bootstrap `R.find`
spread/concat with an explicit element, followed by separately proved document
roots and ownership. Full Bootstrap initialization and the application driver
remain unfinished.

## Literal membership cleanup, 2026-09-20 UTC

Converted **27** same-subject literal equality chains of four or more comparisons
across **26** files: 16 compiler string checks, nine test-case checks and two
runtime/reference character checks. Compiler names use bounded `StringSet::contains`;
integer cases use `DenseSet`, the LLVM-independent inventory uses `unordered_set`,
and punctuation uses allocation-free `string_view::contains`. Shared Map-action,
callback-binding and closure-metadata predicates reuse existing support files.
Shorter chains, enum/SSA comparisons and source fixture contents are unchanged.
The broader C++/Python/CMake/shell scan found no additional qualifying implementation
chains; review verified all 17 raw fixture literals remain byte-identical.

Landed **c223d8cf** (Map actions), **e84eca66** (compiler classifications),
**080ec1e1** (host reserved names), **22d40e13** (test case selection), and
**451339df** (runtime/reference punctuation). No browser semantics or source-proof
authority changed; native output remains independent of LLVM and Script.

Final devbox build passed, followed by **6/6** selected CTests (7.13s) and **8/8**
selected lit cases (7.08s). All **26** final source hashes match. The first build
caught a removed local `subtract` flag still used later in an induction test;
retaining that flag with its set-derived value fixed it. No tests ran in that
first attempt. Both attempts used these commands under the build lock, with
helper/SSH stdin from `/dev/null`:

```sh
# Local helper:
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-inventories ctcompile-test-native-runtime ctcompile-test-native-reference ctcompile-test-exception-recovery ctcompile-test-host-contract ctcompile-test-escape-analysis-arrays ctcompile-test-type-inference ctcompile-native-pipeline-strings
# On devbox, from projects/compile-time-browser (reached in the second attempt):
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_(inventories|native_runtime|exception_recovery|host_contract|escape_analysis_arrays|type_inference)$'
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: (Target/Cpp/const-bindings[.]mlir|CTJS/Transforms/resolve-globals[.]mlir|CTNative/PartialEvaluation/heap-evaluation[.]mlir|CTNative/Lowering/Maps/(map-presence-proof|object-key-proof)[.]mlir|CTNative/HostContract/(prefix[.]test|Provider/mutations[.]test)|CTNative/Fixtures/Scalars/string[.]test)$'
```

Required `tools/format.sh --check` reports **16** existing diagnostics in four
untouched files: `ctdrive.cpp`, `ProviderPaths.h`, `Heap.h` and `Symbolic/Facts.cpp`.
Formatting the two affected files that previously failed removed four old
diagnostics. All changed files pass scoped formatting and whitespace checks.
Full CTest/compiler lit, broad corpus/matrix, WPT/test262 and sanitizers were
skipped. No push. **Next integration boundary remains:** confined Bootstrap
`R.find` spread/concat with an explicit element, then separately proved document
roots and ownership. Full Bootstrap initialization and the application driver
remain unfinished.

## Shared DOM intrinsic membership, 2026-09-20 UTC

**65273121** replaces the duplicated name-comparison chains in `Contract.cpp`
and `DOMEntry.cpp` with one `llvm::StringSet<>` and `.contains()`. The table is
shared through existing host-contract support. The same 13 names, duplicate
rejection and parser-only class-helper/Error exceptions remain; names longer
than 23 bytes refuse before hashing, and the typed count bound follows set size.
No browser semantics, emitted calls or source-proof authority changed.

Final `.contains()` sources built on the devbox and passed host-contract CTest
**1/1** (0.52s total); all **three** final source hashes match. Before the equivalent
lookup-spelling change, the shared StringSet passed host CTest **1/1** (0.52s)
and prototype-selector lit **1/1** (78.45s), with **32 native executions / 62
refusals**. An uncommitted map draft also passed the same focused checks (host
0.51s, prototype 77.88s); the final implementation follows the user's StringSet
and contains refinements. No native replay was needed for the spelling change.

Commands under `/tmp/ctbrowser-devbox-build.lock`, helper/SSH stdin from `/dev/null`:

```sh
# Local helper for each draft and the final sources:
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-host-contract
# On devbox, from projects/compile-time-browser:
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_host_contract$'
# Map and StringSet drafts, before the final contains spelling:
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-prototype-query[.]test$'
```

Required formatting retains **20** baseline diagnostics in six untouched files;
changed C++ formatting and whitespace pass. Full CTest/compiler lit, broad
corpus/matrix, WPT/test262 and sanitizers were skipped. No push.
**Next integration boundary remains:** confined Bootstrap `R.find` spread/concat
with an explicit element; default document roots need their own ownership contract.
The detailed proof requirements and unfinished application work follow below.

## Original prototype selector calls, 2026-09-20 UTC

**3d701f64** proves Bootstrap's original
`Element.prototype.querySelector.call(element, selector)` and
`querySelectorAll.call(element, selector)` for explicit nonnull element and
String inputs. DOM manifests supply `"initial_intrinsics": ["Element", "Function"]`.
These guarantee original global/prototype/selector identities and the original
Function.prototype.call chain without shadows or accessors. Saved constructor/prototype/method
aliases retain those identities; mutation, escape, reentry, detached calls,
wrong arity, coercion and unguarded nullable receivers refuse.

The compiler erases the proved lookup chain and reuses the existing C++ selector
method objects. Argument zero supplies the receiver's document and live Style
engine, including guarded selector results and checked snapshot members. Raw-IR
checks cover exact evidence, source fingerprints and every insufficient work
budget; incomplete proofs publish no global/prototype/method/call evidence.
No browser code, runtime semantics or ownership changed.

Measured on the devbox: host-contract CTest **1/1** (0.51s total), existing direct
query/query-all lit cases passed, final prototype lit **1/1** (77.12s). The new
case executed **32 native clients / 62 refusals**, with both providers, policies,
printing layouts and GCC/Clang. Existing direct cases add 32 executions/58
refusals. All **12** code/test hashes matched before commit. These are standalone
DOM/Core/Style clients and Script/AOT symbol audits; this new driver does not run
VM/browser differential observations.

Exact commands under `/tmp/ctbrowser-devbox-build.lock`, with helper/SSH stdin
from `/dev/null`:

```sh
# Local helper:
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-host-contract
# On devbox, from projects/compile-time-browser:
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_host_contract$'
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-(prototype-query|query|query-all)[.]test$'
# After the test-only correction, local helper then devbox:
tools/remote-build.sh ctjs-opt ctjs-translate
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-prototype-query[.]test$'
```

The first prototype fixture used unsupported Number strict equality in its
zero-length witness. Replacing `children.length === 0` with `!children.length`
retained the receiver-sensitive observable check using existing Number negation;
no production proof was widened. Required formatting retains **20** pre-existing
diagnostics in six untouched files; all changed C++/Python, syntax and whitespace
checks pass. Full CTest/compiler lit, broad corpus/matrix, WPT/test262 and
sanitizers were skipped. No push.

**b8b5c3e2** replaces the requested enum-to-callee switch in `EmitC/DOM.cpp`
with an LLVM DenseMap of 17 names. Optional-String attribute/Number overloads
remain explicit type checks; specialized methods still require their own lowering.
This table is compiler-only; generated calls and argument order are unchanged.

**3b0336d1** fixes two stale String-test refusals after the earlier `c5bcd3de`
loop proof. Entry/helper attribute-mutation loops now have positive source/native
checks, including actual attribute absence, and unsafe-call loop variants still
refuse. Eight Node/VM observations were added; binary and refusal counts did not
change. A focused devbox lowering of the original helper fixture confirmed its
existing admission before the test update. No production loop proof changed.

The map's first build caught a local iterator name collision, fixed by naming
its lookup `calleeName`. The next gate passed class-list/prototype lit but stopped
at the stale String control after its positive comparisons. Final String lit
passed **1/1** (150.19s): **809 Node/VM observations**, **8 GCC/Clang binaries**,
**1,000 source refusal checks** and **463** additional provenance/depth/budget
checks. Its replacement subcheck also reported **11** source observations and
**4** native executions. All **14** final session code/test hashes match the
devbox. Final required formatting retains the same 20 baseline diagnostics;
changed formatting, Python syntax and whitespace checks pass.

Exact map/test followup commands, under the same build lock and stdin rules:

```sh
# Local helper, used for both map attempts and the test-only rerun:
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-reference
# On devbox, from projects/compile-time-browser, after the name correction:
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-(strings|class-list|prototype-query)[.]test$'
# After correcting the two stale loop controls:
~/.lit-venv/bin/lit -av build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-strings[.]test$'
```

**Next exact boundary:** Bootstrap `R.find` at `bootstrap.bundle.js:341` still
wraps the proved call in `[].concat(...nodes)`. Start with an explicit element
and confined local length/indexed consumption. Reuse the element vector only
after proving original Array concat/species and absent element
`@@isConcatSpreadable` hooks. The current VM materializes proxy iterables with
a **2^24-member cap** (`Script/vm/call/invoke.cpp`); preserve it in spread
conversion without changing direct query-all length. Default
`document.documentElement` needs a separate document/session identity, nullable
root and Style contract. Constructor publication/nested Map lifetimes, retained
events, full Bootstrap initialization and the application driver remain unfinished.

## C++ selector method objects, 2026-09-20 UTC

**63b9c4d2** implements the requested object-oriented selector interface in
`Runtime/ctnative.hpp`. `matches`, `closest`, `querySelector` and
`querySelectorAll` are `inline constexpr` instances of stateless classes with
`const` templated `call` members. Generated C++ now spells, for example,
`ctnative::querySelector.call(element, styles, selector)`. The original public
Style calls moved into those methods; ownership, validation, exception order
and source proofs are unchanged. There is no callable table, virtual dispatch,
VM context or GC handle. Regenerate older emitted selector sources for the
new runtime helper spelling.

Focused devbox checks passed on the first gate: native runtime CTest **1/1**
(0.02s total), selector lit **3/3** (54.28s), comprising **48 native executions /
84 refusals** across both providers/policies/layouts and GCC/Clang. The existing
tests now require the `.call` spelling; generated clients retain the Script/AOT
symbol checks. All **five** final code/test hashes match the devbox.

Commands under `/tmp/ctbrowser-devbox-build.lock`, with helper/SSH stdin from
`/dev/null`:

```sh
# Local helper:
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-runtime
# On devbox, from projects/compile-time-browser:
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_native_runtime$'
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-(closest|query|query-all)[.]test$'
```

Required formatting still reports **20** pre-existing diagnostics in six
untouched files; changed C++/Python, syntax and whitespace checks pass. Full
CTest/compiler lit, broad corpus/matrix, WPT/test262 and sanitizers were skipped.
No browser/oracle changes or push. **Next:** prove original Bootstrap's
JavaScript `Element.prototype.querySelector(All).call` identities; the readable
C++ member spelling does not grant that source proof. NodeList spread, document
roots, constructor publication/nested Map lifetimes and the application driver
remain as recorded below.

## Element query snapshots and indexed DOM loops, 2026-09-20 UTC

**c5bcd3de** adds `Element.querySelectorAll(String)` through public
`style::engine::select(..., first_only=false)`. Generated code uses a local
`std::vector<ctbrowser::element_ref>`; the document owns the nodes. The existing
Style core supplies ordering, deduplication, root/scope/shadow behavior and
detached-subtree queries. Selected elements retain the input's live Style engine.
No browser code or runtime-oracle semantics changed, and native binaries link
DOM/Core/Style without Script/AOT.

Exact zero/+1 indices guarded by the same snapshot's length now work in both
structured loop forms, including ordinary `for` bodies in the after region.
Attribute/class mutations preserve saved membership. Contracts with dataset
parameters retain the conservative backedge alias refusal. Snapshot writes,
unsafe indices, escaping handles/callbacks and unproved methods still refuse.
Raw-IR tests cover permuted state tuples, false/before edges, bad latches and
every insufficient work budget; partial proofs publish no index/element evidence.

Measured on the devbox: **16 native executions / 44 refusals**, across both
providers, optimization policies, printing layouts and GCC/Clang. Final
`native-dom-query-all.test` passed **1/1** (39.33s). Initial runtime/host CTests
passed **2/2** (0.53s); after the loop fix, host CTest passed **1/1** (0.59s).
Existing `native-dom-query.test` and `native-dom-dataset.test` passed; dataset
was repeated after the loop change. All **16** final code/test hashes match.

Exact focused commands, under `/tmp/ctbrowser-devbox-build.lock` with SSH/helper
stdin redirected from `/dev/null`:

```sh
# Local helper:
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-host-contract ctcompile-test-native-runtime ctcompile-test-native-reference
# On devbox, from projects/compile-time-browser:
ctest --test-dir build --output-on-failure --no-tests=error -R '^(ctcompile_host_contract|ctcompile_native_runtime)$'
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-(query-all|query|dataset)[.]test$'
# After the loop proof fix, local helper then devbox checks:
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-host-contract
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_host_contract$'
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-(query-all|dataset)[.]test$'
# Test-fixture correction: same three-target helper, then query-all alone;
# final correction used these commands:
tools/remote-build.sh ctjs-opt ctjs-translate
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: CTNative/Browser/native-dom-query-all[.]test$'
```

The first source gate exposed the missing ordinary-loop proof, now fixed.
Another control incorrectly expected an immediate local helper to refuse;
it now tests an escaping closure. Adding that helper to the branch-local
positive snapshot hit the existing local-identity boundary, so the positive
retains its direct length return. No helper proof was widened. Required
`tools/format.sh --check` retains **20** baseline diagnostics in six untouched
files; changed C++/Python, syntax and whitespace checks pass. Full CTest,
compiler lit, broad corpus/matrix, WPT/test262 and sanitizers were skipped.

**Next exact browser boundary:** Bootstrap `bootstrap.bundle.js:341–342` uses
`Element.prototype.querySelectorAll.call(e, t)` and `querySelector.call(e, t)`.
Prove the original Element/prototype method and Function.call identities for
an explicit element and String, then reuse the existing selector helpers.
Its default `document.documentElement` receiver and NodeList spread/concat
need separate contracts; String-array iteration does not prove NodeList
iteration. Event callbacks, parent traversal and sanitizer-owned DOMParser
documents remain further boundaries. Original B/Data+B helper/inherited
constructor publication, nested Map lifetimes, full Bootstrap initialization
and the native application driver are unfinished. No push.

## LLVM command lines and lookup tables, 2026-09-20 UTC

**a21ee995** moves `ctcompile` to a generated `llvm::opt::GenericOptTable` and
`ctbaseline`/`ctpageload` to `llvm::cl::opt/list`. `ctjs-opt` and `ctjs-translate`
already use LLVM parsing through their MLIR drivers. Boost.ProgramOptions is
removed. LLVM **23** is now required even with `CTCOMPILE_ENABLE_MLIR=OFF`;
MLIR remains optional. Named options, defaults, short aliases and parse-error
exit 2 remain; help uses LLVM formatting. `ctpageload` now rejects extra inputs.

**58a56367** maps String method names to the existing native/host enums and uses
`DenseMap` for method and dataset-value proof lookup. RegExp identity, first-unit
lowercasing, receiver checks and bounded key hashing remain. **a6fe76a6** uses
`StringSet` for shape-instantiation counts. The container review kept ordered
JavaScript Map snapshots, sorted emitted fields and escape-lattice vectors;
those orders have consumers. No speedup was measured or claimed.

Measured on the devbox: selected CTests **6/6** (0.85s), selected lit **5/5**
(168.91s), final expanded CLI CTest **1/1** (0.25s). CLI checks cover aliases,
missing/duplicate/unknown arguments, empty entry, a path starting with `--`,
real bundles/manifests, baseline input order and page images. Packaging roundtrip
also checks launcher execution, output-write failures and matching rendering.
A separate MLIR-disabled configuration succeeded; it was a configure check,
not another build. All **17** changed code/test hashes match the devbox.

Commands ran under `/tmp/ctbrowser-devbox-build.lock` (SSH/helper calls used
`</dev/null` so they could not consume the enclosing shell heredoc):

```sh
tools/remote-build.sh ctcompile-tool ctcompile-tool-ctbaseline ctcompile-tool-ctpageload ctjs-opt ctjs-translate ctcompile-test-host-contract ctcompile-test-native-reference ctcompile-test-app_bundle ctrun ctbrowse
# On devbox, from projects/compile-time-browser:
ctest --test-dir build --output-on-failure --no-tests=error -R '^(ctcompile_version|ctcompile_usage|ctcompile_help|ctcompile_rejects_nonsense|ctcompile_cli|ctcompile_host_contract)$'
~/.lit-venv/bin/lit -v build/ctcompile/test --filter='^ctcompile :: (Packaging/roundtrip[.]test|CTNative/Browser/native-dom-(strings|dataset|class-list)[.]test|CTNative/Lowering/Objects/one-shape-one-definition[.]mlir)$'
# After the final CLI edge cases and unconditional LLVM version check:
# Local helper, then devbox CTest/configure:
tools/remote-build.sh ctcompile-tool ctcompile-tool-ctbaseline ctcompile-tool-ctpageload
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_cli$'
cmake -S ctbrowser -B build-cli-no-mlir -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="$PWD/tools/clang-std-embed/bin/clang++" -DCMAKE_PREFIX_PATH=/home/linuxbrew/.linuxbrew -DCTBROWSER_ENABLE_PROJECTS=ctcompile -DCTCOMPILE_ENABLE_MLIR=OFF -DCMAKE_DISABLE_FIND_PACKAGE_MLIR=ON -DBUILD_TESTING=OFF -DCTBROWSER_BUILD_EXAMPLES=OFF
```

Required `tools/format.sh --check` still reports the same **20** baseline
diagnostics in six untouched files; changed C++ and Python pass scoped
clang-format/Black checks, Python syntax and `git diff --check`. Full CTest,
full compiler lit, broad corpus/matrix, WPT/test262 and sanitizer runs were
skipped. No browser/runtime-oracle edits or push.

**Next integration boundary:** typed `Element.querySelectorAll(String)` snapshots
and proved iteration through public `style::engine::select(..., first_only=false)`.
Document-root queries need an explicit document receiver contract. Original
B/Data+B helper/inherited constructor publication, nested Map lifetimes,
retained callbacks and Shell/rendering remain. Full Bootstrap initialization
and the native application driver are unfinished.

## Native class-list integration, 2026-09-20 UTC

**6ea177bc** lifts `classList.contains/add/remove` into the public DOM token-list
API; Shell now converts arguments and delegates to that core. **7743e77a** binds
the methods in native output, including variadic/zero-argument mutations, saved
local aliases, guarded selector receivers and undefined-to-void results. Element
and class-list names use enum maps, with bounded key lengths before hashing.
Generated clients retain ordinary document ownership and link no Script/AOT.

Measured: **32 native executions / 30 refusal controls**, plus two successful
read-only dataset lowerings. Browser/host CTests passed **4/4** (0.62s); the final
host repeat passed **1/1** (0.49s) and class-list lit **1/1** (57.07s). Existing
DOM/closest/query lit cases also passed. The initial class-list run caught an
unsupported comparison in its new test control; the corrected control returns
the supported length directly. All **12** final code/test hashes match devbox.
Required formatting still reports 20 baseline diagnostics in six unchanged
files; changed files pass. Full suites, WPT/test262, broad corpus/matrix replays
and sanitizer runs were skipped. Historical measurements remain historical.

**Next browser boundary:** `Element.querySelectorAll(String)` needs a typed
snapshot of document-owned node handles and proved iteration. Reuse public
`style::engine::select(..., first_only=false)`; document-root queries need an
explicit document receiver contract. Retained callbacks, Shell/rendering,
original B/Data+B constructor publication through helpers/inheritance and nested
Map lifetimes remain. Full Bootstrap initialization and the application driver
are unfinished. No push.

[Exact changes, commands and next boundary](handoff/2026-09-20-class-list.md).

## Native browser queries and guarded handles, 2026-09-20 UTC

**fbb510e2** permits existing DOM/Style calls on a `closest()` result inside
its proved present branch. **67a8af42** adds `Element.querySelector(String)`
through the public ctbrowser selector engine and reuses that guarded borrow.
Selector chains retain their original live Style engine; document ownership,
source effect order and no-Script/VM/GC linkage remain intact. **c188f3e5**
corrects the guide's stale claim that document-owned Data sessions were only
analysis: that integration was already implemented.

The two new focused tests pass **32 native executions / 40 refusal controls**
across GCC/Clang, both optimization policies and printing layouts, including
owned-session calls. Final host CTest **1/1** (0.49s), closest/query lit **2/2**
(39.44s); the preceding DOM/DOM-session/closest selection passed **3/3**
(251.44s). The existing session test includes generated-client ASan/UBSan.
All seven final query file hashes match. Required formatting still reports
20 baseline diagnostics in six unchanged files; changed files pass.

**Next browser boundary:** `classList.contains/add/remove` still need a shared
plain C++ token-list API extracted from Shell before native binding; do not
duplicate their algorithms. Document-root queries, `querySelectorAll` iteration,
retained callbacks and Shell/rendering integration also remain. Original
B/Data+B constructor publication through helpers/inheritance and nested Maps
is unchanged from the previous handoff. Full Bootstrap initialization and the
application driver remain unfinished. No browser/runtime-oracle edits or push;
full suites and broad replays were skipped.

[Exact changes, focused checks and next boundary](handoff/2026-09-20-guarded-closest.md).

## Terminal constructor publication and AND input congruence, 2026-09-20 UTC

Terminal constructor registration now runs natively. **05dbf77e** moves one
proved final `Map.set(literal, this)` immediately after each exact local `new`,
then reuses the completed-record ownership and alias proofs. The two unchanged
publication cases and four new positives add **48 native executions**, including
overwrite, deletion and saved-alias disposal. **7497424c** preserves input
congruence through AND array indices.

Focused selection: **33 observations / 128 main native executions / 66
unprepared refusals / 58 preparation refusals**. Exact arrays and host CTests
pass **1/1** each; lowering lit **3/3** and escape lit **4/4** pass. All **44**
record-pointer artifacts, **10** raw refusal controls and **ten** tested file
hashes pass. AND recording: **165 sites / 35 sound / 35 of 41 precision**, zero
violations, partial, pending or unclaimed sites. Required formatting retains
20 baseline diagnostics in six unchanged files; changed files pass. Original
class fixtures 01–29 and all 43 prior AND source functions remain unchanged.

**Next:** original B/Data+B still refuse `e.set(..., this)` through helpers and
inherited construction. Prove partial initialization, exception/reentry and
owner lifetime across the captured outer element Map and nested DATA_KEY Map,
including conditional conflict checks, nullable gets and deletion. Preserve
`e.set`, `e.remove`, `P.off`, configuration and disposal. Nonterminal publication,
transported/nested and region-local record Maps remain. Full Bootstrap and the
application driver are unfinished. No browser/runtime-oracle edits or push;
full suites and broad replays were skipped.

[Exact changes, focused checks and next boundary](handoff/2026-09-20-terminal-publication.md).

## Saved record Map snapshots and OR/XOR index congruence, 2026-09-20 UTC

Saved Map aliases now participate in the fixed own-field snapshot proof.
**c4087c5c** includes their snapshots and writes before folding, enabling method
snapshots, inherited snapshots and disposal through completed record aliases.
Five new positives add **40 native executions**. Field additions, deletion,
escaping receivers and constructor publication remain refused.
**8dd68d45** preserves the transformed low-bit residue of OR/XOR array indices.

Focused selection: **60 observations / 224 main native executions / 120
unprepared refusals / 85 preparation refusals**. Exact host and arrays CTests
pass **1/1** each; lowering lit **3/3**, escape lit **4/4** pass. All **80**
record-pointer artifacts, **10** raw refusal controls and **nine** tested file
hashes pass. OR/XOR recording: **120 sites / 27 sound / 27 of 34 precision**,
zero violations, partial, pending or unclaimed sites. Required formatting
retains 20 baseline diagnostics in six unchanged files; changed files pass.
Original class fixtures 01–28 and all 28 prior OR/XOR sources remain unchanged.

**Next:** original B/Data+B still refuse constructor-time `e.set(..., this)`.
Connect partial initialization, exception/reentry and enclosing owner lifetime
proofs through the captured outer element Map, nested DATA_KEY Map and helper
calls. Preserve conflict checks, `e.remove`, `P.off`, configuration and disposal.
Transported/nested and region-local record Maps remain. Full Bootstrap and the
application driver are unfinished. No browser/runtime-oracle edits or push;
full suites and broad replays were skipped.

[Exact changes, focused checks and next boundary](handoff/2026-09-20-record-map-snapshots.md).

## Saved record Map methods and AND index gaps, 2026-09-20 UTC

Saved Map aliases now call immutable instance methods in native output.
**6fe314c8** connects the existing exact record origins to the method census,
retains receiver identity across overwrite/delete, and reuses final closed-shape
receiver admission. The unchanged original alias-method case and five new
positives add **48 native executions**. **8b9d3f8b** preserves AND-mask trailing
zero bits as an index stride, admitting disjoint reloads inside those gaps.

Focused selection: **46 observations / 160 main native executions / 92
unprepared refusals / 56 preparation refusals**. Exact host and arrays CTests
pass **1/1** each; lowering lit **3/3** and escape lit **3/3** pass. Ten raw
forged-proof refusals and **60** concrete record-pointer artifact checks pass;
all ten tested file hashes match. AND oracle: **129 sites / 26 sound / 26 of 32
confined precision**, zero violations, partial, pending or unclaimed sites.
Required formatting retains 20 diagnostics in six unchanged files; changed
files pass. Original class files 01–27 and all 32 prior AND source bodies remain
unchanged.

**Next:** original B/Data+B still refuse constructor-time registration of
`this`. Prove partial publication, exception/reentry behavior and enclosing owner
lifetime through the original nested Data Maps and helper calls. Saved-alias
methods now work after completed construction; own-key snapshots and transported
record Maps still need their separate proofs. Preserve conflict checks,
`e.set`, `e.remove`, `P.off`, configuration and disposal bodies. Full Bootstrap
and the application driver remain unfinished. No browser/runtime-oracle edits
or push; full suites and broad replays were skipped.

[Exact changes, focused checks and next boundary](handoff/2026-09-20-record-map-methods.md).

## Native record Map borrows and remainder congruence, 2026-09-20 UTC

Completed class records now run through local native Maps. **c955af5a**
connects the live retention proof to closure lifting, closed-shape groups,
exact saved-read origins and borrowed pointer emission. The six unchanged
preparation-only cases and three new positives add **72 native executions**.
Maps use the existing `map_storage<std::string, concrete_record *>`; records
stay in the enclosing stack frame and saved aliases survive overwrite/delete.
Existing object-identity Maps retain their separate representation.
**35ceaf8b** preserves remainder congruence across quotient wraps using `gcd`.
**2f10fa93** preserves branches in two older Map refusal tests.

Focused selection: **37 observations / 112 main native executions / 74
unprepared refusals / 47 preparation refusals**. Exact host and arrays CTests
pass **1/1** each; lowering lit **3/3**, escape lit **3/3** and adjacent Map lit
**6/6** pass. Eight raw refusal controls reject forged proof markers; all 36
record C++ artifacts carry concrete borrowed pointers. All 19 tested hashes
match. Remainder oracle: **183 sites / 36 sound / 36 of 41 confined precision**,
zero violations, partial, pending or unclaimed sites. Required formatting still
reports 20 diagnostics in six unchanged files; changed files pass.

**Next:** original B/Data+B still refuse constructor-time registration of
`this`. Prove partial publication, exception/reentry behavior and owner lifetime
before transporting record payloads through the original nested Data Maps and
helper calls. Current record Maps require direct entry-block operations,
literal String keys and present gets. Saved-alias methods and own-key snapshots
remain separate proofs. Preserve conflict checks, `e.set`, `e.remove`, `P.off`,
configuration and disposal bodies. Full Bootstrap and the application driver
remain unfinished. No browser/runtime-oracle edits or push; full suites and
broad replays were skipped.

[Exact changes, focused checks and next boundary](handoff/2026-09-20-record-map-borrows.md).

## Completed record Map preparation and remainder bands, 2026-09-20 UTC

**77e02a65** prepares completed same-constructor records stored in direct local
Maps. Six new sources preserve registration, saved reads, overwrite/delete,
clear and alias writes. **They remain preparation-only: zero new native
executions.** The constructor lifter still refuses retained record shapes.
Source proofs require complete standard Map identity, entry-block owners,
literal String keys and present reads; saved constructor/method selectors and
own-key snapshots without Map alias evidence remain refused.
**5e69b712** preserves exact remainder endpoints and stride within one quotient
band, retaining wrap fallback, complete reload checks and exact replay.

Focused selection: **31 observations / 40 existing native executions / 62
unprepared refusals / 41 preparation refusals / 12 prepared native refusals**.
Exact host and arrays CTests each pass **1/1**; lowering lit **3/3** and escape
lit **3/3** pass. Remainder oracle: **141 sites / 27 sound / 27 of 32 confined
precision**, zero violations, partial, pending or unclaimed sites. All nine
tested hashes match. Required formatting retains 20 diagnostics in six unchanged
files; changed files pass. Original class sources 01–25 remain unchanged.

**Next:** connect the retained-record proof to closure lifting, Map payload
family/origin evidence, shape inference and typed pointer emission. Preserve
saved record identities across overwrite/delete and prove enclosing owner
lifetimes. Own-key snapshots need those alias edges before folding. Original
B/Data+B still refuse constructor registration; partial publication requires
exception/reentry proof before nested Data storage. Preserve conflict checks,
`e.set`, `e.remove`, `P.off` and configuration/disposal bodies. Full Bootstrap
and the application driver remain unfinished. No browser/runtime-oracle edits
or push; full suites, whole class lit and broad replays were skipped.

[Exact changes, focused checks and next boundary](handoff/2026-09-20-record-map-preparation.md).

## Inherited getters and signed remainder bounds, 2026-09-20 UTC

**ddf01d98** preserves exact inherited static getter environments, including
transitive dependencies, constructor reads, shared methods and direct/static
reads. Four unchanged sources and seven new positives add **88 native
executions**. Getter overrides and changed dependencies remain refused.
**d121e11b** proves bounded signed remainder indices while retaining exact
replay, valid final indices and complete reload checks.

Two disjoint focused class selections total **40 observations / 144 main native
executions / 80 unprepared and 49 preparation refusals**. Exact host and arrays
CTests each pass **1/1**; lowering lit **3/3** and escape lit **3/3** pass.
Remainder oracle: **99 sites / 18 sound / 18 of 23 confined precision**, zero
violations, partial, pending or unclaimed sites. All eleven tested hashes match.
Required formatting retains 20 diagnostics in six unchanged files; changed
files pass. Original class source files 01–24 remain unchanged.

**Next:** original B/Data+B still refuse receiver observation at registration.
First prove typed record retention after completed construction in a local Map,
including saved reads across overwrite/delete and enclosing owner lifetime.
Existing Map runtime templates can carry record pointers; source/closure proofs,
Map family/origin evidence and shape inference still need to connect. Constructor
publication needs a separate exception/reentry proof before the original nested
Data Maps can retain `this`. Preserve conflict checks, `e.set`, `e.remove`,
`P.off` and configuration/disposal bodies. Full Bootstrap and the application
driver remain unfinished. No browser/runtime-oracle edits or push. Full suites,
whole class lit, DOM replay and broad matrices were skipped.

[Exact changes, focused checks and next boundary](handoff/2026-09-20-inherited-getters.md).

## Optional scalar Map keys and remainder indices, 2026-09-20 UTC

**7b456d79** preserves optional Number/Boolean/Null/Undefined Map keys with the
existing finite scalar carrier, SameValueZero and canonical numeric zero.
Five unchanged numeric-key sources and six new cases add **88 native executions**;
ordered numeric value snapshots add **eight**. Optional-key snapshots and mixed
String/Number or object/absent class keys remain refused. **4a0e7910** proves
bounded nonnegative Number remainder indices with exact replay and complete
reload checks. **f428fd97** retains conditional branches in three existing Map
refusal RUN lines; source bodies and expected diagnostics are unchanged.

Focused class selection: **27 observations / 128 main native executions / 54
unprepared and 37 preparation refusals**, plus **four native boundary controls**.
Exact runtime, host and arrays CTests each pass **1/1**; escape lit **3/3**, new
Map lit **1/1** and adjacent Map lit **2/2** pass. Remainder oracle: **63 sites /
nine sound / nine of 15 confined precision**, zero violations, partial, pending
or unclaimed sites. All 13 tested native/escape hashes match. Required formatting
retains 20 diagnostics in six unchanged files; changed files pass.

**Next:** original B/Data+B still refuse receiver observation at registration.
Prove typed class payloads and enclosing owner lifetime across complete
constructor/helper/Map calls, saved aliases, overwrite/delete, constructor
failure and disposal. Preserve nested Maps, conflict checks, `e.set`, `e.remove`,
`P.off` and configuration bodies. Scalar identity storage cannot substitute for
a concrete class record. Full Bootstrap and the application driver remain
unfinished. No browser/runtime-oracle edits or push. Full suites, whole class
lit, DOM replay and broad matrices were skipped.

[Exact changes, focused checks and next boundary](handoff/2026-09-20-scalar-map-keys.md).

## Helper Map environments and signed AND masks, 2026-09-20 UTC

**a95574e6** passes exact Map environments through local captured helpers and
Data-style holders. Original helper bodies receive ordinary Map parameters;
constructor/method closures retain the corresponding immutable cells. Entry,
transitive, shared, distinct and inherited calls add **72 native executions**,
including the two original inherited-helper/holder sources. Missing user arguments
are padded before environment arguments. Complete caller, symbol, identity,
initialization, static-capture and work-budget checks remain mandatory.
**ed61b17a** extends AND index enclosures to signed/high-bit masks within a proved
conversion band, retaining exact replay and the complete reload census.

Focused class selection: **61 observations / 208 main native executions / 122
unprepared and 90 preparation refusals**, plus **ten native boundary controls**.
Exact host **1/1**, arrays **1/1**, lowering lit **3/3** and escape lit **3/3** pass.
AND oracle: **96 sites / 18 sound / 18 of 24 confined precision**, zero violations,
partial, pending or unclaimed sites. All eleven final tested hashes match.
Required formatting retains 20 diagnostics in six unchanged files; changed files
pass. Full suites, whole class lit, DOM replay and broad matrices were skipped.

**Next:** original B and Data+B both refuse receiver observation at registration.
Local helper Map transport is connected; retaining `this` in the nested Data Maps
still requires typed payload and lifetime proof across construction/disposal,
set/get/remove, saved aliases, overwrite/delete and failure. Retain original
`e.set`, `e.remove`, `P.off`, nested Map creation and all configuration bodies.
Native output uses stack class records, direct functions and existing scalar Map
owners; no stored-class graph or Script/VM/GC dependency was introduced. A new
same-named helper/method control pins Node 8 versus a VM recursion throw.
Full Bootstrap and the application driver remain unfinished. No browser/runtime
changes or push.

[Exact changes, focused checks and next boundary](handoff/2026-09-20-helper-map-environments.md).

## Inherited Map captures and signed OR/XOR bands, 2026-09-20 UTC

**d4c4a82b** preserves exact Map capture cells through inherited constructor
copies. Base and leaf slot numbers are remapped by Map identity; shared Maps,
distinct Maps, repeated ancestry, sibling leaves, ordinary inherited methods and
mixed helper captures add **48 native executions**. Original numeric-key
`class-map-inherited` now prepares and retains its optional-key carrier refusal.
**adac2c92** proves OR/XOR index enclosures within one signed conversion band and
converted sign half, preserving complete reload checks and exact write replay.

Focused class selection: **42 observations / 136 main native executions / 84
unprepared and 66 preparation refusals**, plus **six native boundary controls**.
Exact host **1/1**, arrays **1/1**, lowering lit **3/3** and escape lit **4/4** pass.
OR/XOR oracle: **84 sites / 18 sound / 18 of 24 confined precision**, with zero
violations, partial, pending or unclaimed sites. All ten tested file hashes match.
Required formatting retains 20 diagnostics in six unchanged files; changed files
pass. Full suites, whole class lit, DOM replay and broad matrices were skipped.

**Next:** preserve Map environments through the original local Data holder/helper
calls. Those rewrites currently discard their callee capture identity; merely
allowing the capture is insufficient. Inherited direct captures now work, while
captured lexical super-method targets remain refused. Typed stored-class ownership
across set/get/remove, saved aliases, overwrite/delete and failure remains unproved.
Keep original `e.set`, `e.remove`, `P.off` and configuration bodies. Output retains
stack class records and existing scalar Map owners, with no Script/VM/GC or new
stored-class graph. Full Bootstrap and the application driver remain unfinished.
No browser/runtime changes or push.

[Exact changes, focused checks and next boundary](handoff/2026-09-20-inherited-map-captures.md).

## Direct class Map captures and OR/XOR indices, 2026-09-20 UTC

**cfc7a229** preserves immutable Map capture cells and selected slots for direct
local constructors and instance methods. Existing closure and Map lowering
proves calls, types and ownership; shared mutations, distinct Maps, mixed capture
slot renumbering and a method loop add **40 native executions**. Original numeric
field-key cases reach the optional-key carrier boundary; constructor own-callee
getter reads, helper/holder, static and inherited Map captures remain refused.
**5b2e832a** bounds nonnegative signed-i32 OR/XOR array indices while retaining
exact replay, saved children, gap contents and the complete reload census.

Final focused class selection: **38 observations / 120 main native executions /
76 unprepared and 68 preparation refusals**, plus **nine native boundary controls**.
Exact host **1/1**, arrays **1/1**, lowering lit **3/3** and escape lit **3/3** pass.
OR/XOR oracle: **42 sites / seven sound / seven of 11 confined precision**, zero
violations, partial, pending or unclaimed sites. All 13 tested hashes match.
Required formatting retains 20 diagnostics in six unchanged files; changed files
pass. Full suites, whole class lit, DOM replay and broad matrices were skipped.

**Next:** preserve Map captures through the local Data holder and inherited
constructor/method invocation graph. Original Data+B, now explicitly declaring
Map identity, still refuses that boundary. Typed stored-class ownership across
set/get/remove, saved aliases, overwrites/deletes and failure remains unproved.
Keep original `e.set`, `e.remove`, `P.off` and configuration bodies. Native output
currently reuses shared scalar Map owners; no stored-class or cyclic ownership
graph was admitted. Full Bootstrap and the application driver remain unfinished.
No browser/runtime changes or push.

[Exact changes, focused checks and next boundary](handoff/2026-09-20-class-map-captures.md).

## Receiver forwarding and masked indices, 2026-09-20 UTC

**52cc51db** composes read-only constructor-receiver borrows through exact helper
chains. Global, captured and holder chains, reordered parameters and inherited
argument effects add **40 native executions**. Every leaf read must name a field
already present at the original call. Storage, returned aliases, writes, dynamic
keys, recursive proofs and unused ambient effects remain refused.
**db4d6384** bounds Number BitAnd indices with a nonnegative signed-i32 mask.
Its conservative range protects bounds/reload checks; replay updates only actual
visited elements, preserving saved children and gaps.

Focused class probe: **55 observations / 160 main native executions / 110
unprepared and 70 preparation refusals**, plus **12 native boundary controls**.
Exact host **1/1**, arrays **1/1**, lowering lit **3/3**, escape lit **3/3** pass.
Mask oracle: **48 sites / eight sound / eight of 12 confined precision**, zero
violations, partial, pending or unclaimed sites. All nine tested hashes match.
Required formatting retains 20 diagnostics in six unchanged files; changed files
pass. Full suites, whole class lit, DOM replay and broad matrices were skipped.

**Next:** unchanged B still refuses receiver observation; Data+B still refuses
its shared Map capture. Preserve selected Map captures/cells through class
preparation and reuse the existing closure lifter's capture transport, with a
complete Map identity/body/invocation proof. The local Data holder does not match
the published owned-global factory seam. Typed class-receiver storage still needs
ownership across constructor/dispose, saved aliases, overwrite/delete and failure.
Keep original `e.set`, `e.remove`, `P.off` and configuration bodies. Variable fields,
inherited getter targets, full Bootstrap and the application driver remain.
No browser/runtime changes or push.

[Exact changes, focused checks and next boundary](handoff/2026-09-20-forwarded-receivers.md).

## Constructor receiver borrows and signed output bands, 2026-09-20 UTC

**622f1ca8** proves read-only helper arguments against fields already present at
construction. Exact global, captured and holder helpers, two receiver arguments,
inherited construction and argument order add **48 native executions**. Private
capture-free direct helpers reuse the shrinking borrowed-parameter proof after
class preparation consumes their closure. Every symbol/caller remains checked;
public helpers, module references and mixed or missing arguments refuse.
**04a622d3** proves left shifts within one signed output conversion band using
exact wide products, retaining input-band, own-index and reload checks.

Final focused class probe: **63 observations / 168 main native executions / 126
unprepared and 76 preparation refusals**, plus **11 native boundary controls**.
Exact host **1/1**, arrays **1/1**, lowering lit **3/3** and escape lit **3/3** pass.
Left shift: **156 sites / 30 sound / 30 of 35 confined precision**, zero
violations, partial, pending or unclaimed sites. All eleven tested hashes match.
Changed formatting passes; the required formatter retains 20 diagnostics in six
unchanged files. Full suites, whole class lit, DOM replay and broad matrices
were skipped.

**Next:** original B still refuses receiver observation at `e.set(..., this)`;
original Data+B now identifies the captured shared Map as an unsupported helper
capture. Prove that Map's identity, complete constructor/dispose call graph and
stored class-receiver ownership across set/get/remove, saved aliases and failure.
The existing Map proof accepts entry-local scalar-field leaves and entry calls;
it has no typed class-record payload. Borrowing a helper parameter does not
permit retaining it in a Map. Preserve `e.remove`, `P.off` and all original
config/disposal bodies. Variable field presence, inherited getter targets,
full Bootstrap and the application driver remain unfinished.
No browser/runtime changes or push.

[Exact changes, checks and next boundary](handoff/2026-09-20-borrowed-receivers.md).

## Construction-time getters and left-shift bands, 2026-09-20 UTC

**5b171856** lets construction-time `this.constructor` reads reach the existing
exact getter proof instead of treating the prototype backedge as an own field.
Direct reads, nested methods, getter dependencies, argument order and fresh empty
object identity add **48 native executions**. Shadowing, effects, missing fields,
early snapshots and inherited getter targets still refuse. **2c7810ed** proves
bounded left shifts within one ToInt32 conversion band, preserving output bounds
and complete reload checks.

Focused class probe: **63 observations / 168 main native executions / 126
unprepared and 82 preparation refusals**. Exact host **1/1**, arrays **1/1** and
escape lit **3/3** pass. Left shift: **120 sites / 21 sound / 21 of 26 confined
precision**, zero violations, partial, pending or unclaimed sites. All eight
tested hashes match. Changed formatting passes; the required formatter retains
20 diagnostics in six unchanged files. Full suites, whole class lit, DOM replay
and broad matrices were skipped.

**Next:** original Bootstrap B and Data+B now refuse `class own-key snapshot
constructor observes its receiver`. Source tracing identifies the receiver
argument in `e.set(this._element, this.constructor.DATA_KEY, this)` after
`_getConfig`. Prove that registration's complete shared Map/stored-receiver
ownership and lifetime; retain `e.remove`, `P.off` and all config/disposal bodies.
Variable field presence and inherited per-leaf getter targets remain separate
obligations. Shared method/getter reads must keep the same selected target and
transitive dependencies across receivers, or require separate body proofs.
Inherited DOM, static construction, full Bootstrap and the application driver
remain unfinished. No browser/runtime changes or push.

[Exact changes, focused checks and boundary](handoff/2026-09-20-construction-getters.md).

## Construction-point methods and signed left shifts, 2026-09-20 UTC

**e6cfaede** proves instance-method receiver uses against the fields already
present at each constructor call. Existing-field reads/updates, nested calls,
nearest inherited overrides and argument order add **48 native executions**.
Early snapshots, missing/new fields and receiver escape still refuse. Snapshot
provenance survives inherited `super` expansion. **306af499** proves bounded
signed left-shift intermediates, including zero crossing and `INT32_MIN`.

Final focused class probe: **58 observations / 160 main native executions / 116
unprepared and 62 preparation refusals**. Exact host **1/1**, arrays **1/1** and
escape lit **3/3** pass. Left shift: **90 sites / 15 sound / 15 of 21 confined
precision**, zero violations, partial, pending or unclaimed sites. All ten
tested hashes match. Changed formatting passes; the required formatter retains
20 diagnostics in six unchanged files. Full suites, whole class lit, DOM replay
and broad matrices were skipped.

**Next:** original Bootstrap B and Data+B now refuse `class construction method
requires an existing own field`. `_getConfig` reaches inherited
`this.constructor` getter reads in `_mergeConfigObj`/`_typeCheckConfig`.
Prove the actual most-derived constructor/getter identity at that construction
point; it is not an own field. Variable field presence and shared Map/stored
receiver ownership remain separate obligations. Preserve `e.set`, `e.remove`,
`P.off` and all original config/disposal bodies. Inherited DOM, static
construction, full Bootstrap and the application driver remain unfinished.
No browser/runtime changes or push.

[Exact changes, focused checks and boundary](handoff/2026-09-20-construction-methods.md).

## Constructor helpers and uneven shift footprints, 2026-09-20 UTC

**e88230b1** retains receiver-independent constructor helper computations for
complete callable/source proofs before folding fixed own-field snapshots and
clearing loops. Five new positives add **40 native executions**; partial receiver
observations, unused ambient effects, replaced holders and variable fields refuse.
**1f9b600f** proves uneven right-shift index footprints with a conservative dense
range; exact replay preserves children at unwritten positions.

Final focused class probe: **33 observations / 88 main native executions / 66
unprepared and 40 preparation refusals**. Exact host **1/1**, arrays **1/1** and
escape lit **3/3** pass. Right shift: **162 sites / 34 sound / 34 of 40 confined
precision**, zero violations, partial, pending or unclaimed sites. All eight
tested hashes match. Changed formatting passes; the required formatter retains
20 diagnostics in six unchanged files. Full suites, whole class lit, DOM replay
and broad matrices were skipped.

**Next:** both original Bootstrap B and Data+B now refuse `class own-key snapshot
constructor observes its receiver`. Source inspection identifies `_getConfig`
before `e.set` registration. Prove the instance method's complete effects at the
actual construction point, including what fields exist then; do not substitute
its eventual shape. Variable field presence, shared Map/stored-receiver ownership
and registration remain unproved. Preserve all `e.remove`/`P.off` disposal effects.
Inherited DOM/getters, static construction, full Bootstrap and the application
driver remain. No browser/runtime changes or push.

[Exact changes, focused checks and boundary](handoff/2026-09-20-constructor-helpers.md).

## Own-key clearing loops and signed-shift bands, 2026-09-20 UTC

**9f367dc5** proves original own-key for-of null clearing for fixed class
fields, including inherited, conditional, empty and repeated cases. A proved
break preserves exactly one store. Six new positives and two unchanged loop
promotions add **64 native executions**. **03fcd7dd** proves signed right-shift
indices within one ToInt32 conversion band.

Focused class probe: **60 observations / 200 main native executions / 120
unprepared and 53 preparation refusals**; exact host **1/1**, arrays **1/1** and
escape lit **3/3** pass. Right shift: **138 sites / 27 sound / 27 of 35 confined
precision**, zero violations, partial, pending or unclaimed sites. All fourteen
tested hashes match. Changed formatting passes; the required formatter retains
20 diagnostics in six unchanged files. Full suites, whole class lit, DOM replay
and broad matrices were skipped.

The Array iterator replacement control records **Node 0 / VM 11**, with native
preparation refused and runtime/source unchanged. Inspected C++ uses stack
records, borrowed pointers and direct field assignments, with no iterator
container or Script/VM symbol.

**Next:** authentic Data+B still refuses fixed constructor fields. Its
helper/config/registration operations fail the census before variable presence
is isolated. Prove their complete effects and per-instance field presence, then
connect the clearing loop while retaining `e.remove`/`P.off`. Shared Map/stored
receiver ownership, inherited DOM/getters, static construction, full Bootstrap
and the application driver remain. No browser/runtime change or push.

[Exact changes, focused checks and boundary](handoff/2026-09-20-own-field-loops.md).

## Captured local holders and unsigned-shift bands, 2026-09-20 UTC

**4d909704** proves fixed local callable-holder captures in ordinary classes,
including inherited constructors, distinct base/leaf capture identities and unused
captured-slot cleanup. Eight new sources add **64 native executions**.
**74bb2f83** proves unsigned right-shift indices within the negative ToUint32 band.

Focused constructor probe: **42 observations / 128 main native executions / 84
unprepared and 45 preparation refusals**. Exact host **1/1**, arrays **1/1**,
escape lit **3/3** and DOM lit **1/1 (291.76s)** passed. DOM retains **632
observations / eight executions / 4,922 refusals**. Right shift: **99 sites / 18
sound / 18 of 24 confined precision**, zero violations, partial, pending or
unclaimed sites. All twelve tested hashes match. Changed formatting passes;
the required formatter retains 20 diagnostics in six unchanged files. Full
suites, whole class lit and broad matrices were skipped.

**Next:** authentic Data+B now reaches `class own-key snapshot requires fixed
constructor fields`, after the earlier captured-holder gate. Prove variable
own-field presence and the original clearing loop alongside complete shared Map
and stored-receiver ownership. Data registration remains unproved; preserve
`e.remove`/`P.off` and all original bodies. Inherited getters/DOM, static
construction, config/selectors/events/Popper, broader ownership and the
application driver remain. No browser/runtime change or push.

[Exact changes, focused checks and boundary](handoff/2026-09-20-captured-holders.md).

## Post-super holder calls and left-shift indices, 2026-09-20 UTC

**e055e73e** preserves post-super calls for the existing complete receiver,
holder and source-body proofs. Five new sources add **40 native executions**;
shared/multilevel inheritance, conditional values and argument order are covered.
**4015d707** proves bounded nonwrapping left-shift array indices.

Focused constructor probe: **25 observations / 72 main native executions / 50
unprepared and 20 preparation refusals**. Exact host **1/1**, arrays **1/1** and
escape lit **3/3** passed. DOM lit **1/1 (290.83s)** retains **632 observations /
eight executions / 4,922 refusals**. Left shift: **66 sites / ten sound / 10 of 16
confined precision**, with zero violations, partial, pending or unclaimed sites.
Changed formatting passes; the required formatter retains 20 diagnostics in six
unchanged files. Full suites, whole class lit and broad matrices were skipped.

**Next:** the new original Data+B witness refuses the captured local Data holder
(`class method capture is not its constructor or an inert sibling helper`). Prove
its shared Map ownership and stored receiver using the existing seams. The unchanged
W/B-only control remains refused at fixed constructor fields. Keep the nested
callable/receiver SCF-transport control, variable fields and original own-key for-of clearing effects. Full Data/config,
inherited DOM, static construction, selectors/events/Popper, broader ownership
and the application driver remain. No browser/runtime changes or push.

[Exact changes, focused checks and boundary](handoff/2026-09-20-post-super-holders.md).

## Conditional field branches and right-shift indices, 2026-09-20 UTC

**e38407aa** preserves conditional constructor branches after super and
proves own-field snapshots when every branch leaves the same ordered fields.
Original source branches and nested inherited helper captures remain checked;
seven new positives and one preserved promotion add **64 native executions**.
**4a5ef7b2** independently proves bounded right-shift indices with exact conversion
and reload guards.

Focused arrays **1/1**, escape lit **3/3**, host **1/1** and DOM lit **1/1**
passed. Class probes passed **36 observations / 104 main executions**,
including the 64 new executions. Whole class-lit attempts exposed stale refusal
expectations; the final OWN_FIELDS group and promoted constructor were checked
directly after correction. **No full class-lit pass is claimed this session.**
DOM remains **632 observations / eight executions / 4,922 refusals**. Right shift:
**63 sites / ten sound / 10 of 15 confined precision**, zero violations, partial,
pending or unclaimed sites. Twelve tested hashes match. Changed formatting passes;
the required formatter retains 20 diagnostics in six unchanged files. Exact
failures and corrected focused checks are recorded below. Full suites and broad
matrices were skipped; no browser/runtime changes or push.

**Next measured boundary:** unchanged Bootstrap W/B now refuses
`super initialization contains an unproved call`; source inspection identifies
B's `e.set` registration. Prove authentic Data/config/receiver-getter effects,
variable per-instance fields and the original own-key for-of clearing loop,
retaining dispose's `e.remove`/`P.off`. Static construction, inherited DOM,
H/config/selectors/events/Popper, broader ownership and the driver remain.

[Exact changes, focused validation and next boundary](handoff/2026-09-20-conditional-fields.md).

## Inherited own-field snapshots and complement bands, 2026-09-20 UTC

**117783b6** proves fixed inherited own-field snapshots. Shared inherited methods
require the same ordered field set on every descendant, including empty shapes;
leaf-only snapshots may add fields after proved super initialization. All ancestor
writes and partial-construction observations remain checked. Six new positives add
**48 native executions**. **170eb7e6** independently admits bounded BitNot indices
within one ToInt32 conversion band, preserving discontinuity and reload guards.

Focused arrays **1/1**, escape lit **3/3**, host **1/1** and class initialization/DOM
lit **2/2 (425.99s)** passed. Class: **292 observations / 812 main executions /
584 unprepared and 311 preparation refusals**. DOM remains **632 / eight / 4,922**.
BitNot: **60 sites / 11 sound / 11 of 14 confined precision**, zero violations,
partial, pending or unclaimed sites. Eleven tested hashes match. Changed formatting
passes; the required formatter retains 20 diagnostics in six unchanged files.
Full suites and broad matrices were skipped; no browser/runtime changes or push.

**Next measured boundary:** unchanged Bootstrap W/B now refuses
`super condition is not a proved Boolean` in B's conditional constructor.
Prove conditional field presence and the original own-key for-of clearing loop,
retaining dispose's `e.remove`/`P.off` effects. Distinct inherited shapes, implicit
rest/apply, static `this.getInstance`/`new this`, inherited DOM/getters,
H/config/selectors/events/Popper, broader ownership and the driver remain.

[Exact changes, validation and next boundary](handoff/2026-09-20-inherited-fields.md).

## Fixed own-field snapshots and BitNot indices, 2026-09-20 UTC

**674b985c** folds fixed own-field snapshot length/index observations to constants,
including exact indexed clearing. Five new positives add **40 native executions**.
Constructor field presence/order and all receiver writes are checked; original
method bodies and frames remain. Replacement returns and cached boxed-local
producers have dedicated regressions. **89f987bd** independently proves bounded
BitNot indices, rejecting signed-i32 wraparound and preserving reload/alias guards.

Focused arrays **1/1**, escape lit **3/3**, host **1/1** and class initialization/DOM
lit **2/2 (399.41s)** passed. Class: **276 observations / 764 main executions /
552 unprepared and 299 preparation refusals**. DOM remains **632 / eight / 4,922**.
BitNot oracle: **60 sites / nine sound / 9 of 14 confined precision**; the preserved
unary source now measures **63 / ten / 10 of 14**. Zero violations, partial,
pending or unclaimed sites. Twelve tested hashes match; changed formatting passes.
Required formatter retains 20 diagnostics in six unchanged files. Full suites and
broad matrices were skipped; no browser/runtime change or push.

**Next measured boundary:** original Bootstrap W/B now refuses
`class own-key snapshot requires fixed constructor fields`. The current proof
rejects inheritance; B also conditionally initializes its fields. The preserved
standalone own-key for-of clearing control still refuses snapshot consumption.
Prove inherited/conditional field presence and iterator consumption while retaining
all dispose effects. Static `this.getInstance`/`new this`, inherited DOM/getters,
H/config/selectors/events/Popper, broader ownership and the driver remain.

[Exact changes, checks and next boundary](handoff/2026-09-20-own-fields.md).

## Own static methods and unary indices, 2026-09-20 UTC

**1bd7e508** proves immutable own static method slots and exact-constructor calls,
reusing existing capture/getter proofs. Four positives add **32 native executions**;
original target frames and roots survive. Complete unused bodies retain strict
source checks. Three DOM controls cover shared helpers and both declaration
orders. **6f34888c** independently proves bounded unary plus/minus array indices.

Focused arrays **1/1**, escape lit **4/4**, host **1/1**, class initialization **1/1**
and corrected DOM lit **1/1** passed. Class: **258 observations / 724 main executions /
516 unprepared and 281 preparation refusals**. DOM: **632 / eight / 4,922**.
Unary oracle: **63 sites / nine sound / 9 of 14 confined precision**, zero violations,
partial, pending or unclaimed sites. All 13 tested source hashes match. Formatter
retains 20 diagnostics in six unchanged files; changed formatting passes. The first
DOM run failed a new diagnostic assertion; its source was preserved and the
corrected case passed. Full suites and broad matrices were skipped.

**Next measured boundary:** unchanged Bootstrap W/B gets past static-slot collection
and refuses `class receiver escapes or observes a prototype/descriptor`. Source
inspection points to B.dispose's `Object.getOwnPropertyNames(this)` and `this[t]`
clearing loop. Prove own-field enumeration/clearing without allowing arbitrary
receiver escape. Full static bodies, `this.getInstance`/`new this`, inherited
per-leaf getter/DOM proof, H/config/selectors/events/Popper, broader ownership and
the application driver remain. No browser/runtime changes or push.

[Exact changes, checks and next boundary](handoff/2026-09-20-static-methods.md).

## Nested helper captures and negative divisors, 2026-09-20 UTC

**c5cd6e4f** proves bounded immutable nested helper captures using the existing
fixed-cell and complete source-body checks. Seven new executable cases add
**56 native executions**, including a helper-only root/frame preservation check.
Shared targets, inherited capture identities, argument order and refusal controls
remain covered. **353114ba** proves exact bounded negative-divisor array
index overwrites with positive stride magnitudes and full reload-overlap checks.

Focused arrays **1/1**, escape lit **4/4**, host **1/1**, and class initialization/DOM
lit **2/2 (366.33s)** passed. Class: **246 observations / 692 main native executions /
492 unprepared and 269 preparation refusals**. DOM remains **632 / eight / 4,910**.
Negative-divisor oracle: **69 sites / nine sound / 9 of 13 confined precision**,
zero violations, partial, pending or unclaimed sites. Nine source hashes match.
Formatter retains 20 diagnostics in six unchanged files; changed checks pass.
Full suites and broad matrices were skipped. No browser/runtime change or push.

**Next measured boundary:** unchanged Bootstrap W/B now passes nested `a` → `r`
captures and refuses ordinary static method setup (`getInstance`,
`getOrCreateInstance`, `eventName`). Reuse the existing method/capture/getter
proofs for fixed constructor slots and exact receivers; retain all original
bodies. Inherited DOM per-leaf receiver/getters, `new this`, selectors and full
H/config/events/Popper remain separate proof work, as do broader ownership,
own-data provenance and the application driver.

[Exact changes, checks and next boundary](handoff/2026-09-20-nested-captures.md).

## Inherited helper captures and negative-scale overwrites, 2026-09-20 UTC

**1ef56773** completes inherited constructor helper captures: each copied read
keeps its original helper identity across base/leaf slots, transitive chains and
shared bases. Four new executable cases plus one promotion add **40 native
executions**. The original object-mutating order source remains preparation-only
with its ownership refusal; a primitive companion executes with **1010013**.

**bd099c65** proves bounded negative-scaled own-index overwrites using the existing
Number product and reload-overlap proofs. Earlier **d24dd479** (sibling helper
captures) and **b85f8c14** (reversed subtraction overwrites) are also now recorded
in the detailed handoff; both had landed before this recovery.

Focused arrays **1/1**, escape lit **4/4**, host **1/1**, and class initialization/DOM
lit **2/2 (340.27s)** passed. Class: **230 observations / 636 main native executions /
460 unprepared and 254 preparation refusals**. DOM remains **632 / eight / 4,910**.
Negative-scale oracle: **66 sites / nine sound / 9 of 13 confined precision**,
zero violations, partial, pending or unclaimed sites. Eight source hashes match.
Formatter retains 20 diagnostics in six unchanged files; changed checks pass.
Full suites and broad matrices were skipped. No browser/runtime change or push.

**Next:** Bootstrap's captured `a` helper itself captures `r`. Prove bounded
immutable nested helper captures with the complete body census, then inherited
DOM per-leaf receiver/getter proof. `a` also references `n` and `document` in its
selector branch; the null-input observation grants no authority there. Full
H/config, selectors/events/Popper, broader ownership and the driver remain.

[Exact changes, validation, recovery and next boundary](handoff/2026-09-20-helper-captures.md).

## Lexical super and composed overwrites, 2026-09-20 UTC

**6acc1c5c** resumes lexical-super dispatch: immutable nearest-base selection,
leaf receiver and argument/effect order are proved before expanding linear,
capture-free target bodies. The preserved W/B/Qi dispatch now runs natively with
**118**. Four new positives plus that promotion add **40 native executions**;
every original source section remains. Original and shadowed bodies pass the
complete census before unread inherited slots disappear. Rooted method and
constructor targets refuse before frame-stripping expansion.

**950dfb64** proves composed affine array overwrites with exact bounded signed
intermediate Numbers and positive strides. Reload overlap, growth, saved aliases,
cycles and work limits retain their refusals.

Focused arrays **1/1**, escape lit **4/4**, and class initialization/DOM lit
**2/2 (310.09s)** passed. Class: **207 observations / 564 main native executions /
414 unprepared and 234 preparation refusals**. DOM remains **632 observations /
eight executions / 4,910 refusals**. The final constructor-root guard then passed
a targeted **14-observation / 48-execution** probe and host **1/1**; the two lit
cases were not replayed after that guard. Composed oracle: **54 sites / seven
sound / 7 of 10 precision**, zero violations, partial, pending or unclaimed sites.
Ten source hashes match. Formatter retains 20 baseline diagnostics in six
unchanged files; changed checks pass. Full suites and broad matrices were skipped.

**Next:** authentic W/B helper and constructor captures, starting with W's `r`
and B's `a(t)`, then inherited DOM receiver/getter proof. The original W/B source
still reaches its captured-helper refusal; complete H/config source still reaches
an unproved closure. Configuration, selectors/events/Popper, broader ownership
and the application driver remain. No browser/Script or compliance change.

[Exact changes, commands, measurements, failures and next boundary](handoff/2026-09-20-lexical-super.md).

## Post-super calls and scaled overwrites, 2026-09-19 UTC

**7c4c4866** resumes B's post-super ordinary-call thread: four positive sources
add **32 native executions**, with receiver, method identity and ordering checks
retained. **eb8db016** proves bounded scaled array overwrites and preserves growth,
alias and cycle refusals. All prior source fixtures remain.

Focused host **1/1**, class initialization/DOM lit **2/2 (308.94s)**, arrays **1/1**,
and scaled/quotient/offset escape lit **3/3** passed. Class: **195 observations /
524 main native executions / 390 unprepared and 225 preparation refusals**.
DOM remains **632 observations / eight executions / 4,910 refusals**. Scaled
oracle: **54 sites / seven sound / 7 of 10 precision**, zero violations or
unclaimed/partial/pending sites. Nine source hashes match. Formatter retains
20 baseline diagnostics in six untouched files; changed checks pass. Full suites
and broad matrices were skipped.

**Next:** Qi's immutable lexical-home/base lookup, retaining the leaf receiver.
The preserved 118-result inherited-dispatch now reaches its receiver/prototype
refusal. Captured helpers, full Bootstrap behavior, broader ownership and the
application driver remain. The user is switching to the updated unattended loop,
which now renders live JSON events as readable activity and retains raw logs.

[Exact changes, commands, measurements and loop validation](handoff/2026-09-19-post-super.md).

## CMake ownership, 2026-09-19 UTC

**099852d2** atomically merges the CMake hygiene work. All **192 maintained C++
folders** now have local CMake ownership; 183 `CMakeLists.txt` files were added.
Sibling source lists and parent-relative CMake paths are gone. Existing compiled
sources, flags, link commands, executable paths and test settings are preserved,
with normalized paths and a relocated test PCH. The new deduction executable is
optional; the new `cmake_hygiene` CTest checks ownership and paths.

Ten distinct focused CTests and two lit cases passed. Browser-only configuration
with LLVM/MLIR disabled, the installed package consumer, and a standalone
MLIR-off compiler build passed. Four missing public files and installed subsystem
aliases were fixed. The pinned formatter retains 20 baseline diagnostics in six
untouched files; changed Python formatting passes. Full suites and corpus runs
were skipped. [Exact changes and validation](handoff/2026-09-19-cmake-hygiene.md).

The next native boundary remains B's `this._getConfig(t)` after its own `super()`,
then Qi's lexical home/base lookup. No native admission or runtime behavior changed.

## Repository file splits, 2026-09-19 UTC

**76df4a57** atomically merges the browser cleanup after separate area commits.
Compiler splits landed separately, ending with **7f003cff** for the native DOM
test drivers and preserved controls. All **92 maintained files** formerly over
1,000 lines are now split. Seven vendor/upstream files remain untouched.
Handwritten C++ uses real `.hpp` headers and separately compiled `.cpp` files.

Focused compiler CTests **6/6**, browser CTests **35/35**, class/escape lit **3/3**,
recorder, citation and the complete native DOM Strings lit passed. DOM Strings measured
**801 observations / eight binaries / 1,000 source refusals**. Three outdated
controls were corrected while preserving their source and relevant refusals;
no compiler admission or runtime behavior was changed. The pinned formatter
retains 20 baseline diagnostics in six untouched files; changed files pass.
AGENT-SYNC.jsonl history is archived under the user's approval. Full suites and broad
matrices were skipped. [Detailed changes and validation](handoff/2026-09-19-file-splits.md).

All eight devbox source copies match home by checksum. Twelve retired source
copies remain cache-only; all 49 previously recorded build/tool roots survive.
The temporary browser worktree was properly removed.

**Next native boundary is unchanged:** B's ordinary `this._getConfig(t)` after
`super()`, then Qi's lexical home/base lookup. The full Bootstrap path and the
application driver remain unfinished. The merged class/DOM and citation checks
also passed.

## Nearest method overrides and exact quotient overwrites, 2026-09-19 UTC

Resumed clean **171eee2a** and the nearest-override thread claimed by the
session explicitly abandoned at 14:54:19 in AGENT-SYNC. Both agents' histories,
unmerged branches, standing protocol and handoffs were reviewed. September 7 WIP
is already an ancestor; no recovery merge was needed. Three agents supplied
native dispatch review, the authentic Bootstrap continuation and an independent
quotient-index escape draft. Initial service limits interrupted their first
turns; all resumed tasks completed. Root reviewed, gated and committed each area
separately. Independent final native review found no blocker.

**0414e5fb** selects the nearest ordinary method on a proved local inheritance
chain. Every ancestor body, including shadowed definitions, still passes the
complete receiver and source census. Unused base/method identities disappear only
after checking callable and symbol uses. Four new positive sources plus the
preserved `inherited-method-override` (21) add **40 native executions**. Distinct
leaf targets in a shared method still refuse. Nine new sources were added; all
185 previous split-file sections remain unchanged.

**5019e45f** proves bounded `i / divisor` overwrites for exact positive Number
divisors with divisible starts and strides. Each write range keeps its actual
quotient stride; recursive guard reloads must miss every write. Saved children,
remaining aliases, historical cycles and work limits remain. Added CFG/SCF controls
and 17 source functions.

Focused host **1/1 (0.48s; 0.49s total)**; class initialization/DOM lit **2/2
(313.53s)**: **187 source observations / 492 main native executions**, with
374 unprepared and 221 preparation refusals. DOM remains **632 observations /
eight executions / 4,910 refusals**. Arrays **1/1 (1.28s; 1.29s total)** and
quotient/offset/visited escape lit **3/3 (0.11s)** pass. New oracle: **51 sites /
six sound / 6 of 9 confined precision**, zero violations, partial, pending or
unclaimed sites. Seven tested hashes match; changed formatting passes. Required
formatter retains 26 baseline diagnostics in nine unchanged files.

**Next:** preserve B's ordinary `this._getConfig(t)` call after its own `super()`
initialization. The original 118-result `inherited-dispatch` now stops there with
`super initialization contains an unproved call`, before reaching Qi's method.
Then normalize Qi's `LoadHome -> GetProto -> GetProperty -> Call` using its immutable
method home and immediate base, while retaining Qi as receiver. Complete authentic
W/B/Qi still needs captured constructor helpers, ordinary static methods,
receiver-selected getters, complete H/r/s bodies, configuration, selectors,
events and Popper. Own-data provenance, broader escape control flow/ownership and
the application driver remain. Full suites/broad matrices/whole Bootstrap were
skipped; no browser/Script changes or new compliance measurements. Exact commands,
initial failures and resume provenance: HANDOFF and `/tmp/ctcompile-overrides-1456/`.

Focused devbox commands, all under `/tmp/ctbrowser-devbox-build.lock`:

- `tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-host-contract
  ctcompile-test-native-reference`.
- `ctest --test-dir projects/compile-time-browser/build --output-on-failure
  --no-tests=error -R '^ctcompile_host_contract$'`.
- `~/.lit-venv/bin/lit -sva -j2 projects/compile-time-browser/build/ctcompile/test
  --filter='^ctcompile :: CTNative/Lowering/Objects/class-(initialization|dom)[.]mlir$'`.
- `tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-escape-analysis-arrays
  ctcompile-test-escape-claims ctcompile-test-type-oracle`.
- The same CTest command with `-R '^ctcompile_escape_analysis_arrays$'`.
- `~/.lit-venv/bin/lit -sva projects/compile-time-browser/build/ctcompile/test
  --filter='^ctcompile :: Analysis/Escape/escape-claims/(quotient-index-overwrite|offset-index-overwrite|visited-index-overwrite)[.]test$'`.

The corrected narrow native probe measured **32 observations / 112 executions /
64 unprepared and 30 preparation refusals**, plus 24 existing executions/24
refusals and two prepared refusals. The final initialization case additionally
measured 28 executions/30 refusals and 13 prepared refusals. Existing escape
oracles retain **48 sites / seven sound / 7 of 13 precision** and **31 / six /
6 of 7**, all with zero violations, partial, pending and unclaimed sites.

Initial native probes exposed the new fixtures missing the existing generic-IR
printing workaround for `cf.switch`, then unobserved shadowed closures blocking
admission. The first cleanup draft invalidated the module walk; the final code
collects candidates without mutation, erases all closures, then module-level
bodies. The final narrow probe and focused class/DOM gate passed afterwards.
Escape build/tests passed on the first run. Generated middle-override C++ was
inspected: stack object, borrowed receiver and direct selected-method calls;
no Script/VM or prototype storage. Evidence includes all failed/passing logs,
commands, hashes and `override-middle.cpp` in `/tmp/ctcompile-overrides-1456/`.

Required `tools/format.sh --check` stops in the C++ phase with the baseline
26 diagnostics in nine HEAD-identical files. Changed C++/Python formatting,
Python syntax, temporary shell-script syntax, source JavaScript checks and
`git diff --check` pass. Full CTest/compiler lit, standalone transaction/lifetime,
broad native/corpus matrices, WPT/test262 and whole Bootstrap were skipped.
Focused passes are not full-suite results. No browser/runtime edits or push.
Removed only this session's temporary SSH authorized entry and local keypair;
other keys were preserved.

For the next constructor slice, retain only proved same-receiver ordinary calls
after initialization phase 4, preserving argument/effect order. Let the subsequent
`fieldsOnly` proof record the cloned calls; recording original operations before
`takeBody` creates stale pointers. Keep pre-super/foreign/dynamic calls, method
replacement, new.target, declarations/global effects and budget failures refused.
The original source chain is B constructor -> Qi override -> lexical B method ->
ordinary W merge -> Qi constructor.Default. Standalone super-property reads use
`__ctbrowser_super_get`; the called-method IR above is a different shape.

## Detailed sections

The following sections retain the complete original text and measurements.

<a id="inherited-method-targets-and-bounded-offset-overwrites-2026-09-19-utc"></a>
- [Inherited method targets and bounded offset overwrites, 2026-09-19 UTC](handoff/01-inherited-method-targets-and-bounded-offset-overwrites-2026-09-19-utc.md#inherited-method-targets-and-bounded-offset-overwrites-2026-09-19-utc)
<a id="explicit-super-construction-and-visited-index-reloads-2026-09-19-utc"></a>
- [Explicit super construction and visited-index reloads, 2026-09-19 UTC](handoff/01-inherited-method-targets-and-bounded-offset-overwrites-2026-09-19-utc.md#explicit-super-construction-and-visited-index-reloads-2026-09-19-utc)
<a id="ordered-class-ancestry-and-disjoint-overwrite-reloads-2026-09-19-utc"></a>
- [Ordered class ancestry and disjoint overwrite reloads, 2026-09-19 UTC](handoff/01-inherited-method-targets-and-bounded-offset-overwrites-2026-09-19-utc.md#ordered-class-ancestry-and-disjoint-overwrite-reloads-2026-09-19-utc)
<a id="inheritance-helper-declarations-and-invariant-overwrites-2026-09-19-utc"></a>
- [Inheritance helper declarations and invariant overwrites, 2026-09-19 UTC](handoff/01-inherited-method-targets-and-bounded-offset-overwrites-2026-09-19-utc.md#inheritance-helper-declarations-and-invariant-overwrites-2026-09-19-utc)
<a id="aliased-overwrite-receivers-and-inheritance-boundary-2026-09-19-utc"></a>
- [Aliased overwrite receivers and inheritance boundary, 2026-09-19 UTC](handoff/01-inherited-method-targets-and-bounded-offset-overwrites-2026-09-19-utc.md#aliased-overwrite-receivers-and-inheritance-boundary-2026-09-19-utc)
<a id="constructor-origin-dom-calls-2026-09-19-utc"></a>
- [Constructor-origin DOM calls, 2026-09-19 UTC](handoff/01-inherited-method-targets-and-bounded-offset-overwrites-2026-09-19-utc.md#constructor-origin-dom-calls-2026-09-19-utc)
<a id="actual-h-callers-and-guarded-array-overwrites-2026-09-19-utc"></a>
- [Actual H callers and guarded array overwrites, 2026-09-19 UTC](handoff/01-inherited-method-targets-and-bounded-offset-overwrites-2026-09-19-utc.md#actual-h-callers-and-guarded-array-overwrites-2026-09-19-utc)
<a id="confined-unused-local-cells-2026-09-19-utc"></a>
- [Confined unused local cells, 2026-09-19 UTC](handoff/01-inherited-method-targets-and-bounded-offset-overwrites-2026-09-19-utc.md#confined-unused-local-cells-2026-09-19-utc)
<a id="unused-conditional-and-early-return-bodies-2026-09-19-utc"></a>
- [Unused conditional and early-return bodies, 2026-09-19 UTC](handoff/01-inherited-method-targets-and-bounded-offset-overwrites-2026-09-19-utc.md#unused-conditional-and-early-return-bodies-2026-09-19-utc)
<a id="dynamic-bootstrap-f-and-saved-digit-keys-2026-09-19-utc"></a>
- [Dynamic Bootstrap F and saved digit keys, 2026-09-19 UTC](handoff/01-inherited-method-targets-and-bounded-offset-overwrites-2026-09-19-utc.md#dynamic-bootstrap-f-and-saved-digit-keys-2026-09-19-utc)
<a id="known-matching-f-inputs-and-ascii-string-indices-2026-09-19-utc"></a>
- [Known matching F inputs and ASCII String indices, 2026-09-19 UTC](handoff/02-known-matching-f-inputs-and-ascii-string-indices-2026-09-19-utc.md#known-matching-f-inputs-and-ascii-string-indices-2026-09-19-utc)
<a id="inert-unused-bodies-and-ascii-string-lengths-2026-09-19-utc"></a>
- [Inert unused bodies and ASCII String lengths, 2026-09-19 UTC](handoff/02-known-matching-f-inputs-and-ascii-string-indices-2026-09-19-utc.md#inert-unused-bodies-and-ascii-string-lengths-2026-09-19-utc)
<a id="full-h-instance-methods-and-object-reload-prerequisite-2026-09-19-utc"></a>
- [Full H instance methods and object-reload prerequisite, 2026-09-19 UTC](handoff/02-known-matching-f-inputs-and-ascii-string-indices-2026-09-19-utc.md#full-h-instance-methods-and-object-reload-prerequisite-2026-09-19-utc)
<a id="full-h-entry-calls-and-invariant-array-lengths-2026-09-19-utc"></a>
- [Full H entry calls and invariant array lengths, 2026-09-19 UTC](handoff/02-known-matching-f-inputs-and-ascii-string-indices-2026-09-19-utc.md#full-h-entry-calls-and-invariant-array-lengths-2026-09-19-utc)
<a id="original-h-key-normalization-and-invariant-array-reloads-2026-09-19-utc"></a>
- [Original H key normalization and invariant array reloads, 2026-09-19 UTC](handoff/02-known-matching-f-inputs-and-ascii-string-indices-2026-09-19-utc.md#original-h-key-normalization-and-invariant-array-reloads-2026-09-19-utc)
<a id="repeated-helpers-across-entry-exits-and-budgeted-invariant-depth-2026-09-19-utc"></a>
- [Repeated helpers across entry exits and budgeted invariant depth, 2026-09-19 UTC](handoff/02-known-matching-f-inputs-and-ascii-string-indices-2026-09-19-utc.md#repeated-helpers-across-entry-exits-and-budgeted-invariant-depth-2026-09-19-utc)
<a id="conditional-dataset-iterators-and-invariant-bitwise-latches-2026-09-19-utc"></a>
- [Conditional dataset iterators and invariant bitwise latches, 2026-09-19 UTC](handoff/02-known-matching-f-inputs-and-ascii-string-indices-2026-09-19-utc.md#conditional-dataset-iterators-and-invariant-bitwise-latches-2026-09-19-utc)
<a id="sequential-dataset-iterators-and-invariant-addsub-2026-09-19-utc"></a>
- [Sequential dataset iterators and invariant Add/Sub, 2026-09-19 UTC](handoff/02-known-matching-f-inputs-and-ascii-string-indices-2026-09-19-utc.md#sequential-dataset-iterators-and-invariant-addsub-2026-09-19-utc)
<a id="direct-dataset-helpers-and-nested-invariant-latches-2026-09-19-utc"></a>
- [Direct dataset helpers and nested invariant latches, 2026-09-19 UTC](handoff/02-known-matching-f-inputs-and-ascii-string-indices-2026-09-19-utc.md#direct-dataset-helpers-and-nested-invariant-latches-2026-09-19-utc)
<a id="class-dataset-loops-and-invariant-powers-2026-09-19-utc"></a>
- [Class dataset loops and invariant powers, 2026-09-19 UTC](handoff/02-known-matching-f-inputs-and-ascii-string-indices-2026-09-19-utc.md#class-dataset-loops-and-invariant-powers-2026-09-19-utc)
<a id="confined-class-callbacks-and-invariant-division-2026-09-19-utc"></a>
- [Confined class callbacks and invariant division, 2026-09-19 UTC](handoff/03-confined-class-callbacks-and-invariant-division-2026-09-19-utc.md#confined-class-callbacks-and-invariant-division-2026-09-19-utc)
<a id="shared-utf-16-indexing-and-invariant-products-2026-09-19-utc"></a>
- [Shared UTF-16 indexing and invariant products, 2026-09-19 UTC](handoff/03-confined-class-callbacks-and-invariant-division-2026-09-19-utc.md#shared-utf-16-indexing-and-invariant-products-2026-09-19-utc)
<a id="dataset-loops-beside-native-class-construction-2026-09-19-utc"></a>
- [Dataset loops beside native class construction, 2026-09-19 UTC](handoff/03-confined-class-callbacks-and-invariant-division-2026-09-19-utc.md#dataset-loops-beside-native-class-construction-2026-09-19-utc)
<a id="combined-helper-captures-and-literal-bitnot-latches-2026-09-18-utc"></a>
- [Combined helper captures and literal BitNot latches, 2026-09-18 UTC](handoff/03-confined-class-callbacks-and-invariant-division-2026-09-19-utc.md#combined-helper-captures-and-literal-bitnot-latches-2026-09-18-utc)
<a id="captured-h-objects-and-literal-unary-latches-2026-09-18-utc"></a>
- [Captured H objects and literal unary latches, 2026-09-18 UTC](handoff/03-confined-class-callbacks-and-invariant-division-2026-09-19-utc.md#captured-h-objects-and-literal-unary-latches-2026-09-18-utc)
<a id="captured-local-h-helpers-and-boolean-add-latches-2026-09-18-utc"></a>
- [Captured local H helpers and Boolean Add latches, 2026-09-18 UTC](handoff/03-confined-class-callbacks-and-invariant-division-2026-09-19-utc.md#captured-local-h-helpers-and-boolean-add-latches-2026-09-18-utc)
<a id="local-callable-holders-and-negative-string-latches-2026-09-18-utc"></a>
- [Local callable holders and negative String latches, 2026-09-18 UTC](handoff/03-confined-class-callbacks-and-invariant-division-2026-09-19-utc.md#local-callable-holders-and-negative-string-latches-2026-09-18-utc)
<a id="captured-dataset-filter-callbacks-and-negative-strings-2026-09-18-utc"></a>
- [Captured dataset-filter callbacks and negative Strings, 2026-09-18 UTC](handoff/03-confined-class-callbacks-and-invariant-division-2026-09-19-utc.md#captured-dataset-filter-callbacks-and-negative-strings-2026-09-18-utc)
<a id="captured-bootstrap-f-replacement-proof-2026-09-18-utc"></a>
- [Captured Bootstrap F replacement proof, 2026-09-18 UTC](handoff/03-confined-class-callbacks-and-invariant-division-2026-09-19-utc.md#captured-bootstrap-f-replacement-proof-2026-09-18-utc)
<a id="captured-sibling-bootstrap-m-2026-09-18-utc"></a>
- [Captured sibling Bootstrap M, 2026-09-18 UTC](handoff/03-confined-class-callbacks-and-invariant-division-2026-09-19-utc.md#captured-sibling-bootstrap-m-2026-09-18-utc)
<a id="entry-local-bootstrap-m-and-primitive-addition-2026-09-18-utc"></a>
- [Entry-local Bootstrap M and primitive addition, 2026-09-18 UTC](handoff/04-entry-local-bootstrap-m-and-primitive-addition-2026-09-18-utc.md#entry-local-bootstrap-m-and-primitive-addition-2026-09-18-utc)
<a id="mixed-classdom-intrinsics-and-primitive-subtraction-2026-09-18-utc"></a>
- [Mixed class/DOM intrinsics and primitive subtraction, 2026-09-18 UTC](handoff/04-entry-local-bootstrap-m-and-primitive-addition-2026-09-18-utc.md#mixed-classdom-intrinsics-and-primitive-subtraction-2026-09-18-utc)
<a id="declared-error-classdom-composition-and-primitive-powers-2026-09-18-utc"></a>
- [Declared Error class/DOM composition and primitive powers, 2026-09-18 UTC](handoff/04-entry-local-bootstrap-m-and-primitive-addition-2026-09-18-utc.md#declared-error-classdom-composition-and-primitive-powers-2026-09-18-utc)
<a id="config-defaults-and-primitive-bitwise-snapshots-2026-09-18-utc"></a>
- [Config defaults and primitive bitwise snapshots, 2026-09-18 UTC](handoff/04-entry-local-bootstrap-m-and-primitive-addition-2026-09-18-utc.md#config-defaults-and-primitive-bitwise-snapshots-2026-09-18-utc)
<a id="transitive-method-arguments-and-primitive-division-2026-09-18-utc"></a>
- [Transitive method arguments and primitive division, 2026-09-18 UTC](handoff/04-entry-local-bootstrap-m-and-primitive-addition-2026-09-18-utc.md#transitive-method-arguments-and-primitive-division-2026-09-18-utc)
<a id="original-class-method-arguments-and-primitive-products-2026-09-18-utc"></a>
- [Original class-method arguments and primitive products, 2026-09-18 UTC](handoff/04-entry-local-bootstrap-m-and-primitive-addition-2026-09-18-utc.md#original-class-method-arguments-and-primitive-products-2026-09-18-utc)
<a id="constructor-stored-dom-methods-and-primitive-unary-snapshots-2026-09-18-utc"></a>
- [Constructor-stored DOM methods and primitive unary snapshots, 2026-09-18 UTC](handoff/04-entry-local-bootstrap-m-and-primitive-addition-2026-09-18-utc.md#constructor-stored-dom-methods-and-primitive-unary-snapshots-2026-09-18-utc)
<a id="captured-local-class-getters-2026-09-18-utc"></a>
- [Captured local class getters, 2026-09-18 UTC](handoff/04-entry-local-bootstrap-m-and-primitive-addition-2026-09-18-utc.md#captured-local-class-getters-2026-09-18-utc)
<a id="original-classdom-composition-and-string-bitnot-2026-09-18-utc"></a>
- [Original class/DOM composition and String BitNot, 2026-09-18 UTC](handoff/04-entry-local-bootstrap-m-and-primitive-addition-2026-09-18-utc.md#original-classdom-composition-and-string-bitnot-2026-09-18-utc)
<a id="confined-dom-fields-and-canonical-string-powers-2026-09-18-utc"></a>
- [Confined DOM fields and canonical String powers, 2026-09-18 UTC](handoff/04-entry-local-bootstrap-m-and-primitive-addition-2026-09-18-utc.md#confined-dom-fields-and-canonical-string-powers-2026-09-18-utc)
<a id="direct-dom-receivers-and-canonical-string-bitwise-snapshots-2026-09-18-utc"></a>
- [Direct DOM receivers and canonical String bitwise snapshots, 2026-09-18 UTC](handoff/04-entry-local-bootstrap-m-and-primitive-addition-2026-09-18-utc.md#direct-dom-receivers-and-canonical-string-bitwise-snapshots-2026-09-18-utc)
<a id="explicit-class-entries-and-canonical-string-division-2026-09-18-utc"></a>
- [Explicit class entries and canonical String division, 2026-09-18 UTC](handoff/05-explicit-class-entries-and-canonical-string-division-2026-09-18-utc.md#explicit-class-entries-and-canonical-string-division-2026-09-18-utc)
<a id="shared-dom-preparation-and-canonical-string-products-2026-09-18-utc"></a>
- [Shared DOM preparation and canonical String products, 2026-09-18 UTC](handoff/05-explicit-class-entries-and-canonical-string-division-2026-09-18-utc.md#shared-dom-preparation-and-canonical-string-products-2026-09-18-utc)
<a id="native-global-helpers-and-canonical-string-unary-snapshots-2026-09-18-utc"></a>
- [Native global helpers and canonical String unary snapshots, 2026-09-18 UTC](handoff/05-explicit-class-entries-and-canonical-string-division-2026-09-18-utc.md#native-global-helpers-and-canonical-string-unary-snapshots-2026-09-18-utc)
<a id="recovered-global-holders-and-string-left-subtraction-2026-09-18-utc"></a>
- [Recovered global holders and String-left subtraction, 2026-09-18 UTC](handoff/05-explicit-class-entries-and-canonical-string-division-2026-09-18-utc.md#recovered-global-holders-and-string-left-subtraction-2026-09-18-utc)
<a id="local-callable-holders-and-signed-string-subtraction-2026-09-18-utc"></a>
- [Local callable holders and signed String subtraction, 2026-09-18 UTC](handoff/05-explicit-class-entries-and-canonical-string-division-2026-09-18-utc.md#local-callable-holders-and-signed-string-subtraction-2026-09-18-utc)
<a id="original-class-method-r-and-signed-unary-literals-2026-09-18-utc"></a>
- [Original class-method r and signed unary literals, 2026-09-18 UTC](handoff/05-explicit-class-entries-and-canonical-string-division-2026-09-18-utc.md#original-class-method-r-and-signed-unary-literals-2026-09-18-utc)
<a id="closed-scalar-guards-and-original-bootstrap-helpers-2026-09-18-utc"></a>
- [Closed scalar guards and original Bootstrap helpers, 2026-09-18 UTC](handoff/05-explicit-class-entries-and-canonical-string-division-2026-09-18-utc.md#closed-scalar-guards-and-original-bootstrap-helpers-2026-09-18-utc)
<a id="recovered-declaration-borrows-and-bounded-powers-2026-09-18-utc"></a>
- [Recovered declaration borrows and bounded powers, 2026-09-18 UTC](handoff/05-explicit-class-entries-and-canonical-string-division-2026-09-18-utc.md#recovered-declaration-borrows-and-bounded-powers-2026-09-18-utc)
<a id="local-helper-proofs-and-power-identities-2026-09-18-utc"></a>
- [Local helper proofs and power identities, 2026-09-18 UTC](handoff/05-explicit-class-entries-and-canonical-string-division-2026-09-18-utc.md#local-helper-proofs-and-power-identities-2026-09-18-utc)
<a id="getter-cleanup-and-signed-right-shifts-2026-09-18-utc"></a>
- [Getter cleanup and signed right shifts, 2026-09-18 UTC](handoff/05-explicit-class-entries-and-canonical-string-division-2026-09-18-utc.md#getter-cleanup-and-signed-right-shifts-2026-09-18-utc)
<a id="recovered-error-getters-and-signed-bitwise-snapshots-2026-09-18-utc"></a>
- [Recovered Error getters and signed bitwise snapshots, 2026-09-18 UTC](handoff/05-explicit-class-entries-and-canonical-string-division-2026-09-18-utc.md#recovered-error-getters-and-signed-bitwise-snapshots-2026-09-18-utc)
<a id="recovered-literal-throws-and-signed-bitnot-2026-09-18-utc"></a>
- [Recovered literal throws and signed BitNot, 2026-09-18 UTC](handoff/05-explicit-class-entries-and-canonical-string-division-2026-09-18-utc.md#recovered-literal-throws-and-signed-bitnot-2026-09-18-utc)
<a id="local-constructor-getter-reads-2026-09-18-utc"></a>
- [Local constructor getter reads, 2026-09-18 UTC](handoff/06-local-constructor-getter-reads-2026-09-18-utc.md#local-constructor-getter-reads-2026-09-18-utc)
<a id="recovered-method-counters-and-signed-subtraction-2026-09-18-utc"></a>
- [Recovered method counters and signed subtraction, 2026-09-18 UTC](handoff/06-local-constructor-getter-reads-2026-09-18-utc.md#recovered-method-counters-and-signed-subtraction-2026-09-18-utc)
<a id="recovered-method-dispatch-continuation-2026-09-18-utc"></a>
- [Recovered method dispatch continuation, 2026-09-18 UTC](handoff/06-local-constructor-getter-reads-2026-09-18-utc.md#recovered-method-dispatch-continuation-2026-09-18-utc)
<a id="structured-class-methods-and-signed-division-2026-09-18-utc"></a>
- [Structured class methods and signed division, 2026-09-18 UTC](handoff/06-local-constructor-getter-reads-2026-09-18-utc.md#structured-class-methods-and-signed-division-2026-09-18-utc)
<a id="fresh-config-defaults-and-signed-number-products-2026-09-17-utc"></a>
- [Fresh Config defaults and signed Number products, 2026-09-17 UTC](handoff/06-local-constructor-getter-reads-2026-09-18-utc.md#fresh-config-defaults-and-signed-number-products-2026-09-17-utc)
<a id="filtered-prefix-assignments-and-number-cancellation-2026-09-17-utc"></a>
- [Filtered prefix assignments and Number cancellation, 2026-09-17 UTC](handoff/06-local-constructor-getter-reads-2026-09-18-utc.md#filtered-prefix-assignments-and-number-cancellation-2026-09-17-utc)
<a id="ordered-native-result-assignments-and-negative-sub-snapshots-2026-09-17-utc"></a>
- [Ordered native result assignments and negative Sub snapshots, 2026-09-17 UTC](handoff/06-local-constructor-getter-reads-2026-09-18-utc.md#ordered-native-result-assignments-and-negative-sub-snapshots-2026-09-17-utc)
<a id="validated-element-guards-and-nested-helper-calls-2026-09-17-utc"></a>
- [Validated element guards and nested helper calls, 2026-09-17 UTC](handoff/06-local-constructor-getter-reads-2026-09-18-utc.md#validated-element-guards-and-nested-helper-calls-2026-09-17-utc)
<a id="present-dataset-values-and-signed-unary-snapshots-2026-09-17-utc"></a>
- [Present dataset values and signed unary snapshots, 2026-09-17 UTC](handoff/06-local-constructor-getter-reads-2026-09-18-utc.md#present-dataset-values-and-signed-unary-snapshots-2026-09-17-utc)
<a id="integrated-runtime-recovery-complete-2026-09-17-utc"></a>
- [Integrated-runtime recovery complete, 2026-09-17 UTC](handoff/06-local-constructor-getter-reads-2026-09-18-utc.md#integrated-runtime-recovery-complete-2026-09-17-utc)
<a id="original-snapshot-iteration-and-scalar-loop-completion-2026-09-16-utc"></a>
- [Original snapshot iteration and scalar loop completion, 2026-09-16 UTC](handoff/06-local-constructor-getter-reads-2026-09-18-utc.md#original-snapshot-iteration-and-scalar-loop-completion-2026-09-16-utc)
<a id="owning-snapshot-length-and-negative-number-strides-2026-09-16-utc"></a>
- [Owning snapshot length and negative Number strides, 2026-09-16 UTC](handoff/06-local-constructor-getter-reads-2026-09-18-utc.md#owning-snapshot-length-and-negative-number-strides-2026-09-16-utc)
<a id="original-dataset-filter-and-moved-vm-recovery-2026-09-16-utc"></a>
- [Original dataset filter and moved-VM recovery, 2026-09-16 UTC](handoff/07-original-dataset-filter-and-moved-vm-recovery-2026-09-16-utc.md#original-dataset-filter-and-moved-vm-recovery-2026-09-16-utc)
<a id="dataset-key-snapshots-and-dynamic-add-induction-2026-09-16-utc"></a>
- [Dataset key snapshots and dynamic Add induction, 2026-09-16 UTC](handoff/07-original-dataset-filter-and-moved-vm-recovery-2026-09-16-utc.md#dataset-key-snapshots-and-dynamic-add-induction-2026-09-16-utc)
<a id="guarded-config-spreads-and-recovered-escape-work-2026-09-16-utc"></a>
- [Guarded Config spreads and recovered escape work, 2026-09-16 UTC](handoff/07-original-dataset-filter-and-moved-vm-recovery-2026-09-16-utc.md#guarded-config-spreads-and-recovered-escape-work-2026-09-16-utc)
<a id="config-json-tags-and-reversed-array-guards-2026-09-16-utc"></a>
- [Config JSON tags and reversed array guards, 2026-09-16 UTC](handoff/07-original-dataset-filter-and-moved-vm-recovery-2026-09-16-utc.md#config-json-tags-and-reversed-array-guards-2026-09-16-utc)
<a id="original-bootstrap-attribute-normalization-2026-09-16-utc"></a>
- [Original Bootstrap attribute normalization, 2026-09-16 UTC](handoff/07-original-dataset-filter-and-moved-vm-recovery-2026-09-16-utc.md#original-bootstrap-attribute-normalization-2026-09-16-utc)
<a id="native-json-chain-recovered-and-gated-2026-09-16-utc"></a>
- [Native JSON chain recovered and gated, 2026-09-16 UTC](handoff/07-original-dataset-filter-and-moved-vm-recovery-2026-09-16-utc.md#native-json-chain-recovered-and-gated-2026-09-16-utc)
<a id="helperuri-composition-the-json-chain-draft-and-the-audit-2026-09-15-utc"></a>
- [Helper/URI composition, the JSON chain draft and the audit, 2026-09-15 UTC](handoff/07-original-dataset-filter-and-moved-vm-recovery-2026-09-16-utc.md#helperuri-composition-the-json-chain-draft-and-the-audit-2026-09-15-utc)
<a id="saved-nullable-uri-guards-and-fingerprinting-2026-09-15-utc"></a>
- [Saved nullable URI guards and fingerprinting, 2026-09-15 UTC](handoff/07-original-dataset-filter-and-moved-vm-recovery-2026-09-16-utc.md#saved-nullable-uri-guards-and-fingerprinting-2026-09-15-utc)
<a id="earlier-measurements"></a>
- [Earlier measurements](handoff/07-original-dataset-filter-and-moved-vm-recovery-2026-09-16-utc.md#earlier-measurements)
