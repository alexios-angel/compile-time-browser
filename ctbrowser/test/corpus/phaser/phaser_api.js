// The harness test/corpus/phaser/phaser_api.cpp runs the probes with: the
// game they run against, and the report they leave behind. The probes and
// their runner, `__runProbes`, are phaser-api-probe/*.js and
// phaser-api-probe.js; phaser_api.html loads this file BEFORE them, because
// every probe group pushes onto the list started here.
globalThis.__probes = [];
// The runner's JSON report, once every probe has settled.
var __out = '';

// The game the probes run against. CANVAS by name, so a probe that asserts
// the renderer type is asserting something; noAudio because a headless test
// has no business asking for a device.
//
// The probes run from CREATE, which is the first moment a scene is fully
// built - `scene.add`, `scene.textures` and the camera are all in place by
// then and not before.
function phaser_api_start() {
    new Phaser.Game({
        type: Phaser.CANVAS,
        width: 200,
        height: 150,
        banner: false,
        audio: {
            noAudio: true
        },
        // ARCADE PHYSICS IS ON, or `scene.physics` does not exist and the
        // physics probes could only assert that it is absent. Gravity is
        // zero so a body moves only where a probe pushes it - a probe that
        // has to subtract gravity to check its own arithmetic is testing
        // the probe.
        physics: {
            default: 'arcade',
            arcade: {
                gravity: {
                    y: 0
                },
                debug: false
            }
        },
        scene: {
            create: function () {
                globalThis.__runProbes(this).then(function (json) {
                    __out = json;
                });
            },
            update: function () {}
        }
    });
}

// The report, or empty while the runner has not finished.
function phaser_api_report() {
    return __out;
}

// Where the runner got to: the probe it is on, which `__runProbes` records
// before starting each one, and how many there are.
function phaser_api_at() {
    return 'at=' + globalThis.__at + ' probes=' +
        (globalThis.__probes ? globalThis.__probes.length : 'none');
}
