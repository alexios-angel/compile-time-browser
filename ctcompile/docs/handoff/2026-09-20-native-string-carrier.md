# String value carrier, 2026-09-20 UTC

Continued clean **2bc6cc6d**, following the Number handoff's next String boundary.
No interrupted dirty work or September 7 WIP branch remained. Parallel agents
handled the runtime class, compiler support, tests and semantic audit; root
completed integration after their rate limits.

- **8cc0cf6e** introduces `js_basic_string<char>` / `js_string`, explicit owning
  construction/extraction, truthiness, same-type equality/concatenation and the
  proved ASCII-prefix `startsWith`. The old raw concatenation helper forwards to
  the class's implementation using public Core `join_surrogates`. Const analysis
  recognizes exact read-only methods; typed String exceptions retain ownership.
- **ac3cdaa2** migrates String literals, calls/returns, fields, captures, closures,
  equality and concatenation to the class. Generated prefix calls use
  `.startsWith(...)`. Public browser/Unicode operations, Map/vector/nullable/
  Boolean-String-union/JSON storage and print helpers keep explicit raw text
  adapters. Filter/replacement callbacks receive and return the typed values.

No browser or VM code, source admission rule, ownership proof or accepted type
set changed. Missing Map values still retain absence tags; saved String results
own their bytes across mutation and owner destruction. Construction preserves
bytes rather than globally normalizing them. Both implicit storage conversion
and unrelated primitive construction remain disabled.

## Measured focused checks

All builds and remote checks held `/tmp/ctbrowser-devbox-build.lock`; no local
builds or full suites ran. Explicit targets:

```sh
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-runtime
tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-native-runtime ctcompile-native-pipeline-strings ctcompile-native-pipeline-string_snapshots ctcompile-native-pipeline-optional_scalars ctcompile-native-pipeline-scalar_unions
tools/remote-build.sh ctjs-opt ctjs-translate
```

The foundation build completed 21 steps. The carrier build initially failed
because a field adapter's conditional combined two different MLIR subclasses;
converting the branch to `mlir::Type` fixed it, and the next build completed
16 remaining steps. A later three-step build fixed JSON object replacement:
an already-correct lvalue must not pass through scalar extraction. Test-only
syncs needed no Ninja work; the final formatting repair rebuilt the runtime
target in two steps.

Runtime CTest passed **1/1**, 0.02s, both before and after carrier migration:

```sh
ctest --test-dir build --output-on-failure --no-tests=error -R '^ctcompile_native_runtime$'
```

**17 distinct selected lit cases passed** across these runs. Commands used
`~/.lit-venv/bin/lit -v -j N build/ctcompile/test --filter=REGEX`, with exact paths
anchored to `^ctcompile :: (...)$`. Paths below are relative to `ctcompile/test`.

| Run | Exact cases | Result |
|---|---|---|
| Foundation, `-j 2` | `Target/Cpp/native-string.mlir` | 1/1, 2.22s |
| Carrier, `-j 3` | `CTNative/Lowering/Scalars/{strings,string-control-flow}.mlir`; `Lowering/Maps/string-snapshots.mlir`; `Lowering/Closures/{returned-closures,closure-refusals}.mlir`; `Lowering/Objects/object-string-fields.mlir`; `Lowering/Admission/parameter-carrier.mlir`; `Lowering/Emission/readable-strings.test`; `Fixtures/Scalars/{string,optional-scalars,scalar-unions}.test`; `Fixtures/Maps/string-snapshots.test`; `Browser/native-dom-strings.test` (all abbreviated paths also under `CTNative/`) | 12 passed / 1 failed, 163.20s |
| Refusal and browser adapters, `-j 3` | `CTNative/Lowering/Scalars/strings.mlir`; `CTNative/Browser/native-dom-{json,dataset}.test` | 1 passed / 2 failed, 1.65s |
| JSON reference and dataset pin fixes, `-j 2` | `CTNative/Browser/native-dom-{json,dataset}.test`; `CTNative/Lowering/Closures/owning-callables.mlir` | 3/3, 97.80s |

The first failure was prior precomputation folding a String coercion witness
before its expected runtime refusal. `optimize=false` restores the intended
checks without changing JavaScript. The later failures were the JSON lvalue
adapter above and the dataset check's old `.starts_with` spelling. The relevant
corrected cases passed; passed selections were not broadly replayed.

Six selected existing class cases passed through `class_dom.main` with only
`CLASS_CASES` restricted and unrelated `CASES`, `FIELD_REFUSALS` and
`CLASS_REFUSALS` empty: `class_utf16_empty`, `class_utf16_nul`, `class_utf16_pair`,
`class_utf16_lowercase`, `class_filter_captured`, `class_f_dataset_matches`.
They ran **24 Node/interpreter observations, eight combined native executions
and 118 refusal checks**, including the original identity, budget and mutation
controls. The recorded VM differences for UTF-16/casing remain explicit.

The temporary driver uses `PYTHONPATH=ctcompile/test`, then:

```sh
python3 /tmp/ctcompile-string-class.py --translate build/ctcompile/tools/ctjs-translate/ctjs-translate --opt build/ctcompile/tools/ctjs-opt/ctjs-opt --node /home/ubuntu/tools/node-v26.8.1/bin/node --reference build/ctcompile/test/ctcompile-test-native-reference --clang tools/clang-std-embed/bin/clang++ --build build --include ctbrowser/include --work build/ctcompile/test/string-carrier-class
```

Ownership checks reuse `driver_execution.check_positive` and
`driver_nested_maps.check_nested_maps`, retaining each selected case's existing
Node/interpreter, native emission, GCC/Clang, lifetime, sanitizer and mutation
checks. Completed cases: `result_seeded_string_saved`,
`result_seeded_mixed_string_saved`, `nullable_key_string_saved`,
`constant_string_branch_lifetime`, `shared_parameter`, `field_string_lifetime`,
`local_field_readback_lifetime`, and `leaf_object_lifetime`: **eight admitted
cases passed** across corrected runs. The extra `nested_map_dynamic_nullable`
control remains refused: **zero native programs, one refusal, two typed source
observations and six distinguishing mutations**. It adds no native admission.
Initial failures found stale callable pins and raw String arguments in C++
observers, including four leaf observer arguments fixed in two reruns; their
JavaScript bodies were preserved. Only the outstanding cases reran.
**98786531** commits these ownership clients and the formatting repair below.
All **43** final changed code/test SHA-256 hashes matched the devbox.

The temporary ownership drivers select the named rows above, use the same
arguments below, and call existing complete per-case checks. The remaining-row
driver is `/tmp/ctcompile-string-ownership-remaining.py`:

```sh
PYTHONPATH=ctcompile/test python3 /tmp/ctcompile-string-ownership.py --translate build/ctcompile/tools/ctjs-translate/ctjs-translate --opt build/ctcompile/tools/ctjs-opt/ctjs-opt --node /home/ubuntu/tools/node-v26.8.1/bin/node --reference build/ctcompile/test/ctcompile-test-native-reference --work build/ctcompile/test/string-carrier-ownership
```

Scoped formatting covers **17 C++ and 16 Python files**. All changed Python
files parse, and 11 JavaScript observer functions have identical before/after
ASTs. The required formatter ends with the same **16 pre-existing diagnostics**
in untouched `ctdrive.cpp`, `HostContract/ProviderPaths.h`,
`PartialEvaluation/Heap.h` and `Symbolic/Facts.cpp`. An intermediate formatter
run reported 18: Homebrew clang-format had collapsed an existing Number `if`
differently from the repository's pinned formatter. Reapplying the pinned tool
restored the original formatting; the journal corrects the premature count.

Full CTest/compiler lit, broad corpus/matrix, WPT/test262 and wtfjs replay were
skipped. Native String/browser checks use public platform APIs; the class cases
retain their explicitly recorded oracle differences. No full-suite pass or new
wtfjs specimen coverage is claimed. No push.

Process checks inspected 67 Linux and 359 Windows processes with no Claude
identity matches, but 57 Linux executable links were unreadable. Availability
was treated as uncertain; edits stayed in ctcompile and the requested plans.

## Exact next boundary

Implement proved primitive String numeric conversion and mixed String/Number
addition, beginning with runtime `baNaNa` and negative controls. Existing generic
String numeric/coercive operations remain refused when not precomputed.
General String length/index/casing/comparison alignment is separate: current
native/VM length uses bytes (ND-1), while the admitted DOM charAt/slice/isolated
lowercase paths already use public Core UTF-16 operations. The String class
therefore does not yet expose general length/index/casing methods.

Raw Number alias retirement, collections, document views and boxed primitives
remain. Add the planned `Symbol.hasInstance` wrapper with constructor/prototype
proofs; use `std::holds_alternative` only for equivalent default cases. Before
indexed Bootstrap `R.find`, resolve Shell's NodeList slots above 1,000,000
returning undefined, retaining the independent 2^24 spread cap. Full Bootstrap
and the application driver remain unfinished.
