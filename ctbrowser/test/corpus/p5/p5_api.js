// What test/corpus/p5/p5_api.cpp runs inside p5_api.html: the sketch the
// probes run against, and the report they leave. The probes themselves are in
// p5-api-probe/*.js and their runner in p5-api-probe.js, which this page loads
// after this file.

// The probe groups push onto this as they load, so it has to exist first.
globalThis.__probes = [];

var __out = '';

// The sketch the probes run against. Small, because several read every
// pixel back and the probe is run on every build.
function p5_api_run_probes() {
    new p5(function (s) {
        s.setup = function () {
            s.createCanvas(24, 24);
            s.noLoop();
            s.pixelDensity(1);
            // The runner is async now, because a loader probe returns a
            // promise. Its result lands in a global when it finishes, so the
            // test drives the loop until it does.
            globalThis.__runProbes(s).then(function (json) {
                __out = json;
            });
        };
        s.draw = function () {};
    });
}

// The runner's JSON report, or '' while it has not finished.
function p5_api_report() {
    return __out;
}
