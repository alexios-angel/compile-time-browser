//===- app_bundle.hpp - Packaged application format -------------*- C++ -*-===//
//
// A packaged application as bytes: the page, the resources it asks for, and
// its scripts already compiled to program images.
//
// This is what `ctcompile` emits and what a launcher runs. The page and its
// program images travel in one file because an image only saves the parse if
// it matches its script; files that can be copied apart can drift, and the
// result is a silent recompile that nothing reports.
//
// Everything is little-endian and written a byte at a time, for the reason
// program_image/internal.hpp gives: a struct written by memcpy carries its
// padding, and padding differs between builds of the writer.
//
// An entry is found by name, and the name is exactly what the document wrote.
// p5-basic.html asks for `../../vendor/p5/p5.js`, which is not relative to the
// application directory and could not be re-derived without reproducing the
// engine's resolution rules. A name-to-bytes table does not care what a name
// looks like; a directory of files would.
//
// The format is not authenticated. Like the program image, it is a build
// artefact, and the checks in read_bundle refuse a truncated or mismatched
// file, not a hostile one. Bundles that travel somewhere untrusted need a keyed
// hash first.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ctbrowser::shell {

/// What one entry in a bundle is. `meta` rows are key/value strings describing
/// the application; the rest are bytes the engine consumes directly.
enum class bundle_kind : std::uint32_t {
    meta = 0,
    html = 1,
    asset = 2,
    script_image = 3,
};

/// One named blob in a bundle.
struct bundle_entry {
    bundle_kind kind = bundle_kind::asset;
    /// For `asset`, the name the page asks for. For `meta`, the key. For `html`
    /// and `script_image`, empty: there is one document, and an image is
    /// matched to its script by the source hash it already carries.
    std::string name;
    std::vector<std::byte> bytes;
};

/// A packaged application: its entries in the order they were written.
struct app_bundle {
    std::vector<bundle_entry> entries;

    /// The bytes of the first `html` entry, or an empty span when there is none.
    [[nodiscard]] std::span<const std::byte> html() const;
    /// The value of the first `meta` entry named \p key, or empty.
    [[nodiscard]] std::string meta(std::string_view key) const;
};

/// Serialises \p from. Empty only if an entry is too large to describe, which
/// no real application reaches; bundle_write_error() then says why.
[[nodiscard]] std::vector<std::byte> write_bundle(const app_bundle & from);

/// The format version this build writes and reads. Exposed so a manifest can
/// record it without keeping a second copy of the number.
[[nodiscard]] std::uint32_t bundle_format_version() noexcept;
/// Why the last write_bundle call returned nothing; empty after a success.
[[nodiscard]] std::string_view bundle_write_error() noexcept;

/// The result of read_bundle: `value` when `ok`, otherwise `error` says why.
struct bundle_load_result {
    app_bundle value;
    bool ok = false;
    std::string error;
};

/// Parses \p bytes back into a bundle, or refuses with a reason.
///
/// Every offset is checked against the bytes actually present, because a table
/// read out of a file is a set of instructions to go and look somewhere. The
/// engine fingerprint is checked too: a bundle's program images were compiled
/// by one build of this engine, and another build would read the same bytes as
/// different instructions.
[[nodiscard]] bundle_load_result read_bundle(std::span<const std::byte> bytes);

// A bundle can also be appended to an executable, which makes the output of
// `ctcompile` a single file a user can copy and run. A linked ELF or PE does
// not care what follows its last section (4.5 MB appended to `ctbrowse` left it
// running and its screenshot byte-identical), so a packaged application is a
// prebuilt launcher with a bundle on the end and a trailer saying where the
// bundle starts.

/// \p launcher followed by \p bundle and a trailer recording where it starts.
[[nodiscard]] std::vector<std::byte> append_bundle_to(std::span<const std::byte> launcher,
                                                      std::span<const std::byte> bundle);
/// The bundle appended to \p whole, or an empty span when there is no trailer
/// (the ordinary case for the bare launcher).
[[nodiscard]] std::span<const std::byte> find_appended_bundle(std::span<const std::byte> whole);

/// The running executable's own bytes, for a launcher that has to find the
/// bundle appended to itself. Empty when the platform will not say.
[[nodiscard]] std::vector<std::byte> this_executable_bytes();

} // namespace ctbrowser::shell
