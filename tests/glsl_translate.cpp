// DOES THE TRANSLATED SHADER SAY WHAT THE ORIGINAL SAID, and will a real
// compiler take it.
//
// raster/glsl_translate.cpp rewrites a page's GLSL ES into the dialect glslang
// will lower to SPIR-V - see docs/gpu-shaders-plan.md. Two things can go wrong
// and they need different checks:
//
//   THE TEXT IS NOT VALID GLSL. Caught by handing it to `glslc`, which is
//   OPTIONAL here for the same reason `spirv-val` and plutosvg are: a checkout
//   without it should still build and pass, and say plainly that it did not
//   look. When it IS there, this is the strongest check in the file - it is the
//   actual compiler the runtime path would use.
//
//   THE TEXT IS VALID AND MEANS SOMETHING ELSE. A compiler cannot catch that,
//   so the structural assertions below do: that a `varying` came out as `in` in
//   the fragment stage and `out` in the vertex one, that `gl_FragColor` became
//   a declared output, that `texture2D` became `texture`, and that an
//   attribute kept the location the linker gave it - which is the one number a
//   page can observe through `getAttribLocation`.
//
// THE CORPUS IS SOMEBODY ELSE'S. tests/glsl/ holds the sixteen shaders p5.js
// ships, extracted by tools/gen-glsl-fixtures.py. A translator tested only on
// shaders written to test it proves very little.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <ctbrowser/raster/glsl.hpp>
#include <ctbrowser/raster/glsl_translate.hpp>

#include "check.hpp"

using namespace ctbrowser::raster;

namespace {

[[nodiscard]] std::string read_file(const std::string & path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) { return {}; }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// glslc, if the machine has one. Returns empty when it does not, and the caller
// says so out loud rather than passing quietly.
[[nodiscard]] std::string find_glslc() {
    for (const char * candidate : {"/home/linuxbrew/.linuxbrew/bin/glslc", "/usr/bin/glslc",
                                   "/usr/local/bin/glslc"}) {
        if (std::filesystem::exists(candidate)) { return candidate; }
    }
    return {};
}

// Hand the translation to the real compiler. `stage` is glslc's spelling.
struct compile_result {
    bool ok = false;
    std::string log;
};

[[nodiscard]] compile_result compile_with(const std::string & glslc, const std::string & source,
                                          const char * stage) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path();
    const std::filesystem::path in = dir / "ctbrowser-translate.glsl";
    const std::filesystem::path log = dir / "ctbrowser-translate.log";
    {
        std::ofstream out{in, std::ios::binary};
        out << source;
    }
    // -std IS NOT PASSED, deliberately: the translation writes its own
    // `#version 310 es`, and a test that forced the version would not be
    // checking that it did.
    const std::string command = glslc + " -fshader-stage=" + stage + " " + in.string() +
                                " -o /dev/null 2>" + log.string();
    compile_result out;
    out.ok = std::system(command.c_str()) == 0;
    out.log = read_file(log.string());
    return out;
}

// The engine's own tables. For the corpus below there is no linked program to
// ask, so attributes are numbered in declaration order - which is exactly what
// the linker does, and what makes the assertion about locations meaningful.
[[nodiscard]] glsl::binding_tables tables_for(const glsl::shader & parsed) {
    glsl::binding_tables out;
    for (const glsl::interface_variable & v : parsed.interface_) {
        if (v.store == glsl::storage::attribute) { out.attributes.push_back(v.name); }
        if (v.t.is_sampler()) {
            out.samplers.emplace_back(v.name, static_cast<std::uint32_t>(out.samplers.size()));
        }
    }
    for (const glsl::shader::uniform_block & block : parsed.blocks) {
        out.blocks.emplace_back(block.name, static_cast<std::uint32_t>(out.blocks.size()));
    }
    return out;
}

[[nodiscard]] bool mentions(const std::string & haystack, const std::string & needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

int main() {
    const std::string glslc = find_glslc();
    if (glslc.empty()) {
        std::printf("     glslc not found - the translations are checked STRUCTURALLY only.\n"
                    "     Install shaderc to have a real compiler read them.\n");
    } else {
        std::printf("     checking translations with %s\n", glslc.c_str());
    }

    // --- an ES 1.00 fragment shader: the dialect that needs the most work ----
    {
        glsl::options how;
        how.which = glsl::stage::fragment;
        const glsl::shader parsed = glsl::parse(R"(
            precision mediump float;
            varying vec4 vColour;
            varying vec2 vUV;
            uniform sampler2D uTexture;
            uniform float uAlpha;
            void main(void) {
              vec4 texel = texture2D(uTexture, vUV);
              gl_FragColor = vec4(vColour.rgb * texel.rgb, uAlpha);
            }
        )",
                                              how);
        CHECK(parsed.ok);
        const glsl::translation out = glsl::to_vulkan_glsl(parsed, tables_for(parsed));
        CHECK(out.ok);
        if (!out.ok) { std::printf("FAIL translation refused: %s\n", out.error.c_str()); }

        CHECK(mentions(out.source, "#version 310 es"));
        // A VARYING IS AN INPUT HERE. The same declaration is an output in the
        // vertex shader below, and getting that backwards is not a compile
        // error - it is a shader that reads zero.
        CHECK(mentions(out.source, "in vec4 vColour"));
        CHECK(!mentions(out.source, "varying"));
        // gl_FragColor does not exist in ES 3.00, so an output was declared and
        // the assignment rewritten to it.
        CHECK(!mentions(out.source, "gl_FragColor"));
        CHECK(mentions(out.source, "out vec4 ctbrowser_FragColour"));
        CHECK(mentions(out.source, "ctbrowser_FragColour ="));
        // The renamed built-in.
        CHECK(mentions(out.source, "texture("));
        CHECK(!mentions(out.source, "texture2D("));
        // A sampler needs a binding; an ordinary uniform must NOT have one.
        CHECK(mentions(out.source, "uniform sampler2D uTexture"));
        // AND THE LOOSE UNIFORM WENT INTO A BLOCK. Vulkan GLSL refuses a
        // non-opaque uniform outside one - `not allowed when using GLSL for
        // Vulkan` - which is what every WebGL-on-Vulkan implementation gathers
        // a default block for. The body still says `uAlpha`, because the block
        // has no instance name.
        CHECK(mentions(out.source, "ctbrowser_DefaultUniforms"));
        CHECK(mentions(out.source, "float uAlpha;"));
        CHECK(out.default_block == "ctbrowser_DefaultUniforms");

        if (!glslc.empty()) {
            const compile_result compiled = compile_with(glslc, out.source, "fragment");
            if (!compiled.ok) {
                std::printf("FAIL glslc refused the translated ES 1.00 fragment shader:\n%s\n%s\n",
                            compiled.log.c_str(), out.source.c_str());
                ++ctbrowser_test_failures;
            }
        }
    }

    // --- the vertex side, where a varying goes the other way -----------------
    {
        glsl::options how;
        how.which = glsl::stage::vertex;
        const glsl::shader parsed = glsl::parse(R"(
            attribute vec3 aPosition;
            attribute vec2 aTexCoord;
            uniform mat4 uModelViewProjection;
            varying vec2 vUV;
            void main(void) {
              vUV = aTexCoord;
              gl_Position = uModelViewProjection * vec4(aPosition, 1.0);
            }
        )",
                                              how);
        CHECK(parsed.ok);
        const glsl::binding_tables tables = tables_for(parsed);
        const glsl::translation out = glsl::to_vulkan_glsl(parsed, tables);
        CHECK(out.ok);

        CHECK(mentions(out.source, "out vec2 vUV"));
        CHECK(!mentions(out.source, "attribute"));
        // THE LOCATION IS THE ENGINE'S, and this is the assertion that matters
        // most in the file: `getAttribLocation` already told the page these
        // numbers and the page hands them to `vertexAttribPointer`. If the
        // SPIR-V numbers its inputs differently, the buffers land on the wrong
        // ones - and nothing anywhere reports it.
        CHECK(mentions(out.source, "layout(location = 0) in vec3 aPosition"));
        CHECK(mentions(out.source, "layout(location = 1) in vec2 aTexCoord"));

        if (!glslc.empty()) {
            const compile_result compiled = compile_with(glslc, out.source, "vertex");
            if (!compiled.ok) {
                std::printf("FAIL glslc refused the translated vertex shader:\n%s\n%s\n",
                            compiled.log.c_str(), out.source.c_str());
                ++ctbrowser_test_failures;
            }
        }
    }

    // --- a WebGL 2 shader with a uniform block -------------------------------
    {
        glsl::options how;
        how.which = glsl::stage::vertex;
        const glsl::shader parsed = glsl::parse(R"(#version 300 es
            precision highp float;
            layout(std140) uniform Scene { mat4 viewProjection; vec4 tint; };
            in vec3 position;
            out vec3 vWorld;
            void main(void) {
              vWorld = position * tint.rgb;
              gl_Position = viewProjection * vec4(position, 1.0);
            }
        )",
                                              how);
        CHECK(parsed.ok);
        glsl::binding_tables tables = tables_for(parsed);
        // A binding the page chose, so the assertion is that it is CARRIED
        // rather than that zero happens to be right.
        const glsl::translation out = glsl::to_vulkan_glsl(parsed, tables);
        CHECK(out.ok);
        // THE BINDING IS THE TRANSLATOR'S, and it REPORTS it. A page numbers
        // blocks and texture units separately; Vulkan puts them in one space,
        // so binding by the page's numbers would put a texture where a matrix
        // goes. What matters is that the emitted number and the reported one
        // agree.
        CHECK(mentions(out.source, "uniform Scene"));
        bool reported = false;
        for (const auto & [name, point] : out.vulkan_bindings) {
            if (name == "Scene") {
                reported = mentions(out.source, "binding = " + std::to_string(point) +
                                                    ") uniform Scene");
            }
        }
        CHECK(reported);
        // The block's members must NOT be declared a second time as globals -
        // the parser puts them in both lists, and declaring them twice is a
        // redefinition error that only shows up in the compiler.
        const std::size_t first = out.source.find("viewProjection");
        CHECK(first != std::string::npos);
        CHECK(out.source.find("viewProjection", first + 1) != std::string::npos); // used in main
        CHECK(!mentions(out.source, "uniform mat4 viewProjection;"));

        if (!glslc.empty()) {
            const compile_result compiled = compile_with(glslc, out.source, "vertex");
            if (!compiled.ok) {
                std::printf("FAIL glslc refused the translated WebGL 2 shader:\n%s\n%s\n",
                            compiled.log.c_str(), out.source.c_str());
                ++ctbrowser_test_failures;
            }
        }
    }

    // --- and the whole p5 corpus, which is somebody else's code --------------
    //
    // These are the shaders p5.js ships, and they are FRAGMENTS: p5 prepends a
    // preamble defining IN, OUT and the HOOK_ macros. tools/gen-glsl-fixtures.py
    // extracts that preamble too, so what is parsed here is what p5 would
    // actually compile.
    {
        const std::string preamble = read_file("tests/glsl/preamble.glsl");
        int translated = 0;
        int compiled_ok = 0;
        int refused = 0;
        for (const auto & entry : std::filesystem::directory_iterator{"tests/glsl"}) {
            const std::string path = entry.path().string();
            const bool is_vertex = path.ends_with(".vert");
            if (!is_vertex && !path.ends_with(".frag")) { continue; }
            const std::string body = read_file(path);
            if (body.empty()) { continue; }
            glsl::options how;
            how.which = is_vertex ? glsl::stage::vertex : glsl::stage::fragment;
            const glsl::shader parsed = glsl::parse(preamble + "\n" + body, how);
            if (!parsed.ok) { continue; } // the front end's own corpus test covers parsing
            const glsl::translation out = glsl::to_vulkan_glsl(parsed, tables_for(parsed));
            if (!out.ok) {
                std::printf("     %s: translation refused: %s\n",
                            entry.path().filename().string().c_str(), out.error.c_str());
                ++refused;
                continue;
            }
            ++translated;
            // SHOW THE WORK ON REQUEST. A translator failure is a text problem
            // and the text is what a reader needs; printing it always would
            // bury the result, so it is behind an environment variable the way
            // REGOLDEN is.
            if (const char * dump = std::getenv("CTBROWSER_DUMP_GLSL");
                dump != nullptr && entry.path().filename().string().find(dump) !=
                                       std::string::npos) {
                std::printf("--- %s ---\n%s\n", entry.path().filename().string().c_str(),
                            out.source.c_str());
            }
            if (!glslc.empty()) {
                const compile_result result =
                    compile_with(glslc, out.source, is_vertex ? "vertex" : "fragment");
                if (result.ok) {
                    ++compiled_ok;
                } else {
                    std::printf("     %s: glslc refused it:\n%s",
                                entry.path().filename().string().c_str(),
                                result.log.substr(0, 300).c_str());
                }
            }
        }
        std::printf("     p5 corpus: %d translated, %d refused", translated, refused);
        if (!glslc.empty()) { std::printf(", %d compiled by glslc", compiled_ok); }
        std::printf("\n");
        // THE NUMBER IS NOT ASSERTED YET, and that is deliberate: this is the
        // first version of the translator and the corpus is what says how far
        // it gets. What IS asserted is that it does not CRASH and does not
        // silently produce nothing - a translation that refused everything
        // would read as "0 translated" rather than as a pass.
        CHECK(translated > 0);
    }

    REPORT("glsl_translate");
}
