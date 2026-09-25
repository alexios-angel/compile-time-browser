# Guarded Bootstrap document defaults, 2026-09-21

Continued `8b6ff9d7`. The global-document request was already committed.
The iteration 38 journal and saved diffs identified the interrupted typed-selector
and original Bootstrap default-root thread; this session finished both.
Parallel agents migrated nullable selectors, wrote the complete-source fixture
and reviewed root-guard proof controls. Root completed the callable-holder proof
after the review agent was rate-limited, then reconciled and gated the changes.

`4500fada` admits the unchanged vendor-pinned `R.find` body from
`native_dom_spread_length.py` with an omitted or explicit-`undefined` receiver
under a source `document.documentElement` guard. Expansion supplies missing helper
arguments as exact undefined values; excess arguments remain refused. The existing
initial binding and complete entry reproof authorize selecting the original
default arm. No replacement helper is substituted for Bootstrap's code.

A guard of the bound document root proves repeated root reads present only in
that arm. Branch exit restores the previous fact; an unrelated query guard gives
no authority. The admitted effects cannot replace the root or reenter script.
The shared callable-holder analysis permits reads inside nested `if` arms after
unconditional initialization. Conditional/late/duplicate writes and wrong
receivers remain refused, as do loops and other unproved region boundaries.
The existing confined spread/concat proof still permits only length observations.

`e6279f23` separately emits admitted element `closest` and `querySelector` through
the existing `js_element_t` overloads and `std::optional<js_element_t>`, using
`element_or_null` to bridge to established nullable carriers. Guarded result
chains retain their live Style association. This changes neither source admission
nor browser behavior. Generated code reuses the scoped global document and public
ctbrowser DOM/Style; it has no Script symbols, GC handles or VM context.

## Focused validation

- Devbox builds used explicit `ctjs-opt`, `ctjs-translate` and
  `ctcompile-test-host-contract` targets under the shared build lock.
- The first exact host-contract run failed an old omitted-argument refusal.
  That exact source is now a positive expansion control, alongside a mixed-call
  check that prevents omitted arguments from specializing sibling calls.
  Excess arity still refuses. Initial original-helper probes also exposed the
  callable-holder same-block restriction; the shared proof was extended without
  simplifying the source. The final rebuild and checks below passed.
- Exact `ctcompile_host_contract`: **1/1, 0.56 s; 0.57 s total**. Controls cover
  both document anchors/providers, repeated root reads, wrong/absent/negated
  guards, post-branch leakage, mutated IR and forged reports, exact work budgets,
  omitted arguments and callable initialization order.
- Selected `CTNative/Browser/native-dom-document-default.test`: **1/1, 86.49 s**,
  **32 native executions and 92 refusals**. Two complete sources keep the original
  helper: omitted receiver under a positive guard, and explicit undefined after
  a negated early return. Borrowed/owned providers, GCC/Clang, explicit/deduced
  printing and both optimization policies cover empty/removed/replaced roots,
  matching-root exclusion, detached anchors, shadow/outside exclusion, live hover,
  invalid selector effect order, wrong inputs/Style/session and outer-document
  restoration on success and exceptions.
- Existing `native-dom-spread-length.test`: **1/1, 84.42 s**, **32 native
  executions and 52 refusals**, retaining explicit-receiver original-helper cases.
- Selected `native-dom-query.test` and `native-dom-prototype-query.test`:
  **2/2, 168.70 s**, respectively **16 native executions/14 refusals** and
  **64/100**. Existing source and refusal bodies remain intact; emitted nullable
  results use typed views and preserve live Style through guarded chains.
- All 13 changed code/test SHA-256 hashes match the devbox. Scoped pinned
  formatting of ten C++ files, Black/AST checks of both Python fixtures and
  `git diff --check` pass. Required `tools/format.sh --check` ran twice and
  retained 16 untouched diagnostics in `ctdrive.cpp`, `ProviderPaths.h`, `Heap.h`
  and `Facts.cpp`; it did not pass repository-wide.
- Initial availability check inspected 71 Linux cmdline/comm identities and
  342 Windows CIM Name/ExecutablePath/CommandLine records, finding no Claude
  executable, Node CLI or loop and no errors. No browser/shared implementation
  files changed.
- Full CTest/compiler lit, broad corpus/matrix, WPT, test262, Windows, new
  sanitizers, local native builds and push were skipped. These are focused
  public DOM/Core/Style checks, with no new Node/VM differential measurement,
  asynchronous document support or full-Bootstrap admission claim.

## Next boundary

The original `R.find` result still reaches the diagnostic
`DOM spread/concat result permits only length observations` for element-member
consumers. Extend that confined result proof using the existing element snapshot
bounds, ownership and live Style association; preserve Bootstrap's complete source
and refuse unproved escapes. The new fixture retains a snapshot-escape refusal.
An unguarded omitted default still needs a proved or checked root precondition;
the global document accessor alone cannot establish that a root exists.

Remaining typed element snapshot emission, application initialization/driver,
overlapping DOM Map keys, original B/Data+B, object-valued class fields, tagged
Map snapshots, broader Strings, conditional callees and mutable cells remain
unfinished. Full Bootstrap is not admitted.
