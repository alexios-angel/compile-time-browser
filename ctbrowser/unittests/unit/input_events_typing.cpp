// A USER'S EDIT, TOLD TO THE PAGE: `beforeinput` (cancelable) before the value
// changes and `input` (not) after, both InputEvents naming the edit; `change`
// on blur only for a value the user changed; a <select>'s arrow keys pick an
// option and fire input then change. Each case names the WPT file it stands in
// for.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"

#include <string>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser::shell::input_event;

namespace {

constexpr const char * page_source = R"(<!doctype html><body>
    <input id=t><input id=u><select id=s><option>a<option>b<option>c</select>
    <script>
    var seen = [];
    function note(e) {
        seen.push(e.type + ':' + (e.inputType === undefined ? '-' : e.inputType) + ':' +
                  (e.data === undefined ? '-' : e.data) + ':' + e.cancelable + ':' + e.composed);
    }
    for (const id of ['t', 's']) {
        const el = document.getElementById(id);
        for (const type of ['beforeinput', 'input', 'change']) { el.addEventListener(type, note); }
    }
    </script></body>)";

[[nodiscard]] std::string seen(browser & page) {
    (void)page.run_script("console.log('seen=' + seen.join(' ')); seen = [];");
    std::string last;
    for (const std::string & line : page.bindings().console_output()) {
        if (line.starts_with("seen=")) { last = line; }
    }
    return last;
}

// html/semantics/forms/beforeinput.tentative.html, input-change-event-
// properties.html: typing, deleting, and leaving the field.
void test_typing_fires_beforeinput_input_and_change() {
    browser page{browser_options{400, 300}};
    page.load_html(page_source);
    page.frame();
    CHECK_EQ(page.script_error(), std::string{});

    (void)page.run_script("document.getElementById('t').focus();");
    (void)page.handle(input_event::typed("1"));
    CHECK_EQ(seen(page), std::string{"seen=beforeinput:insertText:1:true:true "
                                     "input:insertText:1:false:true"});
    (void)page.handle(input_event::key_press("Backspace"));
    CHECK_EQ(seen(page), std::string{"seen=beforeinput:deleteContentBackward:null:true:true "
                                     "input:deleteContentBackward:null:false:true"});
    // Nothing left to delete: no edit, no events.
    (void)page.handle(input_event::key_press("Backspace"));
    CHECK_EQ(seen(page), std::string{"seen="});

    // Back where it started: leaving commits nothing.
    (void)page.run_script("document.getElementById('u').focus();");
    CHECK_EQ(seen(page), std::string{"seen="});

    // A cancelled beforeinput keeps the text out, and a real edit commits.
    (void)page.run_script("document.getElementById('t').focus();"
                          "document.getElementById('t').addEventListener('beforeinput',"
                          "  e => { if (e.data === 'x') { e.preventDefault(); } });");
    (void)page.handle(input_event::typed("x"));
    (void)page.handle(input_event::typed("y"));
    (void)page.run_script("console.log('value=' + document.getElementById('t').value);");
    CHECK_EQ(seen(page), std::string{"seen=beforeinput:insertText:x:true:true "
                                     "beforeinput:insertText:y:true:true "
                                     "input:insertText:y:false:true"});
    (void)page.run_script("document.getElementById('u').focus();");
    CHECK_EQ(seen(page), std::string{"seen=change:-:-:false:false"});
}

// input-change-event-properties.html's "<select> pressing down arrow".
void test_select_arrow_keys_fire_input_and_change() {
    browser page{browser_options{400, 300}};
    page.load_html(page_source);
    page.frame();
    (void)page.run_script("document.getElementById('s').focus();");
    (void)page.handle(input_event::key_press("ArrowDown"));
    (void)page.run_script("console.log('picked=' + document.getElementById('s').value);");
    CHECK_EQ(seen(page), std::string{"seen=input:-:-:false:true change:-:-:false:false"});
    bool picked = false;
    for (const std::string & line : page.bindings().console_output()) {
        picked = picked || line == "picked=b";
    }
    check(picked, "ArrowDown picked the next option");
}

// checkable-active-onblur.html: Space held on a focused checkbox makes it
// :active and its release clicks it; Tab away ends the press; Enter clicks a
// button.
void test_the_keyboard_clicks_a_focused_control() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<!doctype html><body><input type=checkbox id=c><button id=b>go</button>
        <script>var clicks = 0; document.getElementById('b').onclick = () => ++clicks;</script>
        </body>)");
    page.frame();
    (void)page.run_script("document.getElementById('c').focus();");
    (void)page.handle(input_event::key_press("Space"));
    (void)page.run_script("console.log('held=' + (document.querySelector('input:active') === "
                          "document.getElementById('c')));");
    (void)page.handle(input_event::key_release("Space"));
    (void)page.run_script("console.log('released=' + document.getElementById('c').checked + ',' + "
                          "document.querySelector(':active'));");
    (void)page.handle(input_event::key_press("Space"));
    (void)page.handle(input_event::key_press("Tab"));
    (void)page.run_script("console.log('tabbed=' + document.querySelector(':active'));");
    (void)page.handle(input_event::key_release("Space"));
    (void)page.run_script("document.getElementById('b').focus();");
    (void)page.handle(input_event::key_press("Enter"));
    (void)page.run_script("console.log('enter=' + clicks + ',' + "
                          "document.getElementById('c').checked);");
    std::string held, released, tabbed, enter;
    for (const std::string & line : page.bindings().console_output()) {
        if (line.starts_with("held=")) { held = line; }
        if (line.starts_with("released=")) { released = line; }
        if (line.starts_with("tabbed=")) { tabbed = line; }
        if (line.starts_with("enter=")) { enter = line; }
    }
    CHECK_EQ(held, std::string{"held=true"});
    CHECK_EQ(released, std::string{"released=true,null"});
    CHECK_EQ(tabbed, std::string{"tabbed=null"});
    CHECK_EQ(enter, std::string{"enter=1,true"});
}

// checkable-active-space-key-being-disabled.html: a control its own keydown
// listener disabled is neither :active while Space is held nor clicked by it.
void test_a_control_disabled_by_its_keydown_is_not_pressed() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<!doctype html><body><input type=checkbox id=c>
        <script>document.getElementById('c').addEventListener('keydown',
            e => { e.target.disabled = true; });</script></body>)");
    page.frame();
    (void)page.run_script("document.getElementById('c').focus();");
    (void)page.handle(input_event::key_press("Space"));
    (void)page.run_script("console.log('held=' + document.querySelector('input:active'));");
    (void)page.handle(input_event::key_release("Space"));
    (void)page.run_script("console.log('after=' + document.getElementById('c').checked);");
    std::string held, after;
    for (const std::string & line : page.bindings().console_output()) {
        if (line.starts_with("held=")) { held = line; }
        if (line.starts_with("after=")) { after = line; }
    }
    CHECK_EQ(held, std::string{"held=null"});
    CHECK_EQ(after, std::string{"after=false"});
}

} // namespace

int main() {
    test_the_keyboard_clicks_a_focused_control();
    test_typing_fires_beforeinput_input_and_change();
    test_select_arrow_keys_fire_input_and_change();
    test_a_control_disabled_by_its_keydown_is_not_pressed();
    REPORT("input_events_typing");
}
