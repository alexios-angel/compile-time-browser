//===- ctcompile/test/Runtime/NativeEvents.cpp - Native listener tests ----===//
//
// Tests the native runtime's listener table (CTNative/Runtime/Events.hpp) over
// a ctbrowser::page::static_page: listeners registered the way generated code
// registers them run in registration order at each step of a bubbling path,
// a listener added during a dispatch waits for the next one, a throwing
// listener is reported without stopping the rest, and forget() drops a
// document's listeners.
//
//===----------------------------------------------------------------------===//

#define CTNATIVE_DOM 1
#include "ctcompile/CTNative/Runtime/Events.hpp"
#include "ctcompile/CTNative/Runtime/Page.hpp"

#include <ctbrowser/page/static_page.hpp>

#include <functional>
#include <stdexcept>
#include <string>

#include "check.hpp"

namespace {

using ctnative::event_listeners;
using ctnative::EventTarget;
using ctnative::js_element_t;
using ctnative::js_string;

/// A page whose root is <html>, with <body> and a <p> inside it.
struct bound_page {
    ctbrowser::page::static_page page{ctbrowser::page::static_page_options{200, 100, 0}};
    bound_page() {
        page.load_html("<!doctype html><html><body><p>text</p></body></html>");
        page.frame();
    }
};

void test_bubbling_runs_each_step_in_registration_order() {
    bound_page bound;
    const ctnative::page_scope scope{bound.page};
    const js_element_t root = *ctnative::document.documentElement();
    const js_element_t body = root.children()[1];
    const js_element_t leaf = body.children()[0];
    std::string order;
    // As generated code registers them: a bare function, or one bound to
    // copies of its captures.
    EventTarget.prototype.addEventListener.call(root, js_string{"click"}, [&] { order += 'r'; });
    EventTarget.prototype.addEventListener.call(
        body, js_string{"click"},
        std::bind_front([&](const js_string & name) { order += name.value(); }, js_string{"b"}));
    EventTarget.prototype.addEventListener.call(body, js_string{"click"}, [&] { order += '2'; });
    EventTarget.prototype.addEventListener.call(leaf, js_string{"focus"}, [&] { order += 'f'; });

    event_listeners & table = event_listeners::program();
    CHECK_EQ(table.dispatch(leaf.value(), "click"), std::size_t{3});
    CHECK_EQ(order, std::string{"b2r"});
    // Without bubbling only the target's listeners run.
    order.clear();
    CHECK_EQ(table.dispatch(body.value(), "click", false), std::size_t{2});
    CHECK_EQ(order, std::string{"b2"});
    // Another type reaches only its own listeners.
    order.clear();
    CHECK_EQ(table.dispatch(leaf.value(), "focus"), std::size_t{1});
    CHECK_EQ(order, std::string{"f"});
    table.forget(bound.page.doc());
    CHECK_EQ(table.dispatch(leaf.value(), "click"), std::size_t{0});
}

void test_a_listener_added_while_dispatching_waits() {
    bound_page bound;
    const ctnative::page_scope scope{bound.page};
    const js_element_t root = *ctnative::document.documentElement();
    int outer = 0, inner = 0;
    EventTarget.prototype.addEventListener.call(root, js_string{"click"}, [&] {
        ++outer;
        // Many more than the registry's capacity, so its storage moves while
        // this listener runs.
        for (int i = 0; i < 64; ++i) {
            EventTarget.prototype.addEventListener.call(root, js_string{"click"}, [&] { ++inner; });
        }
    });
    event_listeners & table = event_listeners::program();
    CHECK_EQ(table.dispatch(root.value(), "click"), std::size_t{1});
    CHECK(outer == 1 && inner == 0);
    CHECK_EQ(table.dispatch(root.value(), "click"), std::size_t{65});
    CHECK(outer == 2 && inner == 64);
    table.forget(bound.page.doc());
}

void test_a_throwing_listener_is_reported() {
    bound_page bound;
    const ctnative::page_scope scope{bound.page};
    const js_element_t root = *ctnative::document.documentElement();
    bool after = false;
    EventTarget.prototype.addEventListener.call(
        root, js_string{"click"}, []() { throw std::invalid_argument("TypeError: boom"); });
    EventTarget.prototype.addEventListener.call(root, js_string{"click"}, [&] { after = true; });
    event_listeners & table = event_listeners::program();
    const std::size_t before = table.reported().size();
    CHECK_EQ(table.dispatch(root.value(), "click"), std::size_t{2});
    CHECK(after);
    CHECK_EQ(table.reported().size(), before + 1);
    CHECK_EQ(table.reported().back(), std::string{"TypeError: boom"});
    table.forget(bound.page.doc());
}

} // namespace

int main() {
    test_bubbling_runs_each_step_in_registration_order();
    test_a_listener_added_while_dispatching_waits();
    test_a_throwing_listener_is_reported();
    REPORT("native_events");
}
