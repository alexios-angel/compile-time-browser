// HOW MUCH OF A BABYLON SCENE THIS ENGINE CAN ACTUALLY RENDER.
//
// tests/webgl2_ratchet.cpp asks whether Babylon draws AT ALL and is answered:
// rung 10 there is a box on the canvas. This asks what a scene can CONTAIN, and
// it starts where that one stops.
//
// SAME SHAPE AS THE OTHER FOUR LADDERS: a level and a blocker, measured here and
// recorded in tests/babylon-ratchet.txt. The level may not go DOWN, and at the
// same level the blocker may not CHANGE, without this test failing. Only
// tools/babylon-ratchet.py --advance writes the record, because a test that
// edits its own expectations cannot fail.
//
// EVERY RUNG ASKS THE CANVAS. Not `scene.isReady()`, not `material.isReady()`,
// not "did it throw" - all three of those said yes while the canvas showed the
// clear colour, for weeks. A Babylon feature that fails here fails SILENTLY from
// JavaScript; that is the whole character of this corpus and the reason the
// rungs are written against pixels.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <ctbrowser.hpp>

#include "check.hpp"

namespace {

enum rung : int {
    rung_none = 0,
    rung_scene = 1,    // a box lands on the canvas in the right colour
    rung_texture = 2,  // a texture SAMPLES - the commonest thing a material does
    rung_two = 3,      // two meshes, two materials, both in the picture
    rung_animate = 4,  // the silhouette changes under a running Animation
    rung_light = 5,    // a directional light shades one face differently
    rung_specular = 6, // a highlight exists
    rung_alpha = 7,    // a transparent mesh shows what is behind it
    rung_post = 8,     // a post-process leaves the scene visible
    rung_shadow = 9,   // a shadow generator darkens the ground
    rung_pbr = 10,     // a PBRMaterial renders
    rung_gltf = 11,    // SceneLoader brings in a mesh
    rung_gui = 12,     // a babylon.gui.js control draws
};

[[nodiscard]] const char * rung_name(int level) {
    switch (level) {
    case rung_none: return "nothing";
    case rung_scene: return "a scene renders";
    case rung_texture: return "a texture samples";
    case rung_two: return "two meshes with two materials";
    case rung_animate: return "an animation moves the picture";
    case rung_light: return "a directional light shades";
    case rung_specular: return "specular highlights";
    case rung_alpha: return "alpha blending";
    case rung_post: return "a post-process runs";
    case rung_shadow: return "a shadow lands";
    case rung_pbr: return "a PBR material renders";
    case rung_gltf: return "glTF import";
    case rung_gui: return "the GUI draws";
    default: return "?";
    }
}

struct measurement {
    int level = rung_none;
    std::string blocker;
    bool stopped = false;

    void fail_at(int at, std::string why) {
        if (stopped) { return; }
        level = at - 1;
        const std::size_t newline = why.find('\n');
        blocker = newline == std::string::npos ? std::move(why) : why.substr(0, newline);
        stopped = true;
    }
    void reached(int at) {
        if (!stopped) { level = at; }
    }
};

[[nodiscard]] std::string read_file(const std::string & path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) { return {}; }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

[[nodiscard]] std::string recorded(const std::string & text, std::string_view key) {
    for (std::size_t at = 0; at < text.size();) {
        const std::size_t end = text.find('\n', at);
        const std::string_view line{text.data() + at,
                                    (end == std::string::npos ? text.size() : end) - at};
        if (line.starts_with(key) && line.size() > key.size() && line[key.size()] == '=') {
            return std::string{line.substr(key.size() + 1)};
        }
        if (end == std::string::npos) { break; }
        at = end + 1;
    }
    return {};
}

[[nodiscard]] std::string ask(ctbrowser::shell::browser & page, const std::string & expression) {
    const std::size_t before = page.bindings().console_output().size();
    (void)page.run_script("try { console.log('=' + String(" + expression +
                          ")); } catch (e) { console.log('=threw: ' + (e && e.message ? "
                          "e.message : e)); }");
    const auto & said = page.bindings().console_output();
    for (std::size_t i = said.size(); i-- > before;) {
        if (said[i].starts_with("=")) { return said[i].substr(1); }
    }
    return "<no answer>";
}

// WHAT IS ON THE CANVAS, as a histogram. Two sampled pixels cannot tell "the
// mesh is missing" from "the mesh is the same colour as the background", and
// several rungs below turn on exactly that difference.
struct picture {
    std::vector<std::pair<std::uint32_t, int>> colours; // most common first
    // EVERY PIXEL, IN SCAN ORDER, reduced to one number. A colour histogram
    // cannot see a picture that MOVED: a rotation preserves area, so the same
    // mesh turned by 22 degrees has the same colours in the same amounts and a
    // count-based rung reads it as "nothing happened". Rung 4 was written that
    // way and failed against a working animation.
    std::uint64_t digest = 0;
    bool ok = false;

    [[nodiscard]] int count_of(std::uint32_t rgb) const {
        for (const auto & [key, n] : colours) {
            if (key == rgb) { return n; }
        }
        return 0;
    }
    [[nodiscard]] std::uint32_t nth(std::size_t i) const {
        return i < colours.size() ? colours[i].first : 0;
    }
    [[nodiscard]] std::string describe() const {
        std::string out = std::to_string(colours.size()) + " colours";
        for (std::size_t i = 0; i < colours.size() && i < 3; ++i) {
            out += " " + std::to_string((colours[i].first >> 16) & 0xff) + "," +
                   std::to_string((colours[i].first >> 8) & 0xff) + "," +
                   std::to_string(colours[i].first & 0xff) + "x" +
                   std::to_string(colours[i].second);
        }
        return out;
    }
};

[[nodiscard]] picture canvas_of(ctbrowser::shell::browser & page) {
    picture out;
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
    if (!canvas || pixels == nullptr) { return out; }
    for (int y = 0; y < static_cast<int>(pixels->height); ++y) {
        for (int x = 0; x < static_cast<int>(pixels->width); ++x) {
            const ctbrowser::color c{pixels->at(x, y)};
            const std::uint32_t key =
                (std::uint32_t{c.red()} << 16) | (std::uint32_t{c.green()} << 8) | c.blue();
            bool found = false;
            for (auto & [existing, n] : out.colours) {
                if (existing == key) {
                    ++n;
                    found = true;
                    break;
                }
            }
            if (!found) { out.colours.emplace_back(key, 1); }
            out.digest = (out.digest ^ key) * 1099511628211ULL; // FNV-1a
        }
    }
    std::ranges::sort(out.colours,
                      [](const auto & a, const auto & b) { return a.second > b.second; });
    out.ok = true;
    return out;
}

// A PAGE WITH BABYLON ON IT, one per rung. Sharing a page would let a rung that
// leaves the scene in a strange state decide the verdict for the next one.
[[nodiscard]] std::unique_ptr<ctbrowser::shell::browser> babylon_page(const std::string & bundle) {
    auto page =
        std::make_unique<ctbrowser::shell::browser>(ctbrowser::shell::browser_options{200, 200});
    page->assets().add(
        "babylon.js",
        std::vector<std::byte>{reinterpret_cast<const std::byte *>(bundle.data()),
                               reinterpret_cast<const std::byte *>(bundle.data() + bundle.size())});
    page->load_html(R"(<html><head><meta charset="utf-8">
      <script src="babylon.js"></script></head>
      <body><canvas id=c width=64 height=64></canvas></body></html>)");
    return page;
}

// BUILD A SCENE, RUN FRAMES, READ THE CANVAS. `setup` is the JavaScript that
// makes the scene; it runs with `engine`, `scene`, `camera` and `box` already
// in scope and may return a string, which is reported when the rung fails.
struct render_result {
    std::string said;
    picture image;
    std::string error;
};

[[nodiscard]] render_result render(ctbrowser::shell::browser & page, const std::string & setup,
                                   int frames = 60) {
    render_result out;
    out.said = ask(page, std::string{R"JS((function () {
        try {
            var canvas = document.getElementById('c');
            var engine = new BABYLON.Engine(canvas, true);
            var scene = new BABYLON.Scene(engine);
            // NOT BLACK, and not a colour any material below uses: every rung
            // tells "painted nothing" from "painted the wrong thing" by
            // comparing against this.
            scene.clearColor = new BABYLON.Color4(0, 0, 1, 1);
            var camera = new BABYLON.FreeCamera('cam', new BABYLON.Vector3(0, 0, -5), scene);
            camera.setTarget(BABYLON.Vector3.Zero());
            var box = BABYLON.MeshBuilder.CreateBox('box', {size: 2}, scene);
            box.material = new BABYLON.StandardMaterial('mat', scene);
            window.__engine = engine; window.__scene = scene;
            window.__camera = camera; window.__box = box;
            var said = (function () { )JS"} +
                             setup + R"JS( })();
            window.__frames = 0;
            engine.runRenderLoop(function () { window.__frames++; scene.render(); });
            return said === undefined ? 'ok' : String(said);
        } catch (e) {
            return 'threw: ' + (e && e.message ? e.message : e);
        }
      })())JS");
    for (int i = 0; i < frames; ++i) { page.tick(16); }
    out.image = canvas_of(page);
    out.error = page.script_error();
    return out;
}

const std::uint32_t clear_blue = 0x0000ff;

[[nodiscard]] measurement measure(const std::string & bundle) {
    measurement m;

    // --- 1: a scene renders -------------------------------------------------
    //
    // GREEN ON DAY ONE, deliberately. A ladder whose first rung fails cannot
    // tell a broken engine from a broken harness, and this one was written
    // after tests/webgl2_ratchet.cpp already proved the same scene lands.
    {
        auto page = babylon_page(bundle);
        const render_result r = render(*page, R"JS(
            box.material.emissiveColor = new BABYLON.Color3(1, 0, 0);
            new BABYLON.HemisphericLight('light', new BABYLON.Vector3(0, 1, 0), scene);
        )JS");
        if (r.said != "ok") {
            m.fail_at(rung_scene, "the scene did not build: " + r.said);
            return m;
        }
        const int painted = 4096 - r.image.count_of(clear_blue);
        if (painted < 400) {
            m.fail_at(rung_scene, "a box painted " + std::to_string(painted) +
                                      " pixels, wanted at least 400 (" + r.image.describe() + ")" +
                                      (r.error.empty() ? "" : " | " + r.error));
            return m;
        }
    }
    m.reached(rung_scene);

    // --- 2: a texture SAMPLES -----------------------------------------------
    //
    // THE COMMONEST THING A MATERIAL DOES, and it comes second because without
    // it no material carrying an image works at all - which is most of what
    // anybody uses Babylon for.
    //
    // THROUGH emissiveTexture WITH A BLACK emissiveColor, so the answer does not
    // depend on the lighting being right as well and the texture is the ONLY
    // thing that can colour the box.
    //
    // BLACK, NOT WHITE, and the difference cost a measurement. Babylon's shader
    // says `emissiveColor += texture(emissiveSampler, uv).rgb * vEmissiveInfos.y`
    // - it ADDS - so a white emissiveColor saturates to white whether the
    // texture sampled or not, and the rung reads the same either way. It was
    // written with white and reported "0 green pixels" while the sampler was
    // broken AND after it was fixed.
    {
        auto page = babylon_page(bundle);
        const render_result r = render(*page, R"JS(
            var t = new BABYLON.DynamicTexture('dt', {width: 16, height: 16}, scene, false);
            var ctx = t.getContext();
            ctx.fillStyle = '#00ff00';
            ctx.fillRect(0, 0, 16, 16);
            t.update();
            box.material.emissiveTexture = t;
            box.material.emissiveColor = new BABYLON.Color3(0, 0, 0);
            box.material.disableLighting = true;
            return 'ready=' + t.isReady();
        )JS");
        const int green = r.image.count_of(0x00ff00);
        if (green < 400) {
            const int black = r.image.count_of(0x000000);
            m.fail_at(rung_texture,
                      std::string{"a green texture painted "} + std::to_string(green) +
                          " green pixels" +
                          (black > 400 ? " and " + std::to_string(black) +
                                             " BLACK ones - the texture sampled as zero"
                                       : "") +
                          " (" + r.said + ", " + r.image.describe() + ")" +
                          (r.error.empty() ? "" : " | " + r.error));
            return m;
        }
    }
    m.reached(rung_texture);

    // --- 3: two meshes, two materials ---------------------------------------
    {
        auto page = babylon_page(bundle);
        const render_result r = render(*page, R"JS(
            box.position.x = -1.5;
            box.material.emissiveColor = new BABYLON.Color3(1, 0, 0);
            box.material.disableLighting = true;
            var sphere = BABYLON.MeshBuilder.CreateSphere('s', {diameter: 1.8}, scene);
            sphere.position.x = 1.5;
            var second = new BABYLON.StandardMaterial('m2', scene);
            second.emissiveColor = new BABYLON.Color3(0, 1, 0);
            second.disableLighting = true;
            sphere.material = second;
        )JS");
        const int red = r.image.count_of(0xff0000);
        const int green = r.image.count_of(0x00ff00);
        if (red < 200 || green < 200) {
            m.fail_at(rung_two, "two materials painted red=" + std::to_string(red) +
                                    " green=" + std::to_string(green) + ", wanted 200 of each (" +
                                    r.image.describe() + ")" +
                                    (r.error.empty() ? "" : " | " + r.error));
            return m;
        }
    }
    m.reached(rung_two);

    // --- 4: an animation moves the picture ----------------------------------
    //
    // THE SILHOUETTE, NOT THE PROPERTY. `rotation.y` advancing proves the
    // animation system runs; it does NOT prove the new matrix reached the
    // shader, and those were separate bugs in the WebGL 2 work. So this rotates
    // a NON-square amount and asks the canvas to change.
    {
        auto page = babylon_page(bundle);
        const render_result before = render(*page, R"JS(
            box.material.emissiveColor = new BABYLON.Color3(1, 0, 0);
            box.material.disableLighting = true;
            box.scaling = new BABYLON.Vector3(1, 0.4, 1);
            BABYLON.Animation.CreateAndStartAnimation(
                'spin', box, 'rotation.z', 30, 120, 0, Math.PI / 2, 0);
        )JS",
                                            2);
        for (int i = 0; i < 60; ++i) { page->tick(16); }
        const picture later = canvas_of(*page);
        // THE PIXELS, NOT THE COUNT. See picture::digest: a rotation PRESERVES
        // AREA, so counting painted pixels reads a turning mesh as a still one.
        // This rung was written that way and failed against a working
        // animation - the rotation had reached 0.38 radians and the count had
        // moved by 16 pixels out of 592.
        if (before.image.digest == later.digest) {
            m.fail_at(rung_animate,
                      "an animation did not change the picture (identical pixels at frame 2 and "
                      "frame 62; rotation.z=" +
                          ask(*page, "String(window.__box.rotation.z)") + ", " + later.describe() +
                          ")");
            return m;
        }
    }
    m.reached(rung_animate);

    // --- 5: a directional light shades --------------------------------------
    //
    // TWO FACES, LIT DIFFERENTLY. A light that is ignored gives one flat colour
    // over the whole mesh, which is what a missing normal matrix looks like.
    {
        auto page = babylon_page(bundle);
        const render_result r = render(*page, R"JS(
            box.rotation.y = 0.7;
            box.rotation.x = 0.4;
            box.material.diffuseColor = new BABYLON.Color3(1, 1, 1);
            box.material.specularColor = new BABYLON.Color3(0, 0, 0);
            new BABYLON.DirectionalLight('d', new BABYLON.Vector3(-1, -1, 1), scene);
        )JS");
        // How many DISTINCT non-background colours the mesh has. One face lit
        // and another not is at least two.
        int shades = 0;
        for (const auto & [key, n] : r.image.colours) {
            if (key != clear_blue && n > 30) { ++shades; }
        }
        if (shades < 2) {
            m.fail_at(rung_light, "a directional light gave " + std::to_string(shades) +
                                      " shade(s) on a turned box, wanted 2 or more (" +
                                      r.image.describe() + ")" +
                                      (r.error.empty() ? "" : " | " + r.error));
            return m;
        }
    }
    m.reached(rung_light);

    // --- 6: specular ---------------------------------------------------------
    {
        auto page = babylon_page(bundle);
        const render_result r = render(*page, R"JS(
            var sphere = BABYLON.MeshBuilder.CreateSphere('s', {diameter: 3}, scene);
            box.dispose();
            var m = new BABYLON.StandardMaterial('spec', scene);
            m.diffuseColor = new BABYLON.Color3(0.2, 0.2, 0.2);
            m.specularColor = new BABYLON.Color3(1, 1, 1);
            m.specularPower = 32;
            sphere.material = m;
            new BABYLON.PointLight('p', new BABYLON.Vector3(-3, 3, -3), scene);
        )JS");
        // A HIGHLIGHT IS A FEW BRIGHT PIXELS, so this asks for a colour that is
        // much brighter than the diffuse term and covers a small area.
        int bright = 0;
        for (const auto & [key, n] : r.image.colours) {
            const int red = static_cast<int>((key >> 16) & 0xff);
            const int green = static_cast<int>((key >> 8) & 0xff);
            if (key != clear_blue && red > 180 && green > 180) { bright += n; }
        }
        if (bright < 4) {
            m.fail_at(rung_specular, "no specular highlight: " + std::to_string(bright) +
                                         " bright pixels (" + r.image.describe() + ")" +
                                         (r.error.empty() ? "" : " | " + r.error));
            return m;
        }
    }
    m.reached(rung_specular);

    // --- 7: alpha blending ---------------------------------------------------
    {
        auto page = babylon_page(bundle);
        const render_result r = render(*page, R"JS(
            box.position.z = 2;
            box.material.emissiveColor = new BABYLON.Color3(1, 0, 0);
            box.material.disableLighting = true;
            box.material.alpha = 0.5;
            var behind = BABYLON.MeshBuilder.CreateBox('b2', {size: 2}, scene);
            behind.position.z = 4;
            var m = new BABYLON.StandardMaterial('m2', scene);
            m.emissiveColor = new BABYLON.Color3(0, 1, 0);
            m.disableLighting = true;
            behind.material = m;
        )JS");
        // A BLEND OF RED OVER GREEN is neither: the rung is that a colour with
        // BOTH channels exists, which no opaque draw can produce here.
        int blended = 0;
        for (const auto & [key, n] : r.image.colours) {
            const int red = static_cast<int>((key >> 16) & 0xff);
            const int green = static_cast<int>((key >> 8) & 0xff);
            if (red > 40 && green > 40) { blended += n; }
        }
        if (blended < 50) {
            m.fail_at(rung_alpha, "nothing blended: " + std::to_string(blended) +
                                      " mixed pixels (" + r.image.describe() + ")" +
                                      (r.error.empty() ? "" : " | " + r.error));
            return m;
        }
    }
    m.reached(rung_alpha);

    // --- 8: a post-process runs ----------------------------------------------
    //
    // PassPostProcess IS THE IDENTITY - it copies the frame and changes nothing
    // - so "the picture survives" is the entire assertion, and a post-process
    // that blanks the canvas fails it loudly.
    {
        auto page = babylon_page(bundle);
        const render_result r = render(*page, R"JS(
            box.material.emissiveColor = new BABYLON.Color3(1, 0, 0);
            box.material.disableLighting = true;
            new BABYLON.PassPostProcess('pass', 1.0, camera);
        )JS");
        const int red = r.image.count_of(0xff0000);
        const int black = r.image.count_of(0x000000);
        if (red < 300) {
            m.fail_at(rung_post, "a pass post-process lost the scene: " + std::to_string(red) +
                                     " red pixels" +
                                     (black > 2000 ? " and " + std::to_string(black) +
                                                         " BLACK ones - the whole canvas went dark"
                                                   : "") +
                                     " (" + r.image.describe() + ")" +
                                     (r.error.empty() ? "" : " | " + r.error));
            return m;
        }
    }
    m.reached(rung_post);

    // --- 9: a shadow lands ---------------------------------------------------
    {
        auto page = babylon_page(bundle);
        const render_result r = render(*page, R"JS(
            camera.position = new BABYLON.Vector3(0, 4, -6);
            camera.setTarget(BABYLON.Vector3.Zero());
            box.position.y = 1.5;
            box.material.diffuseColor = new BABYLON.Color3(1, 0, 0);
            var ground = BABYLON.MeshBuilder.CreateGround('g', {width: 8, height: 8}, scene);
            var gm = new BABYLON.StandardMaterial('gm', scene);
            gm.diffuseColor = new BABYLON.Color3(1, 1, 1);
            gm.specularColor = new BABYLON.Color3(0, 0, 0);
            ground.material = gm;
            ground.receiveShadows = true;
            var light = new BABYLON.DirectionalLight('d', new BABYLON.Vector3(0, -1, 0.2), scene);
            light.position = new BABYLON.Vector3(0, 8, 0);
            var shadows = new BABYLON.ShadowGenerator(256, light);
            shadows.addShadowCaster(box);
        )JS");
        // THE GROUND IN TWO BRIGHTNESSES. A shadow is a darker version of the
        // same surface, so this asks for at least two greys with real area.
        int greys = 0;
        for (const auto & [key, n] : r.image.colours) {
            const int red = static_cast<int>((key >> 16) & 0xff);
            const int green = static_cast<int>((key >> 8) & 0xff);
            const int blue = static_cast<int>((key >> 0) & 0xff);
            if (key != clear_blue && n > 40 && std::abs(red - green) < 12 &&
                std::abs(green - blue) < 12) {
                ++greys;
            }
        }
        if (greys < 2) {
            m.fail_at(rung_shadow, "no shadow on the ground: " + std::to_string(greys) +
                                       " grey level(s) (" + r.image.describe() + ")" +
                                       (r.error.empty() ? "" : " | " + r.error));
            return m;
        }
    }
    m.reached(rung_shadow);

    // --- 10: a PBR material renders ------------------------------------------
    {
        auto page = babylon_page(bundle);
        const render_result r = render(*page, R"JS(
            var m = new BABYLON.PBRMaterial('pbr', scene);
            m.albedoColor = new BABYLON.Color3(1, 0, 0);
            m.metallic = 0;
            m.roughness = 1;
            m.emissiveColor = new BABYLON.Color3(0.3, 0, 0);
            box.material = m;
            new BABYLON.HemisphericLight('light', new BABYLON.Vector3(0, 1, 0), scene);
        )JS");
        const int painted = 4096 - r.image.count_of(clear_blue);
        if (painted < 400) {
            m.fail_at(rung_pbr, "a PBR material painted " + std::to_string(painted) + " pixels (" +
                                    r.image.describe() + ")" +
                                    (r.error.empty() ? "" : " | " + r.error));
            return m;
        }
    }
    m.reached(rung_pbr);

    m.fail_at(rung_gltf, "not measured yet - glTF import and the GUI need corpus additions, "
                         "see docs/babylon-plan.md");
    return m;
}

} // namespace

int main() {
    const std::string bundle = read_file("vendor/babylon/babylon.js");
    if (bundle.empty()) {
        std::printf("SKIP babylon_ratchet: vendor/babylon/babylon.js is missing\n");
        return 0;
    }

    const measurement now = measure(bundle);
    const std::string record = read_file("tests/babylon-ratchet.txt");
    const std::string recorded_level = recorded(record, "level");
    const std::string recorded_blocker = recorded(record, "blocker");
    const int was = recorded_level.empty() ? 0 : std::atoi(recorded_level.c_str());

    std::printf("     BABYLON LEVEL %d/12 (%s)\n", now.level, rung_name(now.level));
    if (!now.blocker.empty()) { std::printf("     blocked by: %s\n", now.blocker.c_str()); }

    if (now.level < was) {
        std::printf("FAIL babylon went BACKWARDS: %d, recorded %d (%s)\n", now.level, was,
                    rung_name(was));
        return 1;
    }
    if (now.level > was) {
        std::printf("     AHEAD of the record (%d > %d) - run tools/babylon-ratchet.py "
                    "--advance\n",
                    now.level, was);
        return 0;
    }
    // SAME LEVEL: the blocker must be the same one. A different blocker at the
    // same level means something else broke while the ladder stood still, which
    // is exactly the change a level number cannot see.
    if (now.blocker != recorded_blocker) {
        std::printf("FAIL babylon is stuck at %d but the blocker CHANGED\n  was: %s\n  now: %s\n",
                    now.level, recorded_blocker.c_str(), now.blocker.c_str());
        return 1;
    }
    std::printf("ok babylon_ratchet\n");
    return 0;
}
