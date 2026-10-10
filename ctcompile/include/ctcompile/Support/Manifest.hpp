//===- Manifest.hpp - Packaged-app manifest ---------------------*- C++ -*-===//
//
// What is in a packaged application, in a form a person and a script can both
// read. The manifest is written into the bundle and, with `--manifest`, to a
// file, so a packaged application stops being an opaque executable: it records
// what the bundle carries, which engine build it was compiled for, which script
// became which image, and what each is keyed by.
//
// Every number in it is one the packager already had. Nothing is recomputed
// for the manifest, because a manifest that derives its own answers can
// disagree with the artifact it describes, and is then believed anyway.
//
// The manifest library stays independent of LLVM. Only the compiler tools
// link LLVM for their command lines; this emitter has its own escaping tests.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ctcompile {

/// One script the bundle carries and the bytecode image compiled from it.
struct manifest_script {
    std::size_t index = 0;
    std::size_t source_bytes = 0;
    std::size_t image_bytes = 0;
    std::size_t functions = 0;
    /// The program identity, the same number the runtime matches on: the hash
    /// of the source the engine reported, not of the file on disk. The two
    /// differ (the script walk appends a newline), and the wrong one would make
    /// a manifest that seems to explain a cache miss and does not.
    std::uint64_t program_id = 0;
};

/// One non-script resource in the bundle.
struct manifest_resource {
    std::string name;
    std::size_t bytes = 0;
};

/// The whole manifest of one packaged application.
struct manifest {
    std::string compiler_version;
    std::string engine;
    std::string entry;
    std::string font_directory;

    std::uint32_t bundle_format = 0;
    std::uint32_t image_format = 0;
    /// Which engine build, as the bundle records it. An application whose
    /// images were compiled by another build of this engine is refused at load,
    /// and this is where someone looks to find out why.
    std::uint64_t engine_fingerprint = 0;

    std::vector<manifest_script> scripts;
    std::vector<manifest_resource> resources;
    std::size_t bundle_bytes = 0;
};

/// The manifest as JSON, one object, newline-terminated.
[[nodiscard]] std::string to_json(const manifest & from);

/// One JSON string literal, quotes included, with everything RFC 8259 requires
/// escaped.
///
/// Public so it can be tested on its own: a resource name is whatever the
/// document said, and `p5"; drop</script>` is a legal file name. Emitted raw, it
/// would produce a manifest nothing can parse, from a packager that reported
/// success.
[[nodiscard]] std::string json_string(std::string_view text);

} // namespace ctcompile
