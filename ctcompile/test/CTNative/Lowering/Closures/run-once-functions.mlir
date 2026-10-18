// A static variable of a run-once function that holds one function is bound
// when it is only ever called.
//
// The local-function rule (ClosureLifting/Bindings.cpp) leaves a function
// binding in a box when the closure it holds captures a binding that rule
// cannot erase; in Bootstrap, Popper's computeAutoPlacement calls
// detectOverflow, whose function is also stored into an object. A closure the
// lift would refuse captures both, so both boxes become static variables
// (RunOnce.cpp). The variable holding `auto` below is then written once, with a
// closure that captures nothing, and every read of it is the callee of a call
// that runs after that write, so ClosureLifting/Singletons.cpp makes those
// calls direct, its self-call included, and the closure lowers to nothing.
// `detect` stays: one read stores it into an object (or passes it to `map`),
// and the static box says so instead of only saying it is static.
//
// The programs stay refused as a whole (`auto` next reads `detect`, which
// holds a boxed function), so the checks are on the IR. The negative reads
// the variable in a function that can run before the variable is written.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/bound.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=BOUND --implicit-check-not="which other functions read by name"
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/callback.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=CALLBACK --implicit-check-not="which other functions read by name"
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/early.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=EARLY --implicit-check-not="which other functions read by name"

// `detect` is stored into a literal in the factory, so its variable stays.
// BOUND: ctjs.create_closure %arg2[2] {{.*}}ctnative.closure_reason = "it is the value of a local binding this tier cannot call directly: it is a static variable of a function that runs once, read in `fn$1` where the name reaches `ctjs.set_property`, so the binding holds a function VALUE and this step makes none"
// `auto`'s closure lowers to nothing, and its function is private: every
// call of it is direct, the self-call too.
// BOUND: ctjs.create_closure %arg2[3] {{.*}}ctnative.lifted
// BOUND: ctjs.func private @auto$3(
// BOUND-SAME: ctnative.not_native = "it reads `{{[a-z_0-9]+}}`, a static variable of a function that runs once, which holds a function of type !ctnative.boxed
// BOUND: ctjs.call_direct @auto$3(
// BOUND: ctjs.call_direct @auto$3(
// BOUND: ctjs.call_direct @auto$3(

// CALLBACK: ctnative.closure_reason = "it is the value of a local binding this tier cannot call directly: it is a static variable of a function that runs once, read in `auto$3` where the name reaches `ctjs.call`, so the binding holds a function VALUE and this step makes none"
// CALLBACK: ctjs.create_closure %arg2[3] {{.*}}ctnative.lifted
// CALLBACK: ctjs.call_direct @auto$3(

// EARLY: ctnative.closure_reason = "it is the value of a local binding this tier cannot call directly: it is a static variable of a function that runs once, and a read in `fn${{[0-9]+}}` can run before the function is written"

//--- bound.js
var r = (function () {
    var log = 0;
    function detect(n) { log = log * 10 + n; return n; }
    const exported = { detect };
    function auto(n) { return n <= 0 ? detect(1) : auto(n - 1) + 1; }
    function make() { return () => auto(1) + auto(2); }
    const fn = make();
    return fn() * 1000 + log;
})();

//--- callback.js
var r = (function () {
    var log = 0;
    function detect(n) { log = log * 10 + n; return n; }
    function auto(n) { return detect(n) + [n].map(detect)[0]; }
    function make() { return () => auto(1) + auto(2); }
    const fn = make();
    return fn() * 1000 + log;
})();

//--- early.js
var r = (function () {
    var log = 0;
    function detect(n) { log = log * 10 + n; return n; }
    const exported = { detect };
    function make() { return () => auto(1); }
    const fn = make();
    if (log > 5) { fn(); }
    var auto = function (n) { return detect(n) + 1; };
    return fn() * 1000 + log;
})();
