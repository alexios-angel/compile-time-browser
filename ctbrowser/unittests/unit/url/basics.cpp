//===- ctbrowser/unittests/unit/url/basics.cpp - URL parser tests ---------===//
//
// Tests for shell/net/url.hpp, the one URL parser shared by the HTTP client
// (parse_absolute) and the DOM's `location` (location_parts), plus resolve()
// against a base. Two separate parsers once disagreed on `http://[::1]/`;
// these cases pin the shared behaviour.
//
//===----------------------------------------------------------------------===//

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
    // `[::1]` is a host containing colons, so anything reaching for the last
    // colon finds one inside the address rather than the port separator.
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

    // The default port is omitted, as the RFC asks.
    CHECK(parse_absolute("http://h/x").authority == "h");
    CHECK(parse_absolute("https://h/x").authority == "h");
    CHECK(parse_absolute("http://h:8080/x").authority == "h:8080");
    CHECK(parse_absolute("http://[::1]/x").authority == "[::1]");
}

void test_the_scheme_and_host_are_lowercased() {
    // A URL's scheme and host are case-insensitive; its path is not, and
    // lowercasing the path would break every case-sensitive server.
    const auto out = parse_absolute("HTTP://Example.COM/Path/To/File");
    CHECK(out.valid);
    CHECK(out.scheme == "http");
    CHECK(out.host == "example.com");
    CHECK(out.target == "/Path/To/File");
}

void test_dot_segments_are_removed() {
    // Dot segments are resolved before sending: servers are not required to,
    // and a cache keyed on the request line would see two URLs where there is
    // one.
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
    // A fragment is client-side. Sending it is a privacy leak and a spec
    // violation; dropping the query instead would break every search.
    const auto out = parse_absolute("http://h/search?q=1&r=2#results");
    CHECK(out.valid);
    CHECK(out.target == "/search?q=1&r=2");
}

void test_credentials_are_dropped() {
    // Credentials are dropped on purpose: this client does no HTTP auth, so
    // they would either be ignored or, worse, sent in the Host header.
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
    CHECK(out.protocol == "http:"); // with the colon, as the DOM reports it
    CHECK(out.host == "example.com:8080");
    CHECK(out.hostname == "example.com");
    CHECK(out.port == "8080");
    CHECK(out.pathname == "/a/b");
    CHECK(out.search == "?q=1");
    CHECK(out.hash == "#frag");
    CHECK(out.origin == "http://example.com:8080");
}

void test_location_hostname_keeps_ipv6_brackets() {
    // The opposite of parse_absolute above, and both are right. A socket wants
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
    // A file URL has no origin. "null" is the string the DOM reports, not the
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
    // The DOM reports "" for a URL with no query, not "?". A page doing
    // `location.search.slice(1)` on a bare "?" gets an empty string either way,
    // but one doing `if (location.search)` takes the wrong branch.
    const auto out = location_parts("http://h/a");
    CHECK(out.search.empty());
    CHECK(out.hash.empty());
    CHECK(out.pathname == "/a");
}

void test_a_url_with_no_path_still_has_one() {
    // A URL with no path still reports pathname "/".
    CHECK(location_parts("http://h").pathname == "/");
    CHECK(location_parts("http://h?q=1").pathname == "/");
}

// --- resolving a reference against a base ---------------------------------

void test_resolve_against_a_base() {
    const std::string base = "http://h/a/b";
    CHECK(resolve(base, "../c") == "http://h/c");
    CHECK(resolve(base, "d") == "http://h/a/d");
    CHECK(resolve(base, "/abs") == "http://h/abs");
    CHECK(resolve(base, "//other/x") == "http://other/x");
    CHECK(resolve(base, "?q=2") == "http://h/a/b?q=2");
    // An absolute reference ignores the base entirely.
    CHECK(resolve(base, "https://elsewhere/z") == "https://elsewhere/z");
}

// --- leniency -------------------------------------------------------------

void test_a_url_a_browser_accepts_is_accepted() {
    // A raw space and a raw UTF-8 byte in a path are percent-encoded, as the
    // URL Standard's path percent-encode set requires (RFC 3986 would reject
    // both). See wpt.cpp for the full corpus.
    const auto spaced = parse_absolute("http://h/a b/c");
    CHECK(spaced.valid);
    CHECK(spaced.target == "/a%20b/c");

    const auto utf8 = parse_absolute("http://h/\xc3\xa9");
    CHECK(utf8.valid);
    CHECK(utf8.target == "/%C3%A9");

    // It does not double-encode what a page already encoded, which is the
    // failure mode of doing this carelessly: `%20` must not become `%2520`.
    const auto already = parse_absolute("http://h/a%20b");
    CHECK(already.valid);
    CHECK(already.target == "/a%20b");
}

// --- where the URL Standard differs from RFC 3986 ----------------------------

void test_the_standard_not_rfc_3986() {
    // Each of these differed from a browser when the parser was Boost.URL
    // (docs/history/ada-url.md measured them); they now match the URL Standard.
    CHECK(parse_absolute("http://h:80/x").authority == "h"); // a default port is dropped
    CHECK(location_parts("http://h:80/x").port.empty());
    CHECK(location_parts("http:\\\\h\\a").pathname == "/a"); // `\` is `/` when special
    CHECK(location_parts("http:\\\\h\\a").hostname == "h");
    CHECK(location_parts("http://h/a\tb").pathname == "/ab"); // a tab is removed anywhere
    CHECK(resolve("http://h/", "http://\xe6\x97\xa5\xe6\x9c\xac.jp/") ==
          "http://xn--wgv71a.jp/");                              // IDNA to punycode
    CHECK(location_parts("http://h/%2e%2e/x").pathname == "/x"); // encoded dot segments
    CHECK(location_parts("http://User@h/").username == "User");
    // An unparseable reference comes back as written, which is what HTML's
    // URL reflection asks for.
    CHECK(resolve("http://h/", "http://[::1") == "http://[::1");
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
    test_the_standard_not_rfc_3986();
    REPORT("url_basics");
}
