// early errors - bound names and scopes: BoundNames, LexicallyDeclaredNames,
// and the one statement list check every scope-opening construct goes through.
//
// One of six files carved out of a 1,476-line compile/early_errors.cpp on
// 2026-09-08 - the checker was one class with every member inline, as
// compile.cpp was before it became compile/. The class is declared in
// checker.hpp beside this, in ctbrowser::script::detail::early; the bodies are
// where they were, in the file that owns the concern. early_errors.hpp, the
// public face of the pass, did not change.

#include "checker.hpp"

namespace ctbrowser::script::detail::early {

// --- scopes ---------------------------------------------------------------

// --- bound names ---------------------------------------------------------
// Every name a binding position introduces: a plain identifier, or every
// name inside a destructuring pattern. 8.2.1 BoundNames.
void checker::bound_names(std::int32_t idx, binding_kind how, std::vector<binding> & out) const {
    if (idx < 0) { return; }
    const vp::node & n = at(idx);
    switch (n.kind) {
    case nk::ident: out.push_back(binding{n.text, how, idx}); return;
    case nk::array_pattern:
        for (const std::int32_t element : kids(n)) { bound_names(element, how, out); }
        return;
    case nk::object_pattern:
        for (const std::int32_t entry : kids(n)) { bound_names(entry, how, out); }
        return;
    case nk::pattern_prop: bound_names(n.b, how, out); return;
    case nk::assign_pattern: bound_names(n.a, how, out); return;
    case nk::rest_element: bound_names(n.a, how, out); return;
    case nk::declarator:
        if (n.b >= 0) {
            bound_names(n.b, how, out);
        } else if (!n.text.empty()) {
            out.push_back(binding{n.text, how, idx});
        }
        return;
    case nk::param:
        if (n.b >= 0) {
            bound_names(n.b, how, out);
        } else if (!n.text.empty()) {
            out.push_back(binding{n.text, how, idx});
        }
        return;
    default: return;
    }
}

// The lexical declarations a statement list makes DIRECTLY - not through a
// nested block, which is a scope of its own. 8.2.5 LexicallyDeclaredNames.
void checker::lexical_names(std::span<const std::int32_t> stmts, list_kind kind,
                            std::vector<binding> & out) const {
    for (std::int32_t s : stmts) {
        // `export let x = 1` declares x exactly as `let x = 1` does; the
        // wrapper is bookkeeping for the loader.
        if (at(s).kind == nk::export_decl && at(s).a >= 0) { s = at(s).a; }
        const vp::node & n = at(s);
        if (n.kind == nk::var_decl && n.text != "var") {
            const binding_kind how = n.text == "const" ? binding_kind::const_ : binding_kind::let_;
            for (const std::int32_t d : kids(n)) { bound_names(d, how, out); }
        } else if (n.kind == nk::class_decl && !n.text.empty()) {
            out.push_back(binding{n.text, binding_kind::class_, s});
        } else if (n.kind == nk::func_decl && !n.text.empty() && kind == list_kind::block) {
            out.push_back(binding{n.text, binding_kind::function_, s});
        }
    }
}

[[nodiscard]] const char * checker::kind_word(binding_kind how) {
    switch (how) {
    case binding_kind::var: return "var";
    case binding_kind::let_: return "let";
    case binding_kind::const_: return "const";
    case binding_kind::class_: return "class";
    case binding_kind::function_: return "function";
    }
    return "declaration";
}

// ONE STATEMENT LIST, WITH ITS OWN LEXICAL SCOPE, and the var names it
// hands back to the scope above.
//
// 14.2.1 (Block), 16.1.1 (Script) and 15.2.1 (FunctionBody) all say the
// same two things, and this is where both are checked:
//   - LexicallyDeclaredNames must not contain a duplicate;
//   - LexicallyDeclaredNames and VarDeclaredNames must not intersect.
// VarDeclaredNames reaches through nested blocks and stops at a function,
// which is exactly what the return value carries upward.
//
// `outer` is the FormalParameters of the function whose body this is, or a
// catch parameter (15.2.1 and 14.15.1 state the same rule for both), or
// null.
std::vector<binding> checker::check_list(std::span<const std::int32_t> stmts, list_kind kind,
                                         const std::vector<binding> * outer,
                                         const char * outer_what) {
    std::vector<binding> lex;
    lexical_names(stmts, kind, lex);

    // Duplicate lexical names. The one relaxation: in sloppy mode two
    // FunctionDeclarations of one name in a block are legal (B.3.2.1), and
    // this engine has no strict mode - so a duplicate is only refused when
    // at least one side is a let, const or class.
    if (lex.size() > 1) {
        std::unordered_map<std::string_view, std::size_t> seen;
        seen.reserve(lex.size());
        for (std::size_t i = 0; i < lex.size(); ++i) {
            const auto [it, fresh] = seen.emplace(lex[i].name, i);
            if (fresh) { continue; }
            const binding & first = lex[it->second];
            if (first.how == binding_kind::function_ && lex[i].how == binding_kind::function_) {
                continue;
            }
            report(quoted(lex[i].name) + " has already been declared in this scope; a " +
                       kind_word(lex[i].how) + " may not redeclare a " + kind_word(first.how),
                   lex[i].node);
        }
    }

    std::vector<binding> vars;
    for (const std::int32_t s : stmts) { walk_statement(s, kind, vars); }

    if (!lex.empty() && !vars.empty()) {
        std::unordered_map<std::string_view, std::size_t> lexical;
        lexical.reserve(lex.size());
        for (std::size_t i = 0; i < lex.size(); ++i) { lexical.emplace(lex[i].name, i); }
        for (const binding & v : vars) {
            const auto it = lexical.find(v.name);
            if (it == lexical.end()) { continue; }
            report(quoted(v.name) + " is declared with var and with " +
                       kind_word(lex[it->second].how) + " in the same scope",
                   v.node);
        }
    }

    // 15.2.1: a parameter name may not be redeclared by a lexical
    // declaration at the top of the body. 14.15.1 says it of a catch
    // parameter and its block.
    if (outer != nullptr && !lex.empty()) {
        std::unordered_set<std::string_view> lexical;
        lexical.reserve(lex.size());
        for (const binding & b : lex) { lexical.insert(b.name); }
        for (const binding & p : *outer) {
            if (lexical.count(p.name) == 0) { continue; }
            report(quoted(p.name) + " is a " + outer_what + " and cannot be redeclared here",
                   p.node);
        }
    }
    return vars;
}

} // namespace ctbrowser::script::detail::early
