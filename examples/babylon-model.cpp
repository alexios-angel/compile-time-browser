// BabylonJS glTF model-viewer example: loads the StandardShaderBall .glb
// (fetched + embedded at COMPILE time via --fetch-allow) and renders it
// with babylon.hpp's software rasterizer. Flat-shaded approximation:
// no textures / PBR / IBL skybox (those APIs are accepted but stubbed).
//
// The script is the Babylon Playground snippet verbatim except: `export`
// is dropped (ctjs has no modules) and a canvas/engine/`.then` wrapper is
// added. DRAG to orbit.
//
//   brew install boost sdl3
//   make babylon-model FETCH=1
//   LD_LIBRARY_PATH=$(brew --prefix)/lib ./babylon-model
#include <ctbrowser.hpp>
#include <ctbrowser/app.hpp>
#include <SDL3/SDL_main.h>

using app = ctbrowser::page<R"(<!DOCTYPE html>
<title>Babylon glTF model</title>
<style> body { margin: 0; } #renderCanvas { width: 800px; height: 600px; } </style>
<canvas id=renderCanvas width=800 height=600></canvas>
<script>
var canvas = document.getElementById("renderCanvas");
var engine = new BABYLON.Engine(canvas, true);

const createScene = async function () {
    const scene = new BABYLON.Scene(engine);

    const loadOptions = { pluginOptions: { gltf: { useOpenPBR: true } } };
    await BABYLON.AppendSceneAsync("https://assets.babylonjs.com/meshes/StandardShaderBall/StandardShaderBall.glb", scene, loadOptions);

    scene.createDefaultCamera(true, true, true);
    scene.activeCamera.alpha -= 3;
    scene.activeCamera.beta -= 0.2;
    scene.activeCamera.useAutoRotationBehavior = true;
    scene.activeCamera.lowerRadiusLimit = 0.1;
    scene.activeCamera.upperRadiusLimit = 0.5;

    const environmentTexture = BABYLON.CubeTexture.CreateFromPrefilteredData("https://assets.babylonjs.com/environments/ulmerMuenster.env", scene);
    scene.createDefaultSkybox(environmentTexture, true, undefined, 0.3, true);

    const surfaceMaterial = scene.getMaterialById("material_surface");
    surfaceMaterial.transmissionWeight = 0.0;
    surfaceMaterial.baseMetalness = 1.0;
    surfaceMaterial.specularRoughness = 0.75;
    surfaceMaterial.coatWeight = 1;
    surfaceMaterial.coatColor = new BABYLON.Color3(0.5, 0.025, 0.025);
    surfaceMaterial.baseColor = new BABYLON.Color3(0.64, 0.64, 0.64);

    const coreMaterial = scene.getMaterialById("core");
    coreMaterial.baseColor = new BABYLON.Color3(0.28, 0.252, 0.252);

    if (!engine.hostInformation.isMobile) {
        const debug = await scene.debugLayer.show({ embedMode: true });
        debug.select(surfaceMaterial, "OpenPBR");
    }

    return scene;
};

createScene().then(function (scene) {
    engine.runRenderLoop(function () { scene.render(); });
});
</script>)">;

static_assert(app::script_valid, "the glTF model script must parse");

int main(int, char **) {
	ctbrowser::app_options opts;
	opts.width = 800;
	opts.height = 600;
	return ctbrowser::run_app<app>(opts);
}
