#include <ctbrowser/shell/url.hpp>

// THE ONLY TRANSLATION UNIT THAT KNOWS BOOST.URL EXISTS. url.hpp declares three
// plain structs and three functions; everything RFC 3986 is in here, which is
// what keeps 1.2 MB of headers off every consumer of the engine.
#include <boost/url.hpp>

#include <string>

namespace ctbrowser::shell {

namespace urls = boost::urls;

namespace {

// LENIENCY, AND IT HAS TO COME FIRST.
//
// Boost.URL is a strict RFC 3986 parser: `http://h/a b/c` and `http://h/<utf8>`
// are both rejected outright with "leftover". A browser accepts them, and so did
// both hand-rolled parsers this file replaces - so parsing strictly would hand
// pages a stricter engine than they were written for and call it correctness.
//
// So the bytes RFC 3986 disallows get percent-encoded first, which is what a
// browser does. `%` IS DELIBERATELY IN THE ALLOWED SET: without that, input a
// page already encoded would be encoded again and `%20` would become `%2520`.
// The cost is that a literal `%` not starting an escape survives as-is, which is
// the same bet every browser makes.
[[nodiscard]] std::string pre_encode(std::string_view raw) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(raw.size());
    for (const char each : raw) {
        const auto c = static_cast<unsigned char>(each);
        // Printable ASCII minus the characters RFC 3986 excludes from a URI.
        const bool allowed = c > 0x20 && c < 0x7F && c != '"' && c != '<' && c != '>' &&
                             c != '\\' && c != '^' && c != '`' && c != '{' && c != '|' && c != '}';
        if (allowed) {
            out.push_back(each);
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4U]);
            out.push_back(hex[c & 0x0FU]);
        }
    }
    return out;
}

// Parse, and normalize only where normalizing is safe.
//
// `normalize()` lowercases the scheme and host, canonicalises percent-encoding
// and removes dot segments - all correct for a hierarchical URL. It is NOT
// applied to a scheme with no authority, because the "path" of `data:` or
// `blob:` is an opaque payload rather than a path, and rewriting it would
// corrupt what a page stored there.
[[nodiscard]] bool parse_into(std::string_view text, urls::url & into) {
    // THE ENCODED STRING MUST OUTLIVE THE PARSE. `parse_uri_reference` hands
    // back a url_VIEW, which does not own its characters - passing it the
    // temporary from pre_encode() directly leaves the view dangling the moment
    // the full expression ends, and the copy below then reads freed memory.
    // It surfaced as a thrown "leftover" from perfectly valid input, which is
    // exactly as confusing as it sounds. `urls::url` owns; `url_view` borrows.
    const std::string encoded = pre_encode(text);
    const auto parsed = urls::parse_uri_reference(encoded);
    if (!parsed) { return false; }
    into = *parsed;
    if (into.has_authority()) { into.normalize(); }
    return true;
}

} // namespace

fetch_url parse_absolute(std::string_view url) {
    fetch_url out;
    urls::url parsed;
    if (!parse_into(url, parsed)) { return out; }

    out.scheme = parsed.scheme();
    // WITHOUT the brackets: this is the address a resolver and a socket want.
    // location_parts keeps them, because that is what the DOM reports - see the
    // note in url.hpp about why the two types differ here.
    out.host = parsed.host_address();
    out.port = parsed.has_port() ? std::string{parsed.port()} : std::string{};
    if (out.port.empty()) { out.port = out.scheme == "https" ? "443" : "80"; }

    out.target = parsed.encoded_path();
    if (out.target.empty()) { out.target = "/"; }
    if (parsed.has_query()) {
        out.target += "?";
        out.target += parsed.encoded_query();
    }
    // The Host header's form: bracketed for IPv6, and carrying the port only
    // when it is not the default. `parsed.host()` keeps the brackets that
    // `host_address()` above strips.
    out.authority = parsed.host();
    const bool default_port =
        (out.scheme == "http" && out.port == "80") || (out.scheme == "https" && out.port == "443");
    if (!default_port) { out.authority += ":" + out.port; }

    // THE FRAGMENT IS NEVER APPENDED. It is client-side state; sending it leaks
    // it to the server and is a specification violation besides.

    // ONLY http AND https ARE FETCHABLE. A `file:` or `blob:` URL arriving on
    // the socket path is a bug in the caller, and answering with a
    // plausible-looking struct would let it stay one.
    out.valid = !out.host.empty() && (out.scheme == "http" || out.scheme == "https");
    return out;
}

location_url location_parts(std::string_view href) {
    location_url out;
    urls::url parsed;
    // NEVER FAILS. `location.*` has no channel for "unparseable", and a page
    // reading location.pathname mid-navigation wants an empty string rather than
    // an exception. Everything below is simply left empty.
    if (!parse_into(href, parsed)) { return out; }

    if (parsed.has_scheme()) { out.protocol = std::string{parsed.scheme()} + ":"; }
    if (parsed.has_authority()) {
        // WITH the brackets, so a page reassembling `hostname + ":" + port`
        // produces a valid URL again rather than a broken IPv6 literal.
        out.hostname = parsed.host();
        if (parsed.has_port()) { out.port = parsed.port(); }
        out.host = out.hostname;
        if (!out.port.empty()) { out.host += ":" + out.port; }
    }
    out.pathname = parsed.encoded_path();
    // A URL with an authority and no path still HAS one, and a page joining onto
    // an empty pathname produces "//thing" instead of "/thing".
    if (out.pathname.empty() && parsed.has_authority()) { out.pathname = "/"; }
    if (parsed.has_query()) { out.search = "?" + std::string{parsed.encoded_query()}; }
    if (parsed.has_fragment()) { out.hash = "#" + std::string{parsed.encoded_fragment()}; }

    // AN ORIGIN IS A (scheme, host, port) TUPLE, and only the schemes that have
    // one get one. `file:` and `data:` are opaque origins, which the DOM reports
    // as the STRING "null" rather than as an absent property.
    const bool tuple_origin =
        !out.hostname.empty() && (out.protocol == "http:" || out.protocol == "https:" ||
                                  out.protocol == "ws:" || out.protocol == "wss:");
    out.origin = tuple_origin ? out.protocol + "//" + out.host : "null";
    return out;
}

std::string resolve(std::string_view base, std::string_view reference) {
    // Both encoded strings held in named locals, for the lifetime reason in
    // parse_into above: what comes back borrows from them.
    const std::string encoded_base = pre_encode(base);
    const std::string encoded_ref = pre_encode(reference);
    const auto base_url = urls::parse_uri(encoded_base);
    const auto ref_url = urls::parse_uri_reference(encoded_ref);
    // LENIENT ON THE WAY OUT TOO. An unparseable base or reference gives back
    // the reference as it arrived, which is the answer that loses the least -
    // an empty string would discard information the caller still has a use for.
    if (!base_url || !ref_url) { return std::string{reference}; }
    urls::url out;
    if (!urls::resolve(*base_url, *ref_url, out)) { return std::string{reference}; }
    out.normalize();
    return out.buffer();
}

} // namespace ctbrowser::shell
