#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include <ctbrowser/raster/glsl.hpp>

// A PAGE'S SHADER, REWRITTEN INTO THE DIALECT A SPIR-V COMPILER ACCEPTS.
//
// The web sends GLSL ES 1.00 (WebGL 1) and ES 3.00 (WebGL 2). glslang - which is
// what libshaderc wraps - will not lower either of those to SPIR-V:
//
//     #version 100      ES shaders for SPIR-V require version 310 or higher
//     #version 300 es   ES shaders for SPIR-V require version 310 or higher
//
// That looks like a wall and is not one. It is a DIALECT problem, and this is
// the translation, which is what every browser's shader compiler does before
// handing anything to a driver. See docs/gpu-shaders-plan.md for the
// measurements behind each decision here.
//
// WHY FROM THE AST RATHER THAN BY EDITING THE TEXT. A regular expression over
// the source would have to know where a comment is, where a string is, which
// `varying` is a declaration and which is a variable someone named `varying_`,
// and what stage it is in. The parser has already answered all of that, and it
// is the same parser that has been reading p5's, Phaser's and Babylon's shaders
// for months - so the translation inherits the leniency rather than
// re-implementing it.
//
// WHAT CHANGES, and nothing else does:
//
//   #version               -> `310 es`, which is the lowest glslang will lower
//   attribute / varying    -> `in` and `out`, by stage; removed in ES 3.00
//   gl_FragColor           -> a declared output; it does not exist in ES 3.00
//   texture2D / textureCube-> `texture`; the old names are gone
//   in/out/uniform blocks  -> the same, with `layout(location=)` and
//                             `layout(binding=)` attached
//
// THE NUMBERS ARE THE ENGINE'S OWN, and that is the point of the `bindings`
// argument rather than a convenience. glslang can assign locations itself with
// `-fauto-map-locations`, and the result compiles - but this engine has ALREADY
// TOLD THE PAGE what the locations are: `getAttribLocation` reports them,
// `bindAttribLocation` is a documented no-op because of it, and the page then
// hands those numbers to `vertexAttribPointer`. If glslang picks its own, the
// SPIR-V's inputs and the page's buffers agree only by coincidence. Passing
// them in makes them agree by construction.

namespace ctbrowser::raster::glsl {

// What the linker knows and the translator needs: which number each name was
// given. Both are filled from `gl_program` on the shell side, which is where
// linking happens.
struct binding_tables {
    // Attribute locations, indexed BY LOCATION - so `attributes[0]` is the name
    // that `getAttribLocation` answers 0 for. That is the same vector
    // `gl_program::attribute_names` already is.
    std::vector<std::string> attributes;
    // Uniform block name -> its binding point, as `uniformBlockBinding` set it.
    std::vector<std::pair<std::string, std::uint32_t>> blocks;
    // Sampler uniform name -> its texture unit, as `uniform1i` set it. A sampler
    // cannot live in a uniform block, so it needs its own binding.
    std::vector<std::pair<std::string, std::uint32_t>> samplers;
};

struct translation {
    std::string source;
    bool ok = false;
    // WHERE EACH RESOURCE ENDED UP, which the caller needs and cannot guess.
    //
    // Vulkan puts uniform blocks and samplers in ONE descriptor space, while
    // WebGL numbers them separately - a block binding of 0 and a texture unit of
    // 0 are different things to a page and the same slot to a driver. So the
    // translator renumbers into a single space and reports the result; binding
    // buffers by the page's numbers would put a texture where a matrix goes.
    std::vector<std::pair<std::string, std::uint32_t>> vulkan_bindings;
    // The name of the block the loose uniforms were gathered into, empty when
    // there were none. See `default_block_name` in the implementation for why
    // that gathering has to happen at all.
    std::string default_block;
    // WHY IT REFUSED, in the shader's own vocabulary. A translator that fails
    // silently hands the compiler something that then fails for a second reason,
    // and the report names the wrong one.
    std::string error;
};

// Translate one parsed shader. `stage` comes from the shader itself
// (`shader::which`), so a varying knows whether it is an input or an output.
[[nodiscard]] translation to_vulkan_glsl(const shader & parsed, const binding_tables & bindings);

} // namespace ctbrowser::raster::glsl
