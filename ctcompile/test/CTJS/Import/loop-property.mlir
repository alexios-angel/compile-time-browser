// A loop and a property read, which is where the register model earns itself.
//
// Every non-entry block takes the whole register file as arguments and every
// branch passes it. That is not SSA construction (no dominance frontiers, no
// phi minimisation, no backpatching): blocks are created with their
// full argument list before anything is emitted, so a back edge's operands are
// known when it is written.

// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null | ctjs-opt | FileCheck %s

function total(items) {
  var sum = 0;
  for (var i = 0; i < items.length; i = i + 1) {
    sum = sum + items[i];
  }
  return sum;
}

// CHECK-LABEL: ctjs.func @total$1(

// The entry block branches into the loop header. Bytecode runs off the end of
// one instruction into the next; an MLIR block does not. Without that branch
// the header would be reachable only from its own back edge, and the function
// would verify, print plausibly and return undefined.
// CHECK: cf.br ^[[HEADER:.*]](

// CHECK: ^[[HEADER]]({{.*}}):
// CHECK-SAME: 2 preds
// CHECK: ctjs.constant #ctjs.string<"length">
// CHECK: ctjs.get_property
// CHECK: ctjs.compare lt
// The only bridge from a value to a branch: cf.cond_br takes an i1, and
// ctjs.convert to_boolean would produce a JavaScript boolean instead.
// CHECK: %[[BIT:.*]] = ctjs.truthy
// CHECK: cf.cond_br %[[BIT]]

// The body: an indexed read, the accumulate, the increment, and the back edge.
// CHECK: ctjs.get_property
// CHECK: ctjs.binary add
// CHECK: ctjs.binary add
// CHECK: cf.br ^[[HEADER]](
