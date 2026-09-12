#pragma once
// The compiler proper, declared so its bodies can live in more than one file.
// Private to lib/Script/compile/ and deliberately not under include/: it
// includes <ctjs/vparse.hpp> and Boost, and "no third-party header in a public
// header" is policed by test/lint/api_surface.
//
// NAMED namespace, not anonymous: an anonymous namespace cannot be shared
// through a header. `detail` rather than plain `script` because `frame`,
// `local`, `interval` and `reference` are generic enough to collide with
// anything - vm.hpp really has a call frame. They stay NESTED.

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/number_format.hpp>

#include <boost/container/small_vector.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

#include <algorithm>
#include <ranges>
#include <span>

#include <array>
#include <charconv>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <ctjs/vparse.hpp>

#include <ctbrowser/script/value.hpp>

namespace ctbrowser::script {

// THE DEBUG SIDE TABLES ARE A BUILD OPTION, and the default is ON. OFF is not
// a stub: nothing is populated, `emit_offset` never leaves `no_offset`, and
// both vectors on every proto stay empty. Every reader must already cope with
// empty, because an image loader produces exactly that.
#ifndef CTBROWSER_SCRIPT_DEBUG_NAMES
#define CTBROWSER_SCRIPT_DEBUG_NAMES 1
#endif

// At `script` scope: compile.cpp names `vp::ast` and `vp::parse` too.
namespace vp = ctjs::vp;

namespace detail {

class compiler_impl {
public:
    struct local {
        std::string name;
        std::uint16_t reg = 0;
        bool boxed = false; // lives in a heap cell; see mark_captured
        // WHERE THIS LOCAL'S ENTRY IN `function_proto::locals` IS, or none when
        // the debug tables are off. The compiler's `locals` is a STACK that
        // shrinks at every scope exit, so by the time a function is finished
        // the only names left are the ones still in scope - which for a body
        // full of blocks is almost none of them. The debug table is written as
        // each local is DECLARED and closed as its scope is popped, and this is
        // the link between the two.
        static constexpr std::uint32_t no_slot = 0xFFFFFFFFu;
        std::uint32_t debug_slot = no_slot;
    };
    // Heterogeneous lookup, so is_captured can ask with a string_view without
    // building a std::string to throw away.
    struct sv_hash {
        using is_transparent = void;
        [[nodiscard]] std::size_t operator()(std::string_view s) const noexcept {
            return std::hash<std::string_view>{}(s);
        }
    };
    // A HALF-OPEN RANGE OF EULER-TOUR TICKS. A function's descendants are
    // exactly the functions whose tick lies strictly inside its own range.
    struct interval {
        std::int32_t lo = 0;
        std::int32_t hi = 0;
        [[nodiscard]] bool empty() const noexcept { return lo >= hi; }
    };

    struct frame {
        std::uint32_t proto = 0;
        std::vector<local> locals;
        // NAME -> THE POSITIONS IN `locals` THAT CARRY IT, innermost last. A
        // backward scan of `locals` runs once per identifier MENTION, so a big
        // function was quadratic in its own size (docs/performance.md).
        //
        // A vector per name rather than one index because names SHADOW: two
        // `let x` in sibling scopes are two entries, and popping the inner one
        // has to uncover the outer rather than erase the name. Entries are
        // pushed in increasing order, so `pop_scope` unwinding from the top
        // pops each name's stack from the top too.
        string_flat_map<boost::container::small_vector<std::uint32_t, 2>> local_index;
        // The same scan, on the upvalue list. This one only ever grows within a
        // frame, so it is a plain name -> position.
        string_flat_map<std::uint32_t> upvalue_index;
        std::vector<std::string> declared; // pre-scanned; see collect_declared_names
        // WHERE THIS FUNCTION SITS IN THE EULER TOUR, which is how
        // is_captured() is answered. Empty (lo >= hi) means nothing is
        // captured, which is what a field initialiser's frame gets - it never
        // had a captured set either.
        interval captures;
        std::vector<std::string> upvalue_names; // parallel to proto().upvalues
        std::vector<std::string> predeclared;   // hoisted at body entry; see predeclare_locals
        std::vector<std::size_t> scope_marks;   // locals.size() at each scope entry
        // WIDER THAN THE OPERAND THEY FEED, on purpose: counting in a wider
        // type lets the compiler SAY how many registers were wanted instead
        // of wrapping in silence.
        std::uint32_t next_reg = 0;
        std::uint32_t high_water = 0;
        bool is_async = false;     // `return v` hands back a settled promise of v
        bool is_generator = false; // `function*` - calling it does not run it
        // WHERE A NAME OR STRING ALREADY WENT. `function_proto::add_name` and
        // `add_string` deduplicate by LINEAR SCAN, quadratic in the distinct
        // names a function mentions. The index lives HERE rather than on the
        // proto because it is wanted only while compiling.
        flat_map<std::string, std::uint32_t> name_index;
        flat_map<std::string, std::uint32_t> string_index;
    };

    compiler_impl(const vp::ast & tree, program & out)
        : ast_(tree), current_ast_(&tree), out_(out) {}

    // A MODULE'S TOP LEVEL IS A SCOPE, NOT THE GLOBAL OBJECT. Set by
    // compiler::compile from the script_kind; see the note on that enum for why
    // the distinction is not cosmetic.
    bool module_scope_ = false;
    // `eval`: a trailing expression statement is the program's return value.
    // See compiler::compile_for_eval.
    bool completion_value_ = false;

    // THE BYTES THE AST WAS PARSED FROM, which is NOT `out_.source`.
    //
    // `compiler::compile` copies the source into the program, so the copy's
    // characters are at different addresses from the ones every `node::text`
    // views. Offsets are the same in both, and this is the buffer they are
    // offsets INTO - so it is the one a subtraction may be taken against.
    //
    // Empty when the caller did not say, which turns the whole of `offset_of`
    // off rather than producing wrong line numbers.
    std::string_view source_view_{};

    // WHERE A NODE WAS WRITTEN, as a byte offset into `source_view_`, or
    // `function_proto::no_offset` when this node cannot answer.
    //
    // `node::begin` is set for FUNCTIONS ONLY, so the offset of an ordinary
    // statement comes from the lexeme: `node::text` is a std::string_view INTO
    // the source, so the address of its first character minus the address of
    // the source IS the offset - the same subtraction `vp::parser::offset_at`
    // makes.
    [[nodiscard]] std::uint32_t offset_of(std::int32_t idx) const;

    // SETS THE EMIT CURSOR FOR ONE SUBTREE AND PUTS IT BACK.
    //
    // Restoring matters: in `a + b` the `add` instruction is emitted AFTER both
    // operands, and without a restore it would be attributed to `b`. With one,
    // the operands carry their own positions and the operator carries the
    // position of the operator.
    //
    // It holds the compiler and not the proto, deliberately. `out_.functions`
    // is a vector that GROWS while a nested function is compiled, so a
    // `function_proto &` taken here would dangle across any function
    // expression in the subtree this guard covers.
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

    // --- AST access -------------------------------------------------------
    // A NEGATIVE INDEX IS "NOTHING", not an address: every fixed child slot is
    // -1 when absent, and an array literal's element list holds -1 for a hole.
    // Returning an empty node makes a missed check compile to nothing instead
    // of reading past the pool.
    //
    // DEFINED HERE, and measured: out of line this one function was 4.18% of a
    // whole page render, having been absent from the profile while inlined.
    // `static const vp::node nothing` is one object across every including
    // file - a function-local static in an inline function is guaranteed to be.
    [[nodiscard]] const vp::node & at(std::int32_t i) const {
        static const vp::node nothing{vp::nk::empty, ""};
        if (i < 0 || static_cast<std::size_t>(i) >= current_ast_->nodes.size()) { return nothing; }
        return current_ast_->nodes[static_cast<std::size_t>(i)];
    }

    // Compile an expression parsed from a DIFFERENT source than the program's.
    // Template literals need it: the parser hands back `${...}` as raw text
    // inside one token. Node indices are per-AST, so the active one is swapped
    // for the duration and every `at()` follows it.
    void compile_foreign_expr(std::string_view source, std::uint16_t dst);
    // The same, for source the compiler BUILT rather than one pointing into the
    // program. The text is kept because the AST borrows it.
    void compile_owned_expr(std::string source, std::uint16_t dst);

    // A VIEW, NOT A COPY: the children are already contiguous, and a vector
    // per node visit was 3.9% of rendering a page.
    //
    // The ACTIVE ast, not the outer one: a node reached inside a template
    // literal's sub-AST indexes that AST's pool, and reading the program's would
    // hand back unrelated nodes.
    //
    // Safe because compilation only READS the AST - nothing pushes to the pool
    // while a span into it is alive, and a template's sub-AST is a separate
    // object whose pool this one never reallocates.
    [[nodiscard]] std::span<const std::int32_t> kids(const vp::node & n) const;

    // --- frames and registers ----------------------------------------------
    [[nodiscard]] frame & fn();
    [[nodiscard]] function_proto & proto();

    // Truncating is correct here and the overflow is caught once, at
    // finish_frame, where high_water knows the REAL total. Failing on the first
    // register past the limit would report 256 every time; what a person needs
    // to hear is that the function wanted 1,452.
    [[nodiscard]] std::uint16_t alloc_reg();
    void release_to(std::uint32_t mark);
    [[nodiscard]] std::uint32_t reg_mark() const;

    void push_scope();
    void pop_scope();

    // THE ONLY TWO PLACES `locals` CHANGES SIZE, so that `local_index` cannot
    // drift out of step with it. Every declaration goes through add_local and
    // every scope exit through shrink_locals; a bare `locals.push_back` would
    // leave a name the index cannot find, which is a variable that silently
    // becomes a global read.
    void add_local(frame & f, local l);
    // Unwind from the TOP, which is what makes `pop_back` on each name's stack
    // the right inverse: entries went on in increasing position order, so the
    // highest position is the last one pushed for its name.
    //
    // NOT static any more, because closing a local's live range needs the proto
    // the frame belongs to and a `frame` only carries its index.
    void shrink_locals(frame & f, std::size_t mark);
    // A LOCAL HAS JUST GONE OUT OF SCOPE: stamp the end of its live range, and
    // the boxedness it FINISHED with. Idempotent - closing twice at the same
    // program counter writes the same numbers - which is what lets both
    // `finish_frame` and `pop_scope` do it without either having to know
    // whether the other already did.
    void close_local(const frame & f, const local & l);

    // A NEW, EMPTY PROTO AT THE END OF `out_.functions`, RETURNING ITS INDEX.
    //
    // It exists so that arming the debug tables happens in exactly one place.
    // `function_proto::code_offsets` may only be empty or exactly parallel to
    // `code`, and which of the two it is is a decision per FUNCTION.
    //
    // `at_offset` is where the cursor STARTS: the function's own first byte.
    // The prologue, the parameter defaults and the implicit `return undefined`
    // are emitted with no statement in hand, and attributing them to the
    // function's opening is both true and what a debugger wants - it is where
    // a breakpoint on entry belongs.
    [[nodiscard]] std::uint32_t new_proto(std::uint32_t at_offset);
    [[nodiscard]] std::uint16_t declare_local(std::string name);
    // BIND a local to its export cell, AT MODULE ENTRY - not at the
    // declaration. The cell belongs to the module RECORD and the loader creates
    // it before anything in the graph runs, so what this emits is an adoption:
    // the local's register becomes the record's cell, and every later write in
    // this module is a write the importer reads.
    //
    // AT ENTRY IS THE WHOLE POINT: in a CYCLE - A imports B imports A - B runs
    // first and asks A for a binding A has not reached the declaration of.
    // Creating the binding early and leaving it undefined is what the
    // specification does.
    //
    // ALWAYS BOXED, captured or not: every read has to go through the cell.
    void bind_export(const std::string & name, std::uint16_t reg);

    // BIND EVERY NAME AN `import` STATEMENT INTRODUCES, AT MODULE ENTRY - for
    // the same reason bind_export runs there. Binding at the statement would
    // put the `load_import` AFTER the function-declaration pass, where a
    // module's closures are made, so a function using an imported name would
    // capture the register's PLACEHOLDER cell rather than the exporter's.
    void bind_imports(std::int32_t idx);

    // THE NAME THE SPECIFICATION GIVES `export default`, which is not a legal
    // identifier on purpose - nothing in the module can name it, and it still
    // hoists and binds like any other export.
    static constexpr std::string_view default_binding = "*default*";

    // THE RE-EXPORT EDGES a top-level statement declares. `export { a as b }
    // from './m.js'` and `export * from './m.js'` introduce no binding in this
    // module at all - they say that a name of ANOTHER module is also a name of
    // this one - so there is nothing here for the compiler to emit. It records
    // the edge and the specifier; the loader wires the cell.
    void collect_reexports(std::int32_t idx);

    // The (local, exported) pairs a top-level statement binds. Collected rather
    // than published in place, because the binding pass runs at entry and the
    // statement compiles later.
    void export_bindings(std::int32_t outer,
                         std::vector<std::pair<std::string, std::string>> & out);

    // Bind a name to a register that ALREADY exists. The catch parameter needs
    // it: the handler writes the thrown value into a register chosen when the
    // try block opened, and the name has to refer to that same slot rather than
    // to a fresh one.
    void declare_local_at(std::string name, std::uint16_t reg);
    // -1 when not a local of the CURRENT frame
    [[nodiscard]] int find_local(std::string_view name) const;
    // A local of the CURRENT SCOPE ONLY.
    //
    // find_local_entry searches the whole frame, which is right for a READ - an
    // inner scope sees an outer binding - and wrong for deciding whether a
    // DECLARATION needs a slot of its own. A `const` in a block that shares a
    // name with an outer binding must SHADOW it; treating the outer one as
    // "already declared" makes the inner declaration write THROUGH to it.
    //
    // The index's back() is the INNERMOST entry for the name, so "is it in the
    // current scope" is one comparison against the scope mark: if the
    // innermost one is outside, every other one is further out still.
    [[nodiscard]] local * find_local_in_current_scope(std::string_view name);

    [[nodiscard]] local * find_local_entry(frame & f, std::string_view name);

    // Resolve `name` as an upvalue of frame `level`, adding the descriptor
    // chain if it is not already there. Returns -1 when the name is not a
    // local of any enclosing FUNCTION frame (frame 0 is the script, whose
    // declarations are globals and reachable directly).
    //
    // The recursion is what makes two-level capture work: if the name belongs
    // to a grandparent, the parent first acquires it as its own upvalue, and
    // this frame then captures the parent's upvalue rather than a register.
    [[nodiscard]] int resolve_upvalue(std::size_t level, std::string_view name);

    [[nodiscard]] int add_upvalue(std::size_t level, std::string_view name, upvalue_desc desc);

    // WHICH OF A NODE'S FOUR FIXED SLOTS ARE ACTUALLY CHILDREN.
    //
    // The parser reuses `c` and `d` as BITFIELDS on the kinds that need flags:
    // a rest parameter is `d == 1`, an async function is `c & 1`, a static class
    // member is `d & 1`, an object-literal accessor is `c == 3`. Nothing on a
    // node says which reading applies, so a generic walk over {a, b, c, d}
    // treats those flags as node indices - and index 1 is a real node, so the
    // walk goes back round the tree and never terminates.
    [[nodiscard]] static std::array<std::int32_t, 4> child_slots(const vp::node & n);

    // The `${...}` HOLES of a template literal, as raw text.
    //
    // A template is ONE node carrying its whole source, holes included - the
    // parser does not break the substitutions out into child nodes. So every
    // walk over the tree is blind to them, and the two walks that decide
    // whether a local is BOXED and whether `arguments` is materialised must
    // look inside. Nesting is counted so an object literal or a nested
    // template inside a hole does not end it early.
    template <typename Fn> static void for_each_template_hole(std::string_view raw, Fn && fn) {
        for (std::size_t i = 0; i + 1 < raw.size(); ++i) {
            if (raw[i] != '$' || raw[i + 1] != '{') { continue; }
            if (i > 0 && raw[i - 1] == '\\') { continue; }
            std::size_t depth = 1;
            std::size_t at_char = i + 2;
            const std::size_t start = at_char;
            while (at_char < raw.size() && depth > 0) {
                if (raw[at_char] == '{') { ++depth; }
                if (raw[at_char] == '}') { --depth; }
                if (depth > 0) { ++at_char; }
            }
            fn(raw.substr(start, at_char - start));
            i = at_char;
        }
    }

    // Every identifier-shaped token in a hole.
    //
    // Lexical rather than parsed, and deliberately OVER-approximate: it counts
    // property names and reserved words as well as variables. Naming something
    // that is not really captured only boxes a local that did not need boxing,
    // which is correct and slightly slower; MISSING one reads undefined at run
    // time with nothing to say so.
    template <typename Fn> static void each_name_in_template(std::string_view raw, Fn && add) {
        for_each_template_hole(raw, [&](std::string_view hole) {
            for (std::size_t i = 0; i < hole.size();) {
                const auto begins = [](char c) {
                    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
                };
                const auto continues = [&](char c) { return begins(c) || (c >= '0' && c <= '9'); };
                if (!begins(hole[i])) {
                    ++i;
                    continue;
                }
                const std::size_t start = i;
                while (i < hole.size() && continues(hole[i])) { ++i; }
                add(hole.substr(start, i - start));
            }
        });
    }

    [[nodiscard]] static bool is_function_node(const vp::node & n);

    // WHICH NAMES A NESTED FUNCTION MENTIONS, WITHOUT A SET PER FUNCTION.
    //
    // Materialising a set of names per function is the cost, not the walking:
    // memoising the walk and inserting into a set during it were both measured
    // and both lost (docs/script.md).
    //
    // So no set is materialised. One pass numbers every function in an Euler
    // tour and records, for each name, the tick of the INNERMOST function that
    // mentions it. A name is captured by function F exactly when one of those
    // ticks lies strictly inside F's range - strictly, because a name F
    // mentions itself is not captured by F. That is a binary search, and the
    // memory is one integer per distinct (name, function) pair rather than
    // O(names x nesting depth).
    //
    // BUILT ONCE, THEN READ-ONLY. Nothing mutates it after build_capture_index
    // returns, so concurrent compilation can share it without a lock - which
    // the memoised version could not have done.
    void build_capture_index();

    void tour(std::int32_t idx, std::int32_t enclosing, std::int32_t & tick, bool boundary);

    [[nodiscard]] interval range_of(std::int32_t idx) const;

    // Does this body read `arguments`?
    //
    // Materialising it costs a register and an array per call, so it is only
    // done where the name is actually mentioned - a bundle this size runs far
    // too many calls a frame to pay for it everywhere.
    //
    // Arrows ARE descended into, and other functions are not: an arrow has no
    // `arguments` of its own and sees the enclosing function's, so a mention
    // inside one is a mention here. Making it a real local is what lets the
    // arrow reach it, as an ordinary captured variable.
    [[nodiscard]] bool mentions_arguments(std::int32_t idx) const;
    [[nodiscard]] bool is_captured(std::string_view name) const;

    // Read up to `count` hex digits after position `at`, leaving `at` on the
    // last one consumed so the caller's ++i lands past it. Lenient: a truncated
    // escape yields what digits there were, matching the parser's leniency
    // contract rather than throwing during compilation.
    [[nodiscard]] static std::uint32_t read_hex(std::string_view s, std::size_t & at,
                                                std::size_t count);

    [[nodiscard]] static std::string encode_code_point(std::uint32_t code);

    // The lexer hands back the RAW lexeme, quotes and all - `'a'` arrives as
    // three characters.
    [[nodiscard]] static std::string decode_string_literal(std::string_view lexeme);

    // Names a nested function might close over. Collected BEFORE the body is
    // compiled, because function declarations hoist and are therefore compiled
    // before the `let` that a closure would capture has been reached - without
    // this pre-scan the enclosing-local check simply never fires.
    void collect_declared_names(std::int32_t body);

    // Hoist this body's own `let`/`const`/`var` names into registers before
    // anything is compiled. Nested function declarations hoist too and are
    // compiled first, so the locals they capture have to exist by then.
    void predeclare_locals(std::int32_t body);
    // `var` IS FUNCTION-SCOPED: `if (c) { var x = 1; }` declares `x` in the
    // function, not the block, and webpack emits exactly that shape.
    //
    // Hoisting stops at a nested function, because that function's vars are
    // ITS scope's, and does not descend into a declarator's initialiser, which
    // is an expression and cannot contain a declaration statement.
    template <typename Hoist> void hoist_nested_vars(std::int32_t index, const Hoist & hoist) {
        if (index < 0) { return; }
        const vp::node & n = at(index);
        if (is_function_node(n)) { return; }
        if (n.kind == vp::nk::var_decl && n.text == "var") {
            for (const std::int32_t d : kids(n)) {
                if (at(d).b >= 0) {
                    std::vector<std::string> names;
                    pattern_names(at(d).b, names);
                    for (std::string & name : names) { hoist(std::move(name)); }
                } else {
                    hoist(std::string{at(d).text});
                }
            }
            return;
        }
        for (const std::int32_t slot : child_slots(n)) { hoist_nested_vars(slot, hoist); }
        for (const std::int32_t k : kids(n)) { hoist_nested_vars(k, hoist); }
    }

    [[nodiscard]] bool was_predeclared(std::string_view name) const;

    void fail(std::string message);

    // `function f(a, b = 1, ...rest)`: a default is the param node's `a`
    // child and a rest is `d == 1`.
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
    void compile_parameter_prologue(std::span<const std::int32_t> params,
                                    const std::function<bool(std::uint16_t)> & is_boxed);

    // A numeric literal's value. The radix prefixes take the integer overload
    // and then widen; a double is exact up to 2^53, which is further than any
    // of these literals reach.
    [[nodiscard]] static double number_literal(std::string_view text);

    // What a node kind is CALLED. Only the kinds the compiler can refuse need
    // a name; anything else falls back to the number, which is still better
    // than nothing when a new kind appears in the parser.
    [[nodiscard]] static std::string kind_name(vp::nk kind);

    // --- the operand limits, said out loud ----------------------------------
    //
    // These do not raise any limit. They make the compiler say which one it hit
    // and what it wanted, so a program that does not fit is a message rather
    // than a silent truncation.
    static constexpr std::size_t operand_limit = 65535;    // a uint16 field
    static constexpr std::int32_t jump_limit = 2147483647; // the signed bx half

    [[nodiscard]] std::string frame_name(std::size_t index) const;

    // SCAN WHILE SMALL, INDEX ONCE IT IS NOT.
    //
    // Indexing everything unconditionally made the p5 bundle 6.4% cheaper to
    // load and the Phaser one 0.5% DEARER: p5 has functions mentioning many
    // distinct names, where the quadratic scan hurt, and Phaser has a great
    // many small ones, where building two hash maps per function costs more
    // than the scan it replaces. Sixteen is where a linear scan of short
    // strings stops beating a hash.
    static constexpr std::size_t small_pool = 16;

    [[nodiscard]] static std::uint32_t intern_into(std::vector<std::string> & pool,
                                                   flat_map<std::string, std::uint32_t> & index,
                                                   std::string text);

    // The same answers add_name and add_string give, without the quadratic
    // scan. The NUMBERING IS IDENTICAL either way - an unseen entry is appended
    // and takes the next index - so the bytecode is byte-for-byte the same.
    [[nodiscard]] std::uint32_t intern_name(std::string text);
    [[nodiscard]] std::uint32_t intern_string(std::string text);

    // The seam every property-name operand goes through.
    [[nodiscard]] std::uint16_t name_operand(std::string text);

    // Called where a frame's size is finally written, because that is the only
    // point at which high_water is the truth rather than a running total.
    void finish_frame(std::size_t index, std::size_t params);

    // --- entry --------------------------------------------------------------
    void compile_program();

    // --- destructuring -------------------------------------------------------
    //
    // array_pattern / object_pattern / assign_pattern / rest_element lower into
    // the opcodes that already exist - get_prop, get_index and the ordinary
    // binding paths - so nothing new is needed in the VM. `declaring`
    // distinguishes `const {a} = o`, which introduces a binding, from
    // `({a} = o)`, which writes to one that already exists.

    // Every name a pattern binds, so declarations can be hoisted before the
    // pattern is compiled - which is what makes a nested function able to
    // capture one.
    void pattern_names(std::int32_t pat, std::vector<std::string> & out) const;

    // DECLARE FIRST, THEN WRITE, and OUTSIDE the caller's reg_mark()/
    // release_to(mark). declare_local allocates; a caller that wraps the
    // binding in a mark to free the temporary holding the source hands the
    // locals' registers straight back, and the next temporary in the same
    // scope lands on top of a live local. In a function's top scope the names
    // are hoisted and nothing is allocated, so this only bites in a block.
    //
    // At the top level there is nothing to declare: a declaration there is a
    // global, and emit_write reaches one by falling through to set_global.
    void declare_pattern_names(std::int32_t pat);

    void compile_pattern_binding(std::int32_t pat, std::uint16_t src, bool declaring);

    // Bind `pattern` to the value sitting in `src`. Every name it mentions
    // already exists by the time this runs - see compile_pattern_binding.
    void compile_pattern(std::int32_t pat, std::uint16_t src);

    // Bind an array or object LITERAL, read in expression position, as if it
    // had been parsed as a pattern. Assigning, never declaring - every name in
    // it already exists.
    void compile_literal_as_pattern(std::int32_t literal, std::uint16_t src);

    // One target inside a literal-as-pattern: a name, a member, or a nested
    // literal that is itself a pattern.
    void compile_literal_target(std::int32_t target, std::uint16_t src);

    // `[a, ...rest] = xs` - rest is everything from `from` onward.

    // `{a, ...rest} = o` - every own property except the ones already named.
    void emit_rest_object(std::uint16_t dst, std::uint16_t source,
                          const std::vector<std::string> & taken);

    // --- statements ---------------------------------------------------------
    void compile_stmt(std::int32_t idx);

    void compile_if(const vp::node & n);

    // One live loop. `break` and `continue` are forward jumps whose targets are
    // not known until the loop is finished being compiled, so each records its
    // jump site here and the loop patches them on the way out.
    //
    // `labels` is what makes `break outer;` reach past an inner loop - without
    // it, a labeled break silently becomes an ordinary one and leaves the wrong
    // loop. It is a list because `a: b: for (;;)` names ONE loop twice: every
    // label in a chain is a break and continue target of the statement it ends
    // on (ECMA-262 14.13).
    struct loop_context {
        std::vector<std::string> labels;
        std::vector<std::size_t> breaks;
        std::vector<std::size_t> continues;
        std::size_t handler_depth = 0; // try blocks open when the loop started
    };

    // AN OPEN `finally`, AND WHY THE COMPILER HAS TO KNOW ABOUT ONE.
    //
    // `finally` runs on EVERY way out of a try block, and a `return`, a `break`
    // and a `continue` are three of those ways: an exit stores WHAT it was
    // trying to do into two registers and jumps to the finally, which runs
    // once and then does that thing.
    //
    // `kind` is 0 for a normal fall-through, 1 for a throw with the thrown value
    // in `value`, 2 for a return with the returned value in `value`, and 3 + i
    // for the i-th entry in `exits` - a break or a continue to a particular
    // loop. Break and continue need the index because "which loop" cannot fit
    // in the kind alone, and a numbered exit keeps the dispatch to one compare
    // per target that actually occurs rather than one per loop in scope.
    struct finally_context {
        std::uint16_t kind_reg = 0;
        std::uint16_t value_reg = 0;
        // HOW MANY LOOPS WERE OPEN WHEN THIS `try` STARTED, which is what says
        // whether a given break CROSSES this finally. A break to a loop opened
        // INSIDE the try - `try { for (;;) { break } } finally {}` - never
        // leaves the try block, so it must not be routed here: it would store a
        // completion, jump to the finally, and have the dispatch try to
        // re-emit a break to a loop that has since been popped.
        std::size_t loops_open = 0;
        std::vector<std::size_t> arrivals; // jumps into the finally, patched at it
        struct exit {
            std::size_t loop_index = 0; // into loops_, NOT a pointer: it reallocates
            bool is_continue = false;
        };
        std::vector<exit> exits;
    };

    // The finallys between here and the top of the function, innermost last. A
    // `return` crossing three of them stores its completion in the innermost and
    // lets each dispatch hand it to the next, which is what makes nesting work
    // without any of them knowing how deep they are.
    std::vector<finally_context> finallies_;

    // Where a `return` goes when a finally is open: into the innermost one.
    // Returns false when there is none and the caller should just emit `ret`.
    [[nodiscard]] bool route_return_through_finally(std::uint16_t value_reg);
    // The same for a loop exit. `loop` must be one of loops_.
    [[nodiscard]] bool route_exit_through_finally(std::size_t loop_index, bool is_continue);
    // The tail of a finally: run the completion it was handed.
    void emit_finally_dispatch(const finally_context & open);
    void compile_try_with_finally(const vp::node & n);

    void patch_breaks(loop_context & loop);
    void patch_continues(loop_context & loop, std::size_t target);

    // The loop a break/continue belongs to: the named one, or the innermost.
    [[nodiscard]] loop_context * loop_for(std::string_view label);

    void compile_break(const vp::node & n);

    void compile_continue(const vp::node & n);

    // A labeled statement. Only labels on loops mean anything here: a label on
    // anything else is legal JS but nothing can target it except `break`, and
    // `break` out of a plain block is vanishingly rare.
    void compile_labeled(const vp::node & n);

    void compile_while(const vp::node & n);

    // do..while: the body runs before the first test, which is the whole
    // difference and the reason it cannot share compile_while.
    void compile_do_while(const vp::node & n);

    void compile_for(const vp::node & n);

    // for..of and for..in.
    //
    // Compiled as an index loop over a length rather than through an iterator
    // protocol: op::iterable turns the source into an array of values first,
    // and `for..in` goes through the same loop over an array of keys, so there
    // is one iteration mechanism here and not two. The limit (docs/script.md):
    // an object with a `next()` of its own is not iterated, because nothing
    // dispatches through Symbol.iterator.
    void compile_for_of(const vp::node & n);
    void compile_for_await(const vp::node & n);

    // switch.
    //
    // Two passes: every case's test first, jumping to its body, then the bodies
    // laid out in order so FALLTHROUGH works - a case without a break really
    // does run the next one, and code relies on that.
    void compile_switch(const vp::node & n);

    // try / catch / finally. `finally` is compiled by DUPLICATING its body on
    // both exits - the normal one and the caught one - rather than through a
    // subroutine-return opcode nothing else needs.
    void compile_try(const vp::node & n);

    void compile_throw(const vp::node & n);

    [[nodiscard]] std::vector<std::string> take_labels();

    // Falling off the end of an async function still owes the caller a promise.
    void emit_implicit_return();

    void compile_function_decl(std::int32_t idx);

    [[nodiscard]] std::uint32_t compile_function_body(std::int32_t idx, std::string name);

    // --- expressions ---------------------------------------------------------
    // Every expression goes through here, and a member/call chain containing an
    // optional link is compiled as ONE unit so it has one exit.
    void compile_expr(std::int32_t idx, std::uint16_t dst);

    void compile_expr_inner(std::int32_t idx, std::uint16_t dst);

    // Writing a name has to know whether it lives in a register, a cell, an
    // upvalue or the global table. ++/-- goes through here too, so it cannot
    // drift out of agreement with the read side - which is `compile_ident`.
    void emit_write(std::string_view name_text, std::uint16_t src);

    void compile_ident(const vp::node & n, std::uint16_t dst);

    // `delete o.x` / `delete o[k]`. Anything else - `delete x` on a plain
    // variable - is a no-op that yields false, which is what non-strict
    // JavaScript does with an undeletable binding.
    void compile_delete(const vp::node & n, std::uint16_t dst);

    void compile_binary(const vp::node & n, std::uint16_t dst);

    // && and || must not evaluate the right side unless they have to, so they
    // are control flow rather than an opcode.
    void compile_logical(const vp::node & n, std::uint16_t dst);

    void compile_unary(const vp::node & n, std::uint16_t dst);

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
        std::uint16_t reg = 0;  // local/boxed: its register. member/index: the object.
        std::uint16_t key = 0;  // index: the key register
        std::uint16_t name = 0; // global/member: the name index
    };

    [[nodiscard]] reference prepare_reference(const vp::node & target);

    void emit_load(const reference & ref, std::uint16_t dst);

    void emit_store(const reference & ref, std::uint16_t src);

    // `+=` and friends. The operator is the assignment's text minus its '='.
    [[nodiscard]] static op compound_op(std::string_view text, bool & ok);

    void compile_assign(const vp::node & n, std::uint16_t dst);

    void compile_update(const vp::node & n, std::uint16_t dst);

    void compile_ternary(const vp::node & n, std::uint16_t dst);

    // A template literal. The parser hands the WHOLE thing back as one token,
    // backticks and all, so the splitting happens here: literal chunks are
    // strings, `${...}` chunks are parsed and compiled, and the whole thing is
    // a chain of concatenations.
    void compile_template(const vp::node & n, std::uint16_t dst);

    // `dst` = the object `super` looks properties up on: the prototype ABOVE the
    // one the running method was written into.
    void emit_super_base(std::uint16_t dst);

    [[nodiscard]] bool any_spread(std::span<const std::int32_t> args) const;

    // The arguments of a call, as one array. Same shape as an array literal,
    // because that is exactly what it is.
    void emit_argument_array(std::span<const std::int32_t> args, std::uint16_t dst);

    // `f(...args)`.
    //
    // Every other call form puts its arguments in consecutive registers and the
    // COUNT in an operand, which cannot work when the count is not known until
    // the spread is evaluated. So the arguments become an array and the callee
    // and receiver are resolved into registers first - which collapses all four
    // call forms into one, since by then the receiver is just a register.
    void compile_spread_call(const vp::node & n, std::uint16_t dst);

    // Calls need their arguments in CONSECUTIVE registers starting just above
    // the callee, so the VM can hand the callee a contiguous frame.
    void compile_call(const vp::node & n, std::uint16_t dst);
    void compile_call_target(const vp::node & n, std::uint16_t target, std::uint16_t self);
    void emit_optional_guard(std::uint16_t value);

    // `new C(...)`. The receiver is created by the VM, which also has to decide
    // what the expression evaluates to - the new object, unless the constructor
    // returned one of its own.
    void compile_new(const vp::node & n, std::uint16_t dst);

    // Does this member/call chain contain an optional link ANYWHERE below it?
    // Only the spine is walked - `a?.b(c.d)` is optional, `a.b(c?.d)` is not,
    // because the argument is its own chain.
    [[nodiscard]] bool chain_has_optional(std::int32_t idx) const;

    // `a?.b.c` AND `a?.m()` SHORT-CIRCUIT THE WHOLE CHAIN, not one link: a
    // chain has one exit, and whichever link short-circuits jumps to it, past
    // everything built on it.
    void compile_chain(std::int32_t idx, std::uint16_t dst);

    void compile_optional(const vp::node & n, std::uint16_t dst);

    // The comma operator: evaluate everything, yield the last.
    void compile_sequence(const vp::node & n, std::uint16_t dst);

    // A function whose whole body is `this.x = <init>` for each instance field,
    // in declaration order. `new` runs it against the fresh object before the
    // constructor body, so every instance gets its OWN value - which is the
    // difference between `items = []` meaning an empty array per instance and
    // meaning one array shared by all of them.
    //
    // It is compiled as an ordinary nested function, so an initialiser that
    // mentions an enclosing local captures it as an upvalue like anything else.
    [[nodiscard]] std::uint32_t compile_field_initialiser(const std::vector<std::int32_t> & fields);

    // Bind a class's own name to the class value, by whichever route this
    // frame uses. Harmless for a `class Foo {}` DECLARATION, which binds the
    // same value again a moment later. `force` is for a class EXPRESSION,
    // whose name must be a binding of its own and never a write to something
    // outer: at the top level a declaration's name is a global, but for an
    // expression that would fall through to set_global and CLOBBER any global
    // of the same name.
    void declare_class_name(std::string name, bool force = false);

    // A class, compiled to what it desugars to: a constructor function plus a
    // prototype object holding the methods, with `new` wiring an instance to
    // that prototype and `extends` chaining the prototype objects.
    //
    // `as_declaration` is the difference between `class C {}` and `let x = class C
    // {}`, and there is ONE node kind for both - only the call site knows which.
    // A named class EXPRESSION binds its name inside its own body and NOWHERE
    // ELSE, exactly like a named function expression: `let p5$2 = class p5 {}`
    // must not give the module scope a local named `p5`.
    void compile_class(const vp::node & n, std::uint16_t dst, bool as_declaration = false,
                       std::string_view inferred_name = {});
    // NamedEvaluation (8.4.5, 13.15.2, 14.3.1.2...): an ANONYMOUS function,
    // arrow or class expression takes the name of the binding, property,
    // parameter or pattern element it initialises. Anything else is
    // compile_expr.
    void compile_named_expr(std::int32_t idx, std::uint16_t dst, std::string_view name);
    // `get [key]() {}` / `set [key](v) {}` on `target` - see define_accessor_name.
    void emit_computed_accessor(std::uint16_t target, std::int32_t key, std::int32_t fn_node,
                                bool setter);
    // An array pattern over the iterator protocol - see iterator_open_name.
    // `elements` are the pattern's children; `bind` binds one element node to
    // the register holding its value (a binding pattern and an assignment
    // pattern bind differently, the iteration is the same).
    void compile_array_pattern(std::span<const std::int32_t> elements, std::uint16_t src,
                               vp::nk rest_kind,
                               const std::function<void(std::int32_t, std::uint16_t)> & bind);
    // One call of one of the three iterator natives; the record (or the
    // item) lands in `dst`.
    void emit_iterator_native(std::string_view name, std::uint16_t dst, std::uint16_t arg,
                              int flag = -1);

    // `/ab+c/gi`. The lexer hands the literal over whole, delimiters and all,
    // so the source is between the first `/` and the last one and the flags are
    // what follows.
    //
    // It compiles to a CALL of the reserved factory rather than to an opcode:
    // a regex is an ordinary object here, and the standard library is the only
    // thing that needs to know how one is built. Reserved rather than `RegExp`
    // so a page that shadows the constructor cannot change what its own
    // literals mean.
    void compile_regex_literal(const vp::node & n, std::uint16_t dst);

    void compile_array(const vp::node & n, std::uint16_t dst);

    // Append every element of `source` to the array in `target`.
    void emit_append_all(std::uint16_t target, std::uint16_t source);

    void compile_object(const vp::node & n, std::uint16_t dst);

    // --- helpers -------------------------------------------------------------
    void emit_string(std::uint16_t dst, std::string text);
    void emit_const(std::uint16_t dst, value v);

    void patch_here(std::size_t at_index);
    void patch_jump(std::size_t at_index, std::size_t target);

    const vp::ast & ast_;
    const vp::ast * current_ast_ = &ast_;
    std::vector<std::unique_ptr<vp::ast>> owned_asts_;
    // SOURCE TEXT THE COMPILER MADE UP, kept alive for as long as the ASTs that
    // borrow it. A parse holds string_views into its input, so a snippet built
    // here - a parenthesised template hole, a synthesised constructor - cannot
    // be a temporary.
    std::vector<std::unique_ptr<std::string>> owned_sources_;
    program & out_;
    std::vector<frame> frames_;
    // The capture index: node -> its Euler-tour range, and name -> the ticks of
    // the innermost functions mentioning it. Read-only after build.
    std::vector<interval> fn_range_;
    boost::unordered_flat_map<std::string, std::vector<std::int32_t>, sv_hash, std::equal_to<>>
        mentions_;
    std::vector<loop_context> loops_;
    std::vector<std::string> pending_labels_;
    // The short-circuit jumps of the optional chain being compiled, and
    // whether one is open - see compile_chain.
    std::vector<std::size_t> optional_exits_;
    bool in_chain_ = false;
    std::size_t handler_depth_ = 0;
};

} // namespace detail

} // namespace ctbrowser::script
