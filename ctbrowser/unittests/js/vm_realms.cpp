// REALMS: a second global environment in one context (ECMA-262 9.3). A frame
// that runs its own scripts is one - the page calls into it through its
// window, and the frame's functions must go on reading the FRAME's globals,
// whoever calls them (10.2.1.1 PrepareForOrdinaryCall step 6). Asserted here
// on a bare VM, which is the shape both embeddings build on: the second
// realm's table starts as the template (the builtins, none of the first
// realm's `var`s), its global object is a proxy over its own table, and the
// first realm reaches it only through that object.

#include <ctbrowser/script/script.hpp>

#include "check.hpp"
#include <cstdio>
#include <string>
#include <string_view>

using namespace ctbrowser::script;

namespace {

struct two_realms {
    context cx;
    realm_record * other = nullptr;

    two_realms() {
        install_builtins(cx);
        cx.set_realm_template(cx.globals());
        other = &cx.create_realm();
        other->global_this = cx.make_global_object(*other);
        other->globals[std::string{"globalThis"}] = other->global_this;
        cx.define_global("other", other->global_this);
    }

    // A classic script in the SECOND realm, as a frame's <script> runs.
    void run_there(std::string_view source) {
        const context::realm_scope in_other{cx, *other};
        const run_result r = cx.run(cx.own_program(compiler::compile(std::string{source})));
        if (!r.ok) {
            std::printf("FAIL     realm script: %s\n", r.error.c_str());
            ++ctbrowser_test_failures;
        }
    }

    // A classic script in the first realm, answering what `result` holds after
    // the turn's microtasks.
    std::string run_here(std::string_view source) {
        const run_result r = cx.run(cx.own_program(compiler::compile(std::string{source})));
        if (!r.ok) { return "<error: " + r.error + ">"; }
        return cx.to_string(cx.global("result"));
    }

    void expect(std::string_view source, std::string_view want) {
        const std::string got = run_here(source);
        if (got != want) {
            std::printf("FAIL     %.80s => %s (want %s)\n", std::string{source}.c_str(),
                        got.c_str(), std::string{want}.c_str());
            ++ctbrowser_test_failures;
        }
    }
};

void test_separate_bindings() {
    two_realms r;
    r.run_there("var x = 'frame'; function f() { return x; }"
                "function g() { return this; }"
                "function make() { return function () { return x; }; }"
                "function* gen() { yield x; }"
                "async function later() { await null; return x; }"
                "function set(v) { x = v; }");
    // The frame's `var` is not the page's, and the page's is not the frame's.
    r.expect("var result = typeof f + '|' + typeof x;", "undefined|undefined");
    r.expect("var x = 'page'; var result = other.f() + '|' + x;", "frame|page");
    // A write through the frame's window is a write to its binding table,
    // which the frame's own functions then read.
    r.expect("other.x = 'written'; var result = other.f();", "written");
    r.expect("other.set('from-frame'); var result = other.x + '|' + x;", "from-frame|page");
    // A closure made in the frame keeps the frame's realm after it escapes.
    r.expect("var result = other.make()();", "from-frame");
    // A sloppy function called with no receiver gets ITS realm's global
    // object (10.2.1.2 OrdinaryCallBindThis step 5).
    r.expect("var h = other.g; var result = (h() === other) + '|' + (h() === globalThis);",
             "true|false");
    // A generator and an async function resume in their own realm, whoever
    // resumes them.
    r.expect("var result = other.gen().next().value;", "from-frame");
    r.expect("var result = 'pending'; other.later().then(v => { result = v; });", "from-frame");
    // A name nowhere in the frame is a ReferenceError there, not the page's.
    r.run_there("function missing() { try { return page_only; } catch (e) { return e.name; } }");
    r.expect("var page_only = 1; var result = other.missing();", "ReferenceError");
}

void test_shared_intrinsics() {
    two_realms r;
    r.run_there("var arr = [1, 2, 3];");
    // ponytail: ONE set of intrinsics - `other.Array === Array` - where a
    // browser has two. What a frame needs first is its own bindings; distinct
    // prototypes are the upgrade when a cross-realm identity test asks.
    r.expect("var result = (other.Array === Array) + '|' + Array.isArray(other.arr);", "true|true");
    // The template is what the realm was made with, not the page's later vars.
    r.expect("var late = 1; var result = typeof other.late;", "undefined");
}

void test_realms_survive_collection() {
    two_realms r;
    r.run_there("var kept = { v: 42 }; function read() { return kept.v; }");
    (void)r.cx.collect();
    r.expect("var result = other.read();", "42");
}

} // namespace

int main() {
    test_separate_bindings();
    test_shared_intrinsics();
    test_realms_survive_collection();
    REPORT("vm_realms");
}
