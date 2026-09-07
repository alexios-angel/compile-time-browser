# Provider object payloads

The optional host-prefix proof now carries initialized entry-object identities
and their current scalar own fields through private Maps. Enable
`follow-provider-objects=true` alongside publication, provider-read and mutation
following. The option defaults off. Bootstrap's exact programs also require
diagnostic and callback following to reach their object insertion.

The unchanged CommonJS, browser and realm-fallback probes complete their Data
method sequence: **24 resolved calls** and **23 completed provider summaries**,
up from 20 and 18. Each records **68 reads, seven sets, three deletes**, three
nested Maps and one callback with two global writes. The final insertion
allocates the third nested Map, stores the actual `instance`, and returns that
same identity through two `Data.get` calls. Native export ownership remains a
separate proof obligation; completing this sequence does not admit Bootstrap.

CommonJS and browser entry following reach the end. The fallback stops at its
appended `scriptThis === this` observer; realm comparisons and missing-field
reads remain outside this ordinary-object proof. It has no remaining provider
boundary. Runtime execution still checks every realm observation.

## Live identities and transactional fields

`ProviderObjects` ties each token to its actual `ctjs.create_object` and entry
invocation. Only source-created ordinary entry objects with scalar own contents
qualify. Equal fields never equate allocations; copies of the analysis state
do not copy runtime objects. Rebinding a source alias preserves any earlier
allocation still retained by a Map.

Checked `set` and `get` preserve the object token through direct and nested Map
storage. Current own-field reads, scalar replacement, named or constant-key
computed deletion, and reinsertion update the same analysis object. A missing
field refuses a read, since it could consult a prototype. Strict identity
comparison distinguishes aliases, separate allocations and primitive values.

Each provider attempt copies Maps, object state and the optional scalar-global
state. All three and their proof rows commit together only after normal return
and the existing retained-resource check. Failed calls, throws, unsupported
effects and incomplete budgets publish no tentative object or callback effects.
Provider field writes invalidate pending diagnostic snapshots just as Map writes
do. Subsequent calls see committed field changes; entry alias writes update the
same live state between calls.

Source global aliases are known values within this one closed startup trace.
They do not prove native confinement. Declared host roots, realm slots, factory
method tables and objects reachable through their fields are publication
boundaries. Publishing a retained object stops following. Publishing another
object marks its reachable objects unavailable as future private payloads.
Object/Map backedges, object-valued fields, callbacks as payloads, dynamic keys,
accessors, prototype changes and provider-local object allocation remain refused.
An object once retained remains conservatively marked retained after deletion.

All allocations, field operations, Maps, callbacks and observer branches remain
runtime. The pass only selects the proved wrapper branches and resolves actual
entry callees. Object proof rows include allocation/invocation ordinals, object
identity, member, action and result. Returned object IDs use a separate field
from Map IDs and primitive attributes; zero is the absent report value.

## Validation

The source regression, `test/CTNative/host-provider-objects.{py,test}`, passes
**38 source cases**. It checks equal-field identities, returned/global aliases,
shared mutation, separate factories, nested Maps, scalar replacement,
deletion/reinsertion and publication refusals. A combined Map/object/callback
case checks atomic normal completion;
throwing controls and work limits check that partial reports cannot escape.
Stale manifests, forged annotations, reruns, missing providers and disabled
dependencies retain their boundaries. Node supplies thrown-completion evidence;
the reference printer does not expose post-throw globals.

The exact modes are registered as
`ctcompile_bootstrap_host_prefix_{commonjs,browser,browser_this_fallback}_provider_objects`.
They retain the existing source/vendor hashes, seven-function denominator and
19/19/24 declared observations, and compare Node with interpreter and boxed
script/wrapper execution under GC stress. Their native census is checked
independently and remains **0/7**.

The full devbox gate passes **467/467 CTests**, including **155/155 lit cases**,
in **540.22 seconds**. All **559 C++ files** pass formatting and whitespace
checks pass. All **62 exact observations** agree across Node, interpreter and
boxed execution, including GC stress. Default and disabled-optimization native
coverage remains Bootstrap **19/574**, p5 **39/4754**, Phaser **45/7725**.

See [the next Bootstrap boundary](bootstrap-provider-next.md) and
[native export ownership and calls](native-export-boundary.md). No native
ownership guard or escape verdict is relaxed by this prefix proof.
