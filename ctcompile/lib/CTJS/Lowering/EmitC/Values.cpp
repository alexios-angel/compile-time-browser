//===- Values.cpp - Boxed EmitC lowering of value operations ----*- C++ -*-===//
//
// Lowers value operations to EmitC: constants, ToBoolean, unary, binary and
// comparison operators, global reads and writes, and property reads. Each
// becomes a call to a ct_aot_* runtime helper declared in aot.hpp.
//
//===----------------------------------------------------------------------===//
#include "Lowering.h"
#include "ctcompile/CTJS/Import/EngineGlobals.h"

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
        // A string literal is a call, not a spelling. It allocates, so it
        // needs the frame, and it is memoised by (site, slot), which
        // ct_aot_new_string's entry in aot/helpers/values-and-properties.def
        // documents as part of the ABI rather than an optimisation: without it a literal in a loop
        // allocates once per iteration and reaches the process-lifetime
        // allocation ceiling on a program the interpreter runs forever.
        //
        // A BigInt literal is passed as source text and parsed at run time on
        // purpose: bigint_from_literal owns `0x1fn`, `0b..n` and the
        // 1.5n-to-0n substitution, and parsing here would be a second
        // implementation of all three.
        //
        // Both are memoised under scope.memo_site, not scope.entry_site. The
        // entry's site is the function_proto, which the interpreter keys the
        // same cache by with the constant-pool index as the slot, while this
        // backend numbers its slots in walk order. Sharing the key would let a
        // compiled body read a slot the interpreter filled with a different
        // literal.
        auto text = mlir::dyn_cast<StringAttr>(constant.getValue());
        auto digits = mlir::dyn_cast<BigIntAttr>(constant.getValue());
        if (text || digits) {
            const mlir::Value slot = u32_literal(build, where, scope.memo_slots.lookup(&op));
            const auto [bytes, length] =
                bytes_and_length(build, where, text ? text.getValue() : digits.getText());
            mapping.map(constant.getResult(),
                        opaque_call(build, where, value,
                                    text ? "ct_aot_new_string" : "ct_aot_new_bigint_literal",
                                    {scope.frame, scope.memo_site, slot, bytes, length}));
            return true;
        }
        mapping.map(constant.getResult(), constant_value(build, where, value, constant));
        return true;
    }

    // ToBoolean. ct_aot_truthy answers with a uint32_t that is 0 or 1 (the
    // ABI has no bool), while ctjs.truthy's result is an i1 because that is
    // what cf.cond_br takes. The explicit test against zero is the
    // conversion, rather than C++'s implicit narrowing.
    if (auto truthy = mlir::dyn_cast<TruthyOp>(op)) {
        const mlir::Value answered =
            opaque_call(build, where, opaque(build.getContext(), "uint32_t"), "ct_aot_truthy",
                        {mapping.lookup(truthy.getValue())});
        mapping.map(truthy.getResult(), nonzero(build, where, answered));
        return true;
    }

    // The two binary families use two helpers and two opcode tables.
    // `ctjs.binary add` is source `+` and must reach op::add_generic, which
    // runs ToPrimitive and can call a user valueOf; `ctjs.binary_static add`
    // is op::add and cannot run user code at all. Folding them would make
    // `x + y` and `x++` the same call, which is what OpcodeMapping.hpp exists
    // to prevent. The opcode is emitted as an enumerator name rather than a
    // number, so if the VM's opcode enum is renumbered the generated code
    // still names the right operator, and a removed opcode is a build error.
    if (mlir::isa<BinaryOp, BinaryStaticOp>(op)) {
        auto binary = mlir::dyn_cast<BinaryOp>(op);
        const mlir::Value kind =
            literal(build, where, opaque(build.getContext(), "uint32_t"),
                    opcode_spelling(binary ? opcode_for_binary(binary.getKind())
                                           : opcode_for_binary_static(
                                                 mlir::cast<BinaryStaticOp>(op).getKind())));
        mapping.map(op.getResult(0),
                    status_call(scope, build, where,
                                callee(binary ? "ct_aot_binary_op" : "ct_aot_binary_op_static"),
                                {scope.frame, kind, mapping.lookup(op.getOperand(0)),
                                 mapping.lookup(op.getOperand(1))},
                                scope.value));
        return true;
    }

    // The unary operators reach four different helpers, and `void` reaches
    // none, which is why ctjs.unary is not a CTJS_RuntimeOp and this switches
    // on the kind.
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
            // `+x` is ToNumber, whose out-parameter is a double rather than a
            // value, so the result is boxed.
            const mlir::Value number =
                status_call(scope, build, where, callee("ct_aot_to_number"), {scope.frame, operand},
                            opaque(build.getContext(), "double"));
            mapping.map(unary.getResult(), box(build, where, value, shim::box_number, number));
            return true;
        }
        case UnaryKind::Not: {
            // `!x` is ToBoolean negated, and ToBoolean cannot fail
            // (ct_aot_truthy has may_throw, may_reenter and is_safepoint all
            // 0, and takes no frame), so there is no status and no exception
            // edge. Testing the uint32_t for zero is the
            // negation.
            const mlir::Value answered = opaque_call(
                build, where, opaque(build.getContext(), "uint32_t"), "ct_aot_truthy", {operand});
            mapping.map(unary.getResult(),
                        box(build, where, value, shim::box_bool,
                            compare_zero(build, where, ec::CmpPredicate::eq, answered)));
            return true;
        }
        case UnaryKind::Void:
            // `void x` evaluates its operand and yields undefined. The operand
            // is already an SSA value, so only the result is emitted.
            mapping.map(unary.getResult(), undefined(build, where, value));
            return true;
        case UnaryKind::TypeOf: {
            // Two calls. ct_aot_type_of_name is infallible and answers with a
            // length plus a pointer to static storage ("the return slot
            // carries a LENGTH, and under the return-type rule an unsigned
            // return is data, never a status"), so there is no exception
            // edge. ct_aot_new_string then makes the string.
            //
            // The site is nullptr, meaning do not memoise: ct_aot_new_string's
            // entry in aot/helpers/values-and-properties.def names this case, because the
            // interpreter's VM_CASE(type_of) has no cache. Memoising here would
            // allocate fewer times than the interpreter, which is exactly the
            // allocation-count divergence the memo exists to prevent.
            const auto u32 = opaque(build.getContext(), "uint32_t");
            const auto text_ptr = pointer_to(build.getContext(), "const char");
            auto slot = ec::VariableOp::create(build, where, ec::LValueType::get(text_ptr),
                                               ec::OpaqueAttr::get(build.getContext(), ""));
            auto address = ec::AddressOfOp::create(build, where, ec::PointerType::get(text_ptr),
                                                   slot.getResult());
            const mlir::Value len = opaque_call(build, where, u32, "ct_aot_type_of_name",
                                                {operand, address.getResult()});
            auto text = ec::LoadOp::create(build, where, text_ptr, slot.getResult());
            mapping.map(unary.getResult(),
                        opaque_call(build, where, value, "ct_aot_new_string",
                                    {scope.frame,
                                     literal(build, where,
                                             pointer_to(build.getContext(),
                                                        "const ctbrowser::aot::ct_aot_site"),
                                             "nullptr"),
                                     u32_literal(build, where, 0), text.getResult(), len}));
            return true;
        }
        }
        llvm_unreachable("body_is_supported admitted a unary kind convert cannot emit");
    }

    // Comparisons use three helpers with three different effect profiles.
    if (auto compare = mlir::dyn_cast<CompareOp>(op)) {
        const mlir::Value lhs = mapping.lookup(compare.getLhs());
        const mlir::Value rhs = mapping.lookup(compare.getRhs());
        const auto u32 = opaque(build.getContext(), "uint32_t");
        const auto i32 = opaque(build.getContext(), "int32_t");
        const auto bit = mlir::IntegerType::get(build.getContext(), 1);

        if (compare.getKind() == CompareKind::StrictEq) {
            // Strict equality cannot throw and takes no frame (may_throw,
            // may_reenter and is_safepoint are all 0), and it answers with a
            // uint32_t directly, so there is no out-parameter, no status and
            // no exception edge.
            const mlir::Value answered =
                opaque_call(build, where, u32, "ct_aot_strict_equals", {lhs, rhs});
            mapping.map(compare.getResult(),
                        box(build, where, value, shim::box_bool, nonzero(build, where, answered)));
            return true;
        }
        if (compare.getKind() == CompareKind::Eq) {
            // Loose equality can throw, because it converts operands. Its
            // out-parameter is a uint32_t boolean, not a value.
            const mlir::Value answered = status_call(
                scope, build, where, callee("ct_aot_loose_equals"), {scope.frame, lhs, rhs}, u32);
            mapping.map(compare.getResult(),
                        box(build, where, value, shim::box_bool, nonzero(build, where, answered)));
            return true;
        }

        // The four relational kinds share one helper and an ordering, and
        // they are not negations of one another. ct_aot_compare answers with
        // less, equivalent, greater or unordered, and unordered (a NaN on
        // either side) makes all four false, including `>=`. Lowering `>=` as
        // `!(<)` would make `NaN >= NaN` true.
        //
        // The ordering's numeric values are contractual (aot.hpp says so),
        // unlike the status enum's, but they are still emitted as enumerator
        // names because a checked name costs nothing.
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
        mapping.map(compare.getResult(), box(build, where, value, shim::box_bool, truth));
        return true;
    }

    // Globals. Only the source's typeof IdentifierReference may answer
    // undefined for an unresolvable name. A saved ordinary read forwarded
    // into typeof still throws, so the opcode-derived mode decides the helper.
    // Both forms can run a throwing global getter. The soft helper returns
    // its value directly, so classify its status before using that value.
    // Names are passed as bytes and a length (see bytes_and_length()).
    if (auto global = mlir::dyn_cast<LoadGlobalOp>(op)) {
        // An engine helper is read by its VM name (EngineGlobals.h); a
        // script's own global of the same IR name keeps it.
        const llvm::StringRef helper = global->hasAttr(kEngineReservedUse)
                                           ? llvm::StringRef{}
                                           : engineHelperVMName(global.getName());
        const auto [text, length] =
            bytes_and_length(build, where, helper.empty() ? global.getName() : helper);
        if (global.getTypeofLookup()) {
            const mlir::Value loaded = opaque_call(build, where, value, "ct_aot_global_get_soft",
                                                   {scope.frame, text, length});
            status_call_void(scope, build, where, callee("ct_aot_check"), {scope.frame});
            mapping.map(global.getResult(), loaded);
            return true;
        }
        mapping.map(global.getResult(),
                    status_call(scope, build, where, callee("ct_aot_global_get"),
                                {scope.frame, text, length}, value));
        return true;
    }
    if (auto global = mlir::dyn_cast<StoreGlobalOp>(op)) {
        const auto [text, length] = bytes_and_length(build, where, global.getName());
        const bool initialize = global->hasAttr("ctjs.initialize");
        opaque_call(build, where, {}, initialize ? "ct_aot_global_init" : "ct_aot_global_set",
                    {scope.frame, text, length, mapping.lookup(global.getValue())});
        if (!initialize) {
            status_call_void(scope, build, where, callee("ct_aot_check"), {scope.frame});
        }
        return true;
    }

    // A property read. Its helper's inline-cache parameter must be nullptr:
    // ct_aot_ic is only forward-declared, so nothing can allocate one, and the
    // implementation says the parameter is "taken and ignored, because the
    // signature is the thing two code generators are written against and a
    // parameter added later is a break". Until the inline cache gets real
    // storage, nullptr is the only value that can be passed.
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
