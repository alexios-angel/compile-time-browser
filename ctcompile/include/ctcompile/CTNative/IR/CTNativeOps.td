//===- CTNativeOps.td - the ctnative dialect's TableGen entry point -------===//
//
// THERE ARE NO OPERATIONS IN THIS FILE AND THAT IS THE PHASE, not an omission.
// Part 24's Stage 53A is "types, every one a TypeDef", and part 24 §A.1 says
// what the emptiness buys: "53 (types) unblocks everything ... Do not start 55,
// 56, 57, 59 or 60 before 53 lands - they would each invent a type lattice."
// Operations belong to those phases, and shipping a speculative one here would
// be this phase inventing part of theirs.
//
// SO WHY THE FILE EXISTS. add_mlir_dialect(CTNativeOps ctnative) is the single
// TableGen invocation that generates the dialect, its operations AND its
// typedefs, and it runs over the file it is named for. CTJSOps.td says the same
// thing about itself - "IT INCLUDES THE TYPES, and that is load-bearing rather
// than tidy". Splitting the typedef generation out into its own
// mlir_tablegen call would be a second wiring pattern in a project that has
// one.
//
// The op decls and defs it emits are empty files. That costs nothing and keeps
// the day an operation arrives a one-line change here rather than a CMake one.
//
//===----------------------------------------------------------------------===//
#ifndef CTCOMPILE_CTNATIVE_OPS
#define CTCOMPILE_CTNATIVE_OPS

include "ctcompile/CTNative/IR/CTNativeBase.td"
include "ctcompile/CTNative/IR/CTNativeTypes.td"
include "ctcompile/CTNative/IR/CTNativeEnums.td"
include "ctcompile/CTNative/IR/CTNativeInterfaces.td"

#endif // CTCOMPILE_CTNATIVE_OPS
