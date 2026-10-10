// RUN: cat %S/Inputs/initialization/*.mlir > %t.sources
// RUN: split-file %t.sources %t
// RUN: python3 %S/initialization.py --translate ctjs-translate --opt ctjs-opt --node %node --reference %native_reference --fixtures %t --work %t.controls

// Source sections live in Inputs/initialization/, in their original order.
