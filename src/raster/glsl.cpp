#include <ctbrowser/raster/glsl.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdlib>
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

enum class tk : std::uint8_t {
    end,
    name,
    number,
    punct,
    keyword
};

struct token {
    tk kind = tk::end;
    std::string text;
    std::uint32_t line = 1;
    bool is_float = false; // a number with a `.`, an exponent or an `f`
};

[[nodiscard]] bool name_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
[[nodiscard]] bool name_part(char c) {
    return name_start(c) || (c >= '0' && c <= '9');
}

// The words that are not identifiers. Type names are NOT here: they are
// identifiers until the parser decides otherwise, which is what lets
// `#define int float` work - a macro may rename a type, and the corpus does
// exactly that.
[[nodiscard]] bool is_keyword(std::string_view word) {
    static constexpr std::array<std::string_view, 24> words{
        "if",      "else",      "for",       "while",     "do",      "return", "break", "continue",
        "discard", "struct",    "uniform",   "attribute", "varying", "const",  "in",    "out",
        "inout",   "precision", "invariant", "highp",     "mediump", "lowp",   "true",  "false"};
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
            } else if (i < text.size() && (text[i] == 'u' || text[i] == 'U')) {
                // `1u` is an UNSIGNED literal, and ES 3.00 shaders are full of
                // them - `(1u << width) - 1u`. The suffix is consumed rather
                // than left to the operator table, where it lexed as an
                // identifier and every such expression failed to parse.
                ++i;
            }
            out.push_back(
                token{tk::number, std::string{text.substr(start, i - start)}, line, real});
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
    static constexpr std::array<entry, 24> table{{
        {"void", base::void_, 1, 1},
        {"float", base::f, 1, 1},
        {"int", base::i, 1, 1},
        {"bool", base::b, 1, 1},
        {"vec2", base::f, 2, 1},
        {"vec3", base::f, 3, 1},
        {"vec4", base::f, 4, 1},
        {"ivec2", base::i, 2, 1},
        {"ivec3", base::i, 3, 1},
        {"ivec4", base::i, 4, 1},
        {"bvec2", base::b, 2, 1},
        {"bvec3", base::b, 3, 1},
        {"bvec4", base::b, 4, 1},
        {"mat2", base::f, 2, 2},
        {"mat3", base::f, 3, 3},
        {"mat4", base::f, 4, 4},
        {"sampler2D", base::sampler2d, 1, 1},
        {"samplerCube", base::sampler_cube, 1, 1},
        {"mat2x2", base::f, 2, 2},
        {"mat3x3", base::f, 3, 3},
        // UNSIGNED INTEGERS, MAPPED ONTO SIGNED ONES, and the limit is stated
        // rather than hidden: this evaluator holds every value as a float, so
        // `uint` differs from `int` only for the top bit and for `>>`, which is
        // arithmetic here and logical in GLSL. Babylon's shaders use them for
        // bit-packed light indices well inside 31 bits. A shader that depends
        // on the difference gets a wrong answer rather than a diagnostic, which
        // is the one thing this tree tries not to ship - so it is recorded here
        // and in docs/raster.md rather than left to be discovered.
        {"uint", base::i, 1, 1},
        {"uvec2", base::i, 2, 1},
        {"uvec3", base::i, 3, 1},
        {"uvec4", base::i, 4, 1},
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
    parser(std::vector<token> tokens, shader & into) : t_(std::move(tokens)), m_(&into) {}

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

    // LOCALE-INDEPENDENT, which is the point as much as the speed. from_chars
    // never consults LC_NUMERIC; strtof and strtol do, and a shader that parsed
    // `0.5` differently on a host whose locale uses a decimal comma would move
    // the goldens. Hex integers are spelled `0x...` in GLSL, which from_chars
    // needs told about explicitly.
    static void parse_literal(node & n) {
        std::string_view text = n.text;
        if (n.t.kind == base::f) {
            float value = 0.0f;
            if (std::from_chars(text.data(), text.data() + text.size(), value).ec == std::errc{}) {
                n.number = value;
            }
            n.integer = static_cast<std::int32_t>(n.number);
            return;
        }
        int base_of = 10;
        if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
            base_of = 16;
            text.remove_prefix(2);
        }
        std::int32_t value = 0;
        if (std::from_chars(text.data(), text.data() + text.size(), value, base_of).ec ==
            std::errc{}) {
            n.integer = value;
        }
        n.number = static_cast<float>(n.integer);
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
            // `layout(std140, column_major)`, `layout(location = 0)`. It says
            // how storage is ARRANGED, which a software rasteriser gathering by
            // name does not need - except for a uniform block's std140 offsets,
            // which are computed from the member types rather than read from
            // here. Skipped as a balanced group so an unfamiliar qualifier
            // inside cannot derail the parse.
            // BY TEXT AND KIND, not through is(): `layout` is not in the
            // keyword table, so it lexes as a name and is() - which only ever
            // matches punctuation or keywords - answered false. The skip below
            // was therefore never reached and every `layout(...) uniform;`
            // still reported "expected a type".
            if (here().kind == tk::name && here().text == "layout") {
                ++at_;
                if (accept("(")) {
                    int depth = 1;
                    while (!at_end() && depth > 0) {
                        if (is("(")) { ++depth; }
                        if (is(")")) { --depth; }
                        ++at_;
                    }
                }
                continue;
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
            // from_chars for consistency with parse_literal above rather than
            // for correctness: strtol is NOT locale-sensitive - LC_NUMERIC
            // governs the decimal point, and an integer has none.
            node sized;
            sized.text = here().text;
            sized.t = type{base::i, 1, 1, -1, 0};
            parse_literal(sized);
            length = sized.integer;
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
        // `layout(std140, column_major) uniform;` - a DEFAULT for the blocks
        // that follow, and a whole statement on its own. It names no variable,
        // so the ordinary path below reports "expected a type" and the rest of
        // the shader is lost with it.
        if (!q.found && q.store == storage::uniform && accept(";")) { return -1; }
        // `uniform Material { vec4 a; mat4 b; };` - a uniform BLOCK. It is not
        // a type followed by a name; it is a name followed by a brace.
        if (!q.found && q.store == storage::uniform && here().kind == tk::name &&
            peek(1).text == "{") {
            return uniform_block();
        }
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

    // GLSL ES 3.00 SPELLS THE INTERFACE `in` AND `out`, and what they mean
    // depends on the stage: `in` is a vertex shader's attribute and a fragment
    // shader's varying, `out` is a vertex shader's varying and a fragment
    // shader's colour output. The existing storage enum already distinguishes
    // all of those, so this is spelling rather than semantics - which is why
    // the mapping is a translation here and not a second code path everywhere.
    //
    // ONLY AT TOP LEVEL. Inside a parameter list `in`/`out` mean the direction
    // an argument is copied, which is a different thing entirely and is what
    // `q.dir` goes on meaning there.
    //
    // LENIENTLY: this does not require `#version 300 es`, and does not reject
    // `attribute`/`varying` in a shader that declared it. Strict ES 3.00
    // removes those and strict ES 1.00 has no `in`/`out`, but pages ship both
    // and browsers take both - the same leniency contract shell/url.hpp records
    // for Boost.URL.
    [[nodiscard]] storage interface_storage(const qualified & q) const {
        if (q.store != storage::none) { return q.store; }
        if (q.dir == direction::in) {
            return m_->which == stage::vertex ? storage::attribute : storage::varying;
        }
        if (q.dir == direction::out) {
            return m_->which == stage::vertex ? storage::varying : storage::fragment_output;
        }
        return storage::none;
    }

    [[nodiscard]] std::int32_t variable(std::string name, const qualified & q, bool top_level) {
        node n;
        n.kind = nk::var_decl;
        n.text = std::move(name);
        n.t = q.t;
        n.store = top_level ? interface_storage(q) : q.store;
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
            extra.store = top_level ? interface_storage(q) : q.store;
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
        // THE DECLARED FRAGMENT OUTPUT, remembered by NAME. softgl reads the
        // colour out of the finished environment and had `gl_FragColor`
        // hardcoded; an ES 3.00 shader may not contain that identifier at all,
        // so the lookup found nothing and the draw painted nothing while
        // compiling and linking perfectly.
        //
        // The FIRST one wins. ES 3.00 allows several outputs with explicit
        // locations, which is multiple render targets - out of scope in
        // docs/webgl2-plan.md, and taking the first is what a single-target
        // rasteriser can honour.
        if (n.store == storage::fragment_output && m_->fragment_output == "gl_FragColor") {
            m_->fragment_output = n.text;
        }
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

    // std140, WHICH IS THE PAGE'S LAYOUT AND NOT A CHOICE. The page writes the
    // buffer to these offsets, so they are the specification's rules verbatim:
    // a scalar aligns to 4, a vec2 to 8, a vec3 and a vec4 to 16, a matrix is
    // its columns each aligned to 16, and an array's elements stride by 16
    // however small the element is.
    static std::uint32_t std140_align(const type & t) {
        if (t.array != 0 || t.is_matrix()) { return 16; }
        if (t.rows == 3 || t.rows == 4) { return 16; }
        if (t.rows == 2) { return 8; }
        return 4;
    }
    static std::uint32_t std140_size(const type & t) {
        const std::uint32_t stride =
            t.is_matrix() ? 16u * t.cols : (t.rows == 3 ? 16u : 4u * t.rows);
        if (t.array > 0) {
            return 16u * static_cast<std::uint32_t>(t.array) * (t.is_matrix() ? t.cols : 1u);
        }
        return stride;
    }

    [[nodiscard]] std::int32_t uniform_block() {
        shader::uniform_block block;
        block.name = here().text;
        ++at_;
        expect("{");
        std::uint32_t offset = 0;
        while (!at_end() && !is("}")) {
            const qualified member_type = type_specifier();
            if (!member_type.found || here().kind != tk::name) {
                fail("expected a member declaration in uniform block `" + block.name + "`");
                recover();
                break;
            }
            while (true) {
                std::string name = here().text;
                ++at_;
                type t = member_type.t;
                t.array = array_suffix();
                const std::uint32_t align = std140_align(t);
                offset = (offset + align - 1) / align * align;
                block.members.push_back(shader::uniform_block::member{std::move(name), t, offset});
                offset += std140_size(t);
                if (!accept(",")) { break; }
                if (here().kind != tk::name) {
                    fail("expected a name after `,`");
                    break;
                }
            }
            expect(";");
        }
        expect("}");
        block.size = (offset + 15) / 16 * 16;

        // AN INSTANCE NAME CHANGES HOW THE BODY REFERS TO THE MEMBERS, and
        // that is the whole difference between the two shapes:
        //
        //   uniform Light0 { vec4 a; };          ->  `a`
        //   uniform Light0 { vec4 a; } light0;   ->  `light0.a`
        //
        // So the second becomes a STRUCT type and one uniform of it, which is
        // machinery this front end already has, rather than a second kind of
        // name resolution. Babylon declares its per-light block that way and
        // its material and scene blocks the other, in the same shader.
        if (here().kind == tk::name) {
            std::string instance = here().text;
            ++at_;
            struct_type shape;
            shape.name = block.name;
            for (const shader::uniform_block::member & each : block.members) {
                shape.members.push_back(struct_type::member{each.name, each.t});
            }
            m_->structs.push_back(std::move(shape));
            node n;
            n.kind = nk::var_decl;
            n.text = instance;
            n.t = type{base::struct_, 1, 1, static_cast<std::int32_t>(m_->structs.size()) - 1, 0};
            n.store = storage::uniform;
            n.t.array = array_suffix();
            const std::int32_t which = add(std::move(n));
            m_->declarations.push_back(which);
            note_interface(which);
            block.instance = std::move(instance);
        } else {
            // NO INSTANCE: the members ARE globals, so they are declared as
            // ordinary uniforms and nothing downstream needs to know a block
            // was involved at all.
            for (const shader::uniform_block::member & each : block.members) {
                node n;
                n.kind = nk::var_decl;
                n.text = each.name;
                n.t = each.t;
                n.store = storage::uniform;
                const std::int32_t which = add(std::move(n));
                m_->declarations.push_back(which);
                note_interface(which);
            }
        }
        expect(";");
        m_->blocks.push_back(std::move(block));
        return -1;
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
        for (const auto & [word, kind] : std::initializer_list<std::pair<std::string_view, nk>>{
                 {"break", nk::break_stmt},
                 {"continue", nk::continue_stmt},
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
        static constexpr std::array<std::string_view, 11> ops{
            "=", "+=", "-=", "*=", "/=", "%=", "<<=", ">>=", "&=", "^=", "|="};
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
            parse_literal(n);
            ++at_;
            return add(std::move(n));
        }
        if (is("true") || is("false")) {
            node n;
            n.kind = nk::literal;
            n.text = here().text;
            n.t = type{base::b, 1, 1, -1, 0};
            n.number = n.text == "true" ? 1.0f : 0.0f;
            n.integer = n.text == "true" ? 1 : 0;
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
    shader * m_ = nullptr;
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

std::string shader::info_log() const {
    std::string out;
    for (const diagnostic & d : errors) {
        out += "ERROR: 0:" + std::to_string(d.line) + ": " + d.message + "\n";
    }
    return out;
}

// QUALIFIED, AND IT HAS TO BE. A line beginning with `module` at namespace scope
// is a C++20 MODULE DECLARATION as far as clang is concerned - `module` is a
// context-sensitive keyword there - so `module parse(...)` is a syntax error
// before it is ever a function. Writing the namespace first moves the keyword
// off the front of the line and it is a return type again.
//
// libstdc++ let this through and the mingw cross-build did not, which is the
// second thing that pairing caught here: it also found the missing <cstdlib>
// above. Anything returning this type at namespace scope needs the same.
glsl::shader parse(std::string_view source, const options & how) {
    shader out;
    out.which = how.which;
    out.preprocessed = preprocess(source, how, out.errors, &out.es300);
    parser p{lex(out.preprocessed), out};
    p.run();
    out.ok = out.errors.empty();
    return out;
} // namespace ctbrowser::raster::glsl

} // namespace ctbrowser::raster::glsl
