#pragma once
#include <cstddef>
#include <memory>
#include <string>

#include <ctbrowser/paint/command.hpp>

// A REAL OpenGL ES DEVICE, offscreen, for the WebGL path.
//
// Stage 1 of docs/angle-plan.md. The software rasteriser executes a page's
// shaders one fragment at a time and manages 1.03 M fragments per second;
// measured against the same shader, ANGLE does 192 M on Linux and 332 M on
// Windows. This is the seam that would let a page's draws go to the second one.
//
// NOTHING CALLS IT YET, deliberately. The plan keeps both paths alive behind a
// switch so "is ANGLE better" stays a measurement rather than a belief, and the
// first step is a device that can be created, drawn into and read back without
// anything depending on it.
//
// NO ANGLE TYPE APPEARS HERE, and that is a rule this repository enforces rather
// than prefers: `tests/api_surface` lints that no third-party header reaches a
// public one, because everyone who touches the engine pays for what a header
// includes. `core/cpu_time.hpp` is the pattern - one declaration, and its .cpp
// owns the platform. EGL and GLES live entirely inside gles.cpp.
//
// THERE IS NO WINDOW AND THERE WILL NOT BE ONE. A page's canvas is a
// paint::bitmap that the software painter composites, so this renders to an
// offscreen surface and hands the pixels back. That readback is the cost the
// plan worried would eat the win; it measured at 8%.

namespace ctbrowser::raster::gles {

// Whether a device can be made AT ALL: built with ANGLE, and the libraries
// actually load and initialise. Asked rather than assumed, for the same reason
// `ctbrowser::allocator_name()` is - a build flag reports an intention and this
// reports what happened.
[[nodiscard]] bool available();

// Why `available()` said no, for a report a person can act on. Empty when it
// said yes.
[[nodiscard]] std::string unavailable_because();

// An offscreen GLES device of a fixed size.
//
// ONE PER SURFACE, not one per process: a page can have several canvases and
// they do not share state. Non-copyable for the obvious reason - it owns a
// display, a surface and a context.
class device {
public:
    device(int width, int height);
    ~device();
    device(const device &) = delete;
    device & operator=(const device &) = delete;
    device(device &&) noexcept;
    device & operator=(device &&) noexcept;

    [[nodiscard]] bool ok() const noexcept;
    // Empty when ok(). The GL error string is not enough on its own - most of
    // what goes wrong here happens in EGL, before there is a context to ask.
    [[nodiscard]] const std::string & error() const noexcept;

    // What actually answered. Worth having in a report: "ANGLE (Vulkan 1.3.0
    // (SwiftShader Device))" and "ANGLE (Vulkan ... Intel Arc)" are the same
    // code path and very different numbers.
    [[nodiscard]] std::string renderer() const;
    [[nodiscard]] std::string version() const;

    [[nodiscard]] int width() const noexcept;
    [[nodiscard]] int height() const noexcept;

    // Make this device's context current on the calling thread. Every call
    // below assumes it - and a second device on the same thread makes the first
    // one's context stale, which is a rule GL has and this cannot hide.
    bool make_current();

    void clear(float red, float green, float blue, float alpha);

    // --- the GL a page's calls forward to -----------------------------------
    //
    // A TYPED FACADE, not a header full of GLenum. Every parameter here is an
    // int, a bool or a string, so `gles.hpp` still names no GLES type and the
    // caller still needs no GLES header - the numbers a page passes are already
    // plain integers by the time they reach C++, because they came from
    // JavaScript.
    //
    // ONLY WHAT A PAGE HAS BEEN SEEN TO NEED. This is the set
    // examples/pages/webgl-triangle.html calls, which is stage 2's first
    // increment; docs/angle-plan.md carries the rest. A call that is NOT here
    // is refused loudly by the caller rather than silently ignored - see
    // webgl_context::unforwarded.
    void viewport(int x, int y, int width, int height);
    void set_capability(int capability, bool on);

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
    [[nodiscard]] int attribute_location(unsigned program, const std::string & name) const;
    [[nodiscard]] int uniform_location(unsigned program, const std::string & name) const;

    [[nodiscard]] unsigned create_buffer();
    void bind_buffer(int target, unsigned buffer);
    void buffer_data(int target, const void * bytes, std::size_t size, int usage);

    void enable_attribute(unsigned location, bool on);
    void attribute_pointer(unsigned location, int size, int type, bool normalised, int stride,
                           std::size_t offset);
    void depth_func(int how);
    void depth_mask(bool on);
    void blend_func(int source, int destination);

    // --- textures ------------------------------------------------------------
    //
    // RGBA8 ONLY, and by design rather than omission: a page's texture arrives
    // here as a paint::bitmap or as bytes the bindings already normalised, so
    // the format zoo stops before this line.
    [[nodiscard]] unsigned create_texture();
    void bind_texture(int target, unsigned texture);
    void active_texture(int unit);
    void texture_image(int target, int width, int height, const void * rgba);
    void texture_parameter(int target, int name, int value);

    void draw_arrays(int mode, int first, int count);
    // AN INDEXED DRAW, which is what a MESH is - every library here draws with
    // one. `offset` is a byte offset into the bound element buffer, which is
    // what WebGL's drawElements takes and the reason it is not an index.
    void draw_elements(int mode, int count, int type, std::size_t offset);

    // A UNIFORM, BY NAME, because that is what the bindings carry: WebGL hands
    // a page an opaque location object and this engine puts the NAME in it, so
    // the location is looked up here rather than plumbed through.
    //
    // `rows`/`cols` are the value's shape - 1x1 is a scalar, 3x1 a vec3, 3x3 a
    // mat3 - which is how one entry point covers the whole `uniform*` family.
    void set_uniform(unsigned program, const std::string & name, const float * values, int count,
                     int rows, int cols, bool integer);

    // The pixels, into a bitmap the painter can composite. Resized if it does
    // not already match.
    //
    // FLIPPED, because GL's origin is the bottom left and a bitmap's is the top
    // left. Handing back the unflipped buffer produces a picture that is upside
    // down and otherwise perfect, which is the kind of wrong that survives a
    // long time in a test that only checks a pixel count.
    [[nodiscard]] bool read_pixels(paint::bitmap & into) const;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

} // namespace ctbrowser::raster::gles
