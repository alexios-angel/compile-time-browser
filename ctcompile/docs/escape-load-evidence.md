# Direct loads and candidate provenance

`EscapeVerdicts::directLoads` connects live top-level `ctjs.get_property`
operations to the [direct writes](escape-storage-evidence.md) whose targets
share a known local allocation site with the read's base. Each read keeps its
operation, base aliases and ascending, deduplicated indices into
`directStorage.writes`. The linked records expose the stored values and exact
write operands for subsequent contents analysis.

These are candidate links between allocation sites. They ignore property keys,
execution order and overwrites. A write after a read remains a candidate, as do
writes to other fields and different runtime objects allocated at the same
site in a loop. Joined bases retain every matching write once. Distinct local
sites do not match merely because both allocate objects or both allocate arrays.

`directLoads.complete` means that every live top-level property read has an
initialized base alias set and the direct-write census is complete. Missing
lattices, unsupported write targets, live nested regions and whole-frame
retention refusals preserve partial records with `complete=false`. Dead CFG
blocks contribute neither records nor refusals. An external base is resolved
evidence, so it does not clear this census marker, but it adds no local-site
link. An empty candidate list therefore does not establish absence of aliases.

The marker does not establish complete contents or points-to information.
Loads still produce the original external alias lattice, so this direct census
does not connect a second read through a loaded container to its writes. Prototype access,
accessors, calls, `copy_props` and other indirect transfers require additional
proofs. No native admission consumes these candidates, and they alone change no
escape verdict, including `Stored`. Consumers must recompute them after IR
changes; the records are not persisted as trusted attributes.

Focused unit controls cover initializer/append links, different keys and later
overwrites, distinct and joined local targets, carried external alternatives,
load-through-load limits, repeated loop allocations, dead blocks, missing
lattices, unsupported writes, nested reads and late argument capture. Two live
read-base mutations independently rebuild the solver and switch candidates
between local containers and an external parameter while preserving the stored
child's verdict and the read's external result lattice.

The claims executable reports candidate links, reads with links, linked stored
site edges, unresolved/external bases and complete/incomplete functions. The
CTest gate checks every live property read is covered and each link refers to
an ordered, valid write sharing a local site. Precision counts are diagnostic;
the existing execution oracle continues to gate zero false confinement.

## Preceding direct-census measurement, 2026-09-07

All **200 unit rows** and the two live-base mutations pass. The full devbox
gate passes **474/474 CTests**; the four corpus oracles report zero violations.
The direct-load census covers every live top-level property read, with zero
unresolved bases and zero invalid links in every corpus:

| Corpus | Read records | Reads with links | Candidate links | Stored-site edges | Complete/total functions |
|---|---:|---:|---:|---:|---:|
| Fixture | 43 | 19 | 35 | 4 | 43/47 |
| Bootstrap | 2946 | 48 | 138 | 0 | 486/588 |
| p5 | 38300 | 754 | 3047 | 539 | 4058/4703 |
| Phaser | 45873 | 571 | 7425 | 194 | 6657/7723 |

Stored-site edges count the tracked allocation sites on candidate writes, not
objects proved reachable through a load. Bootstrap's zero in that column means
these direct links currently identify writes without tracked stored allocations;
it is not evidence that property reads cannot expose allocated objects.
Incomplete function counts are **4/102/645/1066** respectively. These numbers
describe the census and do not authorize a change to any escape verdict.
Evidence: `/tmp/ctcompile-native-integrated-evidence.json`; see
[HANDOFF.md](HANDOFF.md) for the exact build baseline.

## Bounded candidate provenance

The separate `computeLoadProvenance` query now follows those diagnostic
candidates through local contents. It starts from the original alias lattices,
unions every direct stored value into each known target site, and imports those
candidates at property reads. It repeats after loads reveal additional stored
values or target containers. Successor operands and ODS carries transport the
candidates across branches and loops. This handles nested local loads, writes
through loaded containers, loaded values stored into another container, and
self or mutual cycles.

Every original external alternative remains present. Keys, execution order,
overwrites and distinct dynamic instances of one allocation site still do not
filter candidates. Branch copies retain each successor's actual operand list,
including repeated edges to the same block; an edge between two live blocks is
included conservatively even if only another incoming edge was executable.
Dead blocks contribute no constraints or exposures.

The query returns candidate reads, candidate writes, and **every live top-level
sink operand** with its operation, position, reason and candidate aliases. A
child's first verdict can remain `Stored` while its later loaded alias appears
in both a global store and a return. An earlier `Passed` verdict likewise does
not hide the later stores or loaded throws. The query changes neither the
original `AliasLattice` nor any escape verdict, and native admission does not
consume it.

The default propagation limit is 100,000 constraint visits/site joins per
function. `converged` says the supported constraint graph reached a fixed
point; exhaustion preserves all census records and partial candidates with
`converged=false`. `inputsComplete` separately records initialized lattice
inputs and the original direct-census coverage. Missing lattices, unsupported
storage targets or successor operands, nested regions and raw-frame refusals
keep it false. Neither flag, separately or together, establishes complete
contents or permits treating absent candidates as proof of confinement.
Accessors, prototypes, `copy_props`, calls and other indirect transfers are
still outside this query's modeled contents graph.

Nine additional unit rows cover the propagation paths above, cycle identity,
later writes, dead returns and earlier call sinks. Existing controls also
assert incomplete inputs for missing lattices, unsupported targets, regions
and late arguments capture. All three live read-base states independently
rebuild the solver and query, check the newly exposed return, and exercise
every budget below the actual completion point plus the exact completion
budget. The claims executable reports candidate read/store/exposure edges,
newly propagated exposures, convergence, incomplete inputs and work. The CTest
gate checks preservation of original candidates and coverage of all reads and
sink operands; precision and convergence counts remain diagnostics. The table
above measures the preceding direct census, not this new closure.

## Measured provenance gate, 2026-09-07

The focused and full devbox runs pass **209 unit rows**, including all three
live-base states and every incomplete work budget. All four execution oracles report
zero violations. The claims gate reports zero invalid provenance records and
covers every live top-level read and sink operand:

| Corpus | Reads | Sink operands | Read-site edges | Stored-site edges | Exposure-site edges | Newly propagated exposure edges |
|---|---:|---:|---:|---:|---:|---:|
| Fixture | 43 | 506 | 4 | 14 | 43 | 4 |
| Bootstrap | 2946 | 15551 | 0 | 400 | 624 | 0 |
| p5 | 38300 | 154902 | 557 | 1435 | 3930 | 324 |
| Phaser | 45873 | 238652 | 180 | 1936 | 4307 | 52 |

Every supported constraint graph converges within the default per-function
limit: **47/588/4703/7723 functions**, respectively, with zero exhausted queries.
Input completeness is independently **43/486/4058/6657** functions, leaving
**4/102/645/1066** incomplete. Total work across each corpus is
**1356/123846/1302930/1124814**. Convergence does not repair incomplete inputs or
prove complete contents. Bootstrap still has no newly propagated exposure-site
edges; the implementation does not imply a measured Bootstrap precision gain.
The full generated gate passes **475/475 CTests** in **652.00 seconds**.
Evidence: `/tmp/ctcompile-arguments-evidence.json`; logs:
`/tmp/ctcompile-arguments-integrated-focused4.log` and
`/tmp/ctcompile-arguments-full-gate.log`. See [HANDOFF.md](HANDOFF.md) for the
browser baseline.

## Complete own elements for a bounded local subset

`computeArrayContents` is a separate prerequisite that reads the current
verified IR directly, without alias lattices, candidate links or trusted
annotations. A successful result establishes exact dense own elements for
fresh local arrays on every path through a proved acyclic graph of `cf.br`
and `cf.cond_br` branches. Constants and fresh property-free objects can be elements. Inline
initializers, literal `ctjs.append`, initialized
constant-index reads and overwrites are supported. A read resolves to the
original constant or allocation; reading an array and writing through that alias
updates the same array state.

The result records every initializer/append/overwrite and its actual operand,
every read's value at that point, exact final slot contents per return path,
and every local allocation reachable from each return value. Earlier reads retain their original
values after replacement. Return reachability follows current slots, visits each
allocation once, and handles self and mutual cycles. This identifies identities
and edges; it does not select an owner, make a cycle collectable, or change the
legacy `Stored` verdict.

The proof requires a known initialized own Number index. Number `-0` refers
to index zero; fractions, NaN, infinity, negative numbers and `2^32-1` do not.
String indices, including canonical `"0"`, are now refused because the current
VM's named-property path does not access dense elements; see the measured
boundary below. Generic writes
may overwrite existing elements only. Extending a generic property write, reading
an absent slot, deleting elements, accessing named properties, changing prototypes
or defining accessors refuses the entire result. Literal append uses the existing
internal construction operation, not a call to a potentially modified `push`.

Unknown values and bases, all calls and global accesses/publication, cells,
unsupported carriers, loops, nested regions, arguments/rest builders and
suspension also refuse. `ctjs.truthy` may observe an unknown external predicate:
its total, noncapturing operation proves only an `i1`, never the input's contents.
Both conditional edges are checked even when their predicate is constant. Even a late unrelated call invalidates the
proof. `ctjs.throw` is excluded because uncaught diagnostic formatting may call
`toString` and reenter JavaScript. Return is the only supported exit.

The default budget is 100,000 operation, forwarded-argument, initializer-element,
branch-state-copy and exit graph visits. Array key validation examines one Number.
Only a completed function scan and return graph publish `complete=true`. Unsupported
operations or exhausted work return a named failure and witness operation with
**no proof records**, including when all reads were already checked. Every IR
mutation invalidates previous records; callers must recompute.

Focused controls cover mutation order, saved reads, loaded bases/keys/values,
empty arrays, primitive returns, cycles, all listed refusal classes, and canonical
index boundaries. Seven live IR states change a replacement, key, base and late
publication, including a forged completion attribute. Each case checks every
budget below its actual completion/refusal point and the exact budget, while
preserving legacy load lattices and escape verdicts. These are unit proof
controls, not a new measured corpus precision result.

The focused devbox gate passes **31 contents rows, 14 index controls and seven
live mutation states**, alongside the unchanged **209 escape rows**. All four
existing execution oracles pass with zero violations. The integrated seven-test
gate, including host and ownership units, passes in **18.26 seconds**; log:
`/tmp/ctcompile-map-presence-integrated.log`. The full generated devbox gate
passes **475/475 CTests in 693.86 seconds**, including all four oracles; log:
`/tmp/ctcompile-map-presence-full.log`. These measurements do not claim a corpus
precision increase.

## Bounded retention consumer

`computeVerdicts` now independently recomputes the complete local-array query
before discharging a `Stored` verdict. Every operation must belong to the
supported subset above, and the exact allocation must be absent from the
returned value's complete reachability graph. A private array containing a
local object can therefore leave that object `Confined`; returning the array
keeps the child `Stored`. Returning a saved read also keeps the original child
`Stored`, even after its old slot was overwritten. Loaded array aliases and
loaded values stored into a second returned container retain their identities.

The consumer requires an acyclic graph over **all writes**, including writes
subsequently overwritten. Self cycles, mutual cycles and transient cycles
preserve every original verdict. The graph may share children and contain
repeated edges. A returned child keeps its original `Stored` witness; it is
never relabeled as uniquely `Returned`. This increment chooses no graph owner.

Missing solver lattices, whole-frame refusals, incomplete census evidence or
any unsupported contents operation prevent refinement. A separate 100,000-step
default limit includes the contents query, all-write graph construction,
acyclicity traversal, returned-site collection and the complete verdict scan.
No verdict changes until all work finishes. A zero limit preserves the original
sink-table verdicts. `arrayRetentionComplete`, `arrayRetentionWork` and
`confinedStoredSites` report whether the query completed, its work and the number
of discharged sites; incomplete work never publishes a partial refinement.
No stored annotations authorize this proof, and every IR change requires a
new solver and query.

The unit suite adds eighteen retention rows and seven live mutation states,
checking every budget below actual completion/refusal and the exact budget.
Controls cover indirect returns, overwritten values, nested/shared children,
late publication, external keys/bases, forged markers and missing lattices.
All existing unsupported contents rows also assert that the default consumer
preserves original verdicts. The 209 sink-table rows explicitly disable the
refinement so they continue to check ODS classifications independently. The
devbox gate passes all eighteen rows, seven mutation states and **454 budget
cutoffs**, alongside the 31 contents rows and 209 sink-table rows. All four
existing execution oracles report zero violations. The combined Map/escape
gate passes **7/7 CTests in 22.54 seconds**; log:
`/tmp/ctcompile-map-keyfacts-units.log`. Corpus precision is unchanged in this
gate. The full frozen generated build passes **475/475 CTests in 720.98 seconds**,
including all four oracles; log: `/tmp/ctcompile-map-keyfacts-full.log`.
Preceding measurements above describe the prerequisite only.

No native admission consumes these verdicts. Complete contents for general control
flow, external values and other containers remain unfinished. Native automatic storage
still requires the plan's separate type, identity, cycle ownership and
frame-lifetime obligations. Corpus precision improvements require measurement;
these unit cases do not establish a Bootstrap gain.

## Imported frame and root bookkeeping

The complete query now checks the optional frame carried by raw imported
functions. `ctjs.frame_enter` must be the first operation, with a nonnegative
register count and no second frame. Every root must name that exact active
frame and a value already proved by the contents query. A matching
`ctjs.frame_exit` must immediately precede every return on its own path. Missing, repeated, late
or foreign frame operations refuse the whole proof, as do unknown root values
and unknown users of the frame handle. Refusal publishes no partial contents
and preserves every original escape verdict.

The current query follows every structural path through an acyclic graph of
`cf.br`, `cf.cond_br` and `cf.switch` operations, with no nested regions. Every
forwarded argument must resolve to an already proved constant, allocation, earlier read,
truthy predicate or active frame handle. No external alternative is dropped even
when the destination never uses it. A join keeps separate exact states for each
incoming path; repeating a block within one path and unknown successor semantics
refuse. The complete path scan proves every unvisited block unreachable directly
from current IR, including the importer's default-return block after an explicit
source return. Dead blocks contribute no contents, effects or return
roots. No solver reachability flag or annotation supplies this exclusion.

This is a retention proof, not an effect summary: frame entry still has its
depth-failure path. Its checked position establishes that no tracked allocation
from this function exists on that path. Successful frame exit kills only this
frame's roots; the independently computed return graph still retains every
returned child. Calls, publication, raw arguments retention and the other
unsupported effects cannot borrow this bookkeeping proof. Native admission
does not consume it or acquire any dependency on interpreter frames.

Nineteen frame controls and eight live mutation states exercise both the complete
contents query and the default retention consumer, including every incomplete
budget and its exact endpoint. Live edits move entry/exit, change a root to an
unknown parameter, insert late publication and make a previously dead
publication block reachable while forged completion markers remain present.
Restoring the actual supported IR permits refinement again.

The source fixture adds nine executed functions, with nineteen allocation
sites and twenty-one measured instances. It covers private arrays called
twice, returned containers, saved reads after overwrite, overwritten children,
writes through loaded array aliases, late publication/calls, and final/transient
cycles. The claims gate joins observations and claims by program, function and
bytecode coordinate and checks exact retention routes. Its eleven
retained instances must keep their escape claims; final and transient cycles
stay conservatively `Stored` even when observed confined. The source checks
also require the newly discharged private/overwritten children to be `Confined`,
so disabling the imported-frame proof cannot pass vacuously.

The preceding serialized devbox gate passes **8/8 CTests in 8.45 seconds**: all four
escape units and all four source/oracle comparisons. The frame controls cover
**19 rows, eight live states and 248 retention budget cutoffs**; the existing
contents and retention controls pass **32/18 rows**, respectively. Every oracle
reports zero soundness violations. Log: `/tmp/ctcompile-size-escape2.log`.
The initial source run exposed the importer's dead fallback block; the final
proof supports that shape without relaxing any source precision expectation.

The next contents boundary is loop control flow, external stored/returned
values and other containers, with native ownership/type consumers still separate.
Bootstrap/p5/Phaser observed precision remains **0/64, 0/16, 0/20** in these
script-mode runs; this focused source improvement does not establish a corpus
precision gain or full application coverage.

The unconditional-chain increment adds controls for exact branch argument order,
duplicate array aliases, saved reads across later overwrites, successor-local
allocations, transported frame handles and block layout differing from execution
order. Negative controls retain joins with dead predecessors, loops, conditional
flow, opaque successors, unused external arguments, late entry/early exit and
successor-frame capture. Twelve live frame/control-flow states move a publication
block onto the reachable chain, remove/reinsert its publication and replace an
exact forwarded array with an external parameter while forged markers persist.
Every contents and retention case checks all incomplete budgets and the exact
endpoint. Source fixtures and their existing precision assertions are unchanged.
The combined serialized devbox gate passes **12/12 focused CTests in 28.45
seconds**. Array controls pass **35 contents rows, 20 retention rows and 26
frame rows**, with **12 live frame/control-flow states**, **500 retention
budget cutoffs** and **387 frame budget cutoffs**. All four execution oracles
report zero violations; fixture precision stays **22/33** and corpus precision
is unchanged. Log: `/tmp/ctcompile-cardinality-focused2.log`. These results
cover the unconditional-chain increment; the preceding measurements describe
the entry-only proof. Native ownership consumers remain separate.

## Acyclic conditional paths

The conditional increment explores both `cf.cond_br` edges and replays a join
separately for each incoming path. Each path owns its exact origin map, current
array slots and frame state. A conditional target can therefore denote either
of two arrays without an overwrite erasing a child from both arrays. Saved
reads keep their earlier origins, and allocations at a shared successor retain
their path-specific elements. The top-level array list inventories sites once;
each exit carries the exact final contents and reachable allocations for that
path. The same return operation may produce several exit records. Reads and
writes include every visited alternative, including repeated successor operations.

Retention unions reachable sites across all exits. An old child is discharged
only when every supported path excludes it. The acyclicity requirement still
uses the conservative union of all writes, including transient edges and edges
from mutually exclusive paths. This increment does not select an owner for any
cycle. An unsupported operation, unknown forwarded/stored value, missing own
slot, invalid frame or loop on either edge discards all earlier path records.
Even a constant predicate cannot hide an unsupported edge; no solver liveness
flag or completion attribute supplies the proof.

Path enumeration is bounded by the existing work limit. Before copying a branch
state it charges every origin, visited block, array and stored element. This
bounds the cost of a compact CFG with exponentially many paths. Exhaustion,
including after earlier paths already returned, publishes no contents and
leaves every original escape verdict intact.

Controls add **17 conditional rows, five live mutation states and five
path-explosion cutoffs**, alongside the existing **35 contents, 20 retention
and 26 frame rows**. They check exact per-path slots/reads/returns, overwrite
selection through duplicate successor edges, saved reads, successor-local and
join-local allocations, imported frames, late effects, external alternatives,
missing slots and conservative cycle refusal. Every conditional row and live
state checks all incomplete contents/retention budgets and its exact endpoint.
The live second-path edits keep forged markers while changing the actual
replacement and inserting/removing publication. The exponential-path fixture
checks both contents and unchanged retention at five bounded budgets.

The combined devbox build and **12/12 focused CTests pass in 29.38 seconds**.
All seventeen conditional rows and five live states pass, including **738
retention budget cutoffs** and all five path-explosion cutoffs. Existing
controls pass **35 contents rows, fourteen key controls and seven live contents
states**; **20 retention rows, seven live states and 500 budget cutoffs**; and
**26 frame rows, twelve live states and 387 budget cutoffs**. Log:
`/tmp/ctcompile-payloads-focused.log`.

All four execution oracles report zero violations. Fixture precision remains
**22/33**; Bootstrap, p5 and Phaser remain **0/64, 0/16 and 0/20**, respectively.
The p5 oracle retains its one partial observation, without a soundness violation.
Existing source fixtures and precision expectations are unchanged; this unit
precision increment does not establish a corpus gain. Homebrew clang-format
22.1.8 and whitespace checks pass locally. The full generated gate completes
**512/517 CTests in 808.06 seconds**, including **372/372 compiler** and
**164/164 lit cases**. Only the five recorded browser failures remain; no browser
source changed. Log: `/tmp/ctcompile-payloads-full.log`; fresh oracle evidence:
`/tmp/ctcompile-payloads-evidence.json`. Native ownership consumers remain separate.

## Fixed own properties on fresh objects

The complete query now includes ordinary `ctjs.create_object` containers with
bounded constant String keys. A write establishes or replaces an own data
property; a read requires an earlier write to that exact key on the current
path. Missing own properties remain refused, including names supplied by a
builtin or prototype. Numeric, Boolean, external and otherwise coercing keys
are not accepted. Strings may be empty, contain NUL or look like indices;
their byte length is limited to 256. `__proto__` is explicitly refused.

This follows the existing `Containers.td`/`Properties.td` fresh-object contract:
the object has a null explicit prototype, no accessors and writable/extensible
own data. The proof still refuses every prototype/descriptor operation, call,
global access, publication and unsupported effect. It does not infer an
unchanged prototype from a marker or use a missing-field VM result as evidence.
No runtime or native-admission behavior changed.

Each path records its exact object fields alongside its arrays. Reads retain
their original values after replacement, and object aliases loaded through
arrays or other objects update the same current instance. Return reachability
crosses both field and element edges. The retention consumer checks the union
of all writes across both container kinds, including overwritten edges and
mutually exclusive paths, before discharging a single `Stored` verdict.
Self, mixed and transient cycles therefore keep every original verdict.

Branch snapshots charge each object and own property before copying it. Every
incomplete budget or unsupported operation discards all object, array, read,
write and exit records; the retention transaction also preserves all original
verdicts. Keys are interned exact String attributes, so equal bytes in separate
constants cannot create distinct fields. Current IR is rechecked on every query.

The focused gate passes **31 object rows, eleven key controls and twelve live mutation
states**, with every incomplete contents/retention budget and exact endpoint.
They cover field replacement, saved reads, mixed-container aliases and returns,
conditional target identity, absent properties, late effects, frame bookkeeping,
cycle refusal and live changes under forged completion/confinement markers.
Existing source fixtures and precision expectations are unchanged. Homebrew
clang-format 22.1.8 and whitespace checks pass. These controls are not a measured
corpus precision increase. Loops, external stored values, coercing keys, absent own
reads and other container kinds remain outside the proof; native type,
identity, cycle ownership and frame-lifetime consumers remain separate.

The initial object gate passed, but a subsequent source audit exposed an older
array-index assumption that this extension made easier to reach. In
`/tmp/ctcompile-object-key-oracle.json`, both a direct `a['0'] = next` and an
object-loaded String key produce **Node `trace=2`, interpreter `trace=1`**;
the interpreter leaves the old dense element unchanged. Reading through an
object-loaded String key produces **Node `trace=1`, interpreter
`trace=undefined`**. Current VM `lookup_index`/`store_index` use dense array
slots only for Number keys, and the named-property paths do not access them.
The compiler now refuses String array reads and writes rather than claiming
which child was read or replaced. The VM and source oracle expectations are
unchanged. Direct and object/array-loaded String key refusals, plus live
Number-to-String-to-Number changes under forged markers, guard this boundary.
The revised array contents suite passes **38 rows**; all incomplete work
budgets continue to publish no proof and preserve the original escape verdicts.
The final focused devbox gate passes **12/12 CTests in 28.30 seconds**, including
**1269 object-retention cutoffs**, 14 array keys, 20 retention rows/661 cutoffs,
26 frame rows/444 cutoffs and 17 conditional rows/877 cutoffs with five
path-explosion controls. All four execution oracles report zero violations.
Fixture precision remains **22/33**; Bootstrap/p5/Phaser remains **0/64, 0/16,
0/20**. Log: `/tmp/ctcompile-object-focused2.log`. The complete generated rerun
passes **372/372 compiler CTests**, including **165/165 lit cases**; overall
**512/517** leaves only the five recorded browser failures. Final log:
`/tmp/ctcompile-mixed-full2.log`; evidence: `/tmp/ctcompile-mixed-evidence.json`.
The full checkpoint and next native boundary are in [HANDOFF.md](HANDOFF.md).

## Bounded switch paths

The complete contents query now explores `cf.switch` default and case edges,
with a separate exact origin, array, object and frame state for every edge.
Repeated destinations preserve their individual successor operands. Default
and cases are visited in source order, with each case snapshot charged before
copying. A default-only switch forwards its state without copying it.

The selector must already have an independently known origin, currently an
`i1` from `ctjs.truthy` or an exact forwarding of it. This adds no selector
producer or JavaScript conversion. Every structural edge is checked even when
the flag is constant or the listed cases exhaust its possible values; default
is not omitted by an exhaustiveness argument. Unsupported operations, absent
own slots/properties, unknown operands, invalid frame operations and loops on
any edge discard every earlier contents record. The retention consumer still
requires an acyclic union of all writes, including transient and mutually
exclusive array/object edges, before changing any `Stored` verdict.

The unit suite configures **21 switch rows, six live states, four malformed
controls and five path-explosion cutoffs**. The rows cover same-destination
edges with different target/value operands, saved children after overwrite,
shared-successor allocations, own-object reads, mixed-container aliases,
return reachability, frames, unknown selectors and late effects. Live edits
change the final case's operands under forged completion/confinement markers,
then restore the real proof. Direct queries reject missing successor operands,
an empty target and an unknown selector without passing malformed IR to the
legacy solver. Every row and live state checks every incomplete contents and
retention budget plus its exact endpoint; the malformed queries do likewise
for contents. A ten-block, three-edge-per-block case checks bounded rollback
through an exponential path graph with both arrays and object properties.

The combined devbox build and focused CTest gate pass **12/12 in 29.05 seconds**.
The switch rows pass all **1526 retention budget cutoffs**, six live states,
four malformed controls and five path-explosion cutoffs. Existing array,
object, conditional and frame cases still pass. All four execution oracles
report zero violations; fixture precision remains **22/33** and Bootstrap,
p5 and Phaser remain **0/64, 0/16 and 0/20**. Log:
`/tmp/ctcompile-saved-checkpoint.log`. Homebrew clang-format 22.1.8 passes all
742 files; whitespace checks pass. The full 243-step generated build succeeds;
CTest passes **372/372 compiler tests**, including **165/165 lit cases**, and
**140/145 browser tests**. Overall **512/517 in 864.73 seconds** leaves only
the five recorded browser failures. Final log: `/tmp/ctcompile-saved-full.log`;
evidence: `/tmp/ctcompile-saved-evidence.json`.
Source fixtures, runtime behavior and native ownership admission are unchanged.
Loops, external contents, additional selector producers and native lifetime
consumers remain separate work.

## Fixed own-field deletion

The complete contents query also models `ctjs.delete_property` and
`ctjs.delete_named` on proved fresh ordinary objects. Computed keys must have an
exact String origin; named keys use their current String attribute. Both forms
use the existing 256-byte limit and explicit `__proto__` refusal. Each deletion
records its operation, object, exact key and removed value, or an absent value
when there was no own field. Deletion of an absent own field is a no-op; a later
read of that absent field still refuses the whole proof.

The runtime contract was checked against `Properties.td`, `Runtime.td`,
`ctbrowser/lib/Script/vm/objects/descriptors.cpp` and
`ctbrowser/include/ctbrowser/script/value.hpp`. Fresh assignment creates
configurable own data (`attr_default`), and both delete paths remove exactly
that own property without consulting a prototype or invoking a getter. All
descriptor, prototype, call, publication and unknown-value effects remain
outside this complete query. Arrays remain refused for both deletion forms:
the current VM does not represent deletion holes, so an object erasure proof
cannot supply array semantics. No runtime behavior or native admission changes.

Only the current path's property snapshot loses the field. Saved reads,
including object aliases and String keys, retain their original values.
All historical writes remain in the conservative cycle graph; removing a
self-edge or mixed array/object edge cannot discharge cycle ownership. Return
reachability still unions every structural conditional/switch path, so an
undeleted alternative retains its child. Before an actual erase, the query
charges the entire current property set for `MapVector`'s linear update.
Refusal or exhaustion discards every deletion record along with all earlier
contents, and retention remains transactional through its final verdict scan.

The devbox unit suite passes **30 deletion rows, eighteen key controls, fourteen
live mutation states and one missing-lattice control**. It checks exact own
snapshots, historical writes, removed/absent values, return routes and
discharged sites. Every row and live state checks every incomplete contents
and retention budget and the exact endpoint. Live edits change deletion keys,
bases and a later branch's named key, insert publication or a missing read,
and restore valid IR while forged completion/confinement markers remain.
There are **1663 incomplete retention budget cutoffs**. Three new executed
functions cover five sites and five instances: a deleted child, a saved child
returned after deletion, and a deleted self-cycle. The two retained instances
keep their escape claims; the observed-confined cycle remains `Stored`.
Two allocation sites receive `Confined` claims. Earlier source families keep
their exact expectations. The expanded fixture measures **24/36** precision;
Bootstrap/p5/Phaser remain **0/64, 0/16, 0/20**. All four execution oracles
report zero violations.

The combined focused devbox gate passes **12/12 CTests in 29.32 seconds**.
Homebrew clang-format **22.1.8** passes all **743 files**, and whitespace checks
pass. Log: `/tmp/ctcompile-conditional-checkpoint2.log`. The full generated
gate is recorded separately in [HANDOFF.md](HANDOFF.md).

## Fresh own-data object copies

The complete contents query now accepts `ctjs.copy_props` only when both
operands independently resolve to fresh ordinary objects in the current path.
Every source property already has an exact bounded String key and a known
primitive or allocation origin. Copying establishes or replaces those keys on
the target, preserves unrelated keys and leaves the source unchanged. Deletion
before copying removes a key from the copied mapping; deletion or replacement
after copying does not alter the earlier copied value. Reads saved from either
object retain their original identities and values.

The runtime audit uses `ctbrowser/lib/Script/vm/objects/chain.cpp` and
`ctbrowser/include/ctbrowser/script/value.hpp`. `copy_own_properties` snapshots
enumerable own entries through `lookup_property`, so accessors can run in the
general runtime operation despite the older no-accessor comments in its ODS
and AOT bridge. This complete proof excludes descriptors, accessors, prototype
changes, unknown effects and external values. Every admitted field therefore
has ordinary enumerable, writable, configurable own-data semantics. Array and
primitive sources/targets also remain outside this proof. No runtime or ODS
contract is changed or used to infer an absent getter proof.

Each copy charges a full source-property snapshot before allocating it, then
charges every copied field. Snapshotting also handles equal source/target
aliases. `ObjectPropertyCopy` records the source, target, key and exact copied
value separately from direct `Stored` operand witnesses: neither operand of
`copy_props` is the copied child. Return reachability follows current target
fields, and the cycle check includes every historical copy edge along with
all direct writes. A copied self-edge or mixed array/object cycle remains
outside retention refinement after deletion. Copying fields does not itself
retain the source object. Property mapping contents are exact; record order
does not prove `OwnPropertyKeys` enumeration order, whose consumers remain
unsupported.

Conditional and switch paths retain independent source/target identities and
snapshots. Every structural edge is checked, including literal-predicate arms.
Any unsupported path or exhausted budget discards all copied records and every
earlier contents record. The retention transaction still preserves all original
verdicts until its final graph and verdict visits complete. Forged completion
or confinement markers supply no evidence.

The devbox unit fixture passes **39 copy rows, eleven key controls, seventeen live
mutation states, one wide snapshot, one missing-lattice control and five
path-explosion cutoffs**. Every row and live state checks every incomplete
contents/retention budget and the exact completion/refusal endpoint. The wide
case checks all 66 indirect edges through a 33-field copy followed by self-copy.
Live controls replace source/target identities, create a copied self-cycle,
insert deletion, accessors or publication, and mutate a later path while
forged markers persist. All **2794 incomplete retention budgets** pass. Three
executed source functions cover ten sites and ten instances: copied children
retained by a returned target, overwritten copied children, and a saved child
after deletion from both objects. The five retained instances keep their escape
claims; the other five receive `Confined` claims. Earlier source families retain
their exact expectations.

The expanded source fixture measures **29/41** precision; Bootstrap/p5/Phaser
remain **0/64, 0/16, 0/20**. All four execution oracles report zero violations.
The combined focused CTest gate passes **12/12 in 30.19 seconds**, and Homebrew
clang-format **22.1.8** passes all **743 files**. Whitespace checks pass. Log:
`/tmp/ctcompile-guard-copy.log`. This adds no native ownership admission. The
full generated gate is recorded separately in [HANDOFF.md](HANDOFF.md).

## Executed copy paths and the raw-import boundary

Four additional source functions exercise copy behavior through real imported
frames. Both flags of the conditional alias case execute: replacing and deleting
the selected source or target leaves the old child reachable through the other
object in the returned graph. A second conditional chooses the copy source,
then deletes both original fields; each child is retained on exactly one call.
The switch case executes `0`, `1` and default. Its saved copied child survives
every arm, while the replacement and original containers are retained only by
the graphs returned from the corresponding case.

These are deliberately conservative source controls. The raw importer forwards
the entire register vector to every successor. An external predicate parameter
therefore remains an unknown forwarded value even though `ctjs.truthy` itself
is allowed by the complete query. JavaScript `switch` also emits strict equality,
Boolean conversion and conditional jumps; it does not import as `cf.switch`.
Those comparison/conversion producers remain outside complete contents. The
existing unit switch result does not establish source-switch precision.

A fourth, parameter-free function supplies a literal conditional control for
the existing complete path proof: after selected-alias replacement, both
original and copied fields are deleted before the replacement is returned.
The old child is absent on both structural arms, including the untaken one.
The checker joins every new observation to its independent compiler claim by
program hash, function index and bytecode PC, keeping every earlier family's
exact expectations unchanged. The devbox source oracle passes **21 sites,
39 instances and 23 retained instances**, including the conservative
dynamic-path claims and the literal control's old-child confinement.

Local Node syntax and execution checks pass for all **eight calls**, with four
explicit identity assertions. Eight altered variants fail those observations:
forced alias or source selection, deletion from both possible alias targets,
copy/source aliasing, loss of the saved child, a saved read recomputed after
overwrite, a changed default return and returning the replaced literal child.
Node evidence: `/tmp/ctcompile-escape-copy-paths-node.js`.

All four devbox execution oracles report zero soundness violations. The expanded
fixture measures **34/47** precision, with zero partial or pending claims;
Bootstrap/p5/Phaser remain **0/64, 0/16, 0/20**, including p5's existing single
partial observation. The change from **29/41** adds five proved-confined sites
and six observed-confined sites in the new fixture. It measures the existing
analysis on additional source witnesses; no production analysis changed and
none of the corpus precision counts improved. Homebrew clang-format **22.1.8**
passes all **744 files** in the gate snapshot, and whitespace checks pass.
Log: `/tmp/ctcompile-shortcircuit-focused.log`. The full **252-step generated
build succeeds**, with **372/372 compiler CTests** and **165/165 lit cases**
passing. Overall **512/517 in 953.39 seconds** leaves only the five recorded
browser failures. All four oracles repeat the same zero-violation measurements
and precision counts. Final log: `/tmp/ctcompile-shortcircuit-full.log`;
evidence: `/tmp/ctcompile-shortcircuit-evidence.json`. See [HANDOFF.md](HANDOFF.md).

The next small source-boundary proof is exact transport of an external
predicate through unused raw register arguments without treating that value as
known container contents, a retained root or an allowed unknown effect.
Comparison/Boolean selector producers for source switches are a separate
increment. Both need refusal and budget controls before dynamic source claims
can improve. No runtime behavior, production analysis or native ownership
admission changes in this source-only increment.

## Opaque entry-register transport

The interrupted `codex-escape-predicate-transport` thread from the
2026-09-08 **11:17:14** synchronization journal is resumed here. It left clean
files and was explicitly abandoned by the **11:17:55** loop journal. The
preceding source-only checkpoint identified unused raw parameter registers as
the next boundary; this increment changes the complete contents query.

Every entry `!ctjs.value` receives an opaque identity in a separate map.
`cf.br`, `cf.cond_br` and `cf.switch` successor operands may carry that exact
identity through any number of acyclic register vectors. The existing total,
noncapturing `ctjs.truthy` may observe it. Known constants, local allocation
identities, loaded origins and the active frame remain in the original map.
Each structural path keeps both maps independently, so a local/opaque join
cannot lend the local alternative's contents to the opaque one.

Opaque entries never become initialized array elements, object fields, roots,
return values, property keys, array indices or copy endpoints. Unknown effects,
calls and publication still refuse the whole result, even when their only
operand is opaque. Unknown producer operations cannot create transport
evidence. Non-`!ctjs.value` entry arguments remain unproved, preserving the
selector-origin controls. No new comparisons, Boolean conversions or source
switch producers are admitted. Every failure discards all earlier proof
records and preserves the original escape verdicts.

The work limit charges every entry-argument visit, forwarded operand and
opaque-map snapshot entry. The devbox unit suite passes **nineteen rows**,
**eight live mutation states** with forged markers, and a wide snapshot whose
32 extra unused entry arguments must cost exactly 64 additional work units:
one seed and one snapshot visit per argument. Every row and live state sweeps
all incomplete contents and retention budgets plus the exact endpoint. Three
historical unused-forwarding rows now expect complete contents; their source
graphs and all other historical rows remain. All **664 incomplete retention
budgets** pass. Existing path-explosion controls also exercise the additional
opaque-state charges.

The executed source fixture keeps its four copy-path functions, eight calls,
and existing observations. Its conditional-alias replacement now receives
a `Confined` claim after replacement and deletion on either selected target;
the old child still stays `Stored` because the other object retains it.
Conditional-source children retain their existing path-dependent escape
claims. Real source-switch claims remain conservative. Measured precision on
the unchanged fixture improves **34/47 -> 35/47**, with zero partial or pending
claims. Exact source-coordinate checks retain **21 sites, 39 instances and
23 retained instances**. All four execution oracles report zero violations;
Bootstrap/p5/Phaser precision remains **0/64, 0/16, 0/20**, including p5's
existing single partial observation. This is one fixture-site precision gain;
the corpus precision counts do not improve.

Commit **`6f13212`** records the implementation and witnesses. All **eight escape
CTests pass** in `/tmp/ctcompile-nullable-escape.log` (**8.07 seconds**).
After correcting two new host fixtures, the combined focused CTest gate passes
**12/12 in 34.43 seconds**, log `/tmp/ctcompile-nullable-native-focused.log`.
The final **252-step generated build** succeeds without warnings. Full CTest
passes **512/517 in 969.20 seconds**, all **372 compiler tests**, with only the
five recorded browser failures. All **165 lit cases** pass. The four execution
oracles retain the precision counts above and report zero violations in the
final run. Evidence: `/tmp/ctcompile-nullable-full.log` and
`/tmp/ctcompile-nullable-evidence.json`.
Homebrew clang-format **22.1.8** passes all **745 files** in the frozen input.
Node syntax, the eight-call copy-path observations, four identity assertions,
eight discriminating mutations and whitespace checks also pass. The parent
session committed the change and verified all 28 session code/test paths
against committed HEAD, frozen input and final devbox source. This escape
increment changes neither runtime behavior nor native ownership admission.
Comparison/Boolean selector producers for real source switches remain the next
bounded escape proof.

## Noncapturing source-switch producers

This continues the exact next boundary in **`178f65e9`** and the **13:07:05**
synchronization journal after opaque entry transport landed in **`6f13212`**.
The complete contents query now admits only `ctjs.compare strict_eq` and
`ctjs.convert to_boolean` from their respective operation families.
Strict equality reads primitive contents or object identity without conversion;
ToBoolean inspects a tag without invoking `valueOf` or `toString`.
Both produce an independent primitive Boolean. Neither records its input as a
known origin, retains an input object, proves a comparison value or prunes an
edge. Loose equality, every relation and every other conversion still refuse,
even when another analysis or an input attribute claims a safe type.

The Boolean result can be forwarded, stored, returned or parked in a matched
local frame as a primitive terminal. Its opaque input still cannot become a
stored value, root, return value, property key or copy endpoint. A Boolean
producer is not a literal Number array index or an own String property key.
All structural arms, path-specific contents, historical cycle edges and
complete refusal behavior remain in force. The result origin uses the existing
budgeted map: every producer visit and every copied origin costs work. No
runtime behavior or native ownership admission changes.

The devbox unit family passes **28 rows**, **eleven live mutation states**
under forged completion/confinement markers, and a wide snapshot with 32 extra
Boolean origins costing exactly **64 additional work units**. Controls
cover primitive results, local and opaque operands, raw forwarding and roots,
all ten refused comparison/conversion kinds, unknown producers/effects,
retained final arms and forbidden uses of the original opaque value. Every
row and live state sweeps all incomplete contents and retention budgets and
the exact endpoint. All **1,079 incomplete retention budgets** pass.

The preceding four-function copy-path source family and its **eight calls**
are unchanged. Its source switch becomes supported, but every `Stored` site
there still has a retaining arm. The existing exact expectations remain
**21 sites, 39 instances and 23 retained instances**. A separate source
function deletes the original and copied child fields on every case/default
arm and returns a different container from each. Calls with `0`, `1` and
String `"0"` distinguish strict equality from a coercing comparison. Its new
source-coordinate checks observe **four sites, ten instances and three
retained instances**, with the old child confined on all three calls.

Local Node syntax and execution checks pass the combined **eleven fixture
calls**, the four historical and two new identity assertions, and an extra
object selector whose `valueOf`/`toString` counters stay **zero**. All eight
historical observation mutations and six new switch mutations fail their
expected observations. Evidence: `/tmp/ctcompile-escape-selectors-node.js`.

All **eight escape CTests pass in 8.35 seconds**, with all four execution
oracles reporting **zero soundness violations**. The expanded fixture measures
**36/48** precision, with zero partial or pending claims, versus the preceding
**35/47**. The new source witness contributes the additional proved-confined
site and observed-confined site; the historical source families and their
claims remain unchanged. This is additional fixture coverage, not a measured
precision gain on the old fixture. Bootstrap/p5/Phaser precision remains
**0/64, 0/16, 0/20**, including p5's existing single partial observation.
Focused gate: `/tmp/ctcompile-nullable-keys-escape.log`. These measurements do
not claim a completed full-suite gate.

The next bounded proof remains outside coercing comparisons: logical negation
and other total selector producers need their own effect, primitive-result,
refusal and budget evidence.

## Noncapturing logical negation

This resumes the next boundary recorded after **`643501db`** and in the
**`1c7985a5`** handoff. The complete contents query admits only `ctjs.unary not`
from the unary family. `Operators.td`, the `logical_not` opcode contract and
`ctbrowser/lib/Script/vm/{run_loop,coerce}.cpp` agree: the operation inspects
truthiness and negates it, yielding an independent Boolean without allocating,
throwing, retaining the input or invoking `valueOf`/`toString`. The query records
no truth value, input alias or branch liveness. Every other unary kind still
refuses, including `typeof` and `void`, whose result proofs remain separate.

The Boolean may be stored, returned or forwarded and rooted in a matched local
frame. Its input remains opaque when it began as an entry value. Keys, contents,
returns, roots and copy endpoints still require their independent existing
proofs. Unsupported producers and effects refuse even when negation consumes
their results. Both structural conditional arms remain checked, including an
unsupported arm after a literal zero's negation. Budget exhaustion discards all
contents evidence and leaves every original escape verdict unchanged.

The devbox unit family passes **22 rows**, **eleven live mutation states**
under forged completion/confinement markers, and a wide snapshot with 32 extra
negation origins. Their measured additional cost is **64 work units**, one
producer and one copied origin per extra result. Every row and live state
sweeps every incomplete contents/retention budget and the exact endpoint.
The arithmetic unary kinds, `typeof` and `void` each have refusal controls.
Saved child identity, primitive return/storage, opaque/local joins, invalid
Boolean keys, forbidden opaque uses and a later retained arm are covered.
All **946 incomplete retention budget cutoffs** pass.

One separate executed-source witness negates `0`, `1`, empty String and
nonempty String `"0"`. It deletes the original and copied child field before
returning both distinct containers, the selected alias and the Boolean result.
Its source-coordinate checker observes **four sites, sixteen instances and
twelve retained instances**, with the old child confined on all four calls.
The historical copy-path family remains **21 sites, 39 instances, 23 retained**;
the preceding source-switch family remains **four sites, ten instances, three
retained**. All earlier family expectations pass unchanged.

All **eight escape CTests pass in 8.23 seconds**, and all four execution oracles
report **zero soundness violations**. Expanded-fixture precision measures
**37/49**, with zero partial or pending claims, versus the preceding **36/48**.
The separate negation witness contributes one additional proved-confined site
and one observed-confined site. This is additional coverage, not a precision
gain on the historical fixture. Bootstrap/p5/Phaser precision remains
**0/64, 0/16, 0/20**, including p5's existing single partial observation.
Focused log: `/tmp/ctcompile-nullable-payloads-focused.log`. The final 247-step
generated build succeeds without warnings. Full CTest passes **512/517 in
1045.44 seconds**, all **372 compiler tests**, with only the five recorded
browser failures. All **165 lit cases** pass in **440.08 seconds**. The four
oracles repeat the same precision and zero-violation results in that gate.
All eighteen session code/test paths match committed HEAD, frozen input and
final devbox source. Evidence: `/tmp/ctcompile-nullable-payloads-full.log`,
`-evidence.json` and `-postgate.log`.

Local Node syntax and execution pass **fifteen fixture calls**, sixteen explicit
identity assertions and two extra object-selector probes whose coercion
counters remain zero. Eight new negation mutations discriminate removed or
duplicated negation, coercing equality, forced alias selection, retained child
fields and copied-container aliasing. The eight historical copy-path and six
source-switch mutations also continue to discriminate. Evidence:
`/tmp/ctcompile-escape-negation-node.js` and its `.py` generator. Homebrew
clang-format **22.1.8** passes all **745 files**; whitespace checks pass.
No runtime behavior or native ownership admission changes. Other total unary
producers, loops and native lifetime consumers remain separate work.

## Noncapturing typeof and void

This continues the next producer boundary in **`b3ecab58`**, the **`4f5e248d`**
handoff and the **15:15:11** synchronization journal. Complete contents now
admit `ctjs.unary typeof` and `ctjs.unary void` as independent primitive
origins. TypeOf produces a String containing a type name; Void produces
Undefined after its operand has already been evaluated. Neither result contains
an object/array reference to the input, and neither operation invokes user code.
Opaque inputs remain opaque for every other use.

The TypeOf proof is deliberately narrower than a no-allocation or nonthrowing
claim. `ctbrowser/lib/Script/vm/coerce.cpp::type_of` inspects tags and callable
identity without property access or conversion. `vm/run_loop.cpp` allocates a
fresh primitive String from that name. The opcode row marks allocation and the
fatal allocation ceiling, with no JS reentry or catchable exception. Existing
contents proofs already admit local allocation; admitting this primitive
terminal neither elides its allocation nor proves that allocation succeeds.
The String's bytes are not inferred, even for a literal operand. Its use as
an own-property key still requires an independent exact-key proof and refuses
here. Numeric array indices and copy/container endpoints also remain unproved.

Void's ODS and boxed lowering contracts return Undefined from an already
evaluated SSA operand. A preceding call, publication or unsupported producer
still refuses the whole contents transaction. The real source compiler emits
the operand's bytecode and then `load_undef`; source `void` therefore provides
an execution control for discarded results and preserved effects, while the
unit fixture independently exercises `ctjs.unary void` itself. No runtime or
ODS behavior changes. Neg, Plus and BitNot remain refused because they can
invoke conversion code. No result supplies an input alias, a branch-liveness
fact, a known opaque value or native ownership admission.

Each kind passes **twenty unit rows**, **twelve live mutation states**
under forged completion/confinement markers, and a wide snapshot with 32 extra
origins costing exactly **64 additional work units**. Every row and live
state sweeps all incomplete contents/retention budgets and the exact endpoint.
Controls cover primitive return/storage/rooting, local/opaque joins, retained
saved reads, unsupported operand evaluation, opaque uses, unknown effects,
invalid result keys/indices and a later retained structural arm. All 22
historical logical-negation rows and eleven mutation states remain; their two
TypeOf/Void rows and TypeOf mutation now expect the independently proved
complete result instead of refusal. Each new kind passes **976 incomplete
retention budgets**; the preserved negation family now passes **1,010** because
those previously refused paths complete.

Two separate executed-source functions add five TypeOf calls (Undefined, Null,
Number, Boolean and String) and two Void calls. Both retain distinct original
and copied containers and the selected alias, after deleting their old child
fields. Void additionally returns a visible field write on the selected
container and an Undefined result. The new source-coordinate family measures
**eight sites, 28 instances and 21 retained instances**. All older source
families, calls and exact claim expectations pass unchanged: the copy-path
family stays **21 sites, 39 instances, 23 retained**; source switch stays
**four sites, ten instances, three retained**; negation stays **four sites,
sixteen instances, twelve retained**.

All **eight escape CTests pass in 8.35 seconds**, and all four execution oracles
report **zero soundness violations**. Expanded-fixture precision measures
**39/51**, with zero partial or pending claims, versus the preceding **37/49**.
The two new witnesses each add a proved-confined and an observed-confined site;
these are additional coverage, not a precision gain on the historical fixture.
Source Void already imported as constant Undefined, so its new witness does
not measure a gain from admitting `ctjs.unary void`. Bootstrap/p5/Phaser
precision remains **0/64, 0/16, 0/20**, including p5's existing single partial
observation. Focused log: `/tmp/ctcompile-mixed-nullable-focused.log`.

Local Node syntax and execution pass **22 combined fixture calls**, **32
identity assertions** and three object-selector probes whose conversion
counters stay zero. All **nineteen new observation mutations** discriminate
wrong type names, lost/aliased identities, omitted deletion, a retained unary
operand, omitted Void effects and writes to the wrong container. The eight
historical copy-path, six source-switch and eight negation mutations continue
to discriminate. Evidence: `/tmp/ctcompile-escape-total-unary-node.js` and its
`.py` generator. Homebrew clang-format **22.1.8** passes all **745 files** in
the parent's frozen snapshot, and whitespace checks pass.

The final 247-step devbox build passes warning-free. CTest finishes **512/517
in 1113.08 seconds**, with all **372 compiler tests** passing and only the five
recorded browser failures. All **165 lit cases pass in 489.13 seconds**. The
four escape oracles again report zero soundness violations and the same fixture
and corpus counts above. Native Bootstrap remains **19/574**, exact Data
**0/7 browser/CommonJS and 0/8 AMD**; this escape work adds no measured native
corpus coverage. Final logs: `/tmp/ctcompile-mixed-nullable-full.log`,
`-evidence.json` and `-postgate.log`.

The next bounded producer candidate is the supported `ctjs.binary_static`
family, requiring its own result/effect and refusal/budget evidence. Loops and
native lifetime consumers remain separate work.

## Static binary results after excluding BigInt

This resumes that exact producer boundary from **`a539c3fa`**, the
**`ae8e021a`** handoff and the **16:28:03** synchronization journal. Complete
contents now admit all seven verified `ctjs.binary_static` kinds only after
both operands have independently known, non-BigInt origins on the current
structural path. An explicit whitelist recognizes primitive non-BigInt
constants, fresh objects/arrays, previously admitted total primitive producers
and earlier proved static Number results. Forwarded values and saved reads
use their original identity, including after the source slot is overwritten.
No alias lattice, inferred native type or completion annotation supplies this
proof. The result is a separate primitive Number origin, without an inferred
value, index, property key, input alias or branch-liveness fact.

The exclusion is required by the actual VM contract, rather than by a blanket
purity assumption. `Operators.td`, `bytecode_opcodes.def` and
`vm/coerce.cpp::binary_op_static` agree that the seven operations cannot
reenter user code, but their BigInt path can allocate and throw catchable
TypeError/RangeError. Mixed operands and unsigned BigInt shifts throw; signed
BigInt shifts can also fail. Even a successful literal BigInt pair stays
outside this Number-only proof. Opaque arguments refuse on either side.
After independently excluding BigInt, the static conversions return Number
without an input alias, a catchable JS throw or user conversion. They can
allocate ordinary C++ temporaries: `number_format.cpp::string_to_number`
uses `std::string without_point` to parse a trailing decimal point. The initial
no-allocation comment was incorrect and was corrected during review. As with
the earlier TypeOf proof, absence of allocation or fatal/foreign failure is
not proved. Fresh objects use those same static conversions in the VM; this
is an existing documented deviation from source JavaScript object coercion.
This change does not assert that source behavior agrees on objects and changes
no runtime operation.

The unit table passes **31 rows per kind**, both BigInt/opaque operand positions,
all previously admitted primitive producer origins, saved reads before a
BigInt/non-BigInt overwrite, retained structural arms and unsupported effects.
Each kind passes **18 live states** under forged completion/confinement markers,
including all six invalid static kinds and an in-place Number-to-BigInt
constant mutation. Every table/live state uses the existing complete contents
and retention budget sweeps, including **1,283 incomplete retention budgets
per kind**; the wide snapshot's 32 extra results cost exactly **64 work units**.
All earlier contents/retention families pass unchanged.

Three executed-source functions separately exercise the seven Number
operations, an opaque numeric observation and a successful literal BigInt
pair. The high-bit input and shift count 33 distinguish signed/unsigned
results, truncation and masking. All returned graphs preserve distinct
source/copied containers after deleting their child fields. The two controls
keep conservative Stored claims despite observed confinement; source Number
observations cannot establish an opaque argument's tag. The new source family
measures **twelve sites, twenty instances and fifteen retained instances**.
All older source-coordinate expectations pass unchanged.

All **eight escape CTests pass** in the focused nine-test gate, which includes
the seeded host test and finishes in **21.62 seconds**. All four execution
oracles report **zero soundness violations**. Expanded-fixture precision is
**40/54**, with zero partial/pending claims, versus the preceding **39/51**.
The new family adds one proved-confined site and three observed-confined sites;
this adds coverage rather than measuring a precision gain on the historical
fixture. Bootstrap/p5/Phaser precision remains **0/64, 0/16, 0/20**, including
p5's existing single partial observation. Focused evidence:
`/tmp/ctcompile-nullable-host-results-focused.log`. No measured native corpus
gain is claimed for this escape proof.

Local Node syntax/execution passes **27 combined fixture calls**, **41 identity
assertions**, the three historical object-selector probes with zero conversion
calls and an opaque BigInt TypeError probe. All **28 new observation mutations**
discriminate changed operations, wrong shifts/masks, lost identities, omitted
deletions and changed BigInt result tags. All earlier **41 observation
mutations** continue to discriminate. Evidence:
`/tmp/ctcompile-escape-static-binary-node.js` and its `.py` generator.

The next producer boundary is catchable BigInt outcomes, requiring a separate
completion-path proof; it is not discharged by successful literal observations.
Loops and native lifetime consumers remain separate work.

The full **253-step generated build passes warning-free**. Its initial CTest
run passes **511/517 in 1141.97 seconds**: **371/372 compiler** and **140/145
browser** tests. The compiler failure was an old nullable short-circuit test
expecting no host owner; test-only **`077328ae`** now requires the complete
owner proof while preserving refusal of an unsupported native intermediate.
The complete refusal tail passes, followed by the corrected `ctcompile_lit`
CTest **1/1 in 526.35 seconds**, with **165/165 lit cases in 526.28 seconds**.
All **372 compiler tests pass across those two runs**; the five established
browser failures remain. Four escape oracles again report zero violations,
fixture **40/54** and corpus **0/64, 0/16, 0/20**. Native counts remain
Bootstrap **19/574**, p5 **39/4754**, Phaser **45/7725** in both modes, with
zero pruned; exact Data stays **0/7 browser/CommonJS and 0/8 AMD**. Evidence:
`/tmp/ctcompile-nullable-host-results-evidence.json` and
`/tmp/ctcompile-nullable-host-results-lit-final-detail.log`.

## Arithmetic unary results from proved primitive inputs

This continues the producer thread in **`ac3d0d43`**, the **`a8da7c27`**
handoff and the **17:53:27** synchronization journal. Catchable BigInt binary
outcomes still lack a completion-path proof. The bounded increment instead
admits `ctjs.unary neg`, `plus` and `bitnot` only when the current structural
path independently supplies a primitive non-BigInt operand origin. The
whitelist contains Undefined, Null, Boolean, Number and String constants and
previously admitted primitive producers. Saved reads and forwarded values use
their original origin, including after a source field acquires a BigInt.
Neither native type facts, alias lattices nor completion annotations establish
this primitive proof. The legacy unary operand-role table is unchanged.

`Operators.td`, `Bytecode/OperatorTables.h`, `bytecode_opcodes.def` and
`vm/coerce.cpp` connect source minus/plus/complement to these operations.
`to_number_value` calls static `to_number` directly for the five admitted
primitive categories. It cannot invoke object `valueOf`/`toString` there, and
the BigInt TypeError path is excluded independently. Negation and complement
likewise return Number for these inputs. BitNot uses static conversion in the
VM, which deviates from source JavaScript on object inputs; all fresh objects
and arrays remain refused by this common primitive proof. An opaque input
also remains refused, even when every recorded call supplied a Number.

The result has its own primitive Number origin, with no operand alias,
numeric value, property key, array index or branch-liveness inference.
String parsing may allocate an ordinary C++ temporary for a trailing decimal
point. No absence or success of allocation, or absence of fatal/foreign
failure, is proved. Successful literal BigInt negation/complement are separate
result categories and remain refused; Plus's catchable BigInt TypeError also
remains outside this proof. Unknown producers, calls, publications and effects
continue to refuse the entire contents transaction.

The devbox unit family passes **34 rows per kind**, **nineteen live mutation
states** under forged completion/confinement markers, and a wide snapshot
requiring exactly **64 additional work units** for 32 extra origins. It checks
both incoming structural arms, object/array/opaque/BigInt refusals, all prior
primitive producer categories, original saved reads across tag overwrites,
retained child identities, forbidden result keys and preserved unsupported
effects. Every row and live state sweeps all incomplete contents/retention
budgets and the exact endpoint, including **1,779 incomplete retention budget
cutoffs per kind**. All historical contents/retention families pass unchanged.

Four new source functions separately exercise all three Number operators,
saved String/Boolean inputs before a BigInt field overwrite, opaque input
refusal and successful literal BigInt refusal. Null checks signed zero;
`"4294967297."` checks String conversion and 32-bit truncation. Returned graphs
retain distinct original/copied containers after child deletion, and the two
positive functions also return the selected alias. The source-coordinate
checker measures **sixteen sites, 28 instances and 21 retained instances**.
Earlier source families and their exact claim expectations are unchanged.

All **eight escape CTests pass** in the focused gate, and all four execution
oracles report **zero soundness violations**. Expanded-fixture precision is
**42/58**, with zero partial/pending claims, versus the preceding **40/54**.
The new family contributes two proved-confined and four observed-confined
sites; this adds coverage rather than measuring a precision improvement on
the historical fixture. Bootstrap/p5/Phaser precision remains **0/64, 0/16,
0/20**, including p5's existing single partial observation. The complete
focused gate passes **12/12 CTests in 61.47 seconds**. Evidence:
`/tmp/ctcompile-nested-method-focused.log`. No native corpus gain is claimed
for this escape increment.

Local Node syntax and execution pass **34 combined fixture calls** and their
numeric, tag, field and identity assertions. All **31 new observation
mutations** discriminate changed operators, lost signed zero/conversion,
wrong selected aliases, changed saved-field state, omitted deletion and
copied-container aliasing; all **69 historical mutations** still discriminate.
Additional opaque probes observe a BigInt TypeError, three source object
conversion calls and a thrown conversion value with one call. These object
probes explain the refusal and do not assert that the VM's static BitNot object
behavior agrees. Evidence: `/tmp/ctcompile-escape-arithmetic-unary-node.js`
and its `.py` generator. Homebrew clang-format **22.1.8** and whitespace checks
pass the changed C++ files. No runtime behavior or native admission changed.

The next BigInt boundary still requires completion-path evidence, rather than
successful literal observations. Loops and native lifetime consumers remain
separate work.

The final **243-step generated devbox build passes warning-free**. CTest
finishes **512/517 in 1170.30 seconds**: all **372 compiler tests** and
**140/145 browser tests** pass. Only the five established browser failures
remain: `selectors`, `frames`, `element_attrs`, `vm_async` and `early_errors`.
All **165/165 lit cases pass in 529.85 seconds** (CTest **530.05 seconds**),
including the complete **137-program/eighteen-lifetime** published Map gate.
The four escape oracles again report zero violations, fixture **42/58** and
corpora **0/64, 0/16, 0/20**. Native coverage remains Bootstrap **19/574**, p5
**39/4754** and Phaser **45/7725** in both modes, with zero pruned; exact Data
remains **0/7 browser/CommonJS and 0/8 AMD**. All twelve session code/test
paths match committed HEAD, frozen gate input and final devbox sources.
Evidence: `/tmp/ctcompile-nested-method-full.log`, `-full-detail.log`,
`-evidence.json` and `-postgate.log`.

## Loose equality from two proved primitive origins

This continues the producer thread in **`1392478f`**, the **`e533a865`**
handoff and the **18:56:37** synchronization journal. Complete contents now
admit `ctjs.compare eq` only when **both** current original operand origins
independently prove Undefined, Null, Boolean, Number or String. Earlier admitted
primitive producers also supply independent origins. Objects, arrays, opaque
entry values and BigInt refuse on either side; proving one operand never proves
the other. Saved reads keep their original identity after a slot is overwritten
or deleted, and all structural incoming paths must pass independently.

`CTJS/IR/Ops/Operators.td`, `CTJS/Import/Bytecode/OperatorTables.h` and the
`loose_equal`/`loose_not_equal` opcode rows connect source `==` and `!=` to
Eq and Eq followed by Not. The new restriction follows the current
`ctbrowser/lib/Script/vm/coerce.cpp::loose_equals` implementation: the five
primitive categories use tag/nullish tests, primitive String equality or static
`to_number`. These paths never invoke user conversion or a reentry-depth guard,
throw a catchable JS exception, or carry either input's object identity.
String parsing can allocate C++ temporaries; neither absence nor success of
allocation is proved. The Boolean has no inferred truth value, property key,
array index, operand alias or branch-liveness fact. StrictEq and `operandRole`
are unchanged; the existing converted sink classifications remain conservative.

The devbox unit family passes **70 rows**, **34 live mutation states** under
forged completion/confinement markers, and a 32-origin wide snapshot requiring
exactly **64 additional work units**. Every row and live state uses the existing
exhaustive incomplete contents/retention budget sweeps and exact endpoint.
Controls cover each operand position, original primitive/object/BigInt reads
across array overwrites, own fields after overwrite/deletion, both structural
incoming arms, retained child identities, primitive storage/return/rooting,
invalid result keys, unsupported effects and all four relational kinds.
Number-to-BigInt in-place constant mutations and Eq/StrictEq/relational kind
changes defeat stale completion markers. All **3,017 incomplete retention
budget cutoffs** pass; all historical array contents/retention families pass.

The initial focused devbox gate passes **11/12 CTests in 71.88 seconds**,
including the four escape analysis tests and all three corpus claim tests. Its
fixture oracle reports **zero violations**, **43/63 precision**, zero partial
and pending claims, and the expected **twenty sites, 36 instances, 27 retained**
for the five new functions. The fixture CTest nevertheless fails because the
new `Released` child's predicted confinement was overbroad: its source spells
`other = choice ? 18 : undefined`, whose bare identifier imports as
`ctjs.load_global "undefined"` at line **2422** of
`/tmp/ctcompile-leaf-object-escape-raw.mlir`. `compile_ident` in
`Script/compile/expressions.cpp` emits `get_global` for that unbound identifier;
`CTJS/Import/Bytecode/Instructions.cpp` preserves the lookup. The whole-function
query correctly refuses that unsupported operation, regardless of the runtime
value it returned. This was a test expectation error, not a producer defect.
Only the `Saved` source proves confinement in this first run. All historical
source-coordinate families pass unchanged; Bootstrap/p5/Phaser remain
**0/64, 0/16, 0/20**, with zero soundness violations. Focused log:
`/tmp/ctcompile-leaf-object-focused.log`.

The corrected source family preserves the original `Released` function and
both calls byte-for-byte as a global-lookup refusal control. A separately named
`Literal` repair changes only that operand spelling to `void 0`, which imports
as constant Undefined; the two observations still agree in Node. The original
function-and-calls SHA256 is
`7e07592c147839ff38475ed69820f04004942fdb541a9bbaacc45a5eb1c27353`.
Six source functions now provide two positives (`Literal` and `Saved`) and
four controls (global lookup, opaque formal, BigInt and relational). The
corrected devbox rerun passes **2/2 CTests in 0.26 seconds**: all array unit
families, including the **70 rows/34 states/3,017 cutoffs** above, and the exact
fixture checker with **24 sites, 44 instances and 33 retained instances**.
The expanded fixture reports **zero violations**, **44/64 precision**, and
zero partial/pending claims. Relative to the preceding committed **42/58**,
this adds two proved-confined and six observed-confined source sites; it adds
coverage, rather than measuring a precision improvement on historical input.
All **eight escape CTests pass across the initial gate and corrected rerun**.
Every original source function, invocation and historical family expectation
is preserved. Evidence: `/tmp/ctcompile-leaf-object-escape-rerun.log`.
The final generated build/CTest gate is recorded below; no native corpus gain
is claimed.

Local Node syntax/execution passes **45 combined fixture calls**, with explicit
result/tag/field/identity checks. All **56 new observation mutations** and
**100 historical mutations** discriminate; additional opaque object probes
observe three `valueOf` calls and one thrown conversion value with one call.
These probes justify retaining opaque/object refusals and do not replace
current-IR proofs. Evidence: `/tmp/ctcompile-escape-primitive-equality-node.js`
and its `.py` generator; the preceding 43-call observations are preserved in
`/tmp/ctcompile-escape-primitive-equality-initial-node.{js,py}`. Homebrew
clang-format **22.1.8** and whitespace checks pass the changed paths. No runtime
behavior, producer logic or native admission changed in this correction.

A source audit also qualifies the preceding arithmetic-unary contract.
`coerce.cpp::negate_value` reaches `to_number_value`, whose unconditional
`vm.hpp::reentry_scope` can throw a catchable RangeError above depth **512**
before testing a primitive operand's tag. BitNot uses the guard-free static
conversion. Thus Neg/Plus exclude conversion of an object operand and the
BigInt TypeError; they do **not** prove normal completion or absence of all
runtime exceptions. Uncaught error description can itself consult Error
prototype accessors (`vm/call/invoke.cpp::describe_thrown`). The current sole
consumer, `refineArrayRetention`, proves the entire function without calls,
publication, handlers or closure creation. `vm.hpp::make_error` attaches only
an unrelated Error, primitive message/stack data and a pre-existing prototype;
an early exceptional exit cannot expose those unpublished fresh locals.
Caught unwind excludes the ending frame's register window in the oracle
(`vm.hpp::unwind_to_handler`/`each_root`). No retention counterexample was found;
this is source inspection, **not a measured deep-stack execution**. Comments
now explicitly forbid treating this retention evidence as a no-throw/effect
contract for future native consumers. Earlier measured unary behavior stays.

All four relational kinds remain a conservative next producer boundary. They
unconditionally call `to_primitive`, including its depth guard for primitive
operands, so they need their own completion/retention argument and controls.
BigInt comparison categories, loops and native lifetime consumers also remain
separate work.


The Eq increment is saved in **`5e2cb6b2`**. The final **244-step generated
devbox build passes warning-free**. Full CTest finishes **512/517 in 1199.12
seconds**, with all **372 compiler tests** and **140/145 browser tests** passing.
Only the five established browser failures remain: `selectors`, `frames`,
`element_attrs`, `vm_async` and `early_errors`. All **165 lit cases pass in
551.23 seconds**, CTest **551.43 seconds**; exception recovery passes in
**1.18 seconds**. No corrective rerun was needed for this full gate.

All eight escape CTests pass again. The Eq unit family repeats **70 rows,
34 live states and 3,017 retention cutoffs**, and all historical unit families
pass unchanged. Four execution oracles again report **zero soundness
violations**. Expanded-fixture precision remains **44/64**, with zero
partial/pending claims and the exact **24-site/44-instance/33-retained** source
family. Bootstrap/p5/Phaser precision stays **0/64, 0/16, 0/20**, including
p5's existing single partial observation. Native corpus counts stay Bootstrap
**19/574**, p5 **39/4754** and Phaser **45/7725** in both modes, with zero
pruned; exact Data stays **0/7 browser/CommonJS and 0/8 AMD**. Final evidence:
`/tmp/ctcompile-leaf-object-full.log`, `-full-detail.log` and `-evidence.json`.
The completed postgate checks confirm that all **nineteen code/test paths**
match committed HEAD, frozen input and final devbox sources. Both actual emitted
leaf programs contain no Script/VM symbols. Evidence:
`/tmp/ctcompile-leaf-object-postgate.log` and `-final-hashes.json`.

## Primitive relational origins, 2026-09-08

This continues the exact relational boundary left by **`5e2cb6b2`** and the
**`5d2d843a`** handoff. `computeArrayContents` now accepts Lt, Le, Gt and Ge
only when **both original operand origins** independently prove Undefined,
Null, Boolean, Number or String. The proof follows saved reads and every
structural incoming arm; a later primitive overwrite cannot repair an older
object/BigInt origin. Existing Eq and StrictEq behavior is preserved. No type,
completion or confinement report supplies authority, and `operandRole` remains
unchanged. Results are independent Booleans with no inferred truth, key,
index, alias or branch-liveness fact.

The runtime argument is deliberately narrower than no-throw. Current
`Script/vm/coerce.cpp::compare_relational` passes both operands, in source order,
through `to_primitive`. On these independently proved primitive categories,
normal execution then uses String/String byte comparison or static numeric
conversion; neither invokes a user conversion or returns an input object.
The unconditional `to_primitive` depth guard can still throw an unrelated
RangeError above depth 512. `script/vm.hpp::make_error` attaches only primitive
message/stack data and a pre-existing error prototype. The entire contents
query refuses calls, publication, closure creation and handlers, so none of
its fresh local identities is available to an outer catch or an Error prototype
accessor reached by uncaught-error description. The unwind oracle excludes the
ending frame's register window (`unwind_to_handler`/`each_root`). This justifies
retention on an early exit, **not normal completion or an effect/no-throw
contract**. C++ String parsing may allocate; allocation success is unproved.
A further source-level audit found no publication counterexample. `make_error`
uses direct own-data writes, with no `cause`/options processing or Error
constructor call. `current_stack` serializes only function-proto names, indices
and instruction offsets into a String; it captures no frame values. The
importer preserves closure, cell and upvalue operations explicitly, and this
query rejects them, so a hidden closure cannot retain these fresh locals.
No dynamic deep-stack measurement is claimed. Native lifetime/effect consumers
still require their own proof. No runtime/browser source changed.

The independent unit table now runs for Eq and each of the four relational
kinds. Each passes **75 rows**, **34 live mutation states**, an exact
**64-work-unit** wide snapshot increment, and **3,300 incomplete retention
budget cutoffs**, alongside the exhaustive incomplete contents budgets.
Controls cover both operands, original array/field reads across replacement/deletion, every structural arm, retained aliases,
opaque/BigInt/object exclusions, invalid result keys, fresh/stale completion
markers, and actual publication before/after the comparison, retaining calls,
explicit throws and local exception handlers. The latter keep the exceptional
retention argument bounded to the function subset it audited.

The initial focused devbox run passes **11/12 CTests in 87.98 seconds**.
The only failure is five repetitions of the new handler row, one per comparison
kind: it expects `UnsupportedOperation` (2), while the existing non-cf successor
guard returns `UnsupportedControlFlow` (1) at `ctjs.push_handler`.
The query already refuses with empty evidence, and the retention refusal and
budget checks pass. Correcting only that expected enum fixes the test; a
nearby comment now explicitly limits the guard-free/catchable-throw statement
to Eq. No production behavior or JavaScript source changes in the correction.
The subsequent nineteen-step focused rebuild passes, followed by **12/12
CTests in 83.03 seconds**, including **all eight escape CTests**. All five
comparison kinds repeat the exact unit counts above, and historical array
contents/retention families pass. Evidence:
`/tmp/ctcompile-leaf-readback-focused.log`, `-focused2.log` and
`-focused2-detail.log`. The completed full gate is recorded below.

The historical `objectFrameLooseEqualityRelational` function and both calls
retain their exact source bytes. Its numeric/String results already ran in
the preceding oracle; only its child's claim changes from Stored to confined,
now independently checked against both observed instances. This is a precision
improvement on existing input, not new source coverage. The historical
loose-equality family keeps its **24 sites, 44 instances and 33 retained**;
all other historical family expectations pass unchanged. Five separate new
functions measure **20 sites, 40 instances, 32 retained**: saved String origins
after a BigInt field overwrite and deletion; unordered Undefined/invalid numeric String results; opaque
formal and literal BigInt refusal controls; and an independently saved child
still returned after both container fields are deleted. Four execution oracles
report **zero soundness violations**. Expanded-fixture precision is **47/68**,
with zero partial/pending claims. Relative to **44/64**, one already observed
child gains confinement; the new family contributes two proved-confined and
four observed-confined sites. Bootstrap/p5/Phaser remain **0/64, 0/16, 0/20**,
including p5's existing single partial observation. These escape measurements
do not increase native corpus admission.

Local Node execution passes **55 combined fixture calls**, preserving every
historical function and call. Independent assertions check lexical versus
numeric order, all four false outcomes for unordered values, reverse operand
results, exact source/target identities and selected aliases, and the returned
saved child. All **51 new observation mutations** and **156 historical
mutations** discriminate. Additional opaque-object probes observe four
`valueOf` calls and a thrown conversion value after one call; these justify
retaining the object/opaque refusal and do not replace the current-IR proof.
Evidence: `/tmp/ctcompile-escape-relational-node.{py,js,json}`. Homebrew
clang-format **22.1.8** and changed-path whitespace checks pass.

The relational increment is saved in **`b98efac0`**. The final **244-step
generated devbox build completes with zero warnings**. Full CTest finishes
**512/517 in 1257.06 seconds**: all **372 compiler tests** and **140/145 browser
tests** pass. The five established browser failures remain `selectors`,
`frames`, `element_attrs`, `vm_async` and `early_errors`. All **165 lit cases
pass in 606.48 seconds** (CTest **606.54 seconds**), including the integrated
published Map driver. Its source census contains **163 programs and twenty
lifetime families**; those counts follow from the passing complete driver,
not an additional execution run. Exception recovery passes in **1.17 seconds**.

All eight escape CTests repeat successfully in this full run. Each comparison
kind repeats **75 rows, 34 live states and 3,300 retention cutoffs**; all four
execution oracles again report **zero soundness violations**. Fixture precision
stays **47/68**, zero partial/pending, with the exact **20/40/32** relational
and **24/44/33** historical equality family observations. Bootstrap/p5/Phaser
remain **0/64, 0/16, 0/20**, including p5's existing single partial observation.
Native corpus counts remain Bootstrap **19/574**, p5 **39/4754** and Phaser
**45/7725** in both modes, zero pruned; exact Data stays **0/7 browser/CommonJS
and 0/8 AMD**.

All **seventeen code/test paths** match committed HEAD, frozen input and final
devbox sources. Both actual emitted identity/lifetime C++ files retain owning
`map_get_present_identity` results across later mutations and contain no
Script/VM context symbols. Evidence:
`/tmp/ctcompile-leaf-readback-{full.log,full-detail.log,evidence.json,postgate.log}`,
`-root-hashes.json`, `-final-identity.cpp` and `-final-lifetime.cpp`.

Remaining boundaries include independently proved primitive conversions and
dynamic arithmetic, BigInt comparison categories, loops, callee summaries and
native lifetime consumers. These retain their existing conservative behavior.

## Primitive dynamic arithmetic origins, 2026-09-08

This continues the dynamic-arithmetic boundary left by **`b98efac0`** and the
**`e385f885`** handoff. `computeArrayContents` accepts dynamic Sub, Mul, Div,
Mod and Pow only when **both original operand origins** independently prove
Undefined, Null, Boolean, Number or String. Saved reads keep their original
identity through overwrites and deletions, and every structural incoming arm
must qualify separately. No type, completion or confinement annotation is
proof. Dynamic Add/Concat, all other dynamic kinds, objects, arrays, opaque
inputs and BigInt remain outside this increment. The operand-role table and
native admission are unchanged.

Current `Script/vm/coerce.cpp::binary_op` first checks `bigint_binary`.
Independently excluding BigInt makes that helper return false without
allocation or a JS throw; each admitted kind then converts both operands
through `to_number_value`. Normal primitive paths use static numeric conversion
and produce an independent Number, including NaN, infinity and signed zero.
Mod uses `fmod`; Pow uses the VM's `exponentiate`, including its +/-1 to NaN
special case. None of these paths returns an input object or invokes an object
conversion. The origin proof infers no numeric value, array index, property key
or branch liveness.

As with Neg/Plus, `to_number_value` enters the depth guard before inspecting
the primitive tag. Its unrelated RangeError cannot retain unpublished fresh
locals in this complete call/handler/publication-free query; the preceding
relational section's Error construction, stack serialization and unwind-root
audit also applies here. This is **retention-only evidence**, never normal
completion or a no-throw/effect contract. String parsing can allocate C++
temporaries, and allocation success remains unproved. No deep-stack dynamic
measurement or browser/runtime change is claimed.

The existing independent two-operand table is shared with the new arithmetic
family, preserving every comparison row and its operator-specific mutation
controls. Each kind passes **80 rows**, an exact **64-work-unit** wide snapshot
increment, and exhaustive incomplete contents/retention budgets. Each comparison
kind passes **34 live states and 3,425 retention budget cutoffs**; each arithmetic
kind passes **49 live states and 3,941 retention budget cutoffs**, including all
thirteen dynamic enum values and restoration. Controls cover both operands,
every structural arm, original array/field reads before
replacement/deletion, saved returned children, primitive rooting/storage,
invalid result keys, forged completion/confinement markers, and publication,
calls, explicit throws and local handlers. Five new cross-producer rows also
exercise the independent Number origins as inputs to every comparison kind.

The initial devbox build compiles production but finds four missing dependent
`template` keywords in the shared test helper: three `getOps` calls and one
`getDefiningOp`. Adding only those keywords fixes compilation; production
behavior and every JavaScript source byte are unchanged. The corrected
eleven-step rebuild and focused **14/14 CTests pass in 18.28 seconds**, including
**all eight escape CTests**. Historical array/object contents and retention
families also pass. Evidence: `/tmp/ctcompile-field-presence-build3.log`,
`-focused.log` and `-focused-detail.log`. The completed full gate is recorded below.

Five additive source functions measure **20 sites, 40 instances and 32
retained**, with confinement only for the two independently proved arithmetic
children. Opaque formal and successful BigInt controls retain Stored claims; an
independently saved returned child remains retained after both container fields
are deleted. Every historical source byte and historical
family expectation is preserved, including the historical relational
**20/40/32** and equality **24/44/33** families. All four execution oracles report
**zero soundness violations**. Expanded-fixture precision is **49/72**, with
zero partial/pending claims, versus the preceding **47/68**. This adds two
proved-confined and four observed-confined sites; it measures additional source
coverage, not precision improvement on historical source. Bootstrap/p5/Phaser
remain **0/64, 0/16, 0/20**, including p5's existing single partial observation.
These escape measurements do not increase native corpus admission.

Local Node execution passes **65 combined fixture calls**, **60 new observation
mutations** and **207 historical mutations**. Assertions distinguish operand
order, saved String values after a BigInt overwrite, finite results, NaN,
infinity, signed zero, BigInt result types, container identity and retained
saved children. An opaque object executes five `valueOf` calls; a throwing
conversion stops after one; five mixed/zero-divisor/negative-exponent BigInt
probes throw. These observations justify conservative refusal without replacing
current-IR proof. Evidence: `/tmp/ctcompile-escape-dynamic-node.{py,js,json}`.
Homebrew clang-format **22.1.8** and changed-path whitespace checks pass.

The arithmetic increment is saved in **`56569199`**. The final **246-step
generated devbox build completes with zero warnings**. Full CTest finishes
**512/517 in 1267.11 seconds**: all **372 compiler tests** and **140/145 browser
tests** pass. The five established browser failures remain `selectors`,
`frames`, `element_attrs`, `vm_async` and `early_errors`. All **165 lit cases
pass in 619.29 seconds** (CTest **619.49 seconds**), including the integrated
published Map driver. Exception recovery passes in **1.15 seconds**.

All eight escape CTests repeat successfully. Each comparison kind repeats
**80 rows, 34 live states and 3,425 retention cutoffs**; each arithmetic kind
repeats **80 rows, 49 live states and 3,941 retention cutoffs**. All four execution
oracles again report **zero soundness violations**. Fixture precision stays
**49/72**, zero partial/pending, with the exact arithmetic **20/40/32**,
relational **20/40/32** and equality **24/44/33** family observations.
Bootstrap/p5/Phaser remain **0/64, 0/16, 0/20**, including p5's existing single
partial observation. Native corpus counts remain Bootstrap **19/574**, p5
**39/4754** and Phaser **45/7725** in both modes, zero pruned; exact Data stays
**0/7 browser/CommonJS and 0/8 AMD**.

All **fourteen code/test paths** match committed HEAD, frozen input and final
devbox sources. Both actual emitted field/lifetime C++ artifacts use native
object accessors and contain no Script/VM context/value symbols or AOT runtime
calls. These hashes and output checks were independently rechecked against the
root's evidence parser and final devbox hash log. Evidence:
`/tmp/ctcompile-field-presence-{full.log,full-detail.log,evidence.json,postgate.log}`,
`-root-hashes.json`, `-final-field.cpp` and `-final-lifetime.cpp`.

Remaining producer boundaries include primitive conversions, dynamic
Add/Concat and BigInt categories, followed by loops, callee summaries and native
lifetime/effect consumers. Each retains its existing conservative behavior.

## Primitive Add and Concat origins, 2026-09-08

This continues the producer boundary in **`56569199`** and the **`bf2fd02e`**
handoff. Dynamic Add and Concat now require **both original operand origins**
to independently prove Undefined, Null, Boolean, Number or String. The existing
bounded path state follows saved reads through later field overwrites/deletions
and checks every structural arm. A primitive result has no object alias, but
supplies no concrete Number/String tag, numeric value, literal property key or
branch-liveness fact. BigInt, object/array and opaque operands still refuse;
no annotation, host summary or observation supplies a missing origin proof.
The operand-role table and native admission remain unchanged.

The current VM's `Script/vm/coerce.cpp::binary_op` handles these two operations
separately. Add first calls `to_primitive` in source order, then either combines
primitive String bytes or performs static numeric addition. Concat applies
`to_string` directly to each input. The five admitted primitive categories
return before either String conversion can inspect an object or call user
code. String results are VM allocations and conversions may allocate C++
temporaries; this proof establishes neither absence nor success of allocation.
Add's unconditional `to_primitive` depth guard may still throw an unrelated
RangeError. The preceding Error/stack/unwind audit applies: this complete query
rejects calls, handlers, closure capture and publication, so its fresh local
identities cannot become retained through that guard. This remains **retention
only**, with no normal-completion, no-throw or native effect guarantee. Concat's
primitive path does not use that guard. No deep-stack execution is claimed.

Before production changed, local Node and the parent's serialized current
VM `ctcompile-test-native-reference` both measured **`trace=4095`** on twelve
checks. These distinguish Number versus String addition, operand order, NaN,
signed zero, Null/Undefined/Boolean formatting, Unicode bytes, infinity,
object `valueOf` for Add versus `toString` for Concat, the two thrown conversion
values, successful BigInt operations and a mixed BigInt TypeError. The object
and BigInt observations explain the conservative cut; they do not prove future
operands primitive. Source: `/tmp/ctcompile-escape-add-concat-semantics.js`.
No browser or runtime source changed.

The independent two-operand unit table preserves all historical rows and adds
four cross-producer cases: numeric Add, Concat and each String-producing Add
operand order. It passes **84 rows per kind**, including independent Add and Concat
runs. Each of the seven dynamic binary kinds passes **49 live states and 4,127
retention cutoffs**; each of Eq/Lt/Le/Gt/Ge passes **34 live states and 3,527
retention cutoffs**. Tests retain separate live operand/kind/constant mutations
under forged completion/confinement reports, every incomplete contents budget
and the exact **64-work** wide snapshot check. Unknown inputs, saved
BigInt/object origins, late publication/calls/throws/handlers and nonliteral
result keys remain negative.

Seven additive source functions measure **28 sites, 56 instances and 44
retained**: saved String addition after BigInt replacement, numeric addition,
saved Null/Undefined template inputs after object replacement, separate opaque
Add-only and Concat-only refusals, successful BigInt controls, and an independently
returned saved child. Every historical JavaScript byte and oracle-family
expectation is preserved. The new checker joins recording and compiler claims
by exact program/function/pc; only the three independently proved children gain
confinement. All four escape oracles report **zero soundness violations**.
Expanded-fixture precision is **52/78**, with zero partial/pending claims,
versus the preceding **49/72**. This adds three proved-confined and six
observed-confined sites; it is added source coverage, with no precision gain
claimed on historical source. Bootstrap/p5/Phaser remain **0/64, 0/16, 0/20**,
including p5's existing single partial observation. The historical arithmetic
**20/40/32**, relational **20/40/32**, equality **24/44/33** and all older family
expectations pass unchanged.

Local Node executes **79 combined fixture calls**, and **72 new plus 267
historical observation mutations** discriminate. Assertions check Number,
String, BigInt and NaN results, operand order, saved primitive bytes, independent
source/target identities, selected aliases and retained children. Separate
opaque-object calls observe two `valueOf` and two `toString` calls; throwing
versions stop after one conversion each. Evidence:
`/tmp/ctcompile-escape-add-concat-node.{py,js,json}`. Homebrew clang-format
**22.1.8** and changed-path whitespace checks pass; the complete formatter
checks all **745 files**.

The first 48-step devbox build compiles the new production and array tests,
and both selected units pass, but its target list omits `escape-claims`.
That older executable produces Stored claims for all three new children,
while the recorder correctly observes their confinement: the initial focused
result is **2/3 in 0.44 seconds**, with zero oracle violations but the expected
precision assertion failing. Rebuilding all escape targets relinks the claims
executable. No source, proof or expectation changes are needed. The corrected
**15-step build has zero warnings**, and focused **15/15 CTests pass in 18.85
seconds**, including **all eight escape CTests**. The source-family and unit
measurements above are from this corrected gate. Evidence:
`/tmp/ctcompile-comparison-identity-{build3.log,focused.log,build4.log,focused2.log}`
and `-format22.log`.

The Add/Concat increment is committed as **`267545cd`**. The full **245-step
generated devbox build completes with zero warnings**. Its CTest result is
**511/517 in 1313.13 seconds**: **371/372 compiler tests** and **140/145 browser
tests** pass. The compiler failure is `ctcompile_lit`, where **164/165 cases
pass in 658.70 seconds** (CTest **658.90 seconds**). The unchanged
`object-argument-refusals.mlir` catches an unrelated native identity census
clearing an earlier, more specific refusal diagnostic. **`8e603ce5`** limits
that cleanup to the census's own strict-comparison reasons; no source test,
proof semantics or escape implementation changes. The complete corrected lit
rerun passes **165/165 in 658.71 seconds** (CTest **658.78 seconds**, command
elapsed **658.79 seconds**). Thus all **372 compiler tests have passing results
across the initial run and corrected rerun**. This is not a second complete
517-test CTest run. The five browser failures remain `selectors`, `frames`,
`element_attrs`, `vm_async` and `early_errors`. Exception recovery passes in
**1.18 seconds** in the full run.

All eight escape CTests pass again in the full run. The comparison kinds repeat
**84 rows, 34 live states and 3,527 retention cutoffs**; all seven dynamic binary
kinds repeat **84 rows, 49 live states and 4,127 retention cutoffs**. Four
execution oracles again report **zero soundness violations**. Fixture precision
stays **52/78**, zero partial/pending, with exact new **28/56/44** observations
and unchanged historical families. Bootstrap/p5/Phaser remain **0/64, 0/16,
0/20**, including p5's existing single partial observation. Native corpus
counts remain Bootstrap **19/574**, p5 **39/4754** and Phaser **45/7725** in
both modes, zero pruned; exact Data stays **0/7 browser/CommonJS and 0/8 AMD**.
These native counts are separate from the escape proof's additional coverage.

All **thirteen final code/test paths** match committed HEAD, final frozen
input and final devbox sources. Both actual emitted distinct-identity and
lifetime C++ artifacts contain no Script/VM context/value or AOT runtime
symbols. The distinct-identity method retains three fresh allocations and
field writes, an owning saved Map read, two Map writes, deletion and a runtime
identity comparison. The sanitizer observer remains separate test code.
Evidence: `/tmp/ctcompile-comparison-identity-{full.log,full-detail.log,lit-rerun.log,lit-detail.log,evidence.json}`,
`-final-hashes.json`, `-final-remote-hashes.log`, `-final-distinct.cpp` and
`-final-lifetime.cpp`. The full-run failure and corrected rerun are retained as
separate evidence, and the escape precision claim is unchanged.

Remaining producer boundaries include primitive conversions and BigInt
categories; loops, callee summaries and native lifetime/effect consumers also
retain their existing conservative behavior.
