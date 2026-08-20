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
    out_.functions.emplace_back();
    out_.functions[0].name = "<script>";
    frames_.emplace_back();
    frames_.back().proto = 0;
    push_scope();

    const vp::node & root = at(ast_.root);
    build_capture_index();
    fn().captures = range_of(ast_.root);
    collect_declared_names(ast_.root);
    // A MODULE'S TOP LEVEL IS A SCOPE, so its declarations are pre-declared
    // exactly as a function body's are. A classic script's are globals and
    // need none of this, which is why it was never called here before.
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
    for (const std::int32_t s : kids(root)) {
        if (at(declared_by(s)).kind != vp::nk::func_decl) { compile_stmt(s); }
    }
    proto().emit(instruction{op::ret_undef});
    finish_frame(fn().proto, 0);
    pop_scope();
    frames_.pop_back();
}

} // namespace ctbrowser::script::detail
