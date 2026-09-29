// compiler_impl - capture analysis.
//
// Which functions capture which names, answered by an EULER TOUR:
// a function's descendants are exactly those whose tick lies inside its
// half-open range. `tour` and `mentions_arguments` are the two hottest
// functions in a page render - see docs/performance.md.
//
// One of the files carved out of a 3,845-line compile.cpp on 2026-08-09.
// The class is declared whole in compiler_impl.hpp beside this.

#include "compiler_impl.hpp"

#include <algorithm>

namespace ctbrowser::script::detail {

bool compiler_impl::is_function_node(const vp::node & n) {
    return n.kind == vp::nk::func_decl || n.kind == vp::nk::func_expr || n.kind == vp::nk::arrow;
}

void compiler_impl::build_capture_index() {
    fn_range_.assign(ast_.nodes.size(), interval{});
    std::int32_t tick = 0;
    tour(ast_.root, -1, tick, true);
    for (auto & [name, ticks] : mentions_) {
        std::ranges::sort(ticks);
        ticks.erase(std::unique(ticks.begin(), ticks.end()), ticks.end());
    }
    std::ranges::sort(eval_ticks_);
}

bool compiler_impl::eval_reachable(std::string_view name) {
    if (name.empty() || name == "this") { return false; }
    const auto c = static_cast<unsigned char>(name[0]);
    return c == '_' || c == '$' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80;
}

bool compiler_impl::is_direct_eval_call(const vp::node & n) const {
    if (n.kind != vp::nk::call || n.a < 0) { return false; }
    const vp::node & callee = at(n.a);
    return callee.kind == vp::nk::ident && callee.text == "eval";
}

void compiler_impl::tour(std::int32_t idx, std::int32_t enclosing, std::int32_t & tick,
                         bool boundary) {
    if (idx < 0 || static_cast<std::size_t>(idx) >= ast_.nodes.size()) { return; }
    const vp::node & n = ast_.nodes[static_cast<std::size_t>(idx)];
    const bool opens = boundary || is_function_node(n);
    const std::int32_t inner = opens ? tick++ : enclosing;
    // RECORDED AGAINST `inner`, INCLUDING WHEN THIS NODE OPENED IT. A field
    // initialiser's boundary IS the node itself - `class A { val = v; }`
    // makes `v` the whole initialiser - so skipping the opener loses the
    // only mention there is, and the enclosing local never gets boxed.
    // vm_objects caught exactly that: `function build(v) { class A { val =
    // v; } ... }` read undefined. A real function node is never an ident,
    // so this costs it nothing.
    if (n.kind == vp::nk::ident) { mentions_[std::string{n.text}].push_back(inner); }
    // A DIRECT EVAL MENTIONS EVERY NAME IN SCOPE: its source can read and
    // write any binding of this function and of the ones around it, so all of
    // their locals must be cells it can be handed (is_captured). Recorded
    // against the function the call is in.
    if (is_direct_eval_call(n)) { eval_ticks_.push_back(inner); }
    // `new.target` - or an eval, which may say it - under the name of the
    // hidden local that carries it into arrows (new_target_local), so a
    // function asks is_captured before walking its body for one.
    if (n.kind == vp::nk::new_target || is_direct_eval_call(n)) {
        mentions_[std::string{new_target_local}].push_back(inner);
    }
    // A template's substitutions are text on the node, not children, so
    // nothing below this reaches them.
    if (n.kind == vp::nk::tmpl) {
        each_name_in_template(
            n.text, [&](std::string_view name) { mentions_[std::string{name}].push_back(inner); });
    }
    // An object shorthand `{ b }` is an IdentifierReference (13.2.5.5) whose
    // name lives in the prop node's TEXT, with no ident child - so, like a
    // template hole, nothing below records it. Missing it left a name captured
    // ONLY through a shorthand unboxed: is_captured answered false, so no
    // new_cell was emitted, and the boxing resolve_upvalue then does after the
    // fact turned every direct read into a cell_get on a plain register -
    // undefined. `for (const b of xs) { f(() => ({ b })); use(b); }` read
    // undefined (shadow-dom/attach-shadow-non-html-namespace.html).
    if (n.kind == vp::nk::prop && n.c == 2) { mentions_[std::string{n.text}].push_back(inner); }
    // An instance field's initialiser compiles into its OWN function - that
    // is what makes each instance get its own value - so anything it
    // mentions is captured exactly as if it had been written inside one.
    // Without this the initialiser reads an unboxed enclosing local and
    // finds undefined. Slot 1 is `b`, which for a field is the initialiser.
    // A STATIC field or a `static { }` block compiles into the class's
    // `<static>` function for the same reason (its `this` is the class), so
    // slot 1 opens a boundary for those too.
    const bool field_init = n.kind == vp::nk::class_member && (n.c == 0 || n.c == 3);
    const std::array<std::int32_t, 4> slots = child_slots(n);
    for (std::size_t i = 0; i < slots.size(); ++i) {
        tour(slots[i], inner, tick, field_init && i == 1);
    }
    for (const std::int32_t k : kids(n)) { tour(k, inner, tick, false); }
    if (opens) { fn_range_[static_cast<std::size_t>(idx)] = interval{inner, tick}; }
}

compiler_impl::interval compiler_impl::range_of(std::int32_t idx) const {
    if (idx < 0 || static_cast<std::size_t>(idx) >= fn_range_.size()) { return {}; }
    return fn_range_[static_cast<std::size_t>(idx)];
}

bool compiler_impl::mentions_arguments(std::int32_t idx) const {
    if (idx < 0) { return false; }
    const vp::node & n = at(idx);
    if (n.kind == vp::nk::func_decl || n.kind == vp::nk::func_expr) { return false; }
    if (n.kind == vp::nk::ident && n.text == "arguments") { return true; }
    // A direct eval's source may name `arguments`, so the function has one.
    if (is_direct_eval_call(n)) { return true; }
    if (n.kind == vp::nk::tmpl) {
        bool found = false;
        each_name_in_template(n.text,
                              [&](std::string_view name) { found = found || name == "arguments"; });
        if (found) { return true; }
    }
    for (const std::int32_t slot : child_slots(n)) {
        if (mentions_arguments(slot)) { return true; }
    }
    for (const std::int32_t k : kids(n)) {
        if (mentions_arguments(k)) { return true; }
    }
    return false;
}

bool compiler_impl::contains_closure(std::int32_t idx) const {
    if (idx < 0) { return false; }
    const vp::node & n = at(idx);
    if (is_function_node(n) || n.kind == vp::nk::class_decl) { return true; }
    for (const std::int32_t slot : child_slots(n)) {
        if (contains_closure(slot)) { return true; }
    }
    for (const std::int32_t k : kids(n)) {
        if (contains_closure(k)) { return true; }
    }
    return false;
}

bool compiler_impl::mentions_super(std::int32_t idx, bool root) const {
    if (idx < 0) { return false; }
    const vp::node & n = at(idx);
    if (n.kind == vp::nk::super_lit) { return true; }
    if (!root && (n.kind == vp::nk::func_decl || n.kind == vp::nk::func_expr)) { return false; }
    for (const std::int32_t slot : child_slots(n)) {
        if (mentions_super(slot, false)) { return true; }
    }
    for (const std::int32_t k : kids(n)) {
        if (mentions_super(k, false)) { return true; }
    }
    return false;
}

bool compiler_impl::contains_direct_eval(std::int32_t idx) const {
    // The index covers the program's own tree; a template hole's sub-tree
    // numbers its nodes afresh (see compile_ident's dead-zone note).
    if (current_ast_ != &ast_) { return false; }
    const interval where = range_of(idx);
    if (where.empty()) { return false; }
    const auto e = std::ranges::lower_bound(eval_ticks_, where.lo);
    return e != eval_ticks_.end() && *e < where.hi;
}

bool compiler_impl::own_direct_eval(std::int32_t idx) const {
    if (current_ast_ != &ast_) { return false; }
    const interval where = range_of(idx);
    // eval_ticks_ holds the tick of the function each call is IN.
    return !where.empty() && std::ranges::binary_search(eval_ticks_, where.lo);
}

bool compiler_impl::arrow_wants_new_target(std::int32_t idx, bool in_arrow) const {
    if (idx < 0) { return false; }
    const vp::node & n = at(idx);
    if (n.kind == vp::nk::func_decl || n.kind == vp::nk::func_expr) { return false; }
    const bool inside = in_arrow || n.kind == vp::nk::arrow;
    if (inside && (n.kind == vp::nk::new_target || is_direct_eval_call(n))) { return true; }
    for (const std::int32_t slot : child_slots(n)) {
        if (arrow_wants_new_target(slot, inside)) { return true; }
    }
    for (const std::int32_t k : kids(n)) {
        if (arrow_wants_new_target(k, inside)) { return true; }
    }
    return false;
}

bool compiler_impl::is_captured(std::string_view name) const {
    const interval where = frames_.back().captures;
    if (where.empty()) { return false; }
    // A direct eval in this function, or in one inside it, can reach every
    // named local here - `lo` INCLUSIVE, since an eval in the function itself
    // does. Not a pattern parameter's register: boxing it made the pattern
    // destructure the cell ("object is not iterable").
    if (const auto e = std::ranges::lower_bound(eval_ticks_, where.lo);
        e != eval_ticks_.end() && *e < where.hi && eval_reachable(name)) {
        return true;
    }
    const auto found = mentions_.find(name);
    if (found == mentions_.end()) { return false; }
    // The first tick strictly greater than `lo`; captured if it is also
    // inside. Strictly, because `lo` is this function's own tick and the
    // names it mentions itself are not captured by it.
    const auto & ticks = found->second;
    const auto at = std::upper_bound(ticks.begin(), ticks.end(), where.lo);
    return at != ticks.end() && *at < where.hi;
}

void compiler_impl::collect_lexical_names(std::span<const std::int32_t> stmts,
                                          std::vector<std::string> & out) const {
    for (const std::int32_t s : stmts) {
        const vp::node & stmt = at(s);
        if (stmt.kind == vp::nk::var_decl && stmt.text != "var") {
            for (const std::int32_t d : kids(stmt)) {
                if (at(d).b >= 0) {
                    pattern_names(at(d).b, out);
                } else {
                    out.emplace_back(at(d).text);
                }
            }
        } else if (stmt.kind == vp::nk::class_decl) {
            out.emplace_back(stmt.text);
        }
    }
}

void compiler_impl::collect_function_names(std::span<const std::int32_t> stmts,
                                           std::vector<std::string> & out) const {
    for (const std::int32_t s : stmts) {
        if (at(s).kind == vp::nk::func_decl) { out.emplace_back(at(s).text); }
    }
}

void compiler_impl::collect_declared_names(std::int32_t body) {
    if (body < 0) { return; }
    const vp::node & n = at(body);
    // A NESTED FUNCTION'S vars ARE ITS OWN. The walk used to descend through
    // a function declaration statement into its body, so `function f() { var
    // i }` made `i` a name of the enclosing scope too - harmless while the
    // list only fed was_predeclared, a phantom global once the script's list
    // became program::hoisted_vars.
    if (is_function_node(n) || n.kind == vp::nk::class_decl) { return; }
    if (n.kind == vp::nk::var_decl) {
        for (const std::int32_t d : kids(n)) { fn().declared.push_back(std::string{at(d).text}); }
        return;
    }
    if (n.kind == vp::nk::block || n.kind == vp::nk::program) {
        for (const std::int32_t s : kids(n)) { collect_declared_names(s); }
        return;
    }
    // loops and conditionals can declare too
    for (const std::int32_t slot : child_slots(n)) {
        if (slot >= 0 && at(slot).kind != vp::nk::func_decl && at(slot).kind != vp::nk::func_expr &&
            at(slot).kind != vp::nk::arrow) {
            collect_declared_names(slot);
        }
    }
}

void compiler_impl::predeclare_locals(std::int32_t body, bool lexical_only) {
    // A PROGRAM'S top level counts too, not only a function's block. It
    // never used to: a classic script's top-level declarations are globals,
    // so there was nothing to pre-declare. A MODULE's are locals, and a
    // function declared there has to be a binding before `export { f }` can
    // find it - which it could not, and the importer was told the module
    // had no such export.
    if (body < 0 || (at(body).kind != vp::nk::block && at(body).kind != vp::nk::program)) {
        return;
    }
    // A NAMED FUNCTION EXPRESSION'S OWN NAME is not one of its body's
    // bindings - it lives in a scope outside them (15.2.5 step 4) - so any
    // declaration of the name in the body shadows it: `function me() { var
    // me; }` has a `me` that is undefined, and assignable.
    const auto bound = [this](std::string_view name) {
        const local * l = find_local_entry(fn(), name);
        return l != nullptr && l->readonly != immutability::sloppy;
    };
    const auto hoist = [this, &bound](std::string name, std::uint32_t initialized_at = 0,
                                      immutability readonly = immutability::none,
                                      bool lexical = false) {
        if (name.empty() || bound(name)) { return; }
        const std::uint16_t r = declare_local(name);
        fn().locals.back().initialized_at = initialized_at;
        fn().locals.back().readonly = readonly;
        fn().locals.back().lexical = lexical;
        proto().emit(instruction{op::load_undef, r});
        if (fn().locals.back().boxed) { proto().emit(instruction{op::new_cell, r}); }
        fn().predeclared.push_back(std::move(name));
    };
    for (const std::int32_t outer : kids(at(body))) {
        // `export const x = 1` and `export function f() {}` DECLARE, and
        // the declaration is wrapped. Looking only at the wrapper hoists
        // nothing, so `export function f` was never a binding here - and
        // then `export { f }` could not find it to publish, which read as
        // "has no export named f" on the importing side.
        // `export default <expr>` carries the EXPRESSION in `a` and says
        // so with `c == 1`, so unwrapping it hands the loop an expression
        // where it expects a statement. It binds one name of its own.
        if (at(outer).kind == vp::nk::export_decl && at(outer).c == 1) {
            hoist(std::string{default_binding});
            // ...AND A NAMED ONE BINDS ITS NAME TOO (16.2.3.7: the
            // declaration's BoundNames) - `export default function F() {}`
            // and `export default class C {}` are declarations of F and C,
            // which `export { F as G }` may name.
            if (const std::int32_t named = default_export_name(outer); named >= 0) {
                hoist(std::string{at(named).text});
            }
            continue;
        }
        const std::int32_t stmt =
            at(outer).kind == vp::nk::export_decl && at(outer).a >= 0 ? at(outer).a : outer;
        if (lexical_only && !(at(stmt).kind == vp::nk::var_decl && at(stmt).text != "var") &&
            at(stmt).kind != vp::nk::class_decl) {
            continue;
        }
        if (at(stmt).kind == vp::nk::var_decl) {
            // A `let`/`const`/`using` binding is in its TDZ until its
            // declarator has run: the offset past the declarator (or, with
            // no initialiser, past the statement) is when it is initialised.
            const bool lexical = at(stmt).text != "var";
            const immutability readonly = declared_immutability(at(stmt));
            for (const std::int32_t d : kids(at(stmt))) {
                const std::uint32_t ready = lexical ? at(d).end : 0;
                if (at(d).b >= 0) { // a shape: hoist every name inside it
                    // ...with no static dead zone: `const {a, b = a} = o`
                    // initialises `a` before `b`'s default reads it, which a
                    // single offset cannot say.
                    std::vector<std::string> names;
                    pattern_names(at(d).b, names);
                    for (std::string & name : names) {
                        hoist(std::move(name), 0, readonly, lexical);
                    }
                } else {
                    hoist(std::string{at(d).text}, ready, readonly, lexical);
                }
            }
        } else if (at(stmt).kind == vp::nk::import_decl) {
            // AN IMPORT IS A DECLARATION, so its names are hoisted like any
            // other. Allocating them inside the statement instead put them
            // ABOVE the statement's register mark, and `release_to(mark)`
            // handed them straight back - so `import { a }` survived by
            // luck and `import { a, b }` did not: the next statement's
            // temporary landed on top of `b`. The same trap
            // declare_pattern_names records for destructuring.
            // Immutable, as every import binding is (InitializeEnvironment:
            // an import binding, or CreateImmutableBinding for a namespace).
            for (const std::int32_t spec : kids(at(stmt))) {
                hoist(std::string{at(spec).text}, 0, immutability::strict);
            }
        } else if (at(stmt).kind == vp::nk::func_decl) {
            // A nested function declaration is a BINDING IN ITS SCOPE, and
            // it has to exist before its own body compiles or a recursive
            // call inside it resolves to a global instead of to itself.
            hoist(std::string{at(stmt).text});
        } else if (at(stmt).kind == vp::nk::class_decl) {
            // A CLASS DECLARATION IS A BINDING TOO, and it has to be
            // hoisted for a reason worth stating: a local first declared
            // while an EXPRESSION is being compiled sits above the
            // statement's register mark, so the statement releases it and
            // the next statement's temporaries reuse the slot. `class S {}`
            // followed by two `new S()` therefore worked once and then
            // found an object in the register the second time.
            hoist(std::string{at(stmt).text}, at(stmt).end, immutability::none, true);
        }
    }
    if (lexical_only) { return; }
    // And every `var` in a NESTED block, which the loop above cannot see -
    // it walks this body's own statements only. `hoist` returns early on a
    // name already declared, so the statements just handled cost a lookup.
    std::vector<std::string> vars; // every `var` name below this body, any depth
    const auto hoist_var = [&](std::string name) {
        vars.push_back(name);
        hoist(std::move(name));
    };
    for (const std::int32_t stmt : kids(at(body))) { hoist_nested_vars(stmt, hoist_var); }
    // ANNEX B.3.3 (sloppy code only): a function declared in a nested block
    // takes a var binding of this function as well - created at entry when
    // nothing else of that name exists, and WRITTEN when the declaration is
    // evaluated (B.3.3.1 steps 1.a.ii.2 and 1.a.ii.3, which are two separate
    // things: with a `var` or another function declaration of the name the
    // binding is already there, and the write still happens).
    //
    // It is refused only for a name a PARAMETER or a LEXICAL declaration of
    // this body already binds - "replacing the declaration with a
    // VariableStatement would produce an Early Error" - which is what "a
    // local that is neither a var nor one of this body's own functions"
    // means here, the parameters and the body's let/const/class having been
    // declared above.
    if (!fn().is_strict) {
        std::vector<std::string> var_scoped = vars;
        for (const std::int32_t stmt : kids(at(body))) {
            if (at(stmt).kind == vp::nk::func_decl) { var_scoped.emplace_back(at(stmt).text); }
        }
        std::vector<std::string> lexical;
        each_block_function(
            body,
            [&](std::string name, std::int32_t decl) {
                // NEVER `arguments`: the arguments object binding is not
                // something a block's function may create or overwrite
                // (B.3.3.1 step 1.a.ii.2, "and F is not `arguments`"), and
                // hoisting a local of that name shadowed the object itself.
                if (name == "arguments") { return; }
                const bool fresh = !bound(name);
                if (fresh) {
                    // AND IT IS VAR-SCOPED FROM NOW ON. Two blocks declaring
                    // one name are two applicable declarations: the second
                    // finds the binding the first made and only skips
                    // CREATING it again (step 1.a.ii.2 guards the creation,
                    // not the write), so `{ function h() {} } { function h()
                    // {} }` ends with the second.
                    var_scoped.push_back(name);
                    hoist(std::move(name));
                } else if (std::find(var_scoped.begin(), var_scoped.end(), name) ==
                           var_scoped.end()) {
                    return;
                }
                fn().annex_b_decls.push_back(decl);
            },
            lexical, true);
    }
}

void compiler_impl::hoist_captured_lexicals(const vp::node & block) {
    // The capture index covers the program's own tree (contains_direct_eval).
    if (current_ast_ != &ast_) { return; }
    const std::span<const std::int32_t> stmts = kids(block);
    const auto lexical = [this](std::int32_t s) {
        return at(s).kind == vp::nk::var_decl && at(s).text != "var";
    };
    // The cheap question first: is any simple `let`/`const` of this block
    // captured by a function at all? Nearly every block answers no.
    bool captured = false;
    for (const std::int32_t s : stmts) {
        if (!lexical(s)) { continue; }
        for (const std::int32_t d : kids(at(s))) {
            captured = captured || (at(d).b < 0 && is_captured(at(d).text));
        }
    }
    if (!captured) { return; }
    for (std::size_t i = 0; i < stmts.size(); ++i) {
        if (!lexical(stmts[i])) { continue; }
        const std::span<const std::int32_t> declarators = kids(at(stmts[i]));
        for (std::size_t j = 0; j < declarators.size(); ++j) {
            const vp::node & decl = at(declarators[j]);
            if (decl.b >= 0 || !is_captured(decl.text) ||
                find_local_in_current_scope(decl.text) != nullptr) {
                continue;
            }
            // Read by a function compiled before the declaration - in an
            // earlier statement or declarator - which would otherwise
            // resolve the name to some other binding.
            const bool before =
                std::ranges::any_of(stmts.first(i),
                                    [&](std::int32_t s) { return closure_reads(s, decl.text); }) ||
                std::ranges::any_of(declarators.first(j), [&](std::int32_t d) {
                    return closure_reads(at(d).a, decl.text);
                });
            if (!before) { continue; }
            // As predeclare_locals makes a function body's own: in its
            // dead zone for this frame until the declarator has run - the
            // offset its statement knows it by (compile_stmt).
            const std::uint16_t r = declare_local(std::string{decl.text});
            fn().locals.back().initialized_at = decl.end;
            fn().locals.back().readonly = declared_immutability(at(stmts[i]));
            fn().locals.back().lexical = true;
            proto().emit(instruction{op::load_undef, r});
            if (fn().locals.back().boxed) { proto().emit(instruction{op::new_cell, r}); }
            // A closure may run before OR after this declaration. Its
            // runtime readiness travels in an ordinary captured cell.
            const std::string flag = "@tdz:" + std::string{decl.text};
            fn().locals.back().ready_flag = flag;
            const std::uint16_t ready = declare_local(flag);
            fn().locals.back().boxed = true;
            proto().emit(instruction{op::load_false, ready});
            proto().emit(instruction{op::new_cell, ready});
        }
    }
}

bool compiler_impl::closure_reads(std::int32_t idx, std::string_view name, bool in_function) {
    if (idx < 0) { return false; }
    const vp::node & n = at(idx);
    if (in_function) {
        // A mention as tour records one: a name, a shorthand `{ name }`, a
        // template's substitution.
        if ((n.kind == vp::nk::ident || (n.kind == vp::nk::prop && n.c == 2)) && n.text == name) {
            return true;
        }
        if (n.kind == vp::nk::tmpl) {
            bool found = false;
            each_name_in_template(n.text, [&](std::string_view m) { found = found || m == name; });
            if (found) { return true; }
        }
    }
    if (binds_again(n, name)) { return false; }
    const bool function = in_function || is_function_node(n);
    // A field initialiser runs later, as a function body does (see tour).
    const bool field_init = n.kind == vp::nk::class_member && (n.c == 0 || n.c == 3);
    const std::array<std::int32_t, 4> slots = child_slots(n);
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (closure_reads(slots[i], name, function || (field_init && i == 1))) { return true; }
    }
    return std::ranges::any_of(kids(n),
                               [&](std::int32_t k) { return closure_reads(k, name, function); });
}

bool compiler_impl::binds_again(const vp::node & n, std::string_view name) {
    std::vector<std::string> names;
    if (is_function_node(n)) {
        // Its parameters, its own name as an expression, `arguments` unless
        // it is an arrow, and every name its body declares.
        if ((n.kind == vp::nk::func_expr && n.text == name) ||
            (n.kind != vp::nk::arrow && name == "arguments")) {
            return true;
        }
        for (const std::int32_t p : kids(n)) {
            names.emplace_back(at(p).text);
            pattern_names(at(p).b, names);
        }
        if (n.a >= 0 && at(n.a).kind == vp::nk::block) {
            hoist_nested_vars(n.a, [&](std::string v) { names.push_back(std::move(v)); });
            collect_lexical_names(kids(at(n.a)), names);
            collect_function_names(kids(at(n.a)), names);
        }
    } else if (n.kind == vp::nk::block) {
        collect_lexical_names(kids(n), names);
        collect_function_names(kids(n), names);
    } else if (n.kind == vp::nk::switch_stmt) {
        // Every CaseClause of a switch is one scope.
        for (const std::int32_t clause : kids(n)) {
            collect_lexical_names(kids(at(clause)), names);
            collect_function_names(kids(at(clause)), names);
        }
    } else if (n.kind == vp::nk::catch_clause) {
        names.emplace_back(n.text);
        pattern_names(n.b, names);
    } else if ((n.kind == vp::nk::for_stmt || n.kind == vp::nk::forof_stmt) && n.a >= 0) {
        collect_lexical_names(std::span<const std::int32_t>{&n.a, 1}, names);
    } else if (n.kind == vp::nk::class_decl) {
        names.emplace_back(n.text);
    }
    return std::ranges::find(names, name) != names.end();
}

bool compiler_impl::was_predeclared(std::string_view name) const {
    for (const std::string & p : frames_.back().predeclared) {
        if (p == name) { return true; }
    }
    return false;
}

} // namespace ctbrowser::script::detail
