// Optional numeric and boolean equality retain their runtime tags. These
// programs formerly refused because undefined was represented by NaN/false;
// both now lower through the same tagged strict comparison.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/undefined.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=UNDEF
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/notanumber.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=NAN

// UNDEF: emitc.func @eqnum_1({{.*}}) -> f64
// UNDEF: call_opaque "ctnative::scalar_strict_equal"
// UNDEF-NOT: ctnative.not_native
// NAN: emitc.func @eqbool_1({{.*}}) -> f64
// NAN: call_opaque "ctnative::scalar_strict_equal"
// NAN-NOT: ctnative.not_native

//--- undefined.js
function eqnum(n) {
  var u;
  if (n > 0) { u = 1; }
  return u === 1 ? 1 : 0;
}
var r = eqnum(3);

//--- notanumber.js
function eqbool(n) {
  var u;
  if (n > 0) { u = true; }
  return u === true ? 1 : 0;
}
var r = eqbool(3);
