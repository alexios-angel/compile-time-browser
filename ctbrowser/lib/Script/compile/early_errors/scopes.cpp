//===- scopes.cpp - Bound names and scopes --------------------------------===//
//
// Early errors for bound names and scopes: BoundNames, LexicallyDeclaredNames,
// the statement-list check every scope-opening construct goes through, and
// the module-level export checks. The `checker` class is declared in
// checker.hpp; each file in this directory implements the members for one
// concern. Section numbers are ECMA-262's.
//
//===----------------------------------------------------------------------===//

#include "checker.hpp"

#include <algorithm>

#include <ctbrowser/core/algorithms.hpp>

namespace ctbrowser::script::detail::early {

/// Every name a binding position introduces: a plain identifier, or every
/// name inside a destructuring pattern. 8.2.1 BoundNames.
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

namespace {

/// IsStringWellFormedUnicode (6.1.4) of a string literal's value, read from
/// its lexeme: every lead surrogate (escaped as \uD83D or \u{D83D}, or a
/// lone one in the WTF-8 source) must be followed by a trail, and no trail
/// may stand alone. Escapes other than \u cannot make a surrogate.
[[nodiscard]] bool literal_well_formed(std::string_view lexeme) {
    if (lexeme.size() < 2) { return true; }
    const std::string_view body = lexeme.substr(1, lexeme.size() - 2);
    bool want_trail = false;
    const auto unit = [&](char32_t cp) {
        const bool lead = cp >= 0xD800 && cp <= 0xDBFF;
        const bool trail = cp >= 0xDC00 && cp <= 0xDFFF;
        if (want_trail != trail) { return false; }
        want_trail = lead;
        return true;
    };
    for (std::size_t i = 0; i < body.size();) {
        if (body[i] != '\\') {
            if (!unit(ctbrowser::decode_utf8(body, i))) { return false; }
            continue;
        }
        if (i + 1 >= body.size() || body[i + 1] != 'u') {
            if (want_trail) { return false; }
            i += 2;
            continue;
        }
        char32_t cp = 0;
        std::size_t k = i + 2;
        const bool braced = k < body.size() && body[k] == '{';
        if (braced) { ++k; }
        std::size_t digits = 0;
        for (; k < body.size() && ctbrowser::hex_value(body[k]) >= 0 && (braced || digits < 4);
             ++k, ++digits) {
            cp = cp * 16 + static_cast<char32_t>(ctbrowser::hex_value(body[k]));
            if (cp > 0x10FFFF) { return true; } // Not this rule's to refuse.
        }
        if (braced) { ++k; }
        i = k;
        if (!unit(cp)) { return false; }
    }
    return !want_trail;
}

} // namespace

/// The lexical declarations a statement list makes directly, not through a
/// nested block, which is a scope of its own. 8.2.5 LexicallyDeclaredNames.
/// `using` and `await using` declarations are recorded as let_: they are
/// lexical, and no rule here treats them differently.
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
            // A module's top level is lexical (16.2.1: LexicallyDeclaredNames
            // of a Module's ModuleItemList is TopLevelLexicallyDeclaredNames
            // plus its function declarations), which is the one place this
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

/// Checks one statement list with its own lexical scope, and returns the var
/// names it hands back to the scope above.
///
/// 14.2.1 (Block), 16.1.1 (Script) and 15.2.1 (FunctionBody) all say the
/// same two things, and this is where both are checked:
///   - LexicallyDeclaredNames must not contain a duplicate;
///   - LexicallyDeclaredNames and VarDeclaredNames must not intersect.
/// VarDeclaredNames reaches through nested blocks and stops at a function,
/// which is exactly what the return value carries upward.
///
/// `outer` is the FormalParameters of the function whose body this is, or a
/// catch parameter (15.2.1 and 14.15.1 state the same rule for both), or
/// null.
std::vector<binding> checker::check_list(std::span<const std::int32_t> stmts, list_kind kind,
                                         const std::vector<binding> * outer,
                                         const char * outer_what) {
    std::vector<binding> lex;
    lexical_names(stmts, kind, lex);

    // Duplicate lexical names. The one relaxation (B.3.2.4): in sloppy code
    // two plain function declarations of one name in a block are legal.
    // Strict code, or a generator or async function on either side, makes it
    // the duplicate 14.2.1 refuses.
    if (lex.size() > 1) {
        std::unordered_map<std::string_view, std::size_t> seen;
        seen.reserve(lex.size());
        for (std::size_t i = 0; i < lex.size(); ++i) {
            const auto [it, fresh] = seen.emplace(lex[i].name, i);
            if (fresh) { continue; }
            const binding & first = lex[it->second];
            // `c` holds the parser's function flags (bit 0 async, bit 1
            // generator); a plain function has none set.
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

/// 16.2.1, the two static semantics of a Module's ModuleItemList that no
/// other production has:
///   - ExportedNames must not contain a duplicate. `export { x }; export { x }`
///     and `export default x; export { y as default }` name one export twice,
///     and a module with two of a name cannot be linked.
///   - Every ExportedBinding must be declared by this module. `export { X }`
///     for an X the module never binds is a SyntaxError, not a lookup that
///     fails later.
/// `vars` is what check_list handed back for the top level: its `var`s and,
/// in a script, its function declarations.
void checker::check_module_items(std::int32_t root, const std::vector<binding> & vars) {
    const std::span<const std::int32_t> items = kids(at(root));
    std::vector<binding> declared = vars;
    lexical_names(items, list_kind::script, declared);
    // 16.2.1.1: the ModuleItemList's LexicallyDeclaredNames (imports and a
    // named default export among them) may not repeat, nor name a var. The
    // names lexical_names already compared are in `declared`; these two kinds
    // it cannot see are compared against them, and each other, as they join.
    const auto declare = [&](std::string_view name, std::int32_t node) {
        for (const binding & b : declared) {
            if (b.name == name) {
                report(quoted(name) + " has already been declared", node);
                break;
            }
        }
        declared.push_back(binding{name, binding_kind::const_, node});
    };
    for (const std::int32_t s : items) {
        const vp::node & item = at(s);
        // 16.2.2.1 / 16.2.3.1: a ModuleExportName written as a string must be
        // well-formed Unicode - `export { x as "\uD83D" }` names nothing any
        // importer could spell.
        if (item.kind == nk::import_decl || item.kind == nk::export_decl) {
            for (const std::int32_t spec : kids(item)) {
                for (const std::int32_t named : {spec, at(spec).a}) {
                    const std::string_view text = named >= 0 ? at(named).text : "";
                    if (!text.empty() && (text.front() == '"' || text.front() == '\'') &&
                        !literal_well_formed(text)) {
                        report("a module export name must be well-formed Unicode", named);
                    }
                }
            }
        }
        // An imported binding is a binding of this module too, and `export
        // { a }` may name one (16.2.3: ExportedBindings are resolved against
        // everything the module declares, imports included). It is an
        // ImportedBinding - a BindingIdentifier of strict code, so `eval`
        // and `arguments` are refused as any strict binding of them is.
        if (item.kind == nk::import_decl) {
            for (const std::int32_t spec : kids(item)) {
                check_strict_binding(at(spec).text, spec);
                declare(at(spec).text, spec);
            }
        }
        // `export default function f() {}` binds f as well as exporting
        // `default` (16.2.3.7: the HoistableDeclaration's BoundNames), and a
        // named `export default class C {}` binds C. The parser reads either
        // in expression position, where lexical_names mostly cannot see the
        // name - so it is declared here unless lexical_names already recorded
        // this very node, which would report it as its own duplicate.
        // `export default y` names an expression, which binds nothing.
        if (item.kind == nk::export_decl && item.c == 1 && item.a >= 0 &&
            !at(item.a).text.empty() &&
            (at(item.a).kind == nk::func_expr || at(item.a).kind == nk::class_decl) &&
            std::ranges::none_of(declared, [&](const binding & b) { return b.node == item.a; })) {
            declare(at(item.a).text, item.a);
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
            // Without `from` the local name must exist here; with one it is
            // the other module's business and is resolved at link time.
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

namespace {
// An identifier as the lexer left it may still spell its code points with
// `\uXXXX` or `\u{...}` escapes; the rules below are about the name, so
// `var \u0070ublic` in strict code is `public`, reserved (12.7.1.1: an
// escaped reserved word is still that word). ASCII is all a reserved word
// can be, so anything past it just stays as it was written.
std::string unescape_identifier(std::string_view raw) {
    std::string out;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '\\' || i + 1 >= raw.size() || raw[i + 1] != 'u') {
            out.push_back(raw[i]);
            continue;
        }
        std::size_t j = i + 2;
        const bool braced = j < raw.size() && raw[j] == '{';
        if (braced) { ++j; }
        std::uint32_t code = 0;
        std::size_t digits = 0;
        while (j < raw.size() && (braced ? raw[j] != '}' : digits < 4) &&
               ctbrowser::hex_value(raw[j]) >= 0) {
            code = code * 16 + static_cast<std::uint32_t>(ctbrowser::hex_value(raw[j]));
            ++j;
            ++digits;
        }
        if (braced && j < raw.size() && raw[j] == '}') { ++j; }
        if (digits == 0 || code >= 0x80) {
            out.append(raw.substr(i, j - i));
        } else {
            out.push_back(static_cast<char>(code));
        }
        i = j - 1;
    }
    return out;
}
} // namespace

void checker::check_strict_binding(std::string_view spelled, std::int32_t node, bool trusted) {
    const std::string decoded =
        spelled.find('\\') == std::string_view::npos ? std::string{} : unescape_identifier(spelled);
    const std::string_view name = decoded.empty() ? spelled : std::string_view{decoded};
    // A binding that is not an identifier at all: the parser takes whatever
    // token stands where a name should, so `var 1 = 1` and `function (1, 1)`
    // arrive as bindings named "1" (13.1 BindingIdentifier: IdentifierStart
    // is a letter, `$`, `_`, an escape or a non-ASCII code point).
    if (trusted && !name.empty()) {
        const auto first = static_cast<unsigned char>(name.front());
        if (!(std::isalpha(first) != 0 || first == '$' || first == '_' || first == '\\' ||
              first >= 0x80)) {
            report("unexpected token " + quoted(name) + " where a name was expected", node);
            return;
        }
    }
    // A binding named with a reserved word is one the parser took leniently
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
