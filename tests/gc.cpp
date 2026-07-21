// The ctjs cycle collector, headless. Reference counting frees acyclic garbage
// on its own; this proves it also reclaims reference CYCLES that pure
// refcounting cannot - both plain object cycles (a.o=b; b.o=a) and closure
// cycles (c.fn = () => c), the kind the DOM/game create constantly. Two natives
// expose glibc's in-use heap and a manual collect(); the script allocates pure
// garbage cycles and we assert collect() gives the memory back. No SDL.
#include <ctbrowser.hpp>
#include <cstdio>
#include <malloc.h>
#include <vector>

using page = ctbrowser::page<R"(<!DOCTYPE html>
<title>gc</title>
<script>
  function objCycles(n)  { for (let i=0;i<n;i++){ let a={}; let b={}; a.o=b; b.o=a; } }
  function closCycles(n) { for (let i=0;i<n;i++){ let c={}; c.fn=function(){return c;}; } }
  var h0 = __heap();  objCycles(20000);  var h1 = __heap();  var h2 = __collect();
  var objLeak = h1 - h0;      var objAfter = h2 - h0;
  var c0 = __heap();  closCycles(20000); var c1 = __heap();  var c2 = __collect();
  var closLeak = c1 - c0;     var closAfter = c2 - c0;
)">;

int main() {
	std::vector<ctjs::binding> extra;
	extra.push_back({"__heap", ctjs::native([](const std::vector<ctjs::value> &) {
		                                          return ctjs::value{static_cast<double>(mallinfo2().uordblks)};
	                                          },
	                                          "__heap")});
	extra.push_back({"__collect", ctjs::native([](const std::vector<ctjs::value> &) {
		                                            ctjs::gc::collect();
		                                            return ctjs::value{static_cast<double>(mallinfo2().uordblks)};
	                                            },
	                                            "__collect")});
	ctbrowser::engine<page> e(std::move(extra));
	if (!e.script.ok()) {
		std::printf("FAIL: script threw: %s\n", e.script.exception_message().c_str());
		return 1;
	}
	const double objLeak = e.script["objLeak"].to_number();
	const double objAfter = e.script["objAfter"].to_number();
	const double closLeak = e.script["closLeak"].to_number();
	const double closAfter = e.script["closAfter"].to_number();
	std::printf("object cycles : leaked %.0f KB, after collect() %.0f KB\n", objLeak / 1024, objAfter / 1024);
	std::printf("closure cycles: leaked %.0f KB, after collect() %.0f KB\n", closLeak / 1024, closAfter / 1024);

	int fails = 0;
	if (objLeak < 1000000) { std::printf("FAIL: object cycles did not leak (test setup)\n"); ++fails; }
	if (objAfter > objLeak * 0.30) { std::printf("FAIL: object cycles not collected\n"); ++fails; }
	if (closLeak < 1000000) { std::printf("FAIL: closure cycles did not leak (test setup)\n"); ++fails; }
	if (closAfter > closLeak * 0.30) { std::printf("FAIL: closure cycles not collected\n"); ++fails; }
	if (fails == 0) { std::printf("gc: PASS (object AND closure cycles reclaimed)\n"); }
	return fails == 0 ? 0 : 1;
}
