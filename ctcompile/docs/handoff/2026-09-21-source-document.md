# Explicit source document binding, 2026-09-21

Continued clean `8420a066`, following its next browser boundary. The prior typed
browser views were already committed; `dbc75a2b` connects source document reads
and queries to those views. Parallel work covered the contract schema, execution
fixtures and source/owner review. All implementation changes stay in ctcompile.

## Implemented boundary

The fingerprinted `ctbrowser-dom-v1` and `ctbrowser-dom-session-v1` contracts now
accept `current_document_parameter`, a zero-based declared element input index.
The input's document owner and live Style engine supply the source `document`
binding. The field supplies no authority to other providers or arbitrary globals.
Every declared element and required Style association is checked before effects.
The existing nonmovable session owns its resources; borrowed entries keep their
existing caller-owned lifetime contract. No ambient or thread-local state is used.

The live source proof admits `document.documentElement` and
`document.querySelector(String)`, including a local document alias. It proves the
original accessor/method and exact call receiver, excludes replacement/reentry,
and retains the existing guarded nullable-element use rules. Escaping the document,
its methods or element results still refuses. Proof-only document loads disappear
from native output. Type admission and lowering consume the successful live proof,
not printed report attributes.

Generated code constructs a borrowed `js_document_t` over that explicit owner and
Style engine, then calls its accessor or method. Those methods reuse public
ctbrowser DOM/Style. A small `element_or_null` bridge returns the existing null-only
element carrier from `optional<js_element_t>`; downstream identity, guard and Style
flow remain unchanged. Root reads occur at the source operation, so replacement
and removal are observed. No root cache or alternate browser implementation was
added, and the Script/VM symbol gate remains in force.

## Focused validation

- Devbox targets: `ctjs-opt`, `ctjs-translate`, `ctcompile-test-host-contract`,
  serialized under the shared build lock. The first build found a missing explicit
  EmitC dereference result type in the new emitter branch; it was fixed, and the
  second build passed.
- Exact `ctcompile_host_contract` CTest: 1/1 passed, 0.56 s (0.57 s total).
  New schema controls cover both DOM providers, absent/unused binding, either
  input index, malformed/out-of-range values and foreign/programmatic providers.
- Selected `CTNative/Browser/native-dom-document.test`: 1/1 passed, 121.50 s;
  **48 native executions and 82 refusals**, GCC/Clang, both lowering policies,
  explicit/deduced printing, borrowed and owned entries. Checks cover empty,
  removed and replaced roots; detached anchors; root-inclusive queries; live
  hover; cross-document identity; invalid-selector effect order; invalid inputs
  before effects; wrong Style association; binding, mutation, escape, null guard,
  stale fingerprint and zero-budget refusals. These use public DOM/Style expected
  behavior; no new Node/VM differential measurement is claimed.
- Selected `CTNative/Browser/native-dom-prototype-query.test`: 1/1 passed,
  167.73 s; **64 native executions and 100 refusals**. Its original default-document
  refusals remain unchanged.
- All 17 changed code/test files match the devbox by SHA-256. Pinned scoped
  clang-format, Python AST/Black and `git diff --check` pass. Required
  `tools/format.sh --check` still stops on 16 untouched formatting diagnostics in
  ctdrive.cpp, ProviderPaths.h, Heap.h and Facts.cpp; it is not a repository-wide
  formatting pass.
- Full CTest/lit, broad corpus/matrix, WPT, test262, Windows and sanitizers were
  not run. No browser/shared implementation changed. Linux `/proc` and Windows
  CIM process checks completed at iteration start with no Claude process found;
  the result was recorded in the synchronization journal.

## Next boundary

The C++ document view already provides `querySelectorAll`, but source
`document.querySelectorAll` still lacks host/type/emission proof. Connect its
owning snapshot to the existing proved element-vector consumers while preserving
the document/Style association for every borrowed member. Keep NodeList snapshot
semantics distinct from Array identity and validate bounds before element use.

Bootstrap's default `document.documentElement` argument remains a separate
presence/nullability obligation. The new anchor field does not prove a root exists:
retain unguarded-root refusals until the complete source guard or a checked entry
precondition establishes presence. The application driver, migration of remaining
nullable/snapshot selector results to typed views and broader source composition
remain incomplete.

Overlapping DOM Map key lifetimes, original B/Data+B, object-valued class fields,
tagged Map snapshots, broader Strings, conditional callees and mutable cells are
also unfinished. Full Bootstrap is not admitted.
