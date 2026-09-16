// The Selection API: one selection per window, a range and a direction.
//
// The regression net for lib/Shell/bindings/selection.cpp. What it pins down
// is the model: `getSelection()` is the SAME object every time, the selection
// holds the caller's own Range (so `getRangeAt(0) === range`), the anchor
// follows the direction, and a document with no browsing context has no
// selection at all.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include <string>
#include <string_view>

using namespace ctbrowser;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

[[nodiscard]] std::string said(std::string_view html) {
    browser page{browser_options{200, 100}};
    page.load_html(html);
    std::string out;
    for (const std::string & one : page.alerts()) {
        if (!out.empty()) { out += ';'; }
        out += one;
    }
    if (!page.script_error().empty()) { out += "|error:" + page.script_error(); }
    return out;
}

void test_one_selection_per_window() {
    CHECK_EQ(said("<html><body><script>"
                  "alert(getSelection() === window.getSelection());"
                  "alert(getSelection() === document.getSelection());"
                  "alert(getSelection() instanceof Selection);"
                  "alert(getSelection().rangeCount);"
                  "alert(getSelection().type);"
                  "alert(getSelection().anchorNode);"
                  "alert(getSelection().isCollapsed);"
                  "alert(getSelection().toString());"
                  "alert(document.implementation.createHTMLDocument('').getSelection());"
                  "</script></body></html>"),
             "true;true;true;0;None;null;true;;null");
}

void test_add_range_keeps_the_callers_range() {
    CHECK_EQ(said("<html><body><p id=p>hello</p><script>"
                  "var t = document.getElementById('p').firstChild;"
                  "var r = document.createRange();"
                  "r.setStart(t, 1); r.setEnd(t, 4);"
                  "var s = getSelection();"
                  "s.addRange(r);"
                  "alert(s.rangeCount);"
                  "alert(s.getRangeAt(0) === r);"
                  "alert(s.anchorNode === t);"
                  "alert(s.anchorOffset);"
                  "alert(s.focusOffset);"
                  "alert(s.type);"
                  "alert(s.toString());"
                  "s.removeAllRanges();"
                  "alert(s.rangeCount);"
                  "</script></body></html>"),
             "1;true;true;1;4;Range;ell;0");
}

void test_collapse_and_extend_carry_the_direction() {
    CHECK_EQ(said("<html><body><p id=p>hello</p><script>"
                  "var t = document.getElementById('p').firstChild;"
                  "var s = getSelection();"
                  "s.collapse(t, 3);"
                  "alert(s.anchorOffset + ',' + s.focusOffset + ',' + s.type);"
                  "s.extend(t, 5);"
                  "alert(s.anchorOffset + ',' + s.focusOffset + ',' + s.direction);"
                  "s.extend(t, 1);"
                  "alert(s.anchorOffset + ',' + s.focusOffset + ',' + s.direction);"
                  "alert(s.toString());"
                  "s.selectAllChildren(document.getElementById('p'));"
                  "alert(s.toString() + ',' + s.rangeCount + ',' + s.focusOffset);"
                  "alert(s.containsNode(t, true));"
                  "s.collapseToStart();"
                  "alert(s.isCollapsed);"
                  "</script></body></html>"),
             "3,3,Caret;3,5,forward;3,1,backward;el;hello,1,1;true;true");
}

void test_what_the_selection_refuses() {
    CHECK_EQ(said("<html><body><p id=p>hello</p><script>"
                  "function threw(f){ try { f(); return 'ok'; } catch (e) { return e.name; } }"
                  "var s = getSelection();"
                  "alert(threw(function(){ s.extend(document.body, 0); }));"
                  "alert(threw(function(){ s.getRangeAt(0); }));"
                  // A node in a detached fragment is not in this document: the
                  // call is ABORTED, not an exception, and nothing is selected.
                  "var loose = document.createElement('div');"
                  "s.collapse(loose, 0);"
                  "alert(s.rangeCount);"
                  "alert(threw(function(){ s.collapse(document.getElementById('p'), 9); }));"
                  "s.collapse(null);"
                  "alert(s.rangeCount + ',' + s.direction);"
                  "</script></body></html>"),
             "InvalidStateError;IndexSizeError;0;IndexSizeError;0,none");
}

} // namespace

int main() {
    test_one_selection_per_window();
    test_add_range_keeps_the_callers_range();
    test_collapse_and_extend_carry_the_direction();
    test_what_the_selection_refuses();
    REPORT("selection");
}
