globalThis.__probes.push(
    // --- saving -------------------------------------------------------------
    //
    // Every p5 save() ends in downloadFile: a Blob, an object URL, an <a href
    // download> built entirely from script, and click(). A probe can only see that
    // it did not throw - whether a file appeared is unittests/unit/image_basics.cpp's
    // question, because only the embedder can see the disk.
    ['save', 'saveCanvas', function (s) {
        s.saveCanvas('probe-out', 'png');
        return 'ok';
    }],
    ['save', 'saveJSON', function (s) {
        s.saveJSON({
            a: 1
        }, 'probe.json');
        return 'ok';
    }],
    ['save', 'saveStrings', function (s) {
        s.saveStrings(['a', 'b'], 'probe.txt');
        return 'ok';
    }],
    ['save', 'downloadFile', function (s) {
        if (typeof s.downloadFile !== 'function') {
            throw 'absent';
        }
        s.downloadFile('some text', 'probe-direct', 'txt');
        return 'ok';
    }],
    ['save', 'canvas.toDataURL', function (s) {
        var url = s.canvas.toDataURL();
        if (url.indexOf('data:image/png;base64,') !== 0) {
            throw 'url=' + url.slice(0, 40);
        }
        if (url.length < 100) {
            throw 'too short: ' + url.length;
        }
        return 'ok';
    }],

    // --- 3D (out of scope; the constructors must still exist) ---------------
    ['webgl', 'WEBGL renderer is constructible', function (s) {
        if (typeof s.constructor.renderers['webgl'] !== 'function') {
            throw 'not a function';
        }
        return 'ok';
    }],
    // THE ENGINE'S GUARANTEE, asked of the engine rather than of p5: a canvas
    // hands out a REAL webgl context now - compile, link, buffer, draw - and
    // answers null for `webgl2`, which is what lets p5's fallback reach the one
    // that works.
    ['webgl', 'getContext(webgl) works', function (s) {
        const gl = document.createElement('canvas').getContext('webgl');
        if (gl === null) {
            throw 'no context';
        }
        for (const name of ['createShader', 'compileShader', 'linkProgram', 'bufferData',
                'vertexAttribPointer', 'drawArrays', 'readPixels'
            ]) {
            if (typeof gl[name] !== 'function') {
                throw name + ' is missing';
            }
        }
        if (gl.TRIANGLES !== 4) {
            throw 'TRIANGLES=' + gl.TRIANGLES;
        }
        // webgl2 IS implemented since 2026-08-02, and this assertion has now been
        // right in three different forms, which is worth keeping the history of:
        //
        //   1. it asserted a THROW, on the theory that a null was the
        //      silent-wrong-answer shape;
        //   2. it asserted NULL, because the specification returns null for an
        //      unsupported context id and p5's RendererGL is written as
        //      `getContext('webgl2') || getContext('webgl')` - the throw escaped
        //      the constructor and left the sketch on Renderer2D, which is exactly
        //      what throwing was meant to prevent;
        //   3. it asserts a CONTEXT, because there is one - docs/history/webgl2.md
        //      stage 4 - and p5 now takes that path first.
        //
        // Replaced rather than deleted at each step: what is asserted changed,
        // the fact that SOMETHING is asserted did not.
        const two = document.createElement('canvas').getContext('webgl2');
        if (two === null) {
            throw 'webgl2 should be a context now';
        }
        if (two.RGBA8 === undefined) {
            throw 'a webgl2 context without the WebGL 2 constants';
        }
        return 'ok';
    }],
    // THIS PROBE FAILED FOR TWO SEPARATE REASONS BEFORE IT PASSED, and staying
    // measured through both is the only reason either was found.
    //
    // First there was no webgl context at all. Then there was one and p5 still
    // chose Renderer2D - which read like a fault in p5's renderer selection, and
    // was not. Selection was fine: `getContext('webgl2')` THREW instead of
    // returning null, so p5's `webgl2 || webgl` fallback never ran, and then
    // `Float32Array.from` was missing, so the constructor died. Two engine bugs,
    // three layers from what the probe could see.
    ['webgl', 'createCanvas(WEBGL) uses the webgl renderer', function (s) {
        s.createCanvas(20, 20, s.WEBGL);
        const name = s._renderer && s._renderer.constructor && s._renderer.constructor.name;
        if (name === 'Renderer2D') {
            throw 'p5 selected Renderer2D despite a real webgl context being available';
        }
        // THE DRAWING BUFFER MUST BE THE CANVAS'S SIZE, which is a separate bug from
        // selecting the renderer and was hidden behind it. p5 creates the canvas,
        // asks for a context, and only THEN sets width and height - so a context
        // that read the size once was left at the 300x150 HTML default, and a
        // readback of the sketch's own 20x20 window found nothing at all.
        const gl = s._renderer.drawingContext;
        // THE DRAWING BUFFER MUST BE THE CANVAS'S SIZE, which is a separate bug from
        // selecting the renderer and was hidden behind it. p5 creates the canvas,
        // asks for a context, and only THEN sets width and height - so a context
        // that read the size once was left at the 300x150 HTML default, and a
        // readback of the sketch's own 20x20 window found nothing.
        if (gl.drawingBufferWidth !== 20 || gl.drawingBufferHeight !== 20) {
            throw 'drawing buffer is ' + gl.drawingBufferWidth + 'x' + gl.drawingBufferHeight +
                ', not the canvas 20x20';
        }
        return 'ok';
    }],
    // AND IT ACTUALLY DRAWS, which is a different question from every one above
    // and the only one a sketch cares about. Three engine bugs sat between
    // "p5 picked the WebGL renderer" and "a cube appears", every one of them
    // silent: no GL error, no console warning, one correct-looking drawElements
    // of 36 indices, and a canvas with nothing on it but the background.
    //
    // The last was the worst. p5 dispatches uniforms with
    // `switch (uniform.type) { case gl.FLOAT_MAT4: ... }` - against the CONTEXT'S
    // OWN CONSTANTS - and this context did not define them, so `gl.FLOAT_MAT4`
    // was undefined, no case matched, there is no default, and every matrix was
    // dropped without a word.
    ['webgl', 'a WEBGL sketch draws geometry', function (s) {
        s.createCanvas(20, 20, s.WEBGL);
        s.background(0, 0, 255);
        s.noStroke();
        s.fill(255, 0, 0);
        s.box(8);
        const gl = s._renderer.drawingContext;
        const buf = new Uint8Array(20 * 20 * 4);
        gl.readPixels(0, 0, 20, 20, gl.RGBA, gl.UNSIGNED_BYTE, buf);
        // The centre is inside a box drawn at the origin; a corner is not. Both
        // halves matter - an all-red buffer would pass a "did anything draw" check
        // and mean the geometry covered everything.
        const at = (x, y) => buf[(y * 20 + x) * 4] + ',' + buf[(y * 20 + x) * 4 + 1] +
            ',' + buf[(y * 20 + x) * 4 + 2];
        if (at(10, 10) !== '255,0,0') {
            throw 'centre is ' + at(10, 10) + ', not the fill';
        }
        if (at(0, 0) !== '0,0,255') {
            throw 'corner is ' + at(0, 0) + ', not the background';
        }
        return 'ok';
    }],

    // --- WEBGL, the 3D surface ----------------------------------------------
    //
    // Every probe below draws and then READS THE CANVAS BACK, because in WEBGL
    // mode "it did not throw" is worth even less than usual: the whole reason
    // this module exists is that p5 drew a geometrically correct cube with no
    // vertices, threw nothing, and set no GL error.
    //
    // `drew` is the shared shape - clear to a known background, draw, and require
    // that SOMETHING is no longer the background. It deliberately does not care
    // what colour, because lighting and material probes each produce a different
    // one and pinning them here would be a golden in the wrong place.
    ...(function () {
        // THE BACKGROUND IS A COLOUR NO MATERIAL PRODUCES. It was pure blue, and
        // p5's normal material paints a face with its normal AS the colour - a
        // box seen head-on is one face with normal (0, 0, 1), which is (0, 0, 255):
        // exactly the background, so `normalMaterial` read as "nothing drawn" the
        // moment the engine computed normals correctly (it had passed on garbage
        // normals). (10, 20, 30) is not a unit vector and not a fill any probe uses.
        const drew = function (s, body) {
            s.createCanvas(24, 24, s.WEBGL);
            s.background(10, 20, 30);
            body(s);
            const gl = s._renderer.drawingContext;
            const buf = new Uint8Array(24 * 24 * 4);
            gl.readPixels(0, 0, 24, 24, gl.RGBA, gl.UNSIGNED_BYTE, buf);
            let other = 0;
            for (let i = 0; i < buf.length; i += 4) {
                if (buf[i] !== 10 || buf[i + 1] !== 20 || buf[i + 2] !== 30) {
                    other++;
                }
            }
            if (other === 0) {
                throw 'nothing was drawn - the canvas is all background' +
                    ' [shaderError=' + (gl.ctbrowserShaderError() || 'none') + ']';
            }
            if (other === 24 * 24) {
                throw 'everything was drawn - no background survives';
            }
            const err = gl.getError();
            if (err !== 0) {
                throw 'gl error ' + err;
            }
            return other + ' px';
        };
        const shape = function (name, body) {
            return ['webgl', name, function (s) {
                return drew(s, body);
            }];
        };
        return [
            shape('box', function (s) {
                s.noStroke();
                s.fill(255, 0, 0);
                s.box(10);
            }),
            shape('sphere', function (s) {
                s.noStroke();
                s.fill(255, 0, 0);
                s.sphere(8);
            }),
            shape('plane', function (s) {
                s.noStroke();
                s.fill(255, 0, 0);
                s.plane(12, 12);
            }),
            shape('cylinder', function (s) {
                s.noStroke();
                s.fill(255, 0, 0);
                s.cylinder(6, 12);
            }),
            shape('cone', function (s) {
                s.noStroke();
                s.fill(255, 0, 0);
                s.cone(6, 12);
            }),
            shape('torus', function (s) {
                s.noStroke();
                s.fill(255, 0, 0);
                s.torus(8, 3);
            }),
            shape('ellipsoid', function (s) {
                s.noStroke();
                s.fill(255, 0, 0);
                s.ellipsoid(6, 8, 5);
            }),
            shape('rotateX/Y/Z', function (s) {
                s.noStroke();
                s.fill(255, 0, 0);
                s.rotateX(0.4);
                s.rotateY(0.5);
                s.rotateZ(0.6);
                s.box(10);
            }),
            shape('translate/scale in 3D', function (s) {
                s.noStroke();
                s.fill(255, 0, 0);
                s.translate(2, -2, 0);
                s.scale(1.5);
                s.box(6);
            }),
            shape('push/pop keeps the matrix', function (s) {
                s.noStroke();
                s.fill(255, 0, 0);
                s.push();
                s.translate(60, 60, 0);
                s.box(6);
                s.pop();
                // The first box is translated far off-canvas, so anything visible is
                // the SECOND one - which only lands if pop() restored the matrix.
                s.box(10);
            }),
            shape('normalMaterial', function (s) {
                s.noStroke();
                s.normalMaterial();
                s.box(10);
            }),
            shape('ambientLight + ambientMaterial', function (s) {
                s.noStroke();
                s.ambientLight(200);
                s.ambientMaterial(255, 0, 0);
                s.box(10);
            }),
            shape('directionalLight', function (s) {
                s.noStroke();
                s.directionalLight(255, 255, 255, 0, 0, -1);
                s.fill(255, 0, 0);
                s.box(10);
            }),
            shape('pointLight', function (s) {
                s.noStroke();
                s.pointLight(255, 255, 255, 0, 0, 40);
                s.fill(255, 0, 0);
                s.box(10);
            }),
            shape('specularMaterial + shininess', function (s) {
                s.noStroke();
                s.directionalLight(255, 255, 255, 0, 0, -1);
                s.specularMaterial(255, 0, 0);
                s.shininess(20);
                s.sphere(8);
            }),
            shape('ortho', function (s) {
                s.ortho(-12, 12, -12, 12, 0, 500);
                s.noStroke();
                s.fill(255, 0, 0);
                s.box(10);
            }),
            shape('perspective', function (s) {
                s.perspective(Math.PI / 3, 1, 0.1, 500);
                s.noStroke();
                s.fill(255, 0, 0);
                s.box(10);
            }),
            shape('camera', function (s) {
                s.camera(0, 0, 40, 0, 0, 0, 0, 1, 0);
                s.noStroke();
                s.fill(255, 0, 0);
                s.box(10);
            }),
            shape('stroke in 3D', function (s) {
                s.stroke(255, 0, 0);
                s.strokeWeight(2);
                s.noFill();
                s.box(10);
            }),
            shape('createShader + shader()', function (s) {
                const sh = s.createShader(
                    'attribute vec3 aPosition;' +
                    'uniform mat4 uModelViewMatrix;' +
                    'uniform mat4 uProjectionMatrix;' +
                    'void main() {' +
                    '  gl_Position = uProjectionMatrix * uModelViewMatrix * vec4(aPosition, 1.0);' +
                    '}',
                    'precision mediump float;' +
                    'uniform vec3 uTint;' +
                    'void main() { gl_FragColor = vec4(uTint, 1.0); }');
                s.shader(sh);
                sh.setUniform('uTint', [1.0, 0.0, 0.0]);
                s.noStroke();
                s.box(10);
            }),
            shape('texture', function (s) {
                const g = s.createGraphics(8, 8);
                g.background(255, 0, 0);
                s.noStroke();
                s.texture(g);
                s.plane(12, 12);
            }),
        ];
    })(),
);
