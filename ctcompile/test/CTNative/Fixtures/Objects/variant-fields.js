// Closed-shape fields that hold a String or something else: a union of String
// with Boolean, and a String that may be undefined. Each is one struct member
// of the union's carrier, written from two sources, as a component's options
// are by its defaults and by its caller. The interpreter's output is the
// reference for every printed global.

// `pause` is a String or a Boolean, `ride` a Boolean or a String, and `title`
// a String or undefined.
function options(flag) {
    var config = {
        pause: "hover",
        ride: false,
        title: "x"
    };
    if (flag > 1) {
        config.pause = false;
        config.ride = "carousel";
    }
    if (flag > 5) {
        config.title = undefined;
    }
    return (config.pause === "hover" ? 1 : 0) + (config.ride === "carousel" ? 10 : 0) +
        (config.pause === false ? 100 : 0) + (config.title === undefined ? 1000 : 0);
}

// The defaults and the caller's options merged by spread: `pause` is stored a
// String by the first and a Boolean by the second.
function merged(flag) {
    var defaults = {
        pause: "hover",
        keyboard: true
    };
    var caller = {
        pause: false
    };
    var config = {
        ...defaults,
        ...caller
    };
    if (flag > 1) {
        config.pause = "click";
    }
    return (config.pause === false ? 1 : 0) + (config.pause === "click" ? 10 : 0) +
        (config.keyboard ? 100 : 0);
}

var plain = options(0);
var overridden = options(3);
var cleared = options(9);
var merged_default = merged(0);
var merged_click = merged(3);
