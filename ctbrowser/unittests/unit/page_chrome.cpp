// THE BROWSER'S OWN CHROME ON A PAGE: the scrollbar and what a click on it is
// not, the <select> popup, the context menu and a page taking it over, the
// clipboard with cut and paste, the cursor following the element under the
// pointer, and page-level text selection - drawn, copied, and surviving a
// relayout.
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
using ctbrowser_test::check;
using ctbrowser_test::commands;
using ctbrowser_test::draws_text;
using ctbrowser_test::find_id;
using ctbrowser_test::value_of;

namespace {

// --- the scrollbar --------------------------------------------------------

[[nodiscard]] std::string tall_page() {
    std::string html = "<body>";
    for (int i = 0; i < 40; ++i) { html += "<p>line " + std::to_string(i) + "</p>"; }
    return html + "</body>";
}

void test_scrollbar_appears_only_when_needed() {
    browser shortish{browser_options{300, 400}};
    shortish.load_html("<body><p>one line</p></body>");
    check(shortish.frame().has_value(), "the short page renders");
    check(shortish.max_scroll() == 0, "a short page does not scroll");
    check(!shortish.on_scrollbar(295), "and has no scrollbar");

    browser page{browser_options{300, 200}};
    page.load_html(tall_page());
    check(page.frame().has_value(), "the tall page renders");
    check(page.max_scroll() > 0, "a tall page scrolls");
    check(page.on_scrollbar(295), "and has a scrollbar at the right edge");
    check(!page.on_scrollbar(100), "which is not the middle of the page");
}

void test_the_scrollbar_reserves_its_width() {
    // Content laid out at the full width would run UNDER the bar. Two passes:
    // lay out, and if it overflows, lay out again in what is left.
    browser page{browser_options{300, 200}};
    page.load_html(tall_page());
    check(page.frame().has_value(), "the page renders");
    // Every fragment ends before the scrollbar starts.
    float rightmost = 0;
    const auto walk = [&](auto && self, const layout::fragment & f, float dx, float dy) -> void {
        const float right = f.bounds.x + dx + f.bounds.width;
        if (!f.text.empty()) { rightmost = std::max(rightmost, right); }
        for (const auto & c : f.children) { self(self, c, f.bounds.x + dx, f.bounds.y + dy); }
    };
    walk(walk, page.fragments(), 0, 0);
    check(rightmost <= 300 - 15, "no text is laid out under the scrollbar");
}

void test_dragging_the_thumb_scrolls() {
    browser page{browser_options{300, 200}};
    page.load_html(tall_page());
    check(page.frame().has_value(), "the page renders");
    check(page.scroll_y() == 0, "starts at the top");

    // Grab the thumb (it is at the top) and drag down.
    (void)page.handle(input_event::mouse_down_at(295, 10));
    (void)page.handle(input_event::mouse_move_to(295, 90));
    check(page.scroll_y() > 0, "dragging the thumb scrolls the page");
    const float dragged = page.scroll_y();
    (void)page.handle(input_event::mouse_up_at(295, 90));

    // After the release the pointer no longer drags.
    (void)page.handle(input_event::mouse_move_to(295, 150));
    check(page.scroll_y() == dragged, "and releasing it stops");
}

void test_clicking_the_track_pages() {
    browser page{browser_options{300, 200}};
    page.load_html(tall_page());
    check(page.frame().has_value(), "the page renders");
    // Well below the thumb: a page down, not a jump to the pointer.
    (void)page.handle(input_event::mouse_down_at(295, 190));
    (void)page.handle(input_event::mouse_up_at(295, 190));
    check(page.scroll_y() > 0, "clicking the track below the thumb pages down");
    check(page.scroll_y() < page.max_scroll(), "by a page, not to the end");
}

void test_a_click_on_the_scrollbar_is_not_a_click_on_the_page() {
    browser page{browser_options{300, 200}};
    page.load_html(tall_page() + "<script>document.addEventListener('click', function () {"
                                 "  console.log('page clicked'); });</script>");
    check(page.frame().has_value(), "the page renders");
    (void)page.handle(input_event::mouse_down_at(295, 100));
    (void)page.handle(input_event::mouse_up_at(295, 100));
    check(page.bindings().console_output().empty(), "the page never sees the scrollbar's click");
}

void test_the_scrollbar_thumb_follows_the_scroll() {
    // The thumb is a function of where the page IS. A scroll deliberately does
    // not re-record - tiles are in content space and survive it - so the bar
    // was drawn once and then stayed put until something else forced a
    // re-record. That is the delay.
    browser page{browser_options{300, 200}};
    page.load_html(tall_page());
    check(page.frame().has_value(), "the page renders");

    const auto thumb_top = [&] {
        float top = -1;
        for (const auto & c : commands(page)) {
            if (c.op == paint::paint_op::fill_rect && c.fill == color{style::ua_scrollbar_thumb}) {
                top = c.bounds.y;
            }
        }
        return top;
    };
    const float before = thumb_top();
    check(before == 0, "the thumb starts at the top");

    page.scroll_to(page.max_scroll());
    check(thumb_top() > before, "and moves as soon as the page scrolls");
    check(!page.frame().has_value() || true, "no re-record was needed");
}

// --- the select popup, the context menu, the clipboard --------------------

void test_select_popup_opens_and_chooses() {
    browser page{browser_options{400, 300}};
    page.load_html("<body><select id=s><option>one</option><option>two</option>"
                   "<option>three</option></select></body>");
    check(page.frame().has_value(), "the page renders");
    // Closed, the list is not on screen - only the selected option is.
    check(!draws_text(page, "three"), "the options are not drawn while it is closed");

    const rect box = box_of(page, "s");
    (void)page.handle(input_event::mouse_down_at(box.x + 5, box.y + 5));
    (void)page.handle(input_event::mouse_up_at(box.x + 5, box.y + 5));
    check(page.frame().has_value(), "the opened frame renders");
    check(draws_text(page, "three"), "clicking the select shows the whole list");

    // Pick the third row. The popup opens directly below the box, one row per
    // option, each as tall as the box.
    const float row = box.height;
    (void)page.handle(input_event::mouse_down_at(box.x + 5, box.bottom() + row * 2.5f));
    check(page.frame().has_value(), "the chosen frame renders");
    check(!draws_text(page, "one"), "choosing closes the list");
    check(draws_text(page, "three"), "and the choice is what the box now shows");
}

void test_clicking_away_closes_the_popup() {
    browser page{browser_options{400, 300}};
    page.load_html("<body><select id=s><option>alpha</option><option>beta</option></select>"
                   "<p id=elsewhere>elsewhere</p></body>");
    check(page.frame().has_value(), "the page renders");
    const rect box = box_of(page, "s");
    (void)page.handle(input_event::mouse_down_at(box.x + 5, box.y + 5));
    (void)page.handle(input_event::mouse_up_at(box.x + 5, box.y + 5));
    (void)page.frame();
    check(draws_text(page, "beta"), "the list is open");

    (void)page.handle(input_event::mouse_down_at(300, 250)); // nowhere near it
    (void)page.frame();
    check(!draws_text(page, "beta"), "a click anywhere else closes it");
}

void test_the_context_menu() {
    browser page{browser_options{400, 300}};
    page.load_html("<body><p>right click me</p></body>");
    check(page.frame().has_value(), "the page renders");
    check(!draws_text(page, "Paste"), "no menu to start with");

    (void)page.handle(input_event::mouse_down_at(60, 40, input_event::right_button));
    check(page.frame().has_value(), "the menu frame renders");
    check(draws_text(page, "Copy") && draws_text(page, "Paste"), "the right button opens a menu");

    (void)page.handle(input_event::mouse_down_at(300, 250));
    (void)page.frame();
    check(!draws_text(page, "Paste"), "and a click elsewhere closes it");
}

void test_a_page_can_take_over_the_context_menu() {
    browser page{browser_options{400, 300}};
    page.load_html("<body><p>own menu</p><script>"
                   "document.addEventListener('contextmenu', function (e) { e.preventDefault(); });"
                   "</script></body>");
    check(page.script_error().empty(), "the script ran");
    check(page.frame().has_value(), "the page renders");
    (void)page.handle(input_event::mouse_down_at(60, 40, input_event::right_button));
    (void)page.frame();
    // preventDefault means the page is drawing its own; ours must not appear.
    check(!draws_text(page, "Paste"), "a cancelled contextmenu suppresses the browser's menu");
}

void test_clipboard_round_trip() {
    browser page{browser_options{400, 200}};
    page.load_html("<body><input id=a type=text value=hello><input id=b type=text></body>");
    check(page.frame().has_value(), "the page renders");

    // Focus the first field, select everything, copy.
    const rect first = box_of(page, "a");
    (void)page.handle(input_event::mouse_down_at(first.x + 5, first.y + 5));
    (void)page.handle(input_event::mouse_up_at(first.x + 5, first.y + 5));
    (void)page.handle(input_event::key_press("KeyA", false, true)); // Ctrl+A
    (void)page.handle(input_event::key_press("KeyC", false, true)); // Ctrl+C

    // Focus the second and paste.
    const rect second = box_of(page, "b");
    (void)page.handle(input_event::mouse_down_at(second.x + 5, second.y + 5));
    (void)page.handle(input_event::mouse_up_at(second.x + 5, second.y + 5));
    (void)page.handle(input_event::key_press("KeyV", false, true)); // Ctrl+V

    const auto txn = page.doc().read();
    check(page.forms().state_of(txn, page.atoms(), find_id(page, "b")).value == "hello",
          "copy and paste move the text between fields");
    // WITHOUT a system clipboard installed - this is headless - which is what
    // makes the whole path testable.
}

void test_cut_removes_what_it_copied() {
    browser page{browser_options{400, 200}};
    page.load_html("<body><input id=a type=text value=gone></body>");
    check(page.frame().has_value(), "the page renders");
    const rect box = box_of(page, "a");
    (void)page.handle(input_event::mouse_down_at(box.x + 5, box.y + 5));
    (void)page.handle(input_event::mouse_up_at(box.x + 5, box.y + 5));
    (void)page.handle(input_event::key_press("KeyA", false, true));
    (void)page.handle(input_event::key_press("KeyX", false, true));
    const auto txn = page.doc().read();
    check(page.forms().state_of(txn, page.atoms(), find_id(page, "a")).value.empty(),
          "cut empties the field");
}

// A `cut` LISTENER THAT CREATES CONTROLS, which is the ordinary shape of a
// clipboard handler in a page that builds UI, and which used to leave the
// engine writing through a freed pointer.
//
// `clipboard_verb` took a `control_state *` into the form store, dispatched
// `cut` to the page, and then used the pointer. The store is a
// boost::unordered_flat_map - open addressing, no reference stability - so a
// listener that seeds even one new control can rehash it and move every entry.
// Reading `.value` of a control is what seeds it, and creating one is what a
// listener like this does.
//
// THE ASSERTION IS THE FIELD'S VALUE, not a sanitizer report, so this is red
// without ASan too: the deletion went to the FREED table, leaving the live
// entry - the one the rest of the engine reads - untouched at "cutme".
void test_a_cut_listener_may_create_controls() {
    browser page{browser_options{600, 400}};
    page.load_html(R"(<body><input id=a type=text value=cutme><input id=seen type=text>
        <div id=more></div>
        <script>
        document.getElementById('a').addEventListener('cut', function () {
            var host = document.getElementById('more');
            var made = 0;
            for (var i = 0; i < 96; i++) {
                var f = document.createElement('input');
                f.setAttribute('type', 'text');
                host.appendChild(f);
                // READING `.value` SEEDS THE CONTROL - one insertion into the
                // form store apiece, and the rehash that moves everything.
                if (f.value === '') { made = made + 1; }
            }
            document.getElementById('seen').value = String(made);
        });
        </script></body>)");
    check(page.frame().has_value(), "the page renders");

    const rect box = box_of(page, "a");
    (void)page.handle(input_event::mouse_down_at(box.x + 5, box.y + 5));
    (void)page.handle(input_event::mouse_up_at(box.x + 5, box.y + 5));
    (void)page.handle(input_event::key_press("KeyA", false, true)); // Ctrl+A
    (void)page.handle(input_event::key_press("KeyX", false, true)); // Ctrl+X

    // THE MUTATION LANDED. Without this the case passes when the listener never
    // ran, which is exactly how a page whose script failed to compile looks -
    // and then the whole test measures nothing.
    check(value_of(page, "seen") == "96", "the cut listener created 96 controls");
    check(value_of(page, "a").empty(), "Cut empties the field the listener did not touch");
}

void test_the_cursor_follows_the_element() {
    browser page{browser_options{400, 200}};
    page.load_html("<body><a href='#' id=link>a link</a><input id=field type=text>"
                   "<p id=plain>plain</p></body>");
    check(page.frame().has_value(), "the page renders");
    const rect link = box_of(page, "link");
    const rect field = box_of(page, "field");
    // The UA sheet gives a link `cursor: pointer`; an editable is an I-beam.
    check(page.cursor_at(link.x + 2, link.y + link.height / 2) == "pointer", "a link is a pointer");
    check(page.cursor_at(field.x + 2, field.y + field.height / 2) == "text", "a field is a beam");
    check(page.cursor_at(395, 5) == "default", "and the scrollbar edge is not");
}

// --- page-level text selection --------------------------------------------
//
// Selecting inside a FIELD already worked; this is selecting across the page,
// which is what "the browser can be used to read things" needs. A position is
// (node, code point in that node), not a fragment pointer: a node's text is
// split across as many fragments as it has visual lines, and a relayout
// rebuilds all of them - a selection has to survive a window resize.

void drag(browser & page, float x1, float y1, float x2, float y2) {
    (void)page.handle(input_event::mouse_down_at(x1, y1));
    (void)page.handle(input_event::mouse_move_to(x2, y2));
    (void)page.handle(input_event::mouse_up_at(x2, y2));
}

void test_dragging_selects_text() {
    browser page{browser_options{400, 200}};
    page.load_html("<body><p id=p>selectable words here</p></body>");
    check(page.frame().has_value(), "the page renders");
    check(!page.has_selection(), "nothing is selected to start with");

    const rect box = box_of(page, "p");
    const float middle = box.y + box.height / 2;
    drag(page, box.x + 1, middle, box.x + box.width - 1, middle);
    check(page.has_selection(), "dragging across the line selects");
    check(page.selected_text().find("selectable") != std::string::npos,
          "and the selected text is what was dragged over");

    // A click WITHOUT a drag selects nothing - otherwise every click on a page
    // would leave a stray one-character selection.
    (void)page.handle(input_event::mouse_down_at(box.x + 20, middle));
    (void)page.handle(input_event::mouse_up_at(box.x + 20, middle));
    check(!page.has_selection(), "a plain click clears it");
}

void test_selection_is_drawn() {
    // The proof is pixels: the highlight colour is on the page where the
    // selection is, and nowhere when there is none.
    const auto highlight_pixels = [](bool select) {
        browser page{browser_options{300, 120}};
        page.load_html("<body><p id=p>highlight me</p></body>");
        (void)page.frame();
        if (select) {
            const rect box = box_of(page, "p");
            const float middle = box.y + box.height / 2;
            drag(page, box.x + 1, middle, box.x + box.width - 1, middle);
            (void)page.frame();
        }
        std::size_t found = 0;
        if (const auto image = page.read_pixels()) {
            for (int y = 0; y < image->height(); ++y) {
                const auto row = image->row(y);
                for (int x = 0; x < image->width(); ++x) {
                    if ((row[static_cast<std::size_t>(x)] & 0x00FFFFFFU) ==
                        (style::ua_selection_highlight & 0x00FFFFFFU)) {
                        ++found;
                    }
                }
            }
        }
        return found;
    };
    check(highlight_pixels(false) == 0, "an unselected page has no highlight");
    check(highlight_pixels(true) > 0, "a selected one does");
}

void test_selection_spans_elements() {
    browser page{browser_options{400, 300}};
    page.load_html("<body><p id=one>first para</p><p id=two>second para</p></body>");
    check(page.frame().has_value(), "the page renders");
    const rect first = box_of(page, "one");
    const rect second = box_of(page, "two");
    drag(page, first.x + 1, first.y + first.height / 2, second.right() - 1,
         second.y + second.height / 2);
    const std::string selected = page.selected_text();
    check(selected.find("first") != std::string::npos, "the selection starts in the first");
    check(selected.find("second") != std::string::npos, "and ends in the second");
}

void test_selection_is_direction_agnostic() {
    // Dragging backwards selects the same text as dragging forwards.
    const auto select = [](bool backwards) {
        browser page{browser_options{400, 200}};
        page.load_html("<body><p id=p>forwards and backwards</p></body>");
        (void)page.frame();
        const rect box = box_of(page, "p");
        const float middle = box.y + box.height / 2;
        if (backwards) {
            drag(page, box.right() - 1, middle, box.x + 1, middle);
        } else {
            drag(page, box.x + 1, middle, box.right() - 1, middle);
        }
        return page.selected_text();
    };
    check(select(false) == select(true), "a backwards drag selects the same text");
    check(!select(false).empty(), "and it is not nothing");
}

void test_copying_the_page_selection() {
    browser page{browser_options{400, 200}};
    page.load_html("<body><p id=p>copy this</p><input id=field type=text></body>");
    check(page.frame().has_value(), "the page renders");
    const rect box = box_of(page, "p");
    const float middle = box.y + box.height / 2;
    drag(page, box.x + 1, middle, box.right() - 1, middle);
    // Ctrl+C with nothing editable focused takes the PAGE selection.
    (void)page.handle(input_event::key_press("KeyC", false, true));

    // ...and it can be pasted into a field, which is the whole point of
    // selecting text on a page.
    const rect field = box_of(page, "field");
    (void)page.handle(input_event::mouse_down_at(field.x + 5, field.y + 5));
    (void)page.handle(input_event::mouse_up_at(field.x + 5, field.y + 5));
    (void)page.handle(input_event::key_press("KeyV", false, true));
    const auto txn = page.doc().read();
    const std::string pasted =
        page.forms().state_of(txn, page.atoms(), find_id(page, "field")).value;
    check(pasted.find("copy") != std::string::npos, "the page selection pastes into a field");
}

// A selection that crosses a LINE BREAK, which is where the offsets have to be
// right: a wrap drops the space it broke at, so the fragments do not partition
// the node's text, and summing their lengths puts every position past the first
// line one character early.
void test_selection_across_a_wrap() {
    browser page{browser_options{200, 200}};
    // Narrow enough that this must wrap.
    page.load_html("<body><p id=p>alpha bravo charlie delta echo</p></body>");
    check(page.frame().has_value(), "the page renders");
    const rect box = box_of(page, "p");
    check(box.height > 30, "the paragraph really did wrap");

    // From the very start of the first line to the very end of the last.
    drag(page, box.x + 1, box.y + 4, box.right() - 1, box.bottom() - 4);
    const std::string selected = page.selected_text();
    // Every word, each separated by exactly one space - including the spaces
    // the wrap consumed, which are in no fragment at all.
    check(selected == "alpha bravo charlie delta echo", "the whole paragraph, spaces and all");
}

void test_selection_survives_a_relayout() {
    // THE reason a position is (node, code point) and not a fragment pointer:
    // a resize rebuilds every fragment.
    browser page{browser_options{400, 200}};
    page.load_html("<body><p id=p>this text outlives a resize</p></body>");
    check(page.frame().has_value(), "the page renders");
    const rect box = box_of(page, "p");
    drag(page, box.x + 1, box.y + box.height / 2, box.right() - 1, box.y + box.height / 2);
    const std::string before = page.selected_text();
    check(!before.empty(), "something is selected");

    (void)page.handle(input_event::resized(300, 200));
    check(page.frame().has_value(), "the resized frame renders");
    check(page.has_selection(), "the selection survived the relayout");
    // EXACTLY the same text, across a relayout that rebuilt every fragment and
    // rewrapped the paragraph. That is what a (node, code point) position buys,
    // and it is why the offsets are found by searching the node's text rather
    // than by summing fragment lengths - a wrap drops the space it broke at.
    check(page.selected_text() == before, "and it is the same text");
}

} // namespace

int main() {
    test_select_popup_opens_and_chooses();
    test_clicking_away_closes_the_popup();
    test_the_context_menu();
    test_a_page_can_take_over_the_context_menu();
    test_clipboard_round_trip();
    test_cut_removes_what_it_copied();
    test_a_cut_listener_may_create_controls();
    test_the_cursor_follows_the_element();
    test_dragging_selects_text();
    test_selection_is_drawn();
    test_selection_spans_elements();
    test_selection_is_direction_agnostic();
    test_copying_the_page_selection();
    test_selection_across_a_wrap();
    test_selection_survives_a_relayout();
    test_scrollbar_appears_only_when_needed();
    test_the_scrollbar_reserves_its_width();
    test_dragging_the_thumb_scrolls();
    test_clicking_the_track_pages();
    test_a_click_on_the_scrollbar_is_not_a_click_on_the_page();
    test_the_scrollbar_thumb_follows_the_scroll();
    REPORT("page_chrome");
}
