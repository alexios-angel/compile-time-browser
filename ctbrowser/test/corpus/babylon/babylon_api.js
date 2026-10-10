// THE HARNESS FOR babylon-api-probe.js: run every probe and return the JSON
// report, which test/corpus/babylon/babylon_api.cpp parses by hand.
//
// THE RUNNER TAKES THE CANVAS, not a context: Babylon makes its own engine
// and its own context, which is the whole point of running the probes
// through the corpus rather than against the API directly.
function babylon_api_report() {
    return globalThis.__runProbes(document.getElementById('c'));
}
