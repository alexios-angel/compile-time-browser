// gl::device - the objects a page creates: shaders and programs and their
// uniforms, buffers, vertex attributes and arrays, textures, framebuffers and
// renderbuffers.
//
// One of four files carved out of a 1,162-line Raster/gl.cpp on 2026-09-08.
// All are member functions of the one class declared in
// include/ctbrowser/raster/gl.hpp; the pimpl both branches define is in
// internal.hpp beside this, with the includes gl.cpp had. Nothing about the
// public header changed.

#include "internal.hpp"

namespace ctbrowser::raster::gl {

#if CTBROWSER_WITH_ANGLE

namespace {

[[nodiscard]] std::string log_of(unsigned object, bool is_program) {
    GLint length = 0;
    if (is_program) {
        glGetProgramiv(object, GL_INFO_LOG_LENGTH, &length);
    } else {
        glGetShaderiv(object, GL_INFO_LOG_LENGTH, &length);
    }
    if (length <= 1) { return {}; }
    std::string text(static_cast<std::size_t>(length), '\0');
    GLsizei written = 0;
    if (is_program) {
        glGetProgramInfoLog(object, length, &written, text.data());
    } else {
        glGetShaderInfoLog(object, length, &written, text.data());
    }
    text.resize(static_cast<std::size_t>(written));
    return text;
}

// ASKED OF THE PROGRAM, one index at a time, which is the only source that can
// answer correctly. `enumerate` is shared because glGetActiveAttrib and
// glGetActiveUniform differ only in which pair of entry points they call.
template <typename Count, typename Get>
[[nodiscard]] std::vector<device::active> enumerate(unsigned program, Count count, Get get) {
    GLint total = 0;
    count(program, &total);
    GLint longest = 0;
    std::vector<device::active> out;
    for (GLint i = 0; i < total; ++i) {
        std::string name(256, '\0');
        GLsizei written = 0;
        GLint size = 0;
        GLenum type = 0;
        get(program, static_cast<GLuint>(i), static_cast<GLsizei>(name.size()), &written, &size,
            &type, name.data());
        name.resize(static_cast<std::size_t>(written));
        out.push_back({name, static_cast<std::uint32_t>(type), static_cast<int>(size)});
    }
    (void)longest;
    return out;
}

} // namespace

unsigned device::create_shader(int kind) {
    return ok() ? glCreateShader(static_cast<GLenum>(kind)) : 0u;
}

void device::shader_source(unsigned shader, const std::string & source) {
    if (!ok()) { return; }
    const char * text = source.c_str();
    const auto length = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &text, &length);
}

void device::compile_shader(unsigned shader) {
    if (ok()) { glCompileShader(shader); }
}

bool device::shader_compiled(unsigned shader) const {
    if (!ok()) { return false; }
    GLint status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    return status == GL_TRUE;
}

std::string device::shader_log(unsigned shader) const {
    return ok() ? log_of(shader, false) : std::string{};
}

unsigned device::create_program() {
    return ok() ? glCreateProgram() : 0u;
}

void device::attach_shader(unsigned program, unsigned shader) {
    if (ok()) { glAttachShader(program, shader); }
}

void device::link_program(unsigned program) {
    if (ok()) { glLinkProgram(program); }
}

bool device::program_linked(unsigned program) const {
    if (!ok()) { return false; }
    GLint status = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    return status == GL_TRUE;
}

std::string device::program_log(unsigned program) const {
    return ok() ? log_of(program, true) : std::string{};
}

void device::use_program(unsigned program) {
    if (ok()) { glUseProgram(program); }
}

unsigned device::program_in_use() const {
    if (!ok()) { return 0u; }
    GLint current = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &current);
    return static_cast<unsigned>(current);
}

std::vector<device::active> device::active_attributes(unsigned program) const {
    if (!ok()) { return {}; }
    return enumerate(
        program, [](unsigned p, GLint * n) { glGetProgramiv(p, GL_ACTIVE_ATTRIBUTES, n); },
        glGetActiveAttrib);
}

std::vector<device::active> device::active_uniforms(unsigned program) const {
    if (!ok()) { return {}; }
    return enumerate(
        program, [](unsigned p, GLint * n) { glGetProgramiv(p, GL_ACTIVE_UNIFORMS, n); },
        glGetActiveUniform);
}

int device::attribute_location(unsigned program, const std::string & name) const {
    return ok() ? glGetAttribLocation(program, name.c_str()) : -1;
}

int device::uniform_location(unsigned program, const std::string & name) const {
    return ok() ? glGetUniformLocation(program, name.c_str()) : -1;
}

int device::uniform_block_index(unsigned program, const std::string & name) const {
    if (!ok()) { return -1; }
    const GLuint index = glGetUniformBlockIndex(program, name.c_str());
    return index == GL_INVALID_INDEX ? -1 : static_cast<int>(index);
}

void device::uniform_block_binding(unsigned program, unsigned index, unsigned binding) {
    if (ok()) { glUniformBlockBinding(program, index, binding); }
}

void device::set_uniform(int location, const float * values, int count, int rows, int cols,
                         bool integer) {
    if (!ok() || location < 0 || values == nullptr || count <= 0) { return; }
    if (cols > 1) {
        // A MATRIX. Never transposed: WebGL requires the flag to be false and a
        // page's data is already column-major.
        const GLsizei n = static_cast<GLsizei>(count);
        if (rows == 2) { glUniformMatrix2fv(location, n, GL_FALSE, values); }
        if (rows == 3) { glUniformMatrix3fv(location, n, GL_FALSE, values); }
        if (rows == 4) { glUniformMatrix4fv(location, n, GL_FALSE, values); }
        return;
    }
    const GLsizei n = static_cast<GLsizei>(count);
    if (integer) {
        std::vector<GLint> whole(static_cast<std::size_t>(count) * static_cast<std::size_t>(rows));
        for (std::size_t i = 0; i < whole.size(); ++i) { whole[i] = static_cast<GLint>(values[i]); }
        if (rows == 1) { glUniform1iv(location, n, whole.data()); }
        if (rows == 2) { glUniform2iv(location, n, whole.data()); }
        if (rows == 3) { glUniform3iv(location, n, whole.data()); }
        if (rows == 4) { glUniform4iv(location, n, whole.data()); }
        return;
    }
    if (rows == 1) { glUniform1fv(location, n, values); }
    if (rows == 2) { glUniform2fv(location, n, values); }
    if (rows == 3) { glUniform3fv(location, n, values); }
    if (rows == 4) { glUniform4fv(location, n, values); }
}

unsigned device::create_buffer() {
    if (!ok()) { return 0u; }
    GLuint name = 0;
    glGenBuffers(1, &name);
    return name;
}

void device::bind_buffer(int target, unsigned buffer) {
    if (ok()) { glBindBuffer(static_cast<GLenum>(target), buffer); }
}

void device::buffer_data(int target, const void * bytes, std::size_t size, int usage) {
    if (!ok()) { return; }
    glBufferData(static_cast<GLenum>(target), static_cast<GLsizeiptr>(size), bytes,
                 static_cast<GLenum>(usage));
}

void device::buffer_sub_data(int target, std::size_t offset, const void * bytes, std::size_t size) {
    if (!ok() || bytes == nullptr) { return; }
    glBufferSubData(static_cast<GLenum>(target), static_cast<GLintptr>(offset),
                    static_cast<GLsizeiptr>(size), bytes);
}

void device::bind_buffer_base(int target, unsigned index, unsigned buffer) {
    if (ok()) { glBindBufferBase(static_cast<GLenum>(target), index, buffer); }
}

unsigned device::bound_buffer(int target) const {
    if (!ok()) { return 0u; }
    // A TARGET AND ITS BINDING QUERY ARE DIFFERENT NUMBERS, and there is no
    // arithmetic between them - GL_ARRAY_BUFFER is 0x8892 and its query 0x8894,
    // but GL_UNIFORM_BUFFER is 0x8A11 and its query 0x8A28. A table, not a
    // formula.
    GLenum query = 0;
    switch (static_cast<GLenum>(target)) {
    case GL_ARRAY_BUFFER: query = GL_ARRAY_BUFFER_BINDING; break;
    case GL_ELEMENT_ARRAY_BUFFER: query = GL_ELEMENT_ARRAY_BUFFER_BINDING; break;
    case GL_UNIFORM_BUFFER: query = GL_UNIFORM_BUFFER_BINDING; break;
    case GL_COPY_READ_BUFFER: query = GL_COPY_READ_BUFFER_BINDING; break;
    case GL_COPY_WRITE_BUFFER: query = GL_COPY_WRITE_BUFFER_BINDING; break;
    case GL_PIXEL_PACK_BUFFER: query = GL_PIXEL_PACK_BUFFER_BINDING; break;
    case GL_PIXEL_UNPACK_BUFFER: query = GL_PIXEL_UNPACK_BUFFER_BINDING; break;
    default: return 0u;
    }
    GLint bound = 0;
    glGetIntegerv(query, &bound);
    return static_cast<unsigned>(bound);
}

void device::delete_object(object_kind kind, unsigned name) {
    if (!ok() || name == 0) { return; }

    // WHAT ELSE WEARS THIS NUMBER. Off unless asked for, and it exists because
    // the bug it documents was invisible for five commits: the collision is
    // silent, GL raises nothing, and the damage only surfaces later as a
    // wrongly-sized buffer. One line per delete says whether the namespaces
    // really do overlap on this page rather than leaving it an argument.
    const char * trace = std::getenv("CTBROWSER_GL_DELETE");
    if (trace != nullptr && std::string_view{trace} != "0") {
        std::fprintf(stderr, "[del] kind=%d name=%u  also:%s%s%s%s%s%s\n", static_cast<int>(kind),
                     name, glIsBuffer(name) == GL_TRUE ? " buffer" : "",
                     glIsTexture(name) == GL_TRUE ? " texture" : "",
                     glIsFramebuffer(name) == GL_TRUE ? " framebuffer" : "",
                     glIsRenderbuffer(name) == GL_TRUE ? " renderbuffer" : "",
                     glIsProgram(name) == GL_TRUE ? " program" : "",
                     glIsShader(name) == GL_TRUE ? " shader" : "");
    }

    // SWITCH, DO NOT PROBE. The caller named the class; asking GL which classes
    // the number happens to belong to answers a different question and deletes
    // whatever else it finds.
    switch (kind) {
    case object_kind::buffer: glDeleteBuffers(1, &name); break;
    case object_kind::texture: glDeleteTextures(1, &name); break;
    case object_kind::framebuffer: glDeleteFramebuffers(1, &name); break;
    case object_kind::renderbuffer: glDeleteRenderbuffers(1, &name); break;
    case object_kind::program: glDeleteProgram(name); break;
    case object_kind::shader: glDeleteShader(name); break;
    }
}

void device::enable_attribute(unsigned location, bool on) {
    if (!ok()) { return; }
    if (on) {
        glEnableVertexAttribArray(location);
    } else {
        glDisableVertexAttribArray(location);
    }
}

void device::attribute_pointer(unsigned location, int size, int type, bool normalised, int stride,
                               std::size_t offset) {
    if (!ok()) { return; }
    glVertexAttribPointer(location, size, static_cast<GLenum>(type),
                          normalised ? GL_TRUE : GL_FALSE, stride,
                          reinterpret_cast<const void *>(offset));
}

void device::attribute_divisor(unsigned location, unsigned divisor) {
    if (ok()) { glVertexAttribDivisor(location, divisor); }
}

device::attribute_state device::attribute_at(unsigned location) const {
    attribute_state out;
    if (!ok()) { return out; }
    GLint value = 0;
    glGetVertexAttribiv(location, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &value);
    out.enabled = value != 0;
    glGetVertexAttribiv(location, GL_VERTEX_ATTRIB_ARRAY_SIZE, &value);
    out.size = value;
    glGetVertexAttribiv(location, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &value);
    out.stride = value;
    glGetVertexAttribiv(location, GL_VERTEX_ATTRIB_ARRAY_TYPE, &value);
    out.type = static_cast<std::uint32_t>(value);
    glGetVertexAttribiv(location, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &value);
    out.normalised = value != 0;
    glGetVertexAttribiv(location, GL_VERTEX_ATTRIB_ARRAY_DIVISOR, &value);
    out.divisor = static_cast<std::uint32_t>(value);
    return out;
}

unsigned device::create_vertex_array() {
    if (!ok()) { return 0u; }
    GLuint name = 0;
    glGenVertexArrays(1, &name);

    // A NAME IS NOT AN OBJECT UNTIL IT IS BOUND. glGenVertexArrays only
    // RESERVES the number; until the first glBindVertexArray, glIsVertexArray
    // answers GL_FALSE for it. That is GL's rule and it is not WebGL's -
    // `createVertexArray` creates the object, and a page that asks
    // `isVertexArray` about one it just made is told yes by every browser.
    //
    // So bind it once and put the previous binding back. Restoring matters:
    // creating a vertex array must not disturb the one the page is building,
    // and leaving the new one current would silently retarget every
    // vertexAttribPointer that followed.
    GLint previous = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous);
    glBindVertexArray(name);
    glBindVertexArray(static_cast<GLuint>(previous));
    return name;
}

void device::bind_vertex_array(unsigned array) {
    if (ok()) { glBindVertexArray(array); }
}

void device::delete_vertex_array(unsigned array) {
    if (!ok() || array == 0) { return; }
    glDeleteVertexArrays(1, &array);
}

bool device::is_vertex_array(unsigned array) const {
    return ok() && glIsVertexArray(array) == GL_TRUE;
}

unsigned device::bound_vertex_array() const {
    if (!ok()) { return 0u; }
    GLint current = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &current);
    return static_cast<unsigned>(current);
}

unsigned device::create_texture() {
    if (!ok()) { return 0u; }
    GLuint name = 0;
    glGenTextures(1, &name);
    return name;
}

void device::bind_texture(int target, unsigned texture) {
    if (ok()) { glBindTexture(static_cast<GLenum>(target), texture); }
}

void device::active_texture(int unit) {
    if (ok()) { glActiveTexture(static_cast<GLenum>(unit)); }
}

void device::texture_image(int target, int width, int height, const void * rgba) {
    if (!ok()) { return; }
    glTexImage2D(static_cast<GLenum>(target), 0, GL_RGBA, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba);
}

void device::texture_parameter(int target, int name, int value) {
    if (ok()) { glTexParameteri(static_cast<GLenum>(target), static_cast<GLenum>(name), value); }
}

unsigned device::create_framebuffer() {
    if (!ok()) { return 0u; }
    GLuint name = 0;
    glGenFramebuffers(1, &name);
    return name;
}

void device::bind_framebuffer(unsigned framebuffer) {
    if (ok()) { glBindFramebuffer(GL_FRAMEBUFFER, framebuffer); }
}

void device::framebuffer_texture(int attachment, unsigned texture) {
    if (!ok()) { return; }
    glFramebufferTexture2D(GL_FRAMEBUFFER, static_cast<GLenum>(attachment), GL_TEXTURE_2D, texture,
                           0);
}

std::uint32_t device::framebuffer_status() const {
    return ok() ? static_cast<std::uint32_t>(glCheckFramebufferStatus(GL_FRAMEBUFFER)) : 0u;
}

// RENDERBUFFERS, which a render target needs and which were three no-ops.
//
// A page attaching depth to an offscreen target got nothing attached, so the
// target had colour and no depth - every draw into it passed the depth test in
// whatever order it arrived. Babylon's post-processes render the scene into
// exactly such a target.
unsigned device::create_renderbuffer() {
    if (!ok()) { return 0u; }
    GLuint name = 0;
    glGenRenderbuffers(1, &name);
    return name;
}

void device::bind_renderbuffer(unsigned renderbuffer) {
    if (ok()) { glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer); }
}

void device::renderbuffer_storage(int format, int width, int height) {
    if (ok()) {
        glRenderbufferStorage(GL_RENDERBUFFER, static_cast<GLenum>(format), width, height);
    }
}

void device::framebuffer_renderbuffer(int attachment, unsigned renderbuffer) {
    if (!ok()) { return; }
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, static_cast<GLenum>(attachment), GL_RENDERBUFFER,
                              renderbuffer);
}

#endif

} // namespace ctbrowser::raster::gl
