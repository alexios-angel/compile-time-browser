# Native quality, Boost and timing review — 2026-09-15

Reviewed the resumed URI changes and the nullable guard using the requested
thermo-nuclear quality, sharp-edges, modern C++ and modern Python guidance.
The Ponytail audit scanned owned browser/compiler/tool sources, excluding vendor,
generated data and historical documents. Three agents supplied independent
design/review/test drafts; root integrated and checked them after service limits.
Repository C++23 and Black conventions remain authoritative; no tooling migration
or dependency was added. Gate details and continuation state are in [HANDOFF](HANDOFF.md).

## Findings and disposition

1. **Fixed — mixed responsibilities crossed 1,000 lines.** URI work had grown
   `HostContract/Contract.cpp` from 972 to 1,066 lines. **b13382ec** moves the
   complete DOM proof and queries to `HostContract/DOMEntry.cpp`, retaining the
   existing interface. Contract now has 437 lines; DOMEntry has 742 after the
   nullable extension. The proof remains cohesive and no forwarding layer was added.
2. **Fixed — avoidable whole-module clone.** Every fingerprint cloned the IR
   just to remove advisory reports. **9517ff21** prints/hashes the immutable
   original when no report exists and preserves clone/clear behavior otherwise.
   It retains every complete reproof and source-freshness barrier. Regression
   coverage compares the old hash, nested reports, source mutation, nonmutation,
   lookalike attributes and source-location exclusion.
3. **Addressed in the new guard — optional presence must not become global String
   authority.** **bb7ba402** records exact SSA operands inside the selected arm,
   restores local facts on scope exit, and publishes evidence only after complete
   proof. The producer stays optional; an empty String is present, a missing
   attribute has `typeof "object"`, and another read receives no authority.
   Negative source tests and all incomplete proof budgets enforce that boundary.
4. **Open — proof budgets exclude fingerprint cost.** `DOMEntryAnalysis` computes
   the module fingerprint before its first charged step, including at a zero
   budget. `maxSteps` bounds structural proof work, not total hashing/allocation
   work. Follow the charged whole-module census used by `normalizeDOMURI` if a
   total-work limit is required. Caching stale proof results is not a valid fix.

No additional functional blocker was found in the reviewed URI path. Exact
pre-call state, payload non-observation, source/effect coverage and private
publication remain required. Native decoding calls the existing public Core
implementation. Foreign C++ allocation failures do not become JavaScript catches.
The generated code has no Script/context/value dependency.

## Measured transcompilation work

Devbox, saved pre-change `ctjs-opt` versus **bb7ba402**, unchanged complete
Bootstrap IR, one warm-up and 11 alternating pairs. Command timing includes
process startup, parsing, the pass and output to `/dev/null`; pass timing uses
MLIR instrumentation in a separate paired run. These are local warm medians,
not a whole-application compilation benchmark or a statistical confidence claim.

| Measurement | Baseline median | Candidate median | Reduction |
| --- | ---: | ---: | ---: |
| Bootstrap fingerprint command | 159.20 ms | 152.11 ms | 4.45% |
| Bootstrap host fingerprint pass | 60.4 ms | 51.0 ms | 15.56% |
| Small URI native-lowering command | 8.263 ms | 8.579 ms | -3.82% |

All compared output bytes and fingerprints match. The small URI command has no
measured gain. Both normal and report-decorated Bootstrap CLI inputs were checked;
the pass clears reports before fingerprinting, so both exercise the fast path.
The clone fallback is covered by the direct C++ regression, not that timing.
Raw samples and replay scripts are in `/tmp/ctcompile-nullable-uri/`:
`benchmark.py`, `timing.json`, `pass-timing.py` and `pass-timing.json`.

## Boost assessment

Boost.Scope can replace the three local cleanup structs listed below. The existing
dependency already supplies it; preserve each guard's creation point and use a
`noexcept` cleanup lambda. These are browser-owned follow-up candidates, not changes
landed in this session. ctcompile already uses LLVM's scope-exit helper, so replacing
that with Boost would add churn without removing machinery.

Keep Core URI decoding: Boost.URL percent decoding does not supply ECMAScript's
escaped UTF-8 checks, literal-byte preservation or `decodeURI` reserved-escape case
preservation. Likewise Boost.JSON, Regex and string trimming do not preserve the
concrete JavaScript witnesses already recorded in `StdLibMap.def`. Boost.Endian
would save about one line in program-image reads; defer that to related work.

## Ranked complexity cuts — proposals only

- shrink: Merge duplicate Number/String `map_values` bodies into one `<K, V>` pointer helper and one shared-pointer forwarder; retain owning snapshots and update the pinned helper spelling. About -10 lines. [NativeMapHelpers.h](../lib/CTNative/Lowering/EmitC/NativeMapHelpers.h#L253).
- shrink: Replace three one-use cleanup structs with existing `boost::scope::scope_exit` and `noexcept` lambdas, preserving scope and unwind behavior. About -7 lines including includes. [classes.cpp](../../ctbrowser/lib/Script/compile/classes.cpp#L135), [frames.cpp](../../ctbrowser/lib/Script/compile/frames.cpp#L382), [functions.cpp](../../ctbrowser/lib/Script/compile/early_errors/functions.cpp#L195).
- stdlib: Replace the JSON control-byte `snprintf` buffer with existing `std::format("\\u{:04x}", c)`; keep the byte-preserving escape loop and remove unused includes. About -5 lines. [Manifest.cpp](../lib/Support/Manifest.cpp#L41).

net: -22 lines, -0 deps possible.
