// dom_bindings - the constant table of a WebGL context object, WebGL 2's
// additions included.

#include "internal.hpp"

namespace ctbrowser::shell {

using namespace detail;

void dom_bindings::install_webgl_constants(script::object_object * obj, bool webgl2) {
    // Set as properties rather than resolved per call: a page reads gl.TRIANGLES
    // far more often than it calls anything, and a lookup is a lookup. The rows
    // are page/webgl.hpp's, in its order.
#define X(NAME, ident, v, ver)                                                                     \
    if (ver == 1 || webgl2) { obj->set(#NAME, value::number(gl_enum::ident)); }
    CTBROWSER_GL_CONSTANTS(X)
#undef X
}

} // namespace ctbrowser::shell
