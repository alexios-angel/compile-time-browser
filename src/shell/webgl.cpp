#include <ctbrowser/shell/webgl.hpp>

#include <algorithm>
#include <cstdio>
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
    attributes_.resize(max_vertex_attributes);
}

void webgl_context::resize(paint::bitmap * target, int width, int height) {
    framebuffer_.colour = target;
    canvas_ = target;
    // THE DEVICE HAS ITS OWN SURFACE AND IT DOES NOT FOLLOW. An EGL pbuffer is
    // created at a fixed size, so a canvas that resizes after its context was
    // made leaves the two disagreeing - and `read_pixels` then REPLACES the
    // canvas bitmap with one of the device's size, which the compositor draws
    // at the wrong scale or not at all.
    //
    // Remade rather than resized: EGL has no operation for growing a pbuffer,
    // and a page that resizes a canvas loses its GL state in a browser too.
    if (angle_ != nullptr && (width != width_ || height != height_)) {
        angle_.reset();
        width_ = width;
        height_ = height;
        (void)use_angle();
    }
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
    if (angle_ != nullptr) { return angle_->create_buffer(); }
    const std::uint32_t id = next_id_++;
    buffers_[id] = {};
    return id;
}

std::uint32_t webgl_context::create_shader(std::uint32_t which) {
    if (angle_ != nullptr) { return angle_->create_shader(static_cast<int>(which)); }
    const std::uint32_t id = next_id_++;
    shaders_[id] = gl_shader{which, {}, {}, false, {}};
    return id;
}

std::uint32_t webgl_context::create_program() {
    if (angle_ != nullptr) { return angle_->create_program(); }
    const std::uint32_t id = next_id_++;
    programs_[id] = {};
    return id;
}

std::uint32_t webgl_context::create_texture() {
    if (angle_ != nullptr) { return angle_->create_texture(); }
    const std::uint32_t id = next_id_++;
    textures_[id] = {};
    return id;
}

void webgl_context::delete_object(std::uint32_t id) {
    buffers_.erase(id);
    textures_.erase(id);
    const bool was_program = programs_.erase(id) > 0;

    // A SHADER ATTACHED TO A PROGRAM SURVIVES ITS OWN DELETION. `glDeleteShader`
    // flags the object and the driver frees it once nothing references it -
    // which is why the universal idiom is compile, attach, link, delete, and
    // why erasing it here broke every draw that followed.
    //
    // Babylon.js does exactly that, and it cost 119 refused draws a frame: the
    // program linked, the material reported itself ready, and drawElements
    // failed on a program whose shaders had evaporated.
    if (const auto shader = shaders_.find(id); shader != shaders_.end()) {
        if (attached_to_a_program(id)) {
            shader->second.deleted = true;
        } else {
            shaders_.erase(shader);
        }
    }
    // Deleting a PROGRAM can be what finally releases its shaders.
    if (was_program) {
        std::erase_if(shaders_, [this](const auto & entry) {
            return entry.second.deleted && !attached_to_a_program(entry.first);
        });
    }
    // UNBIND IT TOO. Deleting the bound buffer and leaving the binding is how a
    // later draw reads freed state; GL unbinds on delete and so does this.
    if (array_buffer_ == id) { array_buffer_ = 0; }
    if (element_buffer_ == id) { element_buffer_ = 0; }
    if (current_program_ == id) { current_program_ = 0; }
    for (auto & [unit, texture] : texture_units_) {
        if (texture == id) { texture = 0; }
    }
}

bool webgl_context::attached_to_a_program(std::uint32_t shader) const {
    return std::ranges::any_of(programs_, [shader](const auto & entry) {
        return entry.second.vertex == shader || entry.second.fragment == shader;
    });
}

// --- the ANGLE backend ------------------------------------------------------
//
// Stage 2 of docs/angle-plan.md, first increment. The calls
// examples/pages/webgl-triangle.html makes are forwarded to a real GLES device;
// everything else is RECORDED as unforwarded rather than silently ignored,
// because a silent no-op that leaves a black canvas is the exact failure this
// corpus keeps finding.

bool webgl_context::use_angle() {
    if (angle_ != nullptr) { return true; }
    if (!raster::gles::available()) { return false; }
    auto device = std::make_unique<raster::gles::device>(width_, height_);
    if (!device->ok()) { return false; }
    device->viewport(0, 0, width_, height_);
    angle_ = std::move(device);
    return true;
}

void webgl_context::note_unforwarded(std::string_view call) const {
    const std::string name{call};
    // ONCE EACH. A call in a render loop would otherwise be the only thing in
    // the list, which is the same reason `refuse` deduplicates.
    if (std::ranges::find(unforwarded_, name) == unforwarded_.end()) {
        unforwarded_.push_back(name);
    }
}

void webgl_context::bind_buffer(std::uint32_t target, std::uint32_t buffer) {
    if (angle_ != nullptr) {
        angle_->bind_buffer(static_cast<int>(target), buffer);
        return;
    }
    if (target == gl_enum::element_array_buffer) {
        element_buffer_ = buffer;
    } else if (target == gl_enum::uniform_buffer) {
        uniform_buffer_ = buffer;
    } else {
        array_buffer_ = buffer;
    }
}

// WHICH BUFFER A TARGET WRITES TO. It was `element ? element : array`, so a page
// filling a UNIFORM buffer wrote its uniforms over whichever VERTEX buffer
// happened to be bound - which is a wrong picture rather than an error, and the
// worst shape of bug this tree keeps finding.
std::uint32_t webgl_context::buffer_for(std::uint32_t target) const noexcept {
    if (target == gl_enum::element_array_buffer) { return element_buffer_; }
    if (target == gl_enum::uniform_buffer) { return uniform_buffer_; }
    return array_buffer_;
}

void webgl_context::buffer_data(std::uint32_t target, std::vector<std::byte> bytes,
                                std::uint32_t usage) {
    if (angle_ != nullptr) {
        angle_->buffer_data(static_cast<int>(target), bytes.data(), bytes.size(),
                            static_cast<int>(usage));
        return;
    }
    const std::uint32_t which = buffer_for(target);
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

namespace {

// A SHADER THAT USES ES 3.00 SYNTAX AND FORGOT TO SAY SO.
//
// This engine's own GLSL front end is LENIENT by design - it accepts `layout(`
// whatever the `#version` line claims - and a real compiler is not. Babylon's
// processed vertex shader arrives with fifty lines of `#define`, then
// `layout(std140, column_major) uniform;`, and NO `#version` directive at all.
// ANGLE therefore compiles it as ESSL 1.00, where the word does not exist:
//
//     VERTEX SHADER ERROR: 0:54: 'layout' : syntax error
//
// The page is not wrong to expect this to work - it works in a browser, because
// a browser's WebGL 2 context defaults differently - so the version is supplied
// rather than the shader refused. That is the leniency contract this tree
// already keeps, written out for a compiler that does not keep it.
//
// ON `layout(` ALONE, and deliberately narrow: `attribute` and `varying` are ES
// 1.00 spellings that ES 3.00 REMOVED, so a shader using those must NOT be
// given a 300 es header - p5's are exactly that, and promoting them would break
// what currently works.
[[nodiscard]] std::string versioned(const std::string & source) {
    if (source.find("#version") != std::string::npos) { return source; }
    if (source.find("layout(") == std::string::npos) { return source; }
    return "#version 300 es\n" + source;
}

} // namespace

void webgl_context::shader_source(std::uint32_t shader, std::string source) {
    if (angle_ != nullptr) {
        angle_->shader_source(shader, versioned(source));
        return;
    }
    if (const auto found = shaders_.find(shader); found != shaders_.end()) {
        found->second.source = std::move(source);
    }
}

void webgl_context::compile_shader(std::uint32_t shader) {
    if (angle_ != nullptr) {
        angle_->compile_shader(shader);
        return;
    }
    const auto found = shaders_.find(shader);
    if (found == shaders_.end()) { return; }
    glsl::options how;
    how.which =
        found->second.which == gl_enum::vertex_shader ? glsl::stage::vertex : glsl::stage::fragment;
    // `WEBGL2`, WHICH IS WHAT FLIPS p5's PREAMBLE. p5 ships one set of shaders
    // written to macro over the two languages - `#ifdef WEBGL2` selects
    // in/out/a declared output over attribute/varying/gl_FragColor - so
    // defining this is what makes a WebGL 2 context get ES 3.00 source out of
    // the same file.
    if (version_ >= 2) { how.defines.emplace_back("WEBGL2", "1"); }
    found->second.compiled = glsl::parse(found->second.source, how);
    found->second.compiled_ok = found->second.compiled.ok;
    found->second.log = found->second.compiled.info_log();
}

bool webgl_context::shader_compiled(std::uint32_t shader) const {
    if (angle_ != nullptr) { return angle_->shader_compiled(shader); }
    const auto found = shaders_.find(shader);
    return found != shaders_.end() && found->second.compiled_ok;
}

std::string webgl_context::shader_log(std::uint32_t shader) const {
    if (angle_ != nullptr) { return angle_->shader_log(shader); }
    const auto found = shaders_.find(shader);
    return found == shaders_.end() ? std::string{} : found->second.log;
}

void webgl_context::attach_shader(std::uint32_t program, std::uint32_t shader) {
    if (angle_ != nullptr) {
        angle_->attach_shader(program, shader);
        return;
    }
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
    if (angle_ != nullptr) {
        // FROM THE PROGRAM ITSELF. Answering from anywhere else tells a library
        // that enumerates - p5 and Babylon both do - that the shader declares
        // nothing, so it binds nothing and draws nothing, silently.
        for (const raster::gles::device::active & each : angle_->active_uniforms(program)) {
            out.push_back({each.name, each.type, each.size});
        }
        return out;
    }
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
            if (!already) {
                out.push_back({v.name, gl_type_code(v.t), v.t.array > 0 ? v.t.array : 1});
            }
        }
    }
    return out;
}

std::vector<webgl_context::active_variable> webgl_context::active_attributes(
    std::uint32_t program) const {
    std::vector<active_variable> out;
    if (angle_ != nullptr) {
        // FROM THE PROGRAM ITSELF. Answering from anywhere else tells a library
        // that enumerates - p5 and Babylon both do - that the shader declares
        // nothing, so it binds nothing and draws nothing, silently.
        for (const raster::gles::device::active & each : angle_->active_attributes(program)) {
            out.push_back({each.name, each.type, each.size});
        }
        return out;
    }
    const auto found = programs_.find(program);
    if (found == programs_.end() || !found->second.linked) { return out; }
    const auto shader = shaders_.find(found->second.vertex);
    if (shader == shaders_.end()) { return out; }
    // IN THE ORDER link_program ASSIGNED, so index i is location i - which is
    // the whole contract a caller enumerating these depends on.
    for (const std::string & name : found->second.attribute_names) {
        for (const glsl::interface_variable & v : shader->second.compiled.interface_) {
            if (v.store == glsl::storage::attribute && v.name == name) {
                out.push_back({v.name, gl_type_code(v.t), v.t.array > 0 ? v.t.array : 1});
                break;
            }
        }
    }
    return out;
}

void webgl_context::link_program(std::uint32_t program) {
    if (angle_ != nullptr) {
        angle_->link_program(program);
        return;
    }
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
        // WHICH SHADER, AND WHY. `getProgramInfoLog` saying only "a shader did
        // not compile" makes a page report a link failure and say nothing about
        // the cause, which is half a diagnostic - and the compiler already knew
        // the answer.
        p.log = "a shader did not compile";
        if (!vertex->second.compiled_ok && !vertex->second.log.empty()) {
            p.log += "\n  vertex: " + vertex->second.log;
        }
        if (!fragment->second.compiled_ok && !fragment->second.log.empty()) {
            p.log += "\n  fragment: " + fragment->second.log;
        }
        return;
    }
    // ATTRIBUTE LOCATIONS ARE ASSIGNED HERE, in declaration order. That is what
    // getAttribLocation reports, and a page binds its arrays by the number it
    // gets back - so the order has to be stable across a relink or the arrays
    // point at the wrong data.
    for (const glsl::interface_variable & v : vertex->second.compiled.interface_) {
        if (v.store == glsl::storage::attribute) { p.attribute_names.push_back(v.name); }
    }
    // UNIFORM BLOCKS, merged by NAME across the two shaders. A block declared in
    // both is one block with one binding point - the same rule as for a uniform
    // declared in both - and Babylon declares `Scene` and `Material` in each.
    p.blocks.clear();
    p.block_bindings.clear();
    for (const gl_shader * stage : {&vertex->second, &fragment->second}) {
        for (const glsl::shader::uniform_block & block : stage->compiled.blocks) {
            const bool already =
                std::ranges::any_of(p.blocks, [&](const glsl::shader::uniform_block & b) {
                    return b.name == block.name;
                });
            if (!already) {
                p.blocks.push_back(block);
                p.block_bindings.push_back(0);
            }
        }
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
    if (angle_ != nullptr) { return angle_->program_linked(program); }
    const auto found = programs_.find(program);
    return found != programs_.end() && found->second.linked;
}

std::string webgl_context::program_log(std::uint32_t program) const {
    if (angle_ != nullptr) { return angle_->program_log(program); }
    const auto found = programs_.find(program);
    return found == programs_.end() ? std::string{} : found->second.log;
}

void webgl_context::use_program(std::uint32_t program) {
    // TRACKED IN BOTH MODES. On ANGLE the program object belongs to GL, but
    // `set_uniform` still needs to know which one is current to look a name up
    // in it - so this is not software-only state.
    current_program_ = program;
    if (angle_ != nullptr) {
        angle_->use_program(program);
        return;
    }
}

int webgl_context::attribute_location(std::uint32_t program, const std::string & name) const {
    if (angle_ != nullptr) { return angle_->attribute_location(program, name); }
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
    if (angle_ != nullptr) {
        if (location >= 0) { angle_->enable_attribute(static_cast<unsigned>(location), on); }
        return;
    }
    if (location < 0 || static_cast<std::size_t>(location) >= attributes_.size()) { return; }
    attributes_[static_cast<std::size_t>(location)].enabled = on;
}

void webgl_context::attribute_pointer(int location, int size, std::uint32_t type, bool normalized,
                                      int stride, int offset) {
    if (angle_ != nullptr) {
        if (location >= 0) {
            angle_->attribute_pointer(static_cast<unsigned>(location), size, static_cast<int>(type),
                                      normalized, stride,
                                      static_cast<std::size_t>(offset < 0 ? 0 : offset));
        }
        return;
    }
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
    if (angle_ != nullptr) {
        // THE SHAPE COMES FROM THE VALUE, which is how one call covers the
        // whole uniform family. A mat3 arrives as nine floats with cols == 3.
        const int rows = v.t.rows > 0 ? v.t.rows : 1;
        const int cols = v.t.cols > 0 ? v.t.cols : 1;
        const int per = rows * cols;
        const int count = per > 0 ? static_cast<int>(v.v.size()) / per : 1;
        angle_->set_uniform(current_program_, name, v.v.data(), count > 0 ? count : 1, rows, cols,
                            v.t.kind == glsl::base::i || v.t.kind == glsl::base::b);
        return;
    }
    const auto found = programs_.find(current_program_);
    if (found == programs_.end()) {
        fail(gl_enum::invalid_operation);
        return;
    }
    found->second.uniforms[name] = std::move(v);
}

// --- state -----------------------------------------------------------------

void webgl_context::viewport(int x, int y, int width, int height) {
    if (angle_ != nullptr) {
        angle_->viewport(x, y, width, height);
        return;
    }
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
    if (angle_ != nullptr) {
        angle_->set_capability(static_cast<int>(capability), on);
        return;
    }
    switch (capability) {
    case gl_enum::depth_test: state_.depth_enabled = on; break;
    case gl_enum::blend: state_.blend_enabled = on; break;
    case gl_enum::scissor_test: state_.scissor_enabled = on; break;
    case gl_enum::cull_face: state_.cull = on ? cull_mode::back : cull_mode::none; break;
    // POLYGON_OFFSET_FILL, DITHER and RASTERIZER_DISCARD are not implemented,
    // and the two directions are NOT the same answer. Turning one OFF is a
    // no-op that is genuinely correct - it is already off, because this
    // rasteriser never had it - so refusing that is a false alarm, and Babylon
    // trips it on its first frame. Turning one ON is a request this engine
    // cannot honour, and saying nothing would be the silent wrong answer.
    case gl_enum::polygon_offset_fill:
    case gl_enum::dither:
    case gl_enum::rasterizer_discard:
        if (on) { fail(gl_enum::invalid_enum); }
        break;
    default: fail(gl_enum::invalid_enum); break;
    }
}

void webgl_context::depth_func(std::uint32_t how) {
    if (angle_ != nullptr) {
        angle_->depth_func(static_cast<int>(how));
        return;
    }
    state_.depth = to_depth(how);
}
void webgl_context::depth_mask(bool on) {
    if (angle_ != nullptr) {
        angle_->depth_mask(on);
        return;
    }
    state_.depth_write = on;
}

void webgl_context::blend_func(std::uint32_t source, std::uint32_t destination) {
    if (angle_ != nullptr) {
        angle_->blend_func(static_cast<int>(source), static_cast<int>(destination));
        return;
    }
    state_.source = to_blend(source);
    state_.destination = to_blend(destination);
}

void webgl_context::cull_face(std::uint32_t which) {
    // NOT FORWARDED YET: recorded rather than silently ignored. A page
    // whose calls quietly do nothing paints something plausible, which is how
    // a missing uniform cost an afternoon - see tests/webgl_angle.cpp.
    if (angle_ != nullptr) { note_unforwarded("cullFace"); }
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
    if (angle_ != nullptr) {
        // THE COLOUR IS ALREADY SET: `clearColor` stored it in `clear_` above,
        // and GL keeps it as state exactly the same way, so there is nothing to
        // pass here beyond the mask - which is why clearColor needs no
        // forwarding of its own.
        angle_->clear(clear_[0], clear_[1], clear_[2], clear_[3]);
        return;
    }
    if ((mask & gl_enum::color_buffer_bit) != 0 && framebuffer_.colour != nullptr) {
        const auto byte = [](float channel) {
            return static_cast<std::uint32_t>(std::clamp(channel, 0.0f, 1.0f) * 255.0f + 0.5f);
        };
        const std::uint32_t packed = (byte(clear_[3]) << 24) | (byte(clear_[0]) << 16) |
                                     (byte(clear_[1]) << 8) | byte(clear_[2]);
        std::ranges::fill(framebuffer_.colour->pixels, packed);
    }
    if ((mask & gl_enum::depth_buffer_bit) != 0) {
        // SIZED TO WHAT IS BOUND, not to the canvas. A render target is usually
        // a different size - a shadow map especially - and a depth buffer sized
        // to the window would compare a fragment against a neighbour's depth.
        const int depth_width =
            framebuffer_.colour != nullptr ? framebuffer_.colour->width : width_;
        const int depth_height =
            framebuffer_.colour != nullptr ? framebuffer_.colour->height : height_;
        framebuffer_.ensure_depth(depth_width, depth_height);
        std::ranges::fill(framebuffer_.depth, clear_depth_);
    }
}

// --- framebuffer objects ----------------------------------------------------
//
// RENDER TO A TEXTURE, which is what a post-process, a shadow map and every
// RenderTargetTexture are built on. `bindFramebuffer` used to record an id and
// change nothing: the scene went to the CANVAS while the page believed it was
// filling a texture, and whatever sampled that texture afterwards read an empty
// one. Babylon's IDENTITY post-process - a pass that copies the frame and
// changes nothing - turned a working scene into a black canvas that way, with
// no GL error anywhere and `checkFramebufferStatus` answering COMPLETE.

void webgl_context::framebuffer_texture(std::uint32_t attachment, std::uint32_t texture) {
    // NOT FORWARDED YET: recorded rather than silently ignored. A page
    // whose calls quietly do nothing paints something plausible, which is how
    // a missing uniform cost an afternoon - see tests/webgl_angle.cpp.
    if (angle_ != nullptr) { note_unforwarded("framebufferTexture2D"); }
    if (bound_framebuffer_ == 0) {
        // Attaching to the default framebuffer is meaningless and GL says so.
        fail(gl_enum::invalid_operation);
        return;
    }
    if (attachment != gl_enum::color_attachment0) {
        // DEPTH and STENCIL attachments are not implemented - the depth buffer
        // here belongs to the framebuffer rather than to a texture a page can
        // sample. Refused by name rather than silently ignored, because a page
        // reading back a depth texture would get a wrong picture.
        fail(gl_enum::invalid_enum);
        return;
    }
    framebuffers_[bound_framebuffer_].colour_texture = texture;
    // AND RE-POINT IMMEDIATELY IF IT IS THE BOUND ONE: a page attaches after
    // binding, which is the usual order, and the redirect has to follow.
    bind_framebuffer(bound_framebuffer_);
}

void webgl_context::bind_framebuffer(std::uint32_t id) {
    // NOT FORWARDED YET: recorded rather than silently ignored. A page
    // whose calls quietly do nothing paints something plausible, which is how
    // a missing uniform cost an afternoon - see tests/webgl_angle.cpp.
    if (angle_ != nullptr) { note_unforwarded("bindFramebuffer"); }
    bound_framebuffer_ = id;
    if (id == 0) {
        framebuffer_.colour = canvas_;
        framebuffer_.depth.clear();
        return;
    }
    gl_framebuffer & target = framebuffers_[id];
    const auto texture = textures_.find(target.colour_texture);
    if (texture == textures_.end() || texture->second.pixels.empty()) {
        // AN INCOMPLETE FRAMEBUFFER DRAWS NOWHERE, which is what GL does and is
        // much better than drawing to the canvas by accident - that was the old
        // behaviour and it is how a post-process erased a scene.
        framebuffer_.colour = nullptr;
        return;
    }
    framebuffer_.colour = &texture->second.pixels;
    // ITS OWN DEPTH, swapped in with it. Sharing one buffer across targets of
    // different sizes is the same class of bug as sharing the attribute table
    // across vertex arrays, which this engine has already paid for once.
    framebuffer_.depth.swap(target.depth);
}

std::uint32_t webgl_context::framebuffer_status() const {
    if (bound_framebuffer_ == 0) { return gl_enum::framebuffer_complete; }
    const auto found = framebuffers_.find(bound_framebuffer_);
    if (found == framebuffers_.end() || found->second.colour_texture == 0) {
        return gl_enum::framebuffer_incomplete_attachment;
    }
    const auto texture = textures_.find(found->second.colour_texture);
    if (texture == textures_.end() || texture->second.pixels.empty()) {
        return gl_enum::framebuffer_incomplete_attachment;
    }
    return gl_enum::framebuffer_complete;
}

// --- textures --------------------------------------------------------------

void webgl_context::bind_texture(std::uint32_t target, std::uint32_t texture) {
    if (angle_ != nullptr) {
        angle_->bind_texture(static_cast<int>(target), texture);
        return;
    }
    (void)target; // TEXTURE_2D only; a cube map binds nothing, per glsl.hpp
    texture_units_[active_unit_] = texture;
}

void webgl_context::active_texture(std::uint32_t unit) {
    if (angle_ != nullptr) {
        angle_->active_texture(static_cast<int>(unit));
        return;
    }
    // TEXTURE0 + n, which is how the constant is defined - a page passes
    // gl.TEXTURE0 + i, and treating it as a bare index puts everything on 33984.
    active_unit_ = unit >= gl_enum::texture0 ? unit - gl_enum::texture0 : unit;
}

void webgl_context::texture_image(std::uint32_t target, int width, int height,
                                  std::vector<std::byte> pixels) {
    if (angle_ != nullptr) {
        angle_->texture_image(static_cast<int>(target), width, height, pixels.data());
        return;
    }
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
    if (angle_ != nullptr) {
        // ARGB OUT, RGBA IN - the same swap read_pixels does in the other
        // direction, and the same red-and-blue-exchanged picture if it is
        // skipped.
        std::vector<unsigned char> rgba(static_cast<std::size_t>(from.width) *
                                        static_cast<std::size_t>(from.height) * 4);
        for (int y = 0; y < from.height; ++y) {
            for (int x = 0; x < from.width; ++x) {
                const std::uint32_t argb = from.at(x, y);
                const std::size_t at =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(from.width) +
                     static_cast<std::size_t>(x)) *
                    4;
                rgba[at + 0] = static_cast<unsigned char>((argb >> 16) & 0xFF);
                rgba[at + 1] = static_cast<unsigned char>((argb >> 8) & 0xFF);
                rgba[at + 2] = static_cast<unsigned char>(argb & 0xFF);
                rgba[at + 3] = static_cast<unsigned char>((argb >> 24) & 0xFF);
            }
        }
        angle_->texture_image(static_cast<int>(target), from.width, from.height, rgba.data());
        return;
    }
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
    if (angle_ != nullptr) {
        angle_->texture_parameter(static_cast<int>(target), static_cast<int>(name),
                                  static_cast<int>(value));
        return;
    }
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
    // NEVER FEWER SLOTS THAN THE CONTEXT STARTED WITH. The table is sized when
    // a vertex array is created, so this cannot fire today - it is here because
    // the failure it prevents is invisible: a short table makes every attribute
    // setter a silent no-op rather than an error, and the draw simply paints
    // nothing.
    if (attributes_.size() < max_vertex_attributes) { attributes_.resize(max_vertex_attributes); }
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

void webgl_context::buffer_sub_data(std::uint32_t target, int offset,
                                    std::span<const std::byte> bytes) {
    const std::uint32_t which = buffer_for(target);
    const auto found = buffers_.find(which);
    if (found == buffers_.end()) {
        fail(gl_enum::invalid_operation);
        return;
    }
    if (offset < 0) {
        fail(gl_enum::invalid_value);
        return;
    }
    // PAST THE END IS AN ERROR, NOT A RESIZE. bufferSubData updates a range of
    // a buffer that bufferData already sized; growing it here would let a page
    // that miscounted keep working locally and then fail on a real driver,
    // which is the wrong direction for a difference to point.
    const auto at = static_cast<std::size_t>(offset);
    if (at + bytes.size() > found->second.bytes.size()) {
        fail(gl_enum::invalid_value);
        return;
    }
    std::copy(bytes.begin(), bytes.end(), found->second.bytes.begin() + offset);
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

const vertex_attribute * webgl_context::attribute_at(int location) const {
    if (location < 0 || static_cast<std::size_t>(location) >= attributes_.size()) {
        return nullptr;
    }
    return &attributes_[static_cast<std::size_t>(location)];
}

int webgl_context::attribute_divisor_of(int location) const {
    if (location < 0 || static_cast<std::size_t>(location) >= attributes_.size()) { return 0; }
    return attributes_[static_cast<std::size_t>(location)].divisor;
}

std::size_t webgl_context::draw_arrays_instanced(std::uint32_t mode, int first, int count,
                                                 int instances) {
    // NOT FORWARDED YET: recorded rather than silently ignored. A page
    // whose calls quietly do nothing paints something plausible, which is how
    // a missing uniform cost an afternoon - see tests/webgl_angle.cpp.
    if (angle_ != nullptr) { note_unforwarded("drawArraysInstanced"); }
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
    // NOT FORWARDED YET: recorded rather than silently ignored. A page
    // whose calls quietly do nothing paints something plausible, which is how
    // a missing uniform cost an afternoon - see tests/webgl_angle.cpp.
    if (angle_ != nullptr) { note_unforwarded("drawElementsInstanced"); }
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

// A STRIP OR A FAN, AS INDEPENDENT TRIANGLES.
//
// The rasteriser takes triangles three vertices at a time, so a strip is
// expanded here rather than taught to it - N vertices become N-2 triangles, and
// the winding ALTERNATES: (0,1,2), (2,1,3), (2,3,4)... Getting that flip wrong
// makes every other triangle back-facing, which is invisible until something
// enables culling and then looks like a hole rather than a winding bug.
//
// A fan is the simpler one: every triangle shares vertex 0.
//
// WORTH DOING BECAUSE PHASER ASKS. Its WebGL renderer defaults to
// TRIANGLE_STRIP topology, so `drawArrays(gl.TRIANGLE_STRIP, ...)` was the
// first thing it did and INVALID_ENUM was the answer - the renderer initialised,
// ran, and painted nothing.
[[nodiscard]] std::vector<int> expand_topology(std::uint32_t mode, int first, int count) {
    std::vector<int> out;
    if (mode == gl_enum::triangles) {
        out.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) { out.push_back(first + i); }
        return out;
    }
    if (count < 3) { return out; }
    out.reserve(static_cast<std::size_t>(count - 2) * 3U);
    if (mode == gl_enum::triangle_fan) {
        for (int i = 1; i + 1 < count; ++i) {
            out.push_back(first);
            out.push_back(first + i);
            out.push_back(first + i + 1);
        }
        return out;
    }
    for (int i = 0; i + 2 < count; ++i) {
        if ((i % 2) == 0) {
            out.push_back(first + i);
            out.push_back(first + i + 1);
        } else {
            out.push_back(first + i + 1);
            out.push_back(first + i);
        }
        out.push_back(first + i + 2);
    }
    return out;
}

std::size_t webgl_context::draw_arrays(std::uint32_t mode, int first, int count) {
    if (angle_ != nullptr) {
        angle_->draw_arrays(static_cast<int>(mode), first, count);
        // AND STRAIGHT BACK INTO THE CANVAS. The page composites a
        // paint::bitmap, so a draw that stayed on the device would be invisible
        // however correct it was. Per draw is wasteful and per FRAME is what
        // this should become - but the frame boundary is the compositor's and
        // this is the increment that proves the path, so it is honest about
        // costing more than it needs to.
        if (framebuffer_.colour != nullptr) { (void)angle_->read_pixels(*framebuffer_.colour); }
        return static_cast<std::size_t>(count);
    }
    if (mode != gl_enum::triangles && mode != gl_enum::triangle_strip &&
        mode != gl_enum::triangle_fan) {
        // POINTS and LINES are named in softgl.hpp as not drawn. Reporting
        // INVALID_ENUM is better than drawing nothing in silence: a page that
        // asks for a line strip gets an answer it can read.
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

    const std::vector<int> order = expand_topology(mode, first, count);
    std::vector<attribute_set> vertices;
    vertices.reserve(order.size());
    for (const int index : order) { vertices.push_back(gather(program->second, index)); }

    draw_request request;
    request.vertex_shader = &vertex->second.compiled;
    request.fragment_shader = &fragment->second.compiled;
    request.vertices = &vertices;
    request.state = state_;
    uniform_cache block_values;
    request.uniform = [this, &program, &block_values](std::string_view name) {
        return uniform_for_draw(program->second, name, block_values);
    };
    request.sample = sampler();
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

// ONE SAMPLER FOR EVERY DRAW PATH, and the reason it is a function rather than
// a lambda written twice is a real bug: `drawArrays` set `request.sample` and
// `drawElements` DID NOT. Every indexed draw therefore sampled (0, 0, 0, 1) -
// black - however correct the texture was, and an indexed draw is what a mesh
// is. Babylon's every textured material came out black while a hand-written
// `drawArrays` quad through the same context sampled perfectly, which is what
// made it look like a Babylon problem for a while.
//
// Two copies of one job is the shape this tree has paid for three times now -
// two URL parsers, two base64 decoders, two sampler callbacks - so there is one.
std::function<raster::glsl::value(int, float, float)> webgl_context::sampler() const {
    return [this](int unit, float s, float t) {
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
}

// --- uniform blocks ---------------------------------------------------------
//
// WHY THIS EXISTS AT ALL: WebGL 2 delivers uniforms through BUFFERS rather than
// through `uniform4fv` calls, and Babylon uses them for everything the moment
// it sees a WebGL 2 context. Without them its shaders link, its draws are
// issued, and every matrix reads as zero - so the geometry collapses to a point
// and the canvas keeps the colour it was cleared to. Nothing errors.

int webgl_context::get_uniform_block_index(std::uint32_t program, std::string_view name) const {
    const auto found = programs_.find(program);
    if (found == programs_.end()) { return -1; }
    for (std::size_t i = 0; i < found->second.blocks.size(); ++i) {
        if (found->second.blocks[i].name == name) { return static_cast<int>(i); }
    }
    // INVALID_INDEX, which is what GL returns for a name no block has. It is
    // 0xFFFFFFFF and not an error: a page may ask about a block its shader
    // dropped, and Babylon does.
    return -1;
}

void webgl_context::uniform_block_binding(std::uint32_t program, std::uint32_t index,
                                          std::uint32_t binding) {
    // NOT FORWARDED YET: recorded rather than silently ignored. A page
    // whose calls quietly do nothing paints something plausible, which is how
    // a missing uniform cost an afternoon - see tests/webgl_angle.cpp.
    if (angle_ != nullptr) { note_unforwarded("uniformBlockBinding"); }
    const auto found = programs_.find(program);
    if (found == programs_.end() || index >= found->second.block_bindings.size()) {
        fail(gl_enum::invalid_value);
        return;
    }
    found->second.block_bindings[index] = binding;
}

void webgl_context::bind_buffer_base(std::uint32_t target, std::uint32_t index,
                                     std::uint32_t buffer) {
    // NOT FORWARDED YET: recorded rather than silently ignored. A page
    // whose calls quietly do nothing paints something plausible, which is how
    // a missing uniform cost an afternoon - see tests/webgl_angle.cpp.
    if (angle_ != nullptr) { note_unforwarded("bindBufferBase"); }
    if (target != gl_enum::uniform_buffer) {
        // TRANSFORM FEEDBACK also uses bindBufferBase and is not implemented -
        // refused by name rather than silently binding a uniform block.
        fail(gl_enum::invalid_enum);
        return;
    }
    uniform_buffer_bindings_[index] = buffer;
}

namespace {

// ONE VALUE OUT OF A std140 BUFFER.
//
// The layout is the specification's and the page fills the buffer to it, so
// these are not choices: a scalar or a vec2 is packed, a vec3 and a vec4 both
// occupy 16 bytes, and a matrix is COLUMNS each padded to 16. A mat4 is
// therefore contiguous and a mat3 is NOT - its three columns sit 16 bytes apart
// with three floats used out of each four - so reading it as nine contiguous
// floats gives a rotation built out of the wrong numbers, which looks like a
// plausible wrong answer rather than a fault.
[[nodiscard]] float read_float(std::span<const std::byte> bytes, std::size_t at) {
    float out = 0.0f;
    if (at + sizeof(float) <= bytes.size()) { std::memcpy(&out, bytes.data() + at, sizeof(float)); }
    return out;
}

[[nodiscard]] raster::glsl::value read_std140(std::span<const std::byte> bytes, std::size_t base,
                                              const raster::glsl::type & t) {
    raster::glsl::value out;
    out.t = t;
    const std::size_t rows = std::max<std::size_t>(1, t.rows);
    const std::size_t cols = std::max<std::size_t>(1, t.cols);
    const std::size_t elements = t.array > 0 ? static_cast<std::size_t>(t.array) : 1;
    // An array's elements stride by 16 however small the element is, and a
    // matrix's columns do the same.
    const std::size_t element_stride = t.is_matrix() ? 16 * cols : 16;
    for (std::size_t e = 0; e < elements; ++e) {
        const std::size_t element = base + (t.array > 0 ? e * element_stride : 0);
        for (std::size_t c = 0; c < cols; ++c) {
            const std::size_t column = element + (t.is_matrix() ? c * 16 : 0);
            for (std::size_t r = 0; r < rows; ++r) {
                out.v.push_back(read_float(bytes, column + r * sizeof(float)));
            }
        }
    }
    return out;
}

} // namespace

// WHAT A DRAW READS A UNIFORM OUT OF. A name set by `uniform4fv` lives on the
// program; a name that belongs to a uniform block lives in a BUFFER, and which
// buffer depends on the block's binding point. Both look the same to the
// shader, which is the point.
//
// BUILT INTO A CACHE THE DRAW OWNS, because the evaluator takes a pointer and
// the value has to outlive the call that made it.
const raster::glsl::value * webgl_context::uniform_for_draw(const gl_program & program,
                                                            std::string_view name,
                                                            uniform_cache & cache) const {
    const auto direct = program.uniforms.find(name);
    if (direct != program.uniforms.end()) { return &direct->second; }
    if (const auto made = cache.find(name); made != cache.end()) { return &made->second; }

    for (std::size_t i = 0; i < program.blocks.size(); ++i) {
        const glsl::shader::uniform_block & block = program.blocks[i];
        const auto point = uniform_buffer_bindings_.find(program.block_bindings[i]);
        if (point == uniform_buffer_bindings_.end()) { continue; }
        const auto buffer = buffers_.find(point->second);
        if (buffer == buffers_.end()) { continue; }
        const std::span<const std::byte> bytes{buffer->second.bytes};

        // A BLOCK WITH AN INSTANCE NAME is one struct-typed uniform, and the
        // body reads `light0.member`. The value is the members' floats in
        // declaration order - which is how the front end built the struct - and
        // the declared type it is converted to carries the struct index for
        // whichever shader is asking.
        if (!block.instance.empty()) {
            if (block.instance != name) { continue; }
            glsl::value whole;
            whole.t = glsl::type{glsl::base::struct_, 1, 1, -1, 0};
            for (const glsl::shader::uniform_block::member & each : block.members) {
                const glsl::value part = read_std140(bytes, each.offset, each.t);
                for (std::size_t k = 0; k < part.v.size(); ++k) { whole.v.push_back(part.v[k]); }
            }
            return &cache.emplace(std::string{name}, std::move(whole)).first->second;
        }
        for (const glsl::shader::uniform_block::member & each : block.members) {
            if (each.name != name) { continue; }
            return &cache.emplace(std::string{name}, read_std140(bytes, each.offset, each.t))
                        .first->second;
        }
    }
    return nullptr;
}

std::size_t webgl_context::draw_elements(std::uint32_t mode, int count, std::uint32_t type,
                                         int offset) {
    if (angle_ != nullptr) {
        angle_->draw_elements(static_cast<int>(mode), count, static_cast<int>(type),
                              static_cast<std::size_t>(offset < 0 ? 0 : offset));
        // Straight back into the canvas, for the reason draw_arrays gives.
        if (framebuffer_.colour != nullptr) { (void)angle_->read_pixels(*framebuffer_.colour); }
        return static_cast<std::size_t>(count);
    }
    if (mode != gl_enum::triangles && mode != gl_enum::triangle_strip &&
        mode != gl_enum::triangle_fan) {
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
    // THE TOPOLOGY IS EXPANDED OVER THE INDEX POSITIONS, not over the indices
    // themselves: a strip's alternating winding is about which slots of the
    // index buffer form each triangle, and the value read from each slot is
    // then the vertex. Expanding the values instead would give the right
    // triangles from the wrong buffer positions the moment indices repeat.
    const std::vector<int> order = expand_topology(mode, 0, count);
    std::vector<attribute_set> vertices;
    vertices.reserve(order.size());
    const int component = size_of(type);
    for (const int slot : order) {
        const auto at = static_cast<std::size_t>(offset) +
                        static_cast<std::size_t>(slot) * static_cast<std::size_t>(component);
        const auto index = static_cast<int>(read_component(indices->second.bytes, at, type, false));
        vertices.push_back(gather(program->second, index));
    }

    draw_request request;
    request.vertex_shader = &vertex->second.compiled;
    request.fragment_shader = &fragment->second.compiled;
    request.vertices = &vertices;
    request.state = state_;
    uniform_cache block_values;
    request.uniform = [this, &program, &block_values](std::string_view name) {
        return uniform_for_draw(program->second, name, block_values);
    };
    request.sample = sampler();
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
