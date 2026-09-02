// ctbrowser's testharnessreport.js - THE PER-VENDOR HOOK.
//
// Every web-platform-test loads two scripts: `testharness.js`, which is the
// harness proper and is the same for everybody, and this file, which is the
// slot WPT leaves for the implementation to say where results should go. The
// copy in the WPT checkout does nothing; tools/wpt/run-wpt.py copies THIS over
// it before a run, which is the same thing every browser vendor does.
//
// WHERE THE RESULTS GO: onto `window.__wpt_state`, as a string that is already
// JSON. The runner reads it back through ctdrive's `eval`, which returns
// whatever the snippet LOGGED - so one console.log of one string is the whole
// channel, and it needs nothing from the engine that a page could not do.
//
// THE JSON IS BUILT BY HAND, and that is deliberate rather than primitive.
// Using JSON.stringify would make every result in the suite depend on this
// engine's JSON.stringify being right about nested objects, string escaping and
// lone surrogates - and when it was not, the failure would arrive as a hundred
// HARNESS_ERRORs that look like the DOM is broken. The one thing that must not
// be under test is the instrument.

(function () {
    "use strict";

    // JSON string quoting, by the letter of RFC 8259. Control characters below
    // 0x20 MUST be escaped; a raw one produces a string Python's json refuses,
    // and assert messages are full of newlines.
    function quote(text) {
        var out = '"';
        var s = String(text);
        for (var i = 0; i < s.length; i++) {
            var c = s.charAt(i);
            var n = s.charCodeAt(i);
            if (c === '"') {
                out += '\\"';
            } else if (c === "\\") {
                out += "\\\\";
            } else if (n < 0x20 || n === 0x7f) {
                // \u00XX rather than the short forms: fewer cases, same meaning.
                var hex = n.toString(16);
                while (hex.length < 4) { hex = "0" + hex; }
                out += "\\u" + hex;
            } else if (n > 0x7e) {
                // NON-ASCII ESCAPED TOO. The bytes travel through a socket, a
                // C++ std::string and a Python decode; escaping here means the
                // payload is pure ASCII and none of those three has to agree
                // about an encoding. Test NAMES in WPT contain astral
                // characters on purpose - dom/nodes is full of them.
                var hex2 = n.toString(16);
                while (hex2.length < 4) { hex2 = "0" + hex2; }
                out += "\\u" + hex2;
            } else {
                out += c;
            }
        }
        return out + '"';
    }

    // A subtest's status, as the name the runner classifies on. The numbers are
    // testharness.js's own constants; they are written out rather than read off
    // `Test.prototype` so that a harness that failed to define them cannot make
    // this file throw.
    function subtest_status(code) {
        switch (code) {
        case 0: return "PASS";
        case 1: return "FAIL";
        case 2: return "TIMEOUT";
        case 3: return "NOTRUN";
        case 4: return "PRECONDITION_FAILED";
        default: return "UNKNOWN";
        }
    }

    // And the HARNESS's own, which is a different enumeration with overlapping
    // numbers - the one mistake this file could make that would silently turn
    // errors into passes.
    function harness_status_name(code) {
        switch (code) {
        case 0: return "OK";
        case 1: return "ERROR";
        case 2: return "TIMEOUT";
        case 3: return "PRECONDITION_FAILED";
        default: return "UNKNOWN";
        }
    }

    function publish(json) {
        window.__wpt_state = json;
        window.__wpt_done = true;
    }

    function report(status_name, message, tests) {
        var json = '{"harness":' + quote(status_name) + ',"message":' + quote(message || "") +
                   ',"subtests":[';
        for (var i = 0; i < tests.length; i++) {
            if (i > 0) { json += ","; }
            json += '{"name":' + quote(tests[i].name) + ',"status":' +
                    quote(subtest_status(tests[i].status)) + ',"message":' +
                    quote(tests[i].message === null || tests[i].message === undefined
                              ? ""
                              : tests[i].message) + "}";
        }
        return json + "]}";
    }

    // THE HARNESS ITSELF MAY NOT BE THERE. `<script src="/resources/testharness.js">`
    // is a server-absolute path, and an engine that cannot resolve one loads
    // this file into a page with no harness at all. Saying so HERE, in the
    // payload, is what stops that arriving as "0 subtests, must be a pass".
    if (typeof add_completion_callback !== "function") {
        publish('{"harness":"ERROR","message":"testharness.js did not load: ' +
                'add_completion_callback is not defined","subtests":[]}');
        return;
    }

    // NO HTML OUTPUT. wptrunner's own hook does exactly this, and for exactly
    // this reason: the results table testharness.js builds into `#log` is for a
    // human with a browser window, and nothing here has one. Building it costs
    // a few hundred DOM operations per test and - worse - puts the engine's
    // `createElementNS`, `appendChild` and `textContent` between every test and
    // its result, so a defect in any of the three would arrive as a corpus-wide
    // failure that says nothing about the test that found it.
    //
    // The subtests are read from the completion callback's own array, which is
    // the harness's data rather than its rendering.
    if (typeof setup === "function") { setup({output: false}); }

    add_completion_callback(function (tests, status) {
        var json;
        try {
            json = report(harness_status_name(status.status), status.message, tests);
        } catch (e) {
            // A REPORTER THAT THREW IS AN ERROR, NOT A SILENCE. Without this the
            // page would simply never publish and the runner would call it a
            // timeout - blaming the engine for a bug in this file.
            json = '{"harness":"ERROR","message":' + quote("testharnessreport threw: " + e) +
                   ',"subtests":[]}';
        }
        publish(json);
    });
}());
