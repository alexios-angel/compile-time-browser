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
fresh local arrays in one straight-line block. Constants and fresh property-free
objects can be elements. Inline initializers, literal `ctjs.append`, initialized
constant-index reads and overwrites are supported. A read resolves to the
original constant or allocation; reading an array and writing through that alias
updates the same array state.

The result records every initializer/append/overwrite and its actual operand,
every read's value at that point, the final slot contents, and every local
allocation reachable from the return value. Earlier reads retain their original
values after replacement. Return reachability follows current slots, visits each
allocation once, and handles self and mutual cycles. This identifies identities
and edges; it does not select an owner, make a cycle collectable, or change the
legacy `Stored` verdict.

The proof requires a known initialized own index. Number `-0` and canonical
decimal String `"0"` refer to index zero; String `"-0"`, noncanonical spellings,
fractions, NaN, infinity, negative numbers and `2^32-1` do not. Generic writes
may overwrite existing elements only. Extending a generic property write, reading
an absent slot, deleting elements, accessing named properties, changing prototypes
or defining accessors refuses the entire result. Literal append uses the existing
internal construction operation, not a call to a potentially modified `push`.

Unknown values and bases, all calls and global accesses/publication, cells,
unsupported carriers, multiple blocks, loops, nested regions, arguments/rest
builders and suspension also refuse. Even a late unrelated call invalidates the
proof. `ctjs.throw` is excluded because uncaught diagnostic formatting may call
`toString` and reenter JavaScript. Return is the only supported exit.

The default budget is 100,000 operation, initializer-element and exit graph
visits. Key parsing examines one Number or at most ten String digits. Only a
completed function scan and return graph publish `complete=true`. Unsupported
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
gate; preceding measurements above describe the prerequisite only.

No native admission consumes these verdicts. Complete contents for control flow,
external values and other containers remain unfinished. Native automatic storage
still requires the plan's separate type, identity, cycle ownership and
frame-lifetime obligations. Corpus precision improvements require measurement;
these unit cases do not establish a Bootstrap gain.
