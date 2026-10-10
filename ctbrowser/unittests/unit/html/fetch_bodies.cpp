//===- ctbrowser/unittests/unit/html/fetch_bodies.cpp - Fetch objects -----===//
//
// Fetch's Headers, Request and Response (Fetch 5.2-5.5) as a page builds them
// itself rather than getting one from fetch(): the header list's case-blind
// names, its sort-and-combine iteration and immutable guard; url/
// urlencoded-parser's `new Request(..., {body}).formData()` and `new
// Response(body, {headers}).formData()`; Babylon's `new Response(arrayBuffer,
// {status, statusText, headers: new Headers()})`; and a body read only once.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser.hpp>

#include "check.hpp"

#include <string>
#include <vector>

namespace {

void test_fetch_objects_a_page_builds() {
    ctbrowser::browser page{ctbrowser::browser_options{300, 200}};
    page.assets().add("data.json",
                      std::vector<std::byte>{std::byte{'{'}, std::byte{'"'}, std::byte{'n'},
                                             std::byte{'"'}, std::byte{':'}, std::byte{'7'},
                                             std::byte{'}'}});
    page.load_html(R"(<html><body><script>
        var log = [];
        var h = new Headers({'X-B': ' two ', 'x-a': 'one'});
        h.append('X-B', 'three');
        log.push(h.get('x-b'), h.has('X-A'), [...h.keys()].join('+'));
        h.set('x-b', '4'); log.push(h.get('X-B'));
        try { h.append('bad name', 'x'); } catch (e) { log.push(e.name); }
        (async function () {
          var fd = await new Request('about:blank', {method: 'LADIDA', body: 'a=b&c=%41',
              headers: {'Content-Type': 'application/x-www-form-urlencoded;charset=x'}}).formData();
          log.push([...fd].map(function (p) { return p.join('='); }).join('&'));
          var headers = new Headers(); headers.set('Content-Type', 'application/octet-stream');
          var bytes = new Uint8Array([1, 2, 255]);
          var r = new Response(bytes, {status: 201, statusText: 'Made', headers: headers});
          log.push(r.status, r.ok, r.statusText, r.headers.get('content-type'), r.bodyUsed);
          var buf = await r.arrayBuffer();
          log.push(buf.byteLength, r.bodyUsed);
          try { await r.text(); } catch (e) { log.push(e.name); }
          log.push(await new Response('\u00e9').text());
          try { new Response('x', {status: 99}); } catch (e) { log.push(e.name); }
          try { new Request('about:blank', {body: 'x'}); } catch (e) { log.push(e.name); }
          var fetched = await fetch('data.json');
          try { fetched.headers.set('x', 'y'); } catch (e) { log.push(e.name); }
          log.push((await fetched.json()).n);
        })();
        function report() { console.log(log.join(',')); }
    </script></body></html>)");
    CHECK(page.script_error().empty());
    for (int frame = 0; frame < 20; ++frame) { page.tick(16); }
    (void)page.run_script("report();");
    const std::vector<std::string> & logged = page.bindings().console_output();
    CHECK(!logged.empty());
    if (logged.empty()) { return; }
    CHECK_EQ(logged.back(), std::string{"two, three,true,x-a+x-b,4,TypeError,a=b&c=A,201,true,Made,"
                                        "application/octet-stream,false,3,true,TypeError,\u00e9,"
                                        "RangeError,TypeError,TypeError,7"});
}

} // namespace

int main() {
    test_fetch_objects_a_page_builds();
    REPORT("html_fetch_bodies");
}
