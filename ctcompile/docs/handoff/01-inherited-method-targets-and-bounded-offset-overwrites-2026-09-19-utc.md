[Back to HANDOFF.md](../HANDOFF.md)

## Inherited method targets and bounded offset overwrites, 2026-09-19 UTC

Resumed seven dirty ctcompile paths left by the session explicitly abandoned
at 14:30:15 in AGENT-SYNC: inherited methods and the frozen Number-offset escape
draft. Both agents' histories, unmerged branches and current handoffs were read;
September 7 WIP is already an ancestor. Three agents supplied escape review,
concrete native defect findings and the authentic Bootstrap continuation; two
hit service limits and root finished integration. The inherited draft's dominance
and duplicate constructor-read hazards were fixed before the final gate.

**fb7d6a3d** admits inherited ordinary methods over proved local explicit-super
chains, including same-receiver calls from base constructors, three-level chains,
separate base/leaf instances and new nonoverriding leaf methods. It reuses the
existing immutable prototype and borrowed-receiver lowering. Every original
method body still passes the complete census. Overrides, lexical super, inherited
DOM bodies and receiver-selected constructor/getter identities remain refused.

Five new positive sources add **40 native executions**; their observations are
7, 14, 273, 7 and 14. Twelve new source specimens plus a hoisted-prototype IR
control cover the boundary. All **173 previous split-file sections** are unchanged.
The insertion point follows completed base setup, so inherited keys/closures
always dominate their new slots. Per-leaf checks reject shadowing from inherited
methods or base constructors and reject even unused constructor reads, avoiding
duplicate erasure records. Constructor completion is checked after private super
normalization. No Script/VM dependency or prototype storage is emitted.

**9876bc23** finishes the interrupted bounded Number-offset overwrite proof:
one-level `i + offset`, `offset + i` and `i - offset` retain the original stride
and prove both endpoint bounds. Every guard reload must miss every fixed and
shifted write. Exact replay, saved children, remaining aliases, historical cycles
and work limits remain. Sixteen source functions plus CFG/SCF controls cover
signed offsets, gaps, zero trips, mutation, String Add and out-of-bounds writes.

Focused validation on the devbox, serialized by `/tmp/ctbrowser-devbox-build.lock`:

- `tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-escape-analysis-arrays
  ctcompile-test-escape-claims ctcompile-test-type-oracle`.
- `ctest --test-dir projects/compile-time-browser/build --output-on-failure
  --no-tests=error -R '^ctcompile_escape_analysis_arrays$'`: **1/1, 1.28s
  (1.29s total)**.
- `~/.lit-venv/bin/lit -sva projects/compile-time-browser/build/ctcompile/test
  --filter='^ctcompile :: Analysis/Escape/escape-claims/(offset-index-overwrite|visited-index-overwrite|disjoint-index-overwrite)[.]test$'`:
  **3/3, 0.11s**. New oracle **48 sites / seven sound / 7 of 13 confined
  precision**; existing **31 / 6 / 6 of 7** and **25 / 5 / 5 of 7**. All have
  zero violations, partial, pending and unclaimed sites.
- `tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-host-contract
  ctcompile-test-native-reference`; same CTest command with
  `-R '^ctcompile_host_contract$'`: **1/1, 0.48s (0.49s total)**.
- `~/.lit-venv/bin/lit -sva -j2 projects/compile-time-browser/build/ctcompile/test
  --filter='^ctcompile :: CTNative/Lowering/Objects/class-(initialization|dom)[.]mlir$'`:
  **2/2, 307.87s total**. Initialization: **178 source observations,
  452 main native executions, 356 unprepared/218 preparation
  refusals**. Additional 28 native executions/30 refusals and 11 prepared refusals.
  DOM remains **632 Node/interpreter observations, eight native executions and
  4,910 refusals**. Individual final case timings were not recorded.

The first narrow probe failed in the new test helper because its required
`success=True` argument was missing; corrected and synced only that Python file.
Production builds passed. The corrected narrow probe passed **23 source
observations / 72 native executions / 46 unprepared and 27 preparation refusals**,
plus 24 existing executions/24 refusals. Final code/test hashes match the devbox
(four escape and three native). The generated leaf-method C++ was inspected.

Required `tools/format.sh --check` reports **26 existing diagnostics in nine
HEAD-identical files** and stops in its C++ phase. Changed C++/Python formatting,
Python syntax, shell syntax and `git diff --check` pass. No browser/Script edits,
semantics or new WPT/test262 measurements. Full CTest/compiler lit, standalone
transaction/lifetime checks, broad native/corpus matrices, WPT/test262 and whole
Bootstrap were skipped. These focused passes are not a full-suite result.
Evidence and gate scripts: `/tmp/ctcompile-inherited-1438/`. No push.

**Next native:** select the nearest method definition for a proved override,
while retaining all shadowed bodies in the complete source census. The existing
`inherited-method-override` observes 21; it currently refuses. Then prove lexical
`super` from the declaring method's immutable home and immediate base while
preserving the final receiver. `inherited-dispatch` observes 118 and preserves
W's shadowed `_getConfig`: B's constructor selects Qi's override, Qi's super
selects B, and B's ordinary merge call selects W. Reuse existing constructor
prototype and method receiver proofs; differing leaf targets may still refuse.

Complete authentic W/B/Qi remains beyond these local sources. W's current source
fixture stops at helper captures; the DOM route also needs B's captured constructor
helpers and ordinary static methods, complete H/r/s bodies, configuration,
selectors, events and Popper. Do not invent H calls or parameter authority, remove
shadowed methods, or just relax capture checks. Default rest/apply, replacement
returns, fields, new.target and broader constructor effects remain. Part 25 still
needs own-data definition provenance and broader ownership/control flow; the
application driver remains incomplete.


## Explicit super construction and visited-index reloads, 2026-09-19 UTC

Resumed clean **ca62d3e8** and the explicit-super thread in the latest
HANDOFF/master 00/24 and AGENT-SYNC's 13:47 closure. Both agents' histories and
unmerged branches were reviewed. Contrary to earlier journal wording, the
September 7 WIP branch exists, but it is already an ancestor of `ctcompile-v1`;
no recovery merge was needed. Three agents returned super/fixture reviews and a
four-file escape draft before service limits; root completed integration and gates.

**883cac8e** admits explicit `super(...)` statements for field-only local
class chains. The private candidate proves the fresh Boolean guard, one ordered
base invocation, immediate new.target forwarding, receiver binding, absent field
initializers and final receiver completion. Base bodies must return undefined
and cannot observe new.target. Argument producers and field writes stay in source
order on the same final receiver. Only completely proved, unconstructed base
closures with no remaining callable/symbol use disappear. A separately constructed
base remains. Refusal publishes no partial source transformation.

Four sources now execute natively: the predecessor's unchanged explicit-super
specimen, ordered derived writes, a three-level chain, and separate base/derived
instances. Their observations are **7, 312, 13 and 27**; optimized/unoptimized,
explicit/deduced C++ and both compilers give **32 new native executions**. Generated
C++ uses a stack object and borrowed receiver pointer with existing finite native
scalar helpers; no Script/VM or prototype storage appears. All **164** previous
split-file JavaScript specimens retain their statements. Nine source specimens
and eight guard/identity/budget mutations were added.

**0be3bd32** proves current-index overwrites whose actual `start + n * stride`
visits exclude every invariant guard-array reload. Reload indices already have
own-element bounds; fixed-write overlap still refuses. Exact replay, saved-child
retention, historical cycles and work limits remain. Six CFG/six SCF controls and
ten source functions cover starts, stride gaps, zero trips, aliases and overlap.

**Next native:** `ClassInitialization.cpp::examine` still refuses inherited method
keys before super normalization. Prove inherited target tables and original bodies,
keeping leaf `this` dispatch separate from lexical `super` selection. The original
W/B fixture still stops at its captured-helper proof; default derived rest/apply,
replacement/primitive returns, public field initializers, observed new.target,
constructor declarations/global writes and broader control flow remain outside
this slice. Exercise inheritance with the DOM provider and authentic W/B/Qi calls;
do not insert H calls, invent parameter facts or erase shadowed bodies. Real
configuration, selectors, events, Popper and the application driver remain.
Part 25 own-data definition provenance and broader ownership/control flow remain.
This is a native inheritance gain, **not whole-Bootstrap admission**.

Focused devbox validation, all under `/tmp/ctbrowser-devbox-build.lock`:

- `tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-escape-analysis-arrays
  ctcompile-test-escape-claims ctcompile-test-type-oracle`.
- `ctest --test-dir projects/compile-time-browser/build --output-on-failure
  --no-tests=error -R '^ctcompile_escape_analysis_arrays$'`: **1/1, 1.27s
  (1.28s total)**.
- `~/.lit-venv/bin/lit -sva projects/compile-time-browser/build/ctcompile/test
  --filter='^ctcompile :: Analysis/Escape/escape-claims/(visited-index-overwrite|disjoint-index-overwrite|own-element-overwrite)[.]test$'`:
  **3/3, 0.11s**. New oracle: **31 sites / 6 sound / 6 of 7 confined precision**.
  Existing oracles: **25 / 5 / 5 of 7** and **37 / 6 / 6 of 15**. All have zero
  violations, partial, pending and unclaimed sites.
- `tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-host-contract
  ctcompile-test-native-reference`; the same CTest command with
  `-R '^ctcompile_host_contract$'`: final **1/1, 0.48s**.
- `~/.lit-venv/bin/lit -sva -j2 projects/compile-time-browser/build/ctcompile/test
  --filter='^ctcompile :: CTNative/Lowering/Objects/class-(initialization|dom)[.]mlir$'`:
  **2/2, 304.36s total**. Initialization: **166 source observations, 412 main native
  executions, 332 unprepared/211 preparation refusals**; additional **28 native
  executions / 30 refusals**, plus **11 prepared refusals**. DOM classes retain
  **632 Node/interpreter observations, eight combined native executions and
  4,910 refusals**. Individual final case timings were not recorded.

The first native build found const-qualified MLIR wrapper accessor errors; fixed
before testing. The first class case failed **137.46s** after successful super
normalization because the unused base closure blocked native lowering. Complete
body proof plus dead-identity removal fixed that boundary. A cleanup-order review
kept the heritage load alive until its call was erased; the next direct probe
caught its missing census admission, corrected by retaining it through the census.
The resulting narrow probe passed **nine observations / 24 native executions /
18 unprepared and 19 preparation refusals**, plus 24 existing native executions /
24 refusals. Final tests include two later source controls as well. Earlier host
check: 1/1, 0.47s. No browser/Script source or semantics changed.

All seven final code/test hashes match the devbox. Required
`tools/format.sh --check` completes with **26 baseline diagnostics in nine
HEAD-identical files**; changed C++/Python formatting, Python/source-JavaScript
syntax/execution and gate shell syntax/whitespace checks pass. Source preservation
was checked against HEAD before the native commit. Logs, scripts and an inspected
generated C++ specimen: `/tmp/ctcompile-super-1350/`.

Full CTest/compiler lit, standalone transaction/lifetime checks, broad native and
corpus matrices, WPT/test262 and whole Bootstrap were skipped. Focused passes are
not full-suite or compliance measurements. No push; the plan remains unfinished.


## Ordered class ancestry and disjoint overwrite reloads, 2026-09-19 UTC

Resumed clean **7725dc25** and the W/B/Qi plus disjoint-index threads explicitly
abandoned at 13:15:53 in AGENT-SYNC. Both agents' histories and unmerged branches
were read; September 7 WIP is absent and already recorded as merged. Three agents
supplied ancestry/fixture reviews and an escape draft; two hit service limits.
Root completed the draft, integrated the native work and ran focused gates.

**95120020** proves ordered local heritage edges: an earlier completed base,
a later derived completion, unique local constructor identities, and the exact
fresh attached prototype. Bases can pass setup validation without direct
construction. Receiver-use checks include method names from the proved base setup;
these names do not select inherited targets or establish the final receiver.
Self/repeated heritage, reordered base completion, a different prototype and an
undeclared helper are pinned by five refusal controls.

This advances **structural validation only**. Every derived class still refuses
before rewriting: the original default-derived and three-level dispatch sources,
and a new explicit-super source, reach `derived class requires receiver-preserving
super normalization`. The complete original Bootstrap W/B fixture instead reaches
`class method capture is not its constructor or an inert sibling helper` in W;
its manifest now also declares the original Error getter. No original JavaScript
statements were removed: all 163 earlier split-file bodies remain. No inheritance
execution, generated browser API, or whole-Bootstrap admission was added.

**0943b287** completes the interrupted disjoint-index escape draft. It gathers
all guard-array slots reloaded by strides, keys and receivers, then checks them
against every fixed-index store after the complete loop census. Overlap and
current-index writes remain conservative. Exact replay retains saved children,
remaining aliases and historical cycles. One historical key-reload body was
preserved and promoted; four CFG/four SCF controls and eight source functions
cover the extension. **c0924d11** strengthens the later-key-reload control by
actually changing its index, so the returned child must remain reachable.

Focused devbox validation, serialized by `/tmp/ctbrowser-devbox-build.lock`:

- `tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-escape-analysis-arrays
  ctcompile-test-escape-claims ctcompile-test-type-oracle`.
- `ctest --test-dir projects/compile-time-browser/build --output-on-failure
  --no-tests=error -R '^ctcompile_escape_analysis_arrays$'`: **1/1, 1.27s
  (1.28s total)**.
- `~/.lit-venv/bin/lit -sva projects/compile-time-browser/build/ctcompile/test
  --filter='^ctcompile :: Analysis/Escape/escape-claims/(disjoint-index-overwrite|invariant-element-overwrite|disjoint-element-reload)[.]test$'`:
  **3/3, 0.11s**. New oracle initially **25 sites / 5 sound / 5 of 8 precision**;
  existing oracles **28 / 5 / 5 of 7** and **45 / 12 / 12 of 21**.
- After the one-line test refinement, only `disjoint-index-overwrite.test` was
  synced and selected with the same lit command: **1/1, 0.11s; 25 sites / 5 sound /
  5 of 7 precision**. Every oracle has zero violations, partial, pending and
  unclaimed sites. This precision change is test data, not another analysis gain.
- `tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-host-contract
  ctcompile-test-native-reference`; same CTest command with
  `-R '^ctcompile_host_contract$'`: final **1/1, 0.49s**.
- `~/.lit-venv/bin/lit -sva -j2 projects/compile-time-browser/build/ctcompile/test
  --filter='^ctcompile :: CTNative/Lowering/Objects/class-(initialization|dom)[.]mlir$'`:
  **2/2, 303.86s total**. Class initialization: **157 source observations,
  380 main native executions, 314 unprepared/198 preparation refusals**;
  additional **28 native executions / 30 refusals**, plus **11 prepared refusals**.
  DOM classes: **632 Node/interpreter observations, eight combined native
  executions, 4,910 refusals**. No individual final case timings were recorded.

The first native case failed **137.06s** on the inherited receiver's missing base
method names; the second failed **137.60s** on the Bootstrap fixture's stale
expected diagnostic. Direct probes confirmed both boundaries. The first required
the inherited-name census; the second only corrected the expectation to the actual
captured-helper refusal. Source bodies stayed intact. Both builds and host runs
passed; the earlier host measurement was 0.48s. The final replay synced only the
changed Python expectation, reusing the successful production build.

All seven final code/test hashes match the devbox (four escape, three native).
Required `tools/format.sh --check` completes with the same **26 baseline
diagnostics in nine HEAD-identical files**; changed C++/Python formatting, Python
and new JavaScript syntax/execution, gate shell syntax and whitespace checks pass.
An independent final review found no blocking issue and confirmed that no derived
class can reach rewriting. Evidence and exact gate scripts:
`/tmp/ctcompile-inheritance-1324/`.

**Next native:** normalize the original explicit-super constructor on a private
candidate. Prove the super guard cell and one ordered base call, preserve argument
and effect order, map the base body onto the same final receiver/new.target, and
handle bind-this, field initialization and derived completion. A first slice can
refuse replacement-object returns and fields; default derived constructors also
need their original rest/apply proof. Keep leaf `this` dispatch distinct from
lexical `super` target selection. Do not remove the current refusal merely because
ancestry or inherited method names were recorded. The original W/B source still
needs its captured helpers and complete bodies proved under the appropriate DOM
provider; do not invent H calls or parameter facts or erase shadowed W methods.
Configuration, selectors, events, Popper and the application driver remain.
Part 25 still needs own-data definition provenance and broader control flow and
ownership. Current-index stores disjoint from the actually visited induction
range are a separate escape continuation; this change handles fixed stores.

Full CTest/compiler lit, standalone transaction/lifetime checks, broad native
and corpus matrices, WPT/test262 and whole Bootstrap were skipped. Focused passes
are not full-suite or compliance measurements. Browser/Script sources and behavior
are unchanged. No push; the plan remains unfinished.


## Inheritance helper declarations and invariant overwrites, 2026-09-19 UTC

Resumed clean **57d8d421** and the W/B/Qi thread in the latest handoff and
12:40 AGENT-SYNC closure. Both agents' histories and unmerged branches were read;
September 7 WIP is absent and already recorded as merged. The predecessor's dirty
escape work was already committed. Three parallel agents returned findings before
service limits; none left edits. Root completed both changes and focused gates.

**1d0c953a** declares the mutable helpers emitted by original inheritance:
`__ctbrowser_class_heritage` (three arguments), `__ctbrowser_bind_this` (one),
`__ctbrowser_init_fields` (two), and `__ctbrowser_super_get` (three). The existing
manifest/binding proof accepts only their exact direct invocation shape with an
undefined receiver. Replacement, reflection, passing the callable itself, invalid
arity and conflicting declarations refuse. Identity supplies no argument or
effect proof; unknown helper effects still refuse, and DOM declarations remain
until their operations receive semantic normalization. Original W/B and dispatch
sources now declare the helpers and retain their precise inheritance refusal.
No source bodies or runtime operations were removed; this adds **no inheritance
or whole-Bootstrap admission**.

**2a00a6de** proves finite loops that overwrite an invariant existing index of
the guard array. It reuses exact allocation identity, original length and the
bounded invariant resolver. Changing keys, growth, guard-element-dependent
strides/keys/receivers and unsupported effects still refuse. Original replay
preserves saved children, remaining aliases and historical cycles. Five CFG/four
SCF controls and nine source functions were added; seven historical refusal
bodies were preserved and promoted to exact contents/read/return expectations.

Focused devbox validation, always under `/tmp/ctbrowser-devbox-build.lock`:

- `tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-host-contract
  ctcompile-test-native-reference`.
- `ctest --test-dir projects/compile-time-browser/build --output-on-failure
  --no-tests=error -R '^ctcompile_host_contract$'`: **1/1, 0.48s**.
- `~/.lit-venv/bin/lit -sva projects/compile-time-browser/build/ctcompile/test
  --filter='^ctcompile :: CTNative/Lowering/Objects/class-initialization[.]mlir$'`:
  **1/1, 196.32s; 156 source observations, 380 main native executions,
  312 unprepared and 192 preparation refusals**. Existing additional controls:
  **28 native executions / 30 refusals**, plus **11 prepared-source refusals**.
- `tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-escape-analysis-arrays
  ctcompile-test-escape-claims ctcompile-test-type-oracle`.
- Same CTest command with `-R '^ctcompile_escape_analysis_arrays$'`:
  **1/1, 1.25s (1.26s total)**.
- Same lit command with
  `--filter='^ctcompile :: Analysis/Escape/escape-claims/(invariant-element-overwrite|aliased-element-overwrite|own-element-overwrite)[.]test$'`:
  **3/3, 0.11s**. New oracle: **28 sites / 5 sound / 5 of 7 confined precision**;
  alias oracle **37 / 10 / 10 of 16**; current-element oracle **37 / 6 / 6 of 15**.
  All have zero violations, partial, pending and unclaimed sites.

The first native build caught a test-only StringLiteral/std::string mismatch;
corrected before tests. An intermediate host **1/1, 0.47s** and class case
**1/1, 196.41s** passed before the final unknown-effect control and preserving
unconsumed DOM declarations. The first arrays run failed **23 assertions in seven
old refusal rows (1.28s)**; source bodies were unchanged, and the final expectations
check the newly proved contents instead. The recorded failures required only test fixes.
All ten final tested hashes match the devbox. All original class split-file source
bytes are unchanged. Required `tools/format.sh --check` retains **26 baseline
diagnostics in nine HEAD-identical files**; changed C++/Python formatting, Python
and source-JavaScript syntax/execution, gate shell syntax and whitespace checks pass.
Exact scripts, logs and hash evidence: `/tmp/ctcompile-inheritance-1243/`.

**Next native:** normalize an original ancestry chain on the private candidate.
The helper identities are now expressible; they are not semantic authority.
`ClassInitialization.cpp::examine` still needs completed base setup without direct
base construction, immutable prototype/home relationships, exact final receiver
and new.target, super guards/rebinding/field initialization, constructor returns
and argument/effect order. Start with explicit super; default derived constructors
also require rest/apply proof. Keep B -> Qi._getConfig dynamic dispatch distinct
from Qi's lexical super -> B._getConfig and B -> W._mergeConfigObj. Preserve every
original body, including shadowed W methods, without invented H calls or parameter
facts. Real configuration/selectors/events/Popper, Part 25 own-data definition
provenance and broader control flow/ownership, and the application driver remain.

Full CTest/compiler lit, class DOM/transaction cases, standalone lifetime and broad
native/corpus matrices, WPT/test262 and whole Bootstrap were skipped. No full-suite
pass or browser compliance gain is claimed. Browser/Script sources and behavior
are unchanged. The plan remains unfinished. No push.


## Aliased overwrite receivers and inheritance boundary, 2026-09-19 UTC

Resumed **6034e970** and the inheritance/escape threads abandoned at 11:59:25
in AGENT-SYNC. The two dirty escape files were preserved and completed first.
Three agents investigated independent work; two hit service limits before edits,
and root completed implementation, focused gates and commits.

**c27bd349** proves invariant overwrite receivers that retain the guard array's
exact allocation identity, including saved aliases and reloads through disjoint
containers. Changing/unrelated receivers and reads from overwritten storage still
refuse. Original replay, saved-child retention, historical cycles and work limits
remain. Added CFG/SCF controls and nine source functions; earlier bodies remain.

Focused devbox validation under `/tmp/ctbrowser-devbox-build.lock`:

- `tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-escape-analysis-arrays
  ctcompile-test-escape-claims ctcompile-test-type-oracle`.
- `ctest --test-dir projects/compile-time-browser/build --output-on-failure
  --no-tests=error -R '^ctcompile_escape_analysis_arrays$'`: **1/1, 1.26s
  (1.27s total)**.
- `~/.lit-venv/bin/lit -sva projects/compile-time-browser/build/ctcompile/test
  --filter='^ctcompile :: Analysis/Escape/escape-claims/(aliased-element-overwrite|disjoint-element-reload|own-element-overwrite)[.]test$'`:
  **3/3, 0.50s**. New oracle: **37 sites / 10 sound / 10 of 16 confined
  precision**. Disjoint reload: **45 / 12 / 12 of 21**; original overwrite:
  **37 / 6 / 6 of 15**. All have zero violations, partial, pending and unclaimed
  sites. First arrays run failed one predecessor expectation: the returned array
  cannot be discharged. Only that expectation changed; its source was retained.

**6d42e0e8** gives unsupported class heritage calls a precise refusal:
`class inheritance requires proved heritage, receiver and super initialization`.
The source regressions retain complete original Bootstrap W/B classes and r/a
helpers, executing B's real missing-element early exit. A separate three-level
fixture distinguishes dynamic receiver dispatch from lexical super dispatch.
Node/interpreter observe **7** for the base specimen and **118** for the
three-level dispatch case.
All 162 earlier split-file JavaScript bodies remain unchanged apart from comments.
The inherited-static-getter comment now reflects its existing agreeing Node/VM
expectations. This is diagnostic and regression work, **no inheritance admission**.

- `tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-reference`.
- `~/.lit-venv/bin/lit -sva projects/compile-time-browser/build/ctcompile/test
  --filter='^ctcompile :: CTNative/Lowering/Objects/class-initialization[.]mlir$'`:
  **1/1, 196.14s; 156 source observations, 380 main native executions,
  312 unprepared and 192 preparation refusals**. Existing constructed-method,
  r-helper and prototype-key controls add **28 executions / 30 refusals**;
  **11 prepared-source refusals** retain original calls/exits.

The first native lit attempt failed after 139.42s before class proof: custom
`cf.switch` parsing rejected a grouped SCF default operand in the explicit-super
fixture. The two new fixtures now use standard `--mlir-print-op-generic`; source
programs and every IR operation/operand are preserved. An intermediate replay
passed in 196.06s; the final gate repeated after moving the diagnostic into the
existing constructor-use refusal branch, preserving admission behavior.

All seven tested source/test hashes match the devbox. Required
`tools/format.sh --check` retains **26 baseline diagnostics in nine HEAD-identical
files**; changed C++/Python formatting, JavaScript and shell syntax, source
execution and whitespace checks pass. Exact scripts/logs/hashes:
`/tmp/ctcompile-inheritance-1215/`. No browser/Script source or semantics changed.

**Next native:** normalize one proved original ancestry chain on the existing
private candidate. Prove the mutable heritage, receiver-binding and field-init
helper identities; completed base setup, immutable prototypes/homes, exact
new.target, super guards/rebinding, constructor returns and argument/effect order.
Default derived constructors additionally need rest/apply proof. Keep declaring
class and effective receiver distinct: B's constructor calls Qi._getConfig;
Qi's super call selects B._getConfig; B's same-this merge selects W._mergeConfigObj.
Inherited getters retain Qi as receiver. W's shadowed method is still a complete
original body and gains no parameter facts from B's similarly named method.
Then follow real configuration, selectors, events and Popper. Never manufacture
H calls or delete unproved siblings. Part 25 own-data definition provenance,
broader loop mutation/control flow, ownership and the application driver remain.

Full CTest/compiler lit, class DOM/transaction/host cases, separate lifetime and
broad native/corpus matrices, WPT/test262 and whole Bootstrap were skipped.
No whole-suite pass, native Bootstrap admission gain or compliance count is
claimed. The overall plan remains unfinished. No push.

## Constructor-origin DOM calls, 2026-09-19 UTC

**a0644324** resumes clean **2f279dc4** and the actual Dropdown caller thread
in the preceding handoff and 11:17 AGENT-SYNC closure. Both agents' histories and
unmerged branches were reviewed; September 7 WIP is already an ancestor.
Parallel agents identified the constructor-call gap and completed a separate
escape proposal; root integrated the native change and owns validation/commits.

Bootstrap B's constructor calls `this._getConfig(i)`. The native reachability
census now starts from each proved original constructor as well as subsequent
entry calls. Exact same-`this` edges retain actual arguments, instance identity,
initialization and DOM-write order. Reachability supplies no parameter types:
the complete private DOM proof still checks every body and original call.
Three positive and six refusal source cases cover direct/transitive calls,
repeated construction, later entry calls, invalid inputs, missing field state,
dead invalid operations and unused parameterized siblings. All earlier source
bodies are unchanged. Both DOM providers reproduced the old reachability refusal
on the constructor-only source before the fix.

Focused devbox checks under `/tmp/ctbrowser-devbox-build.lock`:

- `tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-reference
  ctcompile-test-exception-recovery ctcompile-test-host-contract`.
- `ctest --test-dir projects/compile-time-browser/build --output-on-failure
  --no-tests=error -R '^ctcompile_exception_recovery$'`: **1/1, 4.55s**.
- Same CTest command with `-R '^ctcompile_host_contract$'`: **1/1, 0.48s**.
- `~/.lit-venv/bin/lit -sva projects/compile-time-browser/build/ctcompile/test
  --filter='^ctcompile :: CTNative/Lowering/Objects/class-dom[.]mlir$'`:
  **1/1, 286.13s; 632 Node/interpreter observations, eight GCC/Clang native
  executions, 4,910 refusal checks**. Ownership and Script/dispatch/link
  exclusions pass; existing pinned VM differences remain.

**Next native:** follow original `W -> B -> Qi` inheritance and configuration.
Constructor-origin reachability is a prerequisite, not inheritance admission.
Class initialization still needs exact base/derived constructor, prototype,
lexical-home and receiver proofs; W and B are not directly constructed in that
path. Keep complete original source and real configuration, selector, event and
Popper dependencies. The one-slot full-H source still refuses in the class gate;
never insert helper calls or guess its unused parameters. No whole-Bootstrap
admission gain or browser/Script behavior change is claimed.

**1d0d2a65** independently completes disjoint stride reloads during guarded
own-element overwrite loops. Every recursive element read is compared with the
actual guard-array allocation. Disjoint arrays remain invariant; saved, nested
and cyclic aliases of the overwritten array still refuse. Original length reads,
exact replay, historical cycle checks and work limits remain. Seven CFG rows,
five SCF rows and 11 source functions were added; earlier bodies are unchanged.

Focused escape validation under the same devbox lock:

- `tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-escape-analysis-arrays
  ctcompile-test-escape-claims ctcompile-test-type-oracle`.
- `ctest --test-dir projects/compile-time-browser/build --output-on-failure
  --no-tests=error -R '^ctcompile_escape_analysis_arrays$'`: **1/1, 1.25s
  (1.26s total)**.
- `~/.lit-venv/bin/lit -sva projects/compile-time-browser/build/ctcompile/test
  --filter='^ctcompile :: Analysis/Escape/escape-claims/(disjoint-element-reload|own-element-overwrite|invariant-reload)[.]test$'`:
  **3/3, 0.11s**. New oracle: **45 sites / 12 sound / 12 of 21 confined
  precision**. Existing overwrite: **37 / 6 / 6 of 15**; invariant reload:
  **33 / 15 / 15 of 24**. All report zero violations, partial, pending and
  unclaimed sites. Broader mutations/control flow and actual own-data definition
  provenance for ordinary objects remain open.

No focused build or test failed. All six tested source/test hashes match the
devbox. Required `tools/format.sh --check` reports **26 baseline diagnostics in
nine HEAD-identical files**; changed C++/Python formatting, source JavaScript
syntax/execution, gate shell syntax and whitespace checks pass. The native driver
preserves all earlier sources (1,444 recorded source/check entries compared).
Exact scripts, logs and hashes: `/tmp/ctcompile-h-1119/`.

Full CTest/compiler lit, separate lifetime and broad native/corpus matrices,
standalone RegExp/broad DOM-String cases, WPT/test262 and whole Bootstrap were
skipped. No whole-suite, whole-Bootstrap admission or new browser compliance
claim. The application driver and overall plan remain unfinished. No push.

## Actual H callers and guarded array overwrites, 2026-09-19 UTC

**75e341e0** continues clean **aa80f5b5** and the full-H authority boundary
recorded in the 11:04 AGENT-SYNC closure. Both agents' histories and unmerged
branches were reviewed; September 7 WIP is already an ancestor. Three agents
supplied call-graph and escape findings before service limits; root implemented
and validated the escape change. No predecessor edits were pending.

Finite array loops now permit overwriting their current own element. The store
must use the guard array and its transported induction index inside the guarded
body. Resizing, other targets/keys, effects and element-reloading strides refuse.
Existing exact replay retains saved values, bounds, every historical cycle edge
and work limits. Three old refusal bodies are unchanged and now prove; added
CFG/SCF rows and 12 source functions cover release, saved children, skipped/zero
trips, changing strides, resizing and retained/transient cycles.

Focused devbox validation, always under `/tmp/ctbrowser-devbox-build.lock`:

- Built `ctjs-opt ctjs-translate ctcompile-test-escape-analysis-arrays
  ctcompile-test-escape-claims ctcompile-test-type-oracle` with
  `tools/remote-build.sh`.
- `ctest --test-dir projects/compile-time-browser/build --output-on-failure
  --no-tests=error -R '^ctcompile_escape_analysis_arrays$'`: **1/1, 1.24s
  (1.25s total)**.
- `~/.lit-venv/bin/lit -sva projects/compile-time-browser/build/ctcompile/test
  --filter='^ctcompile :: Analysis/Escape/escape-claims/(own-element-overwrite|invariant-reload|invariant-length-reload)[.]test$'`:
  **3/3, 0.11s**. After adding the two cycle source controls, only
  `own-element-overwrite.test` was rerun: **1/1, 0.10s**, no rebuild work.
  Final new oracle: **37 sites / 6 sound / 6 of 15 confined precision**.
  Existing reload: **33 / 15 / 15 of 24**; length: **38 / 16 / 16 of 27**.
  All report zero violations, partial, pending and unclaimed sites.
- The unchanged `class_filter_full_h` was imported and probed alone with both
  DOM providers: both still refuse **fn$7 / ctjs.create_object**, without native
  publication. Its first 100,000-step probe hit the work limit; the existing
  1,000,000-step budget reaches the semantic refusal. No class suite was replayed.
- Four tested file hashes match the devbox. Required `tools/format.sh --check`
  retains **26 baseline diagnostics in nine HEAD-identical files**; changed
  C++ formatting, source JS syntax/execution, gate shell syntax and whitespace
  pass. The first arrays run failed five assertions from one stale refusal row
  already updated locally after that sync; its original body was preserved.
  Exact scripts and logs: `/tmp/ctcompile-h-1105/`.

**Next native boundary:** stop treating the one-call full-H specimen as a
positive waiting for more inert operations. It calls only `getDataAttribute`;
the other original slots lack parameter authority under the complete-body rule.
The authentic combined path is Bootstrap **Dropdown `Qi`**: its constructor goes
through `B`/`W._mergeConfigObj` to both getters; `_getPopperConfig` supplies
`this._menu`, `"popper"`, `"static"` to the setter; `_completeHide` supplies
`this._menu`, `"popper"` to the remover. Start from those original callers and
their real receiver/configuration flow, retaining all source bodies. This still
requires class inheritance/configuration, DOM selection, events and Popper
boundaries. Config alone and the scrollbar helper do not reach all four slots.
See `bootstrap-native-next.md` for source coordinates. Never insert calls or
guess unused parameter facts to make the smaller fixture pass.

Next escape work remains real own-data definition provenance for ordinary
objects and broader loop mutation/control flow. Full CTest/compiler lit, class
DOM/transaction/host suites, lifetime/corpus/native matrices, WPT/test262 and
whole Bootstrap were skipped. No native Bootstrap admission gain, browser/Script
change or compliance measurement is claimed. The application driver and overall
plan remain unfinished. No push.

## Confined unused local cells, 2026-09-19 UTC

**6c877519** resumes clean **632bfc98** and the original unused-H thread claimed
at **10:42:50**, explicitly abandoned at **10:44:32** in AGENT-SYNC. Both agents'
histories and unmerged branches were reviewed; September 7 WIP is already an
ancestor. Parallel agents supplied native proof review, source fixtures and the
Part 25 definition-semantics investigation; root completed integration and gates.

Unused bodies now prove confined local cells without assigning types to their
contents. Every cell identity use must be a same-function root, direct read or
direct write; returned, stored, captured and unknown cell identities refuse.
Uncalled nested declarations require unique uncaptured closures, root-only uses,
no symbol references and recursive proof of every original body. Every arm,
consumer, completion, frame and implicit argument remains checked, with bounded
work/depth and recursion refusal. Calls, coercions, properties and effects still
refuse. Existing source normalization and ordinary native ownership remain.

All **425 prior driver sources** and **18 raw C++ JavaScript bodies** are unchanged.
Added **three positive and 15 refusal driver bodies**, plus **12 transaction
controls per provider**, including four refreshed-fingerprint cell forgeries.
Source fixtures assert that actual cell operations survive import. Standalone
unused declarations retain class initialization's separate refusal; equivalent
local-holder fixtures exercise the shared proof used by H.

Focused validation passed:

- Built `ctjs-opt ctjs-translate ctcompile-test-native-reference
  ctcompile-test-exception-recovery ctcompile-test-host-contract` with
  `tools/remote-build.sh` under `/tmp/ctbrowser-devbox-build.lock`.
- On the devbox, `ctest --test-dir projects/compile-time-browser/build
  --output-on-failure --no-tests=error -R '^ctcompile_exception_recovery$'`:
  **1/1, 4.61s (4.62s total)**. Separate exact selection
  `'^ctcompile_host_contract$'`: **1/1, 0.47s (0.48s total)**.
- `~/.lit-venv/bin/lit -sva projects/compile-time-browser/build/ctcompile/test
  --filter='^ctcompile :: CTNative/Lowering/Objects/class-dom[.]mlir$'`:
  **1/1, 283.35s**, **620 Node/interpreter observations**, **eight GCC/Clang
  executions**, **4,844 refusals**. Four tested file hashes match the devbox;
  emitted C++ and Script/dispatch/link exclusions pass. Pinned VM UTF-16
  differences remain unchanged.
- First build failed only on a new test's `BlockArgument`/`TypedValue` ternary;
  an explicit `mlir::Value` fixed it. First class run **44.08s** exposed the new
  standalone fixture boundary above. All source bodies were retained; holder
  variants were added. Only the class test was retried after fixture changes,
  with the three native tool targets reporting no rebuild work.
- `tools/format.sh --check` reported **26 baseline diagnostics in nine
  HEAD-identical files**. Changed C++ passes pinned clang-format; the Python
  driver passes `black --check`; all 18 added sources pass Node syntax checks;
  gate scripts pass `bash -n`; `git diff --check` passes. Docs need no build.

**Next:** unchanged `class_filter_full_h` now refuses at `native DOM class:
DOM class method body: native DOM source: unused DOM helper body contains an
unproved operation: fn$7 / ctjs.create_object`. Local cells are proved; the
original fresh result, iterator and callback still need their complete proof.
The semantic boundary is missing parameter authority: unused `t` has no DOM
identity, and unused setters lack proved DOM receivers/String keys. Merely
allowing allocation or more inert operators cannot supply those facts. Use
justified facts from the original application call graph; never insert calls,
guess values or delete unproved siblings. Standalone helper registration remains
separate. Part 25 still needs real own-data definition semantics/provenance,
preserving assignment setters, literal `__proto__` and computed-key order.
The existing class-field `emit_define_own`/`define_own_name` implementation was
recorded in Part 25 and AGENT-SYNC for coordinated runtime work; it is not a
trusted definition opcode. No escape code or precision measurement changed.
W, nested iterators, broader ownership and the application driver remain.

Skipped: full CTest/compiler lit, escape tests, standalone RegExp and broader
DOM/String suites, separate lifetime matrices, broad corpus/native matrices,
WPT/test262 and whole Bootstrap. No whole-bundle admission gain, browser/Script
semantics change or new compliance count is claimed. Evidence:
`/tmp/ctcompile-h-1047/`. Standard SSH authentication failed; the abandoned 10:42
session key supplied temporary access and was removed at closure. No push.

## Unused conditional and early-return bodies, 2026-09-19 UTC

Resumed clean **a76e1a53** and the original unused-H thread claimed at
**10:18:04**, explicitly abandoned at **10:19:04** in AGENT-SYNC. Both agents'
histories and unmerged branches were reviewed; September 7 WIP is not pending.
Three agents supplied native/fixture/escape findings before service limits;
root completed implementation and validation.

**104ab81c** independently proves uncaptured conditional leaves using total
operations on arbitrary values. Every arm is checked, including discarded values
and constant-dead effects; diagnostics now identify the function and operation.
**16dc5860** reuses the existing complete-source completion normalizer before
that proof, admitting safe early returns without observing inactive poison.
Original dominance, frames, implicit arguments, reference census and work limits
remain. Calls, coercions, properties, captures and loops still refuse.

Final focused passes: transaction **1/1 (4.54s; total 4.55s)**; host contract
**1/1 (0.47s; total 0.48s)**; class DOM **1/1 (278.83s)** with **608 Node/interpreter
observations, eight GCC/Clang native executions and 4,742 refusals**. The first
commit separately passed the same selection: transaction **4.55s total**, host
**0.48s**, class **265.28s / 596 observations / eight executions / 4,596 refusals**.
No focused build or test failed. All three tested source/test hashes matched the
devbox at each commit; emitted C++ and Script/dispatch/link exclusions pass.
All **408 original driver bodies** and **18 raw C++ JavaScript bodies** are
preserved; 17 driver bodies and 17 transaction controls per provider were added.
Existing pinned VM UTF-16 differences remain. Required formatting retains
**26 baseline diagnostics in nine unchanged files**; changed formatting, added
JS syntax and whitespace checks pass.

**Next:** unchanged `class_filter_full_h` now refuses at `native DOM class:
DOM class method body: native DOM source: unused DOM helper body contains an
unproved operation: fn$7 / ctjs.create_cell`. This is original
`H.getDataAttributes`' parameter cell; its local state, fresh result, iterator and
callback body still need complete independent proof. Do not merely whitelist
cells or allocate guessed parameter values. Its unknown `t` has no DOM authority;
uncalled setters likewise lack proved DOM receivers and String keys. Dynamic F
is already implemented for proved Strings. Do not insert calls, remove unproved
siblings or keep adding unrelated inert operators as a substitute for that proof.
Part 25 ordinary-object reloads still require actual literal own-data definition
semantics/provenance, preserving assignment setters. The parallel escape review
reconfirmed that prerequisite; no escape code or measurement changed.
W, nested iterators, complex completion, broader ownership and the application
driver remain. No whole-Bootstrap admission gain or new compliance count is claimed.

Exact validation, with every devbox command under
`/tmp/ctbrowser-devbox-build.lock`:

- `tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-reference
  ctcompile-test-exception-recovery ctcompile-test-host-contract`, once per code
  change; then the same focused selection for that changed implementation.
- `ctest --test-dir projects/compile-time-browser/build --output-on-failure
  --no-tests=error -R '^ctcompile_exception_recovery$'` and the separate exact
  selection `-R '^ctcompile_host_contract$'`.
- `~/.lit-venv/bin/lit -sva projects/compile-time-browser/build/ctcompile/test
  --filter='^ctcompile :: CTNative/Lowering/Objects/class-dom[.]mlir$'`.
- `tools/format.sh --check` reported the baseline above; pinned clang-format
  checked the two changed C++ files, `black --check` checked `class_dom.py`,
  Node syntax-checked the new source bodies, and `git diff --check` passed.
  Temporary gate scripts passed `bash -n`; docs require no build or CTest.

Skipped: full CTest/compiler lit, escape tests, standalone RegExp and broader
DOM/String suites, separate lifetime matrices, broad corpus/native matrices,
WPT/test262 and whole Bootstrap. These are focused results. No browser or Script
semantics changed. Evidence: `/tmp/ctcompile-h-1021/`, including both gate logs,
source preservation, hashes, emitted C++ and exact full-H diagnostics. Normal
SSH authentication failed before the build; temporary session access restored it.
The session authorized key and local keypair were removed after validation.


## Dynamic Bootstrap F and saved digit keys, 2026-09-19 UTC

Resumed **1b93d242** and three dirty escape files from the **09:33 session**,
explicitly abandoned at **09:36:29** in AGENT-SYNC. The native continuation was
the full H/F boundary promised in this handoff and that session's journal.
Both agents' histories and unmerged branches were reviewed; September 7 WIP is
not pending. Three agents supplied fixtures and native/escape findings before
service limits; root finished the implementation and focused gates.

**995585be** finishes the interrupted saved digit String own-array key proof.
The shared contents analysis now recognizes a saved canonical digit String in
ordinary reads/writes and invariant reloads. It retains the original String
identity and separate numeric conversion facts. String *indexing* still needs
a Number key, the 256-byte ASCII ceiling, original provenance, bounded indices,
read-only loop census and complete work budgets. CFG/SCF regressions and eight
source functions cover saved/overwritten children, invalid keys and mutation.

**84b3357a** proves original Bootstrap F on dynamically produced, proved Strings.
The original `/[A-Z]/g` callback remains in the IR and emitted C++; a small
ordinary String scan invokes it once per ASCII uppercase match and preserves
all other bytes. Its lowercase call uses public Core helpers. Complete callback,
String/RegExp intrinsic, receiver, enclosure and invocation proofs remain.
Unknown/nullable/object inputs, changed callbacks, effects and mutated intrinsics
still refuse. Iterator prefix analysis removes unreferenced callback bodies only
from its temporary prefix; the original candidate retains every body for final
proof. No browser/Script implementation or semantics changed.

All **395 prior class bodies**, **17 raw C++ JS bodies** and **62 standalone
RegExp case/refusal/matching bodies** are unchanged. Added four class positives,
nine refusals, eight transaction controls and three refreshed-fingerprint IR
forgeries per provider. The new DOM-write-inside-loop specimen remains a refusal;
the added saved-result/write-after-loop specimen passes.

Focused passes: arrays **1/1 (2.08s; total 2.09s)**; snapshot/index escape lit
**2/2 (0.35s)**; transaction **1/1 (4.51s)**; host contract **1/1 (0.47s; total
0.48s)**; class DOM **1/1 (251.87s)**, **584 Node/interpreter observations, eight
GCC/Clang executions, 4,446 refusals**; RegExp **1/1 (12.93s)**, **24 observations,
four executions, 169 source/provenance/budget checks**. New snapshot oracle:
**24 sites / 8 sound / 8 of 14 confined precision**; prior index: **31 / 10 /
10 of 20**. Zero violations, partial, pending or unclaimed sites. All **15**
tested source/test hashes match the devbox. Emitted C++ inspection and
Script/dispatch/link exclusions pass; existing pinned VM UTF-16 differences stay.

**Next:** exact original `class_filter_full_h` still refuses at
`native DOM class: DOM class method body: native DOM source: unused DOM helper
body contains an unproved operation`. Dynamic String F is now proved; independent
proof of the original unused parameterized H bodies remains before publication.
Do not insert calls, assume missing parameter facts or erase unproved siblings.
DOM writes inside the loop retain the backedge alias boundary. Part 25 ordinary
object reloads still need real literal own-data definitions preserving assignment
setters. General String indices, W operations, nested iterators, complex completion,
broader ownership work and the application driver remain. No whole-Bootstrap
admission gain or new WPT/test262 measurement is claimed.

Exact validation, under `/tmp/ctbrowser-devbox-build.lock`:

- Built escape targets with `tools/remote-build.sh
  ctcompile-test-escape-analysis-arrays ctcompile-test-escape-claims
  ctcompile-test-type-oracle`; native targets with `tools/remote-build.sh ctjs-opt
  ctjs-translate ctcompile-test-native-reference ctcompile-test-exception-recovery
  ctcompile-test-host-contract`. Fixture retries selected only the three native
  tools; final RegExp retry required no C++ rebuild.
- `ctest --test-dir projects/compile-time-browser/build --output-on-failure
  --no-tests=error -R '^ctcompile_escape_analysis_arrays$'`; separate exact
  selections `'^ctcompile_exception_recovery$'` and `'^ctcompile_host_contract$'`.
- `~/.lit-venv/bin/lit -sva projects/compile-time-browser/build/ctcompile/test
  --filter='^ctcompile :: Analysis/Escape/escape-claims/(invariant-string-snapshot|invariant-string-index)[.]test$'`.
  The same generated-config command separately selected
  `'^ctcompile :: CTNative/Lowering/Objects/class-dom[.]mlir$'` and
  `'^ctcompile :: CTNative/Browser/native-dom-regexp[.]test$'`.
- Initial native build fixed a const `FuncOp` accessor. First transaction
  **4.51s** exposed two new prefix-orphan callback failures; prefix cleanup fixed
  them. Class retries **39.56s / 40.45s** exposed the missing callback enclosure
  forwarding and the new DOM-write-inside-loop fixture; forwarding was fixed,
  that source retained as a refusal, and a separate after-loop positive added.
  RegExp **11.84s** exposed three diagnostic-stage expectations: factory/pattern/
  flags mutations still refuse, now at DOM entry. Only RegExp was rerun after
  correcting those expectations. Final passing measurements are above.
- `tools/format.sh --check` retains **26 baseline diagnostics in nine
  HEAD-identical files**. All changed C++ passes pinned clang-format; both changed
  Python files pass `black --check`; added JS bodies, temporary gate scripts and
  `git diff --check` pass syntax/whitespace checks. Docs need no build or CTest.

Skipped: full CTest/compiler lit, complete DOM/String/Number/URI suites, separate
lifetime matrices, broad corpus/native matrices, WPT/test262 and whole Bootstrap.
Evidence: `/tmp/ctcompile-h-0944/`, including exact full-H diagnostic, source
preservation, hashes and emitted C++. No push; native Bootstrap and the plan
remain unfinished.
