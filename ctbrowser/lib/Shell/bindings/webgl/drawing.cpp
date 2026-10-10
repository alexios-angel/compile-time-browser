//===- drawing.cpp - WebGL drawing and queries ------------------*- C++ -*-===//
//
// The WebGL methods that draw and read back: getShaderPrecisionFormat,
// getVertexAttrib, the WebGL 2 vertex-array and instancing methods,
// drawArrays/drawElements, getParameter, the extensions, readPixels and the
// framebuffer and renderbuffer family.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

void dom_bindings::install_webgl_draw_methods(context & cx, script::object_object * obj,
                                              webgl_context * gl, canvas_context * surface,
                                              int width, int height, bool webgl2) {
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

    // Makes the JS handle for a new GL object: `__id` is its GL name and
    // `__kind` names its object class.
    const auto handle = [](context & c, std::uint32_t made_id, const char * kind) {
        auto * out = c.allocate<script::object_object>();
        out->set("__id", value::number(made_id));
        out->set("__kind", c.string(kind));
        return value::object(out);
    };

    // getShaderPrecisionFormat: Babylon.js reads it during capability detection
    // and sets `highPrecisionShaderSupported` from the answer; without it,
    // Babylon.js assumes no high-precision floats and picks its low-precision
    // shader paths. The answer describes IEEE single precision for every
    // precision qualifier (rangeMin/rangeMax 127, precision 23; integers
    // 31/31/0). The values are fixed rather than queried from the GL backend.
    set_method(cx, *obj, "getShaderPrecisionFormat", [](context & c, std::span<value> a) {
        const std::uint32_t kind = enum_at(a, 1);
        value out = c.make_object();
        auto * o = static_cast<script::object_object *>(out.as_heap());
        // GL's LOW_FLOAT..HIGH_FLOAT are 0x8DF0..0x8DF2 and LOW_INT..HIGH_INT
        // are 0x8DF3..0x8DF5, so this range answers LOW_INT as a float format.
        const bool integer = kind >= 0x8DF4 && kind <= 0x8DF6;
        o->set("rangeMin", value::number(integer ? 31 : 127));
        o->set("rangeMax", value::number(integer ? 31 : 127));
        o->set("precision", value::number(integer ? 0 : 23));
        return out;
    });
    // getVertexAttrib is how a page checks that a vertex array object captured
    // its attribute state; every other VAO call returns nothing.
    set_method(cx, *obj, "getVertexAttrib", [gl](context &, std::span<value> a) {
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

    // WebGL 2 names for vertex array objects and instancing. They call the same
    // webgl_context methods as the OES_vertex_array_object and
    // ANGLE_instanced_arrays extension objects below (see docs/history/webgl2.md).
    if (webgl2) {
        set_method(cx, *obj, "createVertexArray", [gl](context &, std::span<value>) {
            return value::number(gl->device().create_vertex_array());
        });
        set_method(cx, *obj, "bindVertexArray", [gl](context &, std::span<value> a) {
            gl->device().bind_vertex_array(
                a.empty() || !a[0].is_number() ? 0U : to_index(a[0].as_number()));
            return value::undefined();
        });
        set_method(cx, *obj, "deleteVertexArray", [gl](context &, std::span<value> a) {
            if (!a.empty() && a[0].is_number()) {
                gl->device().delete_vertex_array(to_index(a[0].as_number()));
            }
            return value::undefined();
        });
        set_method(cx, *obj, "isVertexArray", [gl](context &, std::span<value> a) {
            return value::boolean(!a.empty() && a[0].is_number() &&
                                  gl->device().is_vertex_array(to_index(a[0].as_number())));
        });
        set_method(cx, *obj, "vertexAttribDivisor", [gl](context &, std::span<value> a) {
            gl->attribute_divisor(!a.empty() && a[0].is_number() ? to_int(a[0].as_number()) : -1,
                                  a.size() > 1 && a[1].is_number() ? to_int(a[1].as_number()) : 0);
            return value::undefined();
        });
        set_method(cx, *obj, "drawArraysInstanced", touches([gl](context &, std::span<value> a) {
                       (void)gl->draw_arrays_instanced(enum_at(a, 0), int_at(a, 1), int_at(a, 2),
                                                       int_at(a, 3));
                       return value::undefined();
                   }));
        set_method(cx, *obj, "drawElementsInstanced", touches([gl](context &, std::span<value> a) {
                       (void)gl->draw_elements_instanced(enum_at(a, 0), int_at(a, 1), enum_at(a, 2),
                                                         int_at(a, 3), int_at(a, 4));
                       return value::undefined();
                   }));

        // WebGL 2 methods this engine does not implement. They are defined anyway so
        // that a page calling them gets INVALID_OPERATION and one console message,
        // not a TypeError. Pages do not feature-detect these on a webgl2 context;
        // Babylon.js, for example, calls them directly.
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
            set_method(cx, *obj, name, [this, gl, name](context &, std::span<value>) {
                const std::size_t before = gl->refused().size();
                gl->refuse(name);
                // Logged once per name to the page's console, where a developer
                // looks; `refuse` deduplicates, so a call refused every frame does
                // not flood it.
                if (gl->refused().size() != before) {
                    console_.push_back(std::string{"WebGL: gl."} + name +
                                       " is not implemented by this engine "
                                       "(see docs/history/webgl2.md); it raises INVALID_OPERATION");
                }
                return value::null();
            });
        }
    }

    set_method(cx, *obj, "drawArrays", touches([gl](context &, std::span<value> a) {
                   (void)gl->draw_arrays(enum_at(a, 0), int_at(a, 1), int_at(a, 2));
                   return value::undefined();
               }));
    set_method(cx, *obj, "drawElements", touches([gl](context &, std::span<value> a) {
                   (void)gl->draw_elements(enum_at(a, 0), int_at(a, 1), enum_at(a, 2),
                                           int_at(a, 3));
                   return value::undefined();
               }));
    // drawArraysInstanced/drawElementsInstanced exist only on WebGL 2 (above).
    // WebGL 1 pages reach instancing through ANGLE_instanced_arrays, and feature
    // detection depends on the methods being absent here. Do not register a stub
    // under these names after the WebGL 2 block: it would overwrite the real
    // methods.

    // --- reading back
    set_method(cx, *obj, "getError",
               [gl](context &, std::span<value>) { return value::number(gl->take_error()); });
    set_method(cx, *obj, "getParameter",
               [width, height, webgl2, gl](context & c, std::span<value> a) {
                   switch (enum_at(a, 0)) {
                   // 0x85B5 is VERTEX_ARRAY_BINDING. A page reads it to save and restore
                   // the binding around its own work; the default array (0) must read as
                   // null, because that is what a page tests for.
                   case 0x85B5:
                       return gl->device().bound_vertex_array() == 0
                                  ? value::null()
                                  : value::number(gl->device().bound_vertex_array());
                   // p5.js reads both of these to decide which WebGL version it has, so
                   // they must say 2.0 / 3.00 on a WebGL 2 context.
                   case gl_enum::version:
                       return c.string(webgl2 ? "WebGL 2.0 (ctbrowser)" : "WebGL 1.0 (ctbrowser)");
                   case gl_enum::shading_language_version:
                       return c.string(webgl2 ? "WebGL GLSL ES 3.00" : "WebGL GLSL ES 1.0");
                   case gl_enum::vendor: return c.string("ctbrowser");
                   // RENDERER reports the real backend (for example SwiftShader or a GPU
                   // name) because pages print it in bug reports.
                   case gl_enum::renderer: return c.string(gl->device().renderer());
                   case 0x0D3A: { // MAX_VIEWPORT_DIMS
                       const value out = c.make_array();
                       auto * items = static_cast<script::array_object *>(out.as_heap());
                       items->items.push_back(value::number(width));
                       items->items.push_back(value::number(height));
                       return out;
                   }
                   default: break;
                   }

                   // Integer limits are queried from GL. Only the enums listed are forwarded:
                   // glGetIntegerv on a boolean, float or unknown enum is wrong or raises
                   // INVALID_ENUM. Any other enum returns null, as WebGL does for unrecognised
                   // parameters. Answering 0 would mislead pages that size buffers or pick
                   // shader variants from these limits.
                   static constexpr std::uint32_t integer_parameters[] = {
                       0x0D33, // MAX_TEXTURE_SIZE
                       0x8869, // MAX_VERTEX_ATTRIBS
                       0x8872, // MAX_TEXTURE_IMAGE_UNITS
                       0x8B4C, // MAX_VERTEX_TEXTURE_IMAGE_UNITS
                       0x8B4D, // MAX_COMBINED_TEXTURE_IMAGE_UNITS
                       0x8DFB, // MAX_VERTEX_UNIFORM_VECTORS
                       0x8DFC, // MAX_VARYING_VECTORS
                       0x8DFD, // MAX_FRAGMENT_UNIFORM_VECTORS
                       0x851C, // MAX_CUBE_MAP_TEXTURE_SIZE
                       0x84E8, // MAX_RENDERBUFFER_SIZE
                       0x8073, // MAX_3D_TEXTURE_SIZE
                       0x88FF, // MAX_ARRAY_TEXTURE_LAYERS
                       0x8824, // MAX_DRAW_BUFFERS
                       0x8D57, // MAX_SAMPLES
                       0x8A2F, // MAX_UNIFORM_BUFFER_BINDINGS
                       0x8A30, // MAX_UNIFORM_BLOCK_SIZE
                       0x8A34, // UNIFORM_BUFFER_OFFSET_ALIGNMENT
                       0x8B49, // MAX_FRAGMENT_UNIFORM_COMPONENTS
                       0x8B4A, // MAX_VERTEX_UNIFORM_COMPONENTS
                       0x8CDF, // MAX_COLOR_ATTACHMENTS
                       0x84E2, // MAX_TEXTURE_UNITS
                   };
                   const std::uint32_t asked = enum_at(a, 0);
                   for (const std::uint32_t known : integer_parameters) {
                       if (known == asked) {
                           return value::number(gl->device().limit(static_cast<int>(asked)));
                       }
                   }
                   return value::null();
               });
    // --- Extensions: the WebGL 1 spelling of WebGL 2 features ---------------
    //
    // The extension objects call the same webgl_context methods as the WebGL 2
    // names (createVertexArrayOES and createVertexArray share one implementation),
    // so the Phaser corpus, which uses only the extensions, also tests WebGL 2.
    // Unsupported extensions return null, as a driver without them does; returning
    // an object would send pages down a path that fails later.
    set_method(cx, *obj, "getSupportedExtensions", [](context & c, std::span<value>) {
        const value list = c.make_array();
        auto * items = static_cast<script::array_object *>(list.as_heap());
        items->items.push_back(c.string("OES_vertex_array_object"));
        items->items.push_back(c.string("ANGLE_instanced_arrays"));
        // dFdx/dFdy/fwidth are provided by the GL backend (ANGLE), so advertising
        // OES_standard_derivatives promises nothing extra.
        items->items.push_back(c.string("OES_standard_derivatives"));
        return list;
    });
    set_method(cx, *obj, "getExtension", [gl, touches](context & c, std::span<value> a) {
        const std::string name = a.empty() ? std::string{} : c.to_string(a[0]);
        const auto make = [&c](const char * label, auto && install) {
            auto * ext = c.allocate<script::object_object>();
            const auto add = [&c, ext](const std::string & method_name, script::native_fn fn) {
                set_method(c, *ext, method_name, std::move(fn));
            };
            install(add);
            (void)label;
            return value::object(ext);
        };
        if (name == "OES_vertex_array_object") {
            return make("vao", [gl](auto add) {
                add("createVertexArrayOES", [gl](context & inner, std::span<value>) {
                    return value::number(gl->device().create_vertex_array());
                    (void)inner;
                });
                add("bindVertexArrayOES", [gl](context & inner, std::span<value> args) {
                    gl->device().bind_vertex_array(
                        args.empty() || !args[0].is_number() ? 0U : to_index(args[0].as_number()));
                    (void)inner;
                    return value::undefined();
                });
                add("deleteVertexArrayOES", [gl](context & inner, std::span<value> args) {
                    if (!args.empty() && args[0].is_number()) {
                        gl->device().delete_vertex_array(to_index(args[0].as_number()));
                    }
                    (void)inner;
                    return value::undefined();
                });
                add("isVertexArrayOES", [gl](context & inner, std::span<value> args) {
                    (void)inner;
                    return value::boolean(
                        !args.empty() && args[0].is_number() &&
                        gl->device().is_vertex_array(to_index(args[0].as_number())));
                });
            });
        }
        if (name == "ANGLE_instanced_arrays") {
            return make("angle", [gl, touches](auto add) {
                add("vertexAttribDivisorANGLE", [gl](context & inner, std::span<value> args) {
                    (void)inner;
                    gl->attribute_divisor(
                        args.size() > 0 && args[0].is_number() ? to_int(args[0].as_number()) : -1,
                        args.size() > 1 && args[1].is_number() ? to_int(args[1].as_number()) : 0);
                    return value::undefined();
                });
                add("drawArraysInstancedANGLE",
                    touches([gl](context & inner, std::span<value> args) {
                        const auto n = [&args](std::size_t i) {
                            return args.size() > i && args[i].is_number()
                                       ? to_int(args[i].as_number())
                                       : 0;
                        };
                        (void)gl->draw_arrays_instanced(args.empty() || !args[0].is_number()
                                                            ? 0U
                                                            : to_index(args[0].as_number()),
                                                        n(1), n(2), n(3));
                        (void)inner;
                        return value::undefined();
                    }));
                add("drawElementsInstancedANGLE",
                    touches([gl](context & inner, std::span<value> args) {
                        const auto n = [&args](std::size_t i) {
                            return args.size() > i && args[i].is_number()
                                       ? to_int(args[i].as_number())
                                       : 0;
                        };
                        (void)gl->draw_elements_instanced(
                            args.empty() || !args[0].is_number() ? 0U
                                                                 : to_index(args[0].as_number()),
                            n(1),
                            args.size() > 2 && args[2].is_number() ? to_index(args[2].as_number())
                                                                   : 0U,
                            n(3), n(4));
                        (void)inner;
                        return value::undefined();
                    }));
            });
        }
        if (name == "OES_standard_derivatives") {
            // No methods: the extension only enables dFdx/dFdy/fwidth in shaders,
            // which the backend supports. A page only checks that the object is
            // non-null.
            return make("derivatives", [](auto) {});
        }
        return value::null();
    });
    set_method(cx, *obj, "isContextLost",
               [](context &, std::span<value>) { return value::boolean(false); });
    set_method(cx, *obj, "readPixels", [gl](context & c, std::span<value> a) {
        // Writes into the caller's typed array (argument 6). A Uint8Array is an
        // array_object with an element kind; only an ArrayBuffer stores its data
        // in `__bytes`. Accept both.
        if (a.size() < 7) { return value::undefined(); }
        value into = a[6];
        if (into.is_object()) {
            const value held = c.lookup_property(into, "__bytes");
            if (held.is_array()) { into = held; }
        }
        if (!into.is_array()) { return value::undefined(); }
        auto * out = static_cast<script::array_object *>(into.as_heap());
        // Present first so the canvas holds this frame's pixels: readPixels reads
        // the framebuffer as it is now, and a page calls it mid-frame, before the
        // end-of-frame present(). Without this it would read the previous frame.
        gl->present();
        const paint::bitmap * from = gl->surface();
        if (from == nullptr) { return value::undefined(); }
        const int x = int_at(a, 0);
        const int y = int_at(a, 1);
        const int w = int_at(a, 2);
        const int h = int_at(a, 3);
        std::size_t at = 0;
        for (int row = 0; row < h; ++row) {
            for (int column = 0; column < w; ++column) {
                // GL reads bottom-up: row 0 of the result is the bottom row of
                // the surface, the same origin flip the viewport has.
                const int sy = gl->device().height() - 1 - (y + row);
                const std::uint32_t texel =
                    (sy < 0 || sy >= from->height) ? 0 : from->at(x + column, sy);
                for (const std::uint32_t shift : {16u, 8u, 0u, 24u}) { // ARGB texel -> RGBA
                    if (at < out->items.size()) {
                        out->items[at] = value::number((texel >> shift) & 0xFF);
                    }
                    ++at;
                }
            }
        }
        return value::undefined();
    });

    // Framebuffers and renderbuffers get their names from their own GL
    // namespaces (not glGenBuffers), so their ids never collide with buffer ids.
    set_method(cx, *obj, "createFramebuffer", [gl, handle](context & c, std::span<value>) {
        return handle(c, gl->device().create_framebuffer(), "framebuffer");
    });
    set_method(cx, *obj, "createRenderbuffer", [gl, handle](context & c, std::span<value>) {
        return handle(c, gl->device().create_renderbuffer(), "renderbuffer");
    });
    // A WebGLFramebuffer arrives as a wrapped object, so read its id with id_of;
    // as_number would give 0 and bind the default framebuffer (the canvas).
    set_method(cx, *obj, "bindFramebuffer", [gl](context & c, std::span<value> a) {
        gl->device().bind_framebuffer(id_of(c, a.size() > 1 ? a[1] : value::undefined()));
        return value::undefined();
    });
    // `framebufferTexture2D(target, attachment, textarget, texture, level)`
    // attaches a texture to the bound framebuffer; see
    // webgl_context::bind_framebuffer.
    set_method(cx, *obj, "framebufferTexture2D", [gl](context & c, std::span<value> a) {
        gl->device().framebuffer_texture(int_enum_at(a, 1),
                                         id_of(c, a.size() > 3 ? a[3] : value::undefined()));
        return value::undefined();
    });
    // Renderbuffers give offscreen render targets a depth attachment (Babylon.js
    // post-processes need one, or every draw passes the depth test).
    set_method(cx, *obj, "bindRenderbuffer", [gl](context & c, std::span<value> a) {
        gl->device().bind_renderbuffer(id_of(c, a.size() > 1 ? a[1] : value::undefined()));
        return value::undefined();
    });
    set_method(cx, *obj, "renderbufferStorage", [gl](context &, std::span<value> a) {
        gl->device().renderbuffer_storage(int_enum_at(a, 1), int_at(a, 2), int_at(a, 3));
        return value::undefined();
    });
    set_method(cx, *obj, "framebufferRenderbuffer", [gl](context & c, std::span<value> a) {
        gl->device().framebuffer_renderbuffer(int_enum_at(a, 1),
                                              id_of(c, a.size() > 3 ? a[3] : value::undefined()));
        return value::undefined();
    });
    // Not implemented: multisample storage needs a resolve step and
    // blitFramebuffer needs separate read/draw framebuffer bindings, neither of
    // which is tracked. Registered after the WebGL 2 refusal loop above, so on a
    // WebGL 2 context these replace that version: they return undefined and do
    // not log to the page console.
    for (const char * name : {"renderbufferStorageMultisample", "blitFramebuffer"}) {
        set_method(cx, *obj, name, [gl, name](context &, std::span<value>) {
            gl->refuse(name);
            return value::undefined();
        });
    }
    // The real status from GL. Pages (Babylon.js) check it before using a
    // render target, so a target with nothing attached must not report complete.
    set_method(cx, *obj, "checkFramebufferStatus", [gl](context &, std::span<value>) {
        return value::number(gl->device().framebuffer_status());
    });
}

} // namespace ctbrowser::shell
