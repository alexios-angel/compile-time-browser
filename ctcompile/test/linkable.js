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
  // `undefined` IS A GLOBAL READ in JavaScript, so spelling it here would need
  // ct_aot_global_get - which is one of the 37 rows with no body. An
  // uninitialised local is the same value and reaches no helper.
  var nothing;
  if (b === nothing) { return false; }
  // A property read, whose helper takes an inline cache this backend can only
  // pass as nullptr - ct_aot_ic is forward-declared and nothing can allocate
  // one - and a call, which is the only operation so far that needs a
  // CONTIGUOUS run of frame slots rather than just rooting.
  var got = a[b];
  return got(a, b, sum);
}
