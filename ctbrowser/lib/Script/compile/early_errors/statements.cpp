// early errors - statements: the walk over every statement form, the
// declaration-as-body rules, loops, switch, try, labels, break and continue.
//
// One of six files carved out of a 1,476-line compile/early_errors.cpp on
// 2026-09-08 - the checker was one class with every member inline, as
// compile.cpp was before it became compile/. The class is declared in
// checker.hpp beside this, in ctbrowser::script::detail::early; the bodies are
// where they were, in the file that owns the concern. early_errors.hpp, the
// public face of the pass, did not change.

#include "checker.hpp"

namespace ctbrowser::script::detail::early {

namespace {

[[nodiscard]] bool is_iteration(nk kind) {
    return kind == nk::for_stmt || kind == nk::forof_stmt || kind == nk::while_stmt ||
           kind == nk::do_stmt;
}

} // namespace

// --- statements ------------------------------------------------------------

void checker::walk_statement(std::int32_t idx, list_kind kind, std::vector<binding> & vars) {
    if (idx < 0 || depth_ >= max_depth) { return; }
    const deeper nesting{depth_};
    const vp::node & n = at(idx);
    switch (n.kind) {
    case nk::empty: return;

    case nk::export_decl:
        // The wrapper contributes nothing of its own; its declaration does.
        walk_statement(n.a, kind, vars);
        return;

    case nk::var_decl: check_declaration(idx, vars); return;

    case nk::block: {
        std::vector<binding> inner = check_list(kids(n), list_kind::block, nullptr, "");
        vars.insert(vars.end(), inner.begin(), inner.end());
        return;
    }

    case nk::expr_stmt: walk_expression(n.a); return;
    case nk::throw_stmt: walk_expression(n.a); return;

    case nk::return_stmt:
        // NOT CHECKED, AND THIS IS THE ONE DELIBERATE DEVIATION IN THIS
        // FILE. A Script's StatementList is parsed with [~Return], so
        // 16.1.1 makes a top-level `return` a SyntaxError - and it is worth
        // about fifteen tests in test262.
        //
        // It is also this engine's EMBEDDING CONTRACT. `run_result::returned`
        // is the value a compiled program hands back, and the way a program
        // hands one back is `return` at its top level: `unittests/js`
        // compiles `return (expr);` for every expression it checks,
        // `unittests/unit/script_gc` has fourteen of them and
        // `test/corpus/babylon` another. Refusing it would refuse the
        // engine's own API to buy fifteen tests.
        //
        // The honest fix is a script_kind that says "this source is a
        // function body", which every embedder would have to pass; that is
        // a change to callers this file does not own.
        walk_expression(n.a);
        return;

    case nk::if_stmt:
        // NESTED, so `nested` rather than `kind` - and that is Annex B,
        // not tidiness. `let f = 1; if (true) function f() {}` is legal
        // sloppy JavaScript: B.3.3.2 says a FunctionDeclaration that is the
        // whole body of an `if` is hoisted only when doing so "would not
        // produce any Early Errors", so it must not MAKE one. Passing the
        // nested kind is what stops its name being var-declared here, and
        // five annexB tests are exactly this shape.
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
        // Its NAME is this scope's business - lexical in a block, var at the
        // top of a script or a function body (8.2.6).
        if (!n.text.empty() && kind != list_kind::block) {
            vars.push_back(binding{n.text, binding_kind::function_, idx});
        }
        check_function(idx, frame_kind::function);
        return;

    case nk::class_decl: check_class(idx); return;

    default: walk_expression(idx); return;
    }
}

// A DECLARATION IS NOT A STATEMENT, and the grammar is where that is
// written rather than any early-error clause: the body of an `if`, a loop
// or a labelled statement is a Statement, and `let`, `const`, `class`, a
// generator and an async function are Declarations. `if (true) let x = 1;`
// does not parse in a conforming implementation.
//
// The exception is Annex B and it is narrow: B.3.3 admits a plain
// FunctionDeclaration as the body of an `if` clause, and B.3.2 as a
// LabelledItem, in sloppy code - which is all the code there is here. It
// does NOT admit one as the body of a loop, and it does not admit a
// generator or an async function anywhere.
// IS THERE A LINE TERMINATOR after this node's lexeme, before the next
// thing that is neither whitespace nor a comment? The one question this
// pass has to ask the SOURCE rather than the tree, because automatic
// semicolon insertion is not in the tree at all.
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
            return true; // a line comment runs to a line terminator
        }
        if (c == '/' && i + 1 < source_.size() && source_[i + 1] == '*') {
            const std::size_t end = source_.find("*/", i + 2);
            if (end == std::string_view::npos) { return false; }
            // A block comment CONTAINING a line terminator is one for the
            // purposes of ASI, which is the rule people forget.
            if (source_.substr(i, end - i).find('\n') != std::string_view::npos) { return true; }
            i = end + 2;
            continue;
        }
        return false;
    }
    return true; // end of input ends the line too
}

void checker::check_nested_declaration(std::int32_t idx, bool annex_b_function) {
    if (idx < 0) { return; }
    const vp::node & n = at(idx);
    if (n.kind == nk::var_decl && n.text != "var") {
        // `let` IS AN IDENTIFIER HERE WHEN A NEWLINE FOLLOWS IT.
        //
        // A Statement cannot be a Declaration, so in this position `let` is
        // not a keyword at all - it is an IdentifierReference, and
        // `if (false) let \n x = 1;` is two statements with a semicolon
        // inserted between them. Legal sloppy JavaScript, and twelve
        // test262 files (`let-identifier-with-newline`,
        // `let-block-with-newline`, in six directories) are exactly it.
        //
        // ctjs's parser has no ASI here and reads the declaration, so the
        // source is what has to be asked. `const` and `class` are reserved
        // words and get no such reading.
        if (n.text == "let" && newline_follows(idx, n.text.size())) { return; }
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
        const std::int32_t bits = n.c > 0 ? n.c : 0;
        if ((bits & 3) != 0) {
            report("a generator or async function declaration cannot be the body of a "
                   "statement; it needs a block",
                   idx);
        } else if (!annex_b_function) {
            report("a function declaration cannot be the body of a loop; it needs a block", idx);
        }
    }
}

// A loop's body, with the loop counted so `break` and `continue` inside it
// have somewhere to go.
void checker::walk_loop_body(std::int32_t body, std::vector<binding> & vars) {
    ++frames_.back().loops;
    walk_statement(body, nested, vars);
    --frames_.back().loops;
}

// `var` / `let` / `const`.
void checker::check_declaration(std::int32_t idx, std::vector<binding> & vars) {
    const vp::node & n = at(idx);
    const bool is_var = n.text == "var";
    const bool is_const = n.text == "const";
    for (const std::int32_t d : kids(n)) {
        const vp::node & decl = at(d);
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
        if (decl.b >= 0) { walk_pattern(decl.b); }
        walk_expression(decl.a);
    }
}

// `for (;;)`.
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

// `for (x in o)` and `for (x of xs)`.
//
// The parser does NOT keep which keyword declared the head - `let` and
// `var` both arrive as d == 0 - so the head-against-body rule that
// check_for applies cannot be applied here without guessing, and guessing
// would refuse `for (var x of xs) { var x; }`, which is legal. The head's
// own shape is still walked.
void checker::check_for_in_of(std::int32_t idx, std::vector<binding> & vars) {
    const vp::node & n = at(idx);
    const vp::node & target = at(n.a);
    if (target.b >= 0) { walk_pattern(target.b); }
    walk_expression(n.b);
    check_nested_declaration(n.c, false);
    walk_loop_body(n.c, vars);
}

// A switch's CaseBlock is ONE lexical scope spanning every clause
// (14.12.1), which is why `case 1: let x; case 2: let x;` is an error.
void checker::check_switch(std::int32_t idx, std::vector<binding> & vars) {
    const vp::node & n = at(idx);
    walk_expression(n.a);
    std::vector<std::int32_t> body;
    for (const std::int32_t clause : kids(n)) {
        walk_expression(at(clause).a);
        for (const std::int32_t s : kids(at(clause))) { body.push_back(s); }
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
            parameter.push_back(binding{clause.text, binding_kind::let_, n.b});
        }
        // 14.15.1: the catch parameter may not be redeclared lexically in
        // the block. A `var` of the same name IS allowed in sloppy mode
        // (B.3.4), so only the lexical half is checked - which is what
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

// A label ends in an iteration statement if, after unwrapping any labels of
// its own, that is what it names. `a: b: for (;;) continue a;` is legal
// precisely because of the unwrapping.
[[nodiscard]] bool checker::labels_iteration(std::int32_t idx) const {
    while (at(idx).kind == nk::labeled) { idx = at(idx).a; }
    return is_iteration(at(idx).kind);
}

void checker::check_labeled(std::int32_t idx, std::vector<binding> & vars) {
    const vp::node & n = at(idx);
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
    check_nested_declaration(n.a, true);
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
    // 13.8.1: the label of a `continue` must be on an ITERATION statement,
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
