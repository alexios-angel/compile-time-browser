// What of Phaser 4 actually works.
//
// test/corpus/phaser/phaser_ratchet.cpp asks how FAR the bundle gets - it reaches 10/10, so
// a scene boots, runs and paints. This asks the much broader and duller
// question: does each part of the surface DO something, rather than throw or
// answer garbage.
//
// IT EXISTS BECAUSE ONE NUMBER HIDES A LOT. `getProgramParameter` answered 0 to
// ACTIVE_UNIFORMS for as long as WebGL existed here and every corpus page still
// rendered, because hand-written pages ask for uniforms by name and only a
// library enumerates. The p5 probe found five real bugs in one run after an
// afternoon of reading found one. This is that instrument pointed at the second
// corpus.
//
// THE SHAPE OF A PROBE. `[module, name, body]`. The body is handed the live
// scene and returns whatever it wants; a THROW is recorded as a failure and does
// not stop the rest. Returning the string 'SKIP' records the probe as not
// applicable - used where something needs a display, a network or a real input
// device.
//
// A PROBE SHOULD ASSERT, NOT MERELY CALL. `scene.add.rectangle(...)` passes in
// an engine that draws nothing; checking the object's width back does not. A
// function that runs and returns garbage is the failure mode this whole file
// exists to catch, and it is not hypothetical - `+"2"` returning "2" was found
// exactly this way.
//
// EACH PROBE CLEANS UP AFTER ITSELF. Unlike p5's sketch there is no push/pop to
// wrap this in: a Phaser scene accumulates game objects on its display list, so
// a probe that adds one destroys it. A probe that leaves objects behind changes
// what the NEXT probe sees, and a harness whose failures contaminate each other
// reports a work queue that is partly fiction.

// Probe groups are loaded first by phaser_api.cpp.

// A PROBE MAY NOT HANG THE REPORT. One that awaits something which never
// settles used to leave `__out` empty forever, and the harness could then say
// only "the probes did not run" about all seventy-six of them - which is the
// same failure mode as the bracket that once ate five of p5's, arrived at from
// the other direction. A stalled probe is a FINDING and belongs in the failed
// list with its own name on it.
//
// Hand-rolled rather than `Promise.race`, which this engine does not have -
// itself worth knowing, and recorded as a probe below.
const __withTimeout = function (work, ms) {
    return new Promise(function (resolve, reject) {
        var settled = false;
        setTimeout(function () {
            if (!settled) {
                settled = true;
                reject(new Error('timed out after ' + ms + 'ms'));
            }
        }, ms);
        Promise.resolve(work).then(
            function (v) {
                if (!settled) {
                    settled = true;
                    resolve(v);
                }
            },
            function (e) {
                if (!settled) {
                    settled = true;
                    reject(e);
                }
            });
    });
};

globalThis.__runProbes = async function (scene) {
    const passed = [];
    const failed = [];
    const skipped = [];
    for (const entry of globalThis.__probes) {
        const name = entry[0] + '/' + entry[1];
        // NO push/pop TO WRAP THIS IN, unlike p5's runner - a Phaser scene has no
        // state stack. Each probe cleans up its own game objects instead, and the
        // display-list length is checked either side so a probe that forgets is
        // reported rather than left to contaminate the next one.
        // WHERE IT STALLED, if it does. A probe that awaits something that never
        // settles leaves the whole report empty, and "the probes did not run" says
        // nothing about which one. This global is the only thing that does.
        globalThis.__at = name;
        const before = scene.children.length;
        let verdict = 'pass';
        let why = '';
        try {
            const out = await __withTimeout(entry[2](scene), 1500);
            if (out === 'SKIP') {
                verdict = 'skip';
            }
        } catch (e) {
            verdict = 'fail';
            why = (e && e.message ? e.message : String(e));
        }
        // A LEAK IS REPORTED, NOT SILENTLY REPAIRED: it means the probe above is
        // wrong, and quietly tidying up would hide that. It is folded into the
        // probe's own single entry rather than pushed as a second one - EXACTLY ONE
        // ENTRY PER PROBE is what makes the count invariant on the C++ side mean
        // anything, and a probe that both failed and leaked would otherwise be two.
        const after = scene.children.length;
        if (after !== before) {
            why += (why ? ' | ' : '') + 'left ' + (after - before) + ' object(s) on the display list';
            verdict = 'fail';
            while (scene.children.length > before) {
                scene.children.getChildren().pop().destroy();
            }
        }
        if (verdict === 'fail') {
            failed.push(name + ': ' + why);
        } else if (verdict === 'skip') {
            skipped.push(name);
        } else {
            passed.push(name);
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
