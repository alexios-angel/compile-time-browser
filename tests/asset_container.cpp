// AssetContainer.removeAllFromScene must detach the container's meshes from the
// scene so clearLevel()'s dispose-everything loop can't destroy them. The game
// parks its model ORIGINALS in a container and clones them each round; without
// this, the second game clones disposed (empty) meshes -> invisible aliens/ship.
// This reproduces the exact sequence headlessly and asserts the clone renders.
#include <ctbrowser.hpp>
#include <cstdint>
#include <cstdio>

using page = ctbrowser::page<R"(<!DOCTYPE html>
<title>ac</title>
<canvas id=renderCanvas width=200 height=200></canvas>
<script>
  const engine = new BABYLON.Engine(document.getElementById("renderCanvas"), true);
  const scene = new BABYLON.Scene(engine);
  new BABYLON.HemisphericLight("l1", new BABYLON.Vector3(0, 1, 0), scene);
  new BABYLON.HemisphericLight("l2", new BABYLON.Vector3(0, -1, 0), scene);
  new BABYLON.FreeCamera("cam", new BABYLON.Vector3(0, 0, -8), scene);

  // the model ORIGINAL, parked off-screen and stashed in an AssetContainer
  const orig = BABYLON.MeshBuilder.CreateBox("orig", { size: 2 }, scene);
  orig.position = new BABYLON.Vector3(0, -2000, -2000);
  const container = new BABYLON.AssetContainer(scene);
  container.meshes.push(orig);
  container.removeAllFromScene();

  // GAME OVER: clearLevel disposes everything still in the scene
  let g = 0;
  while (scene.meshes.length) { scene.meshes[0].dispose(); if (++g > 1000) break; }

  // NEXT GAME: clone the original into view - must still have geometry
  const clone = orig.clone("player2");
  clone.position = new BABYLON.Vector3(0, 0, 0);
  engine.runRenderLoop(() => scene.render());
</script>)">;

int main() {
	ctbrowser::engine<page> e;
	if (!e.script.ok()) {
		std::printf("FAIL: script threw: %s\n", e.script.exception_message().c_str());
		return 1;
	}
	e.resize_viewport(200, 200);
	for (int i = 0; i < 5; ++i) {
		e.frame(200);
		e.tick(1.0 / 60.0);
	}
	const ctbrowser::node * canvas = e.doc.by_id("renderCanvas");
	if (canvas == nullptr || canvas->pixels.empty()) {
		std::printf("FAIL: no canvas (found=%d)\n", canvas != nullptr);
		return 1;
	}
	const uint32_t clear = canvas->pixels[0];
	int lit = 0;
	for (uint32_t px : canvas->pixels) {
		if (px != clear) { ++lit; }
	}
	std::printf("clone-after-teardown lit pixels = %d\n", lit);
	if (lit < 50) {
		std::printf("FAIL: cloned mesh did not render - the container original was destroyed\n");
		return 1;
	}
	std::printf("asset container: PASS (original survived teardown, clone renders)\n");
	return 0;
}
