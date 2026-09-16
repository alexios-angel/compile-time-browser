// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/source.js | ctjs-opt | FileCheck %s
// RUN: ctjs-translate --ctbrowser-module-to-ctjs %t/source.js | ctjs-opt | FileCheck %s --check-prefix=MODULE

// Hoisted declarations are source metadata, including conditional declarations
// and duplicate bare vars. Lexical and nested-function locals stay out.
// CHECK: module attributes {ctjs.hoisted_vars = ["conditional", "saved"]
// CHECK-LABEL: ctjs.func @_script_$0
// CHECK: ctjs.load_global "saved"
// CHECK-LABEL: ctjs.func @nested$
// MODULE-NOT: ctjs.hoisted_vars
// MODULE: ctjs.func @_script_$0
// MODULE-NOT: ctjs.hoisted_vars

//--- source.js
var saved;
var saved;
if (false) { var conditional; }
let lexical = 1;
const fixed = 2;
function nested() { var local; return local; }
var saved = saved;
