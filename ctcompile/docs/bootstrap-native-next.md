# What native Bootstrap needs next

## Current boundary, 2026-09-15

**f017e1ea / dcd213d3 / 1772fc4f / 523e631d** compile the pinned original
Bootstrap Data probe with three direct DOM inputs: **3,218 bytes, 7/7 functions,
23 calls and 19 observations**. The nonmovable document session privately owns
Data, the recorder and the payload alias. All five input-alias partitions pass
Node/interpreter comparison, both policies/layouts/compilers, lifetime sanitizers
and mutation/privacy controls. See [HANDOFF](HANDOFF.md) for the final gate and
full-bundle measurements. The earlier survey below records superseded Data steps.

The registered `ctcompile_native_bootstrap_button_probe` now preserves an
**original Button construction/toggle/disposal probe**: **16,194 bytes / 86
imported functions / 4 native**, both optimization policies, no skipped functions.
It retains vendor lines **1–330 and 420–433**, including Config (`W`),
BaseComponent (`B`), Button (`U`) and live helpers. Its element is an explicit
JavaScript test double; this is a source/progress gate, not native DOM support.
Node passes **22 lifecycle observations**, including Data identity, config parsing,
two toggles and disposal's otherwise easy-to-miss event-registry mutation.

The interpreter fails uncaught in `_typeCheckConfig` with
`Object.entries called on null or undefined`. A separate original-source prefix
shows missing inherited `Default`, `DefaultType`, `getInstance`, `DATA_KEY` and
`EVENT_KEY`, while Button's own `NAME` works. Two independent witnesses isolate
missing constructor linkage and inherited closure-accessor lookup even after
explicit linkage. Those measured discrepancies are journaled for Claude; the test
fails when they change so the next measurement cannot silently reuse them.
The older driver catches this failure and never disposes Button.

**075bdd9c** supplies the first trusted class-initialization slice. The explicit
`ctnative-specialize-class-initialization` pass binds the complete source fingerprint
and the host's initial `__ctbrowser_class_defined` identity. Only a complete local
base-class/setup/use census can remove its unobservable descriptor effects. The
empty/default and numeric own-field cases pass **16 native executions**, with
**26 unprepared refusals / 17 preparation refusals**. Methods, inheritance,
field-initializer closures, reflection, helper mutation and unknown source effects
remain refused. This pass is separate from the DOM Data provider and does not yet
prepare original Button. [Host contract details](native-host-slots.md) describe the
input and its limits; [HANDOFF](HANDOFF.md) records the full-gate status.

**fae7cac3** extends preparation to immutable local base-class methods. It proves
exact prototype/home uses, primitive constructor returns and unobservable method
identity before installing bindings on instances. Existing constructor and receiver
lowering then independently prove initialization order and method-key immutability.
The gate passes **40 class native executions / 48 unprepared / 30 preparation
refusals**, plus **8 plain constructed-method native executions / 10 refusals**.

**67867ae0** additionally admits chained calls through those immutable local methods,
reusing the existing receiver fixpoint. Nested mutation, argument evaluation order,
independent instances and unused receivers pass **72 class native executions**;
the expanded source gate retains **72 unprepared / 42 preparation refusals**.

**1fa7709e** makes immutable methods available during construction. The exact
prototype seeds the existing receiver fixpoint after preliminary constructor checks;
full constructor/method admission still gates lowering. Later instance stores never
supply earlier constructor methods. The preserved constructor-call/order sources
now execute as native C++ with results **8 / 132**. The expanded class gate passes
**104 native executions / 88 unprepared / 50 preparation refusals**, plus ordinary
method controls. The local proof remains capture-free and does not prepare Button.

**f6ee9bea** admits local scalar static getter chains under that same complete
class/host proof. Getter dependencies are acyclic and clone work is bounded before
mutation; every expansion stays at its original read. The class gate now passes
**152 native executions / 138 unprepared / 82 preparation refusals**, preserving
**69 source observations**. Static metadata collisions remain refused with separate
Node/interpreter controls. Bootstrap's inherited receivers and object-valued
`Default`/`DefaultType` getters are beyond this local scalar proof.

**Next compiler boundary: inherited instance and static-getter receivers**, default
derived forwarding, lexical `super` and observable `this.constructor`; then compose
with DOM Data ownership. BaseComponent calls `_getConfig` during construction and
observes `DATA_KEY`. Config merges defaults/dataset/config and reads inherited
`DefaultType`/`NAME`. These source operations cannot be erased. The interpreter's
static-inheritance discrepancy remains separately measured.

**3d45614c** now admits the unchanged `prototype-written.js` source through a
complete local immutable scalar-prototype census. Defaults initialize fresh fields
before constructor execution; inherited and constructor reads, conditional shadowing,
borrowed receivers and independent Number/Boolean instances pass 40 native executions.
The full constructor proof controls receiver eligibility. The original inherited-method source now executes unchanged under **1fa7709e**.
Remaining refusals retain late/alias/replacement mutation, new.target, constructor
arguments, arrows, the mutable helper and unsupported String/Null/Undefined field storage.
`unwritten-key.js` still refuses its inherited constructor observation. **158f1fef**
rejects constructing unused-this arrows. Its preserved source exposes another oracle
discrepancy: Node throws TypeError, while the interpreter's inline construct opcode
returns 7. Native refuses; the runtime finding is journaled for Claude.

**fef19039** additionally admits definite local String fields as `std::string`.
The preserved literal-primitives prototype now executes unchanged, bringing that
group to **48 native executions**. Its original saved-string gate passed **8 native
executions**. **f854d2f6** adds definite String length inference and `std::size`
emission; the expanded group passes **32 native executions / 10 refusals**, including
the unchanged original length source. ND-1's Unicode byte count remains separately
measured against Node. **be8781ac** now carries definite String fields across one
closed direct object-argument borrow, with exact initialization before every call
and all callee writes retained in the type join. Saved strings own their bytes.
**3e494a80** also proves stored-method callable provenance, keeping initialization
at every actual direct call and all callee writes in the type join. The String gate
passes **96 native executions / 30 refusals**. **ff125088** completes forwarded
parameters through the closed object-argument census and exact caller initialization
proof; the expanded gate passes **136 native executions / 52 refusals**. Mixed/possibly
absent String storage remains a separate proof. See HANDOFF for current gate status.

HostContract now accepts the helper's explicitly declared initial identity as well
as Map and Array. The local class pass accepts only its helper declaration; the
existing realm descriptor guard still rejects prototype writes. Broader composition
needs a shared complete provenance/use proof, not a helper-name exemption.
Component publication retains `_element` and `_config`, beyond Data's current
scalar-field leaf proof. See HANDOFF for measured gates and the final full-gate status.

Config still reads attributes and dataset when defaults are empty. **8547ad64**
lifts dataset name conversion, supported-property reads and ordered entries into
`ctbrowser/dom/dataset.hpp` and the DOM library. The Shell binding is now an
adapter over that core; native callers receive owning optional strings or vectors
of String pairs. Writes and removal reuse the public document namespace APIs.
The direct DOM/Core client and **8 WPT files / 47 subtests** pass, with identical
WPT results before/after; the complete **601/601 CTest / 176/176 lit** gate passes.

Native synchronous entries lower proved String-name `getAttribute` calls through
`get_element_attribute` in the public DOM library. The Shell binding uses the same
core. Results are owning `std::optional<std::string>` values; absent and empty
remain distinct, and copies survive later mutations and document destruction.
The source gate checks both DOM providers, printing layouts and optimization
policies with Node, the VM, GCC and Clang. **a41b3bc3** adds strict
null/String comparisons, optional String truthiness and definite String + String
names/values. Missing and empty are both false in Boolean observations; saved reads
keep their copied value after mutation. **0aa8dd47** expands
closed, capture-free straight-line calls at their original call sites, then
rechecks the entire DOM body. Nested name construction, repeated calls and saved
reads pass **105 Node/VM observations / eight GCC-Clang binaries**. Callable
identity, captures, recursion and unsupported effects remain refusals; this adds
no nullable-to-String coercion.
Bootstrap's original `getDataAttribute` (vendor line **263**) computes its name
through `F` and feeds the optional result to `M`. `F` still requires regex replace,
its callback and `toLowerCase`; `M` still requires source branches, `Number`,
`toString`, `typeof`, URI decoding, JSON parsing and exceptions. Prove those source
operations and their composition with the DOM entry. Start with `_mergeConfigObj`'s
actual `H.getDataAttribute(e, "config")` call: the key is constant, so checked source
specialization may discharge `F` before a general runtime regex backend is needed.
The exported entry still declares every explicit parameter as an element; local
helpers receive their proved actual arguments. **1aa5c2c2** now proves unique local
object-held helper slots and exact method receivers under the isolated standard
Object-prototype premise. The expanded gate passes **145 Node/VM observations /
eight GCC-Clang binaries / 224 source refusals / 24 method provenance checks**,
with the earlier 41 provenance/depth and four budget/fingerprint controls intact.
**d1c1ab96** additionally proves local immutable leaf captures, including object-held
methods: **173 Node/VM observations / eight GCC-Clang binaries / 45 capture
provenance-budget controls**. It reuses the shared cell/closure proof and checks
assignment-before-read/call, then substitutes each invocation independently.
**18f4519b** composes local captured callable/holder graphs under the unchanged
leaf rules, with composed depth bounded by **e81304b4**: **213 Node/VM observations / eight GCC-Clang binaries / 304 source
refusals / 43 provenance-depth / 24 method / 53 capture controls**. Consumers
expand before cells and callable holders are retired; the complete DOM proof still
gates publication. The original two optional-return sources execute unchanged.
**122715d8** additionally expands nested capturing helpers and proves forwarded
immutable slots at each invocation: **249 Node/VM observations / eight GCC-Clang
binaries / 336 source refusals / 44 provenance-depth / 24 method / 74 capture controls**.
The original forwarded optional-return source executes unchanged. Shared leaf queries
remain unchanged; mixed slots and composed depth receive the private DOM proof.
**4ef7649c** additionally proves immutable block-local setup of one exported DOM
entry: **285 Node/VM observations / eight GCC-Clang binaries / 392 source refusals /
44 provenance-depth / 24 method / 113 capture-initialization-budget controls**.
A private invocation follows all initialization, including writes after publication;
existing expansion eliminates every setup cell/holder/callable before DOM reproof.
Top-level global bindings and factory calls remain refused. Original Bootstrap still
needs its factory/global initialization and the complete H/F/M source graph.
For `F('config')`, a no-match fold must prove standard String replacement and
RegExp replacement/execution/property lookup, plus absence of source mutation or
reentry. The current DOM manifest supplies none of those intrinsic identities.
The original regex helper remains a source refusal in the gate. `M` still consumes
the live optional attribute value: preserve its normalization source instead of
replacing it with browser helper code.

The compiler still refuses dataset operations. Bootstrap's original
`getDataAttributes` (vendor lines **253–261**) needs `Object.keys`, filtering, a
loop and dynamic reads. Prove those source uses and preserve live read order;
a missing supported own property alone does not prove the prototype lookup misses.
Native optional strings must use ordinary `std::optional<std::string>` storage.
Keep Bootstrap's parsing and config merge in its source. Disposal calls `P.off`,
which writes `uidEvent` and initializes registry state even without listeners;
it cannot be omitted. Retained events will need a plain platform callback seam.

A construct/toggle/dispose lifecycle can run entirely within one entry call.
The current entry recreates its Data/recorder/payload on every invocation, as
the source does. Persistent initialization followed by later actions requires a
separate ownership proof; deleting the existing resets changes the program.
Full native Bootstrap startup, components, retained callbacks and the application
driver remain unfinished.

## Earlier boundary survey

Surveyed on 2026-09-13 at `a4458ae1`; shared DOM token extraction landed as
`1e71c6ad` after the browser and compiler gates described in HANDOFF. Bootstrap is
**5.3.8**, **133,701 bytes**, SHA256
`5b29f1692a632853edc37b45bc1deedd595a777c9b234e8262ccd74ebfcf3d65`.

The subsequent [typed DOM entry](native-dom-entry.md) connects source functions
to real document/node handles and the shared token/attribute APIs. Its separate
action gate covers synchronous borrowed parameters, identity, mutation and errors.
Retained DOM-backed Data keys, original Button receivers/construction and full
bundle initialization remain the next boundaries; the survey below is not a
native Bootstrap completion claim. Synchronous query entries now call public
DOM containment and Style matching/closest, including Bootstrap's delegated
`[data-bs-toggle="button"]` ancestor lookup. They borrow the caller's live
Style engine and keep nullable closest results local to identity comparisons.
The shared closest implementation preserves `:scope` and shadow boundaries.

The separate `closed-source-session-v1` provider now uses the same manifest fields
and complete source ownership proof as `closed-source-v1`, and additionally requires
a captured method table. It emits Data methods as members of a noncopyable,
nonmovable table. Every source method read must feed its proved direct call;
an extracted callable cannot carry the captures away.
The existing `closed-source-v1` owning-callable contract stays available.

The Data session now owns its outer Map by value; its methods pass that member
directly to their source functions. The three redundant capture tuples and their
initialization are gone, and the table occupies exactly its Map storage.
Measured on 2026-09-14 with GCC13.3/Clang18.1.3: **48 → 24 bytes** per table;
the same generated probe loses **1793 bytes / 34 lines**, excluding test assertions.
Saved child Maps and payloads retain their independent ownership;
removal, reinsertion and session destruction must preserve those saved values.
The table and ordinary object carriers keep their existing ownership contract.
This completes the method-call and outer-Map prerequisites for a Data + DOM session;
it accepts no DOM parameters or keys. Owning atoms, document, then Data state in
a single session and proving document-domain key provenance remain the next boundary.
The registered `ctcompile_native_data_session` gate uses the same pinned Data
probe, with direct call order, typed Node/VM observations and lifetime checks.

The compiler now separately records complete-family `outerKeyObjects`: source
allocations used only as direct outer Map keys, with every actual use and named
alias rechecked. Payloads, child keys, field uses, transport and outer snapshots
exclude that role; ordinary owning-object admission remains. The pinned Data
probe has three such allocations (`element`, `other`, `absent`), excluding its
payload. This is source-use evidence, not DOM provenance or permission to retain
an externally supplied handle. Recovery commits `722ddd82` and `cb76be27` passed
the full **562/562 CTest / 168/168 lit** gate on 2026-09-14. The generated Data
probe remains byte-identical, and full Bootstrap admission remains **19/574**.
The combined Data/DOM boundary still requires explicit DOM origins across the
Data family and Data storage attached to the document lifetime. The subsequent
owning action and direct-array milestones below narrow the remaining work.

The separate `ctbrowser-dom-session-v1` action provider now owns atoms, its document
and an optional live Style engine in a nonmovable C++ class. Explicit element inputs
must belong to that document; all owner comparisons precede handle validation and
source effects. This completes the synchronous DOM owner, with no retained Data
keys or callbacks. Connecting it to Data still requires actual DOM origins in the
complete family proof and removal of escaping shared table/global ownership for
that mode. Distinct input parameters can alias the same node and must not be
assumed unequal. See [native-dom-entry.md](native-dom-entry.md) for the entry API.

The complete-family proof now records outer-key formal parameters separately from
caller allocation roles (**23d360db**). A getter's parameter can retain its role
when the caller also passes that same object as a sibling payload. DOM origins,
conservative input aliases and nonescaping storage must still be proved separately.

The `ctbrowser-dom-data-session-v1` contract proves explicit DOM actual origins
across the complete family, separately from source allocations. Inputs may alias.
**e967db10** also proves the imported inert declaration wrapper and rejects detached
method reads or whole-table aliases. The CLI gate covers seven imported functions,
two external inputs, alias/reentry observations and eleven source refusals; the IR
matrix has 31 rows per source form. Native storage remains refused until it is
private to the document owner. The source entry recreates Data on each invocation;
emission must preserve that reset. See [Data input provenance](native-dom-entry.md#data-input-provenance).

**acf98caa** uses the complete current array contents proof to narrow direct reads.
The original read-fed value/index overwrite sources now emit unchanged, with both
policies/layouts/compilers and signed-zero observations gated. Whole-function
failure or an unproved index keeps the optional fallback; no vector alias ownership
permission was added. Final focused 14/14 CTests pass in 30.78s, including the
existing Data session and twelve new array checks.

The preserved `array-overwrite-loop.js` now improves **0/2 → 2/2 native**, both
policies, through **c4b7cf8c**. Exact `1/0` guard recovery exposes the existing
header/body contents certificate; checked upstream SCF while patterns remove
the temporary poison/flag transport. No escape proof was weakened. Every pattern
application checks for duplicate forwarding, including aliases introduced during
cleanup and moved nested loops. **c65ad9cb** also guards before-region passthrough
uses and makes dominance data local to each function, fixing Bootstrap's do-while
and p5/Phaser crashes exposed by the full gate. Corrected **5/5 lit / 47/47 CTests
PASS in 24.08s** cover all native claims, both loop policies/layouts/compilers, VM
observations and the complete oracle.
The corrected full gate passed **573/573 CTests in 1226.65s / 169/169 lit in
870.79s**, with all **1418 frozen inputs** matching local/devbox. Fresh full Bootstrap
remains **19/574 native / 0 of 43 globals**, both policies, no skips/prunes.
[HANDOFF](HANDOFF.md) records the measurements and the initial failed gate.

## What is measured

The final gate after **e967db10 / acf98caa** passes **585/585 CTests in
1283.39s**, including **170/170 lit in 846.62s**. All **1,422 frozen inputs**
match local/devbox. The Data, DOM, escape and full-bundle rows below were
remeasured with the same results; the new imported DOM Data contract remains
analysis-only. [HANDOFF](HANDOFF.md) names the evidence and exact next boundary.

| Gate | What it establishes | What remains outside it |
| --- | --- | --- |
| Full bundle: **19/574 native**, both optimization policies; no skipped or pruned functions | Admission of individual functions from the unchanged vendor source | Native initialization, an interactive component, or a native application |
| Browser Data/UMD probe: **7/7** with manifest, prefix specialization and explicit 1m budget | Ownership and execution of the extracted Data methods and wrapper, including their preserved observations | A real Window, DOM nodes, Bootstrap constructors, event registration |
| Data session probe: **7/7**, 23 direct calls and 19 typed observations | Nonmovable method table owns its outer Map by value; source methods access it directly without stored captures and cannot escape independently; compile-clean and saved-child/payload lifetime gates | Atoms/document ownership and retained DOM keys; table, child-Map and ordinary object ownership remain |
| Full bundle: **0/43 globals resolved** | Current module-wide global-name census refuses | This is not a count of 43 missing browser APIs |
| Called local array-overwrite fixture: **0/6 → 6/6 native**, both policies | Live own-element write proof, stored-value type joins and direct vector assignments; unchanged source | Broader loop forms and a preserved vendor admission gain |
| Called local array-overwrite-loop fixture: **0/2 → 2/2 native**, both policies | Exact imported guard normalization lets the existing complete contents proof authorize the unchanged source | Duplicate guard forwarding, broader aliases and a preserved vendor admission gain |
| Generic escape oracle: **40/172 precision**, zero violations in the recorded snapshot | Independent analysis evidence; complete current contents now feed the local vector density check | General retained-graph ownership and refined escape verdicts remain outside native emission |

Fresh full-source IR names the leading first refusals: **272 `this` receivers**,
**137 own closures**, **70 unproved boxed parameters**, **29 closures passed to
callees without a specialized native call contract**, and **15 lexical-`this`
arrows**. These counts identify current blockers; they do not predict how many functions a single fix
will admit.

The seven-function program is a **source-derived probe**, not the complete original
bundle. [The extractor](../../tools/check/bootstrap-data-probe.py) cuts the source
at the Data/transitionend boundary, returns Data and adds a test environment with
ordinary `{}` keys and scalar/leaf-object payloads. Its byte pins protect that probe.
They do not demonstrate DOM-backed Bootstrap startup.

The full-bundle census runs import, global resolution and native lowering without
a browser manifest. The registered host-prefix tests use the same Data extractor;
there is no registered full-bundle browser-manifest/native-startup gate yet. Function
admission is not a percentage of project completion. Reported refusal categories
show only each function's first failure; fixing one exposes its downstream failures.

## The next browser path

1. **Extend the typed browser entry to retained DOM-backed Data keys.**
   Synchronous borrowed element entry and direct token/attribute calls now exist
   through the `ctbrowser-dom-v1` provider. It cannot retain handles or combine its
   contract with the existing source-owned Data provider. The next proof must
   establish that each owning document outlives the stored keys and every future
   Data invocation, using ordinary C++ ownership.
   The existing owning-callable provider permits extracted callables to outlive
   their owner; its session variant prevents method extraction, but the table and
   global carriers can still escape. Borrowing an element into either escaping
   carrier would dangle. Also, `document` borrows its atom table. Extend the owned
   DOM session with private Data storage declared after atoms and document, so Data
   is destroyed first. Keep this distinct from the existing owning Data-callable
   contract, and give DOM keys their own provenance instead of treating them as
   source-created ordinary objects.
   The new analysis-only Data contract supplies explicit DOM origins and matches
   them to `outerKeyParameters` across the full method family. Its separate
   `HostMethodArgument.element` and `outerKeyInputs` evidence rejects payloads,
   child keys, outer snapshots, returns and input aliases through globals.
   Consume and revalidate that live evidence before type inference or emission;
   the ordinary `objectKeys` category also permits owning payloads and cannot
   authorize DOM storage. Distinct external parameters may denote the same node;
   the entry replay conservatively retains that uncertainty. Compare
   foreign document ownership before validation dereferences the owner pointer.
   Reuse `ctbrowser::document` and `node_id` from the public DOM API. Bind their
   identity, ownership and permitted calls through the existing HostContract,
   inference/admission and emission machinery. A host declaration must prove which
   receiver an operation belongs to; the spelling `querySelector` alone is not proof.
   Start with the preserved Data methods using actual DOM node keys: repeated lookup
   must preserve identity, distinct nodes must differ, and detaching a node must not
   free it or confuse Data entries. Prove a single document domain or preserve
   document identity too: node IDs from different documents can have equal bits.
   The document must outlive borrowed handles. `element_ref` currently supplies
   equality only, while the non-iterating native Map uses `std::less<K>`; prove its
   key ordering as well as the insertion-order representation. Test a source with
   no Map snapshots too: Bootstrap Data's child-key snapshot selects insertion-order
   storage module-wide and would otherwise hide that comparator path.
   The remaining implementation spans `OwnedGlobalRoots`, DOM input seeding in
   `TypeInference`, `LoweringSupport.cpp`'s Map/table carriers, and EmitC
   `OwnedGlobals.cpp`, `MethodTables.cpp` and `DOM.cpp`. Replacing only the outer
   Map leaves shared ownership in the table and global root. The imported inert
   wrapper now has a bounded source proof. Revalidate it before omission, preserve
   source-driven root/Map/table replacement on each invocation, and keep all Data
   access private. A persistent initialization/action split needs separate proof.
   This is a **Data + DOM** milestone with its own denominator.
2. **Compile a real Button action, then its construction and lifetime.**
   [Button.toggle](../../ctbrowser/vendor/bootstrap/bootstrap.bundle.js#L424) toggles
   `active` and writes `aria-pressed`. Two calls on a real node must produce
   active/true then inactive/false. An action-only bridge is useful, but the original
   `BaseComponent` constructor adds inheritance, `super`, `_element` and `_config`,
   static constructor getters and publication of `this` into Data. Current
   [constructor lifting](../lib/CTNative/Lowering/ClosureLifting/Constructors.cpp)
   explicitly refuses prototype access; closed scalar literals do not prove this
   object graph. Disposal must preserve registration and alias lifetimes.
3. **Support retained browser callbacks and real event delivery.**
   Bootstrap registers delegated document listeners during factory initialization,
   and queues jQuery hooks through `DOMContentLoaded`. EventTarget must retain typed
   callbacks with exact receiver, identity/removal, capture/once and cancellation
   semantics. A callback can remove listeners, dispose a component or reenter
   dispatch. Existing source-owned Map closure checks do not authorize a browser
   event queue to retain that closure.
4. **Add transitions and geometry after the synchronous path.**
   No-fade Alert still requires cancelable close/closed events, a lexical-`this`
   callback, element removal and disposal. Fade/Collapse additionally need timers,
   transitionend, shared completion state, layout flushes, computed styles and
   dimensions. Popper-backed components need more geometry. Each should use the
   same platform implementation as the interpreter.
5. **Connect native compilation to the application driver.**
   [The current CLI](../tools/ctcompile/ctcompile.cpp) packages bytecode images with
   a fixed VM launcher. It does not generate native C++. Reuse asset discovery and
   packaging, then require an executable that initializes the real library, handles
   a click and tears down without a Script dependency. Keep an untouched-bundle gate
   separate from component/action probes throughout.

## Reuse the browser, with the actual dependency boundary

The DOM, selector/style, layout and painting subsystems already expose ordinary
C++ APIs. The implementation namespace for `document`/`node_id` is `ctbrowser`;
`dom/` is their header directory. Lift missing platform behavior out of the VM
binding, make the binding convert values and call it, and let generated code call
that same core. Do not duplicate token parsing, selectors, events or layout in
ctcompile.

The existing [AOT entry ABI](../../ctbrowser/include/ctbrowser/aot/aot_entry.h)
explicitly uses `script::context` and boxed values. The current Shell target also
links Script. Neither is already a native browser entry. Extend the public seams
with typed C++ operations and owners while retaining that dependency boundary.
The old generator helper was retired in `71952bf3`; Bootstrap's immediate need is
callbacks/timers, not restoring unused generator machinery.

The initial shared-token extraction exposes `parse_ordered_tokens`, `validate_token`,
`update_tokens` and `toggle_token` in
[dom/token_list.hpp](../../ctbrowser/include/ctbrowser/dom/token_list.hpp).
They work for an associated attribute, not just `class`. The classList binding and
class-name collections reuse the same implementation. Native clients check token
validation and DOM-write results; the adapter preserves its existing behavior for
failed writes. Forced no-ops preserve raw whitespace/duplicates, while an actual
same-byte update still records a mutation. This prepares the Button API; it does
not add compiler DOM types or increase native Bootstrap admission.

## Parallel work worth doing

- Compiler: atoms/document ownership and DOM-key provenance in the direct-method
  Data session; then the exact
  constructor/prototype/receiver proof required by Button.
- Browser, in a claimed isolated worktree: shared event/timer behavior and typed
  callback ownership beyond the current token/attribute/selector APIs. Preserve
  browser behavior.
- Validation: real DOM/component observations, lifecycle and no-Script link gates;
  keep original full-source coverage visible.

CommonJS replacement/old-exports alias support remains valid provider work, but it
is not required to take the browser branch of Bootstrap's wrapper. The original
`confinedArray` now passes its bounded read-only zero/+1 escape proof.
`TypeInference::isDenseVectorSite` now consumes complete current `computeArrayContents`
write evidence for direct local overwrites. Stored values join the element type;
admission requires definite Number indices and values, and emission records accesses
before retyping invalidates the proof. The called fixture preserves its source while
moving from refusal to execution under both policies. The preserved counted-loop
fixture now also emits through checked guard normalization and the complete contents
proof. Broader loop/alias transport, retained graph ownership and a preserved vendor
admission gain remain separate work.

The `window.scrollTo` receiver in ScrollSpy triggers the current global-object
escape reason. [Native.cmake](../test/cmake/Native.cmake) also records why removing
that global guard alone does not resolve vendor host bindings: those names are not
closures declared in the bundle. A name-resolution workaround is not the browser
bridge.

Validation and landed commit IDs are recorded in [HANDOFF.md](HANDOFF.md).
Scratch evidence, exact full-source IR and standalone link commands are under
`/tmp/ctcompile-bootstrap-survey/` for this session.
