// CSS @keyframes at runtime: the engine advances animations each frame(),
// writing interpolated values into the animated element's inline style. This
// drives a color+opacity fade and checks the interpolation at t=0 / 0.5.
#include <ctbrowser.hpp>
#include <cstdio>
#include <string_view>

using page = ctbrowser::page<R"HTML(
<style>
@keyframes fade { from { color: #ff0000; opacity: 0 } to { color: #0000ff; opacity: 1 } }
#box { color: green; animation-name: fade; animation-duration: 1000ms; }
</style>
<div id=box>hi</div>
)HTML">;

static int failures = 0;
#define CHECK(c)                                                                                   \
	do {                                                                                           \
		if (!(c)) {                                                                                \
			std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                \
			++failures;                                                                            \
		}                                                                                          \
	} while (0)

static void eq(const char * label, std::string_view got, std::string_view want) {
	if (got != want) {
		std::printf("FAIL %s: got[%.*s] want[%.*s]\n", label, (int)got.size(), got.data(),
		            (int)want.size(), want.data());
		++failures;
	}
}

int main() {
	ctbrowser::engine<page> e;
	ctbrowser::node * box = e.doc.by_id("box");
	CHECK(box != nullptr);
	if (box == nullptr) { return 1; }

	// t = 0: the `from` stop (opacity 0, color #ff0000)
	e.frame(400);
	eq("t0 opacity", box->inline_style.get("opacity"), "0");
	eq("t0 color", box->inline_style.get("color"), "#ff0000");

	// t = 500ms -> progress 0.5: opacity 0.5, color halfway (#800080)
	e.tick(0.5);
	e.frame(400);
	eq("t0.5 opacity", box->inline_style.get("opacity"), "0.5");
	eq("t0.5 color", box->inline_style.get("color"), "#800080");

	if (failures == 0) { std::printf("anim suite: all checks passed\n"); }
	return failures ? 1 : 0;
}
