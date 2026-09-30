// `<iframe>` - a nested browsing context, which here is a second Document.
//
// Its own file because what it pins down is a MODEL rather than a method, the
// same way unit/second_document.cpp does: a frame is a second `dom_bindings`
// over its own tree, with a REALM of its own for its scripts, and the file that
// builds it is lib/Shell/bindings/frames.cpp. These cases are the parts of that
// decision that would otherwise drift.
//
// TWO OF THEM ASSERT A LIMIT, and those are the important ones: the frame's
// realm shares the page's intrinsics (one `Array`, one `Node`), and a `src`
// that resolves to nothing still leaves a Document behind and reports `error`
// rather than `load`. A test that only checked the happy path would let each
// of those turn into a silent wrong answer.
//
// THE ORDERING IS THE POINT of the last two: a page's own `load` handler is
// where a test reads `frame.contentDocument`, so the frame's document has to
// exist BEFORE that event - and the frame's own `load`, like a sheet's, is
// settled just before it, since HTML delays the window's load until the
// subresources have (bindings/events/input.cpp, dispatch).

#include <ctbrowser.hpp>

#include "check.hpp"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

// Two frame sources, seeded into the registry rather than written to disk, so
// the test is hermetic and needs no document root: `asset_registry::load`
// consults the registry before it ever looks at the filesystem.
constexpr const char * inner_html = "<!DOCTYPE html><html><head><title>inner</title></head>"
                                    "<body><p id=greeting>hello</p></body></html>";
constexpr const char * inner_xml = "<?xml version=\"1.0\"?><root viewBox=\"0 0 1 1\">"
                                   "<Leaf/>text</root>";

// Load a page with the two frames available by name, run enough ticks for the
// reconcile and the frame's load event to have happened, and answer with what
// the expression logged.
[[nodiscard]] std::string answer(const std::string & body, const std::string & expression) {
    browser page{browser_options{400, 300}};
    // An address, so a frame's `location` resolves against something: a
    // submission's `?query` is only a query of an absolute URL.
    page.set_location("file:///pages/the-page.html");
    page.assets().add("inner.html", bytes_of(inner_html));
    page.assets().add("inner.xml", bytes_of(inner_xml));
    page.assets().add("self.html", bytes_of("<p>self</p><iframe id=s src=self.html></iframe>"));
    // A `setTimeout` INSIDE the window's load handler. One tick does, in
    // order: reconcile the frames, settle the frame and sheet loads, dispatch
    // the page's own `load`, then run the timers - so the timer is a moment at
    // which everything has happened, whichever way the middle two are ordered.
    const std::string html =
        "<!DOCTYPE html><html><head><title>the page</title></head><body>" + body +
        "<script>window.addEventListener('load', function () {"
        " setTimeout(function () {"
        " try { console.log(String(" +
        expression +
        ")); } catch (e) { console.log('threw:' + e.name); } }, 0); });</script></body></html>";
    page.load_html(html);
    for (int i = 0; i < 4; ++i) { (void)page.tick(16.0); }
    const std::vector<std::string> & logged = page.bindings().console_output();
    if (logged.empty()) { return "<nothing logged: " + page.script_error() + ">"; }
    return logged.back();
}

void is(const std::string & body, const std::string & expression, const std::string & expected) {
    const std::string got = answer(body, expression);
    CHECK_EQ(got, expected);
    if (got != expected) { std::printf("    %s\n", expression.c_str()); }
}

constexpr const char * one_frame = "<iframe id=f src=inner.html></iframe>";

void test_a_frame_has_a_document_of_its_own() {
    is(one_frame, "document.getElementById('f').contentDocument.title", "inner");
    is(one_frame,
       "document.getElementById('f').contentDocument.getElementById('greeting').textContent",
       "hello");
    // NOT THE PAGE'S DOCUMENT. The whole model is that it is a second one, and
    // an implementation that answered with the page's would pass every
    // assertion above.
    is(one_frame, "document.getElementById('f').contentDocument === document", "false");
    is(one_frame, "document.getElementById('f').contentDocument.contentType", "text/html");
    // The element the frame is, reached back through the frame's window.
    is(one_frame, "document.getElementById('f').contentWindow.frameElement.id", "f");
    is(one_frame, "document.getElementById('f').contentWindow.document.title", "inner");
    is(one_frame, "document.getElementById('f').contentWindow.parent === window", "true");
    // THE FRAME'S WINDOW REACHES THE PAGE'S GLOBALS, because the realm is one:
    // `assert_throws_dom` is handed `root.ownerDocument.defaultView
    // .DOMException` and needs a constructor whose `.name` is "DOMException"
    // and whose prototype an exception the frame's DOM threw is on.
    is(one_frame,
       "document.getElementById('f').contentDocument.defaultView === "
       "document.getElementById('f').contentWindow",
       "true");
    is(one_frame, "document.getElementById('f').contentWindow.DOMException.name", "DOMException");
    is(one_frame,
       "(function () { var d = document.getElementById('f').contentDocument;"
       " try { d.querySelector(''); } catch (e) {"
       " return (e instanceof d.defaultView.DOMException) && e.name === 'SyntaxError'; } })()",
       "true");
    is(one_frame, "'TypeError' in document.getElementById('f').contentWindow", "true");
    // ...BUT NOT THE PAGE'S BROWSING-CONTEXT STATE: a frame's `location` is
    // its own document's, never the page's.
    is(one_frame,
       "(function () { var w = document.getElementById('f').contentWindow;"
       " return (w.location === window.location) + ',' + /inner\\.html$/.test(w.location.href);"
       " })()",
       "false,true");
}

void test_a_frame_whose_source_is_xml_is_parsed_as_xml() {
    constexpr const char * xml_frame = "<iframe id=f src=inner.xml></iframe>";
    is(xml_frame, "document.getElementById('f').contentDocument.contentType", "application/xml");
    // CASE IS PRESERVED, which is the whole reason the XML front end exists: the
    // HTML tree builder would answer `LEAF` here and lowercase `viewBox`.
    is(xml_frame, "document.getElementById('f').contentDocument.documentElement.tagName", "root");
    is(xml_frame,
       "document.getElementById('f').contentDocument.documentElement.getAttribute('viewBox')",
       "0 0 1 1");
}

void test_a_frame_with_no_source_is_still_a_document() {
    // about:blank, which a page uses as a scratch document: `frame.contentDocument
    // .body` has to be there to write into.
    is("<iframe id=f></iframe>", "document.getElementById('f').contentDocument.body.tagName",
       "BODY");
    is("<iframe id=f></iframe>", "document.getElementById('f').contentDocument.contentType",
       "text/html");
}

void test_a_source_that_resolves_to_nothing_still_loads() {
    // A Document is still there - a browser shows its own error page in one -
    // and it is `load` that the element hears, as for a 404: an iframe's
    // navigation always ends in a document. An EMPTY file is the same case.
    is("<iframe id=f src=missing.html></iframe>",
       "document.getElementById('f').contentDocument.body.tagName", "BODY");
    // The listener is registered by a script in the markup, which runs while
    // the document is still being parsed and so is in place before the first
    // reconcile. `answer`'s own script runs on the window's load event, which
    // is one moment too late for this.
    is("<iframe id=f src=missing.html></iframe><script>"
       "document.getElementById('f').addEventListener('error', function () {"
       " window.__saw = 'error'; });"
       "document.getElementById('f').addEventListener('load', function () {"
       " window.__saw = 'load'; });</script>",
       "String(window.__saw)", "load");
}

void test_the_load_event_arrives_at_the_frame() {
    // Registered in the markup, so the handler exists before the reconcile runs
    // - which is the ordering the file's header is about.
    is("<iframe id=f src=inner.html></iframe><script>"
       "document.getElementById('f').addEventListener('load', function () {"
       " window.__loaded = (window.__loaded || 0) + 1; });</script>",
       "String(window.__loaded)", "1");
}

void test_a_frame_appended_by_script_loads_too() {
    // The one that cannot go through `answer`: the frame does not exist until
    // the page's own load handler has run, so what it reports has to be read a
    // tick later, from the frame's OWN load event.
    browser page{browser_options{400, 300}};
    page.assets().add("inner.html", bytes_of(inner_html));
    page.load_html("<!DOCTYPE html><html><body>"
                   "<script>window.addEventListener('load', function () {"
                   " var f = document.createElement('iframe');"
                   " f.onload = function () { console.log(f.contentDocument.title); };"
                   " f.src = 'inner.html';"
                   " document.body.appendChild(f); });</script></body></html>");
    for (int i = 0; i < 4; ++i) { (void)page.tick(16.0); }
    const std::vector<std::string> & logged = page.bindings().console_output();
    CHECK(!logged.empty());
    if (!logged.empty()) { CHECK_EQ(logged.back(), std::string{"inner"}); }
}

void test_a_data_url_frame_carries_its_own_type() {
    is("<iframe id=f src='data:text/html,<p>x</p>'></iframe>",
       "document.getElementById('f').contentDocument.contentType", "text/html");
}

void test_a_frame_runs_its_scripts_in_its_own_realm() {
    // The frame's `var`s are the frame's and not the page's; its function,
    // called BY THE PAGE, reads the frame's `document` and the frame's
    // globals (dom/ranges' Range-test-iframe.html is exactly this: the page
    // calls the frame's `run()`); an inline handler in the frame compiles in
    // the frame's realm; and the frame's window hears its own `load` before
    // the <iframe>'s reaches the page. The intrinsics are still shared.
    browser page{browser_options{400, 300}};
    page.set_location("file:///pages/the-page.html");
    page.assets().add("scripted.html",
                      bytes_of("<html><head><title>framed</title></head>"
                               "<body onload=\"log.push('body:' + where)\"><script>"
                               "var log = []; var where = 'frame'; window.__ranInFrame = 1;"
                               "function run() { return document.title + ':' + where; }"
                               "addEventListener('load', function () { log.push('listener'); });"
                               "</script></body></html>"));
    page.load_html(
        "<!DOCTYPE html><html><head><title>page</title></head><body>"
        "<iframe id=f src=scripted.html></iframe><script>"
        "var where = 'page'; var order = [];"
        "document.getElementById('f').addEventListener('load', function () {"
        "  order.push(this.contentWindow.log.slice().sort().join('+')); });"
        "window.addEventListener('load', function () {"
        "  var f = document.getElementById('f'), w = f.contentWindow;"
        "  console.log([String(window.__ranInFrame), w.__ranInFrame, w.run(), w.where, where,"
        "    typeof run, w.document === f.contentDocument, w.parent === window,"
        "    w.Array === Array, order.join('|')].join());"
        "});</script></body></html>");
    for (int i = 0; i < 3; ++i) { (void)page.tick(16.0); }
    const std::vector<std::string> & logged = page.bindings().console_output();
    CHECK(!logged.empty());
    if (!logged.empty()) {
        CHECK_EQ(logged.back(), std::string{"undefined,1,framed:frame,frame,page,undefined,true,"
                                            "true,true,body:frame+listener"});
    }
}

void test_windows_post_messages_to_each_other() {
    // HTML 9.4.3: the message is CLONED when posted and arrives in a later
    // task as a MessageEvent whose `source` is the sender's window - so the
    // frame's reply names the frame, and the page's names the page.
    browser page{browser_options{400, 300}};
    page.set_location("file:///pages/the-page.html");
    page.assets().add(
        "echo.html",
        bytes_of("<html><body><script>"
                 "addEventListener('message', function (e) {"
                 "  e.source.postMessage({got: e.data.n, fromParent: e.source === parent,"
                 "    type: e.constructor.name}, '*'); });"
                 "</script></body></html>"));
    page.load_html(
        "<!DOCTYPE html><html><body><iframe id=f src=echo.html></iframe><script>"
        "var log = [];"
        "addEventListener('message', function (e) {"
        "  var w = document.getElementById('f').contentWindow;"
        "  log.push([e.data.got, e.data.fromParent, e.data.type, e.source === w, e.origin,"
        "            e.ports.length].join());"
        "  console.log(log.join('|')); });"
        "window.addEventListener('load', function () {"
        "  var msg = {n: 1}, w = document.getElementById('f').contentWindow;"
        "  w.postMessage(msg, '*'); msg.n = 2; log.push('sync');"
        "});</script></body></html>");
    for (int i = 0; i < 5; ++i) { (void)page.tick(16.0); }
    const std::vector<std::string> & logged = page.bindings().console_output();
    CHECK(!logged.empty());
    if (!logged.empty()) {
        CHECK_EQ(logged.back(), std::string{"sync|1,true,MessageEvent,true,null,0"});
    }
}

void test_a_shadowed_origin_does_not_forge_a_message() {
    // ACCESS-006: the sender's `origin` and the targetOrigin check come from
    // the document's URL. A page that shadows `window.origin` neither signs
    // its messages with the shadow nor receives one addressed to it.
    browser page{browser_options{400, 300}};
    page.set_location("file:///pages/the-page.html");
    page.load_html("<!DOCTYPE html><html><body><script>"
                   "var log = [];"
                   "addEventListener('message', function (e) {"
                   "  log.push(e.data + ':' + e.origin); console.log(log.join('|')); });"
                   "Object.defineProperty(window, 'origin', {value: 'https://bank.example'});"
                   "postMessage('addressed', 'https://bank.example');"
                   "postMessage('any', '*');"
                   "</script></body></html>");
    for (int i = 0; i < 5; ++i) { (void)page.tick(16.0); }
    const std::vector<std::string> & logged = page.bindings().console_output();
    CHECK(!logged.empty());
    if (!logged.empty()) { CHECK_EQ(logged.back(), std::string{"any:null"}); }
}

void test_a_frame_window_scrolls_its_own_viewport() {
    // scrollIntoView-iframes.html: a frame's message handler answers with its
    // `scrollY`, which a frame window did not have.
    is("<iframe id=f src=inner.html></iframe>",
       "[typeof document.getElementById('f').contentWindow.scrollY,"
       " typeof document.getElementById('f').contentWindow.scrollTo].join()",
       "number,function");
}

void test_a_frames_function_and_eval_are_the_frames() {
    // CreateDynamicFunction and PerformEval take the callee's realm: code made
    // by a frame's Function, or run by its eval, resolves its free names in
    // the frame - here the frame's named element, which the page lacks.
    is("<iframe id=f src=inner.html></iframe>",
       "[new (document.getElementById('f').contentWindow.Function)('return typeof greeting')(),"
       " document.getElementById('f').contentWindow.eval('typeof greeting'),"
       " typeof greeting].join()",
       "object,object,undefined");
}

void test_report_error_fires_at_its_this_window() {
    // reporterror-cross-realm-method.html: the page's `reportError` called on
    // a frame's window reports in the FRAME - `this`'s global, not the realm
    // the method came from.
    browser page{browser_options{400, 300}};
    page.load_html("<!DOCTYPE html><html><body><iframe id=f></iframe><script>"
                   "var log = [];"
                   "addEventListener('error', function () { log.push('page'); });"
                   "window.addEventListener('load', function () {"
                   "  var w = document.getElementById('f').contentWindow;"
                   "  w.addEventListener('error', function (e) { log.push('frame:' + e.error); });"
                   "  reportError.call(w, 'x'); console.log(log.join()); });"
                   "</script></body></html>");
    for (int i = 0; i < 5; ++i) { (void)page.tick(16.0); }
    const std::vector<std::string> & logged = page.bindings().console_output();
    CHECK(!logged.empty());
    if (!logged.empty()) { CHECK_EQ(logged.back(), std::string{"frame:x"}); }
}

void test_a_removed_frames_callbacks() {
    // settimeout-detached-iframe.html: a timer set on a frame that is then
    // removed never fires - the attached frame's does. And an animation
    // frame callback of a frame's realm reports its throw at the FRAME
    // (callback-cross-realm-report-exception.html).
    browser page{browser_options{400, 300}};
    page.load_html(
        "<!DOCTYPE html><html><body><div id=c></div><script>"
        "var ran = [];"
        "function make(detach) { var f = document.createElement('iframe');"
        "  document.getElementById('c').appendChild(f); var w = f.contentWindow;"
        "  if (detach) { f.remove(); } return w; }"
        "var kept = make(false), gone = make(true);"
        "kept.setTimeout(function () { ran.push('kept'); }, 0);"
        "gone.setTimeout(function () { ran.push('gone'); }, 0);"
        "addEventListener('error', function (e) { ran.push('top'); e.preventDefault(); });"
        "kept.addEventListener('error', function (e) { ran.push('frame');"
        "  e.preventDefault(); });"
        "kept.requestAnimationFrame(new kept.Function('throw 1'));"
        "setTimeout(function () { setTimeout(function () {"
        "  console.log(ran.join()); }, 0); }, 0);"
        "</script></body></html>");
    for (int i = 0; i < 6; ++i) { (void)page.tick(16.0); }
    const std::vector<std::string> & logged = page.bindings().console_output();
    CHECK(!logged.empty());
    if (!logged.empty()) { CHECK_EQ(logged.back(), std::string{"kept,frame"}); }
}

void test_a_named_frame_is_its_window_on_the_window() {
    // nameditem-02.html: `window.x` for `<iframe name=x>` is the frame's
    // WindowProxy - HTML 7.3.3 puts child navigables first - while an id
    // still names the element.
    is("<iframe name=x src=inner.html id=y></iframe>",
       "(x === document.getElementsByName('x')[0].contentWindow) + ',' + x.document.title + ','"
       " + y.tagName",
       "true,inner,IFRAME");
}

void test_the_frames_are_indexed_on_the_window() {
    // Node-removeChild.html reads `frames[0].document`: `frames` is the
    // window, `length` counts the child navigables, and an index is one's
    // WindowProxy - and not a property past the end.
    is("<iframe src=inner.html></iframe>",
       "(frames === window) + ',' + window.length + ',' + frames[0].document.title + ','"
       " + (0 in window) + ',' + (1 in window) + ',' + String(window[1]) + ','"
       " + (new frames[0].Text('x').ownerDocument === frames[0].document) + ','"
       " + (new frames[0].Comment('x') instanceof Comment)",
       "true,1,inner,true,false,undefined,true,true");
}

void test_an_inserted_frame_has_its_window_at_once() {
    // event-global-extra.window.js: `appendChild(iframe).contentWindow` in the
    // same statement, before any tick has reconciled the frames.
    browser page{browser_options{400, 300}};
    page.load_html("<!DOCTYPE html><html><body><script>"
                   "var w = document.body.appendChild(document.createElement('iframe'))"
                   ".contentWindow; console.log(w.document.body.nodeName + ',' +"
                   " (w.frameElement === document.querySelector('iframe')));"
                   "</script></body></html>");
    CHECK_EQ(page.bindings().console_output().back(), std::string{"BODY,true"});
}

void test_a_frame_and_a_script_inserted_together_run_in_tree_order() {
    // Node-appendChild-script-and-iframe.html: the post-connection steps run
    // per inserted node in tree order, so a script inserted AFTER a frame in
    // the same appendChild sees the frame's window, and one inserted BEFORE
    // it sees null - the frame is set up after the script has run.
    browser page{browser_options{400, 300}};
    page.load_html("<!DOCTYPE html><html><body><script>"
                   "var seen = [];"
                   "function both(frameFirst) {"
                   "  window.f = document.createElement('iframe');"
                   "  var s = document.createElement('script');"
                   "  s.textContent = 'seen.push(f.contentWindow ? \"window\" : \"null\")';"
                   "  var div = document.createElement('div');"
                   "  if (frameFirst) { div.append(f, s); } else { div.append(s, f); }"
                   "  document.body.appendChild(div);"
                   "  seen.push(f.contentWindow ? 'later:window' : 'later:null');"
                   "}"
                   "both(true); both(false);"
                   "console.log(seen.join());"
                   "</script></body></html>");
    CHECK_EQ(page.bindings().console_output().back(),
             std::string{"window,later:window,null,later:window"});
}

void test_a_frame_document_is_a_full_member_of_the_realm() {
    // Node-isConnected.html's iframe case: the page's node goes INTO a frame's
    // document (adopted, connected there), and a frame inside a frame gets a
    // document of its own - while a document with no browsing context does
    // not give its <iframe> one.
    is(one_frame,
       R"JS((function () {
        var f = document.getElementById('f');
        var d = document.createElement('div');
        f.contentDocument.body.appendChild(d);
        var inner = f.contentDocument.createElement('iframe');
        f.contentDocument.body.appendChild(inner);
        var parsed = new DOMParser().parseFromString('<iframe></iframe>', 'text/html');
        return [d.isConnected, d.ownerDocument === f.contentDocument,
                inner.contentDocument !== null && inner.contentDocument.body.nodeName,
                String(parsed.querySelector('iframe').contentDocument)].join();
    })())JS",
       "true,true,BODY,null");
}

void test_a_frame_loaded_at_a_fragment_has_a_target() {
    // `:target` inside a frame loaded as `inner.html#greeting`, and the frame
    // document's URL is the resolved src, fragment and all.
    is("<iframe id=f src='inner.html#greeting'></iframe>",
       R"JS((function () {
        var d = document.getElementById('f').contentDocument;
        var hit = d.querySelector(':target');
        return [hit && hit.id, d.getElementById('greeting').matches(':target'),
                document.querySelectorAll(':target').length,
                /inner\.html#greeting$/.test(d.URL)].join();
    })())JS",
       "greeting,true,0,true");
}

// THE RENDERING HALF: a frame's document is laid out at the size its box got
// (browser/nested.cpp), so what a script measures inside it is about the
// frame. `100vw` in a 200px-wide frame is 200px, a media query in the frame's
// own sheet is evaluated against the frame's viewport, and the two reads that
// reach that layout - getComputedStyle through the frame's window, offsetWidth
// on the frame's element - see a write to the frame's document at once.
void test_a_frame_document_is_laid_out_at_the_size_of_its_box() {
    constexpr const char * sized_frame =
        "<style>iframe { width: 200px; height: 100px; border: 0 }</style>"
        "<iframe id=f src=inner.html></iframe>";
    is(sized_frame,
       R"JS((function () {
        var f = document.getElementById('f'), d = f.contentDocument, w = f.contentWindow;
        d.body.innerHTML = '<style>* { margin: 0 } div { height: 100vw; width: 50vh }'
            + ' @media (width: 200px) { div { color: rgb(1, 2, 3) } }</style><div></div>';
        var div = d.querySelector('div'), cs = w.getComputedStyle(div);
        return [cs.height, cs.width, cs.color, div.offsetWidth,
                d.querySelectorAll('style').length, w.getComputedStyle(d.body).marginLeft].join();
    })())JS",
       "200px,50px,rgb(1, 2, 3),50,1,0px");
    // A SECOND WRITE IS SEEN BY THE NEXT READ: the frame has no mutation hook,
    // so the flush compares the document's version with the one laid out.
    is(sized_frame,
       R"JS((function () {
        var f = document.getElementById('f'), d = f.contentDocument, w = f.contentWindow;
        d.body.innerHTML = '<div style="height: 10vh"></div>';
        var first = w.getComputedStyle(d.querySelector('div')).height;
        d.body.innerHTML = '<div style="height: 20vh"></div>';
        return first + ',' + w.getComputedStyle(d.querySelector('div')).height;
    })())JS",
       "10px,20px");
    // A frame with no box - display: none - renders nothing, and its document
    // reads as unrendered: the empty declaration, as through the page's window.
    is("<iframe id=f src=inner.html style='display: none'></iframe>",
       "document.getElementById('f').contentWindow.getComputedStyle("
       "document.getElementById('f').contentDocument.body).height === ''",
       "true");
}

} // namespace

// HTML 4.10.21.3: a GET submission aimed at a frame this document names
// navigates the frame to the action with the entries as its query. PLANNED:
// the frame still has its old document when submit() returns, and the new
// one - whose `location` reads the query back - by the frame's next `load`.
void test_a_form_submits_into_a_named_frame() {
    browser page{browser_options{400, 300}};
    page.set_location("file:///pages/the-page.html");
    page.assets().add("inner.html", bytes_of(inner_html));
    page.load_html("<!DOCTYPE html><html><body>"
                   "<form id=fm action=inner.html target=f><input name=a value='x y'>"
                   "<input name=b value=2></form><iframe id=f name=f></iframe><script>"
                   "addEventListener('load', function () {"
                   " var f = document.getElementById('f');"
                   " document.getElementById('fm').submit();"
                   " var before = f.contentDocument.title;"
                   " f.onload = function () { var w = f.contentWindow;"
                   "  console.log([before, w.location.search, w.document.title,"
                   "   w.location === w.document.location, w.location === window.location]"
                   "   .join('|')); }; });</script></body></html>");
    for (int i = 0; i < 4; ++i) { (void)page.tick(16.0); }
    const std::vector<std::string> & logged = page.bindings().console_output();
    CHECK(!logged.empty());
    if (!logged.empty()) { CHECK_EQ(logged.back(), std::string{"|?a=x+y&b=2|inner|true|false"}); }
}

// window.open(), HTML 7.2.2.1: a URL that does not parse is a SyntaxError
// before anything else; `_blank` is a popup, which this user agent blocks
// (null); a name an <iframe> has navigates it and answers its window.
void test_window_open_follows_the_window_open_steps() {
    is("<iframe id=f name=f></iframe>",
       "(function () { var threw = ''; try { window.open('http://[') } catch (e) {"
       " threw = e.name + (e instanceof DOMException); }"
       " return [threw, window.open(), window.open('inner.html', '_blank'),"
       " window.open('inner.html', 'f') === document.getElementById('f').contentWindow,"
       " window.open('inner.html', 'f', 'noopener')].join('|'); })()",
       "SyntaxErrortrue|||true|");
    // ...and the navigation it started has happened a tick later - to the
    // ABSOLUTE URL, which a page from disk reads as that path.
    browser page{browser_options{400, 300}};
    page.set_location("file:///pages/the-page.html");
    page.assets().add("/pages/inner.html", bytes_of(inner_html));
    page.load_html("<!DOCTYPE html><html><body><iframe id=f name=f></iframe><script>"
                   "addEventListener('load', function () {"
                   " var f = document.getElementById('f');"
                   " window.open('inner.html?x=1', 'f');"
                   " f.onload = function () {"
                   "  console.log(f.contentDocument.title + f.contentWindow.location.search); };"
                   "});</script></body></html>");
    for (int i = 0; i < 4; ++i) { (void)page.tick(16.0); }
    const std::vector<std::string> & logged = page.bindings().console_output();
    CHECK(!logged.empty());
    if (!logged.empty()) { CHECK_EQ(logged.back(), std::string{"inner?x=1"}); }
}

// THE LOCATION OBJECT, HTML 7.2.4: getters over the document's URL, and
// setters that navigate - `window.location = url` forwards to `href`
// ([PutForwards]), a URL that does not parse is a SyntaxError, and a new
// fragment is a fragment navigation whose `hashchange` comes a task later.
void test_the_location_object_navigates() {
    is(one_frame,
       "(function () { var w = document.getElementById('f').contentWindow, threw = '';"
       " try { w.location = 'http://[' } catch (e) { threw = e.name; }"
       " var heard = []; w.addEventListener('hashchange', function (e) {"
       "  heard.push(e.newURL); });"
       " w.location.hash = 'there'; var now = w.location.href + ',' + heard.length;"
       " var own = Object.getOwnPropertyDescriptor(w.location, 'href');"
       " return [threw, now, typeof own.get, own.configurable, w.location.hash,"
       "  String(w.location) === w.location.href].join('|'); })()",
       "SyntaxError|file:///pages/inner.html#there,0|function|false|#there|true");
    // A frame's `location.href =` navigates the frame, a tick later.
    browser page{browser_options{400, 300}};
    page.set_location("file:///pages/the-page.html");
    page.assets().add("/pages/inner.html", bytes_of(inner_html));
    page.load_html("<!DOCTYPE html><html><body><iframe id=f></iframe><script>"
                   "addEventListener('load', function () {"
                   " var f = document.getElementById('f');"
                   " f.contentWindow.location.href = 'inner.html?y=2';"
                   " f.onload = function () {"
                   "  console.log(f.contentDocument.title + f.contentWindow.location.search); };"
                   "});</script></body></html>");
    for (int i = 0; i < 4; ++i) { (void)page.tick(16.0); }
    const std::vector<std::string> & logged = page.bindings().console_output();
    CHECK(!logged.empty());
    if (!logged.empty()) { CHECK_EQ(logged.back(), std::string{"inner?y=2"}); }
}

// HTML 4.8.5's insertion steps: a frame inserted with no `src` fires its
// about:blank `load` DURING the insertion - heard by a listener added before
// it, never by one added after - and a `src` given afterwards is the only
// `load` a later listener hears (dom/ranges' Range-test-iframe setup).
void test_a_blank_frame_loads_during_its_insertion() {
    browser page{browser_options{400, 300}};
    page.set_location("file:///pages/the-page.html");
    page.assets().add("inner.html", bytes_of(inner_html));
    page.load_html("<!DOCTYPE html><html><body><script>"
                   "var heard = [];"
                   "addEventListener('load', function () {"
                   " var f = document.createElement('iframe');"
                   " f.onload = function () { heard.push('before'); };"
                   " document.body.appendChild(f); heard.push('appended');"
                   " var g = document.body.appendChild(document.createElement('iframe'));"
                   " g.onload = function () { heard.push(g.contentDocument.title); };"
                   " g.src = 'inner.html';"
                   " setTimeout(function () { console.log(heard.join()); }, 40);"
                   "});</script></body></html>");
    for (int i = 0; i < 6; ++i) { (void)page.tick(16.0); }
    const std::vector<std::string> & logged = page.bindings().console_output();
    CHECK(!logged.empty());
    if (!logged.empty()) { CHECK_EQ(logged.back(), std::string{"before,appended,inner"}); }
}

// HTML 4.8.5: an <iframe srcdoc> is its markup - text/html in UTF-8 at
// about:srcdoc, over any `src`, its scripts run in the frame - and a
// relative reference in it resolves against the page (the fallback base URL).
void test_a_srcdoc_frame_is_its_markup() {
    is("<iframe id=f src=inner.html srcdoc='<title>doc</title><a id=a href=x/y.html></a>"
       "<script>parent.__ran = document.URL</script>'></iframe>",
       "(function () { var d = document.getElementById('f').contentDocument;"
       " return [d.title, d.URL, window.__ran, d.contentType, d.characterSet,"
       " d.getElementById('a').href].join('|'); })()",
       "doc|about:srcdoc|about:srcdoc|text/html|UTF-8|file:///pages/x/y.html");
}

// HTML 7.4.2.3.2: a `javascript:` source navigates the about:blank a frame
// starts on - a task later, in its realm - to its string result, and a
// result that is no string leaves the frame as it was and announces nothing.
void test_a_javascript_source_is_its_string_result() {
    browser page{browser_options{400, 300}};
    page.set_location("file:///pages/the-page.html");
    page.load_html("<!DOCTYPE html><html><body>"
                   "<iframe id=f src=\"javascript:'<title>js</title>'\"></iframe>"
                   "<iframe id=g src='javascript:false'></iframe><script>"
                   "var heard = [], f = document.getElementById('f'),"
                   " g = document.getElementById('g');"
                   "f.onload = function () { heard.push(f.contentDocument.title); };"
                   "g.onload = function () { heard.push('g'); };"
                   "setTimeout(function () {"
                   " console.log(heard.join() + '|' + g.contentDocument.body.childNodes.length);"
                   "}, 60);</script></body></html>");
    for (int i = 0; i < 8; ++i) { (void)page.tick(16.0); }
    const std::vector<std::string> & logged = page.bindings().console_output();
    CHECK(!logged.empty());
    if (!logged.empty()) { CHECK_EQ(logged.back(), std::string{"js|0"}); }
}

// A FRAME IN A FRAME'S DOCUMENT LOADS ON THE TICK, not only when its
// `contentWindow` is read: the tick reconciles the page's frames and each
// frame document's own (html-forms/test-003.html waited for such a `load`).
void test_a_frame_inside_a_frame_document_loads() {
    is("<iframe id=b></iframe><script>var d = document.getElementById('b').contentDocument;"
       " var n = d.createElement('iframe'); n.src = 'inner.html'; window.nested = 'none';"
       " n.onload = function () { window.nested = n.contentDocument.title; };"
       " d.body.appendChild(n);</script>",
       "window.nested", "inner");
}

// THE ANIMATION FRAME CALLBACKS DOCUMENT BY DOCUMENT: the page's, then its
// frames' in tree order, whatever order they were asked for in
// (child-document-raf-order.html) - and a removed frame's never.
void test_animation_frames_run_in_document_order() {
    is("<iframe id=f1 src=inner.html></iframe><iframe id=f2 src=inner.html></iframe>"
       "<iframe id=f3 src=inner.html></iframe><script>var order = [];"
       " addEventListener('load', function () {"
       " var w = function (id) { return document.getElementById(id).contentWindow; };"
       " var gone = w('f3'); document.getElementById('f3').remove();"
       " gone.requestAnimationFrame(function () { order.push('removed'); });"
       " w('f2').requestAnimationFrame(function () { order.push('second');"
       " setTimeout(function () { console.log(order.join()); }, 0); });"
       " w('f1').requestAnimationFrame(function () { order.push('first'); });"
       " requestAnimationFrame(function () { order.push('page'); }); });</script>",
       "'unused'", "page,first,second");
}

// A PAGE THAT EMBEDS ITSELF STOPS AT ONE COPY (HTML 4.8.5: an ancestor
// navigable's document already has the URL, so the frame gets no navigable).
// Before the guard each copy's frame loaded the next, in one tick, until the
// native stack ran out.
void test_a_frame_that_embeds_itself_stops() {
    is("<iframe id=f src=self.html></iframe>",
       "document.getElementById('f').contentDocument.getElementById('s').contentDocument", "null");
}

int main() {
    test_a_frame_that_embeds_itself_stops();
    test_animation_frames_run_in_document_order();
    test_a_frame_inside_a_frame_document_loads();
    test_a_frame_document_is_laid_out_at_the_size_of_its_box();
    test_the_frames_are_indexed_on_the_window();
    test_a_frame_loaded_at_a_fragment_has_a_target();
    test_an_inserted_frame_has_its_window_at_once();
    test_a_frame_and_a_script_inserted_together_run_in_tree_order();
    test_a_frame_document_is_a_full_member_of_the_realm();
    test_a_named_frame_is_its_window_on_the_window();
    test_a_frame_has_a_document_of_its_own();
    test_a_frame_whose_source_is_xml_is_parsed_as_xml();
    test_a_frame_with_no_source_is_still_a_document();
    test_a_source_that_resolves_to_nothing_still_loads();
    test_the_load_event_arrives_at_the_frame();
    test_a_frame_appended_by_script_loads_too();
    test_a_data_url_frame_carries_its_own_type();
    test_a_frame_runs_its_scripts_in_its_own_realm();
    test_windows_post_messages_to_each_other();
    test_a_shadowed_origin_does_not_forge_a_message();
    test_report_error_fires_at_its_this_window();
    test_a_removed_frames_callbacks();
    test_a_frame_window_scrolls_its_own_viewport();
    test_a_frames_function_and_eval_are_the_frames();
    test_a_form_submits_into_a_named_frame();
    test_window_open_follows_the_window_open_steps();
    test_the_location_object_navigates();
    test_a_blank_frame_loads_during_its_insertion();
    test_a_srcdoc_frame_is_its_markup();
    test_a_javascript_source_is_its_string_result();
    REPORT("frames");
}
