#include "url_internal.hpp"

namespace ctbrowser::shell::url_detail {
[[nodiscard]] bool windows_drive_letter(std::u32string_view s) noexcept {
    return s.size() == 2 && is_alpha(s[0]) && (s[1] == ':' || s[1] == '|');
}
[[nodiscard]] bool normalized_windows_drive_letter(std::string_view s) noexcept {
    return s.size() == 2 && is_alpha(static_cast<unsigned char>(s[0])) && s[1] == ':';
}
[[nodiscard]] bool starts_with_windows_drive_letter(std::u32string_view s) noexcept {
    return s.size() >= 2 && windows_drive_letter(s.substr(0, 2)) &&
           (s.size() == 2 || s[2] == '/' || s[2] == '\\' || s[2] == '?' || s[2] == '#');
}
[[nodiscard]] bool single_dot(std::string_view s) noexcept {
    return s == "." || ascii_iequals(s, "%2e");
}
[[nodiscard]] bool double_dot(std::string_view s) noexcept {
    return s == ".." || ascii_iequals(s, ".%2e") || ascii_iequals(s, "%2e.") ||
           ascii_iequals(s, "%2e%2e");
}

// §4.4 "shorten a URL's path".
void shorten_path(url_record & url) {
    if (url.scheme == "file" && url.path.size() == 1 &&
        normalized_windows_drive_letter(url.path[0])) {
        return;
    }
    if (!url.path.empty()) { url.path.pop_back(); }
}

// The state machine. `url` is fresh for a plain parse and the record being
// written for a setter (`override` given); false is the standard's failure.
[[nodiscard]] bool basic_parse(std::string_view raw, const url_record * base, url_record & url,
                               std::optional<state> override) {
    std::string cleaned;
    if (!override) {
        // Steps 1-2: leading and trailing C0 controls and spaces go.
        while (!raw.empty() && static_cast<unsigned char>(raw.front()) <= 0x20) {
            raw.remove_prefix(1);
        }
        while (!raw.empty() && static_cast<unsigned char>(raw.back()) <= 0x20) {
            raw.remove_suffix(1);
        }
    }
    // Step 3: tabs and newlines go from anywhere.
    cleaned.reserve(raw.size());
    for (const char c : raw) {
        if (c != '\t' && c != '\n' && c != '\r') { cleaned.push_back(c); }
    }
    const std::u32string input = code_points(cleaned);
    const auto n = static_cast<std::ptrdiff_t>(input.size());

    state st = override.value_or(state::scheme_start);
    std::u32string buffer;
    bool at_sign_seen = false;
    bool inside_brackets = false;
    bool password_token_seen = false;
    std::ptrdiff_t p = 0;

    const auto remaining_starts_with = [&](std::u32string_view prefix) {
        return p + 1 + static_cast<std::ptrdiff_t>(prefix.size()) <= n &&
               std::u32string_view{input}.substr(static_cast<std::size_t>(p + 1), prefix.size()) ==
                   prefix;
    };
    const auto from_pointer = [&]() {
        return std::u32string_view{input}.substr(
            static_cast<std::size_t>(std::max<std::ptrdiff_t>(p, 0)));
    };

    while (true) {
        const char32_t c = p >= 0 && p < n ? input[static_cast<std::size_t>(p)] : eof;
        const bool special = url.is_special();
        switch (st) {
        case state::scheme_start:
            if (is_alpha(c)) {
                buffer.push_back(lower(c));
                st = state::scheme;
            } else if (!override) {
                st = state::no_scheme;
                --p;
            } else {
                return false;
            }
            break;

        case state::scheme:
            if (is_alpha(c) || is_digit(c) || c == '+' || c == '-' || c == '.') {
                buffer.push_back(lower(c));
            } else if (c == ':') {
                const std::string scheme = utf8_of(buffer);
                if (override) {
                    if (special_scheme(url.scheme) != special_scheme(scheme)) { return true; }
                    if ((url.has_credentials() || url.port) && scheme == "file") { return true; }
                    if (url.scheme == "file" && url.host && url.host->empty()) { return true; }
                }
                url.scheme = scheme;
                if (override) {
                    if (url.port && url.port == default_port(url.scheme)) { url.port.reset(); }
                    return true;
                }
                buffer.clear();
                if (url.scheme == "file") {
                    st = state::file;
                } else if (url.is_special() && base != nullptr && base->scheme == url.scheme) {
                    st = state::special_relative_or_authority;
                } else if (url.is_special()) {
                    st = state::special_authority_slashes;
                } else if (remaining_starts_with(U"/")) {
                    st = state::path_or_authority;
                    ++p;
                } else {
                    url.opaque_path = true;
                    url.path = {std::string{}};
                    st = state::opaque_path;
                }
            } else if (!override) {
                buffer.clear();
                st = state::no_scheme;
                p = -1;
            } else {
                return false;
            }
            break;

        case state::no_scheme:
            if (base == nullptr || (base->opaque_path && c != '#')) { return false; }
            if (base->opaque_path && c == '#') {
                url.scheme = base->scheme;
                url.path = base->path;
                url.opaque_path = true;
                url.query = base->query;
                url.fragment = std::string{};
                st = state::fragment;
            } else if (base->scheme != "file") {
                st = state::relative;
                --p;
            } else {
                st = state::file;
                --p;
            }
            break;

        case state::special_relative_or_authority:
            if (c == '/' && remaining_starts_with(U"/")) {
                st = state::special_authority_ignore_slashes;
                ++p;
            } else {
                st = state::relative;
                --p;
            }
            break;

        case state::path_or_authority:
            if (c == '/') {
                st = state::authority;
            } else {
                st = state::path;
                --p;
            }
            break;

        case state::relative:
            url.scheme = base->scheme;
            if (c == '/' || (url.is_special() && c == '\\')) {
                st = state::relative_slash;
            } else {
                url.username = base->username;
                url.password = base->password;
                url.host = base->host;
                url.port = base->port;
                url.path = base->path;
                url.opaque_path = base->opaque_path;
                url.query = base->query;
                if (c == '?') {
                    url.query = std::string{};
                    st = state::query;
                } else if (c == '#') {
                    url.fragment = std::string{};
                    st = state::fragment;
                } else if (c != eof) {
                    url.query.reset();
                    shorten_path(url);
                    st = state::path;
                    --p;
                }
            }
            break;

        case state::relative_slash:
            if (url.is_special() && (c == '/' || c == '\\')) {
                st = state::special_authority_ignore_slashes;
            } else if (c == '/') {
                st = state::authority;
            } else {
                url.username = base->username;
                url.password = base->password;
                url.host = base->host;
                url.port = base->port;
                st = state::path;
                --p;
            }
            break;

        case state::special_authority_slashes:
            st = state::special_authority_ignore_slashes;
            if (c == '/' && remaining_starts_with(U"/")) {
                ++p;
            } else {
                --p;
            }
            break;

        case state::special_authority_ignore_slashes:
            if (c != '/' && c != '\\') {
                st = state::authority;
                --p;
            }
            break;

        case state::authority:
            if (c == '@') {
                if (at_sign_seen) { buffer.insert(0, U"%40"); }
                at_sign_seen = true;
                for (const char32_t each : buffer) {
                    if (each == ':' && !password_token_seen) {
                        password_token_seen = true;
                        continue;
                    }
                    percent_encode(password_token_seen ? url.password : url.username, each,
                                   encode_set::userinfo);
                }
                buffer.clear();
            } else if (c == eof || c == '/' || c == '?' || c == '#' || (special && c == '\\')) {
                if (at_sign_seen && buffer.empty()) { return false; }
                p -= static_cast<std::ptrdiff_t>(buffer.size()) + 1;
                buffer.clear();
                st = state::host;
            } else {
                buffer.push_back(c);
            }
            break;

        case state::host:
        case state::hostname:
            if (override && url.scheme == "file") {
                --p;
                st = state::file_host;
            } else if (c == ':' && !inside_brackets) {
                if (buffer.empty()) { return false; }
                if (override == state::hostname) { return true; }
                const std::optional<std::string> host = parse_host(buffer, !special);
                if (!host) { return false; }
                url.host = *host;
                buffer.clear();
                st = state::port;
            } else if (c == eof || c == '/' || c == '?' || c == '#' || (special && c == '\\')) {
                --p;
                if (special && buffer.empty()) { return false; }
                if (override && buffer.empty() && (url.has_credentials() || url.port)) {
                    return true;
                }
                const std::optional<std::string> host = parse_host(buffer, !special);
                if (!host) { return false; }
                url.host = *host;
                buffer.clear();
                st = state::path_start;
                if (override) { return true; }
            } else {
                if (c == '[') { inside_brackets = true; }
                if (c == ']') { inside_brackets = false; }
                buffer.push_back(c);
            }
            break;

        case state::port:
            if (is_digit(c)) {
                buffer.push_back(c);
            } else if (c == eof || c == '/' || c == '?' || c == '#' || (special && c == '\\') ||
                       override) {
                if (!buffer.empty()) {
                    std::uint32_t port = 0;
                    for (const char32_t d : buffer) {
                        port = std::min<std::uint32_t>(port * 10 + (d - '0'), 70000);
                    }
                    if (port > 65535) { return false; }
                    const auto value = static_cast<std::uint16_t>(port);
                    url.port = default_port(url.scheme) == value
                                   ? std::nullopt
                                   : std::optional<std::uint16_t>{value};
                    buffer.clear();
                }
                if (override) { return true; }
                st = state::path_start;
                --p;
            } else {
                return false;
            }
            break;

        case state::file:
            url.scheme = "file";
            url.host = std::string{};
            if (c == '/' || c == '\\') {
                st = state::file_slash;
            } else if (base != nullptr && base->scheme == "file") {
                url.host = base->host;
                url.path = base->path;
                url.opaque_path = base->opaque_path;
                url.query = base->query;
                if (c == '?') {
                    url.query = std::string{};
                    st = state::query;
                } else if (c == '#') {
                    url.fragment = std::string{};
                    st = state::fragment;
                } else if (c != eof) {
                    url.query.reset();
                    if (!starts_with_windows_drive_letter(from_pointer())) {
                        shorten_path(url);
                    } else {
                        url.path.clear();
                    }
                    st = state::path;
                    --p;
                }
            } else {
                st = state::path;
                --p;
            }
            break;

        case state::file_slash:
            if (c == '/' || c == '\\') {
                st = state::file_host;
            } else {
                if (base != nullptr && base->scheme == "file") {
                    url.host = base->host;
                    if (!starts_with_windows_drive_letter(from_pointer()) && !base->path.empty() &&
                        normalized_windows_drive_letter(base->path[0])) {
                        url.path.push_back(base->path[0]);
                    }
                }
                st = state::path;
                --p;
            }
            break;

        case state::file_host:
            if (c == eof || c == '/' || c == '\\' || c == '?' || c == '#') {
                --p;
                if (!override && windows_drive_letter(buffer)) {
                    st = state::path;
                } else if (buffer.empty()) {
                    url.host = std::string{};
                    if (override) { return true; }
                    st = state::path_start;
                } else {
                    std::optional<std::string> host = parse_host(buffer, !special);
                    if (!host) { return false; }
                    if (*host == "localhost") { host->clear(); }
                    url.host = *host;
                    if (override) { return true; }
                    buffer.clear();
                    st = state::path_start;
                }
            } else {
                buffer.push_back(c);
            }
            break;

        case state::path_start:
            if (special) {
                st = state::path;
                if (c != '/' && c != '\\') { --p; }
            } else if (!override && c == '?') {
                url.query = std::string{};
                st = state::query;
            } else if (!override && c == '#') {
                url.fragment = std::string{};
                st = state::fragment;
            } else if (c != eof) {
                st = state::path;
                if (c != '/') { --p; }
            } else if (override && !url.host) {
                url.path.emplace_back();
            }
            break;

        case state::path: {
            const bool slash = c == '/' || (special && c == '\\');
            if (c == eof || slash || (!override && (c == '?' || c == '#'))) {
                const std::string segment = utf8_of(buffer);
                if (double_dot(segment)) {
                    shorten_path(url);
                    if (!slash) { url.path.emplace_back(); }
                } else if (single_dot(segment) && !slash) {
                    url.path.emplace_back();
                } else if (!single_dot(segment)) {
                    std::string kept = segment;
                    if (url.scheme == "file" && url.path.empty() && windows_drive_letter(buffer)) {
                        kept[1] = ':';
                    }
                    url.path.push_back(std::move(kept));
                }
                buffer.clear();
                if (c == '?') {
                    url.query = std::string{};
                    st = state::query;
                }
                if (c == '#') {
                    url.fragment = std::string{};
                    st = state::fragment;
                }
            } else {
                // Encoded into the buffer as code points so the dot-segment
                // tests above see `%2e` spelled as the input spelled it.
                std::string encoded;
                percent_encode(encoded, c, encode_set::path);
                for (const char each : encoded) {
                    buffer.push_back(static_cast<unsigned char>(each));
                }
            }
            break;
        }

        case state::opaque_path:
            if (c == '?') {
                url.query = std::string{};
                st = state::query;
            } else if (c == '#') {
                url.fragment = std::string{};
                st = state::fragment;
            } else if (c == ' ') {
                url.path[0] +=
                    remaining_starts_with(U"?") || remaining_starts_with(U"#") ? "%20" : " ";
            } else if (c != eof) {
                percent_encode(url.path[0], c, encode_set::c0);
            }
            break;

        case state::query:
            if ((!override && c == '#') || c == eof) {
                *url.query +=
                    percent_encode(buffer, special ? encode_set::special_query : encode_set::query);
                buffer.clear();
                if (c == '#') {
                    url.fragment = std::string{};
                    st = state::fragment;
                }
            } else {
                buffer.push_back(c);
            }
            break;

        case state::fragment:
            if (c != eof) { percent_encode(*url.fragment, c, encode_set::fragment); }
            break;
        }
        if (p >= n) { break; }
        ++p;
    }
    return true;
}

} // namespace ctbrowser::shell::url_detail
