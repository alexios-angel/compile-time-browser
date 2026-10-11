// String.prototype.replace refusals: one program for each reason a
// replacement function stays refused. The accepted calls are run end to end in
// ../../Fixtures/Objects/string-replace.js.
//
// Each program is its own file under split-file: one refusal anywhere refuses
// the whole call graph, so two clauses in one program would test neither.

// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/unicode-class.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=UNICODE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/position.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=POSITION
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/variable-pattern.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=VARIABLE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/stored.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=STORED

// --- the pattern uses `\s`, which JavaScript reads as Unicode white space and std::regex as the locale's ---
// UNICODE: String.prototype.replace with `\\s` - its meaning depends on Unicode in JavaScript and on the locale in std::regex

// --- the callback reads the match position, which JavaScript counts in UTF-16 units ---
// POSITION: String.prototype.replace whose callback declares 2 parameters - only the match and its groups are passed, since the position counts UTF-16 units

// --- the RegExp is not a literal at the call, so its pattern is not known when compiling ---
// VARIABLE: it is passed as an argument - lifting has no call site to move the captures to

// --- the callback is also stored, so the call is not its only use ---
// STORED: a closure used as a value

//--- unicode-class.js
function strip(text) {
    return text.replace(/\s/g, function (space) {
        return "_";
    });
}
var answer = strip("a b");

//--- position.js
function mark(text) {
    return text.replace(/b/g, function (found, at) {
        return found + at;
    });
}
var answer = mark("abc");

//--- variable-pattern.js
function swap(text, pattern) {
    return text.replace(pattern, function (found) {
        return found + found;
    });
}
var answer = swap("ab", /a/g);

//--- stored.js
var kept = 0;

function keep(text) {
    var twice = function (found) {
        return found + found;
    };
    kept = twice;
    return text.replace(/a/g, twice);
}
var answer = keep("ab");
