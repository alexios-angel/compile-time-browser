// What test/corpus/phaser/phaser_invaders.cpp asks
// examples/pages/phaser-invaders.html. That page belongs to the example - the
// example program and a render golden load it too - so it does not list this
// file; the test runs it into the page after loading it. Every question goes
// through `window.__game`, the global the page keeps its game on for exactly
// this.

// Where the ship is.
function phaser_invaders_ship_x() {
    return window.__game.scene.scenes[0].player.x;
}

// score,lives,over - after the game has played on its own.
function phaser_invaders_state() {
    var s = window.__game.scene.scenes[0];
    return s.score + ',' + s.lives + ',' + s.over;
}

// score/invaders - how dirty the scene is before a restart.
function phaser_invaders_dirty() {
    var s = window.__game.scene.scenes[0];
    return s.score + '/' + s.invaders.getChildren().length;
}

// Tears the scene down and runs its create() again.
function phaser_invaders_restart() {
    window.__game.scene.scenes[0].scene.restart();
}

// score/invaders/lives/over/bullets, all as create() leaves them.
function phaser_invaders_fresh() {
    var s = window.__game.scene.scenes[0];
    return s.score + '/' + s.invaders.getChildren().length + '/' + s.lives + '/' + s.over +
        '/' + s.bullets.getChildren().length;
}
