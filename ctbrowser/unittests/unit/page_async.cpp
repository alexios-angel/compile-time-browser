// WHAT HAPPENS ON A LATER TURN: `await` suspending a frame and the frame
// surviving collection, `fetch` as genuinely asynchronous work with an abort
// that has something to abort, collection running on its own without freeing
// what the page still uses, and the timers and animation frames that drive all
// of it. Every case here needs an event loop, which is why none of them is in
// js/vm_basics.
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

[[nodiscard]] std::vector<std::byte> bytes_of(std::string_view text) {
    return std::vector<std::byte>{reinterpret_cast<const std::byte *>(text.data()),
                                  reinterpret_cast<const std::byte *>(text.data() + text.size())};
}

// `await` ON A PENDING PROMISE SUSPENDS THE FRAME.
//
// There is one stack and the event loop is above it, so await cannot block: the
// frame is lifted out of the register stack, the caller is handed a promise,
// and the frame goes back when the awaited promise settles. It used to read
// `__value` off a promise that had none - so await on anything genuinely
// asynchronous evaluated to UNDEFINED and ran the rest of the function
// immediately, which is the largest silent wrong answer this engine had left.
//
// Here rather than in js/vm_async because it needs an event loop: nothing
// suspends without something to resume it.
void test_await_suspends_and_resumes() {
    browser page{browser_options{200, 200}};
    page.load_html(R"(<html><body><script>
        var log = '';
        function gate() {
          const g = {};
          g.p = new Promise(function (ok, no) { g.go = ok; g.fail = no; });
          return g;
        }

        // Several awaits in one function, with LOCALS that have to survive each
        // suspension - they live in the register window, which is copied out and
        // back.
        const a = gate(), b = gate();
        async function many() {
          let total = 100;
          const first = await a.p;
          total += first;
          const second = await b.p;
          return total + second;
        }
        many().then(function (v) { log += 'many(' + v + ');'; });

        // A rejection across a suspension reaches a catch INSIDE the function.
        const c = gate();
        async function caught() {
          try { await c.p; return 'no throw'; } catch (e) { return 'caught:' + e; }
        }
        caught().then(function (v) { log += v + ';'; });

        // ...and an uncaught one rejects the function's own promise rather than
        // ending the run.
        const d = gate();
        async function uncaught() { await d.p; return 'unreached'; }
        uncaught().then(function () { log += 'wrong;'; },
                       function (e) { log += 'rejected:' + e + ';'; });

        // An async function awaiting another that suspends - the outer one
        // suspends too, and both come back in order.
        const e = gate();
        async function inner() { return await e.p; }
        async function outer() { return 'outer(' + (await inner()) + ')'; }
        outer().then(function (v) { log += v + ';'; });

        // Nothing has run past its first await yet: that is the point.
        console.log('atLoad=[' + log + ']');
        setTimeout(function () { a.go(1); b.go(2); c.fail('boom'); d.fail('bang'); e.go(9); }, 0);
        function report() { console.log('after=[' + log + ']'); }
    </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    // Enough ticks that the timer fires and every resumption drains. Each await
    // costs a turn, and the nested pair costs two.
    for (int frame = 0; frame < 12; ++frame) { page.tick(16); }
    (void)page.run_script("report();");

    const auto & log = log_of(page);
    check(log[0] == "atLoad=[]", "no function ran past its first await at load: " + log[0]);
    const std::string & after = log.back();
    // 100 + 1 + 2: the locals were still there after two suspensions.
    check(after.find("many(103);") != std::string::npos,
          "locals survive several suspensions: " + after);
    check(after.find("caught:boom;") != std::string::npos,
          "a rejection throws AT the await, so try/catch spans it: " + after);
    check(after.find("rejected:bang;") != std::string::npos,
          "an uncaught rejection rejects the function's own promise: " + after);
    check(after.find("outer(9);") != std::string::npos,
          "an async function awaiting one that suspends suspends too: " + after);
}

// A `value` CAPTURED BY A NATIVE LAMBDA IS NOT A ROOT.
//
// `new Promise(fn)` handed its executor a resolve function that held the promise
// in a C++ lambda capture. The collector walks a native's properties, not its
// captures, so a promise nothing else referenced was freed while the page was
// still holding the resolve that would settle it - and settling a recycled cell
// does nothing, silently, because settle() checks is_object() first.
//
// The symptom was an async function that could suspend EXACTLY ONCE. The first
// await's promise was still in a live frame's registers; a promise created
// during the resumption existed only in those captures and in its own handler
// list - a cycle with no root - so the second await never came back.
//
// test_await_suspends_and_resumes has two awaits and passed throughout: its
// gates are top-level consts, so they were rooted. Only a promise created DURING
// the resumption shows it, which is what every real loader does - `await
// fetch(u)` and then `await response.bytes()`.
void test_a_promise_made_during_a_resumption_survives() {
    browser page{browser_options{200, 200}};
    page.load_html(R"(<html><body><script>
        var log = '';
        // No reference kept anywhere: the promise exists only inside the
        // executor's resolve and, once awaited, in its own handler list.
        function later(v) {
          return new Promise(function (ok) { setTimeout(function () { ok(v); }, 0); });
        }
        async function three() {
          const a = await later(1);
          // Garbage between the suspensions, so a collection actually happens
          // while the next promise is the only thing holding itself up.
          for (var i = 0; i < 20000; i = i + 1) { var junk = { n: i }; }
          const b = await later(2);
          for (var j = 0; j < 20000; j = j + 1) { var more = { n: j }; }
          const c = await later(3);
          return a + b + c;
        }
        three().then(function (v) { log += 'sum=' + v + ';'; },
                     function (e) { log += 'rejected:' + e + ';'; });
        function report() { console.log('log=[' + log + ']'); }
    </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    // Three suspensions, each waiting on a timer registered by the previous
    // resumption - so this needs a turn per await plus the drains between them.
    for (int frame = 0; frame < 20; ++frame) { page.tick(16); }
    (void)page.run_script("report();");
    check(log_of(page).back() == "log=[sum=6;]",
          "all three suspensions resumed: " + log_of(page).back());
}

// A SUSPENDED FRAME IS A GC ROOT. Its register window is copied out of the
// register stack, which is what the collector normally walks - so without
// tracing it, everything a waiting function was holding is freed and comes back
// as garbage. The churn here is what makes the test mean something: a
// collection has to actually happen while the frame is away.
void test_a_suspended_frame_survives_collection() {
    browser page{browser_options{200, 200}};
    page.load_html(R"(<html><body><script>
        var log = '';
        const g = {};
        const p = new Promise(function (ok) { g.go = ok; });
        async function holds() {
          const mine = { tag: 'kept', list: [1, 2, 3], deep: { s: 'still here' } };
          const v = await p;
          return mine.tag + '/' + mine.list.join('') + '/' + mine.deep.s + '/' + v;
        }
        holds().then(function (r) { log += r; });
        setTimeout(function () {
          for (let i = 0; i < 60000; i++) { const junk = { i: i, s: 'x' + i, a: [i, i, i] }; }
          g.go('resumed');
        }, 0);
        function report() { console.log(log); }
    </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    for (int frame = 0; frame < 12; ++frame) { page.tick(16); }
    (void)page.run_script("report();");
    check(log_of(page).back() == "kept/123/still here/resumed",
          "a suspended frame's locals survive a collection: " + log_of(page).back());
}

// `fetch` IS ASYNCHRONOUS.
//
// It used to do the work and hand back an already-settled promise - the only
// option while `await` could not suspend, since a pending one would have
// evaluated to undefined and the rest of the function would have run with it.
// `await` suspends now, so a fetch can be what it is: work that finishes on a
// later turn.
//
// That is not pedantry. `await fetch(url)` used to return before any other timer
// or listener could run, so nothing a page does to stay responsive while loading
// was observable - and an AbortController had nothing to abort, because the
// request was over before the object existed.
void test_fetch_is_async() {
    browser page{browser_options{300, 200}};
    page.assets().add("data.json", bytes_of(R"({"name":"ctbrowser","n":3})"));
    page.load_html(R"(<html><body><script>
        var log = '';
        fetch('data.json').then(function (r) {
          log += 'ok=' + r.ok + ',' + r.status + ';';
          log += 'ct=' + r.headers.get('content-type') + ';';
          log += 'absent=' + r.headers.get('x-nothing') + ';';
          return r.json();
        }).then(function (j) { log += 'name=' + j.name + ',' + j.n + ';'; });
        // The request is OUTSTANDING here: nothing it produces may have
        // happened yet, which is the whole difference.
        log += 'sync;';
        function report() { console.log(log); }
    </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    (void)page.run_script("report();");
    check(log_of(page).back() == "sync;",
          "nothing resolved before the turn ended: " + log_of(page).back());

    for (int frame = 0; frame < 4; ++frame) { page.tick(16); }
    (void)page.run_script("report();");
    const std::string & after = log_of(page).back();
    check(after.find("ok=true,200;") != std::string::npos, "the response arrived: " + after);
    // `headers` is an OBJECT with get(), not a content-type string - a page
    // doing the ordinary thing used to throw on it.
    check(after.find("absent=null;") != std::string::npos,
          "an unknown header is null rather than an error: " + after);
    check(after.find("name=ctbrowser,3;") != std::string::npos, "json() parsed it: " + after);
}

// `await fetch(...)` across a real suspension, and the bytes three ways.
void test_fetch_await_and_bytes() {
    browser page{browser_options{300, 200}};
    page.assets().add("blob.bin",
                      std::vector<std::byte>{std::byte{1}, std::byte{2}, std::byte{255}});
    page.load_html(R"(<html><body><script>
        var log = '';
        (async function () {
          const r = await fetch('blob.bin');
          const buf = await r.arrayBuffer();
          // A view over the WHOLE buffer shares its storage, so this is the
          // response's bytes rather than a copy of them.
          const view = new Uint8Array(buf);
          log += 'len=' + buf.byteLength + ',' + view.length + ';';
          log += 'bytes=' + view[0] + ',' + view[1] + ',' + view[2] + ';';
          const again = await (await fetch('blob.bin')).bytes();
          log += 'bytes2=' + again.length + ';';
          const blob = await (await fetch('blob.bin')).blob();
          log += 'blob=' + blob.size + ';';
        })();
        log += 'sync;';
        function report() { console.log(log); }
    </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    // Several awaits, each one a turn: the loop has to run enough times.
    for (int frame = 0; frame < 20; ++frame) { page.tick(16); }
    (void)page.run_script("report();");
    const std::string & out = log_of(page).back();
    check(out.find("sync;") == 0, "the async function suspended at its first await: " + out);
    check(out.find("len=3,3;") != std::string::npos, "arrayBuffer and its view agree: " + out);
    check(out.find("bytes=1,2,255;") != std::string::npos, "the bytes are the file's: " + out);
    check(out.find("bytes2=3;") != std::string::npos, "bytes() too: " + out);
    check(out.find("blob=3;") != std::string::npos, "and blob(): " + out);
}

// An ABORT now has something to abort, because the request is outstanding for at
// least one turn.
void test_fetch_abort() {
    browser page{browser_options{300, 200}};
    page.assets().add("data.json", bytes_of("{}"));
    page.load_html(R"(<html><body><script>
        var log = '';
        const control = new AbortController();
        fetch('data.json', { signal: control.signal })
          .then(function () { log += 'resolved;'; },
                function (e) { log += 'rejected=' + e.name + ';'; });
        control.abort();
        function report() { console.log(log); }
    </script></body></html>)");
    check(page.script_error().empty(), "the script ran: " + page.script_error());
    for (int frame = 0; frame < 4; ++frame) { page.tick(16); }
    (void)page.run_script("report();");
    check(log_of(page).back() == "rejected=AbortError;",
          "an aborted fetch rejects: " + log_of(page).back());
}

// --- garbage collection ---------------------------------------------------
//
// Collection never ran. Not "ran rarely" - the VM had no automatic trigger at
// all, so a long-running page accumulated every object it ever made. The
// reason it could not simply be switched on is that the DOM bindings hold
// every listener, every timer callback and every element wrapper in C++
// containers the collector cannot see, so a sweep would have freed a page's
// own listeners while the page was still using them.
void test_collection_keeps_what_the_page_still_uses() {
    browser page{browser_options{300, 200}};
    page.load_html(R"(<body><div id=a>click me</div><script>
    var count = 0;
    document.getElementById('a').addEventListener('click', function () {
      count = count + 1;
      console.log('fired ' + count);
    });
    setInterval(function () { console.log('tick'); }, 1000);
    var kept = document.getElementById('a');
    </script></body>)");
    check(page.frame().has_value(), "the page renders");
    check(page.script_error().empty(), "the script ran");

    // Make a lot of garbage, then collect. Without external roots this frees
    // the listener, the interval's callback and the wrapper `kept` refers to.
    check(page.run_script("for (var i = 0; i < 20000; i = i + 1) { var junk = { n: i }; }"),
          "made some garbage");
    const std::size_t freed = page.collect_garbage();
    check(freed > 0, "and collecting freed some of it");

    // The listener still fires.
    (void)page.handle(input_event::mouse_down_at(20, 20));
    (void)page.handle(input_event::mouse_up_at(20, 20));
    check(!log_of(page).empty() && log_of(page).back() == "fired 1",
          "the listener survived the collection");

    // The interval still fires.
    check(page.tick(1100) >= 1, "the interval survived too");
    check(log_of(page).back() == "tick", "and ran its callback");

    // And the wrapper the page is holding is still the live element.
    check(page.run_script("console.log(kept.tagName);"), "reading the kept wrapper works");
    check(log_of(page).back() == "DIV", "it is still the element it was");
}

void test_collection_happens_on_its_own() {
    browser page{browser_options{300, 200}};
    page.load_html(R"(<body><p>x</p><script>
    function churn() { for (var i = 0; i < 3000; i = i + 1) { var junk = { n: i }; } }
    setInterval(churn, 16);
    </script></body>)");
    check(page.script_error().empty(), "the script ran");

    // A page that makes garbage on a timer must not grow without bound. Before
    // this, nothing was ever freed for the life of the document.
    //
    // WHAT IS MEASURED IS THE SHAPE OF THE CURVE, not a threshold on the peak.
    // This used to assert `peak > 3000` - 3,000 objects being what one call of
    // `churn` makes - and then that the heap came back down from it. That held
    // only while `collect_if_due` did NOT fire every tick: the sample is taken
    // AFTER each tick, so a collector that keeps up is never seen holding the
    // garbage, and the assertion started failing the moment the page's baseline
    // heap grew enough (the DOM interface prototypes, the CSSOM) to make every
    // tick due. A collector doing its job cannot be what makes this red.
    //
    // So: the timer really ran, and after sixty rounds of three thousand
    // objects each the heap is no bigger than it was after the first.
    std::size_t ticked = 0;
    ticked += page.tick(20);
    const std::size_t early = page.live_script_objects();
    std::size_t peak = early;
    for (int i = 0; i < 59; ++i) {
        ticked += page.tick(20);
        peak = std::max(peak, page.live_script_objects());
    }
    const std::size_t settled = page.live_script_objects();
    // The numbers are IN the message: a bare "failed" tells whoever reads it
    // nothing about which half of the claim broke.
    check(ticked >= 60, "the timer really ran (" + std::to_string(ticked) + " callbacks)");
    check(early > 100, "and the page has a heap at all (" + std::to_string(early) + ")");
    check(settled <= early + 200, "the heap did not grow over 60 rounds of 3,000 objects (" +
                                      std::to_string(early) + " -> " + std::to_string(settled) +
                                      ", peak " + std::to_string(peak) + ")");
}

// --- timers and frames ----------------------------------------------------

void test_timers() {
    browser page{browser_options{200, 200}};
    page.load_html(R"(<html><body><script>
    setTimeout(function () { console.log('late'); }, 100);
    setTimeout(function () { console.log('soon'); }, 5);
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");
    check(page.bindings().pending_timers() == 2, "two timers are armed");

    check(page.tick(1) == 0, "nothing is due after 1ms");
    check(page.tick(10) == 1, "the 5ms timer fires by 11ms");
    check(log_of(page).size() == 1 && log_of(page)[0] == "soon", "and it is the right one");
    check(page.tick(200) == 1, "the 100ms timer fires later");
    check(log_of(page).back() == "late", "in the right order");
    check(page.tick(1000) == 0, "a one-shot timer does not fire twice");
}

void test_interval_repeats_and_can_be_cleared() {
    browser page{browser_options{200, 200}};
    page.load_html(R"(<html><body><script>
    var n = 0;
    var id = setInterval(function () { n = n + 1; console.log('tick ' + n);
      if (n == 3) { clearInterval(id); } }, 10);
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");
    for (int i = 0; i < 6; ++i) { (void)page.tick(11); }
    // Three ticks then cleared. An interval that keeps firing after
    // clearInterval is the classic leak, and it only shows up over time.
    check(log_of(page).size() == 3, "the interval fired three times and then stopped");
    check(page.bindings().pending_timers() == 0, "and is no longer armed");
}

void test_request_animation_frame() {
    browser page{browser_options{200, 200}};
    page.load_html(R"(<html><body><script>
    var frames = 0;
    function loop() { frames = frames + 1; console.log('frame ' + frames);
      if (frames < 3) { requestAnimationFrame(loop); } }
    requestAnimationFrame(loop);
    </script></body></html>)");
    check(page.frame().has_value(), "the page renders");
    check(page.bindings().pending_animation_frames() == 1, "one frame callback is queued");
    for (int i = 0; i < 5; ++i) { (void)page.tick(16); }
    // A rAF callback that re-registers itself is the commonest animation
    // idiom, and running the queue in place would loop forever inside one tick.
    check(log_of(page).size() == 3, "a self-re-registering rAF runs once per tick");
    check(page.bindings().pending_animation_frames() == 0, "and stops when it stops asking");
}

} // namespace

int main() {
    test_collection_keeps_what_the_page_still_uses();
    test_collection_happens_on_its_own();
    test_timers();
    test_interval_repeats_and_can_be_cleared();
    test_request_animation_frame();
    test_fetch_is_async();
    test_fetch_await_and_bytes();
    test_fetch_abort();
    test_await_suspends_and_resumes();
    test_a_promise_made_during_a_resumption_survives();
    test_a_suspended_frame_survives_collection();
    REPORT("page_async");
}
