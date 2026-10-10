//===- webgl.hpp - WebGL calls translated to GLES ---------------*- C++ -*-===//
//
// A page's WebGL calls, translated to GLES, and nothing else.
//
// The rule: a method here translates and returns. It does not remember
// anything GL can be asked for. Where WebGL needs identity that GL does not
// provide (the `WebGLBuffer` object a page holds onto), a handle table stores
// the identity and never the behaviour; caching what a buffer contains or
// which program is bound would make it a mirror of GL state. An earlier design
// kept such a mirror and silently dropped calls it never forwarded (see
// docs/history/webgl-rewrite.md).
//
// Every method the bindings in lib/Shell/bindings/webgl/ need is declared
// here, so a method that is not implemented is a compile error at the binding
// site rather than a silent no-op.
//
//===----------------------------------------------------------------------===//

#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/paint/command.hpp>
#include <ctbrowser/raster/gl.hpp>

namespace ctbrowser::shell {

// The GL constants, defined once. Each row is what a page reads as `gl.NAME`,
// the C++ name the engine uses for the same number, the number, and whether it
// is on every context (1) or only a WebGL 2 one (2). The rows are in the order
// the context object gets them, which is the order `Object.keys(gl)` reports.
//
// The numbers are fixed by the specification and identical in every engine.
// The list is an X-macro so the JavaScript constant and the `gl_enum::` name
// come from the same row and cannot drift apart.
//
// The version column matters both ways: a WebGL 2 constant that arrives as
// `undefined` makes every comparison against it silently false, and a WebGL 1
// page that finds `gl.RGBA8` concludes it has WebGL 2. TEXTURE_BINDING_3D in
// particular is what Babylon.js tests to pick its GLSL dialect; without it
// every shader was ES 3.00 source with no `#version` and the post-process quad
// never drew, with no GL error anywhere.
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

/// The C++ names for the GL constants, because `0x8892` in a switch is
/// unreadable and a transposed digit is invisible.
namespace gl_enum {
#define X(NAME, ident, v, ver) inline constexpr std::uint32_t ident = v;
CTBROWSER_GL_CONSTANTS(X)
#undef X
} // namespace gl_enum

/// A uniform's value as the bindings collect it from JavaScript: a shape
/// (rows x cols) and its numbers, independent of any rasteriser type.
struct uniform_value {
    int rows = 1; // 1 is a scalar, 3 a vec3, 3 x 3 a mat3
    int cols = 1;
    bool integer = false;
    /// Floats even for integers. A uniform is at most a mat4, so widening
    /// costs nothing and one path is easier to keep right than two; `integer`
    /// says which GL entry point to call.
    std::vector<float> data;
};

/// One vertex attribute's state, as `getVertexAttrib` reports it. Filled from
/// GL on each request.
struct vertex_attribute {
    bool enabled = false;
    int size = 4;
    int stride = 0;
    std::uint32_t type = 0;
    bool normalized = false;
    std::uint32_t divisor = 0;
};

/// One active attribute or uniform of a linked program, as GL reports it;
/// never cached. p5 and Babylon enumerate a program instead of asking for
/// names they know, so answering from anywhere but GL makes the shader look
/// empty and nothing draws, with no error.
struct active_variable {
    std::string name;
    std::uint32_t type = 0; // the GL code, e.g. GL_FLOAT_VEC3
    int size = 1;           // the array length, 1 for a plain declaration
};

/// A WebGL or WebGL 2 rendering context over a raster::gl::device.
class webgl_context {
public:
    // The bindings spell these webgl_context::active_variable and so on,
    // since they are what a context reports. They are defined outside the
    // class so readers meet them before the methods.
    using active_variable = shell::active_variable;
    using vertex_attribute = shell::vertex_attribute;
    using uniform_value = shell::uniform_value;

    /// The canvas owns `surface`, not this context. Borrowing the surface the
    /// painter already composites means `present()` writes where the
    /// compositor looks, with no second copy to keep in step.
    webgl_context(paint::bitmap * surface, int width, int height);

    /// Called when the canvas changes size. The device is recreated, because a
    /// GL surface has a fixed size and a drawing buffer that no longer matches
    /// the canvas renders into the wrong rectangle.
    void resize(paint::bitmap * surface, int width, int height);

    /// The device itself, for every call that is a plain translation: this
    /// class keeps only the methods that validate, record an error or trace.
    [[nodiscard]] raster::gl::device & device() { return device_; }

    /// The canvas bitmap the painter composites. present() copies the device
    /// into it once per frame, not per draw, since each copy is a full surface
    /// read-back.
    [[nodiscard]] paint::bitmap * surface();
    void present();

    /// Which WebGL version (1 or 2) the page asked for. Kept because it is a
    /// fact about the page's request, not a mirror of driver state:
    /// `getContext('webgl')` and `getContext('webgl2')` on the same canvas
    /// must not both succeed.
    void set_version(int version);
    [[nodiscard]] int version() const;

    void viewport(int x, int y, int width, int height);
    void scissor(int x, int y, int width, int height);

    // --- shaders and programs ------------------------------------------------

    void shader_source(std::uint32_t shader, std::string_view source);
    void compile_shader(std::uint32_t shader);
    void link_program(std::uint32_t program);

    [[nodiscard]] std::vector<active_variable> active_attributes(std::uint32_t program) const;
    [[nodiscard]] std::vector<active_variable> active_uniforms(std::uint32_t program) const;

    [[nodiscard]] int get_uniform_block_index(std::uint32_t program, std::string_view name) const;
    void uniform_block_binding(std::uint32_t program, std::uint32_t index, std::uint32_t binding);

    /// Sets a uniform by name, because that is what the bindings carry: WebGL
    /// hands a page an opaque location object and this engine puts the name in
    /// it.
    void set_uniform(std::string_view name, const uniform_value & value);

    // --- buffers, attributes and vertex arrays --------------------------------

    void buffer_data(std::uint32_t target, std::span<const std::byte> bytes, std::uint32_t usage);
    void buffer_data(std::uint32_t target, int size, std::uint32_t usage);
    void buffer_sub_data(std::uint32_t target, int offset, std::span<const std::byte> bytes);
    void bind_buffer_base(std::uint32_t target, std::uint32_t index, std::uint32_t buffer);

    void enable_attribute(int location, bool on);
    void attribute_pointer(int location, int size, std::uint32_t type, bool normalised, int stride,
                           int offset);
    void attribute_divisor(int location, int divisor);

    /// Asked of GL every time, into scratch the caller borrows. A page can
    /// change an attribute through a vertex array this layer never saw, so a
    /// remembered answer could be wrong.
    [[nodiscard]] const vertex_attribute * attribute_at(int location) const;

    // --- textures and framebuffers --------------------------------------------

    void texture_image(std::uint32_t target, int width, int height,
                       std::span<const std::byte> rgba);
    /// Uploads a decoded image straight from the canvas layer. `texImage2D`
    /// with an <img> or a <canvas> is the common case and the bitmap is ARGB,
    /// so the swap to RGBA happens once here rather than in every caller.
    void texture_from_bitmap(std::uint32_t target, const paint::bitmap & image);

    // --- draws -----------------------------------------------------------------

    void draw_arrays(std::uint32_t mode, int first, int count);
    void draw_elements(std::uint32_t mode, int count, std::uint32_t type, int offset);
    void draw_arrays_instanced(std::uint32_t mode, int first, int count, int instances);
    void draw_elements_instanced(std::uint32_t mode, int count, std::uint32_t type, int offset,
                                 int instances);

    // --- errors and refused calls -------------------------------------------
    //
    // Refusing a call by name is the documented leniency contract in
    // docs/history/webgl2.md, and is the opposite of a silent no-op: a page can
    // read `getError`, and a report can list what this engine declined.
    [[nodiscard]] std::uint32_t take_error();
    void refuse(std::string_view call);
    [[nodiscard]] const std::vector<std::string> & refused() const;
    [[nodiscard]] const std::string & shader_error() const;

private:
    void fail(std::uint32_t error);
    /// The `[ubo] data` half of the CTBROWSER_GL_UBO diagnostic, shared by
    /// both buffer_data overloads so `bufferData(target, size, usage)`, which a
    /// page uses to reserve storage it fills later, is recorded too.
    void note_storage(std::uint32_t target, std::size_t size);

    raster::gl::device device_;
    paint::bitmap * surface_ = nullptr;
    int version_ = 1;
    std::uint32_t error_ = 0;
    std::string shader_error_;
    std::vector<std::string> refused_;
    // Scratch for attribute_at, refilled from GL on every call. It is a query
    // result the caller borrows, not cached GL state.
    mutable vertex_attribute scratch_;
};

} // namespace ctbrowser::shell
