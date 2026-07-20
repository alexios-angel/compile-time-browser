// BabylonJS "basic scene" Playground example, verbatim: a FreeCamera
// looking at a sphere on a ground, under a hemispheric light. Runs on
// babylon.hpp's software 3D renderer.
//
//   brew install boost sdl3
//   make babylon-freecam && LD_LIBRARY_PATH=$(brew --prefix)/lib ./babylon-freecam
#include <ctbrowser.hpp>
#include <ctbrowser/app.hpp>
#include <SDL3/SDL_main.h>

using app = ctbrowser::page<R"(<!DOCTYPE html>
<title>Babylon basic scene</title>
<style> body { margin: 0; } #renderCanvas { width: 800px; height: 600px; } </style>
<canvas id=renderCanvas width=800 height=600></canvas>
<script>
// Get the canvas DOM element
var canvas = document.getElementById('renderCanvas');
// Load the 3D engine
var engine = new BABYLON.Engine(canvas, true, {preserveDrawingBuffer: true, stencil: true});
// CreateScene function that creates and return the scene
var createScene = function(){
    // Create a basic BJS Scene object
    var scene = new BABYLON.Scene(engine);
    // Create a FreeCamera, and set its position to {x: 0, y: 5, z: -10}
    var camera = new BABYLON.FreeCamera('camera1', new BABYLON.Vector3(0, 5, -10), scene);
    // Target the camera to scene origin
    camera.setTarget(BABYLON.Vector3.Zero());
    // Attach the camera to the canvas
    camera.attachControl(canvas, false);
    // Create a basic light, aiming 0, 1, 0 - meaning, to the sky
    var light = new BABYLON.HemisphericLight('light1', new BABYLON.Vector3(0, 1, 0), scene);
    // Create a built-in "sphere" shape using the SphereBuilder
    var sphere = BABYLON.MeshBuilder.CreateSphere('sphere1', {segments: 16, diameter: 2, sideOrientation: BABYLON.Mesh.FRONTSIDE}, scene);
    // Move the sphere upward 1/2 of its height
    sphere.position.y = 1;
    // Create a built-in "ground" shape;
    var ground = BABYLON.MeshBuilder.CreateGround("ground1", { width: 6, height: 6, subdivisions: 2, updatable: false }, scene);
    // Return the created scene
    return scene;
};
// call the createScene function
var scene = createScene();
// run the render loop
engine.runRenderLoop(function(){
    scene.render();
});
// the canvas/window resize event handler
window.addEventListener('resize', function(){
    engine.resize();
});
</script>)">;

static_assert(ctjs::vp::is_valid(app::script_text()), "the basic-scene script must parse");

int main(int, char **) {
	ctbrowser::app_options opts;
	opts.width = 800;
	opts.height = 600;
	return ctbrowser::run_app<app>(opts);
}
