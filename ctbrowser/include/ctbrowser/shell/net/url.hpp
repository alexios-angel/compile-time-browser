#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// URLs, parsed in ONE place, by the WHATWG URL Standard - the specification a
// browser implements - for `new URL()`, `location.*`, `a.href`, every relative
// `src` and the HTTP client alike. Two parsers for one job drift apart, and
// nothing can compare them; this file has been that bug twice (see the history
// in unittests/unit/url_basics.cpp and docs/plans/ada-url.md).
//
// NOT RFC 3986. Until 2026-09 this was Boost.URL with the input percent-encoded
// first to get it past a strict parser, and eight of fifteen measured cases
// differed from a browser: `http:\\host\a` lost its host, `http://例.jp/` was
// unreachable, `/%2e%2e/x` kept its `..`, `:80` was not dropped. The URL
// Standard's parser is the one the platform is held to (url/ in WPT drives
// ~1,000 inputs through it), so it is written out here in full: the state
// machine of §4.4, the host parser of §3.5 with the IPv4 numeric forms and
// IPv6, the percent-encode sets of §1.3, the serialiser of §4.5, the setter
// steps of §6.1 and application/x-www-form-urlencoded of §5.
//
// UTS #46 IS DONE FROM UNICODE'S OWN TABLES: `domain to ASCII` runs the whole
// mapping table, the validity criteria and both ContextJ rules, and RFC 3492
// punycode in both directions, so an `xn--` label is decoded and checked rather
// than taken on trust. tools/gen/idna_table.py generates the four tables.
//
// WHAT IT DOES NOT DO: step 2's NFC normalisation, and CheckBidi. The first
// costs 134 of url/IdnaTestV2.any.js's 2,671 cases and wants the canonical
// decomposition and composition data; the second costs ONE, which is why no
// Bidi_Class table is carried. See the note above domain_to_ascii in url.cpp.
//
// NOTHING THIRD-PARTY IS INCLUDED ABOVE, and nothing of the VM either: this is
// plain C++ over strings, which is what lets a unit test drive it with the
// suite's own urltestdata.json and lets the bindings be thin.

namespace ctbrowser::shell {

// --- the URL record, §4.1 ---------------------------------------------------

struct url_record {
    std::string scheme;
    std::string username;
    std::string password;
    // The SERIALISED host: a domain, `1.2.3.4`, `[::1]` with its brackets, an
    // opaque host, or "" for the empty host. Null is "no host" - `mailto:x`,
    // `data:,x` - which serialises with no `//` at all.
    std::optional<std::string> host;
    std::optional<std::uint16_t> port; // null when it is the scheme's default
    // Segments, serialised as `/a/b`; or ONE opaque string when `opaque_path`
    // (`data:text/plain,x`, `mailto:a@b`), serialised as itself.
    std::vector<std::string> path;
    bool opaque_path = false;
    std::optional<std::string> query;    // without the `?`
    std::optional<std::string> fragment; // without the `#`

    [[nodiscard]] bool is_special() const noexcept;
    [[nodiscard]] bool has_credentials() const noexcept {
        return !username.empty() || !password.empty();
    }
    // "cannot have a username/password/port": host null or empty, or `file:`.
    [[nodiscard]] bool cannot_have_credentials_or_port() const noexcept;

    // §4.5. `href` with, and `toJSON`'s without, the fragment.
    [[nodiscard]] std::string serialize(bool exclude_fragment = false) const;
    // The pieces §6.1's getters report, spelled the way a page reads them.
    [[nodiscard]] std::string protocol() const { return scheme + ":"; }
    [[nodiscard]] std::string host_and_port() const; // `example.com:8080`, "" for no host
    [[nodiscard]] std::string hostname() const { return host.value_or(std::string{}); }
    [[nodiscard]] std::string port_text() const;
    [[nodiscard]] std::string pathname() const;
    [[nodiscard]] std::string search() const; // `?a=1`, "" for null AND for empty
    [[nodiscard]] std::string hash() const;   // `#x`, "" for null and for empty
    // §4.7, serialised: `https://example.com:8443`, or "null" for an opaque
    // origin (`file:`, `data:`, any non-special scheme).
    [[nodiscard]] std::string origin() const;
};

// The basic URL parser, §4.4, with an optional base. Nullopt is the standard's
// "failure" - which `new URL()` reports as a TypeError and every lenient
// caller below turns into "leave it as written".
[[nodiscard]] std::optional<url_record> parse_url(std::string_view input,
                                                  const url_record * base = nullptr);
[[nodiscard]] std::optional<url_record> parse_url(std::string_view input, std::string_view base);

// §6.1 "setter steps", one per IDL attribute of URL and of
// HTMLHyperlinkElementUtils. `href` is the only one that can fail (a URL that
// does not parse), and it leaves the record untouched when it does. `search`
// and `hash` take the value with or without their leading `?`/`#`.
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
bool set_url_part(url_record & url, url_part part, std::string_view value);

// A DOMString made a USVString: WTF-8's lone surrogates become U+FFFD, which
// is what every URL-facing IDL argument undergoes before the parser sees it.
[[nodiscard]] std::string to_usv_string(std::string_view text);

// §5, application/x-www-form-urlencoded - the list URLSearchParams is a view
// of. The parser takes the query WITHOUT its `?`; the serialiser answers
// without one. `parse(serialize(list)) == list` for any list, which is what
// lets the bindings keep the string and not the list.
using form_pairs = std::vector<std::pair<std::string, std::string>>;
[[nodiscard]] form_pairs parse_form_urlencoded(std::string_view query);
[[nodiscard]] std::string serialize_form_urlencoded(const form_pairs & pairs);

// --- the two consumers' views, derived from the record ---------------------

// What the HTTP CLIENT needs: enough to open a socket and write a request line.
//
// `host` is the CONNECT address - an IPv6 literal arrives here without its
// brackets, because that is what a resolver wants. `location_url::hostname`
// below keeps them, because that is what the DOM reports. Both are correct and
// the difference is the reason they are separate types.
struct fetch_url {
    std::string scheme;
    std::string host;
    std::string port;         // never empty: the scheme's default when unstated
    std::string target = "/"; // path and query, NEVER the fragment
    // WHAT GOES IN THE `Host:` HEADER, which is NOT `host` above. An IPv6
    // literal has to be bracketed there - `Host: [::1]:8080` - or the server
    // reads the address's own colons as the port separator. The port is
    // included only when it is not the scheme's default, as the RFC asks.
    std::string authority;
    bool valid = false;
};

// What the DOM needs: the pieces `location.*` and an `<a>` report, spelled
// the way a page reads them. All empty when the input does not parse.
struct location_url {
    std::string href;
    std::string protocol; // "http:" - WITH the colon, as the DOM reports it
    std::string username;
    std::string password;
    std::string host;     // "example.com:8080" - hostname and port together
    std::string hostname; // an IPv6 literal KEEPS its brackets here
    std::string port;
    std::string pathname;
    std::string search; // "?a=1", empty when there is none - never a bare "?"
    std::string hash;   // "#x"
    std::string origin; // "null" where there is no tuple origin, as a file: URL
};

// Parse something a request can be made to. Anything that is not http or https
// comes back `valid == false` rather than as a plausible-looking struct - a
// `file:` URL reaching the socket path is a bug, not a request.
[[nodiscard]] fetch_url parse_absolute(std::string_view url);

// Parse anything at all, for the DOM. NEVER FAILS: `location.*` has no way to
// report that it could not parse, and a page reading `location.pathname` during
// an odd navigation should get an empty string rather than a thrown exception.
[[nodiscard]] location_url location_parts(std::string_view href);

// Resolve a reference against a base - `../c`, `/abs`, `//other/x`, `?q=2`, or
// an absolute URL that ignores the base entirely.
//
// Returns the reference unchanged if the pair does not parse, which is the
// lenient answer rather than an empty string that loses information - and is
// what HTML's URL reflection asks for ("if parsing fails, return the content
// attribute").
[[nodiscard]] std::string resolve(std::string_view base, std::string_view reference);

// A `data:` URL carries its own bytes, so it is a resource that needs no
// transport at all.
//
// Parsed here rather than at each consumer because every one of them - an <img>
// src, `fetch`, a CSS `url()`, a <script> - resolves through asset_registry,
// which is the single place this needs to be understood.
struct data_url {
    std::string mime = "text/plain"; // the RFC 2397 default when none is stated
    std::vector<std::byte> bytes;
};

// Cheap enough to ask before every load, which is why it is separate from the
// parse: it is a scheme comparison and nothing else. ASCII case-insensitive,
// because `DATA:` is the same scheme.
[[nodiscard]] bool is_data_url(std::string_view url);

// Decodes both forms RFC 2397 allows: `;base64` and percent-encoded text. Fails
// only on a URL that is not a data: URL at all - a malformed payload decodes as
// far as it can, which is the same leniency the rest of this file documents, and
// matches what browsers do with a truncated base64 tail.
[[nodiscard]] bool parse_data_url(std::string_view url, data_url & out);

// Percent-decode, LENIENTLY: `%XX` becomes the byte, anything else - a stray
// `%`, a truncated escape - passes through as itself. One decoder for a data:
// URL's text form, a `javascript:` href's body, a fragment being matched
// against an id and the host parser, so they cannot disagree about a malformed
// escape. It is also exactly the standard's percent-decoder (§1.3).
[[nodiscard]] std::string percent_decode(std::string_view text);

} // namespace ctbrowser::shell
