#include <ctbrowser/script/compile.hpp>

#include <array>
#include <charconv>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <ctjs/vparse.hpp>

#include <ctbrowser/script/value.hpp>

namespace ctbrowser::script {

namespace vp = ctjs::vp;

namespace {

// The compiler proper. Everything here was in the module interface before, so
// every translation unit that imported it re-instantiated the lot; it is an
// implementation detail and it lives in one translation unit now.
class compiler_impl {
public:
    struct local {
        std::string name;
        std::uint8_t reg = 0;
        bool boxed = false; // lives in a heap cell; see mark_captured
    };
    struct frame {
        std::uint32_t proto = 0;
        std::vector<local> locals;
        std::vector<std::string> declared;      // pre-scanned; see collect_declared_names
        std::vector<std::string> captured;      // names some nested function mentions
        std::vector<std::string> upvalue_names; // parallel to proto().upvalues
        std::vector<std::string> predeclared;   // hoisted at body entry; see predeclare_locals
        std::vector<std::size_t> scope_marks;   // locals.size() at each scope entry
        // WIDER THAN THE OPERAND THEY FEED, on purpose. A register index is a
        // uint8 in an instruction, and these used to be uint8 too - so a
        // function wanting more than 256 registers wrapped to r0 in silence and
        // its locals aliased each other. Counting in a wider type does not make
        // the bytecode hold more; it makes the compiler able to SAY how many
        // were wanted, which is the difference between a diagnostic and a bug.
        std::uint32_t next_reg = 0;
        std::uint32_t high_water = 0;
        bool is_async = false; // `return v` hands back a settled promise of v
    };

    compiler_impl(const vp::ast & tree, program & out)
        : ast_(tree), current_ast_(&tree), out_(out) {}

    // --- AST access -------------------------------------------------------
    [[nodiscard]] const vp::node & at(std::int32_t i) const {
        return current_ast_->nodes[static_cast<std::size_t>(i)];
    }

    // Compile an expression parsed from a DIFFERENT source than the program's.
    //
    // Template literals need it: the parser hands back `${...}` as raw text
    // inside one token, so the interpolations have to be parsed separately.
    // Node indices are per-AST, so the active one is swapped for the duration
    // and every `at()` follows it.
    void compile_foreign_expr(std::string_view source, std::uint8_t dst) {
        auto tree = std::make_unique<vp::ast>(vp::parse(source));
        if (!tree->ok || tree->root < 0) {
            proto().emit(instruction{op::load_undef, dst});
            return;
        }
        const vp::ast * saved = current_ast_;
        current_ast_ = tree.get();
        // The parse produces a program node; its single expression statement is
        // what we want.
        std::int32_t expression = tree->root;
        if (at(expression).kind == vp::nk::program) {
            const auto statements = kids(at(expression));
            expression = statements.empty() ? -1 : statements.front();
        }
        if (expression >= 0 && at(expression).kind == vp::nk::expr_stmt) {
            expression = at(expression).a;
        }
        if (expression >= 0) {
            compile_expr(expression, dst);
        } else {
            proto().emit(instruction{op::load_undef, dst});
        }
        current_ast_ = saved;
        owned_asts_.push_back(std::move(tree));
    }
    [[nodiscard]] std::vector<std::int32_t> kids(const vp::node & n) const {
        std::vector<std::int32_t> out;
        // The ACTIVE ast, not the outer one: a node reached inside a template
        // literal's sub-AST indexes that AST's pool, and reading the program's
        // would hand back unrelated nodes.
        for (std::int32_t i = 0; i < n.list_len; ++i) {
            out.push_back(current_ast_->pool[static_cast<std::size_t>(n.list + i)]);
        }
        return out;
    }

    // --- frames and registers ----------------------------------------------
    [[nodiscard]] frame & fn() { return frames_.back(); }
    [[nodiscard]] function_proto & proto() { return out_.functions[fn().proto]; }

    // Truncating is correct here and the overflow is caught once, at
    // finish_frame, where high_water knows the REAL total. Failing on the first
    // register past the limit would report 256 every time; what a person needs
    // to hear is that the function wanted 1,452.
    [[nodiscard]] std::uint8_t alloc_reg() {
        const std::uint32_t r = fn().next_reg++;
        if (fn().next_reg > fn().high_water) { fn().high_water = fn().next_reg; }
        return static_cast<std::uint8_t>(r);
    }
    void release_to(std::uint32_t mark) { fn().next_reg = mark; }
    [[nodiscard]] std::uint32_t reg_mark() const { return frames_.back().next_reg; }

    void push_scope() { fn().scope_marks.push_back(fn().locals.size()); }
    void pop_scope() {
        const std::size_t mark = fn().scope_marks.back();
        fn().scope_marks.pop_back();
        fn().locals.resize(mark);
    }
    [[nodiscard]] std::uint8_t declare_local(std::string name) {
        const std::uint8_t r = alloc_reg();
        const bool boxed = is_captured(name);
        fn().locals.push_back(local{std::move(name), r, boxed});
        return r;
    }
    // Bind a name to a register that ALREADY exists. The catch parameter needs
    // it: the handler writes the thrown value into a register chosen when the
    // try block opened, and the name has to refer to that same slot rather than
    // to a fresh one.
    void declare_local_at(std::string name, std::uint8_t reg) {
        const bool boxed = is_captured(name);
        if (boxed) { proto().emit(instruction{op::new_cell, reg}); }
        fn().locals.push_back(local{std::move(name), reg, boxed});
    }
    // -1 when not a local of the CURRENT frame
    [[nodiscard]] int find_local(std::string_view name) const {
        const frame & f = frames_.back();
        for (std::size_t i = f.locals.size(); i-- > 0;) {
            if (f.locals[i].name == name) { return f.locals[i].reg; }
        }
        return -1;
    }
    [[nodiscard]] local * find_local_entry(frame & f, std::string_view name) {
        for (std::size_t i = f.locals.size(); i-- > 0;) {
            if (f.locals[i].name == name) { return &f.locals[i]; }
        }
        return nullptr;
    }

    // Resolve `name` as an upvalue of frame `level`, adding the descriptor
    // chain if it is not already there. Returns -1 when the name is not a
    // local of any enclosing FUNCTION frame (frame 0 is the script, whose
    // declarations are globals and reachable directly).
    //
    // The recursion is what makes two-level capture work: if the name belongs
    // to a grandparent, the parent first acquires it as its own upvalue, and
    // this frame then captures the parent's upvalue rather than a register.
    [[nodiscard]] int resolve_upvalue(std::size_t level, std::string_view name) {
        if (level == 0) { return -1; }
        frame & f = frames_[level];
        for (std::size_t i = 0; i < f.upvalue_names.size(); ++i) {
            if (f.upvalue_names[i] == name) { return static_cast<int>(i); }
        }
        frame & parent = frames_[level - 1];
        // Frame 0 is the script, and its `var`s are globals rather than locals
        // (see the note in the var_decl arm), so the only locals it ever holds
        // are the ones a construct makes for itself: a for..of item, a catch
        // parameter. Those must be capturable or `for (const x of xs)
        // fns.push(() => x)` closes over nothing at the top level.
        if (local * l = find_local_entry(parent, name)) {
            l->boxed = true; // captured, so it must be a cell
            return add_upvalue(level, name, upvalue_desc{true, l->reg});
        }
        const int inherited = resolve_upvalue(level - 1, name);
        if (inherited < 0) { return -1; }
        return add_upvalue(level, name, upvalue_desc{false, static_cast<std::uint8_t>(inherited)});
    }

    [[nodiscard]] int add_upvalue(std::size_t level, std::string_view name, upvalue_desc desc) {
        frame & f = frames_[level];
        function_proto & p = out_.functions[f.proto];
        p.upvalues.push_back(desc);
        f.upvalue_names.emplace_back(name);
        return static_cast<int>(p.upvalues.size() - 1);
    }

    // Names that any nested function inside `body` mentions. Over-approximate
    // on purpose - see the note at the top of this file.
    // WHICH OF A NODE'S FOUR FIXED SLOTS ARE ACTUALLY CHILDREN.
    //
    // The parser reuses `c` and `d` as BITFIELDS on the kinds that need flags:
    // a rest parameter is `d == 1`, an async function is `c & 1`, a static class
    // member is `d & 1`, an object-literal accessor is `c == 3`. Nothing on a
    // node says which reading applies, so a generic walk over {a, b, c, d}
    // treats those flags as node indices - and index 1 is a real node, so the
    // walk goes back round the tree and never terminates.
    //
    // `function f(...rest) {}` overflowed the stack on the first one of these
    // the tests ever contained. The flags were always there; nothing had asked
    // a walker to look at a parameter node before.
    [[nodiscard]] static std::array<std::int32_t, 4> child_slots(const vp::node & n) {
        switch (n.kind) {
        // a = the default expression; d = the rest flag
        case vp::nk::param: return {n.a, -1, -1, -1};
        // a = the body; c = async/generator bits
        case vp::nk::func_decl:
        case vp::nk::func_expr:
        case vp::nk::arrow: return {n.a, -1, -1, -1};
        // a = a computed key, b = the value or method; c and d are both flags
        case vp::nk::class_member:
        case vp::nk::prop: return {n.a, n.b, -1, -1};
        default: return {n.a, n.b, n.c, n.d};
        }
    }

    void collect_captured_names(std::int32_t idx, bool inside_nested,
                                std::vector<std::string> & out) {
        if (idx < 0) { return; }
        const vp::node & n = at(idx);
        const bool nested = inside_nested || n.kind == vp::nk::func_decl ||
                            n.kind == vp::nk::func_expr || n.kind == vp::nk::arrow;
        if (inside_nested && n.kind == vp::nk::ident) { out.emplace_back(n.text); }
        for (const std::int32_t slot : child_slots(n)) {
            collect_captured_names(slot, nested, out);
        }
        for (const std::int32_t k : kids(n)) { collect_captured_names(k, nested, out); }
    }
    [[nodiscard]] bool is_captured(std::string_view name) const {
        const frame & f = frames_.back();
        for (const std::string & c : f.captured) {
            if (c == name) { return true; }
        }
        return false;
    }

    // The lexer hands back the RAW lexeme, quotes and all - `'a'` arrives as
    // three characters. Without this, every string literal in the program is
    // wrong by two characters, which shows up as 'a' + 'b' === "'a''b'" and
    // as o['a'] failing to find the property named a.
    [[nodiscard]] static std::string decode_string_literal(std::string_view lexeme) {
        if (lexeme.size() >= 2 &&
            (lexeme.front() == '\'' || lexeme.front() == '"' || lexeme.front() == '`')) {
            lexeme = lexeme.substr(1, lexeme.size() - 2);
        }
        std::string out;
        out.reserve(lexeme.size());
        for (std::size_t i = 0; i < lexeme.size(); ++i) {
            if (lexeme[i] != '\\' || i + 1 >= lexeme.size()) {
                out.push_back(lexeme[i]);
                continue;
            }
            switch (lexeme[++i]) {
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case 'r': out.push_back('\r'); break;
            case '0': out.push_back('\0'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'v': out.push_back('\v'); break;
            default: out.push_back(lexeme[i]); break; // \\ \' \" and anything else
            }
        }
        return out;
    }

    // Names a nested function might close over. Collected BEFORE the body is
    // compiled, because function declarations hoist and are therefore compiled
    // before the `let` that a closure would capture has been reached - without
    // this pre-scan the enclosing-local check simply never fires.
    void collect_declared_names(std::int32_t body) {
        if (body < 0) { return; }
        const vp::node & n = at(body);
        if (n.kind == vp::nk::var_decl) {
            for (const std::int32_t d : kids(n)) {
                fn().declared.push_back(std::string{at(d).text});
            }
            return;
        }
        if (n.kind == vp::nk::block || n.kind == vp::nk::program) {
            for (const std::int32_t s : kids(n)) { collect_declared_names(s); }
            return;
        }
        // loops and conditionals can declare too
        for (const std::int32_t slot : child_slots(n)) {
            if (slot >= 0 && at(slot).kind != vp::nk::func_decl &&
                at(slot).kind != vp::nk::func_expr && at(slot).kind != vp::nk::arrow) {
                collect_declared_names(slot);
            }
        }
    }

    // Hoist this body's own `let`/`const`/`var` names into registers before
    // anything is compiled. Nested function declarations hoist too and are
    // compiled first, so the locals they capture have to exist by then.
    void predeclare_locals(std::int32_t body) {
        if (body < 0 || at(body).kind != vp::nk::block) { return; }
        for (const std::int32_t stmt : kids(at(body))) {
            if (at(stmt).kind != vp::nk::var_decl) { continue; }
            for (const std::int32_t d : kids(at(stmt))) {
                std::string name{at(d).text};
                if (find_local_entry(fn(), name) != nullptr) { continue; }
                const std::uint8_t r = declare_local(name);
                proto().emit(instruction{op::load_undef, r});
                if (fn().locals.back().boxed) { proto().emit(instruction{op::new_cell, r}); }
                fn().predeclared.push_back(std::move(name));
            }
        }
    }
    [[nodiscard]] bool was_predeclared(std::string_view name) const {
        for (const std::string & p : frames_.back().predeclared) {
            if (p == name) { return true; }
        }
        return false;
    }

    void fail(std::string message) {
        if (out_.ok) {
            out_.ok = false;
            out_.error = std::move(message);
        }
    }

    // `function f(a, b = 1, ...rest)` - the two parts of that signature the
    // compiler used to DROP.
    //
    // The parser has carried both all along: a default is the param node's `a`
    // child and a rest is `d == 1`. Nothing read either, so an omitted argument
    // stayed undefined instead of taking its default, and `rest` bound the
    // single positional argument in that slot rather than an array of the
    // remainder. Neither was an error; both were wrong answers. p5.js has 47
    // signatures with a rest parameter alone.
    //
    // ORDER IS LOAD-BEARING here, and all three of these are the same hazard -
    // the arguments are in registers this frame is about to reuse:
    //
    //   1. gather_rest first. The extra arguments live in the registers just
    //      past the declared parameters, which is exactly where the body's
    //      locals and temporaries get allocated. Anything emitted before this
    //      reads them has already overwritten them.
    //   2. defaults next, and BEFORE boxing. A captured parameter is wrapped in
    //      a heap cell in place; a plain register write afterwards would drop
    //      the cell on the floor and the closure would see the wrong variable.
    //   3. a temporary allocated for a default expression must be released, or
    //      every default permanently widens the frame.
    void compile_parameter_prologue(const std::vector<std::int32_t> & params) {
        for (std::size_t i = 0; i < params.size(); ++i) {
            const vp::node & p = at(params[i]);
            if (p.d == 1) {
                proto().emit(instruction{op::gather_rest, static_cast<std::uint8_t>(i),
                                         static_cast<std::uint8_t>(i)});
            }
        }
        for (std::size_t i = 0; i < params.size(); ++i) {
            const vp::node & p = at(params[i]);
            if (p.d == 1 || p.a < 0) { continue; }
            const auto slot = static_cast<std::uint8_t>(i);
            const std::size_t skip = proto().emit(instruction{op::jump_if_defined, slot});
            const std::uint32_t mark = reg_mark();
            compile_expr(p.a, slot);
            release_to(mark);
            patch_here(skip);
        }
    }

    // A numeric literal's value.
    //
    // This was one call to std::from_chars in `general` format, which stops at
    // the `x` - so every `0xFF` in the program was the number ZERO, silently.
    // p5.js has 734 of them, spread through colour maths, bit masks and font
    // tables, and not one would have produced an error.
    //
    // The radix prefixes take the integer overload and then widen; a double is
    // exact up to 2^53, which is further than any of these literals reach.
    [[nodiscard]] static double number_literal(std::string_view text) {
        const std::string lex{text};
        const auto radix_of = [](char c) -> int {
            if (c == 'x' || c == 'X') { return 16; }
            if (c == 'o' || c == 'O') { return 8; }
            if (c == 'b' || c == 'B') { return 2; }
            return 0;
        };
        if (lex.size() > 2 && lex[0] == '0') {
            if (const int radix = radix_of(lex[1]); radix != 0) {
                std::uint64_t bits = 0;
                const char * const begin = lex.data() + 2;
                const char * const end = lex.data() + lex.size();
                if (std::from_chars(begin, end, bits, radix).ec == std::errc{}) {
                    return static_cast<double>(bits);
                }
                return 0;
            }
        }
        double d = 0;
        std::from_chars(lex.data(), lex.data() + lex.size(), d);
        return d;
    }

    // What a node kind is CALLED. Only the kinds the compiler can refuse need
    // a name; anything else falls back to the number, which is still better
    // than nothing when a new kind appears in the parser.
    [[nodiscard]] static std::string kind_name(vp::nk kind) {
        switch (kind) {
        case vp::nk::spread: return "spread in a call, `f(...args)`";
        case vp::nk::seq: return "the comma operator";
        case vp::nk::regex: return "a regular expression literal";
        case vp::nk::yield_expr: return "`yield`";
        case vp::nk::tagged: return "a tagged template literal";
        case vp::nk::arrow: return "an arrow function";
        case vp::nk::class_decl: return "a class declaration";
        case vp::nk::func_expr: return "a function expression";
        default: return "AST kind " + std::to_string(static_cast<int>(kind));
        }
    }

    // --- the operand limits, said out loud ----------------------------------
    //
    // Every one of these used to be a silent truncation. An instruction is four
    // bytes - `op` and three uint8s - so a register index, a property-name
    // index and a jump displacement all have to fit fields far smaller than a
    // real script needs, and the casts that made them fit were unchecked. The
    // 257th distinct property name in a function read a DIFFERENT property,
    // with no diagnostic anywhere; a function wanting more registers than a
    // byte holds aliased its own locals.
    //
    // These do not raise any limit. They make the compiler say which one it hit
    // and what it wanted, so a program that does not fit is a message rather
    // than a wrong answer. Widening the instruction is the next commit; this is
    // what makes it possible to tell whether the widening worked.
    static constexpr std::size_t operand_limit = 255; // a uint8 field
    static constexpr std::int32_t jump_limit = 32767; // the signed bx half

    [[nodiscard]] std::string frame_name(std::size_t index) const {
        const std::string & name = out_.functions[index].name;
        return "`" + (name.empty() ? std::string{"<anonymous>"} : name) + "`";
    }

    // The seam every property-name operand goes through. One place to check,
    // and one place for the next commit to widen.
    [[nodiscard]] std::uint8_t name_operand(std::string text) {
        const std::uint16_t index = proto().add_name(std::move(text));
        if (index > operand_limit) {
            fail(frame_name(fn().proto) + " mentions more than " +
                 std::to_string(operand_limit + 1) +
                 " distinct property names; the operand that selects one holds " +
                 std::to_string(operand_limit + 1) + ". Past that it reads a DIFFERENT property.");
        }
        return static_cast<std::uint8_t>(index);
    }

    // Called where a frame's size is finally written, because that is the only
    // point at which high_water is the truth rather than a running total.
    void finish_frame(std::size_t index, std::size_t params) {
        function_proto & fp = out_.functions[index];
        const std::uint32_t wanted = fn().high_water;
        fp.frame_size = static_cast<std::uint8_t>(wanted);
        fp.param_count = static_cast<std::uint8_t>(params);
        if (wanted > operand_limit) {
            fail(frame_name(index) + " needs " + std::to_string(wanted) +
                 " registers; a frame holds " + std::to_string(operand_limit + 1) +
                 ". Past that its locals alias each other.");
        }
        if (params > operand_limit) {
            fail(frame_name(index) + " takes " + std::to_string(params) +
                 " parameters; the limit is " + std::to_string(operand_limit) + ".");
        }
    }

    // --- entry --------------------------------------------------------------
    void compile_program() {
        out_.functions.emplace_back();
        out_.functions[0].name = "<script>";
        frames_.emplace_back();
        frames_.back().proto = 0;
        push_scope();

        const vp::node & root = at(ast_.root);
        collect_captured_names(ast_.root, false, fn().captured);
        collect_declared_names(ast_.root);
        // Function declarations hoist: a script may call one before its text.
        for (const std::int32_t s : kids(root)) {
            if (at(s).kind == vp::nk::func_decl) { compile_function_decl(s); }
        }
        for (const std::int32_t s : kids(root)) {
            if (at(s).kind != vp::nk::func_decl) { compile_stmt(s); }
        }
        proto().emit(instruction{op::ret_undef});
        finish_frame(fn().proto, 0);
        pop_scope();
        frames_.pop_back();
    }

    // --- statements ---------------------------------------------------------
    void compile_stmt(std::int32_t idx) {
        if (idx < 0 || !out_.ok) { return; }
        const vp::node & n = at(idx);
        const std::uint32_t mark = reg_mark();
        switch (n.kind) {
        case vp::nk::expr_stmt: {
            const std::uint8_t r = alloc_reg();
            compile_expr(n.a, r);
            break;
        }
        case vp::nk::var_decl:
            // TOP-LEVEL declarations become globals, not frame-0 registers.
            // Script scope is what functions declared alongside them close
            // over, and `let n = 0; function inc() { n = n + 1; }` is the most
            // common shape in JavaScript there is. Making them registers would
            // turn every one of those into the enclosing-local refusal below,
            // which would be correct and useless.
            if (frames_.size() == 1) {
                for (const std::int32_t d : kids(n)) {
                    const vp::node & decl = at(d);
                    const std::uint32_t mark = reg_mark();
                    const std::uint8_t r = alloc_reg();
                    if (decl.a >= 0) {
                        compile_expr(decl.a, r);
                    } else {
                        proto().emit(instruction{op::load_undef, r});
                    }
                    const std::uint8_t name = name_operand(std::string{decl.text});
                    proto().emit(instruction::with_bx(op::set_global, r, name));
                    release_to(mark);
                }
                return;
            }
            for (const std::int32_t d : kids(n)) {
                const vp::node & decl = at(d);
                if (was_predeclared(decl.text)) {
                    // hoisted above: this statement is only the initializer,
                    // and emit_write knows whether it goes through a cell
                    if (decl.a >= 0) {
                        const std::uint32_t mark = reg_mark();
                        const std::uint8_t tmp = alloc_reg();
                        compile_expr(decl.a, tmp);
                        emit_write(decl.text, tmp);
                        release_to(mark);
                    }
                    continue;
                }
                const std::uint8_t r = declare_local(std::string{decl.text});
                if (decl.a >= 0) {
                    compile_expr(decl.a, r);
                } else {
                    proto().emit(instruction{op::load_undef, r});
                }
                // A captured local is boxed AFTER its initializer runs, so the
                // cell starts out holding the right value.
                if (fn().locals.back().boxed) { proto().emit(instruction{op::new_cell, r}); }
                // a declared local keeps its register beyond this statement
                if (fn().next_reg <= r) { fn().next_reg = static_cast<std::uint8_t>(r + 1); }
            }
            return; // locals must NOT be released by the mark below
        case vp::nk::block:
            push_scope();
            for (const std::int32_t s : kids(n)) { compile_stmt(s); }
            pop_scope();
            break;
        case vp::nk::if_stmt: compile_if(n); break;
        case vp::nk::while_stmt: compile_while(n); break;
        case vp::nk::do_stmt: compile_do_while(n); break;
        case vp::nk::forof_stmt: compile_for_of(n); break;
        case vp::nk::class_decl: {
            const std::uint8_t r = alloc_reg();
            compile_class(n, r);
            emit_write(std::string{n.text}, r);
            break;
        }
        case vp::nk::switch_stmt: compile_switch(n); break;
        case vp::nk::for_stmt: compile_for(n); break;
        case vp::nk::break_stmt: compile_break(n); break;
        case vp::nk::continue_stmt: compile_continue(n); break;
        case vp::nk::labeled: compile_labeled(n); break;
        case vp::nk::try_stmt: compile_try(n); break;
        case vp::nk::throw_stmt: compile_throw(n); break;
        case vp::nk::return_stmt: {
            const std::uint8_t r = alloc_reg();
            if (n.a >= 0) {
                compile_expr(n.a, r);
            } else {
                proto().emit(instruction{op::load_undef, r});
            }
            // `async function f() { return 5 }` hands back a PROMISE of 5, not
            // 5 - so `f().then(...)` works and not only `await f()`.
            if (fn().is_async) { proto().emit(instruction{op::wrap_promise, r}); }
            proto().emit(instruction{op::ret, r});
            break;
        }
        case vp::nk::func_decl: compile_function_decl(idx); return;
        case vp::nk::empty: break;
        default: {
            // anything not yet handled is still an expression in most cases
            const std::uint8_t r = alloc_reg();
            compile_expr(idx, r);
            break;
        }
        }
        release_to(mark);
    }

    void compile_if(const vp::node & n) {
        const std::uint32_t mark = reg_mark();
        const std::uint8_t cond = alloc_reg();
        compile_expr(n.a, cond);
        const std::size_t to_else = proto().emit(instruction{op::jump_if_false, cond});
        release_to(mark);

        compile_stmt(n.b);
        if (n.c >= 0) {
            const std::size_t to_end = proto().emit(instruction{op::jump});
            patch_here(to_else);
            compile_stmt(n.c);
            patch_here(to_end);
        } else {
            patch_here(to_else);
        }
    }

    // One live loop. `break` and `continue` are forward jumps whose targets are
    // not known until the loop is finished being compiled, so each records its
    // jump site here and the loop patches them on the way out.
    //
    // `label` is what makes `break outer;` reach past an inner loop - without
    // it, a labeled break silently becomes an ordinary one and leaves the wrong
    // loop.
    struct loop_context {
        std::string label;
        std::vector<std::size_t> breaks;
        std::vector<std::size_t> continues;
        std::size_t handler_depth = 0; // try blocks open when the loop started
    };

    void patch_breaks(loop_context & loop) {
        for (const std::size_t site : loop.breaks) { patch_here(site); }
    }
    void patch_continues(loop_context & loop, std::size_t target) {
        for (const std::size_t site : loop.continues) { patch_jump(site, target); }
    }

    // The loop a break/continue belongs to: the named one, or the innermost.
    [[nodiscard]] loop_context * loop_for(std::string_view label) {
        if (loops_.empty()) { return nullptr; }
        if (label.empty()) { return &loops_.back(); }
        for (std::size_t i = loops_.size(); i-- > 0;) {
            if (loops_[i].label == label) { return &loops_[i]; }
        }
        return nullptr;
    }

    void compile_break(const vp::node & n) {
        loop_context * loop = loop_for(n.text);
        if (loop == nullptr) {
            fail(n.text.empty() ? "break outside a loop" : "break to unknown label");
            return;
        }
        // Leaving a try block by jumping out of it has to drop its handler, or
        // the catch stays reachable after the loop is gone.
        for (std::size_t i = handler_depth_; i > loop->handler_depth; --i) {
            proto().emit(instruction{op::pop_handler});
        }
        loop->breaks.push_back(proto().emit(instruction{op::jump}));
    }

    void compile_continue(const vp::node & n) {
        loop_context * loop = loop_for(n.text);
        if (loop == nullptr) {
            fail(n.text.empty() ? "continue outside a loop" : "continue to unknown label");
            return;
        }
        for (std::size_t i = handler_depth_; i > loop->handler_depth; --i) {
            proto().emit(instruction{op::pop_handler});
        }
        loop->continues.push_back(proto().emit(instruction{op::jump}));
    }

    // A labeled statement. Only labels on loops mean anything here: a label on
    // anything else is legal JS but nothing can target it except `break`, and
    // `break` out of a plain block is vanishingly rare.
    void compile_labeled(const vp::node & n) {
        pending_label_ = std::string{n.text};
        compile_stmt(n.a);
        pending_label_.clear();
    }

    void compile_while(const vp::node & n) {
        const std::size_t top = proto().code.size();
        loops_.push_back(loop_context{take_label(), {}, {}, handler_depth_});
        const std::uint32_t mark = reg_mark();
        const std::uint8_t cond = alloc_reg();
        compile_expr(n.a, cond);
        const std::size_t exit = proto().emit(instruction{op::jump_if_false, cond});
        release_to(mark);
        compile_stmt(n.b);
        patch_continues(loops_.back(), top); // continue re-tests the condition
        patch_jump(proto().emit(instruction{op::jump}), top);
        patch_here(exit);
        patch_breaks(loops_.back());
        loops_.pop_back();
    }

    // do..while: the body runs before the first test, which is the whole
    // difference and the reason it cannot share compile_while.
    void compile_do_while(const vp::node & n) {
        const std::size_t top = proto().code.size();
        loops_.push_back(loop_context{take_label(), {}, {}, handler_depth_});
        compile_stmt(n.a);
        const std::size_t test = proto().code.size();
        patch_continues(loops_.back(), test);
        const std::uint32_t mark = reg_mark();
        const std::uint8_t cond = alloc_reg();
        compile_expr(n.b, cond);
        const std::size_t exit = proto().emit(instruction{op::jump_if_false, cond});
        release_to(mark);
        patch_jump(proto().emit(instruction{op::jump}), top);
        patch_here(exit);
        patch_breaks(loops_.back());
        loops_.pop_back();
    }

    void compile_for(const vp::node & n) {
        push_scope();
        const std::string label = take_label();
        if (n.a >= 0) { compile_stmt(n.a); }
        const std::size_t top = proto().code.size();
        loops_.push_back(loop_context{label, {}, {}, handler_depth_});
        std::size_t exit = 0;
        bool has_cond = false;
        if (n.b >= 0) {
            const std::uint32_t mark = reg_mark();
            const std::uint8_t cond = alloc_reg();
            compile_expr(n.b, cond);
            exit = proto().emit(instruction{op::jump_if_false, cond});
            has_cond = true;
            release_to(mark);
        }
        compile_stmt(n.d);
        // `continue` in a for-loop runs the UPDATE and then re-tests - it does
        // not skip back to the condition. Getting this wrong turns every
        // `for (...; i++) { ... continue; }` into an infinite loop.
        patch_continues(loops_.back(), proto().code.size());
        if (n.c >= 0) {
            const std::uint32_t mark = reg_mark();
            const std::uint8_t tmp = alloc_reg();
            compile_expr(n.c, tmp);
            release_to(mark);
        }
        patch_jump(proto().emit(instruction{op::jump}), top);
        if (has_cond) { patch_here(exit); }
        patch_breaks(loops_.back());
        loops_.pop_back();
        pop_scope();
    }

    // for..of and for..in.
    //
    // Compiled as an index loop over a length rather than through an iterator
    // protocol: arrays and strings are what pages iterate, and a real iterator
    // needs Symbol.iterator, which needs symbols. `for..in` goes through the
    // same loop over an array of keys, so there is one iteration mechanism
    // here and not two.
    //
    // The limitation is real and worth stating: an object that is neither an
    // array nor a string has no `length`, so `for (x of somethingElse)` runs
    // zero times instead of throwing.
    void compile_for_of(const vp::node & n) {
        push_scope();
        const std::string label = take_label();
        const std::uint32_t mark = reg_mark();

        const std::uint8_t source = alloc_reg();
        compile_expr(n.b, source);
        if (n.text == "in") { proto().emit(instruction{op::own_keys, source, source}); }

        const std::uint8_t length = alloc_reg();
        const std::uint8_t length_name = name_operand("length");
        proto().emit(instruction{op::get_prop, length, source, length_name});
        const std::uint8_t index = alloc_reg();
        emit_const(index, value::number(0));
        const std::uint8_t one = alloc_reg();
        emit_const(one, value::number(1));

        // The loop variable is a real local, so a closure made inside the body
        // captures THIS iteration's value - which is the whole reason `let` in
        // a loop behaves differently from `var`.
        const std::uint8_t item = declare_local(std::string{at(n.a).text});

        const std::size_t top = proto().code.size();
        loops_.push_back(loop_context{label, {}, {}, handler_depth_});
        const std::uint8_t test = alloc_reg();
        proto().emit(instruction{op::less, test, index, length});
        const std::size_t exit = proto().emit(instruction{op::jump_if_false, test});

        proto().emit(instruction{op::get_index, item, source, index});
        if (const local * l = find_local_entry(fn(), at(n.a).text); l != nullptr && l->boxed) {
            // A captured loop variable lives in a cell, and a fresh cell per
            // iteration is what makes the capture see this element rather than
            // the last.
            proto().emit(instruction{op::new_cell, item});
        }
        compile_stmt(n.c);

        patch_continues(loops_.back(), proto().code.size());
        proto().emit(instruction{op::add, index, index, one});
        patch_jump(proto().emit(instruction{op::jump}), top);
        patch_here(exit);
        patch_breaks(loops_.back());
        loops_.pop_back();
        release_to(mark);
        pop_scope();
    }

    // switch.
    //
    // Two passes: every case's test first, jumping to its body, then the bodies
    // laid out in order so FALLTHROUGH works - a case without a break really
    // does run the next one, and code relies on that.
    void compile_switch(const vp::node & n) {
        push_scope();
        const std::uint32_t mark = reg_mark();
        const std::uint8_t subject = alloc_reg();
        compile_expr(n.a, subject);

        const std::vector<std::int32_t> clauses = kids(n);
        std::vector<std::size_t> entries(clauses.size(), 0);
        std::size_t default_clause = clauses.size();

        const std::uint8_t candidate = alloc_reg();
        const std::uint8_t matched = alloc_reg();
        for (std::size_t i = 0; i < clauses.size(); ++i) {
            const vp::node & clause = at(clauses[i]);
            // `default` is `d == 1`. Testing `d != 0` treated EVERY case as the
            // default - node.d starts at -1, which is ctjs's documented gotcha -
            // so no comparison was emitted at all and each clause ran
            // unconditionally.
            if (clause.d == 1 || clause.a < 0) {
                default_clause = i;
                continue;
            }
            compile_expr(clause.a, candidate);
            // STRICT equality, per spec - `switch (1)` does not match `case "1"`.
            proto().emit(instruction{op::equal, matched, subject, candidate});
            entries[i] = proto().emit(instruction{op::jump_if_true, matched});
        }
        const std::size_t to_default = proto().emit(instruction{op::jump});
        release_to(mark);

        // `break` inside a switch leaves the switch, so it needs a loop context
        // even though nothing here loops.
        loops_.push_back(loop_context{take_label(), {}, {}, handler_depth_});
        for (std::size_t i = 0; i < clauses.size(); ++i) {
            if (i == default_clause) {
                patch_here(to_default);
            } else {
                patch_here(entries[i]);
            }
            for (const std::int32_t statement : kids(at(clauses[i]))) { compile_stmt(statement); }
        }
        if (default_clause == clauses.size()) { patch_here(to_default); }
        patch_breaks(loops_.back());
        loops_.pop_back();
        pop_scope();
    }

    // try / catch / finally.
    //
    // `finally` is compiled by DUPLICATING its body on both exits - the normal
    // one and the caught one. The alternative is a subroutine-return opcode,
    // and duplication is the honest trade at this size: two copies of a small
    // block against a control-flow mechanism nothing else needs. A `return`
    // inside a try does NOT run the finally block, which is a real gap and is
    // noted rather than hidden.
    void compile_try(const vp::node & n) {
        const std::int32_t catch_clause = n.b;
        const std::int32_t finally_block = n.c;

        std::uint8_t caught_reg = 0;
        std::string caught_name;
        if (catch_clause >= 0) {
            // The parameter name is the clause's TEXT and the body is its `a`.
            // Reading them as `a` and `b` compiled an empty catch block, so a
            // throw jumped correctly and then fell straight through it.
            caught_name = std::string{at(catch_clause).text};
        }

        push_scope();
        caught_reg = alloc_reg();
        const std::size_t guard = proto().emit(instruction{op::push_handler, caught_reg});
        ++handler_depth_;
        compile_stmt(n.a);
        proto().emit(instruction{op::pop_handler});
        --handler_depth_;
        if (finally_block >= 0) { compile_stmt(finally_block); }
        const std::size_t done = proto().emit(instruction{op::jump});

        // The catch block. push_handler's operand is a RELATIVE address, so it
        // is patched the same way a forward jump is.
        patch_here(guard);
        if (catch_clause >= 0) {
            push_scope();
            if (!caught_name.empty()) { declare_local_at(caught_name, caught_reg); }
            compile_stmt(at(catch_clause).a);
            pop_scope();
        }
        if (finally_block >= 0) { compile_stmt(finally_block); }
        patch_here(done);
        pop_scope();
    }

    void compile_throw(const vp::node & n) {
        const std::uint32_t mark = reg_mark();
        const std::uint8_t r = alloc_reg();
        compile_expr(n.a, r);
        proto().emit(instruction{op::throw_value, r});
        release_to(mark);
    }

    [[nodiscard]] std::string take_label() {
        std::string out = std::move(pending_label_);
        pending_label_.clear();
        return out;
    }

    // Falling off the end of an async function still owes the caller a promise.
    void emit_implicit_return() {
        if (!fn().is_async) {
            proto().emit(instruction{op::ret_undef});
            return;
        }
        const std::uint8_t r = alloc_reg();
        proto().emit(instruction{op::load_undef, r});
        proto().emit(instruction{op::wrap_promise, r});
        proto().emit(instruction{op::ret, r});
    }

    void compile_function_decl(std::int32_t idx) {
        const vp::node & n = at(idx);
        const std::uint32_t index = compile_function_body(idx, std::string{n.text});
        const std::uint8_t r = alloc_reg();
        proto().emit(instruction::with_bx(op::closure, r, static_cast<std::uint16_t>(index)));
        const std::uint8_t name = name_operand(std::string{n.text});
        proto().emit(instruction::with_bx(op::set_global, r, name));
        release_to(static_cast<std::uint8_t>(r));
    }

    [[nodiscard]] std::uint32_t compile_function_body(std::int32_t idx, std::string name) {
        const vp::node & n = at(idx);
        const auto index = static_cast<std::uint32_t>(out_.functions.size());
        out_.functions.emplace_back();
        out_.functions[index].name = std::move(name);

        frames_.emplace_back();
        frames_.back().proto = index;
        push_scope();
        // Which of this body's names some nested function mentions has to be
        // known BEFORE any local is declared - that is what decides whether a
        // local gets a register or a cell.
        collect_captured_names(n.a, false, fn().captured);
        const std::vector<std::int32_t> params = kids(n);
        for (const std::int32_t p : params) { (void)declare_local(std::string{at(p).text}); }
        compile_parameter_prologue(params);
        // A captured PARAMETER needs boxing too, and it arrives already
        // holding its value - so box in place, after the arguments land.
        for (const local & l : fn().locals) {
            if (l.boxed) { proto().emit(instruction{op::new_cell, l.reg}); }
        }
        collect_declared_names(n.a);
        // Declarations are hoisted to the top of the body BEFORE any nested
        // function is compiled. Without this, a nested function DECLARATION
        // (which hoists, so it compiles first) resolves the enclosing local
        // it means to capture as a global instead, and reads undefined.
        predeclare_locals(n.a);

        // c bit0 = async, bit1 = generator - but `c` DEFAULTS TO -1, so an
        // unmarked function looks async unless the sign is checked first.
        fn().is_async = n.c > 0 && (n.c & 1) != 0;

        const std::int32_t body = n.a;
        if (body >= 0 && at(body).kind == vp::nk::block) {
            for (const std::int32_t s : kids(at(body))) {
                if (at(s).kind == vp::nk::func_decl) { compile_stmt(s); }
            }
            for (const std::int32_t s : kids(at(body))) {
                if (at(s).kind != vp::nk::func_decl) { compile_stmt(s); }
            }
            emit_implicit_return();
        } else if (body >= 0) {
            // concise arrow body: `x => expr` returns expr
            const std::uint8_t r = alloc_reg();
            compile_expr(body, r);
            if (fn().is_async) { proto().emit(instruction{op::wrap_promise, r}); }
            proto().emit(instruction{op::ret, r});
        } else {
            emit_implicit_return();
        }
        finish_frame(index, params.size());
        pop_scope();
        frames_.pop_back();
        return index;
    }

    // --- expressions ---------------------------------------------------------
    void compile_expr(std::int32_t idx, std::uint8_t dst) {
        if (idx < 0 || !out_.ok) {
            proto().emit(instruction{op::load_undef, dst});
            return;
        }
        const vp::node & n = at(idx);
        switch (n.kind) {
        case vp::nk::num: {
            emit_const(dst, value::number(number_literal(n.text)));
            break;
        }
        case vp::nk::str:
            proto().emit(instruction::with_bx(op::load_string, dst,
                                              proto().add_string(decode_string_literal(n.text))));
            break;
        case vp::nk::tmpl: compile_template(n, dst); break;
        case vp::nk::new_expr: compile_new(n, dst); break;
        case vp::nk::opt_member:
        case vp::nk::opt_index:
        case vp::nk::opt_call: compile_optional(n, dst); break;
        case vp::nk::seq: compile_sequence(n, dst); break;
        case vp::nk::class_decl: compile_class(n, dst); break;
        case vp::nk::true_lit: proto().emit(instruction{op::load_true, dst}); break;
        case vp::nk::false_lit: proto().emit(instruction{op::load_false, dst}); break;
        case vp::nk::null_lit: proto().emit(instruction{op::load_null, dst}); break;
        case vp::nk::ident: compile_ident(n, dst); break;
        case vp::nk::binary: compile_binary(n, dst); break;
        case vp::nk::logical: compile_logical(n, dst); break;
        case vp::nk::unary: compile_unary(n, dst); break;
        case vp::nk::assign: compile_assign(n, dst); break;
        case vp::nk::update: compile_update(n, dst); break;
        case vp::nk::ternary: compile_ternary(n, dst); break;
        case vp::nk::member: {
            // `super.x` reads through the parent prototype rather than through
            // an object expression - there is no value `super` evaluates to.
            if (n.a >= 0 && at(n.a).kind == vp::nk::super_lit) {
                emit_super_base(dst);
            } else {
                compile_expr(n.a, dst);
            }
            const std::uint8_t name = name_operand(std::string{n.text});
            proto().emit(instruction{op::get_prop, dst, dst, name});
            break;
        }
        case vp::nk::super_lit:
            // Bare `super` is not a value in JavaScript either - it is only ever
            // `super(...)` or `super.x`, both handled where they appear.
            fail("`super` is only valid as `super(...)` or `super.member`");
            break;
        case vp::nk::index: {
            const std::uint32_t mark = reg_mark();
            compile_expr(n.a, dst);
            const std::uint8_t key = alloc_reg();
            compile_expr(n.b, key);
            proto().emit(instruction{op::get_index, dst, dst, key});
            release_to(mark > dst ? mark : static_cast<std::uint8_t>(dst + 1));
            break;
        }
        case vp::nk::call: compile_call(n, dst); break;
        case vp::nk::array: compile_array(n, dst); break;
        case vp::nk::object: compile_object(n, dst); break;
        case vp::nk::func_expr:
        case vp::nk::arrow: {
            const std::uint32_t index = compile_function_body(idx, std::string{n.text});
            proto().emit(instruction::with_bx(op::closure, dst, static_cast<std::uint16_t>(index)));
            break;
        }
        case vp::nk::this_lit: proto().emit(instruction{op::load_this, dst}); break;
        // Regular expressions are DEFERRED, not overlooked: they need a regex
        // engine, and the standard library says the same about String.match and
        // the RegExp forms of replace/split. Rejecting one by name beats
        // mis-compiling it into something that silently does nothing.
        case vp::nk::regex:
            fail("regular expression literals are not in this VM subset - "
                 "there is no regex engine (" +
                 std::string{n.text} + ")");
            proto().emit(instruction{op::load_undef, dst});
            break;
        case vp::nk::yield_expr:
            fail("`yield` is not in this VM subset - there are no generators");
            proto().emit(instruction{op::load_undef, dst});
            break;
        case vp::nk::tagged:
            fail("tagged template literals are not in this VM subset");
            proto().emit(instruction{op::load_undef, dst});
            break;
        default:
            // "AST kind 13" is a number, not a diagnostic. Naming the construct
            // is the difference between a report you can act on and one you
            // have to go and decode: kind 13 is `f(...args)`, and it stops
            // thirteen of p5.js's seventy-one modules on its own.
            fail(std::string{"unsupported syntax in this VM subset: "} + kind_name(n.kind));
            proto().emit(instruction{op::load_undef, dst});
            break;
        }
    }

    // Reading and writing a name are the only two places that need to know
    // whether it lives in a register, a cell, an upvalue or the global table.
    // ++/-- goes through them too, so it cannot drift out of agreement.
    void emit_read(std::string_view name_text, std::uint8_t dst) {
        if (const local * l = find_local_entry(fn(), name_text)) {
            if (l->boxed) {
                proto().emit(instruction{op::cell_get, dst, l->reg});
            } else {
                proto().emit(instruction{op::move, dst, l->reg});
            }
            return;
        }
        const int up = resolve_upvalue(frames_.size() - 1, name_text);
        if (up >= 0) {
            proto().emit(instruction{op::get_upvalue, dst, static_cast<std::uint8_t>(up)});
            return;
        }
        proto().emit(
            instruction::with_bx(op::get_global, dst, proto().add_name(std::string{name_text})));
    }
    void emit_write(std::string_view name_text, std::uint8_t src) {
        if (const local * l = find_local_entry(fn(), name_text)) {
            if (l->boxed) {
                proto().emit(instruction{op::cell_set, l->reg, src});
            } else {
                proto().emit(instruction{op::move, l->reg, src});
            }
            return;
        }
        const int up = resolve_upvalue(frames_.size() - 1, name_text);
        if (up >= 0) {
            proto().emit(instruction{op::set_upvalue, static_cast<std::uint8_t>(up), src});
            return;
        }
        proto().emit(
            instruction::with_bx(op::set_global, src, proto().add_name(std::string{name_text})));
    }

    void compile_ident(const vp::node & n, std::uint8_t dst) {
        if (const local * l = find_local_entry(fn(), n.text)) {
            if (l->boxed) {
                proto().emit(instruction{op::cell_get, dst, l->reg});
            } else {
                proto().emit(instruction{op::move, dst, l->reg});
            }
            return;
        }
        const int up = resolve_upvalue(frames_.size() - 1, n.text);
        if (up >= 0) {
            proto().emit(instruction{op::get_upvalue, dst, static_cast<std::uint8_t>(up)});
            return;
        }
        const std::uint8_t name = name_operand(std::string{n.text});
        proto().emit(instruction::with_bx(op::get_global, dst, name));
    }

    // `delete o.x` / `delete o[k]`. Anything else - `delete x` on a plain
    // variable - is a no-op that yields false, which is what non-strict
    // JavaScript does with an undeletable binding.
    void compile_delete(const vp::node & n, std::uint8_t dst) {
        const std::uint32_t mark = reg_mark();
        const vp::node & target = at(n.a);
        if (target.kind == vp::nk::member) {
            const std::uint8_t object = alloc_reg();
            compile_expr(target.a, object);
            proto().emit(
                instruction{op::delete_prop, object, name_operand(std::string{target.text})});
            emit_const(dst, value::boolean(true));
        } else if (target.kind == vp::nk::index) {
            const std::uint8_t object = alloc_reg();
            compile_expr(target.a, object);
            const std::uint8_t key = alloc_reg();
            compile_expr(target.b, key);
            proto().emit(instruction{op::delete_index, object, key});
            emit_const(dst, value::boolean(true));
        } else {
            emit_const(dst, value::boolean(false));
        }
        release_to(mark);
    }

    void compile_binary(const vp::node & n, std::uint8_t dst) {
        const std::uint32_t mark = reg_mark();
        const std::uint8_t lhs = alloc_reg();
        const std::uint8_t rhs = alloc_reg();
        compile_expr(n.a, lhs);
        compile_expr(n.b, rhs);
        const std::string_view o = n.text;
        op code = op::add_generic;
        if (o == "+") {
            code = op::add_generic;
        } else if (o == "-") {
            code = op::sub;
        } else if (o == "*") {
            code = op::mul;
        } else if (o == "/") {
            code = op::div;
        } else if (o == "%") {
            code = op::mod;
        } else if (o == "**") {
            code = op::pow;
        } else if (o == "===") {
            code = op::equal;
        } else if (o == "!==") {
            code = op::not_equal;
        }
        // `==` and `!=` are LOOSE. Compiling `!=` as `!==` made `1 != "1"` true,
        // which is the opposite of what the operator means.
        else if (o == "==") {
            code = op::loose_equal;
        } else if (o == "!=") {
            code = op::loose_not_equal;
        } else if (o == "&") {
            code = op::bit_and;
        } else if (o == "|") {
            code = op::bit_or;
        } else if (o == "^") {
            code = op::bit_xor;
        } else if (o == "<<") {
            code = op::shl;
        } else if (o == ">>") {
            code = op::shr;
        } else if (o == ">>>") {
            code = op::ushr;
        } else if (o == "instanceof") {
            code = op::instance_of;
        } else if (o == "in") {
            code = op::has_property;
        } else if (o == "<") {
            code = op::less;
        } else if (o == "<=") {
            code = op::less_equal;
        } else if (o == ">") {
            code = op::greater;
        } else if (o == ">=") {
            code = op::greater_equal;
        } else {
            fail("unsupported binary operator '" + std::string{o} + "'");
        }
        proto().emit(instruction{code, dst, lhs, rhs});
        release_to(mark);
    }

    // && and || must not evaluate the right side unless they have to, so they
    // are control flow rather than an opcode.
    void compile_logical(const vp::node & n, std::uint8_t dst) {
        compile_expr(n.a, dst);
        // `??` is not `||`. It asks whether the left side is null or undefined,
        // so `0 ?? 5` is 0 and `"" ?? "x"` is "" - which is why anyone reaches
        // for it over `||` in the first place.
        const op test = n.text == "&&"    ? op::jump_if_false
                        : n.text == "?\?" ? op::jump_if_not_nullish
                                          : op::jump_if_true;
        const std::size_t skip = proto().emit(instruction{test, dst});
        compile_expr(n.b, dst);
        patch_here(skip);
    }

    void compile_unary(const vp::node & n, std::uint8_t dst) {
        // `delete o.x` must NOT evaluate `o.x` - it takes the object and the
        // key, which is why it cannot go through the operand-first path below.
        if (n.text == "delete") {
            compile_delete(n, dst);
            return;
        }
        const std::uint32_t mark = reg_mark();
        const std::uint8_t operand = alloc_reg();
        compile_expr(n.a, operand);
        if (n.text == "-") {
            proto().emit(instruction{op::negate, dst, operand});
        } else if (n.text == "!") {
            proto().emit(instruction{op::logical_not, dst, operand});
        } else if (n.text == "typeof") {
            proto().emit(instruction{op::type_of, dst, operand});
        } else if (n.text == "await") {
            // Promises here are SETTLED on creation - there is no event loop
            // suspending a frame - so awaiting one is reading its value, and
            // awaiting a plain value is the value. That is the same subset the previous engine
            // shipped, and it is what `await fetch(...)` needs.
            proto().emit(instruction{op::await_value, dst, operand});
        } else if (n.text == "+") {
            proto().emit(instruction{op::move, dst, operand});
        } else if (n.text == "~") {
            proto().emit(instruction{op::bit_not, dst, operand});
        }
        // `void x` evaluates x for its effects and yields undefined.
        else if (n.text == "void") {
            proto().emit(instruction{op::load_undef, dst});
        } else {
            fail("unsupported unary operator '" + std::string{n.text} + "'");
        }
        release_to(mark);
    }

    // A place a value can be read from AND written to.
    //
    // Compound assignment and ++/-- both have to evaluate their target once and
    // then read-modify-write it. Re-compiling the target expression for the
    // write would evaluate its side effects twice, so `a[i++] += 1` would
    // increment i twice and store into the wrong slot. This is the shape that
    // makes both of them correct, and it is why they share a code path.
    struct reference {
        enum class kind : std::uint8_t {
            local,
            boxed_local,
            upvalue,
            global,
            member,
            index
        };
        kind what = kind::local;
        std::uint8_t reg = 0;   // local/boxed: its register. member/index: the object.
        std::uint8_t key = 0;   // index: the key register
        std::uint16_t name = 0; // global/member: the name index
    };

    [[nodiscard]] reference prepare_reference(const vp::node & target) {
        reference out;
        if (target.kind == vp::nk::ident) {
            if (const local * l = find_local_entry(fn(), target.text)) {
                out.what = l->boxed ? reference::kind::boxed_local : reference::kind::local;
                out.reg = l->reg;
                return out;
            }
            if (const int up = resolve_upvalue(frames_.size() - 1, target.text); up >= 0) {
                out.what = reference::kind::upvalue;
                out.reg = static_cast<std::uint8_t>(up);
                return out;
            }
            out.what = reference::kind::global;
            out.name = proto().add_name(std::string{target.text});
            return out;
        }
        if (target.kind == vp::nk::member) {
            out.what = reference::kind::member;
            out.reg = alloc_reg();
            compile_expr(target.a, out.reg);
            out.name = proto().add_name(std::string{target.text});
            return out;
        }
        if (target.kind == vp::nk::index) {
            out.what = reference::kind::index;
            out.reg = alloc_reg();
            out.key = alloc_reg();
            compile_expr(target.a, out.reg);
            compile_expr(target.b, out.key);
            return out;
        }
        fail("unsupported assignment target");
        return out;
    }

    void emit_load(const reference & ref, std::uint8_t dst) {
        switch (ref.what) {
        case reference::kind::local: proto().emit(instruction{op::move, dst, ref.reg}); break;
        case reference::kind::boxed_local:
            proto().emit(instruction{op::cell_get, dst, ref.reg});
            break;
        case reference::kind::upvalue:
            proto().emit(instruction{op::get_upvalue, dst, ref.reg});
            break;
        case reference::kind::global:
            proto().emit(instruction::with_bx(op::get_global, dst, ref.name));
            break;
        case reference::kind::member:
            proto().emit(
                instruction{op::get_prop, dst, ref.reg, static_cast<std::uint8_t>(ref.name)});
            break;
        case reference::kind::index:
            proto().emit(instruction{op::get_index, dst, ref.reg, ref.key});
            break;
        }
    }

    void emit_store(const reference & ref, std::uint8_t src) {
        switch (ref.what) {
        case reference::kind::local: proto().emit(instruction{op::move, ref.reg, src}); break;
        case reference::kind::boxed_local:
            proto().emit(instruction{op::cell_set, ref.reg, src});
            break;
        case reference::kind::upvalue:
            proto().emit(instruction{op::set_upvalue, ref.reg, src});
            break;
        case reference::kind::global:
            proto().emit(instruction::with_bx(op::set_global, src, ref.name));
            break;
        case reference::kind::member:
            proto().emit(
                instruction{op::set_prop, ref.reg, static_cast<std::uint8_t>(ref.name), src});
            break;
        case reference::kind::index:
            proto().emit(instruction{op::set_index, ref.reg, ref.key, src});
            break;
        }
    }

    // `+=` and friends. The operator is the assignment's text minus its '='.
    [[nodiscard]] static op compound_op(std::string_view text, bool & ok) {
        ok = true;
        if (text == "+=") { return op::add_generic; }
        if (text == "-=") { return op::sub; }
        if (text == "*=") { return op::mul; }
        if (text == "/=") { return op::div; }
        if (text == "%=") { return op::mod; }
        if (text == "**=") { return op::pow; }
        ok = false;
        return op::add;
    }

    void compile_assign(const vp::node & n, std::uint8_t dst) {
        const vp::node & target = at(n.a);
        const std::uint32_t mark = reg_mark();
        const reference ref = prepare_reference(target);

        if (n.text == "=") {
            compile_expr(n.b, dst);
            emit_store(ref, dst);
            release_to(mark);
            return;
        }

        // Logical assignment short-circuits: `a ||= b` must not evaluate b when
        // a is already truthy, which is the whole reason it exists.
        if (n.text == "||=" || n.text == "&&=" || n.text == "?\?=") {
            emit_load(ref, dst);
            const op test = n.text == "&&="    ? op::jump_if_false
                            : n.text == "?\?=" ? op::jump_if_not_nullish
                                               : op::jump_if_true;
            const std::size_t skip = proto().emit(instruction{test, dst});
            compile_expr(n.b, dst);
            emit_store(ref, dst);
            patch_here(skip);
            release_to(mark);
            return;
        }

        bool known = false;
        const op operation = compound_op(n.text, known);
        if (!known) {
            fail("unsupported assignment operator '" + std::string{n.text} + "'");
            release_to(mark);
            return;
        }
        const std::uint8_t rhs = alloc_reg();
        emit_load(ref, dst);
        compile_expr(n.b, rhs);
        proto().emit(instruction{operation, dst, dst, rhs});
        emit_store(ref, dst);
        release_to(mark);
    }

    void compile_update(const vp::node & n, std::uint8_t dst) {
        const vp::node & target = at(n.a);
        const std::uint32_t mark = reg_mark();
        // Through the same reference machinery as compound assignment, so
        // `obj.n++` and `a[i]++` work and evaluate their target exactly once.
        const reference ref = prepare_reference(target);
        const std::uint8_t cur = alloc_reg();
        const std::uint8_t one = alloc_reg();
        emit_load(ref, cur);
        emit_const(one, value::number(1));
        // postfix yields the OLD value, prefix the new one
        if (n.b == 0) { proto().emit(instruction{op::move, dst, cur}); }
        proto().emit(instruction{n.text == "++" ? op::add : op::sub, cur, cur, one});
        if (n.b != 0) { proto().emit(instruction{op::move, dst, cur}); }
        emit_store(ref, cur);
        release_to(mark);
    }

    void compile_ternary(const vp::node & n, std::uint8_t dst) {
        const std::uint32_t mark = reg_mark();
        const std::uint8_t cond = alloc_reg();
        compile_expr(n.a, cond);
        const std::size_t to_alt = proto().emit(instruction{op::jump_if_false, cond});
        release_to(mark);
        compile_expr(n.b, dst);
        const std::size_t to_end = proto().emit(instruction{op::jump});
        patch_here(to_alt);
        compile_expr(n.c, dst);
        patch_here(to_end);
    }

    // Calls need their arguments in CONSECUTIVE registers starting just above
    // the callee, so the VM can hand the callee a contiguous frame.
    // A template literal. The parser hands the WHOLE thing back as one token,
    // backticks and all, so the splitting happens here: literal chunks are
    // strings, `${...}` chunks are parsed and compiled, and the whole thing is
    // a chain of concatenations.
    void compile_template(const vp::node & n, std::uint8_t dst) {
        std::string_view raw = n.text;
        if (raw.size() >= 2 && raw.front() == '`' && raw.back() == '`') {
            raw = raw.substr(1, raw.size() - 2);
        }
        const std::uint32_t mark = reg_mark();
        const std::uint8_t piece = alloc_reg();
        bool started = false;

        const auto append = [&](std::uint8_t src) {
            if (!started) {
                proto().emit(instruction{op::move, dst, src});
                started = true;
            } else {
                proto().emit(instruction{op::concat, dst, dst, src});
            }
        };
        const auto append_literal = [&](std::string text) {
            if (text.empty() && started) { return; }
            emit_string(piece, std::move(text));
            append(piece);
        };

        std::string literal;
        for (std::size_t i = 0; i < raw.size();) {
            if (raw[i] == '\\' && i + 1 < raw.size()) {
                // One level of escape handling, so `\n` and `\`` behave.
                const char c = raw[i + 1];
                literal += c == 'n' ? '\n' : (c == 't' ? '\t' : (c == 'r' ? '\r' : c));
                i += 2;
                continue;
            }
            if (raw[i] == '$' && i + 1 < raw.size() && raw[i + 1] == '{') {
                append_literal(std::move(literal));
                literal.clear();
                // Find the matching brace, counting nesting so an object
                // literal or a nested template inside the hole survives.
                std::size_t depth = 1;
                std::size_t at_char = i + 2;
                const std::size_t start = at_char;
                while (at_char < raw.size() && depth > 0) {
                    if (raw[at_char] == '{') { ++depth; }
                    if (raw[at_char] == '}') { --depth; }
                    if (depth > 0) { ++at_char; }
                }
                compile_foreign_expr(raw.substr(start, at_char - start), piece);
                // Concatenation is what stringifies the value, which is exactly
                // the coercion a template literal performs.
                append(piece);
                i = at_char + 1;
                continue;
            }
            literal += raw[i++];
        }
        append_literal(std::move(literal));
        if (!started) { emit_string(dst, std::string{}); }
        release_to(mark);
    }

    // `dst` = the object `super` looks properties up on: the prototype ABOVE the
    // one the running method was written into.
    void emit_super_base(std::uint8_t dst) {
        proto().emit(instruction{op::load_home, dst});
        proto().emit(instruction{op::get_proto, dst, dst});
    }

    void compile_call(const vp::node & n, std::uint8_t dst) {
        const std::vector<std::int32_t> args = kids(n);
        const vp::node & callee = at(n.a);
        const std::uint32_t mark = reg_mark();
        const std::uint8_t base = alloc_reg();

        const bool super_method = callee.kind == vp::nk::member && callee.a >= 0 &&
                                  at(callee.a).kind == vp::nk::super_lit;
        if (callee.kind == vp::nk::super_lit || super_method) {
            // `super(...)` is the parent CONSTRUCTOR run against this same
            // object - it does not make a new one - so the receiver is `this`
            // in both forms.
            emit_super_base(base);
            const std::string name = super_method ? std::string{callee.text} : "constructor";
            proto().emit(instruction{op::get_prop, base, base, name_operand(name)});
            for (const std::int32_t arg : args) { compile_expr(arg, alloc_reg()); }
            const std::uint8_t self = alloc_reg();
            proto().emit(instruction{op::load_this, self});
            proto().emit(
                instruction{op::call_receiver, base, static_cast<std::uint8_t>(args.size()), self});
            proto().emit(instruction{op::move, dst, base});
            release_to(mark);
            return;
        }

        if (callee.kind == vp::nk::member) {
            compile_expr(callee.a, base); // the receiver
            for (const std::int32_t arg : args) { compile_expr(arg, alloc_reg()); }
            const std::uint8_t name = name_operand(std::string{callee.text});
            proto().emit(
                instruction{op::call_method, base, static_cast<std::uint8_t>(args.size()), name});
        } else if (callee.kind == vp::nk::index) {
            // `obj[name](...)` is a METHOD call: the receiver is obj. Compiling
            // it as a plain call leaves `this` undefined inside the method.
            compile_expr(callee.a, base); // the receiver
            const std::uint8_t key = alloc_reg();
            compile_expr(callee.b, key);
            for (const std::int32_t arg : args) { compile_expr(arg, alloc_reg()); }
            proto().emit(
                instruction{op::call_computed, base, static_cast<std::uint8_t>(args.size()), key});
        } else {
            compile_expr(n.a, base);
            for (const std::int32_t arg : args) { compile_expr(arg, alloc_reg()); }
            proto().emit(instruction{op::call, base, static_cast<std::uint8_t>(args.size())});
        }
        proto().emit(instruction{op::move, dst, base});
        release_to(mark);
    }

    // `new C(...)`. The receiver is created by the VM, which also has to decide
    // what the expression evaluates to - the new object, unless the constructor
    // returned one of its own.
    void compile_new(const vp::node & n, std::uint8_t dst) {
        const std::vector<std::int32_t> args = kids(n);
        const std::uint32_t mark = reg_mark();
        const std::uint8_t base = alloc_reg();
        compile_expr(n.a, base);
        for (const std::int32_t arg : args) { compile_expr(arg, alloc_reg()); }
        proto().emit(instruction{op::construct, base, static_cast<std::uint8_t>(args.size())});
        proto().emit(instruction{op::move, dst, base});
        release_to(mark);
    }

    // Optional chaining. The whole point is the SHORT CIRCUIT: `a?.b.c` yields
    // undefined without evaluating `.c` when a is null-ish, so writing it as an
    // ordinary member access with a test afterwards would still crash.
    void compile_optional(const vp::node & n, std::uint8_t dst) {
        const std::uint32_t mark = reg_mark();
        const std::uint8_t object = alloc_reg();
        compile_expr(n.a, object);

        // null and undefined both short-circuit; nothing else does.
        const std::uint8_t nullish = alloc_reg();
        proto().emit(instruction{op::load_null, nullish});
        const std::uint8_t test = alloc_reg();
        proto().emit(instruction{op::loose_equal, test, object, nullish});
        const std::size_t skip = proto().emit(instruction{op::jump_if_true, test});

        if (n.kind == vp::nk::opt_member) {
            proto().emit(instruction{op::get_prop, dst, object, name_operand(std::string{n.text})});
        } else if (n.kind == vp::nk::opt_index) {
            const std::uint8_t key = alloc_reg();
            compile_expr(n.b, key);
            proto().emit(instruction{op::get_index, dst, object, key});
        } else { // opt_call
            const std::vector<std::int32_t> args = kids(n);
            const std::uint8_t base = alloc_reg();
            proto().emit(instruction{op::move, base, object});
            for (const std::int32_t arg : args) { compile_expr(arg, alloc_reg()); }
            proto().emit(instruction{op::call, base, static_cast<std::uint8_t>(args.size())});
            proto().emit(instruction{op::move, dst, base});
        }
        const std::size_t done = proto().emit(instruction{op::jump});
        patch_here(skip);
        proto().emit(instruction{op::load_undef, dst});
        patch_here(done);
        release_to(mark);
    }

    // The comma operator: evaluate everything, yield the last.
    void compile_sequence(const vp::node & n, std::uint8_t dst) {
        const std::vector<std::int32_t> parts = kids(n);
        if (parts.empty()) {
            proto().emit(instruction{op::load_undef, dst});
            return;
        }
        for (const std::int32_t part : parts) { compile_expr(part, dst); }
    }

    // A class.
    //
    // Compiled to what it desugars to: a constructor function plus a prototype
    // object holding the methods, with `new` wiring an instance to that
    // prototype. `extends` chains the prototype objects, which is what makes an
    // inherited method reachable.
    //
    // NOT here: `super(...)` and `super.m()`. A subclass's constructor does not
    // call its parent's, so a class with `extends` inherits METHODS but not
    // construction. That is a real gap, and calling it out beats a `super` that
    // silently does nothing.
    void compile_class(const vp::node & n, std::uint8_t dst) {
        const std::vector<std::int32_t> members = kids(n);
        const std::uint32_t mark = reg_mark();
        const std::uint8_t prototype_reg = alloc_reg();
        proto().emit(instruction{op::new_object, prototype_reg});

        if (n.a >= 0) {
            // `extends`: the parent's prototype becomes this one's, so a lookup
            // that misses here walks up to it.
            const std::uint8_t parent = alloc_reg();
            compile_expr(n.a, parent);
            const std::uint8_t parent_proto = alloc_reg();
            proto().emit(
                instruction{op::get_prop, parent_proto, parent, name_operand("prototype")});
            proto().emit(instruction{op::set_proto, prototype_reg, parent_proto});
        }

        std::int32_t constructor_body = -1;
        for (const std::int32_t member : members) {
            const vp::node & m = at(member);
            if (m.c == 1 && m.text == "constructor") { constructor_body = m.b; }
        }

        if (constructor_body >= 0) {
            compile_expr(constructor_body, dst);
        } else {
            // A class with no constructor still needs a callable, or `new` has
            // nothing to invoke.
            compile_foreign_expr("(function () {})", dst);
        }
        proto().emit(instruction{op::set_prop, dst, name_operand("prototype"), prototype_reg});
        // `C.prototype.constructor === C`, which is both what pages expect and
        // how `super(...)` finds the parent constructor to call.
        proto().emit(instruction{op::set_prop, prototype_reg, name_operand("constructor"), dst});
        // The constructor's home is this class's prototype, so `super(...)`
        // inside it starts one level up.
        proto().emit(instruction{op::set_prop, dst, name_operand("__home"), prototype_reg});

        const std::uint8_t slot = alloc_reg();
        for (const std::int32_t member : members) {
            const vp::node & m = at(member);
            if (m.text == "constructor" && m.c == 1) { continue; }
            if (m.b < 0) { continue; }
            compile_expr(m.b, slot);
            const std::uint8_t name = name_operand(std::string{m.text});
            // A static member goes on the constructor; everything else on the
            // prototype, where instances find it.
            const std::uint8_t target = (m.d & 1) != 0 ? dst : prototype_reg;
            proto().emit(instruction{op::set_prop, target, name, slot});
            // Each method remembers where it was WRITTEN. `super.m()` resolves
            // against that, not against `this` - in a three-deep hierarchy the
            // two differ and resolving against `this` calls the same method
            // forever.
            if (m.c == 1) {
                proto().emit(instruction{op::set_prop, slot, name_operand("__home"), target});
            }
        }
        release_to(mark);
    }

    void compile_array(const vp::node & n, std::uint8_t dst) {
        proto().emit(instruction{op::new_array, dst});
        const std::uint32_t mark = reg_mark();
        for (const std::int32_t element : kids(n)) {
            const std::uint8_t v = alloc_reg();
            if (at(element).kind == vp::nk::spread) {
                // `[...a]` appends a's ELEMENTS, not a itself. Compiled as an
                // index loop rather than an opcode, because that is all it is.
                compile_expr(at(element).a, v);
                emit_append_all(dst, v);
            } else {
                compile_expr(element, v);
                proto().emit(instruction{op::append, dst, v});
            }
            release_to(mark);
        }
    }

    // Append every element of `source` to the array in `target`.
    void emit_append_all(std::uint8_t target, std::uint8_t source) {
        const std::uint32_t mark = reg_mark();
        const std::uint8_t length = alloc_reg();
        proto().emit(instruction{op::get_prop, length, source, name_operand("length")});
        const std::uint8_t index = alloc_reg();
        emit_const(index, value::number(0));
        const std::uint8_t one = alloc_reg();
        emit_const(one, value::number(1));
        const std::uint8_t test = alloc_reg();
        const std::uint8_t item = alloc_reg();
        const std::size_t top = proto().code.size();
        proto().emit(instruction{op::less, test, index, length});
        const std::size_t exit = proto().emit(instruction{op::jump_if_false, test});
        proto().emit(instruction{op::get_index, item, source, index});
        proto().emit(instruction{op::append, target, item});
        proto().emit(instruction{op::add, index, index, one});
        patch_jump(proto().emit(instruction{op::jump}), top);
        patch_here(exit);
        release_to(mark);
    }

    void compile_object(const vp::node & n, std::uint8_t dst) {
        proto().emit(instruction{op::new_object, dst});
        const std::uint32_t mark = reg_mark();
        for (const std::int32_t p : kids(n)) {
            const vp::node & prop = at(p);
            if (prop.kind == vp::nk::spread) {
                // `{...o}` copies o's own properties in, and a later key still
                // wins - which is why this is a copy at this point in the
                // sequence rather than a merge at the end.
                const std::uint8_t source = alloc_reg();
                compile_expr(prop.a, source);
                proto().emit(instruction{op::copy_props, dst, source});
                release_to(mark);
                continue;
            }
            if (prop.kind != vp::nk::prop) {
                fail("unsupported object literal member");
                return;
            }
            if (prop.c == 3) {
                // get/set accessors need a property that runs code on read,
                // which this VM's objects do not have.
                fail("object literal get/set accessors are not in this VM subset");
                return;
            }
            const std::uint8_t v = alloc_reg();
            if (prop.b >= 0) {
                compile_expr(prop.b, v);
            } else {
                compile_ident(prop, v); // shorthand { x }
            }
            // A computed key - `{[k]: v}`, and also `{"a": v}` and `{1: v}`,
            // which the parser routes the same way so quotes and escapes get
            // cooked by evaluating the literal.
            if ((prop.d & 1) != 0) {
                const std::uint8_t key = alloc_reg();
                compile_expr(prop.a, key);
                proto().emit(instruction{op::set_index, dst, key, v});
            } else {
                const std::uint8_t name = name_operand(decode_string_literal(prop.text));
                proto().emit(instruction{op::set_prop, dst, name, v});
            }
            release_to(mark);
        }
    }

    // --- helpers -------------------------------------------------------------
    void emit_string(std::uint8_t dst, std::string text) {
        proto().emit(
            instruction::with_bx(op::load_string, dst, proto().add_string(std::move(text))));
    }
    void emit_const(std::uint8_t dst, value v) {
        proto().emit(instruction::with_bx(op::load_const, dst, proto().add_constant(v)));
    }

    void patch_here(std::size_t at_index) { patch_jump(at_index, proto().code.size()); }
    void patch_jump(std::size_t at_index, std::size_t target) {
        const auto offset =
            static_cast<std::int32_t>(target) - static_cast<std::int32_t>(at_index) - 1;
        if (offset > jump_limit || offset < -jump_limit - 1) {
            fail(frame_name(fn().proto) + " needs a jump of " + std::to_string(offset) +
                 " instructions; the displacement holds " + std::to_string(jump_limit) +
                 ". Past that it branches to the WRONG address.");
        }
        instruction & jump = proto().code[at_index];
        const auto narrow = static_cast<std::uint16_t>(static_cast<std::int16_t>(offset));
        jump.b = static_cast<std::uint8_t>(narrow >> 8);
        jump.c = static_cast<std::uint8_t>(narrow & 0xFF);
    }

    const vp::ast & ast_;
    const vp::ast * current_ast_ = &ast_;
    std::vector<std::unique_ptr<vp::ast>> owned_asts_;
    program & out_;
    std::vector<frame> frames_;
    std::vector<loop_context> loops_;
    std::string pending_label_;
    std::size_t handler_depth_ = 0;
};

} // namespace

program compiler::compile(std::string_view source) {
    const vp::ast tree = vp::parse(source);
    program out;
    if (!tree.ok) {
        out.ok = false;
        out.error = "parse error: " + std::string{tree.error};
        return out;
    }
    compiler_impl c{tree, out};
    c.compile_program();
    return out;
}

} // namespace ctbrowser::script
