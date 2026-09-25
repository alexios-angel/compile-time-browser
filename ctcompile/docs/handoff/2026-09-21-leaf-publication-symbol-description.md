# Inherited leaf publication and Symbol descriptions, 2026-09-21 UTC

Continued clean `97e96751`, using the latest handoff and master-plan journal.
The interrupted Symbol/sibling drafts were already committed; there was no
dirty native work or unmerged `codex-wip-20260907`. The startup audit inspected
73 Linux process identities and 360 Windows CIM process records, with no Claude
executable, CLI or loop found. This slice changes only ctcompile code, tests and
docs, plus the external master-plan journal.

## Landed

- `744ebe85` normalizes the proved super chain once, before constructor
  publication and own-key snapshot checks. The existing terminal registration
  proof now accepts an inherited leaf's final `Map.set(literal, this)` after
  its complete initialization. It retains the base-class exclusion, sole Map
  capture, exact construction/caller census, undefined completion and inert
  suffix requirements. Registration stays immediately after the exact `new`;
  ordinary stack records and borrowed Map pointers retain identity.
- `be96b029` admits source Symbol `.description` through the existing
  fingerprinted DOM and intrinsic entry contracts. A separate undefined/String
  proof kind calls the native `optional<js_string>` API, then constructs the
  existing owning `nullable_string` with its exact tag. Equality, truthiness,
  `typeof`, branches, loops and returns retain absence and owning snapshots.
  DOM null/String remains a separate proof. No runtime carrier was added.

The original helper, base-publication and Bootstrap B/Data+B source bodies remain
refused. The former well-known Symbol description refusal now executes unchanged.
An independent read-only review found no concrete class-normalization issue.
The ownership audit recommended the leaf boundary; two auxiliary agents hit
service limits without edits, and the root completed the class fixtures.

## Focused validation

All builds and executions ran on the devbox under
`/tmp/ctbrowser-devbox-build.lock`. Explicit `tools/remote-build.sh` targets were
`ctjs-opt`, `ctjs-translate`, `ctcompile-test-host-contract`,
`ctcompile-test-native-runtime` and `ctcompile-test-native-reference`.
The initial build passed all 80 steps; later fixture syncs needed no rebuild.

Exact CTest selection:

```sh
ctest --test-dir projects/compile-time-browser/build --output-on-failure \
  --no-tests=error -R '^(ctcompile_host_contract|ctcompile_native_runtime)$'
```

Both passed, **2/2 in 0.58 s**. Six distinct lit cases passed across the corrected
selections using the generated `build/ctcompile/test` configuration:

| Case under `CTNative/` | Result |
| --- | --- |
| `Lowering/Scalars/symbol-boundary.test` | Passed in initial Symbol selection. |
| `Exports/native-intrinsic-symbols.test` | Passed in initial Symbol selection. |
| `Browser/native-dom-symbols.test` | Final **1/1 in 60.81 s**. |
| `Lowering/Objects/class-terminal-publication.test` | Passed in **3/3, 173.44 s** regression selection. |
| `Lowering/Objects/class-instanceof.test` | Passed in that regression selection. |
| `Browser/native-dom-strings.test` | Passed in that regression selection. |

Each filter was anchored, for example:

```sh
~/.lit-venv/bin/lit -av projects/compile-time-browser/build/ctcompile/test \
  --filter='^ctcompile :: CTNative/Browser/native-dom-symbols[.]test$'
```

Measured source coverage:

- Class publication: **21 selected source observations**, nine admitted cases.
  Four new leaf cases add **32 native executions**, covering registration,
  overwrite, deletion and inherited method mutation. Concrete-record pointer
  checks exclude shared identity objects. Original partial-publication,
  replacement-return, escaping Map, inherited snapshot and Bootstrap controls
  retain their outcomes. The selected harness also runs its existing authority,
  forged-proof, work-budget, constructed-method and original `r` controls.
- Symbol exports: **72 native executions, 84 refusals and two mutations**.
  Eleven Node/VM Boolean observations include the 17-part description transcript.
  Absent, explicit undefined, empty, NUL, surrogate and well-known descriptions,
  saved copies, loop state and joins pass. Replacing undefined with null in the
  native artifact fails the intended assertion.
- DOM Symbols: **40 native executions, 42 refusals and one mutation**. The new
  entry checks five attribute effects and both undefined/String return tags,
  alongside all existing identity and transport observations. Original Node/VM
  call observations and real DOM write notes are checked separately.
- All **19 final code/test SHA-256 hashes** match the devbox. All **12 changed
  C++ files and five Python files** pass scoped formatting; Python syntax and
  whitespace checks pass.

Two fixture corrections were required. Initial Symbol lit was **2/3 in 58.49 s**:
the DOM oracle produced all true values, but globals print alphabetically and the
new expected lines were appended instead. The next DOM run failed **0/1 in
47.86 s** because its shared Boolean checker rejected the existing undefined
literal carrier. `native_dom.py` now permits only exact undefined constant
declarations with explicit opt-in; default restrictions and all other scalar
carrier exclusions remain. Neither correction changed a source program or runtime.

Required `tools/format.sh --check` on frozen code retains **16 existing
diagnostics** in untouched `ctbrowser/tools/ctdrive/ctdrive.cpp`,
`HostContract/ProviderPaths.h`, `PartialEvaluation/Heap.h` and `Symbolic/Facts.cpp`.
It stops before repository-wide Python/web formatting. An earlier check ran
during the Symbol draft edits and is superseded by this frozen result.

Transient evidence: `/tmp/ctcompile-leaf-symbol-gate.log`, `-gate2.log`,
`-gate3.log`, `-gate4.log`, `-format2.log` and `.sha256`.
Skipped: full CTest/compiler lit, broad corpus/native matrices, full Bootstrap,
WPT/test262, Windows, new sanitizer runs and push. This is a focused pass.

## Exact next boundaries

Bootstrap still needs original constructor registration through helpers and
base constructors before leaf completion. Start with the unchanged
`class-map-record-constructor-helper.js` in class fixture file 30: its captured
Map remains outside the direct completed-owner census. Merely moving the helper
call does not prove the Map's observers or lifetime. The original file-30
inherited publication case has a field write after registration and remains
refused. Prove partial initialization, exceptions and reentry before admitting it.

Original B/Data+B additionally needs captured outer element and nested DATA_KEY
Map origins, conditional conflict checks, nullable gets, deletion/empty cleanup
and enclosing-owner lifetime. Preserve `e.set`, `e.remove`, `P.off`, configuration
and disposal bodies. No full-Bootstrap admission or coverage gain is claimed.

For Symbols, next prove useful intrinsic entry parameters and helper calls.
Description-to-String narrowing, mixed null/description joins, registry access,
symbol-keyed fields and custom hooks remain separate. The historical VM custom
`Symbol.hasInstance` gap was not remeasured. Deeper sibling relays, String
ordering, collections/document views and the application driver remain unfinished.
