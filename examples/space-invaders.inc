R"ctb(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8"/>
<title>Space Invaders</title>
<style>
html,body{margin:0;padding:0;background:#000;color:#fff;font-family:monospace}
canvas{display:block;margin:0 auto}
</style>
</head>
<body>
<div id="container">
  <canvas width="900" height="700"></canvas>
  <div id="ui" class="active">
    <div id="title-screen" class="active">
      <div id="title">
        <div class="layer layer1">
          SPACE<br/>INVADERS
        </div>
        <div class="layer layer2">
          SPACE<br/>INVADERS
        </div>
        <div class="layer layer3">
          SPACE<br/>INVADERS
        </div>
      </div>
      <div id="portrait-warning">Best played in landscape</div>
      <div id="credits">
        <div>
          <p>------</p>
          <p>Arrow keys to move <br/>Shift, Space or Enter to shoot</p>
          <p>------</p>
          <div class="small">
            <p>2022 by John Pitchers</p>
            <p>Built with BabylonJS. 3D models created in Blender. Sound effects by Juhani Junkala</p>
            <p>All code and models on Github: https://github.com/johnpitchers/Space-Invaders</p>
          </div>
        </div>
      </div>
    </div>
    <div id="game-ui" class="">
      <div id="panel-game-over" class="">
        GAME OVER
      </div>
      <div id="panel-new-highscore" class="">
        **** NEW HIGH SCORE ****<br>
        <div class="value">0</div>
      </div>
      <div id="panel-game-hints">
        ---==Ξ <span class="lg">!</span> Ξ==---
        <div class="value"></div>
        ---==Ξ==---
      </div>
    </div>
  </div>
</div>
<div id="loading" class="active">LOADING<br/>...</div>
<div id="intro" class="">
  <p>Select mode<br/><select id="change-mode">
    <option value="0">Traditional 2D</option>
    <option value="1">2D oldschool CRT</option>
    <option value="2">Action cam 3D</option>
  </select></p>
  <p>
    <button id="start-game">START GAME</button>
  </p>
</div>
<div id="panel-play-again" class="">
  <button id="play-again">PLAY AGAIN</button>
</div>
<script>
const ActionManager = BABYLON.ActionManager;
const AssetContainer = BABYLON.AssetContainer;
const AssetsManager = BABYLON.AssetsManager;
const Axis = BABYLON.Axis;
const Camera = BABYLON.Camera;
const Color3 = BABYLON.Color3;
const Color4 = BABYLON.Color4;
const Engine = BABYLON.Engine;
const ExecuteCodeAction = BABYLON.ExecuteCodeAction;
const GlowLayer = BABYLON.GlowLayer;
const HemisphericLight = BABYLON.HemisphericLight;
const MeshBuilder = BABYLON.MeshBuilder;
const Scalar = BABYLON.Scalar;
const Scene = BABYLON.Scene;
const SceneLoader = BABYLON.SceneLoader;
const Sound = BABYLON.Sound;
const Space = BABYLON.Space;
const Sprite = BABYLON.Sprite;
const SpriteManager = BABYLON.SpriteManager;
const StandardMaterial = BABYLON.StandardMaterial;
const UniversalCamera = BABYLON.UniversalCamera;
const Vector3 = BABYLON.Vector3;
const AdvancedDynamicTexture = BABYLON.GUI.AdvancedDynamicTexture;
const Control = BABYLON.GUI.Control;
const Ellipse = BABYLON.GUI.Ellipse;
const Line = BABYLON.GUI.Line;
const Rectangle = BABYLON.GUI.Rectangle;
const Style = BABYLON.GUI.Style;
const TextBlock = BABYLON.GUI.TextBlock;
/* ===== module: ../spaceinvaders.config.js ===== */
const spaceinvadersConfig = {
  actionCam: false, // This value is changed dynamically in the code based on the users selected mode.
  light: {
    intensity: 1.5
  },
  motherShip: {
    interval: 20, //seconds
    rotateSpeed: 0.1,
    velocity: 0.75,
    fireRate: 5,
    hitsToKill: 4
  },
  oldSchoolEffects: {
    enabled: false, // This value is changed dynamically in the code based on the users selected mode.
    blurIntensity: 0.35,
    scanLines: true
  },
  starField: {
    speed: 0.1,
    minStarSize: 0.1,
    maxStarSize: 0.6,
    numberofStars: 500,
    ratioOfBlinkingStars: 0.3,
  },
  startingLevel: 0, // Starting level -1;
  startingLives: 2,
}
const config = spaceinvadersConfig;
/* ===== module: State.js ===== */
const State = {
  state: "TITLESCREEN",
  level: 0,
  lives: 3,
  score: 0,
  defaultFPS:60,
  FPS: 60, // FPS and delta are updated every frame.
  delta: 1
}
/* ===== module: Environment.js ===== */
class Environment {

  clearColor = new Color3(0.01, 0.03, 0.1);

  constructor(engine) {
    this.isComplete = false;
    this.canvas = document.querySelector('canvas');
    this.engine = engine;
    this.scene = this.CreateScene();
    this.initLightsAndCamera();
    this.oldSchoolEffects();
    if (spaceinvadersConfig.actionCam) {
      this.createFog();
    }
    this.isComplete = true;
  }

  CreateScene() {
    const scene = new Scene(this.engine);
    if (spaceinvadersConfig.oldSchoolEffects.enabled) {
      this.clearColor = new Color3(0.05, 0.05, 0.15);
    }
    scene.clearColor = this.clearColor.toColor4(1);
    scene.collisionsEnabled = true;
    return scene;
  }

  oldSchoolEffects() {
    if (!spaceinvadersConfig.oldSchoolEffects.enabled) return;
    let glow = new GlowLayer("glow", this.scene);
    glow.intensity = spaceinvadersConfig.oldSchoolEffects.blurIntensity;
    glow.customEmissiveColorSelector = function (mesh, subMesh, material, result) {
      result.set(1, 1, 1, 0.5);
    };
    if (spaceinvadersConfig.oldSchoolEffects.scanLines) {
      document.querySelector('body').classList.add('scanlines');
    }
  }

  createFog() {
    this.scene.fogEnabled = true;
    this.scene.fogColor = this.clearColor;
    this.scene.fogMode = Scene.FOGMODE_LINEAR;
    this.scene.fogStart = 200;
    this.scene.fogEnd = 400;
    if (spaceinvadersConfig.actionCam) {
      this.scene.fogStart = 140;
      this.scene.fogEnd = 160;
    }
    this.scene.fogDensity = 0.1;
  }

  initLightsAndCamera() {
    this.camera = this.CreateCamera();
    this.light = new HemisphericLight('light', new Vector3(0, 0.5, -1), this.scene);
    this.light.intensity = 1;
  }

  actionCam(x) {
    if (spaceinvadersConfig.actionCam) {
      this.camera.position.x = Scalar.Lerp(this.camera.position.x, x, 0.1 * State.delta);
      this.camera.rotation.y = Scalar.Lerp(this.camera.rotation.y, -this.camera.position.x / 300, 0.05 * State.delta);
    }
  }

  CreateCamera() {
    let camera;
    if (spaceinvadersConfig.actionCam) {
      //camera = new UniversalCamera("camera", new Vector3(0, -60, -60), this.scene);
      camera = new UniversalCamera("camera", new Vector3(0, -35, -22), this.scene);
    } else {
      camera = new UniversalCamera("camera", new Vector3(0, 50, -150), this.scene);
    }
    if (spaceinvadersConfig.oldSchoolEffects.enabled) {
      let orthoSize = 60;
      camera.mode = Camera.ORTHOGRAPHIC_CAMERA;
      camera.orthoTop = orthoSize;
      camera.orthoBottom = -orthoSize;
      this.setOrthographicRatio(camera, orthoSize);

      // resize on canvas resize
      this.engine.onResizeObservable.add(() => {
        this.setOrthographicRatio(camera, orthoSize);
      });
    }
    if (spaceinvadersConfig.actionCam) {
      camera.setTarget(new Vector3(0, 30, 0));
    } else {
      camera.setTarget(new Vector3(0, 50, 0));
    }
    return camera;
  }

  setOrthographicRatio(camera, orthoSize) {
    camera.width = this.engine.getRenderWidth();
    camera.height = this.engine.getRenderHeight();
    camera.ratio = camera.width / camera.height;
    camera.orthoLeft = camera.ratio * -orthoSize;
    camera.orthoRight = camera.ratio * orthoSize;
  }
}
/* ===== module: DeltaTime.js ===== */
// This class averages out the framerate over a
// specified number of frames. Creates a much smoother
// experience. FPS and delta is written to the State
// every frame.



class DeltaTime {
  lastRenderTime = 0;
  FPSarray = [];
  arraySize = 60; // Higher number for smoother results.

  constructor(scene) {
    this.loop = scene.onBeforeRenderObservable.add(() => {
      let timeNow = Date.now();
      let ms = timeNow - this.lastRenderTime;
      this.lastRenderTime = timeNow;
      let FPS = 1000 / ms;
      this.FPSarray.push(FPS);
      if (this.FPSarray.length > this.arraySize) this.FPSarray.shift();

      State.FPS = this.average(this.FPSarray);
      State.delta = State.defaultFPS / State.FPS;
    });
  }

  average(arr) {
    return arr.reduce((a, b) => a + b) / arr.length;
  }
}
/* ===== module: AliensController.js ===== */
class AliensController {

  static _formation = {
    x: 0,
    y: 75
  };

  constructor(scene) {
  }

  get formation() {
    return AliensController._formation;
  }

  set formation(newformation) {
    AliensController._formation = newformation;
  }

  recenterFormation() {
    AliensController._formation = {
      x: 0,
      y: 75
    }
  }
}
/* ===== module: Alien.js ===== */
class Alien extends AliensController {

  constructor(scene, mesh = null, x = 0, y = 0) {
    super();
    this.sourceMesh = mesh;
    this.x = x;
    this.y = y;
    this.z = 0;
    this.scene = scene;
    this.initAlien();
  }

  initAlien() {
    if (this.x === 0 && this.y === 0) {
      this.x = Math.random() * 3000 - 1500;
      this.y = Math.random() * 3000 + 70;
    }
    this.randomiser = [
      Math.floor(Math.random() * 100),
      Math.floor(Math.random() * 100),
      Math.floor(Math.random() * 100),
      Math.floor(Math.random() * 100)
    ];
    let id = Math.floor(Math.random() * 100000).toString(16);
    if (!this.sourceMesh) {
      this.mesh = new MeshBuilder.CreateBox("alien-" + id, {
        width: 4,
        height: 3,
        depth: 2,
      }, this.scene);
    } else{
      this.mesh = this.sourceMesh.clone("alien-" + id);
    }
    this.mesh.name = "alien-" + id;
    this.mesh.position.x = this.x;
    this.mesh.position.y = this.y;
    this.mesh.position.z = this.z;
    this.mesh.checkCollisions = true;
    this.mesh.collisionGroup = 2;
    this.mesh.collisionMask = 17;
    this.mesh.metadata = {
      type: "alien",
      scoreValue: 10
    };
  }

  updateMeshPosition(v = 0.05) {
    let currentPosition = this.mesh.position;
    let newPosition = {
      x: this.formation.x + this.x,
      y: this.formation.y + this.y
    }
    //this.mesh.position.x = Scalar.Lerp(currentPosition.x, newPosition.x, v);
    //this.mesh.position.y = Scalar.Lerp(currentPosition.y, newPosition.y, v);
    let newX = Scalar.Lerp(currentPosition.x, newPosition.x, v);
    let newY = Scalar.Lerp(currentPosition.y, newPosition.y, v);
    let scalarX = newX - currentPosition.x;
    let scalarY = newY - currentPosition.y;
    this.mesh.moveWithCollisions(new Vector3(scalarX, scalarY, currentPosition.z));
    if (this.mesh.collider.collidedMesh) {
      this.handleCollision();
    }

    return newPosition;
  }

  handleCollision() {
    let collidedWithType = (this.mesh.collider.collidedMesh.metadata).type;
    if (collidedWithType === "player") {
      this.mesh.collider.collidedMesh.dispose(); // perform action with player meshes onDispose event.
      this.mesh.dispose();
    }
    if (collidedWithType === "barrier") {
      this.mesh.collider.collidedMesh.dispose(); // perform action with meshes onDispose event.
    }
  }
}
/* ===== module: AlienBullet.js ===== */
class AlienBullet {

  constructor(scene, playerMesh) {
    this.scene = scene;
    this.minY = -30;
    this.offset = -2;
    this.bulletSpeed = -0.5;
    this.bullet = new MeshBuilder.CreateBox("bullet", {
      width: 0.5,
      height: 3,
      depth: 1
    }, this.scene);
    this.bullet.position = new Vector3(
      playerMesh.position.x,
      playerMesh.position.y + this.offset,
      playerMesh.position.z
    );
    let bulletArc = Math.PI/6;
    this.bullet.rotate(new Vector3(0, 0, 1), (Math.random() * bulletArc) - bulletArc/2);
    this.bullet.checkCollisions = true;
    this.bullet.collisionGroup = 8;
    this.bullet.collisionMask = 17;

    this.startBulletLoop();
  }

  startBulletLoop() {
    this.bulletObserver = this.scene.onBeforeRenderObservable.add(() => {
      let moveVector = this.bullet.calcMovePOV(0, this.bulletSpeed * State.delta, 0);
      this.bullet.moveWithCollisions(moveVector);
      if (this.bullet.position.y < this.minY) {
        this.destroyBullet();
      }
      if (this.bullet.collider.collidedMesh) {
        this.handleCollision();
      }

    });
  }

  handleCollision() {
    let collidedWithType = (this.bullet.collider.collidedMesh.metadata).type;
    if (collidedWithType === "player") {
      this.bullet.collider.collidedMesh.dispose(); // perform action with player meshes onDispose event.
      this.destroyBullet();
    }
    if (collidedWithType === "barrier") {
      this.bullet.collider.collidedMesh.dispose();
      this.destroyBullet();
    }
  }

  destroyBullet() {
    this.scene.onBeforeRenderObservable.remove(this.bulletObserver);
    this.bullet.dispose();
    this.disposed = true; // Tells our game loop to destroy this instance.
  }
}
/* ===== module: Explosion.js ===== */
class Explosion {

  constructor(mesh, numParticles = 20, maxSize, scene) {
    this.particles = {};
    this.position = mesh.position;
    this.numParticles = numParticles;
    this.scene = scene;
    this.maxSize = maxSize;
    this.createParticles();
  }

  createParticles() {
    for (let i = 0; i < this.numParticles; i++) {
      this.createParticle();
    }
  }

  createParticle() {
    let id = "p" + Math.floor(Math.random() * 1000000).toString(16);
    //let color = new Color4(1,1,0,1);
    let color = new Color4(1, 1, Math.random(), 1);
    let minSize = this.maxSize * 0.33;
    let mesh = MeshBuilder.CreateBox("ExplosionParticle" + id, {
      //size: Math.random() * (this.maxSize - minSize) + minSize,
      size: this.maxSize/1.5,
      faceColors: [color, color, color, color, color, color]
    }, this.scene);

    let particle = {
      id: id,
      mesh: mesh,
      velocity: Math.random() / 3,
      movementVector: new Vector3(Math.random() - 0.5, Math.random() - 0.5, Math.random() - 0.5),
      rotationVector: new Vector3(Math.random(), Math.random(), Math.random()),
      rotationVelocity: Math.random() / 20,
      duration: Math.floor(Math.random() * 120)
    }
    particle.mesh.position = new Vector3(
      this.position.x,
      this.position.y,
      this.position.z
    );
    // Rotate it in a random direction and move
    // it out. Move out distance is based on
    // the number of particles and velocity.
    if (!spaceinvadersConfig.oldSchoolEffects.enabled) {
      particle.mesh.rotation = new Vector3(Math.random() * Math.PI * 2, Math.random() * Math.PI * 2, Math.random() * Math.PI * 2);
    }
    particle.mesh.translate(particle.movementVector, particle.velocity * this.numParticles / 1.5, Space.WORLD);
    particle.loopFunction = particle.mesh.onBeforeRenderObservable.add(() => {
      this.move(particle);
    });
    this.particles[id] = particle;
  }

  move(particle) {
    particle.duration -= State.delta;
    // Fade it out at the end of it's duration
    if (particle.duration < 20) {
      particle.mesh.visibility = particle.duration / 20;
    }
    // hide it completely. For some reason, mesh.dispose causes flickering and other meshes to be disposed.
    if (particle.duration < 1 && particle.mesh.isVisible) {
      particle.mesh.onBeforeRenderObservable.remove(particle.loopFunction);
      particle.mesh.visibility = 0;
    }
    // Delete it.
    if (particle.mesh.visibility === 0) {
      delete this.particles[particle.id];
    } else {
      particle.mesh.translate(particle.movementVector, particle.velocity, Space.WORLD);
      if (!spaceinvadersConfig.oldSchoolEffects.enabled) {
        particle.mesh.rotate(particle.rotationVector, particle.rotationVelocity);
      }
    }
  }
}
/* ===== module: Barrier.js ===== */
class Barrier {

  constructor(gameAssets, scene, position) {
    this.scene = scene;
    this.gameAssets = gameAssets;
    this.position = position;
    this.bricks = [];
    this.params = {
      brickSize: {
        x: 1,
        y: 1
      },
      columns: 14,
      rows: 7
    }

    this.buildBarrier();
  }

  buildBarrier() {
    let rootMesh = MeshBuilder.CreateBox('brick', {
      width: this.params.brickSize.x,
      height: this.params.brickSize.y,
      depth: 4
    }, this.scene);
    rootMesh.position = new Vector3(0, -300, 0);
    rootMesh.isVisible = false;
    rootMesh.checkCollisions = true;
    rootMesh.collisionGroup = 16;
    rootMesh.collisionResponse = false;
    rootMesh.collisionRetryCount = 10;
    rootMesh.metadata = {
      type: "barrier"
    }
    rootMesh.material = new StandardMaterial("brick_mesh", this.scene);
    //rootMesh.material.disableLighting = true;
    rootMesh.material.diffuseColor = new Color3(0.9, 0.8, 0.6);
    rootMesh.material.specularColor = new Color3(0.9, 0.8, 0.6);
    rootMesh.registerInstancedBuffer("color", 3);
    rootMesh.instancedBuffers.color = new Color3(0.9, 0.8, 0.6);

    this.bricks.push(rootMesh);

    for (let rc = 0; rc < this.params.rows; rc++) {
      for (let cc = 0; cc < this.params.columns; cc++) {
        // Omit certain blocks to create the arch
        if (rc === 2 && cc > 3 && cc < this.params.columns - 4) {
          continue;
        }
        if (rc < 2 && cc > 2 && cc < this.params.columns - 3) {
          continue;
        }
        if (rc === this.params.rows - 1 && (cc === 0 || cc === this.params.columns - 1)) {
          continue;
        }

        let x = (cc * this.params.brickSize.x) - ((this.params.columns - 1) / 2 * this.params.brickSize.x);
        let y = (rc * this.params.brickSize.y) - ((this.params.rows - 1) / 2 * this.params.brickSize.y);

        let brick = rootMesh.createInstance("i" + cc + rc);

        brick.position = new Vector3(
          this.position.x + x,
          this.position.y + y,
          this.position.z
        );
        brick.checkCollisions = true;
        brick.collisionGroup = 16;
        brick.collisionResponse = false;
        brick.collisionRetryCount = 10;
        brick.metadata = {
          type: "barrier"
        }
        let color = Scalar.RandomRange(0.8, 1);
        //brick.material.diffuseColor = new Color3(color,color,color);
        brick.instancedBuffers.color = new Color3(color, color, color / 2);

        brick.onDispose = (mesh) => {
          this.onDisposeMesh(mesh);
        };
        this.bricks.push(brick);
      }
    }
  }

  onDisposeMesh(mesh) {
    new Explosion(mesh, 2, 1, this.scene);
    if (State.state !== "CLEARLEVEL" && State.state !== "GAMEOVER") {
      this.gameAssets.sounds.alienExplosion.play(0, 0.1, 1);
    }
  }

  destroyBarrierMesh() {
    //Destroy random mesh but not the root [0]
    let index = Math.floor(Math.random() * (this.bricks.length - 1)) + 1;
    this.bricks[index].dispose();
    this.bricks.splice(index, 1);
    State.score += 1;

    if (this.bricks.length === 1) {
      //Destroy the root mesh
      this.bricks[0].dispose();
      this.bricks = [];
    }
  }


  sleep(ms) {
    let date = new Date();
    let curDate = null;
    do {
      curDate = new Date();
    }
    while (curDate - date < ms);
  }
}
/* ===== module: MotherShip.js ===== */
class MotherShip {

  constructor(gameAssets, scene) {
    this.enabled = false;
    this.active = false;
    this.lastactivated = Math.floor(Date.now() / 1000) - 1;
    this.gameAssets = gameAssets;
    this.interval = spaceinvadersConfig.motherShip.interval;
    this.fireRate = spaceinvadersConfig.motherShip.fireRate;
    this.hitsToKill = spaceinvadersConfig.motherShip.hitsToKill;
    this.velocity = spaceinvadersConfig.motherShip.velocity;
    this.scene = scene;
    this.bullets = [];
    this.enable();
  }

  initMotherShipMesh() {
    this.mesh = this.gameAssets.clone("MotherShip");
    this.mesh.position = new Vector3(-3000, 100, 0);
    this.mesh.metadata = {
      type: "mothership",
      scoreValue: 1000,
      lives: this.hitsToKill,
      frameCounter: 0 // Used in orthographic camera mode
    }
    this.mesh.onDispose = (mesh) => {
      if (this.mesh.metadata.silentDispose === undefined) {
        State.score += mesh.metadata.scoreValue;
        new Explosion(mesh, 60, 1.5, this.scene);
        this.gameAssets.sounds.motherShipExplosion.play();
      }
      this.gameAssets.sounds.motherShip.stop();
      this.deactivate();
    }
    this.mesh.checkCollisions = true;
    this.mesh.collisionGroup = 2;
  }

  fireBullets() {
    if (!this.active || State.state !== "GAMELOOP") return;
    let firenow = Math.random() * (60 / State.delta) < this.fireRate;
    if (firenow) {
      this.bullets.push(new AlienBullet(this.scene, this.mesh));
      this.gameAssets.sounds.alienBullet.play();
    }
  }

  enable() {
    if (this.enabled) return;
    this.enabled = true;
    this.activatedTimer = this.scene.onBeforeRenderObservable.add(() => {
      if (!this.active) {
        let now = Math.floor(Date.now() / 1000);
        if (now > this.lastactivated + this.interval) {
          this.activate();
        }
      }
      this.fireBullets();
    });
  }

  disable() {
    if (!this.enabled) return;
    this.scene.onBeforeRenderObservable.remove(this.activatedTimer);
    if (this.mesh) this.mesh.position = new Vector3(-3000, 100, 0);
    this.enabled = false;
    this.active = false;
  }

  activate() {
    if (State.state !== "GAMELOOP") return;
    this.initMotherShipMesh();
    this.mesh.position = new Vector3(-100, 100, 0);
    this.startLoopArc();
    this.active = true;
  }

  moveOffScreen() {
    if (!this.active) return false;
    let posY = this.mesh.position.y;
    if (posY < 150) {
      this.mesh.position.y = Scalar.Lerp(posY, 160, 0.015);
      return true;
    }
    return false;
  }

  destroyMotherShip() {
    this.deactivate();
    this.disable();
    if (this.mesh) {
      this.mesh.metadata.silentDispose = true;
      this.mesh.dispose();
    }
  }

  startLoopArc() {
    this.gameAssets.sounds.motherShip.play();
    let direction = Math.random() < 0.5 ? "leftToRight" : "rightToLeft";
    let center = {
      x: 0,
      y: 125
    }
    let radius = {
      x: 100,
      y: 27
    };
    let i = 0;
    this.mothershipLoop = this.scene.onBeforeRenderObservable.add(() => {
      i += (this.velocity / 250) * State.delta;
      if (spaceinvadersConfig.oldSchoolEffects.enabled) {
        // If orthographic camera is in use,
        // rotate the mesh just once every 15 frames
        // to create an old school animation.
        this.mesh.metadata.frameCounter ++
        if (this.mesh.metadata.frameCounter % 15 === 0) {
           this.mesh.rotate(Axis.Y, this.mesh.rotation.y+Math.PI / 8, Space.LOCAL);
        }
      } else {
        this.mesh.rotate(Axis.Y, this.mesh.rotation.y + (spaceinvadersConfig.motherShip.rotateSpeed * State.delta), Space.LOCAL);
      }
      let pos = {
        x: center.x + Math.cos(i) * radius.x,
        y: center.y - Math.sin(i) * radius.y
      }
      if (State.state === "CLEARLEVEL") {
        pos.y = this.mesh.position.y;
      }
      if (direction === "leftToRight") {
        this.mesh.position = new Vector3(-pos.x, pos.y, 0);
      } else { // rightToLeft
        this.mesh.position = new Vector3(pos.x, pos.y, 0);
      }
      if (i > Math.PI) {
        this.gameAssets.sounds.motherShip.stop();
        this.deactivate();
        this.mesh.metadata.silentDispose = true;
        this.mesh.dispose();
      }
    });
  }

  deactivate() {
    this.scene.onBeforeRenderObservable.remove(this.mothershipLoop);
    this.active = false;
    this.lastactivated = Math.floor(Date.now() / 1000);
  }
}
/* ===== module: AlienFormationController.js ===== */
class AlienFormationController {

  defaultParams = {
    columns: 5,
    rows: 5,
    spacing: {
      x: 9,
      y: 8
    },
    formationAnimInterval: 1500, //1500 is good start value
    formationAnimSpeed: 0.2,
    fireRate: 1, // Avg bullets per second
    numBarriers: 3,
    alien1Lives: 0,
    alien2Lives: 0,
    alien3Lives: 0
  }
  minX = -45;
  maxX = 45;
  minY = -6;
  aliens = [];
  bullets = [];
  barriers = [];

  constructor(scene, gameAssets = null) {
    this.scene = scene;
    this.gameAssets = gameAssets;
    this.aliensController = new AliensController(scene);
    this.motherShip = new MotherShip(this.gameAssets, scene);
    this.direction = Math.random() < 0.5 ? "left" : "right";
    this.movementStarted = false;
    this.levelParams = this.levelFormationAlgorithm();
    this.level = State.level;
    this.buildFormation();
    this.buildBarriers(this.levelParams.numBarriers);
    this.setLoop();
  }

  buildBarriers(numBarriers) {
    let posy = 15;
    let totalWidth = (this.maxX - this.minX);
    let distance = totalWidth / (numBarriers - 1);
    let posx = this.minX;
    for (let i = 0; i < numBarriers; i++) {
      let pos = new Vector3(posx, posy, 0);
      let barrier = new Barrier(this.gameAssets, this.scene, pos);
      this.barriers.push(barrier);
      posx += distance;
    }
  }

  destroyBarriers() {
    let index = 0;
    for (let barrier of this.barriers) {
      barrier.destroyBarrierMesh();
      if (!barrier.bricks.length) {
        this.barriers.splice(index, 1);
        break;
      }
      index++;
    }
  }

  buildFormation() {
    let alien1 = this.gameAssets.clone("Alien_1");
    let alien2 = this.gameAssets.clone("Alien_2");
    let alien3 = this.gameAssets.clone("Alien_3");
    let alien = {};
    for (let rc = 0; rc < this.levelParams.rows; rc++) {
      for (let cc = 0; cc < this.levelParams.columns; cc++) {
        let x = (cc * this.levelParams.spacing.x) - ((this.levelParams.columns - 1) / 2 * this.levelParams.spacing.x);
        let y = (rc * this.levelParams.spacing.y) - ((this.levelParams.rows - 1) / 2 * this.levelParams.spacing.y);
        if (rc < 2) {
          alien = new Alien(this.scene, alien1);
          alien.mesh.metadata.lives = this.levelParams.alien1Lives ?? 0;
          alien.mesh.metadata.scoreValue = 10;
        } else if (rc < 4) {
          alien = new Alien(this.scene, alien2);
          alien.mesh.metadata.lives = this.levelParams.alien2Lives ?? 0;
          alien.mesh.metadata.scoreValue = 20;
        } else {
          alien = new Alien(this.scene, alien3);
          alien.mesh.metadata.lives = this.levelParams.alien3Lives ?? 0;
          alien.mesh.metadata.scoreValue = 30;
        }

        // Scaling
        alien.mesh.scaling = new Vector3(1.7 - (rc * 0.14), 1.7 - (rc * 0.14), 4 - (rc * 0.14));

        alien.x = x;
        alien.y = y;
        alien.id = alien.mesh.id;
        alien.mesh.onDispose = (mesh) => {
          if (State.state !== "GAMEOVER") {
            this.onDestroyMesh(mesh);
          }
        };
        this.aliens.push(alien);
      }
    }
    this.alienCount = this.aliens.length;
    State.formation = 1;
  }

  onDestroyMesh(mesh) {
    // destroy the alien object.
    let index = 0
    for (let a of this.aliens) {
      if (a.id === mesh.id) {
        State.score += mesh.metadata.scoreValue;

        // Bigger aliens have bigger explosions
        new Explosion(mesh, 20 * mesh.scaling.x, mesh.scaling.x / 1.5, this.scene);
        this.gameAssets.sounds.alienExplosion.play();
        this.aliens.splice(index, 1);
      }
      index++;
    }
    this.alienCount = this.aliens.length;
  }

  setLoop() {
    this.formationAnimInterval = this.levelParams.formationAnimInterval;
    this.formationAnimSpeed = this.levelParams.formationAnimSpeed / 2;
    this.formationAnimTick = setTimeout(() => {
      this.formationAnimSpeed = this.levelParams.formationAnimSpeed;
      this.moveFormation();
      this.movementStarted = true;
    }, 3000);
    this.formationObserver = this.scene.onBeforeRenderObservable.add(() => {
      this.updateAlienMeshPositions();
      if (this.movementStarted) {
        this.fireBullets();
      }
    }, 1);
  }

  fireBullets() {
    if (!this.aliens.length) return;
    let firenow = Math.random() * (60 / State.delta) < this.levelParams.fireRate;
    if (firenow) {
      let randAlien = this.aliens[Math.floor(Math.random() * this.aliens.length)];
      let randAlienMesh = randAlien.mesh;
      this.bullets.push(new AlienBullet(this.scene, randAlienMesh));
      this.gameAssets.sounds.alienBullet.play();
    }
  }

  updateAlienMeshPositions() {
    for (const alien of this.aliens) {
      let alienPosition = alien.updateMeshPosition(this.formationAnimSpeed);
      this.changeDirectionIfAtEdge(alienPosition);
      this.checkCollisionWithGround(alienPosition);
    }
  }

  checkCollisionWithGround(position) {
    if (position.y < this.minY) {
      this.scene.onBeforeRenderObservable.remove(this.formationObserver);
      State.state = "ALIENSWIN";
    }
  }

  moveFormation() {
    if (this.direction === "right") {
      this.aliensController.formation.x += 5;
    }
    if (this.direction === "left") {
      this.aliensController.formation.x -= 5;
    }
    if (this.direction === "down") {
      // If the player has died no need to move further
      // down during the delay before loading the next level
      if (State.state !== "GAMEOVER") {
        this.aliensController.formation.y -= 5;
      }
      this.direction = this.nextDirection;
    }
    //this.formationAnimSpeed = Scalar.Lerp(this.formationAnimSpeed, 0.8, 0.013);
    if (State.state === "GAMELOOP" && this.aliens.length) {
      this.gameAssets.sounds.alienMove.play();
      this.formationAnimInterval = Scalar.Lerp(this.formationAnimInterval, 100, 0.025);
    }
    this.formationAnimTick = setTimeout(() => {
      this.moveFormation();
    }, this.formationAnimInterval);
  }

  changeDirectionIfAtEdge(position) {
    if (position.x > this.maxX && this.direction === "right") {
      this.direction = "down";
      this.nextDirection = "left";
    }
    if (position.x < this.minX && this.direction === "left") {
      this.direction = "down";
      this.nextDirection = "right";
    }
  }

  clearScene() {
    this.scene.onBeforeRenderObservable.remove(this.formationObserver);
    clearTimeout(this.formationAnimTick);
    this.aliensController.recenterFormation();
  }

  levelFormationAlgorithm() {
    let levelParams = this.defaultParams;

    // Alien grid gets bigger each level up to a max size
    const maxColumns = 10;
    const maxRows = 6;
    levelParams.columns = this.defaultParams.columns + State.level - 1;
    levelParams.rows = this.defaultParams.rows + State.level - 1;
    if (levelParams.columns > maxColumns) levelParams.columns = maxColumns;
    if (levelParams.rows > maxRows) levelParams.rows = maxRows;

    // Fire rate increases every level by a multiple;
    const fireRateIncreaseMultiple = 1.2;
    levelParams.fireRate = this.defaultParams.fireRate * Math.pow(fireRateIncreaseMultiple, State.level - 1);

    // Increase the number of barriers;
    // Additional barrier every 2 levels
    // up to a maximum
    const maxBarriers = 6;
    levelParams.numBarriers = Math.floor(this.defaultParams.numBarriers + (State.level / 2) - 0.5);
    if (levelParams.numBarriers > maxBarriers) levelParams.numBarriers = maxBarriers;

    // Give aliens lives, so they take more than one hit to die.
    // From Level 4 Alien1 gets 1 life increasing by 1 every 3 levels.
    // From Level 6 Alien2 gets 1 life increasing by 1 every 3 levels.
    const maxAlienLives = 3;
    if (State.level > 3) {
      levelParams.alien1Lives = 1 + Math.floor((State.level / 3) - 1);
      if (levelParams.alien1Lives > maxAlienLives) levelParams.alien1Lives = maxAlienLives;
    }
    if (State.level > 5) {
      levelParams.alien2Lives = 1 + Math.floor((State.level / 3) - 2);
      if (levelParams.alien2Lives > maxAlienLives) levelParams.alien2Lives = maxAlienLives;
    }

    // Motherships spawn faster at higher levels.
    this.motherShip.interval = spaceinvadersConfig.motherShip.interval - (State.level / 2);
    this.motherShip.fireRate = 2 + (State.level / 3);

    return levelParams
  }
}
/* ===== module: PlayerBullet.js ===== */
class PlayerBullet {

  constructor(gameAssets, scene, playerMesh) {
    this.scene = scene;
    this.gameAssets = gameAssets;
    this.maxY = 120;
    if (spaceinvadersConfig.actionCam) {
      this.maxY = 400;
    }
    this.offset = 2;
    this.bulletSpeed = 1.25;
    this.bullet = new MeshBuilder.CreateBox("bullet", {
      width: 0.5,
      height: 4,
      depth: 1
    }, this.scene);
    this.bullet.position = new Vector3(
      playerMesh.position.x,
      playerMesh.position.y + this.offset,
      playerMesh.position.z
    );
    this.bullet.metadata = {"type": "playerbullet"};
    this.bullet.collisionGroup = 4;
    this.bullet.collisionMask = 18;
    this.startBulletLoop();
  }

  startBulletLoop() {
    this.bulletObserver = this.scene.onBeforeRenderObservable.add(() => {
      this.bullet.moveWithCollisions(new Vector3(0, this.bulletSpeed * State.delta, 0))
      if (this.bullet.position.y > this.maxY) {
        this.destroyBullet();
      }
      if (this.bullet.collider.collidedMesh) {
        this.handleCollision();
      }
      this.bullet.checkCollisions = true;
    });
  }

  handleCollision() {
    let collidedWithType = (this.bullet.collider.collidedMesh.metadata).type;

    // If the collidedMesh has lives, destroy the bullet and subtract a life and exit the function
    if (this.bullet.collider.collidedMesh.metadata?.lives > 0) {
      this.bullet.collider.collidedMesh.metadata.lives -= 1;

      if (spaceinvadersConfig.oldSchoolEffects.enabled) {
        if (this.bullet.collider.collidedMesh.metadata.type ==="mothership"){
          this.bullet.collider.collidedMesh.rotate(Axis.Z, Scalar.RandomRange(-0.25, 0.25), Space.WORLD);

        }else {
          this.bullet.collider.collidedMesh.rotate(Axis.Z, Scalar.RandomRange(-0.25, 0.25), Space.LOCAL);
        }
      } else {
        this.bullet.collider.collidedMesh.rotate(Axis.X, Scalar.RandomRange(-0.3, 0.3), Space.LOCAL);
      }
      new Explosion(this.bullet, 10, 1, this.scene);
      this.destroyBullet();
      this.gameAssets.sounds.alienExplosion.play(0, 0.15, 1);
      return;
    }

    // No lives left so destroy the collidedMesh.
    if (collidedWithType === "alien") {
      this.bullet.collider.collidedMesh.dispose(); // perform action with meshes onDispose event.
      this.destroyBullet();
    }
    if (collidedWithType === "barrier") {
      this.bullet.collider.collidedMesh.dispose(); // perform action with meshes onDispose event.
      this.destroyBullet();
    }
    if (collidedWithType === "mothership") {
      this.bullet.collider.collidedMesh.dispose(); // perform action with meshes onDispose event.
      this.destroyBullet();
    }
  }

  destroyBullet() {
    this.scene.onBeforeRenderObservable.remove(this.bulletObserver);
    this.bullet.dispose();
    this.disposed = true; // Tells our game loop to destroy this instance.
  }
}
/* ===== module: InputController.js ===== */
class InputController {

  constructor(scene) {
    scene.actionManager = new ActionManager(scene);
    this.inputMap = {};
    window.addEventListener("keydown", (key) => {
      key.preventDefault();
      key.stopPropagation();
      let keyPressed = key.key;
      if (key.key === " ") keyPressed = "space";
      this.inputMap[keyPressed.toLowerCase()] = true;
    });
    window.addEventListener("keyup", (key) => {
      key.preventDefault();
      key.stopPropagation();
      let keyPressed = key.key;
      if (key.key === " ") keyPressed = "space";
      this.inputMap[keyPressed.toLowerCase()] = false;
    });
  }
}
/* ===== module: MobileInputs.js ===== */
// @todo: proper mobile virtual joysticks. So far results have been poor.
// Good example of custom controls here:https://playground.babylonjs.com/#C6V6UY#5



class MobileInputs {

  fire = false;
  left = false;
  right = false;
  isMobile = false;

  constructor(scene) {
    this.scene = scene;
    this.isMobile = /Android|webOS|iPhone|iPad|iPod|BlackBerry|IEMobile|Opera Mini/i.test(navigator.userAgent);
  }

  enable(texture) {
    if (this.isMobile) {
      this.drawJoystick(texture);
      this.drawFireButton(texture);
    }
  }

  disable() {

  }

  drawFireButton(texture) {
    let offsetX = 40;
    let offsetY = -30;
    let width = 60;
    let height = 60;
    let fireButton = this.makeThumbArea("thumb", 2, "white", null);

    fireButton.height = height + "px";
    fireButton.width = width + "px";
    fireButton.horizontalAlignment = Control.HORIZONTAL_ALIGNMENT_LEFT;
    fireButton.verticalAlignment = Control.VERTICAL_ALIGNMENT_BOTTOM;
    fireButton.alpha = 1;
    fireButton.left = offsetX;
    fireButton.top = offsetY;
    fireButton.onPointerDownObservable.add(() => {
      this.fire = true;
    });
    fireButton.onPointerUpObservable.add(() => {
      this.fire = false;
    });
    texture.addControl(fireButton);

    let line1 = new Line();
    line1.x1 = (width / 2) - 1;
    line1.y1 = height / 8 * 2;
    line1.x2 = (width / 3) - 2;
    line1.y2 = height / 8 * 5;
    line1.color = "white";
    line1.lineWidth = 4;
    fireButton.addControl(line1);

    let line2 = new Line();
    line2.x1 = (width / 2) - 2;
    line2.y1 = height / 8 * 2;
    line2.x2 = (width / 3 * 2) - 2;
    line2.y2 = height / 8 * 5;
    line2.color = "white";
    line2.lineWidth = 4;
    fireButton.addControl(line2);

    let line3 = new Line();
    line3.x1 = (width / 3) - 2;
    line3.y1 = (height / 8 * 5) - 1;
    line3.x2 = (width / 3 * 2) - 2;
    line3.y2 = (height / 8 * 5) - 1;
    line3.color = "white";
    line3.lineWidth = 4;
    fireButton.addControl(line3);
  }

  drawJoystick(texture) {
    let joystickX = 0;
    let joystickY = 0;
    let offsetX = 30;
    let offsetY = -20;

    let thumbContainer = this.makeThumbArea("thumb", 2, "white", null);
    thumbContainer.height = "100px";
    thumbContainer.width = "100px";
    thumbContainer.isPointerBlocker = true;
    thumbContainer.horizontalAlignment = Control.HORIZONTAL_ALIGNMENT_RIGHT;
    thumbContainer.verticalAlignment = Control.VERTICAL_ALIGNMENT_BOTTOM;
    thumbContainer.alpha = 1;
    thumbContainer.left = -offsetX;
    thumbContainer.top = offsetY;

    let innerThumbContainer = this.makeThumbArea("rightInnterThumb", 4, "white", null);
    innerThumbContainer.height = "40px";
    innerThumbContainer.width = "40px";
    innerThumbContainer.isPointerBlocker = true;
    innerThumbContainer.horizontalAlignment = Control.HORIZONTAL_ALIGNMENT_CENTER;
    innerThumbContainer.verticalAlignment = Control.VERTICAL_ALIGNMENT_CENTER;

    let puck = this.makeThumbArea("puck", 0, "red", "red");
    puck.height = "35px";
    puck.width = "35px";
    puck.alpha = 0.6;
    puck.isPointerBlocker = true;
    puck.horizontalAlignment = Control.HORIZONTAL_ALIGNMENT_CENTER;
    puck.verticalAlignment = Control.VERTICAL_ALIGNMENT_CENTER;

    thumbContainer.onPointerDownObservable.add((coordinates) => {
      puck.isVisible = true;
      puck.floatLeft = -(texture._canvas.width - coordinates.x - (thumbContainer._currentMeasure.width * .5) - offsetX);
      puck.left = puck.floatLeft;
      puck.floatTop = texture._canvas.height - coordinates.y - (thumbContainer._currentMeasure.height * .5) + offsetY;
      puck.top = puck.floatTop * -1;
      puck.isDown = true;
      this.left = this.right = false;
      if (puck.floatLeft < 0) this.left = true;
      if (puck.floatLeft > 0) this.right = true;
    });

    thumbContainer.onPointerUpObservable.add((coordinates) => {
      joystickX = 0;
      joystickY = 0;
      this.left = this.right = false;
      puck.isDown = false;
      puck.isVisible = false;
    });

    thumbContainer.onPointerMoveObservable.add((coordinates) => {
      let deadZone = 15;
      if (puck.isDown) {
        joystickX = -(texture._canvas.width - coordinates.x - (thumbContainer._currentMeasure.width * .5) - offsetX);
        joystickY = texture._canvas.height - coordinates.y - (thumbContainer._currentMeasure.height * .5) + offsetY;
        puck.floatLeft = joystickX;
        puck.floatTop = joystickY * -1;
        puck.left = puck.floatLeft;
        puck.top = puck.floatTop;
        this.left = this.right = false;
        if (joystickX < -deadZone) this.left = true;
        if (joystickX > deadZone) this.right = true;
      }
    });

    texture.addControl(thumbContainer);
    thumbContainer.addControl(innerThumbContainer);
    thumbContainer.addControl(puck);
    puck.isVisible = false;

  }

  makeThumbArea(name, thickness, color, background, curves) {
    let ellipse = new Ellipse();
    ellipse.name = name;
    ellipse.thickness = thickness;
    ellipse.color = color;
    ellipse.background = background;
    ellipse.paddingLeft = "0px";
    ellipse.paddingRight = "0px";
    ellipse.paddingTop = "0px";
    ellipse.paddingBottom = "0px";
    return ellipse;
  }
}
/* ===== module: PlayerController.js ===== */
class PlayerController {

  constructor(environment, gameAssets) {
    this.environment = environment;
    this.scene = environment.scene;
    this.gameAssets = gameAssets;
    this.movementEnabled = false;
    this.inputController = new InputController(this.scene);
    this.mobileInputs = new MobileInputs(this.scene);
    this.bullets = [];
    this.maxBullets = 50;
    this.momentum = 0;
    this.isInvincible = false;
  }

  initPlayer() {
    this.playerMesh = this.gameAssets.clone("Player_1");
    this.playerMesh.position = new Vector3(0, 0, 0);
    this.playerMesh.metadata = {
      type: "player"
    }
    this.playerMesh.onDispose = (mesh) => {
      if (State.state === "GAMELOOP" || State.state === "ALIENSWIN") {
        this.playerHit(mesh);
        this.scene.onBeforeRenderObservable.remove(this.playerObserver);
      }
    };
    this.playerMesh.checkCollisions = false;
    this.playerMesh.collisionGroup = 1
    this.playerMesh.collisionMask = 1;
    this.setInvicibility(true, 3000);
    this.enableMovement();
    this.playerObserver = this.scene.onBeforeRenderObservable.add(()=>{
      this.playerMove();
    });
  }

  actionCam(x = -1) {
    if (x === -1) x = this.playerMesh.position.x;
    this.environment.actionCam(x);
  }

  enableMovement() {
    this.momentum = 0;
    this.movementEnabled = true;
    this.inputObserver = this.scene.onBeforeRenderObservable.add(() => {
      this.playerActions();
      this.cleanupBullets();
    });
    //this.mobileInputs.enable();
  }

  disableMovement() {
    //this.mobileInputs.disable();
    this.movementEnabled = false;
    this.disableCollisions();
    this.scene.onBeforeRenderObservable.remove(this.inputObserver);
  }

  disableCollisions() {
    this.playerMesh.checkCollisions = false;
  }

  enableCollisions() {
    this.playerMesh.checkCollisions = true;
  }

  playerHit(mesh) {
    this.disableMovement();
    this.gameAssets.sounds.playerExplosion.play();
    new Explosion(mesh, 60, 1.1, this.scene);
    this.disableBulletCollisions();
    State.lives--;
    if (State.lives > -1) {
      setTimeout(() => {
        this.initPlayer();
      }, 2000)
    } else {
      State.state = "GAMEOVER";
    }
  }

  disableBulletCollisions() {
    for (let bullet of this.bullets) {
      bullet.bullet.checkCollisions = false;
      bullet.bullet.collisionMask = 0;
    }
  }

  setInvicibility(invincible = true, howLongFor = 3000) {
    this.isInvincible = invincible;
    this.disableCollisions();

    // Become vulnerable again after howLongFor
    setTimeout(() => {
      this.isInvincible = false;
      this.enableCollisions();
      this.playerMesh.visibility = 1;
    }, howLongFor);
  }

  cleanupBullets() {
    let i = 0;
    for (let bullet of this.bullets) {
      if (bullet.disposed) {
        this.bullets.splice(i, 1);
      }
      i++;
    }
  }

  hidePlayer() {
    this.playerMesh.visibility = 0;
  }

  moveOffScreen() {
    let playerY = this.playerMesh.position.y;
    if (playerY > -100) {
      this.playerMesh.position.y = Scalar.Lerp(playerY, -150, 0.003);
      return true;
    }
    return false;
  }

  destroyPlayer() {
    this.playerMesh.dispose();
  }

  playerActions() {
    //Flash if invincible
    if (this.isInvincible) {
      if (Date.now() % 200 > 100) {
        this.playerMesh.visibility = 0;
      } else {
        this.playerMesh.visibility = 1;
      }
    }
    let input = this.inputController.inputMap;
    if (this.movementEnabled) {
      if (input.arrowleft || input.a || this.mobileInputs.left) {
        this.playerMoveLeft(State.delta);
      }
      if (input.arrowright || input.d || this.mobileInputs.right) {
        this.playerMoveRight(State.delta);
      }
    }
    if (input.shift || input.enter || input.space || this.mobileInputs.fire) {
      if (!this.fireKeyDown && this.bullets.length < this.maxBullets && this.movementEnabled) {
        this.bullets.push(new PlayerBullet(this.gameAssets, this.scene, this.playerMesh));
        this.gameAssets.sounds.lazer.play();
        this.fireKeyDown = true;
      }
    } else {
      this.fireKeyDown = false;
    }
    this.playerMove();
  }

  playerMoveLeft() {
    this.momentum -= .2 * State.delta;
  }

  playerMoveRight() {
    this.momentum += .2 * State.delta;
  }

  // @todo: Player movement is faster at lower framerates!!!
  //  Approx 30% fast at 24FPS than at 60.
  //  Need to work on delta calculations.
  playerMove() {
    this.playerMesh.position.x += this.momentum * (State.delta);
    this.momentum /= Math.pow(1.4, State.delta);
    this.playerMesh.rotation = new Vector3(0, this.momentum, this.momentum / 4);
  }
}
/* ===== module: GameGUI.js ===== */
class GameGUI {

  texture;
  canvasWidth;
  canvasHeight;
  textScore;
  textLevel;
  textLives;
  textHigh;
  textScale = 1;

  constructor() {
    this.texture = AdvancedDynamicTexture.CreateFullscreenUI("UI", false);
    this.getCanvasSize();
    this.createTextNodes();
    this.fixTextScale();
    window.onresize = () => {
      this.getCanvasSize();
      this.fixTextScale();
    }
  }

  createTextNodes() {
    let fontSize = 22 * this.textScale;
    let spacing = 150 * this.textScale;
    //Score
    this.textScore = new TextBlock();
    this.textScore.color = "white";
    this.textScore.fontFamily = '"Press Start 2P"';
    this.textScore.fontSize = fontSize;
    this.textScore.textVerticalAlignment = Control.VERTICAL_ALIGNMENT_TOP;
    this.textScore.textHorizontalAlignment = Control.HORIZONTAL_ALIGNMENT_CENTER;
    this.textScore.left = -spacing * 3;
    this.textScore.top = 40;
    this.texture.addControl(this.textScore);

    // Level
    this.textLevel = new TextBlock();
    this.textLevel.color = "white";
    this.textLevel.fontSize = fontSize;
    this.textLevel.fontFamily = '"Press Start 2P"';
    this.textLevel.textVerticalAlignment = Control.VERTICAL_ALIGNMENT_TOP;
    this.textLevel.textHorizontalAlignment = Control.HORIZONTAL_ALIGNMENT_CENTER
    this.textLevel.left = -spacing;
    this.textLevel.top = 40;
    this.texture.addControl(this.textLevel);

    // Lives
    this.textLives = new TextBlock();
    this.textLives.color = "white";
    this.textLives.fontSize = fontSize;
    this.textLives.fontFamily = '"Press Start 2P"';
    this.textLives.textVerticalAlignment = Control.VERTICAL_ALIGNMENT_TOP;
    this.textLives.textHorizontalAlignment = Control.HORIZONTAL_ALIGNMENT_CENTER
    this.textLives.left = spacing;
    this.textLives.top = 40;
    this.texture.addControl(this.textLives);

    // High score
    this.textHigh = new TextBlock();
    this.textHigh.color = "white";
    this.textHigh.fontSize = fontSize;
    this.textHigh.fontFamily = '"Press Start 2P"';
    this.textHigh.textVerticalAlignment = Control.VERTICAL_ALIGNMENT_TOP;
    this.textHigh.textHorizontalAlignment = Control.HORIZONTAL_ALIGNMENT_CENTER
    this.textHigh.left = spacing * 3;
    this.textHigh.top = 40;
    this.texture.addControl(this.textHigh);

  }

  update() {
    this.textScore.text = "SCORE:" + State.score;
    this.textLevel.text = "LEVEL:" + State.level;
    this.textLives.text = "LIVES:" + Math.max(0,State.lives);
    this.textHigh.text = "HIGH:" + (window.localStorage.getItem('highScore') ?? 0);
  }

  getCanvasSize() {
    this.canvasWidth = document.querySelector("canvas").width;
    this.canvasHeight = document.querySelector("canvas").height;
  }

  fixTextScale() {
    this.textScale = Math.min(1, this.canvasWidth / 1280);
    let fontSize = 22 * this.textScale;
    let spacing = 150 * this.textScale;
    this.textScore.fontSize = fontSize;
    this.textLives.fontSize = fontSize;
    this.textLevel.fontSize = fontSize;
    this.textHigh.fontSize = fontSize;
    this.textScore.left = -spacing * 3;
    this.textLevel.left = -spacing;
    this.textLives.left = spacing;
    this.textHigh.left = spacing * 3;
  }
}
/* ===== module: GameController.js ===== */
class GameController {

  constructor(environment, inputController, gameAssets = null, UI) {
    this.UI = UI;
    this.environment = environment;
    this.scene = environment.scene;
    this.inputController = inputController;
    this.gameAssets = gameAssets;
    this.playerController = new PlayerController(environment, this.gameAssets);
  }

  initialise() {
    State.lives = config.startingLives;
    State.level = config.startingLevel;
    State.score = 0;
    State.highScore = this.getHighScore();
    State.gameOverStep = 0;
    State.gameOverStep = 0;
  }

  setHighScore(score) {
    window.localStorage.setItem('highScore', score);
    State.highScore = score;
  }

  getHighScore() {
    return parseInt(window.localStorage.getItem('highScore') ?? 0);
  }

  startGame() {
    //this.fullScreen();
    this.initialise();
    this.nextLevel();
    this.loadGameGUI();
  }

  loadGameGUI(){
    this.UI.disable();
    this.gameGUI = new GameGUI();
    this.playerController.mobileInputs.enable(this.gameGUI.texture);
  }

  fullScreen() {
    if (/Android|webOS|iPhone|iPad|iPod|BlackBerry|IEMobile|Opera Mini/i.test(navigator.userAgent)) {
      let el = document.documentElement;
      if (el.requestFullscreen) {
        el.requestFullscreen();
      } else if (el.webkitRequestFullscreen) { /* Safari */
        el.webkitRequestFullscreen();
      } else if (el.msRequestFullscreen) { /* IE11 */
        el.msRequestFullscreen();
      }
    }
  }

  titleScreen() {
    this.UI.showTitleScreen();
    this.UI.hideLoadingScreen()
  }

  nextLevel() {
    State.level += 1;
    this.playerController.movementEnabled = false;
    this.buildAliensFormation();
    this.playerController.initPlayer();
    State.state = "GAMELOOP";
    this.gameAssets.sounds.levelStart.play();
  }

  checkStates() {
    this.playerController.actionCam();
    if (this.alienFormation.alienCount === 0) {
      State.state = "PLAYERWINS";
      this.playerController.disableMovement();
      setTimeout(() => {
        State.state = "CLEARLEVEL";
        this.gameAssets.sounds.clearLevel.play();
        this.UI.showGameUI();
        this.UI.showGameHints();
      }, 1500);
    }
    this.gameGUI.update();
  }

  aliensWin() {
    State.lives = 0;
    this.playerController.playerMesh.dispose();
  }

  gameOver() {
    switch (State.gameOverStep) {
      case 0:
        this.UI.showGameUI();
        setTimeout(() => {
          this.UI.showGameOver();
          this.checkForNewHighScore();
          this.gameAssets.sounds.gameOver.play();
        }, 2000);
        setTimeout(() => {
          this.UI.showPlayAgain();
          State.gameOverStep = 2;
        }, 4000);
        State.gameOverStep = 1;
        break;
      case 1:
        break;
      case 2:
        if (this.UI.playAgainPressed) {
          this.destroyGameGUI();
          this.UI.hideGameOver();
          this.UI.hidePlayAgain();
          this.UI.hideNewHighScore();
          State.gameOverStep = 3;
          this.gameAssets.sounds.clearLevel.play();
        }
        break;
      case 3 :
        this.clearLevel();
        break;
      case 4:
        State.state = "TITLESCREEN";
        break;
      default:
        break;
    }
  }

  destroyGameGUI(){
    this.gameGUI.texture.dispose();
    delete this.gameGUI.texture;
  }

  checkForNewHighScore() {
    if (State.score > this.getHighScore()) {
      this.setHighScore(State.score);
      this.UI.showNewHighScore();
      this.gameGUI.update();
    }
  }

  buildAliensFormation() {
    this.alienFormation = new AlienFormationController(this.scene, this.gameAssets);
  }

  clearLevel() {
    let clearSteps = 4;
    this.playerController.actionCam(0);
    this.gameGUI.update();
    // Step 1. All barriers must be destroyed.
    if (this.alienFormation.barriers.length) {
      this.alienFormation.destroyBarriers();
      clearSteps -= 1;
    }
    //Step 2. The player should be moved off-screen.
    this.playerController.disableMovement();
    if (this.playerController.moveOffScreen()) {
      clearSteps -= 1;
    }
    // Step 3. Move the mothership offscreen.
    if (this.alienFormation.motherShip.moveOffScreen()) {
      clearSteps -= 1;
    }
    // Step 4. Destroy remaining alien bullets.
    // Step 5. Destroy remaining aliens
    if (this.alienFormation.aliens.length) {
      let randID = Math.floor(Math.random() * this.alienFormation.aliens.length);
      this.alienFormation.aliens[randID].mesh.dispose();
      this.alienFormation.aliens.splice(randID, 1);
      clearSteps -= 1;
    }

    if (clearSteps === 4) {
      this.playerController.destroyPlayer();
      this.alienFormation.motherShip.destroyMotherShip();
      this.alienFormation.clearScene();
      delete this.alienFormation;
      // final cleanup to ensure everything has been disposed of.
      while (this.scene.meshes.length) {
        this.scene.meshes[0].dispose();
      }
      if (State.state === "GAMEOVER") {
        State.gameOverStep += 1;
      } else {
        State.state = "NEXTLEVEL";
      }
      this.UI.hideGameHints();
      this.UI.hideGameUI();
      this.UI.disable();
    }
  }
}
/* ===== module: Starfield.js ===== */
class Starfield {

  constructor(scene) {
    this.scene = scene;
    this.maxStars = spaceinvadersConfig.starField.numberofStars;
    this.stars = [];
    this.ratioOfBlinkers = spaceinvadersConfig.starField.ratioOfBlinkingStars;
    this.minY = -50;
    this.maxY = 150;
    this.minSize = spaceinvadersConfig.starField.minStarSize;
    this.maxSize = spaceinvadersConfig.starField.maxStarSize;
    if (spaceinvadersConfig.actionCam) {
      this.maxY = 500;
    }
    this.colors = [
      [1, 1, 0, 1],
      [0, 1, 0, 1],
      [1, 0, 0, 1],
      [1, 0, 1, 1],
      [1, 1, 1, 1]
    ];
    this.speed = spaceinvadersConfig.starField.speed;

    // With actioncam enabled the starfield is rolling
    // at an angle. Speed it up to compensate.
    if (spaceinvadersConfig.actionCam) this.speed *= 2;

    if (spaceinvadersConfig.oldSchoolEffects.enabled && spaceinvadersConfig.oldSchoolEffects.blurIntensity > 0.2) {
      this.spriteManager = new SpriteManager("starsManager", "/assets/images/star_glow.png", 1000, {width: 384, height: 384}, this.scene);
      this.minSize *= 3;
      this.maxSize *= 3;
    } else {
      this.spriteManager = new SpriteManager("starsManager", "/assets/images/star.png", 1000, {width: 128, height: 128}, this.scene);
    }

    this.createStarField();
    this.startRoll();
  }

  startRoll() {
    this.roll = this.scene.onBeforeRenderObservable.add(() => {
      this.rollStarsDown();
    });
  }

  rollStarsDown() {
    let i = 0;
    while (i < this.stars.length) {
      this.stars[i].position.y -= this.speed * this.getDelta();
      if (this.stars[i].position.y < this.minY) {
        this.stars[i].position.y = this.maxY - (this.minY - this.stars[i].position.y);
      }
      i++;
    }
  }

  createStarField() {
    while (this.stars.length < this.maxStars) {
      this.stars.push(this.addStar());
    }
  }

  addStar() {
    let z = 10;
    if (spaceinvadersConfig.actionCam) z = 5;

    let star = new Sprite("star", this.spriteManager);
    star.size = Scalar.RandomRange(this.minSize, this.maxSize);
    star.position.x = Scalar.RandomRange(-this.maxY, this.maxY);
    star.position.y = Scalar.RandomRange(this.minY, this.maxY);
    star.position.z = z;
    let color = Math.floor(Math.random() * this.colors.length);
    star.color = new Color4(this.colors[color][0], this.colors[color][1], this.colors[color][2], 1);
    if (Math.random() < this.ratioOfBlinkers) {
      star.blinkInterval = (Math.random() * 2000) + 500;
      setTimeout(() => {
        this.blinkStar(star);
      }, Math.random() * star.blinkInterval);
    }
    return star;
  }

  blinkStar(star) {
    let opacity = star.color.a;
    star.color.a = 1 - opacity;
    setTimeout(() => {
      this.blinkStar(star);
    }, star.blinkInterval);
  }

  // This class uses its own delta function.
  // The starfield is created and started
  // before the deltaTime class can accurately
  // calculate an average FPS which causes
  // issues with the initial creation of the
  // starfield
  getDelta() {
    let targetFps = 60;
    let fps = 1000 / this.scene.deltaTime;
    let delta = targetFps / fps;
    if (delta === 0) delta = 1;
    if (delta > 50) delta = 1;
    return delta;
  }

}
/* ===== module: GameAssetsManager.js ===== */
class GameAssetsManager {

  sounds = {};

  constructor(scene) {
    this.isComplete = false;
    this.scene = scene;
    this.assetContainer = new AssetContainer(this.scene);
    this.totalAssetsToLoad = 5 + 10;
    this.assetsLoaded = 0;

    this.loadSounds();
    this.loadModels();
  }

  loadSounds() {
    this.sounds.levelStart = new Sound("levelStart", "/assets/sounds/level-start-sfx.wav", this.scene, () => {
      this.assetsLoaded++;
      this.checkComplete();
    });

    this.sounds.lazer = new Sound("Lazer", "/assets/sounds/player-bullet-sfx.wav", this.scene, () => {
      this.assetsLoaded++;
      this.checkComplete();
    });

    this.sounds.alienMove = new Sound("alienMove", "/assets/sounds/alien-move-sfx.wav", this.scene, () => {
      this.assetsLoaded++;
      this.checkComplete();
    });

    this.sounds.alienBullet = new Sound("alienBullet", "/assets/sounds/alien-bullet-sfx.wav", this.scene, () => {
      this.assetsLoaded++;
      this.checkComplete();
    });

    this.sounds.clearLevel = new Sound("clearLevel", "/assets/sounds/clear-level-sfx.wav", this.scene, () => {
      this.assetsLoaded++;
      this.checkComplete();
    });

    this.sounds.motherShipExplosion = new Sound("motherShipExplosion", "/assets/sounds/mothership-explosion-sfx.wav", this.scene, () => {
      this.assetsLoaded++;
      this.checkComplete();
    });

    this.sounds.playerExplosion = new Sound("playerExplosion", "/assets/sounds/player-explosion-sfx.wav", this.scene, () => {
      this.assetsLoaded++;
      this.checkComplete();
    });

    this.sounds.alienExplosion = new Sound("alienExplosion", "/assets/sounds/alien-explosion-sfx.wav", this.scene, () => {
      this.assetsLoaded++;
      this.checkComplete();
    })

    this.sounds.gameOver = new Sound("gameOver", "/assets/sounds/game-over-sfx.wav", this.scene, () => {
      this.assetsLoaded++;
      this.checkComplete();
    });

    this.sounds.motherShip = new Sound("motherShip", "/assets/sounds/mothership-sfx.wav", this.scene, () => {
      this.assetsLoaded++;
      this.checkComplete();
    }, {
      loop: true
    });
    this.checkComplete();
  }

  loadModels() {
    this.loadAsset("Alien_1.glb").then((assets) => {
      assets.meshes[0].rotation = new Vector3(0, 0, 0); // root Mesh
      assets.meshes[1].position = new Vector3(0, -2000, -2000); // Alien
      this.pushToAssetsContainer(assets.meshes[0]);
      this.pushToAssetsContainer(assets.meshes[1]);
      this.assetsLoaded++;
      this.checkComplete();
    });

    this.loadAsset("Alien_2.glb").then((assets) => {
      assets.meshes[0].rotation = new Vector3(0, 0, 0);
      assets.meshes[1].position = new Vector3(0, -2000, -2000);
      this.pushToAssetsContainer(assets.meshes[0]);
      this.pushToAssetsContainer(assets.meshes[1]);
      this.assetsLoaded++;
      this.checkComplete();
    });

    this.loadAsset("Alien_3.glb").then((assets) => {
      assets.meshes[0].rotation = new Vector3(0, 0, 0);
      assets.meshes[1].position = new Vector3(0, -2000, -2000);
      this.pushToAssetsContainer(assets.meshes[0]);
      this.pushToAssetsContainer(assets.meshes[1]);
      this.assetsLoaded++;
      this.checkComplete();
    });

    this.loadAsset("Player_1.glb").then((assets) => {
      assets.meshes[0].rotation = new Vector3(0, 0, 0);
      assets.meshes[1].position = new Vector3(0, -2000, -2000);
      this.pushToAssetsContainer(assets.meshes[0]);
      this.pushToAssetsContainer(assets.meshes[1]);
      this.assetsLoaded++;
      this.checkComplete();
    });

    this.loadAsset("MotherShip.glb").then((assets) => {
      assets.meshes[0].rotation = new Vector3(0, 0, 0);
      assets.meshes[1].position = new Vector3(0, -2000, -2000);
      this.pushToAssetsContainer(assets.meshes[0]);
      this.pushToAssetsContainer(assets.meshes[1]);
      this.assetsLoaded++;
      this.checkComplete();
    });
  }

  async loadAsset(file) {
    let assets = await SceneLoader.ImportMeshAsync("", "/assets/models/", file, this.scene);
    return assets;
  }

  pushToAssetsContainer(mesh) {
    this.assetContainer.meshes.push(mesh);
  }

  clone(name, newName = null) {
    if (!newName) newName = "id" + Math.floor(Math.random() * 1000000).toString(16);
    let newMesh = null;
    let sourceMesh = this.assetContainer.meshes.filter((mesh) => {
      return mesh.name === name;
    })
    if (sourceMesh.length) {
      newMesh = sourceMesh[0].clone(newName, undefined, undefined);
      newMesh.name = newName;
    }
    return newMesh;
  }

  checkComplete() {
    if (this.assetsLoaded > this.totalAssetsToLoad - 1) {
      // Must call removeAllFromSceneon next tick for some reason
      setTimeout(() => {
        this.assetContainer.removeAllFromScene();
        this.isComplete = true;
      }, 1);
    }
  }
}
/* ===== module: UIText.js ===== */
class UIText {

  playAgainPressed = false;
  randNum = Math.floor(Math.random() * 10);
  gameHints = [
    'You start with a number of lives but if invaders get past you, it\'s game over.',
    'By 1982, Space Invaders was the highest-grossing entertainment product at the time.',
    'Alien formations get stronger, larger and more aggressive with each level.',
    'British paper, The Times, ranked Space Invaders No. 1 on its list of "The ten most influential video games ever".',
    'Destroy motherships for a huge score bonus.',
    'Tomohiro Nishikado developed Space Invaders in 1978. His name was never put on the game for contractual reasons.'
  ]

  constructor() {
    this.startButtonInit();
    this.modeSelectorInit();
  }

  startButtonInit() {
    document.getElementById("start-game").addEventListener('click', (el) => {
      this.startButtonClick(el)
    });
  }

  modeSelectorInit() {
    let selector = document.getElementById("change-mode");
    let mode = parseInt(window.localStorage.getItem('mode') ?? 0);
    selector.getElementsByTagName('option')[mode].selected = true;

    selector.onchange = (function (el) {
      window.localStorage.setItem("mode", selector.value);
      location.reload();
    });

  }

  startButtonClick(el) {
    this.hideTitleScreen();
    State.state = "STARTGAME";
  }

  enable() {
    let UI = document.querySelector("#ui");
    UI.classList.add("active");
  }

  disable() {
    let UI = document.querySelector("#ui");
    UI.classList.remove("active");
  }

  showGameUI() {
    this.enable();
    let UI = document.querySelector("#game-ui");
    UI.classList.add("active");
  }

  hideGameUI() {
    let UI = document.querySelector("#game-ui");
    UI.classList.remove("active");
  }

  showGameOver() {
    this.enable();
    let UI = document.querySelector("#panel-game-over");
    UI.classList.add("active");
  }

  hideGameOver() {
    let UI = document.querySelector("#panel-game-over");
    UI.classList.remove("active");
  }

  showGameHints() {
    this.newGameHint();
    this.enable();
    let UI = document.querySelector("#panel-game-hints");
    UI.classList.add("active");
  }

  hideGameHints() {
    let UI = document.querySelector("#panel-game-hints");
    UI.classList.remove("active");
  }

  newGameHint() {
    let i = ((this.randNum + State.level) % this.gameHints.length)
    document.querySelector("#panel-game-hints .value").innerHTML = this.gameHints[i];
  }

  showPlayAgain() {
    let UI = document.querySelector("#panel-play-again");
    UI.classList.add("active");
    UI.onclick = () => {
      this.playAgainPressed = true;
    }
  }

  hidePlayAgain() {
    let UI = document.querySelector("#panel-play-again");
    UI.classList.remove("active");
    this.playAgainPressed = false;
  }

  showNewHighScore() {
    this.enable();
    document.querySelector("#panel-new-highscore .value").innerHTML = window.localStorage.getItem('highScore');
    let UI = document.querySelector("#panel-new-highscore");
    UI.classList.add("active");
  }

  hideNewHighScore() {
    let UI = document.querySelector("#panel-new-highscore");
    UI.classList.remove("active");
  }

  showTitleScreen() {
    this.enable();
    let UI = document.querySelector("#title-screen");
    UI.classList.add("active");
    let buttons = document.querySelector("#intro");
    buttons.classList.add("active");
  }

  hideTitleScreen() {
    let UI = document.querySelector("#title-screen");
    UI.classList.remove("active");
    let buttons = document.querySelector("#intro");
    buttons.classList.remove("active");
  }

  hideLoadingScreen() {
    let loading = document.querySelector("#loading");
    loading.classList.remove('active');
  }
}
/* ===== module: main.js ===== */
parseSelectedMode();












const canvas = document.querySelector('canvas');
const engine = new Engine(canvas, true);
const environment = new Environment(engine);

const stars = new Starfield(environment.scene);
const deltaTime = new DeltaTime(environment.scene);
const gameAssets = new GameAssetsManager(environment.scene);
const inputController = new InputController(environment.scene);
const UI = new UIText();
const gameController = new GameController(environment, inputController, gameAssets, UI);

// Set default FPS to 60.
// Low FPS in oldSchoolEffects mode
let lastRenderTime = 0;
let FPS = 60;
if (spaceinvadersConfig.oldSchoolEffects.enabled) FPS = 18;

engine.runRenderLoop(() => {
  if (gameAssets.isComplete) {
    switch (State.state) {
      case "LOADING":
        break;
      case "TITLESCREEN":
        gameController.titleScreen();
        break;
      case "STARTGAME":
        Engine.audioEngine.unlock();
        gameController.startGame();
        break;
      case "NEXTLEVEL":
        gameController.nextLevel();
        break;
      case "GAMELOOP":
        gameController.checkStates();
        break;
      case "ALIENSWIN":
        gameController.aliensWin();
        break;
      case "CLEARLEVEL":
        gameController.clearLevel();
        break;
      case "GAMEOVER":
        gameController.gameOver();
        break;
      default:
        // does nothing.
        break;
    }
    // Force a low FPS if required by oldSchoolEffects mode.
    let timeNow = Date.now();
    while (timeNow - lastRenderTime < 1000 / FPS) {
      timeNow = Date.now()
    }
    lastRenderTime = timeNow;
    window.scrollTo(0, 0);
    environment.scene.render();
  }
});

window.addEventListener('resize', () => {
  engine.resize();
});

function parseSelectedMode() {
  let mode = parseInt(window.localStorage.getItem('mode') ?? 0);
  document.querySelector("body").classList.add("mode"+mode);
  switch (mode) {
    case 0:
      break;
    case 1:
      spaceinvadersConfig.oldSchoolEffects.enabled = true;
      break;
    case 2:
      spaceinvadersConfig.actionCam = true;
      break;
    default:
      break;
  }
}
</script>
<script>
  loading();

  function loading() {
    setTimeout(function () {
      let el = document.querySelector("#loading")
      el.append(".");
      if (el.classList.contains("active")) {
        loading();
      }
    }, 1000);
  }
</script>
</body>
</html>
)ctb"
