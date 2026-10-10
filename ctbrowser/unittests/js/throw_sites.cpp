//===- ctbrowser/unittests/js/throw_sites.cpp - Throw sites -----*- C++ -*-===//
//
// Where a throw happened, and where it lands.
//
// context::last_throw_site is the position an embedder reports an uncaught
// exception at (an ErrorEvent's lineno and colno): the instruction the throw
// left, in the program that instruction belongs to. A throw inside a callback
// that a native parked and rethrew at its call site keeps the site inside the
// callback. A parse error carries its line and column as numbers too
// (program::error_line).
//
// A throw at a `yield*` (ECMA-262 14.4.14 step 7): when a getter on the inner
// iterator or its result throws, the exception is raised at the `yield*`, so
// the generator's own `catch` handles it rather than the caller of `.throw()`.
//
//===----------------------------------------------------------------------===//

#include "js_expect.hpp"

#include <ctbrowser/script/source_lines.hpp>

namespace {

using namespace ctbrowser::script;

/// Returns the line and column the throw of `source` was recorded at, as
/// "L:C", or "none" / "RAN" when there was no site or nothing threw.
std::string site_of(std::string_view source) {
    context cx;
    install_builtins(cx);
    const program & compiled = cx.own_program(compiler::compile(source));
    if (!compiled.ok) { return "COMPILE " + compiled.error; }
    if (cx.run(compiled).ok) { return "RAN"; }
    const context::throw_site site = cx.last_throw_site();
    if (site.source == nullptr) { return "none"; }
    const line_table lines{site.source->source};
    return std::to_string(lines.line_of(site.offset)) + ":" +
           std::to_string(lines.column_of(site.offset));
}

/// Fails the test unless site_of(source) equals `want`.
void expect_site(std::string_view source, std::string_view want) {
    const std::string got = site_of(source);
    if (got != want) {
        std::printf("FAIL     %s\n  -> %s (want %s)\n", std::string{source}.c_str(), got.c_str(),
                    std::string{want}.c_str());
        ++ctbrowser_test_failures;
    }
}

} // namespace

int main() {
    // The VM's own TypeError, a `throw` statement, and a throw inside a
    // callback that a native's `call` parked and rethrew at its call site.
    expect_site("var a = 1;\n\nnull.x;\n", "3:6");
    expect_site("var e = 'bar';\ntry {\n  throw e;\n} catch (x) {}\n  throw e;\n", "5:9");
    expect_site("[1].forEach(function () {\n  throw 1;\n});\n", "2:9");
    // A finally's rethrow, and an iterator close's, keep the site of the throw
    // they carry on, unless a throw caught inside the finally replaced it.
    expect_site("var n = 0;\ntry {\n  null.x;\n} finally {\n  n = 1;\n}\n", "3:8");
    expect_site("for (const v of [1]) {\n  try {\n    null.x;\n  } finally {}\n}\n", "3:10");
    expect_site("try {\n  null.x;\n} finally {\n  try { throw 2; } catch (e) {}\n}\n", "none");
    // A catch's own `throw` is a throw of its own, reported where it is.
    expect_site("try {\n  null.x;\n} catch (e) {\n  throw e;\n}\n", "4:9");

    // A parse error's position, as numbers.
    {
        const program bad = compiler::compile("var ok = 1;\nvar = ;\n");
        check(!bad.ok && bad.error_line == 2 && bad.error_column != 0,
              "a parse error records its line and column");
    }

    // yield*: .throw() with the inner result's `value` getter throwing, with
    // its `done` getter throwing, and with the `throw` getter itself throwing.
    js_expect("(function(){ var thrown = {}; var it = {}; it[Symbol.iterator] = function () {"
              " return { next: function () { return {done: false}; }, throw: function () {"
              " return Object.defineProperty({done: true}, 'value', {get: function () {"
              " throw thrown; }}); } }; }; var caught;"
              " function* g() { try { yield* it; } catch (e) { caught = e; } }"
              " var i = g(); i.next(); i.throw(); return caught === thrown; })()",
              "true");
    js_expect("(function(){ var thrown = {}; var it = {}; it[Symbol.iterator] = function () {"
              " return { next: function () { return {done: false}; }, return: function () {"
              " return Object.defineProperty({}, 'done', {get: function () {"
              " throw thrown; }}); } }; }; var caught;"
              " function* g() { try { yield* it; } catch (e) { caught = e; } }"
              " var i = g(); i.next(); i.return(); return caught === thrown; })()",
              "true");
    js_expect("(function(){ var thrown = {}; var it = {}; it[Symbol.iterator] = function () {"
              " return Object.defineProperty({ next: function () { return {done: false}; } },"
              " 'throw', {get: function () { throw thrown; }}); }; var caught;"
              " function* g() { try { yield* it; } catch (e) { caught = e; } }"
              " var i = g(); i.next(); i.throw(); return caught === thrown; })()",
              "true");
    REPORT("throw_sites");
}
