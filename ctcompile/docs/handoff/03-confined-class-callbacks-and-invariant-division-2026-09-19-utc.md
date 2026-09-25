[Back to HANDOFF.md](../HANDOFF.md)

## Confined class callbacks and invariant division, 2026-09-19 UTC

Resumed clean **768a71c5** from the **03:18:30 AGENT-SYNC.jsonl closure**. Both agents'
commits and unmerged branches were reviewed; the September 7 WIP is already an
ancestor and the interrupted UTF-16 extraction was already landed. Three agents
supplied casing/alias analysis, fixture recommendations and a partial escape draft
before service limits; root completed, reviewed and gated the changes.

**6a1f3147** lets DOM class methods retain Bootstrap's original dataset
predicate when its callback saves lexical `this` but never reads it. The complete
original callback/callee/receiver census precedes clearing that inert operand.
Fixed method-local cells reuse the existing ordered identity proof; cell and
callback identities follow private method normalization. Constructor fields,
receiver aliases, actual parameters, early returns and repeated calls are covered.
Observed callback receivers, changing cells, escapes, invalid later inputs and unused
method effects still refuse. All prior fixture dictionary entries are unchanged;
three new strict-number-equality bodies and two unused borrowed-field bodies
are preserved as refusals.

**11f707a4** shares the existing bounded signed Div/Mod transfer with one invariant
quotient/remainder latch. Both operands must remain original literal or saved
primitive values; division additionally requires an integral quotient. Zero
divisors/strides, noncanonical/unknown/changing/nested operands, original property
keys and final-index bounds retain their checks. Ten older expressions retain their
source bodies with updated outcomes; 37 CFG/SCF rows and a nine-function source
oracle were added. Arrays **1/1 (1.15s; total 1.16s)** and three selected escape
oracles **3/3 (0.13s)** pass. New oracle: **28 observed sites / 6 sound / 6 of 18
precision**, zero violations, partial, pending or unclaimed sites.

Final native transaction **1/1 (4.40s)**, host contract **1/1 (0.46s; total
0.47s)** and class DOM **1/1 (107.33s)** pass. Class DOM checks **328 source
observations, 8 combined native executions and 2,416 refusals**. The 20 explicit
VM byte-indexing divergences remain pinned; native agrees with Node. Public class:
**1/1 (198.66s)**. Generated-source and binary Script/dispatch exclusions pass.
All **seven tested hashes** match the devbox. Required formatting retains 26
existing diagnostics in nine unchanged files; changed checks pass. Exact commands, retries and skipped coverage are below.
Evidence: `/tmp/ctcompile-class-alias/`.

**Next:** original H still needs Unicode-correct lowercase and normalized output-key
proof. Script's String casing is deliberately ASCII-only and no public Unicode
casing implementation exists; extracting the ASCII binding cannot meet that
contract. Keep the VM behavior unchanged. Lowercase collisions must preserve
insertion/overwrite order and the `__proto__` setter constraint; transformed keys
must never acquire dataset-membership authority. `class_dynamic_method` stops at
nonlocal/unordered method-cell uses,
`class_dynamic_capture` at an observable lifted closure, and
`class_dynamic_helper` at its unsupported `ctjs.call_direct`. Original H retains
the typed property/member refusal. Repeated iterators, unused H slots and global
publication remain. W still needs Object.entries/destructuring/original s/RegExp/
TypeError/spread before inheritance. One invariant Pow latch and broader part-25
work remain. Full H, W/W+r/W+r+H, native Bootstrap, the application driver and the
whole plan remain unfinished. No full-suite or whole-bundle admission gain is claimed.

Exact validation and limits:

- Native build targets: `ctjs-opt`, `ctjs-translate`,
  `ctcompile-test-native-reference`, `ctcompile-test-exception-recovery`,
  `ctcompile-test-host-contract`. Initial build: **7 actions**, including the
  escape production change. Receiver-rewrite and method-cell revisions rebuilt
  the affected compiler/recovery targets in **4 actions each**; fixture-only
  syncs rebuilt nothing. Exact CTests: `ctcompile_exception_recovery` and
  `ctcompile_host_contract`. Final-source timings are above.
- Native lit: `CTNative/Lowering/Objects/{class-dom,class-initialization}.mlir`
  through the generated `build/ctcompile/test` configuration. Public controls
  record 154 source observations, 380 native executions, 308 unprepared refusals
  and 190 preparation refusals; separate constructed-method/original-r/prototype
  controls also pass. The inspected new output uses a borrowed `element_ref`,
  ordinary vectors/strings/optionals and the existing dataset/filter helpers.
- Escape build: `ctjs-translate`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`: **5 actions**,
  then **2** after fixture corrections. Exact CTest:
  `ctcompile_escape_analysis_arrays`. Lit:
  `Analysis/Escape/escape-claims/{invariant-division-latch,invariant-product-latch,signed-division}.test`.
- First array attempt failed **52 assertions (1.14s; total 1.15s)**: ten older
  expected refusals now prove, plus eight added SCF specimens lacked a constant.
  Older source expressions are preserved; the added specimens were repaired.
  Native preliminary class failures were **18.00s** (retained inert receiver),
  **18.10s** (strict Number equality), **18.70s** (method parameter cell), and
  **19.02s** (unused borrowed constructor field). The latter equality/field
  bodies remain explicit refusals. Earlier public compatibility **199.37s**
  tested the callback-only draft; **198.66s** above tests final production.
- Logs: `escape-gate.log`, `escape-retry.log`, `native-gate.log`,
  `native-retry.log`, `native-numeric-gate.log`, `native-final-gate.log`,
  `native-field-gate.log`, `compat-gate.log`, `final-evidence.log`,
  `native-inspect.log`, and `commit-format.log`. All prior fixture dictionaries
  were compared against the initial snapshot and retained unchanged.
- Required `tools/format.sh --check` reports the same **26 diagnostics in nine
  unchanged files**. Changed C++/Python formatting and `git diff --check` pass.
  No browser implementation or Script semantics changed. Authenticated Azure
  run-command supplied a temporary task SSH key after the normal key failed;
  it was removed after verification, preserving all other authorized keys.
- Full CTest/compiler lit, complete DOM/String suites, broad corpus/native
  matrices, WPT/test262, whole Bootstrap and independent dataset lifetime
  replays were skipped. Browser compliance counts remain historical. No
  full-suite pass, whole-bundle admission gain, application-driver completion
  or push is claimed.

## Shared UTF-16 indexing and invariant products, 2026-09-19 UTC

Resumed clean **fb55eb20** and its original H Unicode boundary. The branch census
also found **ecca5b66**, a public UTF-16 extraction committed and gated on September
18 but never merged (AGENT-SYNC.jsonl 02:05:29/02:11:42). **0eadd9a0** finishes that
interrupted thread by atomic merge from an isolated current-tip worktree. Existing
CharacterData conversion bodies now live in public Core; the binding calls those
same functions. Browser behavior and Script implementation are unchanged.

**aeb877ee** proves original `charAt(0)` and `slice(1)` calls on known Strings,
with explicit initial String identity and the same method receiver. Generated C++
uses `ctbrowser::wtf8_to_utf16`, ordinary `std::u16string::substr`, and
`ctbrowser::utf16_to_wtf8`. Empty suffixes, NULs, BMP text, surrogate halves and
rejoining are checked. Strict null comparisons now refine the original optional
String in the non-null arm through the existing predicate mechanism. Other
indices/coercions, wrong receivers, replacement and unproved null arms refuse.
All previous fixture entries remain unchanged; nine new early-return specimens
remain refused at the existing shadow-frame boundary.

Parallel **677f73ec** proves one product latch over literal or unchanged saved
primitive operands through the shared signed-product transfer. Nested/recomputed
operands, changing transport, unknown/noncanonical/zero strides and final-index
bounds retain their checks. Four older source expressions are preserved; 31
CFG/SCF rows and an eleven-function source oracle cover the increment.

Focused passes: Core/CharacterData **2/2 (0.67s)**; transaction **1/1 (4.32s)**,
host contract **1/1 (0.47s)**, class DOM **1/1 (106.62s)**, public class
**1/1 (201.81s)**. Class DOM checks **304 source observations, 8 native executions
and 2,228 refusals**. **20 Node/VM observations intentionally differ** because the
VM indexes bytes; each engine's expected result is explicit and native matches
Node. Existing nullable URI checks pass **44 positive lowerings / 64 refusals**.
Arrays **1/1 (1.96s; total 1.97s)** and four escape oracles **4/4 (0.93s)** pass;
new oracle **34 sites / 6 sound / 6 of 22 precision**, zero violations. All **13
tested hashes** match the devbox. Required formatting retains 26 diagnostics in
nine unchanged files; changed checks pass. Exact targets, preliminary failures
and skipped coverage are in HANDOFF; evidence: `/tmp/ctcompile-utf16-resume/`.

**Next:** original H still needs Unicode-correct `toLowerCase`, then proof for its
normalized assignment keys. Lowercasing can collide (`bsFoo` / `bsfoo`), so it
cannot inherit the injective prefix-removal proof; preserve insertion/overwrite
order and the `__proto__` setter constraint. Transformed keys must never gain
dataset-membership authority. Class-method aliases, receiver captures, repeated
iterators, every unused H slot and global wrapper publication remain. W still
needs Object.entries/destructuring/original s/RegExp/TypeError/spread before
inheritance. Full H and W/W+r/W+r+H remain refused. Broader induction and part-25
backlog, the application driver and native Bootstrap remain unfinished. No broad
suite, whole-Bootstrap replay, bundle-admission gain or push is claimed.

Exact validation and limits:

- Escape build: `ctjs-translate`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`: **5 actions**.
  CTest: `ctcompile_escape_analysis_arrays`. Lit:
  `Analysis/Escape/escape-claims/{invariant-product-latch,invariant-unary-latch,signed-product,bitnot-latch}.test`.
- Browser build: `ctbrowser-test-core_basics`, `ctbrowser-test-character_data`:
  **223 affected actions**. Exact CTests `character_data` **0.11s**, `core_basics`
  **0.56s**. Core binary has both new helper symbols and no Script symbol. No
  native callers existed at that extraction gate; native use is checked below.
- Native build: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
  `ctcompile-test-exception-recovery`, `ctcompile-test-host-contract`: **71 actions**.
  The fixture-only retry rebuilt nothing; the final DOMEntry fix rebuilt the
  three affected compiler/contract targets in **5 actions**. Exact CTests:
  `ctcompile_exception_recovery`, `ctcompile_host_contract`. Lit:
  `CTNative/Lowering/Objects/{class-dom,class-initialization}.mlir`.
- The existing nullable-URI module's Node/VM observations, 44 positive lowerings
  and 64 refusal checks ran separately through `nullable-check.py`, to check the
  shared refinement without replaying the whole String matrix. Original source
  bodies and expectations are unchanged. Public class controls record 154 source
  observations, 380 native executions, 308 unprepared and 190 preparation refusals.
- First class attempt **14.10s** hit the existing early shadow-frame boundary;
  its nine new source bodies are retained as refusals, with guarded positives.
  Second attempt **13.90s** exposed the missing strict-null predicate. The shared
  refinement fixes that cause; reversed comparisons, else arms and a null-arm
  refusal are checked. Earlier transaction/contract passes were **4.31s/0.47s**;
  the results above come from the final source. All old fixture dictionary entries
  were compared against the initial snapshot and preserved.
- Logs: `core-retry.log`, `escape-gate.log`, `native-null-gate.log`,
  `nullable-gate.log`, `evidence.log`, and `final-format.log`. Earlier failures
  remain in `native-gate.log` and `native-retry.log`. Inspected native output uses
  owning strings/optionals, the public converters and existing concatenation;
  generated-source and binary Script/dispatch exclusions pass.
- The original SSH agent was unloaded. Authenticated Azure run-command installed
  a temporary task key; start/ssh-config/allow-ip restored access after the first
  core attempt failed before building. The temporary authorized key and local
  private key were removed after verification; other keys were preserved. The
  isolated worktree's unpublished ctjs pin was fetched from the original local
  submodule, with no pin or source change.
- Full CTest/compiler lit, complete DOM/String suites, broad corpus/native matrix
  replays, WPT/test262, whole Bootstrap and independent dataset lifetime replays
  were skipped. Historical browser compliance counts were retained without replay. No full-suite
  pass, whole-bundle admission gain, Script semantics change or push is claimed.

## Dataset loops beside native class construction, 2026-09-19 UTC

Resumed clean **4167e94c** from the **19:35:36 AGENT-SYNC.jsonl closure** and its
original H dynamic-key thread. Both agents' logs and unmerged branches were
reviewed; the September 7 WIP is already an ancestor. No predecessor edits
remained. Two parallel agents supplied native recommendations before service
limits; another completed the escape draft. Root integrated and gated both areas.
No browser implementation or runtime semantics changed.

**c1768e78** retains dynamic properties and iterable materialization for
complete typed DOM proof. Constructor-only classes now compose with a local H
slot's original filter predicate and for-of loop, including dataset member reads,
fresh-result writes and prefix-stripped output keys. Receiver, membership,
mutation order, callback bodies and every original identity use remain checked.
The completion normalizer also handles inactive poison slots without a switch;
MLIR folds exact integer dispatch before continuation selection. Every original
operation must still be visited across source paths before publication. All
**229 prior source entries and 14 effect checks** are unchanged.

**cf0ef348** proves one Plus/Neg/BitNot latch over an unchanged saved primitive.
Shared CFG/SCF checks preserve operand identity, original property keys, final
bounds and returned-child escape. Changing/recomputed operands, unknown or
noncanonical conversion and zero strides still refuse. Thirty new CFG/SCF rows,
ten source functions and seven preserved older expressions cover the change.

**c420b0ae** corrects one stale assignment refusal found by the focused regression.
A controlled build with both changed production files restored to **4167e94c**
already emits `member_read`; a check of all **40 original refusal sources** finds
only that admission. Its exact body now runs as a positive value/ownership test.
All nine prior positive sources/input sets and 39 remaining refusals are unchanged;
object key-order checks remain intact alongside scalar/array result comparisons.
This corrects coverage and is not a new native admission gain.

Focused devbox results: transaction **1/1 (4.55s; total 4.56s)**, host contract
**1/1 (0.49s; total 0.50s)**, class DOM **1/1 (90.75s): 260 Node/interpreter
observations, 8 native executions, 1,968 refusals**, public class initialization
**1/1 (210.85s)**, DOM assignment **1/1 (197.44s): 10 sources, 225 Node/VM
observations plus Node accessor traces, 8 GCC/Clang binaries, lifetime sanitizer
and 192 refusals**. Escape arrays
**1/1 (1.21s; total 1.22s)** and four selected escape oracles **4/4 (0.58s)**
pass, zero violations. New oracle: **31 observed sites / 8 sound / 8 of 20
precision**, no partial, pending or unclaimed sites. All **ten final tested
source/test hashes** match the devbox.

Required formatter retains **26 existing diagnostics in nine HEAD-identical
files**. Changed C++/Black checks, **18 new source syntax checks**, **12 Node
observations** and `git diff --check` pass. Evidence:
`/tmp/ctcompile-dynamic-dom-keys/`, particularly `completion-poison-gate.log`,
`remaining-gate.log`, `escape-evidence.log`, `native-evidence.log`,
`commit-format.log`, `assignment-replay.log`, `baseline-member.log`,
`baseline-refusals.log`, `assignment-format.log` and `final-access.log`.
Inspected output uses owning strings, vectors and JSON values, direct predicates
and existing ctbrowser dataset/attribute helpers; Script and dynamic-dispatch
gates pass.

**Exact next:** the retained `class_dynamic_original` contains the complete
original H.getDataAttributes method, including M, for-of and Unicode key
normalization. It now reaches
`native DOM entry: DOM property read lacks a proved receiver and supported member`.
Its `i.charAt(0).toLowerCase() + i.slice(1)` key chain still needs Unicode-correct
proof.
The generic closure lifter still treats unrelated dynamic properties as possible
class-method identity uses; preserve `class_dynamic_capture` while proving that
alias boundary. Direct method filter closures and repeated iterators retain
separate refusals. Full H also needs every unused slot proved without invented
parameter authority, then global wrapper publication. W's Object.entries,
destructuring, original s, RegExp/TypeError/spread and inheritance remain. Preserve
full-H and W/W+r/W+r+H refusals (last omits s). Recomputed induction operands,
broader conversions and part 25's backlog remain. Native Bootstrap, the application
driver and the overall plan are unfinished.

Exact validation and limits:

- Initial explicit build: `ctjs-opt`, `ctjs-translate`,
  `ctcompile-test-native-reference`, `ctcompile-test-exception-recovery`,
  `ctcompile-test-host-contract`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`: **11 actions**.
  Native acceptance build: `ctjs-opt`, `ctcompile-test-exception-recovery`,
  `ctcompile-test-host-contract`: **5 actions**. Intermediate builds used subsets
  of those targets; none failed compilation. The controlled baseline used
  `ctjs-opt` (**5 actions**); restoring the final source used the three native
  targets above (**7 actions**), followed only by the affected assignment replay.
- Exact CTests: `ctcompile_exception_recovery`, `ctcompile_host_contract`,
  `ctcompile_escape_analysis_arrays`. Exact lit cases:
  `CTNative/Lowering/Objects/{class-dom,class-initialization}.mlir`,
  `CTNative/Browser/native-dom-assignment.test`, and
  `Analysis/Escape/escape-claims/{invariant-unary-latch,bitnot-latch,primitive-unary-latch,negative-string}.test`.
- Earlier native transaction/class-DOM runs failed during diagnosis of generic
  class-method aliases, lexical receiver captures and inactive loop completion.
  A later host-contract check caught mutation of an already-refused loop callback;
  narrowing normalization to actual completion values fixed it. That test and the
  native class refusals were retained. All debug instrumentation and experimental
  iterator-prefix cleanup were removed; DOMIteration.cpp is unchanged.
- The first assignment run failed **184.40s** at the stale `member_read`
  refusal after its positive executions. The baseline comparison above established
  that this predated the session; the focused replay passes **197.44s**.
- Initial SSH failed because the original agent was unloaded; authenticated Azure
  access installed a temporary task key. `start`, `ssh-config` and `allow-ip` were
  used; a later timeout recovered after lifecycle/IP refresh. The temporary
  authorized key and local private key were removed after final hash verification;
  other authorized keys were preserved. The original SSH agent remains unloaded.
  The first combined shell invocation consumed the following test commands from
  stdin; only its build is counted. Subsequent remote commands use `</dev/null`.
- Full CTest/compiler lit, complete DOM suites, broad corpus/native matrices,
  WPT/test262, whole Bootstrap and independent dataset lifetime replay were skipped.
  No browser implementation/runtime change, bundle admission gain, full-suite pass
  or push is claimed.

## Combined helper captures and literal BitNot latches, 2026-09-18 UTC

Resumed clean **2c17b381** from the **19:20:02 AGENT-SYNC.jsonl closure** and
HANDOFF's combined M/filter-callee thread. Both agents' logs and unmerged branches
were reviewed; the September 7 WIP is already an ancestor. No predecessor edits
remained. A parallel agent audited the native proof; root completed fixture
recommendations and a partial three-file escape draft after two agents reached
service limits. No browser implementation or runtime semantics changed.

**bb96fa31** shares the existing confined-callback proof between ordinary
helper and method/slot capture censuses. Each original callee use still passes
its own exact capture or callback checks. Callback bodies and source positions
survive for complete typed DOM proof. Bounded compositions of original M and the
Bootstrap dataset predicate now execute directly and through a class method
capturing H; repeated calls preserve DOM write order. All **218 prior entries
across five fixture dictionaries** are unchanged. Unknown effects, callback captures,
identity escape, changing helpers, invalid inputs and uncalled slots still refuse.
This is a bounded composition, not execution of full original H.

**7cf800bc** proves one original literal BitNot loop latch through the same
bounded ToUint32 complement used for ordinary unary snapshots. CFG and SCF
preserve returned-child escape and result identity. Original property keys,
zero/noncanonical/unknown strides, repeated nonliteral producers and final bounds
retain their checks. Existing literal BitNot source bodies remain unchanged with
updated expectations.

Focused devbox validation: transaction **1/1 (4.27s; total 4.28s)**, host
contract **1/1 (0.47s; total 0.48s)**, class DOM **1/1 (81.08s): 248
Node/interpreter observations, 8 native executions, 1,818 refusals**, public
class initialization **1/1 (199.38s)**. Arrays **1/1 (1.13s; total 1.14s)**; four
selected escape oracles **4/4 (0.12s)**, zero soundness violations. New BitNot
oracle: **30 observed sites / 9 sound / 9 of 19 precision**.
Required formatting retains **26 existing diagnostics in nine HEAD-identical
files**; changed C++/Python checks pass. Initial formatting caught the unfinished
escape draft before root formatted it; the final count excludes those corrected
lines. All eight tested source/test hashes match the devbox.
Evidence: `/tmp/ctcompile-combined-callee/`.

**Exact next:** complete original H.getDataAttributes needs dynamic dataset/output
keys (`t.dataset[n]`, `e[i]`), its original loop and Unicode normalization. Prove
every original unused H slot without inventing authority for its parameters;
global holders also need wrapper publication. Preserve full H and W/W+r/W+r+H
refusals (the last omits original s). W's Object.entries/destructuring/original
s/RegExp/TypeError/spread remain before inheritance. Repeated nonliteral induction,
broader conversions and part-25's backlog remain. Native Bootstrap, the application
driver and the overall plan are unfinished.

Exact targets/checks and skipped coverage:

- Native build: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
  `ctcompile-test-exception-recovery`, `ctcompile-test-host-contract`: **8 actions**.
- Escape build: `ctjs-translate`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`: **5 actions**.
- Exact CTests: `ctcompile_exception_recovery`, `ctcompile_host_contract`,
  `ctcompile_escape_analysis_arrays`. Exact lit cases:
  `CTNative/Lowering/Objects/class-dom.mlir`, `class-initialization.mlir`, and
  `Analysis/Escape/escape-claims/{bitnot-latch,primitive-unary-latch,signed-bitwise,negative-string}.test`.
- `server.sh start` confirmed the devbox running; no network-rule change was
  needed. Local new fixture syntax **11/11** and Node observations **12/12** pass.
- Full CTest/compiler lit, complete DOM suites, broad corpus/native matrices,
  WPT/test262, whole Bootstrap and independent dataset lifetime replay were
  skipped. No bundle admission gain, full-suite pass or push is claimed.

Inspected `native-output.cpp`: ordinary strings, vectors, optionals, RAII JSON
values and existing ctbrowser attribute/dataset calls. The original predicate is
a direct function passed to `filter_strings`; Script and dynamic-dispatch gates pass.

## Captured H objects and literal unary latches, 2026-09-18 UTC

Continued clean **ce4c9628** from the **18:59:17 AGENT-SYNC.jsonl closure** and its
unfinished class-method H-object capture thread. The September 7 WIP is already
an ancestor. **ed69f95a** proves fixed local holder captures and aliases in DOM
class methods. Shared holder analysis checks every original use and slot; all
slots precede capture, writes require the original object, and every slot body
remains for actual-call and typed DOM proof. Capture/cell transport disappears
before the holder object. Branching methods and repeated calls execute original
M/F and H.getDataAttribute without Script dependencies. All **205 prior entries across five
fixture dictionaries** remain unchanged. Unknown effects, changing holders/slots,
escaping identities, uncalled slots and invalid later inputs still refuse.

**953d776d** proves one original literal Plus/Neg in a loop latch, including
Boolean and canonical String conversion. CFG/SCF use the same bounded proof;
returned children still escape. Original keys, zero/noncanonical/unknown strides,
nonliteral repeated producers and final index bounds retain their checks. Old
literal-unary source bodies remain unchanged with corrected proof expectations.
A parallel agent supplied native fixtures and another audited the proof; root
completed the escape proposal after its agent reached a service limit.

Focused devbox passes: transaction **1/1 (4.19s; total 4.20s)**, host contract
**1/1 (0.46s; total 0.47s)**, class DOM **1/1 (74.66s): 236 source observations,
8 native executions, 1,678 refusals**, public class initialization **1/1 (198.85s)**.
Arrays **1/1 (1.09s; total 1.10s)**; four selected escape oracles **4/4 (0.12s)**,
zero violations. New unary-latch oracle: **24 sites / 9 sound / 9 of 15 precision**.
All **ten tested hashes** match the devbox. Required formatting retains **26
existing diagnostics in nine unchanged files**; changed checks pass. HANDOFF
records exact targets, preliminary failures and skipped coverage. Evidence:
`/tmp/ctcompile-h-object/`. No browser edits, whole-Bootstrap replay, bundle gain
or full-suite claim. No push.

**Exact next:** combine a slot's original M capture and retained filter-callback
callee uses, then prove every original unused H slot without invented parameter
authority. Global holders still need wrapper publication. H Unicode normalization
and W's Object.entries/destructuring/original s/RegExp/TypeError/spread remain
before inheritance. Preserve W/W+r/W+r+H refusals (last omits s). Direct broad
JSON-result/String equality now has a retained refusal specimen. Literal BitNot
latches, broader conversions and part-25's backlog remain. The application driver,
native Bootstrap and the overall plan are unfinished.

Exact validation and preliminary failures:

- First native build targets: `ctjs-opt`, `ctjs-translate`,
  `ctcompile-test-native-reference`, `ctcompile-test-exception-recovery`,
  `ctcompile-test-host-contract`: **36 actions**.
- Combined build added `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`: **9 actions**.
  The escape-only retry used those three plus `ctjs-translate`: **2 actions**.
- Exact CTests: `ctcompile_exception_recovery`, `ctcompile_host_contract`,
  `ctcompile_escape_analysis_arrays`. Exact lit cases:
  `CTNative/Lowering/Objects/class-dom.mlir`, `class-initialization.mlir`, and
  `Analysis/Escape/escape-claims/{primitive-unary-latch,primitive-add-latch,negative-string-latch,negated-stride}.test`.
- Initial transaction **1/1 (4.17s)** and contract **1/1 (0.46s)** passed.
  Initial class DOM failed **12.11s** on a new broad JSON-result/String equality.
  That source is retained as a refusal; the write-order positive observes
  supported typeof results. A newly proposed immutable alias refusal became a
  positive because the same fixed-cell proof establishes its identity.
- First arrays failed **1.14s (total 1.15s)** with **37 assertions** from old
  literal-unary refusal expectations. Their source bodies are preserved; the
  final expectations distinguish proved Plus/Neg from still-unproved BitNot.
- Read-only audit found one uncharged use-list allocation; its collection now
  spends the existing budget. Final transaction/contract and public compatibility
  results above include that correction. Original native fixture syntax/source
  observations and changed C++/Python formatting pass. The full formatter's nine
  diagnostic files are HEAD-identical (four browser, five compiler).
- `server.sh start` confirmed the box running; no network-rule change was needed.
  Full CTest/compiler lit, complete DOM, broad corpus/native matrices, WPT/test262,
  whole-Bootstrap and independent dataset lifetime replay were skipped.

Inspected `/tmp/ctcompile-h-object/native-output.cpp`: ordinary strings, optional
values, RAII JSON values and existing ctbrowser attribute/URI calls; the native
Script/dispatch gates pass.

The next combined-callee proof can share validation of one confined callback
between `helperCallbacks` and `methodCaptures`; neither should blindly skip
unproved callee uses. Existing DOMSource callback transport remains the seam.

## Captured local H helpers and Boolean Add latches, 2026-09-18 UTC

Continued clean **9b9ca236**, following the **18:35:50 AGENT-SYNC.jsonl closure** and
its unfinished full-H capture thread. Both agents' logs and unmerged branches
were reviewed; the September 7 WIP exists and is already an ancestor. No dirty
predecessor work remained. Parallel agents supplied a frozen escape draft and
native fixture/proof recommendations; two hit service limits without edits.
Root completed native implementation, fixtures, review, gates and small commits.
No browser source or runtime representation changed.

**51d419c2** lets an entry-local callable holder slot capture fixed sibling
functions through the existing class-method capture proof. Bootstrap's original
one-slot H.getDataAttribute expression now composes with a class result, complete
M and F bodies, repeated calls, and JSON/URI fallback. Exact closure identities,
source order, implicit arguments and every captured use precede rewriting. Proved
capture metadata clears before holder closure erasure; every slot body remains
for actual-call and complete typed DOM proof. Helper replacement, identity escape,
callback effects, invalid later keys, missing authority and uncalled slots refuse.
All **187 prior fixture entries** remain unchanged; three positives, nine refusal
specimens and twelve transaction controls per provider were added.

**153a4daa** proves Boolean true Add latches in either operand order using
the existing bounded conversion. Saved inputs and CFG/SCF transport retain their
identity; String concatenation, zero/unknown/changing strides, repeated producers,
original property keys and final bounds keep their refusals. Returned children
still escape; only unreturned children discharge. Prior fixture bodies are intact.

**9fd098ed** fixes a shared native C++ statement ambiguity exposed by the
new repeated-H call. An unused opaque constructor call was printed as Type(v),
which C++ parses as a declaration. Native statements now explicitly discard the
value with `(void)`, preserving construction and destruction. The focused
regression observes destructor effects before the next source call; unmarked
upstream printing remains unchanged.

Focused evidence: `/tmp/ctcompile-holder-captures/`.

- Explicit first-build targets: `ctjs-opt`, `ctjs-translate`,
  `ctcompile-test-native-reference`, `ctcompile-test-exception-recovery`,
  `ctcompile-test-host-contract`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`: **11 actions**.
  The emitter retry built the five native targets: **3 actions**.
- Exact `ctcompile_exception_recovery`: **1/1 (4.10s; total 4.11s)**.
- Exact `ctcompile_host_contract`: **1/1 (0.46s; total 0.47s)**.
- Exact `CTNative/Lowering/Emission/unused-call.mlir` and
  `Target/Cpp/upstream/call.mlir`: **2/2 (0.13s)**, including compiled execution.
- Exact `CTNative/Lowering/Objects/class-dom.mlir`: **1/1 (66.81s)**,
  **220 Node/interpreter observations, 8 native executions, 1,530 refusals**.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`: **1/1 (198.50s)**,
  preserving the original W/W+r/W+r+H refusals.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (1.10s; total 1.11s)**.
- Five selected escape-claims cases: `primitive-add-latch`, `add-latch`,
  `commuted-add-latch`, `negative-string-latch`, `primitive-addition`:
  **5/5 (0.16s)**, zero violations. New primitive-add-latch:
  **35 observed sites / 11 sound / 11 of 21 precision**.

The first class-DOM execution failed **23.09s** in emitted C++ for a discarded
JSON constructor. The minimized emitter regression first needed a FileCheck
LABEL correction (**0.03s**), then failed at precisely the missing explicit
discard (**0.03s**). Its fix retains every original native fixture. Transaction,
contract and escape results were not replayed after the emitter-only fix.
`server.sh start` confirmed the devbox running; resync waited on local filesystem
I/O before the three-action emitter build. No network-rule change was needed.

Required `tools/format.sh --check` reports **26 existing diagnostics in nine
HEAD-identical files (four browser, five compiler)**. Changed formatting, Node
syntax and whitespace checks pass. All **ten tested hashes** match the devbox.
The emitted H/M/F path uses ordinary C++ values and existing ctbrowser helpers;
its native Script/AOT/dispatch gates pass. Full CTest/compiler lit, complete DOM,
broad corpus/native matrices, WPT/test262, whole-Bootstrap replay and independent
dataset lifetime replay were skipped. No bundle gain or full-suite pass is claimed.
No push.

**Exact next:** the class method's H-object capture still needs a fixed local
holder identity proof. Full H's getDataAttributes also combines an M capture with
its retained filter callback; the reused callee census currently accepts captured
reads or callback creation separately. Every original unused slot still needs
complete proof without invented parameter authority. Global holders need wrapper
publication to compose with the inert entry declaration. Then H's Unicode
charAt(0).toLowerCase(), W's Object.entries/destructuring/original s/RegExp/TypeError/
spread, and inheritance. Preserve W/W+r/W+r+H refusals (last omits s). Broader
conversion/induction and part-25's backlog remain. The application driver, native
Bootstrap and the overall plan remain unfinished.

## Local callable holders and negative String latches, 2026-09-18 UTC

Continued clean **414d395e**, following the **18:13:26 AGENT-SYNC.jsonl closure** and
its unfinished full-H holder proof. No predecessor edits remained. Both agents'
commits and unmerged branches were reviewed; the September 7 WIP branch exists
and is already an ancestor. Parallel agents supplied holder/fixture/induction
recommendations; two hit service limits without edits. Root completed the code,
fixtures, gates and commits. No browser source or runtime representation changed.

**6c965f19** composes entry-local callable holders with original class and DOM
proofs. Exact own slots, closure enclosure, captures, callback uses, implicit
arguments and same-block source order are checked before rewriting. The existing
holder rewrite makes direct calls before ordinary closure lifting, but retains
**every local slot function** for actual invocation and complete typed DOM proof.
Uncalled slots, even pure ones, refuse. Original filter callbacks survive as plain
C++ predicates; repeated calls preserve their results. Captures, unknown effects,
replacement, identity escape, invalid later inputs and missing authority refuse
transactionally. Public closed-source/global-holder rules remain strict.
All **172 pre-session fixture entries** remain unchanged; two positive and
thirteen refusal specimens and twelve transaction controls per provider were
added. The diagnostic now names an otherwise unclassified rejected operation.

**476c635f** proves original negative canonical String Sub latches such as
`i -= '-1'`. Direct literals and saved/transported inputs reuse bounded conversion;
Add still requires a Number stride. CFG/SCF backedges, original property keys,
changing/repeated producers, canonical spelling and final-index bounds retain
their checks. Returned children still escape; only unreturned children discharge.
Both existing oracle source sections remain byte-identical.

Focused devbox evidence: `/tmp/ctcompile-holder-proof/`.

- Native explicit targets: `ctjs-opt`, `ctjs-translate`,
  `ctcompile-test-native-reference`, `ctcompile-test-exception-recovery`,
  `ctcompile-test-host-contract`. Builds: **5, 5, 5, then 4 actions**.
- Exact `ctcompile_exception_recovery`: **1/1 (4.00s; total 4.01s)**.
- Exact `ctcompile_host_contract`: **1/1 (0.46s; total 0.47s)**.
- Exact `CTNative/Lowering/Objects/class-dom.mlir`: **1/1 (60.71s)**,
  **208 Node/interpreter observations, 8 native executions, 1,410 refusals**.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`: **1/1 (198.14s)**,
  retaining original W/W+r/W+r+H refusals.
- Escape explicit targets: `ctjs-translate`,
  `ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims`,
  `ctcompile-test-type-oracle`, **5 build actions**.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (1.08s)**.
- Six selected escape-claims cases: `negative-string-latch`, `sub-negative-latch`,
  `negative-string`, `add-latch`, `commuted-add-latch`, `negated-stride`.
  Final **6/6**, zero violations: four passed initially (**0.18s** for the
  six-case invocation), two corrected cases passed their retry (**0.11s**).
  New latch oracle: **23 sites / 8 sound / 8 of 14 precision**;
  sub-negative-latch **15 / 8 / 8 of 8**; negated-stride **17 / 10 / 10 of 10**.

The first native transaction failed **4.09s**; a diagnostic-only retry (**4.01s**)
identified helper-local cells because a conditional holder use violated the
existing identity proof. Entry-linear fixtures then reached the ordinary
closure-lift identity refusal (**4.16s**). Retaining slot functions while reusing
the holder rewrite fixed it. The escape retry changed only test expectations
and the new mutation specimen: a constant overwrite before the latch is still a
fixed step, so the control now actually changes its latch. The existing unary
String specimen retains its body with a corrected expectation.
Required `tools/format.sh --check` still reports **26 existing diagnostics in nine
HEAD-identical files (four browser, five compiler)**; changed C++/Black/Node and
whitespace checks pass. All **10 source/test hashes** match the devbox. The
inspected holder output uses ordinary String/vector/optional values and the
existing ctbrowser DOM API; the native Script/AOT/dispatch gates pass.
Full CTest/compiler lit, complete DOM suites,
broad corpus/native matrices, WPT/test262, whole-Bootstrap replay and independent
dataset lifetime replay were skipped. No bundle gain or full-suite pass is claimed.
No push.

**Exact next:** full local H still captures M/F, and a class method captures H
itself; the sibling capture proof currently accepts functions, not that object.
Original uncalled H slots must receive complete effect/type proof, with no invented
parameter authority. Global holders also require their script-wrapper publication
to compose with the class path's inert-entry-declaration requirement. H's original
Unicode `charAt(0).toLowerCase()` remains. Preserve W/W+r/W+r+H specimens (last
omits `s`); W still needs Object.entries, destructuring, original s, RegExp/TypeError
and spread before inheritance. Broader conversion/induction and the part-25
backlog remain. The application driver, native Bootstrap and overall plan are
unfinished.

## Captured dataset-filter callbacks and negative Strings, 2026-09-18 UTC

**aef32da5** resumes the full-H thread found in the **17:40:39 AGENT-SYNC.jsonl journal**
and claims abandoned by the **17:42:18 loop failure**, starting clean at
**40db7482**. Both agents' recent commits and unmerged branches were reviewed;
the old September 7 WIP branch is absent. Parallel agents audited the proof,
planned fixtures and implemented escape conversion. The fixture/audit agents hit
service limits; root completed the native code/tests and integrated the frozen
escape draft.

The landed native increment preserves **Bootstrap's original dataset-filter
predicate** through a class method's fixed sibling helper capture. Only a confined,
capture-free, single-parameter filter callback with unused implicit arguments
survives direct-helper expansion. Original enclosure checks precede rebinding its
inert closure metadata to the caller; source position and branch stay intact.
The existing typed DOM proof still checks callback bodies, uses and intrinsic/
dataset authority. Repeated calls work. Unknown effects, captures, identity
escape, implicit arguments and unused-method effects refuse transactionally.
Full H's holder-slot census remains strict; complete original H is pinned as a
refusal. No browser implementation or runtime representation changed.

All **161 pre-session fixture entries** remain unchanged. Two positive fixtures
exercise the original prefix inclusion/exclusion against four dataset keys;
eight mutations and complete H refuse. Ten new transaction controls per provider
check publication and rollback, including dataset authority. The shared emission
checker accepts an explicitly requested callback count; its default and all
Script/AOT/output checks remain. **8dbb2abc** formats its diagnostic afterward.

**7d9e9f8e** adds bounded canonical negative String conversion (**-1 through
-4294967295**) to the shared numeric snapshot proof. Unary, subtraction,
product, division/remainder, unit powers and bitwise operations retain saved
input and CFG/SCF provenance. Original String property keys and Add concatenation
remain unchanged; -0, noncanonical spellings, unknown/changing/repeated inputs and
out-of-bound intermediate results stay unproved. Old test source bodies remain;
affected expectations now reflect proved conversion.

Focused devbox evidence: `/tmp/ctcompile-h-complete/`.

- Native explicit targets: `ctjs-opt`, `ctjs-translate`,
  `ctcompile-test-native-reference`, `ctcompile-test-exception-recovery`,
  `ctcompile-test-host-contract`. Initial build **9 actions**, fixture retry **2**.
- Exact `ctcompile_exception_recovery`: **1/1 (3.96s; total 3.97s)**.
- Exact `ctcompile_host_contract`: **1/1 (0.46s; total 0.47s)**.
- Exact `CTNative/Lowering/Objects/class-dom.mlir`: **1/1 (58.30s)**,
  **200 Node/interpreter observations, 8 native executions, 1,310 refusals**.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`: **1/1 (199.15s)**,
  preserving original W/W+r/W+r+H refusals.
- Exact `CTNative/Browser/native-dom-dataset.test`: **1/1 (56.83s)**,
  **28 sources, 112 Node/VM observations, 8 GCC/Clang binaries, 432 refusals**,
  with HTML/SVG and lifetime sanitizer checks.
- Escape explicit targets: `ctjs-translate`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`;
  initial build **3 actions**, expectation retry **2**.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (1.07s; total 1.08s)**.
- Exact escape-claims `negative-string`, `signed-division`, `power-identities`,
  `signed-bitwise`, `signed-right-shifts`, `signed-subtraction`: **6/6 (0.21s)**,
  all zero violations. New negative-string oracle: **35 sites / 11 sound /
  11 of 21 precision**. Other measurements are in the saved log.

Initial native transaction failed **4.77s** because the new fixture used numeric
strict equality, outside DOM admission; the fixture now uses supported comparisons
for the same exact count. Class-DOM then failed **19.72s** because the shared test
checker expected one function; it now explicitly expects the retained callback.
Production did not change after the initial native build. Initial escape arrays
failed **1.11s** at two stale negative-String power/product expectations (eight
assertions); source-preserving expectation fixes produced the pass.
The native commit command failed to stop after Black rejected one diagnostic
line; **8dbb2abc** fixes that formatting only. Final changed-file Black/Python/C++
and diff checks pass. Required repository formatting still reports **26 existing
diagnostics in nine HEAD-identical files (four browser, five compiler)**.
Authorized devbox start and allow-ip restored SSH before any tests ran.
All **16 source/test file hashes** match the devbox. The formatting follow-up
is AST-identical to the tested helper. Inspected filter output uses ordinary
String/vector values and a plain C++ predicate; Script/AOT symbol gates pass.
Full CTest/compiler lit, complete DOM suites, broad corpus/native matrices,
WPT/test262 and whole-Bootstrap replay were skipped. No bundle gain or full-suite
pass is claimed. No push.

**Exact next:** full H's callable holder and every original unused slot must reach
complete effect/type proof before erasure. `ClassInitialization::sourceClosure`
still rejects its filter-producing callee dependency; `expandGlobalHolders`
can remove uncalled slots before DOM method probes, so simply allowing callbacks
there is unsound. Capturing a local H object also remains outside the sibling
function capture proof. The direct-helper callback transport prerequisite is now
landed. Original H's Unicode `charAt(0).toLowerCase()` normalization remains.
Preserve W/W+r/W+r+H specimens (last omits `s`); W still needs Object.entries,
destructuring, original s, RegExp/TypeError and spread before inheritance.
Direct negative-String loop latches (for example `i -= '-1'`) still need their
own induction certification; broader conversions and the part-25 backlog remain.
The application driver, native Bootstrap and the overall plan are unfinished.

## Captured Bootstrap F replacement proof, 2026-09-18 UTC

**5b3d9602** resumes the original-F thread at clean **9e60cdfd**. The unfinished
**16:58:03 fixture claim**, abandoned by the **16:59:11 loop failure**, and the
previous HANDOFF/master 00/24 identified it; no predecessor edits remained.
September 7 WIP is already an ancestor. Both agents' commits and historical
unmerged branches were reviewed. Parallel agents supplied a proof audit and
fixture/escape recommendations, then hit service limits without edits; root
completed the native work. No escape implementation changed.

Original vendor-pinned **F** now works when called only through a DOM class
method's fixed sibling capture, including literal, forwarded and distinct
lowercase inputs. The class census retains helper-local cells and uncaptured
replacement callbacks for the existing DOM proof. The reserved RegExp factory
must be declared. Every direct symbol caller contributes to bounded String input
facts; any unknown input invalidates that formal. Original callback identities
are checked before the shared no-match fold, and any surviving closure/capture
refuses before direct inlining. Ordinary nested helpers, matching/live inputs,
unused matching methods, unknown effects and identity escape still refuse.
The global-holder census and public closed-source class pass remain strict.

All **148 pre-session fixture entries** are unchanged. Three new positive fixtures
retain original F; ten mutations refuse. Twelve transaction controls per provider
check publication and rollback. Local Python/Black, thirteen Node syntax checks,
twelve new Node observations and changed C++ formatting pass. No browser source,
new runtime representation or VM dependency was added.

Focused devbox evidence: `/tmp/ctcompile-f-resume/`.

- Explicit targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
  `ctcompile-test-exception-recovery`, `ctcompile-test-host-contract`.
  Initial and corrected builds: **8 actions each**.
- Exact `ctcompile_exception_recovery`: **1/1 (3.86s; total 3.87s)**.
- Exact `ctcompile_host_contract`: **1/1 (0.48s)**.
- Exact `CTNative/Lowering/Objects/class-dom.mlir`: **1/1 (53.78s)**,
  **192 Node/interpreter observations, 8 native executions, 1,226 refusals**.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`: **1/1 (197.50s)**,
  preserving original W/W+r/W+r+H refusals.

The initial transaction failed **3.89s**: the first deferral admitted ordinary
nested-helper control 67 and omitted F's local-cell/factory prerequisites.
Restricting callback uses, retaining cell proof and declaring the factory fixed
those failures. The corrected class-DOM run then failed **9.72s** because the new
F fixtures were missing from existing missing-identity bookkeeping. Its test-only
correction produced the final class-DOM pass; production did not change again.
The first public compatibility invocation lost its SSH connection after starting;
its result is unobserved. Azure reported the VM running. The authorized
`server.sh allow-ip` refresh restored connectivity, and no test process remained.
Only that compatibility case was restarted, with a devbox log/JSON result and SSH
keepalive. The already-passing F checks were not replayed for this transport failure.

Required `tools/format.sh --check` reports **26 pre-existing diagnostics in nine
HEAD-identical browser files**; changed formatting and `git diff --check` pass.
All **five tested file hashes** match the devbox. Inspected F output uses ordinary
String/optional values and the existing ctbrowser DOM helper; native symbol checks
exclude Script/AOT dependencies. Full CTest/compiler lit, complete DOM suites,
broad corpus/native matrices, WPT/test262, whole-Bootstrap replay and unchanged
escape tests were skipped. No bundle gain or full-suite pass is claimed. No push.

**Exact next:** compose **full H** without bypassing its unused-slot census.
`getDataAttributes` creates the retained dataset-filter callback; global holder
admission still rejects its callee dependency before complete slot/effect proof.
Original H also retains Unicode `charAt(0).toLowerCase()` normalization. Preserve
W/W+r/W+r+H specimens (the last omits `s`). W still needs Object.entries,
destructuring iteration, original s, RegExp/TypeError and spread before inheritance.
The read-only escape candidate is bounded negative canonical String conversion;
it has no implementation or measurement from this session. Retained callbacks,
the application driver and native Bootstrap remain unfinished.

## Captured sibling Bootstrap M, 2026-09-18 UTC

**1b27ab4b** finishes the interrupted helper-capture thread at **2d72d9b0**.
The **16:35:25 AGENT-SYNC.jsonl entry** and two dirty class-DOM fixture files identified
the work; the **16:37:28 loop failure** abandoned it before production changes.
September 7 WIP was verified as an ancestor; both agents' recent commits and
historical unmerged branches were reviewed. Parallel agents recovered fixtures
and audited the proof and next boundary. The escape investigation stopped at a
service limit without edits.

DOM class methods can now be the original Bootstrap **M** helper's only caller.
The captured cell must have a fixed, ordered sibling closure. Its target captures
nothing and observes none of its implicit arguments. Captured reads feed only
ordinary calls with undefined receivers; missing arguments are padded under the
work budget, and surplus arguments refuse. Rewriting preserves each original
call's position and arguments. Only an unobserved helper closure disappears.
All implicit-argument premises are checked before mutation, including closure
deletion. Method normalization retains the captured target mapping.

Existing complete DOM/URI/JSON analysis still proves every method, including
unused bodies, before publication. Changing bindings, callable escape, nested
callee dependencies, unknown effects and exhausted budgets refuse without
changing the original module or contract. Global-holder slots keep their strict
census. The public class pass remains closed-source-only. No browser source,
runtime representation or VM dependency was added.

All **137 pre-session fixture entries** remain unchanged. Five new positive
fixtures cover captured Number conversion and complete M's Number/null, JSON and
URI fallback paths; six new negative fixtures cover mutation, escape and effects.
The transaction test adds twelve controls per DOM provider. Independent local
checks passed Python/Black, eleven Node syntax checks and twenty new Node
observations.

Focused devbox evidence: `/tmp/ctcompile-capture-finalize/`.

- Explicit targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
  `ctcompile-test-exception-recovery`, `ctcompile-test-host-contract`.
  Initial build: **5 actions**; final build after budget charging: **4 actions**.
- Exact `ctcompile_exception_recovery`: **1/1 (3.86s; total 3.87s)**.
- Exact `ctcompile_host_contract`: **1/1 (0.47s)**.
- Exact `CTNative/Lowering/Objects/class-dom.mlir`: **1/1 (51.23s)**.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`:
  **1/1 (197.96s)**, retaining the original W/W+r/W+r+H refusals.

The initial shell runner's SSH consumed its remaining stdin, so that invocation
built only and ran no tests. The corrected runner used protected stdin and ran
all four selections above. All four tested file hashes match the devbox.
Required `tools/format.sh --check` reports **26 pre-existing diagnostics in nine
HEAD-identical files**; changed C++ formatting, Black and `git diff --check` pass.
Full CTest/compiler lit, complete DOM suites, broad corpus/native matrices,
WPT/test262, whole-Bootstrap replay and unchanged escape tests were skipped.
No whole-bundle gain or full-suite pass is claimed. No push.

**Exact next:** original **F** creates a nested replacement callback, making its
implicit callee live before the sibling proof can accept it. Reuse
`DOMSource::foldNoMatchReplacements` for proved lowercase inputs such as `config`,
retaining original callback validation; do not relax callee identity generally.
Then compose full **H**, whose unused holder slots and dataset filter callback
still require proof. Preserve W/W+r/W+r+H specimens; the last still omits `s`.
W also needs `Object.entries`, destructuring iteration, original `s`,
RegExp/TypeError and spread before inheritance. Full H Unicode, retained
callbacks, broader escape conversions and the application driver remain open.
Native Bootstrap and the overall plan are unfinished.
