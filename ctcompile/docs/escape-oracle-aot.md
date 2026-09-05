# AOT allocations in the escape oracle

Compiled frames have no bytecode program counter. Their `ip` holds either zero
or an encoded catch-pad identifier. Treating `ip - 1` as a source location can
attribute an AOT allocation to an unrelated JavaScript object-literal site.
The shared unwinder previously also adjudicated compiled allocations even
though `FrameEnds.def` declares AOT exits uninstrumented.

Compiled allocations now use the reserved `compiled_pc` value, 4294967294.
It remains an ordinary numeric coordinate in recording format v2, which the
existing checker reports as **UNCLAIMED**. A compiled return can now supply an
explicit result handoff and observe lifetimes without claiming a source site.
Legacy exits, failed exits and mixed AOT/VM unwind remain **UNCHECKED**.

`EscapeOracleAOT.cpp` executes real AOT entries with returned objects, thrown
objects and an encoded catch pad whose low bits collide with a static object
allocation. Six compiled allocations produce four unchecked site records.
An interpreted child still records its thrown object as escaping, and a
following interpreted local object remains confined. Recording must leave
collection counts and the live heap unchanged. The companion checker test
reads the unchanged format and requires exactly four unclaimed sites.

The additive `ct_aot_leave_return(frame, result)` ABI takes the value after
`ct_aot_return_value` applies constructor semantics. The boxed EmitC backend
normalizes the result, hands it to the exit helper, then writes the caller's
output. The helper removes the ending frame, roots the result temporarily, and
runs the existing bounded mark before releasing the register span. It neither
collects nor invokes JavaScript. The ending frame's registers, receiver and
closure are excluded from the root walk.

Only an ordinary return with exactly that frame on top opts into observation.
The existing `ct_aot_leave(frame)` ABI is unchanged, including handwritten
entries and failure cleanup. The shared unwinder still leaves compiled
allocations unchecked. A separate per-function compiled check counter uses the
configured budget without spending the interpreted function's checks; the
format-v2 total check count includes both tiers.

`EscapeOracleAOTReturn.cpp` runs bodies generated from
`escape-oracle-aot-return.js` by the boxed pipeline. It distinguishes returned
objects and their children from local objects left in dead frame slots, and
checks the child of a constructor receiver when the body returns a primitive.
That case calls the generated ABI without a caller root for the receiver, so
passing the raw primitive to the hook would incorrectly report the child confined.
Legacy and deliberately failed exits remain unchecked. Budget 1 checks only
the first compiled call while a later interpreted call of the same function
still receives its own check. A companion checker test requires the five
compiled site groups to remain unclaimed in the unchanged recording format.

The generated-body test and its checker pass on the devbox. Two temporary
generated-code mutations confirm that the assertions are sensitive: restoring
legacy leave produces twelve failed assertions, while handing off the raw
constructor primitive produces two. Neither mutation changes the runtime or
the tracked generated source.

Regenerating boxed Bootstrap passes the C++ syntax check. Its 657 normal
returns gain the same handoff; applying only that expected rewrite to the
previous output reproduces every byte. The generated hash changes from
`8dfd8e45a6a69a032c6f7b325573dc584f130989f81e49e6805e7c5faa71a6ba` to
`6847477849e52f51b8369b8e9d969c223fd647d881970003b1541c76fc67ec8a`.

These are observations of boxed AOT lifetimes. Static comparison still needs
compiled source coordinates. They do not broaden native escape claims; the
native backend's owning environments continue to use standalone differential
execution and sanitizers.
