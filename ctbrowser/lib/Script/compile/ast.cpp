//===- ctbrowser/lib/Script/compile/ast.cpp - AST access --------*- C++ -*-===//
//
// compiler_impl members that read the node pool, and that compile an
// expression from a foreign AST: one parsed separately, whose nodes this
// compiler owns. The class is declared in compiler_impl.hpp beside this file.
//
//===----------------------------------------------------------------------===//

#include "compiler_impl.hpp"

namespace ctbrowser::script::detail {

void compiler_impl::compile_owned_expr(std::string source, std::uint16_t dst) {
    owned_sources_.push_back(std::make_unique<std::string>(std::move(source)));
    compile_foreign_expr(*owned_sources_.back(), dst);
}

void compiler_impl::compile_foreign_expr(std::string_view source, std::uint16_t dst) {
    auto tree = std::make_unique<vp::ast>(vp::parse(source));
    if (!tree->ok || tree->root < 0) {
        // A template hole that does not parse is a SyntaxError, not undefined.
        // Thrown where it is evaluated rather than refusing the whole script:
        // the parse also fails on engine gaps (`${yield x}` parses alone).
        emit_throw("SyntaxError", "parse error: " + std::string{tree->error});
        proto().emit(instruction{op::load_undef, dst});
        return;
    }
    const vp::ast * saved = current_ast_;
    current_ast_ = tree.get();
    // The parse produces a program node; its single expression statement is
    // what we want.
    std::int32_t expression = tree->root;
    if (at(expression).kind == vp::nk::program) {
        const auto statements = kids(at(expression));
        expression = statements.empty() ? -1 : statements.front();
    }
    if (expression >= 0 && at(expression).kind == vp::nk::expr_stmt) {
        expression = at(expression).a;
    }
    if (expression >= 0) {
        compile_expr(expression, dst);
    } else {
        proto().emit(instruction{op::load_undef, dst});
    }
    current_ast_ = saved;
    owned_asts_.push_back(std::move(tree));
}

std::span<const std::int32_t> compiler_impl::kids(const vp::node & n) const {
    if (n.list < 0 || n.list_len <= 0) { return {}; }
    return std::span<const std::int32_t>{current_ast_->pool.data() + n.list,
                                         static_cast<std::size_t>(n.list_len)};
}

} // namespace ctbrowser::script::detail
