//===- unittests/unit/bindings_rooting.cpp - Natives survive page GC ------===//
//
// What a native holds while the page runs must be rooted, copied, or found
// again.
//
// Each case gives a native a page callback (a getter, a toString, an overridden
// DOM method) that mutates what the native is walking and allocates enough to
// make the next call boundary collect. If the native does not root, copy or
// re-find what it holds, it reads freed memory: ASan (the asan preset) reports
// it, and a plain build usually answers wrong or crashes. A correct native
// produces the expected answer below.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include "dom_probe.hpp"
#include <string>

using namespace ctbrowser;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser_test::said;

namespace {

/// Script defining churn(), which makes enough garbage to put the heap past
/// its collection threshold, so the next C++ entry into script collects.
const std::string churn = "function churn(){ var a = []; for (var i = 0; i < 200000; ++i)"
                          " a.push({i: i}); return a.length; }";

/// Runs `script` (with churn() defined) in a fresh page; returns its alerts.
std::string page(const std::string & script) {
    return said("<html><body><script>" + churn + script + "</script></body></html>");
}

/// localStorage's backing object is what the Storage natives hold by pointer;
/// it must not be reachable as a deletable `__items` property, or deleting that
/// property frees it.
void test_storage_backing_cannot_be_deleted() {
    CHECK_EQ(page("localStorage.setItem('a', '1');"
                  "delete localStorage.__items; churn(); churn();"
                  "localStorage.setItem('b', '2');"
                  "alert(localStorage.getItem('a') + localStorage.getItem('b') +"
                  " localStorage.length);"
                  "alert(Object.keys(localStorage).indexOf('__items'));"),
             "122;-1");
    // ...and a method the page kept holds it once the store itself is gone.
    CHECK_EQ(page("var get = localStorage.getItem; localStorage.setItem('k', 'v');"
                  "delete window.localStorage; delete globalThis.localStorage;"
                  "churn(); churn(); alert(get.call(null, 'k'));"),
             "v");
}

/// Object spread reads each accessor, and this getter defines more accessors
/// on the object being walked - reallocating the table under the walk. What is
/// copied is the key list as it stood (CopyDataProperties takes it once).
void test_spread_walks_a_snapshot() {
    CHECK_EQ(page("var o = {};"
                  "for (var i = 0; i < 4; ++i) Object.defineProperty(o, 'k' + i, {enumerable: true,"
                  " configurable: true, get: function() { for (var j = 0; j < 64; ++j)"
                  " Object.defineProperty(o, 'x' + Math.random(), {enumerable: true,"
                  " configurable: true, get: function() { return 0; }}); return 1; }});"
                  "alert(Object.keys({...o}).join());"),
             "k0,k1,k2,k3");
}

/// Each getter's answer sits in copy_own_properties' C++ list while the next
/// getter runs, and that getter's native call collects, so the list must be
/// rooted.
void test_spread_holds_getter_results() {
    const std::string collect = "churn(); [0].forEach(function() {});";
    CHECK_EQ(page("var o = {get a() { return {tag: 'kept'}; }, get b() { " + collect +
                  " return 2; }};"
                  "var s = {...o}; " +
                  collect + " alert(s.a.tag + s.b);"),
             "kept2");
}

/// Once the stack-depth limit has thrown its RangeError, a further re-entry
/// that is refused must still throw, not return undefined: the async-from-sync
/// iterator's step would treat that undefined as its result pair. Several depths
/// are tried, because which call hits the limit depends on how many natives the
/// step goes through.
void test_refused_reentry_is_a_throw() {
    CHECK_EQ(
        page("function nest(fn, n) { var args = [fn, null, []];"
             " for (var i = 0; i < n; ++i) args = [Reflect.apply, null, args];"
             " return function() { return Reflect.apply.apply(null, args); }; }"
             "var deep = nest(function() {}, 700);"
             "async function step() { for await (const x of [1]) {} }"
             "for (var d = 480; d < 530; ++d) try { nest(function() {"
             "  try { deep(); } catch (e) {} step().catch(function() {}); }, d)(); } catch (e) {}"
             "alert('survived');"),
        "survived");
}

/// A Float32Array store of a double past float's range is Infinity (the plain
/// cast is undefined behaviour), through the array and through a DataView.
void test_float32_store_out_of_range() {
    CHECK_EQ(page("var f = new Float32Array(2); f[0] = 1e300; f[1] = -1e300;"
                  "var v = new DataView(new ArrayBuffer(4)); v.setFloat32(0, 1e300);"
                  "alert(f[0] + ',' + f[1] + ',' + v.getFloat32(0));"),
             "Infinity,-Infinity,Infinity");
}

/// new Blob(parts): a part's `__bytes` getter grows the page's own array under
/// the walk and collects; new File(parts, name): the name's toString collects
/// while the Blob the constructor made is only in a C++ local.
void test_blob_and_file_constructors() {
    CHECK_EQ(page("var parts = ['ab', {get __bytes() { for (var i = 0; i < 256; ++i)"
                  " parts.push('zz'); churn(); return [1, 2]; }}, 'c'];"
                  "alert(new Blob(parts, {get type() { churn(); return 'text/x'; }}).size);"
                  "var f = new File(['abc'], {toString() { churn(); return 'n.txt'; }});"
                  "alert(f.name + ':' + f.size);"),
             "5;n.txt:3");
}

/// new URLSearchParams(sequence): iterable_values hands back the page's own
/// arrays, and a toString can grow the outer one and empty a pair.
void test_url_search_params_sequence() {
    CHECK_EQ(page("var init = [['a', {toString() { init.push(['q', 'r']); init[0].length = 0;"
                  " churn(); return '1'; }}], ['b', '2']];"
                  "alert(new URLSearchParams(init).toString());"),
             "a=1&b=2");
}

/// Range.createContextualFragment in an XML document reads the list its
/// querySelectorAll call returns - here the page's own array - and walks each
/// entry through calls the page can use to drop the entry and collect.
void test_contextual_fragment_holds_what_it_read() {
    CHECK_EQ(page("var doc = new DOMParser().parseFromString("
                  "'<html xmlns=\"http://www.w3.org/1999/xhtml\"><body><p/></body></html>',"
                  " 'application/xhtml+xml');"
                  "var range = doc.createRange(); range.selectNodeContents(doc.documentElement);"
                  "var list = [];"
                  "var probe = doc.createDocumentFragment();"
                  "Object.getPrototypeOf(probe).querySelectorAll = function() {"
                  "  list = [{namespaceURI: 'http://www.w3.org/1999/xhtml', n: 0,"
                  "    parentNode: {insertBefore: function() { list.length = 0; churn(); }},"
                  "    get firstChild() { return this.n++ < 2 ? {} : null; },"
                  "    remove: function() { alert('removed:' + this.n); }}];"
                  "  return list; };"
                  "range.createContextualFragment('<b>x</b>'); alert('done');"),
             "removed:3;done");
}

/// Range.cloneContents holds the clone its `cloneNode` call returned while it
/// looks up `appendChild`. A bound Reflect.get is a native callee that returns a
/// value read from a data property. Once the getter clears the property and
/// collects, only the root the invoke path keeps for the callee's result holds
/// it.
void test_range_holds_a_native_callees_result() {
    CHECK_EQ(page("var t = document.createTextNode('hello'); document.body.appendChild(t);"
                  "var store = {c: {tag: 'ok'}};"
                  "t.cloneNode = Reflect.get.bind(null, store, 'c');"
                  "document.createDocumentFragment = function() { return {"
                  "  get appendChild() { store.c = null; churn(); churn();"
                  "    return function(x) { alert('got:' + x.tag + ':' + x.data); }; } }; };"
                  "var r = document.createRange(); r.setStart(t, 1); r.setEnd(t, 3);"
                  "r.cloneContents(); alert('done');"),
             "got:ok:el;done");
}

/// A module that dynamic-imports while it runs: the loader inserts into the
/// registry the running module's record lives in, and the record must stay
/// where run_module left it.
void test_dynamic_import_inside_a_running_module() {
    browser p{browser_options{200, 100}};
    for (int i = 0; i < 64; ++i) {
        p.assets().add("m" + std::to_string(i) + ".js",
                       bytes_of("export const v = " + std::to_string(i) + ";"));
    }
    p.load_html("<html><body><script type=\"module\">"
                "for (let i = 0; i < 64; ++i) import('./m' + i + '.js');"
                "export const after = 1;"
                "alert('ran');"
                "</script></body></html>");
    std::string out;
    for (const std::string & one : p.alerts()) { out += one + ";"; }
    CHECK_EQ(out, std::string{"ran;"});
    CHECK_EQ(p.script_error(), std::string{});
}

} // namespace

int main() {
    test_storage_backing_cannot_be_deleted();
    test_spread_walks_a_snapshot();
    test_spread_holds_getter_results();
    test_refused_reentry_is_a_throw();
    test_float32_store_out_of_range();
    test_blob_and_file_constructors();
    test_url_search_params_sequence();
    test_contextual_fragment_holds_what_it_read();
    test_range_holds_a_native_callees_result();
    test_dynamic_import_inside_a_running_module();
    REPORT("bindings_rooting");
}
