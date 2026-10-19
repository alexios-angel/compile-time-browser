// ClosureValues answers which closures a value may hold, and
// --ctjs-test-closure-values writes that answer on every call through a value.
//
// handlers.js has the shape of Bootstrap's EventHandler: `P.on` and `P.one`
// hand a handler to `D`, which destructures `S`'s array, may wrap the handler
// for a custom event, and passes it through an IIFE into the wrapper that
// calls it with `apply`. `P.off` hands its own handler to `S` too, and the
// wrappers pass theirs back to `P.off`, so the set merges all of them. Each
// wrapper's `apply` may call the three handlers given to `P`, and the custom
// event wrapper (fn$5) that `D` makes of them.
//
// Each variant must stay unknown, with its reason: without that check it
// would answer a set missing a closure the call can enter (`missed`, `onKey`,
// or for escape.js another script's handler):
//   - prototypes.js writes Array.prototype[Symbol.iterator], so a
//     destructuring may not read the array's elements;
//   - mutated.js overwrites the destructured array's element;
//   - in methods.js (one method and one handler), self.js calls a method
//     that reaches `on` through `this`, unknown.js passes a handler read off
//     an object, inherited.js reads a key the literal does not own, which an
//     inherited getter answers, and escape.js stores the literal where any
//     script can call its methods;
//   - static.js passes a class's static method, which the class reassigns
//     first. A static read stays unknown: a static method's `__home` property
//     holds its class, so any code holding the method can reach the class and
//     rewrite its statics.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/handlers.js --mlir-print-debuginfo 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-test-closure-values | FileCheck %s
// RUN: sed 's|// prototypes: ||' %t/handlers.js > %t/prototypes.js
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/prototypes.js --mlir-print-debuginfo 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-test-closure-values | FileCheck %s --check-prefix=PROTOTYPES
// RUN: sed 's|// mutated: ||' %t/handlers.js > %t/mutated.js
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mutated.js --mlir-print-debuginfo 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-test-closure-values | FileCheck %s --check-prefix=MUTATED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/methods.js --mlir-print-debuginfo 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-test-closure-values | FileCheck %s --check-prefix=METHODS
// RUN: sed 's|// self: ||' %t/methods.js > %t/self.js
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/self.js --mlir-print-debuginfo 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-test-closure-values | FileCheck %s --check-prefix=SELF
// RUN: sed 's|// unknown: ||' %t/methods.js > %t/unknown.js
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/unknown.js --mlir-print-debuginfo 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-test-closure-values | FileCheck %s --check-prefix=UNKNOWN
// RUN: sed 's|// inherited: ||' %t/methods.js > %t/inherited.js
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/inherited.js --mlir-print-debuginfo 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-test-closure-values | FileCheck %s --check-prefix=INHERITED
// RUN: sed 's|// escape: ||' %t/methods.js > %t/escape.js
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/escape.js --mlir-print-debuginfo 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-test-closure-values | FileCheck %s --check-prefix=ESCAPE
// RUN: sed 's|// static: ||' %t/methods.js > %t/static.js
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/static.js --mlir-print-debuginfo 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-test-closure-values | FileCheck %s --check-prefix=STATIC

// D calls S, and the custom-event arrow that wraps a handler.
// CHECK-LABEL: ctjs.func @D$3(
// CHECK: ctjs.call {{.*}} {ctjs.test_closures = ["S$2"]}
// CHECK: ctjs.call {{.*}} {ctjs.test_closures = ["fn$4"]}
// The custom-event wrapper calls what it wraps, with `call`. Its parameter
// is read in its own call's frame (ClosureValues.cpp, inFrame), so the answer
// is only what D hands the arrow there, not the wrapper itself.
// CHECK-LABEL: ctjs.func @fn$5(
// CHECK: ctjs.call {{.*}} {ctjs.test_closures = ["fn$15", "onClick$10", "onItem$14"]}
// The delegated and the direct wrapper call the handler with `apply`.
// CHECK-LABEL: ctjs.func @n$7(
// CHECK: ctjs.call {{.*}} {ctjs.test_closures = ["fn$15", "fn$5", "onClick$10", "onItem$14"]}
// CHECK-LABEL: ctjs.func @i$9(
// CHECK: ctjs.call {{.*}} {ctjs.test_closures = ["fn$15", "fn$5", "onClick$10", "onItem$14"]}

// PROTOTYPES-LABEL: ctjs.func @n$7(
// PROTOTYPES: ctjs.call {{.*}} {ctjs.test_closures_unknown = "an array destructuring, and the program may change how arrays iterate: a prototype table is used other than to read it (ctjs.get_property at {{.*}}prototypes.js:35)"}

// MUTATED-LABEL: ctjs.func @n${{[0-9]+}}(
// MUTATED: ctjs.call {{.*}} {ctjs.test_closures_unknown = "what `S$2` returns is also used by ctjs.set_property at {{.*}}mutated.js:11"}

// METHODS-LABEL: ctjs.func @fn$2(
// METHODS: ctjs.call {{.*}} {ctjs.test_closures = ["onClick$3"]}

// SELF-LABEL: ctjs.func @fn$2(
// SELF: ctjs.call {{.*}} {ctjs.test_closures_unknown = "`fn$3` uses `this`, which may be an object literal it is a method of"}

// UNKNOWN-LABEL: ctjs.func @fn$2(
// UNKNOWN: ctjs.call {{.*}} {ctjs.test_closures_unknown = "the result of ctjs.get_property at {{.*}}unknown.js:9"}

// INHERITED-LABEL: ctjs.func @fn$2(
// INHERITED: ctjs.call {{.*}} {ctjs.test_closures_unknown = "the object literal holding `on` is read with a key it does not own (ctjs.get_property at {{.*}}inherited.js:10)"}

// ESCAPE-LABEL: ctjs.func @fn$2(
// ESCAPE: ctjs.call {{.*}} {ctjs.test_closures_unknown = "the object literal holding `on` is used by ctjs.set_property at {{.*}}escape.js:11"}

// STATIC-LABEL: ctjs.func @fn$2(
// STATIC: ctjs.call {{.*}} {ctjs.test_closures_unknown = "the result of ctjs.get_property at {{.*}}static.js:12"}

//--- handlers.js
(function () {
  "use strict";
  const C = { mouseenter: "mouseover" };
  function S(t, e, i) {
    const n = "string" == typeof e, s = n ? i : e || i;
    return [n, s, t];
  }
  function D(t, e, i, n, s) {
    if ("string" != typeof e || !t) return;
    const row = S(e, i, n);
    // mutated: row[1] = function missed(event) { return 5; };
    let [o, r, a] = row;
    if (e in C) {
      const t = t => function (e) { return t.call(this, e); };
      r = t(r);
    }
    const u = o ? function (t, e, i) {
      return function n(s) { return n.oneOff && P.off(t, s.type, e, i), i.apply(t, [s]); };
    }(t, i, r) : function (t, e) {
      return function i(n) { return i.oneOff && P.off(t, n.type, e), e.apply(t, [n]); };
    }(t, r);
    u.oneOff = s;
    t.addEventListener(a, u, o);
  }
  const P = {
    on(t, e, i, n) { D(t, e, i, n, false); },
    one(t, e, i, n) { D(t, e, i, n, true); },
    off(t, e, i, n) { const [s, o, r] = S(e, i, n); t.removeEventListener(r, o, s); }
  };
  function onClick(event) { return 1; }
  P.on(document, "click", onClick);
  P.one(document, "keydown", ".item", function onItem(event) { return 2; });
  P.on(document, "mouseenter", event => 3);
  P.off(document, "click", onClick);
  // prototypes: Object.defineProperty(Array.prototype, Symbol.iterator, { value: function* () { yield 0; yield function missed(event) { return 5; }; } });
})();

//--- methods.js
(function () {
  "use strict";
  const P = {
    on(t, h) { return h.call(t, 1); },
    // self: fire(t, h) { return this.on(t, h); },
  };
  P.on(document, function onClick(event) { return 1; });
  // self: P.fire(document, function missed(event) { return 2; });
  // unknown: P.on(document, { onKey(event) { return 3; } }.onKey);
  // inherited: Object.defineProperty(Object.prototype, "spy", { get() { return this.on(document, function missed(event) { return 4; }); } }); P.spy;
  // escape: window.EventHandler = P;
  // static: class Keys { static onKey(event) { return 3; } } Keys.onKey = function missed(event) { return 4; }; P.on(document, Keys.onKey);
})();
