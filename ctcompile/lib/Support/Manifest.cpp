#include <ctcompile/Support/Manifest.hpp>

#include <array>
#include <cstdio>
#include <string_view>

namespace ctcompile {

namespace {

// A 64-bit value as "0x…", because these are identities rather than
// quantities. A decimal program id invites arithmetic on it.
std::string hex(std::uint64_t value) {
    std::array<char, 32> buffer{};
    const int written = std::snprintf(buffer.data(), buffer.size(), "0x%016llx",
                                      static_cast<unsigned long long>(value));
    return written > 0 ? std::string{buffer.data(), static_cast<std::size_t>(written)}
                       : std::string{"0x0"};
}

} // namespace

std::string json_string(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    out += '"';
    for (const char raw : text) {
        const auto c = static_cast<unsigned char>(raw);
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            // EVERYTHING BELOW SPACE, not just the named ones. RFC 8259 forbids
            // a raw control character in a string, and the five with short
            // escapes are the only ones anybody remembers - a stray 0x01 in a
            // file name would otherwise produce JSON that half the parsers in
            // the world reject and the other half accept differently.
            if (c < 0x20) {
                std::array<char, 8> escape{};
                const int written = std::snprintf(escape.data(), escape.size(), "\\u%04x",
                                                  static_cast<unsigned>(c));
                if (written > 0) { out.append(escape.data(), static_cast<std::size_t>(written)); }
            } else {
                // BYTES ABOVE 0x7F PASS THROUGH UNTOUCHED, which is correct for
                // UTF-8 input and is what a name out of a document is. Escaping
                // them to \u would require decoding, and decoding requires
                // knowing the input is valid - which a file name is not obliged
                // to be.
                out += raw;
            }
            break;
        }
    }
    out += '"';
    return out;
}

std::string to_json(const manifest & from) {
    std::string out = "{\n";
    const auto field = [&out](std::string_view key, const std::string & value, bool comma = true) {
        out += "  ";
        out += json_string(key);
        out += ": ";
        out += value;
        out += comma ? ",\n" : "\n";
    };

    field("ctcompile", json_string(from.compiler_version));
    field("engine", json_string(from.engine));
    field("entry", json_string(from.entry));
    field("mode", json_string(from.mode));
    field("bundle_format", std::to_string(from.bundle_format));
    field("image_format", std::to_string(from.image_format));
    field("engine_fingerprint", json_string(hex(from.engine_fingerprint)));
    field("font_directory", json_string(from.font_directory));
    field("bundle_bytes", std::to_string(from.bundle_bytes));

    out += "  \"scripts\": [";
    for (std::size_t i = 0; i < from.scripts.size(); ++i) {
        const manifest_script & one = from.scripts[i];
        out += i == 0 ? "\n" : ",\n";
        out += "    { \"index\": " + std::to_string(one.index) +
               ", \"program_id\": " + json_string(hex(one.program_id)) +
               ", \"source_bytes\": " + std::to_string(one.source_bytes) +
               ", \"image_bytes\": " + std::to_string(one.image_bytes) +
               ", \"functions\": " + std::to_string(one.functions) + " }";
    }
    out += from.scripts.empty() ? "],\n" : "\n  ],\n";

    out += "  \"resources\": [";
    for (std::size_t i = 0; i < from.resources.size(); ++i) {
        const manifest_resource & one = from.resources[i];
        out += i == 0 ? "\n" : ",\n";
        out += "    { \"name\": " + json_string(one.name) +
               ", \"bytes\": " + std::to_string(one.bytes) + " }";
    }
    out += from.resources.empty() ? "]\n" : "\n  ]\n";

    out += "}\n";
    return out;
}

} // namespace ctcompile
