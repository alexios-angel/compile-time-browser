//===- ExactPrimitives.h - Exact constant folding ---------------*- C++ -*-===//
//
// Constant folding for normalizeDOMCustomIteration's helper expansion. Only
// constant operands fold, so no conversion hook can run. String bytes are
// charged to `work` before decoding; a charge that does not fit drains the
// budget without a refusal. Every function returns null or nullopt when its
// operation does not fold exactly.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../Source/Proof.hpp"

#include <optional>

namespace ctcompile::ctnative::dom_iteration_detail {

/// A constant i1, or the truthiness of a constant primitive.
std::optional<bool> exactBoolean(mlir::Value condition);
/// Folds !, typeof, void, +, - and ~ of a constant primitive.
mlir::Attribute exactUnary(ctjs::UnaryOp unary, dom_source_detail::DOMSource & work);
/// Folds a comparison (equality or relational) of two constants.
std::optional<bool> exactComparison(ctjs::CompareOp comparison,
                                    dom_source_detail::DOMSource & work);
/// Folds ctjs.binary or ctjs.binary_static over two constants.
mlir::Attribute exactArithmetic(mlir::Operation * binary, dom_source_detail::DOMSource & work);

} // namespace ctcompile::ctnative::dom_iteration_detail
