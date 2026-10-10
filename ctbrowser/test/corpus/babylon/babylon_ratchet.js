// THE SCENES OF test/corpus/babylon/babylon_ratchet.cpp, one function per rung.
// Each builds its rung's scene and starts the render loop. The rung's check,
// and why it is written that way, is in the C++, which runs the frames and
// asks the canvas.

// BUILD A SCENE AND START RENDERING IT. `setup` makes the rung's scene; it runs
// with `scene`, `camera` and `box` already made and may return a string, which
// is reported when the rung fails. The answer is 'ok' when it returns nothing
// and 'threw: ...' when anything throws.
function babylon_ratchet_render(setup) {
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
        var box = BABYLON.MeshBuilder.CreateBox('box', {
            size: 2
        }, scene);
        box.material = new BABYLON.StandardMaterial('mat', scene);
        window.__engine = engine;
        window.__scene = scene;
        window.__camera = camera;
        window.__box = box;
        var said = setup(scene, camera, box);
        window.__frames = 0;
        engine.runRenderLoop(function () {
            window.__frames++;
            scene.render();
        });
        return said === undefined ? 'ok' : String(said);
    } catch (e) {
        return 'threw: ' + (e && e.message ? e.message : e);
    }
}

// --- 1: a scene renders ------------------------------------------------------
function babylon_ratchet_scene() {
    return babylon_ratchet_render(function (scene, camera, box) {
        box.material.emissiveColor = new BABYLON.Color3(1, 0, 0);
        new BABYLON.HemisphericLight('light', new BABYLON.Vector3(0, 1, 0), scene);
    });
}

// --- 2: a texture SAMPLES ----------------------------------------------------
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
function babylon_ratchet_texture() {
    return babylon_ratchet_render(function (scene, camera, box) {
        var t = new BABYLON.DynamicTexture('dt', {
            width: 16,
            height: 16
        }, scene, false);
        var ctx = t.getContext();
        ctx.fillStyle = '#00ff00';
        ctx.fillRect(0, 0, 16, 16);
        t.update();
        box.material.emissiveTexture = t;
        box.material.emissiveColor = new BABYLON.Color3(0, 0, 0);
        box.material.disableLighting = true;
        return 'ready=' + t.isReady();
    });
}

// --- 3: two meshes, two materials --------------------------------------------
function babylon_ratchet_two() {
    return babylon_ratchet_render(function (scene, camera, box) {
        box.position.x = -1.5;
        box.material.emissiveColor = new BABYLON.Color3(1, 0, 0);
        box.material.disableLighting = true;
        var sphere = BABYLON.MeshBuilder.CreateSphere('s', {
            diameter: 1.8
        }, scene);
        sphere.position.x = 1.5;
        var second = new BABYLON.StandardMaterial('m2', scene);
        second.emissiveColor = new BABYLON.Color3(0, 1, 0);
        second.disableLighting = true;
        sphere.material = second;
    });
}

// --- 4: an animation moves the picture ---------------------------------------
function babylon_ratchet_animate() {
    return babylon_ratchet_render(function (scene, camera, box) {
        box.material.emissiveColor = new BABYLON.Color3(1, 0, 0);
        box.material.disableLighting = true;
        box.scaling = new BABYLON.Vector3(1, 0.4, 1);
        BABYLON.Animation.CreateAndStartAnimation(
            'spin', box, 'rotation.z', 30, 120, 0, Math.PI / 2, 0);
    });
}

// How far rung 4's animation has turned the box, for the blocker when the
// picture did not move.
function babylon_ratchet_rotation_z() {
    return String(window.__box.rotation.z);
}

// --- 5: a directional light shades -------------------------------------------
function babylon_ratchet_light() {
    return babylon_ratchet_render(function (scene, camera, box) {
        box.rotation.y = 0.7;
        box.rotation.x = 0.4;
        box.material.diffuseColor = new BABYLON.Color3(1, 1, 1);
        box.material.specularColor = new BABYLON.Color3(0, 0, 0);
        new BABYLON.DirectionalLight('d', new BABYLON.Vector3(-1, -1, 1), scene);
    });
}

// --- 6: specular -------------------------------------------------------------
function babylon_ratchet_specular() {
    return babylon_ratchet_render(function (scene, camera, box) {
        var sphere = BABYLON.MeshBuilder.CreateSphere('s', {
            diameter: 3
        }, scene);
        box.dispose();
        var m = new BABYLON.StandardMaterial('spec', scene);
        m.diffuseColor = new BABYLON.Color3(0.2, 0.2, 0.2);
        m.specularColor = new BABYLON.Color3(1, 1, 1);
        m.specularPower = 32;
        sphere.material = m;
        new BABYLON.PointLight('p', new BABYLON.Vector3(-3, 3, -3), scene);
    });
}

// --- 7: alpha blending -------------------------------------------------------
function babylon_ratchet_alpha() {
    return babylon_ratchet_render(function (scene, camera, box) {
        box.position.z = 2;
        box.material.emissiveColor = new BABYLON.Color3(1, 0, 0);
        box.material.disableLighting = true;
        box.material.alpha = 0.5;
        var behind = BABYLON.MeshBuilder.CreateBox('b2', {
            size: 2
        }, scene);
        behind.position.z = 4;
        var m = new BABYLON.StandardMaterial('m2', scene);
        m.emissiveColor = new BABYLON.Color3(0, 1, 0);
        m.disableLighting = true;
        behind.material = m;
    });
}

// --- 8: a post-process runs --------------------------------------------------
function babylon_ratchet_post() {
    return babylon_ratchet_render(function (scene, camera, box) {
        box.material.emissiveColor = new BABYLON.Color3(1, 0, 0);
        box.material.disableLighting = true;
        new BABYLON.PassPostProcess('pass', 1.0, camera);
    });
}

// --- 9: a shadow lands -------------------------------------------------------
function babylon_ratchet_shadow() {
    return babylon_ratchet_render(function (scene, camera, box) {
        camera.position = new BABYLON.Vector3(0, 4, -6);
        camera.setTarget(BABYLON.Vector3.Zero());
        box.position.y = 1.5;
        box.material.diffuseColor = new BABYLON.Color3(1, 0, 0);
        var ground = BABYLON.MeshBuilder.CreateGround('g', {
            width: 8,
            height: 8
        }, scene);
        var gm = new BABYLON.StandardMaterial('gm', scene);
        gm.diffuseColor = new BABYLON.Color3(1, 1, 1);
        gm.specularColor = new BABYLON.Color3(0, 0, 0);
        ground.material = gm;
        ground.receiveShadows = true;
        var light = new BABYLON.DirectionalLight('d', new BABYLON.Vector3(0, -1, 0.2), scene);
        light.position = new BABYLON.Vector3(0, 8, 0);
        var shadows = new BABYLON.ShadowGenerator(256, light);
        shadows.addShadowCaster(box);
    });
}

// --- 10: a PBR material renders ----------------------------------------------
function babylon_ratchet_pbr() {
    return babylon_ratchet_render(function (scene, camera, box) {
        var m = new BABYLON.PBRMaterial('pbr', scene);
        m.albedoColor = new BABYLON.Color3(1, 0, 0);
        m.metallic = 0;
        m.roughness = 1;
        m.emissiveColor = new BABYLON.Color3(0.3, 0, 0);
        box.material = m;
        new BABYLON.HemisphericLight('light', new BABYLON.Vector3(0, 1, 0), scene);
    });
}
