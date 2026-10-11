//===- StaticTables.h - Constant tables of run-once statics -----*- C++ -*-===//
//
// Declares the proof that a static variable of a run-once function
// (ClosureLifting/RunOnce.cpp) holds one object literal of constants that
// nothing writes after it is made, and the attributes the proof writes for type
// inference, admission and the EmitC lowering to read.
//
// Such a table becomes one namespace-scope `const` struct, and the variable a
// pointer to it that is null (undefined) until the run-once function stores
// it. A getter like `static get Default() { return Default; }` returns that
// pointer, so every read sees the one object JavaScript has.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/StringRef.h"

namespace ctcompile::ctnative {

/// On the literal, its array fields, the stores and appends that build it, and
/// each proved read of a field: the static variable's name (a StringAttr, the
/// cells' kNativeStaticCell).
inline constexpr llvm::StringLiteral kNativeStaticTable = "ctnative.static_table";

/// On a proved field read: the constant the field holds (a ctjs attribute), so
/// the read's type is the constant's.
inline constexpr llvm::StringLiteral kNativeStaticTableField = "ctnative.static_table_field";

/// On a proved field read that a spread of the table wrote out (`{...table}`):
/// the table may still be undefined there, which copies nothing, so the read is
/// the field's constant or undefined. A unit attribute.
inline constexpr llvm::StringLiteral kNativeStaticTableSpread = "ctnative.static_table_spread";

/// On every cell of a static whose stored object literal is not a constant
/// table: why not, as a clause that completes "it reads `x`, a run-once static
/// holding an object literal that ...".
inline constexpr llvm::StringLiteral kNativeStaticTableReason = "ctnative.static_table_reason";

/// Proves which run-once statics hold a constant table and writes the
/// attributes above. Runs once per module after closure lifting, before type
/// inference. A table qualifies when:
///  1. Its static has exactly one write in the whole program, in the function
///     that owns it, and that write stores an object literal made in the same
///     block.
///  2. The literal's other uses define its fields, before that write, each with
///     a constant key once. Each value is a number, a boolean, a string, null,
///     or an array literal of numbers only or strings only that is used for
///     nothing else.
///  3. Every read of the static, and every result of a direct call of a
///     function that returns one, is only read through a constant key the
///     literal defines with a number, boolean, string or null, or is returned.
///     A function that returns it may be called directly or through a closure
///     the program cannot follow (a class getter); either way the caller holds
///     a pointer to a `const` object.
///  4. A read may also be the source of an object spread into a fresh literal,
///     when the table has no array field. The spread is then written out as
///     one marked read (kNativeStaticTableSpread) and one own definition per
///     field, in the literal's order. A table that is still undefined copies
///     nothing, and those reads give undefined instead: the target can only
///     hold an undefined field where JavaScript has no field at all, which is
///     unobservable because native lowering gives the target a struct only
///     when its keys are never enumerated, tested or copied again.
void markStaticTables(mlir::ModuleOp module);

} // namespace ctcompile::ctnative
