#include <ctbrowser/raster/glsl.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>
#include <unordered_map>

// The GLSL ES lexer and parser. The preprocessor is next door in
// glsl_preprocess.cpp - a separate language, and separately testable.
//
// A HAND-WRITTEN RECURSIVE-DESCENT PARSER, like the JavaScript one, and for the
// same reason: the tree is a flat vector of nodes with integer child indices, so
// a node index is a stable name that survives the vector growing, and the whole
// program is one allocation.
//
// The grammar handled is the one tests/glsl/ needs - sixteen shaders p5.js
// ships, which is somebody else's GLSL and therefore the only kind worth testing
// against. What is not handled is listed at the bottom of glsl.hpp.
//
// EVERY MALFORMED SHADER COMES BACK AS A DIAGNOSTIC. A shader is untrusted text
// from a page: there is no input that may crash this, and after the first error
// the parser skips to a statement boundary and keeps going, because a page shows
// getShaderInfoLog to a person and one message at a time is a poor way to fix
// twenty.

namespace ctbrowser::raster::glsl {
namespace {

// --- the lexer -------------------------------------------------------------

enum class tk : std::uint8_t { end, name, number, punct, keyword };

struct token {
    tk kind = tk::end;
    std::string text;
    std::uint32_t line = 1;
    bool is_float = false; // a number with a `.`, an exponent or an `f`
};

[[nodiscard]] bool name_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
[[nodiscard]] bool name_part(char c) { return name_start(c) || (c >= '0' && c <= '9'); }

// The words that are not identifiers. Type names are NOT here: they are
// identifiers until the parser decides otherwise, which is what lets
// `#define int float` work - a macro may rename a type, and the corpus does
// exactly that.
[[nodiscard]] bool is_keyword(std::string_view word) {
    static constexpr std::array<std::string_view, 24> words{
        "if",        "else",   "for",     "while",  "do",      "return", "break",  "continue",
        "discard",   "struct", "uniform", "attribute", "varying", "const",  "in",     "out",
        "inout",     "precision", "invariant", "highp", "mediump", "lowp",  "true",   "false"};
    return std::ranges::find(words, word) != words.end();
}

[[nodiscard]] std::vector<token> lex(std::string_view text) {
    std::vector<token> out;
    std::uint32_t line = 1;
    for (std::size_t i = 0; i < text.size();) {
        const char c = text[i];
        if (c == '\n') {
            ++line;
            ++i;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r') {
            ++i;
            continue;
        }
        if (name_start(c)) {
            const std::size_t start = i;
            while (i < text.size() && name_part(text[i])) { ++i; }
            std::string word{text.substr(start, i - start)};
            const bool key = is_keyword(word);
            out.push_back(token{key ? tk::keyword : tk::name, std::move(word), line, false});
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) != 0 ||
            (c == '.' && i + 1 < text.size() &&
             std::isdigit(static_cast<unsigned char>(text[i + 1])) != 0)) {
            const std::size_t start = i;
            bool real = false;
            // Hex first, because 0x1p3 is not a thing in GLSL and 0x1e2 must not
            // eat the `e` as an exponent.
            if (c == '0' && i + 1 < text.size() && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
                i += 2;
                while (i < text.size() && std::isxdigit(static_cast<unsigned char>(text[i])) != 0) {
                    ++i;
                }
            } else {
                while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])) != 0) {
                    ++i;
                }
                if (i < text.size() && text[i] == '.') {
                    real = true;
                    ++i;
                    while (i < text.size() &&
                           std::isdigit(static_cast<unsigned char>(text[i])) != 0) {
                        ++i;
                    }
                }
                if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
                    real = true;
                    ++i;
                    if (i < text.size() && (text[i] == '+' || text[i] == '-')) { ++i; }
                    while (i < text.size() &&
                           std::isdigit(static_cast<unsigned char>(text[i])) != 0) {
                        ++i;
                    }
                }
            }
            if (i < text.size() && (text[i] == 'f' || text[i] == 'F')) {
                real = true;
                ++i;
            }
            out.push_back(token{tk::number, std::string{text.substr(start, i - start)}, line, real});
            continue;
        }
        // The longest operator that matches, so `<<=` beats `<<` beats `<`.
        static constexpr std::array<std::string_view, 16> three{"<<=", ">>="};
        static constexpr std::array<std::string_view, 18> two{
            "++", "--", "<=", ">=", "==", "!=", "&&", "||", "^^",
            "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<"};
        std::string op;
        for (const std::string_view candidate : three) {
            if (!candidate.empty() && text.compare(i, candidate.size(), candidate) == 0) {
                op = candidate;
                break;
            }
        }
        if (op.empty()) {
            for (const std::string_view candidate : two) {
                if (!candidate.empty() && text.compare(i, candidate.size(), candidate) == 0) {
                    op = candidate;
                    break;
                }
            }
        }
        // `>>` is lexed here rather than in the table above so it stays beside
        // its partner; GLSL has no templates, so there is no ambiguity to fear.
        if (op.empty() && text.compare(i, 2, ">>") == 0) { op = ">>"; }
        if (op.empty()) { op = std::string{c}; }
        i += op.size();
        out.push_back(token{tk::punct, std::move(op), line, false});
    }
    out.push_back(token{tk::end, {}, line, false});
    return out;
}

// --- the parser ------------------------------------------------------------

// Built-in type names, and the shape each one means.
[[nodiscard]] bool builtin_type(std::string_view word, type & into) {
    struct entry {
        std::string_view name;
        base kind;
        std::uint8_t rows;
        std::uint8_t cols;
    };
    static constexpr std::array<entry, 20> table{{
        {"void", base::void_, 1, 1},      {"float", base::f, 1, 1},
        {"int", base::i, 1, 1},           {"bool", base::b, 1, 1},
        {"vec2", base::f, 2, 1},          {"vec3", base::f, 3, 1},
        {"vec4", base::f, 4, 1},          {"ivec2", base::i, 2, 1},
        {"ivec3", base::i, 3, 1},         {"ivec4", base::i, 4, 1},
        {"bvec2", base::b, 2, 1},         {"bvec3", base::b, 3, 1},
        {"bvec4", base::b, 4, 1},         {"mat2", base::f, 2, 2},
        {"mat3", base::f, 3, 3},          {"mat4", base::f, 4, 4},
        {"sampler2D", base::sampler2d, 1, 1},
        {"samplerCube", base::sampler_cube, 1, 1},
        {"mat2x2", base::f, 2, 2},        {"mat3x3", base::f, 3, 3},
    }};
    for (const entry & known : table) {
        if (known.name == word) {
            into = type{known.kind, known.rows, known.cols, -1, 0};
            return true;
        }
    }
    return false;
}

class parser {
public:
    parser(std::vector<token> tokens, module & into) : t_(std::move(tokens)), m_(&into) {}

    void run() {
        while (!at_end()) {
            const std::size_t before = at_;
            const std::int32_t declared = declaration();
            if (declared >= 0) { m_->declarations.push_back(declared); }
            // NO INFINITE LOOP ON A SHAPE NOT UNDERSTOOD. If a declaration
            // consumed nothing, skip a token; the error was already reported.
            if (at_ == before) { ++at_; }
        }
    }

private:
    // --- token helpers
    [[nodiscard]] const token & here() const { return t_[at_]; }
    [[nodiscard]] bool at_end() const { return here().kind == tk::end; }
    [[nodiscard]] const token & peek(std::size_t ahead) const {
        return t_[std::min(at_ + ahead, t_.size() - 1)];
    }
    [[nodiscard]] bool is(std::string_view text) const {
        return (here().kind == tk::punct || here().kind == tk::keyword) && here().text == text;
    }
    bool accept(std::string_view text) {
        if (!is(text)) { return false; }
        ++at_;
        return true;
    }
    void expect(std::string_view text) {
        if (accept(text)) { return; }
        fail("expected `" + std::string{text} + "`, found " +
             (at_end() ? std::string{"end of shader"} : "`" + here().text + "`"));
    }
    void fail(std::string message) {
        // ONE ERROR PER LINE. A parser that has lost its place produces a
        // message per token, and twenty messages about line 12 tell a reader
        // less than one does.
        if (!m_->errors.empty() && m_->errors.back().line == here().line) { return; }
        m_->errors.push_back(diagnostic{here().line, std::move(message)});
    }

    // Skip to something that can start a new statement, so one syntax error
    // does not cascade.
    void recover() {
        int depth = 0;
        while (!at_end()) {
            if (is("{")) { ++depth; }
            if (is("}")) {
                if (depth == 0) { return; }
                --depth;
            }
            if (depth == 0 && is(";")) {
                ++at_;
                return;
            }
            ++at_;
        }
    }

    [[nodiscard]] std::int32_t add(node n) {
        n.line = here().line;
        m_->nodes.push_back(std::move(n));
        return static_cast<std::int32_t>(m_->nodes.size()) - 1;
    }

    // --- types
    //
    // Qualifiers come first and in any order, which is what `uniform highp vec2`
    // and `const in float` both need.
    struct qualified {
        type t;
        storage store = storage::none;
        direction dir = direction::in;
        bool found = false;
    };

    [[nodiscard]] qualified type_specifier() {
        qualified out;
        while (true) {
            if (accept("highp") || accept("mediump") || accept("lowp") || accept("invariant")) {
                continue; // parsed and ignored: everything here is a float
            }
            if (is("uniform")) {
                ++at_;
                out.store = storage::uniform;
                continue;
            }
            if (is("attribute")) {
                ++at_;
                out.store = storage::attribute;
                continue;
            }
            if (is("varying")) {
                ++at_;
                out.store = storage::varying;
                continue;
            }
            if (is("const")) {
                ++at_;
                out.store = storage::constant;
                continue;
            }
            if (is("in")) {
                ++at_;
                out.dir = direction::in;
                continue;
            }
            if (is("out")) {
                ++at_;
                out.dir = direction::out;
                continue;
            }
            if (is("inout")) {
                ++at_;
                out.dir = direction::inout;
                continue;
            }
            break;
        }
        if (here().kind != tk::name && here().kind != tk::keyword) { return out; }
        if (builtin_type(here().text, out.t)) {
            ++at_;
            out.found = true;
            return out;
        }
        // A struct this shader declared earlier.
        for (std::size_t i = 0; i < m_->structs.size(); ++i) {
            if (m_->structs[i].name == here().text) {
                ++at_;
                out.t = type{base::struct_, 1, 1, static_cast<std::int32_t>(i), 0};
                out.found = true;
                return out;
            }
        }
        return out;
    }

    // `[4]` after a name, or `[]`. Returns 0 when there is no array suffix.
    [[nodiscard]] std::int32_t array_suffix() {
        if (!accept("[")) { return 0; }
        if (accept("]")) { return -1; } // sized by its initialiser or by the caller
        std::int32_t length = 0;
        if (here().kind == tk::number) {
            length = static_cast<std::int32_t>(std::strtol(here().text.c_str(), nullptr, 0));
            ++at_;
        } else {
            // A constant expression this parser does not fold. Recorded as
            // unsized rather than guessed, and stage 2 resolves it.
            while (!at_end() && !is("]")) { ++at_; }
            length = -1;
        }
        expect("]");
        return length;
    }

    // --- declarations
    [[nodiscard]] std::int32_t declaration() {
        if (accept(";")) { return -1; }
        if (is("precision")) {
            // `precision highp float;` - parsed and dropped.
            while (!at_end() && !is(";")) { ++at_; }
            accept(";");
            return -1;
        }
        if (is("struct")) { return struct_definition(); }

        const std::size_t start = at_;
        qualified q = type_specifier();
        if (!q.found) {
            fail("expected a type, found `" + here().text + "`");
            at_ = start;
            recover();
            return -1;
        }
        if (here().kind != tk::name) {
            fail("expected a name after the type");
            recover();
            return -1;
        }
        std::string name = here().text;
        ++at_;

        // A function: `type name(` - and never a variable, because a variable
        // cannot be followed by a paren.
        if (is("(")) { return function(std::move(name), q.t); }

        return variable(std::move(name), q, true);
    }

    [[nodiscard]] std::int32_t struct_definition() {
        expect("struct");
        struct_type made;
        if (here().kind == tk::name) {
            made.name = here().text;
            ++at_;
        }
        expect("{");
        while (!at_end() && !is("}")) {
            const qualified member = type_specifier();
            if (!member.found) {
                fail("expected a type in struct " + made.name);
                recover();
                continue;
            }
            // `vec3 a, b;` - one type, several names.
            while (!at_end()) {
                if (here().kind != tk::name) {
                    fail("expected a member name");
                    break;
                }
                struct_type::member field{here().text, member.t};
                ++at_;
                field.t.array = array_suffix();
                made.members.push_back(std::move(field));
                if (!accept(",")) { break; }
            }
            expect(";");
        }
        expect("}");
        const auto index = static_cast<std::int32_t>(m_->structs.size());
        m_->structs.push_back(std::move(made));
        node n;
        n.kind = nk::struct_def;
        n.text = m_->structs.back().name;
        n.t = type{base::struct_, 1, 1, index, 0};
        const std::int32_t made_node = add(std::move(n));
        // `struct S { ... } instance;` declares a variable too.
        if (here().kind == tk::name) {
            qualified q;
            q.t = type{base::struct_, 1, 1, index, 0};
            q.found = true;
            std::string name = here().text;
            ++at_;
            return variable(std::move(name), q, true);
        }
        expect(";");
        return made_node;
    }

    [[nodiscard]] std::int32_t variable(std::string name, const qualified & q, bool top_level) {
        node n;
        n.kind = nk::var_decl;
        n.text = std::move(name);
        n.t = q.t;
        n.store = q.store;
        n.t.array = array_suffix();
        if (accept("=")) { n.a = expression(); }
        const std::int32_t first = add(std::move(n));

        // `float a, b = 1.0, c;` - the rest share the type but not the array
        // suffix or the initialiser.
        std::vector<std::int32_t> more;
        while (accept(",")) {
            if (here().kind != tk::name) {
                fail("expected a name after `,`");
                break;
            }
            node extra;
            extra.kind = nk::var_decl;
            extra.text = here().text;
            extra.t = q.t;
            extra.store = q.store;
            ++at_;
            extra.t.array = array_suffix();
            if (accept("=")) { extra.a = expression(); }
            more.push_back(add(std::move(extra)));
        }
        expect(";");
        if (top_level) {
            note_interface(first);
            for (const std::int32_t each : more) {
                note_interface(each);
                m_->declarations.push_back(each);
            }
        } else if (!more.empty()) {
            // Inside a body several declarators become one block, so a caller
            // walking statements sees them all.
            node group;
            group.kind = nk::block;
            group.kids.push_back(first);
            for (const std::int32_t each : more) { group.kids.push_back(each); }
            return add(std::move(group));
        }
        return first;
    }

    void note_interface(std::int32_t which) {
        const node & n = m_->at(which);
        if (n.store == storage::none || n.store == storage::constant) { return; }
        m_->interface_.push_back(interface_variable{n.text, n.t, n.store, n.line});
    }

    [[nodiscard]] std::int32_t function(std::string name, type returns) {
        node fn;
        fn.kind = nk::function;
        fn.text = std::move(name);
        fn.t = returns;
        expect("(");
        // `void main(void)` - a lone `void` is an empty parameter list.
        if (here().kind == tk::name && here().text == "void" && peek(1).text == ")") { ++at_; }
        while (!at_end() && !is(")")) {
            const qualified p = type_specifier();
            if (!p.found) {
                fail("expected a parameter type");
                break;
            }
            node param;
            param.kind = nk::parameter;
            param.t = p.t;
            param.dir = p.dir;
            if (here().kind == tk::name) {
                param.text = here().text;
                ++at_;
                param.t.array = array_suffix();
            }
            fn.kids.push_back(add(std::move(param)));
            if (!accept(",")) { break; }
        }
        expect(")");
        // A PROTOTYPE ends here. The corpus has none, but a shader may declare a
        // function before defining it and rejecting that would be wrong.
        if (accept(";")) { return add(std::move(fn)); }
        const std::int32_t index = add(std::move(fn));
        const std::int32_t body = block();
        m_->nodes[static_cast<std::size_t>(index)].a = body;
        return index;
    }

    // --- statements
    [[nodiscard]] std::int32_t block() {
        node n;
        n.kind = nk::block;
        const std::int32_t index = add(std::move(n));
        expect("{");
        while (!at_end() && !is("}")) {
            const std::size_t before = at_;
            const std::int32_t s = statement();
            if (s >= 0) { m_->nodes[static_cast<std::size_t>(index)].kids.push_back(s); }
            if (at_ == before) { ++at_; }
        }
        expect("}");
        return index;
    }

    [[nodiscard]] std::int32_t statement() {
        if (is("{")) { return block(); }
        if (accept(";")) { return -1; }
        if (is("if")) { return if_statement(); }
        if (is("for")) { return for_statement(); }
        if (is("while")) { return while_statement(); }
        if (is("do")) { return do_statement(); }
        if (is("struct")) { return struct_definition(); }
        if (is("precision")) {
            while (!at_end() && !is(";")) { ++at_; }
            accept(";");
            return -1;
        }
        if (is("return")) {
            ++at_;
            node n;
            n.kind = nk::return_stmt;
            if (!is(";")) { n.a = expression(); }
            expect(";");
            return add(std::move(n));
        }
        for (const auto & [word, kind] :
             std::initializer_list<std::pair<std::string_view, nk>>{
                 {"break", nk::break_stmt}, {"continue", nk::continue_stmt},
                 {"discard", nk::discard_stmt}}) {
            if (is(word)) {
                ++at_;
                expect(";");
                node n;
                n.kind = kind;
                return add(std::move(n));
            }
        }
        // A DECLARATION OR AN EXPRESSION, and the difference is whether it
        // starts with a type. `vec3 x = ...` declares; `x = vec3(...)` does not.
        if (starts_a_declaration()) {
            const qualified q = type_specifier();
            if (q.found && here().kind == tk::name) {
                std::string name = here().text;
                ++at_;
                return variable(std::move(name), q, false);
            }
            fail("expected a name after the type");
            recover();
            return -1;
        }
        node n;
        n.kind = nk::expr_stmt;
        n.a = expression();
        expect(";");
        return add(std::move(n));
    }

    // Look ahead without consuming: does a type specifier start here?
    [[nodiscard]] bool starts_a_declaration() {
        const std::size_t saved = at_;
        const qualified q = type_specifier();
        const bool yes = q.found && here().kind == tk::name;
        at_ = saved;
        return yes;
    }

    [[nodiscard]] std::int32_t if_statement() {
        expect("if");
        node n;
        n.kind = nk::if_stmt;
        expect("(");
        n.a = expression();
        expect(")");
        const std::int32_t index = add(std::move(n));
        const std::int32_t then = statement();
        m_->nodes[static_cast<std::size_t>(index)].b = then;
        if (accept("else")) {
            const std::int32_t otherwise = statement();
            m_->nodes[static_cast<std::size_t>(index)].c = otherwise;
        }
        return index;
    }

    [[nodiscard]] std::int32_t for_statement() {
        expect("for");
        node n;
        n.kind = nk::for_stmt;
        const std::int32_t index = add(std::move(n));
        expect("(");
        std::int32_t init = -1;
        if (!is(";")) {
            init = statement(); // consumes its own `;`
        } else {
            accept(";");
        }
        std::int32_t cond = -1;
        if (!is(";")) { cond = expression(); }
        expect(";");
        std::int32_t step = -1;
        if (!is(")")) { step = expression(); }
        expect(")");
        const std::int32_t body = statement();
        node & made = m_->nodes[static_cast<std::size_t>(index)];
        made.a = init;
        made.b = cond;
        made.c = step;
        made.d = body;
        return index;
    }

    [[nodiscard]] std::int32_t while_statement() {
        expect("while");
        node n;
        n.kind = nk::while_stmt;
        expect("(");
        n.a = expression();
        expect(")");
        const std::int32_t index = add(std::move(n));
        const std::int32_t body = statement();
        m_->nodes[static_cast<std::size_t>(index)].b = body;
        return index;
    }

    [[nodiscard]] std::int32_t do_statement() {
        expect("do");
        node n;
        n.kind = nk::do_stmt;
        const std::int32_t index = add(std::move(n));
        const std::int32_t body = statement();
        expect("while");
        expect("(");
        const std::int32_t cond = expression();
        expect(")");
        expect(";");
        node & made = m_->nodes[static_cast<std::size_t>(index)];
        made.a = body;
        made.b = cond;
        return index;
    }

    // --- expressions
    //
    // Precedence climbing. The table is GLSL ES's, which is C's with the
    // additions GLSL makes (`^^`) and the omissions it makes (no comma inside a
    // function argument, handled by parsing arguments at assignment level).

    [[nodiscard]] std::int32_t expression() {
        std::int32_t left = assignment();
        while (is(",")) {
            ++at_;
            node n;
            n.kind = nk::sequence;
            n.a = left;
            n.b = assignment();
            left = add(std::move(n));
        }
        return left;
    }

    [[nodiscard]] std::int32_t assignment() {
        const std::int32_t left = conditional();
        static constexpr std::array<std::string_view, 11> ops{"=",  "+=", "-=", "*=", "/=", "%=",
                                                              "<<=", ">>=", "&=", "^=", "|="};
        for (const std::string_view op : ops) {
            if (is(op)) {
                node n;
                n.kind = nk::assign;
                n.text = std::string{op};
                ++at_;
                n.a = left;
                // RIGHT ASSOCIATIVE: `a = b = c` is `a = (b = c)`.
                n.b = assignment();
                return add(std::move(n));
            }
        }
        return left;
    }

    [[nodiscard]] std::int32_t conditional() {
        const std::int32_t cond = binary_expression(0);
        if (!accept("?")) { return cond; }
        node n;
        n.kind = nk::ternary;
        n.a = cond;
        n.b = expression();
        expect(":");
        n.c = assignment();
        return add(std::move(n));
    }

    // Lowest precedence first. Each entry is the set of operators at that level.
    [[nodiscard]] std::int32_t binary_expression(std::size_t precedence) {
        static const std::array<std::vector<std::string_view>, 10> levels{{
            {"||"},
            {"^^"},
            {"&&"},
            {"|"},
            {"^"},
            {"&"},
            {"==", "!="},
            {"<", ">", "<=", ">="},
            {"<<", ">>"},
            {"+", "-"},
        }};
        if (precedence >= levels.size()) { return multiplicative(); }
        std::int32_t left = binary_expression(precedence + 1);
        while (true) {
            std::string_view found;
            for (const std::string_view op : levels[precedence]) {
                if (is(op)) {
                    found = op;
                    break;
                }
            }
            if (found.empty()) { return left; }
            ++at_;
            node n;
            n.kind = nk::binary;
            n.text = std::string{found};
            n.a = left;
            n.b = binary_expression(precedence + 1);
            left = add(std::move(n));
        }
    }

    [[nodiscard]] std::int32_t multiplicative() {
        std::int32_t left = unary();
        while (is("*") || is("/") || is("%")) {
            node n;
            n.kind = nk::binary;
            n.text = here().text;
            ++at_;
            n.a = left;
            n.b = unary();
            left = add(std::move(n));
        }
        return left;
    }

    [[nodiscard]] std::int32_t unary() {
        if (is("++") || is("--")) {
            node n;
            n.kind = nk::prefix;
            n.text = here().text;
            ++at_;
            n.a = unary();
            return add(std::move(n));
        }
        if (is("-") || is("+") || is("!") || is("~")) {
            node n;
            n.kind = nk::unary;
            n.text = here().text;
            ++at_;
            n.a = unary();
            return add(std::move(n));
        }
        return postfix();
    }

    [[nodiscard]] std::int32_t postfix() {
        std::int32_t left = primary();
        while (true) {
            if (accept(".")) {
                node n;
                n.kind = nk::field;
                n.a = left;
                if (here().kind == tk::name) {
                    n.text = here().text;
                    ++at_;
                } else {
                    fail("expected a field or swizzle after `.`");
                }
                left = add(std::move(n));
                continue;
            }
            if (accept("[")) {
                node n;
                n.kind = nk::index;
                n.a = left;
                n.b = expression();
                expect("]");
                left = add(std::move(n));
                continue;
            }
            if (is("++") || is("--")) {
                node n;
                n.kind = nk::postfix;
                n.text = here().text;
                ++at_;
                n.a = left;
                left = add(std::move(n));
                continue;
            }
            return left;
        }
    }

    [[nodiscard]] std::int32_t primary() {
        if (accept("(")) {
            const std::int32_t inner = expression();
            expect(")");
            return inner;
        }
        if (here().kind == tk::number) {
            node n;
            n.kind = nk::literal;
            n.text = here().text;
            n.t = here().is_float ? type{base::f, 1, 1, -1, 0} : type{base::i, 1, 1, -1, 0};
            ++at_;
            return add(std::move(n));
        }
        if (is("true") || is("false")) {
            node n;
            n.kind = nk::literal;
            n.text = here().text;
            n.t = type{base::b, 1, 1, -1, 0};
            ++at_;
            return add(std::move(n));
        }
        if (here().kind == tk::name || here().kind == tk::keyword) {
            std::string word = here().text;
            ++at_;
            // A CALL, which is also how every constructor is spelled -
            // `vec3(1.0)` and `myFunction(x)` differ only in what the name
            // resolves to, and that is stage 2's decision rather than the
            // parser's.
            if (is("(")) {
                node n;
                n.kind = nk::call;
                n.text = std::move(word);
                ++at_;
                while (!at_end() && !is(")")) {
                    n.kids.push_back(assignment()); // not expression(): a comma separates arguments
                    if (!accept(",")) { break; }
                }
                expect(")");
                return add(std::move(n));
            }
            node n;
            n.kind = nk::identifier;
            n.text = std::move(word);
            return add(std::move(n));
        }
        fail("expected an expression, found `" + here().text + "`");
        node n;
        n.kind = nk::literal;
        n.text = "0";
        n.t = type{base::i, 1, 1, -1, 0};
        return add(std::move(n));
    }

    std::vector<token> t_;
    module * m_ = nullptr;
    std::size_t at_ = 0;
};

} // namespace

std::string spell(const type & t) {
    std::string out;
    switch (t.kind) {
    case base::void_: out = "void"; break;
    case base::sampler2d: out = "sampler2D"; break;
    case base::sampler_cube: out = "samplerCube"; break;
    case base::struct_: out = "struct"; break;
    case base::f:
    case base::i:
    case base::b: {
        const char * prefix = t.kind == base::f ? "" : (t.kind == base::i ? "i" : "b");
        if (t.is_matrix()) {
            out = "mat" + std::to_string(t.cols);
        } else if (t.is_vector()) {
            out = std::string{prefix} + "vec" + std::to_string(t.rows);
        } else {
            out = t.kind == base::f ? "float" : (t.kind == base::i ? "int" : "bool");
        }
        break;
    }
    }
    if (t.array > 0) { out += "[" + std::to_string(t.array) + "]"; }
    if (t.array < 0) { out += "[]"; }
    return out;
}

std::string module::info_log() const {
    std::string out;
    for (const diagnostic & d : errors) {
        out += "ERROR: 0:" + std::to_string(d.line) + ": " + d.message + "\n";
    }
    return out;
}

module parse(std::string_view source, const options & how) {
    module out;
    out.which = how.which;
    out.preprocessed = preprocess(source, how, out.errors);
    parser p{lex(out.preprocessed), out};
    p.run();
    out.ok = out.errors.empty();
    return out;
}

} // namespace ctbrowser::raster::glsl
