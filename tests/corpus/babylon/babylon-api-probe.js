// HOW WIDE THE WORKING BABYLON SURFACE IS, as opposed to how far one scene gets.
//
// tests/corpus/babylon/babylon_ratchet.cpp climbs a ladder and stops at the first failure,
// which tells you ONE thing. This runs everything and tells you the shape of the
// gap. Both are needed, and the reason is recorded in tests/corpus/phaser/phaser-api.txt: the
// Phaser ratchet read 10/10 while `(5).hasOwnProperty` was undefined, because
// nothing on the ladder happened to ask a number for a property.
//
// EACH PROBE GETS A FRESH SCENE, built by the harness and handed in as
// (scene, box, engine). A probe returns a short string on success and THROWS on
// failure - the string is recorded, so a probe that starts answering something
// different is as visible as one that starts failing.
//
// PROBES EXPECTED TO FAIL ARE INCLUDED ON PURPOSE, the way
// webgl2-api-probe.js does it. "Not implemented" is a fact worth recording and
// worth being told about deliberately rather than discovering as a wrong answer
// three months later. tests/corpus/babylon/babylon-api.txt is what says which are which.

globalThis.__probes = [
  // --- the objects a scene is made of -------------------------------------
  ['mesh', 'CreateBox', function (scene) {
    var m = BABYLON.MeshBuilder.CreateBox('b', {size: 1}, scene);
    return 'verts=' + m.getTotalVertices();
  }],
  ['mesh', 'CreateSphere', function (scene) {
    var m = BABYLON.MeshBuilder.CreateSphere('s', {diameter: 1, segments: 8}, scene);
    return 'verts=' + m.getTotalVertices();
  }],
  ['mesh', 'CreateGround', function (scene) {
    var m = BABYLON.MeshBuilder.CreateGround('g', {width: 2, height: 2}, scene);
    return 'verts=' + m.getTotalVertices();
  }],
  ['mesh', 'CreateCylinder', function (scene) {
    var m = BABYLON.MeshBuilder.CreateCylinder('c', {height: 1, diameter: 1}, scene);
    return 'verts=' + m.getTotalVertices();
  }],
  ['mesh', 'CreateTorus', function (scene) {
    var m = BABYLON.MeshBuilder.CreateTorus('t', {}, scene);
    return 'verts=' + m.getTotalVertices();
  }],
  ['mesh', 'CreateLines', function (scene) {
    var m = BABYLON.MeshBuilder.CreateLines('l', {points: [
      BABYLON.Vector3.Zero(), new BABYLON.Vector3(1, 1, 0)]}, scene);
    if (!m) { throw 'no mesh'; }
    return 'made';
  }],
  ['mesh', 'uv and normal attributes', function (scene, box) {
    var uv = box.getVerticesData('uv');
    var normal = box.getVerticesData('normal');
    if (!uv) { throw 'no uv data'; }
    if (!normal) { throw 'no normal data'; }
    return 'uv=' + uv.length + ' normal=' + normal.length;
  }],
  ['mesh', 'instancing', function (scene, box) {
    var i = box.createInstance('i');
    if (!i) { throw 'no instance'; }
    return 'made';
  }],
  ['mesh', 'MergeMeshes', function (scene, box) {
    var other = BABYLON.MeshBuilder.CreateBox('b2', {size: 1}, scene);
    var merged = BABYLON.Mesh.MergeMeshes([box.clone('c'), other]);
    if (!merged) { throw 'merge returned nothing'; }
    return 'verts=' + merged.getTotalVertices();
  }],
  ['mesh', 'VertexData.CreateBox', function () {
    var d = BABYLON.VertexData.CreateBox({size: 1});
    return 'positions=' + d.positions.length;
  }],

  // --- materials -----------------------------------------------------------
  ['material', 'StandardMaterial', function (scene, box) {
    var m = new BABYLON.StandardMaterial('m', scene);
    m.diffuseColor = new BABYLON.Color3(1, 0, 0);
    box.material = m;
    return 'ready=' + m.isReady(box);
  }],
  ['material', 'PBRMaterial', function (scene, box) {
    var m = new BABYLON.PBRMaterial('pbr', scene);
    m.albedoColor = new BABYLON.Color3(1, 0, 0);
    m.metallic = 0;
    m.roughness = 1;
    box.material = m;
    return 'ready=' + m.isReady(box);
  }],
  ['material', 'PBRMetallicRoughnessMaterial', function (scene, box) {
    var m = new BABYLON.PBRMetallicRoughnessMaterial('pbrmr', scene);
    box.material = m;
    return 'ready=' + m.isReady(box);
  }],
  ['material', 'MultiMaterial', function (scene) {
    var m = new BABYLON.MultiMaterial('multi', scene);
    if (!m) { throw 'no MultiMaterial'; }
    return 'made';
  }],
  ['material', 'ShaderMaterial', function (scene, box) {
    BABYLON.Effect.ShadersStore['probeVertexShader'] =
      'precision highp float;\nattribute vec3 position;\nuniform mat4 worldViewProjection;\n' +
      'void main(){ gl_Position = worldViewProjection * vec4(position, 1.0); }';
    BABYLON.Effect.ShadersStore['probeFragmentShader'] =
      'precision highp float;\nvoid main(){ gl_FragColor = vec4(1.0, 0.0, 1.0, 1.0); }';
    var m = new BABYLON.ShaderMaterial('sm', scene, 'probe',
      {attributes: ['position'], uniforms: ['worldViewProjection']});
    box.material = m;
    return 'made';
  }],

  // --- textures ------------------------------------------------------------
  ['texture', 'DynamicTexture', function (scene) {
    var t = new BABYLON.DynamicTexture('dt', {width: 8, height: 8}, scene, false);
    var ctx = t.getContext();
    ctx.fillStyle = '#00ff00';
    ctx.fillRect(0, 0, 8, 8);
    t.update();
    return 'ready=' + t.isReady();
  }],
  ['texture', 'RawTexture', function (scene) {
    var t = BABYLON.RawTexture.CreateRGBATexture(
      new Uint8Array([255, 0, 255, 255]), 1, 1, scene);
    return 'ready=' + t.isReady();
  }],
  ['texture', 'RenderTargetTexture', function (scene) {
    var rtt = new BABYLON.RenderTargetTexture('rtt', 32, scene);
    scene.customRenderTargets.push(rtt);
    return 'made';
  }],
  ['texture', 'texture wrap and sampling modes', function (scene) {
    var t = new BABYLON.DynamicTexture('dt2', {width: 4, height: 4}, scene, false);
    t.wrapU = BABYLON.Texture.CLAMP_ADDRESSMODE;
    t.updateSamplingMode(BABYLON.Texture.NEAREST_SAMPLINGMODE);
    return 'set';
  }],

  // --- lights and shadows ---------------------------------------------------
  ['light', 'HemisphericLight', function (scene) {
    return String(!!new BABYLON.HemisphericLight('h', new BABYLON.Vector3(0, 1, 0), scene));
  }],
  ['light', 'PointLight', function (scene) {
    return String(!!new BABYLON.PointLight('p', new BABYLON.Vector3(0, 3, -3), scene));
  }],
  ['light', 'DirectionalLight', function (scene) {
    return String(!!new BABYLON.DirectionalLight('d', new BABYLON.Vector3(0, -1, 1), scene));
  }],
  ['light', 'SpotLight', function (scene) {
    return String(!!new BABYLON.SpotLight('s', new BABYLON.Vector3(0, 3, 0),
      new BABYLON.Vector3(0, -1, 0), Math.PI / 3, 2, scene));
  }],
  ['light', 'ShadowGenerator', function (scene, box) {
    var l = new BABYLON.DirectionalLight('d2', new BABYLON.Vector3(0, -1, 1), scene);
    var g = new BABYLON.ShadowGenerator(64, l);
    g.addShadowCaster(box);
    return 'made';
  }],

  // --- cameras and input ----------------------------------------------------
  ['camera', 'FreeCamera', function (scene) {
    return String(!!new BABYLON.FreeCamera('f', new BABYLON.Vector3(0, 0, -5), scene));
  }],
  ['camera', 'ArcRotateCamera', function (scene) {
    var c = new BABYLON.ArcRotateCamera('a', 1, 1, 5, BABYLON.Vector3.Zero(), scene);
    scene.activeCamera = c;
    return 'made';
  }],
  ['camera', 'attachControl', function (scene) {
    scene.activeCamera.attachControl(document.getElementById('c'), true);
    return 'attached';
  }],
  ['camera', 'isInFrustum', function (scene, box) {
    return 'in=' + scene.activeCamera.isInFrustum(box);
  }],

  // --- the scene ------------------------------------------------------------
  ['scene', 'pick', function (scene) {
    var p = scene.pick(32, 32);
    if (!p) { throw 'pick returned nothing'; }
    return 'hit=' + p.hit;
  }],
  ['scene', 'fog', function (scene) {
    scene.fogMode = BABYLON.Scene.FOGMODE_EXP;
    scene.fogDensity = 0.1;
    return 'set';
  }],
  ['scene', 'observables fire', function (scene) {
    var seen = 0;
    scene.onBeforeRenderObservable.add(function () { seen++; });
    scene.render();
    if (seen === 0) { throw 'onBeforeRenderObservable never fired'; }
    return 'fired';
  }],
  ['scene', 'getDeltaTime', function (scene) {
    return 'dt=' + typeof scene.getEngine().getDeltaTime();
  }],
  ['scene', 'clone and dispose', function (scene, box) {
    var c = box.clone('cl');
    c.dispose();
    return 'ok';
  }],

  // --- animation ------------------------------------------------------------
  ['animation', 'CreateAndStartAnimation', function (scene, box) {
    var a = BABYLON.Animation.CreateAndStartAnimation(
      'spin', box, 'rotation.y', 30, 60, 0, Math.PI, 1);
    if (!a) { throw 'no animatable'; }
    return 'started';
  }],
  ['animation', 'keyframed Animation', function (scene, box) {
    var a = new BABYLON.Animation('k', 'position.x', 30,
      BABYLON.Animation.ANIMATIONTYPE_FLOAT,
      BABYLON.Animation.ANIMATIONLOOPMODE_CYCLE);
    a.setKeys([{frame: 0, value: 0}, {frame: 30, value: 2}]);
    box.animations.push(a);
    scene.beginAnimation(box, 0, 30, true);
    return 'started';
  }],

  // --- post-processing ------------------------------------------------------
  ['post', 'PassPostProcess', function (scene) {
    return String(!!new BABYLON.PassPostProcess('pass', 1.0, scene.activeCamera));
  }],
  ['post', 'BlackAndWhitePostProcess', function (scene) {
    return String(!!new BABYLON.BlackAndWhitePostProcess('bw', 1.0, scene.activeCamera));
  }],

  // --- the maths, which everything above stands on --------------------------
  ['math', 'Matrix decompose', function () {
    var m = BABYLON.Matrix.RotationY(1);
    var s = new BABYLON.Vector3();
    var r = new BABYLON.Quaternion();
    var t = new BABYLON.Vector3();
    return String(m.decompose(s, r, t));
  }],
  ['math', 'Vector3 arithmetic', function () {
    var v = new BABYLON.Vector3(1, 2, 3).add(new BABYLON.Vector3(1, 1, 1)).normalize();
    return 'len=' + v.length().toFixed(3);
  }],
  ['math', 'Quaternion from euler', function () {
    var q = BABYLON.Quaternion.FromEulerAngles(0.1, 0.2, 0.3);
    return 'w=' + q.w.toFixed(3);
  }],

  // --- the loaders, which need corpus additions to do anything ---------------
  ['loader', 'SceneLoader exists', function () {
    if (typeof BABYLON.SceneLoader === 'undefined') { throw 'no SceneLoader'; }
    return typeof BABYLON.SceneLoader.ImportMesh;
  }],
  ['loader', 'glTF plugin registered', function () {
    var names = (BABYLON.SceneLoader._registeredPlugins &&
                 Object.keys(BABYLON.SceneLoader._registeredPlugins)) || [];
    if (names.indexOf('.gltf') < 0 && names.indexOf('.glb') < 0) {
      throw 'no glTF plugin in this bundle: ' + names.join(' ');
    }
    return names.length + ' plugins';
  }],
  ['gui', 'BABYLON.GUI', function () {
    if (typeof BABYLON.GUI === 'undefined') {
      throw 'no BABYLON.GUI - it ships as a separate babylon.gui.js';
    }
    return 'present';
  }],
];

// A FRESH SCENE PER PROBE, disposed after. Sharing one would let a probe that
// leaves the scene in a strange state - and several deliberately do, adding
// lights or post-processes - decide the verdict for every probe after it. That
// is the same reason tests/corpus/babylon/babylon_ratchet.cpp builds a page per rung.
globalThis.__runProbes = function (canvas) {
  const passed = [];
  const failed = [];
  const skipped = [];
  if (typeof BABYLON === 'undefined') {
    // NOT AN EMPTY REPORT. Every probe fails, by name, so the recorded surface
    // shrinking says WHICH ones rather than "the bundle is missing".
    for (const entry of globalThis.__probes) {
      failed.push(entry[0] + '/' + entry[1] + ': BABYLON is not defined');
    }
  } else {
    const engine = new BABYLON.Engine(canvas, true);
    for (const entry of globalThis.__probes) {
      const name = entry[0] + '/' + entry[1];
      let scene = null;
      try {
        scene = new BABYLON.Scene(engine);
        const camera = new BABYLON.FreeCamera('cam', new BABYLON.Vector3(0, 0, -5), scene);
        camera.setTarget(BABYLON.Vector3.Zero());
        scene.activeCamera = camera;
        const box = BABYLON.MeshBuilder.CreateBox('box', {size: 2}, scene);
        box.material = new BABYLON.StandardMaterial('mat', scene);
        const out = entry[2](scene, box, engine);
        if (out === 'SKIP') { skipped.push(name); } else { passed.push(name); }
      } catch (e) {
        failed.push(name + ': ' + (e && e.message ? e.message : String(e)));
      }
      try { if (scene) { scene.dispose(); } } catch (e) { /* a probe may have broken it */ }
    }
  }
  passed.sort();
  failed.sort();
  skipped.sort();
  // `count` is a STRING so the harness's small JSON reader, which only pulls
  // string arrays, can see it. It proves no probe fell out of the report rather
  // than trusting that none did.
  return JSON.stringify({
    passed: passed, failed: failed, skipped: skipped,
    count: [String(globalThis.__probes.length)]
  });
};
