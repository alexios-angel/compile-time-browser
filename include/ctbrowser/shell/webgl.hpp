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
    webgl_context(int width, int height, raster::gl::driver which = raster::gl::driver::fastest);

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
    [[nodiscard]] paint::bitmap & surface();
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
    void delete_object(std::uint32_t name, std::string_view kind);

    void enable_attribute(int location, bool on);
    void attribute_pointer(int location, int size, std::uint32_t type, bool normalised, int stride,
                           int offset);
    void attribute_divisor(int location, std::uint32_t divisor);

    // ASKED OF GL EVERY TIME, into scratch the caller borrows. A page can change
    // an attribute through a vertex array this layer never saw, so a remembered
    // answer would be a guess wearing the shape of a fact.
    [[nodiscard]] const vertex_attribute * attribute_at(int location) const;

    [[nodiscard]] std::uint32_t create_vertex_array();
    void bind_vertex_array(std::uint32_t array);
    void delete_vertex_array(std::uint32_t array);
    [[nodiscard]] bool is_vertex_array(std::uint32_t array) const;
    [[nodiscard]] std::uint32_t bound_vertex_array() const;

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

    raster::gl::device device_;
    paint::bitmap surface_;
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
