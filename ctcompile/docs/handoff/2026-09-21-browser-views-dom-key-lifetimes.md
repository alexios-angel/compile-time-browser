# Typed browser views and sequential DOM Map keys, 2026-09-21

Continued clean `4b76250f`, with no earlier unfinished code. The user reiterated
the ctbrowser integration direction during the work. The subsequent interruption
left only the alias proof and its focused tests uncommitted; that exact thread
was resumed. All work stays in ctcompile; public browser behavior is unchanged.

## Browser integration

`dee4ff0a` adds `Runtime/Browser.hpp`. `js_document_t` borrows a public document
and its live Style engine; `js_element_t` additionally carries the node identity.
Neither view owns resources or extends their lifetime. Document, atom table and
Style must remain alive at stable addresses. Element equality includes document
identity. Construction and source operations validate the element and Style
association. Root access rereads the current document root.

The document view exposes `documentElement`, `querySelector` and `querySelectorAll`.
The element view exposes all four existing selectors, including `matches` and
`closest`. Nullable results use `optional<js_element_t>` for DOM null; snapshots
own their vector while each element still borrows the browser resources. The
four `Element.prototype` methods accept a typed receiver without a separate
Style argument. They delegate to the existing public DOM/Style implementation.

`32f5311b` emits proved `matches` calls through this typed receiver and an owning
`js_string`. Other selector results retain their existing raw carriers. Generated
selector entries include the browser view header; primitive-only entries retain
the smaller header. Source identity, effect, owner and nullability proofs remain
unchanged. The native symbol gate still excludes Script/VM dependencies.

## Sequential DOM keys

`c90e4545` changes nested Map normalization to require at most one **live** DOM-input key in
an outer Map. Once deletion/cleanup or clear retires that key, another declared
DOM formal can be used: equal and distinct inputs both begin with an absent
DOM-key entry. Literal String and fresh-object entries may remain. Saved child
Maps and records retain their own allocation identities after the outer key dies.
Overlapping DOM-key lifetimes still require a stronger alias proof.

Preparation can therefore discharge every use of an input. The provider now
accepts an already validated formal with only Root uses or no uses, without
claiming it belongs to the published Map family. The owner/type/lowering pipeline
still retains every declared parameter and validates its document and node before
source effects. A remaining property read or returned identity must still refuse.

The focused fixture retains complete vendor Data declarations, constructor bodies,
five constructions and eight functions in each prepared publication. Four sources
exercise holder and constructor cleanup, resident String/fresh keys, clear,
replacement, saved child aliases and switching back to the first formal. Expected
results are 15927, 59112, 15939 and 15951 for equal and distinct inputs.

The two original handoff witnesses remain refused. With equal inputs they publish
15927/59112; with distinct inputs they dereference the absent old key and throw
TypeError before assigning the result. The new proof does not fold those programs
to the equal-input answer.

## Validation state

- Explicit devbox targets: `ctjs-opt`, `ctjs-translate`,
  `ctcompile-test-native-runtime`, `ctcompile-test-host-contract`,
  `ctcompile-test-native-reference`; later provider rebuild uses only
  `ctjs-opt` and `ctcompile-test-host-contract`.
- Exact `ctcompile_native_runtime` and `ctcompile_host_contract` CTests pass
  2/2, 0.57 s total. The later host-contract check passes 1/1, 0.55 s total.
- Selected `CTNative/Browser/native-dom-prototype-query.test` passes:
  64 native executions and 100 refusals, GCC/Clang, both lowering policies,
  explicit/deduced output, borrowed/owned entries. The two-case initial lit run
  took 166.76 s and had one failure in the alias fixture's provider stage.
- Final selected `CTNative/Browser/native-class-dom-alias.test` and
  `CTNative/HostContract/dom-data-inputs.test` pass, 2/2 in 51.58 s. The alias case
  passes six Node/VM observations, four preparations, four provider proofs,
  32 native executions and 23 refusals. The earlier 51.67 s alias replay reached
  all four native positives but failed on an overly specific negative diagnostic:
  removing a helper call triggered its earlier captured-owner refusal. The final
  fixture keeps that helper call and exercises the intended live-key boundary.
  Fresh-IR observer and return mutations withdraw the unused-input provider proof.
- The first build rejected a test assignment to the nonassignable Style engine;
  that hypothetical test was removed and the rebuild passed. A PATH formatter
  disagreed with the pinned formatter on an unrelated line; the original form
  was restored. Pinned scoped formatting passes. Required repository formatting
  retains the 16 pre-existing diagnostics in ctdrive, ProviderPaths, Heap and Facts.
- All eleven changed code/test files match the devbox by SHA-256. Python syntax
  and scoped Black checks pass. The working source has no browser/shared edits.
- Full CTest/lit, broad corpus/matrix, WPT and test262 were not run.

## Next boundary

Source `document` globals and default `document.documentElement` arguments still
need an explicit host binding and invocation/session ownership proof. The C++
view itself grants no source authority. Nullable/snapshot selector emission still
needs migration to the typed view result flow. The application driver is incomplete.

A bounded next implementation could declare a current-document anchor explicitly
in the DOM contract, referring to one validated element parameter's owner and live
Style. This is a proposal, not an implemented contract field. It can reuse the
existing entry ABI and owned session, avoiding ambient state. The relevant seam is
HostContract parsing, DOMEntry's global/member proof, live TypeInference/admission
queries and EmitC's DOM census. Emit `js_document_t` operations and preserve
nullable roots/results; a full nullable-carrier migration need not come first.

Start with guarded `document.documentElement` identity and
`document.querySelector` followed by guarded `matches`/attribute mutation. Exercise
empty/removed/replaced roots, root-inclusive document queries, detached anchors,
live hover and separate sessions with equal node-ID bits. Missing binding,
replacement, unguarded dereference, escape and forged/stale proof remain refusals.
The existing default-document fixtures dereference a potentially absent root and
must not silently become accepted with this smaller binding slice.

The original overlapping two-input Map programs need equality-dependent behavior,
including their distinct-input throw; do not treat different formals as distinct
nodes. Original B/Data+B, object-valued class-field ownership, tagged Map snapshots,
broader String operations, conditional callees and mutable cells also remain.
