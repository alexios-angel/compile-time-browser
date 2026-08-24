// EVERY OPERATION THE BACKEND ACCEPTS, IN ONE FUNCTION, SO THAT THE RESULT CAN
// BE LINKED.
//
// A call to a helper the runtime declares but never defines COMPILES PERFECTLY
// and fails at link - and that is not hypothetical: the backend emitted
// ct_aot_global_get and ct_aot_negate for two commits while every test passed,
// because every EmitC test compiles its output with -fsyntax-only and none of
// them linked it.
//
// So this file exists to be LINKED. It is deliberately not a test of what the
// generated code computes - GCRoots.cpp does that - only of whether the symbols
// it names exist. Anything the backend learns to lower belongs here on the same
// day, or the next unimplemented helper is found by whoever ships first.
function everything(a, b) {
  // Arithmetic: the re-entering family, with its status edge.
  var sum = a + b;
  // The relational operators, which reach ct_aot_compare and an ordering.
  if (a < b) { sum = a - b; }
  if (a >= b) { sum = a * b; }
  // Both equalities: one infallible, one with an edge.
  if (a === b) { sum = a / b; }
  if (a == b) { sum = a % b; }
  // ToBoolean, and the negation that is a comparison against zero.
  if (!a) { sum = b; }
  // NO UNARY PLUS HERE, and the reason is upstream: the IMPORTER has no CTJS
  // operation for op::to_number, so `+sum` never reaches the backend from real
  // JavaScript. ctjs.unary plus IS lowered - operators.mlir exercises it from
  // hand-written IR - and it belongs in this file the day the importer learns
  // the opcode.
  // `void`, which reaches no helper at all.
  void a;
  // And constants of every kind the backend spells.
  if (a === null) { return true; }
  // `undefined` IS A GLOBAL READ in JavaScript. It needed ct_aot_global_get,
  // which had no body - so this was an uninitialised local, and it is spelled
  // properly again now that the row is implemented. It is the link check for
  // both global rows: a write follows.
  var nothing = undefined;
  globalThis = nothing;
  if (b === nothing) { return false; }
  // A property read, whose helper takes an inline cache this backend can only
  // pass as nullptr - ct_aot_ic is forward-declared and nothing can allocate
  // one - and a call, which is the only operation so far that needs a
  // CONTIGUOUS run of frame slots rather than just rooting.
  var got = a[b];
  return got(a, b, sum);
}

// EVERY FUNCTION IN THIS FILE COUNTS, not only the one the driver declares.
// mlir-translate emits the whole module into one translation unit and the test
// binary links all of it, so a helper named by ANY function here is checked.
// That is what lets the operations below sit in shapes of their own rather than
// being crammed into `everything` - a class method cannot go there at all.

// STRINGS, ARRAYS, OBJECTS AND typeof.
//
// ct_aot_new_string memoises per (site, slot); ct_aot_new_array takes a
// reserve HINT and the elements follow as ct_aot_append; an object literal is
// ct_aot_new_object written through ct_aot_set_index; and typeof reaches
// ct_aot_type_of_name, which answers a LENGTH and a static pointer.
function literals(a) {
  var text = "ab\u0001cd";
  var list = [1, a, 3];
  var box = { k: a, j: 2 };
  list[0] = box.k;
  return text + list.length + box.j + typeof a;
}

// UNARY MINUS AND BITWISE NOT, which are two different helpers because one has
// a BigInt arm with no width to truncate to.
function unaries(a) { return -a + ~a; }

// A THROW, WHICH NEVER COMES BACK ok. ct_aot_throw returns CAUGHT, UNWOUND or
// FAILED and never CT_AOT_OK, so the lowering makes it a terminator that
// branches straight to the shared failure path.
function thrower(a) { throw a; }

// CLOSURES, WHICH ARE FIVE HELPERS AT ONCE: ct_aot_cell_new boxes the captured
// binding, ct_aot_make_closure builds the instance, ct_aot_callee is how the
// body reaches its own closure at all, and ct_aot_upvalue_cell plus
// ct_aot_cell_get / ct_aot_cell_set read and write through it.
function counter(start) {
  var n = start;
  return function () { n = n + 1; return n; };
}

// for-of AND SPREAD, which are the same helper reached two ways.
function iterate(xs) {
  var total = 0;
  for (var v of xs) { total = total + v; }
  return total + [...xs].length;
}

// `in`, `instanceof` and `delete` - three helpers with three different shapes:
// a status with a uint32_t out-parameter, a RAISE-tier call that returns its
// boolean, and a status with no out-parameter at all.
function predicates(o, k, C) {
  var there = k in o;
  var kind = o instanceof C;
  delete o[k];
  return there && kind;
}

// `new`, WHOSE `site` IS THE ENTRY'S OWN - the only helper that reads it for
// anything but a memo key.
function Maker(v) { this.v = v; }
function build(n) { return new Maker(n).v; }

// AND new.target, WHICH WAS REFUSED UNTIL BOTH HALVES OF ITS BLOCKER CLOSED.
function guarded() { return new.target === undefined; }

// `super`, WHICH IS FOUR HELPERS AND ONLY WORKS AS A CLASS. ct_aot_home and
// ct_aot_get_proto are the method form; ct_aot_pass_new_target is the
// constructor form; ct_aot_set_proto is what `extends` itself writes.
class LinkBase {
  constructor(v) { this.v = v; }
  tag() { return "base"; }
}
class LinkDerived extends LinkBase {
  constructor(v) { super(v); }
  tag() { return "derived" + super.tag(); }
}
function hierarchy(n) { return new LinkDerived(n).tag(); }
