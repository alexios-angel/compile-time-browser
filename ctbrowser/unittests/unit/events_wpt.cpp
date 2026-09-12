// THE DISPATCH RULES dom/events FOUND MISSING, one case per behaviour: where an
// event's path crosses a shadow boundary and where it stops, what `target` and
// `window.event` read on each side of that boundary, that a detached tree's
// events reach neither the document nor the window, and where `offsetX` is
// measured from.
//
// Each case names the WPT file it stands in for. They are here rather than in
// event_listeners because every one is about the PATH - which objects an event
// visits and what it looks like at each - where that file is about what a
// listener does once reached.

#include <ctbrowser.hpp>

#include "check.hpp"

#include <string>
#include <vector>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

constexpr const char * page_html = R"(<!DOCTYPE html>
<html><head><title>events</title>
<style>body { margin: 8px; padding: 0; }</style>
</head><body>
<div id=target>hello</div>
</body></html>)";

// ONE EXPRESSION, EVALUATED AFTER THE PAGE HAS LAID OUT. `frame()` is what
// makes `box_of` answer, and the offsetX case needs a box to measure from; the
// rest do not care and share the helper so there is one.
[[nodiscard]] std::string answer(const std::string & expression) {
    browser page{browser_options{400, 300}};
    page.load_html(page_html);
    (void)page.frame();
    const std::string source = "try { console.log(String(" + expression +
                               ")); } catch (e) { console.log('threw:' + e.name); }";
    if (!page.run_script(source)) { return "<did not run: " + page.script_error() + ">"; }
    const std::vector<std::string> & logged = page.bindings().console_output();
    if (logged.empty()) { return "<nothing logged: " + page.script_error() + ">"; }
    return logged.back();
}

void is(const std::string & expression, const std::string & expected) {
    const std::string got = answer(expression);
    CHECK_EQ(got, expected);
    if (got != expected) { std::printf("    %s\n", expression.c_str()); }
}

// The tree Event-dispatch-listener-order.window.js builds: a detached
// <section> holding a <div> host, whose closed shadow tree holds <p><span>.
// Every node on both sides gets a capturing and a bubbling listener that logs
// its nodeName, and the target is the innermost <span>.
constexpr const char * listener_order_setup =
    "var hostParent = document.createElement('section'),"
    "    host = hostParent.appendChild(document.createElement('div')),"
    "    shadowRoot = host.attachShadow({mode: 'closed'}),"
    "    targetParent = shadowRoot.appendChild(document.createElement('p')),"
    "    target = targetParent.appendChild(document.createElement('span')),"
    "    path = [hostParent, host, shadowRoot, targetParent, target], result = [];"
    "path.forEach(function (node) {"
    "    node.addEventListener('test', function () { result.push('bubbling ' + node.nodeName); });"
    "    node.addEventListener('test', function () { result.push('capturing ' + node.nodeName); },"
    "                          true);"
    "});";

// --- the path through a shadow tree ---------------------------------------

void test_a_composed_event_crosses_the_shadow_boundary() {
    // Event-dispatch-listener-order.window.js. The path used to stop at the
    // shadow root, so the host and its parent heard nothing.
    is(std::string{"(function () {"} + listener_order_setup +
           " target.dispatchEvent(new CustomEvent('test', {bubbles: true, composed: true}));"
           " return result.join(','); })()",
       "capturing SECTION,capturing DIV,capturing #document-fragment,capturing P,"
       "capturing SPAN,bubbling SPAN,bubbling P,bubbling #document-fragment,bubbling DIV,"
       "bubbling SECTION");
}

void test_an_uncomposed_event_stops_at_the_shadow_root() {
    // DOM's "get the parent" for a shadow root: null unless composed.
    is(std::string{"(function () {"} + listener_order_setup +
           " target.dispatchEvent(new CustomEvent('test', {bubbles: true}));"
           " return result.join(','); })()",
       "capturing #document-fragment,capturing P,capturing SPAN,bubbling SPAN,bubbling P,"
       "bubbling #document-fragment");
}

void test_the_target_is_retargeted_at_the_host() {
    // relatedTarget.window.js and shadow-relatedTarget.html both read `target`
    // from outside the tree. The span sees itself; the host sees ITSELF, at
    // phase AT_TARGET; and after the dispatch the target is the host, because
    // that is the last shadow-adjusted target and it is in the light tree.
    is("(function () {"
       " var host = document.body.appendChild(document.createElement('div'));"
       " var root = host.attachShadow({mode: 'open'});"
       " var span = root.appendChild(document.createElement('span'));"
       " var seen = [];"
       " span.addEventListener('t', function (e) {"
       "     seen.push((e.target === span) + ':' + e.eventPhase); });"
       " host.addEventListener('t', function (e) {"
       "     seen.push((e.target === host) + ':' + e.eventPhase); });"
       " var e = new Event('t', {composed: true});"
       " span.dispatchEvent(e);"
       " seen.push(e.target === host);"
       " return seen.join(','); })()",
       "true:2,true:2,true");
    // A host hears a NON-BUBBLING composed event too - its shadow-adjusted
    // target is itself, so it is a target and not a bystander.
    is("(function () {"
       " var host = document.createElement('div');"
       " var root = host.attachShadow({mode: 'open'});"
       " var span = root.appendChild(document.createElement('span'));"
       " var heard = 0;"
       " host.addEventListener('t', function () { ++heard; });"
       " span.dispatchEvent(new Event('t', {composed: true}));"
       " return heard; })()",
       "1");
    // And an event that never left the shadow tree has NO target afterwards:
    // concept-event-dispatch's clearTargets, which is what keeps a closed
    // tree's nodes off an object the light tree can read.
    is("(function () {"
       " var host = document.createElement('div');"
       " var root = host.attachShadow({mode: 'closed'});"
       " var span = root.appendChild(document.createElement('span'));"
       " var e = new Event('t');"
       " span.dispatchEvent(e);"
       " return e.target; })()",
       "null");
}

void test_window_event_is_hidden_inside_a_shadow_tree() {
    // event-global.html: a listener on a node whose root is a shadow root does
    // not see `window.event`; the host's listener does.
    is("(function () {"
       " var host = document.createElement('div');"
       " var root = host.attachShadow({mode: 'closed'});"
       " var span = root.appendChild(document.createElement('span'));"
       " var seen = [];"
       " span.addEventListener('t', function (e) { seen.push(window.event === undefined); });"
       " host.addEventListener('t', function (e) { seen.push(window.event === e); });"
       " span.dispatchEvent(new Event('t', {composed: true, bubbles: true}));"
       " seen.push(window.event === undefined);"
       " return seen.join(','); })()",
       "true,true,true");
}

void test_related_target_is_retargeted_and_cleared() {
    // relatedTarget.window.js: a relatedTarget inside a closed tree names the
    // host outside it; a target inside one leaves both null afterwards; and a
    // relatedTarget that retargets to the target itself is no dispatch at all.
    is("(function () {"
       " var host = document.body.appendChild(document.createElement('div'));"
       " var shadow = host.attachShadow({mode: 'closed'});"
       " var inner = shadow.appendChild(document.createElement('div'));"
       " var seen = [];"
       " document.body.addEventListener('demo', function (e) {"
       "   seen.push(e.relatedTarget === host); });"
       " var e1 = new FocusEvent('demo', {relatedTarget: inner});"
       " document.body.dispatchEvent(e1); seen.push(e1.relatedTarget === host);"
       " var e2 = new FocusEvent('demo', {relatedTarget: host});"
       " inner.dispatchEvent(e2); seen.push(e2.target === null && e2.relatedTarget === null);"
       " var e3 = new FocusEvent('demo', {relatedTarget: shadow});"
       " var ran = false; host.addEventListener('demo', function () { ran = true; });"
       " host.dispatchEvent(e3); seen.push(!ran && e3.target === null);"
       " return seen.join(','); })()",
       "true,true,true,true");
}

void test_focus_moves_with_blur_and_related_targets() {
    // shadow-relatedTarget.html: focus leaving one field for another fires
    // `blur` naming the next and `focus` naming the previous, neither
    // bubbling, `focusin`/`focusout` bubbling - and the one inside a closed
    // tree is seen from outside as its host.
    browser page{browser_options{400, 300}};
    page.load_html(R"(<!DOCTYPE html><html><body><div id=host></div><input id=light><script>
    var root = host.attachShadow({mode: 'closed'}); root.innerHTML = '<input id=s>';
    var seen = [];
    ['focus', 'blur', 'focusin', 'focusout'].forEach(function (t) {
      document.body.addEventListener(t, function (e) {
        seen.push('body:' + t + ':' + e.eventPhase); });
      light.addEventListener(t, function (e) {
        seen.push(t + ':' + (e.relatedTarget === host) + ':' + e.bubbles); });
    });
    root.getElementById('s').focus(); light.focus();
    console.log(seen.join(' '));
    </script></body></html>)");
    CHECK_EQ(page.bindings().console_output().back(),
             "body:focusin:3 body:focusout:3 focus:true:false focusin:true:true body:focusin:3");
}

// --- a detached tree ---------------------------------------------------------

void test_a_detached_tree_reaches_neither_document_nor_window() {
    // The DOM chain runs parent to parent and the document's parent is the
    // window; a root that is not the document has no parent. The same element
    // inserted into the body reaches both.
    is("(function () {"
       " var heard = 0, f = function () { ++heard; };"
       " document.addEventListener('t', f);"
       " window.addEventListener('t', f);"
       " var d = document.createElement('div');"
       " d.dispatchEvent(new Event('t', {bubbles: true}));"
       " var before = heard;"
       " document.body.appendChild(d);"
       " d.dispatchEvent(new Event('t', {bubbles: true}));"
       " return before + ',' + heard; })()",
       "0,2");
}

// --- offsetX -----------------------------------------------------------------

void test_offset_x_is_measured_from_the_target_box() {
    // mouse-event-retarget.html: `clientX: 50` on a div at the body's 8px
    // margin is offsetX 42. It used to be a copy of clientX.
    is("(function () {"
       " var seen = '';"
       " var target = document.getElementById('target');"
       " target.addEventListener('click', function (e) {"
       "     seen = e.offsetX + ',' + e.offsetY; });"
       " target.dispatchEvent(new MouseEvent('click', {clientX: 50, clientY: 20}));"
       " return seen; })()",
       "42,12");
    // AND FROM A PARSER-INSERTED SCRIPT, before any frame has laid the page
    // out - which is when the file dispatches. The box is flushed on demand.
    browser page{browser_options{400, 300}};
    std::string html{page_html};
    html.insert(html.find("</body>"),
                "<script>document.getElementById('target').addEventListener('click',"
                " function (e) { console.log(e.offsetX); });"
                "document.getElementById('target').dispatchEvent("
                "new MouseEvent('click', {clientX: 50}));</script>");
    page.load_html(html);
    CHECK_EQ(page.bindings().console_output().back(), std::string{"42"});
}

// --- activation behaviour ----------------------------------------------------

void test_a_javascript_link_runs_its_script_once_as_a_task() {
    // Event-dispatch-click.html "pick the first with activation behavior <a
    // href>": two nested `javascript:` anchors, a click at the inner one, and
    // only the inner script runs - later, as a task, not inside the dispatch.
    browser page{browser_options{400, 300}};
    page.load_html("<!DOCTYPE html><html><body><script>"
                   "window.ran = [];"
                   "var link = document.createElement('a');"
                   "link.href = 'javascript:ran.push(%27link%27)';"
                   "document.body.appendChild(link);"
                   "var child = link.appendChild(document.createElement('a'));"
                   "child.href = 'javascript:ran.push(\"child\")';"
                   "child.dispatchEvent(new MouseEvent('click', {bubbles: true}));"
                   "console.log('sync=' + ran.join());"
                   "setTimeout(function () { console.log('later=' + ran.join()); }, 0);"
                   "</script></body></html>");
    (void)page.tick(16.0);
    const std::vector<std::string> & logged = page.bindings().console_output();
    CHECK_EQ(logged.size(), std::size_t{2});
    if (logged.size() == 2) {
        CHECK_EQ(logged[0], std::string{"sync="});
        CHECK_EQ(logged[1], std::string{"later=child"});
    }
}

void test_queue_microtask_shares_the_promise_queue() {
    browser page{browser_options{400, 300}};
    page.load_html(page_html);
    CHECK(page.run_script("Promise.resolve().then(function () { console.log('promise'); });"
                          "queueMicrotask(function () { console.log('micro'); });"
                          "console.log('sync');"));
    const std::vector<std::string> & logged = page.bindings().console_output();
    CHECK_EQ(logged.size(), std::size_t{3});
    if (logged.size() == 3) {
        CHECK_EQ(logged[0], std::string{"sync"});
        CHECK_EQ(logged[1], std::string{"promise"});
        CHECK_EQ(logged[2], std::string{"micro"});
    }
    is("(function () { try { queueMicrotask(1); } catch (e) { return e.name; } })()", "TypeError");
}

void test_an_image_input_submits_its_form() {
    browser page{browser_options{400, 300}};
    page.load_html("<!DOCTYPE html><html><body><form id=f><input name=q value=1>"
                   "<input id=go type=image alt=go></form><script>"
                   "document.getElementById('f').addEventListener('submit', function () {"
                   " console.log('submit'); });"
                   "document.getElementById('go').click();"
                   "</script></body></html>");
    CHECK_EQ(page.bindings().console_output().size(), std::size_t{1});
    CHECK_EQ(page.last_submission().size(), std::size_t{1});
}

} // namespace

int main() {
    test_related_target_is_retargeted_and_cleared();
    test_focus_moves_with_blur_and_related_targets();
    test_a_composed_event_crosses_the_shadow_boundary();
    test_an_uncomposed_event_stops_at_the_shadow_root();
    test_the_target_is_retargeted_at_the_host();
    test_window_event_is_hidden_inside_a_shadow_tree();
    test_a_detached_tree_reaches_neither_document_nor_window();
    test_offset_x_is_measured_from_the_target_box();
    test_a_javascript_link_runs_its_script_once_as_a_task();
    test_queue_microtask_shares_the_promise_queue();
    test_an_image_input_submits_its_form();
    REPORT("events_wpt");
}
