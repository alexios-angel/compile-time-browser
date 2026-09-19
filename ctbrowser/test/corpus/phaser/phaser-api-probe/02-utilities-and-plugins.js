globalThis.__probes.push(
    // --- utilities ----------------------------------------------------------
    ['utils', 'Array.Shuffle', function (scene) {
        var a = Phaser.Utils.Array.Shuffle([1, 2, 3, 4, 5]);
        if (a.length !== 5) {
            throw 'shuffle changed the length to ' + a.length;
        }
        return 'ok';
    }],
    ['utils', 'Array.GetRandom', function (scene) {
        var got = Phaser.Utils.Array.GetRandom([7]);
        if (got !== 7) {
            throw 'GetRandom of one item gave ' + got;
        }
        return 'ok';
    }],
    ['utils', 'Objects.GetValue', function (scene) {
        var v = Phaser.Utils.Objects.GetValue({
            a: {
                b: 3
            }
        }, 'a.b', 0);
        if (v !== 3) {
            throw 'GetValue gave ' + v;
        }
        var d = Phaser.Utils.Objects.GetValue({}, 'nope', 'fallback');
        if (d !== 'fallback') {
            throw 'the default was not used';
        }
        return 'ok';
    }],
    ['utils', 'String.Pad', function (scene) {
        var p = Phaser.Utils.String.Pad('7', 3, '0', 1);
        if (p !== '007') {
            throw 'Pad gave ' + p;
        }
        return 'ok';
    }],
    ['utils', 'Display.Color', function (scene) {
        var c = Phaser.Display.Color.IntegerToColor(0xff8000);
        if (c.red !== 255 || c.green !== 128 || c.blue !== 0) {
            throw 'IntegerToColor gave ' + c.red + ',' + c.green + ',' + c.blue;
        }
        return 'ok';
    }],
    ['utils', 'Display.HexStringToColor', function (scene) {
        var c = Phaser.Display.Color.HexStringToColor('#00ff00');
        if (c.green !== 255 || c.red !== 0) {
            throw 'hex gave ' + c.red + ',' + c.green;
        }
        return 'ok';
    }],

    // --- the renderer -------------------------------------------------------
    ['renderer', 'type is CANVAS', function (scene) {
        // The harness asks for CANVAS by name, so this asserts the request was
        // honoured rather than quietly downgraded - a golden that changed renderer
        // silently would be a golden that means nothing.
        if (scene.game.renderer.type !== Phaser.CANVAS) {
            throw 'renderer type is ' + scene.game.renderer.type + ', want ' + Phaser.CANVAS;
        }
        return 'ok';
    }],
    ['renderer', 'canvas element', function (scene) {
        var c = scene.game.canvas;
        if (!c || c.tagName.toLowerCase() !== 'canvas') {
            throw 'game.canvas is not a canvas';
        }
        if (c.width <= 0 || c.height <= 0) {
            throw 'canvas is ' + c.width + 'x' + c.height;
        }
        return 'ok';
    }],
    ['renderer', '2d context', function (scene) {
        var ctx = scene.game.context;
        if (!ctx || typeof ctx.fillRect !== 'function') {
            throw 'no 2d context';
        }
        return 'ok';
    }],
    ['renderer', 'snapshot', function (scene) {
        // Needs a real display path to read back; the corpus page's golden is where
        // pixels are compared.
        return 'SKIP';
    }],

    // --- scale --------------------------------------------------------------
    ['scale', 'gameSize', function (scene) {
        var s = scene.scale;
        if (!s) {
            throw 'no scale manager';
        }
        if (s.gameSize.width <= 0) {
            throw 'gameSize is ' + s.gameSize.width;
        }
        return 'ok';
    }],
    ['scale', 'displaySize', function (scene) {
        if (!scene.scale.displaySize) {
            throw 'no displaySize';
        }
        return 'ok';
    }],

    // --- audio --------------------------------------------------------------
    ['sound', 'noAudio manager', function (scene) {
        // The harness boots with noAudio, so what is asserted is that the NULL
        // manager is in place and answers - not that anything makes a sound.
        if (!scene.sound) {
            throw 'no sound manager';
        }
        if (typeof scene.sound.play !== 'function') {
            throw 'sound manager has no play()';
        }
        return 'ok';
    }],

    // --- animation ----------------------------------------------------------
    // --- physics ------------------------------------------------------------
    // Arcade is enabled in the harness config, so these exercise the real
    // integrator rather than asserting that a plugin is missing.
    ['physics', 'plugin', function (scene) {
        if (!scene.physics || !scene.physics.add) {
            throw 'no arcade physics plugin';
        }
        return 'ok';
    }],
    ['physics', 'body velocity integrates', function (scene) {
        var s = scene.physics.add.image(0, 0, '__WHITE');
        s.setVelocity(60, 0);
        // TWO CALLS, NOT ONE, and the distinction is worth knowing before writing a
        // game against it: `step` integrates the BODIES, and `postUpdate` is what
        // writes each body's position back onto its game object. A probe that
        // stepped and then read `s.x` saw 0 while `s.body.x` had moved exactly 30 -
        // the physics was right and the sync had simply not happened yet. The real
        // game loop calls both; driving them by hand here keeps the assertion off
        // however many frames the harness happened to tick.
        scene.physics.world.step(0.5);
        scene.physics.world.postUpdate();
        var moved = s.x;
        s.destroy();
        // 60 px/s for half a second is 30 px, and Arcade integrates velocity * dt
        // exactly - so this is an equality, not a tolerance.
        if (Math.abs(moved - 30) > 0.001) {
            throw 'after 0.5s at 60px/s x is ' + moved + ', want 30';
        }
        return 'ok';
    }],
    ['physics', 'setGravity', function (scene) {
        var s = scene.physics.add.image(0, 0, '__WHITE');
        s.setGravityY(100);
        var g = s.body.gravity.y;
        s.destroy();
        if (g !== 100) {
            throw 'gravity.y is ' + g;
        }
        return 'ok';
    }],
    ['physics', 'overlap', function (scene) {
        var a = scene.physics.add.image(0, 0, '__WHITE');
        var b = scene.physics.add.image(0, 0, '__WHITE');
        var hit = false;
        scene.physics.add.overlap(a, b, function () {
            hit = true;
        });
        scene.physics.world.step(0.016);
        a.destroy();
        b.destroy();
        if (!hit) {
            throw 'two bodies at the same point did not overlap';
        }
        return 'ok';
    }],
    ['physics', 'world bounds', function (scene) {
        var w = scene.physics.world.bounds;
        if (w.width <= 0 || w.height <= 0) {
            throw 'world bounds are ' + w.width + 'x' + w.height;
        }
        return 'ok';
    }],

    // --- structs ------------------------------------------------------------
    ['structs', 'Map', function (scene) {
        var m = new Phaser.Structs.Map([]);
        m.set('a', 1);
        if (m.get('a') !== 1) {
            throw 'get gave ' + m.get('a');
        }
        if (m.size !== 1) {
            throw 'size is ' + m.size;
        }
        m.delete('a');
        if (m.has('a')) {
            throw 'delete left it behind';
        }
        return 'ok';
    }],
    ['structs', 'List', function (scene) {
        var l = new Phaser.Structs.List(null);
        l.add('x');
        l.add('y');
        if (l.length !== 2) {
            throw 'length ' + l.length;
        }
        if (l.first !== 'x') {
            throw 'first is ' + l.first;
        }
        l.remove('x');
        if (l.length !== 1) {
            throw 'remove left ' + l.length;
        }
        return 'ok';
    }],
    ['structs', 'Size', function (scene) {
        var sz = new Phaser.Structs.Size(100, 50);
        if (sz.width !== 100 || sz.height !== 50) {
            throw 'size ' + sz.width + 'x' + sz.height;
        }
        sz.setSize(20, 10);
        if (sz.width !== 20) {
            throw 'setSize gave ' + sz.width;
        }
        return 'ok';
    }],
    ['structs', 'ProcessQueue', function (scene) {
        var q = new Phaser.Structs.ProcessQueue();
        q.add('a');
        // The queue is DOUBLE BUFFERED - an add lands on the next update, which is
        // the behaviour worth asserting rather than working around.
        q.update();
        if (q.getActive().length !== 1) {
            throw 'after update the queue holds ' + q.getActive().length;
        }
        return 'ok';
    }],

    // --- curves -------------------------------------------------------------
    ['curves', 'Line', function (scene) {
        var c = new Phaser.Curves.Line([0, 0, 10, 0]);
        if (c.getLength() !== 10) {
            throw 'length ' + c.getLength();
        }
        var mid = c.getPoint(0.5);
        if (mid.x !== 5) {
            throw 'midpoint x is ' + mid.x;
        }
        return 'ok';
    }],
    ['curves', 'Ellipse', function (scene) {
        var c = new Phaser.Curves.Ellipse(0, 0, 10);
        if (!(c.getLength() > 0)) {
            throw 'zero-length ellipse';
        }
        return 'ok';
    }],
    ['curves', 'Path', function (scene) {
        var path = new Phaser.Curves.Path(0, 0);
        path.lineTo(10, 0);
        path.lineTo(10, 10);
        var len = path.getLength();
        if (Math.abs(len - 20) > 0.5) {
            throw 'path length ' + len + ', want 20';
        }
        return 'ok';
    }],

    // --- actions ------------------------------------------------------------
    ['actions', 'SetXY', function (scene) {
        var a = scene.add.rectangle(0, 0, 4, 4, 0xffffff);
        var b = scene.add.rectangle(0, 0, 4, 4, 0xffffff);
        Phaser.Actions.SetXY([a, b], 10, 20, 5);
        var got = a.x + ',' + a.y + ' ' + b.x + ',' + b.y;
        a.destroy();
        b.destroy();
        // The fourth argument is a STEP, so the second object is offset from the
        // first - which is the whole point of an Action over a loop.
        if (got !== '10,20 15,20') {
            throw 'SetXY with step gave ' + got;
        }
        return 'ok';
    }],
    ['actions', 'IncX', function (scene) {
        var a = scene.add.rectangle(3, 0, 4, 4, 0xffffff);
        Phaser.Actions.IncX([a], 7);
        var x = a.x;
        a.destroy();
        if (x !== 10) {
            throw 'IncX gave ' + x;
        }
        return 'ok';
    }],
    ['actions', 'Call', function (scene) {
        var seen = 0;
        Phaser.Actions.Call([{}, {}], function () {
            seen++;
        });
        if (seen !== 2) {
            throw 'Call ran ' + seen + ' times';
        }
        return 'ok';
    }],

    // --- cache --------------------------------------------------------------
    ['cache', 'text', function (scene) {
        scene.cache.text.add('probe-text', 'hello');
        var got = scene.cache.text.get('probe-text');
        scene.cache.text.remove('probe-text');
        if (got !== 'hello') {
            throw 'cache round-trip gave ' + got;
        }
        return 'ok';
    }],
    ['cache', 'json', function (scene) {
        scene.cache.json.add('probe-json', {
            n: 4
        });
        var got = scene.cache.json.get('probe-json');
        scene.cache.json.remove('probe-json');
        if (!got || got.n !== 4) {
            throw 'json cache gave ' + JSON.stringify(got);
        }
        return 'ok';
    }],
    ['cache', 'exists', function (scene) {
        if (scene.cache.text.exists('probe-absent')) {
            throw 'exists() says yes for a key never added';
        }
        return 'ok';
    }],

    // --- plugins ------------------------------------------------------------
    ['plugins', 'PluginCache core', function (scene) {
        // Phaser's own boot refuses to start without this exact check, so a false
        // here is not a detail - it is the framework declining to run.
        if (!Phaser.Plugins.PluginCache.hasCore('EventEmitter')) {
            throw 'no core EventEmitter';
        }
        return 'ok';
    }],
    ['plugins', 'scene plugins', function (scene) {
        if (!scene.plugins || typeof scene.plugins.get !== 'function') {
            throw 'no plugin manager';
        }
        return 'ok';
    }],

    // --- data, events, game -------------------------------------------------
    ['data', 'DataManager', function (scene) {
        var d = new Phaser.Data.DataManager({}, new Phaser.Events.EventEmitter());
        d.set('k', 1);
        if (d.get('k') !== 1) {
            throw 'get gave ' + d.get('k');
        }
        d.remove('k');
        if (d.has('k')) {
            throw 'remove left it';
        }
        return 'ok';
    }],
    ['events', 'EventEmitter', function (scene) {
        var e = new Phaser.Events.EventEmitter();
        var seen = [];
        e.on('x', function (v) {
            seen.push(v);
        });
        e.emit('x', 1);
        e.emit('x', 2);
        if (seen.join(',') !== '1,2') {
            throw 'emitted ' + seen.join(',');
        }
        return 'ok';
    }],
    ['events', 'once', function (scene) {
        var e = new Phaser.Events.EventEmitter();
        var seen = 0;
        e.once('x', function () {
            seen++;
        });
        e.emit('x');
        e.emit('x');
        if (seen !== 1) {
            throw 'once fired ' + seen + ' times';
        }
        return 'ok';
    }],
    ['game', 'instanceof', function (scene) {
        if (!(scene.game instanceof Phaser.Game)) {
            throw 'game is not a Phaser.Game';
        }
        return 'ok';
    }],
    ['game', 'loop', function (scene) {
        if (!scene.game.loop || scene.game.loop.running !== true) {
            throw 'the game loop is not running';
        }
        return 'ok';
    }],

    // --- constants ----------------------------------------------------------
    // Dull, and worth having: a constant table that arrives as `undefined` makes
    // every comparison against it silently false.
    ['const', 'BlendModes', function (scene) {
        if (Phaser.BlendModes.NORMAL === undefined) {
            throw 'BlendModes.NORMAL is undefined';
        }
        return 'ok';
    }],
    ['const', 'ScaleModes', function (scene) {
        if (Phaser.ScaleModes.DEFAULT === undefined) {
            throw 'ScaleModes.DEFAULT is undefined';
        }
        if (Phaser.ScaleModes.LINEAR === undefined) {
            throw 'ScaleModes.LINEAR is undefined';
        }
        return 'ok';
    }],
    ['const', 'TintModes', function (scene) {
        if (Phaser.TintModes.MULTIPLY === undefined) {
            throw 'TintModes.MULTIPLY is undefined';
        }
        return 'ok';
    }],
    ['const', 'renderer types', function (scene) {
        if (Phaser.CANVAS === Phaser.WEBGL) {
            throw 'CANVAS and WEBGL are the same value';
        }
        if (Phaser.AUTO === undefined) {
            throw 'AUTO is undefined';
        }
        return 'ok';
    }],

    // --- Class, Core, DOM ---------------------------------------------------
    ['class', 'Extends', function (scene) {
        // Phaser's own class builder - every one of its types is made with this, so
        // a gap here is a gap in all of them.
        var Base = new Phaser.Class({
            initialize: function Base() {
                this.n = 1;
            },
            bump: function () {
                return ++this.n;
            }
        });
        var Derived = new Phaser.Class({
            Extends: Base,
            initialize: function Derived() {
                Base.call(this);
            }
        });
        var d = new Derived();
        if (d.bump() !== 2) {
            throw 'inherited method gave ' + d.n;
        }
        if (!(d instanceof Base)) {
            throw 'instanceof says the subclass is not a Base';
        }
        return 'ok';
    }],
    ['core', 'Events constants', function (scene) {
        if (typeof Phaser.Core.Events.READY !== 'string') {
            throw 'Core.Events.READY is not a string';
        }
        return 'ok';
    }],
    ['core', 'TimeStep', function (scene) {
        var loop = scene.game.loop;
        if (!(loop.delta >= 0)) {
            throw 'delta is ' + loop.delta;
        }
        // NOT `frame >= 1`. These probes run from create(), which IS frame zero, so
        // that assertion was about when the harness runs rather than about the
        // engine - and it failed for exactly that reason.
        if (typeof loop.frame !== 'number') {
            throw 'frame counter is ' + typeof loop.frame;
        }
        if (loop.running !== true) {
            throw 'the TimeStep is not running';
        }
        return 'ok';
    }],
    ['dom', 'ParseXML', function (scene) {
        var doc = Phaser.DOM.ParseXML('<a><b>t</b></a>');
        if (!doc) {
            throw 'ParseXML gave nothing';
        }
        return 'ok';
    }],

    // --- tilemaps and filters ------------------------------------------------
    ['tilemaps', 'Formats', function (scene) {
        if (Phaser.Tilemaps.Formats.ARRAY_2D === undefined) {
            throw 'Formats.ARRAY_2D is undefined';
        }
        return 'ok';
    }],
    ['tilemaps', 'make from data', function (scene) {
        var map = scene.make.tilemap({
            data: [
                [0, 1],
                [1, 0]
            ],
            tileWidth: 8,
            tileHeight: 8
        });
        if (!map) {
            throw 'no tilemap';
        }
        if (map.width !== 2 || map.height !== 2) {
            throw 'map is ' + map.width + 'x' + map.height;
        }
        map.destroy();
        return 'ok';
    }],
    ['filters', 'controller', function (scene) {
        // `Phaser.Filters.ColorMatrix` is the FILTER CONTROLLER, not the matrix
        // maths - the two share a name and are different modules. The controller
        // needs a camera's filter list to attach to, so only its presence is
        // asserted here; the maths is probed as display/ColorMatrix below.
        if (typeof Phaser.Filters.ColorMatrix !== 'function') {
            throw 'no ColorMatrix filter';
        }
        if (typeof Phaser.Filters.Controller !== 'function') {
            throw 'no filter Controller';
        }
        return 'ok';
    }],
    ['display', 'ColorMatrix', function (scene) {
        var cm = new Phaser.Display.ColorMatrix();
        cm.grayscale();
        var data = cm.getData();
        if (!data || !data.length) {
            throw 'no matrix data';
        }
        return 'ok';
    }],

    ['anims', 'create', function (scene) {
        var a = scene.anims.create({
            key: 'probe-anim',
            frames: [{
                key: '__WHITE'
            }],
            frameRate: 1
        });
        var ok = scene.anims.exists('probe-anim');
        scene.anims.remove('probe-anim');
        if (!a || !ok) {
            throw 'anims.create did not register';
        }
        return 'ok';
    }]
);
