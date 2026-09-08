// dom_bindings - the constant table of a WebGL context object, WebGL 2's
// additions included.
//
// One of four files carved out of a 1,431-line bindings/webgl.cpp on
// 2026-09-08. webgl_context_object was ONE 1,213-line function; it is split
// at three seams that share no local into private member functions, called
// in the order the original installed things. The argument helpers every
// file needs are in internal.hpp beside this, in ctbrowser::shell::detail.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

void dom_bindings::install_webgl_constants(script::object_object * obj, bool webgl2) {
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
        // A CONSTANT THAT IS A FEATURE TEST, and the most expensive omission
        // this table has had.
        //
        // Babylon picks its GLSL dialect from whether this property EXISTS:
        //
        //     _webGLVersion: e.TEXTURE_BINDING_3D ? 2 : 1
        //     const c = l._webGLVersion > 1 ? "#version 300 es\n#define WEBGL2 \n" : ""
        //
        // Undefined, so Babylon concluded WebGL 1 and prepended NO `#version`
        // to anything - while its shader PROCESSOR, which reads the engine's
        // own version and correctly saw 2, rewrote every body to ES 3.00.
        // Every shader was then ES 3.00 source with no directive. The fragment
        // ones survived because the processor injects `layout(location = 0)`
        // and shell/page/webgl.cpp's repair keys on `layout(`; the post-process
        // VERTEX shader has none, so it was parsed as ESSL 1.00 and rejected,
        // the fullscreen quad never drew, and the canvas kept its clear colour
        // with NO GL ERROR ANYWHERE. That is Babylon ratchet rung 8.
        //
        // It sits inside the webgl2 block for the reason the block exists: it
        // is precisely the constant a page uses to conclude it has WebGL 2,
        // and on a WebGL 1 context that conclusion would be wrong.
        constant("TEXTURE_BINDING_3D", 0x806A);
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
}

} // namespace ctbrowser::shell
