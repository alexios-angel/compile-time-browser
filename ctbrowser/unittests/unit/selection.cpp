//===- ctbrowser/unittests/unit/selection.cpp - Selection API tests -------===//
//
// The Selection API: one selection per window, a range and a direction.
//
// The regression tests for lib/Shell/bindings/selection.cpp. They pin down the
// model: `getSelection()` is the same object every time, the selection holds
// the caller's own Range (so `getRangeAt(0) === range`), the anchor follows
// the direction, and a document with no browsing context has no selection at
// all.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include "dom_probe.hpp"
#include <string>
#include <string_view>

using namespace ctbrowser;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser_test::said;

namespace {

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
                  // A clone is a live range too, and the selection takes it
                  // (selection/collapse-*.html add theirs that way).
                  "var c = r.cloneRange(); s.addRange(c);"
                  "alert(s.rangeCount + ',' + (s.getRangeAt(0) === c));"
                  "</script></body></html>"),
             "1;true;true;1;4;Range;ell;0;1,true");
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
    CHECK_EQ(
        said("<html><body><p id=p>hello</p><script>"
             "function threw(f){ try { f(); return 'ok'; } catch (e) { return e.name; } }"
             "var s = getSelection();"
             "alert(threw(function(){ s.extend(document.body, 0); }));"
             "alert(threw(function(){ s.getRangeAt(0); }));"
             // A detached node is not in this document's tree: the call is
             // silently ignored, not an exception, and nothing is selected.
             "var loose = document.createElement('div');"
             "s.collapse(loose, 0);"
             "alert(s.rangeCount);"
             "alert(threw(function(){ s.collapse(document.getElementById('p'), 9); }));"
             "s.collapse(null);"
             "alert(s.rangeCount + ',' + s.direction);"
             // WebIDL's argument conversion: a Range must be a Range, and
             // setBaseAndExtent's four arguments are all required.
             "alert(threw(function(){ s.removeRange(s); }) + ',' + threw(function(){"
             " s.addRange(new StaticRange({startContainer: document.body, startOffset: 0,"
             " endContainer: document.body, endOffset: 0})); }) + ',' + threw(function(){"
             " s.setBaseAndExtent(document.body, 0, document.body); }));"
             "</script></body></html>"),
        "InvalidStateError;IndexSizeError;0;IndexSizeError;0,none;TypeError,TypeError,TypeError");
}

/// A selection across a shadow boundary: the anchor inside a closed tree and
/// the focus outside it. The direction comes from the shadow-including order,
/// the selection is collapsed where its range is, and getComposedRanges()
/// rescopes the inner point to the host unless its tree is named, and refuses
/// a list that holds anything but ShadowRoots.
void test_a_selection_across_a_shadow_boundary() {
    CHECK_EQ(said("<html><body><div id=c>a<div id=h></div>b</div><script>"
                  "function threw(f){ try { f(); return 'ok'; } catch (e) { return e.name; } }"
                  "var c = document.getElementById('c'), h = document.getElementById('h');"
                  "var sr = h.attachShadow({mode: 'closed'}); sr.innerHTML = 'hello, world';"
                  "var s = getSelection();"
                  "s.setBaseAndExtent(sr.firstChild, 7, c, 2);"
                  "var r = s.getComposedRanges()[0];"
                  "alert(s.direction + ',' + s.isCollapsed + ',' + (r.startContainer === c) + ','"
                  " + r.startOffset + ',' + (r.endContainer === c) + ',' + r.endOffset);"
                  "var inner = s.getComposedRanges({shadowRoots: [sr]})[0];"
                  "alert((inner.startContainer === sr.firstChild) + ',' + inner.startOffset + ','"
                  " + inner.endOffset);"
                  "s.setBaseAndExtent(sr.firstChild, 7, c, 1);"
                  "alert(s.direction);"
                  "alert(threw(function(){ s.getComposedRanges({shadowRoots: [c]}); }));"
                  "</script></body></html>"),
             "forward,true,true,1,true,2;true,7,2;backward;TypeError");
}

} // namespace

int main() {
    test_one_selection_per_window();
    test_add_range_keeps_the_callers_range();
    test_collapse_and_extend_carry_the_direction();
    test_what_the_selection_refuses();
    test_a_selection_across_a_shadow_boundary();
    REPORT("selection");
}
