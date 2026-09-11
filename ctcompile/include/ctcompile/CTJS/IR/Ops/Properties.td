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
  // An inherited getter, including on implicit Object.prototype, can retain
  // the receiver. A fresh allocation alone does not prove an own data field.
  // Key conversion can likewise invoke the key's toString/valueOf.
  let arguments = (ins Arg<CTJS_ValueType, "", [CTJS_SinkPassed]>:$object,
                       Arg<CTJS_ValueType, "", [CTJS_SinkConverted]>:$key);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$object `[` $key `]` attr-dict";
}

def CTJS_SetPropertyOp : CTJS_RuntimeOp<"set_property", "ct_aot_set_index", CTJS_GenericEffects> {
  let summary = "Generic property write with full ECMAScript semantics";
  // An inherited setter can retain the receiver and value without creating
  // an own field. Stored-value refinement therefore needs an independent
  // own-data proof; receiver exposure alone cannot justify removing that sink.
  let arguments = (ins Arg<CTJS_ValueType, "", [CTJS_SinkPassed]>:$object,
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
  // an `erase` (delete_index -> delete_own_property, vm/objects/descriptors.cpp).
  // $key SINK(converted): to_string.
  let arguments = (ins CTJS_ValueType:$object,
                       Arg<CTJS_ValueType, "", [CTJS_SinkConverted]>:$key);
  let assemblyFormat = "$object `[` $key `]` attr-dict";
}

def CTJS_HasPropertyOp
    : CTJS_RuntimeOp<"has_property", "ct_aot_has_property", CTJS_GenericEffects> {
  let summary = "`key in obj`";
  // PHASE 55 (may_reenter 1: has_property). $object NEITHER: the proxy arm
  // (has_property, vm/objects/chain.cpp) is unreachable for a site and the
  // chain walk makes no call. $key SINK(converted): to_string. The result is a
  // boolean, not an object.
  let arguments = (ins CTJS_ValueType:$object,
                       Arg<CTJS_ValueType, "", [CTJS_SinkConverted]>:$key);
  let results = (outs CTJS_ValueType:$result);
  let assemblyFormat = "$key `in` $object attr-dict";
}

//===----------------------------------------------------------------------===//

#endif
