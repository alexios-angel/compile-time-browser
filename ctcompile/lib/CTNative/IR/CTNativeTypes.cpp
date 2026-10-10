//===- CTNativeTypes.cpp - ctnative dialect types and their verifiers -----===//
//
// Instantiates the TableGen-generated ctnative types (storage, parsers,
// printers), registers them with the dialect, and defines the two hand-written
// type verifiers, TupleType::verify and VariantType::verify.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTNative/IR/CTNativeTypes.h"

#include "ctcompile/CTNative/IR/CTNativeDialect.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/DialectImplementation.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/TypeSwitch.h"

// The generated enums first: some types take an enum parameter, and
// EnumParameter's parser and printer name symbolizeNumKind / stringifyNumKind
// by their unqualified spelling inside this namespace.
#include "ctcompile/CTNative/IR/CTNativeEnums.cpp.inc"

// Then the storage, parsers and printers. Nothing hand-written: the dialect
// sets useDefaultTypePrinterParser and every parameterized type carries an
// assemblyFormat.
#define GET_TYPEDEF_CLASSES
#include "ctcompile/CTNative/IR/CTNativeOpsTypes.cpp.inc"

namespace ctcompile::ctnative {

// Here rather than in CTNativeDialect.cpp because addTypes<> needs the storage
// classes above to be complete. Same split, same reason, as CTJSTypes.cpp.
void CTNativeDialect::registerTypes() {
    addTypes<
#define GET_TYPEDEF_LIST
#include "ctcompile/CTNative/IR/CTNativeOpsTypes.cpp.inc"
        >();
}

/// A tuple has at least two positions, each a type: a one-element array literal
/// is an ordinary array, and a null position would print as nothing.
/// ctcompile/test/CTNative/IR/tuple-form.mlir checks the arity clause; hand-written
/// IR cannot spell a null position.
::mlir::LogicalResult TupleType::verify(
    ::llvm::function_ref<::mlir::InFlightDiagnostic()> emitError,
    ::llvm::ArrayRef<::mlir::Type> elements) {
    if (elements.size() < 2) {
        return emitError() << "a tuple needs at least two elements; a one-element array "
                              "literal is an ordinary array";
    }
    if (::llvm::is_contained(elements, ::mlir::Type{})) {
        return emitError() << "a tuple element may not be null";
    }
    return ::mlir::success();
}

/// The variant verifier. It enforces a canonical form rather than a
/// well-formedness rule.
///
/// Every clause below rejects a type that is a second spelling of one that
/// already exists: `variant` nests, `bottom` and `opt` lift out of a union,
/// `json` and `boxed` absorb it. A lattice with two spellings for one element
/// does not have a working `==`, and `meet` compares types with `==` in three
/// places: the fast path, the dedupe, and the caller's fixpoint check. So the
/// meet's termination depends on this invariant.
///
/// It fires on hand-written IR, which is where a wrong variant can come from:
/// `meet` constructs only canonical ones. ctcompile/test/CTNative/IR/variant-form.mlir
/// has one `expected-error` per clause.
::mlir::LogicalResult VariantType::verify(
    ::llvm::function_ref<::mlir::InFlightDiagnostic()> emitError,
    ::llvm::ArrayRef<::mlir::Type> alternatives) {
    if (alternatives.size() < 2) {
        return emitError() << "a variant needs at least two alternatives; a one-way "
                              "choice is the alternative itself";
    }
    ::llvm::SmallPtrSet<::mlir::Type, 4> seen;
    for (::mlir::Type alternative : alternatives) {
        if (!alternative) { return emitError() << "a variant alternative may not be null"; }
        if (::llvm::isa<VariantType>(alternative)) {
            return emitError() << "a variant alternative may not itself be a variant; "
                                  "nested unions flatten";
        }
        if (::llvm::isa<BottomType>(alternative)) {
            return emitError() << "a variant alternative may not be `bottom`; it contributes "
                                  "no values and drops out of the union";
        }
        if (::llvm::isa<OptType>(alternative)) {
            return emitError() << "a variant alternative may not be `opt`; nullability is "
                                  "hoisted outside the variant, as `opt<variant<...>>`";
        }
        if (::llvm::isa<JsonType>(alternative) || ::llvm::isa<BoxedType>(alternative)) {
            return emitError() << "a variant alternative may not be `json` or `boxed`; "
                                  "both absorb the whole union";
        }
        if (!seen.insert(alternative).second) {
            return emitError() << "a variant may not repeat an alternative: " << alternative;
        }
    }
    return ::mlir::success();
}

} // namespace ctcompile::ctnative
