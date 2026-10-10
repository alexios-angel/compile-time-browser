//===- ctbrowser/unittests/js/structured_clone.cpp - structuredClone ------===//
//
// structuredClone with a transfer list, and of a platform object (HTML 2.7).
//
// A transferred ArrayBuffer arrives with its bytes and leaves its original
// detached; a buffer listed twice, a detached one and anything that is not an
// ArrayBuffer throw DataCloneError before anything is detached. A view over a
// buffer shrunk below it throws DataCloneError too. An instance of an
// interface the host installed (a builtin global that is not an intrinsic) is
// copied onto the same interface prototype when the host marked it
// serializable, and refused otherwise.
//
//===----------------------------------------------------------------------===//

#include "js_expect.hpp"

namespace {

using namespace ctbrowser::script;

/// Runs `source` in a context whose host installed a `Widget` interface after
/// the intrinsics, marked serializable or not, and returns the result as text.
std::string with_widget(std::string_view source, bool serializable) {
    context cx;
    install_builtins(cx);
    const value prototype = cx.make_object();
    auto * ctor = cx.allocate<native_object>("Widget", [prototype](context & c, std::span<value>) {
        const value made = c.make_object();
        auto * object = static_cast<object_object *>(made.as_heap());
        object->prototype = prototype;
        object->define(CTBROWSER_PRIVATE_KEY("WidgetState"), c.string("inner"), attr_none);
        object->set("size", value::number(3));
        return made;
    });
    ctor->set("prototype", prototype);
    cx.define_builtin_global("Widget", value::object(ctor));
    if (serializable) {
        cx.set_serializable_interface("Widget",
                                      {std::string{CTBROWSER_PRIVATE_KEY("WidgetState")}, "size"});
    }
    cx.set_gc_stress(true);
    const program & compiled = cx.own_program(compiler::compile(source));
    const run_result ran = cx.run(compiled);
    return ran.ok ? cx.to_string(ran.returned) : "THREW " + ran.error;
}

void expect_widget(std::string_view source, bool serializable, std::string_view want) {
    const std::string got = with_widget(source, serializable);
    if (got != want) {
        std::printf("FAIL     %s\n  -> %s (want %s)\n", std::string{source}.c_str(), got.c_str(),
                    std::string{want}.c_str());
        ++ctbrowser_test_failures;
    }
}

} // namespace

int main() {
    js_expect("(function(){ var b = new Uint8Array([1, 2]).buffer;"
              " var c = structuredClone(b, {transfer: [b]});"
              " return b.byteLength + ',' + c.byteLength + ',' + new Uint8Array(c)[1]; })()",
              "0,2,2");
    // A view over a transferred buffer is a view over its copy.
    js_expect("(function(){ var b = new ArrayBuffer(4, {maxByteLength: 8});"
              " var c = structuredClone(new Uint8Array(b), {transfer: [b]});"
              " c.buffer.resize(8); return b.byteLength + ',' + c.length; })()",
              "0,8");
    js_expect("(function(){ var b = new ArrayBuffer(1); try { structuredClone(b, {transfer: [b, "
              "b]}); } catch (e) { return e.name + ',' + b.byteLength; } })()",
              "DataCloneError,1");
    js_expect("(function(){ var b = new ArrayBuffer(1); structuredClone(b, {transfer: [b]});"
              " try { structuredClone(b, {transfer: [b]}); } catch (e) { return e.name; } })()",
              "DataCloneError");
    js_expect("(function(){ try { structuredClone({}, {transfer: [{}]}); }"
              " catch (e) { return e.name; } })()",
              "DataCloneError");
    js_expect("(function(){ try { structuredClone(1, {transfer: 1}); }"
              " catch (e) { return e.name; } })()",
              "TypeError");
    js_expect("(function(){ var ab = new ArrayBuffer(16, {maxByteLength: 32});"
              " var dv = new DataView(ab, 8); ab.resize(0);"
              " try { structuredClone(dv); } catch (e) { return e.name; } })()",
              "DataCloneError");

    // A platform object: refused unless serializable, then its own state
    // (the internal slot included) on the interface's prototype.
    expect_widget("try { structuredClone(new Widget()); return 'copied'; }"
                  " catch (e) { return e.name; }",
                  false, "DataCloneError");
    expect_widget("var w = new Widget(); var c = structuredClone(w);"
                  "return (c !== w) + ',' + (Object.getPrototypeOf(c) === Widget.prototype) +"
                  " ',' + c.size;",
                  true, "true,true,3");
    // Only the interface's state: a page's expando is neither copied nor able
    // to make the clone fail (a function is not cloneable).
    expect_widget("var w = new Widget(); w.extra = 1; w.fn = function () {};"
                  "var c = structuredClone(w);"
                  "return Object.keys(c).join() + ',' + ('extra' in c) + ',' + ('fn' in c);",
                  true, "size,false,false");
    // The global binding is configurable; the registered interface survives
    // deletion or replacement, including collections during the clone.
    for (const std::string change : {"delete globalThis.Widget;", "Widget = function () {};"}) {
        expect_widget("var w = new Widget();" + change +
                          "var c = structuredClone(w);"
                          "return (Object.getPrototypeOf(c) === Object.getPrototypeOf(w)) +"
                          " ',' + c.size;",
                      true, "true,3");
    }
    // An object on a prototype the page made is not a platform object at all.
    expect_widget("class Plain {} var c = structuredClone(new Plain());"
                  "return Object.getPrototypeOf(c) === Object.prototype;",
                  false, "true");
    REPORT("structured_clone");
}
