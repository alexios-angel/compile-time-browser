#include <ctbrowser/raster/spirv.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

// GLSL ES to SPIR-V. See spirv.hpp for why this exists and what it covers.
//
// SPIR-V IS SSA, AND THAT IS THE WHOLE SHAPE OF THIS FILE. Every value is a
// numbered result produced exactly once, so emitting an expression is a
// post-order walk that returns an id, and there is no register allocation to do.
// The awkward part is not the instructions - it is that types, constants and
// variable declarations must all appear BEFORE the code that uses them, in a
// module whose sections are ordered by the specification. So this builds the
// sections separately and concatenates them at the end, rather than trying to
// emit in one pass.
//
// The layout, in the order the spec demands:
//
//   header - capabilities - memory model - entry point - execution mode
//   - names(skipped) - decorations - types/constants/global variables
//   - function definitions

namespace ctbrowser::raster::spirv {
namespace {

// The opcodes used. Named rather than pasted, because a wrong number here is
// rejected by the driver with no clue which instruction it was.
enum op : std::uint32_t {
    op_name = 5,
    op_member_name = 6,
    op_extension = 10,
    op_ext_inst_import = 11,
    op_ext_inst = 12,
    op_memory_model = 14,
    op_entry_point = 15,
    op_execution_mode = 16,
    op_capability = 17,
    op_type_void = 19,
    op_type_bool = 20,
    op_type_int = 21,
    op_type_float = 22,
    op_type_vector = 23,
    op_type_matrix = 24,
    op_type_pointer = 32,
    op_type_function = 33,
    op_constant = 43,
    op_constant_composite = 44,
    op_function = 54,
    op_function_end = 56,
    op_variable = 59,
    op_load = 61,
    op_store = 62,
    op_access_chain = 65,
    op_decorate = 71,
    op_member_decorate = 72,
    op_vector_shuffle = 79,
    op_composite_construct = 80,
    op_composite_extract = 81,
    op_f_negate = 127,
    op_f_add = 129,
    op_f_sub = 131,
    op_f_mul = 133,
    op_f_div = 136,
    op_vector_times_scalar = 142,
    op_matrix_times_vector = 145,
    op_matrix_times_matrix = 146,
    op_f_ord_equal = 180,
    op_f_ord_not_equal = 182,
    op_f_ord_less_than = 184,
    op_f_ord_greater_than = 186,
    op_f_ord_less_than_equal = 188,
    op_f_ord_greater_than_equal = 190,
    op_label = 248,
    op_branch = 249,
    op_branch_conditional = 250,
    op_return = 253,
};

// GLSL.std.450 extended instructions, for the built-ins that are not core.
enum ext : std::uint32_t {
    glsl_fabs = 4,
    glsl_floor = 8,
    glsl_ceil = 9,
    glsl_fract = 10,
    glsl_sin = 13,
    glsl_cos = 14,
    glsl_tan = 15,
    glsl_pow = 26,
    glsl_exp = 27,
    glsl_log = 28,
    glsl_sqrt = 31,
    glsl_fmin = 37,
    glsl_fmax = 40,
    glsl_fclamp = 43,
    glsl_fmix = 46,
    glsl_step = 48,
    glsl_smoothstep = 49,
    glsl_cross = 68,
    glsl_normalize = 69,
    glsl_reflect = 71,
    glsl_length = 66,
    glsl_distance = 67,
};

class emitter {
public:
    explicit emitter(const glsl::shader & m) : m_(&m) {}

    [[nodiscard]] module_binary run() {
        module_binary out;
        // Only the two shapes a WebGL pipeline has.
        entry_ = next_id();

        void_type_ = type_void();
        float_type_ = type_float();
        function_type_ = declare_function_type();

        collect_interface();
        if (failed()) {
            out.error = failure_;
            return out;
        }
        emit_main();
        if (failed()) {
            out.error = failure_;
            return out;
        }

        // Assembled in the order the specification demands. Emitting in one pass
        // is impossible: a type has to precede its first use, and the first use
        // is discovered while walking the body.
        std::vector<std::uint32_t> words;
        words.push_back(magic);
        words.push_back(0x00010000); // SPIR-V 1.0, which every Vulkan 1.0 driver takes
        words.push_back(0);          // generator: unregistered
        words.push_back(next_ + 1);  // the id bound - one past the largest used
        words.push_back(0);          // reserved

        append(words, {op_capability, 1 /* Shader */});
        // The extended instruction set, imported even when unused - it is two
        // words and it keeps the import id stable.
        std::vector<std::uint32_t> import{op_ext_inst_import, ext_set_};
        push_string(import, "GLSL.std.450");
        append_raw(words, import);
        append(words, {op_memory_model, 0 /* Logical */, 1 /* GLSL450 */});

        std::vector<std::uint32_t> entry{op_entry_point, m_->which == glsl::stage::vertex ? 0u : 4u,
                                         entry_};
        push_string(entry, "main");
        for (const std::uint32_t id : interface_ids_) { entry.push_back(id); }
        append_raw(words, entry);
        if (m_->which == glsl::stage::fragment) {
            // OriginUpperLeft, which is what Vulkan requires and what a bitmap's
            // row 0 already means here.
            append(words, {op_execution_mode, entry_, 7});
        }

        words.insert(words.end(), decorations_.begin(), decorations_.end());
        words.insert(words.end(), types_.begin(), types_.end());
        words.insert(words.end(), code_.begin(), code_.end());

        // Every instruction's first word carries its length; that was filled in
        // as each was appended, so nothing is patched here.
        out.words = std::move(words);
        out.ok = true;
        return out;
    }

private:
    void fail(std::string message) {
        if (failure_.empty()) { failure_ = std::move(message); }
    }
    [[nodiscard]] bool failed() const { return !failure_.empty(); }
    [[nodiscard]] std::uint32_t next_id() { return ++next_; }

    // One instruction: the word count goes in the high half of the first word.
    static void append(std::vector<std::uint32_t> & into, std::vector<std::uint32_t> parts) {
        parts[0] |= static_cast<std::uint32_t>(parts.size()) << 16;
        into.insert(into.end(), parts.begin(), parts.end());
    }
    static void append_raw(std::vector<std::uint32_t> & into, std::vector<std::uint32_t> parts) {
        append(into, std::move(parts));
    }
    // A SPIR-V string is UTF-8 packed into words, NUL-terminated, and PADDED to a
    // word boundary - so "main" is two words, not one. Getting the padding wrong
    // shifts every following word and the driver rejects the module.
    static void push_string(std::vector<std::uint32_t> & into, std::string_view text) {
        std::uint32_t word = 0;
        int at = 0;
        for (const char c : text) {
            word |= static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << (8 * at);
            if (++at == 4) {
                into.push_back(word);
                word = 0;
                at = 0;
            }
        }
        into.push_back(word); // the terminating NUL is the remaining zero bytes
    }

    // --- types, cached so each is declared exactly once
    [[nodiscard]] std::uint32_t type_void() {
        if (void_type_ != 0) { return void_type_; }
        const std::uint32_t id = next_id();
        append(types_, {op_type_void, id});
        return id;
    }
    [[nodiscard]] std::uint32_t type_float() {
        if (float_type_ != 0) { return float_type_; }
        const std::uint32_t id = next_id();
        append(types_, {op_type_float, id, 32});
        return id;
    }
    [[nodiscard]] std::uint32_t type_vector(std::uint32_t components) {
        if (components <= 1) { return float_type_; }
        auto & cached = vectors_[components];
        if (cached != 0) { return cached; }
        cached = next_id();
        append(types_, {op_type_vector, cached, float_type_, components});
        return cached;
    }
    [[nodiscard]] std::uint32_t type_pointer(std::uint32_t storage_class, std::uint32_t pointee) {
        const std::uint64_t key = (static_cast<std::uint64_t>(storage_class) << 32) | pointee;
        auto & cached = pointers_[key];
        if (cached != 0) { return cached; }
        cached = next_id();
        append(types_, {op_type_pointer, cached, storage_class, pointee});
        return cached;
    }
    [[nodiscard]] std::uint32_t declare_function_type() {
        const std::uint32_t id = next_id();
        append(types_, {op_type_function, id, void_type_});
        return id;
    }
    [[nodiscard]] std::uint32_t constant(float f) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &f, sizeof(bits));
        auto & cached = constants_[bits];
        if (cached != 0) { return cached; }
        cached = next_id();
        append(types_, {op_constant, float_type_, cached, bits});
        return cached;
    }

    [[nodiscard]] std::uint32_t type_of(const glsl::type & t) {
        if (t.is_matrix()) {
            const std::uint32_t column = type_vector(t.rows);
            auto & cached = matrices_[t.cols];
            if (cached == 0) {
                cached = next_id();
                append(types_, {op_type_matrix, cached, column, t.cols});
            }
            return cached;
        }
        return type_vector(t.rows);
    }

    // --- the interface: attributes in, varyings across, uniforms, the output
    void collect_interface() {
        const bool vertex = m_->which == glsl::stage::vertex;
        std::uint32_t next_location = 0;
        for (const glsl::interface_variable & v : m_->interface_) {
            if (v.t.is_sampler() || v.t.array != 0 || v.t.kind == glsl::base::struct_) {
                fail("this back end cannot express `" + v.name + "` (" + glsl::spell(v.t) + ")");
                return;
            }
            std::uint32_t storage_class = 0;
            switch (v.store) {
            case glsl::storage::attribute:
                if (!vertex) {
                    fail("an attribute in a fragment shader");
                    return;
                }
                storage_class = 1; // Input
                break;
            case glsl::storage::varying: storage_class = vertex ? 3u : 1u; break; // Output : Input
            case glsl::storage::uniform: storage_class = 2; break;                // UniformConstant
            default: continue;
            }
            const std::uint32_t pointee = type_of(v.t);
            const std::uint32_t pointer = type_pointer(storage_class, pointee);
            const std::uint32_t id = next_id();
            append(types_, {op_variable, pointer, id, storage_class});
            // A LOCATION PER VARIABLE, in declaration order. The vertex and
            // fragment stages must agree on the numbering for a varying or the
            // driver links them to the wrong slots - which is why both walk the
            // same list in the same order.
            if (v.store != glsl::storage::uniform) {
                append(decorations_, {op_decorate, id, 30 /* Location */, next_location++});
            } else {
                // A uniform needs a binding; one descriptor set, one binding each.
                append(decorations_, {op_decorate, id, 34 /* DescriptorSet */, 0});
                append(decorations_, {op_decorate, id, 33 /* Binding */, uniform_binding_++});
            }
            variables_[v.name] = {id, pointee, v.t};
            interface_ids_.push_back(id);
        }

        // The built-in the stage writes.
        if (vertex) {
            const std::uint32_t vec4 = type_vector(4);
            const std::uint32_t pointer = type_pointer(3 /* Output */, vec4);
            position_ = next_id();
            append(types_, {op_variable, pointer, position_, 3});
            append(decorations_, {op_decorate, position_, 11 /* BuiltIn */, 0 /* Position */});
            interface_ids_.push_back(position_);
            variables_["gl_Position"] = {position_, vec4, glsl::type{glsl::base::f, 4, 1, -1, 0}};
        } else {
            const std::uint32_t vec4 = type_vector(4);
            const std::uint32_t pointer = type_pointer(3 /* Output */, vec4);
            position_ = next_id();
            append(types_, {op_variable, pointer, position_, 3});
            append(decorations_, {op_decorate, position_, 30 /* Location */, 0});
            interface_ids_.push_back(position_);
            variables_["gl_FragColor"] = {position_, vec4, glsl::type{glsl::base::f, 4, 1, -1, 0}};
        }
    }

    // --- the body
    void emit_main() {
        std::int32_t main_fn = -1;
        for (const std::int32_t which : m_->declarations) {
            const glsl::node & n = m_->at(which);
            if (n.kind == glsl::nk::function && n.text == "main" && n.a >= 0) {
                main_fn = which;
            } else if (n.kind == glsl::nk::function && n.a >= 0) {
                fail("this back end cannot express the function `" + n.text + "`");
                return;
            }
        }
        if (main_fn < 0) {
            fail("no main()");
            return;
        }
        append(code_, {op_function, void_type_, entry_, 0 /* None */, function_type_});
        append(code_, {op_label, next_id()});
        statement(m_->at(main_fn).a);
        append(code_, {op_return});
        append(code_, {op_function_end});
    }

    void statement(std::int32_t which) {
        if (which < 0 || failed()) { return; }
        const glsl::node & n = m_->at(which);
        switch (n.kind) {
        case glsl::nk::block:
            for (const std::int32_t s : n.kids) { statement(s); }
            return;
        case glsl::nk::var_decl: {
            // A LOCAL IS AN SSA VALUE HERE, not a variable. Nothing in this
            // subset writes a local twice from different branches, so binding the
            // name to the id of its initialiser is correct and avoids a function-
            // scope OpVariable and a load per read.
            const std::uint32_t id = n.a >= 0 ? expression(n.a) : constant(0.0f);
            locals_[n.text] = {id, type_of(n.t), n.t};
            return;
        }
        case glsl::nk::expr_stmt: (void)expression(n.a); return;
        case glsl::nk::assign: (void)expression(which); return;
        default: fail("this back end cannot express that statement"); return;
        }
    }

    struct known {
        std::uint32_t id = 0;
        std::uint32_t type = 0;
        glsl::type t;
    };

    // Evaluate an expression, returning the id of its VALUE.
    [[nodiscard]] std::uint32_t expression(std::int32_t which) {
        if (which < 0 || failed()) { return constant(0.0f); }
        const glsl::node & n = m_->at(which);
        switch (n.kind) {
        case glsl::nk::literal: return constant(n.number);
        case glsl::nk::identifier: return load(n.text);
        case glsl::nk::assign: return assign(n);
        case glsl::nk::field: return swizzle(n);
        case glsl::nk::binary: return binary(n);
        case glsl::nk::unary: {
            const std::uint32_t on = expression(n.a);
            if (n.text == "-") {
                const std::uint32_t id = next_id();
                append(code_, {op_f_negate, type_of_id(on), id, on});
                remember_type(id, type_of_id(on), shape_of(on));
                return id;
            }
            if (n.text == "+") { return on; }
            fail("this back end cannot express unary `" + n.text + "`");
            return on;
        }
        case glsl::nk::call: return call(n);
        default: fail("this back end cannot express that expression"); return constant(0.0f);
        }
    }

    [[nodiscard]] std::uint32_t load(const std::string & name) {
        if (const auto found = locals_.find(name); found != locals_.end()) {
            remember_type(found->second.id, found->second.type, found->second.t);
            return found->second.id;
        }
        const auto found = variables_.find(name);
        if (found == variables_.end()) {
            fail("`" + name + "` is not declared");
            return constant(0.0f);
        }
        const std::uint32_t id = next_id();
        append(code_, {op_load, found->second.type, id, found->second.id});
        remember_type(id, found->second.type, found->second.t);
        return id;
    }

    [[nodiscard]] std::uint32_t assign(const glsl::node & n) {
        if (n.text != "=") {
            fail("this back end cannot express `" + n.text + "`");
            return constant(0.0f);
        }
        const std::uint32_t value = expression(n.b);
        const glsl::node & target = m_->at(n.a);
        if (target.kind != glsl::nk::identifier) {
            fail("this back end can only assign to a name");
            return value;
        }
        if (const auto found = variables_.find(target.text); found != variables_.end()) {
            append(code_, {op_store, found->second.id, value});
            return value;
        }
        locals_[target.text] = {value, type_of_id(value), shape_of(value)};
        return value;
    }

    [[nodiscard]] std::uint32_t swizzle(const glsl::node & n) {
        const std::uint32_t on = expression(n.a);
        std::vector<std::uint32_t> picks;
        for (const char c : n.text) {
            switch (c) {
            case 'x':
            case 'r':
            case 's': picks.push_back(0); break;
            case 'y':
            case 'g':
            case 't': picks.push_back(1); break;
            case 'z':
            case 'b':
            case 'p': picks.push_back(2); break;
            case 'w':
            case 'a':
            case 'q': picks.push_back(3); break;
            default: fail("`." + n.text + "` is not a swizzle this back end knows"); return on;
            }
        }
        const std::uint32_t id = next_id();
        if (picks.size() == 1) {
            // ONE COMPONENT IS AN EXTRACT, not a shuffle - a shuffle of width one
            // is not a vector type and the driver rejects it.
            append(code_, {op_composite_extract, float_type_, id, on, picks[0]});
            remember_type(id, float_type_, glsl::type{glsl::base::f, 1, 1, -1, 0});
            return id;
        }
        const std::uint32_t result = type_vector(static_cast<std::uint32_t>(picks.size()));
        std::vector<std::uint32_t> parts{op_vector_shuffle, result, id, on, on};
        for (const std::uint32_t p : picks) { parts.push_back(p); }
        append(code_, std::move(parts));
        remember_type(id, result,
                      glsl::type{glsl::base::f, static_cast<std::uint8_t>(picks.size()), 1, -1, 0});
        return id;
    }

    [[nodiscard]] std::uint32_t binary(const glsl::node & n) {
        const std::uint32_t left = expression(n.a);
        const std::uint32_t right = expression(n.b);
        const glsl::type ls = shape_of(left);
        const glsl::type rs = shape_of(right);

        // MATRIX PRODUCTS ARE THEIR OWN OPCODES, not componentwise - the same
        // rule the software path has, and the same consequence for getting it
        // wrong: a picture rather than an error.
        if (n.text == "*" && ls.is_matrix() && rs.is_vector()) {
            const std::uint32_t id = next_id();
            const std::uint32_t result = type_vector(ls.rows);
            append(code_, {op_matrix_times_vector, result, id, left, right});
            remember_type(id, result, glsl::type{glsl::base::f, ls.rows, 1, -1, 0});
            return id;
        }
        if (n.text == "*" && ls.is_matrix() && rs.is_matrix()) {
            const std::uint32_t id = next_id();
            append(code_, {op_matrix_times_matrix, type_of_id(left), id, left, right});
            remember_type(id, type_of_id(left), ls);
            return id;
        }
        // A VECTOR TIMES A SCALAR has its own opcode too; OpFMul needs both
        // operands the same width, so this is not an optimisation but a
        // requirement.
        if (n.text == "*" && ls.is_vector() && rs.is_scalar()) {
            const std::uint32_t id = next_id();
            append(code_, {op_vector_times_scalar, type_of_id(left), id, left, right});
            remember_type(id, type_of_id(left), ls);
            return id;
        }
        if (n.text == "*" && ls.is_scalar() && rs.is_vector()) {
            const std::uint32_t id = next_id();
            append(code_, {op_vector_times_scalar, type_of_id(right), id, right, left});
            remember_type(id, type_of_id(right), rs);
            return id;
        }

        std::uint32_t code = 0;
        bool comparison = false;
        if (n.text == "+") {
            code = op_f_add;
        } else if (n.text == "-") {
            code = op_f_sub;
        } else if (n.text == "*") {
            code = op_f_mul;
        } else if (n.text == "/") {
            code = op_f_div;
        } else if (n.text == "<") {
            code = op_f_ord_less_than;
            comparison = true;
        } else if (n.text == ">") {
            code = op_f_ord_greater_than;
            comparison = true;
        } else if (n.text == "<=") {
            code = op_f_ord_less_than_equal;
            comparison = true;
        } else if (n.text == ">=") {
            code = op_f_ord_greater_than_equal;
            comparison = true;
        } else if (n.text == "==") {
            code = op_f_ord_equal;
            comparison = true;
        } else if (n.text == "!=") {
            code = op_f_ord_not_equal;
            comparison = true;
        } else {
            fail("this back end cannot express `" + n.text + "`");
            return left;
        }
        if (comparison) {
            fail("this back end cannot express a comparison yet");
            return left;
        }
        // Both operands must be the SAME width for OpF*, so a scalar meeting a
        // vector is broadcast first.
        std::uint32_t a = left;
        std::uint32_t b = right;
        const std::uint32_t width = std::max<std::uint32_t>(ls.rows, rs.rows);
        if (ls.rows < width) { a = broadcast(a, width); }
        if (rs.rows < width) { b = broadcast(b, width); }
        const std::uint32_t result = type_vector(width);
        const std::uint32_t id = next_id();
        append(code_, {code, result, id, a, b});
        remember_type(id, result,
                      glsl::type{glsl::base::f, static_cast<std::uint8_t>(width), 1, -1, 0});
        return id;
    }

    [[nodiscard]] std::uint32_t broadcast(std::uint32_t scalar, std::uint32_t width) {
        const std::uint32_t result = type_vector(width);
        const std::uint32_t id = next_id();
        std::vector<std::uint32_t> parts{op_composite_construct, result, id};
        for (std::uint32_t i = 0; i < width; ++i) { parts.push_back(scalar); }
        append(code_, std::move(parts));
        remember_type(id, result,
                      glsl::type{glsl::base::f, static_cast<std::uint8_t>(width), 1, -1, 0});
        return id;
    }

    [[nodiscard]] std::uint32_t call(const glsl::node & n) {
        std::vector<std::uint32_t> args;
        args.reserve(n.kids.size());
        for (const std::int32_t each : n.kids) { args.push_back(expression(each)); }
        if (failed()) { return constant(0.0f); }

        // A CONSTRUCTOR is a composite construct, and its arguments may be a mix
        // of scalars and vectors - `vec4(v3, 1.0)` - so each is flattened into
        // components first.
        static constexpr std::pair<std::string_view, std::uint32_t> shapes[] = {
            {"vec2", 2}, {"vec3", 3}, {"vec4", 4}, {"float", 1}};
        for (const auto & [name, width] : shapes) {
            if (n.text != name) { continue; }
            std::vector<std::uint32_t> components;
            for (const std::uint32_t each : args) {
                const glsl::type shape = shape_of(each);
                if (shape.is_scalar()) {
                    components.push_back(each);
                    continue;
                }
                for (std::uint32_t i = 0; i < shape.rows; ++i) {
                    const std::uint32_t part = next_id();
                    append(code_, {op_composite_extract, float_type_, part, each, i});
                    components.push_back(part);
                }
            }
            if (width == 1) { return components.empty() ? constant(0.0f) : components.front(); }
            // `vec3(1.0)` fills every component from one value.
            while (components.size() < width) {
                components.push_back(components.empty() ? constant(0.0f) : components.front());
            }
            components.resize(width);
            const std::uint32_t result = type_vector(width);
            const std::uint32_t id = next_id();
            std::vector<std::uint32_t> parts{op_composite_construct, result, id};
            for (const std::uint32_t c : components) { parts.push_back(c); }
            append(code_, std::move(parts));
            remember_type(id, result,
                          glsl::type{glsl::base::f, static_cast<std::uint8_t>(width), 1, -1, 0});
            return id;
        }

        // The built-ins that live in GLSL.std.450.
        struct builtin {
            std::string_view name;
            std::uint32_t which;
            std::size_t arity;
            bool returns_scalar;
        };
        static constexpr builtin table[] = {
            {"abs", glsl_fabs, 1, false},
            {"floor", glsl_floor, 1, false},
            {"ceil", glsl_ceil, 1, false},
            {"fract", glsl_fract, 1, false},
            {"sin", glsl_sin, 1, false},
            {"cos", glsl_cos, 1, false},
            {"tan", glsl_tan, 1, false},
            {"exp", glsl_exp, 1, false},
            {"log", glsl_log, 1, false},
            {"sqrt", glsl_sqrt, 1, false},
            {"normalize", glsl_normalize, 1, false},
            {"pow", glsl_pow, 2, false},
            {"min", glsl_fmin, 2, false},
            {"max", glsl_fmax, 2, false},
            {"step", glsl_step, 2, false},
            {"reflect", glsl_reflect, 2, false},
            {"cross", glsl_cross, 2, false},
            {"clamp", glsl_fclamp, 3, false},
            {"mix", glsl_fmix, 3, false},
            {"smoothstep", glsl_smoothstep, 3, false},
            {"length", glsl_length, 1, true},
            {"distance", glsl_distance, 2, true},
        };
        for (const builtin & known : table) {
            if (n.text != known.name || args.size() != known.arity) { continue; }
            // The result is the WIDEST argument's shape, except for the two that
            // reduce to a scalar.
            glsl::type shape = shape_of(args.front());
            for (const std::uint32_t each : args) {
                if (shape_of(each).rows > shape.rows) { shape = shape_of(each); }
            }
            // A scalar operand of a componentwise built-in has to be broadcast,
            // exactly as for arithmetic.
            if (!known.returns_scalar) {
                for (std::uint32_t & each : args) {
                    if (shape_of(each).rows < shape.rows) { each = broadcast(each, shape.rows); }
                }
            }
            const std::uint32_t result =
                known.returns_scalar ? float_type_ : type_vector(shape.rows);
            const std::uint32_t id = next_id();
            std::vector<std::uint32_t> parts{op_ext_inst, result, id, ext_set_, known.which};
            for (const std::uint32_t each : args) { parts.push_back(each); }
            append(code_, std::move(parts));
            remember_type(id, result,
                          known.returns_scalar ? glsl::type{glsl::base::f, 1, 1, -1, 0} : shape);
            return id;
        }
        // `dot` is core rather than extended.
        if (n.text == "dot" && args.size() == 2) {
            const std::uint32_t id = next_id();
            append(code_, {148 /* OpDot */, float_type_, id, args[0], args[1]});
            remember_type(id, float_type_, glsl::type{glsl::base::f, 1, 1, -1, 0});
            return id;
        }
        fail("this back end cannot express `" + n.text + "`");
        return constant(0.0f);
    }

    // What SPIR-V type an id has, and what GLSL shape that is. Tracked because
    // SPIR-V is typed and the emitter has to pick an opcode by operand shape.
    void remember_type(std::uint32_t id, std::uint32_t type, const glsl::type & shape) {
        id_types_[id] = type;
        id_shapes_[id] = shape;
    }
    [[nodiscard]] std::uint32_t type_of_id(std::uint32_t id) {
        const auto found = id_types_.find(id);
        return found == id_types_.end() ? float_type_ : found->second;
    }
    [[nodiscard]] glsl::type shape_of(std::uint32_t id) {
        const auto found = id_shapes_.find(id);
        return found == id_shapes_.end() ? glsl::type{glsl::base::f, 1, 1, -1, 0} : found->second;
    }

    const glsl::shader * m_ = nullptr;
    std::string failure_;
    std::uint32_t next_ = 1;
    std::uint32_t entry_ = 0;
    std::uint32_t ext_set_ = 1; // reserved first, so its id is stable
    std::uint32_t void_type_ = 0;
    std::uint32_t float_type_ = 0;
    std::uint32_t function_type_ = 0;
    std::uint32_t position_ = 0;
    std::uint32_t uniform_binding_ = 0;

    std::vector<std::uint32_t> decorations_;
    std::vector<std::uint32_t> types_;
    std::vector<std::uint32_t> code_;
    std::vector<std::uint32_t> interface_ids_;

    std::unordered_map<std::uint32_t, std::uint32_t> vectors_;
    std::unordered_map<std::uint32_t, std::uint32_t> matrices_;
    std::unordered_map<std::uint64_t, std::uint32_t> pointers_;
    std::unordered_map<std::uint32_t, std::uint32_t> constants_;
    std::unordered_map<std::uint32_t, std::uint32_t> id_types_;
    std::unordered_map<std::uint32_t, glsl::type> id_shapes_;
    std::unordered_map<std::string, known> variables_;
    std::unordered_map<std::string, known> locals_;
};

} // namespace

module_binary emit(const glsl::shader & m) {
    module_binary out;
    if (!m.ok) {
        out.error = "the shader did not compile";
        return out;
    }
    emitter make{m};
    return make.run();
}

} // namespace ctbrowser::raster::spirv
