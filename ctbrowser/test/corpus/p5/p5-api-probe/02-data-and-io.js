globalThis.__probes.push(
    // --- loading ------------------------------------------------------------
    //
    // These are ASYNC and the runner is not, so each returns its promise and the
    // runner awaits it. The assets are baked by test/corpus/p5/p5_api.cpp, so the whole
    // fetch-and-parse path runs without reaching the network.
    ['load', 'loadJSON', function (s) {
        return s.loadJSON('probe-data.json').then(function (j) {
            if (!j || j.name !== 'probe' || j.n !== 4) {
                throw 'parsed=' + JSON.stringify(j);
            }
            return 'ok';
        });
    }],
    ['load', 'loadStrings', function (s) {
        return s.loadStrings('probe-lines.txt').then(function (lines) {
            if (lines.join('/') !== 'one/two/three') {
                throw 'lines=' + JSON.stringify(lines);
            }
            return 'ok';
        });
    }],
    ['load', 'loadTable', function (s) {
        return s.loadTable('probe-table.csv').then(function (t) {
            if (!t || typeof t.getRowCount !== 'function') {
                throw 'not a Table: ' + typeof t;
            }
            if (t.getRowCount() < 2) {
                throw 'rows=' + t.getRowCount();
            }
            return 'ok';
        });
    }],
    // loadImage is the whole blob/object-URL/Image chain in one call: p5 fetches
    // the bytes, wraps them in a Blob, makes an object URL, points an Image at it,
    // REVOKES the URL inside onload and only then draws the image into its own
    // canvas. Every one of those has to work for this to return pixels.
    ['load', 'loadImage', function (s) {
        return s.loadImage('probe-image.bmp').then(function (img) {
            if (!img) {
                throw 'no image';
            }
            if (img.width !== 4 || img.height !== 4) {
                throw 'size=' + img.width + 'x' + img.height;
            }
            // The PIXELS, not just the size: a 4x4 image of the wrong colour is what
            // a decode that silently produced nothing looks like.
            img.loadPixels();
            var green = img.get(1, 1);
            if (green[1] < 200 || green[0] > 50) {
                throw 'pixel=' + green.join(',');
            }
            return 'ok';
        });
    }],
    ['load', 'image() draws a loaded image', function (s) {
        return s.loadImage('probe-image.bmp').then(function (img) {
            s.background(0);
            s.image(img, 0, 0);
            s.loadPixels();
            var at = 4 * (2 * s.width + 2);
            if (s.pixels[at + 1] < 200) {
                throw 'canvas pixel=' + s.pixels.slice(at, at + 4).join(',');
            }
            return 'ok';
        });
    }],
    ['load', 'loadFont', function (s) {
        return typeof s.loadFont === 'function' ? 'SKIP' : 'absent';
    }],

    // KNOWN FAILING, and here so it is measured rather than remembered.
    //
    // `tint()` needs globalCompositeOperation, which this engine ignores. p5
    // builds a tinted copy through five composited draws - luminosity, color,
    // multiply, destination-in - and with every mode treated as source-over the
    // `multiply` fillRect covers the whole canvas and the `destination-in` that
    // would restore the alpha does nothing. So a tinted image comes out as a solid
    // rectangle of the tint colour with the sprite on top of it.
    //
    // Found by comparing examples/pages/p5-image.html against Chrome pixel by
    // pixel: every other stage of that page agreed exactly, and this one differed
    // wherever the source was transparent.
    // blendMode() is SIXTEEN NAMES FOR globalCompositeOperation - p5's constants
    // are the CSS strings verbatim (DARKEST === 'darken'), so this probe is really
    // asking whether the operator reached the canvas and changed the answer.
    //
    // Each mode is checked against a value computed from the W3C formula, not
    // recorded: backdrop (128,64,32) opaque, source (64,192,96) opaque, so the
    // result is B(b, s) with no alpha weighting.
    ['image', 'blendMode', function (s) {
        const cases = [
            [s.BLEND, [64, 192, 96]],
            [s.MULTIPLY, [32, 48, 12]],
            [s.SCREEN, [160, 208, 116]],
            [s.DARKEST, [64, 64, 32]],
            [s.LIGHTEST, [128, 192, 96]],
            [s.DIFFERENCE, [64, 128, 64]],
            [s.EXCLUSION, [128, 160, 104]],
            [s.ADD, [192, 255, 128]], // lighter: b + s, clamped
        ];
        const wrong = [];
        for (const [mode, want] of cases) {
            s.blendMode(s.BLEND);
            s.background(128, 64, 32);
            s.blendMode(mode);
            s.noStroke();
            s.fill(64, 192, 96);
            s.rect(0, 0, s.width, s.height);
            s.blendMode(s.BLEND);
            const got = s.get(4, 4);
            if (got[0] !== want[0] || got[1] !== want[1] || got[2] !== want[2]) {
                wrong.push(mode + ' gave ' + got.slice(0, 3).join(',') + ' want ' + want.join(','));
            }
        }
        // The rest have to be ACCEPTED even where the value is not pinned here -
        // p5 sets them straight onto globalCompositeOperation, so an unknown one
        // would silently draw as source-over.
        for (const mode of [s.OVERLAY, s.HARD_LIGHT, s.SOFT_LIGHT, s.DODGE, s.BURN, s.REPLACE,
                s.REMOVE
            ]) {
            s.blendMode(mode);
            if (s.drawingContext.globalCompositeOperation !== mode) {
                wrong.push(mode + ' did not reach the context');
            }
        }
        s.blendMode(s.BLEND);
        if (wrong.length) {
            throw wrong.join(' | ');
        }
        return 'ok';
    }],
    ['image', 'tint preserves transparency', function (s) {
        s.background(255);
        const g = s.createGraphics(8, 8);
        g.clear();
        g.noStroke();
        g.fill(255);
        g.rect(0, 0, 4, 8); // the left half opaque, the right half transparent
        s.tint(255, 0, 0);
        s.image(g, 0, 0);
        s.noTint();
        s.loadPixels();
        // (6, 4) is under the transparent half, so it must still be the background.
        const at = 4 * (4 * s.width + 6);
        const there = [s.pixels[at], s.pixels[at + 1], s.pixels[at + 2]].join(',');
        if (there !== '255,255,255') {
            throw 'tint painted over transparency: ' + there;
        }
        return 'ok';
    }],

    // --- the unmeasured P2D surface -----------------------------------------
    //
    // tools/corpus/ratchet.py p5 api --coverage listed 190 functions no probe mentioned. Most are
    // WEBGL, audio, video or motion sensors and are out of scope by name. These are
    // the ones that are NOT: they run in P2D, headless, with no hardware - so
    // nothing excuses them being unmeasured, and unmeasured is the state every bug
    // found here was hiding in.

    ['math', 'trigonometry', function (s) {
        const wrong = [];
        const near = (n, got, want) => {
            if (Math.abs(got - want) > 1e-6) {
                wrong.push(n + '=' + got + ' want ' + want);
            }
        };
        near('acos', s.acos(1), 0);
        near('asin', s.asin(0), 0);
        near('atan', s.atan(1), Math.PI / 4);
        near('atan2', s.atan2(1, 1), Math.PI / 4);
        near('tan', s.tan(0), 0);
        near('radians', s.radians(180), Math.PI);
        near('degrees', s.degrees(Math.PI), 180);
        // fract is the fractional part, and 2.5 -> 0.5 is the whole of it.
        near('fract', s.fract(2.5), 0.5);
        // ANGLE MODE changes what every one of these means, which is the part a
        // sketch actually depends on.
        s.angleMode(s.DEGREES);
        near('acos in degrees', s.acos(0), 90);
        near('atan2 in degrees', s.atan2(1, 0), 90);
        s.angleMode(s.RADIANS);
        if (wrong.length) {
            throw wrong.join(' | ');
        }
        return 'ok';
    }],
    ['math', 'byte/char/unchar/code conversions', function (s) {
        const wrong = [];
        const is = (n, got, want) => {
            if (String(got) !== String(want)) {
                wrong.push(n + '=' + got + ' want ' + want);
            }
        };
        is('byte', s.byte(65), 65);
        is('byte wraps', s.byte(200), -56); // a signed 8-bit value, which is the point
        is('char', s.char(65), 'A');
        is('unchar', s.unchar('A'), 65);
        // NOT s.code(): in p5 2.x `code` is the keyboard system variable - the
        // KeyboardEvent.code of the last key - and it shadows the old function.
        if (typeof s.code !== 'string') {
            wrong.push('code is ' + typeof s.code + ', want a string');
        }
        if (wrong.length) {
            throw wrong.join(' | ');
        }
        return 'ok';
    }],

    ['env', 'time', function (s) {
        const wrong = [];
        // A clock, so the assertion is on the RANGE - the failure being guarded
        // against is a constant, which is what an unimplemented one returns.
        if (!(s.second() >= 0 && s.second() < 60)) {
            wrong.push('second=' + s.second());
        }
        if (!(s.minute() >= 0 && s.minute() < 60)) {
            wrong.push('minute=' + s.minute());
        }
        if (!(s.hour() >= 0 && s.hour() < 24)) {
            wrong.push('hour=' + s.hour());
        }
        if (!(s.day() >= 1 && s.day() <= 31)) {
            wrong.push('day=' + s.day());
        }
        if (!(s.month() >= 1 && s.month() <= 12)) {
            wrong.push('month=' + s.month());
        }
        if (!(s.year() > 2000)) {
            wrong.push('year=' + s.year());
        }
        if (typeof s.millis() !== 'number') {
            wrong.push('millis=' + typeof s.millis());
        }
        if (wrong.length) {
            throw wrong.join(' | ');
        }
        return 'ok';
    }],
    ['env', 'frame rate', function (s) {
        const wrong = [];
        if (typeof s.getFrameRate() !== 'number') {
            wrong.push('getFrameRate not a number');
        }
        s.frameRate(30);
        if (s.getTargetFrameRate() !== 30) {
            wrong.push('target=' + s.getTargetFrameRate());
        }
        if (typeof s.deltaTime !== 'number') {
            wrong.push('deltaTime=' + typeof s.deltaTime);
        }
        if (wrong.length) {
            throw wrong.join(' | ');
        }
        return 'ok';
    }],
    ['env', 'window and display metrics', function (s) {
        const wrong = [];
        for (const name of ['windowWidth', 'windowHeight', 'displayWidth', 'displayHeight']) {
            if (!(s[name] > 0)) {
                wrong.push(name + '=' + s[name]);
            }
        }
        if (!(s.displayDensity() > 0)) {
            wrong.push('displayDensity=' + s.displayDensity());
        }
        if (s.focused !== true) {
            wrong.push('focused=' + s.focused);
        }
        if (wrong.length) {
            throw wrong.join(' | ');
        }
        return 'ok';
    }],
    ['env', 'mouse and key state', function (s) {
        const wrong = [];
        for (const name of ['mouseX', 'mouseY', 'pmouseX', 'pmouseY', 'winMouseX', 'winMouseY',
                'pwinMouseX', 'pwinMouseY', 'movedX', 'movedY'
            ]) {
            if (typeof s[name] !== 'number') {
                wrong.push(name + '=' + typeof s[name]);
            }
        }
        if (typeof s.mouseButton !== 'object' && typeof s.mouseButton !== 'string') {
            wrong.push('mouseButton=' + typeof s.mouseButton);
        }
        if (typeof s.keyIsPressed !== 'boolean') {
            wrong.push('keyIsPressed=' + typeof s.keyIsPressed);
        }
        if (typeof s.key !== 'string') {
            wrong.push('key=' + typeof s.key);
        }
        if (typeof s.keyCode !== 'number' && s.keyCode !== undefined) {
            wrong.push('keyCode=' + typeof s.keyCode);
        }
        if (wrong.length) {
            throw wrong.join(' | ');
        }
        return 'ok';
    }],

    ['color', 'the CSS colour spaces p5 2.x added', function (s) {
        const wrong = [];
        // p5 2.x exposes HSL, HWB, LAB, LCH, OKLAB, OKLCH and RGBA as colorMode
        // arguments. A mode that is not understood is worse than an error: every
        // colour after it is wrong and the sketch still draws.
        // WHITE in each mode, which is a different triple in each: (1,1,1) in RGB,
        // but zero saturation in HSB and full lightness in HSL. Asking for (1,1,1)
        // everywhere asks for RED in the cylindrical modes, since hue 1 of 1 is a
        // full turn.
        const whites = [
            [s.RGB, [1, 1, 1]],
            [s.HSB, [0, 0, 1]],
            [s.HSL, [0, 0, 1]]
        ];
        for (const [mode, triple] of whites) {
            s.colorMode(mode, 1);
            const c = s.color(triple[0], triple[1], triple[2]);
            if (!c) {
                wrong.push('no colour in mode ' + mode);
                continue;
            }
            const [r, g, b] = [s.red(c), s.green(c), s.blue(c)];
            if (Math.abs(r - 1) > 0.02 || Math.abs(g - 1) > 0.02 || Math.abs(b - 1) > 0.02) {
                wrong.push(mode + ' white came out ' + [r, g, b].join(','));
            }
        }
        // And colours the cylindrical modes have to get RIGHT rather than merely
        // accept. Built in HSB and read back in RGB 255 - red() reports in the
        // CURRENT mode's range in p5 2.x, so reading without switching first gives
        // 0..1 and looks like a wrong answer when it is a different scale.
        s.colorMode(s.HSB, 360, 100, 100);
        const red = s.color(0, 100, 100);
        const cyan = s.color(180, 100, 100);
        s.colorMode(s.RGB, 255);
        const rgb = (c) => [s.red(c), s.green(c), s.blue(c)];
        if (Math.abs(rgb(red)[0] - 255) > 2 || rgb(red)[1] > 2) {
            wrong.push('HSB red came out ' + rgb(red).join(','));
        }
        if (rgb(cyan)[0] > 2 || Math.abs(rgb(cyan)[1] - 255) > 2) {
            wrong.push('HSB cyan came out ' + rgb(cyan).join(','));
        }
        s.colorMode(s.RGB, 255);
        if (wrong.length) {
            throw wrong.join(' | ');
        }
        return 'ok';
    }],
    ['color', 'lightness and paletteLerp', function (s) {
        s.colorMode(s.RGB, 255);
        const white = s.color(255, 255, 255);
        const black = s.color(0, 0, 0);
        if (typeof s.lightness !== 'function') {
            throw 'lightness absent';
        }
        const light = s.lightness(white);
        if (!(light > 90)) {
            throw 'lightness of white = ' + light;
        }
        if (typeof s.paletteLerp === 'function') {
            const mid = s.paletteLerp([
                [black, 0],
                [white, 1]
            ], 0.5);
            const r = s.red(mid);
            if (!(r > 100 && r < 155)) {
                throw 'paletteLerp midpoint red = ' + r;
            }
        }
        return 'ok';
    }],

    ['shape', 'contours', function (s) {
        // A CONTOUR IS A HOLE. beginContour/endContour inside a shape is how p5
        // draws a ring, and a contour that filled solid would look like a shape
        // that merely lost its hole.
        s.background(255);
        s.noStroke();
        s.fill(0);
        s.beginShape();
        s.vertex(2, 2);
        s.vertex(22, 2);
        s.vertex(22, 22);
        s.vertex(2, 22);
        s.beginContour();
        s.vertex(8, 8);
        s.vertex(8, 16);
        s.vertex(16, 16);
        s.vertex(16, 8);
        s.endContour();
        s.endShape(s.CLOSE);
        s.loadPixels();
        const at = (x, y) => {
            const i = 4 * (y * s.width + x);
            return [s.pixels[i], s.pixels[i + 1], s.pixels[i + 2]].join(',');
        };
        if (at(4, 4) !== '0,0,0') {
            throw 'the shape did not fill: ' + at(4, 4);
        }
        if (at(12, 12) !== '255,255,255') {
            throw 'the contour did not leave a hole: ' + at(12, 12);
        }
        return 'ok';
    }],
    ['shape', 'bezierPoint and bezierTangent', function (s) {
        const wrong = [];
        // At t = 0 and t = 1 a bezier is AT its endpoints, whatever the controls -
        // which makes those two the only values worth pinning without redoing the
        // arithmetic.
        if (s.bezierPoint(0, 10, 20, 30, 0) !== 0) {
            wrong.push('bezierPoint(0) = ' + s.bezierPoint(0, 10, 20, 30, 0));
        }
        if (s.bezierPoint(0, 10, 20, 30, 1) !== 30) {
            wrong.push('bezierPoint(1) = ' + s.bezierPoint(0, 10, 20, 30, 1));
        }
        const mid = s.bezierPoint(0, 0, 30, 30, 0.5);
        if (!(mid > 10 && mid < 20)) {
            wrong.push('bezierPoint(0.5) = ' + mid);
        }
        if (typeof s.bezierTangent !== 'function') {
            wrong.push('bezierTangent absent');
        }
        if (wrong.length) {
            throw wrong.join(' | ');
        }
        return 'ok';
    }],
    ['shape', 'splines', function (s) {
        // p5 2.x renamed curve* to spline*. The old names are gone, so a sketch
        // written for 2.x has only these.
        const wrong = [];
        for (const name of ['spline', 'splineVertex', 'splinePoint', 'splineTangent',
                'splineProperty'
            ]) {
            if (typeof s[name] !== 'function') {
                wrong.push(name + ' absent');
            }
        }
        if (wrong.length) {
            throw wrong.join(' | ');
        }
        s.background(255);
        s.noFill();
        s.stroke(0);
        s.beginShape();
        s.splineVertex(2, 20);
        s.splineVertex(8, 4);
        s.splineVertex(16, 20);
        s.splineVertex(22, 4);
        s.endShape();
        s.loadPixels();
        // SOMETHING was drawn: a spline that silently drew nothing is the failure
        // here, and one dark pixel anywhere in the band rules it out.
        let dark = 0;
        for (let x = 0; x < 24; x++) {
            for (let y = 0; y < 24; y++) {
                const i = 4 * (y * s.width + x);
                if (s.pixels[i] < 128) {
                    dark++;
                }
            }
        }
        if (dark === 0) {
            throw 'the spline drew nothing';
        }
        return 'ok';
    }],
    ['shape', 'clip', function (s) {
        // beginClip/endClip is p5's own name for canvas clipping, and this engine
        // has clip() - so the question is whether p5's wrapper reaches it.
        if (typeof s.beginClip !== 'function' || typeof s.endClip !== 'function') {
            throw 'beginClip/endClip absent';
        }
        s.background(255);
        s.beginClip();
        s.rect(0, 0, 8, 8);
        s.endClip();
        s.noStroke();
        s.fill(0);
        s.rect(0, 0, 24, 24); // clipped to the top-left 8x8
        s.loadPixels();
        const at = (x, y) => s.pixels[4 * (y * s.width + x)];
        if (at(4, 4) !== 0) {
            throw 'the clipped fill did not draw: ' + at(4, 4);
        }
        if (at(16, 16) !== 255) {
            throw 'the fill escaped the clip: ' + at(16, 16);
        }
        return 'ok';
    }],

    ['image', 'blend()', function (s) {
        // p5's blend() copies a region from one surface to another THROUGH a
        // globalCompositeOperation, so it is the composite path reached from a
        // different direction.
        if (typeof s.blend !== 'function') {
            throw 'blend absent';
        }
        s.background(128, 64, 32);
        const g = s.createGraphics(8, 8);
        g.background(64, 192, 96);
        s.blend(g, 0, 0, 8, 8, 0, 0, 8, 8, s.MULTIPLY);
        s.loadPixels();
        const i = 4 * (2 * s.width + 2);
        const got = [s.pixels[i], s.pixels[i + 1], s.pixels[i + 2]].join(',');
        if (got !== '32,48,12') {
            throw 'multiply blend gave ' + got + ' want 32,48,12';
        }
        return 'ok';
    }],
    ['image', 'smooth and noSmooth', function (s) {
        if (typeof s.smooth !== 'function' || typeof s.noSmooth !== 'function') {
            throw 'smooth/noSmooth absent';
        }
        s.noSmooth();
        s.smooth();
        s.noSmooth();
        return 'ok';
    }],

    ['dom', 'createImg', function (s) {
        if (typeof s.createImg !== 'function') {
            throw 'createImg absent';
        }
        const el = s.createImg('../assets/sprites.bmp', 'sprites');
        if (!el || !el.elt) {
            throw 'no element';
        }
        if (el.elt.tagName !== 'IMG') {
            throw 'tagName=' + el.elt.tagName;
        }
        el.remove();
        return 'ok';
    }],
    ['dom', 'createColorPicker and createFileInput', function (s) {
        const wrong = [];
        if (typeof s.createColorPicker === 'function') {
            const picker = s.createColorPicker('#ff0000');
            if (!picker || !picker.elt) {
                wrong.push('createColorPicker gave nothing');
            } else {
                if (picker.elt.type !== 'color') {
                    wrong.push('type=' + picker.elt.type);
                }
                picker.remove();
            }
        } else {
            wrong.push('createColorPicker absent');
        }
        if (typeof s.createFileInput === 'function') {
            const input = s.createFileInput(function () {});
            if (!input || !input.elt) {
                wrong.push('createFileInput gave nothing');
            } else {
                if (input.elt.type !== 'file') {
                    wrong.push('file type=' + input.elt.type);
                }
                input.remove();
            }
        } else {
            wrong.push('createFileInput absent');
        }
        if (wrong.length) {
            throw wrong.join(' | ');
        }
        return 'ok';
    }],
    ['dom', 'removeElements', function (s) {
        if (typeof s.removeElements !== 'function') {
            throw 'removeElements absent';
        }
        s.createDiv('one');
        s.createDiv('two');
        s.removeElements();
        return 'ok';
    }],

    ['io', 'getURL/getURLParams/getURLPath', function (s) {
        const wrong = [];
        if (typeof s.getURL() !== 'string') {
            wrong.push('getURL=' + typeof s.getURL());
        }
        if (!Array.isArray(s.getURLPath())) {
            wrong.push('getURLPath is not an array');
        }
        if (typeof s.getURLParams() !== 'object') {
            wrong.push('getURLParams is not an object');
        }
        if (wrong.length) {
            throw wrong.join(' | ');
        }
        return 'ok';
    }],
    ['io', 'loadBytes', function (s) {
        return s.loadBytes('probe-lines.txt').then(function (bytes) {
            // p5 2.x hands back the array itself rather than { bytes: [...] }.
            const data = bytes && bytes.bytes ? bytes.bytes : bytes;
            if (!data || data.length !== 13) {
                throw 'length=' + (data && data.length);
            }
            if (data[0] !== 111) {
                throw "first byte is not 'o': " + data[0];
            }
            return 'ok';
        });
    }],
    ['io', 'loadXML', function (s) {
        return s.loadXML('probe.xml').then(function (xml) {
            if (!xml) {
                throw 'no document';
            }
            const items = xml.getChildren('item');
            if (!items || items.length !== 2) {
                throw 'children=' + (items && items.length);
            }
            if (items[0].getContent() !== 'first') {
                throw 'content=' + items[0].getContent();
            }
            return 'ok';
        });
    }],
    ['io', 'httpGet', function (s) {
        if (typeof s.httpGet !== 'function') {
            throw 'httpGet absent';
        }
        return s.httpGet('probe-data.json', 'json').then(function (data) {
            if (!data || data.n !== 4) {
                throw 'parsed=' + JSON.stringify(data);
            }
            return 'ok';
        });
    }],
    ['io', 'saveTable', function (s) {
        if (typeof s.saveTable !== 'function') {
            throw 'saveTable absent';
        }
        const t = new s.constructor.Table();
        t.addColumn('a');
        const row = t.addRow();
        row.setString('a', 'x');
        s.saveTable(t, 'probe-table-out.csv');
        return 'ok';
    }],
    ['io', 'writeFile and createWriter', function (s) {
        const wrong = [];
        if (typeof s.writeFile !== 'function') {
            wrong.push('writeFile absent');
        }
        if (typeof s.createWriter !== 'function') {
            wrong.push('createWriter absent');
        } else {
            const w = s.createWriter('probe-writer.txt');
            if (!w || typeof w.write !== 'function') {
                wrong.push('createWriter gave no writer');
            } else {
                w.write(['hello']);
                w.close();
            }
        }
        if (wrong.length) {
            throw wrong.join(' | ');
        }
        return 'ok';
    }],

    ['env', 'print and noCanvas', function (s) {
        const wrong = [];
        if (typeof s.print !== 'function') {
            wrong.push('print absent');
        } else {
            s.print('probe');
        }
        if (typeof s.noCanvas !== 'function') {
            wrong.push('noCanvas absent');
        }
        if (wrong.length) {
            throw wrong.join(' | ');
        }
        return 'ok';
    }],

    // The 2D remainder of the coverage list. What is left after these is WEBGL,
    // motion sensors, audio, video and capture - out of scope by name, not by
    // omission.

    ['io', 'httpPost and httpDo', function (s) {
        const wrong = [];
        if (typeof s.httpPost !== 'function') {
            wrong.push('httpPost absent');
        }
        if (typeof s.httpDo !== 'function') {
            wrong.push('httpDo absent');
        }
        if (wrong.length) {
            throw wrong.join(' | ');
        }
        // httpDo with an explicit GET reaches the same path httpGet does, which is
        // the one this engine can serve without a network.
        return s.httpDo('probe-data.json', 'GET', 'json').then(function (data) {
            if (!data || data.n !== 4) {
                throw 'parsed=' + JSON.stringify(data);
            }
            return 'ok';
        });
    }],
    ['io', 'loadBlob', function (s) {
        if (typeof s.loadBlob !== 'function') {
            throw 'loadBlob absent';
        }
        return s.loadBlob('probe-lines.txt').then(function (blob) {
            if (!blob) {
                throw 'no blob';
            }
            if (blob.size !== 13) {
                throw 'size=' + blob.size;
            }
            if (!(blob instanceof Blob)) {
                throw 'not a Blob';
            }
            return 'ok';
        });
    }],

    ['env', 'fullscreen and pointer lock', function (s) {
        const wrong = [];
        // READ ONLY. There is no window manager here, so asking to go fullscreen or
        // to lock the pointer cannot be honoured - but the READ has to answer, and
        // p5 calls fullscreen() with no argument to ask.
        // FALSY, not literally false: p5 returns document.fullscreenElement, which is
        // the element or null. What matters is that the read ANSWERS - undefined
        // there means the property does not exist, which is a different question.
        if (typeof s.fullscreen !== 'function') {
            wrong.push('fullscreen absent');
        } else if (s.fullscreen() !== null && s.fullscreen() !== false) {
            wrong.push('fullscreen() = ' + s.fullscreen());
        }
        for (const name of ['requestPointerLock', 'exitPointerLock']) {
            if (typeof s[name] !== 'function') {
                wrong.push(name + ' absent');
            }
        }
        if (wrong.length) {
            throw wrong.join(' | ');
        }
        return 'ok';
    }],
    ['env', 'setFrameRate and touches', function (s) {
        const wrong = [];
        if (typeof s.setFrameRate !== 'function') {
            wrong.push('setFrameRate absent');
        } else {
            s.setFrameRate(24);
            if (s.getTargetFrameRate() !== 24) {
                wrong.push('target=' + s.getTargetFrameRate());
            }
            s.setFrameRate(60);
        }
        // `touches` is an ARRAY even with no touch device - a sketch reads
        // touches.length every frame, and undefined there is a TypeError per frame.
        if (!Array.isArray(s.touches)) {
            wrong.push('touches is ' + typeof s.touches);
        }
        if (wrong.length) {
            throw wrong.join(' | ');
        }
        return 'ok';
    }],

    ['math', 'createMatrix', function (s) {
        if (typeof s.createMatrix !== 'function') {
            throw 'createMatrix absent';
        }
        const m = s.createMatrix(4, 4);
        if (!m) {
            throw 'no matrix';
        }
        return 'ok';
    }],

    ['shape', 'curveDetail, strokeMode and vertexProperty', function (s) {
        const wrong = [];
        for (const name of ['curveDetail', 'strokeMode', 'vertexProperty', 'splineProperties']) {
            if (typeof s[name] !== 'function') {
                wrong.push(name + ' absent');
            }
        }
        if (wrong.length) {
            throw wrong.join(' | ');
        }
        // curveDetail IS WEBGL-ONLY AND p5 THROWS FOR IT IN 2D, deliberately:
        //   fn.curveDetail = function (d) {
        //     if (!(this._renderer instanceof Renderer3D)) { throw new Error(
        //       'curveDetail() only works in WebGL mode. ...'); }
        //
        // This probe used to call it on a 2D canvas and pass, which it could only do
        // while the engine was LOSING the exception - `finally` with no `catch`
        // swallowed a throw outright until 2026-08-21. Calling it and requiring the
        // throw is the honest version, and it pins the gate that was invisible.
        let gated = '';
        try {
            s.curveDetail(10);
        } catch (e) {
            gated = String(e && e.message ? e.message : e);
        }
        if (gated.indexOf('WebGL') < 0) {
            throw 'curveDetail did not report that it is WebGL-only in 2D mode: ' + gated;
        }
        s.background(255);
        s.noFill();
        s.stroke(0);
        s.beginShape();
        s.splineVertex(2, 20);
        s.splineVertex(8, 4);
        s.splineVertex(16, 20);
        s.splineVertex(22, 4);
        s.endShape();
        s.loadPixels();
        let dark = 0;
        for (let i = 0; i < s.pixels.length; i += 4) {
            if (s.pixels[i] < 128) {
                dark++;
            }
        }
        if (dark === 0) {
            throw 'the shape drew nothing after curveDetail';
        }
        return 'ok';
    }],

    ['save', 'saveFrames', function (s) {
        if (typeof s.saveFrames !== 'function') {
            throw 'saveFrames absent';
        }
        // The callback form, which hands the frames back instead of downloading
        // them - the only form a headless run can observe.
        return new Promise(function (resolve, reject) {
            let settled = false;
            // Half a second at 10fps - saveFrames paces itself with setInterval at
            // 1000/fps and stops on a setTimeout at the duration, so a duration
            // SHORTER than one interval captures nothing at all. Which is correct, and
            // was my probe's mistake rather than the library's.
            s.saveFrames('probe', 'png', 0.5, 10, function (frames) {
                settled = true;
                if (!Array.isArray(frames)) {
                    reject('frames is ' + typeof frames);
                    return;
                }
                if (frames.length === 0) {
                    reject('no frames captured');
                    return;
                }
                if (!frames[0].imageData || frames[0].imageData.indexOf('data:image') !== 0) {
                    reject('frame 0 has no data URL: ' + JSON.stringify(frames[0]).slice(0, 60));
                    return;
                }
                resolve('ok');
            });
            // saveFrames paces itself with the frame loop, so it needs turns to pass.
            setTimeout(function () {
                if (!settled) {
                    reject('saveFrames never called back');
                }
            }, 800);
        });
    }],

);
