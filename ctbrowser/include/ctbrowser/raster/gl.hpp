//===- ctbrowser/include/ctbrowser/raster/gl.hpp - GL device ----*- C++ -*-===//
//
// The GL device behind WebGL: a thin handle onto an ANGLE context. It keeps no
// copy of any state GL can report, so nothing here can drift from the driver
// (docs/history/webgl-rewrite.md describes the design this replaced).
//
// No EGL or GLES type appears in this header (test/lint/api_surface.cpp
// enforces it), because every consumer pays for what a header includes.
// Parameters are plain integers because they arrive from JavaScript as plain
// integers.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <ctbrowser/paint/command.hpp>

namespace ctbrowser::raster::gl {

/// Which ANGLE back end to use. `fastest` is the default; `deterministic`
/// selects SwiftShader (a software renderer), which byte-compared golden images
/// need and which can also be selected at run time for debugging.
enum class driver {
    fastest,
    deterministic
};

/// Whether a device can be made at all: the build has ANGLE, and its libraries
/// load and initialise. This is probed rather than read from a build flag,
/// because a flag reports an intention and this reports what happened.
[[nodiscard]] bool available();

/// Why `available()` said no, in words a person can act on. Empty when it said
/// yes. Most failures happen in EGL, before there is a context to ask for an
/// error string.
[[nodiscard]] std::string unavailable_because();

/// One GL context and its drawing surface.
class device {
public:
    device(int width, int height, driver which = driver::fastest);
    ~device();
    device(const device &) = delete;
    device & operator=(const device &) = delete;
    device(device &&) noexcept;
    device & operator=(device &&) noexcept;

    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] const std::string & error() const noexcept;

    /// The renderer string GL reports. "ANGLE (Vulkan 1.3.0 (SwiftShader
    /// Device))" and "ANGLE (Vulkan ... Intel Arc)" run the same code path with
    /// very different results, so a report that omits this is not reproducible.
    [[nodiscard]] std::string renderer() const;
    [[nodiscard]] std::string version() const;

    /// Whether the context runs in ANGLE's WebGL compatibility mode, queried
    /// from GL rather than inferred from the requested attributes. ANGLE
    /// validates a whole class of page mistakes only in that mode (a uniform
    /// buffer bound with no storage is undefined behaviour to GLES but an
    /// INVALID_OPERATION to WebGL), so without it a page's error can become a
    /// crash in the Vulkan back end. The mode is requested whenever the display
    /// advertises it.
    [[nodiscard]] bool webgl_compatible() const;

    [[nodiscard]] int width() const noexcept;
    [[nodiscard]] int height() const noexcept;

    /// Replaces the drawing surface and keeps the context. GL objects and state
    /// belong to the context, so they survive (EGL allows a context to be made
    /// current against a different surface). Recreating the device instead
    /// would silently invalidate every handle and setting the page holds:
    /// draws stop working while clears still do. p5's WEBGL mode depends on
    /// this, because it enables DEPTH_TEST before createCanvas() resizes the
    /// canvas.
    bool resize(int width, int height);

    /// Makes this device's context current. Every call below assumes it is; a
    /// second device made current on the same thread displaces this one, which
    /// is GL's rule and not something a wrapper can hide.
    bool make_current();

    void viewport(int x, int y, int width, int height);
    void scissor(int x, int y, int width, int height);
    void clear(float red, float green, float blue, float alpha);
    /// Clears exactly the buffers in the page's `mask`. `gl.clear(COLOR_BUFFER_BIT)`
    /// must not also clear depth: a page that clears colour per object and depth
    /// per frame would render inside out.
    void clear_buffers(std::uint32_t mask);
    void clear_color(float red, float green, float blue, float alpha);
    void clear_depth(float depth);
    void set_capability(int capability, bool on);
    /// GL's own error flag, read and cleared (glGetError), never tracked here.
    [[nodiscard]] std::uint32_t take_error();

    /// One integer parameter, queried from GL and never from a table: pages size
    /// buffers and pick shaders from these limits, and a made-up 0 looks
    /// plausible. The caller decides which enums are integer-valued.
    [[nodiscard]] int limit(int name) const;

    // --- shaders and programs ------------------------------------------------

    [[nodiscard]] unsigned create_shader(int kind);
    void shader_source(unsigned shader, const std::string & source);
    void compile_shader(unsigned shader);
    [[nodiscard]] bool shader_compiled(unsigned shader) const;
    [[nodiscard]] std::string shader_log(unsigned shader) const;

    [[nodiscard]] unsigned create_program();
    void attach_shader(unsigned program, unsigned shader);
    void link_program(unsigned program);
    [[nodiscard]] bool program_linked(unsigned program) const;
    [[nodiscard]] std::string program_log(unsigned program) const;
    void use_program(unsigned program);

    /// One active attribute or uniform, as glGetActive* reports it.
    struct active {
        std::string name;
        std::uint32_t type = 0;
        int size = 1;
    };
    [[nodiscard]] std::vector<active> active_attributes(unsigned program) const;
    [[nodiscard]] std::vector<active> active_uniforms(unsigned program) const;
    [[nodiscard]] int attribute_location(unsigned program, const std::string & name) const;
    [[nodiscard]] int uniform_location(unsigned program, const std::string & name) const;

    /// GL_INVALID_INDEX comes back as -1. A name no block declares is not an
    /// error: a page may ask about a block its shader compiler dropped.
    [[nodiscard]] int uniform_block_index(unsigned program, const std::string & name) const;
    void uniform_block_binding(unsigned program, unsigned index, unsigned binding);

    /// Sets a uniform of shape `rows` x `cols` (1x1 a scalar, 3x1 a vec3, 4x4 a
    /// mat4), which is how one entry point covers the whole uniform* family.
    void set_uniform(int location, const float * values, int count, int rows, int cols,
                     bool integer);

    [[nodiscard]] unsigned program_in_use() const;

    // --- buffers, attributes and vertex arrays --------------------------------

    [[nodiscard]] unsigned create_buffer();
    void bind_buffer(int target, unsigned buffer);
    void buffer_data(int target, const void * bytes, std::size_t size, int usage);
    void buffer_sub_data(int target, std::size_t offset, const void * bytes, std::size_t size);
    void bind_buffer_base(int target, unsigned index, unsigned buffer);
    /// The buffer currently bound to `target`, queried from GL. Uploads name a
    /// target, not a buffer, so this is how to find which buffer an upload
    /// landed in (for example to diagnose a uniform buffer with no storage).
    /// Returns 0 for a target with no binding query.
    [[nodiscard]] unsigned bound_buffer(int target) const;
    /// The kind of GL object a name refers to. GL numbers each kind separately,
    /// starting at 1, so name 1 routinely exists as a buffer, a texture and a
    /// shader at the same time.
    enum class object_kind {
        buffer,
        texture,
        framebuffer,
        renderbuffer,
        program,
        shader
    };

    /// Deletes `name` as an object of `kind`. Because one integer can name a
    /// live object of several kinds at once, guessing the kind with glIs*
    /// deletes the wrong object, and GL then reissues the freed name on top of
    /// live data. The caller always knows the kind: WebGL defines deleteBuffer
    /// and its siblings by the method's kind, not by what the name looks like.
    void delete_object(object_kind kind, unsigned name);

    void enable_attribute(unsigned location, bool on);
    void attribute_pointer(unsigned location, int size, int type, bool normalised, int stride,
                           std::size_t offset);
    void attribute_divisor(unsigned location, unsigned divisor);

    /// One vertex attribute's state, queried from GL rather than recorded: a
    /// page can change it through a vertex array this layer never saw.
    struct attribute_state {
        bool enabled = false;
        int size = 4;
        int stride = 0;
        std::uint32_t type = 0;
        bool normalised = false;
        std::uint32_t divisor = 0;
    };
    [[nodiscard]] attribute_state attribute_at(unsigned location) const;

    [[nodiscard]] unsigned create_vertex_array();
    void bind_vertex_array(unsigned array);
    void delete_vertex_array(unsigned array);
    [[nodiscard]] bool is_vertex_array(unsigned array) const;
    [[nodiscard]] unsigned bound_vertex_array() const;

    // --- textures and framebuffers --------------------------------------------

    [[nodiscard]] unsigned create_texture();
    void bind_texture(int target, unsigned texture);
    void active_texture(int unit);
    /// Uploads an RGBA8 image. Only RGBA8 is accepted, by design: the bindings
    /// convert every other format before calling this.
    void texture_image(int target, int width, int height, const void * rgba);
    void texture_parameter(int target, int name, int value);

    [[nodiscard]] unsigned create_framebuffer();
    void bind_framebuffer(unsigned framebuffer);
    void framebuffer_texture(int attachment, unsigned texture);
    [[nodiscard]] std::uint32_t framebuffer_status() const;

    /// Renderbuffers, mainly so an offscreen render target can have a depth
    /// attachment.
    [[nodiscard]] unsigned create_renderbuffer();
    void bind_renderbuffer(unsigned renderbuffer);
    void renderbuffer_storage(int format, int width, int height);
    void framebuffer_renderbuffer(int attachment, unsigned renderbuffer);

    // --- draws and pipeline state ---------------------------------------------

    void draw_arrays(int mode, int first, int count);
    /// `offset` is a byte offset into the bound element buffer, as drawElements
    /// takes it, not an index.
    void draw_elements(int mode, int count, int type, std::size_t offset);
    void draw_arrays_instanced(int mode, int first, int count, int instances);
    void draw_elements_instanced(int mode, int count, int type, std::size_t offset, int instances);

    void cull_face(int which);
    void front_face(int which);
    void depth_func(int how);
    void depth_mask(bool on);
    void blend_func(int source, int destination);

    /// Reads the pixels into a bitmap the painter composites. Rows are flipped,
    /// because GL's origin is bottom-left and a bitmap's is top-left, and pixels
    /// are repacked as ARGB, the bitmap's format. Skipping either gives an image
    /// that is upside down or has red and blue swapped yet passes a test that
    /// only counts painted pixels.
    [[nodiscard]] bool read_pixels(paint::bitmap & into) const;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

} // namespace ctbrowser::raster::gl
