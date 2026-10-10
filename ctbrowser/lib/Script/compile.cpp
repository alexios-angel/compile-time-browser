//===- ctbrowser/lib/Script/compile.cpp - compiler::compile entry ---------===//
//
// `compiler::compile()` and its siblings: the whole public surface of the
// script compiler. Each parses the source, runs the early errors, and hands
// the tree to compiler_impl, which is in compile/compiler_impl.hpp and the
// files beside it.
//
//===----------------------------------------------------------------------===//
#include "compile/compiler_impl.hpp"
#include "compile/early_errors.hpp"

#include <ctbrowser/script/source_lines.hpp>

namespace ctbrowser::script {

namespace {

/// Formats byte `offset` in `source` as the " - at line:column" suffix every
/// diagnostic in this engine ends with, used by both parse failures and early
/// errors. Also stores the numbers on `out` (program::error_line and
/// error_column) for an embedder that reports the SyntaxError with a position
/// of its own, such as an ErrorEvent's lineno and colno.
[[nodiscard]] std::string position_in(std::string_view source, std::size_t offset, program & out) {
    const line_table lines{source};
    const auto at = static_cast<std::uint32_t>(std::min(offset, source.size()));
    out.error_line = lines.line_of(at);
    out.error_column = lines.column_of(at);
    return " - at " + std::to_string(out.error_line) + ":" + std::to_string(out.error_column);
}

} // namespace

namespace {
/// Parses, checks and compiles `source`. `site` is null for a program an
/// embedder runs and the eval call site otherwise; `outer` lists the bindings a
/// direct eval can see; `script_goal` selects the classic Script goal for a
/// top-level program, which disables top-level await.
program compile_source(std::string_view source, script_kind kind, const eval_site * site,
                       std::span<const eval_binding> outer = {}, bool script_goal = false);
} // namespace

program compiler::compile(std::string_view source, script_kind kind) {
    return compile_source(source, kind, nullptr);
}

program compiler::compile_script(std::string_view source) {
    return compile_source(source, script_kind::classic, nullptr, {}, true);
}

program compiler::compile_for_eval(std::string_view source, const eval_site & site) {
    return compile_source(source, script_kind::classic, &site);
}

program compiler::compile_for_direct_eval(std::string_view source,
                                          std::span<const eval_binding> outer,
                                          const eval_site & site) {
    return compile_source(source, script_kind::classic, &site, outer);
}

namespace {
program compile_source(std::string_view source, script_kind kind, const eval_site * site,
                       std::span<const eval_binding> outer, bool script_goal) {
    // Source text is text: a stray 0xFF in a file handed over as bytes is the
    // U+FFFD decoding makes of it, before a string literal or a regular
    // expression can carry it into a constant and spell an internal key
    // (core/symbol.hpp). Everything below reads the decoded copy.
    std::string decoded;
    if (source.find(internal_key_lead) != std::string_view::npos) [[unlikely]] {
        decoded = std::string{source};
        replace_internal_key_lead(decoded);
        source = decoded;
    }
    const bool top_level_await = kind == script_kind::module_ || (site == nullptr && !script_goal);
    const vp::ast tree = vp::parse(source, top_level_await);
    program out;
    out.kind = kind;
    if (!tree.ok) {
        out.ok = false;
        // Report where the parser stopped (vp::ast::error_offset) as
        // `line:column`, resolved here because the source is at hand and
        // callers do not have it in a convenient form. Ratchet tooling greps
        // for `<name>:<line>:<column>`.
        out.error =
            "parse error: " + std::string{tree.error} + position_in(source, tree.error_offset, out);
        return out;
    }
    // Early errors are checked before a single instruction is emitted: clause
    // 17 says they are reported "prior to the first evaluation of the source
    // text", and the only way to promise that is to answer before the compiler
    // starts.
    //
    // They use the parser's own prefix because an early error is a SyntaxError
    // in the source text, and `parse error:` is the only thing this engine says
    // that means that. `ct262` reads exactly that prefix to decide between
    // phase `parse` and phase `refusal` ("the compiler does not implement
    // this"). See lib/Script/compile/early_errors.hpp.
    if (const auto early = detail::find_early_error(tree, source, kind == script_kind::module_,
                                                    site, script_goal)) {
        out.ok = false;
        out.error = "parse error: " + early->message;
        if (early->offset != detail::early_error::nowhere) {
            out.error += position_in(source, early->offset, out);
        }
        return out;
    }
    out.source = std::string{source};
    out.eval_code = site != nullptr;
    detail::compiler_impl c{tree, out};
    c.module_scope_ = kind == script_kind::module_;
    // The caller's bytes, not `out.source`: every `node::text` is a view into
    // the buffer the parser saw, and `out.source` is a copy at a different
    // address, so the copy is the wrong thing to subtract against. See
    // compiler_impl::offset_of.
    c.source_view_ = source;
    c.completion_value_ = site != nullptr;
    for (const eval_binding & binding : outer) {
        c.eval_outer_names_.push_back(binding.name);
        c.eval_outer_readonly_.push_back(binding.readonly);
        c.eval_outer_lexical_.push_back(binding.lexical);
    }
    if (site != nullptr) { c.eval_site_ = *site; }
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
