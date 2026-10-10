#pragma once

#include "../Harness.h"
#include <utility>

namespace ctcompile::test::escape::arrays::length_detail {

struct LengthCases {
    mlir::MLIRContext & context;
    explicit LengthCases(mlir::MLIRContext & input) : context(input) {}
    std::string values{};
    std::string read{};
    std::string overwrite{};
    std::string done{};
    unsigned rows{};
    std::size_t budgets{};
    std::string privateValues{};
    contents_row branch{};
    std::string one{};
    std::string subtract{};
    std::string indexed{};
    contents_row originalIndex{};
    std::string shrink{};
    contents_row originalShrink{};
    contents_row computedShrink{};
    contents_row literalShrink{};
    contents_row productShrink{};
    contents_row quotientShrink{};
    contents_row remainderShrink{};
    contents_row shiftShrink{};
    contents_row signedShiftShrink{};
    contents_row leftShiftShrink{};
    contents_row maskShrink{};
    contents_row orShrink{};
    contents_row xorShrink{};
    contents_row heldOffsetShrink{};
    contents_row unaryShrink{};
    contents_row negatedShrink{};
    contents_row mutation{};
    unsigned liveStates{};

    void check(mlir::ModuleOp module, const contents_row & expected, const char * discharged);
    mlir::OwningOpRef<mlir::ModuleOp> parse(const contents_row & expected);
    void run(const contents_row & expected, const char * discharged = "x",
             const char * receiverVerdict = nullptr);

    void setup();
    void indexSnapshots();
    void arithmetic();
    void shrinkArithmetic();
    void shrinkRetention();
    void liveMutations();
    void finish();
};

} // namespace ctcompile::test::escape::arrays::length_detail
