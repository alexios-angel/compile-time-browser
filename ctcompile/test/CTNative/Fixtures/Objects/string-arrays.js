// Arrays of Strings lower to std::vector<ctnative::js_string>: literals,
// push (with its new length), length, index reads, join, sort in UTF-16
// order, split by a String and by /\s+/, and an array carried through a loop.
function pushed() {
    var words = ['b', 'a'];
    words.push('c' + 'd');
    return words.push('e');
}
function joined() {
    var words = ['b', 'a'];
    words.push('cd');
    return words.join('-');
}
function firstOf() {
    var words = ['x', 'y'];
    return words[0];
}
function pastTheEnd() {
    var words = ['x'];
    return words[3];
}
function sizeOf() {
    var words = [];
    words.push('a');
    words.push('b');
    return words.length;
}
function sortedDefault() {
    var words = ['b', 'C', 'a'];
    return words.sort().join();
}
function pieces() {
    return 'a,b,,c'.split(',').join('|');
}
function classes(text) {
    return text.split(/\s+/).sort().join('.');
}
function astral() {
    return ['Ａ', '😀', 'z'].sort().join(' ');
}
function emptyJoin() {
    var words = [];
    return words.join(',');
}
function counted() {
    var words = [];
    for (var i = 0; i < 3; i++) {
        words.push('n' + i);
    }
    return words.join(',');
}
var pushedLength = pushed();
var joinedText = joined();
var first = firstOf();
var missing = pastTheEnd();
var size = sizeOf();
var sorted = sortedDefault();
var split = pieces();
var classList = classes('  btn btn-primary\tactive ');
var astralOrder = astral();
var empty = emptyJoin();
var loop = counted();
