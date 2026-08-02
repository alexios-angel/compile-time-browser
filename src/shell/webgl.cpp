#include <ctbrowser/shell/webgl.hpp>

#include <algorithm>
#include <cstring>

// The WebGL context's state machine. See webgl.hpp for the shape; this is the
// behaviour.
//
// THE PART WHERE THE BUGS LIVE is `gather` at the bottom: turning a buffer of
// bytes plus a (size, type, stride, offset) into one vertex's attributes. Every
// other method here records something; that one interprets it, and getting a
// stride wrong draws a plausible triangle out of the wrong numbers.

namespace ctbrowser::shell {
namespace {

using namespace ctbrowser::raster;

[[nodiscard]] depth_test to_depth(std::uint32_t how) {
    switch (how) {
    case gl_enum::never: return depth_test::never;
    case gl_enum::less: return depth_test::less;
    case gl_enum::equal: return depth_test::equal;
    case gl_enum::lequal: return depth_test::less_equal;
    case gl_enum::greater: return depth_test::greater;
    case gl_enum::notequal: return depth_test::not_equal;
    case gl_enum::gequal: return depth_test::greater_equal;
    default: return depth_test::always;
    }
}

[[nodiscard]] blend_factor to_blend(std::uint32_t which) {
    switch (which) {
    case gl_enum::zero: return blend_factor::zero;
    case gl_enum::src_color: return blend_factor::src_color;
    case gl_enum::one_minus_src_color: return blend_factor::one_minus_src_color;
    case gl_enum::dst_color: return blend_factor::dst_color;
    case gl_enum::one_minus_dst_color: return blend_factor::one_minus_dst_color;
    case gl_enum::src_alpha: return blend_factor::src_alpha;
    case gl_enum::one_minus_src_alpha: return blend_factor::one_minus_src_alpha;
    case gl_enum::dst_alpha: return blend_factor::dst_alpha;
    case gl_enum::one_minus_dst_alpha: return blend_factor::one_minus_dst_alpha;
    default: return blend_factor::one;
    }
}

// How wide one component of an attribute is, in bytes.
[[nodiscard]] int size_of(std::uint32_t type) {
    switch (type) {
    case gl_enum::byte_:
    case gl_enum::unsigned_byte: return 1;
    case gl_enum::short_:
    case gl_enum::unsigned_short: return 2;
    default: return 4;
    }
}

// Read one component out of a buffer at a byte offset, as a float.
//
// `normalized` maps an integer's full range onto 0..1 (or -1..1 for a signed
// one), which is how a colour arrives as four bytes rather than four floats.
[[nodiscard]] float read_component(const std::vector<std::byte> & bytes, std::size_t at,
                                   std::uint32_t type, bool normalized) {
    const auto want = static_cast<std::size_t>(size_of(type));
    if (at + want > bytes.size()) { return 0.0f; }
    const auto * raw = reinterpret_cast<const unsigned char *>(bytes.data() + at);
    switch (type) {
    case gl_enum::float_: {
        float f = 0;
        std::memcpy(&f, raw, sizeof(f));
        return f;
    }
    case gl_enum::unsigned_byte: {
        const float v = static_cast<float>(raw[0]);
        return normalized ? v / 255.0f : v;
    }
    case gl_enum::byte_: {
        const auto v = static_cast<float>(static_cast<signed char>(raw[0]));
        return normalized ? std::max(v / 127.0f, -1.0f) : v;
    }
    case gl_enum::unsigned_short: {
        std::uint16_t v = 0;
        std::memcpy(&v, raw, sizeof(v));
        return normalized ? static_cast<float>(v) / 65535.0f : static_cast<float>(v);
    }
    case gl_enum::short_: {
        std::int16_t v = 0;
        std::memcpy(&v, raw, sizeof(v));
        return normalized ? std::max(static_cast<float>(v) / 32767.0f, -1.0f)
                          : static_cast<float>(v);
    }
    case gl_enum::unsigned_int: {
        std::uint32_t v = 0;
        std::memcpy(&v, raw, sizeof(v));
        return static_cast<float>(v);
    }
    case gl_enum::int_: {
        std::int32_t v = 0;
        std::memcpy(&v, raw, sizeof(v));
        return static_cast<float>(v);
    }
    default: return 0.0f;
    }
}

} // namespace

webgl_context::webgl_context(paint::bitmap * target, int width, int height) {
    resize(target, width, height);
    // SIXTEEN, which is WebGL 1's floor and what a page reads from
    // MAX_VERTEX_ATTRIBS before deciding how much it can pack in.
    attributes_.resize(16);
}

void webgl_context::resize(paint::bitmap * target, int width, int height) {
    framebuffer_.colour = target;
    width_ = width;
    height_ = height;
    framebuffer_.depth.clear();
    // The viewport starts as the whole surface, which is what a context does on
    // creation and what a page that never calls viewport() relies on.
    state_.viewport_x = 0;
    state_.viewport_y = 0;
    state_.viewport_width = width;
    state_.viewport_height = height;
}

// --- objects ---------------------------------------------------------------

std::uint32_t webgl_context::create_buffer() {
    const std::uint32_t id = next_id_++;
    buffers_[id] = {};
    return id;
}

std::uint32_t webgl_context::create_shader(std::uint32_t which) {
    const std::uint32_t id = next_id_++;
    shaders_[id] = gl_shader{which, {}, {}, false, {}};
    return id;
}

std::uint32_t webgl_context::create_program() {
    const std::uint32_t id = next_id_++;
    programs_[id] = {};
    return id;
}

std::uint32_t webgl_context::create_texture() {
    const std::uint32_t id = next_id_++;
    textures_[id] = {};
    return id;
}

void webgl_context::delete_object(std::uint32_t id) {
    buffers_.erase(id);
    shaders_.erase(id);
    programs_.erase(id);
    textures_.erase(id);
    // UNBIND IT TOO. Deleting the bound buffer and leaving the binding is how a
    // later draw reads freed state; GL unbinds on delete and so does this.
    if (array_buffer_ == id) { array_buffer_ = 0; }
    if (element_buffer_ == id) { element_buffer_ = 0; }
    if (current_program_ == id) { current_program_ = 0; }
    for (auto & [unit, texture] : texture_units_) {
        if (texture == id) { texture = 0; }
    }
}

void webgl_context::bind_buffer(std::uint32_t target, std::uint32_t buffer) {
    if (target == gl_enum::element_array_buffer) {
        element_buffer_ = buffer;
    } else {
        array_buffer_ = buffer;
    }
}

void webgl_context::buffer_data(std::uint32_t target, std::vector<std::byte> bytes,
                                std::uint32_t usage) {
    const std::uint32_t which =
        target == gl_enum::element_array_buffer ? element_buffer_ : array_buffer_;
    const auto found = buffers_.find(which);
    if (found == buffers_.end()) {
        // NO BUFFER BOUND. A real driver raises INVALID_OPERATION rather than
        // writing into nothing, and a page that forgot bindBuffer wants to know.
        fail(gl_enum::invalid_operation);
        return;
    }
    found->second.bytes = std::move(bytes);
    found->second.usage = usage;
}

// --- shaders and programs --------------------------------------------------

void webgl_context::shader_source(std::uint32_t shader, std::string source) {
    if (const auto found = shaders_.find(shader); found != shaders_.end()) {
        found->second.source = std::move(source);
    }
}

void webgl_context::compile_shader(std::uint32_t shader) {
    const auto found = shaders_.find(shader);
    if (found == shaders_.end()) { return; }
    glsl::options how;
    how.which =
        found->second.which == gl_enum::vertex_shader ? glsl::stage::vertex : glsl::stage::fragment;
    found->second.compiled = glsl::parse(found->second.source, how);
    found->second.compiled_ok = found->second.compiled.ok;
    found->second.log = found->second.compiled.info_log();
}

bool webgl_context::shader_compiled(std::uint32_t shader) const {
    const auto found = shaders_.find(shader);
    return found != shaders_.end() && found->second.compiled_ok;
}

std::string webgl_context::shader_log(std::uint32_t shader) const {
    const auto found = shaders_.find(shader);
    return found == shaders_.end() ? std::string{} : found->second.log;
}

void webgl_context::attach_shader(std::uint32_t program, std::uint32_t shader) {
    const auto found = programs_.find(program);
    const auto which = shaders_.find(shader);
    if (found == programs_.end() || which == shaders_.end()) { return; }
    if (which->second.which == gl_enum::vertex_shader) {
        found->second.vertex = shader;
    } else {
        found->second.fragment = shader;
    }
}

std::uint32_t gl_type_code(const glsl::type & t) noexcept {
    using glsl::base;
    if (t.kind == base::sampler2d) { return gl_enum::sampler_2d; }
    if (t.kind == base::sampler_cube) { return gl_enum::sampler_cube; }
    if (t.is_matrix()) {
        // WebGL 1 has square matrices only, and the GLSL front end parses no
        // others - so anything unsquare here is a bug rather than a shape to
        // report, and mat4 is the least surprising thing to say about it.
        if (t.cols == 2 && t.rows == 2) { return gl_enum::float_mat2; }
        if (t.cols == 3 && t.rows == 3) { return gl_enum::float_mat3; }
        return gl_enum::float_mat4;
    }
    const std::uint8_t n = t.rows;
    switch (t.kind) {
    case base::i:
        return n == 1   ? gl_enum::int_
               : n == 2 ? gl_enum::int_vec2
               : n == 3 ? gl_enum::int_vec3
                        : gl_enum::int_vec4;
    case base::b:
        return n == 1   ? gl_enum::bool_
               : n == 2 ? gl_enum::bool_vec2
               : n == 3 ? gl_enum::bool_vec3
                        : gl_enum::bool_vec4;
    default:
        return n == 1   ? gl_enum::float_
               : n == 2 ? gl_enum::float_vec2
               : n == 3 ? gl_enum::float_vec3
                        : gl_enum::float_vec4;
    }
}

std::vector<webgl_context::active_variable> webgl_context::active_uniforms(
    std::uint32_t program) const {
    std::vector<active_variable> out;
    const auto found = programs_.find(program);
    if (found == programs_.end() || !found->second.linked) { return out; }
    // Vertex first, then any the fragment shader adds. GL links the two stages
    // into ONE uniform namespace, so a name declared in both is one uniform -
    // which p5's shaders rely on, declaring uModelViewMatrix in each.
    for (const std::uint32_t which : {found->second.vertex, found->second.fragment}) {
        const auto shader = shaders_.find(which);
        if (shader == shaders_.end()) { continue; }
        for (const glsl::interface_variable & v : shader->second.compiled.interface_) {
            if (v.store != glsl::storage::uniform) { continue; }
            const bool already = std::ranges::any_of(
                out, [&](const active_variable & seen) { return seen.name == v.name; });
            if (!already) { out.push_back({v.name, v.t}); }
        }
    }
    return out;
}

std::vector<webgl_context::active_variable> webgl_context::active_attributes(
    std::uint32_t program) const {
    std::vector<active_variable> out;
    const auto found = programs_.find(program);
    if (found == programs_.end() || !found->second.linked) { return out; }
    const auto shader = shaders_.find(found->second.vertex);
    if (shader == shaders_.end()) { return out; }
    // IN THE ORDER link_program ASSIGNED, so index i is location i - which is
    // the whole contract a caller enumerating these depends on.
    for (const std::string & name : found->second.attribute_names) {
        for (const glsl::interface_variable & v : shader->second.compiled.interface_) {
            if (v.store == glsl::storage::attribute && v.name == name) {
                out.push_back({v.name, v.t});
                break;
            }
        }
    }
    return out;
}

void webgl_context::link_program(std::uint32_t program) {
    const auto found = programs_.find(program);
    if (found == programs_.end()) { return; }
    gl_program & p = found->second;
    p.linked = false;
    p.log.clear();
    p.attribute_names.clear();

    const auto vertex = shaders_.find(p.vertex);
    const auto fragment = shaders_.find(p.fragment);
    if (vertex == shaders_.end() || fragment == shaders_.end()) {
        p.log = "the program needs both a vertex and a fragment shader";
        return;
    }
    if (!vertex->second.compiled_ok || !fragment->second.compiled_ok) {
        p.log = "a shader did not compile";
        return;
    }
    // ATTRIBUTE LOCATIONS ARE ASSIGNED HERE, in declaration order. That is what
    // getAttribLocation reports, and a page binds its arrays by the number it
    // gets back - so the order has to be stable across a relink or the arrays
    // point at the wrong data.
    for (const glsl::interface_variable & v : vertex->second.compiled.interface_) {
        if (v.store == glsl::storage::attribute) { p.attribute_names.push_back(v.name); }
    }
    // A VARYING THE FRAGMENT SHADER READS AND THE VERTEX ONE NEVER WRITES is a
    // link error in GL, and a silent black screen without this check.
    for (const glsl::interface_variable & wanted : fragment->second.compiled.interface_) {
        if (wanted.store != glsl::storage::varying) { continue; }
        const bool written = std::ranges::any_of(
            vertex->second.compiled.interface_, [&](const glsl::interface_variable & v) {
                return v.store == glsl::storage::varying && v.name == wanted.name;
            });
        if (!written) {
            p.log = "varying `" + wanted.name +
                    "` is read by the fragment shader but never "
                    "written by the vertex shader";
            return;
        }
    }
    p.linked = true;
}

bool webgl_context::program_linked(std::uint32_t program) const {
    const auto found = programs_.find(program);
    return found != programs_.end() && found->second.linked;
}

std::string webgl_context::program_log(std::uint32_t program) const {
    const auto found = programs_.find(program);
    return found == programs_.end() ? std::string{} : found->second.log;
}

void webgl_context::use_program(std::uint32_t program) {
    current_program_ = program;
}

int webgl_context::attribute_location(std::uint32_t program, const std::string & name) const {
    const auto found = programs_.find(program);
    if (found == programs_.end()) { return -1; }
    for (std::size_t i = 0; i < found->second.attribute_names.size(); ++i) {
        if (found->second.attribute_names[i] == name) { return static_cast<int>(i); }
    }
    // -1 FOR AN ATTRIBUTE THAT IS NOT THERE, which is what GL returns and what a
    // page checks. Returning 0 would silently alias it to the first one.
    return -1;
}

void webgl_context::enable_attribute(int location, bool on) {
    if (location < 0 || static_cast<std::size_t>(location) >= attributes_.size()) { return; }
    attributes_[static_cast<std::size_t>(location)].enabled = on;
}

void webgl_context::attribute_pointer(int location, int size, std::uint32_t type, bool normalized,
                                      int stride, int offset) {
    if (location < 0 || static_cast<std::size_t>(location) >= attributes_.size()) { return; }
    vertex_attribute & attribute = attributes_[static_cast<std::size_t>(location)];
    // THE BUFFER BOUND *NOW* IS THE ONE THIS POINTS AT, for the rest of time.
    // That is the single most surprising thing in the API: `vertexAttribPointer`
    // captures ARRAY_BUFFER at the moment it is called, so binding a different
    // buffer afterwards does not change where this attribute reads from.
    attribute.buffer = array_buffer_;
    attribute.size = size;
    attribute.type = type;
    attribute.normalized = normalized;
    attribute.stride = stride;
    attribute.offset = offset;
}

void webgl_context::set_uniform(const std::string & name, glsl::value v) {
    const auto found = programs_.find(current_program_);
    if (found == programs_.end()) {
        fail(gl_enum::invalid_operation);
        return;
    }
    found->second.uniforms[name] = std::move(v);
}

// --- state -----------------------------------------------------------------

void webgl_context::viewport(int x, int y, int width, int height) {
    state_.viewport_x = x;
    // GL'S ORIGIN IS THE BOTTOM LEFT and a bitmap's is the top left, so a
    // viewport that is not the whole surface has to be flipped too - not just
    // the vertices. A page that draws into the top half asks for the BOTTOM half
    // in GL's terms.
    state_.viewport_y = height_ - y - height;
    state_.viewport_width = width;
    state_.viewport_height = height;
}

void webgl_context::scissor(int x, int y, int width, int height) {
    state_.scissor_x = x;
    state_.scissor_y = height_ - y - height; // same flip, same reason
    state_.scissor_width = width;
    state_.scissor_height = height;
}

void webgl_context::set_enabled(std::uint32_t capability, bool on) {
    switch (capability) {
    case gl_enum::depth_test: state_.depth_enabled = on; break;
    case gl_enum::blend: state_.blend_enabled = on; break;
    case gl_enum::scissor_test: state_.scissor_enabled = on; break;
    case gl_enum::cull_face: state_.cull = on ? cull_mode::back : cull_mode::none; break;
    default: fail(gl_enum::invalid_enum); break;
    }
}

void webgl_context::depth_func(std::uint32_t how) {
    state_.depth = to_depth(how);
}
void webgl_context::depth_mask(bool on) {
    state_.depth_write = on;
}

void webgl_context::blend_func(std::uint32_t source, std::uint32_t destination) {
    state_.source = to_blend(source);
    state_.destination = to_blend(destination);
}

void webgl_context::cull_face(std::uint32_t which) {
    if (state_.cull == cull_mode::none) { return; } // enable(CULL_FACE) decides that
    state_.cull = which == gl_enum::front ? cull_mode::front : cull_mode::back;
}

void webgl_context::front_face(std::uint32_t which) {
    state_.front_face_is_counter_clockwise = which == gl_enum::ccw;
}

void webgl_context::clear_color(float r, float g, float b, float a) {
    clear_[0] = r;
    clear_[1] = g;
    clear_[2] = b;
    clear_[3] = a;
}

void webgl_context::clear_depth(float depth) {
    clear_depth_ = depth;
}

void webgl_context::clear(std::uint32_t mask) {
    if ((mask & gl_enum::color_buffer_bit) != 0 && framebuffer_.colour != nullptr) {
        const auto byte = [](float channel) {
            return static_cast<std::uint32_t>(std::clamp(channel, 0.0f, 1.0f) * 255.0f + 0.5f);
        };
        const std::uint32_t packed = (byte(clear_[3]) << 24) | (byte(clear_[0]) << 16) |
                                     (byte(clear_[1]) << 8) | byte(clear_[2]);
        std::ranges::fill(framebuffer_.colour->pixels, packed);
    }
    if ((mask & gl_enum::depth_buffer_bit) != 0) {
        framebuffer_.ensure_depth(width_, height_);
        std::ranges::fill(framebuffer_.depth, clear_depth_);
    }
}

// --- textures --------------------------------------------------------------

void webgl_context::bind_texture(std::uint32_t target, std::uint32_t texture) {
    (void)target; // TEXTURE_2D only; a cube map binds nothing, per glsl.hpp
    texture_units_[active_unit_] = texture;
}

void webgl_context::active_texture(std::uint32_t unit) {
    // TEXTURE0 + n, which is how the constant is defined - a page passes
    // gl.TEXTURE0 + i, and treating it as a bare index puts everything on 33984.
    active_unit_ = unit >= gl_enum::texture0 ? unit - gl_enum::texture0 : unit;
}

void webgl_context::texture_image(std::uint32_t target, int width, int height,
                                  std::vector<std::byte> pixels) {
    (void)target;
    const auto found = textures_.find(texture_units_[active_unit_]);
    if (found == textures_.end()) {
        fail(gl_enum::invalid_operation);
        return;
    }
    paint::bitmap made{width, height};
    const auto want = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    for (std::size_t i = 0; i + 3 < std::min(want, pixels.size()); i += 4) {
        const auto at = static_cast<int>(i / 4);
        const auto channel = [&](std::size_t which) {
            return static_cast<std::uint32_t>(static_cast<unsigned char>(pixels[i + which]));
        };
        // RGBA IN, ARGB OUT: the byte order a page uploads is not the packing a
        // bitmap uses, and swapping them silently exchanges red and blue.
        made.put(at % width, at / width,
                 (channel(3) << 24) | (channel(0) << 16) | (channel(1) << 8) | channel(2));
    }
    found->second.pixels = std::move(made);
}

void webgl_context::texture_from_bitmap(std::uint32_t target, const paint::bitmap & from) {
    (void)target;
    const auto found = textures_.find(texture_units_[active_unit_]);
    if (found == textures_.end()) {
        fail(gl_enum::invalid_operation);
        return;
    }
    found->second.pixels = from;
}

void webgl_context::texture_parameter(std::uint32_t target, std::uint32_t name,
                                      std::uint32_t value) {
    (void)target;
    const auto found = textures_.find(texture_units_[active_unit_]);
    if (found == textures_.end()) { return; }
    switch (name) {
    case gl_enum::texture_min_filter: found->second.min_filter = value; break;
    case gl_enum::texture_mag_filter: found->second.mag_filter = value; break;
    case gl_enum::texture_wrap_s: found->second.wrap_s = value; break;
    case gl_enum::texture_wrap_t: found->second.wrap_t = value; break;
    default: fail(gl_enum::invalid_enum); break;
    }
}

// --- drawing ---------------------------------------------------------------

attribute_set webgl_context::gather(const gl_program & program, int index) const {
    attribute_set out;
    for (std::size_t location = 0; location < program.attribute_names.size(); ++location) {
        if (location >= attributes_.size()) { break; }
        const vertex_attribute & where = attributes_[location];
        glsl::value made;
        made.t = glsl::type{glsl::base::f, static_cast<std::uint8_t>(std::clamp(where.size, 1, 4)),
                            1, -1, 0};
        made.v.assign(static_cast<std::size_t>(std::clamp(where.size, 1, 4)), 0.0f);
        // A DISABLED ATTRIBUTE IS NOT AN ERROR: it reads as zero, which is what
        // GL does with a constant vertex attribute nobody set. `vec4(0,0,0,1)`
        // would be the strictly correct default; zero is what a page that
        // forgot to enable an array actually sees on a real driver too.
        if (where.enabled) {
            // THE DIVISOR IS THE WHOLE OF INSTANCING. Divisor 0 advances once
            // per vertex - every WebGL 1 attribute - and divisor N advances
            // once per N instances, so one buffer holds per-instance data and
            // `index` is not the row to read. Applied here because this is the
            // one place an attribute row is chosen.
            const int row = where.divisor > 0 ? current_instance_ / where.divisor : index;
            const auto buffer = buffers_.find(where.buffer);
            if (buffer != buffers_.end()) {
                const int component = size_of(where.type);
                // A STRIDE OF ZERO MEANS TIGHTLY PACKED, which is the default
                // and by far the commonest case - treating it as a literal zero
                // makes every vertex read the first one.
                const int stride = where.stride > 0 ? where.stride : component * where.size;
                const auto start = static_cast<std::size_t>(where.offset) +
                                   static_cast<std::size_t>(row) * static_cast<std::size_t>(stride);
                for (int c = 0; c < where.size && c < 4; ++c) {
                    made.v[static_cast<std::size_t>(c)] = read_component(
                        buffer->second.bytes, start + static_cast<std::size_t>(c * component),
                        where.type, where.normalized);
                }
            }
        }
        out.emplace_back(program.attribute_names[location], std::move(made));
    }
    return out;
}

// --- vertex array objects, both spellings -------------------------------
// ONE IMPLEMENTATION. `createVertexArray` on a WebGL 2 context and
// `createVertexArrayOES` on the OES_vertex_array_object object both land here,
// which is the decision docs/webgl2-plan.md records and the reason Phaser can
// verify the WebGL 2 machinery: it reaches VAOs only through the extension.
std::uint32_t webgl_context::create_vertex_array() {
    const std::uint32_t id = next_vao_++;
    vertex_arrays_[id] = gl_vertex_array{};
    return id;
}

void webgl_context::bind_vertex_array(std::uint32_t id) {
    // SWAP, DO NOT COPY-OUT-AND-FORGET. The live table is `attributes_`, so
    // binding means saving it back into whatever was bound and loading the new
    // one - otherwise everything set while a VAO was bound is lost the moment
    // something else binds, which is the failure that makes a VAO look like it
    // works until a second one exists.
    if (id == bound_vao_) { return; }
    if (bound_vao_ != 0) {
        const auto previous = vertex_arrays_.find(bound_vao_);
        if (previous != vertex_arrays_.end()) {
            previous->second.attributes = attributes_;
            previous->second.element_buffer = element_buffer_;
        }
    } else {
        default_vao_.attributes = attributes_;
        default_vao_.element_buffer = element_buffer_;
    }
    if (id == 0) {
        attributes_ = default_vao_.attributes;
        element_buffer_ = default_vao_.element_buffer;
        bound_vao_ = 0;
        return;
    }
    const auto next = vertex_arrays_.find(id);
    if (next == vertex_arrays_.end()) {
        fail(gl_enum::invalid_operation);
        return;
    }
    attributes_ = next->second.attributes;
    element_buffer_ = next->second.element_buffer;
    bound_vao_ = id;
}

void webgl_context::delete_vertex_array(std::uint32_t id) {
    if (id == 0) { return; }
    // Deleting the BOUND one unbinds it first, which is what the spec says and
    // what stops the next bind saving state back into a hole.
    if (id == bound_vao_) { bind_vertex_array(0); }
    vertex_arrays_.erase(id);
}

bool webgl_context::is_vertex_array(std::uint32_t id) const {
    return id != 0 && vertex_arrays_.find(id) != vertex_arrays_.end();
}

void webgl_context::attribute_divisor(int location, int divisor) {
    if (location < 0 || divisor < 0) {
        fail(gl_enum::invalid_value);
        return;
    }
    if (static_cast<std::size_t>(location) >= attributes_.size()) {
        attributes_.resize(static_cast<std::size_t>(location) + 1);
    }
    attributes_[static_cast<std::size_t>(location)].divisor = divisor;
}

int webgl_context::attribute_divisor_of(int location) const {
    if (location < 0 || static_cast<std::size_t>(location) >= attributes_.size()) { return 0; }
    return attributes_[static_cast<std::size_t>(location)].divisor;
}

std::size_t webgl_context::draw_arrays_instanced(std::uint32_t mode, int first, int count,
                                                 int instances) {
    if (instances < 0) {
        fail(gl_enum::invalid_value);
        return 0;
    }
    // ZERO INSTANCES DRAWS NOTHING, and is not an error - a page that computed
    // an empty batch should not have to branch around the call.
    std::size_t painted = 0;
    for (int i = 0; i < instances; ++i) {
        current_instance_ = i;
        painted += draw_arrays(mode, first, count);
    }
    current_instance_ = 0;
    return painted;
}

std::size_t webgl_context::draw_elements_instanced(std::uint32_t mode, int count,
                                                   std::uint32_t type, int offset, int instances) {
    if (instances < 0) {
        fail(gl_enum::invalid_value);
        return 0;
    }
    std::size_t painted = 0;
    for (int i = 0; i < instances; ++i) {
        current_instance_ = i;
        painted += draw_elements(mode, count, type, offset);
    }
    current_instance_ = 0;
    return painted;
}

std::size_t webgl_context::draw_arrays(std::uint32_t mode, int first, int count) {
    if (mode != gl_enum::triangles) {
        // POINTS, LINES and the strips are named in softgl.hpp as not drawn.
        // Reporting INVALID_ENUM is better than drawing nothing in silence: a
        // page that asks for a line strip gets an answer it can read.
        fail(gl_enum::invalid_enum);
        return 0;
    }
    const auto program = programs_.find(current_program_);
    if (program == programs_.end() || !program->second.linked) {
        fail(gl_enum::invalid_operation);
        return 0;
    }
    const auto vertex = shaders_.find(program->second.vertex);
    const auto fragment = shaders_.find(program->second.fragment);
    if (vertex == shaders_.end() || fragment == shaders_.end()) {
        fail(gl_enum::invalid_operation);
        return 0;
    }
    if (count <= 0 || first < 0) { return 0; }

    std::vector<attribute_set> vertices;
    vertices.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) { vertices.push_back(gather(program->second, first + i)); }

    draw_request request;
    request.vertex_shader = &vertex->second.compiled;
    request.fragment_shader = &fragment->second.compiled;
    request.vertices = &vertices;
    request.state = state_;
    request.uniform = [&program](std::string_view name) -> const glsl::value * {
        const auto found = program->second.uniforms.find(name);
        return found == program->second.uniforms.end() ? nullptr : &found->second;
    };
    request.sample = [this](int unit, float s, float t) {
        const auto bound = texture_units_.find(static_cast<std::uint32_t>(unit));
        if (bound == texture_units_.end()) { return glsl::value::vector({0, 0, 0, 1}); }
        const auto texture = textures_.find(bound->second);
        if (texture == textures_.end() || texture->second.pixels.empty()) {
            return glsl::value::vector({0, 0, 0, 1});
        }
        const paint::bitmap & from = texture->second.pixels;
        const auto wrap = [](float value, std::uint32_t how, int size) {
            if (how == gl_enum::clamp_to_edge) {
                return std::clamp(static_cast<int>(value * static_cast<float>(size)), 0, size - 1);
            }
            // REPEAT, and the modulo has to handle a negative coordinate - which
            // a page reaches by scrolling a texture backwards.
            int at = static_cast<int>(std::floor(value * static_cast<float>(size)));
            at %= size;
            return at < 0 ? at + size : at;
        };
        const int x = wrap(s, texture->second.wrap_s, from.width);
        // A TEXTURE'S ORIGIN IS ITS BOTTOM LEFT in GL, and a bitmap's is its top
        // left, so t is flipped - the same reason the viewport is.
        const int y = wrap(1.0f - t, texture->second.wrap_t, from.height);
        const std::uint32_t texel = from.at(x, y);
        return glsl::value::vector({static_cast<float>((texel >> 16) & 0xFF) / 255.0f,
                                    static_cast<float>((texel >> 8) & 0xFF) / 255.0f,
                                    static_cast<float>(texel & 0xFF) / 255.0f,
                                    static_cast<float>((texel >> 24) & 0xFF) / 255.0f});
    };
    // A SHADER THAT FAILS AT RUN TIME IS AN ENGINE CONDITION, not a GL one, and
    // it used to be pure silence. INVALID_OPERATION is the closest GL has, and
    // it means a page checking getError() sees that the draw did not do what it
    // asked instead of an empty canvas with a clean bill of health.
    shader_error_.clear();
    request.shader_error = &shader_error_;
    const std::size_t drawn = draw_triangles(request, framebuffer_);
    if (!shader_error_.empty()) { fail(gl_enum::invalid_operation); }
    return drawn;
}

std::size_t webgl_context::draw_elements(std::uint32_t mode, int count, std::uint32_t type,
                                         int offset) {
    if (mode != gl_enum::triangles) {
        fail(gl_enum::invalid_enum);
        return 0;
    }
    const auto program = programs_.find(current_program_);
    const auto indices = buffers_.find(element_buffer_);
    if (program == programs_.end() || !program->second.linked || indices == buffers_.end()) {
        fail(gl_enum::invalid_operation);
        return 0;
    }
    const auto vertex = shaders_.find(program->second.vertex);
    const auto fragment = shaders_.find(program->second.fragment);
    if (vertex == shaders_.end() || fragment == shaders_.end()) {
        fail(gl_enum::invalid_operation);
        return 0;
    }

    // The index buffer decides the ORDER vertices are gathered in, which is the
    // whole point of an indexed draw: a shared vertex is stored once and used
    // many times.
    std::vector<attribute_set> vertices;
    vertices.reserve(static_cast<std::size_t>(std::max(count, 0)));
    const int component = size_of(type);
    for (int i = 0; i < count; ++i) {
        const auto at = static_cast<std::size_t>(offset) +
                        static_cast<std::size_t>(i) * static_cast<std::size_t>(component);
        const auto index = static_cast<int>(read_component(indices->second.bytes, at, type, false));
        vertices.push_back(gather(program->second, index));
    }

    draw_request request;
    request.vertex_shader = &vertex->second.compiled;
    request.fragment_shader = &fragment->second.compiled;
    request.vertices = &vertices;
    request.state = state_;
    request.uniform = [&program](std::string_view name) -> const glsl::value * {
        const auto found = program->second.uniforms.find(name);
        return found == program->second.uniforms.end() ? nullptr : &found->second;
    };
    // A SHADER THAT FAILS AT RUN TIME IS AN ENGINE CONDITION, not a GL one, and
    // it used to be pure silence. INVALID_OPERATION is the closest GL has, and
    // it means a page checking getError() sees that the draw did not do what it
    // asked instead of an empty canvas with a clean bill of health.
    shader_error_.clear();
    request.shader_error = &shader_error_;
    const std::size_t drawn = draw_triangles(request, framebuffer_);
    if (!shader_error_.empty()) { fail(gl_enum::invalid_operation); }
    return drawn;
}

} // namespace ctbrowser::shell
