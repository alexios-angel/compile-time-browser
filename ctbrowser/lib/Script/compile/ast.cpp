// compiler_impl - AST access.
//
// Reading the node pool, and compiling an expression from a
// FOREIGN ast - one parsed separately, whose nodes this compiler owns.
//
// One of the files carved out of a 3,845-line compile.cpp on 2026-08-09.
// The class is declared whole in compiler_impl.hpp beside this.

#include "compiler_impl.hpp"

namespace ctbrowser::script::detail {

void compiler_impl::compile_owned_expr(std::string source, std::uint16_t dst) {
    owned_sources_.push_back(std::make_unique<std::string>(std::move(source)));
    compile_foreign_expr(*owned_sources_.back(), dst);
}

void compiler_impl::compile_foreign_expr(std::string_view source, std::uint16_t dst) {
    auto tree = std::make_unique<vp::ast>(vp::parse(source));
    if (!tree->ok || tree->root < 0) {
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
