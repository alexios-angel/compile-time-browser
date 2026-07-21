// Regression for the "PLAY AGAIN hangs the game" bug. The game's clearLevel()
// tears the scene down, inside onBeforeRender, with:
//     while (scene.meshes.length) scene.meshes[0].dispose();
// This only terminates if scene.meshes is a LIVE view that shrinks as meshes are
// disposed. When it was a per-frame snapshot the array never shrank mid-loop and
// the loop spun forever, freezing the whole app. An iteration guard here catches
// a regression as a finite failure instead of hanging the test. No SDL.
#include <ctbrowser.hpp>
#include <cstdio>

using page = ctbrowser::page<R"(<!DOCTYPE html>
<canvas id="c" width="200" height="200"></canvas>
<script>
  const engine = new BABYLON.Engine(document.getElementById("c"), true);
  const scene = new BABYLON.Scene(engine);
  new BABYLON.FreeCamera("cam", new BABYLON.Vector3(0, 0, -10), scene);
  for (let i = 0; i < 20; i++) { BABYLON.MeshBuilder.CreateBox("b" + i, { size: 1 }, scene); }
  var g_ran = 0, g_runaway = 0, g_after = -1, g_iters = 0;
  scene.onBeforeRenderObservable.add(() => {
    if (g_ran) { return; }
    g_ran = 1;
    let guard = 0;
    // the EXACT idiom from clearLevel()
    while (scene.meshes.length) {
      scene.meshes[0].dispose();
      if (++guard > 5000) { g_runaway = 1; break; } // was: infinite loop
    }
    g_iters = guard;
    g_after = scene.meshes.length;
  });
  engine.runRenderLoop(() => scene.render());
</script>)">;

static int num(ctbrowser::engine<page> & e, const char * g) {
	return static_cast<int>(e.script[g].to_number());
}

int main() {
	ctbrowser::engine<page> e;
	if (!e.script.ok()) {
		std::printf("FAIL: script threw: %s\n", e.script.exception_message().c_str());
		return 1;
	}
	e.resize_viewport(200, 200);
	// pump a few frames so runRenderLoop -> scene.render() -> onBeforeRender fires
	for (int i = 0; i < 5; ++i) {
		e.frame(200);
		e.tick(1.0 / 60.0);
	}

	const int ran = num(e, "g_ran");
	const int runaway = num(e, "g_runaway");
	const int after = num(e, "g_after");
	const int iters = num(e, "g_iters");
	std::printf("teardown ran=%d runaway=%d meshesAfter=%d iters=%d\n", ran, runaway, after, iters);

	int fails = 0;
	if (ran != 1) { std::printf("FAIL: teardown never ran\n"); ++fails; }
	if (runaway != 0) { std::printf("FAIL: runaway loop - scene.meshes did not shrink\n"); ++fails; }
	if (after != 0) { std::printf("FAIL: scene not fully torn down (meshes=%d)\n", after); ++fails; }
	if (iters != 20) { std::printf("FAIL: expected 20 disposals, got %d\n", iters); ++fails; }

	if (fails == 0) { std::printf("scene teardown: PASS (dispose loop terminates, scene emptied)\n"); }
	return fails == 0 ? 0 : 1;
}
