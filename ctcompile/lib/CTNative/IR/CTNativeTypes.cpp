#include "ctcompile/CTNative/IR/CTNativeTypes.h"

#include "ctcompile/CTNative/IR/CTNativeDialect.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/DialectImplementation.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/TypeSwitch.h"

// The generated enums first: four of the fifteen types take one as a parameter,
// and EnumParameter's parser and printer name symbolizeNumKind /
// stringifyNumKind by their unqualified spelling inside this namespace.
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

// THE ONLY TYPE WITH A VERIFIER, AND IT ENFORCES A CANONICAL FORM RATHER THAN
// A WELL-FORMEDNESS RULE.
//
// Every clause below rejects a type that is a SECOND SPELLING of one that
// already exists: `variant` nests, `bottom` and `opt` lift out of a union,
// `json` and `boxed` absorb it. A lattice with two spellings for one element
// does not have a working `==`, and `meet` compares types with `==` in three
// places - the fast path, the dedupe, and the caller's fixpoint check. So this
// is not tidiness; it is the invariant the meet's termination rests on.
//
// It fires on hand-written IR, which is where a wrong variant can come from:
// `meet` constructs only canonical ones. ctcompile/test/CTNative/variant-form.mlir
// is one `expected-error` per clause.
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
