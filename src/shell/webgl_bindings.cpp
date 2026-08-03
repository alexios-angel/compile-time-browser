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

// A double as an unsigned index, WITHOUT the undefined behaviour. Every entry
// point here takes numbers from a page, and `undefined` arrives as NaN while
// `1/0` arrives as infinity - neither of which CONVERTS to an integer. The cast
// is undefined behaviour rather than a large number, and UBSan found one
// reaching enum_at where a page passed a null location.
[[nodiscard]] std::uint32_t to_index(double d) noexcept {
    if (!(d >= 0.0)) { return 0; } // false for NaN as well as for negatives
    return d >= 4294967295.0 ? 4294967295u : static_cast<std::uint32_t>(d);
}

[[nodiscard]] std::uint32_t id_of(context & cx, value v) {
    if (!v.is_object()) { return 0; }
    const value held = cx.lookup_property(v, "__id");
    return held.is_undefined() ? 0 : to_index(context::to_number(held));
}

[[nodiscard]] double number_at(std::span<value> args, std::size_t i) {
    return i < args.size() ? context::to_number(args[i]) : 0.0;
}

[[nodiscard]] std::uint32_t enum_at(std::span<value> args, std::size_t i) {
    return to_index(number_at(args, i));
}

[[nodiscard]] int int_at(std::span<value> args, std::size_t i) {
    const double d = number_at(args, i);
    if (std::isnan(d)) { return 0; }
    return static_cast<int>(std::clamp(d, static_cast<double>(std::numeric_limits<int>::min()),
                                       static_cast<double>(std::numeric_limits<int>::max())));
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

value dom_bindings::webgl_context_object(context & cx, node_id id, int version) {
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
    // `made` IS A REFERENCE INTO THE MAP, so it is non-null from here on
    // whatever happened - which is why the freshness has to be captured before
    // the create rather than tested after it. Checking `made != nullptr`
    // afterwards is always true, and that mistake converted an existing WebGL 1
    // context into a WebGL 2 one the moment a page asked for `webgl2`.
    const bool created = !made;
    if (!made) {
        made = std::make_unique<webgl_context>(
            const_cast<paint::bitmap *>(surface->surface().get()), width, height);
    }
    webgl_context * gl = made.get();
    // THE VERSION IS DECIDED ONCE, WHEN THE CONTEXT IS CREATED. A canvas has
    // one context type for ever: the spec says a request for a different id
    // returns null rather than converting what is there, and the cache check
    // below is where that null comes from.
    if (created && version >= 2) { gl->set_version(2); }
    const bool webgl2 = gl->version() >= 2;

    // THE SAME JS OBJECT, not just the same state. getContext is idempotent in
    // the spec, and a page compares what it gets - `if (this.gl !== canvas
    // .getContext('webgl'))` is a real pattern - so handing back a fresh wrapper
    // each call is observably wrong even when the state behind it is shared.
    if (const auto seen = webgl_objects_.find(pack(id)); seen != webgl_objects_.end()) {
        // A CANVAS HAS ONE CONTEXT TYPE, FOR EVER. The spec is explicit: once
        // a canvas has a context, asking for a DIFFERENT id returns null - it
        // does not convert, and it does not hand back the one it has under the
        // wrong name. `webgl` and `webgl2` are different ids.
        //
        // Handing back the WebGL 1 object for a `webgl2` request is the shape
        // that matters here: a page would feature-detect on a non-null answer
        // and then reach for constants and methods that are not on it.
        if (gl->version() != version) { return value::null(); }
        return value::object(seen->second);
    }

    auto * obj = static_cast<script::object_object *>(cx.make_object().as_heap());
    // So `gl instanceof WebGLRenderingContext` is true, which Phaser asks - and
    // `instanceof WebGL2RenderingContext` for a version 2 context, which is a
    // DIFFERENT interface rather than a subclass. A page gets the same answer
    // from both tests that a browser would give it.
    const value & interface_prototype = webgl2 ? webgl2_prototype_ : webgl_prototype_;
    if (interface_prototype.is_object()) { obj->prototype = interface_prototype; }
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
    // THE TYPE CODES getActiveUniform REPORTS, and they have to be here because
    // a caller SWITCHES on them against these very constants:
    //
    //     switch (uniform.type) { case gl.FLOAT_MAT4: ... }
    //
    // is p5's uniform dispatch. Without the constant, `gl.FLOAT_MAT4` is
    // undefined, no case matches, there is no default, and every uniform is
    // dropped in silence - which is what left a correctly-built cube with no
    // matrices and an empty canvas. Named from gl_enum so they cannot drift
    // apart from what gl_type_code answers; webgl_basics asserts they agree.
    constant("FLOAT_VEC2", gl_enum::float_vec2);
    constant("FLOAT_VEC3", gl_enum::float_vec3);
    constant("FLOAT_VEC4", gl_enum::float_vec4);
    constant("INT_VEC2", gl_enum::int_vec2);
    constant("INT_VEC3", gl_enum::int_vec3);
    constant("INT_VEC4", gl_enum::int_vec4);
    constant("BOOL", gl_enum::bool_);
    constant("BOOL_VEC2", gl_enum::bool_vec2);
    constant("BOOL_VEC3", gl_enum::bool_vec3);
    constant("BOOL_VEC4", gl_enum::bool_vec4);
    constant("FLOAT_MAT2", gl_enum::float_mat2);
    constant("FLOAT_MAT3", gl_enum::float_mat3);
    constant("FLOAT_MAT4", gl_enum::float_mat4);
    constant("SAMPLER_2D", gl_enum::sampler_2d);
    constant("SAMPLER_CUBE", gl_enum::sampler_cube);
    constant("MAX_TEXTURE_SIZE", gl_enum::max_texture_size);
    constant("MAX_VERTEX_ATTRIBS", gl_enum::max_vertex_attribs);
    constant("MAX_TEXTURE_IMAGE_UNITS", 0x8872);
    constant("MAX_VIEWPORT_DIMS", 0x0D3A);
    // --- WebGL 2 only ------------------------------------------------------
    // Dull, and load-bearing: a constant that arrives as `undefined` makes every
    // comparison against it silently false, so a page takes a path nobody
    // intended and nothing reports an error. Set only on a WebGL 2 context,
    // because a WebGL 1 page that finds `gl.RGBA8` concludes it has WebGL 2.
    //
    // THE VALUES ARE THE SPECIFICATION'S. They are not derived from anything -
    // they are the numbers every driver reports, and a page comparing against
    // 0x8058 wants exactly 0x8058.
    if (webgl2) {
        constant("RGBA8", 0x8058);
        constant("RGB8", 0x8051);
        constant("SRGB8_ALPHA8", 0x8C43);
        constant("R8", 0x8229);
        constant("RG8", 0x822B);
        constant("RGBA16F", 0x881A);
        constant("RGBA32F", 0x8814);
        constant("DEPTH_COMPONENT24", 0x81A6);
        constant("DEPTH24_STENCIL8", 0x88F0);
        constant("TEXTURE_3D", 0x806F);
        constant("TEXTURE_2D_ARRAY", 0x8C1A);
        constant("UNIFORM_BUFFER", 0x8A11);
        constant("COPY_READ_BUFFER", 0x8F36);
        constant("PIXEL_PACK_BUFFER", 0x88EB);
        constant("VERTEX_ARRAY_BINDING", 0x85B5);
        constant("TRANSFORM_FEEDBACK", 0x8E22);
        constant("SYNC_GPU_COMMANDS_COMPLETE", 0x9117);
        constant("MAX_DRAW_BUFFERS", 0x8824);
        constant("MAX_COLOR_ATTACHMENTS", 0x8CDF);
        constant("DRAW_BUFFER0", 0x8825);
        for (std::uint32_t i = 0; i < 8; ++i) {
            constant((std::string{"COLOR_ATTACHMENT"} + std::to_string(i)).c_str(), 0x8CE0 + i);
            constant((std::string{"DRAW_BUFFER"} + std::to_string(i)).c_str(), 0x8825 + i);
        }
        constant("VERTEX_ATTRIB_ARRAY_DIVISOR", 0x88FE);
    }
    // WHAT getVertexAttrib IS ASKED. WebGL 1 constants, so they sit out here
    // rather than in the block above - the divisor one is the exception and
    // stays there, because the divisor only exists with the extension or with
    // WebGL 2.
    constant("VERTEX_ATTRIB_ARRAY_ENABLED", 0x8622);
    constant("VERTEX_ATTRIB_ARRAY_SIZE", 0x8623);
    constant("VERTEX_ATTRIB_ARRAY_STRIDE", 0x8624);
    constant("VERTEX_ATTRIB_ARRAY_TYPE", 0x8625);
    constant("VERTEX_ATTRIB_ARRAY_NORMALIZED", 0x886A);
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
        made->set("size", value::number(v.t.array > 0 ? v.t.array : 1));
        made->set("type", value::number(gl_type_code(v.t)));
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

    // --- drawing
    // --- WebGL 2's spelling of what the extensions already expose -----------
    // THE SAME webgl_context METHODS the OES/ANGLE objects call. That is the
    // decision docs/webgl2-plan.md records, and it is why this arrives already
    // exercised: Phaser has been driving VAOs and instancing through the
    // extension names since stage 2, so this binds names to code a real
    // renderer has been using rather than to code written for a test.
    // `getVertexAttrib`, which is how a page CHECKS that a vertex array
    // object captured anything. Without it the VAO probe could not tell a
    // working implementation from a stub that remembered nothing, because
    // every other VAO call returns void.
    // `getShaderPrecisionFormat`. Babylon asks for it during capability
    // detection and sets `highPrecisionShaderSupported` from the answer;
    // returning undefined made it decide this engine has no high-precision
    // floats and pick its lower-precision shader paths.
    //
    // THE NUMBERS ARE IEEE SINGLE, because that is literally what the software
    // rasteriser computes in: raster/glsl_eval.cpp holds every float in a C++
    // `float`, so highp, mediump and lowp are all the same type here and saying
    // otherwise would be a lie a page could act on.
    method("getShaderPrecisionFormat", [](context & c, std::span<value> a) {
        const std::uint32_t kind = enum_at(a, 1);
        value out = c.make_object();
        auto * o = static_cast<script::object_object *>(out.as_heap());
        // INT_* are 0x8DF4..0x8DF6; the float ones are 0x8DF0..0x8DF2.
        const bool integer = kind >= 0x8DF4 && kind <= 0x8DF6;
        o->set("rangeMin", value::number(integer ? 31 : 127));
        o->set("rangeMax", value::number(integer ? 31 : 127));
        o->set("precision", value::number(integer ? 0 : 23));
        return out;
    });
    method("getVertexAttrib", [gl](context &, std::span<value> a) {
        const vertex_attribute * where = gl->attribute_at(int_at(a, 0));
        if (where == nullptr) { return value::null(); }
        switch (enum_at(a, 1)) {
        case 0x8622: return value::boolean(where->enabled);    // ARRAY_ENABLED
        case 0x8623: return value::number(where->size);        // ARRAY_SIZE
        case 0x8624: return value::number(where->stride);      // ARRAY_STRIDE
        case 0x8625: return value::number(where->type);        // ARRAY_TYPE
        case 0x886A: return value::boolean(where->normalized); // ARRAY_NORMALIZED
        case 0x88FE: return value::number(where->divisor);     // ARRAY_DIVISOR
        default: return value::null();
        }
    });

    if (webgl2) {
        method("createVertexArray", [gl](context &, std::span<value>) {
            return value::number(gl->create_vertex_array());
        });
        method("bindVertexArray", [gl](context &, std::span<value> a) {
            gl->bind_vertex_array(
                a.empty() || !a[0].is_number() ? 0U : static_cast<std::uint32_t>(a[0].as_number()));
            return value::undefined();
        });
        method("deleteVertexArray", [gl](context &, std::span<value> a) {
            if (!a.empty() && a[0].is_number()) {
                gl->delete_vertex_array(static_cast<std::uint32_t>(a[0].as_number()));
            }
            return value::undefined();
        });
        method("isVertexArray", [gl](context &, std::span<value> a) {
            return value::boolean(
                !a.empty() && a[0].is_number() &&
                gl->is_vertex_array(static_cast<std::uint32_t>(a[0].as_number())));
        });
        method("vertexAttribDivisor", [gl](context &, std::span<value> a) {
            gl->attribute_divisor(
                !a.empty() && a[0].is_number() ? static_cast<int>(a[0].as_number()) : -1,
                a.size() > 1 && a[1].is_number() ? static_cast<int>(a[1].as_number()) : 0);
            return value::undefined();
        });
        method("drawArraysInstanced", touches([gl](context &, std::span<value> a) {
                   (void)gl->draw_arrays_instanced(enum_at(a, 0), int_at(a, 1), int_at(a, 2),
                                                   int_at(a, 3));
                   return value::undefined();
               }));
        method("drawElementsInstanced", touches([gl](context &, std::span<value> a) {
                   (void)gl->draw_elements_instanced(enum_at(a, 0), int_at(a, 1), enum_at(a, 2),
                                                     int_at(a, 3), int_at(a, 4));
                   return value::undefined();
               }));

        // STAGE 5 of docs/webgl2-plan.md: THE HALF THIS ENGINE DOES NOT
        // IMPLEMENT, present by name and refusing loudly.
        //
        // A page that asked for `webgl2` and got one does not feature-detect
        // these - it calls them, because in a browser a WebGL 2 context always
        // has them. Leaving them off turns that into a TypeError with nothing
        // in it about why; defining them means the page is TOLD, and gets the
        // INVALID_OPERATION a driver refusing an operation would give it.
        //
        // Babylon.js calls every one of these, gating 52 sites on
        // `_webGLVersion` so it degrades rather than refusing. It is the page
        // that will find out, and the plan's own words are that it should be
        // told rather than left to guess.
        for (const char * name : {"texImage3D",
                                  "texSubImage3D",
                                  "texStorage2D",
                                  "texStorage3D",
                                  "copyTexSubImage3D",
                                  "compressedTexImage3D",
                                  "drawBuffers",
                                  "clearBufferfv",
                                  "clearBufferiv",
                                  "clearBufferuiv",
                                  "clearBufferfi",
                                  "createSampler",
                                  "deleteSampler",
                                  "bindSampler",
                                  "samplerParameteri",
                                  "samplerParameterf",
                                  "isSampler",
                                  "createQuery",
                                  "deleteQuery",
                                  "beginQuery",
                                  "endQuery",
                                  "getQueryParameter",
                                  "isQuery",
                                  "fenceSync",
                                  "deleteSync",
                                  "clientWaitSync",
                                  "waitSync",
                                  "getSyncParameter",
                                  "isSync",
                                  "createTransformFeedback",
                                  "deleteTransformFeedback",
                                  "bindTransformFeedback",
                                  "beginTransformFeedback",
                                  "endTransformFeedback",
                                  "transformFeedbackVaryings",
                                  "getTransformFeedbackVarying",
                                  "pauseTransformFeedback",
                                  "resumeTransformFeedback",
                                  "isTransformFeedback",
                                  "bindBufferRange",
                                  "getActiveUniformBlockParameter",
                                  "getActiveUniformBlockName",
                                  "getActiveUniforms",
                                  "getUniformIndices",
                                  "renderbufferStorageMultisample",
                                  "blitFramebuffer",
                                  "invalidateFramebuffer",
                                  "readBuffer",
                                  "getInternalformatParameter",
                                  "getBufferSubData",
                                  "copyBufferSubData",
                                  "getFragDataLocation",
                                  "vertexAttribI4i",
                                  "vertexAttribI4ui",
                                  "vertexAttribIPointer"}) {
            method(name, [this, gl, name](context &, std::span<value>) {
                const std::size_t before = gl->refused().size();
                gl->refuse(name);
                // ONCE EACH, into the page's own console - which is where a
                // developer looks and what `refuse` deduplicates for. A call
                // refused every frame would otherwise be the only thing in it.
                if (gl->refused().size() != before) {
                    console_.push_back(std::string{"WebGL: gl."} + name +
                                       " is not implemented by this engine "
                                       "(see docs/webgl2-plan.md); it raises INVALID_OPERATION");
                }
                return value::null();
            });
        }
    }

    method("drawArrays", touches([gl](context &, std::span<value> a) {
               (void)gl->draw_arrays(enum_at(a, 0), int_at(a, 1), int_at(a, 2));
               return value::undefined();
           }));
    method("drawElements", touches([gl](context &, std::span<value> a) {
               (void)gl->draw_elements(enum_at(a, 0), int_at(a, 1), enum_at(a, 2), int_at(a, 3));
               return value::undefined();
           }));
    // THE NO-OP STUBS FOR drawArraysInstanced/drawElementsInstanced ARE GONE.
    // They were honest when instancing was out of scope - doing nothing beats
    // drawing one instance and being wrong by however many were asked for -
    // and they were registered AFTER the real methods above, so they silently
    // overwrote them the moment those arrived. A draw that reported no error
    // and painted nothing, which is exactly the shape both stubs existed to
    // avoid, arrived at from the other side.
    //
    // They are not replaced by a WebGL 1 stub either: in WebGL 1 these names do
    // not exist at all - a page reaches instancing through
    // ANGLE_instanced_arrays - and an absent method is what feature detection
    // reads.

    // --- reading back
    method("getError",
           [gl](context &, std::span<value>) { return value::number(gl->take_error()); });
    method("getParameter", [width, height, webgl2, gl](context & c, std::span<value> a) {
        switch (enum_at(a, 0)) {
        // WHICH VERTEX ARRAY IS BOUND. A page checks this to save and restore
        // the binding around its own work, and zero - the default array - has
        // to read as null rather than 0, because that is what a page tests.
        case 0x85B5:
            return gl->bound_vertex_array() == 0 ? value::null()
                                                 : value::number(gl->bound_vertex_array());
        // p5 READS BOTH OF THESE to decide what it is talking to, so they
        // have to say 2.0 / 3.00 on a WebGL 2 context rather than being
        // decoration.
        case gl_enum::version:
            return c.string(webgl2 ? "WebGL 2.0 (ctbrowser software)"
                                   : "WebGL 1.0 (ctbrowser software)");
        case gl_enum::shading_language_version:
            return c.string(webgl2 ? "WebGL GLSL ES 3.00" : "WebGL GLSL ES 1.0");
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
    // --- the extensions, which are the WebGL 1 spelling of WebGL 2 ----------
    //
    // ONE IMPLEMENTATION, TWO NAMES. `createVertexArrayOES` here and
    // `createVertexArray` on a WebGL 2 context call the SAME webgl_context
    // method, which is the decision docs/webgl2-plan.md records - and the
    // reason a corpus can check it. Phaser reaches VAOs and instancing ONLY
    // through these objects, so this is the machinery getting a workout from
    // somebody else's renderer rather than from a page written to test it.
    //
    // STILL NULL FOR EVERYTHING ELSE, which is what a driver without an
    // extension returns and what a page checks for. Handing back an object for
    // a name this cannot honour would make a page take a path that then fails
    // somewhere unrelated.
    method("getSupportedExtensions", [](context & c, std::span<value>) {
        const value list = c.make_array();
        auto * items = static_cast<script::array_object *>(list.as_heap());
        items->items.push_back(c.string("OES_vertex_array_object"));
        items->items.push_back(c.string("ANGLE_instanced_arrays"));
        // Derivatives are already implemented in the GLSL evaluator, so this
        // extension is a NAME over a capability that exists rather than new
        // work - checked in glsl_eval.cpp, not assumed.
        items->items.push_back(c.string("OES_standard_derivatives"));
        return list;
    });
    method("getExtension", [gl, touches](context & c, std::span<value> a) {
        const std::string name = a.empty() ? std::string{} : c.to_string(a[0]);
        const auto make = [&c](const char * label, auto && install) {
            auto * ext = static_cast<script::object_object *>(c.make_object().as_heap());
            const auto add = [&c, ext](std::string method_name, script::native_fn fn) {
                ext->set(method_name, value::object(c.allocate<script::native_object>(
                                          method_name, std::move(fn))));
            };
            install(add);
            (void)label;
            return value::object(ext);
        };
        if (name == "OES_vertex_array_object") {
            return make("vao", [gl](auto add) {
                add("createVertexArrayOES", [gl](context & inner, std::span<value>) {
                    return value::number(gl->create_vertex_array());
                    (void)inner;
                });
                add("bindVertexArrayOES", [gl](context & inner, std::span<value> args) {
                    gl->bind_vertex_array(args.empty() || !args[0].is_number()
                                              ? 0U
                                              : static_cast<std::uint32_t>(args[0].as_number()));
                    (void)inner;
                    return value::undefined();
                });
                add("deleteVertexArrayOES", [gl](context & inner, std::span<value> args) {
                    if (!args.empty() && args[0].is_number()) {
                        gl->delete_vertex_array(static_cast<std::uint32_t>(args[0].as_number()));
                    }
                    (void)inner;
                    return value::undefined();
                });
                add("isVertexArrayOES", [gl](context & inner, std::span<value> args) {
                    (void)inner;
                    return value::boolean(
                        !args.empty() && args[0].is_number() &&
                        gl->is_vertex_array(static_cast<std::uint32_t>(args[0].as_number())));
                });
            });
        }
        if (name == "ANGLE_instanced_arrays") {
            return make("angle", [gl, touches](auto add) {
                add("vertexAttribDivisorANGLE", [gl](context & inner, std::span<value> args) {
                    (void)inner;
                    gl->attribute_divisor(args.size() > 0 && args[0].is_number()
                                              ? static_cast<int>(args[0].as_number())
                                              : -1,
                                          args.size() > 1 && args[1].is_number()
                                              ? static_cast<int>(args[1].as_number())
                                              : 0);
                    return value::undefined();
                });
                add("drawArraysInstancedANGLE",
                    touches([gl](context & inner, std::span<value> args) {
                        const auto n = [&args](std::size_t i) {
                            return args.size() > i && args[i].is_number()
                                       ? static_cast<int>(args[i].as_number())
                                       : 0;
                        };
                        (void)gl->draw_arrays_instanced(
                            args.empty() || !args[0].is_number()
                                ? 0U
                                : static_cast<std::uint32_t>(args[0].as_number()),
                            n(1), n(2), n(3));
                        (void)inner;
                        return value::undefined();
                    }));
                add("drawElementsInstancedANGLE",
                    touches([gl](context & inner, std::span<value> args) {
                        const auto n = [&args](std::size_t i) {
                            return args.size() > i && args[i].is_number()
                                       ? static_cast<int>(args[i].as_number())
                                       : 0;
                        };
                        (void)gl->draw_elements_instanced(
                            args.empty() || !args[0].is_number()
                                ? 0U
                                : static_cast<std::uint32_t>(args[0].as_number()),
                            n(1),
                            args.size() > 2 && args[2].is_number()
                                ? static_cast<std::uint32_t>(args[2].as_number())
                                : 0U,
                            n(3), n(4));
                        (void)inner;
                        return value::undefined();
                    }));
            });
        }
        if (name == "OES_standard_derivatives") {
            // No methods: the extension only enables dFdx/dFdy/fwidth in the
            // shading language, and those already evaluate. An empty object is
            // the correct answer - a page checks that it is non-null.
            return make("derivatives", [](auto) {});
        }
        return value::null();
    });
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
    // THROUGH id_of, NOT as_number. A framebuffer arrives as a WRAPPED OBJECT
    // like every other GL object, so reading it as a number saw `undefined` and
    // bound ZERO every time - which meant every render target silently drew to
    // the canvas, and reading that target back gave an empty texture. It was
    // invisible while framebufferTexture2D was a no-op, and became an
    // INVALID_OPERATION the moment that stopped being one.
    method("bindFramebuffer", [gl](context & c, std::span<value> a) {
        gl->bind_framebuffer(id_of(c, a.size() > 1 ? a[1] : value::undefined()));
        return value::undefined();
    });
    // `framebufferTexture2D(target, attachment, textarget, texture, level)` -
    // WHAT MAKES A FRAMEBUFFER SOMEWHERE TO DRAW. It was a no-op, so every
    // render target was an empty texture and the scene went to the canvas
    // instead; see webgl_context::bind_framebuffer.
    method("framebufferTexture2D", [gl](context & c, std::span<value> a) {
        gl->framebuffer_texture(enum_at(a, 1), id_of(c, a.size() > 3 ? a[3] : value::undefined()));
        return value::undefined();
    });
    for (const char * name : {"bindRenderbuffer", "framebufferRenderbuffer", "renderbufferStorage",
                              "renderbufferStorageMultisample", "blitFramebuffer"}) {
        method(name, [](context &, std::span<value>) { return value::undefined(); });
    }
    // ASKED, NOT ASSUMED. This answered FRAMEBUFFER_COMPLETE unconditionally,
    // which is a lie a page acts on - Babylon checks it before deciding a
    // render target is usable, and would have been told yes about a target that
    // had nothing attached.
    method("checkFramebufferStatus",
           [gl](context &, std::span<value>) { return value::number(gl->framebuffer_status()); });

    return value::object(obj);
}

} // namespace ctbrowser::shell
