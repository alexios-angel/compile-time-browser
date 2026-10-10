// What test/corpus/phaser/phaser_ratchet.cpp asks phaser_ratchet.html, one
// function per question. Named phaser_ratchet_* so none of them can collide
// with a global of Phaser's own.

// The `defines Phaser` rung: the bundle ran and left a callable Phaser behind.
function phaser_ratchet_defines() {
    return typeof Phaser !== 'undefined' && !!Phaser.Game;
}

// A Game with a headless-ish config. AUTO would pick a renderer; CANVAS is
// asked for by name so a WebGL gap cannot be mistaken for a Game gap.
//
// create() and update() each leave a mark in `window.__phase`, which is how
// the next two rungs know they ran.
function phaser_ratchet_game() {
    window.__phase = '';
    window.__game = new Phaser.Game({
        type: Phaser.CANVAS,
        width: 200,
        height: 200,
        banner: false,
        audio: {
            noAudio: true
        },
        scene: {
            create: function () {
                window.__phase += 'create;';
            },
            update: function () {
                window.__phase += 'update;';
            }
        }
    });
    return 'constructed';
}

function phaser_ratchet_phase() {
    return window.__phase || '<empty>';
}

// One red 60px square at the origin, through the running scene's own Graphics
// object - the `paints` rung reads a pixel inside it back off the canvas.
function phaser_ratchet_draw() {
    var scene = window.__game.scene.scenes[0];
    var g = scene.add.graphics();
    g.fillStyle(0xff0000, 1);
    g.fillRect(0, 0, 60, 60);
    return 'drew';
}

// The keyboard verdict's game: a scene that POLLS the cursor keys every frame,
// the way a Phaser game reads input, and records the arrow it saw held.
function phaser_ratchet_keyboard() {
    window.__seen = 'none';
    new Phaser.Game({
        type: Phaser.CANVAS,
        width: 100,
        height: 100,
        banner: false,
        audio: {
            noAudio: true
        },
        scene: {
            create: function () {
                this.cursors = this.input.keyboard.createCursorKeys();
            },
            update: function () {
                if (this.cursors.left.isDown) {
                    window.__seen = 'left';
                } else if (this.cursors.right.isDown) {
                    window.__seen = 'right';
                }
            }
        }
    });
}

function phaser_ratchet_seen() {
    return window.__seen;
}
