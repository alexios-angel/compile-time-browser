// gl::device WITHOUT ANGLE: every method of the device answers "no" and says
// why, so a build without the libraries still links and still reports.
//
// One of four files carved out of a 1,162-line Raster/gl.cpp on 2026-09-08.
// All are member functions of the one class declared in
// include/ctbrowser/raster/gl.hpp; the pimpl both branches define is in
// internal.hpp beside this, with the includes gl.cpp had. Nothing about the
// public header changed.

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
bool device::ok() const noexcept {
    return false;
}
const std::string & device::error() const noexcept {
    return impl_->error;
}
std::string device::renderer() const {
    return {};
}
std::string device::version() const {
    return {};
}
bool device::webgl_compatible() const {
    return false;
}
int device::width() const noexcept {
    return 0;
}
int device::height() const noexcept {
    return 0;
}
bool device::make_current() {
    return false;
}
bool device::resize(int, int) {
    return false;
}
void device::clear(float, float, float, float) {}
void device::viewport(int, int, int, int) {}
void device::scissor(int, int, int, int) {}
void device::clear_buffers(std::uint32_t) {}
void device::clear_color(float, float, float, float) {}
void device::clear_depth(float) {}
void device::set_capability(int, bool) {}
std::uint32_t device::take_error() {
    return 0u;
}
int device::limit(int) const {
    return 0;
}

unsigned device::create_shader(int) {
    return 0u;
}
void device::shader_source(unsigned, const std::string &) {}
void device::compile_shader(unsigned) {}
bool device::shader_compiled(unsigned) const {
    return false;
}
std::string device::shader_log(unsigned) const {
    return {};
}
unsigned device::create_program() {
    return 0u;
}
void device::attach_shader(unsigned, unsigned) {}
void device::link_program(unsigned) {}
bool device::program_linked(unsigned) const {
    return false;
}
std::string device::program_log(unsigned) const {
    return {};
}
void device::use_program(unsigned) {}
unsigned device::program_in_use() const {
    return 0u;
}
std::vector<device::active> device::active_attributes(unsigned) const {
    return {};
}
std::vector<device::active> device::active_uniforms(unsigned) const {
    return {};
}
int device::attribute_location(unsigned, const std::string &) const {
    return -1;
}
int device::uniform_location(unsigned, const std::string &) const {
    return -1;
}
int device::uniform_block_index(unsigned, const std::string &) const {
    return -1;
}
void device::uniform_block_binding(unsigned, unsigned, unsigned) {}
void device::set_uniform(int, const float *, int, int, int, bool) {}

unsigned device::create_buffer() {
    return 0u;
}
void device::bind_buffer(int, unsigned) {}
void device::buffer_data(int, const void *, std::size_t, int) {}
void device::buffer_sub_data(int, std::size_t, const void *, std::size_t) {}
void device::bind_buffer_base(int, unsigned, unsigned) {}
unsigned device::bound_buffer(int) const {
    return 0u;
}
void device::delete_object(object_kind, unsigned) {}
unsigned device::create_texture() {
    return 0u;
}
void device::bind_texture(int, unsigned) {}
void device::active_texture(int) {}
void device::texture_image(int, int, int, const void *) {}
void device::texture_parameter(int, int, int) {}
unsigned device::create_framebuffer() {
    return 0u;
}
unsigned device::create_renderbuffer() {
    return 0u;
}
void device::bind_renderbuffer(unsigned) {}
void device::renderbuffer_storage(int, int, int) {}
void device::framebuffer_renderbuffer(int, unsigned) {}
void device::bind_framebuffer(unsigned) {}
void device::framebuffer_texture(int, unsigned) {}
std::uint32_t device::framebuffer_status() const {
    return 0u;
}
void device::draw_arrays(int, int, int) {}
void device::draw_elements(int, int, int, std::size_t) {}
void device::draw_arrays_instanced(int, int, int, int) {}
void device::draw_elements_instanced(int, int, int, std::size_t, int) {}
void device::cull_face(int) {}
void device::front_face(int) {}
void device::depth_func(int) {}
void device::depth_mask(bool) {}
void device::blend_func(int, int) {}
void device::enable_attribute(unsigned, bool) {}
void device::attribute_pointer(unsigned, int, int, bool, int, std::size_t) {}
void device::attribute_divisor(unsigned, unsigned) {}
device::attribute_state device::attribute_at(unsigned) const {
    return {};
}
unsigned device::create_vertex_array() {
    return 0u;
}
void device::bind_vertex_array(unsigned) {}
void device::delete_vertex_array(unsigned) {}
bool device::is_vertex_array(unsigned) const {
    return false;
}
unsigned device::bound_vertex_array() const {
    return 0u;
}

bool device::read_pixels(paint::bitmap &) const {
    return false;
}

#endif

} // namespace ctbrowser::raster::gl
