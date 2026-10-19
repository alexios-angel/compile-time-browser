// Equality on an optional Number or Boolean keeps the runtime tag, so an
// undefined value is never confused with NaN or false. Both programs lower
// through the same tagged strict comparison, nullable_scalar's `==`, whose
// left operand keeps the tag.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/undefined.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false \
// RUN:   | FileCheck %s --check-prefix=UNDEF
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/notanumber.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc=optimize=false \
// RUN:   | FileCheck %s --check-prefix=NAN

// UNDEF: emitc.func @eqnum_1({{.*}}) -> !emitc.opaque<"ctnative::js_num">
// UNDEF: cmp eq, {{.*}} : (!emitc.opaque<"ctnative::nullable_scalar">, !emitc.opaque<"ctnative::js_num">) -> i1
// UNDEF-NOT: ctnative.not_native
// NAN: emitc.func @eqbool_1({{.*}}) -> !emitc.opaque<"ctnative::js_num">
// NAN: cmp eq, {{.*}} : (!emitc.opaque<"ctnative::nullable_scalar">, !emitc.opaque<"ctnative::js_boolean_t">) -> i1
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
