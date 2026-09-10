// THE `blocking` ATTRIBUTE, HTML 2.5.7, and what "render-blocking" means here.
//
// `[SameObject, PutForwards=value] readonly attribute DOMTokenList blocking` on
// `<link>`, `<script>` and `<style>`, with `render` as its one supported token.
// It is the same token-list object `classList` is, over another attribute -
// see install_element_views in lib/Shell/bindings/element/views.cpp - so the
// first two cases pin down the shape `html/dom/render-blocking/blocking-idl-
// attr.html` asks for: `supports`, the forwarding assignment, and that the
// list follows the attribute in both directions.
//
// THE LAST CASE IS THE ORDERING, AND IT IS THE ENGINE'S ONLY ORDERING. The
// corpus observes render-blocking through time - a paint entry must not
// precede the sheet's load - and this engine loads a stylesheet synchronously
// from the asset registry while the page is built, so there is no moment at
// which a frame could run before the sheet applied. That is what the case
// proves: the first animation frame and the window's load event both see the
// blocking sheet's colour. A rung that makes stylesheet loading asynchronous
// is a rung that has to keep this case green by holding the frame back.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/layout/layout.hpp>
#include <ctbrowser/paint/paint.hpp>
#include <ctbrowser/raster/raster.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"
#include "cssom_probe.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser_test::logged;

namespace {

[[nodiscard]] std::vector<std::byte> bytes_of(std::string_view text) {
    std::vector<std::byte> out;
    out.reserve(text.size());
    for (const char c : text) { out.push_back(static_cast<std::byte>(c)); }
    return out;
}

void test_blocking_is_a_token_list_with_one_supported_token() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><body><script>
        const out = [];
        for (const tag of ['link', 'script', 'style']) {
            const el = document.createElement(tag);
            out.push(tag + ':' + el.blocking.supports('render') + ',' +
                     el.blocking.supports('RENDER') + ',' + el.blocking.supports('asdf') + ',' +
                     (el.blocking === el.blocking) + ',' + (el.blocking instanceof DOMTokenList));
        }
        console.log('supports=' + out.join(';'));
        console.log('div=' + document.createElement('div').blocking);
        let caught = '';
        try { document.body.classList.supports('x'); } catch (e) { caught = e.name; }
        console.log('class=' + caught);
    </script></body></html>)");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "supports="),
             std::string{"supports=link:true,true,false,true,true;"
                         "script:true,true,false,true,true;style:true,true,false,true,true"});
    // Not on every element: `blocking` belongs to three interfaces.
    CHECK_EQ(logged(page, "div="), std::string{"div=undefined"});
    // `class` defines no supported tokens, and DOM says that is a TypeError.
    CHECK_EQ(logged(page, "class="), std::string{"class=TypeError"});
}

void test_assignment_forwards_to_value_and_the_list_follows_the_attribute() {
    browser page{browser_options{400, 200}};
    page.load_html(R"(<html><head><link id=s rel=stylesheet blocking="render"></head><body><script>
        const link = document.createElement('link');
        link.blocking = 'asdf';
        console.log('set=' + link.blocking.value + '|' + link.getAttribute('blocking') + '|' +
                    link.blocking.length + '|' + link.blocking);
        const s = document.getElementById('s');
        const before = s.blocking.contains('render') + ',' + s.blocking.length;
        // What remove-attr-*-keeps-blocking.html does to the parser's element.
        s.blocking = '';
        const after = s.blocking.contains('render') + ',' + s.blocking.length + ',' +
                      s.hasAttribute('blocking') + ',"' + s.getAttribute('blocking') + '"';
        s.blocking.add('render');
        console.log('parsed=' + before + '|' + after + '|' + s.getAttribute('blocking'));
    </script></body></html>)");
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "set="), std::string{"set=asdf|asdf|1|asdf"});
    CHECK_EQ(logged(page, "parsed="), std::string{"parsed=true,1|false,0,true,\"\"|render"});
}

void test_a_blocking_stylesheet_has_applied_before_the_first_frame() {
    browser page{browser_options{400, 200}};
    page.assets().add("target-red.css", bytes_of(".target { color: rgb(255, 0, 0); }"));
    page.load_html(R"(<html><head>
        <link rel=stylesheet blocking=render href=target-red.css>
        <script>
        const colour = () => getComputedStyle(document.querySelector('.target')).color;
        requestAnimationFrame(() => console.log('frame=' + colour()));
        window.addEventListener('load', () => console.log('load=' + colour()));
        </script></head><body><div class=target>This should be red</div></body></html>)");
    for (int i = 0; i < 4; ++i) { (void)page.tick(16.0); }
    CHECK(page.script_error().empty());
    CHECK_EQ(logged(page, "frame="), std::string{"frame=rgb(255, 0, 0)"});
    CHECK_EQ(logged(page, "load="), std::string{"load=rgb(255, 0, 0)"});
}

} // namespace

int main() {
    test_blocking_is_a_token_list_with_one_supported_token();
    test_assignment_forwards_to_value_and_the_list_follows_the_attribute();
    test_a_blocking_stylesheet_has_applied_before_the_first_frame();
    REPORT("render_blocking");
}
