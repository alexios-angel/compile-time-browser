// The WebGL context: the state machine, driven directly.
//
// No JavaScript here on purpose. shell/webgl.hpp is the state and the draw path,
// and shell/bindings.cpp is the thin layer that turns `gl.bufferData(...)` into
// a call on it - splitting them that way is what makes the interesting half
// testable without a page, and this is that test.
//
// WebGL's bugs are rarely in the drawing and almost always in WHAT WAS BOUND
// WHEN, so most of what follows is about binding rather than about pixels.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace ctbrowser;
using namespace ctbrowser::shell;

namespace {

constexpr const char * plain_vertex = R"(
    attribute vec2 aPosition;
    attribute vec4 aColor;
    varying vec4 vColor;
    void main() {
      vColor = aColor;
      gl_Position = vec4(aPosition, 0.0, 1.0);
    }
)";

constexpr const char * plain_fragment = R"(
    varying vec4 vColor;
    void main() { gl_FragColor = vColor; }
)";

// A buffer of floats, as bytes - which is what bufferData is handed.
[[nodiscard]] std::vector<std::byte> floats(std::initializer_list<float> values) {
    std::vector<std::byte> out(values.size() * sizeof(float));
    std::size_t at = 0;
    for (const float f : values) {
        std::memcpy(out.data() + at, &f, sizeof(f));
        at += sizeof(f);
    }
    return out;
}

// Everything a drawing test needs: a surface, a linked program, and a buffer.
struct harness {
    paint::bitmap surface;
    webgl_context gl;
    std::uint32_t program = 0;

    explicit harness(int size, const char * fragment = plain_fragment)
        : surface{size, size}, gl{&surface, size, size} {
        const std::uint32_t vertex_shader = gl.create_shader(gl_enum::vertex_shader);
        gl.shader_source(vertex_shader, plain_vertex);
        gl.compile_shader(vertex_shader);
        if (!gl.shader_compiled(vertex_shader)) {
            std::printf("FAIL the vertex shader did not compile: %s\n",
                        gl.shader_log(vertex_shader).c_str());
            ++ctbrowser_test_failures;
        }
        const std::uint32_t fragment_shader = gl.create_shader(gl_enum::fragment_shader);
        gl.shader_source(fragment_shader, fragment);
        gl.compile_shader(fragment_shader);
        if (!gl.shader_compiled(fragment_shader)) {
            std::printf("FAIL the fragment shader did not compile: %s\n",
                        gl.shader_log(fragment_shader).c_str());
            ++ctbrowser_test_failures;
        }
        program = gl.create_program();
        gl.attach_shader(program, vertex_shader);
        gl.attach_shader(program, fragment_shader);
        gl.link_program(program);
        if (!gl.program_linked(program)) {
            std::printf("FAIL the program did not link: %s\n", gl.program_log(program).c_str());
            ++ctbrowser_test_failures;
        }
        gl.use_program(program);
    }

    // Position and colour interleaved, which is how a real page packs a vertex:
    // x, y, r, g, b, a - six floats, stride 24, colour at offset 8.
    void bind_interleaved(std::vector<std::byte> data) {
        const std::uint32_t buffer = gl.create_buffer();
        gl.bind_buffer(gl_enum::array_buffer, buffer);
        gl.buffer_data(gl_enum::array_buffer, std::move(data), gl_enum::static_draw);
        const int position = gl.attribute_location(program, "aPosition");
        const int colour = gl.attribute_location(program, "aColor");
        gl.enable_attribute(position, true);
        gl.attribute_pointer(position, 2, gl_enum::float_, false, 24, 0);
        gl.enable_attribute(colour, true);
        gl.attribute_pointer(colour, 4, gl_enum::float_, false, 24, 8);
    }

    [[nodiscard]] int red(int x, int y) const {
        return static_cast<int>((surface.at(x, y) >> 16) & 0xFF);
    }
    [[nodiscard]] int green(int x, int y) const {
        return static_cast<int>((surface.at(x, y) >> 8) & 0xFF);
    }
    [[nodiscard]] int blue(int x, int y) const { return static_cast<int>(surface.at(x, y) & 0xFF); }
};

// --- compiling and linking -------------------------------------------------

void test_a_broken_shader_reports_why() {
    paint::bitmap surface{8, 8};
    webgl_context gl{&surface, 8, 8};
    const std::uint32_t shader = gl.create_shader(gl_enum::fragment_shader);
    gl.shader_source(shader, "void main() { gl_FragColor = ; }");
    gl.compile_shader(shader);
    CHECK(!gl.shader_compiled(shader));
    // The log is what a page shows a person, so it has to say something and
    // carry a line number.
    CHECK(!gl.shader_log(shader).empty());
    CHECK(gl.shader_log(shader).find("0:1") != std::string::npos);
}

// A program whose fragment shader reads a varying the vertex shader never
// writes is a LINK ERROR in GL. Without the check it links and draws black,
// which is a silent wrong answer of exactly the kind this engine refuses.
void test_linking_catches_a_missing_varying() {
    paint::bitmap surface{8, 8};
    webgl_context gl{&surface, 8, 8};
    const std::uint32_t vertex = gl.create_shader(gl_enum::vertex_shader);
    gl.shader_source(vertex, "attribute vec2 aPosition;\n"
                             "void main() { gl_Position = vec4(aPosition, 0.0, 1.0); }");
    gl.compile_shader(vertex);
    const std::uint32_t fragment = gl.create_shader(gl_enum::fragment_shader);
    gl.shader_source(fragment, "varying vec4 vNeverWritten;\n"
                               "void main() { gl_FragColor = vNeverWritten; }");
    gl.compile_shader(fragment);
    CHECK(gl.shader_compiled(vertex) && gl.shader_compiled(fragment));

    const std::uint32_t program = gl.create_program();
    gl.attach_shader(program, vertex);
    gl.attach_shader(program, fragment);
    gl.link_program(program);
    CHECK(!gl.program_linked(program));
    CHECK(gl.program_log(program).find("vNeverWritten") != std::string::npos);
}

// Attribute locations are assigned at link time in declaration order, and a
// name that is not an attribute is -1 rather than 0 - which would silently
// alias it to the first one.
void test_attribute_locations() {
    harness h{8};
    CHECK(h.gl.attribute_location(h.program, "aPosition") == 0);
    CHECK(h.gl.attribute_location(h.program, "aColor") == 1);
    CHECK(h.gl.attribute_location(h.program, "aNotThere") == -1);
    CHECK(h.gl.attribute_location(h.program, "vColor") == -1); // a varying is not an attribute
}

// --- drawing ---------------------------------------------------------------

void test_a_triangle_reaches_the_surface() {
    harness h{32};
    // A triangle covering the viewport, red at every corner.
    h.bind_interleaved(floats({
        -3,
        -3,
        1,
        0,
        0,
        1,
        3,
        -3,
        1,
        0,
        0,
        1,
        0,
        3,
        1,
        0,
        0,
        1,
    }));
    const std::size_t written = h.gl.draw_arrays(gl_enum::triangles, 0, 3);
    CHECK(written > 0);
    CHECK(h.red(16, 16) == 255);
    CHECK(h.green(16, 16) == 0);
    CHECK(h.gl.error() == gl_enum::no_error);
}

// AN INTERLEAVED BUFFER is how a real page packs a vertex, and unpacking it is
// where the bugs are: a wrong stride reads plausible numbers from the wrong
// place and draws a triangle that looks like a different one.
void test_interleaved_attributes_are_unpacked() {
    harness h{32};
    h.bind_interleaved(floats({
        // x    y     r  g  b  a
        -3,
        -3,
        1,
        0,
        0,
        1, // red
        3,
        -3,
        0,
        1,
        0,
        1, // green
        0,
        3,
        0,
        0,
        1,
        1, // blue
    }));
    CHECK(h.gl.draw_arrays(gl_enum::triangles, 0, 3) > 0);
    // Near the bottom-left corner it is mostly red; bottom-right mostly green.
    // If the stride were wrong these would be equal, or zero.
    CHECK(h.red(3, 28) > h.green(3, 28));
    CHECK(h.green(28, 28) > h.red(28, 28));
    CHECK(h.blue(16, 3) > h.red(16, 3));
}

// A STRIDE OF ZERO MEANS TIGHTLY PACKED. Treating it literally makes every
// vertex read the first one, which draws a degenerate triangle - that is, none.
void test_a_zero_stride_means_tightly_packed() {
    harness h{32};
    const std::uint32_t buffer = h.gl.create_buffer();
    h.gl.bind_buffer(gl_enum::array_buffer, buffer);
    h.gl.buffer_data(gl_enum::array_buffer, floats({-3, -3, 3, -3, 0, 3}), gl_enum::static_draw);
    const int position = h.gl.attribute_location(h.program, "aPosition");
    h.gl.enable_attribute(position, true);
    h.gl.attribute_pointer(position, 2, gl_enum::float_, false, 0, 0); // stride 0
    CHECK(h.gl.draw_arrays(gl_enum::triangles, 0, 3) > 0);
}

// `vertexAttribPointer` CAPTURES THE BUFFER BOUND AT THE MOMENT IT IS CALLED.
// That is the most surprising rule in the API: binding a different buffer
// afterwards does not move the attribute, and an implementation that looks up
// ARRAY_BUFFER at draw time instead reads the wrong data with no complaint.
void test_the_attribute_captures_its_buffer() {
    harness h{32};
    const std::uint32_t real = h.gl.create_buffer();
    h.gl.bind_buffer(gl_enum::array_buffer, real);
    h.gl.buffer_data(gl_enum::array_buffer, floats({-3, -3, 3, -3, 0, 3}), gl_enum::static_draw);
    const int position = h.gl.attribute_location(h.program, "aPosition");
    h.gl.enable_attribute(position, true);
    h.gl.attribute_pointer(position, 2, gl_enum::float_, false, 0, 0);

    // Now bind something else entirely. The attribute must not follow it.
    const std::uint32_t decoy = h.gl.create_buffer();
    h.gl.bind_buffer(gl_enum::array_buffer, decoy);
    h.gl.buffer_data(gl_enum::array_buffer, floats({0, 0, 0, 0, 0, 0}), gl_enum::static_draw);

    // The triangle still draws, because the attribute still points at `real`.
    CHECK(h.gl.draw_arrays(gl_enum::triangles, 0, 3) > 0);
}

// Bytes with `normalized` map 0..255 onto 0..1, which is how a colour arrives
// as four bytes rather than four floats - a four-fold saving a real page takes.
void test_normalized_byte_attributes() {
    harness h{32};
    const std::uint32_t positions = h.gl.create_buffer();
    h.gl.bind_buffer(gl_enum::array_buffer, positions);
    h.gl.buffer_data(gl_enum::array_buffer, floats({-3, -3, 3, -3, 0, 3}), gl_enum::static_draw);
    const int position = h.gl.attribute_location(h.program, "aPosition");
    h.gl.enable_attribute(position, true);
    h.gl.attribute_pointer(position, 2, gl_enum::float_, false, 0, 0);

    const std::uint32_t colours = h.gl.create_buffer();
    h.gl.bind_buffer(gl_enum::array_buffer, colours);
    std::vector<std::byte> bytes;
    for (int i = 0; i < 3; ++i) {
        for (const int channel : {255, 128, 0, 255}) {
            bytes.push_back(static_cast<std::byte>(channel));
        }
    }
    h.gl.buffer_data(gl_enum::array_buffer, std::move(bytes), gl_enum::static_draw);
    const int colour = h.gl.attribute_location(h.program, "aColor");
    h.gl.enable_attribute(colour, true);
    h.gl.attribute_pointer(colour, 4, gl_enum::unsigned_byte, true, 0, 0);

    CHECK(h.gl.draw_arrays(gl_enum::triangles, 0, 3) > 0);
    CHECK(h.red(16, 16) == 255);
    CHECK(h.green(16, 16) == 128); // 128/255 back to 128, within a rounding step
    CHECK(h.blue(16, 16) == 0);
}

// Uniforms live with the PROGRAM, so useProgram does not reset them - a page
// sets them once and relies on that.
void test_uniforms() {
    paint::bitmap surface{16, 16};
    webgl_context gl{&surface, 16, 16};
    const std::uint32_t vertex = gl.create_shader(gl_enum::vertex_shader);
    gl.shader_source(vertex, "attribute vec2 aPosition;\n"
                             "void main() { gl_Position = vec4(aPosition, 0.0, 1.0); }");
    gl.compile_shader(vertex);
    const std::uint32_t fragment = gl.create_shader(gl_enum::fragment_shader);
    gl.shader_source(fragment, "uniform vec4 uColor;\n"
                               "void main() { gl_FragColor = uColor; }");
    gl.compile_shader(fragment);
    const std::uint32_t program = gl.create_program();
    gl.attach_shader(program, vertex);
    gl.attach_shader(program, fragment);
    gl.link_program(program);
    CHECK(gl.program_linked(program));
    gl.use_program(program);

    raster::glsl::value colour;
    colour.t = raster::glsl::type{raster::glsl::base::f, 4, 1, -1, 0};
    colour.v = {0.0f, 0.0f, 1.0f, 1.0f};
    gl.set_uniform("uColor", colour);

    const std::uint32_t buffer = gl.create_buffer();
    gl.bind_buffer(gl_enum::array_buffer, buffer);
    gl.buffer_data(gl_enum::array_buffer, floats({-3, -3, 3, -3, 0, 3}), gl_enum::static_draw);
    gl.enable_attribute(0, true);
    gl.attribute_pointer(0, 2, gl_enum::float_, false, 0, 0);

    CHECK(gl.draw_arrays(gl_enum::triangles, 0, 3) > 0);
    CHECK((surface.at(8, 8) & 0xFF) == 255); // blue
}

// An indexed draw reuses a vertex rather than storing it twice, which is the
// whole point - and it means the index buffer decides the ORDER.
void test_draw_elements() {
    harness h{32};
    // Four corners of a quad, drawn as two triangles by index.
    h.bind_interleaved(floats({
        -1, -1, 1, 0, 0, 1, 1, -1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, -1, 1, 1, 0, 0, 1,
    }));
    const std::uint32_t indices = h.gl.create_buffer();
    h.gl.bind_buffer(gl_enum::element_array_buffer, indices);
    std::vector<std::byte> index_bytes(6 * sizeof(std::uint16_t));
    const std::uint16_t order[] = {0, 1, 2, 0, 2, 3};
    std::memcpy(index_bytes.data(), order, sizeof(order));
    h.gl.buffer_data(gl_enum::element_array_buffer, std::move(index_bytes), gl_enum::static_draw);

    const std::size_t written =
        h.gl.draw_elements(gl_enum::triangles, 6, gl_enum::unsigned_short, 0);
    // The whole 32x32 surface, exactly once - which also re-checks the fill rule
    // through a different path than softgl_basics uses.
    CHECK(written == 1024);
    CHECK(h.red(16, 16) == 255);
}

// --- state -----------------------------------------------------------------

void test_clear() {
    paint::bitmap surface{8, 8};
    webgl_context gl{&surface, 8, 8};
    gl.clear_color(0.0f, 1.0f, 0.0f, 1.0f);
    gl.clear(gl_enum::color_buffer_bit);
    CHECK(((surface.at(4, 4) >> 8) & 0xFF) == 255);
    CHECK(((surface.at(0, 0) >> 8) & 0xFF) == 255); // every pixel, not just some
}

// GL'S ORIGIN IS THE BOTTOM LEFT and a bitmap's is the top left, so a viewport
// that is not the whole surface has to be flipped. A page asking for the top
// half in its own terms is asking for the BOTTOM half in GL's.
void test_the_viewport_origin_is_flipped() {
    harness h{32};
    // GL's bottom half: y = 0, height 16.
    h.gl.viewport(0, 0, 32, 16);
    h.bind_interleaved(floats({
        -3,
        -3,
        1,
        0,
        0,
        1,
        3,
        -3,
        1,
        0,
        0,
        1,
        0,
        3,
        1,
        0,
        0,
        1,
    }));
    CHECK(h.gl.draw_arrays(gl_enum::triangles, 0, 3) > 0);
    // ...lands in the BOTTOM rows of the bitmap.
    CHECK(h.red(16, 28) == 255);
    CHECK(h.red(16, 4) == 0);
}

void test_error_reporting() {
    paint::bitmap surface{8, 8};
    webgl_context gl{&surface, 8, 8};
    CHECK(gl.error() == gl_enum::no_error);
    // bufferData with nothing bound is INVALID_OPERATION rather than a write
    // into nothing.
    gl.buffer_data(gl_enum::array_buffer, floats({1, 2, 3}), gl_enum::static_draw);
    CHECK(gl.error() == gl_enum::invalid_operation);
    // THE FIRST ERROR WINS and is cleared by reading it, which is what
    // glGetError does - so a later failure cannot hide the cause.
    gl.set_enabled(0xDEAD, true);
    CHECK(gl.take_error() == gl_enum::invalid_operation);
    CHECK(gl.error() == gl_enum::no_error);
    // A draw with no program is an error rather than a silent nothing.
    CHECK(gl.draw_arrays(gl_enum::triangles, 0, 3) == 0);
    CHECK(gl.error() == gl_enum::invalid_operation);
}

// Anything but TRIANGLES is refused out loud. softgl.hpp names points and lines
// as not drawn; a page that asks for a line strip gets an answer it can read
// rather than an empty canvas.
void test_unsupported_modes_are_refused() {
    harness h{16};
    h.bind_interleaved(floats({-3, -3, 1, 0, 0, 1, 3, -3, 1, 0, 0, 1, 0, 3, 1, 0, 0, 1}));
    CHECK(h.gl.draw_arrays(gl_enum::triangle_strip, 0, 3) == 0);
    CHECK(h.gl.take_error() == gl_enum::invalid_enum);
    CHECK(h.gl.draw_arrays(gl_enum::points, 0, 3) == 0);
    CHECK(h.gl.take_error() == gl_enum::invalid_enum);
}

// Deleting a bound object UNBINDS it. Leaving the binding is how a later draw
// reads state that is gone.
void test_delete_unbinds() {
    harness h{16};
    const std::uint32_t buffer = h.gl.create_buffer();
    h.gl.bind_buffer(gl_enum::array_buffer, buffer);
    h.gl.delete_object(buffer);
    // Writing to the deleted binding is an error rather than a crash.
    h.gl.buffer_data(gl_enum::array_buffer, floats({1}), gl_enum::static_draw);
    CHECK(h.gl.error() == gl_enum::invalid_operation);

    h.gl.delete_object(h.program);
    CHECK(h.gl.draw_arrays(gl_enum::triangles, 0, 3) == 0);
}

// --- textures --------------------------------------------------------------

void test_texture_sampling() {
    paint::bitmap surface{16, 16};
    webgl_context gl{&surface, 16, 16};
    const std::uint32_t vertex = gl.create_shader(gl_enum::vertex_shader);
    gl.shader_source(vertex, "attribute vec2 aPosition;\n"
                             "varying vec2 vCoord;\n"
                             "void main() {\n"
                             "  vCoord = aPosition * 0.5 + 0.5;\n"
                             "  gl_Position = vec4(aPosition, 0.0, 1.0);\n"
                             "}");
    gl.compile_shader(vertex);
    const std::uint32_t fragment = gl.create_shader(gl_enum::fragment_shader);
    gl.shader_source(fragment, "uniform sampler2D uSampler;\n"
                               "varying vec2 vCoord;\n"
                               "void main() { gl_FragColor = texture2D(uSampler, vCoord); }");
    gl.compile_shader(fragment);
    const std::uint32_t program = gl.create_program();
    gl.attach_shader(program, vertex);
    gl.attach_shader(program, fragment);
    gl.link_program(program);
    CHECK(gl.program_linked(program));
    gl.use_program(program);

    // A 2x2 texture uploaded as RGBA BYTES - the byte order a page uses is not
    // the packing a bitmap uses, and swapping them exchanges red and blue.
    const std::uint32_t texture = gl.create_texture();
    gl.active_texture(gl_enum::texture0);
    gl.bind_texture(gl_enum::texture_2d, texture);
    std::vector<std::byte> texels;
    for (const int channel : {255, 0, 0, 255, /**/ 255, 0, 0, 255, /**/ 255, 0, 0, 255,
                              /**/ 255, 0, 0, 255}) {
        texels.push_back(static_cast<std::byte>(channel));
    }
    gl.texture_image(gl_enum::texture_2d, 2, 2, std::move(texels));
    gl.texture_parameter(gl_enum::texture_2d, gl_enum::texture_min_filter, gl_enum::nearest);

    raster::glsl::value unit = raster::glsl::value::integer(0);
    gl.set_uniform("uSampler", unit);

    const std::uint32_t buffer = gl.create_buffer();
    gl.bind_buffer(gl_enum::array_buffer, buffer);
    gl.buffer_data(gl_enum::array_buffer, floats({-3, -3, 3, -3, 0, 3}), gl_enum::static_draw);
    gl.enable_attribute(0, true);
    gl.attribute_pointer(0, 2, gl_enum::float_, false, 0, 0);

    CHECK(gl.draw_arrays(gl_enum::triangles, 0, 3) > 0);
    // Red, not blue - which is what a swapped channel order would give.
    CHECK(((surface.at(8, 8) >> 16) & 0xFF) == 255);
    CHECK((surface.at(8, 8) & 0xFF) == 0);
}

// --- through a page --------------------------------------------------------

// THE WHOLE STACK, from `getContext('webgl')` to pixels: the binding layer, the
// context, the rasteriser and the GLSL evaluator, driven the way a page drives
// them. Everything above this point tests one layer; this is the one that fails
// when two of them disagree.
void test_a_page_can_draw_a_triangle() {
    ctbrowser::browser page{{.width = 200, .height = 200}};
    page.load_html(R"(<body><canvas id='c' width='64' height='64'></canvas><script>
      var gl = document.getElementById('c').getContext('webgl');
      console.log('context=' + (gl !== null) + ' TRIANGLES=' + gl.TRIANGLES);
      var vs = gl.createShader(gl.VERTEX_SHADER);
      gl.shaderSource(vs, 'attribute vec2 aPosition; attribute vec3 aColor;' +
                          'varying vec3 vColor;' +
                          'void main() { vColor = aColor;' +
                          '  gl_Position = vec4(aPosition, 0.0, 1.0); }');
      gl.compileShader(vs);
      console.log('vs=' + gl.getShaderParameter(vs, gl.COMPILE_STATUS));
      var fs = gl.createShader(gl.FRAGMENT_SHADER);
      gl.shaderSource(fs, 'precision mediump float; varying vec3 vColor;' +
                          'void main() { gl_FragColor = vec4(vColor, 1.0); }');
      gl.compileShader(fs);
      console.log('fs=' + gl.getShaderParameter(fs, gl.COMPILE_STATUS));
      var p = gl.createProgram();
      gl.attachShader(p, vs); gl.attachShader(p, fs); gl.linkProgram(p);
      console.log('link=' + gl.getProgramParameter(p, gl.LINK_STATUS) + ' ' +
                  gl.getProgramInfoLog(p));
      gl.useProgram(p);
      var buf = gl.createBuffer();
      gl.bindBuffer(gl.ARRAY_BUFFER, buf);
      gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
        -0.9, -0.9, 1, 0, 0,
         0.9, -0.9, 0, 1, 0,
         0.0,  0.9, 0, 0, 1]), gl.STATIC_DRAW);
      var pos = gl.getAttribLocation(p, 'aPosition');
      var col = gl.getAttribLocation(p, 'aColor');
      console.log('locations=' + pos + ',' + col);
      gl.enableVertexAttribArray(pos);
      gl.vertexAttribPointer(pos, 2, gl.FLOAT, false, 20, 0);
      gl.enableVertexAttribArray(col);
      gl.vertexAttribPointer(col, 3, gl.FLOAT, false, 20, 8);
      gl.clearColor(0.0, 0.0, 0.0, 1.0);
      gl.clear(gl.COLOR_BUFFER_BIT);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
      console.log('error=' + gl.getError());
      var px = new Uint8Array(4);
      gl.readPixels(32, 8, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
      console.log('bottom=' + px[0] + ',' + px[1] + ',' + px[2]);
      gl.readPixels(1, 1, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
      console.log('corner=' + px[0] + ',' + px[1] + ',' + px[2] + ',' + px[3]);
    </script></body>)");
    CHECK(page.script_error().empty());
    if (!page.script_error().empty()) { std::printf("     %s\n", page.script_error().c_str()); }
    const auto & log = page.bindings().console_output();
    for (const std::string & line : log) { std::printf("     %s\n", line.c_str()); }
    CHECK(log.size() == 8);
    if (log.size() != 8) { return; }
    CHECK(log[0] == "context=true TRIANGLES=4");
    CHECK(log[1] == "vs=true");
    CHECK(log[2] == "fs=true");
    CHECK(log[3] == "link=true ");
    // Locations are assigned in declaration order at link time.
    CHECK(log[4] == "locations=0,1");
    CHECK(log[5] == "error=0");
    // NEAR THE BOTTOM EDGE the triangle is between its red and green corners, so
    // both channels are substantial and blue - the apex colour - is not.
    // readPixels reads BOTTOM-UP, which is the same origin flip the viewport has.
    // Measured: red and green both substantial, blue small. A stride or an
    // interpolation bug gives one channel or none.
    CHECK(log[6] == "bottom=114,118,23");
    // The corner outside the triangle is the clear colour, opaque black.
    CHECK(log[7] == "corner=0,0,0,255");
}

// WHAT A PROGRAM DECLARES, enumerated - and this is the shape of hole that a
// hand-written test cannot find.
//
// Every WebGL page in this tree, and every test above, asks for uniforms and
// attributes BY NAME, because a page that wrote the shader already knows what
// is in it. A LIBRARY does the opposite: it asks how many there are and walks
// them. p5 does exactly that, got zero for both, concluded the shader declared
// nothing, bound no attributes, set no matrices, and drew a cube with no
// vertices - through a getProgramParameter that answered 0 to every question it
// did not recognise.
void test_a_program_can_be_enumerated() {
    ctbrowser::browser page{{.width = 64, .height = 64}};
    page.load_html(R"(<body><canvas id='c' width='64' height='64'></canvas><script>
      var gl = document.getElementById('c').getContext('webgl');
      var vs = gl.createShader(gl.VERTEX_SHADER);
      gl.shaderSource(vs, 'attribute vec3 aPos;' +
                          'attribute vec2 aUV;' +
                          'uniform mat4 uModel;' +
                          'uniform float uScale;' +
                          'varying vec2 vUV;' +
                          'void main() { vUV = aUV;' +
                          '  gl_Position = uModel * vec4(aPos * uScale, 1.0); }');
      gl.compileShader(vs);
      var fs = gl.createShader(gl.FRAGMENT_SHADER);
      gl.shaderSource(fs, 'precision mediump float;' +
                          'uniform vec4 uColour;' +
                          'varying vec2 vUV;' +
                          'void main() { gl_FragColor = uColour * vec4(vUV, 1.0, 1.0); }');
      gl.compileShader(fs);
      var p = gl.createProgram();
      gl.attachShader(p, vs); gl.attachShader(p, fs); gl.linkProgram(p);
      console.log('linked=' + gl.getProgramParameter(p, gl.LINK_STATUS));
      console.log('attribs=' + gl.getProgramParameter(p, gl.ACTIVE_ATTRIBUTES));
      console.log('uniforms=' + gl.getProgramParameter(p, gl.ACTIVE_UNIFORMS));
      // The INDEX is the location too, which is the contract a caller walking
      // these depends on - it enables attribute i and points it at its buffer.
      var names = [];
      for (var i = 0; i < gl.getProgramParameter(p, gl.ACTIVE_ATTRIBUTES); i++) {
        var a = gl.getActiveAttrib(p, i);
        names.push(a.name + ':' + a.size + ':' + (a.type === gl.FLOAT_VEC3 ? 'vec3' :
                   a.type === gl.FLOAT_VEC2 ? 'vec2' : a.type) +
                   ':' + gl.getAttribLocation(p, a.name) + '=' + i);
      }
      console.log('attrib=' + names.join(' '));
      // The TYPE has to be reported as the constant the context itself hands
      // out, because a caller switches on it: `case gl.FLOAT_MAT4:`. Comparing
      // against gl.* here rather than against a pasted 0x8B5C is what makes this
      // test fail if the two ever disagree.
      var us = {};
      for (var j = 0; j < gl.getProgramParameter(p, gl.ACTIVE_UNIFORMS); j++) {
        var u = gl.getActiveUniform(p, j);
        us[u.name] = (u.type === gl.FLOAT_MAT4) ? 'mat4'
                   : (u.type === gl.FLOAT) ? 'float'
                   : (u.type === gl.FLOAT_VEC4) ? 'vec4' : ('?' + u.type);
      }
      console.log('uModel=' + us.uModel + ' uScale=' + us.uScale + ' uColour=' + us.uColour);
      // Past the end is null, not a throw and not a made-up entry.
      console.log('past=' + (gl.getActiveUniform(p, 99) === null));
    </script></body>)");
    CHECK(page.script_error().empty());
    const auto & log = page.bindings().console_output();
    CHECK(log.size() == 6);
    if (log.size() != 6) { return; }
    CHECK(log[0] == "linked=true");
    // TWO attributes and THREE uniforms - the fragment shader's uColour counts,
    // because GL links both stages into one uniform namespace.
    CHECK(log[1] == "attribs=2");
    CHECK(log[2] == "uniforms=3");
    CHECK(log[3] == "attrib=aPos:1:vec3:0=0 aUV:1:vec2:1=1");
    CHECK(log[4] == "uModel=mat4 uScale=float uColour=vec4");
    CHECK(log[5] == "past=true");
}

// p5 asks for `webgl2` FIRST and falls back to `webgl`, and this test asserted
// the WRONG HALF of that for as long as it existed.
//
// It said refusing 2 out loud was what made the fallback happen. It is not: the
// fallback is `getContext('webgl2') || getContext('webgl')`, which needs a falsy
// value to fall THROUGH. A throw escaped p5's constructor entirely and left the
// sketch on its 2D renderer - the exact outcome the throw was defending against.
// Null is also what the specification returns for an unsupported context id.
void test_webgl2_is_null_not_a_throw() {
    ctbrowser::browser page{{.width = 100, .height = 100}};
    page.load_html(R"(<body><canvas id='c'></canvas><script>
      var c = document.getElementById('c');
      console.log('webgl2=' + (c.getContext('webgl2') === null));
      console.log('webgl=' + (c.getContext('webgl') !== null));
      console.log('unknown=' + (c.getContext('nonsense') === null));
      // getContext is IDEMPOTENT: a page calling it twice gets the same context,
      // with its buffers and programs still there. A fresh one each time would
      // quietly lose everything uploaded.
      console.log('same=' + (c.getContext('webgl') === c.getContext('webgl')));
    </script></body>)");
    CHECK(page.script_error().empty());
    const auto & log = page.bindings().console_output();
    CHECK(log.size() == 4);
    if (log.size() == 4) {
        CHECK(log[0] == "webgl2=true");
        CHECK(log[1] == "webgl=true");
        CHECK(log[2] == "unknown=true");
        CHECK(log[3] == "same=true");
    }
}

} // namespace

int main() {
    test_a_broken_shader_reports_why();
    test_linking_catches_a_missing_varying();
    test_attribute_locations();
    test_a_triangle_reaches_the_surface();
    test_interleaved_attributes_are_unpacked();
    test_a_zero_stride_means_tightly_packed();
    test_the_attribute_captures_its_buffer();
    test_normalized_byte_attributes();
    test_uniforms();
    test_draw_elements();
    test_clear();
    test_the_viewport_origin_is_flipped();
    test_error_reporting();
    test_unsupported_modes_are_refused();
    test_delete_unbinds();
    test_texture_sampling();
    test_a_page_can_draw_a_triangle();
    test_a_program_can_be_enumerated();
    test_webgl2_is_null_not_a_throw();
    REPORT("webgl_basics");
}
