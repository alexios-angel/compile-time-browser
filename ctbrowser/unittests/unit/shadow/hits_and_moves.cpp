//===- hits_and_moves.cpp - Shadow tree hit tests and moves -----*- C++ -*-===//
//
// What a shadow tree hides and what moves with it, each as the smallest
// expression that fails if its fix is reverted:
//
//   * a ShadowRoot has its own elementFromPoint and elementsFromPoint, null
//     off the viewport, and the document's answer is the host
//     (shadowroot-methods test-007). Shadow trees are not laid out yet (the
//     box tree is the light tree), so the hit is the host for both callers;
//   * window.event is set around each listener and restored after a nested
//     dispatch (event-global.html);
//   * a shadow root cannot be cloned (Node-prototype-cloneNode.html);
//   * a custom element inside the shadow tree of a plain host moved by
//     moveBefore gets connectedMoveCallback (custom-element-move-reactions.html);
//   * the selection drops a range in a shadow tree whose host was removed,
//     even when it came straight back - `replaceChild(host, host)`
//     (selection-range-in-shadow-after-the-shadow-removed.tentative.html);
//   * createElementNS constructs a custom element and gives it its prefix
//     only after the constructor has returned
//     (Document-createElementNS-prefix-timing.html).
//
//===----------------------------------------------------------------------===//

#include <ctbrowser.hpp>

#include "check.hpp"
#include "dom_probe.hpp"

#include <string>

namespace {

constexpr const char * page_html = R"(<!DOCTYPE html>
<html><head><style>body { margin: 0 }</style></head><body>
<div id=host style="width: 100px; height: 100px"></div>
<section id=section></section>
</body></html>)";

void is(const std::string & expression, const std::string & expected) {
    ctbrowser_test::is_in(page_html, expression, expected);
}

void test_element_from_point_is_retargeted() {
    is(R"JS((function () {
        var host = document.getElementById('host');
        var shadow = host.attachShadow({mode: 'closed'});
        shadow.innerHTML = '<span style="display: block; width: 50px; height: 50px">x</span>';
        var span = shadow.firstChild;
        return [document.elementFromPoint(10, 10) === host,
                shadow.elementFromPoint(10, 10) === host,
                shadow.elementsFromPoint(10, 10)[0] === host,
                document.elementsFromPoint(10, 10).indexOf(span),
                shadow.elementFromPoint(-1, 1)].join();
    })())JS",
       "true,true,true,-1,");
    is("document.getElementById('host').attachShadow({mode: 'open'}).elementFromPoint(NaN, 1)",
       "threw:TypeError");
}

void test_window_event_is_set_around_each_listener() {
    // DOM "inner invoke" sets the listener's global's current event, leaves it
    // undefined for a listener inside a shadow tree, and restores it after each
    // call, so a nested dispatch hands the outer event back (event-global.html).
    is(R"JS((function () {
        var log = [];
        var host = document.getElementById('host');
        var inside = host.attachShadow({mode: 'open'}).appendChild(document.createElement('b'));
        inside.addEventListener('x', function () { log.push(String(window.event)); });
        host.addEventListener('x', function (e) {
            log.push(window.event === e);
            var other = document.createElement('p');
            other.addEventListener('y', function (f) { log.push(window.event === f); });
            other.dispatchEvent(new Event('y'));
            log.push(window.event === e && event === e);
        });
        inside.dispatchEvent(new Event('x', {composed: true, bubbles: true}));
        log.push(String(window.event));
        return log.join();
    })())JS",
       "undefined,true,true,true,undefined");
}

void test_a_shadow_root_is_not_cloned() {
    is("document.getElementById('host').attachShadow({mode: 'open'}).cloneNode(false)",
       "threw:NotSupportedError");
}

void test_connected_move_reaches_into_a_plain_hosts_shadow_tree() {
    is(R"JS((function () {
        var log = [];
        customElements.define('x-mover', class extends HTMLElement {
            connectedMoveCallback() { log.push('move'); }
            connectedCallback() { log.push('connected'); }
            disconnectedCallback() { log.push('disconnected'); }
        });
        var host = document.getElementById('host');
        host.attachShadow({mode: 'open'}).appendChild(document.createElement('x-mover'));
        log.length = 0;
        document.getElementById('section').moveBefore(host, null);
        return log.join();
    })())JS",
       "move");
}

void test_the_selection_lets_go_of_a_range_whose_host_left() {
    is(R"JS((function () {
        var host = document.getElementById('host');
        var root = host.attachShadow({mode: 'open'});
        root.innerHTML = '<div>ABC</div>';
        getSelection().collapse(root.firstChild.firstChild, 2);
        var range = getSelection().getRangeAt(0);
        host.parentNode.replaceChild(host, host);
        return [getSelection().rangeCount, range.startOffset,
                range.startContainer === root.firstChild.firstChild].join();
    })())JS",
       "0,2,true");
}

void test_create_element_ns_constructs_and_prefixes_after() {
    is(R"JS((function () {
        var during;
        customElements.define('x-prefixed', class extends HTMLElement {
            constructor() { super(); during = this.prefix + ':' + this.tagName; }
        });
        var made = document.createElementNS('http://www.w3.org/1999/xhtml', 'q:x-prefixed');
        return [during, made.prefix, made.tagName, made.localName,
                made instanceof customElements.get('x-prefixed')].join();
    })())JS",
       "null:X-PREFIXED,q,Q:X-PREFIXED,x-prefixed,true");
}

} // namespace

int main() {
    test_element_from_point_is_retargeted();
    test_window_event_is_set_around_each_listener();
    test_a_shadow_root_is_not_cloned();
    test_connected_move_reaches_into_a_plain_hosts_shadow_tree();
    test_the_selection_lets_go_of_a_range_whose_host_left();
    test_create_element_ns_constructs_and_prefixes_after();
    REPORT("shadow_hits_and_moves");
}
