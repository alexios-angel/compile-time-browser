#pragma once

#include <cstdint>
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

// Why the last write_image on this thread produced nothing.
[[nodiscard]] std::string_view write_error() noexcept;

struct load_result {
    program value;
    bool ok = false;
    std::string error;
};

// Bytes back to a program, or a refusal with a reason.
//
// EVERY FIELD IS VALIDATED, because the VM does not. Its pool reads are
// unchecked `operator[]` and one of them - the parameter fill at
// run_loop.cpp:1012 - is a WRITE past the register window when `param_count`
// exceeds `frame_size`. A compiled program cannot express that; an image can,
// so this is where it stops.
[[nodiscard]] load_result load_image(std::span<const std::byte> bytes);

} // namespace ctbrowser::script
