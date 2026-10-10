//===- calls.cpp - AOT helpers for calls, closures and modules ------------===//
//
// The AOT runtime helpers for calls beyond ct_aot_call: construct, closures,
// the spread forms, promise wrapping, and the four module helpers. Each
// extern "C" function implements one row of aot_helpers.def; shared
// declarations are in internal.hpp.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::aot {

extern "C" {

/// ct_aot_construct: `new callee(...args)`, through the same
/// context::construct_new that op::construct dispatches to.
///
/// This preserves the plain opcode's pending-new-target handling and
/// diagnostic. Both construction paths reject non-constructors with a
/// catchable TypeError.
///
/// `site` is this frame's own function_proto, which names the function in that
/// TypeError. There is no origin scan to go with it (callee_origin walks
/// emitted bytecode from an ip and a register index, and an AOT frame has
/// neither), so describe_callee renders the callee as "the value".
std::int32_t ct_aot_construct(ct_aot_frame * f, std::uint64_t callee, const std::uint64_t * argv,
                              std::uint32_t argc, const ct_aot_site * site, std::uint64_t * out) {
    const aot_frame_storage & held = aot_bridge::frame_of(f);
    context & cx = *held.ctx;
    // Copied out before the call, for ct_aot_call's reason: argv points into
    // registers_ for a compiled body, and the call may resize it.
    std::vector<value> args;
    args.reserve(argc);
    for (std::uint32_t i = 0; i < argc; ++i) { args.push_back(value::from_bits(argv[i])); }

    const function_proto & from = *reinterpret_cast<const function_proto *>(site);
    const value produced = cx.construct_new(value::from_bits(callee), args, from);
    const std::int32_t status = aot_bridge::check(f);
    if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
    return status;
}

/// ct_aot_make_closure. The shared context::make_closure, with the ABI's
/// parallel upvalue array.
///
/// An enclosing value that is not a closure becomes nullptr rather than being
/// cast: the top level has no enclosing closure and passes undefined, and
/// make_closure reads `enclosing->owner` to choose the program.
///
/// Failures are reported only by raising on the context, never by a status
/// (the "raise tier" of calls-and-modules.def), from three sources: the
/// allocation ceiling, no program when there is no enclosing closure, or a
/// function index or upvalue count that does not match. The caller polls
/// ct_aot_failed; there is no status to return because the row answers with a
/// value.
std::uint64_t ct_aot_make_closure(ct_aot_frame * f, std::uint64_t enclosing_closure,
                                  std::uint32_t function_index,
                                  const std::uint64_t * local_upvalues, std::uint32_t upvalue_count,
                                  std::uint64_t enclosing_this) {
    context & cx = *aot_bridge::frame_of(f).ctx;
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

/// ct_aot_wrap_promise. Wraps an async function's return value in a promise.
/// This is the one part of async that never suspends, so it works without any
/// suspension support: an async function with no `await` compiles to a body
/// using this helper and no await_value.
///
/// Fails only by raising (the "raise tier"): the row's may_throw comes only
/// from the allocation ceiling, which is uncatchable and unwinds nothing. So
/// this returns a value plainly, never a status. allocate() raises past the
/// ceiling and still returns a well-formed object, so the result is always
/// written. Returning a bare 0 on failure would hand back value::from_bits(0),
/// which is not undefined, and the lowering parks the result as a GC root and
/// traces it.
///
/// may_reenter is 0, verified against the installed factory rather than the VM
/// stub: detail::make_promise is new_table, a prototype, four sets and a
/// make_array, with no call() anywhere.
std::uint64_t ct_aot_wrap_promise(ct_aot_frame * f, std::uint64_t v) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    return cx.wrap_in_promise(value::from_bits(v)).bits();
}

/// ct_aot_call_spread and ct_aot_construct_spread: the two halves of
/// VM_CASE(apply), through the shared context members.
///
/// Unlike ct_aot_call there is no argument window and no argc. The arguments
/// arrive as an array because their count was not known until the spread ran,
/// so the array is one already-rooted value and there is nothing to copy out
/// or park.
///
/// Both ignore `site`, as ct_aot_call ignores its own: the messages these paths
/// raise are the interpreter's, and neither names the enclosing function. It is
/// taken so the ABI need not change if one of them ever does.
std::int32_t ct_aot_call_spread(ct_aot_frame * f, std::uint64_t callee, std::uint64_t arg_array,
                                std::uint64_t receiver, const ct_aot_site * site,
                                std::uint64_t * out) {
    (void)site;
    context & cx = *aot_bridge::frame_of(f).ctx;
    const value produced = cx.call_spread(value::from_bits(callee), value::from_bits(arg_array),
                                          value::from_bits(receiver));
    const std::int32_t status = aot_bridge::check(f);
    if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
    return status;
}

std::int32_t ct_aot_construct_spread(ct_aot_frame * f, std::uint64_t callee,
                                     std::uint64_t arg_array, const ct_aot_site * site,
                                     std::uint64_t * out) {
    (void)site;
    context & cx = *aot_bridge::frame_of(f).ctx;
    const value produced =
        cx.construct_spread(value::from_bits(callee), value::from_bits(arg_array));
    const std::int32_t status = aot_bridge::check(f);
    if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
    return status;
}

// ---- Module helpers ----------------------------------------------------
//
// Each delegates to the same context member that VM_CASE(load_import),
// VM_CASE(bind_export), VM_CASE(load_namespace) and VM_CASE(dyn_import) call,
// so the interpreter and compiled code cannot resolve a specifier, raise a
// message or adopt a cell differently.
//
// Building a std::string per call is deliberate. modules_,
// module_record::resolved and module_record::exports are plain
// flat_map<std::string, ...>, and as core/containers.hpp explains,
// `flat_map<std::string, V>::find` takes the key type: there is no
// heterogeneous overload without string_hash, which these maps do not use.
// The interpreter builds the key too; it simply has one already, from the
// constant pool. Each of these opcodes runs once per binding in a module
// prologue, never in a loop, so the allocation is not worth a wider map.

/// ct_aot_module_import_cell. Fails only by raising on the context (the
/// "raise tier"): it answers the cell rather than a status, so on either miss
/// the value is undefined and the caller polls ct_aot_failed at a back edge.
/// Not a safepoint: it allocates nothing.
std::uint64_t ct_aot_module_import_cell(ct_aot_frame * f, const char * specifier,
                                        std::uint32_t specifier_len, const char * export_name,
                                        std::uint32_t export_name_len) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    return cx
        .module_import_cell(std::string{specifier, specifier_len},
                            std::string{export_name, export_name_len})
        .bits();
}

/// ct_aot_module_export_cell. The caller seeds *out with the destination
/// register's current value. When no module is being evaluated the helper hands
/// that value straight back, which matches the interpreter leaving the register
/// alone; ct_aot_status has no "no write" member (the row's CT_AOT_NO_WRITE),
/// so this is how the conditional write is expressed. See the entry in
/// ctbrowser/include/ctbrowser/aot/helpers/calls-and-modules.def.
std::int32_t ct_aot_module_export_cell(ct_aot_frame * f, const char * name, std::uint32_t name_len,
                                       std::uint64_t * out) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    const value published =
        cx.module_export_cell(std::string{name, name_len}, value::from_bits(*out));
    const std::int32_t status = aot_bridge::check(f);
    if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = published.bits(); }
    return status;
}

/// ct_aot_module_namespace. Fails only by raising, like the import helper, but
/// unlike it this is a safepoint: context::module_namespace allocates the
/// namespace object and one native getter per export. That is safe because
/// the half-built object is stored in module_record::namespace_object, a GC
/// root, before the getters are made.
std::uint64_t ct_aot_module_namespace(ct_aot_frame * f, const char * specifier,
                                      std::uint32_t specifier_len) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    return cx.module_namespace_for(std::string{specifier, specifier_len}).bits();
}

/// ct_aot_dynamic_import: `import(specifier)`. The heaviest safepoint in the
/// table: the specifier's toString and then a whole module graph, both of which
/// run user JavaScript.
///
/// The referrer comes off the frame, as the row says and as the interpreter's
/// handler does. A literal could not be right: function_proto::module is
/// stamped by the loader after compilation, so the compiler would be baking in
/// a guess at what the loader will call the file.
///
/// The referrer is copied before the call. frames_ is a vector and the loader
/// pushes frames, so a reference into frames_[i].proto->module taken before
/// it would dangle, and `module` is a std::string on a proto the loader may
/// also be assigning to.
std::int32_t ct_aot_dynamic_import(ct_aot_frame * f, std::uint64_t specifier, std::uint64_t * out) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    const context::call_frame * record = aot_bridge::frame_record(f);
    const std::string referrer =
        (record == nullptr || record->proto == nullptr) ? std::string{} : record->proto->module;
    const value produced = cx.dynamic_import(value::from_bits(specifier), referrer);
    const std::int32_t status = aot_bridge::check(f);
    if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
    return status;
}

} // extern "C"

} // namespace ctbrowser::aot
