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
// `space-invaders.html` is a 66 KB BUNDLE (21 ES modules through
// tools/js-bundle.py) and is the stress case. It is a progress marker rather
// than an assertion: it stops on a regex literal, which is DEFERRED - there is
// no regex engine - so this prints what it is waiting on instead of failing.

import ctbrowser;

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

    // REGEX IS REJECTED BY NAME rather than mis-compiled. This used to be
    // checked against a 66 KB bundled page whose only blocker was one regex
    // literal - a mobile user-agent sniff. That page went with the engine it
    // was bundled for, so the claim is made directly: what mattered was never
    // the page, it was that the compiler says which construct it will not
    // take.
    must_stop_at_source("var mobile = /iphone|android/i.test(navigator.userAgent);",
                        "regular expression literals");

    REPORT("page_scripts");
}
