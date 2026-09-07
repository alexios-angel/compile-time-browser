// The early errors - one walk over the AST, before a single instruction is
// emitted. The header beside this says what an early error IS and which
// families are deliberately absent; this file is the rules.
//
// EVERY RULE HERE CITES ITS CLAUSE, and that is not decoration. The cost of a
// false positive is not a wrong answer, it is a page that does not run: this
// engine compiles p5.js, Phaser, Babylon.js and Bootstrap out of vendor/ as
// ratchet tests, and any one of them refusing to compile is a red suite. So a
// construct is refused here only where the specification says it must be, and
// where the rule needs strict mode to bite it is NOT refused - see the header.
//
// The walk is structural rather than generic. A generic walk over a node's four
// fixed slots cannot be used unaccompanied, because the parser reuses `c` and
// `d` as BITFIELDS on the kinds that carry flags - `update` keeps prefix/postfix
// in `b`, which as a node index is node 1 - so `slots()` below says which slots
// of which kinds are really children, and every construct that opens a SCOPE is
// handled by name.

#include "early_errors.hpp"

#include <array>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ctbrowser::script::detail {
namespace {

namespace vp = ctjs::vp;
using vp::nk;

// HOW A NAME WAS INTRODUCED. The distinction that matters is lexical
// (let/const/class, and a function declaration inside a BLOCK) against var,
// because every redeclaration rule is stated between those two sets.
enum class binding_kind : std::uint8_t {
    var,
    let_,
    const_,
    class_,
    function_
};

struct binding {
    std::string_view name;
    binding_kind how = binding_kind::var;
    std::int32_t node = -1;
};

// WHAT A STATEMENT LIST IS, which decides where a function declaration's name
// goes. At the top level of a script or a function body a FunctionDeclaration
// is VAR-scoped (TopLevelVarDeclaredNames, 8.2.6); inside a block or a case
// block it is LEXICALLY scoped (14.2.1). The whole of the redeclaration
// behaviour follows from that one difference.
enum class list_kind : std::uint8_t {
    script,
    function_body,
    block
};

// The kind a statement NESTED inside another statement is walked with. Only
// one thing turns on it - whether a function declaration's name is
// var-declared here - and a declaration that is the whole body of an `if` or a
// loop declares nothing this pass may draw a conclusion from.
inline constexpr list_kind nested = list_kind::block;

// THE ENCLOSING FUNCTION-SHAPED THING. `return`, `break`, `continue`, a label,
// `new.target` and `super` are all answered against this stack and nothing
// else - which is what makes them stop at a function boundary, as the
// specification's parameterised grammar does.
enum class frame_kind : std::uint8_t {
    script,     // the top level: no return, no new.target, no super
    function,   // function declaration or expression
    arrow,      // transparent to `new.target`, `super` and `this`
    method,     // a method, accessor or constructor: has a home object
    field_init, // a class field initialiser - a function of its own
};

struct frame {
    frame_kind what = frame_kind::script;
    std::vector<std::string_view> labels;      // every label in scope in this frame
    std::vector<std::string_view> loop_labels; // ...that names an iteration statement
    int loops = 0;
    int switches = 0;
    // MAY `super(...)` APPEAR HERE? Exactly one thing may say yes: the
    // constructor of a class that has a ClassHeritage. 15.7.1 puts it two ways
    // and they come to the same rule - a MethodDefinition that is not that
    // constructor may not Contain a SuperCall, and a class with no `extends`
    // may not either.
    bool super_call_ok = false;
};

[[nodiscard]] bool is_iteration(nk kind) {
    return kind == nk::for_stmt || kind == nk::forof_stmt || kind == nk::while_stmt ||
           kind == nk::do_stmt;
}

// The kinds walk_statement handles by name. Anything else in statement position
// is an expression - and anything in EXPRESSION position that turns out to be
// one of these is a routing mistake, so walk_expression sends it back rather
// than walking a block as if it were an operand.
[[nodiscard]] bool is_statement(nk kind) {
    switch (kind) {
    case nk::block:
    case nk::var_decl:
    case nk::empty:
    case nk::expr_stmt:
    case nk::if_stmt:
    case nk::for_stmt:
    case nk::forof_stmt:
    case nk::while_stmt:
    case nk::do_stmt:
    case nk::return_stmt:
    case nk::break_stmt:
    case nk::continue_stmt:
    case nk::throw_stmt:
    case nk::labeled:
    case nk::try_stmt:
    case nk::switch_stmt:
    case nk::func_decl: return true;
    default: return false;
    }
}

// WHICH OF A NODE'S FOUR FIXED SLOTS ARE REALLY CHILDREN. The same table
// compiler_impl::child_slots keeps, plus `update` - whose `b` is 1 for a prefix
// operator and 0 for a postfix one, and is a NODE INDEX to anything that does
// not know that.
[[nodiscard]] std::array<std::int32_t, 4> slots(const vp::node & n) {
    switch (n.kind) {
    case nk::param:
    case nk::prop:
    case nk::pattern_prop:
    case nk::class_member: return {n.a, n.b, -1, -1};
    case nk::func_decl:
    case nk::func_expr:
    case nk::arrow: return {n.a, -1, -1, -1};
    case nk::update: return {n.a, -1, -1, -1};
    case nk::new_expr: return {n.a, -1, -1, -1};
    case nk::forof_stmt: return {n.a, n.b, n.c, -1};
    case nk::case_clause: return {n.a, -1, -1, -1};
    case nk::import_decl:
    case nk::import_meta: return {-1, -1, -1, -1};
    case nk::import_spec:
    case nk::export_decl:
    case nk::export_spec:
    case nk::dynamic_import: return {n.a, -1, -1, -1};
    default: return {n.a, n.b, n.c, n.d};
    }
}

class checker {
public:
    checker(const vp::ast & tree, std::string_view source) : ast_(tree), source_(source) {}

    void run() {
        frames_.push_back(frame{frame_kind::script, {}, {}, 0, 0, false});
        const vp::node & root = at(ast_.root);
        if (root.kind != nk::program) { return; }
        (void)check_list(kids(root), list_kind::script, nullptr, "");
        frames_.pop_back();
    }

    std::optional<early_error> result() const { return found_; }

private:
    // --- the AST, read the way the compiler reads it -------------------------
    [[nodiscard]] const vp::node & at(std::int32_t i) const {
        static const vp::node nothing{nk::empty, ""};
        if (i < 0 || static_cast<std::size_t>(i) >= ast_.nodes.size()) { return nothing; }
        return ast_.nodes[static_cast<std::size_t>(i)];
    }

    [[nodiscard]] std::span<const std::int32_t> kids(const vp::node & n) const {
        if (n.list < 0 || n.list_len <= 0) { return {}; }
        return std::span<const std::int32_t>{ast_.pool.data() + n.list,
                                             static_cast<std::size_t>(n.list_len)};
    }

    // WHERE A NODE WAS WRITTEN. `node::text` is a view INTO the source, so its
    // address minus the source's is the offset; a function additionally carries
    // a real span. A node with neither answers `nowhere` rather than 0, because
    // a wrong line number in a diagnostic costs more than no line number.
    [[nodiscard]] std::size_t offset_of(std::int32_t idx) const {
        const vp::node & n = at(idx);
        if (n.begin != 0) { return n.begin; }
        if (n.text.empty() || source_.empty()) { return early_error::nowhere; }
        const char * first = n.text.data();
        if (first < source_.data() || first >= source_.data() + source_.size()) {
            return early_error::nowhere;
        }
        return static_cast<std::size_t>(first - source_.data());
    }

    // THE FIRST ERROR IN SOURCE ORDER, not the first one found. The walk
    // collects a scope's declarations before it walks the scope's statements,
    // so "found first" and "written first" are not the same thing, and the one
    // a person wants to be told about is the one they can see at the top of
    // the file.
    void report(std::string message, std::int32_t node) {
        const std::size_t where = offset_of(node);
        if (cut_short(node, where)) { return; }
        if (found_ && found_->offset <= where) { return; }
        found_ = early_error{std::move(message), where};
    }

    // A NAME THE LEXER DID NOT FINISH READING, and the one place this pass has
    // to distrust its own input.
    //
    // ctjs's lexer does not implement `\u` escapes inside an identifier, so
    // `const implements = 6` arrives as `const impl` followed by
    // something else entirely - a declaration with no initialiser, which is an
    // early error about source text nobody wrote. Seven of test262's
    // future-reserved-words tests are exactly that shape.
    //
    // So: a lexeme with a backslash immediately after it was cut short, and
    // nothing may be concluded from it. That is a parser gap reported honestly
    // as a miss rather than laundered into a wrong refusal.
    [[nodiscard]] bool cut_short(std::int32_t node, std::size_t where) const {
        if (where == early_error::nowhere) { return false; }
        const std::size_t after = where + at(node).text.size();
        return after < source_.size() && source_[after] == '\\';
    }

    static std::string quoted(std::string_view name) { return "`" + std::string{name} + "`"; }

    // --- bound names ---------------------------------------------------------
    // Every name a binding position introduces: a plain identifier, or every
    // name inside a destructuring pattern. 8.2.1 BoundNames.
    void bound_names(std::int32_t idx, binding_kind how, std::vector<binding> & out) const {
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

    // --- scopes ---------------------------------------------------------------

    // The lexical declarations a statement list makes DIRECTLY - not through a
    // nested block, which is a scope of its own. 8.2.5 LexicallyDeclaredNames.
    void lexical_names(std::span<const std::int32_t> stmts, list_kind kind,
                       std::vector<binding> & out) const {
        for (std::int32_t s : stmts) {
            // `export let x = 1` declares x exactly as `let x = 1` does; the
            // wrapper is bookkeeping for the loader.
            if (at(s).kind == nk::export_decl && at(s).a >= 0) { s = at(s).a; }
            const vp::node & n = at(s);
            if (n.kind == nk::var_decl && n.text != "var") {
                const binding_kind how =
                    n.text == "const" ? binding_kind::const_ : binding_kind::let_;
                for (const std::int32_t d : kids(n)) { bound_names(d, how, out); }
            } else if (n.kind == nk::class_decl && !n.text.empty()) {
                out.push_back(binding{n.text, binding_kind::class_, s});
            } else if (n.kind == nk::func_decl && !n.text.empty() && kind == list_kind::block) {
                out.push_back(binding{n.text, binding_kind::function_, s});
            }
        }
    }

    [[nodiscard]] static const char * kind_word(binding_kind how) {
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
    std::vector<binding> check_list(std::span<const std::int32_t> stmts, list_kind kind,
                                    const std::vector<binding> * outer, const char * outer_what) {
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

    // --- statements ------------------------------------------------------------
    // HOW DEEP THIS WALK MAY GO, and why there is a limit at all.
    //
    // The walk recurses once per level of nesting, and ctjs's expression parser
    // does NOT - its Pratt loop is iterative - so a minifier's
    // `"a" + "b" + ... ` chain of fifty thousand terms parses into a tree fifty
    // thousand deep that nothing has recursed over yet. This pass would be the
    // first thing to try, and the first thing to overflow the C++ stack.
    //
    // The COMPILER recurses over the same shape immediately afterwards, with
    // three larger frames per term, so any input that overflows here was going
    // to take the process down a moment later - measured, this walk survives
    // 20,000 terms and dies before 50,000. The guard is not a fix for that; it
    // is so that a new pass is not the thing that appears to have broken a page
    // that was already too deep to compile. Past the limit the subtree is
    // simply not checked, which is a MISS and never a refusal.
    static constexpr int max_depth = 2000;

    class deeper {
    public:
        explicit deeper(int & at) : at_(at) { ++at_; }
        ~deeper() { --at_; }
        deeper(const deeper &) = delete;
        deeper & operator=(const deeper &) = delete;
        deeper(deeper &&) = delete;
        deeper & operator=(deeper &&) = delete;

    private:
        int & at_;
    };

    void walk_statement(std::int32_t idx, list_kind kind, std::vector<binding> & vars) {
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
    [[nodiscard]] bool newline_follows(std::int32_t idx, std::size_t lexeme) const {
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
                if (source_.substr(i, end - i).find('\n') != std::string_view::npos) {
                    return true;
                }
                i = end + 2;
                continue;
            }
            return false;
        }
        return true; // end of input ends the line too
    }

    void check_nested_declaration(std::int32_t idx, bool annex_b_function) {
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
                report("a function declaration cannot be the body of a loop; it needs a block",
                       idx);
            }
        }
    }

    // A loop's body, with the loop counted so `break` and `continue` inside it
    // have somewhere to go.
    void walk_loop_body(std::int32_t body, std::vector<binding> & vars) {
        ++frames_.back().loops;
        walk_statement(body, nested, vars);
        --frames_.back().loops;
    }

    // `var` / `let` / `const`.
    void check_declaration(std::int32_t idx, std::vector<binding> & vars) {
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
                report(names.empty() ? std::string{"a `const` declaration must have an initialiser"}
                                     : quoted(names.front().name) +
                                           " is declared `const` with no initialiser",
                       names.empty() ? d : names.front().node);
            }
            if (is_var) { bound_names(d, binding_kind::var, vars); }
            if (decl.b >= 0) { walk_pattern(decl.b); }
            walk_expression(decl.a);
        }
    }

    // `for (;;)`.
    void check_for(std::int32_t idx, list_kind kind, std::vector<binding> & vars) {
        const vp::node & n = at(idx);
        std::vector<binding> head;
        const vp::node & init = at(n.a);
        const bool lexical_head = init.kind == nk::var_decl && init.text != "var";
        if (lexical_head) {
            for (const std::int32_t d : kids(init)) {
                bound_names(d, init.text == "const" ? binding_kind::const_ : binding_kind::let_,
                            head);
            }
            // 14.7.4.1: the head's own names must be distinct.
            if (head.size() > 1) {
                std::unordered_set<std::string_view> seen;
                for (const binding & b : head) {
                    if (!seen.insert(b.name).second) {
                        report(quoted(b.name) + " is declared twice in the head of this `for`",
                               b.node);
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
    void check_for_in_of(std::int32_t idx, std::vector<binding> & vars) {
        const vp::node & n = at(idx);
        const vp::node & target = at(n.a);
        if (target.b >= 0) { walk_pattern(target.b); }
        walk_expression(n.b);
        check_nested_declaration(n.c, false);
        walk_loop_body(n.c, vars);
    }

    // A switch's CaseBlock is ONE lexical scope spanning every clause
    // (14.12.1), which is why `case 1: let x; case 2: let x;` is an error.
    void check_switch(std::int32_t idx, std::vector<binding> & vars) {
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

    void check_try(std::int32_t idx, std::vector<binding> & vars) {
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
    [[nodiscard]] bool labels_iteration(std::int32_t idx) const {
        while (at(idx).kind == nk::labeled) { idx = at(idx).a; }
        return is_iteration(at(idx).kind);
    }

    void check_labeled(std::int32_t idx, std::vector<binding> & vars) {
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

    void check_break(std::int32_t idx) {
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

    void check_continue(std::int32_t idx) {
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

    // --- functions --------------------------------------------------------------

    // Is this a simple parameter list - names only, no default, no rest, no
    // pattern? 15.1.3 IsSimpleParameterList, and the answer decides whether a
    // duplicate parameter name is an error at all.
    [[nodiscard]] bool simple_parameters(std::span<const std::int32_t> params) const {
        for (const std::int32_t p : params) {
            const vp::node & n = at(p);
            if (n.d == 1 || n.a >= 0 || n.b >= 0) { return false; }
        }
        return true;
    }

    void check_function(std::int32_t idx, frame_kind what, bool super_call_ok = false) {
        const vp::node & n = at(idx);
        const std::span<const std::int32_t> params = kids(n);

        std::vector<binding> names;
        for (const std::int32_t p : params) { bound_names(p, binding_kind::let_, names); }

        // 15.1.2, 15.2.1, 15.3.1: a duplicate parameter name is an error when
        // the list is not simple, and always for an arrow or a method. A
        // duplicate in a SIMPLE list is an error only in strict mode, which
        // this engine does not have - so `function f(a, a) {}` is accepted, as
        // sloppy JavaScript accepts it.
        const bool must_be_unique =
            what == frame_kind::arrow || what == frame_kind::method || !simple_parameters(params);
        if (must_be_unique && names.size() > 1) {
            std::unordered_set<std::string_view> seen;
            seen.reserve(names.size());
            for (const binding & b : names) {
                if (!seen.insert(b.name).second) {
                    report("parameter " + quoted(b.name) +
                               " is bound twice, which a list with a default, a rest element, a "
                               "pattern, an arrow or a method may not do",
                           b.node);
                }
            }
        }
        // 15.1.1: a rest parameter must be last.
        for (std::size_t i = 0; i + 1 < params.size(); ++i) {
            if (at(params[i]).d == 1) {
                report("a rest parameter must be the last one", params[i]);
                break;
            }
        }

        frames_.push_back(frame{what, {}, {}, 0, 0, super_call_ok});
        for (const std::int32_t p : params) {
            const vp::node & param = at(p);
            if (param.b >= 0) { walk_pattern(param.b); }
            walk_expression(param.a);
        }
        const vp::node & body = at(n.a);
        if (body.kind == nk::block) {
            (void)check_list(kids(body), list_kind::function_body, &names, "parameter");
        } else {
            // A concise arrow body: one expression, no declarations to check.
            walk_expression(n.a);
        }
        frames_.pop_back();
    }

    // --- classes -----------------------------------------------------------------

    // WHAT A CLASS BODY HAS ALREADY BOUND UNDER A PRIVATE NAME. 15.7.1 lets one
    // name appear twice and only twice: once as a getter and once as a setter,
    // both static or both not. Anything else is a duplicate.
    struct private_name {
        std::string_view name;
        std::size_t seen = 0;
        bool getter = false;
        bool setter = false;
        bool is_static = false;
        bool other = false; // a field or a method - anything that is not an accessor
    };

    void check_class(std::int32_t idx) {
        const vp::node & n = at(idx);
        walk_expression(n.a); // `extends <expr>`
        const bool derived = n.a >= 0;

        std::vector<private_name> privates;
        std::size_t constructors = 0;
        for (const std::int32_t m : kids(n)) {
            const vp::node & member = at(m);
            const bool is_static = (member.d & 1) != 0;
            const bool computed = (member.d & 2) != 0;
            const bool is_method = member.c == 1;
            const bool is_accessor = member.c == 2;
            const bool is_field = member.c == 0;
            // `c` ON A FUNCTION NODE IS -1 WHEN IT IS NEITHER async NOR a
            // generator, not 0: the parser only writes the field when one of
            // the bits is set. Masking -1 says both, which made every ordinary
            // `constructor` look like a generator.
            const vp::node & body = at(member.b);
            const std::int32_t bits = body.c > 0 ? body.c : 0;
            const bool is_generator = is_method && (bits & 2) != 0;
            const bool is_async = is_method && (bits & 1) != 0;

            // 15.7.1. A class body may define at most one constructor, and
            // "constructor" may not be a getter, a setter, a generator, an
            // async method or a field.
            if (!computed && !is_static && member.text == "constructor") {
                if (is_method && !is_generator && !is_async) {
                    ++constructors;
                    if (constructors > 1) {
                        report("a class may define only one `constructor`", m);
                    }
                } else if (is_accessor || is_generator || is_async) {
                    report("`constructor` may not be an accessor, a generator or async", m);
                } else if (is_field) {
                    report("a class field may not be named `constructor`", m);
                }
            }
            // 15.7.1: a static member may not be named `prototype`.
            if (!computed && is_static && member.text == "prototype") {
                report("a static class member may not be named `prototype`", m);
            }
            // 15.7.1: `#constructor` is not a private name a class may bind.
            if (!computed && member.text == "#constructor") {
                report("`#constructor` is not a name a class member may have", m);
            }
            // 15.7.1: PrivateBoundIdentifiers may not contain a duplicate,
            // unless the pair is one getter and one setter of the same
            // staticness - which is how a private accessor is written.
            if (!computed && member.text.starts_with('#')) {
                private_name * seen = nullptr;
                for (private_name & held : privates) {
                    if (held.name == member.text) { seen = &held; }
                }
                if (seen == nullptr) {
                    privates.push_back(
                        private_name{member.text, 0, false, false, is_static, false});
                    seen = &privates.back();
                }
                const bool getter = is_accessor && (member.d & 4) == 0;
                const bool setter = is_accessor && (member.d & 4) != 0;
                ++seen->seen;
                const bool pairs_up = seen->seen == 2 && seen->is_static == is_static &&
                                      !seen->other && !is_method && !is_field &&
                                      ((seen->getter && setter) || (seen->setter && getter));
                if (seen->seen > 1 && !pairs_up) {
                    report("the private name " + quoted(member.text) +
                               " is bound twice in this class",
                           m);
                }
                seen->getter = seen->getter || getter;
                seen->setter = seen->setter || setter;
                seen->other = seen->other || is_method || is_field;
            }

            if (computed) { walk_expression(member.a); }
            if (is_method || is_accessor) {
                // THE ONE PLACE `super(...)` IS ALLOWED: the constructor of a
                // class that has a heritage.
                const bool is_constructor =
                    is_method && !is_static && !computed && member.text == "constructor";
                check_function(member.b, frame_kind::method, derived && is_constructor);
            } else {
                frames_.push_back(frame{frame_kind::field_init, {}, {}, 0, 0, false});
                walk_expression(member.b);
                frames_.pop_back();
            }
        }
    }

    // --- expressions ---------------------------------------------------------------

    // A target a value may be assigned TO. 13.15.1 refuses everything else, and
    // "everything else" is most of the grammar: a literal, a call, a sequence,
    // an operator expression, an arrow, an optional chain.
    [[nodiscard]] bool simple_target(std::int32_t idx) const {
        switch (at(idx).kind) {
        case nk::ident:
        case nk::member:
        case nk::index: return true;
        default: return false;
        }
    }

    [[nodiscard]] static bool destructuring(nk kind) {
        return kind == nk::array || kind == nk::object || kind == nk::array_pattern ||
               kind == nk::object_pattern;
    }

    // A CALL TARGET, AND THE ONE PLACE THIS PASS IS NOT THE BEST ANSWER.
    //
    // "Runtime Errors for Function Call Assignment Targets" is a normative
    // OPTIONAL clause: an implementation that is a web browser may answer
    // ~web-compat~ for `f() = 1` in non-strict code and evaluate it to a
    // runtime ReferenceError instead of refusing the source. Five annexB tests
    // assert exactly that, and a browser is what this engine is.
    //
    // It is refused here anyway, because the alternative is not available:
    // `compiler_impl::prepare_reference` already fails outright on a call
    // target, so the whole script is refused either way and the only thing
    // this pass changes is WHICH kind of failure it is - a SyntaxError rather
    // than "the compiler does not implement this", which is the truer of the
    // two. Emitting the ReferenceError instead is a compiler change (evaluate
    // the call, throw, and do not touch the right-hand side) and is the right
    // follow-up; until then those five tests fail as they already did.
    void check_assignment(std::int32_t idx) {
        const vp::node & n = at(idx);
        const nk target = at(n.a).kind;
        // `=` also accepts an ArrayLiteral or ObjectLiteral, which is then
        // reinterpreted as a destructuring pattern (13.15.5). No other
        // operator does: `[a] += b` is an error.
        if (n.text == "=" && destructuring(target)) { return; }
        if (simple_target(n.a)) { return; }
        report("the left side of `" + std::string{n.text} +
                   "` is not something a value can be "
                   "assigned to",
               n.a >= 0 ? n.a : idx);
    }

    void check_update(std::int32_t idx) {
        const vp::node & n = at(idx);
        if (simple_target(n.a)) { return; }
        report("the operand of `" + std::string{n.text} +
                   "` is not something a value can be assigned to",
               n.a >= 0 ? n.a : idx);
    }

    // 13.2.5.1 with B.3.1: an object literal may not carry two plain-data
    // `__proto__` properties, because each of them would set the prototype.
    // A shorthand, a method, an accessor and a computed key are all excluded -
    // none of them is the prototype-setting form.
    // IS THIS PROPERTY'S NAME `__proto__`? Written plainly, or QUOTED: the rule
    // is about the PropName, and `'__proto__': null` names the same thing that
    // `__proto__: null` does. Only a COMPUTED key is exempt, because its name is
    // not known until it is evaluated.
    //
    // The parser routes a quoted key down the same slot a computed one uses -
    // `d` bit 0 set, `a` holding the literal - so the two are told apart by
    // what is in `a`. A key with an escape in it is left alone rather than
    // cooked here: missing one is a miss, and cooking it wrongly would be a
    // refusal of source nobody wrote.
    [[nodiscard]] bool names_proto(const vp::node & prop) const {
        if ((prop.d & 1) == 0) { return prop.text == "__proto__"; }
        const vp::node & key = at(prop.a);
        if (key.kind != nk::str || key.text.size() < 2) { return false; }
        if (bracketed(prop.a)) { return false; }
        const std::string_view inner = key.text.substr(1, key.text.size() - 2);
        return inner == "__proto__";
    }

    // IS THIS KEY IN BRACKETS? The parser gives a QUOTED key and a COMPUTED one
    // the same shape - `d` bit 0 set and `a` holding the expression - and for a
    // computed key that happens to be a string literal the two are identical in
    // the tree. `{ '__proto__': null }` sets the prototype and
    // `{ ['__proto__']: null }` defines an ordinary property, so the difference
    // decides whether the duplicate rule applies at all, and the only place it
    // survives is the source.
    [[nodiscard]] bool bracketed(std::int32_t key) const {
        const std::size_t where = offset_of(key);
        if (where == early_error::nowhere) { return false; }
        for (std::size_t i = where; i-- > 0;) {
            const char c = source_[i];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') {
                continue;
            }
            return c == '[';
        }
        return false;
    }

    void check_proto_duplicates(std::int32_t idx) {
        std::size_t seen = 0;
        std::int32_t second = -1;
        for (const std::int32_t p : kids(at(idx))) {
            const vp::node & prop = at(p);
            if (prop.kind != nk::prop) { continue; }
            const bool plain_data = prop.c != 1 && prop.c != 2 && prop.c != 3;
            if (!plain_data || !names_proto(prop)) { continue; }
            ++seen;
            if (seen == 2) { second = p; }
        }
        if (seen > 1) { report("an object literal may not set `__proto__` twice", second); }
    }

    // A RESERVED WORD USED AS AN IDENTIFIER IS *NOT* CHECKED, and this is the
    // one rule that was written, measured, and then TAKEN OUT AGAIN. It is
    // recorded here so it is not attempted a second time from the same
    // reasoning.
    //
    // 13.1.1 reserves `const`, `return`, `class` and twenty more in every
    // context, and ctjs's parser reads a keyword in expression position as a
    // plain name - which it HAS to, because `of`, `get`, `set`, `static`,
    // `async`, `let`, `await` and `yield` are contextual and `const of = 1` is
    // valid JavaScript. Refusing the unconditionally-reserved ones scored +52
    // negative parse tests and refused NOTHING in the 41,163 valid test262
    // files.
    //
    // It refused p5.js. The bundle contains
    //
    //     if (strandsContext._builtinGlobalsAccessorsInstalled) return
    //     const getRuntimeP5Instance = () => ...
    //
    // and this parser has no automatic semicolon insertion after a bare
    // `return`, so it reads the `const` as that return's OPERAND - an
    // identifier named `const`. The rule is therefore not a rule about the
    // language here, it is a rule about a parser gap, and any statement
    // following an argument-less `return` at the end of a line can be caught by
    // it. That is a page that does not load, and the whole point of this file
    // is not to be that.

    // A NUMERIC LITERAL'S OWN GRAMMAR, 12.9.3, read back off the lexeme.
    //
    // The lexer is deliberately total: it takes `0` followed by `x`, `o` or `b`
    // and then EVERY identifier character, and a decimal run of digits, dots
    // and underscores, without asking whether the result is a number. That is
    // the right shape for a lexer - `0o17` used to lex as `0` followed by the
    // identifier `o17` - and it leaves `0b2`, `0x`, `1__0` and `1.5n` as tokens
    // that parse and are not literals.
    //
    // WHAT IS NOT CHECKED: a legacy octal (`01`) and a non-octal decimal (`08`).
    // Both are legal sloppy JavaScript and only an error in strict mode, which
    // this engine does not have.
    void check_number(std::int32_t idx) {
        const std::string_view text = at(idx).text;
        if (text.empty()) { return; }
        const bool bigint = text.back() == 'n';
        const std::string_view body = bigint ? text.substr(0, text.size() - 1) : text;
        if (body.empty()) {
            report("`" + std::string{text} + "` is not a number", idx);
            return;
        }
        const auto decimal = [](char c) { return c >= '0' && c <= '9'; };
        if (body.size() >= 2 && body[0] == '0' &&
            (body[1] == 'x' || body[1] == 'X' || body[1] == 'o' || body[1] == 'O' ||
             body[1] == 'b' || body[1] == 'B')) {
            const char radix = body[1];
            const auto belongs = [radix, decimal](char c) {
                if (radix == 'b' || radix == 'B') { return c == '0' || c == '1'; }
                if (radix == 'o' || radix == 'O') { return c >= '0' && c <= '7'; }
                return decimal(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            };
            const std::string_view digits = body.substr(2);
            if (digits.empty()) {
                report("`" + std::string{text} + "` has no digits after its prefix", idx);
                return;
            }
            for (std::size_t i = 0; i < digits.size(); ++i) {
                // 12.9.3: a separator goes BETWEEN two digits and nowhere else.
                if (digits[i] == '_') {
                    if (i == 0 || i + 1 == digits.size() || !belongs(digits[i - 1]) ||
                        !belongs(digits[i + 1])) {
                        report("`" + std::string{text} +
                                   "` has a `_` that is not between two digits",
                               idx);
                        return;
                    }
                    continue;
                }
                if (!belongs(digits[i])) {
                    report("`" + std::string{text} + "` has a digit its radix does not have", idx);
                    return;
                }
            }
            return;
        }
        // A BigInt IS AN INTEGER (12.9.3): no fraction and no exponent, and no
        // leading zero - `01n` and `08n` are not legacy anything, they are
        // errors even in sloppy code.
        if (bigint) {
            if (body.find('.') != std::string_view::npos ||
                body.find('e') != std::string_view::npos ||
                body.find('E') != std::string_view::npos) {
                report("`" + std::string{text} + "` is not an integer, so it cannot be a BigInt",
                       idx);
                return;
            }
            if (body.size() > 1 && body[0] == '0') {
                report("`" + std::string{text} + "` has a leading zero, which a BigInt may not",
                       idx);
                return;
            }
        }
        // TWO DECIMAL POINTS ARE NOT CHECKED, and the reason is `0..toString(2)`
        // - which is legal JavaScript, is how a page calls a method on a
        // numeric literal, and arrives here as the single lexeme `0..` because
        // the lexer takes every dot it can. Four test262 files are that shape.
        // Refusing it would be refusing valid source to catch a case the parser
        // already fails on.
        // A LEGACY OCTAL OR NON-OCTAL DECIMAL TAKES NO SEPARATORS. `01` and `08`
        // are legal sloppy and `0_1` is not: the separator is only in the
        // modern productions.
        // `body[1] == '_'` COUNTS AS ONE. After a leading `0` the only things
        // the grammar admits are a radix prefix, a `.`, an exponent and a
        // legacy octal digit run - so `0_1` is a legacy literal with a
        // separator in it rather than a modern literal that happens to start
        // with a zero.
        const bool legacy =
            body.size() > 1 && body[0] == '0' && (decimal(body[1]) || body[1] == '_') &&
            body.find('.') == std::string_view::npos && body.find('e') == std::string_view::npos &&
            body.find('E') == std::string_view::npos;
        if (legacy && body.find('_') != std::string_view::npos) {
            report("`" + std::string{text} + "` is a legacy octal literal and may not use `_`",
                   idx);
            return;
        }
        for (std::size_t i = 0; i < body.size(); ++i) {
            if (body[i] != '_') { continue; }
            if (i == 0 || i + 1 == body.size() || !decimal(body[i - 1]) || !decimal(body[i + 1])) {
                report("`" + std::string{text} + "` has a `_` that is not between two digits", idx);
                return;
            }
        }
    }

    // 13.5.1.1: `delete` of a private member is an error wherever it appears -
    // no strict mode needed, unlike `delete` of a plain identifier.
    void check_delete(std::int32_t operand) {
        const vp::node & n = at(operand);
        if ((n.kind == nk::member || n.kind == nk::opt_member) && n.text.starts_with('#')) {
            report("`delete` of the private member " + quoted(n.text) + " is not allowed", operand);
        }
    }

    // The frame `new.target` and `super` are answered against: the nearest one
    // that is not an arrow, since an arrow has neither of its own.
    [[nodiscard]] const frame & enclosing_non_arrow_frame() const {
        for (std::size_t i = frames_.size(); i-- > 0;) {
            if (frames_[i].what != frame_kind::arrow) { return frames_[i]; }
        }
        return frames_.front();
    }

    [[nodiscard]] frame_kind enclosing_non_arrow() const {
        return enclosing_non_arrow_frame().what;
    }

    void walk_expression(std::int32_t idx) {
        if (idx < 0 || depth_ >= max_depth) { return; }
        const deeper nesting{depth_};
        const vp::node & n = at(idx);
        switch (n.kind) {
        case nk::assign:
            check_assignment(idx);
            if (n.text == "=" && destructuring(at(n.a).kind)) {
                walk_pattern(n.a);
            } else {
                walk_expression(n.a);
            }
            walk_expression(n.b);
            return;

        case nk::update:
            check_update(idx);
            walk_expression(n.a);
            return;

        case nk::unary:
            if (n.text == "delete") { check_delete(n.a); }
            walk_expression(n.a);
            return;

        case nk::arrow:
            // AN ARROW INHERITS `super`, so the constructor's permission has to
            // travel into it: `constructor() { const f = () => super(); }` is
            // legal and is how a derived class defers the call.
            check_function(idx, frame_kind::arrow, enclosing_non_arrow_frame().super_call_ok);
            return;
        case nk::func_expr:
        case nk::func_decl: check_function(idx, frame_kind::function); return;
        case nk::class_decl: check_class(idx); return;

        case nk::call:
            // `super(...)` IS NOT A CALL OF THE VALUE `super`. It is its own
            // production, and 15.7.1 admits it in exactly one place: the
            // constructor of a class with a heritage. Handled here rather than
            // in the `super_lit` arm below, which would otherwise report the
            // callee as a `super` outside a method.
            if (at(n.a).kind == nk::super_lit) {
                if (!enclosing_non_arrow_frame().super_call_ok) {
                    report("`super()` outside the constructor of a derived class", idx);
                }
                for (const std::int32_t argument : kids(n)) { walk_expression(argument); }
                return;
            }
            break;

        case nk::object:
            check_proto_duplicates(idx);
            for (const std::int32_t p : kids(n)) { walk_property(p); }
            return;

        case nk::num: check_number(idx); return;

        case nk::new_target:
            // 16.1.1: a Script may not contain `new.target`; the grammar only
            // admits it inside a function.
            if (enclosing_non_arrow() == frame_kind::script) {
                report("`new.target` outside a function", idx);
            }
            return;

        case nk::super_lit: {
            // 16.1.1 again, and 15.7.1: `super` needs a home object, which only
            // a method, an accessor, a constructor or a field initialiser has.
            const frame_kind where = enclosing_non_arrow();
            if (where == frame_kind::script || where == frame_kind::function) {
                report("`super` outside a method", idx);
            }
            return;
        }

        // A template's `${...}` holes are raw text inside one token; the
        // compiler parses them separately, so there is nothing here to walk.
        case nk::tmpl: return;

        default: break;
        }
        if (is_statement(n.kind)) {
            std::vector<binding> ignored;
            walk_statement(idx, list_kind::block, ignored);
            return;
        }
        for (const std::int32_t slot : slots(n)) { walk_expression(slot); }
        for (const std::int32_t k : kids(n)) { walk_expression(k); }
    }

    // One entry of an object literal. A method or an accessor is a function
    // with a home object; a plain value is an expression.
    void walk_property(std::int32_t idx) {
        const vp::node & n = at(idx);
        if (n.kind != nk::prop) {
            walk_expression(idx);
            return;
        }
        if ((n.d & 1) != 0) { walk_expression(n.a); } // a computed key
        if (n.c == 1 || n.c == 3) {
            check_function(n.b, frame_kind::method);
            return;
        }
        walk_expression(n.b);
    }

    // A destructuring target: an array or object LITERAL being used as a
    // pattern, or a real pattern node. Nothing inside one is an assignment, so
    // the assignment-target rules do not apply to its elements - but the
    // DEFAULTS inside it are ordinary expressions and are walked.
    // ONE ELEMENT OF A DESTRUCTURING ASSIGNMENT, checked as the target it has
    // to be.
    //
    // 13.15.5 reinterprets an ArrayLiteral or an ObjectLiteral on the left of
    // `=` as a pattern, and the reinterpretation is not total: every element
    // has to be something a value can be assigned to, or another pattern.
    // `[[(x, y)]] = [[]]` parses as two array literals and is not one.
    //
    // Only for the LITERAL forms. `array_pattern` and `object_pattern` are what
    // the parser builds in a DECLARATION, where it has already restricted each
    // position to a name or a nested pattern - there is nothing left to check
    // and asking would only find the parser's own shapes.
    void check_pattern_target(std::int32_t idx) {
        if (idx < 0) { return; } // an elision: `[, a] = xs` skips a position
        switch (at(idx).kind) {
        case nk::ident:
        case nk::member:
        case nk::index:
        case nk::array:
        case nk::object:
        case nk::array_pattern:
        case nk::object_pattern: return;
        // A default: `[a = 1] = []`. Only `=` - `[a += 1] = []` is not one.
        case nk::assign:
            if (at(idx).text != "=") { break; }
            check_pattern_target(at(idx).a);
            return;
        case nk::assign_pattern: return;
        default: break;
        }
        report("this is not something a value can be assigned to in a destructuring pattern", idx);
    }

    void walk_pattern(std::int32_t idx) {
        if (idx < 0 || depth_ >= max_depth) { return; }
        const deeper nesting{depth_};
        const vp::node & n = at(idx);
        switch (n.kind) {
        case nk::array:
        case nk::array_pattern: {
            const std::span<const std::int32_t> elements = kids(n);
            const bool literal = n.kind == nk::array;
            for (std::size_t i = 0; i < elements.size(); ++i) {
                const vp::node & element = at(elements[i]);
                const bool rest = element.kind == nk::rest_element || element.kind == nk::spread;
                // 13.15.5.1 / 8.2.2: a rest element must be the last one, and
                // it may not carry a default - `[...x = 1] = []` is not a
                // pattern with a defaulted rest, it is an error.
                if (rest && i + 1 < elements.size()) {
                    report("a rest element must be the last one in the pattern", elements[i]);
                }
                if (rest && at(element.a).kind == nk::assign) {
                    report("a rest element may not have a default", elements[i]);
                }
                if (literal) { check_pattern_target(rest ? element.a : elements[i]); }
                walk_pattern(elements[i]);
            }
            return;
        }
        case nk::object:
        case nk::object_pattern: {
            const std::span<const std::int32_t> entries = kids(n);
            const bool literal = n.kind == nk::object;
            for (std::size_t i = 0; i < entries.size(); ++i) {
                const vp::node & entry = at(entries[i]);
                if (entry.kind == nk::spread || entry.kind == nk::rest_element) {
                    if (i + 1 < entries.size()) {
                        report("a rest element must be the last one in the pattern", entries[i]);
                    }
                    if (at(entry.a).kind == nk::assign) {
                        report("a rest element may not have a default", entries[i]);
                    }
                    if (literal) { check_pattern_target(entry.a); }
                } else if (literal && entry.kind == nk::prop) {
                    // A METHOD OR AN ACCESSOR IS NOT AN AssignmentProperty.
                    // `({ x: { get x() {} } } = o)` reads as an object literal
                    // right up to the `=`, and only then is it a pattern -
                    // which a getter cannot be part of.
                    // c == 1 IS A METHOD AND c == 3 AN ACCESSOR; c == 2 is a
                    // SHORTHAND, which is the commonest pattern element there
                    // is. The three numbers do not mean the same thing on a
                    // class member, where 2 is the accessor - checking the
                    // wrong one here reported `({ x } = o)`.
                    if (entry.c == 1 || entry.c == 3) {
                        report("a method cannot appear in a destructuring pattern", entries[i]);
                    } else if (entry.b >= 0) {
                        check_pattern_target(entry.b);
                    }
                }
                walk_pattern(entries[i]);
            }
            return;
        }
        case nk::prop:
            if ((n.d & 1) != 0) { walk_expression(n.a); }
            walk_pattern(n.b);
            return;
        case nk::pattern_prop:
            if ((n.d & 2) != 0) { walk_expression(n.a); }
            walk_pattern(n.b);
            return;
        case nk::assign_pattern:
            walk_pattern(n.a);
            walk_expression(n.b);
            return;
        case nk::assign:
            walk_pattern(n.a);
            walk_expression(n.b);
            return;
        case nk::rest_element:
        case nk::spread: walk_pattern(n.a); return;
        default: walk_expression(idx); return;
        }
    }

    const vp::ast & ast_;
    std::string_view source_;
    int depth_ = 0;
    std::vector<frame> frames_;
    std::optional<early_error> found_;
};

} // namespace

std::optional<early_error> find_early_error(const vp::ast & tree, std::string_view source) {
    if (!tree.ok || tree.root < 0) { return std::nullopt; }
    checker walk{tree, source};
    walk.run();
    return walk.result();
}

} // namespace ctbrowser::script::detail
