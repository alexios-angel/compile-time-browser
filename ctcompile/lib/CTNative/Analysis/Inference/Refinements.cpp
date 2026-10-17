//===- Refinements.cpp - Types of values a branch narrowed ----------------===//
//
// TypeInference::refinedType: the type of `ctnative.refine`, the value a
// dominating `typeof` test or truthiness branch proved to be of one kind
// (Lowering/Refinements.cpp inserts the operation).
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTNative/Analysis/TypeInference.h"

namespace ctcompile::ctnative {

namespace {

/// The alternative of `from` that `is` accepts: `from` itself, an optional's
/// element or a variant's alternative; null when there is none.
template <class Kind> mlir::Type alternativeOf(mlir::Type from) {
    if (llvm::isa_and_nonnull<Kind>(from)) { return from; }
    if (auto optional = llvm::dyn_cast_or_null<OptType>(from)) {
        return alternativeOf<Kind>(optional.getElementType());
    }
    if (auto variant = llvm::dyn_cast_or_null<VariantType>(from)) {
        for (mlir::Type alternative : variant.getAlternatives()) {
            if (llvm::isa<Kind>(alternative)) { return alternative; }
        }
    }
    return {};
}

} // namespace

mlir::Type TypeInference::refinedType(mlir::MLIRContext * c, llvm::StringRef kind,
                                      mlir::Type from) {
    // `typeof v == "string"` held, so v is a String whatever was proved before;
    // when nothing was, the String is still a fact, not a guess.
    if (kind == "string") {
        const mlir::Type string = alternativeOf<StrType>(from);
        return string ? string : defaultStringType(c);
    }
    if (kind == "number") {
        const mlir::Type number = alternativeOf<NumType>(from);
        return number ? number : NumType::get(c, NumKind::F64);
    }
    // A truthy value is neither null nor undefined.
    if (kind == "truthy") {
        if (auto optional = llvm::dyn_cast_or_null<OptType>(from);
            optional && !llvm::isa<BottomType>(optional.getElementType())) {
            return optional.getElementType();
        }
    }
    return from;
}

} // namespace ctcompile::ctnative
