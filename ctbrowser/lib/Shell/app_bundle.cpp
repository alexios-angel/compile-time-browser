#include <ctbrowser/shell/app_bundle.hpp>

#include <ctbrowser/script/program_image.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

// The format, in one file so the writer and the reader cannot drift - the same
// rule program_image.cpp states and for the same reason.
//
//   header   magic 'CTAP', format version, engine fingerprint, entry count,
//            payload offset, payload length
//   table    one row per entry: kind, name length, name bytes, blob offset,
//            blob length
//   payload  the blobs
//
// Offsets in the table are relative to the payload, so the whole thing can be
// read from a span that starts anywhere - which is what lets a bundle live at
// the end of an executable without any of this knowing that.
namespace ctbrowser::shell {

namespace {

constexpr std::uint32_t bundle_magic = 0x43544150; // 'CTAP'
constexpr std::uint32_t bundle_format_version = 1;
constexpr char trailer_magic[8] = {'C', 'T', 'A', 'P', 'T', 'A', 'I', 'L'};
constexpr std::size_t trailer_size = sizeof(trailer_magic) + 8 + 8;

thread_local std::string last_write_error;

struct sink {
    std::vector<std::byte> bytes;
    void u8(std::uint8_t v) { bytes.push_back(static_cast<std::byte>(v)); }
    void u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) { u8(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF)); }
    }
    void u64(std::uint64_t v) {
        for (int i = 0; i < 8; ++i) { u8(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF)); }
    }
    void text(std::string_view s) {
        u32(static_cast<std::uint32_t>(s.size()));
        for (const char c : s) { u8(static_cast<std::uint8_t>(c)); }
    }
};

// Every read is bounds-checked and fails once. A reader that returned garbage
// past the end would turn a truncated file into an application.
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
        T v = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            v |= static_cast<T>(static_cast<std::uint8_t>(bytes[at + i])) << (8 * i);
        }
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

std::string_view bundle_write_error() noexcept {
    return last_write_error;
}

std::vector<std::byte> write_bundle(const app_bundle & from) {
    last_write_error = {};
    // THE TRUNCATIONS THIS FORMAT CAN COMMIT, refused rather than committed.
    // The header has promised since it was written that write_bundle returns
    // nothing when an entry is too large to describe, and `bundle_write_error`
    // was declared to say which - and neither was ever implemented, so the one
    // branch in ctcompile that reads it printed an empty string. A count and a
    // name length are both written as u32, and a static_cast that wraps would
    // produce a bundle whose table disagrees with its payload: readable,
    // internally consistent, and wrong.
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
    // THE TABLE IS WRITTEN BEFORE THE PAYLOAD AND HAS TO KNOW ITS OWN SIZE, so
    // the blob offsets are laid out first and the header is filled in with the
    // payload's position once the table's length is known.
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
    out.u32(bundle_format_version);
    // WHICH ENGINE COMPILED THE IMAGES INSIDE. A bundle is only worth anything
    // if its images load, and an image from another build is refused one layer
    // down with a message about opcode numbering - true, but a long way from
    // "this application was built by a different ctbrowser".
    out.u64(script::image_fingerprint());
    out.u32(static_cast<std::uint32_t>(from.entries.size()));
    // THE HEADER IS THIRTY-SIX BYTES and this said twenty-eight, so every blob
    // was read eight bytes early - which nothing caught, because the offsets
    // still landed inside the file and the only symptom was three program
    // images that were "not built by this engine build". Counted rather than
    // written down: magic 4, version 4, fingerprint 8, count 4, payload offset
    // 8, payload length 8.
    constexpr std::uint64_t header_bytes = 4u + 4u + 8u + 4u + 8u + 8u;
    const std::uint64_t payload_at = header_bytes + table.bytes.size();
    out.u64(payload_at);
    out.u64(payload_length);
    for (const std::byte b : table.bytes) { out.bytes.push_back(b); }
    for (const bundle_entry & one : from.entries) {
        out.bytes.insert(out.bytes.end(), one.bytes.begin(), one.bytes.end());
    }
    return std::move(out.bytes);
}

bundle_load_result read_bundle(std::span<const std::byte> bytes) {
    bundle_load_result out;
    source in{bytes, 0, false, {}};

    if (in.u32() != bundle_magic) {
        out.error = "not a ctbrowser application bundle";
        return out;
    }
    if (const std::uint32_t v = in.u32(); v != bundle_format_version) {
        out.error = "application bundle format version " + std::to_string(v) +
                    ", this build reads " + std::to_string(bundle_format_version);
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
    // A COUNT AGAINST BYTES, not against a constant. The smallest row this
    // format can encode is a kind, an empty name, an offset and a length - and
    // that is TWENTY-FOUR bytes, not the twenty this said: an empty name still
    // costs the four-byte length prefix `sink::text` always writes. The old
    // figure was safe, because every read is bounds-checked anyway, but it made
    // the pre-check twenty percent looser than its own comment claimed and cost
    // files in that band this message in favour of a vaguer one.
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
    // WHAT HAS BEEN HANDED OUT SO FAR. Each blob is checked against the payload
    // it claims to sit in, which stops any one of them pointing outside the
    // file - and says nothing about all of them pointing at the SAME bytes. A
    // ten-megabyte file whose four hundred thousand rows each claim the whole
    // payload asks this reader for four terabytes before any single check
    // fails, which on a machine with 7.5 GiB is an OOM kill rather than a
    // refusal. A bundle's blobs do not overlap, so their sizes cannot exceed
    // the payload that holds them.
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
        // EVERY BLOB IS CHECKED AGAINST THE PAYLOAD IT CLAIMS TO BE IN, and the
        // arithmetic is written so it cannot overflow: a length added to an
        // offset is exactly how a table out of a file gets a reader to read
        // somewhere else entirely.
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
    std::vector<std::byte> out{launcher.begin(), launcher.end()};
    const std::uint64_t at = out.size();
    out.insert(out.end(), bundle.begin(), bundle.end());
    for (const char c : trailer_magic) { out.push_back(static_cast<std::byte>(c)); }
    for (int i = 0; i < 8; ++i) { out.push_back(static_cast<std::byte>((at >> (8 * i)) & 0xFF)); }
    const std::uint64_t length = bundle.size();
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::byte>((length >> (8 * i)) & 0xFF));
    }
    return out;
}

std::span<const std::byte> find_appended_bundle(std::span<const std::byte> whole) {
    if (whole.size() < trailer_size) { return {}; }
    const std::size_t tail = whole.size() - trailer_size;
    for (std::size_t i = 0; i < sizeof(trailer_magic); ++i) {
        if (whole[tail + i] != static_cast<std::byte>(trailer_magic[i])) { return {}; }
    }
    const auto read64 = [&whole](std::size_t at) {
        std::uint64_t v = 0;
        for (std::size_t i = 0; i < 8; ++i) {
            v |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(whole[at + i])) << (8 * i);
        }
        return v;
    };
    const std::uint64_t at = read64(tail + sizeof(trailer_magic));
    const std::uint64_t length = read64(tail + sizeof(trailer_magic) + 8);
    // THE TRAILER MUST ACCOUNT FOR THE WHOLE FILE. A copy that was truncated,
    // or a launcher that happens to contain these eight bytes in its own data,
    // fails this and is treated as having no bundle at all rather than as
    // having one at a made-up offset.
    if (at > whole.size() || length > whole.size() - at ||
        at + length + trailer_size != whole.size()) {
        return {};
    }
    return whole.subspan(at, length);
}

std::vector<std::byte> this_executable_bytes() {
    std::error_code failed;
    // /proc/self/exe is a symlink to the running image, and reading THAT rather
    // than argv[0] is what makes a launcher work when it was found on the PATH
    // or invoked through a symlink of its own.
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", failed);
    if (failed) { return {}; }
    std::ifstream in{self, std::ios::binary};
    if (!in) { return {}; }
    const std::string raw{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    std::vector<std::byte> out(raw.size());
    if (!raw.empty()) { std::memcpy(out.data(), raw.data(), raw.size()); }
    return out;
}

} // namespace ctbrowser::shell
