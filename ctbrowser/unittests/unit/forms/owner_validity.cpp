//===- owner_validity.cpp - Form owner and validity tests -------*- C++ -*-===//
//
// Two answers the forms give outside a script: a control's form owner when
// the parser's form element pointer made it (HTML 13.2.6.1, 4.10.18.3), where
// a form a table popped at once still owns the controls after it; and what
// :valid and :invalid match (4.16.3), dom_bindings::control_validity.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser.hpp>

#include "check.hpp"
#include "dom_probe.hpp"

#include <optional>
#include <string>

namespace {

void test_parser_form_owner() {
    // form_owner_and_table.html: `<form>` in a table is popped at once, and
    // the input in the cell and the one after the table - whose own `<form>`
    // the parser ignored, the pointer being set - are its controls, before
    // and after the whole subtree leaves the document.
    const char * page = R"(<!DOCTYPE html><body><div id=root><form id=form1></form>
<table><form id=form2><tr><td><input id=input1></td></tr>
<tr><td><input id=input2 form=form1></td></tr></table>
<form id=form3><input id=input3></form></div></body>)";
    ctbrowser_test::is_in(
        page,
        "(function () { var f2 = document.getElementById('form2'), f1 ="
        " document.getElementById('form1'), i1 = document.getElementById('input1'),"
        " i2 = document.getElementById('input2'), i3 = document.getElementById('input3');"
        " var o = [i1.form === f2, i2.form === f1, i3.form === f2,"
        " document.getElementById('form3') === null, f2.elements.length];"
        " var root = document.getElementById('root'); root.remove();"
        " o.push(i1.form === f2, i2.form, i3.form === f2);"
        " return o.join(); })()",
        "true,true,true,true,2,true,,true");
    // Taking the control out of the form's tree ends the association, and
    // putting it back does not restore it (form_owner_and_table_3.html).
    ctbrowser_test::is_in(page,
                          "(function () { var i1 = document.getElementById('input1');"
                          " var home = i1.parentNode; i1.remove(); var o = [i1.form];"
                          " home.appendChild(i1); o.push(i1.form); return o.join(); })()",
                          ",");
}

void test_control_validity() {
    ctbrowser::browser page{ctbrowser::browser_options{400, 300}};
    page.load_html(R"(<!DOCTYPE html><body>
<form id=f><input id=a required><input id=b value=ok></form>
<form id=g><input id=h value=fine><input id=i disabled required></form>
<fieldset id=fs><input id=c pattern="[0-9]+" value=x></fieldset>
<fieldset id=empty></fieldset><div id=d></div></body>)");
    auto & b = page.bindings();
    const auto of = [&](const char * id) {
        return b.control_validity(ctbrowser_test::find_id(page, id));
    };
    CHECK(of("a") == std::optional<bool>{false});
    CHECK(of("b") == std::optional<bool>{true});
    CHECK(of("f") == std::optional<bool>{false});
    CHECK(of("g") == std::optional<bool>{true});
    CHECK(!of("i").has_value()); // barred: disabled
    CHECK(of("c") == std::optional<bool>{false});
    CHECK(of("fs") == std::optional<bool>{false});
    CHECK(of("empty") == std::optional<bool>{true});
    CHECK(!of("d").has_value());
}

// A pattern whose match runs out of the matcher's depth or step budget is a
// mismatch, not a stack overflow (a backtracking matcher recurses once per
// character on `(?:a|b)+`).
void test_pattern_budget() {
    ctbrowser::browser page{ctbrowser::browser_options{400, 300}};
    page.load_html("<!DOCTYPE html><body><input id=deep pattern=\"(?:a|b)+\" value=" +
                   std::string(200000, 'a') +
                   "><input id=exp pattern=\"(a+)+\" value=" + std::string(40, 'a') +
                   "b><input id=ok pattern=\"(?:a|b)+\" value=" + std::string(100, 'b') +
                   "></body>");
    auto & b = page.bindings();
    const auto of = [&](const char * id) {
        return b.control_validity(ctbrowser_test::find_id(page, id));
    };
    CHECK(of("deep") == std::optional<bool>{false});
    CHECK(of("exp") == std::optional<bool>{false});
    CHECK(of("ok") == std::optional<bool>{true});
}

} // namespace

int main() {
    test_parser_form_owner();
    test_control_validity();
    test_pattern_budget();
    REPORT("forms_owner_validity");
}
