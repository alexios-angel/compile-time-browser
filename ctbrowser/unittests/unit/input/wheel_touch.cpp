// THE WHEEL, A FINGER AND THE END OF A SCROLL, through the browser the way a
// driver sends them: a wheel event's cancelability follows its listeners'
// passivity (UI Events), `mousewheel` is the legacy name a trusted `wheel`
// reaches, a notch scrolls the nearest scroll container that can move, a touch
// is a TouchEvent after its touch PointerEvent, and every completed scroll
// ends in `scrollend` (CSSOM View §13.1). Each case names the dom/events file
// it stands in for.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

#include "../cssom/probe.hpp"
#include "check.hpp"

#include <string>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser::shell::input_event;
using ctbrowser_test::logged;

namespace {

constexpr const char * page_source = R"(<!doctype html><html><head><style>
    body { margin: 0; height: 2000px }
    #box { width: 200px; height: 100px; overflow: scroll }
    #inner { height: 1000px }
    </style></head><body><div id=box><div id=inner></div></div><p id=text>text</p>
    <script>
    var seen = [];
    function note(e) {
        seen.push(e.type + ':' + (e.target.id || e.target.nodeName) + ':' + e.cancelable);
    }
    </script></body></html>)";

void settle(browser & page) {
    page.frame();
    (void)page.tick(1);
    page.frame();
}

[[nodiscard]] std::string seen(browser & page) {
    (void)page.run_script("console.log('seen=' + seen.join(' ')); seen = [];");
    std::string last;
    for (const std::string & line : page.bindings().console_output()) {
        if (line.starts_with("seen=")) { last = line; }
    }
    return last;
}

// non-cancelable-when-passive/{passive,non-passive}-{wheel,mousewheel}-*: a
// wheel event is cancelable only when a non-passive listener could cancel it,
// and a `mousewheel` listener hears a trusted wheel nobody listened for.
void test_wheel_cancelability_and_mousewheel() {
    browser page{browser_options{400, 300}};
    page.load_html(page_source);
    page.frame();
    CHECK_EQ(page.script_error(), std::string{});

    (void)page.run_script("window.addEventListener('wheel', note, {once: true});");
    (void)page.handle(input_event::wheel_at(300, 200, -1));
    CHECK_EQ(seen(page), std::string{"seen=wheel:BODY:false"});

    (void)page.run_script("window.addEventListener('wheel', note, {once: true, passive: false});");
    (void)page.handle(input_event::wheel_at(300, 200, -1));
    CHECK_EQ(seen(page), std::string{"seen=wheel:BODY:true"});

    (void)page.run_script("document.body.addEventListener('mousewheel', note, {once: true});");
    (void)page.handle(input_event::wheel_at(300, 200, -1));
    CHECK_EQ(seen(page), std::string{"seen=mousewheel:BODY:false"});

    // wheel-event-composed: a WheelEvent, composed, deltaY positive for a
    // notch towards the user.
    (void)page.run_script(
        "addEventListener('wheel', function (e) {"
        "  seen.push((e instanceof WheelEvent) + ',' + e.composed + ',' + e.deltaY);"
        "}, {once: true});");
    (void)page.handle(input_event::wheel_at(300, 200, -1));
    CHECK_EQ(seen(page), std::string{"seen=true,true,100"});
}

// wheel-event-transactions-basic, scrollend-event-fired-for-programmatic-
// scroll: a notch over a scroll container scrolls IT, not the page; one
// anywhere else scrolls the page; each completed scroll ends in a scrollend
// that bubbles from the document and not from an element.
void test_wheel_scrolls_the_container_and_scrollend_follows() {
    browser page{browser_options{400, 300}};
    page.load_html(page_source);
    page.frame();
    (void)page.run_script("box.addEventListener('scrollend', note);"
                          "document.addEventListener('scrollend', function (e) {"
                          "  seen.push('scrollend:document:' + e.bubbles); });");

    (void)page.handle(input_event::wheel_at(50, 50, -1));
    check(page.scroll_y() == 0, "a notch over the box leaves the page where it was");
    (void)page.run_script("console.log('box=' + (box.scrollTop > 0));");
    CHECK_EQ(logged(page, "box="), std::string{"box=true"});
    settle(page);
    CHECK_EQ(seen(page), std::string{"seen=scrollend:box:false"});

    (void)page.handle(input_event::wheel_at(300, 250, -1));
    check(page.scroll_y() > 0, "a notch past the box scrolls the page");
    settle(page);
    CHECK_EQ(seen(page), std::string{"seen=scrollend:document:true"});

    // scrollend-event-not-fired-on-no-scroll: nothing moved, nothing ends.
    (void)page.run_script("box.scrollTo(0, box.scrollTop);");
    settle(page);
    CHECK_EQ(seen(page), std::string{"seen="});
}

// non-cancelable-when-passive/*-touch{start,move}-*: a finger is a touch
// PointerEvent and then a TouchEvent at the element it went down on, the
// touch events cancelable only for a non-passive listener; the lift empties
// `touches` and keeps the finger in `changedTouches`.
void test_a_touch_is_pointer_then_touch_events() {
    browser page{browser_options{400, 300}};
    page.load_html(page_source);
    page.frame();
    (void)page.run_script(
        "text.addEventListener('pointerdown', function (e) { seen.push(e.pointerType); });"
        "window.addEventListener('touchstart', note);"
        "window.addEventListener('touchmove', note, {passive: false});"
        "window.addEventListener('touchend', function (e) {"
        "  seen.push(e.touches.length + '/' + e.changedTouches.length + '/' +"
        "            (e.changedTouches[0].target === text) + '/' + (e instanceof TouchEvent));"
        "});");
    (void)page.touch("start", 5, 125);
    (void)page.touch("move", 300, 250);
    (void)page.touch("end", 300, 250);
    CHECK_EQ(seen(page),
             std::string{"seen=touch touchstart:text:false touchmove:text:true 0/1/true/true"});
}

// scrollend-event-fires-for-repeat-key-ending-after-scroll-container-end-is-
// reached: the scrolling keys move the focused scroller, not the page.
void test_keys_scroll_the_focused_container() {
    browser page{browser_options{400, 300}};
    page.load_html(page_source);
    page.frame();
    (void)page.run_script("box.tabIndex = 0; box.focus();");
    (void)page.handle(input_event::key_press("PageDown"));
    (void)page.handle(input_event::key_press("End"));
    (void)page.run_script("console.log('keys=' + (box.scrollTop === box.scrollHeight - "
                          "box.clientHeight));");
    CHECK_EQ(logged(page, "keys="), std::string{"keys=true"});
    check(page.scroll_y() == 0, "and the page stays where it was");
}

// html/semantics/forms/the-input-element/selection-pointer.html,
// selection/selection-direction-on-single-click.html: a press on text collapses
// the page's Selection there - directionless - and a drag extends it.
void test_a_drag_selects_in_the_page_selection() {
    browser page{browser_options{400, 300}};
    page.load_html(page_source);
    page.frame();
    (void)page.handle(input_event::mouse_move_to(1, 125));
    (void)page.handle(input_event::mouse_down_at(1, 125));
    (void)page.run_script("var s = getSelection(); console.log('pressed=' + (s.anchorNode === "
                          "text.firstChild) + ',' + s.isCollapsed + ',' + s.direction);");
    CHECK_EQ(logged(page, "pressed="), std::string{"pressed=true,true,none"});
    (void)page.handle(input_event::mouse_move_to(300, 125));
    (void)page.handle(input_event::mouse_up_at(300, 125));
    (void)page.run_script("console.log('dragged=' + (s.focusNode === text.firstChild) + ',' + "
                          "(s.focusOffset > s.anchorOffset) + ',' + s.direction);");
    CHECK_EQ(logged(page, "dragged="), std::string{"dragged=true,true,forward"});
}

// The painted selection counts the box's text, where "Hello\n     world" shows
// as "Hello world"; the page's Selection counts the node's own. A press past
// the d is offset 16 of the node, not 11.
void test_a_press_counts_the_node_text_not_the_folded_box() {
    browser page{browser_options{400, 300}};
    page.load_html("<!doctype html><body style='margin:0'>"
                   "<p id=p style='margin:0'>Hello\n     world</p>");
    page.frame();
    (void)page.handle(input_event::mouse_move_to(390, 8));
    (void)page.handle(input_event::mouse_down_at(390, 8));
    (void)page.handle(input_event::mouse_up_at(390, 8));
    (void)page.run_script("console.log('end=' + getSelection().anchorOffset + ',' + "
                          "p.firstChild.length);");
    CHECK_EQ(logged(page, "end="), std::string{"end=16,16"});
}

// selection/canvas-drag.html: a press and a drag on a canvas select nothing;
// modifying-selection-with-primary-mouse-button.tentative.html: a press whose
// mousedown the page cancelled moves neither the selection nor the focus.
void test_a_canvas_or_a_cancelled_press_selects_nothing() {
    browser page{browser_options{400, 300}};
    page.load_html("<!doctype html><body style='margin:0'><p id=p style='margin:0'>words</p>"
                   "<canvas id=c style='display:block;width:200px;height:100px'></canvas>"
                   "<input id=field></body>");
    page.frame();
    (void)page.handle(input_event::mouse_move_to(10, 40));
    (void)page.handle(input_event::mouse_down_at(10, 40));
    (void)page.handle(input_event::mouse_move_to(190, 40));
    (void)page.handle(input_event::mouse_up_at(190, 40));
    (void)page.run_script("console.log('canvas=' + getSelection().isCollapsed);");
    CHECK_EQ(logged(page, "canvas="), std::string{"canvas=true"});

    (void)page.run_script("field.focus(); getSelection().collapse(p.firstChild, 2);"
                          "addEventListener('mousedown', e => e.preventDefault());");
    (void)page.handle(input_event::mouse_move_to(1, 8));
    (void)page.handle(input_event::mouse_down_at(1, 8));
    (void)page.handle(input_event::mouse_up_at(1, 8));
    (void)page.run_script("console.log('cancelled=' + getSelection().anchorOffset + ',' +"
                          " document.activeElement.id);");
    CHECK_EQ(logged(page, "cancelled="), std::string{"cancelled=2,field"});
}

// scrollend-event-not-fired-after-removing-scroller.tentative.html: a scroller
// removed while it scrolls never finishes, so it hears no scrollend.
void test_a_removed_scroller_hears_no_scrollend() {
    browser page{browser_options{400, 300}};
    page.load_html(page_source);
    page.frame();
    (void)page.run_script("box.addEventListener('scrollend', note);"
                          "box.onscroll = () => box.remove();"
                          "box.scrollTo(0, 50);");
    settle(page);
    CHECK_EQ(seen(page), std::string{"seen="});
}

// scrollend-event-fired-after-sequence-of-scrolls.tentative.html: a drag that
// pans cancels the finger's pointer once, and its scrolls end in ONE
// scrollend, after the lift.
void test_a_panning_drag_cancels_its_pointer_and_ends_once() {
    browser page{browser_options{400, 300}};
    page.load_html(page_source);
    page.frame();
    (void)page.run_script("for (const type of ['pointercancel', 'pointerup', 'scrollend']) {"
                          "  box.addEventListener(type, e => seen.push(e.type)); }");
    (void)page.touch("start", 50, 80);
    (void)page.touch("move", 50, 60);
    settle(page);
    (void)page.touch("move", 50, 40);
    settle(page);
    CHECK_EQ(seen(page), std::string{"seen=pointercancel"});
    (void)page.touch("end", 50, 40);
    settle(page);
    CHECK_EQ(seen(page), std::string{"seen=scrollend"});
}

} // namespace

int main() {
    test_a_panning_drag_cancels_its_pointer_and_ends_once();
    test_a_drag_selects_in_the_page_selection();
    test_a_press_counts_the_node_text_not_the_folded_box();
    test_a_canvas_or_a_cancelled_press_selects_nothing();
    test_a_removed_scroller_hears_no_scrollend();
    test_wheel_cancelability_and_mousewheel();
    test_wheel_scrolls_the_container_and_scrollend_follows();
    test_a_touch_is_pointer_then_touch_events();
    test_keys_scroll_the_focused_container();
    REPORT("input_wheel_touch");
}
