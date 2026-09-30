// What a native holds while the page runs: rooted, copied, or found again.
//
// Each case gives a native a page callback - a getter, a toString, an
// overridden DOM method - that mutates what the native is walking and
// allocates enough to make the next call boundary collect. Without the fix
// the native reads freed memory; ASan (the asan preset) says so, and a plain
// build usually answers wrong or dies. With it, the answer below.

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

// Enough garbage to put the heap past its collection threshold, so the next
// C++ entry into script collects.
const std::string churn = "function churn(){ var a = []; for (var i = 0; i < 200000; ++i)"
                          " a.push({i: i}); return a.length; }";

std::string page(const std::string & script) {
    return said("<html><body><script>" + churn + script + "</script></body></html>");
}

// localStorage's backing object was a deletable `__items` property and the
// only heap reference to what six natives hold by pointer.
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

// new Blob(parts): a part's `__bytes` getter grows the page's own array under
// the walk and collects; new File(parts, name): the name's toString collects
// while the Blob the constructor made is only in a C++ local.
void test_blob_and_file_constructors() {
    CHECK_EQ(page("var parts = ['ab', {get __bytes() { for (var i = 0; i < 256; ++i)"
                  " parts.push('zz'); churn(); return [1, 2]; }}, 'c'];"
                  "alert(new Blob(parts, {get type() { churn(); return 'text/x'; }}).size);"
                  "var f = new File(['abc'], {toString() { churn(); return 'n.txt'; }});"
                  "alert(f.name + ':' + f.size);"),
             "5;n.txt:3");
}

// new URLSearchParams(sequence): iterable_values hands back the page's own
// arrays, and a toString can grow the outer one and empty a pair.
void test_url_search_params_sequence() {
    CHECK_EQ(page("var init = [['a', {toString() { init.push(['q', 'r']); init[0].length = 0;"
                  " churn(); return '1'; }}], ['b', '2']];"
                  "alert(new URLSearchParams(init).toString());"),
             "a=1&b=2");
}

// Range.createContextualFragment in an XML document reads the list its
// querySelectorAll call returns - here the page's own array - and walks each
// entry through calls the page can use to drop the entry and collect.
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

// Range.cloneContents holds the clone its `cloneNode` call returned while it
// looks up `appendChild`. A bound Reflect.get is a NATIVE callee answering a
// value read off a data property - nothing but the invoke pin holds it once
// the getter clears the property and collects.
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

} // namespace

int main() {
    test_storage_backing_cannot_be_deleted();
    test_blob_and_file_constructors();
    test_url_search_params_sequence();
    test_contextual_fragment_holds_what_it_read();
    test_range_holds_a_native_callees_result();
    REPORT("bindings_rooting");
}
