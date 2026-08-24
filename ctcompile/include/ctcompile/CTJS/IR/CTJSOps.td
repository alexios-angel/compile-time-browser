//===- CTJSOps.td - every CTJS operation ----------------------------------===//
//
// THE INVENTORY, FOLDED. The plan's original list of ctjs.* names is
// "reorganized to fold variants into kind-carrying operations, per the enum
// rule in the policy. The result is roughly half as many ODS records with
// identical expressive power."
//
// MOST OF THESE ARE CTJS_RuntimeOp, which names a real ctbrowser::aot::helper_id
// enumerator - so an operation cannot claim a helper the runtime does not
// declare, and the check is a C++ compile error rather than a lowering that
// calls the wrong thing.
//
// IT INCLUDES THE TYPES, and that is load-bearing rather than tidy:
// add_mlir_dialect(CTJSOps ctjs) is the only TableGen invocation the policy's
// CMakeLists makes for typedefs, and it runs over THIS file.
//
//===----------------------------------------------------------------------===//
#ifndef CTCOMPILE_CTJS_OPS
#define CTCOMPILE_CTJS_OPS

include "mlir/Interfaces/ControlFlowInterfaces.td"
include "ctcompile/CTJS/IR/CTJSBase.td"
include "ctcompile/CTJS/IR/CTJSTypes.td"
include "ctcompile/CTJS/IR/CTJSAttrs.td"
include "ctcompile/CTJS/IR/CTJSEnums.td"

//===----------------------------------------------------------------------===//
// Constants and bindings
//===----------------------------------------------------------------------===//

def CTJS_ConstantOp : CTJS_Op<"constant", [Pure, ConstantLike]> {
  let summary = "Materialize a statically known JavaScript value";
  let description = [{
    PURE, AND ONE OF THE VERY FEW. The policy: mark an operation Pure "only
    when it cannot allocate, cannot throw, cannot call user code, and cannot
    observe or mutate runtime state. In practice that is a very short list."
    A constant qualifies because its value is an attribute - nothing is
    computed, nothing is allocated, and materializing it twice is
    indistinguishable from materializing it once.

    Its attribute must be one of this dialect's constant attributes rather
    than an arbitrary builtin one, which is what the verifier checks: a
    builtin FloatAttr would lose the difference between -0.0 and 0.0.
  }];
  let arguments = (ins AnyAttr:$value);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$value attr-dict";
  let hasVerifier = 1;
}

def CTJS_LoadGlobalOp : CTJS_RuntimeOp<"load_global", "ct_aot_global_get", CTJS_GenericEffects> {
  let summary = "Read a global binding by name";
  let description = [{
    Generic effects because a global may be an accessor on the global object,
    and reading one runs a getter.
  }];
  let arguments = (ins StrAttr:$name);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$name attr-dict";
}

def CTJS_StoreGlobalOp : CTJS_RuntimeOp<"store_global", "ct_aot_global_set", CTJS_GenericEffects> {
  let summary = "Write a global binding by name";
  let arguments = (ins StrAttr:$name, CTJS_ValueType:$value);
  let assemblyFormat = "$name `,` $value attr-dict";
}

def CTJS_LoadUpvalueOp : CTJS_Op<"load_upvalue"> {
  let summary = "Read a captured binding out of its cell";
  let description = [{
    NOT a CTJS_RuntimeOp, and that is a correction. It named ct_aot_cell_get,
    whose only parameter is `(uint64_t cell)` - so the $index went nowhere.
    The lowering matched on arity, passed the closure where the CELL belongs,
    and dropped the index in silence; ct_aot_cell_get "no-ops on a non-cell",
    so every read of a captured variable would have compiled to `undefined`
    rather than to a failure anybody could see.

    IT IS TWO CALLS, which is why no single helper can be named here - the
    same reason ctjs.unary and ctjs.compare are not CTJS_RuntimeOps.
    aot_helpers.def:568-586 spells the decomposition out: get_upvalue is
    ct_aot_upvalue_cell(fr, index), which reads call_frame::closure from the
    FRAME rather than from any operand, and then ct_aot_cell_get on what that
    returns. "Pairing it with ct_aot_cell_get/ct_aot_cell_set reproduces the
    fused opcodes."

    NO GENERIC EFFECTS, which is unusual here and is justified: a cell is a
    box this compiler created, reading one cannot run user code, cannot
    throw, and cannot collect. Both runtime rows agree - ct_aot_upvalue_cell
    and ct_aot_cell_get are (0, 0, 0).
  }];
  // THE CLOSURE IS A !ctjs.value, not a !ctjs.closure, and the change is the
  // importer's doing: the bytecode's get_upvalue reads the CURRENT frame's
  // closure, which arrives as an ordinary function argument - and ctjs.func
  // takes only !ctjs.value parameters. A closure is a value at run time; the
  // distinct type is for the one place that BUILDS one.
  let arguments = (ins CTJS_ValueType:$closure, I32Attr:$index);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$closure `[` $index `]` attr-dict";
}

def CTJS_StoreUpvalueOp : CTJS_Op<"store_upvalue"> {
  let summary = "Write a captured binding into its cell";
  let description = [{
    NOT a CTJS_RuntimeOp, for the same reason as ctjs.load_upvalue and with
    the same consequence reversed: it named ct_aot_cell_set, the index went
    nowhere, and the write landed on whatever the closure value was - which
    cell_set no-ops on. A captured variable would never have been updated.
  }];
  let arguments = (ins CTJS_ValueType:$closure, I32Attr:$index, CTJS_ValueType:$value);
  let assemblyFormat = "$closure `[` $index `]` `,` $value attr-dict";
}

def CTJS_CreateClosureOp
    : CTJS_RuntimeOp<"create_closure", "ct_aot_make_closure", [CTJS_MayThrow, CTJS_Safepoint]> {
  let summary = "Build a closure over a function and its captured cells";
  let description = [{
    Allocating, so a safepoint and a throw edge for the allocation ceiling -
    but NOT re-entering: building a closure runs none of the function's own
    code.

    ITS FIRST OPERAND WAS A !ctjs.program AND THAT WAS WRONG. Nothing produces
    one - which is why the importer could not emit this operation at all and
    skipped every `closure` opcode, and with it every function that declares
    another. ct_aot_make_closure takes the ENCLOSING CLOSURE as a value,
    because the program is selected from `enclosing->owner`: a context can be
    running functions from more than one program, and a function index means
    nothing outside the one it was compiled in.

    $upvalues IS INDEXED IN PARALLEL WITH THE TARGET'S DESCRIPTORS, not packed.
    Only the entries the compiler marked from_parent_local are read; the helper
    fills the rest from the enclosing closure. A packed list is the plausible
    wrong reading and captures the wrong bindings silently.

    AND $enclosing_this IS THE EFFECTIVE RECEIVER, used only when the target is
    an arrow - because an arrow's `this` is decided where it is WRITTEN, not
    where it is called. Reading the effective one rather than the raw receiver
    is what makes an arrow inside an arrow inside a method still see the
    method's object.

    THE RESULT IS A !ctjs.value, NOT A !ctjs.closure, and that is the second
    half of the same correction. The importer's register file is entirely
    !ctjs.value - because at run time a closure IS one, as ctjs.load_upvalue's
    note already says - so a !ctjs.closure result could not be stored in the
    register it is assigned to. The distinct type was "for the one place that
    BUILDS one", and that place cannot use it either: it leaves !ctjs.closure
    and !ctjs.program with no producer AND no consumer.
  }];
  // THE VARIADIC IS LAST, and it has to be: a Variadic followed by a fixed
  // operand cannot be resolved without AttrSizedOperandSegments, and the
  // verifier reports it as a type error on operand #0 rather than as the
  // ordering problem it is. The ABI's own argument order is different again -
  // (fr, closure, index, upvalues, count, this) - and the lowering maps
  // between them, which is what a lowering is for.
  let arguments = (ins CTJS_ValueType:$enclosing_closure, CTJS_ValueType:$enclosing_this,
                       I32Attr:$function, Variadic<CTJS_ValueType>:$upvalues);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$enclosing_closure `[` $function `]` `this` $enclosing_this"
                       " (`captures` $upvalues^)? attr-dict";
}

//===----------------------------------------------------------------------===//
// Property access
//===----------------------------------------------------------------------===//

def CTJS_GetPropertyOp : CTJS_RuntimeOp<"get_property", "ct_aot_get_index", CTJS_GenericEffects> {
  let summary = "Generic property read with full ECMAScript semantics";
  let description = [{
    Performs the complete `GetV` / `[[Get]]` sequence: prototype walk,
    accessor invocation, proxy traps, exotic object behavior. This operation
    must never be lowered to a direct slot load without a guard proving the
    receiver's shape and the absence of accessors.

    ITS HELPER IS ct_aot_get_index, NOT ct_aot_get_prop, and the difference is
    the KEY. The plan folds indexed and named access into one operation -
    "ctjs.get_index and ctjs.get_property have identical semantics at the
    generic level" - and the ABI does not: ct_aot_get_prop takes a
    `const ct_aot_name *`, an INTERNED HANDLE produced by ct_aot_intern_name,
    while ct_aot_get_index takes a `uint64_t` key. This operation's key is a
    value, so the value-taking helper is the one it means. A named access is a
    constant string key, which is exactly what the importer emits.

    Pointing it at ct_aot_get_prop was wrong and would have passed a value
    where an interned name pointer belongs. Using the name-taking helper - and
    the inline cache Phase 26 attaches to it - needs a SECOND operation carrying
    a name attribute, which is a specialisation rather than a fold.
  }];
  let arguments = (ins CTJS_ValueType:$object, CTJS_ValueType:$key);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$object `[` $key `]` attr-dict";
}

def CTJS_SetPropertyOp : CTJS_RuntimeOp<"set_property", "ct_aot_set_index", CTJS_GenericEffects> {
  let summary = "Generic property write with full ECMAScript semantics";
  let arguments = (ins CTJS_ValueType:$object, CTJS_ValueType:$key, CTJS_ValueType:$value);
  let assemblyFormat = "$object `[` $key `]` `,` $value attr-dict";
}

def CTJS_DeletePropertyOp
    : CTJS_RuntimeOp<"delete_property", "ct_aot_delete_index", CTJS_GenericEffects> {
  let summary = "`delete obj[key]`";
  let description = [{
    NO RESULT, WHICH IS A CORRECTION. This declared a !ctjs.value result
    against ct_aot_delete_index, whose int32_t is a ct_aot_status - so
    `delete o[k]` would have evaluated to the STATUS, and `ok` is 3.

    The helper has no out-parameter and the .def says why: "No out-parameter -
    same separate load-true as delete_prop". The bytecode performs the delete
    and then loads `true` as a separate step, because ECMAScript's `delete`
    yields true in every case this runtime can reach. A front end that wants
    the value materialises it; the call does not carry one.
  }];
  let arguments = (ins CTJS_ValueType:$object, CTJS_ValueType:$key);
  let assemblyFormat = "$object `[` $key `]` attr-dict";
}

def CTJS_HasPropertyOp
    : CTJS_RuntimeOp<"has_property", "ct_aot_has_property", CTJS_GenericEffects> {
  let summary = "`key in obj`";
  let arguments = (ins CTJS_ValueType:$object, CTJS_ValueType:$key);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$key `in` $object attr-dict";
}

//===----------------------------------------------------------------------===//
// Operators
//===----------------------------------------------------------------------===//

def CTJS_FromBoolOp : CTJS_Op<"from_bool", [Pure]> {
  let summary = "A raw i1 as a JavaScript boolean";
  let description = [{
    THE OTHER HALF OF ctjs.truthy, and what ctjs.instanceof's own description
    means by "boxing it is the front end's job". That operation answers an i1 on
    purpose - `value::from_bits(1)` is a subnormal double, not
    `value::boolean(true)` - so something has to put the bit back in a register,
    and a register holds a !ctjs.value.

    PURE, because a boolean is an immediate: nothing is allocated, nothing can
    throw, and materializing it twice is indistinguishable from once. It is also
    what lets `if (x instanceof C)` stay unboxed - the boxing op is simply not
    emitted on that path.
  }];
  let arguments = (ins I1:$bit);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$bit attr-dict";
}

def CTJS_IterableOp
    : CTJS_RuntimeOp<"iterable", "ct_aot_iterable_values", CTJS_GenericEffects> {
  let summary = "The values of an iterable, as an array";
  let description = [{
    Every `for (x of it)` and every spread compiles to this, so a corpus without
    it compiles very little: 36 of Bootstrap's functions were skipped for this
    one opcode alone, more than twice any other.

    IT IS NOT THE ITERATOR PROTOCOL. There is no `Symbol.iterator` dispatch in
    this runtime - `context::iterable_values` answers with a plain array and the
    loop that follows indexes it, which is why a for-of needs no other new
    operation. Two consequences a backend must not optimise away: the ARRAY ARM
    RETURNS THE SAME ARRAY rather than a copy, so `for (x of arr)` aliases and a
    hoisted length read would change behaviour; and a generator is DRAINED
    eagerly under a bound rather than iterated lazily, so an infinite one hangs.
    Both are the interpreter's semantics, reproduced rather than repaired.
  }];
  let arguments = (ins CTJS_ValueType:$source);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "`of` $source attr-dict";
}

def CTJS_BinaryOp : CTJS_RuntimeOp<"binary", "ct_aot_binary_op", CTJS_GenericEffects> {
  let summary = "Generic binary operator with ToPrimitive semantics";
  let description = [{
    The re-entering family: `+` can invoke valueOf, and every arm can reach a
    user toString. `concat` is here rather than folded into `add` because the
    runtime keeps the three `+` opcodes distinct, and for its reason - concat
    ToStrings both sides unconditionally and must never reach the BigInt arm.
  }];
  let arguments = (ins CTJS_BinaryKind:$kind, CTJS_ValueType:$lhs, CTJS_ValueType:$rhs);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$kind $lhs `,` $rhs attr-dict";
}

def CTJS_BinaryStaticOp
    : CTJS_RuntimeOp<"binary_static", "ct_aot_binary_op_static", [CTJS_MayThrow, CTJS_Safepoint]> {
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
  }];
  let arguments = (ins CTJS_BinaryKind:$kind, CTJS_ValueType:$lhs, CTJS_ValueType:$rhs);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$kind $lhs `,` $rhs attr-dict";
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
  let arguments = (ins CTJS_ConvertKind:$kind, CTJS_ValueType:$operand);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$kind $operand attr-dict";
}

def CTJS_TruthyOp : CTJS_RuntimeOp<"truthy", "ct_aot_truthy", [Pure]> {
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
    : CTJS_RuntimeOp<"instanceof", "ct_aot_instance_of", CTJS_GenericEffects> {
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
  }];
  let arguments = (ins CTJS_ValueType:$object, CTJS_ValueType:$constructor);
  let results = (outs I1:$result);
  let assemblyFormat = "$object `,` $constructor attr-dict";
}

//===----------------------------------------------------------------------===//
// Allocation
//===----------------------------------------------------------------------===//

def CTJS_CreateCellOp
    : CTJS_RuntimeOp<"create_cell", "ct_aot_cell_new", [CTJS_MayThrow, CTJS_Safepoint]> {
  let summary = "Box a value so a closure can share it";
  let description = [{
    A cell is a box this compiler creates, not anything a page can see. It is
    what makes a captured `var` shared rather than copied: two closures over the
    same binding hold the same cell, so one writing it is the other reading it.
  }];
  let arguments = (ins CTJS_ValueType:$initial);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$initial attr-dict";
}

def CTJS_CellGetOp : CTJS_RuntimeOp<"cell_get", "ct_aot_cell_get"> {
  let summary = "Read a cell's slot";
  let description = [{
    No effects at all, and the runtime row agrees - ct_aot_cell_get is
    (0, 0, 0). Reading a box cannot run user code, throw, or collect.
  }];
  let arguments = (ins CTJS_ValueType:$cell);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$cell attr-dict";
}

def CTJS_CellSetOp : CTJS_RuntimeOp<"cell_set", "ct_aot_cell_set"> {
  let summary = "Write a cell's slot";
  let arguments = (ins CTJS_ValueType:$cell, CTJS_ValueType:$value);
  let assemblyFormat = "$cell `,` $value attr-dict";
}

def CTJS_CreateObjectOp
    : CTJS_RuntimeOp<"create_object", "ct_aot_new_object", [CTJS_MayThrow, CTJS_Safepoint]> {
  let summary = "A fresh empty object";
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "attr-dict";
}

def CTJS_CreateArrayOp
    : CTJS_RuntimeOp<"create_array", "ct_aot_new_array", [CTJS_MayThrow, CTJS_Safepoint]> {
  let summary = "A fresh array, optionally with its elements";
  let arguments = (ins Variadic<CTJS_ValueType>:$elements);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "`[` ($elements^)? `]` attr-dict";
}

def CTJS_AppendOp : CTJS_RuntimeOp<"append", "ct_aot_append", [CTJS_MayThrow, CTJS_Safepoint]> {
  let summary = "Push one element onto an array literal under construction";
  let description = [{
    An array literal is `new_array` followed by one `append` per element, which
    is how the bytecode builds one - so importing `[a, b]` without this means
    importing it as an empty array.
  }];
  let arguments = (ins CTJS_ValueType:$array, CTJS_ValueType:$element);
  let assemblyFormat = "$element `to` $array attr-dict";
}

def CTJS_CreateRegExpOp : CTJS_Op<"create_regexp", [CTJS_MayThrow, CTJS_Safepoint]> {
  let summary = "A RegExp literal";
  let description = [{
    NOT a CTJS_RuntimeOp: aot_helpers.def declares no helper for a regexp
    literal, so there is nothing for getHelperID to name. Saying so here is
    better than pointing it at a helper that does something else - and it is
    a real gap in the ABI rather than in this file.
  }];
  let arguments = (ins StrAttr:$pattern, StrAttr:$flags);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$pattern `,` $flags attr-dict";
}

//===----------------------------------------------------------------------===//
// Calls
//===----------------------------------------------------------------------===//

def CTJS_CallOp
    : CTJS_RuntimeOp<"call", "ct_aot_call", CTJS_GenericEffects # [CTJS_MaySuspend]> {
  let summary = "Invoke a callee through the mixed-mode dispatcher";
  let description = [{
    THROUGH THE DISPATCHER, never around it. The callee may be interpreted, a
    compiled body, or a native built-in, and which of those it is is decided
    at run time by the one place that reads function_proto::aot_entry.
  }];
  let arguments = (ins CTJS_ValueType:$callee, CTJS_ValueType:$receiver,
                       Variadic<CTJS_ValueType>:$args);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$callee `(` $receiver (`,` $args^)? `)` attr-dict";
}

def CTJS_ConstructOp
    : CTJS_RuntimeOp<"construct", "ct_aot_construct", CTJS_GenericEffects # [CTJS_MaySuspend]> {
  let summary = "`new callee(...)`";
  let arguments = (ins CTJS_ValueType:$callee, CTJS_ValueType:$new_target,
                       Variadic<CTJS_ValueType>:$args);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$callee `(` $new_target (`,` $args^)? `)` attr-dict";
}

//===----------------------------------------------------------------------===//
// Control flow and completion
//
// NO CTJS BRANCH OPERATIONS. "Use the standard dialects wherever they suffice.
// cf.br, cf.cond_br, and cf.switch cover all bytecode branching."
//===----------------------------------------------------------------------===//

def CTJS_FuncOp : CTJS_Op<"func", [
    IsolatedFromAbove, FunctionOpInterface, Symbol,
    DeclareOpInterfaceMethods<CallableOpInterface>]> {
  let summary = "A JavaScript function: one region, an unstructured CFG";
  let description = [{
    A DISTINCT OPERATION RATHER THAN func.func, because a JavaScript function
    carries closure, receiver, new.target, constructor state, and
    program/function identity that do not belong on a func.func.

    It implements FunctionOpInterface and CallableOpInterface so that MLIR's
    inliner, symbol tables and pass infrastructure work unmodified - which is
    the whole reason for using an interface rather than inventing a parallel
    mechanism.
  }];
  let arguments = (ins SymbolNameAttr:$sym_name,
                       TypeAttrOf<FunctionType>:$function_type,
                       I32Attr:$upvalue_count,
                       OptionalAttr<DictArrayAttr>:$arg_attrs,
                       OptionalAttr<DictArrayAttr>:$res_attrs);
  let regions = (region AnyRegion:$body);
  let hasCustomAssemblyFormat = 1;
  let hasVerifier = 1;
  // NO extraClassDeclaration. MLIR 22 generates getArgumentTypes,
  // getResultTypes and getCallableRegion for a FunctionOpInterface operation
  // itself; declaring them here as the older idiom does is a redefinition.
}

def CTJS_ReturnOp : CTJS_Op<"return", [Terminator, ReturnLike, HasParent<"FuncOp">]> {
  let summary = "Return a value from a JavaScript function";
  let arguments = (ins CTJS_ValueType:$value);
  let assemblyFormat = "$value attr-dict";
}

def CTJS_ThrowOp
    : CTJS_Op<"throw", [Terminator, CTJS_MayThrow, CTJS_MayReenterJS, CTJS_Safepoint]> {
  let summary = "Throw a JavaScript value";
  let description = [{
    RE-ENTERING, which is the most surprising column on its runtime row and is
    real: an uncaught throw formats a diagnostic, and formatting reaches
    to_primitive_string, which calls the thrown object's own toString. Page
    JavaScript runs in the middle of a fatal error.
  }];
  let arguments = (ins CTJS_ValueType:$value);
  let assemblyFormat = "$value attr-dict";
}

def CTJS_ResumeThrowOp : CTJS_Op<"resume_throw", [Terminator]> {
  let summary = "Re-throw the value a handler landed with";
  let description = [{
    What the end of a `finally` block does on the thrown completion: the value
    was carried across the block and is thrown again. Distinct from ctjs.throw
    because it names no operand - the value is the pending completion, not
    something the block computed.
  }];
  let assemblyFormat = "attr-dict";
}

def CTJS_PushHandlerOp : CTJS_Op<"push_handler", [DeclareOpInterfaceMethods<BranchOpInterface>,
                                                  AttrSizedOperandSegments, Terminator]> {
  let summary = "Install an exception handler naming a landing block";
  let description = [{
    A TERMINATOR WITH TWO SUCCESSORS, which is how an unstructured CFG says
    "from here on, a throw goes there": control falls through to $body, and
    the handler block is a successor the unwinder may transfer to. Modelling
    it as an ordinary operation would leave the handler block unreachable in
    the CFG and every pass would treat it as dead.
  }];
  let arguments = (ins Variadic<CTJS_ValueType>:$bodyOperands,
                       Variadic<CTJS_ValueType>:$handlerOperands);
  let successors = (successor AnySuccessor:$body, AnySuccessor:$handler);
  let assemblyFormat = [{
    $body (`(` $bodyOperands^ `:` type($bodyOperands) `)`)?
    `catch` $handler (`(` $handlerOperands^ `:` type($handlerOperands) `)`)? attr-dict
  }];
  let hasVerifier = 1;
}

def CTJS_PopHandlerOp : CTJS_Op<"pop_handler", []> {
  let summary = "Remove the innermost exception handler";
  let description = [{
    Balance is a COMPILER invariant, not a runtime check: the runtime's
    ct_aot_handler_pop takes the globally innermost handler without consulting
    the frame, so a mis-balanced emission silently takes a CALLER's catch. The
    verifier here is what makes that a build error instead.
  }];
  let assemblyFormat = "attr-dict";
  let hasVerifier = 1;
}

//===----------------------------------------------------------------------===//
// Suspension - defined in Phase 8, not lowered until Phase 14
//===----------------------------------------------------------------------===//

def CTJS_SuspendOp
    : CTJS_Op<"suspend", CTJS_GenericEffects # [CTJS_MaySuspend]> {
  let summary = "await, yield, or yield*";
  let description = [{
    Defined now and lowered in Phase 14. Defining it early "keeps the importer
    from needing a second structural pass later" - a function containing one
    is structurally different from one that does not, and discovering that
    after the importer exists would mean rewriting it.
  }];
  let arguments = (ins CTJS_SuspendKind:$kind, CTJS_ValueType:$value);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$kind $value attr-dict";
}

def CTJS_ResumePointOp : CTJS_Op<"resume_point", []> {
  let summary = "A numbered point a suspended function can resume at";
  let arguments = (ins I32Attr:$index);
  let assemblyFormat = "$index attr-dict";
  let hasVerifier = 1;
}

//===----------------------------------------------------------------------===//
// Frames and rooting
//===----------------------------------------------------------------------===//

def CTJS_FrameEnterOp : CTJS_RuntimeOp<"frame_enter", "ct_aot_enter", [CTJS_MayThrow]> {
  let summary = "Establish the AOT shadow frame";
  let description = [{
    May throw, and only that: the depth guard raises when the frame stack is
    exhausted, and the row says the caller returns FAILED without leaving. It
    does not re-enter and is not a safepoint - nothing inside it allocates.

    ITS FAILURE HAS A THIRD SHAPE and no backend may treat it as either of the
    other two. ct_aot_enter returns NULL on the depth raise - not a status to
    test, not a poll to defer - which is why RuntimeHelpers.hpp carves it out
    of both tiers by name. A status compare here would be comparing a pointer
    against an enumerator, and the next helper call would dereference null.

    $reg_count IS THE FRAME'S REGISTER WINDOW, and it is carried here because
    nothing else in the IR knows it. The importer reads proto.frame_size to
    seed its register file and then dropped the number; ct_aot_enter needs it,
    and a backend that guessed param_count instead would size the window to
    the parameters and leave every local without a slot.

    THE OTHER FOUR ARGUMENTS ARE NOT OPERANDS AND WILL NOT BECOME ANY. `ctx`
    and `site` are parameters of ct_aot_entry_fn, `receiver` is another, and
    `storage` is CT_AOT_FRAME_BYTES of caller-allocated space that exists only
    in the emitted body. All four exist at the entry and nowhere else in the
    IR, so this operation is lowered by the pattern that builds the entry
    rather than by a generic one.
  }];
  let arguments = (ins I32Attr:$reg_count);
  let results = (outs CTJS_ContextType:$context);
  let assemblyFormat = "$reg_count attr-dict";
}

def CTJS_FrameExitOp : CTJS_RuntimeOp<"frame_exit", "ct_aot_leave", []> {
  let summary = "Release the AOT shadow frame";
  let description = [{
    Infallible, and it must NOT run on the unwound path - the unwinder has
    already destroyed the frame, and leaving again would pop somebody else's.
    That is a lowering obligation rather than something ODS can express.
  }];
  let arguments = (ins CTJS_ContextType:$context);
  let assemblyFormat = "$context attr-dict";
}

def CTJS_RootOp : CTJS_Op<"root", []> {
  let summary = "Keep a value reachable across a safepoint";
  let description = [{
    CONSERVATIVE, and Phase 29 refines it. The collector is precise and walks
    only the roots it knows; a value living in a native frame is reachable
    from none of them, so anything live across an operation carrying
    CTJS_Safepoint has to be parked where the collector can see it.

    NOT a CTJS_RuntimeOp: the runtime's ct_aot_slots hands back the frame's
    register window and the STORE is generated code, so there is no single
    helper this lowers to.
  }];
  let arguments = (ins CTJS_ContextType:$context, CTJS_ValueType:$value);
  let assemblyFormat = "$value `in` $context attr-dict";
}

#endif // CTCOMPILE_CTJS_OPS
