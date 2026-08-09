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

namespace vp = ctjs::vp;

namespace {

// The compiler proper. Everything here was in the module interface before, so
// every translation unit that imported it re-instantiated the lot; it is an
// implementation detail and it lives in one translation unit now.
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
    void compile_owned_expr(std::string source, std::uint16_t dst) {
        owned_sources_.push_back(std::make_unique<std::string>(std::move(source)));
        compile_foreign_expr(*owned_sources_.back(), dst);
    }

    void compile_foreign_expr(std::string_view source, std::uint16_t dst) {
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
    [[nodiscard]] std::span<const std::int32_t> kids(const vp::node & n) const {
        if (n.list < 0 || n.list_len <= 0) { return {}; }
        return std::span<const std::int32_t>{current_ast_->pool.data() + n.list,
                                             static_cast<std::size_t>(n.list_len)};
    }

    // --- frames and registers ----------------------------------------------
    [[nodiscard]] frame & fn() { return frames_.back(); }
    [[nodiscard]] function_proto & proto() { return out_.functions[fn().proto]; }

    // Truncating is correct here and the overflow is caught once, at
    // finish_frame, where high_water knows the REAL total. Failing on the first
    // register past the limit would report 256 every time; what a person needs
    // to hear is that the function wanted 1,452.
    [[nodiscard]] std::uint16_t alloc_reg() {
        const std::uint32_t r = fn().next_reg++;
        if (fn().next_reg > fn().high_water) { fn().high_water = fn().next_reg; }
        return static_cast<std::uint16_t>(r);
    }
    void release_to(std::uint32_t mark) { fn().next_reg = mark; }
    [[nodiscard]] std::uint32_t reg_mark() const { return frames_.back().next_reg; }

    void push_scope() { fn().scope_marks.push_back(fn().locals.size()); }
    void pop_scope() {
        const std::size_t mark = fn().scope_marks.back();
        fn().scope_marks.pop_back();
        shrink_locals(fn(), mark);
    }

    // THE ONLY TWO PLACES `locals` CHANGES SIZE, so that `local_index` cannot
    // drift out of step with it. Every declaration goes through add_local and
    // every scope exit through shrink_locals; a bare `locals.push_back` would
    // leave a name the index cannot find, which is a variable that silently
    // becomes a global read.
    void add_local(frame & f, local l) {
        f.local_index[l.name].push_back(static_cast<std::uint32_t>(f.locals.size()));
        f.locals.push_back(std::move(l));
    }
    // Unwind from the TOP, which is what makes `pop_back` on each name's stack
    // the right inverse: entries went on in increasing position order, so the
    // highest position is the last one pushed for its name.
    static void shrink_locals(frame & f, std::size_t mark) {
        for (std::size_t i = f.locals.size(); i-- > mark;) {
            const auto it = f.local_index.find(std::string_view{f.locals[i].name});
            if (it == f.local_index.end()) { continue; }
            it->second.pop_back();
            if (it->second.empty()) { f.local_index.erase(it); }
        }
        f.locals.resize(mark);
    }
    [[nodiscard]] std::uint16_t declare_local(std::string name) {
        const std::uint16_t r = alloc_reg();
        const bool boxed = is_captured(name);
        add_local(fn(), local{std::move(name), r, boxed});
        return r;
    }
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
    void bind_export(const std::string & name, std::uint16_t reg) {
        for (local & each : fn().locals) {
            if (each.reg == reg) { each.boxed = true; }
        }
        proto().emit(instruction::with_bx(op::bind_export, reg, name_operand(name)));
        if (std::ranges::find(out_.exports, name) == out_.exports.end()) {
            out_.exports.push_back(name);
        }
    }

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
    void bind_imports(std::int32_t idx) {
        const vp::node & n = at(idx);
        if (n.kind != vp::nk::import_decl) { return; }
        // THE SPECIFIER IS RECORDED FOR THE LOADER, which walks the graph
        // without re-parsing anything.
        //
        // DECODED, because the parser hands over the token as written - quotes
        // included. Recording `'./m.js'` rather than `./m.js` made every lookup
        // miss and every import read undefined, with the loader reporting a
        // module it had never been asked for.
        const std::string specifier = decode_string_literal(n.text);
        if (std::ranges::find(out_.imports, specifier) == out_.imports.end()) {
            out_.imports.push_back(specifier);
        }
        const std::uint16_t from = name_operand(specifier);
        for (const std::int32_t spec_index : kids(n)) {
            const vp::node & spec = at(spec_index);
            // `c`: 0 named, 1 default, 2 namespace. A namespace import wants
            // the whole module object, which is stage 4 work - it is refused by
            // name rather than bound to nothing.
            if (spec.c == 2) {
                // `import * as ns`: one binding, holding the exporter's
                // namespace object. NOT a cell - the namespace is itself the
                // live thing, because each of its properties reads a cell.
                const int hoisted_ns = find_local(spec.text);
                if (hoisted_ns < 0) {
                    fail("`import * as " + std::string{spec.text} +
                         "` was not hoisted - see predeclare_locals");
                    return;
                }
                // THROUGH THE CELL IF IT IS ONE. A namespace binding captured
                // by a function was hoisted BOXED, so writing the register
                // directly would throw the cell away and leave the closure
                // reading undefined - the same shape of bug that made an
                // imported function called from a closure read undefined.
                bool boxed = false;
                for (const local & each : fn().locals) {
                    if (each.name == spec.text) { boxed = each.boxed; }
                }
                if (boxed) {
                    const std::uint16_t temp = alloc_reg();
                    proto().emit(instruction{op::load_namespace, temp, from});
                    proto().emit(
                        instruction{op::cell_set, static_cast<std::uint16_t>(hoisted_ns), temp});
                } else {
                    proto().emit(instruction{op::load_namespace,
                                             static_cast<std::uint16_t>(hoisted_ns), from});
                }
                continue;
            }
            // The name the EXPORTER knows it by: the original when renamed,
            // otherwise the same as the local one. `default` for a default
            // import, which is the name the specification gives it.
            const std::string exported = spec.c == 1   ? std::string{"default"}
                                         : spec.a >= 0 ? std::string{at(spec.a).text}
                                                       : std::string{spec.text};
            const std::uint16_t what = name_operand(exported);
            const int hoisted = find_local(spec.text);
            if (hoisted < 0) {
                fail("`import` of `" + std::string{spec.text} +
                     "` was not hoisted - see predeclare_locals");
                return;
            }
            const auto r = static_cast<std::uint16_t>(hoisted);
            proto().emit(instruction{op::load_import, r, what, from});
            // ALWAYS BOXED, whether or not this module captures it: every read
            // has to go through the exporter's cell to see the writes that make
            // the binding live. And no `new_cell` - load_import has already put
            // the exporter's box here, and boxing a box leaves the importer
            // reading a cell containing a cell.
            for (local & each : fn().locals) {
                if (each.name == spec.text) { each.boxed = true; }
            }
        }
    }

    // THE NAME THE SPECIFICATION GIVES `export default`, which is not a legal
    // identifier on purpose - nothing in the module can name it, and it still
    // hoists and binds like any other export.
    static constexpr std::string_view default_binding = "*default*";

    // THE RE-EXPORT EDGES a top-level statement declares. `export { a as b }
    // from './m.js'` and `export * from './m.js'` introduce no binding in this
    // module at all - they say that a name of ANOTHER module is also a name of
    // this one - so there is nothing here for the compiler to emit. It records
    // the edge and the specifier; the loader wires the cell.
    void collect_reexports(std::int32_t idx) {
        const vp::node & n = at(idx);
        if (n.kind != vp::nk::export_decl || n.text.empty()) { return; }
        const std::string specifier = decode_string_literal(n.text);
        if (std::ranges::find(out_.imports, specifier) == out_.imports.end()) {
            out_.imports.push_back(specifier);
        }
        if (n.c == 2) {
            if (kids(n).empty()) {
                // `export * from './m.js'`: every name it exports.
                out_.reexports.push_back(program::reexport{"", "", specifier});
                return;
            }
            // `export * as ns from './m.js'`: ONE name, holding the namespace.
            // Not built - a namespace is a value rather than a cell, so it does
            // not fit the alias the loader wires - and refused by name rather
            // than silently exporting nothing.
            fail("`export * as ns from` is not implemented yet - ES modules are staged in "
                 "docs/plans/modules.md");
            return;
        }
        for (const std::int32_t spec_index : kids(n)) {
            const vp::node & spec = at(spec_index);
            out_.reexports.push_back(program::reexport{spec.a >= 0 ? std::string{at(spec.a).text}
                                                                   : std::string{spec.text},
                                                       std::string{spec.text}, specifier});
        }
    }

    // The (local, exported) pairs a top-level statement binds. Collected rather
    // than published in place, because the binding pass runs at entry and the
    // statement compiles later.
    void export_bindings(std::int32_t outer,
                         std::vector<std::pair<std::string, std::string>> & out) {
        const vp::node & n = at(outer);
        if (n.kind != vp::nk::export_decl || !n.text.empty() || n.c == 2) { return; }
        if (n.c == 1) {
            out.emplace_back(std::string{default_binding}, "default");
            return;
        }
        if (n.a >= 0) {
            const vp::node & declared = at(n.a);
            if (declared.kind == vp::nk::var_decl) {
                for (const std::int32_t d : kids(declared)) {
                    if (!at(d).text.empty()) {
                        out.emplace_back(std::string{at(d).text}, std::string{at(d).text});
                    }
                }
            } else if (!declared.text.empty()) {
                out.emplace_back(std::string{declared.text}, std::string{declared.text});
            }
            return;
        }
        for (const std::int32_t spec_index : kids(n)) {
            const vp::node & spec = at(spec_index);
            out.emplace_back(std::string{spec.text},
                             spec.a >= 0 ? std::string{at(spec.a).text} : std::string{spec.text});
        }
    }

    // Bind a name to a register that ALREADY exists. The catch parameter needs
    // it: the handler writes the thrown value into a register chosen when the
    // try block opened, and the name has to refer to that same slot rather than
    // to a fresh one.
    void declare_local_at(std::string name, std::uint16_t reg) {
        const bool boxed = is_captured(name);
        if (boxed) { proto().emit(instruction{op::new_cell, reg}); }
        add_local(fn(), local{std::move(name), reg, boxed});
    }
    // -1 when not a local of the CURRENT frame
    [[nodiscard]] int find_local(std::string_view name) const {
        const frame & f = frames_.back();
        for (std::size_t i = f.locals.size(); i-- > 0;) {
            if (f.locals[i].name == name) { return f.locals[i].reg; }
        }
        return -1;
    }
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
    [[nodiscard]] local * find_local_in_current_scope(std::string_view name) {
        frame & f = frames_.back();
        const std::size_t from = f.scope_marks.empty() ? 0 : f.scope_marks.back();
        const auto it = f.local_index.find(name);
        if (it == f.local_index.end()) { return nullptr; }
        const std::uint32_t at = it->second.back();
        return at >= from ? &f.locals[at] : nullptr;
    }

    [[nodiscard]] local * find_local_entry(frame & f, std::string_view name) {
        const auto it = f.local_index.find(name);
        return it == f.local_index.end() ? nullptr : &f.locals[it->second.back()];
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
        // Was a linear scan of upvalue_names, and it ran once per level per
        // identifier mention - 8.65% of a Babylon compile between this and its
        // recursive self, most of it inside memcmp.
        if (const auto it = f.upvalue_index.find(name); it != f.upvalue_index.end()) {
            return static_cast<int>(it->second);
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
        return add_upvalue(level, name, upvalue_desc{false, static_cast<std::uint16_t>(inherited)});
    }

    [[nodiscard]] int add_upvalue(std::size_t level, std::string_view name, upvalue_desc desc) {
        frame & f = frames_[level];
        function_proto & p = out_.functions[f.proto];
        p.upvalues.push_back(desc);
        f.upvalue_names.emplace_back(name);
        // Kept in step here rather than anywhere else: this is the only place
        // upvalue_names grows, and it never shrinks within a frame.
        f.upvalue_index.emplace(std::string{name},
                                static_cast<std::uint32_t>(p.upvalues.size() - 1));
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
        // a = the default expression, b = a destructuring pattern; d = the rest
        // flag
        case vp::nk::param: return {n.a, n.b, -1, -1};
        // a = the body; c = async/generator bits
        case vp::nk::func_decl:
        case vp::nk::func_expr:
        case vp::nk::arrow: return {n.a, -1, -1, -1};
        // a = a computed key, b = the value or method; c and d are both flags
        case vp::nk::class_member:
        case vp::nk::prop:
        case vp::nk::pattern_prop: return {n.a, n.b, -1, -1};
        // a = the callee, the arguments are the list; d says whether there were
        // parentheses at all
        case vp::nk::new_expr: return {n.a, -1, -1, -1};
        // a = the target, b = the iterable, c = the body; d carries `const` and
        // whether there is anything to declare
        case vp::nk::forof_stmt: return {n.a, n.b, n.c, -1};
        // a = the test, the statements are the list; d marks `default:`
        case vp::nk::case_clause: return {n.a, -1, -1, -1};
        // ES MODULES. `c` IS A FLAG ON ALL OF THESE - which binding form an
        // import_spec is, whether an export is `default` or `*` - and the
        // default branch below would follow it as a NODE INDEX. It did: walking
        // `export default 42` segfaulted, because `c = 1` sent the tour into
        // node 1 and off from there.
        //
        // The bindings live in `list`, which every walk handles separately, and
        // `a` is the only real child: the declaration an export wraps, the
        // specifier a dynamic import takes, or the str node holding the
        // original name of a renamed binding.
        case vp::nk::import_decl:
        case vp::nk::import_meta: return {-1, -1, -1, -1};
        case vp::nk::import_spec:
        case vp::nk::export_decl:
        case vp::nk::export_spec:
        case vp::nk::dynamic_import: return {n.a, -1, -1, -1};
        default: return {n.a, n.b, n.c, n.d};
        }
    }

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

    [[nodiscard]] static bool is_function_node(const vp::node & n) {
        return n.kind == vp::nk::func_decl || n.kind == vp::nk::func_expr ||
               n.kind == vp::nk::arrow;
    }

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
    void build_capture_index() {
        fn_range_.assign(ast_.nodes.size(), interval{});
        std::int32_t tick = 0;
        tour(ast_.root, -1, tick, true);
        for (auto & [name, ticks] : mentions_) {
            std::ranges::sort(ticks);
            ticks.erase(std::unique(ticks.begin(), ticks.end()), ticks.end());
        }
    }

    void tour(std::int32_t idx, std::int32_t enclosing, std::int32_t & tick, bool boundary) {
        if (idx < 0 || static_cast<std::size_t>(idx) >= ast_.nodes.size()) { return; }
        const vp::node & n = ast_.nodes[static_cast<std::size_t>(idx)];
        const bool opens = boundary || is_function_node(n);
        const std::int32_t inner = opens ? tick++ : enclosing;
        // RECORDED AGAINST `inner`, INCLUDING WHEN THIS NODE OPENED IT. A field
        // initialiser's boundary IS the node itself - `class A { val = v; }`
        // makes `v` the whole initialiser - so skipping the opener loses the
        // only mention there is, and the enclosing local never gets boxed.
        // vm_basics caught exactly that: `function build(v) { class A { val =
        // v; } ... }` read undefined. A real function node is never an ident,
        // so this costs it nothing.
        if (n.kind == vp::nk::ident) { mentions_[std::string{n.text}].push_back(inner); }
        // A template's substitutions are text on the node, not children, so
        // nothing below this reaches them.
        if (n.kind == vp::nk::tmpl) {
            each_name_in_template(n.text, [&](std::string_view name) {
                mentions_[std::string{name}].push_back(inner);
            });
        }
        // An instance field's initialiser compiles into its OWN function - that
        // is what makes each instance get its own value - so anything it
        // mentions is captured exactly as if it had been written inside one.
        // Without this the initialiser reads an unboxed enclosing local and
        // finds undefined. Slot 1 is `b`, which for a field is the initialiser.
        const bool field_init = n.kind == vp::nk::class_member && n.c == 0 && (n.d & 1) == 0;
        const std::array<std::int32_t, 4> slots = child_slots(n);
        for (std::size_t i = 0; i < slots.size(); ++i) {
            tour(slots[i], inner, tick, field_init && i == 1);
        }
        for (const std::int32_t k : kids(n)) { tour(k, inner, tick, false); }
        if (opens) { fn_range_[static_cast<std::size_t>(idx)] = interval{inner, tick}; }
    }

    [[nodiscard]] interval range_of(std::int32_t idx) const {
        if (idx < 0 || static_cast<std::size_t>(idx) >= fn_range_.size()) { return {}; }
        return fn_range_[static_cast<std::size_t>(idx)];
    }

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
    [[nodiscard]] bool mentions_arguments(std::int32_t idx) const {
        if (idx < 0) { return false; }
        const vp::node & n = at(idx);
        if (n.kind == vp::nk::func_decl || n.kind == vp::nk::func_expr) { return false; }
        if (n.kind == vp::nk::ident && n.text == "arguments") { return true; }
        if (n.kind == vp::nk::tmpl) {
            bool found = false;
            each_name_in_template(
                n.text, [&](std::string_view name) { found = found || name == "arguments"; });
            if (found) { return true; }
        }
        for (const std::int32_t slot : child_slots(n)) {
            if (mentions_arguments(slot)) { return true; }
        }
        for (const std::int32_t k : kids(n)) {
            if (mentions_arguments(k)) { return true; }
        }
        return false;
    }
    [[nodiscard]] bool is_captured(std::string_view name) const {
        const interval where = frames_.back().captures;
        if (where.empty()) { return false; }
        const auto found = mentions_.find(name);
        if (found == mentions_.end()) { return false; }
        // The first tick strictly greater than `lo`; captured if it is also
        // inside. Strictly, because `lo` is this function's own tick and the
        // names it mentions itself are not captured by it.
        const auto & ticks = found->second;
        const auto at = std::upper_bound(ticks.begin(), ticks.end(), where.lo);
        return at != ticks.end() && *at < where.hi;
    }

    // The lexer hands back the RAW lexeme, quotes and all - `'a'` arrives as
    // three characters. Without this, every string literal in the program is
    // wrong by two characters, which shows up as 'a' + 'b' === "'a''b'" and
    // as o['a'] failing to find the property named a.
    // Read up to `count` hex digits after position `at`, leaving `at` on the
    // last one consumed so the caller's ++i lands past it. Lenient: a truncated
    // escape yields what digits there were, matching the parser's leniency
    // contract rather than throwing during compilation.
    [[nodiscard]] static std::uint32_t read_hex(std::string_view s, std::size_t & at,
                                                std::size_t count) {
        std::uint32_t value = 0;
        for (std::size_t n = 0; n < count && at + 1 < s.size(); ++n) {
            const int digit = hex_value(s[at + 1]);
            if (digit < 0) { break; }
            value = value * 16 + static_cast<std::uint32_t>(digit);
            ++at;
        }
        return value;
    }

    [[nodiscard]] static std::string encode_code_point(std::uint32_t code) {
        std::string out;
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
        return out;
    }

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
            // \xHH, \uHHHH, \u{H..} - the escapes that carry a code point.
            // Missing them was not a missing feature but a WRONG ANSWER:
            // '\x41' came out as the three characters x41, so btoa('\x00')
            // encoded the letter x. Strings here are bytes, so a code point
            // becomes its UTF-8 - the same choice String.fromCharCode makes,
            // which is what keeps a round trip through String.prototype honest.
            case 'x': out += encode_code_point(read_hex(lexeme, i, 2)); break;
            case 'u': {
                std::uint32_t code = 0;
                if (i + 1 < lexeme.size() && lexeme[i + 1] == '{') {
                    i += 2; // past the u and the {
                    for (; i < lexeme.size() && lexeme[i] != '}'; ++i) {
                        code = code * 16 + static_cast<std::uint32_t>(hex_value(lexeme[i]));
                    }
                } else {
                    code = read_hex(lexeme, i, 4);
                    // A high surrogate followed by a low one is ONE code point.
                    // Encoding the halves separately would give WTF-8, and an
                    // emoji written as an escape would not equal the same emoji
                    // written literally in the source.
                    if (code >= 0xD800 && code <= 0xDBFF && i + 6 < lexeme.size() &&
                        lexeme[i + 1] == '\\' && lexeme[i + 2] == 'u') {
                        std::size_t peek = i + 2;
                        const std::uint32_t low = read_hex(lexeme, peek, 4);
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                            i = peek;
                        }
                    }
                }
                out += encode_code_point(code);
                break;
            }
            // A backslash before a real newline is a line continuation and
            // contributes nothing - not even the newline.
            case '\n': break;
            case '\r':
                if (i + 1 < lexeme.size() && lexeme[i + 1] == '\n') { ++i; }
                break;
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
        // A PROGRAM'S top level counts too, not only a function's block. It
        // never used to: a classic script's top-level declarations are globals,
        // so there was nothing to pre-declare. A MODULE's are locals, and a
        // function declared there has to be a binding before `export { f }` can
        // find it - which it could not, and the importer was told the module
        // had no such export.
        if (body < 0 || (at(body).kind != vp::nk::block && at(body).kind != vp::nk::program)) {
            return;
        }
        const auto hoist = [this](std::string name) {
            if (name.empty() || find_local_entry(fn(), name) != nullptr) { return; }
            const std::uint16_t r = declare_local(name);
            proto().emit(instruction{op::load_undef, r});
            if (fn().locals.back().boxed) { proto().emit(instruction{op::new_cell, r}); }
            fn().predeclared.push_back(std::move(name));
        };
        for (const std::int32_t outer : kids(at(body))) {
            // `export const x = 1` and `export function f() {}` DECLARE, and
            // the declaration is wrapped. Looking only at the wrapper hoists
            // nothing, so `export function f` was never a binding here - and
            // then `export { f }` could not find it to publish, which read as
            // "has no export named f" on the importing side.
            // `export default <expr>` carries the EXPRESSION in `a` and says
            // so with `c == 1`, so unwrapping it hands the loop an expression
            // where it expects a statement. It binds one name of its own.
            if (at(outer).kind == vp::nk::export_decl && at(outer).c == 1) {
                hoist(std::string{default_binding});
                continue;
            }
            const std::int32_t stmt =
                at(outer).kind == vp::nk::export_decl && at(outer).a >= 0 ? at(outer).a : outer;
            if (at(stmt).kind == vp::nk::var_decl) {
                for (const std::int32_t d : kids(at(stmt))) {
                    if (at(d).b >= 0) { // a shape: hoist every name inside it
                        std::vector<std::string> names;
                        pattern_names(at(d).b, names);
                        for (std::string & name : names) { hoist(std::move(name)); }
                    } else {
                        hoist(std::string{at(d).text});
                    }
                }
            } else if (at(stmt).kind == vp::nk::import_decl) {
                // AN IMPORT IS A DECLARATION, so its names are hoisted like any
                // other. Allocating them inside the statement instead put them
                // ABOVE the statement's register mark, and `release_to(mark)`
                // handed them straight back - so `import { a }` survived by
                // luck and `import { a, b }` did not: the next statement's
                // temporary landed on top of `b`. The same trap
                // declare_pattern_names records for destructuring.
                for (const std::int32_t spec : kids(at(stmt))) {
                    hoist(std::string{at(spec).text});
                }
            } else if (at(stmt).kind == vp::nk::func_decl) {
                // A nested function declaration is a BINDING IN ITS SCOPE, and
                // it has to exist before its own body compiles or a recursive
                // call inside it resolves to a global instead of to itself.
                hoist(std::string{at(stmt).text});
            } else if (at(stmt).kind == vp::nk::class_decl) {
                // A CLASS DECLARATION IS A BINDING TOO, and it has to be
                // hoisted for a reason worth stating: a local first declared
                // while an EXPRESSION is being compiled sits above the
                // statement's register mark, so the statement releases it and
                // the next statement's temporaries reuse the slot. `class S {}`
                // followed by two `new S()` therefore worked once and then
                // found an object in the register the second time.
                hoist(std::string{at(stmt).text});
            }
        }
        // And every `var` in a NESTED block, which the loop above cannot see -
        // it walks this body's own statements only. `hoist` returns early on a
        // name already declared, so the statements just handled cost a lookup.
        for (const std::int32_t stmt : kids(at(body))) { hoist_nested_vars(stmt, hoist); }
    }
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
    void compile_parameter_prologue(std::span<const std::int32_t> params) {
        for (std::size_t i = 0; i < params.size(); ++i) {
            const vp::node & p = at(params[i]);
            if (p.d == 1) {
                proto().emit(instruction{op::gather_rest, static_cast<std::uint16_t>(i),
                                         static_cast<std::uint16_t>(i)});
            }
        }
        for (std::size_t i = 0; i < params.size(); ++i) {
            const vp::node & p = at(params[i]);
            if (p.d == 1 || p.a < 0) { continue; }
            const auto slot = static_cast<std::uint16_t>(i);
            const std::size_t skip = proto().emit(instruction{op::jump_if_defined, slot});
            const std::uint32_t mark = reg_mark();
            compile_expr(p.a, slot);
            release_to(mark);
            patch_here(skip);
        }
        // A parameter may be a SHAPE - `function f({x, y})`. Its register holds
        // the argument; the names inside come out of it. Last, so a default has
        // already been applied to the value being destructured.
        for (std::size_t i = 0; i < params.size(); ++i) {
            const vp::node & p = at(params[i]);
            if (p.b >= 0) { compile_pattern_binding(p.b, static_cast<std::uint16_t>(i), true); }
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
        // NUMERIC SEPARATORS COME OFF FIRST. `1_000_000` is one token from the
        // lexer, and `std::from_chars` accepts no underscore in either overload
        // - it would stop at the first one and read 1. Removing them here keeps
        // that knowledge in one place rather than in both parse paths below.
        std::string lex{text};
        std::erase(lex, '_');
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
        // A LITERAL TOO BIG FOR A DOUBLE IS Infinity, NOT ZERO. from_chars
        // reports result_out_of_range and leaves `d` UNTOUCHED, so ignoring the
        // error made `1e400` compile to 0 - silently, and at the opposite end of
        // the number line from the truth. Runtime overflow was always right
        // (`1e308 * 10` is Infinity); only the lexer disagreed.
        const auto failed = std::from_chars(lex.data(), lex.data() + lex.size(), d).ec;
        if (failed == std::errc::result_out_of_range) { return out_of_range_value(lex); }
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
    static constexpr std::size_t operand_limit = 65535;    // a uint16 field
    static constexpr std::int32_t jump_limit = 2147483647; // the signed bx half

    [[nodiscard]] std::string frame_name(std::size_t index) const {
        const std::string & name = out_.functions[index].name;
        return "`" + (name.empty() ? std::string{"<anonymous>"} : name) + "`";
    }

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
                                                   std::string text) {
        if (pool.size() < small_pool) {
            for (std::size_t i = 0; i < pool.size(); ++i) {
                if (pool[i] == text) { return static_cast<std::uint32_t>(i); }
            }
            pool.push_back(std::move(text));
            return static_cast<std::uint32_t>(pool.size() - 1);
        }
        if (index.empty()) { // crossing the threshold: catch the index up
            for (std::size_t i = 0; i < pool.size(); ++i) {
                index.emplace(pool[i], static_cast<std::uint32_t>(i));
            }
        }
        if (const auto it = index.find(text); it != index.end()) { return it->second; }
        const auto at = static_cast<std::uint32_t>(pool.size());
        pool.push_back(text);
        index.emplace(std::move(text), at);
        return at;
    }

    // The same answers add_name and add_string give, without the quadratic
    // scan. The NUMBERING IS IDENTICAL either way - an unseen entry is appended
    // and takes the next index - so the bytecode is byte-for-byte the same.
    [[nodiscard]] std::uint32_t intern_name(std::string text) {
        return intern_into(proto().names, fn().name_index, std::move(text));
    }
    [[nodiscard]] std::uint32_t intern_string(std::string text) {
        return intern_into(proto().strings, fn().string_index, std::move(text));
    }

    [[nodiscard]] std::uint16_t name_operand(std::string text) {
        const std::uint32_t index = intern_name(std::move(text));
        if (index > operand_limit) {
            fail(frame_name(fn().proto) + " mentions more than " +
                 std::to_string(operand_limit + 1) +
                 " distinct property names; the operand that selects one holds " +
                 std::to_string(operand_limit + 1) + ". Past that it reads a DIFFERENT property.");
        }
        return static_cast<std::uint16_t>(index);
    }

    // Called where a frame's size is finally written, because that is the only
    // point at which high_water is the truth rather than a running total.
    void finish_frame(std::size_t index, std::size_t params) {
        function_proto & fp = out_.functions[index];
        const std::uint32_t wanted = fn().high_water;
        fp.frame_size = static_cast<std::uint16_t>(wanted);
        fp.param_count = static_cast<std::uint16_t>(params);
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
        build_capture_index();
        fn().captures = range_of(ast_.root);
        collect_declared_names(ast_.root);
        // A MODULE'S TOP LEVEL IS A SCOPE, so its declarations are pre-declared
        // exactly as a function body's are. A classic script's are globals and
        // need none of this, which is why it was never called here before.
        if (module_scope_) {
            predeclare_locals(ast_.root);
            // THEN THE IMPORTS, still at entry: a function declared anywhere in
            // this module closes over them, and the closures are made below.
            for (const std::int32_t s : kids(root)) { bind_imports(s); }
            // AND THE RE-EXPORT EDGES, which are data for the loader rather
            // than code - collected here because this is where the top level is
            // walked, not because anything is emitted.
            for (const std::int32_t s : kids(root)) { collect_reexports(s); }
            // AND THE EXPORTS, likewise before a single statement of the body
            // runs. See bind_export.
            std::vector<std::pair<std::string, std::string>> bindings;
            for (const std::int32_t s : kids(root)) { export_bindings(s, bindings); }
            for (const auto & [local_name, exported] : bindings) {
                const int r = find_local(local_name);
                if (r < 0) {
                    fail("`export { " + local_name +
                         " }` names something this module does not "
                         "declare");
                    continue;
                }
                bind_export(exported, static_cast<std::uint16_t>(r));
            }
        }
        // Function declarations hoist: a script may call one before its text.
        // THROUGH AN `export` WRAPPER TOO - `export function f() {}` is a
        // function declaration that hoists like any other, and looking only at
        // the wrapper left it compiled in the second pass, after anything that
        // called it.
        const auto declared_by = [this](std::int32_t s) {
            return at(s).kind == vp::nk::export_decl && at(s).c != 1 && at(s).a >= 0 ? at(s).a : s;
        };
        for (const std::int32_t s : kids(root)) {
            if (at(declared_by(s)).kind == vp::nk::func_decl) { compile_stmt(s); }
        }
        for (const std::int32_t s : kids(root)) {
            if (at(declared_by(s)).kind != vp::nk::func_decl) { compile_stmt(s); }
        }
        proto().emit(instruction{op::ret_undef});
        finish_frame(fn().proto, 0);
        pop_scope();
        frames_.pop_back();
    }

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
    void pattern_names(std::int32_t pat, std::vector<std::string> & out) const {
        if (pat < 0) { return; }
        const vp::node & n = at(pat);
        switch (n.kind) {
        case vp::nk::ident: out.emplace_back(n.text); return;
        case vp::nk::assign_pattern: pattern_names(n.a, out); return;
        case vp::nk::rest_element: pattern_names(n.a, out); return;
        case vp::nk::pattern_prop: pattern_names(n.b, out); return;
        case vp::nk::array_pattern:
        case vp::nk::object_pattern:
            for (const std::int32_t k : kids(n)) { pattern_names(k, out); }
            return;
        default: return;
        }
    }

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
    void declare_pattern_names(std::int32_t pat) {
        if (frames_.size() <= 1) { return; } // a declaration there is a global
        std::vector<std::string> names;
        pattern_names(pat, names);
        for (std::string & name : names) {
            // ONLY the current scope decides this. was_predeclared is
            // function-scoped - it is how `var` and function declarations
            // hoist - and asking it here made a `const {x}` inside a block
            // reuse a slot hoisted at the top of the function, writing
            // through to it instead of shadowing it. A hoisted name IS a
            // local of the function's top scope, so when we are in that
            // scope this finds it anyway.
            if (find_local_in_current_scope(name) != nullptr) { continue; }
            const std::uint16_t reg = declare_local(name);
            proto().emit(instruction{op::load_undef, reg});
            if (fn().locals.back().boxed) { proto().emit(instruction{op::new_cell, reg}); }
        }
    }

    void compile_pattern_binding(std::int32_t pat, std::uint16_t src, bool declaring) {
        if (declaring) { declare_pattern_names(pat); }
        compile_pattern(pat, src);
    }

    // Bind `pattern` to the value sitting in `src`. Every name it mentions
    // already exists by the time this runs - see compile_pattern_binding.
    void compile_pattern(std::int32_t pat, std::uint16_t src) {
        if (pat < 0 || !out_.ok) { return; }
        const vp::node & n = at(pat);
        switch (n.kind) {
        case vp::nk::ident: emit_write(n.text, src); return;

        case vp::nk::member:
        case vp::nk::index: {
            // `[o.a, o.b] = pair` - a target that is not a name at all.
            const reference ref = prepare_reference(n);
            emit_store(ref, src);
            return;
        }

        case vp::nk::assign_pattern: {
            // The default applies when the value is UNDEFINED, so it is written
            // into the source register before the target ever sees it.
            const std::size_t skip = proto().emit(instruction{op::jump_if_defined, src});
            const std::uint32_t mark = reg_mark();
            compile_expr(n.b, src);
            release_to(mark);
            patch_here(skip);
            compile_pattern(n.a, src);
            return;
        }

        case vp::nk::array_pattern: {
            const std::span<const std::int32_t> elements = kids(n);
            for (std::size_t i = 0; i < elements.size(); ++i) {
                if (elements[i] < 0) { continue; } // a hole binds nothing
                const std::uint32_t mark = reg_mark();
                const std::uint16_t item = alloc_reg();
                if (at(elements[i]).kind == vp::nk::rest_element) {
                    // everything from here on, as a new array
                    emit_slice_from(item, src, i);
                    compile_pattern(at(elements[i]).a, item);
                } else {
                    const std::uint16_t index = alloc_reg();
                    emit_const(index, value::number(static_cast<double>(i)));
                    proto().emit(instruction{op::get_index, item, src, index});
                    compile_pattern(elements[i], item);
                }
                release_to(mark);
            }
            return;
        }

        case vp::nk::object_pattern: {
            // The keys already taken, so an object rest knows what to leave out.
            std::vector<std::string> taken;
            for (const std::int32_t entry : kids(n)) {
                const vp::node & e = at(entry);
                const std::uint32_t mark = reg_mark();
                const std::uint16_t item = alloc_reg();
                if (e.kind == vp::nk::rest_element) {
                    emit_rest_object(item, src, taken);
                    compile_pattern(e.a, item);
                } else if ((e.d & 2) != 0 && e.a >= 0) { // a computed key
                    const std::uint16_t key = alloc_reg();
                    compile_expr(e.a, key);
                    proto().emit(instruction{op::get_index, item, src, key});
                    compile_pattern(e.b, item);
                } else {
                    proto().emit(
                        instruction{op::get_prop, item, src, name_operand(std::string{e.text})});
                    taken.emplace_back(e.text);
                    compile_pattern(e.b, item);
                }
                release_to(mark);
            }
            return;
        }

        default: fail("unsupported destructuring target: " + kind_name(n.kind)); return;
        }
    }

    // Bind an array or object LITERAL, read in expression position, as if it
    // had been parsed as a pattern. Assigning, never declaring - every name in
    // it already exists.
    void compile_literal_as_pattern(std::int32_t literal, std::uint16_t src) {
        const vp::node & n = at(literal);
        if (n.kind == vp::nk::array) {
            const std::span<const std::int32_t> elements = kids(n);
            for (std::size_t i = 0; i < elements.size(); ++i) {
                if (elements[i] < 0) { continue; } // `[, x] = pair` skips one
                const std::uint32_t mark = reg_mark();
                const std::uint16_t item = alloc_reg();
                if (at(elements[i]).kind == vp::nk::spread) {
                    emit_slice_from(item, src, i);
                    compile_literal_target(at(elements[i]).a, item);
                } else {
                    const std::uint16_t index = alloc_reg();
                    emit_const(index, value::number(static_cast<double>(i)));
                    proto().emit(instruction{op::get_index, item, src, index});
                    compile_literal_target(elements[i], item);
                }
                release_to(mark);
            }
            return;
        }
        std::vector<std::string> taken;
        for (const std::int32_t entry : kids(n)) {
            const vp::node & e = at(entry);
            const std::uint32_t mark = reg_mark();
            const std::uint16_t item = alloc_reg();
            if (e.kind == vp::nk::spread) {
                emit_rest_object(item, src, taken);
                compile_literal_target(e.a, item);
            } else {
                // `{a}` is shorthand (c == 2) and binds its own name; `{a: b}`
                // binds `b`.
                const std::int32_t value_node = e.c == 2 ? -1 : e.b;
                proto().emit(
                    instruction{op::get_prop, item, src, name_operand(std::string{e.text})});
                taken.emplace_back(e.text);
                if (value_node < 0) {
                    emit_write(e.text, item);
                } else {
                    compile_literal_target(value_node, item);
                }
            }
            release_to(mark);
        }
    }

    // One target inside a literal-as-pattern: a name, a member, or a nested
    // literal that is itself a pattern.
    void compile_literal_target(std::int32_t target, std::uint16_t src) {
        if (target < 0) { return; }
        const vp::node & t = at(target);
        if (t.kind == vp::nk::array || t.kind == vp::nk::object) {
            compile_literal_as_pattern(target, src);
            return;
        }
        if (t.kind == vp::nk::assign) { // `[a = 1] = xs`
            const std::size_t skip = proto().emit(instruction{op::jump_if_defined, src});
            const std::uint32_t mark = reg_mark();
            compile_expr(t.b, src);
            release_to(mark);
            patch_here(skip);
            compile_literal_target(t.a, src);
            return;
        }
        if (t.kind == vp::nk::ident) {
            emit_write(t.text, src);
            return;
        }
        const reference ref = prepare_reference(t);
        emit_store(ref, src);
    }

    // `[a, ...rest] = xs` - rest is everything from `from` onward.
    void emit_slice_from(std::uint16_t dst, std::uint16_t source, std::size_t from) {
        proto().emit(instruction{op::new_array, dst});
        const std::uint32_t mark = reg_mark();
        const std::uint16_t length = alloc_reg();
        proto().emit(instruction{op::get_prop, length, source, name_operand("length")});
        const std::uint16_t index = alloc_reg();
        emit_const(index, value::number(static_cast<double>(from)));
        const std::uint16_t one = alloc_reg();
        emit_const(one, value::number(1));
        const std::uint16_t test = alloc_reg();
        const std::uint16_t item = alloc_reg();
        const std::size_t top = proto().code.size();
        proto().emit(instruction{op::less, test, index, length});
        const std::size_t exit = proto().emit(instruction{op::jump_if_false, test});
        proto().emit(instruction{op::get_index, item, source, index});
        proto().emit(instruction{op::append, dst, item});
        proto().emit(instruction{op::add, index, index, one});
        patch_jump(proto().emit(instruction{op::jump}), top);
        patch_here(exit);
        release_to(mark);
    }

    // `{a, ...rest} = o` - every own property except the ones already named.
    void emit_rest_object(std::uint16_t dst, std::uint16_t source,
                          const std::vector<std::string> & taken) {
        proto().emit(instruction{op::new_object, dst});
        proto().emit(instruction{op::copy_props, dst, source});
        for (const std::string & key : taken) {
            proto().emit(instruction{op::delete_prop, dst, name_operand(key)});
        }
    }

    // --- statements ---------------------------------------------------------
    void compile_stmt(std::int32_t idx) {
        if (idx < 0 || !out_.ok) { return; }
        const vp::node & n = at(idx);
        const std::uint32_t mark = reg_mark();
        switch (n.kind) {
        // REFUSED BY NAME, for the same reason the expression forms are: a
        // page whose `import` silently did nothing would run with half its
        // bindings undefined and fail somewhere else entirely. The syntax
        // parses (ctjs 2026-08-02); the semantics are staged in
        // docs/plans/modules.md.
        case vp::nk::import_decl: {
            if (!module_scope_) {
                fail("`import` is only allowed in a module - a classic <script> cannot use it");
                break;
            }
            // AND NOTHING ELSE HAPPENS HERE. The binding was made at module
            // entry - see bind_imports - because a function declared above the
            // `import` still closes over what it imports.
            break;
        }
        case vp::nk::export_decl: {
            if (!module_scope_) {
                fail("`export` is only allowed in a module - a classic <script> cannot use it");
                break;
            }
            // A RE-EXPORT BINDS NOTHING HERE. It was recorded at module entry
            // as an edge for the loader to resolve - see collect_reexports -
            // and there is no local name for it to compile into.
            if (!n.text.empty()) { break; }
            if (n.c == 1) {
                // `export default <expr>`: the binding was hoisted and bound at
                // entry under `*default*`, so this only WRITES it. Making a
                // fresh cell here instead would leave the importer holding the
                // one from entry - which is exactly the bug a cycle exposes.
                const int slot = find_local(default_binding);
                if (slot < 0) {
                    fail("`export default` was not hoisted - see predeclare_locals");
                    break;
                }
                const std::uint32_t mark = reg_mark();
                const std::uint16_t r = alloc_reg();
                compile_expr(n.a, r);
                proto().emit(instruction{op::cell_set, static_cast<std::uint16_t>(slot), r});
                release_to(mark);
                break;
            }
            if (n.a >= 0) {
                // `export const x = 1`, `export function f() {}`. The
                // declaration compiles as itself, then the names it binds are
                // published.
                //
                // BY NAME, NOT BY DIFFING fn().locals. A module's top-level
                // declarations are PRE-DECLARED at entry - that is how a
                // function declared above its own `let` still closes over it -
                // so the locals list does not grow here and a diff publishes
                // nothing at all. It read `undefined` on the other side and
                // said nothing, which is the failure this whole ladder exists
                // to make loud.
                if (at(n.a).kind == vp::nk::var_decl) {
                    for (const std::int32_t d : kids(at(n.a))) {
                        if (at(d).text.empty()) {
                            fail("`export` of a destructuring declaration is not implemented yet - "
                                 "ES modules are staged in docs/plans/modules.md");
                            break;
                        }
                    }
                }
                // AND THAT IS ALL: the declaration compiles as itself. Its
                // names were bound to their export cells at module entry, so
                // there is nothing left to publish here - see bind_export.
                compile_stmt(n.a);
                break;
            }
            // `export { a, b as c }` emits NOTHING. The binding pass at entry
            // already tied each name to its cell, and it is the pass that
            // reports a name this module does not declare.
            break;
        }
        case vp::nk::expr_stmt: {
            const std::uint16_t r = alloc_reg();
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
            // A MODULE'S top-level declarations are its OWN, so they take the
            // local path below exactly as a function body's would. A classic
            // script's become globals, which is what lets two <script> tags see
            // each other.
            if (frames_.size() == 1 && !module_scope_) {
                for (const std::int32_t d : kids(n)) {
                    const vp::node & decl = at(d);
                    const std::uint32_t mark = reg_mark();
                    const std::uint16_t r = alloc_reg();
                    if (decl.a >= 0) {
                        compile_expr(decl.a, r);
                    } else {
                        proto().emit(instruction{op::load_undef, r});
                    }
                    if (decl.b >= 0) { // a shape, not a name
                        compile_pattern_binding(decl.b, r, true);
                    } else {
                        const std::uint16_t name = name_operand(std::string{decl.text});
                        proto().emit(instruction::with_bx(op::set_global, r, name));
                    }
                    release_to(mark);
                }
                return;
            }
            for (const std::int32_t d : kids(n)) {
                const vp::node & decl = at(d);
                if (decl.b >= 0) { // a shape, not a name
                    // THE NAMES FIRST, ABOVE THE MARK. release_to(mark) below
                    // frees the temporary holding the initializer - and would
                    // free the pattern's own locals with it if they were
                    // allocated inside, leaving the next temporary to overwrite
                    // one. See declare_pattern_names.
                    declare_pattern_names(decl.b);
                    const std::uint32_t mark = reg_mark();
                    const std::uint16_t r = alloc_reg();
                    if (decl.a >= 0) {
                        compile_expr(decl.a, r);
                    } else {
                        proto().emit(instruction{op::load_undef, r});
                    }
                    compile_pattern_binding(decl.b, r, false);
                    release_to(mark);
                    continue;
                }
                // WHICH KEYWORD IT IS DECIDES THIS, and until now nothing
                // asked. `var` is FUNCTION-scoped: it always writes the hoisted
                // binding, wherever the statement sits, because there is only
                // one of it per function. `let` and `const` are BLOCK-scoped
                // and must shadow, which is why the hoisted slot is reused for
                // them only when it is in THIS scope - asking was_predeclared
                // alone made a `const` inside a block assign through to a
                // binding at the top of the function.
                //
                // Getting this wrong the other way is what `if (c) { var x = 1; }`
                // did: the block declared its own `x`, the scope popped it, and
                // every later read found undefined.
                const bool function_scoped = n.text == "var";
                if (was_predeclared(decl.text) &&
                    (function_scoped || find_local_in_current_scope(decl.text) != nullptr)) {
                    // hoisted above: this statement is only the initializer,
                    // and emit_write knows whether it goes through a cell
                    if (decl.a >= 0) {
                        const std::uint32_t mark = reg_mark();
                        const std::uint16_t tmp = alloc_reg();
                        compile_expr(decl.a, tmp);
                        emit_write(decl.text, tmp);
                        release_to(mark);
                    }
                    continue;
                }
                const std::uint16_t r = declare_local(std::string{decl.text});
                if (decl.a >= 0) {
                    compile_expr(decl.a, r);
                } else {
                    proto().emit(instruction{op::load_undef, r});
                }
                // A captured local is boxed AFTER its initializer runs, so the
                // cell starts out holding the right value.
                if (fn().locals.back().boxed) { proto().emit(instruction{op::new_cell, r}); }
                // a declared local keeps its register beyond this statement
                if (fn().next_reg <= r) { fn().next_reg = static_cast<std::uint16_t>(r + 1); }
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
            const std::uint16_t r = alloc_reg();
            // A DECLARATION, so its name is a binding of this scope - which is
            // the whole difference from the expression form.
            compile_class(n, r, true);
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
            const std::uint16_t r = alloc_reg();
            if (n.a >= 0) {
                compile_expr(n.a, r);
            } else {
                proto().emit(instruction{op::load_undef, r});
            }
            // `async function f() { return 5 }` hands back a PROMISE of 5, not
            // 5 - so `f().then(...)` works and not only `await f()`.
            // NOT for a generator, even an async one. A generator's `return`
            // becomes the `value` of a `{value, done: true}` record; the
            // promise, if there is to be one, is the driver's job - which is
            // exactly what TypeScript's __awaiter helper does with it.
            if (fn().is_async && !fn().is_generator) {
                proto().emit(instruction{op::wrap_promise, r});
            }
            proto().emit(instruction{op::ret, r});
            break;
        }
        case vp::nk::func_decl: compile_function_decl(idx); return;
        case vp::nk::empty: break;
        default: {
            // anything not yet handled is still an expression in most cases
            const std::uint16_t r = alloc_reg();
            compile_expr(idx, r);
            break;
        }
        }
        release_to(mark);
    }

    void compile_if(const vp::node & n) {
        const std::uint32_t mark = reg_mark();
        const std::uint16_t cond = alloc_reg();
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
        // A label on a LOOP or a switch is picked up by that statement, which
        // owns the break and continue targets. A label on anything else - most
        // often a bare block - has no loop to hand it to, and `break lbl` out
        // of one is legal JavaScript that used to be refused outright.
        //
        // The mechanism is already there: a loop_context is a label plus a list
        // of jumps to patch. This pushes one with nothing to continue TO, so
        // `continue lbl` still fails (correctly - there is no iteration), and
        // `break lbl` lands after the block.
        const vp::nk labelled = n.a >= 0 ? at(n.a).kind : vp::nk::empty;
        const bool owns_its_label = labelled == vp::nk::while_stmt || labelled == vp::nk::do_stmt ||
                                    labelled == vp::nk::for_stmt ||
                                    labelled == vp::nk::forof_stmt ||
                                    labelled == vp::nk::switch_stmt || labelled == vp::nk::labeled;
        if (owns_its_label) {
            pending_label_ = std::string{n.text};
            compile_stmt(n.a);
            pending_label_.clear();
            return;
        }
        loops_.push_back(loop_context{std::string{n.text}, {}, {}, handler_depth_});
        compile_stmt(n.a);
        patch_breaks(loops_.back());
        if (!loops_.back().continues.empty()) {
            fail("`continue " + std::string{n.text} + "` names a block, not a loop");
        }
        loops_.pop_back();
    }

    void compile_while(const vp::node & n) {
        const std::size_t top = proto().code.size();
        loops_.push_back(loop_context{take_label(), {}, {}, handler_depth_});
        const std::uint32_t mark = reg_mark();
        const std::uint16_t cond = alloc_reg();
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
        const std::uint16_t cond = alloc_reg();
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
        const std::size_t init_mark = fn().scope_marks.back();
        if (n.a >= 0) { compile_stmt(n.a); }
        // THE PER-ITERATION BINDINGS. `for (let i = 0; ...)` gives every
        // iteration its OWN `i`, so closures made in the body capture 0, 1, 2 -
        // where `for (var i = ...)` shares one binding and they all capture 3.
        // This engine had only the `var` behaviour, silently, and modern
        // minified output leans on the difference constantly.
        //
        // Only a BOXED local can tell: an unboxed one lives in a register that
        // nothing outside the frame can reach, so copying it would be work with
        // no observer. `var` is hoisted to the function scope and so never
        // appears among the locals this scope opened - which is what keeps the
        // two loops apart without the compiler having to track declaration
        // kinds. tests/js/obfuscated.cpp pins BOTH shapes, so getting that
        // backwards fails immediately.
        std::vector<std::uint16_t> per_iteration;
        for (std::size_t k = init_mark; k < fn().locals.size(); ++k) {
            if (fn().locals[k].boxed) { per_iteration.push_back(fn().locals[k].reg); }
        }
        const std::size_t top = proto().code.size();
        loops_.push_back(loop_context{label, {}, {}, handler_depth_});
        std::size_t exit = 0;
        bool has_cond = false;
        if (n.b >= 0) {
            const std::uint32_t mark = reg_mark();
            const std::uint16_t cond = alloc_reg();
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
        // BETWEEN THE BODY AND THE UPDATE, which is where the specification puts
        // it (ForBodyEvaluation step 3.e): the fresh binding takes the value the
        // body left, and the increment then applies to the NEW one. Doing it
        // after the update instead would shift every captured value by one.
        //
        // `continue` lands on the instruction above, so it flows through here
        // too - which is correct, and is why this sits after patch_continues
        // rather than before it.
        if (!per_iteration.empty()) {
            const std::uint32_t mark = reg_mark();
            const std::uint16_t scratch = alloc_reg();
            for (const std::uint16_t r : per_iteration) {
                proto().emit(instruction{op::cell_get, scratch, r});
                proto().emit(instruction{op::move, r, scratch});
                proto().emit(instruction{op::new_cell, r});
            }
            release_to(mark);
        }
        if (n.c >= 0) {
            const std::uint32_t mark = reg_mark();
            const std::uint16_t tmp = alloc_reg();
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
    void compile_for_of(const vp::node & n) {
        push_scope();
        const std::string label = take_label();
        const std::uint32_t mark = reg_mark();

        const std::uint16_t source = alloc_reg();
        compile_expr(n.b, source);
        if (n.text == "in") {
            proto().emit(instruction{op::own_keys, source, source});
        } else {
            // `for (x of ...)` TAKES ANYTHING ITERABLE. This is an index loop over
            // `length`, so a Map or a Set - which has neither - ran zero times and
            // reported nothing.
            proto().emit(instruction{op::iterable, source, source});
        }

        const std::uint16_t length = alloc_reg();
        const std::uint16_t length_name = name_operand("length");
        proto().emit(instruction{op::get_prop, length, source, length_name});
        const std::uint16_t index = alloc_reg();
        emit_const(index, value::number(0));
        const std::uint16_t one = alloc_reg();
        emit_const(one, value::number(1));

        // The loop variable is a real local, so a closure made inside the body
        // captures THIS iteration's value - which is the whole reason `let` in
        // a loop behaves differently from `var`.
        //
        // Two shapes are not a plain local: `for (const [k, v] of pairs)` binds
        // a SHAPE, and `for (prop in obj)` with no declaration keyword assigns
        // to a binding that already exists (d bit1). Both still need a register
        // to read the element into.
        const vp::node & target = at(n.a);
        const bool declares = (n.d & 2) == 0;
        const bool is_shape = target.b >= 0;
        const std::uint16_t item =
            (declares && !is_shape) ? declare_local(std::string{target.text}) : alloc_reg();

        const std::size_t top = proto().code.size();
        loops_.push_back(loop_context{label, {}, {}, handler_depth_});
        const std::uint16_t test = alloc_reg();
        proto().emit(instruction{op::less, test, index, length});
        const std::size_t exit = proto().emit(instruction{op::jump_if_false, test});

        proto().emit(instruction{op::get_index, item, source, index});
        if (is_shape) {
            compile_pattern_binding(target.b, item, declares);
        } else if (!declares) {
            emit_write(target.text, item);
        } else if (const local * l = find_local_entry(fn(), target.text);
                   l != nullptr && l->boxed) {
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
        const std::uint16_t subject = alloc_reg();
        compile_expr(n.a, subject);

        const std::span<const std::int32_t> clauses = kids(n);
        std::vector<std::size_t> entries(clauses.size(), 0);
        std::size_t default_clause = clauses.size();

        const std::uint16_t candidate = alloc_reg();
        const std::uint16_t matched = alloc_reg();
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

        std::uint16_t caught_reg = 0;
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
        const std::uint16_t r = alloc_reg();
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
        if (!fn().is_async || fn().is_generator) {
            proto().emit(instruction{op::ret_undef});
            return;
        }
        const std::uint16_t r = alloc_reg();
        proto().emit(instruction{op::load_undef, r});
        proto().emit(instruction{op::wrap_promise, r});
        proto().emit(instruction{op::ret, r});
    }

    void compile_function_decl(std::int32_t idx) {
        const vp::node & n = at(idx);
        const std::uint32_t index = compile_function_body(idx, std::string{n.text});
        const std::uint32_t mark = reg_mark();
        const std::uint16_t r = alloc_reg();
        proto().emit(instruction::with_bx(op::closure, r, static_cast<std::uint16_t>(index)));
        // AT THE TOP LEVEL a function declaration is a global, by design: a page
        // defines functions the host calls by name, and script scope is what
        // sibling declarations close over.
        //
        // ANYWHERE ELSE it is a local, and this used to emit set_global at every
        // depth. Two helpers named `handler` in two different closures collided
        // in one table, and a nested function meant to capture an enclosing
        // local read a global instead. In a bundle where every module is an
        // IIFE - which is every bundle - that is the whole point of the IIFE
        // silently undone.
        // A MODULE'S TOP LEVEL IS NOT THE GLOBAL SCOPE, so its functions are
        // locals like any other binding. Leaving them global would leak every
        // module's helpers into one namespace - and, more visibly, make
        // `export function f() {}` unpublishable, because find_local would not
        // know the name.
        if (frames_.size() == 1 && !module_scope_) {
            proto().emit(
                instruction::with_bx(op::set_global, r, name_operand(std::string{n.text})));
        } else {
            emit_write(n.text, r);
        }
        release_to(mark);
    }

    [[nodiscard]] std::uint32_t compile_function_body(std::int32_t idx, std::string name) {
        const vp::node & n = at(idx);
        const auto index = static_cast<std::uint32_t>(out_.functions.size());
        out_.functions.emplace_back();
        out_.functions[index].name = std::move(name);
        // The VM cannot tell an arrow from a function once it is bytecode, and
        // it has to: an arrow sees the `this` where it was written.
        out_.functions[index].is_arrow = n.kind == vp::nk::arrow;
        out_.functions[index].source_begin = n.begin;
        out_.functions[index].source_end = n.end;

        frames_.emplace_back();
        frames_.back().proto = index;
        push_scope();
        // A FUNCTION BODY IS NOT PART OF THE CHAIN THAT ENCLOSES IT.
        //
        // `a?.b(() => c?.d)` compiles the arrow while the outer chain is open,
        // so without this the arrow's own short-circuit would be recorded on
        // the outer chain's exit list - and patched into the ENCLOSING proto's
        // code array, at an index that means something else entirely there.
        const bool saved_in_chain = in_chain_;
        std::vector<std::size_t> saved_exits;
        saved_exits.swap(optional_exits_);
        in_chain_ = false;
        // Which of this body's names some nested function mentions has to be
        // known BEFORE any local is declared - that is what decides whether a
        // local gets a register or a cell.
        fn().captures = range_of(idx);
        const std::span<const std::int32_t> params = kids(n);
        for (const std::int32_t p : params) { (void)declare_local(std::string{at(p).text}); }
        // WHICH LOCALS THE BOXING LOOP BELOW OWNS: the parameters, and only
        // them. The prologue declares more - every name inside a destructuring
        // pattern - and boxes those itself as it binds them. Boxing them a
        // second time here wrapped a cell in a cell, so reading the variable
        // gave the inner CELL rather than the value: a captured `{ space }`
        // parameter came out as an object with no properties, which is exactly
        // what colorjs then failed to use as a colour space.
        const std::size_t declared_parameters = fn().locals.size();
        // `arguments` IS MATERIALISED ONCE, BEFORE THE PROLOGUE, AND IS A REAL
        // LOCAL.
        //
        // Before, because the prologue REWRITES the parameter registers: a
        // destructured parameter unpacks into its own slot, a default overwrites
        // an undefined one. Building `arguments` after that read the unpacked
        // values rather than the arguments, so `function f(a, {b} = {})` had
        // `arguments[1]` holding whatever the pattern left there - which is how
        // colorjs's `isString(arguments[1])` was handed an options object it
        // then tried to use as a colour space.
        //
        // After the parameters are DECLARED, though: the calling convention puts
        // argument i in register i, so taking a register ahead of them would
        // shift every one.
        //
        // Building it where the name is MENTIONED is wrong for the same reason
        // one step further on - by then the surrounding expression has reused
        // the registers holding arguments past the last declared parameter.
        //
        // A local also gives it the right identity - one object per call, not a
        // fresh array per mention - and lets an arrow capture the enclosing
        // function's, which is the arrow rule, for free. Arrows do not make
        // their own: `mentions_arguments` descends into them so the enclosing
        // function makes one they can capture.
        std::uint16_t arguments_slot = 0;
        bool arguments_boxed = false;
        const bool wants_arguments = !out_.functions[index].is_arrow && mentions_arguments(n.a) &&
                                     find_local_in_current_scope("arguments") == nullptr;
        if (wants_arguments) {
            arguments_slot = declare_local("arguments");
            proto().emit(instruction{op::make_arguments, arguments_slot});
            arguments_boxed = fn().locals.back().boxed;
        }
        compile_parameter_prologue(params);
        // A captured PARAMETER needs boxing too, and it arrives already
        // holding its value - so box in place, after the arguments land.
        for (std::size_t i = 0; i < declared_parameters && i < fn().locals.size(); ++i) {
            const local & l = fn().locals[i];
            // `arguments` was built above and boxes itself; a pattern name is
            // declared and boxed by the prologue. Neither is a parameter, and
            // neither belongs here.
            if (l.boxed && !(wants_arguments && l.reg == arguments_slot)) {
                proto().emit(instruction{op::new_cell, l.reg});
            }
        }
        if (wants_arguments && arguments_boxed) {
            proto().emit(instruction{op::new_cell, arguments_slot});
        }
        // A NAMED FUNCTION EXPRESSION BINDS ITS OWN NAME, in its own body and
        // nowhere else. `var f = function me(n) { return me(n - 1); }` is how
        // an unnamed function recurses, and `(function pump() { raf(pump); })()`
        // is how one drives an animation loop - both called an undefined name
        // without this, silently in the second case because a callback that is
        // undefined simply never runs.
        //
        // AFTER the parameters, because the calling convention puts argument i
        // in register i: taking a register ahead of them would shift every
        // argument by one. A parameter of the same name legitimately shadows
        // this binding, so one is only made when no parameter claimed the name.
        if (n.kind == vp::nk::func_expr && !out_.functions[index].name.empty() &&
            find_local_in_current_scope(out_.functions[index].name) == nullptr) {
            const std::uint16_t self = declare_local(out_.functions[index].name);
            proto().emit(instruction{op::load_callee, self});
            if (fn().locals.back().boxed) { proto().emit(instruction{op::new_cell, self}); }
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
        fn().is_generator = n.c > 0 && (n.c & 2) != 0;
        proto().is_generator = fn().is_generator;

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
            const std::uint16_t r = alloc_reg();
            compile_expr(body, r);
            // NOT for a generator, even an async one. A generator's `return`
            // becomes the `value` of a `{value, done: true}` record; the
            // promise, if there is to be one, is the driver's job - which is
            // exactly what TypeScript's __awaiter helper does with it.
            if (fn().is_async && !fn().is_generator) {
                proto().emit(instruction{op::wrap_promise, r});
            }
            proto().emit(instruction{op::ret, r});
        } else {
            emit_implicit_return();
        }
        finish_frame(index, params.size());
        pop_scope();
        frames_.pop_back();
        in_chain_ = saved_in_chain;
        optional_exits_.swap(saved_exits);
        return index;
    }

    // --- expressions ---------------------------------------------------------
    // Every expression goes through here, and a member/call chain containing an
    // optional link is compiled as ONE unit so it has one exit.
    void compile_expr(std::int32_t idx, std::uint16_t dst) {
        if (idx >= 0 && out_.ok && !in_chain_ && chain_has_optional(idx)) {
            compile_chain(idx, dst);
            return;
        }
        compile_expr_inner(idx, dst);
    }

    void compile_expr_inner(std::int32_t idx, std::uint16_t dst) {
        if (idx < 0 || !out_.ok) {
            proto().emit(instruction{op::load_undef, dst});
            return;
        }
        const vp::node & n = at(idx);
        switch (n.kind) {
        case vp::nk::num: {
            // THE `n` SUFFIX MAKES IT A BigInt. The lexer keeps it on the token
            // because whether the digits are a valid one is this question, not
            // a lexing question: `1.5n` and `1e3n` are integers-only failures
            // and are refused here rather than there.
            if (n.text.ends_with('n')) {
                const std::string_view digits = n.text.substr(0, n.text.size() - 1);
                proto().emit(
                    instruction::with_bx(op::load_bigint, dst, intern_string(std::string{digits})));
                break;
            }
            emit_const(dst, value::number(number_literal(n.text)));
            break;
        }
        case vp::nk::str:
            proto().emit(instruction::with_bx(op::load_string, dst,
                                              intern_string(decode_string_literal(n.text))));
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
            const std::uint16_t name = name_operand(std::string{n.text});
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
            const std::uint16_t key = alloc_reg();
            compile_expr(n.b, key);
            proto().emit(instruction{op::get_index, dst, dst, key});
            release_to(mark > dst ? mark : static_cast<std::uint16_t>(dst + 1));
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
        // `new.target` - a meta-property, so it takes no operands and reads the
        // frame. Every transpiler emits it (Babel's `_classCallCheck` guard is
        // built on it) and Babylon.js uses it in its decorator metadata; it
        // reached this compiler as a PARSE error until ctjs learned it.
        case vp::nk::new_target: proto().emit(instruction{op::load_new_target, dst}); break;
        // Regular expressions are DEFERRED, not overlooked: they need a regex
        // engine, and the standard library says the same about String.match and
        // the RegExp forms of replace/split. Rejecting one by name beats
        // mis-compiling it into something that silently does nothing.
        case vp::nk::regex: compile_regex_literal(n, dst); break;
        // `yield x`, and `yield` with nothing - which yields undefined.
        //
        // Refused by name here until 2026-08-02, and the reason it stopped
        // being refused is Babylon.js: TypeScript compiles every `async`
        // function to a generator driven by an `__awaiter` helper, so 622
        // `function*` bodies in that bundle are not the author writing
        // generators at all - they are what `await` became.
        case vp::nk::yield_expr: {
            if (!fn().is_generator) {
                // `yield` outside a generator is a plain identifier in sloppy
                // mode and a SyntaxError in a generator-less function body.
                // Saying so beats compiling a suspend into a frame that can
                // never be resumed.
                fail("`yield` outside a generator function");
                proto().emit(instruction{op::load_undef, dst});
                break;
            }
            const std::uint16_t sent = alloc_reg();
            if (n.a >= 0) {
                compile_expr(n.a, sent);
            } else {
                proto().emit(instruction{op::load_undef, sent});
            }
            proto().emit(instruction{op::yield_value, dst, sent});
            break;
        }
        // ES MODULES ARE REFUSED BY NAME, not mis-compiled. The syntax parses
        // now (ctjs 2026-08-02); the semantics - a scope per module, live
        // bindings, a dependency graph, a loader - are staged in
        // docs/plans/modules.md and measured by tests/corpus/modules/module_ratchet.cpp.
        //
        // Refusing beats accepting: a page whose `import` silently did nothing
        // would run with half its bindings undefined and fail somewhere else
        // entirely, which is the failure mode this tree keeps paying for.
        case vp::nk::dynamic_import: {
            // A RUNTIME SPECIFIER, which is the whole difference from a static
            // import: there is nothing here for the loader to have resolved in
            // advance, and nothing to record in out_.imports. The graph a
            // dynamic import reaches is not knowable at compile time - that is
            // what it is FOR.
            const std::uint32_t mark = reg_mark();
            const std::uint16_t spec = alloc_reg();
            compile_expr(n.a, spec);
            proto().emit(instruction{op::dyn_import, dst, spec});
            release_to(mark);
            break;
        }
        case vp::nk::import_meta:
            fail("`import.meta` is not implemented yet - ES modules are staged in "
                 "docs/plans/modules.md");
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

    // Writing a name has to know whether it lives in a register, a cell, an
    // upvalue or the global table. ++/-- goes through here too, so it cannot
    // drift out of agreement with the read side - which is `compile_ident`.
    //
    // There was an `emit_read` here saying exactly that about itself, with a
    // body identical to compile_ident's but for taking a string_view instead of
    // a node. Nothing called it. Deleted 2026-08-09; the drift it was written to
    // prevent had already happened to it.
    void emit_write(std::string_view name_text, std::uint16_t src) {
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
            proto().emit(instruction{op::set_upvalue, static_cast<std::uint16_t>(up), src});
            return;
        }
        proto().emit(
            instruction::with_bx(op::set_global, src, intern_name(std::string{name_text})));
    }

    void compile_ident(const vp::node & n, std::uint16_t dst) {
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
            proto().emit(instruction{op::get_upvalue, dst, static_cast<std::uint16_t>(up)});
            return;
        }
        // `arguments` is not synthesised here: compile_function_body made it a
        // real local at entry, so it resolved above as a local or an upvalue.
        // Reaching this point means the mention is at TOP LEVEL, where a script
        // has no arguments and reading the name is an ordinary global lookup -
        // which is what a browser does too.
        const std::uint16_t name = name_operand(std::string{n.text});
        proto().emit(instruction::with_bx(op::get_global, dst, name));
    }

    // `delete o.x` / `delete o[k]`. Anything else - `delete x` on a plain
    // variable - is a no-op that yields false, which is what non-strict
    // JavaScript does with an undeletable binding.
    void compile_delete(const vp::node & n, std::uint16_t dst) {
        const std::uint32_t mark = reg_mark();
        const vp::node & target = at(n.a);
        if (target.kind == vp::nk::member) {
            const std::uint16_t object = alloc_reg();
            compile_expr(target.a, object);
            proto().emit(
                instruction{op::delete_prop, object, name_operand(std::string{target.text})});
            emit_const(dst, value::boolean(true));
        } else if (target.kind == vp::nk::index) {
            const std::uint16_t object = alloc_reg();
            compile_expr(target.a, object);
            const std::uint16_t key = alloc_reg();
            compile_expr(target.b, key);
            proto().emit(instruction{op::delete_index, object, key});
            emit_const(dst, value::boolean(true));
        } else {
            emit_const(dst, value::boolean(false));
        }
        release_to(mark);
    }

    void compile_binary(const vp::node & n, std::uint16_t dst) {
        const std::uint32_t mark = reg_mark();
        const std::uint16_t lhs = alloc_reg();
        const std::uint16_t rhs = alloc_reg();
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
    void compile_logical(const vp::node & n, std::uint16_t dst) {
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

    void compile_unary(const vp::node & n, std::uint16_t dst) {
        // `delete o.x` must NOT evaluate `o.x` - it takes the object and the
        // key, which is why it cannot go through the operand-first path below.
        if (n.text == "delete") {
            compile_delete(n, dst);
            return;
        }
        const std::uint32_t mark = reg_mark();
        const std::uint16_t operand = alloc_reg();
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
            // NOT `move`. Unary plus is ToNumber, and the only reason a copy
            // survived here is that the difference hides: `+x` used in a string
            // concatenation reads identically whether it converted or not, and
            // that is most of where it appears. It stops hiding the moment the
            // result indexes an array.
            proto().emit(instruction{op::to_number, dst, operand});
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
        std::uint16_t reg = 0;  // local/boxed: its register. member/index: the object.
        std::uint16_t key = 0;  // index: the key register
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
                out.reg = static_cast<std::uint16_t>(up);
                return out;
            }
            out.what = reference::kind::global;
            out.name = name_operand(std::string{target.text});
            return out;
        }
        if (target.kind == vp::nk::member) {
            out.what = reference::kind::member;
            out.reg = alloc_reg();
            compile_expr(target.a, out.reg);
            out.name = name_operand(std::string{target.text});
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

    void emit_load(const reference & ref, std::uint16_t dst) {
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
                instruction{op::get_prop, dst, ref.reg, static_cast<std::uint16_t>(ref.name)});
            break;
        case reference::kind::index:
            proto().emit(instruction{op::get_index, dst, ref.reg, ref.key});
            break;
        }
    }

    void emit_store(const reference & ref, std::uint16_t src) {
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
                instruction{op::set_prop, ref.reg, static_cast<std::uint16_t>(ref.name), src});
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
        // The bitwise compounds. Every one of these opcodes already existed for
        // the plain operator; only the assignment form was missing, so `x <<= 1`
        // was refused while `x = x << 1` compiled. p5.js's noise module is
        // stopped by exactly that.
        if (text == "&=") { return op::bit_and; }
        if (text == "|=") { return op::bit_or; }
        if (text == "^=") { return op::bit_xor; }
        if (text == "<<=") { return op::shl; }
        if (text == ">>=") { return op::shr; }
        if (text == ">>>=") { return op::ushr; }
        ok = false;
        return op::add;
    }

    void compile_assign(const vp::node & n, std::uint16_t dst) {
        const vp::node & target = at(n.a);
        const std::uint32_t mark = reg_mark();

        // DESTRUCTURING ASSIGNMENT: `[a, b] = pair` and `({x} = o)`.
        //
        // The left side is not a pattern node - the parser met it in expression
        // position and read an array or object LITERAL, which is the only thing
        // it could have been at the time. Re-reading it as a pattern here is
        // what the grammar itself does, and it is why array literals had to
        // learn about holes: `[, ref] = pair` skips the first element.
        if (n.text == "=" && (target.kind == vp::nk::array || target.kind == vp::nk::object)) {
            compile_expr(n.b, dst);
            compile_literal_as_pattern(n.a, dst);
            release_to(mark);
            return;
        }

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
        const std::uint16_t rhs = alloc_reg();
        emit_load(ref, dst);
        compile_expr(n.b, rhs);
        proto().emit(instruction{operation, dst, dst, rhs});
        emit_store(ref, dst);
        release_to(mark);
    }

    void compile_update(const vp::node & n, std::uint16_t dst) {
        const vp::node & target = at(n.a);
        const std::uint32_t mark = reg_mark();
        // Through the same reference machinery as compound assignment, so
        // `obj.n++` and `a[i]++` work and evaluate their target exactly once.
        const reference ref = prepare_reference(target);
        const std::uint16_t cur = alloc_reg();
        const std::uint16_t one = alloc_reg();
        emit_load(ref, cur);
        emit_const(one, value::number(1));
        // postfix yields the OLD value, prefix the new one
        if (n.b == 0) { proto().emit(instruction{op::move, dst, cur}); }
        proto().emit(instruction{n.text == "++" ? op::add : op::sub, cur, cur, one});
        if (n.b != 0) { proto().emit(instruction{op::move, dst, cur}); }
        emit_store(ref, cur);
        release_to(mark);
    }

    void compile_ternary(const vp::node & n, std::uint16_t dst) {
        const std::uint32_t mark = reg_mark();
        const std::uint16_t cond = alloc_reg();
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
    void compile_template(const vp::node & n, std::uint16_t dst) {
        std::string_view raw = n.text;
        if (raw.size() >= 2 && raw.front() == '`' && raw.back() == '`') {
            raw = raw.substr(1, raw.size() - 2);
        }
        const std::uint32_t mark = reg_mark();
        const std::uint16_t piece = alloc_reg();
        bool started = false;

        const auto append = [&](std::uint16_t src) {
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
                // PARENTHESISED, so the hole is parsed as an EXPRESSION.
                // `${ {v: 1}.v }` parses as a program otherwise, and a leading
                // brace at statement position is a BLOCK - so the object
                // literal became a labelled statement and the whole hole
                // evaluated to undefined, silently.
                compile_owned_expr("(" + std::string{raw.substr(start, at_char - start)} + ")",
                                   piece);
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
    void emit_super_base(std::uint16_t dst) {
        proto().emit(instruction{op::load_home, dst});
        proto().emit(instruction{op::get_proto, dst, dst});
    }

    [[nodiscard]] bool any_spread(std::span<const std::int32_t> args) const {
        for (const std::int32_t arg : args) {
            if (at(arg).kind == vp::nk::spread) { return true; }
        }
        return false;
    }

    // The arguments of a call, as one array. Same shape as an array literal,
    // because that is exactly what it is.
    void emit_argument_array(std::span<const std::int32_t> args, std::uint16_t dst) {
        proto().emit(instruction{op::new_array, dst});
        const std::uint32_t mark = reg_mark();
        for (const std::int32_t arg : args) {
            const std::uint16_t v = alloc_reg();
            if (at(arg).kind == vp::nk::spread) {
                compile_expr(at(arg).a, v);
                emit_append_all(dst, v);
            } else {
                compile_expr(arg, v);
                proto().emit(instruction{op::append, dst, v});
            }
            release_to(mark);
        }
    }

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
    void compile_spread_call(const vp::node & n, std::uint16_t dst) {
        const std::span<const std::int32_t> args = kids(n);
        const vp::node & callee = at(n.a);
        const std::uint32_t mark = reg_mark();
        const std::uint16_t target = alloc_reg();
        const std::uint16_t self = alloc_reg();

        const bool super_method = callee.kind == vp::nk::member && callee.a >= 0 &&
                                  at(callee.a).kind == vp::nk::super_lit;
        if (callee.kind == vp::nk::super_lit || super_method) {
            emit_super_base(target);
            const std::string name = super_method ? std::string{callee.text} : "constructor";
            proto().emit(instruction{op::get_prop, target, target, name_operand(name)});
            proto().emit(instruction{op::load_this, self});
        } else if (callee.kind == vp::nk::member) {
            compile_expr(callee.a, self);
            proto().emit(
                instruction{op::get_prop, target, self, name_operand(std::string{callee.text})});
        } else if (callee.kind == vp::nk::index) {
            compile_expr(callee.a, self);
            const std::uint16_t key = alloc_reg();
            compile_expr(callee.b, key);
            proto().emit(instruction{op::get_index, target, self, key});
        } else {
            compile_expr(n.a, target);
            proto().emit(instruction{op::load_undef, self});
        }

        const std::uint16_t argv = alloc_reg();
        emit_argument_array(args, argv);
        // `super(...)` - NOT `super.m(...)` - carries new.target into the base
        // constructor. Babylon reads it there to hang decorator metadata off
        // the class actually being constructed, and got undefined.
        if (callee.kind == vp::nk::super_lit) { proto().emit(instruction{op::pass_new_target}); }
        proto().emit(instruction{op::apply, target, argv, self});
        proto().emit(instruction{op::move, dst, target});
        release_to(mark);
    }

    void compile_call(const vp::node & n, std::uint16_t dst) {
        const std::span<const std::int32_t> args = kids(n);
        if (any_spread(args)) {
            compile_spread_call(n, dst);
            return;
        }
        const vp::node & callee = at(n.a);
        const std::uint32_t mark = reg_mark();
        const std::uint16_t base = alloc_reg();

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
            const std::uint16_t self = alloc_reg();
            proto().emit(instruction{op::load_this, self});
            // `super(...)` - NOT `super.m(...)` - carries new.target into the
            // base constructor. THIS is the path a plain super() takes; the
            // spread form a few hundred lines up is the other one, and marking
            // only that one left the common case still reading undefined.
            if (callee.kind == vp::nk::super_lit) {
                proto().emit(instruction{op::pass_new_target});
            }
            proto().emit(instruction{op::call_receiver, base,
                                     static_cast<std::uint16_t>(args.size()), self});
            proto().emit(instruction{op::move, dst, base});
            release_to(mark);
            return;
        }

        // `x?.m(...)` IS A METHOD CALL. The parser gives it as call(opt_member(x,
        // 'm')), so the callee is an opt_member and this fell through to the plain
        // path below - which calls the function with NO receiver. `this` was
        // undefined inside the method, and for a primitive receiver that means the
        // wrong answer rather than an error: `s?.trim()` was undefined,
        // `(5)?.toFixed(1)` was NaN, `[1,2]?.join('-')` was "".
        //
        // It cost every colour string in p5.js. `parse$4` opens with `String(str)
        // ?.trim()`, so every `fill('#ff0000')`, `color('red')` and
        // `background('#fff')` threw "Invalid color string" - and an object
        // receiver hid it, because a method that ignores `this` works either way.
        const bool optional_member =
            callee.kind == vp::nk::opt_member || callee.kind == vp::nk::opt_index;
        if (optional_member) {
            compile_expr(callee.a, base); // the receiver, which may be nullish
            // The short-circuit, released before the argument window is reserved
            // so the arguments stay contiguous from base+1.
            {
                const std::uint32_t guard = reg_mark();
                const std::uint16_t nullish = alloc_reg();
                proto().emit(instruction{op::load_null, nullish});
                const std::uint16_t test = alloc_reg();
                proto().emit(instruction{op::loose_equal, test, base, nullish});
                optional_exits_.push_back(proto().emit(instruction{op::jump_if_true, test}));
                release_to(guard);
            }
            if (callee.kind == vp::nk::opt_member) {
                for (const std::int32_t arg : args) { compile_expr(arg, alloc_reg()); }
                proto().emit(instruction{op::call_method, base,
                                         static_cast<std::uint16_t>(args.size()),
                                         name_operand(std::string{callee.text})});
            } else {
                std::vector<std::uint16_t> arg_regs;
                arg_regs.reserve(args.size());
                for (std::size_t i = 0; i < args.size(); ++i) { arg_regs.push_back(alloc_reg()); }
                const std::uint16_t key = alloc_reg();
                compile_expr(callee.b, key);
                for (std::size_t i = 0; i < args.size(); ++i) {
                    compile_expr(args[i], arg_regs[i]);
                }
                proto().emit(instruction{op::call_computed, base,
                                         static_cast<std::uint16_t>(args.size()), key});
            }
            proto().emit(instruction{op::move, dst, base});
            release_to(mark);
            return;
        }

        if (callee.kind == vp::nk::member) {
            compile_expr(callee.a, base); // the receiver
            for (const std::int32_t arg : args) { compile_expr(arg, alloc_reg()); }
            const std::uint16_t name = name_operand(std::string{callee.text});
            proto().emit(
                instruction{op::call_method, base, static_cast<std::uint16_t>(args.size()), name});
        } else if (callee.kind == vp::nk::index) {
            // `obj[name](...)` is a METHOD call: the receiver is obj. Compiling
            // it as a plain call leaves `this` undefined inside the method.
            compile_expr(callee.a, base); // the receiver
            // THE ARGUMENT WINDOW IS RESERVED BEFORE THE KEY IS EVALUATED.
            //
            // call_computed reads its arguments from base+1 upwards, so base+1
            // belongs to argument 0 and to nothing else. Evaluating the key into
            // a register first took base+1, pushed the arguments up one, and the
            // VM then read the KEY as argument 0 and dropped the last argument -
            // `t['k'](1, 2, 3)` arrived as `('k', 1, 2)`.
            //
            // Reserving first and filling after keeps both rules: the arguments
            // are contiguous from base+1, and the key is still evaluated BEFORE
            // them, which is the order JavaScript specifies.
            std::vector<std::uint16_t> arg_regs;
            arg_regs.reserve(args.size());
            for (std::size_t i = 0; i < args.size(); ++i) { arg_regs.push_back(alloc_reg()); }
            const std::uint16_t key = alloc_reg();
            compile_expr(callee.b, key);
            for (std::size_t i = 0; i < args.size(); ++i) { compile_expr(args[i], arg_regs[i]); }
            proto().emit(
                instruction{op::call_computed, base, static_cast<std::uint16_t>(args.size()), key});
        } else {
            compile_expr(n.a, base);
            for (const std::int32_t arg : args) { compile_expr(arg, alloc_reg()); }
            proto().emit(instruction{op::call, base, static_cast<std::uint16_t>(args.size())});
        }
        proto().emit(instruction{op::move, dst, base});
        release_to(mark);
    }

    // `new C(...)`. The receiver is created by the VM, which also has to decide
    // what the expression evaluates to - the new object, unless the constructor
    // returned one of its own.
    void compile_new(const vp::node & n, std::uint16_t dst) {
        const std::span<const std::int32_t> args = kids(n);
        const std::uint32_t mark = reg_mark();
        const std::uint16_t base = alloc_reg();
        if (any_spread(args)) {
            compile_expr(n.a, base);
            const std::uint16_t argv = alloc_reg();
            emit_argument_array(args, argv);
            proto().emit(instruction{op::construct_apply, base, argv});
            proto().emit(instruction{op::move, dst, base});
            release_to(mark);
            return;
        }
        compile_expr(n.a, base);
        for (const std::int32_t arg : args) { compile_expr(arg, alloc_reg()); }
        proto().emit(instruction{op::construct, base, static_cast<std::uint16_t>(args.size())});
        proto().emit(instruction{op::move, dst, base});
        release_to(mark);
    }

    // Optional chaining. The whole point is the SHORT CIRCUIT: `a?.b.c` yields
    // undefined without evaluating `.c` when a is null-ish, so writing it as an
    // ordinary member access with a test afterwards would still crash.
    // Does this member/call chain contain an optional link ANYWHERE below it?
    // Only the spine is walked - `a?.b(c.d)` is optional, `a.b(c?.d)` is not,
    // because the argument is its own chain.
    [[nodiscard]] bool chain_has_optional(std::int32_t idx) const {
        for (std::int32_t at = idx; at >= 0;) {
            const vp::node & n = this->at(at);
            switch (n.kind) {
            case vp::nk::opt_member:
            case vp::nk::opt_index:
            case vp::nk::opt_call: return true;
            case vp::nk::member:
            case vp::nk::index:
            case vp::nk::call: at = n.a; continue;
            default: return false;
            }
        }
        return false;
    }

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
    void compile_chain(std::int32_t idx, std::uint16_t dst) {
        std::vector<std::size_t> outer;
        outer.swap(optional_exits_);
        const bool root = !in_chain_;
        in_chain_ = true;
        compile_expr_inner(idx, dst);
        if (root) {
            in_chain_ = false;
            if (!optional_exits_.empty()) {
                const std::size_t done = proto().emit(instruction{op::jump});
                for (const std::size_t site : optional_exits_) { patch_here(site); }
                proto().emit(instruction{op::load_undef, dst});
                patch_here(done);
            }
        }
        optional_exits_.swap(outer);
        if (!root) {
            // a nested chain hands its exits back to the enclosing one
            for (const std::size_t site : outer) { optional_exits_.push_back(site); }
        }
    }

    void compile_optional(const vp::node & n, std::uint16_t dst) {
        const std::uint32_t mark = reg_mark();
        const std::uint16_t object = alloc_reg();
        compile_expr(n.a, object);

        // null and undefined both short-circuit; nothing else does.
        const std::uint16_t nullish = alloc_reg();
        proto().emit(instruction{op::load_null, nullish});
        const std::uint16_t test = alloc_reg();
        proto().emit(instruction{op::loose_equal, test, object, nullish});
        const std::size_t skip = proto().emit(instruction{op::jump_if_true, test});

        if (n.kind == vp::nk::opt_member) {
            proto().emit(instruction{op::get_prop, dst, object, name_operand(std::string{n.text})});
        } else if (n.kind == vp::nk::opt_index) {
            const std::uint16_t key = alloc_reg();
            compile_expr(n.b, key);
            proto().emit(instruction{op::get_index, dst, object, key});
        } else { // opt_call
            const std::span<const std::int32_t> args = kids(n);
            const std::uint16_t base = alloc_reg();
            proto().emit(instruction{op::move, base, object});
            for (const std::int32_t arg : args) { compile_expr(arg, alloc_reg()); }
            proto().emit(instruction{op::call, base, static_cast<std::uint16_t>(args.size())});
            proto().emit(instruction{op::move, dst, base});
        }
        // The exit belongs to the CHAIN, not to this link. compile_chain
        // patches every one of them to a single point past the whole thing.
        optional_exits_.push_back(skip);
        release_to(mark);
    }

    // The comma operator: evaluate everything, yield the last.
    void compile_sequence(const vp::node & n, std::uint16_t dst) {
        // A SEQUENCE IS BINARY, not a list. The parser builds `a, b, c` as
        // seq(seq(a, b), c) - left-nested, two children per node - and this
        // read `kids(n)` instead, which for such a node is EMPTY. So every
        // comma expression evaluated nothing at all and produced undefined,
        // silently, from the day the operator was added.
        //
        // Nothing caught it because the operator was added to the parser with
        // no test that a comma expression has EFFECTS - only that it parsed.
        // `_createClass(e, r, t) { return r && _defineProperties(...), ..., e; }`
        // is how every Babel-transpiled class installs its methods, so this one
        // omission silently emptied every such class in p5.js.
        compile_expr(n.a, dst);
        compile_expr(n.b, dst);
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
    // A function whose whole body is `this.x = <init>` for each instance field,
    // in declaration order. `new` runs it against the fresh object before the
    // constructor body, so every instance gets its OWN value - which is the
    // difference between `items = []` meaning an empty array per instance and
    // meaning one array shared by all of them.
    //
    // It is compiled as an ordinary nested function, so an initialiser that
    // mentions an enclosing local captures it as an upvalue like anything else.
    [[nodiscard]] std::uint32_t compile_field_initialiser(
        const std::vector<std::int32_t> & fields) {
        const auto index = static_cast<std::uint32_t>(out_.functions.size());
        out_.functions.emplace_back();
        out_.functions[index].name = "<fields>";

        frames_.emplace_back();
        frames_.back().proto = index;
        push_scope();
        // A FUNCTION BODY IS NOT PART OF THE CHAIN THAT ENCLOSES IT.
        //
        // `a?.b(() => c?.d)` compiles the arrow while the outer chain is open,
        // so without this the arrow's own short-circuit would be recorded on
        // the outer chain's exit list - and patched into the ENCLOSING proto's
        // code array, at an index that means something else entirely there.
        const bool saved_in_chain = in_chain_;
        std::vector<std::size_t> saved_exits;
        saved_exits.swap(optional_exits_);
        in_chain_ = false;
        for (const std::int32_t member : fields) {
            const vp::node & m = at(member);
            const std::uint32_t mark = reg_mark();
            const std::uint16_t self = alloc_reg();
            proto().emit(instruction{op::load_this, self});
            const std::uint16_t v = alloc_reg();
            // `class A { x; }` declares x and gives it undefined - a field
            // without an initialiser is still a field.
            if (m.b >= 0) {
                compile_expr(m.b, v);
            } else {
                proto().emit(instruction{op::load_undef, v});
            }
            if ((m.d & 2) != 0 && m.a >= 0) { // a computed key: `[expr] = init`
                const std::uint16_t key = alloc_reg();
                compile_expr(m.a, key);
                proto().emit(instruction{op::set_index, self, key, v});
            } else {
                proto().emit(instruction{op::set_prop, self, name_operand(std::string{m.text}), v});
            }
            release_to(mark);
        }
        proto().emit(instruction{op::ret_undef});
        finish_frame(index, 0);
        pop_scope();
        frames_.pop_back();
        in_chain_ = saved_in_chain;
        optional_exits_.swap(saved_exits);
        return index;
    }

    // Bind a class's own name to the class value, by whichever route this
    // frame uses. Harmless for a `class Foo {}` DECLARATION, which binds the
    // same value again a moment later.
    // `force` is for a class EXPRESSION, whose name must be a binding of its own
    // and never a write to something outer. At the top level a declaration's name
    // is a global, so the frame-depth test is right for that case - but for an
    // expression it meant the name had no binding at all and emit_write fell
    // through to set_global, CLOBBERING any global of the same name. `var Shared =
    // {...}; var alias = class Shared {}` replaced the object with the class.
    void declare_class_name(std::string name, bool force = false) {
        if ((force || frames_.size() > 1) && !was_predeclared(name) &&
            find_local_in_current_scope(name) == nullptr) {
            const std::uint16_t reg = declare_local(name);
            proto().emit(instruction{op::load_undef, reg});
            if (fn().locals.back().boxed) { proto().emit(instruction{op::new_cell, reg}); }
        }
    }

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
    void compile_class(const vp::node & n, std::uint16_t dst, bool as_declaration = false) {
        // The name is DECLARED first, before any method body is compiled, so a
        // method that mentions it resolves to this binding rather than to an
        // outer one - capture is decided when the nested function is compiled,
        // not when it runs. The value is written further down, as soon as the
        // class exists.
        // A scope of its own for an expression's name, so the methods can capture
        // it and the code after the expression cannot see it.
        const bool own_scope = !as_declaration && !n.text.empty();
        if (own_scope) { push_scope(); }
        if (!n.text.empty()) { declare_class_name(std::string{n.text}, own_scope); }
        const std::span<const std::int32_t> members = kids(n);
        const std::uint32_t mark = reg_mark();
        const std::uint16_t prototype_reg = alloc_reg();
        proto().emit(instruction{op::new_object, prototype_reg});

        if (n.a >= 0) {
            // `extends`: the parent's prototype becomes this one's, so a lookup
            // that misses here walks up to it.
            const std::uint16_t parent = alloc_reg();
            compile_expr(n.a, parent);
            const std::uint16_t parent_proto = alloc_reg();
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
            // Named after the CLASS. A constructor is a function expression, so
            // it had no name of its own and every stack trace through one said
            // `<anonymous>` - which in a 4.5 MB bundle is no answer at all.
            const std::uint32_t index =
                compile_function_body(constructor_body, std::string{n.text});
            proto().emit(instruction::with_bx(op::closure, dst, index));
        } else if (n.a >= 0) {
            // A DERIVED class with no constructor gets `constructor(...args) {
            // super(...args); }`. Synthesising an EMPTY one instead meant the
            // parent constructor never ran, so `class MySet extends Set {}`
            // produced an object with none of Set's state and every method on
            // it failed - with nothing to say the constructor had been skipped.
            compile_foreign_expr("(function (...args) { super(...args); })", dst);
        } else {
            // A base class with no constructor still needs a callable, or `new`
            // has nothing to invoke.
            compile_foreign_expr("(function () {})", dst);
        }
        // A SYNTHESISED CONSTRUCTOR IS STILL NAMED AFTER ITS CLASS. Both
        // branches above build one from source text, so it arrives anonymous -
        // and `Object.getPrototypeOf(x).constructor.name` is a standard way to
        // identify a value, where an undefined name compares equal to the other
        // undefined it is being tested against and reports a false match.
        if (!proto().code.empty() && proto().code.back().code == op::closure) {
            out_.functions[proto().code.back().bx()].name = std::string{n.text};
        }
        proto().emit(instruction{op::set_prop, dst, name_operand("prototype"), prototype_reg});
        // `C.prototype.constructor === C`, which is both what pages expect and
        // how `super(...)` finds the parent constructor to call.
        proto().emit(instruction{op::set_prop, prototype_reg, name_operand("constructor"), dst});
        // The constructor's home is this class's prototype, so `super(...)`
        // inside it starts one level up.
        proto().emit(instruction{op::set_prop, dst, name_operand("__home"), prototype_reg});

        // INSTANCE FIELDS ARE PER INSTANCE, and they used to be evaluated once
        // at class-definition time and stored on the PROTOTYPE. So every
        // instance of `class A { items = [] }` shared one array: push to one and
        // it appeared in all of them. Nothing about that was an error, and it is
        // the nastiest shape of bug in this whole batch.
        //
        // They are compiled into a hidden initialiser instead, which `new` runs
        // against the fresh object before the constructor body. Static fields
        // are NOT this - evaluating those once and putting them on the
        // constructor is exactly right, and that is what still happens below.
        std::vector<std::int32_t> instance_fields;
        for (const std::int32_t member : members) {
            const vp::node & m = at(member);
            if (m.c == 0 && (m.d & 1) == 0) { instance_fields.push_back(member); }
        }
        if (!instance_fields.empty()) {
            const std::uint32_t fields = compile_field_initialiser(instance_fields);
            const std::uint16_t init = alloc_reg();
            proto().emit(
                instruction::with_bx(op::closure, init, static_cast<std::uint16_t>(fields)));
            proto().emit(instruction{op::set_prop, dst, name_operand("__fields"), init});
        }

        // A NAMED CLASS EXPRESSION BINDS ITS OWN NAME, and its methods see it.
        //
        // `let p5$2 = class p5 { static register(a) { p5._seen.has(a); } }` is
        // how p5.js declares itself, and `p5` inside those methods is the class
        // - not any outer binding. Without this the methods read an undefined
        // global and failed at the first property they touched, three frames
        // deep and nowhere near the class.
        //
        // The binding is made before the methods are COMPILED so they capture
        // it, and written as soon as the class value exists.
        if (!n.text.empty()) { emit_write(n.text, dst); }

        const std::uint16_t slot = alloc_reg();
        for (const std::int32_t member : members) {
            const vp::node & m = at(member);
            if (m.text == "constructor" && m.c == 1) { continue; }
            if (m.c == 0 && (m.d & 1) == 0) { continue; } // an instance field; handled above
            if (m.c == 2) {
                // An accessor. It goes on the prototype like a method - or on
                // the constructor when static - and `d` bit2 says which half.
                // (Named below, with the methods.)
                // Installing it as a DATA property, which is what happened
                // before, made `obj.v` be the function rather than call it, and
                // a `set` of the same name overwrote the getter outright.
                compile_expr(m.b, slot);
                const std::uint16_t name = name_operand(std::string{m.text});
                const std::uint16_t target = (m.d & 1) != 0 ? dst : prototype_reg;
                proto().emit(instruction{(m.d & 4) != 0 ? op::define_setter : op::define_getter,
                                         target, name, slot});
                continue;
            }
            if (m.b < 0) { continue; }
            compile_expr(m.b, slot);
            const std::uint16_t name = name_operand(std::string{m.text});
            // A static member goes on the constructor; everything else on the
            // prototype, where instances find it.
            const std::uint16_t target = (m.d & 1) != 0 ? dst : prototype_reg;
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
        // Closed AFTER every method is compiled, so they capture the name, and
        // before anything else in the enclosing scope is - so nothing else sees
        // it. The register stays allocated, which is what a scope pop means here.
        if (own_scope) { pop_scope(); }
    }

    // `/ab+c/gi`. The lexer hands the literal over whole, delimiters and all,
    // so the source is between the first `/` and the last one and the flags are
    // what follows.
    //
    // It compiles to a CALL of the reserved factory rather than to an opcode:
    // a regex is an ordinary object here, and the standard library is the only
    // thing that needs to know how one is built. Reserved rather than `RegExp`
    // so a page that shadows the constructor cannot change what its own
    // literals mean.
    void compile_regex_literal(const vp::node & n, std::uint16_t dst) {
        const std::string_view literal = n.text;
        const std::size_t close = literal.rfind('/');
        if (literal.size() < 2 || literal.front() != '/' || close == 0) {
            fail("malformed regular expression literal (" + std::string{literal} + ")");
            proto().emit(instruction{op::load_undef, dst});
            return;
        }
        const std::uint32_t mark = reg_mark();
        const std::uint16_t callee = alloc_reg();
        proto().emit(instruction::with_bx(op::get_global, callee,
                                          intern_name(std::string{regexp_factory_name})));
        const std::uint16_t source = alloc_reg();
        emit_string(source, std::string{literal.substr(1, close - 1)});
        const std::uint16_t flags = alloc_reg();
        emit_string(flags, std::string{literal.substr(close + 1)});
        proto().emit(instruction{op::call, callee, 2});
        proto().emit(instruction{op::move, dst, callee});
        release_to(mark);
    }

    void compile_array(const vp::node & n, std::uint16_t dst) {
        proto().emit(instruction{op::new_array, dst});
        const std::uint32_t mark = reg_mark();
        for (const std::int32_t element : kids(n)) {
            const std::uint16_t v = alloc_reg();
            // A HOLE. `[, x]` and `[a, , b]` are legal, and an element list is
            // the one place kids() yields -1 - which is why at() refuses a
            // negative index rather than reading past the pool.
            if (element < 0) {
                proto().emit(instruction{op::load_undef, v});
                proto().emit(instruction{op::append, dst, v});
                release_to(mark);
                continue;
            }
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
    void emit_append_all(std::uint16_t target, std::uint16_t source) {
        const std::uint32_t mark = reg_mark();
        // SPREAD TAKES ANYTHING ITERABLE, not just an array. This walks a
        // `length`, so `[...new Set(v)]` and `f(...map.keys())` produced nothing
        // at all - see context::iterable_values for what that cost.
        proto().emit(instruction{op::iterable, source, source});
        const std::uint16_t length = alloc_reg();
        proto().emit(instruction{op::get_prop, length, source, name_operand("length")});
        const std::uint16_t index = alloc_reg();
        emit_const(index, value::number(0));
        const std::uint16_t one = alloc_reg();
        emit_const(one, value::number(1));
        const std::uint16_t test = alloc_reg();
        const std::uint16_t item = alloc_reg();
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

    void compile_object(const vp::node & n, std::uint16_t dst) {
        proto().emit(instruction{op::new_object, dst});
        const std::uint32_t mark = reg_mark();
        for (const std::int32_t p : kids(n)) {
            const vp::node & prop = at(p);
            if (prop.kind == vp::nk::spread) {
                // `{...o}` copies o's own properties in, and a later key still
                // wins - which is why this is a copy at this point in the
                // sequence rather than a merge at the end.
                const std::uint16_t source = alloc_reg();
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
                // An accessor, not a data property. `d` bit2 says which half.
                const std::uint16_t fnreg = alloc_reg();
                compile_expr(prop.b, fnreg);
                const std::uint16_t name = name_operand(decode_string_literal(prop.text));
                proto().emit(instruction{(prop.d & 4) != 0 ? op::define_setter : op::define_getter,
                                         dst, name, fnreg});
                release_to(mark);
                continue;
            }
            const std::uint16_t v = alloc_reg();
            if (prop.b >= 0) {
                compile_expr(prop.b, v);
            } else {
                compile_ident(prop, v); // shorthand { x }
            }
            // A computed key - `{[k]: v}`, and also `{"a": v}` and `{1: v}`,
            // which the parser routes the same way so quotes and escapes get
            // cooked by evaluating the literal.
            if ((prop.d & 1) != 0) {
                const std::uint16_t key = alloc_reg();
                compile_expr(prop.a, key);
                proto().emit(instruction{op::set_index, dst, key, v});
            } else {
                const std::uint16_t name = name_operand(decode_string_literal(prop.text));
                proto().emit(instruction{op::set_prop, dst, name, v});
            }
            release_to(mark);
        }
    }

    // --- helpers -------------------------------------------------------------
    void emit_string(std::uint16_t dst, std::string text) {
        proto().emit(instruction::with_bx(op::load_string, dst, intern_string(std::move(text))));
    }
    void emit_const(std::uint16_t dst, value v) {
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
        // The SAME split with_bx uses, and it has to stay that way: sbx() reads
        // b and c back as one 32-bit field. Widening the operands without
        // widening this shift left every jump encoded 8/8 into 16-bit halves,
        // so every branch went somewhere else - and the symptom was an `if`
        // body silently not running, not a crash.
        const auto wide = static_cast<std::uint32_t>(offset);
        jump.b = static_cast<std::uint16_t>(wide >> 16);
        jump.c = static_cast<std::uint16_t>(wide & 0xFFFF);
    }

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

} // namespace

program compiler::compile(std::string_view source, script_kind kind) {
    const vp::ast tree = vp::parse(source);
    program out;
    if (!tree.ok) {
        out.ok = false;
        // WITH THE POSITION, which was thrown away here for as long as this
        // function has existed. `vp::ast` carries `error_offset` precisely so a
        // caller holding the source can say where the parser stopped - the
        // comment on that field says so - and dropping it left every parse
        // failure reading "parse error: expression" about a bundle several
        // megabytes long. That is half a diagnostic, and it is the half that
        // costs the afternoon.
        //
        // Resolved to a line and column here rather than by every caller,
        // because the source is right there and none of them have it in a
        // convenient form. The shape matches what the ratchets' tooling already
        // greps for - `<name>:<line>:<column>`.
        std::size_t line = 1;
        std::size_t column = 1;
        const std::size_t stopped = std::min(tree.error_offset, source.size());
        for (std::size_t i = 0; i < stopped; ++i) {
            if (source[i] == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }
        out.error = "parse error: " + std::string{tree.error} + " - at " + std::to_string(line) +
                    ":" + std::to_string(column);
        return out;
    }
    out.source = std::string{source};
    compiler_impl c{tree, out};
    c.module_scope_ = kind == script_kind::module_;
    c.compile_program();
    return out;
}

} // namespace ctbrowser::script
