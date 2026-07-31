#include <ctbrowser/shell/bindings.hpp>

#include <cstring>

// `canvas.getContext('webgl')` - the JavaScript surface over shell/webgl.hpp.
//
// IN ITS OWN FILE because it is a different kind of code from the rest of the
// bindings: seventy-nine methods that almost all do one thing, plus a constant
// table. Mixed into bindings.cpp it would double that file and bury the DOM.
//
// This layer is DELIBERATELY THIN. It unpacks arguments, hands them to
// webgl_context, and packs the answer back; every decision about what a call
// MEANS lives next door in webgl.cpp, where it is testable without a page. If
// something here is more than a few lines, it is in the wrong file.
//
// A WebGL object - a buffer, a texture, a program - is a JS object carrying an
// integer id. The page only ever passes them back, so the object is a handle and
// the integer is what the context knows.

namespace ctbrowser::shell {
namespace {

using script::value;
using context = script::context;

[[nodiscard]] std::uint32_t id_of(context & cx, value v) {
    if (!v.is_object()) { return 0; }
    const value held = cx.lookup_property(v, "__id");
    return held.is_undefined() ? 0 : static_cast<std::uint32_t>(context::to_number(held));
}

[[nodiscard]] double number_at(std::span<value> args, std::size_t i) {
    return i < args.size() ? context::to_number(args[i]) : 0.0;
}

[[nodiscard]] std::uint32_t enum_at(std::span<value> args, std::size_t i) {
    return static_cast<std::uint32_t>(number_at(args, i));
}

[[nodiscard]] int int_at(std::span<value> args, std::size_t i) {
    return static_cast<int>(number_at(args, i));
}

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
[[nodiscard]] raster::glsl::value uniform_value(context & cx, std::span<value> args,
                                                std::size_t from, std::uint8_t rows,
                                                std::uint8_t cols, raster::glsl::base kind) {
    raster::glsl::value made;
    made.t = raster::glsl::type{kind, rows, cols, -1, 0};
    // Either loose numbers - uniform3f(x, y, z) - or one array, which is what
    // the `v` suffix means.
    if (args.size() > from && args[from].is_array()) {
        auto * array = static_cast<script::array_object *>(args[from].as_heap());
        for (const value & each : array->items) {
            made.v.push_back(static_cast<float>(context::to_number(each)));
        }
    } else if (args.size() > from && args[from].is_object()) {
        const value held = cx.lookup_property(args[from], "__bytes");
        if (held.is_array()) {
            for (const value & each : static_cast<script::array_object *>(held.as_heap())->items) {
                made.v.push_back(static_cast<float>(context::to_number(each)));
            }
        }
    } else {
        for (std::size_t i = from; i < args.size(); ++i) {
            made.v.push_back(static_cast<float>(context::to_number(args[i])));
        }
    }
    made.v.resize(static_cast<std::size_t>(rows) * cols, 0.0f);
    return made;
}

} // namespace

void dom_bindings::resize_webgl_context(node_id id, int width, int height) {
    const auto found = webgl_contexts_.find(pack(id));
    if (found == webgl_contexts_.end() || !found->second || canvases_ == nullptr) { return; }
    canvas_context * surface = canvases_->context_for(id, width, height);
    if (surface == nullptr) { return; }
    found->second->resize(const_cast<paint::bitmap *>(surface->surface().get()), width, height);
    // The page reads these, and p5 reads them to size its projection matrix.
    if (const auto seen = webgl_objects_.find(pack(id)); seen != webgl_objects_.end()) {
        seen->second->set("drawingBufferWidth", value::number(width));
        seen->second->set("drawingBufferHeight", value::number(height));
    }
}

value dom_bindings::webgl_context_object(context & cx, node_id id) {
    if (canvases_ == nullptr) { return value::null(); }
    const auto txn = doc_->read();
    const auto attribute = [&](std::string_view name, int fallback) {
        const std::string_view text = txn.attribute_value(id, atoms_->intern(name));
        int out = 0;
        bool any = false;
        for (const char c : text) {
            if (c < '0' || c > '9') { break; }
            out = out * 10 + (c - '0');
            any = true;
        }
        return any ? out : fallback;
    };
    const int width = attribute("width", 300);
    const int height = attribute("height", 150);
    // THE SAME SURFACE THE 2D PATH DRAWS INTO. A canvas has one set of pixels
    // whichever context it handed out, and the painter reads them through
    // canvases_->pixels_of - so a WebGL draw has to land there or nothing
    // composites it.
    canvas_context * surface = canvases_->context_for(id, width, height);
    if (surface == nullptr) { return value::null(); }

    auto & made = webgl_contexts_[pack(id)];
    if (!made) {
        made = std::make_unique<webgl_context>(
            const_cast<paint::bitmap *>(surface->surface().get()), width, height);
    }
    webgl_context * gl = made.get();

    // THE SAME JS OBJECT, not just the same state. getContext is idempotent in
    // the spec, and a page compares what it gets - `if (this.gl !== canvas
    // .getContext('webgl'))` is a real pattern - so handing back a fresh wrapper
    // each call is observably wrong even when the state behind it is shared.
    if (const auto seen = webgl_objects_.find(pack(id)); seen != webgl_objects_.end()) {
        return value::object(seen->second);
    }

    auto * obj = static_cast<script::object_object *>(cx.make_object().as_heap());
    webgl_objects_[pack(id)] = obj;
    obj->set("canvas", wrap(cx, id));
    obj->set("drawingBufferWidth", value::number(width));
    obj->set("drawingBufferHeight", value::number(height));

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

    // --- the constants
    //
    // Set as properties rather than resolved per call: a page reads gl.TRIANGLES
    // far more often than it calls anything, and a lookup is a lookup.
    const auto constant = [&](const char * name, std::uint32_t v) {
        obj->set(name, value::number(v));
    };
    constant("DEPTH_BUFFER_BIT", gl_enum::depth_buffer_bit);
    constant("STENCIL_BUFFER_BIT", gl_enum::stencil_buffer_bit);
    constant("COLOR_BUFFER_BIT", gl_enum::color_buffer_bit);
    constant("POINTS", gl_enum::points);
    constant("LINES", gl_enum::lines);
    constant("LINE_LOOP", 0x0002);
    constant("LINE_STRIP", 0x0003);
    constant("TRIANGLES", gl_enum::triangles);
    constant("TRIANGLE_STRIP", gl_enum::triangle_strip);
    constant("TRIANGLE_FAN", gl_enum::triangle_fan);
    constant("DEPTH_TEST", gl_enum::depth_test);
    constant("BLEND", gl_enum::blend);
    constant("CULL_FACE", gl_enum::cull_face);
    constant("SCISSOR_TEST", gl_enum::scissor_test);
    constant("DITHER", 0x0BD0);
    constant("STENCIL_TEST", 0x0B90);
    constant("POLYGON_OFFSET_FILL", 0x8037);
    constant("SAMPLE_ALPHA_TO_COVERAGE", 0x809E);
    constant("SAMPLE_COVERAGE", 0x80A0);
    constant("FRONT", gl_enum::front);
    constant("BACK", gl_enum::back);
    constant("FRONT_AND_BACK", 0x0408);
    constant("CW", gl_enum::cw);
    constant("CCW", gl_enum::ccw);
    constant("NEVER", gl_enum::never);
    constant("LESS", gl_enum::less);
    constant("EQUAL", gl_enum::equal);
    constant("LEQUAL", gl_enum::lequal);
    constant("GREATER", gl_enum::greater);
    constant("NOTEQUAL", gl_enum::notequal);
    constant("GEQUAL", gl_enum::gequal);
    constant("ALWAYS", gl_enum::always);
    constant("KEEP", 0x1E00);
    constant("REPLACE", 0x1E01);
    constant("ZERO", gl_enum::zero);
    constant("ONE", gl_enum::one);
    constant("SRC_COLOR", gl_enum::src_color);
    constant("ONE_MINUS_SRC_COLOR", gl_enum::one_minus_src_color);
    constant("SRC_ALPHA", gl_enum::src_alpha);
    constant("ONE_MINUS_SRC_ALPHA", gl_enum::one_minus_src_alpha);
    constant("DST_ALPHA", gl_enum::dst_alpha);
    constant("ONE_MINUS_DST_ALPHA", gl_enum::one_minus_dst_alpha);
    constant("DST_COLOR", gl_enum::dst_color);
    constant("ONE_MINUS_DST_COLOR", gl_enum::one_minus_dst_color);
    constant("SRC_ALPHA_SATURATE", 0x0308);
    constant("FUNC_ADD", 0x8006);
    constant("FUNC_SUBTRACT", 0x800A);
    constant("FUNC_REVERSE_SUBTRACT", 0x800B);
    constant("BYTE", gl_enum::byte_);
    constant("UNSIGNED_BYTE", gl_enum::unsigned_byte);
    constant("SHORT", gl_enum::short_);
    constant("UNSIGNED_SHORT", gl_enum::unsigned_short);
    constant("INT", gl_enum::int_);
    constant("UNSIGNED_INT", gl_enum::unsigned_int);
    constant("FLOAT", gl_enum::float_);
    constant("ARRAY_BUFFER", gl_enum::array_buffer);
    constant("ELEMENT_ARRAY_BUFFER", gl_enum::element_array_buffer);
    constant("STATIC_DRAW", gl_enum::static_draw);
    constant("DYNAMIC_DRAW", gl_enum::dynamic_draw);
    constant("STREAM_DRAW", 0x88E0);
    constant("TEXTURE_2D", gl_enum::texture_2d);
    constant("TEXTURE_CUBE_MAP", 0x8513);
    constant("RGBA", gl_enum::rgba);
    constant("RGB", gl_enum::rgb);
    constant("LUMINANCE", 0x1909);
    constant("LUMINANCE_ALPHA", 0x190A);
    constant("ALPHA", 0x1906);
    constant("NEAREST", gl_enum::nearest);
    constant("LINEAR", gl_enum::linear);
    constant("NEAREST_MIPMAP_NEAREST", 0x2700);
    constant("LINEAR_MIPMAP_NEAREST", 0x2701);
    constant("NEAREST_MIPMAP_LINEAR", 0x2702);
    constant("LINEAR_MIPMAP_LINEAR", 0x2703);
    constant("TEXTURE_MAG_FILTER", gl_enum::texture_mag_filter);
    constant("TEXTURE_MIN_FILTER", gl_enum::texture_min_filter);
    constant("TEXTURE_WRAP_S", gl_enum::texture_wrap_s);
    constant("TEXTURE_WRAP_T", gl_enum::texture_wrap_t);
    constant("CLAMP_TO_EDGE", gl_enum::clamp_to_edge);
    constant("REPEAT", gl_enum::repeat);
    constant("MIRRORED_REPEAT", 0x8370);
    constant("UNPACK_FLIP_Y_WEBGL", 0x9240);
    constant("UNPACK_PREMULTIPLY_ALPHA_WEBGL", 0x9241);
    constant("UNPACK_ALIGNMENT", 0x0CF5);
    constant("COMPILE_STATUS", gl_enum::compile_status);
    constant("LINK_STATUS", gl_enum::link_status);
    constant("VALIDATE_STATUS", 0x8B83);
    constant("DELETE_STATUS", 0x8B80);
    constant("VERTEX_SHADER", gl_enum::vertex_shader);
    constant("FRAGMENT_SHADER", gl_enum::fragment_shader);
    constant("ACTIVE_UNIFORMS", 0x8B86);
    constant("ACTIVE_ATTRIBUTES", 0x8B89);
    constant("MAX_TEXTURE_SIZE", gl_enum::max_texture_size);
    constant("MAX_VERTEX_ATTRIBS", gl_enum::max_vertex_attribs);
    constant("MAX_TEXTURE_IMAGE_UNITS", 0x8872);
    constant("MAX_VIEWPORT_DIMS", 0x0D3A);
    constant("VERSION", gl_enum::version);
    constant("RENDERER", gl_enum::renderer);
    constant("VENDOR", gl_enum::vendor);
    constant("SHADING_LANGUAGE_VERSION", gl_enum::shading_language_version);
    constant("NO_ERROR", gl_enum::no_error);
    constant("INVALID_ENUM", gl_enum::invalid_enum);
    constant("INVALID_VALUE", gl_enum::invalid_value);
    constant("INVALID_OPERATION", gl_enum::invalid_operation);
    constant("OUT_OF_MEMORY", 0x0505);
    constant("FRAMEBUFFER", 0x8D40);
    constant("RENDERBUFFER", 0x8D41);
    constant("DEPTH_COMPONENT16", 0x81A5);
    constant("DEPTH_ATTACHMENT", 0x8D00);
    constant("COLOR_ATTACHMENT0", 0x8CE0);
    constant("FRAMEBUFFER_COMPLETE", 0x8CD5);
    for (int unit = 0; unit < 16; ++unit) {
        constant(("TEXTURE" + std::to_string(unit)).c_str(),
                 gl_enum::texture0 + static_cast<std::uint32_t>(unit));
    }

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
    for (const char * name : {"deleteBuffer", "deleteTexture", "deleteProgram", "deleteShader",
                              "deleteFramebuffer", "deleteRenderbuffer"}) {
        method(name, [gl](context & c, std::span<value> a) {
            gl->delete_object(id_of(c, a.empty() ? value::undefined() : a[0]));
            return value::undefined();
        });
    }

    method("bindBuffer", [gl](context & c, std::span<value> a) {
        gl->bind_buffer(enum_at(a, 0), id_of(c, a.size() > 1 ? a[1] : value::undefined()));
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
        if (which == gl_enum::link_status || which == 0x8B83) {
            return value::boolean(
                gl->program_linked(id_of(c, a.empty() ? value::undefined() : a[0])));
        }
        return value::number(0);
    });
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
        raster::glsl::base kind;
    };
    for (const uniform_shape & shape : {uniform_shape{"uniform1f", 1, 1, raster::glsl::base::f},
                                        uniform_shape{"uniform2f", 2, 1, raster::glsl::base::f},
                                        uniform_shape{"uniform3f", 3, 1, raster::glsl::base::f},
                                        uniform_shape{"uniform4f", 4, 1, raster::glsl::base::f},
                                        uniform_shape{"uniform1i", 1, 1, raster::glsl::base::i},
                                        uniform_shape{"uniform2i", 2, 1, raster::glsl::base::i},
                                        uniform_shape{"uniform3i", 3, 1, raster::glsl::base::i},
                                        uniform_shape{"uniform4i", 4, 1, raster::glsl::base::i},
                                        uniform_shape{"uniform1fv", 1, 1, raster::glsl::base::f},
                                        uniform_shape{"uniform2fv", 2, 1, raster::glsl::base::f},
                                        uniform_shape{"uniform3fv", 3, 1, raster::glsl::base::f},
                                        uniform_shape{"uniform4fv", 4, 1, raster::glsl::base::f},
                                        uniform_shape{"uniform1iv", 1, 1, raster::glsl::base::i},
                                        uniform_shape{"uniform2iv", 2, 1, raster::glsl::base::i},
                                        uniform_shape{"uniform3iv", 3, 1, raster::glsl::base::i},
                                        uniform_shape{"uniform4iv", 4, 1, raster::glsl::base::i}}) {
        method(shape.name, [gl, shape, uniform_name](context & c, std::span<value> a) {
            gl->set_uniform(uniform_name(c, a),
                            uniform_value(c, a, 1, shape.rows, shape.cols, shape.kind));
            return value::undefined();
        });
    }
    for (const uniform_shape & shape :
         {uniform_shape{"uniformMatrix2fv", 2, 2, raster::glsl::base::f},
          uniform_shape{"uniformMatrix3fv", 3, 3, raster::glsl::base::f},
          uniform_shape{"uniformMatrix4fv", 4, 4, raster::glsl::base::f}}) {
        method(shape.name, [gl, shape, uniform_name](context & c, std::span<value> a) {
            // ARGUMENT 1 IS `transpose`, and the value is argument 2 - a
            // signature that catches everyone once. WebGL 1 requires transpose to
            // be false, so it is read and ignored rather than honoured.
            gl->set_uniform(uniform_name(c, a),
                            uniform_value(c, a, 2, shape.rows, shape.cols, shape.kind));
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
    for (const char * name :
         {"blendEquation", "blendEquationSeparate", "blendColor", "stencilFunc", "stencilOp",
          "stencilMask", "colorMask", "polygonOffset", "sampleCoverage", "hint", "lineWidth",
          "pixelStorei", "generateMipmap", "flush", "finish"}) {
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

    // --- drawing
    method("drawArrays", touches([gl](context &, std::span<value> a) {
               (void)gl->draw_arrays(enum_at(a, 0), int_at(a, 1), int_at(a, 2));
               return value::undefined();
           }));
    method("drawElements", touches([gl](context &, std::span<value> a) {
               (void)gl->draw_elements(enum_at(a, 0), int_at(a, 1), enum_at(a, 2), int_at(a, 3));
               return value::undefined();
           }));
    for (const char * name : {"drawArraysInstanced", "drawElementsInstanced"}) {
        method(name, [](context &, std::span<value>) {
            // Instancing is named as out of scope in docs/webgl-plan.md. Doing
            // nothing is the honest answer; drawing ONE instance would look like
            // it worked and be wrong by however many were asked for.
            return value::undefined();
        });
    }

    // --- reading back
    method("getError",
           [gl](context &, std::span<value>) { return value::number(gl->take_error()); });
    method("getParameter", [width, height](context & c, std::span<value> a) {
        switch (enum_at(a, 0)) {
        case gl_enum::version: return c.string("WebGL 1.0 (ctbrowser software)");
        case gl_enum::shading_language_version: return c.string("WebGL GLSL ES 1.0");
        case gl_enum::vendor: return c.string("ctbrowser");
        case gl_enum::renderer: return c.string("ctbrowser software rasteriser");
        case gl_enum::max_texture_size: return value::number(4096);
        case gl_enum::max_vertex_attribs: return value::number(16);
        case 0x8872: return value::number(16); // MAX_TEXTURE_IMAGE_UNITS
        case 0x0D3A: {                         // MAX_VIEWPORT_DIMS
            const value out = c.make_array();
            auto * items = static_cast<script::array_object *>(out.as_heap());
            items->items.push_back(value::number(width));
            items->items.push_back(value::number(height));
            return out;
        }
        default: return value::number(0);
        }
    });
    method("getExtension", [](context &, std::span<value>) {
        // NULL FOR EVERY EXTENSION, which is what a driver without one returns
        // and what a page checks for. Handing back an object would make a page
        // take a path this cannot honour.
        return value::null();
    });
    method("getSupportedExtensions", [](context & c, std::span<value>) { return c.make_array(); });
    method("isContextLost", [](context &, std::span<value>) { return value::boolean(false); });
    method("readPixels", [gl](context & c, std::span<value> a) {
        // Into the caller's typed array, which is how a page gets pixels back.
        //
        // A TYPED ARRAY *IS* AN ARRAY HERE. Only an ArrayBuffer carries its data
        // in `__bytes`; a Uint8Array is an array_object with an element kind. I
        // looked only at `__bytes` first, so readPixels wrote nothing at all and
        // the page read four zeros - which looked exactly like a triangle that
        // had not drawn, and sent me looking in the wrong place.
        if (a.size() < 7) { return value::undefined(); }
        value into = a[6];
        if (into.is_object()) {
            const value held = c.lookup_property(into, "__bytes");
            if (held.is_array()) { into = held; }
        }
        if (!into.is_array()) { return value::undefined(); }
        auto * out = static_cast<script::array_object *>(into.as_heap());
        const paint::bitmap * from = gl->surface();
        if (from == nullptr) { return value::undefined(); }
        const int x = int_at(a, 0);
        const int y = int_at(a, 1);
        const int w = int_at(a, 2);
        const int h = int_at(a, 3);
        std::size_t at = 0;
        for (int row = 0; row < h; ++row) {
            for (int column = 0; column < w; ++column) {
                // GL READS BOTTOM-UP: row 0 of the result is the BOTTOM row of
                // the surface, which is the same origin flip the viewport has.
                const int sy = gl->height() - 1 - (y + row);
                const std::uint32_t texel =
                    (sy < 0 || sy >= from->height) ? 0 : from->at(x + column, sy);
                for (const std::uint32_t shift : {16u, 8u, 0u, 24u}) { // RGBA out
                    if (at < out->items.size()) {
                        out->items[at] = value::number((texel >> shift) & 0xFF);
                    }
                    ++at;
                }
            }
        }
        return value::undefined();
    });

    // Framebuffers and renderbuffers: created, bound, and NOT honoured - every
    // draw goes to the canvas. p5 creates them for its filter path and checks
    // completeness, so refusing outright would stop it starting; drawing into
    // the canvas instead is wrong in a way the corpus page would show, and is
    // named in docs/webgl-plan.md rather than hidden here.
    method("createFramebuffer", [gl, handle](context & c, std::span<value>) {
        return handle(c, gl->create_buffer(), "framebuffer");
    });
    method("createRenderbuffer", [gl, handle](context & c, std::span<value>) {
        return handle(c, gl->create_buffer(), "renderbuffer");
    });
    for (const char * name :
         {"bindFramebuffer", "bindRenderbuffer", "framebufferTexture2D", "framebufferRenderbuffer",
          "renderbufferStorage", "renderbufferStorageMultisample", "blitFramebuffer"}) {
        method(name, [](context &, std::span<value>) { return value::undefined(); });
    }
    method("checkFramebufferStatus", [](context &, std::span<value>) {
        return value::number(0x8CD5); // FRAMEBUFFER_COMPLETE
    });

    return value::object(obj);
}

} // namespace ctbrowser::shell
