#pragma once
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
// in `b`, which as a node index is node 1 - so `slots()` (in expressions.cpp) says which slots
// of which kinds are really children, and every construct that opens a SCOPE is
// handled by name.
//
// PRIVATE to lib/Script/compile/early_errors/, and in no file set: early_errors.hpp
// beside the directory declares find_early_error() whole, and this exists only so
// the checker can be more than one file - it was 1,476 lines in one until
// 2026-09-08. The class has external linkage for the reason compiler_impl does:
// an anonymous namespace cannot be shared through a header. The AST-reading
// accessors stay inline here on purpose - docs/architecture.md measured what
// out-of-lining the compiler's `at()` cost, and this is the same function.

#include "../early_errors.hpp"

#include <array>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ctbrowser::script::detail::early {

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
    // The top level: no `super`. `return` and `new.target` are the embedding
    // contract's two deviations and are NOT refused here - see the notes on
    // `nk::return_stmt` and `nk::new_target`.
    script,
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

class checker {
public:
    checker(const vp::ast & tree, std::string_view source) : ast_(tree), source_(source) {}

    void run();

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

    // --- defined in scopes.cpp ------------------------------------------------------------
    void bound_names(std::int32_t idx, binding_kind how, std::vector<binding> & out) const;
    void lexical_names(std::span<const std::int32_t> stmts, list_kind kind,
                       std::vector<binding> & out) const;
    [[nodiscard]] static const char * kind_word(binding_kind how);
    std::vector<binding> check_list(std::span<const std::int32_t> stmts, list_kind kind,
                                    const std::vector<binding> * outer, const char * outer_what);

    // --- defined in statements.cpp --------------------------------------------------------
    void walk_statement(std::int32_t idx, list_kind kind, std::vector<binding> & vars);
    [[nodiscard]] bool newline_follows(std::int32_t idx, std::size_t lexeme) const;
    void check_nested_declaration(std::int32_t idx, bool annex_b_function);
    void walk_loop_body(std::int32_t body, std::vector<binding> & vars);
    void check_declaration(std::int32_t idx, std::vector<binding> & vars);
    void check_for(std::int32_t idx, list_kind kind, std::vector<binding> & vars);
    void check_for_in_of(std::int32_t idx, std::vector<binding> & vars);
    void check_switch(std::int32_t idx, std::vector<binding> & vars);
    void check_try(std::int32_t idx, std::vector<binding> & vars);
    [[nodiscard]] bool labels_iteration(std::int32_t idx) const;
    void check_labeled(std::int32_t idx, std::vector<binding> & vars);
    void check_break(std::int32_t idx);
    void check_continue(std::int32_t idx);

    // --- defined in functions.cpp ---------------------------------------------------------
    [[nodiscard]] bool simple_parameters(std::span<const std::int32_t> params) const;
    void check_function(std::int32_t idx, frame_kind what, bool super_call_ok = false);
    void check_class(std::int32_t idx);

    // --- defined in expressions.cpp -------------------------------------------------------
    [[nodiscard]] bool simple_target(std::int32_t idx) const;
    [[nodiscard]] static bool destructuring(nk kind);
    void check_assignment(std::int32_t idx);
    void check_update(std::int32_t idx);
    [[nodiscard]] bool names_proto(const vp::node & prop) const;
    [[nodiscard]] bool bracketed(std::int32_t key) const;
    void check_proto_duplicates(std::int32_t idx);
    void check_number(std::int32_t idx);
    void check_delete(std::int32_t operand);
    [[nodiscard]] const frame & enclosing_non_arrow_frame() const;
    [[nodiscard]] frame_kind enclosing_non_arrow() const;
    void walk_expression(std::int32_t idx);
    void walk_property(std::int32_t idx);

    // --- defined in patterns.cpp ----------------------------------------------------------
    void check_pattern_target(std::int32_t idx);
    void walk_pattern(std::int32_t idx);

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

    const vp::ast & ast_;
    std::string_view source_;
    int depth_ = 0;
    std::vector<frame> frames_;
    std::optional<early_error> found_;
};

} // namespace ctbrowser::script::detail::early
