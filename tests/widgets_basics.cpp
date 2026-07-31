// Form controls and the 2D canvas.
//
// Both are places where the engine draws something the document did not
// describe, and both keep their state OFF the DOM node - a control's value and
// caret in a form_store, a canvas's pixels in a canvas_store, each keyed by
// node_id. the previous engine put all of it on `node`, which is what left thirty layout- and
// UI-only fields on a struct that is supposed to be document content.
//
// The tests check behaviour through the browser rather than against the stores,
// because a control whose value changes without the page redrawing is not a
// working control, whatever a unit test of the store says.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser::shell::control_kind;
using ctbrowser::shell::input_event;

namespace {

void check(bool ok, std::string_view what) {
    if (!ok) {
        std::printf("FAIL %s\n", std::string{what}.c_str());
        ++ctbrowser_test_failures;
    }
}

[[nodiscard]] node_id find_id(browser & page, std::string_view want) {
    const auto txn = page.doc().read();
    const atom key = page.atoms().intern("id");
    node_id found{};
    const auto walk = [&](auto && self, node_id at) -> void {
        if (!found && txn.attribute_value(at, key) == want) { found = at; }
        for (const node_id c : txn.children(at)) { self(self, c); }
    };
    walk(walk, txn.root());
    return found;
}

[[nodiscard]] std::string value_of(browser & page, std::string_view id) {
    const auto txn = page.doc().read();
    const node_id node = find_id(page, id);
    return node ? page.forms().state_of(txn, page.atoms(), node).value : std::string{};
}

[[nodiscard]] bool checked_of(browser & page, std::string_view id) {
    const auto txn = page.doc().read();
    const node_id node = find_id(page, id);
    return node && page.forms().state_of(txn, page.atoms(), node).checked;
}

// Where a control ended up, so a test can click it rather than guessing.
[[nodiscard]] rect box_of(browser & page, std::string_view id) {
    const node_id want = find_id(page, id);
    const auto walk = [&](auto && self, const layout::fragment & f, float dx, float dy) -> rect {
        const rect box{f.bounds.x + dx, f.bounds.y + dy, f.bounds.width, f.bounds.height};
        if (f.source == want && !box.empty()) { return box; }
        for (const auto & child : f.children) {
            if (const rect hit = self(self, child, box.x, box.y); !hit.empty()) { return hit; }
        }
        return rect{};
    };
    return walk(walk, page.fragments(), 0, 0);
}

void click(browser & page, const rect & where) {
    const float x = where.x + where.width / 2;
    const float y = where.y + where.height / 2;
    (void)page.handle(input_event::mouse_down_at(x, y));
    (void)page.handle(input_event::mouse_up_at(x, y));
}

// --- controls exist, are sized, and are drawn -----------------------------

void test_controls_are_sized_by_the_element() {
    browser page{browser_options{600, 300}};
    page.load_html("<html><body><input id=t type=text><input id=c type=checkbox>"
                   "<button id=b>Press</button></body></html>");
    check(page.frame().has_value(), "the page renders");

    const rect text_box = box_of(page, "t");
    const rect check_box = box_of(page, "c");
    const rect button_box = box_of(page, "b");

    // A REPLACED element is sized by what it is, not by what it contains.
    // Laying one out from its children gives a zero-height control, and taking
    // the block width gives a checkbox as wide as the page - which is what the
    // UA sheet's blanket `input { width: 160px }` did until it was removed.
    check(!text_box.empty() && text_box.width > 60, "a text field has a real width");
    check(check_box.width > 8 && check_box.width < 30, "a checkbox is small and square");
    check(check_box.height > 8 && check_box.height < 30, "in both directions");
    check(check_box.width < text_box.width / 3, "and much narrower than a text field");
    // A button is as wide as its label - the one replaced element whose size
    // does come from its content.
    check(button_box.width >= raster::font8x8_advance("Press", 16),
          "a button is at least as wide as its label");
    check(button_box.width < 400, "but not as wide as the page");
}

// A SELECT IS AS WIDE AS WHAT IT SHOWS. It used to be a fixed twelve
// characters whatever it held, so a three-letter colour picker was as wide as
// a list of country names, and neither was the size it should have been.
void test_a_select_is_as_wide_as_its_widest_option() {
    browser page{browser_options{600, 300}};
    page.load_html("<html><body>"
                   "<select id=short><option>red</option><option>green</option></select>"
                   "<select id=long><option>red</option>"
                   "<option>a considerably longer option</option></select>"
                   "</body></html>");
    check(page.frame().has_value(), "the page renders");

    const rect small = box_of(page, "short");
    const rect big = box_of(page, "long");
    check(!small.empty() && !big.empty(), "both selects are laid out");
    check(big.width > small.width * 2, "the one with a long option is much wider");
    // The WIDEST option decides it, not the first or the selected one - `red`
    // is first in both.
    check(big.width >= raster::font8x8_advance("a considerably longer option", 16),
          "wide enough for its longest label");
    check(small.width >= raster::font8x8_advance("green", 16), "and so is the short one");
    // The old rule was a flat twelve characters regardless of content. Stated
    // against the measured width of twelve characters rather than a pixel
    // count, so it holds whichever font backend is in use.
    check(small.width < raster::font8x8_advance("000000000000", 16),
          "but not padded out to the old fixed twelve characters");
}

// Layout reserves room around a control's text and the painter insets by the
// same amount. They were two separate numbers - 4 per side reserved, 6 drawn -
// so text started inside the space set aside for it.
void test_a_control_reserves_the_room_its_text_is_drawn_in() {
    browser page{browser_options{600, 300}};
    page.load_html("<html><body><input id=t size=10 value=0000000000></body></html>");
    check(page.frame().has_value(), "the page renders");

    const rect box = box_of(page, "t");
    const float text = raster::font8x8_advance("0000000000", 16);
    // Ten characters of value in a size=10 field: the box has to hold the text
    // plus the inset on BOTH sides, or the last character is clipped by a
    // field that claimed it would fit.
    check(box.width >= text + 2 * layout::control_text_inset,
          "the box holds its full value plus the inset on both sides");
}

void test_a_seeded_value_is_drawn() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><body><input id=t type=text value="hello"></body></html>)");
    check(page.frame().has_value(), "the page renders");
    // The value comes from the ATTRIBUTE until the user edits it.
    check(value_of(page, "t") == "hello", "the value attribute seeds the control");

    bool drawn = false;
    for (const auto & layer : page.layers().layers) {
        if (!layer.contents) { continue; }
        for (const auto & c : layer.contents->commands()) {
            if (c.op == paint::paint_op::text_run && c.text == "hello") { drawn = true; }
        }
    }
    check(drawn, "and it is painted into the field");
}

// --- editing ---------------------------------------------------------------

void test_typing_and_editing() {
    browser page{browser_options{400, 200}};
    page.load_html("<html><body><input id=t type=text></body></html>");
    check(page.frame().has_value(), "the page renders");

    // Nothing typed goes anywhere until something has focus.
    check(!page.text_input("ignored"), "typing with no focus does nothing");
    check(value_of(page, "t").empty(), "and the field is untouched");

    click(page, box_of(page, "t"));
    check(page.focused() == find_id(page, "t"), "clicking the field focuses it");
    check(page.text_input("abc"), "typing is accepted");
    check(value_of(page, "t") == "abc", "and lands in the value");

    check(page.handle(input_event::key_press("Backspace")), "backspace is handled");
    check(value_of(page, "t") == "ab", "and removes the last character");

    check(page.handle(input_event::key_press("Home")), "Home is handled");
    check(page.text_input("X"), "typing at the caret");
    check(value_of(page, "t") == "Xab", "inserts rather than appends");

    check(page.handle(input_event::key_press("End")), "End is handled");
    check(page.text_input("Z"), "typing at the end");
    check(value_of(page, "t") == "XabZ", "appends");
}

// --- Tab walks the form ---------------------------------------------------

// The order is DOCUMENT order, and the two things that must be left out of it
// are the two a user cannot reach: a disabled control and one that is not
// rendered. Both used to be reachable for the simple reason that nothing was.
void test_tab_moves_focus_between_controls() {
    browser page{browser_options{400, 300}};
    page.load_html("<html><body>"
                   "<input id=a type=text>"
                   "<input id=b type=text disabled>"
                   "<input id=c type=text style='display:none'>"
                   "<textarea id=d></textarea>"
                   "<button id=e>Go</button>"
                   "</body></html>");
    check(page.frame().has_value(), "the page renders");

    click(page, box_of(page, "a"));
    check(page.focused() == find_id(page, "a"), "clicking the first field focuses it");

    check(page.handle(input_event::key_press("Tab")), "Tab is handled");
    check(page.focused() == find_id(page, "d"),
          "Tab skips the disabled and the unrendered control");

    check(page.handle(input_event::key_press("Tab")), "Tab again");
    check(page.focused() == find_id(page, "e"), "and reaches the button");

    check(page.handle(input_event::key_press("Tab")), "Tab off the end");
    check(page.focused() == find_id(page, "a"), "wraps around to the first");

    check(page.handle(input_event::key_press("Tab", /*shift=*/true)), "Shift+Tab is handled");
    check(page.focused() == find_id(page, "e"), "and goes backwards, wrapping the other way");

    check(page.handle(input_event::key_press("Tab", true)), "Shift+Tab again");
    check(page.focused() == find_id(page, "d"), "stepping back through the order");
}

void test_tab_with_nothing_focused_starts_at_an_end() {
    browser page{browser_options{400, 300}};
    page.load_html("<html><body><input id=a><input id=b></body></html>");
    check(page.frame().has_value(), "the page renders");

    check(!page.focused(), "nothing is focused to begin with");
    check(page.handle(input_event::key_press("Tab")), "Tab is handled");
    check(page.focused() == find_id(page, "a"), "Tab starts at the first control");

    browser back{browser_options{400, 300}};
    back.load_html("<html><body><input id=a><input id=b></body></html>");
    check(back.frame().has_value(), "the page renders");
    check(back.handle(input_event::key_press("Tab", true)), "Shift+Tab is handled");
    check(back.focused() == find_id(back, "b"), "and Shift+Tab starts at the last");
}

// Tab is a DEFAULT ACTION, so it must not also scroll the page - the bug every
// key here has had at least once - and a page that claims the key keeps it.
void test_tab_does_not_scroll_and_can_be_prevented() {
    browser page{browser_options{200, 100}};
    page.load_html("<html><body><input id=a><input id=b>"
                   "<div style='height:900px'>tall</div></body></html>");
    check(page.frame().has_value(), "the page renders");
    check(page.max_scroll() > 0, "the page is scrollable, so a stray scroll would show");

    click(page, box_of(page, "a"));
    (void)page.handle(input_event::key_press("Tab"));
    check(page.scroll_y() == 0, "Tab moved focus without scrolling the page");

    browser cancelled{browser_options{200, 100}};
    cancelled.load_html("<html><body><input id=a><input id=b>"
                        "<script>document.addEventListener('keydown', function(e) {"
                        "  if (e.code == 'Tab') { e.preventDefault(); }"
                        "}, false);</script></body></html>");
    check(cancelled.frame().has_value(), "the page renders");
    click(cancelled, box_of(cancelled, "a"));
    const node_id before = cancelled.focused();
    check(before == find_id(cancelled, "a"), "the first field is focused");
    (void)cancelled.handle(input_event::key_press("Tab"));
    check(cancelled.focused() == before, "a page that preventDefaults Tab keeps focus");
}

// The other half of Tab: the key must not arrive as TEXT. Nothing reachable
// through SDL produces it, but the headless path does, and a control character
// inserted into a field is invisible and permanent.
void test_typing_a_control_character_inserts_nothing() {
    browser page{browser_options{400, 200}};
    page.load_html("<html><body><input id=t type=text></body></html>");
    check(page.frame().has_value(), "the page renders");

    click(page, box_of(page, "t"));
    check(page.text_input("ab"), "ordinary text is accepted");
    check(!page.text_input("\t"), "a tab is not text and is refused");
    check(value_of(page, "t") == "ab", "and nothing was inserted");
    check(page.text_input("c\td"), "text around a control character is still accepted");
    check(value_of(page, "t") == "abcd", "with the control character dropped");
}

void test_backspace_deletes_a_whole_code_point() {
    browser page{browser_options{400, 200}};
    page.load_html("<html><body><input id=t type=text></body></html>");
    check(page.frame().has_value(), "the page renders");
    click(page, box_of(page, "t"));
    check(page.text_input("a\xc3\xa9"), "a multi-byte character is typed"); // 'a' + U+00E9
    check(value_of(page, "t") == "a\xc3\xa9", "and stored whole");
    (void)page.handle(input_event::key_press("Backspace"));
    // Deleting one BYTE would leave invalid UTF-8, which then renders as
    // replacement characters for the rest of the field.
    check(value_of(page, "t") == "a", "backspace removes the whole code point");
}

void test_editing_keys_do_not_scroll_the_page() {
    browser page{browser_options{300, 120}};
    page.load_html("<html><body><input id=t type=text value=abc>"
                   "<div style='height:600px'>tall</div></body></html>");
    check(page.frame().has_value(), "the page renders");
    click(page, box_of(page, "t"));
    const float before = page.scroll_y();
    (void)page.handle(input_event::key_press("Home"));
    (void)page.handle(input_event::key_press("End"));
    // Home in a focused field moves the caret. If it scrolled the page too,
    // every keystroke in a form would jump the document.
    check(page.scroll_y() == before, "Home and End move the caret, not the page");
}

void test_selection_and_replacement() {
    browser page{browser_options{400, 200}};
    page.load_html("<html><body><input id=t type=text value=hello></body></html>");
    check(page.frame().has_value(), "the page renders");
    click(page, box_of(page, "t"));
    // Ctrl+A, as the platform actually delivers it: the physical key plus the
    // modifier, not a made-up "SelectAll" name no keyboard produces.
    (void)page.handle(input_event::key_press("KeyA", false, true));
    check(page.text_input("bye"), "typing over a selection");
    check(value_of(page, "t") == "bye", "replaces it");
}

// --- checkboxes, radios and forms -----------------------------------------

// --- labels -----------------------------------------------------------------

// Clicking a point rather than a box centre. The centre of a <label> that wraps
// its control can land on the CONTROL, which would pass whether or not labels
// work at all - the whole point is to hit the label's TEXT.
void click_at(browser & page, float x, float y) {
    (void)page.handle(input_event::mouse_down_at(x, y));
    (void)page.handle(input_event::mouse_up_at(x, y));
}

// The IMPLICIT form, <label><input> text</label>, which is what widgets.html
// uses for its checkboxes and radios. The control is a SIBLING of the clicked
// text, so the upward walk that finds a control from a click can never see it -
// clicking "large" used to blur whatever was focused and do nothing else.
void test_clicking_label_text_activates_the_control() {
    browser page{browser_options{400, 300}};
    page.load_html("<html><body>"
                   "<label id=la><input type=radio name=size id=s1 checked> small</label>"
                   "<label id=lb><input type=radio name=size id=s2> large</label>"
                   "</body></html>");
    check(page.frame().has_value(), "the page renders");
    check(checked_of(page, "s1"), "the first radio starts checked");

    // The right-hand end of the second label's box is its text, well clear of
    // the radio at the left-hand end.
    const rect label = box_of(page, "lb");
    const rect radio = box_of(page, "s2");
    check(label.width > radio.width, "the label is wider than its control");
    click_at(page, label.right() - 4, label.y + label.height / 2);

    check(checked_of(page, "s2"), "clicking the label's TEXT checks the radio");
    check(!checked_of(page, "s1"), "and the other radio in the group is cleared");
    check(page.focused() == find_id(page, "s2"), "and the control takes focus");
}

// A checkbox toggles, so the same click twice must not land twice. This is the
// guard on resolving a click only ONCE: a press on the control inside a label
// finds it on the upward walk and must never also resolve through the label.
void test_clicking_a_checkbox_inside_a_label_toggles_once() {
    browser page{browser_options{400, 300}};
    page.load_html("<html><body>"
                   "<label id=l><input type=checkbox id=c> option one</label>"
                   "</body></html>");
    check(page.frame().has_value(), "the page renders");
    check(!checked_of(page, "c"), "it starts unchecked");

    const rect box = box_of(page, "c");
    click_at(page, box.x + box.width / 2, box.y + box.height / 2);
    check(checked_of(page, "c"), "clicking the box itself checks it");

    const rect label = box_of(page, "l");
    click_at(page, label.right() - 4, label.y + label.height / 2);
    check(!checked_of(page, "c"), "and clicking its label text unchecks it again");
}

// The EXPLICIT form, <label for=id>. widgets.html labels its text fields this
// way, so both have to work for that page to behave.
void test_a_label_for_a_field_focuses_it_without_disturbing_it() {
    browser page{browser_options{400, 300}};
    page.load_html("<html><body>"
                   "<label id=l for=name>name</label> <input type=text id=name value=ada>"
                   "</body></html>");
    check(page.frame().has_value(), "the page renders");

    // Put the caret somewhere specific first, then click the label.
    click(page, box_of(page, "name"));
    check(page.handle(input_event::key_press("Home")), "Home is handled");
    check(page.focused() == find_id(page, "name"), "the field is focused");

    // Focus elsewhere so the click has something to move.
    click_at(page, 2, 290);
    const rect label = box_of(page, "l");
    click_at(page, label.x + label.width / 2, label.y + label.height / 2);
    check(page.focused() == find_id(page, "name"), "clicking the label focuses the field");

    // ...and does NOT drop a caret wherever the label happened to be. The
    // pointer was over the label's glyphs, not the value's.
    const auto * state = page.control_state_of(find_id(page, "name"));
    check(state != nullptr, "the field has state");
    if (state != nullptr) {
        check(state->caret == 0, "the caret is left where it was, not moved to the click");
        check(state->selection == state->caret, "and no selection was begun");
    }
}

// A `for` that names nothing labels NOTHING - it must not quietly fall back to
// a control the label happens to contain. And a label around a disabled control
// stays inert, because activate() already refuses those.
void test_a_label_that_resolves_to_nothing_does_nothing() {
    browser page{browser_options{400, 300}};
    page.load_html("<html><body>"
                   "<label id=l for=nosuch><input type=checkbox id=c> nope</label>"
                   "<label id=d><input type=checkbox id=e disabled> off</label>"
                   "</body></html>");
    check(page.frame().has_value(), "the page renders");

    const rect dangling = box_of(page, "l");
    click_at(page, dangling.right() - 4, dangling.y + dangling.height / 2);
    check(!checked_of(page, "c"), "an unresolvable for= does not fall back to a contained control");

    const rect disabled = box_of(page, "d");
    click_at(page, disabled.right() - 4, disabled.y + disabled.height / 2);
    check(!checked_of(page, "e"), "and a label around a disabled control is inert");
    check(page.focused() != find_id(page, "e"), "which also takes no focus");
}

void test_checkbox_toggles_on_click() {
    browser page{browser_options{400, 200}};
    page.load_html("<html><body><input id=c type=checkbox></body></html>");
    check(page.frame().has_value(), "the page renders");
    check(!checked_of(page, "c"), "it starts unchecked");
    click(page, box_of(page, "c"));
    check(checked_of(page, "c"), "clicking checks it");
    check(page.frame().has_value(), "the frame after renders");
    click(page, box_of(page, "c"));
    check(!checked_of(page, "c"), "clicking again unchecks it");
}

void test_radios_are_exclusive_within_a_name() {
    browser page{browser_options{500, 200}};
    page.load_html("<html><body>"
                   "<input id=a type=radio name=g><input id=b type=radio name=g>"
                   "<input id=c type=radio name=other></body></html>");
    check(page.frame().has_value(), "the page renders");
    click(page, box_of(page, "a"));
    click(page, box_of(page, "c"));
    check(checked_of(page, "a") && checked_of(page, "c"), "different groups are independent");
    click(page, box_of(page, "b"));
    // Selecting one radio clears every other radio with the SAME name, and
    // nothing else. Getting the grouping wrong makes a form with two radio
    // groups behave as one.
    check(checked_of(page, "b"), "the clicked radio is selected");
    check(!checked_of(page, "a"), "its group-mate is cleared");
    check(checked_of(page, "c"), "and the other group is untouched");
}

void test_form_submission_collects_successful_controls() {
    browser page{browser_options{600, 300}};
    page.load_html(R"(<html><body><form id=f>
    <input name=user type=text value=alice>
    <input name=agree type=checkbox checked>
    <input name=maybe type=checkbox>
    <input name=colour type=radio value=red>
    <button id=go type=submit>Go</button>
    </form></body></html>)");
    check(page.frame().has_value(), "the page renders");
    click(page, box_of(page, "go"));

    const auto & sent = page.last_submission();
    check(sent.size() == 2, "only successful controls are submitted");
    if (sent.size() >= 2) {
        check(sent[0].first == "user" && sent[0].second == "alice",
              "the text field, with its value");
        // An UNCHECKED box is not a successful control and is omitted entirely -
        // a server that sees the name at all knows it was ticked.
        check(sent[1].first == "agree", "the checked box");
    }
    bool has_maybe = false;
    for (const auto & [name, value] : sent) {
        if (name == "maybe" || name == "colour") { has_maybe = true; }
    }
    check(!has_maybe, "unchecked boxes and radios are omitted, not sent empty");
}

void test_reset_restores_the_markup() {
    browser page{browser_options{600, 300}};
    page.load_html(R"(<html><body><form id=f>
    <input id=t name=user type=text value=original>
    <button id=r type=reset>Reset</button>
    </form></body></html>)");
    check(page.frame().has_value(), "the page renders");
    click(page, box_of(page, "t"));
    check(page.text_input("!"), "the field is edited");
    check(value_of(page, "t") == "original!", "and shows the edit");
    click(page, box_of(page, "r"));
    check(value_of(page, "t") == "original", "reset restores the value attribute");
}

void test_submit_can_be_cancelled() {
    browser page{browser_options{600, 300}};
    page.load_html(R"(<html><body><form id=f>
    <input name=user type=text value=alice>
    <button id=go type=submit>Go</button>
    </form><script>
    document.getElementById('f').addEventListener('submit', function (e) {
      console.log('submitting'); e.preventDefault();
    });
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");
    click(page, box_of(page, "go"));
    check(page.bindings().console_output().size() == 1, "the submit listener ran");
    // preventDefault on submit is how every client-validated form works.
    check(page.last_submission().empty(), "and preventDefault stopped the submission");
}

// --- script sees and sets control state ------------------------------------

void test_script_reads_and_writes_values() {
    browser page{browser_options{500, 250}};
    page.load_html(R"(<html><body>
    <input id=t type=text value=start><input id=c type=checkbox checked>
    <script>
    var t = document.getElementById('t');
    console.log('read ' + t.getValue());
    t.setValue('written');
    console.log('checked ' + document.getElementById('c').isChecked());
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");
    const auto & log = page.bindings().console_output();
    check(log.size() == 2, "two console lines");
    if (log.size() == 2) {
        check(log[0] == "read start", "script reads the seeded value");
        check(log[1] == "checked true", "and a checkbox's state");
    }
    check(value_of(page, "t") == "written", "and a write from script takes effect");
}

void test_script_can_focus() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><body><input id=t type=text><script>
    document.getElementById('t').focus();
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");
    check(page.focused() == find_id(page, "t"), "focus() moves focus");
    check(page.text_input("typed"), "so typing goes to the field");
    check(value_of(page, "t") == "typed", "without the user having clicked it");
}

// --- canvas ---------------------------------------------------------------

[[nodiscard]] const paint::bitmap * canvas_of(browser & page, std::string_view id) {
    const shell::canvas_context * found = page.canvases().find(find_id(page, id));
    return found == nullptr ? nullptr : found->surface().get();
}

void test_canvas_is_sized_by_its_attributes() {
    browser page{browser_options{600, 400}};
    page.load_html("<html><body><canvas id=c width=200 height=90></canvas>"
                   "<canvas id=d></canvas><script>"
                   "document.getElementById('c').getContext('2d');"
                   "document.getElementById('d').getContext('2d');</script></body></html>");
    check(page.frame().has_value(), "the page renders");
    const rect box = box_of(page, "c");
    check(box.width == 200 && box.height == 90, "the canvas box is its attribute size");
    // The HTML defaults, which pages rely on when they omit the attributes.
    const paint::bitmap * defaulted = canvas_of(page, "d");
    check(defaulted != nullptr && defaulted->width == 300 && defaulted->height == 150,
          "a canvas with no attributes is 300x150");
}

void test_canvas_width_and_height_are_readable() {
    browser page{browser_options{400, 300}};
    page.load_html("<html><body><canvas id=c width=200 height=90></canvas>"
                   "<canvas id=d></canvas><script>"
                   "var c = document.getElementById('c');"
                   "var d = document.getElementById('d');"
                   "console.log(c.width + 'x' + c.height);"
                   "console.log(d.width + 'x' + d.height);"
                   "console.log('half=' + (c.width / 2));"
                   "</script></body></html>");
    check(page.frame().has_value(), "the page renders");
    const auto & log = page.bindings().console_output();
    check(log.size() == 3, "three console lines");
    if (log.size() != 3) { return; }
    check(log[0] == "200x90", "canvas.width and .height read the attributes");
    check(log[1] == "300x150", "and fall back to the HTML defaults");
    // The failure this guards is silent and total: without these they are
    // undefined, every coordinate computed from them is NaN, and the page draws
    // nothing while reporting no error at all. `canvas.width/2` is the first
    // line of most canvas pages.
    check(log[2] == "half=100", "so arithmetic on them works");
}

// getContext answers for 2d AND webgl, and gives null for everything else.
//
// This assertion has now been through three shapes, which is worth recording
// because each was right at the time:
//
//   1. null for webgl - and p5 kept the null, fell back to its 2D renderer, and
//      drew nothing 3D while reporting nothing.
//   2. a catchable Error for the whole WebGL family, so that failure was at
//      least loud while WEBGL was out of scope.
//   3. a real context for `webgl`, now that there is one.
//   4. NULL for `webgl2`, which is what the specification says an unsupported
//      context id returns - and what p5's `webgl2 || webgl` fallback needs in
//      order to reach the one that works. Shape 3 kept the throw for webgl2 on
//      the reasoning that it forced that fallback. It prevented it.
void test_getcontext_only_answers_for_2d() {
    browser page{browser_options{400, 300}};
    page.load_html("<html><body><canvas id=c></canvas><script>"
                   "var c = document.getElementById('c');"
                   "console.log('2d ' + (c.getContext('2d') != null));"
                   "console.log('webgl ' + (c.getContext('webgl') != null));"
                   "console.log('webgl2 ' + (c.getContext('webgl2') === null));"
                   "console.log('unknown ' + (c.getContext('nonsense') === null));"
                   "</script></body></html>");
    check(page.frame().has_value(), "the page renders");
    const auto & log = page.bindings().console_output();
    check(log.size() == 4, "four console lines");
    if (log.size() == 4) {
        check(log[0] == "2d true", "a 2d context exists");
        check(log[1] == "webgl true", "and so does a webgl one");
        check(log[2] == "webgl2 true", "webgl2 is null, not a context and not a throw");
        // Anything ELSE is still null - that is what an unknown context type
        // means, and a page testing for one expects null rather than a throw.
        check(log[3] == "unknown true", "an unknown context type is still null");
    }
}

// measureText is the one canvas method that changes no pixels, so it is not
// wrapped in draws() - and it was therefore the one method that never SYNCED
// the JS-side properties onto the context. A page that set ctx.font and then
// measured got whatever font the last DRAWING call had left behind, which on a
// first call is the 10px default rather than the font it just asked for.
//
// Checked with the bitmap font on purpose: font8x8's advance is exactly
// 8 * round(size/8) per glyph, so the numbers are the same on every machine
// and this test needs no SDL3_ttf.
void test_measuretext_reads_the_font_just_set() {
    browser page{browser_options{400, 300}};
    page.load_html("<html><body><canvas id=c width=300 height=100></canvas><script>"
                   "var ctx = document.getElementById('c').getContext('2d');"
                   "ctx.font = '40px sans-serif';"
                   "console.log('first=' + ctx.measureText('AA').width);"
                   "ctx.font = '16px sans-serif';"
                   "console.log('second=' + ctx.measureText('AA').width);"
                   "</script></body></html>");
    check(page.frame().has_value(), "the page renders");
    const auto & log = page.bindings().console_output();
    check(log.size() == 2, "two console lines");
    if (log.size() != 2) { return; }
    // 40px -> scale 5 -> 40px a glyph -> 80 for two. Before the fix this was
    // 16: the default 10px size, scale 1, 8px a glyph.
    check(log[0] == "first=80", "measureText on the FIRST call uses the font just set");
    check(log[1] == "second=32", "and a later change is picked up too");
}

// save()/restore() covers the whole drawing state, not just the transform.
//
// It used to cover only the transform, and for a reason that looked like a
// canvas bug and was really a bindings one: fillStyle, strokeStyle, lineWidth,
// globalAlpha and font are all JS PROPERTIES, sync() copies them onto the
// context before every call, and restore() popped the C++ stack without
// touching the object script reads. The next draw put the "restored" values
// straight back. The transform survived because it is the one piece of state
// with no property behind it.
void test_the_canvas_state_stack_restores_every_property() {
    browser page{browser_options{400, 300}};
    page.load_html("<html><body><canvas id=c width=300 height=100></canvas><script>"
                   "var ctx = document.getElementById('c').getContext('2d');"
                   "ctx.font = '16px sans-serif';"
                   "ctx.fillStyle = '#112233';"
                   "ctx.strokeStyle = '#445566';"
                   "ctx.lineWidth = 3;"
                   "ctx.globalAlpha = 0.5;"
                   "ctx.save();"
                   "ctx.font = '40px monospace';"
                   "ctx.fillStyle = '#aabbcc';"
                   "ctx.strokeStyle = '#ddeeff';"
                   "ctx.lineWidth = 9;"
                   "ctx.globalAlpha = 1;"
                   "ctx.restore();"
                   // Read back THROUGH the JS properties, which is what a page
                   // sees and what the next draw call re-reads.
                   "console.log('font=' + ctx.font);"
                   "console.log('fill=' + ctx.fillStyle);"
                   "console.log('stroke=' + ctx.strokeStyle);"
                   "console.log('lw=' + ctx.lineWidth);"
                   "console.log('alpha=' + ctx.globalAlpha);"
                   // And that the restored font is what actually MEASURES,
                   // i.e. the C++ side and the JS side agree after a restore.
                   "console.log('w=' + ctx.measureText('AA').width);"
                   "</script></body></html>");
    check(page.frame().has_value(), "the page renders");
    const auto & log = page.bindings().console_output();
    check(log.size() == 6, "six console lines");
    if (log.size() != 6) { return; }
    check(log[0] == "font=16px sans-serif", "restore() puts the font back");
    check(log[1] == "fill=#112233", "and fillStyle");
    check(log[2] == "stroke=#445566", "and strokeStyle");
    check(log[3] == "lw=3", "and lineWidth");
    check(log[4] == "alpha=0.5", "and globalAlpha");
    // 16px -> font8x8 scale 2 -> 16px a glyph -> 32 for two. If the write-back
    // and the C++ state had disagreed this would still be the 40px figure.
    check(log[5] == "w=32", "and the restored font is the one that measures");
}

// An unbalanced restore() must not corrupt the state - a page that restores
// more than it saved is common enough that it cannot be allowed to reset the
// context to defaults.
void test_an_unbalanced_canvas_restore_is_harmless() {
    browser page{browser_options{400, 300}};
    page.load_html("<html><body><canvas id=c width=300 height=100></canvas><script>"
                   "var ctx = document.getElementById('c').getContext('2d');"
                   "ctx.fillStyle = '#123456';"
                   "ctx.restore();" // nothing was saved
                   "console.log('fill=' + ctx.fillStyle);"
                   "</script></body></html>");
    check(page.frame().has_value(), "the page renders");
    const auto & log = page.bindings().console_output();
    check(log.size() == 1, "one console line");
    if (log.size() != 1) { return; }
    check(log[0] == "fill=#123456", "a restore with an empty stack changes nothing");
}

void test_canvas_drawing_reaches_the_pixels() {
    browser page{browser_options{400, 300}};
    page.load_html("<html><body><canvas id=c width=100 height=60></canvas><script>"
                   "var ctx = document.getElementById('c').getContext('2d');"
                   "ctx.fillStyle = '#ff0000';"
                   "ctx.fillRect(10, 10, 30, 20);"
                   "</script></body></html>");
    check(page.frame().has_value(), "the page renders");
    const paint::bitmap * pixels = canvas_of(page, "c");
    check(pixels != nullptr, "the canvas has a bitmap");
    if (pixels == nullptr) { return; }
    check(pixels->at(20, 20) == 0xFFFF0000u, "fillRect painted the interior");
    check(pixels->at(5, 5) == 0u, "and left the rest transparent");
    check(pixels->at(45, 20) == 0u, "including just past its right edge");
}

void test_fill_style_is_read_at_draw_time() {
    browser page{browser_options{400, 300}};
    page.load_html("<html><body><canvas id=c width=60 height=60></canvas><script>"
                   "var ctx = document.getElementById('c').getContext('2d');"
                   "ctx.fillStyle = '#00ff00'; ctx.fillRect(0, 0, 20, 20);"
                   "ctx.fillStyle = '#0000ff'; ctx.fillRect(30, 0, 20, 20);"
                   "</script></body></html>");
    check(page.frame().has_value(), "the page renders");
    const paint::bitmap * pixels = canvas_of(page, "c");
    check(pixels != nullptr, "the canvas has a bitmap");
    if (pixels == nullptr) { return; }
    // fillStyle is a PROPERTY the drawing calls read back. Capturing it when
    // the context is made, rather than at draw time, gives both rects the same
    // colour - and that is the whole canvas idiom broken.
    check(pixels->at(10, 10) == 0xFF00FF00u, "the first rect used the first fillStyle");
    check(pixels->at(35, 10) == 0xFF0000FFu, "and the second used the second");
}

void test_paths_transforms_and_clear() {
    browser page{browser_options{400, 300}};
    page.load_html("<html><body><canvas id=c width=80 height=80></canvas><script>"
                   "var ctx = document.getElementById('c').getContext('2d');"
                   "ctx.fillStyle = '#ffffff'; ctx.fillRect(0, 0, 80, 80);"
                   "ctx.translate(20, 20);"
                   "ctx.fillStyle = '#123456';"
                   "ctx.beginPath(); ctx.moveTo(0, 0); ctx.lineTo(20, 0);"
                   "ctx.lineTo(20, 20); ctx.lineTo(0, 20); ctx.closePath(); ctx.fill();"
                   "ctx.resetTransform(); ctx.clearRect(60, 60, 20, 20);"
                   "</script></body></html>");
    check(page.frame().has_value(), "the page renders");
    const paint::bitmap * pixels = canvas_of(page, "c");
    check(pixels != nullptr, "the canvas has a bitmap");
    if (pixels == nullptr) { return; }
    // The path was built in user space and drawn through the CTM: the square
    // is at 20,20 even though its points are 0,0.
    check(pixels->at(30, 30) == 0xFF123456u, "a filled path honours the transform");
    check(pixels->at(10, 10) == 0xFFFFFFFFu, "outside it is untouched");
    // clearRect makes pixels TRANSPARENT, not white - which is what lets an
    // overlay canvas show the page through it. resetTransform first, because
    // clearRect goes through the CTM like every other verb: without it this
    // clears at 80,80 and misses the canvas entirely.
    check(pixels->at(70, 70) == 0u, "clearRect clears to transparent");
}

void test_canvas_reaches_the_display_list_and_the_screen() {
    browser page{browser_options{200, 160}};
    page.load_html("<html><head><style>body { margin: 0; padding: 0 }</style></head>"
                   "<body><canvas id=c width=100 height=60></canvas><script>"
                   "var ctx = document.getElementById('c').getContext('2d');"
                   "ctx.fillStyle = '#ff0000'; ctx.fillRect(0, 0, 100, 60);"
                   "</script></body></html>");
    check(page.frame().has_value(), "the page renders");

    std::size_t images = 0;
    for (const auto & layer : page.layers().layers) {
        if (!layer.contents) { continue; }
        for (const auto & c : layer.contents->commands()) {
            if (c.op == paint::paint_op::image) { ++images; }
        }
    }
    check(images == 1, "the canvas is one image command in the display list");

    // And it made it all the way to pixels. A canvas that draws into its own
    // bitmap but never reaches the screen is the failure this catches.
    const auto image = page.read_pixels();
    check(image.has_value(), "the frame reads back");
    if (!image) { return; }
    check(image->row(10)[10] == 0xFFFF0000u, "the canvas is composited into the page");
}

void test_drawing_after_a_frame_redraws() {
    browser page{browser_options{200, 160}};
    page.load_html("<html><head><style>body { margin: 0; padding: 0 }</style></head>"
                   "<body><canvas id=c width=100 height=60></canvas><script>"
                   "var ctx = document.getElementById('c').getContext('2d');"
                   "ctx.fillStyle = '#ff0000'; ctx.fillRect(0, 0, 100, 60);"
                   "function later() { ctx.fillStyle = '#00ff00'; ctx.fillRect(0, 0, 100, 60); }"
                   "setTimeout(later, 1);</script></body></html>");
    check(page.frame().has_value(), "the first frame renders");
    const auto first = page.read_pixels();
    check(first.has_value() && first->row(10)[10] == 0xFFFF0000u, "and shows red");

    check(page.tick(5) == 1, "the timer runs and draws again");
    check(page.frame().has_value(), "the next frame renders");
    const auto second = page.read_pixels();
    // A canvas drawn into between frames must invalidate its tiles. Without
    // that the page keeps showing the old contents, which is the bug everyone
    // hits once and then never forgets.
    check(second.has_value() && second->row(10)[10] == 0xFF00FF00u,
          "and the new canvas contents are on screen");
}

} // namespace

int main() {
    test_controls_are_sized_by_the_element();
    test_a_select_is_as_wide_as_its_widest_option();
    test_a_control_reserves_the_room_its_text_is_drawn_in();
    test_a_seeded_value_is_drawn();

    test_typing_and_editing();
    test_tab_moves_focus_between_controls();
    test_tab_with_nothing_focused_starts_at_an_end();
    test_tab_does_not_scroll_and_can_be_prevented();
    test_typing_a_control_character_inserts_nothing();
    test_backspace_deletes_a_whole_code_point();
    test_editing_keys_do_not_scroll_the_page();
    test_selection_and_replacement();

    test_clicking_label_text_activates_the_control();
    test_clicking_a_checkbox_inside_a_label_toggles_once();
    test_a_label_for_a_field_focuses_it_without_disturbing_it();
    test_a_label_that_resolves_to_nothing_does_nothing();
    test_checkbox_toggles_on_click();
    test_radios_are_exclusive_within_a_name();
    test_form_submission_collects_successful_controls();
    test_reset_restores_the_markup();
    test_submit_can_be_cancelled();

    test_script_reads_and_writes_values();
    test_script_can_focus();

    test_canvas_is_sized_by_its_attributes();
    test_canvas_width_and_height_are_readable();
    test_getcontext_only_answers_for_2d();
    test_measuretext_reads_the_font_just_set();
    test_the_canvas_state_stack_restores_every_property();
    test_an_unbalanced_canvas_restore_is_harmless();
    test_canvas_drawing_reaches_the_pixels();
    test_fill_style_is_read_at_draw_time();
    test_paths_transforms_and_clear();
    test_canvas_reaches_the_display_list_and_the_screen();
    test_drawing_after_a_frame_redraws();

    REPORT("widgets_basics");
}
