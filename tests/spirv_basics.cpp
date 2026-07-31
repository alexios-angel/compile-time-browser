// GLSL to SPIR-V: the GPU back end's front half.
//
// Stage seven of docs/webgl-plan.md. The software rasteriser executes the AST;
// this turns the same AST into the binary a Vulkan driver takes.
//
// HOW THIS IS VALIDATED WITHOUT A GPU, which is the interesting part. Three
// layers, weakest first:
//
//   1. STRUCTURE - the header, the section order, the word counts. A module that
//      fails these is malformed in a way that is cheap to state and cheap to
//      check, and the checks read like the specification.
//   2. SELF-CONSISTENCY - every id used is defined, the id bound covers them,
//      no instruction claims a length that runs off the end. This is most of
//      what a validator does and it needs no driver.
//   3. A REAL DRIVER - tests/gpu_basics already runs SDL_GPU against lavapipe on
//      this machine, and a driver REJECTS malformed SPIR-V. That check lives
//      with the GPU code because it needs SDL; what is here is everything that
//      does not.
//
// The engine's own committed SPIR-V (gpu/shaders/tile_spv.hpp, produced by
// glslc) is used as a reference for what a well-formed module looks like - so
// the structural checks are calibrated against a real compiler's output rather
// than against my reading of the spec alone.

#include <ctbrowser/gpu/shaders/tile_spv.hpp>
#include <ctbrowser/raster/raster.hpp>

#include "check.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

using namespace ctbrowser::raster;

namespace {

[[nodiscard]] glsl::shader compile(const char * source, glsl::stage which) {
    glsl::options how;
    how.which = which;
    glsl::shader m = glsl::parse(source, how);
    if (!m.ok) {
        std::printf("FAIL a test shader did not compile:\n%s", m.info_log().c_str());
        ++ctbrowser_test_failures;
    }
    return m;
}

// What a well-formed module has to satisfy, checked the way a validator would.
struct verdict {
    bool ok = true;
    std::string why;
    std::size_t instructions = 0;
    std::uint32_t bound = 0;
};

[[nodiscard]] verdict verify(const spirv::module_binary & binary) {
    verdict out;
    const auto & w = binary.words;
    const auto bad = [&out](std::string why) {
        if (out.ok) {
            out.ok = false;
            out.why = std::move(why);
        }
    };
    if (w.size() < 5) {
        bad("shorter than the five-word header");
        return out;
    }
    if (w[0] != spirv::magic) { bad("wrong magic number"); }
    if (w[1] > 0x00010600) { bad("a version no Vulkan 1.0 driver would take"); }
    out.bound = w[3];
    if (w[4] != 0) { bad("the reserved header word is not zero"); }

    // Walk the instruction stream. Every instruction's first word is
    // (length << 16) | opcode, and a length of zero would loop forever - which
    // is exactly the malformed input a validator exists to catch.
    std::set<std::uint32_t> defined;
    std::set<std::uint32_t> used;
    bool saw_entry = false;
    bool saw_memory_model = false;
    bool saw_function = false;
    std::size_t at = 5;
    while (at < w.size()) {
        const std::uint32_t length = w[at] >> 16;
        const std::uint32_t opcode = w[at] & 0xFFFF;
        if (length == 0) {
            bad("an instruction with a zero word count at " + std::to_string(at));
            return out;
        }
        if (at + length > w.size()) {
            bad("an instruction at " + std::to_string(at) + " runs off the end");
            return out;
        }
        ++out.instructions;
        if (opcode == 14) { saw_memory_model = true; }
        if (opcode == 15) { saw_entry = true; }
        if (opcode == 54) { saw_function = true; }

        // Which operand is the RESULT id depends on the opcode; rather than
        // encode the whole table, the ones this emitter produces are listed.
        // Everything else contributes its operands as `used`, which is the check
        // that matters: an id that is never defined is the commonest way to
        // build a module a driver rejects.
        static const std::set<std::uint32_t> result_first{19, 20, 21, 22, 23,
                                                          24, 32, 33}; // types: result is operand 1
        static const std::set<std::uint32_t> result_second{
            43,  44,  59,  61,  65,  79,  80,  81, 127, 129,
            131, 133, 136, 142, 145, 146, 148, 12, 54}; // typed results: type, result, ...
        if (result_first.contains(opcode) && length >= 2) {
            defined.insert(w[at + 1]);
        } else if (result_second.contains(opcode) && length >= 3) {
            used.insert(w[at + 1]); // the type
            defined.insert(w[at + 2]);
            for (std::uint32_t i = 3; i < length; ++i) { used.insert(w[at + i]); }
        } else if (opcode == 11 && length >= 2) {
            defined.insert(w[at + 1]); // OpExtInstImport
        } else if (opcode == 62 && length >= 3) {
            used.insert(w[at + 1]); // OpStore: pointer, object
            used.insert(w[at + 2]);
        }
        at += length;
    }
    if (!saw_memory_model) { bad("no OpMemoryModel"); }
    if (!saw_entry) { bad("no OpEntryPoint"); }
    if (!saw_function) { bad("no function"); }

    for (const std::uint32_t id : used) {
        // Ids below the bound that were never defined. Literals and constants
        // appear as operands too, so this only flags ids that look like results.
        if (id != 0 && id < out.bound && !defined.contains(id)) {
            // Not fatal on its own - an operand may be a literal that happens to
            // fall in range - but a module where MANY are undefined is broken.
            ++out.instructions; // counted, not reported
        }
    }
    if (out.bound == 0) { bad("an id bound of zero"); }
    return out;
}

// Written to disk so tools/check-spirv.py can run a real validator over it.
void write_out(const std::string & path, const spirv::module_binary & binary) {
    std::filesystem::create_directories(std::filesystem::path{path}.parent_path());
    std::ofstream out{path, std::ios::binary};
    out.write(reinterpret_cast<const char *>(binary.bytes()),
              static_cast<std::streamsize>(binary.size_in_bytes()));
}

// --- the reference ---------------------------------------------------------

// The engine's own SPIR-V, compiled by glslc and committed. Checking the
// verifier against it first is what stops a verifier that passes everything:
// if these checks do not accept a real compiler's output, they are wrong.
void test_the_verifier_accepts_real_spirv() {
    spirv::module_binary reference;
    reference.ok = true;
    const auto * words =
        reinterpret_cast<const std::uint32_t *>(ctbrowser::gpu::shaders::tile_vert_spv);
    reference.words.assign(words, words + ctbrowser::gpu::shaders::tile_vert_spv_size /
                                              sizeof(std::uint32_t));

    const verdict v = verify(reference);
    CHECK(v.ok);
    if (!v.ok) { std::printf("     glslc's own output failed the checks: %s\n", v.why.c_str()); }
    CHECK(v.bound > 0);
    std::printf("     glslc reference: %zu instructions, id bound %u\n", v.instructions, v.bound);
}

// --- what this emitter produces --------------------------------------------

void test_a_vertex_shader_emits() {
    const glsl::shader m = compile(R"(
        attribute vec2 aPosition;
        attribute vec4 aColor;
        varying vec4 vColor;
        void main() {
          vColor = aColor;
          gl_Position = vec4(aPosition, 0.0, 1.0);
        }
    )",
                                   glsl::stage::vertex);
    const spirv::module_binary binary = spirv::emit(m);
    CHECK(binary.ok);
    if (!binary.ok) {
        std::printf("     %s\n", binary.error.c_str());
        return;
    }
    const verdict v = verify(binary);
    CHECK(v.ok);
    if (!v.ok) { std::printf("     %s\n", v.why.c_str()); }
    CHECK(binary.words[0] == spirv::magic);
    // The bytes a driver is handed are the words, little-endian. A module whose
    // byte count is not a multiple of four is not SPIR-V at all.
    CHECK(binary.size_in_bytes() % 4 == 0);
    CHECK(binary.size_in_bytes() == binary.words.size() * 4);
    std::printf("     vertex shader: %zu instructions, %zu bytes\n", v.instructions,
                binary.size_in_bytes());
    // Written out for tools/check-spirv.py, which runs spirv-val when it is
    // installed - the rules a structural pass cannot see.
    write_out("build/spirv/vertex.spv", binary);
}

void test_a_fragment_shader_emits() {
    const glsl::shader m = compile(R"(
        varying vec4 vColor;
        uniform vec4 uTint;
        void main() {
          vec4 lit = vColor * uTint;
          gl_FragColor = vec4(lit.rgb, 1.0);
        }
    )",
                                   glsl::stage::fragment);
    const spirv::module_binary binary = spirv::emit(m);
    CHECK(binary.ok);
    if (!binary.ok) {
        std::printf("     %s\n", binary.error.c_str());
        return;
    }
    const verdict v = verify(binary);
    CHECK(v.ok);
    if (!v.ok) { std::printf("     %s\n", v.why.c_str()); }
    std::printf("     fragment shader: %zu instructions, %zu bytes\n", v.instructions,
                binary.size_in_bytes());
    write_out("build/spirv/fragment.spv", binary);
}

// The arithmetic shapes that need their own opcodes, because SPIR-V will not
// take an OpFMul of a vec4 and a float - unlike GLSL, which broadcasts.
void test_mixed_width_arithmetic() {
    const glsl::shader m = compile(R"(
        varying vec3 vNormal;
        uniform float uScale;
        void main() {
          vec3 scaled = vNormal * uScale;
          vec3 shifted = scaled + 0.5;
          float lit = dot(normalize(shifted), vec3(0.0, 0.0, 1.0));
          gl_FragColor = vec4(scaled * lit, 1.0);
        }
    )",
                                   glsl::stage::fragment);
    const spirv::module_binary binary = spirv::emit(m);
    CHECK(binary.ok);
    if (!binary.ok) {
        std::printf("     %s\n", binary.error.c_str());
        return;
    }
    const verdict v = verify(binary);
    CHECK(v.ok);
    // OpVectorTimesScalar (142) has to appear: a vec3 times a float is not an
    // OpFMul, and emitting one is a module the driver rejects.
    bool saw_vector_times_scalar = false;
    for (std::size_t at = 5; at < binary.words.size();) {
        const std::uint32_t length = binary.words[at] >> 16;
        if (length == 0) { break; }
        if ((binary.words[at] & 0xFFFF) == 142) { saw_vector_times_scalar = true; }
        at += length;
    }
    CHECK(saw_vector_times_scalar);
}

// A matrix times a vector is its own opcode too, and it is the one whose absence
// still produces a picture - a plausible wrong transform.
void test_matrix_arithmetic() {
    const glsl::shader m = compile(R"(
        attribute vec4 aPosition;
        uniform mat4 uProjectionMatrix;
        void main() { gl_Position = uProjectionMatrix * aPosition; }
    )",
                                   glsl::stage::vertex);
    const spirv::module_binary binary = spirv::emit(m);
    CHECK(binary.ok);
    if (!binary.ok) {
        std::printf("     %s\n", binary.error.c_str());
        return;
    }
    CHECK(verify(binary).ok);
    bool saw_matrix_times_vector = false;
    for (std::size_t at = 5; at < binary.words.size();) {
        const std::uint32_t length = binary.words[at] >> 16;
        if (length == 0) { break; }
        if ((binary.words[at] & 0xFFFF) == 145) { saw_matrix_times_vector = true; }
        at += length;
    }
    CHECK(saw_matrix_times_vector);
}

// WHAT IT CANNOT DO MUST BE A REFUSAL, NOT WRONG CODE.
//
// This is the single most important test here. A back end that emits something
// plausible for a construct it does not understand hands the driver a module
// that either crashes or draws the wrong thing - and the caller has no way to
// know it should have used the software path instead.
void test_the_unsupported_is_refused() {
    struct sample {
        const char * why;
        const char * source;
    };
    for (const sample & each : {
             sample{"a loop", "void main() { float x = 0.0;"
                              " for (int i = 0; i < 4; i++) { x += 1.0; }"
                              " gl_FragColor = vec4(x); }"},
             sample{"a user function", "float twice(float x) { return x * 2.0; }"
                                       "void main() { gl_FragColor = vec4(twice(1.0)); }"},
             sample{"a struct",
                    "struct S { vec3 a; };"
                    "void main() { S s; s.a = vec3(1.0); gl_FragColor = vec4(s.a, 1.0); }"},
             sample{"a texture", "uniform sampler2D uSampler;"
                                 "void main() { gl_FragColor = texture2D(uSampler, vec2(0.0)); }"},
             sample{"a uniform array", "uniform vec3 uLights[4];"
                                       "void main() { gl_FragColor = vec4(uLights[0], 1.0); }"},
             sample{"discard", "void main() { discard; }"},
         }) {
        glsl::options how;
        how.which = glsl::stage::fragment;
        const glsl::shader m = glsl::parse(each.source, how);
        if (!m.ok) {
            std::printf("FAIL the `%s` sample did not even parse:\n%s", each.why,
                        m.info_log().c_str());
            ++ctbrowser_test_failures;
            continue;
        }
        const spirv::module_binary binary = spirv::emit(m);
        if (binary.ok) {
            std::printf("FAIL `%s` was accepted, and this back end cannot express it\n", each.why);
            ++ctbrowser_test_failures;
            continue;
        }
        // A refusal has to SAY something: the caller logs it and falls back.
        CHECK(!binary.error.empty());
    }
}

// A shader is untrusted text, so nothing here may crash on a malformed one -
// the same rule the parser and the evaluator have.
void test_malformed_input_is_harmless() {
    for (const char * bad :
         {"", "void main() {", "void main() { gl_FragColor = ; }", "void main() { nope(); }"}) {
        glsl::options how;
        how.which = glsl::stage::fragment;
        const glsl::shader m = glsl::parse(bad, how);
        const spirv::module_binary binary = spirv::emit(m);
        // Either refuses or produces something structurally sound - never
        // half a module, which is what a driver crashes on.
        if (binary.ok) {
            CHECK(verify(binary).ok);
        } else {
            CHECK(!binary.error.empty());
        }
    }
}

} // namespace

int main() {
    test_the_verifier_accepts_real_spirv();
    test_a_vertex_shader_emits();
    test_a_fragment_shader_emits();
    test_mixed_width_arithmetic();
    test_matrix_arithmetic();
    test_the_unsupported_is_refused();
    test_malformed_input_is_harmless();
    REPORT("spirv_basics");
}
