// `compiler::compile()` - the whole public surface of this subsystem.
//
// The compiler itself is in compile/compiler_impl.hpp and the files beside it.
#include "compile/compiler_impl.hpp"

namespace ctbrowser::script {

program compiler::compile(std::string_view source, script_kind kind) {
    const vp::ast tree = vp::parse(source);
    program out;
    out.kind = kind;
    if (!tree.ok) {
        out.ok = false;
        // WITH THE POSITION, which was thrown away here for as long as this
        // function has existed. `vp::ast` carries `error_offset` precisely so a
        // caller holding the source can say where the parser stopped - the
        // comment on that field says so - and dropping it left every parse
        // failure reading "parse error: expression" about a bundle several
        // megabytes long. That is half a diagnostic, and it is the half that
        // costs the afternoon.
        //
        // Resolved to a line and column here rather than by every caller,
        // because the source is right there and none of them have it in a
        // convenient form. The shape matches what the ratchets' tooling already
        // greps for - `<name>:<line>:<column>`.
        std::size_t line = 1;
        std::size_t column = 1;
        const std::size_t stopped = std::min(tree.error_offset, source.size());
        for (std::size_t i = 0; i < stopped; ++i) {
            if (source[i] == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }
        out.error = "parse error: " + std::string{tree.error} + " - at " + std::to_string(line) +
                    ":" + std::to_string(column);
        return out;
    }
    out.source = std::string{source};
    detail::compiler_impl c{tree, out};
    c.module_scope_ = kind == script_kind::module_;
    // THE CALLER'S BYTES, not `out.source`. Every `node::text` is a view into
    // the buffer the PARSER saw, and `out.source` is a copy at a different
    // address - so the copy is the wrong thing to subtract against. See
    // compiler_impl::offset_of.
    c.source_view_ = source;
    c.compile_program();
    return out;
}

bool debug_names_enabled() noexcept {
#if CTBROWSER_SCRIPT_DEBUG_NAMES
    return true;
#else
    return false;
#endif
}

} // namespace ctbrowser::script
