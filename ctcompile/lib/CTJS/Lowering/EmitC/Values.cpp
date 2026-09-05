// Boxed EmitC values lowering.
#include "Literals.h"
#include "Lowering.h"

namespace ctcompile::ctjs::emitc_detail {

bool lowering::convertValues(mlir::Operation & op, mlir::OpBuilder & build,
                             mlir::IRMapping & mapping, compiled_entry & scope) {
    const mlir::Location where = op.getLoc();
    const mlir::Type value = scope.value;

    // The frame is established by the entry block, not by this operation.
    if (mlir::isa<FrameEnterOp>(op)) { return true; }

    // Admission requires the return immediately after this marker. Release
    // there, once constructor normalization has produced the value in flight.
    if (mlir::isa<FrameExitOp>(op)) { return true; }

    if (auto constant = mlir::dyn_cast<ConstantOp>(op)) {
        // A STRING LITERAL IS A CALL, not a spelling. It allocates, so it
        // needs the frame - and it is MEMOISED by (site, slot), which the
        // row insists is part of the ABI rather than an optimisation:
        // without it a literal in a loop allocates once per iteration and
        // reaches the process-lifetime allocation ceiling on a program the
        // interpreter runs forever.
        //
        // `site` IS THE ENTRY'S OWN, which is the function_proto the
        // interpreter keys the same cache by - so a literal shared between
        // a compiled body and an interpreted one is one object, not two.
        if (auto text = mlir::dyn_cast<StringAttr>(constant.getValue())) {
            const llvm::StringRef bytes = text.getValue();
            mapping.map(constant.getResult(),
                        ec::CallOpaqueOp::create(
                            build, where, mlir::TypeRange{value}, callee("ct_aot_new_string"),
                            mlir::ValueRange{
                                scope.frame, scope.memo_site,
                                literal(build, where, opaque(build.getContext(), "uint32_t"),
                                        std::to_string(scope.memo_slots.lookup(&op))),
                                literal(build, where, pointer_to(build.getContext(), "const char"),
                                        c_string_literal(bytes)),
                                literal(build, where, opaque(build.getContext(), "uint32_t"),
                                        std::to_string(bytes.size()))})
                            .getResult(0));
            return true;
        }
        // A BIGINT LITERAL IS THE SOURCE TEXT, PARSED AT RUN TIME, and
        // that is deliberate: bigint_from_literal owns `0x1fn`, `0b..n` and
        // the 1.5n-to-0n substitution, and parsing here would be a second
        // implementation of all three.
        //
        // MEMOISED UNDER scope.memo_site, NOT scope.entry_site, for the
        // reason ct_aot_new_string is: the entry's site IS the
        // function_proto, the interpreter keys the same cache by that proto
        // with the CONSTANT-POOL index as the slot, and this backend numbers
        // its slots in walk order. Sharing the key lets a compiled body read
        // a slot the interpreter filled with a different literal.
        if (auto digits = mlir::dyn_cast<BigIntAttr>(constant.getValue())) {
            const llvm::StringRef text = digits.getText();
            mapping.map(constant.getResult(),
                        ec::CallOpaqueOp::create(
                            build, where, mlir::TypeRange{value},
                            callee("ct_aot_new_bigint_literal"),
                            mlir::ValueRange{
                                scope.frame, scope.memo_site,
                                literal(build, where, opaque(build.getContext(), "uint32_t"),
                                        std::to_string(scope.memo_slots.lookup(&op))),
                                literal(build, where, pointer_to(build.getContext(), "const char"),
                                        c_string_literal(text)),
                                literal(build, where, opaque(build.getContext(), "uint32_t"),
                                        std::to_string(text.size()))})
                            .getResult(0));
            return true;
        }
        mapping.map(constant.getResult(), constant_value(build, where, value, constant));
        return true;
    }

    // ToBoolean, AND THE COMPARISON IS PART OF IT. ct_aot_truthy answers
    // with a uint32_t that is 0 or 1 - the ABI has no bool - while
    // ctjs.truthy's result is an i1, because that is what cf.cond_br takes.
    // Testing it against zero is the conversion, and doing it here rather
    // than trusting C++'s implicit narrowing keeps the emitted code saying
    // what it means.
    if (auto truthy = mlir::dyn_cast<TruthyOp>(op)) {
        const auto u32 = opaque(build.getContext(), "uint32_t");
        auto answered =
            ec::CallOpaqueOp::create(build, where, mlir::TypeRange{u32}, callee("ct_aot_truthy"),
                                     mlir::ValueRange{mapping.lookup(truthy.getValue())});
        auto bit = ec::CmpOp::create(build, where, mlir::IntegerType::get(build.getContext(), 1),
                                     ec::CmpPredicate::ne, answered.getResult(0),
                                     literal(build, where, u32, "0"));
        mapping.map(truthy.getResult(), bit.getResult());
        return true;
    }

    // THE TWO BINARY FAMILIES, WHICH ARE TWO HELPERS AND TWO OPCODE
    // TABLES. `ctjs.binary add` is source `+` and must reach
    // op::add_generic, which runs ToPrimitive and can call a user valueOf;
    // `ctjs.binary_static add` is op::add and cannot run user code at all.
    // Folding them would make `x + y` and `x++` the same call, which is
    // what OpcodeMapping.hpp exists to prevent - and the opcode is spelled
    // as an ENUMERATOR, so the renumbering Phases 13 and 14 perform becomes
    // a build error in the generated code rather than a different operator.
    if (auto binary = mlir::dyn_cast<BinaryOp>(op)) {
        const mlir::Value kind = literal(build, where, opaque(build.getContext(), "uint32_t"),
                                         opcode_spelling(opcode_for_binary(binary.getKind())));
        mapping.map(binary.getResult(),
                    status_call(scope, build, where, callee("ct_aot_binary_op"),
                                {scope.frame, kind, mapping.lookup(binary.getLhs()),
                                 mapping.lookup(binary.getRhs())},
                                scope.value));
        return true;
    }
    if (auto binary = mlir::dyn_cast<BinaryStaticOp>(op)) {
        const mlir::Value kind =
            literal(build, where, opaque(build.getContext(), "uint32_t"),
                    opcode_spelling(opcode_for_binary_static(binary.getKind())));
        mapping.map(binary.getResult(),
                    status_call(scope, build, where, callee("ct_aot_binary_op_static"),
                                {scope.frame, kind, mapping.lookup(binary.getLhs()),
                                 mapping.lookup(binary.getRhs())},
                                scope.value));
        return true;
    }

    // THE UNARY OPERATORS, WHICH REACH FOUR DIFFERENT HELPERS AND ONE
    // NONE AT ALL - which is why ctjs.unary is not a CTJS_RuntimeOp and
    // why this switches on the kind.
    if (auto unary = mlir::dyn_cast<UnaryOp>(op)) {
        const mlir::Value operand = mapping.lookup(unary.getOperand());
        switch (unary.getKind()) {
        case UnaryKind::Neg:
            mapping.map(unary.getResult(), status_call(scope, build, where, callee("ct_aot_negate"),
                                                       {scope.frame, operand}, value));
            return true;
        case UnaryKind::BitNot:
            mapping.map(unary.getResult(),
                        status_call(scope, build, where, callee("ct_aot_bit_not"),
                                    {scope.frame, operand}, value));
            return true;
        case UnaryKind::Plus: {
            // `+x` IS ToNumber, AND ITS OUT-PARAMETER IS A double, not a
            // value - so the result is boxed rather than used directly.
            const mlir::Value number =
                status_call(scope, build, where, callee("ct_aot_to_number"), {scope.frame, operand},
                            opaque(build.getContext(), "double"));
            mapping.map(unary.getResult(), box(build, where, value, "ctc_box_number", number));
            return true;
        }
        case UnaryKind::Not: {
            // `!x` IS ToBoolean NEGATED, and ToBoolean cannot fail - the
            // row is (0, 0, 0) and takes no frame - so there is no status
            // and no edge. Testing the uint32_t against zero IS the
            // negation.
            const auto u32 = opaque(build.getContext(), "uint32_t");
            auto answered =
                ec::CallOpaqueOp::create(build, where, mlir::TypeRange{u32},
                                         callee("ct_aot_truthy"), mlir::ValueRange{operand});
            auto negated = ec::CmpOp::create(
                build, where, mlir::IntegerType::get(build.getContext(), 1), ec::CmpPredicate::eq,
                answered.getResult(0), literal(build, where, u32, "0"));
            mapping.map(unary.getResult(),
                        box(build, where, value, "ctc_box_bool", negated.getResult()));
            return true;
        }
        case UnaryKind::Void:
            // `void x` EVALUATES ITS OPERAND AND YIELDS undefined. The
            // operand is already evaluated - it is an SSA value - so there
            // is nothing to emit but the answer.
            mapping.map(unary.getResult(), undefined(build, where, value));
            return true;
        case UnaryKind::TypeOf: {
            // TWO CALLS, and the second is the point. ct_aot_type_of_name
            // is INFALLIBLE and answers with a LENGTH plus a pointer to
            // STATIC storage - "the return slot carries a LENGTH, and under
            // the return-type rule an unsigned return is data, never a
            // status" - so there is no edge. What it needs is a string.
            //
            // THE SITE IS nullptr, MEANING DO NOT MEMOISE, and
            // ct_aot_new_string's row names this exact case: it "lets
            // ct_aot_type_of_name's companion allocation stay at parity",
            // because VM_CASE(type_of) has no cache at all. Memoising here
            // would allocate FEWER times than the interpreter - a
            // divergence in the same raise tier the memo exists to protect.
            const auto u32 = opaque(build.getContext(), "uint32_t");
            const auto text_ptr = pointer_to(build.getContext(), "const char");
            auto slot = ec::VariableOp::create(build, where, ec::LValueType::get(text_ptr),
                                               ec::OpaqueAttr::get(build.getContext(), ""));
            auto address = ec::AddressOfOp::create(build, where, ec::PointerType::get(text_ptr),
                                                   slot.getResult());
            auto len = ec::CallOpaqueOp::create(build, where, mlir::TypeRange{u32},
                                                callee("ct_aot_type_of_name"),
                                                mlir::ValueRange{operand, address.getResult()});
            auto text = ec::LoadOp::create(build, where, text_ptr, slot.getResult());
            mapping.map(
                unary.getResult(),
                ec::CallOpaqueOp::create(
                    build, where, mlir::TypeRange{value}, callee("ct_aot_new_string"),
                    mlir::ValueRange{scope.frame,
                                     literal(build, where,
                                             pointer_to(build.getContext(), "const ctbrowser::aot::"
                                                                            "ct_aot_site"),
                                             "nullptr"),
                                     literal(build, where, u32, "0"), text.getResult(),
                                     len.getResult(0)})
                    .getResult(0));
            return true;
        }
        }
        llvm_unreachable("body_is_supported admitted a unary kind convert cannot emit");
    }

    // THE COMPARISONS, WHICH ARE THREE HELPERS AND THREE EFFECT PROFILES.
    if (auto compare = mlir::dyn_cast<CompareOp>(op)) {
        const mlir::Value lhs = mapping.lookup(compare.getLhs());
        const mlir::Value rhs = mapping.lookup(compare.getRhs());
        const auto u32 = opaque(build.getContext(), "uint32_t");
        const auto i32 = opaque(build.getContext(), "int32_t");
        const auto bit = mlir::IntegerType::get(build.getContext(), 1);

        if (compare.getKind() == CompareKind::StrictEq) {
            // STRICT EQUALITY CANNOT THROW AND TAKES NO FRAME: its row is
            // (0, 0, 0) and it answers with a uint32_t directly, so there
            // is no out-parameter, no status and no exception edge.
            auto answered = ec::CallOpaqueOp::create(build, where, mlir::TypeRange{u32},
                                                     callee("ct_aot_strict_equals"),
                                                     mlir::ValueRange{lhs, rhs});
            auto truth = ec::CmpOp::create(build, where, bit, ec::CmpPredicate::ne,
                                           answered.getResult(0), literal(build, where, u32, "0"));
            mapping.map(compare.getResult(),
                        box(build, where, value, "ctc_box_bool", truth.getResult()));
            return true;
        }
        if (compare.getKind() == CompareKind::Eq) {
            // LOOSE EQUALITY CAN, because it converts - and its
            // out-parameter is a uint32_t boolean, not a value.
            const mlir::Value answered = status_call(
                scope, build, where, callee("ct_aot_loose_equals"), {scope.frame, lhs, rhs}, u32);
            auto truth = ec::CmpOp::create(build, where, bit, ec::CmpPredicate::ne, answered,
                                           literal(build, where, u32, "0"));
            mapping.map(compare.getResult(),
                        box(build, where, value, "ctc_box_bool", truth.getResult()));
            return true;
        }

        // THE FOUR RELATIONAL KINDS SHARE ONE HELPER AND AN ORDERING, and
        // they are NOT negations of one another. ct_aot_compare answers
        // with less/equivalent/greater/UNORDERED, and unordered - a NaN on
        // either side - makes all four false, including `>=`. Lowering `>=`
        // as `!(<)` would make `NaN >= NaN` true.
        //
        // THE ORDERING'S NUMBERS ARE CONTRACTUAL, unlike the status enum's -
        // aot.hpp says so in as many words - but they are still spelled as
        // enumerators, because a name that is checked costs nothing.
        const mlir::Value ordering = status_call(scope, build, where, callee("ct_aot_compare"),
                                                 {scope.frame, lhs, rhs}, i32);
        const auto is = [&](llvm::StringRef named) {
            return ec::CmpOp::create(
                       build, where, bit, ec::CmpPredicate::eq, ordering,
                       literal(
                           build, where, i32,
                           ("static_cast<int32_t>(ctbrowser::aot::ct_aot_ordering::" + named + ")")
                               .str()))
                .getResult();
        };
        mlir::Value truth;
        switch (compare.getKind()) {
        case CompareKind::Lt: truth = is("less"); break;
        case CompareKind::Gt: truth = is("greater"); break;
        case CompareKind::Le:
            truth = ec::LogicalOrOp::create(build, where, bit, is("less"), is("equivalent"))
                        .getResult();
            break;
        case CompareKind::Ge:
            truth = ec::LogicalOrOp::create(build, where, bit, is("greater"), is("equivalent"))
                        .getResult();
            break;
        default: llvm_unreachable("the two equality kinds are handled above");
        }
        mapping.map(compare.getResult(), box(build, where, value, "ctc_box_bool", truth));
        return true;
    }

    // THE GLOBALS, WHICH ARE INFALLIBLE AND SO HAVE NO EDGE AT ALL.
    // Both rows are (0, 0, 0): reading an undeclared global does NOT throw
    // a ReferenceError here - the row says the absence is load-bearing -
    // and neither reads nor writes can collect. So each is one call.
    //
    // THE NAME IS BYTES AND A LENGTH, not a NUL-terminated string, which is
    // why the length is emitted rather than left to strlen: a global whose
    // name contains a zero byte is legal JavaScript and strlen would stop
    // at it.
    if (auto global = mlir::dyn_cast<LoadGlobalOp>(op)) {
        const llvm::StringRef name = global.getName();
        mapping.map(
            global.getResult(),
            ec::CallOpaqueOp::create(
                build, where, mlir::TypeRange{value}, callee("ct_aot_global_get"),
                mlir::ValueRange{scope.frame,
                                 literal(build, where, pointer_to(build.getContext(), "const char"),
                                         c_string_literal(name)),
                                 literal(build, where, opaque(build.getContext(), "uint32_t"),
                                         std::to_string(name.size()))})
                .getResult(0));
        return true;
    }
    if (auto global = mlir::dyn_cast<StoreGlobalOp>(op)) {
        const llvm::StringRef name = global.getName();
        ec::CallOpaqueOp::create(
            build, where, mlir::TypeRange{}, callee("ct_aot_global_set"),
            mlir::ValueRange{scope.frame,
                             literal(build, where, pointer_to(build.getContext(), "const char"),
                                     c_string_literal(name)),
                             literal(build, where, opaque(build.getContext(), "uint32_t"),
                                     std::to_string(name.size())),
                             mapping.lookup(global.getValue())});
        return true;
    }

    // A PROPERTY READ. Its helper's inline-cache parameter is nullptr and
    // has to be: ct_aot_ic is FORWARD-DECLARED ONLY, so nothing can
    // allocate one, and the implementation says the parameter is "taken and
    // ignored, because the signature is the thing two code generators are
    // written against and a parameter added later is a break". Phase 26
    // attaches real storage; until then this is not a shortcut but the only
    // spelling available.
    if (auto get = mlir::dyn_cast<GetPropertyOp>(op)) {
        const mlir::Value no_cache = literal(
            build, where, pointer_to(build.getContext(), "ctbrowser::aot::ct_aot_ic"), "nullptr");
        mapping.map(get.getResult(), status_call(scope, build, where, callee("ct_aot_get_index"),
                                                 {scope.frame, mapping.lookup(get.getObject()),
                                                  mapping.lookup(get.getKey()), no_cache},
                                                 value));
        return true;
    }

    return false;
}

} // namespace ctcompile::ctjs::emitc_detail
