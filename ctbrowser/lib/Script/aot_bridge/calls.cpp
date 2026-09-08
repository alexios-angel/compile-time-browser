// The AOT ABI - calls beyond ct_aot_call: construct, closures, the spread
// forms, promise wrapping, and the four module rows.
//
// One of four files carved out of a 1,586-line aot_bridge.cpp on 2026-09-08.
// `struct aot_bridge` is declared in internal.hpp beside this; each of its
// bodies is defined in the file that also holds the extern "C" row calling
// it, so a wrapper and the member behind it are still one translation unit
// and the call still inlines. The four members every file calls - ctx_of,
// frame_of, frame_record and check - stay in-class in the header for the
// same reason.

#include "internal.hpp"

namespace ctbrowser::script {

// ct_aot_construct. op::construct's own dispatch through the shared
// context::construct_new.
//
// NOT context::construct WHOLESALE, which the row is emphatic about and is
// right: construct() tests !is_callable() FIRST and raise()s, which no
// try/catch can see, while the opcode allocates, runs the field
// initialisers and only then throws a CATCHABLE TypeError. Delegating
// wholesale turns `try { new obj() } catch` into an engine fault.
//
// `site` IS THIS FRAME'S OWN function_proto, which is what names the
// function in that TypeError. There is no origin scan to go with it -
// callee_origin walks emitted bytecode from an ip and a register index, and
// an AOT frame has neither - so describe_callee renders the callee as "the
// value".
std::int32_t aot_bridge::construct(aot::ct_aot_frame * f, std::uint64_t callee,
                                   const std::uint64_t * argv, std::uint32_t argc,
                                   const aot::ct_aot_site * site, std::uint64_t * out) {
    aot_frame_storage & held = frame_of(f);
    context & cx = *held.ctx;
    // COPIED OUT BEFORE THE CALL, for ct_aot_call's reason: argv points
    // into registers_ for a compiled body, and the call resizes it.
    std::vector<value> args;
    args.reserve(argc);
    for (std::uint32_t i = 0; i < argc; ++i) { args.push_back(value::from_bits(argv[i])); }

    const function_proto & from = *reinterpret_cast<const function_proto *>(site);
    const value produced = cx.construct_new(value::from_bits(callee), args, from);
    const std::int32_t status = check(f);
    if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
    return status;
}

// ct_aot_make_closure. The shared context::make_closure, with the ABI's
// parallel array.
//
// A NON-CLOSURE ENCLOSING VALUE BECOMES nullptr, not a cast: the top level
// has no enclosing closure and passes undefined, and make_closure reads
// `enclosing->owner` to choose the program.
//
// RAISE TIER ONLY, from three sources rather than one - the allocation
// ceiling, no program with no enclosing closure, and a function index or
// upvalue count that does not match. make_closure raises for all three and
// the caller polls ct_aot_failed; there is no status to return, because the
// row answers with a value.
std::uint64_t aot_bridge::make_closure(aot::ct_aot_frame * f, std::uint64_t enclosing_closure,
                                       std::uint32_t function_index,
                                       const std::uint64_t * local_upvalues,
                                       std::uint32_t upvalue_count, std::uint64_t enclosing_this) {
    context & cx = *frame_of(f).ctx;
    const value enclosing = value::from_bits(enclosing_closure);
    closure_object * parent = enclosing.is_kind(heap_kind::function)
                                  ? static_cast<closure_object *>(enclosing.as_heap())
                                  : nullptr;
    return cx
        .make_closure(parent, function_index,
                      context::upvalue_source{local_upvalues, upvalue_count, nullptr},
                      value::from_bits(enclosing_this))
        .bits();
}

// ct_aot_wrap_promise. An async function's `return`, and the ONE half of
// async that does not suspend - which is what makes it implementable
// ahead of Phase 14's suspension work rather than with it. An async
// function containing no `await` compiles to a body carrying this opcode
// and no await_value at all.
//
// RAISE TIER: the row is may_throw only through the allocation ceiling,
// uncatchable and unwinding nothing, so this returns a value plainly and
// never a status - allocate() raises past the ceiling and STILL returns a
// well-formed object, so the operation completes and its result is
// written. Returning a bare 0 on failure would hand back
// value::from_bits(0), which is not undefined, and the lowering parks the
// result as a GC root and traces it.
//
// may_reenter is 0 and that was verified against the INSTALLED factory
// rather than the VM stub: detail::make_promise is new_table, a
// prototype, four sets and a make_array, with no call() anywhere.
std::uint64_t aot_bridge::wrap_promise(aot::ct_aot_frame * f, std::uint64_t v) {
    context & cx = *frame_of(f).ctx;
    return cx.wrap_in_promise(value::from_bits(v)).bits();
}

// ct_aot_call_spread and ct_aot_construct_spread - the two halves of
// VM_CASE(apply), through the shared members.
//
// NO ARGUMENT WINDOW AND NO argc, which is the whole difference from
// ct_aot_call. The arguments arrived as an ARRAY because their count was
// not known until the spread ran, so the array is one already-rooted value
// and there is nothing to copy out and nothing to park a run of.
//
// THE site IS IGNORED BY BOTH, exactly as ct_aot_call ignores its own: the
// messages these paths raise are the interpreter's, and neither names the
// enclosing function. It is taken so the ABI does not have to change if one
// of them ever does.
std::int32_t aot_bridge::call_spread(aot::ct_aot_frame * f, std::uint64_t callee,
                                     std::uint64_t arg_array, std::uint64_t receiver,
                                     const aot::ct_aot_site * site, std::uint64_t * out) {
    (void)site;
    context & cx = *frame_of(f).ctx;
    const value produced = cx.call_spread(value::from_bits(callee), value::from_bits(arg_array),
                                          value::from_bits(receiver));
    const std::int32_t status = check(f);
    if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
    return status;
}

std::int32_t aot_bridge::construct_spread(aot::ct_aot_frame * f, std::uint64_t callee,
                                          std::uint64_t arg_array, const aot::ct_aot_site * site,
                                          std::uint64_t * out) {
    (void)site;
    context & cx = *frame_of(f).ctx;
    const value produced =
        cx.construct_spread(value::from_bits(callee), value::from_bits(arg_array));
    const std::int32_t status = check(f);
    if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
    return status;
}

// ---- THE FOUR MODULE ROWS -------------------------------------------
//
// Each delegates to the context member VM_CASE(load_import),
// VM_CASE(bind_export), VM_CASE(load_namespace) and VM_CASE(dyn_import)
// now call, so the two tiers cannot resolve a specifier, raise a message
// or adopt a cell differently.
//
// A std::string PER CALL, and it is not a shortcut. modules_,
// module_record::resolved and module_record::exports are plain
// flat_map<std::string, ...>, and core/containers.hpp says at length that
// `flat_map<std::string, V>::find` takes the KEY TYPE - there is no
// heterogeneous overload without string_hash, which these three maps do not
// use. Building the key is what the interpreter does too; it simply has one
// already, because its names came out of the constant pool. Every one of
// these opcodes runs once per binding in a module prologue, never in a
// loop, which is why the allocation is not worth a wider map.

// ct_aot_module_import_cell. RAISE TIER: it answers the CELL rather than a
// status, so on either miss the value is undefined and a caller polls
// ct_aot_failed at a back edge. Not a safepoint - it allocates nothing.
std::uint64_t aot_bridge::module_import_cell(aot::ct_aot_frame * f, const char * specifier,
                                             std::uint32_t specifier_len, const char * export_name,
                                             std::uint32_t export_name_len) {
    context & cx = *frame_of(f).ctx;
    return cx
        .module_import_cell(std::string{specifier, specifier_len},
                            std::string{export_name, export_name_len})
        .bits();
}

// ct_aot_module_export_cell. THE CALLER SEEDS *out, which is what makes the
// conditional write expressible without a status the enum does not have.
//
// The row asked for CT_AOT_NO_WRITE when there is no module being
// evaluated; ct_aot_status has four members and none of them is that. So
// the lowering initialises the out-slot with the destination register's
// current value and this hands the same value straight back on that arm -
// the register ends up holding what it held, which is what the interpreter
// leaving it alone means. See the row.
std::int32_t aot_bridge::module_export_cell(aot::ct_aot_frame * f, const char * name,
                                            std::uint32_t name_len, std::uint64_t * out) {
    context & cx = *frame_of(f).ctx;
    const value published =
        cx.module_export_cell(std::string{name, name_len}, value::from_bits(*out));
    const std::int32_t status = check(f);
    if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = published.bits(); }
    return status;
}

// ct_aot_module_namespace. RAISE TIER like the import row, and a SAFEPOINT
// unlike it: context::module_namespace allocates the namespace object and
// one native getter per export. Safe under a real collector because the
// half-built object is stored into module_record::namespace_object - a GC
// root - before the accessor loop runs.
std::uint64_t aot_bridge::module_namespace(aot::ct_aot_frame * f, const char * specifier,
                                           std::uint32_t specifier_len) {
    context & cx = *frame_of(f).ctx;
    return cx.module_namespace_for(std::string{specifier, specifier_len}).bits();
}

// ct_aot_dynamic_import. The heaviest safepoint in the table: the
// specifier's toString and then a whole module graph, both user JavaScript.
//
// THE REFERRER COMES OFF THE FRAME, exactly as the row says and as the
// handler does. A literal could not be right: function_proto::module is
// stamped by the LOADER after compilation, so the compiler would be baking
// its guess at what the loader will call the file.
//
// THE REFERRER IS COPIED BEFORE THE CALL. frames_ is a vector and the
// loader pushes frames, so a reference into frames_[i].proto->module taken
// before it would dangle - and `module` is a std::string on a proto the
// loader may also be assigning to.
std::int32_t aot_bridge::dynamic_import(aot::ct_aot_frame * f, std::uint64_t specifier,
                                        std::uint64_t * out) {
    context & cx = *frame_of(f).ctx;
    const context::call_frame * record = frame_record(f);
    const std::string referrer =
        (record == nullptr || record->proto == nullptr) ? std::string{} : record->proto->module;
    const value produced = cx.dynamic_import(value::from_bits(specifier), referrer);
    const std::int32_t status = check(f);
    if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
    return status;
}

} // namespace ctbrowser::script

namespace ctbrowser::aot {

extern "C" {

// The `site` parameter is where Phase 26 attaches an inline cache without an
// ABI break. Taken and ignored, because the signature is the thing two code
// generators are written against and a parameter added later is a break.
std::int32_t ct_aot_construct(ct_aot_frame * fr, std::uint64_t callee, const std::uint64_t * argv,
                              std::uint32_t argc, const ct_aot_site * site, std::uint64_t * out) {
    return script::aot_bridge::construct(fr, callee, argv, argc, site, out);
}

std::uint64_t ct_aot_make_closure(ct_aot_frame * fr, std::uint64_t enclosing_closure,
                                  std::uint32_t function_index,
                                  const std::uint64_t * local_upvalues, std::uint32_t upvalue_count,
                                  std::uint64_t enclosing_this) {
    return script::aot_bridge::make_closure(fr, enclosing_closure, function_index, local_upvalues,
                                            upvalue_count, enclosing_this);
}

std::uint64_t ct_aot_wrap_promise(ct_aot_frame * fr, std::uint64_t v) {
    return script::aot_bridge::wrap_promise(fr, v);
}

std::uint64_t ct_aot_module_import_cell(ct_aot_frame * fr, const char * specifier,
                                        std::uint32_t specifier_len, const char * export_name,
                                        std::uint32_t export_name_len) {
    return script::aot_bridge::module_import_cell(fr, specifier, specifier_len, export_name,
                                                  export_name_len);
}

std::int32_t ct_aot_module_export_cell(ct_aot_frame * fr, const char * name, std::uint32_t name_len,
                                       std::uint64_t * out) {
    return script::aot_bridge::module_export_cell(fr, name, name_len, out);
}

std::uint64_t ct_aot_module_namespace(ct_aot_frame * fr, const char * specifier,
                                      std::uint32_t specifier_len) {
    return script::aot_bridge::module_namespace(fr, specifier, specifier_len);
}

std::int32_t ct_aot_dynamic_import(ct_aot_frame * fr, std::uint64_t specifier,
                                   std::uint64_t * out) {
    return script::aot_bridge::dynamic_import(fr, specifier, out);
}

std::int32_t ct_aot_call_spread(ct_aot_frame * fr, std::uint64_t callee, std::uint64_t arg_array,
                                std::uint64_t receiver, const ct_aot_site * site,
                                std::uint64_t * out) {
    return script::aot_bridge::call_spread(fr, callee, arg_array, receiver, site, out);
}

std::int32_t ct_aot_construct_spread(ct_aot_frame * fr, std::uint64_t callee,
                                     std::uint64_t arg_array, const ct_aot_site * site,
                                     std::uint64_t * out) {
    return script::aot_bridge::construct_spread(fr, callee, arg_array, site, out);
}

} // extern "C"

} // namespace ctbrowser::aot
