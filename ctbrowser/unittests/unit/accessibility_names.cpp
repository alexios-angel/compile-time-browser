// THE COMPUTED ROLE AND THE ACCESSIBLE NAME - what testdriver's
// get_computed_role and get_computed_label read through ctdrive: the role
// attribute, then ElementInternals' default, then HTML-AAM's implicit role;
// the name by Accessible Name 1.2 (aria-labelledby, aria-label, <label>, alt,
// the subtree). Each case names the WPT file it stands in for.

#include <ctbrowser/core/core.hpp>
#include <ctbrowser/dom/dom.hpp>
#include <ctbrowser/script/script.hpp>
#include <ctbrowser/shell/shell.hpp>
#include <ctbrowser/style/style.hpp>

#include "check.hpp"

#include <string>

using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;

namespace {

// The slot the driver would fill, and the answer for the element `expression`
// names: "role|label", or <none> when the bindings found no element.
[[nodiscard]] std::string ask(browser & page, const std::string & expression) {
    if (!page.run_script("window.__ctdrive_asked = function () { return " + expression + "; };")) {
        return "<did not run: " + page.script_error() + ">";
    }
    const auto role = page.bindings().computed_accessibility("__ctdrive_asked", 1, false);
    const auto label = page.bindings().computed_accessibility("__ctdrive_asked", 1, true);
    if (!role || !label) { return "<none>"; }
    return *role + "|" + *label;
}

void test_roles_and_names() {
    browser page{browser_options{400, 300}};
    page.load_html(R"(<!doctype html><body>
        <button id=b>Save <span>now</span></button>
        <label for=i>Name</label><input id=i>
        <label>Mail <input id=m value=typed></label>
        <img id=g alt="Logo"><img id=d alt="">
        <span id=l1>First</span><span id=l2>Second</span>
        <div id=r role="bogus  tab">Tab text</div>
        <input id=lb aria-labelledby="l1 l2" aria-label="ignored">
        <h2 id=h>Title <em>here</em></h2>
        <p id=hidden style="display: none">gone</p>
        <input type=submit id=s>
        <x-widget id=x></x-widget>
        <script>
        customElements.define('x-widget', class extends HTMLElement {
            constructor() {
                super();
                const internals = this.attachInternals();
                internals.role = 'img';
                internals.ariaLabel = 'Test label';
            }
        });
        </script></body>)");
    page.frame();
    CHECK_EQ(page.script_error(), std::string{});

    // ElementInternals-role.html: the internals' role and ariaLabel, the
    // synonym folded.
    CHECK_EQ(ask(page, "document.getElementById('x')"), std::string{"image|Test label"});
    CHECK_EQ(ask(page, "document.getElementById('b')"), std::string{"button|Save now"});
    CHECK_EQ(ask(page, "document.getElementById('i')"), std::string{"textbox|Name"});
    // A label holding its control names it with its own text only.
    CHECK_EQ(ask(page, "document.getElementById('m')"), std::string{"textbox|Mail"});
    CHECK_EQ(ask(page, "document.getElementById('g')"), std::string{"image|Logo"});
    CHECK_EQ(ask(page, "document.getElementById('d')"), std::string{"none|"});
    CHECK_EQ(ask(page, "document.getElementById('r')"), std::string{"tab|Tab text"});
    // aria-element-reflection-labelledby.html: labelledby before aria-label,
    // the parts joined by a space.
    CHECK_EQ(ask(page, "document.getElementById('lb')"), std::string{"textbox|First Second"});
    CHECK_EQ(ask(page, "document.getElementById('h')"), std::string{"heading|Title here"});
    CHECK_EQ(ask(page, "document.getElementById('hidden')"), std::string{"|"});
    CHECK_EQ(ask(page, "document.getElementById('s')"), std::string{"button|Submit"});
    // Detached: nothing to say. Not an element at all: no answer.
    CHECK_EQ(ask(page, "document.createElement('button')"), std::string{"|"});
    CHECK_EQ(ask(page, "42"), std::string{"<none>"});

    (void)page.run_script("document.getElementById('lb').ariaLabelledByElements = "
                          "[document.getElementById('l2')];");
    CHECK_EQ(ask(page, "document.getElementById('lb')"), std::string{"textbox|Second"});
}

} // namespace

int main() {
    test_roles_and_names();
    REPORT("accessibility_names");
}
