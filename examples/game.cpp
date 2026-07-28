// A small canvas game, driven by keys.
//
// the previous engine's version of this used engine-specific shorthand - getContext("game") by
// element id, and global onKey/onFrame callbacks. None of that is here: the
// page below is ordinary web code, because the engine now supports ordinary
// web code and a shim that only this browser understands is a shim nobody can
// test against a real one.

import ctbrowser;

int main() {
    ctbrowser::app_options options;
    options.title = "game";
    options.width = 480;
    options.height = 360;

    return ctbrowser::run_app(R"(<!DOCTYPE html>
<title>game</title>
<style>
    body   { margin: 0; padding: 8px; background-color: #101018; color: #e8e8f0 }
    h1     { font-size: 16px; margin: 0 0 8px 0 }
    canvas { background-color: #1c1c28 }
    #score { font-size: 16px; margin: 8px 0 0 0 }
</style>
<h1>arrow keys to move, space to drop a marker</h1>
<canvas id=board width=460 height=280></canvas>
<p id=score>markers: 0</p>
<script>
    var ctx = document.getElementById("board").getContext("2d");
    var board = document.getElementById("board");
    var x = board.width / 2;
    var y = board.height / 2;
    var held = {};
    var markers = [];

    // e.code is the PHYSICAL key, so this reads the same on any layout.
    document.addEventListener("keydown", function (e) { held[e.code] = true; });
    document.addEventListener("keyup", function (e) { held[e.code] = false; });

    function step() {
        var speed = 4;
        if (held["ArrowLeft"])  { x -= speed; }
        if (held["ArrowRight"]) { x += speed; }
        if (held["ArrowUp"])    { y -= speed; }
        if (held["ArrowDown"])  { y += speed; }
        x = Math.max(10, Math.min(board.width - 10, x));
        y = Math.max(10, Math.min(board.height - 10, y));

        if (held["Space"] && markers.length < 40) {
            markers.push({ x: x, y: y });
            held["Space"] = false;
            document.getElementById("score").setText("markers: " + markers.length);
        }

        ctx.fillStyle = "#1c1c28";
        ctx.fillRect(0, 0, board.width, board.height);

        ctx.fillStyle = "#3a6ea5";
        markers.forEach(function (m) { ctx.fillRect(m.x - 3, m.y - 3, 6, 6); });

        ctx.fillStyle = "#ffcc33";
        ctx.beginPath();
        ctx.arc(x, y, 10, 0, Math.PI * 2);
        ctx.fill();

        requestAnimationFrame(step);
    }
    requestAnimationFrame(step);
</script>)",
                              options);
}
