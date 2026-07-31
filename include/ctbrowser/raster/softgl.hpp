#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/glsl.hpp>

// The software rasteriser: triangles, in software, through the GLSL evaluator.
//
// Stage four of docs/webgl-plan.md, and the REFERENCE back end. It is not a
// placeholder for the GPU one - it is what a golden means, what runs where there
// is no adapter, and what the GPU path is compared against. See the plan for why
// those are three reasons rather than one.
//
// WHAT THIS IS NOT. It is not a renderer for the page: the 2D canvas has its own
// (shell/canvas.hpp) and is much better at rectangles and text. This draws
// TRIANGLES with a program bound, which is the only thing WebGL can do and the
// thing nothing else here can.
//
// The pipeline is the one the specification describes, in the order it describes
// it, because the order is observable:
//
//   vertex shader -> clip -> divide by w -> viewport -> cull -> rasterise
//   -> interpolate -> fragment shader -> depth test -> blend -> write

namespace ctbrowser::raster {

// The blend equations, matching shell/composite.hpp's Porter-Duff coefficients
// but named the way GL names them. A caller sets a factor per side.
enum class blend_factor : std::uint8_t {
    zero,
    one,
    src_color,
    one_minus_src_color,
    dst_color,
    one_minus_dst_color,
    src_alpha,
    one_minus_src_alpha,
    dst_alpha,
    one_minus_dst_alpha,
};

enum class depth_test : std::uint8_t {
    never,
    less,
    equal,
    less_equal,
    greater,
    not_equal,
    greater_equal,
    always
};

enum class cull_mode : std::uint8_t {
    none,
    front,
    back
};

// One draw call's state. Everything the WebGL context has bound at the moment
// `drawArrays` is called, flattened into what the rasteriser needs - so this
// file has no opinion about how a context stores it.
struct draw_state {
    int viewport_x = 0;
    int viewport_y = 0;
    int viewport_width = 0;
    int viewport_height = 0;

    bool depth_enabled = false;
    bool depth_write = true;
    depth_test depth = depth_test::less;

    bool blend_enabled = false;
    blend_factor source = blend_factor::one;
    blend_factor destination = blend_factor::zero;

    cull_mode cull = cull_mode::none;
    // COUNTER-CLOCKWISE IS THE FRONT by default, which is GL's rule and the
    // opposite of what a reader who has done Direct3D expects.
    bool front_face_is_counter_clockwise = true;

    bool scissor_enabled = false;
    int scissor_x = 0;
    int scissor_y = 0;
    int scissor_width = 0;
    int scissor_height = 0;
};

// One vertex's attributes, by name, as the vertex shader will read them. The
// context fills this from the bound buffers; the rasteriser never sees a buffer.
using attribute_set = std::vector<std::pair<std::string, glsl::value>>;

// The target: colour, and a depth buffer that is allocated only when something
// asks for depth testing.
struct framebuffer {
    paint::bitmap * colour = nullptr;
    std::vector<float> depth;

    void ensure_depth(int width, int height) {
        const auto want = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        if (depth.size() != want) {
            // 1.0 is the far plane: `depthFunc(LESS)` against a cleared buffer
            // has to let the first fragment through.
            depth.assign(want, 1.0f);
        }
    }
};

// Draw one triangle list.
//
// `vertices` is a flat list; every three make a triangle, and a count that is not
// a multiple of three drops the remainder, which is what GL does. `uniforms` and
// `sample` are handed to both shader stages unchanged.
//
// Returns the number of fragments actually written, which is what the tests
// assert on when they want to know whether anything happened at all.
struct draw_request {
    const glsl::shader * vertex_shader = nullptr;
    const glsl::shader * fragment_shader = nullptr;
    // PREPARED, so the per-fragment cost is running `main` rather than
    // rebuilding the shader's function table and globals every time. Optional:
    // with neither set the modules above are prepared here, once per draw, which
    // is what a test that only wants pixels can rely on.
    const glsl::program * vertex_program = nullptr;
    const glsl::program * fragment_program = nullptr;
    const std::vector<attribute_set> * vertices = nullptr;
    std::function<const glsl::value *(std::string_view)> uniform;
    std::function<glsl::value(int unit, float s, float t)> sample;
    draw_state state;

    // WHERE A SHADER THAT FAILS AT RUN TIME GETS REPORTED, and it needs
    // somewhere to go. A vertex shader whose `main` errors used to drop its
    // triangle and a fragment shader's error used to drop its pixel, both in
    // total silence - so a shader this evaluator cannot run produced an empty
    // canvas indistinguishable from geometry that legitimately missed.
    //
    // A REAL DRIVER HAS NO EQUIVALENT, because a compiled GPU shader cannot
    // fail per fragment. This evaluator can, so the condition is this engine's
    // to report rather than something to model on GL.
    //
    // Optional: null means the caller does not want it, which is what the tests
    // that only care about pixels pass.
    std::string * shader_error = nullptr;
};

[[nodiscard]] std::size_t draw_triangles(const draw_request & request, framebuffer & into);

// NOT IMPLEMENTED, named rather than discovered:
//
//   * points and lines - `drawArrays(gl.POINTS)` draws nothing. p5 draws its
//     strokes as triangles, so this costs it nothing.
//   * multisampling: a fragment is in or out, decided at the pixel centre
//   * `gl_FragDepth`: a fragment's depth is the interpolated one
//   * clipping against the near plane is done by REJECTING a triangle with any
//     vertex behind the eye rather than by splitting it, so a triangle that
//     straddles the camera vanishes instead of being cut. Splitting is the right
//     fix and belongs with the first scene that needs it.

} // namespace ctbrowser::raster
