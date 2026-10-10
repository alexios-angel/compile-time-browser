//===- activation.cpp - Label activation tests ------------------*- C++ -*-===//
//
// A <label>'s activation behaviour (HTML 4.10.4): it leaves a click aimed at
// its interactive content alone, and it clicks its labeled control, which a
// form-associated custom element can be.
//
// Reduced from WPT's clicking-interactive-content.html and
// ElementInternals-labels.html.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser.hpp>

#include "check.hpp"

#include <string>
#include <vector>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

/// Evaluates `expression` on a page holding one empty <label id=label> and
/// returns what it logged, or "threw:<name>".
[[nodiscard]] std::string answer(const std::string & expression) {
    browser page{browser_options{400, 300}};
    page.load_html("<!DOCTYPE html><html><body><label id=label></label></body></html>");
    const std::string source = "try { console.log(String(" + expression +
                               ")); } catch (e) { console.log('threw:' + e.name); }";
    if (!page.run_script(source)) { return "<did not run: " + page.script_error() + ">"; }
    const std::vector<std::string> & logged = page.bindings().console_output();
    return logged.empty() ? "<nothing logged>" : logged.back();
}

/// Checks `answer(expression)` against `expected`.
void is(const std::string & expression, const std::string & expected) {
    CHECK_EQ(answer(expression), expected);
}

void test_a_label_leaves_its_interactive_content_alone() {
    // A <textarea> has no activation behaviour, so the label is its click's
    // activation target - and must not then click the button it labels; a
    // <div> is not interactive, and a click on it is the label's.
    is("(function () { var l = document.getElementById('label'), n = 0;"
       " var b = l.appendChild(document.createElement('button'));"
       " var t = l.appendChild(document.createElement('textarea'));"
       " b.addEventListener('click', function () { ++n; });"
       " t.click(); t.appendChild(document.createElement('span')).click();"
       " var d = l.appendChild(document.createElement('div')); d.click();"
       " return n; })()",
       "1");
}

void test_a_label_clicks_a_form_associated_custom_element() {
    is("(function () {"
       " class C extends HTMLElement { static get formAssociated() { return true; } }"
       " customElements.define('my-control', C);"
       " document.body.insertAdjacentHTML('beforeend',"
       "  '<label for=mc id=l2></label><form><my-control id=mc></my-control></form>');"
       " var n = 0; document.getElementById('mc').addEventListener('click',"
       "  function () { ++n; });"
       " document.getElementById('l2').click(); return n; })()",
       "1");
}

} // namespace

int main() {
    test_a_label_leaves_its_interactive_content_alone();
    test_a_label_clicks_a_form_associated_custom_element();
    REPORT("forms_activation");
}
