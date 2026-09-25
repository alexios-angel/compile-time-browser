#include "Cases.hpp"

namespace ctcompile::test::escape::arrays::length_detail {

void LengthCases::liveMutations() {
    liveStates = 0;
    if (auto module = parse(mutation)) {
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        ctjs::GetPropertyOp load;
        ctjs::UnaryOp unary;
        module->walk([&](ctjs::GetPropertyOp op) { load = op; });
        module->walk([&](ctjs::UnaryOp op) { unary = op; });
        const mlir::Value base = load.getObject();
        const mlir::Value key = load.getKey();
        auto literal = key.getDefiningOp<ctjs::ConstantOp>();
        const mlir::Value parameter = function.getBody().front().getArgument(3);
        mlir::OpBuilder builder(load);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*module))) {
            fail(row{.what = mutation.what, .body = mutation.body, .expected = ""},
                 "the length stale solver did not converge");
        }
        const auto inspect = [&](ArrayContentsFailure failure) {
            mutation.failure = failure;
            check(*module, mutation, "x");
            const bool complete = failure == ArrayContentsFailure::None;
            const auto current = computeVerdicts(stale, function);
            if (current.arrayRetentionComplete != complete ||
                current.confinedStoredSites != (complete ? 1U : 0U)) {
                fail(row{.what = mutation.what, .body = mutation.body, .expected = ""},
                     "stale solver or forged marker supplied length authority");
            }
            ++liveStates;
        };
        inspect(ArrayContentsFailure::None);
        load->setOperand(0, parameter);
        inspect(ArrayContentsFailure::UnknownArray);
        load->setOperand(0, base);
        load->setOperand(1, parameter);
        inspect(ArrayContentsFailure::UnknownIndex);
        load->setOperand(1, key);
        literal.setValueAttr(ctjs::StringAttr::get(&context, "0"));
        inspect(ArrayContentsFailure::UnsupportedOperation);
        literal.setValueAttr(ctjs::StringAttr::get(&context, "Length"));
        inspect(ArrayContentsFailure::UnknownIndex);
        literal.setValueAttr(ctjs::StringAttr::get(&context, "length"));
        inspect(ArrayContentsFailure::None);
        unary.setKindAttr(ctjs::UnaryKindAttr::get(&context, static_cast<ctjs::UnaryKind>(99)));
        inspect(ArrayContentsFailure::UnsupportedOperation);
        unary.setKindAttr(ctjs::UnaryKindAttr::get(&context, ctjs::UnaryKind::Neg));
        inspect(ArrayContentsFailure::None);
    } else {
        fail(row{.what = mutation.what, .body = mutation.body, .expected = ""},
             "the live length fixture did not parse");
    }
    if (auto module = parse(originalIndex)) {
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        ctjs::BinaryOp binary;
        ctjs::GetPropertyOp load;
        module->walk([&](ctjs::BinaryOp op) { binary = op; });
        module->walk([&](ctjs::GetPropertyOp op) { load = op; });
        auto literal = binary.getRhs().getDefiningOp<ctjs::ConstantOp>();
        const mlir::Attribute offset = literal.getValue();
        const mlir::Value lhs = binary.getLhs();
        const mlir::Value base = load.getObject();
        mlir::OpBuilder builder(function);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
        binary->setAttr("ctnative.array_index", builder.getI64IntegerAttr(0));
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*module))) {
            fail(row{.what = originalIndex.what, .body = originalIndex.body, .expected = ""},
                 "the subtracted-index stale solver did not converge");
        }
        const auto inspect = [&](ArrayContentsFailure failure) {
            contents_row current = originalIndex;
            current.failure = failure;
            check(*module, current, "x");
            const bool complete = failure == ArrayContentsFailure::None;
            const auto verdicts = computeVerdicts(stale, function);
            if (verdicts.arrayRetentionComplete != complete ||
                verdicts.confinedStoredSites != (complete ? 1U : 0U)) {
                fail(row{.what = current.what, .body = current.body, .expected = ""},
                     "stale solver or forged marker supplied subtracted-index authority");
            }
            ++liveStates;
        };
        inspect(ArrayContentsFailure::None);
        literal.setValueAttr(ctjs::NumberAttr::get(&context, 0));
        inspect(ArrayContentsFailure::MissingElement);
        literal.setValueAttr(ctjs::NumberAttr::get(&context, 4602678819172646912ULL));
        inspect(ArrayContentsFailure::UnknownIndex);
        literal.setValueAttr(ctjs::NumberAttr::get(&context, 9221120237041090560ULL));
        inspect(ArrayContentsFailure::UnknownIndex);
        literal.setValueAttr(ctjs::BigIntAttr::get(&context, "1"));
        inspect(ArrayContentsFailure::UnknownIndex);
        literal.setValueAttr(ctjs::StringAttr::get(&context, "1"));
        inspect(ArrayContentsFailure::None);
        literal.setValueAttr(ctjs::StringAttr::get(&context, "01"));
        inspect(ArrayContentsFailure::None);
        literal.setValueAttr(ctjs::StringAttr::get(&context, "2"));
        inspect(ArrayContentsFailure::UnknownIndex);
        literal.setValueAttr(ctjs::StringAttr::get(&context, "0"));
        inspect(ArrayContentsFailure::MissingElement);
        literal.setValueAttr(ctjs::StringAttr::get(&context, "1"));
        inspect(ArrayContentsFailure::None);
        literal.setValueAttr(offset);
        inspect(ArrayContentsFailure::None);
        binary->setOperand(0, binary.getRhs());
        inspect(ArrayContentsFailure::None);
        literal.setValueAttr(ctjs::StringAttr::get(&context, "1"));
        inspect(ArrayContentsFailure::None);
        literal.setValueAttr(ctjs::StringAttr::get(&context, "01"));
        inspect(ArrayContentsFailure::None);
        literal.setValueAttr(ctjs::NumberAttr::get(&context, 4751297606873776128ULL));
        inspect(ArrayContentsFailure::None);
        literal.setValueAttr(ctjs::NumberAttr::get(&context, 4751297606875873280ULL));
        inspect(ArrayContentsFailure::UnknownIndex);
        literal.setValueAttr(offset);
        binary->setOperand(0, lhs);
        binary.setKindAttr(ctjs::BinaryKindAttr::get(&context, ctjs::BinaryKind::Add));
        inspect(ArrayContentsFailure::MissingElement);
        binary.setKindAttr(ctjs::BinaryKindAttr::get(&context, ctjs::BinaryKind::Sub));
        load->setOperand(0, function.getBody().front().getArgument(3));
        inspect(ArrayContentsFailure::UnknownArray);
        load->setOperand(0, base);
        inspect(ArrayContentsFailure::None);
    } else {
        fail(row{.what = originalIndex.what, .body = originalIndex.body, .expected = ""},
             "the live subtracted-index fixture did not parse");
    }
    for (const contents_row & source :
         {originalShrink, computedShrink, literalShrink, productShrink, quotientShrink,
          remainderShrink, shiftShrink, signedShiftShrink, leftShiftShrink, maskShrink, orShrink,
          xorShrink, heldOffsetShrink, unaryShrink, negatedShrink}) {
        auto module = parse(source);
        if (!module) {
            fail(row{.what = source.what, .body = source.body, .expected = ""},
                 "the live length-write fixture did not parse");
            continue;
        }
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp store;
        module->walk([&](ctjs::SetPropertyOp op) { store = op; });
        const mlir::Value target = store.getObject();
        const mlir::Value key = store.getKey();
        const mlir::Value value = store.getValue();
        auto binary = value.getDefiningOp<ctjs::BinaryOp>();
        const auto binaryKind = binary ? binary.getKindAttr() : ctjs::BinaryKindAttr{};
        auto shift = value.getDefiningOp<ctjs::BinaryStaticOp>();
        const auto shiftKind = shift ? shift.getKindAttr() : ctjs::BinaryKindAttr{};
        auto unary = value.getDefiningOp<ctjs::UnaryOp>();
        const auto unaryKind = unary ? unary.getKindAttr() : ctjs::UnaryKindAttr{};
        auto literal = (shift    ? shift.getRhs()
                        : binary ? binary.getRhs()
                        : unary  ? unary.getOperand()
                                 : value)
                           .getDefiningOp<ctjs::ConstantOp>();
        if (binary) {
            if (auto offset = binary.getRhs().getDefiningOp<ctjs::BinaryOp>()) {
                literal = offset.getLhs().getDefiningOp<ctjs::ConstantOp>();
            }
        }
        const mlir::Attribute original = literal.getValue();
        mlir::OpBuilder builder(function);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
        store->setAttr("ctnative.array_length", builder.getI64IntegerAttr(0));
        if (binary) { binary->setAttr("ctnative.array_length", builder.getI64IntegerAttr(0)); }
        if (shift) { shift->setAttr("ctnative.array_length", builder.getI64IntegerAttr(0)); }
        if (unary) { unary->setAttr("ctnative.array_length", builder.getI64IntegerAttr(0)); }
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*module))) {
            fail(row{.what = source.what, .body = source.body, .expected = ""},
                 "the length-write stale solver did not converge");
        }
        const auto inspect = [&](ArrayContentsFailure failure, bool retained = false) {
            contents_row current = source;
            current.failure = failure;
            if (retained) {
                current.arrays = shift   ? "a:[x]; result:[a,index]"
                                 : unary ? "a:[x]; result:[a,wanted]"
                                         : "a:[x]; result:[a,length]";
                current.exit = "result -> {a,result,x}";
            }
            check(*module, current, retained ? "" : "x");
            const bool complete = failure == ArrayContentsFailure::None;
            const auto verdicts = computeVerdicts(stale, function);
            if (verdicts.arrayRetentionComplete != complete ||
                verdicts.confinedStoredSites != (complete && !retained ? 1U : 0U)) {
                fail(row{.what = current.what, .body = current.body, .expected = ""},
                     "stale solver or forged marker supplied length-write authority");
            }
            ++liveStates;
        };
        inspect(ArrayContentsFailure::None);
        literal.setValueAttr(ctjs::NumberAttr::get(&context, 4611686018427387904ULL));
        inspect((binary && binary.getKind() == ctjs::BinaryKind::Sub) ||
                        (unary && unary.getKind() == ctjs::UnaryKind::Neg)
                    ? ArrayContentsFailure::UnknownIndex
                : (shift && shiftKind.getValue() != ctjs::BinaryKind::BitOr &&
                   shiftKind.getValue() != ctjs::BinaryKind::BitXor) ||
                        (binary && (binary.getKind() == ctjs::BinaryKind::Div ||
                                    binary.getKind() == ctjs::BinaryKind::Mod))
                    ? ArrayContentsFailure::None
                    : ArrayContentsFailure::MissingElement);
        literal.setValueAttr(ctjs::NumberAttr::get(&context, 4602678819172646912ULL));
        inspect(shift ? ArrayContentsFailure::None : ArrayContentsFailure::UnknownIndex);
        literal.setValueAttr(ctjs::NumberAttr::get(&context, 13830554455654793216ULL));
        // A negative Sub offset, including saved Add, proves growth, never holes or release.
        // A nonzero negative divisor keeps these zero results exact.
        // A signed mask or shift count also keeps the original zero result.
        // Negating the original -1 retains the full unit-length array.
        const bool negatedUnit = unary && unary.getKind() == ctjs::UnaryKind::Neg;
        inspect(binary && binary.getKind() == ctjs::BinaryKind::Sub
                    ? ArrayContentsFailure::MissingElement
                : negatedUnit ||
                        (binary && (binary.getKind() == ctjs::BinaryKind::Div ||
                                    binary.getKind() == ctjs::BinaryKind::Mod)) ||
                        (shift && (shiftKind.getValue() == ctjs::BinaryKind::Shl ||
                                   shiftKind.getValue() == ctjs::BinaryKind::Shr ||
                                   shiftKind.getValue() == ctjs::BinaryKind::UShr ||
                                   shiftKind.getValue() == ctjs::BinaryKind::BitAnd))
                    ? ArrayContentsFailure::None
                    : ArrayContentsFailure::UnknownIndex,
                negatedUnit);
        literal.setValueAttr(ctjs::NumberAttr::get(&context, 9221120237041090560ULL));
        inspect(shift ? ArrayContentsFailure::None : ArrayContentsFailure::UnknownIndex);
        literal.setValueAttr(ctjs::StringAttr::get(&context, binary ? "0.5" : "0"));
        inspect(unary || shift ? ArrayContentsFailure::None : ArrayContentsFailure::UnknownIndex);
        if (unary || shift) {
            literal.setValueAttr(ctjs::StringAttr::get(&context, "00"));
            inspect(ArrayContentsFailure::None);
        }
        literal.setValueAttr(ctjs::BigIntAttr::get(&context, "0"));
        inspect(ArrayContentsFailure::UnknownIndex);
        literal.setValueAttr(original);
        inspect(ArrayContentsFailure::None);
        const mlir::Value parameter = function.getBody().front().getArgument(3);
        if (shift) {
            const mlir::Value lhs = shift.getLhs();
            shift->setOperand(0, parameter);
            inspect(ArrayContentsFailure::UnknownValue);
            shift->setOperand(0, lhs);
            shift.setKindAttr(ctjs::BinaryKindAttr::get(&context, ctjs::BinaryKind::Add));
            inspect(ArrayContentsFailure::None,
                    shiftKind.getValue() != ctjs::BinaryKind::BitOr &&
                        shiftKind.getValue() != ctjs::BinaryKind::BitXor);
            shift.setKindAttr(shiftKind);
            inspect(ArrayContentsFailure::None);
            if (shiftKind.getValue() == ctjs::BinaryKind::Shl) {
                auto input = lhs.getDefiningOp<ctjs::ConstantOp>();
                const mlir::Attribute originalInput = input.getValue();
                input.setValueAttr(ctjs::NumberAttr::get(&context, 4607182418800017408ULL));
                inspect(ArrayContentsFailure::MissingElement);
                literal.setValueAttr(ctjs::NumberAttr::get(&context, 4629418941960159232ULL));
                inspect(ArrayContentsFailure::UnknownIndex);
                input.setValueAttr(ctjs::NumberAttr::get(&context, 4611686018427387904ULL));
                inspect(ArrayContentsFailure::None);
                literal.setValueAttr(ctjs::NumberAttr::get(&context, 4629700416936869888ULL));
                inspect(ArrayContentsFailure::MissingElement);
                input.setValueAttr(ctjs::NumberAttr::get(&context, 4746794007248502784ULL));
                inspect(ArrayContentsFailure::UnknownIndex);
                literal.setValueAttr(original);
                inspect(ArrayContentsFailure::None);
                input.setValueAttr(originalInput);
                inspect(ArrayContentsFailure::None);
            }
            if (shiftKind.getValue() == ctjs::BinaryKind::Shr) {
                auto input = lhs.getDefiningOp<ctjs::ConstantOp>();
                const mlir::Attribute originalInput = input.getValue();
                input.setValueAttr(ctjs::NumberAttr::get(&context, 4746794007244308480ULL));
                inspect(ArrayContentsFailure::MissingElement);
                literal.setValueAttr(ctjs::NumberAttr::get(&context, 4629418941960159232ULL));
                inspect(ArrayContentsFailure::None);
                input.setValueAttr(ctjs::NumberAttr::get(&context, 4746794007248502784ULL));
                inspect(ArrayContentsFailure::UnknownIndex);
                input.setValueAttr(ctjs::NumberAttr::get(&context, 4751297606873776128ULL));
                inspect(ArrayContentsFailure::UnknownIndex);
                input.setValueAttr(originalInput);
                literal.setValueAttr(original);
                inspect(ArrayContentsFailure::None);
            }
            if (shiftKind.getValue() == ctjs::BinaryKind::BitAnd) {
                auto input = lhs.getDefiningOp<ctjs::ConstantOp>();
                const mlir::Attribute originalInput = input.getValue();
                input.setValueAttr(ctjs::NumberAttr::get(&context, 4746794007248502784ULL));
                inspect(ArrayContentsFailure::None);
                literal.setValueAttr(ctjs::NumberAttr::get(&context, 4746794007248502784ULL));
                inspect(ArrayContentsFailure::UnknownIndex);
                literal.setValueAttr(ctjs::NumberAttr::get(&context, 4751297606873776128ULL));
                inspect(ArrayContentsFailure::UnknownIndex);
                input.setValueAttr(ctjs::NumberAttr::get(&context, 4751297606873776128ULL));
                literal.setValueAttr(ctjs::NumberAttr::get(&context, 4746794007244308480ULL));
                inspect(ArrayContentsFailure::MissingElement);
                input.setValueAttr(originalInput);
                literal.setValueAttr(original);
                inspect(ArrayContentsFailure::None);
            }
            if (shiftKind.getValue() == ctjs::BinaryKind::BitOr ||
                shiftKind.getValue() == ctjs::BinaryKind::BitXor) {
                const bool isXor = shiftKind.getValue() == ctjs::BinaryKind::BitXor;
                auto input = lhs.getDefiningOp<ctjs::ConstantOp>();
                const mlir::Attribute originalInput = input.getValue();
                input.setValueAttr(ctjs::NumberAttr::get(&context, 4746794007248502784ULL));
                inspect(ArrayContentsFailure::UnknownIndex);
                literal.setValueAttr(ctjs::NumberAttr::get(&context, 4746794007248502784ULL));
                inspect(isXor ? ArrayContentsFailure::None : ArrayContentsFailure::UnknownIndex);
                literal.setValueAttr(ctjs::NumberAttr::get(&context, 4751297606873776128ULL));
                inspect(isXor ? ArrayContentsFailure::MissingElement
                              : ArrayContentsFailure::UnknownIndex);
                input.setValueAttr(ctjs::NumberAttr::get(&context, 4751297606873776128ULL));
                inspect(isXor ? ArrayContentsFailure::None : ArrayContentsFailure::UnknownIndex);
                input.setValueAttr(originalInput);
                literal.setValueAttr(original);
                inspect(ArrayContentsFailure::None);
            }
        }
        if (binary) {
            const mlir::Value lhs = binary.getLhs();
            binary->setOperand(0, parameter);
            inspect(ArrayContentsFailure::UnsupportedOperation);
            binary->setOperand(0, lhs);
            if (binaryKind.getValue() == ctjs::BinaryKind::Div ||
                binaryKind.getValue() == ctjs::BinaryKind::Mod) {
                literal.setValueAttr(ctjs::NumberAttr::get(&context, 0));
                inspect(ArrayContentsFailure::UnknownIndex);
                literal.setValueAttr(ctjs::NumberAttr::get(&context, 9223372036854775808ULL));
                inspect(ArrayContentsFailure::UnknownIndex);
                literal.setValueAttr(original);
                inspect(ArrayContentsFailure::None);
            }
            binary.setKindAttr(ctjs::BinaryKindAttr::get(&context, ctjs::BinaryKind::Mul));
            inspect(ArrayContentsFailure::None, binaryKind.getValue() == ctjs::BinaryKind::Sub);
            binary.setKindAttr(ctjs::BinaryKindAttr::get(&context, ctjs::BinaryKind::Div));
            inspect(binaryKind.getValue() == ctjs::BinaryKind::Mul
                        ? ArrayContentsFailure::UnknownIndex
                        : ArrayContentsFailure::None,
                    binaryKind.getValue() == ctjs::BinaryKind::Sub);
            binary.setKindAttr(ctjs::BinaryKindAttr::get(&context, ctjs::BinaryKind::Mod));
            inspect(binaryKind.getValue() == ctjs::BinaryKind::Mul
                        ? ArrayContentsFailure::UnknownIndex
                        : ArrayContentsFailure::None);
            binary.setKindAttr(binaryKind);
            inspect(ArrayContentsFailure::None);
        }
        if (unary) {
            const mlir::Value operand = unary.getOperand();
            unary->setOperand(0, parameter);
            inspect(ArrayContentsFailure::UnsupportedOperation);
            unary->setOperand(0, operand);
            unary.setKindAttr(ctjs::UnaryKindAttr::get(&context, ctjs::UnaryKind::Not));
            inspect(ArrayContentsFailure::UnknownIndex);
            unary.setKindAttr(unaryKind);
            inspect(ArrayContentsFailure::None);
        }
        store->setOperand(0, parameter);
        inspect(ArrayContentsFailure::UnknownArray);
        store->setOperand(0, target);
        store->setOperand(1, parameter);
        inspect(ArrayContentsFailure::UnknownIndex);
        store->setOperand(1, key);
        store->setOperand(2, parameter);
        inspect(ArrayContentsFailure::UnknownIndex);
        store->setOperand(2, value);
        inspect(ArrayContentsFailure::None);
    }
}

} // namespace ctcompile::test::escape::arrays::length_detail
