#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <ctbrowser/paint/command.hpp>

// THE GL DEVICE. One implementation, no software twin.
//
// The stack this replaces kept a software mirror of GL state and, separately and
// by hand, forwarded some calls to ANGLE. Nineteen methods never forwarded at
// all, and because a call that never learned to forward also never learned to
// record that it hadn't, the ledger could not see them. See
// docs/webgl-rewrite-plan.md.
//
// So there is nothing here but a handle onto ANGLE. No state is remembered that
// GL can be asked for, because remembered state is the thing that drifts.
//
// NO EGL OR GLES TYPE APPEARS IN THIS HEADER, and `tests/api_surface` enforces
// it: every consumer pays for what a header includes. Parameters are plain
// integers because the numbers a page passes came from JavaScript and are
// already plain integers by the time they arrive.

namespace ctbrowser::raster::gl {

// WHICH DRIVER. The old code hard-coded SwiftShader and left no way to ask for
// anything else, which made "safe mode" a rewrite rather than a switch.
//
// `fastest` is the default because it is what a user should get; `deterministic`
// is SwiftShader, which is what the byte-compared goldens need and what a player
// or a debugging session can select at run time.
enum class driver {
    fastest,
    deterministic
};

// Whether a device can be made AT ALL: built with ANGLE, and the libraries load
// and initialise. Asked rather than assumed - a build flag reports an intention
// and this reports what happened.
[[nodiscard]] bool available();

// Why `available()` said no, in words a person can act on. Empty when it said
// yes. Most of what goes wrong happens in EGL, before there is a context to ask
// for an error string.
[[nodiscard]] std::string unavailable_because();

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

    // What actually answered - "ANGLE (Vulkan 1.3.0 (SwiftShader Device))" and
    // "ANGLE (Vulkan ... Intel Arc)" are the same code path and very different
    // numbers, so a report that omits this is not reproducible.
    [[nodiscard]] std::string renderer() const;
    [[nodiscard]] std::string version() const;

    // WHETHER THIS IS A BROWSER'S CONTEXT OR A NATIVE APP'S, asked of GL rather
    // than inferred from the attributes that were requested. ANGLE validates a
    // whole class of page mistake only in WebGL compatibility mode - a uniform
    // buffer bound to a binding point with no storage is undefined behaviour to
    // GLES and an INVALID_OPERATION to WebGL - so a context that quietly came up
    // without it turns a page's error into a crash in the Vulkan back end. It
    // is requested when the display advertises it, and this reports what
    // happened.
    [[nodiscard]] bool webgl_compatible() const;

    [[nodiscard]] int width() const noexcept;
    [[nodiscard]] int height() const noexcept;

    // A NEW DRAWING BUFFER, THE SAME CONTEXT.
    //
    // Constructing a fresh device instead - which is what a canvas resize used
    // to do - throws away every program, buffer, texture and piece of pipeline
    // state the page has set up, and GL says nothing about any of it. The page
    // goes on using handles that name objects in a destroyed context, so its
    // draws quietly stop working while `glClear` on the new device keeps
    // painting: "clears, but draws no geometry".
    //
    // Measured on p5's WEBGL page. `createCanvas(300, 200, WEBGL)` assigns
    // canvas.width, which resized; p5 had already run `enable(DEPTH_TEST)` and
    // `depthFunc(LEQUAL)` on the context it was handed. The first draw then
    // reported test=0 and func=GL_LESS - both back at their defaults - and the
    // sphere drew over the box instead of behind it.
    //
    // Only the SURFACE is replaced here. GL objects and GL state belong to the
    // context, so they survive; EGL is explicitly fine with a context being
    // made current against a different surface.
    bool resize(int width, int height);

    // Every call below assumes this device's context is current. A second device
    // on the same thread makes the first one's stale, which is GL's rule and not
    // something a wrapper can hide.
    bool make_current();

    void viewport(int x, int y, int width, int height);
    void scissor(int x, int y, int width, int height);
    void clear(float red, float green, float blue, float alpha);
    // THE MASK A PAGE PASSED, not a fixed set. `gl.clear(COLOR_BUFFER_BIT)`
    // must not silently clear depth as well - a page that clears colour
    // per-object and depth per-frame renders inside out if it does.
    void clear_buffers(std::uint32_t mask);
    void clear_color(float red, float green, float blue, float alpha);
    void clear_depth(float depth);
    void set_capability(int capability, bool on);
    // GL's own error, reported and cleared. Asked of the driver rather than
    // tracked here, because tracked state is what drifts.
    [[nodiscard]] std::uint32_t take_error();

    // ONE INTEGER, ASKED OF GL. `getParameter` answered a hardcoded table with
    // `default: 0`, so ten of the caps Babylon reads - MAX_VARYING_VECTORS,
    // MAX_DRAW_BUFFERS, MAX_SAMPLES and the rest - came back as ZERO. A page
    // sizes buffers and picks shader permutations from those numbers, and zero
    // is a plausible-looking answer rather than an obviously missing one.
    //
    // The CALLER decides which enums are integers; this only asks.
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

    struct active {
        std::string name;
        std::uint32_t type = 0;
        int size = 1;
    };
    [[nodiscard]] std::vector<active> active_attributes(unsigned program) const;
    [[nodiscard]] std::vector<active> active_uniforms(unsigned program) const;
    [[nodiscard]] int attribute_location(unsigned program, const std::string & name) const;
    [[nodiscard]] int uniform_location(unsigned program, const std::string & name) const;

    // GL_INVALID_INDEX comes back as -1. A name no block declares is NOT an
    // error: a page may ask about a block its shader dropped.
    [[nodiscard]] int uniform_block_index(unsigned program, const std::string & name) const;
    void uniform_block_binding(unsigned program, unsigned index, unsigned binding);

    // `rows` x `cols` is the shape - 1x1 a scalar, 3x1 a vec3, 4x4 a mat4 -
    // which is how one entry point covers the whole uniform* family.
    void set_uniform(int location, const float * values, int count, int rows, int cols,
                     bool integer);

    [[nodiscard]] unsigned program_in_use() const;

    // --- buffers, attributes and vertex arrays --------------------------------

    [[nodiscard]] unsigned create_buffer();
    void bind_buffer(int target, unsigned buffer);
    void buffer_data(int target, const void * bytes, std::size_t size, int usage);
    void buffer_sub_data(int target, std::size_t offset, const void * bytes, std::size_t size);
    void bind_buffer_base(int target, unsigned index, unsigned buffer);
    // WHICH BUFFER IS BOUND TO A TARGET - asked of GL, never remembered. The
    // uploads a page makes name a target and GL resolves it to a buffer, so
    // "which buffer did that 3,072 bytes land in" is not answerable from the
    // call alone, and it is the question a uniform buffer with no storage
    // behind it is diagnosed by. 0 for a target with no binding query.
    [[nodiscard]] unsigned bound_buffer(int target) const;
    // WHICH CLASS A HANDLE BELONGS TO. GL numbers each of these SEPARATELY and
    // every one of them starts at 1, so name 1 routinely exists as a buffer AND
    // a texture AND a shader at the same time.
    enum class object_kind {
        buffer,
        texture,
        framebuffer,
        renderbuffer,
        program,
        shader
    };

    // THE KIND IS AN ARGUMENT, and it has to be.
    //
    // This took a kind-less name and asked each `glIs*` in turn, on the stated
    // premise that "a handle belongs to exactly one of them". That premise is
    // FALSE - the namespaces are independent - and the six probes had no
    // `else`, so `deleteShader(1)` also ran `glDeleteBuffers` on buffer 1.
    //
    // The damage was not a visible error. GL FREES THE NAME, `glGenBuffers`
    // hands it straight back, and the next allocation lands on top of live
    // geometry: measured on Babylon, buffer 7 held 1,835,008 bytes of vertices
    // and was reissued as a 1,072-byte uniform buffer. What ANGLE finally
    // reported was "it is undefined behaviour to use a uniform buffer that is
    // too small" - three layers away from the delete that caused it.
    //
    // The caller always knows: `deleteBuffer` deletes a buffer whatever the
    // handle claims to be, which is also what WebGL specifies.
    void delete_object(object_kind kind, unsigned name);

    void enable_attribute(unsigned location, bool on);
    void attribute_pointer(unsigned location, int size, int type, bool normalised, int stride,
                           std::size_t offset);
    void attribute_divisor(unsigned location, unsigned divisor);

    // ONE ATTRIBUTE'S STATE, ASKED OF GL. Not a record of what was set: a page
    // can change it through a vertex array this layer never saw, so anything
    // remembered here would be a guess that looks like a fact.
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
    // RGBA8 ONLY, by design rather than omission: a texture arrives here as
    // bytes the bindings already normalised, so the format zoo stops before
    // this line.
    void texture_image(int target, int width, int height, const void * rgba);
    void texture_parameter(int target, int name, int value);

    [[nodiscard]] unsigned create_framebuffer();
    void bind_framebuffer(unsigned framebuffer);
    void framebuffer_texture(int attachment, unsigned texture);
    [[nodiscard]] std::uint32_t framebuffer_status() const;

    // A RENDER TARGET USUALLY WANTS DEPTH, and these were three no-ops in the
    // bindings, so it never got any: an offscreen target had colour and nothing
    // else, and every draw into it passed the depth test in arrival order.
    [[nodiscard]] unsigned create_renderbuffer();
    void bind_renderbuffer(unsigned renderbuffer);
    void renderbuffer_storage(int format, int width, int height);
    void framebuffer_renderbuffer(int attachment, unsigned renderbuffer);

    // --- draws and pipeline state ---------------------------------------------

    void draw_arrays(int mode, int first, int count);
    // `offset` is a BYTE OFFSET into the bound element buffer, which is what
    // drawElements takes and the reason it is not an index.
    void draw_elements(int mode, int count, int type, std::size_t offset);
    void draw_arrays_instanced(int mode, int first, int count, int instances);
    void draw_elements_instanced(int mode, int count, int type, std::size_t offset, int instances);

    void cull_face(int which);
    void front_face(int which);
    void depth_func(int how);
    void depth_mask(bool on);
    void blend_func(int source, int destination);

    // The pixels, into a bitmap the painter composites.
    //
    // FLIPPED, because GL's origin is bottom-left and a bitmap's is top-left,
    // and ARGB, because that is the bitmap's packing and not GL's byte order.
    // Handing back the raw buffer gives a picture that is upside down with red
    // and blue swapped and otherwise perfect - the kind of wrong that survives a
    // test which only counts painted pixels.
    [[nodiscard]] bool read_pixels(paint::bitmap & into) const;

private:
    // Prints the pipeline state of the FIRST draw under CTBROWSER_GL_DEBUG.
    void note_first_draw();

    struct impl;
    std::unique_ptr<impl> impl_;
};

} // namespace ctbrowser::raster::gl
