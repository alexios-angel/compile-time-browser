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

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
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
[[nodiscard]] glsl::shader parse_fragment(const std::string & source) {
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
        const glsl::shader m = glsl::parse(preamble + "\n" + read_file(path), how);
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
            std::printf("FAIL %s parsed but declared no main()\n",
                        path.filename().string().c_str());
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
    const glsl::shader phong =
        glsl::parse(preamble + "\n" + read_file(dir / "phongVert.vert"), how);
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
    const std::string body =
        "IN vec3 aPosition;\nvoid main() { gl_Position = vec4(aPosition, 1.); }";

    glsl::options as_vertex;
    as_vertex.which = glsl::stage::vertex;
    const glsl::shader v = glsl::parse(preamble + "\n" + body, as_vertex);
    CHECK(v.ok);
    CHECK(v.interface_.size() >= 1);
    if (!v.interface_.empty()) {
        CHECK(v.interface_.front().name == "aPosition");
        CHECK(v.interface_.front().store == glsl::storage::attribute);
    }

    glsl::options as_fragment;
    as_fragment.which = glsl::stage::fragment;
    const glsl::shader f = glsl::parse(preamble + "\n" + body, as_fragment);
    CHECK(f.ok);
    if (!f.interface_.empty()) { CHECK(f.interface_.front().store == glsl::storage::varying); }
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
    CHECK(surviving(preprocess_only("#if 1\none\n#elif 1\ntwo\n#else\nthree\n#endif")) == "one");
    CHECK(surviving(preprocess_only("#if 0\none\n#elif 1\ntwo\n#else\nthree\n#endif")) == "two");
    CHECK(surviving(preprocess_only("#if 0\none\n#elif 0\ntwo\n#else\nthree\n#endif")) == "three");
    // Nesting, including a nested #if inside a SKIPPED block - which still has
    // to find its own #endif or everything after it is misplaced.
    CHECK(surviving(preprocess_only("#if 0\n#ifdef X\ngone\n#endif\ngone\n#endif\nkept")) ==
          "kept");
    CHECK(surviving(preprocess_only("#if 1\n#if 1\nboth\n#endif\n#endif")) == "both");
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
    const glsl::shader m = parse_fragment(source);
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
    const glsl::shader m = parse_fragment(R"(
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
    const glsl::shader m = parse_fragment(R"(
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
    const glsl::shader m = parse_fragment(R"(
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
    const glsl::shader m = parse_fragment(R"(
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
        const glsl::shader m = parse_fragment(bad);
        // Not asserting that each FAILS - `for(;;){}` is valid, and an empty
        // shader has no syntax error either. Asserting that each one RETURNS,
        // and that a failure always carries a message a page could show.
        if (!m.ok) { CHECK(!m.info_log().empty()); }
    }
    // And one that must definitely fail, so the loop above cannot pass by
    // accepting everything.
    CHECK(!parse_fragment("void main() { gl_FragColor = ; }").ok);
}

// --- the reference evaluator -----------------------------------------------

// Run a fragment shader body and read back one float from gl_FragColor.
[[nodiscard]] glsl::execution run_fragment(const std::string & body,
                                           const glsl::environment & env) {
    const glsl::shader m = parse_fragment("void main() {\n" + body + "\n}");
    if (!m.ok) {
        std::printf("FAIL the test shader did not compile:\n%s%s\n", m.info_log().c_str(),
                    body.c_str());
        ++ctbrowser_test_failures;
        return {};
    }
    return glsl::execute(m, env);
}

// `gl_FragColor = vec4(<expr>);` and read component 0 back. The shortest way to
// ask "what does this expression evaluate to".
[[nodiscard]] float value_of(const std::string & expression) {
    glsl::environment env;
    const glsl::execution ran = run_fragment("gl_FragColor = vec4(" + expression + ");", env);
    if (!ran.ok) {
        std::printf("FAIL `%s` did not run: %s\n", expression.c_str(), ran.error.c_str());
        ++ctbrowser_test_failures;
        return 0;
    }
    const glsl::value * colour = ran.find("gl_FragColor");
    return colour == nullptr ? 0.0f : colour->f(0);
}

void expect_value(const std::string & expression, float want) {
    const float got = value_of(expression);
    // EXACT, not approximate. Every float result here has to be bit-identical
    // across platforms because the goldens are byte-compared, so a test that
    // tolerates drift would hide exactly the thing that matters. The
    // expectations are chosen to be exactly representable.
    if (got != want) {
        std::printf("FAIL `%s` = %.9g, want %.9g\n", expression.c_str(), static_cast<double>(got),
                    static_cast<double>(want));
        ++ctbrowser_test_failures;
    }
}

// THE RULE THAT LOOKS ALMOST RIGHT WHEN IT IS WRONG.
//
// A matrix is COLUMN-MAJOR: `m[i]` is a column, and `m * v` is the sum of the
// columns scaled by v's components. Transposing it still produces a picture -
// a plausible wrong rotation - which is why it gets a test of its own with a
// matrix that is not symmetric.
void test_matrices_are_column_major() {
    // mat2(a, b, c, d) fills COLUMNS: first column (a, b), second (c, d).
    expect_value("mat2(1.0, 2.0, 3.0, 4.0)[0].x", 1.0f);
    expect_value("mat2(1.0, 2.0, 3.0, 4.0)[0].y", 2.0f);
    expect_value("mat2(1.0, 2.0, 3.0, 4.0)[1].x", 3.0f);
    // m * v with a NON-SYMMETRIC matrix, so transposing changes the answer:
    // columns (1,2) and (3,4), times (10, 20) = (1*10 + 3*20, 2*10 + 4*20).
    expect_value("(mat2(1.0, 2.0, 3.0, 4.0) * vec2(10.0, 20.0)).x", 70.0f);
    expect_value("(mat2(1.0, 2.0, 3.0, 4.0) * vec2(10.0, 20.0)).y", 100.0f);
    // v * m is the OTHER product and must differ: (10,20) dotted with each
    // column = (10*1 + 20*2, 10*3 + 20*4).
    expect_value("(vec2(10.0, 20.0) * mat2(1.0, 2.0, 3.0, 4.0)).x", 50.0f);
    expect_value("(vec2(10.0, 20.0) * mat2(1.0, 2.0, 3.0, 4.0)).y", 110.0f);
    // `mat4(1.0)` is the IDENTITY scaled, not sixteen ones - the most
    // surprising constructor in the language.
    expect_value("(mat4(1.0) * vec4(5.0, 6.0, 7.0, 1.0)).z", 7.0f);
    expect_value("mat4(2.0)[1].y", 2.0f);
    expect_value("mat4(2.0)[1].x", 0.0f);
    // A matrix product, checked against the same result computed by hand.
    expect_value("(mat2(1.0, 2.0, 3.0, 4.0) * mat2(5.0, 6.0, 7.0, 8.0))[0].x", 23.0f);
    expect_value("(mat2(1.0, 2.0, 3.0, 4.0) * mat2(5.0, 6.0, 7.0, 8.0))[0].y", 34.0f);
    // mat3(mat4) takes the top-left corner.
    expect_value("mat3(mat4(3.0))[2].z", 3.0f);
}

// `int / int` TRUNCATES. Getting this wrong shifts a texel index by one only
// sometimes, which is the worst kind of wrong.
void test_integer_arithmetic() {
    expect_value("float(7 / 2)", 3.0f);
    expect_value("float(-7 / 2)", -3.0f); // toward zero, not toward -infinity
    expect_value("7.0 / 2.0", 3.5f);
    // A float divisor makes the whole expression float.
    expect_value("float(7) / 2.0", 3.5f);
    expect_value("float(7 % 3)", 1.0f);
    // DIVISION BY ZERO IS ZERO rather than a trap: a shader is a page's text
    // and must not be able to fault the process.
    expect_value("float(1 / 0)", 0.0f);
    expect_value("1.0 / 0.0", 0.0f);
    expect_value("float(int(2.9))", 2.0f);
    expect_value("float(int(-2.9))", -2.0f);
}

void test_swizzles() {
    expect_value("vec4(1.0, 2.0, 3.0, 4.0).z", 3.0f);
    expect_value("vec4(1.0, 2.0, 3.0, 4.0).b", 3.0f); // rgba spells the same places
    expect_value("vec4(1.0, 2.0, 3.0, 4.0).p", 3.0f); // and so does stpq
    // A swizzle may REORDER and REPEAT.
    expect_value("vec4(1.0, 2.0, 3.0, 4.0).wzyx.x", 4.0f);
    expect_value("vec3(1.0, 2.0, 3.0).xxx.z", 1.0f);
    // Writing through one is next door, in test_swizzle_assignment - it needs
    // statements rather than an expression.
}

void test_swizzle_assignment() {
    glsl::environment env;
    const glsl::execution ran = run_fragment("vec4 v = vec4(0.0);\n"
                                             "v.xz = vec2(7.0, 9.0);\n"
                                             "v.a = 1.0;\n"
                                             "gl_FragColor = v;",
                                             env);
    CHECK(ran.ok);
    const glsl::value * colour = ran.find("gl_FragColor");
    CHECK(colour != nullptr);
    if (colour != nullptr) {
        CHECK(colour->f(0) == 7.0f);
        CHECK(colour->f(1) == 0.0f); // untouched
        CHECK(colour->f(2) == 9.0f);
        CHECK(colour->f(3) == 1.0f);
    }
    // A SCALAR SPREADS across the named components.
    const glsl::execution spread =
        run_fragment("vec4 v = vec4(0.0); v.rgb = vec3(0.5); gl_FragColor = v;", env);
    CHECK(spread.ok);
    if (const glsl::value * c = spread.find("gl_FragColor")) {
        CHECK(c->f(0) == 0.5f && c->f(1) == 0.5f && c->f(2) == 0.5f && c->f(3) == 0.0f);
    }
}

void test_builtins() {
    expect_value("length(vec3(3.0, 4.0, 0.0))", 5.0f);
    expect_value("dot(vec3(1.0, 2.0, 3.0), vec3(4.0, 5.0, 6.0))", 32.0f);
    expect_value("cross(vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0)).z", 1.0f);
    expect_value("normalize(vec3(0.0, 5.0, 0.0)).y", 1.0f);
    // A ZERO VECTOR NORMALISES TO ZERO, not to NaN - a NaN spreads through the
    // rest of the shader and comes out as a missing pixel with nothing to say why.
    expect_value("normalize(vec3(0.0)).x", 0.0f);
    expect_value("mix(0.0, 10.0, 0.25)", 2.5f);
    expect_value("clamp(5.0, 0.0, 1.0)", 1.0f);
    expect_value("step(0.5, 0.25)", 0.0f);
    expect_value("step(0.5, 0.75)", 1.0f);
    expect_value("smoothstep(0.0, 1.0, 0.5)", 0.5f);
    expect_value("abs(-2.5)", 2.5f);
    expect_value("sign(-3.0)", -1.0f);
    expect_value("floor(-1.5)", -2.0f);
    expect_value("ceil(-1.5)", -1.0f);
    // `fract` is x - floor(x), so a negative x does NOT give the C fractional part.
    expect_value("fract(-0.25)", 0.75f);
    expect_value("mod(-1.0, 4.0)", 3.0f);             // and mod follows floor, not fmod
    expect_value("max(vec2(1.0, 5.0), 3.0).y", 5.0f); // a scalar broadcasts
    expect_value("min(vec2(1.0, 5.0), 3.0).x", 1.0f);
    expect_value("pow(2.0, 10.0)", 1024.0f);
    expect_value("sqrt(16.0)", 4.0f);
    // A NEGATIVE SQRT IS ZERO, for the same reason as normalize.
    expect_value("sqrt(-1.0)", 0.0f);
    expect_value("float(all(bvec2(true, true)))", 1.0f);
    expect_value("float(any(bvec2(false, false)))", 0.0f);
    expect_value("float(lessThan(vec2(1.0, 5.0), vec2(3.0, 3.0)).x)", 1.0f);
    expect_value("reflect(vec2(1.0, -1.0), vec2(0.0, 1.0)).y", 1.0f);
    expect_value("distance(vec2(0.0, 0.0), vec2(3.0, 4.0))", 5.0f);
}

void test_control_flow_and_functions() {
    glsl::environment env;
    // A loop accumulating into a local, which is p5's ambient-light shape.
    CHECK(value_of("0.0") == 0.0f);
    const glsl::execution loop = run_fragment("float sum = 0.0;\n"
                                              "for (int i = 0; i < 4; i++) { sum += float(i); }\n"
                                              "gl_FragColor = vec4(sum);",
                                              env);
    CHECK(loop.ok);
    if (const glsl::value * c = loop.find("gl_FragColor")) { CHECK(c->f(0) == 6.0f); }

    // `break` and `continue`.
    const glsl::execution jumps = run_fragment("float sum = 0.0;\n"
                                               "for (int i = 0; i < 10; i++) {\n"
                                               "  if (i == 2) { continue; }\n"
                                               "  if (i == 4) { break; }\n"
                                               "  sum += float(i);\n"
                                               "}\n"
                                               "gl_FragColor = vec4(sum);",
                                               env);
    CHECK(jumps.ok);
    if (const glsl::value * c = jumps.find("gl_FragColor")) { CHECK(c->f(0) == 4.0f); }
}

// A shader may declare the same function name several times with different
// parameter types. p5's font shader has three `ifloor`, so picking the first by
// name would call the wrong one two times in three.
void test_overload_resolution() {
    const glsl::shader m = parse_fragment(R"(
        float pick(float v) { return 1.0; }
        float pick(vec2 v) { return 2.0; }
        float pick(vec3 v) { return 3.0; }
        void main() {
          gl_FragColor = vec4(pick(0.0), pick(vec2(0.0)), pick(vec3(0.0)), 0.0);
        }
    )");
    CHECK(m.ok);
    glsl::environment env;
    const glsl::execution ran = glsl::execute(m, env);
    CHECK(ran.ok);
    if (const glsl::value * c = ran.find("gl_FragColor")) {
        CHECK(c->f(0) == 1.0f);
        CHECK(c->f(1) == 2.0f);
        CHECK(c->f(2) == 3.0f);
    }
}

// `out` and `inout` parameters are copied BACK. GLSL passes by value, so a
// callee that writes one has written a copy until the call returns.
void test_out_parameters() {
    const glsl::shader m = parse_fragment(R"(
        void addTo(inout float total, in float by) { total += by; }
        void split(float v, out float half1, out float half2) { half1 = v * 0.5; half2 = v * 0.5; }
        void main() {
          float total = 1.0;
          addTo(total, 4.0);
          float a; float b;
          split(10.0, a, b);
          gl_FragColor = vec4(total, a, b, 0.0);
        }
    )");
    CHECK(m.ok);
    glsl::environment env;
    const glsl::execution ran = glsl::execute(m, env);
    CHECK(ran.ok);
    if (!ran.ok) { std::printf("     %s\n", ran.error.c_str()); }
    if (const glsl::value * c = ran.find("gl_FragColor")) {
        CHECK(c->f(0) == 5.0f);
        CHECK(c->f(1) == 5.0f);
        CHECK(c->f(2) == 5.0f);
    }
}

void test_structs_at_runtime() {
    const glsl::shader m = parse_fragment(R"(
        struct Vertex { vec3 position; vec3 normal; vec2 texCoord; vec4 color; };
        Vertex scale(Vertex v, float by) { v.position = v.position * by; return v; }
        void main() {
          Vertex inputs;
          inputs.position = vec3(1.0, 2.0, 3.0);
          inputs.color = vec4(0.25);
          Vertex bigger = scale(inputs, 10.0);
          gl_FragColor = vec4(bigger.position.y, bigger.color.r, inputs.position.y, 0.0);
        }
    )");
    CHECK(m.ok);
    if (!m.ok) { std::printf("%s", m.info_log().c_str()); }
    glsl::environment env;
    const glsl::execution ran = glsl::execute(m, env);
    CHECK(ran.ok);
    if (!ran.ok) { std::printf("     %s\n", ran.error.c_str()); }
    if (const glsl::value * c = ran.find("gl_FragColor")) {
        CHECK(c->f(0) == 20.0f); // scaled
        CHECK(c->f(1) == 0.25f); // the other members survived the round trip
        // PASSED BY VALUE: the caller's struct is untouched.
        CHECK(c->f(2) == 2.0f);
    }
}

// Uniforms, attributes and varyings arrive through the environment, and a
// uniform ARRAY is indexed by a loop - which is exactly p5's light shaders.
void test_uniforms_and_arrays() {
    const glsl::shader m = parse_fragment(R"(
        uniform vec3 uColors[4];
        uniform int uCount;
        uniform mat4 uProjectionMatrix;
        void main() {
          vec3 sum = vec3(0.0);
          for (int i = 0; i < 4; i++) {
            if (i < uCount) { sum += uColors[i]; }
          }
          gl_FragColor = vec4(sum, (uProjectionMatrix * vec4(0.0, 0.0, 0.0, 1.0)).x);
        }
    )");
    CHECK(m.ok);

    std::unordered_map<std::string, glsl::value> uniforms;
    glsl::value colours;
    colours.t = glsl::type{glsl::base::f, 3, 1, -1, 4};
    colours.v = {1, 0, 0, 0, 1, 0, 0, 0, 1, 9, 9, 9}; // the fourth is past uCount
    uniforms["uColors"] = colours;
    uniforms["uCount"] = glsl::value::integer(3);
    glsl::value projection;
    projection.t = glsl::type{glsl::base::f, 4, 4, -1, 0};
    projection.v.assign(16, 0.0f);
    for (int i = 0; i < 4; ++i) { projection.v[static_cast<std::size_t>(i * 4 + i)] = 1.0f; }
    projection.v[12] = 5.0f; // the translation column, which is what column-major means
    uniforms["uProjectionMatrix"] = projection;

    glsl::environment env;
    env.read = [&uniforms](std::string_view name) -> const glsl::value * {
        const auto found = uniforms.find(std::string{name});
        return found == uniforms.end() ? nullptr : &found->second;
    };
    const glsl::execution ran = glsl::execute(m, env);
    CHECK(ran.ok);
    if (!ran.ok) { std::printf("     %s\n", ran.error.c_str()); }
    if (const glsl::value * c = ran.find("gl_FragColor")) {
        CHECK(c->f(0) == 1.0f);
        CHECK(c->f(1) == 1.0f);
        CHECK(c->f(2) == 1.0f);
        // The translation reached the right place, which only happens if the
        // matrix is read column-major.
        CHECK(c->f(3) == 5.0f);
    }
}

// `discard` is not an error: the fragment is simply not written. Reporting it
// through `ok` would confuse it with a shader that failed.
void test_discard() {
    glsl::environment env;
    const glsl::execution ran =
        run_fragment("if (true) { discard; }\ngl_FragColor = vec4(1.0);", env);
    CHECK(ran.ok);
    CHECK(ran.discarded);
    const glsl::execution kept = run_fragment("gl_FragColor = vec4(1.0);", env);
    CHECK(kept.ok);
    CHECK(!kept.discarded);
}

// A vertex shader's varyings are OUTPUTS it writes; a fragment shader's are
// INPUTS it reads. The same declaration, opposite directions.
void test_a_vertex_shader_publishes_its_varyings() {
    glsl::options how;
    how.which = glsl::stage::vertex;
    const glsl::shader m = glsl::parse(R"(
        attribute vec3 aPosition;
        uniform mat4 uProjectionMatrix;
        varying vec2 vTexCoord;
        varying vec4 vColor;
        void main() {
          vTexCoord = aPosition.xy;
          vColor = vec4(1.0, 0.5, 0.0, 1.0);
          gl_Position = uProjectionMatrix * vec4(aPosition, 1.0);
        }
    )",
                                       how);
    CHECK(m.ok);

    std::unordered_map<std::string, glsl::value> inputs;
    inputs["aPosition"] = glsl::value::vector({2.0f, 3.0f, 4.0f});
    glsl::value identity;
    identity.t = glsl::type{glsl::base::f, 4, 4, -1, 0};
    identity.v.assign(16, 0.0f);
    for (int i = 0; i < 4; ++i) { identity.v[static_cast<std::size_t>(i * 4 + i)] = 1.0f; }
    inputs["uProjectionMatrix"] = identity;

    glsl::environment env;
    env.read = [&inputs](std::string_view name) -> const glsl::value * {
        const auto found = inputs.find(std::string{name});
        return found == inputs.end() ? nullptr : &found->second;
    };
    const glsl::execution ran = glsl::execute(m, env);
    CHECK(ran.ok);
    if (!ran.ok) { std::printf("     %s\n", ran.error.c_str()); }
    const glsl::value * position = ran.find("gl_Position");
    CHECK(position != nullptr);
    if (position != nullptr) {
        CHECK(position->f(0) == 2.0f && position->f(1) == 3.0f && position->f(2) == 4.0f);
        CHECK(position->f(3) == 1.0f);
    }
    // The varyings came out too, which is what stage 3 interpolates.
    const glsl::value * coord = ran.find("vTexCoord");
    CHECK(coord != nullptr);
    if (coord != nullptr) { CHECK(coord->f(0) == 2.0f && coord->f(1) == 3.0f); }
    const glsl::value * colour = ran.find("vColor");
    CHECK(colour != nullptr && colour->f(1) == 0.5f);
}

// A texture fetch goes through the environment, so this file knows nothing
// about how textures are stored.
void test_texture_sampling() {
    const glsl::shader m = parse_fragment(R"(
        uniform sampler2D uSampler;
        void main() { gl_FragColor = texture2D(uSampler, vec2(0.25, 0.75)); }
    )");
    CHECK(m.ok);
    glsl::value unit = glsl::value::integer(3);
    glsl::environment env;
    env.read = [&unit](std::string_view name) -> const glsl::value * {
        return name == "uSampler" ? &unit : nullptr;
    };
    env.sample = [](int which, float s, float t) {
        // Hands back what it was asked, so the test can check the unit and the
        // coordinate arrived unmangled.
        return glsl::value::vector({static_cast<float>(which), s, t, 1.0f});
    };
    const glsl::execution ran = glsl::execute(m, env);
    CHECK(ran.ok);
    if (const glsl::value * c = ran.find("gl_FragColor")) {
        CHECK(c->f(0) == 3.0f); // the sampler's unit, which uniform1i uploads
        CHECK(c->f(1) == 0.25f);
        CHECK(c->f(2) == 0.75f);
    }
}

// A RUNTIME FAILURE IS A MESSAGE, NOT A CRASH - the same rule the parser has,
// and for the same reason: this is a page's text.
void test_runtime_failures_are_reported() {
    glsl::environment env;
    for (const char * bad : {
             "gl_FragColor = vec4(undeclaredName);",
             "vec3 v = vec3(0.0); gl_FragColor = vec4(v[99]);",
             "gl_FragColor = vec4(noSuchFunction(1.0));",
             "vec3 v = vec3(0.0); v.qqqq = vec4(1.0); gl_FragColor = vec4(v, 1.0);",
         }) {
        const glsl::shader m = parse_fragment(std::string{"void main() { "} + bad + " }");
        if (!m.ok) { continue; } // a parse error is a fine outcome too
        const glsl::execution ran = glsl::execute(m, env);
        if (!ran.ok) { CHECK(!ran.error.empty()); }
    }
    // And one that must definitely fail, so the loop cannot pass by accepting
    // everything.
    const glsl::shader m = parse_fragment("void main() { gl_FragColor = vec4(nope); }");
    CHECK(m.ok);
    CHECK(!glsl::execute(m, env).ok);

    // A LOOP THAT NEVER ENDS MUST NOT HANG. A page can write one, so the
    // evaluator caps iterations rather than trusting the shader.
    const glsl::shader spin =
        parse_fragment("void main() { float x = 0.0; while (true) { x += 1.0; } "
                       "gl_FragColor = vec4(x); }");
    CHECK(spin.ok);
    const glsl::execution ran = glsl::execute(spin, env);
    CHECK(ran.ok || !ran.error.empty()); // it RETURNS, which is the whole point
}

// --- the benchmark ---------------------------------------------------------
//
// THE NUMBER STAGE 3 HAS TO BEAT. docs/webgl-plan.md predicts what a bytecode VM
// over eight-wide fragment packets should do to this; a prediction with no
// baseline is a wish, so the baseline is measured here and printed in the commit
// that changes it.
//
// Not a ctest: it takes seconds and its result is a number rather than a
// pass/fail. `ctbrowser-test-glsl_basics --bench` runs it.
void run_benchmark() {
    // A shader with the shape of real work: a normalize, a dot, a couple of
    // multiplies and a clamp - which is what a diffuse term costs, and roughly
    // what p5's lightTextureFrag does per fragment.
    const glsl::shader m = parse_fragment(R"(
        uniform vec3 uLightDirection;
        uniform vec4 uMaterialColor;
        varying vec3 vNormal;
        void main() {
          vec3 n = normalize(vNormal);
          float diffuse = max(dot(n, uLightDirection), 0.0);
          vec3 lit = uMaterialColor.rgb * (0.2 + 0.8 * diffuse);
          gl_FragColor = vec4(clamp(lit, 0.0, 1.0), uMaterialColor.a);
        }
    )");
    if (!m.ok) {
        std::printf("the benchmark shader did not compile:\n%s", m.info_log().c_str());
        return;
    }

    std::unordered_map<std::string, glsl::value> inputs;
    inputs["uLightDirection"] = glsl::value::vector({0.0f, 0.0f, 1.0f});
    inputs["uMaterialColor"] = glsl::value::vector({0.8f, 0.4f, 0.2f, 1.0f});
    inputs["vNormal"] = glsl::value::vector({0.3f, 0.5f, 0.8f});
    glsl::environment env;
    env.read = [&inputs](std::string_view name) -> const glsl::value * {
        const auto found = inputs.find(std::string{name});
        return found == inputs.end() ? nullptr : &found->second;
    };

    // 200x200 is the corpus page size the plan budgets for, so the count is the
    // number of fragments one full-screen draw actually costs.
    constexpr int fragments = 200 * 200;
    const auto measure = [&](const char * label, auto && one_fragment) {
        const auto started = std::chrono::steady_clock::now();
        double sink = 0;
        for (int i = 0; i < fragments; ++i) {
            // The normal varies per fragment, so nothing can be hoisted by
            // accident and the measurement is of real work.
            inputs["vNormal"] =
                glsl::value::vector({0.3f + static_cast<float>(i % 17) * 0.01f, 0.5f, 0.8f});
            sink += one_fragment();
        }
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        std::printf("    %-32s %7.2f M frag/s  %6.1f ms per draw  (sink %.0f)\n", label,
                    fragments / seconds / 1e6, seconds * 1000.0, sink);
        return seconds;
    };

    std::printf("\n  GLSL execution, per fragment, over a 200x200 full-screen draw\n\n");
    const double naive = measure("execute() - prepares each time", [&] {
        const glsl::execution ran = glsl::execute(m, env);
        const glsl::value * c = ran.find("gl_FragColor");
        return c == nullptr ? 0.0 : static_cast<double>(c->f(0));
    });
    const glsl::program prepared{m};
    const double fast = measure("program::run() - prepared once", [&] {
        const glsl::execution ran = prepared.run(env);
        const glsl::value * c = ran.find("gl_FragColor");
        return c == nullptr ? 0.0 : static_cast<double>(c->f(0));
    });
    std::printf("\n    Preparing once is %.1fx faster here - and this shader declares three\n",
                naive / fast);
    std::printf("    things, so there is almost no setup to save. THE TOY SHADER IS THE\n");
    std::printf("    WRONG MEASUREMENT; a real one follows.\n\n");

    // A REAL SHADER, from the corpus. p5's lightTextureFrag is what actual work
    // looks like: several uniforms, several varyings, and a function or two -
    // which is exactly the setup a per-fragment `execute` was rebuilding.
    const std::string preamble = read_file("tests/glsl/preamble.glsl");
    const std::string real_source = preamble + "\n" + read_file("tests/glsl/lightTextureFrag.frag");
    glsl::options how;
    how.which = glsl::stage::fragment;
    const glsl::shader real = glsl::parse(real_source, how);
    if (!real.ok) {
        std::printf("  the corpus shader did not compile:\n%s", real.info_log().c_str());
        return;
    }
    // Its inputs, all zero - the numbers do not matter, the WORK does.
    for (const glsl::interface_variable & v : real.interface_) {
        glsl::value made;
        made.t = v.t;
        made.v.assign(static_cast<std::size_t>(std::max(1, v.t.components())), 0.5f);
        inputs[v.name] = made;
    }
    env.sample = [](int, float, float) { return glsl::value::vector({0.5f, 0.5f, 0.5f, 1.0f}); };

    std::printf("  p5's lightTextureFrag - %zu declarations\n\n", real.interface_.size());
    const double real_naive = measure("execute() - prepares each time", [&] {
        const glsl::execution ran = glsl::execute(real, env);
        const glsl::value * c = ran.find("gl_FragColor");
        return c == nullptr ? 0.0 : static_cast<double>(c->f(0));
    });
    const glsl::program real_prepared{real};
    const double real_fast = measure("program::run() - prepared once", [&] {
        const glsl::execution ran = real_prepared.run(env);
        const glsl::value * c = ran.find("gl_FragColor");
        return c == nullptr ? 0.0 : static_cast<double>(c->f(0));
    });
    std::printf("\n    Preparing once is %.1fx faster on a shader anyone would actually run.\n\n",
                real_naive / real_fast);
}

} // namespace

int main(int argc, char ** argv) {
    if (argc > 1 && std::string_view{argv[1]} == "--bench") {
        run_benchmark();
        return 0;
    }
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
    test_matrices_are_column_major();
    test_integer_arithmetic();
    test_swizzles();
    test_swizzle_assignment();
    test_builtins();
    test_control_flow_and_functions();
    test_overload_resolution();
    test_out_parameters();
    test_structs_at_runtime();
    test_uniforms_and_arrays();
    test_discard();
    test_a_vertex_shader_publishes_its_varyings();
    test_texture_sampling();
    test_runtime_failures_are_reported();
    REPORT("glsl_basics");
}
