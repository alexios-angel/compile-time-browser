# Direct storage evidence

`EscapeVerdicts::directStorage` records every `Stored` operand in the function's
live top-level CFG, independently of the first escape reason. Each record keeps
the operation, operand position, stored-value aliases and direct-target aliases.
The existing `sites` verdicts and oracle claims do not change.

This closes two omissions in the first-witness diagnostic. For
`const a = [child]; external.x = child`, both writes appear. If `child` was
passed to a call before those stores, both still appear although its first
verdict is `Passed`. External and primitive stored values also appear, so a
local container's incoming writes are not restricted to tracked children.
Array initializer positions stay distinct, and overwrites never erase earlier
writes. A loop contributes its static write once; the census makes no claim
that the allocation site denotes only one runtime object.

`directStorage.complete` means that every enumerated write has initialized
value and supported target aliases, and there is no live nested-region or
whole-frame refusal. Unsupported targets such as cell destinations, missing
lattices, live nested regions and suspension/late-arguments refusals clear it.
Partial records remain available when it is false. Dead top-level CFG blocks
contribute no records or refusals. An external alias is resolved evidence and
therefore does not clear the marker; it provides no local ownership proof.

The supported direct targets remain `create_array` results, `append` arrays
and `set_property` bases. The marker covers this direct-write census only.
It does not describe all contents or prove confinement: `copy_props` can
transfer children, property loads remain external, setters can retain elsewhere,
and a spread call can expose children of an otherwise confined packing array.
Cycles retain their `Stored` verdicts. No native admission uses this evidence.

The claims executable reports all writes, write-to-site edges, sites with
multiple stores, sites whose first reason is something else, unresolved aliases
and complete/incomplete function counts. The CTest script checks that every
first `Stored` witness is covered and every function is classified. Precision
counts are not gated; the existing oracle still gates zero false confinement.

Unit controls distinguish the actual allocation identities of same-kind sites
in joined values and a two-object cycle. They also cover repeated initializer
positions, stores after an earlier call escape, overwrites, external contents,
both live branch stores, loop-created objects with carried external aliases,
dead CFG blocks, unsupported destinations, missing lattices, nested regions,
suspension and late arguments capture. A complete spread-call example still
leaves its child `Stored`, demonstrating the limit of the marker.

Measured on the devbox on 2026-09-07: the **188-row** escape unit passes in
**0.01 seconds**; fixture and Bootstrap claims CTests pass in **0.08** and
**0.23 seconds**, with zero oracle violations. The compiler files pass the
formatting gate. Full-suite and corpus-wide census measurements are separate
from these focused checks.

The next proof must follow contents through loads and indirect transfers,
propagate every exposure including sinks after the first `Stored` witness,
and refuse external contents, accessors, unsupported regions and raw-frame
retention before changing a verdict. Automatic storage additionally needs the
plan's O-2 contents/type obligation and O-3/O-4 identity/frame obligations.
