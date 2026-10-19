// Values a branch narrowed. EMIT checks that `typeof t == "string"`, its `!==`
// else-region and the truthiness of a String|Null insert ctnative.refine and
// lower with nothing refused. WRITTEN rebinds the tested parameter inside the
// branch, so the split reads a value no test narrowed and is refused; LEAKED
// splits after the branch, where the test proves nothing. CELL is Bootstrap's
// `_getOffset`: `t` stays a cell because a closure that is not lifted
// captures it, the branch re-reads the cell, and the map callback still gets
// a String parameter (it is refused only because getOffset is, for the
// closure it returns); CELLWRITTEN has that closure write `t`, so the reads are
// not one value and the callback's parameter stays unproved. No refinement
// outlives the pass. The reachability proof reads a refused function with
// its refinements (Reachability/Analysis.cpp accepts ctnative.refine): DEAD
// keeps a refinement in the refused `render` and still removes css-dump.js's
// shape of dead callback, the forEach under `typeof PROPS !== 'undefined'`
// with PROPS absent.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/emit.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=EMIT --implicit-check-not=ctnative.not_native --implicit-check-not=ctnative.refine
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/dead.js --whole-page 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=DEAD --implicit-check-not=ctnative.unreachable_reason
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/dead.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=OPEN --implicit-check-not=ctnative.unreachable_refusals
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/written.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=WRITTEN
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/cell.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=CELL
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/cellwritten.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=CELLWRITTEN
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/leaked.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=LEAKED --implicit-check-not=ctnative.refine

// EMIT-DAG: call_opaque "ctnative::as_string"
// EMIT-DAG: call_opaque "ctnative::Array.prototype.map.call"
// WRITTEN: @written${{[0-9]+}}{{.*}}ctnative.not_native =
// DEAD: ctnative.unreachable_refusals = [{function = "fn${{[0-9]+}}", reason = "removed: unreachable
// OPEN: ctjs.load_global "PROPS"
// OPEN: ctnative.array_callback = @fn$[[CALLBACK:[0-9]+]]
// OPEN: ctjs.func @fn$[[CALLBACK]]
// OPEN-SAME: ctnative.not_native = "parameter 0 is !ctnative.boxed
// CELL-NOT: parameter 0 is !ctnative.boxed
// CELL: @fn${{[0-9]+}}{{.*}}ctnative.not_native = "called back by `getOffset${{[0-9]+}}`, which is not native"
// CELL-NOT: parameter 0 is !ctnative.boxed
// CELLWRITTEN: parameter 0 is !ctnative.boxed - no caller proves it
// LEAKED: @leaked${{[0-9]+}}{{.*}}ctnative.not_native =

//--- emit.js
function offsets(t) {
    return typeof t == 'string' ? t.split(',').map(function (p) {
        return Number.parseInt(p, 10);
    }).length : -1;
}
function parts(t) {
    return typeof t !== 'string' ? -1 : t.split(',').length;
}
function pick(flag) {
    return flag ? 'x,y' : null;
}
function targets(flag) {
    var e = pick(flag);
    return e ? e.split(',').length : 0;
}
var a = offsets('1,2') + offsets(3) + parts('a') + parts(4) + targets(true) + targets(false);

//--- written.js
function other(flag) {
    return flag ? 'p,q' : 7;
}
function written(t, flag) {
    if (typeof t == 'string') {
        t = other(flag);
        return t.split(',').length;
    }
    return 0;
}
var r = written('a,b', true) + written(1, false);

//--- leaked.js
function leaked(t) {
    var n = typeof t == 'string' ? 1 : 0;
    return t.split(',').length + n;
}
var r = leaked('a,b') + leaked(5);

//--- cell.js
function getOffset(config) {
    var t = config.offset;
    return typeof t == 'string' ? t.split(',').map(function (p) {
        return Number.parseInt(p, 10);
    }) : function (e) {
        return t(e);
    };
}
// A computed key opens the shape, so `config.offset` is unproved.
var config = {};
config['off' + 'set'] = '1,2';
var r = getOffset(config);

//--- cellwritten.js
function getOffset(config) {
    var t = config.offset;
    return typeof t == 'string' ? t.split(',').map(function (p) {
        return Number.parseInt(p, 10);
    }) : function (e) {
        t = e;
        return t;
    };
}
// A computed key opens the shape, so `config.offset` is unproved.
var config = {};
config['off' + 'set'] = '1,2';
var r = getOffset(config);

//--- dead.js
function render(value) {
    return typeof value == 'string' ? value.split(',').length + this.x : 0;
}
if (typeof PROPS !== 'undefined') {
    render(PROPS).forEach(function (line) {
        return line.foo.bar;
    });
}
var r = render('a,b');
