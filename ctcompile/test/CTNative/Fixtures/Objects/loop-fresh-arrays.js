// An empty array literal inside a loop body makes a new array on every
// iteration, as `var pairs = [];` does in tools/check/css-dump.js's element
// loop. Each line must hold only its own iteration's elements: an array kept
// across iterations would give lengths 2, 4, 6 and repeat earlier entries.
// Every global is a scalar compared with the interpreter.
function rows(text) {
    var parts = text.split(',');
    var out = '';
    for (var i = 0; i < parts.length; i++) {
        var pairs = [];
        pairs.push(parts[i]);
        pairs.push('x');
        out = out + pairs.length + ':' + pairs.join('-') + ';';
    }
    return out;
}
var a = rows('p,q,r');
