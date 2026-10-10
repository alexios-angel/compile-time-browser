// The insertion and attribute change steps of radio buttons and options
// (bindings/element/control_methods/form_writes.cpp): the last checked radio
// of a group wins, joining a group by a type or name change unchecks the
// others, a clean option follows its `selected` attribute, and an inserted
// selected option deselects the rest of a single select.

#include <ctbrowser.hpp>

#include "check.hpp"
#include "dom_probe.hpp"

#include <string>

namespace {

void test_radios() {
    // The parser's two checked radios: the second one's connection unchecks
    // the first.
    ctbrowser_test::is_in(R"(<!DOCTYPE html><body><input type=radio name=g id=a checked>
<input type=radio name=g id=b checked></body>)",
                          "[a.checked, b.checked].join()", "false,true");
    // The `checked` attribute given to a clean radio sets its checkedness,
    // which unchecks the rest of its group.
    ctbrowser_test::is_in(R"(<!DOCTYPE html><body><input type=radio name=g id=a checked>
<input type=radio name=g id=b></body>)",
                          "(b.setAttribute('checked', ''), [a.checked, b.checked].join())",
                          "false,true");
    // radio-morphed.html: a checked text input turned radio joins the group.
    ctbrowser_test::is_in(R"(<!DOCTYPE html><body><input id=r type=radio name=n checked>
<input id=t name=n checked></body>)",
                          "(t.type = 'radio', [r.checked, t.checked].join())", "false,true");
    // Into a detached, formless tree two checked radios stay checked; into a
    // detached FORM the second one's new form owner unchecks the first.
    ctbrowser_test::is_in(
        "<!DOCTYPE html><body></body>",
        "(function () { function r() { var i = document.createElement('input');"
        " i.type = 'radio'; i.name = 'g'; i.checked = true; return i; }"
        " var d = document.createElement('div'), f = document.createElement('form');"
        " var a = d.appendChild(r()), b = d.appendChild(r());"
        " var c = f.appendChild(r()), e = f.appendChild(r());"
        " return [a.checked, b.checked, c.checked, e.checked].join(); })()",
        "true,true,false,true");
    // innerHTML copies each radio's attributes in before the radio itself: a
    // detached, formless container keeps both checked.
    ctbrowser_test::is_in("<!DOCTYPE html><body></body>",
                          "(function () { var d = document.createElement('div');"
                          " d.innerHTML = '<input type=radio name=g checked>"
                          "<input type=radio name=g checked>';"
                          " return [d.children[0].checked, d.children[1].checked].join(); })()",
                          "true,true");
}

void test_options() {
    // inserted-or-removed.html: an option inserted selected is the one
    // selected, wherever it goes.
    ctbrowser_test::is_in(
        R"(<!DOCTYPE html><body><select id=s><option>A<option selected>C<option>D</select></body>)",
        "(function () { var o = document.createElement('option'); o.selected = true;"
        " o.textContent = 'B'; s.insertBefore(o, s.options[0]);"
        " return s.selectedOptions[0].textContent; })()",
        "B");
    // The first option a select chose for itself keeps its selectedness when
    // it leaves; the next one is chosen in its place.
    ctbrowser_test::is_in(
        R"(<!DOCTYPE html><body><select><option id=a>A<option id=b>B</select></body>)",
        "(function () { var o = [a.selected, b.selected]; document.body.appendChild(a);"
        " o.push(a.selected, b.selected); return o.join(); })()",
        "true,false,true,true");
    // option-element-constructor.html: the constructor leaves the option
    // clean, so its selectedness follows the attribute afterwards.
    ctbrowser_test::is_in("<!DOCTYPE html><body></body>",
                          "(function () { var o = new Option('t', 'v', false, true);"
                          " var out = [o.selected]; o.setAttribute('selected', '');"
                          " out.push(o.selected); o.removeAttribute('selected');"
                          " out.push(o.selected, new Option('t', 'v', true, false).selected);"
                          " return out.join(); })()",
                          "true,true,false,false");
    // Removing a clean option's `selected` attribute re-settles its select,
    // which selects the first enabled option.
    ctbrowser_test::is_in(
        R"(<!DOCTYPE html><body><select id=s><option>A<option selected>B</select></body>)",
        "(s.options[1].removeAttribute('selected'), s.selectedIndex)", "0");
}

// UAF-021: clicking a radio BELOW the checked one in a big group. The toggle's
// walk creates the state of every radio it has not seen, which rehashes the
// store - and the clicked radio's own state, looked up before the walk, must
// not be written through a reference the rehash moved.
void test_a_click_in_a_big_group_checks_the_clicked_radio() {
    std::string html = "<!DOCTYPE html><body><input type=radio name=g id=first checked>";
    for (int i = 0; i < 300; ++i) { html += "<input type=radio name=g>"; }
    html += "<input type=radio name=g id=last></body>";
    ctbrowser_test::is_in(html,
                          "(last.click(), [first.checked, last.checked,"
                          " document.querySelectorAll('input:checked').length].join())",
                          "false,true,1");
}

} // namespace

int main() {
    test_radios();
    test_a_click_in_a_big_group_checks_the_clicked_radio();
    test_options();
    REPORT("forms_insertion_steps");
}
