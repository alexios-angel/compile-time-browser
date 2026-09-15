// Public numeric conversion must work when only Core is linked.
#include <ctbrowser/core/number_format.hpp>

#include "check.hpp"
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

using namespace ctbrowser;

int main() {
    // Expected spellings also exercised through the VM in js/number_format.cpp.
    struct sample {
        double value;
        std::string_view text;
    };
    for (const auto [value, text] : {
             sample{1.0 / 3.0, "0.3333333333333333"},
             sample{1e20, "100000000000000000000"},
             sample{1e21, "1e+21"},
             sample{1e-6, "0.000001"},
             sample{1e-7, "1e-7"},
             sample{std::numeric_limits<double>::denorm_min(), "5e-324"},
             sample{std::numeric_limits<double>::max(), "1.7976931348623157e+308"},
             sample{-2.5, "-2.5"},
         }) {
        CHECK_EQ(number_to_string(value), text);
        CHECK_EQ(std::bit_cast<std::uint64_t>(string_to_number(number_to_string(value))),
                 std::bit_cast<std::uint64_t>(value));
    }
    CHECK_EQ(number_to_string(-0.0), "0");
    CHECK_EQ(number_to_string(std::numeric_limits<double>::infinity()), "Infinity");
    CHECK_EQ(number_to_string(-std::numeric_limits<double>::infinity()), "-Infinity");
    CHECK_EQ(number_to_string(std::numeric_limits<double>::quiet_NaN()), "NaN");
    CHECK_EQ(number_to_fixed(2.5, 0), "3");
    CHECK_EQ(number_to_fixed(1.005, 2), "1.00");
    CHECK_EQ(number_to_exponential(25, 0), "3e+1");
    CHECK_EQ(number_to_exponential(5, -1), "5e+0");
    CHECK_EQ(number_to_precision(1.5, 3), "1.50");

    CHECK_EQ(string_to_number(""), 0.0);
    CHECK_EQ(string_to_number("\xC2\xA0 +42\xEF\xBB\xBF"), 42.0);
    CHECK_EQ(string_to_number("0x10"), 16.0);
    CHECK_EQ(string_to_number("0o10"), 8.0);
    CHECK_EQ(string_to_number("0b10"), 2.0);
    CHECK_EQ(string_to_number("5.e3"), 5000.0);
    CHECK(std::signbit(string_to_number("-0")));
    CHECK(std::signbit(string_to_number("-1e-400")));
    CHECK(std::isinf(string_to_number("1e400")));
    for (const char * text : {"12abc", "-0x10", "+", "inf", "."}) {
        CHECK(std::isnan(string_to_number(text)));
    }
    CHECK_EQ(string_to_number_prefix("\xC2\xA0 3.14xyz"), 3.14);
    CHECK_EQ(string_to_number_prefix("0x10"), 0.0);
    CHECK(std::isnan(string_to_number_prefix("")));
    CHECK_EQ(out_of_range_value("1e-400"), 0.0);
    CHECK(std::isinf(out_of_range_value("1e400")));

    // Bootstrap M compares the input with Number(input).toString().
    for (std::string_view text : {"0", "NaN", "Infinity", "-Infinity", "1e+21", "0.000001"}) {
        CHECK_EQ(number_to_string(string_to_number(text)), text);
    }
    for (std::string_view text : {"-0", "01", "+1", "0x10", "1e21", "1e-6", " 42 ", "", "null"}) {
        CHECK(number_to_string(string_to_number(text)) != text);
    }
    CHECK(std::isnan(string_to_number(std::string_view{"1\0x", 3})));

    const std::string original = "\xC2\xA0 42 \xEF\xBB\xBF";
    const std::string_view trimmed = trim_js_space(original);
    CHECK_EQ(trimmed, "42");
    CHECK(trimmed.data() == original.data() + 3);
    REPORT("core_text");
}
