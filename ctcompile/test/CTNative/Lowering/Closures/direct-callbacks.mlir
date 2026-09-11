// RUN: split-file %s %t
// RUN: python3 %S/check-direct-callbacks.py --translate ctjs-translate --opt ctjs-opt --fixtures %t --work %t.executables

// The checker names selected callback calls in source-derived IR, preserving
// their actual callback values. Both forms must lower and run identically.

//--- positive.js
var direct42 = (function invoke(factory, value) {
    return factory(value);
})(function doubleIt(value) { return value * 2; }, 21);

var mixed85 = (function invokeMixed(factory, value) {
    return factory(value) + factory(value + 1);
})(function increment(value) { return value + 1; }, 41);

var padded73 = (function invokePadded(factory) {
    return factory(7);
})(function pair(left, right) { return left * 10 + (right || 3); });

var owned42 = (function invokeFactory(factory, seed) {
    const ns = {exports: factory(seed)};
    return ns.exports;
})(function make(seed) {
    const state = new Map();
    state.set("value", seed);
    return {get() { return state.get("value") + 0; }};
}, 42).get();

//--- guards.mlir
module {
  ctjs.func @_script_$0(%receiver: !ctjs.value, %new_target: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %undefined = ctjs.constant #ctjs.undefined
    %wrapper = ctjs.create_closure %callee[1] this %undefined
    %factory = ctjs.create_closure %callee[2] this %undefined
    %number = ctjs.constant #ctjs.number<4631107791820423168>
    %result = ctjs.call_direct @wrapper$1(%undefined, %undefined, %wrapper, %factory, %number)
    // EXTRA_CALLER
    ctjs.store_global "result", %result
    ctjs.return %result
  }
  ctjs.func @wrapper$1(%receiver: !ctjs.value, %new_target: !ctjs.value, %callee: !ctjs.value, %factory: !ctjs.value, %value: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %undefined = ctjs.constant #ctjs.undefined
    // WRAPPER_USE
    %result = ctjs.call_direct @factory$2(%undefined, %undefined, %factory, %value)
    ctjs.return %result
  }
  ctjs.func @factory$2(%receiver: !ctjs.value, %new_target: !ctjs.value, %callee: !ctjs.value, %value: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    // FACTORY_USE
    ctjs.return %value
  }
  // EXTRA_FUNCTION
}
