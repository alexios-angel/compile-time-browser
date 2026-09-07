# Direct load evidence

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
proofs. No native admission consumes this evidence and every escape verdict,
including `Stored`, remains unchanged. Consumers must recompute it after IR
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

## Measured gate, 2026-09-07

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

The next proof must establish complete contents under a checked set of data
property, prototype, accessor and indirect-transfer restrictions, then use all
exposures when considering a different escape verdict. External contents and
unsupported retention paths must be refused before any `Stored` verdict
changes. Native automatic storage also requires the plan's separate type,
identity and frame-lifetime obligations.
