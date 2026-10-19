// Array callbacks that capture a binding the callback or its frame writes, so
// the callback takes the binding by the address of its variable: a forEach
// that adds into a total, a filter whose limit is written after the callback
// is made, a sort comparator that counts its calls (the runtime's merge sort
// must call it as often as the interpreter's), a map that keeps the last
// String it saw, a binding only the callback assigns and one the frame sets
// to a String and the callback to a Number (both types come from the
// callback's writes), and a local function that hands its own pointer on to
// the forEach it makes. Every global is a scalar compared with the
// interpreter.
function words() {
    return ['card', 'btn', 'dropdown', 'nav', 'modal', 'tab'];
}
function totalLength() {
    var total = 0;
    words().forEach(function (s) {
        total = total + s.length;
    });
    return total;
}
function longerThanLater() {
    var limit = 1;
    var kept = words().filter(function (s) {
        return s.length > limit;
    });
    limit = 4;
    return kept.length + limit;
}
function comparisons() {
    var calls = 0;
    var sorted = words().sort(function (a, b) {
        calls = calls + 1;
        return a.length - b.length;
    });
    return calls * 100 + sorted.length;
}
function lastSeen() {
    var last = 'none';
    var lengths = words().map(function (s) {
        last = s;
        return s.length;
    });
    return last + lengths.length;
}
function lastLength() {
    var seen;
    words().forEach(function (s) {
        seen = s.length;
    });
    return seen;
}
function lengthOrName() {
    var seen = 'none';
    words().forEach(function (s) {
        seen = s.length;
    });
    return seen;
}
function handedOn() {
    var count = 0;
    function add(list) {
        list.forEach(function (s) {
            count = count + s.length;
        });
    }
    add(['ab', 'c']);
    add(['def']);
    return count;
}
var total = totalLength();
var keptPlusLimit = longerThanLater();
var sortCalls = comparisons();
var last = lastSeen();
var lastLengthSeen = lastLength();
var lengthOrNameSeen = lengthOrName();
var counted = handedOn();
