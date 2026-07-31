#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>

// The WebGL context's STATE MACHINE, with no JavaScript in it.
//
// Stage five of docs/webgl-plan.md. The binding layer in shell/bindings.cpp
// turns `gl.bufferData(...)` into a call here; this file owns what a context
// remembers and what a draw call does with it. Splitting them that way is what
// makes the interesting half testable without a page: tests/webgl_basics.cpp
// drives this directly.
//
// WebGL IS A STATE MACHINE, and an unusually large one. Almost every call here
// sets something that a later `drawArrays` reads - which is why the bugs in an
// implementation of it are rarely in the drawing and almost always in what was
// bound when. The state is therefore kept in ONE struct, in the order the
// specification groups it, rather than scattered across the methods that set it.
//
// WebGL 1 only. See the plan: p5 asks for `webgl2` first and falls back, and
// offering only 1 removes a language version and a large amount of surface for
// no caller.

namespace ctbrowser::shell {

// GL's numeric constants, as a page sees them. Named rather than pasted at each
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

// The objects a context owns. Each is reached by a small integer, which is what
// `createBuffer` hands back and what the binding layer wraps in a JS object.
struct gl_buffer {
    std::vector<std::byte> bytes;
    std::uint32_t usage = gl_enum::static_draw;
};

struct gl_shader {
    std::uint32_t which = gl_enum::vertex_shader;
    std::string source;
    raster::glsl::module compiled;
    bool compiled_ok = false;
    std::string log;
};

struct gl_program {
    std::uint32_t vertex = 0;
    std::uint32_t fragment = 0;
    bool linked = false;
    std::string log;
    // Attribute locations, assigned at link time in declaration order - which is
    // what `getAttribLocation` reports and what a page uses to bind its arrays.
    std::vector<std::string> attribute_names;
    // Uniform VALUES live with the program, because that is where GL keeps them:
    // `useProgram` does not reset them, and a page relies on that.
    std::unordered_map<std::string, raster::glsl::value> uniforms;
};

struct gl_texture {
    paint::bitmap pixels;
    std::uint32_t min_filter = gl_enum::linear;
    std::uint32_t mag_filter = gl_enum::linear;
    std::uint32_t wrap_s = gl_enum::repeat;
    std::uint32_t wrap_t = gl_enum::repeat;
};

// One `vertexAttribPointer`: where a named attribute's data lives.
struct vertex_attribute {
    bool enabled = false;
    std::uint32_t buffer = 0;
    int size = 4;
    std::uint32_t type = gl_enum::float_;
    bool normalized = false;
    int stride = 0;
    int offset = 0;
};

class webgl_context {
public:
    webgl_context(paint::bitmap * target, int width, int height);

    // --- objects
    [[nodiscard]] std::uint32_t create_buffer();
    [[nodiscard]] std::uint32_t create_shader(std::uint32_t which);
    [[nodiscard]] std::uint32_t create_program();
    [[nodiscard]] std::uint32_t create_texture();
    void delete_object(std::uint32_t id);

    void bind_buffer(std::uint32_t target, std::uint32_t buffer);
    void buffer_data(std::uint32_t target, std::vector<std::byte> bytes, std::uint32_t usage);

    void shader_source(std::uint32_t shader, std::string source);
    void compile_shader(std::uint32_t shader);
    [[nodiscard]] bool shader_compiled(std::uint32_t shader) const;
    [[nodiscard]] std::string shader_log(std::uint32_t shader) const;

    void attach_shader(std::uint32_t program, std::uint32_t shader);
    void link_program(std::uint32_t program);
    [[nodiscard]] bool program_linked(std::uint32_t program) const;
    [[nodiscard]] std::string program_log(std::uint32_t program) const;
    void use_program(std::uint32_t program);

    [[nodiscard]] int attribute_location(std::uint32_t program, const std::string & name) const;
    void enable_attribute(int location, bool on);
    void attribute_pointer(int location, int size, std::uint32_t type, bool normalized, int stride,
                           int offset);

    // A uniform's "location" here is its NAME. WebGL hands back an opaque object
    // and a page only ever passes it straight back, so the name is a location
    // that is also readable in a diagnostic.
    void set_uniform(const std::string & name, raster::glsl::value v);

    // --- state
    void viewport(int x, int y, int width, int height);
    void scissor(int x, int y, int width, int height);
    void set_enabled(std::uint32_t capability, bool on);
    void depth_func(std::uint32_t how);
    void depth_mask(bool on);
    void blend_func(std::uint32_t source, std::uint32_t destination);
    void cull_face(std::uint32_t which);
    void front_face(std::uint32_t which);
    void clear_color(float r, float g, float b, float a);
    void clear_depth(float depth);
    void clear(std::uint32_t mask);

    void bind_texture(std::uint32_t target, std::uint32_t texture);
    void active_texture(std::uint32_t unit);
    void texture_image(std::uint32_t target, int width, int height, std::vector<std::byte> pixels);
    void texture_from_bitmap(std::uint32_t target, const paint::bitmap & from);
    void texture_parameter(std::uint32_t target, std::uint32_t name, std::uint32_t value);

    // --- drawing
    //
    // Returns how many fragments were written, which is what a test asserts on
    // when the question is "did anything happen at all".
    std::size_t draw_arrays(std::uint32_t mode, int first, int count);
    std::size_t draw_elements(std::uint32_t mode, int count, std::uint32_t type, int offset);

    [[nodiscard]] std::uint32_t error() const noexcept { return error_; }
    [[nodiscard]] std::uint32_t take_error() noexcept {
        const std::uint32_t was = error_;
        error_ = gl_enum::no_error;
        return was;
    }
    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    void resize(paint::bitmap * target, int width, int height);

    // What `readPixels` hands back, and what a test looks at.
    [[nodiscard]] const paint::bitmap * surface() const noexcept { return framebuffer_.colour; }

private:
    void fail(std::uint32_t code) {
        // FIRST ERROR WINS, which is what glGetError does: it reports the oldest
        // unretrieved one, so a later failure cannot hide the cause.
        if (error_ == gl_enum::no_error) { error_ = code; }
    }

    // Pull one vertex's attributes out of the bound buffers.
    [[nodiscard]] raster::attribute_set gather(const gl_program & program, int index) const;

    std::unordered_map<std::uint32_t, gl_buffer> buffers_;
    std::unordered_map<std::uint32_t, gl_shader> shaders_;
    std::unordered_map<std::uint32_t, gl_program> programs_;
    std::unordered_map<std::uint32_t, gl_texture> textures_;
    std::uint32_t next_id_ = 1;

    std::uint32_t array_buffer_ = 0;
    std::uint32_t element_buffer_ = 0;
    std::uint32_t current_program_ = 0;
    std::uint32_t active_unit_ = 0;
    std::unordered_map<std::uint32_t, std::uint32_t> texture_units_; // unit -> texture id
    std::vector<vertex_attribute> attributes_;

    raster::draw_state state_;
    raster::framebuffer framebuffer_;
    int width_ = 0;
    int height_ = 0;
    float clear_[4]{0, 0, 0, 0};
    float clear_depth_ = 1.0f;
    std::uint32_t error_ = gl_enum::no_error;
};

} // namespace ctbrowser::shell
