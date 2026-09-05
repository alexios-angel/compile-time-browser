# AOT allocations in the escape oracle

Compiled frames have no bytecode program counter. Their `ip` holds either zero
or an encoded catch-pad identifier. Treating `ip - 1` as a source location can
attribute an AOT allocation to an unrelated JavaScript object-literal site.
The shared unwinder previously also adjudicated compiled allocations even
though `FrameEnds.def` declares AOT exits uninstrumented.

Compiled allocations now use the reserved `compiled_pc` value, 4294967294.
It remains an ordinary numeric coordinate in recording format v2, which the
existing checker reports as **UNCLAIMED**. Their escape observations remain
**UNCHECKED** on both ordinary return and mixed AOT/VM unwind. They do not spend
the interpreted function's checking budget or join bytecode-site claims.

`EscapeOracleAOT.cpp` executes real AOT entries with returned objects, thrown
objects and an encoded catch pad whose low bits collide with a static object
allocation. Six compiled allocations produce four unchecked site records.
An interpreted child still records its thrown object as escaping, and a
following interpreted local object remains confined. Recording must leave
collection counts and the live heap unchanged. The companion checker test
reads the unchanged format and requires exactly four unclaimed sites.

This closes an oracle attribution bug. Checking AOT lifetimes still requires
source coordinates and a returned-value handoff at `ct_aot_leave`; its current
ABI does not provide those. The native backend's owning environments are
instead tested through standalone differential execution and sanitizers.
