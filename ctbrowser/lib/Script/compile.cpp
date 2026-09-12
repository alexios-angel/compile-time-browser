// `compiler::compile()` - the whole public surface of this subsystem.
//
// The compiler itself is in compile/compiler_impl.hpp and the files beside it.
#include "compile/compiler_impl.hpp"
#include "compile/early_errors.hpp"

#include <ctbrowser/script/source_lines.hpp>

namespace ctbrowser::script {

namespace {

// A byte offset, as the `line:column` every diagnostic in this engine ends
// with. Shared by the parser's failure and the early errors' - they are the
// same kind of answer about the same source and there is no reason for them to
// be spelled differently.
[[nodiscard]] std::string position_in(std::string_view source, std::size_t offset) {
    const line_table lines{source};
    const auto at = static_cast<std::uint32_t>(std::min(offset, source.size()));
    return " - at " + std::to_string(lines.line_of(at)) + ":" + std::to_string(lines.column_of(at));
}

} // namespace

namespace {
program compile_source(std::string_view source, script_kind kind, bool completion_value);
} // namespace

program compiler::compile(std::string_view source, script_kind kind) {
    return compile_source(source, kind, /*completion_value*/ false);
}

program compiler::compile_for_eval(std::string_view source) {
    return compile_source(source, script_kind::classic, /*completion_value*/ true);
}

namespace {
program compile_source(std::string_view source, script_kind kind, bool completion_value) {
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
        out.error =
            "parse error: " + std::string{tree.error} + position_in(source, tree.error_offset);
        return out;
    }
    // THE EARLY ERRORS, BEFORE A SINGLE INSTRUCTION IS EMITTED. Clause 17 says
    // they are reported "prior to the first evaluation of the source text", and
    // the only way to promise that is to answer before the compiler starts.
    //
    // Reported with the parser's own prefix, and that is not cosmetic: an early
    // error IS a SyntaxError in the source text, indistinguishable to a caller
    // from one the grammar caught, and `parse error:` is the only thing this
    // engine says that means "this source is a SyntaxError". `ct262` reads
    // exactly that prefix to decide between phase `parse` and phase `refusal` -
    // a refusal being "the compiler does not implement this", which an early
    // error is emphatically not. See lib/Script/compile/early_errors.hpp.
    if (const auto early = detail::find_early_error(tree, source)) {
        out.ok = false;
        out.error = "parse error: " + early->message;
        if (early->offset != detail::early_error::nowhere) {
            out.error += position_in(source, early->offset);
        }
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
    c.completion_value_ = completion_value;
    c.compile_program();
    return out;
}
} // namespace

bool debug_names_enabled() noexcept {
#if CTBROWSER_SCRIPT_DEBUG_NAMES
    return true;
#else
    return false;
#endif
}

} // namespace ctbrowser::script
