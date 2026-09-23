# Terminal iterator-close read values and correlated powers, 2026-09-23

Resumed clean **dde07a16** and retained source `b1b67287` from HANDOFF,
current00, the terminal-writes-varying-divisors detail and iteration 106's final
journal. No dirty drafts or unmerged `codex-wip-20260907` remained; unrelated
branches were preserved. Source, raw-host and escape workflows ran in parallel.
Source/raw agents hit rate limits; the parent completed their work. The escape
agent completed its change and independently reviewed the native diff with no
actionable findings. The parent reviewed and gated both changes.

**0024c099** retains a `hasAttribute` read inside a pending final `setAttribute`
after terminal selectors. It reuses the existing suffix read-to-write proof:
every operand use, typed DOM call, literal name/selector, source guard/order,
private callable/frame and proof budget still needs validation. Selectors cannot
feed this pending write, and previously discarded read values remain unobserved.
The original saved body exception still wins. No platform copy or runtime fallback
was added. Raw tests cover nested/alternating reads, direct calls, typed refusals,
read identity/order/count, extra uses and exact/one-less expansion budgets.

The original source is unchanged, SHA-256
`b1b672877691164ce46b37d674f4adba1dc17c7c93543fcf9cbabaf85992212f`.
Getter `4b0bf742` and reordered-read `3202e6ce` also execute. The latter changes
the final Boolean from false to true, checking which read feeds the write.

**81a3673d** enables bounded whole-key subdivision for correlated varying power
operands. Every accepted subrange still needs the existing exact scalar transfer;
general powers remain unproved. For example, `i ** (2-i)` for `i=0,1,2` visits
only `0,1,1`. Complete reload/store checks, independent reload-gap proof, restored
index bounds, actual-write replay and budgets remain. Eleven raw and ten source
controls were added. All 245 historical source bodies are unchanged. Two raw
nonsingleton-exponent refusals and source CHECK109 now admit after exact Node
writes `0,1,2`, reads `0,0,0`, own-key and child-identity witnesses.

Focused validation on the devbox:

- Exact `ctcompile_host_contract`: **1/1 PASS, 2.46 s test / 2.47 s total**.
- Three selected saved-throw sources: **48 native executions, 76 refusals and
  24 Node/VM observations PASS**, both providers/policies, GCC/Clang and
  explicit/deduced C++. Preflight admits three positives and refuses ten controls
  under both policies. The selector executes the committed fixture.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS, 2.52 s / 2.53 s total**.
- Exact `Analysis/Escape/escape-claims/power-index-overwrite.test`:
  **1/1 PASS, 0.17 s**, with 355 other lit cases excluded.
- Final `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**.
  Scoped formatting, Python/Node syntax, scratch `bash -n` and diff checks pass.
- Seven final code/test hashes match the devbox. All 24 generated C++ files retain
  both selectors, ordered terminal reads, the final read Boolean feeding its
  write, and the saved body exception. No Script/VM or nullable fallback appears;
  fixture standalone and linked checks pass.
- Preserved 233 historical iterator bodies, 86 metadata rows, 69 saved cases,
  131 retained saved refusal bodies and 69 historical oracle constructions.
  Local Node checks cover 12 positive and 20 refusal observations. Historical
  oracle construction was compared without replay. Eleven focused escape Node
  outputs/own-key/identity checks and all 255 source syntax checks pass.

The initial arrays gate found two new SCF expectation-label mismatches: an
unlabeled computed two is printed as `ctjs.binary`. Corrected only those new
expectations; production and fixture bodies were unchanged. An initial formatter
ran during raw fixture editing; final formatting passes. A scratch artifact check
was corrected to allow an unused read emitted as `(void)`; all final checks pass.

Builds used `tools/remote-build.sh` with explicit targets under the shared devbox
lock. The host gate built `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-host-contract` and `ctcompile-test-native-reference`; the escape
gate built `ctjs-opt`, `ctcompile-test-escape-analysis-arrays`,
`ctcompile-test-escape-claims` and `ctcompile-test-type-oracle`. Commands, selectors,
witnesses, artifacts and checksums are in
`../test-results/2026-09-23-terminal-read-values/` relative to the monorepo.

Initial/final availability checks inspected 14/21 accessible Linux processes
and 344/347 Windows processes. No actual executable/CLI/loop matches appeared,
but 61 Linux executable-link permission errors left Claude status uncertain.
Concurrent-agent area rules were observed; no browser/shared implementation
edits, local C++ builds, pushes or history rewrites occurred.

Full CTest/compiler lit, complete iterator fixtures, unaffected native replays,
broad corpus/matrices, full Bootstrap, browser/WPT/test262, Windows and sanitizers
were skipped. No full-Bootstrap coverage gain is claimed.

**Next:** retained `unsupported-terminal-read-write-earlier-value`, SHA-256
`378326b6423bc5f439e4fd413d8adfa54606c7e1bb2a1cd3aea37ee955e8909e`,
refuses **DOM protected helper needs an independent inert-body proof** under both
policies. It saves `hasAttribute('data-closed')`, performs an ignored
`hasAttribute('data-unvisited')`, then writes the saved Boolean to
`data-after-terminal`. Preserve that earlier value, call order and original body
exception. Single-write selector cleanup, nonterminal exceptional state, multiple
protected regions, implicit cleanup, nested custom iterators, unguarded Bootstrap
defaults, the application driver, full native Bootstrap and general powers remain.
