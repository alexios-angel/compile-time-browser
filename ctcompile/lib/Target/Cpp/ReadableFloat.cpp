#include "ReadableFloat.h"

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"

#include <charconv>

namespace ctcompile::cpp {

bool emitReadableFloat(const llvm::APFloat & value, llvm::raw_ostream & output) {
    const auto semantics = llvm::APFloatBase::SemanticsToEnum(value.getSemantics());
    const bool single = semantics == llvm::APFloatBase::S_IEEEsingle;
    if (!value.isFinite() || (!single && semantics != llvm::APFloatBase::S_IEEEdouble)) {
        return false;
    }

    // The overload without a format or precision chooses the shortest decimal
    // that round-trips to the original type. Do not widen a float first: that
    // would spell the double's exact view of the float instead of its shortest
    // float literal. This conversion is locale-independent and preserves -0.
    char buffer[64];
    const auto result =
        single ? std::to_chars(buffer, buffer + sizeof(buffer), value.convertToFloat())
               : std::to_chars(buffer, buffer + sizeof(buffer), value.convertToDouble());
    if (result.ec != std::errc{}) { return false; }
    const llvm::StringRef literal(buffer, static_cast<size_t>(result.ptr - buffer));
    output << literal;
    // `auto x = 100` and `auto x = 100.0` have different types. An exponent
    // already makes the token floating point; an integral decimal needs .0.
    if (!literal.contains('.') && !literal.contains('e') && !literal.contains('E')) {
        output << ".0";
    }
    if (single) { output << 'f'; }
    return true;
}

} // namespace ctcompile::cpp
