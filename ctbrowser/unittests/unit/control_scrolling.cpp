// A FIELD'S VIEW: a single-line input scrolling sideways and a textarea
// scrolling by line to keep the caret visible, the wheel over a field going to
// the field and not the page, auto-scroll while drag-selecting past the edge,
// and the caret blink - the one thing an idle page has to wake up for.
//
// One of five files carved out of unittests/unit/chrome_basics.cpp on
// 2026-09-07, when it had reached 2,299 lines. Every case is verbatim and in
// the order it had; the helpers more than one of the five needs are in
// chrome_probe.hpp beside this.

#include <ctbrowser.hpp>

#include "check.hpp"
#include "chrome_probe.hpp"
#include "dom_probe.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser::shell::input_event;
// Shared with widgets_basics and bootstrap_layout - test/support/dom_probe.hpp.
using ctbrowser_test::box_of;
using ctbrowser_test::caret_bars;
using ctbrowser_test::caret_of;
using ctbrowser_test::check;
using ctbrowser_test::click;
using ctbrowser_test::draws_text;
using ctbrowser_test::find_id;
using ctbrowser_test::selection_of;

namespace {

// --- a field scrolls to keep the caret in view ------------------------------

[[nodiscard]] float scroll_x_of(browser & page, std::string_view id) {
    const auto * state = page.control_state_of(find_id(page, id));
    return state == nullptr ? 0 : state->scroll_x;
}
[[nodiscard]] std::size_t scroll_line_of(browser & page, std::string_view id) {
    const auto * state = page.control_state_of(find_id(page, id));
    return state == nullptr ? 0 : state->scroll_line;
}

// A single-line <input> never scrolled sideways: the value ran off the clip and
// the caret went with it, so you could not see what you were typing.
void test_a_single_line_field_scrolls_horizontally() {
    browser page{browser_options{400, 200}};
    page.load_html("<body><input type=text id=t size=8></body>");
    check(page.frame().has_value(), "the page renders");
    const rect box = box_of(page, "t");
    click(page, box.x + 4, box.y + box.height / 2);
    check(page.focused() == find_id(page, "t"), "the field is focused");
    check(scroll_x_of(page, "t") == 0, "it starts unscrolled");

    check(page.text_input("the quick brown fox jumps over the lazy dog"), "typing is accepted");
    (void)page.frame();
    check(scroll_x_of(page, "t") > 0, "typing past the right edge scrolls the view");
    // ...and the caret came with it. caret_bars only counts bars strictly
    // INSIDE the box, so this is exactly the "can I see what I am typing" test.
    check(caret_bars(page, "t").size() == 1, "and the caret is still visible");

    (void)page.handle(input_event::key_press("Home"));
    (void)page.frame();
    check(scroll_x_of(page, "t") == 0, "Home scrolls back to the start");
    check(caret_bars(page, "t").size() == 1, "with the caret visible there too");
}

// The painter subtracts the scroll and the click mapping adds it back. Get the
// sign wrong in either and they disagree by twice the scroll.
void test_clicking_a_scrolled_field_lands_where_you_pointed() {
    browser page{browser_options{400, 200}};
    page.load_html("<body><input type=text id=t size=8></body>");
    check(page.frame().has_value(), "the page renders");
    const rect box = box_of(page, "t");
    click(page, box.x + 4, box.y + box.height / 2);
    check(page.text_input("abcdefghijklmnopqrstuvwxyz"), "typing is accepted");
    (void)page.frame();
    check(scroll_x_of(page, "t") > 0, "the field is scrolled");

    // Click at the left inside edge: that is the first character still visible,
    // which is NOT offset 0 - it is wherever the scroll starts.
    click(page, box.x + 7, box.y + box.height / 2);
    (void)page.frame();
    const std::size_t at = caret_of(page, "t");
    check(at > 0, "clicking a scrolled field does not snap back to the start");
    check(at < 26, "nor to the end");

    // And the caret it produced is drawn near where the click was, rather than
    // a scroll's width away from it.
    const auto bars = caret_bars(page, "t");
    check(bars.size() == 1, "one caret is drawn");
    if (bars.size() == 1) {
        check(std::fabs(bars[0].x - (box.x + 7)) < 12,
              "and it is drawn at the point that was clicked");
    }
}

// The stale-scroll class, killed by clamping the view where the geometry is
// derived rather than chasing every writer of the value.
void test_a_shrinking_value_does_not_leave_an_empty_field() {
    browser page{browser_options{400, 200}};
    page.load_html("<body><textarea id=t rows=2 cols=12></textarea>"
                   "<script>function shrink() {"
                   "  document.getElementById('t').value = 'hi';"
                   "}</script></body>");
    check(page.frame().has_value(), "the page renders");
    const rect box = box_of(page, "t");
    click(page, box.x + 4, box.y + 6);
    check(page.text_input("aaa bbb ccc ddd eee fff ggg hhh iii jjj"), "typing is accepted");
    (void)page.frame();
    check(scroll_line_of(page, "t") > 0, "the textarea scrolled down");

    // Script replaces the value with something that fits on one line. The
    // stored scroll is now far past the end.
    check(page.run_script("shrink();"), "the script ran");
    (void)page.frame();
    check(draws_text(page, "hi"), "the new value is drawn rather than an empty box");
}

// --- the wheel over a field -------------------------------------------------

// A tall page with a small textarea in it, so "did the FIELD scroll or the
// PAGE" is answerable.
[[nodiscard]] std::string wheel_page() {
    return "<body><textarea id=t rows=2 cols=12>aaa\nbbb\nccc\nddd\neee\nfff\nggg\nhhh</textarea>"
           "<div style='height:900px'>tall</div></body>";
}

void test_the_wheel_scrolls_the_textarea_under_the_pointer() {
    browser page{browser_options{300, 200}};
    page.load_html(wheel_page());
    check(page.frame().has_value(), "the page renders");
    check(page.max_scroll() > 0, "the page itself can scroll, so a stray page scroll would show");
    const rect box = box_of(page, "t");

    // Over the textarea: the FIELD moves and the page does not.
    (void)page.handle(input_event::wheel_at(box.x + 4, box.y + 4, -1));
    (void)page.frame();
    check(scroll_line_of(page, "t") > 0, "the wheel scrolls the textarea under the pointer");
    check(page.scroll_y() == 0, "and leaves the page alone");

    // Wheel to its end, then one more: the page takes that one.
    for (int i = 0; i < 20; ++i) {
        (void)page.handle(input_event::wheel_at(box.x + 4, box.y + 4, -1));
    }
    (void)page.frame();
    const std::size_t at_end = scroll_line_of(page, "t");
    check(page.scroll_y() > 0, "at its end, further notches fall through to the page");
    check(scroll_line_of(page, "t") == at_end, "and the field stays put");

    // Over the page instead: always the page.
    browser other{browser_options{300, 200}};
    other.load_html(wheel_page());
    check(other.frame().has_value(), "the page renders");
    (void)other.handle(input_event::wheel_at(10, 190, -1));
    (void)other.frame();
    check(other.scroll_y() > 0, "a wheel away from the field scrolls the page");
    check(scroll_line_of(other, "t") == 0, "and not the field");

    // And a wheel with NO pointer position cannot pick a field, so it is the
    // page's - which is what the headless wheel_by helper means.
    browser blind{browser_options{300, 200}};
    blind.load_html(wheel_page());
    check(blind.frame().has_value(), "the page renders");
    (void)blind.handle(input_event::wheel_by(-1));
    check(blind.scroll_y() > 0, "a wheel with no position scrolls the page");
}

// RULE 1 AGAINST RULE 2. The wheel moves the view and leaves the caret where it
// was, off screen if that is where it ends up - anything else makes the wheel
// useless, because the view would snap back the instant it moved. The caret
// comes back on the next keystroke, not before.
void test_wheeling_away_from_the_caret_leaves_it_until_you_type() {
    browser_options options{300, 200};
    options.caret_blink_ms = 0; // a solid caret, so its absence means absent
    browser page{options};
    page.load_html(wheel_page());
    check(page.frame().has_value(), "the page renders");
    const rect box = box_of(page, "t");

    click(page, box.x + 4, box.y + 6);
    check(page.focused() == find_id(page, "t"), "the textarea is focused");
    (void)page.frame();
    check(caret_bars(page, "t").size() == 1, "the caret is visible to begin with");

    // Wheel down, away from the caret's line.
    for (int i = 0; i < 3; ++i) {
        (void)page.handle(input_event::wheel_at(box.x + 4, box.y + 4, -1));
    }
    (void)page.frame();
    const std::size_t scrolled = scroll_line_of(page, "t");
    check(scrolled > 0, "the wheel scrolled the field");
    check(caret_bars(page, "t").empty(), "and the caret is left off screen, not chased");
    check(caret_of(page, "t") == 0, "the caret itself did not move");

    // Now type. Rule 1 fires and the view comes back to the caret.
    check(page.text_input("X"), "typing is accepted");
    (void)page.frame();
    check(scroll_line_of(page, "t") < scrolled, "typing snaps the view back to the caret");
    check(caret_bars(page, "t").size() == 1, "so the caret is visible again");
}

void test_pageup_and_pagedown_belong_to_a_focused_textarea() {
    browser page{browser_options{300, 200}};
    page.load_html(wheel_page());
    check(page.frame().has_value(), "the page renders");
    check(page.max_scroll() > 0, "the page can scroll, so a stray page scroll would show");
    const rect box = box_of(page, "t");
    click(page, box.x + 4, box.y + 6);
    check(caret_of(page, "t") == 0, "the caret starts at the top");

    check(page.handle(input_event::key_press("PageDown")), "PageDown is handled");
    (void)page.frame();
    check(caret_of(page, "t") > 0, "it moves the caret down the field");
    check(page.scroll_y() == 0, "and does NOT scroll the page out from under it");

    check(page.handle(input_event::key_press("PageUp")), "PageUp is handled");
    (void)page.frame();
    check(caret_of(page, "t") == 0, "and PageUp comes back");
    check(page.scroll_y() == 0, "still without scrolling the page");
}

// --- auto-scroll while drag-selecting ---------------------------------------

// Holding the pointer below the field keeps scrolling it with NO further mouse
// events. Without this the selection freezes one line short of the window:
// offset_at_point clamps to what the value has, so a stationary pointer picks
// the same offset for ever, and you could only select what was already visible.
void test_dragging_below_a_textarea_keeps_scrolling_it() {
    browser_options options{300, 200};
    options.caret_blink_ms = 0; // so next_wakeup_ms reports the autoscroll term alone
    browser page{options};
    page.load_html(wheel_page());
    check(page.frame().has_value(), "the page renders");
    const rect box = box_of(page, "t");

    (void)page.handle(input_event::mouse_down_at(box.x + 4, box.y + 6));
    check(page.focused() == find_id(page, "t"), "the drag focused the field");
    // Below the box, and then NOTHING further happens by mouse.
    (void)page.handle(input_event::mouse_move_to(box.x + 4, box.bottom() + 30));
    const std::size_t before = scroll_line_of(page, "t");
    const std::size_t caret_before = caret_of(page, "t");
    check(page.next_wakeup_ms() < 1e9, "a step is scheduled while the pointer is outside");

    (void)page.tick(500);
    (void)page.frame();
    check(scroll_line_of(page, "t") > before, "time alone scrolls the field");
    check(caret_of(page, "t") > caret_before, "and the selection grows with it");
    check(selection_of(page, "t").first == 0, "the anchor stays where the drag began");

    // Keep going to the end, and then it must STOP asking to be woken - a
    // pointer parked below a fully-scrolled field would otherwise spin the
    // event loop at the step interval for ever.
    for (int i = 0; i < 20; ++i) { (void)page.tick(500); }
    (void)page.frame();
    const std::size_t at_end = scroll_line_of(page, "t");
    check(page.next_wakeup_ms() > 1e9, "at the end it stops asking for wakeups");

    // Releasing disarms it.
    (void)page.handle(input_event::mouse_up_at(box.x + 4, box.bottom() + 30));
    (void)page.tick(1000);
    (void)page.frame();
    check(scroll_line_of(page, "t") == at_end, "and after mouse-up nothing moves");
    check(page.next_wakeup_ms() > 1e9, "with no wakeup pending");
}

// The rate rises with distance, so a small overshoot creeps and a big one
// races. Same elapsed time, two distances, more steps for the further one.
void test_autoscroll_goes_faster_the_further_out_you_drag() {
    const auto steps_at = [](float beyond) {
        browser_options options{300, 200};
        options.caret_blink_ms = 0;
        browser page{options};
        page.load_html(wheel_page());
        (void)page.frame();
        const rect box = box_of(page, "t");
        (void)page.handle(input_event::mouse_down_at(box.x + 4, box.y + 6));
        (void)page.handle(input_event::mouse_move_to(box.x + 4, box.bottom() + beyond));
        (void)page.tick(200);
        (void)page.frame();
        return scroll_line_of(page, "t");
    };
    const std::size_t near = steps_at(1);
    const std::size_t far = steps_at(200);
    check(near > 0, "a pointer just outside still scrolls");
    check(far > near, "and one far outside scrolls further in the same time");
}

// A drag that never leaves the field must not arm anything - the idle page
// contract is that a browser with nothing to do asks for no wakeups at all.
void test_dragging_inside_a_field_asks_for_no_wakeups() {
    browser_options options{300, 200};
    options.caret_blink_ms = 0;
    browser page{options};
    page.load_html(wheel_page());
    check(page.frame().has_value(), "the page renders");
    const rect box = box_of(page, "t");
    (void)page.handle(input_event::mouse_down_at(box.x + 4, box.y + 6));
    (void)page.handle(input_event::mouse_move_to(box.x + 20, box.y + 8));
    check(page.next_wakeup_ms() > 1e9, "a drag inside the box schedules nothing");
    const std::size_t before = scroll_line_of(page, "t");
    (void)page.tick(2000);
    check(scroll_line_of(page, "t") == before, "and time does not move it");
}

void test_the_caret_blinks() {
    browser_options options{400, 200};
    options.caret_blink_ms = 500;
    browser page{options};
    page.load_html("<body><input type=text id=f value=hi></body>");
    check(page.frame().has_value(), "the page renders");
    const rect field = box_of(page, "f");
    (void)page.handle(input_event::mouse_down_at(field.x + 4, field.y + 6));
    (void)page.handle(input_event::mouse_up_at(field.x + 4, field.y + 6));
    check(page.frame().has_value(), "it redraws focused");
    check(caret_bars(page, "f").size() == 1, "the caret starts SOLID, right after the click");

    (void)page.tick(600);
    check(page.frame().has_value(), "redraws");
    check(caret_bars(page, "f").empty(), "and is gone half a period later");

    (void)page.tick(500);
    check(page.frame().has_value(), "redraws");
    check(caret_bars(page, "f").size() == 1, "and back again");

    // TYPING RESTARTS IT SOLID. A caret that blinks out from under the
    // character you just typed reads as a dropped keystroke.
    (void)page.tick(600);
    check(page.frame().has_value(), "redraws");
    check(caret_bars(page, "f").empty(), "off again");
    check(page.text_input("x"), "typed");
    check(page.frame().has_value(), "redraws");
    check(caret_bars(page, "f").size() == 1, "typing brings the caret straight back");
}

// The loop has to be TOLD to draw, and a blink is the case where nothing else
// tells it: no event arrived and no callback ran. A loop that redraws only
// when it did something itself shows a caret that appears late or not at all -
// which is the same shape as the scrollbar that did not appear until the next
// mouse move.
void test_the_page_asks_for_a_frame_when_the_caret_blinks() {
    browser_options options{400, 200};
    options.caret_blink_ms = 500;
    browser page{options};
    page.load_html("<body><input type=text id=f value=hi></body>");
    check(page.frame().has_value(), "the page renders");
    const rect field = box_of(page, "f");
    (void)page.handle(input_event::mouse_down_at(field.x + 4, field.y + 6));
    (void)page.handle(input_event::mouse_up_at(field.x + 4, field.y + 6));
    check(page.frame().has_value(), "redraws focused");
    check(!page.needs_frame(), "and is then up to date");

    // No event, no callback: only time passing.
    check(page.tick(600) == 0, "no callbacks ran");
    check(page.needs_frame(), "but the page wants a frame, because the caret changed");

    // And it says when the NEXT one is due, so an idle loop can block instead
    // of waking sixty times a second to find out.
    check(page.frame().has_value(), "redraws");
    const double due = page.next_wakeup_ms();
    check(due > 0 && due <= 500, "the next blink is due within a period");
}

void test_an_idle_page_asks_for_nothing() {
    browser page{browser_options{300, 200}};
    page.load_html("<body><p>a page with no timers and nothing focused</p></body>");
    check(page.frame().has_value(), "the page renders");
    check(!page.needs_frame(), "nothing to draw");
    check(page.tick(1000) == 0, "and nothing to run");
    check(!page.needs_frame(), "still nothing to draw after a whole second");
    // Infinity is what lets an application BLOCK rather than poll - the
    // difference between an idle browser costing nothing and costing a core.
    check(page.next_wakeup_ms() > 1e9, "and no wakeup is due at all");
}

void test_a_blink_does_not_relayout() {
    browser_options options{400, 200};
    options.caret_blink_ms = 500;
    browser page{options};
    page.load_html("<body><input type=text id=f value=hi></body>");
    check(page.frame().has_value(), "the page renders");
    const rect field = box_of(page, "f");
    (void)page.handle(input_event::mouse_down_at(field.x + 4, field.y + 6));
    (void)page.handle(input_event::mouse_up_at(field.x + 4, field.y + 6));
    check(page.frame().has_value(), "redraws");
    const std::size_t layouts = page.layout_count();
    (void)page.tick(600);
    check(page.frame().has_value(), "redraws with the caret hidden");
    // Only the caret changed. the previous engine re-ran layout every frame for exactly this,
    // and it is the reason the engine has a dirty level per stage.
    check(page.layout_count() == layouts, "a blink re-PAINTS and does not re-lay-out");
}

} // namespace

int main() {
    test_a_single_line_field_scrolls_horizontally();
    test_clicking_a_scrolled_field_lands_where_you_pointed();
    test_a_shrinking_value_does_not_leave_an_empty_field();
    test_the_wheel_scrolls_the_textarea_under_the_pointer();
    test_wheeling_away_from_the_caret_leaves_it_until_you_type();
    test_pageup_and_pagedown_belong_to_a_focused_textarea();
    test_dragging_below_a_textarea_keeps_scrolling_it();
    test_autoscroll_goes_faster_the_further_out_you_drag();
    test_dragging_inside_a_field_asks_for_no_wakeups();
    test_the_caret_blinks();
    test_a_blink_does_not_relayout();
    test_the_page_asks_for_a_frame_when_the_caret_blinks();
    test_an_idle_page_asks_for_nothing();
    REPORT("control_scrolling");
}
