//===- url.hpp - WHATWG URL parser ------------------------------*- C++ -*-===//
//
// URLs, parsed in one place by the WHATWG URL Standard, for `new URL()`,
// `location.*`, `a.href`, every relative `src` and the HTTP client alike. Two
// parsers for one job drift apart (docs/history/ada-url.md).
//
// This follows the URL Standard, not RFC 3986: browsers differ from RFC 3986
// on inputs such as `http:\\host\a`, IDN hosts and `/%2e%2e/x`. Implemented in
// full: the state machine of §4.4, the host parser of §3.5 (IPv4 numeric forms
// and IPv6), the percent-encode sets of §1.3, the serialiser of §4.5, the
// setter steps of §6.1 and application/x-www-form-urlencoded of §5.
//
// UTS #46 is implemented from Unicode's own tables: `domain to ASCII` runs the
// whole mapping table, the validity criteria and both ContextJ rules, and RFC
// 3492 punycode in both directions, so an `xn--` label is decoded and checked
// rather than taken on trust. tools/gen/idna_table.py generates the tables.
//
// UTS #46 processing step 2 (normalise to NFC) is implemented too, from UAX
// #15's data (tools/gen/nfc_table.py). It is the only Unicode normalisation in
// the engine, and it is here because a domain is the one string the platform
// normalises before comparing.
//
// Not implemented: CheckBidi (UTS #46 §5.4). It accounts for 3 of the 2,671
// cases in WPT url/IdnaTestV2.any.js, too few to carry a table of every code
// point's Bidi_Class. See the note above domain_to_ascii in url.cpp.
//
// No third-party header and nothing of the VM is included: this is plain C++
// over strings, so a unit test can drive it with WPT's urltestdata.json and
// the bindings stay thin.
//
//===----------------------------------------------------------------------===//
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ctbrowser::shell {

// --- The URL record, §4.1 ---------------------------------------------------

/// A parsed URL (URL Standard §4.1).
struct url_record {
    std::string scheme;
    std::string username;
    std::string password;
    /// The serialised host: a domain, `1.2.3.4`, `[::1]` with its brackets, an
    /// opaque host, or "" for the empty host. Null is "no host" (`mailto:x`,
    /// `data:,x`), which serialises with no `//` at all.
    std::optional<std::string> host;
    std::optional<std::uint16_t> port; // Null when it is the scheme's default.
    /// Segments, serialised as `/a/b`; or one opaque string when `opaque_path`
    /// (`data:text/plain,x`, `mailto:a@b`), serialised as itself.
    std::vector<std::string> path;
    bool opaque_path = false;
    std::optional<std::string> query;    // Without the `?`.
    std::optional<std::string> fragment; // Without the `#`.

    /// Whether the scheme is one of the special schemes (http, https, ws, wss,
    /// ftp, file).
    [[nodiscard]] bool is_special() const noexcept;
    [[nodiscard]] bool has_credentials() const noexcept {
        return !username.empty() || !password.empty();
    }
    /// "Cannot have a username/password/port": host null or empty, or `file:`.
    [[nodiscard]] bool cannot_have_credentials_or_port() const noexcept;

    /// The URL serialiser (§4.5): `href` with the fragment, or without it when
    /// \p exclude_fragment is set.
    [[nodiscard]] std::string serialize(bool exclude_fragment = false) const;
    // The pieces §6.1's getters report, spelled the way a page reads them.
    [[nodiscard]] std::string protocol() const { return scheme + ":"; }
    [[nodiscard]] std::string host_and_port() const; // `example.com:8080`, "" for no host.
    [[nodiscard]] std::string hostname() const { return host.value_or(std::string{}); }
    [[nodiscard]] std::string port_text() const;
    [[nodiscard]] std::string pathname() const;
    [[nodiscard]] std::string search() const; // `?a=1`; "" for null and for empty.
    [[nodiscard]] std::string hash() const;   // `#x`; "" for null and for empty.
    /// The origin (§4.7), serialised: `https://example.com:8443`, or "null"
    /// for an opaque origin (`file:`, `data:`, any non-special scheme).
    [[nodiscard]] std::string origin() const;
};

/// The basic URL parser (§4.4), with an optional base. Nullopt is the
/// standard's "failure", which `new URL()` reports as a TypeError and every
/// lenient caller below turns into "leave it as written".
[[nodiscard]] std::optional<url_record> parse_url(std::string_view input,
                                                  const url_record * base = nullptr);
/// As above, with the base given as a string; failure if the base itself does
/// not parse.
[[nodiscard]] std::optional<url_record> parse_url(std::string_view input, std::string_view base);

/// The IDL attributes of URL and HTMLHyperlinkElementUtils that set_url_part
/// can set.
enum class url_part {
    href,
    protocol,
    username,
    password,
    host,
    hostname,
    port,
    pathname,
    search,
    hash
};
/// Runs §6.1's setter steps for \p part. `href` is the only one that can fail
/// (a URL that does not parse); it returns false and leaves the record
/// untouched. `search` and `hash` take the value with or without their
/// leading `?` or `#`.
bool set_url_part(url_record & url, url_part part, std::string_view value);

/// Converts a DOMString to a USVString: WTF-8's lone surrogates become U+FFFD,
/// as every URL-facing IDL argument is converted before the parser sees it.
[[nodiscard]] std::string to_usv_string(std::string_view text);

// §5, application/x-www-form-urlencoded: the list URLSearchParams is a view
// of. `parse(serialize(list)) == list` for any list, which lets the bindings
// keep the string and not the list.

/// Name-value pairs in order, as URLSearchParams holds them.
using form_pairs = std::vector<std::pair<std::string, std::string>>;
/// Parses \p query, given without its `?`.
[[nodiscard]] form_pairs parse_form_urlencoded(std::string_view query);
/// Serialises \p pairs, without a leading `?`.
[[nodiscard]] std::string serialize_form_urlencoded(const form_pairs & pairs);

// --- The two consumers' views, derived from the record ---------------------

/// What the HTTP client needs: enough to open a socket and write a request
/// line.
///
/// `host` is the connect address: an IPv6 literal arrives here without its
/// brackets, because that is what a resolver wants. `location_url::hostname`
/// keeps them, because that is what the DOM reports. That difference is why
/// they are separate types.
struct fetch_url {
    std::string scheme;
    std::string host;
    std::string port;         // Never empty: the scheme's default when unstated.
    std::string target = "/"; // Path and query, never the fragment.
    /// What goes in the `Host:` header, which is not `host` above. An IPv6
    /// literal has to be bracketed there (`Host: [::1]:8080`) or the server
    /// reads the address's own colons as the port separator. The port is
    /// included only when it is not the scheme's default, as the RFC asks.
    std::string authority;
    bool valid = false;
};

/// What the DOM needs: the pieces `location.*` and an `<a>` report, spelled
/// the way a page reads them. All empty when the input does not parse.
struct location_url {
    std::string href;
    std::string protocol; // "http:", with the colon, as the DOM reports it.
    std::string username;
    std::string password;
    std::string host;     // "example.com:8080": hostname and port together.
    std::string hostname; // An IPv6 literal keeps its brackets here.
    std::string port;
    std::string pathname;
    std::string search; // "?a=1"; empty when there is none, never a bare "?".
    std::string hash;   // "#x".
    std::string origin; // "null" where there is no tuple origin, as for a file: URL.
};

/// Parses a URL a request can be made to. Anything that is not http or https
/// comes back with `valid == false` rather than as a plausible-looking struct:
/// a `file:` URL reaching the socket path is a bug, not a request.
[[nodiscard]] fetch_url parse_absolute(std::string_view url);

/// Parses anything at all, for the DOM. Never fails: `location.*` has no way
/// to report that it could not parse, so a page reading `location.pathname`
/// during an odd navigation gets empty strings rather than an exception.
[[nodiscard]] location_url location_parts(std::string_view href);

/// Resolves \p reference against \p base: `../c`, `/abs`, `//other/x`,
/// `?q=2`, or an absolute URL that ignores the base entirely.
///
/// Returns the reference unchanged if the pair does not parse, which is what
/// HTML's URL reflection asks for ("if parsing fails, return the content
/// attribute").
[[nodiscard]] std::string resolve(std::string_view base, std::string_view reference);

/// A decoded `data:` URL: a resource that carries its own bytes and needs no
/// transport. Parsed here rather than at each consumer because every one of
/// them (an <img> src, `fetch`, a CSS `url()`, a <script>) resolves through
/// asset_registry, the single place this needs to be understood.
struct data_url {
    std::string mime = "text/plain"; // The RFC 2397 default when none is stated.
    std::vector<std::byte> bytes;
};

/// Whether \p url has the `data:` scheme, compared ASCII case-insensitively.
/// Separate from the parse because it is cheap enough to ask before every
/// load.
[[nodiscard]] bool is_data_url(std::string_view url);

/// Decodes both forms RFC 2397 allows: `;base64` and percent-encoded text.
/// Returns false only for a URL that is not a data: URL at all; a malformed
/// payload decodes as far as it can, as browsers do with a truncated base64
/// tail.
[[nodiscard]] bool parse_data_url(std::string_view url, data_url & out);

/// Percent-decodes leniently: `%XX` becomes the byte, and anything else (a
/// stray `%`, a truncated escape) passes through as itself, exactly as the
/// standard's percent-decoder (§1.3). One decoder for a data: URL's text form,
/// a `javascript:` href's body, a fragment matched against an id, and the host
/// parser, so they cannot disagree about a malformed escape.
[[nodiscard]] std::string percent_decode(std::string_view text);

} // namespace ctbrowser::shell
