#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/paint/command.hpp>
#include <ctbrowser/raster/gl.hpp>

// A PAGE'S WebGL CALLS, TRANSLATED TO GLES. Nothing else.
//
// The context this replaces kept a software mirror of GL state and separately,
// by hand, forwarded some calls to ANGLE; nineteen methods never forwarded at
// all and no test could see it. See docs/plans/webgl-rewrite.md - this file
// exists to make that shape impossible rather than to make it less likely.
//
// THE RULE: a method here translates and returns. It does not remember anything
// GL can be asked for. Where WebGL needs identity that GL does not provide - the
// `WebGLBuffer` object a page holds onto - a handle table stores the identity
// and NEVER the behaviour. The moment it caches what a buffer contains or which
// program is bound, it is the mirror again.
//
// BUILT GROUP BY GROUP against the 64 methods `webgl_bindings.cpp` calls, which
// is the checklist in docs/plans/webgl-rewrite.md. A method that is not written
// yet is a COMPILE ERROR at the binding site - which is the whole point, and the
// opposite of the silent no-op this replaces.

namespace ctbrowser::shell {

// THE GL CONSTANTS, ONCE. Each row is what a page reads as `gl.NAME`, the C++
// name the engine uses for the same number, the number, and whether it is on
// every context (1) or only a WebGL 2 one (2). The rows are in the order the
// context object gets them, which is the order `Object.keys(gl)` reports.
//
// This is a TABLE, not an implementation - the numbers WebGL gives a page are
// fixed by the specification and identical in every engine. The list is an
// X-macro so the JavaScript constant and the `gl_enum::` name are the SAME
// row: they used to be two tables, 95 names here and 152 `constant()` calls in
// bindings/webgl/constants.cpp, some by name and some as raw hex free to drift.
//
// A WebGL 2 constant that arrives as `undefined` makes every comparison
// against it silently false, and a WebGL 1 page that finds `gl.RGBA8`
// concludes it has WebGL 2 - so the column is load-bearing both ways.
// TEXTURE_BINDING_3D in particular is the property Babylon tests to pick its
// GLSL dialect; missing, every shader was ES 3.00 source with no `#version`
// and the post-process quad never drew, with no GL error anywhere (Babylon
// ratchet rung 8).
#define CTBROWSER_GL_CONSTANTS(X)                                                                  \
    X(DEPTH_BUFFER_BIT, depth_buffer_bit, 0x0100, 1)                                               \
    X(STENCIL_BUFFER_BIT, stencil_buffer_bit, 0x0400, 1)                                           \
    X(COLOR_BUFFER_BIT, color_buffer_bit, 0x4000, 1)                                               \
    X(POINTS, points, 0x0000, 1)                                                                   \
    X(LINES, lines, 0x0001, 1)                                                                     \
    X(LINE_LOOP, line_loop, 0x0002, 1)                                                             \
    X(LINE_STRIP, line_strip, 0x0003, 1)                                                           \
    X(TRIANGLES, triangles, 0x0004, 1)                                                             \
    X(TRIANGLE_STRIP, triangle_strip, 0x0005, 1)                                                   \
    X(TRIANGLE_FAN, triangle_fan, 0x0006, 1)                                                       \
    X(DEPTH_TEST, depth_test, 0x0B71, 1)                                                           \
    X(BLEND, blend, 0x0BE2, 1)                                                                     \
    X(CULL_FACE, cull_face, 0x0B44, 1)                                                             \
    X(SCISSOR_TEST, scissor_test, 0x0C11, 1)                                                       \
    X(DITHER, dither, 0x0BD0, 1)                                                                   \
    X(STENCIL_TEST, stencil_test, 0x0B90, 1)                                                       \
    X(POLYGON_OFFSET_FILL, polygon_offset_fill, 0x8037, 1)                                         \
    X(SAMPLE_ALPHA_TO_COVERAGE, sample_alpha_to_coverage, 0x809E, 1)                               \
    X(SAMPLE_COVERAGE, sample_coverage, 0x80A0, 1)                                                 \
    X(FRONT, front, 0x0404, 1)                                                                     \
    X(BACK, back, 0x0405, 1)                                                                       \
    X(FRONT_AND_BACK, front_and_back, 0x0408, 1)                                                   \
    X(CW, cw, 0x0900, 1)                                                                           \
    X(CCW, ccw, 0x0901, 1)                                                                         \
    X(NEVER, never, 0x0200, 1)                                                                     \
    X(LESS, less, 0x0201, 1)                                                                       \
    X(EQUAL, equal, 0x0202, 1)                                                                     \
    X(LEQUAL, lequal, 0x0203, 1)                                                                   \
    X(GREATER, greater, 0x0204, 1)                                                                 \
    X(NOTEQUAL, notequal, 0x0205, 1)                                                               \
    X(GEQUAL, gequal, 0x0206, 1)                                                                   \
    X(ALWAYS, always, 0x0207, 1)                                                                   \
    X(KEEP, keep, 0x1E00, 1)                                                                       \
    X(REPLACE, replace, 0x1E01, 1)                                                                 \
    X(ZERO, zero, 0x0000, 1)                                                                       \
    X(ONE, one, 0x0001, 1)                                                                         \
    X(SRC_COLOR, src_color, 0x0300, 1)                                                             \
    X(ONE_MINUS_SRC_COLOR, one_minus_src_color, 0x0301, 1)                                         \
    X(SRC_ALPHA, src_alpha, 0x0302, 1)                                                             \
    X(ONE_MINUS_SRC_ALPHA, one_minus_src_alpha, 0x0303, 1)                                         \
    X(DST_ALPHA, dst_alpha, 0x0304, 1)                                                             \
    X(ONE_MINUS_DST_ALPHA, one_minus_dst_alpha, 0x0305, 1)                                         \
    X(DST_COLOR, dst_color, 0x0306, 1)                                                             \
    X(ONE_MINUS_DST_COLOR, one_minus_dst_color, 0x0307, 1)                                         \
    X(SRC_ALPHA_SATURATE, src_alpha_saturate, 0x0308, 1)                                           \
    X(FUNC_ADD, func_add, 0x8006, 1)                                                               \
    X(FUNC_SUBTRACT, func_subtract, 0x800A, 1)                                                     \
    X(FUNC_REVERSE_SUBTRACT, func_reverse_subtract, 0x800B, 1)                                     \
    X(BYTE, byte_, 0x1400, 1)                                                                      \
    X(UNSIGNED_BYTE, unsigned_byte, 0x1401, 1)                                                     \
    X(SHORT, short_, 0x1402, 1)                                                                    \
    X(UNSIGNED_SHORT, unsigned_short, 0x1403, 1)                                                   \
    X(INT, int_, 0x1404, 1)                                                                        \
    X(UNSIGNED_INT, unsigned_int, 0x1405, 1)                                                       \
    X(FLOAT, float_, 0x1406, 1)                                                                    \
    X(ARRAY_BUFFER, array_buffer, 0x8892, 1)                                                       \
    X(ELEMENT_ARRAY_BUFFER, element_array_buffer, 0x8893, 1)                                       \
    X(STATIC_DRAW, static_draw, 0x88E4, 1)                                                         \
    X(DYNAMIC_DRAW, dynamic_draw, 0x88E8, 1)                                                       \
    X(STREAM_DRAW, stream_draw, 0x88E0, 1)                                                         \
    X(TEXTURE_2D, texture_2d, 0x0DE1, 1)                                                           \
    X(TEXTURE_CUBE_MAP, texture_cube_map, 0x8513, 1)                                               \
    X(RGBA, rgba, 0x1908, 1)                                                                       \
    X(RGB, rgb, 0x1907, 1)                                                                         \
    X(LUMINANCE, luminance, 0x1909, 1)                                                             \
    X(LUMINANCE_ALPHA, luminance_alpha, 0x190A, 1)                                                 \
    X(ALPHA, alpha, 0x1906, 1)                                                                     \
    X(NEAREST, nearest, 0x2600, 1)                                                                 \
    X(LINEAR, linear, 0x2601, 1)                                                                   \
    X(NEAREST_MIPMAP_NEAREST, nearest_mipmap_nearest, 0x2700, 1)                                   \
    X(LINEAR_MIPMAP_NEAREST, linear_mipmap_nearest, 0x2701, 1)                                     \
    X(NEAREST_MIPMAP_LINEAR, nearest_mipmap_linear, 0x2702, 1)                                     \
    X(LINEAR_MIPMAP_LINEAR, linear_mipmap_linear, 0x2703, 1)                                       \
    X(TEXTURE_MAG_FILTER, texture_mag_filter, 0x2800, 1)                                           \
    X(TEXTURE_MIN_FILTER, texture_min_filter, 0x2801, 1)                                           \
    X(TEXTURE_WRAP_S, texture_wrap_s, 0x2802, 1)                                                   \
    X(TEXTURE_WRAP_T, texture_wrap_t, 0x2803, 1)                                                   \
    X(CLAMP_TO_EDGE, clamp_to_edge, 0x812F, 1)                                                     \
    X(REPEAT, repeat, 0x2901, 1)                                                                   \
    X(MIRRORED_REPEAT, mirrored_repeat, 0x8370, 1)                                                 \
    X(UNPACK_FLIP_Y_WEBGL, unpack_flip_y_webgl, 0x9240, 1)                                         \
    X(UNPACK_PREMULTIPLY_ALPHA_WEBGL, unpack_premultiply_alpha_webgl, 0x9241, 1)                   \
    X(UNPACK_ALIGNMENT, unpack_alignment, 0x0CF5, 1)                                               \
    X(COMPILE_STATUS, compile_status, 0x8B81, 1)                                                   \
    X(LINK_STATUS, link_status, 0x8B82, 1)                                                         \
    X(VALIDATE_STATUS, validate_status, 0x8B83, 1)                                                 \
    X(DELETE_STATUS, delete_status, 0x8B80, 1)                                                     \
    X(VERTEX_SHADER, vertex_shader, 0x8B31, 1)                                                     \
    X(FRAGMENT_SHADER, fragment_shader, 0x8B30, 1)                                                 \
    X(ACTIVE_UNIFORMS, active_uniforms, 0x8B86, 1)                                                 \
    X(ACTIVE_ATTRIBUTES, active_attributes, 0x8B89, 1)                                             \
    X(FLOAT_VEC2, float_vec2, 0x8B50, 1)                                                           \
    X(FLOAT_VEC3, float_vec3, 0x8B51, 1)                                                           \
    X(FLOAT_VEC4, float_vec4, 0x8B52, 1)                                                           \
    X(INT_VEC2, int_vec2, 0x8B53, 1)                                                               \
    X(INT_VEC3, int_vec3, 0x8B54, 1)                                                               \
    X(INT_VEC4, int_vec4, 0x8B55, 1)                                                               \
    X(BOOL, bool_, 0x8B56, 1)                                                                      \
    X(BOOL_VEC2, bool_vec2, 0x8B57, 1)                                                             \
    X(BOOL_VEC3, bool_vec3, 0x8B58, 1)                                                             \
    X(BOOL_VEC4, bool_vec4, 0x8B59, 1)                                                             \
    X(FLOAT_MAT2, float_mat2, 0x8B5A, 1)                                                           \
    X(FLOAT_MAT3, float_mat3, 0x8B5B, 1)                                                           \
    X(FLOAT_MAT4, float_mat4, 0x8B5C, 1)                                                           \
    X(SAMPLER_2D, sampler_2d, 0x8B5E, 1)                                                           \
    X(SAMPLER_CUBE, sampler_cube, 0x8B60, 1)                                                       \
    X(MAX_TEXTURE_SIZE, max_texture_size, 0x0D33, 1)                                               \
    X(MAX_VERTEX_ATTRIBS, max_vertex_attribs, 0x8869, 1)                                           \
    X(MAX_TEXTURE_IMAGE_UNITS, max_texture_image_units, 0x8872, 1)                                 \
    X(MAX_VIEWPORT_DIMS, max_viewport_dims, 0x0D3A, 1)                                             \
    X(RGBA8, rgba8, 0x8058, 2)                                                                     \
    X(RGB8, rgb8, 0x8051, 2)                                                                       \
    X(SRGB8_ALPHA8, srgb8_alpha8, 0x8C43, 2)                                                       \
    X(R8, r8, 0x8229, 2)                                                                           \
    X(RG8, rg8, 0x822B, 2)                                                                         \
    X(RGBA16F, rgba16f, 0x881A, 2)                                                                 \
    X(RGBA32F, rgba32f, 0x8814, 2)                                                                 \
    X(DEPTH_COMPONENT24, depth_component24, 0x81A6, 2)                                             \
    X(DEPTH24_STENCIL8, depth24_stencil8, 0x88F0, 2)                                               \
    X(TEXTURE_3D, texture_3d, 0x806F, 2)                                                           \
    X(TEXTURE_2D_ARRAY, texture_2d_array, 0x8C1A, 2)                                               \
    X(TEXTURE_BINDING_3D, texture_binding_3d, 0x806A, 2)                                           \
    X(UNIFORM_BUFFER, uniform_buffer, 0x8A11, 2)                                                   \
    X(COPY_READ_BUFFER, copy_read_buffer, 0x8F36, 2)                                               \
    X(PIXEL_PACK_BUFFER, pixel_pack_buffer, 0x88EB, 2)                                             \
    X(VERTEX_ARRAY_BINDING, vertex_array_binding, 0x85B5, 2)                                       \
    X(TRANSFORM_FEEDBACK, transform_feedback, 0x8E22, 2)                                           \
    X(SYNC_GPU_COMMANDS_COMPLETE, sync_gpu_commands_complete, 0x9117, 2)                           \
    X(MAX_DRAW_BUFFERS, max_draw_buffers, 0x8824, 2)                                               \
    X(MAX_COLOR_ATTACHMENTS, max_color_attachments, 0x8CDF, 2)                                     \
    X(DRAW_BUFFER0, draw_buffer0, 0x8825, 2)                                                       \
    X(COLOR_ATTACHMENT1, color_attachment1, 0x8CE1, 2)                                             \
    X(DRAW_BUFFER1, draw_buffer1, 0x8826, 2)                                                       \
    X(COLOR_ATTACHMENT2, color_attachment2, 0x8CE2, 2)                                             \
    X(DRAW_BUFFER2, draw_buffer2, 0x8827, 2)                                                       \
    X(COLOR_ATTACHMENT3, color_attachment3, 0x8CE3, 2)                                             \
    X(DRAW_BUFFER3, draw_buffer3, 0x8828, 2)                                                       \
    X(COLOR_ATTACHMENT4, color_attachment4, 0x8CE4, 2)                                             \
    X(DRAW_BUFFER4, draw_buffer4, 0x8829, 2)                                                       \
    X(COLOR_ATTACHMENT5, color_attachment5, 0x8CE5, 2)                                             \
    X(DRAW_BUFFER5, draw_buffer5, 0x882A, 2)                                                       \
    X(COLOR_ATTACHMENT6, color_attachment6, 0x8CE6, 2)                                             \
    X(DRAW_BUFFER6, draw_buffer6, 0x882B, 2)                                                       \
    X(COLOR_ATTACHMENT7, color_attachment7, 0x8CE7, 2)                                             \
    X(DRAW_BUFFER7, draw_buffer7, 0x882C, 2)                                                       \
    X(VERTEX_ATTRIB_ARRAY_DIVISOR, vertex_attrib_array_divisor, 0x88FE, 2)                         \
    X(VERTEX_ATTRIB_ARRAY_ENABLED, vertex_attrib_array_enabled, 0x8622, 1)                         \
    X(VERTEX_ATTRIB_ARRAY_SIZE, vertex_attrib_array_size, 0x8623, 1)                               \
    X(VERTEX_ATTRIB_ARRAY_STRIDE, vertex_attrib_array_stride, 0x8624, 1)                           \
    X(VERTEX_ATTRIB_ARRAY_TYPE, vertex_attrib_array_type, 0x8625, 1)                               \
    X(VERTEX_ATTRIB_ARRAY_NORMALIZED, vertex_attrib_array_normalized, 0x886A, 1)                   \
    X(VERSION, version, 0x1F02, 1)                                                                 \
    X(RENDERER, renderer, 0x1F01, 1)                                                               \
    X(VENDOR, vendor, 0x1F00, 1)                                                                   \
    X(SHADING_LANGUAGE_VERSION, shading_language_version, 0x8B8C, 1)                               \
    X(NO_ERROR, no_error, 0x0000, 1)                                                               \
    X(INVALID_ENUM, invalid_enum, 0x0500, 1)                                                       \
    X(INVALID_VALUE, invalid_value, 0x0501, 1)                                                     \
    X(INVALID_OPERATION, invalid_operation, 0x0502, 1)                                             \
    X(OUT_OF_MEMORY, out_of_memory, 0x0505, 1)                                                     \
    X(FRAMEBUFFER, framebuffer, 0x8D40, 1)                                                         \
    X(RENDERBUFFER, renderbuffer, 0x8D41, 1)                                                       \
    X(DEPTH_COMPONENT16, depth_component16, 0x81A5, 1)                                             \
    X(DEPTH_ATTACHMENT, depth_attachment, 0x8D00, 1)                                               \
    X(COLOR_ATTACHMENT0, color_attachment0, 0x8CE0, 1)                                             \
    X(FRAMEBUFFER_COMPLETE, framebuffer_complete, 0x8CD5, 1)                                       \
    X(TEXTURE0, texture0, 0x84C0, 1)                                                               \
    X(TEXTURE1, texture1, 0x84C1, 1)                                                               \
    X(TEXTURE2, texture2, 0x84C2, 1)                                                               \
    X(TEXTURE3, texture3, 0x84C3, 1)                                                               \
    X(TEXTURE4, texture4, 0x84C4, 1)                                                               \
    X(TEXTURE5, texture5, 0x84C5, 1)                                                               \
    X(TEXTURE6, texture6, 0x84C6, 1)                                                               \
    X(TEXTURE7, texture7, 0x84C7, 1)                                                               \
    X(TEXTURE8, texture8, 0x84C8, 1)                                                               \
    X(TEXTURE9, texture9, 0x84C9, 1)                                                               \
    X(TEXTURE10, texture10, 0x84CA, 1)                                                             \
    X(TEXTURE11, texture11, 0x84CB, 1)                                                             \
    X(TEXTURE12, texture12, 0x84CC, 1)                                                             \
    X(TEXTURE13, texture13, 0x84CD, 1)                                                             \
    X(TEXTURE14, texture14, 0x84CE, 1)                                                             \
    X(TEXTURE15, texture15, 0x84CF, 1)

// The C++ names, because `0x8892` in a switch is unreadable and a transposed
// digit is invisible.
namespace gl_enum {
#define X(NAME, ident, v, ver) inline constexpr std::uint32_t ident = v;
CTBROWSER_GL_CONSTANTS(X)
#undef X
} // namespace gl_enum

// A UNIFORM'S VALUE, as the bindings collect it from JavaScript.
//
// This exists because the bindings used to carry `raster::glsl::value` - a type
// from the GLSL front end the rewrite deleted - which made the JavaScript
// surface depend on a rasteriser it should know nothing about. A uniform is a
// shape and some numbers; that is all this is.
struct uniform_value {
    int rows = 1; // 1 is a scalar, 3 a vec3, 3 x 3 a mat3
    int cols = 1;
    bool integer = false;
    // FLOATS EVEN FOR INTEGERS. A uniform is at most a mat4, so widening costs
    // nothing and one path is easier to keep right than two - `integer` says
    // which GL entry point to call.
    std::vector<float> data;
};

// WHAT A LINKED PROGRAM DECLARES, asked of GL and never cached.
//
// Both p5 and Babylon ENUMERATE a program rather than asking for names they
// already know. Answering from anywhere but the program itself tells them the
// shader declares nothing, so they bind nothing and draw nothing, with no error
// at any point.
// ONE ATTRIBUTE, as `getVertexAttrib` reports it. Filled from GL on each ask.
struct vertex_attribute {
    bool enabled = false;
    int size = 4;
    int stride = 0;
    std::uint32_t type = 0;
    bool normalized = false;
    std::uint32_t divisor = 0;
};

struct active_variable {
    std::string name;
    std::uint32_t type = 0; // the GL code, e.g. GL_FLOAT_VEC3
    int size = 1;           // the ARRAY LENGTH, 1 for a plain declaration
};

class webgl_context {
public:
    // THE BINDINGS SPELL THESE AS webgl_context::active_variable, and that is
    // the right name from a caller's point of view - they are what a context
    // reports. They live outside the class so the header's readers meet them
    // before the 64 methods rather than inside them.
    using active_variable = shell::active_variable;
    using vertex_attribute = shell::vertex_attribute;
    using uniform_value = shell::uniform_value;

    // THE CANVAS OWNS THE BITMAP, not this. A page's canvas already has a
    // surface the painter composites; borrowing it means `present()` writes
    // where the compositor already looks, with no second copy to keep in step.
    webgl_context(paint::bitmap * surface, int width, int height,
                  raster::gl::driver which = raster::gl::driver::fastest);

    // A CANVAS THAT CHANGED SIZE. The device is recreated, because a GL surface
    // has a fixed size and a context whose drawing buffer no longer matches the
    // canvas renders into the wrong rectangle - which is invisible until
    // something is drawn near an edge.
    void resize(paint::bitmap * surface, int width, int height);

    [[nodiscard]] bool ok() const;

    // --- context and surface -------------------------------------------------

    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;

    // The canvas bitmap, which is what the painter composites. Reading the
    // device into it is `present()`, and it is deliberately NOT done per draw:
    // the old context read back after every drawArrays, which is a full surface
    // copy per mesh.
    [[nodiscard]] paint::bitmap * surface();
    void present();

    // WHICH WebGL a page asked for. Kept because it is a fact about the PAGE's
    // request, not a mirror of driver state - `getContext('webgl')` and
    // `getContext('webgl2')` on the same canvas must not both succeed.
    void set_version(int version);
    [[nodiscard]] int version() const;

    void viewport(int x, int y, int width, int height);
    void scissor(int x, int y, int width, int height);
    void clear(std::uint32_t mask);
    void clear_color(float red, float green, float blue, float alpha);
    void clear_depth(float depth);
    void set_enabled(std::uint32_t capability, bool on);

    // --- shaders and programs ------------------------------------------------

    [[nodiscard]] std::uint32_t create_shader(std::uint32_t kind);
    void shader_source(std::uint32_t shader, std::string_view source);
    void compile_shader(std::uint32_t shader);
    [[nodiscard]] bool shader_compiled(std::uint32_t shader) const;
    [[nodiscard]] std::string shader_log(std::uint32_t shader) const;

    [[nodiscard]] std::uint32_t create_program();
    void attach_shader(std::uint32_t program, std::uint32_t shader);
    void link_program(std::uint32_t program);
    [[nodiscard]] bool program_linked(std::uint32_t program) const;
    [[nodiscard]] std::string program_log(std::uint32_t program) const;
    void use_program(std::uint32_t program);

    [[nodiscard]] std::vector<active_variable> active_attributes(std::uint32_t program) const;
    [[nodiscard]] std::vector<active_variable> active_uniforms(std::uint32_t program) const;
    [[nodiscard]] int attribute_location(std::uint32_t program, std::string_view name) const;

    [[nodiscard]] int get_uniform_block_index(std::uint32_t program, std::string_view name) const;
    void uniform_block_binding(std::uint32_t program, std::uint32_t index, std::uint32_t binding);

    // BY NAME, because that is what the bindings carry: WebGL hands a page an
    // opaque location object and this engine puts the NAME in it.
    void set_uniform(std::string_view name, const uniform_value & value);

    // --- buffers, attributes and vertex arrays --------------------------------

    [[nodiscard]] std::uint32_t create_buffer();
    void bind_buffer(std::uint32_t target, std::uint32_t buffer);
    void buffer_data(std::uint32_t target, std::span<const std::byte> bytes, std::uint32_t usage);
    void buffer_data(std::uint32_t target, int size, std::uint32_t usage);
    void buffer_sub_data(std::uint32_t target, int offset, std::span<const std::byte> bytes);
    void bind_buffer_base(std::uint32_t target, std::uint32_t index, std::uint32_t buffer);
    void delete_object(raster::gl::device::object_kind kind, std::uint32_t name);

    void enable_attribute(int location, bool on);
    void attribute_pointer(int location, int size, std::uint32_t type, bool normalised, int stride,
                           int offset);
    void attribute_divisor(int location, int divisor);

    // ASKED OF GL EVERY TIME, into scratch the caller borrows. A page can change
    // an attribute through a vertex array this layer never saw, so a remembered
    // answer would be a guess wearing the shape of a fact.
    [[nodiscard]] const vertex_attribute * attribute_at(int location) const;

    [[nodiscard]] std::uint32_t create_vertex_array();
    void bind_vertex_array(std::uint32_t array);
    void delete_vertex_array(std::uint32_t array);
    [[nodiscard]] bool is_vertex_array(std::uint32_t array) const;
    [[nodiscard]] std::uint32_t bound_vertex_array() const;

    // --- textures and framebuffers --------------------------------------------

    [[nodiscard]] std::uint32_t create_texture();
    void bind_texture(std::uint32_t target, std::uint32_t texture);
    void active_texture(std::uint32_t unit);
    void texture_image(std::uint32_t target, int width, int height,
                       std::span<const std::byte> rgba);
    // A DECODED IMAGE, straight from the canvas layer. `texImage2D` with an
    // <img> or a <canvas> is the common case and the bitmap is already ARGB, so
    // the swap happens once here rather than in every caller.
    void texture_from_bitmap(std::uint32_t target, const paint::bitmap & image);
    void texture_parameter(std::uint32_t target, std::uint32_t name, std::uint32_t value);

    void bind_framebuffer(std::uint32_t framebuffer);
    void framebuffer_texture(std::uint32_t attachment, std::uint32_t texture);
    [[nodiscard]] std::uint32_t framebuffer_status() const;
    [[nodiscard]] std::uint32_t create_framebuffer();
    [[nodiscard]] std::uint32_t create_renderbuffer();
    void bind_renderbuffer(std::uint32_t renderbuffer);
    void renderbuffer_storage(std::uint32_t format, int width, int height);
    void framebuffer_renderbuffer(std::uint32_t attachment, std::uint32_t renderbuffer);

    // --- draws and pipeline state ---------------------------------------------

    void draw_arrays(std::uint32_t mode, int first, int count);
    void draw_elements(std::uint32_t mode, int count, std::uint32_t type, int offset);
    void draw_arrays_instanced(std::uint32_t mode, int first, int count, int instances);
    void draw_elements_instanced(std::uint32_t mode, int count, std::uint32_t type, int offset,
                                 int instances);

    void cull_face(std::uint32_t which);
    void front_face(std::uint32_t which);
    void depth_func(std::uint32_t how);
    void depth_mask(bool on);
    void blend_func(std::uint32_t source, std::uint32_t destination);

    // --- the error contract, which is NOT the thing being deleted ------------
    //
    // Refusing a call BY NAME is the documented leniency contract in
    // docs/history/webgl2.md, and it is the opposite of a silent no-op: a page can
    // read `getError`, and a report can list what this engine declined. Keeping
    // it is deliberate.
    [[nodiscard]] std::uint32_t take_error();
    // One integer cap, asked of GL. See raster::gl::device::limit.
    [[nodiscard]] int limit(std::uint32_t name) const;
    // WHAT IS ACTUALLY DRAWING, for getParameter(RENDERER) - the string a
    // page prints into a bug report, so it has to be the driver's own.
    [[nodiscard]] std::string renderer() const;
    void refuse(std::string_view call);
    [[nodiscard]] const std::vector<std::string> & refused() const;
    [[nodiscard]] const std::string & shader_error() const;

private:
    void fail(std::uint32_t error);
    // The `[ubo] data` half of the CTBROWSER_GL_UBO diagnostic, shared by both
    // buffer_data overloads so `bufferData(target, size, usage)` - the one a
    // page uses to RESERVE storage it fills later - cannot go unrecorded.
    void note_storage(std::uint32_t target, std::size_t size);

    raster::gl::device device_;
    paint::bitmap * surface_ = nullptr;
    int version_ = 1;
    std::uint32_t error_ = 0;
    std::string shader_error_;
    std::vector<std::string> refused_;
    // SCRATCH for attribute_at, refilled from GL on every call. It is a query
    // result the caller borrows, not state - the distinction that this whole
    // rewrite turns on.
    mutable vertex_attribute scratch_;
};

} // namespace ctbrowser::shell
