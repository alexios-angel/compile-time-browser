// A LOOP AND A PROPERTY READ, which is where the register model earns itself.
//
// Every non-entry block takes the whole register file as arguments and every
// branch passes it. That is not SSA construction - no dominance frontiers, no
// phi minimisation, no backpatching - because blocks are created with their
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

// CHECK-LABEL: ctjs.func @total(

// THE ENTRY BLOCK BRANCHES INTO THE LOOP HEADER. Bytecode runs off the end of
// one instruction into the next; an MLIR block does not. Without that branch
// the header was reachable only from its own back edge, and the function
// verified, printed plausibly and returned undefined.
// CHECK: cf.br ^[[HEADER:.*]](

// CHECK: ^[[HEADER]]({{.*}}):
// CHECK-SAME: 2 preds
// CHECK: ctjs.constant #ctjs.string<"length">
// CHECK: ctjs.get_property
// CHECK: ctjs.compare lt
// THE ONLY BRIDGE FROM A VALUE TO A BRANCH: cf.cond_br takes an i1, and
// ctjs.convert to_boolean would produce a JavaScript boolean instead.
// CHECK: %[[BIT:.*]] = ctjs.truthy
// CHECK: cf.cond_br %[[BIT]]

// The body: an indexed read, the accumulate, the increment, and the back edge.
// CHECK: ctjs.get_property
// CHECK: ctjs.binary add
// CHECK: ctjs.binary add
// CHECK: cf.br ^[[HEADER]](
