// What of WebGL 2 actually works.
//
// tests/corpus/webgl2/webgl2_ratchet.cpp asks how FAR a WebGL 2 page gets - one number up a
// ladder. This asks the much broader and duller question: does each entry point
// DO something, rather than throw or answer garbage.
//
// IT EXISTS BECAUSE ONE NUMBER HIDES A LOT, and this engine has the receipt:
// `getProgramParameter` answered 0 to ACTIVE_UNIFORMS for as long as WebGL
// existed here and every corpus page still rendered, because hand-written pages
// ask for uniforms by name and only a library enumerates. The p5 probe found
// five real bugs in one run.
//
// THE SHAPE OF A PROBE. `[module, name, body]`. The body is handed a context
// and returns whatever it wants; a THROW is recorded as a failure and does not
// stop the rest. Returning 'SKIP' records it as not applicable.
//
// A PROBE ASSERTS, IT DOES NOT MERELY CALL. `gl.createVertexArray()` "passes"
// in an engine that returns undefined for everything; binding it and reading
// VERTEX_ARRAY_BINDING back does not.
//
// WHAT IS DELIBERATELY PROBED AND EXPECTED TO FAIL. docs/history/webgl2.md puts
// uniform buffer objects, 3D textures, MRT, samplers, queries, sync and
// transform feedback OUT of scope - and Babylon.js calls every one of them. A
// probe for each is here anyway, because "not implemented" is a fact worth
// recording and worth being told about deliberately rather than discovering as
// a wrong answer. They are expected to fail until the plan says otherwise; the
// recorded surface in tests/corpus/webgl2/webgl2-api.txt is what says which.

// Calls `body` and insists it REFUSED: raised INVALID_OPERATION and threw
// nothing. Shared by every out-of-scope probe, because `typeof x === 'function'`
// on its own passes against a stub that silently does nothing - which is the
// failure mode this whole file exists to catch.
function refuses(gl, body) {
  while (gl.getError() !== gl.NO_ERROR) { /* drain what came before */ }
  body();
  var err = gl.getError();
  if (err !== gl.INVALID_OPERATION) {
    throw 'did not refuse: getError said ' + err + ', wanted INVALID_OPERATION';
  }
  return 'refuses';
}

globalThis.__probes = [
  // --- the context itself -------------------------------------------------
  ['context', 'getContext webgl2', function (gl) {
    if (!gl) { throw 'no context'; }
    return 'ok';
  }],
  ['context', 'is a WebGL2RenderingContext', function (gl) {
    // A page checks this to decide which path to take - Phaser does exactly
    // that with `gl instanceof WebGLRenderingContext`.
    if (typeof WebGL2RenderingContext === 'undefined') { throw 'no WebGL2RenderingContext'; }
    if (!(gl instanceof WebGL2RenderingContext)) { throw 'the context is not one'; }
    return 'ok';
  }],
  ['context', 'VERSION says 2', function (gl) {
    var v = String(gl.getParameter(gl.VERSION));
    if (v.indexOf('2.0') < 0) { throw 'VERSION is ' + v; }
    return v;
  }],
  ['context', 'SHADING_LANGUAGE_VERSION says 3.00', function (gl) {
    var v = String(gl.getParameter(gl.SHADING_LANGUAGE_VERSION));
    if (v.indexOf('3.00') < 0) { throw 'SHADING_LANGUAGE_VERSION is ' + v; }
    return v;
  }],

  // --- constants ----------------------------------------------------------
  // Dull, and the reason they are here: a constant arriving as `undefined`
  // makes every comparison against it silently false, so a page takes a path
  // nobody intended and nothing reports an error.
  ['constants', 'texture formats', function (gl) {
    var want = ['RGBA8', 'RGB8', 'SRGB8_ALPHA8', 'R8', 'RG8', 'RGBA16F', 'RGBA32F',
                'DEPTH_COMPONENT24', 'DEPTH24_STENCIL8'];
    var missing = want.filter(function (k) { return gl[k] === undefined; });
    if (missing.length) { throw missing.join(','); }
    return 'ok';
  }],
  ['constants', 'targets and bindings', function (gl) {
    var want = ['TEXTURE_3D', 'TEXTURE_2D_ARRAY', 'UNIFORM_BUFFER', 'COPY_READ_BUFFER',
                'VERTEX_ARRAY_BINDING', 'TRANSFORM_FEEDBACK', 'PIXEL_PACK_BUFFER'];
    var missing = want.filter(function (k) { return gl[k] === undefined; });
    if (missing.length) { throw missing.join(','); }
    return 'ok';
  }],
  ['constants', 'draw buffers', function (gl) {
    var want = ['COLOR_ATTACHMENT1', 'COLOR_ATTACHMENT7', 'MAX_DRAW_BUFFERS',
                'MAX_COLOR_ATTACHMENTS', 'DRAW_BUFFER0'];
    var missing = want.filter(function (k) { return gl[k] === undefined; });
    if (missing.length) { throw missing.join(','); }
    return 'ok';
  }],

  // --- vertex array objects -----------------------------------------------
  ['vao', 'create and bind', function (gl) {
    var a = gl.createVertexArray();
    if (!a) { throw 'createVertexArray gave nothing'; }
    gl.bindVertexArray(a);
    var bound = gl.getParameter(gl.VERTEX_ARRAY_BINDING);
    gl.bindVertexArray(null);
    var unbound = gl.getParameter(gl.VERTEX_ARRAY_BINDING);
    gl.deleteVertexArray(a);
    if (!bound) { throw 'VERTEX_ARRAY_BINDING empty while one was bound'; }
    if (unbound) { throw 'unbinding left one bound'; }
    return 'ok';
  }],
  ['vao', 'isVertexArray', function (gl) {
    var a = gl.createVertexArray();
    if (gl.isVertexArray(a) !== true) { throw 'isVertexArray says no about one it made'; }
    gl.deleteVertexArray(a);
    if (gl.isVertexArray(a) !== false) { throw 'isVertexArray says yes about a deleted one'; }
    return 'ok';
  }],
  ['vao', 'captures attribute state', function (gl) {
    // THE POINT OF A VAO, and the part a stub gets wrong: binding one must
    // restore the enables and pointers that were set while it was bound.
    var a = gl.createVertexArray();
    gl.bindVertexArray(a);
    gl.enableVertexAttribArray(0);
    gl.bindVertexArray(null);
    var loose = gl.getVertexAttrib(0, gl.VERTEX_ATTRIB_ARRAY_ENABLED);
    gl.bindVertexArray(a);
    var captured = gl.getVertexAttrib(0, gl.VERTEX_ATTRIB_ARRAY_ENABLED);
    gl.bindVertexArray(null);
    gl.deleteVertexArray(a);
    if (captured !== true) { throw 'the enable did not survive rebinding'; }
    if (loose === true) { throw 'the enable leaked to the default vertex array'; }
    return 'ok';
  }],

  // --- instancing ---------------------------------------------------------
  ['instancing', 'vertexAttribDivisor', function (gl) {
    if (typeof gl.vertexAttribDivisor !== 'function') { throw 'not a function'; }
    gl.vertexAttribDivisor(0, 1);
    var d = gl.getVertexAttrib(0, gl.VERTEX_ATTRIB_ARRAY_DIVISOR);
    gl.vertexAttribDivisor(0, 0);
    if (d !== 1) { throw 'the divisor read back as ' + d; }
    return 'ok';
  }],
  ['instancing', 'drawArraysInstanced', function (gl) {
    if (typeof gl.drawArraysInstanced !== 'function') { throw 'not a function'; }
    return 'ok';
  }],
  ['instancing', 'drawElementsInstanced', function (gl) {
    if (typeof gl.drawElementsInstanced !== 'function') { throw 'not a function'; }
    return 'ok';
  }],

  // --- GLSL ES 3.00 -------------------------------------------------------
  ['glsl300', 'compiles in/out', function (gl) {
    var vs = gl.createShader(gl.VERTEX_SHADER);
    gl.shaderSource(vs, '#version 300 es\nin vec2 p;\nout vec2 uv;\n' +
                        'void main(){ uv = p; gl_Position = vec4(p,0.,1.); }');
    gl.compileShader(vs);
    if (!gl.getShaderParameter(vs, gl.COMPILE_STATUS)) { throw gl.getShaderInfoLog(vs); }
    return 'ok';
  }],
  ['glsl300', 'declared fragment output', function (gl) {
    var fs = gl.createShader(gl.FRAGMENT_SHADER);
    gl.shaderSource(fs, '#version 300 es\nprecision mediump float;\n' +
                        'out vec4 colour;\nvoid main(){ colour = vec4(1.); }');
    gl.compileShader(fs);
    if (!gl.getShaderParameter(fs, gl.COMPILE_STATUS)) { throw gl.getShaderInfoLog(fs); }
    return 'ok';
  }],
  ['glsl300', 'texture() not texture2D()', function (gl) {
    var fs = gl.createShader(gl.FRAGMENT_SHADER);
    gl.shaderSource(fs, '#version 300 es\nprecision mediump float;\nuniform sampler2D s;\n' +
                        'in vec2 uv;\nout vec4 colour;\nvoid main(){ colour = texture(s, uv); }');
    gl.compileShader(fs);
    if (!gl.getShaderParameter(fs, gl.COMPILE_STATUS)) { throw gl.getShaderInfoLog(fs); }
    return 'ok';
  }],
  ['glsl300', 'ES 1.00 still compiles', function (gl) {
    // THE LENIENCY CONTRACT: a WebGL 2 context must still take an ES 1.00
    // shader, because pages ship both and browsers accept both.
    var fs = gl.createShader(gl.FRAGMENT_SHADER);
    gl.shaderSource(fs, 'precision mediump float;\nvoid main(){ gl_FragColor = vec4(1.); }');
    gl.compileShader(fs);
    if (!gl.getShaderParameter(fs, gl.COMPILE_STATUS)) { throw gl.getShaderInfoLog(fs); }
    return 'ok';
  }],

  // --- the WebGL 1 extension spelling of the same thing --------------------
  // ONE IMPLEMENTATION, TWO NAMES is the decision in docs/history/webgl2.md, and
  // these probes are what make it checkable. Phaser reaches VAOs and instancing
  // ONLY through here.
  ['extensions', 'getSupportedExtensions lists them', function (gl) {
    var c = document.createElement('canvas');
    var gl1 = c.getContext('webgl');
    if (!gl1) { throw 'no webgl1 context'; }
    var names = gl1.getSupportedExtensions() || [];
    var want = ['OES_vertex_array_object', 'ANGLE_instanced_arrays', 'OES_standard_derivatives'];
    var absent = want.filter(function (n) { return names.indexOf(n) < 0; });
    if (absent.length) { throw 'not advertised: ' + absent.join(','); }
    return 'ok';
  }],
  ['extensions', 'OES_vertex_array_object', function (gl) {
    var gl1 = document.createElement('canvas').getContext('webgl');
    var ext = gl1.getExtension('OES_vertex_array_object');
    if (!ext) { throw 'getExtension returned null'; }
    var a = ext.createVertexArrayOES();
    if (!a) { throw 'createVertexArrayOES gave nothing'; }
    ext.bindVertexArrayOES(a);
    ext.bindVertexArrayOES(null);
    ext.deleteVertexArrayOES(a);
    return 'ok';
  }],
  ['extensions', 'ANGLE_instanced_arrays', function (gl) {
    var gl1 = document.createElement('canvas').getContext('webgl');
    var ext = gl1.getExtension('ANGLE_instanced_arrays');
    if (!ext) { throw 'getExtension returned null'; }
    if (typeof ext.drawArraysInstancedANGLE !== 'function') { throw 'no drawArraysInstancedANGLE'; }
    if (typeof ext.vertexAttribDivisorANGLE !== 'function') { throw 'no vertexAttribDivisorANGLE'; }
    return 'ok';
  }],
  ['extensions', 'unimplemented ones stay null', function (gl) {
    // A driver without an extension returns null, and that is what a page
    // checks for. Handing back an object would make a page take a path this
    // cannot honour - which is worse than not having it.
    var gl1 = document.createElement('canvas').getContext('webgl');
    var made_up = gl1.getExtension('EXT_disjoint_timer_query');
    if (made_up !== null) { throw 'an unimplemented extension answered non-null'; }
    return 'ok';
  }],

  // --- OUT OF SCOPE, and REFUSING BY NAME ---------------------------------
  // Babylon.js calls every one of these. Stage 5 of docs/history/webgl2.md: they
  // are PRESENT on a WebGL 2 context, because a page that got one calls them
  // rather than feature-detecting them, and an absent method is a TypeError
  // that says nothing about why. Each raises INVALID_OPERATION and names itself
  // in the console.
  //
  // So these probes assert the refusal, not merely the presence - `typeof x ===
  // 'function'` alone would pass against a stub that silently did nothing,
  // which is the exact failure this whole plan exists to avoid.
  ['unscoped', 'refusing is loud', function (gl) {
    while (gl.getError() !== gl.NO_ERROR) { /* drain */ }
    gl.createQuery();
    var err = gl.getError();
    if (err !== gl.INVALID_OPERATION) {
      throw 'createQuery did not raise INVALID_OPERATION, it raised ' + err;
    }
    return 'ok';
  }],
  // UNIFORM BUFFER OBJECTS ARE IN SCOPE NOW, and this probe changed sides: it
  // used to assert they REFUSED. They are how WebGL 2 delivers uniforms and how
  // Babylon delivers all of them, so "out of scope" meant Babylon's every matrix
  // read as zero while its shaders linked and its draws were issued.
  //
  // A REAL BLOCK, LINKED AND ASKED ABOUT, not just `typeof`: a stub returning a
  // plausible number passes a shallow check and is exactly what this file exists
  // to catch.
  ['blocks', 'uniform buffer objects', function (gl) {
    if (typeof gl.bindBufferBase !== 'function') { throw 'no bindBufferBase'; }
    if (typeof gl.getUniformBlockIndex !== 'function') { throw 'no getUniformBlockIndex'; }
    var vs = gl.createShader(gl.VERTEX_SHADER);
    gl.shaderSource(vs, '#version 300 es\n' +
      'layout(std140) uniform Tint { vec4 colour; };\n' +
      'in vec2 at;\n' +
      'void main() { gl_Position = vec4(at, 0.0, 1.0) + colour * 0.0; }\n');
    gl.compileShader(vs);
    if (!gl.getShaderParameter(vs, gl.COMPILE_STATUS)) {
      throw 'a uniform block did not compile: ' + gl.getShaderInfoLog(vs);
    }
    var fs = gl.createShader(gl.FRAGMENT_SHADER);
    gl.shaderSource(fs, '#version 300 es\nprecision highp float;\n' +
      'out vec4 fragColour;\nvoid main() { fragColour = vec4(1.0); }\n');
    gl.compileShader(fs);
    var p = gl.createProgram();
    gl.attachShader(p, vs);
    gl.attachShader(p, fs);
    gl.linkProgram(p);
    if (!gl.getProgramParameter(p, gl.LINK_STATUS)) {
      throw 'link failed: ' + gl.getProgramInfoLog(p);
    }
    var index = gl.getUniformBlockIndex(p, 'Tint');
    if (index !== 0) { throw 'getUniformBlockIndex said ' + index; }
    // AND A NAME NO BLOCK HAS is INVALID_INDEX rather than an error - a page
    // asks about blocks its shader may have dropped, and Babylon does.
    var missing = gl.getUniformBlockIndex(p, 'NotABlock');
    if (missing !== 0xFFFFFFFF) { throw 'an unknown block said ' + missing; }
    while (gl.getError() !== gl.NO_ERROR) { /* drain */ }
    gl.uniformBlockBinding(p, index, 1);
    var buffer = gl.createBuffer();
    gl.bindBuffer(gl.UNIFORM_BUFFER, buffer);
    gl.bufferData(gl.UNIFORM_BUFFER, new Float32Array([1, 0, 0, 1]), gl.DYNAMIC_DRAW);
    gl.bindBufferBase(gl.UNIFORM_BUFFER, 1, buffer);
    var err = gl.getError();
    if (err !== gl.NO_ERROR) { throw 'binding a block raised ' + err; }
    return 'works';
  }],
  ['unscoped', '3D textures', function (gl) {
    if (typeof gl.texImage3D !== 'function') { throw 'no texImage3D'; }
    if (typeof gl.texStorage3D !== 'function') { throw 'no texStorage3D'; }
    return refuses(gl, function () { gl.texImage3D(0, 0, 0, 0, 0, 0, 0, 0, 0, null); });
  }],
  ['unscoped', 'multiple render targets', function (gl) {
    if (typeof gl.drawBuffers !== 'function') { throw 'no drawBuffers'; }
    return refuses(gl, function () { gl.drawBuffers([]); });
  }],
  ['unscoped', 'sampler objects', function (gl) {
    if (typeof gl.createSampler !== 'function') { throw 'no createSampler'; }
    return refuses(gl, function () { gl.createSampler(); });
  }],
  ['unscoped', 'query objects', function (gl) {
    if (typeof gl.createQuery !== 'function') { throw 'no createQuery'; }
    return refuses(gl, function () { gl.createQuery(); });
  }],
  ['unscoped', 'sync objects', function (gl) {
    if (typeof gl.fenceSync !== 'function') { throw 'no fenceSync'; }
    return refuses(gl, function () { gl.fenceSync(0, 0); });
  }],
  ['unscoped', 'transform feedback', function (gl) {
    if (typeof gl.createTransformFeedback !== 'function') { throw 'no createTransformFeedback'; }
    return refuses(gl, function () { gl.createTransformFeedback(); });
  }],
  ['unscoped', 'blitFramebuffer', function (gl) {
    if (typeof gl.blitFramebuffer !== 'function') { throw 'no blitFramebuffer'; }
    return 'ok';
  }],
  ['unscoped', 'texStorage2D', function (gl) {
    if (typeof gl.texStorage2D !== 'function') { throw 'no texStorage2D'; }
    return refuses(gl, function () { gl.texStorage2D(0, 0, 0, 0, 0); });
  }]
];

globalThis.__runProbes = function (gl) {
  const passed = [];
  const failed = [];
  const skipped = [];
  for (const entry of globalThis.__probes) {
    const name = entry[0] + '/' + entry[1];
    try {
      const out = entry[2](gl);
      if (out === 'SKIP') { skipped.push(name); } else { passed.push(name); }
    } catch (e) {
      failed.push(name + ': ' + (e && e.message ? e.message : String(e)));
    }
  }
  passed.sort();
  failed.sort();
  skipped.sort();
  // `count` is a STRING so the harness's small JSON reader, which only pulls
  // string arrays, can see it. It exists so the C++ side can prove no probe
  // fell out of the report rather than trusting that none did - the p5 harness
  // lost five failures to a bracket in a message before that check existed.
  return JSON.stringify({
    passed: passed, failed: failed, skipped: skipped,
    count: [String(globalThis.__probes.length)]
  });
};
