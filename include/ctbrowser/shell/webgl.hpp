#pragma once
#include <cstdint>
#include <map>
#include <span>
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

// A GLSL type as the GL enum that names it. Free rather than a method on
// `glsl::type` because it is GL's vocabulary, not the language's - raster/ has
// no business knowing these numbers.
[[nodiscard]] std::uint32_t gl_type_code(const raster::glsl::type & t) noexcept;

// The objects a context owns. Each is reached by a small integer, which is what
// `createBuffer` hands back and what the binding layer wraps in a JS object.
struct gl_buffer {
    std::vector<std::byte> bytes;
    std::uint32_t usage = gl_enum::static_draw;
};

struct gl_shader {
    std::uint32_t which = gl_enum::vertex_shader;
    std::string source;
    raster::glsl::shader compiled;
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
    std::unordered_map<std::string, raster::glsl::value, raster::glsl::string_hash, std::equal_to<>>
        uniforms;
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
    // INSTANCING. 0 means "advance once per vertex", which is every attribute a
    // WebGL 1 page ever set; N means "advance once per N instances", which is
    // what makes one buffer hold per-instance data. `vertexAttribDivisor` in
    // WebGL 2 and `vertexAttribDivisorANGLE` in the extension set the same
    // field - see the note on gl_vertex_array below.
    int divisor = 0;
};

// A VERTEX ARRAY OBJECT: the attribute state, captured.
//
// That is all one is - `bindVertexArray` swaps the whole attribute table and
// the element-array binding in and out, so a renderer can set up its formats
// once and select them with a single call instead of a dozen. Phaser does
// exactly that, through OES_vertex_array_object.
//
// ONE IMPLEMENTATION, TWO SPELLINGS, which is the decision docs/webgl2-plan.md
// records: `createVertexArray` on a WebGL 2 context and `createVertexArrayOES`
// on the extension object are the same operation, so they call the same code
// here. Two implementations of one job is the bug this tree has already paid
// for twice.
struct gl_vertex_array {
    std::vector<vertex_attribute> attributes;
    std::uint32_t element_buffer = 0;
};

class webgl_context {
public:
    webgl_context(paint::bitmap * target, int width, int height);

    // WHICH WEBGL THIS CONTEXT IS. 1 or 2, decided by the string the page
    // passed to getContext, and it changes exactly two things here: the version
    // strings a page reads, and whether `WEBGL2` is defined for every shader
    // this context compiles. That define is what flips p5's own preamble from
    // attribute/varying/gl_FragColor to in/out/a declared output - p5 ships
    // ONE set of shaders written to macro over the difference.
    void set_version(int version) { version_ = version; }
    [[nodiscard]] int version() const noexcept { return version_; }

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

    // WHAT A PROGRAM DECLARES, enumerated. `getActiveUniform` and
    // `getActiveAttrib` are how a LIBRARY discovers a shader rather than how a
    // page uses one: a hand-written page knows its own uniform names and asks
    // for them by name, so nothing needed this until p5 arrived.
    //
    // p5 asks for the counts first and only then asks for locations. Answering
    // zero - which is what an unimplemented getProgramParameter did - told it
    // the shader had no inputs at all, so it bound no attributes, set no
    // matrices, and drew a cube with no vertices. No GL error, no warning, one
    // correct-looking drawElements, and an empty canvas.
    struct active_variable {
        std::string name;
        raster::glsl::type t;
    };
    // Uniforms from BOTH stages, by name: GL links them into one namespace, and
    // p5's shaders declare the same matrix in each.
    [[nodiscard]] std::vector<active_variable> active_uniforms(std::uint32_t program) const;
    // Attributes are the VERTEX shader's alone, in the order link_program
    // assigned their locations - so index i here is location i.
    [[nodiscard]] std::vector<active_variable> active_attributes(std::uint32_t program) const;

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
    // INSTANCED, which is `count` vertices drawn `instances` times. An
    // attribute with divisor 0 advances per vertex as always; one with divisor
    // N advances once per N instances, which is the whole feature.
    std::size_t draw_arrays_instanced(std::uint32_t mode, int first, int count, int instances);
    std::size_t draw_elements_instanced(std::uint32_t mode, int count, std::uint32_t type,
                                        int offset, int instances);
    // `bufferSubData(target, offset, data)` - UPDATE IN PLACE, which is how a
    // batching renderer works: allocate one large buffer once and rewrite the
    // used prefix every frame. Phaser's WebGL renderer does exactly that and
    // could not draw a single quad without it.
    void buffer_sub_data(std::uint32_t target, int offset, std::span<const std::byte> bytes);
    void attribute_divisor(int location, int divisor);
    [[nodiscard]] int attribute_divisor_of(int location) const;

    // --- vertex array objects, both spellings ------------------------------
    [[nodiscard]] std::uint32_t create_vertex_array();
    void bind_vertex_array(std::uint32_t id);
    void delete_vertex_array(std::uint32_t id);
    [[nodiscard]] bool is_vertex_array(std::uint32_t id) const;
    [[nodiscard]] std::uint32_t bound_vertex_array() const noexcept { return bound_vao_; }

    // WHY A DRAW DREW NOTHING, when the reason was the shader rather than the
    // geometry. Empty when the last draw ran clean. GL has no such call - a
    // compiled GPU shader cannot fail per fragment - so this is the engine's
    // own, and it is the difference between a diagnosable failure and an empty
    // canvas. The matching getError() code is INVALID_OPERATION.
    [[nodiscard]] const std::string & shader_error() const noexcept { return shader_error_; }

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
    // The vertex arrays a page made, and which one is bound. ZERO IS THE
    // DEFAULT vertex array and is not in the map: it is `attributes_` itself,
    // which is what every WebGL 1 page has always used and what binding null
    // returns to.
    std::map<std::uint32_t, gl_vertex_array> vertex_arrays_;
    int version_ = 1;
    std::uint32_t bound_vao_ = 0;
    // The DEFAULT vertex array's saved state, for when a page binds one and
    // then binds null again. It is not in the map because zero is not an id a
    // page may delete or re-create.
    gl_vertex_array default_vao_;
    std::uint32_t next_vao_ = 1;
    // Which instance the draw is on, for gather() to apply divisors against.
    // Held here rather than threaded through every call between draw and
    // gather, which is four layers of signature for one integer.
    int current_instance_ = 0;

    raster::draw_state state_;
    raster::framebuffer framebuffer_;
    int width_ = 0;
    int height_ = 0;
    float clear_[4]{0, 0, 0, 0};
    float clear_depth_ = 1.0f;
    std::uint32_t error_ = gl_enum::no_error;
    std::string shader_error_;
};

} // namespace ctbrowser::shell
