//===- ctcompile/test/Runtime/NativeJson.cpp - JSON, replace and rows -----===//
//
// Tests the runtime helpers css-dump.js lowers to: ctnative::json_record and
// JSON.stringify of a record and of a dictionary (CTNative/Runtime/Json.hpp),
// String.prototype.replace with a String replacement (RegExp.hpp), and
// ctnative::vec_row (Array.hpp). The cases pin what differs from a plain C++
// rendering: NaN and the infinities serialize as null and -0 as 0, array-index
// keys of a dictionary come first, `$` patterns follow GetSubstitution, and a
// row past the end throws the TypeError a property read of undefined throws.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTNative/Runtime/Array.hpp"
#include "ctcompile/CTNative/Runtime/Dictionary.hpp"
#include "ctcompile/CTNative/Runtime/Json.hpp"
#include "ctcompile/CTNative/Runtime/RegExp.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "check.hpp"

namespace {

using ctnative::js_string;
using number = ctnative::js_num;

js_string text(const char * value) {
    return js_string{std::string{value}};
}

void test_record_serializes_in_definition_order() {
    const auto out = ctnative::JSON.stringify(ctnative::json_record(
        "v", number{3.0}, "nan", number{std::numeric_limits<double>::quiet_NaN()}, "inf",
        number{std::numeric_limits<double>::infinity()}, "neg", number{-0.0}, "s", text("a\"b\n"),
        "ok", ctnative::js_boolean_t{true}, "d", 0.5));
    CHECK(out.value() == R"({"v":3,"nan":null,"inf":null,"neg":0,"s":"a\"b\n","ok":true,"d":0.5})");
}

void test_dictionary_puts_indices_first() {
    const auto table = ctnative::make_dictionary<ctnative::number_string>();
    table->set("b", ctnative::number_string{number{1.0}});
    table->set("10", ctnative::number_string{text("x")});
    table->set("2", ctnative::number_string{number{2.5}});
    CHECK(ctnative::JSON.stringify(table).value() == R"({"2":2.5,"10":"x","b":1})");
    CHECK(ctnative::JSON.stringify(ctnative::make_dictionary<number>()).value() == "{}");
}

std::string replaced(const char * subject, const char * pattern, const char * flags,
                     const char * replacement) {
    return ctnative::String.prototype.replace
        .call(text(subject), ctnative::RegExp(pattern, flags), text(replacement))
        .value();
}

void test_replace_substitutes() {
    // css-dump.js's trim of a line's trailing CR and spaces: `$` is the end of
    // the input only, so the space before the newline stays.
    CHECK(replaced("a \nb \r ", "[\\r ]+$", "", "") == "a \nb");
    CHECK(replaced("aXbX", "X", "g", "-") == "a-b-");
    CHECK(replaced("abc", "(b)", "", "[$1|$&|$`|$'|$$|$2|$0|$01|$]") == "a[b|b|a|c|$|$2|$0|b|$]c");
}

void test_row_past_the_end_throws() {
    const std::vector<std::pair<js_string, number>> rows{{text("a"), number{1.0}}};
    CHECK(std::get<1>(ctnative::vec_row(rows, number{0.0}, 1)).value() == 1.0);
    // The interpreter's message for `rows[1][1]`.
    std::string message;
    try {
        (void)ctnative::vec_row(rows, number{1.0}, 1);
    } catch (const ctnative::type_error & thrown) { message = thrown.what(); }
    CHECK(message == "Cannot read properties of undefined (reading '1')");
    message.clear();
    try {
        (void)ctnative::vec_row(rows, number{-1.0}, 0);
    } catch (const ctnative::type_error & thrown) { message = thrown.what(); }
    CHECK(message == "Cannot read properties of undefined (reading '0')");
}

} // namespace

int main() {
    test_record_serializes_in_definition_order();
    test_dictionary_puts_indices_first();
    test_replace_substitutes();
    test_row_past_the_end_throws();
    REPORT("native_json");
}
