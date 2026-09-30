#pragma once

#include "../Source/Proof.hpp"

#include <optional>

// EXACT PRIMITIVE FOLDING for normalizeDOMCustomIteration's private helper
// projection: constant operands only, so no conversion hook can run. String
// bytes are charged to `work` before decoding; a charge that does not fit
// drains it without a refusal. Every function answers null or nullopt when
// its operation is not an exact primitive fold.
namespace ctcompile::ctnative::dom_iteration_detail {

// A constant i1, or the truthiness of a constant primitive.
std::optional<bool> exactBoolean(mlir::Value condition);
mlir::Attribute exactUnary(ctjs::UnaryOp unary, dom_source_detail::DOMSource & work);
std::optional<bool> exactComparison(ctjs::CompareOp comparison,
                                    dom_source_detail::DOMSource & work);
// ctjs.binary or ctjs.binary_static over two constants.
mlir::Attribute exactArithmetic(mlir::Operation * binary, dom_source_detail::DOMSource & work);

} // namespace ctcompile::ctnative::dom_iteration_detail
