#include <ctbrowser/shell/net/url.hpp>

// The WHATWG URL Standard (https://url.spec.whatwg.org/), section numbers as
// of 2025 in the comments. Boost.URL, which used to be here, is gone: it parses
// RFC 3986, which is not the specification a browser implements - see url.hpp.

#include <ctbrowser/core/algorithms.hpp>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "url_internal.hpp"

namespace ctbrowser::shell {
using namespace url_detail;
std::string percent_decode(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const int high = i + 2 < text.size() && text[i] == '%' ? hex_value(text[i + 1]) : -1;
        const int low = high < 0 ? -1 : hex_value(text[i + 2]);
        if (low < 0) {
            out += text[i];
            continue;
        }
        out += static_cast<char>(high * 16 + low);
        i += 2;
    }
    return out;
}

// --- url_record ------------------------------------------------------------------------

bool url_record::is_special() const noexcept {
    return special_scheme(scheme);
}

bool url_record::cannot_have_credentials_or_port() const noexcept {
    return !host || host->empty() || scheme == "file";
}

std::string url_record::host_and_port() const {
    if (!host) { return {}; }
    return port ? *host + ":" + std::to_string(*port) : *host;
}

std::string url_record::port_text() const {
    return port ? std::to_string(*port) : std::string{};
}

std::string url_record::pathname() const {
    if (opaque_path) { return path.empty() ? std::string{} : path[0]; }
    std::string out;
    for (const std::string & segment : path) {
        out += '/';
        out += segment;
    }
    return out;
}

std::string url_record::search() const {
    return query && !query->empty() ? "?" + *query : std::string{};
}

std::string url_record::hash() const {
    return fragment && !fragment->empty() ? "#" + *fragment : std::string{};
}

std::string url_record::serialize(bool exclude_fragment) const {
    std::string out = scheme + ":";
    if (host) {
        out += "//";
        if (has_credentials()) {
            out += username;
            if (!password.empty()) { out += ":" + password; }
            out += '@';
        }
        out += host_and_port();
    } else if (!opaque_path && path.size() > 1 && path[0].empty()) {
        // `web+demo:/.//not-a-host/`: without this a path starting `//` would
        // read back as an authority.
        out += "/.";
    }
    out += pathname();
    if (query) { out += "?" + *query; }
    if (!exclude_fragment && fragment) { out += "#" + *fragment; }
    return out;
}

std::string url_record::origin() const {
    if (scheme == "blob") {
        // §4.7: the origin of the URL the path names, for http(s) only.
        const std::optional<url_record> inner = parse_url(pathname());
        if (inner && (inner->scheme == "http" || inner->scheme == "https")) {
            return inner->origin();
        }
        return "null";
    }
    if (scheme == "ftp" || scheme == "http" || scheme == "https" || scheme == "ws" ||
        scheme == "wss") {
        return scheme + "://" + host_and_port();
    }
    return "null";
}

// --- parsing, setting, form-urlencoded -------------------------------------------------

std::optional<url_record> parse_url(std::string_view input, const url_record * base) {
    url_record url;
    if (!basic_parse(input, base, url, std::nullopt)) { return std::nullopt; }
    return url;
}

std::optional<url_record> parse_url(std::string_view input, std::string_view base) {
    const std::optional<url_record> parsed_base = parse_url(base);
    if (!parsed_base) { return std::nullopt; }
    return parse_url(input, &*parsed_base);
}

bool set_url_part(url_record & url, url_part part, std::string_view value) {
    const auto with_override = [&](std::string_view text, state override) {
        // IN PLACE, as the standard has it: `host = "example.com:65536"` sets
        // the host and THEN fails on the port, and the host stays set.
        (void)basic_parse(text, nullptr, url, override);
    };
    switch (part) {
    case url_part::href: {
        std::optional<url_record> parsed = parse_url(value);
        if (!parsed) { return false; }
        url = std::move(*parsed);
        return true;
    }
    case url_part::protocol:
        with_override(std::string{value} + ":", state::scheme_start);
        return true;
    case url_part::username:
        if (url.cannot_have_credentials_or_port()) { return true; }
        url.username = percent_encode(code_points(value), encode_set::userinfo);
        return true;
    case url_part::password:
        if (url.cannot_have_credentials_or_port()) { return true; }
        url.password = percent_encode(code_points(value), encode_set::userinfo);
        return true;
    case url_part::host:
        if (url.opaque_path) { return true; }
        with_override(value, state::host);
        return true;
    case url_part::hostname:
        if (url.opaque_path) { return true; }
        with_override(value, state::hostname);
        return true;
    case url_part::port:
        if (url.cannot_have_credentials_or_port()) { return true; }
        if (value.empty()) {
            url.port.reset();
        } else {
            with_override(value, state::port);
        }
        return true;
    case url_part::pathname:
        if (url.opaque_path) { return true; }
        url.path.clear();
        with_override(value, state::path_start);
        return true;
    case url_part::search:
        if (value.empty()) {
            url.query.reset();
            return true;
        }
        if (value.front() == '?') { value.remove_prefix(1); }
        url.query = std::string{};
        with_override(value, state::query);
        return true;
    case url_part::hash:
        if (value.empty()) {
            url.fragment.reset();
            return true;
        }
        if (value.front() == '#') { value.remove_prefix(1); }
        url.fragment = std::string{};
        with_override(value, state::fragment);
        return true;
    }
    return true;
}

std::string to_usv_string(std::string_view text) {
    return utf8_of(code_points(text));
}

form_pairs parse_form_urlencoded(std::string_view query) {
    form_pairs out;
    std::size_t start = 0;
    while (start <= query.size()) {
        std::size_t end = query.find('&', start);
        if (end == std::string_view::npos) { end = query.size(); }
        const std::string_view sequence = query.substr(start, end - start);
        start = end + 1;
        if (sequence.empty()) { continue; }
        const std::size_t equals = sequence.find('=');
        std::string name{sequence.substr(0, equals)};
        std::string value{equals == std::string_view::npos ? std::string_view{}
                                                           : sequence.substr(equals + 1)};
        std::replace(name.begin(), name.end(), '+', ' ');
        std::replace(value.begin(), value.end(), '+', ' ');
        out.emplace_back(utf8_of(decode_replacing(percent_decode(name))),
                         utf8_of(decode_replacing(percent_decode(value))));
    }
    return out;
}

std::string serialize_form_urlencoded(const form_pairs & pairs) {
    // §5.2, the byte serializer: space is `+`, the form set is escaped.
    const auto append = [](std::string & out, std::string_view text) {
        for (const char32_t c : code_points(text)) {
            if (c == ' ') {
                out.push_back('+');
            } else {
                percent_encode(out, c, encode_set::form);
            }
        }
    };
    std::string out;
    for (const auto & [name, value] : pairs) {
        if (!out.empty()) { out.push_back('&'); }
        append(out, name);
        out.push_back('=');
        append(out, value);
    }
    return out;
}

// --- the two consumers' views ---------------------------------------------------------------

fetch_url parse_absolute(std::string_view text) {
    fetch_url out;
    const std::optional<url_record> url = parse_url(text);
    // ONLY http AND https ARE FETCHABLE. A `file:` or `blob:` URL arriving on
    // the socket path is a bug in the caller, and answering with a
    // plausible-looking struct would let it stay one.
    if (!url || (url->scheme != "http" && url->scheme != "https") || !url->host ||
        url->host->empty()) {
        return out;
    }
    out.scheme = url->scheme;
    // WITHOUT the brackets: this is the address a resolver and a socket want.
    // location_parts keeps them, because that is what the DOM reports - see
    // the note in url.hpp about why the two types differ here.
    out.host = *url->host;
    if (out.host.size() > 2 && out.host.front() == '[') {
        out.host = out.host.substr(1, out.host.size() - 2);
    }
    out.port = url->port ? std::to_string(*url->port) : (out.scheme == "https" ? "443" : "80");
    out.target = url->pathname();
    if (url->query) { out.target += "?" + *url->query; }
    // THE FRAGMENT IS NEVER APPENDED. It is client-side state; sending it leaks
    // it to the server and is a specification violation besides.
    //
    // The Host header's form: bracketed for IPv6, and carrying the port only
    // when it is not the default - which is exactly the record's `host:port`,
    // since the parser already dropped a default port.
    out.authority = url->host_and_port();
    out.valid = true;
    return out;
}

location_url location_parts(std::string_view href) {
    location_url out;
    // NEVER FAILS. `location.*` has no channel for "unparseable", and a page
    // reading location.pathname mid-navigation wants an empty string rather than
    // an exception. Everything below is simply left empty.
    const std::optional<url_record> url = parse_url(href);
    if (!url) { return out; }
    out.href = url->serialize();
    out.protocol = url->protocol();
    out.username = url->username;
    out.password = url->password;
    out.host = url->host_and_port();
    out.hostname = url->hostname();
    out.port = url->port_text();
    out.pathname = url->pathname();
    out.search = url->search();
    out.hash = url->hash();
    out.origin = url->origin();
    return out;
}

std::string resolve(std::string_view base, std::string_view reference) {
    const std::optional<url_record> resolved = parse_url(reference, base);
    // LENIENT ON THE WAY OUT. An unparseable base or reference gives back the
    // reference as it arrived, which is the answer that loses the least - an
    // empty string would discard information the caller still has a use for.
    return resolved ? resolved->serialize() : std::string{reference};
}

// NOT THROUGH THE URL PARSER, and that is deliberate rather than an oversight:
// everything interesting lives in the opaque path, which the parser hands back
// as one string - and it would percent-encode a raw byte in the payload where
// RFC 2397 wants it decoded as written.
bool is_data_url(std::string_view url) {
    constexpr std::string_view scheme = "data:";
    if (url.size() < scheme.size()) { return false; }
    for (std::size_t i = 0; i < scheme.size(); ++i) {
        if (ascii_lower(url[i]) != scheme[i]) { return false; }
    }
    return true;
}

bool parse_data_url(std::string_view url, data_url & out) {
    if (!is_data_url(url)) { return false; }
    url.remove_prefix(std::string_view{"data:"}.size());
    // RFC 2397's comma separates the metadata from the payload, and a URL
    // without one is not a data URL - there is no payload to be lenient about.
    const std::size_t comma = url.find(',');
    if (comma == std::string_view::npos) { return false; }
    std::string_view meta = url.substr(0, comma);
    const std::string_view payload = url.substr(comma + 1);

    // `;base64` is the LAST parameter or it is not the encoding marker: a
    // media type parameter that merely contains the word is not one.
    constexpr std::string_view marker = ";base64";
    bool is_base64 = false;
    if (meta.size() >= marker.size()) {
        const std::string_view tail = meta.substr(meta.size() - marker.size());
        if (ascii_iequals(tail, marker)) {
            is_base64 = true;
            meta.remove_suffix(marker.size());
        }
    }

    // The media type is everything up to the first parameter. Its parameters -
    // `;charset=utf-8` - are dropped: nothing in this engine dispatches on
    // them, and a type that lies about its bytes is decided by the decoder
    // sniffing the bytes anyway.
    const std::size_t semicolon = meta.find(';');
    const std::string_view type = meta.substr(0, semicolon);
    // Empty means the RFC's default, which is already in the struct.
    if (!type.empty()) { out.mime = ascii_lower_copy(type); }

    const std::string decoded = is_base64 ? base64_decode(payload) : percent_decode(payload);
    out.bytes.resize(decoded.size());
    // `data:,` IS A VALID DATA URL and decodes to no bytes at all - and memcpy
    // is declared never-null in both arguments, so the empty case is UB rather
    // than a harmless no-op. UBSan caught this; a release build would not have.
    if (!decoded.empty()) { std::memcpy(out.bytes.data(), decoded.data(), decoded.size()); }
    return true;
}

} // namespace ctbrowser::shell
