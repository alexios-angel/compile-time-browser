//===- checker.hpp - Early-error checker ------------------------*- C++ -*-===//
//
// The early-error checker: one walk over the AST, before a single instruction
// is emitted. early_errors.hpp says what an early error is; this class holds
// the rules.
//
// Every rule cites its ECMA-262 clause, because a false positive costs a page
// that does not run: this engine compiles p5.js, Phaser, Babylon.js and
// Bootstrap out of vendor/ as ratchet tests, and any one of them refusing to
// compile is a red suite. So a construct is refused only where the
// specification says it must be, and a strict-mode rule only in strict code.
//
// The walk is structural rather than generic. A generic walk over a node's
// four fixed slots is unsafe on its own, because the parser reuses `c` and
// `d` as bitfields on the kinds that carry flags (`update` keeps
// prefix/postfix in `b`, which as a node index is node 1). So `child_slots()`
// (compile/child_slots.hpp, the same table the compiler's walks use) says
// which slots are really children, and every construct that opens a scope is
// handled by name.
//
// Private to lib/Script/compile/early_errors/: early_errors.hpp declares the
// public find_early_error(), and this header exists so the checker can be
// split across several .cpp files. The class has external linkage because an
// anonymous namespace cannot be shared through a header. The AST accessors
// stay inline on purpose; docs/architecture.md measures the cost of
// out-of-lining the compiler's equivalent `at()`.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "../early_errors.hpp"

#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ctbrowser::script::detail::early {

namespace vp = ctjs::vp;
using vp::nk;

/// How a name was introduced. The distinction that matters is lexical
/// (let/const/class, and a function declaration inside a block) against var,
/// because every redeclaration rule is stated between those two sets.
enum class binding_kind : std::uint8_t {
    var,
    let_,
    const_,
    class_,
    function_
};

/// One declared name and the node that declares it.
struct binding {
    std::string_view name;
    binding_kind how = binding_kind::var;
    std::int32_t node = -1;
};

/// What kind of statement list is being walked, which decides where a
/// function declaration's name goes. At the top level of a script or a
/// function body a FunctionDeclaration is var-scoped
/// (TopLevelVarDeclaredNames, 8.2.6); inside a block or a case block it is
/// lexically scoped (14.2.1). The whole of the redeclaration behaviour follows
/// from that one difference.
enum class list_kind : std::uint8_t {
    script,
    function_body,
    block
};

/// The kind a statement nested inside another statement is walked with. Only
/// one thing turns on it (whether a function declaration's name is
/// var-declared here), and a declaration that is the whole body of an `if` or
/// a loop declares nothing this pass may draw a conclusion from.
inline constexpr list_kind nested = list_kind::block;

/// The kind of the enclosing function-shaped construct. `return`, `break`,
/// `continue`, a label, `new.target` and `super` are all answered against the
/// frame stack and nothing else, which is what makes them stop at a function
/// boundary, as the specification's parameterised grammar does.
enum class frame_kind : std::uint8_t {
    /// The top level: no `super`. `return` and `new.target` are the embedding
    /// contract's two deviations and are not refused here unless the source
    /// is a declared Script; see the notes on `nk::return_stmt` and
    /// `nk::new_target`.
    script,
    function,   // A function declaration or expression.
    arrow,      // Transparent to `new.target`, `super` and `this`.
    method,     // A method, accessor or constructor: has a home object.
    field_init, // A class field initialiser, a function of its own.
    /// `static { }` (15.7.1 ClassStaticBlockBody): a function body with a home
    /// object, no `arguments`, no `await`, no `return` and no `super()`.
    static_block,
};

/// The checker's state for one enclosing function-shaped construct.
struct frame {
    frame_kind what = frame_kind::script;
    std::vector<std::string_view> labels;      // Every label in scope in this frame.
    std::vector<std::string_view> loop_labels; // The labels naming an iteration statement.
    int loops = 0;
    int switches = 0;
    /// Whether `super(...)` may appear here. Only the constructor of a class
    /// that has a ClassHeritage allows it. 15.7.1 puts it two ways that come
    /// to the same rule: a MethodDefinition that is not that constructor may
    /// not Contain a SuperCall, and a class with no `extends` may not either.
    bool super_call_ok = false;
    /// Strict mode code (11.2.2): a "use strict" directive here or in an
    /// enclosing frame, a module, or a class body. The rules it turns on are
    /// the ones below that say so: a binding or an assignment target named
    /// `eval`/`arguments`, `yield` and the future reserved words as
    /// identifiers, a duplicate in a simple parameter list, `with`, `delete`
    /// of a plain name, a legacy octal literal.
    bool strict = false;
    /// 15.8.1 / 15.5.1: inside an async function `await` is not an
    /// identifier, and inside a generator `yield` is not, as a binding or a
    /// reference, strict or not. An arrow is its own frame here and only an
    /// async arrow says async: a plain arrow's body is FunctionBody[~Await].
    bool is_async = false;
    bool is_generator = false;
};

/// 13.2.7.1: returns the first thing wrong with a regular expression literal
/// (the lexeme as the lexer kept it, `/body/flags`), or nothing. Defined in
/// regexp.cpp; see the note at the top of that file for what is and is not
/// judged.
[[nodiscard]] std::optional<std::string> regexp_literal_error(std::string_view lexeme);

/// The early-error walk over one AST. Construct it, call run(), then read
/// result().
class checker {
public:
    checker(const vp::ast & tree, std::string_view source, bool strict_root,
            const eval_site * eval = nullptr, bool script_goal = false)
        : ast_(tree), source_(source), strict_root_(strict_root), eval_(eval),
          script_goal_(script_goal) {}

    void run();

    std::optional<early_error> result() const { return found_; }

private:
    // --- The AST, read the way the compiler reads it. ---
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

    /// Returns where a node was written. `node::text` is a view into the
    /// source, so its address minus the source's is the offset; a function
    /// also carries a real span. A node with neither answers `nowhere` rather
    /// than 0, because a wrong line number in a diagnostic costs more than no
    /// line number.
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

    /// Records an error, keeping the first in source order rather than the
    /// first found. The walk collects a scope's declarations before it walks
    /// the scope's statements, so "found first" and "written first" differ,
    /// and the one a person wants to be told about is the earliest in the
    /// file.
    void report(std::string message, std::int32_t node) {
        const std::size_t where = offset_of(node);
        if (cut_short(node, where)) { return; }
        if (found_ && found_->offset <= where) { return; }
        found_ = early_error{std::move(message), where};
    }

    /// Returns whether the lexer stopped reading this name early, the one place
    /// this pass has to distrust its own input.
    ///
    /// ctjs's lexer does not implement `\u` escapes inside an identifier, so
    /// `const implements = 6` arrives as `const impl` followed by
    /// something else entirely: a declaration with no initialiser, which is an
    /// early error about source text nobody wrote. Seven of test262's
    /// future-reserved-words tests have exactly that shape.
    ///
    /// So a lexeme with a backslash immediately after it was cut short, and
    /// nothing may be concluded from it: the parser gap shows up as a missed
    /// error rather than a wrong refusal.
    [[nodiscard]] bool cut_short(std::int32_t node, std::size_t where) const {
        if (where == early_error::nowhere) { return false; }
        const std::size_t after = where + at(node).text.size();
        return after < source_.size() && source_[after] == '\\';
    }

    static std::string quoted(std::string_view name) { return "`" + std::string{name} + "`"; }

    /// Returns whether this lexeme starts an IdentifierName (12.7). The parser
    /// takes
    /// whatever token follows a `.` or fills a catch parameter, so a string
    /// (its text keeps the quotes) or a number arrives there as a name. A
    /// private `#x`, an escape and any non-ASCII lead byte are let through:
    /// only the spelling is judged, and only where it cannot be a name.
    [[nodiscard]] static bool identifier_start(std::string_view text) {
        if (text.empty()) { return false; }
        const auto c = static_cast<unsigned char>(text.front());
        return ((c | 0x20) >= 'a' && (c | 0x20) <= 'z') || c == '$' || c == '_' || c == '\\' ||
               c == '#' || c >= 0x80;
    }

    /// Returns whether this name was spelled with an escape. The lexer decodes
    /// `\u0062reak` into a buffer of its own (vp::decoded_names), so a lexeme
    /// that does not point into the source is one it decoded. 12.7.1: such a
    /// name may not be a reserved word, and only such a name can be judged by
    /// its text alone, because the parser is lenient with a keyword in
    /// expression position (`return\nconst x` reads `const` as the returned
    /// name), and a lenient parse of a valid program must not become a
    /// refusal.
    [[nodiscard]] bool escaped(std::int32_t idx) const {
        const vp::node & n = at(idx);
        if (n.text.empty() || source_.empty()) { return false; }
        const char * first = n.text.data();
        return first < source_.data() || first >= source_.data() + source_.size();
    }

    // --- Defined in scopes.cpp. ---
    void bound_names(std::int32_t idx, binding_kind how, std::vector<binding> & out) const;
    // --- Strict mode. ---
    [[nodiscard]] bool strict() const { return !frames_.empty() && frames_.back().strict; }
    /// Returns whether this source is a module. `strict_root_` is the flag the
    /// compiler sets for one, and the module rules ask this rather than
    /// strict(): a script with a "use strict" directive is strict and is
    /// still not a module.
    [[nodiscard]] bool module_() const { return strict_root_; }
    /// Returns whether `body` (a block or a program) opens with a "use strict"
    /// directive (11.2.1).
    [[nodiscard]] bool has_use_strict_directive(std::int32_t body) const;
    /// 13.1.1: in strict code neither a binding nor an assignment target may
    /// be `eval` or `arguments`, and `yield`, `let`, `static`, `implements`,
    /// `interface`, `package`, `private`, `protected`, `public` are reserved.
    /// `trusted` says the spelling may be judged against the reserved words
    /// too: a binding always is, a reference only when `escaped` (see there).
    void check_strict_binding(std::string_view spelled, std::int32_t node, bool trusted = true);
    /// 12.7.2: the ReservedWords that are never an identifier, in any mode.
    /// The contextual ones (`let`, `static`, `async`, `of`, `get`, `set`,
    /// `yield`, `await`) are not here: they are identifiers in sloppy code,
    /// and the strict and async/generator rules take the rest.
    [[nodiscard]] static bool reserved_word(std::string_view name);
    /// Checks an IdentifierReference: a reserved word is refused where the
    /// spelling can be trusted (see `escaped`, and a shorthand property,
    /// which no leniency reaches), then the strict and contextual rules
    /// apply; `eval`/`arguments` may be read in strict code.
    void check_identifier_reference(std::string_view name, std::int32_t node, bool trusted);
    /// The async and generator rules for a binding or reference named
    /// `await`/`yield`.
    void check_contextual_name(std::string_view name, std::int32_t node);
    void check_strict_bindings(const std::vector<binding> & names);
    void lexical_names(std::span<const std::int32_t> stmts, list_kind kind,
                       std::vector<binding> & out) const;
    [[nodiscard]] static const char * kind_word(binding_kind how);
    std::vector<binding> check_list(std::span<const std::int32_t> stmts, list_kind kind,
                                    const std::vector<binding> * outer, const char * outer_what);
    void check_module_items(std::int32_t root, const std::vector<binding> & vars);

    // --- Defined in statements.cpp. ---
    void walk_statement(std::int32_t idx, list_kind kind, std::vector<binding> & vars);
    [[nodiscard]] bool newline_follows(std::int32_t idx, std::size_t lexeme) const;
    void check_nested_declaration(std::int32_t idx, bool annex_b_function,
                                  bool labelled_item = false);
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

    // --- Defined in functions.cpp. ---
    [[nodiscard]] bool simple_parameters(std::span<const std::int32_t> params) const;
    void check_function(std::int32_t idx, frame_kind what, bool super_call_ok = false);
    void check_class(std::int32_t idx);
    [[nodiscard]] bool heritage_parenthesised(std::int32_t klass) const;
    void check_accessor_arity(std::int32_t fn, bool setter);
    /// 15.7.1 AllPrivateIdentifiersValid: a `#name` is only ever a reference
    /// to a name some enclosing class body declares. `private_names_` is that
    /// stack, one entry per open body, pushed after the heritage is walked
    /// (the heritage sees the outer environment, not the class's own).
    void check_private_reference(std::string_view name, std::int32_t node);

    // --- Defined in expressions.cpp. ---
    [[nodiscard]] bool simple_target(std::int32_t idx) const;
    [[nodiscard]] bool web_compat_call(std::int32_t idx) const;
    [[nodiscard]] static bool destructuring(nk kind);
    void check_assignment(std::int32_t idx);
    void check_update(std::int32_t idx);
    void check_operand(std::int32_t op_node, std::int32_t operand);
    [[nodiscard]] bool names_proto(const vp::node & prop) const;
    void check_proto_duplicates(std::int32_t idx);
    void check_number(std::int32_t idx);
    /// 12.9.4 / 12.9.6: the escapes a string or template literal may carry,
    /// and that a string ends on its own line. `tagged` is a tagged template,
    /// whose invalid escapes are not an error (the cooked value is undefined).
    void check_string(std::int32_t idx, bool is_template, bool tagged);
    void check_delete(std::int32_t operand);
    [[nodiscard]] const frame & enclosing_non_arrow_frame() const;
    [[nodiscard]] frame_kind enclosing_non_arrow() const;
    void walk_expression(std::int32_t idx);
    void walk_property(std::int32_t idx);

    // --- Defined in patterns.cpp. ---
    void check_pattern_target(std::int32_t idx);
    [[nodiscard]] bool comma_follows(std::int32_t target) const;
    void walk_pattern(std::int32_t idx);

    /// How deep this walk may recurse.
    ///
    /// The walk recurses once per level of nesting, and ctjs's expression
    /// parser does not (its Pratt loop is iterative), so a minifier's
    /// `"a" + "b" + ... ` chain of fifty thousand terms parses into a tree
    /// fifty thousand deep that nothing has recursed over yet. This pass is the
    /// first to recurse over it, and would be the first to overflow the C++
    /// stack.
    ///
    /// The compiler recurses over the same shape immediately afterwards, with
    /// three larger frames per term, so any input that overflows here would
    /// crash the process a moment later (measured: this walk survives 20,000
    /// terms and dies before 50,000). The guard does not fix that; it keeps
    /// this pass from being what appears to break a page that was already too
    /// deep to compile. Past the limit the subtree is simply not checked,
    /// which is a missed error and never a refusal.
    static constexpr int max_depth = 2000;

    /// RAII guard that counts one level of recursion in `depth_`.
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

    /// What a class body has already bound under one private name. 15.7.1
    /// lets a name appear at most twice: once as a getter and once as a
    /// setter, both static or both not. Anything else is a duplicate.
    struct private_name {
        std::string_view name;
        std::size_t seen = 0;
        bool getter = false;
        bool setter = false;
        bool is_static = false;
        bool other = false; // A field or a method: anything that is not an accessor.
    };

    const vp::ast & ast_;
    std::string_view source_;
    int depth_ = 0;
    std::vector<frame> frames_;
    bool strict_root_ = false; // A module: strict code from the first line.
    /// For an eval's source, where the eval was (19.2.1.1 steps 4-7): its top
    /// level behaves as that place, not as a script; see run(). Null otherwise.
    const eval_site * eval_ = nullptr;
    /// True for an ECMAScript Script, not the embedder's program (see
    /// find_early_error): the top level is [~Return] and may not contain
    /// `new.target`.
    bool script_goal_ = false;
    std::size_t class_depth_ = 0; // Inside a class body: strict code (15.7.1).
    std::vector<std::vector<std::string_view>> private_names_; // See check_private_reference.
    /// True while walking a formal parameter list of the innermost frame:
    /// `yield` and `await` expressions are not allowed there (15.5.1, 15.8.1:
    /// FormalParameters[~Yield] and [~Await] for the function's own kind).
    bool in_parameters_ = false;
    std::optional<early_error> found_;
};

} // namespace ctbrowser::script::detail::early
