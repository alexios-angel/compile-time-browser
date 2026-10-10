//===- statements.cpp - Statement early errors ----------------------------===//
//
// Early errors for statements: the walk over every statement form, the rule
// that a declaration cannot be a statement body, loops, switch, try, labels,
// break and continue. The `checker` class is declared in checker.hpp; the
// pass's public entry point is early_errors.hpp. Section numbers are
// ECMA-262's.
//
//===----------------------------------------------------------------------===//

#include "checker.hpp"

namespace ctbrowser::script::detail::early {

namespace {

[[nodiscard]] bool is_iteration(nk kind) {
    return kind == nk::for_stmt || kind == nk::forof_stmt || kind == nk::while_stmt ||
           kind == nk::do_stmt;
}

/// The first character of the next token at or after `from`: white space, line
/// terminators and comments skipped. '\0' at the end of the source or inside
/// an unterminated comment.
[[nodiscard]] char next_token_char(std::string_view source, std::size_t from) {
    for (std::size_t i = from; i < source.size();) {
        const char c = source[i];
        if (c == ' ' || c == '\t' || c == '\v' || c == '\f' || c == '\n' || c == '\r') {
            ++i;
            continue;
        }
        if (c == '/' && i + 1 < source.size() && source[i + 1] == '/') {
            i = source.find('\n', i);
            if (i == std::string_view::npos) { return '\0'; }
            continue;
        }
        if (c == '/' && i + 1 < source.size() && source[i + 1] == '*') {
            const std::size_t end = source.find("*/", i + 2);
            if (end == std::string_view::npos) { return '\0'; }
            i = end + 2;
            continue;
        }
        return c;
    }
    return '\0';
}

} // namespace

void checker::walk_statement(std::int32_t idx, list_kind kind, std::vector<binding> & vars) {
    if (idx < 0 || depth_ >= max_depth) { return; }
    const deeper nesting{depth_};
    const vp::node & n = at(idx);
    switch (n.kind) {
    case nk::empty: return;

    case nk::import_decl:
    case nk::export_decl:
        // 16.2.1: an ImportDeclaration and an ExportDeclaration are
        // ModuleItems. They are not Statements, so neither is in the grammar
        // inside a block, a clause or a function body, and a classic Script's
        // grammar has neither at any depth: `{ export default null; }` and
        // `() => { import v from "./m.js"; }` are SyntaxErrors. The parser reads them wherever a
        // statement may stand, so the position is this pass's to refuse.
        // `list_kind::script` is only ever the program's own statement list
        // (see run()), so anything else is nested in something.
        if (!module_() || kind != list_kind::script) {
            report(std::string{n.kind == nk::import_decl ? "`import`" : "`export`"} +
                       (module_() ? " is only allowed at the top level of a module"
                                  : " is only allowed in a module, not in a script"),
                   idx);
        }
        // `export default function () {}()` (16.2.3): after `default` a
        // `function` or `class` is a declaration, which ends at its `}`, so
        // a call, member or operator after it is the SyntaxError, where the
        // parser read one expression. Found as the leftmost operand being a
        // function or class that no `(` stands in front of.
        // Only a continuation that cannot start a new statement is an error:
        // `(x)`, `[a]`, a template, `+1`, `-1` and `/re/` after the `}` begin
        // a valid next statement.
        if (n.kind == nk::export_decl && n.c == 1 && n.a >= 0) {
            std::int32_t left = n.a;
            std::int32_t after = -1; // The first continuation past the declaration.
            while (at(left).a >= 0 && (at(left).kind == nk::call || at(left).kind == nk::member ||
                                       at(left).kind == nk::index || at(left).kind == nk::binary ||
                                       at(left).kind == nk::tagged || at(left).kind == nk::seq)) {
                after = left;
                left = at(left).a;
            }
            const vp::node & first = at(left);
            const bool cannot_start = after >= 0 && [&] {
                const vp::node & next = at(after);
                return (next.kind == nk::call && kids(next).empty()) || next.kind == nk::member ||
                       next.kind == nk::seq ||
                       (next.kind == nk::binary && next.text != "+" && next.text != "-" &&
                        next.text != "/");
            }();
            if (cannot_start && (first.kind == nk::func_expr || first.kind == nk::class_decl)) {
                const std::size_t where = offset_of(left);
                if (where != early_error::nowhere && where > 0 &&
                    !after_open_paren(source_, where)) {
                    report("a default-exported declaration ends at its `}`", idx);
                }
            }
        }
        // The wrapper contributes nothing of its own; its declaration does.
        if (n.kind == nk::export_decl) { walk_statement(n.a, kind, vars); }
        return;

    case nk::var_decl: check_declaration(idx, vars); return;

    case nk::block: {
        std::vector<binding> inner = check_list(kids(n), list_kind::block, nullptr, "");
        vars.insert(vars.end(), inner.begin(), inner.end());
        return;
    }

    case nk::expr_stmt:
        // A statement that is one reserved word (a `catch` or a `finally`
        // with no `try` before it) is the parser's leniency in expression
        // position reaching the top of a statement, where no valid program
        // puts a keyword the statement parser did not take.
        if (n.a >= 0 && at(n.a).kind == nk::ident && reserved_word(at(n.a).text)) {
            report(quoted(at(n.a).text) + " cannot start a statement here", n.a);
            return;
        }
        walk_expression(n.a);
        return;
    case nk::throw_stmt:
        // 14.14: `throw [no LineTerminator here] Expression`. ASI cannot end
        // a throw early, so `throw` then a line break is the SyntaxError,
        // which the parser read as `throw` of the next line's expression.
        // Judged only when the operand is a single token, whose lexeme is
        // where the expression starts (a binary node's is its operator).
        if (const vp::node & operand = at(n.a);
            operand.kind == nk::num || operand.kind == nk::str || operand.kind == nk::ident ||
            operand.kind == nk::true_lit || operand.kind == nk::false_lit ||
            operand.kind == nk::null_lit || operand.kind == nk::this_lit) {
            if (const std::size_t where = offset_of(n.a); where != early_error::nowhere) {
                // Back over the white space to the token before, which must be
                // the `throw` itself, or nothing is concluded (`throw (\n x)`).
                std::size_t i = where;
                bool line_break = false;
                while (i > 0 && (source_[i - 1] == ' ' || source_[i - 1] == '\t' ||
                                 source_[i - 1] == '\n' || source_[i - 1] == '\r')) {
                    line_break = line_break || source_[i - 1] == '\n' || source_[i - 1] == '\r';
                    --i;
                }
                const auto word_char = [](char c) {
                    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '$';
                };
                if (line_break && i >= 5 && source_.substr(i - 5, 5) == "throw" &&
                    (i == 5 || !word_char(source_[i - 6]))) {
                    report("a line break may not follow `throw`", n.a);
                }
            }
        }
        walk_expression(n.a);
        return;

    case nk::return_stmt:
        // A top-level `return` is allowed by default, which deliberately
        // deviates from 16.1.1: it is how an embedded program hands back its
        // result (`run_result::returned`), and the unit tests rely on it. A
        // caller whose source really is a Script compiles it with
        // compiler::compile_script (`script_goal_`), and then it is refused.
        //
        // A static block is [~Return] (15.7.1) and is not the top level.
        if (frames_.back().what == frame_kind::static_block) {
            report("`return` is not allowed in a class static block", idx);
        }
        // Nor is an eval's top level: its source is a Script from script,
        // not an embedder's program, so the default above does not apply.
        // Nor is a module's (16.2.1: a ModuleItemList is [~Return]).
        if ((eval_ != nullptr || module_() || script_goal_) && frames_.size() == 1) {
            report("`return` outside a function", idx);
        }
        walk_expression(n.a);
        return;

    case nk::if_stmt:
        // The branches are walked with `nested` rather than `kind`, for
        // Annex B: `let f = 1; if (true) function f() {}` is legal sloppy
        // JavaScript. B.3.3.2 hoists a FunctionDeclaration that is the whole
        // body of an `if` only when doing so "would not produce any Early
        // Errors", so it must not make one. Passing the nested kind stops its
        // name being var-declared here.
        walk_expression(n.a);
        check_nested_declaration(n.b, true);
        check_nested_declaration(n.c, true);
        walk_statement(n.b, nested, vars);
        walk_statement(n.c, nested, vars);
        return;

    case nk::while_stmt:
        walk_expression(n.a);
        check_nested_declaration(n.b, false);
        walk_loop_body(n.b, vars);
        return;

    // 14.11.1: `with` is not allowed in strict mode code. Its body is a
    // Statement, so a declaration there is the same error an `if` body has.
    case nk::with_stmt:
        if (strict()) { report("`with` is not allowed in strict mode code", idx); }
        walk_expression(n.a);
        check_nested_declaration(n.b, false);
        walk_statement(n.b, nested, vars);
        return;

    case nk::do_stmt:
        check_nested_declaration(n.a, false);
        walk_loop_body(n.a, vars);
        walk_expression(n.b);
        return;

    case nk::for_stmt: check_for(idx, kind, vars); return;
    case nk::forof_stmt: check_for_in_of(idx, vars); return;
    case nk::switch_stmt: check_switch(idx, vars); return;
    case nk::try_stmt: check_try(idx, vars); return;
    case nk::labeled: check_labeled(idx, vars); return;

    case nk::break_stmt: check_break(idx); return;
    case nk::continue_stmt: check_continue(idx); return;

    case nk::func_decl:
        // Its name is this scope's business: lexical in a block, var at the
        // top of a script or a function body (8.2.6).
        if (!n.text.empty() && kind != list_kind::block &&
            !(kind == list_kind::script && module_())) {
            vars.push_back(binding{n.text, binding_kind::function_, idx});
        }
        check_function(idx, frame_kind::function);
        return;

    case nk::class_decl: check_class(idx); return;

    default: walk_expression(idx); return;
    }
}

/// Whether a line terminator follows this node's lexeme, before the next
/// thing that is neither white space nor a comment. This pass asks the source
/// rather than the tree here, because automatic semicolon insertion is not
/// in the tree at all.
[[nodiscard]] bool checker::newline_follows(std::int32_t idx, std::size_t lexeme) const {
    const std::size_t where = offset_of(idx);
    if (where == early_error::nowhere) { return false; }
    for (std::size_t i = where + lexeme; i < source_.size();) {
        const char c = source_[i];
        if (c == '\n' || c == '\r') { return true; }
        if (c == ' ' || c == '\t' || c == '\v' || c == '\f') {
            ++i;
            continue;
        }
        if (c == '/' && i + 1 < source_.size() && source_[i + 1] == '/') {
            return true; // A line comment runs to a line terminator.
        }
        if (c == '/' && i + 1 < source_.size() && source_[i + 1] == '*') {
            const std::size_t end = source_.find("*/", i + 2);
            if (end == std::string_view::npos) { return false; }
            // A block comment containing a line terminator is one for the
            // purposes of ASI, which is the rule people forget.
            if (source_.substr(i, end - i).find('\n') != std::string_view::npos) { return true; }
            i = end + 2;
            continue;
        }
        return false;
    }
    return true; // End of input ends the line too.
}

/// A declaration is not a statement: the body of an `if`, a loop or a
/// labelled statement is a Statement, and `let`, `const`, `class`, generators
/// and async functions are Declarations, so `if (true) let x = 1;` is a
/// SyntaxError. Annex B is the narrow exception: in sloppy code only, B.3.3
/// allows a plain function declaration as an `if` body and B.3.2 as a
/// labelled item, never as a loop body, and never a generator or async
/// function.
void checker::check_nested_declaration(std::int32_t idx, bool annex_b_function,
                                       bool labelled_item) {
    if (idx < 0) { return; }
    // 14.6.1 / 14.7.1: IsLabelledFunction(Statement) - a function declaration
    // behind one or more labels is never the body of an `if` or a loop, and
    // B.3.3 does not reach it. A label's item is not such a body: `a: b:
    // function f() {}` is B.3.1's labelled function, which the inner label's
    // own check judges.
    std::int32_t item = idx;
    while (at(item).kind == nk::labeled) { item = at(item).a; }
    if (!labelled_item && item != idx && at(item).kind == nk::func_decl) {
        report("a labelled function declaration cannot be the body of a statement", item);
        return;
    }
    const vp::node & n = at(idx);
    if (n.kind == nk::var_decl && (n.text == "using" || n.text == "await using")) {
        report("a `using` declaration cannot be the body of a statement; it needs a block", idx);
        return;
    }
    if (n.kind == nk::var_decl && n.text != "var") {
        // `let` is an identifier here when a newline follows it.
        //
        // A Statement cannot be a Declaration, so in this position `let` is
        // not a keyword at all but an IdentifierReference, and
        // `if (false) let \n x = 1;` is two statements with a semicolon
        // inserted between them: legal sloppy JavaScript (test262's
        // `let-identifier-with-newline` and `let-block-with-newline`).
        //
        // ctjs's parser has no ASI here and reads the declaration, so the
        // source is what has to be asked. `const` and `class` are reserved
        // words and get no such reading.
        //
        // Except before `[`: an ExpressionStatement may not begin with the
        // two tokens `let [` (14.5, its lookahead restriction), so
        // `if (false) let \n [a] = 0;` is neither a declaration nor an
        // expression here; it is the SyntaxError, newline or not.
        if (n.text == "let" && newline_follows(idx, n.text.size())) {
            if (next_token_char(source_, offset_of(idx) + n.text.size()) == '[') {
                report("a statement cannot begin with `let [`", idx);
            }
            return;
        }
        report("a `" + std::string{n.text} +
                   "` declaration cannot be the body of a statement; it needs a block",
               idx);
        return;
    }
    if (n.kind == nk::class_decl) {
        report("a class declaration cannot be the body of a statement; it needs a block", idx);
        return;
    }
    if (n.kind == nk::func_decl) {
        // n.c holds the function's flags from the parser: bit 0 async,
        // bit 1 generator.
        const std::int32_t bits = n.c > 0 ? n.c : 0;
        if ((bits & 3) != 0) {
            report("a generator or async function declaration cannot be the body of a "
                   "statement; it needs a block",
                   idx);
        } else if (!annex_b_function) {
            report("a function declaration cannot be the body of a loop; it needs a block", idx);
        } else if (strict()) {
            // B.3.3 is sloppy-mode only: in strict code an `if` body is a
            // Statement and a declaration is not one.
            report("a function declaration cannot be the body of an `if` in strict mode code; "
                   "it needs a block",
                   idx);
        }
    }
}

/// A loop's body, with the loop counted so `break` and `continue` inside it
/// have somewhere to go.
void checker::walk_loop_body(std::int32_t body, std::vector<binding> & vars) {
    ++frames_.back().loops;
    walk_statement(body, nested, vars);
    --frames_.back().loops;
}

/// `var`, `let`, `const`, `using` and `await using` declarations.
void checker::check_declaration(std::int32_t idx, std::vector<binding> & vars) {
    const vp::node & n = at(idx);
    const bool is_var = n.text == "var";
    const bool is_const = n.text == "const";
    // `using` / `await using` (14.3.2.1 / 14.3.3.1): a name, never a pattern,
    // always with an initialiser, never named `let` (which the lexical
    // rule below already refuses); `await using` only where `await` is the
    // keyword: an async body, a module's top level, a static block.
    const bool is_using = n.text == "using" || n.text == "await using";
    if (n.text == "await using" && !(frames_.empty() || frames_.back().is_async)) {
        report("`await using` is only allowed in an async function or a module", idx);
    }
    for (const std::int32_t d : kids(n)) {
        const vp::node & decl = at(d);
        if (is_using) {
            if (decl.b >= 0) {
                report("a `using` declaration binds a name, not a pattern", d);
            } else if (decl.a < 0) {
                report(quoted(decl.text) + " is declared `using` with no initialiser", d);
            } else if (decl.text == "let") {
                report("`let` cannot be the name of a `using` declaration", d);
            }
        }
        // 14.3.1.1: "It is a Syntax Error if Initializer is not present and
        // IsConstantDeclaration of LexicalDeclaration is true." The only
        // `const` without one that the grammar allows is a for-in/of head,
        // which the parser gives a shape of its own and never reaches here.
        if (is_const && decl.a < 0) {
            std::vector<binding> names;
            bound_names(d, binding_kind::const_, names);
            report(names.empty()
                       ? std::string{"a `const` declaration must have an initialiser"}
                       : quoted(names.front().name) + " is declared `const` with no initialiser",
                   names.empty() ? d : names.front().node);
        }
        if (is_var) { bound_names(d, binding_kind::var, vars); }
        // 14.3.1.1: `let` is not a name a lexical declaration may bind, in
        // sloppy code too, where `let let = 1` would otherwise parse.
        if (!is_var) {
            std::vector<binding> names;
            bound_names(d, binding_kind::let_, names);
            for (const binding & b : names) {
                if (b.name == "let") {
                    report("`let` cannot be the name of a lexically declared binding", b.node);
                }
            }
        }
        if (decl.b >= 0) { walk_pattern(decl.b); }
        walk_expression(decl.a);
    }
}

/// `for (;;)`.
void checker::check_for(std::int32_t idx, list_kind kind, std::vector<binding> & vars) {
    const vp::node & n = at(idx);
    std::vector<binding> head;
    const vp::node & init = at(n.a);
    const bool lexical_head = init.kind == nk::var_decl && init.text != "var";
    if (lexical_head) {
        for (const std::int32_t d : kids(init)) {
            bound_names(d, init.text == "const" ? binding_kind::const_ : binding_kind::let_, head);
        }
        // 14.7.4.1: the head's own names must be distinct.
        if (head.size() > 1) {
            std::unordered_set<std::string_view> seen;
            for (const binding & b : head) {
                if (!seen.insert(b.name).second) {
                    report(quoted(b.name) + " is declared twice in the head of this `for`", b.node);
                }
            }
        }
    }
    if (n.a >= 0) { walk_statement(n.a, kind, vars); }
    walk_expression(n.b);
    walk_expression(n.c);

    std::vector<binding> body_vars;
    check_nested_declaration(n.d, false);
    ++frames_.back().loops;
    walk_statement(n.d, nested, body_vars);
    --frames_.back().loops;
    // 14.7.4.1: "It is a Syntax Error if any element of the BoundNames of
    // LexicalDeclaration also occurs in the VarDeclaredNames of Statement."
    if (!head.empty() && !body_vars.empty()) {
        std::unordered_set<std::string_view> declared;
        for (const binding & b : head) { declared.insert(b.name); }
        for (const binding & v : body_vars) {
            if (declared.count(v.name) != 0) {
                report(quoted(v.name) + " is declared in the head of this `for` and with var "
                                        "in its body",
                       v.node);
            }
        }
    }
    vars.insert(vars.end(), body_vars.begin(), body_vars.end());
}

/// `for (x in o)` and `for (x of xs)`.
///
/// `n.d` carries the head's flags from the ctjs parser: bit 0 `const`,
/// bit 1 no declaration (an assignment target), bit 2 `for await`, bit 3
/// `let`, bit 4 `using`, bit 5 `await using`. A lexical head (bits 0, 3, 4
/// or 5, mask 57) gets 14.7.5.1's checks: distinct names, none named `let`,
/// and none also var-declared in the body. A `var` head gets none of them,
/// so `for (var x of xs) { var x; }` stays legal.
void checker::check_for_in_of(std::int32_t idx, std::vector<binding> & vars) {
    const vp::node & n = at(idx);
    const vp::node & target = at(n.a);
    // 14.7.5.1: a head with nothing to declare (d bit 1) is a
    // LeftHandSideExpression whose AssignmentTargetType must be simple, or
    // an array/object literal re-read as a pattern; `for (f() of xs)` and
    // `for ((a, b) in o)` are errors.
    if ((n.d & 2) != 0 && target.b >= 0) {
        switch (at(target.b).kind) {
        case nk::ident:
        case nk::member:
        case nk::index:
        case nk::array:
        case nk::object:
        case nk::array_pattern:
        case nk::object_pattern: break;
        default:
            // ...or a call in sloppy code, allowed for web compatibility as an
            // assignment's is: each iteration evaluates it and throws the ReferenceError.
            if (web_compat_call(target.b)) { break; }
            report("the head of this `for` loop is not something a value can be assigned to",
                   target.b);
            break;
        }
    }
    if (target.b >= 0) {
        walk_pattern(target.b);
    } else {
        check_strict_binding(target.text, n.a);
    }
    // A declared head: its names are bindings (13.1.1 in strict code), and
    // for a lexical head 14.7.5.1 adds that they are
    // distinct, that none is `let`, and that none is also var-declared in
    // the body.
    std::vector<binding> head;
    // `for (using x of xs)` (d bit 4) / `for (await using x of xs)` (bit 5):
    // `of` only, a plain name, and the async form only where `await` is the
    // keyword (14.7.5.1).
    if ((n.d & 48) != 0) {
        if (n.text != "of") { report("a `using` declaration in a `for` head needs `of`", idx); }
        if (target.b >= 0) { report("a `using` declaration binds a name, not a pattern", n.a); }
        if ((n.d & 32) != 0 && !(frames_.empty() || frames_.back().is_async)) {
            report("`await using` is only allowed in an async function or a module", idx);
        }
    }
    if ((n.d & 2) == 0) {
        const bool lexical = (n.d & 57) != 0;
        bound_names(n.a, lexical ? binding_kind::let_ : binding_kind::var, head);
        check_strict_bindings(head);
        if (lexical) {
            std::unordered_set<std::string_view> seen;
            for (const binding & b : head) {
                if (b.name == "let") {
                    report("`let` cannot be the name of a lexically declared loop variable",
                           b.node);
                }
                if (!seen.insert(b.name).second) {
                    report(quoted(b.name) + " is declared twice in the head of this loop", b.node);
                }
            }
        } else {
            head.clear();
        }
    }
    walk_expression(n.b);
    check_nested_declaration(n.c, false);
    std::vector<binding> body_vars;
    walk_loop_body(n.c, body_vars);
    if (!head.empty()) {
        for (const binding & v : body_vars) {
            for (const binding & h : head) {
                if (h.name != v.name) { continue; }
                report(quoted(v.name) + " is declared in the head of this loop and with var in "
                                        "its body",
                       v.node);
                break;
            }
        }
    }
    vars.insert(vars.end(), body_vars.begin(), body_vars.end());
}

/// A switch's CaseBlock is one lexical scope spanning every clause
/// (14.12.1), which is why `case 1: let x; case 2: let x;` is an error.
void checker::check_switch(std::int32_t idx, std::vector<binding> & vars) {
    const vp::node & n = at(idx);
    walk_expression(n.a);
    std::vector<std::int32_t> body;
    bool has_default = false;
    for (const std::int32_t clause : kids(n)) {
        // 14.12.1: CaseBlock's grammar admits one DefaultClause, between the
        // two runs of CaseClauses. `default` is `d == 1` (ctjs starts `d` at
        // -1), or a clause with no test, as compile_switch reads it.
        if (at(clause).d == 1 || at(clause).a < 0) {
            if (has_default) { report("a `switch` may have only one `default` clause", clause); }
            has_default = true;
        }
        walk_expression(at(clause).a);
        for (const std::int32_t s : kids(at(clause))) {
            // A `using` is not a CaseClause's statement (14.3.2's grammar
            // puts UsingDeclaration in a block, a body or a module only).
            if (at(s).kind == nk::var_decl &&
                (at(s).text == "using" || at(s).text == "await using")) {
                report("a `using` declaration is not allowed directly in a `case` clause; it "
                       "needs a block",
                       s);
            }
            body.push_back(s);
        }
    }
    ++frames_.back().switches;
    std::vector<binding> inner = check_list(body, list_kind::block, nullptr, "");
    --frames_.back().switches;
    vars.insert(vars.end(), inner.begin(), inner.end());
}

void checker::check_try(std::int32_t idx, std::vector<binding> & vars) {
    const vp::node & n = at(idx);
    std::vector<binding> inner = check_list(kids(at(n.a)), list_kind::block, nullptr, "");
    vars.insert(vars.end(), inner.begin(), inner.end());
    if (n.b >= 0) {
        const vp::node & clause = at(n.b);
        std::vector<binding> parameter;
        if (!clause.text.empty()) {
            // 14.15: a CatchParameter is a BindingIdentifier or a pattern;
            // the parser takes any token there, `catch ("22")` included.
            if (clause.text.starts_with('#') || !identifier_start(clause.text)) {
                report("a catch parameter must be a name or a pattern", n.b);
            }
            parameter.push_back(binding{clause.text, binding_kind::let_, n.b});
            check_strict_binding(clause.text, n.b);
        } else if (clause.b >= 0) { // `catch ({message})`: a pattern's names.
            bound_names(clause.b, binding_kind::let_, parameter);
            check_strict_bindings(parameter);
            walk_pattern(clause.b);
            // 14.15.1: BoundNames of the CatchParameter has no duplicate.
            for (std::size_t i = 1; i < parameter.size(); ++i) {
                for (std::size_t j = 0; j < i; ++j) {
                    if (parameter[i].name != parameter[j].name) { continue; }
                    report(quoted(parameter[i].name) + " is bound twice by the catch parameter",
                           parameter[i].node);
                    break;
                }
            }
        }
        // 14.15.1: the catch parameter may not be redeclared lexically in
        // the block. A `var` of the same name is allowed in sloppy mode
        // (B.3.4), so only the lexical half is checked, which is what
        // passing it as `outer` does.
        std::vector<binding> caught =
            check_list(kids(at(clause.a)), list_kind::block, &parameter, "catch parameter");
        vars.insert(vars.end(), caught.begin(), caught.end());
    }
    if (n.c >= 0) {
        std::vector<binding> finally_vars =
            check_list(kids(at(n.c)), list_kind::block, nullptr, "");
        vars.insert(vars.end(), finally_vars.begin(), finally_vars.end());
    }
}

/// A label ends in an iteration statement if, after unwrapping any labels of
/// its own, that is what it names. `a: b: for (;;) continue a;` is legal
/// precisely because of the unwrapping.
[[nodiscard]] bool checker::labels_iteration(std::int32_t idx) const {
    while (at(idx).kind == nk::labeled) { idx = at(idx).a; }
    return is_iteration(at(idx).kind);
}

void checker::check_labeled(std::int32_t idx, std::vector<binding> & vars) {
    const vp::node & n = at(idx);
    // A LabelIdentifier follows the identifier rules of 13.1.1: no reserved
    // word (an escaped spelling is the one way one reaches here), `await`
    // not in an async body, `yield` not in a generator, the strict set not
    // in strict code.
    check_strict_binding(n.text, idx, escaped(idx));
    frame & f = frames_.back();
    // 14.13.1: "It is a Syntax Error if any source text is matched by this
    // production" when a LabelledItem is contained in a LabelledStatement
    // with the same label.
    for (const std::string_view existing : f.labels) {
        if (existing == n.text) {
            report("label " + quoted(n.text) + " is already in scope here", idx);
            break;
        }
    }
    check_nested_declaration(n.a, true, true);
    const bool names_a_loop = labels_iteration(n.a);
    f.labels.push_back(n.text);
    if (names_a_loop) { f.loop_labels.push_back(n.text); }
    walk_statement(n.a, nested, vars);
    frame & after = frames_.back();
    if (!after.labels.empty()) { after.labels.pop_back(); }
    if (names_a_loop && !after.loop_labels.empty()) { after.loop_labels.pop_back(); }
}

void checker::check_break(std::int32_t idx) {
    const vp::node & n = at(idx);
    const frame & f = frames_.back();
    if (n.text.empty()) {
        // 13.9.1 / 16.1.1: a `break` with no label must be nested in an
        // iteration statement or a switch.
        if (f.loops == 0 && f.switches == 0) {
            report("`break` with no enclosing loop or switch", idx);
        }
        return;
    }
    for (const std::string_view label : f.labels) {
        if (label == n.text) { return; }
    }
    report("`break " + std::string{n.text} + "` names a label that is not in scope", idx);
}

void checker::check_continue(std::int32_t idx) {
    const vp::node & n = at(idx);
    const frame & f = frames_.back();
    if (n.text.empty()) {
        if (f.loops == 0) { report("`continue` with no enclosing loop", idx); }
        return;
    }
    // 13.8.1: the label of a `continue` must be on an iteration statement,
    // which is the difference between it and `break`.
    for (const std::string_view label : f.loop_labels) {
        if (label == n.text) { return; }
    }
    for (const std::string_view label : f.labels) {
        if (label == n.text) {
            report("`continue " + std::string{n.text} + "` names a label that is not on a loop",
                   idx);
            return;
        }
    }
    report("`continue " + std::string{n.text} + "` names a label that is not in scope", idx);
}

} // namespace ctbrowser::script::detail::early
