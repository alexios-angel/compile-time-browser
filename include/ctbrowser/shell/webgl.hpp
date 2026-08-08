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
// all and no test could see it. See docs/webgl-rewrite-plan.md - this file
// exists to make that shape impossible rather than to make it less likely.
//
// THE RULE: a method here translates and returns. It does not remember anything
// GL can be asked for. Where WebGL needs identity that GL does not provide - the
// `WebGLBuffer` object a page holds onto - a handle table stores the identity
// and NEVER the behaviour. The moment it caches what a buffer contains or which
// program is bound, it is the mirror again.
//
// BUILT GROUP BY GROUP against the 64 methods `webgl_bindings.cpp` calls, which
// is the checklist in docs/webgl-rewrite-plan.md. A method that is not written
// yet is a COMPILE ERROR at the binding site - which is the whole point, and the
// opposite of the silent no-op this replaces.

namespace ctbrowser::shell {

// THE GL CONSTANTS, recovered intact from the deleted header.
//
// This is a TABLE, not an implementation - the numbers WebGL gives a page, which
// are fixed by the specification and identical in every engine. Nothing here can
// drift out of step with a driver, which is why it survived the rewrite when the
// code around it did not.
// use, because `0x8892` in a switch is unreadable and a transposed digit is
// invisible.
namespace gl_enum {
inline constexpr std::uint32_t depth_buffer_bit = 0x00000100;
inline constexpr std::uint32_t stencil_buffer_bit = 0x00000400;
inline constexpr std::uint32_t color_buffer_bit = 0x00004000;

inline constexpr std::uint32_t points = 0x0000;
inline constexpr std::uint32_t lines = 0x0001;
inline constexpr std::uint32_t triangles = 0x0004;
inline constexpr std::uint32_t triangle_strip = 0x0005;
inline constexpr std::uint32_t triangle_fan = 0x0006;

inline constexpr std::uint32_t depth_test = 0x0B71;
inline constexpr std::uint32_t blend = 0x0BE2;
inline constexpr std::uint32_t cull_face = 0x0B44;
inline constexpr std::uint32_t scissor_test = 0x0C11;

inline constexpr std::uint32_t front = 0x0404;
inline constexpr std::uint32_t back = 0x0405;
inline constexpr std::uint32_t cw = 0x0900;
inline constexpr std::uint32_t ccw = 0x0901;

inline constexpr std::uint32_t never = 0x0200;
inline constexpr std::uint32_t less = 0x0201;
inline constexpr std::uint32_t equal = 0x0202;
inline constexpr std::uint32_t lequal = 0x0203;
inline constexpr std::uint32_t greater = 0x0204;
inline constexpr std::uint32_t notequal = 0x0205;
inline constexpr std::uint32_t gequal = 0x0206;
inline constexpr std::uint32_t always = 0x0207;

inline constexpr std::uint32_t zero = 0;
inline constexpr std::uint32_t one = 1;
inline constexpr std::uint32_t src_color = 0x0300;
inline constexpr std::uint32_t one_minus_src_color = 0x0301;
inline constexpr std::uint32_t src_alpha = 0x0302;
inline constexpr std::uint32_t one_minus_src_alpha = 0x0303;
inline constexpr std::uint32_t dst_alpha = 0x0304;
inline constexpr std::uint32_t one_minus_dst_alpha = 0x0305;
inline constexpr std::uint32_t dst_color = 0x0306;
inline constexpr std::uint32_t one_minus_dst_color = 0x0307;

inline constexpr std::uint32_t byte_ = 0x1400;
inline constexpr std::uint32_t unsigned_byte = 0x1401;
inline constexpr std::uint32_t short_ = 0x1402;
inline constexpr std::uint32_t unsigned_short = 0x1403;
inline constexpr std::uint32_t int_ = 0x1404;
inline constexpr std::uint32_t unsigned_int = 0x1405;
inline constexpr std::uint32_t float_ = 0x1406;

inline constexpr std::uint32_t array_buffer = 0x8892;
inline constexpr std::uint32_t element_array_buffer = 0x8893;
// WebGL 2's buffer for uniform blocks. It needs its own binding: everything
// that was not the element buffer used to go to `array_buffer_`, so a page
// filling a uniform buffer overwrote whichever vertex buffer was bound.
inline constexpr std::uint32_t uniform_buffer = 0x8A11;
inline constexpr std::uint32_t color_attachment0 = 0x8CE0;
inline constexpr std::uint32_t framebuffer_complete = 0x8CD5;
inline constexpr std::uint32_t framebuffer_incomplete_attachment = 0x8CD6;
// Capabilities this rasteriser does not have. They are named so that DISABLING
// one can be a no-op rather than an error - see set_enabled.
inline constexpr std::uint32_t polygon_offset_fill = 0x8037;
inline constexpr std::uint32_t dither = 0x0BD0;
inline constexpr std::uint32_t rasterizer_discard = 0x8C89;
inline constexpr std::uint32_t static_draw = 0x88E4;
inline constexpr std::uint32_t dynamic_draw = 0x88E8;

inline constexpr std::uint32_t texture_2d = 0x0DE1;
inline constexpr std::uint32_t texture0 = 0x84C0;
inline constexpr std::uint32_t rgba = 0x1908;
inline constexpr std::uint32_t rgb = 0x1907;
inline constexpr std::uint32_t nearest = 0x2600;
inline constexpr std::uint32_t linear = 0x2601;
inline constexpr std::uint32_t texture_mag_filter = 0x2800;
inline constexpr std::uint32_t texture_min_filter = 0x2801;
inline constexpr std::uint32_t texture_wrap_s = 0x2802;
inline constexpr std::uint32_t texture_wrap_t = 0x2803;
inline constexpr std::uint32_t clamp_to_edge = 0x812F;
inline constexpr std::uint32_t repeat = 0x2901;

inline constexpr std::uint32_t compile_status = 0x8B81;
inline constexpr std::uint32_t link_status = 0x8B82;
inline constexpr std::uint32_t active_uniforms = 0x8B86;
inline constexpr std::uint32_t active_attributes = 0x8B89;

// The type codes getActiveUniform and getActiveAttrib report. A caller
// SWITCHES on these to decide which uniform* entry point to call, so a wrong
// one here sends a mat4 through uniform4fv.
inline constexpr std::uint32_t float_vec2 = 0x8B50;
inline constexpr std::uint32_t float_vec3 = 0x8B51;
inline constexpr std::uint32_t float_vec4 = 0x8B52;
inline constexpr std::uint32_t int_vec2 = 0x8B53;
inline constexpr std::uint32_t int_vec3 = 0x8B54;
inline constexpr std::uint32_t int_vec4 = 0x8B55;
inline constexpr std::uint32_t bool_ = 0x8B56;
inline constexpr std::uint32_t bool_vec2 = 0x8B57;
inline constexpr std::uint32_t bool_vec3 = 0x8B58;
inline constexpr std::uint32_t bool_vec4 = 0x8B59;
inline constexpr std::uint32_t float_mat2 = 0x8B5A;
inline constexpr std::uint32_t float_mat3 = 0x8B5B;
inline constexpr std::uint32_t float_mat4 = 0x8B5C;
inline constexpr std::uint32_t sampler_2d = 0x8B5E;
inline constexpr std::uint32_t sampler_cube = 0x8B60;
inline constexpr std::uint32_t vertex_shader = 0x8B31;
inline constexpr std::uint32_t fragment_shader = 0x8B30;

inline constexpr std::uint32_t max_texture_size = 0x0D33;
inline constexpr std::uint32_t max_vertex_attribs = 0x8869;
inline constexpr std::uint32_t version = 0x1F02;
inline constexpr std::uint32_t renderer = 0x1F01;
inline constexpr std::uint32_t vendor = 0x1F00;
inline constexpr std::uint32_t shading_language_version = 0x8B8C;

inline constexpr std::uint32_t no_error = 0;
inline constexpr std::uint32_t invalid_enum = 0x0500;
inline constexpr std::uint32_t invalid_value = 0x0501;
inline constexpr std::uint32_t invalid_operation = 0x0502;
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
    // Why the context could not be made, for a page that asked for one and got
    // null. EGL fails before there is a GL error to read, so this is not
    // `take_error`'s job.
    [[nodiscard]] const std::string & device_error() const;

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
    // docs/webgl2-plan.md, and it is the opposite of a silent no-op: a page can
    // read `getError`, and a report can list what this engine declined. Keeping
    // it is deliberate.
    [[nodiscard]] std::uint32_t take_error();
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
