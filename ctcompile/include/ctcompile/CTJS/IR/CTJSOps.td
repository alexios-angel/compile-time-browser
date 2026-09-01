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

def CTJS_PassNewTargetOp
    : CTJS_RuntimeOp<"pass_new_target", "ct_aot_pass_new_target"> {
  let summary = "Hand this frame's new.target to the next frame pushed";
  let description = [{
    What `super(...)` emits just before its call, and the other half of why
    lowering `load_home` alone moved nothing: 14 of Bootstrap's 24 `super`
    functions call `super(...)` rather than `super.m()`.

    NO OPERANDS, deliberately. The helper reads THIS frame's new.target, and an
    explicit operand would let a backend hand over a stale or cached one.

    ADJACENCY TO THE CALL AFTER IT IS AN INVARIANT NOTHING CHECKS. The flag is
    consumed by the next JS-closure frame push, whatever that turns out to be -
    and if the callee is a native or a generator it is not consumed at all, so
    the next ordinary call anywhere sees a truthy new.target. That leak is the
    interpreter's and is reproduced rather than repaired.
  }];
  let assemblyFormat = "attr-dict";
}

def CTJS_GetProtoOp : CTJS_RuntimeOp<"get_proto", "ct_aot_get_proto"> {
  let summary = "The prototype link of an object, or undefined";
  let description = [{
    THE SECOND HALF OF `super`, and the reason lowering `load_home` alone moved
    no functions at all: `super.m()` is load_home, then this, then an ordinary
    property read. All 24 of Bootstrap's `super` functions simply stopped one
    opcode later.

    Undefined for anything that is not heap_kind::object exactly - an array, a
    string, a proxy, a native and a closure all report undefined, a closure's
    chain being a different field. A fresh object's prototype is null and only
    `extends` sets it, so `super.m()` in a BASE-class method reads null. A
    backend that folded super-dispatch would produce a different program.
  }];
  let arguments = (ins CTJS_ValueType:$object);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$object attr-dict";
}

def CTJS_SetProtoOp : CTJS_RuntimeOp<"set_proto", "ct_aot_set_proto"> {
  let summary = "Point an object's prototype link at another value";
  let description = [{
    What `class D extends B` writes, and a silent no-op on anything that is not
    an object - which is the interpreter's behaviour, not an oversight.
  }];
  let arguments = (ins CTJS_ValueType:$object, CTJS_ValueType:$proto);
  let assemblyFormat = "$proto `on` $object attr-dict";
}

def CTJS_LoadHomeOp : CTJS_RuntimeOp<"load_home", "ct_aot_home"> {
  let summary = "The `__home` of the running closure - what `super` resolves against";
  let description = [{
    NO OPERANDS, because the answer is a property of the frame rather than of
    anything in it: the helper reads `fr->closure` and looks `__home` up on it.
    A compiled frame has a closure for the same reason it can read its own
    upvalues - ct_aot_enter puts it there.

    ALL THREE FLAGS ARE ZERO and that is verified rather than assumed:
    closure_object::find consults `props` only and never `accessors`, so no
    getter can run, nothing allocates and nothing can throw. Undefined when the
    frame has no closure and undefined when the closure has no `__home` - the
    top-level script frame is both.

    `__home` IS AN ORDINARY OBSERVABLE PROPERTY installed by op::set_prop when a
    class is built, which is why this is a lookup and not a field read, and why
    the home object must never be baked into an image.
  }];
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "attr-dict";
}

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

def CTJS_CheckOp : CTJS_Op<"check", [DeclareOpInterfaceMethods<BranchOpInterface>,
                                     AttrSizedOperandSegments, Terminator]> {
  let summary = "Continue, or transfer to this region's handler if the call was caught";
  let description = [{
    THE HANDLER EDGE ORIGINATES HERE, NOT AT ctjs.push_handler, and that is the
    whole design rather than a detail.

    A handler block needs the register file AS OF THE THROW. ctjs.push_handler
    also names the pad, but only to keep it reachable in the CFG - taking the
    edge from there would hand the handler the registers as they were when the
    `try` was ENTERED. `function g(f) { var n = 0; try { n = 1; f(); } catch (e)
    {} return n; }` then answers 0 where the interpreter answers 1, because
    context::unwind_to_handler overwrites exactly ONE slot - the catch register -
    and leaves every other register as the throw left it.

    SO THERE IS ONE OF THESE AFTER EVERY FALLIBLE OPERATION inside a protected
    region, and its $handlerOperands are a snapshot taken BEFORE that operation
    ran. Passing the post-operation vector instead would name the operation's own
    result in a block the operation does not dominate.

    AFTER EVERY FALLIBLE OPERATION, NOT AFTER EVERY INSTRUCTION. One bytecode
    instruction can be two of them: op::call_method is a ctjs.get_property and
    then a ctjs.call, and `try { o.m(); } catch (e) {}` is about as ordinary as
    JavaScript gets.
  }];
  let arguments = (ins Variadic<CTJS_ValueType>:$contOperands,
                       Variadic<CTJS_ValueType>:$handlerOperands);
  let successors = (successor AnySuccessor:$cont, AnySuccessor:$handler);
  let assemblyFormat = [{
    $cont (`(` $contOperands^ `:` type($contOperands) `)`)?
    `caught` $handler (`(` $handlerOperands^ `:` type($handlerOperands) `)`)? attr-dict
  }];
  let hasVerifier = 1;
}

def CTJS_CatchLandOp : CTJS_Op<"catch_land", []> {
  let summary = "Take the thrown value and the pad id a caught throw landed on";
  let description = [{
    THE FIRST OPERATION OF A HANDLER BLOCK, and only there. ct_aot_catch_land
    reads back what context::unwind_to_handler already wrote - the frame's ip
    and the catch register - and then CLEARS CT_AOT_PAD_BIT, so running it twice
    is not a second catch and running it on a normal path reads a pad that was
    never set.

    IT ANSWERS TWO THINGS. The pad id says WHICH handler fired, which is what
    lets one compiled frame carry several; the thrown value is what the catch
    clause binds. The pad comes first because the ABI shape verifier requires
    the helper's uint32_t return to be result 0 and its out-parameter to follow.

    NOT A CTJS_RuntimeOp: it is the only operation with two results, and the
    plain form keeps it out of the interface that assumes one.
  }];
  let results = (outs I32:$pad, CTJS_ValueType:$thrown);
  let assemblyFormat = "attr-dict";
  let hasVerifier = 1;
}

def CTJS_MakeArgumentsOp
    : CTJS_RuntimeOp<"make_arguments", "ct_aot_make_arguments", CTJS_GenericEffects> {
  let summary = "The `arguments` object";
  let description = [{
    NO OPERANDS, because what it needs is not in the IR at all. The window and
    the count come from the FRAME - ct_aot_args and ct_aot_argc - and the
    lowering materialises both. The importer has no SSA value for an argument
    past the declared parameters: it seeds register i for i >= param_count with
    `ctjs.constant undefined`, so there is nothing here to pass.

    IT ALSO WRITES call_frame::arguments_object, which is not a detail:
    ctjs.gather_rest READS that side-slot on one of its two paths, because
    building this array claims a register an extra argument may be sitting in.
    The write belongs to the shared context member rather than to either
    caller, so a fusion of the two operations cannot drop it.

    RAISE TIER: it answers the array rather than a status.
  }];
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "attr-dict";
}

def CTJS_GatherRestOp
    : CTJS_RuntimeOp<"gather_rest", "ct_aot_gather_rest", CTJS_GenericEffects> {
  let summary = "A rest parameter - the arguments past the declared ones";
  let description = [{
    `function f(a, ...xs)`. NOT array rest and NOT destructuring rest, which
    compile to an index loop and to new_object plus copy_props respectively -
    this opcode has exactly one emitter, the parameter prologue.

    `from` IS A COUNT, NOT A REGISTER, and it is the only operand: the window
    and the count come from the frame for ctjs.make_arguments' reason.

    TWO PATHS AND THE FIRST IS THE SUBTLE ONE. If the body also built an
    `arguments` object - which happens BEFORE this and claims a register an
    extra argument may be in - then the frame's copy is the one that still has
    them, and reading the window instead would return the wrong values. Landing
    this without ctjs.make_arguments leaves that path dead in the compiled tier.
  }];
  let arguments = (ins UI32Attr:$from);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "`from` $from attr-dict";
}

def CTJS_DeleteNamedOp : CTJS_Op<"delete_named", [CTJS_Safepoint]> {
  let summary = "`delete o.k` - the NAMED form";
  let description = [{
    NOT ctjs.delete_property, WHICH IS THE COMPUTED ONE. They are separate
    opcodes in the bytecode because the key arrives differently: a
    constant-pool name here, a VALUE there - and converting a value key runs
    to_string, which for an object runs user JavaScript. So delete_index can
    throw and re-enter, and this cannot.

    A PLAIN CTJS_Op FOR ctjs.define_accessor's REASON. ct_aot_delete_prop's
    parameter roles are frame/operand/name, and the ABI shape verifier counts a
    `name` as an SSA operand rather than an immediate, so the census allows ZERO
    inherent attributes and a CTJS_RuntimeOp carrying $name would fail its own
    verifier. It lowers to TWO calls: ct_aot_intern_name, then the helper.

    A SAFEPOINT because interning allocates, even though the helper's own row is
    all zeros - erasing from a hash map is malloc's business, not the
    collector's.
  }];
  let arguments = (ins CTJS_ValueType:$object, StrAttr:$name);
  let assemblyFormat = "$name `from` $object attr-dict";
}

def CTJS_OwnKeysOp : CTJS_RuntimeOp<"own_keys", "ct_aot_own_keys", CTJS_GenericEffects> {
  let summary = "The own string keys of an object, as an array - what for-in iterates";
  let description = [{
    `for (k in o)` COMPILES TO A for-of OVER THIS, which is how the runtime
    keeps one iteration mechanism instead of two: own_keys builds the array and
    ctjs.iterable and an index loop do the rest, both of which already lower.

    IN DEFINITION ORDER, data properties and accessors interleaved, because
    that is what a page sees and what Object.keys has to match. An ARRAY source
    enumerates its indices AS STRINGS - `for (k in [7,8])` binds "0" and "1",
    not 0 and 1 - and anything else yields an empty array rather than throwing.

    RAISE TIER: the helper returns the ARRAY rather than a status, so on failure
    the value is meaningless and a caller polls ct_aot_failed at a back edge. It
    allocates, so it is a safepoint even though it cannot run user code.
  }];
  let arguments = (ins CTJS_ValueType:$source);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "`of` $source attr-dict";
}

def CTJS_DefineAccessorOp : CTJS_Op<"define_accessor", [CTJS_Safepoint]> {
  let summary = "Install a getter, a setter, or both, under one name";
  let description = [{
    BOTH OPCODES ARE THIS ONE OPERATION. op::define_getter and op::define_setter
    share a VM_CASE and a helper, because the runtime primitive is already
    fused: accessor_table::define SKIPS undefined halves, which is exactly what
    merges a get/set pair into one entry.

    THE DISCRIMINATOR IS THE OPCODE, and it goes no further than the importer. A
    compiled site knows statically which half it is compiling, so this carries
    both operands and the caller passes `undefined` for the one it does not
    have - there is no attribute saying which, and there must not be.

    NOT A CTJS_RuntimeOp, AND THAT IS FORCED. ct_aot_define_accessor's parameter
    roles are frame/operand/name/operand/operand, and the ABI shape verifier
    counts a `name` as an SSA operand rather than an immediate - so the census
    allows ZERO inherent attributes, and a CTJS_RuntimeOp carrying $name would
    fail its own verifier. It is the same reason ctjs.load_upvalue is a plain
    op: this lowers to TWO calls, ct_aot_intern_name and then the helper.

    A SAFEPOINT BECAUSE INTERNING ALLOCATES - the name pool is immortal and a
    new record is a heap allocation - even though installing an accessor only
    STORES the function and never invokes it, which is why the helper's own row
    is all zeros.
  }];
  let arguments = (ins CTJS_ValueType:$target, StrAttr:$name,
                       CTJS_ValueType:$getter, CTJS_ValueType:$setter);
  let assemblyFormat = "$name `on` $target `get` $getter `set` $setter attr-dict";
}

def CTJS_CopyPropsOp : CTJS_RuntimeOp<"copy_props", "ct_aot_copy_props"> {
  let summary = "Copy a source's own properties onto a target - object spread";
  let description = [{
    `{...o}` in an object literal and `{a, ...rest}` in a destructuring pattern,
    which are its only two emitters. Both always run it against a freshly built
    object, so the target is never a value the program can also be holding under
    another name.

    ALL THREE FLAGS ARE ZERO, and that is a measurement rather than an
    assumption: object_object::set grows a std::vector and a hash map, which is
    malloc and not allocate(), so nothing is collected, nothing throws and no
    accessor can run. It looks like ctjs.append and it is NOT a safepoint the
    way append's row is.

    AN ARRAY SOURCE SPREADS BY INDEX - `{...[7, 8]}` is `{"0": 7, "1": 8}` -
    and anything that is neither object nor array contributes nothing.
  }];
  let arguments = (ins CTJS_ValueType:$target, CTJS_ValueType:$source);
  let assemblyFormat = "$source `into` $target attr-dict";
}

def CTJS_CallSpreadOp
    : CTJS_RuntimeOp<"call_spread", "ct_aot_call_spread", CTJS_GenericEffects # [CTJS_MaySuspend]> {
  let summary = "Call with an argument ARRAY rather than a window";
  let description = [{
    WHAT A SPREAD CALL COMPILES TO - and only a spread call. `f(a, b)` is
    ctjs.call; `f(...xs)`, `o.m(...xs)`, `o[k](...xs)`, `super.m(...xs)` and
    `super(...xs)` are this. Nine of Bootstrap's functions need it, the largest
    single opcode left after `super`.

    THE ARRAY IS ALREADY BUILT BY OPERATIONS THAT ALREADY LOWER: new_array, one
    append per plain argument, and for a spread an iterable-plus-index loop.
    Nothing new was needed for the arguments themselves.

    SO THIS NEEDS NONE OF ctjs.call's WINDOW MACHINERY. ct_aot_call takes a
    contiguous `argv` and an `argc`, which is why its lowering parks each
    argument into a reserved run of frame slots and passes the run's address.
    Here the count was not known until the spread ran, so the arguments arrive
    as ONE value that is already a rooted array - there is no run to reserve.

    A NON-ARRAY YIELDS NO ARGUMENTS, not one. `f(...undefined)` calls f with
    nothing.
  }];
  let arguments = (ins CTJS_ValueType:$callee, CTJS_ValueType:$args,
                       CTJS_ValueType:$receiver);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$callee `(` $receiver `,` $args `)` attr-dict";
}

def CTJS_ConstructSpreadOp
    : CTJS_RuntimeOp<"construct_spread", "ct_aot_construct_spread",
                     CTJS_GenericEffects # [CTJS_MaySuspend]> {
  let summary = "`new C(...args)`";
  let description = [{
    THE OTHER HALF OF ONE VM_CASE, and NOT the spread form of ctjs.construct.
    op::construct_apply calls context::construct wholesale - no acceptance test
    of its own and no pending_new_target_ handling - so `new C(...xs)` reports
    new.target UNDEFINED where `new C(x)` reports C, and a non-constructor gets
    an uncatchable raise where the plain form throws a catchable TypeError.

    That divergence is the INTERPRETER's. It is reproduced here rather than
    repaired, because repairing it in one tier only would be worse than the
    divergence; repairing it in both is a separate change with its own
    before/after test.
  }];
  let arguments = (ins CTJS_ValueType:$callee, CTJS_ValueType:$args);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$callee `(` $args `)` attr-dict";
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
