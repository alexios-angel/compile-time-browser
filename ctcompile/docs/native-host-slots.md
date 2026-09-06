# Checked host roots and publication-slot flow

`ctnative-host-contract` implements the first proof boundary proposed by
[the Bootstrap host contract](native-bootstrap-host-contract.md). It is an
opt-in analysis pass. It does not change runtime operations, native admission,
the boxed ABI, or the existing exact Data census.

A driver supplies a `closed-source-v1` JSON manifest. The manifest identifies
the script entry, publication roots/properties and observed global names.
Absent bindings and present bindings whose value is undefined are separate:
absence can answer `typeof`, but cannot justify a bare identifier read.

```json
{
  "version": 1,
  "provider": "closed-source-v1",
  "module_sha256": "<canonical module fingerprint>",
  "entry": "_script_$0",
  "roots": [{"binding": "host", "properties": ["slot"]}],
  "observations": ["trace"],
  "absent_bindings": [],
  "undefined_bindings": ["undefined"]
}
```

The provider promises ordinary own global data bindings, the declared initial
environment, and initially unmodified intrinsic prototypes. All execution and
mutation must come from the supplied source. It does not promise that source
operations are pure or nonthrowing. Unknown fields/provider names, supplied
effect claims, conflicting binding declarations and source writes to fixed
absent/undefined bindings are rejected.

The fingerprint covers the canonical generic IR of the complete program and
driver. Locations and previous `ctnative.host_*` reports are excluded. Changing
semantic IR invalidates the manifest; comments or printing locations do not.
The separate exact-probe artifact records the original JavaScript and vendor
hashes as well.

```sh
ctjs-opt prepared.mlir --ctnative-host-contract=fingerprint=true -o /dev/null
ctjs-opt prepared.mlir \
  --ctnative-host-contract='manifest=host.json output=host-report.json report=true'
```

`require-proof=true` makes an incomplete contract a pass failure. Without it,
the pass writes the partial report and its refusal reason. `max-steps` bounds
discovery and defaults to 100,000; exhaustion withholds every usable proof.

## What is proved

Each requested root must have one source binding initialization to a fresh
object in the script entry. The initialization must be unconditional. The
analysis follows global aliases, exact direct-call arguments/returns and
structured branch values. UMD `typeof` string comparisons can select a branch
from the actual source initializers and declared absent/undefined bindings.
This branch interpretation changes no source operations.

An ordinary constant property key and a checked environment establish an own
data slot. Source alias writes are tracked, including ordered replacement.
Every read needs a preceding initialization, and all writes must have a
supported order relative to the read. Cross-function ordering currently
requires one visible invocation path per helper. Multiple invocations and
recursion need context-specific slot state; source schema equality does not
equate different runtime allocations. Every allocation used as an object
identity must have one exact invocation path back to the script entry,
including callers of wrappers around the allocation site.

Direct-call identity is checked against the actual retained closure value and
numeric function identity. A symbol annotation or private visibility alone
does not establish it. Unknown calls, unknown property receivers, dynamic or
prototype keys, accessors, deletion, throwing operations, unknown conversions
and unsupported control flow prevent a complete proof. Intrinsic and error
recorder providers are not implemented by this pass yet.

The public `HostContractAnalysis` object exposes the source write/read edges.
Its `property(read)` query returns an edge only when the **whole requested
contract** passes. The analysis borrows the current module and must be discarded
after a semantic mutation. Printed `candidate_edges` are partial evidence;
`proved_edges` stays zero after any refusal. Old or forged report attributes
never authorize the query, including on repeated pass invocation. No native
pass currently consumes these diagnostic attributes.

## Exact Bootstrap evidence and remaining work

`tools/check/bootstrap-host-slots.py` calls the existing exact probe generator;
the retained vendor wrapper and Data declaration are unchanged. Its separate
artifact contains the generated source, source/vendor hashes, canonical
manifest and slot report. It requests `module.exports` for CommonJS and
`globalThis.bootstrap` for the defined browser host. AMD retains an explicit
unsupported callable-owner frontier: `define` is a function, not a fresh
ordinary object root accepted by this first provider.

These reports preserve the seven/seven/eight source-function denominators and
explicitly list the 19/19/20 observation roots. They do not replace the existing
reference observations, native admission census or lifetime tests. A report of
source publication flow is not a native Bootstrap executable.

The measured CommonJS report finds one fresh root, two source writes and 24
reads with candidate write/read edges. The browser report finds one fresh
root, one source write and 23 candidate edges. AMD finds no accepted fresh
root or candidate edge. All three contracts remain refused with zero usable
edges; the current complete-contract refusal is `property receiver lacks a
fresh own-data object proof`.

The next consumer must use a live complete proof to extend the owning method
table and retained callable flows. The exact Data component still needs the
supported intrinsic/error provider behavior, publication ownership, call
component closure and unchecked mixed-result field access described in the
host contract. Delayed/repeated AMD factory invocation needs a distinct owning
callable slot and per-invocation identities. Full Bootstrap initialization
remains outside this increment.

Focused coverage is registered as `ctcompile_host_contract`, the
`CTNative/host-contract.test` lit test, and
`ctcompile_bootstrap_host_slots_{commonjs,browser,amd}`. It covers live proof
queries, ordered aliases/direct helper publication, missing initialization,
source mutation/accessors/throws, absent-binding misuse, missing roots,
stale manifests, forged reports, unknown provider/effect claims and exhausted
work. Single-invocation factories remain supported; repeated factories and
wrappers cannot merge their allocations or property histories. The existing
exact Data tests remain registered independently.
