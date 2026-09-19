[Back to escape-load-evidence.md](../escape-load-evidence.md)

## Mixed primitive BigInt Mod retention, 2026-09-10

**4680d90e** continues the completed **c20f43a4** Div boundary. Dynamic Mod
now accepts exactly one independently proved original BigInt and one primitive
non-BigInt origin through the existing mixed-error retention proof. In the
current interpreter, `binary_op` reaches `bigint_binary` before Number
conversion; the mixed-category TypeError is created before digit operations or
object hooks. Its independent Undefined carrier carries no local object edge.
Every structural continuation still needs the complete retention proof. This
adds no native BigInt carrier, successful-completion or effect permission.

The same original-category matrix now runs for Mul, Div and Mod. Each checks
**57 rows, 43 stale/fresh live states and 3451 retention budget cutoffs**, plus
one wide snapshot. Unknown/object operands, saved children, publication before
or after the error, calls, handlers, forged annotations and incomplete budgets
retain their independent controls. Historical comparison/Mod sources now prove
retention independently; separate Pow controls still refuse. The first focused
run reported **235 failed assertions** because three older test predicates
still omitted Mod. Updating those predicates preserves their source bodies and
retains the independently reached UnknownIndex refusal when a live Number index
is changed to BigInt. No production correction or source rewriting was needed.

The exact probe **12ad522f** (`/tmp/ctcompile-after-mixed-div-boundary.js`)
records Number **63** under Node and passes its identical interpreter assertions:
three normal returns, three distinct TypeErrors, deleted fields and the saved
child are all observed. Seven separate Node mutations distinguish arithmetic,
each deletion, saved value, operand category, missing Error and Error identity.
All **92,753 preexisting fixture bytes** are preserved, and all **40 source-body
pins** pass. Before the change, its twelve literal claims were conservative.
Afterward, only the early function's child at **pc5** changes from Stored to
confined; the retained and opaque controls keep their claims. The interpreter
records **12 literal sites, 21 instances, eleven confined and ten retained**, plus
three independent Errors at **pcs25, 29 and 18**, each without a literal claim.

The initial **14-test** focused gate passes **13/14 in 231.24 seconds**,
including seven escape tests. The corrected arrays rerun passes **1/1 in
0.58 seconds**, completing all eight focused escape checks across these two
runs. The strict fixture reports **737 claims, 760 observed sites, 24 unclaimed,
101/139 precision and zero soundness violations, partial or pending claims**.
All four actual corpus oracles pass. The complete run at **2172add2** confirms
these counts and all four mixed Sub/Mul/Div/Mod matrices at **57 rows, 43 live
states and 3451 cutoffs**. All corpus oracles have zero violations and pending
claims; p5 retains its historical **one partial claim**. The full monorepo run
passes **522/523 CTests in 2228.37 seconds**, including **151/151 browser tests**.
Its only failure is the historical `parameter_object` native-admission test
expectation. Test-only correction **0995a758** then passes the affected CTest
**1/1**, with **168/168 lit cases in 1499.87 seconds**. All 523 tests are covered
across the two runs; this is not a single green full run. The independent
composite audit is `/tmp/ctcompile-retained-composite-audit.json`.
The requested split is committed as **681bd899**: all twenty escape split files
match local/committed/frozen hashes, and the assembled fixture retains all
**95,603 bytes**, SHA-256 **c03c5607**. Raw evidence:
`/tmp/ctcompile-retained-full-detail.log` and
`/tmp/ctcompile-retained-initial-full-audit.json`.

The next measured boundary is mixed primitive BigInt **Pow**, exact source
**06f94afe** (`/tmp/ctcompile-after-mixed-mod-boundary.js`). Its `input ** 1`
driver again records Number **63** in Node and passes the interpreter's source
assertions. The twelve literal sites have **21 instances, eleven confined and
ten retained**, and three separate TypeErrors at **pcs25, 29 and 18**. All twelve
literal claims remain conservative; the Errors have no source allocation claims.
Continue with independent original categories and the existing whole-frame
retention checks. Observed opaque actuals grant no completion or effect proof.

Evidence: `/tmp/ctcompile-before-mod.{log,rec,claims}`,
`/tmp/ctcompile-before-mod-audit.json`, `/tmp/ctcompile-after-mod.claims`,
`/tmp/ctcompile-next-pow.{rec,claims}`,
`/tmp/ctcompile-mod-and-next-pow-audit.json`,
`/tmp/ctcompile-mixed-mod-{preservation,node-audit,frozen}.json` and
`/tmp/ctcompile-retained-{focused,probe}.log`.


The folder reorganization is committed: escape tests and fixture chunks now live
under `test/Analysis/Escape/` (**d75b4a51**), with the unchanged source assembly
and registered test names. The folder full gate passes **522/523**; its sole
native-suite timeout is recovered by the full **168/168** lit rerun in
**1509.23 seconds** at a 2,400-second limit. The final lowering-only moves
then pass all **67** affected lit cases. See [HANDOFF.md](../HANDOFF.md) for the
complete folder map and separate run evidence. No escape semantics change.

The filename cleanup is committed: **89fc5763**, **5a0f3cec** and **1c72b01d**
rename 193 tests and fixtures while preserving their source programs. The final
347-step rebuild passes; the user stopped the remaining full test run after
169 tests passed. See the handoff for the earlier affected gates and exact
source-preservation audit. This cleanup changes no escape proof.

Before extending the next primitive boundary, repair the incoming implicit
Object.prototype receiver-retention assumption. A 220-byte getter witness
(**441a8930**, program **a99e2d04fc92995e**, function 1 / PC 1 / obj) is claimed
Confined by both compiler builds. The current runtime observes confinement;
incoming **637e4a40** observes one object escaping through globals and the
checker reports one soundness violation. Property receiver sinks alone are
insufficient: preserve Iterable's Carry alongside exposure, retain both
provenance edges, and refuse ordinary-object contents writes without a valid
own-data/prototype proof. The setter/iteration follow-ups remain unexecuted.
Evidence: `/tmp/ctcompile-nd3-probe-results/summary.json`; repair scope:
`/tmp/ctcompile-nd3-repair-scope.md`. Current main retains the old runtime and
its existing ND-3 expectation; the incoming integration remains pending.
