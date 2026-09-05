// CTJS properties operations; included by CTJSOps.td.
#ifndef CTCOMPILE_CTJS_OPS_PROPERTIES_TD
#define CTCOMPILE_CTJS_OPS_PROPERTIES_TD

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
  // PHASE 55 (may_reenter 1: get_prop/get_index). $object NEITHER, THE STAR
  // PROOF for a confined base: a fresh object's prototype is null
  // (value.hpp:569) and its accessor table empty, lookup_property walks the
  // own chain (objects.cpp:445-461) - a getter runs only if found on THAT
  // chain - and then consults the shared table with data-only `find`
  // (462-464; value.hpp:575-580); the proxy arm (425-433) is unreachable for
  // a site and the array arm (467-511) runs no call(). The two operations
  // that could give a confined base an accessor, set_proto and
  // define_accessor, sink the base itself. $key SINK(converted): to_string
  // (objects.cpp:154) -> to_primitive_string (coerce.cpp:171-179) calls the
  // key's own toString/valueOf with the key as receiver.
  let arguments = (ins CTJS_ValueType:$object,
                       Arg<CTJS_ValueType, "", [CTJS_SinkConverted]>:$key);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$object `[` $key `]` attr-dict";
}

def CTJS_SetPropertyOp : CTJS_RuntimeOp<"set_property", "ct_aot_set_index", CTJS_GenericEffects> {
  let summary = "Generic property write with full ECMAScript semantics";
  // PHASE 55 (may_reenter 1: set_prop/set_index). $object NEITHER by the
  // star proof: the object arm (objects.cpp:345-349) goes through
  // assign_through_accessor (397-419), which walks ONLY obj->prototype - null
  // for a site - and the array arm (361-378) resizes on `length` only; the
  // proxy arm (334-344) is unreachable for a site. $key SINK(converted):
  // to_string (objects.cpp:193) runs the key's own toString. $value
  // SINK(stored): object_object::set (value.hpp:632-639) / store_index
  // (objects.cpp:189), traced (objects.cpp:51 / 42).
  let arguments = (ins CTJS_ValueType:$object,
                       Arg<CTJS_ValueType, "", [CTJS_SinkConverted]>:$key,
                       Arg<CTJS_ValueType, "", [CTJS_SinkStored]>:$value);
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
  // PHASE 55 (may_reenter 1: delete_index). $object NEITHER: delete_index is
  // an `erase` (objects.cpp:325-329). $key SINK(converted): to_string (327).
  let arguments = (ins CTJS_ValueType:$object,
                       Arg<CTJS_ValueType, "", [CTJS_SinkConverted]>:$key);
  let assemblyFormat = "$object `[` $key `]` attr-dict";
}

def CTJS_HasPropertyOp
    : CTJS_RuntimeOp<"has_property", "ct_aot_has_property", CTJS_GenericEffects> {
  let summary = "`key in obj`";
  // PHASE 55 (may_reenter 1: has_property). $object NEITHER: the proxy arm
  // (objects.cpp:256-264) is unreachable for a site and 266-278 makes no
  // call. $key SINK(converted): to_string (263/265). The result is a
  // boolean, not an object.
  let arguments = (ins CTJS_ValueType:$object,
                       Arg<CTJS_ValueType, "", [CTJS_SinkConverted]>:$key);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$key `in` $object attr-dict";
}

//===----------------------------------------------------------------------===//

#endif
