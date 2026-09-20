# Native class-list integration, 2026-09-20 UTC

Resumed the browser-query handoff at `3383f4ef`; its next unimplemented boundary
was `classList.contains/add/remove`. No predecessor edits or unmerged rescue
branch remained. The user confirmed Claude stopped; Linux `/proc` and Windows
`Get-CimInstance Win32_Process` checks independently confirmed this. The final
pre-landing check inspected 66 Linux and 364 Windows processes, with no unknown
Linux processes or matching Claude executable, Node CLI entry, or loop.

## Landed

- `6ea177bc` extracts membership and mutation into `dom/token_list.hpp` and
  `lib/DOM/token_list.cpp`. The element child-view binding now converts VM
  values, reports JavaScript errors and Shell notifications, and calls the core.
  Membership keeps the old snapshot-versus-coercion evaluation boundary.
  Mutations validate every converted token before reading or writing the DOM.
  Zero-argument normalization, same-value writes and absent attributes retain
  their existing behavior. Core and actual Shell regressions cover these cases.
- `7743e77a` proves and emits class-list membership and variadic mutations.
  Tokens must be definite Strings; detached methods, coercions, retained token
  lists and unguarded selector results refuse. Mutations invalidate dataset
  presence proofs; membership does not. The wrappers assemble ordinary C++
  arguments and call the public DOM implementation, checking both validation
  and DOM-write errors. No platform algorithm was copied into ctcompile.
  Element/class-list name-to-enum maps replace chained conditional associations,
  as requested; lookup key lengths are bounded before hashing.

The native driver covers HTML/SVG, saved aliases, invalid arguments after
preceding source effects, zero-argument calls, guarded query/closest receivers,
entry handle validation and void results. Both DOM providers, optimization
policies, explicit/deduced printing and GCC/Clang pass: **32 executable runs**,
**30 refusal controls** and **two read-only dataset lowerings**. Sixteen generated
C++ files were produced. Linked binaries contain no Script/AOT symbols.

## Focused validation

All builds and test runs used the devbox under
`/tmp/ctbrowser-devbox-build.lock`. The first build selected:

```sh
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-host-contract \
  ctbrowser-test-dom_token_list ctbrowser-test-element_attrs ctbrowser-test-dom_mutation
ctest --test-dir build --output-on-failure \
  -R '^(dom_token_list|element_attrs|dom_mutation|ctcompile_host_contract)$'
~/.lit-venv/bin/lit -v \
  build/ctcompile/test/CTNative/Browser/native-dom-class-list.test \
  build/ctcompile/test/CTNative/Browser/native-dom.test \
  build/ctcompile/test/CTNative/Browser/native-dom-closest.test \
  build/ctcompile/test/CTNative/Browser/native-dom-query.test
```

CTest passed **4/4, 0.62s**. Existing DOM/closest/query lit cases passed. The
class-list case completed its first 16 clients, then failed at a new control's
`keys.length > 0`, which is outside the current DOM comparison contract. The
control now returns the supported length directly. The first lit selection
therefore finished **3/4, 260.08s**, not a full pass.

After correcting that fixture and adding the reviewed key-length bounds:

```sh
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-host-contract
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_host_contract$'
~/.lit-venv/bin/lit -v build/ctcompile/test/CTNative/Browser/native-dom-class-list.test
```

The final host CTest passed **1/1, 0.49s**; final class-list lit passed
**1/1, 57.07s**. All **12** changed code/test file hashes match the devbox copies.
Both dataset membership controls were also inspected for complete native
emission, and generated C++ was inspected for ordinary typed browser calls.

Local `tools/format.sh --check` ran and still reports 20 existing diagnostics in:

- `ctbrowser/tools/ctdrive/ctdrive.cpp`
- `ctcompile/lib/CTNative/HostContract/PrefixAnalysis.cpp`
- `ctcompile/lib/CTNative/HostContract/ProviderCallbacks.cpp`
- `ctcompile/lib/CTNative/HostContract/ProviderPaths.h`
- `ctcompile/lib/CTNative/PartialEvaluation/Heap.h`
- `ctcompile/lib/CTNative/Symbolic/Facts.cpp`

Scoped clang-format, Black, Python syntax and `git diff --check` pass. Logs are
`/tmp/ctcompile-class-list-gate.log`, `/tmp/ctcompile-class-list-final-gate.log`
and `/tmp/ctcompile-class-list-final-format.log`. Full CTest/compiler lit,
WPT/test262, broad corpus/matrix replays and sanitizers were not run. No full
Bootstrap or whole-plan completion is claimed; historical counts are unchanged.

## Next boundary

Prove `Element.querySelectorAll(String)` as a local snapshot of node handles
borrowed from one document, then preserve its iterator and lifetime semantics
through native lowering. The public selector implementation already exists:
`style::engine::select(txn, root, selectors, false, scope)`. It returns
`std::vector<node_id>` in tree order; reuse it instead of implementing traversal.
Document-root queries require explicit document receiver/ownership evidence.

Original Bootstrap B/Data+B constructor publication through helper calls and
inheritance, nested Map lifetimes, retained callbacks, events, Shell/rendering
and the application driver remain unfinished. Their earlier measurements were
not replayed this session. No push was performed.
