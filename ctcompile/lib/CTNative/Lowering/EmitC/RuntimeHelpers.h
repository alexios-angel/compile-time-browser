#pragma once

#include "llvm/ADT/StringRef.h"

namespace ctcompile::ctnative::lowering_detail {

// THE DENSE-ARRAY HELPERS - part 24 Phase 57A, emitted ONLY when the unit has
// a vector site, because a preamble emitted unconditionally moves every byte
// count the printing gate reports and every line the other lits pin.
//
// THREE OF THEM AND NO MORE. `push` and `size` are what the plan's rule names;
// `at` is the one that has to exist rather than being `v[i]`, because
// `a[7]` on a three-element array is `undefined` in JavaScript and undefined
// behaviour in C++, and undefined is this tier's NaN. Every out-of-range,
// fractional or negative index therefore answers NaN, which is EXACTLY what
// the element type says it may be - the join starts from `undefined` for this
// reason (TypeInference::elementTypeOf).
//
// EACH ONE UNDER ITS OWN PROVENANCE COMMENT, which is Phase 63 Step 7's rule
// for a generated definition, and `inline` so no translation unit that
// includes none of them warns about one.
constexpr llvm::StringLiteral kVectorHelpers =
    "// ctcompile: the dense-array helpers - part 24 Phase 57A\n"
    "namespace ctnative {\n"
    "// ctcompile: `a[i]`, whose out-of-range answer is undefined, which is NaN "
    "here\n"
    "inline nullable_scalar vec_at(const std::vector<double> & v, nullable_scalar key) {\n"
    "  if (key.tag != nullable_scalar::kind::number) { return {}; }\n"
    "  double i = std::trunc(key.value);\n"
    "  if (!(i >= 0.0) || i >= static_cast<double>(v.size())) {\n"
    "    return {};\n"
    "  }\n"
    "  return v[static_cast<std::vector<double>::size_type>(i)];\n"
    "}\n"
    "// ctcompile: `a.length`, which is `size()` exactly - the site proof is "
    "what rules out a hole\n"
    "inline double vec_length(const std::vector<double> & v) {\n"
    "  return static_cast<double>(v.size());\n"
    "}\n"
    "// ctcompile: one element of an array literal, in source order\n"
    "inline void vec_push(std::vector<double> & v, double x) {\n"
    "  v.push_back(x);\n"
    "}\n"
    "} // namespace ctnative";

} // namespace ctcompile::ctnative::lowering_detail
