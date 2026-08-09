// IS THE LIGHTING ARITHMETIC RIGHT, not merely present.
//
// tests/corpus/babylon/babylon_ratchet.cpp rung 5 asks whether a directional light produces
// two different shades on a turned box. That catches a light being ignored
// entirely and nothing else: a normal matrix that transposed, an intensity
// applied twice, or a Lambert term clamped in the wrong place would all still
// give "two shades" and all still be wrong.
//
// SO THIS ONE COMPUTES THE ANSWER FIRST AND COMPARES. A directional light on a
// flat surface with no specular, no emissive and no ambient is
//
//     colour = diffuseColor * intensity * max(0, dot(N, -L))
//
// which is arithmetic anybody can do on paper, and the geometry below is chosen
// so the dot product is a number worth checking: 1, then cos(60 degrees) = 0.5,
// then a surface facing away where the whole term must vanish.
//
// A PLANE FACING THE CAMERA, not a box, and not a ground seen at an angle: one
// flat surface whose normal is exactly (0, 0, -1) makes the expected value a
// calculation rather than an estimate, and puts the whole face at one colour so
// the centre pixel is representative of it.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <ctbrowser.hpp>

#include "check.hpp"

namespace {

[[nodiscard]] std::string read_file(const std::string & path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) { return {}; }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// The colour at the middle of the canvas, which is the middle of the plane.
struct sample {
    int red = -1;
    int green = -1;
    int blue = -1;
    [[nodiscard]] std::string describe() const {
        return std::to_string(red) + "," + std::to_string(green) + "," + std::to_string(blue);
    }
};

[[nodiscard]] sample centre_of(ctbrowser::shell::browser & page) {
    const auto txn = page.doc().read();
    ctbrowser::node_id canvas{};
    const auto walk = [&](auto && self, ctbrowser::node_id at) -> void {
        if (!canvas && page.atoms().text(txn.tag(at).value_or(ctbrowser::atom{})) == "canvas") {
            canvas = at;
        }
        for (const ctbrowser::node_id child : txn.children(at)) { self(self, child); }
    };
    walk(walk, txn.root());
    const auto pixels = page.canvases().pixels_of(canvas);
    if (!canvas || pixels == nullptr) { return {}; }
    const ctbrowser::color c{
        pixels->at(static_cast<int>(pixels->width / 2), static_cast<int>(pixels->height / 2))};
    return sample{c.red(), c.green(), c.blue()};
}

// Render one plane under one directional light and read the middle of it.
//
// `light` is the direction the light TRAVELS, which is what Babylon's
// DirectionalLight takes - so a surface facing (0, 0, -1) is lit head-on by a
// light travelling (0, 0, 1).
[[nodiscard]] sample lit_by(const std::string & bundle, const std::string & light,
                            double grey = 0.8) {
    auto page =
        std::make_unique<ctbrowser::shell::browser>(ctbrowser::shell::browser_options{200, 200});
    page->assets().add(
        "babylon.js",
        std::vector<std::byte>{reinterpret_cast<const std::byte *>(bundle.data()),
                               reinterpret_cast<const std::byte *>(bundle.data() + bundle.size())});
    page->load_html(R"(<html><head><meta charset="utf-8">
      <script src="babylon.js"></script></head>
      <body><canvas id=c width=64 height=64></canvas></body></html>)");
    (void)page->run_script(std::string{R"JS((function () {
        var engine = new BABYLON.Engine(document.getElementById('c'), true);
        var scene = new BABYLON.Scene(engine);
        // BLACK, so nothing the light does not account for can reach the pixel.
        scene.clearColor = new BABYLON.Color4(0, 0, 0, 1);
        // AND NO AMBIENT. Babylon adds scene.ambientColor * material.ambientColor
        // to every surface; leaving it at its default would put a constant into
        // an equation this test is checking exactly.
        scene.ambientColor = new BABYLON.Color3(0, 0, 0);
        var camera = new BABYLON.FreeCamera('cam', new BABYLON.Vector3(0, 0, -5), scene);
        camera.setTarget(BABYLON.Vector3.Zero());
        // A PLANE FACING THE CAMERA: its normal is exactly (0, 0, -1), so the
        // Lambert term is a number this test can compute rather than estimate.
        // Big enough to cover the middle of the canvas at this distance.
        var plane = BABYLON.MeshBuilder.CreatePlane('p', {size: 6}, scene);
        var m = new BABYLON.StandardMaterial('m', scene);
        m.diffuseColor = new BABYLON.Color3()JS"} +
                           std::to_string(grey) + ", " + std::to_string(grey) + ", " +
                           std::to_string(grey) + R"JS();
        // NO SPECULAR AND NO EMISSIVE, so `diffuse * dot` is the whole answer.
        m.specularColor = new BABYLON.Color3(0, 0, 0);
        m.emissiveColor = new BABYLON.Color3(0, 0, 0);
        m.ambientColor = new BABYLON.Color3(0, 0, 0);
        plane.material = m;
        var sun = new BABYLON.DirectionalLight('sun', new BABYLON.Vector3()JS" +
                           light + R"JS(), scene);
        sun.intensity = 1;
        sun.diffuse = new BABYLON.Color3(1, 1, 1);
        sun.specular = new BABYLON.Color3(0, 0, 0);
        engine.runRenderLoop(function () { scene.render(); });
        return 'ok';
      })())JS");
    for (int i = 0; i < 90; ++i) { page->tick(16); }
    return centre_of(*page);
}

// sRGB is NOT applied by StandardMaterial's diffuse path, so the expected byte
// is the linear value scaled - which is what the engine's own software
// rasteriser writes and what the measurement below confirms.
[[nodiscard]] int expected_byte(double diffuse, double lambert) {
    return static_cast<int>(std::lround(std::clamp(diffuse * lambert, 0.0, 1.0) * 255.0));
}

void check_close(const char * what, int got, int wanted, int tolerance) {
    const bool ok = std::abs(got - wanted) <= tolerance;
    if (!ok) {
        std::printf("FAIL %s: %d, wanted %d (+/-%d)\n", what, got, wanted, tolerance);
        ++ctbrowser_test_failures;
    } else {
        std::printf("     %s: %d (wanted %d)\n", what, got, wanted);
    }
}

} // namespace

int main() {
    const std::string bundle = read_file("vendor/babylon/babylon.js");
    if (bundle.empty()) {
        std::printf("SKIP babylon_lighting: vendor/babylon/babylon.js is missing\n");
        return 0;
    }

    const double grey = 0.8;

    // --- head on: dot(N, -L) is 1 -------------------------------------------
    {
        const sample head_on = lit_by(bundle, "0, 0, 1", grey);
        check_close("a surface facing the light", head_on.red, expected_byte(grey, 1.0), 3);
        // GREY MEANS GREY. A channel that drifted would say the light's colour
        // or the material's is being applied per-channel differently, which a
        // single-channel check cannot see.
        check_close("the same in green", head_on.green, head_on.red, 1);
        check_close("the same in blue", head_on.blue, head_on.red, 1);
    }

    // --- 60 degrees off: dot is exactly 0.5 ---------------------------------
    //
    // The light travels (sin 60, 0, cos 60), so -L is (-sin 60, 0, -cos 60) and
    // the plane's normal (0, 0, -1) dots with it to cos 60 = 0.5. Half the
    // brightness, and a Lambert term that was squared, halved twice or applied
    // to the wrong vector misses it.
    {
        const sample angled = lit_by(bundle, "0.8660254, 0, 0.5", grey);
        check_close("a surface at 60 degrees", angled.red, expected_byte(grey, 0.5), 4);
    }

    // --- 45 degrees: dot is sqrt(2)/2 ---------------------------------------
    {
        const sample angled = lit_by(bundle, "0.7071068, 0, 0.7071068", grey);
        check_close("a surface at 45 degrees", angled.red, expected_byte(grey, 0.7071068), 4);
    }

    // --- facing away: the term must VANISH ----------------------------------
    //
    // Not "be small". A Lambert term without the max(0, ...) clamp goes
    // NEGATIVE behind the surface, and a renderer that took the absolute value
    // instead would light the back of everything - which looks plausible until
    // a scene has a light behind it.
    {
        const sample behind = lit_by(bundle, "0, 0, -1", grey);
        check_close("a surface facing away", behind.red, 0, 2);
    }

    // --- and the material's colour is respected ------------------------------
    {
        const sample dim = lit_by(bundle, "0, 0, 1", 0.4);
        check_close("half the diffuse colour", dim.red, expected_byte(0.4, 1.0), 3);
    }

    REPORT("babylon_lighting");
}
