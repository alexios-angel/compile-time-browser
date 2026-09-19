// RUN: cat %S/Inputs/class-initialization/*.mlir > %t.sources
// RUN: split-file %t.sources %t
// RUN: python3 %S/class_initialization.py --translate ctjs-translate --opt ctjs-opt --node %node --reference %native_reference --fixtures %t --work %t.controls

// Source sections live in Inputs/class-initialization in their original order.
