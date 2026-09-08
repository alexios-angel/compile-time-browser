// dom_bindings - the WebGL methods that draw and read back: getVertexAttrib,
// the WebGL 2 surface, drawArrays/drawElements, getParameter, the
// extensions, readPixels and the framebuffer family.
//
// One of four files carved out of a 1,431-line bindings/webgl.cpp on
// 2026-09-08. webgl_context_object was ONE 1,213-line function; it is split
// at three seams that share no local into private member functions, called
// in the order the original installed things. The argument helpers every
// file needs are in internal.hpp beside this, in ctbrowser::shell::detail.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

void dom_bindings::install_webgl_draw_methods(context & cx, script::object_object * obj,
                                              webgl_context * gl, canvas_context * surface,
                                              int width, int height, bool webgl2) {
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

    const auto handle = [](context & c, std::uint32_t made_id, const char * kind) {
        auto * out = static_cast<script::object_object *>(c.make_object().as_heap());
        out->set("__id", value::number(made_id));
        out->set("__kind", c.string(kind));
        return value::object(out);
    };

    // --- drawing
    // --- WebGL 2's spelling of what the extensions already expose -----------
    // THE SAME webgl_context METHODS the OES/ANGLE objects call. That is the
    // decision docs/history/webgl2.md records, and it is why this arrives already
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

        // STAGE 5 of docs/history/webgl2.md: THE HALF THIS ENGINE DOES NOT
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
                                       "(see docs/history/webgl2.md); it raises INVALID_OPERATION");
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
            return c.string(webgl2 ? "WebGL 2.0 (ctbrowser)" : "WebGL 1.0 (ctbrowser)");
        case gl_enum::shading_language_version:
            return c.string(webgl2 ? "WebGL GLSL ES 3.00" : "WebGL GLSL ES 1.0");
        case gl_enum::vendor: return c.string("ctbrowser");
        // WHAT IS ACTUALLY DRAWING. This said "ctbrowser software rasteriser",
        // which stopped being true when the software rasteriser was deleted -
        // and RENDERER is the string a page prints into a bug report, so a
        // wrong one costs somebody else the afternoon. SwiftShader and an Intel
        // Arc are the same code path and very different numbers.
        case gl_enum::renderer: return c.string(gl->renderer());
        case 0x0D3A: { // MAX_VIEWPORT_DIMS
            const value out = c.make_array();
            auto * items = static_cast<script::array_object *>(out.as_heap());
            items->items.push_back(value::number(width));
            items->items.push_back(value::number(height));
            return out;
        }
        default: break;
        }

        // ASKED OF GL, from a list of the enums that ARE plain integers.
        //
        // This was a hardcoded table ending in `default: return 0`, so ten caps
        // Babylon reads - MAX_VARYING_VECTORS, MAX_DRAW_BUFFERS, MAX_SAMPLES,
        // MAX_COMBINED_TEXTURE_IMAGE_UNITS and the rest - came back ZERO. A
        // page sizes buffers and picks shader permutations from those numbers,
        // and zero is a plausible-looking answer rather than a missing one:
        // Babylon's PBR path disables a feature when maxVaryingVectors <= 8,
        // and it was reading 0.
        //
        // A LIST rather than forwarding anything: most GL parameters are not
        // integers, and glGetIntegerv on a boolean, a float or an unknown enum
        // is either wrong or an INVALID_ENUM this layer would then have to
        // swallow out of the page's error queue. An enum that is not here
        // answers null, which is what WebGL says for one it does not recognise.
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
            if (known == asked) { return value::number(gl->limit(asked)); }
        }
        return value::null();
    });
    // --- the extensions, which are the WebGL 1 spelling of WebGL 2 ----------
    //
    // ONE IMPLEMENTATION, TWO NAMES. `createVertexArrayOES` here and
    // `createVertexArray` on a WebGL 2 context call the SAME webgl_context
    // method, which is the decision docs/history/webgl2.md records - and the
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
        // FRESH PIXELS, NOT LAST FRAME'S. readPixels reads the framebuffer AS IT
        // IS NOW, and a page calls it in the middle of the frame it just drew -
        // before the end-of-frame present(). Reading the canvas bitmap without
        // this returns whatever the previous frame left, which for the first
        // frame is nothing at all and looks exactly like a draw that missed.
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

    // FROM THEIR OWN NAMESPACES. Both of these called `create_buffer` -
    // glGenBuffers - so a "framebuffer" was a buffer name and the two classes
    // handed out colliding integers. It survived only because ANGLE's default
    // bindGeneratesResource lets glBindFramebuffer invent the object; what it
    // cost was a name burned out of the buffer namespace for every render
    // target, widening the collision the kind-less delete_object was deleting
    // through.
    method("createFramebuffer", [gl, handle](context & c, std::span<value>) {
        return handle(c, gl->create_framebuffer(), "framebuffer");
    });
    method("createRenderbuffer", [gl, handle](context & c, std::span<value>) {
        return handle(c, gl->create_renderbuffer(), "renderbuffer");
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
    // A RENDER TARGET USUALLY WANTS DEPTH, and these three were no-ops - so an
    // offscreen target got colour and nothing else, and every draw into it
    // passed the depth test in arrival order. Babylon's post-processes render
    // the scene into exactly such a target.
    method("bindRenderbuffer", [gl](context & c, std::span<value> a) {
        gl->bind_renderbuffer(id_of(c, a.size() > 1 ? a[1] : value::undefined()));
        return value::undefined();
    });
    method("renderbufferStorage", [gl](context &, std::span<value> a) {
        gl->renderbuffer_storage(enum_at(a, 1), int_at(a, 2), int_at(a, 3));
        return value::undefined();
    });
    method("framebufferRenderbuffer", [gl](context & c, std::span<value> a) {
        gl->framebuffer_renderbuffer(enum_at(a, 1),
                                     id_of(c, a.size() > 3 ? a[3] : value::undefined()));
        return value::undefined();
    });
    // STILL NOT IMPLEMENTED, and now they say so rather than returning quietly.
    // Multisample storage needs a resolve this engine has nowhere to put, and
    // blitFramebuffer needs two bound framebuffers it does not track.
    for (const char * name : {"renderbufferStorageMultisample", "blitFramebuffer"}) {
        method(name, [gl, name](context &, std::span<value>) {
            gl->refuse(name);
            return value::undefined();
        });
    }
    // ASKED, NOT ASSUMED. This answered FRAMEBUFFER_COMPLETE unconditionally,
    // which is a lie a page acts on - Babylon checks it before deciding a
    // render target is usable, and would have been told yes about a target that
    // had nothing attached.
    method("checkFramebufferStatus",
           [gl](context &, std::span<value>) { return value::number(gl->framebuffer_status()); });
}

} // namespace ctbrowser::shell
