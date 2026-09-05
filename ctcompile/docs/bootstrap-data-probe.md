# Bootstrap Data publication probe

`tools/check/bootstrap-data-probe.py` extracts the UMD wrapper and Data
declaration directly from `ctbrowser/vendor/bootstrap/bootstrap.bundle.js`.
The vendor fragment remains verbatim through Data's closing brace. The
remaining declaration and factory body become `; return e;`, exposing Data.
Unique markers check the wrapper's publication branches, factory, three Data
methods and following `transitionend` declaration. An unfamiliar bundle fails
with a source-structure diagnostic instead of silently extracting a different
fragment. The Bootstrap license header stays with every generated source.

The three CTests `ctcompile_bootstrap_data_commonjs`,
`ctcompile_bootstrap_data_browser` and `ctcompile_bootstrap_data_amd` establish
the host bindings outside the unchanged wrapper. CommonJS reads
`module.exports`; the browser reads `globalThis.bootstrap`. AMD stores the
factory and invokes it after the UMD call has returned, first checking that
the retained value is a function.

All Data calls occur after the factory returns. The interpreter reference must
print exactly 19 numeric observations, or 20 for AMD:

| Branch | Observations |
|---|---|
| Element never inserted | `traceAbsentGet=1` for `null`; `traceAbsentRemove=1` for `undefined` |
| Two distinct object keys | `traceGet=42`, `traceOther=21` |
| Replace the same component key | `traceReplacement=43` |
| Read/remove the wrong component key | `traceWrongKeyGet=1` for `null`, `traceWrongKeyRemove=1` for `undefined`, `traceAfterWrongKeyRemove=43` |
| Reject a second component on one element | `traceErrorCount=1`, `traceErrorMessage=1`, `traceRejectedKey=1` for `null`, `traceAfterRejectedSet=43` |
| Delete one element and remove it again | `traceRemoved=1` for `null`, `traceOtherAfterRemove=21`, `traceRemovedAgain=1` for `undefined` |
| Reinsert after deleting the outer Map entry | `traceReinserted=64`, `traceReinsertedIdentity=1`, `traceOldKeyAfterReinsert=1` for `null`, `traceOtherAfterReinsert=21` |
| Delay AMD invocation | `traceDelayed=1` |

The reference has no console, so the harness supplies a `console.error`
recorder. The real Data method chooses the rejection branch and builds the
message; the recorder checks that exactly one error names the existing
`bs.alert` instance. Removing that element then allows the previously rejected
`bs.collapse` key. Its new value is an object with a `value` field, and reads
must preserve both that field and the original object's identity.

Each test then runs `native-claims.py`. It requires all seven source functions
to remain imported, or eight for AMD, and records every native refusal with
its reason. These totals include the script entry, UMD wrapper, factory, three
Data methods and console recorder; AMD adds its registration function. The
recorder accounts for the increase from the previous 6/6/7 totals. Native
coverage may increase without failing the gate. The recorded census is compile
coverage; this probe does not execute a native Bootstrap binary or establish
full Bootstrap support.

`bootstrap-data-binding-time.py` reuses the same browser source to inspect
[Binding-Time Analysis](native-binding-time.md). It requires dynamic host reads,
unproved calls and publication, complete operation facts, repeatable analysis
and rejection of forged annotations. The Map initializer and three latent
methods remain intact under partial evaluation; the imported public functions
are outside its closed private candidate set. The companion negative control
changes the real console-read classification and must fail with its exact
diagnostic. These two tests add analysis evidence to the 19 browser observations.

The expanded probes measured **0/7** native functions for CommonJS, **0/7** for
the browser and **0/8** for AMD on the devbox. Every refusal has a reason and
no source function is skipped. All 19/19/20 interpreter observations and both
negative controls pass. The 1,101-byte vendor fragment and its SHA-256 hash
remain unchanged from the original four-observation probe.

Two negative controls change the actual executed replacement observation to
`44` and remove the vendor extraction boundary. They pass only on the exact
expected diagnostic, so a missing tool or interpreter error cannot substitute
for a working check.

Generated JavaScript, source and fragment SHA-256 hashes, interpreter output,
diagnostics, native census JSON and combined reports live under
`build/ctcompile/test/bootstrap-data/`. Run the checks on the devbox:

```sh
ctest --test-dir build -R '^ctcompile_bootstrap_data_' --output-on-failure
```

To inspect generated source without building or running it:

```sh
python3 tools/check/bootstrap-data-probe.py \
  --bootstrap ctbrowser/vendor/bootstrap/bootstrap.bundle.js \
  --mode amd --work /tmp/bootstrap-data --generate-only
```
