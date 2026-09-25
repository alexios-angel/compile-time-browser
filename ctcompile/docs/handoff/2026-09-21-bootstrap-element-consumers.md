# Bootstrap indexed element consumers, 2026-09-21

Continued clean `0ce0bdf3`. The prior interrupted default-root thread was fully
committed; its handoff named indexed/iterated `R.find` elements as the next
boundary. Three bounded agents handled typed emission, a complete-source fixture
and proof review/host controls while root implemented the snapshot proof. Root
finished the saved two-file typed emission draft after that agent was rate-limited.

`bebdc40d` emits admitted element `querySelectorAll` calls through the existing
typed receiver and `std::vector<ctnative::js_element_t>`, matching document
query-all. Checked `.at(index).value()` extraction reuses existing DOM operations
and retains live Style. Existing source and refusal bodies are unchanged.

`6c32120f` extends the confined Bootstrap spread/concat proof to canonical indexed
loops. The original vendor-pinned `R.find` body is preserved. Before rebinding a
copied element read to the original snapshot, its loop must test that exact index
against the copy's own length. This prevents an uncapped original NodeList alias
from authorizing an index beyond the copy's proxy materialization cap.

The copied length remains `min(snapshot.length, 16777216)`. The existing DOM index
proof recognizes that exact live minimum, including both yield arms, and checks
the same snapshot, zero/unit-step progression, complete effects and borrowing.
Original query aliases stay uncapped. Supported attribute/class writes cannot
reclaim nodes or alter saved membership. Array identity, result mutation, unknown
indices, borrowed escapes and unproved hooks remain refused. All normalization
runs in a private clone and publishes only after complete reproof.

The native vector owns membership; its views borrow the caller/session's document
and Style. No browser behavior was copied, no runtime API or browser/shared
implementation changed, and generated code/binaries have no Script dependency.

## Focused validation

- Devbox builds used explicit `ctjs-opt`, `ctjs-translate` and
  `ctcompile-test-host-contract` targets under the shared build lock.
  The first build compiled production code but failed a test-only
  `ConstantOp.setValue` call; it was corrected to the existing `setValueAttr` API.
  The second build passed. No test ran from the failed build.
- Exact `ctcompile_host_contract`: **1/1, 0.64 s; 0.66 s total**. The new indexed
  source mutates attributes and observes the original NodeList length afterward.
  Controls reject a copied index guarded by that original length, wrong bounds,
  increments and values, array/member escapes, result mutation and structural
  mutation. Both providers and required intrinsic identities are covered.
  Exact-budget checks leave refused normalization unchanged and incomplete
  proofs without evidence; stale/fresh IR cap/direction/arm mutations refuse
  even with forged printed reports.
- Three selected lit cases passed **3/3, 141.75 s**:
  `CTNative/Browser/native-dom-find-elements.test` **48 native executions and
  114 refusals**; existing `native-dom-query-all.test` **16/44**; existing
  `native-dom-spread-length.test` **32/52**. No full compiler suite ran.
  The new fixture keeps the original helper with explicit receiver, guarded
  omitted receiver and guarded explicit undefined. Borrowed/owned providers,
  GCC/Clang, explicit/deduced printing and both optimization policies cover
  tree-order writes, duplicate selectors, identity, live hover, saved membership
  through class removal, absent/replaced roots, detached inputs, shadow/outside
  exclusion, invalid-selector effect order, input/Style rejection and restoration
  of the outer document binding. Emitted text and linked binaries exclude Script.
- All eight code/test SHA-256 hashes match the devbox. Pinned scoped formatting
  of five C++ files, Black/AST checks of two Python fixtures and `git diff --check`
  pass. Required `tools/format.sh --check` retains 16 untouched diagnostics in
  `ctdrive.cpp`, `ProviderPaths.h`, `Heap.h` and `Facts.cpp`; this is not a
  repository-wide formatting pass.
- Initial availability check inspected 76 Linux cmdline/comm identities and
  345 Windows CIM Name/ExecutablePath/CommandLine records, finding no Claude
  executable, Node CLI or loop and no errors. No broader browser authorization
  was needed for these ctcompile-only changes.
- Full CTest/compiler lit, broad corpus/matrix, WPT, test262, Windows, new
  sanitizers, local native builds and push were skipped. The cap is checked in
  live IR; no collection above the 2^24 cap was allocated. No new Node/VM
  differential result or full-Bootstrap admission is claimed.

## Next boundary

Prove `for…of` element consumers while preserving the original `R.find` helper.
`normalizeDOMIteration` currently proves String snapshot iteration; element
consumers need the copied cap, exact iterator helper identities, owner/Style
borrowing and the source document-root guard to survive normalization. Reuse the
existing typed snapshot and public DOM operations rather than adding a VM path.

Unguarded default roots still need a root guarantee. General Array behavior,
application initialization/driver, overlapping DOM Map keys, original B/Data+B,
object-valued fields, tagged Map snapshots, broader Strings, conditional callees
and mutable cells remain unfinished. Full Bootstrap is not admitted.
