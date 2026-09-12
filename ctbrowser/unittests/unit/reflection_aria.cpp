// THE ENUMERATED ARIA ATTRIBUTES - twenty of the `aria-*` rows in the
// reflection table (lib/Shell/bindings/element/reflection.cpp) that w3c/aria#2484
// turned from a nullable DOMString into a nullable ENUMERATED attribute:
// `ariaBusy`, `ariaChecked`, `ariaLive` and the rest. What this pins down is
// the rule's three defaults per row, each of which may be a keyword or null:
// the getter folds a keyword, a non-keyword reads the invalid value default,
// and an absent attribute reads the missing value default - while the content
// attribute keeps exactly what was written.
//
// `aria-attribute-reflection-enumerated.tentative.html` is the corpus file, and
// unit/element_attrs.cpp holds the nullable-DOMString half of the same table.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <cstdio>
#include <string>
#include <vector>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

[[nodiscard]] std::string answer(const std::string & expression) {
    browser page{browser_options{400, 300}};
    page.load_html("<!DOCTYPE html><html><body><script>try { console.log(String(" + expression +
                   ")); } catch (e) { console.log('threw:' + e.name); }</script></body></html>");
    const std::vector<std::string> & logged = page.bindings().console_output();
    if (logged.empty()) { return "<nothing logged: " + page.script_error() + ">"; }
    return logged.back();
}

void is(const std::string & expression, const std::string & expected) {
    const std::string got = answer(expression);
    CHECK_EQ(got, expected);
    if (got != expected) { std::printf("    %s\n", expression.c_str()); }
}

void test_the_missing_value_default_is_a_keyword_or_null_per_row() {
    is(R"JS((function () {
        var e = document.createElement('div');
        return e.ariaBusy + ',' + e.ariaAtomic + ',' + e.ariaLive + ',' + e.ariaChecked;
    })())JS",
       "false,null,off,null");
}

void test_a_keyword_folds_and_anything_else_reads_the_invalid_value_default() {
    is(R"JS((function () {
        var e = document.createElement('div');
        e.setAttribute('aria-busy', 'TRUE');
        e.setAttribute('aria-atomic', 'maybe');
        e.setAttribute('aria-checked', 'maybe');
        e.setAttribute('aria-current', 'nope');
        e.setAttribute('aria-live', '');
        e.ariaSort = 'Descending';
        return e.ariaBusy + ',' + e.ariaAtomic + ',' + e.ariaChecked + ',' + e.ariaCurrent + ',' +
               e.ariaLive + ',' + e.ariaSort + '|' + e.getAttribute('aria-busy') + ',' +
               e.getAttribute('aria-sort');
    })())JS",
       "true,false,null,true,off,descending|TRUE,Descending");
}

void test_null_still_removes_the_attribute() {
    // The stable aria-attribute-reflection.html's `testNullable`, which the
    // enumerated rows must keep passing: null and undefined both remove.
    is(R"JS((function () {
        var e = document.createElement('div');
        e.ariaHidden = 'true';
        e.ariaHidden = null;
        var a = e.hasAttribute('aria-hidden') + ',' + e.ariaHidden;
        e.ariaHidden = 'true';
        e.ariaHidden = undefined;
        return a + ',' + e.hasAttribute('aria-hidden');
    })())JS",
       "false,false,false");
}

} // namespace

int main() {
    test_the_missing_value_default_is_a_keyword_or_null_per_row();
    test_a_keyword_folds_and_anything_else_reads_the_invalid_value_default();
    test_null_still_removes_the_attribute();
    REPORT("reflection_aria");
}
