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
    // A template's substitutions are text on the node, not children, so
    // nothing below this reaches them.
    if (n.kind == vp::nk::tmpl) {
        each_name_in_template(
            n.text, [&](std::string_view name) { mentions_[std::string{name}].push_back(inner); });
    }
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

bool compiler_impl::is_captured(std::string_view name) const {
    const interval where = frames_.back().captures;
    if (where.empty()) { return false; }
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

void compiler_impl::predeclare_locals(std::int32_t body) {
    // A PROGRAM'S top level counts too, not only a function's block. It
    // never used to: a classic script's top-level declarations are globals,
    // so there was nothing to pre-declare. A MODULE's are locals, and a
    // function declared there has to be a binding before `export { f }` can
    // find it - which it could not, and the importer was told the module
    // had no such export.
    if (body < 0 || (at(body).kind != vp::nk::block && at(body).kind != vp::nk::program)) {
        return;
    }
    const auto hoist = [this](std::string name, std::uint32_t initialized_at = 0) {
        if (name.empty() || find_local_entry(fn(), name) != nullptr) { return; }
        const std::uint16_t r = declare_local(name);
        fn().locals.back().initialized_at = initialized_at;
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
            continue;
        }
        const std::int32_t stmt =
            at(outer).kind == vp::nk::export_decl && at(outer).a >= 0 ? at(outer).a : outer;
        if (at(stmt).kind == vp::nk::var_decl) {
            // A `let`/`const`/`using` binding is in its TDZ until its
            // declarator has run: the offset past the declarator (or, with
            // no initialiser, past the statement) is when it is initialised.
            const bool lexical = at(stmt).text != "var";
            for (const std::int32_t d : kids(at(stmt))) {
                const std::uint32_t ready = lexical ? at(d).end : 0;
                if (at(d).b >= 0) { // a shape: hoist every name inside it
                    // ...with no static dead zone: `const {a, b = a} = o`
                    // initialises `a` before `b`'s default reads it, which a
                    // single offset cannot say.
                    std::vector<std::string> names;
                    pattern_names(at(d).b, names);
                    for (std::string & name : names) { hoist(std::move(name)); }
                } else {
                    hoist(std::string{at(d).text}, ready);
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
            for (const std::int32_t spec : kids(at(stmt))) { hoist(std::string{at(spec).text}); }
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
            hoist(std::string{at(stmt).text}, at(stmt).end);
        }
    }
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
                const bool fresh = find_local_entry(fn(), name) == nullptr;
                if (fresh) {
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

bool compiler_impl::was_predeclared(std::string_view name) const {
    for (const std::string & p : frames_.back().predeclared) {
        if (p == name) { return true; }
    }
    return false;
}

} // namespace ctbrowser::script::detail
