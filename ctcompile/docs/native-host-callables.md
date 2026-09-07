# Current exported getter proof

`HostContractAnalysis` can identify the actual uncaptured literal-return getter
loaded from an ordinary source-created table. The motivating source is:

```js
var host = {};
function make() { return { get() { return 42; } }; }
host.slot = make();
var trace = host.slot.get();
```

Prepare the source with `--ctjs-resolve-globals --ctjs-lift-to-scf`, then create
the host manifest from that prepared module. Analysis does not lift closures,
rewrite calls, change visibility or refresh the manifest's fingerprint.

The live `HostCallableEdge` identifies the original call, property read, current
preceding own-data write, exact `ctjs.create_closure` and numeric source function
identity. Both `ctjs.call` and an already resolved `ctjs.call_direct` are accepted;
the latter must name that same function and carry undefined `new.target`.
Receiver, callee and argument operands remain unchanged. The current slice
requires no explicit arguments and the exact source table as receiver.

The closure comes from the creating function's own source program, captures
neither upvalues nor a lexical receiver, and initializes only the current field.
Its target has one block, three unused implicit arguments, and a primitive literal return;
only frame/root bookkeeping and constants may accompany the return. Receiver,
closure, argument-window and capture observations, getter effects, missing or
ambiguous own-data initialization, indirect source identities and additional
closure exports refuse the proof. Ordered ordinary field replacement can select
the actual replacement closure; a native owner consumer may impose a stricter
single-initialization rule.

Callable edges become visible only after the entire host contract succeeds.
Unknown behavior elsewhere, stale fingerprints and every incomplete work budget
withhold both slot and callable queries. Printed reports are diagnostic only.
Rebuild the analysis after changing semantic IR; its operation pointers are live
module-specific evidence and cannot survive mutation.

This is proof for an actual source call. It does not promise argument types or
effects for future exported calls and does not make an exported function private.
The native consumer separately needs owning global/table storage and final
admission. Captured Map tables and Bootstrap's broader Data export remain outside
this getter slice.

Generator invocation cannot be inferred from its body: even a generator with no
`yield` creates a deferred iterator when called. The importer now refuses these
bodies through its existing skipped-function accounting, retaining their source
identities and possible global writes. Generators containing suspension points
keep their existing refusal. Ordinary async literal returns retain their explicit
`ctjs.wrap_promise`, which this getter proof refuses.

`ctcompile_host_contract` checks exact edge identity, indirect/direct agreement,
stale and forged inputs, late getter mutation, wrong target/receiver/new-target,
additional aliases and every incomplete budget. The source host-contract lit
gate includes numeric/string getters, root aliases, actual field replacement,
capture/receiver/effect/argument controls and the preceding publication cases.
The export-boundary regression continues to measure native admission separately
from complete host proof.

The live-query fixture completes at **295 charged steps**; every smaller
budget withholds both slot and callable edges. The source gate passes eight
positive and 22 refusal programs. Generator and async getter controls remain
refused: an iterator/promise invocation cannot be treated as an ordinary
literal-return call. The importer also refuses generators without a suspension
instruction, retaining their source-function and global-store accounting.
