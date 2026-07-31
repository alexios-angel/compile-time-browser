#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include <ctbrowser/raster/glsl.hpp>

// GLSL ES to SPIR-V, at run time.
//
// Stage seven of docs/webgl-plan.md: the GPU back end's front half. The software
// rasteriser executes the AST; this turns the same AST into the binary a Vulkan
// driver takes, so `gpu/` can build a graphics pipeline out of a page's shader.
//
// WHY THIS HAS TO EXIST AT ALL. The engine's existing GPU code compiles its
// shaders with `glslc` at BUILD time and commits the result
// (`tools/gen-shaders.py`). WebGL cannot: the shaders arrive from the page while
// it is running, and shelling out to a compiler that may not be installed is not
// something a browser engine gets to do. So the compiler has to be in here.
//
// IN raster/ RATHER THAN gpu/, AND THAT IS DELIBERATE. Emitting SPIR-V needs the
// GLSL front end and nothing else - no SDL, no device, no Vulkan headers - so it
// belongs with the language it reads rather than with the API that consumes it.
// `tests/api_surface` lints the SDL-free rule, and this file keeps it: `gpu/`
// takes the bytes from here and hands them to SDL_CreateGPUShader.
//
// HOW THIS IS CHECKED, and the first answer here was wrong. An earlier version
// of this comment said a driver rejecting malformed SPIR-V made the round trip
// self-checking. It does not: neither lavapipe nor a real Intel Arc rejects
// garbage at shader creation, and neither rejects a module with mismatched
// operand widths at PIPELINE creation either - both measured, the second by
// deliberately breaking the operand broadcast below and watching the pipeline
// build anyway. Vulkan is an explicit API and trusts its input.
//
// So the checks are the ones this repository owns: `tests/spirv_basics.cpp`
// decodes the words structurally, `tools/check-spirv.py` runs `spirv-val` where
// it is installed, and `gpu_basics` hands a real driver a real module - which
// proves the bytes survive the whole path, and says out loud that it is not
// proving them valid.

namespace ctbrowser::raster::spirv {

// SPIR-V is a stream of 32-bit words: a five-word header, then instructions
// whose first word packs a length and an opcode. Nothing here needs a reader, so
// the result is just the words.
struct module_binary {
    bool ok = false;
    std::string error;
    std::vector<std::uint32_t> words;

    // The bytes a driver wants. SPIR-V is defined little-endian-first with a
    // magic number that lets a driver detect the other order, and every platform
    // this builds for is little-endian - so this is a reinterpret rather than a
    // swap, and the magic number in the header is what proves it.
    [[nodiscard]] const std::byte * bytes() const noexcept {
        return reinterpret_cast<const std::byte *>(words.data());
    }
    [[nodiscard]] std::size_t size_in_bytes() const noexcept {
        return words.size() * sizeof(std::uint32_t);
    }
};

inline constexpr std::uint32_t magic = 0x07230203;

// Emit SPIR-V for a parsed shader.
//
// Fails with a MESSAGE rather than partial output: a driver handed half a module
// crashes or hangs, and a page's shader is untrusted text. Everything this
// cannot express is a clean `ok == false` and a reason, which the caller turns
// into a fall back to the software path.
[[nodiscard]] module_binary emit(const glsl::shader & m);

// WHAT THIS SUBSET COVERS, and it is smaller than what the software path runs:
//
//   * float and vector arithmetic, swizzles, comparisons, the common built-ins
//   * uniforms (as a single uniform block), attributes, varyings
//   * gl_Position and the fragment output
//   * `if`, and expressions - straight-line code with branches
//
// NOT covered, each of which comes back as a refusal rather than as wrong code:
// loops, user-defined functions, structs, arrays, textures, matrices beyond
// mat4, `discard`, and integer arithmetic. Those are the reason a caller must
// keep the software path: it runs everything, and this runs the shaders it can
// and says so about the rest.

} // namespace ctbrowser::raster::spirv
