#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ctbrowser/script/bytecode.hpp>

// A COMPILED PROGRAM, AS BYTES, so a packaged application never parses its own
// JavaScript.
//
// This is the largest number this project can delete. `docs/performance.md`
// profiles a whole p5 page load as 17.5% lexing, 15.1% declare_local and 7.6%
// collect_captured_names against 1.4% in the interpreter - about forty percent
// of a page load is READING JavaScript. Measured per corpus in
// ctcompile/docs/baseline/startup.json: babylon 264 ms, phaser 70, p5 53, every
// single start.
//
// THE WRITER AND THE READER LIVE TOGETHER, IN THE RUNTIME, and both halves are
// here for the same two reasons. The reader must exist in a runtime-only build -
// the `browser-no-llvm` preset has no ctcompile in the tree at all - and a
// writer in another project would be a second walk over the same twenty fields
// that is free to drift from this one. One file, one field order, one version.
//
// WHAT IT IS NOT: a stable interchange format. An image is written and read by
// ONE build of this engine. `enum class op` is renumbered by Phases 13 and 14,
// and an image written before that is meaningless afterwards - so the header
// carries a fingerprint of the things whose meaning it depends on, and a
// mismatch is a REFUSAL rather than a best effort.
//
// AN IMAGE IS EXECUTABLE INPUT AND IS NOT AUTHENTICATED. `load` validates
// structure exhaustively - every index, every operand, every count - so a
// corrupt file is refused rather than run. That is sufficient for a build
// artefact this build produced and consumed. It is NOT sufficient for an image
// that could be written by somebody else: the checksum is unkeyed, and the VM
// indexes its pools with unchecked operator[] once a program is in its hands.
// If images ever land in a shared cache, a user-writable directory or a
// network, they need a keyed hash first, and that is a decision to take before
// they move rather than after.
namespace ctbrowser::script {

// Everything about this build whose meaning an image depends on. A mismatch
// means the bytes describe a different engine, whatever else they contain.
[[nodiscard]] std::uint64_t image_fingerprint() noexcept;

// A hash of the JavaScript an image was built from. Written into every image,
// and a DIFFERENT question from the fingerprint: that one asks "was this built
// by this engine", this one asks "was this built from this source". A cache
// that answers only the first will happily run yesterday's code.
//
// It is written even when the source itself is dropped from the image, because
// the question still has to be answerable then - that is exactly the build
// where nothing else could answer it.
[[nodiscard]] std::uint64_t image_source_hash(std::string_view source) noexcept;

enum class image_option : std::uint32_t {
    // Keep `program::source`. Retaining it roughly doubles the image - babylon
    // goes to 27 MB against 11.3 MB of source - and dropping it changes
    // behaviour: `f.toString()` can no longer return the text, and p5's own
    // error system reads its source. So it is KEPT unless dropping it is asked
    // for, and the image records which it is so a loader never guesses.
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
// images needs to key them without paying to load one, and the header is the
// only place that answer lives - recomputing it from the source text would mean
// having the source text, which is the thing an image exists to avoid.
struct image_header {
    std::uint64_t source_hash = 0;
    script_kind kind = script_kind::classic;
    // AND WHETHER IT KEPT THE SOURCE, which a holder of several images needs
    // and which this deliberately did not report at first. Two images of ONE
    // script differing only in this are both valid and are not interchangeable:
    // `f.toString()` returns the text from one and "[native code]" from the
    // other. A holder that cannot see the difference cannot avoid choosing at
    // random.
    image_option option = image_option::keep_source;
};

// The header, or nothing at all when these bytes are not an image this build
// would load: wrong magic, another format version, or another engine. Refusing
// at the door means a packager hears about it when it hands the image over
// rather than months later as a cache that never hits.
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
// EVERY FIELD IS VALIDATED, because the VM does not. Its pool reads are
// unchecked `operator[]` and one of them - the parameter fill at
// run_loop.cpp:1012 - is a WRITE past the register window when `param_count`
// exceeds `frame_size`. A compiled program cannot express that; an image can,
// so this is where it stops.
// `expect_source_hash`, when given, is checked against the image's own and a
// mismatch is a refusal. That is the whole cache-correctness story: an image
// built from different source is not a slow path, it is WRONG CODE running at
// full speed, and the only defence is to refuse it here.
// `expect_kind` DEFAULTS TO CLASSIC AND IS CHECKED ALWAYS, not only when a hash
// is given, because a mismatch here is not a cache miss - it is a program with
// the right text and the wrong scope rules. A module's top level declares into
// its own scope, so running one where a classic script belongs publishes
// nothing to the page and raises no error at all.
[[nodiscard]] load_result load_image(std::span<const std::byte> bytes,
                                     std::optional<std::uint64_t> expect_source_hash = {},
                                     script_kind expect_kind = script_kind::classic);

} // namespace ctbrowser::script
