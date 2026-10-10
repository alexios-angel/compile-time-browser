//===- ctbrowser/unittests/js/immutable_bindings.cpp - Immutable bindings -===//
//
// Bindings that cannot be assigned (ECMA-262 9.1.1.1.5 SetMutableBinding
// step 5). `const`, `using`, an import and a class's own name inside its body
// refuse every assignment with a TypeError; a named function expression's own
// name refuses it in strict code and ignores it in sloppy code. The compiler
// decides which is which in compiler_impl::binding_immutability.
//
//===----------------------------------------------------------------------===//

#include "js_expect.hpp"

#include <string>

namespace {
/// Builds an expression answering what `body` throws, by constructor name, or
/// "none".
std::string thrown(std::string_view body) {
    return "(function(){ try { " + std::string{body} +
           "; return 'none'; } catch (e) { return e.constructor.name; } })()";
}
} // namespace

int main() {
    // const, in every place a write can come from.
    js_expect(thrown("const c = 1; c = 2"), "TypeError");
    js_expect(thrown("const c = 1; c += 2"), "TypeError");
    js_expect(thrown("const c = 1; c++"), "TypeError");
    js_expect(thrown("const c = 1; (() => { c = 2; })()"), "TypeError");
    js_expect(thrown("const c = 1; [c] = [2]"), "TypeError");
    js_expect(thrown("const c = 1; ({c} = {c: 2})"), "TypeError");
    js_expect(thrown("const {c} = {c: 1}; c = 2"), "TypeError");
    js_expect(thrown("for (const i = 0; i < 1; i++) {}"), "TypeError");
    js_expect(thrown("for (const k in {a: 1}) { k = 'b'; }"), "TypeError");
    js_expect(thrown("for (const [v] of [[1]]) { v = 2; }"), "TypeError");
    js_expect(thrown("{ const b = 1; { b = 2; } }"), "TypeError");
    // The right side runs first; a logical assignment that does not assign
    // does not throw; the declaration itself and a shadowing `let` are writes.
    js_expect("(function(){ const c = 1; var ran = 0; try { c = ran = 5; } catch (e) {}"
              " return ran; })()",
              "5");
    js_expect(thrown("const c = 1; c ||= 2"), "none");
    js_expect("(function(){ const c = 1; { let c = 2; c = 3; return c; } })()", "3");
    js_expect("(function(){ for (const x of [1, 2]) { var last = x; } return last; })()", "2");
    // A named function expression's own name: ignored in sloppy code, the
    // TypeError in strict code, shadowed by any declaration in the body.
    js_expect("(function me() { me = 1; return typeof me; })()", "function");
    js_expect("(function me() { (() => { me = 1; })(); return typeof me; })()", "function");
    js_expect("(function me() { eval('me = 1'); return typeof me; })()", "function");
    js_expect(thrown("(function me() { 'use strict'; me = 1; })()"), "TypeError");
    js_expect(thrown("(function me() { 'use strict'; eval('me = 1'); })()"), "TypeError");
    js_expect("(function me() { var me = 2; me = 3; return me; })()", "3");
    js_expect("(function me() { function me() {} return typeof me; })()", "function");
    js_expect("(function me() { var me; return typeof me; })()", "undefined");
    // A class's own name inside its body - a declaration's and an expression's.
    js_expect(thrown("class C { m() { C = 1; } } new C().m()"), "TypeError");
    js_expect(thrown("(new (class C { get x() { C = 1; } })).x"), "TypeError");
    js_expect(thrown("class C { constructor() { C = 1; } } new C()"), "TypeError");
    js_expect(thrown("class C {} C = 1"), "none");
    // A direct eval sees the caller's const as a const.
    js_expect(thrown("const c = 1; eval('c = 2')"), "TypeError");
    js_expect(thrown("const c = 1; (function(){ eval('c = 2'); })()"), "TypeError");
    // A named class expression's own binding is in its dead zone while its
    // heritage runs; a method reads the class.
    js_expect("(function(){ try { (class x extends x {}); } catch (e) { return e.name; } })()",
              "ReferenceError");
    js_expect("(function(){ var C = class K { m() { return K; } }; return new C().m() === C; })()",
              "true");
    REPORT("immutable_bindings");
}
