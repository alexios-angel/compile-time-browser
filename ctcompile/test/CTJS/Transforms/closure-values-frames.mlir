// ClosureValues follows a call's result into its callee in that call's frame
// (ClosureValues.cpp, inFrame): there the callee's parameters take the call's
// arguments, not every caller's.
//
// frames.js has the shape of Bootstrap's EventHandler, whose `P.off` hands
// `S` a handler read off an object (Tooltip.dispose's
// `this._hideModalHandler`). `w` calls the handler `D` takes from `S`'s
// array and the one `pick` returns to `D`. Both answers are `onClick`: the
// read reaches `S` and `pick` only through `P.off`'s calls, whose frames are
// their own.
//
// Each variant must stay unknown, with its reason:
//   - open.js gives `P.on` the same read, so `w` may call it;
//   - in shared.js, `S` passes the handler through a binding of the enclosing
//     function. Every call of `S` shares that binding's cell, so `P.off`'s
//     read reaches `D` through it. `pick` is unchanged and keeps its answer.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/frames.js --mlir-print-debuginfo 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-test-closure-values | FileCheck %s
// RUN: sed 's|// open: ||' %t/frames.js > %t/open.js
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/open.js --mlir-print-debuginfo 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-test-closure-values | FileCheck %s --check-prefix=OPEN
// RUN: sed 's|return \[s\];|last = s; return [last];|' %t/frames.js > %t/shared.js
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/shared.js --mlir-print-debuginfo 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-test-closure-values | FileCheck %s --check-prefix=SHARED

// CHECK-LABEL: ctjs.func @w${{[0-9]+}}(
// CHECK: ctjs.call {{.*}} {ctjs.test_closures = ["onClick${{[0-9]+}}"]}
// CHECK: ctjs.call {{.*}} {ctjs.test_closures = ["onClick${{[0-9]+}}"]}

// OPEN-LABEL: ctjs.func @w${{[0-9]+}}(
// OPEN: ctjs.call {{.*}} {ctjs.test_closures_unknown = "the result of ctjs.get_property at {{.*}}open.js:29"}
// OPEN: ctjs.call {{.*}} {ctjs.test_closures_unknown = "the result of ctjs.get_property at {{.*}}open.js:29"}

// SHARED-LABEL: ctjs.func @w${{[0-9]+}}(
// SHARED: ctjs.call {{.*}} {ctjs.test_closures_unknown = "the result of ctjs.get_property at {{.*}}shared.js:28"}
// SHARED: ctjs.call {{.*}} {ctjs.test_closures = ["onClick${{[0-9]+}}"]}

//--- frames.js
(function () {
  "use strict";
  let last;
  function S(e, i) {
    const s = "string" == typeof e ? i : e || i;
    return [s];
  }
  function pick(e, i) {
    return e || i;
  }
  function D(t, e, i) {
    let [r] = S(e, i);
    const q = pick(e, i);
    t.addEventListener("click", function w(n) {
      r.apply(t, [n]);
      return q.call(t, n);
    });
  }
  const P = {
    on(t, e, i) { D(t, e, i); },
    off(t, e, i) {
      const [o] = S(e, i);
      t.removeEventListener("click", o);
      return pick(e, i);
    }
  };
  P.on(document, function onClick(event) { return 1; });
  P.off(document, document.gone);
  // open: P.on(document, document.gone);
})();
