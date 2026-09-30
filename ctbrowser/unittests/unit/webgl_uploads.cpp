// THE BYTES A TYPED ARRAY UPLOADS. getBufferSubData is WebGL 2 and refused
// here, and readPixels reads the canvas rather than a bound framebuffer, so
// each case is a 1x1 RGBA texture whose four bytes come from the array under
// test, sampled by a point drawn onto the canvas: once for R,G,B and once for
// A (as red), both opaque so no premultiplication touches them.
//
// OPTIONAL, like gl_basics: a build with no ANGLE, or an ANGLE whose device
// does not come up, passes and says it skipped - the page clears to red first
// and checks it reads back.

#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/shell/shell.hpp>

#include "check.hpp"
#include "chrome_probe.hpp"

using namespace ctbrowser;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser_test::log_of;

namespace {

[[nodiscard]] std::string said(browser & page, std::string_view prefix) {
    for (const std::string & line : log_of(page)) {
        if (line.starts_with(prefix)) { return line; }
    }
    return "(not logged: " + std::string{prefix} + ")";
}

// Every element kind is its own little-endian bytes: ToInt8 is modular, so
// Int8Array([-1]) is 0xFF (it uploaded 0 through a clamp); a Float64Array is
// eight bytes an element (it uploaded one, which the 1x1 level then refused as
// short); and a NaN or an out-of-float-range double reaches no undefined
// conversion on the way.
void test_typed_array_bytes() {
    browser page{browser_options{100, 100}};
    page.load_html(R"(<html><body><script>
        var gl = document.createElement('canvas').getContext('webgl');
        function shader(type, src) {
            var s = gl.createShader(type); gl.shaderSource(s, src); gl.compileShader(s); return s;
        }
        var p = gl.createProgram();
        gl.attachShader(p, shader(gl.VERTEX_SHADER,
            'void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); gl_PointSize = 8.0; }'));
        gl.attachShader(p, shader(gl.FRAGMENT_SHADER,
            'precision mediump float; uniform sampler2D t; uniform float alpha;' +
            'void main() { vec4 c = texture2D(t, vec2(0.5));' +
            ' gl_FragColor = alpha > 0.5 ? vec4(c.a, 0.0, 0.0, 1.0) : vec4(c.rgb, 1.0); }'));
        gl.linkProgram(p); gl.useProgram(p);
        var which = gl.getUniformLocation(p, 'alpha');
        function pixel() {
            var px = new Uint8Array(4);
            gl.readPixels(150, 75, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
            return px;
        }
        function probe(data) {
            var t = gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D, t);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
            gl.getError();
            gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, data);
            var e = gl.getError();
            gl.uniform1f(which, 0); gl.drawArrays(gl.POINTS, 0, 1);
            var rgb = pixel();
            gl.uniform1f(which, 1); gl.drawArrays(gl.POINTS, 0, 1);
            var a = pixel();
            return e + ':' + rgb[0] + ',' + rgb[1] + ',' + rgb[2] + ',' + a[0];
        }
        gl.clearColor(1, 0, 0, 1); gl.clear(gl.COLOR_BUFFER_BIT);
        var live = new Uint8Array(4);
        gl.readPixels(150, 75, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, live);
        console.log('live=' + live[0]);
        console.log('i8=' + probe(new Int8Array([-1, -2, 127, -1])));
        console.log('u8c=' + probe(new Uint8ClampedArray([300, -5, 1.5, 255])));
        console.log('f32=' + probe(new Float32Array([-1e300])));
        console.log('f64=' + probe(new Float64Array([NaN])));
        console.log('f32big=' + probe(new Float32Array([1e300])));
    </script></body></html>)");
    check(page.script_error().empty(), "the page loaded: " + page.script_error());
    if (said(page, "live=") != "live=255") {
        std::printf("SKIP webgl_uploads: no live WebGL device (%s)\n", said(page, "live=").c_str());
        return;
    }
    check(said(page, "i8=") == "i8=0:255,254,127,255", said(page, "i8="));
    check(said(page, "u8c=") == "u8c=0:255,0,2,255", said(page, "u8c="));
    // -Infinity as a float is 0xFF800000, little-endian.
    check(said(page, "f32=") == "f32=0:0,0,128,255", said(page, "f32="));
    // The canonical NaN's low four bytes are zero; eight bytes arrived.
    check(said(page, "f64=") == "f64=0:0,0,0,0", said(page, "f64="));
    // Past FLT_MAX a double rounds to +Infinity, 0x7F800000.
    check(said(page, "f32big=") == "f32big=0:0,0,128,127", said(page, "f32big="));
}

// bufferData(target, size, usage) is ZERO-FILLED: the storage is uploaded as
// zeros rather than left to whatever the GPU memory held. A point drawn with
// its colour read out of the fresh buffer (all four bytes summed into red)
// must be black. (ANGLE with robust resource init zeroes it anyway; without
// it this is the leak.)
void test_size_only_buffer_is_zeros() {
    browser page{browser_options{100, 100}};
    page.load_html(R"(<html><body><script>
        var gl = document.createElement('canvas').getContext('webgl');
        function shader(type, src) {
            var s = gl.createShader(type); gl.shaderSource(s, src); gl.compileShader(s); return s;
        }
        var p = gl.createProgram();
        gl.attachShader(p, shader(gl.VERTEX_SHADER, 'attribute vec4 c; varying vec4 v;' +
            'void main() { v = c; gl_Position = vec4(0.0, 0.0, 0.0, 1.0); gl_PointSize = 64.0; }'));
        gl.attachShader(p, shader(gl.FRAGMENT_SHADER, 'precision mediump float; varying vec4 v;' +
            'void main() { gl_FragColor = vec4(v.r + v.g + v.b + v.a, 0.0, 0.0, 1.0); }'));
        gl.linkProgram(p); gl.useProgram(p);
        var b = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, b);
        gl.bufferData(gl.ARRAY_BUFFER, new Uint8Array([255, 255, 255, 255]), gl.STATIC_DRAW);
        gl.bufferData(gl.ARRAY_BUFFER, 64, gl.STATIC_DRAW);
        var loc = gl.getAttribLocation(p, 'c');
        gl.enableVertexAttribArray(loc);
        gl.vertexAttribPointer(loc, 4, gl.UNSIGNED_BYTE, true, 0, 0);
        gl.clearColor(0, 1, 0, 1); gl.clear(gl.COLOR_BUFFER_BIT);
        gl.drawArrays(gl.POINTS, 0, 1);
        var px = new Uint8Array(4);
        gl.readPixels(150, 75, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
        console.log('zeros=' + gl.getError() + ':' + Array.from(px).join(','));
    </script></body></html>)");
    check(page.script_error().empty(), "the page loaded: " + page.script_error());
    const std::string zeros = said(page, "zeros=");
    if (zeros == "zeros=0:0,255,0,255") {
        std::printf("SKIP size-only bufferData: the point did not draw\n");
        return;
    }
    check(zeros == "zeros=0:0,0,0,255", zeros);
}

} // namespace

int main() {
    if (!raster::gl::available()) {
        std::printf("SKIP webgl_uploads: %s\n", raster::gl::unavailable_because().c_str());
        return 0;
    }
    test_typed_array_bytes();
    test_size_only_buffer_is_zeros();
    REPORT("webgl_uploads");
}
