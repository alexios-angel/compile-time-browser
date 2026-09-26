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

    function centre(element) {
        var r = element.getBoundingClientRect();
        return {
            x: r.left + r.width / 2,
            y: r.top + r.height / 2
        };
    }

    var driver = window.test_driver_internal;
    driver.in_automation = true;

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

    // Element Send Keys: focus, then every character - a special one as its
    // key, anything else as typed text.
    driver.send_keys = function (element, keys) {
        element.focus();
        var steps = [];
        for (var i = 0; i < keys.length; ++i) {
            var ch = keys[i];
            if (special[ch]) {
                steps.push({
                    cmd: "key",
                    code: special[ch]
                });
            } else {
                steps.push({
                    cmd: "type",
                    text: ch
                });
            }
        }
        return perform(steps);
    };

    // Perform Actions, tick by tick: tick i runs action i of every source.
    driver.action_sequence = function (sources) {
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
                if (source.type === "pointer") {
                    if (a.type === "pointerMove") {
                        var x = a.x || 0,
                            y = a.y || 0;
                        if (a.origin === "pointer") {
                            x += pointer.x;
                            y += pointer.y;
                        } else if (a.origin && typeof a.origin === "object") {
                            var c = centre(a.origin);
                            x += c.x;
                            y += c.y;
                        }
                        pointer = {
                            x: x,
                            y: y
                        };
                        steps.push({
                            cmd: "move",
                            x: x,
                            y: y
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
                    if (a.type === "keyDown" && code) {
                        steps.push({
                            cmd: "keydown",
                            code: code
                        });
                    } else if (a.type === "keyDown") {
                        steps.push({
                            cmd: "type",
                            text: a.value
                        });
                    } else if (a.type === "keyUp" && code) {
                        steps.push({
                            cmd: "keyup",
                            code: code
                        });
                    }
                } else if (source.type === "wheel" && a.type === "scroll") {
                    steps.push({
                        cmd: "wheel",
                        x: a.x || 0,
                        y: a.y || 0,
                        notches: (a.deltaY || 0) / 100
                    });
                }
            }
        }
        return perform(steps);
    };
})();
