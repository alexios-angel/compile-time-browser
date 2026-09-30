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

// Object spread reads each accessor, and this getter defines more accessors
// on the object being walked - reallocating the table under the walk. What is
// copied is the key list as it stood (CopyDataProperties takes it once).
void test_spread_walks_a_snapshot() {
    CHECK_EQ(page("var o = {};"
                  "for (var i = 0; i < 4; ++i) Object.defineProperty(o, 'k' + i, {enumerable: true,"
                  " configurable: true, get: function() { for (var j = 0; j < 64; ++j)"
                  " Object.defineProperty(o, 'x' + Math.random(), {enumerable: true,"
                  " configurable: true, get: function() { return 0; }}); return 1; }});"
                  "alert(Object.keys({...o}).join());"),
             "k0,k1,k2,k3");
}

// A re-entry refused AFTER its episode's one RangeError returned undefined with
// nothing thrown, and the async-from-sync step cast that undefined to its pair
// array. Each depth is tried, since which one lands on the refusal depends on
// how many natives the step itself goes through.
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

// A Float32Array store of a double past float's range is Infinity (the plain
// cast is undefined behaviour), through the array and through a DataView.
void test_float32_store_out_of_range() {
    CHECK_EQ(page("var f = new Float32Array(2); f[0] = 1e300; f[1] = -1e300;"
                  "var v = new DataView(new ArrayBuffer(4)); v.setFloat32(0, 1e300);"
                  "alert(f[0] + ',' + f[1] + ',' + v.getFloat32(0));"),
             "Infinity,-Infinity,Infinity");
}

// A module that dynamic-imports while it runs: the loader inserts into the
// registry the running module's record lives in, and the record must stay
// where run_module left it.
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
    test_spread_walks_a_snapshot();
    test_refused_reentry_is_a_throw();
    test_float32_store_out_of_range();
    test_dynamic_import_inside_a_running_module();
    REPORT("bindings_rooting");
}
