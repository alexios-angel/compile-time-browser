#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// WHAT IS IN A PACKAGED APPLICATION, in a form a person and a script can both
// read.
//
// Phase 1's gate asks for a documented CLI, a manifest, stable program
// identities and format versions. This is the manifest. It is written into the
// bundle and, with `--manifest`, to a file - and its value is that a packaged
// application stops being an opaque 15 MB executable: what it carries, which
// engine build it was compiled for, which script became which image, and what
// each one is keyed by.
//
// EVERY NUMBER IN IT IS A NUMBER THE PACKAGER ALREADY HAD. Nothing here is
// recomputed for the manifest's benefit, because a manifest that derives its
// own answers is a manifest that can disagree with the artifact it describes -
// and then it is worse than nothing, because it is believed.
//
// NOT llvm::json, WHICH THE PLAN ASKS FOR, and the reason is one the plan gives
// itself: LLVM sits behind CTCOMPILE_ENABLE_MLIR, which is OFF until Phase 7,
// because a compiler that needs a 2 GB dependency to write a JSON file is one a
// runtime-only machine cannot build. The emitter below is forty lines and its
// escaping has its own tests. When Phase 7 turns MLIR on, switching is one file.
namespace ctcompile {

struct manifest_script {
    std::size_t index = 0;
    std::size_t source_bytes = 0;
    std::size_t image_bytes = 0;
    std::size_t functions = 0;
    // THE PROGRAM IDENTITY, and it is the same number the runtime matches on:
    // the hash of the source the engine reported, not of the file on disk. The
    // two differ - the script walk appends a newline - and writing the wrong one
    // here would produce a manifest that looks like it explains a cache miss
    // and does not.
    std::uint64_t program_id = 0;
};

struct manifest_resource {
    std::string name;
    std::size_t bytes = 0;
};

struct manifest {
    std::string compiler_version;
    std::string engine;
    std::string entry;
    // `vm` today. `hybrid` and `aot-only` are Phase 1's other two modes and
    // there is no native code to run in either, so ctcompile refuses them
    // rather than accepting a flag that changes nothing.
    std::string mode = "vm";
    std::string font_directory;

    std::uint32_t bundle_format = 0;
    std::uint32_t image_format = 0;
    // WHICH ENGINE BUILD, as the bundle records it. An application whose images
    // were compiled by another build of this engine is refused at load, and
    // this is where someone looks to find out why.
    std::uint64_t engine_fingerprint = 0;

    std::vector<manifest_script> scripts;
    std::vector<manifest_resource> resources;
    std::size_t bundle_bytes = 0;
};

// The manifest as JSON, one object, newline-terminated.
[[nodiscard]] std::string to_json(const manifest & from);

// One JSON string literal, quotes included, with everything RFC 8259 requires
// escaped.
//
// Public because it is the part that can be wrong: a resource name is whatever
// the document said, which is not a promise of anything - `p5"; drop</script>`
// is a legal file name, and a manifest that emitted it raw would produce a file
// nothing can parse, from a packager that reported success.
[[nodiscard]] std::string json_string(std::string_view text);

} // namespace ctcompile
