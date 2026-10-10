// What test/corpus/p5/p5_page.cpp runs inside its two pages. Each page loads
// this after the bundle and starts its own sketch from a script in its body,
// which is where the sketch started when the page was a string in a unit test.

// --- p5_page-webgl.html -----------------------------------------------------
//
// What the page saw, one line each, for p5_page_webgl_said to hand back.
var p5_page_webgl_lines = [];

function p5_page_webgl_start() {
    p5_page_webgl_lines.push('registered=' + (typeof p5.renderers['webgl']));
    var outcome = 'no error';
    try {
        new p5(function (s) {
            s.setup = function () {
                s.createCanvas(100, 100, s.WEBGL);
            };
            s.draw = function () {};
        });
    } catch (e) {
        outcome = e.name + ': ' + e.message;
    }
    p5_page_webgl_lines.push('asking=' + outcome);
}

// The engine's own contexts, asked for directly rather than through p5;
// p5_page.cpp says what it guarantees about them.
function p5_page_webgl_contexts() {
    var one = document.createElement('canvas').getContext('webgl');
    p5_page_webgl_lines.push('direct=' + (one !== null && typeof one.drawArrays === 'function'));
    p5_page_webgl_lines.push('two=' + (document.createElement('canvas').getContext('webgl2') !== null));
}

// The first line that starts with `prefix`, or '<not logged>' when the page
// never said one.
function p5_page_webgl_said(prefix) {
    return p5_page_webgl_lines.find(function (line) {
        return line.startsWith(prefix);
    }) || '<not logged>';
}

// --- p5_page-input.html -----------------------------------------------------
//
// Instance mode, so the assertions name what they mean rather than relying on
// p5 having installed 200 globals.
var p5_page_input_log = '';
var p5_page_input_sketch = null;

function p5_page_input_start() {
    new p5(function (s) {
        p5_page_input_sketch = s;
        s.setup = function () {
            s.createCanvas(200, 200);
            s.noLoop();
        };
        s.draw = function () {};
        s.mousePressed = function () {
            p5_page_input_log += 'down@' + s.mouseX + ',' + s.mouseY + ';';
        };
        s.mouseReleased = function () {
            p5_page_input_log += 'up;';
        };
        s.mouseMoved = function () {
            p5_page_input_log += 'move@' + s.mouseX + ',' + s.mouseY + ';';
        };
        s.keyPressed = function () {
            p5_page_input_log += 'key(' + s.key + ');';
        };
    });
}

// p5 computes mouseX by subtracting the canvas's own box from the
// event's viewport coordinates, so the check is that the two AGREE -
// not that the canvas sits at any particular place on the page.
function p5_page_input_report() {
    const box = p5_page_input_sketch._renderer.canvas.getBoundingClientRect();
    return p5_page_input_log + ' | offset=' + (40 - box.left) + ',' + (60 - box.top) +
        ' | pressed=' + p5_page_input_sketch.mouseIsPressed;
}
