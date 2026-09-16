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
        } else if (n.kind == nk::func_decl && !n.text.empty() &&
                   (kind == list_kind::block || (kind == list_kind::script && module_()))) {
            // A MODULE'S TOP LEVEL IS LEXICAL (16.2.1: LexicallyDeclaredNames
            // of a Module's ModuleItemList is TopLevelLexicallyDeclaredNames
            // PLUS its function declarations), which is the one place this
            // differs from a script: `function x() {} function x() {}` is a
            // redeclaration in a module and legal in a script.
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
// (`using` and `await using` are let_ for every rule here: lexical, and
// never a duplicate of anything.)

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
            // B.3.2.4: the relaxation is for two PLAIN function declarations
            // in sloppy code; a generator or an async function on either
            // side, or strict code, is the duplicate 14.2.1 refuses.
            const auto plain = [&](const binding & b) {
                return b.how == binding_kind::function_ && at(b.node).c <= 0;
            };
            if (plain(first) && plain(lex[i]) && !strict()) { continue; }
            report(quoted(lex[i].name) + " has already been declared in this scope; a " +
                       kind_word(lex[i].how) + " may not redeclare a " + kind_word(first.how),
                   lex[i].node);
        }
    }

    check_strict_bindings(lex);
    std::vector<binding> vars;
    for (const std::int32_t s : stmts) { walk_statement(s, kind, vars); }
    check_strict_bindings(vars);

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

// 16.2.1, the two static semantics of a Module's ModuleItemList that no
// other production has:
//   - ExportedNames must not contain a duplicate. `export { x }; export { x }`
//     and `export default x; export { y as default }` name one export twice,
//     and a module with two of a name cannot be linked.
//   - every ExportedBinding must be declared by this module. `export { X }`
//     for an X the module never binds is a SyntaxError, not a lookup that
//     fails later - and it was a compiler refusal here, which is a different
//     answer to a different question.
// `vars` is what check_list handed back for the top level: its `var`s and,
// in a script, its function declarations.
void checker::check_module_items(std::int32_t root, const std::vector<binding> & vars) {
    const std::span<const std::int32_t> items = kids(at(root));
    std::vector<binding> declared = vars;
    lexical_names(items, list_kind::script, declared);
    for (const std::int32_t s : items) {
        const vp::node & item = at(s);
        // An imported binding is a binding of this module too, and `export
        // { a }` may name one (16.2.3: ExportedBindings are resolved against
        // everything the module declares, imports included).
        if (item.kind == nk::import_decl) {
            for (const std::int32_t spec : kids(item)) {
                declared.push_back(binding{at(spec).text, binding_kind::const_, spec});
            }
        }
    }
    const auto is_declared = [&](std::string_view name) {
        for (const binding & b : declared) {
            if (b.name == name) { return true; }
        }
        return false;
    };

    std::vector<binding> exported;
    for (const std::int32_t s : items) {
        const vp::node & item = at(s);
        if (item.kind != nk::export_decl) { continue; }
        if (item.c == 1) { // `export default <anything>`
            exported.push_back(binding{"default", binding_kind::const_, s});
            continue;
        }
        if (item.c == 2) { // `export * from` names nothing; `export * as ns` names ns
            for (const std::int32_t spec : kids(item)) {
                exported.push_back(binding{at(spec).text, binding_kind::const_, spec});
            }
            continue;
        }
        if (item.a >= 0) { // `export const x = 1`, `export function f() {}`
            const vp::node & decl = at(item.a);
            if (decl.kind == nk::var_decl) {
                for (const std::int32_t d : kids(decl)) {
                    bound_names(d, binding_kind::const_, exported);
                }
            } else if (!decl.text.empty()) {
                exported.push_back(binding{decl.text, binding_kind::const_, item.a});
            }
            continue;
        }
        for (const std::int32_t spec : kids(item)) {
            const vp::node & one = at(spec);
            exported.push_back(
                binding{one.a >= 0 ? at(one.a).text : one.text, binding_kind::const_, spec});
            // WITHOUT `from` the local name must exist here; with one it is
            // the OTHER module's business and is resolved at link time.
            if (item.text.empty() && !is_declared(one.text)) {
                report(quoted(one.text) + " is exported and is not declared in this module", spec);
            }
        }
    }
    for (std::size_t i = 0; i < exported.size(); ++i) {
        for (std::size_t j = 0; j < i; ++j) {
            if (exported[j].name != exported[i].name) { continue; }
            report(quoted(exported[i].name) + " is exported twice", exported[i].node);
            break;
        }
    }
}

bool checker::has_use_strict_directive(std::int32_t body) const {
    for (const std::int32_t st : kids(at(body))) {
        const vp::node & stmt = at(st);
        if (stmt.kind != nk::expr_stmt || stmt.a < 0) { return false; }
        const vp::node & e = at(stmt.a);
        if (e.kind != nk::str) { return false; }
        if (e.text == "\"use strict\"" || e.text == "'use strict'") { return true; }
    }
    return false;
}

void checker::check_contextual_name(std::string_view name, std::int32_t node) {
    if (frames_.empty()) { return; }
    if (name == "await" && frames_.back().is_async) {
        report("`await` is not an identifier in an async function", node);
    } else if (name == "yield" && frames_.back().is_generator) {
        report("`yield` is not an identifier in a generator", node);
    }
}

bool checker::reserved_word(std::string_view name) {
    for (const std::string_view word :
         {"break",  "case",     "catch",  "class",  "const",  "continue",   "debugger", "default",
          "delete", "do",       "else",   "enum",   "export", "extends",    "false",    "finally",
          "for",    "function", "if",     "import", "in",     "instanceof", "new",      "null",
          "return", "super",    "switch", "this",   "throw",  "true",       "try",      "typeof",
          "var",    "void",     "while",  "with"}) {
        if (name == word) { return true; }
    }
    return false;
}

void checker::check_identifier_reference(std::string_view name, std::int32_t node, bool trusted) {
    if (trusted && reserved_word(name)) {
        report(quoted(name) + " is a reserved word and cannot be an identifier", node);
        return;
    }
    if (strict() && name != "eval" && name != "arguments") {
        check_strict_binding(name, node, trusted);
    } else {
        check_contextual_name(name, node);
    }
}

void checker::check_strict_binding(std::string_view name, std::int32_t node, bool trusted) {
    // A BINDING named with a reserved word is one the parser took leniently
    // (`var default`) or an escaped spelling; either is the SyntaxError, and
    // no valid program declares one.
    if (trusted && reserved_word(name)) {
        report(quoted(name) + " is a reserved word and cannot be an identifier", node);
        return;
    }
    check_contextual_name(name, node);
    if (!strict()) { return; }
    if (name == "eval" || name == "arguments") {
        report(quoted(name) + " may not be bound or assigned in strict mode code", node);
        return;
    }
    for (const std::string_view reserved : {"yield", "let", "static", "implements", "interface",
                                            "package", "private", "protected", "public"}) {
        if (name == reserved) {
            report(quoted(name) + " is a reserved word in strict mode code", node);
            return;
        }
    }
}

void checker::check_strict_bindings(const std::vector<binding> & names) {
    for (const binding & b : names) { check_strict_binding(b.name, b.node); }
}

} // namespace ctbrowser::script::detail::early
