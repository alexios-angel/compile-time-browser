// The BabylonJS core-API shim, headless. A Playground-style scene
// (Engine, Scene, ArcRotateCamera, HemisphericLight, a sphere, a ground
// and a spinning box) drives the software 3D renderer into the canvas
// pixel buffer. We tick frames and assert the buffer actually rendered
// (non-clear, opaque geometry) and animates (a rotating box changes
// pixels between frames) - no SDL, no GPU.
#include <ctbrowser.hpp>
#include <cstdio>
#include <vector>

static int failures = 0;
#define CHECK(cond)                                                          \
	do {                                                                     \
		if (!(cond)) {                                                       \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
			++failures;                                                      \
		}                                                                    \
	} while (0)

using app = ctbrowser::page<R"(<!DOCTYPE html>
<title>babylon</title>
<canvas id=renderCanvas width=320 height=240></canvas>
<script>
  const canvas = document.getElementById("renderCanvas");
  const engine = new BABYLON.Engine(canvas, true);
  const scene  = new BABYLON.Scene(engine);
  scene.clearColor = new BABYLON.Color4(0.1, 0.1, 0.15, 1);

  const camera = new BABYLON.ArcRotateCamera("cam", Math.PI / 2, Math.PI / 3, 8,
                       BABYLON.Vector3.Zero(), scene);
  camera.attachControl(canvas, true);

  const light = new BABYLON.HemisphericLight("light", new BABYLON.Vector3(0, 1, 0), scene);
  light.intensity = 0.9;

  const mat = new BABYLON.StandardMaterial("m", scene);
  mat.diffuseColor = new BABYLON.Color3(0.9, 0.3, 0.2);

  const sphere = BABYLON.MeshBuilder.CreateSphere("sphere", { diameter: 2, segments: 16 }, scene);
  sphere.material = mat;
  sphere.position.y = 1;

  const ground = BABYLON.MeshBuilder.CreateGround("ground", { width: 8, height: 8 }, scene);

  const box = BABYLON.MeshBuilder.CreateBox("box", { size: 1.5 }, scene);
  box.position.x = 2.5;

  engine.runRenderLoop(function () {
    box.rotation.y += 0.08;
    scene.render();
  });
</script>)">;

static_assert(app::script_valid, "the Babylon script must parse");

int main() {
	ctbrowser::engine<app> e;
	if (!e.script.ok()) {
		std::printf("FAIL script threw: %s\n", e.script.exception_message().c_str());
		return 1;
	}

	ctbrowser::node * c = e.doc.by_id("renderCanvas");
	CHECK(c != nullptr && c->is_canvas());
	CHECK(c != nullptr && c->canvas_w == 320 && c->canvas_h == 240);
	if (c == nullptr) { return 1; }

	e.frame(320);            // layout (not required for pixels, but realistic)

	// before any tick the render loop hasn't run: buffer is initial black
	const uint32_t init = c->pixels[0];

	for (int i = 0; i < 3; ++i) { e.tick(1.0 / 60.0); }
	std::vector<uint32_t> snapA = c->pixels;   // after a few frames

	// the render loop re-registered itself (one rAF wrapper in flight)
	CHECK(e.ev.raf.size() == 1);

	const uint32_t clear = ((0xFFu) << 24) |            // ARGB, from clearColor (0.1,0.1,0.15)
	                       (static_cast<uint32_t>(0.1 * 255 + 0.5) << 16) |
	                       (static_cast<uint32_t>(0.1 * 255 + 0.5) << 8) |
	                       static_cast<uint32_t>(0.15 * 255 + 0.5);
	size_t nonClear = 0, opaque = 0, reddish = 0, distinct_from_init = 0;
	for (uint32_t p : snapA) {
		if (p != clear) { ++nonClear; }
		if ((p >> 24) == 0xFFu) { ++opaque; }
		const uint32_t rr = (p >> 16) & 0xFF, gg = (p >> 8) & 0xFF, bb = p & 0xFF;
		if (rr > gg && rr > bb && rr > 60) { ++reddish; }
		if (p != init) { ++distinct_from_init; }
	}
	std::printf("nonClear=%zu opaque=%zu reddish=%zu of %zu\n",
	            nonClear, opaque, reddish, snapA.size());
	CHECK(distinct_from_init > 0);              // the loop rendered SOMETHING
	CHECK(nonClear > 500);                      // real geometry rasterized
	CHECK(opaque == snapA.size());              // renderer writes opaque pixels
	CHECK(reddish > 100);                       // the red-material sphere is visible

	// animation: the spinning box changes the picture between frames
	for (int i = 0; i < 8; ++i) { e.tick(1.0 / 60.0); }
	size_t changed = 0;
	for (size_t i = 0; i < snapA.size(); ++i) {
		if (c->pixels[i] != snapA[i]) { ++changed; }
	}
	std::printf("pixels changed after spinning=%zu\n", changed);
	CHECK(changed > 30);                        // rotation.y += ... actually animated

	// attachControl: camera.attachControl registered mouse-orbit listeners,
	// and a drag orbits the camera without crashing (still renders)
	CHECK(e.ev.listeners.count("mousemove") == 1);
	CHECK(e.ev.listeners.count("mousedown") == 1);
	e.mouse_button(160, 120, true);             // press
	e.mouse_move(230, 80);                       // drag -> orbit alpha/beta
	e.tick(1.0 / 60.0);
	e.mouse_button(230, 80, false);             // release
	size_t stillRendering = 0;
	for (uint32_t p : c->pixels) { if (p != clear) { ++stillRendering; } }
	CHECK(stillRendering > 500);                // orbiting keeps a valid render

	if (failures == 0) { std::printf("babylon suite: all checks passed\n"); }
	return failures ? 1 : 0;
}
