#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// URLs, parsed in ONE place.
//
// There used to be two parsers: `detail::parse_url` in net.cpp for the HTTP
// client and `split_url` in bindings.cpp for `location.*`. Written separately,
// they drifted, and one of them was wrong - `split_url` reached for the last
// colon in the authority with no bracket guard, so `http://[::1]/` gave hostname
// `[:` and port `1]`. Nothing compared them, because nothing could.
//
// NOTHING THIRD-PARTY IS INCLUDED ABOVE, and that is the same rule net.hpp
// states for Asio. `boost/url/` is 1.2 MB of headers; every consumer of the
// engine would parse it to get three structs. Boost.URL lives in url.cpp and
// reaches nobody.
//
// WHY BOOST AT ALL. RFC 3986 is not hard to get roughly right and is very hard
// to get exactly right - dot segments, IPv6 literals, percent-encoding, scheme
// and host case, and relative resolution - and the two parsers between them had
// none of it. Boost.URL is compiled-only (its `src.hpp` is discontinued
// upstream), which is why this engine now links a Boost library at all.
//
// LENIENT, LIKE THE REST OF THIS TREE. Boost.URL is a strict parser and refuses
// a raw space or a UTF-8 byte in a path. A browser accepts both, and so did the
// two parsers replaced here, so the implementation percent-encodes what RFC 3986
// disallows before parsing rather than handing pages a stricter engine than they
// were written for. ctcss and ctjs are documented lenient parsers; this matches.

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
    // reads the address's own colons as the port separator, which is the same
    // mistake the old bindings.cpp parser made in the other direction. The port
    // is included only when it is not the scheme's default, as the RFC asks.
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
// THE CAPABILITY NEITHER OLD PARSER HAD, which is why `new URL(href, base)` did
// not exist and why relative asset paths were resolved by string surgery
// elsewhere. Returns the reference unchanged if the base is unparseable, which
// is the lenient answer rather than an empty string that loses information.
[[nodiscard]] std::string resolve(std::string_view base, std::string_view reference);

// A `data:` URL carries its own bytes, so it is a resource that needs no
// transport at all.
//
// THE ENGINE COULD WRITE THESE LONG BEFORE IT COULD READ THEM - `toDataURL` and
// FileReader both produce one - and nothing noticed, because a hand-written page
// that makes a data URL hands it straight back to the same page. A LIBRARY ships
// its own images inside itself: Phaser's texture manager loads three base64 PNGs
// during boot and waits for all three, so every one of them failing left it
// waiting forever with no error anyone could see.
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

} // namespace ctbrowser::shell
