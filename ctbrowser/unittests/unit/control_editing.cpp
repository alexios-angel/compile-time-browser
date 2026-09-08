// FORM CONTROLS AS THEY LOOK AND AS THEY ARE EDITED: the caret measured in the
// face the text is drawn in, a click and the arrow keys placing it, a drag
// selecting inside a field, password bullets, a disabled control, the tick in a
// checkbox, a button's label, a textarea's lines and soft wraps - and, at the
// end, what a script reads back off a control and writes into one.
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
using ctbrowser_test::commands;
using ctbrowser_test::draws_text;
using ctbrowser_test::find_id;
using ctbrowser_test::log_of;
using ctbrowser_test::selection_of;

namespace {

// --- the form-control batch ------------------------------------------------
//
// Eight bugs a real page found that the test suite did not, because every one
// of them lives in what a control LOOKS like or what a script can READ from it
// - and the example ctests only check that the process exits 0.

[[nodiscard]] std::size_t arrow_rows(browser & page, std::string_view id) {
    // The drop-down arrow is a stack of 1px-tall fills in the frame colour.
    const node_id want = find_id(page, id);
    std::size_t rows = 0;
    for (const auto & c : commands(page)) {
        if (c.op == paint::paint_op::fill_rect && c.source == want && c.bounds.height == 1 &&
            c.fill == color{style::ua_widget_frame} && c.bounds.width < 10) {
            ++rows;
        }
    }
    return rows;
}

// --- editing with the mouse and the arrow keys ----------------------------

// A caret must be measured with the face the text is DRAWN in. A textarea is
// monospace by UA rule and its text was drawn in the default serif, so the
// caret ran ahead by the difference on every character - which reads as a
// growing gap between what you typed and where the caret is.
void test_a_control_draws_in_the_face_it_measures() {
    // NO SDL3_ttf, NO REAL FACES. `use_real_fonts()` answers false in that
    // build and this asserted it was true, so the whole file failed on a
    // machine that simply does not have the library - which the engine
    // supports and `test_inline_text_shares_a_baseline` below already guarded
    // for. Found when builds moved to the shared devbox, which has no SDL at
    // all.
    if (!raster::ttf_available()) {
        std::printf("     no SDL3_ttf in this build - real-font checks skipped\n");
        return;
    }
    browser page{browser_options{500, 300}};
    check(page.use_real_fonts(), "the vendored faces load");
    page.load_html("<body><textarea id=t rows=3 cols=20></textarea></body>");
    check(page.frame().has_value(), "the page renders");
    const rect box = box_of(page, "t");
    click(page, box.x + 6, box.y + 6);
    check(page.text_input("Hello world"), "typed");
    check(page.frame().has_value(), "redraws");

    std::string drawn;
    float text_x = 0;
    layout::text_face drawn_face;
    for (const auto & c : commands(page)) {
        if (c.op == paint::paint_op::text_run && c.source == find_id(page, "t")) {
            drawn = c.text;
            text_x = c.bounds.x;
            drawn_face = layout::text_face{c.face.family, c.face.bold, c.face.italic};
        }
    }
    check(drawn == "Hello world", "the value is drawn");
    // The caret sits at the END of the drawn text, measured in the SAME face.
    const std::vector<rect> bars = caret_bars(page, "t");
    check(bars.size() == 1, "there is a caret");
    if (bars.empty()) { return; }
    const float expected = text_x + page.metrics()(drawn, 16, drawn_face);
    check(std::fabs(bars[0].x - expected) < 2,
          "and it is where the drawn text ENDS, not a different font's width along");
}

void test_clicking_in_a_textarea_places_the_caret() {
    browser page{browser_options{500, 300}};
    page.load_html("<body><textarea id=t rows=3 cols=20>abcdef\nghijkl</textarea></body>");
    check(page.frame().has_value(), "the page renders");
    const rect box = box_of(page, "t");

    // The start of the FIRST line.
    click(page, box.x + 7, box.y + 6);
    check(caret_of(page, "t") == 0, "clicking the first character lands before it");

    // The start of the SECOND line: past the newline, at offset 7.
    click(page, box.x + 7, box.y + 26);
    check(caret_of(page, "t") == 7, "clicking the second line lands at its start");

    // Well past the end of a line clamps to that line's end, not to the value's.
    click(page, box.right() - 4, box.y + 6);
    check(caret_of(page, "t") == 6, "clicking past a line's end lands at ITS end");
}

void test_arrows_move_by_visual_line_in_a_textarea() {
    browser page{browser_options{500, 300}};
    page.load_html("<body><textarea id=t rows=3 cols=20>abcdef\nghijkl</textarea></body>");
    check(page.frame().has_value(), "the page renders");
    const rect box = box_of(page, "t");
    click(page, box.x + 7, box.y + 6); // caret at 0

    (void)page.handle(input_event::key_press("ArrowRight"));
    (void)page.handle(input_event::key_press("ArrowRight"));
    check(caret_of(page, "t") == 2, "right moves by a character");

    // DOWN keeps the column. Walking by characters through the newline would
    // land at 3, which is not what down means.
    (void)page.handle(input_event::key_press("ArrowDown"));
    check(caret_of(page, "t") == 9, "down keeps the column on the next line");
    (void)page.handle(input_event::key_press("ArrowUp"));
    check(caret_of(page, "t") == 2, "and up comes back to it");

    // HOME and END are that LINE's ends, not the whole value's.
    (void)page.handle(input_event::key_press("ArrowDown"));
    (void)page.handle(input_event::key_press("End"));
    check(caret_of(page, "t") == 13, "End goes to the end of the second line");
    (void)page.handle(input_event::key_press("Home"));
    check(caret_of(page, "t") == 7, "Home to its start");
}

void test_dragging_selects_inside_a_field() {
    browser page{browser_options{500, 300}};
    page.load_html("<body><input type=text id=f value=abcdefgh size=20></body>");
    check(page.frame().has_value(), "the page renders");
    const rect field = box_of(page, "f");

    (void)page.handle(input_event::mouse_down_at(field.x + 7, field.y + 10));
    (void)page.handle(input_event::mouse_move_to(field.x + 40, field.y + 10));
    (void)page.handle(input_event::mouse_up_at(field.x + 40, field.y + 10));
    const auto [from, to] = selection_of(page, "f");
    check(from == 0, "the selection starts where the press was");
    check(to > from, "and covers what was dragged over");

    // And it can be COPIED, which is the whole reason to select it.
    std::string clipboard;
    page.set_clipboard_hooks([&clipboard](const std::string & text) { clipboard = text; },
                             [&clipboard] { return clipboard; });
    input_event copy = input_event::key_press("KeyC");
    copy.ctrl = true;
    (void)page.handle(copy);
    check(!clipboard.empty(), "Ctrl+C copied something");
    check(clipboard == std::string_view{"abcdefgh"}.substr(from, to - from),
          "and it is exactly what was selected");
}

void test_escape_and_blur_drop_a_field_selection() {
    browser page{browser_options{500, 300}};
    page.load_html("<body><input type=text id=f value=abcdef size=20>"
                   "<p id=elsewhere>not a field</p></body>");
    check(page.frame().has_value(), "the page renders");
    const rect field = box_of(page, "f");
    click(page, field.x + 6, field.y + 10);

    input_event select_all = input_event::key_press("KeyA");
    select_all.ctrl = true;
    (void)page.handle(select_all);
    {
        const auto [from, to] = selection_of(page, "f");
        check(to - from == 6, "Ctrl+A selects the whole value");
    }
    (void)page.handle(input_event::key_press("Escape"));
    {
        const auto [from, to] = selection_of(page, "f");
        check(from == to, "Escape drops it, as a browser does");
    }

    // And so does clicking away: a highlight left behind in a field nobody is
    // typing in reads as still selected.
    (void)page.handle(select_all);
    check(selection_of(page, "f").second > 0, "selected again");
    const rect elsewhere = box_of(page, "elsewhere");
    click(page, elsewhere.x + 4, elsewhere.y + 4);
    {
        const auto [from, to] = selection_of(page, "f");
        check(from == to, "clicking somewhere else drops it too");
    }
}

void test_a_password_shows_bullets() {
    browser page{browser_options{500, 300}};
    page.load_html("<body><input type=password id=p value=hunter2 size=20>"
                   "<input type=text id=t value=hunter2 size=20></body>");
    check(page.frame().has_value(), "the page renders");

    std::string password_run;
    std::string text_run;
    for (const auto & c : commands(page)) {
        if (c.op != paint::paint_op::text_run) { continue; }
        if (c.source == find_id(page, "p")) { password_run = c.text; }
        if (c.source == find_id(page, "t")) { text_run = c.text; }
    }
    check(text_run == "hunter2", "a text field shows its value");
    check(!password_run.empty(), "a password field draws something");
    check(password_run.find("hunter") == std::string::npos,
          "but NOT the password - it was drawn in plain text");
    // One bullet per code point, so the field is the same length as the value.
    check(password_run == "•••••••", "seven characters, seven bullets");
}

void test_a_password_caret_is_measured_on_the_bullets() {
    if (!raster::ttf_available()) {
        std::printf("     no SDL3_ttf in this build - real-font checks skipped\n");
        return;
    }
    browser page{browser_options{500, 300}};
    check(page.use_real_fonts(), "the vendored faces load");
    page.load_html("<body><input type=password id=p size=20></body>");
    check(page.frame().has_value(), "the page renders");
    const rect field = box_of(page, "p");
    click(page, field.x + 6, field.y + 10);
    check(page.text_input("abcd"), "typed");
    check(page.frame().has_value(), "redraws");

    float text_x = 0;
    std::string drawn;
    for (const auto & c : commands(page)) {
        if (c.op == paint::paint_op::text_run && c.source == find_id(page, "p")) {
            text_x = c.bounds.x;
            drawn = c.text;
        }
    }
    const std::vector<rect> bars = caret_bars(page, "p");
    check(bars.size() == 1, "there is a caret");
    if (bars.empty() || drawn.empty()) { return; }
    // Measured on what is SHOWN. A bullet is wider than most letters, so
    // measuring the letters puts the caret inside the bullets.
    const float expected = text_x + page.metrics()(drawn, 16, layout::text_face{});
    check(std::fabs(bars[0].x - expected) < 2, "and it sits after the last bullet");
}

// --- disabled --------------------------------------------------------------

void test_a_disabled_control_looks_and_acts_disabled() {
    browser page{browser_options{500, 300}};
    page.load_html("<body><button id=on>Live</button><button id=off disabled>Dead</button>"
                   "<input type=text id=f disabled value=x size=10></body>");
    check(page.frame().has_value(), "the page renders");

    color live{};
    color dead{};
    for (const auto & c : commands(page)) {
        if (c.op != paint::paint_op::text_run) { continue; }
        if (c.text == "Live") { live = c.fill; }
        if (c.text == "Dead") { dead = c.fill; }
    }
    check(!(live == color{}) && !(dead == color{}), "both labels are drawn");
    check(!(live == dead), "and a disabled button is NOT drawn like a live one");
    check(dead == color{style::ua_widget_disabled_text}, "it is greyed");

    // And it is INERT: no focus, no activation, no events.
    const rect off = box_of(page, "off");
    click(page, off.x + 6, off.y + 6);
    check(!page.focused(), "a disabled button does not take focus");
    const rect disabled_field = box_of(page, "f");
    click(page, disabled_field.x + 6, disabled_field.y + 8);
    check(!page.focused(), "and neither does a disabled field");
    check(!page.text_input("hello"), "so typing goes nowhere");
}

// A CHECKED checkbox has a TICK in it, and that is what distinguishes it from
// an unchecked one. What it used to draw was a white square inset a quarter of
// the box - at 13x13, an empty blue ring - so checked and unchecked differed by
// a border and a checked box read as unchecked.
//
// Counted in PIXELS rather than commands: the tick is a staircase of short
// rows, so "how many commands" says nothing about whether it looks like a tick,
// and a future implementation that draws it differently should still pass.
void test_a_checked_checkbox_draws_a_tick() {
    browser page{browser_options{400, 200}};
    page.load_html("<body><input type=checkbox id=on checked><input type=checkbox id=off></body>");
    check(page.frame().has_value(), "the page renders");

    const auto mark_pixels = [&page](std::string_view id) {
        const rect box = box_of(page, id);
        const auto image = page.read_pixels();
        std::size_t marks = 0;
        if (!image) { return marks; }
        // White INSIDE the box's border - the tick, or nothing.
        for (int y = static_cast<int>(box.y) + 2; y < static_cast<int>(box.bottom()) - 2; ++y) {
            const auto row = image->row(y);
            for (int x = static_cast<int>(box.x) + 2; x < static_cast<int>(box.right()) - 2; ++x) {
                if ((row[static_cast<std::size_t>(x)] & 0x00FFFFFFU) == 0x00FFFFFFU) { ++marks; }
            }
        }
        return marks;
    };

    // An unchecked box is white THROUGHOUT, so it has the most white of all -
    // the comparison that matters is that the checked one has some white (the
    // tick) but much less (the blue fill around it).
    const std::size_t on = mark_pixels("on");
    const std::size_t off = mark_pixels("off");
    check(on > 0, "a checked checkbox draws something in the mark colour");
    check(on < off / 2, "but far less than an unchecked one, which is white throughout");

    // ...and it is not a solid block: a tick leaves most of the box coloured.
    const rect box = box_of(page, "on");
    const auto inside = static_cast<std::size_t>((box.width - 4) * (box.height - 4));
    check(on < inside / 2, "the mark is a stroke, not a filled square");
}

void test_a_radio_is_round_and_a_checkbox_is_not() {
    browser page{browser_options{400, 200}};
    page.load_html(
        "<body><input type=radio id=r checked><input type=checkbox id=c checked></body>");
    check(page.frame().has_value(), "the page renders");

    std::size_t radio_ellipses = 0;
    std::size_t checkbox_ellipses = 0;
    std::size_t checkbox_rects = 0;
    for (const auto & c : commands(page)) {
        if (c.source == find_id(page, "r") && c.op == paint::paint_op::fill_ellipse) {
            ++radio_ellipses;
        }
        if (c.source == find_id(page, "c")) {
            if (c.op == paint::paint_op::fill_ellipse) { ++checkbox_ellipses; }
            if (c.op == paint::paint_op::fill_rect) { ++checkbox_rects; }
        }
    }
    // Drawn as squares the two controls are indistinguishable, and the shape is
    // what tells you one of them is exclusive.
    check(radio_ellipses >= 2, "a radio is drawn with ellipses");
    check(checkbox_ellipses == 0, "a checkbox is not");
    check(checkbox_rects > 0, "it is drawn with rectangles");
}

void test_a_button_shows_its_label() {
    browser page{browser_options{400, 200}};
    page.load_html("<body><button id=b>Send it</button><select id=s><option>red</option></select>"
                   "</body>");
    check(page.frame().has_value(), "the page renders");

    // A BUTTON IS NOT A SELECT. They shared one painter arm, so a button was
    // asked for its selected <option> - it has none, so the label came out
    // empty - and then got the drop-down arrow anyway. Every button on the
    // page was an empty box with an arrow in it.
    check(draws_text(page, "Send it"), "the button draws its label");
    check(arrow_rows(page, "b") == 0, "and has NO drop-down arrow");
    check(arrow_rows(page, "s") > 0, "while the select still has one");
}

void test_a_submit_button_has_a_default_label() {
    browser page{browser_options{400, 200}};
    page.load_html("<body><input type=submit id=s><input type=reset id=r></body>");
    check(page.frame().has_value(), "the page renders");
    // <input type=submit> has no children to take a label from, so the UA
    // supplies one - an unlabelled grey box is not a submit button.
    check(draws_text(page, "Submit"), "submit is labelled");
    check(draws_text(page, "Reset"), "and so is reset");
}

// The caret must be measured with the font that DRAWS the text. Verified with
// real fonts on purpose: font8x8 quantises every size to the same cell, so the
// two measurements agree there and the bug is invisible.
void test_the_caret_is_measured_with_the_drawing_font() {
    if (!raster::ttf_available()) {
        std::printf("     no SDL3_ttf in this build - real-font checks skipped\n");
        return;
    }
    browser page{browser_options{400, 200}};
    check(page.use_real_fonts(), "the vendored faces load");
    page.load_html("<body><input type=text id=f style='font-family:serif'></body>");
    check(page.frame().has_value(), "the page renders");
    const rect field = box_of(page, "f");
    (void)page.handle(input_event::mouse_down_at(field.x + 4, field.y + 6));
    (void)page.handle(input_event::mouse_up_at(field.x + 4, field.y + 6));
    check(page.focused() == find_id(page, "f"), "the field is focused");

    check(page.text_input("abcd"), "typed");
    check(page.frame().has_value(), "and it redraws");
    const std::vector<rect> bars = caret_bars(page, "f");
    check(bars.size() == 1, "there is one caret");
    if (bars.size() != 1) { return; }

    // Where the caret SHOULD be: the width of "abcd" in the field's own font,
    // from the text's left edge. Measured through the browser's metrics, which
    // is the same object the rasterizer draws with.
    const float expected = page.metrics()("abcd", 16, layout::text_face{"serif", false, false});
    const float actual = bars[0].x - field.x;
    // Generous, because the inset is the painter's business - but nothing like
    // the 2x that measuring with font8x8 produced.
    check(std::fabs(actual - expected) < expected * 0.5f + 8,
          "the caret sits at the END of what was typed, not a font's width past it");
}

void test_a_textarea_shows_a_caret_on_the_right_line() {
    browser page{browser_options{400, 200}};
    page.load_html("<body><textarea id=t rows=3 cols=20>one\ntwo</textarea></body>");
    check(page.frame().has_value(), "the page renders");
    const rect box = box_of(page, "t");
    // Clicked on the SECOND line, which is also the click-to-position test:
    // the caret goes where the pointer was, not where it happened to be.
    (void)page.handle(input_event::mouse_down_at(box.x + 6, box.y + 28));
    (void)page.handle(input_event::mouse_up_at(box.x + 6, box.y + 28));
    check(page.focused() == find_id(page, "t"), "the textarea is focused");
    check(page.frame().has_value(), "it redraws");

    // Measured as one run the caret landed the width of "one\ntwo" past the
    // left edge - past the right edge of the box, where the clip threw it
    // away. That is why a textarea appeared to have no caret at all.
    const std::vector<rect> bars = caret_bars(page, "t");
    check(bars.size() == 1, "the textarea has a caret");
    if (bars.size() != 1) { return; }
    check(bars[0].x < box.right(), "and it is INSIDE the box, not clipped away past the end");
    check(bars[0].y > box.y + 8, "on the second line, not the first");
}

void test_a_textarea_draws_its_lines_separately() {
    browser page{browser_options{400, 200}};
    page.load_html("<body><textarea id=t rows=3 cols=20>one\ntwo</textarea></body>");
    check(page.frame().has_value(), "the page renders");
    // Drawn as one run, the newline reaches the rasterizer as a glyph - a box,
    // with a real font - and both words end up on one line.
    check(draws_text(page, "one"), "the first line is drawn");
    check(draws_text(page, "two"), "the second line is drawn");
    check(!draws_text(page, "one\ntwo"), "and NOT as a single run with a newline in it");

    float first = -1;
    float second = -1;
    for (const auto & c : commands(page)) {
        if (c.op != paint::paint_op::text_run) { continue; }
        if (c.text == "one") { first = c.bounds.y; }
        if (c.text == "two") { second = c.bounds.y; }
    }
    check(first >= 0 && second > first, "the second line is BELOW the first");
}

// A textarea SOFT-WRAPS a line too long for it.
//
// value_lines split on '\n' and nothing else, so a paragraph with no newline in
// it was one line however long, drawn straight through the right edge of the
// box and clipped. This is the last thing on docs/history/v1-retirement.md's list that
// the deleted engine had and this one did not.
//
// Deliberately NOT use_real_fonts(): font8x8's advance is exactly
// 8 * round(size/8) per glyph, so where the break falls is the same number on
// every machine, and this test does not need SDL3_ttf to mean something.
void test_a_textarea_soft_wraps_a_long_line() {
    browser page{browser_options{400, 200}};
    page.load_html("<body><textarea id=t rows=4 cols=10>aaaaaaaaaa bbbbbbbbbb</textarea></body>");
    check(page.frame().has_value(), "the page renders");

    // The whole value cannot fit: cols=10 at 16px gives an inner width of
    // about nine glyphs, and the value is 21.
    check(!draws_text(page, "aaaaaaaaaa bbbbbbbbbb"),
          "the value is NOT drawn as one run running off the box");

    std::vector<rect> runs;
    std::string rebuilt;
    for (const auto & c : commands(page)) {
        if (c.op != paint::paint_op::text_run || c.source != find_id(page, "t")) { continue; }
        runs.push_back(c.bounds);
        rebuilt += c.text;
    }
    check(runs.size() >= 2, "it is drawn as two or more lines");
    if (runs.size() >= 2) { check(runs[1].y > runs[0].y, "the second line is BELOW the first"); }
    // A soft break consumes NO character, unlike a '\n'. If the wrapper ate the
    // space it broke at, this is where it shows.
    check(rebuilt == "aaaaaaaaaa bbbbbbbbbb", "and the lines still spell the whole value");
}

// The regression guard for the boundary case the wrap creates. After a soft
// break line n's end IS line n+1's begin, so a caret sitting exactly there
// satisfies both lines' range test - and the painter, which loops over lines,
// drew a bar on each.
void test_a_soft_wrapped_textarea_shows_exactly_one_caret() {
    browser page{browser_options{400, 200}};
    // Words that FIT, so every caret position lands well inside the box and
    // caret_bars can see it. (An unbreakable word wider than the field
    // overflows by design, and a caret at its end sits on the frame, where the
    // helper cannot tell it from the outline.)
    page.load_html(
        "<body><textarea id=t rows=4 cols=20>aaaa bbbb cccc dddd eeee</textarea></body>");
    check(page.frame().has_value(), "the page renders");
    const rect box = box_of(page, "t");
    click(page, box.x + 4, box.y + 8); // into the first visual line
    check(page.focused() == find_id(page, "t"), "the textarea is focused");
    (void)page.handle(input_event::key_press("Home"));

    // Walk the caret across the whole value. At no position may there be two
    // carets - and the wrap boundary is one of the positions visited.
    for (std::size_t step = 0; step <= 24; ++step) {
        (void)page.frame(); // the display list is what caret_bars reads
        const std::size_t bars = caret_bars(page, "t").size();
        check(bars == 1, "exactly one caret is drawn at every offset");
        if (bars != 1) { break; }
        (void)page.handle(input_event::key_press("ArrowRight"));
    }
}

// Click and paint must agree about which line is which, including after a soft
// break - the whole reason the geometry lives in one place.
void test_clicking_the_second_visual_line_of_a_wrapped_textarea() {
    browser page{browser_options{400, 200}};
    page.load_html("<body><textarea id=t rows=4 cols=10>aaaaaaaaaa bbbbbbbbbb</textarea></body>");
    check(page.frame().has_value(), "the page renders");
    const rect box = box_of(page, "t");

    // Find where the painter actually put the second line, and click there
    // rather than guessing a y.
    std::vector<rect> runs;
    for (const auto & c : commands(page)) {
        if (c.op == paint::paint_op::text_run && c.source == find_id(page, "t")) {
            runs.push_back(c.bounds);
        }
    }
    check(runs.size() >= 2, "the value wrapped");
    if (runs.size() < 2) { return; }

    click(page, box.x + 4, runs[1].y + runs[1].height / 2);
    // Past the break, which is at offset 11 ("aaaaaaaaaa " is 11 bytes).
    check(caret_of(page, "t") >= 10, "clicking the second visual line lands past the wrap");

    // And ArrowUp from there comes back to the first line.
    (void)page.handle(input_event::key_press("ArrowUp"));
    check(caret_of(page, "t") <= 11, "and ArrowUp returns to the first visual line");
}

// A textarea is sized by `rows` and does not grow, so wrapping past the bottom
// has to scroll or the caret types somewhere nobody can see.
void test_a_textarea_scrolls_to_keep_the_caret_visible() {
    browser page{browser_options{400, 200}};
    page.load_html("<body><textarea id=t rows=2 cols=10></textarea></body>");
    check(page.frame().has_value(), "the page renders");
    const rect box = box_of(page, "t");
    click(page, box.x + 4, box.y + 8);
    check(page.focused() == find_id(page, "t"), "the textarea is focused");

    const auto scroll_of = [&page] {
        const auto * state = page.control_state_of(find_id(page, "t"));
        return state == nullptr ? 0 : state->scroll_line;
    };
    check(scroll_of() == 0, "it starts at the top");

    // Six words at ~9 glyphs a line is well past two visible rows.
    check(page.text_input("aaaa bbbb cccc dddd eeee ffff"), "typing is accepted");
    (void)page.frame();
    check(scroll_of() > 0, "typing past the last visible row scrolls the textarea");

    // The caret's line is still inside the visible window - which is the point
    // of scrolling at all - so a caret is still drawn.
    check(caret_bars(page, "t").size() == 1, "and the caret is still visible");

    // Home to the top of the value brings it back.
    for (int i = 0; i < 40; ++i) { (void)page.handle(input_event::key_press("ArrowUp")); }
    (void)page.frame();
    check(scroll_of() == 0, "and moving back up scrolls it home again");
}

// --- what a script can read off a control ---------------------------------

void test_script_reads_a_live_control_value() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<body><input type=text id=f value=start>
    <select id=s><option value=red>red</option><option value=green selected>green</option></select>
    <script>
    var f = document.getElementById('f');
    var s = document.getElementById('s');
    function report() { console.log(f.value + '/' + s.value); }
    </script></body>)");
    check(page.script_error().empty(), "the script ran");
    check(page.frame().has_value(), "the page renders");

    // A wrapper's properties were a SNAPSHOT taken when it was made, so a page
    // that kept the element in a variable - which every page does - read the
    // page-load value forever. The widget gallery reported `color: undefined`.
    check(page.run_script("report()"), "the script call runs");
    check(!log_of(page).empty(), "it logged");
    if (!log_of(page).empty()) {
        check(log_of(page).back() == "start/green", "value and the selected option are readable");
    }

    const rect field = box_of(page, "f");
    // Clicked PAST the end of the text, so the caret lands after it - a click
    // places the caret now, and clicking at the left edge would insert there.
    (void)page.handle(input_event::mouse_down_at(field.right() - 8, field.y + 6));
    (void)page.handle(input_event::mouse_up_at(field.right() - 8, field.y + 6));
    check(page.text_input("!"), "typed into it");
    check(page.run_script("report()"), "the script call runs again");
    check(log_of(page).back() == "start!/green", "and now reads what was TYPED, not the snapshot");
}

void test_an_input_listener_sees_the_new_value() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<body><input type=text id=f><script>
    var f = document.getElementById('f');
    f.addEventListener('input', function () { console.log('now:' + f.value); });
    </script></body>)");
    check(page.frame().has_value(), "the page renders");
    const rect field = box_of(page, "f");
    (void)page.handle(input_event::mouse_down_at(field.x + 4, field.y + 6));
    (void)page.handle(input_event::mouse_up_at(field.x + 4, field.y + 6));
    check(page.text_input("a"), "typed");
    check(page.text_input("b"), "typed again");
    // The handler runs AFTER the edit, so it must see the character that
    // caused it. Refreshing wrappers only after layout would show the value
    // from before the keystroke.
    check(log_of(page).size() == 2, "the listener fired per keystroke");
    if (log_of(page).size() == 2) {
        check(log_of(page)[0] == "now:a" && log_of(page)[1] == "now:ab",
              "each one sees the value INCLUDING the character that fired it");
    }
}

void test_script_writes_a_control_value() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<body><input type=text id=f value=old>
    <input type=checkbox id=c></body>)");
    check(page.frame().has_value(), "the page renders");
    check(draws_text(page, "old"), "the field shows its attribute value");

    check(page.run_script("document.getElementById('f').value = 'new';"
                          "document.getElementById('c').checked = true;"),
          "the script runs");
    check(page.frame().has_value(), "the page redraws");
    // The VM has no property accessors, so a write is a sync rather than a
    // setter - without the write-back this sets a property nothing reads and
    // the field keeps showing the old text.
    check(draws_text(page, "new"), "the field shows what the script assigned");
    check(!draws_text(page, "old"), "and not what it used to say");
    check(page.run_script("console.log(String(document.getElementById('c').checked));"),
          "reads it back");
    check(!log_of(page).empty() && log_of(page).back() == "true", "the checkbox is checked");
}

void test_one_wrapper_per_element() {
    browser page{browser_options{300, 150}};
    page.load_html(R"(<body><p id=p>x</p><script>
    console.log(String(document.getElementById('p') === document.getElementById('p')));
    </script></body>)");
    check(!log_of(page).empty(), "it logged");
    // A browser hands out the SAME object every time. Two wrappers for one
    // element also meant two independent property snapshots.
    if (!log_of(page).empty()) { check(log_of(page).back() == "true", "one element, one wrapper"); }
}

} // namespace

int main() {
    test_a_control_draws_in_the_face_it_measures();
    test_clicking_in_a_textarea_places_the_caret();
    test_arrows_move_by_visual_line_in_a_textarea();
    test_dragging_selects_inside_a_field();
    test_escape_and_blur_drop_a_field_selection();
    test_a_password_shows_bullets();
    test_a_password_caret_is_measured_on_the_bullets();
    test_a_disabled_control_looks_and_acts_disabled();
    test_a_checked_checkbox_draws_a_tick();
    test_a_radio_is_round_and_a_checkbox_is_not();
    test_a_button_shows_its_label();
    test_a_submit_button_has_a_default_label();
    test_the_caret_is_measured_with_the_drawing_font();
    test_a_textarea_shows_a_caret_on_the_right_line();
    test_a_textarea_draws_its_lines_separately();
    test_a_textarea_soft_wraps_a_long_line();
    test_a_soft_wrapped_textarea_shows_exactly_one_caret();
    test_clicking_the_second_visual_line_of_a_wrapped_textarea();
    test_a_textarea_scrolls_to_keep_the_caret_visible();
    test_script_reads_a_live_control_value();
    test_an_input_listener_sees_the_new_value();
    test_script_writes_a_control_value();
    test_one_wrapper_per_element();
    REPORT("control_editing");
}
