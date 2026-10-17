// A hoisted function that names a function declared after it is still a
// local function binding.
//
// Function declarations are hoisted: a frame's prologue stores each closure
// into its binding in source order, so `function S() { N(); }` is stored into
// S's binding before N's binding is written. S's closure captures N's
// binding, but S can only run through a read of its own binding, and those
// reads come after the prologue. writeReachesEveryRead follows a capturing
// closure stored into another binding of the frame to that binding's reads
// (holderReadsFollow), so N is bound and every call through the names is
// direct. Bootstrap's EventHandler declares getTypeEvent (N) after
// normalizeParameters (S) and addHandler (D), and hydrateObj (j) after the
// EventHandler table that calls it.
//
// The positive lowers completely, with the table's methods called directly,
// and prints the same `r` as Node and the interpreter with both compilers.
// The mutual pair, formerly refused, does the same for two function
// expressions that call each other. The negative reads the earlier binding before the later one is written, so
// the later one keeps its box and its reason.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/hoisted.js --mlir-print-debuginfo 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/hoisted.mlir
// RUN: FileCheck %s --check-prefix=HOISTED --implicit-check-not=ctnative.not_native < %t/hoisted.mlir
// RUN: ctjs-opt %t/hoisted.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/hoisted.cpp
// RUN: %gxx -O2 %t/hoisted.cpp -o %t/hoisted-gcc
// RUN: %t/hoisted-gcc | FileCheck %s --check-prefix=OBSERVATION
// RUN: %clangxx -O2 %t/hoisted.cpp -o %t/hoisted-clang
// RUN: %t/hoisted-clang | FileCheck %s --check-prefix=OBSERVATION
// RUN: %native_reference %t/hoisted.js | FileCheck %s --check-prefix=OBSERVATION
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/hoisted.js | FileCheck %s --check-prefix=OBSERVATION
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mutual.js --mlir-print-debuginfo 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/mutual.mlir
// RUN: FileCheck %s --check-prefix=MUTUAL --implicit-check-not=ctnative.not_native < %t/mutual.mlir
// RUN: ctjs-opt %t/mutual.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/mutual.cpp
// RUN: %gxx -O2 %t/mutual.cpp -o %t/mutual-gcc
// RUN: %t/mutual-gcc | FileCheck %s --check-prefix=MUTUALRUN
// RUN: %clangxx -O2 %t/mutual.cpp -o %t/mutual-clang
// RUN: %t/mutual-clang | FileCheck %s --check-prefix=MUTUALRUN
// RUN: %native_reference %t/mutual.js | FileCheck %s --check-prefix=MUTUALRUN
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/mutual.js | FileCheck %s --check-prefix=MUTUALRUN
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/early.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=EARLY

// HOISTED-NOT: static_cell
// HOISTED-NOT: singleton_reason
// HOISTED: emitc.func @main
// OBSERVATION: r=60444

// Two function expressions that call each other: one of the two stores
// comes second, yet both names are read only after both stores.
// MUTUAL: emitc.func @main
// MUTUALRUN: r=1

// EARLY: it is the value of a local binding this tier cannot call directly: its one assignment does not reach every read of it

//--- hoisted.js
var r = (function () {
    var log = 0;
    function S(t, e) { return N(t) + e; }
    function D(t, e) { log = log * 10 + S(t, e); return j(t); }
    const P = {
        on(t, e) { return D(t, e); },
        off(t) { log = log * 10 + N(t); return 0; },
    };
    function N(t) { return t + 1; }
    function j(t) { return t * 2; }
    function make() { return () => P.on(1, 2) + P.off(3) + P.on(2, 1); }
    const fn = make();
    return fn() * 10000 + log;
})();

//--- mutual.js
function mutual(k) {
    var even_ish = function (n) { if (n <= 0) { return 1; } return odd_ish(n - 1); };
    var odd_ish = function (n) { if (n <= 0) { return 0; } return even_ish(n - 1); };
    return even_ish(k) * 10 + odd_ish(k);
}
var r = mutual(3);

//--- early.js
var r = (function () {
    var log = 0;
    var S = function (t) { return N(t) + 1; };
    log = S(1);
    var N = function (t) { return t + 1; };
    return log + S(2);
})();
