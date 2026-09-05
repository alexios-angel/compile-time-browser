// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %S/../../native-returned-closure-fixture.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=NATIVE --implicit-check-not=ctnative.not_native --implicit-check-not=ctjs.func
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mutable.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MUTABLE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/late.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=LATE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/late-store.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=LATESTORE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/mixed.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MIXED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/missing.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=MIXED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/identity.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=IDENTITY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/this.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=THIS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/array.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=ARRAY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/methods.js | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc | FileCheck %s --check-prefix=METHODS

// NATIVE: emitc.verbatim {{.*}}using ctn_env_{{.*}} = std::tuple<std::shared_ptr<ctnative::number_map<std::string>>>;
// NATIVE: emitc.verbatim {{.*}}std::tuple<std::string>
// NATIVE: emitc.verbatim {{.*}}std::tuple<bool, double, double>
// NATIVE: emitc.declare_func @makeStore_1
// NATIVE: emitc.func @makeStore_1({{.*}}) -> !emitc.opaque<"ctnative::ctn_env_
// NATIVE: call_opaque "std::make_tuple"
// NATIVE: emitc.func @forwardCallable_3({{.*}}!emitc.opaque<"ctnative::ctn_env_
// NATIVE: emitc.func @invokeCallable_4
// NATIVE: call_opaque "std::get<0>"
// MUTABLE: returned closure capture 0 is a mutable or late-initialized binding; it needs an owning shared cell
// LATE: returned closure: capture 0 is a binding whose value does not reach this call of it
// LATESTORE: returned closure: capture 0 is a binding whose single assignment does not dominate this call of it
// MIXED: returned closure flow contains another callable or a non-callable producer
// IDENTITY: returned closure escapes or is inspected through `ctjs.compare`
// THIS: returned closure reads `this`
// ARRAY: an array literal that escapes - it reaches `ctjs.create_closure`
// METHODS: emitc.func @make_1({{.*}}) -> !emitc.opaque<"std::shared_ptr<ctnative::method_table_0>">

//--- mutable.js
function make() { let value = 0; return () => ++value; }
function probe() { const fn = make(); return fn() + fn(); }
probe();

//--- late.js
// Copying at creation would capture undefined; copying the later 42 would
// hide the missing lifetime proof. The binding needs an owning shared cell.
function make() { var value; const fn = () => value; value = 42; return fn; }
function probe() { const fn = make(); return fn(); }
probe();

//--- mixed.js
function first() { return value => value + 1; }
function second() { return value => value + 2; }
function invoke(fn) { return fn(40); }
function probe() { return invoke(first()) + invoke(second()); }
probe();

//--- late-store.js
// The value exists, but the captured binding has not been assigned yet.
function make(n) {
    var value;
    const saved = n + 1;
    const fn = () => value;
    value = saved;
    return fn;
}
function probe() { const fn = make(41); return fn(); }
probe();

//--- missing.js
function make() { return value => value + 1; }
function invoke(fn) { return fn(40); }
function probe() { invoke(make()); return invoke(); }
probe();

//--- identity.js
function make() { return value => value + 1; }
function probe() { const first = make(); const second = make(); first(1); return first === second; }
probe();

//--- this.js
function make() { return function () { return this.value; }; }
function probe() { const fn = make(); return fn(); }
probe();

//--- array.js
function make() { const values = [42]; return () => values[0]; }
function probe() { const fn = make(); return fn(); }
probe();

//--- methods.js
function make() { const value = 42; return { get() { return value; } }; }
function probe() { const methods = make(); return methods.get(); }
probe();
