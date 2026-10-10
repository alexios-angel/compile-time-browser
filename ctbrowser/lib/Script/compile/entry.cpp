//===- ctbrowser/lib/Script/compile/entry.cpp - Compiler entry point ------===//
//
// compiler_impl::compile_program, the top of the compiler and the only member
// compiler::compile() calls. It sets up the program frame (script, module or
// eval), hoists declarations, then compiles the body. Section numbers are
// ECMA-262's. compiler_impl is declared in compiler_impl.hpp.
//
//===----------------------------------------------------------------------===//

#include <boost/scope/scope_exit.hpp>

#include "compiler_impl.hpp"
#include "early_errors.hpp"

namespace ctbrowser::script::detail {

void compiler_impl::compile_program() {
    const std::uint32_t script = new_proto(0);
    out_.functions[script].name = "<script>";
    frames_.emplace_back();
    frames_.back().proto = script;
    push_scope();

    const vp::node & root = at(ast_.root);
    // A module is strict code; a script is when it says so (11.2.1, 16.2.1).
    fn().is_strict = module_scope_ || has_use_strict_directive(ast_.root) || eval_site_.strict;
    out_.functions[script].is_strict = fn().is_strict;
    // An eval program is the one that returns its completion value.
    top_level_is_scope_ = module_scope_ || (completion_value_ && fn().is_strict);
    // A direct eval's program is closed over its caller's bindings: they are
    // the entry function's upvalues, in the order the caller hands the cells
    // over (context: direct_eval_name), and resolve_upvalue(0, ...) finds them.
    for (std::size_t i = 0; i < eval_outer_names_.size(); ++i) {
        (void)add_upvalue(0, eval_outer_names_[i],
                          upvalue_desc{false, static_cast<std::uint16_t>(i)});
    }
    // A direct eval sees its caller's private names, under the caller's keys.
    if (!eval_site_.private_names.empty()) {
        private_scope outer;
        for (const auto & [name, key] : eval_site_.private_names) {
            outer.names.emplace_back(name);
            outer.keys.emplace_back(key);
        }
        private_scopes_.push_back(std::move(outer));
    }
    build_capture_index();
    fn().captures = range_of(ast_.root);
    collect_declared_names(ast_.root);
    // A classic script hoists its `var`s (16.1.7 GlobalDeclarationInstantiation
    // step 12): `use(x); var x = 1;` reads undefined rather than throwing
    // ReferenceError. The names are recorded on the program
    // (program::hoisted_vars) instead of emitted as code, so no native
    // pipeline has to type an extra global read, and context::run binds them
    // before the first instruction. Only `var`s count; `let`/`const` are
    // lexical, which is what hoist_nested_vars keeps.
    if (!top_level_is_scope_) {
        hoist_nested_vars(ast_.root,
                          [&](std::string n) { out_.hoisted_vars.push_back(std::move(n)); });
        // B.3.3 for a script: a block's function declaration is a global var
        // too (undefined until the block runs - compile_function_decl writes
        // it as a global at any depth of a classic script).
        // Only the ones in a block: a top-level function declaration is a
        // global the declaration itself writes, and listing it here as well
        // would show the native prover a var where it expects a function.
        if (!fn().is_strict) {
            // ...and the script's own `let`/`const`/`class` shadow it, which
            // is B.3.3.2's "If env.HasLexicalDeclaration(F) is false": `let f
            // = 1; { function f() {} }` leaves the global alone entirely.
            // `each_block_function` is handed the program node so it sees
            // them; `true` says this level's own function declarations are
            // globals the declaration itself writes, not block functions.
            std::vector<std::string> lexical;
            each_block_function(
                ast_.root,
                [&](std::string n, std::int32_t decl) {
                    fn().annex_b_decls.push_back(decl);
                    out_.annex_b_names.push_back(std::move(n));
                },
                lexical, true);
        }
        std::sort(out_.hoisted_vars.begin(), out_.hoisted_vars.end());
        out_.hoisted_vars.erase(std::unique(out_.hoisted_vars.begin(), out_.hoisted_vars.end()),
                                out_.hoisted_vars.end());
        // A block function's name is a var either way; it is an Annex B name
        // (program::annex_b_names) only when no `var` declares it too.
        std::ranges::sort(out_.annex_b_names);
        out_.annex_b_names.erase(std::ranges::unique(out_.annex_b_names).begin(),
                                 out_.annex_b_names.end());
        std::erase_if(out_.annex_b_names, [&](const std::string & name) {
            return std::ranges::binary_search(out_.hoisted_vars, name);
        });
        out_.hoisted_vars.insert(out_.hoisted_vars.end(), out_.annex_b_names.begin(),
                                 out_.annex_b_names.end());
        std::ranges::sort(out_.hoisted_vars);
        // 19.2.1.3 step 3: a sloppy direct eval's `var` or function may not
        // share a name with a `let`, `const` or `class` of its caller's
        // function - hoisting the var would reach through it.
        if (std::ranges::find(eval_outer_lexical_, true) != eval_outer_lexical_.end()) {
            std::vector<std::string> var_names;
            hoist_nested_vars(ast_.root,
                              [&](std::string name) { var_names.push_back(std::move(name)); });
            for (const std::int32_t s : kids(root)) {
                if (at(s).kind == vp::nk::func_decl) { var_names.emplace_back(at(s).text); }
            }
            for (const std::string & name : var_names) {
                const auto it = std::ranges::find(eval_outer_names_, name);
                const auto at_index = static_cast<std::size_t>(it - eval_outer_names_.begin());
                if (it != eval_outer_names_.end() && at_index < eval_outer_lexical_.size() &&
                    eval_outer_lexical_[at_index]) {
                    fail("Identifier '" + name + "' has already been declared");
                }
            }
        }
        // A direct eval's `var` of a caller's binding is that binding, not a
        // global to create.
        const auto caller_binds = [&](const std::string & name) {
            return std::ranges::find(eval_outer_names_, name) != eval_outer_names_.end();
        };
        std::erase_if(out_.hoisted_vars, caller_binds);
        std::erase_if(out_.annex_b_names, caller_binds);
    }
    // A module's top level is a scope, so its declarations are pre-declared
    // exactly as a function body's are. An eval's `let`, `const` and `class`
    // are its own, strict or not (19.2.1.3 step 17: a new declarative
    // environment), and are pre-declared the same way; only its `var`s go the
    // caller's way.
    if (top_level_is_scope_) {
        predeclare_locals(ast_.root);
    } else if (completion_value_) {
        predeclare_locals(ast_.root, true);
    }
    // A classic script's top-level `const`s are globals here, and immutable
    // ones, known before the hoisted functions below compile their writes.
    if (!top_level_is_scope_ && !completion_value_) {
        for (const std::int32_t s : kids(root)) {
            if (at(s).kind != vp::nk::var_decl ||
                declared_immutability(at(s)) == immutability::none) {
                continue;
            }
            for (const std::int32_t d : kids(at(s))) {
                if (at(d).b >= 0) {
                    pattern_names(at(d).b, script_consts_);
                } else {
                    script_consts_.emplace_back(at(d).text);
                }
            }
        }
        // Sorted, for binding_immutability's binary search: every write to a
        // global asks, and a large script has many of both.
        std::ranges::sort(script_consts_);
        // Collect the names GlobalDeclarationInstantiation compares (16.1.7
        // steps 3-6); see program::lexical_names.
        for (const std::int32_t s : kids(root)) {
            const vp::node & d = at(s);
            if (d.kind == vp::nk::var_decl && d.text != "var") {
                for (const std::int32_t each : kids(d)) {
                    if (at(each).b >= 0) {
                        pattern_names(at(each).b, out_.lexical_names);
                    } else {
                        out_.lexical_names.emplace_back(at(each).text);
                    }
                }
            } else if (d.kind == vp::nk::class_decl && !d.text.empty()) {
                out_.lexical_names.emplace_back(d.text);
            } else if (d.kind == vp::nk::func_decl && !d.text.empty()) {
                out_.function_names.emplace_back(d.text);
            }
        }
    }
    // An eval in the global variable environment (program::eval_global): its
    // top-level functions are what EvalDeclarationInstantiation checks with
    // CanDeclareGlobalFunction and makes with CreateGlobalFunctionBinding.
    if (!top_level_is_scope_ && completion_value_ && !eval_site_.in_function && !fn().is_strict) {
        out_.eval_global = true;
        for (const std::int32_t s : kids(root)) {
            if (at(s).kind == vp::nk::func_decl && !at(s).text.empty()) {
                out_.function_names.emplace_back(at(s).text);
            }
        }
    }
    if (module_scope_) {
        // Then the imports, still at entry: a function declared anywhere in
        // this module closes over them, and the closures are made below.
        for (const std::int32_t s : kids(root)) { bind_imports(s); }
        // Then the re-export edges, which are data for the loader rather
        // than code, collected here because this is where the top level is
        // walked, not because anything is emitted.
        for (const std::int32_t s : kids(root)) { collect_reexports(s); }
        // Then the exports, likewise before a single statement of the body
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
    // That includes one inside an `export` wrapper: `export function f() {}`
    // hoists like any other, so declared_by looks through the wrapper.
    const auto declared_by = [this](std::int32_t s) {
        return at(s).kind == vp::nk::export_decl && at(s).c != 1 && at(s).a >= 0 ? at(s).a : s;
    };
    // `export default function () {}` is a declaration too (16.2.3: a
    // HoistableDeclaration[+Default]), instantiated before the module's first
    // statement - a module importing itself, or a cycle, calls it before its
    // text. A parenthesised function is an expression and is not hoisted; the
    // parser gives the two the same node, so the token before the function
    // says which: `default`, or a `(` (after_open_paren, which a comment
    // between them cannot fool). Asked once, before the two passes.
    const auto hoisted_default = [this](std::int32_t s) {
        const vp::node & n = at(s);
        if (!module_scope_ || n.kind != vp::nk::export_decl || n.c != 1 || n.a < 0 ||
            at(n.a).kind != vp::nk::func_expr) {
            return false;
        }
        const std::size_t begin = at(n.a).begin;
        return begin != 0 && begin <= source_view_.size() && !after_open_paren(source_view_, begin);
    };
    std::int32_t default_declaration = -1;
    for (const std::int32_t s : kids(root)) {
        if (hoisted_default(s)) {
            default_declaration = s;
            break;
        }
    }
    // A direct eval inside functions that keep their evals' `var`s on objects
    // (with_scope::eval_vars) gets those objects as bindings: every name beyond
    // the eval's own is looked up on them first, innermost first - deepest
    // name last - and a sloppy eval's `var`s and functions are made on the
    // innermost, its caller's variable environment (19.2.1.3 steps 16-17
    // for one that is not the global environment: CreateMutableBinding
    // (name, true) when it does not have the name - deletable, and undefined
    // until written), not as globals.
    std::vector<std::string> eval_vars;
    for (const std::string & name : eval_outer_names_) {
        if (name.starts_with(eval_vars_prefix)) { eval_vars.push_back(name); }
    }
    std::ranges::sort(eval_vars, [](const std::string & a, const std::string & b) {
        return a.size() != b.size() ? a.size() < b.size() : a < b;
    });
    for (const std::string & name : eval_vars) {
        with_scopes_.push_back(with_scope{name, 0, 0, true});
    }
    if (!eval_vars.empty()) {
        eval_vars_name_ = eval_vars.back();
        if (!fn().is_strict) {
            std::vector<std::string> names = out_.hoisted_vars;
            for (const std::int32_t s : kids(root)) {
                if (at(s).kind == vp::nk::func_decl && resolve_upvalue(0, at(s).text) < 0) {
                    names.emplace_back(at(s).text);
                }
            }
            std::ranges::sort(names);
            names.erase(std::ranges::unique(names).begin(), names.end());
            const std::uint32_t mark = reg_mark();
            const std::uint16_t object = alloc_reg();
            emit_plain_read(eval_vars_name_, object);
            for (const std::string & name : names) {
                const std::uint32_t inner = reg_mark();
                const std::uint16_t key = alloc_reg();
                emit_string(key, name);
                const std::uint16_t has = alloc_reg();
                proto().emit(instruction{op::has_property, has, key, object});
                const std::size_t exists = proto().emit(instruction{op::jump_if_true, has});
                const std::uint16_t nothing = alloc_reg();
                proto().emit(instruction{op::load_undef, nothing});
                proto().emit(instruction{op::set_prop, object, name_operand(name), nothing});
                patch_here(exists);
                release_to(inner);
            }
            release_to(mark);
            out_.eval_vars_env = true;
        }
    }
    const boost::scope::scope_exit leave_eval_vars{[this, handed = eval_vars.size()]() noexcept {
        with_scopes_.resize(with_scopes_.size() - handed);
    }};
    for (const std::int32_t s : kids(root)) {
        if (at(declared_by(s)).kind == vp::nk::func_decl || s == default_declaration) {
            compile_stmt(s);
        }
    }
    const std::span<const std::int32_t> body = kids(root);
    // `eval("a; b")` is b, and `eval("1; if (x) {}")` is undefined: the
    // completion register below is what every statement of the program
    // updates (see completion_reg_), and it is what the program returns.
    // Allocated here, above every statement's mark, so nothing releases it.
    if (completion_value_) {
        completion_reg_ = alloc_reg();
        proto().emit(instruction{op::load_undef, static_cast<std::uint16_t>(completion_reg_)});
    }
    const auto ret_completion = [&] {
        if (completion_reg_ >= 0) {
            proto().emit(instruction{op::ret, static_cast<std::uint16_t>(completion_reg_)});
        } else {
            proto().emit(instruction{op::ret_undef});
        }
    };
    // A `using` at the top level: the rest of the script is its region.
    bool has_using = false;
    for (const std::int32_t s : body) { has_using = has_using || is_using_decl(at(s)); }
    if (has_using) {
        compile_statement_list(body, true);
        ret_completion();
        finish_frame(fn().proto, 0);
        pop_scope();
        frames_.pop_back();
        return;
    }
    for (const std::int32_t s : body) {
        if (at(declared_by(s)).kind == vp::nk::func_decl || s == default_declaration) { continue; }
        compile_stmt(s);
    }
    ret_completion();
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
        if (e.kind != vp::nk::str) { return false; } // The prologue ended.
        // The lexeme keeps its quotes, and escapes are not allowed to spell
        // it (11.2.1: "the exact code point sequence").
        if (e.text == "\"use strict\"" || e.text == "'use strict'") { return true; }
    }
    return false;
}

} // namespace ctbrowser::script::detail
