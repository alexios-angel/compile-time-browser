# Next Bootstrap provider proof

The [diagnostic and callback increment](native-provider-diagnostics.md) is
implemented. Exact CommonJS/browser/script-this-fallback probes now resolve
20 entry calls and complete 18 provider summaries, including the conflict
recorder. Their 19/19/24 observations and seven-function denominators remain
unchanged. Native admission is still 0/7 in each mode.

## Next provider boundary: ordinary object payloads

The next actual call is `set(element, "bs.collapse", instance)`. The current
transaction supports primitive Map values and nested private Maps, while
ordinary objects are supported only as identity keys. It refuses this value
before publishing a post-state summary.

Start with the existing initialized entry-object identity, bound to its actual
allocation and invocation. Extend provider values and retention checks to carry
that identity through Map set/get without copying or reconstructing the object.
Require current own-data field facts at subsequent field reads. A Map round trip
must preserve aliases and distinguish objects with identical fields. An object
token or previous prefix observation does not prove future immutability or
native ownership.

Keep writes and mutations runtime, rebuilding or invalidating facts on actual
field replacement, deletion, publication and unknown effects. Initially refuse
accessors, prototype mutation, dynamic fields, identity escape and object/Map
cycles without an explicit owner proof. Charge object edges and field work to
the transaction budget; a failed path must discard all tentative Map, object
and scalar-global changes.

Use unchanged exact programs before changing expected counts. Add source
differentials for two objects with equal fields, an alias mutated between set
and get, two factory invocations, nested Map retention, field replacement,
deletion/reinsertion and incomplete budgets. Measure extra resolved calls
before reporting them; no new count is predicted here.

## Native ownership and calls remain separate

Prefix discovery describes one executed startup path. Bootstrap publishes
callable objects for future callers, so native admission still needs live
callee/type proofs and ownership across the export boundary. The existing
confined method-table-field proof cannot be applied to an arbitrary realm or
global owner. Keep the seven-function denominator and named native refusals
while connecting proved value flow to those consumers.

Exceptions do not make a callback or allocation inert. Numeric native try/catch
currently covers one acyclic handler with explicit primitive throws; throwing
callees, general finally, reentry and owning payloads require further work.
Normal-return provider facts cannot authorize an exceptional continuation.

Retain the eleven scenarios in
[the exact host audit](../../tools/check/bootstrap-host-contract-audit.py),
including callee-order observation `1234` and throwing/reentrant sinks. Preserve
vendor/program hashes, runtime methods and observer branches. Plan parts 24 and
25 remain the native type and ownership requirements.
