// What of p5.js actually works.
//
// The corpus pages prove specific things render. This asks a much broader and
// duller question - does each function DO something rather than throw - across
// as much of p5's 417-function surface as can be called headlessly.
//
// It exists because the colour-mode bug taught the lesson twice: `colorMode(HSB)`
// was broken from the first day p5 ran here, silently, and no test noticed
// because no test called it. A wide, shallow probe found five real bugs in one
// run after an afternoon of reading found one.
//
// THE SHAPE OF A PROBE. `[module, name, body]`. The body is handed the sketch
// and returns whatever it wants; a THROW is recorded as a failure and does not
// stop the rest. Returning the string 'SKIP' records the probe as not
// applicable - used where a function needs a display, a network or a file.
//
// A probe should ASSERT, not merely call. `s.rect(0, 0, 10, 10)` passes in an
// engine that draws nothing; reading a pixel back does not. Where checking the
// result is cheap the probe checks it and throws its own message, because a
// function that runs and returns garbage is the failure mode this whole file
// exists to catch.
//
// tools/corpus/ratchet.py p5 api lists which `fn.*` in the bundle no probe here mentions -
// that list is the work queue, and it is why probe names match p5's own.
//
// A CORRECTION worth keeping, because it is the failure mode of a harness. An
// earlier commit message here claimed five failures were contamination from a
// throwing probe unwinding p5's state stack. The contamination was real and is
// fixed (see the try/finally in the runner), but those five were NOT it: they
// were real failures that a mis-applied edit had briefly deleted the probes
// for. Checked one at a time afterwards, every one reproduced on its own.
// Never conclude a failure was noise without reproducing it in isolation.

// Probe groups are loaded first by p5_api.cpp.

// The runner. Each probe gets the same sketch and a clean-ish canvas; a throw
// is recorded against its name and the next probe still runs, because the
// point is a LIST of what is broken rather than the first thing that is.
// ASYNC, because some probes are. A probe may return a promise - a loader
// does - and the runner awaits it, so a load that fails reports as that probe
// failing rather than as an unhandled rejection with no name attached.
globalThis.__runProbes = async function (sketch) {
    const passed = [];
    const failed = [];
    const skipped = [];
    for (const entry of globalThis.__probes) {
        const name = entry[0] + '/' + entry[1];
        // A fresh state for each, so one probe's fill or transform cannot make the
        // next one pass or fail.
        //
        // try/FINALLY, so the pop happens exactly once however the body leaves. An
        // earlier version popped in the catch as well, which on a throwing probe
        // unwound p5's state stack one level too far - and the NEXT five probes
        // then failed for reasons that had nothing to do with them. A harness whose
        // failures contaminate each other reports a work queue that is partly
        // fiction.
        sketch.push();
        try {
            sketch.resetMatrix();
            const out = await entry[2](sketch);
            if (out === 'SKIP') {
                skipped.push(name);
            } else {
                passed.push(name);
            }
        } catch (e) {
            failed.push(name + ': ' + (e && e.message ? e.message : String(e)));
        } finally {
            sketch.pop();
        }
    }
    passed.sort();
    failed.sort();
    skipped.sort();
    // `count` is a STRING so the harness's small JSON reader, which only pulls
    // string arrays, can see it. It exists so the C++ side can prove no probe
    // fell out of the report rather than trusting that none did.
    return JSON.stringify({
        passed: passed,
        failed: failed,
        skipped: skipped,
        count: [String(globalThis.__probes.length)]
    });
};
