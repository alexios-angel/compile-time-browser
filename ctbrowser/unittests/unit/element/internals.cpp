//===- ctbrowser/unittests/unit/element/internals.cpp - ElementInternals --===//
//
// ElementInternals: attachInternals() and what it hands back (HTML 4.13.7).
//
// Regression tests for lib/Shell/bindings/element_internals.cpp, written like
// unittests/unit/custom_elements.cpp: the page reports through alert().
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
#include <string_view>

using namespace ctbrowser;
using ctbrowser::shell::browser;
using ctbrowser::shell::browser_options;
using ctbrowser_test::said;

namespace {

/// attachInternals succeeds once per element, only for a defined autonomous
/// custom element whose definition did not disable internals, and only from
/// the constructor on (HTMLElement-attachInternals.html,
/// element-internals-shadowroot.html).
void test_attach_internals_refusals() {
    CHECK_EQ(said("<html><body><x-later id=l></x-later><script>"
                  "function threw(f){ try { f(); return 'ok'; } catch (e) { return e.name; } }"
                  "class A extends HTMLElement {}"
                  "customElements.define('x-a', A);"
                  "var a = new A();"
                  "var i = a.attachInternals();"
                  "alert(i instanceof ElementInternals);"
                  "alert(threw(function(){ a.attachInternals(); }));"
                  "alert(threw(function(){ document.createElement('div').attachInternals(); }));"
                  "alert(threw(function(){ document.createElement('x-none').attachInternals(); }));"
                  "class D extends HTMLDivElement {}"
                  "customElements.define('x-d', D, {extends: 'div'});"
                  "alert(threw(function(){ new D().attachInternals(); }));"
                  "class N extends HTMLElement { static get disabledFeatures() { return "
                  "['internals']; } }"
                  "customElements.define('x-n', N);"
                  "alert(threw(function(){ new N().attachInternals(); }));"
                  "var l = document.getElementById('l');"
                  "alert(threw(function(){ l.attachInternals(); }));"
                  "customElements.define('x-later', class extends HTMLElement {"
                  "  constructor() { super(); alert('ctor:' + threw(() => this.attachInternals()));"
                  "    alert(this.attachInternals === undefined ? 'none' : 'has'); } });"
                  "alert(i.shadowRoot);"
                  "var s = a.attachShadow({mode: 'closed'});"
                  "alert(i.shadowRoot === s);"
                  "</script></body></html>"),
             "true;NotSupportedError;NotSupportedError;NotSupportedError;NotSupportedError;"
             "NotSupportedError;NotSupportedError;ctor:ok;has;null;true");
}

/// The form-associated members: they throw for a definition without
/// `formAssociated`. With it: the validity flags, the validation message,
/// willValidate being false while disabled, checkValidity's `invalid` event,
/// the form owner, and the formAssociatedCallback and formDisabledCallback
/// reactions.
void test_form_associated_internals() {
    CHECK_EQ(said("<html><body><form id=f></form><script>"
                  "function threw(f){ try { f(); return 'ok'; } catch (e) { return e.name; } }"
                  "var log = [];"
                  "class P extends HTMLElement {}"
                  "customElements.define('x-p', P);"
                  "var pi = new P().attachInternals();"
                  "alert(threw(function(){ pi.willValidate; }) + ':' +"
                  "      threw(function(){ pi.setFormValue('x'); }));"
                  "class F extends HTMLElement {"
                  "  static get formAssociated() { return true; }"
                  "  constructor() { super(); this.i = this.attachInternals(); }"
                  "  formAssociatedCallback(form) { log.push('assoc:' + (form && form.id)); }"
                  "  formDisabledCallback(d) { log.push('disabled:' + d); }"
                  "}"
                  "customElements.define('x-f', F);"
                  "var f = new F();"
                  "alert(f.i.willValidate + ':' + f.i.validity.valid + ':' + f.i.checkValidity());"
                  "f.i.setValidity({valueMissing: true}, 'need it');"
                  "var count = 0; f.addEventListener('invalid', function (e) { count++; });"
                  "alert(f.i.validity.valueMissing + ':' + f.i.validity.valid + ':' +"
                  "      f.i.validationMessage + ':' + f.i.checkValidity() + ':' + count);"
                  "alert(threw(function(){ f.i.setValidity({badInput: true}); }));"
                  "f.i.setValidity({});"
                  "alert(f.i.validity.valid + ':' + JSON.stringify(f.i.validationMessage));"
                  "f.setAttribute('disabled', '');"
                  "alert(f.i.willValidate);"
                  "f.removeAttribute('disabled');"
                  "document.getElementById('f').appendChild(f);"
                  "alert(f.i.form === document.getElementById('f'));"
                  "f.remove();"
                  "alert(log.join(','));"
                  "f.i.states.add('--on');"
                  "alert(f.i.states.has('--on') + ':' + f.i.states.size + ':' +"
                  "      (f.i.states instanceof CustomStateSet) + ':' + f.i.role);"
                  "f.i.role = 'button';"
                  "alert(f.i.role + ':' + ('ariaLabel' in f.i));"
                  "</script></body></html>"),
             "NotSupportedError:NotSupportedError;true:true:true;true:false:need it:false:1;"
             "TypeError;true:\"\";false;true;disabled:true,disabled:false,assoc:f,assoc:null;"
             "true:1:true:null;button:true");
}

} // namespace

int main() {
    test_attach_internals_refusals();
    test_form_associated_internals();
    REPORT("element_internals");
}
