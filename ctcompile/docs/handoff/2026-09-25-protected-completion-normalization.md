# Protected completion proof and conditional array receivers

Continued clean `ctcompile-v1` at `26424e84` from the recorded inactive
completion boundary. The earlier state transport and captured-child retention
were already committed; the old WIP branch was already an ancestor. Agents
were resumed after steering interrupted the team. The escape agent completed
its candidate and reviewed the native change; the parent implemented and
validated the native proof and committed both changes separately.

## Native checkpoint

`60ffb6ea` extends `DOMSource::normalizeCompletion` to the retained protected
shape. The tests first obtain the original rewritten getter/method entries,
then normalize separate copies. Both verify with no poison, three Invokes
retaining 15 saved slots each, one Try and unchanged caught-anchor CellGet
identity. The original eight raw literals, including both iterator sources,
remain byte-identical.

- Each Invoke keeps its call/InvokeExit body. Normal and unwind yields feed
  their exact source continuation before joining the enclosing terminal.
- Live saved values map to the new unwind arguments. An inactive saved slot
  may receive undefined padding in the emitted state, but its source argument
  stays poison in the proof mapping. Observing it still refuses.
- Try body and catch normalize separately. An exact exit flag decides which
  normal/caught tuple is active; only the other side can be padded. The flag
  travels through Invoke as a JavaScript Boolean and becomes i1 at TryExit.
- Constant value-only branches retain both guarded arms and source effects.
  Integer completion selectors still select exact arms, and the final source
  census still requires every source operation to have been visited.
- Mapping copies and region/state traversal consume the existing work budget.
  Depth remains bounded at 64. Invocation terminals containing loop-condition
  types still refuse; this increment does not add that correspondence.

Nine variants per original source cover the valid projection, poison passed
to the call or observed in normal/unwind/catch, an unknown exit flag, a normal
payload observer, zero budget and one-less-than-required budget. Refusals leave
the input unchanged. The existing state-transport assertions remain intact.

Custom.cpp is unchanged and still refuses before calling the normalizer on
this observed shape. Thus this is an independently checked normalization
checkpoint, **not a new native JavaScript admission**. Complete preparation
still rolls back the original source and fingerprint.

## Escape checkpoint

`f313a0b0` lets an exact nested UnknownOp capture use its current sink role
when testing array-retention eligibility. Passed remains restricted to operand
zero of an array GetProperty/SetProperty operation. Complete region inputs,
own-element contents, acyclicity across all writes and every returned graph
still decide the result transactionally.

Thirteen new rows cover own reads/writes, direct and selected returns, later
publication, absent/dynamic indices, earlier unrelated captures, wrong operand
roles, append, transient cycles, nested origins and missing solver evidence.
Receiver transitions are asserted separately from discharged stored children.
One existing row now proves its private receiver while preserving the child's
earlier unrelated capture. Its source body is unchanged. The public legacy
storage/load census and both historical Induction witness files are unchanged.
The raw oracle was not replayed, and no raw confinement gain is claimed.

## Focused validation

All devbox operations held `/tmp/ctbrowser-devbox-build.lock`; no local C++
build ran. Explicit targets: `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-exception-recovery`, `ctcompile-test-host-contract`,
`ctcompile-test-native-reference`, `ctcompile-test-escape-analysis-arrays`.

- Exact `ctcompile_escape_analysis_arrays`: **1/1, 4.68 s**.
- Exact `ctcompile_exception_recovery`: final **1/1, 5.64 s**.
- Exact `ctcompile_host_contract`: final **1/1, 2.72 s**.
- Source selection: `invocation-return`, `write-boolean-snapshot`,
  `helper-next-record`, retaining all original/refusal controls and existing
  provider, printing, optimization, GCC/Clang and Node/VM comparisons. All
  24 generated C++ files pass namespace checks; harness binary checks pass.
  This was not the whole native fixture or a compiler lit replay.
- Six final implementation/test hashes match the devbox. Eight raw DOMURI
  literals (five JavaScript, three MLIR) and both historical escape witness
  files are unchanged from the starting commit.
- `tools/format.sh --check`: **1126 C++, 159 Python, 114 web files pass**;
  `git diff --check` passes. Final independent bounded static review is clean.

Initial builds caught a TypeRange conditional mismatch and a test accessor
typo. The first normalization gate reached the unchanged source-visit guard
at an exhausted close's return lookup; retaining both guarded value-only arms
resolved it without dropping effects. The host gate then exposed overbroad
Boolean Truthy folding that static review had missed. Folding is now limited
to protected terminals; recovery, host and selected source checks pass after
that correction. The failed gates are retained in evidence.

Skipped: full CTest/compiler lit, whole native fixtures, broad corpus/matrix
runs, raw escape oracle, full Bootstrap, WPT/test262, Windows/macOS,
sanitizers and performance. No browser/shared implementation changed, no push
or history rewrite occurred. Availability remained uncertain: 11 readable
Linux identities, 60 permission failures, 348 Windows Get-CimInstance records
and no Claude matches. Concurrent ownership rules applied.

## Next boundary

Integrate the proved completion normalization with Custom.cpp's final census
before removing its explicit refusal. The final census currently searches the
next call's direct users for `done` and expects a direct entry or loop test.
For the protected shape, the call result belongs only to InvokeExit; own
`done`/`value` reads use the normal block argument. Prove that exact relationship
and the enclosing Try/body ancestry, rather than allowing arbitrary nested
calls. Keep exhaustion guards, saved return values, caught identity and effects.
The existing staged-state assertions must retain equivalent coverage when the
integration starts publishing normalized bodies.

Then run complete focused preparation on the original getter/method sources
and record its actual next refusal or admission. Do not assume this internal
normalization checkpoint proves helper expansion or final native ownership.
Mutable exceptional scalar state, Invoke loop-condition transport, nested
allocation origins, raw helper CFGs, escaping node owners, broader iterators,
unguarded Bootstrap defaults, the application driver and full native Bootstrap
remain unfinished.

Evidence: `../test-results/2026-09-25-protected-completion-normalization`
relative to the monorepo root: focused logs including failures, source selector,
final hashes, preservation checks, independent review and checksums.
