// The URL Standard's own conformance corpus, driven through shell/net/url.hpp
// with no VM in between: url/resources/urltestdata.json (the parser and the
// serialiser, ~900 inputs) and setters_tests.json (the §6.1 setter steps, ~280
// cases) from the web-platform-tests checkout tools/wpt/fetch-wpt.sh makes at
// ~/.cache/wpt. Read when present, like the html5lib fixtures; absent, the
// hand-written cases below still run and the file passes.
//
// EVERY CASE PASSES at the WPT commit tools/wpt/fetch-wpt.sh pins (893 of 893
// and 278 of 278, 2026-09-16) - so the file asserts the total rather than a
// ratchet.
//
// IdnaTestV2.json is a RATCHET at 2,668 of 2,671: its JSON is generated with
// --exclude-bidi, so CheckBidi (now done, see domain_to_ascii) does not move
// it, and the three that miss turn on a handful of code points whose mapping
// status differs between the fetched IdnaMappingTable and unicodedata's Unicode
// release (the drift the generator's docstring names). Raise the floor if that
// closes; never lower it. The CheckBidi wins are in url/toascii.window.js,
// which carries its Bidi cases inline.

#include <ctbrowser.hpp>
#include <ctbrowser/core/json.hpp>

#include "check.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <variant>

using namespace ctbrowser;
using namespace ctbrowser::shell;

namespace {

[[nodiscard]] std::filesystem::path corpus_dir() {
    if (const char * home = std::getenv("HOME"); home != nullptr) {
        return std::filesystem::path{home} / ".cache" / "wpt" / "url" / "resources";
    }
    return {};
}

[[nodiscard]] const json_value * member(const json_value & object, std::string_view key) {
    const auto * fields = std::get_if<json_value::object>(&object.data);
    if (fields == nullptr) { return nullptr; }
    for (const json_value::member & m : *fields) {
        if (m.key == key) { return &m.value; }
    }
    return nullptr;
}

[[nodiscard]] std::string text_of(const json_value * v) {
    const auto * s = v == nullptr ? nullptr : std::get_if<std::string>(&v->data);
    return s == nullptr ? std::string{} : *s;
}

// --- urltestdata.json -------------------------------------------------------

void test_the_parser_corpus() {
    const std::string text = read_file(corpus_dir() / "urltestdata.json");
    if (text.empty()) {
        std::puts("url_wpt: no ~/.cache/wpt checkout; the parser corpus is skipped");
        return;
    }
    const auto parsed = parse_json(text);
    CHECK(parsed.has_value());
    if (!parsed) { return; }
    const auto * cases = std::get_if<json_value::array>(&parsed->data);
    CHECK(cases != nullptr);
    if (cases == nullptr) { return; }

    int total = 0;
    int passed = 0;
    for (const json_value & c : *cases) {
        if (std::holds_alternative<std::string>(c.data)) { continue; } // a comment
        ++total;
        const std::string input = text_of(member(c, "input"));
        const json_value * base = member(c, "base");
        const bool has_base = base != nullptr && std::holds_alternative<std::string>(base->data);
        const std::optional<url_record> url =
            has_base ? parse_url(input, text_of(base)) : parse_url(input);
        const bool expect_failure = member(c, "failure") != nullptr;
        bool ok = false;
        if (expect_failure) {
            ok = !url.has_value();
        } else if (url) {
            ok = url->serialize() == text_of(member(c, "href")) &&
                 url->protocol() == text_of(member(c, "protocol")) &&
                 url->username == text_of(member(c, "username")) &&
                 url->password == text_of(member(c, "password")) &&
                 url->host_and_port() == text_of(member(c, "host")) &&
                 url->hostname() == text_of(member(c, "hostname")) &&
                 url->port_text() == text_of(member(c, "port")) &&
                 url->pathname() == text_of(member(c, "pathname")) &&
                 url->search() == text_of(member(c, "search")) &&
                 url->hash() == text_of(member(c, "hash"));
            if (const json_value * origin = member(c, "origin"); ok && origin != nullptr) {
                ok = url->origin() == text_of(origin);
            }
            // And the serialisation re-parses to itself - see the setters below.
            const std::optional<url_record> again = parse_url(url->serialize());
            if (!again || again->serialize() != url->serialize()) { ok = false; }
        }
        if (ok) {
            ++passed;
        } else if (std::getenv("CTBROWSER_URL_VERBOSE") != nullptr) {
            std::printf("  miss: <%s> against <%s> -> %s\n", input.c_str(),
                        has_base ? text_of(base).c_str() : "(none)",
                        url ? url->serialize().c_str() : "failure");
        }
    }
    std::printf("url_wpt: urltestdata.json %d / %d\n", passed, total);
    CHECK(total >= 800);
    CHECK(passed == total);
}

// --- setters_tests.json ----------------------------------------------------------

void test_the_setters_corpus() {
    const std::string text = read_file(corpus_dir() / "setters_tests.json");
    if (text.empty()) { return; }
    const auto parsed = parse_json(text);
    CHECK(parsed.has_value());
    if (!parsed) { return; }
    const auto * groups = std::get_if<json_value::object>(&parsed->data);
    CHECK(groups != nullptr);
    if (groups == nullptr) { return; }

    static constexpr std::pair<std::string_view, url_part> parts[] = {
        {"href", url_part::href},         {"protocol", url_part::protocol},
        {"username", url_part::username}, {"password", url_part::password},
        {"host", url_part::host},         {"hostname", url_part::hostname},
        {"port", url_part::port},         {"pathname", url_part::pathname},
        {"search", url_part::search},     {"hash", url_part::hash},
    };
    const auto part_named = [&](std::string_view name) -> std::optional<url_part> {
        for (const auto & [known, part] : parts) {
            if (known == name) { return part; }
        }
        return std::nullopt;
    };
    const auto read = [](const url_record & url, std::string_view name) -> std::string {
        if (name == "href") { return url.serialize(); }
        if (name == "protocol") { return url.protocol(); }
        if (name == "username") { return url.username; }
        if (name == "password") { return url.password; }
        if (name == "host") { return url.host_and_port(); }
        if (name == "hostname") { return url.hostname(); }
        if (name == "port") { return url.port_text(); }
        if (name == "pathname") { return url.pathname(); }
        if (name == "search") { return url.search(); }
        if (name == "hash") { return url.hash(); }
        if (name == "origin") { return url.origin(); }
        return "?";
    };

    int total = 0;
    int passed = 0;
    for (const json_value::member & group : *groups) {
        const std::optional<url_part> part = part_named(group.key);
        if (!part) { continue; } // "comment"
        const auto * cases = std::get_if<json_value::array>(&group.value.data);
        if (cases == nullptr) { continue; }
        for (const json_value & c : *cases) {
            ++total;
            std::optional<url_record> url = parse_url(text_of(member(c, "href")));
            if (!url) { continue; }
            (void)set_url_part(*url, *part, text_of(member(c, "new_value")));
            bool ok = true;
            const json_value * expected = member(c, "expected");
            const auto * fields =
                expected == nullptr ? nullptr : std::get_if<json_value::object>(&expected->data);
            if (fields == nullptr) { continue; }
            for (const json_value::member & e : *fields) {
                if (read(*url, e.key) != text_of(&e.value)) { ok = false; }
            }
            // THE BINDINGS KEEP THE SERIALISATION, NOT THE RECORD (bindings/
            // window/url.cpp re-parses on every read), so every record a
            // setter can produce must survive the round trip unchanged.
            if (const std::optional<url_record> again = parse_url(url->serialize());
                !again || again->serialize() != url->serialize() ||
                again->pathname() != url->pathname() || again->host != url->host ||
                again->username != url->username || again->query != url->query) {
                ok = false;
            }
            if (ok) {
                ++passed;
            } else if (std::getenv("CTBROWSER_URL_VERBOSE") != nullptr) {
                std::printf("  miss: <%s>.%s = '%s' -> %s\n", text_of(member(c, "href")).c_str(),
                            group.key.c_str(), text_of(member(c, "new_value")).c_str(),
                            url->serialize().c_str());
            }
        }
    }
    std::printf("url_wpt: setters_tests.json %d / %d\n", passed, total);
    CHECK(total >= 250);
    CHECK(passed == total);
}

// --- the rows docs/plans/ada-url.md measured, now pinned ----------------------------

void test_the_eight_rows_that_differed_from_a_browser() {
    CHECK(resolve("http://example.com/", "http://example.com:80/a") == "http://example.com/a");
    CHECK(resolve("http://example.com/", "https://example.com:443/a") == "https://example.com/a");
    CHECK(resolve("http://example.com/", "http:\\\\example.com\\a") == "http://example.com/a");
    CHECK(resolve("http://example.com/", "http://example.com/a\tb") == "http://example.com/ab");
    CHECK(resolve("http://example.com/", "http://\xe6\x97\xa5\xe6\x9c\xac.jp/") ==
          "http://xn--wgv71a.jp/");
    CHECK(resolve("http://example.com/", "http://example.com") == "http://example.com/");
    CHECK(resolve("http://example.com/", "http://example.com/a/%2e%2e/x") ==
          "http://example.com/x");
    CHECK(resolve("http://example.com/", "  http://example.com/a  ") == "http://example.com/a");
}

void test_form_urlencoded_round_trips() {
    const form_pairs pairs = parse_form_urlencoded("a=b+c&d=%26&e&=f&&");
    CHECK(pairs.size() == 4);
    CHECK(pairs[0].first == "a" && pairs[0].second == "b c");
    CHECK(pairs[1].first == "d" && pairs[1].second == "&");
    CHECK(pairs[2].first == "e" && pairs[2].second.empty());
    CHECK(pairs[3].first.empty() && pairs[3].second == "f");
    CHECK(serialize_form_urlencoded(pairs) == "a=b+c&d=%26&e=&=f");
    CHECK(parse_form_urlencoded(serialize_form_urlencoded(pairs)) == pairs);
    CHECK(serialize_form_urlencoded({{"q", "+1 (555)"}}) == "q=%2B1+%28555%29");
}

// CheckBidi (RFC 5893) and the all-ASCII short-circuit that carries
// IgnoreInvalidPunycode - the url/toascii.window.js cases, driven through the
// whole parser so they run even without the corpus on disk.
void test_checkbidi_and_the_ascii_short_circuit() {
    const auto host_of = [](const std::string & domain) -> std::optional<std::string> {
        const std::optional<url_record> url = parse_url("https://" + domain + "/x");
        return url ? std::optional<std::string>{url->hostname()} : std::nullopt;
    };
    // An all-ASCII domain is left verbatim: an invalid `xn--` label is not
    // decoded to a disallowed U+0080 and refused.
    CHECK(host_of("xn--a") == "xn--a");
    CHECK(host_of("xn--a.xn--zca") == "xn--a.xn--zca");
    // But a domain that already carries non-ASCII must decode every `xn--`
    // label, and one that decodes to an invalid label fails the whole domain.
    CHECK(host_of("xn--a.\xc3\x9f") == std::nullopt); // xn--a.ß
    // CheckBidi: a Latin letter ends an Arabic (RTL) label - rule 2.
    CHECK(host_of("\xd9\x8a"
                  "a") == std::nullopt); // ي then 'a'
    // An R (Hebrew) point inside a Latin (LTR) label - rule 5.
    CHECK(host_of("look\xd6\xbeout.net") == std::nullopt); // look U+05BE out.net
    // A well-formed Arabic (RTL) domain still resolves - CheckBidi is not a
    // blanket ban on right-to-left labels.
    CHECK(host_of("\xd9\x85\xd8\xab\xd8\xa7\xd9\x84") == "xn--mgbh0fb"); // مثال
}

void test_a_lone_surrogate_becomes_the_replacement_character() {
    // WTF-8 for U+D800, as the VM spells a lone surrogate.
    CHECK(to_usv_string("a\xed\xa0\x80z") == "a\xef\xbf\xbdz");
    const std::optional<url_record> url = parse_url("http://h/\xed\xa0\x80");
    CHECK(url && url->pathname() == "/%EF%BF%BD");
}

// --- IdnaTestV2.json ---------------------------------------------------------

// Through the WHOLE URL parser rather than a private entry point: the domain
// is what `https://<input>/x` reports as its host, which is the same path
// url/IdnaTestV2.any.js drives and the only one a page can reach.
void test_the_idna_corpus() {
    for (const char * name : {"IdnaTestV2.json", "IdnaTestV2-removed.json"}) {
        const std::string text = read_file(corpus_dir() / name);
        if (text.empty()) { return; }
        const auto parsed = parse_json(text);
        CHECK(parsed.has_value());
        if (!parsed) { return; }
        const auto * cases = std::get_if<json_value::array>(&parsed->data);
        CHECK(cases != nullptr);
        if (cases == nullptr) { return; }

        int total = 0;
        int passed = 0;
        for (const json_value & c : *cases) {
            if (std::holds_alternative<std::string>(c.data)) { continue; }
            ++total;
            const std::string input = text_of(member(c, "input"));
            const json_value * output = member(c, "output");
            // A null or empty `output` is the corpus's "this must not parse".
            const bool expect_failure = output == nullptr ||
                                        !std::holds_alternative<std::string>(output->data) ||
                                        text_of(output).empty();
            const std::optional<url_record> url = parse_url("https://" + input + "/x");
            const bool ok =
                expect_failure ? !url.has_value() : (url && url->hostname() == text_of(output));
            if (ok) {
                ++passed;
            } else if (std::getenv("CTBROWSER_URL_VERBOSE") != nullptr) {
                std::printf("  miss: <%s> -> %s (want %s)\n", input.c_str(),
                            url ? url->hostname().c_str() : "failure",
                            expect_failure ? "failure" : text_of(output).c_str());
            }
        }
        std::printf("url_wpt: %s %d / %d\n", name, passed, total);
        // The ratchet, and the removed-codepoint file which is exact.
        CHECK(passed >= (total > 100 ? 2668 : total));
    }
}

} // namespace

int main() {
    test_the_parser_corpus();
    test_the_idna_corpus();
    test_the_setters_corpus();
    test_the_eight_rows_that_differed_from_a_browser();
    test_form_urlencoded_round_trips();
    test_checkbidi_and_the_ascii_short_circuit();
    test_a_lone_surrogate_becomes_the_replacement_character();
    REPORT("url_wpt");
}
