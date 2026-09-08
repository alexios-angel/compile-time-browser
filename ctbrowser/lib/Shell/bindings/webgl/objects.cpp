// dom_bindings - the WebGL methods from createBuffer to texSubImage2D: objects,
// shaders and programs, attributes, uniforms, state and textures.
//
// One of four files carved out of a 1,431-line bindings/webgl.cpp on
// 2026-09-08. webgl_context_object was ONE 1,213-line function; it is split
// at three seams that share no local into private member functions, called
// in the order the original installed things. The argument helpers every
// file needs are in internal.hpp beside this, in ctbrowser::shell::detail.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

namespace {

// Pull bytes out of whatever a page passed: a typed array, an ArrayBuffer, or a
// plain array of numbers. All three carry their data in `__bytes` or are arrays
// themselves, which is the shape install_typed_arrays already uses.
[[nodiscard]] std::vector<std::byte> bytes_of(context & cx, value v) {
    std::vector<std::byte> out;
    value items = v;
    if (v.is_object()) {
        const value held = cx.lookup_property(v, "__bytes");
        if (held.is_array()) { items = held; }
    }
    if (!items.is_array()) { return out; }
    auto * array = static_cast<script::array_object *>(items.as_heap());
    // A VIEW ALREADY IS BYTES, in the layout the GPU wants - that is the whole
    // point of it - so this is a copy of a range rather than a re-encoding of
    // element values. Going through the element accessors instead would turn
    // the bytes back into numbers and then back into bytes, which is both
    // slower and a chance to disagree with what the page actually wrote.
    if (array->is_view()) {
        const auto * bytes = static_cast<const script::array_object *>(array->viewed.as_heap());
        const std::size_t width = script::bytes_per_element(array->elements);
        const std::size_t from = array->byte_offset;
        const std::size_t want = std::min(
            array->view_length * width, bytes->items.size() - std::min(from, bytes->items.size()));
        out.resize(want);
        for (std::size_t i = 0; i < want; ++i) {
            out[i] = static_cast<std::byte>(static_cast<unsigned char>(
                std::clamp(context::to_number(bytes->items[from + i]), 0.0, 255.0)));
        }
        return out;
    }
    // A FLOAT ARRAY IS FOUR BYTES A NUMBER, not one. The element kind says which
    // - getting it wrong turns a buffer of positions into a quarter of one, and
    // the triangle that results looks like a bad transform rather than a bad
    // upload.
    const bool floats = array->elements == script::element_kind::f32 ||
                        array->elements == script::element_kind::none;
    if (floats) {
        out.resize(array->items.size() * sizeof(float));
        for (std::size_t i = 0; i < array->items.size(); ++i) {
            const auto f = static_cast<float>(context::to_number(array->items[i]));
            std::memcpy(out.data() + i * sizeof(float), &f, sizeof(f));
        }
        return out;
    }
    if (array->elements == script::element_kind::u16 ||
        array->elements == script::element_kind::i16) {
        out.resize(array->items.size() * sizeof(std::uint16_t));
        for (std::size_t i = 0; i < array->items.size(); ++i) {
            const auto v16 = static_cast<std::uint16_t>(context::to_number(array->items[i]));
            std::memcpy(out.data() + i * sizeof(std::uint16_t), &v16, sizeof(v16));
        }
        return out;
    }
    if (array->elements == script::element_kind::u32 ||
        array->elements == script::element_kind::i32) {
        out.resize(array->items.size() * sizeof(std::uint32_t));
        for (std::size_t i = 0; i < array->items.size(); ++i) {
            const auto v32 = static_cast<std::uint32_t>(context::to_number(array->items[i]));
            std::memcpy(out.data() + i * sizeof(std::uint32_t), &v32, sizeof(v32));
        }
        return out;
    }
    out.reserve(array->items.size());
    for (const value & each : array->items) {
        out.push_back(static_cast<std::byte>(
            static_cast<unsigned char>(std::clamp(context::to_number(each), 0.0, 255.0))));
    }
    return out;
}

// The numbers a page passes to uniform*, as a GLSL value of the given shape.
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
        // THROUGH THE ACCESSORS, because A VIEW'S `items` IS EMPTY BY DESIGN -
        // script/value.hpp says so at the member, and `typed_proto.set` already
        // reads both kinds this way.
        //
        // Reading `items` directly gave a length-ZERO array for every
        // view-backed Float32Array, and the resize below then padded it to a
        // matrix of zeros. `uniformMatrix4fv` with a view over an ArrayBuffer
        // therefore uploaded a ZERO MATRIX, which collapses every vertex to the
        // origin - with no GL error anywhere, because a zero matrix is a
        // perfectly legal one.
        for (std::size_t i = 0; i < array->length(); ++i) {
            made.data.push_back(static_cast<float>(array->is_view()
                                                       ? script::view_get(*array, i)
                                                       : context::to_number(array->items[i])));
        }
    } else if (args.size() > from && args[from].is_object()) {
        const value held = cx.lookup_property(args[from], "__bytes");
        if (held.is_array()) {
            for (const value & each : static_cast<script::array_object *>(held.as_heap())->items) {
                made.data.push_back(static_cast<float>(context::to_number(each)));
            }
        }
    } else {
        for (std::size_t i = from; i < args.size(); ++i) {
            made.data.push_back(static_cast<float>(context::to_number(args[i])));
        }
    }
    // PAD TO THE SHAPE, NEVER TRUNCATE TO IT.
    //
    // This was an unconditional `resize(rows * cols)`, which is right for a
    // scalar and wrong for an ARRAY uniform: `uniform4fv` with eight floats
    // kept four, a 32-float bone array kept one matrix, and
    // webgl_context::set_uniform then computed `count = size / per` as 1 every
    // time. Every array uniform in the engine was a one-element array uniform.
    const std::size_t shape = std::max<std::size_t>(1, static_cast<std::size_t>(rows) * cols);
    if (made.data.size() < shape) {
        made.data.resize(shape, 0.0f);
    } else {
        // A WHOLE NUMBER OF THEM. `count` is size/shape, so a partial trailing
        // element would be one the driver reads past the end of.
        made.data.resize(made.data.size() - made.data.size() % shape);
    }
    return made;
}

} // namespace

void dom_bindings::install_webgl_methods(context & cx, script::object_object * obj,
                                         webgl_context * gl, canvas_context * surface) {
    const auto method = [&](std::string name, script::native_fn fn) {
        obj->set(name, value::object(cx.allocate<script::native_object>(name, std::move(fn))));
    };
    // Every draw touches the canvas, and the browser learns a canvas changed by
    // its revision moving - so a call that writes pixels has to say so or the
    // frame shows the previous one.
    const auto touches = [surface](auto body) {
        return [surface, body](context & c, std::span<value> args) {
            const value out = body(c, args);
            surface->note_foreign_draw();
            return out;
        };
    };

    // --- objects
    //
    // A handle: a JS object carrying the integer the context knows it by. The
    // page only passes it back, so this is the whole of what one needs.
    const auto handle = [](context & c, std::uint32_t made_id, const char * kind) {
        auto * out = static_cast<script::object_object *>(c.make_object().as_heap());
        out->set("__id", value::number(made_id));
        out->set("__kind", c.string(kind));
        return value::object(out);
    };

    method("createBuffer", [gl, handle](context & c, std::span<value>) {
        return handle(c, gl->create_buffer(), "buffer");
    });
    method("createTexture", [gl, handle](context & c, std::span<value>) {
        return handle(c, gl->create_texture(), "texture");
    });
    method("createProgram", [gl, handle](context & c, std::span<value>) {
        return handle(c, gl->create_program(), "program");
    });
    method("createShader", [gl, handle](context & c, std::span<value> a) {
        return handle(c, gl->create_shader(enum_at(a, 0)), "shader");
    });
    // THE ENTRY POINT NAMES THE KIND, and the kind has to travel with the
    // handle. This loop already knew which of the six it was writing and threw
    // that away, so the device was left probing every GL namespace for the
    // number and deleting whatever else answered to it - which recycled live
    // buffer names under Babylon and cost this branch its geometry.
    //
    // Taken from the METHOD rather than from the handle's `__kind`, because
    // `deleteBuffer(x)` must delete a buffer whatever `x` claims to be. That is
    // what WebGL specifies and it is the safer of the two readings.
    using object_kind = raster::gl::device::object_kind;
    const std::pair<const char *, object_kind> deleters[] = {
        {"deleteBuffer", object_kind::buffer},
        {"deleteTexture", object_kind::texture},
        {"deleteProgram", object_kind::program},
        {"deleteShader", object_kind::shader},
        {"deleteFramebuffer", object_kind::framebuffer},
        {"deleteRenderbuffer", object_kind::renderbuffer}};
    for (const auto & [name, kind] : deleters) {
        method(name, [gl, kind = kind](context & c, std::span<value> a) {
            gl->delete_object(kind, id_of(c, a.empty() ? value::undefined() : a[0]));
            return value::undefined();
        });
    }

    method("bindBuffer", [gl](context & c, std::span<value> a) {
        gl->bind_buffer(enum_at(a, 0), id_of(c, a.size() > 1 ? a[1] : value::undefined()));
        return value::undefined();
    });
    method("bufferSubData", [gl](context & c, std::span<value> a) {
        const std::vector<std::byte> bytes = bytes_of(c, a.size() > 2 ? a[2] : value::undefined());
        gl->buffer_sub_data(enum_at(a, 0), int_at(a, 1), bytes);
        return value::undefined();
    });
    method("bufferData", [gl](context & c, std::span<value> a) {
        // `bufferData(target, size, usage)` ALLOCATES rather than uploading -
        // a page that means to reserve space passes a number, and treating that
        // as data would upload one float where it asked for a megabyte.
        if (a.size() > 1 && a[1].is_number()) {
            gl->buffer_data(enum_at(a, 0),
                            std::vector<std::byte>(static_cast<std::size_t>(number_at(a, 1))),
                            enum_at(a, 2));
            return value::undefined();
        }
        gl->buffer_data(enum_at(a, 0), bytes_of(c, a.size() > 1 ? a[1] : value::undefined()),
                        enum_at(a, 2));
        return value::undefined();
    });

    // --- shaders and programs
    method("shaderSource", [gl](context & c, std::span<value> a) {
        gl->shader_source(id_of(c, a.empty() ? value::undefined() : a[0]),
                          a.size() > 1 ? c.to_string(a[1]) : std::string{});
        return value::undefined();
    });
    method("compileShader", [gl](context & c, std::span<value> a) {
        gl->compile_shader(id_of(c, a.empty() ? value::undefined() : a[0]));
        return value::undefined();
    });
    method("getShaderParameter", [gl](context & c, std::span<value> a) {
        const std::uint32_t which = enum_at(a, 1);
        if (which == gl_enum::compile_status) {
            return value::boolean(
                gl->shader_compiled(id_of(c, a.empty() ? value::undefined() : a[0])));
        }
        return value::boolean(true);
    });
    method("getShaderInfoLog", [gl](context & c, std::span<value> a) {
        return c.string(gl->shader_log(id_of(c, a.empty() ? value::undefined() : a[0])));
    });
    method("attachShader", [gl](context & c, std::span<value> a) {
        gl->attach_shader(id_of(c, a.empty() ? value::undefined() : a[0]),
                          id_of(c, a.size() > 1 ? a[1] : value::undefined()));
        return value::undefined();
    });
    method("detachShader", [](context &, std::span<value>) { return value::undefined(); });
    method("linkProgram", [gl](context & c, std::span<value> a) {
        gl->link_program(id_of(c, a.empty() ? value::undefined() : a[0]));
        return value::undefined();
    });
    method("getProgramParameter", [gl](context & c, std::span<value> a) {
        const std::uint32_t which = enum_at(a, 1);
        const std::uint32_t program = id_of(c, a.empty() ? value::undefined() : a[0]);
        if (which == gl_enum::link_status || which == 0x8B83) {
            return value::boolean(gl->program_linked(program));
        }
        // ACTIVE_UNIFORMS and ACTIVE_ATTRIBUTES. These used to fall through to
        // the zero below, which is the shape of wrong answer this engine keeps
        // being bitten by: a library that ENUMERATES a program instead of
        // asking for names it already knows was told the shader declared
        // nothing, and bound nothing. See webgl.hpp.
        if (which == gl_enum::active_uniforms) {
            return value::number(static_cast<double>(gl->active_uniforms(program).size()));
        }
        if (which == gl_enum::active_attributes) {
            return value::number(static_cast<double>(gl->active_attributes(program).size()));
        }
        // Still zero for everything else, and still a guess. Anything a page
        // actually reads should be listed above rather than left to this.
        return value::number(0);
    });
    // WebGLActiveInfo: `{size, type, name}`. The INDEX is positional and is the
    // attribute's location as well, which is why active_attributes preserves
    // link order rather than sorting.
    const auto active_info = [](context & c, const webgl_context::active_variable & v) {
        value out = c.make_object();
        auto * made = static_cast<script::object_object *>(out.as_heap());
        made->set("name", c.string(v.name));
        // `size` is the ARRAY LENGTH, not the component count - 1 for a plain
        // uniform, and mat4 uColour[2] is size 2. Reporting components here
        // would make a caller loop the wrong number of times.
        made->set("size", value::number(v.size));
        made->set("type", value::number(v.type));
        return out;
    };
    method("getActiveUniform", [gl, active_info](context & c, std::span<value> a) {
        const auto all = gl->active_uniforms(id_of(c, a.empty() ? value::undefined() : a[0]));
        const auto i = static_cast<std::size_t>(std::max(0.0f, number(a, 1)));
        if (i >= all.size()) { return value::null(); }
        return active_info(c, all[i]);
    });
    method("getActiveAttrib", [gl, active_info](context & c, std::span<value> a) {
        const auto all = gl->active_attributes(id_of(c, a.empty() ? value::undefined() : a[0]));
        const auto i = static_cast<std::size_t>(std::max(0.0f, number(a, 1)));
        if (i >= all.size()) { return value::null(); }
        return active_info(c, all[i]);
    });
    // NOT A GL CALL. There is no such entry point in WebGL, because a compiled
    // GPU shader cannot fail per fragment - this evaluator can, and a draw that
    // silently wrote nothing is the hardest kind of failure to diagnose. Named
    // with the engine's prefix so nobody mistakes it for standard surface.
    method("ctbrowserShaderError",
           [gl](context & c, std::span<value>) { return c.string(gl->shader_error()); });
    method("getProgramInfoLog", [gl](context & c, std::span<value> a) {
        return c.string(gl->program_log(id_of(c, a.empty() ? value::undefined() : a[0])));
    });
    method("validateProgram", [](context &, std::span<value>) { return value::undefined(); });
    method("useProgram", [gl](context & c, std::span<value> a) {
        gl->use_program(id_of(c, a.empty() ? value::undefined() : a[0]));
        return value::undefined();
    });

    // --- attributes
    method("getAttribLocation", [gl](context & c, std::span<value> a) {
        return value::number(
            gl->attribute_location(id_of(c, a.empty() ? value::undefined() : a[0]),
                                   a.size() > 1 ? c.to_string(a[1]) : std::string{}));
    });
    method("bindAttribLocation", [](context &, std::span<value>) {
        // Locations are assigned at link time in declaration order here, so this
        // cannot be honoured. Silent rather than an error: a page calls it
        // defensively, and the locations it would ask for are the ones it gets.
        return value::undefined();
    });
    method("enableVertexAttribArray", [gl](context &, std::span<value> a) {
        gl->enable_attribute(int_at(a, 0), true);
        return value::undefined();
    });
    method("disableVertexAttribArray", [gl](context &, std::span<value> a) {
        gl->enable_attribute(int_at(a, 0), false);
        return value::undefined();
    });
    method("vertexAttribPointer", [gl](context &, std::span<value> a) {
        gl->attribute_pointer(int_at(a, 0), int_at(a, 1), enum_at(a, 2),
                              a.size() > 3 && context::truthy(a[3]), int_at(a, 4), int_at(a, 5));
        return value::undefined();
    });

    // --- uniforms
    //
    // A "location" here is the NAME. WebGL hands back an opaque object and a
    // page only ever passes it straight back, so a name is a location that is
    // also readable in a diagnostic.
    // --- uniform blocks, WebGL 2's way of delivering uniforms ---------------
    //
    // A page using them does not call `uniform4fv` at all: it fills a buffer and
    // binds it. Refused by name, these three left Babylon's every matrix reading
    // zero while its shaders linked and its draws were issued - a collapsed
    // scene on a canvas showing exactly the colour it was cleared to.
    method("getUniformBlockIndex", [gl](context & c, std::span<value> a) {
        const int index =
            gl->get_uniform_block_index(id_of(c, a.empty() ? value::undefined() : a[0]),
                                        a.size() > 1 ? c.to_string(a[1]) : std::string{});
        // GL_INVALID_INDEX, which is 0xFFFFFFFF and NOT an error: asking about a
        // block the shader does not have is a legitimate question.
        return index < 0 ? value::number(4294967295.0) : value::number(index);
    });
    method("uniformBlockBinding", [gl](context & c, std::span<value> a) {
        gl->uniform_block_binding(id_of(c, a.empty() ? value::undefined() : a[0]),
                                  static_cast<std::uint32_t>(int_at(a, 1)),
                                  static_cast<std::uint32_t>(int_at(a, 2)));
        return value::undefined();
    });
    method("bindBufferBase", [gl](context & c, std::span<value> a) {
        gl->bind_buffer_base(enum_at(a, 0), static_cast<std::uint32_t>(int_at(a, 1)),
                             id_of(c, a.size() > 2 ? a[2] : value::undefined()));
        return value::undefined();
    });
    method("getUniformLocation", [](context & c, std::span<value> a) {
        if (a.size() < 2) { return value::null(); }
        auto * out = static_cast<script::object_object *>(c.make_object().as_heap());
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
        method(shape.name, [gl, shape, uniform_name](context & c, std::span<value> a) {
            gl->set_uniform(uniform_name(c, a),
                            make_uniform(c, a, 1, shape.rows, shape.cols, shape.integer));
            return value::undefined();
        });
    }
    for (const uniform_shape & shape : {uniform_shape{"uniformMatrix2fv", 2, 2, false},
                                        uniform_shape{"uniformMatrix3fv", 3, 3, false},
                                        uniform_shape{"uniformMatrix4fv", 4, 4, false}}) {
        method(shape.name, [gl, shape, uniform_name](context & c, std::span<value> a) {
            // ARGUMENT 1 IS `transpose`, and the value is argument 2 - a
            // signature that catches everyone once. WebGL 1 requires transpose to
            // be false, so it is read and ignored rather than honoured.
            gl->set_uniform(uniform_name(c, a),
                            make_uniform(c, a, 2, shape.rows, shape.cols, shape.integer));
            return value::undefined();
        });
    }

    // --- state
    method("viewport", [gl](context &, std::span<value> a) {
        gl->viewport(int_at(a, 0), int_at(a, 1), int_at(a, 2), int_at(a, 3));
        return value::undefined();
    });
    method("scissor", [gl](context &, std::span<value> a) {
        gl->scissor(int_at(a, 0), int_at(a, 1), int_at(a, 2), int_at(a, 3));
        return value::undefined();
    });
    method("enable", [gl](context &, std::span<value> a) {
        gl->set_enabled(enum_at(a, 0), true);
        return value::undefined();
    });
    method("disable", [gl](context &, std::span<value> a) {
        gl->set_enabled(enum_at(a, 0), false);
        return value::undefined();
    });
    method("depthFunc", [gl](context &, std::span<value> a) {
        gl->depth_func(enum_at(a, 0));
        return value::undefined();
    });
    method("depthMask", [gl](context &, std::span<value> a) {
        gl->depth_mask(!a.empty() && context::truthy(a[0]));
        return value::undefined();
    });
    method("blendFunc", [gl](context &, std::span<value> a) {
        gl->blend_func(enum_at(a, 0), enum_at(a, 1));
        return value::undefined();
    });
    method("blendFuncSeparate", [gl](context &, std::span<value> a) {
        // The RGB pair only: this rasteriser blends alpha with the same factors,
        // which is what softgl.hpp's blend state can express. A page separating
        // them gets the colour pair applied to both.
        gl->blend_func(enum_at(a, 0), enum_at(a, 1));
        return value::undefined();
    });
    // ACCEPTED AND IGNORED. Two different reasons live in this list and it is
    // worth being honest about which is which:
    //
    //   genuinely nothing to do here - `flush`, `finish`, `hint`,
    //   `generateMipmap`, `pixelStorei` - a software rasteriser with no command
    //   queue and no mip chain has no work for them.
    //
    //   NOT IMPLEMENTED, and a page using them gets a wrong picture rather than
    //   an error - the whole stencil family, `colorMask`, `polygonOffset`,
    //   `sampleCoverage`, `lineWidth`, and the blend-equation calls. There is no
    //   stencil buffer in softgl.hpp at all, so a page masking with one simply
    //   draws unmasked.
    //
    // The second group SHOULD refuse by name the way the WebGL 2 surface does.
    // They do not yet because they are called during ordinary engine setup by
    // libraries that then render fine without them - Babylon sets stencil state
    // on every scene - and turning that into a torrent of refusals would bury
    // the diagnostics that matter. Recorded here rather than left as a
    // pleasant-looking list of no-ops; docs/raster.md carries the same note.
    for (const char * name :
         {"blendEquation", "blendEquationSeparate", "blendColor", "stencilFunc",
          "stencilFuncSeparate", "stencilOp", "stencilOpSeparate", "stencilMask",
          "stencilMaskSeparate", "clearStencil", "colorMask", "polygonOffset", "sampleCoverage",
          "hint", "lineWidth", "pixelStorei", "generateMipmap", "flush", "finish"}) {
        method(name, [](context &, std::span<value>) { return value::undefined(); });
    }
    method("cullFace", [gl](context &, std::span<value> a) {
        gl->cull_face(enum_at(a, 0));
        return value::undefined();
    });
    method("frontFace", [gl](context &, std::span<value> a) {
        gl->front_face(enum_at(a, 0));
        return value::undefined();
    });
    method("clearColor", [gl](context &, std::span<value> a) {
        gl->clear_color(static_cast<float>(number_at(a, 0)), static_cast<float>(number_at(a, 1)),
                        static_cast<float>(number_at(a, 2)), static_cast<float>(number_at(a, 3)));
        return value::undefined();
    });
    method("clearDepth", [gl](context &, std::span<value> a) {
        gl->clear_depth(static_cast<float>(number_at(a, 0)));
        return value::undefined();
    });
    method("clearStencil", [](context &, std::span<value>) { return value::undefined(); });
    method("clear", touches([gl](context &, std::span<value> a) {
               gl->clear(enum_at(a, 0));
               return value::undefined();
           }));

    // --- textures
    method("bindTexture", [gl](context & c, std::span<value> a) {
        gl->bind_texture(enum_at(a, 0), id_of(c, a.size() > 1 ? a[1] : value::undefined()));
        return value::undefined();
    });
    method("activeTexture", [gl](context &, std::span<value> a) {
        gl->active_texture(enum_at(a, 0));
        return value::undefined();
    });
    method("texParameteri", [gl](context &, std::span<value> a) {
        gl->texture_parameter(enum_at(a, 0), enum_at(a, 1), enum_at(a, 2));
        return value::undefined();
    });
    method("texParameterf", [gl](context &, std::span<value> a) {
        gl->texture_parameter(enum_at(a, 0), enum_at(a, 1), enum_at(a, 2));
        return value::undefined();
    });
    method("texImage2D", [this, gl](context & c, std::span<value> a) {
        // TWO SIGNATURES, and they are told apart by how many arguments arrived:
        // (target, level, internalformat, width, height, border, format, type,
        // pixels) is nine, and (target, level, internalformat, format, type,
        // source) is six - where `source` is an image, a canvas or an ImageData.
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
                const int w =
                    static_cast<int>(context::to_number(c.lookup_property(source, "width")));
                const int h =
                    static_cast<int>(context::to_number(c.lookup_property(source, "height")));
                gl->texture_image(enum_at(a, 0), w, h, bytes_of(c, data));
                return value::undefined();
            }
            if (const std::shared_ptr<const paint::bitmap> pixels = image_argument(source)) {
                gl->texture_from_bitmap(enum_at(a, 0), *pixels);
            }
        }
        return value::undefined();
    });
    method("texSubImage2D", [](context &, std::span<value>) { return value::undefined(); });
}

} // namespace ctbrowser::shell
