// A native client of JSON parsing links Core alone and owns every parsed byte.
#include <ctbrowser/core/json.hpp>

#include "check.hpp"
#include <cmath>
#include <utility>

using namespace ctbrowser;

[[nodiscard]] static json_value parsed(std::string_view text) {
    auto result = parse_json(text);
    CHECK(result.has_value());
    return std::move(result).value();
}

int main() {
    CHECK(std::holds_alternative<std::nullptr_t>(parsed(" \t\r\nnull \n").data));
    CHECK(std::get<bool>(parsed("true").data));
    CHECK(!std::get<bool>(parsed("false").data));
    CHECK(std::get<json_value::array>(parsed("[]").data).empty());
    CHECK(std::get<json_value::object>(parsed("{}").data).empty());

    const json_value owned = [] {
        std::string source = R"({"name":"original","nested":[{"key":"value"},null,true,1.25]})";
        json_value result = parsed(source);
        source.assign(source.size(), '!');
        return result;
    }();
    const auto & members = std::get<json_value::object>(owned.data);
    CHECK_EQ(members.size(), 2u);
    CHECK_EQ(members[0].key, "name");
    CHECK_EQ(std::get<std::string>(members[0].value.data), "original");
    CHECK_EQ(members[1].key, "nested");
    const auto & nested = std::get<json_value::array>(members[1].value.data);
    CHECK_EQ(nested.size(), 4u);
    const auto & child = std::get<json_value::object>(nested[0].data);
    CHECK_EQ(child[0].key, "key");
    CHECK_EQ(std::get<std::string>(child[0].value.data), "value");
    CHECK(std::holds_alternative<std::nullptr_t>(nested[1].data));
    CHECK(std::get<bool>(nested[2].data));
    CHECK_EQ(std::get<double>(nested[3].data), 1.25);
    json_value copied = owned;
    std::get<json_value::object>(copied.data)[0].key = "changed";
    CHECK_EQ(members[0].key, "name");

    // Duplicate names replace their first slot; numeric-looking names stay in
    // source order here. The VM adapter applies its own property enumeration.
    const auto duplicates = parsed(
        R"({"b":1,"2":2,"__proto__":{},"a":4,"2":5,"b":6,"\u0000":7,"__proto__":null,"01":8,"\u0000":9,"\u0061":10})");
    const auto & entries = std::get<json_value::object>(duplicates.data);
    CHECK_EQ(entries.size(), 6u);
    CHECK_EQ(entries[0].key, "b");
    CHECK_EQ(std::get<double>(entries[0].value.data), 6.0);
    CHECK_EQ(entries[1].key, "2");
    CHECK_EQ(std::get<double>(entries[1].value.data), 5.0);
    CHECK_EQ(entries[2].key, "__proto__");
    CHECK(std::holds_alternative<std::nullptr_t>(entries[2].value.data));
    CHECK_EQ(entries[3].key, "a");
    CHECK_EQ(std::get<double>(entries[3].value.data), 10.0);
    CHECK_EQ(entries[4].key, std::string(1, '\0'));
    CHECK_EQ(std::get<double>(entries[4].value.data), 9.0);
    CHECK_EQ(entries[5].key, "01");

    CHECK_EQ(std::get<std::string>(parsed(R"("\"\\\/\b\f\n\r\t")").data), "\"\\/\b\f\n\r\t");
    CHECK_EQ(std::get<std::string>(parsed(R"("a\u0000b")").data), (std::string{"a\0b", 3}));
    CHECK_EQ(std::get<std::string>(parsed(R"("\u0041\u00e9\u20AC\ud83d\ude00")").data),
             "A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80");
    CHECK_EQ(std::get<std::string>(parsed(R"("\ud800\u0041\udc00")").data), "\xEF\xBF\xBD"
                                                                            "A"
                                                                            "\xEF\xBF\xBD");
    CHECK_EQ(std::get<std::string>(parsed(R"("\ud800\ud800\udc00")").data),
             "\xEF\xBF\xBD\xF0\x90\x80\x80");
    // The original reader passes raw bytes through, including malformed UTF-8.
    CHECK_EQ(std::get<std::string>(parsed("\"\xC3\xA9\xFF\xED\xA0\x80\"").data),
             "\xC3\xA9\xFF\xED\xA0\x80");

    struct number_case {
        std::string_view text;
        double value;
    };
    for (const auto [text, value] :
         {number_case{"0", 0.0}, number_case{"-12", -12.0}, number_case{"1.25e+2", 125.0},
          number_case{"1E-2", 0.01}, number_case{"1e400", 0.0}, number_case{"-1e-400", 0.0}}) {
        CHECK_EQ(std::get<double>(parsed(text).data), value);
    }
    CHECK(std::signbit(std::get<double>(parsed("-0").data)));
    CHECK(std::signbit(std::get<double>(parsed("-0.0e+1").data)));
    // Preserve the old reader's ignored from_chars out-of-range result.
    CHECK(!std::signbit(std::get<double>(parsed("-1e-400").data)));

    struct bad_case {
        std::string_view text;
        std::size_t offset;
    };
    for (const auto [text, offset] : {
             bad_case{"", 0},
             bad_case{" \t\n", 3},
             bad_case{"[", 1},
             bad_case{"[1,", 3},
             bad_case{"[1,]", 3},
             bad_case{R"({"a":})", 5},
             bad_case{R"({"a":1,})", 7},
             bad_case{R"({"a" 1})", 5},
             bad_case{R"({"a":1 x})", 7},
             bad_case{R"({"a":[0,]})", 8},
             bad_case{R"("\x")", 3},
             bad_case{R"("\u00")", 3},
             bad_case{R"("\uZZZZ")", 3},
             bad_case{R"("\ud800\uZZZZ")", 9},
             bad_case{"\"abc", 4},
             bad_case{"\"\\", 2},
             bad_case{"+1", 0},
             bad_case{"01", 1},
             bad_case{"-01", 2},
             bad_case{".5", 0},
             bad_case{"1.", 2},
             bad_case{"1e", 2},
             bad_case{"1e+", 3},
             bad_case{"-", 1},
             bad_case{"Infinity", 0},
             bad_case{"NaN", 0},
             bad_case{"tru", 0},
             bad_case{"nullx", 4},
             bad_case{"true \t false", 7},
             bad_case{"0x10", 1},
             bad_case{"\f1", 0},
             bad_case{"1\v", 1},
             bad_case{"\xC2\xA0"
                      "1",
                      0},
             bad_case{"\"\n\"", 2},
             bad_case{"1e \tX", 4},
             bad_case{"[1, \n", 5},
         }) {
        const auto result = parse_json(text);
        CHECK(!result);
        if (!result) { CHECK_EQ(result.error(), offset); }
    }
    for (unsigned byte = 0; byte < 0x20; ++byte) {
        const std::string source = "\"" + std::string(1, static_cast<char>(byte)) + "\"";
        CHECK(!parse_json(source));
    }
    CHECK(!parse_json(std::string_view{"null\0", 5}));
    REPORT("core_json");
}
