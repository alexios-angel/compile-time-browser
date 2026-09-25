# Typed document query snapshots, 2026-09-21

Continued clean `c786ecd6` and resumed the saved nine-path query-all draft after
the iteration was interrupted. The interrupted build had stopped in the new
indexed-result type selection; no native check had run at that point. Parallel
agents supplied the execution fixture, live-proof controls and documentation.

## Native document snapshots

`d5149745` makes source `document.querySelectorAll(String)` use the existing explicit
`current_document_parameter` binding and `js_document_t` view. The successful
live proof supplies the exact method, receiver, document owner and Style engine.
Missing authority, replacement, unsupported effects and escaped borrows refuse.
Fresh fingerprints and printed reports cannot authorize a changed receiver.
Every incomplete proof budget withholds the document and snapshot evidence.

The result remains a local `std::vector<ctnative::js_element_t>`. Its sequence is
owned; its elements borrow the stable caller/session document and live Style.
The existing snapshot proof admits length and zero-based, unit-increment indexed
loops guarded by that same snapshot's length. Indexed emission uses `.at()` and
checked `.value()` extraction for existing element operations, retaining Style.
There is no whole-vector conversion, NodeList runtime object or Script dependency.
Snapshot mutation, identity, retained callbacks, returns and borrowed joins remain
refused. Dataset-enabled mutating loops retain their separate alias boundary.

Document selection includes the root and respects public Style's tree order,
deduplication and shadow boundaries. Membership-changing attribute/class writes
do not change the saved sequence; a later query takes a fresh snapshot. Empty,
removed and replaced roots and detached document anchors use the same public core.

## Browser collection correction

`607cf1be` changes only `ctbrowser/lib/Shell/bindings/document/collections.cpp`
and `ctbrowser/unittests/unit/dom_nodes_wpt.cpp`. The collection `get` trap no
longer rejects every numeric index above 1,000,000. Its supported indices now
agree with its actual length, `item()` and indexed descriptors. Parsing still
rejects overflow and noncanonical keys, and access still checks the actual
member-vector size before one indexed read.

This corrects the browser's supported-index semantics. The removed guard could
not prevent index-proportional allocation: the read performs none, and collection
refresh already happened before the guard. The independent `ownKeys` allocation
cap and the VM's 2^24 proxy-spread materialization limit are unchanged. No browser
behavior was copied into ctcompile, and no runtime behavior was adjusted merely
to accommodate native output. Historical WPT/test262 counts remain historical.

The focused browser test covers ordinary index/item agreement and huge absent,
overflowing and noncanonical reads. A VM collection containing over one million
members was not constructed; no such measurement is claimed.

## Focused validation

- Explicit devbox targets: `ctjs-opt`, `ctjs-translate`,
  `ctcompile-test-host-contract`, then `ctbrowser-test-dom_nodes_wpt`. All build
  and test work was serialized under the shared build lock.
- The first build rejected the new ternary's different MLIR wrapper types.
  An explicit `mlir::Type` conversion fixed it; the second build passed.
- Exact `ctcompile_host_contract`: 1/1 passed, 0.55 s (0.56 s total), including
  both document providers/anchor positions, exact budgets, fresh receiver/key/
  global changes, stale fingerprints, forged reports and withheld evidence.
- Exact `dom_nodes_wpt`: 1/1 passed, 0.06 s (0.07 s total).
- Selected existing `native-dom-document.test` and `native-dom-query-all.test`:
  2/2 passed, 122.39 s; respectively 48 native executions/82 refusals and
  16 executions/44 refusals.
- The initial new document-all lit run completed all 32 native executions,
  then stopped at the test harness's entry selection for a nested-function
  refusal. The fixture now uses the existing `entry_name="bad"` option; its
  source and proof requirements were preserved. The final selected
  `native-dom-document-all.test` passes **1/1 in 87.38 s**, with **32 native
  executions and 84 refusals**, GCC/Clang, both lowering policies and printing
  layouts, borrowed/owned entries and both document-anchor positions.
- All eleven changed code/test files match the devbox by SHA-256. Pinned scoped
  formatting of nine C++ files, Python AST/Black and `git diff --check` pass.
  Required `tools/format.sh --check` retains 16 untouched diagnostics in ctdrive,
  ProviderPaths, Heap and Facts; this is not a repository formatting pass.
- Claude was confirmed stopped before browser edits and landing through complete
  Linux cmdline/comm and Windows CIM identity/CLI checks. The prelanding check
  inspected 75 Linux and 348 Windows processes with no matches or errors.
- Full CTest/lit, broad corpus/matrix, WPT, test262, Windows and sanitizers were
  not run. No new Node/VM differential result or full-Bootstrap admission is claimed.

## Next boundary

The original Bootstrap helper remains in `native_dom_spread_length.py`:
`find: (t, e = document.documentElement) =>
[].concat(...Element.prototype.querySelectorAll.call(e, t))`. Its explicit-element
call already avoids the default arm; an omitted receiver still needs the complete
document-root presence and helper/concat proof. The current document binding does
not guarantee that a root exists. Retain the unguarded-root refusals until source
control flow or a checked host precondition establishes presence.

Remaining element-selector nullable/snapshot carrier migration and the application
driver are incomplete. Overlapping DOM Map keys, original B/Data+B, object-valued
class fields, tagged Map snapshots, broader Strings, conditional callees and
mutable cells also remain. Full Bootstrap is unfinished.
