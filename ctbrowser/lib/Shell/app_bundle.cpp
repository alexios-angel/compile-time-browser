//===- app_bundle.cpp - Application bundle reader and writer ----*- C++ -*-===//
//
// Reads and writes the application bundle format declared in app_bundle.hpp,
// and appends a bundle to (or finds one at the end of) a launcher executable.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/shell/app_bundle.hpp>

#include <ctbrowser/script/program_image.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

// The format, in one file so the writer and the reader cannot drift (the same
// rule program_image/internal.hpp states, for the same reason):
//
//   header   magic 'CTAP', format version, engine fingerprint, entry count,
//            payload offset, payload length
//   table    one row per entry: kind, name length, name bytes, blob offset,
//            blob length
//   payload  the blobs
//   trailer  only when appended to an executable (append_bundle_to):
//            'CTAPTAIL', bundle offset u64, bundle length u64, as the
//            last 24 bytes of the file
//
// Offsets in the table are relative to the payload, so the bundle can be read
// from a span that starts anywhere, which lets it live at the end of an
// executable without the reader knowing.
namespace ctbrowser::shell {

namespace {

constexpr std::uint32_t bundle_magic = 0x43544150; // 'CTAP'
constexpr std::uint32_t bundle_format_version_value = 1;
constexpr char trailer_magic[8] = {'C', 'T', 'A', 'P', 'T', 'A', 'I', 'L'};
constexpr std::size_t trailer_size = sizeof(trailer_magic) + 8 + 8;

thread_local std::string last_write_error;

/// Every integer in the format is little-endian, whatever the host is.
template <typename T> [[nodiscard]] std::array<std::byte, sizeof(T)> little_bytes(T v) {
    if constexpr (std::endian::native == std::endian::big) { v = std::byteswap(v); }
    return std::bit_cast<std::array<std::byte, sizeof(T)>>(v);
}
template <typename T> [[nodiscard]] T from_little(std::span<const std::byte> at) {
    std::array<std::byte, sizeof(T)> raw;
    std::copy_n(at.begin(), sizeof(T), raw.begin());
    T v = std::bit_cast<T>(raw);
    if constexpr (std::endian::native == std::endian::big) { v = std::byteswap(v); }
    return v;
}

/// Appends little-endian values and length-prefixed strings to a buffer.
struct sink {
    std::vector<std::byte> bytes;
    void append(std::span<const std::byte> more) {
        bytes.insert(bytes.end(), more.begin(), more.end());
    }
    void u32(std::uint32_t v) { append(little_bytes(v)); }
    void u64(std::uint64_t v) { append(little_bytes(v)); }
    void text(std::string_view s) {
        u32(static_cast<std::uint32_t>(s.size()));
        append(std::as_bytes(std::span{s}));
    }
};

/// A bounds-checked reader over a byte span. The first failure sets `bad`
/// and `why`; later reads return zero. A reader that returned garbage past
/// the end would turn a truncated file into an application.
struct source {
    std::span<const std::byte> bytes;
    std::size_t at = 0;
    bool bad = false;
    std::string why;

    void fail(std::string reason) {
        if (!bad) {
            bad = true;
            why = std::move(reason);
        }
    }
    [[nodiscard]] bool need(std::size_t n) {
        if (bad) { return false; }
        if (n > bytes.size() || at > bytes.size() - n) {
            fail("the bundle ends in the middle of a value");
            return false;
        }
        return true;
    }
    template <typename T> T little() {
        if (!need(sizeof(T))) { return 0; }
        const T v = from_little<T>(bytes.subspan(at, sizeof(T)));
        at += sizeof(T);
        return v;
    }
    std::uint32_t u32() { return little<std::uint32_t>(); }
    std::uint64_t u64() { return little<std::uint64_t>(); }
    std::string text() {
        const std::uint32_t n = u32();
        // A length prefix is the classic way to ask a reader to allocate four
        // gigabytes: check it against what is left before reserving anything.
        if (!need(n)) { return {}; }
        std::string out(reinterpret_cast<const char *>(bytes.data() + at), n);
        at += n;
        return out;
    }
};

} // namespace

std::span<const std::byte> app_bundle::html() const {
    for (const bundle_entry & one : entries) {
        if (one.kind == bundle_kind::html) { return one.bytes; }
    }
    return {};
}

std::string app_bundle::meta(std::string_view key) const {
    for (const bundle_entry & one : entries) {
        if (one.kind == bundle_kind::meta && one.name == key) {
            return std::string{reinterpret_cast<const char *>(one.bytes.data()), one.bytes.size()};
        }
    }
    return {};
}

std::uint32_t bundle_format_version() noexcept {
    return bundle_format_version_value;
}

std::string_view bundle_write_error() noexcept {
    return last_write_error;
}

std::vector<std::byte> write_bundle(const app_bundle & from) {
    last_write_error = {};
    // The entry count and each name length are stored as u32. Refuse values
    // that do not fit, with the reason in bundle_write_error(), instead of
    // truncating them into a bundle whose table disagrees with its payload.
    if (from.entries.size() > 0xFFFFFFFFull) {
        last_write_error = "an application bundle cannot hold more than 4294967295 entries";
        return {};
    }
    for (const bundle_entry & one : from.entries) {
        if (one.name.size() > 0xFFFFFFFFull) {
            last_write_error = "an entry name of " + std::to_string(one.name.size()) +
                               " bytes is too long to record";
            return {};
        }
    }
    // The table precedes the payload, so the header's payload offset depends
    // on the table's size: lay out the table first, then write the header.
    sink table;
    std::uint64_t payload_length = 0;
    for (const bundle_entry & one : from.entries) {
        table.u32(static_cast<std::uint32_t>(one.kind));
        table.text(one.name);
        table.u64(payload_length);
        table.u64(one.bytes.size());
        payload_length += one.bytes.size();
    }

    sink out;
    out.u32(bundle_magic);
    out.u32(bundle_format_version_value);
    // Which engine compiled the images inside. An image from another build is
    // refused one layer down with a message about opcode numbering; checking
    // here lets read_bundle say plainly that a different build packaged it.
    out.u64(script::image_fingerprint());
    out.u32(static_cast<std::uint32_t>(from.entries.size()));
    // Header size: magic 4 + version 4 + fingerprint 8 + entry count 4 +
    // payload offset 8 + payload length 8 = 36 bytes.
    constexpr std::uint64_t header_bytes = 4u + 4u + 8u + 4u + 8u + 8u;
    const std::uint64_t payload_at = header_bytes + table.bytes.size();
    out.u64(payload_at);
    out.u64(payload_length);
    out.append(table.bytes);
    for (const bundle_entry & one : from.entries) { out.append(one.bytes); }
    return std::move(out.bytes);
}

bundle_load_result read_bundle(std::span<const std::byte> bytes) {
    bundle_load_result out;
    source in{bytes, 0, false, {}};

    if (in.u32() != bundle_magic) {
        out.error = "not a ctbrowser application bundle";
        return out;
    }
    if (const std::uint32_t v = in.u32(); v != bundle_format_version_value) {
        out.error = "application bundle format version " + std::to_string(v) +
                    ", this build reads " + std::to_string(bundle_format_version_value);
        return out;
    }
    if (const std::uint64_t got = in.u64(); got != script::image_fingerprint()) {
        out.error = "this application was packaged by a different ctbrowser build - its "
                    "compiled scripts describe a different engine";
        return out;
    }
    const std::uint32_t count = in.u32();
    const std::uint64_t payload_at = in.u64();
    const std::uint64_t payload_length = in.u64();
    if (in.bad) {
        out.error = in.why;
        return out;
    }
    // Reject an entry count the remaining bytes cannot hold. The smallest row
    // is 24 bytes: kind 4, name-length prefix 4 (empty name), offset 8, length
    // 8.
    constexpr std::size_t least_bytes_per_entry = 4 + 4 + 8 + 8;
    if (!in.need(static_cast<std::size_t>(count) * least_bytes_per_entry)) {
        out.error = "the bundle claims more entries than its bytes could describe";
        return out;
    }
    if (payload_at > bytes.size() || payload_length > bytes.size() - payload_at) {
        out.error = "the bundle's payload does not fit inside it";
        return out;
    }

    out.value.entries.reserve(count);
    // Total bytes handed out so far. The per-blob check below keeps each blob
    // inside the payload but not apart from the others; since blobs never
    // overlap, their total cannot exceed the payload. Without this, a small
    // file whose rows all claim the whole payload would make the reader copy
    // terabytes.
    std::uint64_t claimed = 0;
    for (std::uint32_t i = 0; i < count && !in.bad; ++i) {
        bundle_entry one;
        const std::uint32_t kind = in.u32();
        if (kind > static_cast<std::uint32_t>(bundle_kind::script_image)) {
            in.fail("entry " + std::to_string(i) + " has an unknown kind " + std::to_string(kind));
            break;
        }
        one.kind = static_cast<bundle_kind>(kind);
        one.name = in.text();
        const std::uint64_t blob_at = in.u64();
        const std::uint64_t blob_length = in.u64();
        if (in.bad) { break; }
        // Check the blob against the payload, written so the arithmetic cannot
        // overflow: an offset plus a length that wraps would let a crafted
        // table point the reader anywhere.
        if (blob_at > payload_length || blob_length > payload_length - blob_at) {
            in.fail("entry " + std::to_string(i) + " points outside the bundle's payload");
            break;
        }
        claimed += blob_length;
        if (claimed > payload_length) {
            in.fail("the bundle's entries claim more bytes than its payload holds");
            break;
        }
        const std::byte * from = bytes.data() + payload_at + blob_at;
        one.bytes.assign(from, from + blob_length);
        out.value.entries.push_back(std::move(one));
    }
    if (in.bad) {
        out.error = in.why;
        return out;
    }
    if (out.value.html().empty()) {
        out.error = "the bundle carries no document - there is nothing to run";
        return out;
    }
    out.ok = true;
    return out;
}

std::vector<std::byte> append_bundle_to(std::span<const std::byte> launcher,
                                        std::span<const std::byte> bundle) {
    sink out;
    out.append(launcher);
    const std::uint64_t at = out.bytes.size();
    out.append(bundle);
    out.append(std::as_bytes(std::span{trailer_magic}));
    out.u64(at);
    out.u64(bundle.size());
    return std::move(out.bytes);
}

std::span<const std::byte> find_appended_bundle(std::span<const std::byte> whole) {
    if (whole.size() < trailer_size) { return {}; }
    const std::size_t tail = whole.size() - trailer_size;
    const std::span<const std::byte> magic = std::as_bytes(std::span{trailer_magic});
    if (!std::ranges::equal(whole.subspan(tail, magic.size()), magic)) { return {}; }
    const std::uint64_t at = from_little<std::uint64_t>(whole.subspan(tail + magic.size(), 8));
    const std::uint64_t length =
        from_little<std::uint64_t>(whole.subspan(tail + magic.size() + 8, 8));
    // The trailer must account for the whole file. A truncated copy, or a
    // launcher that happens to contain these eight bytes in its own data,
    // fails this and is treated as having no bundle rather than one at a
    // made-up offset.
    if (at > whole.size() || length > whole.size() - at ||
        at + length + trailer_size != whole.size()) {
        return {};
    }
    return whole.subspan(at, length);
}

std::vector<std::byte> this_executable_bytes() {
    std::error_code failed;
    // /proc/self/exe is a symlink to the running image. Reading it rather than
    // argv[0] makes a launcher work when it was found on the PATH or invoked
    // through a symlink of its own.
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", failed);
    if (failed) { return {}; }
    std::ifstream in{self, std::ios::binary};
    if (!in) { return {}; }
    const std::string raw{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    const std::span<const std::byte> bytes = std::as_bytes(std::span{raw});
    return {bytes.begin(), bytes.end()};
}

} // namespace ctbrowser::shell
