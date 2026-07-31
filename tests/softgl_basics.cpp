// The software rasteriser: triangles, through the GLSL evaluator, into pixels.
//
// Stage four of docs/webgl-plan.md and the REFERENCE back end - what a golden
// means, what runs where there is no adapter, and what the GPU path is compared
// against.
//
// The tests below are chosen for the rules that look ALMOST RIGHT when they are
// wrong, because those are the ones a picture does not reveal:
//
//   * perspective correction - linear interpolation looks fine on a quad facing
//     the camera and bends a texture along each triangle's diagonal otherwise
//   * the fill rule - a shared edge either double-draws or drops a line
//   * winding and culling - getting the sign backwards culls exactly the
//     triangles you meant to keep, which looks like nothing rendering
//   * the Y flip - GL puts +1 at the top and a bitmap puts row 0 there

#include <ctbrowser/raster/raster.hpp>

#include "check.hpp"

#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

using namespace ctbrowser;
using namespace ctbrowser::raster;

namespace {

// A vertex shader that passes its position straight through, so a test can put
// a triangle exactly where it wants it in normalised device coordinates.
constexpr const char * passthrough_vertex = R"(
    attribute vec4 aPosition;
    attribute vec4 aColor;
    varying vec4 vColor;
    void main() {
      vColor = aColor;
      gl_Position = aPosition;
    }
)";

constexpr const char * colour_fragment = R"(
    varying vec4 vColor;
    void main() { gl_FragColor = vColor; }
)";

[[nodiscard]] glsl::shader compile(const char * source, glsl::stage which) {
    glsl::options how;
    how.which = which;
    glsl::shader m = glsl::parse(source, how);
    if (!m.ok) {
        std::printf("FAIL a test shader did not compile:\n%s", m.info_log().c_str());
        ++ctbrowser_test_failures;
    }
    return m;
}

[[nodiscard]] attribute_set vertex(float x, float y, float z, float w, float r, float g, float b,
                                   float a) {
    glsl::value position;
    position.t = glsl::type{glsl::base::f, 4, 1, -1, 0};
    position.v = {x, y, z, w};
    glsl::value colour;
    colour.t = glsl::type{glsl::base::f, 4, 1, -1, 0};
    colour.v = {r, g, b, a};
    return {{"aPosition", position}, {"aColor", colour}};
}

struct scene {
    paint::bitmap surface;
    framebuffer target;
    glsl::shader vertex_shader;
    glsl::shader fragment_shader;
    std::vector<attribute_set> vertices;
    draw_state state;

    explicit scene(int size, const char * fragment = colour_fragment) {
        surface = paint::bitmap{size, size};
        target.colour = &surface;
        vertex_shader = compile(passthrough_vertex, glsl::stage::vertex);
        fragment_shader = compile(fragment, glsl::stage::fragment);
        state.viewport_width = size;
        state.viewport_height = size;
    }

    [[nodiscard]] std::size_t draw() {
        draw_request request;
        request.vertex_shader = &vertex_shader;
        request.fragment_shader = &fragment_shader;
        request.vertices = &vertices;
        request.state = state;
        request.uniform = [this](std::string_view name) -> const glsl::value * {
            const auto found = uniforms.find(std::string{name});
            return found == uniforms.end() ? nullptr : &found->second;
        };
        return draw_triangles(request, target);
    }

    [[nodiscard]] std::uint32_t at(int x, int y) const { return surface.at(x, y); }
    [[nodiscard]] int red(int x, int y) const {
        return static_cast<int>((surface.at(x, y) >> 16) & 0xFF);
    }
    [[nodiscard]] int green(int x, int y) const {
        return static_cast<int>((surface.at(x, y) >> 8) & 0xFF);
    }

    std::unordered_map<std::string, glsl::value> uniforms;
};

// A triangle covering the whole viewport, so a test that cares about one pixel
// does not also have to care about coverage.
void cover(scene & s, float r, float g, float b, float a = 1.0f, float z = 0.0f) {
    s.vertices.push_back(vertex(-3, -3, z, 1, r, g, b, a));
    s.vertices.push_back(vertex(3, -3, z, 1, r, g, b, a));
    s.vertices.push_back(vertex(0, 3, z, 1, r, g, b, a));
}

// --- the basics ------------------------------------------------------------

void test_a_triangle_appears() {
    scene s{32};
    cover(s, 1.0f, 0.0f, 0.0f);
    const std::size_t written = s.draw();
    CHECK(written > 0);
    // The centre is red, and it went through the shader rather than a memset:
    // the colour arrived as a varying the vertex shader wrote.
    CHECK(s.red(16, 16) == 255);
    CHECK(s.green(16, 16) == 0);
}

// GL PUTS +1 AT THE TOP; a bitmap puts row 0 there. Getting the flip wrong
// draws the scene upside down, which on a symmetric test looks perfect.
void test_the_y_axis_is_flipped() {
    scene s{32};
    // A triangle filling only the TOP half in GL terms: y from 0 to +1.
    s.vertices.push_back(vertex(-1, 0, 0, 1, 0, 1, 0, 1));
    s.vertices.push_back(vertex(1, 0, 0, 1, 0, 1, 0, 1));
    s.vertices.push_back(vertex(0, 1, 0, 1, 0, 1, 0, 1));
    CHECK(s.draw() > 0);
    // ...lands in the top rows of the bitmap.
    CHECK(s.green(16, 4) == 255);
    CHECK(s.green(16, 28) == 0);
}

// A pixel belongs to exactly ONE of two triangles sharing an edge. Without a
// fill rule the seam either draws twice - visible with blending - or not at
// all, which is visible always.
void test_the_fill_rule_covers_a_shared_edge_once() {
    scene s{32};
    // Two triangles making a quad, sharing the diagonal.
    s.vertices.push_back(vertex(-1, -1, 0, 1, 0, 0, 0, 0.5f));
    s.vertices.push_back(vertex(1, -1, 0, 1, 0, 0, 0, 0.5f));
    s.vertices.push_back(vertex(-1, 1, 0, 1, 0, 0, 0, 0.5f));
    s.vertices.push_back(vertex(1, -1, 0, 1, 0, 0, 0, 0.5f));
    s.vertices.push_back(vertex(1, 1, 0, 1, 0, 0, 0, 0.5f));
    s.vertices.push_back(vertex(-1, 1, 0, 1, 0, 0, 0, 0.5f));
    const std::size_t written = s.draw();
    // A 32x32 quad is 1024 pixels. Drawing the seam twice would exceed it;
    // dropping it would fall short. Exactly once is the whole point.
    CHECK(written == 1024);
}

// --- interpolation ---------------------------------------------------------

// A varying is interpolated across the triangle, which is what makes a gradient
// and what carries a texture coordinate.
void test_varyings_interpolate() {
    scene s{32};
    // Red at the left, green at the right, over a full-viewport triangle.
    s.vertices.push_back(vertex(-3, -3, 0, 1, 1, 0, 0, 1));
    s.vertices.push_back(vertex(3, -3, 0, 1, 0, 1, 0, 1));
    s.vertices.push_back(vertex(0, 3, 0, 1, 0.5f, 0.5f, 0, 1));
    CHECK(s.draw() > 0);
    // Left of centre is redder than right of centre, and the middle is between.
    CHECK(s.red(4, 20) > s.red(28, 20));
    CHECK(s.green(28, 20) > s.green(4, 20));
}

// PERSPECTIVE CORRECTION, and it gets its own test because linear interpolation
// looks fine on a quad facing the camera and wrong on everything else.
//
// Two vertices with very different w: interpolating attr/w against 1/w and
// dividing at the end is NOT the same as interpolating attr directly, and the
// midpoint is where they differ most.
void test_perspective_correct_interpolation() {
    scene s{64};
    // A triangle whose right edge is four times further away. In screen space
    // the midpoint is halfway; in the scene it is much nearer the far end, so a
    // varying that runs 0..1 reads well BELOW 0.5 at the middle pixel.
    s.vertices.push_back(vertex(-1, -1, 0, 1, 0, 0, 0, 1));
    s.vertices.push_back(vertex(4, -4, 0, 4, 1, 0, 0, 1));
    s.vertices.push_back(vertex(-1, 3, 0, 1, 0, 0, 0, 1));
    CHECK(s.draw() > 0);

    const int middle = s.red(32, 40);
    // Screen-linear would put the midpoint at about half. Perspective-correct
    // puts it at 1/(1+w) weighting - decidedly less. The exact number depends on
    // where the triangle lands; what cannot be true is "about half".
    CHECK(middle < 110);
    // And it is not zero either, so this is measuring interpolation rather than
    // a triangle that failed to draw.
    CHECK(middle > 0);
    std::printf("     perspective midpoint = %d/255 (screen-linear would be ~128)\n", middle);
}

// --- depth -----------------------------------------------------------------

void test_depth_test_resolves_a_z_fight() {
    scene s{16};
    s.state.depth_enabled = true;
    s.state.depth = depth_test::less;
    // Far first, then near. The near one wins.
    cover(s, 1, 0, 0, 1, 0.8f);
    cover(s, 0, 1, 0, 1, 0.2f);
    CHECK(s.draw() > 0);
    CHECK(s.green(8, 8) == 255);
    CHECK(s.red(8, 8) == 0);

    // The other order, which without a depth buffer would give the opposite
    // answer - that is what makes this a test of depth rather than of order.
    scene reversed{16};
    reversed.state.depth_enabled = true;
    reversed.state.depth = depth_test::less;
    cover(reversed, 0, 1, 0, 1, 0.2f); // near first
    cover(reversed, 1, 0, 0, 1, 0.8f); // far second, must be rejected
    CHECK(reversed.draw() > 0);
    CHECK(reversed.green(8, 8) == 255);
}

void test_depth_write_can_be_turned_off() {
    scene s{16};
    s.state.depth_enabled = true;
    s.state.depth = depth_test::less;
    s.state.depth_write = false;
    cover(s, 1, 0, 0, 1, 0.2f); // near, but does not record its depth
    cover(s, 0, 1, 0, 1, 0.8f); // far - and passes, because nothing was written
    CHECK(s.draw() > 0);
    CHECK(s.green(8, 8) == 255);
}

// --- culling ---------------------------------------------------------------

// Getting the winding sign backwards culls exactly the triangles you meant to
// keep, which looks like nothing rendering at all.
void test_back_face_culling() {
    scene s{16};
    s.state.cull = cull_mode::back;
    // Counter-clockwise in GL's terms - the front face by default.
    s.vertices.push_back(vertex(-1, -1, 0, 1, 1, 0, 0, 1));
    s.vertices.push_back(vertex(3, -1, 0, 1, 1, 0, 0, 1));
    s.vertices.push_back(vertex(-1, 3, 0, 1, 1, 0, 0, 1));
    CHECK(s.draw() > 0);
    CHECK(s.red(4, 12) == 255);

    // The same triangle wound the other way is a back face and disappears.
    scene flipped{16};
    flipped.state.cull = cull_mode::back;
    flipped.vertices.push_back(vertex(-1, -1, 0, 1, 1, 0, 0, 1));
    flipped.vertices.push_back(vertex(-1, 3, 0, 1, 1, 0, 0, 1));
    flipped.vertices.push_back(vertex(3, -1, 0, 1, 1, 0, 0, 1));
    CHECK(flipped.draw() == 0);

    // And with culling off it comes back, so the test is about culling rather
    // than about the triangle being malformed.
    scene uncalled{16};
    uncalled.vertices.push_back(vertex(-1, -1, 0, 1, 1, 0, 0, 1));
    uncalled.vertices.push_back(vertex(-1, 3, 0, 1, 1, 0, 0, 1));
    uncalled.vertices.push_back(vertex(3, -1, 0, 1, 1, 0, 0, 1));
    CHECK(uncalled.draw() > 0);
}

// --- blending and scissor --------------------------------------------------

void test_blending() {
    scene s{16};
    cover(s, 1, 0, 0, 1); // opaque red first
    CHECK(s.draw() > 0);
    CHECK(s.red(8, 8) == 255);

    // Half-transparent green over it, source-alpha blended.
    s.vertices.clear();
    s.state.blend_enabled = true;
    s.state.source = blend_factor::src_alpha;
    s.state.destination = blend_factor::one_minus_src_alpha;
    cover(s, 0, 1, 0, 0.5f);
    CHECK(s.draw() > 0);
    // Half of each, within a rounding step.
    CHECK(s.red(8, 8) >= 127 && s.red(8, 8) <= 128);
    CHECK(s.green(8, 8) >= 127 && s.green(8, 8) <= 128);
}

void test_scissor() {
    scene s{32};
    s.state.scissor_enabled = true;
    s.state.scissor_x = 0;
    s.state.scissor_y = 0;
    s.state.scissor_width = 16;
    s.state.scissor_height = 16;
    cover(s, 1, 0, 0);
    CHECK(s.draw() > 0);
    CHECK(s.red(4, 4) == 255); // inside
    CHECK(s.red(24, 24) == 0); // outside, untouched
}

void test_viewport() {
    scene s{32};
    // Draw into the bottom-right quadrant only.
    s.state.viewport_x = 16;
    s.state.viewport_y = 16;
    s.state.viewport_width = 16;
    s.state.viewport_height = 16;
    cover(s, 1, 0, 0);
    CHECK(s.draw() > 0);
    CHECK(s.red(24, 24) == 255);
    CHECK(s.red(4, 4) == 0);
}

// --- discard ---------------------------------------------------------------

// A discarded fragment writes NOTHING - and that includes the depth buffer, or
// it would occlude whatever comes after it while being invisible.
void test_discard_writes_nothing() {
    scene s{16, R"(
        varying vec4 vColor;
        void main() {
          if (vColor.r > 0.5) { discard; }
          gl_FragColor = vColor;
        }
    )"};
    s.state.depth_enabled = true;
    cover(s, 1, 0, 0, 1, 0.2f); // red, and the shader discards it
    CHECK(s.draw() == 0);
    CHECK(s.at(8, 8) == 0);

    // Something behind it still draws, which is what proves the depth buffer
    // was not written by the discarded fragment.
    s.vertices.clear();
    cover(s, 0, 1, 0, 1, 0.8f);
    CHECK(s.draw() > 0);
    CHECK(s.green(8, 8) == 255);
}

// --- the things that must not crash ----------------------------------------

// A draw call is driven by a page, so every degenerate one has to come back
// rather than fault.
void test_degenerate_draws_are_harmless() {
    scene s{16};
    // No vertices.
    CHECK(s.draw() == 0);
    // A count that is not a multiple of three drops the remainder, as GL does.
    s.vertices.push_back(vertex(-1, -1, 0, 1, 1, 0, 0, 1));
    s.vertices.push_back(vertex(1, -1, 0, 1, 1, 0, 0, 1));
    CHECK(s.draw() == 0);
    // A degenerate triangle - all three vertices identical - has no area.
    s.vertices.clear();
    for (int i = 0; i < 3; ++i) { s.vertices.push_back(vertex(0, 0, 0, 1, 1, 0, 0, 1)); }
    CHECK(s.draw() == 0);
    // A triangle entirely off screen.
    s.vertices.clear();
    s.vertices.push_back(vertex(-9, -9, 0, 1, 1, 0, 0, 1));
    s.vertices.push_back(vertex(-8, -9, 0, 1, 1, 0, 0, 1));
    s.vertices.push_back(vertex(-9, -8, 0, 1, 1, 0, 0, 1));
    CHECK(s.draw() == 0);
    // w <= 0 - a vertex at or behind the eye. Dropped rather than divided by,
    // which would mirror it through the origin and draw a wrong triangle.
    s.vertices.clear();
    s.vertices.push_back(vertex(-1, -1, 0, 1, 1, 0, 0, 1));
    s.vertices.push_back(vertex(1, -1, 0, 0, 1, 0, 0, 1));
    s.vertices.push_back(vertex(0, 1, 0, -1, 1, 0, 0, 1));
    CHECK(s.draw() == 0);
    // A zero-sized viewport.
    scene empty{16};
    empty.state.viewport_width = 0;
    empty.state.viewport_height = 0;
    cover(empty, 1, 0, 0);
    CHECK(empty.draw() == 0);
}

// A shader that fails at run time stops the fragment, not the process.
void test_a_failing_shader_draws_nothing() {
    scene s{16, R"(
        varying vec4 vColor;
        void main() { gl_FragColor = vec4(undeclaredThing); }
    )"};
    cover(s, 1, 0, 0);
    CHECK(s.draw() == 0);
    CHECK(s.at(8, 8) == 0);
}

} // namespace

int main() {
    test_a_triangle_appears();
    test_the_y_axis_is_flipped();
    test_the_fill_rule_covers_a_shared_edge_once();
    test_varyings_interpolate();
    test_perspective_correct_interpolation();
    test_depth_test_resolves_a_z_fight();
    test_depth_write_can_be_turned_off();
    test_back_face_culling();
    test_blending();
    test_scissor();
    test_viewport();
    test_discard_writes_nothing();
    test_degenerate_draws_are_harmless();
    test_a_failing_shader_draws_nothing();
    REPORT("softgl_basics");
}
