// What of p5.js actually works.
//
// The corpus pages prove specific things render. This asks a much broader and
// duller question - does each function DO something rather than throw - across
// as much of p5's 417-function surface as can be called headlessly.
//
// It exists because the colour-mode bug taught the lesson twice: `colorMode(HSB)`
// was broken from the first day p5 ran here, silently, and no test noticed
// because no test called it. A wide, shallow probe found five real bugs in one
// run after an afternoon of reading found one.
//
// THE SHAPE OF A PROBE. `[module, name, body]`. The body is handed the sketch
// and returns whatever it wants; a THROW is recorded as a failure and does not
// stop the rest. Returning the string 'SKIP' records the probe as not
// applicable - used where a function needs a display, a network or a file.
//
// A probe should ASSERT, not merely call. `s.rect(0, 0, 10, 10)` passes in an
// engine that draws nothing; reading a pixel back does not. Where checking the
// result is cheap the probe checks it and throws its own message, because a
// function that runs and returns garbage is the failure mode this whole file
// exists to catch.
//
// tools/p5-api.py lists which `fn.*` in the bundle no probe here mentions -
// that list is the work queue, and it is why probe names match p5's own.
//
// A CORRECTION worth keeping, because it is the failure mode of a harness. An
// earlier commit message here claimed five failures were contamination from a
// throwing probe unwinding p5's state stack. The contamination was real and is
// fixed (see the try/finally in the runner), but those five were NOT it: they
// were real failures that a mis-applied edit had briefly deleted the probes
// for. Checked one at a time afterwards, every one reproduced on its own.
// Never conclude a failure was noise without reproducing it in isolation.

globalThis.__probes = [
  // --- colour -------------------------------------------------------------
  ['color', 'color', function (s) {
    const c = s.color(255, 0, 0);
    if (s.red(c) !== 255 || s.green(c) !== 0) { throw 'channels: ' + c.toString(); }
    return c.toString();
  }],
  ['color', 'colorMode', function (s) {
    s.colorMode(s.HSB, 360, 100, 100);
    const c = s.color(0, 100, 100);
    const out = s.red(c);
    s.colorMode(s.RGB, 255);
    if (out < 250) { throw 'HSB hue 0 should be red, got red=' + out; }
    return 'ok';
  }],
  ['color', 'lerpColor', function (s) {
    const mid = s.lerpColor(s.color(0), s.color(255), 0.5);
    if (s.red(mid) < 100 || s.red(mid) > 155) { throw 'midpoint red=' + s.red(mid); }
    return 'ok';
  }],
  ['color', 'red/green/blue/alpha', function (s) {
    const c = s.color(10, 20, 30, 40);
    return [s.red(c), s.green(c), s.blue(c), Math.round(s.alpha(c))].join(',');
  }],
  ['color', 'hue/saturation/brightness', function (s) {
    const c = s.color(255, 0, 0);
    return [Math.round(s.hue(c)), Math.round(s.saturation(c)), Math.round(s.brightness(c))]
      .join(',');
  }],
  ['color', 'background', function (s) {
    s.background(10, 20, 30);
    s.loadPixels();
    const px = s.pixels;
    if (px[0] !== 10 || px[1] !== 20 || px[2] !== 30) {
      throw 'background did not reach the pixels: ' + px[0] + ',' + px[1] + ',' + px[2];
    }
    return 'ok';
  }],
  ['color', 'fill/noFill', function (s) {
    s.background(0);
    s.noStroke();
    s.fill(255, 0, 0);
    s.rect(0, 0, 10, 10);
    s.loadPixels();
    if (s.pixels[0] !== 255) { throw 'fill did not take: ' + s.pixels[0]; }
    s.noFill();
    return 'ok';
  }],
  ['color', 'stroke/noStroke/strokeWeight', function (s) {
    s.stroke(0, 255, 0);
    s.strokeWeight(3);
    s.line(0, 20, 30, 20);
    s.noStroke();
    return 'ok';
  }],
  ['color', 'clear', function (s) { s.clear(); return 'ok'; }],

  // --- shape --------------------------------------------------------------
  ['shape', 'rect', function (s) {
    s.background(0); s.noStroke(); s.fill(255);
    s.rect(2, 2, 6, 6);
    s.loadPixels();
    const i = 4 * (4 * s.width + 4);
    if (s.pixels[i] !== 255) { throw 'rect did not fill at 4,4'; }
    return 'ok';
  }],
  ['shape', 'square', function (s) { s.square(0, 0, 5); return 'ok'; }],
  ['shape', 'ellipse', function (s) {
    s.background(0); s.noStroke(); s.fill(255);
    s.ellipse(10, 10, 10, 10);
    s.loadPixels();
    const i = 4 * (10 * s.width + 10);
    if (s.pixels[i] !== 255) { throw 'ellipse did not fill its centre'; }
    return 'ok';
  }],
  ['shape', 'circle', function (s) { s.circle(10, 10, 6); return 'ok'; }],
  ['shape', 'line', function (s) { s.line(0, 0, 10, 10); return 'ok'; }],
  ['shape', 'point', function (s) { s.point(3, 3); return 'ok'; }],
  ['shape', 'triangle', function (s) { s.triangle(0, 0, 10, 0, 0, 10); return 'ok'; }],
  ['shape', 'quad', function (s) { s.quad(0, 0, 10, 0, 10, 10, 0, 10); return 'ok'; }],
  ['shape', 'arc', function (s) { s.arc(10, 10, 10, 10, 0, s.PI); return 'ok'; }],
  ['shape', 'beginShape/vertex/endShape', function (s) {
    s.background(0); s.noStroke(); s.fill(255);
    s.beginShape();
    s.vertex(0, 0); s.vertex(16, 0); s.vertex(16, 16); s.vertex(0, 16);
    s.endShape(s.CLOSE);
    s.loadPixels();
    const i = 4 * (8 * s.width + 8);
    if (s.pixels[i] !== 255) { throw 'the custom shape did not fill'; }
    return 'ok';
  }],
  ['shape', 'bezierVertex', function (s) {
    s.beginShape();
    s.vertex(0, 0);
    s.bezierOrder(3);
    s.bezierVertex(5, 0); s.bezierVertex(10, 5); s.bezierVertex(10, 10);
    s.endShape();
    return 'ok';
  }],
  ['shape', 'bezier', function (s) { s.bezier(0, 0, 5, 0, 10, 5, 10, 10); return 'ok'; }],
  ['shape', 'rectMode/ellipseMode', function (s) {
    s.rectMode(s.CENTER); s.rect(10, 10, 4, 4); s.rectMode(s.CORNER);
    s.ellipseMode(s.CENTER); s.ellipse(10, 10, 4, 4); s.ellipseMode(s.CENTER);
    return 'ok';
  }],
  ['shape', 'strokeCap/strokeJoin', function (s) {
    s.strokeCap(s.ROUND); s.strokeJoin(s.BEVEL); return 'ok';
  }],
  ['shape', 'erase/noErase', function (s) { s.erase(); s.noErase(); return 'ok'; }],
  ['shape', 'blendMode', function (s) { s.blendMode(s.BLEND); return 'ok'; }],

  // --- transform ----------------------------------------------------------
  ['transform', 'push/pop', function (s) {
    s.push(); s.translate(5, 5); s.pop();
    // The stack must actually unwind: draw at the origin and look there.
    s.background(0); s.noStroke(); s.fill(255);
    s.rect(0, 0, 4, 4);
    s.loadPixels();
    if (s.pixels[0] !== 255) { throw 'pop() did not restore the origin'; }
    return 'ok';
  }],
  ['transform', 'translate', function (s) {
    s.background(0); s.noStroke(); s.fill(255);
    s.push(); s.translate(10, 10); s.rect(0, 0, 4, 4); s.pop();
    s.loadPixels();
    const i = 4 * (11 * s.width + 11);
    if (s.pixels[i] !== 255) { throw 'translate did not move the rect'; }
    return 'ok';
  }],
  ['transform', 'rotate', function (s) { s.push(); s.rotate(s.PI / 4); s.pop(); return 'ok'; }],
  ['transform', 'scale', function (s) { s.push(); s.scale(2, 0.5); s.pop(); return 'ok'; }],
  ['transform', 'shearX/shearY', function (s) {
    s.push(); s.shearX(0.1); s.shearY(0.1); s.pop(); return 'ok';
  }],
  ['transform', 'resetMatrix', function (s) { s.resetMatrix(); return 'ok'; }],
  ['transform', 'applyMatrix', function (s) {
    s.push(); s.applyMatrix(1, 0, 0, 1, 2, 2); s.pop(); return 'ok';
  }],
  ['transform', 'angleMode', function (s) {
    s.angleMode(s.DEGREES);
    const out = s.sin(90);
    s.angleMode(s.RADIANS);
    if (out < 0.99) { throw 'DEGREES not honoured: sin(90)=' + out; }
    return 'ok';
  }],

  // --- typography ---------------------------------------------------------
  ['typography', 'text', function (s) {
    s.background(0); s.fill(255); s.noStroke();
    s.textSize(20);
    s.text('M', 0, 18);
    s.loadPixels();
    let ink = 0;
    for (let i = 0; i < s.pixels.length; i += 4) { if (s.pixels[i] > 128) { ink++; } }
    if (ink === 0) { throw 'text drew nothing'; }
    return 'ok';
  }],
  ['typography', 'textWidth', function (s) {
    s.textSize(20);
    const w = s.textWidth('MM');
    if (!(w > 0)) { throw 'textWidth=' + w; }
    if (!(s.textWidth('MMMM') > w)) { throw 'a longer string is not wider'; }
    return 'ok';
  }],
  ['typography', 'textAlign', function (s) { s.textAlign(s.CENTER, s.CENTER); s.textAlign(s.LEFT, s.BASELINE); return 'ok'; }],
  ['typography', 'textSize', function (s) { s.textSize(14); return '' + s.textSize(); }],
  ['typography', 'textLeading', function (s) { s.textLeading(20); return '' + s.textLeading(); }],
  ['typography', 'textStyle', function (s) { s.textStyle(s.BOLD); s.textStyle(s.NORMAL); return 'ok'; }],
  ['typography', 'textFont', function (s) { s.textFont('serif'); return 'ok'; }],
  ['typography', 'textAscent/textDescent', function (s) {
    const a = s.textAscent();
    if (!(a > 0)) { throw 'textAscent=' + a; }
    return 'ok';
  }],
  ['typography', 'textWrap', function (s) { s.textWrap(s.WORD); return 'ok'; }],
  ['typography', 'textBounds', function (s) {
    const b = s.textBounds('hi', 0, 0);
    if (!b || typeof b.w !== 'number') { throw 'no bounds: ' + JSON.stringify(b); }
    return 'ok';
  }],

  // --- math ---------------------------------------------------------------
  ['math', 'map', function (s) {
    const out = s.map(5, 0, 10, 0, 100);
    if (out !== 50) { throw 'map=' + out; }
    return 'ok';
  }],
  ['math', 'constrain', function (s) {
    if (s.constrain(11, 0, 10) !== 10 || s.constrain(-1, 0, 10) !== 0) { throw 'constrain'; }
    return 'ok';
  }],
  ['math', 'lerp', function (s) { if (s.lerp(0, 10, 0.5) !== 5) { throw 'lerp'; } return 'ok'; }],
  ['math', 'dist', function (s) { if (s.dist(0, 0, 3, 4) !== 5) { throw 'dist'; } return 'ok'; }],
  ['math', 'mag', function (s) { if (s.mag(3, 4) !== 5) { throw 'mag'; } return 'ok'; }],
  ['math', 'norm', function (s) { if (s.norm(5, 0, 10) !== 0.5) { throw 'norm'; } return 'ok'; }],
  ['math', 'abs/ceil/floor/round', function (s) {
    return [s.abs(-2), s.ceil(1.2), s.floor(1.8), s.round(1.5)].join(',');
  }],
  ['math', 'sq/sqrt/pow/exp/log', function (s) {
    return [s.sq(3), s.sqrt(9), s.pow(2, 3), Math.round(s.exp(0)), s.log(1)].join(',');
  }],
  ['math', 'min/max', function (s) { return s.min(1, 2) + ',' + s.max([3, 9, 4]); }],
  ['math', 'trig', function (s) {
    const out = Math.round(s.sin(s.PI / 2)) + ',' + Math.round(s.cos(0)) + ',' +
                Math.round(s.degrees(s.PI));
    if (out !== '1,1,180') { throw 'trig: ' + out; }
    return 'ok';
  }],
  ['math', 'random', function (s) {
    s.randomSeed(1);
    const a = s.random();
    s.randomSeed(1);
    if (s.random() !== a) { throw 'randomSeed does not make it repeatable'; }
    if (a < 0 || a >= 1) { throw 'random out of range: ' + a; }
    return 'ok';
  }],
  ['math', 'randomGaussian', function (s) { return typeof s.randomGaussian(); }],
  ['math', 'noise', function (s) {
    s.noiseSeed(1);
    const a = s.noise(0.5);
    if (a < 0 || a > 1) { throw 'noise out of range: ' + a; }
    s.noiseSeed(1);
    if (s.noise(0.5) !== a) { throw 'noiseSeed does not make it repeatable'; }
    return 'ok';
  }],
  ['math', 'noiseDetail', function (s) { s.noiseDetail(4, 0.5); return 'ok'; }],

  // --- vector -------------------------------------------------------------
  ['vector', 'createVector', function (s) {
    const v = s.createVector(3, 4);
    if (v.mag() !== 5) { throw 'mag=' + v.mag(); }
    return 'ok';
  }],
  ['vector', 'add/sub/mult/div', function (s) {
    const v = s.createVector(1, 2).add(1, 1).sub(1, 1).mult(2).div(2);
    return v.x + ',' + v.y;
  }],
  ['vector', 'normalize/limit/setMag', function (s) {
    const v = s.createVector(3, 4).normalize();
    if (Math.abs(v.mag() - 1) > 0.001) { throw 'normalized mag=' + v.mag(); }
    return 'ok';
  }],
  ['vector', 'dot/cross', function (s) {
    const a = s.createVector(1, 0), b = s.createVector(0, 1);
    if (a.dot(b) !== 0) { throw 'dot'; }
    return 'ok';
  }],
  ['vector', 'p5.Vector statics', function (s) {
    const sum = s.constructor.Vector.add(s.createVector(1, 1), s.createVector(2, 2));
    return sum.x + ',' + sum.y;
  }],
  ['vector', 'heading/rotate', function (s) {
    const v = s.createVector(1, 0);
    v.rotate(s.PI / 2);
    if (Math.abs(v.y - 1) > 0.001) { throw 'rotate: ' + v.x + ',' + v.y; }
    return 'ok';
  }],

  // --- pixels -------------------------------------------------------------
  ['pixels', 'loadPixels/updatePixels', function (s) {
    s.background(0);
    s.loadPixels();
    if (s.pixels.length !== s.width * s.height * 4 * s.pixelDensity() * s.pixelDensity()) {
      throw 'pixels length=' + s.pixels.length;
    }
    for (let i = 0; i < s.pixels.length; i += 4) { s.pixels[i] = 255; }
    s.updatePixels();
    s.loadPixels();
    if (s.pixels[0] !== 255) { throw 'updatePixels did not write back'; }
    return 'ok';
  }],
  ['pixels', 'get', function (s) {
    s.background(10, 20, 30);
    const px = s.get(0, 0);
    if (px[0] !== 10 || px[1] !== 20) { throw 'get=' + px.join(','); }
    return 'ok';
  }],
  ['pixels', 'set', function (s) {
    s.background(0);
    s.set(1, 1, s.color(0, 255, 0));
    s.updatePixels();
    const px = s.get(1, 1);
    if (px[1] !== 255) { throw 'set=' + px.join(','); }
    return 'ok';
  }],
  ['pixels', 'pixelDensity', function (s) { return '' + s.pixelDensity(); }],
  ['pixels', 'createImage', function (s) {
    const img = s.createImage(4, 4);
    if (img.width !== 4) { throw 'width=' + img.width; }
    return 'ok';
  }],
  ['pixels', 'image', function (s) {
    const g = s.createGraphics(4, 4);
    g.background(255, 0, 0);
    s.background(0);
    s.image(g, 0, 0);
    const px = s.get(1, 1);
    if (px[0] !== 255) { throw 'the graphics did not draw: ' + px.join(','); }
    return 'ok';
  }],
  ['pixels', 'copy', function (s) {
    s.background(0);
    s.copy(0, 0, 4, 4, 8, 8, 4, 4);
    return 'ok';
  }],
  // filter() compiles a shader, which needs the WebGL path this engine
  // refuses by design - so it is out of scope rather than broken.
  ['pixels', 'filter', function (s) { return 'SKIP'; }],
  ['pixels', 'imageMode/tint/noTint', function (s) {
    s.imageMode(s.CORNER); s.tint(255, 128); s.noTint(); return 'ok';
  }],

  // --- structure ----------------------------------------------------------
  ['structure', 'createCanvas/resizeCanvas', function (s) {
    const was = s.width;
    s.resizeCanvas(was, s.height);
    if (s.width !== was) { throw 'resizeCanvas changed the width'; }
    return 'ok';
  }],
  ['structure', 'createGraphics', function (s) {
    const g = s.createGraphics(8, 8);
    if (g.width !== 8) { throw 'width=' + g.width; }
    g.background(0);
    return 'ok';
  }],
  ['structure', 'frameRate/frameCount', function (s) {
    if (typeof s.frameCount !== 'number') { throw 'frameCount=' + s.frameCount; }
    return typeof s.frameRate();
  }],
  ['structure', 'millis', function (s) { return typeof s.millis(); }],
  ['structure', 'noLoop/loop/isLooping', function (s) {
    s.noLoop();
    if (s.isLooping && s.isLooping()) { throw 'still looping after noLoop'; }
    s.loop();
    s.noLoop();
    return 'ok';
  }],
  ['structure', 'redraw', function (s) { s.redraw(); return 'ok'; }],
  ['structure', 'day/month/year/hour', function (s) {
    return [typeof s.day(), typeof s.month(), typeof s.year(), typeof s.hour()].join(',');
  }],

  // --- data ---------------------------------------------------------------
  ['data', 'nf/nfc/nfp/nfs', function (s) {
    const out = s.nf(3.14159, 1, 2);
    if (out !== '3.14') { throw 'nf=' + out; }
    return 'ok';
  }],
  // p5 2.x REMOVED its own split/trim/match/sort/reverse helpers - the
  // language has them - so probing for those is asking the wrong question.
  // splitTokens is the one that survived, because it has no String equivalent.
  ['data', 'splitTokens', function (s) {
    const out = s.splitTokens('a b,c', ' ,');
    if (out.length !== 3) { throw 'splitTokens=' + JSON.stringify(out); }
    return 'ok';
  }],
  ['data', 'shuffle', function (s) {
    const out = s.shuffle([1, 2, 3, 4]);
    if (out.length !== 4) { throw 'shuffle length=' + out.length; }
    return 'ok';
  }],
  ['data', 'int/float/str/boolean', function (s) {
    return [s.int('3'), s.float('2.5'), s.str(1), s.boolean('true')].join(',');
  }],
  ['data', 'hex/unhex', function (s) { return s.hex(255, 2); }],
  ['data', 'storeItem/getItem/removeItem', function (s) {
    s.storeItem('probe', 5);
    if (s.getItem('probe') !== 5) { throw 'getItem=' + s.getItem('probe'); }
    s.removeItem('probe');
    return 'ok';
  }],

  // --- dom ----------------------------------------------------------------
  ['dom', 'createDiv', function (s) {
    const e = s.createDiv('hi');
    if (!e || !e.elt || e.elt.tagName.toLowerCase() !== 'div') { throw 'not a div'; }
    e.remove();
    return 'ok';
  }],
  ['dom', 'createP/createSpan', function (s) {
    s.createP('p').remove();
    s.createSpan('span').remove();
    return 'ok';
  }],
  ['dom', 'createButton', function (s) {
    const b = s.createButton('go');
    if (b.elt.tagName.toLowerCase() !== 'button') { throw 'not a button'; }
    b.remove();
    return 'ok';
  }],
  ['dom', 'createSlider', function (s) {
    const sl = s.createSlider(0, 10, 5);
    if (Number(sl.value()) !== 5) { throw 'value=' + sl.value(); }
    sl.remove();
    return 'ok';
  }],
  ['dom', 'createInput', function (s) {
    const i = s.createInput('t');
    if (i.value() !== 't') { throw 'value=' + i.value(); }
    i.remove();
    return 'ok';
  }],
  ['dom', 'createCheckbox', function (s) { s.createCheckbox('c', true).remove(); return 'ok'; }],
  ['dom', 'createSelect', function (s) { s.createSelect().remove(); return 'ok'; }],
  ['dom', 'createRadio', function (s) { s.createRadio().remove(); return 'ok'; }],
  ['dom', 'createA', function (s) { s.createA('#', 'link').remove(); return 'ok'; }],
  ['dom', 'createElement', function (s) { s.createElement('h1', 'title').remove(); return 'ok'; }],
  ['dom', 'select/selectAll', function (s) {
    if (!s.select('body')) { throw 'select("body") found nothing'; }
    return 'ok';
  }],
  ['dom', 'element.position/size', function (s) {
    const e = s.createDiv('x');
    e.position(1, 2);
    e.size(10, 10);
    e.remove();
    return 'ok';
  }],
  ['dom', 'element.style/class', function (s) {
    const e = s.createDiv('x');
    e.style('color', 'red');
    e.addClass('k');
    if (!e.hasClass('k')) { throw 'addClass/hasClass disagree'; }
    e.removeClass('k');
    e.remove();
    return 'ok';
  }],
  ['dom', 'element.html/attribute', function (s) {
    const e = s.createDiv('x');
    e.html('<b>bold</b>');
    e.attribute('data-x', '1');
    if (e.attribute('data-x') !== '1') { throw 'attribute round trip'; }
    e.remove();
    return 'ok';
  }],
  ['dom', 'element.show/hide', function (s) {
    const e = s.createDiv('x');
    e.hide(); e.show(); e.remove();
    return 'ok';
  }],
  ['dom', 'element.parent/child', function (s) {
    // p5's child() is `elt.childNodes`, which includes TEXT nodes - the div's
    // own label is one of them - so this looks for the element rather than
    // counting.
    const parent = s.createDiv('p');
    const kid = s.createDiv('k');
    kid.parent(parent);
    const kids = parent.child();
    let found = false;
    for (const node of kids) { if (node === kid.elt) { found = true; } }
    parent.remove();
    if (!found) { throw 'the child is not among ' + kids.length + ' childNodes'; }
    return 'ok';
  }],

  // --- events -------------------------------------------------------------
  ['events', 'mouseX/mouseY', function (s) {
    if (typeof s.mouseX !== 'number') { throw 'mouseX=' + s.mouseX; }
    return 'ok';
  }],
  ['events', 'pmouseX/movedX', function (s) {
    return typeof s.pmouseX + ',' + typeof s.movedX;
  }],
  ['events', 'mouseIsPressed/keyIsPressed', function (s) {
    return typeof s.mouseIsPressed + ',' + typeof s.keyIsPressed;
  }],
  ['events', 'keyIsDown', function (s) { return typeof s.keyIsDown(65); }],
  ['events', 'cursor/noCursor', function (s) { s.cursor(s.ARROW); s.noCursor(); return 'ok'; }],
  ['events', 'element.mousePressed', function (s) {
    const b = s.createButton('x');
    b.mousePressed(function () {});
    b.remove();
    return 'ok';
  }],

  // --- accessibility ------------------------------------------------------
  ['a11y', 'describe', function (s) { s.describe('a test sketch'); return 'ok'; }],
  ['a11y', 'describeElement', function (s) { s.describeElement('box', 'a box'); return 'ok'; }],
  ['a11y', 'textOutput/gridOutput', function (s) { return 'SKIP'; }],

  // --- loading ------------------------------------------------------------
  //
  // These are ASYNC and the runner is not, so each returns its promise and the
  // runner awaits it. The assets are baked by tests/p5_api.cpp, so the whole
  // fetch-and-parse path runs without reaching the network.
  ['load', 'loadJSON', function (s) {
    return s.loadJSON('probe-data.json').then(function (j) {
      if (!j || j.name !== 'probe' || j.n !== 4) { throw 'parsed=' + JSON.stringify(j); }
      return 'ok';
    });
  }],
  ['load', 'loadStrings', function (s) {
    return s.loadStrings('probe-lines.txt').then(function (lines) {
      if (lines.join('/') !== 'one/two/three') { throw 'lines=' + JSON.stringify(lines); }
      return 'ok';
    });
  }],
  ['load', 'loadTable', function (s) {
    return s.loadTable('probe-table.csv').then(function (t) {
      if (!t || typeof t.getRowCount !== 'function') { throw 'not a Table: ' + typeof t; }
      if (t.getRowCount() < 2) { throw 'rows=' + t.getRowCount(); }
      return 'ok';
    });
  }],
  // loadImage is the whole blob/object-URL/Image chain in one call: p5 fetches
  // the bytes, wraps them in a Blob, makes an object URL, points an Image at it,
  // REVOKES the URL inside onload and only then draws the image into its own
  // canvas. Every one of those has to work for this to return pixels.
  ['load', 'loadImage', function (s) {
    return s.loadImage('probe-image.bmp').then(function (img) {
      if (!img) { throw 'no image'; }
      if (img.width !== 4 || img.height !== 4) { throw 'size=' + img.width + 'x' + img.height; }
      // The PIXELS, not just the size: a 4x4 image of the wrong colour is what
      // a decode that silently produced nothing looks like.
      img.loadPixels();
      var green = img.get(1, 1);
      if (green[1] < 200 || green[0] > 50) { throw 'pixel=' + green.join(','); }
      return 'ok';
    });
  }],
  ['load', 'image() draws a loaded image', function (s) {
    return s.loadImage('probe-image.bmp').then(function (img) {
      s.background(0);
      s.image(img, 0, 0);
      s.loadPixels();
      var at = 4 * (2 * s.width + 2);
      if (s.pixels[at + 1] < 200) { throw 'canvas pixel=' + s.pixels.slice(at, at + 4).join(','); }
      return 'ok';
    });
  }],
  ['load', 'loadFont', function (s) { return typeof s.loadFont === 'function' ? 'SKIP' : 'absent'; }],

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
      [s.ADD, [192, 255, 128]],       // lighter: b + s, clamped
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
                        s.REMOVE]) {
      s.blendMode(mode);
      if (s.drawingContext.globalCompositeOperation !== mode) {
        wrong.push(mode + ' did not reach the context');
      }
    }
    s.blendMode(s.BLEND);
    if (wrong.length) { throw wrong.join(' | '); }
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
    if (there !== '255,255,255') { throw 'tint painted over transparency: ' + there; }
    return 'ok';
  }],

  // --- the unmeasured P2D surface -----------------------------------------
  //
  // tools/p5-api.py --coverage listed 190 functions no probe mentioned. Most are
  // WEBGL, audio, video or motion sensors and are out of scope by name. These are
  // the ones that are NOT: they run in P2D, headless, with no hardware - so
  // nothing excuses them being unmeasured, and unmeasured is the state every bug
  // found here was hiding in.

  ['math', 'trigonometry', function (s) {
    const wrong = [];
    const near = (n, got, want) => {
      if (Math.abs(got - want) > 1e-6) { wrong.push(n + '=' + got + ' want ' + want); }
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
    if (wrong.length) { throw wrong.join(' | '); }
    return 'ok';
  }],
  ['math', 'byte/char/unchar/code conversions', function (s) {
    const wrong = [];
    const is = (n, got, want) => {
      if (String(got) !== String(want)) { wrong.push(n + '=' + got + ' want ' + want); }
    };
    is('byte', s.byte(65), 65);
    is('byte wraps', s.byte(200), -56); // a signed 8-bit value, which is the point
    is('char', s.char(65), 'A');
    is('unchar', s.unchar('A'), 65);
    // NOT s.code(): in p5 2.x `code` is the keyboard system variable - the
    // KeyboardEvent.code of the last key - and it shadows the old function.
    if (typeof s.code !== 'string') { wrong.push('code is ' + typeof s.code + ', want a string'); }
    if (wrong.length) { throw wrong.join(' | '); }
    return 'ok';
  }],

  ['env', 'time', function (s) {
    const wrong = [];
    // A clock, so the assertion is on the RANGE - the failure being guarded
    // against is a constant, which is what an unimplemented one returns.
    if (!(s.second() >= 0 && s.second() < 60)) { wrong.push('second=' + s.second()); }
    if (!(s.minute() >= 0 && s.minute() < 60)) { wrong.push('minute=' + s.minute()); }
    if (!(s.hour() >= 0 && s.hour() < 24)) { wrong.push('hour=' + s.hour()); }
    if (!(s.day() >= 1 && s.day() <= 31)) { wrong.push('day=' + s.day()); }
    if (!(s.month() >= 1 && s.month() <= 12)) { wrong.push('month=' + s.month()); }
    if (!(s.year() > 2000)) { wrong.push('year=' + s.year()); }
    if (typeof s.millis() !== 'number') { wrong.push('millis=' + typeof s.millis()); }
    if (wrong.length) { throw wrong.join(' | '); }
    return 'ok';
  }],
  ['env', 'frame rate', function (s) {
    const wrong = [];
    if (typeof s.getFrameRate() !== 'number') { wrong.push('getFrameRate not a number'); }
    s.frameRate(30);
    if (s.getTargetFrameRate() !== 30) { wrong.push('target=' + s.getTargetFrameRate()); }
    if (typeof s.deltaTime !== 'number') { wrong.push('deltaTime=' + typeof s.deltaTime); }
    if (wrong.length) { throw wrong.join(' | '); }
    return 'ok';
  }],
  ['env', 'window and display metrics', function (s) {
    const wrong = [];
    for (const name of ['windowWidth', 'windowHeight', 'displayWidth', 'displayHeight']) {
      if (!(s[name] > 0)) { wrong.push(name + '=' + s[name]); }
    }
    if (!(s.displayDensity() > 0)) { wrong.push('displayDensity=' + s.displayDensity()); }
    if (s.focused !== true) { wrong.push('focused=' + s.focused); }
    if (wrong.length) { throw wrong.join(' | '); }
    return 'ok';
  }],
  ['env', 'mouse and key state', function (s) {
    const wrong = [];
    for (const name of ['mouseX', 'mouseY', 'pmouseX', 'pmouseY', 'winMouseX', 'winMouseY',
                        'pwinMouseX', 'pwinMouseY', 'movedX', 'movedY']) {
      if (typeof s[name] !== 'number') { wrong.push(name + '=' + typeof s[name]); }
    }
    if (typeof s.mouseButton !== 'object' && typeof s.mouseButton !== 'string') {
      wrong.push('mouseButton=' + typeof s.mouseButton);
    }
    if (typeof s.keyIsPressed !== 'boolean') { wrong.push('keyIsPressed=' + typeof s.keyIsPressed); }
    if (typeof s.key !== 'string') { wrong.push('key=' + typeof s.key); }
    if (typeof s.keyCode !== 'number' && s.keyCode !== undefined) {
      wrong.push('keyCode=' + typeof s.keyCode);
    }
    if (wrong.length) { throw wrong.join(' | '); }
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
    const whites = [[s.RGB, [1, 1, 1]], [s.HSB, [0, 0, 1]], [s.HSL, [0, 0, 1]]];
    for (const [mode, triple] of whites) {
      s.colorMode(mode, 1);
      const c = s.color(triple[0], triple[1], triple[2]);
      if (!c) { wrong.push('no colour in mode ' + mode); continue; }
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
    if (wrong.length) { throw wrong.join(' | '); }
    return 'ok';
  }],
  ['color', 'lightness and paletteLerp', function (s) {
    s.colorMode(s.RGB, 255);
    const white = s.color(255, 255, 255);
    const black = s.color(0, 0, 0);
    if (typeof s.lightness !== 'function') { throw 'lightness absent'; }
    const light = s.lightness(white);
    if (!(light > 90)) { throw 'lightness of white = ' + light; }
    if (typeof s.paletteLerp === 'function') {
      const mid = s.paletteLerp([[black, 0], [white, 1]], 0.5);
      const r = s.red(mid);
      if (!(r > 100 && r < 155)) { throw 'paletteLerp midpoint red = ' + r; }
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
    s.vertex(2, 2); s.vertex(22, 2); s.vertex(22, 22); s.vertex(2, 22);
    s.beginContour();
    s.vertex(8, 8); s.vertex(8, 16); s.vertex(16, 16); s.vertex(16, 8);
    s.endContour();
    s.endShape(s.CLOSE);
    s.loadPixels();
    const at = (x, y) => {
      const i = 4 * (y * s.width + x);
      return [s.pixels[i], s.pixels[i + 1], s.pixels[i + 2]].join(',');
    };
    if (at(4, 4) !== '0,0,0') { throw 'the shape did not fill: ' + at(4, 4); }
    if (at(12, 12) !== '255,255,255') { throw 'the contour did not leave a hole: ' + at(12, 12); }
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
    if (!(mid > 10 && mid < 20)) { wrong.push('bezierPoint(0.5) = ' + mid); }
    if (typeof s.bezierTangent !== 'function') { wrong.push('bezierTangent absent'); }
    if (wrong.length) { throw wrong.join(' | '); }
    return 'ok';
  }],
  ['shape', 'splines', function (s) {
    // p5 2.x renamed curve* to spline*. The old names are gone, so a sketch
    // written for 2.x has only these.
    const wrong = [];
    for (const name of ['spline', 'splineVertex', 'splinePoint', 'splineTangent',
                        'splineProperty']) {
      if (typeof s[name] !== 'function') { wrong.push(name + ' absent'); }
    }
    if (wrong.length) { throw wrong.join(' | '); }
    s.background(255);
    s.noFill();
    s.stroke(0);
    s.beginShape();
    s.splineVertex(2, 20); s.splineVertex(8, 4); s.splineVertex(16, 20); s.splineVertex(22, 4);
    s.endShape();
    s.loadPixels();
    // SOMETHING was drawn: a spline that silently drew nothing is the failure
    // here, and one dark pixel anywhere in the band rules it out.
    let dark = 0;
    for (let x = 0; x < 24; x++) {
      for (let y = 0; y < 24; y++) {
        const i = 4 * (y * s.width + x);
        if (s.pixels[i] < 128) { dark++; }
      }
    }
    if (dark === 0) { throw 'the spline drew nothing'; }
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
    s.rect(0, 0, 24, 24);   // clipped to the top-left 8x8
    s.loadPixels();
    const at = (x, y) => s.pixels[4 * (y * s.width + x)];
    if (at(4, 4) !== 0) { throw 'the clipped fill did not draw: ' + at(4, 4); }
    if (at(16, 16) !== 255) { throw 'the fill escaped the clip: ' + at(16, 16); }
    return 'ok';
  }],

  ['image', 'blend()', function (s) {
    // p5's blend() copies a region from one surface to another THROUGH a
    // globalCompositeOperation, so it is the composite path reached from a
    // different direction.
    if (typeof s.blend !== 'function') { throw 'blend absent'; }
    s.background(128, 64, 32);
    const g = s.createGraphics(8, 8);
    g.background(64, 192, 96);
    s.blend(g, 0, 0, 8, 8, 0, 0, 8, 8, s.MULTIPLY);
    s.loadPixels();
    const i = 4 * (2 * s.width + 2);
    const got = [s.pixels[i], s.pixels[i + 1], s.pixels[i + 2]].join(',');
    if (got !== '32,48,12') { throw 'multiply blend gave ' + got + ' want 32,48,12'; }
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
    if (typeof s.createImg !== 'function') { throw 'createImg absent'; }
    const el = s.createImg('../assets/sprites.bmp', 'sprites');
    if (!el || !el.elt) { throw 'no element'; }
    if (el.elt.tagName !== 'IMG') { throw 'tagName=' + el.elt.tagName; }
    el.remove();
    return 'ok';
  }],
  ['dom', 'createColorPicker and createFileInput', function (s) {
    const wrong = [];
    if (typeof s.createColorPicker === 'function') {
      const picker = s.createColorPicker('#ff0000');
      if (!picker || !picker.elt) { wrong.push('createColorPicker gave nothing'); }
      else {
        if (picker.elt.type !== 'color') { wrong.push('type=' + picker.elt.type); }
        picker.remove();
      }
    } else { wrong.push('createColorPicker absent'); }
    if (typeof s.createFileInput === 'function') {
      const input = s.createFileInput(function () {});
      if (!input || !input.elt) { wrong.push('createFileInput gave nothing'); }
      else {
        if (input.elt.type !== 'file') { wrong.push('file type=' + input.elt.type); }
        input.remove();
      }
    } else { wrong.push('createFileInput absent'); }
    if (wrong.length) { throw wrong.join(' | '); }
    return 'ok';
  }],
  ['dom', 'removeElements', function (s) {
    if (typeof s.removeElements !== 'function') { throw 'removeElements absent'; }
    s.createDiv('one');
    s.createDiv('two');
    s.removeElements();
    return 'ok';
  }],

  ['io', 'getURL/getURLParams/getURLPath', function (s) {
    const wrong = [];
    if (typeof s.getURL() !== 'string') { wrong.push('getURL=' + typeof s.getURL()); }
    if (!Array.isArray(s.getURLPath())) { wrong.push('getURLPath is not an array'); }
    if (typeof s.getURLParams() !== 'object') { wrong.push('getURLParams is not an object'); }
    if (wrong.length) { throw wrong.join(' | '); }
    return 'ok';
  }],
  ['io', 'loadBytes', function (s) {
    return s.loadBytes('probe-lines.txt').then(function (bytes) {
      // p5 2.x hands back the array itself rather than { bytes: [...] }.
      const data = bytes && bytes.bytes ? bytes.bytes : bytes;
      if (!data || data.length !== 13) { throw 'length=' + (data && data.length); }
      if (data[0] !== 111) { throw "first byte is not 'o': " + data[0]; }
      return 'ok';
    });
  }],
  ['io', 'loadXML', function (s) {
    return s.loadXML('probe.xml').then(function (xml) {
      if (!xml) { throw 'no document'; }
      const items = xml.getChildren('item');
      if (!items || items.length !== 2) { throw 'children=' + (items && items.length); }
      if (items[0].getContent() !== 'first') { throw 'content=' + items[0].getContent(); }
      return 'ok';
    });
  }],
  ['io', 'httpGet', function (s) {
    if (typeof s.httpGet !== 'function') { throw 'httpGet absent'; }
    return s.httpGet('probe-data.json', 'json').then(function (data) {
      if (!data || data.n !== 4) { throw 'parsed=' + JSON.stringify(data); }
      return 'ok';
    });
  }],
  ['io', 'saveTable', function (s) {
    if (typeof s.saveTable !== 'function') { throw 'saveTable absent'; }
    const t = new s.constructor.Table();
    t.addColumn('a');
    const row = t.addRow();
    row.setString('a', 'x');
    s.saveTable(t, 'probe-table-out.csv');
    return 'ok';
  }],
  ['io', 'writeFile and createWriter', function (s) {
    const wrong = [];
    if (typeof s.writeFile !== 'function') { wrong.push('writeFile absent'); }
    if (typeof s.createWriter !== 'function') { wrong.push('createWriter absent'); }
    else {
      const w = s.createWriter('probe-writer.txt');
      if (!w || typeof w.write !== 'function') { wrong.push('createWriter gave no writer'); }
      else {
        w.write(['hello']);
        w.close();
      }
    }
    if (wrong.length) { throw wrong.join(' | '); }
    return 'ok';
  }],

  ['env', 'print and noCanvas', function (s) {
    const wrong = [];
    if (typeof s.print !== 'function') { wrong.push('print absent'); }
    else { s.print('probe'); }
    if (typeof s.noCanvas !== 'function') { wrong.push('noCanvas absent'); }
    if (wrong.length) { throw wrong.join(' | '); }
    return 'ok';
  }],

  // --- saving -------------------------------------------------------------
  //
  // Every p5 save() ends in downloadFile: a Blob, an object URL, an <a href
  // download> built entirely from script, and click(). A probe can only see that
  // it did not throw - whether a file appeared is tests/image_basics.cpp's
  // question, because only the embedder can see the disk.
  ['save', 'saveCanvas', function (s) {
    s.saveCanvas('probe-out', 'png');
    return 'ok';
  }],
  ['save', 'saveJSON', function (s) { s.saveJSON({ a: 1 }, 'probe.json'); return 'ok'; }],
  ['save', 'saveStrings', function (s) { s.saveStrings(['a', 'b'], 'probe.txt'); return 'ok'; }],
  ['save', 'downloadFile', function (s) {
    if (typeof s.downloadFile !== 'function') { throw 'absent'; }
    s.downloadFile('some text', 'probe-direct', 'txt');
    return 'ok';
  }],
  ['save', 'canvas.toDataURL', function (s) {
    var url = s.canvas.toDataURL();
    if (url.indexOf('data:image/png;base64,') !== 0) { throw 'url=' + url.slice(0, 40); }
    if (url.length < 100) { throw 'too short: ' + url.length; }
    return 'ok';
  }],

  // --- 3D (out of scope; the constructors must still exist) ---------------
  ['webgl', 'WEBGL renderer is constructible', function (s) {
    if (typeof s.constructor.renderers['webgl'] !== 'function') { throw 'not a function'; }
    return 'ok';
  }],
  // THE ENGINE'S GUARANTEE, asked of the engine rather than of p5: a canvas
  // refuses a webgl context out loud. Returning null - which is what a browser
  // does - is worse here, because p5 keeps the null and falls back to its 2D
  // renderer, so a WEBGL sketch drew nothing 3D and reported nothing.
  ['webgl', 'getContext(webgl) refuses', function (s) {
    try {
      document.createElement('canvas').getContext('webgl');
    } catch (e) {
      if (!/webgl/i.test(String(e && e.message))) { throw 'refused without naming webgl: ' + e; }
      return 'ok';
    }
    throw 'a webgl context was handed out, which this engine cannot honour';
  }],
  // KNOWN FAILING, and here to stay measured. As of p5 2.3.1 createCanvas(w, h,
  // WEBGL) does not ask for a webgl context at all - it selects a renderer that
  // reports itself as Renderer2D - so the refusal above is never reached and the
  // sketch degrades to 2D in silence. It used to throw, and stopped when a batch
  // of unrelated VM fixes let p5's renderer selection get further. WEBGL is out
  // of scope; a degradation inside the library is not something this engine can
  // observe, so this is a note in the queue rather than a claim that it is fine.
  ['webgl', 'createCanvas(WEBGL) refuses', function (s) {
    try {
      s.createCanvas(10, 10, s.WEBGL);
    } catch (e) {
      if (!/webgl/i.test(String(e && e.message))) { throw 'refused without naming webgl: ' + e; }
      return 'ok';
    }
    throw 'WEBGL was accepted and silently rendered 2D';
  }],
];

// The runner. Each probe gets the same sketch and a clean-ish canvas; a throw
// is recorded against its name and the next probe still runs, because the
// point is a LIST of what is broken rather than the first thing that is.
// ASYNC, because some probes are. A probe may return a promise - a loader
// does - and the runner awaits it, so a load that fails reports as that probe
// failing rather than as an unhandled rejection with no name attached.
globalThis.__runProbes = async function (sketch) {
  const passed = [];
  const failed = [];
  const skipped = [];
  for (const entry of globalThis.__probes) {
    const name = entry[0] + '/' + entry[1];
    // A fresh state for each, so one probe's fill or transform cannot make the
    // next one pass or fail.
    //
    // try/FINALLY, so the pop happens exactly once however the body leaves. An
    // earlier version popped in the catch as well, which on a throwing probe
    // unwound p5's state stack one level too far - and the NEXT five probes
    // then failed for reasons that had nothing to do with them. A harness whose
    // failures contaminate each other reports a work queue that is partly
    // fiction.
    sketch.push();
    try {
      sketch.resetMatrix();
      const out = await entry[2](sketch);
      if (out === 'SKIP') { skipped.push(name); } else { passed.push(name); }
    } catch (e) {
      failed.push(name + ': ' + (e && e.message ? e.message : String(e)));
    } finally {
      sketch.pop();
    }
  }
  passed.sort();
  failed.sort();
  skipped.sort();
  return JSON.stringify({ passed: passed, failed: failed, skipped: skipped });
};
