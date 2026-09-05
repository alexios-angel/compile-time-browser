// CTJS runtime operations; included by CTJSOps.td.
#ifndef CTCOMPILE_CTJS_OPS_RUNTIME_TD
#define CTCOMPILE_CTJS_OPS_RUNTIME_TD

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

// PHASE 55: NEITHER - a field read (objects.cpp:245-248); the result is an
// external object (a site's prototype is null).
def CTJS_GetProtoOp : CTJS_RuntimeOp<"get_proto", "ct_aot_get_proto", [CTJS_NoEscapeEffects]> {
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
  // PHASE 55: $object SINK(proto_mutated) - after objects.cpp:250-252 stores
  // the link, every later load (445-461) and store (400-417) walks the new
  // chain and can reach a getter or setter with the object as `this`, which
  // is exactly what the star proof on the property operations rules out.
  // $proto SINK(stored): the link is traced (objects.cpp:56).
  let arguments = (ins Arg<CTJS_ValueType, "", [CTJS_SinkProtoMutated]>:$object,
                       Arg<CTJS_ValueType, "", [CTJS_SinkStored]>:$proto);
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
  // PHASE 55 (R1): a boxed site, reason `arguments`, and the function is
  // flagged captures_all_arguments. make_arguments_object copies the RAW
  // register window slots[0..argc) (call.cpp:783-790) - values the IR has no
  // SSA name for - into a fresh array rooted per frame (794; objects.cpp:684).
  // Not a whole-function refusal for object/array sites: the array holds
  // only what the parameter registers held in the prologue, and the analysis
  // CHECKS that placement (entry block, before every site) rather than
  // believing it - otherwise `arguments_late`.
  let results = (outs Res<CTJS_ValueType, "", [CTJS_BoxedArguments]>:$result);
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
  // PHASE 55 (R1): a boxed site, reason `arguments`, guarded like
  // make_arguments (gather_rest_values, call.cpp:799-813, reads the frame's
  // arguments array back at 806-808).
  let arguments = (ins UI32Attr:$from);
  let results = (outs Res<CTJS_ValueType, "", [CTJS_BoxedArguments]>:$result);
  let assemblyFormat = "`from` $from attr-dict";
}

// PHASE 55: NEITHER - objects.cpp:200-202 erases a name from the own table.
def CTJS_DeleteNamedOp : CTJS_Op<"delete_named", [CTJS_Safepoint, CTJS_NoEscapeEffects]> {
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
  // PHASE 55: $source NEITHER - objects.cpp:204-219 reads keys and makes
  // strings (an untracked kind), no call(). The result is a boxed site,
  // reason runtime_array: an array the runtime made, not a literal.
  let arguments = (ins CTJS_ValueType:$source);
  let results = (outs Res<CTJS_ValueType, "", [CTJS_BoxedRuntimeArray]>:$result);
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
  // PHASE 55: $target SINK(accessor_defined) - objects.cpp:221-228 installs
  // the pair, and 452-457 / 404-414 then run user code with the target as
  // `this` on every later load or store, breaking the star proof for it.
  // $getter and $setter SINK(stored): traced (objects.cpp:52-55).
  let arguments = (ins Arg<CTJS_ValueType, "", [CTJS_SinkAccessorDefined]>:$target,
                       StrAttr:$name,
                       Arg<CTJS_ValueType, "", [CTJS_SinkStored]>:$getter,
                       Arg<CTJS_ValueType, "", [CTJS_SinkStored]>:$setter);
  let assemblyFormat = "$name `on` $target `get` $getter `set` $setter attr-dict";
}

// PHASE 55: both NEITHER - objects.cpp:230-243 is a raw `set` of the copied
// entries, no accessor and no call(); everything ever stored into $source was
// already sunk at its own store. (Phase 56's "spread means not closed" is a
// SHAPE fact, not an escape fact.)
def CTJS_CopyPropsOp
    : CTJS_RuntimeOp<"copy_props", "ct_aot_copy_props", [CTJS_NoEscapeEffects]> {
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
  // PHASE 55 (may_reenter 1: apply). $callee and $receiver SINK(passed);
  // $args NEITHER: spread_arguments COPIES the items (call.cpp:711-717) and
  // the array itself is stored nowhere - its elements were sunk at
  // create_array/append. The re-entering arm is the call, and no site
  // reaches it except through an element, already sunk.
  let arguments = (ins Arg<CTJS_ValueType, "", [CTJS_SinkPassed]>:$callee,
                       CTJS_ValueType:$args,
                       Arg<CTJS_ValueType, "", [CTJS_SinkPassed]>:$receiver);
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
  // PHASE 55 (may_reenter 1: construct_apply). $callee SINK(passed); $args
  // NEITHER for call_spread's reason (call.cpp:734-741 -> spread_arguments).
  let arguments = (ins Arg<CTJS_ValueType, "", [CTJS_SinkPassed]>:$callee,
                       CTJS_ValueType:$args);
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
  // PHASE 55 (may_reenter 1: iterable). $source CARRY, the one carrier: the
  // array arm returns THE SAME ARRAY (call.cpp:555), so the result aliases
  // the source. Every other arm builds a fresh array (559, 579, 595, 603,
  // 611) - the result's boxed-site claim, reason runtime_array - and reads
  // the source with `find` (578, 594, 602, 610) or lookup_property (615),
  // which for an object site is data-only by the star proof on get_property.
  // The re-entering arm resumes a generator (582): generator_resume reads
  // the source once, at call.cpp:957 (`find("__co")`), stores it nowhere and
  // resumes with the coroutine's own saved receiver - no site of this
  // function is handed to the body.
  let arguments = (ins Arg<CTJS_ValueType, "", [CTJS_Carry]>:$source);
  let results = (outs Res<CTJS_ValueType, "", [CTJS_BoxedRuntimeArray]>:$result);
  let assemblyFormat = "`of` $source attr-dict";
}

#endif
