// Custom functions, CSS Functions and Mixins 1 §2: `@function` in a sheet and
// `--name()` calls in the cascade - parameters, defaults, typed slots, locals,
// the function's scope over the caller's, and how a call goes invalid.

#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include "style_fixture.hpp"

#include <string>
#include <string_view>

namespace {

void test_function_calls_in_the_cascade() {
    fixture f;
    f.load("<div id=outer><p id=a></p><p id=b></p></div>",
           "@property --n { syntax: '<number>'; inherits: true; initial-value: 0 }"
           "@property --w { syntax: '*'; inherits: false }"
           "@function --double(--x <length>) { result: calc(var(--x) * 2); }"
           "@function --pad(--a, --b: 4px) returns <length> {"
           "  --sum: calc(var(--a) + var(--b));"
           "  result: var(--sum);"
           "}"
           "@function --outer-read() { result: var(--scale) var(--missing, none); }"
           "@function --typed(--x <number>: 7) returns <number> { result: var(--x); }"
           "@function --nested(--x <length>) { result: --double(--double(var(--x))); }"
           "div { --scale: 3 }"
           "#a { width: --double(10px); height: --pad(1px); padding-left: --pad(1px, 2em);"
           "     font-size: 10px; --n: --typed(); --w: --outer-read(); margin-left: --nested(2px) }"
           "#b { width: --double(); height: --double(1px, 2px); padding-left: --pad(3);"
           "     margin-left: --unknown(1px); --n: --typed(red) }");
    const node_id a = f.find_id("a");
    const node_id b = f.find_id("b");
    // An untyped result is a token stream the property reads: a calc() the
    // cascade folds like any other.
    expect_value(f, a, "width", "20px", "--double(10px)");
    // A default, a local, a typed result, and an `em` against the element.
    expect_value(f, a, "height", "5px", "--pad(1px) takes the default");
    expect_value(f, a, "padding-left", "21px", "--pad(1px, 2em) against 10px");
    expect_value(f, a, "font-size", "10px", "font-size stays");
    expect_value(f, a, "--n", "7", "a typed default");
    // The function's scope sees the caller's custom properties after its own,
    // and a var() fallback works inside it.
    expect_value(f, a, "--w", "3 none", "the caller's --scale from inside the body");
    expect_value(f, a, "margin-left", "8px", "a call inside a call");
    // Invalid calls: a missing argument with no default, too many arguments,
    // an argument that does not parse as the parameter's type, an unknown
    // function, and a typed default overridden by a value of the wrong type -
    // each is invalid at computed-value time, which is `unset`.
    expect_value(f, b, "width", "", "--double() has no argument");
    expect_value(f, b, "height", "", "--double(1px, 2px) has one too many");
    expect_value(f, b, "padding-left", "", "--pad(3): 3 is not a <length>");
    expect_value(f, b, "margin-left", "", "--unknown() is no function");
    expect_value(f, b, "--n", "0", "--typed(red): the initial value");
}

// A sheet's functions leave with the sheet, unlike a registration.
void test_functions_belong_to_their_sheet() {
    fixture f;
    f.load("<p id=a></p>", "@function --f() { result: 1px; } p { width: --f() }");
    expect_value(f, f.find_id("a"), "width", "1px", "before the sheet is replaced");
    f.styles.clear_origin(1);
    f.styles.add_sheet("@function --f() { result: 2px; } p { width: --f() }", 1);
    {
        const auto txn = f.doc.read();
        f.resolved = f.styles.resolve_all(txn);
    }
    expect_value(f, f.find_id("a"), "width", "2px", "the replacement's --f");
}

} // namespace

int main() {
    test_function_calls_in_the_cascade();
    test_functions_belong_to_their_sheet();
    REPORT("style_custom_functions");
}
