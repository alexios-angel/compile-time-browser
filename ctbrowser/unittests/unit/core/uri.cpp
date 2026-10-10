#include <ctbrowser/core/uri.hpp>

#include "check.hpp"

using namespace ctbrowser;

int main() {
    for (auto decode : {decode_uri, decode_uri_component}) {
        CHECK(decode("") == "");
        CHECK(decode("plain+text") == "plain+text");
        CHECK(decode("a%20b%25c") == "a b%c");
        CHECK(decode("%252F") == "%2F"); // one decoding pass
        CHECK(decode("%C2%80%DF%BF") == "\xC2\x80\xDF\xBF");
        CHECK(decode("%E0%A0%80%ED%9F%BF%EE%80%80%EF%BF%BF") ==
              "\xE0\xA0\x80\xED\x9F\xBF\xEE\x80\x80\xEF\xBF\xBF");
        CHECK(decode("%F0%90%80%80%F4%8F%BF%BF") == "\xF0\x90\x80\x80\xF4\x8F\xBF\xBF");
        CHECK(decode("%c3%a9%E2%82%Ac") == "\xC3\xA9\xE2\x82\xAC");
        CHECK((decode("a%00b") == std::string{"a\0b", 3}));
        CHECK((decode(std::string_view{"a\0b", 3}) == std::string{"a\0b", 3}));

        // Only escaped bytes are UTF-8 validated, matching the original VM decoder.
        for (unsigned byte = 0; byte <= 255; ++byte) {
            if (byte == '%') { continue; }
            const std::string raw(1, static_cast<char>(byte));
            CHECK(decode(raw) == raw);
        }
        CHECK(decode("\xED\xA0\x80%20\xFF") == "\xED\xA0\x80 \xFF");

        for (const std::string_view malformed : {"%",
                                                 "%0",
                                                 "%GG",
                                                 "%0G",
                                                 "%G0",
                                                 "%80",
                                                 "%BF",
                                                 "%C0%80",
                                                 "%C1%BF",
                                                 "%C2",
                                                 "%C2%",
                                                 "%C2xA0",
                                                 "%C2%4A",
                                                 "%C2%8G",
                                                 "%C2%G0",
                                                 "%E0%80%80",
                                                 "%E0%A0",
                                                 "%E2%82xAC",
                                                 "%ED%A0%80",
                                                 "%ED%BF%BF",
                                                 "%F0%80%80%80",
                                                 "%F0%90%80",
                                                 "%F4%90%80%80",
                                                 "%F5%80%80%80",
                                                 "%F8%80%80%80%80",
                                                 "%FF",
                                                 "prefix%20ok%"}) {
            CHECK(!decode(malformed));
        }
        CHECK(!decode("%C2\x80")); // raw continuation bytes cannot complete an escape
    }

    CHECK(decode_uri("%3b%2F%3f%3A%40%26%3d%2B%24%2c%23") == "%3b%2F%3f%3A%40%26%3d%2B%24%2c%23");
    CHECK(decode_uri_component("%3b%2F%3f%3A%40%26%3d%2B%24%2c%23") == ";/?:@&=+$,#");
    CHECK(decode_uri("a%20b%26c%2F%C3%A9") == "a b%26c%2F\xC3\xA9");
    CHECK(decode_uri_component("a%20b%26c%2F%C3%A9") == "a b&c/\xC3\xA9");
    REPORT("core_uri");
}
