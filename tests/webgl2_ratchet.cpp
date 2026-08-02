// How far a WebGL 2 page gets through the engine.
//
// SAME SHAPE AS tests/p5_ratchet.cpp AND tests/phaser_ratchet.cpp - a ladder, a
// recorded rung, and a blocker that may not change silently - because that
// shape has earned it three times now.
//
// WHAT IS DIFFERENT HERE is that the corpus is not somebody else's library. p5
// and Phaser both run on WebGL 1 today, so neither can measure a WebGL 2 gap:
// p5's RendererGL asks for `webgl2` first and falls back without complaint, and
// PHASER NEVER ASKS FOR IT AT ALL - it requests `getContext('webgl')` and takes
// its extras from WebGL 1 extensions (see docs/webgl2-plan.md, which was
// rewritten around that measurement). So the rungs below drive the API
// directly, and the last one hands the finished context to Phaser's own
// renderer, which is the only part a library can answer.
//
// THE LADDER IS ORDERED BY WHAT DEPENDS ON WHAT, not by importance: a context
// first, then the constants a page compares against, then the language, then
// the two capabilities that are genuine additions rather than spellings, then
// pixels, then a real renderer on top.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

enum rung {
    rung_none = 0,
    rung_context = 1,    // getContext('webgl2') hands back a context
    rung_constants = 2,  // and the WebGL-2-only enums a page compares against
    rung_glsl300 = 3,    // a `#version 300 es` shader compiles and links
    rung_vao = 4,        // vertex array objects round-trip
    rung_instancing = 5, // vertexAttribDivisor + drawArraysInstanced accepted
    rung_draws = 6,      // and an instanced draw is IN THE PIXELS
    rung_extensions = 7, // the SAME capability under its WebGL 1 names
    rung_phaser = 8,     // Phaser's own WebGL renderer boots on it and paints
    rung_babylon = 9,    // and Babylon - the one corpus that WANTS WebGL 2 - runs
};

[[nodiscard]] const char * rung_name(int level) {
    switch (level) {
    case rung_none: return "nothing";
    case rung_context: return "makes a webgl2 context";
    case rung_constants: return "has the WebGL 2 constants";
    case rung_glsl300: return "compiles #version 300 es";
    case rung_vao: return "vertex array objects work";
    case rung_instancing: return "accepts instanced drawing";
    case rung_draws: return "an instanced draw reaches the pixels";
    case rung_extensions: return "the WebGL 1 extensions expose the same thing";
    case rung_phaser: return "Phaser's WebGL renderer paints on it";
    case rung_babylon: return "Babylon runs and reports its WebGL version";
    default: return "?";
    }
}

struct measurement {
    int level = rung_none;
    std::string blocker;
    bool stopped = false;

    void fail_at(int at, std::string why) {
        if (stopped) { return; }
        level = at - 1;
        const std::size_t newline = why.find('\n');
        blocker = newline == std::string::npos ? std::move(why) : why.substr(0, newline);
        stopped = true;
    }
    void reached(int at) {
        if (!stopped) { level = at; }
    }
};

[[nodiscard]] std::string read_file(const std::string & path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) { return {}; }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// The recorded floor: `key=value` lines, the same shape the other two records
// use.
[[nodiscard]] std::string recorded(const std::string & text, std::string_view key) {
    for (std::size_t at = 0; at < text.size();) {
        const std::size_t end = text.find('\n', at);
        const std::string_view line{text.data() + at,
                                    (end == std::string::npos ? text.size() : end) - at};
        if (line.starts_with(key) && line.size() > key.size() && line[key.size()] == '=') {
            return std::string{line.substr(key.size() + 1)};
        }
        if (end == std::string::npos) { break; }
        at = end + 1;
    }
    return {};
}

[[nodiscard]] std::string ask(ctbrowser::shell::browser & page, const char * expression) {
    const std::size_t before = page.bindings().console_output().size();
    (void)page.run_script(std::string{"try { console.log('=' + String("} + expression +
                          ")); } catch (e) { console.log('=threw: ' + (e && e.message ? "
                          "e.message : e)); }");
    const auto & said = page.bindings().console_output();
    for (std::size_t i = said.size(); i-- > before;) {
        if (said[i].starts_with("=")) { return said[i].substr(1); }
    }
    return "<no answer>";
}

// DOES BABYLON EVEN RUN? Reported beside the ladder rather than inside it,
// because the ladder stops at its first failure and this is the number that
// says whether rung 9 is reachable at all - an 11.6 MB bundle is a real ask of
// a JavaScript engine, and "the corpus is aspirational" is a different problem
// from "WebGL 2 is not implemented".
//
// The version it settles on is the honest headline for this whole plan: it
// reads 1 until getContext('webgl2') returns a context, and the day it reads 2
// is the day the work landed.
[[nodiscard]] std::string babylon_verdict() {
    const std::string source = read_file("examples/assets/babylon/babylon.js");
    if (source.empty()) { return "examples/assets/babylon/babylon.js is missing"; }
    // THROUGH THE COMPILER FIRST, because a page reports "parse error:
    // expression" with no position and that is half a diagnostic. The compiler
    // names the offset, which is the difference between a finding and a hunt -
    // the same reason tests/phaser_ratchet.cpp measures its language rungs
    // before it opens a page.
    const ctbrowser::script::program program = ctbrowser::script::compiler::compile(source);
    if (!program.ok) { return "does not compile: " + program.error; }
    ctbrowser::shell::browser page{ctbrowser::shell::browser_options{200, 200}};
    page.assets().add(
        "babylon.js",
        std::vector<std::byte>{reinterpret_cast<const std::byte *>(source.data()),
                               reinterpret_cast<const std::byte *>(source.data() + source.size())});
    page.load_html(R"(<html><head><meta charset="utf-8">
      <script src="babylon.js"></script></head>
      <body><canvas id=c width=64 height=64></canvas></body></html>)");
    if (!page.script_error().empty()) { return "the bundle threw: " + page.script_error(); }
    return ask(page, R"JS((function () {
        if (typeof BABYLON === 'undefined') { return 'BABYLON is not defined'; }
        try {
            var e = new BABYLON.Engine(document.getElementById('c'), true);
            return 'runs, webGLVersion=' + e.webGLVersion;
        } catch (err) { return 'Engine threw: ' + (err && err.message ? err.message : err); }
      })())JS");
}

[[nodiscard]] measurement measure() {
    measurement m;
    ctbrowser::shell::browser page{ctbrowser::shell::browser_options{200, 200}};
    page.load_html(R"(<html><body><canvas id=c width=64 height=64></canvas></body></html>)");

    // --- 1: a context at all ------------------------------------------------
    const std::string context = ask(page, "(function(){ var c = document.getElementById('c');"
                                          "var gl = c.getContext('webgl2'); window.__gl = gl;"
                                          "return gl ? 'ok' : 'null'; })()");
    if (context != "ok") {
        m.fail_at(rung_context, "getContext('webgl2') returned " + context);
        return m;
    }
    m.reached(rung_context);

    // --- 2: the constants a page compares against ---------------------------
    // Dull and load-bearing: a constant that arrives as `undefined` makes every
    // comparison against it silently false, which is how a page takes a path
    // nobody intended. These are WebGL-2-only values.
    const std::string constants =
        ask(page, "(function(){ var gl = window.__gl;"
                  "var want = ['RGBA8','DEPTH_COMPONENT24','TEXTURE_3D','UNIFORM_BUFFER',"
                  "'COLOR_ATTACHMENT1','MAX_DRAW_BUFFERS','SYNC_GPU_COMMANDS_COMPLETE',"
                  "'TRANSFORM_FEEDBACK','VERTEX_ARRAY_BINDING'];"
                  "var missing = want.filter(function(k){ return gl[k] === undefined; });"
                  "return missing.length ? missing.join(',') : 'ok'; })()");
    if (constants != "ok") {
        m.fail_at(rung_constants, "WebGL 2 constants missing: " + constants);
        return m;
    }
    m.reached(rung_constants);

    // --- 3: the language ----------------------------------------------------
    // GLSL ES 3.00 is what shader code on the web is written in now: `in`/`out`
    // rather than attribute/varying, and a declared output rather than
    // gl_FragColor. A context that accepts the calls and refuses the shaders is
    // a WebGL 2 context nobody can use.
    const std::string glsl = ask(page, R"JS((function(){ var gl = window.__gl;
        var vs = gl.createShader(gl.VERTEX_SHADER);
        gl.shaderSource(vs, '#version 300 es\nin vec2 p;\nvoid main(){ gl_Position = vec4(p,0.,1.); }');
        gl.compileShader(vs);
        if (!gl.getShaderParameter(vs, gl.COMPILE_STATUS)) { return 'vertex: ' + gl.getShaderInfoLog(vs); }
        var fs = gl.createShader(gl.FRAGMENT_SHADER);
        gl.shaderSource(fs, '#version 300 es\nprecision mediump float;\nout vec4 colour;\nvoid main(){ colour = vec4(1.,0.,0.,1.); }');
        gl.compileShader(fs);
        if (!gl.getShaderParameter(fs, gl.COMPILE_STATUS)) { return 'fragment: ' + gl.getShaderInfoLog(fs); }
        var p = gl.createProgram();
        gl.attachShader(p, vs); gl.attachShader(p, fs); gl.linkProgram(p);
        if (!gl.getProgramParameter(p, gl.LINK_STATUS)) { return 'link: ' + gl.getProgramInfoLog(p); }
        window.__program = p;
        return 'ok';
      })())JS");
    if (glsl != "ok") {
        m.fail_at(rung_glsl300, "a #version 300 es program did not build: " + glsl);
        return m;
    }
    m.reached(rung_glsl300);

    // --- 4: vertex array objects --------------------------------------------
    // THE ONE PHASER WOULD USE, though it reaches them through the WebGL 1
    // extension rather than through a WebGL 2 context. p5 uses none at all.
    const std::string vao = ask(page, R"JS((function(){ var gl = window.__gl;
        var a = gl.createVertexArray();
        if (!a) { return 'createVertexArray gave nothing'; }
        gl.bindVertexArray(a);
        var bound = gl.getParameter(gl.VERTEX_ARRAY_BINDING);
        gl.bindVertexArray(null);
        var unbound = gl.getParameter(gl.VERTEX_ARRAY_BINDING);
        gl.deleteVertexArray(a);
        if (!bound) { return 'VERTEX_ARRAY_BINDING was empty while one was bound'; }
        if (unbound) { return 'unbinding left one bound'; }
        return 'ok';
      })())JS");
    if (vao != "ok") {
        m.fail_at(rung_vao, "vertex array objects: " + vao);
        return m;
    }
    m.reached(rung_vao);

    // --- 5: instancing ------------------------------------------------------
    const std::string instancing = ask(page, R"JS((function(){ var gl = window.__gl;
        if (typeof gl.vertexAttribDivisor !== 'function') { return 'no vertexAttribDivisor'; }
        if (typeof gl.drawArraysInstanced !== 'function') { return 'no drawArraysInstanced'; }
        var buf = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, buf);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1,-1, 1,-1, 0,1]), gl.STATIC_DRAW);
        gl.useProgram(window.__program);
        var loc = gl.getAttribLocation(window.__program, 'p');
        if (loc < 0) { return 'the attribute did not survive linking'; }
        gl.enableVertexAttribArray(loc);
        gl.vertexAttribPointer(loc, 2, gl.FLOAT, false, 0, 0);
        gl.vertexAttribDivisor(loc, 0);
        gl.drawArraysInstanced(gl.TRIANGLES, 0, 3, 2);
        var err = gl.getError();
        return err === 0 ? 'ok' : 'getError ' + err;
      })())JS");
    if (instancing != "ok") {
        m.fail_at(rung_instancing, "instanced drawing: " + instancing);
        return m;
    }
    m.reached(rung_instancing);

    // --- 6: and does it reach the PIXELS? -----------------------------------
    // A draw that is accepted and paints nothing satisfies every rung above.
    // readPixels rather than the canvas store, because that is what a page
    // uses and it asks the context rather than the engine.
    const std::string pixels = ask(page, R"JS((function(){ var gl = window.__gl;
        gl.clearColor(0., 0., 0., 1.); gl.clear(gl.COLOR_BUFFER_BIT);
        gl.drawArraysInstanced(gl.TRIANGLES, 0, 3, 1);
        var px = new Uint8Array(4);
        gl.readPixels(32, 32, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
        return px[0] + ',' + px[1] + ',' + px[2];
      })())JS");
    if (pixels != "255,0,0") {
        m.fail_at(rung_draws, "the instanced draw did not reach the pixels: centre is " + pixels);
        return m;
    }
    m.reached(rung_draws);

    // --- 7: the same capability, spelled the WebGL 1 way --------------------
    // THE DECISION IN docs/webgl2-plan.md MADE CHECKABLE. VAOs and instancing
    // are implemented once and exposed twice, because Phaser reaches them only
    // through the extensions and would otherwise never touch the code the
    // WebGL 2 rungs above just proved. If these two ever disagree, there are
    // two implementations - which is the bug this tree has already paid for
    // with two URL parsers and two base64 decoders.
    const std::string extensions =
        ask(page, R"JS((function(){ var c = document.createElement('canvas');
        var gl = c.getContext('webgl');
        if (!gl) { return 'no webgl1 context'; }
        var names = gl.getSupportedExtensions() || [];
        var want = ['OES_vertex_array_object', 'ANGLE_instanced_arrays',
                    'OES_standard_derivatives'];
        var absent = want.filter(function (n) { return names.indexOf(n) < 0; });
        if (absent.length) { return 'not advertised: ' + absent.join(','); }
        var vao = gl.getExtension('OES_vertex_array_object');
        var ang = gl.getExtension('ANGLE_instanced_arrays');
        if (!vao || !ang) { return 'getExtension returned null for an advertised name'; }
        if (typeof vao.createVertexArrayOES !== 'function') { return 'no createVertexArrayOES'; }
        if (typeof ang.drawArraysInstancedANGLE !== 'function') { return 'no drawArraysInstancedANGLE'; }
        if (typeof ang.vertexAttribDivisorANGLE !== 'function') { return 'no vertexAttribDivisorANGLE'; }
        var a = vao.createVertexArrayOES();
        if (!a) { return 'createVertexArrayOES gave nothing'; }
        vao.bindVertexArrayOES(a);
        vao.bindVertexArrayOES(null);
        vao.deleteVertexArrayOES(a);
        // AND STILL null FOR THE REST, which is what a driver without one
        // returns and what a page checks for.
        if (gl.getExtension('EXT_disjoint_timer_query') !== null) {
          return 'an unimplemented extension answered non-null';
        }
        return 'ok';
      })())JS");
    if (extensions != "ok") {
        m.fail_at(rung_extensions, "the WebGL 1 extension spelling: " + extensions);
        return m;
    }
    m.reached(rung_extensions);

    // --- 8: and a real renderer on top --------------------------------------
    // PHASER DOES NOT ASK FOR webgl2 (it requests `webgl` and takes VAOs and
    // instancing from WebGL 1 extensions), so this rung asks the question that
    // is actually available: does its WebGL renderer boot and paint on this
    // engine at all. That is the claim a corpus can settle and the API cannot.
    const std::string bundle = read_file("examples/assets/phaser/phaser.js");
    if (bundle.empty()) {
        m.fail_at(rung_phaser, "examples/assets/phaser/phaser.js is missing");
        return m;
    }
    ctbrowser::shell::browser game{ctbrowser::shell::browser_options{200, 200}};
    game.assets().add(
        "phaser.js",
        std::vector<std::byte>{reinterpret_cast<const std::byte *>(bundle.data()),
                               reinterpret_cast<const std::byte *>(bundle.data() + bundle.size())});
    game.load_html(R"(<html><head><meta charset="utf-8">
      <script src="phaser.js"></script></head><body></body></html>)");
    (void)game.run_script(R"JS((function () {
        window.__painted = 'no scene';
        window.__game = new Phaser.Game({
            type: Phaser.WEBGL, width: 64, height: 64, banner: false, audio: {noAudio: true},
            scene: { create: function () {
                var g = this.add.graphics();
                g.fillStyle(0xff0000, 1);
                g.fillRect(0, 0, 64, 64);
                window.__painted = 'drew';
            } }
        });
    })())JS");
    for (int i = 0; i < 40; ++i) { game.tick(16); }
    const std::string drew = ask(game, "window.__painted");
    if (drew != "drew") {
        m.fail_at(rung_phaser,
                  "Phaser's WEBGL renderer did not reach create(): " + drew +
                      (game.script_error().empty() ? "" : " | " + game.script_error()));
        return m;
    }
    // Drew is not painted: the pixels are the claim.
    //
    // ASK PHASER WHICH CANVAS IS ITS OWN. Taking the first `canvas` in the
    // document is what tests/phaser_ratchet.cpp does and it is right there,
    // because a CANVAS-mode game makes one. A WEBGL game makes SEVERAL - the
    // texture manager builds canvases for its base64 textures - so the first in
    // document order is a texture, and reading it finds black however well the
    // renderer is working. That cost an afternoon: the draws were painting 2048
    // fragments with no shader error the whole time.
    (void)game.run_script("window.__game.canvas.id = 'phaser-game-canvas';");
    const auto txn = game.doc().read();
    ctbrowser::node_id canvas{};
    const ctbrowser::atom id_attribute = game.atoms().intern("id");
    const auto walk = [&](auto && self, ctbrowser::node_id at) -> void {
        if (!canvas && game.atoms().text(txn.tag(at).value_or(ctbrowser::atom{})) == "canvas" &&
            txn.attribute_value(at, id_attribute) == "phaser-game-canvas") {
            canvas = at;
        }
        for (const ctbrowser::node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    const auto surface = game.canvases().pixels_of(canvas);
    if (!canvas || surface == nullptr) {
        m.fail_at(rung_phaser, "Phaser's WEBGL canvas has no pixel buffer");
        return m;
    }
    const ctbrowser::color drawn{surface->at(32, 32)};
    if (drawn != ctbrowser::color::rgba(255, 0, 0)) {
        m.fail_at(rung_phaser, "Phaser's WEBGL fill did not reach the pixels (32,32 is " +
                                   std::to_string(drawn.red()) + "," +
                                   std::to_string(drawn.green()) + "," +
                                   std::to_string(drawn.blue()) + ")");
        return m;
    }
    m.reached(rung_phaser);

    // --- 9: the corpus this work exists for ---------------------------------
    // BABYLON IS THE ONLY ONE THAT WANTS WEBGL 2. It asks for it first, uses
    // nearly the whole specification, and gates 52 sites on `_webGLVersion` so
    // it degrades rather than refusing - which is what makes it measurable
    // today, before any of this is implemented.
    //
    // The rung asks two things: that the bundle runs at all, and WHICH VERSION
    // it settled on. That number is the honest headline for this whole plan -
    // it reads 1 until `getContext('webgl2')` returns a context, and the day it
    // reads 2 is the day the work landed.
    const std::string babylon_source = read_file("examples/assets/babylon/babylon.js");
    if (babylon_source.empty()) {
        m.fail_at(rung_babylon, "examples/assets/babylon/babylon.js is missing");
        return m;
    }
    ctbrowser::shell::browser babylon{ctbrowser::shell::browser_options{200, 200}};
    babylon.assets().add(
        "babylon.js",
        std::vector<std::byte>{
            reinterpret_cast<const std::byte *>(babylon_source.data()),
            reinterpret_cast<const std::byte *>(babylon_source.data() + babylon_source.size())});
    babylon.load_html(R"(<html><head><meta charset="utf-8">
      <script src="babylon.js"></script></head>
      <body><canvas id=c width=64 height=64></canvas></body></html>)");
    if (!babylon.script_error().empty()) {
        m.fail_at(rung_babylon, "the bundle did not run: " + babylon.script_error());
        return m;
    }
    const std::string version = ask(babylon, R"JS((function () {
        if (typeof BABYLON === 'undefined') { return 'BABYLON is not defined'; }
        var e = new BABYLON.Engine(document.getElementById('c'), true);
        return 'webgl' + e.webGLVersion;
      })())JS");
    if (version != "webgl2") {
        m.fail_at(rung_babylon, "Babylon settled on " + version);
        return m;
    }
    m.reached(rung_babylon);
    return m;
}

} // namespace

int main() {
    const measurement m = measure();

    std::printf("LEVEL %d/%d\n", m.level, rung_babylon);
    std::printf("BLOCKER %s\n", m.blocker.c_str());
    std::printf("     WEBGL2 LEVEL %d/%d (%s)\n", m.level, rung_babylon, rung_name(m.level));
    if (!m.blocker.empty()) { std::printf("     blocked by: %s\n", m.blocker.c_str()); }

    std::printf("     babylon: %s\n", babylon_verdict().c_str());

    // THE PAWL, identical in rule to the other two: the level may not go down,
    // and at the same level the blocker may not change. Only
    // tools/webgl2-ratchet.py --advance writes the record.
    const std::string record = read_file("tests/webgl2-ratchet.txt");
    if (record.empty()) {
        std::printf("     (no tests/webgl2-ratchet.txt yet - run "
                    "tools/webgl2-ratchet.py --advance to record this)\n");
        REPORT("webgl2_ratchet");
    }
    const std::string want_level = recorded(record, "level");
    const std::string want_blocker = recorded(record, "blocker");
    if (!want_level.empty()) {
        const int floor_level = std::stoi(want_level);
        if (m.level < floor_level) {
            std::printf("FAIL webgl2 went BACKWARDS: %d, recorded %d (%s)\n", m.level, floor_level,
                        rung_name(floor_level));
            ++ctbrowser_test_failures;
        } else if (m.level == floor_level && m.blocker != want_blocker) {
            std::printf("FAIL webgl2 is stuck at %d but the blocker CHANGED\n"
                        "  was: %s\n  now: %s\n",
                        m.level, want_blocker.c_str(), m.blocker.c_str());
            ++ctbrowser_test_failures;
        } else if (m.level > floor_level) {
            std::printf("     AHEAD of the record (%d > %d) - run "
                        "tools/webgl2-ratchet.py --advance\n",
                        m.level, floor_level);
        }
    }
    REPORT("webgl2_ratchet");
}
