// URL parsing, in ONE place.
//
// There used to be two: `detail::parse_url` in shell/net/net.cpp for the HTTP client
// and `split_url` in shell/bindings.cpp for `location.*`. They were written
// separately, and they disagreed - which is the whole reason this file exists.
//
// THE BUG THAT PROVES IT. `split_url` took `host.rfind(':')` with no bracket
// guard, so `http://[::1]/` gave hostname `[:` and port `1]`. `net.cpp` guarded
// exactly that case. One codebase, two parsers, one of them wrong, and nothing
// compared them - so this test compares them by making them the same code.
//
// Written against the NEW header before it existed, so every assertion below
// failed first and then passed. A test written after the fact proves the code
// does what it does.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <string>

using namespace ctbrowser;
using ctbrowser::shell::location_parts;
using ctbrowser::shell::parse_absolute;
using ctbrowser::shell::resolve;

namespace {

// --- what the HTTP client asks -------------------------------------------

void test_an_ipv6_host_keeps_its_brackets() {
    // THE REGRESSION THIS FILE IS NAMED FOR. `[::1]` is a host containing
    // colons, so anything reaching for the last colon finds one INSIDE the
    // address rather than the port separator.
    const auto with_port = parse_absolute("http://[::1]:8080/x");
    CHECK(with_port.valid);
    CHECK(with_port.host == "::1"); // the connect address, brackets stripped
    CHECK(with_port.port == "8080");
    CHECK(with_port.target == "/x");

    const auto no_port = parse_absolute("http://[::1]/x");
    CHECK(no_port.valid);
    CHECK(no_port.host == "::1");
    CHECK(no_port.port == "80"); // the scheme's default, not "1]"
}

void test_the_host_header_form_is_not_the_connect_address() {
    // Two different needs from one authority, and conflating them is a bug in
    // whichever direction you pick. A resolver wants `::1`; the `Host:` header
    // must say `[::1]:8080` or the server parses the address's colons as a port.
    const auto six = parse_absolute("http://[::1]:8080/x");
    CHECK(six.host == "::1");
    CHECK(six.authority == "[::1]:8080");

    // The DEFAULT port is omitted, as the RFC asks.
    CHECK(parse_absolute("http://h/x").authority == "h");
    CHECK(parse_absolute("https://h/x").authority == "h");
    CHECK(parse_absolute("http://h:8080/x").authority == "h:8080");
    CHECK(parse_absolute("http://[::1]/x").authority == "[::1]");
}

void test_the_scheme_and_host_are_lowercased() {
    // A URL's scheme and host are case-insensitive; its PATH is not. Getting
    // that wrong in either direction is a bug - lowercasing the path breaks
    // every case-sensitive server.
    const auto out = parse_absolute("HTTP://Example.COM/Path/To/File");
    CHECK(out.valid);
    CHECK(out.scheme == "http");
    CHECK(out.host == "example.com");
    CHECK(out.target == "/Path/To/File");
}

void test_dot_segments_are_removed() {
    // `/a/b/../c` reached the server verbatim before. Servers are not required
    // to resolve it, and a cache keyed on the request line sees two URLs where
    // there is one.
    const auto out = parse_absolute("http://h/a/b/../c");
    CHECK(out.valid);
    CHECK(out.target == "/a/c");
}

void test_a_missing_path_becomes_a_slash() {
    const auto out = parse_absolute("http://h");
    CHECK(out.valid);
    CHECK(out.target == "/");
    CHECK(out.port == "80");
    CHECK(parse_absolute("https://h").port == "443");
}

void test_the_query_is_sent_and_the_fragment_is_not() {
    // A FRAGMENT IS CLIENT-SIDE. Sending it is a privacy leak and a spec
    // violation; dropping the query instead would break every search.
    const auto out = parse_absolute("http://h/search?q=1&r=2#results");
    CHECK(out.valid);
    CHECK(out.target == "/search?q=1&r=2");
}

void test_credentials_are_dropped() {
    // DELIBERATE, and carried over rather than inherited by accident: this
    // client does no HTTP auth, so credentials in the authority would either be
    // ignored or - much worse - sent as part of the Host header.
    const auto out = parse_absolute("http://user:secret@h/x");
    CHECK(out.valid);
    CHECK(out.host == "h");
    CHECK(out.target == "/x");
    CHECK(out.target.find("secret") == std::string::npos);
}

void test_only_http_schemes_are_valid_to_fetch() {
    CHECK(!parse_absolute("file:///etc/passwd").valid);
    CHECK(!parse_absolute("blob:ctbrowser/1").valid);
    CHECK(!parse_absolute("not a url at all").valid);
    CHECK(!parse_absolute("").valid);
}

// --- what the DOM asks ----------------------------------------------------

void test_location_parts_of_an_http_url() {
    const auto out = location_parts("http://example.com:8080/a/b?q=1#frag");
    CHECK(out.protocol == "http:"); // WITH the colon, as the DOM reports it
    CHECK(out.host == "example.com:8080");
    CHECK(out.hostname == "example.com");
    CHECK(out.port == "8080");
    CHECK(out.pathname == "/a/b");
    CHECK(out.search == "?q=1");
    CHECK(out.hash == "#frag");
    CHECK(out.origin == "http://example.com:8080");
}

void test_location_hostname_keeps_ipv6_brackets() {
    // The OPPOSITE of parse_absolute above, and both are right. A socket wants
    // the address; `location.hostname` is specified to keep the brackets, so a
    // page reassembling `hostname + ':' + port` produces a valid URL again.
    const auto out = location_parts("http://[::1]:8080/x");
    CHECK(out.hostname == "[::1]");
    CHECK(out.port == "8080");
    CHECK(out.host == "[::1]:8080");
}

void test_location_parts_of_a_file_url() {
    const auto out = location_parts("file:///home/a/page.html");
    CHECK(out.protocol == "file:");
    CHECK(out.pathname == "/home/a/page.html");
    CHECK(out.host.empty());
    // A file URL has NO origin. "null" is the string the DOM reports, not the
    // absence of the property.
    CHECK(out.origin == "null");
}

void test_location_parts_survive_the_odd_schemes_the_engine_mints() {
    // `createObjectURL` mints these, and `toDataURL` the other. Neither has an
    // authority, and a parser that rejected them would break image export.
    CHECK(location_parts("blob:ctbrowser/1").protocol == "blob:");
    CHECK(location_parts("data:image/png;base64,iVBOR").protocol == "data:");
    CHECK(location_parts("about:blank").protocol == "about:");
}

void test_an_empty_search_is_empty_not_a_bare_question_mark() {
    // The DOM reports "" for a URL with no query, not "?" - a page doing
    // `location.search.slice(1)` on a bare "?" gets an empty string either way,
    // but one doing `if (location.search)` takes the wrong branch.
    const auto out = location_parts("http://h/a");
    CHECK(out.search.empty());
    CHECK(out.hash.empty());
    CHECK(out.pathname == "/a");
}

void test_a_url_with_no_path_still_has_one() {
    // Separate from the above, because it is a separate claim - and asserting
    // pathname == "/" for `http://h/a` is how this test first failed, which was
    // the test being wrong rather than the parser.
    CHECK(location_parts("http://h").pathname == "/");
    CHECK(location_parts("http://h?q=1").pathname == "/");
}

// --- the capability neither parser had ------------------------------------

void test_resolve_against_a_base() {
    const std::string base = "http://h/a/b";
    CHECK(resolve(base, "../c") == "http://h/c");
    CHECK(resolve(base, "d") == "http://h/a/d");
    CHECK(resolve(base, "/abs") == "http://h/abs");
    CHECK(resolve(base, "//other/x") == "http://other/x");
    CHECK(resolve(base, "?q=2") == "http://h/a/b?q=2");
    // An ABSOLUTE reference ignores the base entirely.
    CHECK(resolve(base, "https://elsewhere/z") == "https://elsewhere/z");
}

// --- leniency -------------------------------------------------------------

void test_a_url_a_browser_accepts_is_accepted() {
    // BOOST.URL IS A STRICT RFC 3986 PARSER and rejects both of these outright.
    // A browser does not, and neither did the two hand-rolled parsers this
    // replaced - so handing pages a stricter engine than they were written for
    // would be a regression dressed as correctness. The wrapper percent-encodes
    // what RFC 3986 disallows before parsing, which is what a browser does.
    //
    // ctcss and ctjs are both documented as lenient parsers. This matches them.
    const auto spaced = parse_absolute("http://h/a b/c");
    CHECK(spaced.valid);
    CHECK(spaced.target == "/a%20b/c");

    const auto utf8 = parse_absolute("http://h/\xc3\xa9");
    CHECK(utf8.valid);
    CHECK(utf8.target == "/%C3%A9");

    // AND IT DOES NOT DOUBLE-ENCODE what a page already encoded, which is the
    // failure mode of doing this carelessly: `%20` must not become `%2520`.
    const auto already = parse_absolute("http://h/a%20b");
    CHECK(already.valid);
    CHECK(already.target == "/a%20b");
}

} // namespace

int main() {
    test_an_ipv6_host_keeps_its_brackets();
    test_the_host_header_form_is_not_the_connect_address();
    test_the_scheme_and_host_are_lowercased();
    test_dot_segments_are_removed();
    test_a_missing_path_becomes_a_slash();
    test_the_query_is_sent_and_the_fragment_is_not();
    test_credentials_are_dropped();
    test_only_http_schemes_are_valid_to_fetch();
    test_location_parts_of_an_http_url();
    test_location_hostname_keeps_ipv6_brackets();
    test_location_parts_of_a_file_url();
    test_location_parts_survive_the_odd_schemes_the_engine_mints();
    test_an_empty_search_is_empty_not_a_bare_question_mark();
    test_a_url_with_no_path_still_has_one();
    test_resolve_against_a_base();
    test_a_url_a_browser_accepts_is_accepted();
    REPORT("url_basics");
}
