// ONE PLANE UNDER ONE DIRECTIONAL LIGHT, rendering, for
// test/corpus/babylon/babylon_lighting.cpp to read the middle of.
//
// `light` is the direction the light TRAVELS, as "x, y, z", and `grey` the
// plane's diffuse grey level. Both arrive as the C++ side's text and are read
// with Number(), which parses a decimal exactly as a literal in the source
// would.
function babylon_lighting_scene(light, grey) {
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
    var plane = BABYLON.MeshBuilder.CreatePlane('p', {
        size: 6
    }, scene);
    var m = new BABYLON.StandardMaterial('m', scene);
    var g = Number(grey);
    m.diffuseColor = new BABYLON.Color3(g, g, g);
    // NO SPECULAR AND NO EMISSIVE, so `diffuse * dot` is the whole answer.
    m.specularColor = new BABYLON.Color3(0, 0, 0);
    m.emissiveColor = new BABYLON.Color3(0, 0, 0);
    m.ambientColor = new BABYLON.Color3(0, 0, 0);
    plane.material = m;
    var d = light.split(',').map(Number);
    var sun = new BABYLON.DirectionalLight('sun', new BABYLON.Vector3(d[0], d[1], d[2]), scene);
    sun.intensity = 1;
    sun.diffuse = new BABYLON.Color3(1, 1, 1);
    sun.specular = new BABYLON.Color3(0, 0, 0);
    engine.runRenderLoop(function () {
        scene.render();
    });
    return 'ok';
}
