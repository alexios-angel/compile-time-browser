// EVENT DISPATCH: an event travelling its whole path, `new EventTarget()`, the
// event interface hierarchy, what `dispatchEvent` refuses and what a listener
// is called with, `passive`, `once` and `capture`, the `on...` handler
// properties, and bubbling with `preventDefault`. The listener-invocation
// RULES dom/events was getting wrong one at a time are in event_listeners.cpp.
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

// An event travelling its whole path, which is the part that was missing.
//
// Dispatch used to be three lines: fire the global bucket, walk the ancestors,
// fire the global bucket again. Nothing carried `currentTarget`, nothing carried
// `eventPhase`, `stopPropagation` was a no-op, the document and the window were
// one indistinguishable bucket, and nothing a page CONSTRUCTED could be
// dispatched at all. Every one of those is asserted here.
void test_events_travel_the_whole_path() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><body><div id=outer><div id=inner></div></div><script>
        var outer = document.getElementById('outer');
        var inner = document.getElementById('inner');
        var seen = [];
        function note(name) {
          return function (e) {
            var where = e.currentTarget === window ? 'window'
                      : e.currentTarget === document ? 'document'
                      : e.currentTarget.id;
            seen.push(name + ':' + e.eventPhase + ':' + where);
          };
        }
        window.addEventListener('poke', note('w-cap'), true);
        document.addEventListener('poke', note('d-cap'), true);
        outer.addEventListener('poke', note('o-cap'), true);
        inner.addEventListener('poke', note('i-cap'), true);
        inner.addEventListener('poke', note('i'), false);
        outer.addEventListener('poke', note('o'), false);
        document.addEventListener('poke', note('d'), false);
        window.addEventListener('poke', note('w'), false);

        var evt = document.createEvent('Event');
        console.log('fresh=' + evt.type + ',' + evt.bubbles + ',' + evt.cancelable +
                    ',' + evt.eventPhase);
        evt.initEvent('poke', true, true);
        var ok = inner.dispatchEvent(evt);
        console.log('path=' + seen.join('|'));
        console.log('after=' + ok + ',' + evt.eventPhase + ',' + (evt.currentTarget === null) +
                    ',' + evt.target.id + ',' + (evt.srcElement === evt.target));

        // A NON-BUBBLING event still captures all the way down; only the bubble
        // pass is cut to the target.
        seen.length = 0;
        inner.dispatchEvent(new Event('poke'));
        console.log('nobubble=' + seen.join('|'));

        // preventDefault needs `cancelable`, and dispatchEvent reports it.
        inner.addEventListener('stop', function (e) { e.preventDefault(); }, false);
        console.log('cancel=' + inner.dispatchEvent(new Event('stop', {cancelable: true})) +
                    ',' + inner.dispatchEvent(new Event('stop')));

        // stopPropagation ends the path after the step it was called on.
        seen.length = 0;
        inner.addEventListener('halt', note('i-halt'), false);
        inner.addEventListener('halt', function (e) { e.stopPropagation(); }, false);
        outer.addEventListener('halt', note('o-halt'), false);
        inner.dispatchEvent(new Event('halt', {bubbles: true}));
        console.log('halted=' + seen.join('|'));

        // ... and stopImmediatePropagation ends it DURING the step.
        seen.length = 0;
        inner.addEventListener('halt2', function (e) { e.stopImmediatePropagation(); }, false);
        inner.addEventListener('halt2', note('i-halt2'), false);
        inner.dispatchEvent(new Event('halt2', {bubbles: true}));
        console.log('immediate=' + seen.length);

        var custom = new CustomEvent('mine', {detail: 7, bubbles: true});
        var got = 0;
        document.addEventListener('mine', function (e) { got = e.detail; });
        inner.dispatchEvent(custom);
        console.log('custom=' + got + ',' + (custom instanceof Event) +
                    ',' + (custom.constructor === CustomEvent));
        console.log('kinds=' + (evt instanceof Event) + ',' + (evt.constructor === Event) +
                    ',' + Event.AT_TARGET + ',' + evt.BUBBLING_PHASE);
      </script></body></html>)");
    check(page.script_error().empty(), "the event script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log.size() == 9, "every line was logged");
    check(log[0] == "fresh=,false,false,0",
          "createEvent hands back an uninitialised event: " + log[0]);
    check(log[1] == "path=w-cap:1:window|d-cap:1:document|o-cap:1:outer|i-cap:2:inner|"
                    "i:2:inner|o:3:outer|d:3:document|w:3:window",
          "capture down to the target, then bubble back to the window: " + log[1]);
    check(log[2] == "after=true,0,true,inner,true",
          "the event stops travelling when the dispatch ends: " + log[2]);
    check(log[3] == "nobubble=w-cap:1:window|d-cap:1:document|o-cap:1:outer|i-cap:2:inner|"
                    "i:2:inner",
          "a non-bubbling event reaches the target and stops: " + log[3]);
    check(log[4] == "cancel=false,true", "preventDefault needs cancelable: " + log[4]);
    check(log[5] == "halted=i-halt:2:inner", "stopPropagation ends the path: " + log[5]);
    check(log[6] == "immediate=0", "stopImmediatePropagation ends the step: " + log[6]);
    check(log[7] == "custom=7,true,true", "CustomEvent carries detail: " + log[7]);
    check(log[8] == "kinds=true,true,2,3", "Event identity and the phase constants: " + log[8]);
}

// `new EventTarget()` - a listener list with no node under it, and the three
// methods a subclass inherits. Also the DOM's duplicate rule, which applies
// everywhere and was implemented nowhere: registering the same
// (type, callback, capture) twice must do nothing.
void test_a_standalone_event_target() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><body><script>
        var et = new EventTarget();
        var n = 0;
        function once() { n += 100; }
        function each() { n += 1; }
        et.addEventListener('go', once, {once: true});
        et.addEventListener('go', each);
        et.addEventListener('go', each);   // the same three: must be ignored
        et.dispatchEvent(new Event('go'));
        et.dispatchEvent(new Event('go'));
        console.log('counts=' + n);

        // A standalone target IS its whole path: nothing reaches the document.
        var leaked = 0;
        document.addEventListener('go', function () { leaked++; });
        et.dispatchEvent(new Event('go'));
        console.log('leak=' + leaked);

        // preventDefault reaches dispatchEvent's answer here too.
        et.addEventListener('no', function (e) { e.preventDefault(); });
        console.log('cancel=' + et.dispatchEvent(new Event('no', {cancelable: true})));

        // The three methods are on the PROTOTYPE, so a subclass has them and
        // `this` is the instance rather than the base.
        class Nicer extends EventTarget {
          fire(d) { this.dispatchEvent(new CustomEvent('x', {detail: d})); }
        }
        var sub = new Nicer();
        var got = 0;
        sub.addEventListener('x', function (e) { got = e.detail; });
        sub.fire(9);
        console.log('sub=' + got + ',' + (sub instanceof EventTarget));
      </script></body></html>)");
    check(page.script_error().empty(), "the EventTarget script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log.size() == 4, "every line was logged");
    check(log[0] == "counts=102", "once fires once and a duplicate is not added: " + log[0]);
    check(log[1] == "leak=0", "a standalone target does not reach the document: " + log[1]);
    check(log[2] == "cancel=false", "preventDefault reaches dispatchEvent: " + log[2]);
    check(log[3] == "sub=9,true", "a subclass inherits the three methods: " + log[3]);
}

// THE EVENT INTERFACES, and the four things that have to be true of each one.
//
// There were two - `Event` and `CustomEvent` - and every other spelling was
// undefined, so `new MouseEvent('click')` threw. That is not a corner: a
// constructor is the ONLY way a page can synthesise an event, and a page that
// wants to drive its own UI has nothing else to build one with.
//
// The subclass case at the bottom is the one that decides the shape of the
// whole file: a constructor that allocates its own object leaves `super()`'s
// caller holding an instance nothing was written to, so every property the
// subclass inherits reads undefined. Every constructor here initialises its
// RECEIVER instead, which is also what makes `new` and `super()` one path.
void test_the_event_interface_hierarchy() {
    browser page{browser_options{200, 150}};
    page.load_html(R"(<html><body><script>
        var m = new MouseEvent('click', {bubbles: true, clientX: 4, clientY: 5,
                                         button: 2, shiftKey: true});
        console.log('mouse=' + m.type + ',' + m.clientX + ',' + m.clientY + ',' + m.button +
                    ',' + m.shiftKey + ',' + m.bubbles + ',' + m.detail);
        // The whole chain, and every link in it is a real prototype rather than
        // a marker object: a page tests `instanceof` to tell events apart.
        console.log('chain=' + (m instanceof MouseEvent) + ',' + (m instanceof UIEvent) +
                    ',' + (m instanceof Event) + ',' + (m.constructor === MouseEvent) +
                    ',' + m.constructor.name);
        // An absent dictionary gives the DEFAULTS the IDL names, not undefined.
        var u = new UIEvent('x');
        console.log('defaults=' + u.view + ',' + u.detail + ',' + u.bubbles + ',' +
                    u.cancelable + ',' + u.isTrusted);
        var k = new KeyboardEvent('keydown', {key: 'a', keyCode: 65, ctrlKey: true});
        console.log('key=' + k.key + ',' + k.keyCode + ',' + k.which + ',' +
                    k.getModifierState('Control') + ',' + k.getModifierState('Shift') +
                    ',' + (k.initKeyEvent === undefined));
        var w = new WheelEvent('wheel', {deltaY: 3.5, deltaMode: 1});
        console.log('wheel=' + w.deltaY + ',' + w.deltaMode + ',' + (w instanceof MouseEvent));
        // `null` and a missing dictionary are the same answer - WebIDL converts
        // both to the dictionary with every member defaulted.
        console.log('nullinit=' + new FocusEvent('focus', null).relatedTarget + ',' +
                    new CompositionEvent('c').data.length);
        // An interface object is not callable, the type is mandatory, and a
        // nullable interface member is not "anything, or null".
        var errs = '';
        try { Event('x'); } catch (e) { errs += e.name; }
        try { new Event(); } catch (e) { errs += ',' + e.name; }
        try { new UIEvent('x', {view: 7}); } catch (e) { errs += ',' + e.name; }
        console.log('throws=' + errs);
        // `isTrusted` is [LegacyUnforgeable]: an OWN accessor of every instance,
        // sharing ONE getter function. Both halves are observable.
        var d1 = Object.getOwnPropertyDescriptor(new Event('a'), 'isTrusted');
        var d2 = Object.getOwnPropertyDescriptor(new Event('b'), 'isTrusted');
        console.log('trusted=' + (typeof d1.get) + ',' + (d1.get === d2.get));
        // A page can EXTEND one, which is the whole reason a constructor
        // initialises `this` rather than allocating.
        class Mine extends MouseEvent {
          constructor(type, init) { super(type, init); this.extra = 12; }
        }
        var mine = new Mine('click', {clientX: 3, cancelable: true});
        console.log('extend=' + mine.extra + ',' + mine.clientX + ',' + mine.cancelable +
                    ',' + (mine instanceof MouseEvent) + ',' + (mine instanceof Event));
      </script></body></html>)");
    check(page.script_error().empty(), "the interface script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log.size() == 9, "every line was logged");
    check(log[0] == "mouse=click,4,5,2,true,true,0", "MouseEventInit is read in full: " + log[0]);
    check(log[1] == "chain=true,true,true,true,MouseEvent",
          "the prototype chain is real all the way to Event: " + log[1]);
    check(log[2] == "defaults=null,0,false,false,false",
          "an absent dictionary means the IDL's defaults: " + log[2]);
    check(log[3] == "key=a,65,65,true,false,true",
          "KeyboardEvent carries its members and not initKeyEvent: " + log[3]);
    check(log[4] == "wheel=3.5,1,true", "WheelEvent inherits MouseEvent: " + log[4]);
    check(log[5] == "nullinit=null,0", "a null dictionary is an absent one: " + log[5]);
    check(log[6] == "throws=TypeError,TypeError,TypeError",
          "not callable, type mandatory, view type-checked: " + log[6]);
    check(log[7] == "trusted=function,true",
          "isTrusted is an own accessor with one shared getter: " + log[7]);
    check(log[8] == "extend=12,3,true,true,true",
          "a page can subclass an event interface: " + log[8]);
}

// WHAT dispatchEvent REFUSES, AND WHAT A LISTENER IS CALLED WITH.
//
// Three things a page can see, and all three were silently wrong.
//
// `dispatchEvent` used to accept anything: an event still travelling, an event
// `document.createEvent` had never been given a type, `null`. Each is an
// InvalidStateError or a TypeError in the specification and each was a quiet
// success here, which is the worst answer - the page believes it dispatched.
//
// A listener used to be called with `this` UNDEFINED, so
// `el.addEventListener('click', function () { this.classList.add('on') })` -
// which is how a great deal of shipped code is written - went nowhere at all.
// And an OBJECT with a `handleEvent` method is a listener too: EventListener is
// a callback interface, and the method is looked up at DISPATCH time, so an
// object that grows one after registering still works.
void test_dispatch_refuses_and_binds_this() {
    browser page{browser_options{200, 150}};
    page.load_html(R"(<html><body><div id=t></div><script>
        var t = document.getElementById('t');
        var errs = '';
        var fresh = document.createEvent('Event');
        try { t.dispatchEvent(fresh); }
        catch (e) { errs += e.name + ':' + e.code + ':' + (e.constructor === DOMException); }
        fresh.initEvent('go', true, true);
        t.addEventListener('go', function (e) {
          try { t.dispatchEvent(e); } catch (err) { errs += '|' + err.name; }
        });
        t.dispatchEvent(fresh);
        console.log('refuse=' + errs);

        var seen = '';
        var lookups = 0;
        t.addEventListener('bound', function () { seen += (this === t) + ';'; });
        t.addEventListener('bound', {
          tag: 'obj',
          get handleEvent() {
            lookups++;
            return function () { seen += this.tag + ';'; };
          }
        });
        t.dispatchEvent(new Event('bound'));
        t.dispatchEvent(new Event('bound'));
        console.log('this=' + seen + 'lookups=' + lookups);

        var during = null;
        var atWindow = '';
        t.addEventListener('peek', function () { during = window.event; });
        // A HANDLER PROPERTY ON THE WINDOW. It is the one target whose
        // `on<type>` never fired: the window is a PROXY and the guard tested
        // for a plain object, so every window handler property in the engine
        // was dead and an element's - the only kind any test used - was not.
        // Bubbling, or the event never reaches the window to begin with.
        window.onpeek = function (e) { atWindow = e.type + ':' + (this === window); };
        var peek = new Event('peek', {bubbles: true});
        t.dispatchEvent(peek);
        console.log('global=' + (during === peek) + ',' + (window.event === undefined) +
                    ',' + ('event' in window) + ',' + atWindow);

        var et = new EventTarget();
        var fired = 0;
        var ac = new AbortController();
        ac.abort();
        et.addEventListener('x', function () { fired++; }, {signal: ac.signal});
        et.dispatchEvent(new Event('x'));
        var bad = '';
        try { et.addEventListener('y', function () {}, {signal: null}); }
        catch (e) { bad = e.name; }
        console.log('signal=' + fired + ',' + bad);
      </script></body></html>)");
    check(page.script_error().empty(), "the dispatch script ran: " + page.script_error());
    const auto & log = log_of(page);
    check(log.size() == 4, "every line was logged");
    // 11 is InvalidStateError's legacy code, and the constructor test is the
    // one an ECMAScript Error cannot pass - see bindings/exceptions.cpp.
    check(log[0] == "refuse=InvalidStateError:11:true|InvalidStateError",
          "an uninitialised event and a re-entered one are both refused: " + log[0]);
    check(log[1] == "this=true;obj;true;obj;lookups=2",
          "`this` is the current target, and handleEvent is looked up every time: " + log[1]);
    check(log[2] == "global=true,true,true,peek:true",
          "window.event travels, exists outside a dispatch, and the window's own "
          "handler property fires: " +
              log[2]);
    check(log[3] == "signal=0,TypeError",
          "an aborted signal adds nothing and a null one throws: " + log[3]);
}

// `{passive: true}` IS ENFORCED, AND A THROWING LISTENER IS REPORTED.
//
// Passive is a promise not to call preventDefault, and the DOM does not take it
// on trust: the canceled flag is not set while a passive listener runs. It is a
// scrolling optimisation everywhere it is used - a compositor cannot start a
// scroll until it knows the page will not refuse it - so an engine that lets a
// passive listener cancel has removed the entire point of the option.
//
// The second half is the other end of a gap wpt.md already records at load
// time: a listener that threw reached `callback_error()`, which is an EMBEDDER
// channel, and the PAGE was told nothing - no `error` event, no `window.onerror`.
// Worse, the VM's failure flag stayed up for the rest of the dispatch, so every
// LATER listener for the same event was silently declined. One listener
// throwing stopped all the others.
void test_passive_listeners_and_a_throwing_one() {
    browser page{browser_options{200, 150}};
    page.load_html(R"(<html><body><script>
        var seen = '';
        function cancels(e) { e.preventDefault(); seen += e.defaultPrevented + ','; }
        var et = new EventTarget();
        et.addEventListener('p', cancels, {passive: true});
        var a = et.dispatchEvent(new Event('p', {cancelable: true}));
        // `passive` is NOT part of a listener's identity, so this removes it.
        et.removeEventListener('p', cancels);
        et.addEventListener('p', cancels);
        var b = et.dispatchEvent(new Event('p', {cancelable: true}));
        console.log('passive=' + seen + a + ',' + b);

        // `returnValue = false` is preventDefault under another name and is
        // refused the same way.
        var et2 = new EventTarget();
        var got = '';
        et2.addEventListener('r', function (e) {
          e.returnValue = false;
          got += e.defaultPrevented;
        }, {passive: true});
        et2.dispatchEvent(new Event('r', {cancelable: true}));
        console.log('return=' + got);

        // The flag belongs to the LISTENER, not the event: a non-passive one
        // later in the same dispatch can still cancel.
        var et3 = new EventTarget();
        et3.addEventListener('m', function () {}, {passive: true});
        et3.addEventListener('m', function (e) { e.preventDefault(); });
        console.log('mixed=' + et3.dispatchEvent(new Event('m', {cancelable: true})));

        // A throwing listener reaches the page twice - as an `error` event and
        // as window.onerror, which takes a STRING and not the event - and does
        // not stop the listener after it.
        var reports = '';
        window.addEventListener('error', function (e) {
          reports += 'evt:' + (typeof e.message) + ';';
          e.preventDefault();
        });
        window.onerror = function (message) { reports += 'on:' + (typeof message) + ';'; };
        var t = document.createElement('div');
        var after = 0;
        t.addEventListener('boom', function () { throw new Error('from the listener'); });
        t.addEventListener('boom', function () { after++; });
        t.dispatchEvent(new Event('boom'));
        console.log('fault=' + reports + 'after=' + after);
      </script></body></html>)");
    // EMPTY because the page HANDLED it: preventDefault on an error event is
    // how a page says so, and the embedder channel is the console, which a
    // handled exception does not reach.
    check(page.script_error().empty(),
          "a handled fault stays off the embedder channel: " + page.script_error());
    const auto & log = log_of(page);
    check(log.size() == 4, "every line was logged");
    check(log[0] == "passive=false,true,true,false",
          "a passive listener cannot cancel and a plain one can: " + log[0]);
    check(log[1] == "return=false", "returnValue is refused in a passive listener too: " + log[1]);
    check(log[2] == "mixed=false", "the flag is cleared after each passive listener: " + log[2]);
    check(log[3] == "fault=evt:string;on:string;after=1",
          "a throwing listener is reported and does not stop the next one: " + log[3]);
}

// `once` AND `capture` ON A LISTENER.
//
// Both were accepted and ignored. `once` meant a listener a page registered to
// run exactly once ran on every event - a one-shot "has the user interacted
// yet" handler kept firing. `capture` meant a listener that asked to see an
// event BEFORE its target saw it ran after instead, which is the entire reason
// to pass the flag.
void test_listener_options() {
    browser page{browser_options{300, 200}};
    page.load_html(R"(<html><body style="margin:0">
        <div id=outer style="width:100px;height:100px">
          <div id=inner style="width:50px;height:50px"></div>
        </div>
        <script>
          var log = '';
          const outer = document.getElementById('outer');
          const inner = document.getElementById('inner');
          outer.addEventListener('click', function () { log += 'outer-capture;'; }, { capture: true });
          outer.addEventListener('click', function () { log += 'outer-bubble;'; });
          inner.addEventListener('click', function () { log += 'inner;'; });
          // The old spelling: a bare boolean means capture.
          document.addEventListener('click', function () { log += 'doc-capture;'; }, true);
          var counted = 0;
          inner.addEventListener('click', function () { counted++; }, { once: true });
          function report() { console.log(log + ' counted=' + counted); }
        </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    (void)page.frame();
    for (int i = 0; i < 3; ++i) {
        (void)page.handle(input_event::mouse_down_at(10, 10));
        (void)page.handle(input_event::mouse_up_at(10, 10));
    }
    (void)page.run_script("report();");
    const std::string & line = log_of(page).back();
    // Down to the target first, then back up: the document's capturing
    // listener is outermost and runs before the outer element's, which runs
    // before the target's own.
    const std::size_t doc = line.find("doc-capture;");
    const std::size_t cap = line.find("outer-capture;");
    const std::size_t at = line.find("inner;");
    const std::size_t bubble = line.find("outer-bubble;");
    check(doc != std::string::npos && cap != std::string::npos && at != std::string::npos &&
              bubble != std::string::npos,
          "every phase fired: " + line);
    check(doc < cap && cap < at && at < bubble,
          "capture runs outermost-first and before the target, bubble after: " + line);
    // Three clicks, one call.
    check(line.find("counted=1") != std::string::npos,
          "a `once` listener fires exactly once: " + line);
}

void test_click_dispatch() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><head><style>#a { width: 200px; height: 100px }</style></head>
    <body><div id=a>click me</div><script>
    document.getElementById('a').addEventListener('click', function (e) {
      console.log('clicked ' + e.type);
    });
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");
    check(log_of(page).empty(), "nothing fired yet");

    // A click is a press and a release on the same element - which is what
    // makes dragging off a button cancel it.
    (void)page.handle(input_event::mouse_down_at(20, 20));
    (void)page.handle(input_event::mouse_up_at(20, 20));
    check(log_of(page).size() == 1, "the listener fired once");
    if (!log_of(page).empty()) { check(log_of(page)[0] == "clicked click", "with the event type"); }

    // Released somewhere else: no click.
    (void)page.handle(input_event::mouse_down_at(20, 20));
    (void)page.handle(input_event::mouse_up_at(380, 290));
    check(log_of(page).size() == 1, "releasing off the element does not click it");
}

// `el.onclick = fn` - THE OTHER HALF OF THE EVENT API, and it did nothing.
//
// Assigning a handler stored a function on the wrapper that nothing ever looked
// at, so a page written the older way fired no callbacks and reported no
// problem. p5.js needs it on its own load path: loadImage sets img.onload and
// img.onerror and awaits a promise those two settle.
void test_handler_properties() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><head><style>#a { width: 200px; height: 100px }</style></head>
    <body><div id=a>click me</div><script>
    var a = document.getElementById('a');
    a.onclick = function (e) { console.log('handler ' + e.type + ' ' + (this === a)); };
    a.addEventListener('click', function () { console.log('listener'); });
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");

    (void)page.handle(input_event::mouse_down_at(20, 20));
    (void)page.handle(input_event::mouse_up_at(20, 20));
    const auto & log = log_of(page);
    check(log.size() == 2, "both the listener and the handler property fired");
    if (log.size() == 2) {
        // `this` is the element, which is what a handler written this way reads.
        check(log[0] == "listener" && log[1] == "handler click true",
              "the handler runs with the event and the element as `this`");
    }

    // ASSIGNING OVER ONE REPLACES IT - that is the whole difference from
    // addEventListener, and a page that reassigns in a loop relies on it.
    (void)page.run_script("a.onclick = function () { console.log('replaced'); };");
    (void)page.handle(input_event::mouse_down_at(20, 20));
    (void)page.handle(input_event::mouse_up_at(20, 20));
    check(log_of(page).size() == 4 && log_of(page)[3] == "replaced",
          "the second assignment replaced the first rather than adding to it");
    (void)page.run_script("a.onclick = null;");
    (void)page.handle(input_event::mouse_down_at(20, 20));
    (void)page.handle(input_event::mouse_up_at(20, 20));
    check(log_of(page).size() == 5, "and null removes it, leaving the listener");
}

void test_events_bubble_and_can_be_prevented() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<html><head><style>#outer { width: 300px; height: 200px }</style></head>
    <body><div id=outer><div id=inner>x</div></div><script>
    document.getElementById('inner').addEventListener('click', function (e) {
      console.log('inner'); e.preventDefault();
    });
    document.getElementById('outer').addEventListener('click', function () { console.log('outer'); });
    document.addEventListener('click', function () { console.log('document'); });
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");

    (void)page.handle(input_event::mouse_down_at(10, 10));
    (void)page.handle(input_event::mouse_up_at(10, 10));
    const auto & log = log_of(page);
    check(log.size() == 3, "the event reached all three listeners");
    if (log.size() == 3) {
        // Order matters: a listener on the target must see the event before one
        // on its parent, or preventDefault from the inner one is pointless.
        check(log[0] == "inner" && log[1] == "outer" && log[2] == "document",
              "and bubbled outwards in order");
    }
}

} // namespace

int main() {
    test_click_dispatch();
    test_handler_properties();
    test_events_bubble_and_can_be_prevented();
    test_listener_options();
    test_events_travel_the_whole_path();
    test_a_standalone_event_target();
    test_the_event_interface_hierarchy();
    test_dispatch_refuses_and_binds_this();
    test_passive_listeners_and_a_throwing_one();
    REPORT("event_dispatch");
}
