// CTJS operators operations; included by CTJSOps.td.
#ifndef CTCOMPILE_CTJS_OPS_OPERATORS_TD
#define CTCOMPILE_CTJS_OPS_OPERATORS_TD

def CTJS_BinaryOp : CTJS_RuntimeOp<"binary", "ct_aot_binary_op", CTJS_GenericEffects> {
  let summary = "Generic binary operator with ToPrimitive semantics";
  let description = [{
    The re-entering family: `+` can invoke valueOf, and every arm can reach a
    user toString. `concat` is here rather than folded into `add` because the
    runtime keeps the three `+` opcodes distinct, and for its reason - concat
    ToStrings both sides unconditionally and must never reach the BigInt arm.
  }];
  // Escape effects (may_reenter 1: concat, add_generic, sub, mul, div, mod,
  // pow, and the six bitwise opcodes, which ToNumeric both operands first):
  // both operands SINK(converted). ToPrimitive runs valueOf/toString with the
  // operand as receiver (context::to_primitive, vm/coerce.cpp). Those methods
  // come from the shared Object.prototype table, which a page can replace, so
  // a confined object is handed to user code here. Anything reachable from
  // globalThis counts as external, which is why this is a sink and not
  // NEITHER.
  let arguments = (ins CTJS_BinaryKind:$kind,
                       Arg<CTJS_ValueType, "", [CTJS_SinkConverted]>:$lhs,
                       Arg<CTJS_ValueType, "", [CTJS_SinkConverted]>:$rhs);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$kind $lhs `,` $rhs attr-dict";
}

def CTJS_BinaryStaticOp
    : CTJS_RuntimeOp<"binary_static", "ct_aot_binary_op_static",
                     [CTJS_MayThrow, CTJS_Safepoint, CTJS_NoEscapeEffects]> {
  let summary = "Binary operator using the STATIC conversions, which cannot re-enter";
  let description = [{
    A SEPARATE OPERATION RATHER THAN A FLAG, because the policy says to split
    on exactly this: these differ from ctjs.binary in their TRAITS. They use
    to_number and to_int32 rather than to_number_value, so no user valueOf or
    toString runs - which lets a backend that has proven both operands are
    Numbers drop the call, the exception edge AND the safepoint.

    The runtime reaches this family from `++` and three internal counters, not
    from source `+`. That is a deviation from the specification which
    aot_helpers.def records as one, and it is the reason `x++` on
    `{valueOf: () => 3}` is NaN here.

    The importer emits the six bitwise kinds as ctjs.binary, because the VM
    ToNumerics their operands. A bitwise ctjs.binary_static means the
    operands are already numeric: the helper converts nothing.
  }];
  // Escape effects: both NEITHER, because these are the static conversions.
  // context::binary_op_static (vm/coerce.cpp) uses bigint_binary, then
  // to_number (no user code) and to_int32, and the bytecode rows in
  // bytecode_opcodes.def say "never runs user code".
  let arguments = (ins CTJS_BinaryKind:$kind, CTJS_ValueType:$lhs, CTJS_ValueType:$rhs);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$kind $lhs `,` $rhs attr-dict";
  // The verifier accepts only the seven kinds the helper implements.
  // context::binary_op_static switches on add, bit_and, bit_or, bit_xor, shl,
  // shr and ushr and answers `undefined` for anything else, deliberately,
  // because the AOT entry is untrusted input. The importer never emits the
  // others here, but hand-written IR could, and an inference that said `f64`
  // for `binary_static sub` would be wrong about a value that is really
  // undefined.
  let hasVerifier = 1;
}

def CTJS_UnaryOp : CTJS_Op<"unary", CTJS_GenericEffects> {
  let summary = "Generic unary operator";
  let description = [{
    NOT a CTJS_RuntimeOp: the six kinds reach four different helpers -
    ct_aot_negate, ct_aot_to_number, ct_aot_bit_not, ct_aot_type_of_name - and
    `void` reaches none at all, since it evaluates its operand and yields
    undefined. One getHelperID() cannot answer for all six, and the interface
    exists precisely so that operations which CAN answer are lowered without
    a pattern of their own. Phase 10 lowers this one by kind.
  }];
  // Escape effects: deliberately no annotation. The role depends on $kind,
  // which a decorator cannot express, so this is one of the three operations
  // in EscapeAnalysis.cpp's kind switch: not/typeof/void NEITHER (total, no
  // conversion); neg/plus/bitnot SINK(converted), because negate and to_number
  // are may_reenter 1 in bytecode_opcodes.def.
  let arguments = (ins CTJS_UnaryKind:$kind, CTJS_ValueType:$operand);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$kind $operand attr-dict";
}

def CTJS_CompareOp : CTJS_Op<"compare", CTJS_GenericEffects> {
  let summary = "Generic comparison";
  let description = [{
    NOT a CTJS_RuntimeOp, for the same reason as ctjs.unary: strict equality
    is ct_aot_strict_equals and cannot throw at all, loose equality is
    ct_aot_loose_equals, and the four relational kinds are ct_aot_compare
    against an ordering. Three helpers, three effect profiles.

    The relational kinds also carry a trap worth stating: ct_aot_compare
    returns an ORDERING, and UNORDERED - either operand NaN - makes all four
    of them false. They cannot be lowered as negations of one another.
  }];
  // Escape effects: deliberately no annotation; the second operation in the
  // kind switch. strict_eq is NEITHER (value::strict_equals in value.hpp is a
  // bit or content compare); eq/lt/le/gt/ge are SINK(converted), because
  // loose_equal and less are may_reenter 1 in bytecode_opcodes.def.
  let arguments = (ins CTJS_CompareKind:$kind, CTJS_ValueType:$lhs, CTJS_ValueType:$rhs);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$kind $lhs `,` $rhs attr-dict";
}

def CTJS_ConvertOp : CTJS_Op<"convert", CTJS_GenericEffects> {
  let summary = "An ECMAScript abstract conversion";
  let description = [{
    Every kind can run user code except to_boolean, which is ct_aot_truthy and
    is total - but the operation carries generic effects anyway rather than
    varying them by kind, because an attribute is not a trait and a pass
    querying traits would otherwise be told the wrong thing about five of the
    six. Phase 10 narrows this by splitting to_boolean out if it measures.
  }];
  // Escape effects: deliberately no annotation; the third operation in the
  // kind switch. to_boolean is NEITHER (total); every other kind is
  // SINK(converted). ToObject is the identity on an object, so it could be a
  // CARRY, but nothing would use that yet, so it is a SINK.
  let arguments = (ins CTJS_ConvertKind:$kind, CTJS_ValueType:$operand);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$kind $operand attr-dict";
}

// Escape effects: NEITHER - total, runtime row (0,0,0), and the result is an i1.
def CTJS_TruthyOp : CTJS_RuntimeOp<"truthy", "ct_aot_truthy", [Pure, CTJS_NoEscapeEffects]> {
  let summary = "ToBoolean, as an i1 - the only bridge from a value to a branch";
  let description = [{
    THE OPERATION THE DIALECT WAS MISSING, and its absence blocked the whole
    importer. The plan's control-flow decision is "use the standard dialects
    wherever they suffice: cf.br, cf.cond_br, and cf.switch cover all bytecode
    branching" - and `cf.cond_br` takes an `i1`. Nothing in CTJS produced one:
    `ctjs.convert to_boolean` yields a `!ctjs.value` holding a boolean, which is
    a JavaScript value, not a branch condition.

    PURE, AND IT IS ONE OF THE VERY FEW THAT QUALIFY. ToBoolean is total: it
    inspects a tag, allocates nothing, calls nothing, and cannot throw - there
    is no `valueOf` path into it, because ToBoolean is defined on every value
    without conversion. The runtime agrees; ct_aot_truthy's row is (0, 0, 0) and
    it takes no frame handle at all.

    It is NOT ctjs.convert to_boolean by another name. That one produces a
    JavaScript boolean a program can store; this produces a machine bit a
    terminator can branch on, and conflating them would mean a `cf.cond_br`
    operand that has to be unboxed by every backend.
  }];
  let arguments = (ins CTJS_ValueType:$value);
  let results = (outs I1:$result);
  let assemblyFormat = "$value attr-dict";
}

def CTJS_InstanceOfOp
    : CTJS_RuntimeOp<"instanceof", "ct_aot_instance_of",
                     CTJS_GenericEffects # [CTJS_NoEscapeEffects]> {
  let summary = "`object instanceof constructor`";
  let description = [{
    AN i1, NOT A !ctjs.value, AND THAT IS A CORRECTION. ct_aot_instance_of
    returns a uint32_t that is 0 or 1 - the .def puts it in the raise tier and
    has it "return its boolean" rather than a status. Declared as a
    !ctjs.value, `x instanceof C` would have produced value::from_bits(1): a
    subnormal double, not value::boolean(true), and every later pass would have
    been right to believe the type.

    Boxing it is the front end's job, one ctjs.constant away, and keeping the
    raw predicate is what lets `if (x instanceof C)` branch without boxing at
    all.

    PHASE 55: both operands NEITHER. instance_of (vm/objects/chain.cpp) walks
    `prototype`
    fields and runs no call(); the bytecode row is may_reenter 0
    (bytecode_opcodes.def:451-452). Its ensure_prototype(ctor) stores a
    fresh object ON THE CONSTRUCTOR (self-reachable only, vm/call/construct.cpp),
    never the $object.
  }];
  let arguments = (ins CTJS_ValueType:$object, CTJS_ValueType:$constructor);
  let results = (outs I1:$result);
  let assemblyFormat = "$object `,` $constructor attr-dict";
}

//===----------------------------------------------------------------------===//

#endif
