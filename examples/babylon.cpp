// BabylonJS on ctbrowser — the classic Playground "hello world": an
// ArcRotate camera, a hemispheric light, a red sphere on a ground, and a
// spinning blue box. Rendered by babylon.hpp's software 3D rasterizer
// straight into the <canvas> pixel buffer. No WebGL, no GPU — the
// BABYLON.* API is a native shim.
//
// DRAG with the left mouse button to orbit the camera. Close the window
// to quit.
//
// Build & run (needs SDL3 + Boost; both via linuxbrew):
//   brew install boost sdl3
//   make babylon && ./babylon
#include <ctbrowser.hpp>
#include <ctbrowser/app.hpp>
#include <SDL3/SDL_main.h>

using app = ctbrowser::page<R"(<!DOCTYPE html>
<title>Babylon on ctbrowser</title>
<style>
  body { margin: 0; background: #101015; }
  #renderCanvas { width: 800px; height: 600px; }
</style>
<canvas id=renderCanvas width=800 height=600></canvas>
<script>
  const canvas = document.getElementById("renderCanvas");
  const engine = new BABYLON.Engine(canvas, true);

  const createScene = function () {
    const scene = new BABYLON.Scene(engine);
    scene.clearColor = new BABYLON.Color4(0.08, 0.09, 0.13, 1);

    const camera = new BABYLON.ArcRotateCamera("camera",
        Math.PI / 3, Math.PI / 3, 12, BABYLON.Vector3.Zero(), scene);
    camera.attachControl(canvas, true);

    const light = new BABYLON.HemisphericLight("light",
        new BABYLON.Vector3(0.3, 1, 0.2), scene);
    light.intensity = 0.95;

    const red = new BABYLON.StandardMaterial("red", scene);
    red.diffuseColor = new BABYLON.Color3(0.85, 0.25, 0.2);
    const sphere = BABYLON.MeshBuilder.CreateSphere("sphere",
        { diameter: 3, segments: 20 }, scene);
    sphere.position.y = 1.5;
    sphere.material = red;

    const blue = new BABYLON.StandardMaterial("blue", scene);
    blue.diffuseColor = new BABYLON.Color3(0.3, 0.5, 0.9);
    const box = BABYLON.MeshBuilder.CreateBox("box", { size: 2 }, scene);
    box.position.x = 4;
    box.position.y = 1;
    box.material = blue;

    BABYLON.MeshBuilder.CreateGround("ground", { width: 16, height: 16 }, scene);
    return { scene: scene, box: box };
  };

  const world = createScene();
  engine.runRenderLoop(function () {
    world.box.rotation.y += 0.02;
    world.box.rotation.x += 0.01;
    world.scene.render();
  });
</script>)">;

static_assert(app::script_valid, "Babylon example script must parse");

int main(int, char **) {
	ctbrowser::app_options opts;
	opts.width = 800;
	opts.height = 600;
	return ctbrowser::run_app<app>(opts);
}
