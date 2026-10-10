//===- ctbrowser/lib/Shell/bindings/webgl/constants.cpp - WebGL constants -===//
//
// Installs the GL enum constants (gl.TRIANGLES and the rest) on a WebGL
// context object, including WebGL 2's additions on a webgl2 context.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

void dom_bindings::install_webgl_constants(script::object_object * obj, bool webgl2) {
    // Stored as plain data properties rather than getters, because a page reads
    // constants such as gl.TRIANGLES far more often than it calls methods. The
    // rows come from CTBROWSER_GL_CONSTANTS in shell/page/webgl.hpp, in its order;
    // a row with version 1 is installed on every context, version 2 only on
    // WebGL 2.
#define X(NAME, ident, v, ver)                                                                     \
    if (ver == 1 || webgl2) { obj->set(#NAME, value::number(gl_enum::ident)); }
    CTBROWSER_GL_CONSTANTS(X)
#undef X
}

} // namespace ctbrowser::shell
