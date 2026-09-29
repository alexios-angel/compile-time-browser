// testdriver-vendor.js for ctdrive - installed over the corpus's own by
// tools/wpt/run-wpt.py. WebDriver is not here, so an action is QUEUED on the
// page and run-wpt.py, which polls the page through ctdrive's `eval` anyway,
// drains the queue, replays each step as ctdrive's own input commands (real
// input events through the browser, the way a click from a user arrives) and
// then settles the promise the test is waiting on. What is not implemented
// below keeps testdriver.js's own "not implemented" rejection.
(function () {
    "use strict";
    var queue = [];
    var pending = {};
    var next = 1;
    var pointer = {
        x: 0,
        y: 0
    };

    window.__ctdrive_take = function () {
        var out = queue;
        queue = [];
        return JSON.stringify(out);
    };
    window.__ctdrive_settle = function (id, error) {
        var waiting = pending[id];
        if (!waiting) {
            return;
        }
        delete pending[id];
        if (error) {
            waiting.reject(new Error(error));
        } else {
            waiting.resolve();
        }
    };

    function perform(steps) {
        return new Promise(function (resolve, reject) {
            var id = next++;
            pending[id] = {
                resolve: resolve,
                reject: reject
            };
            queue.push({
                id: id,
                steps: steps
            });
        });
    }

    // WebDriver's key values (the Private Use Area of its key table) as the
    // DOM `code` ctdrive's key commands take. A character outside the table is
    // typed as text.
    var special = {
        "\uE003": "Backspace",
        "\uE004": "Tab",
        "\uE006": "Enter",
        "\uE007": "Enter",
        "\uE008": "ShiftLeft",
        "\uE009": "ControlLeft",
        "\uE00A": "AltLeft",
        "\uE00C": "Escape",
        "\uE00D": "Space",
        "\uE00E": "PageUp",
        "\uE00F": "PageDown",
        "\uE010": "End",
        "\uE011": "Home",
        "\uE012": "ArrowLeft",
        "\uE013": "ArrowUp",
        "\uE014": "ArrowRight",
        "\uE015": "ArrowDown",
        "\uE016": "Insert",
        "\uE017": "Delete",
        "\uE03D": "MetaLeft",
        "\uE050": "ShiftRight",
        "\uE051": "ControlRight",
        "\uE052": "AltRight",
        "\uE053": "MetaRight"
    };

    // WebDriver's "in-view center point": the middle of the element's first
    // client rect once it is clipped to the viewport, floored.
    function centre(element) {
        var r = element.getClientRects()[0] || element.getBoundingClientRect();
        var left = Math.max(0, Math.min(r.x, r.x + r.width));
        var right = Math.min(window.innerWidth, Math.max(r.x, r.x + r.width));
        var top = Math.max(0, Math.min(r.y, r.y + r.height));
        var bottom = Math.min(window.innerHeight, Math.max(r.y, r.y + r.height));
        return {
            x: Math.floor((left + right) / 2),
            y: Math.floor((top + bottom) / 2)
        };
    }

    var driver = window.test_driver_internal;
    driver.in_automation = true;

    // get_computed_role / get_computed_label: the element waits in a slot,
    // ctdrive's `accessibility` step reads it through __ctdrive_asked and
    // writes the answer back through __ctdrive_answer before the step's
    // promise settles.
    var asked = {};
    var answers = {};
    var slots = 1;
    window.__ctdrive_asked = function (slot) {
        return asked[slot];
    };
    window.__ctdrive_answer = function (slot, text) {
        answers[slot] = text;
    };

    function accessibility(what, element) {
        var slot = slots++;
        asked[slot] = element;
        return perform([{
            cmd: "accessibility",
            what: what,
            slot: slot
        }]).then(function () {
            var answer = answers[slot];
            delete asked[slot];
            delete answers[slot];
            return answer;
        }, function (error) {
            delete asked[slot];
            throw error;
        });
    }
    driver.get_computed_role = function (element) {
        return accessibility("role", element);
    };
    driver.get_computed_label = function (element) {
        return accessibility("label", element);
    };

    driver.click = function (element, coords) {
        pointer = {
            x: coords.x,
            y: coords.y
        };
        return perform([{
            cmd: "click",
            x: coords.x,
            y: coords.y,
            button: 0
        }]);
    };

    // Which modifier a key code is, if any: WebDriver holds Shift and
    // Control down across the keys that follow them, and ctdrive's key
    // steps carry that state as flags.
    function modifier(code) {
        if (code === "ShiftLeft" || code === "ShiftRight") {
            return "shift";
        }
        if (code === "ControlLeft" || code === "ControlRight") {
            return "ctrl";
        }
        return null;
    }

    // Element Send Keys: focus, then every character - a special one as its
    // key, anything else as typed text. A modifier TOGGLES, as WebDriver's
    // "dispatch the events for a typeable string" has it, and the null key
    // releases them all.
    driver.send_keys = function (element, keys) {
        element.focus();
        var steps = [];
        var held = {
            shift: false,
            ctrl: false
        };

        function release() {
            if (held.shift) {
                held.shift = false;
                steps.push({
                    cmd: "keyup",
                    code: "ShiftLeft",
                    ctrl: held.ctrl
                });
            }
            if (held.ctrl) {
                held.ctrl = false;
                steps.push({
                    cmd: "keyup",
                    code: "ControlLeft"
                });
            }
        }
        for (var i = 0; i < keys.length; ++i) {
            var ch = keys[i];
            var which = modifier(special[ch]);
            if (ch === "\uE000") {
                release();
            } else if (which) {
                held[which] = !held[which];
                steps.push({
                    cmd: held[which] ? "keydown" : "keyup",
                    code: special[ch],
                    shift: held.shift,
                    ctrl: held.ctrl
                });
            } else if (special[ch]) {
                steps.push({
                    cmd: "key",
                    code: special[ch],
                    shift: held.shift,
                    ctrl: held.ctrl
                });
            } else {
                steps.push({
                    cmd: "type",
                    text: ch
                });
            }
        }
        release();
        return perform(steps);
    };

    // Where an action with x, y and an origin lands: the viewport's origin,
    // the pointer's last position, or an element's in-view center point.
    function place(a, from) {
        var x = a.x || 0,
            y = a.y || 0;
        if (a.origin === "pointer") {
            x += from.x;
            y += from.y;
        } else if (a.origin && typeof a.origin === "object") {
            var c = centre(a.origin);
            x += c.x;
            y += c.y;
        }
        return {
            x: x,
            y: y
        };
    }

    // Each touch source's finger, by source id.
    var fingers = {};

    function touching(source) {
        return (source.parameters || {}).pointerType === "touch";
    }

    // Perform Actions, tick by tick: tick i runs action i of every source.
    // A key source's Shift and Control stay held from their keyDown to their
    // keyUp, so Shift+Tab reaches the page as a shifted Tab.
    driver.action_sequence = function (sources) {
        var held = {
            shift: false,
            ctrl: false
        };
        var ticks = 0;
        for (var s = 0; s < sources.length; ++s) {
            ticks = Math.max(ticks, (sources[s].actions || []).length);
        }
        var steps = [];
        for (var t = 0; t < ticks; ++t) {
            for (var s2 = 0; s2 < sources.length; ++s2) {
                var source = sources[s2];
                var a = (source.actions || [])[t];
                if (!a) {
                    continue;
                }
                if (a.type === "pause") {
                    continue;
                }
                if (source.type === "pointer" && touching(source)) {
                    // A TOUCH POINTER is a finger: it exists only while down,
                    // so a move before the press just places it.
                    var finger = fingers[source.id] || (fingers[source.id] = {
                        x: 0,
                        y: 0,
                        down: false
                    });
                    if (a.type === "pointerMove") {
                        var to = place(a, finger);
                        finger.x = to.x;
                        finger.y = to.y;
                        if (finger.down) {
                            steps.push({
                                cmd: "touch",
                                phase: "move",
                                x: to.x,
                                y: to.y
                            });
                        }
                    } else if (a.type === "pointerDown" || a.type === "pointerUp") {
                        finger.down = a.type === "pointerDown";
                        steps.push({
                            cmd: "touch",
                            phase: finger.down ? "start" : "end",
                            x: finger.x,
                            y: finger.y
                        });
                    }
                } else if (source.type === "pointer") {
                    if (a.type === "pointerMove") {
                        pointer = place(a, pointer);
                        steps.push({
                            cmd: "move",
                            x: pointer.x,
                            y: pointer.y
                        });
                    } else if (a.type === "pointerDown") {
                        steps.push({
                            cmd: "down",
                            x: pointer.x,
                            y: pointer.y,
                            button: a.button || 0
                        });
                    } else if (a.type === "pointerUp") {
                        steps.push({
                            cmd: "up",
                            x: pointer.x,
                            y: pointer.y,
                            button: a.button || 0
                        });
                    }
                } else if (source.type === "key") {
                    var code = special[a.value];
                    var which = modifier(code);
                    if (which) {
                        held[which] = a.type === "keyDown";
                    }
                    if (a.type === "keyDown" && code) {
                        steps.push({
                            cmd: "keydown",
                            code: code,
                            shift: held.shift,
                            ctrl: held.ctrl
                        });
                    } else if (a.type === "keyDown") {
                        steps.push({
                            cmd: "type",
                            text: a.value
                        });
                    } else if (a.type === "keyUp" && code) {
                        steps.push({
                            cmd: "keyup",
                            code: code,
                            shift: held.shift,
                            ctrl: held.ctrl
                        });
                    }
                } else if (source.type === "wheel" && a.type === "scroll") {
                    // Pixels in, notches out, and the engine's vertical notch
                    // is positive AWAY from the user - which is scrolling UP,
                    // the opposite of a positive deltaY.
                    var at = place(a, {
                        x: 0,
                        y: 0
                    });
                    steps.push({
                        cmd: "wheel",
                        x: at.x,
                        y: at.y,
                        notches: -(a.deltaY || 0) / 100,
                        notches_x: (a.deltaX || 0) / 100
                    });
                }
            }
        }
        return perform(steps);
    };
})();
