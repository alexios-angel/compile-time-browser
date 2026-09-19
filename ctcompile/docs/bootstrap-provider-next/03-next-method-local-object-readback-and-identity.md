[Back to bootstrap-provider-next.md](../bootstrap-provider-next.md)

## Next: method-local object readback and identity

The eight-call saved-object source stores `{value: 1}`, saves a Map.get,
overwrites/deletes the entry and compares exact identities. It gives
Node/interpreter trace=1 and remains unowned **0/5 native** in both modes.
The distinct equal-field eight-call control and fresh-read-after-delete nine-call
control give trace=0 and retain the same refusal. Prove the local object origin,
presence and identity without treating a public object result as already owned.
Final probes isolate a direct fixed own-field read at five calls, trace=1,
and saved same-key identity, Map.get field-read and guarded-read controls at
six calls, trace=1. All remain unowned 0/5 in both modes. The two-stored-object
distinct control is seven calls, trace=0; the comparison-only fresh-object
control is six calls, trace=0 and adds a separate native family limitation.
Saved references must retain their original object after replacement/deletion
and observe later writes through aliases. All sixteen measured sources,
including the unchanged historical 8/8/9-call cases, are in
`/tmp/ctcompile-leaf-object-next.json`. The source and proof obligations are in
[the Map boundary](../native-owned-global-maps.md#next-boundary).

String fields and Object/String Map payload carriers remain separate. The
historical nested leaf-writing sibling now has complete ownership but stays
0/6 at that mixed carrier; the original eleven-call `{value: 'instance'}` source
remains unowned0/6, trace=2. Provider entry tokens cannot authorize future local
objects, and ordinary ownership supplies no throwing-call or general export ABI.

The exact Bootstrap getter at vendor line 17 also needs nested/object payloads.
Exact Bootstrap Data, general realm owners and future-call contracts remain
unfinished; the measured corpus counts and full gate are in [HANDOFF.md](../HANDOFF.md).

Exceptions do not make a callback or allocation inert. Native try/catch covers
one acyclic handler with homogeneous number, boolean or owning string throws
and proved nonthrowing primitive helpers in its try and catch. Checked callee
resolution now follows preserved status/register vectors. Target verification
also follows homogeneous primitive payloads through defined EmitC helpers.
Explicit `ctjs.invoke` regions now separate the normal result from an implicit
thrown payload and pre-call state; bounded type flow now covers both payloads
and the invoked helper's normal returns. Ordinary call inference remains
conservative on throw exits. An internal `CheckedInvocations` recovery mode now
constructs those regions and connects the two completions to the enclosing try
through a value-only tuple, retaining pre-call state on unwind. The default
mode and all native throwing-call refusals remain. An additional
`EffectCheckedInvocations` mode validates other status and continuation effects
before adopting the recovered clone. Commit `e4b2d5f` proves source global
callee lookups from the complete live declaration/store/use and effect census,
with separate bounded primitive completion facts. The three direct-call source
specimens now recover in this internal mode, preserving assignment and argument
state. The focused gate passes all 3521 source-binding budget cutoffs and 937
effect cutoffs, nineteen live binding mutations and uncalled-throw controls.
Native admission still needs the complete call component; target emission must
consume the accepted invokes. Native throwing callees, general
finally, reentry and object payloads require
further work. The [source invocation gate](../native-source-invocations.md) now
retains four source programs, sixteen functions and eleven observations for
assignment snapshots, prior normal calls, argument mutation and receiver/key/
getter/argument order. All source throwing calls still refuse native lowering;
the document identifies the remaining admission and emission obligations.
Normal-return provider facts cannot authorize an exceptional continuation.

Retain the eleven scenarios in
[the exact host audit](../../../tools/check/bootstrap-host-contract-audit.py),
including callee-order observation `1234` and throwing/reentrant sinks. Preserve
vendor/program hashes, runtime methods and observer branches. Plan parts 24 and
25 remain the native type and ownership requirements.
