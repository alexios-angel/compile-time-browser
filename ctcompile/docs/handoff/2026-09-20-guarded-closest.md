# Native browser queries and guarded handles — 2026-09-20 UTC

Continued clean `1feb921f` after reading standing instructions, synchronization
history, both areas' commits and the terminal-publication handoff. No interrupted
dirty patch or unmerged September 7 rescue remained. The user steered this
session to integrating ctbrowser and subsequently confirmed Claude would not
run during the session. Initial process checks found no matching Claude
identities (14 readable Linux processes, 56 unreadable, 347 Windows processes),
so availability initially remained uncertain. No browser edits were needed.

Parallel review identified the next browser API boundary and corrected stale
Data-session documentation. Two other agents, and later the query implementation
agent, hit service rate limits without leaving drafts. The root completed code,
tests, devbox gates and commits. No escape-analysis change was attempted.

## Landed

- `c188f3e5`: corrected the integration guide's analysis-only description of
  Data sessions. Private document-owned storage already existed; its historical
  Bootstrap measurements remain explicitly historical, without a new replay.
- `fbb510e2`: `closest()` truthiness and negation establish branch-local
  permission to call existing DOM/Style APIs. The false branch and uses after
  the branch stay nullable. Native emission reads current receiver operands
  after replacing selector calls and propagates their original Style engine.
  Handles remain ordinary borrowed `element_ref` values; no new owner or value
  model is introduced.
- `67a8af42`: `Element.querySelector(String)` calls the existing public
  `style::engine::select` through the same parser/adapter pattern as `closest`.
  It returns the first current descendant, excludes self, binds `:scope`,
  preserves shadow boundaries, works on detached subtrees, and uses the same
  guarded-result proof. No parser or traversal is copied from ctbrowser.

The generated C++ was inspected: borrowed node identities, ordinary Boolean
branches and direct helper calls into public DOM/Style. The new clients link
DOM/Core/Style only and check both generated text and binary symbols for
Script/AOT dependencies. The native runtime remains free of a VM or collector.

## Focused validation

Every build, test and artifact read used `/tmp/ctbrowser-devbox-build.lock`.
Explicit targets were `ctjs-opt`, `ctjs-translate`, `ctcompile-tool`,
`ctcompile-test-native-reference` and `ctcompile-test-host-contract`; later
builds selected only the targets affected by subsequent changes.

- Final exact `ctcompile_host_contract`: **1/1**, 0.49s.
- Final lit `native-dom-closest.test` and `native-dom-query.test`: **2/2**,
  39.44s. Closest: **16 native executions / 26 refusal controls**. Query:
  **16 native executions / 14 refusal controls**. Counts include both
  optimization policies, explicit/deduced printing and GCC/Clang; repeated
  gates do not count as additional newly supported cases.
- Before the additive query change, lit `native-dom.test`,
  `native-dom-session.test`, `native-dom-closest.test`: **3/3**, 251.44s.
  The existing session test includes generated-client ASan/UBSan, teardown and
  dangling foreign inputs. The new drivers themselves run ordinary clients.
- Closest checks cover missing matches, inverted/double-negated guards,
  independently guarded chains, two document/engine domains, live hover
  state, ordered writes, detachment, invalid inputs and foreign session owners.
- Query checks cover current tree order rather than allocation order, self
  exclusion, `:scope`, cross-document identity, shadow isolation, detachment,
  syntax-error effect order, engine validation and owned-session invocation.
- Refusals retain unguarded/wrong/sibling/absent-branch uses, handle transport
  and retention, derived dataset access, explicit null comparisons, stale or
  forged proof claims, insufficient budgets, coercion, arity and unsupported
  query families. The forged control verifies that its attribute was inserted.
- Five closest file hashes matched before its commit; all seven final query
  code/test hashes match the devbox inputs. Changed C++ clang-format, Python
  Black/AST and whitespace checks pass; the closest source also passed Node
  syntax checking. All existing browser fixture files remain unchanged.

`tools/format.sh --check` was run after both changes. It still reports **20**
pre-existing diagnostics in six HEAD-identical files: `ctdrive.cpp`,
`PrefixAnalysis.cpp`, `ProviderCallbacks.cpp`, `ProviderPaths.h`, `Heap.h` and
`Facts.cpp`. This is not a whole-repository formatting pass.

The first host run caught incidental admission of direct element truth tests.
That widening was removed; the existing parameter-guard normalization boundary
is preserved. The final new predicate applies to nullable selector results.

Evidence: `/tmp/ctcompile-closest-final-gate.log`,
`/tmp/ctcompile-closest-final-control.log`, `/tmp/ctcompile-query-gate.log`,
`/tmp/ctcompile-query-format.log`; generated clients remain under the devbox's
`build/ctcompile/test/CTNative/Browser/Output/` directories for each lit case.

Skipped: full CTest/compiler lit, broad native/corpus matrices, WPT/test262,
fresh vendor Bootstrap or Node/VM differential replay, Windows and whole-project
sanitizers. No browser/runtime-oracle changes, push or history rewrite.

## Next boundary

For the next browser API integration, expose `classList.contains/add/remove`
from the shared token-list core and make Shell a thin adapter. The public
`dom/token_list.hpp` currently offers parsing, validation, updates and toggle;
the other operation logic remains inside Shell's `element/child_views.cpp`.
Lift that behavior once, preserve observable adapter semantics, then bind native
calls and run affected browser regressions plus native clients.

Document-root queries and `querySelectorAll` need separate input/borrowed-vector
iteration proofs. Explicit null comparisons, derived dataset membership,
retained callbacks, Shell event/render delivery and the application driver remain.

Original B/Data+B still require helper/inherited constructor `e.set(..., this)`
publication proof: partial initialization, exceptions/reentry, enclosing lifetime,
captured outer element Map, nested DATA_KEY Map, conflict checks and removal.
Preserve `e.set`, `e.remove`, `P.off`, configuration and disposal bodies. The
previous terminal-publication and AND-congruence work is unchanged. Full native
Bootstrap initialization is unfinished.
