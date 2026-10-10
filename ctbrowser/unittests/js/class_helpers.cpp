//===- ctbrowser/unittests/js/class_helpers.cpp - Engine class helpers ----===//
//
// The compiler spells part of a class definition as calls of engine helpers
// (`%class_defined`, `%class_heritage`, `%init_fields`, `%bind_this`,
// `%super_get`, `%super_set`; see builtins.hpp and vm/objects.hpp). They live
// in realm_record::internals, which no script write reaches, so a page that
// assigns `__ctbrowser_class_defined` (their old global name) or writes
// `globalThis['%class_defined']` makes an ordinary global and changes nothing
// about its classes. Every answer here is node's, except the ShadowRealm one,
// which node does not ship; shadow_realm.cpp covers that realm's semantics.
//
//===----------------------------------------------------------------------===//

#include "js_expect.hpp"

int main() {
    // The two probes: a replacement through an assignment and through a
    // computed key on the global object never runs when a class is defined.
    js_expect("(function(){ __ctbrowser_class_defined = function (k) { k.prototype.hit = 1; };"
              " class A { m() {} } return (new A()).hit; })()",
              "undefined");
    js_expect("(function(){ globalThis['__ctbrowser_' + 'class_defined'] ="
              " function (k) { k.prototype.hit = 1; }; class A { m() {} } return (new A()).hit;"
              " })()",
              "undefined");
    // The page's assignment is an ordinary global, readable back.
    js_expect("(function(){ __ctbrowser_class_defined = 7; return __ctbrowser_class_defined;"
              " })()",
              "7");
    // Before any assignment the old names are not globals at all.
    js_expect("typeof __ctbrowser_class_defined", "undefined");
    js_expect("typeof __ctbrowser_super_get", "undefined");
    js_expect("'__ctbrowser_class_defined' in globalThis", "false");
    // Every helper's key written through the global object leaves classes as
    // they were: methods stay non-enumerable, `extends`, `super()`,
    // `super.m()`, `super.x = v` and field initialisers still work.
    js_expect("(function(){ for (const n of ['class_defined', 'class_heritage', 'init_fields',"
              " 'bind_this', 'super_get', 'super_set']) {"
              " globalThis['%' + n] = function () { throw new Error(n); };"
              " globalThis['__ctbrowser_' + n] = function () { throw new Error(n); }; }"
              " class B { constructor(v) { this.v = v; } get() { return this.v; } set w(x) {"
              " this.v = x; } }"
              " class D extends B { f = 2; constructor(v) { super(v); }"
              " get() { return super.get() + this.f; } put(x) { super.w = x; return this.v; } }"
              " const d = new D(1);"
              " return d.get() + ':' + d.put(5) + ':' + Object.keys(B.prototype).length; })()",
              "3:5:0");
    // A ShadowRealm uses its own realm's helpers.
    js_expect("(function(){ globalThis['%class_defined'] = 1; const r = new ShadowRealm();"
              " return r.evaluate('class A { m() { return 4; } } new A().m()'); })()",
              "4");
    REPORT("class_helpers");
}
