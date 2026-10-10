// WHAT test/corpus/babylon/babylon_interaction.cpp ASKS THE ORBIT PAGE. The
// test loads this into examples/pages/babylon-orbit.html once the page is up:
// that page is the example program's and a golden's too, so it does not list
// this file itself. The input events that move the camera are dispatched by
// the C++.

// THE CAMERA IS REACHED THROUGH THE SCENE, not through a global the page
// exports for the test's benefit. A page written to be testable is a page
// that proves less: this one is exactly what a human runs.
function babylon_interaction_grab_camera() {
    window.__cam = BABYLON.Engine.LastCreatedScene.activeCamera;
}

function babylon_interaction_ready() {
    return String(typeof BABYLON !== 'undefined' &&
        document.getElementById('scene') !== null &&
        BABYLON.Engine.LastCreatedScene.activeCamera !== null);
}

function babylon_interaction_alpha() {
    return window.__cam.alpha.toFixed(4);
}

function babylon_interaction_beta() {
    return window.__cam.beta.toFixed(4);
}

function babylon_interaction_radius() {
    return window.__cam.radius.toFixed(3);
}

function babylon_interaction_alpha_beta() {
    return window.__cam.alpha.toFixed(4) + ',' + window.__cam.beta.toFixed(4);
}
