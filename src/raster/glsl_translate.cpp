#include <ctbrowser/raster/glsl_translate.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The AST back out as text, in the dialect glslang will lower. See
// glsl_translate.hpp for why this exists and what it is allowed to change.

namespace ctbrowser::raster::glsl {
namespace {

// A LITERAL IS PRINTED FROM ITS PARSED VALUE, not from `text`.
//
// The parser keeps the source spelling in `text` and the value in `number` /
// `integer`, and printing the spelling would be the obvious thing. It is wrong
// for a reason this tree has already paid for once: `0.5` written in a locale
// that uses a comma, or `1e-7` written as `.0000001`, are the same value spelled
// differently, and a translation that carries spellings around carries every
// lexical accident with them. std::to_chars is locale-independent, which is the
// same reason the parser uses std::from_chars.
[[nodiscard]] std::string print_float(float value) {
    std::array<char, 32> buffer{};
    const auto [end, code] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    std::string out{buffer.data(), end};
    // A GLSL FLOAT LITERAL NEEDS A POINT. `1` is an int and `1.0` is a float,
    // and in a language with no implicit conversion the difference is a compile
    // error rather than a rounding question.
    if (out.find('.') == std::string::npos && out.find('e') == std::string::npos &&
        out.find("inf") == std::string::npos && out.find("nan") == std::string::npos) {
        out += ".0";
    }
    return out;
}

[[nodiscard]] std::string print_int(std::int32_t value) {
    return std::to_string(value);
}

// The BUILT-INS THAT WERE RENAMED. ES 1.00 spells these one way and ES 3.00 the
// other, and the old names are not merely deprecated - they are gone, so a
// shader using them does not compile at all.
[[nodiscard]] std::string_view modern_name(std::string_view called) {
    if (called == "texture2D" || called == "texture2DProj") { return "texture"; }
    if (called == "textureCube") { return "texture"; }
    if (called == "texture2DLod" || called == "textureCubeLod") { return "textureLod"; }
    return called;
}

class printer {
public:
    printer(const shader & from, const binding_tables & bindings)
        : m_(&from), bindings_(&bindings) {}

    // VULKAN GLSL FORBIDS A NON-OPAQUE UNIFORM OUTSIDE A BLOCK:
    //
    //     'non-opaque uniforms outside a block' : not allowed when using GLSL
    //     for Vulkan
    //
    // and WebGL 1 shaders are made of them - `uniform float uAlpha` is the
    // normal way to write one. So they are gathered into a synthetic block,
    // which is what every WebGL-on-Vulkan implementation does; ANGLE calls it
    // the default uniform block. A block with NO INSTANCE NAME is the right
    // shape for it, because then the body still says `uAlpha` and nothing in
    // the shader has to be rewritten.
    static constexpr std::string_view default_block_name = "ctbrowser_DefaultUniforms";

    [[nodiscard]] translation run() {
        translation out;
        // 310 es IS THE FLOOR, and the reason is glslang's rather than a
        // preference: it is the lowest ES version it will lower to SPIR-V.
        text_ += "#version 310 es\n";
        // PRECISION IS MANDATORY IN AN ES FRAGMENT SHADER and an ES 1.00 one may
        // not have said so - `precision` statements are parsed and dropped by
        // this front end, so the information is not in the AST to carry over.
        // highp matches what the software rasteriser computes in: every value
        // there is a C++ `float`, so claiming anything less would be a lie the
        // GPU path could act on.
        text_ += "precision highp float;\nprecision highp int;\n";

        // ONE DESCRIPTOR SPACE, assigned here. A page numbers uniform blocks and
        // texture units independently and Vulkan does not, so binding 0 as a
        // block and unit 0 as a sampler are the same slot to a driver. Renumber
        // once, report it, and let the pipeline bind by the answer.
        assign_bindings();

        for (const struct_type & shape : m_->structs) { print_struct(shape); }
        print_blocks();
        print_globals();
        print_default_block();
        if (!failure_.empty()) {
            out.error = failure_;
            return out;
        }
        for (const std::int32_t which : m_->declarations) {
            if (m_->at(which).kind == nk::function) { print_function(m_->at(which)); }
        }
        if (!failure_.empty()) {
            out.error = failure_;
            return out;
        }
        out.source = std::move(text_);
        out.vulkan_bindings = vulkan_;
        if (!loose_.empty()) { out.default_block = std::string{default_block_name}; }
        out.ok = true;
        return out;
    }

private:
    const shader * m_;
    const binding_tables * bindings_;
    std::string text_;
    std::string failure_;
    // The name a fragment shader's colour comes out of, once it has one. ES 1.00
    // writes `gl_FragColor`, which does not exist in 3.00 - so one is declared
    // and every mention rewritten to it.
    std::string fragment_out_;
    // The loose non-opaque uniforms, gathered rather than declared where they
    // were found - see default_block_name.
    std::vector<const node *> loose_;
    std::vector<std::pair<std::string, std::uint32_t>> vulkan_;
    std::uint32_t next_binding_ = 0;

    [[nodiscard]] std::uint32_t binding_for(const std::string & name) const {
        for (const auto & [known, point] : vulkan_) {
            if (known == name) { return point; }
        }
        return 0;
    }

    // Blocks first, then samplers, then the gathered block last. The ORDER is
    // arbitrary and the REPORT is not: whatever is chosen here is what the
    // caller must bind by.
    void assign_bindings() {
        for (const shader::uniform_block & block : m_->blocks) {
            vulkan_.emplace_back(block.name, next_binding_++);
        }
        for (const std::int32_t which : m_->declarations) {
            const node & n = m_->at(which);
            if (n.kind == nk::var_decl && n.store == storage::uniform && n.t.is_sampler()) {
                vulkan_.emplace_back(n.text, next_binding_++);
            }
        }
    }

    void print_default_block() {
        if (loose_.empty()) { return; }
        const std::uint32_t binding = next_binding_++;
        vulkan_.emplace_back(std::string{default_block_name}, binding);
        text_ += "layout(std140, binding = " + std::to_string(binding) + ") uniform " +
                 std::string{default_block_name} + " {\n";
        for (const node * each : loose_) {
            text_ += "    " + type_name(each->t) + " " + each->text + array_suffix(each->t) + ";\n";
        }
        text_ += "};\n";
    }

    void fail(std::string why) {
        if (failure_.empty()) { failure_ = std::move(why); }
    }

    [[nodiscard]] bool is_vertex() const { return m_->which == stage::vertex; }

    // WHERE A DECLARATION GOES, once ES 3.00 has removed the words the page
    // used. `attribute` is a vertex input; a `varying` is an OUTPUT of the
    // vertex shader and an INPUT to the fragment shader - the same declaration,
    // read in opposite directions. Getting that backwards is not a compile
    // error, it is a shader that reads zero.
    [[nodiscard]] std::string_view direction_of(storage store) const {
        switch (store) {
        case storage::attribute: return "in";
        case storage::varying: return is_vertex() ? "out" : "in";
        case storage::fragment_output: return "out";
        case storage::uniform: return "uniform";
        case storage::constant: return "const";
        default: return "";
        }
    }

    [[nodiscard]] int location_of(const std::string & name) const {
        for (std::size_t i = 0; i < bindings_->attributes.size(); ++i) {
            if (bindings_->attributes[i] == name) { return static_cast<int>(i); }
        }
        return -1;
    }

    void print_struct(const struct_type & shape) {
        text_ += "struct " + shape.name + " {\n";
        for (const struct_type::member & each : shape.members) {
            text_ += "    " + type_name(each.t) + " " + each.name + array_suffix(each.t) + ";\n";
        }
        text_ += "};\n";
    }

    // A STRUCT IS SPELLED BY ITS NAME, and `spell()` cannot do it: that helper
    // is a free function with no module in reach, so it answers "struct" for
    // every user type. Printing that gave `struct inputs;`, which glslang reads
    // as the start of a definition and rejects with "unexpected SEMICOLON,
    // expecting LEFT_BRACE" - a message that names the punctuation and not the
    // type.
    [[nodiscard]] std::string type_name(const type & t) const {
        if (t.kind == base::struct_ && t.user >= 0 &&
            static_cast<std::size_t>(t.user) < m_->structs.size()) {
            return m_->structs[static_cast<std::size_t>(t.user)].name;
        }
        return spell(t);
    }

    [[nodiscard]] static std::string array_suffix(const type & t) {
        return t.array > 0 ? "[" + std::to_string(t.array) + "]" : (t.array < 0 ? "[]" : "");
    }

    // A UNIFORM BLOCK, put back together. The parser DISSOLVES a block without
    // an instance name into ordinary uniforms, because that is how the body
    // refers to them - so the block itself survives only in `shader::blocks`,
    // and this is the one place that has to read it.
    void print_blocks() {
        for (const shader::uniform_block & block : m_->blocks) {
            const std::uint32_t binding = binding_for(block.name);
            text_ += "layout(std140, binding = " + std::to_string(binding) + ") uniform " +
                     block.name + " {\n";
            for (const shader::uniform_block::member & each : block.members) {
                text_ += "    " + type_name(each.t) + " " + each.name + array_suffix(each.t) + ";\n";
            }
            text_ += "}";
            if (!block.instance.empty()) { text_ += " " + block.instance; }
            text_ += ";\n";
        }
    }

    // Every name a block already declared, so the globals pass does not declare
    // it a second time - the parser put block members in the global list too.
    [[nodiscard]] bool declared_by_a_block(const std::string & name) const {
        for (const shader::uniform_block & block : m_->blocks) {
            if (block.instance == name) { return true; }
            if (block.instance.empty()) {
                for (const shader::uniform_block::member & each : block.members) {
                    if (each.name == name) { return true; }
                }
            }
        }
        return false;
    }

    void print_globals() {
        int next_varying = 0;
        for (const std::int32_t which : m_->declarations) {
            const node & n = m_->at(which);
            if (n.kind != nk::var_decl) { continue; }
            if (declared_by_a_block(n.text)) { continue; }

            const std::string_view where = direction_of(n.store);
            if (where.empty()) {
                // An ordinary global with no storage qualifier.
                text_ += type_name(n.t) + " " + n.text + array_suffix(n.t);
                if (n.a >= 0) { text_ += " = " + expression(n.a); }
                text_ += ";\n";
                continue;
            }
            if (n.store == storage::uniform) {
                // A SAMPLER NEEDS A BINDING and an ordinary uniform must NOT
                // have one: outside a block, a plain uniform is a push constant
                // or a default-block member, and glslang rejects a binding on
                // it. Only opaque types take one.
                if (n.t.is_sampler()) {
                    text_ += "layout(binding = " + std::to_string(binding_for(n.text)) +
                             ") uniform " + type_name(n.t) + " " + n.text + array_suffix(n.t) + ";\n";
                    continue;
                }
                // NOT DECLARED HERE. Vulkan GLSL refuses a non-opaque uniform
                // outside a block, so it is remembered and printed into the
                // gathered one - see print_default_block.
                loose_.push_back(&n);
                continue;
            }
            if (n.store == storage::constant) {
                text_ += "const " + type_name(n.t) + " " + n.text + array_suffix(n.t);
                if (n.a >= 0) { text_ += " = " + expression(n.a); }
                text_ += ";\n";
                continue;
            }

            // AN IN OR AN OUT, and SPIR-V insists every one carries a location.
            //
            // An ATTRIBUTE's number is the engine's, because the page was told
            // it. A VARYING's is not visible to the page at all - it only has to
            // MATCH between the two stages - so they are numbered in declaration
            // order, which both stages walk identically.
            int location = -1;
            if (n.store == storage::attribute) {
                location = location_of(n.text);
                if (location < 0) {
                    fail("the attribute `" + n.text + "` has no location: the linker did not "
                         "assign one, so the SPIR-V could not agree with what getAttribLocation "
                         "told the page");
                    return;
                }
            } else {
                location = next_varying++;
            }
            text_ += "layout(location = " + std::to_string(location) + ") " + std::string{where} +
                     " " + type_name(n.t) + " " + n.text + array_suffix(n.t) + ";\n";
            if (n.store == storage::fragment_output) { fragment_out_ = n.text; }
        }

        // `gl_FragColor` DOES NOT EXIST IN ES 3.00. A shader that writes it has
        // no declared output at all, so one is invented - and every mention is
        // rewritten to it in `identifier` below. The name is deliberately one no
        // page can collide with.
        if (!is_vertex() && m_->fragment_output == "gl_FragColor" && fragment_out_.empty()) {
            fragment_out_ = "ctbrowser_FragColour";
            text_ += "layout(location = 0) out vec4 " + fragment_out_ + ";\n";
        }
    }

    void print_function(const node & fn) {
        text_ += type_name(fn.t) + " " + fn.text + "(";
        for (std::size_t i = 0; i < fn.kids.size(); ++i) {
            const node & p = m_->at(fn.kids[i]);
            if (i != 0) { text_ += ", "; }
            switch (p.dir) {
            case direction::out: text_ += "out "; break;
            case direction::inout: text_ += "inout "; break;
            default: break;
            }
            text_ += type_name(p.t) + " " + p.text + array_suffix(p.t);
        }
        text_ += ")";
        if (fn.a < 0) {
            text_ += ";\n"; // a prototype
            return;
        }
        text_ += " ";
        statement(fn.a, 0);
        text_ += "\n";
    }

    void indent(int depth) {
        for (int i = 0; i < depth; ++i) { text_ += "    "; }
    }

    void statement(std::int32_t which, int depth) {
        if (which < 0) {
            text_ += ";";
            return;
        }
        const node & n = m_->at(which);
        switch (n.kind) {
        case nk::block: {
            text_ += "{\n";
            for (const std::int32_t kid : n.kids) {
                indent(depth + 1);
                statement(kid, depth + 1);
                text_ += "\n";
            }
            indent(depth);
            text_ += "}";
            break;
        }
        case nk::var_decl: {
            text_ += type_name(n.t) + " " + n.text + array_suffix(n.t);
            if (n.a >= 0) { text_ += " = " + expression(n.a); }
            text_ += ";";
            break;
        }
        case nk::expr_stmt: text_ += expression(n.a) + ";"; break;
        case nk::if_stmt: {
            text_ += "if (" + expression(n.a) + ") ";
            statement(n.b, depth);
            if (n.c >= 0) {
                text_ += " else ";
                statement(n.c, depth);
            }
            break;
        }
        case nk::for_stmt: {
            text_ += "for (";
            // The initialiser is a STATEMENT and prints its own semicolon; the
            // condition and step are expressions and do not.
            if (n.a >= 0) {
                statement(n.a, depth);
            } else {
                text_ += ";";
            }
            text_ += " " + (n.b >= 0 ? expression(n.b) : std::string{}) + "; " +
                     (n.c >= 0 ? expression(n.c) : std::string{}) + ") ";
            statement(n.d, depth);
            break;
        }
        case nk::while_stmt:
            text_ += "while (" + expression(n.a) + ") ";
            statement(n.b, depth);
            break;
        case nk::do_stmt:
            text_ += "do ";
            statement(n.a, depth);
            text_ += " while (" + expression(n.b) + ");";
            break;
        case nk::return_stmt:
            text_ += "return";
            if (n.a >= 0) { text_ += " " + expression(n.a); }
            text_ += ";";
            break;
        case nk::break_stmt: text_ += "break;"; break;
        case nk::continue_stmt: text_ += "continue;"; break;
        case nk::discard_stmt: text_ += "discard;"; break;
        case nk::struct_def: break; // already printed with the other structs
        default: text_ += expression(which) + ";"; break;
        }
    }

    // FULLY PARENTHESISED, and that is deliberate. Re-deriving precedence while
    // printing is a second place for it to be wrong, and the only cost is a
    // shader nobody reads being uglier than the one the page wrote. glslang does
    // not care and neither does the SPIR-V.
    [[nodiscard]] std::string expression(std::int32_t which) {
        if (which < 0) { return {}; }
        const node & n = m_->at(which);
        switch (n.kind) {
        case nk::literal:
            if (n.t.kind == base::b) { return n.integer != 0 ? "true" : "false"; }
            if (n.t.kind == base::i) { return print_int(n.integer); }
            return print_float(n.number);
        case nk::identifier:
            // The one rewrite an expression needs: ES 1.00's built-in colour.
            if (n.text == "gl_FragColor" && !fragment_out_.empty()) { return fragment_out_; }
            return n.text;
        case nk::field: return "(" + expression(n.a) + ")." + n.text;
        case nk::index: return "(" + expression(n.a) + ")[" + expression(n.b) + "]";
        case nk::call: {
            std::string out{modern_name(n.text)};
            out += "(";
            for (std::size_t i = 0; i < n.kids.size(); ++i) {
                if (i != 0) { out += ", "; }
                out += expression(n.kids[i]);
            }
            return out + ")";
        }
        case nk::unary: return "(" + n.text + expression(n.a) + ")";
        case nk::prefix: return "(" + n.text + expression(n.a) + ")";
        case nk::postfix: return "(" + expression(n.a) + n.text + ")";
        case nk::binary: return "(" + expression(n.a) + " " + n.text + " " + expression(n.b) + ")";
        case nk::ternary:
            return "(" + expression(n.a) + " ? " + expression(n.b) + " : " + expression(n.c) + ")";
        case nk::assign:
            // NOT PARENTHESISED ON THE LEFT: `(a) = b` is not an lvalue.
            return expression(n.a) + " " + n.text + " " + expression(n.b);
        case nk::sequence: return "(" + expression(n.a) + ", " + expression(n.b) + ")";
        default: return {};
        }
    }
};

} // namespace

translation to_vulkan_glsl(const shader & parsed, const binding_tables & bindings) {
    translation out;
    if (!parsed.ok) {
        out.error = "the shader did not parse";
        return out;
    }
    printer print{parsed, bindings};
    return print.run();
}

} // namespace ctbrowser::raster::glsl
