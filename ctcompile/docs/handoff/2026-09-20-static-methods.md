# Own static methods and unary index overwrites — 2026-09-20 UTC

Continued clean `5ec7fa43` and the measured ordinary-static-method setup boundary
from the previous nested-capture handoff. No interrupted uncommitted work remained
at session start; September 7 WIP was absent from the unmerged branch list.
Service interruptions at 03:13:11 and 03:20:41 resumed the same exact claimed work.
Three agents supplied source regressions, an independent unary-index proof and
static-proof review; root reconciled and gated separate commits.

Linux executable/argv checks found no Claude identity but had 54 denied executable
reads. Windows Get-CimInstance succeeded (353 processes), without Claude matches.
Availability remained uncertain and concurrent-agent restrictions remained.
No browser/shared implementation edits, runtime changes or push.

## Landed

- `6f34888c`: bounded unary Number plus/minus in affine own-index overwrites.
  Reuses the exact bounded endpoint proof, positive stride and complete reload
  overlap census; negation reverses endpoints. Signed zero remains own key zero.
  Fractional/rounded/unbounded intermediates, growth, saved children, aliases,
  cycles, unsupported operators and proof depth/work limits retain refusals.
  Added five CFG/four SCF positives, eight unit refusals and a 21-function oracle.
- 1bd7e508: immutable own constructor static slots reuse the existing method
  closure/home, capture and getter proofs. Calls retain exact receiver and source
  argument order, become direct symbol calls, and preserve the original target
  frame/body. Unique slots and accessor/metadata collisions are checked. Receiver
  uses are limited to exact own getter reads; inherited calls, detached methods,
  mutation, new-this and receiver escape remain refused. All original bodies,
  including uncalled slots, pass the complete strict source census.

Static bodies do not inherit instance-method DOM authority. A bounded traversal
of accepted helper calls rejects transitive sharing with helpers requiring DOM
body proof, after all classes/captures are discovered. Three source regressions cover direct/transitive sharing and both class
declaration orders for both DOM providers. A helper first discovered under
strict static rules retains the earlier source-census refusal; an already
DOM-authorized helper reaches the new graph guard.

Twelve ordinary-static sources add four native positives and eight refusals.
One collision fixture explicitly records Node 9 / interpreter 8: the interpreter
returns the earlier callable while Node reads the later accessor. It remains
refused natively. Original fixture parts 01–08 and Bootstrap generation are
unchanged. The captured-static target retains its exact frame/root counts and
passes existing exact-budget, forged-input and duplicate-index controls.

## Focused validation

Every devbox command held `/tmp/ctbrowser-devbox-build.lock`. Scripts/logs:
`/tmp/ctcompile-static-0310/`.

- Built `ctjs-opt ctjs-translate ctcompile-test-host-contract
  ctcompile-test-native-reference ctcompile-test-escape-analysis-arrays
  ctcompile-test-escape-claims ctcompile-test-type-oracle` with
  `tools/remote-build.sh` (later invocations synchronized focused corrections).
- Exact `ctcompile_escape_analysis_arrays`: 1/1, 1.34s (1.35s total).
- Lit filter `^ctcompile :: Analysis/Escape/escape-claims/(unary|composed|reversed|negative-divisor)-index-overwrite[.]test$`:
  4/4, 0.14s. Unary oracle: 63 sites, nine sound claims, 9/14 confined precision,
  zero violations, partial, pending or unclaimed sites. Four source hashes match.
- Exact `ctcompile_host_contract`: 1/1, 0.50s (0.51s total).
- Class initialization lit passed: 258 source observations, 724 main native
  executions (32 new), 516 unprepared and 281 preparation refusals. Static
  capture first complete budget: 357. Ancillary controls: 16 constructed-method
  executions/20 refusals, eight original-r executions/four refusals, four
  prototype-key executions/six refusals and 15 prepared-source refusals.
  The first class-initialization/DOM invocation took 363.76s and had one pass
  and one failed new DOM diagnostic assertion; initialization was not repeated.
- Corrected DOM lit: 1/1, 288.75s. It measured 632 Node/interpreter
  observations, eight combined native executions and 4,922 refusals, including
  the twelve added provider checks. Nine native source hashes match the devbox.
  The two selected native lit cases both pass; the initial combined invocation
  was not a clean pass.
- Inspected argument-order, captured-static and getter-chain generated C++:
  ordinary typed free-function calls, std::string values and no closure
  environment, Script/VM symbols or prototype storage.
- Required `tools/format.sh --check`: same 20 diagnostics in six HEAD-identical
  files (browser ctdrive; compiler HostContract PrefixAnalysis/ProviderCallbacks/
  ProviderPaths, PartialEvaluation Heap, Symbolic Facts). Changed formatting,
  Python syntax, Node source observations, temporary shell syntax and whitespace
  checks pass. Full CTest/compiler lit, broad native/corpus matrices, WPT/test262,
  whole Bootstrap, Windows and sanitizer suites were skipped.

The first narrow native probe passed four new native cases and the static
root/budget controls, then exposed the collision fixture's unrecorded interpreter
behavior. The fixture was made explicitly observable on both engines; no runtime
or production proof was changed. The next probe passed the twelve new sources
and reached the authentic Bootstrap diagnostic mismatch below. Only the pinned
expectation changed. Independent review identified the shared-DOM-helper risk;
the final implementation guards it before any rewrite. The initial DOM check
then exposed an expectation mismatch for the opposite declaration order: its
helper stays strict and correctly refuses in the existing source census. That
source was preserved, its diagnostic pinned, and a third companion added for
the new transitive guard. Only the affected DOM lit case was rerun.

## Exact next boundary

The unchanged authentic W/B specimen now passes constructor static-slot collection
and refuses `class receiver escapes or observes a prototype/descriptor`. Source
inspection identifies B.dispose's `Object.getOwnPropertyNames(this)` and `this[t]`
clearing loop as receiver uses beyond the current fields-only proof. Preserve the
complete original dispose body. Start from fresh-object/field provenance and
prove the complete own-key snapshot before clearing; do not authorize arbitrary
dynamic keys or receiver escapes.
This is a preparation refusal, not successful Bootstrap execution.

B's ordinary static getInstance/getOrCreateInstance/eventName bodies are still
subject to later complete capture/receiver/census checks; the moved setup gate does
not establish support for all of them. `this.getInstance`/`new this`, inherited
per-leaf getter/DOM receivers, data storage, original H/config, n/document selector
paths, events/Popper and the application driver remain. Broader escape control
flow, ownership and own-data provenance also remain. No null-input observation
grants authority to uncalled methods or other branches.
