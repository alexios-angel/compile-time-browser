// Every operation the backend accepts, in one function, so that the result can
// be linked.
//
// A call to a helper the runtime declares but never defines compiles and
// fails only at link, and the EmitC tests compile their output with
// -fsyntax-only, so none of them would notice. This file is compiled through
// the real pipeline and linked into the ctcompile-test-linkable binary
// (Linkable.cpp), which checks that every helper the backend names exists.
//
// It is deliberately not a test of what the generated code computes -
// GC/Roots.cpp does that - only of whether the symbols it names exist. Add
// each newly lowered operation here, or the next unimplemented helper is
// found only when someone ships it.
function everything(a, b) {
    // Arithmetic: the re-entering family, with its status edge.
    var sum = a + b;
    // The relational operators, which reach ct_aot_compare and an ordering.
    if (a < b) {
        sum = a - b;
    }
    if (a >= b) {
        sum = a * b;
    }
    // Both equalities: one infallible, one with an edge.
    if (a === b) {
        sum = a / b;
    }
    if (a == b) {
        sum = a % b;
    }
    // ToBoolean, and the negation that is a comparison against zero.
    if (!a) {
        sum = b;
    }
    // Unary plus is missing here: `+x` imports as ctjs.unary plus and lowers
    // to a ct_aot_to_number call. Runtime/Differential's `plusOf` already
    // compiles and links that helper, so its absence here leaves no symbol
    // unchecked, but by the rule at the top of this file `+a` belongs in this
    // function too.
    // `void`, which reaches no helper at all.
    void a;
    // And constants of every kind the backend spells.
    if (a === null) {
        return true;
    }
    // `undefined` is a global read, so this links ct_aot_global_get; the
    // assignment to globalThis below links ct_aot_global_set. Together they
    // are the link check for both global helpers, which nothing else in this
    // file reaches.
    var nothing = undefined;
    globalThis = nothing;
    if (b === nothing) {
        return false;
    }
    // A property read, whose helper takes an inline cache this backend can only
    // pass as nullptr - ct_aot_ic is forward-declared and nothing can allocate
    // one - and a call, which is the only operation here that needs a
    // contiguous run of frame slots rather than just rooting.
    var got = a[b];
    return got(a, b, sum);
}

// Every function in this file counts, not only the one the driver declares.
// mlir-translate emits the whole module into one translation unit and the test
// binary links all of it, so a helper named by any function here is checked.
// That is what lets the operations below sit in shapes of their own rather than
// being crammed into `everything` - a class method cannot go there at all.

// Strings, arrays, objects and typeof.
//
// ct_aot_new_string memoises per (site, slot); ct_aot_new_array takes a
// reserve hint and the elements follow as ct_aot_append; an object literal is
// ct_aot_new_object written through ct_aot_set_index; and typeof reaches
// ct_aot_type_of_name, which answers a length and a static pointer.
function literals(a) {
    var text = "ab\u0001cd";
    var list = [1, a, 3];
    var box = {
        k: a,
        j: 2
    };
    list[0] = box.k;
    return text + list.length + box.j + typeof a;
}

// Unary minus and bitwise not, which are two different helpers because one has
// a BigInt arm with no width to truncate to.
function unaries(a) {
    return -a + ~a;
}

// A throw, which never comes back ok. ct_aot_throw returns CAUGHT, UNWOUND or
// FAILED and never CT_AOT_OK, so the lowering makes it a terminator that
// branches straight to the shared failure path.
function thrower(a) {
    throw a;
}

// Closures, which are five helpers at once: ct_aot_cell_new boxes the captured
// binding, ct_aot_make_closure builds the instance, ct_aot_callee is how the
// body reaches its own closure at all, and ct_aot_upvalue_cell plus
// ct_aot_cell_get / ct_aot_cell_set read and write through it.
function counter(start) {
    var n = start;
    return function () {
        n = n + 1;
        return n;
    };
}

// for-of and spread, which are the same helper reached two ways.
function iterate(xs) {
    var total = 0;
    for (var v of xs) {
        total = total + v;
    }
    return total + [...xs].length;
}

// `in`, `instanceof` and `delete` - three helper shapes: a status with a
// uint32_t out-parameter, a call that returns its boolean directly (errors are
// raised and polled later through ct_aot_failed), and a status with no out-parameter.
function predicates(o, k, C) {
    var there = k in o;
    var kind = o instanceof C;
    delete o[k];
    return there && kind;
}

// `new`, whose `site` is the entry's own - the only helper that reads it for
// anything but a memo key.
function Maker(v) {
    this.v = v;
}

function build(n) {
    return new Maker(n).v;
}

// `new.target`, which reaches ct_aot_new_target.
function guarded() {
    return new.target === undefined;
}

// `super`, which is four helpers and only works in a class. ct_aot_home and
// ct_aot_get_proto are the method form; ct_aot_pass_new_target is the
// constructor form; ct_aot_set_proto is what `extends` itself writes.
class LinkBase {
    constructor(v) {
        this.v = v;
    }
    tag() {
        return "base";
    }
}
class LinkDerived extends LinkBase {
    constructor(v) {
        super(v);
    }
    tag() {
        return "derived" + super.tag();
    }
}

function hierarchy(n) {
    return new LinkDerived(n).tag();
}

// An async function's `return`, which is ct_aot_wrap_promise and nothing else.
//
// The whole of async that does not suspend. A body with no `await` carries
// op::wrap_promise on every return path - the explicit one and the implicit
// one at the end - and no await_value at all, so it compiles like any other
// function. One with an `await` is refused by the importer with a reason of
// its own; see BytecodeImport.cpp.
async function settles(a) {
    return a + 1;
}
