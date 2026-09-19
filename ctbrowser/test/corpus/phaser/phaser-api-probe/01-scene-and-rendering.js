globalThis.__probes.push(
    // --- the scene itself ---------------------------------------------------
    ['scene', 'sys.settings', function (scene) {
        if (!scene.sys || !scene.sys.settings) {
            throw 'no sys.settings';
        }
        if (typeof scene.sys.settings.key !== 'string') {
            throw 'key is not a string';
        }
        return scene.sys.settings.key;
    }],
    ['scene', 'isActive', function (scene) {
        if (scene.scene.isActive() !== true) {
            throw 'the running scene says it is not active';
        }
        return 'ok';
    }],
    ['scene', 'game', function (scene) {
        if (!scene.game || scene.game.isRunning !== true) {
            throw 'game is not running';
        }
        return 'ok';
    }],
    ['scene', 'events', function (scene) {
        var seen = 0;
        var fn = function () {
            seen++;
        };
        scene.events.on('probe-event', fn);
        scene.events.emit('probe-event');
        scene.events.off('probe-event', fn);
        scene.events.emit('probe-event');
        if (seen !== 1) {
            throw 'emit/off counted ' + seen + ', want 1';
        }
        return 'ok';
    }],
    ['scene', 'registry', function (scene) {
        scene.registry.set('probe', 41);
        scene.registry.set('probe', scene.registry.get('probe') + 1);
        var got = scene.registry.get('probe');
        scene.registry.remove('probe');
        if (got !== 42) {
            throw 'registry round-trip gave ' + got;
        }
        return 'ok';
    }],
    ['scene', 'data', function (scene) {
        scene.data.set('probe', 'x');
        var got = scene.data.get('probe');
        if (got !== 'x') {
            throw 'data round-trip gave ' + got;
        }
        if (scene.data.has('probe') !== true) {
            throw 'has() says no after set()';
        }
        return 'ok';
    }],

    // --- game objects, the add factory --------------------------------------
    // Every one of these asserts a property BACK off the object, because
    // "it constructed" is true of an object that is entirely wrong.
    ['add', 'graphics', function (scene) {
        var g = scene.add.graphics();
        if (!g || typeof g.fillRect !== 'function') {
            throw 'no fillRect on a Graphics';
        }
        g.destroy();
        return 'ok';
    }],
    ['add', 'rectangle', function (scene) {
        var r = scene.add.rectangle(10, 20, 30, 40, 0xff0000);
        if (r.x !== 10 || r.y !== 20) {
            throw 'position ' + r.x + ',' + r.y;
        }
        if (r.width !== 30 || r.height !== 40) {
            throw 'size ' + r.width + 'x' + r.height;
        }
        r.destroy();
        return 'ok';
    }],
    ['add', 'circle', function (scene) {
        var c = scene.add.circle(5, 6, 12, 0x00ff00);
        if (c.x !== 5 || c.y !== 6) {
            throw 'position ' + c.x + ',' + c.y;
        }
        c.destroy();
        return 'ok';
    }],
    ['add', 'line', function (scene) {
        var l = scene.add.line(0, 0, 0, 0, 40, 40, 0xffffff);
        if (!l) {
            throw 'no line';
        }
        l.destroy();
        return 'ok';
    }],
    ['add', 'triangle', function (scene) {
        var t = scene.add.triangle(0, 0, 0, 40, 20, 0, 40, 40, 0xffff00);
        if (!t) {
            throw 'no triangle';
        }
        t.destroy();
        return 'ok';
    }],
    ['add', 'container', function (scene) {
        var child = scene.add.rectangle(0, 0, 4, 4, 0xffffff);
        var box = scene.add.container(10, 10, [child]);
        if (box.length !== 1) {
            throw 'container holds ' + box.length;
        }
        box.destroy();
        return 'ok';
    }],
    ['add', 'image', function (scene) {
        // __WHITE is one of the textures Phaser loads during boot, so this needs no
        // asset of its own - and it only exists if the base64 boot textures
        // decoded, which is the bug that kept the framework from starting at all.
        var im = scene.add.image(30, 40, '__WHITE');
        if (im.x !== 30 || im.y !== 40) {
            throw 'position ' + im.x + ',' + im.y;
        }
        if (im.width !== 4) {
            throw '__WHITE is ' + im.width + 'px wide, want 4';
        }
        im.destroy();
        return 'ok';
    }],
    ['add', 'sprite', function (scene) {
        var sp = scene.add.sprite(1, 2, '__WHITE');
        if (typeof sp.play !== 'function') {
            throw 'a Sprite with no play()';
        }
        sp.destroy();
        return 'ok';
    }],
    ['add', 'text', function (scene) {
        var t = scene.add.text(0, 0, 'hi');
        if (t.text !== 'hi') {
            throw 'text reads ' + t.text;
        }
        // The measured width depends on the font backend, so only its existence is
        // asserted - a golden is where text pixels get compared.
        if (!(t.width > 0)) {
            throw 'zero-width text';
        }
        t.destroy();
        return 'ok';
    }],

    // --- transforms and display state ---------------------------------------
    ['gameobject', 'setPosition', function (scene) {
        var r = scene.add.rectangle(0, 0, 10, 10, 0xffffff);
        r.setPosition(7, 8);
        if (r.x !== 7 || r.y !== 8) {
            throw 'setPosition gave ' + r.x + ',' + r.y;
        }
        r.destroy();
        return 'ok';
    }],
    ['gameobject', 'setScale', function (scene) {
        var r = scene.add.rectangle(0, 0, 10, 10, 0xffffff);
        r.setScale(2, 3);
        if (r.scaleX !== 2 || r.scaleY !== 3) {
            throw 'scale ' + r.scaleX + ',' + r.scaleY;
        }
        r.destroy();
        return 'ok';
    }],
    ['gameobject', 'setRotation', function (scene) {
        var r = scene.add.rectangle(0, 0, 10, 10, 0xffffff);
        r.setAngle(90);
        if (Math.abs(r.rotation - Math.PI / 2) > 1e-6) {
            throw 'rotation ' + r.rotation;
        }
        r.destroy();
        return 'ok';
    }],
    ['gameobject', 'setAlpha', function (scene) {
        var r = scene.add.rectangle(0, 0, 10, 10, 0xffffff);
        r.setAlpha(0.25);
        if (r.alpha !== 0.25) {
            throw 'alpha ' + r.alpha;
        }
        r.destroy();
        return 'ok';
    }],
    ['gameobject', 'setVisible', function (scene) {
        var r = scene.add.rectangle(0, 0, 10, 10, 0xffffff);
        r.setVisible(false);
        if (r.visible !== false) {
            throw 'visible stayed ' + r.visible;
        }
        r.destroy();
        return 'ok';
    }],
    ['gameobject', 'setOrigin', function (scene) {
        var im = scene.add.image(0, 0, '__WHITE');
        im.setOrigin(0, 0);
        if (im.originX !== 0 || im.originY !== 0) {
            throw 'origin ' + im.originX + ',' + im.originY;
        }
        im.destroy();
        return 'ok';
    }],
    ['gameobject', 'setDepth', function (scene) {
        var r = scene.add.rectangle(0, 0, 10, 10, 0xffffff);
        r.setDepth(5);
        if (r.depth !== 5) {
            throw 'depth ' + r.depth;
        }
        r.destroy();
        return 'ok';
    }],
    ['gameobject', 'getBounds', function (scene) {
        var r = scene.add.rectangle(50, 50, 20, 10, 0xffffff);
        var b = r.getBounds();
        if (Math.round(b.width) !== 20 || Math.round(b.height) !== 10) {
            throw 'bounds ' + b.width + 'x' + b.height;
        }
        r.destroy();
        return 'ok';
    }],
    ['gameobject', 'destroy', function (scene) {
        var before = scene.children.length;
        var r = scene.add.rectangle(0, 0, 4, 4, 0xffffff);
        if (scene.children.length !== before + 1) {
            throw 'add did not reach the display list';
        }
        r.destroy();
        if (scene.children.length !== before) {
            throw 'destroy left it on the display list';
        }
        return 'ok';
    }],

    // --- the display list ---------------------------------------------------
    ['displaylist', 'getChildren', function (scene) {
        var kids = scene.children.getChildren();
        if (!Array.isArray(kids)) {
            throw 'getChildren is not an array';
        }
        return String(kids.length);
    }],
    ['displaylist', 'bringToTop', function (scene) {
        var a = scene.add.rectangle(0, 0, 4, 4, 0xff0000);
        var b = scene.add.rectangle(0, 0, 4, 4, 0x00ff00);
        scene.children.bringToTop(a);
        var kids = scene.children.getChildren();
        var top = kids[kids.length - 1];
        a.destroy();
        b.destroy();
        if (top !== a) {
            throw 'bringToTop did not reorder';
        }
        return 'ok';
    }],

    // --- maths, which is pure and should simply be right --------------------
    ['math', 'Between', function (scene) {
        var d = Phaser.Math.Distance.Between(0, 0, 3, 4);
        if (d !== 5) {
            throw 'distance ' + d;
        }
        return 'ok';
    }],
    ['math', 'Clamp', function (scene) {
        if (Phaser.Math.Clamp(15, 0, 10) !== 10) {
            throw 'clamp high';
        }
        if (Phaser.Math.Clamp(-5, 0, 10) !== 0) {
            throw 'clamp low';
        }
        return 'ok';
    }],
    ['math', 'Linear', function (scene) {
        var mid = Phaser.Math.Linear(0, 10, 0.5);
        if (mid !== 5) {
            throw 'lerp ' + mid;
        }
        return 'ok';
    }],
    ['math', 'DegToRad', function (scene) {
        if (Math.abs(Phaser.Math.DegToRad(180) - Math.PI) > 1e-9) {
            throw 'DegToRad';
        }
        if (Math.abs(Phaser.Math.RadToDeg(Math.PI) - 180) > 1e-9) {
            throw 'RadToDeg';
        }
        return 'ok';
    }],
    ['math', 'Wrap', function (scene) {
        var w = Phaser.Math.Wrap(12, 0, 10);
        if (w !== 2) {
            throw 'wrap ' + w;
        }
        return 'ok';
    }],
    ['math', 'Vector2', function (scene) {
        var v = new Phaser.Math.Vector2(3, 4);
        if (v.length() !== 5) {
            throw 'length ' + v.length();
        }
        v.normalize();
        if (Math.abs(v.length() - 1) > 1e-6) {
            throw 'normalize gave ' + v.length();
        }
        return 'ok';
    }],
    ['math', 'Vector2.add', function (scene) {
        var v = new Phaser.Math.Vector2(1, 2).add(new Phaser.Math.Vector2(3, 4));
        if (v.x !== 4 || v.y !== 6) {
            throw 'add gave ' + v.x + ',' + v.y;
        }
        return 'ok';
    }],
    ['math', 'Matrix4', function (scene) {
        if (!Phaser.Math.Matrix4) {
            throw 'no Matrix4';
        }
        var m = new Phaser.Math.Matrix4();
        m.identity();
        if (m.val[0] !== 1 || m.val[5] !== 1) {
            throw 'identity is not identity';
        }
        return 'ok';
    }],
    ['math', 'RND', function (scene) {
        // Phaser's own PRNG, seeded - so this asserts REPEATABILITY, which is the
        // property a golden depends on, rather than a particular number.
        var a = new Phaser.Math.RandomDataGenerator(['probe']);
        var b = new Phaser.Math.RandomDataGenerator(['probe']);
        if (a.frac() !== b.frac()) {
            throw 'the same seed gave different numbers';
        }
        return 'ok';
    }],
    ['math', 'Snap.To', function (scene) {
        var s = Phaser.Math.Snap.To(13, 10);
        if (s !== 10) {
            throw 'snap ' + s;
        }
        return 'ok';
    }],

    // --- geometry -----------------------------------------------------------
    ['geom', 'Rectangle', function (scene) {
        var r = new Phaser.Geom.Rectangle(0, 0, 10, 10);
        if (!Phaser.Geom.Rectangle.Contains(r, 5, 5)) {
            throw 'Contains says no for the centre';
        }
        if (Phaser.Geom.Rectangle.Contains(r, 15, 5)) {
            throw 'Contains says yes for outside';
        }
        return 'ok';
    }],
    ['geom', 'Circle', function (scene) {
        var c = new Phaser.Geom.Circle(0, 0, 10);
        if (!Phaser.Geom.Circle.Contains(c, 0, 5)) {
            throw 'Contains says no inside';
        }
        if (Phaser.Geom.Circle.Contains(c, 0, 15)) {
            throw 'Contains says yes outside';
        }
        return 'ok';
    }],
    ['geom', 'Intersects', function (scene) {
        var a = new Phaser.Geom.Rectangle(0, 0, 10, 10);
        var b = new Phaser.Geom.Rectangle(5, 5, 10, 10);
        var c = new Phaser.Geom.Rectangle(50, 50, 10, 10);
        if (!Phaser.Geom.Intersects.RectangleToRectangle(a, b)) {
            throw 'overlapping said no';
        }
        if (Phaser.Geom.Intersects.RectangleToRectangle(a, c)) {
            throw 'distant said yes';
        }
        return 'ok';
    }],
    // NOT `Geom.Point`, WHICH PHASER 4 DROPPED. The probe asked for it first and
    // failed - and the failure was the probe's, not the engine's. Phaser 4's Geom
    // namespace is Circle, Ellipse, Intersects, Line, Polygon, Rectangle and
    // Triangle, with points now plain `{x, y}` or Math.Vector2. Kept as a note
    // because a probe that fails for its own reasons is worse than no probe: it
    // spends somebody's afternoon on a bug that is not there.
    ['geom', 'Triangle', function (scene) {
        var t = new Phaser.Geom.Triangle(0, 0, 10, 0, 0, 10);
        if (!Phaser.Geom.Triangle.Contains(t, 2, 2)) {
            throw 'Contains says no inside';
        }
        if (Phaser.Geom.Triangle.Contains(t, 9, 9)) {
            throw 'Contains says yes outside';
        }
        return 'ok';
    }],
    ['geom', 'Polygon', function (scene) {
        var poly = new Phaser.Geom.Polygon([0, 0, 10, 0, 10, 10, 0, 10]);
        if (!Phaser.Geom.Polygon.Contains(poly, 5, 5)) {
            throw 'Contains says no for the centre';
        }
        return 'ok';
    }],
    ['geom', 'Line', function (scene) {
        var l = new Phaser.Geom.Line(0, 0, 3, 4);
        if (Phaser.Geom.Line.Length(l) !== 5) {
            throw 'length ' + Phaser.Geom.Line.Length(l);
        }
        return 'ok';
    }],

    // --- textures -----------------------------------------------------------
    ['textures', 'boot textures exist', function (scene) {
        // THE ONE THAT WOULD HAVE CAUGHT IT. Phaser loads __DEFAULT, __MISSING and
        // __WHITE from base64 PNGs during boot and will not start until all three
        // settle; when data: URLs could not be read they all failed, and the
        // framework hung with no error anything could see.
        var missing = ['__DEFAULT', '__MISSING', '__WHITE'].filter(function (k) {
            return !scene.textures.exists(k);
        });
        if (missing.length) {
            throw 'missing boot textures: ' + missing.join(' ');
        }
        return 'ok';
    }],
    ['textures', 'get', function (scene) {
        var t = scene.textures.get('__WHITE');
        if (!t || t.key !== '__WHITE') {
            throw 'get gave ' + (t && t.key);
        }
        return 'ok';
    }],
    ['textures', 'getFrame size', function (scene) {
        var f = scene.textures.getFrame('__WHITE');
        if (f.width !== 4 || f.height !== 4) {
            throw '__WHITE frame is ' + f.width + 'x' + f.height;
        }
        return 'ok';
    }],
    ['textures', 'addCanvas', function (scene) {
        var c = document.createElement('canvas');
        c.width = 8;
        c.height = 8;
        scene.textures.addCanvas('probe-canvas', c);
        var ok = scene.textures.exists('probe-canvas');
        scene.textures.remove('probe-canvas');
        if (!ok) {
            throw 'addCanvas did not register';
        }
        return 'ok';
    }],
    ['textures', 'getPixel', function (scene) {
        // __WHITE is a 4x4 solid white PNG, so this reads a real decoded pixel back
        // through Phaser's own accessor - which is a decode assertion as much as a
        // Phaser one.
        var p = scene.textures.getPixel(1, 1, '__WHITE');
        if (!p) {
            throw 'no pixel';
        }
        if (p.red !== 255 || p.green !== 255 || p.blue !== 255) {
            throw '__WHITE pixel is ' + p.red + ',' + p.green + ',' + p.blue;
        }
        return 'ok';
    }],

    // --- time ---------------------------------------------------------------
    ['time', 'addEvent', function (scene) {
        var ev = scene.time.addEvent({
            delay: 1000,
            callback: function () {}
        });
        if (!ev || ev.delay !== 1000) {
            throw 'delay ' + (ev && ev.delay);
        }
        ev.remove();
        return 'ok';
    }],
    ['time', 'delayedCall', function (scene) {
        var ev = scene.time.delayedCall(500, function () {});
        if (!ev) {
            throw 'no event';
        }
        ev.remove();
        return 'ok';
    }],
    ['time', 'now', function (scene) {
        if (!(scene.time.now >= 0)) {
            throw 'now is ' + scene.time.now;
        }
        return 'ok';
    }],

    // --- tweens -------------------------------------------------------------
    ['tweens', 'add', function (scene) {
        var r = scene.add.rectangle(0, 0, 4, 4, 0xffffff);
        var t = scene.tweens.add({
            targets: r,
            x: 100,
            duration: 100
        });
        if (!t) {
            throw 'no tween';
        }
        t.remove();
        r.destroy();
        return 'ok';
    }],
    ['tweens', 'getTweens', function (scene) {
        var list = scene.tweens.getTweens();
        if (!Array.isArray(list)) {
            throw 'getTweens is not an array';
        }
        return String(list.length);
    }],
    ['tweens', 'Easing', function (scene) {
        if (Phaser.Math.Easing.Linear(0.5) !== 0.5) {
            throw 'linear easing';
        }
        var q = Phaser.Math.Easing.Quadratic.In(0.5);
        if (Math.abs(q - 0.25) > 1e-9) {
            throw 'Quadratic.In(0.5) = ' + q;
        }
        return 'ok';
    }],

    // --- cameras ------------------------------------------------------------
    ['cameras', 'main', function (scene) {
        var cam = scene.cameras.main;
        if (!cam) {
            throw 'no main camera';
        }
        if (cam.width <= 0 || cam.height <= 0) {
            throw 'camera is ' + cam.width + 'x' + cam.height;
        }
        return 'ok';
    }],
    ['cameras', 'setScroll', function (scene) {
        var cam = scene.cameras.main;
        cam.setScroll(10, 20);
        var got = cam.scrollX + ',' + cam.scrollY;
        cam.setScroll(0, 0);
        if (got !== '10,20') {
            throw 'scroll ' + got;
        }
        return 'ok';
    }],
    ['cameras', 'setZoom', function (scene) {
        var cam = scene.cameras.main;
        cam.setZoom(2);
        var got = cam.zoom;
        cam.setZoom(1);
        if (got !== 2) {
            throw 'zoom ' + got;
        }
        return 'ok';
    }],
    ['cameras', 'setBackgroundColor', function (scene) {
        var cam = scene.cameras.main;
        cam.setBackgroundColor(0x112233);
        return 'ok';
    }],

    // --- input --------------------------------------------------------------
    // A real device is not available, so these assert the PLUMBING exists and
    // responds - not that a click arrives, which is the corpus page's job.
    ['input', 'keyboard', function (scene) {
        if (!scene.input.keyboard) {
            throw 'no keyboard plugin';
        }
        var key = scene.input.keyboard.addKey('A');
        if (!key) {
            throw 'addKey gave nothing';
        }
        if (key.isDown !== false) {
            throw 'a key nobody pressed is down';
        }
        return 'ok';
    }],
    ['input', 'createCursorKeys', function (scene) {
        var cur = scene.input.keyboard.createCursorKeys();
        if (!cur || !cur.left || !cur.up) {
            throw 'no cursor keys';
        }
        return 'ok';
    }],
    ['input', 'activePointer', function (scene) {
        var p = scene.input.activePointer;
        if (!p) {
            throw 'no active pointer';
        }
        if (typeof p.x !== 'number') {
            throw 'pointer x is ' + typeof p.x;
        }
        return 'ok';
    }],
    ['input', 'setInteractive', function (scene) {
        var r = scene.add.rectangle(0, 0, 10, 10, 0xffffff);
        r.setInteractive();
        var ok = !!r.input;
        r.destroy();
        if (!ok) {
            throw 'setInteractive left no input object';
        }
        return 'ok';
    }],

    // --- the loader ---------------------------------------------------------
    ['loader', 'exists', function (scene) {
        if (!scene.load || typeof scene.load.image !== 'function') {
            throw 'no loader';
        }
        return 'ok';
    }],
    ['loader', 'image from a data URL', function (scene) {
        // ASYNC, and the point of it: a data: URL is exactly what Phaser's own boot
        // textures are, and this drives one through the PUBLIC loader rather than
        // through boot. A 2x2 PNG, generated rather than pasted.
        var png = 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0k' +
            'AAAAEklEQVR42mP4z8DwHwyBNBgAAEnICfcD2WTxAAAAAElFTkSuQmCC';
        return new Promise(function (resolve, reject) {
            scene.load.image('probe-png', png);
            scene.load.once('complete', function () {
                try {
                    if (!scene.textures.exists('probe-png')) {
                        throw 'the loader reported complete but no texture';
                    }
                    var f = scene.textures.getFrame('probe-png');
                    if (f.width !== 2 || f.height !== 2) {
                        throw 'loaded frame is ' + f.width + 'x' + f.height;
                    }
                    scene.textures.remove('probe-png');
                    resolve('ok');
                } catch (e) {
                    reject(e);
                }
            });
            scene.load.start();
        });
    }],

);
