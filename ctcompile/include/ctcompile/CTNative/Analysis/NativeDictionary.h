//===- NativeDictionary.h - Objects used as tables ---------------*- C++ -*-===//
//
// The proof that an object literal is used only as a string-keyed table, so
// native code can hold it as a ctnative::dictionary<V> (Runtime/Dictionary.hpp)
// rather than a struct, and the attributes prepareNativeDictionaries() writes
// to record it.
//
//===----------------------------------------------------------------------===//
#ifndef CTCOMPILE_CTNATIVE_ANALYSIS_NATIVEDICTIONARY_H
#define CTCOMPILE_CTNATIVE_ANALYSIS_NATIVEDICTIONARY_H

#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/StringRef.h"

namespace ctcompile::ctnative {

/// On a proved `ctjs.create_object`: the literal is a dictionary.
inline constexpr llvm::StringLiteral kNativeDictionarySite = "ctnative.dict_site";
/// On each operation of a proved dictionary: which table operation it is.
/// "get" and "set" are `o[k]` reads and writes, "delete" is `delete o[k]`,
/// "object" is `__ctbrowser_in_object(o)` (the receiver check of `k in o`),
/// "key" is ToPropertyKey of an `in` key, "has" is the `in` test itself, and
/// "plumbing" is a read of one of the intrinsics those calls go through.
inline constexpr llvm::StringLiteral kNativeDictionaryAction = "ctnative.dict_action";
/// On a `ctjs.create_object` with no initial fields that is not a dictionary
/// although some use treats it as one: why.
inline constexpr llvm::StringLiteral kNativeDictionaryReason = "ctnative.dict_reason";

/// Marks every object literal that is used only as a table. A literal
/// qualifies when it is made with no fields and every use of it is
///   - `o[k]` or `o[k] = v`, the object operand, under any key;
///   - `delete o[k]`, as `__ctbrowser_delete`'s object;
///   - `k in o`, as `__ctbrowser_in_object`'s operand, whose result is only
///     the object of `has_property`;
/// and at least one of those uses has a computed key, a delete or an `in`, so
/// a literal that a struct can hold stays a struct (TypeInference::
/// hasClosedShape). A literal with any other use records why in
/// kNativeDictionaryReason, when some use treats it as a table, and is left
/// alone. The value type, the key types and Object.prototype's own keys are
/// admission's to check (Admission/Dictionaries.cpp); this proof is only that
/// nothing but table operations sees the object.
void prepareNativeDictionaries(mlir::ModuleOp module);

/// The table operation `op` performs on a proved dictionary, or empty.
llvm::StringRef nativeDictionaryAction(mlir::Operation * op);

/// Whether `op` makes a proved dictionary.
bool isNativeDictionarySite(mlir::Operation * op);

} // namespace ctcompile::ctnative
#endif
