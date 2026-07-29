// Do the real example pages' scripts compile?
//
// Unit tests check features one at a time; this checks whether the language is
// actually usable, against pages nobody wrote for this engine. `pong.html` is
// the MDN breakout tutorial verbatim - if it compiles, ordinary JavaScript
// compiles.
//
// `fetchboard.html` is the async/await proof - fetch, await, try/catch,
// template literals and for..of in one page.
//
// And the constructs the compiler REFUSES are asserted to be refused BY NAME,
// which is the difference between a deferred feature and a mis-compile.

#include <ctbrowser.hpp>

#include "check.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

// Every <script> body in a page, concatenated - which is what the browser does
// with them, since there is one global scope.
[[nodiscard]] std::string page_script(const std::string & path) {
    std::ifstream in{path};
    if (!in) { return {}; }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string html = buffer.str();

    std::string js;
    std::size_t at = 0;
    while ((at = html.find("<script", at)) != std::string::npos) {
        const std::size_t open = html.find('>', at);
        const std::size_t close = html.find("</script", open);
        if (open == std::string::npos || close == std::string::npos) { break; }
        js += html.substr(open + 1, close - open - 1);
        js += '\n';
        at = close;
    }
    return js;
}

void must_compile(const std::string & path) {
    const std::string js = page_script(path);
    if (js.empty()) {
        std::printf("FAIL %s: no script found\n", path.c_str());
        ++ctbrowser_test_failures;
        return;
    }
    const auto program = ctbrowser::script::compiler::compile(js);
    if (!program.ok) {
        std::printf("FAIL %s (%zu bytes of JS): %s\n", path.c_str(), js.size(),
                    program.error.c_str());
        ++ctbrowser_test_failures;
        return;
    }
    std::printf("     %s: %zu bytes of JS, compiles\n", path.c_str(), js.size());
}

// Not yet expected to compile. The blocker is asserted, not just printed: if it
// changes, either something regressed or a deferral was lifted, and both are
// worth being told about.
// The same check against a snippet rather than a file: what is being tested
// is the compiler's refusal, not anybody's page.
void must_stop_at_source(const std::string & source, std::string_view blocker) {
    const ctbrowser::script::program compiled = ctbrowser::script::compiler::compile(source);
    if (compiled.ok) {
        std::printf("FAIL `%s` COMPILES - promote it\n", source.c_str());
        ++ctbrowser_test_failures;
        return;
    }
    if (compiled.error.find(blocker) == std::string::npos) {
        std::printf("FAIL `%s` stops at \"%s\", not at \"%s\"\n", source.c_str(),
                    compiled.error.c_str(), std::string{blocker}.c_str());
        ++ctbrowser_test_failures;
    }
}

// The other side of must_stop_at_source: a construct that DOES compile now, and
// is asserted to keep doing so. A deferral that has been lifted moves from one
// to the other, which is the whole reason the refusals are written down.
void must_compile_source(const std::string & source) {
    const ctbrowser::script::program compiled = ctbrowser::script::compiler::compile(source);
    if (!compiled.ok) {
        std::printf("FAIL `%s`: %s\n", source.c_str(), compiled.error.c_str());
        ++ctbrowser_test_failures;
    }
}

// head, then `before<i>after` for i in [0, times), then tail. Enough to build a
// program that is over one of the compiler's limits without writing it out.
[[nodiscard]] std::string repeated(std::string_view head, std::string_view before,
                                   std::string_view after, int times, std::string_view tail) {
    std::string out{head};
    for (int i = 0; i < times; ++i) {
        out += std::string{before} + std::to_string(i) + std::string{after};
    }
    return out + std::string{tail};
}

} // namespace

int main() {
    // The web-compat proof. Nothing in it was written for this engine.
    must_compile("examples/pages/pong.html");

    // async/await, template literals, for..of and try/catch, in a page written
    // against the real web platform.
    must_compile("examples/pages/fetchboard.html");

    // The pages the engine actually ships.
    must_compile("examples/pages/widgets.html");
    must_compile("examples/pages/invaders.html");

    // REGEX WAS REJECTED BY NAME rather than mis-compiled, and this line
    // asserted the refusal. It compiles now: the user-agent sniff that used to
    // be the standing example of what this engine would not take is an
    // ordinary expression. The refusal it replaced is worth remembering - the
    // point was never the page, it was that the compiler said which construct
    // it would not take, and so could say when it started taking it.
    must_compile_source("var mobile = /iphone|android/i.test(navigator.userAgent);");
    must_compile_source("var named = /(?<k>\\d+)/.exec('42');");
    must_compile_source("var ahead = /\\B(?=(\\d{3})+(?!\\d))/g;");

    // Spread in a call was refused here one commit ago, by name rather than by
    // number - "AST kind 13" is a fact about the parser's enum, not about
    // anybody's program. It compiles now, and this line moving from
    // must_stop_at_source to must_compile_source is what a lifted deferral is
    // supposed to look like: the test failed the moment it started working.
    must_compile_source("Math.max(...[1, 2, 3]);");
    must_compile_source("function f(a, ...rest) { return f(a, ...rest); }");

    // Accessors were the last name on the refusal list, and the object model
    // has somewhere to put one now. Both spellings compile.
    must_compile_source("var o = { get v() { return 1; }, set v(x) { this.n = x; } };");
    must_compile_source("class C { get v() { return 1; } static get w() { return 2; } }");
    must_compile_source("Object.defineProperty({}, 'x', { get() { return 1; } });");
    // A labelled BLOCK is not a loop, and `break lbl` out of one is legal.
    must_compile_source("outer: { if (1) { break outer; } }");
    must_stop_at_source("outer: { continue outer; }", "names a block, not a loop");

    // THE STRUCTURAL LIMITS THAT USED TO STOP THESE ARE GONE.
    //
    // Each of these was a silent truncation once - the program compiled, then
    // read the wrong property, or aliased two locals onto one register, or
    // branched to an address that was never a jump target. They were made LOUD
    // first and asserted here as refusals, which is what made it possible to
    // tell whether widening the instruction had actually worked. It had: all
    // three compile now, and the assertions moved rather than being deleted.
    //
    // p5.js needs every one of them - 1,452 declarations in its top-level frame
    // alone.
    must_compile_source(repeated("function f(o){ return 0", " + o.p", "", 300, "; }"));
    must_compile_source(repeated("function g(){ ", "let v", " = 1; ", 300, "return v0; }"));
    must_compile_source(repeated("function h(o){ if (o) { ", "o.m(", ");", 40000, "} return 1; }"));

    REPORT("page_scripts");
}
