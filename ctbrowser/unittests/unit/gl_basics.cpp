//===- ctbrowser/unittests/unit/gl_basics.cpp - GLES device smoke test ----===//
//
// Constructs a GLES device with each driver (`fastest` and `deterministic`),
// prints ok() and renderer() for both side by side, then checks that the
// deterministic (SwiftShader) device clears, draws a triangle and depth-tests.
// Background: docs/history/webgl-rewrite.md.
//
// Optional, like svg_basics without plutosvg: a checkout that has not run
// tools/fetch-angle.sh builds and passes, and prints SKIP to say it did not
// look. What it must not do is pass in silence.
//
//===----------------------------------------------------------------------===//

#include <cstdio>

#include <ctbrowser/paint/command.hpp>
#include <ctbrowser/raster/gl.hpp>

#include "check.hpp"

using namespace ctbrowser;

namespace {

/// What one driver reported when a device was constructed with it.
struct outcome {
    bool ok = false;
    bool webgl = false;
    std::string renderer;
};

/// Constructs a 64x48 device with \p which and prints its result under \p name.
[[nodiscard]] outcome try_driver(raster::gl::driver which, const char * name) {
    raster::gl::device device{64, 48, which};
    outcome out;
    out.ok = device.ok();
    out.webgl = device.webgl_compatible();
    out.renderer = device.renderer();
    // Print both drivers, always, side by side. A run that prints only the
    // failure leaves the reader guessing whether the other one was even tried.
    std::printf("     %-14s ok=%s  webgl=%s  %s\n", name, out.ok ? "yes" : "NO ",
                out.webgl ? "yes" : "NO ", out.ok ? out.renderer.c_str() : device.error().c_str());
    return out;
}

} // namespace

int main() {
    if (!raster::gl::available()) {
        std::printf("SKIP gl_basics: %s\n", raster::gl::unavailable_because().c_str());
        return 0;
    }

    const outcome fastest = try_driver(raster::gl::driver::fastest, "fastest");
    const outcome deterministic = try_driver(raster::gl::driver::deterministic, "deterministic");

    // SwiftShader is the floor. It is software, it needs no GPU, and the
    // byte-compared goldens depend on it, so a build with ANGLE where this
    // fails is broken however well the other driver does.
    CHECK(deterministic.ok);
    if (deterministic.ok) {
        CHECK(deterministic.renderer.find("SwiftShader") != std::string::npos);
    }

    // The context must be in WebGL compatibility mode. Without it ANGLE skips
    // the validation that turns a page's mistake into INVALID_OPERATION, and,
    // for example, a uniform buffer with no storage reaches the Vulkan back end
    // as a null dereference. Asserted, not just reported, because a context
    // without it crashes on the wrong page.
    CHECK(deterministic.webgl);

    // `fastest` may legitimately fail: a headless box with no GPU has nothing
    // for it to pick. That is why it is reported rather than asserted - and why
    // the engine must fall back rather than hand a page a null context.
    if (!fastest.ok) {
        std::printf("     note: fastest did not come up, so the fallback to "
                    "deterministic is what a page will get\n");
    }

    // --- The device clears ------------------------------------------------------
    {
        raster::gl::device device{64, 48, raster::gl::driver::deterministic};
        CHECK(device.ok());
        paint::bitmap into;
        device.clear(1.0f, 0.0f, 0.0f, 1.0f);
        CHECK(device.read_pixels(into));
        CHECK(into.width == 64);
        const std::uint32_t pixel = into.at(32, 24);
        // ARGB, not GL's byte order. A red clear coming back blue is the one
        // bug this check exists for.
        CHECK(((pixel >> 16) & 0xFF) == 255);
        CHECK(((pixel >> 8) & 0xFF) == 0);
        CHECK((pixel & 0xFF) == 0);
        if (((pixel >> 16) & 0xFF) != 255) { std::printf("     clear came back %08x\n", pixel); }
    }

    // --- The device draws -------------------------------------------------------
    //
    // Draw a green triangle over a red clear and read a corner pixel (the clear)
    // and the centre pixel (the geometry) separately, at device level with no
    // page or JavaScript involved. When a WebGL page stops painting, this shows
    // whether ANGLE draws at all or the engine loses the draw on the way to the
    // canvas. There is no software rasteriser to diff against, so the pixel
    // assertions are the only evidence.
    {
        // GL's numbers, spelled out. `raster::gl::device` takes plain ints on
        // purpose - the values a page passes arrive from JavaScript already as
        // integers - so a caller needs the constants and no GLES header may
        // appear outside lib/Raster/gl/.
        constexpr int gl_vertex_shader = 0x8B31;
        constexpr int gl_fragment_shader = 0x8B30;
        constexpr int gl_array_buffer = 0x8892;
        constexpr int gl_static_draw = 0x88E4;
        constexpr int gl_float = 0x1406;
        constexpr int gl_triangles = 0x0004;

        raster::gl::device device{64, 48, raster::gl::driver::deterministic};
        CHECK(device.ok());

        // ES 3.00, because the context is ES 3.1 and `#version` must be the
        // first thing in the string. The page corpora supply their own; this
        // one is ours and says so.
        const unsigned vertex = device.create_shader(gl_vertex_shader);
        device.shader_source(vertex, "#version 300 es\n"
                                     "in vec2 xy;\n"
                                     "void main() { gl_Position = vec4(xy, 0.0, 1.0); }\n");
        device.compile_shader(vertex);
        CHECK(device.shader_compiled(vertex));
        if (!device.shader_compiled(vertex)) {
            std::printf("     vertex shader: %s\n", device.shader_log(vertex).c_str());
        }

        const unsigned fragment = device.create_shader(gl_fragment_shader);
        device.shader_source(fragment, "#version 300 es\n"
                                       "precision mediump float;\n"
                                       "out vec4 colour;\n"
                                       "void main() { colour = vec4(0.0, 1.0, 0.0, 1.0); }\n");
        device.compile_shader(fragment);
        CHECK(device.shader_compiled(fragment));
        if (!device.shader_compiled(fragment)) {
            std::printf("     fragment shader: %s\n", device.shader_log(fragment).c_str());
        }

        const unsigned program = device.create_program();
        device.attach_shader(program, vertex);
        device.attach_shader(program, fragment);
        device.link_program(program);
        CHECK(device.program_linked(program));
        if (!device.program_linked(program)) {
            std::printf("     link: %s\n", device.program_log(program).c_str());
        }
        device.use_program(program);

        // A triangle that covers the centre and leaves every corner alone, so
        // the two pixels below answer different questions.
        const float xy[] = {-0.9F, -0.9F, 0.9F, -0.9F, 0.0F, 0.9F};
        const unsigned buffer = device.create_buffer();
        device.bind_buffer(gl_array_buffer, buffer);
        device.buffer_data(gl_array_buffer, xy, sizeof(xy), gl_static_draw);

        // The location is asked of the linked program. A location the engine
        // invents agrees with the shader only by coincidence.
        const int location = device.attribute_location(program, "xy");
        CHECK(location >= 0);
        if (location >= 0) {
            const auto at = static_cast<unsigned>(location);
            device.enable_attribute(at, true);
            device.attribute_pointer(at, 2, gl_float, false, 0, 0);
        }

        // No glViewport: the engine never calls one either, and GL's default is
        // the pbuffer size. If that ever stops being true this test says so
        // before a page does.
        device.clear(1.0F, 0.0F, 0.0F, 1.0F);
        device.draw_arrays(gl_triangles, 0, 3);

        // Nothing may have gone wrong silently. A draw that GL rejected returns
        // normally and paints nothing, which is the failure mode this file
        // exists to catch, so the error queue is part of the assertion, not a
        // diagnostic beside it.
        const std::uint32_t error = device.take_error();
        CHECK(error == 0);
        if (error != 0) { std::printf("     GL error after the draw: 0x%04X\n", error); }

        paint::bitmap into;
        CHECK(device.read_pixels(into));

        const std::uint32_t corner = into.at(1, 1);
        const std::uint32_t centre = into.at(32, 24);

        // The clear reached the pixels.
        CHECK(((corner >> 16) & 0xFF) == 255);
        CHECK(((corner >> 8) & 0xFF) == 0);

        // And so did the geometry: green where the triangle is, not the clear
        // colour.
        CHECK(centre != corner);
        CHECK(((centre >> 8) & 0xFF) == 255);
        CHECK(((centre >> 16) & 0xFF) == 0);
        if (centre == corner) {
            std::printf("     the device cleared but drew nothing: corner=%08x centre=%08x\n",
                        corner, centre);
        }
    }

    // --- The device honours depth -----------------------------------------------
    //
    // The triangle above proves a draw lands but says nothing about depth. This
    // separates "the device cannot depth-test" from "the engine never enabled
    // it", which are bugs in different files.
    //
    // Draw order matters: near first, then far. Without a working depth test
    // the far triangle, drawn second, paints over the near one, so this order
    // fails unless depth testing rejects it. Far-then-near would pass by
    // accident.
    {
        constexpr int gl_vertex_shader = 0x8B31;
        constexpr int gl_fragment_shader = 0x8B30;
        constexpr int gl_array_buffer = 0x8892;
        constexpr int gl_static_draw = 0x88E4;
        constexpr int gl_float = 0x1406;
        constexpr int gl_triangles = 0x0004;
        constexpr int gl_depth_test = 0x0B71;
        constexpr int gl_less = 0x0201;

        raster::gl::device device{64, 48, raster::gl::driver::deterministic};
        CHECK(device.ok());

        const unsigned vertex = device.create_shader(gl_vertex_shader);
        device.shader_source(vertex, "#version 300 es\n"
                                     "in vec3 xyz;\n"
                                     "void main() { gl_Position = vec4(xyz, 1.0); }\n");
        device.compile_shader(vertex);
        const unsigned fragment = device.create_shader(gl_fragment_shader);
        device.shader_source(fragment, "#version 300 es\n"
                                       "precision mediump float;\n"
                                       "uniform vec4 tint;\n"
                                       "out vec4 colour;\n"
                                       "void main() { colour = tint; }\n");
        device.compile_shader(fragment);
        const unsigned program = device.create_program();
        device.attach_shader(program, vertex);
        device.attach_shader(program, fragment);
        device.link_program(program);
        CHECK(device.program_linked(program));
        device.use_program(program);

        const int tint = device.uniform_location(program, "tint");
        const int location = device.attribute_location(program, "xyz");
        CHECK(location >= 0);
        const auto at = static_cast<unsigned>(location);

        const unsigned buffer = device.create_buffer();
        device.bind_buffer(gl_array_buffer, buffer);
        device.enable_attribute(at, true);

        const auto triangle_at = [&](float z) {
            const float xyz[] = {-0.9F, -0.9F, z, 0.9F, -0.9F, z, 0.0F, 0.9F, z};
            device.buffer_data(gl_array_buffer, xyz, sizeof(xyz), gl_static_draw);
            device.attribute_pointer(at, 3, gl_float, false, 0, 0);
            device.draw_arrays(gl_triangles, 0, 3);
        };
        const auto green = [&] {
            const float c[] = {0.0F, 1.0F, 0.0F, 1.0F};
            device.set_uniform(tint, c, 1, 4, 1, false);
        };
        const auto blue = [&] {
            const float c[] = {0.0F, 0.0F, 1.0F, 1.0F};
            device.set_uniform(tint, c, 1, 4, 1, false);
        };

        device.set_capability(gl_depth_test, true);
        device.depth_func(gl_less);
        device.depth_mask(true);

        // Near first, then far. The far triangle must be rejected; if depth is
        // not working it paints over and the centre comes back green.
        device.clear(1.0F, 0.0F, 0.0F, 1.0F);
        blue();
        triangle_at(-0.5F);
        green();
        triangle_at(0.5F);
        CHECK(device.take_error() == 0);

        paint::bitmap into;
        CHECK(device.read_pixels(into));
        const std::uint32_t centre = into.at(32, 24);
        const bool is_blue = (centre & 0xFF) == 255 && ((centre >> 8) & 0xFF) == 0;
        CHECK(is_blue);
        if (!is_blue) {
            std::printf("     the far triangle was NOT rejected - centre=%08x, so this device is "
                        "not depth-testing\n",
                        centre);
        }
    }

    REPORT("gl_basics");
}
