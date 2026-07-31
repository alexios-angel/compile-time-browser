// The GLSL front end: preprocessor, lexer, parser.
//
// THE CORPUS IS THE TEST. tests/glsl/ holds the sixteen shaders p5.js actually
// ships, extracted by tools/gen-glsl-fixtures.py, plus the preamble p5 prepends
// to every one of them. They are somebody else's GLSL, written without any
// knowledge of this implementation - which is the only kind worth testing a
// parser against, because a parser tested on shaders written to suit it passes
// by construction.
//
// Reading that corpus is what decided the language subset: it is where the
// preprocessor, the structs, the function overloading and the uniform arrays in
// docs/webgl-plan.md came from, and none of them were in the first plan.
//
// The unit tests below it pin the things a corpus cannot: that a syntax error
// reports a LINE, that `#if` arithmetic works, and that the shapes come out
// right. Nothing here executes a shader - that is stage 2.

#include <ctbrowser/raster/raster.hpp>

#include "check.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace ctbrowser::raster;

namespace {

[[nodiscard]] std::string read_file(const std::filesystem::path & path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) { return {}; }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// Parse a fragment of GLSL with nothing prepended. For the unit tests.
[[nodiscard]] glsl::module parse_fragment(const std::string & source) {
    glsl::options how;
    how.which = glsl::stage::fragment;
    return glsl::parse(source, how);
}

[[nodiscard]] std::string preprocess_only(const std::string & source) {
    glsl::options how;
    std::vector<glsl::diagnostic> errors;
    return glsl::preprocess(source, how, errors);
}

// Every non-blank line of the output, joined - so a test can say what survived
// an #if without counting the blank lines the directives leave behind.
[[nodiscard]] std::string surviving(const std::string & text) {
    std::string out;
    std::istringstream lines{text};
    std::string line;
    while (std::getline(lines, line)) {
        const std::size_t first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos) { continue; }
        if (!out.empty()) { out += '|'; }
        out += line.substr(first, line.find_last_not_of(" \t\r") - first + 1);
    }
    return out;
}

// --- the corpus ------------------------------------------------------------

// THE BAR: every shader p5 ships parses, compiled the way p5 compiles it.
//
// p5 prepends webgl2CompatibilityShader and defines FRAGMENT_SHADER for a
// fragment shader; it defines WEBGL2 only when it got a WebGL 2 context, which
// this engine never gives it. So the text here is the text a real p5 run
// produces, and `IN` expands to `attribute` or `varying` accordingly.
void test_the_p5_corpus_parses() {
    const std::filesystem::path dir{"tests/glsl"};
    const std::string preamble = read_file(dir / "preamble.glsl");
    CHECK(!preamble.empty());

    int seen = 0;
    for (const auto & entry : std::filesystem::directory_iterator{dir}) {
        const std::filesystem::path & path = entry.path();
        const bool vertex = path.extension() == ".vert";
        if (!vertex && path.extension() != ".frag") { continue; }
        ++seen;

        glsl::options how;
        how.which = vertex ? glsl::stage::vertex : glsl::stage::fragment;
        const glsl::module m = glsl::parse(preamble + "\n" + read_file(path), how);
        if (!m.ok) {
            std::printf("FAIL %s did not parse:\n%s", path.filename().string().c_str(),
                        m.info_log().c_str());
            ++ctbrowser_test_failures;
            continue;
        }
        // Parsing without declaring anything would "pass" while doing nothing.
        // Every one of these shaders has a main() and at least one input.
        bool has_main = false;
        for (const std::int32_t which : m.declarations) {
            const glsl::node & n = m.at(which);
            if (n.kind == glsl::nk::function && n.text == "main" && n.a >= 0) { has_main = true; }
        }
        if (!has_main) {
            std::printf("FAIL %s parsed but declared no main()\n", path.filename().string().c_str());
            ++ctbrowser_test_failures;
        }
    }
    // The count is asserted so a broken extractor cannot make this pass by
    // finding nothing to check.
    std::printf("     glsl corpus: %d shaders parsed\n", seen);
    CHECK(seen == 16);

    // AND THE PARSE IS NOT VACUOUS. A parser that returned an empty module for
    // everything would satisfy every check above. phongVert is the corpus's
    // hardest vertex shader - a struct, a preprocessor branch, eleven uniforms -
    // so its shape is pinned by hand.
    glsl::options how;
    how.which = glsl::stage::vertex;
    const glsl::module phong = glsl::parse(preamble + "\n" + read_file(dir / "phongVert.vert"), how);
    CHECK(phong.ok);
    CHECK(phong.structs.size() == 1);
    if (!phong.structs.empty()) {
        CHECK(phong.structs.front().name == "Vertex");
        CHECK(phong.structs.front().members.size() == 4);
    }
    int uniforms = 0;
    int attributes = 0;
    int varyings = 0;
    for (const glsl::interface_variable & v : phong.interface_) {
        uniforms += v.store == glsl::storage::uniform ? 1 : 0;
        attributes += v.store == glsl::storage::attribute ? 1 : 0;
        varyings += v.store == glsl::storage::varying ? 1 : 0;
    }
    // Counted from the source with the preprocessor's branches taken by hand:
    // four `IN` attributes and five `OUT` varyings, and FIVE uniforms -
    // uModelViewMatrix and uNormalMatrix from the #else arm, plus
    // uProjectionMatrix, uUseVertexColor and uMaterialColor.
    //
    // The five is the interesting number. The #ifdef arm declares four DIFFERENT
    // uniforms (uModelMatrix, uViewMatrix and two normal matrices), so a
    // preprocessor that took the wrong branch, or took both, would report seven
    // or nine here and still parse cleanly. This is the assertion that says the
    // conditional actually decided something.
    CHECK(attributes == 4);
    CHECK(varyings == 5);
    CHECK(uniforms == 5);
    CHECK(phong.nodes.size() > 100);
}

// The preamble decides what `IN` MEANS, and it is different in the two stages:
// `attribute` in a vertex shader and `varying` in a fragment one. Getting it
// backwards gives every input the wrong storage, which stage 4 would then feed
// from the wrong place.
void test_the_preamble_decides_storage() {
    const std::string preamble = read_file("tests/glsl/preamble.glsl");
    const std::string body = "IN vec3 aPosition;\nvoid main() { gl_Position = vec4(aPosition, 1.); }";

    glsl::options as_vertex;
    as_vertex.which = glsl::stage::vertex;
    const glsl::module v = glsl::parse(preamble + "\n" + body, as_vertex);
    CHECK(v.ok);
    CHECK(v.interface_.size() >= 1);
    if (!v.interface_.empty()) {
        CHECK(v.interface_.front().name == "aPosition");
        CHECK(v.interface_.front().store == glsl::storage::attribute);
    }

    glsl::options as_fragment;
    as_fragment.which = glsl::stage::fragment;
    const glsl::module f = glsl::parse(preamble + "\n" + body, as_fragment);
    CHECK(f.ok);
    if (!f.interface_.empty()) {
        CHECK(f.interface_.front().store == glsl::storage::varying);
    }
}

// --- the preprocessor ------------------------------------------------------

void test_conditionals() {
    // The plain ones.
    CHECK(surviving(preprocess_only("#define A\n#ifdef A\nkept\n#else\ngone\n#endif")) == "kept");
    CHECK(surviving(preprocess_only("#ifdef A\ngone\n#else\nkept\n#endif")) == "kept");
    CHECK(surviving(preprocess_only("#ifndef A\nkept\n#endif")) == "kept");
    // `#if 0` and `#if 1`, which is how p5's font shader picks its integer maths.
    CHECK(surviving(preprocess_only("#if 0\ngone\n#else\nkept\n#endif")) == "kept");
    CHECK(surviving(preprocess_only("#if 1\nkept\n#endif")) == "kept");
    // An UNDEFINED NAME IS ZERO - the C rule, and what makes `#if UNSET` behave
    // like `#if 0` rather than like an error.
    CHECK(surviving(preprocess_only("#if UNSET\ngone\n#else\nkept\n#endif")) == "kept");
    // Arithmetic and precedence.
    CHECK(surviving(preprocess_only("#if 2 + 3 * 4 == 14\nkept\n#endif")) == "kept");
    CHECK(surviving(preprocess_only("#if (2 + 3) * 4 == 20\nkept\n#endif")) == "kept");
    CHECK(surviving(preprocess_only("#if defined(A) || 1\nkept\n#endif")) == "kept");
    CHECK(surviving(preprocess_only("#define V 3\n#if V > 2 && V < 5\nkept\n#endif")) == "kept");
    // ONLY ONE ARM OF A CHAIN RUNS. Without the `taken` flag an #elif after a
    // branch that already ran would run too.
    CHECK(surviving(preprocess_only(
              "#if 1\none\n#elif 1\ntwo\n#else\nthree\n#endif")) == "one");
    CHECK(surviving(preprocess_only(
              "#if 0\none\n#elif 1\ntwo\n#else\nthree\n#endif")) == "two");
    CHECK(surviving(preprocess_only(
              "#if 0\none\n#elif 0\ntwo\n#else\nthree\n#endif")) == "three");
    // Nesting, including a nested #if inside a SKIPPED block - which still has
    // to find its own #endif or everything after it is misplaced.
    CHECK(surviving(preprocess_only(
              "#if 0\n#ifdef X\ngone\n#endif\ngone\n#endif\nkept")) == "kept");
    CHECK(surviving(preprocess_only(
              "#if 1\n#if 1\nboth\n#endif\n#endif")) == "both");
}

void test_macros() {
    CHECK(surviving(preprocess_only("#define N 4\nint x = N;")) == "int x = 4;");
    // FUNCTION-LIKE, which p5 uses for `#define INT(x) float(x)`.
    CHECK(surviving(preprocess_only("#define TWICE(x) ((x)+(x))\nTWICE(3)")) == "((3)+(3))");
    CHECK(surviving(preprocess_only("#define A(x,y) x*y\nA(2, 3)")) == "2*3");
    // Nested arguments: a comma inside parens does not separate arguments.
    CHECK(surviving(preprocess_only("#define F(x) [x]\nF(g(1, 2))")) == "[g(1, 2)]");
    // A function-like macro NOT followed by a paren is just its name, which is
    // how a shader passes one around.
    CHECK(surviving(preprocess_only("#define F(x) [x]\nF")) == "F");
    // Expansion is repeated until nothing changes.
    CHECK(surviving(preprocess_only("#define A B\n#define B 7\nA")) == "7");
    // A macro may rename a TYPE, which is exactly what p5's font shader does
    // with `#define int float`.
    CHECK(surviving(preprocess_only("#define int float\nint x;")) == "float x;");
    CHECK(surviving(preprocess_only("#define N 2\n#undef N\nN")) == "N");
    // SELF-REFERENCE MUST NOT HANG. A page's mistake is not a reason to stop
    // responding; the depth cap turns it into a finite, wrong answer.
    const std::string looping = preprocess_only("#define A A + 1\nA");
    CHECK(!looping.empty());
}

// LINE NUMBERS SURVIVE. A directive and a skipped block both leave their
// newlines behind, so a diagnostic points at the line the author wrote. Emitting
// nothing would be simpler and would misreport every error after an #if.
void test_line_numbers_survive() {
    const std::string source = "#define A 1\n#if 0\ngone\ngone\n#endif\nvec3 x = ;\n";
    const glsl::module m = parse_fragment(source);
    CHECK(!m.ok);
    CHECK(!m.errors.empty());
    if (!m.errors.empty()) { CHECK(m.errors.front().line == 6); }
}

// A comment must not swallow a directive or a line.
void test_comments() {
    CHECK(surviving(preprocess_only("#define A 1 // a note\nA")) == "1");
    CHECK(surviving(preprocess_only("kept // gone\n")) == "kept");
    CHECK(surviving(preprocess_only("/* gone\ngone */ kept")) == "kept");
    // A block comment spanning lines keeps the lines, so what follows it is
    // still where it was.
    const std::string spanned = preprocess_only("/*\n\n*/x");
    CHECK(std::count(spanned.begin(), spanned.end(), '\n') >= 2);
}

// --- the parser ------------------------------------------------------------

void test_declarations_and_shapes() {
    const glsl::module m = parse_fragment(R"(
        uniform mat4 uProjectionMatrix;
        uniform sampler2D uSampler;
        uniform vec3 uLights[8];
        varying vec2 vTexCoord;
        attribute vec4 aColor;
        const float k = 2.0;
        void main() { gl_FragColor = vec4(1.0); }
    )");
    CHECK(m.ok);
    if (!m.ok) {
        std::printf("%s", m.info_log().c_str());
        return;
    }
    // `const` is not part of the interface - it reaches nothing outside.
    CHECK(m.interface_.size() == 5);
    const auto find = [&](std::string_view name) -> const glsl::interface_variable * {
        for (const glsl::interface_variable & v : m.interface_) {
            if (v.name == name) { return &v; }
        }
        return nullptr;
    };
    const glsl::interface_variable * projection = find("uProjectionMatrix");
    CHECK(projection != nullptr);
    if (projection != nullptr) {
        CHECK(projection->store == glsl::storage::uniform);
        CHECK(projection->t.is_matrix());
        CHECK(projection->t.cols == 4);
        CHECK(projection->t.components() == 16);
    }
    const glsl::interface_variable * sampler = find("uSampler");
    CHECK(sampler != nullptr && sampler->t.is_sampler());
    const glsl::interface_variable * lights = find("uLights");
    CHECK(lights != nullptr);
    if (lights != nullptr) {
        CHECK(lights->t.array == 8);
        // An array of vec3 is 24 floats, which is what the uniform upload needs.
        CHECK(lights->t.components() == 24);
    }
    CHECK(find("vTexCoord") != nullptr && find("vTexCoord")->store == glsl::storage::varying);
    CHECK(find("aColor") != nullptr && find("aColor")->store == glsl::storage::attribute);
    CHECK(glsl::spell(find("aColor")->t) == "vec4");
    CHECK(glsl::spell(projection->t) == "mat4");
}

// Structs, which p5's phong and normal shaders both declare and pass through
// functions. The first plan named them as out of scope; the corpus said
// otherwise.
void test_structs() {
    const glsl::module m = parse_fragment(R"(
        struct Vertex {
          vec3 position;
          vec3 normal;
          vec2 texCoord;
          vec4 color;
        };
        Vertex passthrough(Vertex v) { return v; }
        void main() {
          Vertex inputs;
          inputs.position = vec3(0.0);
          gl_FragColor = passthrough(inputs).color;
        }
    )");
    CHECK(m.ok);
    if (!m.ok) {
        std::printf("%s", m.info_log().c_str());
        return;
    }
    CHECK(m.structs.size() == 1);
    if (!m.structs.empty()) {
        CHECK(m.structs.front().name == "Vertex");
        CHECK(m.structs.front().members.size() == 4);
        CHECK(m.structs.front().members[2].name == "texCoord");
        CHECK(m.structs.front().members[2].t.rows == 2);
    }
}

// A function may be declared more than once with different parameter types -
// p5's font shader has three `ifloor`. The parser records each; choosing between
// them is stage 2's job, and it cannot choose from a list it never got.
void test_overloads_are_all_recorded() {
    const glsl::module m = parse_fragment(R"(
        int ifloor(float v) { return int(v); }
        int ifloor(int v) { return v; }
        ivec2 ifloor(vec2 v) { return ivec2(v); }
        void main() { gl_FragColor = vec4(float(ifloor(1.5))); }
    )");
    CHECK(m.ok);
    int found = 0;
    for (const std::int32_t which : m.declarations) {
        const glsl::node & n = m.at(which);
        if (n.kind == glsl::nk::function && n.text == "ifloor") { ++found; }
    }
    CHECK(found == 3);
}

void test_statements_and_expressions() {
    const glsl::module m = parse_fragment(R"(
        uniform int uCount;
        uniform vec3 uColors[8];
        vec3 total(void) {
          vec3 sum = vec3(0.0);
          for (int i = 0; i < 8; i++) {
            if (i < uCount) { sum += uColors[i]; } else { continue; }
          }
          int n = 0;
          while (n < 3) { n++; }
          do { n--; } while (n > 0);
          return sum;
        }
        void main() {
          vec3 c = total();
          float a = c.r > 0.5 ? 1.0 : 0.0;
          c.rgb *= a;
          if (a == 0.0) { discard; }
          gl_FragColor = vec4(c, a);
        }
    )");
    CHECK(m.ok);
    if (!m.ok) { std::printf("%s", m.info_log().c_str()); }
}

// A SHADER IS UNTRUSTED TEXT. Every one of these is malformed, and every one has
// to come back as a diagnostic rather than a crash or a hang - which is why the
// parser advances on a shape it does not understand instead of looping.
void test_malformed_shaders_are_diagnosed_not_fatal() {
    for (const char * bad : {
             "",
             "void main() {",
             "void main() { gl_FragColor = ; }",
             "vec3 = 1.0;",
             "void main() { if (   }",
             "struct { float a; ",
             "#if\n#endif",
             "#endif",
             "#if 1",
             "uniform vec3 x[;\nvoid main() {}",
             "void main() { for (;;) { } }",
             "((((((((((",
             "}}}}",
             "void main() { x[[[ }",
             "#define A(\nA(1)",
         }) {
        const glsl::module m = parse_fragment(bad);
        // Not asserting that each FAILS - `for(;;){}` is valid, and an empty
        // shader has no syntax error either. Asserting that each one RETURNS,
        // and that a failure always carries a message a page could show.
        if (!m.ok) { CHECK(!m.info_log().empty()); }
    }
    // And one that must definitely fail, so the loop above cannot pass by
    // accepting everything.
    CHECK(!parse_fragment("void main() { gl_FragColor = ; }").ok);
}

} // namespace

int main() {
    test_the_p5_corpus_parses();
    test_the_preamble_decides_storage();
    test_conditionals();
    test_macros();
    test_line_numbers_survive();
    test_comments();
    test_declarations_and_shapes();
    test_structs();
    test_overloads_are_all_recorded();
    test_statements_and_expressions();
    test_malformed_shaders_are_diagnosed_not_fatal();
    REPORT("glsl_basics");
}
