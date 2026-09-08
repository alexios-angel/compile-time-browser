# Handoff: continuing ctcompile

Native work continues on `ctcompile-v1` in the `compile-time-browser` monorepo.
The application driver remains incomplete; native compiler development uses
`ctjs-translate` and `ctjs-opt`. Build on the devbox using `tools/remote-build.sh`
under `/tmp/ctbrowser-devbox-build.lock`, then run the local formatter before
committing. There is no CI. Do not build on the small local machine.

## Guarded Map reads and copied-object checkpoint, 2026-09-08

Saved locally on `ctcompile-v1`: **`677714b`**, guarded saved scalar Map reads
across conditional deletion, and **`72ca88e`**, fixed own-object copy contents
and retention. This resumes `guarded_saved_read` from **`ca99089`** and the
**09:08:55 synchronization journal**. The starting tree was clean. The old
CallDirectOp recovery was already landed in `5307abf` and its branch is an
ancestor; no history was rewritten. Separate agents supplied execution gates,
host unit/audit controls, copy analysis and executed source witnesses. No browser
source changed or push was performed.

The exact guarded ternary advances **0/6 -> 6/6 native** in both optimization
modes, preserving all **eighteen calls** and Node/interpreter **`trace=2`**.
The deletion-free and straight-line fallback controls give **3** and **1** and
remain admitted. Host/native proofs retain payload tags valid whenever present
across deletion joins, independently of definite membership. Only a live `has`
for the same Map/key restores membership. Joins intersect record keys and tags;
unknown contents never become a proved absence. Possible-alias writes still
join payload tags; exact writes replace them. Stale guards, unsupported effects
and incomplete budgets withhold the complete proof. Deleted records do not
inflate size bounds. Every structural branch and runtime call remains.

The published gate passes **95 complete programs** and **eleven lifetime
sanitizer variants**, with both modes, explicit/deduced GCC/Clang, forged/rerun
controls and sixteen new discriminating mutations. Eight new refusal families
cover missing tags, wrong Map/key guards, stale has values, mutation inside an
arm, incompatible tags and literal predicates. The long String getter runs only
false during startup; later saved C++ callables use both flags and own both
selected strings through overwrite/delete, independent reentry and final Map
release. Native budgets first complete at **18690/19232/19232/10792**, checking
**29/31/31/29** cutoffs, with no natural speculative rollback interval.
Log: `/tmp/ctcompile-guard-focused.log`.

Host units pass **44 rows each in source/prepared form**, all **2866/3004**
guarded and **2504/2629** conditional budget cutoffs, exact endpoints and live
forged read/guard edits. Local mixed Maps pass **35 observations and seventeen
refusals** across both storage layouts, Node/interpreter, GCC/Clang and
ASan/UBSan. Seven targeted lit cases pass in **28.21 seconds**. Initial focused
CTest passes **12/12 in 30.69 seconds**. Inspected
`/tmp/ctcompile-guard-string.cpp`: real `map_has` and branch, owning String
selection and saved copy through subsequent writes/deletes; no Script symbols
or interpreter context.

The parallel copy proof admits only exact fresh ordinary own-data source and
target objects. It charges a source snapshot before writes, records copied
edges separately from direct Stored witnesses and preserves every historical
copy edge for cycle refusal. Runtime copy lookup can invoke getters in general;
this proof excludes descriptors, accessors, prototypes and unknown effects.
Arrays/external endpoints remain refused. No native ownership admission changes.

Copy units pass **39 rows, eleven key controls, seventeen live states, one
wide snapshot, one missing-lattice control and 2794 retention budget cutoffs**,
plus five path-explosion cutoffs. Three executed functions add ten sites/instances
with five retained. All previous source families retain their expectations.
All four oracles report zero violations; expanded-fixture precision is **29/41**,
while Bootstrap/p5/Phaser remain **0/64, 0/16, 0/20**. Combined focused CTest passes
**12/12 in 30.19 seconds**. Log: `/tmp/ctcompile-guard-copy.log`.
Homebrew clang-format **22.1.8** passes **743 files** and whitespace checks pass.

The full generated devbox build succeeds and the **517-test CTest gate is
running**, so there is no new completed full-suite total yet. Log:
`/tmp/ctcompile-guard-full.log`. The prior completed baseline was **512/517**,
with all **372 compiler tests** passing and only `selectors`, `frames`,
`element_attrs`, `vm_async` and `early_errors` failing. Fresh complete corpus
numbers await this gate; the preceding measured native components were
Bootstrap **19/574**, p5 **39/4754**, Phaser **45/7725**, and exact Data remained
**0/7 browser, 0/7 CommonJS, 0/8 AMD**. Complete native Bootstrap initialization
is unfinished. The checkpoint does not claim the running full gate passed.

**Exact next boundary:** replace the accepted guarded ternary with
`(state.has('other') && state.get('other')) || state.get('')`. Keep the seed,
conditional deletion, saved write/read/delete chain and two standalone
`set(get(false))`, `set(get(true))` calls followed by `size()`. Node/interpreter
still give **2**, but both native modes remain **0/6**, all **eighteen calls
retained**, with no host owner proof. The ternary control is **6/6** with the same
trace. The intermediate Boolean/String `&&` result needs proof that its truthy
`||` arm retains a String; the host's single optional scalar tag currently loses
that distinction. Prove live result alternatives and connect the native proof
without selecting a startup value or using the storage schema as authority.

Fresh guarded `result || null`, nullable ternary, normalized consumer-key and
object-payload witnesses each remain **0/6**, eighteen calls, Node/interpreter
**3**. A second guarded nullable return keeps nineteen calls with the same result
and refusal. These witnesses differ from the preceding checkpoint's nullable
ones. Full next source is committed in `native-owned-global-maps.md` under
"Next boundary", and on the devbox at
`/tmp/ctcompile-guard-next/shortcircuit_same_tag.js`; measured evidence:
`/tmp/ctcompile-guard-boundary.json`. Keep observer calls as standalone statements.
Nullable carriers, object identity, export ABI and native Bootstrap Data remain
separate obligations.

## Conditional Map values and object-deletion checkpoint, 2026-09-08

Saved locally on `ctcompile-v1`: **`53b44b9`**, saved scalar Map values across
conditionals, and **`55be8e9`**, fixed own-field deletion contents and retention.
This resumes `saved_join` from **`7a4ebf9`** and the **08:16:09 synchronization
journal**. The tree started clean. Interrupted CallDirectOp recovery was already
landed in `5307abf`; its old branch is an ancestor. No history was rewritten,
browser source changed or push performed. Separate agents supplied execution
checks, escape analysis and a host-proof audit with unit controls.

The exact conditional specimen advances **0/6 -> 6/6 native** in both optimization
modes, retaining all **sixteen calls** and Node/interpreter **`trace=3`**. The
always-empty straight-line control gives **2**. Both arms of each captured-method
`scf.if` are checked, including literal predicates. Mutable Map contents
intersect across arms; saved scalar results get a tag only when both yields
independently prove the same type. Zero-result conditionals include the implicit
unchanged path. Publication and factory execution remain unconditional, raw
multi-block bodies remain refused, and nesting is bounded to 32 levels.

The host proof lives in `HostContract/CapturedMapBody.cpp`. Native preparation
recognizes selected saved-read candidates before monotone inference, while
independent presence/payload analysis supplies the actual type. The census
alone authorizes no scalar fact. Missing results, differing tags, unknown
actuals and input annotations remain insufficient.

The published gate passes **89 complete programs**, including six conditional
programs, four new missing/deleted/mixed-tag refusals and **ten lifetime sanitizer
variants**. It checks both optimization modes, GCC/Clang explicit/deduced C++,
all original calls and mutation observations. A long String case calls only
`false` during startup; saved C++ callables later use both flags, preserve two
independent strings across overwritten/deleted entries and final Map release,
and survive independent reentry. First complete budgets for the four new probes
are **17934/18476/18476/10166**, with **32/31/31/31** cutoffs and no natural
speculative rollback interval. Log: `/tmp/ctcompile-conditional-native.log`.

The local mixed-Map gate passes **27 observations and twelve refusals** under
both storage layouts, Node/interpreter, GCC/Clang and ASan/UBSan. All **seven
targeted lit cases pass in 28.11 seconds**. Host units pass **25 conditional rows
per source/prepared form**, every **2501/2626** incomplete budget, exact endpoints
and live forged-marker edits. The final host CTest passes in **3.22 seconds**.
Logs: `/tmp/ctcompile-conditional-checkpoint3.log` and
`/tmp/ctcompile-conditional-boundary.json`. The expanded local gate first exposed
a missing selected-value candidate; its implementation is included in `53b44b9`.
A mixed-tag write is rejected before the read, and its test checks that exact
stage. No native program names Script symbols or an interpreter context.

The parallel contents proof handles exact own String-field deletion on fresh
objects, preserving saved reads and every historical cycle edge. It passes
**30 deletion rows, eighteen key controls, fourteen live states, one missing-lattice
control and 1663 retention cutoffs**. Three executed functions add five sites and
two retained instances; the deleted self-cycle remains `Stored`. All four
execution oracles report zero violations. Expanded-fixture precision is **24/36**;
Bootstrap/p5/Phaser remain **0/64, 0/16, 0/20**. Combined focused CTest passes
**12/12 in 29.32 seconds**. This changes no native ownership admission.

Homebrew clang-format **22.1.8** passes **743 files** and whitespace checks pass.
The full **243-step generated devbox build succeeds**. Final CTest is
**512/517 in 887.20 seconds**: **372/372 compiler** and **140/145 browser** tests.
Only the recorded `selectors`, `frames`, `element_attrs`, `vm_async` and
`early_errors` failures remain. Lit passes **165/165 in 290.02 seconds**;
exception recovery passes in **1.18 seconds**. All four execution oracles again
report zero violations, with precision **24/36, 0/64, 0/16, 0/20**. Corpus
observations remain bounded by their existing environment and execution limits.
Fresh native components remain **Bootstrap 19/574, p5 39/4754, Phaser 45/7725**
in both modes with zero pruned. Exact Data remains **0/7 browser, 0/7 CommonJS,
0/8 AMD**. Complete native Bootstrap initialization remains unfinished.
Evidence: `/tmp/ctcompile-conditional-full.log` and
`/tmp/ctcompile-conditional-evidence.json`.

All **eighteen changed code/test paths** match committed HEAD, frozen gate input
and final devbox source. Inspected `/tmp/ctcompile-conditional-string.cpp`: the
getter selects into an owning `std::string`, retains a separate saved copy and
returns another owning read after entry overwrite/deletion; there are no Script
symbols or interpreter contexts. Checkpoint docs were committed as `e0060af`;
this final update records the completed full gate.

**Exact next boundary:** after seeding `'' -> ''` and `'other' -> 'future'`, run
`if (flag) state.delete('other');`, then select the saved value with
`state.has('other') ? state.get('other') : state.get('')` and keep the existing
write/read/delete chain. The two standalone `set(get(false))`, `set(get(true))`
calls followed by `size()` give Node/interpreter **2**, but native remains **0/6**
in both modes with **all eighteen calls retained** and no host owner proof.
Replacing the conditional deletion with `state.has('other')` gives **3** and
admits **6/6**. Keeping the conditional deletion but replacing the entire
ternary with `state.get('')` gives **1** and admits **6/6**, retaining **sixteen
calls**. Source: `/tmp/ctcompile-conditional-next/guarded_saved_read.js` on the
devbox; the complete source is in `native-owned-global-maps.md` under "Next
boundary". A constant-false ternary still checks its missing arm and is also
refused; it is not a straight-line getter control. The next proof needs live
`has`-guard membership plus a payload tag valid whenever that key is present,
retained across deletion joins. Membership alone cannot establish that tag.
Nullable results (`result || null`) and object identity payloads separately remain
**0/6** with Node/interpreter **4** and **6**. Exact Bootstrap Data remains open.

## Saved Map read/write and switch-retention checkpoint, 2026-09-08

Saved locally on `ctcompile-v1`: **`d9a4b04`**, independent scalar Map read/write
facts, and **`d370807`**, bounded switch contents and retention. This resumes
`saved_read_write` from **`11803bd`** and the **07:37:42 synchronization journal**.
The starting tree was clean. The earlier interrupted CallDirectOp recovery was
already landed in `5307abf`; the old WIP branch is an ancestor. No history was
rewritten, browser source changed or push performed. Disjoint agents supplied
the published execution gate, switch proof and read-only audit.

The exact saved-read/write specimen advances **0/6 -> 6/6 native** in both
optimization modes, with Node/interpreter **`trace=1`** and all **twelve source
calls preserved** in emitted code. A proved-present scalar read keeps its own
Boolean, Number or String tag after known source-entry mutations. A later write
uses that independent fact without narrowing the complete Map storage schema.
Entry membership, possible aliases and callee effects still invalidate mutable
contents. Branches intersect saved SSA facts. Unknown/missing reads, arbitrary
local parameters and input annotations supply no scalar evidence.

The published gate passes **83 complete programs**, including seven saved-chain
programs in both modes and all **nine lifetime sanitizer variants**. Saved
strings survive source overwrite/deletion, a second write/read, final Map
release, caller-buffer mutation and independent reentry. Three new missing or
deleted-read controls retain every call under fresh/stale Bool/String forgeries
and reruns. Wrong-tag and later-overwrite programs retain their real Boolean
result (**2**); saved String/Boolean/Number witnesses return **1**.
Native admission first completes at **8150/8340/8334/8871** steps for the four
new budget probes, with **30/32/32/32** cutoffs checked and no natural speculative
rollback interval. Log: `/tmp/ctcompile-saved-gate2.log`.

The local mixed-Map gate passes **21 observations** across associative and
ordered storage, with Node/interpreter, GCC/Clang, explicit/deduced output and
ASan/UBSan. Ten live refusal controls pass. The new missing-read writeback is
rejected at optional storage admission, before the mixed-read diagnostic; its
test now checks that precise refusal. All **seven targeted lit cases** pass in
**28.28 seconds**, including CTJS-only binding-time and partial-evaluation paths.
The combined focused CTest gate passes **12/12 in 29.05 seconds**.
Log: `/tmp/ctcompile-saved-checkpoint.log`. No native program contains Script
symbols or an interpreter context.

Switch contents enumerate default and every case with exact independent
origin/container/frame states and successor operands. Unsupported paths, loops,
external values, cycles and incomplete budgets retain original verdicts. The
gate passes **21 switch rows, six live states, four malformed controls, 1526
retention cutoffs and five path-explosion cutoffs**. All four execution oracles
report zero violations. Precision stays **22/33** for the fixture and **0/64,
0/16, 0/20** for Bootstrap/p5/Phaser. This adds no native ownership admission;
loops, external contents and native lifetime consumers remain separate work.

Homebrew clang-format **22.1.8** passes **742 files** and whitespace checks pass.
The full **243-step generated devbox build succeeds**. CTest finishes
**512/517 in 864.73 seconds**, comprising **372/372 compiler** and **140/145
browser**. Only the recorded `selectors`, `frames`, `element_attrs`, `vm_async`
and `early_errors` failures remain. All **165/165 lit cases** pass in **264.08
seconds**; source exception recovery passes in **1.16 seconds**.
Log: `/tmp/ctcompile-saved-full.log`; evidence:
`/tmp/ctcompile-saved-evidence.json`. All eleven changed code/test paths
byte-match the final devbox input and committed source; all thirteen frozen
code/test/document paths matched the implementation checkpoint.

Fresh component coverage remains **19/574 Bootstrap**, **39/4754 p5** and
**45/7725 Phaser** in both optimization modes, with zero pruned functions.
Exact Data remains **0/7 browser**, **0/7 CommonJS**, **0/8 AMD**. These are
compile-admission counts, not complete native initialization or execution.
The inspected `/tmp/ctcompile-saved-string.cpp` makes two owning `std::string`
copies, retaining each across its source entry's overwrite and deletion.
The second copy returns by value to the live setter. No Script symbols,
interpreter context or collector occur in the emitted program.

**Exact next native boundary:** selecting a saved value with
`flag ? state.get('other') : state.get('')` remains **0/6 native** in both modes,
with **all sixteen calls retained** and **no host owner proof**. Seed `''` with
`''` and `'other'` with `'future'`, select the saved value in `get(flag)`, then
run the existing write/read/delete chain. Call `set(get(false))` and
`set(get(true))`, then observe `size()`: Node/interpreter agree on **3**.
Replacing the selection with `state.get('')` yields **2** and admits **6/6**.
The next proof must handle live control-flow joins in the host method body as
well as native scalar facts; an observed startup branch cannot authorize future
calls. `HostContract/Values.cpp` currently requires a single-block method
and does not admit truthy/branch/yield operations in that body. Keep the two
calls as standalone statements: placing their results in a weighted numeric
observer also loses ownership for the straight-line control. Final evidence:
`/tmp/ctcompile-saved-boundary-final.json`; exact sources:
`/tmp/ctcompile-saved-next/saved_join.js` and `saved_join_always_empty.js` on the
devbox. Exact Bootstrap Data and complete native initialization remain unfinished.

## Current mixed-Map and object-contents checkpoint, 2026-09-08

Saved locally on `ctcompile-v1`: **`6e68d3f`**, closed mixed Map keys/payloads
with independent exact read evidence. This resumes the unfinished native
boundary from **`6e7dd81`** and the **06:13:42 synchronization journal**.
The starting tree was clean; the interrupted CallDirectOp recovery was already
landed in `5307abf`, and `codex-wip-20260907` is an ancestor. No history was
rewritten, browser source changed or push performed.
The parallel fixed own-property contents increment is saved as **`0a3000a`**.
Two implementation agents and a separate audit supplied disjoint work; service
interruptions were recovered before their results were integrated.
Integration repairs are **`c0777fb`** (semantic Map tags remain usable by
CTJS-only analysis callers) and **`2086c63`** (printing comparisons preserve
genuine program headers).

The three preceding `result_seeded_mixed_contents`, `result_seeded_join_reseed`
and `result_seeded_bool_string_contents` probes advance **0/6 -> 6/6 native**,
with Node/interpreter traces **2/3/2** and all **9/10/9 calls retained**.
Storage uses exact Bool/Number or Bool/String `std::variant` alternatives.
Each mixed read separately proves membership and a scalar payload from the
last literal write on every path. Schema inference still retains every stored
alternative. Key comparison preserves false versus zero and numeric SameValueZero;
string reads return owning copies. Unknown payloads and missing reads refuse.

The published gate passes **76 complete programs** with Node/interpreter and
explicit/deduced GCC/Clang agreement, no Script symbols and all **eight lifetime
sanitizer variants**. Native admission first completes at **7658/7910/7658/8367**
steps for the three original mixed specimens and saved-string specimen, with
**30/30/30/31** checked cutoffs and no natural speculative rollback interval.
The new local gate passes **nine observations across associative/ordered Maps**
and **seven refusal controls**, including possible receiver aliases, callee
writes, different branch tags, deletion, nonliteral payloads and forged facts.
A dead-alternative case checks that homogeneous emission retains proved presence.
The existing representation gate now passes **14 observations**, including the
unchanged literal mixed-storage source. All **seven targeted lit cases** pass.
Logs: `/tmp/ctcompile-mixed-native2.log`, `/tmp/ctcompile-mixed-checkpoint2.log`
and `/tmp/ctcompile-mixed-lit-final.log`.

The formatter passes **742 files** with Homebrew clang-format **22.1.8**.
The final combined focused CTest gate passes **12/12 in 29.04 seconds**. All
**29 changed code/test paths** byte-match committed and frozen input. The full
**243-step generated devbox build succeeds**; CTest finishes **512/517 in
832.07 seconds**, comprising **372/372 compiler** and **140/145 browser**.
Only the same `selectors`, `frames`, `element_attrs`, `vm_async` and
`early_errors` failures remain. All **165/165 lit cases** pass in **236.87
seconds**, and exception recovery passes in **1.16 seconds**.
Final log: `/tmp/ctcompile-mixed-full2.log`; extracted evidence:
`/tmp/ctcompile-mixed-evidence.json`.

The initial full run exposed native dialect loading in standalone binding-time
and partial-evaluation callers. Payload proofs now retain semantic tags and
create native types only during type inference. All seven failing cases and
the new mixed test pass **8/8 in 10.02 seconds**, including valid forged tags.
The printing gate removes only the include adjacent to the pin macro, retaining
every genuine program include in its exact comparison. All **39/39 printing
CTests** pass in **28.40 seconds**. Logs: `/tmp/ctcompile-mixed-fix.log` and
`/tmp/ctcompile-print-gate.log`. The failed initial full run is retained as
`/tmp/ctcompile-mixed-full.log`; it is superseded by the complete rerun above.

Fresh native component coverage remains **19/574 Bootstrap**, **39/4754 p5**
and **45/7725 Phaser** in both optimization modes, with zero pruned functions.
Exact Data remains **0/7 browser**, **0/7 CommonJS**, **0/8 AMD**. Full native
Bootstrap initialization remains unfinished. Final emitted C++ inspected at
`/tmp/ctcompile-mixed-saved-string.cpp` copies a proved `std::string` from the
finite variant, overwrites and deletes its source entry, then returns the
owning saved value. No interpreter context, collector or Script symbols occur.

**Exact next native boundary:** `saved_read_write` remains **0/6 native** in
both optimization modes with complete host ownership, all **12 calls retained**
and Node/interpreter **`trace=1`**. Starting from
`payload_result_sources()["result_seeded_bool"][0]`, replace the getter body with:

```js
state.set('', '');
const saved = state.get('');
state.set(false, true);
state.set(false, saved);
const result = state.get(false);
state.delete(false);
return result;
```

The native proof clears payload evidence at the nonliteral write. Propagate
independent scalar facts through the saved read/write chain while retaining
every mutation, alias invalidation and missing-result refusal. Replacing that
write with `true` yields **2** and admits **6/6**; moving the second read after
deletion also yields **2**, loses host result evidence and remains **0/6**.
Fresh final evidence: `/tmp/ctcompile-mixed-boundary-final.json`; source on the devbox:
`/tmp/ctcompile-mixed-next/saved_read_write.js`.

The independent contents query now tracks exact own String properties on fresh
objects, including saved reads, object/array aliases and return reachability.
The retention consumer checks every write across both container kinds, so
transient or mutually exclusive cycles preserve original escape verdicts.
Keys are bounded to 256 bytes; missing properties, `__proto__`, prototypes,
accessors, external values, loops and incomplete proofs refuse. This adds no
native ownership admission. The gate passes **31 object rows, eleven keys,
twelve live states and 1269 retention cutoffs**, plus **38 array contents
rows/14 keys**, 20 retention rows/661 cutoffs, 26 frame rows/444 cutoffs and
17 conditional rows/877 cutoffs with five path-explosion controls.
All four execution oracles report zero violations; precision remains **22/33**
for the fixture and **0/64, 0/16, 0/20** for Bootstrap/p5/Phaser.
Log: `/tmp/ctcompile-object-focused2.log`.

Review exposed an older unsound String-array-index assumption. A literal or
object-loaded String `"0"` write yields **Node 2 versus interpreter 1** because
the runtime retains the old element; its String read yields **Node 1 versus
interpreter undefined**. Complete contents now refuses String array reads
and writes, retaining numeric indices and own String object fields. Direct,
loaded and live Number/String key changes under forged markers guard the fix.
Runtime sources and source oracle expectations remain unchanged; the mismatch
is in the synchronization journal and `/tmp/ctcompile-object-key-oracle.json`.
Next escape work remains loops/external values and native lifetime consumers.

## Preceding Map payload and conditional-array checkpoint, 2026-09-08

Saved locally on `ctcompile-v1`: **`d16f763`** (homogeneous boolean and owning
string Map payloads) and **`ab10057`** (bounded conditional-array retention).
The starting tree was clean at `7dde504`; this resumes the exact Bool/String
boundary in that handoff and the **05:27:53 synchronization journal**. The
interrupted CallDirectOp work was already recovered in `5307abf`, and
`codex-wip-20260907` is an ancestor. The old unmerged closure-nesting fix is
also already present in `ClosureLifting/Bindings.cpp`. No history was rewritten.
Two disjoint implementation agents supplied the execution gate and array work;
a third audited the lowering boundary. No browser source changed or push occurred.

`result_seeded_bool` and `result_seeded_string` advance **0/6 -> 6/6 native**,
with complete host owner/result proofs and Node/interpreter **`trace=2`**.
The existing owning Map storage now accepts `bool` and `std::string` values.
Ordinary reads preserve false/empty versus missing through existing nullable
carriers; proved-present reads and string value snapshots return owning copies.
Boolean value snapshots and mixed key/payload schemas remain refused. Every
source call, runtime mutation, lookup and consuming argument remains executable.

The published gate passes **69 complete native programs** with Node/interpreter
and explicit/deduced GCC/Clang agreement, no Script symbols, and all **seven
sanitizer lifetime variants**. Saved strings survive overwrite, deletion,
Map destruction, caller-buffer mutation and independent reentry. False, empty,
overwritten and saved-string witnesses return **1**; blinded controls return
**2**. Two deleted-payload refusals preserve all calls under fresh/stale forged
markers and reruns. Native admission first completes at **7474/7474/8008** steps
for the original boolean/string and saved-string programs, checking **32/32/30**
cutoffs. No natural speculative rollback interval was observed. Source/prepared
cardinality proof budgets remain **2278/2372**.

The local Map representation gate passes **13 observations**, including five
new observations independently checked against Node/interpreter. GCC/Clang,
explicit/deduced forms and ASan/UBSan cover missing reads, false/empty values,
long embedded-NUL strings, returned nested Maps and copied value snapshots.
All six targeted lowering lit tests pass after correcting new harness/FileCheck
expectations; no production proof was weakened. Logs:
`/tmp/ctcompile-payloads-native.log`, `/tmp/ctcompile-payloads-lit.log` and
`/tmp/ctcompile-payloads-lit-final.log`.

Array contents now enumerate bounded acyclic `cf.br`/`cf.cond_br` paths with
independent exact origin, array-slot and frame state. Both edges are checked,
and joins replay each predecessor so a strong overwrite cannot erase another
path's aliases. Returned reachability and conservative all-write cycle checks
cover every exit. Loops, unsupported effects and incomplete work still refuse;
an external truthy predicate does not prove an external element or root safe.
The combined focused gate passes **12/12 CTests in 29.38 seconds**: the new
**17 conditional rows, five live states, 738 retention cutoffs and five bounded
path-explosion controls**, plus the existing 35 contents/20 retention/26 frame
rows. Four execution oracles report zero violations. Fixture precision remains
**22/33**; Bootstrap/p5/Phaser remains **0/64, 0/16, 0/20**. Native ownership
consumers remain separate. Log: `/tmp/ctcompile-payloads-focused.log`.

The full generated devbox build succeeds (**243 build steps**). The combined
gate finishes **512/517 CTests in 808.06 seconds**: **372/372 compiler** and
**140/145 browser**. Its five failures exactly match the preceding gate:
`selectors`, `frames`, `element_attrs`, `vm_async` and `early_errors`. No browser
source changed. All **164/164 lit cases** pass in **208.85 seconds**, and source
exception recovery passes in **1.15 seconds**. Log:
`/tmp/ctcompile-payloads-full.log`; extracted evidence:
`/tmp/ctcompile-payloads-evidence.json`.

Fresh native component coverage remains **19/574 Bootstrap**, **39/4754 p5**
and **45/7725 Phaser**, in both optimization modes with zero pruned functions.
Exact Data remains **0/7 browser**, **0/7 CommonJS** and **0/8 AMD**. Complete
native Bootstrap initialization remains unfinished.

The frozen source passes `tools/format.sh --check`: **742 files** using
Homebrew clang-format **22.1.8**. All **19 changed code/test paths** byte-match
the frozen devbox input and committed implementation. Emitted C++ inspected at
`/tmp/ctcompile-payloads-saved-string.cpp` copies the read into a `std::string`,
then overwrites and deletes the source entry in the same owning `std::map`,
returns the saved value and passes it to the live setter. No interpreter context,
collector or Script symbols occur in the native program.

**Exact next native boundary:** `result_seeded_mixed_contents`,
`result_seeded_join_reseed` and `result_seeded_bool_string_contents` remain
**0/6 native** despite complete host owner/result proofs. Fresh Node/interpreter
traces are **2/3/2**, with all **9/10/9 source calls** retained. Both keys and
values have mixed schemas: Bool/Number or Bool/String. Extend closed finite key
comparison, storage, set conversion and exact read/result facts together;
one final get's tag cannot narrow the entire Map schema. Preserve SameValueZero,
false versus numeric keys, owning strings and independent presence evidence.
Evidence: `/tmp/ctcompile-payloads-boundary.json`.

Unseeded published reads still need independent result evidence across the
complete family. Current invocations do not authorize arbitrary future callers.
Exact Bootstrap Data, realm owners, complete throwing-call components and live
caller-depth/reentry proofs remain unfinished. Array loops, external stored
values and other containers are still outside the complete contents query.

## Preceding distinct-key Map and array-chain checkpoint, 2026-09-08

Saved locally on `ctcompile-v1`: **`c900f84`** (distinct-key Map size bounds)
and **`d863dc3`** (array retention through unconditional branch chains). The
starting tree was clean at `310c9a3`. The interrupted CallDirectOp/frame work
was already recovered and gated in `5307abf`, `e8d5cdb` and `f063455`; this
session continued the exact `seeded_size_two_entries` boundary recorded in that
handoff and the 04:49 synchronization journal. Older unmerged branches were
audited and are browser work, not an unfinished native recovery. Main work and
two disjoint implementation agents were integrated; a third audited the next
payload and source-exception boundaries. No browser source changed or push occurred.

The published Map specimen now seeds keys 0 and 1, deletes `state.size`, then
returns `state.get(1)`: **0/5 -> 5/5 native**, with Node/interpreter **`trace=2`**.
Host result and native presence analyses independently count a pairwise-distinct
subset of definite keys in the same runtime Map. Each read examines at most
**64 candidates**; saved lower bounds survive later mutation. Aliasing SSA keys,
signed zeros and NaN encodings cannot inflate cardinality. Host work is charged
per candidate/comparison, and incomplete proofs expose no callable/property facts.
The 64/65-candidate boundary is intentional, not the next implementation target.

The gate passes **63 complete native programs**, eight size programs and ten
size-key refusals, with explicit/deduced GCC/Clang, Node/interpreter agreement,
no interpreter symbols and all six existing sanitizer lifetime variants.
Runtime calls and result transport remain intact. Source/prepared cardinality
proofs check all **2278/2372** incomplete budgets; native completion is
**5931/9391** for two entries/saved emptied state, with **30** cutoffs each.
The separate nested-Map presence lit gate passes **seven positives and fourteen
refusals**. Logs: `/tmp/ctcompile-cardinality-focused2.log` and
`/tmp/ctcompile-cardinality-lit2.log`.

The array query now follows only acyclic `cf.br` chains with one predecessor per
destination and exact value/frame forwarding. Joins, conditional flow, loops,
unknown values/effects and incomplete work still refuse. The combined focused
gate passes **12/12 CTests in 28.45 seconds**: **35 contents rows, 20 retention
rows, 26 frame rows**, twelve live frame/control-flow states, **500 retention
and 387 frame budget cutoffs**, and four execution oracles with zero violations.
Fixture precision stays **22/33**; Bootstrap/p5/Phaser precision stays
**0/64, 0/16, 0/20**. Native ownership consumers remain separate.

The full generated devbox build succeeds (**243 build steps**). The combined
gate finishes **512/517 CTests in 788.70 seconds**: **372/372 compiler** and
**140/145 browser**. Its five failures remain the recorded browser `selectors`,
`frames`, `element_attrs`, `vm_async` and `early_errors`; no browser source
changed. All **164/164 lit cases** pass in **187.86 seconds**, and source
exception recovery passes in **1.20 seconds**. Log:
`/tmp/ctcompile-cardinality-full.log`; extracted evidence:
`/tmp/ctcompile-cardinality-evidence.json`.

Fresh native component coverage remains **19/574 Bootstrap**, **39/4754 p5**
and **45/7725 Phaser**, in both optimization modes with zero pruned functions.
Exact Data remains **0/7 browser**, **0/7 CommonJS** and **0/8 AMD**. Complete
native Bootstrap initialization remains unfinished.

The current frozen source passes `tools/format.sh --check`: **742 files** with
Homebrew clang-format **22.1.8**. All **12 changed code/test paths** byte-match
the frozen devbox input and committed implementation. Emitted C++ at
`/tmp/ctcompile-cardinality-two-entries.cpp` was inspected: the two seeds, size
read, deletion and typed lookup execute against the same owning `std::map`,
with no interpreter context, collector or Script symbols.

**Exact next native boundary:** `result_seeded_bool` and
`result_seeded_string` in the existing Map gate remain **0/6 native** despite
complete host owner/result proofs; Node/interpreter both produce **`trace=2`**.
A fresh post-gate probe retains **all eight source calls** in each refusal and
confirms the completed size specimen at **5/5**, with its owner proof and
`trace=2`. Evidence: `/tmp/ctcompile-cardinality-boundary.json`.
`LoweringSupport.cpp` refuses homogeneous Bool/UTF8 String Map payload carriers.
Extend carrier selection/spelling, `replaceMap` construction and generic Map
reads together, reusing owning string/nullable scalar types. Preserve false and
empty-string versus missing, saved string lifetime and mixed-payload refusals;
string-value snapshots need owning copies or an explicit refusal. Full native
Bootstrap Data, realm owners and future external callers remain unfinished.

Source exceptions next need one transaction covering the complete call
component, standalone throwing helpers, invoke admission/emission and owning
saved state, including pruning only provably dead invocation tuple slots.
A live whole-entry caller-depth/reentry proof must discharge frame-entry
failure separately: the public AOT contract returns `CT_AOT_FAILED`, never a
catchable JS payload. The existing 32-level analysis bound does not prove the
runtime caller stack safe. Retain complete rollback until every component
member lowers. Array contents next need conditional/join/loop flow, external
values and other containers before broader ownership consumers.

## Preceding Map.size and imported-array checkpoint, 2026-09-08

Saved locally on `ctcompile-v1`: **`e8d5cdb`** (nonempty Map.size snapshots)
and **`f063455`** (imported array frame/root retention). This session resumed
the dirty compiler work recorded in the synchronization journal at 03:52/04:00,
abandoned at 04:02:32. The earlier reverted source-completion thread was already
recovered in `5307abf`/`5b0b602`, including the corrected `CallDirectOp` builder;
no branch was rewritten. Two agents completed disjoint proof/test work and a
third audited the next boundary. No browser source changed; no push occurred.

The [published Map gate](native-owned-global-maps.md) now admits
`state.set(0, 1); state.delete(state.size); return state.get(0)` as the producer
in `host.slot.set(host.slot.get())`: **0/5 -> 5/5 native**, preserving
Node/interpreter **`trace=1`**. Host result and native presence proofs each
establish a nonempty size snapshot from a definite entry in the actual Map.
Saved numbers survive later mutations; initial zero sizes, equal positive keys
and equal snapshots cannot borrow disjointness. All runtime calls remain.

**58 complete native programs** pass Node/interpreter and explicit/deduced
GCC/Clang execution, including all six existing sanitizer lifetime variants
and the no-interpreter-symbol gate. Three new size programs and three
observationally distinct refusals cover saved/current size, empty Maps and
aliasing. Source/prepared host proofs check all **2212/2299** incomplete
budgets. The new presence lit test passes three positives and seven refusals.
Logs: `/tmp/ctcompile-size-focused.log`, `/tmp/ctcompile-size-native.log`,
`/tmp/ctcompile-size-lit.log`. Emitted C++ was inspected at
`/tmp/ctcompile-size-dynamic-delete.cpp`: seed, size, delete and typed lookup
execute against the same owning Map, with no VM context or collector.

The [array retention query](escape-load-evidence.md) validates entry-first
frame creation, exact active roots and a matching exit immediately before
return. A no-successor/no-region entry scan proves the importer's extra default
return block unreachable independently of solver flags. Frame-entry failure
still precedes tracked allocations; this is no native nonthrowing permission.
The gate passes **8/8 CTests in 8.45 seconds**, including **19 frame rows,
eight live states and 248 budget cutoffs**, all four escape units and all four
execution oracles with zero soundness violations. Nine new source functions
exercise **19 sites / 21 instances / 11 retained instances**. On this same
expanded fixture, observed confined-site precision advances **18/33 -> 22/33**
after supporting the dead fallback block. Bootstrap/p5/Phaser observed precision
remains **0/64, 0/16, 0/20** in these script-mode probes. Log:
`/tmp/ctcompile-size-escape2.log`. Native ownership consumers remain separate.

The full generated devbox build succeeds (**411 build steps**). The combined
gate finishes **512/517 CTests in 770.50 seconds**: **372/372 compiler** and
**140/145 browser**. Its five failures are the previously recorded browser
`selectors`, `frames`, `element_attrs`, `vm_async` and `early_errors`; no browser
source changed. All **164/164 lit cases** pass in **174.21 seconds**, and the
restored source-exception recovery test passes in **1.15 seconds**. Log:
`/tmp/ctcompile-size-full.log`; extracted evidence:
`/tmp/ctcompile-size-evidence.json`. All **16 changed code/test paths** byte-match
the frozen snapshot and committed implementation.

Fresh native component coverage remains **19/574 Bootstrap**, **39/4754 p5**
and **45/7725 Phaser**, in both optimization modes with zero pruned functions.
Exact Data remains **0/7 browser**, **0/7 CommonJS** and **0/8 AMD**. Complete
native Bootstrap initialization remains unfinished.

The frozen current source passes `tools/format.sh --check`: **742 C++ files**,
Homebrew clang-format **22.1.8**, matching the preceding accepted checkpoint.
The ignored local toolchain's clang-format 23 instead flags nine baseline files;
those were left untouched. Whitespace checks pass, and all session changes are
committed locally without a push.

**Exact next native boundary:** `seeded_size_two_entries` seeds keys 0 and 1,
deletes `state.size`, then returns `state.get(1)`. It is freshly measured at
**0/5 native**, no owner proof and every source call intact; Node/interpreter
both produce **`trace=2`**. Evidence: `/tmp/ctcompile-size-boundary.json`.
Derive a bounded cardinality lower bound from independently distinct definite
keys in both analyses; the number of facts is not a size proof when keys can
alias. Unseeded reads, mixed payload carriers, exact Bootstrap Data, general
realm owners and future-call contracts remain unfinished. Source throwing-call
work next needs complete native component admission and owning payload/state
emission; the restored normal-return inference alone cannot erase fallible
frame entry. The array query next needs reachable control flow, external values
and other containers before broader ownership consumers.

## Preceding per-key Map, array retention and transitive-call checkpoint, 2026-09-07

Saved locally on `ctcompile-v1`: **`ec2dc20`** (bounded array retention
consumer), **`327a3c5`** (disjoint per-key Map facts), and **`fd90ea9`**
(transitive source invocation completions). Main native work and two independent
agent implementations are integrated. No browser source changed; no push occurred.

The [published Map boundary](native-owned-global-maps.md) now accepts
`get() { state.set(0, 1); state.set(1, 2); return state.get(0); }` as the
producer in `host.slot.set(host.slot.get())`, advancing **0/5 -> 5/5 native**
with Node/interpreter `trace=1`. Bounded per-key facts preserve earlier payloads
across independently disjoint writes/deletes. SameValueZero treats both zero
encodings and every NaN payload as equal. Possibly aliasing mutations discard
old facts before a set installs its own independently proved payload tag.
The separate native presence analysis invalidates cached `has` observations
consistently; transitive call summaries remain conservative. No call or lookup
is evaluated away, and each invocation begins with unknown contents.

The native gate passes **47 complete programs**: fifteen **4/4**, twenty-six
**5/5**, six **6/6**, matching Node/interpreter and explicit/deduced GCC/Clang
execution with no linked interpreter symbols. Seven new programs cover disjoint
writes/deletes, overwritten earlier keys, reseeding, nine live keys and
string/boolean keys. All six existing Map/table/callable lifetime variants pass
ASan/UBSan, use-after-scope/return and leak checks. Nine seeded proof refusals
retain every source call; string/boolean/mixed payload carriers remain separate.

The combined Map/escape gate passes **7/7 CTests in 22.54 seconds**;
log: `/tmp/ctcompile-map-keyfacts-units.log`. Per-key source/prepared host proofs
complete at **2145/2230** steps; owner proofs at **5072/4962**. Disjoint-delete
owner proofs complete at **5116/5007**. Every smaller budget withholds the entire
proof. First source native completion is **5721** for the earlier key and
**5700** for the disjoint delete, with **30/31** checked cutoffs and no natural
speculative rollback interval. These are work limits, not speedup claims.
Native execution log: `/tmp/ctcompile-map-keyfacts-focused2.log`.

The [array retention consumer](escape-load-evidence.md) independently recomputes
complete local contents, all-write acyclicity and return reachability before
changing an unretained `Stored` site to `Confined`. Returned children keep their
original storage witness, including saved reads and loaded aliases. Cycles,
transient cycles, unknown effects, missing lattices and incomplete budgets
preserve the original verdicts. **18 retention rows, seven live states and 454
budget cutoffs** pass alongside the **31 contents rows, 14 key controls and 209
sink-table rows**. All four execution oracles report zero violations; corpus
precision is unchanged. Native admission does not consume these verdicts.

The [source invocation prerequisite](native-source-invocations.md) follows a
complete acyclic source call family with exact stacked formal/actual contexts.
Normal returns belong to the selected callee; descendant throws can supply its
unwind. Unknown effects, recursion, depth above 32 and incomplete work refuse
without changing the original function. Four transitive cases recover
**1/2/1/2 invokes** with **7/10/9/12 original checks** available for rollback,
at **3737/6601/5106/9031** steps. All **5106** transitive-budget prefixes,
**3534** source-binding prefixes, **937** effect prefixes and nine new refusal
controls pass. Recovery CTest passes in **1.19 seconds**, then the complete lit
CTest in **159.07 seconds**; log: `/tmp/ctcompile-map-keyfacts-invocations.log`.
Ordinary native throwing-call admission and payload/state emission remain
unfinished.

The full frozen generated devbox gate passes **475/475 CTests in 720.98
seconds**: **367 compiler / 108 browser**, including **163/163 lit cases in
149.67 seconds**. Log: `/tmp/ctcompile-map-keyfacts-full.log`. All thirteen
changed compiler code/test paths byte-match the three committed implementations.
Browser bytes are the committed `3494d44` baseline (unchanged since `7a755dd`),
excluding Claude's unmerged WPT branch and ten browser carryover paths. Compiler
formatting and whitespace checks pass; the whole-tree formatter flags only
untouched browser `style/selector.hpp`, `DOM/document.cpp` and
`Style/css/selector.cpp`.

Fresh native component counts remain **19/574 Bootstrap**, **39/4754 p5** and
**45/7725 Phaser**, in both optimization modes with zero pruned functions.
Exact Data remains **0/7 CommonJS**, **0/7 browser** and **0/8 AMD**. These are
component counts, not complete applications. Evidence:
`/tmp/ctcompile-map-keyfacts-evidence.json`. The emitted
`/tmp/ctcompile-map-keyfacts-parameter.cpp` was reviewed: both seed writes and
the typed `map_get_present` remain, and the result flows into the runtime setter.
There are no interpreter symbols, VM contexts or collector dependencies.

**Exact next native boundary:** `seeded_dynamic_write` replaces the disjoint
write with `state.set(state.size, 2)` before `return state.get(0)`. It remains
**0/5 native** with no owner proof and every source call intact; fresh Node and
interpreter runs both produce `trace=1`. Its runtime key may alias key 0, so the
proof discards the earlier payload fact despite both
payloads being numeric. Next prove a complete type join across possible
same-key overwrites independently of presence; a possibly aliasing delete
still needs its own presence proof. Unseeded gets, unsupported payload carriers,
full Bootstrap Data, realm ownership and future external callers remain open.
Source invocation integration still needs complete native component admission
and owning payload/state emission. The array consumer next needs checked
imported frame/root bookkeeping and an executed source/oracle witness; raw
imports contain `ctjs.frame_enter`, which the complete query still refuses.
Broader contents and native ownership consumers remain unfinished.

## Preceding seeded-Map, contents and source-binding checkpoint, 2026-09-07

Saved locally on `ctcompile-v1`: **`6986611`** (complete bounded local array
contents), **`b2466a0`** (seeded Map.get results across published calls), and
**`e4b2d5f`** (live source callee bindings before invocation recovery). The main
native boundary and three disjoint agent workstreams were integrated. Two agents
hit service limits after implementation; their work was retained and validated.
No browser files were changed and no push was performed.

The [published Map boundary](native-owned-global-maps.md) now accepts
`get() { state.set(0, 1); return state.get(0); }` as the producer in
`host.slot.set(host.slot.get())`, advancing **0/5 -> 5/5 native** with
Node/interpreter `trace=1`. Each invocation begins with unknown contents. A
bounded last-write fact connects an exact SSA/equal constant key to the payload's
independently proved primitive tag. A later set replaces the fact; delete clears
it. The complete body/use proof must finish before publishing a result tag.
Native Map preparation separately proves instance/key presence and infers the
whole Map schema. No lookup or call is evaluated away.

The gate passes **40 complete native programs**: fifteen **4/4**, nineteen
**5/5**, six **6/6**, matching Node/interpreter and explicit/deduced GCC/Clang
execution with no linked interpreter symbols. Five new producer variants cover
seeded reads, repetition, overwrites, growing runtime keys and distinct formal
actuals. The old seeded local nullable-key boundary also advances **0/4 -> 4/4**.
All **six lifetime variants** pass ASan/UBSan, use-after-scope/return and leak
checks; the new growing getter exercises independent saved/fresh Maps through
1024 further calls each. Seven new presence refusals retain all source calls;
three carrier refusals keep complete ownership but reject string/boolean/mixed
Map payloads. Fresh forged presence markers cannot authorize a result.

Focused validation passes **7/7 CTests in 18.26 seconds**, then all forty native
programs/lifetime variants. Log: `/tmp/ctcompile-map-presence-integrated.log`.
The seeded source/prepared host proofs check every incomplete budget at
**2075/2153** steps; source/prepared owner proofs at **5016/4899**. First native
completion is **5558**, with **30** cutoffs and no natural speculative rollback
interval. This does not claim a performance improvement.

The separate [array contents prerequisite](escape-load-evidence.md) recomputes
exact own elements/read origins/overwrites and return reachability for fresh
local arrays in one straight-line block. It follows loaded array aliases and
bounded cycles without choosing an owner. Unknown values, holes, calls, throws,
publication, prototypes and control flow refuse with no proof records.
**31 contents rows, 14 index controls and seven live mutation states** pass,
including every incomplete budget, alongside the unchanged **209 escape rows**
and all four zero-violation execution oracles. No escape verdict, type lattice
or native admission consumes this new query yet.

The [source invocation prerequisite](native-source-invocations.md) now proves
initialized immutable source callee bindings before dropping their status edges
in `EffectCheckedInvocations`. It checks all current declarations, loads, uses,
call identities and module effects; bounded leaf completion facts preserve
primitive payload and argument state. The three source cases recover
**1/2/1 invokes** with **7/10/9 original checks** available for rollback, at
**3521/6068/4875 steps**. All **3521** assignment-budget prefixes, **937**
effect-budget prefixes, nineteen live binding mutations and uncalled-throw
controls pass. Focused CTests pass **2/2 in 130.38 seconds**, including
**163/163 lit cases**; log: `/tmp/ctcompile-map-presence-invocations.log`.
Ordinary native throwing-call admission remains unchanged.

The full frozen generated devbox build passes **475/475 CTests in 693.86
seconds**: **367 compiler / 108 browser**, including **163/163 lit cases in
130.49 seconds**. Log: `/tmp/ctcompile-map-presence-full.log`. Its compiler bytes
match the committed implementation. Browser bytes are committed `9b6c0d4`
baseline content (unchanged since `7a755dd`), excluding Claude's unmerged WPT
branch and ten browser carryover paths. Compiler formatting passes; the
whole-tree check flags only untouched browser `style/selector.hpp`,
`DOM/document.cpp` and `Style/css/selector.cpp`.

Fresh native component counts remain **19/574 Bootstrap**, **39/4754 p5** and
**45/7725 Phaser**, in both optimization modes with zero pruned functions.
The exact Data probes remain **0/7 CommonJS**, **0/7 browser** and **0/8 AMD**;
AMD includes the delayed-factory registration function. These are component
counts, not complete native applications. Evidence:
`/tmp/ctcompile-map-presence-evidence.json` and
`/tmp/ctcompile-map-presence-bootstrap.json`. The emitted
`/tmp/ctcompile-map-presence-parameter.cpp` was reviewed: `map_get_present`
produces a typed numeric result passed to the runtime setter; the seed, lookup,
setter and final getter remain calls on the same owned Map. No interpreter
symbols or VM context appear.

**Exact next native boundary:** `seeded_earlier_key` inserts
`state.set(1, 2)` before the producer's `return state.get(0)`. The source gate
retains every call and refuses **0/5 native** with no owner proof because the
last-write fact forgets key 0. Fresh Node/interpreter runs both produce
`trace=1`. Extend to bounded per-key contents with independent key-disjointness
proofs and conservative invalidation for possibly aliasing writes/deletes.
Unseeded gets remain refused; string/boolean/mixed Map payload carriers remain
separate **0/6** boundaries despite complete host proofs. Full Bootstrap Data,
realm ownership and future external callers remain unfinished. Source invocation
integration next needs complete native call-component admission and owning
payload/state emission. The array query needs supported exposure/indirect
retention consumers before it can change escape verdicts.

## Preceding Map-result and invocation-effects checkpoint, 2026-09-07

Saved locally on `ctcompile-v1`: **`0977572`** (live invocation effect validation)
and **`c18b94b`** (published Map arguments from independent call results). Main
implementation and two parallel agent workstreams were integrated. The escape
contents agent hit a service rate limit before implementation; no contents
proof or escape verdict changed. No browser files were changed and no push was
performed.

The [published Map boundary](native-owned-global-maps.md) now accepts
`host.slot.set(host.slot.get())`, advancing **0/5 -> 5/5 native** and retaining
Node/interpreter `trace=1`. The initial getter, setter mutation and final getter
all remain runtime calls. The existing manifest, standard Map identity, owning
Map environment and typed callable carriers remain required. A complete current
call census precedes a bounded dependency worklist. Only a completed producer
body/effect/use proof publishes a primitive result tag; consumers declared
before producers wait for another pass. No recursive property-call query or
optimistic tag seeds a cyclic family. `Map.get` has no definite result tag here.

The native gate passes **34 complete programs**: fourteen **4/4**, fifteen
**5/5**, five **6/6**, comparing Node/interpreter and explicit/deduced GCC 13 and
Clang 18. Nine new variants cover nested results, reversed member order, aliases,
repeated calls, two mutating actuals, boolean results, an owning string and a
formal return. The argument-order witness observes **3**, while reversed actuals
observe **4**. Generated C++ checks preserve producer SSA operands and runtime
call order; linked binaries contain no interpreter symbols. All five existing
lifetime variants pass ASan/UBSan, use-after-scope/return and leak checks in both
forms. Nine new result-proof refusals retain all original calls. The additional
undefined-result case proves ownership but refuses the unsupported Map key at
**0/6**, retaining its prepared calls and actual-result edges.

The focused gate passes **3/3 CTests in 7.50 seconds**, followed by the complete
native program/lifetime/refusal gate. Log: `/tmp/ctcompile-map-results-integrated2.log`.
Source/prepared result proofs check every incomplete budget at **4789/4656**,
including live producer-return mutations and stale/fresh fingerprints.
Two-method, three-method and parameterized owner units complete at
**2877/2785**, **6603/6418** and **2883/2792**. First complete source admission
budgets are **1239/50004/1353/3297/3261/5336**, with **31/31/31/29/32/29**
cutoffs for ordinary, sixteen-call, growing, shared-growing, parameter and result
specimens. No natural speculative rollback interval is reached; no performance
improvement is claimed.

The parallel [source invocation increment](native-source-invocations.md) adds
`EffectCheckedInvocations` to the existing recovery transaction. It validates
normal/catch operations while their original status edges still exist, before
adopting a clone. Primitive predecessor proofs include both edges to a shared
successor and reject unknown alternatives. An unconditional numeric result is
not a nonthrowing producer proof. The unit passes thirteen live effect mutations,
all **924** incomplete work budgets, exact completion and exact rollback. The
three imported direct-call fixtures retain their original checks because their
global callee lookups lack independent live binding/getter proofs. Default
recovery and ordinary native throwing-call admission remain unchanged.

The full frozen generated devbox build passes **475/475 CTests in 674.94
seconds**: **367 compiler / 108 browser**, including **163/163 lit cases in
113.29 seconds**. Log: `/tmp/ctcompile-map-results-full.log`. Post-gate evidence
collection was rerun successfully with CMake's recorded Node executable after
the SSH environment lacked `node` on PATH; the CTest run itself passed.
All nine committed source/test/exception-document paths byte-match the frozen
gate. Browser bytes are the committed `ce728c0` baseline (unchanged since
`7a755dd`), excluding Claude's unmerged WPT branch and ten browser carryover
paths. Compiler formatting passes; the whole-tree check flags only the known
untouched browser `style/selector.hpp`, `DOM/document.cpp` and `Style/css/selector.cpp`.

Fresh native component counts remain **19/574 Bootstrap**, **39/4754 p5** and
**45/7725 Phaser**, in both optimization modes with zero pruned functions.
These are component counts, not complete native applications; exact Data stays
**0/7** per mode. Evidence: `/tmp/ctcompile-map-results-evidence.json`.
The emitted `/tmp/ctcompile-map-results-parameter.cpp` was reviewed: a typed
numeric getter result is passed to the runtime setter, and both callables own
the same Map. No VM context or collector appears.

**Exact next native boundary:** `result_seeded_map_get` changes the producer to
`get() { state.set(0, 1); return state.get(0); }`, keeping `set(get())` and the
same five functions. The fresh gate records **0/5 native**, Node/interpreter
`trace=1`, every original call retained, and no owner proof. Current named
refusals include `uses its own closure` and an unproved boxed parameter.
Add independent live contents, presence and result-type evidence
before admitting the consuming formal. Neither a completed ownership proof nor
an observed first-call value supplies that evidence. The unseeded Map.get result
also remains refused. Nullable Map keys within one method remain a separate
**0/4** carrier boundary. Exact Bootstrap Data, realm ownership, future external
callers, complete contents analysis and full native application startup remain
unfinished. Source invocation integration next needs live callee-binding effects,
the complete native call component, and owning payload/state emission.

## Preceding typed-Map and provenance checkpoint, 2026-09-07

Saved locally on `ctcompile-v1`: **`84c4b89`** (bounded load provenance),
**`ce2fd8a`** (primitive actuals for published Map methods), **`f81d803`**
(checked source invocation recovery prerequisite), **`f188373`** (typed argument
execution/lifetime gate), and **`c7a3569`** (native boundary documentation).
Three disjoint agent workstreams were integrated and validated. No browser
files were changed and no push was performed.

The [published Map boundary](native-owned-global-maps.md) now accepts
`set(key) { state.set(key, 1); return state.size; }` beside a zero-argument
getter. Calling it with `"x"` advances **0/5 -> 5/5 native**, retaining
Node/interpreter `trace=1`, under the fingerprinted manifest and standard Map
identity. The family census discovers all current calls before checking bodies,
independently classifies every actual, and retains each live SSA operand and
formal with a consistent primitive tag. The prepared Map environment precedes
explicit arguments. Existing typed owners and `std::function` carriers suffice;
there is no new runtime model, VM dependency or future-call ABI authority.

The expanded Map gate passes **25 complete programs**: fourteen **4/4**, ten
**5/5**, one **6/6**, matching Node/interpreter and explicit/deduced GCC 13 and
Clang 18 binaries. Six new variants cover string/number/boolean keys, repeated
distinct actuals, a local alias and two parameters. A fifth lifetime variant
retains typed string setter/getter callables after root/table release, changes
caller buffers, checks **1024 further mutations each** against independent
saved/fresh Maps, and observes destruction through weak witnesses. Both forms
pass ASan/UBSan, use-after-scope/return and leak checks. Ten argument refusals
retain every call operand. Missing/extra/mixed actuals, objects, callbacks,
uncalled parameterized siblings, global initialization defects and circular
sibling-result proofs remain refusals.

The focused gate passes **8/8 CTests** in **14.10 seconds**, followed by all
twenty-five native programs and five lifetime variants. Log:
`/tmp/ctcompile-arguments-integrated-focused4.log`. Every one of the **17
committed source/test paths** byte-matches the frozen full-gate snapshot.
The full frozen generated devbox build passes **475/475 CTests** in **652.00
seconds**: **367 compiler / 108 browser**, including **163/163 lit cases** in
**91.45 seconds**. The browser source is the committed `e90db7d` baseline
(the same browser tree content as `7a755dd`), excluding Claude's unmerged WPT
branch and live carryover. Log: `/tmp/ctcompile-arguments-full-gate.log`.
All compiler files pass formatting; the whole-tree check flags only the same
untouched browser `style/selector.hpp`, `DOM/document.cpp` and
`Style/css/selector.cpp`. The ten browser carryover paths remain untouched.

Measured first complete admission budgets are **1236/49476/1350/3285/3249** for
ordinary, sixteen-call, growing, shared-growing and parameterized shared
specimens, checking **31/30/32/29/33** cutoffs including the sixteen immediately
below completion. No natural speculative rollback interval is reached. The
new census increases proof work; no performance improvement is claimed.
Source/prepared owner units check every incomplete budget at **2865/2773**
(two methods), **6576/6391** (three) and **2871/2780** (parameterized).

The parallel [load-provenance query](escape-load-evidence.md) propagates
diagnostic candidates through local contents, repeated loads, loaded storage
targets, successor operands and loops. It keeps all external alternatives and
every later sink even after the first escape verdict. Work exhaustion and
incomplete inputs remain separate markers. **209 unit rows** and all four
execution oracles pass with zero violations. Bootstrap covers **2946 reads /
15551 sink operands**, **400 stored-site / 624 exposure-site edges**, and zero
newly propagated exposure edges; all **588** supported graphs converge, while
**102** have incomplete inputs. p5 and Phaser gain **324/52** propagated exposure
edges. No load lattice, escape verdict or native admission is changed. Complete
data-property/contents and indirect-retention proofs are still required.

The internal [checked invocation recovery mode](native-source-invocations.md)
now connects normal and unwind invoke completions to the enclosing try using a
value-only tuple. It preserves pre-call state and publishes assignments only on
normal return. The structural unit passes three direct-call fixtures with
**one/two/one invokes** and **seven/ten/nine original checks**, including saved
state, argument mutation, malformed edges, fallible prefixes, budgets and exact
rollback. The default mode and all ordinary native throwing-call refusals stay
unchanged. Live effect admission for other status edges, a complete native call
component and owning exception emission remain prerequisites. The four-source
execution/import regression still retains **16 functions / 11 observations**.

Fresh full native component counts remain **19/574 Bootstrap**, **39/4754 p5**,
**45/7725 Phaser** in both optimization modes, with zero pruned functions. These
are component counts, not complete native applications. Corpus, unit and budget
evidence: `/tmp/ctcompile-arguments-evidence.json`. The emitted setter/getter
C++ was reviewed in `/tmp/ctcompile-arguments-parameter.cpp`: a typed string
argument reaches the Map mutation at runtime and both methods own the same Map.

**Exact next native boundary:** the retained `parameter_call_result` source
calls `host.slot.set(host.slot.get())`, preserving the same five functions.
A fresh devbox check measures **0/5 native** and Node/interpreter `trace=1`,
with every original call and named refusal retained. Current reasons include
`uses its own closure` and an unproved boxed parameter; the failed owner
proof supplies no typed-call authority. Evidence:
`/tmp/ctcompile-arguments-boundary.json`.
Classify the producing call's result from independent live result/effect
evidence before admitting the consuming formal. Do not recurse through the
family's own property-call query as authority. Preserve the initial getter,
setter mutation, final getter, publication and their runtime order.

Nullable Map results used as keys remain a separate **0/4** presence/carrier
boundary; exact Bootstrap Data remains **0/7** per mode. Object keys/payloads,
realm ownership, future external callers, source throwing-call admission and
complete contents analysis remain unfinished. Full native Bootstrap is not yet
an executable native application.

## Preceding shared-Map checkpoint, 2026-09-07

Saved locally: **`c7a849c`** (recovered Map iterator correction), **`fec186e`**
(iterator documentation), **`296cf33`** (shared published Map methods),
**`e41893a`** (direct-load evidence), **`78c2b15`** (source invocation-state
regression), **`881c434`** (boundary checkpoint), and **`f339fe1`** (JSON
signed-zero inventory witness). All 28 inherited compiler changes were
recovered and split by concern; browser carryover was left untouched. No push
was performed.

The next [published Map boundary](native-owned-global-maps.md) is implemented
with a fingerprinted host manifest and explicit standard Map identity:
two zero-argument methods sharing one mutable Map advance **0/5 -> 5/5 native**;
a three-method table admits **6/6**. Complete live proofs follow every captured
closure, fixed publication field, primitive body and current call. They require
one Map identity and the exact source function chain. Preparation validates all
methods before lifting and unboxes the shared cell after every member. It uses
existing typed owners and callables, with no interpreter/collector dependency.
The shared specimen stays **0/5** without the manifest or Map identity.

The integrated lit run passes the nineteen-program shared-Map gate:
fourteen **4/4**, four **5/5** and one **6/6** programs match Node/interpreter
and explicit/deduced GCC 13/Clang 18 binaries. A fourth lifetime variant retains
setter and getter independently after root/table release, churns 4096
allocations, reenters with a distinct Map and performs 1024 further mutations
and reads. ASan/UBSan, use-after-scope/return and leak checks pass in both forms;
weak witnesses confirm destruction at the last callable release.

The parallel [direct-load evidence](escape-load-evidence.md) links live property
reads to direct writes sharing known local allocation sites. Links retain
other keys, later writes and repeated dynamic instances, and do not change load
result lattices, escape verdicts or native admission. Four corpus claims gates
pass with zero oracle violations. The unit increment adds twelve rows and two
live read-base mutations.

The [source invocation gate](native-source-invocations.md) retains four programs,
sixteen source functions and eleven observations for pre-call assignment state,
a prior normal call, argument mutation and receiver/key/getter/argument order.
Source recovery must connect invoke continuations to the enclosing try
completion and preserve other status edges until effect admission; wrapping the
call alone or removing the explicit-throw guard is insufficient.

The focused devbox gate passes **8/8 CTests** in **79.86 seconds**, including
**163/163 lit cases**, all **200 escape unit rows**, and the four corpus escape
oracles with zero violations. Both proof-unit and source regression corrections
were rebuilt and rerun. Log: `/tmp/ctcompile-native-integrated-focused2.log`.
All compiler files pass the formatter; the whole-tree check flags untouched
browser `style/selector.hpp`, `DOM/document.cpp` and `Style/css/selector.cpp`.
A stale header ABI in the first build was fixed by refreshing frozen compiler
source/header timestamps so every dependent object rebuilt. The full frozen
build then passes **474/474 CTests** in **622.05 seconds**: **366 compiler** and
**108 browser** tests, including **163/163 lit cases** in **70.01 seconds**.
All fifteen previously failing Map/snapshot/partial-evaluation/deforestation
execution checks pass. The browser baseline is committed `7a755dd` tree content;
Claude's unmerged WPT work is excluded. Log:
`/tmp/ctcompile-native-integrated-full-gate.log`.

The measured first complete native admission budgets are **1031** (ordinary),
**8516** (sixteen calls), **1132** (growing) and **1906** (shared growing), with
**32/30/31/33 cutoffs** checked. Every specimen preserves its source through the
sixteen budgets immediately below completion. No natural speculative-clone
rollback interval is reached. Shared source/prepared owner units independently
check every incomplete budget: **1582/1521** for two methods and **2367/2279**
for three methods.

The full-run load census covers **2946 Bootstrap property reads**, with **138
candidate links across 48 reads**, zero linked stored-site edges and zero
invalid links or unresolved bases. It classifies **486/588 functions complete**,
with 102 partial; these are census markers, not contents or confinement proofs.
The existing storage census remains **2611 writes / 400 site edges**. p5 has
**3047 links / 754 linked reads**, Phaser **7425 / 571**. All four execution
oracles report zero violations. Full native component admission remains
**19/574 Bootstrap**, **39/4754 p5**, **45/7725 Phaser** in both optimization
modes, with zero pruned functions. These are component counts, not whole native
applications. Evidence: `/tmp/ctcompile-native-integrated-evidence.json`.

The incoming runtime JSON parser correction makes the old malformed-input
inventory witness agree with Boost. `f339fe1` uses `JSON.parse('-0')` instead:
the VM preserves `8000000000000000`, while Boost's integer parsing produces
`0000000000000000`. The separate devbox inventory check passes **1/1** in
**0.01 seconds**, retaining **35 rows / 50 probes** (**29 agreements / 21
expected divergences**). No runtime or native JSON admission changed. Log:
`/tmp/ctcompile-native-json-inventory-gate.log`.

**Exact next native boundary:** the shared setter takes one key parameter,
`set(key) { state.set(key, 1); return state.size; }`, while the getter stays
zero-argument. Called with `"x"`, this retained source measures **0/5 native**
and Node/interpreter `trace=1`. Discover every current method call before
checking the family bodies; independently classify actuals, keep their SSA
operands, and record per-call formal/actual evidence plus per-method primitive
parameter tags. `HostContract/Values.cpp` currently rejects explicit indirect
actuals, assumes three source/four prepared arguments and excludes parameters
from its primitive-body proof. Existing capture lifting already prepends the
Map environment before explicit arguments. The proof must not authorize itself
through property-call recursion or turn one startup value into future-call
permission. A typed external ABI is still a separate obligation.

Nullable Map results used as keys remain a separate **0/4** carrier/presence
boundary. Exact Bootstrap Data remains **0/7** per mode. Full native Bootstrap,
source throwing-call recovery/admission/emission, and complete contents/points-to
propagation through loads and indirect exposures remain unfinished.

## Preceding native Map-effects checkpoint, 2026-09-07

Five work commits are saved locally on `ctcompile-v1`: **`e52897f`** (complete
direct storage census), **`4b36cd1`** (checked invocation normal-return flow),
**`6642912`** (live primitive Map effects), **`a6b0807`** (native mutation and
lifetime gate), and **`0510988`** (Unicode trim inventory correction). No push
was performed. Fifteen dirty compiler files and one untracked document from the
interrupted loop were recovered and split by concern; browser edits were not
included.

The [published Map method](native-owned-global-maps.md) now supports standard
`size`, `set`, `get`, `has` and `delete` over a completely checked primitive
content/use graph. The `state.set("x", 1); return state.size` specimen advances
**0/4 -> 4/4 native** with a fingerprinted host manifest and standard Map
identity, retaining runtime mutation and `trace=1`. The original default and
no-intrinsic modes remain **0/4**. Source allocations, all four functions,
wrapper/factory calls and publication remain runtime operations.

Fourteen **4/4** programs match Node, the interpreter and standalone GCC 13 /
Clang 18 explicit/deduced binaries with no VM symbols. Ordinary, mutating and
growing methods pass owning-lifetime ASan/UBSan, use-after-scope,
stack-use-after-return and leak checks in both forms. Saved callables survive
root/table release, and reentry creates distinct Map owners. The growing method
mutates on every call; saved and fresh environments retain independent sizes
through **1024 further calls each**. Thirty source refusals, three independent
carrier refusals and contract/rerun controls pass.

The first complete admission budgets are **1012** (ordinary), **8257** (sixteen
calls) and **1113** (growing), with **31/31/32 cutoffs** checked. All sixteen
budgets immediately below each cutoff preserve the source graph. None exposes
a naturally reached speculative-clone rollback interval; existing scalar/table
rollback controls remain. Owning a primitive Map does not prove a supported
native key/value/result carrier.

The escape increment records every direct `Stored` operand, including later
stores and values whose first sink was different, with stored-value and target
aliases. Unsupported targets, nested regions and whole-frame refusals preserve
partial evidence with `complete=false`. No escape verdict is weakened and no
native consumer uses this census as a contents proof. All **188 unit rows**,
fixture and Bootstrap oracle gates pass, with zero violations. See
[direct storage evidence](escape-storage-evidence.md).

The invocation increment joins only the protected helper's normal return
operands after a fresh bounded completion query. Passed arguments and SSA joins
retain widening dependencies; transitive callees' returns do not join the outer
result. Fifteen added rows and five live-mutation checks pass, including forged
nothrow markers, unknown effects, recursion and exhausted work. Ordinary calls
remain conservative on throw exits. Source recovery/admission/emission still
need to consume the explicit invocation regions; the explicit-throw guard is
unchanged. See [native exceptions](native-exceptions.md).

Claude's `61416fc` makes JavaScript trim Unicode WhiteSpace/LineTerminator
characters. The compiler inventory now marks both the ASCII helper and Boost
candidate divergent, with NBSP/BOM witnesses and a pinned Boost locale. The
focused test passes **35 rows** (**19 exact / 14 divergent / 2 refused**) and
**50 probes** (**29 agreements / 21 expected divergences**). No runtime code or
native string admission changed.

The focused devbox build and **6/6 CTests** pass in **1.37 seconds**, followed
by the complete fourteen-program native gate. The trim CTest passes **1/1** in
**0.01 seconds**. Logs are `/tmp/ctcompile-map-effects-recovery-focused3.log`
and `/tmp/ctcompile-map-effects-recovery-full-gate.log`.

The frozen `86df9b1` full build succeeded; CTest measured **454/474** in
**605.43 seconds**, including **161/161 lit cases**. Five failures belonged to
that older browser snapshot. Fifteen compiler differential failures exposed
raw `Map.keys()`/`values()` being treated as arrays after runtime `e6c77fc`
changed them to iterator objects. That is a compiler boundary to fix, not a
reason to reverse the runtime correction.

The follow-up in `NativeMap/SnapshotCopies.cpp` requires each iterator to have
one proved `Array.from` argument use in the same block, with only constants,
root bookkeeping and proved Array builtin lookups between them. Direct reads,
publication, mutation, repeated consumption and consumption across effects
refuse. The six execution fixtures now explicitly materialize their intended
arrays without changing observations. Partial evaluation recognizes only the
freshly proved builtin/copy environment; actual snapshot evaluation stays
runtime work. That interrupted correction is now saved as `c7a849c`; the
current checkpoint above records its ten-source regression and passing full
generated gate. No browser sources were edited.

`7a755dd` records the full-run direct-storage corpus census: Bootstrap has
**2611 writes / 400 site edges**, with **29 multiple-store** and **17
other-first-sink** sites. All first Stored witnesses are covered and execution
oracles report zero violations. Both optimization modes still measure native
**19/574 Bootstrap**, **39/4754 p5**, and **45/7725 Phaser**. These are component
admission counts, not whole-program native compilation. Compiler formatting and
whitespace pass; the whole-tree formatter flags only browser files left in the
shared checkout. Historical full-suite counts below predate the iterator
correction; use the current checkpoint above for its validation.

**Exact next native boundary:** two zero-argument published methods sharing the
captured mutable Map. The retained setter/getter specimen measures **0/5
native**, while Node and the interpreter both produce `trace=1`. The live cell
census currently accepts capture by only the selected closure, and the owner
query requires one method and four functions. Extend both proofs to all methods
and their shared owner before Data's parameters/results. Separately, a
`Map.get` result used as a key remains nullable and refuses **0/4** despite
complete ownership and Node/interpreter `trace=1`; it needs presence/type
proofs. Full native Bootstrap remains unfinished.

**Parallel next boundaries:** recover importer throwing calls into invocation
regions and connect both checked completion flows to admission and C++ emission.
Escape precision still needs contents/points-to propagation through loads,
indirect transfers and every exposure before weakening `Stored`.

## Preceding native captured-Map checkpoint, 2026-09-07

Five work commits are saved locally on `ctcompile-v1`: **`357ba41`** (captured
Map source ownership), **`5f54358`** (direct storage-target diagnostics),
**`3c9d402`** (the actual imported indirect factory callback), **`fcfc55b`**
(explicit invocation completions and payload type flow), and **`3c04ccc`**
(native owning Map publication). No push was performed. The abandoned loop's
18 dirty compiler files were recovered, reviewed and split by concern; no
browser edits were taken into these commits.

The [captured Map publication specimen](native-owned-global-maps.md) advances
**0/4 -> 4/4 native** with an explicit fingerprinted host manifest and standard
Map identity. Its default and no-intrinsic modes remain **0/4**. The four source
functions, runtime Map allocation, wrapper/factory calls, publication and size
read remain. The real importer leaves `factory()` indirect inside its wrapper;
the complete source proof now follows that actual callback before any native
preparation. Exact public/framed imported IR is a unit fixture alongside the
direct and prepared variants.

Preparation validates the original fingerprint and reconstructs native facts
on a disposable clone. Existing callback specialization, capture lifting and
cell unboxing connect the immutable environment to shared Map/table/root owners
and an owning callable. Complete live proofs are required after each changed
graph and again at final admission. Standard Map preparation may pass only
ordinary-root reads authorized by that live owner proof. Global publication
remains `StoredGlobal`; general global loads stay external.

Six **4/4** programs match Node, the interpreter and standalone explicit/deduced
GCC 13/Clang 18 binaries with no VM symbols. Both forms pass ASan/UBSan,
use-after-scope, stack-use-after-return and leak checks. The lifetime harness
retains root, table and callable separately after entry, releases globals,
churns allocations and invokes the saved callable after root/table release.
Weak witnesses observe the actual captured Map: reentry creates a distinct Map,
and each expires when its last callable owner is released. Twenty-two source
refusals plus missing/stale/forged contracts, reruns and work limits pass.

Native admission first completes at **1009 steps** for the ordinary specimen
and **8209** for sixteen getter calls. Each gate checks **33 cutoffs**, including
the sixteen immediately below completion, preserving source operations and
signatures after failure. Neither fixture naturally reaches an interval where
the original proof succeeds but the clone proof exhausts; do not claim a new
Map-specific speculative rollback execution. Existing scalar/table rollback
controls remain. The ownership unit independently checks every incomplete
budget for all five fixtures: **861**, **885**, **1009**, **848** and **581 steps**
for direct source, indirect source, exact imported source, lifted and specialized
forms respectively.

Escape diagnostics now classify the direct storage target of each first
`Stored` witness as local-confined, local-escaping, local-mixed,
external-or-mixed, primitive or unresolved. This is evidence for the contents
backlog, not a points-to proof: a confined packing array can expose its children
to a spread callee, and later stores can retain elsewhere. Every existing
escape verdict stays unchanged. The unit passes **173/173 rows**, and fixture
and Bootstrap oracle gates pass with zero violations. The fixture's **12**
first-witness sites split into **2** local-confined, **6** local-escaping,
**2** external-or-mixed and **2** unresolved. Bootstrap's **170** split into
**93** local-escaping, **13** external-or-mixed and **64** unresolved, with no
local-confined target. p5 has **1304** sites (**25** local-confined); Phaser has
**1861** (**12** local-confined). These diagnostic counts do not license any
new confinement claim.

The parallel exception increment adds `ctjs.invoke`, `invoke_exit` and
`invoke_yield`. The normal continuation alone receives the call result; the
unwind continuation receives an implicit payload and explicit pre-call state.
The verifier rejects intervening work, result use outside normal dispatch and
state defined inside the invocation body. A fresh payload query follows live
explicit throws and direct callees, bounded to **4096 operations / 32 helpers**;
unknown effects remain boxed. Thirteen inference rows, two round-trip forms
(including zero results/state) and eleven malformed-IR controls pass. Source
throwing calls are still unsupported; the existing native exception suite
remains **52/52 functions and 39 observations**.

The full serialized devbox build and gate pass **471/471 CTests** in
**566.41 seconds**: all **366 compiler tests** and **105 browser tests**, including
**161/161 lit cases** in **50.33 seconds**. At the implementation gate,
all **575 C++ files** pass `tools/format.sh --check`; whitespace checks pass.
Native coverage with optimization disabled remains Bootstrap **19/574**, p5
**39/4754** and Phaser **45/7725**, with no pruned functions.

The final formatter run still passes every compiler file and flags only Claude's
new live edit in `ctbrowser/lib/Script/builtins/internal.hpp`. It is journaled
and left untouched; the log is `/tmp/ctcompile-map-recovery-final-format.log`.

The evidence audit found a source timestamp race in `OwnedGlobalMethods.cpp`:
the exact imported fixture arrived via rsync after its object was built, but
retained an earlier mtime, so Ninja skipped the new unit body. Refreshing only
that source timestamp and rebuilding runs all five fixtures successfully;
related ownership/host/type CTests pass **4/4** in **0.34 seconds**. The other
changed compiler `.cpp` objects postdate their remote source arrival. No code
change was needed. When editing during a remote build, inspect or refresh the
modified source timestamp before trusting the next incremental build.

Logs are `/tmp/ctcompile-map-recovery-full-gate.log`,
`/tmp/ctcompile-map-recovery-exact-unit.log` and
`/tmp/ctcompile-map-recovery-format4.log`; measured budget, corpus and storage
evidence is `/tmp/ctcompile-map-recovery-evidence.json`. Compiler changes are
committed; Claude's new browser work remains independent in the shared tree.

**Exact next native boundary:** the published getter currently permits only
`state.size`. The retained refusal specimen first executes
`state.set("x", 1)` and then returns `state.size`: **Node/interpreter `trace=1`,
native 0/4**.
Extend the complete callable/environment proof to supported standard Map
operations and their effects before widening to Bootstrap Data's multiple
methods and arguments/results. Existing native Map/type machinery is already
available; completed startup summaries cannot authorize later calls. Exact Data
remains **0/7** per CommonJS/browser/realm-fallback mode. Typed exports, mutable
publication slots, general realm owners, reentry and full initialization remain
unfinished.

**Parallel next boundaries:** recover source throwing calls into the new
invocation regions, add a checked normal-return transfer, and connect admission
and emission to the existing target exception contract. Upstream ordinary call
inference stays conservative when a callee has throw exits, independently of
the new payload proof. Do not relax the explicit-throw guard. Escape precision
still requires complete contents/points-to evidence before weakening `Stored`.

## Preceding native exported-getter checkpoint, 2026-09-07

Three work commits landed locally on `ctcompile-v1`: **`8455458`** (nested-region
escape retention), **`fecb9af`** (exception payloads through defined EmitC
helpers), and **`be76faa`** (native owning global method tables). No push was
performed. The incoming uncommitted escape work was completed and saved first.

The [exported constant getter](native-owned-global-methods.md) advances
**1/3 -> 3/3 native** with an explicit `host-manifest`; its default remains
**1/3**. The existing live source graph now connects fixed field stores and
loads in the returned-table census. Narrow preparation preserves the real
receiver, then global field types, whole-component admission and emission use
the existing shared owner/table and owning callable carriers. No runtime or
escape semantics changed: publication is still `StoredGlobal`, and general
global loads remain external.

Preparation validates the original manifest before working on a clone. It
rebuilds native source-operation facts, prepares only the checked uncaptured
table, and requires a complete live proof of the transformed graph before using
it. Final admission rebuilds the proof again. This also fixes stale
`ctnative.method` annotations that could otherwise erase a real scalar/table
field initialization or observation store. Fresh-fingerprint execution controls
cover both owners; a stale manifest cannot enter preparation.

Eight complete **3/3** getter variants match Node/interpreter and explicit/deduced
GCC 13/Clang 18 output without VM symbols. Owner, table and callable are retained
independently after entry; global release, allocation churn, reentry, distinct
identity, weak expiry and invocation after table release pass lifetime checks.
Both forms pass ASan/UBSan, use-after-scope, stack-use-after-return and leak
checks. Twenty-four source refusals, four unsupported observation-result types,
stale/forged/rerun controls and the unchanged default boundary pass. The scalar
gate now covers **eight 1/1 programs**, including both stale-marker controls.

The imported getter needs **499 proof steps** before preparation and **510**
afterwards. Budgets **499 through 509** discard the speculative clone without
leaking source allocation/field/call rewrites and retain **1/3** admission.
These counts differ from the smaller handwritten query unit, whose completed
budget remains **361**; the scalar unit remains **164**.

[Native exception target validation](native-exceptions.md) now follows defined
`emitc.call` helpers and requires homogeneous escaping number, boolean or owning
string payloads to agree with the surrounding catch. Locally caught throws do
not escape; catch rethrows do. Each verification uses a fresh bounded query
(4096 operations, 32 active helpers), refusing unresolved/external definitions,
recursion, mismatches and exhausted budgets. Opaque calls retain their existing
foreign-exception contract. Five positive and 13 refusal controls pass, along
with explicit/deduced/hoisted GCC/Clang execution and owning string sanitizers.
The fixture checks pre-call state, normal-return-only assignment publication,
exact-once cleanup, local catch/rethrow, negative zero and foreign exceptions.
This is a target prerequisite; **source throwing calls are still unsupported**.
The existing source suite remains **52/52 functions and 39 observations**.

Escape analysis now sinks implicit nested-region captures and retains
whole-frame suspend/late-arguments refusals even without explicit SSA operands.
Nested allocation sites receive no CFG-only confinement verdict. Twenty-one
additional rows bring the unit to **157/157**; escape unit, fixture oracle and
Bootstrap oracle CTests pass **3/3** in **0.59 seconds**.

The complete compiler rebuild and CTest gate pass **366/366 tests** in
**547.65 seconds**, including **159/159 lit cases** (lit **48.54 seconds**).
The shared full devbox build then succeeds, and its CTest run passes
**468/471 tests** in **565.53 seconds**. All **366 compiler tests** pass again
against the updated runtime, including all **159 lit cases** in **48.50 seconds**.
The three failures are in Claude's active browser work: `bindings_basics`
(`isTrusted` own accessor), `bootstrap_layout` (computed-style baselines), and
`property_attributes` (old builtin name/length expectations). They are journaled
in `AGENT-SYNC.md`; no browser files were edited by Codex. The initial unused
lambda capture build failure was fixed by Claude in **`3d30c82`**.

All **574 C++ files** pass the final `tools/format.sh --check`; compiler whitespace
checks pass. Logs are `/tmp/ctcompile-native-session-compiler-gate.log`,
`/tmp/ctb-build4.log` and `/tmp/ctcompile-native-checkpoint-final-format.log`.
The monorepo CTest gate is not wholly green while those browser failures remain.

**Exact next native boundary:** extend the complete live callable/source-owner
graph to the immutable captured Map environment through wrapper return and
global publication, preserving allocation identity and shared ownership.
Connect that proof to existing Map/capture/table types and final component
admission. The Map publication specimen remains **0/4**; its **4/4** native gate
is proposed. Exact Bootstrap Data stays **0/7** in each CommonJS/browser/realm
fallback mode. Prefix completion cannot authorize future callers. Typed export
ABI, mutable slots and general realm owners remain further work. Full native
Bootstrap is unfinished.

The next proof must preserve the specimen's four-function entry/wrapper/factory
chain; current host calls allow only literal uncaptured getters and current
owners require entry-local publication in three functions. After extending
those proofs and capture preparation, `prepareNativeMaps()` must also consume
proved owning-root reads: its standard-builtin check currently refuses every
other host/global value read. Enabling Map preparation alone is insufficient.
See [the exact implementation boundaries](bootstrap-provider-next.md).

**Parallel next boundaries:** represent source throwing calls with an explicit
exceptional call edge carrying the pre-call register vector and an owning
payload, then use the now-tested target helper contract. Publish assignment
results only on normal return; do not relax the current explicit-throw guard.
General source handlers, uncaught entry adapters and mixed/object payloads
remain separate. Escape precision still needs contents/points-to evidence before
weakening retained elements' `Stored` verdicts. See
[the Bootstrap boundary](bootstrap-provider-next.md).

## Preceding checked-getter and protected-helper checkpoint, 2026-09-07

Five work commits landed locally on `ctcompile-v1`: **`688461c`** (spread escape
lifetimes), **`fd756f9`** (checked native helper callees), **`12c1b6b`** (deferred
generator invocation refusal), **`94024fb`** (current host getter proof), and
**`2772cd6`** (fixed global method-table source ownership). No push was performed.

[Current host getters](native-host-callables.md) now expose live
`HostCallableEdge` records for an uncaptured literal-return getter's actual
call, property read, preceding initialization, source closure and function.
Both indirect and already resolved calls preserve their receiver and operands.
The complete contract validates source-program provenance, undefined lexical
receiver, unused implicit arguments and effect-free literal bodies. Its unit
finishes at **295 charged steps**; all smaller budgets withhold slot and call
edges. Eight positive and 22 refusal programs pass, including replacement,
receiver/capture/effect controls, stale/forged contracts and generators/async.

The [owning source graph](native-owned-global-methods.md) connects the exported
constant getter's root, sole factory invocation, returned table, fixed method
and actual calls. It requires three straight-line functions and rejects extra
publications, allocations, factory invocations, schema extension and rewrites.
Its unit covers indirect/direct/repeated calls, detached tables/callables,
reordered initialization and semantic mutations; **361 charged steps** complete
the base query and **all 361 incomplete budgets** refuse atomically. The scalar
owner query now measures **164 steps**, previously 162, after host accounting
changes. `StoredGlobal` and external-global escape rules remain unchanged.

The exported getter stays **1/3 native** with and without `host-manifest`.
Explicit-manifest lowering reports a proved source owner but retains the
numeric-field and closure-value native refusals. Source allocation, publication,
property and call counts remain unchanged; Node/interpreter `trace=42` still
agrees. Reusing the original manifest after partial native lowering refuses its
changed fingerprint. No native table binary or new table lifetime result is
claimed. Captured Map publication remains **0/4**, and exact Bootstrap Data
remains **0/7** in CommonJS/browser/realm-fallback modes.

[Protected helper resolution](native-exceptions.md) follows branch successor
register vectors in a bounded query, independently checking every incoming
callee definition. It preserves `ctjs.check` status and exception snapshots,
including mixed-predecessor and work-exhaustion refusals. The exception gate now
passes **20 programs, 52/52 functions and 39 observations** across Node,
interpreter and explicit/deduced GCC/Clang. Numeric/string defaults and owning
string ASan/UBSan lifetime checks pass. Twenty-two source refusals, late effects,
forged/rerun reports, mixed incoming callees and a 2100-block budget control pass.

The callable review found an importer hole: a generator without `yield` looked
like an ordinary eager function. Import now refuses every generator invocation
until its deferred iterator semantics are represented, retaining skipped source
identities and global-store accounting. Ordinary async returns retain the
existing promise-wrapper refusal. This fix has its own importer regression.

The spread audit found no production escape solver defect. Seven added unit
rows bring the suite to **136/136**. The oracle records **106 observed sites,
103 claims, zero violations and 16 sound confined claims**. Six source/packing
arrays are confined while three literal elements/receivers are retained.
Constructor-created objects stay explicitly unclaimed; mutation controls reject
both unsound child confinement and unnecessary packing-array escape claims.

Serialized full devbox gate: **469/469 CTests**, **157/157 lit cases**, in
**555.03 seconds**. Final source-program provenance and extra ownership controls
pass a subsequent **4/4 targeted CTest gate**, including all **157 lit cases**,
in **44.31 seconds** (lit **44.24 seconds**). All **568 C++ files** pass formatting;
whitespace checks pass. Default/disabled native coverage remains Bootstrap
**19/574**, p5 **39/4754**, Phaser **45/7725**. Exact Data provider progress stays
24 resolved calls, 23 completed summaries and 19/19/24 matching observations.

**Exact next native boundary:** consume the now-existing live source graph in
`ClosedValueFlow` and the returned-method-table census, then carry the existing
owning table carrier through global field types, final call-component admission
and emission. The **3/3** standalone getter gate and post-entry lifetime checks
are still proposed. The manifest path skips closure lifting; any new preparation
must validate the original fingerprint and reconstruct proof for transformed IR
without silently refreshing a stale manifest. Do not rediscover these source
edges or treat their reports as native ownership permission. Captured Maps and
future-call/typed-export contracts follow. Full native Bootstrap is unfinished.

**Next exception boundary:** the throwing `fail()` target now resolves, but
recovery still says `native try/catch needs an explicit throw in its active
handler`. Add a call-region exceptional edge carrying pre-call register state
and an owning payload. Publish assignment results only after normal return;
relaxing the existing `throws == 0` guard cannot model unwinding before
`try_exit`. General finally/nested handlers, uncaught entry adapters and
mixed/object payloads remain separate work. Escape precision next needs a
contents/points-to proof before weakening the element's `Stored` verdict.

## Preceding scalar-owner checkpoint, 2026-09-07

Four more work commits are local on `ctcompile-v1`: **`c822d24`** (composed
global escape regressions), **`98df646`** (live ordinary-global owner query),
**`05b672a`** (closed primitive catch helpers), and **`91627e5`** (native owning
scalar global storage). No push was performed.

[Checked ordinary global owners](native-owned-globals.md) implement the scalar
export gate: `var host = {}; host.slot = 42; var trace = host.slot;` advances
**0/1 -> 1/1 native** with an explicit
`--ctnative-lower-to-emitc="host-manifest=driver.json"`. The manifest fingerprints
prepared IR and selects the root and observations. The live query requires a
complete host proof, one straight-line script, one fresh ordinary allocation,
one binding publication and one fixed numeric field initialization. It is
rebuilt for final admission. Source allocation/publication order remains;
generated global storage and loads own a `std::shared_ptr` to the concrete field
class. Owner storage is separate from driver-selected scalar observations.
`StoredGlobal` and external global-load escape semantics are unchanged.

Six complete programs admit **1/1 each**, with six selected observations matching
Node, interpreter and standalone explicit/deduced GCC 13/Clang 18 output.
Both generated forms pass ASan/UBSan, use-after-scope, stack-use-after-return
and leak checks. The harness retains the owner after entry returns and its
global is reset, churns allocations, runs entry again, checks distinct live
identities, then observes weak-owner expiry after release. Fifteen source
refusals plus stale/forged/rerun and budget controls pass. The live-query unit
completes at **162 charged steps**; all **162 incomplete budgets** refuse
atomically. Fingerprinting retains the host analysis's existing whole-module
hashing behavior. Explicit-manifest lowering preserves prepared source instead
of running default rewrites that would invalidate the fingerprint; callers
must prepare IR before creating the manifest. No report or type marker grants
ownership. Without this option the scalar export stays **0/1**.

[Native exceptions](native-exceptions.md#closed-catch-helper-boundary-2026-09-07)
now admit private primitive nonthrowing helpers in catch bodies, including
transitive calls and owning string arguments/results. The live effect query
is bounded to 4096 helper operations and 32 active helpers. The source gate
passes **16 programs, 38/38 functions and 31 observations** across Node,
interpreter and explicit/deduced GCC/Clang; numeric/string defaults and string
lifetime sanitizers pass. Twenty source refusals, late helper mutation,
forged/rerun reports, depth/work limits and prior recovery/wrong-state controls
pass. A helper loaded inside `try` still flows through `ctjs.check` register
vectors and remains unresolved; actual throwing callees are not implemented.

The escape audit found no production solver defect. Seven unit controls cover
global aliases, mixed fresh/external phi and loop flow, containment, and an
overwritten binding whose object remains globally retained through an alias.
Five added oracle sites make seven objects: five retained through globals and
two confined alternatives. The full oracle measures **91 observed sites,
89 claims, zero violations and ten sound confined claims**.

Final serialized devbox gate: **468/468 CTests**, including **156/156 lit
cases**, in **544.10 seconds**. Tightened boolean/string field refusal controls
also pass the full lit rerun (**36.79 seconds**). All **564 C++ files** pass
formatting; whitespace checks pass. Default and disabled native coverage stays
Bootstrap **19/574**, p5 **39/4754**, Phaser **45/7725**. Exact Data probes remain
**0/7 native** in CommonJS/browser/realm-fallback modes; their existing provider
progress remains 24 resolved calls, 23 completed summaries and 19/19/24 matching
observations. Full native Bootstrap initialization is unfinished.

**Next native boundary:** connect the exported fixed field to its actual owning
table and current uncaptured getter, consuming live callable/environment proof
in closed value flow, method-table analysis and final admission. The measured
getter gate is still **1/3**; **3/3 is proposed**. Complete host analysis still
refuses callable/provider paths, and explicit-manifest lowering skips closure
lifting, so new preparation must preserve or reconstruct valid proof without
silently rebinding a stale manifest. Then address the captured Map export
(currently **0/4**) and future-call/typed-export contracts. See
[the exact next boundary](bootstrap-provider-next.md).

The parallel exception boundary is to preserve and resolve checked callee value
flow, then add an exceptional call-region edge carrying the pre-call register
snapshot and an owning payload type through the closed native component.
Publish an assignment result only on normal return. The current `try_exit`
cannot represent unwinding before that completion. An explicit uncaught-entry
adapter, general finally/nested handlers and mixed/object payloads remain
separate work; normal-return prefix facts do not authorize exception paths.

## Preceding object-payload checkpoint, 2026-09-07

Four work commits are local on `ctcompile-v1`: **`0a4a7c9`** (export-boundary
evidence), **`c358c1b`** (owning primitive exceptions), **`454a886`** (provider
object payloads), and **`6cab1a0`** (live owning-field query regressions).
No push was performed.

[Provider object payloads](native-provider-objects.md) add opt-in
`follow-provider-objects=true`, requiring publication/read/mutation following.
The exact CommonJS/browser/realm-fallback programs additionally enable
diagnostic/callback following. They now resolve **24 calls** and complete
**23 provider summaries**, up from 20 and 18. Each records **68 reads, seven
sets, three deletes, three distinct nested Maps, one callback and two global
writes**. The actual initialized `instance` survives Map storage and both
getter returns as one identity. Current scalar fields follow alias mutation,
replacement and named/computed deletion/reinsertion. Maps, objects and callback
globals commit together only after normal return; runtime effects remain.
Thirty-eight source cases, provenance/refusal controls and incomplete-budget
checks pass. All **19/19/24 exact observations** agree across Node, interpreter
and boxed script/wrapper execution, including GC stress. Source/vendor hashes
and the seven-function denominator are unchanged; native admission stays **0/7**.

CommonJS/browser prefix following reaches the end. The fallback completes every
Data call and then stops at the appended `scriptThis === this` observer:
`unproved comparison behavior at ctjs.compare`. It has no remaining provider
boundary. Realm comparisons and the following missing-own-property observer
reads remain outside this object proof; all runtime observations still pass.

[Native exceptions](native-exceptions.md) now own homogeneous number, boolean
or string payloads and catch state through `js_exception<T>`. Completed
scratch-register computations no longer block recovery, while every discarded
implicit exception edge still requires nonthrowing admission. Thirteen source
programs admit **27/27 functions** with **25 matching observations** in Node,
the interpreter and explicit/deduced GCC/Clang output. Numeric and owning-string
default-optimization checks pass. Source/target string lifetime tests pass
ASan/UBSan, use-after-scope, stack-use-after-return and leak checks. Thirteen
source refusals, six additional target-verifier controls and the prior budget
and wrong-state controls pass. Mixed/null/undefined/object payloads, throwing
callees and general finally/nested source handlers remain unsupported.

[Export-boundary evidence](native-export-boundary.md) isolates the next native
consumer: a scalar global root has a complete live host proof but stays **0/1**
native. An exported constant-getter table is **1/3**; a Map-backed table whose
startup prefix completes is **0/4**; the confined local control is **4/4**.
All four observations match Node/interpreter. Reports, forged annotations and
reruns never supply native ownership. The owning-field audit found no production
defect; its new unit controls rebuild an initially successful query after a late
rewrite, lost initialization dominance or owner escape, retaining an independent
valid owner and rejecting stale markers.

Final serialized devbox gate: **467/467 CTests**, including **155/155 lit
cases**, in **540.22 seconds**. All **559 C++ files** and the exception printer
include pass formatting; whitespace checks pass. Default and disabled native
coverage stays Bootstrap **19/574**, p5 **39/4754**, Phaser **45/7725**.

**Next native boundary:** implement an explicit owner for the checked ordinary
scalar global root, preserving `StoredGlobal` and external escape semantics.
Carry its live proof through type inference, global/field admission and emission;
keep owning storage separate from driver-selected observation globals. The
**0/1 -> 1/1** scalar gate is proposed, not implemented. Then connect one fixed
export field to its current uncaptured getter (**1/3 -> 3/3**, proposed), before
captured Map tables and future-call/typed-export proofs. See
[the exact next boundary](bootstrap-provider-next.md). Full native Bootstrap
initialization is unfinished. In parallel, exceptions next need a closed throwing
callee and pre-call assignment state, followed by an explicit uncaught-entry
adapter; prefix facts cannot authorize exceptional continuations.

## Preceding native checkpoint, 2026-09-07

[Provider diagnostics and callback effects](native-provider-diagnostics.md)
now extend the optional mutation prefix through checked Map.keys/Array.from
snapshots, string messages and the actual source recorder's scalar global writes.
Enable `follow-provider-diagnostics=true` and `follow-provider-callbacks=true`
alongside publication/read/mutation following. Both new options default off.
The exact CommonJS/browser/realm-fallback programs advance **13 to 20 resolved
calls** and **11 to 18 completed summaries**, with 52 reads, five sets, three
deletes, one callback and two global writes. All 62 Node/interpreter/boxed
observations agree, including GC stress. Provider Maps, callback globals and
reports commit together only after normal return; runtime bodies and effects
remain unchanged. Native admission stays **0/7** in each mode. The
[next provider boundary](bootstrap-provider-next.md) is an ordinary object
payload, followed by native ownership and call proofs for exported users.

[Native JavaScript exceptions](native-exceptions.md) now recover one acyclic
handler and emit real typed C++ throw/catch. The recovery preserves throw-site
register state, uses LLVM CFG-to-SCF without a fork, and requires numeric
payloads and proved nonthrowing primitive operations. Unsupported structure,
types/effects, call components and exhausted work retain the original CFG.
Mutable slots carry catch-visible state; copied catch bindings use the existing
const analysis. General throwing callees, nested handlers, finally completions
and foreign-call adapters require further work. A finally that already reduces
to an equivalent unconditional return can use the recovered completion shape.
Seven source programs admit 2/2 functions each, with fourteen observations
matching Node/interpreter and standalone GCC/Clang in explicit/deduced modes.
The guarded specimen also passes default optimizations. Eleven refusals,
zero/tight work limits, seventeen malformed-IR controls and an executed
wrong-state control pass. One refusal records the existing null-property
interpreter/Node discrepancy separately; it is never admitted as native.
Exception support remains separate from callback effects and exported ownership.

[Private Map mutation summaries](native-provider-mutations.md) now extend the
host prefix under `follow-provider-mutations=true`, requiring both publication
and provider-read following. A bounded transaction carries Map state across
completed method calls, including nested allocations and primitive/object keys.
The exact CommonJS/browser/realm-fallback probes advance **4 → 13 resolved
calls**, with eleven completed method summaries, 31 reads, five sets and one
unsuccessful delete. Two distinct nested Maps retain allocation/invocation
provenance. Method bodies, observer branches and runtime effects remain intact.
Without diagnostic following, the boundary remains the conflict arm's
`load_global "console"`; native admission remains **0/7** in each mode. All 62 Node/interpreter/boxed observations
agree, including GC stress. Thirty-eight focused cases and finite work-limit
controls check identities, rollback and refusal behavior. The option is off by
default and never falls back to the old empty-Map model after a mutation.

Callback lifting now accepts an already resolved `ctjs.call_direct` use when its
symbol matches the actual supplied closure. It preserves the call's receiver,
arguments and metadata while removing a proved call-only callback parameter.
The fixture admits 10/10 functions in indirect, resolved and mixed call forms,
with four matching observations in explicit/deduced GCC/Clang builds. Eight
proof refusals and two malformed-signature controls cover unproved identity,
mixed targets, argument-window observations and constructor calls.

[Owning method-table fields](native-owned-method-table-slots.md) now connect a
returned table through one fixed own-data field on a confined local object.
The exact six-function fixture advances **0/6 → 6/6 native**, returning 4211.
Field loads copy the existing owning handle, preserving callable/Map lifetime
after the container dies. The bounded structural query is explicitly consumed
by returned-table flow and rebuilt for final admission; existing capture and
Map checks still apply. Global/realm owners, slot rewrites, uncertain
initialization and incompatible incoming schemas remain refused.
The additional lifetime and shared-field-family fixtures admit 14/14 and 10/10
functions; their seven combined observations (including the six-function
specimen) agree with the interpreter in explicit/deduced GCC/Clang builds and
ASan/UBSan runs. Twenty-one refusal cases, forged/rerun annotations and every
incomplete budget cutoff retain the boundary.
External sample `../ctcompile-samples/07-owning-method-table-fields` contains
the specimen's source and generated C++. All seven samples pass 18 observations;
the first six generated files remain byte-identical to the preceding checkpoint.

Native C++ now spells JavaScript numbers through `using js_num = double;`.
[Returned closures](native-returned-closures.md) now appear directly at their
creation site inside the factory. Their owning init-captures copy source values,
preserving live bindings and shared Map identity. Each lambda uses independent
final-IR names, const/constexpr analysis and deduced-type pins. Nested emission
restores the surrounding function's state. Other direct/address uses retain the
lifted definition; writable captures and recursive or oversized expansions
retain helpers. Sample 5 declares and returns a local `ctn_lambda` inside
`makeCounter_1`, with no `ctn_bind_fn_2` helper. Anonymous closure names use
numbered suffixes when needed; source binding names take precedence.
Used parameters lose redundant generated void casts after final cleanup.

[Maps](native-maps.md) use `std::map` and `.find()` when the admitted module has
no snapshot/iteration observations. SameValueZero lookup handles NaN and signed
zero; modules with snapshots retain insertion order, including after fusion.
Numeric string Maps use `ctnative::string_to_number_map` and a named factory.

The [host-prefix contract](native-host-prefix.md) adds explicit
`follow-publication=true`. It follows a checked straight-line factory's normal
return through its fresh method table and resolves the first actual method call.
The exact CommonJS/browser/realm-fallback probes select 2/5/6 branches, resolve
both the factory and `Data.get`, and retain one runtime Map allocation, three
capture edges and one publication write. Source replacement of a method or the
table selects the replacement's actual target. Separate factory invocations keep
separate resources. The default remains the earlier factory-boundary mode.
Allocation, publication and method effects stay runtime; native admission remains
0/7 on the exact probes. Unknown effects, descriptor changes, mutable captures,
stale manifests and exhausted work retain their conservative boundaries.
The six publication differentials compare 104 observations across Node, the
interpreter and the boxed script/wrapper, including compiled GC-stress runs.

The additional opt-in `follow-provider-reads=true` requires publication
following. It summarizes normal-return paths over still-empty private Maps,
using the actual factory invocation and immutable capture identities. The exact
three probes now resolve factory, `Data.get`, `Data.remove` and `Data.set` calls,
then stop before the first mutation path. Two completed method summaries each
contain one empty-Map `has` read; the methods return null and undefined through
their original control flow. All method bodies,
observer branches and runtime effects remain; native admission is still 0/7.
The three new differentials pass 62 observations under Node, the interpreter
and boxed script/wrapper execution, including GC stress. The focused suite
checks 23 source cases plus stale/forged contracts and work limits.

Final devbox gate: **464/464 CTests**, **152/152 lit cases**, **547.27 seconds**;
all **557 C++ files** pass formatting; `git diff --check` passes. Owning table fields pass
ASan/UBSan, stack-use-after-return and leak checks in explicit and deduced forms.
Existing closure and string-snapshot sanitizer regressions remain green.
Default native coverage stays
Bootstrap **19/574**, p5 **39/4754**, Phaser **45/7725**; exact Data probes remain
**0/7 native** in CommonJS/browser/realm-fallback modes (the separate AMD probe
remains **0/8**). Full Bootstrap initialization is still unfinished.

[Constant-expression bindings](native-constexpr-bindings.md) combine the existing
backward immutability proof with forward target binding-time analysis. Typed
scalar literals and checked exact operations become `constexpr`; parameters,
heap values, opaque calls, invalid/inexact operations and mutable join storage
retain their previous policy. Source BTA reports cannot authorize C++ constant
evaluation. Explicit and deduced declarations share the same qualification and
exact type pins.

[Returned closures](native-returned-closures.md) with concrete signatures now
use a `std::function` alias and a creation-site lambda with its source body inside. Explicit init-captures own their
values, including shared Map handles. They preserve alias mutation and lifetime
after factory return. Unsupported admitted signatures keep the owning tuple
representation; closure admission and escape requirements are unchanged.

[Const bindings](native-const-bindings.md) now qualify native C++ locals and
by-value parameters using backward binding-mutability data flow. Writes, unknown
reference uses and lvalue/capture aliases keep bindings writable. Explicit and
deduced output agree, including exact const type pins and shallow pointer const.
Loop/join storage remains mutable, while `catalog`, `score_1` and `score_2` gain
const where their uses permit it. Compiler-owned helper operand contracts describe
C++ const acceptance without claiming purity or immutable heap contents.

[JavaScript source names](native-source-names.md) now survive into native C++.
The sample's parameter remains `catalog`; its initial and returned scores use
`score_1` and `score_2`, with other intermediates in the same numbered family.
Optional bytecode debug tables supply provenance without changing semantic IR.
One function-wide allocator avoids collisions with types, symbols, macros,
anonymous temporaries and nested loops. Both explicit and deduced output pass
GCC/Clang execution checks, and unmarked boxed output retains its spelling.

[Native C++ literal printing](native-literals.md) now keeps ordinary strings
readable (`std::string("price", 5)`) and spells finite doubles concisely
(`100.0`, `0.1`, `-0.0`). The shared byte-safe formatter preserves embedded NUL,
UTF-8/WTF-8 and escaping boundaries; the C++ printer preserves round-trip float
precision and deduction types. Both GCC and Clang pass 12 string cases covering
all 256 bytes and 168 floating-point bit patterns. All six sample pairs have
been regenerated and retain their 17 expected observations.

The native entry now defaults to bounded primitive precomputation followed by
private reachability pruning. `--ctnative-lower-to-emitc=optimize=false` disables
both; `precompute=false` and `prune-unreachable=false` disable either independently.
Heap PE, direct-call specialization, deforestation and supercompilation remain
opt-in. Type, effect, BTA and ownership proofs remain required. See the
[defaults policy](native-optimization-defaults.md) and [roadmap](native-pe-roadmap.md).
The default/disabled differential preserves eight observations and reduces its
generated C++ from 7,859 to 7,022 bytes with readable literals, source names and
const/constexpr bindings after the call-order correction.
Coverage now accounts for pruned functions without shrinking the source denominator;
historical admission floors run separately with defaults disabled.

[String-key snapshots](native-string-snapshots.md) now lower Bootstrap's exact
`Array.from(map.keys())[0]` diagnostic expression for confined standard Maps.
Owning vectors and nullable strings preserve undefined, null and empty strings,
insertion order and lifetime after mutation. The fixture admits 10/10 functions
with 24 observations and passes ASan/UBSan/leak checks. The Map/Array identity and
confinement proofs remain mandatory; arbitrary hosts and iterators are unsupported.

The first [checked host-slot analysis](native-host-slots.md) implements a
fingerprinted closed-source contract, fresh allocation identity and own-data
publication flow. It exposes usable edges only after the entire contract passes;
unknown effects, stale manifests and repeated factory identities refuse. Exact
CommonJS/browser probes find 24/23 candidate slot edges, but all three complete
contracts remain refused and native admission stays unchanged. Connecting this
proof to retained callables and supported host effects is still open.

[Host entry-prefix specialization](native-host-prefix.md) now consumes a narrower
live proof without claiming the complete host contract. The exact CommonJS and
browser wrappers select two and five UMD branches respectively and resolve one
actual factory closure each. All seven source functions and the runtime factory
call remain; native admission stays 0/7. Initial Map/Array and realm identities
are explicit embedding contracts. Unknown effects, callback exposure, reentry,
stale manifests and exhausted work retain the original control flow. This stage
is opt-in and does not execute initialization early.

The [Bootstrap host/export oracle](native-bootstrap-host-contract.md) covers
publication slots, AMD retention, mutable methods, receivers, exceptions and
error reentry. The [classic-script receiver](native-bootstrap-script-this.md)
now has a stable realm identity independent of the writable `globalThis` binding.
Both interpreted and compiled entries receive it; modules receive undefined.
Together with the preceding call-order/accessor fixes, ctbrowser now agrees with
Node on all **11/11** audit cases. Accessor closures and the realm receiver survive
forced GC. The preceding compiler change intentionally changed boxed Bootstrap output to
11,226,071 bytes, SHA-256
`721e6095554b20eb2241367283ae1b02c032c771c858ca582af974c6754c2528`.

Six readable JavaScript/native-C++ sample pairs live outside the checkout in
`~/Downloads/claude/ctcompile-samples/`. They cover loops, strings, snapshots,
component lifetime, returned closures and explicit heap PE. All 17 observations
agree under GCC, Clang and the interpreter; the directory includes expected
output, a regeneration driver and artifact hashes.

[Owned scalar component fields](native-object-fields.md) now preserve fields
through the exact Bootstrap getter and owning Map payloads. Explicit C++ members
hold number/boolean/null/undefined values; aliases retain one mutable owner after
factory return, replacement, removal and clearing. Field schemas start from
undefined even when another allocation stores the same key. Mixed lookup results
need an exact SSA identity guard or optional-object truthiness guard. The fixture
admits 15/15 functions and compares 29 observations, including reversed guards,
missing fields and distinct allocations. ASan/UBSan/leak checks pass.

[PE call-target proofs](native-pe-call-proof.md) close a concrete wrong-code route:
a native variant and the retained boxed callable could both return undefined while
mutating the caller's object differently. The evaluator now resolves actual
callable identity and checks alternate bodies from copies of the same pre-call
heap. Exact primitive attributes, anchored old identities, bijective fresh
identities, aliases and ordered Map entries must agree under shared work limits.
Disagreement retains the call through the existing transactional prefix path.
This proves a concrete evaluation, not arbitrary runtime dispatch equivalence.

[Scalar supercompiler generalization](native-supercompilation.md) creates a child
configuration retaining only static arguments shared with a whistled ancestor.
Forgotten positions stay dynamic through literal resets; ancestor promises and
bodies remain unchanged. Exact folding runs first, and budgets retain the generic
alternative. The new fixture has 14 configurations, 12 folds and 9 generalizations;
all 23 functions are native with 16 matching observations. Heap-aware contexts,
generalization over heaps and multi-result search remain planned.

[Precomputation](native-precomputation.md) now expresses six scalar replacements
in PDLL. Invocation-scoped native callbacks consult the analysis; PDLL constructs
constants and replaces roots. Runtime producers, budget charges and native region
splicing retain their previous semantics. LLVM 23 supports captured callbacks;
both precompute and supercompile declare the required PDL dialect dependencies.
The differential-test harness also accepts CMake's wrapped diagnostic whitespace
while still requiring the intended wrong-output failure.

Earlier implemented stages remain covered: immutable closure/cell heaps,
conditional BTA effect queries through fields/Maps/captures, exact-tuple direct
specialization, restricted Lumberhack snapshot projection and bounded private
helper pruning. Pruning follows symbolic and numeric closure edges, retaining
original boxed callees and published declarations. See their linked implementation
documents and the [source layout](source-layout.md).

Full native Bootstrap remains unfinished. The selected wrapper, factory return,
retained Map/cell captures and bounded private mutation paths are now proved.
The local storage prerequisite,
[owning method-table fields on confined objects](native-owned-method-table-slots.md),
now admits its six-function fixture fully, with lifetime and independent-state
checks. Its live store-to-load proof connects existing table/capture ownership
to a confined field. The implemented
[transactional Map mutation summaries](native-provider-mutations.md) retain
runtime effects and stop before unknown error/reentry behavior. The next
provider step is designed in [the next provider proof](bootstrap-provider-next.md)
and needs a live effect/reentry proof for console and snapshot calls;
their names or initial intrinsic identities alone are insufficient.
Prefix observations cannot make exports private.
Global/realm storage still needs native ownership/call analysis for exported
callables, supported provider/error effects and current mutable slot values.
Initial provider identity alone does not authorize Map execution or console
errors during PE. Explicit script
receivers, boxed public parameters and open method-table shapes still refuse
native admission. Unchecked mixed-result property access also needs stronger
presence/refinement evidence or an exception boundary; the initial numeric
catch implementation is described in [native exceptions](native-exceptions.md).
Keep exact vendor probe
coverage distinct from the component fixtures. Recursive Lumberhack fusion,
shared mutable capture environments and region splitting remain separate work.

## Earlier compiler bring-up checkpoint

The remaining sections preserve earlier implementation history and its original
measurements; they are not the current native status.

* Repo: `/mnt/c/Users/aange/Downloads/claude/compile-time-browser`
* Branch: **`ctcompile-v1`**, working tree clean
* Suite: **108/108** via `./tools/remote-build.sh`, and **asan 58/58**
* **`ctcompile/docs/ctcompile.md` is the tool's own documentation** — the CLI,
  what it refuses and why, the manifest, and the gaps.
* **Read `ctcompile/docs/plans/ctcompile.md` first** — it is the running plan in
  the house style (Done / Next, measured rungs) and it records every decision
  below with its reasoning. This file is a pointer to it, not a substitute.

The master plan is 21 markdown files in `../ctcompile-plan/` (NOT in the repo).
`00-START-HERE.md` routes by phase; **`01-objective-and-ground-truth.md`
overrides every other file**. Its six non-negotiables matter: `ctjs` is the
parser only, the bytecode is a REGISTER machine, there is no CI, EmitC is the
primary backend, sources are in `lib/`, build on the devbox.

## What is done

* **Phase -1** — the monorepo split. `ctbrowser/` is the CMake configure root;
  `ctcompile/` is a sibling project.
* **Phase 0** — six inventories the build checks, two differential comparators,
  and a recorded startup baseline. See `ctcompile/docs/baseline/*.json`.
* **Phase 2** — the AOT ABI, and its gate is met: `ctbrowser/include/ctbrowser/aot/aot_helpers.def`,
  68 helpers over 83 of the 93 opcodes (84 `CT_AOT_COVERS` rows; `type_of` is
  served by two helpers on purpose). **THE TABLE IS DONE; PHASE 2'S GATE IS
  NOT** — the plan's gate is "VM code calls a hand-authored AOT closure through
  the real runtime ABI", and no helper has a body, no `function_proto` has a
  native entry, and nothing has ever called one. The record said "done" without
  that distinction; it is a contract, and a good one, that has not been
  executed.
  The runtime now at least COMPILES it: `ctbrowser/lib/Script/aot_contract.cpp`
  is a translation unit of nothing but `static_assert`s, in `ctbrowser-script`,
  so every preset checks it. Until 2026-08-21 the only file in the repository
  that included `aot.hpp` was `ctcompile/test/Inventories.cpp`, and `browser`,
  `browser-no-llvm`, `asan`, `tsan` and `windows` all build with
  `CTBROWSER_ENABLE_PROJECTS` empty — so the ABI and `EngineContract.hpp` were
  parsed in exactly one configuration out of six.
* **Phase 1** — the product. CLI documented in `ctcompile/docs/ctcompile.md`,
  a JSON manifest (`--manifest`, and a copy inside every bundle), stable program
  identities (`program_id` is the source hash the runtime matches on), and both
  format versions exposed rather than copied. `--mode` declares Phase 1's three
  modes and refuses the two that need code generation.
* **Phase 3** — mixed-mode dispatch, centralised. `script/dispatch.hpp` is the
  one read of `aot_entry` in the engine and carries the six transition
  counters; `unittests/unit/aot_dispatch` asserts all six, because every arm
  returns the same answer whether it dispatched or not. Before it, `aot_entry`
  was read at ONE line - `op::call` - so a compiled body was reachable from
  interpreted JavaScript and from nowhere else: not from `context::call` (every
  DOM event, timer, promise job and `apply`), not from `new`, and not from a
  program's top level, which for ctcompile is the ordinary case.
* **Phase 4** — AOT GC shadow frames. `context::set_gc_stress` collects at every
  safepoint, which is the only way the ABI's `is_safepoint` obligations can be
  exercised at all: nothing collects while script runs in an ordinary build.
  **It found two real use-after-frees on `new` the first time it ran**, neither
  in code this phase wrote — see the plan. `ct_aot_slots` is the ABI row a body
  needs to keep a value where the collector can see it, and `context::rooted` is
  the general "a C++ scope is holding this across a call" mechanism.
* **A WHOLE FUNCTION RUNS THROUGH THE ABI.** `unittests/unit/aot_program`
  hand-compiles `total(items, scale)` — a loop, an interned name, a property
  read through a getter, an indexed read, a comparison, both binary families, a
  call back into the interpreter, the failure poll — and checks it against the
  interpreter running the same source, including under forced GC. **That is the
  Phase 12A oracle's shape, working on one function.** It also found the
  safepoint in `context::invoke` sitting before arguments were rooted.
* **Phase 10 — the argument strategy is DECIDED and its experiment passed.** A
  three-way panel chose: Phase 10 normalises calls and each backend materialises
  the arguments, with each parameter's ROLE **derived constexpr from
  `aot_helpers.def`** rather than written down twice. The decisive experiment —
  classify all 69 rows before writing any MLIR — gives **zero unknowns**, and
  `ctcompile_inventories` asserts it.
  **The finding that justifies the whole design:** `uint32_t op_kind` is a
  `ctbrowser::script::op` **bytecode opcode**, not a CTJS enum ordinal —
  `aot_bridge/operators.cpp` does `static_cast<op>(op_kind)`. Passing the CTJS ordinal
  would compile `**` into whatever `op(5)` is. A backend must spell it by name.
* **Phase 10 — started: one conversion pattern, matching on the INTERFACE.**
  `ctjs-opt --ctjs-lower-to-runtime` turns CTJS operations into `func.call`s on
  the real helper symbols, and the pass **names no operation** — which the plan
  calls the acceptance criterion. **Its arity check fired immediately and was
  right**: most helpers are not "frame + operands" (`ct_aot_binary_op` is
  `(fr, op_kind, lhs, rhs, out)` where the kind is an attribute and `out` is an
  out-parameter). A mismatch declines the match rather than failing the module,
  so what it declines is the work list for the rest of the phase.
* **Phase 9 — THE IMPORTER WORKS AND ITS GATE IS MET.** Real bytecode functions
  translate into CTJS MLIR: **p5.js imports 3,200 of its functions and phaser
  6,069**, and both modules verify. `ctjs-translate --ctbrowser-js-to-ctjs f.js`
  is the fastest way to see it; `--ctbrowser-bytecode-to-ctjs` takes an image.
  What still refuses is counted, not guessed: `closure` 556 (needs a producer
  for `!ctjs.program`), `gather_rest` 173, `iterable` 117, `make_arguments` 111.
* **Phase 8 — the CTJS dialect exists: 36 operations in ODS**, round-tripped,
  every verifier diagnostic tested under `-verify-diagnostics`, docs building.
  The operations name real `ctbrowser::aot::helper_id` enumerators through
  `CTJS_RuntimeOp`, so **an operation cannot claim a helper the runtime does not
  declare** — which is what ties the dialect to the ABI Phases 2–6 built.
* **Phase 7 — MLIR is stood up and its gate is met.** `ctjs-opt` and
  `ctjs-translate` build and run, the CTJS dialect's five types round-trip, and
  the lit suite runs as ctest #108 so the gate this repo actually uses covers
  it. **MLIR is not built by default and must not be**: `CTCOMPILE_ENABLE_MLIR`
  is OFF, and a runtime-only configure was verified WITH MLIR installed, which
  is the case that matters.
  To build it: `-DCTBROWSER_ENABLE_PROJECTS=ctcompile -DCTCOMPILE_ENABLE_MLIR=ON
  -DCMAKE_PREFIX_PATH="/home/linuxbrew/.linuxbrew;/home/linuxbrew/.linuxbrew/opt/llvm"`.
* **Phase 6 — the throwing tier works.** `ct_aot_catch_land` was recorded in the
  ABI as **unimplementable as written**, found by trying, with two possible
  fixes written down and neither taken "without a compiled `try` to test it".
  `unittests/unit/aot_throw` is that `try`, and the fix is a third:
  `call_frame::landed_slot`, which keeps the helper's signature. `CT_AOT_PAD_BIT`
  is defined now too, with the measurement `aot.hpp` was waiting for.
* **Phase 5** — in progress, and further than it looks. **29 of the ABI's 69
  rows have bodies** (was 4), including the interned-name pool the whole
  property family was blocked on. Two real extractions with the plan's discipline —
  `context::binary_op_static` and `context::binary_op`, the fourteen binary
  operations, one commit each with the suite green — and sixteen rows that
  needed no extraction, only a shim over a function the runtime already had.
  The flags-consistency test the plan asks for is in `Inventories.cpp`.
* **Phase 15** — a working program image, wired into the page load.
  `ctbrowser/{include,lib}/…/program_image.*` writes and reads a compiled
  `script::program`, validated exhaustively, and `browser::set_script_image()`
  uses it.

## The number that justifies the project

From `ctbrowser/docs/performance.md`: a whole p5 page load is 17.5% lexing,
15.1% `declare_local`, 7.6% `collect_captured_names` — and **1.4%
`context::run_loop`, the entire interpreter**. About forty percent of a page
load is READING JavaScript; 1.4% is executing it. So this compiler's value is
overwhelmingly in what it **deletes from startup**.

Measured, `ctcompile/docs/baseline/page-load.json`, p5-basic.html on the devbox:

| p5-basic.html, three classic scripts | ms |
|---|---|
| `load_html` compiling its own scripts | **69.65** |
| `load_html` handed one image per `<script>` | **19.93** |
| | **71% of the page load** |
| **editing the sketch only** | **19.77 — 3.5x, 1 of 3 recompiled** |

## There is an MVP, and it works

```
ctcompile app/ -o myapp        then ./myapp
```

`ctcompile` loads the entry page once with the engine that will run it, asks
that engine which scripts it compiled and which resources it reached for,
compiles each classic `<script>` to a program image, packs page + resources +
images into a bundle, and appends the bundle to a copy of `ctrun`, a fixed
launcher built like any other tool. The output is one executable. Nothing is
generated and no linker runs — a linked ELF does not care what follows its last
section, so packaging is a file copy plus a trailer.

Measured on the devbox, p5-basic.html, seven runs each, whole-process wall clock
including startup and rendering a frame:

| | ms |
|---|---|
| `ctbrowse p5-basic.html`, reading the JavaScript | **78.0** |
| the packaged executable, run from `/tmp` | **47.3** |

That is the honest end-to-end figure, and it also settles a reasonable
objection: the packaged binary is 15 MB against ctbrowse's 3 MB, because
`this_executable_bytes()` reads the whole launcher back at every start to find
its own trailer. Reading 12 MB more still wins by 30 ms.

**IT IS VALIDATED BY COMPARING RENDERS, not by exit codes.** Seven example
pages package and run; six render byte-identically to the same page loaded from
source, and the seventh did too once a real defect was fixed. That comparison is
the only thing that found the defect, and it is now `ctcompile_package`'s last
arm:

> A packaged application is SEALED - it answers from what it carries and never
> from the disk - and the vendored OFL faces are loaded THROUGH the asset
> registry. So the first sealed build silently dropped to the bitmap font.
> Exit 0, rendered, looked worse. The packager now asks for the faces the way
> `run_app` does, which puts them in `requested()`, and records the DIRECTORY in
> the bundle because it is part of the registry key.

The test took two tries to mean anything, which is worth remembering: the first
version compared two bitmap-font runs (everything in that file sets
`CTBROWSER_FONTS=font8x8`), and the second still passed with the fonts blinded,
because the packaged arm inherited `CTBROWSER_FONT_PATH` and found the faces
under the names the packaging machine had recorded. **The packaged arm is now
given nothing** - no font path, and a working directory that is not the
application's, which is what "copy it and run it" means.

**WHAT IT DOES NOT DO IS GENERATE NATIVE CODE.** The bytecode still runs on the
interpreter. This deletes the *parse*, which is ~40% of a page load; the
interpreter is 1.4%. Phases 7–12A are the rest and are not started.

## What the last session did

1. **The source hash was 4.16 ms of that page load.** It is now
   `boost::hash2::xxhash_64`, 0.181 ms. **Do not "improve" it to a four-lane
   FNV over 64-bit words** — that was tried, it is faster (0.127 ms), and it
   collides on 50,678 of 262,145 single-byte edits of real p5.js. The plan
   explains why, and `ctcompile/test/ProgramImage.cpp` keeps that hash as a
   blinded control so the case that catches it can be watched failing.
2. **`page-load.json` re-recorded**, 53% → 72%.
3. **Operand validation**: the per-operand switch became a bound table, 19.74 →
   19.18 ms, 15 of 15 paired runs. The "suspect fast path" the previous handoff
   proposed was NOT implemented and should not be — see the plan.
4. **`function_proto::nested` deleted.** Nothing ever wrote it; its only reader
   was a ratchet check that could not fire. Image format 1 → 2, and the image is
   19 KB smaller for p5, 128 KB for babylon.
5. **The measurement tools are built by `all` now** — see the trap below.
6. **ONE PROGRAM PER `<script>`.** The image is keyed per script, so p5 is baked
   once and editing a sketch no longer invalidates 4.5 MB. It is also a
   conformance fix: a parse error or a throw in one script no longer stops the
   next, and each script is its own microtask checkpoint. What it removed is a
   forward call from an earlier script to a later script's function — Chrome
   makes that a ReferenceError too.
7. **Five defects found by adversarially reviewing that split**, three of them
   the split's own and two older: a dead script's `try` catching the next
   script's `throw` (`context::execute` never cleared `handlers_`), and a
   use-after-free on synchronous navigation, now fixed by queueing the load.
8. **The `asan` preset works again** — 29 of 52 tests were failing on a
   heap-use-after-free in the CSS parser that fires on every browser
   construction. 52 of 52 now.

9. **`finally` was wrong on six of nine specified behaviours** and is rewritten
   as a completion record. One of them lost exceptions outright. p5_api moved
   172 → 175.
10. **The 65,535 proto ceiling was three stray casts**, and Babylon sat at 49%
    of it. Gone; 140,001 functions verified.
11. **The fingerprint now hashes what the compiler EMITS**, not only which
    opcodes exist — a canary compiled and folded. The `finally` rewrite is
    exactly the change it was blind to.

12. **The MVP above**, and then an adversarial review of it that found eight
    defects in the packaging path — every one of them SILENT, in the sense that
    the application ran and produced the right document:
    * **module scripts were invisible.** `script_sources()` lists classic
      scripts only and there is no image path into `load_module`, so a page of
      modules packaged as "0 scripts compiled" and the guard that asks whether
      packaging worked read a truthful, useless zero. `module_sources()`
      publishes them now; the packager refuses them and so does the launcher.
    * **the guard was gated on "some images arrived"**, so the case where NONE
      arrived — the most obviously broken package there is — was the one case it
      skipped.
    * **the probe never ticked the page.** `fetch` and `img.src` queue their
      requests and are drained from `tick`; p5 loads in `preload` and Phaser in
      the first game step. Every sprite, atlas and level was missed with no
      warning. It now runs the page until it stops asking (ceiling 60 frames);
      p5-basic settles after one.
    * **the packager resolved assets through a second, base-less registry**
      whose probe order differed from the one that answered the page — the exact
      second copy of the rule `assets.hpp` spends a paragraph forbidding.
    * **a packaged application fell back to the filesystem**, probing the
      working directory first, so a missing resource was answered by whatever
      sat next to the user. Registries can be SEALED now, and `run_bundle` does.
    * `read_bundle` bounded each blob and not the total; `bundle_write_error()`
      was a channel nothing ever wrote to, behind a header promising a check
      that was never implemented.

    All six new guards were removed one at a time and watched going red, each
    for its own message. The one that could NOT be falsified is `write_bundle`'s
    refusal of >4G entries or a >4G name — reaching it needs a bundle no machine
    here can hold. It is written and untested, and that is better said than
    implied.

## Do these next

1. **NOT Phase 16A or 16B, on this corpus.** `docs/baseline/page-load-profile.json`
   profiles what an image-loaded page load actually spends: HTML parsing is
   0.0%, CSS and style 0.5%, layout and paint absent. A compiled DOM blueprint
   and a compiled style program target under one percent between them. 16B is
   still *unblocked* — `engine::for_each_rule` exists — it is just not worth
   doing next for these pages.
2. **The image LOADER is now the largest single item on the path**, at 26%, and
   its operand pass alone is 7.49% — fifteen times the whole CSS engine. That
   is where the next startup millisecond is.
3. **FINISH PHASE 10 FROM THE DECIDED DESIGN.** The next steps, in order, are in
   the panel's verdict: an `OpcodeMapping.hpp` giving `BinaryKind ->
   script::op` spelled by name (never a literal); a `ctjs.runtime_call`
   operation carrying the helper, its role vector and its literals, with a
   verifier that the roles consume the operands and literals exactly; then the
   EmitC slice — `!ctjs.value -> !emitc.opaque<"ctbrowser::script::value">`,
   out-parameters as `emitc.variable` plus `emitc.apply "&"`, and the status
   compared against `ct_aot_status::ok` **by name**, never a baked number
   (`aot.hpp` says outright "THE PRECEDENCE IS THE CONTRACT; THE NUMBERS ARE
   NOT"). Note `ct_aot_enter` fails with a NULL POINTER, not a status.
4. **WIDEN THE IMPORTER.** The importer's refusals are
   counted in `ctjs.skipped` and printed as warnings, so the work list writes
   itself — run it over a corpus and read the histogram. `closure` is the
   largest single item and needs a producer for `!ctjs.program`, which is a
   design question rather than a mapping.
   **Handlers are NOT imported yet**: `push_handler`/`pop_handler` map to
   operations but the importer has no handler-stack reconstruction, so any
   function with a `try` is refused. The design for it is in the Phase 9 brief —
   abstract interpretation over the CFG with a stack of push offsets, since
   there is **no handler table** in `function_proto`.
4. **The rest of Phase 5, and Phase 6.** Phases 1–4 are done and their gates
   are met; Phase 5 is 26 of 69 rows.
   **`ct_aot_intern_name` is the one hard blocker on the path to a minimal
   compiled function.** Every property helper's key is a `const ct_aot_name *`,
   the row asks for an owning immortal pool that does not exist, and
   `lookup_property` today takes a `const std::string &`. Until it exists,
   `o.x` cannot be emitted at all — which is why `ct_aot_get_index` is
   implemented and `ct_aot_get_prop` is not.
   After that, the cheapest real extractions per opcode bought are
   `ct_aot_cell_get`/`ct_aot_cell_set` (four opcodes for eight lines, and they
   unblock every captured variable). Leave `ct_aot_construct` (~90 lines),
   `ct_aot_instance_of` (~56) and `ct_aot_set_index` (~36) until last.
   PREVIOUSLY: **Phases 4, 5 and 6** / **Phases 1–6**, the runtime preparation. Phase 2's gate is MET as of
   2026-08-22 — `ctbrowser/lib/Script/aot_bridge/` has four helper bodies and
   `unittests/unit/aot_basics` calls a hand-authored compiled function from
   interpreted JavaScript. Doing it falsified `ct_aot_catch_land`, which cannot
   be implemented as written; the row says so now. The throwing tier and Phases
   1, 3–6 are still open. WAS: Phase 2's TABLE is done and its GATE is not — nothing has ever called a hand-authored AOT function through
   the ABI, which is the cheapest way to find out whether 1,881 lines of
   contract are right before 68 helper bodies depend on them.

## Known problems, not yet acted on

* **lit LIVES IN A VIRTUAL ENVIRONMENT.** brew's llvm bottle ships FileCheck but
  no llvm-lit, and both Ubuntu's python and brew's refuse `pip install` under
  PEP 668. `python3 -m venv ~/.lit-venv && ~/.lit-venv/bin/pip install lit`, and
  `tools/Brewfile` says so where somebody provisioning a box will read it. With
  no lit, `check-ctcompile` reports that it is unavailable rather than silently
  running nothing.
* **THE ABI TABLE'S LINE CITATIONS ARE SYSTEMATICALLY STALE.** Every row cites
  the runtime that owns its semantics by file and line; Phases 3–5 moved several
  hundred lines of `run_loop.cpp`, `call.cpp` and `vm.hpp`. Six citations
  pointed past the end of a file and are repaired **as names**;
  `ctcompile_def_citations` keeps that class out. **Many more still land inside
  their file while naming a handler that has since moved**, and no machine can
  see that. If you follow a citation and find something else, the row is stale
  rather than wrong about the semantics — the claims were checked, the addresses
  were not re-checked afterwards. Cite by name in anything you touch.

* **A `<script src>` that ships its source TWICE.** `write_image` defaults to
  `keep_source` and `ctcompile` takes the default, so p5.js is 4.5 MB as an
  `asset` (which the run-time walk must re-read to reproduce the hash) and again
  inside its 7.3 MB image. Dropping the source is not free — it is whether
  `f.toString()` returns the text or `[native code]`, and p5's own error system
  reads it — so this is a real decision, not an oversight to tidy.
* **`ctrun` ignores `argv` once a bundle is appended.** `myapp --help` silently
  starts the application.
* **`this_executable_bytes()` is `/proc/self/exe` only**, so a packaged
  application on Windows finds no bundle and prints usage. The cross build
  exists; this half of it does not.
* ~~The 65,535 proto ceiling~~ — FIXED 2026-08-21, it was three casts.
* **OLD, KEPT FOR THE REASONING:** the 65,535 proto ceiling was at 49% on a
  corpus that already existed. Three
  of four `op::closure` emitters cast the function index to `uint16` before the
  32-bit `with_bx` (`compile_function_decl` in `statements/functions.cpp`, `expressions.cpp:95`,
  `classes.cpp:156`; `classes.cpp:109` does not). Above 65,535 protos the
  COMPILER builds the wrong closure. Babylon is 31,905. The image writer refuses
  such a program rather than freezing the bug into a file.
* **The image is keyed to a whole page's concatenated scripts**, because
  `browser::run_scripts` compiles every classic `<script>` into ONE program. So
  editing an inline sketch invalidates the image for the 4.5 MB bundle beside
  it. Splitting per-script is an engine change: `compile_program` hoists
  function declarations across the whole concatenation, so a call in the first
  script to a function declared in a later one works today and would stop.
  **This is what stands between the current win and "bake p5 once, reuse it",
  and it is the highest-value thing left on this path.**
* **`aot_gc` PROVES MUCH LESS OUTSIDE `asan`.** It asserts correct answers under
  forced GC in every build, but a rooting bug is a use-after-free, and reading
  freed memory usually returns the right bytes. Every one of its guards was
  falsified under `asan`, and that is where a regression in them will show.
* **THE `asan` AND `tsan` PRESETS ARE NOT IN THE GATE.** `tools/remote-build.sh`
  runs the default preset only, and the CSS use-after-free above sat there
  through every green run until somebody built asan by hand. It is 52 of 52 now
  and nothing will notice when that stops being true. Running asan in the gate
  costs a second configure and build; deciding that is the next person's call.
* **`ctbrowser`'s benchmarks are still `EXCLUDE_FROM_ALL` with no aggregate**,
  which is the defect that invalidated the first computed-goto measurement and
  then this session's first page-load reading. Fixing them is the same three
  lines as `ctcompile-tools`.
* **The corruption fuzz prints a count it does not assert** —
  `ProgramImage.cpp` reports "1615 of 3205 offsets still loaded" and nothing
  pins it. Pinning it was considered and not done: the number depends on the
  fixture's compiled bytecode, which Phases 13 and 14 renumber deliberately, so
  a ratchet there would churn without signal. If validation changes, prove
  equivalence differentially instead — see the plan's note on the 60,000-mutation
  digest, which is how the bound-table rewrite was shown to be the same function.
* **`@font-face`** — fixed for `url()` in `a2ef736`, but the style engine still
  records a page font only when the family and url are string tokens elsewhere;
  check before assuming.

## How to work here (learned the hard way)

* **BUILD ON THE DEVBOX, ALWAYS**: `./tools/remote-build.sh` from the repo root.
  The WSL box has 7.5 GiB and has been taken down by local builds twice.
* **The devbox self-deallocates after 30 idle minutes.** When ssh times out:
  `cd ../infra/azure-build-server && ./server.sh start`.
* **The devbox shell is zsh, which does NOT word-split unquoted variables.**
  `CXX="clang++ -O2"; $CXX foo.cpp` fails as one word. Inline your flags.
* **Chain gates with `&&`, never `;` — AND NEVER THROUGH A PIPE.** A `;` after
  `tools/format.sh --check` let an unformatted commit through once; on
  2026-08-21 `./tools/format.sh --check | tail -1 && git commit` did it again,
  because a pipeline's exit status is the LAST command's and `tail` always
  succeeds. Redirect to a file and read it, or check the status first.
* **A green build does not mean the binary you are about to run was built.**
  `EXCLUDE_FROM_ALL` targets are not in `all` AT ALL, so `cmake --build`
  rebuilds the engine, relinks every test, reports 97/97 — and leaves an
  excluded executable at whatever revision someone last built by hand. That has
  now produced a wrong number in this tree three times
  (`docs/history/computed-goto.md`, `docs/performance.md`, and
  `ctcompile/docs/baseline/page-load.json`). The ctcompile measurement tools are
  fixed — `ctcompile-tools ALL` in `ctcompile/tools/CMakeLists.txt` — but
  **`ctbrowser`'s benchmarks still have it**, so anything measured with
  `ctbrowser-test-bench_*` must be built explicitly and checksummed.
  Separately, `rsync -az` preserves mtimes, so restoring a file can leave ninja
  thinking it is current; `touch` it. Distrust any figure that exactly matches
  the arm you were replacing.
* **No hardware perf counters on the devbox** (it is a VM) — `perf stat` reports
  `<not supported>`. Use callgrind for attribution, dhat for allocation, and an
  **interleaved A/B of two binaries** for wall clock. Not a before-and-after
  across sessions: the from-source page-load arm moved 7 ms between sessions
  with no commit that could explain it.
* **Profile the thing itself.** One callgrind run profiled a binary that
  compiled the program to build the image, and the compile drowned the load.

## The discipline that has been earning its keep

* **The positive case is one line; the negative cases are the file.** Every
  comparator here is verified against a deliberately BLINDED implementation, and
  every negative case must be seen going red. The source hash's cases go
  further: the blinded hashes live in the test permanently, and each case
  asserts that its control DOES collide, so a case that stops proving anything
  says so instead of passing.
* **Prove a guard is load-bearing by removing it, and say so plainly when it
  does not go red.** Done for both validation fixes in `59d0339` (one did, one
  did not) and for the Boost.Hash2 configure check, which was verified by
  pointing `CTBROWSER_BOOST_INCLUDE_DIR` at a Boost without Hash2.
* **Silence is not success.** Assert counters, never trust output. That guard
  caught a "58x speedup" that was a loader refusing every corpus.
* **Correct yourself in the record.** Four claims have now been committed and
  later corrected here. The most recent: a hoist that "the compiler cannot do"
  and measurably did not need, reverted with the measurement in the plan.
* **A fast algorithm that is quietly wrong is worse than a slow one.** The
  four-lane FNV was faster than what shipped and would have made the image cache
  accept stale code on one edit in five. Prefer somebody else's algorithm AND
  somebody else's code; check it against a third party's answers.

## Phase 10: what was decided, and what was refuted

**`ctjs.runtime_call` was designed, reviewed and NOT BUILT.** A three-lens
adversarial panel refuted it against the checkout. Its only novel content was
carrying the helper as a string, which converts the project's one *build-error*
ABI check — `CTJS_RuntimeOp` concatenates the name into a `helper_id`
enumerator — into a pass-time lookup. Worse, an `OpInterfaceRewritePattern` that
matches every implementer and *produces* an implementer re-matches its own
output until the iteration cap. The role walk it existed to hold is right and
belongs in a header both backends call, not in an IR node.

**The role table now reads the ABI's *failure tier*, not just `may_throw`.**
37 rows declare `may_throw` and only 24 return a status; the rest fail in the
RAISE tier, where the result is always well-formed and the caller polls
`ct_aot_failed` at back-edges. `ct_aot_enter` is in neither tier — it returns
NULL. Emitting a status test after `ct_aot_new_object` tests nothing.

**Two committed checks were wrong and are corrected.** `classify_return` missed
`ct_aot_to_int32`, the row the `.def` exempts by name ("a signed int32 return
that is DATA, not a status"); the mechanical tell is that it takes no frame
handle, and it is the only int32_t row that does not. And `values_only` admitted
the out-parameters it claimed to exclude, because `"uint64_t *out"` starts with
`"uint64_t "` — six rows passed a check whose comment said they could not.

**The shape trait found four live defects the moment it existed.**
`CTJS_ABIShaped` compares every runtime operation's ODS declaration against its
helper's row. It caught `load_upvalue`/`store_upvalue` (an `$index` attribute
against a helper with nowhere to put it — every captured-variable read compiled
to `undefined`), `instanceof` (a `!ctjs.value` result against a `uint32_t` 0/1),
and `delete_property` (a result against a helper that answers with a status).
**There is deliberately no operand-count rule**: the dialect is higher-level
than the ABI, so supplying *fewer* arguments is normal and only excess is
checkable.

**The EmitC entry shape is pinned and compiles.**
`test/Lowering/EmitC/entry-shape.mlir` is the target, not any pass's output.
Callees must be **qualified** (`ctbrowser::aot::ct_aot_*`) because the
`extern "C"` prototypes live inside that namespace — the table's `symbol` is the
LINKER name, not the callee string. `emitc.call_opaque` emits no declaration, so
the TU just includes `aot.hpp`; `emitc.declare_func` is broken in this LLVM
(drops parameter types). `--declare-variables-at-top` is mandatory, because the
NULL test gives every body two blocks.

**THE PIPELINE IS CONNECTED.** `echo 'function f(a) { return a; }' |
ctjs-translate | ctjs-opt --ctjs-lower-to-emitc | mlir-translate --mlir-to-cpp`
produces a translation unit that compiles against the real `aot.hpp`.
`test/Lowering/EmitC/end-to-end.mlir` runs all four stages and the last one is a
C++ compiler. The backend can barely do anything - it refuses almost every
function and records why as `ctjs.not_lowered` - and that is the point: every
operation added from here is an increment on something that demonstrably works.

**Three runtime facts the backend had to be told, none guessable from the IR:**

* **`argv` dies at `ct_aot_enter`.** It is an interior pointer into
  `context::registers_` and `enter` resizes that vector. Parameters are read
  before the call; `ct_aot_slots` cannot recover it, since that hands back the
  compiled frame's own span rather than the caller's window.
* **`new.target` and the callee cannot be delivered at all.** The importer
  prepends three implicit arguments and only `receiver` is in the entry
  signature. `ct_aot_new_target` and `ct_aot_callee` are declared in `aot.hpp`
  and **defined nowhere** — a call to either is a link error. Two more gaps sit
  behind that: `ct_aot_enter` never sets `call_frame::closure`, so `callee`
  would answer `undefined` anyway, and nothing sets `pending_new_target_` on the
  compiled `new C()` path.
* **`--mlir-to-cpp` miscompiles a parallel copy on a block-argument edge** in
  LLVM 22.1.8 — measured by compiling and running it, see
  `block-argument-hazard.mlir`. The importer's register file *is* block
  arguments, so this is every function with a loop that permutes two registers.
  Non-entry block arguments must become `emitc.variable`, reads before writes.
  Until that exists the backend refuses any function with more than one block.

**Smaller things worth not rediscovering:** `--declare-variables-at-top` is
mandatory (EmitC refuses multi-block functions without it) and it declares
every value at the top, so a `const` local is a build error — the argv pointer
is cast once instead, because the signature must stay assignable to
`ct_aot_entry_fn`. `$` in a symbol compiles only as a GCC/Clang extension. And
`%cxx` in `test/lit.cfg.py` is what makes the compile step available to any
EmitC test.

**Control flow compiles now too.** `function g(a) { if (a) { return 1; } return
2; }` reaches a translation unit that compiles. The pipeline is
`ctjs-opt --ctjs-lower-to-emitc --emitc-eliminate-block-arguments`, and **that
order is a correctness requirement**: the first pass emits block arguments, the
second removes them, and what reaches `mlir-translate` must have none.

`--emitc-eliminate-block-arguments` gives each non-entry block argument an
`emitc.variable`, reads it at the top of its block and writes it on each
incoming edge — so every read precedes every write. **Edges are split rather
than assigned in place**, because `cf.cond_br %c, ^B(%x), ^B(%y)` is legal and
carries different values into one block; assigning both sets before the branch
runs both on whichever path is taken. In-place assignment is correct for every
single-successor terminator, which is exactly why the swap test does not catch
it — that case has its own function.

**Number constants are spelled from bits**, never as a decimal literal:
`value::number(std::bit_cast<double>(UINT64_C(...)))`. The attribute carries the
double's bit pattern precisely because `-0.0` and NaN payloads do not survive a
decimal round-trip, and printing decimal would discard that at the last step.

**The out-parameter/status pattern is done**, which is the shape most of the ABI
has. `status_call()` writes it once: a local for the result, its address, the
call, a test against `ct_aot_status::ok` **by name**, and a block split so the
result is loaded only on the surviving path — which the row requires, not merely
permits (`*out` is written only on `CT_AOT_OK`). The failure edge is shared per
function and **tests for `unwound` before leaving**: on that status the unwinder
has already destroyed this frame, so an unconditional `ct_aot_leave` pops
somebody else's.

`a + b`, `!a`, `+a`, `void a`, `a === b`, `a == b` and the four relational
operators all compile now.

**A GAP IN THE ABI, worth knowing before designing against it:** *no row boxes a
machine quantity into a JavaScript value.* `ct_aot_strict_equals` returns a
`uint32_t`, `ct_aot_compare` an `int32_t` ordering, `ct_aot_to_number` a
`double` — and there is no `ct_aot_from_bool` and no `ct_aot_from_double`. In
C++ that boxing is `value::boolean(b).bits()`, a **member call on a temporary**,
which `emitc.call_opaque` cannot spell because its entire output is
`callee(args)`. The backend therefore emits two `static inline` shims into its
own translation unit rather than adding rows to a runtime ABI for a compiler's
convenience. If the ABI ever grows those rows, the shims go.

**The relational operators are not negations of one another.** `ct_aot_compare`
can answer `unordered` — a NaN on either side — which makes all four false,
`>=` included. `a >= b` as `!(a < b)` makes `NaN >= NaN` true. Each is built
from equality tests against the orderings that make it true. Unlike the status
enum, **the ordering's numbers are contractual** and `aot.hpp` says so.

**COMPILED VALUES ARE ROOTED IN THE FRAME, and this was a real shipped bug.**
`a + b + c` kept the first addition's result in a plain C++ local across the
second `ct_aot_binary_op`, which is a safepoint. The collector is precise; a
value in a native frame is reachable from nothing. Under `set_gc_stress` the
compiled body returned six characters where the interpreter returned
sixty-five, and ASan called it a heap-use-after-free. **Without stress it was
correct every time**, which is why every other test passed.

The tell was an inconsistency in our own file: it refused string constants and
`typeof` because "ct_aot_new_string is a safepoint, and nothing roots the result
yet" — while admitting six operations with exactly that property.

Every produced value now goes into a frame slot immediately, and **the span is
re-fetched at every store**: the row says the pointer "IS VALID UNTIL THE NEXT
SAFEPOINT AND NOT ONE INSTRUCTION LONGER". Storing once suffices because the
collector marks and deletes rather than moving. Slots are never reused — a leak
bounded by the frame beats a liveness analysis that is wrong once.

**`ctcompile_gc_roots` is the only test that runs generated code against the
real runtime with the collector hostile**, and it is the only kind that can see
this class of defect: a use-after-free nothing collects is invisible, because
the freed memory still holds the right bytes. It compiles `gc-roots.js` through
the real pipeline at build time. Removing the parking makes it report 6
characters against 65 while "collector idle" still passes.

**Two ordering hazards are now refused rather than documented.**
`--emitc-eliminate-block-arguments` walks `emitc.func` only, so run *before* the
lowering it silently does nothing and the block arguments reach `mlir-translate`
— measured: `sl(10,20,2)` answers 20 where 10 is correct, every tool exiting 0.
It now refuses to run when a `ctjs.func` remains. And `ctjs.frame_exit` must be
the last thing before the return, or the shared failure path leaves the frame
twice (harmless — `leave` truncates to its own index — but unchecked).

**Property reads and calls compile.** A call is the first operation needing the
frame for something other than rooting: `ct_aot_call` takes a **contiguous**
`argv`, and the arguments are rooted in scattered slots — so each call site
reserves a run in the register window and copies them in just before the call.
In the frame, not a C++ array: the call is a safepoint that runs user JavaScript
before reading them. The GC test now exercises exactly that.

**ONLY 32 OF THE 69 ABI ROWS HAVE IMPLEMENTATIONS.** `aot.hpp` declares all of
them; `lib/Script/aot_bridge/` defines 32. A call to one of the other 37 **compiles
perfectly and fails at link** — and that shipped: `ct_aot_global_get` and
`ct_aot_negate` were emitted for two commits with a green suite, because every
EmitC lit test uses `-fsyntax-only`. `runtime_defines()` in `CTJSToEmitC.cpp` is
the list, and **`ctcompile_linkable`** keeps it honest in both directions by
linking a TU that exercises everything the backend accepts.

**Two limits that are upstream of the backend**, found writing that fixture:
`+a` never arrives — the *importer* has no CTJS operation for `op::to_number`,
so `ctjs.unary plus` is reachable only from hand-written IR. And `undefined` is
a **global read** in JavaScript, so it needs the helper with no body; an
uninitialised local is the same value and reaches nothing.

**The four tests that can see what lit cannot**, and each catches a failure the
other three are green for:

| test | asks | why nothing else can |
|---|---|---|
| `ctcompile_differential` | is the ANSWER right? | fluent, linkable, rooted code can still compute the wrong thing |
| `ctcompile_gc_roots` | do values survive a collection? | a use-after-free nothing collects still holds the right bytes |
| `ctcompile_linkable` | do the symbols exist? | a declared-but-undefined helper compiles perfectly |
| `%cxx` in each lit test | does it agree with `aot.hpp`? | a signature the backend invented looks fluent |

The differential test's inputs **separate** the lowerings rather than covering
them — an object with a `valueOf` for the two `+` families, `NaN` for the
relational operators, `0` against `"0"` for the equalities. A case whose answer
is the same whether or not the compiler is right is worse than no case.

**The two global rows are implemented** (`context::global` and
`define_global` — the same lines the interpreter runs, so the tiers cannot
drift), so globals compile and run. That is the pattern for the rest: most rows
say "DELEGATES TO" a `context` method that already exists.

**Next**: `ct_aot_set_index` and the other 36 unimplemented rows are the
critical path now — the backend can lower more than the runtime can execute.
Either implement rows in `lib/Script/aot_bridge/`, or widen into what is already
implemented: `ct_aot_new_object`, `ct_aot_new_array` and `ct_aot_truthy` are
there, `ct_aot_append` and `ct_aot_construct` are not, so object and array
literals are half-reachable.

## Using subagents

The last two sessions used `Workflow` heavily and it paid for itself. Ask agents
to REFUTE a design against the code, not to agree with it. Their best output has
been defects in already-committed work: two memory-safety holes in the image
loader, and two stale standing decisions in `ctbrowser/docs/` that this session's
Boost floor change had invalidated.
