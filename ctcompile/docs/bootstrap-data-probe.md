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

All Data calls occur after the factory returns. Two distinct object keys
produce `42` and `21`; replacing the first key's value produces `43`; removing
it makes `get` return `null`. The interpreter reference must print exactly
these four observations. AMD also prints `traceDelayed=1`.

Each test then runs `native-claims.py`. It requires all six source functions
to remain imported, or seven for AMD, and records every native refusal with
its reason. Native coverage may increase without failing the gate. The
recorded census is compile coverage; this probe does not execute a native
Bootstrap binary or establish full Bootstrap support.

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
