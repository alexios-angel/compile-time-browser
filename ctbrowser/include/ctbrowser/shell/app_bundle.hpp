#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// A PACKAGED APPLICATION, AS BYTES: the page, the resources it asks for, and
// its scripts already compiled.
//
// This is what `ctcompile` emits and what a launcher runs. It exists because
// the two halves of a fast start have to travel together: an image only saves
// the parse if it MATCHES the script, so an HTML file and a set of images that
// can be copied apart are an HTML file and a set of images that can drift - and
// the failure is a silent recompile, which is the one this project treats as
// worst because nothing reports it.
//
// EVERYTHING IS LITTLE-ENDIAN AND EXPLICIT, for the reason program_image.cpp
// gives at length: a struct written by memcpy carries its padding, and padding
// differs between builds of the writer. Every field here is written a byte at a
// time.
//
// AN ENTRY IS FOUND BY NAME, AND THE NAME IS WHATEVER THE DOCUMENT SAID.
// p5-basic.html asks for `../../vendor/p5/p5.js`, which is not a path relative
// to the application directory and is not anything a packager could re-derive
// without reproducing the engine's own resolution rules. A name-to-bytes table
// does not care what a name looks like; a directory of files does, which is
// most of why this is a table.
//
// IT IS NOT AUTHENTICATED. Same position as the program image: this is a build
// artefact a build produced and a launcher consumes, and the checks below are
// there to refuse a TRUNCATED or MISMATCHED file rather than a hostile one. If
// bundles ever travel somewhere untrusted they need a keyed hash first, and
// that is a decision to take before they move rather than after.
namespace ctbrowser::shell {

// What one entry in a bundle is. `meta` rows are key/value strings describing
// the application; the rest are bytes the engine consumes directly.
enum class bundle_kind : std::uint32_t {
    meta = 0,
    html = 1,
    asset = 2,
    script_image = 3,
};

struct bundle_entry {
    bundle_kind kind = bundle_kind::asset;
    // For `asset`, the name the page asks for. For `meta`, the key. For `html`
    // and `script_image`, empty - there is one document, and an image is
    // matched to its script by the source hash it already carries rather than
    // by anything written here.
    std::string name;
    std::vector<std::byte> bytes;
};

struct app_bundle {
    std::vector<bundle_entry> entries;

    // The convenience the launcher actually uses.
    [[nodiscard]] std::span<const std::byte> html() const;
    [[nodiscard]] std::string meta(std::string_view key) const;
};

// The bytes. Empty only if an entry is too large to describe, which no real
// application reaches; see `bundle_write_error`.
[[nodiscard]] std::vector<std::byte> write_bundle(const app_bundle & from);
[[nodiscard]] std::string_view bundle_write_error() noexcept;

struct bundle_load_result {
    app_bundle value;
    bool ok = false;
    std::string error;
};

// Bytes back to a bundle, or a refusal with a reason.
//
// EVERY OFFSET IS CHECKED AGAINST WHAT IS ACTUALLY THERE, because a table read
// out of a file is a set of instructions to go and look somewhere. The engine
// fingerprint is checked too: a bundle's images were compiled by one build of
// this engine, and one built by another describes different instructions with
// the same bytes.
[[nodiscard]] bundle_load_result read_bundle(std::span<const std::byte> bytes);

// AND THE SAME BUNDLE HIDDEN AT THE END OF AN EXECUTABLE, which is what makes
// the output of `ctcompile` a single file a user can copy and run.
//
// A linked ELF or PE does not care what follows its last section - verified on
// this engine's own binary: 4.5 MB appended to `ctbrowse` left it running and
// its screenshot byte-identical. So a packaged application is a prebuilt
// launcher with a bundle stuck on the end and a trailer saying where it starts.
//
// Returns an empty span when there is no trailer, which is the ordinary case
// for the launcher itself.
[[nodiscard]] std::vector<std::byte> append_bundle_to(std::span<const std::byte> launcher,
                                                      std::span<const std::byte> bundle);
[[nodiscard]] std::span<const std::byte> find_appended_bundle(std::span<const std::byte> whole);

// The running executable's own bytes, for a launcher that has to find the
// bundle stuck to itself. Empty when the platform will not say.
[[nodiscard]] std::vector<std::byte> this_executable_bytes();

} // namespace ctbrowser::shell
