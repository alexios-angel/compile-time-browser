// The BabylonJS core-API shim, headless. A Playground-style scene
// (Engine, Scene, ArcRotateCamera, HemisphericLight, a sphere, a ground
// and a spinning box) drives the software 3D renderer into the canvas
// pixel buffer. We tick frames and assert the buffer actually rendered
// (non-clear, opaque geometry) and animates (a rotating box changes
// pixels between frames) - no SDL, no GPU.
#include <ctbrowser.hpp>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

// fast CONSTEXPR trig (lookup-table idea): std::sin/std::cos are not
// constexpr until C++26, but these evaluate at COMPILE TIME -
namespace ftrig = ctbrowser::babylon::r3d;
static_assert(ftrig::fcos(0.0) == 1.0);
static_assert(ftrig::fsin(0.0) == 0.0);
static_assert(ftrig::fcos(3.141592653589793) < -0.999);         // cos(pi) ~ -1
static_assert(ftrig::fsin(1.5707963267948966) > 0.999);          // sin(pi/2) ~ 1
static_assert(ftrig::fcos(1.5707963267948966) < 1e-3 &&
              ftrig::fcos(1.5707963267948966) > -1e-3);           // cos(pi/2) ~ 0
// ...and a whole rotation matrix folds at compile time from it:
static_assert(ftrig::rotationY(3.141592653589793).a[0][0] < -0.999);

// the ENTIRE software renderer is constexpr: geometry + matrices + a
// z-buffered, lit, perspective rasterization all fold at COMPILE TIME
// (fast-table trig + Boost.Math ccmath give constexpr sqrt/floor/...).
constexpr int ct_box_render() {
	ftrig::geo box = ftrig::make_box(2.0);
	constexpr int W = 32, H = 32;
	std::array<uint32_t, W * H> px{};
	ftrig::view vw;
	vw.clear = {0.1, 0.1, 0.15, 1};
	vw.vp_view = ftrig::lookAtLH(ftrig::V3(1.5, 1.8, -4), ftrig::V3(0, 0, 0), ftrig::V3(0, 1, 0));
	vw.vp_proj = ftrig::perspectiveFovLH(0.8, 1.0, 0.1, 100.0);
	std::vector<ftrig::draw_item> items{ftrig::draw_item{&box, ftrig::identity(), {0.9, 0.3, 0.2, 1}, true}};
	std::vector<ftrig::light> lights{ftrig::light{0, ftrig::V3(0, 1, 0), 1.0, {1, 1, 1, 1}}};
	ftrig::renderer rr;
	rr.render(px.data(), W, H, vw, items, lights);
	const uint32_t clear = ftrig::pack(vw.clear, 1.0);
	int n = 0;
	for (uint32_t p : px) { if (p != clear) { ++n; } }
	return n;
}
static_assert(ct_box_render() > 40, "the whole 3D renderer rasterizes a box at compile time");

// the glTF loader is constexpr too: its JSON parser (unique_ptr recursion
// + a constexpr number parser replacing strtod) folds at compile time
namespace gltfns = ctbrowser::babylon::gltf;
static_assert([] {
	auto d = gltfns::json_parse(R"({"nodes":[{"mesh":0}],"v":[0.5,-1.6e-2,3],"n":"core"})");
	return d.get("nodes")->size() == 1 && (*d.get("v"))[0].as_num() == 0.5 &&
	       d.get("v")->size() == 3 && d.get("n")->as_str() == "core";
}(), "glTF JSON parses at compile time");

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
  window.addEventListener("resize", function () { engine.resize(); });
</script>)">;

static_assert(ctjs::vp::is_valid(app::script_text()), "the Babylon script must parse");

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

	// renderer regression: a SOLID box must occlude a bright sphere placed
	// INSIDE it. Guards the box winding — inward-facing normals get the
	// near faces backface-culled and the box renders inside-out.
	{
		namespace r3d = ctbrowser::babylon::r3d;
		const int RW = 64, RH = 64;
		std::vector<uint32_t> buf(static_cast<size_t>(RW) * RH, 0);
		r3d::geo box = r3d::make_box(2.0);
		r3d::geo inside = r3d::make_sphere(1.0, 12);
		r3d::view v;
		v.vp_view = r3d::lookAtLH(r3d::V3(1.5, 1.5, -5), r3d::V3(0, 0, 0), r3d::V3(0, 1, 0));
		v.vp_proj = r3d::perspectiveFovLH(0.8, 1.0, 0.1, 100.0);
		std::vector<r3d::draw_item> items = {
		    {&inside, r3d::translation(0, 0, 0), {1.0, 0.05, 0.05, 1}, true},
		    {&box, r3d::translation(0, 0, 0), {0.2, 0.3, 0.9, 1}, true},
		};
		std::vector<r3d::light> lights = {{0, r3d::V3(0, 1, 0), 1.0, {1, 1, 1, 1}}};
		r3d::renderer rr;
		rr.render(buf.data(), RW, RH, v, items, lights);
		size_t redThrough = 0, blueBox = 0;
		for (uint32_t p : buf) {
			const uint32_t r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
			if (r > g && r > b && r > 120) { ++redThrough; }
			if (b > r && b > 80) { ++blueBox; }
		}
		CHECK(blueBox > 100);        // the box renders
		CHECK(redThrough == 0);      // ...and occludes the interior sphere (outward winding)
	}

	// window resize: resize_viewport fires the DOM "resize" event, whose
	// listener calls engine.resize(), which resizes the canvas to the
	// new viewport (so the 3D view fills the resized window)
	e.resize_viewport(500, 400);
	CHECK(c->canvas_w == 500 && c->canvas_h == 400);
	e.tick(1.0 / 60.0);                          // renders at the new size
	CHECK(c->pixels.size() == static_cast<size_t>(500) * 400);
	size_t nonClearBig = 0;
	for (uint32_t p : c->pixels) { if (p != clear) { ++nonClearBig; } }
	CHECK(nonClearBig > 500);                    // still rendering after resize

	// fast trig accuracy vs std:: over a few full turns
	{
		double maxerr = 0.0;
		for (int i = -3600; i <= 3600; ++i) {
			const double a = i * (3.141592653589793 / 1800.0); // 0.1 deg steps, +/-2 turns
			maxerr = std::max(maxerr, std::fabs(ftrig::fcos(a) - std::cos(a)));
			maxerr = std::max(maxerr, std::fabs(ftrig::fsin(a) - std::sin(a)));
		}
		std::printf("fast-trig max abs error vs std over +/-2 turns: %.3e\n", maxerr);
		CHECK(maxerr < 3e-4);
	}

	if (failures == 0) { std::printf("babylon suite: all checks passed\n"); }
	return failures ? 1 : 0;
}
