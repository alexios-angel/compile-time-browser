// Bootstrap 5.3.8 Data's diagnostic expression, with a numeric observation
// instead of console publication. The first-key read remains optional.
function diagnostic(key, populated) {
    const map = new Map();
    if (populated) { map.set(key, 7); }
    return `Bootstrap doesn't allow more than one instance per element. Bound instance: ${Array.from(map.keys())[0]}.`;
}
var bootstrap_message = diagnostic("bs.modal", true) === "Bootstrap doesn't allow more than one instance per element. Bound instance: bs.modal." ? 42 : 0;
var bootstrap_empty = diagnostic("bs.modal", false) === "Bootstrap doesn't allow more than one instance per element. Bound instance: undefined." ? 43 : 0;
var bootstrap_empty_key = diagnostic("", true) === "Bootstrap doesn't allow more than one instance per element. Bound instance: ." ? 47 : 0;

function firstKey(key, populated) {
    const map = new Map();
    if (populated) { map.set(key, 1); }
    return Array.from(map.keys())[0];
}
function missing() { return; }
function stringFlags(value) {
    var flags = 0;
    if (value === missing()) { flags = flags + 1; }
    if (value === null) { flags = flags + 2; }
    if (value == null) { flags = flags + 4; }
    if (value === "") { flags = flags + 8; }
    if (value) { flags = flags + 16; }
    if (!value) { flags = flags + 32; }
    if (typeof value === "string") { flags = flags + 64; }
    if (typeof value === "undefined") { flags = flags + 128; }
    if (typeof value === "object") { flags = flags + 256; }
    return flags;
}
function decorated(value) { return "[" + value + "]"; }
var absent_flags = stringFlags(firstKey("seed", false));
var empty_flags = stringFlags(firstKey("", true));
var present_flags = stringFlags(firstKey("bs.modal", true));
var null_flags = stringFlags(null);
var null_text = decorated(null) === "[null]" ? 53 : 0;
var missing_text = decorated(firstKey("seed", false)) === "[undefined]" ? 59 : 0;
var present_text = decorated(firstKey("bs.modal", true)) === "[bs.modal]" ? 61 : 0;

function preservedSnapshots() {
    const map = new Map();
    map.set("0123456789abcdef0123456789abcdef", 1);
    map.set("second", 2);
    map.set("0123456789abcdef0123456789abcdef", 3);
    const original = Array.from(map.keys());
    const copied = Array.from(map.keys());
    map.delete("0123456789abcdef0123456789abcdef");
    map.set("0123456789abcdef0123456789abcdef", 4);
    const reordered = Array.from(map.keys());
    map.clear();
    return original.length === 2 && copied.length === 2 &&
        original[0] === "0123456789abcdef0123456789abcdef" && copied[1] === "second" &&
        reordered[0] === "second" && reordered[1] === "0123456789abcdef0123456789abcdef" &&
        typeof copied[2] === "undefined" && map.size === 0 ? 67 : 0;
}
var independent_snapshots = preservedSnapshots();

function indexed(key) {
    const map = new Map();
    map.set("", 1);
    map.set("a\0b", 2);
    map.set("caf\u00e9\u{1f600}", 3);
    map.set("\ud800", 4);
    return Array.from(map.keys())[key];
}
var fractional_zero = stringFlags(indexed(-0.5));
var fractional_one = indexed(1.9) === "a\0b" ? 71 : 0;
var unicode_key = indexed(2) === "caf\u00e9\u{1f600}" ? 73 : 0;
var surrogate_key = indexed(3) === "\ud800" ? 79 : 0;
var negative_index = stringFlags(indexed(-1));
var bounds_index = stringFlags(indexed(4));
var infinity_index = stringFlags(indexed(1 / 0));
var nan_index = stringFlags(indexed(0 / 0));
var null_index = stringFlags(indexed(null));
var undefined_index = stringFlags(indexed(missing()));

function selected(flag) { return flag ? firstKey("bs.modal", true) : null; }
var selected_null = stringFlags(selected(false));
var selected_string = stringFlags(selected(true));

function numericCopies() {
    const map = new Map();
    map.set(3, 5);
    const keys = Array.from(map.keys());
    const values = Array.from(map.values());
    map.clear();
    return keys[0] + values[0];
}
var numeric_copy = numericCopies();
