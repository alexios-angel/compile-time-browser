// FOCUS, HTML 6.6: only a focusable area takes focus - `focus()` on anything
// else finds a focusable area for it or does nothing - Tab walks the
// flattened tabindex-ordered focus navigation scopes of the document, its
// shadow hosts and their slots, a host that delegates focus hands it on, a
// radio group is one stop, and `:focus` is the focused element and the shadow
// hosts around it, never its other ancestors. Each case names the WPT file it
// stands in for.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include "cssom_probe.hpp"

#include <string>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser::shell::input_event;
using ctbrowser_test::logged;

namespace {

// The id of what has focus, looked for through the shadow roots it is in -
// the LAST such line, since every call logs one.
[[nodiscard]] std::string active(browser & page) {
    (void)page.run_script("var a = document.activeElement;"
                          "while (a && a.shadowRoot && a.shadowRoot.activeElement)"
                          "  a = a.shadowRoot.activeElement;"
                          "console.log('active=' + (a ? a.id || a.localName : 'null'));");
    std::string last;
    for (const std::string & line : page.bindings().console_output()) {
        if (line.starts_with("active=")) { last = line; }
    }
    return last;
}

void tab(browser & page, bool shift = false) {
    (void)page.handle(input_event::key_press("Tab", shift));
}

// shadow-dom/focus/focus-method-*, css/selectors/focus-within-focus-move.html:
// focus() on an element with no tabindex that nothing makes focusable does
// nothing - body.focus() above all, which testdriver's send_keys runs before
// every key - while tabindex=-1 is focusable and the document element's focus
// is the viewport's. Nothing focused is the body, while the page is still
// parsing too (shadow-dom/focus/focus-click-on-shadow-host.html).
void test_only_a_focusable_area_takes_focus() {
    browser page{browser_options{400, 300}};
    page.load_html(
        "<!doctype html><body><input id=field><div id=plain>plain</div>"
        "<div id=minus tabindex=-1>minus</div><div id=gone tabindex=0 style='display:none'>x</div>"
        "<div id=hid tabindex=0 hidden>h</div>"
        "<script>console.log('parsing=' + (document.activeElement === document.body))</script>"
        "</body>");
    CHECK_EQ(logged(page, "parsing="), std::string{"parsing=true"});
    page.frame();
    (void)page.run_script("field.focus(); document.body.focus(); plain.focus(); gone.focus();"
                          "hid.focus();");
    CHECK_EQ(active(page), std::string{"active=field"});
    (void)page.run_script("minus.focus();");
    CHECK_EQ(active(page), std::string{"active=minus"});
    (void)page.run_script("document.documentElement.focus();");
    CHECK_EQ(active(page), std::string{"active=body"});
}

// shadow-dom/focus/focus-tabindex-order-shadow-*.html: positive tabindex
// first, then the rest in tree order; a host's shadow tree follows the host,
// a slot is replaced by what is assigned to it, and tabindex=-1 on a host
// takes its whole tree out of the order.
void test_tab_walks_the_flattened_scopes() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<!doctype html><body>
        <div id=a tabindex=0>a</div><div id=host tabindex=0><span id=slotted slot=s
        tabindex=0>slotted</span></div><div id=later tabindex=2>later</div>
        <div id=first tabindex=1>first</div><div id=closed tabindex=-1>closed</div>
        <input id=last>
        <script>
        const root = host.attachShadow({mode: 'open'});
        root.innerHTML = '<div id=inner tabindex=0>inner</div><slot name=s></slot>' +
                         '<div id=after tabindex=0>after</div>';
        closed.attachShadow({mode: 'open'}).innerHTML = '<input id=hidden-away>';
        </script></body>)");
    page.frame();
    std::string walked;
    for (int i = 0; i < 8; ++i) {
        tab(page);
        walked += active(page).substr(7) + ' ';
    }
    CHECK_EQ(walked, std::string{"first later a host inner slotted after last "});
    tab(page, true);
    CHECK_EQ(active(page), std::string{"active=after"});
}

// shadow-dom/focus/focus-tabindex-order-shadow-varying-tabindex-2/3.html: a
// focus inside a scope that is out of the order - under a host with
// tabindex=-1 - goes on through the rest of that scope, then past its owner.
void test_tab_goes_on_from_a_scope_out_of_the_order() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<!doctype html><body><div id=outer tabindex=-1></div><input id=after>
        <script>
        const root = outer.attachShadow({mode: 'open'});
        root.innerHTML = '<div id=a></div><div id=b tabindex=-1></div><div id=c></div>';
        for (const id of ['a', 'b', 'c'])
            root.querySelector('#' + id).attachShadow({mode: 'open'}).innerHTML =
                '<input id=' + id + 'in>';
        </script></body>)");
    page.frame();
    (void)page.run_script("outer.shadowRoot.querySelector('#b').shadowRoot.firstChild.focus();");
    CHECK_EQ(active(page), std::string{"active=bin"});
    tab(page);
    CHECK_EQ(active(page), std::string{"active=cin"});
    tab(page);
    CHECK_EQ(active(page), std::string{"active=after"});
}

// shadow-dom/focus-navigation/focus-with-negative-index.html: from an
// element out of the order, Tab and Shift+Tab go to its neighbours in TREE
// order that are in it - not to where a tabindex of zero would put it.
void test_tab_from_out_of_the_order_goes_by_tree_order() {
    browser page{browser_options{400, 300}};
    page.load_html("<!doctype html><body><div id=p tabindex=2>p</div><div id=x tabindex=-1>x</div>"
                   "<div id=n tabindex=1>n</div><input id=z></body>");
    page.frame();
    (void)page.run_script("x.focus();");
    tab(page);
    CHECK_EQ(active(page), std::string{"active=n"});
    (void)page.run_script("x.focus();");
    tab(page, true);
    CHECK_EQ(active(page), std::string{"active=p"});
}

// shadow-dom/focus-navigation/focus-navigation-slot-fallback.html: a slot
// nothing is assigned to owns its fallback content as a scope - ordered in
// there, and placed where the slot is.
void test_a_slot_owns_its_fallback() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<!doctype html><body><div id=host></div>
        <script>
        host.attachShadow({mode: 'open'}).innerHTML =
            '<input id=j1 tabindex=1><slot><input id=k0><input id=k1 tabindex=1></slot>' +
            '<input id=j2 tabindex=2>';
        </script></body>)");
    page.frame();
    std::string walked;
    for (int i = 0; i < 4; ++i) {
        tab(page);
        walked += active(page).substr(7) + ' ';
    }
    CHECK_EQ(walked, std::string{"j1 j2 k1 k0 "});
}

// shadow-dom/focus/focus-method-delegatesFocus.html: a host that delegates
// focus is no focusable area itself; focus() hands focus to the first one in
// its shadow tree, and leaves a focus already inside the tree where it is.
void test_a_delegating_host_hands_focus_on() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<!doctype html><body><div id=host></div>
        <script>
        const root = host.attachShadow({mode: 'open', delegatesFocus: true});
        root.innerHTML = '<span>text</span><input id=one><input id=two>';
        </script></body>)");
    page.frame();
    (void)page.run_script("host.focus();");
    CHECK_EQ(active(page), std::string{"active=one"});
    (void)page.run_script("host.shadowRoot.getElementById('two').focus(); host.focus();");
    CHECK_EQ(active(page), std::string{"active=two"});
    (void)page.run_script("console.log('focus=' + document.querySelectorAll(':focus').length +"
                          " ',' + host.matches(':focus'));");
    CHECK_EQ(logged(page, "focus="), std::string{"focus=1,true"});
}

// css/selectors/focus-visible-001.html, focus-within-009.html: :focus is the
// focused element alone - its ancestors match :focus-within, not :focus.
void test_focus_matches_the_element_alone() {
    browser page{browser_options{400, 300}};
    page.load_html("<!doctype html><body><div id=wrap><input id=field></div></body>");
    page.frame();
    (void)page.run_script("field.focus(); console.log('matches=' +"
                          " document.querySelectorAll(':focus').length + ',' +"
                          " wrap.matches(':focus-within'));");
    CHECK_EQ(logged(page, "matches="), std::string{"matches=1,true"});
}

// html/semantics/forms/the-input-element/radio-focus-navigation-*.html: a
// radio group is one stop - its checked radio, or with none checked the first
// one reached - and Tab never walks from one radio of a group to the next.
void test_a_radio_group_is_one_stop() {
    browser page{browser_options{400, 300}};
    page.load_html("<!doctype html><body><input id=start><input type=radio name=r id=r1>"
                   "<input type=radio name=r id=r2><input type=radio name=r id=r3>"
                   "<input id=end></body>");
    page.frame();
    (void)page.run_script("start.focus();");
    tab(page);
    CHECK_EQ(active(page), std::string{"active=r1"});
    tab(page);
    CHECK_EQ(active(page), std::string{"active=end"});
    (void)page.run_script("r2.checked = true;");
    tab(page, true);
    CHECK_EQ(active(page), std::string{"active=r2"});
    tab(page, true);
    CHECK_EQ(active(page), std::string{"active=start"});
}

// custom-elements/form-associated/focusability.html: a form-associated custom
// element takes focus with a tabindex, and not when it is disabled.
void test_a_disabled_form_associated_element_is_not_focusable() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<!doctype html><body><x-face id=on tabindex=0>on</x-face>
        <x-face id=off tabindex=0 disabled>off</x-face>
        <script>customElements.define('x-face', class extends HTMLElement {
            static formAssociated = true; });</script></body>)");
    page.frame();
    (void)page.run_script("on.focus();");
    CHECK_EQ(active(page), std::string{"active=on"});
    (void)page.run_script("off.focus();");
    CHECK_EQ(active(page), std::string{"active=on"});
}

// css/selectors/focus-visible-*.html: :focus-visible after the keyboard moved
// focus, on a field that takes typing however it got focus, and for a
// script's focus() before the user moved focus at all or after a key - not
// after a press moved it to anything else (focus-visible-script-focus-008),
// and not for a script's focus() that follows such a press.
void test_focus_visible_follows_how_focus_came() {
    browser page{browser_options{400, 300}};
    page.load_html("<!doctype html><body style='margin:0'>"
                   "<div id=d tabindex=0 style='height:40px'>d</div>"
                   "<div id=e tabindex=0 style='height:40px'>e</div><input id=field></body>");
    page.frame();
    const auto visible = [&](const char * tag) {
        (void)page.run_script(std::string{"console.log('"} + tag +
                              "=' +"
                              " document.querySelectorAll(':focus-visible').length + ',' +"
                              " document.querySelectorAll(':focus').length);");
        return logged(page, std::string{tag} + "=");
    };
    (void)page.run_script("d.focus();");
    CHECK_EQ(visible("script"), std::string{"script=1,1"});
    (void)page.handle(input_event::mouse_move_to(5, 60));
    (void)page.handle(input_event::mouse_down_at(5, 60));
    (void)page.handle(input_event::mouse_up_at(5, 60));
    CHECK_EQ(visible("pressed"), std::string{"pressed=0,1"});
    (void)page.run_script("field.focus();");
    CHECK_EQ(visible("typing"), std::string{"typing=1,1"});
    (void)page.run_script("d.focus();");
    CHECK_EQ(visible("after"), std::string{"after=0,1"});
    // focus-visible-028: a key - one whose default a listener may cancel -
    // is the keyboard's turn, so a script's focus after it shows.
    (void)page.handle(input_event::key_press("ArrowRight"));
    (void)page.run_script("e.focus();");
    CHECK_EQ(visible("keyed"), std::string{"keyed=1,1"});
    tab(page, true);
    CHECK_EQ(visible("tabbed"), std::string{"tabbed=1,1"});
}

// dom/events/scrolling/scrollend-event-for-user-scroll.html: a scroll
// container whose content overflows is a focusable area (a scrollable region)
// - and one that has nothing to scroll is not.
void test_a_scroller_takes_focus() {
    browser page{browser_options{400, 300}};
    page.load_html(
        "<!doctype html><body>"
        "<div id=s style='height:50px;overflow:auto'><div style='height:500px'></div></div>"
        "<div id=f style='height:50px;overflow:auto'>fits</div></body>");
    page.frame();
    (void)page.run_script("f.focus();");
    CHECK_EQ(active(page), std::string{"active=body"});
    (void)page.run_script("s.focus();");
    CHECK_EQ(active(page), std::string{"active=s"});
}

} // namespace

int main() {
    test_only_a_focusable_area_takes_focus();
    test_tab_walks_the_flattened_scopes();
    test_tab_goes_on_from_a_scope_out_of_the_order();
    test_tab_from_out_of_the_order_goes_by_tree_order();
    test_a_slot_owns_its_fallback();
    test_a_delegating_host_hands_focus_on();
    test_focus_matches_the_element_alone();
    test_a_radio_group_is_one_stop();
    test_a_disabled_form_associated_element_is_not_focusable();
    test_focus_visible_follows_how_focus_came();
    test_a_scroller_takes_focus();
    REPORT("focus_navigation");
}
