#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/script/bytecode.hpp>

// A COMPILED PROGRAM, AS BYTES, so a packaged application never parses its own
// JavaScript - about forty percent of a page load is READING JavaScript
// (docs/performance.md).
//
// THE WRITER AND THE READER LIVE TOGETHER, IN THE RUNTIME: the reader must
// exist in a runtime-only build, and a writer elsewhere would be a second walk
// over the same fields, free to drift. One file, one field order, one version.
//
// NOT a stable interchange format: an image is written and read by ONE build
// of this engine, so the header carries a fingerprint of the things whose
// meaning it depends on, and a mismatch is a REFUSAL rather than a best effort.
//
// AN IMAGE IS EXECUTABLE INPUT AND IS NOT AUTHENTICATED. `load` validates
// SHAPE exhaustively - every index, every operand, every count, and every
// constant against the bit patterns that are values at all - because the VM
// indexes its pools with unchecked operator[]. THERE IS NO CHECKSUM: neither
// the fingerprint nor the source hash covers the bytes that follow, so a
// flipped bit inside a valid field runs. An unkeyed hash would defend against
// nothing an attacker cannot recompute; if images ever land in a shared cache
// or a network, what they need is a KEYED hash.
namespace ctbrowser::script {

// Everything about this build whose meaning an image depends on. A mismatch
// means the bytes describe a different engine, whatever else they contain.
[[nodiscard]] std::uint64_t image_fingerprint() noexcept;

// A hash of the JavaScript an image was built from - "was this built from this
// source", where the fingerprint asks "was this built by this engine". Written
// even when the source itself is dropped from the image.
[[nodiscard]] std::uint64_t image_source_hash(std::string_view source) noexcept;

enum class image_option : std::uint32_t {
    // Keep `program::source`. Retaining it roughly doubles the image, and
    // dropping it changes behaviour: `f.toString()` can no longer return the
    // text. So it is KEPT unless dropping it is asked for, and the image
    // records which it is so a loader never guesses. Dropping the source drops
    // the per-instruction source offsets with it - an offset into text the
    // image no longer carries cannot become a line or a column - which is most
    // of what the debug tables cost (+52% of a source-dropped image; local
    // NAMES alone are +13% and are kept either way, because they are what
    // makes a stack trace and the AOT backend's C++ readable).
    keep_source = 0,
    drop_source = 1,
};

// The bytes. Empty when the program is one this format refuses to write - see
// `write_error` for why.
[[nodiscard]] std::vector<std::byte> write_image(const program & from,
                                                 image_option option = image_option::keep_source);

// The image format version this build writes and reads. Exposed so a manifest
// can record it without keeping a second copy of the number.
[[nodiscard]] std::uint32_t image_format_version() noexcept;

// Why the last write_image on this thread produced nothing.
[[nodiscard]] std::string_view write_error() noexcept;

// WHAT AN IMAGE SAYS ABOUT ITSELF, without decoding it. A holder of several
// images needs to key them without paying to load one.
struct image_header {
    std::uint64_t source_hash = 0;
    script_kind kind = script_kind::classic;
    // Two images of ONE script differing only in this are both valid and are
    // not interchangeable: `f.toString()` returns the text from one and
    // "[native code]" from the other.
    image_option option = image_option::keep_source;
};

// The header, or nothing at all when these bytes are not an image this build
// would load: wrong magic, another format version, or another engine.
[[nodiscard]] std::optional<image_header> read_image_header(std::span<const std::byte> bytes);

struct load_result {
    program value;
    bool ok = false;
    std::string error;
    // What the image says it was built from, whether or not it kept the text.
    std::uint64_t source_hash = 0;
    // And which kind of script it was compiled as. A DIFFERENT question from
    // the hash, which only knows the text: the same text compiled as a module
    // hashes identically and is a different program.
    script_kind kind = script_kind::classic;
};

// Bytes back to a program, or a refusal with a reason.
//
// EVERY FIELD IS VALIDATED, because the VM does not: its pool reads are
// unchecked `operator[]`, and the parameter fill in run_loop.cpp is a WRITE
// past the register window when `param_count` exceeds `frame_size`.
// `expect_source_hash`, when given, is checked against the image's own and a
// mismatch is a refusal: an image built from different source is WRONG CODE
// running at full speed. `expect_kind` DEFAULTS TO CLASSIC AND IS CHECKED
// ALWAYS, because a module's top level declares into its own scope, so running
// one where a classic script belongs publishes nothing and raises no error.
[[nodiscard]] load_result load_image(std::span<const std::byte> bytes,
                                     std::optional<std::uint64_t> expect_source_hash = {},
                                     script_kind expect_kind = script_kind::classic);

} // namespace ctbrowser::script
