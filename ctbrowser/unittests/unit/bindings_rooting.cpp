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
    test_dynamic_import_inside_a_running_module();
    REPORT("bindings_rooting");
}
