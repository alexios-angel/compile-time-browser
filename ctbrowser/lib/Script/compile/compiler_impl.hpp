#pragma once
// The compiler proper, declared so its 3,700 lines of bodies can live in more
// than one file. Private to lib/Script/compile/ - NOT installed, in no file
// set, and deliberately not under include/: it includes <ctjs/vparse.hpp> and
// Boost, and CLAUDE.md's "no third-party header in a public header" invariant
// is policed by test/lint/api_surface. The precedent is
// lib/Script/builtins/internal.hpp.
//
// `include/ctbrowser/script/compile.hpp` still declares exactly one function.
// That is the property docs/architecture.md praises and nothing here changes it.
//
// NAMED namespace, not anonymous. compiler_impl was in `namespace { }` while it
// had one translation unit; an anonymous namespace CANNOT be shared through a
// header - each .cpp would get its own distinct class, and
// `compiler_impl::compile_stmt` defined in one file would be a member of a
// different type from the one another file calls. `detail` rather than plain
// `script` because giving 114 functions and six nested types external linkage
// in the subsystem's own namespace is an ODR accident waiting to happen: `frame`
// reads like a VM call frame, which vm.hpp really has, and `local`, `interval`
// and `reference` are generic enough to collide with anything. They stay NESTED.

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/number_format.hpp>

#include <boost/container/small_vector.hpp>
// unordered_flat_MAP, for mentions_. The include said _set, for a `name_set`
// alias nothing used; the map it actually needs was arriving transitively
// through core/containers.hpp and would have broken the day that stopped.
#include <boost/unordered/unordered_flat_map.hpp>

#include <algorithm>
#include <ranges>
#include <span>

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

// The alias stays at `script` scope, where it was: `compiler::compile()` in
// compile.cpp names `vp::ast` and `vp::parse` too, and `detail` finds it by
// enclosing-namespace lookup.
namespace vp = ctjs::vp;

namespace detail {

class compiler_impl {
public:
    struct local {
        std::string name;
        std::uint16_t reg = 0;
        bool boxed = false; // lives in a heap cell; see mark_captured
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
        // NAME -> THE POSITIONS IN `locals` THAT CARRY IT, innermost last.
        //
        // `locals` is a stack and `find_local_entry` wanted the LAST entry with
        // a given name, which it found by scanning the whole vector backwards
        // and comparing strings. On Babylon's bundle that was 35.2 million
        // memcmp calls from `compile_ident` alone - the scan is O(locals) and it
        // runs once per identifier MENTION, so a big function is quadratic in
        // its own size. See docs/performance.md.
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
        // WIDER THAN THE OPERAND THEY FEED, on purpose. A register index is a
        // uint8 in an instruction, and these used to be uint8 too - so a
        // function wanting more than 256 registers wrapped to r0 in silence and
        // its locals aliased each other. Counting in a wider type does not make
        // the bytecode hold more; it makes the compiler able to SAY how many
        // were wanted, which is the difference between a diagnostic and a bug.
        std::uint32_t next_reg = 0;
        std::uint32_t high_water = 0;
        bool is_async = false;     // `return v` hands back a settled promise of v
        bool is_generator = false; // `function*` - calling it does not run it
        // WHERE A NAME OR STRING ALREADY WENT.
        //
        // `function_proto::add_name` and `add_string` deduplicate by LINEAR
        // SCAN, which is quadratic in the distinct names a function mentions
        // and does a std::string compare at every step. On the p5.js bundle
        // that was 4.4% of the load in add_name alone, plus most of an 8.3%
        // memcmp.
        //
        // The index lives HERE rather than on the proto because it is wanted
        // only while compiling: a shipped program carries the vectors and
        // should not also carry a hash map per function.
        flat_map<std::string, std::uint32_t> name_index;
        flat_map<std::string, std::uint32_t> string_index;
    };

    compiler_impl(const vp::ast & tree, program & out)
        : ast_(tree), current_ast_(&tree), out_(out) {}

    // A MODULE'S TOP LEVEL IS A SCOPE, NOT THE GLOBAL OBJECT. Set by
    // compiler::compile from the script_kind; see the note on that enum for why
    // the distinction is not cosmetic.
    bool module_scope_ = false;

    // --- AST access -------------------------------------------------------
    // A NEGATIVE INDEX IS "NOTHING", not an address.
    //
    // Every fixed child slot is -1 when absent, and an array literal's element
    // list holds -1 for a hole - so `at(x).kind` on an unchecked index read
    // past the end of the pool and segfaulted. Returning an empty node makes a
    // missed check compile to nothing instead of crashing, which is the right
    // failure for a compiler to have.
    // DEFINED HERE, and measured rather than assumed. Splitting compile.cpp
    // cost +4.08% instructions on a whole page render, and callgrind attributed
    // essentially all of it to this one function: out of line it appeared at
    // 4.18% / 32.4 M instructions, having been absent from the profile entirely
    // while it was inlined into every caller. Every other member moved for free.
    //
    // It is five lines and the whole compiler reads the node pool through it.
    // `static const vp::node nothing` is still one object across all the files
    // that include this - a function-local static in an inline function is
    // guaranteed to be.
    [[nodiscard]] const vp::node & at(std::int32_t i) const {
        static const vp::node nothing{vp::nk::empty, ""};
        if (i < 0 || static_cast<std::size_t>(i) >= current_ast_->nodes.size()) { return nothing; }
        return current_ast_->nodes[static_cast<std::size_t>(i)];
    }

    // Compile an expression parsed from a DIFFERENT source than the program's.
    //
    // Template literals need it: the parser hands back `${...}` as raw text
    // inside one token, so the interpolations have to be parsed separately.
    // Node indices are per-AST, so the active one is swapped for the duration
    // and every `at()` follows it.
    // The same, for source the compiler BUILT rather than one pointing into the
    // program. The text is kept because the AST borrows it.
    void compile_owned_expr(std::string source, std::uint16_t dst);

    void compile_foreign_expr(std::string_view source, std::uint16_t dst);
    // A VIEW, NOT A COPY. This built and returned a std::vector on every call,
    // so every walk of the AST - and there are several, over every node - paid a
    // malloc and a free per node visit. Callgrind put it at 3.9% of rendering a
    // page with malloc/free above it, for children that were already contiguous.
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
    static void shrink_locals(frame & f, std::size_t mark);
    [[nodiscard]] std::uint16_t declare_local(std::string name);
    // A NAME THAT IS ALREADY A CELL. An imported binding is the EXPORTER's
    // cell - that is what makes it live - so unlike declare_local this must not
    // emit `new_cell`, which would box the cell and leave the importer reading
    // a box containing a box. It is always boxed, whether or not anything in
    // this module captures it, because every read has to go through the cell to
    // see the exporter's later writes.
    // BIND a local to its export cell, AT MODULE ENTRY - not at the
    // declaration. The cell belongs to the module RECORD and the loader creates
    // it before anything in the graph runs, so what this emits is an adoption:
    // the local's register becomes the record's cell, and every later write in
    // this module is a write the importer reads.
    //
    // AT ENTRY IS THE WHOLE POINT. Publishing at the declaration site instead
    // worked for a straight line and could not work for a CYCLE: A imports B
    // imports A, so B runs first and asks A for a binding A has not reached the
    // declaration of. Creating the binding early and leaving it undefined is
    // what the specification does, and it is why a cycle sees an uninitialised
    // binding rather than a missing one.
    //
    // ALWAYS BOXED, captured or not: every read has to go through the cell.
    void bind_export(const std::string & name, std::uint16_t reg);

    // BIND EVERY NAME AN `import` STATEMENT INTRODUCES, AT MODULE ENTRY - for
    // the same reason bind_export runs there, and found the same way.
    //
    // Binding at the statement instead put the `load_import` AFTER the
    // function-declaration pass, which is where a module's closures are made.
    // So a function that used an imported name captured the register's
    // PLACEHOLDER cell, and `load_import` then replaced the register with the
    // exporter's - leaving the closure holding a box nobody would ever write
    // to. It read `undefined`, and it did so whether or not there was a cycle:
    // `a.js` imports `b.js` and calls it from a function, and the call returned
    // undefined with no error at all. The cycle was not the fault; it was just
    // the first shape that showed it.
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
    // p5.js has a top-level `function boolean(...)` and, inside a block, a
    // `const { boolean } = ...`. Both wrote to one cell, so zod's builder was
    // replaced by a boolean `true` - and the failure surfaced 25,000
    // instructions later as "a captured variable is boolean (true), not a
    // function".
    // The index's back() is the INNERMOST entry for the name, which is what the
    // backward scan this replaced returned. Everything before it is shadowed, so
    // "is it in the current scope" is one comparison against the scope mark
    // rather than a walk: if the innermost one is outside, every other one is
    // further out still.
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
    [[nodiscard]] static std::array<std::int32_t, 4> child_slots(const vp::node & n);

    // The `${...}` HOLES of a template literal, as raw text.
    //
    // A template is ONE node carrying its whole source, holes included - the
    // parser does not break the substitutions out into child nodes. So every
    // walk over the tree is blind to them, and the two walks that matter are
    // the ones that decide whether a local is BOXED and whether `arguments` is
    // materialised. A name used only inside a hole was invisible to both: the
    // enclosing frame never boxed it, the nested function resolved it as a
    // global, and it read undefined.
    //
    // Nesting is counted so an object literal or a nested template inside a
    // hole does not end it early.
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
    // This used to walk each function's whole subtree once per ENCLOSING
    // function - 18,906 calls and 16.5 million node visits on the p5.js bundle,
    // about eighty visits per node. Two obvious repairs were measured and both
    // failed (docs/script.md): memoising the walk moved the quadratic from the
    // traversal into the set copying and won 0.3%, and inserting into a set
    // during the walk was 3% WORSE than building a vector and deduplicating
    // once. The cost was never the walking - it was materialising a set of
    // names for every function.
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

    // The lexer hands back the RAW lexeme, quotes and all - `'a'` arrives as
    // three characters. Without this, every string literal in the program is
    // wrong by two characters, which shows up as 'a' + 'b' === "'a''b'" and
    // as o['a'] failing to find the property named a.
    // Read up to `count` hex digits after position `at`, leaving `at` on the
    // last one consumed so the caller's ++i lands past it. Lenient: a truncated
    // escape yields what digits there were, matching the parser's leniency
    // contract rather than throwing during compilation.
    [[nodiscard]] static std::uint32_t read_hex(std::string_view s, std::size_t & at,
                                                std::size_t count);

    [[nodiscard]] static std::string encode_code_point(std::uint32_t code);

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
    // `var` IS FUNCTION-SCOPED. `let` and `const` are not, and until now the
    // compiler could not tell them apart - the parser has always put the
    // keyword on the node and nothing read it.
    //
    // So `if (c) { var x = 1; }` declared `x` in the BLOCK, the block's scope
    // popped it, and every later read - including one from a nested function -
    // found undefined. Phaser 4 is built by webpack, which emits exactly that
    // shape for its feature flags:
    //
    //     if (true) { var SoundManagerCreator = __webpack_require__(14747); }
    //     var Game = new Class({ initialize: function Game () {
    //         this.sound = SoundManagerCreator.create(this);   // undefined
    //     }});
    //
    // p5 never reached it because p5 is modern code that uses let and const.
    // Two libraries, and the second found it in an afternoon.
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
    void compile_parameter_prologue(std::span<const std::int32_t> params);

    // A numeric literal's value.
    //
    // This was one call to std::from_chars in `general` format, which stops at
    // the `x` - so every `0xFF` in the program was the number ZERO, silently.
    // p5.js has 734 of them, spread through colour maths, bit masks and font
    // tables, and not one would have produced an error.
    //
    // The radix prefixes take the integer overload and then widen; a double is
    // exact up to 2^53, which is further than any of these literals reach.
    [[nodiscard]] static double number_literal(std::string_view text);

    // What a node kind is CALLED. Only the kinds the compiler can refuse need
    // a name; anything else falls back to the number, which is still better
    // than nothing when a new kind appears in the parser.
    [[nodiscard]] static std::string kind_name(vp::nk kind);

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
    static constexpr std::size_t operand_limit = 65535;    // a uint16 field
    static constexpr std::int32_t jump_limit = 2147483647; // the signed bx half

    [[nodiscard]] std::string frame_name(std::size_t index) const;

    // The seam every property-name operand goes through. It was the place the
    // 256-name cap was reported; now it is the place the widening paid off, and
    // the check that remains is for a limit no real program reaches.
    // The same answers add_name and add_string give, without the scan. The
    // NUMBERING IS IDENTICAL: an unseen entry is appended and takes the next
    // index, which is exactly what the linear versions did - the bytecode is
    // byte-for-byte the same.
    // SCAN WHILE SMALL, INDEX ONCE IT IS NOT.
    //
    // Indexing everything unconditionally made the p5 bundle 6.4% cheaper to
    // load and the Phaser one 0.5% DEARER: p5 has functions mentioning many
    // distinct names, where the quadratic scan hurt, and Phaser has a great
    // many small ones, where building two hash maps per function costs more
    // than the scan it replaces. Most functions mention a handful of names.
    //
    // So the scan stays for the small case and the index is built on crossing.
    // Sixteen is where a linear scan of short strings stops beating a hash.
    static constexpr std::size_t small_pool = 16;

    [[nodiscard]] static std::uint32_t intern_into(std::vector<std::string> & pool,
                                                   flat_map<std::string, std::uint32_t> & index,
                                                   std::string text);

    // The same answers add_name and add_string give, without the quadratic
    // scan. The NUMBERING IS IDENTICAL either way - an unseen entry is appended
    // and takes the next index - so the bytecode is byte-for-byte the same.
    [[nodiscard]] std::uint32_t intern_name(std::string text);
    [[nodiscard]] std::uint32_t intern_string(std::string text);

    [[nodiscard]] std::uint16_t name_operand(std::string text);

    // Called where a frame's size is finally written, because that is the only
    // point at which high_water is the truth rather than a running total.
    void finish_frame(std::size_t index, std::size_t params);

    // --- entry --------------------------------------------------------------
    void compile_program();

    // --- destructuring -------------------------------------------------------
    //
    // A binding position may hold a SHAPE. `const {a, b} = o` and
    // `function f([x, y])` are not one binding with a funny name; they are a
    // read out of the value for each name inside. The parser now produces
    // array_pattern / object_pattern / assign_pattern / rest_element, and this
    // lowers them into the opcodes that already exist - get_prop, get_index and
    // the ordinary binding paths - so nothing new is needed in the VM.
    //
    // `declaring` distinguishes `const {a} = o`, which introduces a binding,
    // from `({a} = o)`, which writes to one that already exists.

    // Every name a pattern binds, so declarations can be hoisted before the
    // pattern is compiled - which is what makes a nested function able to
    // capture one.
    void pattern_names(std::int32_t pat, std::vector<std::string> & out) const;

    // DECLARE FIRST, THEN WRITE - and the order is not a style choice.
    //
    // Binding each name as the walk reached it meant declare_local allocated a
    // register INSIDE the scope of the release_to() that frees the element
    // temporary, so the next element's temporary reused the local's register
    // and every name in the pattern ended up sharing one slot: `f({x, y})`
    // returned x+x. Declaring the whole shape's names up front leaves the walk
    // with nothing to allocate, so its temporaries are free to be released.
    //
    // At the top level there is nothing to declare: a declaration there is a
    // global, and emit_write reaches one by falling through to set_global.
    // Give every name a pattern binds a register, before anything is written.
    //
    // SEPARATE FROM compile_pattern_binding BECAUSE OF WHERE IT HAS TO HAPPEN.
    // declare_local allocates, and a caller that wraps the whole thing in
    // reg_mark()/release_to(mark) - which every `const {a} = expr` site does, to
    // free the temporary holding expr - hands those registers straight back. The
    // next temporary in the same scope then lands on top of a live local: inside
    // a try block, `const { data } = f(); return 'len=' + data.length` read
    // `data` as the string "len=". Declaring OUTSIDE the mark is the fix.
    //
    // It only bit inside a block. In a function's top scope the names are
    // hoisted, so find_local_in_current_scope finds them and nothing is
    // allocated - which is why every test of this until now passed.
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
    void emit_slice_from(std::uint16_t dst, std::uint16_t source, std::size_t from);

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
    // `label` is what makes `break outer;` reach past an inner loop - without
    // it, a labeled break silently becomes an ordinary one and leaves the wrong
    // loop.
    struct loop_context {
        std::string label;
        std::vector<std::size_t> breaks;
        std::vector<std::size_t> continues;
        std::size_t handler_depth = 0; // try blocks open when the loop started
    };

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
    // protocol: a real iterator needs Symbol.iterator dispatch, and an index loop
    // is what every case a page actually writes reduces to. `for..in` goes through
    // the same loop over an array of keys, so there is one iteration mechanism
    // here and not two.
    //
    // What makes that safe is op::iterable, which turns the source into an array
    // of values first - arrays, strings, Maps, Sets and the views they hand out.
    // Without it a Map had no `length` and the loop ran ZERO times in silence.
    //
    // The limit that remains, and it is written down in docs/script.md: an object
    // with a `next()` of its own is not iterated, because nothing dispatches
    // through Symbol.iterator.
    void compile_for_of(const vp::node & n);

    // switch.
    //
    // Two passes: every case's test first, jumping to its body, then the bodies
    // laid out in order so FALLTHROUGH works - a case without a break really
    // does run the next one, and code relies on that.
    void compile_switch(const vp::node & n);

    // try / catch / finally.
    //
    // `finally` is compiled by DUPLICATING its body on both exits - the normal
    // one and the caught one. The alternative is a subroutine-return opcode,
    // and duplication is the honest trade at this size: two copies of a small
    // block against a control-flow mechanism nothing else needs. A `return`
    // inside a try does NOT run the finally block, which is a real gap and is
    // noted rather than hidden.
    void compile_try(const vp::node & n);

    void compile_throw(const vp::node & n);

    [[nodiscard]] std::string take_label();

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
    //
    // There was an `emit_read` here saying exactly that about itself, with a
    // body identical to compile_ident's but for taking a string_view instead of
    // a node. Nothing called it. Deleted 2026-08-09; the drift it was written to
    // prevent had already happened to it.
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

    // Calls need their arguments in CONSECUTIVE registers starting just above
    // the callee, so the VM can hand the callee a contiguous frame.
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
    //
    // `nk::spread` was not a case in compile_expr at all, so this used to reach
    // the default arm and refuse the whole call. It stops thirteen of p5.js's
    // seventy-one modules, more than any other single construct.
    void compile_spread_call(const vp::node & n, std::uint16_t dst);

    void compile_call(const vp::node & n, std::uint16_t dst);

    // `new C(...)`. The receiver is created by the VM, which also has to decide
    // what the expression evaluates to - the new object, unless the constructor
    // returned one of its own.
    void compile_new(const vp::node & n, std::uint16_t dst);

    // Optional chaining. The whole point is the SHORT CIRCUIT: `a?.b.c` yields
    // undefined without evaluating `.c` when a is null-ish, so writing it as an
    // ordinary member access with a test afterwards would still crash.
    // Does this member/call chain contain an optional link ANYWHERE below it?
    // Only the spine is walked - `a?.b(c.d)` is optional, `a.b(c?.d)` is not,
    // because the argument is its own chain.
    [[nodiscard]] bool chain_has_optional(std::int32_t idx) const;

    // `a?.b.c` AND `a?.m()` SHORT-CIRCUIT THE WHOLE CHAIN, not one link.
    //
    // Each optional link used to jump only past itself, leaving undefined in
    // the register - and then the rest of the chain ran on it. `o?.m()` with a
    // null `o` therefore CALLED undefined, which is exactly how p5.js stopped:
    // "the result of opcode 2 is undefined, not a function", four thousand
    // instructions into the bundle with nothing to say which line.
    //
    // The fix is that a chain has one exit. Whichever link short-circuits jumps
    // to the same place, past everything built on it.
    void compile_chain(std::int32_t idx, std::uint16_t dst);

    void compile_optional(const vp::node & n, std::uint16_t dst);

    // The comma operator: evaluate everything, yield the last.
    void compile_sequence(const vp::node & n, std::uint16_t dst);

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
    // same value again a moment later.
    // `force` is for a class EXPRESSION, whose name must be a binding of its own
    // and never a write to something outer. At the top level a declaration's name
    // is a global, so the frame-depth test is right for that case - but for an
    // expression it meant the name had no binding at all and emit_write fell
    // through to set_global, CLOBBERING any global of the same name. `var Shared =
    // {...}; var alias = class Shared {}` replaced the object with the class.
    void declare_class_name(std::string name, bool force = false);

    // `as_declaration` is the difference between `class C {}` and `let x = class C
    // {}`, and there is ONE node kind for both - only the call site knows which.
    //
    // A named class EXPRESSION binds its name inside its own body and NOWHERE
    // ELSE, exactly like a named function expression. Binding it in the enclosing
    // scope broke p5.js in a way that took an afternoon to find: the bundle has
    // `let p5$2 = class p5 { ... }`, so the module scope acquired a local named
    // `p5` holding undefined, and every function compiled AFTER that point
    // captured it instead of the global. `new p5.TableRow()` inside p5.Table's
    // addRow read undefined.TableRow - which this engine answers with undefined
    // rather than a TypeError - and reported "`new` on `TableRow` is undefined",
    // naming the wrong thing entirely.
    //
    // Compile ORDER decided whether it bit, which is why it looked so arbitrary:
    // a hoisted function declaration is compiled before the leak exists and reads
    // the global correctly, and a class method three thousand lines later does
    // not.
    void compile_class(const vp::node & n, std::uint16_t dst, bool as_declaration = false);

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
    std::string pending_label_;
    // The short-circuit jumps of the optional chain being compiled, and
    // whether one is open - see compile_chain.
    std::vector<std::size_t> optional_exits_;
    bool in_chain_ = false;
    std::size_t handler_depth_ = 0;
};

} // namespace detail

} // namespace ctbrowser::script
