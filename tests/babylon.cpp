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

// second page: exercises the previously-stubbed surface that is now REAL -
// GlowLayer (additive bloom), scene.registerBeforeRender/registerAfterRender,
// and an ActionManager OnEveryFrameTrigger - all pumped by scene.render().
using app2 = ctbrowser::page<R"(<!DOCTYPE html>
<title>babylon-hooks</title>
<canvas id=c width=64 height=64></canvas>
<script>
  const engine = new BABYLON.Engine(document.getElementById("c"), true);
  const scene  = new BABYLON.Scene(engine);
  scene.clearColor = new BABYLON.Color4(0, 0, 0, 1);
  const cam = new BABYLON.ArcRotateCamera("cam", Math.PI / 2, Math.PI / 3, 6,
                       BABYLON.Vector3.Zero(), scene);
  new BABYLON.HemisphericLight("l", new BABYLON.Vector3(0, 1, 0), scene);
  const mat = new BABYLON.StandardMaterial("m", scene);
  mat.diffuseColor = new BABYLON.Color3(1, 1, 1);          // bright -> glows
  const box = BABYLON.MeshBuilder.CreateBox("b", { size: 3 }, scene);
  box.material = mat;
  const glow = new BABYLON.GlowLayer("glow", scene, { intensity: 1.5 });

  var beforeCount = 0, afterCount = 0, frameCount = 0;
  scene.registerBeforeRender(function () { beforeCount = beforeCount + 1; });
  scene.registerAfterRender(function () { afterCount = afterCount + 1; });

  const am = new BABYLON.ActionManager(scene);
  scene.actionManager = am;
  var trigVal = BABYLON.ActionManager.OnEveryFrameTrigger;
  am.registerAction(new BABYLON.ExecuteCodeAction(
      trigVal, function () { frameCount = frameCount + 1; }));
  var actLen = am.__actions ? am.__actions.length : -1;

  engine.runRenderLoop(function () { scene.render(); });
</script>)">;
static_assert(ctjs::vp::is_valid(app2::script_text()), "the hooks script must parse");

// third page: the mesh transform surface that was stubbed - getBoundingInfo,
// computeWorldMatrix, setPivotPoint/getPivotPoint, freeze/unfreezeWorldMatrix,
// bakeCurrentTransformIntoVertices - exercised without a render loop, results
// stashed in globals for the harness to read back.
using app3 = ctbrowser::page<R"(<!DOCTYPE html>
<title>babylon-mesh</title>
<canvas id=c width=64 height=64></canvas>
<script>
  const engine = new BABYLON.Engine(document.getElementById("c"), true);
  const scene  = new BABYLON.Scene(engine);
  const box = BABYLON.MeshBuilder.CreateBox("b", { size: 2 }, scene);

  const bi = box.getBoundingInfo();      // a size-2 box: local bounds [-1, 1]
  var bmaxx = bi.boundingBox.maximum.x;
  var bminx = bi.boundingBox.minimum.x;

  box.position.x = 5;                     // translation lands in world matrix m[12]
  var wtx = box.computeWorldMatrix(true).m[12];

  box.setPivotPoint(new BABYLON.Vector3(1, 2, 3));
  var pv = box.getPivotPoint();
  var pvsum = pv.x + pv.y + pv.z;

  box.position.x = 5;                     // freeze captures x=5...
  box.freezeWorldMatrix();
  box.position.x = 9;                     // ...and later edits are ignored
  var frozenTx = box.getWorldMatrix().m[12];
  box.unfreezeWorldMatrix();
  var liveTx = box.getWorldMatrix().m[12];

  const box2 = BABYLON.MeshBuilder.CreateBox("b2", { size: 2 }, scene);
  box2.position.x = 10;
  box2.bakeCurrentTransformIntoVertices();
  var bakedPosX = box2.position.x;        // reset to 0
  var bakedMaxX = box2.getBoundingInfo().boundingBox.maximumWorld.x; // ~11

  var sameEngine = (scene.getEngine() === engine) ? 1 : 0;   // getEngine() round-trips
  var uidDiff = (box.uniqueId !== box2.uniqueId) ? 1 : 0;     // meshes get distinct ids
  var uid1 = scene.getUniqueId();
  var uidInc = (scene.getUniqueId() > uid1) ? 1 : 0;          // getUniqueId is monotonic

  const cam = new BABYLON.FreeCamera("fc", new BABYLON.Vector3(0, 0, 0), scene);
  cam.setPosition(new BABYLON.Vector3(3, 4, 5));
  var camsum = cam.position.x + cam.position.y + cam.position.z; // 12
</script>)">;
static_assert(ctjs::vp::is_valid(app3::script_text()), "the mesh script must parse");

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

	// --- GlowLayer bloom, as a direct unit test: one bright pixel on black
	// must bleed a halo into its neighbours (and NOT reach far corners).
	{
		namespace bab = ctbrowser::babylon::detail;
		const int GW = 32, GH = 32;
		std::vector<uint32_t> buf(static_cast<size_t>(GW) * GH, 0xFF000000u); // opaque black
		const size_t ctr = static_cast<size_t>(GH / 2) * GW + GW / 2;
		buf[ctr] = 0xFFFFFFFFu;                        // one white pixel
		bab::apply_glow(buf.data(), GW, GH, 1.5);
		const uint32_t center_r = (buf[ctr] >> 16) & 0xFF;
		const uint32_t near_r = (buf[ctr - 2] >> 16) & 0xFF;  // 2px left, was black
		const uint32_t far_r = (buf[0] >> 16) & 0xFF;         // top-left corner, ~22px away
		std::printf("glow: center=%u near=%u far=%u\n", center_r, near_r, far_r);
		CHECK(center_r > 0);   // center stays lit
		CHECK(near_r > 0);     // bloom spread to a neighbour that was black
		CHECK(far_r == 0);     // and did NOT reach the far corner
	}

	// --- the newly-real Babylon surface (app2): registerBeforeRender /
	// registerAfterRender fire once per rendered frame, an ActionManager
	// OnEveryFrameTrigger fires once per frame, and a GlowLayer'd bright box
	// renders. All driven purely by scene.render() inside the render loop.
	{
		ctbrowser::engine<app2> e2;
		CHECK(e2.script.ok());
		if (!e2.script.ok()) {
			std::printf("FAIL app2 threw: %s\n", e2.script.exception_message().c_str());
		}
		ctbrowser::node * c2 = e2.doc.by_id("c");
		CHECK(c2 != nullptr && c2->is_canvas());
		e2.frame(64);
		const int N = 5;
		for (int i = 0; i < N; ++i) { e2.tick(1.0 / 60.0); }
		const int before = static_cast<int>(e2.script["beforeCount"].to_number());
		const int after = static_cast<int>(e2.script["afterCount"].to_number());
		const int frames = static_cast<int>(e2.script["frameCount"].to_number());
		const int trigVal = static_cast<int>(e2.script["trigVal"].to_number());
		const int actLen = static_cast<int>(e2.script["actLen"].to_number());
		std::printf("hooks: before=%d after=%d frame=%d (ticks=%d) trigVal=%d actLen=%d\n",
		            before, after, frames, N, trigVal, actLen);
		CHECK(trigVal == 11);  // BABYLON.ActionManager.OnEveryFrameTrigger static resolves
		CHECK(actLen == 1);    // registerAction stored the action (this-binding across `new` arg)
		CHECK(before == N);    // scene.registerBeforeRender fired each frame
		CHECK(after == N);     // scene.registerAfterRender fired each frame
		CHECK(frames == N);    // ActionManager OnEveryFrameTrigger fired each frame
		size_t lit = 0;
		if (c2 != nullptr) {
			for (uint32_t p : c2->pixels) { if ((p & 0x00FFFFFFu) != 0) { ++lit; } }
		}
		CHECK(lit > 200);     // the glowing white box rasterized
	}

	// --- glow include/exclude masking, unit-tested: two bright pixels, only the
	// masked one may seed the bloom, so only it grows a halo.
	{
		namespace bab = ctbrowser::babylon::detail;
		const int MW = 32, MH = 32;
		std::vector<uint32_t> buf(static_cast<size_t>(MW) * MH, 0xFF000000u);
		const size_t left = static_cast<size_t>(8) * MW + 8;
		const size_t right = static_cast<size_t>(8) * MW + 24;
		buf[left] = 0xFFFFFFFFu;
		buf[right] = 0xFFFFFFFFu;
		std::vector<uint8_t> mask(static_cast<size_t>(MW) * MH, 0);
		mask[left] = 1;                                // only the left pixel glows
		bab::apply_glow(buf.data(), MW, MH, 1.5, &mask);
		const uint32_t lhalo = (buf[left - 2] >> 16) & 0xFF;
		const uint32_t rhalo = (buf[right - 2] >> 16) & 0xFF;
		std::printf("masked glow: lhalo=%u rhalo=%u\n", lhalo, rhalo);
		CHECK(lhalo > 0);    // the included pixel bloomed
		CHECK(rhalo == 0);   // the excluded pixel did not
	}

	// --- mesh transform surface (app3): bounds, world matrix, pivot, freeze, bake
	{
		ctbrowser::engine<app3> e3;
		CHECK(e3.script.ok());
		if (!e3.script.ok()) {
			std::printf("FAIL app3 threw: %s\n", e3.script.exception_message().c_str());
		}
		const auto num = [&](const char * k) { return e3.script[k].to_number(); };
		std::printf("mesh: bmaxx=%g bminx=%g wtx=%g pvsum=%g frozenTx=%g liveTx=%g bakedPosX=%g bakedMaxX=%g\n",
		            num("bmaxx"), num("bminx"), num("wtx"), num("pvsum"),
		            num("frozenTx"), num("liveTx"), num("bakedPosX"), num("bakedMaxX"));
		CHECK(num("bmaxx") == 1.0);              // getBoundingInfo local bounds
		CHECK(num("bminx") == -1.0);
		CHECK(num("wtx") == 5.0);                // computeWorldMatrix translation
		CHECK(num("pvsum") == 6.0);              // setPivotPoint/getPivotPoint round-trip
		CHECK(num("frozenTx") == 5.0);           // freezeWorldMatrix ignores later edits
		CHECK(num("liveTx") == 9.0);             // ...until unfreezeWorldMatrix
		CHECK(num("bakedPosX") == 0.0);          // bake resets position
		CHECK(std::fabs(num("bakedMaxX") - 11.0) < 1e-6); // ...folding it into the verts
		CHECK(num("sameEngine") == 1.0);         // scene.getEngine() returns the engine
		CHECK(num("uidDiff") == 1.0);            // meshes have distinct uniqueId
		CHECK(num("uidInc") == 1.0);             // scene.getUniqueId() increments
		CHECK(num("camsum") == 12.0);            // camera.setPosition moved the eye
	}

	if (failures == 0) { std::printf("babylon suite: all checks passed\n"); }
	return failures ? 1 : 0;
}
