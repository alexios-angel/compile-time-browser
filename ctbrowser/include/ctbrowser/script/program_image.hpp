//===- program_image.hpp - Compiled program images --------------*- C++ -*-===//
//
// A compiled program serialised to bytes, so a packaged application never
// parses its own JavaScript; about forty percent of a page load is reading
// JavaScript (ctbrowser/docs/performance.md).
//
// The writer and the reader live together, in the runtime: the reader must
// exist in a runtime-only build, and a writer elsewhere would be a second walk
// over the same fields, free to drift. One file, one field order, one version.
//
// This is not a stable interchange format: an image is written and read by one
// build of this engine, so the header carries a fingerprint of the things
// whose meaning it depends on, and a mismatch is refused rather than loaded on
// a best-effort basis.
//
// An image is executable input and is not authenticated. `load_image`
// validates shape exhaustively (every index, every operand, every count, and
// every constant against the bit patterns that are values at all) because the
// VM indexes its pools with unchecked operator[]. There is no checksum:
// neither the fingerprint nor the source hash covers the bytes that follow, so
// a flipped bit inside a valid field runs. An unkeyed hash would defend
// against nothing an attacker cannot recompute; if images ever land in a
// shared cache or come over a network, they need a keyed hash.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/script/bytecode.hpp>

namespace ctbrowser::script {

/// A hash of everything about this build whose meaning an image depends on. A
/// mismatch means the bytes describe a different engine, whatever else they
/// contain.
[[nodiscard]] std::uint64_t image_fingerprint() noexcept;

/// A hash of the JavaScript an image was built from: "was this built from this
/// source", where the fingerprint asks "was this built by this engine". Written
/// even when the source itself is dropped from the image.
[[nodiscard]] std::uint64_t image_source_hash(std::string_view source) noexcept;

/// Whether an image keeps the program's source text.
enum class image_option : std::uint32_t {
    /// Keep `program::source`. Retaining it roughly doubles the image, and
    /// dropping it changes behaviour: `f.toString()` can no longer return the
    /// text. So it is kept unless dropping it is asked for, and the image
    /// records which it is so a loader never guesses. Dropping the source drops
    /// the per-instruction source offsets with it (an offset into text the
    /// image no longer carries cannot become a line or a column), which is most
    /// of what the debug tables cost: +52% of a source-dropped image. Local
    /// names alone are +13% and are kept either way, because they are what
    /// makes a stack trace and the AOT backend's C++ readable.
    keep_source = 0,
    drop_source = 1,
};

/// Serialises `from`. Returns empty when the program is one this format
/// refuses to write; `write_error` says why.
[[nodiscard]] std::vector<std::byte> write_image(const program & from,
                                                 image_option option = image_option::keep_source);

/// The image format version this build writes and reads. Exposed so a manifest
/// can record it without keeping a second copy of the number.
[[nodiscard]] std::uint32_t image_format_version() noexcept;

/// Why the last write_image on this thread produced nothing.
[[nodiscard]] std::string_view write_error() noexcept;

/// What an image says about itself, readable without decoding it, so a holder
/// of several images can key them without paying to load one.
struct image_header {
    std::uint64_t source_hash = 0;
    script_kind kind = script_kind::classic;
    /// Two images of one script differing only in this are both valid and are
    /// not interchangeable: `f.toString()` returns the text from one and
    /// "[native code]" from the other.
    image_option option = image_option::keep_source;
};

/// The header, or nullopt when these bytes are not an image this build would
/// load: wrong magic, another format version, or another engine.
[[nodiscard]] std::optional<image_header> read_image_header(std::span<const std::byte> bytes);

/// The result of load_image: the program, or `ok == false` and `error`.
struct load_result {
    program value;
    bool ok = false;
    std::string error;
    /// What the image says it was built from, whether or not it kept the text.
    std::uint64_t source_hash = 0;
    /// Which kind of script it was compiled as. A different question from the
    /// hash, which only knows the text: the same text compiled as a module
    /// hashes identically and is a different program.
    script_kind kind = script_kind::classic;
};

/// Decodes bytes back to a program, or refuses with a reason.
///
/// Every field is validated, because the VM does not validate: its pool reads
/// are unchecked `operator[]`, and the parameter fill in run_loop.cpp would
/// write past the register window if `param_count` exceeded `frame_size`.
/// `expect_source_hash`, when given, is checked against the image's own and a
/// mismatch is refused: an image built from different source is wrong code
/// running at full speed. `expect_kind` defaults to classic and is always
/// checked, because a module's top level declares into its own scope, so
/// running one where a classic script belongs publishes nothing and raises no
/// error.
[[nodiscard]] load_result load_image(std::span<const std::byte> bytes,
                                     std::optional<std::uint64_t> expect_source_hash = {},
                                     script_kind expect_kind = script_kind::classic);

} // namespace ctbrowser::script
