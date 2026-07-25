// Do the real example pages' scripts compile?
//
// Unit tests check features one at a time; this checks whether the language is
// actually usable, against pages nobody wrote for this engine. `pong.html` is
// the MDN breakout tutorial verbatim - if it compiles, ordinary JavaScript
// compiles.
//
// The other two are progress markers rather than assertions: they need language
// features later stages will add, and this prints WHAT they are still waiting
// on so the gap is visible instead of remembered.

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

// Not yet expected to compile. Reported so the remaining blocker is visible.
void report_progress(const std::string & path, std::string_view waiting_on) {
	const std::string js = page_script(path);
	const auto program = ctbrowser::script::compiler::compile(js);
	std::printf("     %s: %zu bytes of JS, %s (waiting on %s)\n", path.c_str(), js.size(),
	            program.ok ? "COMPILES - update this test" : program.error.c_str(),
	            std::string{waiting_on}.c_str());
}

} // namespace

int main() {
	// The web-compat proof. Nothing in it was written for this engine.
	must_compile("examples/pong.html");

	report_progress("examples/fetchboard.html", "async/await");
	report_progress("examples/space-invaders.html", "classes, for..of, template literals");

	REPORT("page_scripts");
}
