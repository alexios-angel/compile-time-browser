// A call through a value that is one of a proved set of closures.
//
// `const f = c ? (x => x + k) : (x => x * 3); f(5)` calls one of two
// closures, and no call site names either one. ClosureLifting/ClosureSets.cpp
// lifts both as members of a set: each stays a value bound to copies of its
// captures, type inference gives the merged value the lattice's
// `closure_set` type, and the emitted C++ is one struct per set, whose enum
// class tag names the member a value holds, whose fields are each member's
// captures, and whose call, which ctnative::invoke_callable makes, switches
// on the tag and calls that member directly. No std::variant or std::visit
// is emitted. The positive prints what Node and the interpreter print with
// both compilers; each negative pins the reason a set is refused.
//
// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/set.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/set.mlir
// RUN: FileCheck %s --check-prefix=SET --implicit-check-not=ctnative.not_native < %t/set.mlir
// RUN: ctjs-opt %t/set.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/set.cpp
// RUN: FileCheck %s --check-prefix=SETCPP --implicit-check-not=std::variant --implicit-check-not=std::visit < %t/set.cpp
// RUN: %gxx -O2 %t/set.cpp -o %t/set-gcc
// RUN: %t/set-gcc | FileCheck %s --check-prefix=SETOUT
// RUN: %clangxx -O2 %t/set.cpp -o %t/set-clang
// RUN: %t/set-clang | FileCheck %s --check-prefix=SETOUT
// RUN: %native_reference %t/set.js | FileCheck %s --check-prefix=SETOUT
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/set.js | FileCheck %s --check-prefix=SETOUT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/field.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/field.mlir
// RUN: FileCheck %s --check-prefix=FIELD --implicit-check-not=ctnative.not_native < %t/field.mlir
// RUN: ctjs-opt %t/field.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/field.cpp
// RUN: FileCheck %s --check-prefix=FIELDCPP --implicit-check-not=std::variant --implicit-check-not=std::visit < %t/field.cpp
// RUN: %gxx -O2 %t/field.cpp -o %t/field-gcc
// RUN: %t/field-gcc | FileCheck %s --check-prefix=FIELDOUT
// RUN: %clangxx -O2 %t/field.cpp -o %t/field-clang
// RUN: %t/field-clang | FileCheck %s --check-prefix=FIELDOUT
// RUN: %native_reference %t/field.js | FileCheck %s --check-prefix=FIELDOUT
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/field.js | FileCheck %s --check-prefix=FIELDOUT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/reassigned.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/reassigned.mlir
// RUN: FileCheck %s --check-prefix=REASSIGNED --implicit-check-not=ctnative.not_native < %t/reassigned.mlir
// RUN: ctjs-opt %t/reassigned.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/reassigned.cpp
// RUN: FileCheck %s --check-prefix=REASSIGNEDCPP --implicit-check-not=std::variant --implicit-check-not=std::visit < %t/reassigned.cpp
// RUN: %gxx -O2 %t/reassigned.cpp -o %t/reassigned-gcc
// RUN: %t/reassigned-gcc | FileCheck %s --check-prefix=REASSIGNEDOUT
// RUN: %clangxx -O2 %t/reassigned.cpp -o %t/reassigned-clang
// RUN: %t/reassigned-clang | FileCheck %s --check-prefix=REASSIGNEDOUT
// RUN: %native_reference %t/reassigned.js | FileCheck %s --check-prefix=REASSIGNEDOUT
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/reassigned.js | FileCheck %s --check-prefix=REASSIGNEDOUT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/thisset.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/thisset.mlir
// RUN: FileCheck %s --check-prefix=THISSET --implicit-check-not=ctnative.not_native < %t/thisset.mlir
// RUN: ctjs-opt %t/thisset.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/thisset.cpp
// RUN: FileCheck %s --check-prefix=THISSETCPP --implicit-check-not=std::variant --implicit-check-not=std::visit < %t/thisset.cpp
// RUN: %gxx -O2 %t/thisset.cpp -o %t/thisset-gcc
// RUN: %t/thisset-gcc | FileCheck %s --check-prefix=THISSETOUT
// RUN: %clangxx -O2 %t/thisset.cpp -o %t/thisset-clang
// RUN: %t/thisset-clang | FileCheck %s --check-prefix=THISSETOUT
// RUN: %native_reference %t/thisset.js | FileCheck %s --check-prefix=THISSETOUT
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/thisset.js | FileCheck %s --check-prefix=THISSETOUT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/thisescapes.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=THISESCAPES
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/paramset.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc -o %t/paramset.mlir
// RUN: FileCheck %s --check-prefix=PARAMSET --implicit-check-not=ctnative.not_native < %t/paramset.mlir
// RUN: ctjs-opt %t/paramset.mlir '--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))' | ctjs-translate --mlir-to-cpp > %t/paramset.cpp
// RUN: FileCheck %s --check-prefix=PARAMSETCPP --implicit-check-not=std::variant --implicit-check-not=std::visit < %t/paramset.cpp
// RUN: %gxx -O2 %t/paramset.cpp -o %t/paramset-gcc
// RUN: %t/paramset-gcc | FileCheck %s --check-prefix=PARAMSETOUT
// RUN: %clangxx -O2 %t/paramset.cpp -o %t/paramset-clang
// RUN: %t/paramset-clang | FileCheck %s --check-prefix=PARAMSETOUT
// RUN: %native_reference %t/paramset.js | FileCheck %s --check-prefix=PARAMSETOUT
// RUN: %node -e 'const fs = require("fs"), vm = require("vm"), scope = {}; vm.runInNewContext(fs.readFileSync(process.argv[1], "utf8"), scope); console.log("r=" + scope.r);' %t/paramset.js | FileCheck %s --check-prefix=PARAMSETOUT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/paramescapes.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=PARAMESCAPES
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/fieldescape.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=FIELDESCAPE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/fieldunproved.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=FIELDUNPROVED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/unguarded.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=UNGUARDED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/arity.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=ARITY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/result.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=RESULT
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/unproved.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=UNPROVED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/escapes.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=ESCAPES
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/written.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=WRITTEN

// Both closures are members bound to their captures, and both calls go
// through the set. Only fn_2 captures a value, the Number `k`; the call
// switches on the tag and calls each member directly.
// SET: emitc.func @run_1
// SET: call_opaque "closure_set_fn_2_fn_3::make_fn_2"
// SET: call_opaque "closure_set_fn_2_fn_3::make_fn_3"
// SET: call_opaque "ctnative::invoke_callable"
// SET: call_opaque "ctnative::invoke_callable"
// SETCPP: struct closure_set_fn_2_fn_3 {
// SETCPP-NEXT: enum class member : unsigned char { fn_2, fn_3 };
// SETCPP-NEXT: member tag{};
// SETCPP-NEXT: ctnative::js_num c0_0{};
// SETCPP-NEXT: static closure_set_fn_2_fn_3 make_fn_2(ctnative::js_num c0) {
// SETCPP: static closure_set_fn_2_fn_3 make_fn_3() {
// SETCPP: case member::fn_2: return fn_2(c0_0, std::forward<A>(args)...);
// SETCPP-NEXT: case member::fn_3: return fn_3(std::forward<A>(args)...);
// SETCPP: closure_set_fn_2_fn_3 {{v[0-9]+}} = closure_set_fn_2_fn_3::make_fn_2(
// SETCPP: closure_set_fn_2_fn_3 {{v[0-9]+}} = closure_set_fn_2_fn_3::make_fn_3();
// SETCPP: ctnative::invoke_callable(
// SETCPP: ctnative::invoke_callable(
// SETOUT: r=28

// The set reached through a field of an object literal, as Popper's
// modifiers hold their `fn`: the record is a struct whose field is the set's
// struct, defined ahead of it, read once into a local and once as
// `m.fn(1)`, whose `this` no member reads and so is dropped.
// FIELD: emitc.func @run_1
// FIELDCPP: struct closure_set_fn_2_fn_3 {
// FIELDCPP-NEXT: enum class member : unsigned char { fn_2, fn_3 };
// FIELDCPP-NEXT: member tag{};
// FIELDCPP-NEXT: ctnative::js_num c0_0{};
// FIELDCPP: class [[REC:ctn_[a-z_]+]] {
// FIELDCPP: closure_set_fn_2_fn_3 fn;
// FIELDCPP: ctnative::invoke_callable(
// FIELDCPP: ctnative::invoke_callable(
// FIELDOUT: r=28

// A later write of a third closure joins it to the set, and every value of
// the set, the first merge included, is the three-member set, none of whose
// members captures anything.
// REASSIGNED: emitc.func @run_1
// REASSIGNEDCPP: struct closure_set_fn_2_fn_3_fn_4 {
// REASSIGNEDCPP-NEXT: enum class member : unsigned char { fn_2, fn_3, fn_4 };
// REASSIGNEDCPP-NEXT: member tag{};
// REASSIGNEDCPP-NEXT: static closure_set_fn_2_fn_3_fn_4 make_fn_2() {
// REASSIGNEDCPP: closure_set_fn_2_fn_3_fn_4 fn;
// REASSIGNEDOUT: r=2108

// Two arrows that read their method's `this`, merged by an `if` in the
// method and called there. Each member binds the receiver as the pointer the
// method passes (ArrowThis.cpp); the set never leaves the method's frame, so
// no call through it outlives the receiver.
// THISSET: emitc.func @pick_
// THISSETCPP: struct closure_set_[[A:fn_[0-9]+]]_[[B:fn_[0-9]+]] {
// THISSETCPP: ctnative::parameter_t<[[A]], 0> c0_0{};
// THISSETCPP-NEXT: ctnative::parameter_t<[[B]], 0> c1_0{};
// THISSETCPP-NEXT: static closure_set_[[A]]_[[B]] make_[[A]](ctnative::parameter_t<[[A]], 0> c0) {
// THISSETCPP: ctnative::invoke_callable(
// THISSETOUT: r=1424

// Negative: one arrow also hands its `this` to an array, so the receiver is
// not a pointer the member can bind, and the set keeps the refusal.
// THISESCAPES: closure_reason = "a closure called through a set of closures captures its lexical `this`, which a member does not carry"

// The set reached through a parameter, as an event registry's `on` stores
// the handler it is given: `on(reg, h)` writes `h` into `reg.click`, every
// call passes a closure for `h`, and every call passes the one literal `reg`,
// so the parameter and the field are values of the set, and `reg` stays a
// record whose `click` is the set's struct: the first member captures
// nothing and the other two capture one Number each.
// PARAMSET: emitc.func @on_
// PARAMSETCPP: struct closure_set_[[P:fn_[0-9]+_fn_[0-9]+_fn_[0-9]+]] {
// PARAMSETCPP-NEXT: enum class member
// PARAMSETCPP-NEXT: member tag{};
// PARAMSETCPP-NEXT: ctnative::js_num c1_0{};
// PARAMSETCPP-NEXT: ctnative::js_num c2_0{};
// PARAMSETCPP-NEXT: static closure_set_[[P]] make_
// PARAMSETCPP: closure_set_[[P]] click;
// PARAMSETOUT: r=1630

// Negative: the registry is also kept in an array, so a write of its field
// might happen out of sight, and the closures passed to `on` keep their
// refusal.
// PARAMESCAPES: closure_reason = "it is passed as an argument - lifting has no call site to move the captures to, so this needs a specialised callee (Phase 63), not a lift"

// Negative: the record is passed to a function, so a write of its field
// might happen out of sight, and it is no set.
// FIELDESCAPE: closure_reason = "it reaches `scf.yield`, which slice 1 does not lower"

// Negative: the field is assigned a function the caller passes.
// FIELDUNPROVED: closure_reason = "it reaches `scf.yield`, which slice 1 does not lower"

// Negative: a field only one branch writes may be absent where it is read,
// so the read is opt<closure_set>, and calling it untested would throw.
// UNGUARDED: ctnative.not_native = "it calls a field of a set of closures that may be absent - test it with `typeof f === \22function\22` first"

// Negative: one member declares a parameter the call does not pass. One C++
// signature serves the whole set, so both are refused.
// ARITY: closure_reason = "a closure called through a set of closures declares 2 parameters and a call through the set passes 1 - one C++ signature needs the same count"

// Negative: the members return a number and a string. Admission requires one
// result type for every member.
// RESULT: ctnative.not_native = "a borrowed callback's result is !ctnative.variant<!ctnative.num<f64>, !ctnative.str<utf8>>, not a Number, a Boolean or a String"

// Negative: the other arm is a function the caller passes, not a closure made
// here, so the set is not closed and the closure keeps its own refusal.
// UNPROVED: closure_reason = "it reaches `scf.yield`, which slice 1 does not lower"

// Negative: the merged value is returned rather than called, so it leaves
// the frame and is no set.
// ESCAPES: closure_reason = "it reaches `scf.yield`, which slice 1 does not lower"

// Negative: `k` is assigned after the closure captures it, and a member holds
// a copy of its captures.
// WRITTEN: closure_reason = "a closure called through a set of closures captures binding 0, which is written after it is captured - a member takes its captures by value"

//--- set.js
function run(c) {
    const k = c ? 2 : 4;
    const a = x => x + k;
    const b = x => x * 3;
    const f = c ? a : b;
    return f(5) + f(1);
}
var r = run(true) + run(false);

//--- arity.js
function run(c) {
    const a = x => x + 1;
    const b = (x, y) => x * 3;
    const f = c ? a : b;
    return f(5);
}
var r = run(true) + run(false);

//--- result.js
function run(c) {
    const a = x => x + 1;
    const b = x => "n" + x;
    const f = c ? a : b;
    return f(5);
}
var r = run(true);

//--- unproved.js
function run(c, g) {
    const a = x => x + 1;
    const f = c ? a : g;
    return f(5);
}
var r = run(true, function (x) { return x; });

//--- escapes.js
function pick(c) {
    const a = x => x + 1;
    const b = x => x * 3;
    return c ? a : b;
}
var r = pick(true)(5);

//--- written.js
function run(c) {
    let k = 1;
    const a = x => x + k;
    const b = x => x * 3;
    k = 10;
    const f = c ? a : b;
    return f(5);
}
var r = run(true);

//--- field.js
function run(c) {
    const k = c ? 2 : 4;
    const m = { name: "m", fn: c ? x => x + k : x => x * 3 };
    const o = m.fn;
    return o(5) + m.fn(1);
}
var r = run(true) + run(false);

//--- reassigned.js
function run(c) {
    const m = { fn: c ? x => x + 1 : x => x * 3 };
    const before = m.fn(5);
    m.fn = x => x - 1;
    return before * 100 + m.fn(5);
}
var r = run(true) + run(false);

//--- fieldescape.js
function run(c, keep) {
    const m = { fn: c ? x => x + 1 : x => x * 3 };
    keep(m);
    return m.fn(5);
}
var r = run(true, function (m) { return m; });

//--- fieldunproved.js
function run(c, g) {
    const m = { fn: c ? x => x + 1 : x => x * 3 };
    m.fn = g;
    return m.fn(5);
}
var r = run(true, function (x) { return x; });

//--- unguarded.js
function run(c, d) {
    const m = { name: "m", fn: c ? x => x + 1 : x => x * 3 };
    if (d) { m.effect = c ? x => x * 10 : x => x + 100; }
    return m.fn(5) + m.effect(1);
}
var r = run(true, true);

//--- thisset.js
function run() {
    class Box {
        constructor(x) { this._x = x; }
        pick(c) { const f = c ? y => y + this._x : y => y * this._x; return f(5) + f(1); }
    }
    const b = new Box(4);
    return b.pick(true) * 100 + b.pick(false);
}
var r = run();

//--- thisescapes.js
function run() {
    class Box {
        constructor(x) { this._x = x; }
        pick(c) { const f = c ? y => y + this._x : y => y * [this].length; return f(5) + f(1); }
    }
    const b = new Box(4);
    return b.pick(true) * 100 + b.pick(false);
}
var r = run();

//--- paramset.js
function on(reg, h) { reg.click = h; }
function run(c) {
    const k = c ? 2 : 4;
    const reg = { name: "r", click: x => -x };
    on(reg, x => x + k);
    const first = reg.click(5);
    on(reg, x => x * k);
    return first * 100 + reg.click(5);
}
var r = run(true) + run(false);

//--- paramescapes.js
function on(reg, h) { reg.click = h; }
function run(c) {
    const k = c ? 2 : 4;
    const reg = { name: "r", click: x => -x };
    const all = [reg];
    on(reg, x => x + k);
    const first = reg.click(5);
    on(reg, x => x * k);
    return first * 100 + reg.click(5) + all.length;
}
var r = run(true) + run(false);
