#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/core/uri.hpp>

#include <array>
#include <cstdint>

namespace ctbrowser {
namespace {

std::optional<std::string> decode(std::string_view in, std::string_view keep_encoded) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '%') {
            out += in[i];
            continue;
        }
        if (i + 2 >= in.size() || hex_value(in[i + 1]) < 0 || hex_value(in[i + 2]) < 0) {
            return std::nullopt;
        }
        const auto byte =
            static_cast<unsigned char>(hex_value(in[i + 1]) * 16 + hex_value(in[i + 2]));
        if (byte < 0x80) {
            if (keep_encoded.find(static_cast<char>(byte)) != std::string_view::npos) {
                out.append(in, i, 3);
            } else {
                out += static_cast<char>(byte);
            }
            i += 2;
            continue;
        }
        // A multi-byte sequence: the lead says how many continuation
        // escapes follow, and each must be one (steps 4.d.vii-x).
        const int n = (byte & 0xE0) == 0xC0   ? 2
                      : (byte & 0xF0) == 0xE0 ? 3
                      : (byte & 0xF8) == 0xF0 ? 4
                                              : 0;
        if (n == 0 || byte < 0xC2 || byte > 0xF4 ||
            i + static_cast<std::size_t>(n) * 3 > in.size()) {
            return std::nullopt;
        }
        std::string bytes{static_cast<char>(byte)};
        std::uint32_t code = byte & (0xFFu >> (n + 1));
        for (int k = 1; k < n; ++k) {
            const std::size_t at = i + static_cast<std::size_t>(k) * 3;
            if (in[at] != '%' || hex_value(in[at + 1]) < 0 || hex_value(in[at + 2]) < 0) {
                return std::nullopt;
            }
            const auto cont =
                static_cast<unsigned char>(hex_value(in[at + 1]) * 16 + hex_value(in[at + 2]));
            if ((cont & 0xC0) != 0x80) { return std::nullopt; }
            bytes += static_cast<char>(cont);
            code = (code << 6) | (cont & 0x3F);
        }
        // Overlong, surrogate and out-of-range code points are not UTF-8.
        static constexpr std::array<std::uint32_t, 5> floor = {0, 0, 0x80, 0x800, 0x10000};
        if (code < floor[static_cast<std::size_t>(n)] || code > 0x10FFFF ||
            (code >= 0xD800 && code <= 0xDFFF)) {
            return std::nullopt;
        }
        out += bytes;
        i += static_cast<std::size_t>(n) * 3 - 1;
    }
    return out;
}

} // namespace

std::optional<std::string> decode_uri(std::string_view text) {
    return decode(text, ";/?:@&=+$,#");
}

std::optional<std::string> decode_uri_component(std::string_view text) {
    return decode(text, "");
}

} // namespace ctbrowser
