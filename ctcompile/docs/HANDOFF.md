# Handoff: continuing ctcompile

> NOTE (Claude, 2026-09-16): `ctcompile-v1` history was **reworded** while Codex
> was stopped - every unpushed commit from `f7966251` (origin) forward now has a
> `ctcompile(<area>): ...` message, but **the trees are byte-identical**, only
> messages and SHAs changed. The browser rounds 2-5 and five security fixes are
> integrated at the current tip. Pre-reword tips are kept as
> `ctcompile-v1-backup-premsg2` / `-premsg`. Full detail is in the
> `SESSION HANDOFF` journal at the end of `../../AGENT-SYNC.md`. Just branch from
> the current `ctcompile-v1` tip - nothing about the native work changed.

Native work continues on `ctcompile-v1` in the `compile-time-browser` monorepo.
The application driver remains incomplete; native compiler development uses
`ctjs-translate` and `ctjs-opt`. Build on the devbox using `tools/remote-build.sh`
under `/tmp/ctbrowser-devbox-build.lock`, then run the local formatter before
committing. There is no CI. Do not build on the small local machine.

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

## Known matching F inputs and ASCII String indices, 2026-09-19 UTC

Resumed **28aa6180** and the three dirty files from the **08:55 session**,
explicitly abandoned at **08:58:27** in AGENT-SYNC. Both agents' histories and
unmerged branches were reviewed; September 7 WIP is already an ancestor.
Three agents supplied native/escape findings before service limits; root
completed the interrupted code, fixtures and focused gates.

**da3602e3** proves original ASCII String indexed snapshots and invariant
reloads through the shared contents proof. Number keys, original SSA identity,
read-only loop census, 256-byte ASCII ceiling, conversion, work/depth and final
index bounds remain checked. Indexed digits can supply Number strides; the
character stays a String for property-key and addition semantics. Twenty-six
CFG/SCF rows and ten source functions cover retained/discarded children, saved
characters, nested reads, Unicode/key refusals, mutation and incomplete budgets.

**5b01f648** proves known matching inputs to Bootstrap's original F. The complete
call-input census remains; `/[A-Z]/g` additionally requires the complete original
`'-' + t.toLowerCase()` callback body, including discarded operations and implicit
arguments. Shared public Core `ascii_lower` computes the exact constant results;
String comparisons select among the proved inputs. Non-ASCII bytes remain
unchanged. Unknown inputs, changed callbacks, captures, effects and intrinsic
mutation still refuse. All **388 prior class-driver bodies**, **17 raw C++ JS
bodies** and **49 standalone RegExp refusal bodies** are preserved. Seven class
bodies and six transaction controls per provider were added; 11 standalone
matching bodies moved to value/effect differential checks. A RegExp-only entry
point avoids replaying the complete String/Number/URI suite.

Focused passes: arrays **1/1 (1.24s; total 1.25s)**; String-index/String-length
oracles **2/2 (0.38s)**; transaction **1/1 (4.47s; total 4.48s)**; host contract
**1/1 (0.47s)**; class DOM **1/1 (245.16s)**, **568 Node/interpreter observations,
eight GCC/Clang executions, 4,290 refusals**; standalone RegExp **1/1 (12.95s)**,
**24 original observations, four GCC/Clang executions, 169 source/provenance/budget
checks**. New index oracle: **31 sites / 10 sound / 10 of 20 confined precision**;
prior length oracle: **28 / 10 / 10 of 18**. Zero soundness violations, partial,
pending or unclaimed sites. All ten tested source/test hashes match the devbox.
Emitted C++ inspection and Script/dispatch exclusions pass: ordinary String and
document owners, public Core/DOM APIs. Existing pinned VM UTF-16 differences
remain. Required formatting retains **26 baseline diagnostics in nine unchanged
files**; all changed formatting, JS syntax and `git diff --check` pass.

**Next:** an exact original `class_filter_full_h` probe still refuses at
`native DOM class: DOM class method body: native DOM source: DOM direct helper
contains an unproved closure or capture`. Its unused parameterized H slots call
F with unknown keys. Known matching replacement is now proved; general dynamic
String replacement and independent parameterized H-body proof still precede
publication. Do not insert calls or erase unproved siblings. Part 25 object
reloads still need real literal own-data definition provenance, preserving
assignment setters; general String indices need agreement with Script's byte
and Number-key semantics. W operations, nested iterators, complex completion,
broader ownership work and the application driver remain. No whole-Bootstrap
admission gain is claimed. No browser/Script semantics or historical compliance
measurements changed.

Exact validation, with every devbox command under `/tmp/ctbrowser-devbox-build.lock`:

- `tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-reference
  ctcompile-test-exception-recovery ctcompile-test-host-contract
  ctcompile-test-escape-analysis-arrays ctcompile-test-escape-claims
  ctcompile-test-type-oracle`; subsequent builds selected only affected native
  targets. The final RegExp retry had no C++ build work.
- `ctest --test-dir projects/compile-time-browser/build --output-on-failure
  --no-tests=error -R '^ctcompile_escape_analysis_arrays$'`; separate exact
  selections `'^ctcompile_exception_recovery$'` and `'^ctcompile_host_contract$'`.
- `~/.lit-venv/bin/lit -sva projects/compile-time-browser/build/ctcompile/test
  --filter='^ctcompile :: Analysis/Escape/escape-claims/(invariant-string-index|invariant-string-length)[.]test$'`.
- The same generated-config lit command separately selecting
  `'^ctcompile :: CTNative/Lowering/Objects/class-dom[.]mlir$'` and
  `'^ctcompile :: CTNative/Browser/native-dom-regexp[.]test$'`.
- First native transaction failed eight assertions in **4.52s** because unchanged
  controls 114/122 now admit matching H inputs. Their bodies were preserved and
  expectations corrected; no production correction followed. First RegExp run
  failed native harness compilation in **2.48s** because its element handle used
  the wrong namespace. Fixed to public `ctbrowser::element_ref`; only that lit
  case was retried. All other focused gates passed on their first run.
- `tools/format.sh --check` reports the unchanged baseline above; pinned
  clang-format checks every changed C++ file, `black --check` covers changed
  Python, and new class JS bodies pass Node syntax checks. Temporary gate scripts
  pass `bash -n`. Documentation needs no build or CTest.

Skipped: full CTest/compiler lit, complete DOM/String/Number/URI suites, separate
lifetime matrices, broad corpus/native matrices, WPT/test262 and whole Bootstrap.
These are focused results. Scripts, logs, hashes, emitted C++ inspection and the
exact full-H diagnostic are in `/tmp/ctcompile-h-0903/`. No push. Native Bootstrap
and the overall plan remain unfinished.

## Inert unused bodies and ASCII String lengths, 2026-09-19 UTC

Resumed **01c63ec8**, the dirty `EscapeAnalysis.cpp` draft from the **08:05
session**, and the unused-H thread abandoned at **08:06:01** in AGENT-SYNC.
September 7 WIP is already an ancestor; both agents' recent histories were
reviewed. Three agents investigated escape, fixtures and native proof, then hit
service limits. Root completed the code, review and focused gates.

**6fdac943** proves bounded original ASCII String-length snapshots and invariant
reloads. Original base/key identities, read-only loop census, work/64-layer
budgets and index bounds remain. Seventeen CFG/SCF rows and nine source functions
cover retained/discarded children, saved lengths, dense String reloads, growth,
Unicode, computed Strings and mutation. The scan ceiling is 256 ASCII bytes;
Script's byte length still prevents extending this proof to general Unicode.

**3c16788b** independently proves uncaptured, straight-line unused leaves with
literals, identity returns, typeof/Not/Void and strict equality. No parameter
facts or synthetic calls are supplied. Original frame/implicit-argument checks
remain; slots already retired by class lifting also require absent numeric
closures and symbol references. Calls, coercions, properties and captures refuse.
All **373 prior driver source bodies** and **16 raw C++ JavaScript bodies** are
preserved. Fifteen driver bodies and seven transaction controls per provider
were added. Three original literal-slot refusals and the original orphan-body
unit specimen now admit without source edits; effect/reference controls remain.

Measured focused passes: arrays **1/1 (2.10s; total 2.11s)**, transaction
**1/1 (4.48s; total 4.49s)**, host contract **1/1 (0.48s)**, class DOM
**1/1 (246.04s)**: **532 observations, eight GCC/Clang native executions,
4,116 refusals**. Existing pinned VM UTF-16 differences remain. The prior
length oracle is **38 sites / 16 sound / 16 of 27 confined precision**; the new
String oracle is **28 / 10 / 10 of 18**, with zero violations, partial, pending
or unclaimed sites. All ten tested hashes match the devbox. Inspected emitted
C++ uses ordinary owners and public Core/DOM helpers; Script/dispatch exclusions
pass. Required formatting retains **26 baseline diagnostics in nine unchanged
files**; changed formatting, added JS syntax and `git diff --check` pass.

**Next:** original `class_filter_full_h` still has uncalled parameterized H
slots. Their internal `F(e)` calls leave `e` unknown and correctly poison F's
complete no-match replacement proof. General matching replacement and independent
parameterized H-body proof remain before publication; never insert calls or
erase unproved siblings to admit it. Part 25 object reloads still need real
literal own-data definition provenance, preserving assignment setters. W's
operations, nested iterators, complex entry completion, broader ownership work
and the application driver remain. No whole-Bootstrap admission gain is claimed.
No browser/Script behavior or historical compliance measurements changed.

Exact validation (every devbox command held `/tmp/ctbrowser-devbox-build.lock`):

- Built with `tools/remote-build.sh ctjs-opt ctjs-translate
  ctcompile-test-escape-analysis-arrays ctcompile-test-escape-claims
  ctcompile-test-type-oracle ctcompile-test-native-reference
  ctcompile-test-exception-recovery ctcompile-test-host-contract`; later builds
  selected only the affected native targets. Final host fixture retry rebuilt
  only the host-contract object and executable.
- `ctest --test-dir projects/compile-time-browser/build --output-on-failure
  --no-tests=error -R '^ctcompile_escape_analysis_arrays$'`, then separate exact
  selections `'^ctcompile_exception_recovery$'` and `'^ctcompile_host_contract$'`.
- `~/.lit-venv/bin/lit -sva projects/compile-time-browser/build/ctcompile/test
  --filter='^ctcompile :: Analysis/Escape/escape-claims/(invariant-string-length|invariant-length-reload)[.]test$'`:
  prior length passed; new String case initially failed an expectation. Retried
  only `invariant-string-length[.]test`, **1/1 (0.10s)**. Its fixed-literal latch
  source remains a positive; an added growing String supplies the real refusal.
- Same lit command selecting only
  `'^ctcompile :: CTNative/Lowering/Objects/class-dom[.]mlir$'`: **1/1** above.
- Initial build failed two test range-loop-copy warnings; corrected to references.
  First transaction run failed six assertions on orphan unused leaves, exposing
  the class-retired path fixed in production. Host checks first caught the old
  orphan-literal refusal; a subsequent new symbol-reference control needed its
  attribute placed inside the module's symbol scope. Sources were preserved.
  No additional production correction followed the orphan-body fix.
- A later diagnostic-only full-H probe could not connect over SSH (timeout).
  Its refusal is still measured by the class DOM case; the F prerequisite is
  a source-audit finding. This did not invalidate the completed focused gates.
- `tools/format.sh --check` reports the baseline above. Changed C++ uses pinned
  clang-format 23.0.0git; Python uses `black --check`; new JS bodies pass Node's
  syntax check. Documentation needs no build or CTest.

Skipped: full CTest/compiler lit, complete DOM/String/assignment suites, separate
lifetime matrices, broad corpus/native matrices, WPT/test262 and whole Bootstrap.
These are focused results. Exact scripts, logs, source hashes and inspected C++
are in `/tmp/ctcompile-h-0823/`. No push. Native Bootstrap and the plan remain open.

## Full H instance methods and object-reload prerequisite, 2026-09-19 UTC

**6bca7b94** finishes the interrupted receiver-proof thread at **61f627fb**:
three dirty files from the **07:05 session**, explicitly abandoned by the
**07:07:11 AGENT-SYNC exit**. Complete original H/M/F now runs inside instance
methods when every H slot has actual calls, including constructor-stored DOM
receivers and fixed fresh-result aliases. Method stability ignores only a direct
fresh allocation distinct from the prototype/instance; all unknown aliases,
formals and real receiver writes still count. Constructor proof is unchanged.

All **361 prior source bodies** and **16 raw C++ JavaScript bodies** are preserved.
Twelve added bodies and six new transaction controls per provider cover the
boundary, including a local that changes from fresh to the real instance. The
old transaction control 148 now admits without changing its source. No browser
or Script semantics changed.

Focused passes: transaction **1/1 (4.35s; total 4.36s)**, host contract
**1/1 (0.51s; total 0.52s)**, and selected class DOM / class entry / constructor
refusals lit **3/3 (186.36s total)**. Class DOM: **492 observations, eight
GCC/Clang executions, 3,746 refusals**. All four changed source/test hashes match
the devbox; emitted C++ uses public Core/DOM helpers and ordinary owners, and
Script/dispatch exclusions pass. Required formatting retains **26 baseline
diagnostics in nine unchanged files**; changed checks pass. Exact commands,
first failed expectation and evidence: repository HANDOFF and
`/tmp/ctcompile-prototype-final/`.

**Next:** the unchanged `class_filter_full_h` calls only `H.getDataAttribute`;
its uncalled original slots still refuse at `DOM direct helper contains an
unproved closure or capture`. Prove their bodies independently before publication;
do not erase unproved siblings or insert calls into the specimen. Part 25's
object reloads need real literal own-data definition semantics/provenance first:
ordinary-object assignment currently cannot seed a safe contents snapshot.
This prerequisite is rechecked at the top of part 25 and journaled for Claude.
W operations, nested iterators, complex entry completion, broader part 25 and
the application driver remain. Full suites, broad matrices, WPT/test262 and
whole Bootstrap were skipped; no whole-bundle or escape-precision gain is claimed.

The native draft needed no production correction. One agent finished fixture
review; receiver and escape auditors reported their findings before service
limits, and root verified them and completed the gates. Both-agent histories
and unmerged branches were reviewed; September 7 WIP is already an ancestor.
The independent escape audit produced no code change: `CreateObjectOp` seeds
empty snapshots, `CopyPropsOp` cannot introduce unknown entries, and ordinary
`SetPropertyOp` refuses because inherited setters can retain values. Adding an
invariant object-read case alone would be unreachable admission code. Coordinate
literal definition semantics with Claude without changing assignment setters.

Exact focused validation, all devbox operations under
`/tmp/ctbrowser-devbox-build.lock`:

- `tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-reference
  ctcompile-test-exception-recovery ctcompile-test-host-contract`: passed;
  two incremental actions on each of two runs. The inherited Methods.cpp build
  was already current; verified source hashes match.
- `ctest --test-dir projects/compile-time-browser/build --output-on-failure
  --no-tests=error -R '^ctcompile_exception_recovery$'`: first failed four
  assertions in **5.28s total**, solely because unchanged control 148 now admits.
  Its expectation moved to the existing positive proof/contract checks; the
  new controls already passed. Retry **1/1 (4.35s; total 4.36s)**.
- The same exact CTest command with `-R '^ctcompile_host_contract$'`:
  **1/1 (0.51s; total 0.52s)**.
- `~/.lit-venv/bin/lit -sva projects/compile-time-browser/build/ctcompile/test
  --filter='^ctcompile :: CTNative/Lowering/Objects/(class-dom|class-entry|constructor-refusals)[.]mlir$'`:
  **3/3 (186.36s)**. Besides the class DOM counts above, class entry has
  **two preparations / four observations / 26 refusals**; constructor controls
  have **11 observations / eight native executions / 18 refusals**. Existing
  pinned VM differences remain unchanged. The complete-H method source emits
  ordinary `element_ref`, owning strings/vectors/JSON and existing platform calls.
- The original unused-slot fixture was probed separately with the same generated
  tools and its real intrinsic manifest; its exact diagnostic is recorded above.
  `verify.sh` compares all four changed source/test hashes and exports inspected
  C++ to `full-h-method.cpp` beside the gate logs.
- Required `tools/format.sh --check` reports the same **26 diagnostics / nine
  HEAD-identical files** with pinned clang-format 23.0.0git. Changed C++/Python
  formatting, Python/added-JS syntax, source preservation and `git diff --check`
  pass. Documentation needs no build or CTest.

Skipped: full CTest/compiler lit, escape builds/oracles, complete DOM/String/
assignment suites, separate lifetime matrices, broad corpus/native matrices,
WPT/test262 and whole Bootstrap. These are focused results. No escape code or
precision changed. Browser compliance measurements remain historical. Devbox
start and IP allow-list refresh restored access; the predecessor temporary SSH
key was removed from the devbox and local disk, preserving other keys. No push.
Native Bootstrap and the overall plan remain unfinished.

## Full H entry calls and invariant array lengths, 2026-09-19 UTC

Resumed **59d28a62** and five dirty ctcompile paths from the **06:19 H-slot /
own-length threads**, explicitly abandoned by the **06:21:33 AGENT-SYNC loop
exit**. Both-agent histories and unmerged branches were reviewed; September 7
WIP is already an ancestor. The native draft was diagnostic instrumentation;
the four-file escape draft was complete but ungated. Three agents reviewed
escape, added H fixtures and audited the native proof. The latter two reached
service limits; root completed their work and all gates.

**45365d98** proves invariant dense-array own-length reloads through the existing
base/key identity proof and complete read-only loop census. Primitive snapshots,
work/64-layer limits and final-index bounds remain checked. Eighteen CFG/SCF rows
and a ten-function source oracle cover returned/discarded children, nested reads,
mutation, changing bases/keys, conversion and budget cutoffs. String and object
lengths remain refused. The inherited draft needed no production correction.

**186afb38** admits the complete original H/M/F bodies when all four H slots have
original entry calls, beside a constructor-only class. Each sibling's complete
String-argument census now runs before expansion reaches their shared F callee.
This preserves the existing all-use proof, including distinct config/toggle
inputs; one matching or unknown later key still refuses. No callable-slot,
receiver, effect or unused-body check was weakened. All **346 prior source bodies
are unchanged**; 15 new bodies supply two positives and 13 refusals. Six new
transaction controls per provider check success, later inputs/effects, missing
identity and rollback. No browser or Script semantics changed.

Measured focused gates, all devbox work under `/tmp/ctbrowser-devbox-build.lock`:

- Native build: `tools/remote-build.sh ctjs-opt ctjs-translate
  ctcompile-test-native-reference ctcompile-test-exception-recovery
  ctcompile-test-host-contract` (**10 actions**, including removal of temporary
  diagnostics). Exact CTests `ctcompile_exception_recovery` **1/1 (4.38s total)**
  and `ctcompile_host_contract` **1/1 (0.46s total)**, using
  `ctest --test-dir projects/compile-time-browser/build --output-on-failure
  --no-tests=error -R '^<exact-name>$'` on the devbox.
- Generated-config lit: `~/.lit-venv/bin/lit -sva
  projects/compile-time-browser/build/ctcompile/test
  --filter='^ctcompile :: CTNative/Lowering/Objects/class-dom[.]mlir$'`:
  **1/1 (174.59s)**; **480 Node/interpreter observations, eight GCC/Clang native
  executions, 3,584 refusals**, both providers, optimization policies and layouts.
  Forty existing VM byte-index differences remain explicitly pinned. Inspected
  full-H output uses `element_ref`, owning strings/vectors/JSON, public Core
  casing and existing DOM helpers; source/binary Script and dispatch gates pass.
- Escape build: `tools/remote-build.sh ctjs-translate
  ctcompile-test-escape-analysis-arrays ctcompile-test-escape-claims
  ctcompile-test-type-oracle` (**3 actions**, analysis built by the earlier probe).
  Exact `ctcompile_escape_analysis_arrays`: **1/1 (1.23s total)**. The same
  generated-config lit command selected only
  `^ctcompile :: Analysis/Escape/escape-claims/(invariant-length-reload|invariant-reload)[.]test$`:
  **2/2 (0.10s)**. Sites / sound / confined precision: new length **38 / 14 /
  14 of 27**; prior reload **33 / 15 / 15 of 24**. Zero soundness violations,
  partial, pending or unclaimed sites. All focused gates passed on first run.
- Required `tools/format.sh --check` returns the existing **26 diagnostics in
  nine HEAD-identical files** with pinned clang-format 23.0.0git. Changed C++/
  Python formatting, Python/JS syntax, source preservation and `git diff --check`
  pass. All **eight tested source/test hashes** match the devbox. Evidence and
  runnable commands: `/tmp/ctcompile-h-resume/`. Documentation needs no build.

**Next:** H inside an instance method (`class_h_full_method`) still refuses.
The old `DOM helper object requires unique own callable slots` message is about
an **unlifted class prototype**, not H's slots. The earlier failure is
`ClosureLifting/Methods.cpp`'s whole-module write census: H's dynamic result
writes prevent prototype-method stability. Prove which receiver those writes
can reach before narrowing that census. Then check the constructor's method-value
read census and independent unused H bodies; the unchanged `class_filter_full_h`
still lacks actual calls for its unused parameterized slots. Do not discard them.
Publication, W's Object.entries/destructuring/original s/RegExp/TypeError/spread,
complex entry completion, nested iterators, invariant object-property reloads,
broader part 25 and the application driver remain. No whole-bundle admission
measurement is claimed; native Bootstrap and the overall plan are unfinished.

Skipped: full CTest/compiler lit, public-class replay, complete DOM/String/
assignment suites, separate lifetime matrices, broad corpus/native matrices,
WPT/test262 and whole Bootstrap. These are focused results. Browser compliance
counts remain historical. The predecessor temporary SSH authorized/private key
was removed at closure, preserving other keys. No push.

## Original H key normalization and invariant array reloads, 2026-09-19 UTC

Resumed clean **dadadb5c**, its HANDOFF promise to handle original H casing, and
the **05:48 AGENT-SYNC closure**. Both-agent histories and unmerged branches were
reviewed; September 7 WIP is already an ancestor. No predecessor edits remained.
Three agents worked on Core casing, dense reloads and H proof review. Two reached
service limits; root completed the escape draft, native proof and focused gates.

Landed in small commits:

- **794af039** generates pinned Unicode 17 full-lower mappings for one UTF-16
  unit. **eaae9318** exposes `ctbrowser::unicode_lowercase_unit` and checks all
  65,536 units against the UCD-derived checksum, including underscore preimage,
  U+0130 expansion and lone surrogates. The isolated branch landed atomically as
  **7361a3e5**. This is additive Core functionality; no binding/Script behavior
  changed. Core source/header/test and the shared generator are journaled.
- **b484511f** admits unchanged original `H.getDataAttributes` with M. Only a
  proved `charAt(0)` result may lowercase; empty results remain empty. Output-key
  proof recognizes lowercase-first-unit plus the unchanged tail of the same
  prefix-stripped key. The original dataset key remains separate. Only underscore
  lowercases to underscore, preserving one `__proto__` preimage; ordinary
  collisions use the existing ordered snapshot assignment helper. Sole-writer,
  traversal, intrinsic identity, snapshot and effect checks remain.
- **17dc99aa** admits invariant dense own-element reloads through the shared
  CFG/SCF proof. Base/key identities, the read-only census, exact primitive
  conversions, 64-layer/work limits and final-index bounds remain checked.
  Two old C++ expression rows and three old source oracles retain their bodies.

All **346 prior class/field bodies and 49 assignment bodies** are unchanged.
Two former class refusals now admit. Nine assignment bodies, 14 normalized-result
fixtures and seven transaction controls per provider cover collisions, casing,
empty keys, prototype writes, ownership and refusal rollback. Forty existing
class byte-index divergences and five assignment casing divergences are pinned;
Node and native agree. The VM remains the unchanged differential oracle.

Measured focused validation, all devbox work under the shared build lock:

- Core build `ctbrowser-test-core_basics` (**9 actions**), exact CTest
  `core_basics`: **1/1 (0.57s; total 0.58s)**. Generator reproduction and all-unit
  mapping validation pass. Isolated-worktree formatter passes using Homebrew
  clang-format 23.1.1.
- Native probe targets `ctjs-opt ctjs-translate ctcompile-test-native-reference`
  (**276 actions**, Core header dependents rebuilt). Original H admits; complete
  H still refuses at the closed-callable slot census. A direct assignment probe
  first supplied an inapplicable class identity; the actual fixture manifest is
  correct. Native gate adds `ctcompile-test-exception-recovery
  ctcompile-test-host-contract` (**5 actions**). Exact CTests:
  `ctcompile_exception_recovery` **1/1 (4.42s)** and `ctcompile_host_contract`
  **1/1 (0.47s)**, with anchored `-R`, `--output-on-failure --no-tests=error`.
- Generated `build/ctcompile/test` lit selects only
  `CTNative/Lowering/Objects/class-dom.mlir` and
  `CTNative/Browser/native-dom-assignment.test`: **2/2 (223.05s total)**.
  Class: **472 source observations, 8 native executions, 3,448 refusals**.
  Assignment: **11 sources, 239 observations, 8 GCC/Clang binaries, lifetime
  ASan/UBSan with leak detection, 236 refusals**, both providers/policies/layouts.
  Public Core calls, ordinary owning strings/JSON and existing browser helpers
  appear in inspected output; Script/dynamic-dispatch exclusions pass.
- Escape build targets `ctjs-translate ctcompile-test-escape-analysis-arrays
  ctcompile-test-escape-claims ctcompile-test-type-oracle`: first **5 actions**,
  corrected arrays **2 actions**. Exact `ctcompile_escape_analysis_arrays`:
  **1/1 (1.24s; total 1.25s)**. Four generated-config lit cases
  `Analysis/Escape/escape-claims/{invariant-reload,nested-invariant-latch,
  invariant-add-sub-latch,invariant-bitwise-latch}.test`: **4/4 (0.12s)**.
  Sites / sound / precision: **33 / 15 / 15 of 24**, **35 / 13 / 13 of 23**,
  **41 / 17 / 17 of 26**, **44 / 19 / 19 of 28**; zero violations, partial,
  pending or unclaimed sites. Final oracle replay rebuilt no targets.
- First arrays failed **11 assertions (1.24s total)**: two old expression
  expectations plus one new SCF fixture's undefined SSA value. The next four
  oracles had zero soundness violations but failed verdict expectations for old
  reloads and a constant-key producer. Those bodies were preserved, expectations
  corrected, and a truly varying-key refusal added. No production change followed.
- Required shared `tools/format.sh --check` reports **26 baseline diagnostics in
  nine HEAD-identical files** with pinned clang-format 23.0.0git. Changed
  C++/Python formatting, source preservation, JS/Python syntax and `git diff
  --check` pass. All **19 changed source/test hashes** match the devbox. Evidence,
  exact runnable commands and inspected output: `/tmp/ctcompile-h-lowercase/`.

**Next:** the complete original H object still stops at
`DOM helper object requires unique own callable slots` in
`ctcompile/lib/CTNative/Analysis/ClosedCallable.cpp`. Continue its complete slot
census and independent unused-body proof, then publication. Whole-string/contextual
casing is outside the new single-unit API. W's Object.entries/destructuring/
original s/RegExp/TypeError/spread, complex entry completion arithmetic,
loop-nested iterators, invariant own-length/object-property reloads, broader
part 25 and the application driver remain. Native Bootstrap and the overall plan
are unfinished; no whole-bundle admission gain is claimed.

Skipped: full CTest/compiler lit, public-class replay, complete DOM/String
suites, broad corpus/native matrices, WPT/test262 and whole Bootstrap. These are
focused passes. Browser compliance measurements remain historical. The temporary
SSH authorized key and local private key were removed, preserving other keys.
No push.

## Repeated helpers across entry exits and budgeted invariant depth, 2026-09-19 UTC

Resumed **41527341** and the two dirty class fixture files from the **05:21/05:25
conditional-callee threads**, explicitly abandoned in AGENT-SYNC at **05:26:57**.
September 7 WIP exists and is already an ancestor. Three agents started fixture,
escape and proof-review work; all hit service limits. Root preserved the fixture
draft and completed implementation, review and focused gates.

**5c912e13** admits the unchanged repeated-helper source across an entry early
return. Both original call shapes use the existing callback/receiver proof;
arguments remain evaluated, missing formals are padded and excess arguments
refuse. Conditional arms independently check and join frame state; missing,
repeated and loop-nested exits refuse. All **310 prior source bodies and ten
inherited draft bodies are unchanged**; 13 added bodies and eight transaction
controls cover the boundary, with two additional malformed-frame checks per
provider. Ten prior refusals are now positives, including nine UTF-16 early-return
bodies. The **40 VM byte-index divergence observations** are explicitly pinned;
Node and native agree. No browser or Script semantics changed.

**c6fa3d11** lets the existing invariant arithmetic proof use its charged work
budget beyond two operation layers, retaining a **64-layer stack ceiling**.
Original primitive identity, backedges, conversions, overflow, reload and index
checks remain. CFG/SCF tests cover deeper arithmetic/bitwise expressions,
returned/unreturned children, mutation, the depth boundary and every incomplete
budget prefix. All source bodies in the three existing escape oracles remain
unchanged; four older C++ expression rows now have proved outcomes.

Focused passes: transaction **1/1 (4.39s; total 4.40s)**, host contract **1/1
(0.45s; total 0.46s)**, class DOM **1/1 (167.60s; 464 source observations,
8 native executions, 3,398 refusals)**, arrays **1/1 (1.25s; total 1.26s)** and
three escape oracles **3/3 (0.13s)**. Their measured sites / sound / confined
precision are **nested: 35 / 10 / 10 of 23**, **Add/Sub: 41 / 14 / 14 of 26**,
**bitwise: 44 / 16 / 16 of 28**; zero violations, partial, pending or unclaimed
sites. All **11 tested source/test hashes** match the devbox. Inspected output
uses `element_ref`, owning strings/vectors and public browser helpers;
source/binary Script and dynamic-dispatch exclusions pass.

**Next:** original H still needs Unicode-correct lowercase for the proved
`charAt(0)` result and normalized output-key collision/prototype proof. Use the
public Core UTF-16 converters, full-lower mappings (including expanding U+0130
and unchanged surrogates), and keep the original dataset key separate from its
output key. Script's ASCII casing is not that implementation. The new preserved
`class_dynamic_conditional_nested_calls` body refuses at class completion
arithmetic (`arith.constant`); loop-nested iterators, unused H slots, global
publication, W's Object.entries/destructuring/original s/RegExp/TypeError/spread,
reloaded induction, broader part 25 and the application driver remain. Native
Bootstrap and the overall plan are unfinished; no whole-bundle gain is claimed.

Exact validation and limits:

- All builds used explicit `tools/remote-build.sh` targets under
  `/tmp/ctbrowser-devbox-build.lock`. Native targets: `ctjs-opt ctjs-translate
  ctcompile-test-native-reference ctcompile-test-exception-recovery
  ctcompile-test-host-contract`. Final class-only replay needed the first three.
  Exact CTests: `ctcompile_exception_recovery`, `ctcompile_host_contract`, each
  with anchored `-R`, `--output-on-failure --no-tests=error`.
- Escape targets: `ctjs-translate ctcompile-test-escape-analysis-arrays
  ctcompile-test-escape-claims ctcompile-test-type-oracle`. Exact CTest:
  `ctcompile_escape_analysis_arrays`. Generated `build/ctcompile/test` lit
  selections: `CTNative/Lowering/Objects/class-dom.mlir` and
  `Analysis/Escape/escape-claims/{nested-invariant-latch,invariant-add-sub-latch,
  invariant-bitwise-latch}.test`. The final oracle replay rebuilt no targets.
- First transaction attempts failed at frame handling (**4.47s**, **4.40s**).
  The first class replay (**34.69s**) found the newly admitted UTF-16 early-return
  source. First arrays (**1.25s**) found two older bitwise depth expectations;
  the next oracle selection (**1/3, 0.12s**) found two older deep-expression
  expectations. Those sources were preserved and expectations corrected; final
  relevant checks pass. No production change followed the final transaction pass.
- Required `tools/format.sh --check` reports **26 pre-existing diagnostics in
  nine HEAD-identical files**. Changed C++/Python formatting, all 13 added JS
  syntax checks, source preservation and `git diff --check` pass. Documentation
  needs no build. Evidence and runnable commands: `/tmp/ctcompile-callee-finish/`.
- Skipped: full CTest/compiler lit, public-class replay, complete DOM/String/
  assignment suites, broad corpus/native matrices, WPT/test262, whole Bootstrap
  and separate lifetime matrices. These are focused results, not a full-suite
  pass. Browser compliance counts remain historical. The predecessor temporary
  authorized SSH key and local private key were removed, preserving other keys.
  No push.


## Conditional dataset iterators and invariant bitwise latches, 2026-09-19 UTC

Resumed **ce49f0dd** and the six dirty ctcompile paths left by the **04:59
conditional/bitwise threads**, explicitly abandoned at the **05:01:02 AGENT-SYNC
loop exit**. Both-agent history and unmerged branches were reviewed; September 7
WIP is already an ancestor. Three parallel agents supplied escape completion,
conditional-proof review and the next-H analysis. Two hit service limits after
findings; root completed review, transaction coverage, validation and commits.

**b290e7e0** admits the original early-return class parameter/helper bodies
and new holder, inverse-guard and nested-conditional specimens. The private
snapshot proof retains condition producers and dominating operations on the path
reaching an iterator. Complete entry proof still checks both original arms,
effects, snapshot epochs and joined scalar state. Folding stays inside the
iterator's block; live opens are recollected after mutation. All **301 previous
class source bodies are unchanged**; nine new bodies and six new transaction
controls cover conditional success and refusal/rollback. Five bodies moved from
refusals to positives without source edits.

**470e4e82** reuses the existing bounded bitwise/shift transfer for invariant
latches within the existing two-operation limit. Original primitive snapshots,
masked shift counts, signed results, backedge identity, work budget and final
index bounds remain checked. Existing repeated-bitwise bodies retain their
expressions with newly proved outcomes. Added CFG/SCF rows and a fourteen-function
source oracle cover all six operators and negative controls.

Focused passes: transaction **1/1 (4.26s; total 4.27s)**, host contract **1/1
(0.46s; total 0.47s)**, class DOM **1/1 (144.97s): 408 source observations,
8 native executions, 3,108 refusals**. Arrays **1/1 (1.24s; total 1.25s)** and four selected escape oracles **4/4 (0.15s)** pass. New oracle:
**44 observed sites / 14 sound / 14 of 28 precision**, zero violations, partial,
pending or unclaimed sites. The existing 20 VM byte-index divergences remain
explicit; native agrees with Node. All **seven tested source/test hashes** match
the devbox. Inspected output uses
`element_ref`, owning strings/vectors and existing browser helpers; source/binary
Script and dispatch exclusions pass.

**Next:** `class_dynamic_original` still refuses at the typed DOM member proof.
Original H needs Unicode-correct lowercase and normalized output-key
collision/prototype proof. The smallest identified casing seam is a proved
`charAt(0)` result: zero or one UTF-16 unit, with pinned Unicode full-lower mappings
(including expanding U+0130 and unchanged surrogates), using public Core
converters. Regex case folding and Script's deliberately ASCII casing are not
substitutes. Preserve the original dataset key; existing ordered object storage
can handle output collisions, but `__proto__` still needs a unique-preimage proof.
`class_dynamic_conditional_sequential` (direct helper called before and after an
early return) still refuses at lifted closure bookkeeping. Loop-nested iterators,
unused H slots, global publication and W's Object.entries/destructuring/original
s/RegExp/TypeError/spread remain. Deeper/reloaded induction, broader part 25,
the application driver and native Bootstrap remain unfinished.

Exact validation and limits:

- Devbox builds used `tools/remote-build.sh` under
  `/tmp/ctbrowser-devbox-build.lock`. Native probe targets: `ctjs-opt
  ctjs-translate ctcompile-test-native-reference` (**4 actions**); final native
  build added `ctcompile-test-exception-recovery ctcompile-test-host-contract`
  (**4 actions**). Exact CTests: `ctcompile_exception_recovery` and
  `ctcompile_host_contract`, anchored `-R` with `--no-tests=error`. Generated
  build-config lit selected only `CTNative/Lowering/Objects/class-dom.mlir`.
- Escape targets: `ctjs-translate ctcompile-test-escape-analysis-arrays
  ctcompile-test-escape-claims ctcompile-test-type-oracle` (**5 actions**).
  Exact CTest: `ctcompile_escape_analysis_arrays`. Generated-config lit selected
  `Analysis/Escape/escape-claims/{invariant-bitwise-latch,primitive-bitwise,
  signed-bitwise,invariant-add-sub-latch}.test`. All first focused attempts pass.
- Required `tools/format.sh --check` reports **26 pre-existing diagnostics in
  nine HEAD-identical files**. Changed C++/Python formatting, nine new JS syntax
  checks and `git diff --check` pass. Test-description/doc edits need no build.
- Evidence: `/tmp/ctcompile-conditional-final/` contains runnable gate scripts,
  probe/native/escape/format logs, source-preservation inventory and hashes.
  Full CTest/compiler lit, public-class replay, complete DOM/String/assignment
  suites, broad corpus/native matrices, WPT/test262, whole Bootstrap and
  independent dataset lifetime replays were skipped. No full-suite or
  whole-bundle admission gain is claimed. Browser compliance counts remain
  historical; no browser or Script semantics changed. The interrupted session's
  temporary authorized key and local private key were removed, preserving other
  keys. No push.

## Sequential dataset iterators and invariant Add/Sub, 2026-09-19 UTC

Resumed clean **541ed09b** from the **04:31:10 AGENT-SYNC closure** and its
recorded repeated-iterator boundary. Both-agent logs and unmerged branches were
reviewed; September 7 WIP is already an ancestor. No predecessor edits were
pending. Three agents worked in parallel: the escape agent completed its draft
and fixture corrections, and two read-only audits hit service limits after
iterator findings. Root reviewed, integrated and gated the work.

**56a37fd8** admits the unchanged repeated holder, class-method and direct
helper specimens. Sequential top-level iterators normalize in source order;
each snapshot-prefix proof retains all earlier loops and intervening operations.
The final typed DOM proof still covers the entire entry. All **293 prior class
source bodies remain unchanged**. Eight new bodies cover three calls, saved
String results, intervening writes, invalid later calls, effects, nested loops
and stale snapshots. The original repeated-object transaction is now a positive;
two added late-failure transactions preserve source and contract on refusal.

**80849b2d** reuses bounded addition and extracts the existing signed
subtraction transfer for invariant Add/Sub latches within the existing two-layer
limit. Original primitive snapshots, String concatenation versus numeric
conversion, backedge identity, budget charging and final-index bounds remain
checked. Twenty-nine older CFG/SCF expression rows retain their bodies with new
proved outcomes; 70 added rows and a thirteen-function source oracle cover the
extension. No previous source-oracle body changed.

Focused passes: transaction **1/1 (4.49s; total 4.50s)**, host contract **1/1
(0.47s)**, class DOM **1/1 (133.81s)**: **388 source observations, 8 combined native
executions, 2,942 refusals**. The existing 20 VM byte-index divergences remain
explicit; native agrees with Node. Arrays **1/1 (1.19s; total 1.20s)** and six
selected escape oracles **6/6 (0.19s)** pass. New oracle: **41 observed sites /
12 sound / 12 of 26 precision**, zero violations, partial, pending or unclaimed
sites. Sixteen existing assignment refusal checks pass. All **eight tested
source/test hashes** match the devbox. Inspected output uses ordinary owning
strings/vectors and public browser helpers; Script/dispatch exclusions pass.

**Next:** original H still needs Unicode-correct `toLowerCase` and normalized
output-key collision/prototype proof; dataset suffixes can be non-ASCII and
Script's intentionally ASCII casing stays unchanged. Early-return and nested
iterators need a conditional-prefix/scalar-state proof. Unused H slots, global
publication and W's Object.entries/destructuring/original s/RegExp/TypeError/
spread remain before inheritance. Deeper/reloaded induction, broader part 25,
the application driver and native Bootstrap remain unfinished. No whole-bundle
admission gain or full-suite pass is claimed.

Exact validation and limits:

- Native targets: `ctjs-opt ctjs-translate ctcompile-test-native-reference`,
  then `ctcompile-test-exception-recovery ctcompile-test-host-contract` too.
  Probe build **4 actions**, first transaction build **2**, corrected transaction
  build **2**. Exact CTests: `ctcompile_exception_recovery` and
  `ctcompile_host_contract`. Generated-config lit filter:
  `^ctcompile :: CTNative/Lowering/Objects/class-dom[.]mlir$`.
- First transaction attempt **4.38s** failed four assertions because preserved
  control 141 now admits. Its expected verdict was corrected; no production
  change followed. The class probe admits all three original repeated sources;
  early returns retain the top-level-iterator refusal and full original H
  retains the typed DOM member refusal.
- Escape targets: `ctjs-translate ctcompile-test-escape-analysis-arrays
  ctcompile-test-escape-claims ctcompile-test-type-oracle`. Builds **3 actions**,
  then **2** for corrected fixtures. Exact CTest:
  `ctcompile_escape_analysis_arrays`. Generated-config lit filter:
  `^ctcompile :: Analysis/Escape/escape-claims/(invariant-add-sub-latch|nested-invariant-latch|primitive-addition|primitive-subtraction|signed-subtraction|negative-add)[.]test$`.
  First arrays attempt **1.26s** failed 18 assertions: three older outcomes now
  prove, and six new SCF specimens lacked `%two`. Older source expressions were
  retained; the new cases reuse the existing fixture declaring `%two`. No
  production change followed.
- Existing assignment refusal bodies selected independently:
  `dynamic_repeated_loop`, `dynamic_nested_loop`, `dynamic_second_writer`,
  `dynamic_stale_keys`, for both providers and both optimization settings.
  Each still refuses. All devbox actions held `/tmp/ctbrowser-devbox-build.lock`;
  builds used `tools/remote-build.sh` with explicit targets, CTests used anchored
  exact-name `-R` and `--no-tests=error`. Eight new JS syntax checks pass.
- Evidence: `/tmp/ctcompile-sequential-iterators/` contains probe, native,
  native-final, escape, escape-final, boundary, evidence and final-format logs,
  runnable commands and source hashes. Required `tools/format.sh --check`
  retains **26 pre-existing diagnostics in nine HEAD-identical files**; changed
  C++/Python formatting and `git diff --check` pass.
- Full CTest/compiler lit, public-class replay, complete DOM/String/assignment
  suites, broad corpus/native matrices, WPT/test262, whole Bootstrap and
  independent dataset lifetime replays were skipped. Browser compliance counts
  remain historical; no browser implementation or Script semantics changed.
  Temporary task SSH access was removed after verification, preserving other
  keys. No push.

## Direct dataset helpers and nested invariant latches, 2026-09-19 UTC

Resumed clean **1ffeea81** from the **04:11:27 AGENT-SYNC closure** and the
original `class_dynamic_helper` refusal in HANDOFF. Both-agent logs and unmerged
branches were reviewed; September 7 WIP is already an ancestor. There were no
uncommitted predecessor edits. Three agents supplied the escape implementation
and fixture/Unicode recommendations; two hit service limits, and root completed
native implementation, review and focused gates.

**e5426f3c** admits the unchanged original `class_dynamic_helper` body.
Direct entry helpers prove their original callback enclosures before their
unused receiver census. Only proved inert callee operands are cleared; the
existing captured-helper cleanup removes a closure only without remaining
observable uses. Original callbacks and actual arguments retain the complete
DOM proof. Captured class-method calls, argument write order and saved String
results are covered. All **279 previous class source bodies are unchanged**;
**14 new bodies** add positive and refusal coverage. Observable callback
receivers/identity, effects, escaped/replaced helpers, invalid arguments,
new.target, unused helpers and early-return/repeated iterators still refuse.

**0414c4e0** proves two original invariant operation layers with the
existing Plus/Neg/BitNot/Mul/Div/Mod/Pow transfers. Every recursive value consumes
budget; leaves retain their original literal or unchanged saved snapshot. The
change removes duplicated one-operation handling. Repeated property reads,
Add/Sub recomputation, deeper/changing operands, noncanonical conversions,
zero/fractional strides and final-index bounds retain their checks. Twenty-four
prior nested CFG/SCF rows retain their expressions with newly proved outcomes;
58 added rows and an eleven-function source oracle cover the extension.

Focused passes: native transaction **1/1 (4.35s)**, host contract **1/1
(0.46s; total 0.47s)**, class DOM **1/1 (123.43s)**: **364 source observations,
8 combined native executions, 2,754 refusals**. Public class **1/1 (199.25s)**.
The 20 existing VM byte-index divergences remain explicit; native matches Node.
Arrays **1/1 (1.19s)** and six selected escape oracles **6/6 (0.15s)** pass.
New oracle: **35 observed sites / 8 sound / 8 of 23 precision**, zero violations,
partial, pending or unclaimed sites. All **ten tested hashes** match the devbox.
Generated output uses `element_ref`, ordinary owning strings/vectors, direct
predicates and existing dataset helpers; source/binary Script and dispatch
exclusions pass. Required formatting retains **26 existing diagnostics in nine
unchanged files**; changed checks pass. Evidence: `/tmp/ctcompile-class-helper/`.

**Next:** full original H still needs Unicode-correct `toLowerCase` and normalized
assignment-key collision/prototype proof. Dataset suffixes are not necessarily
ASCII; public Core/Boost.URL/simdutf provide no existing correct casing API, and
Script's intentionally ASCII behavior is unchanged. Early-return loops and
actual repeated iterators still need a complete top-level-iterator proof.
Unused H slots, global publication and W's Object.entries/destructuring/original
s/RegExp/TypeError/spread remain before inheritance. Deeper/recomputed induction,
broader part 25, the application driver and native Bootstrap remain unfinished.
No whole-bundle admission gain or full-suite pass is claimed.

Exact validation and limits:

- Native builds used `ctjs-opt ctjs-translate ctcompile-test-native-reference`
  for two **5-action** probes, then those targets plus
  `ctcompile-test-exception-recovery ctcompile-test-host-contract` (**2 actions**).
  Exact CTests: `ctcompile_exception_recovery`, `ctcompile_host_contract`.
  Lit: `CTNative/Lowering/Objects/{class-dom,class-initialization}.mlir` through
  the generated `build/ctcompile/test` configuration. Baseline helper refusal
  advanced from the direct-call census to lifted closure bookkeeping, then
  passed; full original H kept its typed DOM member refusal throughout.
- Escape builds used `ctjs-translate ctcompile-test-escape-analysis-arrays
  ctcompile-test-escape-claims ctcompile-test-type-oracle` (**3 actions**, then
  **2** for corrected expectations). Exact CTest:
  `ctcompile_escape_analysis_arrays`. Lit:
  `Analysis/Escape/escape-claims/{nested-invariant-latch,invariant-unary-latch,invariant-product-latch,invariant-division-latch,invariant-power-latch,bitnot-latch}.test`.
  First array attempt failed **50 assertions (1.20s)**: 13 older `~(+literal)`
  expressions now prove. Their source bodies were preserved and their expected
  reads/escapes corrected. No production change followed that failure. Three
  older source-oracle bodies remain byte-identical; their nested result claims
  are the only changed expected verdicts.
- All devbox actions held `/tmp/ctbrowser-devbox-build.lock`; explicit target
  builds used `tools/remote-build.sh`. Selected CTests used anchored exact-name
  `-R` expressions with `--no-tests=error`; lit used anchored case filters.
  Logs: `baseline-probe.log`, `probe-gate.log`, `callee-probe.log`,
  `native-gate.log`, `escape-gate.log`, `escape-final-gate.log`, `evidence.log`,
  `final-format.log`. Fourteen new JS syntax checks and 16 Node observations pass.
- Full CTest/compiler lit, complete DOM/String suites, broad corpus/native
  matrices, WPT/test262, whole Bootstrap and independent dataset lifetime
  replays were skipped. Browser compliance counts remain historical. No browser
  implementation or Script semantics changed. Temporary task SSH access was
  removed after verification, preserving other keys. No push.

## Class dataset loops and invariant powers, 2026-09-19 UTC

Resumed clean **24b6383e** from the **03:46:37 AGENT-SYNC closure**. The latest
work was committed; September 7 WIP is already an ancestor. Three parallel agents
supplied the escape implementation and class/casing findings before service
limits; root completed diagnosis, integration and focused validation.

**023bbf5e** admits the original `class_dynamic_method` and
`class_dynamic_capture` bodies. Fixed method cells may be read through later
loops/completion switches while writes and captures stay ordered. The shared
constructor census distinguishes closed instances from unrelated SSA values;
all formal parameters and prototype observations retain conservative checks.
Already-called methods use the complete original-call proof, avoiding a synthetic
second iterator. Unused methods still require their own proof. Receiver aliases,
actual parameters and loop branches are checked. All **290 previous source
specimens are unchanged**; **12 new bodies** cover successes and refusals.

**0e8a9705** shares the existing bounded scalar power transfer with one original
invariant Pow latch. Both operands must retain their original primitive snapshots;
only the existing exact zero/unit identities are admitted. Changing/nested inputs,
noncanonical conversions, zero strides, original property keys and final bounds
retain their checks. Existing expression bodies remain unchanged.

Focused passes: arrays **1/1 (1.21s; total 1.22s)** and four selected escape
oracles **4/4 (0.14s)**. New oracle: **34 observed sites / 8 sound / 8 of 22
precision**, zero violations, partial, pending or unclaimed sites. Native
transaction **1/1 (4.43s; total 4.44s)**, host contract **1/1 (0.48s; total
0.49s)**, class DOM **1/1 (117.31s)**: **348 source observations, 8 combined native
executions, 2,594 refusals**. The 20 existing VM byte-index divergences remain
explicit; native agrees with Node. Public class, constructor refusals and borrowed
argument refusals **3/3 (200.59s)**. Source/binary Script and dispatch exclusions
pass. Final eight source/test hashes match the devbox. Required formatting retains
**26 existing diagnostics in nine unchanged files**; changed checks pass.
Evidence: `/tmp/ctcompile-class-loop/`.

**Next:** full original H still stops at Unicode `toLowerCase` and normalized-key
collision/prototype proof. No correct public casing implementation exists; keep
Script's intentionally ASCII behavior unchanged. `class_dynamic_helper` still
stops at the class census's unsupported `ctjs.call_direct`. Loops behind early
returns and actual repeated iterators still require one top-level-iterator proof.
Unused H slots, global wrapper publication and W's Object.entries/destructuring/
original s/RegExp/TypeError/spread remain before inheritance. Nested/recomputed
induction and broader part-25 work remain. Full H, W/W+r/W+r+H, native Bootstrap,
the application driver and the whole plan are unfinished. No whole-bundle
admission gain or full-suite pass is claimed.

Exact validation and limits:

- Native build targets: `ctjs-opt`, `ctjs-translate`,
  `ctcompile-test-native-reference`, `ctcompile-test-exception-recovery`,
  `ctcompile-test-host-contract`. Exact CTests: `ctcompile_exception_recovery`
  and `ctcompile_host_contract`. Lit through `build/ctcompile/test`:
  `CTNative/Lowering/Objects/{class-dom,class-initialization,constructor-refusals,object-argument-refusals}.mlir`.
- Escape build targets: `ctjs-translate`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`. Exact CTest:
  `ctcompile_escape_analysis_arrays`. Lit:
  `Analysis/Escape/escape-claims/{invariant-power-latch,invariant-division-latch,primitive-power,unit-power}.test`.
- Native probe sequence: fixed-cell reads exposed a retained prototype method
  definition; the constructor's module-wide key census was the cause. A discarded
  callee-bookkeeping experiment and diagnostic edits left `DOMPreparation.cpp`
  byte-identical to HEAD. Scoping the method census then exposed synthetic
  duplicate iterators, fixed by reusing original calls. No failed focused test
  expectation was rewritten. The two old dynamic-method/capture bodies moved
  unchanged to positives; every other prior fixture entry remains unchanged.
- Logs: `probe-gate.log`, `callee-probe.log`, `diagnostic.log`, `alias-probe.log`,
  `original-call-probe.log`, `escape-gate.log`, `native-gate.log`, `evidence.log`
  and `final-format.log`. The final sync rebuilt only a production comment that
  names the existing conservative alias ceiling; no logic changed after the
  passing native gate. Twelve new JS syntax checks and changed C++/Python
  formatting pass. `tools/format.sh --check` retains the historical 26 diagnostics
  in nine HEAD-identical files; `git diff --check` passes.
- Full CTest/compiler lit, complete DOM/String suites, broad corpus/native
  matrices, WPT/test262, whole Bootstrap and independent dataset lifetime
  replays were skipped. Browser compliance counts remain historical. No browser
  implementation or Script semantics changed. Temporary task SSH access was
  removed after verification, preserving other keys. No push.

## Confined class callbacks and invariant division, 2026-09-19 UTC

Resumed clean **768a71c5** from the **03:18:30 AGENT-SYNC closure**. Both agents'
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
18 but never merged (AGENT-SYNC 02:05:29/02:11:42). **0eadd9a0** finishes that
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

Resumed clean **4167e94c** from the **19:35:36 AGENT-SYNC closure** and its
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

Resumed clean **2c17b381** from the **19:20:02 AGENT-SYNC closure** and
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

Continued clean **ce4c9628** from the **18:59:17 AGENT-SYNC closure** and its
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

Continued clean **9b9ca236**, following the **18:35:50 AGENT-SYNC closure** and
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

Continued clean **414d395e**, following the **18:13:26 AGENT-SYNC closure** and
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

**aef32da5** resumes the full-H thread found in the **17:40:39 AGENT-SYNC journal**
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
The **16:35:25 AGENT-SYNC entry** and two dirty class-DOM fixture files identified
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

## Entry-local Bootstrap M and primitive addition, 2026-09-18 UTC

Started clean at **92192514**. The **16:07:54 AGENT-SYNC closure** confirmed
all interrupted work committed; September 7 WIP is already an ancestor. Both
agents' logs and historical unmerged branches were reviewed. Continued the
original W/r/H helper boundary in HANDOFF and master 00/24. Parallel agents
supplied an escape draft and source audit; root completed the three-file escape
draft and native fixtures after agent service limits.

**5d12efaa** composes exact entry-local helpers with DOM classes. A helper's
original entry call may be ordinary or already resolved, but its closure and
receiver must be proved. Its intrinsic loads, Number `toString` and raw exception
CFG survive into existing typed DOM and URI/JSON proof. These deferrals also
force complete class-method probes. Global-holder targets, constructors, getters
and helpers without that entry call retain the stricter census; unused bodies
cannot disappear without proof. The public class pass remains closed-source-only.

Ordinary lifted helpers discard a direct-call callee operand only for the exact
target with an unused callee argument. All such checks precede closure deletion,
so deleting a child first cannot change acceptance. Nested callee dependencies
remain refused. The original module and contract survive every preparation
failure; no browser source, new value representation or VM dependency was added.

The complete vendor-pinned **M** now executes as `M(shape.read())`, covering
Number/null paths, valid JSON and malformed URI/JSON fallback. All **123 existing
source/expected fixture entries** remain unchanged. New original-M fixtures use
an explicit **1,000,000-step** budget because the conservative class-lift size
bound exceeds the default; the production default remains **100,000**.
Native output still passes the Script/AOT symbol and class-dispatch exclusions.

**1d13c777** proves bounded Boolean/null addition through the existing primitive
conversion, explicitly excluding String concatenation first. Saved operands,
signed cancellation, CFG/SCF transport and zero/unit lengths retain their original
identities. String, BigInt, undefined, unknown/changing/repeated operands and
out-of-bound results remain unproved. Existing source bodies were preserved;
false/null zero-index and zero-length expectations now reflect the proof.

Focused devbox evidence: `/tmp/ctcompile-helper-effects/`.

- Built `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
  `ctcompile-test-exception-recovery`, `ctcompile-test-host-contract`:
  **8 initial actions, 5 first-retry actions, 6 final actions**.
- Exact `ctcompile_exception_recovery`: **1/1 (3.86s; total 3.87s)**,
  with twelve added transaction controls per DOM provider.
- Exact `ctcompile_host_contract`: **1/1 (0.46s; total 0.47s)**.
- Exact `CTNative/Lowering/Objects/class-dom.mlir`: **1/1 (44.52s)**,
  **160 Node/interpreter observations, 8 native executions, 982 refusals**.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`: **1/1 (196.01s)**;
  **154 observations, 380 native executions, 308 unprepared / 190 preparation
  refusals**, plus 16 constructed-method executions / 20 refusals, 8 original-r
  executions / 4 refusals, 4 prototype-key executions / 6 refusals and 11
  prepared-source refusals. This passed before the final DOM-only ordinary-call
  and cleanup corrections; its public-pass path was unchanged by those edits.
- Built `ctjs-translate`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`: **3 actions**.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (1.09s)**.
- Exact `Analysis/Escape/escape-claims/primitive-addition.test`: **1/1 (0.12s)**,
  **29 sites / 11 sound / 11 of 17 precision**, zero violations.
- Exact `Analysis/Escape/escape-claims/{negative-add,add-cancellation}.test`:
  **2/2 (0.22s)**; each **20 sites / 8 sound / 8 of 12 precision**, zero violations.

The initial transaction run failed **3.77s (total 3.78s)** on retained callee
bookkeeping and the original-M size budget. After those corrections, transaction
and contract passed **3.79s / 0.45s**, but class DOM failed **7.44s** because the
conditional argument left M's call ordinary. Proving that exact local call and
making cleanup order-independent produced the final passes above. A nested-helper
refusal pins the latter boundary. Escape checks needed no retry.

All **10 tested source/test hashes** match the devbox. Required
`tools/format.sh --check` reports **26 pre-existing diagnostics in nine
HEAD-identical files**; changed C++ formatting, Black, Node syntax and
`git diff --check` pass. Full CTest, full compiler lit, complete DOM suites,
broad corpus/native matrices, WPT/test262 and whole-Bootstrap replay were skipped.
No whole-bundle gain is claimed. No browser edits or push.

**Exact next:** original W/r/H still needs callable captures and complete helper
identity/effect composition. A class method capturing sibling M is outside the
constructor-only capture proof; nested callee dependencies and unused global
holder slots remain strict. Continue that boundary without artificial source
calls, then F's callback/RegExp and full H. W also requires `Object.entries`,
destructuring's `__ctbrowser_iter_open`, original `s`, RegExp/TypeError and spread
proofs before inheritance. Preserve the existing W/W+r/W+r+H refusals; the last
specimen still omits `s`. Full H Unicode, retained callbacks, broader escape
conversions and the application driver remain unfinished. Native Bootstrap and
the overall plan are not complete.

## Mixed class/DOM intrinsics and primitive subtraction, 2026-09-18 UTC

Started clean at **051ab3d2**. The **15:45 AGENT-SYNC closure** confirmed the
interrupted Error/power drafts were committed. September 7 WIP is already an
ancestor; historical isolated branches were retained. Continued the exact
W/r/H boundary recorded by HANDOFF and master 00/24. Parallel agents supplied
escape implementation and a source audit; root completed native fixtures after
agent service limits. No browser implementation or runtime semantics changed.

**690ff8b6** composes the existing DOM intrinsic declarations with the class
helper and optional Error. Class preparation proves and consumes only its own
identities; the DOM declarations survive all private method probes and final
reproof. Entry/method intrinsic loads and Number `toString` use that existing
typed proof. Every affected method is probed, even one with no call. The original
source census rejects intrinsic replacement before any method disappears.
Constructors, getters and helpers retain their stricter census.

Number conversion now executes with entry-local classes and an unused throwing
Error getter, using existing ctbrowser behavior. An ignored pure intrinsic result
is inert; observable identity use, arbitrary coercion receivers, unknown effects,
missing/duplicate identity and late failures still refuse. Failed preparation
preserves the original module and contract. Native output retains the no-Script/
AOT and no-class-dispatch checks. All **109 pre-session source/expected entries** remain unchanged.

**18e56dac** reuses bounded primitive Number conversion for Boolean/null
subtraction. Saved operands, negative results and CFG/SCF transport retain their
original identities; primitive property keys and unknown/changing/repeated inputs
remain unproved. Four old expectations changed without source rewrites.

Focused devbox evidence: `/tmp/ctcompile-mixed-intrinsics/`.

- Built `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
  `ctcompile-test-exception-recovery`, `ctcompile-test-host-contract`:
  **10 initial actions**, **4 after the production correction**, then **three
  2-action test-only rebuilds**.
- Exact `ctcompile_exception_recovery`: **1/1 (3.80s; total 3.81s)**;
  eleven additional transaction controls per DOM provider.
- Exact `ctcompile_host_contract`: **1/1 (0.48s; total 0.49s)**.
- Exact `CTNative/Lowering/Objects/class-dom.mlir`: **1/1 (36.29s)**,
  **140 Node/interpreter observations, 8 native executions, 814 refusals**.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`: **1/1 (201.27s)**;
  **154 observations, 380 native executions, 308 unprepared / 190 preparation
  refusals**, plus 16 constructed-method executions / 20 refusals, 8 original-r
  executions / 4 refusals, 4 prototype-key executions / 6 refusals and 11
  prepared-source refusals. Original W/W+r/W+r+H refusal controls remain intact.
- Built `ctjs-translate`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`: **3 actions**.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (1.06s; total 1.07s)**.
- Exact `Analysis/Escape/escape-claims/{primitive-subtraction,signed-subtraction}.test`:
  **2/2 (0.23s)**, zero violations. Primitive: **26 sites / 11 sound /
  11 of 15 precision**; signed: **48 / 26 / 26 of 28**.

The first transaction run failed **3.77s** because the class census rejected
Number `toString`; the production correction delegates that exact read to typed
receiver proof. Two later runs (**3.76s / 3.84s**) identified an incorrect new
refusal expectation for an ignored pure Number return. Its original source became
a positive; a DOM write using the identity supplies the actual refusal. A fourth
run (**3.79s**) caught its stale contract assertion. After transaction/parser
passed, class DOM failed **0.19s** on duplicated mixed-request setup in the direct
helper test loop. Removing that duplicate required only a Python sync and the
successful exact class-DOM retry above. No later production change or replay of
passing compatibility/escape tests was needed.

All **11 tested source/test hashes** match the devbox. Required
`tools/format.sh --check` retains **26 pre-existing diagnostics in nine unchanged
files**, verified byte-identical to HEAD. Changed formatting, Black, Node syntax
and `git diff --check` pass. Full CTest, full compiler lit, complete DOM suites,
broad corpus/native matrices, WPT/test262 and whole-Bootstrap replay were skipped.
No browser edits or push.

**Exact next:** complete original **W/r/H** helper effects through typed DOM proof,
starting M's Number/JSON/URI exception chain and F/H helper identities. Mixed
declarations now compose; constructors/getters/helpers still use the stricter
class census. W also needs `Object.entries`, destructuring's
`__ctbrowser_iter_open`, original `s` (`Object.prototype.toString.call`),
RegExp/TypeError and spread proofs. The current W+r+H specimen does not include
`s`; retain that refusal control and include the original helper in a later full
specimen. Then address inheritance. Full H Unicode, retained callbacks, the
application driver and broader primitive conversions remain unfinished. Escape
next can address Boolean/null addition with explicit exclusion of String
concatenation. Native Bootstrap and the overall plan are not complete.

## Declared Error class/DOM composition and primitive powers, 2026-09-18 UTC

Resumed **eight dirty tracked files plus the primitive-power oracle at c2108572**,
left by the **15:25:30 AGENT-SYNC loop failure**. The original **15:11:07 journal**,
diff and current handoff identified the interrupted Error/primitive-power thread.
September 7 WIP is already an ancestor; both agents' logs and unmerged branches
were reviewed. Parallel agents completed escape recovery and part of the audit;
root finished native fixtures after service limits.

**4bcc196c** composes the existing declared Error binding/getter proof with DOM
class preparation. Requests may declare the class helper plus optional unique
`Error`. The complete original source and initial bindings are proved before
those declarations are consumed. An unused literal-message throwing `NAME`
getter can then be erased. Referenced throws, including reads in unused methods,
still reach the final typed DOM proof and refuse. Error replacement inside the
entry or in a suffix, escaping payloads, effectful messages, missing/duplicate
identity, late unknown DOM effects and exhausted budgets remain refusals; failed
preparation preserves both source and contract. No new Error representation,
VM dependency, browser implementation or ownership carrier was added.

The class fixture retains all **102 previous source bodies and expectations**.
Its former optional-Error declaration refusals now lower those exact requests
successfully; duplicate Error declarations supply a separate refusal control.
Native output still passes its Script/AOT symbol and class-dispatch checks.

**a5a08550** reuses the existing exact primitive Number conversion for
Boolean/null power bases and exponents, keeping the bounded zero/unit identities,
negative parity, saved operands and CFG/SCF transport. Original primitive keys,
unknown/changing/repeated producers and general powers remain unproved. All old
bodies remain intact. The focused oracle exposed two stale `unit-power.test`
expectations for the already supported original `(-1) ** "2"` body; only its
claims changed to confined, with zero observed soundness violations.

Focused devbox evidence: `/tmp/ctcompile-error-finalize/`.

- Built `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
  `ctcompile-test-exception-recovery`, `ctcompile-test-host-contract`: **10 actions**.
- Exact `ctcompile_exception_recovery`: **1/1 (3.78s; total 3.79s)**;
  ten added transaction controls per DOM provider.
- Exact `ctcompile_host_contract`: **1/1 (0.46s; total 0.47s)**.
- Exact `CTNative/Lowering/Objects/class-dom.mlir`: **1/1 (32.92s)**,
  **120 Node/interpreter observations, 8 combined native executions, 720 refusals**.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`: **1/1 (200.61s)**;
  **154 source observations, 380 native executions, 308 unprepared refusals,
  190 preparation refusals**, plus 16 constructed-method executions / 20 refusals,
  8 original-r executions / 4 refusals, 4 prototype-key executions / 6 refusals,
  and 11 prepared-source refusals.
- Built `ctjs-translate`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`: **3 actions**.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (1.04s; total 1.05s)**.
- Exact `Analysis/Escape/escape-claims/{primitive-power,bounded-power,
  power-identities}.test`: **3 passes** in the initial four-case **0.42s** run.
  `unit-power.test` alone failed FileCheck on the two stale expectations above;
  its test-only retry passed **1/1 (0.11s)**. No production correction or passing-test replay was needed.
- All four power oracles report zero violations. Primitive power: **24 sites /
  9 sound / 9 of 13 precision**; bounded/unit power: each **26 / 13 / 13 of 15**;
  power identities: **26 / 11 / 11 of 15**.

Required `tools/format.sh --check` retains **26 pre-existing diagnostics in nine
unchanged files**, verified byte-identical to HEAD. Changed C++ formatting,
Black, Python/Node syntax and `git diff --check` pass. All **12 tested source/test hashes** match the devbox.
Full CTest, full compiler lit, complete DOM suites, broad corpus/native matrices,
WPT/test262 and historical whole-Bootstrap replay were skipped. No browser edits
or push.

**Exact next:** complete original **W/r/H** class/DOM effects and binding
composition. Optional Error now composes for unused throwing getters; mixed
class/DOM intrinsic declarations, TypeError, Object/RegExp/iterator operations
and full helper identities remain unsupported. Preserve every original method
and `NAME` throw while proving them. Original W, W+r and W+r+H controls still
refuse at `r`, `H`, and an unproved call/binding/effect respectively. Then address
inheritance. Full H Unicode, retained callbacks, the native application driver
and broader primitive conversions remain open. Native Bootstrap and the overall
plan are unfinished.

## Config defaults and primitive bitwise snapshots, 2026-09-18 UTC

Resumed **six dirty files at d7e13751**, abandoned by the **14:17:21 AGENT-SYNC
loop failure**: omitted/defaulted Config argument tests and the primitive-bitwise
draft. Both agents' recent commits and unmerged branches were reviewed; September
7 WIP was already an ancestor. Parallel agents worked on native fixtures, escape
recovery and a proof audit; root finished their drafts after service limits.

**b871feb2** proves original omitted/defaulted method arguments through the
shared typed DOM boundary. The existing lifter already supplies undefined for
omitted arguments. Exact strict-undefined comparisons select a result kind only
after both arms and their effects pass proof; the private candidate then folds
the selected arm in source order and receives independent final reproof. Null
does not trigger defaults. Unknown effects in skipped defaults still refuse.
Literal/static-getter defaults, fresh empty `DefaultType`, transitive calls and
argument/default/body write order execute. All 85 previous source bodies and
expectations, plus the interrupted default-order body, remain intact.

Fixed cell reads may occur in later `scf.if` arms; confined field reads may occur
in later structured branches/loops. Writes stay in their original blocks, and
captures/aliases retain the existing census. DOM writes expose their already
proved undefined result. Nullable `getAttribute` Strings pass through a four-line
native conversion to the existing ctbrowser attribute API, including `"null"`.
Entries proved to return only undefined emit ordinary C++ `void` signatures and
returns. The two old DOM write refusal bodies now execute unchanged. No browser
source, VM dependency, collector or ownership carrier was added.

**abdeeec1** shares exact primitive Number conversion with Boolean/null bitwise
and shift snapshots. Both operand orders, signed/unsigned results, saved inputs,
CFG/SCF transport and original primitive-key refusals are checked. Unknown,
changing/repeated and out-of-bound results remain unproved. Twenty-four old
false/null zero-index expectations and two zero-length expectations changed
without rewriting their source bodies.

Focused devbox evidence: `/tmp/ctcompile-default-complete/`.

- Built `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
  `ctcompile-test-exception-recovery` and `ctcompile-test-host-contract`:
  initial complete default gate **7 actions**, final void-return retry
  **58 actions, pass**.
- Exact `ctcompile_exception_recovery`: **1/1 (3.79s)**,
  including twelve added default-argument transaction controls.
- Exact `ctcompile_host_contract`: **1/1 (0.47s; total 0.48s)**.
- Exact `CTNative/Lowering/Objects/class-dom.mlir`: **1/1 (30.54s)**;
  **116 Node/interpreter observations, 8 combined native executions, 678 refusals**.
- Exact `CTNative/Browser/native-dom-strings.test`: **1/1 (143.35s)**;
  **787 Node/VM observations, 8 GCC/Clang binaries**, both providers/policies/layouts;
  **1,052 source, 44 provenance/depth, 24 method, 241 capture,
  101 replacement, 22 branch and 27 completion refusal checks**.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`:
  **1/1 (203.07s)**; **154 source observations, 380 native executions,
  308 unprepared refusals, 190 preparation refusals**. Its additional controls
  report **16 constructed-method executions / 20 refusals, 8 original-r executions /
  4 refusals, 4 prototype-key executions / 6 refusals, 11 prepared-source refusals**.
- Exact `CTNative/Browser/native-dom.test`: **1/1 (244.35s)**;
  **20 entries, 41 refusal controls**, both policies/layouts with GCC/Clang,
  linking DOM/Core and selector-only Style.
- Built `ctjs-translate`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims` and `ctcompile-test-type-oracle`:
  **3 initial / 2 retry actions, pass**.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (1.05s)**.
- Exact `Analysis/Escape/escape-claims/{primitive-bitwise,signed-bitwise,
  primitive-division}.test`: **3/3 (0.32s), zero violations**. Primitive bitwise
  and division each **24 sites, 9 sound, 9 of 13 precision**; signed bitwise
  **26 sites, 11 sound, 11 of 15 precision**.

The initial five-action native build passed; class DOM failed **0.14s** because
116 observations exceeded two-digit lexical ordering. Three-digit names fixed the
harness. Subsequent class DOM failures **8.76s / 9.13s / 8.99s / 8.92s** exposed
strict-undefined proof, nested fixed-cell reads, nested confined-field reads and
used DOM write results respectively. Intermediate native builds ran **64 / 6 / 5
actions**; exception recovery passed **3.72s / 3.80s / 3.73s**, and host contract
passed **0.47s / 0.49s** after its target was added. The initial arrays run failed
**1.03s** with 144 assertions from 24 stale expectations; the original bodies were
preserved. The initial complete default gate also passed class DOM **31.13s**,
exception recovery **3.74s (total 3.75s)** and host contract **0.47s**. The subsequent
native DOM run failed **203.40s** because the unchanged returned-removeAttribute
source exposed a nullable scalar return carrier. The proof now publishes exact
undefined-return evidence for ordinary C++ `void` output; its C++ client adds
a compile-time void-return assertion. A 66-action scheduled rebuild stopped after
nine attempts on a misplaced proof assignment (undeclared `result`); correcting
that placement produced the final 58-action build. Only exception recovery,
host contract, class DOM and native DOM were repeated for this change. Native
DOM then failed **199.36s** on three unused variables in the new C++ test client,
after the void/no-carrier checks passed. Existing-style void suppressions fixed
the client; its unchanged source case moved first, and only native DOM was
rerun using test-only sync. Final checks above pass; no test was repeated after
its final pass.

All 21 tested source/test hashes match the devbox. Final DOM, escape retry and
hash wrappers exit **0**. Required `tools/format.sh --check` retains **26 existing
diagnostics in nine HEAD-identical files**. Changed pinned formatting, Black, Python/Node
syntax and diff checks pass. Full CTest/compiler lit, complete DOM, broad
corpus/native matrices, WPT/test262 and historical whole-Bootstrap replay were
skipped. No push.

**Exact next:** compose complete original Config **W/r/H** with the existing
class/DOM proof, including declared Error/TypeError, Object/RegExp/iterator and
helper identities/effects. Preserve the unused throwing `NAME` getter and every
method body. Existing original-source controls still refuse W at global `r`,
W+r at global `H`, and W+r+H at an unproved call/binding/effect. The DOM class
projection still accepts only the class helper declaration; declared Error
composition is an explicit next seam. Then address inheritance. Full H Unicode,
retained callbacks, the application driver and broader primitive escape
conversions remain open. Native Bootstrap and the overall plan remain unfinished.

## Transitive method arguments and primitive division, 2026-09-18 UTC

Started clean at **2ac4ab0d**. The **13:43:50 AGENT-SYNC session closure**
confirmed the interrupted argument work was committed; this session continued
its recorded Config transitive-call boundary. September 7 WIP was absent.
Parallel agents supplied native tests, an escape draft and a proof audit; root
completed the drafts after agent service limits.

**4ab08f61** proves parameterized methods reached transitively from original
entry calls on each actual instance. A bounded census follows exact
`this.method(...)` edges through that instance's definitions. The existing
shared private proof retains real arguments and field writes in source order.
Independent zero-parameter probes cannot supply parameter authority. Unused
formals, uncalled instances, invalid/dead calls and recursive graphs still refuse.
The previous transitive-only refusal now executes unchanged; all 19 prior positive
and 42 prior refusal source bodies remain intact. Twelve new transaction checks
cover both DOM providers. No browser/runtime code or ownership carrier changed.

**ac4cf776** reuses exact primitive Number conversion for Boolean/null division
and remainder snapshots. Both operand orders, signed results, saved operands,
CFG/SCF transport and original primitive-key refusals are checked. Zero divisors,
nonintegral quotients and unknown/changing/repeated operands remain unproved.
Two old Boolean divisor expectations changed; their source bodies did not.

Focused devbox evidence: `/tmp/ctcompile-transitive-config/`.

- Built `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference` and
  `ctcompile-test-exception-recovery`: **7 actions, pass**.
- Exact `ctcompile_exception_recovery`: **1/1 (3.77s)**.
- Exact `CTNative/Lowering/Objects/class-dom.mlir`: **1/1 (26.74s)**;
  **88 Node/interpreter observations, 8 combined native executions, 526 refusals**.
  Both providers, C++ layouts and optimization settings pass GCC/Clang and
  Script/AOT exclusion.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`:
  **1/1 (202.17s)**; **154 source observations, 380 native executions,
  308 unprepared refusals, 190 preparation refusals**.
- Built `ctjs-translate`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims` and `ctcompile-test-type-oracle`:
  **3 initial / 2 retry actions, pass**.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (1.03s; total 1.04s)**;
  **656 dense / 796 induction / 342 structured rows**, with
  **25,381 / 52,692 / 26,991** budget cutoffs. Initial run failed **1.00s**:
  two stale Boolean expectations and two malformed new repeated-SCF fixtures
  caused nine assertions. Fixed the expectations and new fixture SSA.
- Exact `Analysis/Escape/escape-claims/{primitive-division,primitive-product,
  signed-division}.test`: **3/3 (0.33s), zero violations**. Division/product:
  each **24 sites, 9 sound, 9 of 13 precision**; signed division:
  **26 sites, 13 sound, 13 of 15 precision**.

All nine source/test hashes match the devbox. Final native, escape retry and hash
wrappers exit **0**. Required `tools/format.sh --check` retains **26 existing
diagnostics in nine HEAD-identical files**; changed pinned formatting, Black,
Python/Node syntax and diff checks pass. No test was repeated after its final
focused pass. Full CTest/compiler lit, complete DOM, broad corpus/native matrices,
WPT/test262 and historical whole-Bootstrap replay were skipped. No push.

**Exact next:** preserve original omitted/defaulted arguments in W's
`_getConfig → _mergeConfigObj(t, e)` and
`_typeCheckConfig(t, e = this.constructor.DefaultType)` calls. This session
proves the transitive prerequisite, not complete W. Full Config W/r/H, its unused
Error/TypeError and other effect obligations, inheritance, full H Unicode,
retained callbacks, the application driver and broader primitive escape conversions
remain open. Native Bootstrap and the overall plan remain unfinished.

## Original class-method arguments and primitive products, 2026-09-18 UTC

Resumed four dirty files at **47a08427**, abandoned by the **13:27:02 AGENT-SYNC
loop failure**. The September 7 WIP was absent from unmerged branches. Agents
recovered native tests, extended primitive-product tests and audited the native
proof; root finished the drafts after two agent service limits.

**8f322043** proves parameterized class methods using original entry calls,
with each method called directly on each actual entry-local instance. One private
copy retains all those calls, their actual arguments and their source-position
field state. Zero-parameter methods retain independent probes, including unused
bodies. Unused parameterized methods and transitive-only invocation still refuse;
no synthetic parameter authority is introduced. Entry `scf.if`/`scf.yield` results
now reach the same complete typed DOM proof, including both branches. The public
closed-source pass and constructor/getter/helper census remain unchanged.

Five new native cases cover key/element arguments, repeated calls, field replacement,
two instances and transitive calls with direct witnesses. All pre-session fixture
bodies and observations remain unchanged. Dead-branch bad keys and unknown DOM
calls, missing host authority, later invalid arguments and uncalled second instances
remain refused. Ten added transaction checks cover both DOM providers.

**a5838467** shares the existing exact primitive conversion between unary
operations and multiplication. Boolean/null factors acquire bounded signed Number
facts only at the result; original keys and identities survive. Both operand orders,
saved factors and CFG/SCF snapshots are covered. Unknown/changing/repeated factors
remain unproved. Two original false/null multiplication zero-shrink bodies now admit
without source changes. No browser/runtime code or ownership carrier changed.

Focused devbox evidence: `/tmp/ctcompile-arguments-finish/`.

- Built `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference` and
  `ctcompile-test-exception-recovery`: **7 initial / 4 retry build actions**.
- Exact `ctcompile_exception_recovery`: **1/1 (3.71s; total 3.72s)**; first
  **1/1 (3.64s)**. Covers successful publication and rollback for both providers.
- Exact `CTNative/Lowering/Objects/class-dom.mlir`: **1/1 (23.36s)**,
  **76 Node/interpreter observations / 8 native executions / 450 refusals**.
  Both DOM providers, C++ layouts and optimization settings pass GCC/Clang and
  Script/AOT exclusion. First run failed **6.10s** at entry short-circuit census;
  original fixture bodies were preserved while the proof gained typed branch handling.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`: **1/1 (200.83s)**.
- Built `ctjs-translate`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`: **3 initial /
  2 retry actions**. The first arrays run failed **1.00s** because two new
  structured tests expected one path instead of both; only expectations changed.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (0.99s; total 1.00s)**,
  **656 dense / 756 induction / 330 structured rows**, with
  **25,360 / 50,700 / 25,085** budget cutoffs.
- Exact `Analysis/Escape/escape-claims/{primitive-product,primitive-unary,
  signed-product}.test`: **3/3 (0.32s), zero soundness violations**.
  Product and unary: each **24 sites / 9 sound / 9 of 13 precision**;
  signed product: **33 sites / 17 sound / 17 of 19 precision**.
- All nine source/test hashes match the devbox. Final native, escape and hash
  wrappers exit **0**. No test was rerun after its final focused pass.

Required `tools/format.sh --check` reports **26 existing diagnostics in nine
HEAD-identical files**. Changed pinned C++ formatting, Black, syntax and diff checks
pass. Full CTest/compiler lit, complete DOM suite, broad corpus/native matrices,
WPT/test262 and historical Bootstrap replay were skipped. No push.

**Exact next:** prove transitive-only original method arguments for Bootstrap W's
`_getConfig → _mergeConfigObj / _configAfterMerge / _typeCheckConfig` chain without
inserting artificial source calls. Defaulted/omitted arguments, complete Config
W/r/H composition and inheritance remain open, as do full H Unicode, retained
callbacks, the application driver and broader escape conversion proofs. Native
Bootstrap and the overall plan remain unfinished.

## Constructor-stored DOM methods and primitive unary snapshots, 2026-09-18 UTC

Resumed dirty **9586e616** after the **13:02:02 AGENT-SYNC loop failure**:
two unfinished class DOM test files named the original `class_element` method
boundary. The September 7 WIP is already merged. Parallel agents recovered tests,
audited the proof and implemented the independent escape change; root finished
integration after agent service limits.

**3a933858** proves zero-parameter class methods against each actual entry-local
construction, including unused and transitive method bodies. Private copies reuse
the existing closure lift, field normalization and typed DOM validator. Each method
gets an independent probe; source entry parameters retain their original host
provenance. Unread method definitions can disappear only after all their bodies
pass. The final emitted entry is independently reproved and published atomically
with its contract. Constructor/getter/helper effects and public class preparation
retain their existing restrictions. An early eligibility check prevents retaining
pointers into method bodies that normalization replaces.

The original `class_element` now executes natively through `this.element` and
`Button.NAME`; no original fixture was rewritten. Two new unused-method positives
also pass, including a write that must never execute. Transitive field replacement,
fake second instances, detached methods, invalid arguments, dead-branch unknown
calls and nested constructions remain refused. No runtime or browser code changed.

**d2dc123f** gives original Boolean/null unary Plus/Neg/BitNot results exact
Number snapshots. Original primitive keys, unknown inputs, repeated producers and
changing predecessor values keep their refusals. Four old false/null zero-shrink
bodies now correctly admit; all original test bodies remain intact.

Focused devbox evidence: `/tmp/ctcompile-method-finish/`.

- Built the seven explicit affected targets: `ctjs-opt`, `ctjs-translate`,
  `ctcompile-test-native-reference`, `ctcompile-test-exception-recovery`,
  `ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
  `ctcompile-test-type-oracle`. Build rounds: **9, 10, 4, 2 actions**.
- Exact class DOM lit: **1/1 (19.13s)**, **56 Node/interpreter observations,
  8 combined native executions, 312 refusals**. Both providers, layouts,
  optimization settings, GCC/Clang and Script/AOT exclusion pass.
- Exact class initialization lit: **1/1 (200.18s)**.
- Exact exception recovery: **1/1 (3.57s)**, including 16 class/DOM transaction
  cases across both providers.
- Exact arrays CTest: **1/1 (0.99s; total 1.00s)**. Exact `primitive-unary.test` and
  `signed-bitnot.test`: **2/2 (0.22s), zero soundness violations**. The new unary
  oracle measures **24 sites / 9 sound / 9 of 13 precision**; signed BitNot
  measures **32 sites / 14 sound / 14 of 18 precision**. Arrays retain
  **656 dense / 734 induction / 326 structured rows**.

The initial DOM gate failed **4.16s** because an unused definition blocked the
existing lift. Subsequent **17.75s** and **19.27s** passes preceded added controls
and the early eligibility fix. Initial arrays failed **0.99s** only on the four
newly proved old expectations; their source bodies were preserved. The earlier
exception check passed **3.69s (total 3.70s)**. All eleven final source/test hashes
match the devbox and gate wrappers exit zero. All 45 prior class DOM bodies/signatures remain.
Required pinned formatting retains **26 existing diagnostics in nine unchanged
files**; changed formatting, Black, Python syntax and diff checks pass. The first
format scan also saw four transient diagnostics while the child was editing;
they are absent from the final scan.

**Next boundary:** extend method-argument provenance beyond zero-parameter methods,
without granting unused method formals host authority. The existing original
`unused_key_dom_method` still refuses. Complete Config W/r/H composition, inheritance,
full H Unicode, retained callbacks and the application driver remain open. Full
CTest/compiler lit, complete DOM suite, broad corpus/native matrices, WPT and
test262 were skipped; historical Bootstrap coverage was not replayed. Native
Bootstrap and the overall plan remain unfinished. No push.

## Captured local class getters, 2026-09-18 UTC

Resumed clean **1b941768** after the **12:36 AGENT-SYNC failure** explicitly
abandoned the previous capture work. The **12:34 journal** and this handoff named
`press$4`'s captured `Button.NAME` as the interrupted thread; no dirty draft
remained. September 7 WIP was already merged. Parallel agents supplied tests and
a proof audit. The escape agent hit a service limit before producing a draft.

**84f93391** proves captured reads of a method's own local constructor through
the existing static-getter proof. Local cells in class setup functions must have
ordered same-block uses, with every write storing the same SSA value and reads
or captures after initialization. Fixed aliases retain the constructor/receiver
use census, including chained aliases. Arbitrary captures, changing writes, early
reads, identity escapes, unknown effects and cyclic getters remain refused.
Getter expansion removes only the proved capture/cell plumbing after the complete
original body census. Shared immutable-capture rules and browser/runtime code
are unchanged; no owning runtime carrier was added.

Two pure captured NAME/DATA_KEY cases and one DOM getter-key case now execute
natively. Source refusals and four raw cell controls include a chained-alias
mutation found during parallel review. All pre-existing source bodies remain.
The preserved `class_element` now reaches **unknown call, binding or reflective
effect**, past its original capture refusal.

Focused devbox validation, evidence `/tmp/ctcompile-capture-finish/`:

- Built `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference` and
  `ctcompile-test-exception-recovery`: **4 successful build actions**. The first
  build found a typed-value conditional mismatch in new code; corrected.
- Exact `CTNative/Lowering/Objects/class-dom.mlir`: **1/1 (16.28s)**, including
  both providers, C++ layouts, optimization settings, GCC/Clang and Script/AOT
  exclusion. Three preliminary failures (**3.15s / 3.46s / 3.82s**) showed that
  the new test also requested conditional-cell/entry-flow or Boolean-arithmetic
  support. The new fixture was narrowed to captured getter selection; the
  preserved `class_element` specimen was never rewritten.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`: **1/1 (198.53s)**.
- Exact `ctcompile_exception_recovery`: **1/1 (3.66s; total 3.67s)**.

All five source/test hashes match the devbox; native and hash wrappers exit **0**.
Required pinned `tools/format.sh --check` reports **26 existing diagnostics in
nine HEAD-identical files**; changed C++ formatting, Black, Python syntax and diff
checks pass. Full CTest/compiler lit, complete DOM suite, escape tests, broad
corpus/native matrices, WPT and test262 were skipped. Historical Bootstrap
measurements were not replayed. No browser changes or push.

**Exact next:** prove original `press$4` DOM effects through its constructor-stored
`this.element`: `classList.toggle` and `getAttribute`, including every unused
method/getter/helper body before erasure. The existing typed DOM receiver/field
proof and private preparation transaction are the seam; validating only the
residual called code would lose unused-body obligations. Public class preparation
remains closed-source-only. Config, inheritance, full H Unicode, retained
callbacks, the application driver and broader escape/conversion work remain open.
Native Bootstrap and the overall plan are unfinished.

## Original class/DOM composition and String BitNot, 2026-09-18 UTC

Started clean at **4961fffd** after the **12:09:01 AGENT-SYNC session closure**.
No interrupted edits remained and September 7 WIP was absent. Resumed the recorded
`class_key`/`class_order` composition boundary. Independent agents supplied focused
native tests, String BitNot and a read-only proof audit; root completed integration
after the audit agent hit a service limit.

**9c16dffd** reuses the original class proof and rewrite inside `prepareDOMEntry`'s
private candidate. DOM requests may supply exactly `__ctbrowser_class_defined`,
which the existing binding proof checks before consumption. Mixed intrinsic sets
remain refused. Only selected-entry parameter uses and unknown entry calls defer
to the final typed DOM proof. Every original constructor/method/getter/helper,
including unused bodies, retains the existing complete census. The public class
pass stays closed-source-only; its extracted rewrite body is unchanged.

The existing closure lifter normalizes constructors/methods under a quadratic
IR-size ceiling (operation count times operations, operands and block arguments).
This bounds input size; the lifter does not yet expose a per-scan step budget.
Only closures proved lifted by this invocation and retaining solely inert roots
are removed. Supplied native reports are discarded, then direct receivers,
confined fields and all DOM effects are independently reproved before publishing
module and contract together. No browser/runtime code or ownership carrier changed.

**c23423ff** completes canonical decimal String handling for unary BitNot using
the existing bounded parser and exact signed complement calculation. Original
String identities and CFG/SCF snapshots survive. Noncanonical/signed Strings,
out-of-range values and changing/repeated producers remain unproved. All eight
original signed-bitnot oracle bodies are preserved.

Focused devbox validation only; evidence: `/tmp/ctcompile-dom-class-compose/`.

- Built `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
  `ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims`,
  `ctcompile-test-type-oracle`, `ctcompile-test-exception-recovery`.
  Initial build found a missing include in new DOM preparation; corrected.
  Successful rebuilds took **8, 4 and 4 actions**, with explicit affected targets.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (0.98s)**.
- Exact `Analysis/Escape/escape-claims/signed-bitnot.test`: **1/1 (0.18s)**.
- Exact `ctcompile_exception_recovery`: final **1/1 (3.43s; total 3.44s)**,
  initial **1/1 (3.45s)**. Includes ten class/DOM transaction cases across both
  providers, checking successful publication and rollback after refusal.
- Exact `CTNative/Lowering/Objects/class-dom.mlir`: final **1/1 (15.55s)**,
  **44 Node/interpreter observations / 8 combined native executions / 208 refusals**.
  Preserved `class_key` and `class_order` now execute in the existing GCC/Clang,
  explicit/deduced, optimized/unoptimized, both-provider gate with Script/AOT
  exclusion. All 40 previous source bodies/signatures remain intact. Four new
  unused-body adversaries and mixed-authority controls remain refused.
  Initial run failed **3.35s** at the DOM manifest parser; corrected before the
  **15.59s** pass. Final run also covers stricter lifting-size accounting.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`: **1/1 (191.53s)**.

All twelve changed source/test hashes match the devbox. Final build/native/evidence
wrappers exit **0**. Required pinned `tools/format.sh --check` retains **26 existing
diagnostics in nine HEAD-identical files**; changed-file pinned formatting, Black,
Python syntax and diff checks pass. Full CTest/compiler lit, complete DOM suite,
broad corpus/native matrices, WPT and test262 were skipped. Historical Bootstrap
measurements were not replayed. No push or browser source changes.

**Exact next:** the preserved `class_element` first refuses with **complete
capture-free source functions**. Its `press$4` has one upvalue: the local `Button`
binding used by `Button.NAME`. The entry stores the same constructor closure into
that cell twice, with method closure creation between the writes. Prove that
captured class/getter identity from original source before widening class
preparation; then prove original method DOM effects, including unused bodies.
A separate `unused_key_dom_method` probe still refuses at the complete original
unknown-call/effect census. Keep the original specimen and all bodies intact,
reuse existing capture/getter/receiver proofs, and retain transactional typed DOM
reproof. Config, inheritance, full H Unicode, retained callbacks, application
driver and broader Number/String conversions remain open. Native Bootstrap and
the overall plan are unfinished.

## Confined DOM fields and canonical String powers, 2026-09-18 UTC

Continued clean **86ebc737** after the **11:56:44 AGENT-SYNC journal** closed
its interrupted receiver work. Resumed the ordinary instance-field boundary
recorded in HANDOFF/master00; September 7 WIP was absent from unmerged branches.
Three agents handled escape analysis, native tests and a read-only composition
audit. Root recovered the five-file escape draft after service limits and reused
the existing native driver after its test agent stopped before creating files.

**15e2827b** forwards fields of fresh local objects after direct receiver
expansion. Every object use must be an ordered same-block root or ordinary
constant-key field read/write, with a preceding write for each read. Reads retain
the value at that source position across later writes. The confined allocation
and field operations disappear; all value producers remain for complete DOM
reproof. Element-valued fields become ordinary borrowed DOM operands. Empty
String keys work. Escapes, identity observations, self-stores, reserved/dynamic
keys, conditional writers and missing writes remain outside this rule. Objects
passed to live closures still reach holder classification before expansion.
No class-provider or original constructor proof was added.

**c57a40a4** reuses the existing canonical decimal parser for both operands of
bounded power identities. Existing zero/unit/parity arithmetic, signed Number
snapshots and source identities remain unchanged. Noncanonical/signed Strings,
out-of-range values, general powers and changing/repeated producers remain
unproved. All eight original bounded-power oracle bodies remain byte-identical.

Focused devbox checks only; evidence: `/tmp/ctcompile-dom-fields/`.

- Explicit targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
  `ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims`,
  `ctcompile-test-type-oracle`: **7 build actions**, no build failures.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (0.96s; total 0.97s)**.
  Dense/induction/structured rows **652 / 697 / 319**; budget cutoffs
  **25,082 / 47,124 / 23,548**.
- Exact `Analysis/Escape/escape-claims/bounded-power.test`: **1/1 (0.11s)**,
  **26 observed sites / 13 sound / zero violations / 13 of 15 precision**.
- Exact `CTNative/Lowering/Objects/class-dom.mlir`: **1/1 (13.89s)**,
  **44 Node/interpreter observations / 8 combined native executions /
  134 refusals**. Six new field sources execute alongside the two direct-receiver
  sources. Three original class sources and fourteen adversarial class bodies
  remain unchanged and retain preparation refusals. The initial run passed
  **14.10s / 128 refusals**; the final run adds three adversarial sources only.
- Selected existing `helper_object_method`, `helper_object_extracted`,
  `helper_object_multiple`, `helper_regex_original_h` sources: **16 Node/VM
  observations / 8 combined GCC/Clang executions / 24 provenance checks /
  88 refusals**. Both providers, optimization settings and explicit/deduced C++
  pass with emitted/linked Script/AOT exclusion. This is a selected DOM gate.

All eight changed source/test hashes match the devbox; both gate wrappers exit
**0**. Required pinned `tools/format.sh --check` retains **26 existing diagnostics
in nine HEAD-identical files**. Changed-file pinned formatting, Black, Python
syntax and diff checks pass. Full CTest/compiler lit, exception recovery,
class-initialization and complete DOM suites, broad corpus/native matrices,
WPT and test262 were skipped. No browser source/runtime changes or push;
historical Bootstrap measurements were not replayed.

**Exact next:** compose the existing original class proof with DOM preparation
transactionally for preserved `class_key` and `class_order`. Keep the public class
pass closed-source-only. Reuse its proof/rewrite body on `prepareDOMEntry`'s private
candidate; initially require exactly the class helper intrinsic. Only entry
parameter uses and otherwise-unknown entry calls may defer to the final typed DOM
proof. Every original constructor/method/getter/helper, including unused bodies,
must retain the unchanged complete class effect census. Check the helper binding
with the existing closed-source binding proof before consuming its declaration.
Reuse `lowering_detail::closureLifter` under a bounded budget; remove only freshly
proved lifted closures whose remaining uses are inert roots, clear supplied
native reports, then independently recheck direct receivers and local fields.
Preserve entry effect producers through normalization and publish source/contract
only after full DOM reproof. Do not union intrinsic lists or trust residual code
alone. `class_element` still needs original method DOM-effect proof. Remaining
Config, inheritance, full H Unicode, retained callbacks, application driver and
broader Number/String conversions remain open. Native Bootstrap is incomplete.

## Direct DOM receivers and canonical String bitwise snapshots, 2026-09-18 UTC

Resumed dirty **43860a33** from the **11:38:29 AGENT-SYNC journal** and its
explicit **11:38:51 abandonment**. The interrupted files were the two-file
String bitwise draft and untracked `class_dom.py` / `class-dom.mlir`. September 7
WIP was absent from unmerged branches. Independent agents audited the native
proof and recovered tests; root completed escape drafts after service limits.

**3c4e3b1e** extends `DOMSource` to expand already normalized direct calls with
an exact private, capture-free target, no live closure creation, exact arity and
undefined callee/new.target. Each invocation binds its own actual receiver.
The existing body-cloning logic is shared with live closure expansion; live
closure receiver/lexical rules remain unchanged. Direct targets may observe their
receiver, but not callee/new.target or nested closures/captures. Complete source
bodies, recursion/depth/work limits, unvisited-function rejection and transactional
DOM reproof remain required. This supplies no new source constructor/prototype
proof or class-provider authority.

Two equivalent source/normalized-IR cases execute natively: one attribute read
and repeated reads/writes on two distinct element receivers. Three original
class/DOM specimens retain Node/interpreter observations and preparation refusals;
all fourteen adversarial class bodies remain. Generated and linked output calls
public DOM C++ and excludes Script/AOT dependencies and dynamic class dispatch.

**a1debd07** reuses the canonical decimal parser for both operands of bitwise
AND/OR/XOR and left/signed-right/unsigned-right shifts. Signed Number snapshots,
masked counts and original String identities survive CFG/SCF transport. Signed
or noncanonical String forms and out-of-range values remain unproved. All eight
original source-oracle function bodies remain byte-identical.

Focused devbox checks only; evidence: `/tmp/ctcompile-dom-recovery/`.

- Explicit targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
  `ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims`,
  `ctcompile-test-type-oracle`, `ctcompile-test-exception-recovery`.
  Initial build: **8 actions**; two test-header rebuilds: **2 actions each**.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (0.98s)**.
  Dense/induction/structured rows: **643 / 673 / 312**; budget cutoffs:
  **24,680 / 45,638 / 22,889**.
- Exact `Analysis/Escape/escape-claims/signed-right-shifts.test`:
  **1/1 (0.13s)**, **36 observed sites / 15 sound / zero violations /
  15 of 21 precision**.
- Exact `ctcompile_exception_recovery`: **1/1 (3.48s)**, including the existing
  DOM preparation transaction and rollback checks.
- Exact `CTNative/Lowering/Objects/class-dom.mlir`: **1/1 (10.92s)**,
  **20 Node/interpreter observations / 8 combined native executions / 68 refusals**.
- Selected existing `helper_object_method`, `helper_object_extracted`,
  `helper_object_multiple`, `helper_regex_original_h` fixtures:
  **16 Node/interpreter observations / 8 combined GCC/Clang executions /
  24 provenance checks / 88 refusals**. Both providers, optimization settings and
  explicit/deduced C++ pass. This selection is not the complete DOM String suite.

All eight changed source/test hashes match the devbox. The final native wrapper
exits **0**. The initial array gate failed **0.97s (0.98s total)** on six stale
assertions for the original `"0" >>> "0"` shrink; its source remained intact and
its expectation now records the proved empty array. The first new native test
failed **0.16s** because the fixture expected an indirect call after the resolver
had already made it direct. Only the fixture changed after the production build.
Malformed call arity remains the IR verifier's responsibility.

Required pinned `tools/format.sh --check` reports **26 existing diagnostics in
nine HEAD-identical files**. Changed-file pinned formatting, Black, Python syntax
and diff checks pass. Full CTest/compiler lit, class-initialization and complete
DOM lit suites, broad corpus/native matrices, WPT and test262 were skipped.
No browser source/runtime changes or push; historical Bootstrap measurements
were not replayed.

**Exact next:** join complete original class and DOM effect proofs, then normalize
ordinary instance fields before DOM admission. `ClassInitialization` remains
closed-source-only and explicit entry parameters must be unused. `DOMSource`
classifies ordinary objects with field reads as callable holders, while `DOMEntry`
classifies fresh objects as JSON aggregates, rejects element-valued fields and
lacks ordinary instance-field reads. Reuse constructor lifting's existing
new.target, primitive-return, prototype and receiver proofs and the new direct
inliner; preserve every original unused method/getter's effect obligations before
erasure. The preserved `class_key`, `class_order`, and `class_element` specimens
are the next concrete boundary. Remaining Config, inheritance, full H Unicode,
retained callbacks, application driver and String power/other conversions remain
open. Native Bootstrap is not complete.

## Explicit class entries and canonical String division, 2026-09-18 UTC

Continued clean **18cc8b1b** after the **11:14:38 AGENT-SYNC journal** closed
its predecessor. Resumed the explicit class-entry boundary recorded in HANDOFF;
September 7 WIP was absent. Independent agents implemented the escape slice,
reviewed the proof, and drafted native tests. Root recovered the test draft after
its agent reached a service limit and narrowed it to the implemented boundary.

**1ed820cb** allows class preparation to select an explicit imported entry
under `closed-source-v1`. The existing inert-declaration proof checks its script
wrapper. Explicit parameters must be unused; callee identity can only create
local closures, and receiver/new.target checks remain. Every original body still
receives the complete effect/prototype census. The script entry keeps its old
argument restriction. Wrapper, publication, constructors and method bodies remain
for subsequent representation proofs. **No new DOM provider or native execution
admission is claimed.** Two entries prepare; source and adversarial checks retain
the used-parameter, callee, wrapper, prototype, unknown-call and DOM refusals.

**33732dbd** reuses the existing canonical decimal parser for either String
operand of bounded division/remainder. Exact quotient/nonzero divisor and signed
magnitude rules are unchanged; saved values retain their original identities
through CFG/SCF transport. Noncanonical strings, fractional quotients, zero
divisors and changing/repeated producers remain unproved. All eight original
source-oracle bodies remain intact.

Focused devbox validation only; evidence: `/tmp/ctcompile-class-entry/`.

- Explicit targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
  `ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims`,
  `ctcompile-test-type-oracle`: **8 build actions**.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (0.97s; total 0.98s)**.
  Dense/induction/structured rows: **589 / 670 / 309**; budget cutoffs:
  **23,271 / 45,318 / 22,645**.
- Exact `Analysis/Escape/escape-claims/signed-division.test`: **1/1 (0.13s)**,
  **26 observed sites / 13 sound / zero violations / 13 of 15 precision**.
- Exact `CTNative/Lowering/Objects/class-entry.mlir`: **1/1 (0.76s)**,
  **2 preparations / 4 Node/interpreter observations / 26 refusals**.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`: **1/1 (193.47s)**;
  **146 source observations / 364 native executions / 292 unprepared refusals /
  176 preparation refusals**, plus **16 constructed-method executions / 20 refusals**,
  **8 original r executions / 4 refusals**, **4 key executions / 6 refusals**, and
  **11 prepared native refusals**. Global-chain first complete budget: **418**.

Eight selected source/test hashes match locally and on the devbox. The class
regression checks both optimization settings, explicit/deduced C++ and GCC/Clang;
emitted C++ contains no ctbrowser dependency or prototype/home metadata. Its
original Config r/M/F/H/W specimen still measures **a=7** in Node/interpreter
and refuses preparation.

The new entry fixture initially omitted required closed-source manifest fields,
then counted retained metadata/unused constants as live class operations, and
named an ambient-call control so it called itself. Those fixture defects were
corrected; the production implementation needed no post-build correction.
Required pinned `tools/format.sh --check` retains **26 existing diagnostics in
nine HEAD-identical files**. Changed-file pinned formatting, Black, Python syntax
and diff checks pass. Full CTest/compiler lit, DOM lit, broad corpus/native
matrices, WPT and test262 were skipped. No browser source/runtime changes or push.

**Exact next:** join class and DOM effects, then normalize constructor/method
receivers before `prepareDOMEntry`. The existing constructor lifter already proves
new.target, primitive returns, prototype immutability and closed receiver uses.
Its direct calls carry an explicit object receiver and undefined callee;
`DOMSource` currently expands live closure calls and maps the receiver to the
entry receiver. It needs a proved receiver-aware normalization before those
representations compose. All original method/getter bodies must keep their effect
obligations, including unused bodies; a residual-only proof after erasing them
or a union of intrinsic lists is insufficient. The present class provider remains
closed-source-only and observed entry parameters still refuse. Remaining Config,
inheritance, full H Unicode, retained callbacks, the application driver and further
String bitwise/shift/power conversions remain open. Whole-Bootstrap/Button/Data
measurements remain historical.


## Shared DOM preparation and canonical String products, 2026-09-18 UTC

Started at clean **a4a35dcc**. The **10:57:53 AGENT-SYNC journal** explicitly
closed the previous interruption; September 7 WIP was absent. Resumed its
recorded class/DOM composition boundary. Three agents investigated native tests,
proof composition and escape analysis; the two native agents reached service
limits without edits. Root completed the native work and gated the escape draft.

**2c4eb46a** extracts the existing DOM preparation transaction from
`LowerToEmitC` into `HostContract/prepareDOMEntry`, declared in `Preparation.h`.
The caller now reuses one URI/helper/element-guard/iteration sequence, including
wrapper removal, callback visibility, undefined/force normalization and final
source reproof. Source IR and the refreshed host contract publish together only on
success. The existing URI source checks two successful providers and six
stale-manifest/missing-intrinsic/zero-budget refusals, preserving both inputs.
This is a prerequisite refactor; it admits no new class/DOM programs.

**9336d642** reuses the canonical decimal String parser for either operand of
bounded multiplication, then applies the existing signed-magnitude and overflow
proof. Saved String values, zero and negative Number results retain their source
identities through CFG/SCF transport. Noncanonical/range/BigInt inputs, repeated
producers and changing backedges remain refused. All eight original oracle
function bodies remain intact; two former String refusals are now proved.

Focused devbox validation only; evidence: `/tmp/ctcompile-class-dom/`.

- Explicit targets: `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`, `ctjs-translate`,
  `ctjs-opt`, `ctcompile-test-native-reference`, `ctcompile-test-exception-recovery`
  and `ctcompile-tool`. Initial four-target escape build: **5 actions**;
  final eight-target build: **10 actions**.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (0.93s; CTest total 0.94s)**.
  Dense/induction/structured rows **585 / 639 / 295**; budget cutoffs
  **23,096 / 43,185 / 21,327**.
- Exact `Analysis/Escape/escape-claims/signed-product.test`: **1/1 (0.11s)**,
  **33 observed sites / 17 sound / zero violations / 17 of 19 precision**.
- Exact `ctcompile_exception_recovery`: **1/1 (3.44s; CTest total 3.45s)**,
  including the new complete DOM preparation transaction checks.
- Selected existing DOM String fixtures `helper_object_method`,
  `helper_object_extracted`, `helper_object_multiple`, `helper_regex_original_h`:
  **16 Node/interpreter observations / 8 combined GCC/Clang executions /
  24 provenance checks / 88 refusals**.
- Selected `nullable_uri_original`, `nullable_uri_helper` and
  `dataset_guarded_values`: **20 URI + 5 dataset Node/interpreter observations /
  8 combined GCC/Clang executions**, including returned String lifetime and
  HTML/SVG dataset checks. These and the four holder fixtures cover both DOM
  providers, optimization settings and explicit/deduced C++.
- Existing `EXPLICIT_FORCES` source: **8 native executions**, ordinary DOM
  provider, both optimization settings/layouts and GCC/Clang. All emitted and
  linked native checks exclude Script/AOT dependencies. These selections do not
  constitute complete DOM lit cases or a broad native matrix.

Both final gate wrappers exit **0**; ten selected input hashes match the devbox.
The initial array check failed **0.97s total** on six assertions for the original
`"0" * 0` shrink. Its source stayed intact; the expectation now records the
proved empty array. Required pinned `tools/format.sh --check` retains **26
existing diagnostics in nine HEAD-identical files**. Stable formatting passes
**918 C++ / 109 Python / 105 web files**; changed-file pinned formatting and
diff checks pass. Full CTest/compiler lit, class lit, complete DOM lit cases,
broad corpus/native matrices, WPT and test262 were skipped. No browser source,
runtime semantics or historical Bootstrap measurements changed; no push.

**Exact next:** prove class construction/prototype normalization inside an
explicit DOM element entry before calling `prepareDOMEntry`. The audit found
three concrete gaps: class preparation requires script entry index 0; it requires
constructed instances and leaves their `ConstructOp`/method bodies; DOM helper
expansion/entry proof has no ordinary class-instance representation, and the
host-manifest path skips the ordinary closure/constructor lifter. Preserve the
complete original body/effect and receiver proof while joining those paths.
An intrinsic-list union cannot supply those obligations. The original Config
r/M/F/H/W defaults specimen's last measurement remains **a=7** in Node/interpreter
with preparation refusal; it was not rerun here. Remaining Config, inheritance,
full H Unicode, retained callbacks and the application driver remain open.
Escape String Div/Mod and additional unary/shift conversions are still unproved.

## Native global helpers and canonical String unary snapshots, 2026-09-18 UTC

Started at clean **c32fee6a**. The **10:33:17 AGENT-SYNC journal** closed the
previous interruption, and September 7 WIP was already merged. Continued its
exact prepared-global-holder boundary. Independent agents drafted tests and
escape work and reviewed the native proof; root completed both drafts after
service limits.

**6774a550** expands proved global callable-holder reads into direct private
helper calls during class preparation, after the complete source/prototype and
host-binding checks. Targets have no captures or receiver/new.target/callee
observations. Missing arguments retain undefined padding; surplus arguments
refuse. Holders requested through host roots/observations refuse removal.
Calls and effects stay in order; reads/loads follow private method clones through exit normalization.
Holder storage and unreferenced helper definitions then disappear. Native output
uses the existing direct-function lowering, with no global object carrier.

The four original holders now execute natively (**32 added executions**). A new
loop/return-dispatch specimen covers remapped reads, short calls and an unused
slot (**8 executions**, result **38**). Both optimization settings, explicit/deduced
C++ and GCC/Clang pass. Unsafe order, replacement, aliasing, identity, receiver
and ambient-effect controls remain. The complete original
r/M/F/H/W defaults specimen still returns **a=7** in Node/interpreter and refuses
preparation.

**53d6db64** reuses the bounded canonical decimal parser for unary Plus/Neg String
snapshots. It preserves original result identities, signed magnitudes, saved
values and CFG/SCF transport. Noncanonical strings, changing backedges and
negative own indices remain unproved. All six original source-oracle functions
remain byte-identical; two add retained/noncanonical controls.

Focused devbox checks only; exact evidence: `/tmp/ctcompile-global-native/`.

- Explicit targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
  `ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims`,
  `ctcompile-test-type-oracle`: initial **9 build actions**. Array expectation
  retry: **2 actions**; the fixture-only native retry needed no build work;
  native cleanup retry: **3 actions**.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (0.94s)**. Dense/induction/
  structured rows **585 / 628 / 291**; budget cutoffs **23,079 / 42,090 / 21,009**.
- Exact `Analysis/Escape/escape-claims/signed-unary.test`: **1/1 (0.11s)**,
  **26 observed sites / 13 sound / zero violations / 13 of 15 precision**.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`: **1/1 (191.22s)**;
  **146 source observations / 364 native executions / 292 unprepared refusals /
  176 preparation refusals**, plus **16 constructed-method executions / 20 refusals**,
  **8 original r executions / 4 refusals**, **4 key executions / 6 refusals**, and
  **11 prepared native refusals**. Global-chain first complete proof budget: **418**.

Both final wrappers exit **0**; nine selected source/test hashes match locally
and on the devbox. The class execution checks reject any `ctbrowser::` dependency
or prototype/home metadata in emitted C++.

The initial array check failed **0.93s** on sixteen stale assertions; original
sources stayed intact, with canonical zero/unit expectations corrected and an
additional noncanonical stale-solver mutation retained. The first class attempt
failed **33.78s** because the new fixture lacked the intended dispatch shape;
the next failed **33.60s** on an unused private helper body. The final cleanup
removes that definition only after all holder rewrites and empty symbol scans.
Required pinned `tools/format.sh --check` retains **26 existing diagnostics in
nine HEAD-identical files**; stable formatter **916 C++ / 109 Python / 105 web**,
changed-file pinned formatting, Black, syntax and diff checks pass. Full CTest,
full compiler lit, DOM String tests, broad corpus/native matrices, WPT and
test262 were skipped. No browser source/runtime changes or push.

**Exact next:** jointly prove class initialization and the existing DOM provider
for original H. Class preparation currently permits only closed-source-v1 with
class helper/optional Error; DOM entry/source contracts remain separate. Reuse
`LowerToEmitC`'s existing `withProvedClone` sequence (URI, DOM helpers, element
guards, iteration, reproof) and public H/M/F helpers. Merely combining intrinsic
allow-lists would omit body/effect obligations. Original Number/JSON/URI,
RegExp/TypeError and iterator/destructuring paths remain open, along with the
remaining Config work, inheritance, full H Unicode, retained callbacks and the
application driver. Whole-Bootstrap/Button/Data numbers remain historical.

## Recovered global holders and String-left subtraction, 2026-09-18 UTC

Resumed **0d85f2d9** with five uncommitted predecessor files. The
**10:13:29 AGENT-SYNC journal** marked that iteration abandoned; its claims and
`git diff` identified global-holder tests and the String-left escape draft.
No September 7 WIP remained unmerged. Independent agents recovered both test
areas and reviewed the proof; root finished integration after service limits.

**acd89055** extends the shared callable-holder proof to a unique global
publication in the script entry. All fixed callable slots must precede publication,
and the entry prefix must be incapable of invoking source code. This establishes
order for subsequent cross-function reads without trusting resolver annotations.
Every binding write, holder alias and callable use is checked; receiver, callee
and new.target observation still refuse. Every slot body, including unused slots,
keeps the complete effect census. An unrelated earlier call deliberately remains
outside this bounded proof; widening needs a complete caller-order analysis.

Four global holders now pass class preparation. Their original global stores,
loads, callable slots and calls remain, and **all eight optimized/unoptimized
native attempts still refuse**. This adds preparation coverage, not global native
execution. Early/indirect calls, late slots, replacement, alias writes, detached
calls, identity/receiver observation and ambient effects remain refusal controls.

**752b0177** reuses the existing canonical decimal parser for the left String
operand of subtraction. Saved Number lengths, either/both String operands,
negative snapshots, exact cancellation and CFG/SCF transport keep their original
result identity. Noncanonical strings and changing backedges remain unproved.
Two historical length expectations were corrected with their original source
bodies retained; a noncanonical stale-solver mutation remains a refusal.

Focused devbox validation only; final wrapper exit **0**, eleven selected input
hashes match locally and remotely:

- Explicit targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
  `ctcompile-tool`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`.
  Initial build: **37 actions**; expectation-only retry: **2 actions**.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (0.99s)**.
  Dense/induction/structured rows: **585 / 599 / 283**; budget cutoffs:
  **22,931 / 40,266 / 20,317**.
- Exact `Analysis/Escape/escape-claims/signed-subtraction.test`: **1/1 (0.12s)**;
  **48 sites / 26 sound / zero violations / 26 of 28 precision**.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`: **1/1 (174.47s)**;
  **144 source / 324 native / 288 unprepared / 173 preparation refusals**,
  plus **16 ordinary executions / 20 refusals**, **8 original r executions /
  4 refusals**, **4 key executions / 6 refusals**, and **19 prepared native
  refusals**. Global-holder-chain first complete proof budget: **416**.
- Existing DOM String fixtures `helper_object_method`, `helper_object_extracted`,
  `helper_object_multiple`, `helper_regex_original_h`: **16 Node/interpreter
  observations / 8 combined GCC/Clang executions / 24 provenance / 88 refusal
  checks**, both providers, optimization settings and C++ layouts. Generated
  output and linked symbols pass Script/AOT exclusion. This selects existing
  helpers; it is not the full DOM String lit case.

The first array run failed **0.92s** on thirteen assertions from those two old
expectations. The first class run failed **18.96s** because its new slot-count
assertion mixed identical SSA names from different functions; the check now
examines the script entry. Both corrections changed tests only.
Required pinned `tools/format.sh --check` retains **26 diagnostics in nine
HEAD-identical files**. The stable formatter workflow passes **916 C++ / 109
Python / 105 web files**; changed-file pinned formatting, Black, Python/shell
syntax and diff checks pass. Full CTest/compiler lit, full DOM String case,
broad corpus/native matrices, WPT and test262 were skipped. No browser source
or runtime semantics changed. Evidence: `/tmp/ctcompile-global-resume/`.

**Exact next:** give the four global holders a native lowering using the shared
proof and existing closure machinery. `ClosureLifting/Methods.cpp` still rejects
`StoreGlobalOp` in `usesCloseTheShape`; global receiver provenance also needs a
consumer. Promote the existing `PREPARED_ONLY` cases only after native execution.
Then compose class preparation with the existing DOM provider for original H.
The complete original r/M/F/H/W defaults specimen still returns **a=7** in
Node/interpreter and refuses preparation; global identity proof alone supplies
no DOM effects. Reuse the public H/M/F helpers. Iterator/destructuring,
RegExp/TypeError exits, remaining Config helpers, inheritance, full H Unicode,
retained callbacks and the application driver remain open. Whole-Bootstrap,
Button and Data counts remain historical.

## Local callable holders and signed String subtraction, 2026-09-18 UTC

Started at clean **46e8ae18**. Its **09:50:04 AGENT-SYNC journal** explicitly
closed the preceding interruption; September 7 WIP was absent. Continued the
recorded Config/H boundary with independent escape implementation and native
proof review. The Config audit agent hit a service limit before editing; root
completed its recommended extraction.

**b83da7d5** shares DOMSource's existing local callable-holder identity/order
proof through `analyzeLocalCallableObject` in ClosedCallable. Class preparation
adds exact callee-use checks, unique closure storage, and non-observation of
receiver, new.target and callee. Held helpers are checked before their enclosing
direct callers, so an arrow may save lexical `this` without reading it. Every
slot body, including unused slots, still receives the complete effect census.
The class pass leaves holder operations intact for the existing closure lifter.
Four local method/arrow/branch/argument-order specimens add **32 native
executions**, both optimization settings, both C++ layouts and GCC/Clang.
Replacement, alias writes, detached calls, receiver observation, ambient effects
and global holders remain refusal controls. DOM preparation retains its existing
behavior and diagnostics; browser source and runtime semantics were not changed.

**6bf2351f** extends the existing canonical decimal String offset proof to negative
subtraction results and negative left Number snapshots. It preserves original
result identity, saved lengths, CFG/SCF transport and the bounded magnitude domain;
noncanonical/coercible alternatives, overflow and changing backedges remain
unproved. Original formerly refused String bodies are retained as positive checks.

Focused devbox validation only; ten selected input hashes match locally/remotely:

- Explicit targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
  `ctcompile-tool`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`.
  Initial three-target build: **34 actions**; caller-order correction: **3**;
  final seven-target build: **7 actions**.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`: **1/1 (172.22s)**,
  **130 source / 324 native / 260 unprepared / 159 preparation refusals**,
  plus **16 ordinary executions / 20 refusals**, **8 original r executions /
  4 refusals**, **4 key executions / 6 refusals**, and **11 prepared native
  refusals**. The local-holder arrow's first complete proof budget is **131**.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (0.92s)**.
  Dense/induction/structured rows: **585 / 580 / 278**; budget cutoffs:
  **22,879 / 39,132 / 19,684**.
- Exact `Analysis/Escape/escape-claims/signed-subtraction.test`: **1/1 (0.12s)**;
  **34 sites / 18 sound / zero violations / 18 of 20 precision**.
- Selected existing DOM String fixtures: `helper_object_method`,
  `helper_object_extracted`, `helper_object_multiple`, `helper_regex_original_h`.
  **16 Node/interpreter observations**, **8 combined GCC/Clang executions**
  across both providers, optimization settings and layouts, **24 existing method
  provenance checks**, **88 existing helper-object refusal checks**. Generated
  output and linked symbols pass the Script/AOT exclusion. This was a focused
  invocation of existing helpers, not the full `native-dom-strings.test` case.

The first class run failed **7.48s** on the new arrow caller-order gap, fixed
before the passing rerun. The temporary DOM harness first lacked Node on bare
SSH's PATH, then selected Clang 18 without `std::expected`; using lit's configured
Node and DOM Clang resolved both harness failures. Only the DOM probe was rerun;
no source changes or broad test replay were needed. Final DOM wrapper exit **0**.
Required pinned `tools/format.sh --check` retains **26 diagnostics in nine
HEAD-identical files**. Stable formatter workflow **916/109/105**, changed-file
pinned formatting, Black, Python syntax, shell syntax and diff checks pass.
Full CTest/compiler lit, full DOM String case, broad corpus/native matrices,
WPT and test262 were skipped. Evidence: `/tmp/ctcompile-config-holder/`.

**Exact next:** extend the shared holder proof to original H's global publication
and cross-function initialization order, then compose class and DOM provider
contracts. The complete original r/M/F/H/W defaults specimen still returns **a=7**
in Node/interpreter and refuses class preparation; local SSA holder proof does
not authorize global H or its DOM effects. Reuse the existing public H/M/F DOM
helpers. Iterator/destructuring, RegExp/TypeError exits, remaining Config helpers,
inheritance, full H Unicode keys, retained callbacks and the application driver
remain open. Whole-Bootstrap/Button/Data counts remain historical.

## Original class-method r and signed unary literals, 2026-09-18 UTC

Started at clean **07764b25**. Its **09:33:03 AGENT-SYNC journal** closed
the preceding interruption; no September 7 WIP remained unmerged. Continued the
recorded original class-method **r** boundary. Independent agents investigated
escape precision, Config/H composition and prototype soundness; two hit service
limits, so root completed and gated the frozen escape draft.

**aa5aa952** distinguishes exact String keys, including the empty string, and
bounded Number keys from unknown keys in the existing prototype-method census.
It reuses public `ctbrowser::number_to_string`; it adds no emitted platform or
runtime code. Bootstrap r's numeric `[0]` read no longer blocks the unrelated
`read` method. Constructor lifting can remove the proved prototype, and the
existing private-caller scalar proof then folds the original `r(null)` guard.
The unchanged original class-method specimen now returns **a=7** in Node,
interpreter and native C++: **4 optimized native executions**, explicit/deduced
output and GCC/Clang. Unoptimized and unprepared forms remain refused. The
empty-string IR control adds **4 executions**; negative-zero/name collisions,
NaN, both infinities, a large finite key and an unknown key retain **6 refusals**.
Successful output contains no ctbrowser or prototype metadata.

The Number-key proof deliberately remains bounded to `abs(value) < 1e15`.
Review found that `ctbrowser/lib/Core/number_format.cpp` casts to `int64_t`
before its range test. The compiler avoids that existing large-value hazard;
the browser finding is recorded in AGENT-SYNC. No browser file was changed.

**1dd56617** preserves original bounded negative Number literals through unary
Plus/Neg using the same shared contents transfer as held signed snapshots.
CFG/SCF transport, original result identity, signed zero, retention and budget
checks are covered. Coercion, fractional/nonfinite/out-of-domain values and
changing backedges remain unproved. Two historical `-(-1)` length expectations
were corrected to retain the unit-length array; their source bodies and the
stale-solver mutation check remain.

Focused devbox checks only (final wrapper exit **0**, seven selected input
hashes match locally and remotely):

- Explicit targets: `ctjs-opt`, `ctjs-translate`,
  `ctcompile-test-native-reference`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`.
  Initial three-target IR probe: **5 build actions**. Final six-target gate:
  **6 actions**, followed by **2** after the length-expectation correction.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (0.93s)**.
  Dense/induction/structured rows: **585 / 559 / 273**; corresponding budget
  cutoffs: **22,876 / 37,780 / 19,126**.
- Selected lit cases: `Analysis/Escape/escape-claims/signed-unary.test` and
  `CTNative/Lowering/Objects/{class-initialization,constructor-refusals,prototype-scalars}.mlir`:
  **4/4 (160.75s total)**. Signed-unary oracle: **20 sites / 8 sound / zero
  violations / 8 of 12 precision**.
- Class gate: **120 source observations / 292 native executions / 240 unprepared
  refusals / 149 preparation refusals**, plus **16 ordinary executions / 20
  refusals**, **8 original r helper executions / 4 refusals**, **4 key-control
  executions / 6 refusals**, and **11 prepared native refusals**. The branching
  helper's preparation budget remains **419**. Prototype scalars: **7 source /
  48 native / 2 refusals**; constructor controls: **11 source / 8 native /
  18 refusals**.

The first final gate failed SSH reachability before building; authorized
`server.sh start` and `allow-ip` restored access. The next array run failed
**1.81s** on nine assertions from the two historical length controls; the new
CFG/SCF controls passed. Correcting those expectations required no compiler
change. Native and source bodies stayed unchanged. No browser/runtime semantics
changed.

Required `tools/format.sh --check` retains **26 diagnostics in nine HEAD-identical
files**. The same full formatter workflow with installed stable clang-format
passes **916 C++ / 109 Python / 105 web files**; changed-file pinned formatting,
Black, Python syntax and `git diff --check` pass. Full CTest/compiler lit, broad
corpus/native matrices, WPT and test262 were skipped. Evidence:
`/tmp/ctcompile-class-guards/`.

**Exact next:** compose Config's class proof with original H's callable holder
and the existing DOM provider. In the saved full r/M/F/H/W IR, H.getDataAttribute
is an arrow closure (`fn$18`) retaining lexical `this`, stored in H; it is outside the
class proof's direct-helper set, so the whole-module effect census refuses it.
Even proving its unused receiver leaves cross-function H identity/order and
separate class-versus-DOM intrinsic contracts in `ClassInitialization.cpp`,
`Contract.cpp` and `DOMSource.cpp`. Reuse the existing H/M/F public DOM helpers;
adding intrinsic names alone does not prove composition. Object.entries and
destructuring, RegExp/TypeError exits, remaining Config helpers, inheritance,
full H Unicode keys, retained callbacks and the application driver remain open.
General Number powers stay outside escape proof. Whole-Bootstrap/Button/Data
counts remain historical; no broad measurement was repeated.

## Closed scalar guards and original Bootstrap helpers, 2026-09-18 UTC

Started at clean **66174a17**. The interrupted declaration-borrow/bounded-power
thread was already closed in that commit and AGENT-SYNC's **08:54:45** journal;
no September 7 WIP remained unmerged. Continued the recorded original **r/H**
boundary, with independent escape implementation and caller-proof review.

**b680e2a0** joins scalar argument facts across the complete callers of private,
capture-free functions. The symbolic pass reuses its existing primitive folding
and branch selection, including after closure lifting exposes direct calls.
Calls and effects remain. Public/script entries, escaping or alternate-dispatch
callables, observed callee identities and arguments objects remain excluded;
CFG/SCF arguments stay unknown. Both optimization switches retain their controls.

**24832a56** pins Bootstrap's original **r** body in a closed global declaration
and a local closure. With **null** input and optimization enabled, both now
execute natively: **8 executions**, explicit/deduced C++, GCC/Clang, **a=7** in
Node/interpreter/native, with no ctbrowser or prototype metadata in output.
The immediate global-call form still lacks closed-caller authority. It remains
unchanged as a refusal control, alongside both unoptimized forms (**4 refusals**).
The original class-method r probe also remains a native refusal. A new fixture
includes original **r + M + F + all four H methods + W** without pruning bodies:
Node/interpreter return **a=7**, while preparation refuses an unknown call,
binding or reflective effect before the older named-H boundary.

**3f798821** separately proves exact negative-one base power parity for bounded
signed integer Number exponents. Saved lengths, original result identity and
CFG/SCF transport survive; coercion, nonfinite/fractional/out-of-domain exponents,
changing backedges and general powers remain unproved.

Focused devbox checks only:

- Six explicit targets: `ctjs-opt`, `ctjs-translate`,
  `ctcompile-test-native-reference`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`: **6 build actions**.
  Subsequent three-target native builds performed **9**, then **4** actions;
  final class rebuilds had no work.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 (0.92s)**. Dense/induction/
  structured rows: **585 / 528 / 265**; budget cutoffs: **22,816 / 36,412 / 18,508**.
  `escape-claims/{unit-power,bounded-power,power-identities}.test`: **3/3 (0.22s)**;
  each reports **26 sites / 11 sound / zero violations / 11 of 15 precision**.
- `Precomputation/{arguments,precompute,precompute-pdll}.mlir`: **3/3 (0.10s)**;
  `Optimization/default-optimizations.mlir`: **1/1 (0.08s)**.
  `Lowering/Objects/primitive-fields.mlir` passed (**152 native / 62 refusals**)
  in the initial two-case selection, whose class case failed its new H diagnostic
  assertion (**73.19s** total). Compiler sources did not change afterward.
- Final exact `Lowering/Objects/class-initialization.mlir`: **1/1 (149.98s)**,
  **120 source observations / 288 native executions / 240 unprepared / 149
  preparation refusals**, plus **16 ordinary executions / 20 refusals**, the
  **8 r executions / 4 refusals**, and **12 prepared native refusals**.
  The branching-helper preparation budget remains **419**. Final wrapper exit
  **0**; six native and four escape input hashes match the devbox.

Initial focused runs caught an invalid short-call test operand list and an extra
analysis round affecting the existing public-only budget check; both were fixed.
The H diagnostic expectation and reference-runner stderr-statistics assertion were
corrected without changing source bodies. The immediate r call's public-function
refusal was retained. SSH access rotated mid-session; authorized `start` and
`allow-ip` restored it. No browser source or runtime semantics changed.

Required `tools/format.sh --check` still fails with **26 existing diagnostics in
nine files**: eight files are unchanged, and the flagged lines in the modified
`Symbolic/Facts.cpp` are unchanged. Stable full formatting passes **916 C++ /
109 Python / 105 web files**; Black, Python syntax and `git diff --check` pass.
Full CTest/compiler lit, broad corpus/native matrices, WPT and test262 were
skipped. Evidence: `/tmp/ctcompile-r-guards/`.

**Exact next:** carry the same caller authority through the original class-method
r/prototype boundary, then compose Config's class proof with the existing DOM
proof for H. `ClassInitialization.cpp`, `Contract.cpp` and `DOMSource.cpp` still
separate closed-source class intrinsics from DOM-provider authority; H's global
callable holder also needs cross-function identity/order proof. H.getDataAttribute,
M and F already use public DOM helpers, so no platform copy is needed.
Object.entries/destructuring, RegExp/TypeError exits, inheritance, full H Unicode
keys, retained callbacks and the application driver remain open. Whole-Bootstrap,
Button and Data coverage counts remain historical.

## Recovered declaration borrows and bounded powers, 2026-09-18 UTC

Resumed the eleven dirty compiler paths at `c675f002`, first recorded in
AGENT-SYNC at **05:02:53 / 05:20:56** and most recently abandoned at **08:07:19**.
Both interrupted drafts are now committed; no September 7 WIP remains unmerged.

**`fe2f5b7f`** extends the existing object-borrow census to unique closed hoisted
declarations. Every symbolic caller participates, including forwarded arguments;
caller and callee use the existing object-parameter carrier. Saved String fields
retain their owned bytes across mutation. Mixed/short calls, escaping or replaced
callables, and alias deletion retain refusal controls. The unchanged
`local-helper-order` class specimen now executes natively and returns **122**.

**`7133398b`** proves exact bounded Number powers with zero or positive-one
bases, including negative finite integer exponents for one. Original result
identity preserves signed zero; saved lengths and CFG/SCF transport retain the
same invariance and budget checks. General powers and unknown, coercible or
out-of-domain operands remain unproved.

Focused devbox validation (workflow exit **0**, **933 input hashes** matched
locally/remotely before and after):

- Explicit targets: `ctjs-opt`, `ctjs-translate`,
  `ctcompile-test-native-reference`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`; **38 build actions**.
- Exact `ctcompile_escape_analysis_arrays` CTest: **1/1, 1.74s** total.
  Dense/induction/structured rows: **585 / 512 / 259**; corresponding budget
  cutoffs: **22,816 / 35,040 / 17,884**.
- `Analysis/Escape/escape-claims/{bounded-power,power-identities}.test`:
  **2/2, 0.20s**. Each oracle reports **26 sites / 11 sound / zero violations /
  11 of 15 precision**.
- `CTNative/Lowering/Objects/{primitive-fields,class-initialization}.mlir`:
  **2/2, 151.50s**. Primitive fields: **152 native executions / 62 refusals**.
  Classes: **119 source observations / 288 native executions / 238 unprepared /
  148 preparation refusals**, plus **16 ordinary executions / 20 refusals** and
  **12 prepared native refusals**. Both optimization policies, explicit/deduced
  C++ and GCC/Clang pass; successful output has no ctbrowser/prototype metadata.
  The branching-helper proof still first completes at **419 steps**.

The initial gate failed SSH reachability before building. Authorized
`server.sh start` and `allow-ip` restored access; the retry passed without source
changes. Required `tools/format.sh --check` retains **26 diagnostics in nine
HEAD-identical files**. Stable formatting passes **916 C++ / 109 Python / 105 web
files**; changed-file pinned formatting, Black, Python syntax and
`git diff --check` pass. Full CTest/compiler lit, broad corpus/native matrices,
WPT and test262 were skipped. No browser source or runtime semantics changed.
Evidence: `/tmp/ctcompile-native-close/`.

**Exact next:** original W-only Config still refuses global **r**; original r+W
still refuses global **H**, with **a=7** in Node/interpreter. The original r method
probe prepares but its helper retains **a property read on an object that is not
a closed-shape literal**. Hoisted object borrowing does not prove this nullable
guard/property flow. Compose Config's closed-source class proof with the existing
DOM proof for H through `HostContract/ClassInitialization.cpp`, `Contract.cpp`
and `DOMSource.cpp`; merely allowing another intrinsic is insufficient.
H.getDataAttribute/M/F already use public DOM C++, so no platform copy is needed.
Object.entries/destructuring, RegExp/TypeError exits, inheritance, full H Unicode
keys, retained callbacks and the application driver remain open. Whole-Bootstrap,
Button and Data coverage counts remain historical.

## Local helper proofs and power identities, 2026-09-18 UTC

Started from clean `a0f1b0f7`: its interrupted Error/getter work was already
closed in the latest commits and AGENT-SYNC's **04:00** entry. Continued the
recorded **`r` / `_mergeConfigObj`** boundary, without reopening that recovery.

**`27cb4729`** proves exact parameterized local helpers during class preparation.
Cross-function globals reuse the closed hoisting-declaration proof, charged once
per binding. Structured helper bodies retain the complete effect census; unused
arrow `this` captures and literal Number keys are admitted. Replacement, observed
receivers, dynamic keys and ambient effects in uncalled helpers still refuse.
Scalar, branching and argument-order cases compile to ordinary C++ calls.

The original **W-only Config** fixture remains unchanged: Node/interpreter return
**a=7**, and preparation names missing global **`r`**. A separate fixture adds
Bootstrap's original `r` declaration verbatim; it also returns **a=7**, and now
names missing global **`H`**. The original `r` method probe passes preparation but
still refuses native property reads without a closed object shape. The mutable
object-argument helper likewise prepares but lacks native parameter typing.
Both source bodies remain as explicit native refusal regressions.

Final exact `CTNative/Lowering/Objects/class-initialization.mlir` lit passes
**1/1 (148.54s): 119 source observations / 280 native executions / 238 unprepared /
148 preparation refusals**, plus **16 ordinary executions / 20 refusals** and
**14 prepared native refusals**. Both optimization policies, explicit/deduced C++,
GCC and Clang run. Successful native output has no ctbrowser/prototype metadata.
The new branching helper first completes at **419** proof steps.

**`725713cf`** preserves bounded signed Number power identities for exponents
zero and one, including original result identity, saved lengths and CFG/SCF
transport. General powers, unknown/coercible operands and out-of-domain Numbers
remain unproved. Exact `ctcompile_escape_analysis_arrays` CTest passes **1/1
(0.89s)**; `power-identities` / `signed-right-shifts` lit passes **2/2 (0.22s)**.
New oracle: **26 sites / 11 sound / zero violations / 11 of 15 precision**.
Arrays: **582 dense / 489 induction / 255 structured rows**, with **22,660 /
33,091 / 17,498** budget cutoffs.

Explicit devbox targets: `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-native-reference`, `ctcompile-test-escape-analysis-arrays`,
`ctcompile-test-escape-claims`, `ctcompile-test-type-oracle`. The combined retry
built **six actions**; the two subsequent three-target class rebuilds each built
**three actions**; the final three-target rebuild had **no work**. The first
combined build caught a test-only replacement-helper typo, corrected to the
adjacent `std::string::replace` pattern. Intermediate class runs stopped at the
mutable-object refusal (**7.71s**), missing Number-key proof (**11.77s**), and
original `r` native shape refusal (**11.92s**); sources were preserved. Final
workflow exits **0** and verifies **1,081 selected input hashes** locally/remotely.
Evidence: `/tmp/ctcompile-config-helpers/`, especially `class-final-gate.log` and
`gate-retry.log`.

Required pinned formatting retains **26 diagnostics in nine HEAD-identical
files**. Stable formatting passes **916 C++ / 109 Python / 105 web files**;
changed-file pinned formatting, Black, Python syntax and `git diff --check` pass.
Full CTest/compiler lit, broad corpus/native matrices, WPT and test262 were
skipped. No browser source or runtime semantics changed. Three parallel agents
were launched; service rate limits stopped them after the helper-source finding
and a 20-line escape draft, which root finished, gated and committed separately.

**Exact next:** prove the object argument/shape flow needed by original `r`, and
compose original **H** with Config through the existing DOM host contract.
Object.entries/destructuring, RegExp/TypeError exits, inheritance and DOM/default
composition follow. Full H Unicode keys, native throws, retained callbacks and
the application driver remain open. Whole-Bootstrap/Button/Data counts remain
historical; no broad measurement was repeated.

## Getter cleanup and signed right shifts, 2026-09-18 UTC

The interrupted Error/getter thread is complete in **`5927fdb6`**, with its
recovered measurements below. Two subsequent concerns landed separately:

- **`a427abbe`** removes unused throwing-getter chains in reverse dependency order.
  Every remaining numeric getter closure has a direct symbol call, so MLIR's
  symbol-use check preserves live callees and removes dead dependencies. A source
  probe first reproduced the native refusal from retained, unreachable Error
  getters. The new regression now compiles; Error construction in an uncalled
  ordinary method still refuses. Proof cutoffs are unchanged.
- **`bf363c05`** preserves bounded signed Number `Shr`/`UShr` snapshots using the
  existing bitwise transfer. Counts wrap and mask to five bits; signed right shift
  keeps its sign, while unsigned shift retains the unsigned result. Original
  identity, saved lengths and CFG/SCF transport survive. Negative magnitudes never
  become own-index facts; coercible, unknown, out-of-domain and unstable inputs
  remain unproved. This deletes the duplicate right-shift transfer.

Final focused devbox checks: exact class lit **1/1 (137.06s)** with **109 source
observations / 256 native executions / 218 unprepared / 139 preparation refusals**,
plus **16 ordinary executions / 20 refusals** and **ten preserved native throw
refusals**. Both optimization policies, explicit/deduced C++ and GCC/Clang run;
source/native observations agree, and output contains no ctbrowser or prototype
metadata. The throwing-chain proof still first completes at budget **293**.
Explicit `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference` build:
**five Ninja actions**, including the frozen escape core.

Exact `ctcompile_escape_analysis_arrays` CTest passes **1/1 (0.89s)**;
`signed-right-shifts`, `signed-bitwise`, `signed-bitnot` lit passes **3/3 (0.33s)**.
The new oracle reports **26 sites / 11 sound / zero violations / 11 of 15
precision**. Arrays measure **581 dense / 482 induction / 252 structured rows**,
with **22,609 / 32,619 / 17,251** budget cutoffs. The explicit array/claims/type-oracle/
translate build performed **three actions**; the array-only retry performed **two**.

The first array run failed **1/1 (0.92s)** on four assertions for one old control:
`-1 >>> -1` now proves length one. Its original body and retained-child check
remain; only its expected admission changed. Compiler code was unchanged for the
retry. The class and final escape workflows exit **0** and verify **1,287 / 1,291
input hashes** locally/remotely. The class manifest excluded the three escape
headers the child was still editing; its compiler core was frozen. Evidence:
`/tmp/ctcompile-error-closeout/{chain-gate,shifts-gate,shifts-final-gate}.log`.

Required final pinned formatting retains **26 diagnostics in nine unchanged
files**. Stable full formatting passes **916 C++ / 109 Python / 105 web files**;
the later Length expectation edit also passes both formatters. Changed-file checks,
Black and `git diff --check` pass. Full CTest/compiler lit, broad corpus/native
matrices, WPT and test262 were skipped. No browser source or runtime semantics
changed. Review and the independent escape implementation ran in parallel;
root serialized builds, reconciled results and committed each concern.

**Exact next:** complete original Config still returns **a=7** in Node/interpreter
and refuses **`r` in `_mergeConfigObj`**, after passing throwing-NAME preparation.
Prove the local helper's identity/arguments and compose H's existing host boundary;
do not waive the whole-source effect census. Object.entries/destructuring,
RegExp/type checking and TypeError exits, then inheritance and DOM/default
composition remain. Native throw completion, full H Unicode keys, retained
callbacks and the application driver are unfinished. Whole-Bootstrap/Button/Data
counts remain historical; separately owned browser/Unicode work is tracked in
AGENT-SYNC. No source draft from this session remains uncommitted.

## Recovered Error getters and signed bitwise snapshots, 2026-09-18 UTC

**`5927fdb6`** finishes the four dirty Error/getter paths found at `2904c366`:
AGENT-SYNC's **03:10:19** class-only gate and **03:21:38** recovery were
abandoned at **03:12:41 / 03:22:56**. The September 7 WIP is already integrated.

A separately declared standard `Error` identity permits only exact construction
with one literal string, whose result stays with its throw. Throwing local
getters and their transitive callers retain source functions and direct calls,
preserving abrupt completion and closure scope. Unused single getters can be
removed. Ambient effects, escaping payloads, coercible messages and replacement
of Error still refuse. This supplies no native exception representation.

Fresh focused devbox validation: explicit `ctjs-opt`, `ctjs-translate` and
`ctcompile-test-native-reference` build, **no Ninja work**; ten-source probe
**10 source / eight native / 20 unprepared / 11 preparation refusals**, plus
**six native throw refusals**; exact class lit **1/1 (133.69s)** with **107 source
observations / 248 native executions / 214 unprepared / 138 preparation refusals**,
plus **16 ordinary executions / 20 refusals** and **ten native throw refusals**.
Both optimization policies, explicit/deduced C++ and GCC/Clang run. The throwing
getter chain first completes at budget **293**. Workflow exits **0**; all **1,290
input hashes** match locally/remotely before documentation edits.

The first fresh probe failed because the replacement-Error control's interpreter
also prints `Error=9` before `a=7`. Its expected output now preserves that global;
original JavaScript is unchanged. The saved predecessor failures also corrected
anonymous getter-symbol matching and counting diagnostic text as throw operations.
The prior exact host CTest **1/1 (0.46s)** applies to the same C++ hashes; it was
not rerun. Evidence: `/tmp/ctcompile-error-closeout/` and
`/tmp/ctcompile-error-finalize/`.

Already landed **`2904c366`** preserves signed Number BitAnd/BitOr/BitXor/Shl
snapshots. Recovered focused evidence: array CTest **1/1 (0.88s)** and
`signed-bitwise` / `signed-bitnot` / `signed-subtraction` lit **3/3 (0.32s)**;
new oracle **26 sites / 11 sound / zero violations / 11 of 15 precision**.
Arrays measure **569 dense / 453 induction / 242 structured rows**, with
**22,315 / 31,116 / 16,283** budget cutoffs. These checks were not replayed.

Required pinned formatting retains **26 diagnostics in nine HEAD-identical files**;
stable formatting passes **916 C++ / 109 Python / 105 web files**, and changed-file
pinned formatting, Black and `git diff --check` pass. Full CTest/compiler lit,
broad corpus/native matrices, WPT and test262 were skipped. No browser source
or runtime semantics changed.

**Exact next:** complete original Config still returns **a=7** in Node/interpreter.
Preparation now gets past `NAME` and refuses the ambient **`r` load in
`_mergeConfigObj`**. Its local helper identity/arguments and H calls need a
composable source/host proof; Object.entries/destructuring, RegExp/type checking,
TypeError exits, inheritance and DOM/default composition follow. Standalone native
throw completion, full H Unicode keys, retained callbacks and the application
driver remain open. Whole-Bootstrap/Button/Data measurements remain historical.
The review also identified conservative retention of unused throwing-getter
chains; a focused follow-up is in progress, alongside signed right-shift snapshots.

## Recovered literal throws and signed BitNot, 2026-09-18 UTC

Resumed the seven dirty ctcompile paths at `b71d8034` from the **02:14:59 /
02:21:33 AGENT-SYNC drafts**, abandoned at **02:18:10 / 02:23:31**. Finished
both before taking new implementation work. September 7 WIP is already an
ancestor. The ctjs checkout matches the merged `d2664e9` gitlink.

**`552db1c7`** lets proved local methods retain outer CFG exits and literal
primitive throws during class setup preparation. The complete effect and
receiver census still checks every body; object/parameter throws and ambient
calls in uncalled arms refuse. Constructors, setup and getters stay linear.
Both throw fixtures preserve their original switch/throw exits; the getter
fixture removes its exact `this.constructor.Default` reads. Native completion
lowering still refuses these methods: preparation supplies no throwing-call,
iterator or exception-payload authority.

**`3d0af951`** preserves exact signed Number BitNot snapshots through unsigned
ToInt32 wrap, read-time length snapshots and CFG/SCF transport. Negative
magnitudes never become own-index facts. Coercible inputs, out-of-domain
Numbers, repeated producers and changing latches remain unproved.

Focused devbox validation: explicit `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-native-reference`, `ctcompile-test-escape-analysis-arrays`,
`ctcompile-test-escape-claims` and `ctcompile-test-type-oracle` build passed in
**231 dependency actions** after the browser merge. Exact
`ctcompile_escape_analysis_arrays` CTest **1/1 (0.88s)** and escape lit
`signed-bitnot`, `signed-subtraction`, `signed-unary` **3/3 (0.32s)** pass.
The BitNot oracle measures **26 sites / 11 sound / zero violations / 11 of 15
precision**. Arrays cover **569 dense / 384 induction / 222 structured rows**,
with **22,159 / 27,521 / 14,271** respective budget cutoffs.

The corrected exact `CTNative/Lowering/Objects/class-initialization.mlir` case
passes **1/1 (135.10s): 98 source observations / 240 native executions /
196 unprepared / 128 preparation refusals**, plus **16 ordinary executions /
20 refusals** and **four prepared throwing-method native refusals**. Both
optimization policies, explicit/deduced C++, GCC and Clang run. Literal throw
preparation first completes at budget **652**. Its explicit three-target rebuild
had no work. The final class workflow exits **0** and verifies all **1,289 input
hashes** locally/remotely before documentation edits.

The first class run failed **1/1 (36.59s)** because the interrupted test confused
an unused `"constructor"` key literal with a property read. Inspection confirmed
both getter reads were removed. The corrected assertion checks those operations;
the original JavaScript is unchanged. Only the class case was rerun afterward.

Required `tools/format.sh --check` retains **26 diagnostics in nine unchanged
files**; stable formatting passes **916 C++ / 109 Python / 105 web files**.
Changed-file pinned formatting, Black and `git diff --check` pass. Full CTest,
full compiler lit, broad corpus/native matrices, WPT and test262 were skipped.
No browser source or runtime semantics changed. Evidence:
`/tmp/ctcompile-throw-finalize/`.

**Exact next:** the complete original Config (`W`) still returns **a=7** in
Node/interpreter. Its preparation refusal now reaches **static getter body is
not a closed expression**: `NAME` constructs and throws `Error`. The current
class manifest declares only the class helper, so Error identity/effects and
throwing getter expansion need a separate proof. Original Object.entries /
destructuring, RegExp/type checking and TypeError exits, then inheritance and
DOM/default composition remain. Standalone literal-method throw completion also
remains unlowered. Full H Unicode keys, retained callbacks and the application
driver are unfinished. The separately owned Unicode extraction remains tracked
in AGENT-SYNC. Whole-Bootstrap/Button/Data counts remain historical.

## Local constructor getter reads, 2026-09-18 UTC

**`f5795916`** follows the recovered **`c0b82039` / `55e13932`** drafts below.
Constant getter reads through `this.constructor` or an exact local instance now
reuse the existing static-getter proof and expansion. The constructor identity
may feed only proved local getter reads and inert roots. Every read preserves
its own fresh allocation, including getter dependencies. Reads in normalized
method dispatch follow their private clones. No constructor/prototype value or
Script/VM/GC dependency is emitted.

Instance reads require a primitive constructor return: returning a replacement
object changes which constructor `new` exposes. The new source regression returns
**a=1** in Node/interpreter and now refuses preparation with that diagnostic.
Constructor writes, writes through the constructor, identity escape and inherited
classes remain refused, including effects in uncalled methods. Constructors and
getter bodies remain linear; this adds no iterator or throwing-call authority.

Final focused devbox gate: explicit `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-native-reference` build passes; exact
`CTNative/Lowering/Objects/class-initialization.mlir` **1/1 (127.94s)**.
It measures **94 source observations / 240 native executions / 188 unprepared /
122 preparation refusals**, plus **16 ordinary executions / 20 refusals**,
with both optimization policies, explicit/deduced C++, GCC and Clang. Getter
reads inside dispatch first complete at budget **1,362**. All **1,290 input
hashes** match locally/remotely; workflow exits **0**. The three-target build
performed **223 Ninja actions** after restoring the shared devbox source from
another worktree; this was a dependency rebuild, not a broad test run.

The first getter draft passed the same focused case **1/1 (131.18s)** with
**93 source observations / 240 native / 186 unprepared / 121 preparation
refusals**, after three build actions. Review then added the replacement-object
guard. Its first probe hit an unrelated conditional-wrapper refusal; the linear
comparison isolated the issue and confirmed that the pre-fix draft incorrectly
accepted preparation. Only the affected class case was rerun after the guard.

Required pinned formatting retains **26 diagnostics in nine unchanged files**;
stable formatting passes **912 C++ / 108 Python / 105 web files**, and the final
changed-C++ pinned check, Black and `git diff --check` pass. Full CTest/lit, broad
corpus/native matrices, WPT and test262 were skipped. No browser source or runtime
semantics changed in this task. Evidence: `/tmp/ctcompile-receiver-defaults/`,
including both gate logs/manifests, the probe and inspected emitted C++.

**Exact next:** the complete original Config (`W`) still returns **a=7** in
Node/interpreter and refuses complete capture-free source functions:
`_typeCheckConfig` retains three outer blocks. Its Object.entries/destructuring
iterator, RegExp/type checking and throw exits, plus throwing `NAME`, need their
own proof. Inherited receivers and DOM/default composition follow. Full H Unicode
key normalization, retained callbacks and the application driver remain open.
The disjoint **`codex-unicode-core` / `ecca5b66`** extraction is being integrated
by its own session; consult AGENT-SYNC for its status. It does not yet prove
Unicode casing or normalized dataset keys. Bootstrap **19/574 / 0 of 47 globals**,
Button **4/86 / 22 observations** and Data **7/7** remain historical measurements.

## Recovered method counters and signed subtraction, 2026-09-18 UTC

Resumed the seven dirty ctcompile paths at `6d6dcfbf` from the
**01:35:20 / 01:38:50 AGENT-SYNC drafts**, abandoned at **01:38:57**. Finished
both before new implementation. September 7 WIP is already an ancestor.

**`55e13932`** admits static numeric counters in proved ordinary class methods.
Increment/decrement loops retain break/continue/return dispatch; native admission
still requires Number operands. The whole-source census still rejects an ambient
call in an uncalled counter method. **`c0b82039`** preserves exact negative-left
Number subtraction snapshots, including bounded signed cancellation, source-length
mutation and CFG/SCF transport. Negative magnitudes never become own-index facts;
coercion, unknown inputs, overflow and unstable latches remain refused.

Focused devbox validation: explicit `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-native-reference`, `ctcompile-test-escape-analysis-arrays`,
`ctcompile-test-escape-claims` and `ctcompile-test-type-oracle` build passed in
**eight Ninja actions**. Exact array CTest **1/1 (0.87s)**; exact escape lit
`signed-subtraction`, `negative-add`, `sub-snapshot` **3/3 (0.32s)**; exact class
lit **1/1 (117.62s)**. Class: **86 source observations / 216 native executions /
172 unprepared / 113 preparation refusals**, plus **16 ordinary executions /
20 refusals**; increment dispatch first complete budget **1,210**. Subtraction
oracle: **26 sites / 11 sound / zero violations / 11 of 15 precision**. Arrays:
**569 dense / 359 induction / 216 structured rows**, with **22,159 / 26,292 /
13,661** budget cutoffs. Gate exit **0**, all **1,290 input hashes** verified
locally and remotely before documentation edits.

Required pinned formatting retains **26 diagnostics in nine HEAD-identical files**;
stable formatting passes **912 C++ / 108 Python / 105 web files**, and
`git diff --check` passes. Full CTest/lit, broad corpus/native matrices, WPT and
test262 were skipped. No browser source or runtime semantics changed. Review
agents hit service limits; root completed the reviews locally. Evidence:
`/tmp/ctcompile-config-recovery/next-gate.log` and `next.sha256`.

**Next:** exact local `this.constructor.Default` / `DefaultType` reads can reuse
the static-getter proof. Reads inside normalized methods need clone remapping.
The complete original Config regression still returns **a=7** in Node/interpreter
and refuses complete capture-free source functions (`_typeCheckConfig` has three
outer blocks). Iterator/throw proof, throwing `NAME`, inheritance and DOM/default
composition remain, along with full H Unicode keys, retained callbacks and the
application driver. Bootstrap **19/574 / 0 of 47 globals**, Button **4/86 /
22 observations** and Data **7/7** remain historical measurements.

## Recovered method dispatch continuation, 2026-09-18 UTC

Resumed `2167f4d2`, the sole dirty `class_initialization.py`, and the
**01:19:28 / 01:22:57 AGENT-SYNC checkpoint and interruption**. September 7 WIP
is already an ancestor. The interrupted thread had landed **`ab646056`** (exact
negative Number sums), **`63a021c1`** (integer/index poison and carried while
backedges), and **`2167f4d2`** (proved local method exit dispatch).

Method break/continue/return dispatch is normalized on budgeted private clones
only after complete source effects and receiver uses pass. Existing exception
normalizers handle switches and unused/all-poison results. Throwing calls and
iterators gain no authority. Integer poison retains its control type; an inert
empty EmitC marker keeps upstream SCF conversion from losing an empty-after
while backedge. Neither change adds a native runtime dependency.

Recovered focused evidence in `/tmp/ctcompile-dispatch-focused/`: array CTest
**1/1 (0.86s)**; escape lit `negative-add`, `add-cancellation`, `signed-division`
**3/3 (0.34s)**; negative-Add oracle **20 sites / eight sound / zero violations /
eight of 12 precision**. Class lit passed with **82 source observations / 200
native executions / 164 unprepared / 107 preparation refusals**, plus **16 ordinary
constructor executions / 20 refusals**, and dispatch's first complete budget
**1,196**. The combined class/scalar run had one scalar pipeline failure; after
adding the missing index conversion pass, the exact scalar replay passed **1/1
(0.51s)**. The minimized empty-after loop also passed; its pre-fix run timed out.
These are recovered measurements, not suites rerun by this continuation.

Fresh recovery gate: explicit `ctjs-opt`, `ctjs-translate` and
`ctcompile-test-native-reference` build had no work; exact class lit passes
**1/1 (108.52s): 83 source observations / 200 native executions / 166 unprepared /
108 preparation refusals**, plus **16 ordinary executions / 20 refusals**.
Required pinned formatting retains **26 diagnostics in nine HEAD-identical files**;
stable formatting passes **912 C++ / 108 Python / 105 web files**, and changed-file
Black plus `git diff --check` pass. Full CTest/lit, broad corpus/native matrices,
WPT and test262 were skipped. No browser source or runtime semantics changed.
Fresh evidence: `/tmp/ctcompile-config-recovery/class-gate.log` (exit 0).

The full original Bootstrap Config (`W`) source is now a permanent refusal
regression. It retains all getters and methods, imports every function, and
returns **a=7** in Node/interpreter; preparation still requires complete
capture-free source functions because `_typeCheckConfig` has three outer blocks.
The next native prerequisite is the static numeric increment used by its
iterator counter, then exact local `this.constructor.Default` / `DefaultType` reads; the full class additionally needs iterator/exception proof,
throwing `NAME`, inheritance and DOM/default composition. Full H's Unicode key
seam, retained callbacks and the application driver remain open. Bootstrap
**19/574 / 0 of 47 globals**, Button **4/86 / 22 observations** and Data **7/7**
remain historical measurements.

## Structured class methods and signed division, 2026-09-18 UTC

Continued clean `b965aa43` and the **23:35:19 AGENT-SYNC completion handoff**.
The previous interrupted threads were landed; `codex-wip-20260907` exists but is
already an ancestor of `ctcompile-v1`. This session resumed the recorded Config
method boundary. Agents split escape work, fixtures and proof review; after service
limits, root completed the drafts. Two agents later reviewed the frozen diffs
without findings. No browser source or runtime semantics changed.

**`9105a656`** admits structured branches and loops only inside proved ordinary
local class methods. Both complete recursive operation censuses and all receiver
uses remain checked. Every function still needs one outer block; constructors,
getter expansion and class setup retain their linear-body restrictions. Unknown
regions, nested ambient calls and method replacement refuse before mutation.
Existing lowering emits the methods without new runtime helpers. The original
mutual-method recursion fixture is now an execution positive with its body intact.

**`b21653b9`** preserves exact bounded signed Number division and remainder
snapshots. Div requires an integral quotient and a nonzero divisor; Mod follows
the dividend's sign. Negative magnitudes remain separate from index facts, and
zero retains its original result identity. Coercion, unknown/out-of-domain inputs,
fractional quotients and unproved loop invariance still refuse.

Focused devbox validation:

- Explicit `ctjs-opt`, `ctjs-translate`, `ctcompile-test-escape-analysis-arrays`,
  `ctcompile-test-escape-claims` and `ctcompile-test-type-oracle` build: **nine Ninja
  actions**. The class build additionally requested `ctcompile-test-native-reference`
  and had no work. The corrected array test rebuilt in **two actions**.
- Exact `ctcompile_escape_analysis_arrays` CTest: **1/1 (0.84s)**.
- Exact escape lit `signed-division`, `signed-product` and `add-cancellation`:
  **3/3 (0.32s)**. New signed-division oracle: **26 sites / 11 sound / zero
  violations / 11 of 15 precision (73.3%)**.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`: **1/1 (103.87s)**,
  **78 Node/interpreter observations / 192 native executions / 156 unprepared /
  100 preparation refusals**, both policies, layouts and GCC/Clang. Ordinary
  constructor controls add **16 executions / 20 refusals**. The nested-loop
  source's first complete work budget is **278**.

The first array run failed **24 assertions across four historical controls**:
`-1 / -1` preserves length one, `-1 % -1` clears it, and zero divided/remaindered
by a negative nonzero Number stays zero. Their bodies and live-mutation checks
remain; only the obsolete refusal expectations changed. No production fix or
fixture deletion was needed. Escape lit did not run after that initial failure.

Both final workflows exit **0**. Each verified **1,821 local/remote input hashes**
before documentation edits. Stable formatting passed **912 C++ / 108 Python /
105 web files**; the later length-expectation edit also passed the changed-C++
check. Required `tools/format.sh --check` retains **26 pinned-formatter diagnostics
in nine files byte-identical to HEAD**, not a pinned-format pass. Black and
`git diff --check` pass. Full CTest, full lit, broad corpus/native matrices, WPT
and test262 were **not run by this task**. The initial shared-lock wait was for
the independent browser workflow; it is not compiler validation.

**Exact next:** the unchanged complete original Config (`W`) defaults-only probe
still returns **a=7** in Node and the interpreter, imports every function, and
refuses **class initialization requires complete capture-free source functions**.
`_typeCheckConfig$9` retains three outer blocks after SCF lifting. Full Config
needs the exceptional/iterator method proof, throwing `NAME`, inherited
`this.constructor` and DOM/default composition. Full H still needs the public
UTF-16/Unicode case seam and normalized-key proof. Retained callbacks and the
application driver remain open. Bootstrap **19/574 / 0 of 47 globals**, Button
**4/86 / 22 observations**, and Data **7/7** remain historical measurements.
Evidence: `/tmp/ctcompile-structured-focused/`, including the preserved initial
failure, final gate logs, both source manifests and original full-Config probe.

## Fresh Config defaults and signed Number products, 2026-09-17 UTC

Continued `1af71cf8` and the **17:03:14 AGENT-SYNC handoff**. The earlier
prefix-assignment recovery was complete; the frozen signed-product draft in the
**17:10:37 journal** was reviewed, corrected and committed before the next native
change. Independent agents handled escape proofs, Config fixtures and deletion
review; root reconciled and gated their changes.

**`fe56eae8`** preserves exact bounded signed Number multiplication snapshots.
Negative magnitudes stay separate from nonnegative index facts; the product must
fit the existing 32-bit domain. The original result remains the value identity,
including signed zero. Source-length mutation, both operand orders, negative times
negative, zero trips, retained/released children and CFG/SCF transport are covered.
Coercible Strings/BigInts, unknown operands, unstable latches and overflow refuse.

**`f1450d60`** expands local static getters returning fresh empty objects, including
the exact Bootstrap `Default` and `DefaultType` bodies. Each original read gets its
own allocation, also through getter dependencies. After all reads are expanded,
the pass removes only the proved getter definitions and their closure/descriptor
setup. A charged census checks symbol references in module attributes and the body;
remaining getter references and unresolved targets refuse before mutation. Numeric
function indices remain stable. This does not admit general escaping object
returns, getter stores/nonempty literals, inherited receivers or the full Config
class. The existing complete host identity, callable-use and budget checks remain.

Focused validation on the devbox:

- Explicit targets `ctjs-opt`, `ctjs-translate`, `ctcompile-test-native-reference`,
  `ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
  `ctcompile-test-type-oracle`: eight initial Ninja actions; the corrected array
  test rebuilt in two actions. The final class-only three-target build used four
  actions, including pass declaration generation.
- Exact `ctcompile_escape_analysis_arrays` CTest: **1/1 (0.84s)**. It covers **567
  dense / 296 induction / 196 structured rows**, with **21,961 / 20,847 / 11,765**
  respective budget cutoffs.
- Exact escape lit cases `signed-product`, `add-cancellation`, `sub-snapshot` and
  `signed-unary`: **4/4**. The new oracle measures **26 sites / 11 sound / zero
  violations / 11 of 15 precision (73.3%)**.
- Exact `CTNative/Lowering/Objects/class-initialization.mlir`: **1/1 (92.67s)**,
  **72 source observations / 168 native executions / 144 unprepared refusals /
  93 preparation refusals**. Ordinary constructed-method controls add **16 native
  executions / 20 refusals**. Both optimization policies, explicit/deduced output,
  GCC/Clang, independent allocations and exact getter removal pass. The new
  dependency source's first complete budget is **267**.

The initial array failure was six assertions for one old refusal: `-1 * 0` now
correctly clears array length as `-0`; its original body remains as a positive.
The first class run exposed unused getter definitions that still failed ordinary
escaping-object admission; the complete deletion proof resolves that failure.
The earlier five-case lit run was **four passes / one failure (57.00s)**; only the
class case was replayed after its fix. No failure was hidden by dropping a fixture.

Final class workflow exits **0**, with all **1,820 input hashes** matching locally
and remotely before documentation edits. Stable formatting passes **912 C++ /
108 Python / 105 web files**. Required `tools/format.sh --check` still reports
**26 pinned-formatter diagnostics in nine files byte-identical to HEAD**; this is
not a pinned-format pass. Black, changed-C++ formatting and `git diff --check` pass.
Full CTest, full lit, broad corpus/native matrices, WPT and test262 were **not run**,
following the user's repeated focused-check instruction. No browser source changed.

**Exact next:** one fresh probe retains the complete original Config (`W`) class
body in a defaults-only wrapper. Node and the interpreter both return **a=7**;
import skips no functions, but preparation refuses **class initialization requires
complete capture-free source functions**. All imported functions are capture-free;
`_typeCheckConfig$9` still has three blocks after SCF lifting. Full Config needs a
proof for its structured/exceptional methods, throwing `NAME`, inherited
`this.constructor` and default/DOM composition. Do not merely relax that census.
Full H still needs the public UTF-16/Unicode case seam and normalized-key proof;
its last admission result is from the prior session, not remeasured here. Retained
callbacks and the application driver remain open. Signed division/remainder
snapshots are an independent next escape item. Bootstrap **19/574 / 0 of 47 globals**,
Button **4/86 / 22 observations**, and Data **7/7** remain historical measurements.
Evidence: `/tmp/ctcompile-config-focused/` (logs, source manifests, passing generated
C++, array log, failure evidence and the complete Config probe).

## Filtered prefix assignments and Number cancellation, 2026-09-17 UTC

Resumed unmerged **`codex-dynamic-20260917`** from the **16:17:07 AGENT-SYNC
handoff** at `1d363a7b`; shared main started clean at `d2cc2b8c`. September 7 WIP
was absent. Locked merge **`08a812a6`** lands `73ae525d` (direct unique snapshot
assignments), `88e9f626` (callback census after frontend cell ordering), and
`1d363a7b` (fixture detach write boundaries). This completed the interrupted thread
before new implementation. Browser `0337cd15` is included in the tested oracle.

The predecessor's **377-target build, host-contract CTest 1/1 (0.45s), and focused
native-dom/native-dom-assignment/callback-startup lit 3/3 (249.23s)** passed.
All **1,818 input hashes** were reverified locally/remotely before landing.
Its inherited full CTest replay was deliberately stopped under the new user policy;
workflow exit **255** records cancellation, not a full-suite pass. No duplicate
full replay was started. Assignment recovery retained the 120-second sanitizer
compile timeout and all lifetime checks. The callback graph is **64 targets / 225
calls**: the added `D$8 -> fn$9` edge follows the frontend's cell-before-closure
order; six startup-reachable functions are unchanged. DOM fixture setup detaches
now drain their own logged writes, preserving entry no-mutation assertions.

**`58cb0b3a`** proves `n.replace(/^bs/, '')` as a fresh-result assignment key
when the original pure filter's true result implies an input beginning with `bs`.
The callback proof handles Boolean truth and structured joins; subsequent filters
preserve the subset. Removing the guaranteed prefix is injective. Existing single
traversal, sole writer, allocation placement and final-own-data observation checks
still govern the possible `__proto__` setter. Generated C++ reuses the existing
String replacement and ordered owning JSON assignment helpers. This capability is
separate from dataset membership: `t.dataset[n]` is proved, while
`t.dataset[n.replace(/^bs/, '')]` remains refused. Unfiltered two-preimage keys,
weaker predicates, repeated replacement/writes and incomplete budgets refuse.

**`08d849c2`** extends the existing bounded Number Add transfer to cancellation
of one negative and one nonnegative exact operand, in either order, when the
result is nonnegative. Original producer identity, read-time snapshots, latch
invariance and final-update bounds remain. Zero can initialize an index but cannot
prove loop progress. String/BigInt coercion, unknown values and out-of-domain
operands do not gain Number facts.

The eight requested build targets passed: `ctjs-opt`, `ctjs-translate`,
`ctcompile-tool`, `ctcompile-test-native-reference`, `ctcompile-test-host-contract`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims`, and
`ctcompile-test-type-oracle` (nine incremental Ninja actions). Exact CTests
`ctcompile_host_contract` and `ctcompile_escape_analysis_arrays` pass **2/2 (1.34s)**.
Lit `Analysis/Escape/escape-claims/{add-cancellation,sub-snapshot,signed-unary}.test`
passes **3/3**. The new oracle measures **20 sites / eight sound / zero violations /
eight of 12 precision (66.7%)**. Array checks cover **567 dense / 276 induction /
188 structured rows**, with **21,943 / 19,149 / 11,148** respective budget cutoffs.

The initial four-case lit selection was **three passes / one failure (249.77s)**:
the assignment driver's combined sanitizer compilation exceeded its existing
120-second timeout after six ordinary binaries had passed. Adding only
`-fno-inline-functions` compiled the exact 444 KB fixture in **14.29s**; all
sanitized assertions and output matched ordinary Clang. The test retains `-O1`,
all ASan/UBSan and lifetime options, every fixture, and the 120-second timeout.
The focused replay `CTNative/Browser/native-dom-assignment.test` then passes
**1/1 (181.34s): nine sources / 210 Node-VM observations plus 210 Node accessor
traces / eight GCC-Clang binaries / 196 refusals**, both providers/policies/layouts
and lifetime checks. The incremental build before this replay had no work.

The final workflow exits **0**, and all **1,819 input hashes** match locally and
on the devbox before documentation edits. Stable formatting passes **912 C++ /
108 Python / 105 web files**. Required `tools/format.sh --check` still reports
**26 pinned-formatter diagnostics in nine unchanged files**; that is a recorded
baseline, not a pinned-format pass. Black and `git diff --check` pass.

The existing whole-Bootstrap **19/574 native / 0 of 47 globals**, Button **4/86 /
22 lifecycle observations**, and Data session **7/7** are historical measurements,
not rerun results. Full CTest, full compiler lit, broad native/corpus matrices,
WPT and test262 were skipped per the focused validation policy. No browser source
or runtime semantics changed. Independent agents reviewed both proof paths and
extended execution tests; root recovered the escape draft after its agent hit a
service limit.

**Exact next:** one original full-H admission probe (`ctbrowser-dom-v1`, optimization
off) still refuses **DOM property read lacks a proved receiver and supported
member**. The prefix-result execution above admits two functions, including its
filter, in every tested provider/policy mode. No full-Bootstrap gain is claimed.
Full H still needs the browser-owned public UTF-16 indexing/slicing and Unicode
case seam for `charAt(0).toLowerCase() + slice(1)`. No suffix ASCII premise follows
from the filter. Lowercasing can collide ordinary result keys; the existing ordered
helper handles collisions, but any admission must preserve the source proof for
the sole possible prototype-key write. Coordinate through AGENT-SYNC while Claude
owns ctbrowser. Do not replace Unicode with the runtime's current byte/ASCII String
operations. Config/inheritance/defaults, retained callbacks and the application
driver remain open; signed multiplication snapshots are an independent future
escape step. Evidence: `/tmp/ctcompile-native-recovery/` and
`/tmp/ctcompile-prefix-focused/`. Older checkpoints below are historical.

## Ordered native result assignments and negative Sub snapshots, 2026-09-17 UTC

Continued clean **`1bbaff44`** and the **12:40:58 AGENT-SYNC handoff** after
checking both commit histories and unmerged branches. The interrupted guard/helper
thread was complete; this session resumed its documented fresh-result assignment
boundary. Three agents split source preparation, execution tests and escape review;
root recovered the escape proposal after one agent hit its service limit.

**`e8d5aae0`** preserves receiver-only fresh-object writes through structured
branches and loops. **`1576568d`** proves and lowers constant String-key assignments
except `__proto__` on a direct fresh object. Values are owning JSON trees, Strings,
optional Strings, null, Boolean or Number. Every mutation must finish before a
return, join, spread or value assignment can snapshot its target; observations
inside its mutation loop refuse. The shared member-update helper preserves numeric
key order, ordinary insertion order and last-write values for assignments and
spreads. Two former JSON refusals retain their exact source and earlier DOM reads
as execution positives. No browser implementation or Script/VM/GC dependency was
added; the public `ctbrowser::json_value` owns the result.

**`eec9fcb9`** preserves exact negative Number subtraction snapshots. String
coercion supplies no such fact. Existing latch invariance, sign, index and final
update checks remain. The new source oracle measures **20 sites / eight sound /
zero violations / eight of 12 precision (66.7%)**; the three existing signed
stride/unary source oracles pass. CFG/SCF tests add 27 rows and budget cutoffs.

The corrected **371-target build / 3/3 focused CTests (1.31s)** pass. The final
assignment driver passes **1/1 lit (55.69s): six sources / 111 Node-VM observations /
eight GCC-Clang binaries / 88 refusals**, both providers/policies/layouts and
ASan/UBSan lifetime checks. It executes original M values inside repeated fixed-key
writes and checks results after document/session destruction. The other seven
focused lit tests pass: dataset **28 sources / 112 observations / eight binaries /
432 refusals**, JSON **21 / 584 / eight / 248**, Strings **783 observations / eight
binaries / 1,056 source refusals** plus provenance controls, and four escape drivers.

The new driver retains **111 additional Node accessor traces**. The VM currently
invokes enumerable dataset getters during Object.keys, so its comparison uses the
same original entry source and values with plain data properties in both Node and
VM; dataset-getter and DOM-method order remain checked. Native observation first
checks raw result-key order and duplicates, then applies Node JSON enumeration to
nested parsed trees: public Core deliberately retains parse order. Ordinary nested
key order and scalar types still match exactly. The first compile typo and three
fixture failures are retained as evidence; they did not require runtime changes.

The complete frozen gate passes **310/310 CTests (2087.43s) / 260/260 lit
(1822.86s)** with no skips; workflow exit status is **0**. All **1,796 input
hashes** match locally and on the devbox. Fresh full Bootstrap remains **19/574
native / 0 of 47 globals** in both policy reports, with no skipped or pruned
functions. Button remains **4/86 / 22 agreeing lifecycle observations**. The
original DOM Data session passes its seven-function admission and lifetime
assertions; this is separate from the historical unadapted CommonJS census.
Stable formatting passes **890 C++ / 108 Python / 105 web files**. The required
pinned formatter retains exactly **nine unchanged files / 26 diagnostics**.
WPT/test262 corpus measurements were not rerun.

**Exact next:** 44 fresh admission probes cover 11 sources in both providers and
policies. A fixed result write now admits one function; original M inside a fixed-key
dataset result loop admits two. Guarded value/M loops remain admitted. Full original
H advances from its source-local identity refusal to **DOM property read lacks a
proved receiver and supported member**, also seen in the isolated Unicode
`charAt(0).toLowerCase() + slice(1)` expression. Dynamic and `__proto__` result writes
reach the explicit final DOM assignment diagnostic.

Do not generalize `__proto__` skipping: repeated writes after null, a parsed
prototype's own data slot, or a previous spread can create an own property. A
parallel Node-only review found a narrower possible proof for original H: exactly
one `bs__proto__` snapshot key, one traversal/write, original M values and final
own-data-only observations. Ordinary ASCII/Unicode normalized keys can collide.
This proposal is **not implemented or native-validated**. The current runtime still
has byte-indexed charAt and ASCII case conversion; the earlier É/İ VM discrepancy
was not remeasured. Coordinate a public, non-VM Unicode seam with the browser work.

Full H, Config/inheritance/defaults, retained callbacks and the application driver
remain unfinished. Claude's round-six/seven runtime changes are outside this frozen
oracle and need fresh differential validation after integration. Evidence:
`/tmp/ctcompile-assignment/`, including frozen hashes, failed and passing gates,
generated C++, 44 boundary probes and `prototype-next.md` with Node counterexamples.
Older sections below are historical checkpoints.

## Validated element guards and nested helper calls, 2026-09-17 UTC

Resumed seven dirty ctcompile files on **`d9f89a05`**, identified in the
**10:52:21 recovery / 10:55:26 abandoned-loop AGENT-SYNC journal**. Both histories
and unmerged branches were checked; September 7 WIP was already integrated. Two
editing agents hit service limits after reviewing the drafts; root recovered them.
A third agent completed independent proof and corrected-order reviews.

**`0a186321`** proves helper/capture initialization before enclosing SCF branches
and loops, preserving actual arguments and both URI/JSON exception continuations.
Invoke-boundary crossings, late/mutable captures, callable escapes and incomplete
budgets still refuse. Nested clone accounting now visits every operation. The former
branch-call refusal retains its exact original source as a differential positive.

**`fe650e1c`** folds truthiness and Not only from validated nonnullable element
parameters, on the private fingerprinted candidate. Existing helper/cell expansion
runs first, so original source CellGet operations and helper formals resolve through
that proof; the guard itself grants no facts to cells, nullable reads, joins or loop
state. Iterator preparation follows, then complete DOM reproof. Guard budgets fail
without mutation. Original guarded value/count/alias loops and the former `!element`
refusal retain their source bodies and earlier DOM observations. No browser or
runtime semantics changed; emitted C++ remains ordinary owning containers and public
DOM calls without Script/VM/GC symbols.

The first **62-target build / 2/2 CTests** passed, but three lit drivers exposed the
pass-order error and two newly admitted refusal expectations. Those were corrected,
not removed. The corrected **316-target build / 2/2 CTests (0.43s) / 3/3 lit
(147.24s)** pass. Dataset: **26 sources / 97 Node-VM source-double observations /
eight GCC-Clang binaries / 432 refusals**, HTML/SVG and lifetime sanitization.
JSON: **19 sources / 488 observations / eight binaries / 256 refusals** with lifetime
sanitization. Strings: **783 observations / eight binaries / 1,056 source refusals**
plus the existing provenance/depth/budget controls.

The complete frozen gate passes **310/310 CTests (2076.81s) / 258/258 lit
(1815.84s)**, with no skips. All **1,793 input hashes** match locally and on the
devbox. Fresh full Bootstrap remains **19/574 native / 0 of 47 globals** in both
policy reports, with no skipped or pruned functions. Button remains **4/86 / 22
agreeing Node-VM lifecycle observations**. The separate original DOM Data session
regression also passes its seven-function admission and lifetime assertions;
unadapted CommonJS Data still reports **0/7**, a different census.

After that full run, **`ff594bb6`** adds original M inside guarded dataset-value
and nullable-attribute loops to the existing execution driver. All 26 earlier
sources, fixtures, values and refusals are preserved. The updated **1/1 dataset lit
(52.75s)** passes **28 sources / 112 Node-VM source-double observations / eight
GCC-Clang binaries / 432 refusals**, both providers/policies/layouts, HTML/SVG,
repeated calls, invalid handles and ASan/UBSan lifetime checks. Boolean, Number,
JSON, URI-failure and JSON-failure paths run inside the loops. These new observations
check `typeof` results; the existing JSON driver checks full returned values.
The all-build needed no compilation work; all **1,793 updated input hashes** were
verified locally/remotely before documentation edits. The full-suite result above
precedes this test-only extension; the updated driver was then rerun separately.

Stable formatting passes **890 C++ / 107 Python / 105 web files**. The required
pinned formatter reproduces the existing **nine unchanged files / 26 diagnostics**.
WPT/test262 corpus measurements were not rerun.

**Exact next:** 36 fresh admission probes cover nine sources in both providers and
both optimization policies. The isolated element guard admits one function; guarded
values and original M calls inside value, attribute and guarded-value loops each
admit two. The two guarded M-loop consumers now have the permanent execution checks
above. Full original H still refuses **DOM helper branch contains an unproved local
identity**.
Fresh static result assignment refuses object escape/identity; dynamic loop assignment
refuses nonlocal/unordered uses. Extend the fresh-object/source-use proof and complete
DOM SetProperty proof together, preserving assignment order, collisions and inherited
`__proto__` setter behavior; JSON spread semantics are insufficient.

Unicode `charAt(0).toLowerCase() + slice(1)` still refuses the unsupported member.
The previously measured É/İ oracle gap was not remeasured here. Full H,
Config/inheritance/defaults, retained callbacks and the application driver remain
unfinished. Claude's separately measured round-six runtime changes are outside this
frozen tree and require fresh differential validation when integrated. Evidence:
`/tmp/ctcompile-guard-finish/`. Older sections below are historical checkpoints.

## Present dataset values and signed unary snapshots, 2026-09-17 UTC

Resumed **12 dirty ctcompile files on `6e8fb994`**, identified in the **02:27:52
recovery / 02:29:20 abandoned-loop AGENT-SYNC journal**. The integrated-runtime
recovery was already committed; September 7 WIP is already an ancestor. Three agents
split dataset tests, escape recovery and independent review. Root recovered the two
editing agents after service limits, reviewed their drafts and completed the gates.

**`9a367648`** compiles original same-element `t.dataset[n]` reads through public
`ctbrowser::dataset_value`. Immutable snapshots record their exact element and
enumeration epoch; confined filtering preserves provenance, and the existing guarded
vector index proof supplies a present own member. A fresh dataset lookup cannot renew
an old key. Changed/joined/carried Strings, cross-element reads, mutations, unknown
effects and incomplete budgets receive no membership evidence. Native C++ returns
owning Strings; no browser implementation, Script/VM/GC dependency or runtime
semantics changed.

**`8207a051`** preserves held bounded signed Number facts through unary Plus and
Neg, including a second Neg restoring a positive stride or own index. Exact initial
values, invariant backedges and bounded final updates remain required. Coercible
String/BigInt/unknown inputs do not acquire Number facts. CFG/SCF and complete source
oracles cover retained children, zero trips, overshoot, source mutation and budget
exhaustion.

The **393-target build / 3/3 focused CTests (0.83s) / 5/5 lit (42.59s)** pass.
Dataset tests cover **22 sources / 80 Node-VM source-double observations / eight
GCC-Clang binaries / 392 refusals**, HTML/SVG and ASan/UBSan lifetime checks, including
returned Strings after document/session destruction and mutation between calls.
The new signed-unary oracle observes **20 sites / eight sound / zero violations /
eight of 12 precision (66.7%)**. The complete historical fixture remains **895 sites /
40 sound / zero violations / 40 of 172 precision (23.3%)**, with its snapshot unchanged.
Independent static dataset proof/emission review found no actionable defect.

The complete frozen gate passes **310/310 CTests (1950.15s) / 258/258 lit
(1701.18s)**, wrapper **0**, with no skips. All **1,793 input hashes** match locally
and on the devbox before documentation edits. This includes the corrected Annex B
String witness from the preceding recovery. Array suites now cover **567 dense /
234 induction / 162 structured rows**, with **21,935 / 15,806 / 9,424** conservative
budget cutoffs.

Fresh full Bootstrap remains **19/574 native / 0 of 47 globals**, Data **7/7**, and
Button **4/86 / 22 agreeing Node-VM lifecycle observations**. All four corpus reports
are byte-identical to the preceding gate. The original dataset-value loop now admits
**two native functions in all four provider/policy modes**, alongside the filter,
count and prefix loops. Original M retains **24 nine-register blocks / handler ^bb12**.
Full H still refuses **DOM helper branch contains an unproved local identity**.

Stable formatting passes **890 C++ / 107 Python / 105 web files**. The required
pinned formatter reproduces exactly the same **nine unchanged HEAD files / 26
diagnostics**. WPT/test262 corpus measurements were not rerun. Evidence, source
manifests, generated code, static review and boundary probes are preserved under
`/tmp/ctcompile-values-finalize/`.

**Exact next, measured in all four provider/policy modes:** prove the original
`if (!t) return {};` for an already validated, nonnullable element input, and fold
that exact guard on the private source candidate before iterator preparation. The
isolated guard currently refuses the unsupported `ctjs.unary`; the guarded value
loop refuses **DOM iteration requires one top-level source iterator**. An empty
result object alone already admits one function.

An independent composition thread is loop-local original M: both dataset-value and
getAttribute arguments refuse **DOM helper call has unsupported arity or source
order**. Extend the helper-call dominance/source-order proof at the actual nested
call while preserving capture initialization and original exception continuations.
Fresh result writes remain separate: static assignment refuses object escape/identity,
and dynamic assignment refuses nonlocal/unordered object uses. Ordered collisions and
inherited `__proto__` setter behavior need an assignment proof beyond JSON spread.
The 28 isolated boundary probes are admission measurements, not full-H execution.

The unchanged Unicode witness still disagrees for `bsÉtage` and `bsİtem`; ASCII,
empty, emoji and supplementary Deseret agree. The original expression lowercases its
first UTF-16 code unit. Keep this oracle issue coordinated with Claude; the filter
does not establish an ASCII suffix.

Full H, Unicode first-code-unit key normalization, loop-local M composition,
ordered dynamic result assignment/collision semantics, Config/inheritance/defaults,
retained callbacks and the application driver remain unfinished. Older sections below
are historical checkpoints.

## Integrated-runtime recovery complete, 2026-09-17 UTC

Resumed clean `228d80d1` from this handoff and the interrupted **2026-09-16
22:29 AGENT-SYNC** gate. The native source had already landed: `c8b9856d`
(original anchored prefix), `bf872db3` (branch-local confined filter scheduling),
and `c7d1b1a2` (held bounded Neg stride). Claude reworded their old SHAs without
changing the trees. September 7 WIP is already an ancestor; no branch was replayed.

The integrated browser's Annex B fix gives a block function a local binding.
The former duplicate-export fixture therefore has one global publication and now
correctly admits. Its exact original source is preserved as a native/Node/VM positive;
an explicit repeated `var` export retains the refusal, with one-versus-two raw
publication assertions. An isolated before/after probe confirms the scope distinction.
No compiler or browser semantics changed during recovery.

The fresh devbox build passed **817 targets**. The full gate passed **309/310
CTests (1978.86s)**: all **309 non-lit tests**, and **256/257 lit (1720.91s)**;
only that stale expectation failed. After the test-only correction, **3/3 focused
CTests (143.02s)** passed, including the complete Strings driver **1/1 lit (142.57s)**.
Thus every test has passed across the full and corrected focused runs; this is not
a single all-green full invocation. Both sets of **1,792 hashes** match locally and
on the devbox, and only `native_dom_strings.py` differs between them. Logs, failed
and corrected manifests, generated-code probes and reports are preserved under
`/tmp/ctcompile-runtime-resume/`; the corrected workflow exits **0**.

Stable clang-format 23.1.1 passes **890 C++ / 107 Python / 105 web files**. The
required pinned formatter reproduces the same **nine unchanged HEAD files / 26
diagnostics**, matching the predecessor baseline. Independent bounded static reviews
found no actionable prefix/filter or held-Neg proof defect. WPT/test262 corpus
measurements were not rerun.

Fresh full Bootstrap remains **19/574 native / 0 of 47 globals**, without skips or
pruning. Data remains **7/7**, Button **4/86** with **22 agreeing Node/VM lifecycle
observations**; all four reports are byte-identical to the previous frozen gate.
Original M retains **24 nine-register blocks / handler ^bb12**. In all four
provider/policy modes, the original filter, count loop and anchored-prefix loop each
compile **two native functions**. Full H still refuses **DOM helper branch contains
an unproved local identity**; Unicode key normalization and live dataset values each
refuse **DOM property read lacks a proved receiver and supported member**.

**Exact next:** present own `t.dataset[n]` String reads through public
`ctbrowser::dataset_value`, proving the exact element, immutable snapshot member and
unchanged enumeration epoch. Fresh dataset lookup cannot renew a stale key. The
existing iterator completion normalizer already keeps the key and consumer together;
generic String joins must not gain membership. The independent escape continuation
is preserving held signed Number snapshots through unary Plus and a second Neg.
Full H also needs its fresh result-object/valid-element guard, Unicode first-code-unit
normalization, loop-local M calls and dynamic assignment/collision semantics.
Node/VM still disagree for `bsÉtage` and `bsİtem`; this oracle gap is journaled for
Claude. Config/inheritance/defaults, retained callbacks and the application driver
remain open. Older sections below are historical checkpoints.

## Original snapshot iteration and scalar loop completion, 2026-09-16 UTC

Resumed clean **9b02a344**, its HANDOFF and the **19:22:54 AGENT-SYNC** journal:
the unfinished thread was the original filtered `for...of`, with source regressions
preserved at `/tmp/ctcompile-dom-iterator-tests/native_dom_dataset.loop-draft.py`.
Both histories and unmerged branches were checked; September 7 WIP was already landed
and older lens/JSON branches were explicitly superseded. Three agents split source
normalization, regressions and escape review. Root recovered two service-limited
agents' work; the source agent completed independent proof and next-boundary reviews.

**a607afc6** preserves source loop condition/yield tuples and exact register
correspondence through completion dispatch. An inactive slot may disappear only with
a constant predicate, an unused destination and a same-type live state replacement;
all source operations are accounted for. Original Array iterator and open/next/close
identities are explicit host premises. Iterator preparation reuses the complete
dataset/filter prefix proof on a private clone, then reproves the complete entry.
Scalar Number/String/Boolean loop state is supported. Each indexed String read needs
the exact immutable vector, a zero-start/unit-increment index and dominance by that
vector's `index < length` true arm. Vector writes, loop DOM mutations, escaped helpers,
unknown calls and insufficient budgets still refuse without published evidence.

**d9112248** connects those proofs to native String inference and owning `vector.at`
extraction. Original count, ordered-key and saved-snapshot loops execute unchanged;
generated C++ uses ordinary vectors, strings and loops, with no Script/VM/GC symbols.
The driver preserves all previous source witnesses and adds iterator/prototype,
mutation, escape and missing-premise controls. Independent prepared-loop tests cover
incorrect starts, updates, guards, vectors, branch placement, forwarded slots and every
insufficient proof budget. No browser implementation or runtime semantics changed.

Corrected focused build, **3/3 CTests (0.89s) / 2/2 lit (34.66s) PASS**. Dataset:
**15 sources / 45 Node-VM source-double observations / eight GCC-Clang binaries /
228 refusals**, HTML/SVG and lifetime sanitization. The first build's const MLIR
handle API error was fixed; failed logs and hashes are retained. The escape review
found no defect in the landed negative-Sub proof; no escape extension was started.
Historical escape results remain **895 observed sites / 40 sound / zero violations /
40 of 172 precision (23.3%)**, with all **1,123 snapshot rows unchanged**.

Complete build and **305/305 CTests (2261.10s) / 256/256 lit (1977.19s) PASS**,
wrapper **0**, with no skips. All **1,774 frozen input hashes** match locally and on
the devbox before documentation edits. Fresh full Bootstrap remains **19/574 native /
0 of 47 globals**, Data **7/7**, and Button **4/86** with **22 Node-VM lifecycle
observations**. Both Bootstrap policy reports and Data/Button reports are byte-identical
to the previous gate; no full-bundle admission gain is claimed. Original M retains
**24 nine-register blocks / handler ^bb12**. The full WPT/test262 corpus measurement
was not rerun. Stable formatting passes **879 C++ / 104 Python / 105 web**; the required
pinned formatter reproduces the unchanged **nine files / 26 diagnostics** baseline.

**Exact next, measured in all four provider/policy modes:** the original count loop
now compiles **two native functions**, as does the filter. Full original
`H.getDataAttributes` now refuses **DOM helper branch contains an unproved local
identity**. Its filter closure is created inside the continuation of `if (!t)`, so
branch-local callable scheduling (or proof of that exact valid-element guard) remains
before full H admission. The isolated original `n.replace(/^bs/, "")` loop and the
following `charAt(0).toLowerCase() + slice(1)` loop both refuse **DOM property read
lacks a proved receiver and supported member**. The smallest independent String step
is exact anchored ASCII-prefix removal through ordinary String operations, with
original String/RegExp/factory identities and complete effect/use proof.

The next-key witness measures six Node/VM cases: ASCII, empty, emoji and supplementary
Deseret agree; `bsÉtage` gives Node `étage` versus VM `Étage`, and `bsİtem` gives Node
`i` + U+0307 + `tem` versus VM `İtem`. Current VM charAt/slice use byte positions and
lowercase is ASCII-only. The original expression selects one UTF-16 code unit in JS;
lowercasing the first full Unicode code point would also mishandle the Deseret case.
Record/coordinate this oracle boundary before claiming general key normalization;
the filter alone does not prove an ASCII suffix. No runtime expectations changed.

Live values can reuse public `ctbrowser::dataset_value`, but indexed-key provenance,
same-element presence and mutation epochs must be proved; missing own properties
have Undefined/prototype semantics. Loop-local M calls and dynamic result assignment
remain separate proofs: collisions preserve assignment order and `__proto__` uses its
inherited setter. Full Config/inheritance/defaults, retained callbacks and the
application driver remain open. The read-only `next-key-review.md` and executable
`next-probe.py` under the evidence directory describe these seams.

Claude's **6edb7421** browser work is still separate from this gate: Annex B/catch
bytecode, new globals and platform changes require fresh differential validation when
integrated. Evidence, source hashes, generated C++, reviews and fresh boundary probes:
`/tmp/ctcompile-iteration-resume/`. The previous sections are historical checkpoints.

## Owning snapshot length and negative Number strides, 2026-09-16 UTC

Continued the clean **26ba2f8e** handoff and its exact original dataset iteration
boundary. Both histories and unmerged branches were checked; September 7 WIP was already
an ancestor and old lens branches were explicitly superseded in the journal. Three
agents split source preparation, regressions and escape analysis. Two hit service
limits; root recovered their drafts. A further read-only review found no proof defect;
root removed a duplicate new oracle before the source freeze.

**0f3fbe91** proves `.length` on an owning original dataset-key or filtered String
snapshot. Evidence identifies each exact property read; existing `vec_length` lowering
returns its Number without a browser/VM lookup. Filter identities, callback confinement,
snapshot immutability and complete-budget proof remain required. The original `for...of`
source has not been rewritten into `.length`. The next-loop count/order/saved-snapshot
regression draft is preserved separately at
`/tmp/ctcompile-dom-iterator-tests/native_dom_dataset.loop-draft.py`.

**e1bbd9e4** proves increasing dynamic Sub latches with bounded original negative Number
strides, including the frontend's single literal negation and an unchanged carried
stride. It reuses the existing exact initialization/final-update proof; commuted
subtraction, String/BigInt/unknown and changing strides remain refused. CFG/SCF tests
retain returned children and cover zero trips/overshoot.

Corrected focused build, **3/3 CTests (0.91s)** and **3/3 lit (27.32s)** pass, including
**12 dataset sources / 33 Node-VM source-double observations / eight GCC-Clang binaries
/ 150 refusals**, HTML/SVG and lifetime sanitization. The first array CTest identified
three old UnknownIndex assertions. The unchanged negative-offset sources now prove an
exact absent index/growing length, so still refuse at MissingElement. All three
assertions were corrected and the array CTest rerun successfully.

New escape oracle: **15 observed sites / six sound / zero violations / six of eight
precision (75%)**. Historical oracle remains **895 sites / 40 sound / zero violations /
40 of 172 precision (23.3%)**. The existing **1,123-row** snapshot still matches. Array
suites cover **567 dense / 174 induction / 123 structured rows**, with **21,928 / 12,506
/ 7,047** conservative budget cutoffs.

Complete build and **305/305 CTests (2262.84s) / 256/256 lit (1983.71s) PASS**, wrapper
**0**, with no skips. All **1,772 frozen input hashes** match locally and on the devbox
before these documentation changes. Fresh full Bootstrap remains **19/574 native / 0 of
47 globals**, Data **7/7**, and Button **4/86**, including all **22 Node-VM lifecycle
observations**. Both Bootstrap policy reports, Data, Button and next-boundary reports
are byte-identical to the prior gate. Original M preserves **24 nine-register blocks /
handler ^bb12**. The full WPT/test262 corpus measurement was not rerun. Evidence lives
in `/tmp/ctcompile-dataset-iteration/`. Stable formatting passes **877 C++ / 104 Python
/ 105 web**; the pinned formatter's **nine files / 26 diagnostics** independently match
unchanged HEAD files. No browser implementation or runtime semantics changed.

Exact next (original IR is now local at `/tmp/ctcompile-dataset-filter-next/`): the
count-only filtered `for...of` still refuses **DOM helper completion observes an
inactive value**; full original H refuses **DOM helper completion requires acyclic
structured source**, each in all four provider/policy modes. The loop needs proofs of
iterator-helper identity, original Array iterator behavior, scalar loop state and
inactive completion slots. Its before region contains an index switch; after-region
arguments forward count/index/completion slots. The completion copier currently returns
one function-result value and cannot model that loop terminal tuple merely by accepting
While. Read-only details are in
`/tmp/ctcompile-dataset-iteration/iteration-boundary.md`. Native snapshot length is now
available for its index path; String extraction still needs an exact integral in-bounds
index proof before using existing `vec_at`. Then key normalization, live dataset reads,
M composition and dynamic result writes remain, as do full Config/inheritance, retained
callbacks and the application driver. No full-bundle admission gain is claimed by this
prerequisite.

## Original dataset filter and moved-VM recovery, 2026-09-16 UTC

Resumed the interrupted dataset filter and runtime integration from **17 dirty
ctcompile paths** and the **16:07:28 abandoned-loop AGENT-SYNC entry**. Both commit
histories and pending branches were checked; September 7 work was already landed.
Three delegated recovery tasks hit service limits before editing; root completed
them. A later agent reviewed the next iteration boundary and found no concrete defects
in the filter proof.
Claude's integrated runtime is the differential oracle; no browser source changed.

**32155832** compiles Bootstrap's original
`Object.keys(t.dataset).filter(t => t.startsWith("bs") && !t.startsWith("bsConfig"))`.
The complete proof requires original Object/keys, Array/filter/default species and
String/startsWith identities, a confined capture-free callback, a Boolean result,
and literal ASCII prefixes. The callback becomes an ordinary native function;
`std::copy_if` produces an owning String vector. Source callbacks, effects,
identity uses, budgets and duplicate function indices remain checked. No VM, GC,
Script symbol, copied browser implementation or external dependency was added.

**bf7a56d5** fixes native and host-prefix String concatenation after the VM's
surrogate normalization changed. Both ordinary and optional String operands reuse
public `ctbrowser::join_surrogates`; standalone programs need public Core headers
and link no browser library. The unchanged `split41` witness now agrees with the VM;
new optional-String and provider snapshot-key checks cover both compiler paths.
The MLIR Analysis object target now declares C++23 for that public header.

**4bf39afe** proves the frontend's exact static-getter `__home` assignments before
removing unobservable setup. Wrong/repeated homes, identity observation and missing
proof budgets still refuse. Class checks cover **69 source observations / 152 native
executions / 138 unprepared and 84 preparation refusals**, plus constructed-method
controls. **9659f0bf** updates preserved Button and sloppy-undefined source witnesses:
Node and VM now agree on **22 original Button lifecycle observations**, inherited
statics and the two inheritance isolates. Explicit raw-IR writes to the fixed
undefined binding remain refused; an empty conditional still lacks a global-owner
proof. DOM checks cover **19 entries / 42 refusal controls**.

**56a39f38** preserves delete sources at their new opaque host-call boundary,
including zero-fact controls, while retaining positive provider-object coverage.
Provider suites pass **39 object / 29 diagnostic cases**. **0e0fa2d7** refreshes the
measured escape snapshot: **1,123 rows**, changed bytecode offsets/program hash and
**79 Stored-to-Passed reasons**, with every observation and confinement result
unchanged. Historical oracle: **895 sites / 35 unclaimed / 40 sound / zero violations /
zero partial or pending / 40 of 172 precision (23.3%)**.
Previously landed **618f5775** commuted Add induction is included in this gate:
**15/15 observed sites / 6 of 8 precision / zero violations**. Current array suites
cover **567 dense / 151 induction / 109 structured rows**, with **21,925 / 11,227 /
6,312** conservative budget cutoffs.

Focused build, **3/3 CTests (0.87s) / 13/13 lit (279.05s) PASS**. Dataset tests cover
**11 sources / 29 Node-VM source-double observations / eight GCC-Clang binaries /
146 refusals**, both providers/policies/layouts, HTML/SVG and lifetime sanitization.
Complete build and **305/305 CTests (2238.51s) / 255/255 lit (1954.77s) PASS**,
wrapper **0**; no tests skipped.
All **1,771 frozen input hashes** match local and devbox sources before these docs.
Stable formatter 23.1.1 passes **877 C++ / 104 Python / 105 web**; the required pinned
formatter's **nine files / 26 diagnostics** independently reproduce from unchanged
HEAD files. Evidence: `/tmp/ctcompile-filter-1624/` (full/focused logs and exits,
source manifest, generated C++, measured reports, escape snapshots and next probes).
Earlier failed focused runs are archived; their four failures were repaired before
the green run. The full WPT/test262 corpus measurement was not rerun.

Fresh full Bootstrap remains **19/574 native**, with **0/47 globals resolved**
(the imported global denominator was previously 43), without skips or pruning.
DOM Data remains **7/7** and Button **4/86**. The separate dataset filter compiles
**two native functions** in all four provider/policy modes; original M/H and Config
typeof/spread each compile one. No full-bundle admission gain is claimed.

**Exact next native boundary:** the count-only `for...of` over the original filtered key
snapshot refuses **DOM helper completion observes an inactive value** in all four
provider/policy modes. Full original `H.getDataAttributes` refuses **DOM helper
completion requires acyclic structured source** in all four modes.
Prove the original for-of helper/iterator identities over the owning dense String
vector, preserve snapshot order/lifetime and scalar loop-carried values, then reuse
existing vector length/index helpers and SCF lowering. Current DOM preparation and
entry proof reject loop regions and block arguments; allowing a loop alone does not
prove its open/next/close calls. Then address original `replace(/^bs/, "")`, dynamic
key normalization, live `dataset[n]` reads with Undefined/prototype semantics, M
composition and dynamic result writes (`__proto__` assignment is a setter, unlike
spread). Matching/live F keys, r(e), inherited defaults/initialization, retained
callbacks and the application driver remain open. Original M still has **24
nine-register blocks / handler ^bb12** before preparation.

Earlier entries below are historical checkpoints.

## Dataset key snapshots and dynamic Add induction, 2026-09-16 UTC

Continued **c5682b0f** and the **10:24:07 AGENT-SYNC** next-boundary journal.
The starting tree was clean; both histories and unmerged branches were checked.
Earlier interrupted work was already landed or explicitly superseded. Three agents
were delegated independent work; service limits stopped their drafts before edits.
Root implemented and gated both concerns. Two agents later reviewed the proofs;
one found a dominance flaw in a negative test, corrected before the final gate.

**2dbd73b6** compiles `Object.keys(element.dataset)` into an owning
`std::vector<std::string>` through public `ctbrowser::dataset_entries`. The explicit
`dataset_parameters` subset contracts HTML/SVG inputs, validated before source
effects; original Object/keys identity and receiver are mandatory. Attribute order,
numeric keys, current public DOM namespace/uppercase exclusions, saved snapshots
after mutation and post-document ownership pass. A saved dataset alias cannot
cross a DOM mutation before enumeration. No Script/VM/GC, DOMStringMap implementation or new dependency
is emitted. MathML namespace URIs remain in Shell, outside this contract.

**3d80df96** accepts dynamic Add latches (`i += 1`) under the same exact bounded
Number start, positive stride, final-update and retention proof as static Add.
String/BigInt/unknown operands, zero stride and unsupported updates remain refused.
The new original-source oracle reports **15/15 sites, 6/8 precision, zero violations**;
the historical fixture remains **40/172 precision**, zero violations. Array suites
cover **567 dense / 131 induction / 103 structured rows**, with **21,925 / 10,077 /
5,979** conservative budget cutoffs.

Focused **2/2 proof/runtime CTests (0.33s), 2/2 lit (81.59s) PASS**.
Dataset: **five Node/VM source-double observations, eight GCC/Clang binaries,
160 native observations, lifetime sanitizer and 50 refusals**, both providers,
policies and layouts, HTML and SVG. JSON remains **18 sources / 486 observations /
eight binaries / 260 refusals**. Every insufficient dataset proof budget withholds
all evidence. The source-double uses a DOMStringMap-shaped `ownKeys` Proxy.

**Real-browser witness:** Chromium preserves `[bsZ,10,2,01,"",__proto__]` and
adds `later` when enumerating a saved dataset after mutation. Existing Shell/VM
sorts that prefix to `[2,10,bsZ,01,"",__proto__]` and omits `later`. Both differences
were measured with Playwright and the existing ctdrive, and journaled for Claude.
A further Chromium witness includes namespaced `data-hidden` and `p:data-other`
attributes in HTML/SVG dataset keys. Existing public DOM `dataset_entries` and
`dataset_value` skip namespaced attributes; Shell delegates to that same core.
Native retains this shared platform limitation. Its fixture expectations must move
with a future core fix; namespace-witness.html/log/json records the discrepancy.
No browser/runtime source or expectations changed; full WPT/test262 was not rerun.

The first full run found one stale refusal in `array_borrow.py`: bounded `i += 2`
now passes the same induction proof. **beb5c1db** executes that source with
Node/VM/native observations and copy controls; a String stride remains refused.
The repaired borrow suite passes **80 native executions / 24 copy controls /
38 refusals**, with **1/1 array CTest (0.84s) and 2/2 lit (56.21s)**. Initial
**287/288 CTests (2325.81s), 253/254 lit (2049.19s), wrapper 8** are archived as
`full-failed-1.*`; compiler code was unchanged. The complete suite was rerun.

Complete build, **288/288 CTests (2307.84s), 254/254 lit
(2039.47s) PASS**, wrapper **0**. All **1,725 frozen input hashes**
match local and devbox sources before this documentation update. Stable formatter
23.1.1 passes **832 C++ / 104 Python / 105 web**; required pinned formatter retains
the byte-identical **nine-file / 26-diagnostic** baseline. Evidence:
`/tmp/ctcompile-dataset-keys/` (logs/exits, manifests, measured.json, generated C++,
next source probes and browser-witness.json).

Fresh full Bootstrap stays **19/574 native / 0 of 43 globals**, without skipping or
pruning. DOM Data stays **7/7**; Button **4/86**, with 22 Node observations and its
known VM inheritance failure. Those reports and all **1,123 historical escape rows**
are byte-identical to the prior gate. No full-bundle improvement is claimed.

**Exact next native boundary:** the original
`Object.keys(t.dataset).filter(t => t.startsWith("bs") && !t.startsWith("bsConfig"))`
refuses all four provider/policy modes at **DOM helper callable escapes or its call
shape is unsupported**. Prove the original `Array.prototype.filter` and
`String.prototype.startsWith` identities, default Array species, and callback use
over the owning key snapshot without exporting the callback.
Full `H.getDataAttributes` still first refuses **DOM helper completion requires
acyclic structured source**, even with Object and dataset_parameters supplied.
Then prove original iteration, dynamic key normalization, live dataset value reads
(Undefined/prototype semantics), M composition and dynamic writes (`__proto__`
assignment is a setter, unlike spread). Original M retains **24 nine-register
blocks / handler ^bb12**. Matching/live F keys, r(e), inherited defaults and
initialization, retained callbacks and the application driver remain open.

Earlier entries below are historical checkpoints.

## Guarded Config spreads and recovered escape work, 2026-09-16 UTC

Resumed the interrupted 09:13/09:15 Config-spread and negated-guard drafts,
found as six dirty files plus `negated-guard.test` and the abandoned 09:15:56
AGENT-SYNC loop. Both histories/unmerged branches were checked; September 7 WIP
was already an ancestor. Three agents recovered regressions, checked proof
soundness and surveyed the next independent boundaries. No browser/VM source changed.

**fcd4a0c2** compiles original Config's guarded JSON spread, including two ordered
spreads. Preparation preserves fresh data objects and immutable branch cell reads;
writer locality is checked before source-order comparisons. The complete DOM proof
requires object-tagged JSON (null, arrays or objects), fresh direct targets and all
writes before any copying observation. Members, identity, captures and later/alias
mutation remain refused. Output owns `ctbrowser::json_value`, explicitly constructs
empty objects, and uses standard C++ for own-key order and overwrites. Numeric keys,
duplicate keys, `__proto__`, array indices, fallbacks and post-document lifetime pass.
Core JSON parser behavior, including nested object storage order, is unchanged.

**344b514d** proves equivalent `!(index >= length)` / `!(length <= index)` array
guards only for the existing bounded Number induction. Source order, retained
children, immutable length and complete-budget requirements survive. The new oracle
observes **15/15 sites, 6/8 precision, zero violations**. Array suites cover
**567 dense / 118 induction / 100 structured rows**, with **21,925 / 9,090 / 5,843**
conservative budget cutoffs. The source's negations remain in imported IR.

Focused **4/4 proof/runtime CTests (3.91s) / 4/4 lit (80.02s) PASS**. JSON covers
**18 sources / 486 Node-VM observations / eight GCC-Clang binaries / 260 refusals**,
both providers/policies/layouts and lifetime sanitization. Complete build and
**288/288 CTests (2305.41s) / 252/252 lit (2029.51s) PASS**,
wrapper **0**; all **1,721 frozen input hashes** match local and devbox sources
before docs. Stable formatter 23.1.1 passes **831 C++ / 103 Python / 105 web**;
required pinned formatter retains its unchanged **nine-file / 26-diagnostic** baseline.
Evidence: `/tmp/ctcompile-spread-resume/` (`measured.json`, full/focused logs and exits,
manifests, generated C++, source probes). Initial mixed-upload and emitter/preparation
failures are archived and superseded by the final frozen gate.

Full Bootstrap remains **19/574 native / 0 of 43 globals** without skips/pruning;
DOM Data **7/7**, Button **4/86** with 22 Node observations and its known VM
inheritance failure. These reports and all **1,123 historical escape rows** are
byte-identical to the previous integrated gate; precision remains **40/172**, zero
violations. No full-bundle gain is claimed. H, Config typeof and Config spread each
compile in all four modes; original M retains **24 nine-register blocks / handler
^bb12** before preparation.

**Exact next native boundary:** original `H.getDataAttributes(e)` refuses in all
four provider/policy combinations at `DOM helper completion requires acyclic
structured source`. Its loop, `Object.keys(t.dataset).filter(...)`, dynamic key
normalization and per-key `M(t.dataset[n])` reads remain intact. Public
`dom/dataset.hpp` already supplies owning `dataset_entries` and `dataset_value`;
no browser extraction is needed for those reads. Prove the original iteration and
Object identity, namespace eligibility, key snapshot versus live reads, missing-key
semantics and dynamic writes. Its `e["__proto__"] = value` assignment has setter
semantics, unlike spread's own-data definition. Full Config additionally needs
`r(e)`, inherited defaults/initialization, retained callbacks and the driver.
Matching/live F keys remain refused. The independent plan25 continuation is bounded
Number `i += 1` induction: the shared latch recognizer still requires
`BinaryStaticOp`, although dynamic Add already uses `boundedNumberSum`.

Earlier entries below are historical checkpoints.

## Config JSON tags and reversed array guards, 2026-09-16 UTC

Continued the exact Config `typeof` boundary recorded in **cb002682**, HANDOFF,
plan00 and the 07:09:54 AGENT-SYNC journal. The starting tree was clean; both
commit histories and unmerged branches confirmed the interrupted September 7
and JSON-chain work were already resolved. Three agents split native regressions,
proof review and an independent escape-analysis increment. Root recovered two
service-limited test drafts, reviewed them and ran the gates. No browser/VM source
was changed by this work.

**c127ba96** compiles `"object" == typeof H.getDataAttribute(element, "config")`
and the String tag itself. The complete DOM proof authorizes the observation;
generic JsonType and JSON member access remain refused. Emission reads the owning
`json_value.data` with standard `holds_alternative`, without a variant copy at
the observation. Null, arrays and objects all report `"object"`. The original M/H
source, nullable guard, lookup order and both failure snapshots remain intact.
Two initial emitter gate failures are archived; the final form reuses ordinary
deferred member emission before the conditional expression, with no printer change.

**42a2bd80** accepts the equivalent strict array guard `length > index` through
the shared CFG/SCF induction proof. It preserves source operand order, Number,
start/stride, array stability, retained children and complete-budget requirements.
Fifteen CFG/SCF cases and one original-source oracle were added. The new oracle
observes **9/9 sites, 3/5 precision and zero violations**; it keeps the returned
child escaping and the inclusive guard conservative. Array suites now cover
**567 dense / 102 induction / 90 structured rows**, with **21,925 / 8,211 / 5,414**
conservative budget cutoffs respectively.

Focused **3/3 proof CTests (3.88s) / 3/3 lit (55.27s) PASS**. JSON covers **15 sources /
342 Node-VM observations / 8 GCC-Clang binaries / 172 refusals**, both
providers/policies/layouts and post-document lifetime sanitization.

Claude integrated **28878c6c / 0b1e0911** during the first full gate, followed by
the **4c4f8e7b** documentation update. The audit centralizes plain C++ helpers in
`ctcompile/CTNative/Runtime/ctnative.hpp`, removes source-name provenance and
requires combined drivers to hoist `CTNATIVE_` defines with includes. JSON `typeof`
now keys on its carrier; the removed flag controlled include selection.
Independent integration review found no Script/VM/AOT dependency or ownership
change. The pre-audit build and 288/288 CTests (253 lit) passed, but the final
source check detected the concurrent merge and the wrapper exited **1**. Its
results in `/tmp/ctcompile-config-typeof/` are not a gate for the current tip.

Integrated build and **288/288 CTests (2252.04s) / 251/251 lit (1973.33s) PASS**, wrapper
exit **0**. All **1,720 frozen source/submodule hashes** match the devbox and integrated
**4c4f8e7b**; documentation changed afterward.
Stable formatter 23.1.1 passes **831 C++ / 103 Python / 105 web**. The required
pinned formatter retains the existing **nine-file / 26-diagnostic** baseline.
Evidence: `/tmp/ctcompile-config-integrated/` (`gate.log`, `gate.exit`, manifests,
`full-last-test.log`, `native-json.cpp`, `next-measured.json`, `measured.json`).

Fresh full Bootstrap remains **19/574 native / 0 of 43 globals**, both policies,
with no skips or pruning. DOM Data remains **7/7**, Button **4/86**, with 22 Node
observations and the unchanged VM inheritance failure. Their measured reports and
all **1,123 escape rows** are identical to `/tmp/ctcompile-m-gate/`; historical
escape precision remains **40/172**, with zero violations. No full-bundle gain
is claimed.

**Exact next native boundary:** adding Config's following spread
`{..."object" == typeof parsed ? parsed : {}}` refuses in all four modes at
`DOM helper branch contains an unproved local identity`. The branch-local empty
object reaches `DOMSource.cpp` before the final `copy_props` proof. Preparation
currently rejects the nested constructor; its later object census also assumes
every top-level constructor is a callable method holder and erases it. Preserve
proved data constructors through both stages, then require complete entry proof
of the spread and ownership. Null contributes no entries, arrays contribute indexed
entries, and the object tag alone grants no member proof. The existing escape
`CopyProps` certificate covers fresh fixed own-data objects, not runtime JSON keys
or enumeration order; generic native lowering has no `CopyPropsOp` case. Reuse
`carrier::json` ownership, explicitly constructing its object alternative for `{}`
(default `json_value{}` is null), and prove key order and overwrite behavior.
Spread is shallow: copying or moving an owning tree also needs a proof that surviving
aliases cannot distinguish it. Preserve numeric/duplicate/`__proto__` keys, array
indices without `length`, fallback inputs and post-document ownership in source tests.
Then compose dataset/config merging through the existing public `dom/dataset.hpp`.
Original M still has **24 nine-register blocks / handler ^bb12** before preparation.
Matching/live F keys, inherited static/object-valued defaults, initialization,
retained callbacks and the application driver remain open. Pending browser/runtime
branches remain Claude-owned and require fresh differential validation when landed.
The entries below are historical checkpoints.

## Original Bootstrap attribute normalization, 2026-09-16 UTC

Resumed **codex-m-finish's four dirty M-prefix files**, identified through the
04:52 AGENT-SYNC journal, the abandoned 04:53:42 loop and **e7c14d27**. Started
at **5e3d3b86**, with the test-registry audit merged. Three agents split tests,
declaration metadata and review; root recovered service-limited drafts and
completed integration. The old September 7 and JSON-chain threads were already
resolved. No other branch or browser/runtime source was changed.

**32032893** compiles the byte-pinned original M and
`H.getDataAttribute(element, "config")`. Number truthiness reuses the existing
NaN/zero-aware conversion. Boolean, Number, null and optional-String alternatives
join the existing owning `ctbrowser::json_value`; optional bytes are copied only
inside the selected present arm. Original lookup order, nullable guards and both
failure snapshots survive. Emitted C++ calls public Core/DOM APIs and uses ordinary
RAII, without Script/VM/GC dependencies. The API is documented in **a1bc655d**.
F's constant-key no-match proof already existed and is reused; do not reimplement
that slice.

**40f1f3ef** independently imports classic-script `program::hoisted_vars` into
fingerprinted module metadata. Shared host validation rejects malformed names and
absent-binding contradictions. Prefix identity proof recognizes a declaration
without an Undefined store, while its value, callability and intrinsic authority
remain unknown. Regressions remove only bare declaration stores to simulate
Claude's pending runtime correction. This supplies the compiler prerequisite for
removing the temporary compatibility restoration in **7ad52ce2**; the runtime
change remains Claude-owned.

Focused **2/2 proof CTests (3.73s) / 4/4 lit cases (33.94s) PASS**. JSON now covers
**12 sources / 219 Node-VM observations / 8 GCC-Clang binaries / 144 refusals**,
both providers/policies/layouts and a post-document lifetime sanitizer. Independent
reviews of the proof and generated C++ found no material issue. Complete
**375-step build / 288/288 CTests (2339.80s) / 252/252 lit
(2056.50s) PASS**, wrapper exit **0**. All **1,742 frozen source/submodule
inputs** match the devbox and
implementation commit **32032893**; documentation changed afterward. Formatting
23.1.1 passes **844 C++ / 105 Python / 106 web**; the required pinned formatter
retains the existing **nine-file / 26-diagnostic** baseline. The audit moved many
CTest registrations into lit, so totals differ from the previous 605/177 registry.
Evidence: `/tmp/ctcompile-m-gate/`, including full logs/exits, manifests,
`native-json.cpp`, `next-measured.json` and the final measured report.

Fresh full Bootstrap remains **19/574 native / 0 of 43 globals**, both policies,
with no skips or pruning. DOM Data remains **7/7**, Button **4/86**, with 22 Node
observations and the unchanged VM inheritance failure. All **1,123 escape rows**
match the current pinned baseline. Reports are unchanged except for Button's test
path after the lit migration. No full-bundle gain is claimed.

**Exact next native boundary:** original `_mergeConfigObj` performs
`"object" == typeof H.getDataAttribute(element, "config")`. The attribute call now
compiles to one native function in all four provider/policy combinations; adding
that JSON-result `typeof` still refuses at **`ctjs.unary`**. Original M's imported
**24 nine-register blocks / handler ^bb12** remain intact before preparation.
Prove that JSON observation next: the object tag includes null and arrays, so it
must not narrow to object members alone. Then come JSON object spreads and
dataset/config merging; public `dom/dataset.hpp` already
provides the dataset core. Matching/live F keys remain refused separately.
Inherited static/object-valued defaults, initialization, retained callbacks and the
application driver remain unfinished. Claude's pending runtime/audit branches
remain his to land and require fresh differential validation when integrated.
The entries below are historical checkpoints.

## Native JSON chain recovered and gated, 2026-09-16 UTC

Resumed **efe8daa8**, identified in the previous handoff and AGENT-SYNC, by
replaying its draft over the audit landings in `codex-json-resume-20260916`.
**53b9f68a** and **0a7c0302** finish that thread; the old `claude-json-chain`
draft is superseded. Three agents split recovery review, native regressions and
the Bootstrap boundary survey. September 7 WIP is already an ancestor.

**53b9f68a** recovers bounded checked-call chains in source order. JSON member
origins follow complete register flow; both failure paths retain the exact saved
input. Zero-call limits, every insufficient budget and failed proofs publish no
partial evidence or source mutation. **0a7c0302** adds explicit original JSON/parse
identity, reuses the existing `JsonType` only behind a complete DOM proof, and
emits public `ctbrowser::parse_json` with owning `ctbrowser::json_value` results.
Success moves from `std::expected`; String failure arms own their bytes. No Script,
VM, collector, generic JSON fallback or second parser is emitted. **47d6e275**
documents the contract in [native DOM entries](native-dom-entry.md).

Focused gates passed **2/2 proof tests (3.92s)** and **3/3 DOM drivers (178.24s)**.
JSON covers **7 sources / 14 Node-VM observations / 8 GCC-Clang binaries / 72
refusals**, both providers, policies and layouts, plus a lifetime sanitizer after
document destruction. DOM Strings now reports **775 Node-VM observations / 8
binaries / 1,060 source refusals**, with its separate provenance/budget checks.
Complete **310-step build / 605/605 CTests (1706.00s) / 177/177 lit (1305.06s)
PASS**; full wrapper exit **0**. All **1,561 frozen input files / 113 submodule
files** match the devbox and implementation commit **0a7c0302**; API and checkpoint
docs were updated afterward. Evidence: `/tmp/ctcompile-json-resume/`, including
`full.log`, `full.exit`, `full-last-test.log`, manifests and measured JSON reports.

**a244a2f9** fixes remote sync with `rsync --checksum --no-times`: changed older
worktree contents invalidate Ninja, while identical files keep their timestamps.
Standalone rsync/shell checks pass. The first candidate run mixed stale objects
and is invalid; the fresh compile's const-MLIR-handle test error was fixed before
the green gates. The integrated **09341902** baseline also passed **604/604 CTests
(1503.00s)**. Formatting with 23.1.1 passes **844 C++ / 95 Python / 106 web**;
the required pinned formatter retains the same nine-file/26-diagnostic baseline.

Fresh Bootstrap remains **19/574 native / 0 of 43 globals**, both policies, without
skips or pruning. DOM Data remains **7/7**, Button **4/86**, with 22 Node lifecycle
observations and the unchanged VM inheritance failure. Those reports are identical
to the preceding measurements. All **1,123 escape rows match the current pinned
baseline**; only the already-landed audit program hash differs from the older
pre-format evidence. No browser/runtime or escape-analysis source changed.

**Exact next native boundary:** original `H.getDataAttribute -> M` now reaches the
complete typed DOM proof and refuses at **`ctjs.unary`**, under all four
provider/policy combinations. Original M retains **24 nine-register blocks** and
its handler at **^bb12**. Start with its Number truthiness (`!0`/`!1`), then prove
the full Boolean/Number/null prefix and mixed JSON result ownership, including the
saved optional-String guard. F's original regexp/callback key conversion and H's
attribute-key construction follow. Full dataset/config, initialization/inheritance,
retained callbacks and the application driver remain open; no full-bundle gain
is claimed.

**Independent next compiler task:** Claude's 2026-09-16T03:53 journal records a
temporary restoration of top-level `var x;` writes in **7ad52ce2** to satisfy
`bootstrap-host-prefix.py`'s wrapper proof. Import and prove the existing
`program::hoisted_vars` declaration metadata instead of depending on those writes;
keep runtime semantics as the oracle. The CTJS importer currently carries no such
metadata. Claude's pending runtime/audit branches remain his to land. Earlier
sections below are historical checkpoints.

## Helper/URI composition, the JSON chain draft and the audit, 2026-09-15 UTC

**7a337ee2** composes helper expansion with URI normalization: every
handler-owning function in the fingerprinted DOMSource clone is normalized by
`normalizeDOMURI` (which now takes the function name; the working contract is
re-fingerprinted between functions) and `expandDOMHelpers` then inlines the
structured invoke, treating it as opaque in the helper body check because the
complete DOM entry proof reproves the inlined result. **1392f435** adds three
helper-shaped nullable URI sources (function, saved read, arrow) and three
refusals (payload observed, unguarded nullable read, call inside a branch arm).
Focused gate: `ctcompile_native_dom_strings` PASS 133.48 s; the full gate on
that tip was 605/606 with `ctcompile_native_dom_entry` at its 300 s cap under
`-j8` (223 s in the previous green run) — both DOM driver caps are 900 s now.

The **efe8daa8** JSON draft at this checkpoint has been recovered and gated as
**53b9f68a / 0a7c0302**, described above. Do not resume the old draft again.

**Operator-directed ponytail audit** (this session, all by locked merge, each
branch gated in its own devbox dir): `41d0185a` tools/cmake (mingw builders in
one table, snapshot.sh and its selftest gone, shaderc/gen-shaders/ratchet
shims/CTProject.cmake/LLVMVersion.cmake/GLM deleted, compare.py on Pillow),
`d47a8dd8` Script dedups, `2eae1dc7` Core/DOM/Raster (plain in-place node
payloads, deque slab, one-queue scheduler, abstract ttf backend, GL probes
gone; tsan clean), `11185599` ctcompile lowering (the three PDLL files are
`OpRewritePattern`s, `mlir-pdll` is no longer needed, `withProvedClone`
replaces four host-preparation transactions, `ctjs::functionIndex`/
`isPrimitiveAttr`/`sameValueZero`, `--mode` and `manifest::mode` gone,
`DOMEntryAnalysis` charges its module census before the fingerprint — 604/604),
`4e0b3b77` Style (leading_imports gone, resolve() in engine.cpp, small helper
dedups; the two shorthand expanders were left as two contracts on purpose),
`c3108dc5` Shell (installer helpers, 600 lines out of public headers, WebGL
X-macro, `<canvas width=0>` per spec, ctx.font through the CSS parser, ANGLE
preference deleted). `24eeb654`/`7662b763`/`3c50bc67` format the test
JS/HTML/CSS and repin what that moved (`escape-claims/Initialize.cmake` hashes,
`expected.txt` program row; `Exports/boundary.js` stays byte-exact under
js-beautify ignore markers because 27 pinned hashes derive from it).
The last two audit branches landed on 2026-09-16: **5e3d3b86**
`audit-ctcompile-tests` (24 `cmake -P` checks, the eight Browser drivers and
`native_owned_global_maps` are lit tests; `ctcompile_` CTest registrations
424 → 106, lit 177 → 251, `ctcompile_lit` cap 5400 s; test C++ uses
`ctbrowser/test/support/check.hpp`; escape-claims hash pins live in
`escape-claims/check.py`) and **28878c6c** `audit-ctcompile-emit` (nine
string-literal helper headers → the compiled
`include/ctcompile/CTNative/Runtime/ctnative.hpp` behind `#define
CTNATIVE_ORDERED_MAPS`/`CTNATIVE_DOM`, with `style/engine.hpp` included only by
programs that take a style parameter; every `needs*` flag is gone — **0b1e0911**
removed the `needsDOMJSON` guard c127ba96 had just added; source-name
provenance deleted, locals are `v<N>`, captures `capture_<i>`/`argument_<i>`).
Each passed 288/288 (251 lit) in its own devbox dir; the merged tip compiles
(304 steps) and awaits its combined ctest. The integrated **09341902** baseline has since passed the combined gate above. Sanitizer findings outside the audit, not
fixed: `Script/builtins/collections/keyed.cpp:593` UAF,
`Style/css/calc/units.cpp:36` UAF, `Core/number_format.cpp:194` UB cast.

## Saved nullable URI guards and fingerprinting, 2026-09-15 UTC

Resumed the interrupted **6caa728b** full-validation thread, found in this
handoff and AGENT-SYNC. **16b1660d** records its recovered 605 non-lit and
177 lit passes without inventing the disconnected wrapper's missing exit status.
Both histories/unmerged branches were checked; September 7 WIP was already an
ancestor. Three agents reviewed the nullable proof, strict/API quality and repo
complexity/Boost opportunities; root recovered their service-limited drafts,
integrated the changes and ran the gates. No browser/runtime edits.

**b13382ec** separates the complete DOM entry proof from manifest parsing and
fingerprinting. **9517ff21** avoids cloning report-free IR during fingerprinting;
report-bearing input retains clone/clear behavior. Every freshness check remains,
with complete hashing and no cache. Legacy-hash equivalence, nested reports,
source nonmutation and changed-source fingerprints have a regression.

**bb7ba402** compiles the original M guard `if ('string' != typeof t) return t`
on a saved `getAttribute` result before one URI try/catch. The producer remains
`std::optional<std::string>`. A complete branch proof records the exact dominated
String uses, and emission copies its value only inside the selected arm. Null,
empty String, an independent reread, aliases, later DOM mutation and the original
catch snapshot remain distinct. Loose equality is accepted only for two proved
Strings; String-only branch/Invoke results use ordinary owning Strings. No Script,
AOT, generic nullable carrier, handle table or new decoder is emitted.

Focused **3/3 CTests in 131.04s PASS**. DOM Strings now covers **745 Node/VM
observations / eight GCC-Clang binaries / 1,048 source refusals**; the nullable
slice adds **80 observations / 52 refusals**. Both providers, policies and layouts
pass, including result lifetime after document destruction and every incomplete
host-proof budget. A positive-arm source exposed an unreachable importer epilogue
that rejoins a live return; its dead branch is now accepted while every source
operation/effect remains censused. Stable formatting passes **844 C++ / 104 Python /
33 web**; the required pinned formatter's **nine-file / 26-diagnostic** baseline
is byte-identical.

Timing uses unchanged Bootstrap IR, saved baseline/candidate tools, warm-up and
11 alternating pairs on the devbox. Fingerprint command median: **159.20 →
152.11 ms (4.45%)**; instrumented pass: **60.4 → 51.0 ms (15.56%)**. Output and
fingerprints match exactly. Tiny URI lowering measured **8.263 → 8.579 ms**,
so there is no measured general transcompilation speedup. The CLI clears supplied
reports before fingerprinting; its decorated-input timing does not measure the
clone fallback. See [the quality review](native-quality-review-2026-09-15.md).

Complete **310-step build / 606/606 CTests in 1454.14s / 177/177 lit in
962.95s PASS**, with the full wrapper's exit status **0** retained.
All **1,462 source / 113 submodule hashes** match devbox, local and committed
source. Fresh Bootstrap remains **19/574 native / 0 of 43 globals**, both policies,
no skips/prunes; DOM Data **7/7**, Button **4/86**, 22 Node observations and the
existing VM inheritance failure. Button/Data reports and all **1,123 escape rows**
are unchanged. Original M and the isolated nullable URI helper each refuse all
four provider/policy combinations at the helper source-shape check; M retains
**24 nine-slot blocks** and handler **^bb12**. No full-bundle gain is claimed.
Full WPT/test262 were not remeasured; browser/runtime and expectations are unchanged.
Evidence and replay scripts: `/tmp/ctcompile-nullable-uri/`, including durable
`full.log`, `full.exit`, `measured.json` and the isolated `next-probe.py`.

**Exact next boundary:** compose the existing helper expansion and URI recovery
inside the fingerprinted DOMSource transaction. The current driver chooses one
based only on a handler in the selected entry; a handler inside M reaches helper
expansion instead. Preserve the original call-site actual/capture/receiver proof,
handler vectors, checked status edges and all-budget rollback; reprove the complete
result before publication. Then original M needs JSON/parse identity and original
lookup order, sequential URI/JSON failure continuations, and mixed primitive/JSON
ownership. JSON lookup precedes decoding; either failure returns the saved input.
Full H, initialization/inheritance, retained config/callbacks and the native
application driver remain open. Evidence: `/tmp/ctcompile-nullable-uri/`.

## Earlier measurements

This file holds the current checkpoint. When replacing it, move superseded entries
to the dated history below; keep each file under 1,000 lines. Historical claims and
next steps retain their original context and are not current instructions.

| Date | History |
| --- | --- |
| 2026-09-15 | [URI continuations, shared cores and DOM factories](handoff/2026-09-15-01.md) |
| 2026-09-15 | [DOM captures and Number index evidence](handoff/2026-09-15-02.md) |
| 2026-09-14 | [DOM helpers, attributes and class methods](handoff/2026-09-14-01.md) |
| 2026-09-14 | [Original Bootstrap Data, DOM sessions and arrays](handoff/2026-09-14-02.md) |
| 2026-09-13 | [Browser cores, UMD and Data observations](handoff/2026-09-13-01.md) |
| 2026-09-13 | [Recorder callbacks, captured snapshots and array indices](handoff/2026-09-13-02.md) |
| 2026-09-12 | [Child Maps, nullable values and dense arrays](handoff/2026-09-12.md) |
| 2026-09-11 | [Caller-owned Maps, global aliases and accessors](handoff/2026-09-11.md) |
| 2026-09-10 | [Object keys, mixed carriers and Map sizes](handoff/2026-09-10.md) |
| 2026-09-09 | [Saved Map sizes, scalar globals and BigInt operations](handoff/2026-09-09-01.md) |
| 2026-09-09 | [Scalar initialization and arithmetic provenance](handoff/2026-09-09-02.md) |
| 2026-09-08 | [Map absence, object fields and child ownership](handoff/2026-09-08-01.md) |
| 2026-09-08 | [Finite Map results, nullable values and short-circuit reads](handoff/2026-09-08-02.md) |
| 2026-09-08 | [Map writes, payloads, keys and array retention](handoff/2026-09-08-03.md) |
| 2026-09-07 | [Map effects, source calls and exported getters](handoff/2026-09-07-01.md) |
| 2026-09-07 | [Host ownership, protected helpers and native groundwork](handoff/2026-09-07-02.md) |
| earlier | [Compiler bring-up and original AOT decisions](handoff/earlier.md) |
