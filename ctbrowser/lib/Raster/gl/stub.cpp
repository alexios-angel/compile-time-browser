// gl::device WITHOUT ANGLE: every method of the device answers "no" and says
// why, so a build without the libraries still links and still reports.
//
// One of four files carved out of a 1,162-line Raster/gl.cpp on 2026-09-08.
// All are member functions of the one class declared in
// include/ctbrowser/raster/gl.hpp; the pimpl both branches define is in
// internal.hpp beside this, with the includes gl.cpp had. Nothing about the
// public header changed.
//
// EVERY DEFINITION HERE IS LINK-REQUIRED: shell/page/webgl.cpp forwards to the
// device unconditionally, so a method missing from this list is an undefined
// symbol in a build without ANGLE. Nothing below is ever CALLED without ANGLE
// beyond the constructor, ok() and error() - getContext returns null the
// moment ok() says no, and gl_basics skips on available() - which is why one
// line per method is all a definition needs to be.

#include "internal.hpp"

namespace ctbrowser::raster::gl {

#if !CTBROWSER_WITH_ANGLE

bool available() {
    return false;
}
std::string unavailable_because() {
    return "built without ANGLE - run tools/fetch-angle.sh and configure again";
}
device::device(int, int, driver) : impl_{std::make_unique<impl>()} {}
device::~device() = default;
device::device(device &&) noexcept = default;
device & device::operator=(device &&) noexcept = default;
const std::string & device::error() const noexcept {
    return impl_->error;
}

// The type's zero - false, 0, an empty string or vector, `return;` for void.
#define CTBROWSER_GL_NULL(ret, name, params)                                                       \
    ret device::name params {                                                                      \
        return ret();                                                                              \
    }
// A location that does not exist, which GL spells -1 rather than 0.
#define CTBROWSER_GL_NO_LOCATION(name)                                                             \
    int device::name(unsigned, const std::string &) const {                                        \
        return -1;                                                                                 \
    }

// clang-format off
CTBROWSER_GL_NULL(bool, ok, () const noexcept)
CTBROWSER_GL_NULL(std::string, renderer, () const)
CTBROWSER_GL_NULL(std::string, version, () const)
CTBROWSER_GL_NULL(bool, webgl_compatible, () const)
CTBROWSER_GL_NULL(int, width, () const noexcept)
CTBROWSER_GL_NULL(int, height, () const noexcept)
CTBROWSER_GL_NULL(bool, make_current, ())
CTBROWSER_GL_NULL(bool, resize, (int, int))
CTBROWSER_GL_NULL(void, clear, (float, float, float, float))
CTBROWSER_GL_NULL(void, viewport, (int, int, int, int))
CTBROWSER_GL_NULL(void, scissor, (int, int, int, int))
CTBROWSER_GL_NULL(void, clear_buffers, (std::uint32_t))
CTBROWSER_GL_NULL(void, clear_color, (float, float, float, float))
CTBROWSER_GL_NULL(void, clear_depth, (float))
CTBROWSER_GL_NULL(void, set_capability, (int, bool))
CTBROWSER_GL_NULL(std::uint32_t, take_error, ())
CTBROWSER_GL_NULL(int, limit, (int) const)

CTBROWSER_GL_NULL(unsigned, create_shader, (int))
CTBROWSER_GL_NULL(void, shader_source, (unsigned, const std::string &))
CTBROWSER_GL_NULL(void, compile_shader, (unsigned))
CTBROWSER_GL_NULL(bool, shader_compiled, (unsigned) const)
CTBROWSER_GL_NULL(std::string, shader_log, (unsigned) const)
CTBROWSER_GL_NULL(unsigned, create_program, ())
CTBROWSER_GL_NULL(void, attach_shader, (unsigned, unsigned))
CTBROWSER_GL_NULL(void, link_program, (unsigned))
CTBROWSER_GL_NULL(bool, program_linked, (unsigned) const)
CTBROWSER_GL_NULL(std::string, program_log, (unsigned) const)
CTBROWSER_GL_NULL(void, use_program, (unsigned))
CTBROWSER_GL_NULL(unsigned, program_in_use, () const)
CTBROWSER_GL_NULL(std::vector<device::active>, active_attributes, (unsigned) const)
CTBROWSER_GL_NULL(std::vector<device::active>, active_uniforms, (unsigned) const)
CTBROWSER_GL_NO_LOCATION(attribute_location)
CTBROWSER_GL_NO_LOCATION(uniform_location)
CTBROWSER_GL_NO_LOCATION(uniform_block_index)
CTBROWSER_GL_NULL(void, uniform_block_binding, (unsigned, unsigned, unsigned))
CTBROWSER_GL_NULL(void, set_uniform, (int, const float *, int, int, int, bool))

CTBROWSER_GL_NULL(unsigned, create_buffer, ())
CTBROWSER_GL_NULL(void, bind_buffer, (int, unsigned))
CTBROWSER_GL_NULL(void, buffer_data, (int, const void *, std::size_t, int))
CTBROWSER_GL_NULL(void, buffer_sub_data, (int, std::size_t, const void *, std::size_t))
CTBROWSER_GL_NULL(void, bind_buffer_base, (int, unsigned, unsigned))
CTBROWSER_GL_NULL(unsigned, bound_buffer, (int) const)
CTBROWSER_GL_NULL(void, delete_object, (object_kind, unsigned))
CTBROWSER_GL_NULL(unsigned, create_texture, ())
CTBROWSER_GL_NULL(void, bind_texture, (int, unsigned))
CTBROWSER_GL_NULL(void, active_texture, (int))
CTBROWSER_GL_NULL(void, texture_image, (int, int, int, const void *))
CTBROWSER_GL_NULL(void, texture_parameter, (int, int, int))
CTBROWSER_GL_NULL(unsigned, create_framebuffer, ())
CTBROWSER_GL_NULL(unsigned, create_renderbuffer, ())
CTBROWSER_GL_NULL(void, bind_renderbuffer, (unsigned))
CTBROWSER_GL_NULL(void, renderbuffer_storage, (int, int, int))
CTBROWSER_GL_NULL(void, framebuffer_renderbuffer, (int, unsigned))
CTBROWSER_GL_NULL(void, bind_framebuffer, (unsigned))
CTBROWSER_GL_NULL(void, framebuffer_texture, (int, unsigned))
CTBROWSER_GL_NULL(std::uint32_t, framebuffer_status, () const)
CTBROWSER_GL_NULL(void, draw_arrays, (int, int, int))
CTBROWSER_GL_NULL(void, draw_elements, (int, int, int, std::size_t))
CTBROWSER_GL_NULL(void, draw_arrays_instanced, (int, int, int, int))
CTBROWSER_GL_NULL(void, draw_elements_instanced, (int, int, int, std::size_t, int))
CTBROWSER_GL_NULL(void, cull_face, (int))
CTBROWSER_GL_NULL(void, front_face, (int))
CTBROWSER_GL_NULL(void, depth_func, (int))
CTBROWSER_GL_NULL(void, depth_mask, (bool))
CTBROWSER_GL_NULL(void, blend_func, (int, int))
CTBROWSER_GL_NULL(void, enable_attribute, (unsigned, bool))
CTBROWSER_GL_NULL(void, attribute_pointer, (unsigned, int, int, bool, int, std::size_t))
CTBROWSER_GL_NULL(void, attribute_divisor, (unsigned, unsigned))
CTBROWSER_GL_NULL(device::attribute_state, attribute_at, (unsigned) const)
CTBROWSER_GL_NULL(unsigned, create_vertex_array, ())
CTBROWSER_GL_NULL(void, bind_vertex_array, (unsigned))
CTBROWSER_GL_NULL(void, delete_vertex_array, (unsigned))
CTBROWSER_GL_NULL(bool, is_vertex_array, (unsigned) const)
CTBROWSER_GL_NULL(unsigned, bound_vertex_array, () const)

CTBROWSER_GL_NULL(bool, read_pixels, (paint::bitmap &) const)
// clang-format on

#undef CTBROWSER_GL_NULL
#undef CTBROWSER_GL_NO_LOCATION

#endif

} // namespace ctbrowser::raster::gl
