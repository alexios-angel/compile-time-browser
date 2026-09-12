// compiler_impl - entry.
//
// `compile_program` - the top of the compiler, and the only member
// `compiler::compile()` calls.
//
// One of the files carved out of a 3,845-line compile.cpp on 2026-08-09.
// The class is declared whole in compiler_impl.hpp beside this.

#include "compiler_impl.hpp"

namespace ctbrowser::script::detail {

void compiler_impl::compile_program() {
    const std::uint32_t script = new_proto(0);
    out_.functions[script].name = "<script>";
    frames_.emplace_back();
    frames_.back().proto = script;
    push_scope();

    const vp::node & root = at(ast_.root);
    // A module is strict code; a script is when it says so (11.2.1, 16.2.1).
    fn().is_strict = module_scope_ || has_use_strict_directive(ast_.root);
    out_.functions[script].is_strict = fn().is_strict;
    build_capture_index();
    fn().captures = range_of(ast_.root);
    collect_declared_names(ast_.root);
    // A MODULE'S TOP LEVEL IS A SCOPE, so its declarations are pre-declared
    // exactly as a function body's are. A classic script's are globals and
    // need none of this, which is why it was never called here before.
    // A CLASSIC SCRIPT HOISTS ITS `var`s (16.1.7 GlobalDeclarationInstantiation
    // step 12: CreateGlobalVarBinding for each, undefined unless it already
    // exists): `use(x); var x = 1;` reads undefined, and since 2026-09-12 an
    // unbound name is a ReferenceError, so without this the hoisting gap
    // became a throw. Recorded on the program rather than emitted - a call
    // at the top of every script was a global read no native pipeline could
    // type - and context::run binds them before the first instruction.
    // VarDeclaredNames is `var` only - a `let`/`const` is lexical, and a
    // block's `const` is nobody's global - which is what hoist_nested_vars keeps.
    if (!module_scope_) {
        hoist_nested_vars(ast_.root,
                          [&](std::string n) { out_.hoisted_vars.push_back(std::move(n)); });
        std::sort(out_.hoisted_vars.begin(), out_.hoisted_vars.end());
        out_.hoisted_vars.erase(std::unique(out_.hoisted_vars.begin(), out_.hoisted_vars.end()),
                                out_.hoisted_vars.end());
    }
    if (module_scope_) {
        predeclare_locals(ast_.root);
        // THEN THE IMPORTS, still at entry: a function declared anywhere in
        // this module closes over them, and the closures are made below.
        for (const std::int32_t s : kids(root)) { bind_imports(s); }
        // AND THE RE-EXPORT EDGES, which are data for the loader rather
        // than code - collected here because this is where the top level is
        // walked, not because anything is emitted.
        for (const std::int32_t s : kids(root)) { collect_reexports(s); }
        // AND THE EXPORTS, likewise before a single statement of the body
        // runs. See bind_export.
        std::vector<std::pair<std::string, std::string>> bindings;
        for (const std::int32_t s : kids(root)) { export_bindings(s, bindings); }
        for (const auto & [local_name, exported] : bindings) {
            const int r = find_local(local_name);
            if (r < 0) {
                fail("`export { " + local_name +
                     " }` names something this module does not "
                     "declare");
                continue;
            }
            bind_export(exported, static_cast<std::uint16_t>(r));
        }
    }
    // Function declarations hoist: a script may call one before its text.
    // THROUGH AN `export` WRAPPER TOO - `export function f() {}` is a
    // function declaration that hoists like any other, and looking only at
    // the wrapper left it compiled in the second pass, after anything that
    // called it.
    const auto declared_by = [this](std::int32_t s) {
        return at(s).kind == vp::nk::export_decl && at(s).c != 1 && at(s).a >= 0 ? at(s).a : s;
    };
    for (const std::int32_t s : kids(root)) {
        if (at(declared_by(s)).kind == vp::nk::func_decl) { compile_stmt(s); }
    }
    const std::span<const std::int32_t> body = kids(root);
    for (std::size_t i = 0; i < body.size(); ++i) {
        const std::int32_t s = body[i];
        if (at(declared_by(s)).kind == vp::nk::func_decl) { continue; }
        // `eval("a; b")` is b: the last statement, when it is an expression,
        // is returned rather than discarded.
        if (completion_value_ && i + 1 == body.size() && at(s).kind == vp::nk::expr_stmt &&
            at(s).a >= 0) {
            const std::uint32_t mark = reg_mark();
            const std::uint16_t r = alloc_reg();
            compile_expr(at(s).a, r);
            proto().emit(instruction{op::ret, r});
            release_to(mark);
            continue;
        }
        compile_stmt(s);
    }
    proto().emit(instruction{op::ret_undef});
    finish_frame(fn().proto, 0);
    pop_scope();
    frames_.pop_back();
}

bool compiler_impl::has_use_strict_directive(std::int32_t body) const {
    if (body < 0) { return false; }
    for (const std::int32_t s : kids(at(body))) {
        const vp::node & stmt = at(s);
        if (stmt.kind != vp::nk::expr_stmt || stmt.a < 0) { return false; }
        const vp::node & e = at(stmt.a);
        if (e.kind != vp::nk::str) { return false; } // the prologue ended
        // The lexeme keeps its quotes, and escapes are not allowed to spell
        // it (11.2.1: "the exact code point sequence").
        if (e.text == "\"use strict\"" || e.text == "'use strict'") { return true; }
    }
    return false;
}

} // namespace ctbrowser::script::detail
