// The DOM bindings: script driving the page.
//
// the previous engine's bindings were safe because the document owned every node forever and
// nothing was concurrent. this engine's hold HANDLES, so the interesting tests are the
// ones the previous engine could not have failed:
//
//   * a wrapper for a removed element resolves to nothing, and its methods do
//     nothing, rather than writing through a dangling pointer
//   * a mutation from script invalidates the pipeline, so the NEXT frame shows
//     it - script and rendering are not two views that can disagree
//
// Plus the ordinary web-platform surface, checked through the browser rather
// than against the bindings in isolation: a binding that mutates the DOM but
// does not change what is drawn is not working, whatever a unit test says.
//
// This file keeps the original's subject - script WRITING to the document and
// the pixels following: classList and style, the document API, the document's
// own properties, a stale handle staying inert - and its two robustness cases.
//
// One of eight files carved out of unittests/unit/bindings_basics.cpp on
// 2026-09-07, when it had reached 3,365 lines. Every case is verbatim and in
// the order it had; the helpers more than one of the eight needs are in
// page_probe.hpp beside this, and `find_id` is test/support/dom_probe.hpp's.

#include <ctbrowser/app/app.hpp>
#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include "page_probe.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace ctbrowser;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser::shell::input_event;
using ctbrowser_test::check;
using ctbrowser_test::log_of;

namespace {

// Everything the page ends up drawing as text. The honest way to ask "did the
// page change", since that is what a user sees.
[[nodiscard]] std::string rendered_text(browser & page) {
    std::string out;
    const auto walk = [&](auto && self, const layout::fragment & f) -> void {
        out += f.text;
        for (const auto & c : f.children) { self(self, c); }
    };
    walk(walk, page.fragments());
    return out;
}

[[nodiscard]] std::size_t count_fill(browser & page, color want) {
    std::size_t n = 0;
    for (const auto & layer : page.layers().layers) {
        if (!layer.contents) { continue; }
        for (const auto & c : layer.contents->commands()) {
            if (c.fill == want) { ++n; }
        }
    }
    return n;
}

// --- element.classList and element.style ----------------------------------

void test_class_list_edits_the_attribute() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><body><div id=d class="a b"></div><script>
        const d = document.getElementById('d');
        d.classList.add('c');
        d.classList.add('c');
        console.log('added=' + d.getAttribute('class'));
        console.log('has=' + d.classList.contains('b') + ',' + d.classList.contains('zz'));
        console.log('len=' + d.classList.length);
        d.classList.remove('b');
        console.log('removed=' + d.getAttribute('class'));
        console.log('forced=' + d.classList.toggle('a', true) + ',' + d.getAttribute('class'));
        d.classList.toggle('a');
        console.log('flipped=' + d.getAttribute('class'));
        console.log('item=' + d.classList.item(0) + ',' + d.classList.item(9));
    </script></body></html>)");
    check(page.script_error().empty(), "the class list script ran: " + page.script_error());
    const auto & log = log_of(page);
    // add is a SET, not an append - twice is once.
    check(log[0] == "added=a b c", "classList.add appends a token, once: " + log[0]);
    check(log[1] == "has=true,false", "classList.contains: " + log[1]);
    // The count is live, so it moves with the attribute rather than reporting
    // whatever it was when the element was first wrapped.
    check(log[2] == "len=3", "classList.length: " + log[2]);
    check(log[3] == "removed=a c", "classList.remove: " + log[3]);
    check(log[4] == "forced=true,a c", "toggle(name, true) forces rather than flips: " + log[4]);
    check(log[5] == "flipped=c", "toggle(name) flips: " + log[5]);
    check(log[6] == "item=c,null", "classList.item, past the end too: " + log[6]);
}

void test_class_list_reaches_the_cascade() {
    browser page{browser_options{400, 200}};
    // The point of writing the attribute rather than keeping a list beside it:
    // the style engine matches on what the DOM says.
    page.load_html(R"(<html><head><style>
    .on { background-color: #008000 }
    </style></head><body><div id=d>x</div>
    <script>document.getElementById('d').classList.add('on');</script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    check(page.frame().has_value(), "the page renders");
    check(count_fill(page, color::rgba(0, 128, 0)) == 1, "a class added from script cascades");
}

void test_style_writes_reach_the_document() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><body><div id=d></div><script>
        const d = document.getElementById('d');
        d.style.display = 'none';
        console.log('attr=' + d.getAttribute('style'));
        console.log('read=' + d.style.display);
        d.style.backgroundColor = 'red';
        console.log('camel=' + d.getAttribute('style'));
        d.style.display = '';
        console.log('cleared=' + d.getAttribute('style'));
        d.style.setProperty('--custom', '4px');
        console.log('custom=' + d.style.getPropertyValue('--custom'));
        d.style.removeProperty('--custom');
        console.log('gone=' + d.getAttribute('style'));
    </script></body></html>)");
    check(page.script_error().empty(), "the style script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log[0] == "attr=display: none; ", "a style write serialises to the attribute: " + log[0]);
    // The proxy's target holds the declarations, so a read needs no trap.
    check(log[1] == "read=none", "a style property reads back: " + log[1]);
    // The IDL name and the CSS name are different spellings of one property.
    check(log[2].find("background-color: red") != std::string::npos,
          "backgroundColor is background-color: " + log[2]);
    // Assigning "" REMOVES a declaration. Emitting `display: ;` instead would
    // leave the old value standing as far as the parser is concerned.
    check(log[3].find("display") == std::string::npos,
          "assigning the empty string removes it: " + log[3]);
    check(log[4] == "custom=4px", "setProperty reaches a name no identifier can spell: " + log[4]);
    check(log[5].find("custom") == std::string::npos, "removeProperty: " + log[5]);
}

void test_style_writes_reach_the_pixels() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><body><div id=d>x</div><script>
        document.getElementById('d').style.backgroundColor = '#0000ff';
    </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    check(page.frame().has_value(), "the page renders");
    check(count_fill(page, color::rgba(0, 0, 255)) == 1, "a style write repaints");
}

// --- the document API -----------------------------------------------------

void test_script_mutates_what_is_drawn() {
    browser page{browser_options{400, 200}};
    page.load_html(
        "<html><body><div id=a>original</div>"
        "<script>document.getElementById('a').setText('replaced');</script></body></html>");
    check(page.frame().has_value(), "the page renders");
    check(page.script_error().empty(), "the script ran without error");
    // The whole point: the mutation reached the pixels, not just the DOM.
    check(rendered_text(page).find("replaced") != std::string::npos, "setText changed the page");
    check(rendered_text(page).find("original") == std::string::npos, "and removed the old text");
}

void test_attributes_and_classes() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><head><style>
    .hot { background-color: #ff0000 }
    </style></head><body><div id=a>x</div><script>
    var el = document.getElementById('a');
    el.setAttribute('data-role', 'banner');
    el.addClass('hot');
    console.log('role=' + el.getAttribute('data-role'));
    console.log('hot=' + el.hasClass('hot'));
    console.log('cold=' + el.hasClass('cold'));
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");
    check(page.script_error().empty(), "the script ran without error");

    const auto & log = log_of(page);
    check(log.size() == 3, "three console lines");
    if (log.size() == 3) {
        check(log[0] == "role=banner", "setAttribute then getAttribute round-trips");
        check(log[1] == "hot=true", "hasClass sees the class it added");
        check(log[2] == "cold=false", "and does not see one it did not");
    }
    // And the class actually restyled the element, which is the part that
    // matters: adding a class that changes nothing on screen is not a binding
    // that works.
    check(count_fill(page, color::rgba(255, 0, 0)) == 1, "addClass restyled the element");
}

// --- the document's own properties ------------------------------------------

// `document.title` and `getElementsByTagName` simply were not there. A page
// asking for either got `undefined` and, in the second case, died on calling
// it - which is how the comparison rig found them.
void test_document_title_and_tag_lookup() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><head><title>a page</title></head><body>
    <p>one</p><p>two</p><div><p>three</p></div>
    <script>
    console.log('title=' + document.title);
    console.log('paragraphs=' + document.getElementsByTagName('p').length);
    console.log('second=' + document.getElementsByTagName('p')[1].getText());
    console.log('star=' + (document.getElementsByTagName('*').length > 5));
    console.log('none=' + document.getElementsByTagName('blink').length);
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");
    check(page.script_error().empty(), "the script ran without error");

    const auto & log = log_of(page);
    check(log.size() == 5, "five console lines");
    if (log.size() != 5) { return; }
    check(log[0] == "title=a page", "document.title is the <title>'s text");
    // Nested ones too: the walk is the whole tree, not the body's children.
    check(log[1] == "paragraphs=3", "getElementsByTagName finds every match");
    check(log[2] == "second=two", "in document order, and they are real elements");
    check(log[3] == "star=true", "'*' matches every element");
    check(log[4] == "none=0", "and a tag nothing uses is an empty list");

    // ...and the title is LIVE, not a snapshot taken when the document object
    // was built. Rewriting the <title> has to be visible, which is the same bug
    // class location.href had: set once at install and wrong ever after.
    (void)page.run_script("document.getElementsByTagName('title')[0].setText('renamed');");
    check(page.frame().has_value(), "the page redraws");
    (void)page.run_script("console.log('after=' + document.title);");
    check(log.size() == 6 && log[5] == "after=renamed", "document.title follows the element");
}

// `document.activeElement` is the script-visible mirror of browser::focused().
// The bindings could SET focus - element.focus() - but nothing came back, so
// the property could not exist at all.
void test_document_active_element_follows_focus() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><body>
    <input id=a><input id=b>
    <script>
    function active() { return document.activeElement ? document.activeElement.id : 'none'; }
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");

    const auto ask = [&page](const char * label) {
        (void)page.run_script(std::string{"console.log('"} + label + "=' + active())");
    };
    ask("start");

    // Focus by SCRIPT...
    (void)page.run_script("document.getElementById('b').focus();");
    ask("scripted");
    // ...by the KEYBOARD...
    (void)page.handle(input_event::key_press("Tab"));
    ask("tabbed");
    // ...and blurred.
    (void)page.run_script("document.getElementById('b').blur();");
    ask("blurred");

    const auto & log = log_of(page);
    check(log.size() == 4, "four answers");
    if (log.size() != 4) { return; }
    check(log[0] == "start=none", "nothing is focused to begin with");
    check(log[1] == "scripted=b", "element.focus() is visible as activeElement");
    check(log[2] == "tabbed=a", "and Tab moves it, wrapping past the last control");
    check(log[3] == "blurred=none", "and blur clears it");
}

// A page that errors ONCE used to report that error for ever: run_script only
// ever assigned script_error_, never cleared it, so every later success still
// carried the old message. The rig hit this immediately - a perfectly good eval
// came back with a failure from three calls earlier.
void test_a_script_error_does_not_outlive_the_script() {
    browser page{browser_options{400, 300}};
    page.load_html("<body><p id=p>x</p></body>");
    check(page.frame().has_value(), "the page renders");
    check(page.script_error().empty(), "no error to begin with");

    check(!page.run_script("nosuchfunction();"), "a broken script fails");
    check(!page.script_error().empty(), "and says so");

    check(page.run_script("console.log('fine');"), "a good script runs");
    check(page.script_error().empty(), "and the old error is gone");
}

void test_removeclass_undoes_it() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><head><style>.hot { background-color: #ff0000 }</style></head>
    <body><div id=a class=hot>x</div><script>
    document.getElementById('a').removeClass('hot');
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");
    check(count_fill(page, color::rgba(255, 0, 0)) == 0, "removeClass unstyled the element");
}

void test_create_and_append() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><body><div id=host></div><script>
    var el = document.createElement('p');
    el.setText('made by script');
    document.getElementById('host').appendChild(el);
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");
    check(page.script_error().empty(), "the script ran without error");
    check(rendered_text(page).find("made by script") != std::string::npos,
          "a created element appears once appended");
}

void test_remove_child() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><body><div id=host><p id=gone>remove me</p></div><script>
    var host = document.getElementById('host');
    host.removeChild(document.getElementById('gone'));
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");
    check(rendered_text(page).find("remove me") == std::string::npos,
          "a removed element stops being drawn");
}

void test_a_stale_handle_is_inert() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><body><div id=host><p id=doomed>text</p></div><script>
    var doomed = document.getElementById('doomed');
    document.getElementById('host').removeChild(doomed);
    doomed.setText('written to a dead node');
    doomed.addClass('whatever');
    console.log('survived');
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");
    // the previous engine held a raw node* here. The handle turns a use-after-free into a
    // lookup that finds nothing, so the writes go nowhere and the page is
    // unharmed - which is the entire argument for handles.
    check(page.script_error().empty(), "writing through a stale handle does not fail the script");
    check(log_of(page).size() == 1 && log_of(page)[0] == "survived",
          "and execution continues past it");
    check(rendered_text(page).find("written to a dead node") == std::string::npos,
          "nothing was written to the removed node");
}

void test_layout_is_visible_to_script() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><head><style>#a { width: 123px; height: 45px }</style></head>
    <body><div id=a>x</div><script>
    var el = document.getElementById('a');
    console.log('w=' + el.offsetWidth + ' h=' + el.offsetHeight);
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");

    // The script ran BEFORE the first layout, so it must report 0 rather than
    // a guess. Reporting a plausible-looking wrong number is worse: a page that
    // sizes itself from it would be silently wrong.
    check(log_of(page).size() == 1, "one console line");
    if (!log_of(page).empty()) {
        check(log_of(page)[0] == "w=0 h=0", "before the first layout, geometry reads as zero");
    }

    // After a layout, a freshly-obtained wrapper sees real numbers.
    auto & bindings = page.bindings();
    (void)bindings;
    page.load_html(R"(<html><head><style>#a { width: 123px; height: 45px }</style></head>
    <body><div id=a>x</div><script>
    function report() { var el = document.getElementById('a');
      console.log('w=' + el.offsetWidth + ' h=' + el.offsetHeight); }
    setTimeout(report, 0);
    </script></body></html>)");
    check(page.frame().has_value(), "the second page renders");
    check(page.tick(1) == 1, "the timer ran after layout");
    check(page.frame().has_value(), "and the frame after it renders");
    check(!log_of(page).empty() && log_of(page).back() == "w=123 h=45",
          "after layout, offsetWidth/Height report the real box");
}

void test_zero_height_layout_is_still_visible_to_script() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><head><style>
      body { margin: 0 }
      #host { height: 20px; padding: 7px 0 0 5px }
      #zero { width: 123px; height: 0 }
    </style></head><body><div id=host><div id=zero></div></div><script>
      setTimeout(function () {
        const box = document.getElementById('zero').getBoundingClientRect();
        console.log('x=' + box.x + ' y=' + box.y + ' w=' + box.width + ' h=' + box.height);
      }, 0);
    </script></body></html>)");
    check(page.frame().has_value(), "the zero-height page lays out");
    check(page.tick(1) == 1, "the zero-height geometry timer ran after layout");
    check(page.frame().has_value(), "and the zero-height page renders again");
    check(!log_of(page).empty() && log_of(page).back() == "x=5 y=7 w=123 h=0",
          "a zero-height fragment keeps its real position and width");
}

// --- robustness -----------------------------------------------------------

void test_a_broken_script_still_renders() {
    browser page{browser_options{300, 200}};
    page.load_html(
        "<html><body><p>content</p><script>this is not javascript(((</script></body></html>");
    check(page.frame().has_value(), "the page still renders");
    check(!page.script_error().empty(), "and the error is recorded");
    // A page whose script fails must still show its markup. Anything else
    // turns one bad script into a blank window.
    check(rendered_text(page).find("content") != std::string::npos, "the markup is unaffected");
}

void test_window_and_performance() {
    browser page{browser_options{321, 234}};
    page.load_html(R"(<html><body><script>
    console.log('size ' + window.innerWidth + 'x' + window.innerHeight);
    console.log('t0 ' + performance.now());
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");
    const auto & log = log_of(page);
    check(log.size() == 2, "two console lines");
    if (log.size() == 2) {
        check(log[0] == "size 321x234", "window reports the viewport");
        // A POSITIVE TIME ORIGIN, and a FIXED one. `performance.now()` on a
        // page's first line is not zero in any browser - the origin is when the
        // document began loading and script runs after that - and
        // `dom/events/Event-constructors.any.js` asserts `timeStamp > 0`. It is
        // a constant rather than a real clock for the reason `Math.random` is
        // seeded: three example pages byte-compare their render.
        check(log[1] == "t0 1", "and the page clock starts at a positive origin");
    }
}

} // namespace

int main() {
    test_script_mutates_what_is_drawn();
    test_attributes_and_classes();
    test_document_title_and_tag_lookup();
    test_document_active_element_follows_focus();
    test_a_script_error_does_not_outlive_the_script();
    test_removeclass_undoes_it();
    test_create_and_append();
    test_remove_child();
    test_a_stale_handle_is_inert();
    test_layout_is_visible_to_script();
    test_zero_height_layout_is_still_visible_to_script();
    test_a_broken_script_still_renders();
    test_window_and_performance();
    test_class_list_edits_the_attribute();
    test_class_list_reaches_the_cascade();
    test_style_writes_reach_the_document();
    test_style_writes_reach_the_pixels();
    REPORT("dom_mutation");
}
