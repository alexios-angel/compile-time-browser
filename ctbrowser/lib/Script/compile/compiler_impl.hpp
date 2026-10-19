//===- compiler_impl.hpp - The bytecode compiler ----------------*- C++ -*-===//
//
// The compiler proper (AST to bytecode), declared here so its member bodies
// can live in many .cpp files under lib/Script/compile/. Private to that
// directory and deliberately not under include/: it includes
// <ctjs/vparse.hpp> and Boost, and "no third-party header in a public header"
// is policed by test/lint/api_surface.
//
// A named namespace, not an anonymous one, because an anonymous namespace
// cannot be shared through a header. `detail` rather than plain `script`
// because `frame`, `local`, `interval` and `reference` are generic enough to
// collide with anything (vm.hpp really has a call frame). The state types
// live in compiler_impl/types.hpp and have nested aliases here.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/core/number_format.hpp>
#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/compile.hpp>

#include <boost/container/small_vector.hpp>

#include <algorithm>
#include <ranges>
#include <span>

#include <array>
#include <charconv>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ctjs/vparse.hpp>

#include "child_slots.hpp"
#include "compiler_impl/types.hpp"

#include <ctbrowser/script/value.hpp>

namespace ctbrowser::script {

// The debug side tables (function_proto::locals and code_offsets) are a build
// option, CTBROWSER_SCRIPT_DEBUG_NAMES, on by default. When it is off, nothing
// is populated, `emit_offset` never leaves `no_offset`, and both vectors stay
// empty on every proto. Every reader must already handle empty tables,
// because an image loader produces them.
#ifndef CTBROWSER_SCRIPT_DEBUG_NAMES
#define CTBROWSER_SCRIPT_DEBUG_NAMES 1
#endif

// At `script` scope because compile.cpp names `vp::ast` and `vp::parse` too.
namespace vp = ctjs::vp;

namespace detail {

/// Compiles one parsed program (or eval source) into a `program` of
/// function_protos. One instance per compilation; its members are spread over
/// the .cpp files of this directory.
class compiler_impl {
public:
    using local = compiler_types::local;
    /// A half-open range of Euler-tour ticks. A function's descendants are
    /// exactly the functions whose tick lies strictly inside its own range.
    using interval = compiler_types::interval;

    using frame = compiler_types::frame;

    compiler_impl(const vp::ast & tree, program & out)
        : ast_(tree), current_ast_(&tree), out_(out) {}

    /// True when the top level is a module's: a scope of its own, not the
    /// global object. Set by compiler::compile from the script_kind; see the
    /// note on that enum for why the distinction matters.
    bool module_scope_ = false;
    /// How many class bodies are open: everything inside one is strict code
    /// (15.7.1 note), whatever the surrounding script says.
    std::size_t class_body_depth_ = 0;
    /// The private names in scope (15.7.1 PrivateBoundIdentifiers): one entry
    /// per open class body, the names it declares and the class's own number.
    /// A reference resolves to the innermost body declaring it, so `#x` in
    /// two classes, nested or not, is two keys, `@#x:1` and `@#x:2`, and an
    /// inner class's `#x` never reads an outer instance's. See member_operand.
    struct private_scope {
        std::vector<std::string_view> names;
        std::size_t klass = 0;
        /// Inside a direct eval, the caller's names come with their keys,
        /// parallel to `names`, because the caller numbered its classes, not
        /// this program.
        std::vector<std::string_view> keys;
    };
    std::vector<private_scope> private_scopes_;
    std::size_t private_classes_ = 0;
    /// Returns whether `body` (a block or a program) opens with a "use strict"
    /// directive (11.2.1): a leading expression statement that is exactly
    /// that string literal, before any other statement.
    [[nodiscard]] bool has_use_strict_directive(std::int32_t body) const;
    /// Set for `eval` sources, whose completion value is the program's return
    /// value. See compiler::compile_for_eval.
    bool completion_value_ = false;
    /// The completion register of an eval program (-1 when not tracking): the
    /// value the last non-empty statement produced, as 14.2.2 StatementList and
    /// UpdateEmpty define it. An expression statement at the program's frame
    /// writes it; an `if`, a loop, a `switch`, a `try` or a `with` clears it on
    /// entry (their completion is UpdateEmpty(_, undefined)), and a `finally`
    /// block's statements do not touch it (a normal finally keeps the try's
    /// value, 14.15.3). Declarations and blocks are empty and leave it alone.
    int completion_reg_ = -1;
    int completion_suspended_ = 0; // Greater than 0 inside a finally block.
    /// Returns whether statements in the current frame update completion_reg_.
    [[nodiscard]] bool tracking_completion() const;
    /// Called when the statement about to be compiled starts a construct whose
    /// completion is UpdateEmpty(_, undefined): clears the register.
    void clear_completion();

    /// The bytes the AST was parsed from, which are not `out_.source`.
    ///
    /// `compiler::compile` copies the source into the program, so the copy's
    /// characters are at different addresses from the ones every `node::text`
    /// views. Offsets are the same in both, and this is the buffer they are
    /// offsets into, so it is the one a pointer subtraction may use.
    ///
    /// Empty when the caller did not provide it, which turns `offset_of` off
    /// rather than producing wrong line numbers.
    std::string_view source_view_{};

    /// Returns where a node was written, as a byte offset into `source_view_`,
    /// or `function_proto::no_offset` when this node cannot answer.
    ///
    /// `node::begin` is set for functions only, so the offset of an ordinary
    /// statement comes from the lexeme: `node::text` is a std::string_view into
    /// the source, so the address of its first character minus the address of
    /// the source is the offset (the same subtraction `vp::parser::offset_at`
    /// makes).
    [[nodiscard]] std::uint32_t offset_of(std::int32_t idx) const;

    /// RAII guard that sets the emit cursor (source offset) for one subtree and
    /// restores it afterwards.
    ///
    /// Restoring matters: in `a + b` the `add` instruction is emitted after both
    /// operands, and without a restore it would be attributed to `b`. With one,
    /// the operands carry their own positions and the operator carries the
    /// position of the operator.
    ///
    /// It holds the compiler and not the proto, deliberately. `out_.functions`
    /// is a vector that grows while a nested function is compiled, so a
    /// `function_proto &` taken here would dangle across any function
    /// expression in the subtree this guard covers.
    class at_source {
    public:
        at_source(compiler_impl & c, std::int32_t node) : owner_(c), saved_(c.proto().emit_offset) {
            const std::uint32_t to = c.offset_of(node);
            if (to != function_proto::no_offset) { c.proto().emit_offset = to; }
        }
        ~at_source() { owner_.proto().emit_offset = saved_; }
        at_source(const at_source &) = delete;
        at_source & operator=(const at_source &) = delete;
        at_source(at_source &&) = delete;
        at_source & operator=(at_source &&) = delete;

    private:
        compiler_impl & owner_;
        std::uint32_t saved_;
    };

    // --- AST access. ---

    /// Returns node `i` of the active AST. A negative index means "nothing",
    /// not an address: every fixed child slot is -1 when absent, and an array
    /// literal's element list holds -1 for a hole. Returning an empty node makes
    /// a missed check compile to nothing instead of reading past the pool.
    ///
    /// Defined inline on purpose: out of line, this one function measured 4.18%
    /// of a whole page render. `static const vp::node nothing` is one object
    /// across every including file, as a function-local static in an inline
    /// function is guaranteed to be.
    [[nodiscard]] const vp::node & at(std::int32_t i) const {
        static const vp::node nothing{vp::nk::empty, ""};
        if (i < 0 || static_cast<std::size_t>(i) >= current_ast_->nodes.size()) { return nothing; }
        return current_ast_->nodes[static_cast<std::size_t>(i)];
    }

    /// Compiles an expression parsed from a different source than the program's.
    /// Template literals need it: the parser hands back `${...}` as raw text
    /// inside one token. Node indices are per-AST, so the active one is swapped
    /// for the duration and every `at()` follows it.
    void compile_foreign_expr(std::string_view source, std::uint16_t dst);
    /// The same, for source the compiler built rather than one pointing into
    /// the program. The text is kept alive because the AST borrows it.
    void compile_owned_expr(std::string source, std::uint16_t dst);

    /// Returns `n`'s list children as a view, not a copy: the children are
    /// already contiguous, and a vector per node visit cost 3.9% of rendering
    /// a page.
    ///
    /// It reads the active AST, not the outer one: a node reached inside a
    /// template literal's sub-AST indexes that AST's pool, and reading the
    /// program's would hand back unrelated nodes.
    ///
    /// Safe because compilation only reads the AST: nothing pushes to the pool
    /// while a span into it is alive, and a template's sub-AST is a separate
    /// object whose pool this one never reallocates.
    [[nodiscard]] std::span<const std::int32_t> kids(const vp::node & n) const;

    // --- Frames and registers. ---

    /// The innermost frame being compiled.
    [[nodiscard]] frame & fn();
    /// The function_proto of the innermost frame.
    [[nodiscard]] function_proto & proto();

    /// Allocates the next register. Truncating to 16 bits is correct here and
    /// the overflow is caught once, at finish_frame, where high_water knows the
    /// real total. Failing on the first register past the limit would report
    /// the limit every time; what a person needs to hear is that the function
    /// wanted, say, 1,452.
    [[nodiscard]] std::uint16_t alloc_reg();
    void release_to(std::uint32_t mark);
    [[nodiscard]] std::uint32_t reg_mark() const;

    void push_scope();
    void pop_scope();

    /// add_local and shrink_locals are the only two places `locals` changes
    /// size, so that `local_index` cannot drift out of step with it. Every
    /// declaration goes through add_local and every scope exit through
    /// shrink_locals; a bare `locals.push_back` would leave a name the index
    /// cannot find, which is a variable that silently becomes a global read.
    void add_local(frame & f, local l);
    /// Unwinds from the top, which is what makes `pop_back` on each name's
    /// stack the right inverse: entries went on in increasing position order,
    /// so the highest position is the last one pushed for its name.
    ///
    /// Non-static: closing a local's live range needs the frame's proto, and a
    /// `frame` stores only the proto's index.
    void shrink_locals(frame & f, std::size_t mark);
    /// Called when a local has just gone out of scope: stamps the end of its
    /// live range and the boxedness it finished with. Idempotent (closing twice
    /// at the same program counter writes the same numbers), which lets both
    /// `finish_frame` and `pop_scope` do it without either having to know
    /// whether the other already did.
    void close_local(const frame & f, const local & l);

    /// Appends a new, empty proto to `out_.functions` and returns its index.
    ///
    /// It exists so that arming the debug tables happens in exactly one place.
    /// `function_proto::code_offsets` may only be empty or exactly parallel to
    /// `code`, and which of the two it is is a decision per function.
    ///
    /// `at_offset` is where the cursor starts: the function's own first byte.
    /// The prologue, the parameter defaults and the implicit `return undefined`
    /// are emitted with no statement in hand, and attributing them to the
    /// function's opening is both true and what a debugger wants: it is where
    /// a breakpoint on entry belongs.
    [[nodiscard]] std::uint32_t new_proto(std::uint32_t at_offset);
    [[nodiscard]] std::uint16_t declare_local(std::string name);
    /// Binds a local to its export cell at module entry, not at the
    /// declaration. The cell belongs to the module record and the loader
    /// creates it before anything in the graph runs, so what this emits is an
    /// adoption: the local's register becomes the record's cell, and every
    /// later write in this module is a write the importer reads.
    ///
    /// Binding at entry is required for cycles: when A imports B imports A, B
    /// runs first and asks A for a binding A has not reached the declaration
    /// of. Creating the binding early and leaving it undefined is what the
    /// specification does.
    ///
    /// Always boxed, captured or not: every read has to go through the cell.
    void bind_export(const std::string & name, std::uint16_t reg);

    /// Binds every name an `import` statement introduces, at module entry, for
    /// the same reason bind_export runs there. Binding at the statement would
    /// put the `load_import` after the function-declaration pass, where a
    /// module's closures are made, so a function using an imported name would
    /// capture the register's placeholder cell rather than the exporter's.
    void bind_imports(std::int32_t idx);

    /// The name the specification gives `export default`, deliberately not a
    /// legal identifier: nothing in the module can name it, and it still
    /// hoists and binds like any other export.
    static constexpr std::string_view default_binding = "*default*";

    /// Records the re-export edges a top-level statement declares.
    /// `export { a as b } from './m.js'` and `export * from './m.js'` introduce
    /// no binding in this module at all (they say that a name of another
    /// module is also a name of this one), so there is nothing to emit. It
    /// records the edge and the specifier; the loader wires the cell.
    void collect_reexports(std::int32_t idx);

    /// Returns the function or class node of a named `export default` at
    /// `outer`, or -1.
    [[nodiscard]] std::int32_t default_export_name(std::int32_t outer) const {
        const vp::node & n = at(outer);
        if (n.kind != vp::nk::export_decl || n.c != 1 || n.a < 0 || at(n.a).text.empty()) {
            return -1;
        }
        const vp::nk kind = at(n.a).kind;
        return kind == vp::nk::func_expr || kind == vp::nk::class_decl ? n.a : -1;
    }
    /// Appends the (local, exported) pairs a top-level statement binds.
    /// Collected rather than published in place, because the binding pass runs
    /// at module entry and the statement compiles later.
    void export_bindings(std::int32_t outer,
                         std::vector<std::pair<std::string, std::string>> & out);

    /// Binds a name to a register that already exists. The catch parameter
    /// needs it: the handler writes the thrown value into a register chosen
    /// when the try block opened, and the name has to refer to that same slot
    /// rather than to a fresh one.
    void declare_local_at(std::string name, std::uint16_t reg);
    /// Returns the register of local `name` in the current frame, or -1.
    [[nodiscard]] int find_local(std::string_view name) const;
    /// Returns the local `name` only if it is declared in the current scope.
    ///
    /// find_local_entry searches the whole frame, which is right for a read (an
    /// inner scope sees an outer binding) and wrong for deciding whether a
    /// declaration needs a slot of its own. A `const` in a block that shares a
    /// name with an outer binding must shadow it; treating the outer one as
    /// "already declared" would make the inner declaration write through to it.
    ///
    /// The index's back() is the innermost entry for the name, so "is it in the
    /// current scope" is one comparison against the scope mark: if the
    /// innermost one is outside, every other one is further out still.
    [[nodiscard]] local * find_local_in_current_scope(std::string_view name);

    /// Returns the innermost local `name` anywhere in frame `f`, or null.
    [[nodiscard]] local * find_local_entry(frame & f, std::string_view name);

    /// Resolves `name` as an upvalue of frame `level`, adding the descriptor
    /// chain if it is not already there. Returns -1 when the name is not a
    /// local of any enclosing function frame (frame 0 is the script, whose
    /// declarations are globals and reachable directly).
    ///
    /// The recursion is what makes two-level capture work: if the name belongs
    /// to a grandparent, the parent first acquires it as its own upvalue, and
    /// this frame then captures the parent's upvalue rather than a register.
    [[nodiscard]] int resolve_upvalue(std::size_t level, std::string_view name);

    [[nodiscard]] int add_upvalue(std::size_t level, std::string_view name, upvalue_desc desc);

    template <typename Fn> static void for_each_template_hole(std::string_view raw, Fn && fn);

    template <typename Fn> static void each_name_in_template(std::string_view raw, Fn && add);

    [[nodiscard]] static bool is_function_node(const vp::node & n);

    /// Builds the capture index: which names each nested function mentions,
    /// without a set per function.
    ///
    /// Materialising a set of names per function is the cost, not the walking:
    /// memoising the walk and inserting into a set during it were both
    /// measured and both lost (docs/script.md).
    ///
    /// So no set is materialised. One pass numbers every function in an Euler
    /// tour and records, for each name, the tick of the innermost function
    /// that mentions it. A name is captured by function F exactly when one of
    /// those ticks lies strictly inside F's range (strictly, because a name F
    /// mentions itself is not captured by F). That is a binary search, and the
    /// memory is one integer per distinct (name, function) pair rather than
    /// O(names x nesting depth).
    ///
    /// Built once, then read-only: nothing mutates it after build_capture_index
    /// returns, so concurrent compilation can share it without a lock.
    void build_capture_index();

    /// The Euler-tour walk behind build_capture_index.
    void tour(std::int32_t idx, std::int32_t enclosing, std::int32_t & tick, bool boundary);

    /// The Euler-tour range of function node `idx`.
    [[nodiscard]] interval range_of(std::int32_t idx) const;

    /// Returns whether this body reads `arguments`.
    ///
    /// Materialising it costs a register and an array per call, so it is only
    /// done where the name is actually mentioned; large bundles make far too
    /// many calls to pay for it everywhere.
    ///
    /// Arrows are descended into, and other functions are not: an arrow has no
    /// `arguments` of its own and sees the enclosing function's, so a mention
    /// inside one is a mention here. Making it a real local is what lets the
    /// arrow reach it, as an ordinary captured variable.
    [[nodiscard]] bool mentions_arguments(std::int32_t idx) const;
    /// Returns whether this function's body mentions `super`, itself or through
    /// an arrow (an arrow has no [[HomeObject]] of its own, 15.3.4). A
    /// non-arrow function inside it has its own home object. Decides whether an
    /// object literal's method needs its home object wired at all. `root` is
    /// the function node itself, whose own parameters and body are walked.
    [[nodiscard]] bool mentions_super(std::int32_t idx, bool root) const;
    /// Returns whether there is a function, arrow or class anywhere under
    /// `idx`. A boxed lexical binding whose initialiser makes a closure needs
    /// its cell before the initialiser runs (`let y = () => y`); one whose
    /// initialiser cannot keeps the shorter sequence. See compile_stmt's
    /// var_decl.
    [[nodiscard]] bool contains_closure(std::int32_t idx) const;
    /// Returns whether a function nested in the current one mentions `name`,
    /// or a direct eval could reach it, so a local of that name must live in a
    /// heap cell (see build_capture_index).
    [[nodiscard]] bool is_captured(std::string_view name) const;

    /// Reads up to `count` hex digits after position `at`, leaving `at` on the
    /// last one consumed so the caller's ++i lands past it. Lenient: a truncated
    /// escape yields what digits there were, matching the parser's leniency
    /// contract rather than throwing during compilation.
    [[nodiscard]] static std::uint32_t read_hex(std::string_view s, std::size_t & at,
                                                std::size_t count);

    /// Decodes a string literal's escapes. The lexer hands back the raw lexeme,
    /// quotes and all: `'a'` arrives as three characters.
    [[nodiscard]] static std::string decode_string_literal(std::string_view lexeme);
    /// The same, for text with no surrounding quotes.
    [[nodiscard]] static std::string decode_string_body(std::string_view lexeme);

    /// Collects the names a script declares with var/let/const, at any block
    /// depth and outside any function. Only the script frame's list is read
    /// (the strict assignment probe in compile_assign consults it), so only
    /// compile_program collects.
    void collect_declared_names(std::int32_t body);

    /// The names a statement list declares lexically (`let`, `const`, `using`
    /// and `class`) and, separately, the functions it declares. They are the
    /// same question in a Block and different ones in a function body, where a
    /// function declaration is a `var`: see each_block_function.
    void collect_lexical_names(std::span<const std::int32_t> stmts,
                               std::vector<std::string> & out) const;
    void collect_function_names(std::span<const std::int32_t> stmts,
                                std::vector<std::string> & out) const;

    /// Hoists this body's own `let`/`const`/`var` names into registers before
    /// anything is compiled. Nested function declarations hoist too and are
    /// compiled first, so the locals they capture have to exist by then.
    /// `lexical_only` is for an eval's top level, whose `let`/`const`/`class`
    /// are its own (19.2.1.3 step 17's lexEnv) while its `var`s stay the
    /// caller's.
    void predeclare_locals(std::int32_t body, bool lexical_only = false);
    /// Hoists to the block's start each `let`/`const` of a nested block that a
    /// closure earlier in the block captures (14.2.3
    /// BlockDeclarationInstantiation: every lexical binding of a block exists
    /// from its entry). Only those: any other declaration is made where it
    /// stands.
    void hoist_captured_lexicals(const vp::node & block);
    /// Returns whether something in `idx` reads `name` from inside a function
    /// (`idx` itself or one within it) through no scope in between that binds
    /// the name again (binds_again). If so, compiling that function before the
    /// declaration would resolve the name to some other binding.
    [[nodiscard]] bool closure_reads(std::int32_t idx, std::string_view name,
                                     bool in_function = false);
    /// Returns whether node `n` opens a scope that declares `name` again.
    [[nodiscard]] bool binds_again(const vp::node & n, std::string_view name);
    template <typename Hoist> void hoist_nested_vars(std::int32_t index, const Hoist & hoist);
    template <typename Each>
    void each_block_function(std::int32_t index, const Each & each,
                             std::vector<std::string> & lexical, bool body_level = false);

    /// One statement list that is one scope: a Block, a program, or every
    /// CaseClause of a `switch` together. Its own function declarations are
    /// visited first, because a block's function declaration shadows only
    /// what is nested below it and not itself.
    template <typename Each>
    void each_block_function_scope(std::span<const std::int32_t> stmts, const Each & each,
                                   std::vector<std::string> & lexical, bool body_level);

    /// Returns whether `name` was hoisted at the current function's body entry
    /// (see frame::predeclared).
    [[nodiscard]] bool was_predeclared(std::string_view name) const;

    /// Records a compile error; only the first one is kept.
    void fail(std::string message);

    /// Compiles the parameter prologue of `function f(a, b = 1, ...rest)`: a
    /// default is the param node's `a` child and a rest is `d == 1`.
    ///
    /// The order matters, and all three steps guard the same hazard: the
    /// arguments are in registers this frame is about to reuse:
    //
    ///   1. gather_rest first. The extra arguments live in the registers just
    ///      past the declared parameters, which is exactly where the body's
    ///      locals and temporaries get allocated. Anything emitted before this
    ///      reads them has already overwritten them.
    ///   2. Defaults next, and before boxing. A captured parameter is wrapped
    ///      in a heap cell in place; a plain register write afterwards would
    ///      drop the cell and the closure would see the wrong variable.
    ///   3. A temporary allocated for a default expression must be released,
    ///      or every default permanently widens the frame.
    void compile_parameter_prologue(std::span<const std::int32_t> params);
    /// The parameters still uninitialised while a default runs (10.2.11 steps
    /// 21-28: each parameter's binding is initialised in order, and a default
    /// reading a later one, or its own, is the ReferenceError of an
    /// uninitialised binding). Set around one default's compilation, for the
    /// frame it belongs to: `function f(x = y, y)` reads y in this frame and
    /// throws; `function f(x = () => y, y)` reads it from another frame, later,
    /// and does not. compile_ident consults it.
    std::vector<std::string> tdz_names_;
    std::size_t tdz_frame_ = static_cast<std::size_t>(-1);
    /// Emits `throw new <kind>(message)`, through the global constructor.
    void emit_throw(std::string_view kind, std::string message);

    // --- Derived constructors. ---

    /// Set by compile_class for the constructor it is about to compile; the
    /// body consumes it (see frame::derived_flag).
    bool derived_ctor_pending_ = false;
    /// The function node of `export default function f() {}`, a declaration
    /// the parser hands over as an expression, so it binds no name of its own.
    std::int32_t default_function_ = -1;
    /// Set for a method, accessor or constructor about to be compiled (one
    /// whose home object is wired); the body consumes it (frame::has_home).
    bool method_pending_ = false;
    /// Returns whether this function (or field initialiser) holds a direct
    /// `eval(...)`, which may use `super` on its behalf, itself or in anything
    /// nested. It reads the capture index's eval ticks, so a nested function's
    /// eval counts too: the home object wired for it is then unneeded, never
    /// missing.
    [[nodiscard]] bool contains_direct_eval(std::int32_t idx) const;
    /// An arrow's new.target is its enclosing function's (15.3.3: it has none
    /// of its own), but load_new_target reads the running frame's, which for
    /// the arrow is undefined. A function whose body has an arrow using
    /// `new.target` or `eval(` (arrow_wants_new_target) keeps its own in this
    /// hidden boxed local, which the arrow reads as an upvalue
    /// (emit_new_target).
    static constexpr std::string_view new_target_local = "@new.target";
    [[nodiscard]] bool arrow_wants_new_target(std::int32_t idx, bool in_arrow) const;
    void emit_new_target(std::uint16_t dst);
    /// The flag of the derived constructor `this` resolves to from here, or
    /// null: this frame's, or through any number of arrows, the nearest
    /// non-arrow frame's.
    [[nodiscard]] const std::string * derived_flag();
    /// Emits the check that `this`, a return or a `super()` makes: a false
    /// flag is the ReferenceError; for a second `super()` (`again`), a true
    /// flag is one too.
    void emit_super_check(const std::string & flag, bool again);
    /// Emits the flag update after `super()` returned: the flag goes true.
    void emit_super_done(const std::string & flag);
    /// Emits the tail of a `return v` in a derived constructor: an object is
    /// returned, undefined returns `this` (once super() ran), anything else is
    /// the TypeError (10.2.2 [[Construct]] steps 10-13).
    void emit_derived_return(std::uint16_t value);

    // --- `with`. ---
    //
    // `with (o) body` puts an object in front of the scope chain: every name
    // the body mentions is looked up on `o` first (HasProperty, then a
    // @@unscopables veto, 9.1.1.2.1), and only a name the body itself
    // declares (a `let` in the block, a nested function's parameter) is
    // exempt. The object lives in a hidden, boxed local named `@with:N`, so a
    // closure made inside the body captures it like any other local; each
    // open `with` is one of these, innermost last, and emit_with_object emits
    // the lookup chain for a name. Hoisted function declarations are compiled
    // at the enclosing function's entry, before any `with` is open, and read
    // their names without the object: the one shape this does not cover.
    //
    // A sloppy direct eval's `var`s belong to its caller's function (19.2.1.3
    // EvalDeclarationInstantiation: the variable environment is the caller's)
    // and are made while the function runs. So a function whose own body
    // calls `eval(` keeps them on an object of its own, and every name it and
    // the functions inside it resolve beyond their own locals is looked up
    // there first: a with_scope marked `eval_vars`, whose object has a null
    // prototype and whose bindings are not a call's `this`. A direct eval is
    // handed every such object around it (compile_direct_eval), looks names
    // up on all of them and makes its `var`s on the innermost
    // (compile_program, eval_vars_name_).
    /// One open `with` statement, or a direct eval's variable environment.
    struct with_scope {
        std::string name;        // The hidden local holding the object.
        std::size_t frame = 0;   // The frames_ index the statement is in.
        std::size_t locals_mark; // Position of that local in its frame: names
                                 // declared at or past it are inside the body.
        bool eval_vars = false;  // A direct eval's variable environment, not a `with`.
    };
    std::vector<with_scope> with_scopes_;
    void compile_with(const vp::node & n);
    /// The name's resolution as the compiler sees it: the frame declaring it
    /// and its position there, or `frames_.size()` when it is a global.
    [[nodiscard]] std::pair<std::size_t, std::size_t> declaring_frame(std::string_view name);
    /// Leaves in `obj` the innermost with-object that binds `name` (or
    /// undefined when none does at run time) and returns whether any `with`
    /// applies to the name at all (nothing is emitted when none does).
    [[nodiscard]] bool emit_with_object(std::string_view name, std::uint16_t obj);
    /// Emitted before a write through the with-object `obj`: in strict code,
    /// the ReferenceError when it no longer has `name` (see the definition).
    void emit_with_still_bound(std::uint16_t obj, std::string_view name);
    /// The open with_scopes a lookup of `name` from here must consult.
    [[nodiscard]] std::vector<const with_scope *> applicable_with_scopes(std::string_view name);
    /// Returns whether this function's own body, not a nested function's,
    /// calls `eval(`.
    [[nodiscard]] bool own_direct_eval(std::int32_t idx) const;
    /// In a direct eval's program: the innermost `eval_vars` binding it was
    /// handed (its caller's variable environment), or empty when none was.
    std::string eval_vars_name_;
    /// Returns whether this call needs a receiver register: a member callee,
    /// or a plain name inside a `with` (the object that binds it is the
    /// `this`, 13.3.6.2 step 6 via WithBaseObject).
    [[nodiscard]] bool call_needs_receiver(const vp::node & callee);
    /// The hidden flag that records whether lexical binding `name` has been
    /// initialised (its temporal dead zone), or empty when it has none.
    [[nodiscard]] std::string binding_ready_flag(std::string_view name);
    /// Emits the TDZ ReferenceError taken while `flag` is false; nothing when
    /// `flag` is empty.
    void emit_ready_check(std::string_view flag);
    /// Sets `name`'s ready flag once its declarator has run.
    void initialize_lexical(std::string_view name);
    /// compile_ident without the with lookup: a local, an upvalue or a global.
    void emit_plain_read(std::string_view name, std::uint16_t dst, bool typeof_lookup = false);
    void emit_plain_write(std::string_view name, std::uint16_t src);
    void emit_global_write(std::string_view name, std::uint16_t src);

    // --- Immutable bindings. ---
    //
    // `const x = 1; x = 2` is the TypeError of 9.1.1.1.5 SetMutableBinding
    // step 5, and a named function expression's own name ignores a sloppy
    // write. Both are decided here, where the write is compiled: the binding a
    // name resolves to is known statically (the same walk resolve_upvalue
    // makes), so a refused write compiles to the throw and every other write
    // is untouched. A declaration's own write is never refused (declaring_).

    /// What an assignment to `name`, from here, may do.
    [[nodiscard]] immutability binding_immutability(std::string_view name);
    /// The binding `name` resolves to from here: the frame declaring it and its
    /// position there, or {no_binding, 0} for a global (or a direct eval's
    /// caller's binding).
    static constexpr std::size_t no_binding = static_cast<std::size_t>(-1);
    [[nodiscard]] std::pair<std::size_t, std::size_t> binding_position(std::string_view name);
    /// Emits the TypeError when the binding refuses the write in this code;
    /// returns true when the write must not be emitted at all.
    [[nodiscard]] bool refuse_immutable_write(immutability readonly,
                                              std::string_view global_name = {});
    /// `const`, `using` and `await using` declare immutable bindings.
    [[nodiscard]] static immutability declared_immutability(const vp::node & decl);
    /// A class's own name inside its body is a binding of its own, immutable
    /// (15.7.14 steps 4.a and 30.a: CreateImmutableBinding(className, true)),
    /// whichever binding the declaration or expression writes outside. One
    /// entry per open named class: the name and the binding it resolves to in
    /// the class's scope; a write from inside the body resolving to that same
    /// binding is a write to the inner one.
    struct class_binding {
        std::string name;
        std::size_t level = no_binding;
        std::size_t position = 0;
    };
    std::vector<class_binding> class_bindings_;
    /// A classic script's top-level `const`s, which are globals here rather
    /// than locals (see compile_stmt's var_decl): collected before anything
    /// is compiled, since a hoisted function assigning one compiles first.
    std::vector<std::string> script_consts_;

    /// A numeric literal's value. The radix prefixes take the integer overload
    /// and then widen; a double is exact up to 2^53, which is further than any
    /// of these literals reach.
    [[nodiscard]] static double number_literal(std::string_view text);

    // --- Operand limits. ---
    //
    // These do not raise any limit. They make the compiler say which one it hit
    // and what it wanted, so a program that does not fit is a message rather
    // than a silent truncation.
    static constexpr std::size_t operand_limit = 65535;    // A uint16 field.
    static constexpr std::int32_t jump_limit = 2147483647; // INT32_MAX: the signed bx half.

    /// A readable name for function `index`, for error messages.
    [[nodiscard]] std::string frame_name(std::size_t index) const;

    /// Pool size up to which name and string interning scans linearly, then
    /// switches to a hash index.
    ///
    /// Indexing everything unconditionally made the vendored p5.js bundle 6.4%
    /// cheaper to load and the Phaser bundle 0.5% more expensive: p5 has
    /// functions mentioning many distinct names, where the quadratic scan hurt,
    /// and Phaser has many small ones, where building two hash maps per
    /// function costs more than the scan. Sixteen is where a linear scan of
    /// short strings stops beating a hash.
    static constexpr std::size_t small_pool = 16;

    /// Returns `text`'s index in `pool`, appending it if new; `index` is used
    /// once the pool outgrows small_pool.
    [[nodiscard]] static std::uint32_t intern_into(std::vector<std::string> & pool,
                                                   flat_map<std::string, std::uint32_t> & index,
                                                   std::string text);

    /// The same answers add_name and add_string give, without the quadratic
    /// scan. The numbering is identical either way (an unseen entry is appended
    /// and takes the next index), so the bytecode is byte-for-byte the same.
    [[nodiscard]] std::uint32_t intern_name(std::string text);
    [[nodiscard]] std::uint32_t intern_string(std::string text);

    /// The seam every property-name operand goes through.
    [[nodiscard]] std::uint16_t name_operand(std::string text);
    /// The operand for a member name as the parser spelled it: `#x` becomes the
    /// private key `@#x:N` of the innermost class declaring it (see
    /// private_key_prefix and private_scopes_), or `@#x` alone when none does
    /// (an early error the checker owns); anything else is itself.
    [[nodiscard]] std::uint16_t member_operand(std::string_view text);
    /// The same resolution, as the key string, for a definition that goes
    /// through a native rather than an operand.
    [[nodiscard]] std::string member_key(std::string_view text);

    /// Called where a frame's size is finally written, because that is the only
    /// point at which high_water is the final total rather than a running one.
    void finish_frame(std::size_t index, std::size_t params);

    // --- Entry. ---

    /// Compiles the whole program (or eval source) into out_.
    void compile_program();

    // --- Destructuring. ---
    //
    // array_pattern / object_pattern / assign_pattern / rest_element lower into
    // the opcodes that already exist (get_prop, get_index and the ordinary
    // binding paths), so nothing new is needed in the VM. `declaring`
    // distinguishes `const {a} = o`, which introduces a binding, from
    // `({a} = o)`, which writes to one that already exists.

    /// Appends every name a pattern binds, so declarations can be hoisted
    /// before the pattern is compiled, which is what lets a nested function
    /// capture one.
    void pattern_names(std::int32_t pat, std::vector<std::string> & out) const;

    /// Declares a pattern's names; call it before writing them, and outside
    /// the caller's reg_mark()/release_to(mark). declare_local allocates; a
    /// caller that wraps the binding in a mark to free the temporary holding
    /// the source hands the locals' registers straight back, and the next
    /// temporary in the same scope lands on top of a live local. In a
    /// function's top scope the names are hoisted and nothing is allocated, so
    /// this only matters in a block.
    ///
    /// At the top level there is nothing to declare: a declaration there is a
    /// global, and emit_write reaches one by falling through to set_global.
    /// `readonly` says the names are `const` (or `using`) bindings.
    void declare_pattern_names(std::int32_t pat, bool block_scoped = false,
                               immutability readonly = immutability::none, bool lexical = false);

    /// Binds pattern `pat` to the value in `src`, declaring its names first
    /// when `declaring`.
    void compile_pattern_binding(std::int32_t pat, std::uint16_t src, bool declaring,
                                 bool block_scoped = false,
                                 immutability readonly = immutability::none);

    /// Binds `pat` to the value sitting in `src`. Every name it mentions
    /// already exists by the time this runs; see compile_pattern_binding.
    void compile_pattern(std::int32_t pat, std::uint16_t src);

    /// `{a, ...rest} = o`: every own property except the ones already named.
    void emit_rest_object(std::uint16_t dst, std::uint16_t source,
                          const std::vector<std::string> & taken,
                          const std::vector<std::uint16_t> & taken_keys);

    // --- Statements. ---

    void compile_stmt(std::int32_t idx);

    void compile_if(const vp::node & n);
    void compile_clause(std::int32_t stmt);

    /// One live loop. `break` and `continue` are forward jumps whose targets
    /// are not known until the loop is finished being compiled, so each records
    /// its jump site here and the loop patches them on the way out.
    ///
    /// `labels` is what makes `break outer;` reach past an inner loop; without
    /// it, a labelled break would silently become an ordinary one and leave the
    /// wrong loop. It is a list because `a: b: for (;;)` names one loop twice:
    /// every label in a chain is a break and continue target of the statement
    /// it ends on (ECMA-262 14.13).
    struct loop_context {
        std::vector<std::string> labels;
        std::vector<std::size_t> breaks;
        std::vector<std::size_t> continues;
        std::size_t handler_depth = 0; // Try blocks open when the loop started.
        std::optional<std::uint16_t> iterator_record = std::nullopt;
    };

    /// An open `finally`.
    ///
    /// `finally` runs on every way out of a try block, and a `return`, a
    /// `break` and a `continue` are three of those ways: an exit stores what it
    /// was trying to do into two registers and jumps to the finally, which
    /// runs once and then does that thing.
    ///
    /// The kind register holds 0 for a normal fall-through, 1 for a throw with
    /// the thrown value in the value register, 2 for a return with the returned
    /// value there, and 3 + i for the i-th entry in `exits` (a break or a
    /// continue to a particular loop). Break and continue need the index
    /// because "which loop" cannot fit in the kind alone, and a numbered exit
    /// keeps the dispatch to one compare per target that actually occurs
    /// rather than one per loop in scope.
    struct finally_context {
        std::uint16_t kind_reg = 0;
        std::uint16_t value_reg = 0;
        /// How many loops were open when this `try` started, which says whether
        /// a given break crosses this finally. A break to a loop opened inside
        /// the try (`try { for (;;) { break } } finally {}`) never leaves the
        /// try block, so it must not be routed here: it would store a
        /// completion, jump to the finally, and have the dispatch try to
        /// re-emit a break to a loop that has since been popped.
        std::size_t loops_open = 0;
        std::vector<std::size_t> arrivals; // Jumps into the finally, patched at it.
        struct exit {
            std::size_t loop_index = 0; // Into loops_, not a pointer: it reallocates.
            bool is_continue = false;
        };
        std::vector<exit> exits;
        std::size_t handler_depth = 0; // Outside this finally's own guard.
    };

    /// The finallys between here and the top of the function, innermost last.
    /// A `return` crossing three of them stores its completion in the innermost
    /// and lets each dispatch hand it to the next, which is what makes nesting
    /// work without any of them knowing how deep they are.
    std::vector<finally_context> finallies_;

    /// Routes a `return` into the innermost open finally. Returns false when
    /// there is none and the caller should just emit `ret`.
    [[nodiscard]] bool route_return_through_finally(std::uint16_t value_reg);
    /// Closes iterators exited before the nearest handler, then throws the
    /// saved value. `caught` says the value is one a handler caught and is
    /// throwing on (a finally's, an iterator close's), not a `throw`
    /// statement's (function_proto::rethrows).
    void emit_rethrow(std::uint16_t value_reg, bool caught);
    /// The throw_value of such a rethrow, recorded in function_proto::rethrows.
    void emit_caught_throw(std::uint16_t value_reg);
    /// The same for a loop exit. `loop_index` must index loops_.
    [[nodiscard]] bool route_exit_through_finally(std::size_t loop_index, bool is_continue);
    /// The tail of a finally: runs the completion it was handed.
    void emit_finally_dispatch(const finally_context & open);
    void compile_try_with_finally(const vp::node & n);

    // `using` / `await using` (statements/using.cpp). A statement list with a
    // using declaration in it compiles, from that declaration on, inside a
    // region whose finally disposes the resources; `using_stacks_` holds the
    // register of the innermost region's stack for emit_using_add.
    [[nodiscard]] static bool is_using_decl(const vp::node & n);
    void compile_statement_list(std::span<const std::int32_t> stmts, bool skip_func_decls);
    void compile_using_region(bool async, const std::function<void()> & body);
    void emit_using_add(std::uint16_t reg, bool async);
    std::vector<std::uint16_t> using_stacks_;

    void patch_breaks(loop_context & loop);
    void patch_continues(loop_context & loop, std::size_t target);

    /// The loop a break/continue belongs to: the named one, or the innermost.
    [[nodiscard]] loop_context * loop_for(std::string_view label);

    void compile_break(const vp::node & n);
    /// Closes the for-of iterators of loops_[first..]; see the definition.
    [[nodiscard]] std::size_t close_crossed_iterators(std::size_t first);

    void compile_continue(const vp::node & n);

    /// A labelled statement. A loop or `switch` takes the label as its own
    /// break and continue target. Any other statement gets a loop_context with
    /// no continue target, so `break lbl` jumps past it and `continue lbl` is
    /// a compile error.
    void compile_labeled(const vp::node & n);

    void compile_while(const vp::node & n);

    /// do..while: the body runs before the first test, which is the whole
    /// difference and the reason it cannot share compile_while.
    void compile_do_while(const vp::node & n);

    void compile_for(const vp::node & n);
    void compile_for_loop(const vp::node & n);

    /// for..of and for..in. `for..of` runs the iterator protocol (one `next()`
    /// per iteration, `return()` on early exit) for generators and user
    /// iterables, and falls back to an index loop over op::iterable's array
    /// for built-ins it can materialise exactly. `for..in` always loops over
    /// the array of own keys.
    void compile_for_of(const vp::node & n);
    void compile_for_await(const vp::node & n);
    /// The `of`/`in` operand, with a lexical head's names in their dead zone
    /// (14.7.5.6 ForIn/OfHeadEvaluation step 2): `for (let x of [x])` reads the
    /// new, uninitialised `x`.
    void compile_for_head_source(const vp::node & n, std::uint16_t dst);

    /// switch. Two passes: every case's test first, jumping to its body, then
    /// the bodies laid out in order so fallthrough works (a case without a
    /// break runs the next one).
    void compile_switch(const vp::node & n);

    /// try / catch / finally. A `try` with a `finally` is handed to
    /// compile_try_with_finally, which compiles the finally body once: every
    /// exit (fall-through, throw, return, break, continue) stores its
    /// completion and jumps there, and emit_finally_dispatch then resumes it.
    /// See finally_context.
    void compile_try(const vp::node & n);

    void compile_throw(const vp::node & n);

    [[nodiscard]] std::vector<std::string> take_labels();

    /// Emits the implicit `return undefined`; falling off the end of an async
    /// function still owes the caller a promise.
    void emit_implicit_return();

    void compile_function_decl(std::int32_t idx);

    /// Compiles function node `idx` into a new proto named `name` and returns
    /// the proto's index.
    [[nodiscard]] std::uint32_t compile_function_body(std::int32_t idx, std::string name);

    // --- Expressions. ---

    /// Compiles expression `idx` into register `dst`. Every expression goes
    /// through here, and a member/call chain containing an optional link is
    /// compiled as one unit so it has one exit.
    void compile_expr(std::int32_t idx, std::uint16_t dst);

    void compile_expr_inner(std::int32_t idx, std::uint16_t dst);

    /// Writes `src` to a name, which has to know whether the name lives in a
    /// register, a cell, an upvalue or the global table. ++/-- goes through
    /// here too, so it cannot drift out of agreement with the read side,
    /// `compile_ident`.
    void emit_write(std::string_view name_text, std::uint16_t src);

    void compile_ident(const vp::node & n, std::uint16_t dst, bool typeof_lookup = false);

    /// `delete` (13.5.1.2). A member or index target goes through
    /// delete_ref_name. A plain name deletes the property of a `with` object
    /// that binds it and otherwise yields false. Any non-reference operand is
    /// evaluated and yields true.
    void compile_delete(const vp::node & n, std::uint16_t dst);

    void compile_binary(const vp::node & n, std::uint16_t dst);

    /// && and || must not evaluate the right side unless they have to, so they
    /// are control flow rather than an opcode.
    void compile_logical(const vp::node & n, std::uint16_t dst);

    void compile_unary(const vp::node & n, std::uint16_t dst);

    /// A place a value can be read from and written to.
    ///
    /// Compound assignment and ++/-- both have to evaluate their target once
    /// and then read-modify-write it. Re-compiling the target expression for
    /// the write would evaluate its side effects twice, so `a[i++] += 1` would
    /// increment i twice and store into the wrong slot. This is the shape that
    /// makes both of them correct, and it is why they share a code path.
    using reference = compiler_types::reference;

    [[nodiscard]] reference prepare_reference(const vp::node & target);

    void emit_load(const reference & ref, std::uint16_t dst);

    void emit_store(const reference & ref, std::uint16_t src);
    void emit_super_access(const reference & ref, std::string_view name, std::uint16_t value);

    /// The binary op for `+=` and friends: the assignment's text minus its
    /// '='. Sets `ok` to false for an unknown operator.
    [[nodiscard]] static op compound_op(std::string_view text, bool & ok);

    void compile_assign(const vp::node & n, std::uint16_t dst);

    void compile_update(const vp::node & n, std::uint16_t dst);

    void compile_ternary(const vp::node & n, std::uint16_t dst);

    /// A template literal. The parser hands the whole thing back as one token,
    /// backticks and all, so the splitting happens here: literal chunks are
    /// strings, `${...}` chunks are parsed and compiled, and the whole thing is
    /// a chain of concatenations.
    void compile_template(const vp::node & n, std::uint16_t dst);

    /// Loads into `dst` the object `super` looks properties up on: the
    /// prototype above the one the running method was defined on.
    void emit_super_base(std::uint16_t dst);
    void emit_super_get(std::uint16_t key, std::uint16_t dst);
    /// After `super(...)` returned: runs the derived class's field initialiser
    /// on the bound `this` (see init_fields_name).
    void emit_init_fields_after_super();
    /// `super(...)` answered `result`: binds it as `this` when it is an object.
    void emit_bind_this_after_super(std::uint16_t result);
    /// Set by compile_class while a base class's constructor compiles: its
    /// fields run first thing in the body (10.2.2 [[Construct]] step 6.b,
    /// InitializeInstanceElements before the body), whichever way the
    /// constructor is reached (`new`, a subclass's `super()`,
    /// Reflect.construct). compile_function_body consumes it.
    bool base_fields_pending_ = false;
    void emit_init_fields_at_entry();

    [[nodiscard]] bool any_spread(std::span<const std::int32_t> args) const;

    /// The arguments of a call, as one array. Same shape as an array literal,
    /// because that is exactly what it is.
    void emit_argument_array(std::span<const std::int32_t> args, std::uint16_t dst);

    /// `f(...args)`.
    ///
    /// Every other call form puts its arguments in consecutive registers and
    /// the count in an operand, which cannot work when the count is not known
    /// until the spread is evaluated. So the arguments become an array and the
    /// callee and receiver are resolved into registers first, which collapses
    /// all four call forms into one, since by then the receiver is just a
    /// register.
    void compile_spread_call(const vp::node & n, std::uint16_t dst);

    /// Calls need their arguments in consecutive registers starting just above
    /// the callee, so the VM can hand the callee a contiguous frame.
    void compile_call(const vp::node & n, std::uint16_t dst);
    void compile_call_target(const vp::node & n, std::uint16_t target, std::uint16_t self);
    void emit_optional_guard(std::uint16_t value);

    /// `new C(...)`. The receiver is created by the VM, which also decides what
    /// the expression evaluates to: the new object, unless the constructor
    /// returned one of its own.
    void compile_new(const vp::node & n, std::uint16_t dst);

    /// Returns whether this member/call chain contains an optional link
    /// anywhere below it. Only the spine is walked: `a?.b(c.d)` is optional,
    /// `a.b(c?.d)` is not, because the argument is its own chain.
    [[nodiscard]] bool chain_has_optional(std::int32_t idx) const;

    /// Compiles an optional chain. `a?.b.c` and `a?.m()` short-circuit the
    /// whole chain, not one link: a chain has one exit, and whichever link
    /// short-circuits jumps to it, past everything built on it.
    void compile_chain(std::int32_t idx, std::uint16_t dst);

    void compile_optional(const vp::node & n, std::uint16_t dst);

    /// The comma operator: evaluate everything, yield the last.
    void compile_sequence(const vp::node & n, std::uint16_t dst);
    /// `tag\`a${x}b\`` (13.2.8, 13.3.11): the tag called with the site's cached
    /// strings array (cooked, with `raw` on it) and the substitution values.
    void compile_tagged(const vp::node & n, std::int32_t idx, std::uint16_t dst);
    /// The template's text cut at its `${ }` holes: the literal chunks (raw
    /// spelling, between the delimiters) and the hole sources.
    static void split_template(std::string_view raw, std::vector<std::string> & chunks,
                               std::vector<std::string> & holes);
    /// Returns whether a template chunk's escapes are all well formed
    /// (12.9.6.1): a tagged template with a bad one has an undefined cooked
    /// value.
    [[nodiscard]] static bool template_chunk_cooks(std::string_view chunk);

    /// Compiles a function whose whole body defines each instance field on
    /// `this`, in declaration order. `new` runs it against the fresh object
    /// before the constructor body, so every instance gets its own value (one
    /// empty array per instance for `items = []`, not one shared array).
    ///
    /// It is compiled as an ordinary nested function, so an initialiser that
    /// mentions an enclosing local captures it as an upvalue like anything
    /// else. `is_static` compiles the class's other initialiser, `<static>`:
    /// the static fields and `static { }` blocks in source order, run once
    /// against the class itself (ClassDefinitionEvaluation step 31). It has to
    /// be a function of its own because a static field's `this` is the class
    /// and a static block is a function body.
    /// `brand` is the class's brand key (see context::find_private_element)
    /// when the class declares a private method or accessor of that kind, and
    /// the initialiser adds it to every instance first; empty otherwise.
    [[nodiscard]] std::uint32_t compile_field_initialiser(const std::vector<std::int32_t> & fields,
                                                          bool is_static = false,
                                                          std::string brand = {});
    /// Emits `__ctbrowser_private_add(target, key, v)`: PrivateFieldAdd, or the
    /// brand.
    void emit_private_add(std::uint16_t target, std::string_view key, std::uint16_t v);
    /// The name of the hidden local holding a class field's computed key. The
    /// key is evaluated once, when the class is defined, in member order with
    /// the methods' keys (15.7.10 ClassFieldDefinitionEvaluation step 1), not
    /// by the initialiser, which runs per instance; the initialiser reads it
    /// back as an upvalue. Not a name source text can spell.
    [[nodiscard]] std::string field_key_name(std::int32_t member) const;

    /// Declares a local for a class's own name, by whichever route this frame
    /// uses. Harmless for a `class Foo {}` declaration, which binds the same
    /// value again a moment later. `force` is for a class expression, whose
    /// name must be a binding of its own and never a write to something outer:
    /// at the top level a declaration's name is a global, but for an
    /// expression that would fall through to set_global and overwrite any
    /// global of the same name.
    void declare_class_name(std::string name, bool force = false);

    /// A class, compiled to what it desugars to: a constructor function plus a
    /// prototype object holding the methods, with `new` wiring an instance to
    /// that prototype and `extends` chaining the prototype objects.
    ///
    /// `as_declaration` is the difference between `class C {}` and
    /// `let x = class C {}`, and there is one node kind for both; only the
    /// call site knows which. A named class expression binds its name inside
    /// its own body and nowhere else, exactly like a named function
    /// expression: `let p5$2 = class p5 {}` must not give the module scope a
    /// local named `p5`.
    void compile_class(const vp::node & n, std::uint16_t dst, bool as_declaration = false,
                       std::string_view inferred_name = {});
    /// NamedEvaluation (8.4.5, 13.15.2, 14.3.1.2...): an anonymous function,
    /// arrow or class expression takes the name of the binding, property,
    /// parameter or pattern element it initialises. Anything else is
    /// compile_expr.
    void compile_named_expr(std::int32_t idx, std::uint16_t dst, std::string_view name);
    /// IsAnonymousFunctionDefinition (8.4.3): a function, arrow or class
    /// expression with no name of its own, which is what NamedEvaluation names.
    [[nodiscard]] bool is_anonymous_function_definition(std::int32_t idx) const;
    /// The property name of a string or number literal key, which the parser
    /// routes as computed (`{"a": f}`, `{1: f}`), or nothing for a real
    /// computed key, whose name is only known at run time.
    [[nodiscard]] std::optional<std::string> literal_key_name(std::int32_t key) const;
    /// Emits SetFunctionName(fn, key[, prefix]) at run time, through
    /// set_function_name_name.
    void emit_set_function_name(std::uint16_t fn, std::uint16_t key, std::string_view prefix = {});
    void emit_call_target_error(std::int32_t call, std::uint16_t dst);
    /// `yield* expr`; see the definition.
    void compile_yield_delegate(const vp::node & n, std::uint16_t dst);
    /// The start of a catch clause in a generator; see the definition.
    void emit_catch_filter(std::uint16_t caught);
    /// A data property defined on `target` through define_own_name.
    void emit_define_own(std::uint16_t target, std::string_view key, std::uint16_t v,
                         bool enumerable);
    /// The same with the key in a register: a computed one.
    void emit_define_own_key(std::uint16_t target, std::uint16_t key, std::uint16_t v,
                             bool enumerable);
    /// `get [key]() {}` / `set [key](v) {}` on `target`; see
    /// define_accessor_name.
    void emit_computed_accessor(std::uint16_t target, std::int32_t key, std::int32_t fn_node,
                                bool setter);
    /// An array pattern over the iterator protocol; see iterator_open_name.
    /// `elements` are the pattern's children; `bind` binds one element node to
    /// the register holding its value (a binding pattern and an assignment
    /// pattern bind differently, the iteration is the same).
    void compile_array_pattern(std::span<const std::int32_t> elements, std::uint16_t src,
                               vp::nk rest_kind,
                               const std::function<void(std::int32_t, std::uint16_t)> & bind);
    /// One call of one of the three iterator natives; the record (or the
    /// item) lands in `dst`.
    void emit_iterator_native(std::string_view name, std::uint16_t dst, std::uint16_t arg,
                              int flag = -1);

    /// `/ab+c/gi`. The lexer hands the literal over whole, delimiters and all,
    /// so the source is between the first `/` and the last one and the flags
    /// are what follows.
    ///
    /// It compiles to a call of the reserved factory rather than to an opcode:
    /// a regex is an ordinary object here, and the standard library is the
    /// only thing that needs to know how one is built. Reserved rather than
    /// `RegExp` so a page that shadows the constructor cannot change what its
    /// own literals mean.
    void compile_regex_literal(const vp::node & n, std::uint16_t dst);

    void compile_array(const vp::node & n, std::uint16_t dst);

    /// Appends every element of `source` to the array in `target`.
    void emit_append_all(std::uint16_t target, std::uint16_t source);

    void compile_object(const vp::node & n, std::uint16_t dst);

    // --- Helpers. ---
    void emit_string(std::uint16_t dst, std::string text);
    void emit_const(std::uint16_t dst, value v);

    /// Points the jump at `at_index` to the next instruction to be emitted.
    void patch_here(std::size_t at_index);
    /// Points the jump at `at_index` to instruction `target`.
    void patch_jump(std::size_t at_index, std::size_t target);

    const vp::ast & ast_;
    const vp::ast * current_ast_ = &ast_;
    std::vector<std::unique_ptr<vp::ast>> owned_asts_;
    /// Source text the compiler made up, kept alive for as long as the ASTs
    /// that borrow it. A parse holds string_views into its input, so a snippet
    /// built here (a parenthesised template hole, a synthesised constructor)
    /// cannot be a temporary.
    std::vector<std::unique_ptr<std::string>> owned_sources_;
    program & out_;
    std::vector<frame> frames_;
    /// The capture index: node to its Euler-tour range, and name to the ticks
    /// of the innermost functions mentioning it. Read-only after build.
    std::vector<interval> fn_range_;
    string_flat_map<std::vector<std::int32_t>> mentions_;
    std::vector<loop_context> loops_;
    std::vector<std::string> pending_labels_;
    /// The short-circuit jumps of the optional chain being compiled, and
    /// whether one is open; see compile_chain.
    std::vector<std::size_t> optional_exits_;
    bool in_chain_ = false;
    /// True while a declaration is writing its own binding: `let x = 1`,
    /// `class C {}`, `var x = 1` at a script's top level all reach set_global,
    /// and in strict code only an assignment to a name nothing declares is the
    /// ReferenceError; see emit_strict_assign_check.
    bool declaring_ = false;
    /// An expression met inside a declaration (a default, a computed key, a
    /// nested function body) is not its write: this RAII guard clears
    /// declaring_ for a scope.
    struct not_declaring {
        compiler_impl & self;
        bool saved;
        explicit not_declaring(compiler_impl & c) : self{c}, saved{c.declaring_} {
            c.declaring_ = false;
        }
        ~not_declaring() { self.declaring_ = saved; }
        not_declaring(const not_declaring &) = delete;
        not_declaring & operator=(const not_declaring &) = delete;
    };
    void emit_strict_assign_check(std::string_view name);
    /// While a function's parameter expressions are being compiled: the names
    /// bound in that scope, which a direct `eval` written there may not
    /// `var`-declare (see param_eval_name). Empty outside a prologue.
    std::vector<std::string> param_scope_names_;
    /// `eval(src)` inside parameter expressions, routed through param_eval_name.
    [[nodiscard]] bool compile_param_eval(const vp::node & n, std::uint16_t dst);
    /// A direct eval, `eval(src)` in a function: routed through
    /// direct_eval_name with the cells of every binding in scope (see
    /// compile_direct_eval). A call whose callee is the plain name `eval`.
    [[nodiscard]] bool is_direct_eval_call(const vp::node & n) const;
    [[nodiscard]] bool compile_direct_eval(const vp::node & n, std::uint16_t dst);
    /// The enclosing function ticks of every direct eval, sorted; see tour.
    std::vector<std::int32_t> eval_ticks_;
    /// When compiling a direct eval's source: the caller's bindings, in the
    /// order the entry function's upvalues hold them. Empty for every other
    /// program.
    std::vector<std::string> eval_outer_names_;
    /// When compiling an eval's source: where it was written (all false for
    /// any other program), giving strictness and what a direct eval nested at
    /// this program's top level inherits (see eval_site_here).
    eval_site eval_site_{};
    /// The site a direct eval written here has: the function whose `this` it
    /// shares, found through the arrows around it.
    [[nodiscard]] eval_site eval_site_here();
    /// Parallel to eval_outer_names_: which of them are immutable, and which
    /// are lexical declarations of the caller's function (eval_binding).
    std::vector<immutability> eval_outer_readonly_;
    std::vector<bool> eval_outer_lexical_;
    /// True when the top level is a scope, not the global one: a module's, and
    /// a strict eval's, whose `var`s and functions are its own (19.2.1.1 step
    /// 17: a new variable environment). Set by compile_program.
    bool top_level_is_scope_ = false;
    /// Returns whether source text can spell `name`, so a direct eval can reach
    /// it: not a pattern parameter's empty one, nor a synthetic "@super:" slot.
    [[nodiscard]] static bool eval_reachable(std::string_view name);
    /// How many exception handlers (try blocks) are open in the current
    /// function.
    std::size_t handler_depth_ = 0;
};

} // namespace detail

} // namespace ctbrowser::script

#include "compiler_impl/traversal.hpp"
