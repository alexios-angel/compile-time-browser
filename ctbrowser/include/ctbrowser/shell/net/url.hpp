#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// URLs, parsed in ONE place, for the HTTP client and for `location.*` alike -
// two parsers for one job drift apart, and nothing can compare them.
//
// NOTHING THIRD-PARTY IS INCLUDED ABOVE. `boost/url/` is 1.2 MB of headers;
// Boost.URL lives in url.cpp and reaches nobody.
//
// WHY BOOST AT ALL. RFC 3986 is not hard to get roughly right and is very hard
// to get exactly right - dot segments, IPv6 literals, percent-encoding, scheme
// and host case, and relative resolution. Boost.URL is compiled-only (its
// `src.hpp` is discontinued upstream), which is why this engine links a Boost
// library at all.
//
// LENIENT, LIKE THE REST OF THIS TREE. Boost.URL is a strict parser and refuses
// a raw space or a UTF-8 byte in a path. A browser accepts both, so the
// implementation percent-encodes what RFC 3986 disallows before parsing rather
// than handing pages a stricter engine than they were written for.

namespace ctbrowser::shell {

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

// What the DOM needs: the pieces `location.*` and `new URL()` report, spelled
// the way a page reads them.
struct location_url {
    std::string protocol; // "http:" - WITH the colon, as the DOM reports it
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
// Returns the reference unchanged if the base is unparseable, which is the
// lenient answer rather than an empty string that loses information.
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
// URL's text form, a `javascript:` href's body and a fragment being matched
// against an id, so they cannot disagree about a malformed escape.
[[nodiscard]] std::string percent_decode(std::string_view text);

} // namespace ctbrowser::shell
