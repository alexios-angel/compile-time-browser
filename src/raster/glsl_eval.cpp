#include <ctbrowser/raster/glsl.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <string_view>
#include <unordered_map>

// The REFERENCE evaluator: a plain tree walker.
//
// DELIBERATELY THE SIMPLEST THING THAT IS CORRECT, because it is the ORACLE.
// Stage three of docs/webgl-plan.md compiles the same tree to bytecode and runs
// it over eight fragments at once with masked control flow; that is a large
// amount of machinery to believe on its own, and the way to believe it is to run
// both on the same inputs and require the same bits. A fast implementation
// checked only against itself proves nothing.
//
// So this file optimises for being obviously right. It allocates a vector per
// value, looks up names in a map, and walks the tree for every fragment. It is
// the slowest thing in the renderer by design, and the benchmark says by how
// much so stage three has a number to beat.
//
// THE TWO RULES THAT ARE EASY TO GET WRONG AND LOOK ALMOST RIGHT:
//
//   * matrices are COLUMN-MAJOR - `m[i]` is a column, and `m * v` sums columns
//     scaled by v's components. Transposed gives a plausible wrong rotation.
//   * `int / int` TRUNCATES. Getting that wrong makes a texel index drift by one
//     only sometimes, which is the worst kind of wrong.

namespace ctbrowser::raster::glsl {
namespace {

// String-keyed, but looked up by string_view - see glsl::string_hash.
template <typename V>
using by_name = std::unordered_map<std::string, V, string_hash, std::equal_to<>>;

[[nodiscard]] type shape(base b, std::uint8_t rows, std::uint8_t cols = 1) {
    return type{b, rows, cols, -1, 0};
}

// --- swizzles --------------------------------------------------------------

// `.xyzw`, `.rgba` and `.stpq` are three spellings of the same four positions.
// Returns -1 for a letter that is not one of them.
[[nodiscard]] int swizzle_index(char c) {
    switch (c) {
    case 'x':
    case 'r':
    case 's': return 0;
    case 'y':
    case 'g':
    case 't': return 1;
    case 'z':
    case 'b':
    case 'p': return 2;
    case 'w':
    case 'a':
    case 'q': return 3;
    default: return -1;
    }
}

[[nodiscard]] bool is_swizzle(std::string_view text) {
    if (text.empty() || text.size() > 4) { return false; }
    return std::ranges::all_of(text, [](char c) { return swizzle_index(c) >= 0; });
}

// --- the interpreter -------------------------------------------------------

// How a statement finished. `discard` and `return` both unwind, and telling them
// apart is what stops a discarded fragment from also writing an output.
enum class flow : std::uint8_t {
    normal,
    broke,
    continued,
    returned,
    discarded
};

// What a shader needs built ONCE, whatever it is run against. Everything here is
// decided by the source alone, so it survives from fragment to fragment.
struct prepared_state {
    by_name<std::vector<std::int32_t>> functions;
    by_name<value> globals; // the template, after initialisers
    std::int32_t main_fn = -1;
    std::string error;
};

class interpreter {
public:
    interpreter(const shader & m, const environment & env, execution & out)
        : m_(&m), env_(&env), out_(&out) {}

    // Build what only depends on the source. Split out so a caller drawing many
    // fragments pays for it once - see glsl::program and the measurement in its
    // header comment.
    [[nodiscard]] prepared_state prepare() {
        build_globals();
        prepared_state out;
        out.functions = functions_;
        out.globals = globals_;
        out.main_fn = find_function("main", {});
        if (out.main_fn < 0) { out.error = "no main()"; }
        out.error = out.error.empty() ? failure_ : out.error;
        return out;
    }

    // Run against an already-built state. The globals are COPIED because main
    // writes into them - gl_Position and every varying live there - so the
    // template has to survive for the next fragment.
    void run_prepared(const prepared_state & ready) {
        if (!ready.error.empty()) {
            out_->error = ready.error;
            return;
        }
        functions_ = ready.functions;
        globals_ = ready.globals;
        run_main(ready.main_fn);
    }

    void run() {
        build_globals();
        run_rest();
    }

    void build_globals() {
        // Top-level declarations first: a global `const` may be read by main,
        // and every function has to be findable before one calls another.
        for (const std::int32_t which : m_->declarations) {
            const node & n = m_->at(which);
            if (n.kind == nk::function) {
                functions_[n.text].push_back(which);
                continue;
            }
            if (n.kind != nk::var_decl) { continue; }
            // A uniform, attribute or varying comes from the environment; only a
            // global with an initialiser is evaluated here.
            if (n.store == storage::none || n.store == storage::constant) {
                value made = n.a >= 0 ? evaluate(n.a) : zero(n.t);
                globals_[n.text] = convert(std::move(made), n.t);
            }
        }
        // WHAT THE SHADER WRITES has to exist before it writes it.
        //
        // A vertex shader's `varying`s are its OUTPUTS and a fragment shader's
        // are its INPUTS - the same declaration, read from the environment in one
        // stage and written into a local in the other. Getting that backwards
        // makes every varying either unassignable or always zero.
        for (const interface_variable & v : m_->interface_) {
            if (v.store != storage::varying) { continue; }
            if (m_->which == stage::vertex) { globals_[v.name] = zero(v.t); }
        }
        // STATIC ARRAYS, NOT A TERNARY OVER TWO `initializer_list`s. That form
        // built here for months and is a DANGLING POINTER before C++23: an
        // initializer_list's backing array is a temporary, and only P2718R0
        // extends the lifetime of temporaries in a range-for's initializer.
        // Clang implements it, the GCC 13 on the shared devbox does not, and it
        // failed the build there with -Wdangling-pointer the first time this
        // tree was compiled on a second compiler.
        //
        // Correct under every standard version rather than only the newest:
        // these have static storage duration, so there is no lifetime question
        // to get right.
        static constexpr const char * vertex_outputs[] = {"gl_Position", "gl_PointSize"};
        static constexpr const char * fragment_outputs[] = {"gl_FragColor", "gl_FragDepth"};
        const auto & outputs = m_->which == stage::vertex ? vertex_outputs : fragment_outputs;
        for (const char * name : outputs) {
            const bool wide =
                std::string_view{name} == "gl_Position" || std::string_view{name} == "gl_FragColor";
            globals_[name] = zero(shape(base::f, wide ? 4 : 1));
        }
    }

    void run_rest() {
        const std::int32_t main_fn = find_function("main", {});
        if (main_fn < 0) {
            fail("no main()");
            return;
        }
        run_main(main_fn);
    }

private:
    void run_main(std::int32_t main_fn) {
        std::vector<value> none;
        (void)call(main_fn, none);

        // Collect what it wrote. gl_Position and the varyings are globals, so
        // main's assignments landed there whatever depth they happened at.
        const auto publish = [&](const std::string & name) {
            if (const auto found = globals_.find(name); found != globals_.end()) {
                out_->outputs.emplace_back(name, found->second);
            }
        };
        if (m_->which == stage::vertex) {
            publish("gl_Position");
            publish("gl_PointSize");
            for (const interface_variable & v : m_->interface_) {
                if (v.store == storage::varying) { publish(v.name); }
            }
        } else {
            publish("gl_FragColor");
            publish("gl_FragDepth");
        }
        out_->ok = failure_.empty();
        out_->error = failure_;
    }

    // --- diagnostics
    void fail(std::string message) {
        if (failure_.empty()) { failure_ = std::move(message); }
    }
    [[nodiscard]] bool failed() const { return !failure_.empty(); }

    // --- values
    //
    // HOW WIDE A VALUE IS. type::components() cannot answer for a struct - it
    // knows the shape but not the members - so anything that measures storage
    // has to come through here, where the module's struct table is in reach.
    // Letting a struct report one float was a heap overflow the first time a
    // member was assigned, which asan caught and a reader would not have.
    [[nodiscard]] std::size_t width_of(const type & t) const {
        std::size_t one = 1;
        if (t.kind == base::struct_ && t.user >= 0 &&
            static_cast<std::size_t>(t.user) < m_->structs.size()) {
            one = 0;
            // Recursive, because a struct may hold a struct.
            for (const struct_type::member & each :
                 m_->structs[static_cast<std::size_t>(t.user)].members) {
                one += width_of(each.t);
            }
            one = std::max<std::size_t>(one, 1);
        } else {
            one = static_cast<std::size_t>(std::max(1, t.rows * t.cols));
        }
        return t.array > 0 ? one * static_cast<std::size_t>(t.array) : one;
    }

    [[nodiscard]] value zero(const type & t) const {
        value out;
        out.t = t;
        out.v.assign(width_of(t), 0.0f);
        return out;
    }

    // GLSL ES 1.00 has no implicit conversions. This one is LENIENT and converts
    // int to float and back, deliberately: real drivers accept `float x = 1;`
    // and rejecting a shader that works everywhere else would be a worse answer
    // than quietly widening. The conversion is exact in the direction that
    // matters and truncates in the other, which is what `int(x)` means anyway.
    [[nodiscard]] static value convert(value in, const type & want) {
        if (in.t.kind == want.kind || want.kind == base::struct_ || want.kind == base::void_) {
            in.t = want.array != 0 ? in.t : want;
            return in;
        }
        if (want.kind == base::i) {
            for (float & f : in.v) { f = std::trunc(f); }
        } else if (want.kind == base::b) {
            for (float & f : in.v) { f = f != 0.0f ? 1.0f : 0.0f; }
        }
        in.t.kind = want.kind;
        return in;
    }

    // --- scopes
    //
    // GLOBALS ARE NOT ON THE SCOPE STACK. A call saves and restores the stack,
    // so a globals map living on it would be COPIED into the callee and the
    // copy thrown away on return - which meant main's `gl_Position = ...` wrote
    // to something nobody read, and the shader produced no output while
    // reporting success. Keeping them beside the stack makes a write from any
    // depth land in one place.
    [[nodiscard]] value * lookup(const std::string & name) {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            if (const auto found = scope->find(name); found != scope->end()) {
                return &found->second;
            }
        }
        if (const auto found = globals_.find(name); found != globals_.end()) {
            return &found->second;
        }
        return nullptr;
    }

    // --- functions
    //
    // OVERLOADS ARE RESOLVED BY ARGUMENT SHAPE. p5's font shader declares three
    // `ifloor` - one taking a float, one an int, one a vec2 - so picking the
    // first by name would call the wrong one two times in three.
    [[nodiscard]] std::int32_t find_function(const std::string & name,
                                             const std::vector<value> & args) {
        const auto found = functions_.find(name);
        if (found == functions_.end()) { return -1; }
        std::int32_t best = -1;
        int best_score = -1;
        for (const std::int32_t which : found->second) {
            const node & fn = m_->at(which);
            if (fn.a < 0) { continue; } // a prototype has no body to run
            if (fn.kids.size() != args.size()) { continue; }
            int score = 0;
            bool usable = true;
            for (std::size_t i = 0; i < args.size(); ++i) {
                const type & want = m_->at(fn.kids[i]).t;
                const type & got = args[i].t;
                if (want.rows != got.rows || want.cols != got.cols) {
                    usable = false;
                    break;
                }
                // An exact base is better than one that needs converting, which
                // is how `ifloor(1.5)` picks the float overload over the int one.
                score += want.kind == got.kind ? 2 : 1;
            }
            if (usable && score > best_score) {
                best_score = score;
                best = which;
            }
        }
        return best;
    }

    [[nodiscard]] value call(std::int32_t which, std::vector<value> & args) {
        if (depth_ > 64) {
            fail("call nesting too deep - is a function calling itself?");
            return value::scalar(0);
        }
        const node & fn = m_->at(which);
        // A NEW SCOPE THAT CANNOT SEE THE CALLER'S LOCALS. GLSL has no closures;
        // a function sees its parameters and the globals, and nothing else. The
        // globals are reached through lookup rather than copied in - see there.
        auto saved = std::move(scopes_);
        scopes_.clear();
        scopes_.emplace_back();
        for (std::size_t i = 0; i < fn.kids.size() && i < args.size(); ++i) {
            const node & p = m_->at(fn.kids[i]);
            scopes_.back()[p.text] = convert(args[i], p.t);
        }

        ++depth_;
        returned_ = value::scalar(0);
        const flow how = execute_statement(fn.a);
        --depth_;

        // `out` and `inout` are copied BACK. GLSL passes by value, so a callee
        // that writes an out parameter has written a copy until this happens.
        for (std::size_t i = 0; i < fn.kids.size() && i < args.size(); ++i) {
            const node & p = m_->at(fn.kids[i]);
            if (p.dir == direction::in) { continue; }
            if (const value * updated = lookup(p.text)) { args[i] = *updated; }
        }
        scopes_ = std::move(saved);
        if (how == flow::discarded) { discarded_ = true; }
        return how == flow::returned ? returned_ : zero(fn.t);
    }

    // --- statements
    [[nodiscard]] flow execute_statement(std::int32_t which) {
        if (which < 0 || failed()) { return flow::normal; }
        const node & n = m_->at(which);
        switch (n.kind) {
        case nk::block: {
            scopes_.emplace_back();
            flow how = flow::normal;
            for (const std::int32_t s : n.kids) {
                how = execute_statement(s);
                if (how != flow::normal || failed()) { break; }
            }
            scopes_.pop_back();
            return how;
        }
        case nk::var_decl: {
            value made = n.a >= 0 ? evaluate(n.a) : zero(n.t);
            scopes_.back()[n.text] = convert(std::move(made), n.t);
            return flow::normal;
        }
        case nk::expr_stmt: (void)evaluate(n.a); return flow::normal;
        case nk::if_stmt:
            if (evaluate(n.a).truthy()) { return execute_statement(n.b); }
            return n.c >= 0 ? execute_statement(n.c) : flow::normal;
        case nk::for_stmt: {
            scopes_.emplace_back();
            (void)execute_statement(n.a);
            flow out = flow::normal;
            for (int guard = 0; guard < 1'000'000; ++guard) {
                if (n.b >= 0 && !evaluate(n.b).truthy()) { break; }
                const flow how = execute_statement(n.d);
                if (how == flow::broke) { break; }
                if (how == flow::returned || how == flow::discarded) {
                    out = how;
                    break;
                }
                if (failed()) { break; }
                if (n.c >= 0) { (void)evaluate(n.c); }
            }
            scopes_.pop_back();
            return out;
        }
        case nk::while_stmt: {
            for (int guard = 0; guard < 1'000'000; ++guard) {
                if (!evaluate(n.a).truthy() || failed()) { break; }
                const flow how = execute_statement(n.b);
                if (how == flow::broke) { break; }
                if (how == flow::returned || how == flow::discarded) { return how; }
            }
            return flow::normal;
        }
        case nk::do_stmt: {
            for (int guard = 0; guard < 1'000'000; ++guard) {
                const flow how = execute_statement(n.a);
                if (how == flow::broke) { break; }
                if (how == flow::returned || how == flow::discarded) { return how; }
                if (!evaluate(n.b).truthy() || failed()) { break; }
            }
            return flow::normal;
        }
        case nk::return_stmt:
            returned_ = n.a >= 0 ? evaluate(n.a) : value::scalar(0);
            return flow::returned;
        case nk::break_stmt: return flow::broke;
        case nk::continue_stmt: return flow::continued;
        case nk::discard_stmt: return flow::discarded;
        case nk::struct_def: return flow::normal;
        default: (void)evaluate(which); return flow::normal;
        }
    }

    // --- expressions
    [[nodiscard]] value evaluate(std::int32_t which) {
        if (which < 0 || failed()) { return value::scalar(0); }
        const node & n = m_->at(which);
        switch (n.kind) {
        case nk::literal: return literal(n);
        case nk::identifier: return read_name(n.text);
        case nk::field: return field(n);
        case nk::index: {
            const value base_value = evaluate(n.a);
            const int at = evaluate(n.b).i();
            return element(base_value, at);
        }
        case nk::call: return call_or_construct(n);
        case nk::unary: return unary(n);
        case nk::prefix:
        case nk::postfix: return step(n);
        case nk::binary: return binary(n);
        case nk::ternary: return evaluate(n.a).truthy() ? evaluate(n.b) : evaluate(n.c);
        case nk::assign: return assign(n);
        case nk::sequence: (void)evaluate(n.a); return evaluate(n.b);
        default: return value::scalar(0);
        }
    }

    // PARSED AT PARSE TIME, not here. This called strtof on every evaluation -
    // the same `0.5` re-read from its characters millions of times per draw,
    // 5.4% of the shader benchmark - and strtof respects LC_NUMERIC, so it was
    // a cross-platform determinism bug as well. See node::number in glsl.hpp.
    [[nodiscard]] static value literal(const node & n) {
        if (n.t.kind == base::b) { return value::boolean(n.integer != 0); }
        if (n.t.kind == base::i) { return value::integer(n.integer); }
        return value::scalar(n.number);
    }

    [[nodiscard]] value read_name(const std::string & name) {
        if (const value * local = lookup(name)) { return *local; }
        // Then the environment: uniforms, attributes, varyings, gl_FragCoord.
        if (env_->read) {
            if (const value * outside = env_->read(name)) { return *outside; }
        }
        fail("`" + name + "` is not declared");
        return value::scalar(0);
    }

    // `.xyz` on a vector, or `.member` on a struct. Told apart by the type,
    // which is why the parser did not have to decide.
    [[nodiscard]] value field(const node & n) {
        const value on = evaluate(n.a);
        if (on.t.kind == base::struct_ && on.t.user >= 0) { return member(on, n.text); }
        if (!is_swizzle(n.text)) {
            fail("`." + n.text + "` is not a field of " + spell(on.t));
            return value::scalar(0);
        }
        value out;
        out.t = shape(on.t.kind, static_cast<std::uint8_t>(n.text.size()));
        for (const char c : n.text) {
            const auto at = static_cast<std::size_t>(swizzle_index(c));
            if (at >= on.v.size()) {
                fail("`." + n.text + "` reaches past a " + spell(on.t));
                return value::scalar(0);
            }
            out.v.push_back(on.v[at]);
        }
        return out;
    }

    [[nodiscard]] value member(const value & on, const std::string & name) {
        const struct_type & def = m_->structs[static_cast<std::size_t>(on.t.user)];
        std::size_t offset = 0;
        for (const struct_type::member & field_of : def.members) {
            const std::size_t width = width_of(field_of.t);
            if (field_of.name == name) {
                value out;
                out.t = field_of.t;
                out.v.assign_range(on.v.begin() + static_cast<std::ptrdiff_t>(offset),
                                   on.v.begin() + static_cast<std::ptrdiff_t>(offset + width));
                return out;
            }
            offset += width;
        }
        fail("`." + name + "` is not a member of " + def.name);
        return value::scalar(0);
    }

    // `v[i]` - an array element, a matrix COLUMN, or a vector component. All
    // three are a range of the flat storage, which is why they share a function.
    [[nodiscard]] value element(const value & on, int at) {
        if (at < 0) {
            fail("negative index");
            return value::scalar(0);
        }
        const auto index = static_cast<std::size_t>(at);
        if (on.t.array != 0) {
            type each = on.t;
            each.array = 0;
            const std::size_t width = width_of(each);
            if ((index + 1) * width > on.v.size()) {
                fail("index " + std::to_string(at) + " is past the end of an array");
                return zero(each);
            }
            value out;
            out.t = each;
            out.v.assign_range(on.v.begin() + static_cast<std::ptrdiff_t>(index * width),
                               on.v.begin() + static_cast<std::ptrdiff_t>((index + 1) * width));
            return out;
        }
        if (on.t.is_matrix()) {
            // A COLUMN, because the storage is column-major and `m[i]` means the
            // i'th column in GLSL. Returning a row here is the transposition bug
            // that looks like a plausible wrong rotation.
            const auto rows = static_cast<std::size_t>(on.t.rows);
            if ((index + 1) * rows > on.v.size()) {
                fail("column " + std::to_string(at) + " is past the end of " + spell(on.t));
                return value::scalar(0);
            }
            value out;
            out.t = shape(on.t.kind, on.t.rows);
            out.v.assign_range(on.v.begin() + static_cast<std::ptrdiff_t>(index * rows),
                               on.v.begin() + static_cast<std::ptrdiff_t>((index + 1) * rows));
            return out;
        }
        if (index >= on.v.size()) {
            fail("index " + std::to_string(at) + " is past the end of " + spell(on.t));
            return value::scalar(0);
        }
        value out;
        out.t = shape(on.t.kind, 1);
        out.v.push_back(on.v[index]);
        return out;
    }

    // --- assignment
    //
    // The target is resolved to a POINTER and a set of component positions, so
    // `v.xz = w` writes two of four and `m[1].y = 3.0` writes one of sixteen
    // without either needing its own case.
    struct target {
        value * store = nullptr;
        std::vector<std::size_t> positions;
        type t;
    };

    [[nodiscard]] bool resolve(std::int32_t which, target & into) {
        const node & n = m_->at(which);
        if (n.kind == nk::identifier) {
            value * found = lookup(n.text);
            if (found == nullptr) {
                fail("cannot assign to `" + n.text + "` - it is not declared");
                return false;
            }
            into.store = found;
            into.t = found->t;
            into.positions.resize(found->v.size());
            for (std::size_t i = 0; i < found->v.size(); ++i) { into.positions[i] = i; }
            return true;
        }
        if (n.kind == nk::field) {
            if (!resolve(n.a, into)) { return false; }
            if (into.t.kind == base::struct_ && into.t.user >= 0) {
                const struct_type & def = m_->structs[static_cast<std::size_t>(into.t.user)];
                std::size_t offset = 0;
                for (const struct_type::member & field_of : def.members) {
                    const std::size_t width = width_of(field_of.t);
                    if (field_of.name == n.text) {
                        std::vector<std::size_t> narrowed;
                        for (std::size_t i = 0; i < width; ++i) {
                            narrowed.push_back(into.positions[offset + i]);
                        }
                        into.positions = std::move(narrowed);
                        into.t = field_of.t;
                        return true;
                    }
                    offset += width;
                }
                fail("`." + n.text + "` is not a member of " + def.name);
                return false;
            }
            if (!is_swizzle(n.text)) {
                fail("cannot assign to `." + n.text + "`");
                return false;
            }
            std::vector<std::size_t> narrowed;
            for (const char c : n.text) {
                const auto at = static_cast<std::size_t>(swizzle_index(c));
                if (at >= into.positions.size()) {
                    fail("`." + n.text + "` reaches past a " + spell(into.t));
                    return false;
                }
                narrowed.push_back(into.positions[at]);
            }
            into.positions = std::move(narrowed);
            into.t = shape(into.t.kind, static_cast<std::uint8_t>(n.text.size()));
            return true;
        }
        if (n.kind == nk::index) {
            if (!resolve(n.a, into)) { return false; }
            const int at = evaluate(n.b).i();
            if (at < 0) {
                fail("negative index in an assignment");
                return false;
            }
            type each = into.t;
            std::size_t width = 1;
            if (into.t.array != 0) {
                each.array = 0;
                width = width_of(each);
            } else if (into.t.is_matrix()) {
                each = shape(into.t.kind, into.t.rows);
                width = into.t.rows;
            } else {
                each = shape(into.t.kind, 1);
            }
            const auto start = static_cast<std::size_t>(at) * width;
            if (start + width > into.positions.size()) {
                fail("index " + std::to_string(at) + " is past the end in an assignment");
                return false;
            }
            std::vector<std::size_t> narrowed;
            for (std::size_t i = 0; i < width; ++i) {
                narrowed.push_back(into.positions[start + i]);
            }
            into.positions = std::move(narrowed);
            into.t = each;
            return true;
        }
        fail("that is not something a shader can assign to");
        return false;
    }

    [[nodiscard]] value assign(const node & n) {
        target where;
        if (!resolve(n.a, where)) { return value::scalar(0); }
        value right = evaluate(n.b);
        if (n.text != "=") {
            // `a += b` is `a = a + b`, evaluated once - the target has already
            // been resolved, so the left side is not walked twice.
            value current;
            current.t = where.t;
            for (const std::size_t at : where.positions) {
                current.v.push_back(where.store->v[at]);
            }
            right = arithmetic(n.text.substr(0, n.text.size() - 1), current, right);
        }
        right = convert(std::move(right), where.t);
        // A SCALAR SPREADS. `v.xyz = 1.0` sets all three, which is what GLSL's
        // assignment of a scalar to a vector means.
        for (std::size_t i = 0; i < where.positions.size(); ++i) {
            const float from =
                right.v.size() == 1 ? right.v[0] : (i < right.v.size() ? right.v[i] : 0.0f);
            where.store->v[where.positions[i]] = from;
        }
        value result;
        result.t = where.t;
        for (const std::size_t at : where.positions) { result.v.push_back(where.store->v[at]); }
        return result;
    }

    [[nodiscard]] value step(const node & n) {
        target where;
        if (!resolve(n.a, where)) { return value::scalar(0); }
        value before;
        before.t = where.t;
        for (const std::size_t at : where.positions) { before.v.push_back(where.store->v[at]); }
        const float by = n.text == "++" ? 1.0f : -1.0f;
        for (const std::size_t at : where.positions) { where.store->v[at] += by; }
        if (n.kind == nk::postfix) { return before; }
        value after = before;
        for (float & f : after.v) { f += by; }
        return after;
    }

    // --- operators
    [[nodiscard]] value unary(const node & n) {
        value on = evaluate(n.a);
        if (n.text == "-") {
            for (float & f : on.v) { f = -f; }
        } else if (n.text == "!") {
            on = value::boolean(!on.truthy());
        } else if (n.text == "~") {
            for (float & f : on.v) { f = static_cast<float>(~static_cast<int>(f)); }
        }
        return on;
    }

    [[nodiscard]] value binary(const node & n) {
        // SHORT-CIRCUIT, which matters: `a && f()` must not call f when a is
        // false, and a shader may rely on that to avoid an out-of-range fetch.
        if (n.text == "&&") {
            return value::boolean(evaluate(n.a).truthy() && evaluate(n.b).truthy());
        }
        if (n.text == "||") {
            return value::boolean(evaluate(n.a).truthy() || evaluate(n.b).truthy());
        }
        const value left = evaluate(n.a);
        const value right = evaluate(n.b);
        if (n.text == "^^") { return value::boolean(left.truthy() != right.truthy()); }
        return arithmetic(n.text, left, right);
    }

    [[nodiscard]] value arithmetic(std::string_view op, const value & l, const value & r) {
        if (op == "==" || op == "!=") {
            bool same = l.v.size() == r.v.size();
            for (std::size_t i = 0; same && i < l.v.size(); ++i) { same = l.v[i] == r.v[i]; }
            return value::boolean(op == "==" ? same : !same);
        }
        if (op == "<" || op == ">" || op == "<=" || op == ">=") {
            const float a = l.f();
            const float b = r.f();
            const bool yes = op == "<" ? a < b : op == ">" ? a > b : op == "<=" ? a <= b : a >= b;
            return value::boolean(yes);
        }
        // MATRIX PRODUCTS ARE NOT COMPONENTWISE. mat * vec, mat * mat and
        // vec * mat each have their own rule, and doing any of them elementwise
        // produces a picture rather than an error.
        if (op == "*" && l.t.is_matrix() && r.t.is_vector()) { return matrix_times_vector(l, r); }
        if (op == "*" && l.t.is_matrix() && r.t.is_matrix()) { return matrix_times_matrix(l, r); }
        if (op == "*" && l.t.is_vector() && r.t.is_matrix()) { return vector_times_matrix(l, r); }

        const bool integral = l.t.kind == base::i && r.t.kind == base::i;
        const std::size_t width = std::max(l.v.size(), r.v.size());
        value out;
        out.t = l.v.size() >= r.v.size() ? l.t : r.t;
        if (l.t.kind == base::f || r.t.kind == base::f) { out.t.kind = base::f; }
        out.v.resize(width);
        for (std::size_t i = 0; i < width; ++i) {
            const float a = l.v.size() == 1 ? l.v[0] : (i < l.v.size() ? l.v[i] : 0.0f);
            const float b = r.v.size() == 1 ? r.v[0] : (i < r.v.size() ? r.v[i] : 0.0f);
            float made = 0;
            if (op == "+") {
                made = a + b;
            } else if (op == "-") {
                made = a - b;
            } else if (op == "*") {
                made = a * b;
            } else if (op == "/") {
                // `int / int` TRUNCATES toward zero, and division by zero is a
                // defined nothing rather than a trap - a shader is a page's text
                // and must not be able to fault the process.
                if (b == 0.0f) {
                    made = 0.0f;
                } else if (integral) {
                    made = std::trunc(a / b);
                } else {
                    made = a / b;
                }
            } else if (op == "%") {
                made = b == 0.0f ? 0.0f : std::fmod(a, b);
            }
            out.v[i] = made;
        }
        return out;
    }

    // `m * v`: sum of m's COLUMNS scaled by v's components.
    [[nodiscard]] static value matrix_times_vector(const value & m, const value & v) {
        value out;
        out.t = shape(base::f, m.t.rows);
        out.v.assign(m.t.rows, 0.0f);
        for (std::size_t col = 0; col < m.t.cols; ++col) {
            const float scale = col < v.v.size() ? v.v[col] : 0.0f;
            for (std::size_t row = 0; row < m.t.rows; ++row) {
                out.v[row] += m.v[col * m.t.rows + row] * scale;
            }
        }
        return out;
    }

    // `v * m`: each component is v dotted with a COLUMN, which is the transpose
    // of the case above and not the same answer.
    [[nodiscard]] static value vector_times_matrix(const value & v, const value & m) {
        value out;
        out.t = shape(base::f, m.t.cols);
        out.v.assign(m.t.cols, 0.0f);
        for (std::size_t col = 0; col < m.t.cols; ++col) {
            float sum = 0;
            for (std::size_t row = 0; row < m.t.rows; ++row) {
                sum += m.v[col * m.t.rows + row] * (row < v.v.size() ? v.v[row] : 0.0f);
            }
            out.v[col] = sum;
        }
        return out;
    }

    [[nodiscard]] static value matrix_times_matrix(const value & a, const value & b) {
        value out;
        out.t = shape(base::f, a.t.rows, b.t.cols);
        out.v.assign(static_cast<std::size_t>(a.t.rows) * b.t.cols, 0.0f);
        for (std::size_t col = 0; col < b.t.cols; ++col) {
            for (std::size_t row = 0; row < a.t.rows; ++row) {
                float sum = 0;
                // SUMMED IN A FIXED ORDER, k ascending. Every float result here
                // has to be bit-identical on every platform because the goldens
                // are byte-compared, so the order is a specification and not the
                // compiler's choice.
                for (std::size_t k = 0; k < a.t.cols; ++k) {
                    sum += a.v[k * a.t.rows + row] * b.v[col * b.t.rows + k];
                }
                out.v[col * a.t.rows + row] = sum;
            }
        }
        return out;
    }

    // --- calls and constructors
    [[nodiscard]] value call_or_construct(const node & n);
    [[nodiscard]] value construct(const type & t, const std::vector<value> & args);
    [[nodiscard]] bool builtin(const std::string & name, std::vector<value> & args, value & out);

    const shader * m_ = nullptr;
    const environment * env_ = nullptr;
    execution * out_ = nullptr;
    by_name<value> globals_;
    std::vector<by_name<value>> scopes_;
    by_name<std::vector<std::int32_t>> functions_;
    value returned_;
    std::string failure_;
    int depth_ = 0;
    bool discarded_ = false;

public:
    [[nodiscard]] bool discarded() const { return discarded_; }
};

// --- the built-in library --------------------------------------------------
//
// GLSL's built-ins are almost all COMPONENTWISE: `sin(v)` is the sine of each
// component, and `max(v, 0.0)` broadcasts the scalar. Writing that rule once and
// listing the one-argument and two-argument functions as plain lambdas keeps the
// table short and keeps every one of them consistent.
//
// The ones that are NOT componentwise - length, dot, cross, normalize and the
// matrix helpers - are written out below the table, because each has its own
// shape and a wrong shape is the kind of thing that still produces a picture.

using unary_fn = float (*)(float);
using binary_fn = float (*)(float, float);

[[nodiscard]] value map1(const value & a, unary_fn f) {
    value out = a;
    out.t.kind = base::f;
    for (float & each : out.v) { each = f(each); }
    return out;
}

// The wider of the two shapes wins, and a scalar broadcasts into it - which is
// what makes `max(v, 0.0)` and `pow(v, w)` one function rather than two.
[[nodiscard]] value map2(const value & a, const value & b, binary_fn f) {
    value out;
    out.t = a.v.size() >= b.v.size() ? a.t : b.t;
    out.t.kind = base::f;
    const std::size_t width = std::max(a.v.size(), b.v.size());
    out.v.resize(width);
    for (std::size_t i = 0; i < width; ++i) {
        const float x = a.v.size() == 1 ? a.v[0] : (i < a.v.size() ? a.v[i] : 0.0f);
        const float y = b.v.size() == 1 ? b.v[0] : (i < b.v.size() ? b.v[i] : 0.0f);
        out.v[i] = f(x, y);
    }
    return out;
}

[[nodiscard]] value map3(const value & a, const value & b, const value & c,
                         float (*f)(float, float, float)) {
    value out;
    out.t = a.v.size() >= b.v.size() ? a.t : b.t;
    if (c.v.size() > out.v.size()) { out.t = c.t; }
    out.t.kind = base::f;
    const std::size_t width = std::max({a.v.size(), b.v.size(), c.v.size()});
    out.v.resize(width);
    const auto pick = [](const value & v, std::size_t i) {
        return v.v.size() == 1 ? v.v[0] : (i < v.v.size() ? v.v[i] : 0.0f);
    };
    for (std::size_t i = 0; i < width; ++i) { out.v[i] = f(pick(a, i), pick(b, i), pick(c, i)); }
    return out;
}

// SUMMED IN A FIXED ORDER, ascending. Every float result has to be
// bit-identical on every platform because the goldens are byte-compared, so the
// order of a reduction is a specification rather than the compiler's choice -
// which is also why OpenMP reductions are banned here (docs/webgl-plan.md).
[[nodiscard]] float dot_product(const value & a, const value & b) {
    float sum = 0;
    const std::size_t width = std::min(a.v.size(), b.v.size());
    for (std::size_t i = 0; i < width; ++i) { sum += a.v[i] * b.v[i]; }
    return sum;
}

[[nodiscard]] float length_of(const value & a) {
    return std::sqrt(dot_product(a, a));
}

[[nodiscard]] value normalize_of(const value & a) {
    const float len = length_of(a);
    value out = a;
    out.t.kind = base::f;
    // A ZERO VECTOR NORMALISES TO ZERO rather than to NaN. A NaN here spreads
    // through the rest of the shader and comes out as a black or missing pixel
    // with nothing to say why.
    if (len == 0.0f) { return out; }
    for (float & each : out.v) { each /= len; }
    return out;
}

bool interpreter::builtin(const std::string & name, std::vector<value> & args, value & out) {
    const auto arg = [&](std::size_t i) -> const value & {
        static const value none = value::scalar(0);
        return i < args.size() ? args[i] : none;
    };
    const std::size_t count = args.size();

    // --- one argument, componentwise
    static const std::unordered_map<std::string, unary_fn> ones{
        {"radians", [](float x) { return x * std::numbers::pi_v<float> / 180.0f; }},
        {"degrees", [](float x) { return x * 180.0f / std::numbers::pi_v<float>; }},
        {"sin", [](float x) { return std::sin(x); }},
        {"cos", [](float x) { return std::cos(x); }},
        {"tan", [](float x) { return std::tan(x); }},
        {"asin", [](float x) { return std::asin(std::clamp(x, -1.0f, 1.0f)); }},
        {"acos", [](float x) { return std::acos(std::clamp(x, -1.0f, 1.0f)); }},
        {"atan", [](float x) { return std::atan(x); }},
        {"exp", [](float x) { return std::exp(x); }},
        {"log", [](float x) { return x > 0.0f ? std::log(x) : 0.0f; }},
        {"exp2", [](float x) { return std::exp2(x); }},
        {"log2", [](float x) { return x > 0.0f ? std::log2(x) : 0.0f; }},
        // A NEGATIVE SQRT IS ZERO, not NaN - see normalize above for why.
        {"sqrt", [](float x) { return x > 0.0f ? std::sqrt(x) : 0.0f; }},
        {"inversesqrt", [](float x) { return x > 0.0f ? 1.0f / std::sqrt(x) : 0.0f; }},
        {"abs", [](float x) { return std::fabs(x); }},
        {"sign", [](float x) { return x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f); }},
        {"floor", [](float x) { return std::floor(x); }},
        {"ceil", [](float x) { return std::ceil(x); }},
        // `fract` is x - floor(x), which for a negative x is NOT the fractional
        // part C would give: fract(-0.25) is 0.75.
        {"fract", [](float x) { return x - std::floor(x); }},
    };
    if (const auto found = ones.find(name); found != ones.end() && count >= 1) {
        out = map1(arg(0), found->second);
        return true;
    }

    // --- two arguments, componentwise
    static const std::unordered_map<std::string, binary_fn> twos{
        {"pow", [](float x, float y) { return std::pow(x, y); }},
        {"mod", [](float x, float y) { return y == 0.0f ? 0.0f : x - y * std::floor(x / y); }},
        {"min", [](float x, float y) { return std::min(x, y); }},
        {"max", [](float x, float y) { return std::max(x, y); }},
        {"step", [](float edge, float x) { return x < edge ? 0.0f : 1.0f; }},
        {"atan", [](float y, float x) { return std::atan2(y, x); }},
    };
    if (count == 2) {
        if (const auto found = twos.find(name); found != twos.end()) {
            // `atan(y, x)` is the two-argument form and takes its arguments in
            // that order, which is the opposite of what reading `atan2` suggests.
            out = map2(arg(0), arg(1), found->second);
            return true;
        }
    }

    if (name == "clamp" && count == 3) {
        out = map3(arg(0), arg(1), arg(2),
                   [](float x, float lo, float hi) { return std::clamp(x, lo, hi); });
        return true;
    }
    if (name == "mix" && count == 3) {
        out = map3(arg(0), arg(1), arg(2),
                   [](float x, float y, float a) { return x * (1.0f - a) + y * a; });
        return true;
    }
    if (name == "smoothstep" && count == 3) {
        out = map3(arg(0), arg(1), arg(2), [](float e0, float e1, float x) {
            if (e1 == e0) { return x < e0 ? 0.0f : 1.0f; }
            const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        });
        return true;
    }

    // --- the ones with their own shape
    if (name == "length" && count == 1) {
        out = value::scalar(length_of(arg(0)));
        return true;
    }
    if (name == "distance" && count == 2) {
        value difference = arg(0);
        for (std::size_t i = 0; i < difference.v.size() && i < arg(1).v.size(); ++i) {
            difference.v[i] -= arg(1).v[i];
        }
        out = value::scalar(length_of(difference));
        return true;
    }
    if (name == "dot" && count == 2) {
        out = value::scalar(dot_product(arg(0), arg(1)));
        return true;
    }
    if (name == "cross" && count == 2) {
        const value & a = arg(0);
        const value & b = arg(1);
        if (a.v.size() < 3 || b.v.size() < 3) { return false; }
        out = value::vector({a.v[1] * b.v[2] - a.v[2] * b.v[1], a.v[2] * b.v[0] - a.v[0] * b.v[2],
                             a.v[0] * b.v[1] - a.v[1] * b.v[0]});
        return true;
    }
    if (name == "normalize" && count == 1) {
        out = normalize_of(arg(0));
        return true;
    }
    if (name == "faceforward" && count == 3) {
        out = arg(0);
        if (dot_product(arg(2), arg(1)) >= 0.0f) {
            for (float & each : out.v) { each = -each; }
        }
        return true;
    }
    if (name == "reflect" && count == 2) {
        const float twice = 2.0f * dot_product(arg(1), arg(0));
        out = arg(0);
        for (std::size_t i = 0; i < out.v.size() && i < arg(1).v.size(); ++i) {
            out.v[i] -= twice * arg(1).v[i];
        }
        return true;
    }
    if (name == "refract" && count == 3) {
        const value & incident = arg(0);
        const value & normal = arg(1);
        const float eta = arg(2).f();
        const float d = dot_product(normal, incident);
        const float k = 1.0f - eta * eta * (1.0f - d * d);
        out = incident;
        if (k < 0.0f) {
            // TOTAL INTERNAL REFLECTION is a zero vector, per spec - not a NaN
            // from the square root of a negative.
            for (float & each : out.v) { each = 0.0f; }
            return true;
        }
        const float scale = eta * d + std::sqrt(k);
        for (std::size_t i = 0; i < out.v.size() && i < normal.v.size(); ++i) {
            out.v[i] = eta * incident.v[i] - scale * normal.v[i];
        }
        return true;
    }
    if (name == "matrixCompMult" && count == 2) {
        out = arg(0);
        for (std::size_t i = 0; i < out.v.size() && i < arg(1).v.size(); ++i) {
            out.v[i] *= arg(1).v[i];
        }
        return true;
    }

    // --- the comparison family, which returns a bvec rather than a bool
    static const std::unordered_map<std::string, binary_fn> compares{
        {"lessThan", [](float x, float y) { return x < y ? 1.0f : 0.0f; }},
        {"lessThanEqual", [](float x, float y) { return x <= y ? 1.0f : 0.0f; }},
        {"greaterThan", [](float x, float y) { return x > y ? 1.0f : 0.0f; }},
        {"greaterThanEqual", [](float x, float y) { return x >= y ? 1.0f : 0.0f; }},
        {"equal", [](float x, float y) { return x == y ? 1.0f : 0.0f; }},
        {"notEqual", [](float x, float y) { return x != y ? 1.0f : 0.0f; }},
    };
    if (count == 2) {
        if (const auto found = compares.find(name); found != compares.end()) {
            out = map2(arg(0), arg(1), found->second);
            out.t.kind = base::b;
            return true;
        }
    }
    if ((name == "any" || name == "all") && count == 1) {
        const bool wants_all = name == "all";
        bool result = wants_all;
        for (const float each : arg(0).v) {
            const bool set = each != 0.0f;
            result = wants_all ? (result && set) : (result || set);
        }
        out = value::boolean(result);
        return true;
    }
    if (name == "not" && count == 1) {
        out = arg(0);
        out.t.kind = base::b;
        for (float & each : out.v) { each = each == 0.0f ? 1.0f : 0.0f; }
        return true;
    }

    // --- texture fetch
    //
    // The sampler's VALUE is its unit number - that is what `uniform1i` uploads
    // and what the context binds a texture to. textureCube samples one face
    // rather than picking by direction, which is written down in glsl.hpp as a
    // known limitation rather than left to be discovered.
    if ((name == "texture2D" || name == "texture" || name == "textureCube" ||
         name == "texture2DProj") &&
        count >= 2) {
        if (!env_->sample) {
            out = value::vector({0, 0, 0, 1});
            return true;
        }
        const value & coord = arg(1);
        float s = coord.f(0);
        float t = coord.f(1);
        if (name == "texture2DProj" && coord.v.size() >= 3) {
            const float w = coord.v[coord.v.size() - 1];
            if (w != 0.0f) {
                s /= w;
                t /= w;
            }
        }
        out = env_->sample(arg(0).i(), s, t);
        return true;
    }

    // --- derivatives, which a scanline rasteriser cannot compute
    //
    // ZERO, and said so in glsl.hpp. p5's font shader asks for the extension and
    // then guards its use with #ifdef, so returning zero here is what a driver
    // without the extension does. Stage three's quad shading makes these real.
    if ((name == "dFdx" || name == "dFdy" || name == "fwidth") && count == 1) {
        out = arg(0);
        for (float & each : out.v) { each = 0.0f; }
        return true;
    }

    return false;
}

// A type name used as a call is a constructor: `vec3(1.0)`, `mat4(m)`.
[[nodiscard]] bool constructor_type(std::string_view word, const shader & m, type & into) {
    struct entry {
        std::string_view name;
        base kind;
        std::uint8_t rows;
        std::uint8_t cols;
    };
    static constexpr entry table[] = {
        {"float", base::f, 1, 1}, {"int", base::i, 1, 1},   {"bool", base::b, 1, 1},
        {"vec2", base::f, 2, 1},  {"vec3", base::f, 3, 1},  {"vec4", base::f, 4, 1},
        {"ivec2", base::i, 2, 1}, {"ivec3", base::i, 3, 1}, {"ivec4", base::i, 4, 1},
        {"bvec2", base::b, 2, 1}, {"bvec3", base::b, 3, 1}, {"bvec4", base::b, 4, 1},
        {"mat2", base::f, 2, 2},  {"mat3", base::f, 3, 3},  {"mat4", base::f, 4, 4},
    };
    for (const entry & known : table) {
        if (known.name == word) {
            into = type{known.kind, known.rows, known.cols, -1, 0};
            return true;
        }
    }
    for (std::size_t i = 0; i < m.structs.size(); ++i) {
        if (m.structs[i].name == word) {
            into = type{base::struct_, 1, 1, static_cast<std::int32_t>(i), 0};
            return true;
        }
    }
    return false;
}

value interpreter::construct(const type & t, const std::vector<value> & args) {
    value out;
    out.t = t;
    const std::size_t want = width_of(t);

    // A STRUCT takes its members in order.
    if (t.kind == base::struct_) {
        for (const value & each : args) { out.v.append(each.v.begin(), each.v.end()); }
        out.v.resize(want, 0.0f);
        return out;
    }

    // `mat4(1.0)` is the IDENTITY scaled, not sixteen ones - the single most
    // surprising constructor in the language, and getting it wrong makes every
    // transform a uniform blur.
    if (t.is_matrix() && args.size() == 1 && args.front().t.is_scalar()) {
        out.v.assign(want, 0.0f);
        const float diagonal = args.front().f();
        for (std::size_t i = 0; i < t.cols && i < t.rows; ++i) { out.v[i * t.rows + i] = diagonal; }
        return out;
    }
    // `mat3(m4)` takes the top-left corner; `mat4(m3)` fills the rest with the
    // identity.
    if (t.is_matrix() && args.size() == 1 && args.front().t.is_matrix()) {
        const value & from = args.front();
        out.v.assign(want, 0.0f);
        for (std::size_t i = 0; i < t.cols && i < t.rows; ++i) { out.v[i * t.rows + i] = 1.0f; }
        for (std::size_t col = 0; col < t.cols && col < from.t.cols; ++col) {
            for (std::size_t row = 0; row < t.rows && row < from.t.rows; ++row) {
                out.v[col * t.rows + row] = from.v[col * from.t.rows + row];
            }
        }
        return out;
    }

    // Everything else CONCATENATES its arguments' components, and a single
    // scalar SPREADS: `vec3(1.0)` is three ones, `vec4(v3, 1.0)` is four.
    for (const value & each : args) {
        for (const float f : each.v) { out.v.push_back(f); }
    }
    if (out.v.size() == 1 && want > 1) { out.v.assign(want, out.v.front()); }
    out.v.resize(want, 0.0f);
    if (t.kind == base::i) {
        for (float & f : out.v) { f = std::trunc(f); }
    } else if (t.kind == base::b) {
        for (float & f : out.v) { f = f != 0.0f ? 1.0f : 0.0f; }
    }
    return out;
}

value interpreter::call_or_construct(const node & n) {
    std::vector<value> args;
    args.reserve(n.kids.size());
    for (const std::int32_t each : n.kids) { args.push_back(evaluate(each)); }
    if (failed()) { return value::scalar(0); }

    type made;
    if (constructor_type(n.text, *m_, made)) { return construct(made, args); }

    // A user function beats a built-in of the same name, which is what lets a
    // shader define its own `noise` without losing to one it never asked for.
    if (const std::int32_t which = find_function(n.text, args); which >= 0) {
        value out = call(which, args);
        // `out` parameters were copied back into `args`; write them through to
        // whatever the caller passed, when that was something assignable.
        for (std::size_t i = 0; i < n.kids.size() && i < args.size(); ++i) {
            const node & p = m_->at(m_->at(which).kids[i]);
            if (p.dir == direction::in) { continue; }
            target where;
            if (resolve(n.kids[i], where)) {
                for (std::size_t c = 0; c < where.positions.size() && c < args[i].v.size(); ++c) {
                    where.store->v[where.positions[c]] = args[i].v[c];
                }
            }
        }
        return out;
    }

    value out;
    if (builtin(n.text, args, out)) { return out; }
    fail("no function `" + n.text + "` takes those arguments");
    return value::scalar(0);
}

} // namespace

const value * execution::find(std::string_view name) const {
    for (const auto & [key, held] : outputs) {
        if (key == name) { return &held; }
    }
    return nullptr;
}

// --- the prepared program --------------------------------------------------

struct program::impl {
    const shader * m = nullptr;
    prepared_state ready;
};

program::program(const shader & m) : impl_(std::make_unique<impl>()) {
    impl_->m = &m;
    if (!m.ok) {
        impl_->ready.error = "the shader did not compile";
        return;
    }
    // A throwaway execution: preparing evaluates global initialisers, and those
    // can fail the same way anything else can.
    execution scratch;
    environment none;
    interpreter build{m, none, scratch};
    impl_->ready = build.prepare();
}

program::~program() = default;
program::program(program &&) noexcept = default;
program & program::operator=(program &&) noexcept = default;

bool program::ok() const noexcept {
    return impl_ != nullptr && impl_->ready.error.empty();
}

const std::string & program::error() const noexcept {
    static const std::string none;
    return impl_ == nullptr ? none : impl_->ready.error;
}

execution program::run(const environment & env) const {
    execution out;
    if (impl_ == nullptr || impl_->m == nullptr) {
        out.error = "no program";
        return out;
    }
    if (!impl_->ready.error.empty()) {
        out.error = impl_->ready.error;
        return out;
    }
    interpreter run{*impl_->m, env, out};
    run.run_prepared(impl_->ready);
    out.discarded = run.discarded();
    return out;
}

execution execute(const shader & m, const environment & env) {
    execution out;
    if (!m.ok) {
        out.error = "the shader did not compile";
        return out;
    }
    interpreter run{m, env, out};
    run.run();
    out.discarded = run.discarded();
    return out;
}

} // namespace ctbrowser::raster::glsl
