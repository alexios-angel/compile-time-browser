//===- objects.cpp - WebGL objects and state --------------------*- C++ -*-===//
//
// The WebGL methods from createBuffer to texSubImage2D: object creation and
// deletion, buffers, shaders and programs, attributes, uniforms and uniform
// blocks, fixed-function state and textures.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

namespace {

using script::to_float;

/// The raw bytes of whatever a page passed as buffer or texture data: a typed
/// array, an ArrayBuffer, or a plain array of numbers. Each either is an array
/// or carries one in `__bytes`, the layout install_typed_arrays uses. Returns
/// an empty vector for anything else.
[[nodiscard]] std::vector<std::byte> bytes_of(context & cx, value v) {
    std::vector<std::byte> out;
    value items = v;
    if (v.is_object()) {
        const value held = cx.lookup_property(v, "__bytes");
        if (held.is_array()) { items = held; }
    }
    if (!items.is_array()) { return out; }
    auto * array = static_cast<script::array_object *>(items.as_heap());
    // A view already holds bytes in the layout the GPU wants, so copy its byte
    // range directly. Decoding elements and re-encoding them would be slower
    // and could disagree with what the page wrote.
    if (array->is_view()) {
        const auto * bytes = static_cast<const script::array_object *>(array->viewed.as_heap());
        const std::size_t width = script::bytes_per_element(array->elements);
        const std::size_t from = array->byte_offset;
        const std::size_t want = std::min(
            array->view_length * width, bytes->items.size() - std::min(from, bytes->items.size()));
        out.resize(want);
        for (std::size_t i = 0; i < want; ++i) {
            out[i] = static_cast<std::byte>(
                script::to_uint8(context::to_number(bytes->items[from + i])));
        }
        return out;
    }
    // A Float32Array, and a plain array of numbers, is encoded as four bytes
    // per element, not one; encoding one byte per float would upload a quarter
    // of the data.
    const script::element_kind kind = array->elements;
    if (kind == script::element_kind::f32 || kind == script::element_kind::none) {
        out.resize(array->items.size() * sizeof(float));
        for (std::size_t i = 0; i < array->items.size(); ++i) {
            const float f = to_float(context::to_number(array->items[i]));
            std::memcpy(out.data() + i * sizeof(float), &f, sizeof(f));
        }
        return out;
    }
    // Every other number kind is encoded little-endian at its own width, exactly
    // what a view over the same buffer would hold: Int8Array([-1]) is 0xFF
    // (ToInt8 is modular), a Uint8ClampedArray was clamped when it was stored,
    // and a Float64Array is eight bytes per element. element_to_raw is the
    // buffer store's own encoding, so the two cannot disagree.
    if (!script::is_bigint_kind(kind)) {
        const std::size_t width = script::bytes_per_element(kind);
        out.reserve(array->items.size() * width);
        for (const value & each : array->items) {
            const std::uint64_t raw = script::element_to_raw(kind, context::to_number(each));
            for (std::size_t b = 0; b < width; ++b) {
                out.push_back(static_cast<std::byte>((raw >> (8 * b)) & 0xFFu));
            }
        }
        return out;
    }
    // ponytail: a BigInt64Array uploads one byte an element; 8 via
    // bigint_to_uint64_wrap if a page ever feeds one to WebGL.
    out.reserve(array->items.size());
    for (const value & each : array->items) {
        out.push_back(static_cast<std::byte>(context::to_uint32(each) & 0xFFu));
    }
    return out;
}

/// The numbers a page passes to a uniform* call, starting at argument `from`,
/// as a uniform value of `rows` x `cols` components per element. The data is
/// padded to one element when short and trimmed to whole elements otherwise.
[[nodiscard]] uniform_value make_uniform(context & cx, std::span<value> args, std::size_t from,
                                         std::uint8_t rows, std::uint8_t cols, bool integer) {
    uniform_value made;
    made.rows = rows;
    made.cols = cols;
    made.integer = integer;
    // Either loose numbers - uniform3f(x, y, z) - or one array, which is what
    // the `v` suffix means.
    if (args.size() > from && args[from].is_array()) {
        auto * array = static_cast<script::array_object *>(args[from].as_heap());
        // Read through view_get: a typed-array view's `items` is empty by design
        // (script/value.hpp), and reading it would upload a matrix of zeros.
        for (std::size_t i = 0; i < array->length(); ++i) {
            made.data.push_back(to_float(array->is_view() ? script::view_get(*array, i)
                                                          : context::to_number(array->items[i])));
        }
    } else if (args.size() > from && args[from].is_object()) {
        const value held = cx.lookup_property(args[from], "__bytes");
        if (held.is_array()) {
            for (const value & each : static_cast<script::array_object *>(held.as_heap())->items) {
                made.data.push_back(to_float(context::to_number(each)));
            }
        }
    } else {
        for (std::size_t i = from; i < args.size(); ++i) {
            made.data.push_back(to_float(context::to_number(args[i])));
        }
    }
    // Pad up to one element of the shape, never truncate: an array uniform
    // (`uniform4fv` with eight floats) carries several elements.
    const std::size_t shape = std::max<std::size_t>(1, static_cast<std::size_t>(rows) * cols);
    if (made.data.size() < shape) {
        made.data.resize(shape, 0.0f);
    } else {
        // Keep a whole number of elements: webgl_context::set_uniform computes
        // the count as size / shape, so a partial trailing element would make
        // the driver read past the end.
        made.data.resize(made.data.size() - made.data.size() % shape);
    }
    return made;
}

} // namespace

void dom_bindings::install_webgl_methods(context & cx, script::object_object * obj,
                                         webgl_context * gl, canvas_context * surface) {
    // Wraps a method that writes pixels so it bumps the canvas revision: the
    // browser learns that a canvas changed only from its revision, so without
    // this the frame would show the previous picture.
    const auto touches = [surface](auto body) {
        return [surface, body](context & c, std::span<value> args) {
            const value out = body(c, args);
            surface->note_foreign_draw();
            return out;
        };
    };

    // --- Objects
    //
    // A handle is a JS object carrying the integer GL name (`__id`) and the
    // object class (`__kind`). The page only passes it back.
    const auto handle = [](context & c, std::uint32_t made_id, const char * kind) {
        auto * out = c.allocate<script::object_object>();
        out->set("__id", value::number(made_id));
        out->set("__kind", c.string(kind));
        return value::object(out);
    };

    set_method(cx, *obj, "createBuffer", [gl, handle](context & c, std::span<value>) {
        return handle(c, gl->device().create_buffer(), "buffer");
    });
    set_method(cx, *obj, "createTexture", [gl, handle](context & c, std::span<value>) {
        return handle(c, gl->device().create_texture(), "texture");
    });
    set_method(cx, *obj, "createProgram", [gl, handle](context & c, std::span<value>) {
        return handle(c, gl->device().create_program(), "program");
    });
    set_method(cx, *obj, "createShader", [gl, handle](context & c, std::span<value> a) {
        return handle(c, gl->device().create_shader(int_enum_at(a, 0)), "shader");
    });
    // Each delete* method passes its object kind to the device, so it never has
    // to guess the kind from the id (guessing deleted live objects of another
    // kind with the same number). The kind comes from the method, not the
    // handle's `__kind`, so `deleteBuffer(x)` deletes a buffer whatever x claims.
    using object_kind = raster::gl::device::object_kind;
    const std::pair<const char *, object_kind> deleters[] = {
        {"deleteBuffer", object_kind::buffer},
        {"deleteTexture", object_kind::texture},
        {"deleteProgram", object_kind::program},
        {"deleteShader", object_kind::shader},
        {"deleteFramebuffer", object_kind::framebuffer},
        {"deleteRenderbuffer", object_kind::renderbuffer}};
    for (const auto & [name, kind] : deleters) {
        set_method(cx, *obj, name, [gl, kind = kind](context & c, std::span<value> a) {
            gl->device().delete_object(kind, id_of(c, a.empty() ? value::undefined() : a[0]));
            return value::undefined();
        });
    }

    set_method(cx, *obj, "bindBuffer", [gl](context & c, std::span<value> a) {
        gl->device().bind_buffer(int_enum_at(a, 0),
                                 id_of(c, a.size() > 1 ? a[1] : value::undefined()));
        return value::undefined();
    });
    set_method(cx, *obj, "bufferSubData", [gl](context & c, std::span<value> a) {
        const std::vector<std::byte> bytes = bytes_of(c, a.size() > 2 ? a[2] : value::undefined());
        gl->buffer_sub_data(enum_at(a, 0), int_at(a, 1), bytes);
        return value::undefined();
    });
    set_method(cx, *obj, "bufferData", [gl](context & c, std::span<value> a) {
        // `bufferData(target, size, usage)` allocates rather than uploads: a
        // page that means to reserve space passes a number, and treating that
        // as data would upload one float instead of `size` bytes.
        //
        // The storage is zero-filled here, as WebGL requires: the size-only
        // overload hands the driver no data, and without
        // EGL_ANGLE_robust_resource_initialization the new storage would hold
        // whatever GPU memory held before, readable by the page. A size the
        // context refuses (negative: INVALID_VALUE; above 1 GiB, 1 << 30:
        // OUT_OF_MEMORY) still goes through the size-only overload to raise
        // its error.
        if (a.size() > 1 && a[1].is_number()) {
            const int size = int_at(a, 1);
            if (size < 0 || size > (1 << 30)) {
                gl->buffer_data(enum_at(a, 0), size, enum_at(a, 2));
            } else {
                gl->buffer_data(enum_at(a, 0),
                                std::vector<std::byte>(static_cast<std::size_t>(size)),
                                enum_at(a, 2));
            }
            return value::undefined();
        }
        gl->buffer_data(enum_at(a, 0), bytes_of(c, a.size() > 1 ? a[1] : value::undefined()),
                        enum_at(a, 2));
        return value::undefined();
    });

    // --- Shaders and programs
    set_method(cx, *obj, "shaderSource", [gl](context & c, std::span<value> a) {
        gl->shader_source(id_of(c, a.empty() ? value::undefined() : a[0]),
                          a.size() > 1 ? c.to_string(a[1]) : std::string{});
        return value::undefined();
    });
    set_method(cx, *obj, "compileShader", [gl](context & c, std::span<value> a) {
        gl->compile_shader(id_of(c, a.empty() ? value::undefined() : a[0]));
        return value::undefined();
    });
    set_method(cx, *obj, "getShaderParameter", [gl](context & c, std::span<value> a) {
        const std::uint32_t which = enum_at(a, 1);
        if (which == gl_enum::compile_status) {
            return value::boolean(
                gl->device().shader_compiled(id_of(c, a.empty() ? value::undefined() : a[0])));
        }
        return value::boolean(true);
    });
    set_method(cx, *obj, "getShaderInfoLog", [gl](context & c, std::span<value> a) {
        return c.string(gl->device().shader_log(id_of(c, a.empty() ? value::undefined() : a[0])));
    });
    set_method(cx, *obj, "attachShader", [gl](context & c, std::span<value> a) {
        gl->device().attach_shader(id_of(c, a.empty() ? value::undefined() : a[0]),
                                   id_of(c, a.size() > 1 ? a[1] : value::undefined()));
        return value::undefined();
    });
    set_method(cx, *obj, "detachShader",
               [](context &, std::span<value>) { return value::undefined(); });
    set_method(cx, *obj, "linkProgram", [gl](context & c, std::span<value> a) {
        gl->link_program(id_of(c, a.empty() ? value::undefined() : a[0]));
        return value::undefined();
    });
    set_method(cx, *obj, "getProgramParameter", [gl](context & c, std::span<value> a) {
        const std::uint32_t which = enum_at(a, 1);
        const std::uint32_t program = id_of(c, a.empty() ? value::undefined() : a[0]);
        // 0x8B83 is VALIDATE_STATUS, answered with the link status.
        if (which == gl_enum::link_status || which == 0x8B83) {
            return value::boolean(gl->device().program_linked(program));
        }
        // Libraries enumerate a program's uniforms and attributes instead of asking
        // by name, so these counts must be real.
        if (which == gl_enum::active_uniforms) {
            return value::number(static_cast<double>(gl->active_uniforms(program).size()));
        }
        if (which == gl_enum::active_attributes) {
            return value::number(static_cast<double>(gl->active_attributes(program).size()));
        }
        // Any other parameter answers 0, which is a guess. A parameter a page
        // actually reads should get a case above.
        return value::number(0);
    });
    // Builds a WebGLActiveInfo `{size, type, name}`. The index a page passes is
    // positional and is also the attribute's location, which is why
    // active_attributes preserves link order rather than sorting.
    const auto active_info = [](context & c, const webgl_context::active_variable & v) {
        value out = c.make_object();
        auto * made = static_cast<script::object_object *>(out.as_heap());
        made->set("name", c.string(v.name));
        // `size` is the array length, not the component count: 1 for a plain
        // uniform, 2 for `mat4 uColour[2]`.
        made->set("size", value::number(v.size));
        made->set("type", value::number(v.type));
        return out;
    };
    set_method(cx, *obj, "getActiveUniform", [gl, active_info](context & c, std::span<value> a) {
        const auto all = gl->active_uniforms(id_of(c, a.empty() ? value::undefined() : a[0]));
        const auto i = static_cast<std::size_t>(to_index(arg_number(a, 1)));
        if (i >= all.size()) { return value::null(); }
        return active_info(c, all[i]);
    });
    set_method(cx, *obj, "getActiveAttrib", [gl, active_info](context & c, std::span<value> a) {
        const auto all = gl->active_attributes(id_of(c, a.empty() ? value::undefined() : a[0]));
        const auto i = static_cast<std::size_t>(to_index(arg_number(a, 1)));
        if (i >= all.size()) { return value::null(); }
        return active_info(c, all[i]);
    });
    // Not a WebGL call: an engine extension returning the most recent shader
    // compile or program link log, so a test can see why a draw produced nothing.
    // The `ctbrowser` prefix marks it as non-standard.
    set_method(cx, *obj, "ctbrowserShaderError",
               [gl](context & c, std::span<value>) { return c.string(gl->shader_error()); });
    set_method(cx, *obj, "getProgramInfoLog", [gl](context & c, std::span<value> a) {
        return c.string(gl->device().program_log(id_of(c, a.empty() ? value::undefined() : a[0])));
    });
    set_method(cx, *obj, "validateProgram",
               [](context &, std::span<value>) { return value::undefined(); });
    set_method(cx, *obj, "useProgram", [gl](context & c, std::span<value> a) {
        gl->device().use_program(id_of(c, a.empty() ? value::undefined() : a[0]));
        return value::undefined();
    });

    // --- Attributes
    set_method(cx, *obj, "getAttribLocation", [gl](context & c, std::span<value> a) {
        return value::number(
            gl->device().attribute_location(id_of(c, a.empty() ? value::undefined() : a[0]),
                                            a.size() > 1 ? c.to_string(a[1]) : std::string{}));
    });
    set_method(cx, *obj, "bindAttribLocation", [](context &, std::span<value>) {
        // Not forwarded: the linker assigns attribute locations, so a requested
        // binding is not honoured. Silent rather than an error because pages
        // call it defensively and then read locations with getAttribLocation.
        return value::undefined();
    });
    set_method(cx, *obj, "enableVertexAttribArray", [gl](context &, std::span<value> a) {
        gl->enable_attribute(int_at(a, 0), true);
        return value::undefined();
    });
    set_method(cx, *obj, "disableVertexAttribArray", [gl](context &, std::span<value> a) {
        gl->enable_attribute(int_at(a, 0), false);
        return value::undefined();
    });
    set_method(cx, *obj, "vertexAttribPointer", [gl](context &, std::span<value> a) {
        gl->attribute_pointer(int_at(a, 0), int_at(a, 1), enum_at(a, 2),
                              a.size() > 3 && context::truthy(a[3]), int_at(a, 4), int_at(a, 5));
        return value::undefined();
    });

    // --- Uniform blocks (WebGL 2): a page fills a buffer and binds it instead
    // of calling uniform*; Babylon.js delivers all its matrices this way.
    set_method(cx, *obj, "getUniformBlockIndex", [gl](context & c, std::span<value> a) {
        const int index =
            gl->get_uniform_block_index(id_of(c, a.empty() ? value::undefined() : a[0]),
                                        a.size() > 1 ? c.to_string(a[1]) : std::string{});
        // A missing block answers GL_INVALID_INDEX (0xFFFFFFFF), which is not
        // an error: asking about a block the shader lacks is legitimate.
        return index < 0 ? value::number(4294967295.0) : value::number(index);
    });
    set_method(cx, *obj, "uniformBlockBinding", [gl](context & c, std::span<value> a) {
        gl->uniform_block_binding(id_of(c, a.empty() ? value::undefined() : a[0]),
                                  static_cast<std::uint32_t>(int_at(a, 1)),
                                  static_cast<std::uint32_t>(int_at(a, 2)));
        return value::undefined();
    });
    set_method(cx, *obj, "bindBufferBase", [gl](context & c, std::span<value> a) {
        gl->bind_buffer_base(enum_at(a, 0), static_cast<std::uint32_t>(int_at(a, 1)),
                             id_of(c, a.size() > 2 ? a[2] : value::undefined()));
        return value::undefined();
    });
    // --- Uniforms
    //
    // A uniform location is an object carrying the uniform's name in `__name`.
    // WebGL hands back an opaque object that a page only passes straight back,
    // so the name serves as the location and is readable in diagnostics.
    set_method(cx, *obj, "getUniformLocation", [](context & c, std::span<value> a) {
        if (a.size() < 2) { return value::null(); }
        auto * out = c.allocate<script::object_object>();
        out->set("__name", c.string(c.to_string(a[1])));
        return value::object(out);
    });
    const auto uniform_name = [](context & c, std::span<value> a) {
        if (a.empty() || !a[0].is_object()) { return std::string{}; }
        const value held = c.lookup_property(a[0], "__name");
        return held.is_undefined() ? std::string{} : c.to_string(held);
    };
    struct uniform_shape {
        const char * name;
        std::uint8_t rows;
        std::uint8_t cols;
        bool integer;
    };
    for (const uniform_shape & shape :
         {uniform_shape{"uniform1f", 1, 1, false}, uniform_shape{"uniform2f", 2, 1, false},
          uniform_shape{"uniform3f", 3, 1, false}, uniform_shape{"uniform4f", 4, 1, false},
          uniform_shape{"uniform1i", 1, 1, true}, uniform_shape{"uniform2i", 2, 1, true},
          uniform_shape{"uniform3i", 3, 1, true}, uniform_shape{"uniform4i", 4, 1, true},
          uniform_shape{"uniform1fv", 1, 1, false}, uniform_shape{"uniform2fv", 2, 1, false},
          uniform_shape{"uniform3fv", 3, 1, false}, uniform_shape{"uniform4fv", 4, 1, false},
          uniform_shape{"uniform1iv", 1, 1, true}, uniform_shape{"uniform2iv", 2, 1, true},
          uniform_shape{"uniform3iv", 3, 1, true}, uniform_shape{"uniform4iv", 4, 1, true}}) {
        set_method(
            cx, *obj, shape.name, [gl, shape, uniform_name](context & c, std::span<value> a) {
                gl->set_uniform(uniform_name(c, a),
                                make_uniform(c, a, 1, shape.rows, shape.cols, shape.integer));
                return value::undefined();
            });
    }
    for (const uniform_shape & shape : {uniform_shape{"uniformMatrix2fv", 2, 2, false},
                                        uniform_shape{"uniformMatrix3fv", 3, 3, false},
                                        uniform_shape{"uniformMatrix4fv", 4, 4, false}}) {
        set_method(
            cx, *obj, shape.name, [gl, shape, uniform_name](context & c, std::span<value> a) {
                // Argument 1 is `transpose` (must be false in WebGL 1, ignored here);
                // the values start at argument 2.
                gl->set_uniform(uniform_name(c, a),
                                make_uniform(c, a, 2, shape.rows, shape.cols, shape.integer));
                return value::undefined();
            });
    }

    // --- State
    set_method(cx, *obj, "viewport", [gl](context &, std::span<value> a) {
        gl->viewport(int_at(a, 0), int_at(a, 1), int_at(a, 2), int_at(a, 3));
        return value::undefined();
    });
    set_method(cx, *obj, "scissor", [gl](context &, std::span<value> a) {
        gl->scissor(int_at(a, 0), int_at(a, 1), int_at(a, 2), int_at(a, 3));
        return value::undefined();
    });
    set_method(cx, *obj, "enable", [gl](context &, std::span<value> a) {
        gl->device().set_capability(int_enum_at(a, 0), true);
        return value::undefined();
    });
    set_method(cx, *obj, "disable", [gl](context &, std::span<value> a) {
        gl->device().set_capability(int_enum_at(a, 0), false);
        return value::undefined();
    });
    set_method(cx, *obj, "depthFunc", [gl](context &, std::span<value> a) {
        gl->device().depth_func(int_enum_at(a, 0));
        return value::undefined();
    });
    set_method(cx, *obj, "depthMask", [gl](context &, std::span<value> a) {
        gl->device().depth_mask(!a.empty() && context::truthy(a[0]));
        return value::undefined();
    });
    set_method(cx, *obj, "blendFunc", [gl](context &, std::span<value> a) {
        gl->device().blend_func(int_enum_at(a, 0), int_enum_at(a, 1));
        return value::undefined();
    });
    set_method(cx, *obj, "blendFuncSeparate", [gl](context &, std::span<value> a) {
        // Only the RGB factors are forwarded; alpha is blended with the same
        // pair, so a page that separates them gets the colour pair for both.
        gl->device().blend_func(int_enum_at(a, 0), int_enum_at(a, 1));
        return value::undefined();
    });
    // Accepted and ignored: none of these reach the GL device. For `flush`,
    // `finish` and `hint` that costs nothing. The rest (the stencil family,
    // `colorMask`, `polygonOffset`, `sampleCoverage`, `lineWidth`,
    // `pixelStorei`, `generateMipmap` and the blend-equation calls) are not
    // implemented: a page using them gets a wrong picture, not an error. They
    // stay silent rather than refusing by name because libraries such as
    // Babylon.js set stencil state on every scene and render fine without it,
    // and a refusal per call would bury the diagnostics that matter.
    for (const char * name :
         {"blendEquation", "blendEquationSeparate", "blendColor", "stencilFunc",
          "stencilFuncSeparate", "stencilOp", "stencilOpSeparate", "stencilMask",
          "stencilMaskSeparate", "clearStencil", "colorMask", "polygonOffset", "sampleCoverage",
          "hint", "lineWidth", "pixelStorei", "generateMipmap", "flush", "finish"}) {
        set_method(cx, *obj, name, [](context &, std::span<value>) { return value::undefined(); });
    }
    set_method(cx, *obj, "cullFace", [gl](context &, std::span<value> a) {
        gl->device().cull_face(int_enum_at(a, 0));
        return value::undefined();
    });
    set_method(cx, *obj, "frontFace", [gl](context &, std::span<value> a) {
        gl->device().front_face(int_enum_at(a, 0));
        return value::undefined();
    });
    set_method(cx, *obj, "clearColor", [gl](context &, std::span<value> a) {
        gl->device().clear_color(to_float(arg_number(a, 0)), to_float(arg_number(a, 1)),
                                 to_float(arg_number(a, 2)), to_float(arg_number(a, 3)));
        return value::undefined();
    });
    set_method(cx, *obj, "clearDepth", [gl](context &, std::span<value> a) {
        gl->device().clear_depth(to_float(arg_number(a, 0)));
        return value::undefined();
    });
    set_method(cx, *obj, "clearStencil",
               [](context &, std::span<value>) { return value::undefined(); });
    set_method(cx, *obj, "clear", touches([gl](context &, std::span<value> a) {
                   gl->device().clear_buffers(enum_at(a, 0));
                   return value::undefined();
               }));

    // --- Textures
    set_method(cx, *obj, "bindTexture", [gl](context & c, std::span<value> a) {
        gl->device().bind_texture(int_enum_at(a, 0),
                                  id_of(c, a.size() > 1 ? a[1] : value::undefined()));
        return value::undefined();
    });
    set_method(cx, *obj, "activeTexture", [gl](context &, std::span<value> a) {
        gl->device().active_texture(int_enum_at(a, 0));
        return value::undefined();
    });
    set_method(cx, *obj, "texParameteri", [gl](context &, std::span<value> a) {
        gl->device().texture_parameter(int_enum_at(a, 0), int_enum_at(a, 1), int_enum_at(a, 2));
        return value::undefined();
    });
    set_method(cx, *obj, "texParameterf", [gl](context &, std::span<value> a) {
        gl->device().texture_parameter(int_enum_at(a, 0), int_enum_at(a, 1), int_enum_at(a, 2));
        return value::undefined();
    });
    set_method(cx, *obj, "texImage2D", [this, gl](context & c, std::span<value> a) {
        // Two signatures, told apart by argument count: (target, level,
        // internalformat, width, height, border, format, type, pixels) has nine,
        // and (target, level, internalformat, format, type, source) has six,
        // where `source` is an image, a canvas or an ImageData.
        if (a.size() >= 9) {
            gl->texture_image(enum_at(a, 0), int_at(a, 3), int_at(a, 4),
                              bytes_of(c, a.size() > 8 ? a[8] : value::undefined()));
            return value::undefined();
        }
        if (a.size() >= 6) {
            // An ImageData carries its own bytes; anything else is an image or a
            // canvas, which image_argument already resolves for drawImage.
            const value source = a[5];
            const value data = c.lookup_property(source, "data");
            if (data.is_array() || (data.is_object() && !data.is_undefined())) {
                const int w = to_int(context::to_number(c.lookup_property(source, "width")));
                const int h = to_int(context::to_number(c.lookup_property(source, "height")));
                gl->texture_image(enum_at(a, 0), w, h, bytes_of(c, data));
                return value::undefined();
            }
            if (const std::shared_ptr<const paint::bitmap> pixels = image_argument(source)) {
                gl->texture_from_bitmap(enum_at(a, 0), *pixels);
            }
        }
        return value::undefined();
    });
    set_method(cx, *obj, "texSubImage2D",
               [](context &, std::span<value>) { return value::undefined(); });
}

} // namespace ctbrowser::shell
