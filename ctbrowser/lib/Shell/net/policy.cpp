// shell/net/policy.hpp - what a page may reach. Pure: no sockets, no libcurl;
// net.cpp calls these on the URL and on every address it connects to.

#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/net/policy.hpp>

#include <array>
#include <cstdint>
#include <optional>

namespace ctbrowser::shell {

namespace {

// Four decimal octets, the only spelling the URL parser and inet_ntop emit.
std::optional<std::array<std::uint8_t, 4>> parse_ipv4(std::string_view text) {
    std::array<std::uint8_t, 4> out{};
    for (std::size_t part = 0; part < 4; ++part) {
        if (part != 0) {
            if (text.empty() || text.front() != '.') { return std::nullopt; }
            text.remove_prefix(1);
        }
        unsigned value = 0;
        std::size_t digits = 0;
        while (!text.empty() && text.front() >= '0' && text.front() <= '9' && digits < 3) {
            value = value * 10 + static_cast<unsigned>(text.front() - '0');
            text.remove_prefix(1);
            ++digits;
        }
        if (digits == 0 || value > 255) { return std::nullopt; }
        out[part] = static_cast<std::uint8_t>(value);
    }
    if (!text.empty()) { return std::nullopt; }
    return out;
}

// Eight groups, `::` for a run of zeros, and a trailing dotted IPv4 allowed.
std::optional<std::array<std::uint16_t, 8>> parse_ipv6(std::string_view text) {
    std::array<std::uint16_t, 8> out{};
    std::size_t count = 0;
    std::optional<std::size_t> gap;
    if (text.starts_with("::")) {
        gap = 0;
        text.remove_prefix(2);
    }
    while (!text.empty()) {
        if (count == 8) { return std::nullopt; }
        if (text.find('.') != std::string_view::npos && text.find(':') == std::string_view::npos) {
            const auto v4 = parse_ipv4(text);
            if (!v4 || count > 6) { return std::nullopt; }
            out[count++] = static_cast<std::uint16_t>(((*v4)[0] << 8U) | (*v4)[1]);
            out[count++] = static_cast<std::uint16_t>(((*v4)[2] << 8U) | (*v4)[3]);
            text = {};
            break;
        }
        unsigned value = 0;
        std::size_t digits = 0;
        while (!text.empty() && hex_value(text.front()) >= 0 && digits < 4) {
            value = value * 16 + static_cast<unsigned>(hex_value(text.front()));
            text.remove_prefix(1);
            ++digits;
        }
        if (digits == 0) { return std::nullopt; }
        out[count++] = static_cast<std::uint16_t>(value);
        if (text.empty()) { break; }
        if (text.front() != ':') { return std::nullopt; }
        text.remove_prefix(1);
        if (!text.empty() && text.front() == ':') {
            if (gap) { return std::nullopt; }
            gap = count;
            text.remove_prefix(1);
        } else if (text.empty()) {
            return std::nullopt;
        }
    }
    if (gap) {
        if (count == 8) { return std::nullopt; }
        const std::size_t tail = count - *gap;
        for (std::size_t i = 0; i < tail; ++i) { out[7 - i] = out[count - 1 - i]; }
        for (std::size_t i = *gap; i < 8 - tail; ++i) { out[i] = 0; }
    } else if (count != 8) {
        return std::nullopt;
    }
    return out;
}

bool private_ipv4(const std::array<std::uint8_t, 4> & a) {
    return a[0] == 0 || a[0] == 10 || a[0] == 127 || (a[0] == 169 && a[1] == 254) ||
           (a[0] == 172 && (a[1] & 0xF0U) == 16) || (a[0] == 192 && a[1] == 168) ||
           (a[0] == 100 && (a[1] & 0xC0U) == 64) ||
           a[0] >= 224; // multicast, reserved and broadcast: never a public peer
}

bool private_ipv6(const std::array<std::uint16_t, 8> & g) {
    // ::/96 (unspecified, ::1, IPv4-compatible) and ::ffff:0:0/96 (IPv4-mapped)
    // carry an IPv4 address in the low 32 bits; judge that.
    if (g[0] == 0 && g[1] == 0 && g[2] == 0 && g[3] == 0 && g[4] == 0 &&
        (g[5] == 0 || g[5] == 0xFFFF)) {
        if (g[5] == 0 && g[6] == 0 && g[7] <= 1) { return true; }
        return private_ipv4(
            {static_cast<std::uint8_t>(g[6] >> 8U), static_cast<std::uint8_t>(g[6] & 0xFFU),
             static_cast<std::uint8_t>(g[7] >> 8U), static_cast<std::uint8_t>(g[7] & 0xFFU)});
    }
    return (g[0] & 0xFE00U) == 0xFC00U     // fc00::/7 unique local
           || (g[0] & 0xFFC0U) == 0xFE80U  // fe80::/10 link-local
           || (g[0] & 0xFFC0U) == 0xFEC0U  // fec0::/10 site-local (deprecated)
           || (g[0] & 0xFF00U) == 0xFF00U; // multicast
}

} // namespace

bool is_private_address(std::string_view ip) {
    if (ip.size() > 2 && ip.front() == '[' && ip.back() == ']') {
        ip = ip.substr(1, ip.size() - 2);
    }
    // A zone index (`fe80::1%eth0`) names an interface, so it is link-local.
    ip = ip.substr(0, ip.find('%'));
    if (const auto v4 = parse_ipv4(ip)) { return private_ipv4(*v4); }
    if (const auto v6 = parse_ipv6(ip)) { return private_ipv6(*v6); }
    return false;
}

std::optional<std::string> check_request(std::string_view scheme, std::string_view host,
                                         const fetch_policy & policy) {
    bool scheme_ok = false;
    for (const std::string & allowed : policy.schemes) {
        if (ascii_iequals(allowed, scheme)) { scheme_ok = true; }
    }
    if (!scheme_ok) { return "the fetch policy does not allow " + std::string{scheme} + ":"; }
    if (!policy.hosts.empty()) {
        bool host_ok = false;
        for (const std::string & allowed : policy.hosts) {
            if (std::string_view{allowed}.starts_with("*.")) {
                const std::string_view suffix = std::string_view{allowed}.substr(1);
                host_ok =
                    host_ok || (host.size() > suffix.size() &&
                                ascii_iequals(host.substr(host.size() - suffix.size()), suffix));
            } else {
                host_ok = host_ok || ascii_iequals(allowed, host);
            }
        }
        if (!host_ok) { return "the fetch policy does not allow host " + std::string{host}; }
    }
    if (!policy.allow_private_addresses) {
        // `localhost` is loopback by definition (RFC 6761), whatever DNS says.
        const bool loopback_name =
            ascii_iequals(host, "localhost") ||
            (host.size() > 10 && ascii_iequals(host.substr(host.size() - 10), ".localhost"));
        if (loopback_name || is_private_address(host)) {
            return "the fetch policy does not allow private address " + std::string{host};
        }
    }
    return std::nullopt;
}

} // namespace ctbrowser::shell
