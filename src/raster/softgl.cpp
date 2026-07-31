#include <ctbrowser/raster/softgl.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>

// The software rasteriser. See softgl.hpp for what it is and is not.
//
// The two things here that look almost right when they are wrong:
//
//   * PERSPECTIVE CORRECTION. A varying is interpolated as attr/w against 1/w
//     and divided at the end, NOT linearly in screen space. Linear looks fine on
//     a quad facing the camera and wrong on everything else - a texture that
//     bends along the diagonal of each triangle, which reads as a texturing bug
//     rather than an interpolation one.
//   * THE FILL RULE. A pixel belongs to exactly one of two triangles sharing an
//     edge. Without a rule, a shared edge either double-draws (visible with
//     blending) or drops a line of pixels (visible always).

namespace ctbrowser::raster {
namespace {

struct shaded_vertex {
    glsl::value position; // clip space, from gl_Position
    std::vector<std::pair<std::string, glsl::value>> varyings;
    float x = 0; // window coordinates after the viewport transform
    float y = 0;
    float z = 0; // 0..1, what the depth buffer holds
    float inverse_w = 1;
};

// The environment a shader stage sees: its own inputs first, then the uniforms.
class stage_environment {
public:
    stage_environment(const draw_request & request) : request_(&request) {}

    void set(std::string name, glsl::value v) { locals_[std::move(name)] = std::move(v); }
    void clear() { locals_.clear(); }

    [[nodiscard]] glsl::environment as_environment() const {
        glsl::environment env;
        env.read = [this](std::string_view name) -> const glsl::value * {
            if (const auto found = locals_.find(std::string{name}); found != locals_.end()) {
                return &found->second;
            }
            return request_->uniform ? request_->uniform(name) : nullptr;
        };
        env.sample = request_->sample;
        return env;
    }

private:
    const draw_request * request_;
    std::unordered_map<std::string, glsl::value> locals_;
};

[[nodiscard]] float blend_scale(blend_factor which, float src, float src_alpha, float dst,
                                float dst_alpha) {
    switch (which) {
    case blend_factor::zero: return 0.0f;
    case blend_factor::one: return 1.0f;
    case blend_factor::src_color: return src;
    case blend_factor::one_minus_src_color: return 1.0f - src;
    case blend_factor::dst_color: return dst;
    case blend_factor::one_minus_dst_color: return 1.0f - dst;
    case blend_factor::src_alpha: return src_alpha;
    case blend_factor::one_minus_src_alpha: return 1.0f - src_alpha;
    case blend_factor::dst_alpha: return dst_alpha;
    case blend_factor::one_minus_dst_alpha: return 1.0f - dst_alpha;
    }
    return 1.0f;
}

[[nodiscard]] bool depth_passes(depth_test how, float incoming, float stored) {
    switch (how) {
    case depth_test::never: return false;
    case depth_test::less: return incoming < stored;
    case depth_test::equal: return incoming == stored;
    case depth_test::less_equal: return incoming <= stored;
    case depth_test::greater: return incoming > stored;
    case depth_test::not_equal: return incoming != stored;
    case depth_test::greater_equal: return incoming >= stored;
    case depth_test::always: return true;
    }
    return true;
}

// The signed area of the triangle in window coordinates, twice over. Its SIGN is
// the winding, which is what culling reads, and its magnitude is the denominator
// every barycentric coordinate shares.
[[nodiscard]] float doubled_area(const shaded_vertex & a, const shaded_vertex & b,
                                 const shaded_vertex & c) {
    return (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
}

// THE FILL RULE - top-left, the one GL and Direct3D both use.
//
// A pixel centre exactly on a shared edge belongs to exactly one of the two
// triangles: the one for which the edge is a top or a left edge. Without this a
// shared edge either draws twice - which shows up the moment blending is on - or
// not at all, which shows up always as a seam.
[[nodiscard]] bool top_left(float edge_x, float edge_y) {
    return edge_y > 0.0f || (edge_y == 0.0f && edge_x < 0.0f);
}

} // namespace

std::size_t draw_triangles(const draw_request & request, framebuffer & into) {
    if (request.vertex_shader == nullptr || request.fragment_shader == nullptr ||
        request.vertices == nullptr || into.colour == nullptr) {
        return 0;
    }
    if (!request.vertex_shader->ok || !request.fragment_shader->ok) { return 0; }

    const draw_state & state = request.state;
    paint::bitmap & target = *into.colour;
    const int width = target.width;
    const int height = target.height;
    if (width <= 0 || height <= 0) { return 0; }
    if (state.depth_enabled) { into.ensure_depth(width, height); }

    stage_environment stage{request};
    std::size_t written = 0;

    const std::size_t count = request.vertices->size();
    for (std::size_t first = 0; first + 2 < count; first += 3) {
        // --- the vertex shader, once per vertex
        shaded_vertex corner[3];
        bool usable = true;
        for (int i = 0; i < 3; ++i) {
            stage.clear();
            for (const auto & [name, held] :
                 (*request.vertices)[first + static_cast<std::size_t>(i)]) {
                stage.set(name, held);
            }
            const glsl::environment env = stage.as_environment();
            const glsl::execution ran = glsl::execute(*request.vertex_shader, env);
            if (!ran.ok) {
                usable = false;
                break;
            }
            const glsl::value * position = ran.find("gl_Position");
            if (position == nullptr) {
                usable = false;
                break;
            }
            corner[i].position = *position;
            for (const auto & [name, held] : ran.outputs) {
                if (name.rfind("gl_", 0) == 0) { continue; }
                corner[i].varyings.emplace_back(name, held);
            }
        }
        if (!usable) { continue; }

        // --- clip, then divide by w
        //
        // A triangle with any vertex at or behind the eye is DROPPED rather than
        // split. Splitting is the right answer and is named in the header as not
        // done; dividing by a w at or below zero would send the vertex to
        // infinity or mirror it through the origin, which draws a wrong triangle
        // rather than none.
        bool behind = false;
        for (const shaded_vertex & v : corner) {
            if (v.position.f(3) <= 0.0f) { behind = true; }
        }
        if (behind) { continue; }

        for (shaded_vertex & v : corner) {
            const float w = v.position.f(3);
            v.inverse_w = 1.0f / w;
            const float ndc_x = v.position.f(0) * v.inverse_w;
            const float ndc_y = v.position.f(1) * v.inverse_w;
            const float ndc_z = v.position.f(2) * v.inverse_w;
            // The viewport transform. Y IS FLIPPED: GL's normalised device
            // coordinates put +1 at the top and a bitmap puts row 0 there.
            v.x = static_cast<float>(state.viewport_x) +
                  (ndc_x + 1.0f) * 0.5f * static_cast<float>(state.viewport_width);
            v.y = static_cast<float>(state.viewport_y) +
                  (1.0f - ndc_y) * 0.5f * static_cast<float>(state.viewport_height);
            // Depth to 0..1, which is what the buffer holds and what GL's
            // default depth range means.
            v.z = (ndc_z + 1.0f) * 0.5f;
        }

        // --- cull
        const float area = doubled_area(corner[0], corner[1], corner[2]);
        if (area == 0.0f) { continue; } // degenerate: no pixels either way
        if (state.cull != cull_mode::none) {
            // Y WAS FLIPPED ABOVE, SO THE WINDING REVERSES. A triangle that is
            // counter-clockwise in NDC - GL's front face - has a NEGATIVE
            // doubled area in these window coordinates, because y grows
            // downwards here and upwards there.
            //
            // I wrote this comment the other way round first and the test caught
            // it immediately, which is the argument for testing culling with a
            // triangle whose winding you worked out by hand: getting the sign
            // backwards culls exactly the faces you meant to keep, and the
            // symptom is a black screen with nothing to say why.
            const bool counter_clockwise = area < 0.0f;
            const bool is_front = counter_clockwise == state.front_face_is_counter_clockwise;
            if ((state.cull == cull_mode::back && !is_front) ||
                (state.cull == cull_mode::front && is_front)) {
                continue;
            }
        }

        // --- the bounding box, clipped to the viewport and the scissor
        int left = static_cast<int>(std::floor(std::min({corner[0].x, corner[1].x, corner[2].x})));
        int right = static_cast<int>(std::ceil(std::max({corner[0].x, corner[1].x, corner[2].x})));
        int top = static_cast<int>(std::floor(std::min({corner[0].y, corner[1].y, corner[2].y})));
        int bottom = static_cast<int>(std::ceil(std::max({corner[0].y, corner[1].y, corner[2].y})));
        left = std::max({left, 0, state.viewport_x});
        top = std::max({top, 0, state.viewport_y});
        right = std::min({right, width - 1, state.viewport_x + state.viewport_width - 1});
        bottom = std::min({bottom, height - 1, state.viewport_y + state.viewport_height - 1});
        if (state.scissor_enabled) {
            left = std::max(left, state.scissor_x);
            top = std::max(top, state.scissor_y);
            right = std::min(right, state.scissor_x + state.scissor_width - 1);
            bottom = std::min(bottom, state.scissor_y + state.scissor_height - 1);
        }

        const float inverse_area = 1.0f / area;
        for (int y = top; y <= bottom; ++y) {
            for (int x = left; x <= right; ++x) {
                // The pixel CENTRE, which is what decides coverage.
                const float px = static_cast<float>(x) + 0.5f;
                const float py = static_cast<float>(y) + 0.5f;

                // Edge functions: the doubled area of the sub-triangle opposite
                // each vertex. All three same-signed means inside.
                const float w0 = (corner[2].x - corner[1].x) * (py - corner[1].y) -
                                 (corner[2].y - corner[1].y) * (px - corner[1].x);
                const float w1 = (corner[0].x - corner[2].x) * (py - corner[2].y) -
                                 (corner[0].y - corner[2].y) * (px - corner[2].x);
                const float w2 = (corner[1].x - corner[0].x) * (py - corner[0].y) -
                                 (corner[1].y - corner[0].y) * (px - corner[0].x);
                const bool positive = area > 0.0f;
                const auto inside = [&](float edge, float ex, float ey) {
                    if (edge == 0.0f) { return top_left(ex, ey); }
                    return positive ? edge > 0.0f : edge < 0.0f;
                };
                if (!inside(w0, corner[2].x - corner[1].x, corner[2].y - corner[1].y) ||
                    !inside(w1, corner[0].x - corner[2].x, corner[0].y - corner[2].y) ||
                    !inside(w2, corner[1].x - corner[0].x, corner[1].y - corner[0].y)) {
                    continue;
                }

                const float b0 = w0 * inverse_area;
                const float b1 = w1 * inverse_area;
                const float b2 = w2 * inverse_area;

                const float depth_here = b0 * corner[0].z + b1 * corner[1].z + b2 * corner[2].z;
                const auto pixel = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                   static_cast<std::size_t>(x);
                // EARLY DEPTH. The fragment shader is the expensive thing here,
                // so a fragment that cannot survive the test is not shaded. This
                // is only correct because `discard` cannot change the depth
                // value - it can only remove the fragment - so a shader that
                // discards is handled by not WRITING below, not by shading first.
                if (state.depth_enabled &&
                    !depth_passes(state.depth, depth_here, into.depth[pixel])) {
                    continue;
                }

                // PERSPECTIVE-CORRECT INTERPOLATION. Interpolate attr/w against
                // 1/w linearly and divide at the end. Interpolating the
                // attribute directly is linear in SCREEN space, which is right
                // only when w is constant across the triangle.
                const float inverse_w =
                    b0 * corner[0].inverse_w + b1 * corner[1].inverse_w + b2 * corner[2].inverse_w;
                const float w_here = inverse_w == 0.0f ? 0.0f : 1.0f / inverse_w;

                stage.clear();
                for (std::size_t v = 0; v < corner[0].varyings.size(); ++v) {
                    const glsl::value & a = corner[0].varyings[v].second;
                    glsl::value blended = a;
                    for (std::size_t component = 0; component < blended.v.size(); ++component) {
                        const auto pick = [&](int which) {
                            const auto & from = corner[which].varyings;
                            return v < from.size() && component < from[v].second.v.size()
                                       ? from[v].second.v[component] * corner[which].inverse_w
                                       : 0.0f;
                        };
                        blended.v[component] =
                            (b0 * pick(0) + b1 * pick(1) + b2 * pick(2)) * w_here;
                    }
                    stage.set(corner[0].varyings[v].first, blended);
                }
                // gl_FragCoord, which a shader reads to know where it is.
                glsl::value coord;
                coord.t = glsl::type{glsl::base::f, 4, 1, -1, 0};
                coord.v = {px, py, depth_here, inverse_w};
                stage.set("gl_FragCoord", coord);

                const glsl::environment env = stage.as_environment();
                const glsl::execution ran = glsl::execute(*request.fragment_shader, env);
                if (!ran.ok) { continue; }
                // DISCARD WRITES NOTHING, and that includes the depth buffer -
                // a discarded fragment must not occlude what comes after it.
                if (ran.discarded) { continue; }
                const glsl::value * colour = ran.find("gl_FragColor");
                if (colour == nullptr) { continue; }

                float r = colour->f(0);
                float g = colour->f(1);
                float b = colour->f(2);
                float a = colour->f(3);
                if (state.blend_enabled) {
                    const std::uint32_t under = target.at(x, y);
                    const float dr = static_cast<float>((under >> 16) & 0xFF) / 255.0f;
                    const float dg = static_cast<float>((under >> 8) & 0xFF) / 255.0f;
                    const float db = static_cast<float>(under & 0xFF) / 255.0f;
                    const float da = static_cast<float>((under >> 24) & 0xFF) / 255.0f;
                    const auto mix = [&](float source_channel, float destination_channel) {
                        return source_channel * blend_scale(state.source, source_channel, a,
                                                            destination_channel, da) +
                               destination_channel * blend_scale(state.destination, source_channel,
                                                                 a, destination_channel, da);
                    };
                    r = mix(r, dr);
                    g = mix(g, dg);
                    b = mix(b, db);
                    a = mix(a, da);
                }
                const auto byte = [](float channel) {
                    return static_cast<std::uint32_t>(std::clamp(channel, 0.0f, 1.0f) * 255.0f +
                                                      0.5f);
                };
                target.put(x, y, (byte(a) << 24) | (byte(r) << 16) | (byte(g) << 8) | byte(b));
                if (state.depth_enabled && state.depth_write) { into.depth[pixel] = depth_here; }
                ++written;
            }
        }
    }
    return written;
}

} // namespace ctbrowser::raster
