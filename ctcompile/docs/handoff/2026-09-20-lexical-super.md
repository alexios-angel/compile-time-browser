# Lexical super and composed overwrites — 2026-09-20 UTC

Resumed clean `9b9df3b7`, continuing the threads claimed at 00:15–00:17 UTC
and interrupted at 00:17:15 before any edits. HANDOFF, the master plan and
`7c4c4866` / `eb8db016` identified lexical-super method lookup and composed
index overwrites as the unfinished work. Both agents' histories and unmerged
branches were reviewed. `codex-wip-20260907` is already an ancestor; no recovery
merge was needed. The abandoned predecessor claims were released and reclaimed.

## Landed

`6acc1c5c` proves called lexical-super lookup from the exact immutable
method home and nearest base definition, independently of the leaf receiver's
ordinary method table. A capture-free linear base target is expanded at the
original call, after argument evaluation, with its receiver mapped to the leaf.
Every original base and shadowed body remains for the complete source census;
expanded receiver uses receive the existing field/method proof. Unread inherited
slots are omitted only after that census. An unknown property key conservatively
keeps the slots. No prototype storage or runtime dispatch is emitted.

Four new positive sources plus the unchanged `inherited-dispatch` source add
**40 native executions**. That preserved source now observes **118** natively:
B's constructor invokes Qi's override; Qi's lexical super selects B; B's ordinary
merge call selects W. New cases cover nearest selection, leaf dispatch, inherited
lexical homes and left-to-right argument/body effects. Eight negative sources
retain missing-target, mutation, reflection, capture, dynamic/foreign invocation
and target-CFG refusals. All previous fixture sections are byte-identical.

Expansion refuses target captures, callee/new.target observations, excess
arguments, non-linear control flow, declarations and roots. Independent review
found that cloning roots while omitting the original frame would create a
cross-function SSA reference. The final preflight rejects roots before cloning;
valid rooted-source regressions require the exact diagnostic and no output.
The same guard now protects the existing base-constructor expansion, which
also omits the source frame.
A shared budget check finds the first complete lexical order proof at **1,215**
steps and requires every lower attempted limit to emit no partial IR.

`950dfb64` proves composed affine own-index overwrites, including `i / 2 + 1`,
`(i + 2) / 2`, `(i * 2) / 2 + 1` and `(i - 2) / 2 + 2`. The proof evaluates
every source arithmetic step using the existing bounded Number helpers.
Signed intermediate integers are allowed; fractional or out-of-bound
intermediates cannot be hidden by later cancellation. The varying operand
follows one bounded recursive path; factors/divisors and final strides are
positive, and division must preserve every visited integer. Final indices must
be existing own elements. The unchanged reload-disjointness pass checks the
composed footprint. Saved children, remaining aliases, historical cycles,
mutation and work limits remain. Added CFG/SCF controls and an 18-function
source oracle; prior specimens and ownership representation are unchanged.

An escape agent completed implementation and self-review. The original native
fixture/review agents hit repeated service limits; root completed the fixtures.
A fresh bounded reviewer found the root/frame issue above and confirmed its fix.
Root reviewed, gated and committed each concern separately.

## Measured validation

All builds and remote commands held `/tmp/ctbrowser-devbox-build.lock`.

- `tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-host-contract
  ctcompile-test-native-reference ctcompile-test-escape-analysis-arrays
  ctcompile-test-escape-claims ctcompile-test-type-oracle` passed. Subsequent
  native-only builds used the first four targets.
- `ctest --test-dir projects/compile-time-browser/build --output-on-failure
  --no-tests=error -R '^(ctcompile_host_contract|ctcompile_escape_analysis_arrays)$'`
  passed **2/2**: arrays **1.35s**, host **0.49s**. After the final native edit,
  the exact `^ctcompile_host_contract$` selection passed **1/1, 0.48s**,
  0.49s total.
- `~/.lit-venv/bin/lit -sva projects/compile-time-browser/build/ctcompile/test
  --filter='^ctcompile :: Analysis/Escape/escape-claims/(composed|scaled|quotient|offset)-index-overwrite[.]test$'`
  passed **4/4, 0.13s**. Composed: **54 sites / seven sound / 7 of 10 confined
  precision**. Scaled: 54/seven/7 of 10; quotient: 51/six/6 of 9;
  offset: 48/seven/7 of 13. All have zero violations, partial, pending and
  unclaimed sites.
- A temporary wrapper selecting only the 12 new sources plus
  `inherited-dispatch` passed **13 observations / 40 main native executions /
  26 unprepared and 10 preparation refusals**. Existing ancillary checks added
  16 constructed-method executions/20 refusals and eight original-r
  executions/four refusals.
- `~/.lit-venv/bin/lit -sva -j2 projects/compile-time-browser/build/ctcompile/test
  --filter='^ctcompile :: CTNative/Lowering/Objects/class-(initialization|dom)[.]mlir$'`:
  **2/2, 310.09s**. Class initialization: **207 observations / 564 main native
  executions / 414 unprepared and 234 preparation refusals**; ancillary checks
  retain 16 constructed-method executions/20 refusals, eight original-r
  executions/four refusals, four prototype-key executions/six refusals and 13
  prepared-source refusals. DOM: **632 observations / eight combined native
  executions / 4,910 refusals**. This run preceded only the final constructor-root
  guard. That last change received the targeted follow-up below, with no broad replay.
- Final constructor-root follow-up: the same temporary source driver, adding
  `inherited-explicit`, passed **14 observations / 48 main native executions /
  28 unprepared and 24 preparation refusals**, including both root controls and
  the lexical budget search. Exact host CTest passed **1/1, 0.50s**, 0.51s total.
  The earlier two-case lit selection was not replayed.
- All **ten changed source hashes** match the tested devbox copy. The preserved
  dispatch C++ was inspected: stack-owned object, borrowed receiver parameters,
  direct ordinary method calls and inlined lexical base arithmetic; no
  Script/VM, prototype or home storage.
- Required `tools/format.sh --check` retains **20 baseline diagnostics in six
  HEAD-identical files**. Changed C++ passes the pinned formatter; changed
  Python passes Black with `pyproject.toml`. Python syntax, Node source
  observations, temporary shell-script syntax and `git diff --check` pass.

The first temporary gate script lacked executable permission; it was then run
through `bash`. The first narrow probe used an empty SSH-PATH Node lookup;
subsequent probes use the build's configured Node path. The first compiler
probe exposed the unread inherited-slot issue. A budget-test attempt reused
custom-assembly mutation checks against generic IR; the existing budget loop
was extracted and reused separately. A computed-super source exposed the
existing importer limitation (`super[key]` is not supported), so the new dynamic
invocation control uses supported `super.read[key](this)` syntax. An intermediate checksum comparison also ran before the final guard sync;
only the final resynced comparisons are counted. These failures preceded
the final passing checks above. No runtime behavior was changed to
settle a differential disagreement.

Full CTest/compiler lit, broad native/corpus matrices, WPT/test262, sanitizers,
Windows builds and whole Bootstrap were skipped. These are focused results,
not a full-suite pass. No browser/Script edits, dependencies, history rewriting
or push. Evidence: `/tmp/ctcompile-lexical-super-20260920/`.

## Next boundary

The next source slice is the authentic W/B helper and constructor capture
proof. The unchanged `bootstrap-base` specimen still refuses with
`class method capture is not its constructor or an inert sibling helper`;
`bootstrap-config-r-h-defaults` still refuses an unproved `ctjs.create_closure`.
These diagnostics were remeasured after the final guard. Start with W's original
captured `r` helper and B's original captured `a(t)` constructor call, using the
existing local-cell/helper/holder proof on the DOM path. Retain all original
W/B methods and helper definitions, including uncalled and shadowed bodies.
Do not erase them, fabricate H calls, or infer DOM authority from a formal name.

`examine` still requires capture-free constructor closures and explicitly
refuses inherited DOM methods without a per-leaf body proof. Those guards need
proof extensions, including receiver-selected `this.constructor.Default`,
`DefaultType` and related getters. The real B constructor also has its conditional
element initialization and `e.set` publication; Qi adds parent/menu selection,
configuration validation, events and Popper. The scalar 118 example does not
establish any of those behaviors. General target CFG/captures, own-data definition
provenance, broader ownership/control flow and the application driver remain.

