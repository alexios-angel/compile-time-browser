# What native Bootstrap needs next

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

The direct-array counted contents certificate has also landed. The preserved
`array-overwrite-loop.js` still measures **0/2 native**, both policies before/after:
the imported loop has a branch inside its header and poison/arith flag transport.
Proving that complete shape is next; recognizing the direct array alone did not
produce an emission gain.
Both milestones passed the full **563/563 CTest / 169/169 lit** gate; all **1418
frozen inputs** match the devbox. Full Bootstrap stays **19/574**, both policies.

## What is measured

| Gate | What it establishes | What remains outside it |
| --- | --- | --- |
| Full bundle: **19/574 native**, both optimization policies; no skipped or pruned functions | Admission of individual functions from the unchanged vendor source | Native initialization, an interactive component, or a native application |
| Browser Data/UMD probe: **7/7** with manifest, prefix specialization and explicit 1m budget | Ownership and execution of the extracted Data methods and wrapper, including their preserved observations | A real Window, DOM nodes, Bootstrap constructors, event registration |
| Data session probe: **7/7**, 23 direct calls and 19 typed observations | Nonmovable method table owns its outer Map by value; source methods access it directly without stored captures and cannot escape independently; compile-clean and saved-child/payload lifetime gates | Atoms/document ownership and retained DOM keys; table, child-Map and ordinary object ownership remain |
| Full bundle: **0/43 globals resolved** | Current module-wide global-name census refuses | This is not a count of 43 missing browser APIs |
| Called local array-overwrite fixture: **0/6 → 6/6 native**, both policies | Live own-element write proof, stored-value type joins and direct vector assignments; unchanged source | Structured-loop transport and a preserved vendor admission gain |
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
   The current Data probe permits extracted callables to outlive their owner;
   borrowing an element into that carrier would dangle. Also, `document` borrows
   its atom table. Prove a nonmovable session owning atoms, document, then Data
   state in that declaration order. The separate session provider has established
   direct/member calls that cannot escape independently; extend that proof with
   document ownership. Keep this distinct from the existing owning Data-callable
   contract, and give DOM keys their own provenance instead of treating them as
   source-created ordinary objects.
   The current entry proof accepts only three implicit script arguments, and
   its actual-object census requires source allocations. An owned-session input
   contract must supply DOM origins explicitly. The existing `objectKeys` category
   permits both keys and payloads: give DOM values a role limited to outer Map keys,
   initially refusing payloads, child keys, snapshots and returns. Revalidate that
   role across the full method family before type inference or emission. Compare
   foreign document ownership before validation dereferences the owner pointer.
   Reuse `ctbrowser::document` and `node_id` from the public DOM API. Bind their
   identity, ownership and permitted calls through the existing HostContract,
   inference/admission and emission machinery. A host declaration must prove which
   receiver an operation belongs to; the spelling `querySelector` alone is not proof.
   Start with the preserved Data methods using actual DOM node keys: repeated lookup
   must preserve identity, distinct nodes must differ, and detaching a node must not
   free it or confuse Data entries. Prove a single document domain or preserve
   document identity too: node IDs from different documents can have equal bits.
   The document must outlive borrowed handles.
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
moving from refusal to execution under both policies. Structured-loop transport,
retained graph ownership and a preserved vendor admission gain remain separate work.

The `window.scrollTo` receiver in ScrollSpy triggers the current global-object
escape reason. [Native.cmake](../test/cmake/Native.cmake) also records why removing
that global guard alone does not resolve vendor host bindings: those names are not
closures declared in the bundle. A name-resolution workaround is not the browser
bridge.

Validation and landed commit IDs are recorded in [HANDOFF.md](HANDOFF.md).
Scratch evidence, exact full-source IR and standalone link commands are under
`/tmp/ctcompile-bootstrap-survey/` for this session.
