#pragma once
#include <algorithm>
#include <cstring>
#include <ctbrowser/core/algorithms.hpp>
#include <ctbrowser/shell/net/url.hpp>
#include <string>
#include <vector>

namespace ctbrowser::shell::url_detail {
constexpr char32_t eof = 0xFFFFFFFFu;

[[nodiscard]] constexpr bool is_alpha(char32_t c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
[[nodiscard]] constexpr bool is_digit(char32_t c) noexcept {
    return c >= '0' && c <= '9';
}
[[nodiscard]] constexpr bool is_hex(char32_t c) noexcept {
    return c < 0x80 && hex_value(static_cast<char>(c)) >= 0;
}
[[nodiscard]] constexpr char32_t lower(char32_t c) noexcept {
    return c >= 'A' && c <= 'Z' ? c + 0x20 : c;
}
[[nodiscard]] constexpr bool is_surrogate(char32_t c) noexcept {
    return c >= 0xD800 && c <= 0xDFFF;
}

enum class encode_set {
    c0,
    fragment,
    query,
    special_query,
    path,
    userinfo,
    component,
    form
};
enum class state {
    scheme_start,
    scheme,
    no_scheme,
    special_relative_or_authority,
    path_or_authority,
    relative,
    relative_slash,
    special_authority_slashes,
    special_authority_ignore_slashes,
    authority,
    host,
    hostname,
    port,
    file,
    file_slash,
    file_host,
    path_start,
    path,
    opaque_path,
    query,
    fragment,
};

std::u32string code_points(std::string_view utf8);
void append_scalar(std::string & out, char32_t c);
std::string utf8_of(std::u32string_view text);
std::u32string decode_replacing(std::string_view bytes);
void percent_encode(std::string & out, char32_t c, encode_set set);
std::string percent_encode(std::u32string_view text, encode_set set);
std::optional<std::uint16_t> default_port(std::string_view scheme) noexcept;
bool special_scheme(std::string_view scheme) noexcept;
std::optional<std::string> parse_host(std::u32string_view input, bool is_opaque);
bool basic_parse(std::string_view raw, const url_record * base, url_record & url,
                 std::optional<state> override);
} // namespace ctbrowser::shell::url_detail
